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

Base::Vector3d SketchObject::getPoint(const Part::Geometry *geo, PointPos PosId)
{
    if (geo->is<Part::GeomPoint>()) {
        const Part::GeomPoint* p = static_cast<const Part::GeomPoint*>(geo);
        if (PosId == PointPos::start || PosId == PointPos::mid || PosId == PointPos::end)
            return p->getPoint();
    } else if (geo->is<Part::GeomLineSegment>()) {
        const Part::GeomLineSegment *lineSeg = static_cast<const Part::GeomLineSegment*>(geo);
        if (PosId == PointPos::start)
            return lineSeg->getStartPoint();
        else if (PosId == PointPos::end)
            return lineSeg->getEndPoint();
    } else if (geo->is<Part::GeomCircle>()) {
        const Part::GeomCircle *circle = static_cast<const Part::GeomCircle*>(geo);
        auto pt = circle->getCenter();
        if(PosId != PointPos::mid)
             pt.x += circle->getRadius();
        return pt;
    } else if (geo->is<Part::GeomEllipse>()) {
        const Part::GeomEllipse *ellipse = static_cast<const Part::GeomEllipse*>(geo);
        auto pt = ellipse->getCenter();
        if(PosId != PointPos::mid)
            pt += ellipse->getMajorAxisDir()*ellipse->getMajorRadius();
        return pt;
    } else if (geo->is<Part::GeomArcOfCircle>()) {
        const Part::GeomArcOfCircle *aoc = static_cast<const Part::GeomArcOfCircle*>(geo);
        if (PosId == PointPos::start)
            return aoc->getStartPoint(/*emulateCCW=*/true);
        else if (PosId == PointPos::end)
            return aoc->getEndPoint(/*emulateCCW=*/true);
        else if (PosId == PointPos::mid)
            return aoc->getCenter();
    } else if (geo->is<Part::GeomArcOfEllipse>()) {
        const Part::GeomArcOfEllipse *aoc = static_cast<const Part::GeomArcOfEllipse*>(geo);
        if (PosId == PointPos::start)
            return aoc->getStartPoint(/*emulateCCW=*/true);
        else if (PosId == PointPos::end)
            return aoc->getEndPoint(/*emulateCCW=*/true);
        else if (PosId == PointPos::mid)
            return aoc->getCenter();
    } else if (geo->is<Part::GeomArcOfHyperbola>()) {
        const Part::GeomArcOfHyperbola *aoh = static_cast<const Part::GeomArcOfHyperbola*>(geo);
        if (PosId == PointPos::start)
            return aoh->getStartPoint();
        else if (PosId == PointPos::end)
            return aoh->getEndPoint();
        else if (PosId == PointPos::mid)
            return aoh->getCenter();
    } else if (geo->is<Part::GeomArcOfParabola>()) {
        const Part::GeomArcOfParabola *aop = static_cast<const Part::GeomArcOfParabola*>(geo);
        if (PosId == PointPos::start)
            return aop->getStartPoint();
        else if (PosId == PointPos::end)
            return aop->getEndPoint();
        else if (PosId == PointPos::mid)
            return aop->getCenter();
    } else if (geo->is<Part::GeomBSplineCurve>()) {
        const Part::GeomBSplineCurve *bsp = static_cast<const Part::GeomBSplineCurve*>(geo);
        if (PosId == PointPos::start)
            return bsp->getStartPoint();
        else if (PosId == PointPos::end)
            return bsp->getEndPoint();
    }
    return Base::Vector3d();
}

Base::Vector3d SketchObject::getPoint(int GeoId, PointPos PosId) const
{
    if (!(GeoId == H_Axis || GeoId == V_Axis
          || (GeoId <= getHighestCurveIndex() && GeoId >= -getExternalGeometryCount())))
        THROWM(Base::ValueError, "SketchObject::getPoint. Invalid GeoId was supplied.")
    const Part::Geometry* geo = getGeometry(GeoId);
    return getPoint(geo,PosId);
}

int SketchObject::getAxisCount() const
{
    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    int count = 0;
    for (std::vector<Part::Geometry*>::const_iterator geo = vals.begin(); geo != vals.end(); geo++)
        if ((*geo) && GeometryFacade::getConstruction(*geo)
            && (*geo)->is<Part::GeomLineSegment>())
            count++;

    return count;
}

Base::Axis SketchObject::getAxis(int axId) const
{
    if (axId == H_Axis || axId == V_Axis || axId == N_Axis)
        return Part::Part2DObject::getAxis(axId);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();
    int count = 0;
    for (std::vector<Part::Geometry*>::const_iterator geo = vals.begin(); geo != vals.end(); geo++)
        if ((*geo) && GeometryFacade::getConstruction(*geo)
            && (*geo)->is<Part::GeomLineSegment>()) {
            if (count == axId) {
                Part::GeomLineSegment* lineSeg = static_cast<Part::GeomLineSegment*>(*geo);
                Base::Vector3d start = lineSeg->getStartPoint();
                Base::Vector3d end = lineSeg->getEndPoint();
                return Base::Axis(start, end - start);
            }
            count++;
        }

    return Base::Axis();
}

bool SketchObject::isSupportedGeometry(const Part::Geometry* geo) const
{
    if (geo->is<Part::GeomPoint>()
        || geo->is<Part::GeomCircle>()
        || geo->is<Part::GeomEllipse>()
        || geo->is<Part::GeomArcOfCircle>()
        || geo->is<Part::GeomArcOfEllipse>()
        || geo->is<Part::GeomArcOfHyperbola>()
        || geo->is<Part::GeomArcOfParabola>()
        || geo->is<Part::GeomBSplineCurve>()
        || geo->is<Part::GeomLineSegment>()) {
        return true;
    }
    if (geo->is<Part::GeomTrimmedCurve>()) {
        Handle(Geom_TrimmedCurve) trim = Handle(Geom_TrimmedCurve)::DownCast(geo->handle());
        Handle(Geom_Circle) circle = Handle(Geom_Circle)::DownCast(trim->BasisCurve());
        Handle(Geom_Ellipse) ellipse = Handle(Geom_Ellipse)::DownCast(trim->BasisCurve());
        if (!circle.IsNull() || !ellipse.IsNull()) {
            return true;
        }
    }
    return false;
}

std::vector<Part::Geometry*>
SketchObject::supportedGeometry(const std::vector<Part::Geometry*>& geoList) const
{
    std::vector<Part::Geometry*> supportedGeoList;
    supportedGeoList.reserve(geoList.size());
    // read-in geometry that the sketcher cannot handle
    for (std::vector<Part::Geometry*>::const_iterator it = geoList.begin(); it != geoList.end();
         ++it) {
        if (isSupportedGeometry(*it)) {
            supportedGeoList.push_back(*it);
        }
    }

    return supportedGeoList;
}

int SketchObject::addGeometry(const std::vector<Part::Geometry*>& geoList,
                              bool construction /*=false*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);
    newVals.reserve(newVals.size() + geoList.size());
    for (auto& v : geoList) {
        Part::Geometry* copy = v->copy();
        generateId(copy);

        if (copy->is<Part::GeomPoint>()) {
            // creation mode for points is always construction not to
            // break legacy code
            GeometryFacade::setConstruction(copy, true);
        }
        else if (construction) {
            GeometryFacade::setConstruction(copy, construction);
        }

        newVals.push_back(copy);
    }

    // On setting geometry the onChanged method will call acceptGeometry(), thereby updating
    // constraint geometry indices and rebuilding the vertex index
    Geometry.setValues(std::move(newVals));

    return Geometry.getSize() - 1;
}

int SketchObject::addGeometry(const Part::Geometry* geo, bool construction /*=false*/)
{
    // this copy has a new random tag (see copy() vs clone())
    auto geoNew = std::unique_ptr<Part::Geometry>(geo->copy());

    return addGeometry(std::move(geoNew), construction);
}

int SketchObject::addGeometry(std::unique_ptr<Part::Geometry> newgeo, bool construction /*=false*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    Part::Geometry *geoNew = newgeo.release();
    generateId(geoNew);

    if (geoNew->is<Part::GeomPoint>()) {
        // creation mode for points is always construction not to
        // break legacy code
        GeometryFacade::setConstruction(geoNew, true);
    }
    else if (construction) {
        GeometryFacade::setConstruction(geoNew, construction);
    }

    newVals.push_back(geoNew);

    // On setting geometry the onChanged method will call acceptGeometry(), thereby updating
    // constraint geometry indices and rebuilding the vertex index
    Geometry.setValues(std::move(newVals));

    return Geometry.getSize() - 1;
}

int SketchObject::delGeometry(int GeoId, bool deleteinternalgeo)
{
    if (GeoId < 0) {
        if(GeoId > GeoEnum::RefExt)
            return -1;
        return delExternal(-GeoId-1);
    }

    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();
    if (GeoId < 0 || GeoId >= int(vals.size()))
        return -1;

    if (deleteinternalgeo) {
        const Part::Geometry* geo = getGeometry(GeoId);
        // Only for supported types
        if ((geo->is<Part::GeomEllipse>()
             || geo->is<Part::GeomArcOfEllipse>()
             || geo->is<Part::GeomArcOfHyperbola>()
             || geo->is<Part::GeomArcOfParabola>()
             || geo->is<Part::GeomBSplineCurve>())) {

            this->deleteUnusedInternalGeometry(GeoId, true);

            return 0;
        }
    }

    std::vector<Part::Geometry*> newVals(vals);
    newVals.erase(newVals.begin() + GeoId);

    // Find coincident points to replace the points of the deleted geometry
    std::vector<int> GeoIdList;
    std::vector<PointPos> PosIdList;
    for (PointPos PosId = PointPos::start; PosId != PointPos::mid;) {
        getDirectlyCoincidentPoints(GeoId, PosId, GeoIdList, PosIdList);
        if (GeoIdList.size() > 1) {
            delConstraintOnPoint(GeoId, PosId, true /* only coincidence */);
            transferConstraints(GeoIdList[0], PosIdList[0], GeoIdList[1], PosIdList[1]);
        }
        // loop through [start, end, mid]
        PosId = (PosId == PointPos::start) ? PointPos::end : PointPos::mid;
    }

    const std::vector<Constraint*>& constraints = this->Constraints.getValues();
    std::vector<Constraint*> newConstraints;
    newConstraints.reserve(constraints.size());
    for (auto cstr : constraints) {
        if (cstr->First == GeoId || cstr->Second == GeoId || cstr->Third == GeoId)
            continue;
        if (cstr->First > GeoId || cstr->Second > GeoId || cstr->Third > GeoId) {
            cstr = cstr->clone();
            if (cstr->First > GeoId)
                cstr->First -= 1;
            if (cstr->Second > GeoId)
                cstr->Second -= 1;
            if (cstr->Third > GeoId)
                cstr->Third -= 1;
        }
        newConstraints.push_back(cstr);
    }

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);
        this->Geometry.setValues(std::move(newVals));
        this->Constraints.setValues(std::move(newConstraints));
    }

    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::delGeometriesExclusiveList(const std::vector<int>& GeoIds)
{
    std::vector<int> sGeoIds(GeoIds);

    std::sort(sGeoIds.begin(), sGeoIds.end());
    if (sGeoIds.empty())
        return 0;

    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();
    if (sGeoIds.front() < 0 || sGeoIds.back() >= int(vals.size()))
        return -1;


    std::vector<Part::Geometry*> newVals(vals);
    for (auto it = sGeoIds.rbegin(); it != sGeoIds.rend(); ++it) {
        int GeoId = *it;
        newVals.erase(newVals.begin() + GeoId);

        // Find coincident points to replace the points of the deleted geometry
        std::vector<int> GeoIdList;
        std::vector<PointPos> PosIdList;
        for (PointPos PosId = PointPos::start; PosId != PointPos::mid;) {
            getDirectlyCoincidentPoints(GeoId, PosId, GeoIdList, PosIdList);
            if (GeoIdList.size() > 1) {
                delConstraintOnPoint(GeoId, PosId, true /* only coincidence */);
                transferConstraints(GeoIdList[0], PosIdList[0], GeoIdList[1], PosIdList[1]);
            }
            // loop through [start, end, mid]
            PosId = (PosId == PointPos::start) ? PointPos::end : PointPos::mid;
        }
    }

    // Copy the original constraints
    std::vector<Constraint*> constraints;
    for (const auto ptr : this->Constraints.getValues())
        constraints.push_back(ptr->clone());
    std::vector<Constraint*> filteredConstraints(0);
    for (auto itGeo = sGeoIds.rbegin(); itGeo != sGeoIds.rend(); ++itGeo) {
        int GeoId = *itGeo;
        for (std::vector<Constraint*>::const_iterator it = constraints.begin();
             it != constraints.end();
             ++it) {

            Constraint* copiedConstr(*it);
            if ((*it)->First != GeoId && (*it)->Second != GeoId && (*it)->Third != GeoId) {
                if (copiedConstr->First > GeoId)
                    copiedConstr->First -= 1;
                if (copiedConstr->Second > GeoId)
                    copiedConstr->Second -= 1;
                if (copiedConstr->Third > GeoId)
                    copiedConstr->Third -= 1;
                filteredConstraints.push_back(copiedConstr);
            }
            else {
                delete copiedConstr;
            }
        }

        constraints = filteredConstraints;
        filteredConstraints.clear();
    }

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);
        this->Geometry.setValues(newVals);
        this->Constraints.setValues(std::move(constraints));
    }
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::deleteAllGeometry()
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    std::vector<Part::Geometry*> newVals(0);
    std::vector<Constraint*> newConstraints(0);

    // Avoid unnecessary updates and checks as this is a transaction
    {
        Base::StateLocker lock(internaltransaction, true);
        this->Geometry.setValues(newVals);
        this->Constraints.setValues(newConstraints);
    }
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::toggleConstructions(const std::vector<int> &GeoIds)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    std::set<int> idSet(GeoIds.begin(),GeoIds.end());
    for(int GeoId : idSet) {
        if(GeoId >= Geometry.getSize() ||
           (GeoId < 0 && (GeoId > GeoEnum::RefExt || -GeoId-1 >= ExternalGeo.getSize())))
           return -1;
        if(getGeometryFacade(GeoId)->isInternalAligned())
            return -1;
    }

    bool geometryTouched = false;
    std::vector<Part::Geometry*> geos;
    bool externalTouched = false;
    std::vector<Part::Geometry*> extGeos;
    for(int GeoId : idSet) {
        if (GeoId >= 0) {
            if(geos.empty())
                geos = Geometry.getValues();
            auto &geo = geos[GeoId];
            geo = geo->clone();
            auto gf = GeometryFacade::getFacade(geo);
            gf->setConstruction(!gf->getConstruction());
            geometryTouched = true;
        } else {
            if(extGeos.empty())
                extGeos = ExternalGeo.getValues();
            auto &geo = extGeos[-GeoId-1];
            geo = geo->clone();
            auto egf = ExternalGeometryFacade::getFacade(geo);
            egf->setFlag(ExternalGeometryExtension::Defining,!egf->testFlag(ExternalGeometryExtension::Defining));
            externalTouched = true;
        }
    }

    // While it may seem that there is not a need to trigger an update at this time, because the
    // solver has its own copy of the geometry, and updateColors of the viewprovider may be
    // triggered by the clearselection of the UI command, this won't update the elements widget, in
    // the accumulative of actions it is judged that it is worth to trigger an update here.

    if(geometryTouched)
        Geometry.setValues(std::move(geos));
    if(externalTouched)
        ExternalGeo.setValues(std::move(extGeos));
    solverNeedsUpdate=true;
    return 0;
}

int SketchObject::setConstruction(int GeoId, bool on)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    Part::PropertyGeometryList *prop;
    int idx;
    if (GeoId >= 0) {
        prop = &Geometry;
        if (GeoId < Geometry.getSize())
            idx = GeoId;
        else
            return -1;
    }else if (GeoId <= GeoEnum::RefExt && -GeoId-1 < ExternalGeo.getSize()) {
        prop = &ExternalGeo;
        idx = -GeoId-1;
    }else
        return -1;

    if (getGeometryFacade(GeoId)->isInternalAligned())
        return -1;

    std::unique_ptr<Part::Geometry> geo(prop->getValues()[idx]->clone());
    if(prop == &Geometry)
        GeometryFacade::setConstruction(geo.get(), on);
    else {
        auto egf = ExternalGeometryFacade::getFacade(geo.get());
        egf->setFlag(ExternalGeometryExtension::Defining, on);
    }

    prop->set1Value(idx,std::move(geo));

    solverNeedsUpdate = true;
    return 0;
}

int SketchObject::exposeInternalGeometry(int GeoId)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return -1;

    const Part::Geometry* geo = getGeometry(GeoId);
    // Only for supported types
    if (geo->is<Part::GeomEllipse>()
        || geo->is<Part::GeomArcOfEllipse>()) {
        // First we search what has to be restored
        bool major = false;
        bool minor = false;
        bool focus1 = false;
        bool focus2 = false;

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
                switch ((*it)->AlignmentType) {
                    case Sketcher::EllipseMajorDiameter:
                        major = true;
                        break;
                    case Sketcher::EllipseMinorDiameter:
                        minor = true;
                        break;
                    case Sketcher::EllipseFocus1:
                        focus1 = true;
                        break;
                    case Sketcher::EllipseFocus2:
                        focus2 = true;
                        break;
                    default:
                        return -1;
                }
            }
        }

        int currentgeoid = getHighestCurveIndex();
        int incrgeo = 0;

        Base::Vector3d center;
        double majord;
        double minord;
        Base::Vector3d majdir;

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        if (geo->is<Part::GeomEllipse>()) {
            const Part::GeomEllipse* ellipse = static_cast<const Part::GeomEllipse*>(geo);

            center = ellipse->getCenter();
            majord = ellipse->getMajorRadius();
            minord = ellipse->getMinorRadius();
            majdir = ellipse->getMajorAxisDir();
        }
        else {
            const Part::GeomArcOfEllipse* aoe = static_cast<const Part::GeomArcOfEllipse*>(geo);

            center = aoe->getCenter();
            majord = aoe->getMajorRadius();
            minord = aoe->getMinorRadius();
            majdir = aoe->getMajorAxisDir();
        }

        Base::Vector3d mindir = Vector3d(-majdir.y, majdir.x);

        Base::Vector3d majorpositiveend = center + majord * majdir;
        Base::Vector3d majornegativeend = center - majord * majdir;
        Base::Vector3d minorpositiveend = center + minord * mindir;
        Base::Vector3d minornegativeend = center - minord * mindir;

        double df = sqrt(majord * majord - minord * minord);

        Base::Vector3d focus1P = center + df * majdir;
        Base::Vector3d focus2P = center - df * majdir;

        if (!major) {
            Part::GeomLineSegment* lmajor = new Part::GeomLineSegment();
            lmajor->setPoints(majorpositiveend, majornegativeend);

            igeo.push_back(lmajor);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseMajorDiameter;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!minor) {
            Part::GeomLineSegment* lminor = new Part::GeomLineSegment();
            lminor->setPoints(minorpositiveend, minornegativeend);

            igeo.push_back(lminor);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseMinorDiameter;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!focus1) {
            Part::GeomPoint* pf1 = new Part::GeomPoint();
            pf1->setPoint(focus1P);

            igeo.push_back(pf1);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseFocus1;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!focus2) {
            Part::GeomPoint* pf2 = new Part::GeomPoint();
            pf2->setPoint(focus2P);
            igeo.push_back(pf2);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = EllipseFocus2;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
        }

        this->addGeometry(igeo, true);
        this->addConstraints(icon);

        for (std::vector<Part::Geometry*>::iterator it = igeo.begin(); it != igeo.end(); ++it) {
            if (*it)
                delete *it;
        }

        for (std::vector<Constraint*>::iterator it = icon.begin(); it != icon.end(); ++it) {
            if (*it)
                delete *it;
        }

        icon.clear();
        igeo.clear();

        return incrgeo;// number of added elements
    }
    else if (geo->is<Part::GeomArcOfHyperbola>()) {
        // First we search what has to be restored
        bool major = false;
        bool minor = false;
        bool focus = false;

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
                switch ((*it)->AlignmentType) {
                    case Sketcher::HyperbolaMajor:
                        major = true;
                        break;
                    case Sketcher::HyperbolaMinor:
                        minor = true;
                        break;
                    case Sketcher::HyperbolaFocus:
                        focus = true;
                        break;
                    default:
                        return -1;
                }
            }
        }

        int currentgeoid = getHighestCurveIndex();
        int incrgeo = 0;

        const Part::GeomArcOfHyperbola* aoh = static_cast<const Part::GeomArcOfHyperbola*>(geo);

        Base::Vector3d center = aoh->getCenter();
        double majord = aoh->getMajorRadius();
        double minord = aoh->getMinorRadius();
        Base::Vector3d majdir = aoh->getMajorAxisDir();

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        Base::Vector3d mindir = Vector3d(-majdir.y, majdir.x);

        Base::Vector3d majorpositiveend = center + majord * majdir;
        Base::Vector3d majornegativeend = center - majord * majdir;
        Base::Vector3d minorpositiveend = majorpositiveend + minord * mindir;
        Base::Vector3d minornegativeend = majorpositiveend - minord * mindir;

        double df = sqrt(majord * majord + minord * minord);

        Base::Vector3d focus1P = center + df * majdir;

        if (!major) {
            Part::GeomLineSegment* lmajor = new Part::GeomLineSegment();
            lmajor->setPoints(majorpositiveend, majornegativeend);

            igeo.push_back(lmajor);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::HyperbolaMajor;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }
        if (!minor) {
            Part::GeomLineSegment* lminor = new Part::GeomLineSegment();
            lminor->setPoints(minorpositiveend, minornegativeend);

            igeo.push_back(lminor);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::HyperbolaMinor;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);

            incrgeo++;
        }
        if (!focus) {
            Part::GeomPoint* pf1 = new Part::GeomPoint();
            pf1->setPoint(focus1P);

            igeo.push_back(pf1);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::HyperbolaFocus;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }

        this->addGeometry(igeo, true);
        this->addConstraints(icon);

        for (std::vector<Part::Geometry*>::iterator it = igeo.begin(); it != igeo.end(); ++it)
            if (*it)
                delete *it;

        for (std::vector<Constraint*>::iterator it = icon.begin(); it != icon.end(); ++it)
            if (*it)
                delete *it;

        icon.clear();
        igeo.clear();

        return incrgeo;// number of added elements
    }
    else if (geo->is<Part::GeomArcOfParabola>()) {
        // First we search what has to be restored
        bool focus = false;
        bool focus_to_vertex = false;

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
                switch ((*it)->AlignmentType) {
                    case Sketcher::ParabolaFocus:
                        focus = true;
                        break;
                    case Sketcher::ParabolaFocalAxis:
                        focus_to_vertex = true;
                        break;
                    default:
                        return -1;
                }
            }
        }

        int currentgeoid = getHighestCurveIndex();
        int incrgeo = 0;

        const Part::GeomArcOfParabola* aop = static_cast<const Part::GeomArcOfParabola*>(geo);

        Base::Vector3d center = aop->getCenter();
        Base::Vector3d focusp = aop->getFocus();

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        if (!focus) {
            Part::GeomPoint* pf1 = new Part::GeomPoint();
            pf1->setPoint(focusp);

            igeo.push_back(pf1);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::ParabolaFocus;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::start;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);
            incrgeo++;
        }

        if (!focus_to_vertex) {
            Part::GeomLineSegment* paxis = new Part::GeomLineSegment();
            paxis->setPoints(center, focusp);

            igeo.push_back(paxis);

            Sketcher::Constraint* newConstr = new Sketcher::Constraint();
            newConstr->Type = Sketcher::InternalAlignment;
            newConstr->AlignmentType = Sketcher::ParabolaFocalAxis;
            newConstr->First = currentgeoid + incrgeo + 1;
            newConstr->FirstPos = Sketcher::PointPos::none;
            newConstr->Second = GeoId;

            icon.push_back(newConstr);

            incrgeo++;
        }

        this->addGeometry(igeo, true);
        this->addConstraints(icon);

        for (std::vector<Part::Geometry*>::iterator it = igeo.begin(); it != igeo.end(); ++it) {
            if (*it)
                delete *it;
        }

        for (std::vector<Constraint*>::iterator it = icon.begin(); it != icon.end(); ++it) {
            if (*it)
                delete *it;
        }

        icon.clear();
        igeo.clear();

        return incrgeo;// number of added elements
    }
    else if (geo->is<Part::GeomBSplineCurve>()) {

        const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);
        // First we search what has to be restored
        std::vector<bool> controlpoints(bsp->countPoles());
        std::vector<int> controlpointgeoids(bsp->countPoles());

        std::vector<bool> knotpoints(bsp->countKnots());
        std::vector<int> knotgeoids(bsp->countKnots());

        bool isfirstweightconstrained = false;

        std::vector<bool>::iterator itb;
        std::vector<int>::iterator it;

        for (it = controlpointgeoids.begin(), itb = controlpoints.begin();
             it != controlpointgeoids.end() && itb != controlpoints.end();
             ++it, ++itb) {
            (*it) = -1;
            (*itb) = false;
        }

        for (it = knotgeoids.begin(), itb = knotpoints.begin();
             it != knotgeoids.end() && itb != knotpoints.end();
             ++it, ++itb) {
            (*it) = -1;
            (*itb) = false;
        }

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        // search for existing poles
        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
                switch ((*it)->AlignmentType) {
                    case Sketcher::BSplineControlPoint:
                        controlpoints[(*it)->InternalAlignmentIndex] = true;
                        controlpointgeoids[(*it)->InternalAlignmentIndex] = (*it)->First;
                        break;
                    case Sketcher::BSplineKnotPoint:
                        knotpoints[(*it)->InternalAlignmentIndex] = true;
                        knotgeoids[(*it)->InternalAlignmentIndex] = (*it)->First;
                        break;
                    default:
                        return -1;
                }
            }
        }

        if (controlpoints[0]) {
            // search for first pole weight constraint
            for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin();
                 it != vals.end();
                 ++it) {
                if ((*it)->Type == Sketcher::Weight && (*it)->First == controlpointgeoids[0]) {
                    isfirstweightconstrained = true;
                }
            }
        }

        int currentgeoid = getHighestCurveIndex();
        int incrgeo = 0;

        std::vector<Part::Geometry*> igeo;
        std::vector<Constraint*> icon;

        std::vector<Base::Vector3d> poles = bsp->getPoles();
        std::vector<double> weights = bsp->getWeights();
        std::vector<double> knots = bsp->getKnots();

        double distance_p0_p1 = (poles[1] - poles[0]).Length();// for visual purposes only

        int index = 0;

        for (it = controlpointgeoids.begin(), itb = controlpoints.begin();
             it != controlpointgeoids.end() && itb != controlpoints.end();
             ++it, ++itb, index++) {

            if (!(*itb))// if controlpoint not existing
            {
                Part::GeomCircle* pc = new Part::GeomCircle();
                pc->setCenter(poles[index]);
                pc->setRadius(distance_p0_p1 / 6);

                igeo.push_back(pc);

                Sketcher::Constraint* newConstr = new Sketcher::Constraint();
                newConstr->Type = Sketcher::InternalAlignment;
                newConstr->AlignmentType = Sketcher::BSplineControlPoint;
                newConstr->First = currentgeoid + incrgeo + 1;
                newConstr->FirstPos = Sketcher::PointPos::mid;
                newConstr->Second = GeoId;
                newConstr->InternalAlignmentIndex = index;

                icon.push_back(newConstr);

                if (it != controlpointgeoids.begin()) {
                    if (isfirstweightconstrained && weights[0] == weights[index]) {
                        // if pole-weight newly created AND first weight is radius-constrained,
                        // AND these weights are equal, constrain them to be equal
                        Sketcher::Constraint* newConstr2 = new Sketcher::Constraint();
                        newConstr2->Type = Sketcher::Equal;
                        newConstr2->First = currentgeoid + incrgeo + 1;
                        newConstr2->FirstPos = Sketcher::PointPos::none;
                        newConstr2->Second = controlpointgeoids[0];
                        newConstr2->SecondPos = Sketcher::PointPos::none;

                        icon.push_back(newConstr2);
                    }
                }
                else {
                    controlpointgeoids[0] = currentgeoid + incrgeo + 1;
                    if (weights[0] == 1.0) {
                        // if the first weight is 1.0 it's probably going to be non-rational
                        Sketcher::Constraint* newConstr3 = new Sketcher::Constraint();
                        newConstr3->Type = Sketcher::Weight;
                        newConstr3->First = controlpointgeoids[0];
                        newConstr3->setValue(weights[0]);

                        icon.push_back(newConstr3);

                        isfirstweightconstrained = true;
                    }
                }
                incrgeo++;
            }
        }

        index = 0;

        for (it = knotgeoids.begin(), itb = knotpoints.begin();
             it != knotgeoids.end() && itb != knotpoints.end();
             ++it, ++itb, index++) {

            if (!(*itb))// if knot point not existing
            {
                Part::GeomPoint* kp = new Part::GeomPoint();

                kp->setPoint(bsp->pointAtParameter(knots[index]));

                igeo.push_back(kp);

                Sketcher::Constraint* newConstr = new Sketcher::Constraint();
                newConstr->Type = Sketcher::InternalAlignment;
                newConstr->AlignmentType = Sketcher::BSplineKnotPoint;
                newConstr->First = currentgeoid + incrgeo + 1;
                newConstr->FirstPos = Sketcher::PointPos::start;
                newConstr->Second = GeoId;
                newConstr->InternalAlignmentIndex = index;

                icon.push_back(newConstr);

                incrgeo++;
            }
        }

        Q_UNUSED(isfirstweightconstrained);

        this->addGeometry(igeo, true);
        this->addConstraints(icon);

        for (std::vector<Part::Geometry*>::iterator it = igeo.begin(); it != igeo.end(); ++it)
            if (*it)
                delete *it;

        for (std::vector<Constraint*>::iterator it = icon.begin(); it != icon.end(); ++it)
            if (*it)
                delete *it;

        icon.clear();
        igeo.clear();

        return incrgeo;// number of added elements
    }
    else
        return -1;// not supported type
}

int SketchObject::deleteUnusedInternalGeometry(int GeoId, bool delgeoid)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return -1;

    const Part::Geometry* geo = getGeometry(GeoId);
    // Only for supported types
    if (geo->is<Part::GeomEllipse>()
        || geo->is<Part::GeomArcOfEllipse>()
        || geo->is<Part::GeomArcOfHyperbola>()) {

        int majorelementindex = -1;
        int minorelementindex = -1;
        int focus1elementindex = -1;
        int focus2elementindex = -1;

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
                switch ((*it)->AlignmentType) {
                    case Sketcher::EllipseMajorDiameter:
                    case Sketcher::HyperbolaMajor:
                        majorelementindex = (*it)->First;
                        break;
                    case Sketcher::EllipseMinorDiameter:
                    case Sketcher::HyperbolaMinor:
                        minorelementindex = (*it)->First;
                        break;
                    case Sketcher::EllipseFocus1:
                    case Sketcher::HyperbolaFocus:
                        focus1elementindex = (*it)->First;
                        break;
                    case Sketcher::EllipseFocus2:
                        focus2elementindex = (*it)->First;
                        break;
                    default:
                        return -1;
                }
            }
        }

        // Hide unused geometry here
        int majorconstraints = 0;// number of constraints associated to the geoid of the major axis
        int minorconstraints = 0;
        int focus1constraints = 0;
        int focus2constraints = 0;

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {

            if ((*it)->Second == majorelementindex || (*it)->First == majorelementindex
                || (*it)->Third == majorelementindex)
                majorconstraints++;
            else if ((*it)->Second == minorelementindex || (*it)->First == minorelementindex
                     || (*it)->Third == minorelementindex)
                minorconstraints++;
            else if ((*it)->Second == focus1elementindex || (*it)->First == focus1elementindex
                     || (*it)->Third == focus1elementindex)
                focus1constraints++;
            else if ((*it)->Second == focus2elementindex || (*it)->First == focus2elementindex
                     || (*it)->Third == focus2elementindex)
                focus2constraints++;
        }

        std::vector<int> delgeometries;

        // those with less than 2 constraints must be removed
        if (focus2constraints < 2)
            delgeometries.push_back(focus2elementindex);

        if (focus1constraints < 2)
            delgeometries.push_back(focus1elementindex);

        if (minorconstraints < 2)
            delgeometries.push_back(minorelementindex);

        if (majorconstraints < 2)
            delgeometries.push_back(majorelementindex);

        if (delgeoid)
            delgeometries.push_back(GeoId);

        // indices over an erased element get automatically updated!!
        std::sort(delgeometries.begin(), delgeometries.end());

        if (!delgeometries.empty()) {
            for (std::vector<int>::reverse_iterator it = delgeometries.rbegin();
                 it != delgeometries.rend();
                 ++it) {
                delGeometry(*it, false);
            }
        }

        int ndeleted = delgeometries.size();

        delgeometries.clear();

        return ndeleted;// number of deleted elements
    }
    else if (geo->is<Part::GeomArcOfParabola>()) {
        // if the focus-to-vertex line is constrained, then never delete the focus
        // if the line is unconstrained, then the line may be deleted,
        // in this case the focus may be deleted if unconstrained.
        int majorelementindex = -1;
        int focus1elementindex = -1;

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
                switch ((*it)->AlignmentType) {
                    case Sketcher::ParabolaFocus:
                        focus1elementindex = (*it)->First;
                        break;
                    case Sketcher::ParabolaFocalAxis:
                        majorelementindex = (*it)->First;
                        break;
                    default:
                        return -1;
                }
            }
        }

        // Hide unused geometry here
        // number of constraints associated to the geoid of the major axis other than the coincident
        // ones
        int majorconstraints = 0;
        int focus1constraints = 0;

        for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
             ++it) {
            if ((*it)->Second == majorelementindex || (*it)->First == majorelementindex
                || (*it)->Third == majorelementindex)
                majorconstraints++;
            else if ((*it)->Second == focus1elementindex || (*it)->First == focus1elementindex
                     || (*it)->Third == focus1elementindex)
                focus1constraints++;
        }

        std::vector<int> delgeometries;

        // major has minimum one constraint, the specific internal alignment constraint
        if (majorelementindex != -1 && majorconstraints < 2)
            delgeometries.push_back(majorelementindex);

        // focus has minimum one constraint now, the specific internal alignment constraint
        if (focus1elementindex != -1 && focus1constraints < 2)
            delgeometries.push_back(focus1elementindex);

        if (delgeoid)
            delgeometries.push_back(GeoId);

        // indices over an erased element get automatically updated!!
        std::sort(delgeometries.begin(), delgeometries.end());

        if (!delgeometries.empty()) {
            for (std::vector<int>::reverse_iterator it = delgeometries.rbegin();
                 it != delgeometries.rend();
                 ++it) {
                delGeometry(*it, false);
            }
        }

        int ndeleted = delgeometries.size();

        delgeometries.clear();

        return ndeleted;// number of deleted elements
    }
    else if (geo->is<Part::GeomBSplineCurve>()) {

        const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

        // First we search existing IA
        std::vector<int> controlpointgeoids(bsp->countPoles());
        std::vector<int> cpassociatedconstraints(bsp->countPoles());

        std::vector<int> knotgeoids(bsp->countKnots());
        std::vector<int> kassociatedconstraints(bsp->countKnots());

        std::vector<int>::iterator it;
        std::vector<int>::iterator ita;

        for (it = controlpointgeoids.begin(), ita = cpassociatedconstraints.begin();
             it != controlpointgeoids.end() && ita != cpassociatedconstraints.end();
             ++it, ++ita) {
            (*it) = -1;
            (*ita) = 0;
        }

        for (it = knotgeoids.begin(), ita = kassociatedconstraints.begin();
             it != knotgeoids.end() && ita != kassociatedconstraints.end();
             ++it, ++ita) {
            (*it) = -1;
            (*ita) = 0;
        }

        const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

        // search for existing poles
        for (std::vector<Sketcher::Constraint*>::const_iterator jt = vals.begin(); jt != vals.end();
             ++jt) {
            if ((*jt)->Type == Sketcher::InternalAlignment && (*jt)->Second == GeoId) {
                switch ((*jt)->AlignmentType) {
                    case Sketcher::BSplineControlPoint:
                        controlpointgeoids[(*jt)->InternalAlignmentIndex] = (*jt)->First;
                        break;
                    case Sketcher::BSplineKnotPoint:
                        knotgeoids[(*jt)->InternalAlignmentIndex] = (*jt)->First;
                        break;
                    default:
                        return -1;
                }
            }
        }

        std::vector<int> delgeometries;

        for (it = controlpointgeoids.begin(), ita = cpassociatedconstraints.begin();
             it != controlpointgeoids.end() && ita != cpassociatedconstraints.end();
             ++it, ++ita) {
            if ((*it) != -1) {
                // look for a circle at geoid index
                for (std::vector<Sketcher::Constraint*>::const_iterator itc = vals.begin();
                     itc != vals.end();
                     ++itc) {

                    if ((*itc)->Type == Sketcher::Equal) {
                        bool f = false, s = false;
                        for (std::vector<int>::iterator its = controlpointgeoids.begin();
                             its != controlpointgeoids.end();
                             ++its) {
                            if ((*itc)->First == *its) {
                                f = true;
                            }
                            else if ((*itc)->Second == *its) {
                                s = true;
                            }

                            if (f && s) {// the equality constraint is not interpole
                                break;
                            }
                        }

                        // the equality constraint constraints a pole but it is not interpole
                        if (f != s) {
                            (*ita)++;
                        }
                    }
                    // We do not ignore weight constraints as we did with radius constraints,
                    // because the radius magnitude no longer makes sense without the B-Spline.
                }

                if ((*ita) < 2) {// IA
                    delgeometries.push_back((*it));
                }
            }
        }

        for (it = knotgeoids.begin(), ita = kassociatedconstraints.begin();
             it != knotgeoids.end() && ita != kassociatedconstraints.end();
             ++it, ++ita) {
            if ((*it) != -1) {
                // look for a point at geoid index
                for (std::vector<Sketcher::Constraint*>::const_iterator itc = vals.begin();
                     itc != vals.end();
                     ++itc) {
                    if ((*itc)->Second == (*it) || (*itc)->First == (*it)
                        || (*itc)->Third == (*it)) {
                        (*ita)++;
                    }
                }

                if ((*ita) < 2) {// IA
                    delgeometries.push_back((*it));
                }
            }
        }


        if (delgeoid)
            delgeometries.push_back(GeoId);

        int ndeleted = delGeometriesExclusiveList(delgeometries);

        return ndeleted;// number of deleted elements
    }
    else {
        return -1;// not supported type
    }
}

const Part::Geometry* SketchObject::_getGeometry(int GeoId) const
{
    if (GeoId >= 0) {
        const std::vector<Part::Geometry *> &geomlist = getInternalGeometry();
        if (GeoId < int(geomlist.size()))
            return geomlist[GeoId];
    }
    else if (GeoId < 0 && -GeoId-1 < ExternalGeo.getSize())
        return ExternalGeo[-GeoId-1];

    return nullptr;
}

int SketchObject::getCompleteGeometryIndex(int GeoId) const
{
    if (GeoId >= 0) {
        if (GeoId < int(Geometry.getSize()))
            return GeoId;
    }
    else if (-GeoId <= int(ExternalGeo.getSize()))
        return -GeoId - 1;

    return GeoEnum::GeoUndef;
}

int SketchObject::getGeoIdFromCompleteGeometryIndex(int completeGeometryIndex) const
{
    int completeGeometryCount = int(Geometry.getSize() + ExternalGeo.getSize());

    if (completeGeometryIndex < 0 || completeGeometryIndex >= completeGeometryCount)
        return GeoEnum::GeoUndef;

    if (completeGeometryIndex < Geometry.getSize())
        return completeGeometryIndex;
    else
        return (completeGeometryIndex - completeGeometryCount);
}

std::unique_ptr<const GeometryFacade> SketchObject::getGeometryFacade(int GeoId) const
{
    return GeometryFacade::getFacade(getGeometry(GeoId));
}

std::vector<Part::Geometry*> SketchObject::getCompleteGeometry() const
{
    std::vector<Part::Geometry*> vals = getInternalGeometry();
    const auto &geos = getExternalGeometry();
    vals.insert(vals.end(), geos.rbegin(), geos.rend()); // in reverse order
    return vals;
}

GeoListFacade SketchObject::getGeoListFacade() const
{
    std::vector<GeometryFacadeUniquePtr> facade;
    facade.reserve(Geometry.getSize() + ExternalGeo.getSize());

    for (auto geo : Geometry.getValues())
        facade.push_back(GeometryFacade::getFacade(geo));

    const auto &externalGeos = ExternalGeo.getValues();
    for(auto rit = externalGeos.rbegin(); rit != externalGeos.rend(); rit++)
        facade.push_back(GeometryFacade::getFacade(*rit));

    return GeoListFacade::getGeoListModel(std::move(facade), Geometry.getSize());
}

void SketchObject::rebuildVertexIndex()
{
    GeoPos2VertexId.clear();
    VertexId2GeoId.resize(0);
    VertexId2PosId.resize(0);
    int imax = getHighestCurveIndex();
    int i = 0;
    const std::vector<Part::Geometry*> geometry = getCompleteGeometry();
    if (geometry.size() <= 2)
        return;
    for (std::vector<Part::Geometry*>::const_iterator it = geometry.begin();
         it != geometry.end() - 2;
         ++it, i++) {
        if (i > imax)
            i = -getExternalGeometryCount();
        if ((*it)->is<Part::GeomPoint>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
        }
        else if ((*it)->is<Part::GeomLineSegment>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
            GeoPos2VertexId[std::make_pair(i,PointPos::end)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::end);
        }
        else if ((*it)->is<Part::GeomCircle>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::mid)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::mid);
        }
        else if ((*it)->is<Part::GeomEllipse>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::mid)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::mid);
        } 
        else if ((*it)->is<Part::GeomArcOfCircle>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
            GeoPos2VertexId[std::make_pair(i,PointPos::end)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::end);
            GeoPos2VertexId[std::make_pair(i,PointPos::mid)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::mid);
        }
        else if ((*it)->is<Part::GeomArcOfEllipse>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
            GeoPos2VertexId[std::make_pair(i,PointPos::end)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::end);
            GeoPos2VertexId[std::make_pair(i,PointPos::mid)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::mid);
        }
        else if ((*it)->is<Part::GeomArcOfHyperbola>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
            GeoPos2VertexId[std::make_pair(i,PointPos::end)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::end);
            GeoPos2VertexId[std::make_pair(i,PointPos::mid)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::mid);
        }
        else if ((*it)->is<Part::GeomArcOfParabola>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
            GeoPos2VertexId[std::make_pair(i,PointPos::end)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::end);
            GeoPos2VertexId[std::make_pair(i,PointPos::mid)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::mid);
        }
        else if ((*it)->is<Part::GeomBSplineCurve>()) {
            GeoPos2VertexId[std::make_pair(i,PointPos::start)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::start);
            GeoPos2VertexId[std::make_pair(i,PointPos::end)] = VertexId2GeoId.size();
            VertexId2GeoId.push_back(i);
            VertexId2PosId.push_back(PointPos::end);
        }
    }
}

void SketchObject::getGeometryWithDependentParameters(
    std::vector<std::pair<int, PointPos>>& geometrymap)
{
    auto geos = getInternalGeometry();

    int geoid = 0;

    for (auto geo : geos) {
        if (geo) {
            if (geo->hasExtension(Sketcher::SolverGeometryExtension::getClassTypeId())) {

                auto solvext = std::static_pointer_cast<const Sketcher::SolverGeometryExtension>(
                    geo->getExtension(Sketcher::SolverGeometryExtension::getClassTypeId()).lock());

                if (solvext->getGeometry()
                    == Sketcher::SolverGeometryExtension::NotFullyConstraint) {
                    // The solver differentiates whether the parameters that are dependent are not
                    // those of start, end, mid, and assigns them to the edge (edge params = curve
                    // params - parms of start, end, mid). The user looking at the UI expects that
                    // the edge of a NotFullyConstraint geometry will always move, even if the edge
                    // parameters are independent, for example if mid is the only dependent
                    // parameter. In other words, the user could reasonably restrict the edge to
                    // reach a fully constrained element. Under this understanding, the edge
                    // parameter would always be dependent, unless the element is fully constrained.
                    //
                    // While this is ok from a user visual expectation point of view, it leads to a
                    // loss of information of whether restricting the point start, end, mid that is
                    // dependent may suffice, or even if such points are restricted, the edge would
                    // still need to be restricted.
                    //
                    // Because Python gets the information in this function, it would lead to Python
                    // users having access to a lower amount of detail.
                    //
                    // For this reason, this function returns edge as dependent parameter if and
                    // only if constraining the parameters of the points would not suffice to
                    // constraint the element.
                    if (solvext->getEdge() == SolverGeometryExtension::Dependent)
                        geometrymap.emplace_back(geoid, Sketcher::PointPos::none);
                    if (solvext->getStart() == SolverGeometryExtension::Dependent)
                        geometrymap.emplace_back(geoid, Sketcher::PointPos::start);
                    if (solvext->getEnd() == SolverGeometryExtension::Dependent)
                        geometrymap.emplace_back(geoid, Sketcher::PointPos::start);
                    if (solvext->getMid() == SolverGeometryExtension::Dependent)
                        geometrymap.emplace_back(geoid, Sketcher::PointPos::start);
                }
            }
        }

        geoid++;
    }
}

void SketchObject::getGeoVertexIndex(int VertexId, int& GeoId, PointPos& PosId) const
{
    if (VertexId < 0 || VertexId >= int(VertexId2GeoId.size())) {
        GeoId = GeoEnum::GeoUndef;
        PosId = PointPos::none;
        return;
    }
    GeoId = VertexId2GeoId[VertexId];
    PosId = VertexId2PosId[VertexId];
}

int SketchObject::getVertexIndexGeoPos(int GeoId, PointPos PosId) const
{
    auto it = GeoPos2VertexId.find(std::make_pair(GeoId,PosId));
    if(it != GeoPos2VertexId.end())
        return (int)it->second;
    return -1;
}

Part::TopoShape SketchObject::getEdge(const Part::Geometry *geo, const char *name) const {
    Part::TopoShape shape(geo->toShape());
    shape.setElementName(Data::IndexedName::fromConst("Edge", 1),
                         Data::MappedName::fromRawData(name));
    TopTools_IndexedMapOfShape vmap;
    TopExp::MapShapes(shape.getShape(), TopAbs_VERTEX, vmap);
    std::ostringstream ss;
    for(int i=1;i<=vmap.Extent();++i) {
        auto gpt = BRep_Tool::Pnt(TopoDS::Vertex(vmap(i)));
        Base::Vector3d pt(gpt.X(),gpt.Y(),gpt.Z());
        PointPos pos[] = {PointPos::start,PointPos::end};
        for(size_t j=0;j<sizeof(pos)/sizeof(pos[0]);++j) {
            if(getPoint(geo,pos[j]) == pt) {
                ss.str("");
                ss << name << 'v' << static_cast<int>(pos[j]);
                shape.setElementName(Data::IndexedName::fromConst("Vertex", i),
                                     Data::MappedName::fromRawData(ss.str().c_str()));
                break;
            }
        }
    }
    return shape;
}

Data::IndexedName SketchObject::shapeTypeFromGeoId(int geoId, PointPos posId) const {
    if(geoId == GeoEnum::HAxis) {
        if(posId == PointPos::start)
            return Data::IndexedName::fromConst("RootPoint", 0);
        return Data::IndexedName::fromConst("H_Axis", 0);
    }else if(geoId == GeoEnum::VAxis)
        return Data::IndexedName::fromConst("V_Axis", 0);

    if (posId == PointPos::none) {
        auto geo = getGeometry(geoId);
        if (geo && geo->isDerivedFrom(Part::GeomPoint::getClassTypeId()))
            posId = PointPos::start;
    }
    if(posId != PointPos::none) {
        int idx = getVertexIndexGeoPos(geoId, posId);
        if(idx < 0)
            return Data::IndexedName();
        return Data::IndexedName::fromConst("Vertex", idx+1);
    }
    if(geoId >= 0)
        return Data::IndexedName::fromConst("Edge", geoId+1);
    else
        return Data::IndexedName::fromConst("ExternalEdge", -geoId-2);
}

bool SketchObject::geoIdFromShapeType(const char *shapetype,
                                      int &geoId,
                                      PointPos &posId) const
{
    return geoIdFromShapeType(checkSubName(shapetype), geoId, posId);
}

bool SketchObject::geoIdFromShapeType(const Data::IndexedName & indexedName,
                                      int &geoId,
                                      PointPos &posId) const
{
    posId = PointPos::none;
    if (!indexedName)
        return false;
    const char *shapetype = indexedName.getType();
    if (boost::equals(shapetype,"Edge")
        || boost::equals(shapetype,"edge")) {
        geoId = indexedName.getIndex() - 1;
    } else if (boost::equals(shapetype,"ExternalEdge")) {
        geoId = indexedName.getIndex() - 1;
        geoId = -geoId - 3;
    } else if (boost::equals(shapetype,"Vertex") ||
               boost::equals(shapetype,"vertex")) {
        int VtId = indexedName.getIndex() - 1;
        getGeoVertexIndex(VtId,geoId,posId);
        if (posId==PointPos::none) return false;
    } else if (boost::equals(shapetype,"H_Axis")) {
        geoId = Sketcher::GeoEnum::HAxis;
    } else if (boost::equals(shapetype,"V_Axis")) {
        geoId = Sketcher::GeoEnum::VAxis;
    } else if (boost::equals(shapetype,"RootPoint")) {
        geoId = Sketcher::GeoEnum::RtPnt;
        posId = PointPos::start;
    } else
        return false;
    return true;
}

int SketchObject::setGeometryId(int GeoId, long id)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId >= int(Geometry.getValues().size()))
        return -1;

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();


    std::vector<Part::Geometry*> newVals(vals);

    // deep copy
    for (size_t i = 0; i < newVals.size(); i++) {
        newVals[i] = newVals[i]->clone();

        if ((int)i == GeoId) {
            auto gf = GeometryFacade::getFacade(newVals[i]);

            gf->setId(id);
        }
    }

    // There is not actual internal transaction going on here, however neither the geometry indices
    // nor the vertices need to be updated so this is a convenient way of preventing it.
    {
        Base::StateLocker lock(internaltransaction, true);
        this->Geometry.setValues(std::move(newVals));
    }

    return 0;
}

int SketchObject::getGeometryId(int GeoId, long& id) const
{
    if (GeoId < 0 || GeoId >= int(Geometry.getValues().size()))
        return -1;

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    auto gf = GeometryFacade::getFacade(vals[GeoId]);

    id = gf->getId();

    return 0;
}

// clang-format on
