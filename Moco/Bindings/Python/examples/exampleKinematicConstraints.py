# -------------------------------------------------------------------------- #
# OpenSim Moco: exampleKinematicConstraints.py                               #
# -------------------------------------------------------------------------- #
# Copyright (c) 2020 Stanford University and the Authors                     #
#                                                                            #
# Author(s): Christopher Dembia                                              #
#                                                                            #
# Licensed under the Apache License, Version 2.0 (the "License"); you may    #
# not use this file except in compliance with the License. You may obtain a  #
# copy of the License at http://www.apache.org/licenses/LICENSE-2.0          #
#                                                                            #
# Unless required by applicable law or agreed to in writing, software        #
# distributed under the License is distributed on an "AS IS" BASIS,          #
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   #
# See the License for the specific language governing permissions and        #
# limitations under the License.                                             #
# -------------------------------------------------------------------------- #

# This example illustrates how Moco handles kinematic constraints with a model
# of a planar point mass with degrees of freedom tx and ty that is constrained
# to move along a parabola ty = tx^2. This example plots the trajectory of the
# point mass, which follows the parabola, and multiplies the kinematic
# constraint Jacobian and the Lagrange multipliers to visualize the constraint
# forces that Moco produces to ensure the point mass remains on the parabola.
# Lastly, this example computes the error in the equations of motion, showing
# how the constraint forces are necessary for satisfying the equations of
# motion.

import opensim as osim
import numpy as np

model = osim.Model()
model.setName('planar_point_mass')
g = abs(model.get_gravity()[1])

intermed = osim.Body('intermed', 0, osim.Vec3(0), osim.Inertia(0))
model.addBody(intermed);
mass = 1.0
body = osim.Body('body', mass, osim.Vec3(0), osim.Inertia(0))
model.addBody(body)

body.attachGeometry(osim.Sphere(0.05))

jointX = osim.SliderJoint('tx', model.getGround(), intermed)
coordX = jointX.updCoordinate(osim.SliderJoint.Coord_TranslationX)
coordX.setName('tx')
model.addJoint(jointX)

# The joint's x axis must point in the global "+y" direction.
jointY = osim.SliderJoint('ty',
                          intermed, osim.Vec3(0), osim.Vec3(0, 0, 0.5 * np.pi),
                          body, osim.Vec3(0), osim.Vec3(0, 0, .5 * np.pi))
coordY = jointY.updCoordinate(osim.SliderJoint.Coord_TranslationX)
coordY.setName('ty')
model.addJoint(jointY)

# Add the kinematic constraint ty = tx^2.
constraint = osim.CoordinateCouplerConstraint()
independentCoords = osim.ArrayStr()
independentCoords.append('tx')
constraint.setIndependentCoordinateNames(independentCoords)
constraint.setDependentCoordinateName('ty')
coefficients = osim.Vector(3, 0) # 3 elements initialized to 0.
# The polynomial is c(0)*tx^2 + c(1)*tx + c(2).
coefficients.set(0, 1) # Set the
constraint.setFunction(osim.PolynomialFunction(coefficients))
model.addConstraint(constraint)

study = osim.MocoStudy()
problem = study.updProblem()
problem.setModel(model)

phase0 = problem.getPhase(0)
phase0.setDefaultSpeedBounds(osim.MocoBounds(-5, 5))

problem.setTimeBounds(0, 0.8)
# Start the motion at (-1, 1).
problem.setStateInfo('/jointset/tx/tx/value', [-2, 2], -1.0)
problem.setStateInfo('/jointset/ty/ty/value', [-2, 2], 1.0)

solver = study.initCasADiSolver()
solution = study.solve()

has_pylab = False
try:
    import pylab as pl
    from scipy.interpolate import InterpolatedUnivariateSpline
    has_pylab = True
except:
    print('Skipping plotting')

if has_pylab:
    fig = pl.figure()
    ax = fig.add_subplot(1, 1, 1)
    ax.set_xlabel('tx')
    ax.set_ylabel('ty')

    time = solution.getTimeMat()
    tx = solution.getStateMat('/jointset/tx/tx/value')
    ty = solution.getStateMat('/jointset/ty/ty/value')
    multiplier = -solution.getMultiplierMat(solution.getMultiplierNames()[0])
    print('Number of multipliers: %i' % len(solution.getMultiplierNames()))

    ax.set_aspect('equal')
    pl.plot(tx, ty, color='black')

    # Compute generalized accelerations from the solution trajectory, for use
    # in computing the residual of the equations of motion.
    txSpline = InterpolatedUnivariateSpline(time, tx)
    # Evaluate the second derivative of the spline.
    accelx = txSpline(time, 2)
    tySpline = InterpolatedUnivariateSpline(time, ty)
    accely = tySpline(time, 2)

    model.initSystem()
    statesTraj = solution.exportToStatesTrajectory(model)
    matter = model.getMatterSubsystem()
    constraintJacobian = osim.Matrix()
    itime = 0
    while itime < statesTraj.getSize():
        state = statesTraj.get(itime)
        model.realizePosition(state)
        # Calculate the position-level constraint Jacobian, 2 x 1 (number of
        # degrees of freedom by number of constraints).
        # Pq = [2 * tx, -1]
        matter.calcPq(state, constraintJacobian)
        jac = np.array([constraintJacobian.get(0, 0),
                        constraintJacobian.get(1, 0)])
        # The constraint generates generalized forces G^T * lambda.
        constraintForces = jac * multiplier[itime]
        # Scale down the force vector so it can be easily visualized with the
        # point mass trajectory.
        constraintForcesViz = 0.010 * constraintForces
        pl.arrow(tx[itime], ty[itime], constraintForcesViz[0],
                 constraintForcesViz[1], width=0.005, color='blue')

        # Residual = F - ma
        residualx = +constraintForces[0] - mass * accelx[itime]
        residualy = -mass * g + constraintForces[1] - mass * accely[itime]
        print(
            'time %f: residuals: %f, %f' % (time[itime], residualx, residualy))

        itime += 10

    pl.show()


