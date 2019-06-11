/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoProblemRep.cpp                                           *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2017 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Christopher Dembia, Nicholas Bianco                             *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "MocoProblemRep.h"

#include "Components/AccelerationMotion.h"
#include "Components/DiscreteForces.h"
#include "MocoProblem.h"
#include <regex>
#include <unordered_set>

using namespace OpenSim;

MocoProblemRep::MocoProblemRep(const MocoProblem& problem)
        : m_problem(&problem) {
    initialize();
}
void MocoProblemRep::initialize() {

    m_state_infos.clear();
    m_control_infos.clear();
    m_parameters.clear();
    m_costs.clear();
    m_path_constraints.clear();
    m_kinematic_constraints.clear();
    m_multiplier_infos_map.clear();

    if (!getTimeInitialBounds().isSet() && !getTimeFinalBounds().isSet()) {
        std::cout << "Warning: no time bounds set." << std::endl;
    }

    const auto& ph0 = m_problem->getPhase(0);
    m_model_base = ph0.getModel();
    m_state_base = m_model_base.initSystem();

    // We would like to eventually compute the model accelerations through
    // realizing to Stage::Acceleration. However, if the model has constraints,
    // realizing to Stage::Acceleration will cause Simbody to compute it's own
    // Lagrange multipliers which will not necessarily be consistent with the
    // multipliers provided by a solver. Therefore, we'll create a copy of the
    // original model, disable the its constraints, and apply the constraint
    // forces equivalent to the solver's Lagrange multipliers before computing
    // the accelerations.
    m_model_disabled_constraints = Model(m_model_base);
    // The constraint forces will be applied to the copied model via an
    // OpenSim::DiscreteForces component, a thin wrapper to Simbody's
    // DiscreteForces class, which adds discrete variables to the state.
    auto constraintForcesUPtr = make_unique<DiscreteForces>();
    constraintForcesUPtr->setName("constraint_forces");
    m_constraint_forces.reset(constraintForcesUPtr.get());
    m_model_disabled_constraints.addComponent(constraintForcesUPtr.release());

    // The Acceleration motion is always added, but is only enabled by solvers
    // if using an implicit dynamics mode. We use this motion to ensure that
    // joint reaction forces can be computed correctly from the solver-supplied
    // UDot (otherwise, Simbody will compute its own "incorrect" UDot using
    // forward dynamics).
    auto accelMotionUPtr = make_unique<AccelerationMotion>("motion");
    m_acceleration_motion.reset(accelMotionUPtr.get());
    m_model_disabled_constraints.addModelComponent(accelMotionUPtr.release());
    // Grab a writable state from the copied model -- we'll use this to disable
    // its constraints below.
    m_state_disabled_constraints = m_model_disabled_constraints.initSystem();

    // Get property values for constraints and Lagrange multipliers.
    const auto& kcBounds = ph0.get_kinematic_constraint_bounds();
    const MocoBounds& multBounds = ph0.get_multiplier_bounds();
    MocoInitialBounds multInitBounds(
            multBounds.getLower(), multBounds.getUpper());
    MocoFinalBounds multFinalBounds(
            multBounds.getLower(), multBounds.getUpper());
    // Get model information to loop through constraints.
    const auto& matter = m_model_base.getMatterSubsystem();
    auto& matterDisabledConstraints =
            m_model_disabled_constraints.updMatterSubsystem();
    const auto NC = matter.getNumConstraints();
    const auto& state = m_model_base.getWorkingState();
    int mp, mv, ma;
    m_num_kinematic_constraint_equations = 0;
    for (SimTK::ConstraintIndex cid(0); cid < NC; ++cid) {
        const SimTK::Constraint& constraint = matter.getConstraint(cid);
        SimTK::Constraint& constraintToDisable =
                matterDisabledConstraints.updConstraint(cid);
        if (!constraint.isDisabled(state)) {
            constraint.getNumConstraintEquationsInUse(state, mp, mv, ma);
            MocoKinematicConstraint kc(cid, mp, mv, ma);

            // Set the bounds for this kinematic constraint based on the
            // property.
            MocoConstraintInfo kcInfo = kc.getConstraintInfo();
            std::vector<MocoBounds> kcBoundVec(
                    kc.getConstraintInfo().getNumEquations(), kcBounds);
            kcInfo.setBounds(kcBoundVec);
            kc.setConstraintInfo(kcInfo);

            // Update number of scalar kinematic constraint equations.
            m_num_kinematic_constraint_equations +=
                    kc.getConstraintInfo().getNumEquations();

            // Append this kinematic constraint to the internal vector variable.
            // TODO: Avoid copies when the vector needs to be resized.
            m_kinematic_constraints.push_back(kc);

            // Add variable infos for all Lagrange multipliers in the problem.
            // Multipliers are only added based on the number of holonomic,
            // nonholonomic, or acceleration kinematic constraints and are *not*
            // based on the number for derivatives of holonomic or nonholonomic
            // constraint equations.
            // TODO how to name multiplier variables?
            std::vector<MocoVariableInfo> multInfos;
            for (int i = 0; i < mp; ++i) {
                MocoVariableInfo info("lambda_cid" + std::to_string(cid) +
                                              "_p" + std::to_string(i),
                        multBounds, multInitBounds, multFinalBounds);
                multInfos.push_back(info);
            }
            for (int i = 0; i < mv; ++i) {
                MocoVariableInfo info("lambda_cid" + std::to_string(cid) +
                                              "_v" + std::to_string(i),
                        multBounds, multInitBounds, multFinalBounds);
                multInfos.push_back(info);
            }
            for (int i = 0; i < ma; ++i) {
                MocoVariableInfo info("lambda_cid" + std::to_string(cid) +
                                              "_a" + std::to_string(i),
                        multBounds, multInitBounds, multFinalBounds);
                multInfos.push_back(info);
            }
            m_multiplier_infos_map.insert({kcInfo.getName(), multInfos});

            // Disable this constraint in the copied model.
            constraintToDisable.disable(m_state_disabled_constraints);
        }
    }

    // Verify that the constraint error vectors in the state associated with the
    // copied model are empty.
    m_model_disabled_constraints.getSystem().realize(
            m_state_disabled_constraints, SimTK::Stage::Instance);
    OPENSIM_THROW_IF(m_state_disabled_constraints.getNQErr() != 0 ||
                             m_state_disabled_constraints.getNUErr() != 0 ||
                             m_state_disabled_constraints.getNUDotErr() != 0,
            Exception, "Internal error.");

    // State infos.
    // ------------
    const auto stateNames = m_model_base.getStateVariableNames();
    for (int i = 0; i < ph0.getProperty_state_infos_pattern().size(); ++i) {
        const auto& pattern =
                std::regex(ph0.get_state_infos_pattern(i).getName());
        for (int j = 0; j < stateNames.size(); ++j) {
            if (std::regex_match(stateNames[j], pattern)) {
                m_state_infos[stateNames[j]] = ph0.get_state_infos_pattern(i);
                m_state_infos[stateNames[j]].setName(stateNames[j]);
            }
        }
    }
    for (int i = 0; i < ph0.getProperty_state_infos().size(); ++i) {
        const auto& name = ph0.get_state_infos(i).getName();
        OPENSIM_THROW_IF(stateNames.findIndex(name) == -1, Exception,
                format("State info provided for nonexistent state '%s'.",
                        name));
    }

    // Create internal record of state and control infos, automatically
    // populated from coordinates and actuators.
    for (int i = 0; i < ph0.getProperty_state_infos().size(); ++i) {
        const auto& name = ph0.get_state_infos(i).getName();
        m_state_infos[name] = ph0.get_state_infos(i);
    }

    // TODO this code is from an upcoming commit that hasn't been merged yet.
    // I've left it here in case there's confusion of its placement. Uncomment
    // when the prescribed kinematics updates catch up.
    // if (!m_prescribedKinematics) {
    for (const auto& coord : m_model_base.getComponentList<Coordinate>()) {
        const auto stateVarNames = coord.getStateVariableNames();
        {
            const std::string coordValueName = stateVarNames[0];
            // TODO document: Range used even if not clamped.
            if (m_state_infos.count(coordValueName) == 0) {
                const auto info = MocoVariableInfo(coordValueName, {}, {}, {});
                m_state_infos[coordValueName] = info;
            }
            if (!m_state_infos[coordValueName].getBounds().isSet()) {
                m_state_infos[coordValueName].setBounds(
                        {coord.getRangeMin(), coord.getRangeMax()});
            }
        }
        {
            const std::string coordSpeedName = stateVarNames[1];
            if (m_state_infos.count(coordSpeedName) == 0) {
                const auto info = MocoVariableInfo(coordSpeedName, {}, {}, {});
                m_state_infos[coordSpeedName] = info;
            }
            if (!m_state_infos[coordSpeedName].getBounds().isSet()) {
                m_state_infos[coordSpeedName].setBounds(
                        ph0.get_default_speed_bounds());
            }
        }
    }
    //}

    // Control infos.
    // --------------
    auto controlNames = createControlNamesFromModel(m_model_base);
    for (int i = 0; i < ph0.getProperty_control_infos_pattern().size(); ++i) {
        const auto& pattern = ph0.get_control_infos_pattern(i).getName();
        auto regexPattern = std::regex(pattern);
        for (int j = 0; j < (int)controlNames.size(); ++j) {
            if (std::regex_match(controlNames[j], regexPattern)) {
                m_control_infos[controlNames[j]] =
                        ph0.get_control_infos_pattern(i);
                m_control_infos[controlNames[j]].setName(controlNames[j]);
            }
        }
    }
    for (int i = 0; i < ph0.getProperty_control_infos().size(); ++i) {
        const auto& name = ph0.get_control_infos(i).getName();
        auto it = std::find(controlNames.begin(), controlNames.end(), name);
        OPENSIM_THROW_IF(it == controlNames.end(), Exception,
                format("Control info provided for nonexistent or disabled "
                       "actuator '%s'.",
                        name));
    }

    for (int i = 0; i < ph0.getProperty_control_infos().size(); ++i) {
        const auto& name = ph0.get_control_infos(i).getName();
        m_control_infos[name] = ph0.get_control_infos(i);
    }

    // Loop through all the actuators in the model and create control infos
    // for the associated actuator control variables.
    for (const auto& actu : m_model_base.getComponentList<Actuator>()) {
        const std::string actuName = actu.getAbsolutePathString();
        if (actu.numControls() == 1) {
            // No control info exists; add one.
            if (m_control_infos.count(actuName) == 0) {
                const auto info = MocoVariableInfo(actuName, {}, {}, {});
                m_control_infos[actuName] = info;
            }
            if (!m_control_infos[actuName].getBounds().isSet()) {
                // If this scalar actuator derives from OpenSim::ScalarActuator,
                // use the getMinControl() and getMaxControl() methods to set
                // the bounds. Otherwise, set the bounds to (-inf, inf).
                if (const auto* scalarActu =
                                dynamic_cast<const ScalarActuator*>(&actu)) {
                    m_control_infos[actuName].setBounds(
                            {scalarActu->getMinControl(),
                                    scalarActu->getMaxControl()});
                } else {
                    m_control_infos[actuName].setBounds(
                            MocoBounds::unconstrained());
                }
            }
            if (ph0.get_bound_activation_from_excitation()) {
                const auto* muscle = dynamic_cast<const Muscle*>(&actu);
                if (muscle && !muscle->get_ignore_activation_dynamics()) {
                    auto& info = m_state_infos[actuName + "/activation"];
                    if (!info.getBounds().isSet()) {
                        info.setBounds(m_control_infos[actuName].getBounds());
                    }
                }
            }
        } else {
            // This is a non-scalar actuator, so we need to add multiple
            // control infos.
            for (int idx = 0; idx < actu.numControls(); ++idx) {
                std::string controlName = actuName + "_" + std::to_string(idx);
                if (m_control_infos.count(controlName) == 0) {
                    const auto info = MocoVariableInfo(controlName, {}, {}, {});
                    m_control_infos[controlName] = info;
                }
                if (!m_control_infos[controlName].getBounds().isSet()) {
                    m_control_infos[controlName].setBounds(
                            MocoBounds::unconstrained());
                }
            }
        }
    }

    // Parameters.
    // -----------
    m_parameters.resize(ph0.getProperty_parameters().size());
    std::unordered_set<std::string> paramNames;
    for (int i = 0; i < ph0.getProperty_parameters().size(); ++i) {
        const auto& param = ph0.get_parameters(i);
        OPENSIM_THROW_IF(param.getName().empty(), Exception,
                "All parameters must have a name.");
        OPENSIM_THROW_IF(paramNames.count(param.getName()), Exception,
                format("A parameter with name '%s' already exists.",
                        param.getName()));
        paramNames.insert(param.getName());
        m_parameters[i] = std::unique_ptr<MocoParameter>(param.clone());
        // We must initialize on both models so that they are consistent
        // when parameters are updated when applyParameterToModel() is
        // called. Calling initalizeOnModel() twice here is fine since the
        // models are identical aside from disabled Simbody constraints. The
        // property references to the parameters in both models are added to
        // the MocoParameter's internal vector of property references.
        m_parameters[i]->initializeOnModel(m_model_base);
        m_parameters[i]->initializeOnModel(m_model_disabled_constraints);
    }

    // Costs.
    // ------
    m_costs.resize(ph0.getProperty_costs().size());
    std::unordered_set<std::string> costNames;
    for (int i = 0; i < ph0.getProperty_costs().size(); ++i) {
        const auto& cost = ph0.get_costs(i);
        OPENSIM_THROW_IF(cost.getName().empty(), Exception,
                "All costs must have a name.");
        OPENSIM_THROW_IF(costNames.count(cost.getName()), Exception,
                format("A cost with name '%s' already exists.",
                        cost.getName()));
        costNames.insert(cost.getName());
        m_costs[i] = std::unique_ptr<MocoCost>(cost.clone());
        m_costs[i]->initializeOnModel(m_model_disabled_constraints);
    }

    // Auxiliary path constraints.
    // ---------------------------
    m_num_path_constraint_equations = 0;
    m_path_constraints.resize(ph0.getProperty_path_constraints().size());
    std::unordered_set<std::string> pcNames;
    for (int i = 0; i < ph0.getProperty_path_constraints().size(); ++i) {
        const auto& pc = ph0.get_path_constraints(i);
        OPENSIM_THROW_IF(
                pc.getName().empty(), Exception, "All costs must have a name.");
        OPENSIM_THROW_IF(pcNames.count(pc.getName()), Exception,
                format("A constraint with name '%s' already exists.",
                        pc.getName()));
        pcNames.insert(pc.getName());
        m_path_constraints[i] = std::unique_ptr<MocoPathConstraint>(pc.clone());
        m_path_constraints[i]->initializeOnModel(
                m_model_disabled_constraints, m_num_path_constraint_equations);
        m_num_path_constraint_equations +=
                m_path_constraints[i]->getConstraintInfo().getNumEquations();
    }
}

const std::string& MocoProblemRep::getName() const {
    return m_problem->getName();
}
MocoInitialBounds MocoProblemRep::getTimeInitialBounds() const {
    return m_problem->getPhase(0).get_time_initial_bounds();
}
MocoFinalBounds MocoProblemRep::getTimeFinalBounds() const {
    return m_problem->getPhase(0).get_time_final_bounds();
}
std::vector<std::string> MocoProblemRep::createStateInfoNames() const {
    std::vector<std::string> names(m_state_infos.size());
    int i = 0;
    for (const auto& info : m_state_infos) {
        names[i] = info.first;
        ++i;
    }
    return names;
}
std::vector<std::string> MocoProblemRep::createControlInfoNames() const {
    std::vector<std::string> names(m_control_infos.size());
    int i = 0;
    for (const auto& info : m_control_infos) {
        names[i] = info.first;
        ++i;
    }
    return names;
}
std::vector<std::string> MocoProblemRep::createMultiplierInfoNames() const {
    std::vector<std::string> names;
    for (const auto& kc : m_kinematic_constraints) {
        const auto& infos =
                m_multiplier_infos_map.at(kc.getConstraintInfo().getName());
        for (const auto& info : infos) { names.push_back(info.getName()); }
    }
    return names;
}
std::vector<std::string>
MocoProblemRep::createKinematicConstraintNames() const {
    std::vector<std::string> names(m_kinematic_constraints.size());
    // Kinematic constraint names are stored in the internal constraint
    // info.
    for (int i = 0; i < (int)m_kinematic_constraints.size(); ++i) {
        names[i] = m_kinematic_constraints[i].getConstraintInfo().getName();
    }
    return names;
}
std::vector<std::string> MocoProblemRep::createParameterNames() const {
    std::vector<std::string> names(m_parameters.size());
    int i = 0;
    for (const auto& param : m_parameters) {
        names[i] = param->getName();
        ++i;
    }
    return names;
}
std::vector<std::string> MocoProblemRep::createPathConstraintNames() const {
    std::vector<std::string> names(m_path_constraints.size());
    int i = 0;
    for (const auto& pc : m_path_constraints) {
        names[i] = pc->getName();
        ++i;
    }
    return names;
}
const MocoVariableInfo& MocoProblemRep::getStateInfo(
        const std::string& name) const {
    OPENSIM_THROW_IF(m_state_infos.count(name) == 0, Exception,
            format("No info available for state '%s'.", name));
    return m_state_infos.at(name);
}
const MocoVariableInfo& MocoProblemRep::getControlInfo(
        const std::string& name) const {
    OPENSIM_THROW_IF(m_control_infos.count(name) == 0, Exception,
            format("No info available for control '%s'.", name));
    return m_control_infos.at(name);
}
const MocoParameter& MocoProblemRep::getParameter(
        const std::string& name) const {

    for (const auto& param : m_parameters) {
        if (param->getName() == name) { return *param.get(); }
    }
    OPENSIM_THROW(
            Exception, format("No parameter with name '%s' found.", name));
}
const MocoPathConstraint& MocoProblemRep::getPathConstraint(
        const std::string& name) const {

    for (const auto& pc : m_path_constraints) {
        if (pc->getName() == name) { return *pc.get(); }
    }
    OPENSIM_THROW(Exception,
            format("No path constraint with name '%s' found.", name));
}
const MocoPathConstraint& MocoProblemRep::getPathConstraintByIndex(
        int index) const {
    return *m_path_constraints[index];
}
const MocoKinematicConstraint& MocoProblemRep::getKinematicConstraint(
        const std::string& name) const {

    // Kinematic constraint names are stored in the internal constraint
    // info.
    for (const auto& kc : m_kinematic_constraints) {
        if (kc.getConstraintInfo().getName() == name) { return kc; }
    }
    OPENSIM_THROW(Exception,
            format("No kinematic constraint with name '%s' found.", name));
}
const std::vector<MocoVariableInfo>& MocoProblemRep::getMultiplierInfos(
        const std::string& kinematicConstraintInfoName) const {

    auto search = m_multiplier_infos_map.find(kinematicConstraintInfoName);
    if (search != m_multiplier_infos_map.end()) {
        return m_multiplier_infos_map.at(kinematicConstraintInfoName);
    } else {
        OPENSIM_THROW(Exception, format("No variable infos for kinematic "
                                        "constraint info with "
                                        "name '%s' found.",
                                         kinematicConstraintInfoName));
    }
}

void MocoProblemRep::applyParametersToModelProperties(
        const SimTK::Vector& parameterValues,
        bool initSystemAndDisableConstraints) const {
    OPENSIM_THROW_IF(parameterValues.size() != (int)m_parameters.size(),
            Exception,
            format("There are %i parameters in "
                   "this MocoProblem, but %i values were provided.",
                    m_parameters.size(), parameterValues.size()));
    for (int i = 0; i < (int)m_parameters.size(); ++i) {
        m_parameters[i]->applyParameterToModelProperties(parameterValues(i));
    }
    if (initSystemAndDisableConstraints) {
        // TODO: Avoid these const_casts.
        const_cast<Model&>(m_model_base).initSystem();

        Model& m_model_disabled_constraints_const_cast =
                const_cast<Model&>(m_model_disabled_constraints);
        m_state_disabled_constraints =
                m_model_disabled_constraints_const_cast.initSystem();

        // Re-disable constraints if they were enabled by the previous
        // initSystem() call.
        auto& matterDisabledConstraints =
                m_model_disabled_constraints_const_cast.updMatterSubsystem();
        const auto NC = matterDisabledConstraints.getNumConstraints();
        for (SimTK::ConstraintIndex cid(0); cid < NC; ++cid) {
            SimTK::Constraint& constraintToDisable =
                    matterDisabledConstraints.updConstraint(cid);
            if (!constraintToDisable.isDisabled(m_state_disabled_constraints)) {
                constraintToDisable.disable(m_state_disabled_constraints);
            }
        }
    }
}

void MocoProblemRep::printDescription(std::ostream& stream) const {
    stream << "Costs:";
    if (m_costs.empty())
        stream << " none";
    else
        stream << " (total: " << m_costs.size() << ")";
    stream << "\n";
    for (const auto& cost : m_costs) {
        stream << "  ";
        cost->printDescription(stream);
    }

    stream << "Kinematic constraints: ";
    if (m_kinematic_constraints.empty())
        stream << " none";
    else
        stream << " (total: " << m_kinematic_constraints.size() << ")";
    stream << "\n";
    for (int i = 0; i < (int)m_kinematic_constraints.size(); ++i) {
        stream << "  ";
        m_kinematic_constraints[i].getConstraintInfo().printDescription(stream);
    }

    stream << "Path constraints:";
    if (m_path_constraints.empty())
        stream << " none";
    else
        stream << " (total: " << m_path_constraints.size() << ")";
    stream << "\n";
    for (const auto& pc : m_path_constraints) {
        stream << "  ";
        pc->getConstraintInfo().printDescription(stream);
    }

    stream << "States:";
    if (m_state_infos.empty())
        stream << " none";
    else
        stream << " (total: " << m_state_infos.size() << ")";
    stream << "\n";
    // TODO want to loop through the model's state variables and controls,
    // not just the infos.
    for (const auto& info : m_state_infos) {
        stream << "  ";
        info.second.printDescription(stream);
    }

    stream << "Controls:";
    if (m_control_infos.empty())
        stream << " none";
    else
        stream << " (total: " << m_control_infos.size() << "):";
    stream << "\n";
    for (const auto& info : m_control_infos) {
        stream << "  ";
        info.second.printDescription(stream);
    }

    stream << "Parameters:";
    if (m_parameters.empty())
        stream << " none";
    else
        stream << " (total: " << m_parameters.size() << "):";
    stream << "\n";
    for (const auto& param : m_parameters) {
        stream << "  ";
        param->printDescription(stream);
    }

    stream.flush();
}
