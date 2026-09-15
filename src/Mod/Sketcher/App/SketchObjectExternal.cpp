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

// clang-format off

void SketchObject::initExternalGeo() {
    std::vector<Part::Geometry *> geos;
    auto HLine = GeometryTypedFacade<Part::GeomLineSegment>::getTypedFacade();
    auto VLine = GeometryTypedFacade<Part::GeomLineSegment>::getTypedFacade();
    HLine->getTypedGeometry()->setPoints(Base::Vector3d(0,0,0),Base::Vector3d(1,0,0));
    VLine->getTypedGeometry()->setPoints(Base::Vector3d(0,0,0),Base::Vector3d(0,1,0));
    HLine->setConstruction(true);
    HLine->setId(-1);
    VLine->setConstruction(true);
    VLine->setId(-2);
    geos.push_back(HLine->getGeometry());
    geos.push_back(VLine->getGeometry());
    HLine->setOwner(false); // we have transferred the ownership to ExternalGeo
    VLine->setOwner(false); // we have transferred the ownership to ExternalGeo
    ExternalGeo.setValues(std::move(geos));
}

int SketchObject::toggleFreeze(const std::vector<int> &geoIds)
{
    return toggleExternalGeometryFlag(geoIds, {ExternalGeometryExtension::Frozen});
}

int SketchObject::toggleIntersection(const std::vector<int> &geoIds, bool defining)
{
    std::vector<ExternalGeometryExtension::Flag> flags;
    flags.push_back(ExternalGeometryExtension::Intersection);
    if (defining)
        flags.push_back(ExternalGeometryExtension::Defining);
    return toggleExternalGeometryFlag(geoIds, flags);
}

int SketchObject::toggleExternalGeometryFlag(const std::vector<int> &geoIds,
                                             const std::vector<ExternalGeometryExtension::Flag> &flags)
{
    if (flags.empty())
        return 0;
    auto flag = flags.front();

    Base::StateLocker lock(managedoperation, true); // no need to check input data validity as this is an sketchobject managed operation.

    bool update = false;
    bool touched = false;
    auto geos = ExternalGeo.getValues();
    std::set<int> idSet(geoIds.begin(),geoIds.end());
    for(auto geoId : geoIds) {
        if(geoId > GeoEnum::RefExt || -geoId-1>=ExternalGeo.getSize())
            continue;
        if(!idSet.count(geoId))
            continue;
        idSet.erase(geoId);
        int idx = -geoId-1;
        auto &geo = geos[idx];
        auto egf = ExternalGeometryFacade::getFacade(geo);
        bool value = !egf->testFlag(flag);
        if(egf->getRef().size()) {
            for(auto gid : getRelatedGeometry(geoId)) {
                if(gid == geoId)
                    continue;
                int idx = -gid-1;
                auto &g = geos[idx];
                g = g->clone();
                auto egf = ExternalGeometryFacade::getFacade(g);
                egf->setFlag(flag, value);
                for (size_t i=1; i<flags.size(); ++i)
                    egf->setFlag(flags[i], value);
                idSet.erase(gid);
            }
        }
        geo = geo->clone();
        egf->setGeometry(geo);
        egf->setFlag(flag, value);
        for (size_t i=1; i<flags.size(); ++i)
            egf->setFlag(flags[i], value);
        if (value || flag != ExternalGeometryExtension::Frozen)
            update = true;
        touched = true;
    }

    if(!touched)
        return -1;
    ExternalGeo.setValues(geos);
    if (update)
        rebuildExternalGeometry();
    return 0;
}

int SketchObject::detachExternal(const std::vector<int> &geoIds) {
    Base::StateLocker lock(managedoperation, true); // no need to check input data validity as this is an sketchobject managed operation.

    bool touched = false;
    auto geos = ExternalGeo.getValues();
    for(int geoId : geoIds) {
        if(geoId > GeoEnum::RefExt || -geoId-1>=ExternalGeo.getSize())
            continue;
        auto &geo = geos[-geoId-1];
        geo = geo->clone();
        auto egf = ExternalGeometryFacade::getFacade(geo);
        egf->setFlag(ExternalGeometryExtension::Detached);
        touched = true;
    }
    if(!touched)
        return -1;
    ExternalGeo.setValues(std::move(geos));
    return 0;
}

bool SketchObject::isExternalAllowed(App::Document* pDoc, App::DocumentObject* pObj,
                                     eReasonList* rsn) const
{
    if (rsn)
        *rsn = rlAllowed;

    // Externals outside of the Document are NOT allowed
    if (this->getDocument() != pDoc) {
        if (rsn)
            *rsn = rlOtherDoc;
        return false;
    }

    // circular reference prevention
    try {
        if (!(this->testIfLinkDAGCompatible(pObj))) {
            if (rsn)
                *rsn = rlCircularReference;
            return false;
        }
    }
    catch (Base::Exception& e) {
        Base::Console().Warning(
            "Probably, there is a circular reference in the document. Error: %s\n", e.what());
        return true;// prohibiting this reference won't remove the problem anyway...
    }


    // Note: Checking for the body of the support doesn't work when the support are the three base
    // planes
    // App::DocumentObject *support = this->Support.getValue();
    Part::BodyBase* body_this = Part::BodyBase::findBodyOf(this);
    Part::BodyBase* body_obj = Part::BodyBase::findBodyOf(pObj);
    App::Part* part_this = App::Part::getPartOfObject(this);
    App::Part* part_obj = App::Part::getPartOfObject(pObj);
    if (part_this == part_obj) {// either in the same part, or in the root of document
        if (!body_this) {
            return true;
        }
        else if (body_this == body_obj) {
            return true;
        }
        else {
            if (rsn)
                *rsn = rlOtherBody;
            return false;
        }
    }
    else {
        // cross-part link. Disallow, should be done via shapebinders only
        if (rsn)
            *rsn = rlOtherPart;
        return false;
    }
}

static inline bool
checkOutList(std::set<App::DocumentObject*> outSet, const App::DocumentObject *owner, App::DocumentObject *obj)
{
    if (obj == owner)
        return false;
    if (!outSet.insert(obj).second)
        return true;
    for (auto o : obj->getOutList())
        if (!checkOutList(outSet, owner, o))
            return false;
    return true;
}

bool SketchObject::isCarbonCopyAllowed(App::Document* pDoc, App::DocumentObject* pObj, bool& xinv,
                                       bool& yinv, eReasonList* rsn) const
{
    if (pObj == this) {
        if (rsn)
            *rsn = rlCircularReference;
        return false;
    }

    if (rsn)
        *rsn = rlAllowed;

    // Only applicable to sketches
    if (pObj->getTypeId() != Sketcher::SketchObject::getClassTypeId()) {
        if (rsn)
            *rsn = rlNotASketch;
        return false;
    }

    SketchObject* psObj = static_cast<SketchObject*>(pObj);

    // Sketches outside of the Document are NOT allowed
    if (this->getDocument() != pDoc) {
        if (rsn)
            *rsn = rlOtherDoc;
        return false;
    }

    // circular reference prevention
    try {
        // testIfLinkDAGCompatible disallow pObj link to this sketch. However,
        // carbon copy can now handle external geometry to itself (but not
        // from any other property that links to itself).
        //
        // if (!(this->testIfLinkDAGCompatible(pObj)))
        std::vector<App::Property*> props;
        pObj->getPropertyList(props);
        std::set<App::DocumentObject*> outSet;
        for (auto prop : props) {
            if (auto propLink = Base::freecad_dynamic_cast<App::PropertyLinkBase>(prop)) {
                for (auto obj : propLink->linkedObjects()) {
                    if (obj == this && propLink == &psObj->ExternalGeometry)
                        continue;
                    if (!checkOutList(outSet, this, obj)) {
                        if (rsn)
                            *rsn = rlCircularReference;
                        return false;
                    }
                }
            }
        }
    }
    catch (Base::Exception& e) {
        Base::Console().Warning(
            "Probably, there is a circular reference in the document. Error: %s\n", e.what());
        return true;// prohibiting this reference won't remove the problem anyway...
    }


    // Note: Checking for the body of the support doesn't work when the support are the three base
    // planes
    // App::DocumentObject *support = this->Support.getValue();
    Part::BodyBase* body_this = Part::BodyBase::findBodyOf(this);
    Part::BodyBase* body_obj = Part::BodyBase::findBodyOf(pObj);
    App::Part* part_this = App::Part::getPartOfObject(this);
    App::Part* part_obj = App::Part::getPartOfObject(pObj);
    if (part_this == part_obj) {// either in the same part, or in the root of document
        if (body_this) {
            if (body_this != body_obj) {
                if (!this->allowOtherBody) {
                    if (rsn)
                        *rsn = rlOtherBody;
                    return false;
                }
                // if the original sketch has external geometry AND it is not in this body prevent
                // link
                else if (psObj->getExternalGeometryCount() > 2) {
                    if (rsn)
                        *rsn = rlOtherBodyWithLinks;
                    return false;
                }
            }
        }
    }
    else {
        // cross-part relation. Disallow, should be done via shapebinders only
        if (rsn)
            *rsn = rlOtherPart;
        return false;
    }


    const Rotation& srot = psObj->Placement.getValue().getRotation();
    const Rotation& lrot = this->Placement.getValue().getRotation();

    Base::Vector3d snormal(0, 0, 1);
    Base::Vector3d sx(1, 0, 0);
    Base::Vector3d sy(0, 1, 0);
    srot.multVec(snormal, snormal);
    srot.multVec(sx, sx);
    srot.multVec(sy, sy);

    Base::Vector3d lnormal(0, 0, 1);
    Base::Vector3d lx(1, 0, 0);
    Base::Vector3d ly(0, 1, 0);
    lrot.multVec(lnormal, lnormal);
    lrot.multVec(lx, lx);
    lrot.multVec(ly, ly);

    double dot = snormal * lnormal;
    double dotx = sx * lx;
    double doty = sy * ly;

    // the planes of the sketches must be parallel
    if (!allowUnaligned && fabs(fabs(dot) - 1) > Precision::Confusion()) {
        if (rsn)
            *rsn = rlNonParallel;
        return false;
    }

    // the axis must be aligned
    if (!allowUnaligned
        && ((fabs(fabs(dotx) - 1) > Precision::Confusion())
            || (fabs(fabs(doty) - 1) > Precision::Confusion()))) {
        if (rsn)
            *rsn = rlAxesMisaligned;
        return false;
    }


    // the origins of the sketches must be aligned or be the same
    Base::Vector3d ddir =
        (psObj->Placement.getValue().getPosition() - this->Placement.getValue().getPosition())
            .Normalize();

    double alignment = ddir * lnormal;

    if (!allowUnaligned && (fabs(fabs(alignment) - 1) > Precision::Confusion())
        && (psObj->Placement.getValue().getPosition()
            != this->Placement.getValue().getPosition())) {
        if (rsn)
            *rsn = rlOriginsMisaligned;
        return false;
    }

    xinv = allowUnaligned ? false : (fabs(dotx - 1) > Precision::Confusion());
    yinv = allowUnaligned ? false : (fabs(doty - 1) > Precision::Confusion());

    return true;
}

int SketchObject::carbonCopy(App::DocumentObject* pObj, bool construction)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // so far only externals to the support of the sketch and datum features
    bool xinv = false, yinv = false;

    if (!isCarbonCopyAllowed(pObj->getDocument(), pObj, xinv, yinv))
        return -1;

    SketchObject* psObj = static_cast<SketchObject*>(pObj);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    const std::vector<Sketcher::Constraint*>& cvals = Constraints.getValues();

    std::vector<Part::Geometry*> newVals(vals);

    std::vector<Constraint*> newcVals(cvals);

    int nextgeoid = vals.size();

    int nextcid = cvals.size();

    const std::vector<Part::Geometry*>& svals = psObj->getInternalGeometry();

    const std::vector<Sketcher::Constraint*>& scvals = psObj->Constraints.getValues();

    newVals.reserve(vals.size() + svals.size());
    newcVals.reserve(cvals.size() + scvals.size());

    std::map<int, int> extMap;
    if (psObj->ExternalGeo.getSize() > 1) {
        int i = -1;
        auto geos = this->ExternalGeo.getValues();
        std::string myName(this->getNameInDocument());
        myName += ".";
        for (const auto &geo : psObj->ExternalGeo.getValues()) {
            if (++i < 2) // skip h/v axes
                continue;
            else {
                auto egf = ExternalGeometryFacade::getFacade(geo);
                const auto &ref = egf->getRef();
                if (boost::starts_with(ref, myName)) {
                    int geoId;
                    PointPos posId;
                    if (this->geoIdFromShapeType(ref.c_str()+myName.size(), geoId, posId)) {
                        extMap[-i-1] = geoId;
                        continue;
                    }
                }
            }
            auto copy = geo->copy();
            auto egf = ExternalGeometryFacade::getFacade(copy);
            egf->setId(++geoLastId);
            if (!egf->getRef().empty()) {
                auto &refs = this->externalGeoRefMap[egf->getRef()];
                refs.push_back(geoLastId);
            }
            this->externalGeoMap[geoLastId] = (int)geos.size();
            geos.push_back(copy);
            extMap[-i-1] = -(int)geos.size();
        }
        Base::ObjectStatusLocker<App::Property::Status,App::Property>
            guard(App::Property::User3, &this->ExternalGeo);
        this->ExternalGeo.setValues(std::move(geos));
    }

    if(psObj->ExternalGeometry.getSize()>0) {
        std::vector<DocumentObject*> Objects     = ExternalGeometry.getValues();
        std::vector<std::string>     SubElements = ExternalGeometry.getSubValues();

        std::vector<DocumentObject*> sObjects     = psObj->ExternalGeometry.getValues();
        std::vector<std::string>     sSubElements = psObj->ExternalGeometry.getSubValues();

        if (Objects.size() != SubElements.size() || sObjects.size() != sSubElements.size()) {
            assert(0 /*counts of objects and subelements in external geometry links do not match*/);
            Base::Console().Error("Internal error: counts of objects and subelements in external "
                                  "geometry links do not match\n");
            return -1;
        }

        int si=-1;
        for (const auto &sobj : sObjects) {
            ++si;
            if (sobj == this)
                continue;
            int i=0;
            for (auto & obj : Objects){
                if (obj == sobj && SubElements[i] == sSubElements[si]){
                    FC_WARN("Link to %s already exists in this sketch");
                    i = -1;
                    break;
                }
                i++;
            }

            if (i >= 0) {
                Objects.push_back(sobj);
                SubElements.push_back(sSubElements[si]);
            }
        }

        ExternalGeometry.setValues(Objects,SubElements);
        rebuildExternalGeometry();
        solverNeedsUpdate=true;
    }

    for (std::vector<Part::Geometry *>::const_iterator it=svals.begin(); it != svals.end(); ++it){
        Part::Geometry *geoNew = (*it)->copy();
        generateId(geoNew);
        if(construction && geoNew->getTypeId() != Part::GeomPoint::getClassTypeId()) {
            GeometryFacade::setConstruction(geoNew, true);
        }
        newVals.push_back(geoNew);
    }

    auto adjustConstraint = [&](int idx, int &geoId) {
        if (geoId >= 0)
            geoId += nextgeoid;
        else if (geoId < -2 && geoId != GeoEnum::GeoUndef) {
            auto it = extMap.find(geoId);
            if (it != extMap.end())
                geoId = it->second;
            else {
                std::string name;
                try {
                    name = psObj->Constraints.createPath(idx).toString();
                } catch (Base::Exception &e) {
                }
                FC_WARN("Failed to copy constraint " << name);
                return false;
            }
        }
        return true;
    };

    for (int i=0; i<(int)scvals.size(); ++i) {
        Sketcher::Constraint *newConstr = scvals[i]->copy();
        if (adjustConstraint(i, newConstr->First)
                && adjustConstraint(i, newConstr->Second)
                && adjustConstraint(i, newConstr->Third))
            newcVals.push_back(newConstr);
        else
            delete newConstr;
    }

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);
        Geometry.setValues(std::move(newVals));
        this->Constraints.setValues(std::move(newcVals));
    }
    // we trigger now the update (before dealing with expressions)
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    int sourceid = 0;
    for (std::vector<Sketcher::Constraint*>::const_iterator it = scvals.begin(); it != scvals.end();
         ++it, nextcid++, sourceid++) {

        if ((*it)->isDimensional()) {
            // then we link its value to the parent
            if ((*it)->isDriving) {
                App::ObjectIdentifier spath = psObj->Constraints.createPath(sourceid);
                App::PropertyExpressionEngine::ExpressionInfo expr_info = psObj->getExpression(spath);
                if (expr_info.expression && !expr_info.expression->getDepObjects().count(psObj))
                    setExpression(Constraints.createPath(nextcid), expr_info.expression->copy());
                else {
                    auto expr = App::Expression::parse(this, spath.getDocumentObjectName().getString() + "." + spath.toString());
                    setExpression(Constraints.createPath(nextcid), std::move(expr));
                }
            }
        }
    }

    // We shall solve in all cases, because recompute may fail, and leave the
    // sketch in an inconsistent state. A concrete example. If the copied sketch
    // has broken external geometry, its recomputation will fail. And because we
    // use expression for copied constraint to add dependency to the copied
    // sketch, this sketch will not be recomputed (because its dependency fails
    // to recompute).
#if 0
    if (noRecomputes) // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
#endif
        solve();


    return svals.size();
}

int SketchObject::addExternal(App::DocumentObject *Obj, const char* SubName, bool defining, bool intersection)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // so far only externals to the support of the sketch and datum features
    if (!isExternalAllowed(Obj->getDocument(), Obj))
        return -1;

    Part::TopoShape wholeShape = Part::Feature::getTopoShape(Obj);
    Part::TopoShape shape;
    TopAbs_ShapeEnum shapeType = TopAbs_SHAPE;
    if (!SubName && !SubName[0]) {
        shape = wholeShape.getSubTopoShape(SubName, /*silent*/true);
        if (shape.shapeType(/*silent*/true) != TopAbs_FACE
                && shape.shapeType(/*silent*/true) != TopAbs_WIRE) {
            if (shape.hasSubShape(TopAbs_FACE)) {
                shapeType = TopAbs_FACE;
            }
            else if (shape.hasSubShape(TopAbs_WIRE)) {
                shapeType = TopAbs_WIRE;
            }
            else if (shape.shapeType(/*silent*/true) != TopAbs_EDGE
                    && shape.hasSubShape(TopAbs_EDGE)) {
                shapeType = TopAbs_EDGE;
            }
        }
    }

    if (shapeType != TopAbs_SHAPE) {
        std::string element = Part::TopoShape::shapeName(shapeType);
        std::size_t elementNameSize = element.size();
        int geometryCount = ExternalGeometry.getSize();

        gp_Pln sketchPlane;
        if (intersection) {
            Base::Placement Plm = Placement.getValue();
            Base::Vector3d Pos = Plm.getPosition();
            Base::Rotation Rot = Plm.getRotation();
            Base::Vector3d dN(0,0,1);
            Rot.multVec(dN,dN);
            Base::Vector3d dX(1,0,0);
            Rot.multVec(dX,dX);
            gp_Ax3 sketchAx3(gp_Pnt(Pos.x,Pos.y,Pos.z),
                            gp_Dir(dN.x,dN.y,dN.z),
                            gp_Dir(dX.x,dX.y,dX.z));
            sketchPlane.SetPosition(sketchAx3);
        }
        for (const auto &subShape : shape.getSubShapes(shapeType)) {
            int idx = wholeShape.findShape(subShape);
            if (idx == 0)
                continue;
            if (intersection) {
                try {
                    BRepAlgoAPI_Section maker(subShape, sketchPlane);
                    if (!maker.IsDone() || maker.Shape().IsNull())
                        continue;
                } catch (Standard_Failure &) {
                    continue;
                }
            }
            element += std::to_string(idx);
            addExternal(Obj, element.c_str(), defining, intersection);
            element.resize(elementNameSize);
        }
        if (ExternalGeometry.getSize() == geometryCount)
            return -1;
        return geometryCount;
    }

    // get the actual lists of the externals
    std::vector<DocumentObject*> Objects     = ExternalGeometry.getValues();
    std::vector<std::string>     SubElements = ExternalGeometry.getSubValues();

    if (Objects.size() != SubElements.size()) {
        assert(0 /*counts of objects and subelements in external geometry links do not match*/);
        Base::Console().Error("Internal error: counts of objects and subelements in external "
                              "geometry links do not match\n");
        return -1;
    }
    for (size_t i = 0  ;  i < Objects.size()  ;  ++i){
        if (Objects[i] == Obj   &&   SubElements[i] == SubName) {
            Base::Console().Error("Link to %s already exists in this sketch.\n",SubName);
            return -1;
        }
    }

    // add the new ones
    Objects.push_back(Obj);
    SubElements.emplace_back(SubName);

    // set the Link list.
    ExternalGeometry.setValues(Objects,SubElements);
    rebuildExternalGeometry(defining, intersection);
    if(ExternalGeometry.getSize() == (int)Objects.size())
        return ExternalGeometry.getSize()-1;
    return -1;
}

int SketchObject::delExternal(int ExtGeoId)
{
    return delExternal(std::vector<int>{ExtGeoId});
}

int SketchObject::delExternal(const std::vector<int> &ExtGeoIds)
{
    std::set<long> geoIds;
    for (int ExtGeoId : ExtGeoIds) {
        int GeoId = GeoEnum::RefExt - ExtGeoId;
        if(GeoId > GeoEnum::RefExt || -GeoId-1 >= ExternalGeo.getSize())
            return -1;

        auto geo = getGeometry(GeoId);
        if(!geo)
            return -1;

        auto egf = ExternalGeometryFacade::getFacade(geo);
        geoIds.insert(egf->getId());
        if(egf->getRef().size()) {
            auto &refs = externalGeoRefMap[egf->getRef()];
            geoIds.insert(refs.begin(),refs.end());
        }
    }

    delExternalPrivate(geoIds, true);
    return 0;
}

void SketchObject::delExternalPrivate(const std::set<long> &ids, bool removeRef) {

    Base::StateLocker lock(managedoperation, true); // no need to check input data validity as this is an sketchobject managed operation.

    std::set<std::string> refs;
    // Must sort in reverse order so as to delete geo from back to front to
    // avoid index change
    std::set<int, std::greater<int>> geoIds;

    for(auto id : ids) {
        auto it = externalGeoMap.find(id);
        if(it == externalGeoMap.end())
            continue;

        auto egf = ExternalGeometryFacade::getFacade(ExternalGeo[it->second]);
        if(removeRef && egf->getRef().size())
            refs.insert(egf->getRef());
        geoIds.insert(-it->second-1);
    }

    if(geoIds.empty())
        return;

    std::vector< Constraint * > newConstraints;
    for(auto cstr : Constraints.getValues()) {
        if(!geoIds.count(cstr->First) &&
           (cstr->Second==GeoEnum::GeoUndef || !geoIds.count(cstr->Second)) &&
           (cstr->Third==GeoEnum::GeoUndef || !geoIds.count(cstr->Third)))
        {
            bool cloned = false;
            int offset = 0;
            for(auto GeoId : geoIds) {
                GeoId += offset++;
                bool done = true;
                if (cstr->First < GeoId && cstr->First != GeoEnum::GeoUndef) {
                    if (!cloned) {
                        cloned = true;
                        cstr = cstr->clone();
                    }
                    cstr->First += 1;
                    done = false;
                }
                if (cstr->Second < GeoId && cstr->Second != GeoEnum::GeoUndef) {
                    if (!cloned) {
                        cloned = true;
                        cstr = cstr->clone();
                    }
                    cstr->Second += 1;
                    done = false;
                }
                if (cstr->Third < GeoId && cstr->Third != GeoEnum::GeoUndef) {
                    if (!cloned) {
                        cloned = true;
                        cstr = cstr->clone();
                    }
                    cstr->Third += 1;
                    done = false;
                }
                if(done) break;
            }
            newConstraints.push_back(cstr);
        }
    }

    auto geos = ExternalGeo.getValues();
    int offset = 0;
    for(auto geoId : geoIds) {
        int idx = -geoId-1;
        geos.erase(geos.begin()+idx-offset);
        ++offset;
    }

    if(refs.size()) {
        std::vector<std::string> newSubs;
        std::vector<App::DocumentObject*> newObjs;
        const auto &subs = ExternalGeometry.getSubValues();
        auto itSub = subs.begin();
        const auto &objs = ExternalGeometry.getValues();
        auto itObj = objs.begin();
        bool touched = false;
        assert(externalGeoRef.size() == objs.size());
        assert(externalGeoRef.size() == subs.size());
        for(auto it=externalGeoRef.begin();it!=externalGeoRef.end();++it,++itObj,++itSub) {
            if(refs.count(*it)) {
                if(!touched) {
                    touched = true;
                    if(newObjs.empty()) {
                        newObjs.insert(newObjs.end(),objs.begin(),itObj);
                        newSubs.insert(newSubs.end(),subs.begin(),itSub);
                    }
                }
            }else if(touched) {
                newObjs.push_back(*itObj);
                newSubs.push_back(*itSub);
            }
        }
        if(touched)
            ExternalGeometry.setValues(newObjs,newSubs);
    }

    ExternalGeo.setValues(std::move(geos));

    solverNeedsUpdate = true;
    Constraints.setValues(std::move(newConstraints));
    acceptGeometry(); // This may need to be refactored into OnChanged for ExternalGeometry.
}

int SketchObject::delAllExternal()
{
    int count = 0; // the remaining count of the detached external geometry
    std::map<int,int> indexMap; // the index map of the remain external geometry
    std::vector<Part::Geometry*> geos; // the remaining external geometry
    for(int i=0;i<ExternalGeo.getSize();++i) {
        auto geo = ExternalGeo[i];
        auto egf = ExternalGeometryFacade::getFacade(geo);
        if(egf->getRef().empty())
            indexMap[i] = count++;
        geos.push_back(geo);
    }

    Base::StateLocker lock(managedoperation, true); // no need to check input data validity as this is an sketchobject managed operation.

    const std::vector< Constraint * > &constraints = Constraints.getValues();
    std::vector< Constraint * > newConstraints(0);
    for(auto cstr : constraints) {
        std::unique_ptr<Constraint> clone;
        if(cstr->First <= GeoEnum::RefExt) {
           auto it = indexMap.find(cstr->First);
           if(it==indexMap.end())
               continue;
            if(!clone)
                clone.reset(cstr->clone());
            clone->First = it->second;
        }
        if(cstr->Second <= GeoEnum::RefExt && cstr->Second != GeoEnum::GeoUndef) {
           auto it = indexMap.find(cstr->Second);
           if(it==indexMap.end())
               continue;
            if(!clone)
                clone.reset(cstr->clone());
            clone->Second = it->second;
        }
        if(cstr->Third <= GeoEnum::RefExt && cstr->Third != GeoEnum::GeoUndef) {
           auto it = indexMap.find(cstr->Third);
           if(it==indexMap.end())
               continue;
            if(!clone)
                clone.reset(cstr->clone());
            clone->Third = it->second;
        }
        if(!clone)
            clone.reset(cstr->clone());

        newConstraints.push_back(clone.release());
    }

    ExternalGeometry.setValue(0);
    ExternalGeo.setValues(std::move(geos));
    solverNeedsUpdate=true;
    Constraints.setValues(std::move(newConstraints));
    acceptGeometry();// This may need to be refactored into OnChanged for ExternalGeometry
    return 0;
}

int SketchObject::delConstraintsToExternal()
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& constraints = Constraints.getValuesForce();
    std::vector<Constraint*> newConstraints(0);
    int GeoId = GeoEnum::RefExt, NullId = GeoEnum::GeoUndef;
    for(auto cstr : constraints) {
        if(cstr->First <= GeoId) {
            auto geo = getGeometry(cstr->First);
            if(geo && ExternalGeometryFacade::getFacade(geo)->getRef().size())
                continue;
        } else if(cstr->Second <= GeoId && cstr->Second != NullId) {
            auto geo = getGeometry(cstr->Second);
            if(geo && ExternalGeometryFacade::getFacade(geo)->getRef().size())
                continue;
        } else if(cstr->Third <= GeoId && cstr->Third != NullId) {
            auto geo = getGeometry(cstr->Third);
            if(geo && ExternalGeometryFacade::getFacade(geo)->getRef().size())
                continue;
        }
        newConstraints.push_back(cstr);
    }

    Constraints.setValues(std::move(newConstraints));
    Constraints.acceptGeometry(getCompleteGeometry());

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::attachExternal(
        const std::vector<int> &geoIds, App::DocumentObject *Obj, const char* SubName)
{
    if (!isExternalAllowed(Obj->getDocument(), Obj))
       return -1;

    std::set<std::string> detached;
    std::set<int> idSet;
    for(int geoId : geoIds) {
        if(geoId > GeoEnum::RefExt || -geoId-1 >= ExternalGeo.getSize())
            continue;
        auto geo = getGeometry(geoId);
        if(!geo)
            continue;
        auto egf = ExternalGeometryFacade::getFacade(geo);
        if(egf->getRef().size())
            detached.insert(egf->getRef());
        for(int id : getRelatedGeometry(geoId))
            idSet.insert(id);
    }

    auto geos = ExternalGeo.getValues();

    std::vector<DocumentObject*> Objects     = ExternalGeometry.getValues();
    auto itObj = Objects.begin();
    std::vector<std::string>     SubElements = ExternalGeometry.getSubValues();
    auto itSub = SubElements.begin();

    assert(Objects.size()==SubElements.size());
    assert(externalGeoRef.size() == Objects.size());

    for(auto &key : externalGeoRef) {
        if (*itObj == Obj  &&  *itSub == SubName){
            FC_ERR("Duplicdate external element reference in " << getFullName() << ": " << key);
            return -1;
        }
        // detach old reference
        if(detached.count(key)) {
            itObj = Objects.erase(itObj);
            itSub = SubElements.erase(itSub);
        }else{
            ++itObj;
            ++itSub;
        }
    }

    // add the new ones
    Objects.push_back(Obj);
    SubElements.push_back(std::string(SubName));

    ExternalGeometry.setValues(Objects,SubElements);
    if(externalGeoRef.size()!=Objects.size())
        return -1;

    std::string ref = externalGeoRef.back();
    for(auto geoId : idSet) {
        auto &geo = geos[-geoId-1];
        geo = geo->clone();
        ExternalGeometryFacade::getFacade(geo)->setRef(ref);
    }

    ExternalGeo.setValues(std::move(geos));
    rebuildExternalGeometry();
    return ExternalGeometry.getSize()-1;
}

std::vector<int> SketchObject::getRelatedGeometry(int GeoId) const {
    std::vector<int> res;
    if(GeoId>GeoEnum::RefExt || -GeoId-1>=ExternalGeo.getSize())
        return res;
    auto geo = getGeometry(GeoId);
    if(!geo)
        return res;
    const std::string &ref = ExternalGeometryFacade::getFacade(geo)->getRef();
    if(!ref.size())
       return {GeoId};
    auto iter = externalGeoRefMap.find(ref);
    if(iter == externalGeoRefMap.end())
        return {GeoId};
    for(auto id : iter->second) {
        auto it = externalGeoMap.find(id);
        if(it!=externalGeoMap.end())
            res.push_back(-it->second-1);
    }
    return res;
}

int SketchObject::syncGeometry(const std::vector<int> &geoIds) {
    bool touched = false;
    auto geos = ExternalGeo.getValues();
    std::set<int> idSet;
    for(int geoId : geoIds) {
        auto geo = getGeometry(geoId);
        if(!geo || !ExternalGeometryFacade::getFacade(geo)->testFlag(ExternalGeometryExtension::Frozen))
            continue;
        for(int gid : getRelatedGeometry(geoId))
            idSet.insert(gid);
    }
    for(int geoId : idSet) {
        if(geoId <= GeoEnum::RefExt && -geoId-1 < ExternalGeo.getSize()) {
            auto &geo = geos[-geoId-1];
            geo = geo->clone();
            ExternalGeometryFacade::getFacade(geo)->setFlag(ExternalGeometryExtension::Sync);
            touched = true;
        }
    }
    if(touched)
        ExternalGeo.setValues(std::move(geos));
    return 0;
}

namespace {

// Auxiliary Method: returns vector projection in UV space of plane
gp_Vec2d ProjVecOnPlane_UV(const gp_Vec& V, const gp_Pln& Pl)
{
    return gp_Vec2d(V.Dot(Pl.Position().XDirection()), V.Dot(Pl.Position().YDirection()));
}

// Auxiliary Method: returns vector projection in UVN space of plane
gp_Vec ProjVecOnPlane_UVN(const gp_Vec& V, const gp_Pln& Pl)
{
    gp_Vec2d vector = ProjVecOnPlane_UV(V, Pl);
    return gp_Vec(vector.X(), vector.Y(), 0.0);
}

// Auxiliary Method: returns vector projection in XYZ space
#if 0
gp_Vec ProjVecOnPlane_XYZ( const gp_Vec& V, const gp_Pln& Pl)
{
  return V.Dot(Pl.Position().XDirection()) * Pl.Position().XDirection() +
         V.Dot(Pl.Position().YDirection()) * Pl.Position().YDirection();
}
#endif

// Auxiliary Method: returns point projection in UV space of plane
gp_Vec2d ProjPointOnPlane_UV(const gp_Pnt& P, const gp_Pln& Pl)
{
    gp_Vec OP = gp_Vec(Pl.Location(), P);
    return ProjVecOnPlane_UV(OP, Pl);
}

// Auxiliary Method: returns point projection in UVN space of plane
gp_Vec ProjPointOnPlane_UVN(const gp_Pnt& P, const gp_Pln& Pl)
{
    gp_Vec2d vec2 = ProjPointOnPlane_UV(P, Pl);
    return gp_Vec(vec2.X(), vec2.Y(), 0.0);
}

#if 0
// Auxiliary Method: returns point projection in XYZ space
gp_Pnt ProjPointOnPlane_XYZ(const gp_Pnt& P, const gp_Pln& Pl)
{
    gp_Vec positionUVN = ProjPointOnPlane_UVN(P, Pl);
    return gp_Pnt((positionUVN.X() * Pl.Position().XDirection()
                   + positionUVN.Y() * Pl.Position().YDirection() + gp_Vec(Pl.Location().XYZ()))
                      .XYZ());
}
#endif

// Auxiliary method
Part::Geometry* projectLine(const BRepAdaptor_Curve& curve, const Handle(Geom_Plane) & gPlane,
                            const Base::Placement& invPlm)
{
    double first = curve.FirstParameter();

    if (fabs(first) > 1E99) {
        // TODO: What is OCE's definition of Infinite?
        // TODO: The clean way to do this is to handle a new sketch geometry Geom::Line
        // but its a lot of work to implement...
        first = -10000;
    }

    double last = curve.LastParameter();
    if (fabs(last) > 1E99) {
        last = +10000;
    }

    gp_Pnt P1 = curve.Value(first);
    gp_Pnt P2 = curve.Value(last);

    GeomAPI_ProjectPointOnSurf proj1(P1, gPlane);
    P1 = proj1.NearestPoint();
    GeomAPI_ProjectPointOnSurf proj2(P2, gPlane);
    P2 = proj2.NearestPoint();

    Base::Vector3d p1(P1.X(), P1.Y(), P1.Z());
    Base::Vector3d p2(P2.X(), P2.Y(), P2.Z());
    invPlm.multVec(p1, p1);
    invPlm.multVec(p2, p2);

    if (Base::Distance(p1, p2) < Precision::Confusion()) {
        Base::Vector3d p = (p1 + p2) / 2;
        Part::GeomPoint* point = new Part::GeomPoint(p);
        GeometryFacade::setConstruction(point, true);
        return point;
    }
    else {
        Part::GeomLineSegment* line = new Part::GeomLineSegment();
        line->setPoints(p1, p2);
        GeometryFacade::setConstruction(line, true);
        return line;
    }
}

// Project an edge to a line. Only works if the edge is planar and its plane is
// perpendicular to the projection plane. This function is to work around OCC
// normal projection bug which seems to only repsect the start and ending points
// of an arc but disregarding any extreme points. OCC also has trouble handling
// BSpline projection to a straight line. Although it does correctly projects
// the line including extreme bounds, it will produce a BSpline with degree
// more than one.
//
// The work around here is to use an aligned bounding box of the edge to get
// the projection of the extremem points to construct the projected line.
Part::Geometry* projectEdgeToLine(const TopoDS_Edge &edge,
                                  const Base::Placement& invPlm)
{
    auto shape = Part::TopoShape(edge);
    // First, transform the shape to the projection plane local coordinates.
    shape.setShapePlacement(invPlm * shape.getShapePlacement());

    gp_Pln plane;
    // Check if the edge is planar
    if (!shape.findPlane(plane))
        return nullptr;

    // Check if the edge plane is perpendicular to the projection plane
    if (!plane.Axis().IsNormal(gp_Ax1(), Precision::Angular()))
        return nullptr;

    // Align the z axis of the edge plane to the y axis of the projection
    // plane,  so that the extreme bound will be a line in the x axis direction
    // of the projection plane.
    double angle = plane.Axis().Direction().Angle(gp_Dir(0, 1, 0));
    gp_Trsf trsf;
    if (fabs(angle) > Precision::Angular()) {
        trsf.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), angle);
        shape.move(trsf);
    }

    // Make a copy to work around OCC circular edge transformation bug
    shape = shape.makECopy();

    // Explicitly make the mesh, or else getBoundBox() will be very loosely
    // bound.
    //
    // Use very small deflection to make more accurate measurement. Could be slow!
    BRepMesh_IncrementalMesh aMesh(shape.getShape(), 0.005, false, 0.1, true);

    // Obtain the bounding box and move the extreme points back to its original
    // location
    auto bbox = shape.getBoundBox();
    if (!bbox.IsValid())
        return nullptr;

    gp_Pnt p1(bbox.MinX, bbox.MinY, 0);
    gp_Pnt p2(bbox.MaxX, bbox.MaxY, 0);
    if (fabs(angle) > Precision::Angular()) {
        trsf.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), -angle);
        p1.Transform(trsf);
        p2.Transform(trsf);
    }

    Base::Vector3d P1(p1.X(), p1.Y(), 0);
    Base::Vector3d P2(p2.X(), p2.Y(), 0);

    // check for degenerated case when the line is collapsed to a point
    if (p1.SquareDistance(p2) < Precision::SquareConfusion()) {
        Part::GeomPoint* point = new Part::GeomPoint((P1 + P2) / 2);
        GeometryFacade::setConstruction(point, true);
        return point;
    }
    else {
        Part::GeomLineSegment* line = new Part::GeomLineSegment();
        line->setPoints(P1, P2);
        GeometryFacade::setConstruction(line, true);
        return line;
    }
}

void getParameterRange(Handle(Geom_Curve) curve,
                       const gp_Pnt &firstPoint,
                       const gp_Pnt &lastPoint,
                       double &firstParameter,
                       double &lastParameter)
{
    // The reason of this function is because the first/last parameter reported
    // from some curve does not really corresponds to the first/last vertex of
    // the edge. I can only guess this is because the curve (in some cases) is
    // actually computed on demaond from surface (in BRepAdaptor_Curve maybe).
    // And in the process, there is something off in tolerance causing the
    // derived parameter not matching the value corresponding to the position of
    // the actual vertex.
    GeomAPI_ProjectPointOnCurve pfirst(firstPoint, curve);
    GeomAPI_ProjectPointOnCurve plast(lastPoint, curve);
    firstParameter = pfirst.LowerDistanceParameter();
    lastParameter = plast.LowerDistanceParameter();
    if (firstParameter > lastParameter)
        std::swap(firstParameter, lastParameter);
}

void adjustParameterRange(const TopoDS_Edge &edge,
                                 Handle(Geom_Plane) gPlane,
                                 const gp_Trsf &mov,
                                 Handle(Geom_Curve) curve,
                                 double &firstParameter,
                                 double &lastParameter)
{
    // This function is to deal with the ambiguity of trimming a periodic
    // curve, e.g. given two points on a circle, whether to get the upper or
    // lower arc. Because projection orientation may swap the first and last
    // parameter of the original curve.
    //
    // We project the middel point of the original curve to the projected curve
    // to decide whether to flip the parameters.

    Handle(Geom_Curve) origCurve = BRepAdaptor_Curve(edge).Curve().Curve();

    // GeomAPI_ProjectPointOnCurve will project a point to an untransformed
    // curve, so make sure to obtain the point on an untransformed edge.
    auto e = edge.Located(TopLoc_Location());

    gp_Pnt firstPoint = BRep_Tool::Pnt(TopExp::FirstVertex(TopoDS::Edge(e)));
    double f = GeomAPI_ProjectPointOnCurve(firstPoint, origCurve).LowerDistanceParameter();

    gp_Pnt lastPoint = BRep_Tool::Pnt(TopExp::LastVertex(TopoDS::Edge(e)));
    double l = GeomAPI_ProjectPointOnCurve(lastPoint, origCurve).LowerDistanceParameter();

    auto adjustPeriodic = [](Handle(Geom_Curve) curve, double &f, double &l) {
        // Copied from Geom_TrimmedCurve::setTrim()
        if (curve->IsPeriodic()) {
            Standard_Real Udeb = curve->FirstParameter();
            Standard_Real Ufin = curve->LastParameter();
            // set f in the range Udeb , Ufin
            // set l in the range f , f + Period()
            ElCLib::AdjustPeriodic(Udeb, Ufin,
                    std::min(std::abs(f-l)/2,Precision::PConfusion()),
                    f, l);
        }
    };

    // Adjust for periodic curve to deal with orientation
    adjustPeriodic(origCurve, f, l);

    // Obtain the middle parameter in order to get the mid point of the arc
    double m = (l - f) * 0.5 + f;
    GeomLProp_CLProps prop(origCurve,m,0,Precision::Confusion());
    gp_Pnt midPoint = prop.Value();
    
    // Transform all three points to the world coordinate
    auto trsf = edge.Location().Transformation();
    midPoint.Transform(trsf);
    firstPoint.Transform(trsf);
    lastPoint.Transform(trsf);

    // Project the points to the sketch plane. Note the coordinates are still
    // in world coordinate system.
    gp_Pnt pm = GeomAPI_ProjectPointOnSurf(midPoint, gPlane).NearestPoint();
    gp_Pnt pf = GeomAPI_ProjectPointOnSurf(firstPoint, gPlane).NearestPoint();
    gp_Pnt pl = GeomAPI_ProjectPointOnSurf(lastPoint, gPlane).NearestPoint();

    // Transform the projected points to sketch plane local coordinates
    pm.Transform(mov);
    pf.Transform(mov);
    pl.Transform(mov);

    // Obtain the corresponding parameters for those points in the projected curve
    double f2 = GeomAPI_ProjectPointOnCurve(pf, curve).LowerDistanceParameter();
    double l2 = GeomAPI_ProjectPointOnCurve(pl, curve).LowerDistanceParameter();
    double m2 = GeomAPI_ProjectPointOnCurve(pm, curve).LowerDistanceParameter();

    firstParameter = f2;
    lastParameter = l2;

    adjustPeriodic(curve, f2, l2);
    adjustPeriodic(curve, f2, m2);
    // If the middle point is out of range, it means we need to choose the
    // other half of the arc.
    if (m2 > l2)
        std::swap(firstParameter, lastParameter);
}

} // anonymous namespace

static Part::Geometry *fitArcs(std::vector<std::unique_ptr<Part::Geometry> > &arcs,
                               const gp_Pnt &P1,
                               const gp_Pnt &P2,
                               double tol)
{
    double radius = 0.0;
    double m = 0.0;
    Base::Vector3d center;
    for (auto &geo : arcs) {
        if (auto arc = Base::freecad_dynamic_cast<Part::GeomArcOfCircle>(geo.get())) {
            if (radius == 0.0) {
                radius = arc->getRadius();
                center = arc->getCenter();
                double f = arc->getFirstParameter();
                double l = arc->getLastParameter();
                m = (l-f)*0.5 + f; // middle parameter
            } else if (std::abs(radius - arc->getRadius()) > tol)
                return nullptr;
        } else
            return nullptr;
    }
    if (radius == 0.0) {
        return nullptr;
    }
    if (P1.SquareDistance(P2) < Precision::Confusion()) {
        Part::GeomCircle* circle = new Part::GeomCircle();
        circle->setCenter(center);
        circle->setRadius(radius);
        return circle;
    }
    if (arcs.size() == 1) {
        auto res = arcs.front().release();
        arcs.clear();
        return res;
    }

    GeomLProp_CLProps prop(Handle(Geom_Curve)::DownCast(arcs.front()->handle()),m,0,Precision::Confusion());
    gp_Pnt midPoint = prop.Value();
    GC_MakeArcOfCircle arc(P1, midPoint, P2);
    auto geo = new Part::GeomArcOfCircle();
    geo->setHandle(arc.Value());
    return geo;
}

void SketchObject::rebuildExternalGeometry(bool defining, bool addIntersection)
{
    Base::StateLocker lock(managedoperation, true); // no need to check input data validity as this is an sketchobject managed operation.

    // get the actual lists of the externals
    auto Objects     = ExternalGeometry.getValues();
    auto SubElements = ExternalGeometry.getSubValues();
    assert(externalGeoRef.size() == Objects.size());
    auto keys = externalGeoRef;

    // Remember which way the projected lines currently run. Signed constraints record which side
    // of a line their subject sits on, and that side is expressed relative to the line direction,
    // so a projection that comes back reversed would otherwise drag the sketch to the other side.
    std::map<long, Base::Vector3d> previousLineDirections;
    for (const auto& geo : ExternalGeo.getValues()) {
        if (auto* line = freecad_cast<const Part::GeomLineSegment*>(geo)) {
            previousLineDirections[GeometryFacade::getId(geo)] =
                line->getEndPoint() - line->getStartPoint();
        }
    }

    // re-check for any missing geometry element. The code here has a side
    // effect that the linked external geometry will continue to work even if
    // ExternalGeometry is wiped out.
    for(auto &geo : ExternalGeo.getValues()) {
        auto egf = ExternalGeometryFacade::getFacade(geo);
        if(egf->getRef().size() && egf->testFlag(ExternalGeometryExtension::Missing)) {
            const std::string &ref = egf->getRef();
            auto pos = ref.find('.');
            if(pos == std::string::npos)
                continue;
            std::string objName = ref.substr(0,pos);
            auto obj = getDocument()->getObject(objName.c_str());
            if(!obj)
                continue;
            std::pair<std::string,std::string> elementName;
            App::GeoFeature::resolveElement(obj,ref.c_str()+pos+1,elementName);
            if(elementName.second.size()
                    && !App::GeoFeature::hasMissingElement(elementName.second.c_str()))
            {
                Objects.push_back(obj);
                SubElements.push_back(elementName.second);
                keys.push_back(ref);
            }
        }
    }

    Base::Placement Plm = Placement.getValue();
    Base::Vector3d Pos = Plm.getPosition();
    Base::Rotation Rot = Plm.getRotation();
    Base::Rotation invRot = Rot.inverse();
    Base::Vector3d dN(0, 0, 1);
    Rot.multVec(dN, dN);
    Base::Vector3d dX(1, 0, 0);
    Rot.multVec(dX, dX);

    Base::Placement invPlm = Plm.inverse();
    Base::Matrix4D invMat = invPlm.toMatrix();
    gp_Trsf mov;
    mov.SetValues(invMat[0][0],
                  invMat[0][1],
                  invMat[0][2],
                  invMat[0][3],
                  invMat[1][0],
                  invMat[1][1],
                  invMat[1][2],
                  invMat[1][3],
                  invMat[2][0],
                  invMat[2][1],
                  invMat[2][2],
                  invMat[2][3]);

    gp_Ax3 sketchAx3(
        gp_Pnt(Pos.x, Pos.y, Pos.z), gp_Dir(dN.x, dN.y, dN.z), gp_Dir(dX.x, dX.y, dX.z));
    gp_Pln sketchPlane(sketchAx3);

    Handle(Geom_Plane) gPlane = new Geom_Plane(sketchPlane);
    BRepBuilderAPI_MakeFace mkFace(sketchPlane);
    TopoDS_Shape aProjFace = mkFace.Shape();

    std::set<std::string> refSet;
    // We use a vector here to keep the order (roughly) the same as ExternalGeometry
    std::vector<std::vector<std::unique_ptr<Part::Geometry> > > newGeos;
    newGeos.reserve(Objects.size());

    for (int i=0; i < int(Objects.size()); i++) {
        const App::DocumentObject *Obj=Objects[i];
        const std::string &SubElement=SubElements[i];
        const std::string &key = keys[i];

        // Skip frozen geometries
        bool frozen = false;
        bool sync = false;
        bool intersection = addIntersection && (i+1 == (int)Objects.size());
        for(auto id : externalGeoRefMap[key]) {
            auto it = externalGeoMap.find(id);
            if(it != externalGeoMap.end()) {
                auto egf = ExternalGeometryFacade::getFacade(ExternalGeo[it->second]);
                if(egf->testFlag(ExternalGeometryExtension::Frozen))
                    frozen = true;
                if(egf->testFlag(ExternalGeometryExtension::Sync))
                    sync = true;
                if (egf->testFlag(ExternalGeometryExtension::Intersection))
                    intersection = true;
            }
        }
        if(frozen && !sync) {
            refSet.insert(std::move(key));
            continue;
        }

        if(!Obj || !Obj->getNameInDocument())
            continue;

        std::vector<std::unique_ptr<Part::Geometry> > geos;

        try {
            TopoDS_Shape refSubShape;
            if (Obj->getTypeId().isDerivedFrom(App::Plane::getClassTypeId())) {
                const App::Plane* pl = static_cast<const App::Plane*>(Obj);
                Base::Placement plm = pl->Placement.getValue();
                Base::Vector3d base = plm.getPosition();
                Base::Rotation rot = plm.getRotation();
                Base::Vector3d normal(0,0,1);
                rot.multVec(normal, normal);
                gp_Pln plane(gp_Pnt(base.x,base.y,base.z), gp_Dir(normal.x, normal.y, normal.z));
                BRepBuilderAPI_MakeFace fBuilder(plane);
                if (fBuilder.IsDone()) {
                    TopoDS_Face f = TopoDS::Face(fBuilder.Shape());
                    refSubShape = f;
                }
            } else {
                refSubShape = Part::Feature::getShape(Obj,SubElement.c_str(),true);
            }

            if(refSubShape.IsNull()) {
                FC_WARN("Null shape from geometry reference in " << getFullName() << ": " << key);
                continue;
            }
            if (refSubShape.ShapeType() != TopAbs_FACE
                    && refSubShape.ShapeType() != TopAbs_EDGE
                    && refSubShape.ShapeType() != TopAbs_VERTEX
                    && refSubShape.ShapeType() != TopAbs_WIRE)
            {
                const std::array<TopAbs_ShapeEnum, 4> types = {
                    TopAbs_FACE, TopAbs_WIRE, TopAbs_EDGE, TopAbs_VERTEX};
                Part::TopoShape shape(refSubShape);
                for (auto type : types) {
                    if (shape.hasSubShape(type)) {
                        refSubShape = shape.getSubShape(type, 1);
                        break;
                    }
                }
            }

            auto importFace = [&](const TopoDS_Shape &refSubShape) {
                gp_Pln plane;
                if (Part::TopoShape(refSubShape).findPlane(plane)) {
                    // Check that the plane is perpendicular to the sketch plane
                    gp_Dir dnormal = plane.Axis().Direction();
                    gp_Dir snormal = sketchPlane.Axis().Direction();
                    if (fabs(dnormal.Angle(snormal) - M_PI_2) < Precision::Confusion()) {
                        // Get vector that is normal to both sketch plane normal and plane normal.
                        // This is the line's direction
                        gp_Dir lnormal = dnormal.Crossed(snormal);
                        BRepBuilderAPI_MakeEdge builder(gp_Lin(plane.Location(), lnormal));
                        builder.Build();
                        if (builder.IsDone()) {
                            const TopoDS_Edge& edge = TopoDS::Edge(builder.Shape());
                            BRepAdaptor_Curve curve(edge);
                            if (curve.GetType() == GeomAbs_Line) {
                                geos.emplace_back(projectLine(curve, gPlane, invPlm));
                            }
                        }

                    } else {
                        FC_WARN("Skip external reference plane that is not normal to sketch plane in "
                                << getFullName() << ": " << key);
                    }
                } else {
                    FC_WARN("Skip non-planar external reference face in sketch "
                                << getFullName() << ": " << key);
                }
            };

            auto checkEdge = [&](const Part::TopoShape &s) {
                if (s.shapeType() != TopAbs_EDGE)
                    return false;
                ShapeAnalysis_Wire saw;
                BRepBuilderAPI_MakeWire mkWire;
                mkWire.Add(TopoDS::Edge(s.getShape()));
                saw.Load(mkWire.Wire());
                double tol = InternalTolerance.getValue();
                if (tol < Precision::Confusion())
                    tol = Precision::Confusion();
                saw.SetPrecision(tol);
                if (saw.CheckSmall(tol)) {
                    FC_WARN("Skip small edge: " << getFullName() << ": " << key);
                    return true;
                }
                if (saw.CheckDegenerated()) {
                    FC_WARN("Skip degenerated edge: " << getFullName() << ": " << key);
                    return true;
                }
                // if (saw.CheckSelfIntersection()) {
                //     FC_WARN("Skip self intersecting edge: " << getFullName() << ": " << key);
                //     return true;
                // }
                return false;
            };

            auto importEdge = [&](const TopoDS_Shape &refSubShape) {
                TopoDS_Edge edge = TopoDS::Edge(refSubShape);
                if (checkEdge(edge)) {
                    return;
                }
                BRepAdaptor_Curve curve(edge);
                Handle(Geom_Curve) origCurve = curve.Curve().Curve();
                gp_Pnt firstPoint, lastPoint;

                if (Part::GeomCurve::isLinear(origCurve)) {
                    geos.emplace_back(projectLine(curve, gPlane, invPlm));
                    return;
                }

                bool done = false;
                // TopExp::First/LastVertex() may throw on infinite edge, so
                // we don't do it for linear edge, which does not require
                // end points for projection
                firstPoint = BRep_Tool::Pnt(TopExp::FirstVertex(edge));
                lastPoint = BRep_Tool::Pnt(TopExp::LastVertex(edge));

                if (curve.GetType() == GeomAbs_Circle) {
                    done = true;
                    gp_Dir vec1 = sketchPlane.Axis().Direction();
                    gp_Dir vec2 = curve.Circle().Axis().Direction();

                    gp_Circ circle = curve.Circle();
                    gp_Pnt cnt = circle.Location();

                    GeomAPI_ProjectPointOnSurf proj(cnt,gPlane);
                    cnt = proj.NearestPoint();
                    circle.SetLocation(cnt);

                    double cosTheta = fabs(vec1.Dot(vec2));  // cos of angle between the two planes, assuming vectirs are normalized to 1
                    double minorRadius = circle.Radius() * cosTheta;

                    // Using plane direction to check for projection type may
                    // cause tolerance problem. Projecting a circle as ellipse
                    // to a plane that has very small angle (but greater than
                    // Precision::AngleConfusion()) difference to the sketch
                    // plane may actually result in the major and minor raidus
                    // being essentially equal (within Precision::Confusion()).
                    // In other words, it is essentially a circle after all.
                    // So it is better to compare the projected major and minor
                    // radius directly here.
                    //
                    // if (vec1.IsParallel(vec2, Precision::Confusion()))
                    if (fabs(circle.Radius() - minorRadius) < Precision::Confusion()) {
                        double length = GCPnts_AbscissaPoint::Length(curve, Precision::Confusion());
                        // Checking close distance of two endpoints is not
                        // enough to decide whether the edge is a complete
                        // circle. It cloud also be a very small arc, whose
                        // curve length is longer than Confusion(), but the
                        // endpoints distance is shorter than Confusion(). So
                        // add one more condition, that is, the whole curve
                        // length should be greater than 6 * radius (about 2 * pi * r).
                        if (length > 6*circle.Radius() 
                                && firstPoint.SquareDistance(lastPoint) < Precision::SquareConfusion()) {
                            Part::GeomCircle* gCircle = new Part::GeomCircle();
                            gCircle->setRadius(circle.Radius());
                            cnt.Transform(mov);
                            gCircle->setCenter(Base::Vector3d(cnt.X(),cnt.Y(),cnt.Z()));

                            GeometryFacade::setConstruction(gCircle, true);
                            geos.emplace_back(gCircle);
                        }
                        else {
                            Part::GeomArcOfCircle* gArc = new Part::GeomArcOfCircle();
                            circle.Transform(mov);
                            Handle(Geom_Curve) hCircle = new Geom_Circle(circle);

                            double firstParam, lastParam;
                            adjustParameterRange(edge, gPlane, mov, hCircle, firstParam, lastParam);

                            Handle(Geom_TrimmedCurve) tCurve = 
                                new Geom_TrimmedCurve(hCircle, firstParam, lastParam);
                            gArc->setHandle(tCurve);
                            GeometryFacade::setConstruction(gArc, true);
                            geos.emplace_back(gArc);
                        }
                    }
                    else if (minorRadius < Precision::Confusion()) {
                        //   projection is a line
                        if (firstPoint.SquareDistance(lastPoint) >= Precision::Confusion()) {
                            //   Arc projected to be a a line
                            done = false; // will handle later by projectEdgeToLine()
                        } else {
                            //   Circle projected to be a line
                            //   define center by projection
                            gp_Pnt cnt = circle.Location();
                            GeomAPI_ProjectPointOnSurf proj(cnt, gPlane);
                            cnt = proj.NearestPoint();

                            gp_Dir dirOrientation = gp_Dir(vec1 ^ vec2);
                            gp_Dir dirLine(dirOrientation);

                            Part::GeomLineSegment* projectedSegment = new Part::GeomLineSegment();
                            Geom_Line ligne(cnt, dirLine);// helper object to compute end points
                            gp_Pnt P1, P2;                // end points of the segment, OCC style

                            ligne.D0(-circle.Radius(), P1);
                            ligne.D0(circle.Radius(), P2);
                            Base::Vector3d p1(P1.X(), P1.Y(), P1.Z());// ends of segment FCAD style
                            Base::Vector3d p2(P2.X(), P2.Y(), P2.Z());
                            invPlm.multVec(p1, p1);
                            invPlm.multVec(p2, p2);
                            projectedSegment->setPoints(p1, p2);
                            GeometryFacade::setConstruction(projectedSegment, true);
                            geos.emplace_back(projectedSegment);
                        }
                    } else {  
                        // Projection to an ellipse or elliptic arc
                        Base::Vector3d p(cnt.X(),cnt.Y(),cnt.Z());  // converting to FCAD style vector
                        invPlm.multVec(p,p);  // transforming towards sketch's (x,y) coordinates

                        gp_Vec vecMajorAxis = vec1 ^ vec2;  // major axis in 3D space
                        Base::Vector3d vectorMajorAxis(vecMajorAxis.X(),vecMajorAxis.Y(),vecMajorAxis.Z());  // maj axis into FCAD style vector
                        invRot.multVec(vectorMajorAxis, vectorMajorAxis);  // transforming to sketch's (x,y) coordinates
                        vecMajorAxis.SetXYZ(gp_XYZ(vectorMajorAxis[0], vectorMajorAxis[1], vectorMajorAxis[2]));  // back to OCC

                        gp_Ax2 refFrameEllipse(gp_Pnt(gp_XYZ(p[0], p[1], p[2])), gp_Vec(0, 0, 1), vecMajorAxis);  // NB: force normal of ellipse to be normal of sketch's plane.
                        Handle(Geom_Ellipse) curve = new Geom_Ellipse(refFrameEllipse, circle.Radius(), minorRadius);

                        if (firstPoint.SquareDistance(lastPoint) < Precision::Confusion()) {
                            Part::GeomEllipse* ellipse = new Part::GeomEllipse();
                            ellipse->setHandle(curve);
                            GeometryFacade::setConstruction(ellipse, true);
                            geos.emplace_back(ellipse);
                        } else {
                            double firstParam, lastParam;
                            adjustParameterRange(edge, gPlane, mov, curve, firstParam, lastParam);

                            Part::GeomArcOfEllipse* gArc = new Part::GeomArcOfEllipse();
                            Handle(Geom_TrimmedCurve) tCurve = 
                                new Geom_TrimmedCurve(curve, firstParam, lastParam);
                            gArc->setHandle(tCurve);
                            GeometryFacade::setConstruction(gArc, true);
                            geos.emplace_back(gArc);
                        }
                    }
                }
                else if (curve.GetType() == GeomAbs_Ellipse) {
                    done = true;

                    gp_Elips elipsOrig = curve.Ellipse();
                    gp_Elips elipsDest;
                    gp_Pnt origCenter = elipsOrig.Location();
                    gp_Pnt destCenter = ProjPointOnPlane_UVN(origCenter, sketchPlane).XYZ();

                    gp_Dir origAxisMajorDir = elipsOrig.XAxis().Direction();
                    gp_Vec origAxisMajor = elipsOrig.MajorRadius() * gp_Vec(origAxisMajorDir);
                    gp_Dir origAxisMinorDir = elipsOrig.YAxis().Direction();
                    gp_Vec origAxisMinor = elipsOrig.MinorRadius() * gp_Vec(origAxisMinorDir);

                    // Here, it used to be a test for parallel direction between the sketchplane and
                    // the elipsOrig, in which the original ellipse would be copied and translated
                    // to the new position. The problem with that approach is that for the sketcher
                    // the normal vector is always (0,0,1). If the original ellipse was not on the
                    // XY plane, the copy will not be either. Then, the dimensions would be wrong
                    // because of the different major axis direction (which is not projected on the
                    // XY plane). So here, we default to the more general ellipse construction
                    // algorithm.
                    //
                    // Doing that solves:
                    // https://forum.freecad.org/viewtopic.php?f=3&t=55284#p477522

                    // GENERAL ELLIPSE CONSTRUCTION ALGORITHM
                    //
                    // look for major axis of projected ellipse
                    //
                    // t is the parameter along the origin ellipse
                    //   OM(t) = origCenter
                    //           + majorRadius * cos(t) * origAxisMajorDir
                    //           + minorRadius * sin(t) * origAxisMinorDir
                    gp_Vec2d PA = ProjVecOnPlane_UV(origAxisMajor, sketchPlane);
                    gp_Vec2d PB = ProjVecOnPlane_UV(origAxisMinor, sketchPlane);
                    double t_max = 2.0 * PA.Dot(PB) / (PA.SquareMagnitude() - PB.SquareMagnitude());
                    t_max = 0.5 * atan(t_max);// gives new major axis is most cases, but not all
                    double t_min = t_max + 0.5 * M_PI;

                    // ON_max = OM(t_max) gives the point, which projected on the sketch plane,
                    //     becomes the apoapse of the projected ellipse.
                    gp_Vec ON_max = origAxisMajor * cos(t_max) + origAxisMinor * sin(t_max);
                    gp_Vec ON_min = origAxisMajor * cos(t_min) + origAxisMinor * sin(t_min);
                    gp_Vec destAxisMajor = ProjVecOnPlane_UVN(ON_max, sketchPlane);
                    gp_Vec destAxisMinor = ProjVecOnPlane_UVN(ON_min, sketchPlane);

                    double RDest = destAxisMajor.Magnitude();
                    double rDest = destAxisMinor.Magnitude();

                    if (RDest < rDest) {
                        double rTmp = rDest;
                        rDest = RDest;
                        RDest = rTmp;
                        gp_Vec axisTmp = destAxisMajor;
                        destAxisMajor = destAxisMinor;
                        destAxisMinor = axisTmp;
                    }

                    double sens = sketchAx3.Direction().Dot(elipsOrig.Position().Direction());
                    gp_Ax2 destCurveAx2(
                        destCenter, gp_Dir(0, 0, sens > 0.0 ? 1.0 : -1.0), gp_Dir(destAxisMajor));

                    if ((RDest - rDest) < (double) Precision::Confusion()) {  // projection is a circle
                        Handle(Geom_Circle) hCircle = new Geom_Circle(destCurveAx2, 0.5 * (rDest + RDest));
                        if (firstPoint.SquareDistance(lastPoint) < Precision::Confusion()) {
                            Part::GeomCircle* circle = new Part::GeomCircle();
                            circle->setHandle(hCircle);
                            GeometryFacade::setConstruction(circle, true);
                            geos.emplace_back(circle);
                        } else {
                            Part::GeomArcOfCircle* gArc = new Part::GeomArcOfCircle();

                            double firstParam, lastParam;
                            adjustParameterRange(edge, gPlane, mov, hCircle, firstParam, lastParam);
                            Handle(Geom_TrimmedCurve) tCurve = 
                                new Geom_TrimmedCurve(hCircle, firstParam, lastParam);
                            gArc->setHandle(tCurve);
                            GeometryFacade::setConstruction(gArc, true);
                            geos.emplace_back(gArc);
                        }
                    }
                    else {
                        if (destAxisMinor.SquareMagnitude() < Precision::SquareConfusion()) {
                            // minor axis is practically zero, means we are projecting to a line
                            if (firstPoint.SquareDistance(lastPoint) >= Precision::Confusion()) {
                                done = false; // non closed case will be handled later by projectEdgeToLine()
                            } else {
                                gp_Vec start = gp_Vec(destCenter.XYZ()) + destAxisMajor;
                                gp_Vec end = gp_Vec(destCenter.XYZ()) - destAxisMajor;

                                Part::GeomLineSegment * projectedSegment = new Part::GeomLineSegment();
                                projectedSegment->setPoints(Base::Vector3d(start.X(), start.Y(), start.Z()),
                                                            Base::Vector3d(end.X(), end.Y(), end.Z()));
                                GeometryFacade::setConstruction(projectedSegment, true);
                                geos.emplace_back(projectedSegment);
                            }
                        }
                        else {
                            elipsDest.SetPosition(destCurveAx2);
                            elipsDest.SetMajorRadius(destAxisMajor.Magnitude());
                            elipsDest.SetMinorRadius(destAxisMinor.Magnitude());
                            Handle(Geom_Ellipse) curve = new Geom_Ellipse(elipsDest);
                            if (firstPoint.SquareDistance(lastPoint) < Precision::Confusion()) {
                                Part::GeomEllipse* ellipse = new Part::GeomEllipse();
                                ellipse->setHandle(curve);
                                GeometryFacade::setConstruction(ellipse, true);
                                geos.emplace_back(ellipse);
                            } else {
                                double firstParam, lastParam;
                                adjustParameterRange(edge, gPlane, mov, curve, firstParam, lastParam);

                                Part::GeomArcOfEllipse* gArc = new Part::GeomArcOfEllipse();
                                Handle(Geom_TrimmedCurve) tCurve = 
                                    new Geom_TrimmedCurve(curve, firstParam, lastParam);
                                gArc->setHandle(tCurve);
                                GeometryFacade::setConstruction(gArc, true);
                                geos.emplace_back(gArc);
                            }
                        }
                    }
                }

                if (!done) {
                    // Try to handle any shape that can be projected as a line.
                    if (auto geo = projectEdgeToLine(edge, invPlm)) {
                        geos.emplace_back(geo);
                        done = true;
                    }
                }
                if (!done) {
                    // In some cases, BSpline (or maybe other type of curves)
                    // silently fails (OCC >= 7.6) projection for some reason,
                    // or produce incorrect shape (OCC < 7.6). We check if the
                    // edge is planar and try to manually move the edge to the
                    // sketch plane.
                    Part::TopoShape projShape;
                    gp_Pln pln;
                    if (Part::TopoShape(edge).findPlane(pln)
                            && pln.Position().Direction().IsParallel(
                                sketchPlane.Position().Direction(), Precision::Confusion())) {
                        // We can't use gp_Pln::Distance() because we need to
                        // know which side the plane is regarding the sketch
                        // double d = pln.Distance(sketchPlane);
                        const gp_Pnt& aP = sketchPlane.Location();
                        const gp_Pnt& aLoc = pln.Location ();
                        const gp_Dir& aDir = pln.Position().Direction();
                        double d = (aDir.X() * (aP.X() - aLoc.X()) +
                                aDir.Y() * (aP.Y() - aLoc.Y()) +
                                aDir.Z() * (aP.Z() - aLoc.Z()));
                        gp_Trsf trsf;
                        trsf.SetTranslation(gp_Vec(aDir) * d);
                        projShape.setShape(edge);
                        projShape.transformShape(Part::TopoShape::convert(trsf), /*copy*/false);
                    } else {
                        BRepOffsetAPI_NormalProjection mkProj(aProjFace);

                        mkProj.Add(edge);
                        mkProj.Build();
                        projShape.setShape(mkProj.Projection());

#if OCC_VERSION_HEX >= 0x070700
                        if (projShape.isNull() || !projShape.hasSubShape(TopAbs_EDGE)) {
                            // Work around OCC 7.7 bug by forcing internal
                            // continuity to C2 (changed to C1 in OCC 7.7, see
                            // BRepAlgo_NormalProjection::SetDefaultParams()).
                            // The bug(?) was introduced in
                            // https://git.dev.opencascade.org/gitweb/?p=occt.git;a=commit;h=4ec4e4e8a8d39a632aed54d0a1fea36c1320a0ce
                            auto Continuity = GeomAbs_C2;
                            double Tol3d = 1.e-4;
                            double Tol2d = Pow(Tol3d, 2./3);
                            double MaxDegree = 14;
                            double MaxSeg    = 16;
                            BRepOffsetAPI_NormalProjection retryProj(aProjFace);
                            retryProj.SetParams(Tol3d, Tol2d, Continuity, MaxDegree, MaxSeg);
                            retryProj.Add(edge);
                            retryProj.Build();
                            projShape.setShape(retryProj.Projection());
                        }
#endif
                        if (projShape.isNull() || !projShape.hasSubShape(TopAbs_EDGE)) {
                            FC_ERR("Invalid geometry in sketch " << getFullName() << ": " << key);
                            return;
                        }
                    }
                    for (auto &e : projShape.getSubTopoShapes(TopAbs_EDGE)) {
                        // Must copy the edge to make the transformation work
                        // for some reason.
                        e.transformShape(invMat, /*copy*/true, /*checkScale*/true);
                        TopoDS_Edge projEdge = TopoDS::Edge(e.getShape());
                        BRepAdaptor_Curve projCurve(projEdge);

                        gp_Pnt P1 = BRep_Tool::Pnt(TopExp::FirstVertex(projEdge));
                        gp_Pnt P2 = BRep_Tool::Pnt(TopExp::LastVertex(projEdge));

                        if (Part::GeomCurve::isLinear(projCurve.Curve().Curve())) {
                            Base::Vector3d p1(P1.X(),P1.Y(),P1.Z());
                            Base::Vector3d p2(P2.X(),P2.Y(),P2.Z());

                            if (Base::Distance(p1,p2) < Precision::Confusion()) {
                                Base::Vector3d p = (p1 + p2) / 2;
                                Part::GeomPoint* point = new Part::GeomPoint(p);
                                GeometryFacade::setConstruction(point, true);
                                geos.emplace_back(point);
                            }
                            else {
                                Part::GeomLineSegment* line = new Part::GeomLineSegment();
                                line->setPoints(p1,p2);
                                GeometryFacade::setConstruction(line, true);
                                geos.emplace_back(line);
                            }
                        }
                        else if (projCurve.GetType() == GeomAbs_Circle) {
                            gp_Circ c = projCurve.Circle();
                            gp_Pnt p = c.Location();

                            if (P1.SquareDistance(P2) < Precision::Confusion()) {
                                Part::GeomCircle* circle = new Part::GeomCircle();
                                circle->setRadius(c.Radius());
                                circle->setCenter(Base::Vector3d(p.X(),p.Y(),p.Z()));

                                GeometryFacade::setConstruction(circle, true);
                                geos.emplace_back(circle);
                            }
                            else {
                                Part::GeomArcOfCircle* arc = new Part::GeomArcOfCircle();
                                Handle(Geom_Curve) curve = new Geom_Circle(c);

                                double Param1, Param2;
                                getParameterRange(curve, P1, P2, Param1, Param2);

                                Handle(Geom_TrimmedCurve) tCurve =
                                    new Geom_TrimmedCurve(curve, Param1, Param2);
                                arc->setHandle(tCurve);
                                GeometryFacade::setConstruction(arc, true);
                                geos.emplace_back(arc);
                            }
                        } else if (projCurve.GetType() == GeomAbs_BSplineCurve) {

                            std::unique_ptr<Part::GeomBSplineCurve> bspline;

                            if (ArcFitTolerance.getValue() >= Precision::Confusion()) {
                                bspline.reset(new Part::GeomBSplineCurve(projCurve));
                                std::vector<std::unique_ptr<Part::Geometry>> arcs;
                                for (auto arc : bspline->toBiArcs(ArcFitTolerance.getValue()))
                                    arcs.emplace_back(arc);
                                if (auto geo = fitArcs(arcs, P1, P2, ArcFitTolerance.getValue())) {
                                    geos.emplace_back(geo);
                                    GeometryFacade::setConstruction(geo, true);
                                    return;
                                }
                            }

                            //int s = bSplineSplitter.NbSplits();
                            if (curve.GetType() == GeomAbs_Circle) {
                                // Unfortunately, a normal projection of a circle can also give a Bspline
                                // Split the spline into arcs
                                GeomConvert_BSplineCurveKnotSplitting bSplineSplitter(projCurve.BSpline(), 2);
                                if (bSplineSplitter.NbSplits() == 2) {
                                    // Result of projection is actually a circle...
                                    TColStd_Array1OfInteger splits(1, 2);
                                    bSplineSplitter.Splitting(splits);
                                    gp_Pnt p1 = projCurve.Value(splits(1));
                                    gp_Pnt p2 = projCurve.Value(splits(2));
                                    gp_Pnt p3 = projCurve.Value(0.5 * (splits(1) + splits(2)));
                                    GC_MakeCircle circleMaker(p1, p2, p3);
                                    Handle(Geom_Circle) circ = circleMaker.Value();
                                    Part::GeomCircle* circle = new Part::GeomCircle();
                                    circle->setRadius(circ->Radius());
                                    gp_Pnt center = circ->Axis().Location();
                                    circle->setCenter(Base::Vector3d(center.X(), center.Y(), center.Z()));

                                    GeometryFacade::setConstruction(circle, true);
                                    geos.emplace_back(circle);
                                    return;
                                }
                            }

                            if (!bspline)
                                bspline.reset(new Part::GeomBSplineCurve(projCurve));
                            simplifyBSpline(bspline.get(), key);
                            GeometryFacade::setConstruction(bspline.get(), true);
                            geos.emplace_back(bspline.release());

                        } else if (projCurve.GetType() == GeomAbs_Hyperbola) {
                            gp_Hypr e = projCurve.Hyperbola();
                            gp_Pnt p = e.Location();

                            gp_Dir normal = e.Axis().Direction();
                            gp_Dir xdir = e.XAxis().Direction();
                            gp_Ax2 xdirref(p, normal);

                            if (P1.SquareDistance(P2) < Precision::Confusion()) {
                                Part::GeomHyperbola* hyperbola = new Part::GeomHyperbola();
                                hyperbola->setMajorRadius(e.MajorRadius());
                                hyperbola->setMinorRadius(e.MinorRadius());
                                hyperbola->setCenter(Base::Vector3d(p.X(),p.Y(),p.Z()));
                                hyperbola->setAngleXU(-xdir.AngleWithRef(xdirref.XDirection(),normal));
                                GeometryFacade::setConstruction(hyperbola, true);
                                geos.emplace_back(hyperbola);
                            }
                            else {
                                Part::GeomArcOfHyperbola* aoh = new Part::GeomArcOfHyperbola();
                                Handle(Geom_Curve) curve = new Geom_Hyperbola(e);

                                double Param1, Param2;
                                getParameterRange(curve, P1, P2, Param1, Param2);

                                Handle(Geom_TrimmedCurve) tCurve =
                                    new Geom_TrimmedCurve(curve, Param1, Param2);
                                aoh->setHandle(tCurve);
                                GeometryFacade::setConstruction(aoh, true);
                                geos.emplace_back(aoh);
                            }
                        } else if (projCurve.GetType() == GeomAbs_Parabola) {
                            gp_Parab e = projCurve.Parabola();
                            gp_Pnt p = e.Location();

                            gp_Dir normal = e.Axis().Direction();
                            gp_Dir xdir = e.XAxis().Direction();
                            gp_Ax2 xdirref(p, normal);

                            if (P1.SquareDistance(P2) < Precision::Confusion()) {
                                Part::GeomParabola* parabola = new Part::GeomParabola();
                                parabola->setFocal(e.Focal());
                                parabola->setCenter(Base::Vector3d(p.X(),p.Y(),p.Z()));
                                parabola->setAngleXU(-xdir.AngleWithRef(xdirref.XDirection(),normal));
                                GeometryFacade::setConstruction(parabola, true);
                                geos.emplace_back(parabola);
                            }
                            else {
                                Part::GeomArcOfParabola* aop = new Part::GeomArcOfParabola();
                                Handle(Geom_Curve) curve = new Geom_Parabola(e);

                                double Param1, Param2;
                                getParameterRange(curve, P1, P2, Param1, Param2);

                                Handle(Geom_TrimmedCurve) tCurve =
                                    new Geom_TrimmedCurve(curve, Param1, Param2);
                                aop->setHandle(tCurve);
                                GeometryFacade::setConstruction(aop, true);
                                geos.emplace_back(aop);
                            }
                        }
                        else if (projCurve.GetType() == GeomAbs_Ellipse) {
                            gp_Elips e = projCurve.Ellipse();
                            gp_Pnt p = e.Location();

                            //gp_Dir normal = e.Axis().Direction();
                            gp_Dir normal = gp_Dir(0,0,1);
                            gp_Ax2 xdirref(p, normal);

                            if (P1.SquareDistance(P2) < Precision::Confusion()) {
                                Part::GeomEllipse* ellipse = new Part::GeomEllipse();
                                Handle(Geom_Ellipse) curve = new Geom_Ellipse(e);
                                ellipse->setHandle(curve);
                                GeometryFacade::setConstruction(ellipse, true);
                                geos.emplace_back(ellipse);
                            }
                            else {
                                Part::GeomArcOfEllipse* aoe = new Part::GeomArcOfEllipse();
                                Handle(Geom_Curve) curve = new Geom_Ellipse(e);

                                double Param1, Param2;
                                getParameterRange(curve, P1, P2, Param1, Param2);

                                Handle(Geom_TrimmedCurve) tCurve =
                                    new Geom_TrimmedCurve(curve, Param1, Param2);
                                aoe->setHandle(tCurve);
                                GeometryFacade::setConstruction(aoe, true);
                                geos.emplace_back(aoe);
                            }
                        }
                        else {
                            TopoDS_Edge e = TopoDS::Edge(projEdge.Located(TopLoc_Location()));
                            P1 = BRep_Tool::Pnt(TopExp::FirstVertex(e));
                            P2 = BRep_Tool::Pnt(TopExp::LastVertex(e));

                            double Param1, Param2;
                            BRepAdaptor_Curve bac(e);
                            Handle(Geom_Curve) c = bac.Curve().Curve();
                            try {
                                getParameterRange(c, P1, P2, Param1, Param2);
                            }
                            catch (Standard_Failure &) {
                                Param1 = bac.FirstParameter();
                                Param2 = bac.LastParameter();
                                FC_WARN("Failed to get projected curve parameters. Using fall backs, " << Param1 << ", " << Param2);
                                Part::Feature::create(edge, "failed");
                                if (Param1 > Param2)
                                    std::swap(Param1, Param2);
                            }
                            auto bspline = Part::GeomCurve::toBSpline(c, Param1, Param2);
                            if (!bspline) {
                                FC_ERR("Not supported projected geometry in sketch " << getFullName() << ": " << key);
                                geos.clear();
                            }
                            else {
                                simplifyBSpline(bspline, key);
                                TopLoc_Location loc = projEdge.Location();
                                bspline->handle()->Transform(loc.Transformation());
                                GeometryFacade::setConstruction(bspline, true);
                                geos.emplace_back(bspline);
                            }
                        }
                    }
                }
            };
            
            auto importVertex = [&](const TopoDS_Shape &refSubShape) {
                gp_Pnt P = BRep_Tool::Pnt(TopoDS::Vertex(refSubShape));
                GeomAPI_ProjectPointOnSurf proj(P, gPlane);
                P = proj.NearestPoint();
                Base::Vector3d p(P.X(), P.Y(), P.Z());
                invPlm.multVec(p, p);

                Part::GeomPoint* point = new Part::GeomPoint(p);
                GeometryFacade::setConstruction(point, true);
                geos.emplace_back(point);
            };


            switch (refSubShape.ShapeType())
            {
            case TopAbs_FACE:
                if (!intersection)
                    importFace(refSubShape);
                break;
            case TopAbs_WIRE:
                for (const auto &s : Part::TopoShape(refSubShape).getSubShapes(TopAbs_EDGE))
                    importEdge(s);
                break;
            case TopAbs_EDGE:
                importEdge(refSubShape);
                break;
            case TopAbs_VERTEX:
                importVertex(refSubShape);
                break;
            default:
                FC_ERR("Unknown type of geometry in " << getFullName() << ": " << key);
                break;
            }

            if (intersection && (refSubShape.ShapeType() == TopAbs_EDGE
                                 || refSubShape.ShapeType() == TopAbs_FACE))
            {
                BRepAlgoAPI_Section maker(refSubShape, sketchPlane);
                maker.Approximation(Standard_True);
                if (!maker.IsDone())
                    FC_THROWM(Base::CADKernelError,"Failed to get intersection");
                Part::TopoShape intersectionShape(maker.Shape());
                auto edges = intersectionShape.getSubTopoShapes(TopAbs_EDGE);
                for (const auto &s : edges)
                    importEdge(s.getShape());
                // Section of some face (e.g. sphere) produce more than one arcs
                // from the same circle. So we try to fit the arcs with a single
                // circle/arc.
                if (refSubShape.ShapeType() == TopAbs_FACE && geos.size() > 1) {
                    auto wires = Part::TopoShape().makEWires(edges);
                    if (wires.countSubShapes(TopAbs_WIRE) == 1) {
                        TopoDS_Vertex firstVertex, lastVertex;
                        BRepTools_WireExplorer exp(TopoDS::Wire(wires.getSubShape(TopAbs_WIRE, 1)));
                        firstVertex = exp.CurrentVertex();
                        while (!exp.More())
                            exp.Next();
                        lastVertex = exp.CurrentVertex();
                        gp_Pnt P1 = BRep_Tool::Pnt(firstVertex);
                        gp_Pnt P2 = BRep_Tool::Pnt(lastVertex);
                        if (auto geo = fitArcs(geos, P1, P2, ArcFitTolerance.getValue())) {
                            geos.clear();
                            geos.emplace_back(geo);
                        }
                    }
                }
                for (const auto &s : intersectionShape.getSubShapes(TopAbs_VERTEX, TopAbs_EDGE))
                    importVertex(s);
            }

        } catch (Base::Exception &e) {
            FC_ERR("Failed to project external geometry in "
                   << getFullName() << ": " << key << std::endl << e.what());
            continue;
        } catch (Standard_Failure &e) {
            FC_ERR("Failed to project external geometry in "
                   << getFullName() << ": " << key << std::endl << e.GetMessageString());
            continue;
        } catch (std::exception &e) {
            FC_ERR("Failed to project external geometry in "
                   << getFullName() << ": " << key << std::endl << e.what());
            continue;
        } catch (...) {
            FC_ERR("Failed to project external geometry in "
                   << getFullName() << ": " << key << std::endl << "Unknown exception");
            continue;
        }
        if(geos.empty())
            continue;

        if(!refSet.emplace(key).second) {
            FC_WARN("Duplicated external reference in " << getFullName() << ": " << key);
            continue;
        }
        if (intersection) {
            for(auto &geo : geos) {
                auto egf = ExternalGeometryFacade::getFacade(geo.get());
                egf->setFlag(ExternalGeometryExtension::Intersection);
                egf->setFlag(ExternalGeometryExtension::Defining, defining);
            }
        } else if (defining && i+1==(int)Objects.size()) {
            for(auto &geo : geos)
                ExternalGeometryFacade::getFacade(geo.get())->setFlag(
                        ExternalGeometryExtension::Defining);
        }
        for(auto &geo : geos)
            ExternalGeometryFacade::getFacade(geo.get())->setRef(key);
        newGeos.push_back(std::move(geos));
    }

    // allocate unique geometry id
    for(auto &geos : newGeos) {
        auto egf = ExternalGeometryFacade::getFacade(geos.front().get());
        auto &refs = externalGeoRefMap[egf->getRef()];
        while(refs.size() < geos.size())
            refs.push_back(++geoLastId);

        // In case a projection reduces output geometries, delete them
        std::set<long> geoIds;
        geoIds.insert(refs.begin()+geos.size(),refs.end());

        // Sync id and ref of the new geometries
        int i = 0;
        for(auto &geo : geos)
            GeometryFacade::setId(geo.get(), refs[i++]);

        delExternalPrivate(geoIds,false);
    }

    auto geoms = ExternalGeo.getValues();

    // now update the geometries
    for(auto &geos : newGeos) {
        for(auto &geo : geos) {
            auto it = externalGeoMap.find(GeometryFacade::getId(geo.get()));
            if(it == externalGeoMap.end()) {
                // This is a new geometries.
                geoms.push_back(geo.release());
                continue;
            }
            // This is an existing geometry. Update it while keeping the old flags
            ExternalGeometryFacade::copyFlags(geoms[it->second], geo.get());
            geoms[it->second] = geo.release();
        }
    }

    // Check for any missing references
    bool hasError = false;
    for(auto geo : geoms) {
        auto egf = ExternalGeometryFacade::getFacade(geo);
        egf->setFlag(ExternalGeometryExtension::Sync,false);
        if(egf->getRef().empty())
            continue;
        if(!refSet.count(egf->getRef())) {
            FC_ERR( "External geometry " << getFullName() << ".e" << egf->getId()
                    << " missing reference: " << egf->getRef());
            hasError = true;
            egf->setFlag(ExternalGeometryExtension::Missing,true);
        } else {
            egf->setFlag(ExternalGeometryExtension::Missing,false);
        }
    }

    std::set<int> reversedGeoIds;
    for (std::size_t index = 0; index < geoms.size(); ++index) {
        auto* line = freecad_cast<const Part::GeomLineSegment*>(geoms[index]);
        if (!line) {
            continue;
        }
        auto previous = previousLineDirections.find(GeometryFacade::getId(geoms[index]));
        if (previous != previousLineDirections.end()
            && (line->getEndPoint() - line->getStartPoint()).Dot(previous->second) < 0.0) {
            reversedGeoIds.insert(-static_cast<int>(index) - 1);
        }
    }

    ExternalGeo.setValues(std::move(geoms));
    rebuildVertexIndex();

    reorientConstraintsOnReversedGeometry(reversedGeoIds);

    // clean up geometry reference
    if(refSet.size() != (size_t)ExternalGeometry.getSize()) {
        if(refSet.size() < keys.size()) {
            auto itObj = Objects.begin();
            auto itSub = SubElements.begin();
            for(auto &ref : keys) {
                if(!refSet.count(ref)) {
                    itObj = Objects.erase(itObj);
                    itSub = SubElements.erase(itSub);
                }else {
                    ++itObj;
                    ++itSub;
                }
            }
        }
        ExternalGeometry.setValues(Objects,SubElements);
    }

    solverNeedsUpdate=true;
    Constraints.acceptGeometry(getCompleteGeometry());

    if(hasError && this->isRecomputing())
        THROWM(Base::RuntimeError, "Missing external geometry reference")
}

void SketchObject::fixExternalGeometry(const std::vector<int> &geoIds) {
    std::set<int> idSet(geoIds.begin(),geoIds.end());
    auto geos = ExternalGeo.getValues();
    auto objs = ExternalGeometry.getValues();
    auto subs = ExternalGeometry.getSubValues();
    bool touched = false;
    for(int i=2;i<(int)geos.size();++i) {
        auto &geo = geos[i];
        auto egf = ExternalGeometryFacade::getFacade(geo);
        int GeoId = -i-1;
        if(egf->getRef().empty()
                || !egf->testFlag(ExternalGeometryExtension::Missing)
                || (idSet.size() && !idSet.count(GeoId)))
            continue;
        std::string ref = egf->getRef();
        auto pos = ref.find('.');
        if(pos == std::string::npos) {
            FC_ERR("Invalid geometry reference " << ref);
            continue;
        }
        std::string objName = ref.substr(0,pos);
        auto obj = getDocument()->getObject(objName.c_str());
        if(!obj) {
            FC_ERR("Cannot find object in reference " << ref);
            continue;
        }
        
        auto elements = Part::Feature::getRelatedElements(obj,ref.c_str()+pos+1);
        if(!elements.size()) {
            FC_ERR("No related reference found for " << ref);
            continue;
        }

        geo = geo->clone();
        egf->setGeometry(geo);
        egf->setFlag(ExternalGeometryExtension::Missing,false);
        ref = objName + "." + Data::elementMapPrefix();
        elements.front().name.appendToBuffer(ref);
        egf->setRef(ref);
        objs.push_back(obj);
        subs.emplace_back();
        elements.front().index.appendToStringBuffer(subs.back());
        touched = true;
    }

    if(touched) {
        ExternalGeo.setValues(geos);
        ExternalGeometry.setValues(objs,subs);
        rebuildExternalGeometry();
    }
}

void SketchObject::updateGeometryRefs() {
    const auto &objs = ExternalGeometry.getValues();
    const auto &subs = ExternalGeometry.getSubValues();
    const auto &shadows = ExternalGeometry.getShadowSubs();
    assert(subs.size() == shadows.size());
    std::vector<std::string> originalRefs;
    std::map<std::string,std::string> refMap;
    if(updateGeoRef) {
        assert(externalGeoRef.size() == objs.size());
        updateGeoRef = false;
        originalRefs = std::move(externalGeoRef);
    }
    externalGeoRef.clear();
    std::unordered_map<std::string, int> legacyMap;
    for(int i=0;i<(int)objs.size();++i) {
        auto obj = objs[i];
        const std::string &sub=shadows[i].first.size()?shadows[i].first:subs[i];
        externalGeoRef.emplace_back(obj->getNameInDocument());
        auto &key = externalGeoRef.back();
        key += '.';

        legacyMap[key + Data::oldElementName(sub.c_str())] = i;

        if (!obj->getTypeId().isDerivedFrom(Part::Datum::getClassTypeId()))
            key += Data::newElementName(sub.c_str());
        if(originalRefs.size() && originalRefs[i]!=key)
            refMap[originalRefs[i]] = key;
    }
    bool touched = false;
    auto geos = ExternalGeo.getValues();
    if(refMap.empty()) {
        for(auto geo : geos) {
            auto egf = ExternalGeometryFacade::getFacade(geo);
            if(egf->getRefIndex()<0) {
                if (egf->getId() < 0 && egf->getRef().size()) {
                    FC_ERR("External geometry reference corrupted in " << getFullName()
                            << " Please check.");
                    // This could happen if someone saved the sketch containing
                    // external geometries using some rouge releases during the
                    // migration period. As a remedy, We re-initiate the
                    // external geometry here to trigger rebuild later, with
                    // call to rebuildExternalGeometry()
                    initExternalGeo();
                    return;
                }
                auto it = legacyMap.find(egf->getRef());
                if (it != legacyMap.end() && egf->getRef() != externalGeoRef[it->second]) {
                    if(getDocument() && !getDocument()->isPerformingTransaction()) {
                        // FIXME: this is a bug. Find out when and why does this happen
                        //
                        // Amendment: maybe the original bug is because of not
                        // handling external geometry changes during undo/redo,
                        // which should be considered as normal. So warning only
                        // if not undo/redo.
                        //
                        FC_WARN("Update legacy external reference " << egf->getRef() << " -> "
                                << externalGeoRef[it->second] << " in " << getFullName());
                    } else
                        FC_LOG("Update undo/redo external reference " << egf->getRef() << " -> "
                                << externalGeoRef[it->second] << " in " << getFullName());
                    touched = true;
                    egf->setRef(externalGeoRef[it->second]);
                }
                continue;
            }
            else if(egf->getRefIndex() < (int)externalGeoRef.size()
                    && egf->getRef() != externalGeoRef[egf->getRefIndex()])
            {
                touched = true;
                egf->setRef(externalGeoRef[egf->getRefIndex()]);
            }
            egf->setRefIndex(-1);
        }
    }else{
        for(auto &v : refMap) {
            auto it = externalGeoRefMap.find(v.first);
            if(it == externalGeoRefMap.end())
                continue;
            for(long id : it->second) {
                auto iter = externalGeoMap.find(id);
                if(iter!=externalGeoMap.end()) {
                    auto &geo = geos[iter->second];
                    geo = geo->clone();
                    auto egf = ExternalGeometryFacade::getFacade(geo);
                    FC_LOG(getFullName() << " ref change on ExternalEdge"
                            << iter->second-1 << ' ' << egf->getRef() << " -> " << v.second);
                    egf->setRef(v.second);
                    touched = true;
                }
            }
        }
    }
    if(touched)
        ExternalGeo.setValues(std::move(geos));
}

std::vector<std::string> SketchObject::checkSubNames(const std::vector<std::string> &subnames) const{
    std::vector<std::string> ret;
    ret.reserve(subnames.size());
    for(const auto &subname : subnames) {
        auto indexedName = checkSubName(subname.c_str());
        if (!indexedName.isNull()) {
            ret.emplace_back();
            indexedName.appendToStringBuffer(ret.back());
        }
    }
    return ret;
}

std::string SketchObject::getGeometryReference(int GeoId) const {
    auto geo = getGeometry(GeoId);
    if (!geo)
        return std::string();
    auto egf = ExternalGeometryFacade::getFacade(geo);
    if (egf->getRef().empty())
        return std::string();

    const std::string &ref = egf->getRef();

    if(egf->testFlag(ExternalGeometryExtension::Missing))
        return std::string("? ") + ref;

    auto pos = ref.find('.');
    if(pos == std::string::npos)
        return ref;
    std::string objName = ref.substr(0,pos);
    auto obj = getDocument()->getObject(objName.c_str());
    if(!obj)
        return ref;

    std::pair<std::string,std::string> elementName;
    App::GeoFeature::resolveElement(obj,ref.c_str()+pos+1,elementName);
    if(elementName.second.size())
        return objName + "." + elementName.second;
    return ref;
}

// clang-format on
