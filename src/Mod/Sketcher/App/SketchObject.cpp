/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#include <memory>
#ifndef _PreComp_
#include <cmath>
#include <vector>

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_NormalProjection.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <ElCLib.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GC_MakeCircle.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomConvert_BSplineCurveKnotSplitting.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Hyperbola.hxx>
#include <Geom_Line.hxx>
#include <Geom_Parabola.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomLProp_CLProps.hxx>
#include <Standard_Version.hxx>
#include <ShapeAnalysis_Wire.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Hypr.hxx>
#include <gp_Parab.hxx>
#include <gp_Pln.hxx>
#endif

#include <boost_geometry.hpp>

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <boost/algorithm/string.hpp>

#include <App/Application.h>
#include <App/Document.h>
#include <App/ElementNamingUtils.h>
#include <App/Expression.h>
#include <App/ExpressionParser.h>
#include <App/FeaturePythonPyImp.h>
#include <App/IndexedName.h>
#include <App/MappedName.h>
#include <App/ObjectIdentifier.h>
#include <App/OriginFeature.h>
#include <App/Part.h>
#include <App/MappedElement.h>
#include <App/ElementNamingUtils.h>
#include <Base/Writer.h>
#include <Base/Reader.h>
#include <Base/Tools.h>
#include <Base/Writer.h>
#include <Base/Console.h>
#include <Base/Vector3D.h>
#include <Mod/Part/App/DatumFeature.h>
#include <Mod/Part/App/BodyBase.h>
#include <Mod/Part/App/PartParams.h>
#include <Mod/Part/App/PartPyCXX.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/WireJoiner.h>

#include <Mod/Part/App/GeometryMigrationExtension.h>
#include <Mod/Part/App/TopoShapeOpCode.h>

#include <Mod/Sketcher/App/Sketch.h>
#include <Mod/Sketcher/App/SketchObjectPy.h>
#include <Mod/Sketcher/App/SketchGeometryExtensionPy.h>
#include <Mod/Sketcher/App/SolverGeometryExtension.h>
#include <Mod/Sketcher/App/ExternalGeometryFacade.h>

#include "SketchObject.h"
#include "SketchObjectPy.h"
#include "SolverGeometryExtension.h"


#undef DEBUG
// #define DEBUG

using namespace Sketcher;
using namespace Base;
namespace sp = std::placeholders;
namespace bio = boost::iostreams;

FC_LOG_LEVEL_INIT("Sketch", true, true)

PROPERTY_SOURCE(Sketcher::SketchObject, Part::Part2DObject)

SketchObject::SketchObject()
{
    ADD_PROPERTY_TYPE(
        Geometry, (nullptr), "Sketch", (App::PropertyType)(App::Prop_None), "Sketch geometry");
    ADD_PROPERTY_TYPE(Constraints,
                      (nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None),
                      "Sketch constraints");
    ADD_PROPERTY_TYPE(ExternalGeometry,
                      (nullptr, nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None|App::Prop_ReadOnly),
                      "Sketch external geometry");
    ADD_PROPERTY_TYPE(FullyConstrained,
                      (false),
                      "Sketch",
                      (App::PropertyType)(App::Prop_Output | App::Prop_ReadOnly | App::Prop_Hidden),
                      "Sketch is fully constrained");
    ADD_PROPERTY_TYPE(Exports,
                      (nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_Hidden),"Sketch export geometry");
    ADD_PROPERTY_TYPE(ExternalGeo,
                      (nullptr),
                      "Sketch",
                      (App::PropertyType)(App::Prop_Hidden),"Sketch external geometry");
    ADD_PROPERTY_TYPE(ArcFitTolerance,
                      (0.0),
                      "Sketch",
                      (App::PropertyType)(App::Prop_None),
                      "Tolerance for fitting arcs of projected external geometry");
    ADD_PROPERTY_TYPE(ExternalBSplineMaxDegree,
                      (0),
                      "Sketch",
                      (App::Prop_None),
                      "Maximum degree of imported external BSpline. Zero to disable simplification");
    ADD_PROPERTY_TYPE(ExternalBSplineTolerance,
                      (0.0),
                      "Sketch",
                      (App::Prop_None),
                      "Tolerance for simplifying imported external BSpline");
    geoLastId = 0;

    ADD_PROPERTY(InternalShape,
                 (Part::TopoShape()));
    ADD_PROPERTY_TYPE(MakeInternals,
                      (false),
                      "Internal Geometry",
                      App::Prop_None,
                      "Make internal geometry, e.g. split intersecting edges, face of closed wires.");
    ADD_PROPERTY_TYPE(InternalTolerance,
                      (1e-6),
                      "Internal Geometry",
                      App::Prop_None,
                      "Tolerance used check vertex conincidents when making internal geometry");

    ParameterGrp::handle hGrpp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/Mod/Sketcher");
    geoHistoryLevel = hGrpp->GetInt("GeometryHistoryLevel",1);

    Geometry.setOrderRelevant(true);

    allowOtherBody = true;
    allowUnaligned = true;

    initExternalGeo();

    rebuildVertexIndex();

    lastDoF = 0;
    lastHasConflict = false;
    lastHasRedundancies = false;
    lastHasPartialRedundancies = false;
    lastHasMalformedConstraints = false;
    lastSolverStatus = GCS::SolveStatus::Success;
    lastSolveTime = 0;

    solverNeedsUpdate = false;

    noRecomputes = false;

    //NOLINTBEGIN
    ExpressionEngine.setValidator(
        std::bind(&Sketcher::SketchObject::validateExpression, this, sp::_1, sp::_2));

    constraintsRemovedConn = Constraints.signalConstraintsRemoved.connect(
        std::bind(&Sketcher::SketchObject::constraintsRemoved, this, sp::_1));
    constraintsRenamedConn = Constraints.signalConstraintsRenamed.connect(
        std::bind(&Sketcher::SketchObject::constraintsRenamed, this, sp::_1));
    //NOLINTEND

    analyser = new SketchAnalysis(this);

    internaltransaction = false;
    managedoperation = false;

    registerElementCache(internalPrefix(), &InternalShape);
}

SketchObject::~SketchObject()
{
    delete analyser;
}

void SketchObject::setupObject()
{
    ParameterGrp::handle hGrpp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Sketcher");
    ArcFitTolerance.setValue(hGrpp->GetFloat("ArcFitTolerance", Precision::Confusion()*10.0));
    ExternalBSplineMaxDegree.setValue(hGrpp->GetInt("ExternalBSplineMaxDegree", 5));
    ExternalBSplineTolerance.setValue(hGrpp->GetFloat("ExternalBSplineTolerance", 1e-4));
    MakeInternals.setValue(hGrpp->GetBool("MakeInternals", false));
    inherited::setupObject();
}

short SketchObject::mustExecute() const
{
    if (Geometry.isTouched())
        return 1;
    if (Constraints.isTouched())
        return 1;
    if (ExternalGeometry.isTouched())
        return 1;
    if (ExternalGeo.isTouched())
        return 1;
    return Part2DObject::mustExecute();
}

namespace {
GCS::Algorithm getDefaultSolver()
{
    auto preferences = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Sketcher/SolverAdvanced");
    int solver = preferences->GetInt("DefaultSolver", GCS::DogLeg);
    if (solver < GCS::BFGS || solver > GCS::DogLeg) {
        throw Base::ValueError("Invalid Sketcher DefaultSolver preference: expected a value from 0 to 2");
    }
    return static_cast<GCS::Algorithm>(solver);
}
} // namespace

App::DocumentObjectExecReturn* SketchObject::execute()
{
    try {
        App::DocumentObjectExecReturn* rtn = Part2DObject::execute();// to positionBySupport
        if (rtn != App::DocumentObject::StdReturn)
            // error
            return rtn;
    }
    catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    // setup and diagnose the sketch
    rebuildExternalGeometry();

    try {
        solvedSketch.defaultSolver = getDefaultSolver();
    }
    catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what(), this);
    }

    // This includes a regular solve including full geometry update, except when an error
    // ensues
    std::string msg;
    switch (this->solve(true)) {
        case SketchSolveStatus::Success:
            break;
        case SketchSolveStatus::Overconstrained:
            msg = "Over-constrained sketch\n";
            appendConflictMsg(lastConflicting, msg);
            return new App::DocumentObjectExecReturn(msg.c_str(), this);
        case SketchSolveStatus::ConflictingConstraints:
            msg = "Sketch with conflicting constraints\n";
            appendConflictMsg(lastConflicting, msg);
            return new App::DocumentObjectExecReturn(msg.c_str(), this);
        case SketchSolveStatus::RedundantConstraints:
            msg = "Sketch with redundant constraints\n";
            appendRedundantMsg(lastRedundant, msg);
            return new App::DocumentObjectExecReturn(msg.c_str(), this);
        case SketchSolveStatus::MalformedConstraints:
            msg = "Sketch with malformed constraints\n";
            appendMalformedConstraintsMsg(lastMalformedConstraints, msg);
            return new App::DocumentObjectExecReturn(msg.c_str(), this);
        case SketchSolveStatus::SolverError:
        case SketchSolveStatus::InvalidGeometry:
            return new App::DocumentObjectExecReturn("Solving the sketch failed", this);
    }

    // this is not necessary for sketch representation in edit mode, unless we want to trigger an
    // update of the objects that depend on this sketch (like pads)
    buildShape();

    return App::DocumentObject::StdReturn;
}

void SketchObject::buildShape()
{
    // Shape.setValue(solvedSketch.toShape());
    // We use the following instead to map element names

    std::vector<Part::TopoShape> shapes;
    std::vector<Part::TopoShape> vertices;

    // Use the geometry as the solver left it: the stored geometry is not
    // accurate enough to close a wire.
    std::vector<std::unique_ptr<Part::Geometry>> solvedGeometry;
    for (auto geo : solvedSketch.extractGeometry()) {
        solvedGeometry.emplace_back(geo);
    }

    int i=0;
    for (const auto &solved : solvedGeometry) {
        const Part::Geometry *geo = solved.get();
        ++i;
        if(GeometryFacade::getConstruction(geo))
            continue;
        if (geo->isDerivedFrom<Part::GeomPoint>()) {
            Part::TopoShape vertex(TopoDS::Vertex(geo->toShape()));
            int idx = getVertexIndexGeoPos(i-1, Sketcher::PointPos::start);
            std::string name = convertSubName(Data::IndexedName::fromConst("Vertex", idx+1), false);
            vertex.setElementName(Data::IndexedName::fromConst("Vertex", 1), 
                                  Data::MappedName::fromRawData(name.c_str()));
            vertices.push_back(vertex);
            vertices.back().copyElementMap(vertex, Part::OpCodes::Sketch);
        } else {
            auto indexedName = Data::IndexedName::fromConst("Edge", i);
            shapes.push_back(getEdge(geo,convertSubName(indexedName, false).c_str()));
        }
    }

    for(i=2;i<ExternalGeo.getSize();++i) {
        auto geo = ExternalGeo[i];
        auto egf = ExternalGeometryFacade::getFacade(geo);
        if(!egf->testFlag(ExternalGeometryExtension::Defining))
            continue;
        auto indexedName = Data::IndexedName::fromConst("ExternalEdge", i-1);
        if (geo->isDerivedFrom<Part::GeomPoint>()) {
            Part::TopoShape vertex(TopoDS::Vertex(geo->toShape()));
            vertex.setElementName(Data::IndexedName::fromConst("Vertex", 1),
                                  Data::MappedName::fromRawData(convertSubName(indexedName, false).c_str()));
            vertices.push_back(vertex);
            vertices.back().copyElementMap(vertex, Part::OpCodes::Sketch);
        } else {
            shapes.push_back(getEdge(geo, convertSubName(indexedName, false).c_str()));
        }
    }

    internalElementMap.clear();

    if(shapes.empty() && vertices.empty()) {
        InternalShape.setValue(Part::TopoShape());
        Shape.setValue(Part::TopoShape());
        return;
    }
    Part::TopoShape result(0, getDocument()->getStringHasher());
    if (vertices.empty()) {
        result.makEWires(shapes,Part::OpCodes::Sketch);
    } else {
        std::vector<Part::TopoShape> results;
        if (!shapes.empty()) {
            // Note, that we HAVE TO add the Part::OpCodes::Sketch op code to
            // all geometry exposed through the Shape property, because
            // SketchObject::getElementName() relies on this op code to
            // differentiate geometries that are exposed with those in edit
            // mode.
            auto wires = Part::TopoShape().makEWires(shapes, Part::OpCodes::Sketch);
            for (const auto &wire : wires.getSubTopoShapes(TopAbs_WIRE))
                results.push_back(wire);
        }
        results.insert(results.end(), vertices.begin(), vertices.end());
        result.makECompound(results);
    }
    result.Tag = getID();
    InternalShape.setValue(buildInternals(result.located()));
    // Must set Shape property after InternalShape so that
    // GeoFeature::updateElementReference() can run properly on change of Shape
    // property, because some reference may pointing to the InternalShape
    Shape.setValue(result);
}

const std::map<std::string,std::string> SketchObject::getInternalElementMap() const
{
    if (!internalElementMap.empty() || !MakeInternals.getValue())
        return internalElementMap;

    const auto& internalShape = InternalShape.getShape();
    auto shape = Shape.getShape().located();
    if (!internalShape.isNull() && !shape.isNull()) {
        std::vector<std::string> names;
        std::string prefix;
        const std::array<TopAbs_ShapeEnum, 2> types = {TopAbs_VERTEX, TopAbs_EDGE};
        for (const auto &type : types) {
            prefix = internalPrefix() + Part::TopoShape::shapeName(type);
            std::size_t len = prefix.size();
            int i=0;
            for (const auto &v : internalShape.getSubTopoShapes(type)) {
                ++i;
                shape.searchSubShape(v, &names, Data::SearchOption::CheckGeometry
                                                |Data::SearchOption::SingleResult);
                if (names.empty())
                    continue;
                prefix += std::to_string(i);
                internalElementMap[prefix] = names.front();
                internalElementMap[names.front()] = prefix;
                prefix.resize(len);
                names.clear();
            }
        }
    }
    return internalElementMap;
}

Part::TopoShape SketchObject::buildInternals(const Part::TopoShape &edges) const
{
    if (!MakeInternals.getValue())
        return Part::TopoShape();

    try {
        Part::WireJoiner joiner;
        joiner.setTolerance(InternalTolerance.getValue());
        joiner.setTightBound(true);
        joiner.setMergeEdges(true);
        joiner.addShape(edges);
        Part::TopoShape result(getID(), getDocument()->getStringHasher());
        if (!joiner.Shape().IsNull()) {
            joiner.getResultWires(result, "SKF");

            // NOTE: we set minElementNames to 2 (i.e to use at least two
            // unused edge name to construct face name) in order to reduce the
            // chance of face jumpping.
            result = result.makEFace(result.getSubTopoShapes(TopAbs_WIRE), 
                                     /*op*/"",
                                     /*maker*/"Part::FaceMakerRing",
                                     /*pln*/nullptr,
#if 1
                                     /*minElementNames (revert to 1 for now, see how it fairs)*/1
#else
                                     /*minElementNames*/2
#endif
                                    );
        }
        Part::TopoShape openWires(getID(), getDocument()->getStringHasher());
        joiner.getOpenWires(openWires, "SKF");
        if (openWires.isNull())
            return result;
        if (result.isNull())
            return openWires;
        return result.makECompound({result, openWires});
    } catch (Base::Exception &e) {
        FC_WARN("Failed to make face for sketch: " << e.what());
    } catch (Standard_Failure &e) {
        FC_WARN("Failed to make face for sketch: " << e.GetMessageString());
    }
    return Part::TopoShape();
}

static const char *hasSketchMarker(const char *name) {
    static std::string marker(Data::elementMapPrefix()+Part::OpCodes::Sketch);
    if (!name)
        return nullptr;
    return strstr(name,marker.c_str());
}

namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

BOOST_GEOMETRY_REGISTER_POINT_3D(
        Base::Vector3d,double,bg::cs::cartesian,x,y,z)

class SketchObject::GeoHistory {
public:
    typedef bgi::linear<16> Parameters;

    typedef std::set<long> IdSet;
    typedef std::pair<IdSet,IdSet> IdSets;
    typedef std::list<IdSet> AdjList;

    //associate a geo with connected ones on both points
    typedef std::map<long, IdSets> AdjMap;

    // maps start/end points to all existing geo to query and update adjacencies
    typedef std::pair<Base::Vector3d,AdjList::iterator> Value;

    AdjList adjlist;
    AdjMap adjmap;
    bgi::rtree<Value,Parameters> rtree;

    AdjList::iterator find(const Base::Vector3d &pt,bool strict=true){
        std::vector<Value> ret;
        rtree.query(bgi::nearest(pt,1),std::back_inserter(ret));
        if(ret.size()) {
            // NOTE: we are using square distance here, the 1e-6 threshold is
            // very forgiving. We should have used Precision::SquareConfisuion(),
            // which is 1e-14. However, there is a problem with current
            // commandGeoCreate. They create new geometry with initial point of
            // the exact mouse position, instead of the pre-selected point
            // position, and rely on auto constraint to snap in the new
            // geometry. So, we cannot use a very strict threshold here.
            double tol = strict?Precision::SquareConfusion()*10:1e-6;
            double d = Base::DistanceP2(ret[0].first,pt);
            if(d<tol) {
                if(!strict) FC_TRACE("hit " << FC_xyz(pt));
                return ret[0].second;
            }
            if(!strict)
                FC_TRACE("miss " << FC_xyz(pt) << ", " << FC_xyz(ret[0].first) << ", " << d);
        }else if(!strict)
            FC_TRACE("miss " << FC_xyz(pt));
        return adjlist.end();
    }

    void clear() {
        rtree.clear();
        adjlist.clear();
    }

    void update(const Base::Vector3d &pt, long id) {
        FC_TRACE("update " << id << ", " << FC_xyz(pt));
        auto it = find(pt);
        if(it==adjlist.end()) {
            adjlist.emplace_back();
            it = adjlist.end();
            --it;
            rtree.insert(std::make_pair(pt,it));
        }
        it->insert(id);
    }

    void finishUpdate(const std::map<long,int> &geomap) {
        IdSet oldset;
        for(auto &idset : adjlist) {
            oldset.clear();
            for(long _id : idset) {
                long id = abs(_id);
                auto &v = adjmap[id];
                auto &adj = _id>0?v.first:v.second;
                for(auto it=adj.begin(),itNext=it;it!=adj.end();it=itNext) {
                    ++itNext;
                    long other = *it;
                    if(geomap.find(other) == geomap.end()) {
                        // remember those deleted id's
                        oldset.insert(other);
                        FC_TRACE("insert old " << id << ", " << other);
                    } else if(idset.find(other)==idset.end()) {
                        // delete any existing id's that are no longer in the adj list
                        FC_TRACE("erase " << id << ", " << other);
                        adj.erase(it);
                    }
                }
                // now merge the current ones
                for(long _id2 : idset) {
                    long id2 = abs(_id2);
                    if(id!=id2) {
                        adj.insert(id2);
                        FC_TRACE("insert new " << id << ", " << id2);
                    }
                }
            }
            // now reset the adjacency list with only those deleted id's,
            // because the whole purpose of this history is to try to reuse
            // deleted id.
            idset.swap(oldset);
        }
    }

    AdjList::iterator end() {
        return adjlist.end();
    }

    size_t size() {
        return rtree.size();
    }
};

void SketchObject::updateGeoHistory() {
    if(!geoHistoryLevel) return;

    if(!geoHistory)
        geoHistory.reset(new GeoHistory);

    FC_TIME_INIT(t);
    const auto &geos = getInternalGeometry();
    geoHistory->clear();
    for(int i=0;i<(int)geos.size();++i) {
        auto geo = geos[i];
        auto pstart = getPoint(geo,PointPos::start);
        auto pend = getPoint(geo,PointPos::end);
        int id = GeometryFacade::getId(geo);
        geoHistory->update(pstart,id);
        if(pstart!=pend)
            geoHistory->update(pend,-id);
    }
    geoHistory->finishUpdate(geoMap);
    FC_TIME_LOG(t,"update geometry history (" << geoHistory->size() << ", " << geoMap.size()<<')');
}

void SketchObject::generateId(Part::Geometry *geo) {
    if(!geoHistoryLevel) {
        GeometryFacade::setId(geo, ++geoLastId);
        geoMap[GeometryFacade::getId(geo)] = (long)Geometry.getSize();
        return;
    }

    if(!geoHistory)
        updateGeoHistory();

    // Search geo history to see if the start point and end point belongs to
    // some deleted geometries. Prefer matching both start and end point. If
    // can't then try start and then end. Generate new id if none is found.
    auto pstart = getPoint(geo,PointPos::start);
    auto it = geoHistory->find(pstart,false);
    auto pend = getPoint(geo,PointPos::end);
    auto it2 = it;
    if(pstart!=pend) {
        it2 = geoHistory->find(pend,false);
        if(it2 == geoHistory->end())
            it2 = it;
    }
    long newId = -1;
    std::vector<long> found;

    if(geoHistoryLevel<=1 && (it==geoHistory->end() || it2==it)) {
        // level<=1 means we only reuse id if both start and end matches
        newId = ++geoLastId;
        goto END;
    }

    if(it!=geoHistory->end()) {
        for(long id  : *it) {
            if(geoMap.find(id)==geoMap.end()) {
                if(it2 == it) {
                    newId = id;
                    goto END;
                }
                found.push_back(id);
            }else
                FC_TRACE("ignore " << id);
        }
    }
    if(it2!=it) {
        if(found.empty()) {
            // no candidate exists
            for(long id : *it2) {
                if(geoMap.find(id)==geoMap.end()) {
                    newId = id;
                    goto END;
                }
                FC_TRACE("ignore " << id);
            }
        }else{
            // already some candidate exists, search for matching of both
            // points
            for(long id : found) {
                if(it2->find(id)!=it2->end()) {
                    newId = id;
                    goto END;
                }
                FC_TRACE("ignore " << id);
            }
        }
    }
    if(found.size()) {
        FC_TRACE("found " << found.front());
        newId = found.front();
    }else
        newId = ++geoLastId;
END:
    GeometryFacade::setId(geo, newId);
    geoMap[newId] = (long)Geometry.getSize();
}

void SketchObject::acceptGeometry()
{
    Constraints.acceptGeometry(getCompleteGeometry());
    rebuildVertexIndex();
    signalElementsChanged();
}

int SketchObject::setGeometry(int GeoId, const Part::Geometry *geo) {
    std::unique_ptr<Part::Geometry> g(geo->clone());
    if(GeoId>=0 && GeoId <Geometry.getSize()) {
        Geometry.set1Value(GeoId,std::move(g));
    } else if(GeoId <= GeoEnum::RefExt && -GeoId-1 < ExternalGeo.getSize()) {
        ExternalGeo.set1Value(-GeoId-1,std::move(g));
    } else
        return -1;
    return 0;
}

bool SketchObject::evaluateSupport()
{
    // returns false if the shape is broken, null or non-planar
    App::DocumentObject *link = Support.getValue();
    if (!link || !link->isDerivedFrom<Part::Feature>())
        return false;
    return true;
}

PyObject* SketchObject::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new SketchObjectPy(this), true);
    }
    return Py::new_reference_to(PythonObject);
}

unsigned int SketchObject::getMemSize() const
{
    return 0;
}

void SketchObject::Save(Writer& writer) const
{
    int index = -1;
    auto &geos = const_cast<Part::PropertyGeometryList&>(ExternalGeo).getValues();
    for(auto geo : geos)
        ExternalGeometryFacade::getFacade(geo)->setRefIndex(-1);

    if(isExporting()) {
        // We cannot export shape with the new topological naming, because it
        // uses hasher indices that are unique only within its owner document.
        // Therefore, we cannot rely on Geometry::Ref as key to map geometry to
        // external object reference. So, before exporting, we pre-calculate
        // the mapping and store them in Geometry::RefIndex. When importing,
        // inside updateGeometryRefs() (called by onDocumentRestore()), we shall
        // regenerate Geometry::Ref based on RefIndex.
        //
        // Note that the regenerated Ref will not be using the new topological
        // naming either, because we didn't export them.  This is exactly the
        // same as if we are opening a legacy file without new names.
        // updateGeometryRefs() will know how to handle the name change thanks
        // to a flag setup in onUpdateElementReference().
        for(auto &key : externalGeoRef) {
            ++index;
            auto iter = externalGeoRefMap.find(key);
            if(iter == externalGeoRefMap.end())
                continue;
            for(auto id : iter->second) {
                auto it = externalGeoMap.find(id);
                if(it != externalGeoMap.end())
                    ExternalGeometryFacade::getFacade(geos[it->second])->setRefIndex(index);
            }
        }
    }

    // save the father classes
    Part::Part2DObject::Save(writer);
}

void SketchObject::Restore(XMLReader& reader)
{
    // read the father classes
    Part::Part2DObject::Restore(reader);
}

void SketchObject::handleChangedPropertyType(Base::XMLReader &reader,
        const char *TypeName, App::Property *prop)
{
    if (prop == &Exports) {
        if(strcmp(TypeName, "App::PropertyLinkList") == 0)
            Exports.Restore(reader);
    }
}

static inline bool checkMigration(Part::PropertyGeometryList &prop)
{
    for (auto g : prop.getValues()) {
        if(g->hasExtension(Part::GeometryMigrationExtension::getClassTypeId())
            || !g->hasExtension(SketchGeometryExtension::getClassTypeId()))
            return true;
    }
    return false;
}

void SketchObject::onChanged(const App::Property* prop)
{
    if (prop == &Geometry) {
        if (isRestoring() && checkMigration(Geometry)) {
            // Construction migration to extension
            for( auto g : Geometry.getValues()) {
                if(g->hasExtension(Part::GeometryMigrationExtension::getClassTypeId())) {
                    auto ext = std::static_pointer_cast<Part::GeometryMigrationExtension>(
                                    g->getExtension(Part::GeometryMigrationExtension::getClassTypeId()).lock());

                    auto gf = GeometryFacade::getFacade(g); // at this point IA geometry is already migrated

                    if(ext->testMigrationType(Part::GeometryMigrationExtension::Construction)) {
                        bool oldconstr =  ext->getConstruction();
                        if( g->getTypeId() == Part::GeomPoint::getClassTypeId() && !gf->isInternalAligned())
                            oldconstr = true;
                        gf->setConstruction(oldconstr);
                    }
                    if(ext->testMigrationType(Part::GeometryMigrationExtension::GeometryId))
                        gf->setId(ext->getId());
                }
            }
        }
        geoMap.clear();
        const auto &vals = getInternalGeometry();
        for(long i=0;i<(long)vals.size();++i) {
            auto geo = vals[i];
            auto gf = GeometryFacade::getFacade(geo);
            if(!gf->getId())
                gf->setId(++geoLastId);
            else if(gf->getId() > geoLastId)
                geoLastId = gf->getId();
            while(!geoMap.insert(std::make_pair(gf->getId(),i)).second) {
                FC_WARN("duplicate geometry id " << gf->getId() << " -> " << geoLastId+1);
                gf->setId(++geoLastId);
            }
        }
        updateGeoHistory();
    }

    auto doc = getDocument();

    if (prop == &Geometry || prop == &Constraints) {
        if (doc && doc->isPerformingTransaction()) {// undo/redo
            setStatus(App::PendingTransactionUpdate, true);
        }
        else {
            if (!internaltransaction) {
                // internal sketchobject operations changing both geometry and constraints will
                // explicitly perform an update
                if (prop == &Geometry) {
                    if (managedoperation || isRestoring()) {
                        // if geometry changed, the constraint geometry indices must be updated
                        acceptGeometry();
                    }
                    else {
                        // this change was not effect via SketchObject, but using direct access to
                        // properties, check input data

                        // declares constraint invalid if indices go beyond the geometry and any
                        // call to getValues with return an empty list until this is fixed.
                        bool invalidinput = Constraints.checkConstraintIndices(
                            getHighestCurveIndex(), -getExternalGeometryCount());

                        if (!invalidinput) {
                            acceptGeometry();
                        }
                        else {
                            Base::Console().Error(
                                "SketchObject::onChanged(): Unmanaged change of Geometry Property "
                                "results in invalid constraint indices\n");
                        }
                        Base::StateLocker lock(internaltransaction, true);
                        setUpSketch();
                    }
                }
                else {// Change is in Constraints

                    if (managedoperation || isRestoring()) {
                        Constraints.checkGeometry(getCompleteGeometry());
                    }
                    else {
                        // this change was not effect via SketchObject, but using direct access to
                        // properties, check input data

                        // declares constraint invalid if indices go beyond the geometry and any
                        // call to getValues with return an empty list until this is fixed.
                        bool invalidinput = Constraints.checkConstraintIndices(
                            getHighestCurveIndex(), -getExternalGeometryCount());

                        if (!invalidinput) {
                            if (Constraints.checkGeometry(getCompleteGeometry())) {
                                // if there are invalid geometry indices in the constraints, we need
                                // to update them
                                acceptGeometry();
                            }
                        }
                        else {
                            Base::Console().Error(
                                "SketchObject::onChanged(): Unmanaged change of Constraint "
                                "Property results in invalid constraint indices\n");
                        }
                        Base::StateLocker lock(internaltransaction, true);
                        setUpSketch();
                    }
                }
            }
        }

    } else if ( prop == &ExternalGeo && !prop->testStatus(App::Property::User3) ) {
        if(doc && doc->isPerformingTransaction())
            setStatus(App::PendingTransactionUpdate, true);

        if (isRestoring() && checkMigration(ExternalGeo)) {
            for( auto g : ExternalGeo.getValues()) {
                if(g->hasExtension(Part::GeometryMigrationExtension::getClassTypeId())) {
                    auto ext = std::static_pointer_cast<Part::GeometryMigrationExtension>(
                                    g->getExtension(Part::GeometryMigrationExtension::getClassTypeId()).lock());
                    std::unique_ptr<ExternalGeometryFacade> egf;
                    if(ext->testMigrationType(Part::GeometryMigrationExtension::GeometryId)) {
                        egf = ExternalGeometryFacade::getFacade(g);
                        egf->setId(ext->getId());
                    }

                    if(ext->testMigrationType(Part::GeometryMigrationExtension::ExternalReference)) {
                        if (!egf)
                            egf = ExternalGeometryFacade::getFacade(g);
                        egf->setRef(ext->getRef());
                        egf->setRefIndex(ext->getRefIndex());
                        egf->setFlags(ext->getFlags());
                    }
                }
            }
        }
        externalGeoRefMap.clear();
        externalGeoMap.clear();
        std::set<std::string> detached;
        for(int i=0;i<ExternalGeo.getSize();++i) {
            auto geo = ExternalGeo[i];
            auto egf = ExternalGeometryFacade::getFacade(geo);
            if(egf->testFlag(ExternalGeometryExtension::Detached)) {
                if(egf->getRef().size()) {
                    detached.insert(egf->getRef());
                    egf->setRef(std::string());
                }
                egf->setFlag(ExternalGeometryExtension::Detached,false);
                egf->setFlag(ExternalGeometryExtension::Missing,false);
            }
            if(egf->getId() > geoLastId)
                geoLastId = egf->getId();
            if(!externalGeoMap.emplace(egf->getId(),i).second) {
                FC_WARN("duplicate geometry id " << egf->getId() << " -> " << geoLastId+1);
                egf->setId(++geoLastId);
                externalGeoMap[egf->getId()] = i;
            }
            if(egf->getRef().size())
                externalGeoRefMap[egf->getRef()].push_back(egf->getId());
        }
        if(detached.size()) {
            auto objs = ExternalGeometry.getValues();
            if (externalGeoRef.size() != objs.size()) {
                throw Base::RuntimeError("Inconsistency with external geometries");
            }
            auto itObj = objs.begin();
            auto subs = ExternalGeometry.getSubValues();
            auto itSub = subs.begin();
            for(size_t i=0;i<externalGeoRef.size();++i) {
                if(detached.count(externalGeoRef[i])) {
                    itObj = objs.erase(itObj);
                    itSub = subs.erase(itSub);
                    auto &refs = externalGeoRefMap[externalGeoRef[i]];
                    for(long id : refs) {
                        auto it = externalGeoMap.find(id);
                        if(it!=externalGeoMap.end()) {
                            auto geo = ExternalGeo[it->second];
                            ExternalGeometryFacade::getFacade(geo)->setRef(std::string());
                        }
                    }
                    refs.clear();
                } else {
                    ++itObj;
                    ++itSub;
                }
            }
            ExternalGeometry.setValues(objs,subs);
        } else {
            // Make sure to inform view provider first before emit signal for
            // editting task
            inherited::onChanged(prop);
            signalElementsChanged();
            return;
        }
    } else if( prop == &ExternalGeometry ) {

        if(doc && doc->isPerformingTransaction())
            setStatus(App::PendingTransactionUpdate, true);

        if(!isRestoring()) {
            // must wait till onDocumentRestored() when shadow references are
            // fully restored
            updateGeometryRefs();

            // Make sure to inform view provider first before emit signal for
            // editting task
            inherited::onChanged(prop);
            signalElementsChanged();
            return;
        }
    } else if (prop == &Placement) {
        if (ExternalGeometry.getSize() > 0)
            touch();
    } else if (prop == &ExpressionEngine) {
        auto doc = getDocument();
        if(!isRestoring()
                && doc && !doc->isPerformingTransaction()
                && noRecomputes
                && !managedoperation)
        {
            // if we do not have a recompute, the sketch must be solved to
            // update the DoF of the solver, constraints and UI
            try {
                auto res = ExpressionEngine.execute();
                if(res) {
                    FC_ERR("Failed to recompute " << ExpressionEngine.getFullName() << ": " << res->Why);
                    delete res;
                }
            } catch (Base::Exception &e) {
                e.ReportException();
                FC_ERR("Failed to recompute " << ExpressionEngine.getFullName() << ": " << e.what());
            }
            solve();
        }
    }

    inherited::onChanged(prop);
}

void SketchObject::onUpdateElementReference(const App::Property *prop) {
    if(prop == &ExternalGeometry) {
        updateGeoRef = true;
        // Must call updateGeometryRefs() now to avoid the case of recursive
        // property change (e.g. temporary object removal in SubShapeBinder)
        // afterwards causing assertion failure, although this may mean extra
        // call of updateGeometryRefs() later in onChange().
        updateGeometryRefs();
        signalElementsChanged();
    }
}

void SketchObject::onUndoRedoFinished()
{
    // upon undo/redo, PropertyConstraintList does not have updated valid geometry keys, which
    // results in empty constraint lists when using getValues
    //
    // The sketch will also have invalid vertex indices, requiring a call to rebuildVertexIndex
    //
    // Historically this was "solved" by issuing a recompute, which is absolutely unnecessary and
    // prevents solve() from working before such a recompute in case it is redoing an operation with
    // invalid data.
    Constraints.checkConstraintIndices(getHighestCurveIndex(), -getExternalGeometryCount());
    acceptGeometry();
    synchroniseGeometryState();
    solve();
}

void SketchObject::synchroniseGeometryState()
{
    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    for (size_t i = 0; i < vals.size(); i++) {
        auto gf = GeometryFacade::getFacade(vals[i]);

        auto facadeInternalAlignment = gf->getInternalType();
        auto facadeBlockedState = gf->getBlocked();

        Sketcher::InternalType::InternalType constraintInternalAlignment = InternalType::None;
        bool constraintBlockedState = false;

        for (auto cstr : Constraints.getValues()) {
            if (cstr->First == int(i)) {
                getInternalTypeState(cstr, constraintInternalAlignment);
                getBlockedState(cstr, constraintBlockedState);
            }
        }

        if (constraintInternalAlignment != facadeInternalAlignment)
            gf->setInternalType(constraintInternalAlignment);

        if (constraintBlockedState != facadeBlockedState)
            gf->setBlocked(constraintBlockedState);
    }
}

void SketchObject::onDocumentRestored()
{
    restoreFinished();
    Part::Part2DObject::onDocumentRestored();

    if (getDocument()->testStatus(App::Document::Importing)) {
        App::GeoFeatureGroupExtension *grp = nullptr;
        auto grpObj = App::GeoFeatureGroupExtension::getGroupOfObject(this);
        if (grpObj)
            grp = grpObj->getExtensionByType<App::GeoFeatureGroupExtension>(true);
        
        auto exports = Exports.getValues();
        bool touched = false;
        for (auto &obj : exports) {
            auto exp = Base::freecad_dynamic_cast<SketchExport>(obj);
            if (!exp || exp->BaseRefs.getValue() == this)
                continue;
            auto newexp = Base::freecad_dynamic_cast<SketchExport>(
                    getDocument()->addObject("Sketcher::SketchExport", "Export"));
            if (grp)
                grp->addObject(newexp);
            if (exp->Label.getStrValue() != exp->getNameInDocument())
                newexp->Label.setValue(exp->Label.getValue());
            else
                newexp->Label.setValue(newexp->getNameInDocument());
            newexp->BaseRefs.setValue(this, exp->BaseRefs.getSubValues(false));
            newexp->Visibility.setValue(exp->Visibility.getValue());
            obj = newexp;
            touched = true;
        }
        if (touched)
            Exports.setValues(exports);
    }
}

void SketchObject::restoreFinished()
{
    try {
        migrateSketch();

        updateGeometryRefs();
        if(ExternalGeo.getSize()<=2) {
            if (ExternalGeo.getSize() < 2)
                initExternalGeo();
            for(auto &key : externalGeoRef) {
                long id = getDocument()->getHasher()->getID(key.c_str()).value();
                if(geoLastId < id)
                    geoLastId = id;
                externalGeoRefMap[key].push_back(id);
            }
            rebuildExternalGeometry();
            if(ExternalGeometry.getSize()+2!=ExternalGeo.getSize())
                FC_WARN("Failed to restore some external geometry in " << getFullName());
        }else
            acceptGeometry();

        // Must run after the external geometry above: the orientations are derived from the
        // geometry the constraints reference, and projected external geometry does not exist
        // before it is rebuilt or accepted.
        migrateConstraintOrientations();

        synchroniseGeometryState();

        // this may happen when saving a sketch directly in edit mode
        // but never performed a recompute before
        if (Shape.getValue().IsNull() && hasConflicts() == 0) {
            if (this->solve(true) == SketchSolveStatus::Success)
                Shape.setValue(solvedSketch.toShape());
        }

        // Sanity check on constraints with expression. It is added because the
        // way SketchObject syncs expression and constraints heavily relies on
        // proper setup of undo/redo transactions. The missing transaction in
        // EditDatumDialog may cause stray or worse wrongly bound expressions.
        for (auto &v : ExpressionEngine.getExpressions()) {
            if (v.first.getProperty() != &Constraints)
                continue;
            const Constraint * cstr = nullptr;
            try {
                cstr = Constraints.getConstraint(v.first);
            } catch (Base::Exception &) {
            }
            if (!cstr || !cstr->isDimensional()) {
                FC_WARN((cstr ? "Invalid" : "Orphan")
                        << " constraint expression in "
                        << getFullName() << "."
                        << v.first.toString()
                        << ": " << v.second->toString());
                ExpressionEngine.setValue(v.first, nullptr);
            }
        }
    } catch (Base::Exception &e) {
        e.ReportException();
        FC_ERR("Error while restoring " << getFullName());
    } catch (...) {
    }
}

void SketchObject::migrateConstraintOrientations()
{
    // Migrate point-line and circle-line distance and tangency from unsigned to signed. Documents
    // written before signed constraints existed carry no orientation at all, so the side each
    // constraint was solved on has to be read back out of the geometry stored in the file.
    //
    // Unlike upstream, set only the constraints that change, and only as clones: an in-place edit
    // is already in the snapshot the property compares against, so the write would be dropped as
    // a no-op, and a sketch with nothing to migrate is not written to at all.
    auto constraints = Constraints.getValues();
    bool changed = false;
    for (auto& constr : constraints) {
        if (!constr->Orientation.testFlag(ConstraintOrientations::None)) {
            continue;
        }
        std::unique_ptr<Constraint> oriented(constr->clone());
        setOrientation(oriented.get(), false);
        if (oriented->Orientation.testFlag(ConstraintOrientations::None)) {
            continue;
        }
        constr = oriented.release();
        changed = true;
    }

    if (changed) {
        Constraints.setValues(std::move(constraints));
    }
}

void SketchObject::migrateSketch()
{
    if (!checkMigration(Geometry))
        return;

    for( auto c : Constraints.getValues()) {

        addGeometryState(c);

        // Convert B-Spline controlpoints radius/diameter constraints to Weight constraints
        if(c->Type == InternalAlignment && c->AlignmentType == BSplineControlPoint) {
            int circlegeoid = c->First;
            int bsplinegeoid = c->Second;

            auto bsp = static_cast<const Part::GeomBSplineCurve*>(getGeometry(bsplinegeoid));

            std::vector<double> weights = bsp->getWeights();

            for( auto ccp : Constraints.getValues()) {

                if( (ccp->Type == Radius || ccp->Type == Diameter ) &&
                    ccp->First == circlegeoid ) {

                    if(c->InternalAlignmentIndex < int(weights.size())) {
                        ccp->Type = Weight;
                        ccp->setValue(weights[c->InternalAlignmentIndex]);

                    }
                }
            }
        }
    }

    for (auto g : Geometry.getValues())
        g->deleteExtension(Part::GeometryMigrationExtension::getClassTypeId());
    for (auto g : ExternalGeo.getValues())
        g->deleteExtension(Part::GeometryMigrationExtension::getClassTypeId());

    /* parabola axis as internal geometry */
    auto constraints = Constraints.getValues();
    auto geometries = getInternalGeometry();

    auto parabolafound = std::find_if(geometries.begin(), geometries.end(), [](Part::Geometry* g) {
        return g->is<Part::GeomArcOfParabola>();
    });

    if (parabolafound != geometries.end()) {

        auto focalaxisfound = std::find_if(constraints.begin(), constraints.end(), [](auto c) {
            return c->Type == InternalAlignment && c->AlignmentType == ParabolaFocalAxis;
        });

        // There are parabolas and there isn't an IA axis. (1) there are no axis or (2) there is a
        // legacy construction line
        if (focalaxisfound == constraints.end()) {

            // maps parabola geoid to focusgeoid
            std::map<int, int> parabolageoid2focusgeoid;

            // populate parabola and focus geoids
            for (const auto& c : constraints) {
                if (c->Type == InternalAlignment && c->AlignmentType == ParabolaFocus) {
                    parabolageoid2focusgeoid[c->Second] = {c->First};
                }
            }

            // maps axis geoid to parabolageoid
            std::map<int, int> axisgeoid2parabolageoid;

            // populate axis geoid
            for (const auto& [parabolageoid, focusgeoid] : parabolageoid2focusgeoid) {
                // look for a line from focusgeoid:start to Geoid:mid_external
                std::vector<int> focusgeoidlistgeoidlist;
                std::vector<PointPos> focusposidlist;
                getDirectlyCoincidentPoints(
                    focusgeoid, Sketcher::PointPos::start, focusgeoidlistgeoidlist, focusposidlist);

                std::vector<int> parabgeoidlistgeoidlist;
                std::vector<PointPos> parabposidlist;
                getDirectlyCoincidentPoints(parabolageoid,
                                            Sketcher::PointPos::mid,
                                            parabgeoidlistgeoidlist,
                                            parabposidlist);

                if (!focusgeoidlistgeoidlist.empty() && !parabgeoidlistgeoidlist.empty()) {
                    std::size_t i, j;
                    for (i = 0; i < focusgeoidlistgeoidlist.size(); i++) {
                        for (j = 0; j < parabgeoidlistgeoidlist.size(); j++) {
                            if (focusgeoidlistgeoidlist[i] == parabgeoidlistgeoidlist[j]) {
                                axisgeoid2parabolageoid[focusgeoidlistgeoidlist[i]] = parabolageoid;
                            }
                        }
                    }
                }
            }

            std::vector<Constraint*> newconstraints;
            newconstraints.reserve(constraints.size());

            for (const auto& c : constraints) {

                if (c->Type != Coincident) {
                    newconstraints.push_back(c);
                }
                else {
                    auto axismajorcoincidentfound =
                        std::find_if(axisgeoid2parabolageoid.begin(),
                                     axisgeoid2parabolageoid.end(),
                                     [&](const auto& pair) {
                                         auto parabolageoid = pair.second;
                                         auto axisgeoid = pair.first;
                                         return (c->First == axisgeoid && c->Second == parabolageoid
                                                 && c->SecondPos == PointPos::mid)
                                             || (c->Second == axisgeoid && c->First == parabolageoid
                                                 && c->FirstPos == PointPos::mid);
                                     });

                    if (axismajorcoincidentfound != axisgeoid2parabolageoid.end()) {
                        // we skip this coincident, the other coincident on axis will be substituted
                        // by internal geometry constraint
                        continue;
                    }

                    auto focuscoincidentfound =
                        std::find_if(axisgeoid2parabolageoid.begin(),
                                     axisgeoid2parabolageoid.end(),
                                     [&](const auto& pair) {
                                         auto parabolageoid = pair.second;
                                         auto axisgeoid = pair.first;
                                         auto focusgeoid = parabolageoid2focusgeoid[parabolageoid];
                                         return (c->First == axisgeoid && c->Second == focusgeoid
                                                 && c->SecondPos == PointPos::start)
                                             || (c->Second == axisgeoid && c->First == focusgeoid
                                                 && c->FirstPos == PointPos::start);
                                     });

                    if (focuscoincidentfound != axisgeoid2parabolageoid.end()) {
                        Sketcher::Constraint* newConstr = new Sketcher::Constraint();
                        newConstr->Type = Sketcher::InternalAlignment;
                        newConstr->AlignmentType = Sketcher::ParabolaFocalAxis;
                        newConstr->First = focuscoincidentfound->first;// axis geoid
                        newConstr->FirstPos = Sketcher::PointPos::none;
                        newConstr->Second = focuscoincidentfound->second;// parabola geoid
                        newConstr->SecondPos = Sketcher::PointPos::none;
                        newconstraints.push_back(newConstr);

                        addGeometryState(newConstr);

                        // we skip the coincident, as we have substituted it by internal geometry
                        // constraint
                        continue;
                    }

                    newconstraints.push_back(c);
                }
            }

            Constraints.setValues(std::move(newconstraints));

            Base::Console().Critical(
                this->getFullName(),
                QT_TRANSLATE_NOOP("Notifications",
                                  "Parabolas were migrated. Migrated files won't open in previous "
                                  "versions of FreeCAD!!\n"));
        }
    }
}

App::DocumentObject *SketchObject::getSubObject(
        const char *subname, PyObject **pyObj,
        Base::Matrix4D *pmat, bool transform, int depth) const
{
    while(subname && *subname=='.') ++subname; // skip leading .
    std::string sub;
    const char *mapped = Data::isMappedElement(subname);
    if(!subname || !subname[0])
        return Part2DObject::getSubObject(subname,pyObj,pmat,transform,depth);
    const char *element = Data::findElementName(subname);
    if(element != subname) {
        const char *dot = strchr(subname,'.');
        if(!dot)
            return 0;
        std::string name(subname,dot-subname);
        auto child = Exports.find(name.c_str());
        if(!child)
            return 0;
        return child->getSubObject(dot+1,pyObj,pmat,true,depth+1);
    }

    Data::IndexedName indexedName = checkSubName(subname);
    int index = indexedName.getIndex();
    const char * shapetype = indexedName.getType();
    const Part::Geometry *geo = 0;
    Part::TopoShape subshape;
    Base::Vector3d point;

    if (auto realType = convertInternalName(indexedName.getType())) {
        if (realType[0] == '\0')
            subshape = InternalShape.getShape();
        else {
            auto shapeType = Part::TopoShape::shapeType(realType, true);
            if (shapeType != TopAbs_SHAPE)
                subshape = InternalShape.getShape().getSubTopoShape(shapeType, indexedName.getIndex(), true);
        }
        if (subshape.isNull())
            return nullptr;
    }
    else if (!pyObj || !mapped) {
        if (!pyObj 
                || (index > 0
                    && !boost::algorithm::contains(subname, "edge") 
                    && !boost::algorithm::contains(subname, "vertex")))
            return Part2DObject::getSubObject(subname,pyObj,pmat,transform,depth);
    } else {
        subshape = Shape.getShape().getSubTopoShape(subname, true);
        if (!subshape.isNull())
            return Part2DObject::getSubObject(subname,pyObj,pmat,transform,depth);
    }

    if (subshape.isNull()) {
        if (boost::equals(shapetype,"Edge") ||
            boost::equals(shapetype,"edge")) {
            geo = getGeometry(index - 1);
            if (!geo)
                return nullptr;
        } else if (boost::equals(shapetype,"ExternalEdge")) {
            int GeoId = index - 1;
            GeoId = -GeoId - 3;
            geo = getGeometry(GeoId);
            if(!geo)
                return nullptr;
        } else if (boost::equals(shapetype,"Vertex") ||
                boost::equals(shapetype,"vertex")) {
            int VtId = index- 1;
            int GeoId;
            PointPos PosId;
            getGeoVertexIndex(VtId,GeoId,PosId);
            if (PosId==PointPos::none)
                return nullptr;
            point = getPoint(GeoId,PosId);
        }
        else if (boost::equals(shapetype,"RootPoint"))
            point = getPoint(Sketcher::GeoEnum::RtPnt,PointPos::start);
        else if (boost::equals(shapetype,"H_Axis"))
            geo = getGeometry(Sketcher::GeoEnum::HAxis);
        else if (boost::equals(shapetype,"V_Axis"))
            geo = getGeometry(Sketcher::GeoEnum::VAxis);
        else if (boost::equals(shapetype,"Constraint")) {
            int ConstrId = PropertyConstraintList::getIndexFromConstraintName(shapetype);
            const std::vector< Constraint * > &vals = this->Constraints.getValues();
            if (ConstrId < 0 || ConstrId >= int(vals.size()))
                return nullptr;
            if(pyObj)
                *pyObj = vals[ConstrId]->getPyObject();
            return const_cast<SketchObject*>(this);
        } else
            return nullptr;
    }

    if (pmat && transform)
        *pmat *= Placement.getValue().toMatrix();

    if (pyObj) {
        Part::TopoShape shape;
        std::string name = convertSubName(indexedName,false);
        if (geo) {
            shape = getEdge(geo,name.c_str());
            if(pmat && !shape.isNull())
                shape.transformShape(*pmat,false,true);
        } else if (!subshape.isNull()) {
            shape = subshape;
            if (pmat)
                shape.transformShape(*pmat,false,true);
        } else {
            if(pmat)
                point = (*pmat)*point;
            shape = BRepBuilderAPI_MakeVertex(gp_Pnt(point.x,point.y,point.z)).Vertex();
            shape.setElementName(Data::IndexedName::fromConst("Vertex", 1), 
                                 Data::MappedName::fromRawData(name.c_str()));
        }
        shape.Tag = getID();
        *pyObj = Py::new_reference_to(Part::shape2pyshape(shape));
    }

    return const_cast<SketchObject*>(this);
}

std::vector<Data::IndexedName>
SketchObject::getHigherElements(const char *element, bool silent) const
{
    if (testStatus(App::ObjEditing)) {
        std::vector<Data::IndexedName> res;
        if (boost::istarts_with(element, "vertex")) {
            int n = 0;
            int index = atoi(element+6);
            for (auto cstr : Constraints.getValues()) {
                ++n;
                if (cstr->Type != Sketcher::Coincident)
                    continue;
                for (int i=0; i<2; ++i) {
                    int geoid = i ? cstr->Second : cstr->First;
                    const Sketcher::PointPos &pos = i ? cstr->SecondPos : cstr->FirstPos;
                    if(geoid >= 0 && index == getSolvedSketch().getPointId(geoid, pos) + 1)
                        res.push_back(Data::IndexedName::fromConst("Constraint", n));
                };
            }
        }
        return res;
    }

    std::vector<Data::IndexedName> res;
    auto getNames = [&](const char *element) {
        bool internal = boost::starts_with(element, internalPrefix());
        const auto &shape = internal ? InternalShape.getShape() : Shape.getShape();
        for (const auto &indexedName : shape.getHigherElements(element+(internal?internalPrefix().size():0), silent)) {
            if (!internal) {
                res.push_back(indexedName);
            }
            else if (boost::equals(indexedName.getType(), "Face")
                    || boost::equals(indexedName.getType(), "Edge")
                    || boost::equals(indexedName.getType(), "Wire")) {
                res.emplace_back((internalPrefix() + indexedName.getType()).c_str(), indexedName.getIndex());
            }
        }
    };
    getNames(element);
    const auto &elementMap = getInternalElementMap();
    auto it = elementMap.find(element);
    if (it != elementMap.end()) {
        res.emplace_back(it->second.c_str());
        getNames(it->second.c_str());
    }
    return res;
}

const std::vector<const char *>& SketchObject::getElementTypes(bool all) const
{
    if (!all)
        return Part::Part2DObject::getElementTypes();
    static std::vector<const char *> res;
    if (res.empty()) {
        res = { Part::TopoShape::shapeName(TopAbs_VERTEX).c_str(),
                Part::TopoShape::shapeName(TopAbs_EDGE).c_str(),
                "ExternalEdge",
                "Constraint",
                "InternalEdge",
                "InternalFace",
                "InternalVertex",
              };
    }
    return res;
}

const std::string &SketchObject::internalPrefix()
{
    static std::string _prefix("Internal");
    return _prefix;
}

const char *SketchObject::convertInternalName(const char *name)
{
    if (name && boost::starts_with(name, internalPrefix()))
        return name + internalPrefix().size();
    return nullptr;
}

std::pair<std::string,std::string> SketchObject::getElementName(
        const char *name, ElementNameType type) const
{
    std::pair<std::string, std::string> ret;
    if(!name) return ret;

    if(hasSketchMarker(name))
        return Part2DObject::getElementName(name,type);

    const char *mapped = Data::isMappedElement(name);
    Data::IndexedName index = checkSubName(name);
    index.appendToStringBuffer(ret.second);
    if (auto realName = convertInternalName(ret.second.c_str())) {
        Data::MappedElement mappedElement;
        if (mapped)
            mappedElement = InternalShape.getShape().getElementName(name);
        else if (type == ElementNameType::Export)
            ret.first = getExportElementName(InternalShape.getShape(), realName).first;
        else
            mappedElement = InternalShape.getShape().getElementName(realName);

        if (mapped || type != ElementNameType::Export) {
            if (mappedElement.index) {
                ret.second = internalPrefix();
                mappedElement.index.appendToStringBuffer(ret.second);
            }
            if (mappedElement.name) {
                ret.first = Data::elementMapPrefix();
                mappedElement.name.appendToBuffer(ret.first);
            }
            else if (mapped)
                ret.first = name;
        }

        if (ret.first.size()) {
            if (auto dot = strrchr(ret.first.c_str(), '.'))
                ret.first.resize(dot+1-ret.first.c_str());
            else
                ret.first += ".";
            ret.first += ret.second;
        }
        if (mapped && (!mappedElement.index || !mappedElement.name))
            ret.second.insert(0, Data::missingPrefix());
        return ret;
    }

    if(!mapped) {
        auto occindex = Part::TopoShape::shapeTypeAndIndex(name);
        if (occindex.second)
            return Part2DObject::getElementName(name,type);
    }
    if(index && type==ElementNameType::Export) {
        if(boost::starts_with(ret.second,"Vertex"))
            ret.second[0] = 'v';
        else if(boost::starts_with(ret.second,"Edge"))
            ret.second[0] = 'e';
    }
    ret.first = convertSubName(index, true);
    if(!Data::isMappedElement(ret.first.c_str()))
        ret.first.clear();
    return ret;
}

Data::IndexedName SketchObject::checkSubName(const char *sub) const{
    static std::vector<const char *> types = {
        "Edge",
        "Vertex",
        "edge",
        "vertex",
        "ExternalEdge",
        "RootPoint",
        "H_Axis",
        "V_Axis",
        "Constraint",
        "InternalEdge",
        "InternalFace",
        "InternalVertex",
    };

    if(!sub) return Data::IndexedName();
    const char *subname = Data::isMappedElement(sub);
    if(!subname)  {
        Data::IndexedName res(sub, types, true);
        if (boost::equals(res.getType(), "edge"))
            return Data::IndexedName("Edge", res.getIndex());
        else if (boost::starts_with(res.getType(), "vertex"))
            return Data::IndexedName("Vertex", res.getIndex());
        return res;
    }
    if(!subname[0]) {
        FC_ERR("invalid subname " << sub);
        return Data::IndexedName();
    }
    bio::stream<bio::array_source> iss(subname+1, std::strlen(subname+1));
    int id = -1;
    bool valid = false;
    switch(subname[0]) {
    case 'g':
    case 'e':
        if(iss>>id)
            valid = true;
        break;
    default: {
        // for RootPoint,H_Axis,V_Axis
        const char *dot = strchr(subname,'.');
        if(dot)
            subname = dot+1;
        return Data::IndexedName(subname, types, false);
    }}
    if(!valid) {
        FC_ERR("invalid subname " << sub);
        // return sub;
        return Data::IndexedName();
    }

    int geoId;
    const Part::Geometry *geo = 0;
    switch(subname[0]) {
    case 'g': {
        auto it = geoMap.find(id);
        if(it!=geoMap.end()) {
            geoId = it->second;
            geo = getGeometry(geoId);
        }
        break;
    } case 'e': {
        auto it = externalGeoMap.find(id);
        if(it!=externalGeoMap.end()) {
            geoId = -it->second - 1;
            geo  = getGeometry(geoId);
        }
        break;
    }}
    if(geo && GeometryFacade::getId(geo) == id) {
        char sep;
        int posId = static_cast<int>(PointPos::none);
        if((iss >> sep >> posId) && sep=='v') {
            int idx = getVertexIndexGeoPos(geoId, static_cast<PointPos>(posId));

            // Outside edit mode a circle exposes its seam point but not its centre, while in
            // edit mode it exposes the centre but not the seam, so looking up a circle's start
            // point (g1v1, which happens outside edit mode) fails. A circle has exactly one
            // vertex either way (upstream issue 25089).
            if (idx < 0
                && (static_cast<PointPos>(posId) == PointPos::start
                    || static_cast<PointPos>(posId) == PointPos::end)
                && (geo->is<Part::GeomCircle>() || geo->is<Part::GeomEllipse>())) {
                idx = getVertexIndexGeoPos(geoId, PointPos::mid);
            }

            if(idx < 0) {
                FC_ERR("invalid subname " << sub);
                // return sub;
                return Data::IndexedName();
            }
            return Data::IndexedName::fromConst("Vertex", idx+1);
        }else if(geoId>=0)
            return Data::IndexedName::fromConst("Edge", geoId+1);
        else
            return Data::IndexedName::fromConst("ExternalEdge", -geoId-2);
    }
    FC_ERR("cannot find subname " << sub);
    return Data::IndexedName();
}

std::string SketchObject::convertSubName(const char *subname, bool postfix) const
{
    return convertSubName(checkSubName(subname), postfix);
}

std::string SketchObject::convertSubName(const Data::IndexedName & indexedName, bool postfix) const{
    std::ostringstream ss;
    if (auto realType = convertInternalName(indexedName.getType())) {
        auto mapped = InternalShape.getShape().getMappedName(
                Data::IndexedName::fromConst(realType, indexedName.getIndex()));
        if (!mapped) {
            if (postfix)
                ss << indexedName;
        } else if (postfix)
            ss << Data::elementMapPrefix() << mapped << '.' << indexedName;
        else
            ss << mapped;
        return ss.str();
    }
    int geoId;
    PointPos posId;
    if(!geoIdFromShapeType(indexedName,geoId,posId)) {
        ss << indexedName;
        return ss.str();
    }
    if(geoId == Sketcher::GeoEnum::HAxis ||
       geoId == Sketcher::GeoEnum::VAxis ||
       geoId == Sketcher::GeoEnum::RtPnt) {
        if (postfix)
            ss << Data::elementMapPrefix();
        ss << indexedName;
        if(postfix)
           ss  << '.' << indexedName;
        return ss.str();
    }

    auto geo = getGeometry(geoId);
    if(!geo) {
        std::string res;
        indexedName.appendToStringBuffer(res);
        return res;
    }
    if (postfix)
        ss << Data::elementMapPrefix();
    ss << (geoId>=0?'g':'e') << GeometryFacade::getId(geo);
    if(posId!=PointPos::none)
        ss << 'v' << static_cast<int>(posId);
    if(postfix) {
        // rename Edge to edge, and Vertex to vertex to avoid ambiguous of
        // element mapping of the public shape and internal geometry.
        if (indexedName.getIndex() <= 0)
            ss << '.' << indexedName;
        else if(boost::starts_with(indexedName.getType(),"Edge"))
            ss << ".e" << (indexedName.getType()+1) << indexedName.getIndex();
        else if(boost::starts_with(indexedName.getType(),"Vertex"))
            ss << ".v" << (indexedName.getType()+1) << indexedName.getIndex();
        else
            ss << '.' << indexedName;
    }
    return ss.str();
}

int SketchObject::autoConstraint(double precision, double angleprecision, bool includeconstruction)
{
    return analyser->autoconstraint(precision, angleprecision, includeconstruction);
}

int SketchObject::detectMissingPointOnPointConstraints(double precision, bool includeconstruction)
{
    return analyser->detectMissingPointOnPointConstraints(precision, includeconstruction);
}

void SketchObject::analyseMissingPointOnPointCoincident(double angleprecision)
{
    analyser->analyseMissingPointOnPointCoincident(angleprecision);
}

int SketchObject::detectMissingVerticalHorizontalConstraints(double angleprecision)
{
    return analyser->detectMissingVerticalHorizontalConstraints(angleprecision);
}

int SketchObject::detectMissingEqualityConstraints(double precision)
{
    return analyser->detectMissingEqualityConstraints(precision);
}

std::vector<ConstraintIds>& SketchObject::getMissingPointOnPointConstraints()
{
    return analyser->getMissingPointOnPointConstraints();
}

std::vector<ConstraintIds>& SketchObject::getMissingVerticalHorizontalConstraints()
{
    return analyser->getMissingVerticalHorizontalConstraints();
}

std::vector<ConstraintIds>& SketchObject::getMissingLineEqualityConstraints()
{
    return analyser->getMissingLineEqualityConstraints();
}

std::vector<ConstraintIds>& SketchObject::getMissingRadiusConstraints()
{
    return analyser->getMissingRadiusConstraints();
}

void SketchObject::setMissingRadiusConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingRadiusConstraints(cl);
}

void SketchObject::setMissingLineEqualityConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingLineEqualityConstraints(cl);
}

void SketchObject::setMissingVerticalHorizontalConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingVerticalHorizontalConstraints(cl);
}

void SketchObject::setMissingPointOnPointConstraints(std::vector<ConstraintIds>& cl)
{
    if (analyser)
        analyser->setMissingPointOnPointConstraints(cl);
}

void SketchObject::makeMissingPointOnPointCoincident(bool onebyone)
{
    if (analyser)
        analyser->makeMissingPointOnPointCoincident(onebyone);
}

void SketchObject::makeMissingVerticalHorizontal(bool onebyone)
{
    if (analyser)
        analyser->makeMissingVerticalHorizontal(onebyone);
}

void SketchObject::makeMissingEquality(bool onebyone)
{
    if (analyser)
        analyser->makeMissingEquality(onebyone);
}

std::vector<Base::Vector3d> SketchObject::getOpenVertices() const
{
    std::vector<Base::Vector3d> points;

    if (analyser)
        points = analyser->getOpenVertices();

    return points;
}

// SketchGeometryExtension interface

// Python Sketcher feature ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(Sketcher::SketchObjectPython, Sketcher::SketchObject)
template<>
const char* Sketcher::SketchObjectPython::getViewProviderName() const
{
    return "SketcherGui::ViewProviderPython";
}
template<>
PyObject* Sketcher::SketchObjectPython::getPyObject()
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new FeaturePythonPyT<SketchObjectPy>(this), true);
    }
    return Py::new_reference_to(PythonObject);
}
/// @endcond

// explicit template instantiation
template class SketcherExport FeaturePythonT<Sketcher::SketchObject>;
}// namespace App

// ---------------------------------------------------------

PROPERTY_SOURCE(Sketcher::SketchExport, Part::Part2DObject)

SketchExport::SketchExport() {
    ADD_PROPERTY_TYPE(Base,(0),"",
            (App::PropertyType)(App::Prop_Hidden|App::Prop_ReadOnly),
            "(Deprecated) Base sketch object name");

    ADD_PROPERTY_TYPE(Refs,(),"",App::Prop_Hidden,
            "(Deprecated) Sketch geometry references");

    ADD_PROPERTY_TYPE(BaseRefs,(0),"",App::Prop_None,"Base sketch references");

    ADD_PROPERTY_TYPE(SyncPlacement,(true),"",
            App::Prop_None,"Synchronize placement with parent sketch if not attached");

    ADD_PROPERTY_TYPE(ScaleVector,    (1, 1, 1)  ,"", App::Prop_None,
            "Scale vector intended for mirror. Note that using the scale vector for\n"
            "non-uniform scaling transformed the geometries into BSpline curves");
}

SketchExport::~SketchExport()
{}

App::DocumentObject *SketchExport::getBase() const {
    return BaseRefs.getValue();
}

void SketchExport::onDocumentRestored()
{
    if (!BaseRefs.getValue() && Base.getValue())
        BaseRefs.setValue(Base.getValue(), Refs.getValues());
    if (Shape.getShape().getPlacement() != Placement.getValue())
        Placement.touch();
    Part::Part2DObject::onDocumentRestored();
}

App::DocumentObjectExecReturn *SketchExport::execute(void) {
    try {
        App::DocumentObjectExecReturn* rtn = Part2DObject::execute();//to positionBySupport
        if(rtn!=App::DocumentObject::StdReturn)
            //error
            return rtn;
    }
    catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    auto base = Base::freecad_dynamic_cast<SketchObject>(getBase());
    if(!base)
        return new App::DocumentObjectExecReturn("Missing parent sketch");
    if(update() && SyncPlacement.getValue() && !positionBySupport())
        Placement.setValue(base->Placement.getValue());
    return App::DocumentObject::StdReturn;
}

void SketchExport::onChanged(const App::Property* prop) {
    auto doc = getDocument();
    if(prop == &BaseRefs) {
        if(!isRestoring() && doc && !doc->isPerformingTransaction())
            update();
        Base.setValue(BaseRefs.getValue());
        Refs.setValue(BaseRefs.getSubValues(true));
    } else if(prop == &Shape) {
        // We used to bypass Part::Feature handling of shape change, which sets
        // the placement the same as the shape's. But this is causes bad thing
        // to happen. We'll fix that in onDocumentRestored().
        if (getDocument() && getDocument()->testStatus(App::Document::Restoring)) {
            DocumentObject::onChanged(prop);
            return;
        }
    }
    Part2DObject::onChanged(prop);
}

std::set<std::string> SketchExport::getRefs() const {
    std::set<std::string> refSet;
    const auto &refs = BaseRefs.getSubValues();
    refSet.insert(refs.begin(),refs.end());
    if(refSet.size()>1)
        refSet.erase("");
    return refSet;
}

bool SketchExport::update() {
    auto base = getBase();
    if(!base)
        return false;
    std::vector<Part::TopoShape> points;
    std::vector<Part::TopoShape> shapes;
    for(const auto &ref : getRefs()) {
        // Obtain the shape without feature's placement transformation, because
        // we may have our own support.
        auto shape = Part::Feature::getTopoShape(base,ref.c_str(),true,0,0,false,false);
        if(shape.isNull()) {
            FC_ERR("Invalid element reference: " << ref);
            THROWM(Base::RuntimeError, "Invalid element reference")
        }
        if(!shape.hasSubShape(TopAbs_EDGE))
            points.push_back(shape.makECopy(Part::OpCodes::SketchExport));
        else
            shapes.push_back(shape.makECopy());
    }
    Part::TopoShape res;
    if(shapes.size()) {
        res.makEWires(shapes,Part::OpCodes::SketchExport);
        shapes.clear();
        if(points.size()) {
            // Check if the vertex is already included in the wires
            for(auto &point : points) {
                auto name = point.getMappedName(
                        Data::IndexedName::fromConst("Vertex", 1));
                if(name && res.getIndexedName(name))
                    continue;
                shapes.push_back(point);
            }
            if(shapes.size()) {
                shapes.push_back(res);
                res.makECompound(shapes);
            }
        }
    }else if(points.empty())
        return false;
    else
        res.makECompound(points,0,false);

    const auto &scale = ScaleVector.getValue();
    if ((fabs(scale.x - 1.0) > Precision::Confusion()
            || fabs(scale.y - 1.0) > Precision::Confusion()
            || fabs(scale.z - 1.0) > Precision::Confusion())
            && fabs(scale.x) > Precision::Confusion()
            && fabs(scale.y) > Precision::Confusion()
            && fabs(scale.z) > Precision::Confusion())
    {
        Base::Matrix4D mat;
        mat.scale(scale);
        res.transformShape(mat, false, true);
    }

    Shape.setValue(res);
    return true;
}

void SketchExport::handleChangedPropertyType(Base::XMLReader &reader,
        const char *TypeName, App::Property *prop)
{
    if (prop == &Base && strcmp(TypeName, "App::PropertyString") == 0) {
        App::PropertyString p;
        p.Restore(reader);
        auto obj = getDocument()->getObject(p.getValue());
        if(!obj) {
            FC_ERR("Cannot find parent sketch '" << p.getValue() << "' of " << getFullName());
            return;
        }
        Base.setValue(obj);
    }
}

// clang-format on
