/* -------------------------------------------------------------------------- *
 *                      Smith2018ContactMesh.cpp                              *
 * -------------------------------------------------------------------------- *
 * The OpenSim API is a toolkit for musculoskeletal modeling and simulation.  *
 * See http://opensim.stanford.edu and the NOTICE file for more information.  *
 * OpenSim is developed at Stanford University and supported by the US        *
 * National Institutes of Health (U54 GM072970, R24 HD065690) and by DARPA    *
 * through the Warrior Web program.                                           *
 *                                                                            *
 * Author(s): Colin Smith                                                     *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "Smith2018ContactMesh.h"
#include <OpenSim/Common/ScaleSet.h>
#include "OpenSim/Common/Object.h"
#include "OpenSim/Simulation/SimbodyEngine/Body.h"
#include "OpenSim/Simulation/Model/Model.h"
#include <OpenSim/Simulation/Model/PhysicalOffsetFrame.h>
#include "OpenSim/JAM/JAMUtilities.h"
 //#include "simmath/internal/common.h"
//#include "simmath/LinearAlgebra.h"
//#include "simmath/internal/ContactGeometry.h"
//#include "simmath/internal/OrientedBoundingBox.h"
//#include "simmath/internal/OBBTree.h"
#include <set>
#include <cmath>
#include <math.h>

using namespace OpenSim;

using std::set;

//=============================================================================
// CONSTRUCTOR
//=============================================================================

Smith2018ContactMesh::Smith2018ContactMesh() 
{
    setNull();
    constructProperties();
    _mesh_is_cached = false;
}

Smith2018ContactMesh::Smith2018ContactMesh(const std::string& name, 
    const std::string& mesh_file, const PhysicalFrame& parent_frame) :
    ContactGeometry (parent_frame)
{
    setNull();
    constructProperties();
    _mesh_is_cached = false;

    setName(name);
    set_mesh_file(mesh_file);
    updSocket<PhysicalFrame>("scale_frame").connect(parent_frame);
}

Smith2018ContactMesh::Smith2018ContactMesh(const std::string& name, 
    const std::string& mesh_file, const PhysicalFrame& frame,
    const SimTK::Vec3& location, const SimTK::Vec3& orientation) :
    ContactGeometry(location, orientation, frame)
{
    Smith2018ContactMesh(name, mesh_file, frame);
}

Smith2018ContactMesh::Smith2018ContactMesh(const std::string& name,
    const std::string& mesh_file, const PhysicalFrame& frame,
    const SimTK::Vec3& location, const SimTK::Vec3& orientation,
    bool use_variable_thickness,
    const std::string& mesh_back_file, 
    double min_thickness, double max_thickness) :
    ContactGeometry(location, orientation, frame)
{
    Smith2018ContactMesh(name, mesh_file, frame, location, orientation);
    set_use_variable_thickness(use_variable_thickness);
    set_mesh_back_file(mesh_back_file);
    set_min_thickness(min_thickness);
    set_max_thickness(max_thickness);
}

void Smith2018ContactMesh::setNull()
{
    setAuthors("Colin Smith");
    setReferences(
        "Smith, C. R., Won Choi, K., Negrut, D., & Thelen, D. G. (2018)."
        "Efficient computation of cartilage contact pressures within dynamic "
        "simulations of movement. Computer Methods in Biomechanics and "
        "Biomedical Engineering: Imaging & Visualization, 6(5), 491-498.");
}

void Smith2018ContactMesh::constructProperties()
{
    constructProperty_mesh_file("");
    constructProperty_elastic_modulus(1000000.0);
    constructProperty_poissons_ratio(0.5);
    constructProperty_thickness(0.005);
    constructProperty_use_variable_thickness(false);
    constructProperty_mesh_back_file("");
    constructProperty_min_thickness(0.001);
    constructProperty_max_thickness(0.01);
    constructProperty_scale_factors(SimTK::Vec3(1.0));
}

void Smith2018ContactMesh::extendScale(
    const SimTK::State& s, const ScaleSet& scaleSet)
{
    Super::extendScale(s, scaleSet);
    SimTK::Vec3 scale_factors = getScaleFactors(scaleSet,
        getConnectee<PhysicalFrame>("scale_frame"));

    set_scale_factors(scale_factors);
    _mesh_is_cached = false;
}

void Smith2018ContactMesh::extendFinalizeFromProperties() {
    Super::extendFinalizeFromProperties();
    if (!_mesh_is_cached) {
        initializeMesh();
    }
    
    //Create Decorative Mesh
    if (!isObjectUpToDateWithProperties()) {
        _decorative_mesh.reset(
            new SimTK::DecorativeMeshFile(_full_mesh_file_path.c_str()));
        _decorative_mesh->setScaleFactors(get_scale_factors());
    }
}

void Smith2018ContactMesh::extendConnectToModel(Model& model)
{
    Super::extendConnectToModel(model);

    //Add in a local frame for the mesh reprsenting the resulting translation
    // and orientation from this component's parent frame

    PhysicalOffsetFrame* mesh_frame = 
        new PhysicalOffsetFrame(getFrame(), SimTK::Transform());

    mesh_frame->setName("mesh_frame");
    mesh_frame->set_translation(get_location());
    mesh_frame->set_orientation(get_orientation());

    adoptSubcomponent(mesh_frame);
    setNextSubcomponentInSystem(*mesh_frame);
}

std::string Smith2018ContactMesh::findMeshFile(const std::string& file)
{
    /*This is a modified version of the code found in Geometry.cpp
    Mesh::extendFinalizeFromProperties to find geometry files in
    the same directory as the modelDir, modelDir/Geometry, or 
    the installDir: OPENSIM_HOME/Geometry

    It plays some games to figure out the modelDir, because the
    Smith2018ContactMesh can't call getModel() at this stage
    */

    //bool isAbsolutePath; 
    //std::string directory, fileName, extension;
    //
    //SimTK::Pathname::deconstructPathname(file, isAbsolutePath, directory,
    //    fileName, extension);
    //const std::string lowerExtension = SimTK::String::toLower(extension);
    //
    ////Check for correct extension
    //if (lowerExtension != ".vtp" && lowerExtension != ".obj" && 
    //    lowerExtension != ".stl") {

    //    log_error("Smith2018ContactMesh ERROR: '" + file +
    //        "'; only .vtp .stl and .obj files currently supported.\n");

    //    OPENSIM_THROW(Exception,"Smith2018ContactMesh: Bad file type.");
    //}

    //// Find OpenSim modelDir
    //const Component* rootModel = nullptr;
    //if (!hasOwner()) {
    //    log_error("Mesh {} not connected to model...ignoring",
    //                get_mesh_file());
    //        return file;   // Orphan Mesh not part of a model yet
    //}
    //const Component* parent = &getOwner();
    //while (parent != nullptr) {
    //    if (dynamic_cast<const Model*>(parent) != nullptr) {
    //        rootModel = parent;
    //        break;
    //    }
    //    if (parent->hasOwner())
    //        parent = &(parent->getOwner()); // traverse up Component tree
    //    else
    //        break; // can't traverse up.
    //}

    //if (rootModel == nullptr) {
    //    log_error("Mesh {} not connected to model...ignoring",
    //                get_mesh_file());
    //        return file;   // Orphan Mesh not descendant of a model
    //}
    ////const Model& model = dynamic_cast<const Model&>(*rootModel);
    //std::string osimFileName = rootModel->getDocumentFileName();
    //

    ////Find geometry file
    //Model model;
    //model.setInputFileName(osimFileName);
    //
    //SimTK::Array_<std::string> attempts;

    //bool foundIt = ModelVisualizer::findGeometryFile(model,
    //    file, isAbsolutePath, attempts);

    //if (!foundIt) {
    //    log_error("Model: {}",osimFileName);

    //    log_error("Smith2018ContactMesh couldn't find file '" +
    //        file + "'; tried");

    //    for (unsigned i = 0; i < attempts.size(); ++i) {
    //        log_error(attempts[i]);
    //    }

    //    if (!isAbsolutePath && 
    //        !SimTK::Pathname::environmentVariableExists("OPENSIM_HOME")) {
    //        
    //        log_error("Set environment variable OPENSIM_HOME " 
    //            "to search $OPENSIM_HOME/Geometry.");
    //    }

    //    throw OpenSim::Exception("Smith2018ContactMesh: " + getName() +
    //        "File NOT found: " + file);
    //}

    // File is a .vtp, .stl, or .obj; attempt to find it.
    const Component* rootModel = nullptr;
        if (!hasOwner()) {
             log_error("Smith2018ContactMesh {} not connected to model...ignoring",
                    getName());
             OPENSIM_THROW(Exception, "");
            //return "";   // Orphan Mesh not part of a model yet
        }
        const Component* owner = &getOwner();
        while (owner != nullptr) {
            if (dynamic_cast<const Model*>(owner) != nullptr) {
                rootModel = owner;
                break;
            }
            if (owner->hasOwner())
                owner = &(owner->getOwner()); // traverse up Component tree
            else
                break; // can't traverse up.
        }

        if (rootModel == nullptr) {
             log_error("Smith2018ContactMesh {} not connected to model...ignoring",
                    getName());
             OPENSIM_THROW(Exception, "");
            //return "";   // Orphan Mesh not descendant of a model
        }

        // Current interface to Visualizer calls generateDecorations on every
        // frame. On first time through, load file and create DecorativeMeshFile
        // and cache it so we don't load files from disk during live rendering.
        //const std::string& file = get_mesh_file();
        if (file.empty() || file.compare(PropertyStr::getDefaultStr()) == 0) {
            log_error("No mesh_file property defined in Smith2018ContactMesh: "
                "{}", getName());
            OPENSIM_THROW(Exception, "");
            //return;  // Return immediately if no file has been specified.
        }

        bool isAbsolutePath; std::string directory, fileName, extension;
        SimTK::Pathname::deconstructPathname(file,
            isAbsolutePath, directory, fileName, extension);
        const std::string lowerExtension = SimTK::String::toLower(extension);
        if (lowerExtension != ".vtp" && lowerExtension != ".obj" && lowerExtension != ".stl") {
             log_error("ModelVisualizer ignoring '{}'; only .vtp, .stl, and "
                      ".obj files currently supported.",
                    file);
            OPENSIM_THROW(Exception, "");
            //return;
        }

        SimTK::Array_<std::string> attempts;
        const Model& model = dynamic_cast<const Model&>(*rootModel);
        bool foundIt = ModelVisualizer::findGeometryFile(model, file, isAbsolutePath, attempts);

        if (!foundIt) {
            
            log_error("Couldn't find file '{}'.", file);
            
            log_error( "The following locations were tried:");
            for (unsigned i = 0; i < attempts.size(); ++i)
                log_error(attempts[i]);
            
            if (!isAbsolutePath &&
                !SimTK::Pathname::environmentVariableExists("OPENSIM_HOME"))
                log_debug("Set environment variable OPENSIM_HOME to search $OPENSIM_HOME/Geometry.");
            OPENSIM_THROW(Exception, "");
            //return;
        }

        try {
            std::ifstream objFile;
            objFile.open(attempts.back().c_str());
            // objFile closes when destructed
            // if the file can be opened but had bad contents e.g. binary vtp 
            // it will be handled downstream 
        }
        catch (const std::exception& e) {
            log_warn("Visualizer couldn't open {} because: {}",
                attempts.back(), e.what());
            OPENSIM_THROW(Exception, "");
            //return;
        }

    return attempts.back();
}

void Smith2018ContactMesh::initializeMesh()
{
    _mesh_is_cached = true;
    _mesh.clear();
    _mesh_back.clear();
    
    _obb = OBBTreeNode();
    _back_obb = OBBTreeNode();


    // Load Mesh from file
    _full_mesh_file_path = findMeshFile(get_mesh_file());
    
    _mesh.loadFile(_full_mesh_file_path);

    //Scale Mesh
    SimTK::Real xscale = get_scale_factors()(0);
    SimTK::Real yscale = get_scale_factors()(1);
    SimTK::Real zscale = get_scale_factors()(2);

    SimTK::Rotation scale_rot;
    scale_rot.set(0, 0, xscale);
    scale_rot.set(1, 1, yscale);
    scale_rot.set(2, 2, zscale);
    SimTK::Transform scale_transform(scale_rot,SimTK::Vec3(0.0));
    _mesh.transformMesh(scale_transform);
    
    //Allocate space
    _tri_center.resize(_mesh.getNumFaces());
    _tri_normal.resize(_mesh.getNumFaces());
    _tri_area.resize(_mesh.getNumFaces());
    _tri_thickness.resize(_mesh.getNumFaces());
    _tri_elastic_modulus.resize(_mesh.getNumFaces());
    _tri_poissons_ratio.resize(_mesh.getNumFaces());

    _vertex_locations.resize(_mesh.getNumVertices());
    _face_vertex_locations.resize(_mesh.getNumFaces(), 3);
        
    _regional_tri_ind.resize(6);
    _regional_n_tri.assign(6,0);

    // Compute Mesh Properties
    //========================

    for (int i = 0; i < _mesh.getNumFaces(); ++i) {

        // Get Triangle Vertice Positions
        int v1_i = _mesh.getFaceVertex(i, 0);
        int v2_i = _mesh.getFaceVertex(i, 1);
        int v3_i = _mesh.getFaceVertex(i, 2);

        SimTK::Vec3 v1 = _mesh.getVertexPosition(v1_i);
        SimTK::Vec3 v2 = _mesh.getVertexPosition(v2_i);
        SimTK::Vec3 v3 = _mesh.getVertexPosition(v3_i);

        // Compute Triangle Center
        _tri_center(i) = (v1 + v2 + v3) / 3.0;

        // Compute Triangle Normal
        SimTK::Vec3 e1 = v3 - v1;
        SimTK::Vec3 e2 = v2 - v1;

        SimTK::Vec3 cross = SimTK::cross(e1, e2);

        double mag = cross.norm();

        for (int j = 0; j < 3; ++j) {
            _tri_normal(i).set(j,-cross[j] / mag);
        }

        
        // Compute Triangle Area
        // Compute length of each side of the triangle
        double s1 = 0;
        double s2 = 0;
        double s3 = 0;
                
        for (int j = 0; j<3; j++) {
            s1 += (v2[j] - v1[j])*(v2[j] - v1[j]);
            s2 += (v3[j] - v2[j])*(v3[j] - v2[j]);
            s3 += (v1[j] - v3[j])*(v1[j] - v3[j]);
        }

        s1 = sqrt(s1);
        s2 = sqrt(s2);
        s3 = sqrt(s3);

        // Now employ Heron's formula
        double s = (s1 + s2 + s3) / 2.0;
        _tri_area[i] = sqrt(s*(s - s1)*(s - s2)*(s - s3));
        
        //Determine regional triangle indices
        for (int j = 0; j < 3; ++j) {
            if (_tri_center(i)(j) < 0.0) {
                _regional_tri_ind[j * 2].push_back(i);
                _regional_n_tri[j * 2]++;
            }
            else {
                _regional_tri_ind[j * 2 + 1].push_back(i);
                _regional_n_tri[j * 2 + 1]++;
            }
        }  
    }

    //Vertex Locations
    for (int i = 0; i < _mesh.getNumVertices(); ++i) {
        _vertex_locations(i) = _mesh.getVertexPosition(i);
    }

    //Face Vertex Locations
    for (int i = 0; i < _mesh.getNumFaces(); ++i) {
        for (int j = 0; j < 3; ++j) {
            int v_ind = _mesh.getFaceVertex(i, j);
            _face_vertex_locations(i,j) = _mesh.getVertexPosition(v_ind);
        }
    }

    //Vertex Connectivity
    std::vector<std::vector<int>> ver_tri_ind(_mesh.getNumVertices());
    std::vector<int> ver_nTri(_mesh.getNumVertices());

    for (int i = 0; i < _mesh.getNumFaces(); ++i) {
        for (int j = 0; j < 3; ++j) {
            int ver = _mesh.getFaceVertex(i, j);
            ver_tri_ind[ver].push_back(i);
            ver_nTri[ver]++;
        }
    }

    //Triangle Neighbors
    _tri_neighbors.resize(_mesh.getNumFaces());

    for (int i = 0; i < _mesh.getNumFaces(); ++i) {
        for (int j = 0; j < 3; ++j) {

            int ver = _mesh.getFaceVertex(i, j);

            for (int k = 0; k < ver_nTri[ver]; ++k) {
                int tri = ver_tri_ind[ver][k];

                //triange can't be neighbor with itself
                if (tri == i) {
                    continue;
                }
                _tri_neighbors[i].insert(tri);
            }

        }
    }

    //Construct the OBB Tree
    SimTK::Array_<int> allFaces(_mesh.getNumFaces());
    for (int i = 0; i < _mesh.getNumFaces(); ++i) {
        allFaces[i] = i;}

    createObbTree(_obb, _mesh, allFaces);

    //Triangle Material Properties
    if(get_use_variable_thickness()){
        computeVariableThickness();
    }
    else {
        _tri_thickness = get_thickness();
    }

    _tri_elastic_modulus = get_elastic_modulus();
    _tri_poissons_ratio = get_poissons_ratio();
}

void Smith2018ContactMesh::computeVariableThickness() {

    // Get Mesh Properties
    double min_thickness = get_min_thickness();
    double max_thickness = get_max_thickness();

    // Load mesh_back_file
    std::string file = findMeshFile(get_mesh_back_file());
    _mesh_back.loadFile(file);

    //Scale _mesh_back
    SimTK::Real xscale = get_scale_factors()(0);
    SimTK::Real yscale = get_scale_factors()(1);
    SimTK::Real zscale = get_scale_factors()(2);

    SimTK::Rotation scale_rot;
    scale_rot.set(0, 0, xscale);
    scale_rot.set(1, 1, yscale);
    scale_rot.set(2, 2, zscale);
    SimTK::Transform scale_transform(scale_rot,SimTK::Vec3(0.0));
    _mesh_back.transformMesh(scale_transform);

    // Create OBB tree for back mesh
    SimTK::Array_<int> allFaces(_mesh_back.getNumFaces());

    for (int i = 0; i < _mesh_back.getNumFaces(); ++i) {
        allFaces[i] = i;
    }    

    createObbTree(_back_obb,_mesh_back, allFaces);

    //Loop through all triangles in cartilage mesh
    for (int i = 0; i < _mesh.getNumFaces(); ++i) {

        //Use mesh_back OBB tree to find cartilage thickness
        //--------------------------------------------------

        int tri;
        SimTK::Vec3 intersection_point;
        double depth = 0.0;
        
        if (_back_obb.rayIntersectOBB(_mesh_back,
            _tri_center(i), -_tri_normal(i), tri, intersection_point, depth)) {

            if (depth < min_thickness) {
                depth = min_thickness;
            }
            else if (depth > max_thickness) {
                depth = min_thickness;
            }            
        }
        else{ //Normal from mesh missed mesh back
            depth = min_thickness;
        }
        _tri_thickness(i) = depth;
    }
}

void Smith2018ContactMesh::generateDecorations(
    bool fixed, const ModelDisplayHints& hints,const SimTK::State& state,
    SimTK::Array_<SimTK::DecorativeGeometry>& geometry) const
{
    Super::generateDecorations(fixed, hints, state, geometry);

    // There is no fixed geometry to generate here.
    if (fixed) { return; }

    if (!get_Appearance().get_visible()) return;
    if (!hints.get_show_contact_geometry()) return;

    if (_decorative_mesh.get() != nullptr) {
        try {
            // Force the loading of the mesh to see if it has bad contents
            // (e.g., binary vtp).
            // We do not want to do this in extendFinalizeFromProperties b/c
            // it's expensive to repeatedly load meshes.
            _decorative_mesh->getMesh();
        }
        catch (const std::exception& e) {
            log_warn("Visualizer couldn't open {} because: {}",
                get_mesh_file(), e.what());
            // No longer try to visualize this mesh.
            _decorative_mesh.reset();
            return;
        }
    }
    //_decorative_mesh->setScaleFactors(get_scale_factors());

    //SET TRANSFORM
    // B: base Frame (Body or Ground)
    // F: PhysicalFrame that this ContactGeometry is connected to
    // P: the frame defined (relative to F) by the location and orientation
    //    properties.

    const auto& X_BF = getFrame().findTransformInBaseFrame();
    const auto& X_FP = getTransform();
    const auto X_BP = X_BF * X_FP;

    _decorative_mesh->setTransform(X_BP);
    _decorative_mesh->setBodyId(getFrame().getMobilizedBodyIndex());


    // SET APPERANCE
    _decorative_mesh->setColor(get_Appearance().get_color());
    _decorative_mesh->setOpacity(get_Appearance().get_opacity());
   
    if (get_Appearance().get_visible()) {
        _decorative_mesh->setRepresentation(
            (VisualRepresentation)
            get_Appearance().get_representation());
    }
    else {
        _decorative_mesh->setRepresentation(SimTK::DecorativeGeometry::Hide);
    }

    geometry.push_back(*_decorative_mesh);
}


void Smith2018ContactMesh::createObbTree
    (OBBTreeNode& node, const SimTK::PolygonalMesh& mesh, 
    const SimTK::Array_<int>& faceIndices)
{   // Find all vertices in the node and build the OrientedBoundingBox.
    node._numTriangles = faceIndices.size();
    
    set<int> vertexIndices;
    for (int i = 0; i < (int)faceIndices.size(); i++) {        
        for (int j = 0; j < 3; j++) {
            vertexIndices.insert(mesh.getFaceVertex(faceIndices[i], j));
        }
    }
    SimTK::Vector_<SimTK::Vec3> points((int)vertexIndices.size());
    int index = 0;
    for (set<int>::iterator iter = vertexIndices.begin();
        iter != vertexIndices.end(); ++iter) {
        points[index++] = mesh.getVertexPosition(*iter);
        
    }
    node._bounds = JAMOrientedBoundingBox(points);
    if (faceIndices.size() > 3) {

        // Order the axes by size.

        int axisOrder[3];
        const SimTK::Vec3& size = node._bounds.getSize();
        if (size[0] > size[1]) {
            if (size[0] > size[2]) {
                axisOrder[0] = 0;
                if (size[1] > size[2]) {
                    axisOrder[1] = 1;
                    axisOrder[2] = 2;
                }
                else {
                    axisOrder[1] = 2;
                    axisOrder[2] = 1;
                }
            }
            else {
                axisOrder[0] = 2;
                axisOrder[1] = 0;
                axisOrder[2] = 1;
            }
        }
        else if (size[0] > size[2]) {
            axisOrder[0] = 1;
            axisOrder[1] = 0;
            axisOrder[2] = 2;
        }
        else {
            if (size[1] > size[2]) {
                axisOrder[0] = 1;
                axisOrder[1] = 2;
            }
            else {
                axisOrder[0] = 2;
                axisOrder[1] = 1;
            }
            axisOrder[2] = 0;
        }

        // Try splitting along each axis.

        for (int i = 0; i < 3; i++) {
            SimTK::Array_<int> child1Indices, child2Indices;
            splitObbAxis(mesh,faceIndices, child1Indices, child2Indices, 
                         axisOrder[i]);
            if (child1Indices.size() > 0 && child2Indices.size() > 0) {
                // It was successfully split, so create the child nodes.

                node._child1 = new OBBTreeNode();
                node._child2 = new OBBTreeNode();
                createObbTree(*node._child1, mesh, child1Indices);
                createObbTree(*node._child2, mesh, child2Indices);
                return;
            }
        }
    }

    // This is a leaf node
    node._triangles.insert(node._triangles.begin(), faceIndices.begin(), 
                          faceIndices.end());
}

void Smith2018ContactMesh::splitObbAxis
   (const SimTK::PolygonalMesh& mesh, const SimTK::Array_<int>& parentIndices,
       SimTK::Array_<int>& child1Indices, SimTK::Array_<int>& child2Indices,
       int axis) 
{   // For each face, find its minimum and maximum extent along the axis.
    SimTK::Vector minExtent(parentIndices.size());
    SimTK::Vector maxExtent(parentIndices.size());
    SimTK::Array_<int> vertexIndices(3);
    for (int i = 0; i < (int) parentIndices.size(); i++) {
        for (int j = 0; j < 3; j++) {
            vertexIndices[j] = mesh.getFaceVertex(parentIndices[i], j);
        }
        SimTK::Real minVal = mesh.getVertexPosition(vertexIndices[0])(axis);
        SimTK::Real maxVal = mesh.getVertexPosition(vertexIndices[0])(axis);
        minVal =
            std::min(minVal, mesh.getVertexPosition(vertexIndices[1])(axis));
        maxVal =
            std::max(maxVal, mesh.getVertexPosition(vertexIndices[1])(axis));
        minExtent[i] =
            std::min(minVal, mesh.getVertexPosition(vertexIndices[2])(axis));
        maxExtent[i] =
            std::max(maxVal, mesh.getVertexPosition(vertexIndices[2])(axis));
    }
    
    // Select a split point that tries to put as many faces as possible 
    // entirely on one side or the other.
    
    SimTK::Real split = (median(minExtent)+median(maxExtent)) / 2;
    
    // Choose a side for each face.
    
    for (int i = 0; i < (int) parentIndices.size(); i++) {
        if (maxExtent[i] <= split)
            child1Indices.push_back(parentIndices[i]);
        else if (minExtent[i] >= split)
            child2Indices.push_back(parentIndices[i]);
        else if (0.5*(minExtent[i]+maxExtent[i]) <= split)
            child1Indices.push_back(parentIndices[i]);
        else
            child2Indices.push_back(parentIndices[i]);
    }
}

bool Smith2018ContactMesh::rayIntersectMesh(
    const SimTK::Vec3& origin, const SimTK::UnitVec3& direction,
    const double& min_proximity, const double& max_proximity, 
    int& tri, SimTK::Vec3 intersection_point, SimTK::Real& distance) const {

    double obb_distance=-1;
    SimTK::Array_<int> obb_triangles;

    if (_obb.rayIntersectOBB(_mesh, origin, direction, tri,
        intersection_point, distance)) {

        if ((distance > min_proximity) && (distance < max_proximity)) {
            return true;
        }
    }

    //Shoot the ray in the opposite direction
    if (min_proximity < 0.0) {        
        if (_obb.rayIntersectOBB(_mesh, origin, -direction, tri,
            intersection_point, distance)) {

            distance = -distance;
            if ((distance > min_proximity) && (distance < max_proximity)) {
                return true;
            }
        }
    }
    
    //ray didn't intersect
    distance = -1;
    intersection_point = -1;
    return false;
}

void Smith2018ContactMesh::printMeshDebugInfo() const {
    log_trace("Mesh Properties: {}", getName());
    log_trace("{:<10} {:<15} {:<15} {:<15} {:<15} {:<35} {:<35}",
        "Tri #", "Area", "Thickness",  "Elastic Modulus", "Poissons Ratio",
        "Center", "Normal");


    const SimTK::Vector& area = getTriangleAreas();
    const SimTK::Vector_<SimTK::Vec3>&  center = getTriangleCenters();
    const SimTK::Vector_<SimTK::UnitVec3>& normal = getTriangleNormals();

    for (int i = 0; i < getNumFaces(); i++) {
        double t = getTriangleThickness(i);
        double E = getTriangleElasticModulus(i);
        double v = getTrianglePoissonsRatio(i);
        double a = area(i);
        SimTK::Vec3 c = center(i);
        SimTK::Vec3 n = normal(i);

        log_trace("{:<10} {:<15} {:<15} {:<15} {:<15} {:<35} {:<35}",
          i, a, t, E, v, c, n);
    }
}

//=============================================================================
//               Smith2018ContactMesh :: OBBTreeNode
//=============================================================================
Smith2018ContactMesh::OBBTreeNode::OBBTreeNode(const OBBTreeNode& copy):
   _bounds(copy._bounds), _triangles(copy._triangles), 
    _numTriangles(copy._numTriangles) {
    if (copy._child1 == NULL) {
        _child1 = NULL;
        _child2 = NULL;
    }
    else {
        _child1 = new OBBTreeNode(*copy._child1);
        _child2 = new OBBTreeNode(*copy._child2);
    }
}

Smith2018ContactMesh::OBBTreeNode::~OBBTreeNode() {
    if (_child1 != NULL)
        delete _child1;
    if (_child2 != NULL)
        delete _child2;
}

bool Smith2018ContactMesh::OBBTreeNode::rayIntersectOBB(
    const SimTK::PolygonalMesh& mesh,
    const SimTK::Vec3& origin, const SimTK::UnitVec3& direction,
    int& tri_index, SimTK::Vec3& intersection_point, double& distance) const
{

    if (_child1 != NULL) {
        // Recursively check the child nodes.
        SimTK::Real child1distance, child2distance;
        SimTK::Array_<int> child1triangles, child2triangles;
        bool child1intersects = 
            _child1->_bounds.intersectsRay(origin, direction, child1distance);
        bool child2intersects = 
            _child2->_bounds.intersectsRay(origin, direction, child2distance);
        
        if (child1intersects) {
            if (child2intersects) {
                // The ray intersects both child nodes.
                // First check the closer one.

                if (child1distance < child2distance) {
                    child1intersects =_child1->rayIntersectOBB(
                        mesh, origin, direction, tri_index,
                        intersection_point, child1distance);

                    if (!child1intersects || child2distance < child1distance)
                        child2intersects = _child2->rayIntersectOBB(
                            mesh,origin, direction, tri_index,
                            intersection_point, child2distance);
                }
                else {
                    child2intersects =_child2->rayIntersectOBB(
                        mesh, origin, direction, tri_index,
                        intersection_point, child2distance);

                    if (!child2intersects || child1distance < child2distance)
                        child1intersects =_child1->rayIntersectOBB(
                            mesh, origin, direction, tri_index,
                            intersection_point, child1distance);
                }
            }
            else
                child1intersects = _child1->rayIntersectOBB(mesh, origin,
                    direction, tri_index, intersection_point, child1distance);
        }
        else if (child2intersects)
            child2intersects = _child2->rayIntersectOBB(mesh, origin,
                direction, tri_index, intersection_point, child2distance);

        // If either one had an intersection, return the closer one.  

        if (child1intersects){
            if (!child2intersects || child1distance < child2distance) {
                distance = child1distance;
                return true;
            }
        }
        if (child2intersects) {
            distance = child2distance;
            return true;
        }
        return false;
    }

    //Reached a leaf node, check all containing triangles
    for (int i = 0; i < (int)_triangles.size(); i++) {
        if (rayIntersectTri(mesh, origin, direction, _triangles[i],
            intersection_point, distance)) {

            tri_index = _triangles[i];
            return true;
        }
    }
    return false;
}

bool Smith2018ContactMesh::OBBTreeNode::rayIntersectTri(
    const SimTK::PolygonalMesh& mesh,
    SimTK::Vec3 origin, SimTK::Vec3 direction, 
    int tri_index,
    SimTK::Vec3& intersection_pt, double& distance) const
{
    
    
/* 
origin - reference point of casting ray 
        (i.e. center of triangle from which ray is cast)

direction - casting ray direction vector 
            (i.e. normal to triangle from which ray is cast)

tri_index - The triangle index of the test target
    
www.lighthouse3d.com/tutorials/maths/ray-triangle-intersection/
*/  
    int v0_ind = mesh.getFaceVertex(tri_index, 0);
    int v1_ind = mesh.getFaceVertex(tri_index, 1);
    int v2_ind = mesh.getFaceVertex(tri_index, 2);
    

    SimTK::Vec3 v0 = mesh.getVertexPosition(v0_ind);
    SimTK::Vec3 v1 = mesh.getVertexPosition(v1_ind);
    SimTK::Vec3 v2 = mesh.getVertexPosition(v2_ind);

   
    SimTK::Vec3 e1, e2, h, s, q;
    double a, f, u, v;

    // find triangle edges
    for (int i = 0; i < 3; ++i) {
        e1(i) = v1(i) - v0(i);
        e2(i) = v2(i) - v0(i);
    }

    h = SimTK::cross(direction, e2);
    a = dot(e1, h);

    //If ray is perpendicular to the triangle, no interestion
    //this should be adjusted for precision, a=0 when e1 and h are pependicular

    if (a > -0.00000001 && a < 0.00000001) {
        return(false);
    }

    // Else on to second test
    // find triangle edges
    f = 1 / a;
    for (int i = 0; i < 3; i++) {
        s(i) = origin(i) - v0(i);
    }

    u = f * SimTK::dot(s, h);
    if (u < 0 || u > 1.0)
        return(false);

    q = SimTK::cross(s, e1);

    v = f * SimTK::dot(direction, q);
    double w = 1 - u - v;
    
    if (v < 0.0 || w < 0.0)
        return(false);
    
    
    else {
        // else there is a line intersection
        // at this stage we can compute the distance to the intersection
        // point on the line
        //     point(t) = p + t * d
        //  where
        //      p is a point in the line
        //      d is a vector that provides the line's direction
        //      t is the distance
        intersection_pt = w * v0 + u * v1 + v * v2;
        distance = f * SimTK::dot(e2, q);
        return(true);
    }
}

const Smith2018ContactMesh::JAMOrientedBoundingBox&
Smith2018ContactMesh::OBBTreeNode::getBounds() const {
    return _bounds;
}

bool Smith2018ContactMesh::OBBTreeNode::isLeafNode() const {
    return (_child1 == NULL);
}

const Smith2018ContactMesh::OBBTreeNode 
Smith2018ContactMesh::OBBTreeNode::getFirstChildNode() const {
    SimTK_ASSERT_ALWAYS(_child1, 
        "Called getFirstChildNode() on a leaf node");
    return OBBTreeNode(*_child1);
}

const Smith2018ContactMesh::OBBTreeNode 
Smith2018ContactMesh::OBBTreeNode::getSecondChildNode() const {
    SimTK_ASSERT_ALWAYS(_child2, 
        "Called getFirstChildNode() on a leaf node");
    return OBBTreeNode(*_child2);
}

const SimTK::Array_<int>& 
Smith2018ContactMesh::OBBTreeNode::getTriangles() const {
    SimTK_ASSERT_ALWAYS(_child2 == NULL, 
        "Called getTriangles() on a non-leaf node");
    return _triangles;
}

int Smith2018ContactMesh::OBBTreeNode::getNumTriangles() const {
    return _numTriangles;
}






//=========================================================================
//                            JAMOrientedBoundingBox
//=========================================================================

    

    Smith2018ContactMesh::JAMOrientedBoundingBox::JAMOrientedBoundingBox() {}

    Smith2018ContactMesh::JAMOrientedBoundingBox::JAMOrientedBoundingBox(
            const SimTK::Transform& transform, const SimTK::Vec3& size)
            : transform(transform), size(size) {}

    const SimTK::Transform&
    Smith2018ContactMesh::JAMOrientedBoundingBox::getTransform() const {
        return transform;
    }

    const SimTK::Vec3&
    Smith2018ContactMesh::JAMOrientedBoundingBox::getSize() const {
        return size;
    }

    Smith2018ContactMesh::JAMOrientedBoundingBox::JAMOrientedBoundingBox(
            const SimTK::Vector_<SimTK::Vec3>& points) {
        SimTK_APIARGCHECK(points.size() > 0, "JAMOrientedBoundingBox",
                "JAMOrientedBoundingBox", "No points passed to constructor");

        // Construct the covariance matrix of the points.

        SimTK::Vec3 center = mean(points);
        SimTK::Vector_<SimTK::Vec3> p = points - center;
        SimTK::Mat33 c(0);
        for (int i = 0; i < p.size(); i++)
            for (int j = 0; j < 3; j++)
                for (int k = 0; k < 3; k++) c(j, k) += p[i][j] * p[i][k];
        c *= SimTK::Real(1) / p.size();

        // EDITS AND TESTING BY COLIN 
        // 
        // This works fine for C++ and MATLAB, but causes crash with no
        // error in python at SimTK::Eigen(matrix). I implemented a custom routine
        // to find eigenvectors to work around this (eigen_decomposition). 
        // However, on testing with lenhart model and joint mechanics tool,
        //  I found SimTK::Eigen had better performance than the custom 
        // implementation. I did not test enough to see that custom implementation
        // was giving the same decomposition, and matrix index (i,j vs j,i) 
        // did not seem to matter. Interestingly, just initializing with unit
        // vectors showed the best performance... so that is what is 
        // currently done. 

        // Find the eigenvectors, which will be our initial guess for the axes
        // of the box.

        /* SimTK::Vector_<std::complex<SimTK::Real>> eigenvalues;
        SimTK::Matrix_<std::complex<SimTK::Real>> eigenvectors;
        
        SimTK::Eigen(SimTK::Matrix(c))
                .getAllEigenValuesAndVectors(
                eigenvalues, eigenvectors);
        */
        /*
        double A[3][3];
        double V[3][3];
        double d[3];

        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) { 
                A[i][j] = c(i, j);
            }
        }
        
        eigen_decomposition(A, V, d);        
        */
        SimTK::Vec3 axes[3];
        for (int i = 0; i < 2; i++) {
            SimTK::Vec3 axis_vec(0);
            axis_vec.set(i, 1);
            axes[i] = axis_vec;
            /* for (int j = 0; j < 3; j++) {
                axes[i][j] = eigenvectors(j, i).real();
                //axes[i][j] = V[i][j];
                }*/
        }
        
        // Now try optimizing the rotation to give a better fit.

        SimTK::Rotation rot(
                SimTK::UnitVec3(axes[0]), SimTK::XAxis, axes[1], SimTK::YAxis);
        SimTK::Real volume = calculateVolume(points, rot);
        for (SimTK::Real step = SimTK::Real(0.1); step > SimTK::Real(0.01);
                step /= 2) {
            bool improved = true;
            while (improved) {
                SimTK::Rotation trialRotation[6];
                trialRotation[0].setRotationFromAngleAboutX(step);
                trialRotation[1].setRotationFromAngleAboutX(-step);
                trialRotation[2].setRotationFromAngleAboutY(step);
                trialRotation[3].setRotationFromAngleAboutY(-step);
                trialRotation[4].setRotationFromAngleAboutZ(step);
                trialRotation[5].setRotationFromAngleAboutZ(-step);
                improved = false;
                for (int i = 0; i < 6; i++) {
                    trialRotation[i] = trialRotation[i] * rot;
                    SimTK::Real trialVolume =
                            calculateVolume(points, trialRotation[i]);
                    if (trialVolume < volume) {
                        rot = trialRotation[i];
                        volume = trialVolume;
                        improved = true;
                    }
                }
            }
        }

        // Find the extent along each axis.

        axes[0] = SimTK::Vec3(rot.col(0));
        axes[1] = SimTK::Vec3(rot.col(1));
        axes[2] = SimTK::Vec3(rot.col(2));
        SimTK::Vec3 minExtent = SimTK::Vec3(SimTK::MostPositiveReal);
        SimTK::Vec3 maxExtent = SimTK::Vec3(SimTK::MostNegativeReal);
        for (int i = 0; i < points.size(); i++) {
            for (int j = 0; j < 3; j++) {
                minExtent[j] = std::min(minExtent[j], ~axes[j] * points[i]);
                maxExtent[j] = std::max(maxExtent[j], ~axes[j] * points[i]);
            }
        }

        // Create the bounding box.

        size = maxExtent - minExtent;
        SimTK::Vec3 tol = SimTK::Real(1e-5) * size;
        for (int i = 0; i < 3; i++)
            tol[i] = std::max(tol[i], SimTK::Real(1e-10));
        size += 2 * tol;
        transform = SimTK::Transform(rot, rot * (minExtent - tol));
    }

    SimTK::Real Smith2018ContactMesh::JAMOrientedBoundingBox::calculateVolume(
            const SimTK::Vector_<SimTK::Vec3>& points,
            const SimTK::Rotation& rotation) {
        SimTK::Vec3 minExtent = SimTK::Vec3(SimTK::MostPositiveReal);
        SimTK::Vec3 maxExtent = SimTK::Vec3(SimTK::MostNegativeReal);
        for (int i = 0; i < points.size(); i++) {
            SimTK::Vec3 p = ~rotation * points[i];
            for (int j = 0; j < 3; j++) {
                minExtent[j] = std::min(minExtent[j], p[j]);
                maxExtent[j] = std::max(maxExtent[j], p[j]);
            }
        }
        SimTK::Vec3 size =
                maxExtent - minExtent + SimTK::Vec3(SimTK::Real(2e-10));
        return size[0] * size[1] * size[2];
    }

    bool Smith2018ContactMesh::JAMOrientedBoundingBox::containsPoint(
            const SimTK::Vec3& point) const {
        SimTK::Vec3 p = ~transform * point;
        return (p[0] >= 0 && p[0] <= size[0] && p[1] >= 0 && p[1] <= size[1] &&
                p[2] >= 0 && p[2] <= size[2]);
    }

    bool Smith2018ContactMesh::JAMOrientedBoundingBox::intersectsBox(
            const JAMOrientedBoundingBox& box) const {
        // Precalculate various quantities.

        // From the other box's frame to this one's
        const SimTK::Transform t = ~getTransform() * box.getTransform();
        const SimTK::Mat33& r = t.R().asMat33();
        const SimTK::Mat33 rabs = r.abs();
        const SimTK::Vec3 a = getSize() / 2;
        const SimTK::Vec3 b = box.getSize() / 2;
        const SimTK::Vec3 center1 = a;
        const SimTK::Vec3 center2 = t * b;
        const SimTK::Vec3 d = center2 - center1;

        // Now perform a series of 15 tests where we project each box onto an
        // axis and see if they overlap.  This is described in Gottschalk, S.,
        // Lin, MC, Manocha, D, "OBBTree: a hierarchical structure for rapid
        // interference detection." Proceedings of the 23rd Annual Conference on
        // Computer Graphics and Interactive Techniques, pp. 171-180, 1996. We
        // also perform an additional check which allows an early acceptance if
        // the center of one box is inside the other one.

        // First check the three axes of this box.

        bool accept = true;
        for (int i = 0; i < 3; i++) {
            SimTK::Real ra = a[i];
            SimTK::Real rb = rabs.row(i) * b;
            SimTK::Real distance = std::abs(d[i]);
            if (distance > ra + rb) return false;
            if (distance > ra) accept = false;
        }
        if (accept) return true;

        // Now check the three axes of the other box.

        accept = true;
        for (int i = 0; i < 3; i++) {
            SimTK::Real ra = ~a * rabs.col(i);
            SimTK::Real rb = b[i];
            SimTK::Real distance =
                    std::abs(d[0] * r(0, i) + d[1] * r(1, i) + d[2] * r(2, i));
            if (distance > ra + rb) return false;
            if (distance > rb) accept = false;
        }
        if (accept) return true;

        // Now check the nine axes formed from cross products of one axis from
        // each box.

        {
            SimTK::Real ra = a[1] * rabs(2, 0) + a[2] * rabs(1, 0);
            SimTK::Real rb = b[1] * rabs(0, 2) + b[2] * rabs(0, 1);
            if (std::abs(d[2] * r(1, 0) - d[1] * r(2, 0)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[1] * rabs(2, 1) + a[2] * rabs(1, 1);
            SimTK::Real rb = b[0] * rabs(0, 2) + b[2] * rabs(0, 0);
            if (std::abs(d[2] * r(1, 1) - d[1] * r(2, 1)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[1] * rabs(2, 2) + a[2] * rabs(1, 2);
            SimTK::Real rb = b[0] * rabs(0, 1) + b[1] * rabs(0, 0);
            if (std::abs(d[2] * r(1, 2) - d[1] * r(2, 2)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[0] * rabs(2, 0) + a[2] * rabs(0, 0);
            SimTK::Real rb = b[1] * rabs(1, 2) + b[2] * rabs(1, 1);
            if (std::abs(d[0] * r(2, 0) - d[2] * r(0, 0)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[0] * rabs(2, 1) + a[2] * rabs(0, 1);
            SimTK::Real rb = b[0] * rabs(1, 2) + b[2] * rabs(1, 0);
            if (std::abs(d[0] * r(2, 1) - d[2] * r(0, 1)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[0] * rabs(2, 2) + a[2] * rabs(0, 2);
            SimTK::Real rb = b[0] * rabs(1, 1) + b[1] * rabs(1, 0);
            if (std::abs(d[0] * r(2, 2) - d[2] * r(0, 2)) > ra + rb)
                return false;
        }

        {
            SimTK::Real ra = a[0] * rabs(1, 0) + a[1] * rabs(0, 0);
            SimTK::Real rb = b[1] * rabs(2, 2) + b[2] * rabs(2, 1);
            if (std::abs(d[1] * r(0, 0) - d[0] * r(1, 0)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[0] * rabs(1, 1) + a[1] * rabs(0, 1);
            SimTK::Real rb = b[0] * rabs(2, 2) + b[2] * rabs(2, 0);
            if (std::abs(d[1] * r(0, 1) - d[0] * r(1, 1)) > ra + rb)
                return false;
        }
        {
            SimTK::Real ra = a[0] * rabs(1, 2) + a[1] * rabs(0, 2);
            SimTK::Real rb = b[0] * rabs(2, 1) + b[1] * rabs(2, 0);
            if (std::abs(d[1] * r(0, 2) - d[0] * r(1, 2)) > ra + rb)
                return false;
        }
        return true;
    }

    bool Smith2018ContactMesh::JAMOrientedBoundingBox::intersectsRay(
            const SimTK::Vec3& origin, const SimTK::UnitVec3& direction,
            SimTK::Real& distance)
            const {
        // Transform the ray to the bounding box's reference frame.

        SimTK::Vec3 orig = ~getTransform() * origin;
        SimTK::UnitVec3 dir = ~getTransform().R() * direction;

        // Check it against each plane that defines a side of the box.

        SimTK::Real minDist = SimTK::MostNegativeReal;
        SimTK::Real maxDist = SimTK::MostPositiveReal;
        if (dir[0] == 0.0) {
            if (orig[0] < 0 || orig[0] > getSize()[0]) return false;
        } else {
            SimTK::Real dist1 = -orig[0] / dir[0];
            SimTK::Real dist2 = (getSize()[0] - orig[0]) / dir[0];
            if (dist1 < dist2) {
                if (dist1 > minDist) minDist = dist1;
                if (dist2 < maxDist) maxDist = dist2;
            } else {
                if (dist2 > minDist) minDist = dist2;
                if (dist1 < maxDist) maxDist = dist1;
            }
            if (minDist > maxDist || maxDist < 0.0) return false;
        }
        if (dir[1] == 0.0) {
            if (orig[1] < 0 || orig[1] > getSize()[1]) return false;
        } else {
            SimTK::Real dist1 = -orig[1] / dir[1];
            SimTK::Real dist2 = (getSize()[1] - orig[1]) / dir[1];
            if (dist1 < dist2) {
                if (dist1 > minDist) minDist = dist1;
                if (dist2 < maxDist) maxDist = dist2;
            } else {
                if (dist2 > minDist) minDist = dist2;
                if (dist1 < maxDist) maxDist = dist1;
            }
            if (minDist > maxDist || maxDist < 0.0) return false;
        }
        if (dir[2] == 0.0) {
            if (orig[2] < 0 || orig[2] > getSize()[2]) return false;
        } else {
            SimTK::Real dist1 = -orig[2] / dir[2];
            SimTK::Real dist2 = (getSize()[2] - orig[2]) / dir[2];
            if (dist1 < dist2) {
                if (dist1 > minDist) minDist = dist1;
                if (dist2 < maxDist) maxDist = dist2;
            } else {
                if (dist2 > minDist) minDist = dist2;
                if (dist1 < maxDist) maxDist = dist1;
            }
            if (minDist > maxDist || maxDist < 0.0) return false;
        }
        if (minDist > 0)
            distance = minDist;
        else
            distance = 0;
        return true;
    }

    SimTK::Vec3 Smith2018ContactMesh::JAMOrientedBoundingBox::findNearestPoint(
            const SimTK::Vec3& position) const {
        // Transform the point to the bounding box's reference frame.

        SimTK::Vec3 p = ~getTransform() * position;

        // Find the nearest point in the box.

        if (p[0] < 0) p[0] = 0;
        if (p[0] > getSize()[0]) p[0] = getSize()[0];
        if (p[1] < 0) p[1] = 0;
        if (p[1] > getSize()[1]) p[1] = getSize()[1];
        if (p[2] < 0) p[2] = 0;
        if (p[2] > getSize()[2]) p[2] = getSize()[2];

        // Transform it back again.

        return getTransform() * p;
    }

    void Smith2018ContactMesh::JAMOrientedBoundingBox::getCorners(
            SimTK::Vec3 corners[8]) const {
        SimTK::Vec3 dx = size[0] * transform.R().col(0);
        SimTK::Vec3 dy = size[1] * transform.R().col(1);
        SimTK::Vec3 dz = size[2] * transform.R().col(2);
        corners[0] = transform.p();
        corners[1] = corners[0] + dx;
        corners[2] = corners[0] + dy;
        corners[3] = corners[1] + dy;
        corners[4] = corners[0] + dz;
        corners[5] = corners[1] + dz;
        corners[6] = corners[2] + dz;
        corners[7] = corners[3] + dz;
    }
    /*
    Smith2018ContactMesh::JAMOrientedBoundingBox operator*(
            const SimTK::Transform& t,
            const Smith2018ContactMesh::JAMOrientedBoundingBox& box) {
        return Smith2018ContactMesh::JAMOrientedBoundingBox(
                t * box.getTransform(), box.getSize());
    }
    */

//} // namespace SimTK



    //=============================================================================
    // Eigen Decomposition
    //=============================================================================
#ifdef MAX
#    undef MAX
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define n 3

    static double hypot2(double x, double y) { return sqrt(x * x + y * y); }

    // Symmetric Householder reduction to tridiagonal form.

    static void tred2(double V[n][n], double d[n], double e[n]) {

        //  This is derived from the Algol procedures tred2 by
        //  Bowdler, Martin, Reinsch, and Wilkinson, Handbook for
        //  Auto. Comp., Vol.ii-Linear Algebra, and the corresponding
        //  Fortran subroutine in EISPACK.

        for (int j = 0; j < n; j++) { d[j] = V[n - 1][j]; }

        // Householder reduction to tridiagonal form.

        for (int i = n - 1; i > 0; i--) {

            // Scale to avoid under/overflow.

            double scale = 0.0;
            double h = 0.0;
            for (int k = 0; k < i; k++) { scale = scale + fabs(d[k]); }
            if (scale == 0.0) {
                e[i] = d[i - 1];
                for (int j = 0; j < i; j++) {
                    d[j] = V[i - 1][j];
                    V[i][j] = 0.0;
                    V[j][i] = 0.0;
                }
            } else {

                // Generate Householder vector.

                for (int k = 0; k < i; k++) {
                    d[k] /= scale;
                    h += d[k] * d[k];
                }
                double f = d[i - 1];
                double g = sqrt(h);
                if (f > 0) { g = -g; }
                e[i] = scale * g;
                h = h - f * g;
                d[i - 1] = f - g;
                for (int j = 0; j < i; j++) { e[j] = 0.0; }

                // Apply similarity transformation to remaining columns.

                for (int j = 0; j < i; j++) {
                    f = d[j];
                    V[j][i] = f;
                    g = e[j] + V[j][j] * f;
                    for (int k = j + 1; k <= i - 1; k++) {
                        g += V[k][j] * d[k];
                        e[k] += V[k][j] * f;
                    }
                    e[j] = g;
                }
                f = 0.0;
                for (int j = 0; j < i; j++) {
                    e[j] /= h;
                    f += e[j] * d[j];
                }
                double hh = f / (h + h);
                for (int j = 0; j < i; j++) { e[j] -= hh * d[j]; }
                for (int j = 0; j < i; j++) {
                    f = d[j];
                    g = e[j];
                    for (int k = j; k <= i - 1; k++) {
                        V[k][j] -= (f * e[k] + g * d[k]);
                    }
                    d[j] = V[i - 1][j];
                    V[i][j] = 0.0;
                }
            }
            d[i] = h;
        }

        // Accumulate transformations.

        for (int i = 0; i < n - 1; i++) {
            V[n - 1][i] = V[i][i];
            V[i][i] = 1.0;
            double h = d[i + 1];
            if (h != 0.0) {
                for (int k = 0; k <= i; k++) { d[k] = V[k][i + 1] / h; }
                for (int j = 0; j <= i; j++) {
                    double g = 0.0;
                    for (int k = 0; k <= i; k++) { g += V[k][i + 1] * V[k][j]; }
                    for (int k = 0; k <= i; k++) { V[k][j] -= g * d[k]; }
                }
            }
            for (int k = 0; k <= i; k++) { V[k][i + 1] = 0.0; }
        }
        for (int j = 0; j < n; j++) {
            d[j] = V[n - 1][j];
            V[n - 1][j] = 0.0;
        }
        V[n - 1][n - 1] = 1.0;
        e[0] = 0.0;
    }

    // Symmetric tridiagonal QL algorithm.

    static void tql2(double V[n][n], double d[n], double e[n]) {

        //  This is derived from the Algol procedures tql2, by
        //  Bowdler, Martin, Reinsch, and Wilkinson, Handbook for
        //  Auto. Comp., Vol.ii-Linear Algebra, and the corresponding
        //  Fortran subroutine in EISPACK.

        for (int i = 1; i < n; i++) { e[i - 1] = e[i]; }
        e[n - 1] = 0.0;

        double f = 0.0;
        double tst1 = 0.0;
        double eps = pow(2.0, -52.0);
        for (int l = 0; l < n; l++) {

            // Find small subdiagonal element

            tst1 = MAX(tst1, fabs(d[l]) + fabs(e[l]));
            int m = l;
            while (m < n) {
                if (fabs(e[m]) <= eps * tst1) { break; }
                m++;
            }

            // If m == l, d[l] is an eigenvalue,
            // otherwise, iterate.

            if (m > l) {
                int iter = 0;
                do {
                    iter = iter + 1; // (Could check iteration count here.)

                    // Compute implicit shift

                    double g = d[l];
                    double p = (d[l + 1] - g) / (2.0 * e[l]);
                    double r = hypot2(p, 1.0);
                    if (p < 0) { r = -r; }
                    d[l] = e[l] / (p + r);
                    d[l + 1] = e[l] * (p + r);
                    double dl1 = d[l + 1];
                    double h = g - d[l];
                    for (int i = l + 2; i < n; i++) { d[i] -= h; }
                    f = f + h;

                    // Implicit QL transformation.

                    p = d[m];
                    double c = 1.0;
                    double c2 = c;
                    double c3 = c;
                    double el1 = e[l + 1];
                    double s = 0.0;
                    double s2 = 0.0;
                    for (int i = m - 1; i >= l; i--) {
                        c3 = c2;
                        c2 = c;
                        s2 = s;
                        g = c * e[i];
                        h = c * p;
                        r = hypot2(p, e[i]);
                        e[i + 1] = s * r;
                        s = e[i] / r;
                        c = p / r;
                        p = c * d[i] - s * g;
                        d[i + 1] = h + s * (c * g + s * d[i]);

                        // Accumulate transformation.

                        for (int k = 0; k < n; k++) {
                            h = V[k][i + 1];
                            V[k][i + 1] = s * V[k][i] + c * h;
                            V[k][i] = c * V[k][i] - s * h;
                        }
                    }
                    p = -s * s2 * c3 * el1 * e[l] / dl1;
                    e[l] = s * p;
                    d[l] = c * p;

                    // Check for convergence.

                } while (fabs(e[l]) > eps * tst1);
            }
            d[l] = d[l] + f;
            e[l] = 0.0;
        }

        // Sort eigenvalues and corresponding vectors.

        for (int i = 0; i < n - 1; i++) {
            int k = i;
            double p = d[i];
            for (int j = i + 1; j < n; j++) {
                if (d[j] < p) {
                    k = j;
                    p = d[j];
                }
            }
            if (k != i) {
                d[k] = d[i];
                d[i] = p;
                for (int j = 0; j < n; j++) {
                    p = V[j][i];
                    V[j][i] = V[j][k];
                    V[j][k] = p;
                }
            }
        }
    }

    void eigen_decomposition(double A[n][n], double V[n][n], double d[n]) {
        double e[n];
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) { V[i][j] = A[i][j]; }
        }
        tred2(V, d, e);
        tql2(V, d, e);
    }