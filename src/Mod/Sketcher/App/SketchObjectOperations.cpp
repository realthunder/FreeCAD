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

SketchSolveStatus SketchObject::movePoint(int GeoId, PointPos PosId, const Base::Vector3d& toPoint,
                                          bool relative, bool updateGeoBeforeMoving)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // if we are moving a point at SketchObject level, we need to start from a solved sketch
    // if we have conflicts we can forget about moving. However, there is the possibility that we
    // need to do programmatically moves of new geometry that has not been solved yet and that
    // because they were programmatically generated won't generate a conflict. This is the case of
    // Fillet for example. This is why exceptionally, it may be required to update the sketch
    // geometry to that of of SketchObject upon moving. => use updateGeometry parameter = true then


    if (updateGeoBeforeMoving || solverNeedsUpdate) {
        lastDoF = solvedSketch.setUpSketch(
            getCompleteGeometry(), Constraints.getValues(), getExternalGeometryCount());

        retrieveSolverDiagnostics();

        solverNeedsUpdate = false;
    }

    if (lastDoF < 0)// over-constrained sketch
        return SketchSolveStatus::SolverError;
    if (lastHasConflict)// conflicting constraints
        return SketchSolveStatus::SolverError;

    // move the point and solve
    lastSolverStatus = solvedSketch.moveGeometry(GeoId, PosId, toPoint, relative);

    // moving the point can not result in a conflict that we did not have
    // or a redundancy that we did not have before, or a change of DoF

    if (lastSolverStatus == GCS::SolveStatus::Success) {
        std::vector<Part::Geometry*> geomlist = solvedSketch.extractGeometry();
        Geometry.setValues(geomlist);
        // Constraints.acceptGeometry(getCompleteGeometry());
        for (std::vector<Part::Geometry*>::iterator it = geomlist.begin(); it != geomlist.end();
             ++it) {
            if (*it)
                delete *it;
        }
    }

    solvedSketch.resetInitMove();// reset solver point moving mechanism

    return lastSolverStatus == GCS::SolveStatus::Success ? SketchSolveStatus::Success
                                                         : SketchSolveStatus::SolverError;
}

int SketchObject::delGeometries(const std::vector<int>& GeoIds)
{
    std::vector<int> sGeoIds(GeoIds);

    // if a GeoId has internal geometry, it must delete internal geometries too
    for (auto c : Constraints.getValues()) {
        if (c->Type == InternalAlignment) {
            auto pos = std::find(sGeoIds.begin(), sGeoIds.end(), c->Second);

            if (pos != sGeoIds.end()) {
                sGeoIds.push_back(c->First);
            }
        }
    }

    std::sort(sGeoIds.begin(), sGeoIds.end());
    // eliminate duplicates
    auto newend = std::unique(sGeoIds.begin(), sGeoIds.end());
    sGeoIds.resize(std::distance(sGeoIds.begin(), newend));

    return delGeometriesExclusiveList(sGeoIds);
}

void SketchObject::transferFilletConstraints(int geoId1, PointPos posId1, int geoId2,
                                             PointPos posId2)
{
    // If the lines don't intersect, there's no original corner to work with so
    // don't try to transfer the constraints. But we should delete line length and equal
    // constraints and constraints on the affected endpoints because they're about
    // to move unpredictably.
    if (!arePointsCoincident(geoId1, posId1, geoId2, posId2)) {
        // Delete constraints on the endpoints
        delConstraintOnPoint(geoId1, posId1, false);
        delConstraintOnPoint(geoId2, posId2, false);

        // Delete line length and equal constraints
        const std::vector<Constraint*>& constraints = this->Constraints.getValues();
        std::vector<int> deleteme;
        for (int i = 0; i < int(constraints.size()); i++) {
            const Constraint* c = constraints[i];
            if (c->Type == Sketcher::Distance || c->Type == Sketcher::Equal) {
                bool line1 = c->First == geoId1 && c->FirstPos == PointPos::none;
                bool line2 = c->First == geoId2 && c->FirstPos == PointPos::none;
                if (line1 || line2) {
                    deleteme.push_back(i);
                }
            }
        }
        delConstraints(std::move(deleteme), false);
        return;
    }

    // If the lines aren't straight, don't try to transfer the constraints.
    // TODO: Add support for curved lines.
    const Part::Geometry* geo1 = getGeometry(geoId1);
    const Part::Geometry* geo2 = getGeometry(geoId2);
    if (geo1->getTypeId() != Part::GeomLineSegment::getClassTypeId()
        || geo2->getTypeId() != Part::GeomLineSegment::getClassTypeId()) {
        delConstraintOnPoint(geoId1, posId1, false);
        delConstraintOnPoint(geoId2, posId2, false);
        return;
    }

    // Add a vertex to preserve the original intersection of the filleted lines
    Part::GeomPoint* originalCorner = new Part::GeomPoint(getPoint(geoId1, posId1));
    int originalCornerId = addGeometry(originalCorner);
    delete originalCorner;

    // Constrain the vertex to the two lines
    Sketcher::Constraint* cornerToLine1 = new Sketcher::Constraint();
    cornerToLine1->Type = Sketcher::PointOnObject;
    cornerToLine1->First = originalCornerId;
    cornerToLine1->FirstPos = PointPos::start;
    cornerToLine1->Second = geoId1;
    cornerToLine1->SecondPos = PointPos::none;
    addConstraint(cornerToLine1);
    delete cornerToLine1;
    Sketcher::Constraint* cornerToLine2 = new Sketcher::Constraint();
    cornerToLine2->Type = Sketcher::PointOnObject;
    cornerToLine2->First = originalCornerId;
    cornerToLine2->FirstPos = PointPos::start;
    cornerToLine2->Second = geoId2;
    cornerToLine2->SecondPos = PointPos::none;
    addConstraint(cornerToLine2);
    delete cornerToLine2;

    Base::StateLocker lock(managedoperation, true);

    // Loop through all the constraints and try to do reasonable things with the affected ones
    std::vector<Constraint*> newConstraints;
    for (auto c : this->Constraints.getValues()) {
        // Keep track of whether the affected lines and endpoints appear in this constraint
        bool point1First = c->First == geoId1 && c->FirstPos == posId1;
        bool point2First = c->First == geoId2 && c->FirstPos == posId2;
        bool point1Second = c->Second == geoId1 && c->SecondPos == posId1;
        bool point2Second = c->Second == geoId2 && c->SecondPos == posId2;
        bool point1Third = c->Third == geoId1 && c->ThirdPos == posId1;
        bool point2Third = c->Third == geoId2 && c->ThirdPos == posId2;
        bool line1First = c->First == geoId1 && c->FirstPos == PointPos::none;
        bool line2First = c->First == geoId2 && c->FirstPos == PointPos::none;
        bool line1Second = c->Second == geoId1 && c->SecondPos == PointPos::none;
        bool line2Second = c->Second == geoId2 && c->SecondPos == PointPos::none;

        if (c->Type == Sketcher::Coincident) {
            if ((point1First && point2Second) || (point2First && point1Second)) {
                // This is the constraint holding the two edges together that are about to be
                // filleted.  This constraint goes away because the edges will touch the fillet
                // instead.
                continue;
            }
            if (point1First || point2First) {
                // Move the coincident constraint to the new corner point
                c->First = originalCornerId;
                c->FirstPos = PointPos::start;
            }
            if (point1Second || point2Second) {
                // Move the coincident constraint to the new corner point
                c->Second = originalCornerId;
                c->SecondPos = PointPos::start;
            }
        }
        else if (c->Type == Sketcher::Horizontal || c->Type == Sketcher::Vertical) {
            // Point-to-point horizontal or vertical constraint, move to new corner point
            if (point1First || point2First) {
                c->First = originalCornerId;
                c->FirstPos = PointPos::start;
            }
            if (point1Second || point2Second) {
                c->Second = originalCornerId;
                c->SecondPos = PointPos::start;
            }
        }
        else if (c->Type == Sketcher::Distance || c->Type == Sketcher::DistanceX
                 || c->Type == Sketcher::DistanceY) {
            // Point-to-point distance constraint.  Move it to the new corner point
            if (point1First || point2First) {
                c->First = originalCornerId;
                c->FirstPos = PointPos::start;
            }
            if (point1Second || point2Second) {
                c->Second = originalCornerId;
                c->SecondPos = PointPos::start;
            }

            // Distance constraint on the line itself. Change it to point-point between the far end
            // of the line and the new corner
            if (line1First) {
                c->FirstPos = (posId1 == PointPos::start) ? PointPos::end : PointPos::start;
                c->Second = originalCornerId;
                c->SecondPos = PointPos::start;
            }
            if (line2First) {
                c->FirstPos = (posId2 == PointPos::start) ? PointPos::end : PointPos::start;
                c->Second = originalCornerId;
                c->SecondPos = PointPos::start;
            }
        }
        else if (c->Type == Sketcher::PointOnObject) {
            // The corner to be filleted was touching some other object.
            if (point1First || point2First) {
                c->First = originalCornerId;
                c->FirstPos = PointPos::start;
            }
        }
        else if (c->Type == Sketcher::Equal) {
            // Equal length constraints are dicey because the lines are getting shorter.  Safer to
            // delete them and let the user notice the underconstraint.
            if (line1First || line2First || line1Second || line2Second) {
                continue;
            }
        }
        else if (c->Type == Sketcher::Symmetric) {
            // Symmetries should probably be preserved relative to the original corner
            if (point1First || point2First) {
                c->First = originalCornerId;
                c->FirstPos = PointPos::start;
            }
            else if (point1Second || point2Second) {
                c->Second = originalCornerId;
                c->SecondPos = PointPos::start;
            }
            else if (point1Third || point2Third) {
                c->Third = originalCornerId;
                c->ThirdPos = PointPos::start;
            }
        }
        else if (c->Type == Sketcher::SnellsLaw) {
            // Can't imagine any cases where you'd fillet a vertex going through a lens, so let's
            // delete to be safe.
            continue;
        }
        else if (point1First || point2First || point1Second || point2Second || point1Third
                 || point2Third) {
            // Delete any other point-based constraints on the relevant points
            continue;
        }

        // Default: keep all other constraints
        newConstraints.push_back(c->clone());
    }
    this->Constraints.setValues(std::move(newConstraints));
}

int SketchObject::fillet(int GeoId, PointPos PosId, double radius, bool trim, bool createCorner, bool chamfer)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return -1;

    // Find the other geometry Id associated with the coincident point
    std::vector<int> GeoIdList;
    std::vector<PointPos> PosIdList;
    getDirectlyCoincidentPoints(GeoId, PosId, GeoIdList, PosIdList);

    // only coincident points between two (non-external) edges can be filleted
    if (GeoIdList.size() == 2 && GeoIdList[0] >= 0 && GeoIdList[1] >= 0) {
        const Part::Geometry* geo1 = getGeometry(GeoIdList[0]);
        const Part::Geometry* geo2 = getGeometry(GeoIdList[1]);
        if (geo1->is<Part::GeomLineSegment>()
            && geo2->is<Part::GeomLineSegment>()) {
            auto* lineSeg1 = static_cast<const Part::GeomLineSegment*>(geo1);
            auto* lineSeg2 = static_cast<const Part::GeomLineSegment*>(geo2);

            Base::Vector3d midPnt1 = (lineSeg1->getStartPoint() + lineSeg1->getEndPoint()) / 2;
            Base::Vector3d midPnt2 = (lineSeg2->getStartPoint() + lineSeg2->getEndPoint()) / 2;
            return fillet(GeoIdList[0], GeoIdList[1], midPnt1, midPnt2, radius, trim, createCorner, chamfer);
        }
    }

    return -1;
}

int SketchObject::fillet(int GeoId1, int GeoId2, const Base::Vector3d& refPnt1,
                         const Base::Vector3d& refPnt2, double radius, bool trim, bool createCorner, bool chamfer)
{
    if (GeoId1 < 0 || GeoId1 > getHighestCurveIndex() || GeoId2 < 0 || GeoId2 > getHighestCurveIndex()) {
        return -1;
    }

    // If either of the two input lines are locked, don't try to trim since it won't work anyway
    const Part::Geometry* geo1 = getGeometry(GeoId1);
    const Part::Geometry* geo2 = getGeometry(GeoId2);
    if (trim && (GeometryFacade::getBlocked(geo1) || GeometryFacade::getBlocked(geo2))) {
        trim = false;
    }

    Base::Vector3d p1, p2;
    PointPos PosId1 = PointPos::none;
    PointPos PosId2 = PointPos::none;
    int filletId;

    if (geo1->is<Part::GeomLineSegment>() && geo2->is<Part::GeomLineSegment>()) {
        auto* lineSeg1 = static_cast<const Part::GeomLineSegment*>(geo1);
        auto* lineSeg2 = static_cast<const Part::GeomLineSegment*>(geo2);

        Base::Vector3d filletCenter;
        if (!Part::findFilletCenter(lineSeg1, lineSeg2, radius, refPnt1, refPnt2, filletCenter)) {
            return -1;
        }
        Base::Vector3d dir1 = lineSeg1->getEndPoint() - lineSeg1->getStartPoint();
        Base::Vector3d dir2 = lineSeg2->getEndPoint() - lineSeg2->getStartPoint();

        // the intersection point will and two distances will be necessary later for trimming the
        // lines
        Base::Vector3d intersection, dist1, dist2;

        // create arc from known parameters and lines
        std::unique_ptr<Part::GeomArcOfCircle> arc(
            Part::create2LinesFilletGeometry(lineSeg1, lineSeg2, filletCenter, radius));
        if (!arc) {
            return -1;
        }

        // calculate intersection and distances before we invalidate lineSeg1 and lineSeg2
        if (!find2DLinesIntersection(lineSeg1, lineSeg2, intersection)) {
            return -1;
        }

        p1 = arc->getStartPoint();
        p2 = arc->getEndPoint();

        dist1.ProjectToLine(arc->getStartPoint(/*emulateCCW=*/true) - intersection, dir1);
        dist2.ProjectToLine(arc->getStartPoint(/*emulateCCW=*/true) - intersection, dir2);
        filletId = addGeometry(arc.get());

        if (trim) {
            PosId1 = (filletCenter - intersection) * dir1 > 0 ? PointPos::start : PointPos::end;
            PosId2 = (filletCenter - intersection) * dir2 > 0 ? PointPos::start : PointPos::end;

            if (createCorner) {
                transferFilletConstraints(GeoId1, PosId1, GeoId2, PosId2);
            }
            else {
                delConstraintOnPoint(GeoId1, PosId1, false);
                delConstraintOnPoint(GeoId2, PosId2, false);
            }

            auto tangent1 = std::make_unique<Sketcher::Constraint>();
            auto tangent2 = std::make_unique<Sketcher::Constraint>();

            tangent1->Type = Sketcher::Tangent;
            tangent1->First = GeoId1;
            tangent1->FirstPos = PosId1;
            tangent1->Second = filletId;

            tangent2->Type = Sketcher::Tangent;
            tangent2->First = GeoId2;
            tangent2->FirstPos = PosId2;
            tangent2->Second = filletId;

            if (dist1.Length() < dist2.Length()) {
                tangent1->SecondPos = PointPos::start;
                tangent2->SecondPos = PointPos::end;
                movePoint(GeoId1, PosId1, arc->getStartPoint(/*emulateCCW=*/true), false, true);
                movePoint(GeoId2, PosId2, arc->getEndPoint(/*emulateCCW=*/true), false, true);
            }
            else {
                tangent1->SecondPos = PointPos::end;
                tangent2->SecondPos = PointPos::start;
                movePoint(GeoId1, PosId1, arc->getEndPoint(/*emulateCCW=*/true), false, true);
                movePoint(GeoId2, PosId2, arc->getStartPoint(/*emulateCCW=*/true), false, true);
            }

            addConstraint(std::move(tangent1));
            addConstraint(std::move(tangent2));
        }
    }
    else if (geo1->isDerivedFrom<Part::GeomBoundedCurve>() && geo2->isDerivedFrom<Part::GeomBoundedCurve>()) {

        auto distancetorefpoints =
            [](Base::Vector3d ip1, Base::Vector3d ip2, Base::Vector3d ref1, Base::Vector3d ref2) {
                return (ip1 - ref1).Length() + (ip2 - ref2).Length();
            };

        auto selectintersection =
            [&distancetorefpoints](std::vector<std::pair<Base::Vector3d, Base::Vector3d>>& points,
                                   std::pair<Base::Vector3d, Base::Vector3d>& interpoints,
                                   const Base::Vector3d& refPnt1,
                                   const Base::Vector3d& refPnt2) {
                if (points.empty()) {
                    return -1;
                }
                else {
                    double dist =
                        distancetorefpoints(points[0].first, points[0].second, refPnt1, refPnt2);
                    int i = 0, si = 0;

                    for (auto ipoints : points) {
                        double d =
                            distancetorefpoints(ipoints.first, ipoints.second, refPnt1, refPnt2);

                        if (d < dist) {
                            si = i;
                            dist = d;
                        }

                        i++;
                    }

                    interpoints = points[si];

                    return 0;
                }
            };

        // NOTE: While it is not a requirement that the endpoints of the corner to trim are
        // coincident
        //       for GeomTrimmedCurves, it is for GeomBoundedCurves. The reason is that there is no
        //       basiscurve that can be extended to find an intersection.
        //
        //       However, GeomTrimmedCurves sometimes run into problems when trying to calculate the
        //       intersection of basis curves, for example in the case of hyperbola sometimes the
        //       cosh goes out of range while calculating this intersection of basis curves.
        //
        //        Consequently:
        //        i. for GeomBoundedCurves, other than GeomTrimmedCurves, a coincident endpoint is
        //        mandatory. ii. for GeomTrimmedCurves, if there is a coincident endpoint, it is
        //        used for the fillet, iii. for GeomTrimmedCurves, if there is not a coincident
        //        endpoint, an intersection of basis curves
        //             is attempted.

        const Part::GeomCurve* curve1 = static_cast<const Part::GeomCurve*>(geo1);
        const Part::GeomCurve* curve2 = static_cast<const Part::GeomCurve*>(geo2);

        double refparam1;
        double refparam2;

        try {
            if (!curve1->closestParameter(refPnt1, refparam1))
                return -1;
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError,
                   "Unable to determine the parameter of the first selected curve at the reference "
                   "point.")
        }

        try {
            if (!curve2->closestParameter(refPnt2, refparam2))
                return -1;
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError,
                   "Unable to determine the parameter of the second selected curve at the "
                   "reference point.")
        }

#ifdef DEBUG
        Base::Console().Log("\n\nFILLET DEBUG\n\n");
        Base::Console().Log("Ref param: (%f);(%f)", refparam1, refparam2);
#endif

        std::pair<Base::Vector3d, Base::Vector3d> interpoints;
        std::vector<std::pair<Base::Vector3d, Base::Vector3d>> points;


        // look for coincident constraints between curves, take the coincident closest to the
        // refpoints
        double dist = INFINITY;

        const std::vector<Constraint*>& constraints = this->Constraints.getValues();

        for (auto& constr : constraints) {
            if (constr->Type == Sketcher::Coincident || constr->Type == Sketcher::Perpendicular
                || constr->Type == Sketcher::Tangent) {
                if (constr->First == GeoId1 && constr->Second == GeoId2
                    && constr->FirstPos != PointPos::none
                    && constr->SecondPos != PointPos::none) {
                    Base::Vector3d tmpp1 = getPoint(constr->First, constr->FirstPos);
                    Base::Vector3d tmpp2 = getPoint(constr->Second, constr->SecondPos);
                    double tmpdist = distancetorefpoints(tmpp1, tmpp2, refPnt1, refPnt2);
                    if (tmpdist < dist) {
                        PosId1 = constr->FirstPos;
                        PosId2 = constr->SecondPos;
                        dist = tmpdist;
                        interpoints = std::make_pair(tmpp1, tmpp2);
                    }
                }
                else if (constr->First == GeoId2 && constr->Second == GeoId1
                         && constr->FirstPos != PointPos::none
                         && constr->SecondPos != PointPos::none) {
                    Base::Vector3d tmpp2 = getPoint(constr->First, constr->FirstPos);
                    Base::Vector3d tmpp1 = getPoint(constr->Second, constr->SecondPos);
                    double tmpdist = distancetorefpoints(tmpp1, tmpp2, refPnt1, refPnt2);
                    if (tmpdist < dist) {
                        PosId2 = constr->FirstPos;
                        PosId1 = constr->SecondPos;
                        dist = tmpdist;
                        interpoints = std::make_pair(tmpp1, tmpp2);
                    }
                }
            }
        }

        if (PosId1 == PointPos::none) {
            // no coincident was found, try basis curve intersection if GeomTrimmedCurve
            if (geo1->isDerivedFrom<Part::GeomTrimmedCurve>()
                && geo2->isDerivedFrom<Part::GeomTrimmedCurve>()) {

                auto* tcurve1 =static_cast<const Part::GeomTrimmedCurve*>(geo1);
                auto* tcurve2 = static_cast<const Part::GeomTrimmedCurve*>(geo2);

                try {
                    if (!tcurve1->intersectBasisCurves(tcurve2, points))
                        return -1;
                }
                catch (Base::CADKernelError& e) {
                    e.ReportException();
                    THROWMT(Base::CADKernelError,
                            QT_TRANSLATE_NOOP("Exceptions",
                                              "Unable to guess intersection of curves. Try adding "
                                              "a coincident constraint between the vertices of the "
                                              "curves you are intending to fillet."))
                }

                int res = selectintersection(points, interpoints, refPnt1, refPnt2);

                if (res != 0) {
                    return res;
                }
            }
            else {
                return -1;// not a GeomTrimmedCurve and no coincident point.
            }
        }

        // Now that we know where the curves intersect, get the parameters in the curves of those
        // points
        double intparam1;
        double intparam2;

        try {
            if (!curve1->closestParameter(interpoints.first, intparam1)) {
                return -1;
            }
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError,
                   "Unable to determine the parameter of the first selected curve at the "
                   "intersection of the curves.")
        }

        try {
            if (!curve2->closestParameter(interpoints.second, intparam2)) {
                return -1;
            }
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError,
                   "Unable to determine the parameter of the second selected curve at the "
                   "intersection of the curves.")
        }

        // get the starting parameters of each curve
        double spc1 = curve1->getFirstParameter();
        double spc2 = curve2->getFirstParameter();

        // get a fillet radius if zero was given
        Base::Vector3d ref21 = refPnt2 - refPnt1;

        if (radius == .0f) {
            // guess a radius
            // https://forum.freecad.org/viewtopic.php?f=3&t=31594&start=50#p266658
            //
            // We do not know the actual tangency points until we intersect the offset curves, but
            // we do not have offset curves before with decide on a radius.
            //
            // This estimation guesses a radius as the average of the distances from the reference
            // points with respect to the intersection of the normals at those reference points.

            try {
                Base::Vector3d tdir1;
                Base::Vector3d tdir2;

                // We want normals, but OCCT normals require curves to be 2 times derivable, and
                // lines are not tangency calculation requires 1 time derivable.

                if (!curve1->tangent(refparam1, tdir1))
                    return -1;

                if (!curve2->tangent(refparam2, tdir2))
                    return -1;

                Base::Vector3d dir1(tdir1.y, -tdir1.x, 0);
                Base::Vector3d dir2(tdir2.y, -tdir2.x, 0);

                double det = -dir1.x * dir2.y + dir2.x * dir1.y;

                if (std::abs(det) < Precision::Confusion()) {
                    // no intersection of normals
                    THROWM(Base::RuntimeError, "No intersection of normals")
                }

                Base::Vector3d refp1 = curve1->pointAtParameter(refparam1);
                Base::Vector3d refp2 = curve2->pointAtParameter(refparam2);

                // Base::Console().Log("refpoints:
                // (%f,%f,%f);(%f,%f,%f)",refp1.x,refp1.y,refp1.z,refp2.x,refp2.y,refp2.z);

                Base::Vector3d normalintersect(
                    (-dir1.x * dir2.x * refp1.y + dir1.x * dir2.x * refp2.y
                     - dir1.x * dir2.y * refp2.x + dir2.x * dir1.y * refp1.x)
                        / det,
                    (-dir1.x * dir2.y * refp1.y + dir2.x * dir1.y * refp2.y
                     + dir1.y * dir2.y * refp1.x - dir1.y * dir2.y * refp2.x)
                        / det,
                    0);

                radius = ((refp1 - normalintersect).Length() + (refp2 - normalintersect).Length()) / 2;
            }
            catch (const Base::Exception&) {
                radius = ref21.Length();// fall-back to simplest estimation.
            }
        }


#ifdef DEBUG
        Base::Console().Log("Start param: (%f);(%f)\n", spc1, spc2);

        Base::Vector3d c1pf = curve1->pointAtParameter(spc1);
        Base::Vector3d c2pf = curve2->pointAtParameter(spc2);

        Base::Console().Log("start point curves: (%f,%f,%f);(%f,%f,%f)\n",
                            c1pf.x,
                            c1pf.y,
                            c1pf.z,
                            c2pf.x,
                            c2pf.y,
                            c2pf.z);
#endif
        // We create Offset curves at the suggested radius, the direction of offset is estimated
        // from the tangency vector
        Base::Vector3d tdir1 = curve1->firstDerivativeAtParameter(refparam1);
        Base::Vector3d tdir2 = curve2->firstDerivativeAtParameter(refparam2);

#ifdef DEBUG
        Base::Console().Log("tangent vectors: (%f,%f,%f);(%f,%f,%f)\n",
                            tdir1.x,
                            tdir1.y,
                            tdir1.z,
                            tdir2.x,
                            tdir2.y,
                            tdir2.z);
        Base::Console().Log("inter-ref vector: (%f,%f,%f)\n", ref21.x, ref21.y, ref21.z);
#endif

        Base::Vector3d vn(0, 0, 1);

        double sdir1 = tdir1.Cross(ref21).Dot(vn);
        double sdir2 = tdir2.Cross(-ref21).Dot(vn);

#ifdef DEBUG
        Base::Console().Log("sign of offset: (%f,%f)\n", sdir1, sdir2);
        Base::Console().Log("radius: %f\n", radius);
#endif

        Part::GeomOffsetCurve* ocurve1 = new Part::GeomOffsetCurve(
            Handle(Geom_Curve)::DownCast(curve1->handle()), (sdir1 < 0) ? radius : -radius, vn);

        Part::GeomOffsetCurve* ocurve2 = new Part::GeomOffsetCurve(
            Handle(Geom_Curve)::DownCast(curve2->handle()), (sdir2 < 0) ? radius : -radius, vn);

#ifdef DEBUG
        Base::Vector3d oc1pf = ocurve1->pointAtParameter(ocurve1->getFirstParameter());
        Base::Vector3d oc2pf = ocurve2->pointAtParameter(ocurve2->getFirstParameter());

        Base::Console().Log("start point offset curves: (%f,%f,%f);(%f,%f,%f)\n",
                            oc1pf.x,
                            oc1pf.y,
                            oc1pf.z,
                            oc2pf.x,
                            oc2pf.y,
                            oc2pf.z);

        /*auto printoffsetcurve = [](Part::GeomOffsetCurve *c) {

            for(double param = c->getFirstParameter(); param < c->getLastParameter(); param = param
        + (c->getLastParameter()-c->getFirstParameter())/10) Base::Console().Log("\n%f:
        (%f,%f,0)\n", param, c->pointAtParameter(param).x,c->pointAtParameter(param).y);

        };

        printoffsetcurve(ocurve1);
        printoffsetcurve(ocurve2);*/
#endif

        // Next we calculate the intersection of offset curves to get the center of the fillet
        std::pair<Base::Vector3d, Base::Vector3d> filletcenterpoint;
        std::vector<std::pair<Base::Vector3d, Base::Vector3d>> offsetintersectionpoints;

        try {
            if (!ocurve1->intersect(ocurve2, offsetintersectionpoints)) {
#ifdef DEBUG
                Base::Console().Log("No intersection between offset curves\n");
#endif
                return -1;
            }
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError, "Unable to find intersection between offset curves.")
        }

#ifdef DEBUG
        for (auto inter : offsetintersectionpoints) {
            Base::Console().Log("offset int(%f,%f,0)\n", inter.first.x, inter.first.y);
        }
#endif

        int res = selectintersection(offsetintersectionpoints, filletcenterpoint, refPnt1, refPnt2);

        if (res != 0) {
            return res;
        }

#ifdef DEBUG
        Base::Console().Log(
            "selected offset int(%f,%f,0)\n", filletcenterpoint.first.x, filletcenterpoint.first.y);
#endif

        double refoparam1;
        double refoparam2;

        try {
            if (!curve1->closestParameter(filletcenterpoint.first, refoparam1)) {
                return -1;
            }
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError, "Unable to determine the starting point of the arc.")
        }

        try {
            if (!curve2->closestParameter(filletcenterpoint.second, refoparam2)) {
                return -1;
            }
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            THROWM(Base::CADKernelError, "Unable to determine the end point of the arc.")
        }

        // Next we calculate the closest points to the fillet center, so the points where tangency
        // is to be applied
        Base::Vector3d refp1 = curve1->pointAtParameter(refoparam1);
        Base::Vector3d refp2 = curve2->pointAtParameter(refoparam2);

#ifdef DEBUG
        Base::Console().Log("refpoints: (%f,%f,%f);(%f,%f,%f)",
                            refp1.x,
                            refp1.y,
                            refp1.z,
                            refp2.x,
                            refp2.y,
                            refp2.z);
#endif
        // Now we create arc for the fillet
        double startAngle, endAngle, range;

        Base::Vector3d radDir1 = refp1 - filletcenterpoint.first;
        Base::Vector3d radDir2 = refp2 - filletcenterpoint.first;

        startAngle = atan2(radDir1.y, radDir1.x);

        range = atan2(-radDir1.y * radDir2.x + radDir1.x * radDir2.y,
                      radDir1.x * radDir2.x + radDir1.y * radDir2.y);

        endAngle = startAngle + range;

        if (endAngle < startAngle) {
            std::swap(startAngle, endAngle);
        }

        if (endAngle > 2 * M_PI) {
            endAngle -= 2 * M_PI;
        }

        if (startAngle < 0) {
            endAngle += 2 * M_PI;
        }

        // Create Arc Segment
        auto* arc = new Part::GeomArcOfCircle();
        arc->setRadius(radDir1.Length());
        arc->setCenter(filletcenterpoint.first);
        arc->setRange(startAngle, endAngle, /*emulateCCWXY=*/true);

        p1 = arc->getStartPoint();
        p2 = arc->getEndPoint();

        // add arc to sketch geometry
        filletId = addGeometry(arc);
        if (filletId < 0) {
            delete arc;
            return -1;
        }

        if (trim) {
            auto selectend = [](double intparam, double refparam, double startparam) {
                if ((intparam > refparam && startparam >= refparam)
                    || (intparam < refparam && startparam <= refparam)) {
                    return PointPos::start;
                }
                else {
                    return PointPos::end;
                }
            };

            // Two cases:
            // a) there as a coincidence constraint
            // b) we used the basis curve intersection


            if (PosId1 == Sketcher::PointPos::none) {
                PosId1 = selectend(intparam1, refoparam1, spc1);
                PosId2 = selectend(intparam2, refoparam2, spc2);
            }


            delConstraintOnPoint(GeoId1, PosId1, false);
            delConstraintOnPoint(GeoId2, PosId2, false);


            auto* tangent1 = new Sketcher::Constraint();
            auto* tangent2 = new Sketcher::Constraint();

            tangent1->Type = Sketcher::Tangent;
            tangent1->First = GeoId1;
            tangent1->FirstPos = PosId1;
            tangent1->Second = filletId;

            tangent2->Type = Sketcher::Tangent;
            tangent2->First = GeoId2;
            tangent2->FirstPos = PosId2;
            tangent2->Second = filletId;

            double dist1 = (refp1 - arc->getStartPoint(true)).Length();
            double dist2 = (refp1 - arc->getEndPoint(true)).Length();

            // Base::Console().Log("dists_refpoint_to_arc_sp_ep: (%f);(%f)",dist1,dist2);

            if (dist1 < dist2) {
                tangent1->SecondPos = PointPos::start;
                tangent2->SecondPos = PointPos::end;
                movePoint(GeoId1, PosId1, arc->getStartPoint(true), false, true);
                movePoint(GeoId2, PosId2, arc->getEndPoint(true), false, true);
            }
            else {
                tangent1->SecondPos = PointPos::end;
                tangent2->SecondPos = PointPos::start;
                movePoint(GeoId1, PosId1, arc->getEndPoint(true), false, true);
                movePoint(GeoId2, PosId2, arc->getStartPoint(true), false, true);
            }

            addConstraint(tangent1);
            addConstraint(tangent2);
            delete tangent1;
            delete tangent2;
        }
        delete arc;
        delete ocurve1;
        delete ocurve2;

#ifdef DEBUG
        Base::Console().Log("\n\nEND OF FILLET DEBUG\n\n");
#endif
    }
    else {
        return -1;
    }

    if (chamfer) {
        auto line = std::make_unique<Part::GeomLineSegment>();
        line->setPoints(p1, p2);
        int lineGeoId = addGeometry(line.get());

        auto coinc1 = std::make_unique<Sketcher::Constraint>();
        auto coinc2 = std::make_unique<Sketcher::Constraint>();

        coinc1->Type = Sketcher::Coincident;
        coinc1->First = lineGeoId;
        coinc1->FirstPos = PointPos::start;

        coinc2->Type = Sketcher::Coincident;
        coinc2->First = lineGeoId;
        coinc2->FirstPos = PointPos::end;

        if (trim) {
            coinc1->Second = GeoId1;
            coinc1->SecondPos = PosId1;
            coinc2->Second = GeoId2;
            coinc2->SecondPos = PosId2;
        }
        else {
            coinc1->Second = filletId;
            coinc1->SecondPos = PointPos::start;
            coinc2->Second = filletId;
            coinc2->SecondPos = PointPos::end;
        }

        addConstraint(std::move(coinc1));
        addConstraint(std::move(coinc2));

        setConstruction(filletId, true);
    }

    // if we do not have a recompute after the geometry creation, the sketch must be solved to
    // update the DoF of the solver
    if (noRecomputes) {
        solve();
    }

    return 0;
}

SketchSolveStatus SketchObject::extend(int GeoId, double increment, PointPos endpoint)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return SketchSolveStatus::SolverError;

    const std::vector<Part::Geometry*>& geomList = getInternalGeometry();
    Part::Geometry* geom = geomList[GeoId];
    auto status = SketchSolveStatus::SolverError;
    if (geom->is<Part::GeomLineSegment>()) {
        Part::GeomLineSegment* seg = static_cast<Part::GeomLineSegment*>(geom);
        Base::Vector3d startVec = seg->getStartPoint();
        Base::Vector3d endVec = seg->getEndPoint();
        if (endpoint == PointPos::start) {
            Base::Vector3d newPoint = startVec - endVec;
            double scaleFactor = newPoint.Length() + increment;
            newPoint.Normalize();
            newPoint.Scale(scaleFactor, scaleFactor, scaleFactor);
            newPoint = newPoint + endVec;
            status = movePoint(GeoId, Sketcher::PointPos::start, newPoint, false, true);
        }
        else if (endpoint == PointPos::end) {
            Base::Vector3d newPoint = endVec - startVec;
            double scaleFactor = newPoint.Length() + increment;
            newPoint.Normalize();
            newPoint.Scale(scaleFactor, scaleFactor, scaleFactor);
            newPoint = newPoint + startVec;
            status = movePoint(GeoId, Sketcher::PointPos::end, newPoint, false, true);
        }
    }
    else if (geom->is<Part::GeomArcOfCircle>()) {
        Part::GeomArcOfCircle* arc = static_cast<Part::GeomArcOfCircle*>(geom);
        double startArc, endArc;
        arc->getRange(startArc, endArc, true);
        if (endpoint == PointPos::start) {
            arc->setRange(startArc - increment, endArc, true);
            status = SketchSolveStatus::Success;
        }
        else if (endpoint == PointPos::end) {
            arc->setRange(startArc, endArc + increment, true);
            status = SketchSolveStatus::Success;
        }
    }
    if (status == SketchSolveStatus::Success && noRecomputes) {
        solve();
    }
    return status;
}

bool SketchObject::seekTrimPoints(int GeoId, const Base::Vector3d& point, int& GeoId1,
                                  Base::Vector3d& intersect1, int& GeoId2,
                                  Base::Vector3d& intersect2)
{
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return false;

    auto geos = getCompleteGeometry();// this includes the axes too

    geos.resize(geos.size() - 2);// remove the axes to avoid intersections with the axes

    int localindex1, localindex2;

    // Not found in will be returned as -1, not as GeoUndef, Part WB is agnostic to the concept of
    // GeoUndef
    if (!Part2DObject::seekTrimPoints(
            geos, GeoId, point, localindex1, intersect1, localindex2, intersect2))
        return false;

    // invalid complete geometry indices are mapped to GeoUndef
    GeoId1 = getGeoIdFromCompleteGeometryIndex(localindex1);
    GeoId2 = getGeoIdFromCompleteGeometryIndex(localindex2);

    return true;
}

SketchSolveStatus SketchObject::trim(int GeoId, const Base::Vector3d& point)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    //******************* Basic checks rejecting the operation
    //****************************************//
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return SketchSolveStatus::SolverError;

    auto geo = getGeometry(GeoId);

    if (!GeometryFacade::isInternalType(geo, InternalType::None))
        return SketchSolveStatus::SolverError;// internal alignment geometry is not trimmable

    //******************* Lambdas - common functions for different intersections
    //****************************************//

    // returns true if the point defined by (GeoId1, pos1) can be considered to be coincident with
    // point.
    auto arePointsWithinPrecision = [](Base::Vector3d point1, Base::Vector3d point2) {
        // From testing: 500x (or 0.000050) is needed in order to not falsely distinguish points
        // calculated with seekTrimPoints
        if ((point1 - point2).Length() < 500 * Precision::Confusion())
            return true;

        return false;
    };

    auto isPointAtPosition =
        [this, arePointsWithinPrecision](int GeoId1, PointPos pos1, Base::Vector3d point) {
            Base::Vector3d pp = getPoint(GeoId1, pos1);

            return arePointsWithinPrecision(point, pp);
        };

    // Helper function to remove Equal constraints from a chosen edge (e.g Line segment).
    auto delEqualConstraintsOnGeoId = [this](int GeoId) {
        std::vector<int> delete_list;
        int index = 0;
        const std::vector<Constraint*>& constraints = this->Constraints.getValues();
        for (std::vector<Constraint*>::const_iterator it = constraints.begin();
             it != constraints.end();
             ++it, ++index) {
            Constraint* constr = *(it);
            if (constr->First == GeoId && constr->Type == Sketcher::Equal) {
                delete_list.push_back(index);
            }
            if (constr->Second == GeoId && constr->Type == Sketcher::Equal) {
                delete_list.push_back(index);
            }
        }
        delConstraints(delete_list, false);
    };

    // Checks whether preexisting constraints must be converted to new constraints.
    // Preexisting point on object constraints get converted to coincidents, unless an end-to-end
    // tangency is more relevant. returns by reference:
    //      - The type of constraint that should be used to constraint GeoId1 and GeoId
    //      - The element of GeoId1 to which the constraint should be applied.
    auto transformPreexistingConstraints = [this, isPointAtPosition](int GeoId,
                                                                     int GeoId1,
                                                                     Base::Vector3d point1,
                                                                     ConstraintType& constrType,
                                                                     PointPos& secondPos) {
        const std::vector<Constraint*>& constraints = this->Constraints.getValues();
        int constrId = 0;
        std::vector<int> delete_list;
        for (std::vector<Constraint*>::const_iterator it = constraints.begin();
             it != constraints.end();
             ++it) {
            Constraint* constr = *(it);
            // There is a preexisting PointOnObject constraint, see if it must be converted to a
            // coincident
            if (constr->Type == Sketcher::PointOnObject && constr->First == GeoId1
                && constr->Second == GeoId) {
                if (isPointAtPosition(constr->First, constr->FirstPos, point1)) {
                    constrType = Sketcher::Coincident;
                    secondPos = constr->FirstPos;
                    delete_list.push_back(constrId);
                }
            }
            constrId++;
        }
        /* It is possible that the trimming entity has both a PointOnObject constraint to the
         * trimmed entity, and a simple Tangent contstrait to the trimmed entity. In this case we
         * want to change to a single end-to-end tangency, i.e we want to ensure that constrType1 is
         * set to Sketcher::Tangent, that the secondPos1 is captured from the PointOnObject, and
         * also make sure that the PointOnObject constraint is deleted. The below loop ensures this,
         * also in case the ordering of the constraints is first Tangent and then PointOnObject. */
        constrId = 0;
        for (std::vector<Constraint*>::const_iterator it = constraints.begin();
             it != constraints.end();
             ++it) {
            Constraint* constr = *(it);
            if (constr->Type == Sketcher::Tangent) {
                if (constr->First == GeoId1 && constr->Second == GeoId) {
                    constrType = Sketcher::Tangent;
                    if (secondPos == Sketcher::PointPos::none)
                        secondPos = constr->FirstPos;
                    delete_list.push_back(constrId);
                }
                else if (constr->First == GeoId && constr->Second == GeoId1) {
                    constrType = Sketcher::Tangent;
                    if (secondPos == Sketcher::PointPos::none)
                        secondPos = constr->SecondPos;
                    delete_list.push_back(constrId);
                }
            }
            if (constr->Type == Sketcher::Perpendicular) {
                if (constr->First == GeoId1 && constr->Second == GeoId) {
                    constrType = Sketcher::Perpendicular;
                    if (secondPos == Sketcher::PointPos::none)
                        secondPos = constr->FirstPos;
                    delete_list.push_back(constrId);
                }
                else if (constr->First == GeoId && constr->Second == GeoId1) {
                    constrType = Sketcher::Perpendicular;
                    if (secondPos == Sketcher::PointPos::none)
                        secondPos = constr->SecondPos;
                    delete_list.push_back(constrId);
                }
            }

            constrId++;
        }
        delConstraints(delete_list, false);
    };

    // makes an equality constraint between GeoId1 and GeoId2
    auto constrainAsEqual = [this](int GeoId1, int GeoId2) {
        auto newConstr = std::make_unique<Sketcher::Constraint>();

        // Build Constraints associated with new pair of arcs
        newConstr->Type = Sketcher::Equal;
        newConstr->First = GeoId1;
        newConstr->FirstPos = Sketcher::PointPos::none;
        newConstr->Second = GeoId2;
        newConstr->SecondPos = Sketcher::PointPos::none;
        addConstraint(std::move(newConstr));
    };

    // Removes all internal geometry of a BSplineCurve and updates the GeoId index after removal
    auto ifBSplineRemoveInternalAlignmentGeometry = [this](int& GeoId) {
        const Part::Geometry* geo = getGeometry(GeoId);
        if (geo->is<Part::GeomBSplineCurve>()) {
            // We need to remove the internal geometry of the BSpline, as BSplines change in number
            // of poles and knots We save the tags of the relevant geometry to retrieve the new
            // GeoIds later on.
            boost::uuids::uuid GeoIdTag;

            GeoIdTag = geo->getTag();

            deleteUnusedInternalGeometry(GeoId);

            auto vals = getCompleteGeometry();

            for (size_t i = 0; i < vals.size(); i++) {
                if (vals[i]->getTag() == GeoIdTag) {
                    GeoId = getGeoIdFromCompleteGeometryIndex(i);
                    break;
                }
            }
        }
    };

    // given a geometry and tree points, returns the corresponding parameters of the geometry points
    // closest to them
    auto getIntersectionParameters = [](const Part::Geometry* geo,
                                        const Base::Vector3d point,
                                        double& pointParam,
                                        const Base::Vector3d point1,
                                        double& point1Param,
                                        const Base::Vector3d point2,
                                        double& point2Param) {
        auto curve = static_cast<const Part::GeomCurve*>(geo);

        try {
            curve->closestParameter(point, pointParam);
            curve->closestParameter(point1, point1Param);
            curve->closestParameter(point2, point2Param);
        }
        catch (Base::CADKernelError& e) {
            e.ReportException();
            return false;
        }

        return true;
    };

    //******************* Step A => Detection of intersection - Common to all Geometries
    //****************************************//
    int GeoId1 = GeoEnum::GeoUndef, GeoId2 = GeoEnum::GeoUndef;
    Base::Vector3d point1, point2;
    // Using SketchObject wrapper, as Part2DObject version returns GeoId = -1 when intersection not
    // found, which is wrong for a GeoId (axis). seekTrimPoints returns:
    // - For a parameter associated with "point" between an intersection and the end point
    // (non-periodic case) GeoId1 != GeoUndef and GeoId2 == GeoUndef
    // - For a parameter associated with "point" between the start point and an intersection
    // (non-periodic case) GeoId2 != GeoUndef and GeoId1 == GeoUndef
    // - For a parameter associated with "point" between two intersection points, GeoId1 != GeoUndef
    // and GeoId2 != GeoUndef
    //
    // FirstParam < point1param < point2param < LastParam
    if (!SketchObject::seekTrimPoints(GeoId, point, GeoId1, point1, GeoId2, point2)) {
        // If no suitable trim points are found, then trim defaults to deleting the geometry
        delGeometry(GeoId);
        return SketchSolveStatus::Success;
    }

    //******************* Preparation of BSplines ****************************************//
    // Trimmed B-Spline internal geometry cannot be reused
    geo = getGeometry(GeoId);

    auto isBSpline = geo->is<Part::GeomBSplineCurve>();
    auto isPeriodicBSpline =
        isBSpline && static_cast<const Part::GeomBSplineCurve*>(geo)->isPeriodic();
    auto isNonPeriodicBSpline =
        isBSpline && !static_cast<const Part::GeomBSplineCurve*>(geo)->isPeriodic();
    auto isLineSegment = geo->is<Part::GeomLineSegment>();
    auto isDerivedFromTrimmedCurve = geo->isDerivedFrom(Part::GeomTrimmedCurve::getClassTypeId());
    auto isCircle = geo->is<Part::GeomCircle>();
    auto isEllipse = geo->is<Part::GeomEllipse>();

    if (isBSpline) {

        // Two options, it is a periodic bspline and we need two intersections or
        // it is a non-periodic bspline and one intersection is enough.
        auto bspline = static_cast<const Part::GeomBSplineCurve*>(geo);

        if (bspline->isPeriodic() && (GeoId1 == GeoEnum::GeoUndef || GeoId2 == GeoEnum::GeoUndef))
            return SketchSolveStatus::SolverError;

        ifBSplineRemoveInternalAlignmentGeometry(GeoId);// GeoId gets updated here

        // When internal alignment geometry is removed from a bspline, it moves slightly
        // this causes that small segments are detected near the endpoints.
        //
        // The alternative to this re-detection, is to remove the internal alignment geometry
        // before the detection. However, that would cause the lost of the internal alignment
        // geometry in a case where trimming does not succeed because seekTrimPoints fails to find
        // (suitable) intersection(s)
        if (!SketchObject::seekTrimPoints(GeoId, point, GeoId1, point1, GeoId2, point2)) {
            // If no suitable trim points are found, then trim defaults to deleting the geometry
            delGeometry(GeoId);
            return SketchSolveStatus::Success;
        }

        geo = getGeometry(GeoId);
    }

    if (GeoId1 != GeoEnum::GeoUndef && GeoId2 != GeoEnum::GeoUndef
        && arePointsWithinPrecision(point1, point2)) {
        // If both points are detected and are coincident, deletion is the only option.
        delGeometry(GeoId);

        return SketchSolveStatus::Success;
    }

    //******************* Step B.1 => Trimming for GeomTrimmedCurves (line segment and arcs)
    //****************************************//
    if (isDerivedFromTrimmedCurve || isNonPeriodicBSpline) {

        if (geo->isDerivedFrom(Part::GeomConic::getClassTypeId())) {
            auto* tc = static_cast<const Part::GeomConic*>(geo);
            if (tc->isReversed()) {
                // reversing does not change the curve as seen by the sketcher.
                const_cast<Part::GeomConic*>(tc)->reverse();
            }
        }

        //****** Step B.1 (1) => Determine intersection parameters ******//
        // Now LastParam > FirstParam
        double firstParam, lastParam;
        if (isDerivedFromTrimmedCurve) {
            auto aoc = static_cast<const Part::GeomTrimmedCurve*>(geo);
            aoc->getRange(firstParam, lastParam);
        }
        else if (isNonPeriodicBSpline) {
            auto bsp = static_cast<const Part::GeomBSplineCurve*>(geo);
            firstParam = bsp->getFirstParameter();
            lastParam = bsp->getLastParameter();
        }

        double pointParam, point1Param, point2Param;
        if (!getIntersectionParameters(
                geo, point, pointParam, point1, point1Param, point2, point2Param))
            return SketchSolveStatus::SolverError;

#ifdef DEBUG
        Base::Console().Log("Trim sought: GeoId1=%d (%f), GeoId2=%d (%f)\n",
                            GeoId1,
                            point1Param,
                            GeoId2,
                            point2Param);
#endif

        // seekTrimPoints enforces that firstParam < point1Param < point2Param < lastParam
        auto paramDistance = [](double param1, double param2) {
            double distance = fabs(param1 - param2);

            if (distance < Precision::Confusion())
                return 0.;
            else
                return distance;
        };

        //****** Step B.1 (2) => Determine trimmable sections and trim operation ******//

        // Determine if there is something trimmable
        double startDistance = GeoId1 != GeoEnum::GeoUndef ? paramDistance(firstParam, point1Param)
                                                           : paramDistance(firstParam, point2Param);
        double endDistance = GeoId2 != GeoEnum::GeoUndef ? paramDistance(lastParam, point2Param)
                                                         : paramDistance(lastParam, point1Param);
        double middleDistance = (GeoId1 != GeoEnum::GeoUndef && GeoId2 != GeoEnum::GeoUndef)
            ? paramDistance(point1Param, point2Param)
            : 0.0;

        bool trimmableStart = startDistance > 0.;
        bool trimmableMiddle = middleDistance > 0.;
        bool trimmableEnd = endDistance > 0.;

        struct Operation
        {
            Operation()
                : Type(trim_none),
                  actingParam(0.),
                  intersectingGeoId(GeoEnum::GeoUndef)
            {}
            enum
            {
                trim_none,
                trim_start,
                trim_middle,
                trim_end,
                trim_delete
            } Type;

            double actingParam;
            Base::Vector3d actingPoint;
            int intersectingGeoId;
        };

        Operation op;

        if (GeoId1 != GeoEnum::GeoUndef && GeoId2 != GeoEnum::GeoUndef && pointParam > point1Param
            && pointParam < point2Param) {
            // Trim Point between intersection points

            if ((!trimmableStart && !trimmableEnd) || !trimmableMiddle) {
                // if after trimming nothing would be left or if there is nothing to trim
                op.Type = Operation::trim_delete;
            }
            else if (trimmableStart && trimmableEnd) {
                op.Type = Operation::trim_middle;// trim between point1Param and point2Param
            }
            else if (trimmableStart /*&&!trimmableEnd*/) {
                op.Type = Operation::trim_end;
                op.actingParam = point1Param;// trim from point1Param until lastParam
                op.actingPoint = point1;
                op.intersectingGeoId = GeoId1;
            }
            else {// !trimmableStart && trimmableEnd
                op.Type = Operation::trim_start;
                op.actingParam = point2Param;// trim from firstParam until point2Param
                op.actingPoint = point2;
                op.intersectingGeoId = GeoId2;
            }
        }
        else if (GeoId2 != GeoEnum::GeoUndef && pointParam < point2Param) {
            if (trimmableEnd) {
                op.Type = Operation::trim_start;
                op.actingParam = point2Param;// trim from firstParam until point2Param
                op.actingPoint = point2;
                op.intersectingGeoId = GeoId2;
            }
            else {
                op.Type = Operation::trim_delete;
            }
        }
        else if (GeoId1 != GeoEnum::GeoUndef && pointParam > point1Param) {
            if (trimmableStart) {
                op.Type = Operation::trim_end;
                op.actingParam = point1Param;// trim from point1Param until lastParam
                op.actingPoint = point1;
                op.intersectingGeoId = GeoId1;
            }
            else {
                op.Type = Operation::trim_delete;
            }
        }
        else {
            return SketchSolveStatus::SolverError;
        }

        //****** Step B.1 (3) => Execute Trimming operation ******//

        if (op.Type == Operation::trim_delete) {
            delGeometry(GeoId);

            return SketchSolveStatus::Success;
        }
        else if (op.Type == Operation::trim_middle) {
            // We need to create new curve, this new curve will represent the segment comprising the
            // end
            auto vals = getInternalGeometry();
            auto newVals(vals);
            newVals[GeoId] = newVals[GeoId]->clone();
            newVals.push_back(newVals[GeoId]->clone());
            generateId(newVals.back());
            int newGeoId = newVals.size() - 1;

            if (isDerivedFromTrimmedCurve) {
                static_cast<Part::GeomTrimmedCurve*>(newVals[GeoId])
                    ->setRange(firstParam, point1Param);
                static_cast<Part::GeomTrimmedCurve*>(newVals.back())
                    ->setRange(point2Param, lastParam);
            }
            else if (isNonPeriodicBSpline) {
                static_cast<Part::GeomBSplineCurve*>(newVals[GeoId])->Trim(firstParam, point1Param);
                static_cast<Part::GeomBSplineCurve*>(newVals.back())->Trim(point2Param, lastParam);
            }

            Geometry.setValues(std::move(newVals));

            // go through all constraints and replace the point (GeoId,end) with (newGeoId,end)
            transferConstraints(GeoId, PointPos::end, newGeoId, PointPos::end);

            // For a trimmed line segment, if it had an equality constraint, it must be removed as
            // the segment length is not equal For the rest of trimmed curves, the proportion shall
            // be constrain to be equal.
            if (isLineSegment) {
                delEqualConstraintsOnGeoId(GeoId);
                delEqualConstraintsOnGeoId(newGeoId);
            }

            if (!isLineSegment && !isNonPeriodicBSpline) {
                constrainAsEqual(GeoId, newGeoId);
            }

            //****** Step B.1 (4) => Constraint end points of trim sections ******//

            // constrain the trimming points on the corresponding geometries
            PointPos secondPos1 = Sketcher::PointPos::none, secondPos2 = Sketcher::PointPos::none;
            ConstraintType constrType1 = Sketcher::PointOnObject,
                           constrType2 = Sketcher::PointOnObject;

            // Segment comprising the start
            transformPreexistingConstraints(GeoId, GeoId1, point1, constrType1, secondPos1);

            addConstraint(constrType1, GeoId, Sketcher::PointPos::end, GeoId1, secondPos1);

            // Segment comprising the end
            transformPreexistingConstraints(GeoId, GeoId2, point2, constrType2, secondPos2);

            addConstraint(constrType2, newGeoId, Sketcher::PointPos::start, GeoId2, secondPos2);

            // Both segments have a coincident center
            if (!isLineSegment && !isBSpline) {
                addConstraint(Sketcher::Coincident,
                              GeoId,
                              Sketcher::PointPos::mid,
                              newGeoId,
                              Sketcher::PointPos::mid);
            }

            if (isNonPeriodicBSpline)
                exposeInternalGeometry(GeoId);

            // if we do not have a recompute, the sketch must be solved to update the DoF of the
            // solver
            if (noRecomputes)
                solve();

            return SketchSolveStatus::Success;
        }
        else if (op.Type == Operation::trim_start || op.Type == Operation::trim_end) {
            // drop the second/first intersection point
            geo = getGeometry(GeoId);
            if (isDerivedFromTrimmedCurve) {
                auto newGeo = std::unique_ptr<Part::GeomTrimmedCurve>(
                    static_cast<Part::GeomTrimmedCurve*>(geo->clone()));

                if (op.Type == Operation::trim_start)
                    newGeo->setRange(op.actingParam, lastParam);
                else if (op.Type == Operation::trim_end)
                    newGeo->setRange(firstParam, op.actingParam);

                Geometry.set1Value(GeoId, std::move(newGeo));
            }
            else if (isNonPeriodicBSpline) {
                auto newGeo = std::unique_ptr<Part::GeomBSplineCurve>(
                    static_cast<Part::GeomBSplineCurve*>(geo->clone()));

                if (op.Type == Operation::trim_start)
                    newGeo->Trim(op.actingParam, lastParam);
                else if (op.Type == Operation::trim_end)
                    newGeo->Trim(firstParam, op.actingParam);

                Geometry.set1Value(GeoId, std::move(newGeo));
            }

            // After trimming it, a line segment won't have the same length
            if (isLineSegment) {
                delEqualConstraintsOnGeoId(GeoId);
            }

            //****** Step B.1 (4) => Constraint end points ******//
            // So this is the fallback constraint type here.
            ConstraintType constrType = Sketcher::PointOnObject;
            PointPos secondPos = Sketcher::PointPos::none;

            transformPreexistingConstraints(
                GeoId, op.intersectingGeoId, op.actingPoint, constrType, secondPos);

            if (op.Type == Operation::trim_start) {
                delConstraintOnPoint(GeoId, PointPos::start, false);
                // constrain the trimming point on the corresponding geometry
                addConstraint(constrType, GeoId, PointPos::start, op.intersectingGeoId, secondPos);
            }
            else if (op.Type == Operation::trim_end) {
                delConstraintOnPoint(GeoId, PointPos::end, false);
                // constrain the trimming point on the corresponding geometry
                addConstraint(constrType, GeoId, PointPos::end, op.intersectingGeoId, secondPos);
            }

            if (isNonPeriodicBSpline)
                exposeInternalGeometry(GeoId);

            // if we do not have a recompute, the sketch must be solved to update the DoF of the
            // solver
            if (noRecomputes)
                solve();

            return SketchSolveStatus::Success;
        }
        else {
            return SketchSolveStatus::SolverError;
        }
    }
    //******************* Step B.2 => Trimming for unbounded periodic geometries
    //****************************************//
    else if (isCircle || isEllipse || isPeriodicBSpline) {
        //****** STEP A(2) => Common tests *****//
        if (GeoId1 == GeoEnum::GeoUndef || GeoId2 == GeoEnum::GeoUndef)
            return SketchSolveStatus::SolverError;

        //****** Step B.2 (1) => Determine intersection parameters ******//
        double pointParam, point1Param, point2Param;
        if (!getIntersectionParameters(
                geo, point, pointParam, point1, point1Param, point2, point2Param))
            return SketchSolveStatus::SolverError;

#ifdef DEBUG
        Base::Console().Log("Trim sought: GeoId1=%d (%f), GeoId2=%d (%f)\n",
                            GeoId1,
                            point1Param,
                            GeoId2,
                            point2Param);
#endif

        //****** Step B.2 (3) => Execute Trimming operation ******//
        // Two intersection points detected
        std::unique_ptr<Part::Geometry> geoNew;

        if (isCircle) {
            auto circle = static_cast<const Part::GeomCircle*>(geo);
            auto aoc = std::make_unique<Part::GeomArcOfCircle>();
            aoc->setCenter(circle->getCenter());
            aoc->setRadius(circle->getRadius());
            aoc->setRange(point2Param, point1Param, /*emulateCCW=*/false);
            geoNew = std::move(aoc);
        }
        else if (isEllipse) {
            auto ellipse = static_cast<const Part::GeomEllipse*>(geo);
            auto aoe = std::make_unique<Part::GeomArcOfEllipse>();
            aoe->setCenter(ellipse->getCenter());
            aoe->setMajorRadius(ellipse->getMajorRadius());
            aoe->setMinorRadius(ellipse->getMinorRadius());
            aoe->setMajorAxisDir(ellipse->getMajorAxisDir());
            // CCW curve goes from point2 (start) to point1 (end)
            aoe->setRange(point2Param, point1Param, /*emulateCCW=*/false);
            geoNew = std::move(aoe);
        }
        else if (isPeriodicBSpline) {
            auto bspline = std::unique_ptr<Part::GeomBSplineCurve>(
                static_cast<Part::GeomBSplineCurve*>(geo->clone()));
            bspline->Trim(point2Param, point1Param);
            geoNew = std::move(bspline);
        }

        GeometryFacade::setId(geoNew.get(), GeometryFacade::getId(geo));
        this->Geometry.set1Value(GeoId, std::move(geoNew));

        //****** Step B.2 (4) => Constraint end points ******//

        PointPos secondPos1 = Sketcher::PointPos::none, secondPos2 = Sketcher::PointPos::none;
        ConstraintType constrType1 = Sketcher::PointOnObject, constrType2 = Sketcher::PointOnObject;

        // check first if start and end points are within a confusion tolerance
        if (isPointAtPosition(GeoId1, Sketcher::PointPos::start, point1)) {
            constrType1 = Sketcher::Coincident;
            secondPos1 = Sketcher::PointPos::start;
        }
        else if (isPointAtPosition(GeoId1, Sketcher::PointPos::end, point1)) {
            constrType1 = Sketcher::Coincident;
            secondPos1 = Sketcher::PointPos::end;
        }

        if (isPointAtPosition(GeoId2, Sketcher::PointPos::start, point2)) {
            constrType2 = Sketcher::Coincident;
            secondPos2 = Sketcher::PointPos::start;
        }
        else if (isPointAtPosition(GeoId2, Sketcher::PointPos::end, point2)) {
            constrType2 = Sketcher::Coincident;
            secondPos2 = Sketcher::PointPos::end;
        }

        transformPreexistingConstraints(GeoId, GeoId1, point1, constrType1, secondPos1);
        transformPreexistingConstraints(GeoId, GeoId2, point2, constrType2, secondPos2);

        if ((constrType1 == Sketcher::Coincident && secondPos1 == Sketcher::PointPos::none)
            || (constrType2 == Sketcher::Coincident && secondPos2 == Sketcher::PointPos::none))
            THROWM(
                ValueError,
                "Invalid position Sketcher::PointPos::none when creating a Coincident constraint")

        // constrain the trimming points on the corresponding geometries
        addConstraint(constrType1, GeoId, PointPos::end, GeoId1, secondPos1);

        addConstraint(constrType2, GeoId, PointPos::start, GeoId2, secondPos2);

        if (isBSpline)
            exposeInternalGeometry(GeoId);

        // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
        if (noRecomputes)
            solve();

        return SketchSolveStatus::Success;
    }

    return SketchSolveStatus::SolverError;
}

int SketchObject::split(int GeoId, const Base::Vector3d& point)
{
    // No need to check input data validity as this is an sketchobject managed operation

    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex()) {
        return -1;
    }

    const Part::Geometry* geo = getGeometry(GeoId);
    std::vector<int> newIds;
    std::vector<Constraint*> newConstraints;

    Base::Vector3d startPoint, endPoint, splitPoint;
    double startParam, endParam, splitParam = 0.0;
    unsigned int longestPart = 0;

    auto createGeosFromPeriodic = [&](const Part::GeomCurve* curve,
                                      auto getCurveWithLimitParams,
                                      auto createAndTransferConstraints) {
        // find split point
        curve->closestParameter(point, splitParam);
        double period = curve->getLastParameter() - curve->getFirstParameter();
        startParam = splitParam;
        endParam = splitParam + period;

        // create new arc and restrict it
        auto newCurve = getCurveWithLimitParams(curve, startParam, endParam);
        int newId(GeoEnum::GeoUndef);
        newId = addGeometry(std::move(newCurve));// after here newCurve is a shell
        if (newId >= 0) {
            newIds.push_back(newId);
            setConstruction(newId, GeometryFacade::getConstruction(curve));
            exposeInternalGeometry(newId);

            // transfer any constraints
            createAndTransferConstraints(GeoId, newId);
            return true;
        }

        return false;
    };

    auto createGeosFromNonPeriodic = [&](const Part::GeomBoundedCurve* curve,
                                         auto getCurveWithLimitParams,
                                         auto createAndTransferConstraints) {
        startPoint = curve->getStartPoint();
        endPoint = curve->getEndPoint();

        // find split point
        curve->closestParameter(point, splitParam);
        startParam = curve->getFirstParameter();
        endParam = curve->getLastParameter();
        // TODO: Using parameter difference as a poor substitute of length.
        // Computing length of an arc of a generic conic would be expensive.
        if (endParam - splitParam < Precision::PConfusion()
            || splitParam - startParam < Precision::PConfusion()) {
            THROWM(ValueError, "Split point is at one of the end points of the curve.");
        }
        if (endParam - splitParam > splitParam - startParam) {
            longestPart = 1;
        }

        // create new curves
        auto newCurve = getCurveWithLimitParams(curve, startParam, splitParam);
        int newId(GeoEnum::GeoUndef);
        newId = addGeometry(std::move(newCurve));
        if (newId >= 0) {
            newIds.push_back(newId);
            setConstruction(newId, GeometryFacade::getConstruction(curve));
            exposeInternalGeometry(newId);

            // the "second" half
            newCurve = getCurveWithLimitParams(curve, splitParam, endParam);
            newId = addGeometry(std::move(newCurve));
            if (newId >= 0) {
                newIds.push_back(newId);
                setConstruction(newId, GeometryFacade::getConstruction(curve));
                exposeInternalGeometry(newId);

                // TODO: Certain transfers and new constraint can be directly made here.
                // But this may reduce readability.
                // apply appropriate constraints on the new points at split point and
                // transfer constraints from start and end of original spline
                createAndTransferConstraints(GeoId, newIds[0], newIds[1]);
                return true;
            }
        }

        return false;
    };

    bool ok = false;
    if (geo->is<Part::GeomLineSegment>()) {
        ok = createGeosFromNonPeriodic(
            static_cast<const Part::GeomBoundedCurve*>(geo),
            [](const Part::GeomCurve* curve, double startParam, double endParam) {
                auto newArc = std::unique_ptr<Part::GeomLineSegment>(
                    static_cast<Part::GeomLineSegment*>(curve->copy()));
                newArc->setRange(startParam, endParam);
                return newArc;
            },
            [this, &newConstraints](int GeoId, int newId0, int newId1) {
                Constraint* joint = new Constraint();
                joint->Type = Coincident;
                joint->First = newId0;
                joint->FirstPos = PointPos::end;
                joint->Second = newId1;
                joint->SecondPos = PointPos::start;
                newConstraints.push_back(joint);

                transferConstraints(GeoId, PointPos::start, newId0, PointPos::start);
                transferConstraints(GeoId, PointPos::end, newId1, PointPos::end);
            });
    }
    else if (geo->is<Part::GeomCircle>()) {
        ok = createGeosFromPeriodic(
            static_cast<const Part::GeomCurve*>(geo),
            [](const Part::GeomCurve* curve, double startParam, double endParam) {
                auto newArc = std::make_unique<Part::GeomArcOfCircle>(
                    Handle(Geom_Circle)::DownCast(curve->handle()->Copy()));
                newArc->setRange(startParam, endParam, false);
                return newArc;
            },
            [this](int GeoId, int newId) {
                transferConstraints(GeoId, PointPos::mid, newId, PointPos::mid);
            });
    }
    else if (geo->is<Part::GeomEllipse>()) {
        ok = createGeosFromPeriodic(
            static_cast<const Part::GeomCurve*>(geo),
            [](const Part::GeomCurve* curve, double startParam, double endParam) {
                auto newArc = std::make_unique<Part::GeomArcOfEllipse>(
                    Handle(Geom_Ellipse)::DownCast(curve->handle()->Copy()));
                newArc->setRange(startParam, endParam, false);
                return newArc;
            },
            [this](int GeoId, int newId) {
                transferConstraints(GeoId, PointPos::mid, newId, PointPos::mid);
            });
    }
    else if (geo->is<Part::GeomArcOfCircle>()) {
        ok = createGeosFromNonPeriodic(
            static_cast<const Part::GeomBoundedCurve*>(geo),
            [](const Part::GeomCurve* curve, double startParam, double endParam) {
                auto newArc = std::unique_ptr<Part::GeomArcOfCircle>(
                    static_cast<Part::GeomArcOfCircle*>(curve->copy()));
                newArc->setRange(startParam, endParam, false);
                return newArc;
            },
            [this, &newConstraints](int GeoId, int newId0, int newId1) {
                Constraint* joint = new Constraint();
                joint->Type = Coincident;
                joint->First = newId0;
                joint->FirstPos = PointPos::end;
                joint->Second = newId1;
                joint->SecondPos = PointPos::start;
                newConstraints.push_back(joint);

                joint = new Constraint();
                joint->Type = Coincident;
                joint->First = newId0;
                joint->FirstPos = PointPos::mid;
                joint->Second = newId1;
                joint->SecondPos = PointPos::mid;
                newConstraints.push_back(joint);

                transferConstraints(GeoId, PointPos::start, newId0, PointPos::start, true);
                transferConstraints(GeoId, PointPos::mid, newId0, PointPos::mid);
                transferConstraints(GeoId, PointPos::end, newId1, PointPos::end, true);
            });
    }
    else if (geo->isDerivedFrom(Part::GeomArcOfConic::getClassTypeId())) {
        ok = createGeosFromNonPeriodic(
            static_cast<const Part::GeomBoundedCurve*>(geo),
            [](const Part::GeomCurve* curve, double startParam, double endParam) {
                auto newArc = std::unique_ptr<Part::GeomArcOfConic>(
                    static_cast<Part::GeomArcOfConic*>(curve->copy()));
                newArc->setRange(startParam, endParam);
                return newArc;
            },
            [this, &newConstraints](int GeoId, int newId0, int newId1) {
                // apply appropriate constraints on the new points at split point
                Constraint* joint = new Constraint();
                joint->Type = Coincident;
                joint->First = newId0;
                joint->FirstPos = PointPos::end;
                joint->Second = newId1;
                joint->SecondPos = PointPos::start;
                newConstraints.push_back(joint);

                // TODO: Do we apply constraints on center etc of the conics?

                // transfer constraints from start and end of original
                transferConstraints(GeoId, PointPos::start, newId0, PointPos::start, true);
                transferConstraints(GeoId, PointPos::end, newId1, PointPos::end, true);
            });
    }
    else if (geo->is<Part::GeomBSplineCurve>()) {
        const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

        // what to do for periodic b-splines?
        if (bsp->isPeriodic()) {
            ok = createGeosFromPeriodic(
                static_cast<const Part::GeomCurve*>(geo),
                [](const Part::GeomCurve* curve, double startParam, double endParam) {
                    auto newBsp = std::unique_ptr<Part::GeomBSplineCurve>(
                        static_cast<Part::GeomBSplineCurve*>(curve->copy()));
                    newBsp->Trim(startParam, endParam);
                    return newBsp;
                },
                [](int, int) {
                    // no constraints to transfer here, and we assume the split is to "break" the
                    // b-spline
                });
        }
        else {
            ok = createGeosFromNonPeriodic(
                static_cast<const Part::GeomBoundedCurve*>(geo),
                [](const Part::GeomCurve* curve, double startParam, double endParam) {
                    auto newBsp = std::unique_ptr<Part::GeomBSplineCurve>(
                        static_cast<Part::GeomBSplineCurve*>(curve->copy()));
                    newBsp->Trim(startParam, endParam);
                    return newBsp;
                },
                [this, &newConstraints](int GeoId, int newId0, int newId1) {
                    // apply appropriate constraints on the new points at split point
                    Constraint* joint = new Constraint();
                    joint->Type = Coincident;
                    joint->First = newId0;
                    joint->FirstPos = PointPos::end;
                    joint->Second = newId1;
                    joint->SecondPos = PointPos::start;
                    newConstraints.push_back(joint);

                    // transfer constraints from start and end of original spline
                    transferConstraints(GeoId, PointPos::start, newId0, PointPos::start, true);
                    transferConstraints(GeoId, PointPos::end, newId1, PointPos::end, true);
                });
        }
    }

    if (ok) {
        std::vector<int> oldConstraints;
        getConstraintIndices(GeoId, oldConstraints);

        const auto& allConstraints = this->Constraints.getValues();

        // keep constraints on internal geometries so they are deleted
        // when the old curve is deleted
        oldConstraints.erase(std::remove_if(oldConstraints.begin(),
                                            oldConstraints.end(),
                                            [=](const auto& i) {
                                                return allConstraints[i]->Type == InternalAlignment;
                                            }),
                             oldConstraints.end());

        for (unsigned int i = 0; i < oldConstraints.size(); ++i) {

            Constraint* con = allConstraints[oldConstraints[i]];
            int conId = con->First;
            PointPos conPos = con->FirstPos;
            if (conId == GeoId) {
                conId = con->Second;
                conPos = con->SecondPos;
            }

            bool transferToAll = false;
            switch (con->Type) {
                case Horizontal:
                case Vertical:
                case Parallel: {
                    transferToAll = geo->is<Part::GeomLineSegment>();
                    break;
                }
                case Tangent:
                case Perpendicular: {
                    unsigned int initial = 0;
                    unsigned int limit = newIds.size();

                    if (geo->isDerivedFrom(Part::GeomArcOfConic::getClassTypeId())) {
                        const Part::Geometry* conGeo = getGeometry(conId);

                        if (conGeo && conGeo->isDerivedFrom(Part::GeomCurve::getClassTypeId())) {
                            std::vector<std::pair<Base::Vector3d, Base::Vector3d>> intersections;
                            bool intersects[2];
                            auto* geo1 = getGeometry(newIds[0]);
                            auto* geo2 = getGeometry(newIds[1]);

                            intersects[0] = static_cast<const Part::GeomCurve*>(geo1)->intersect(
                                static_cast<const Part::GeomCurve*>(conGeo), intersections);
                            intersects[1] = static_cast<const Part::GeomCurve*>(geo2)->intersect(
                                static_cast<const Part::GeomCurve*>(conGeo), intersections);

                            initial = longestPart;
                            if (intersects[0] != intersects[1]) {
                                initial = intersects[1] ? 1 : 0;
                            }
                            limit = initial + 1;
                        }
                    }

                    for (unsigned int i = initial; i < limit; ++i) {
                        Constraint* trans = con->copy();
                        trans->substituteIndex(GeoId, newIds[i]);
                        newConstraints.push_back(trans);
                    }
                    break;
                }
                case Distance:
                case DistanceX:
                case DistanceY:
                case PointOnObject: {
                    if (con->FirstPos == PointPos::none && con->SecondPos == PointPos::none) {
                        Constraint* dist = con->copy();
                        dist->First = newIds[0];
                        dist->FirstPos = PointPos::start;
                        dist->Second = newIds[1];
                        dist->SecondPos = PointPos::end;
                        newConstraints.push_back(dist);
                    }
                    else {
                        Constraint* trans = con->copy();
                        trans->First = conId;
                        trans->FirstPos = conPos;
                        trans->SecondPos = PointPos::none;

                        Base::Vector3d conPoint(getPoint(conId, conPos));
                        int targetId = newIds[0];

                        // for non-periodic curves, see if second curve is more appropriate
                        if (geo->is<Part::GeomLineSegment>()) {
                            Base::Vector3d projPoint(
                                conPoint.Perpendicular(startPoint, endPoint - startPoint));
                            Base::Vector3d splitDir = splitPoint - startPoint;
                            if ((projPoint - startPoint) * splitDir > splitDir * splitDir) {
                                targetId = newIds[1];
                            }
                        }
                        else if (geo->isDerivedFrom(Part::GeomArcOfConic::getClassTypeId())) {
                            double conParam;
                            static_cast<const Part::GeomArcOfConic*>(geo)->closestParameter(
                                conPoint, conParam);
                            if (conParam > splitParam)
                                targetId = newIds[1];
                        }
                        trans->Second = targetId;

                        newConstraints.push_back(trans);
                    }
                    break;
                }
                case Radius:
                case Diameter:
                case Equal: {
                    transferToAll = geo->is<Part::GeomCircle>()
                        || geo->is<Part::GeomArcOfCircle>();
                    break;
                }
                default:
                    // Release other constraints
                    break;
            }

            if (transferToAll) {
                for (auto& newId : newIds) {
                    Constraint* trans = con->copy();
                    trans->substituteIndex(GeoId, newId);
                    newConstraints.push_back(trans);
                }
            }
        }

        if (noRecomputes)
            solve();

        delConstraints(oldConstraints);
        addConstraints(newConstraints);
    }

    for (auto& cons : newConstraints) {
        delete cons;
    }

    if (ok) {
        delGeometry(GeoId);
        return 0;
    }

    return -1;
}

int SketchObject::join(int geoId1, Sketcher::PointPos posId1, int geoId2, Sketcher::PointPos posId2)
{
    // No need to check input data validity as this is an sketchobject managed operation

    Base::StateLocker lock(managedoperation, true);

    if (Sketcher::PointPos::start != posId1 && Sketcher::PointPos::end != posId1
        && Sketcher::PointPos::start != posId2 && Sketcher::PointPos::end != posId2) {
        THROWM(ValueError, "Invalid position(s): points must be start or end points of a curve.");
        return -1;
    }

    if (geoId1 == geoId2) {
        THROWM(ValueError, "Connecting the end points of the same curve is not yet supported.");
        return -1;
    }

    if (geoId1 < 0 || geoId1 > getHighestCurveIndex() || geoId2 < 0
        || geoId2 > getHighestCurveIndex()) {
        return -1;
    }

    // get the old splines
    auto* geo1 = dynamic_cast<const Part::GeomCurve*>(getGeometry(geoId1));
    auto* geo2 = dynamic_cast<const Part::GeomCurve*>(getGeometry(geoId2));

    if (GeometryFacade::getConstruction(geo1) != GeometryFacade::getConstruction(geo2)) {
        THROWM(ValueError, "Cannot join construction and non-construction geometries.");
        return -1;
    }

    // TODO: make both curves b-splines here itself
    if (!geo1 || !geo2) {
        return -1;
    }

    // TODO: is there a cleaner way to get our mutable bsp's?
    // we need the splines to be mutable because we may reverse them
    // and/or change their degree
    std::unique_ptr<Part::GeomBSplineCurve> bsp1(
        geo1->toNurbs(geo1->getFirstParameter(), geo1->getLastParameter()));
    std::unique_ptr<Part::GeomBSplineCurve> bsp2(
        geo2->toNurbs(geo2->getFirstParameter(), geo2->getLastParameter()));

    if (bsp1->isPeriodic() || bsp2->isPeriodic()) {
        THROWM(ValueError, "It is only possible to join non-periodic curves.");
        return -1;
    }

    // reverse the splines if needed: join end of 1st to start of 2nd
    if (Sketcher::PointPos::start == posId1)
        bsp1->reverse();
    if (Sketcher::PointPos::end == posId2)
        bsp2->reverse();

    // ensure the degrees of both curves are the same
    if (bsp1->getDegree() < bsp2->getDegree())
        bsp1->increaseDegree(bsp2->getDegree());
    else if (bsp2->getDegree() < bsp1->getDegree())
        bsp2->increaseDegree(bsp1->getDegree());

    // TODO: set up vectors for new poles, knots, mults
    std::vector<Base::Vector3d> poles1 = bsp1->getPoles();
    std::vector<double> weights1 = bsp1->getWeights();
    std::vector<double> knots1 = bsp1->getKnots();
    std::vector<int> mults1 = bsp1->getMultiplicities();
    std::vector<Base::Vector3d> poles2 = bsp2->getPoles();
    std::vector<double> weights2 = bsp2->getWeights();
    std::vector<double> knots2 = bsp2->getKnots();
    std::vector<int> mults2 = bsp2->getMultiplicities();

    std::vector<Base::Vector3d> newPoles(std::move(poles1));
    std::vector<double> newWeights(std::move(weights1));
    std::vector<double> newKnots(std::move(knots1));
    std::vector<int> newMults(std::move(mults1));

    poles2.erase(poles2.begin());
    newPoles.insert(newPoles.end(),
                    std::make_move_iterator(poles2.begin()),
                    std::make_move_iterator(poles2.end()));

    // TODO: Weights might need to be scaled
    weights2.erase(weights2.begin());
    newWeights.insert(newWeights.end(),
                      std::make_move_iterator(weights2.begin()),
                      std::make_move_iterator(weights2.end()));

    // knots of the second spline come after all of the first
    double offset = newKnots.back() - knots2.front();
    knots2.erase(knots2.begin());
    for (auto& knot : knots2)
        knot += offset;
    newKnots.insert(newKnots.end(),
                    std::make_move_iterator(knots2.begin()),
                    std::make_move_iterator(knots2.end()));

    // end knots can have a multiplicity of (degree + 1)
    if (bsp1->getDegree() < newMults.back())
        newMults.back() = bsp1->getDegree();
    mults2.erase(mults2.begin());
    newMults.insert(newMults.end(),
                    std::make_move_iterator(mults2.begin()),
                    std::make_move_iterator(mults2.end()));

    Part::GeomBSplineCurve* newSpline = new Part::GeomBSplineCurve(
        newPoles, newWeights, newKnots, newMults, bsp1->getDegree(), false, true);

    int newGeoId = addGeometry(newSpline);

    if (newGeoId < 0) {
        THROWM(ValueError, "Failed to create joined curve.");
        return -1;
    }
    else {
        exposeInternalGeometry(newGeoId);
        setConstruction(newGeoId, GeometryFacade::getConstruction(geo1));

        // TODO: transfer constraints on the non-connected ends
        auto otherPosId1 = (Sketcher::PointPos::start == posId1) ? Sketcher::PointPos::end
                                                                 : Sketcher::PointPos::start;
        auto otherPosId2 = (Sketcher::PointPos::start == posId2) ? Sketcher::PointPos::end
                                                                 : Sketcher::PointPos::start;

        transferConstraints(geoId1, otherPosId1, newGeoId, PointPos::start, true);
        transferConstraints(geoId2, otherPosId2, newGeoId, PointPos::end, true);

        delGeometries({geoId1, geoId2});
        return 0;
    }

    return -1;
}

int SketchObject::addSymmetric(const std::vector<int>& geoIdList, int refGeoId,
                               Sketcher::PointPos refPosId , bool addSymmetryConstraints )
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& constrvals = this->Constraints.getValues();
    std::vector<Constraint*> newconstrVals(constrvals);

    std::map<int, int> geoIdMap;
    std::map<int, bool> isStartEndInverted;

    // Find out if reference is aligned with V or H axis,
    // if so we can keep Vertical and Horizontal constraints in the mirrored geometry.
    bool refIsLine = refPosId == Sketcher::PointPos::none;
    bool refIsAxisAligned = false;
    if (refGeoId == Sketcher::GeoEnum::VAxis || refGeoId == Sketcher::GeoEnum::HAxis || !refIsLine) {
        refIsAxisAligned = true;
    }
    else {
        for (auto* constr : constrvals) {
            if (constr->First == refGeoId
                && (constr->Type == Sketcher::Vertical || constr->Type == Sketcher::Horizontal)){
                refIsAxisAligned = true;
            }
        }
    }

    std::vector<Part::Geometry*> symgeos = getSymmetric(geoIdList, geoIdMap, isStartEndInverted, refGeoId, refPosId);

    // Perturb geometry to avoid numerical singularities in the solver (Jacobian Rank).
    // If geometry is "perfect", the solver cannot distinguish between the derivative
    // of a Symmetry constraint and an Equal constraint, flagging one as redundant.
    // see https://github.com/FreeCAD/FreeCAD/issues/13551
    // This does not happen with other arcs types.
    if (addSymmetryConstraints) {
        for (auto* geo : symgeos) {
            if (auto* arc = dynamic_cast<Part::GeomArcOfCircle*>(geo)) {
                double start, end;
                arc->getRange(start, end, true);
                arc->setRange(start + Precision::Angular(), end, true);
            }
        }
    }

    {
        addGeometry(symgeos);

        for (auto* constr :  constrvals) {
            // we look in the map, because we might have skipped internal alignment geometry
            auto fit = geoIdMap.find(constr->First);

            if (fit != geoIdMap.end()) {// if First of constraint is in geoIdList
                if (addSymmetryConstraints && constr->Type != Sketcher::InternalAlignment) {
                    // if we are making symmetric constraints, then we don't want to copy all constraints
                    continue;
                }

                if (constr->Second == GeoEnum::GeoUndef ){
                    if (refIsAxisAligned) {
                        // in this case we want to keep the Vertical, Horizontal constraints
                        // DistanceX ,and DistanceY constraints should also be possible to keep in
                        // this case, but keeping them causes segfault, not sure why.

                        if (constr->Type != Sketcher::DistanceX
                            && constr->Type != Sketcher::DistanceY) {
                            Constraint* constNew = constr->copy();
                            constNew->Name = ""; // Make sure we don't have 2 constraint with same name.
                            constNew->First = fit->second;
                            newconstrVals.push_back(constNew);
                        }
                    }
                    else if (constr->Type != Sketcher::DistanceX
                                && constr->Type != Sketcher::DistanceY
                                && constr->Type != Sketcher::Vertical
                                && constr->Type != Sketcher::Horizontal) {
                        // this includes all non-directional single GeoId constraints, as radius,
                        // diameter, weight,...

                        Constraint* constNew = constr->copy();
                        constNew->Name = "";
                        constNew->First = fit->second;
                        newconstrVals.push_back(constNew);
                    }
                }
                else {// other geoids intervene in this constraint

                    auto sit = geoIdMap.find(constr->Second);

                    if (sit != geoIdMap.end()) {// Second is also in the list

                        if (constr->Third == GeoEnum::GeoUndef) {
                            if (constr->Type == Sketcher::Coincident
                                || constr->Type == Sketcher::Perpendicular
                                || constr->Type == Sketcher::Parallel
                                || constr->Type == Sketcher::Tangent
                                || constr->Type == Sketcher::Distance
                                || constr->Type == Sketcher::Equal || constr->Type == Sketcher::Angle
                                || constr->Type == Sketcher::PointOnObject
                                || constr->Type == Sketcher::InternalAlignment) {
                                Constraint* constNew = constr->copy();
                                constNew->Name = "";
                                constNew->First = fit->second;
                                constNew->Second = sit->second;
                                if (isStartEndInverted[constr->First]) {
                                    if (constr->FirstPos == Sketcher::PointPos::start)
                                        constNew->FirstPos = Sketcher::PointPos::end;
                                    else if (constr->FirstPos == Sketcher::PointPos::end)
                                        constNew->FirstPos = Sketcher::PointPos::start;
                                }
                                if (isStartEndInverted[constr->Second]) {
                                    if (constr->SecondPos == Sketcher::PointPos::start)
                                        constNew->SecondPos = Sketcher::PointPos::end;
                                    else if (constr->SecondPos == Sketcher::PointPos::end)
                                        constNew->SecondPos = Sketcher::PointPos::start;
                                }

                                if (constNew->Type == Tangent || constNew->Type == Perpendicular)
                                    AutoLockTangencyAndPerpty(constNew, true);

                                if ((constr->Type == Sketcher::Angle)
                                    && (refPosId == Sketcher::PointPos::none)) {
                                    constNew->setValue(-constr->getValue());
                                }

                                // Signed constraints record which side of a line their subject
                                // sits on, and a symmetry about a *line* reverses that side while
                                // a symmetry about a *point* does not. For the new constraint,
                                // re-derive the sign from the updated geometry instead of trying
                                // to figure it out from the old one.
                                setOrientation(constNew, true);

                                newconstrVals.push_back(constNew);
                            }
                        }
                        else {// three GeoIds intervene in constraint
                            auto tit = geoIdMap.find(constr->Third);

                            if (tit != geoIdMap.end()) {// Third is also in the list
                                Constraint* constNew = constr->copy();
                                constNew->Name = "";
                                constNew->First = fit->second;
                                constNew->Second = sit->second;
                                constNew->Third = tit->second;
                                if (isStartEndInverted[constr->First]) {
                                    if (constr->FirstPos == Sketcher::PointPos::start)
                                        constNew->FirstPos = Sketcher::PointPos::end;
                                    else if (constr->FirstPos == Sketcher::PointPos::end)
                                        constNew->FirstPos = Sketcher::PointPos::start;
                                }
                                if (isStartEndInverted[constr->Second]) {
                                    if (constr->SecondPos == Sketcher::PointPos::start)
                                        constNew->SecondPos = Sketcher::PointPos::end;
                                    else if (constr->SecondPos == Sketcher::PointPos::end)
                                        constNew->SecondPos = Sketcher::PointPos::start;
                                }
                                if (isStartEndInverted[constr->Third]) {
                                    if (constr->ThirdPos == Sketcher::PointPos::start)
                                        constNew->ThirdPos = Sketcher::PointPos::end;
                                    else if (constr->ThirdPos == Sketcher::PointPos::end)
                                        constNew->ThirdPos = Sketcher::PointPos::start;
                                }
                                newconstrVals.push_back(constNew);
                            }
                        }
                    }
                }
            }
        }

        if (addSymmetryConstraints) {
            auto createSymConstr = [&]
            (int first, int second, Sketcher::PointPos firstPos, Sketcher::PointPos secondPos) {
                auto symConstr = new Constraint();
                symConstr->Type = Symmetric;
                symConstr->First = first;
                symConstr->Second = second;
                symConstr->Third = refGeoId;
                symConstr->FirstPos = firstPos;
                symConstr->SecondPos = secondPos;
                symConstr->ThirdPos = refPosId;
                newconstrVals.push_back(symConstr);
            };
            auto createEqualityConstr = [&]
            (int first, int second) {
                auto symConstr = new Constraint();
                symConstr->Type = Equal;
                symConstr->First = first;
                symConstr->Second = second;
                newconstrVals.push_back(symConstr);
            };

            for (auto geoIdPair : geoIdMap) {
                int geoId1 = geoIdPair.first;
                int geoId2 = geoIdPair.second;
                const Part::Geometry* geo = getGeometry(geoId1);

                if (geo->is<Part::GeomLineSegment>()) {
                    auto gf = GeometryFacade::getFacade(geo);
                    if (!gf->isInternalAligned()) {
                        // Note internal aligned lines (ellipse, parabola, hyperbola) are causing redundant constraint.
                        createSymConstr(geoId1, geoId2, PointPos::start, isStartEndInverted[geoId1] ? PointPos::end : PointPos::start);
                        createSymConstr(geoId1, geoId2, PointPos::end, isStartEndInverted[geoId1] ? PointPos::start : PointPos::end);
                    }
                }
                else if (geo->is<Part::GeomCircle>() || geo->is<Part::GeomEllipse>()) {
                    createEqualityConstr(geoId1, geoId2);
                    createSymConstr(geoId1, geoId2, PointPos::mid, PointPos::mid);
                }
                else if (geo->is<Part::GeomArcOfCircle>()
                    || geo->is<Part::GeomArcOfEllipse>()
                    || geo->is<Part::GeomArcOfHyperbola>()
                    || geo->is<Part::GeomArcOfParabola>()) {
                    createEqualityConstr(geoId1, geoId2);
                    createSymConstr(geoId1, geoId2, PointPos::start, isStartEndInverted[geoId1] ? PointPos::end : PointPos::start);
                    createSymConstr(geoId1, geoId2, PointPos::end, isStartEndInverted[geoId1] ? PointPos::start : PointPos::end);
                }
                else if (geo->is<Part::GeomPoint>()) {
                    auto gf = GeometryFacade::getFacade(geo);
                    if (!gf->isInternalAligned()) {
                        createSymConstr(geoId1, geoId2, PointPos::start, PointPos::start);
                    }
                }
                // Note bspline has symmetric by the internal aligned circles.
            }
        }

        if (newconstrVals.size() > constrvals.size()){
            Constraints.setValues(std::move(newconstrVals));
        }
    }

    // we delayed update, so trigger it now.
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    return Geometry.getSize() - 1;
}

std::vector<Part::Geometry*> SketchObject::getSymmetric(const std::vector<int>& geoIdList,
    std::map<int, int>& geoIdMap,
    std::map<int, bool>& isStartEndInverted,
    int refGeoId,
    Sketcher::PointPos refPosId)
{
    std::vector<Part::Geometry*> symmetricVals;
    bool refIsLine = refPosId == Sketcher::PointPos::none;
    int cgeoid = getHighestCurveIndex() + 1;

    auto shouldCopyGeometry = [&](auto* geo, int geoId) -> bool {
        auto gf = GeometryFacade::getFacade(geo);
        if (gf->isInternalAligned()) {
            // only add if the corresponding geometry it defines is also in the list.
            int definedGeo = GeoEnum::GeoUndef;
            for (auto c : Constraints.getValues()) {
                if (c->Type == Sketcher::InternalAlignment && c->First == geoId) {
                    definedGeo = c->Second;
                    break;
                }
            }
            // Return true if definedGeo is in geoIdList, false otherwise
            return std::find(geoIdList.begin(), geoIdList.end(), definedGeo) != geoIdList.end();
        }
        // Return true if not internal aligned, indicating it should always be copied
        return true;
    };

    if (refIsLine) {
        const Part::Geometry* georef = getGeometry(refGeoId);
        if (!georef->is<Part::GeomLineSegment>()) {
            return {};
        }

        auto* refGeoLine = static_cast<const Part::GeomLineSegment*>(georef);
        // line
        Base::Vector3d refstart = refGeoLine->getStartPoint();
        Base::Vector3d vectline = refGeoLine->getEndPoint() - refstart;

        for (auto geoId : geoIdList) {
            const Part::Geometry* geo = getGeometry(geoId);
            Part::Geometry* geosym;

            if (!shouldCopyGeometry(geo, geoId)) {
                continue;
            }

            geosym = geo->copy();

            // Handle Geometry
            if (geosym->is<Part::GeomLineSegment>()) {
                auto* geosymline = static_cast<Part::GeomLineSegment*>(geosym);
                Base::Vector3d sp = geosymline->getStartPoint();
                Base::Vector3d ep = geosymline->getEndPoint();

                geosymline->setPoints(
                    sp + 2.0 * (sp.Perpendicular(refGeoLine->getStartPoint(), vectline) - sp),
                    ep + 2.0 * (ep.Perpendicular(refGeoLine->getStartPoint(), vectline) - ep));
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomCircle>()) {
                auto* geosymcircle = static_cast<Part::GeomCircle*>(geosym);
                Base::Vector3d cp = geosymcircle->getCenter();

                geosymcircle->setCenter(
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp));
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfCircle>()) {
                auto* geoaoc = static_cast<Part::GeomArcOfCircle*>(geosym);
                Base::Vector3d sp = geoaoc->getStartPoint(true);
                Base::Vector3d ep = geoaoc->getEndPoint(true);
                Base::Vector3d cp = geoaoc->getCenter();

                Base::Vector3d ssp =
                    sp + 2.0 * (sp.Perpendicular(refGeoLine->getStartPoint(), vectline) - sp);
                Base::Vector3d sep =
                    ep + 2.0 * (ep.Perpendicular(refGeoLine->getStartPoint(), vectline) - ep);
                Base::Vector3d scp =
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                double theta1 = Base::fmod(atan2(sep.y - scp.y, sep.x - scp.x), 2.f * M_PI);
                double theta2 = Base::fmod(atan2(ssp.y - scp.y, ssp.x - scp.x), 2.f * M_PI);

                geoaoc->setCenter(scp);
                geoaoc->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomEllipse>()) {
                auto* geosymellipse = static_cast<Part::GeomEllipse*>(geosym);
                Base::Vector3d cp = geosymellipse->getCenter();

                Base::Vector3d majdir = geosymellipse->getMajorAxisDir();
                double majord = geosymellipse->getMajorRadius();
                double minord = geosymellipse->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 =
                    f1 + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp =
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymellipse->setMajorAxisDir(sf1 - scp);

                geosymellipse->setCenter(scp);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfEllipse>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfEllipse*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 =
                    f1 + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp =
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = 2.0 * M_PI - theta1;
                theta2 = 2.0 * M_PI - theta2;
                std::swap(theta1, theta2);
                if (theta1 < 0) {
                    theta1 += 2.0 * M_PI;
                    theta2 += 2.0 * M_PI;
                }

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomArcOfHyperbola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfHyperbola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord + minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 =
                    f1 + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp =
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = -theta1;
                theta2 = -theta2;
                std::swap(theta1, theta2);

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomArcOfParabola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfParabola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d f1 = geosymaoe->getFocus();

                Base::Vector3d sf1 =
                    f1 + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp =
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setXAxisDir(sf1 - scp);
                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = -theta1;
                theta2 = -theta2;
                std::swap(theta1, theta2);

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomBSplineCurve>()) {
                auto* geosymbsp = static_cast<Part::GeomBSplineCurve*>(geosym);

                std::vector<Base::Vector3d> poles = geosymbsp->getPoles();

                for (auto& pole : poles) {
                    pole = pole
                        + 2.0 * (pole.Perpendicular(refGeoLine->getStartPoint(), vectline) - pole);
                }

                geosymbsp->setPoles(poles);

                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomPoint>()) {
                auto* geosympoint = static_cast<Part::GeomPoint*>(geosym);
                Base::Vector3d cp = geosympoint->getPoint();

                geosympoint->setPoint(
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp));
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else {
                Base::Console().Error("Unsupported Geometry!! Just copying it.\n");
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }

            symmetricVals.push_back(geosym);
            geoIdMap.insert(std::make_pair(geoId, cgeoid));
            cgeoid++;
        }
    }
    else {// reference is a point
        Vector3d refpoint;
        const Part::Geometry* georef = getGeometry(refGeoId);

        if (georef->is<Part::GeomPoint>()) {
            refpoint = static_cast<const Part::GeomPoint*>(georef)->getPoint();
        }
        else if (refGeoId == -1 && refPosId == Sketcher::PointPos::start) {
            refpoint = Vector3d(0, 0, 0);
        }
        else {
            if (refPosId == Sketcher::PointPos::none) {
                Base::Console().Error("Wrong PointPosId.\n");
                return {};
            }
            refpoint = getPoint(georef, refPosId);
        }

        for (auto geoId : geoIdList) {
            const Part::Geometry* geo = getGeometry(geoId);
            Part::Geometry* geosym;

            if (!shouldCopyGeometry(geo, geoId)) {
                continue;
            }

            geosym = geo->copy();

            // Handle Geometry
            if (geosym->is<Part::GeomLineSegment>()) {
                auto* geosymline = static_cast<Part::GeomLineSegment*>(geosym);
                Base::Vector3d sp = geosymline->getStartPoint();
                Base::Vector3d ep = geosymline->getEndPoint();
                Base::Vector3d ssp = sp + 2.0 * (refpoint - sp);
                Base::Vector3d sep = ep + 2.0 * (refpoint - ep);

                geosymline->setPoints(ssp, sep);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomCircle>()) {
                auto* geosymcircle = static_cast<Part::GeomCircle*>(geosym);
                Base::Vector3d cp = geosymcircle->getCenter();

                geosymcircle->setCenter(cp + 2.0 * (refpoint - cp));
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfCircle>()) {
                auto* geoaoc = static_cast<Part::GeomArcOfCircle*>(geosym);
                Base::Vector3d sp = geoaoc->getStartPoint(true);
                Base::Vector3d ep = geoaoc->getEndPoint(true);
                Base::Vector3d cp = geoaoc->getCenter();

                Base::Vector3d ssp = sp + 2.0 * (refpoint - sp);
                Base::Vector3d sep = ep + 2.0 * (refpoint - ep);
                Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

                double theta1 = Base::fmod(atan2(ssp.y - scp.y, ssp.x - scp.x), 2.f * M_PI);
                double theta2 = Base::fmod(atan2(sep.y - scp.y, sep.x - scp.x), 2.f * M_PI);

                geoaoc->setCenter(scp);
                geoaoc->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomEllipse>()) {
                auto* geosymellipse = static_cast<Part::GeomEllipse*>(geosym);
                Base::Vector3d cp = geosymellipse->getCenter();

                Base::Vector3d majdir = geosymellipse->getMajorAxisDir();
                double majord = geosymellipse->getMajorRadius();
                double minord = geosymellipse->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
                Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

                geosymellipse->setMajorAxisDir(sf1 - scp);

                geosymellipse->setCenter(scp);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfEllipse>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfEllipse*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
                Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfHyperbola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfHyperbola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord + minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
                Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfParabola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfParabola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();
                Base::Vector3d f1 = geosymaoe->getFocus();

                Base::Vector3d sf1 = f1 + 2.0 * (refpoint - f1);
                Base::Vector3d scp = cp + 2.0 * (refpoint - cp);

                geosymaoe->setXAxisDir(sf1 - scp);
                geosymaoe->setCenter(scp);

                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomBSplineCurve>()) {
                auto* geosymbsp = static_cast<Part::GeomBSplineCurve*>(geosym);

                std::vector<Base::Vector3d> poles = geosymbsp->getPoles();

                for (auto& pole : poles) {
                    pole = pole + 2.0 * (refpoint - pole);
                }

                geosymbsp->setPoles(poles);

            }
            else if (geosym->is<Part::GeomPoint>()) {
                auto* geosympoint = static_cast<Part::GeomPoint*>(geosym);
                Base::Vector3d cp = geosympoint->getPoint();

                geosympoint->setPoint(cp + 2.0 * (refpoint - cp));
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else {
                Base::Console().Error("Unsupported Geometry!! Just copying it.\n");
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }

            symmetricVals.push_back(geosym);
            geoIdMap.insert(std::make_pair(geoId, cgeoid));
            cgeoid++;
        }
    }
    return symmetricVals;
}

int SketchObject::addCopy(const std::vector<int>& geoIdList, const Base::Vector3d& displacement,
                          bool moveonly /*=false*/, bool clone /*=false*/, int csize /*=2*/,
                          int rsize /*=1*/, bool constraindisplacement /*= false*/,
                          double perpscale /*= 1.0*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Part::Geometry*>& geovals = getInternalGeometry();
    std::vector<Part::Geometry*> newgeoVals(geovals);

    const std::vector<Constraint*>& constrvals = this->Constraints.getValues();
    std::vector<Constraint*> newconstrVals(constrvals);

    if (!moveonly) {
        newgeoVals.reserve(geovals.size() + geoIdList.size());
    }

    std::vector<int> newgeoIdList;

    if (geoIdList.empty()) {// default option to operate on all the geometry
        for (int i = 0; i < int(geovals.size()); i++)
            newgeoIdList.push_back(i);
    }
    else {
        for (const auto &GeoId : geoIdList) {
            if ((GeoId >= 0 && GeoId >= static_cast<int>(geovals.size()))
                  || (GeoId < 0 && -GeoId-1 >= ExternalGeo.getSize())) {
                FC_ERR("Invalid geometry index");
                break;
            }
            else if (moveonly && GeoId < 0) {
                FC_ERR("Cannot move external geometry");
                break;
            }
            newgeoIdList.push_back(GeoId);
        }
        if (newgeoIdList.size() != geoIdList.size()) {
            return Geometry.getSize() - 1;
        }
    }


    int cgeoid = getHighestCurveIndex() + 1;

    int iterfirstgeoid = -1;

    Base::Vector3d iterfirstpoint;

    int refgeoid = -1;

    int colrefgeoid = 0, rowrefgeoid = 0;

    int currentrowfirstgeoid = -1, prevrowstartfirstgeoid = -1, prevfirstgeoid = -1;

    Sketcher::PointPos refposId = Sketcher::PointPos::none;

    std::map<int, int> geoIdMap;

    Base::Vector3d perpendicularDisplacement =
        Base::Vector3d(perpscale * displacement.y, perpscale * -displacement.x, 0);

    int x, y;

    for (y = 0; y < rsize; y++) {
        for (x = 0; x < csize; x++) {
            // the reference for constraining array elements is the first valid point of the first
            // element
            if (x == 0 && y == 0) {
                const Part::Geometry* geo = getGeometry(*(newgeoIdList.begin()));

                auto gf = GeometryFacade::getFacade(geo);

                if (gf->isInternalAligned() && !moveonly) {
                    // only add this geometry if the corresponding geometry it defines is also in
                    // the list.
                    int definedGeo = GeoEnum::GeoUndef;

                    for (auto c : Constraints.getValues()) {
                        if (c->Type == Sketcher::InternalAlignment
                            && c->First == *(newgeoIdList.begin())) {
                            definedGeo = c->Second;
                            break;
                        }
                    }

                    if (std::find(newgeoIdList.begin(), newgeoIdList.end(), definedGeo)
                        == newgeoIdList.end()) {
                        // the first element setting the reference is an internal alignment
                        // geometry, wherein the geometry it defines is not part of the copy
                        // operation.
                        THROWM(Base::ValueError,
                               "A move/copy/array operation on an internal alignment geometry is "
                               "only possible together with the geometry it defines.")
                    }
                }

                refgeoid = *(newgeoIdList.begin());
                currentrowfirstgeoid = refgeoid;
                iterfirstgeoid = refgeoid;
                if (geo->is<Part::GeomCircle>()
                    || geo->is<Part::GeomEllipse>()) {
                    refposId = Sketcher::PointPos::mid;
                }
                else
                    refposId = Sketcher::PointPos::start;

                continue;// the first element is already in place
            }
            else {
                prevfirstgeoid = iterfirstgeoid;

                iterfirstgeoid = cgeoid;

                if (x == 0) {// if first element of second row
                    prevrowstartfirstgeoid = currentrowfirstgeoid;
                    currentrowfirstgeoid = cgeoid;
                }
            }

            int index = 0;
            for (std::vector<int>::const_iterator it = newgeoIdList.begin();
                 it != newgeoIdList.end();
                 ++it, index++) {
                const Part::Geometry* geo = getGeometry(*it);

                Part::Geometry* geocopy;

                auto gf = GeometryFacade::getFacade(geo);

                if (gf->isInternalAligned() && !moveonly) {
                    // only add this geometry if the corresponding geometry it defines is also in
                    // the list.
                    int definedGeo = GeoEnum::GeoUndef;

                    for (auto c : Constraints.getValues()) {
                        if (c->Type == Sketcher::InternalAlignment && c->First == *it) {
                            definedGeo = c->Second;
                            break;
                        }
                    }

                    if (std::find(newgeoIdList.begin(), newgeoIdList.end(), definedGeo)
                        == newgeoIdList.end()) {
                        // we should not copy internal alignment geometry, unless the element they
                        // define is also mirrored
                        continue;
                    }
                }

                // We have already cloned all geometry and constraints, we only need a copy if not
                // moving
                if (!moveonly) {
                    geocopy = geo->copy();
                    if (auto egf = ExternalGeometryFacade::getFacade(geo)) {
                        if(egf->testFlag(ExternalGeometryExtension::Defining)) {
                            GeometryFacade::setConstruction(geocopy, false);
                        }
                        geocopy->deleteExtension(ExternalGeometryExtension::getClassTypeId());
                    }
                    generateId(geocopy);
                } else
                    geocopy = newgeoVals[*it];

                // Handle Geometry
                if (geocopy->is<Part::GeomLineSegment>()) {
                    Part::GeomLineSegment* geosymline =
                        static_cast<Part::GeomLineSegment*>(geocopy);
                    Base::Vector3d ep = geosymline->getEndPoint();
                    Base::Vector3d ssp = geosymline->getStartPoint() + double(x) * displacement
                        + double(y) * perpendicularDisplacement;

                    geosymline->setPoints(
                        ssp, ep + double(x) * displacement + double(y) * perpendicularDisplacement);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = ssp;
                }
                else if (geocopy->is<Part::GeomCircle>()) {
                    Part::GeomCircle* geosymcircle = static_cast<Part::GeomCircle*>(geocopy);
                    Base::Vector3d cp = geosymcircle->getCenter();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;

                    geosymcircle->setCenter(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = scp;
                }
                else if (geocopy->is<Part::GeomArcOfCircle>()) {
                    Part::GeomArcOfCircle* geoaoc = static_cast<Part::GeomArcOfCircle*>(geocopy);
                    Base::Vector3d cp = geoaoc->getCenter();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;

                    geoaoc->setCenter(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = geoaoc->getStartPoint(true);
                }
                else if (geocopy->is<Part::GeomEllipse>()) {
                    Part::GeomEllipse* geosymellipse = static_cast<Part::GeomEllipse*>(geocopy);
                    Base::Vector3d cp = geosymellipse->getCenter();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;

                    geosymellipse->setCenter(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = scp;
                }
                else if (geocopy->is<Part::GeomArcOfEllipse>()) {
                    Part::GeomArcOfEllipse* geoaoe = static_cast<Part::GeomArcOfEllipse*>(geocopy);
                    Base::Vector3d cp = geoaoe->getCenter();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;

                    geoaoe->setCenter(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = geoaoe->getStartPoint(true);
                }
                else if (geocopy->is<Part::GeomArcOfHyperbola>()) {
                    Part::GeomArcOfHyperbola* geoaoe =
                        static_cast<Part::GeomArcOfHyperbola*>(geocopy);
                    Base::Vector3d cp = geoaoe->getCenter();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;

                    geoaoe->setCenter(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = geoaoe->getStartPoint(true);
                }
                else if (geocopy->is<Part::GeomArcOfParabola>()) {
                    Part::GeomArcOfParabola* geoaoe =
                        static_cast<Part::GeomArcOfParabola*>(geocopy);
                    Base::Vector3d cp = geoaoe->getCenter();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;

                    geoaoe->setCenter(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = geoaoe->getStartPoint(true);
                }
                else if (geocopy->is<Part::GeomBSplineCurve>()) {
                    Part::GeomBSplineCurve* geobsp = static_cast<Part::GeomBSplineCurve*>(geocopy);

                    std::vector<Base::Vector3d> poles = geobsp->getPoles();

                    for (std::vector<Base::Vector3d>::iterator jt = poles.begin();
                         jt != poles.end();
                         ++jt) {

                        (*jt) = (*jt) + double(x) * displacement
                            + double(y) * perpendicularDisplacement;
                    }

                    geobsp->setPoles(poles);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = geobsp->getStartPoint();
                }
                else if (geocopy->is<Part::GeomPoint>()) {
                    Part::GeomPoint* geopoint = static_cast<Part::GeomPoint*>(geocopy);
                    Base::Vector3d cp = geopoint->getPoint();
                    Base::Vector3d scp =
                        cp + double(x) * displacement + double(y) * perpendicularDisplacement;
                    geopoint->setPoint(scp);

                    if (it == newgeoIdList.begin())
                        iterfirstpoint = scp;
                }
                else {
                    Base::Console().Error("Unsupported Geometry!! Just skipping it.\n");
                    continue;
                }

                if (!moveonly) {// we are copying
                    newgeoVals.push_back(geocopy);
                    geoIdMap.insert(std::make_pair(*it, cgeoid));
                    cgeoid++;
                }
            }

            if (!moveonly) {
                // handle geometry constraints
                for (std::vector<Constraint*>::const_iterator it = constrvals.begin();
                     it != constrvals.end();
                     ++it) {

                    auto fit = geoIdMap.find((*it)->First);

                    if (fit != geoIdMap.end()) {// if First of constraint is in geoIdList

                        if ((*it)->Second
                            == GeoEnum::GeoUndef /*&& (*it)->Third == GeoEnum::GeoUndef*/) {
                            if (((*it)->Type != Sketcher::DistanceX
                                 && (*it)->Type != Sketcher::DistanceY)
                                || (*it)->FirstPos == Sketcher::PointPos::none) {
                                // if it is not a point locking DistanceX/Y
                                if (((*it)->Type == Sketcher::DistanceX
                                     || (*it)->Type == Sketcher::DistanceY
                                     || (*it)->Type == Sketcher::Distance
                                     || (*it)->Type == Sketcher::Diameter
                                     || (*it)->Type == Sketcher::Weight
                                     || (*it)->Type == Sketcher::Radius)
                                    && clone) {
                                    // Distances on a single Element are mapped to equality
                                    // constraints in clone mode
                                    Constraint* constNew = (*it)->copy();
                                    constNew->Type = Sketcher::Equal;
                                    constNew->isDriving = true;
                                    // first is already (*it->First)
                                    constNew->Second = fit->second;
                                    newconstrVals.push_back(constNew);
                                }
                                else if ((*it)->Type == Sketcher::Angle && clone) {
                                    if (getGeometry((*it)->First)->is<Part::GeomLineSegment>()) {
                                        // Angles on a single Element are mapped to parallel
                                        // constraints in clone mode
                                        Constraint* constNew = (*it)->copy();
                                        constNew->Type = Sketcher::Parallel;
                                        constNew->isDriving = true;
                                        // first is already (*it->First)
                                        constNew->Second = fit->second;
                                        newconstrVals.push_back(constNew);
                                    }
                                }
                                else {
                                    Constraint* constNew = (*it)->copy();
                                    constNew->First = fit->second;
                                    newconstrVals.push_back(constNew);
                                }
                            }
                        }
                        else {// other geoids intervene in this constraint

                            auto sit = geoIdMap.find((*it)->Second);

                            if (sit != geoIdMap.end()) {// Second is also in the list
                                if ((*it)->Third == GeoEnum::GeoUndef) {
                                    if (((*it)->Type == Sketcher::DistanceX
                                         || (*it)->Type == Sketcher::DistanceY
                                         || (*it)->Type == Sketcher::Distance)
                                        && ((*it)->First == (*it)->Second) && clone) {
                                        // Distances on a two Elements, which must be points of the
                                        // same line are mapped to equality constraints in clone
                                        // mode
                                        Constraint* constNew = (*it)->copy();
                                        constNew->Type = Sketcher::Equal;
                                        constNew->isDriving = true;
                                        constNew->FirstPos = Sketcher::PointPos::none;
                                        // first is already (*it->First)
                                        constNew->Second = fit->second;
                                        constNew->SecondPos = Sketcher::PointPos::none;
                                        newconstrVals.push_back(constNew);
                                    }
                                    else {// this includes InternalAlignment constraints
                                        Constraint* constNew = (*it)->copy();
                                        constNew->First = fit->second;
                                        constNew->Second = sit->second;
                                        newconstrVals.push_back(constNew);
                                    }
                                }
                                else {
                                    auto tit = geoIdMap.find((*it)->Third);

                                    if (tit != geoIdMap.end()) {// Third is also in the list
                                        Constraint* constNew = (*it)->copy();
                                        constNew->First = fit->second;
                                        constNew->Second = sit->second;
                                        constNew->Third = tit->second;

                                        newconstrVals.push_back(constNew);
                                    }
                                }
                            }
                        }
                    }
                }

                // handle inter-geometry constraints
                if (constraindisplacement) {

                    // add a construction line
                    Part::GeomLineSegment* constrline = new Part::GeomLineSegment();

                    // position of the reference point
                    Base::Vector3d sp = getPoint(refgeoid, refposId)
                        + ((x == 0) ? (double(x) * displacement
                                       + double(y - 1) * perpendicularDisplacement)
                                    : (double(x - 1) * displacement
                                       + double(y) * perpendicularDisplacement));

                    // position of the current instance corresponding point
                    Base::Vector3d ep = iterfirstpoint;
                    constrline->setPoints(sp, ep);
                    GeometryFacade::setConstruction(constrline, true);

                    generateId(constrline);
                    newgeoVals.push_back(constrline);

                    Constraint* constNew;

                    if (x == 0) {// first element of a row

                        // add coincidents for construction line
                        constNew = new Constraint();
                        constNew->Type = Sketcher::Coincident;
                        constNew->First = prevrowstartfirstgeoid;
                        constNew->FirstPos = refposId;
                        constNew->Second = cgeoid;
                        constNew->SecondPos = Sketcher::PointPos::start;
                        newconstrVals.push_back(constNew);

                        constNew = new Constraint();
                        constNew->Type = Sketcher::Coincident;
                        constNew->First = iterfirstgeoid;
                        constNew->FirstPos = refposId;
                        constNew->Second = cgeoid;
                        constNew->SecondPos = Sketcher::PointPos::end;
                        newconstrVals.push_back(constNew);

                        // it is the first added element of this row in the perpendicular to
                        // displacementvector direction
                        if (y == 1) {
                            rowrefgeoid = cgeoid;
                            cgeoid++;

                            // add length (or equal if perpscale==1) and perpendicular
                            if (perpscale == 1.0) {
                                constNew = new Constraint();
                                constNew->Type = Sketcher::Equal;
                                constNew->First = rowrefgeoid;
                                constNew->FirstPos = Sketcher::PointPos::none;
                                constNew->Second = colrefgeoid;
                                constNew->SecondPos = Sketcher::PointPos::none;
                                newconstrVals.push_back(constNew);
                            }
                            else {
                                constNew = new Constraint();
                                constNew->Type = Sketcher::Distance;
                                constNew->First = rowrefgeoid;
                                constNew->FirstPos = Sketcher::PointPos::none;
                                constNew->setValue(perpendicularDisplacement.Length());
                                newconstrVals.push_back(constNew);
                            }

                            constNew = new Constraint();
                            constNew->Type = Sketcher::Perpendicular;
                            constNew->First = rowrefgeoid;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->Second = colrefgeoid;
                            constNew->SecondPos = Sketcher::PointPos::none;
                            newconstrVals.push_back(constNew);
                        }
                        else {// it is just one more element in the col direction
                            cgeoid++;

                            // all other first rowers get an equality and perpendicular constraint
                            constNew = new Constraint();
                            constNew->Type = Sketcher::Equal;
                            constNew->First = rowrefgeoid;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->Second = cgeoid - 1;
                            constNew->SecondPos = Sketcher::PointPos::none;
                            newconstrVals.push_back(constNew);

                            constNew = new Constraint();
                            constNew->Type = Sketcher::Perpendicular;
                            constNew->First = cgeoid - 1;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->Second = colrefgeoid;
                            constNew->SecondPos = Sketcher::PointPos::none;
                            newconstrVals.push_back(constNew);
                        }
                    }
                    else {// any element not being the first element of a row

                        // add coincidents for construction line
                        constNew = new Constraint();
                        constNew->Type = Sketcher::Coincident;
                        constNew->First = prevfirstgeoid;
                        constNew->FirstPos = refposId;
                        constNew->Second = cgeoid;
                        constNew->SecondPos = Sketcher::PointPos::start;
                        newconstrVals.push_back(constNew);

                        constNew = new Constraint();
                        constNew->Type = Sketcher::Coincident;
                        constNew->First = iterfirstgeoid;
                        constNew->FirstPos = refposId;
                        constNew->Second = cgeoid;
                        constNew->SecondPos = Sketcher::PointPos::end;
                        newconstrVals.push_back(constNew);

                        if (y == 0 && x == 1) {// first element of the first row
                            colrefgeoid = cgeoid;
                            cgeoid++;

                            // add length and Angle
                            constNew = new Constraint();
                            constNew->Type = Sketcher::Distance;
                            constNew->First = colrefgeoid;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->setValue(displacement.Length());
                            newconstrVals.push_back(constNew);

                            constNew = new Constraint();
                            constNew->Type = Sketcher::Angle;
                            constNew->First = colrefgeoid;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->setValue(atan2(displacement.y, displacement.x));
                            newconstrVals.push_back(constNew);
                        }
                        else {// any other element
                            cgeoid++;

                            // all other elements get an equality and parallel constraint
                            constNew = new Constraint();
                            constNew->Type = Sketcher::Equal;
                            constNew->First = colrefgeoid;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->Second = cgeoid - 1;
                            constNew->SecondPos = Sketcher::PointPos::none;
                            newconstrVals.push_back(constNew);

                            constNew = new Constraint();
                            constNew->Type = Sketcher::Parallel;
                            constNew->First = cgeoid - 1;
                            constNew->FirstPos = Sketcher::PointPos::none;
                            constNew->Second = colrefgeoid;
                            constNew->SecondPos = Sketcher::PointPos::none;
                            newconstrVals.push_back(constNew);
                        }
                    }
                }
                // after each creation reset map so that the key-value is univoque (only for
                // operations other than move)
                geoIdMap.clear();
            }
        }
    }

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);
        Geometry.setValues(std::move(newgeoVals));

        if (newconstrVals.size() > constrvals.size())
            Constraints.setValues(std::move(newconstrVals));
    }

    // we inhibited update, so we trigger it now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    return Geometry.getSize() - 1;
}

bool SketchObject::convertToNURBS(int GeoId)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId > getHighestCurveIndex()
        || (GeoId < 0 && -GeoId > ExternalGeo.getSize())
        || GeoId == -1
        || GeoId == -2)
        return false;

    const Part::Geometry* geo = getGeometry(GeoId);

    if (geo->is<Part::GeomPoint>())
        return false;

    const Part::GeomCurve* geo1 = static_cast<const Part::GeomCurve*>(geo);

    Part::GeomBSplineCurve* bspline;

    try {
        bspline = geo1->toNurbs(geo1->getFirstParameter(), geo1->getLastParameter());

        if (geo1->isDerivedFrom(Part::GeomArcOfConic::getClassTypeId())) {
            const Part::GeomArcOfConic* geoaoc = static_cast<const Part::GeomArcOfConic*>(geo1);

            if (geoaoc->isReversed())
                bspline->reverse();
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n", e.what());
        // revert to original values
        return false;
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    // Block checks and updates in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);

        if (GeoId < 0) {// external geometry
            newVals.push_back(bspline);
            generateId(bspline);
        }
        else {// normal geometry

            newVals[GeoId] = bspline;
            GeometryFacade::copyId(geo, bspline);

            const std::vector<Sketcher::Constraint*>& cvals = Constraints.getValues();

            std::vector<Constraint*> newcVals(cvals);

            int index = cvals.size() - 1;
            // delete constraints on this elements other than coincident constraints (bspline does
            // not support them currently), except for coincidents on mid point of the
            // to-be-converted curve.
            for (; index >= 0; index--) {
                auto otherthancoincident = cvals[index]->Type != Sketcher::Coincident
                    && (cvals[index]->First == GeoId || cvals[index]->Second == GeoId
                        || cvals[index]->Third == GeoId);

                auto coincidentonmidpoint = cvals[index]->Type == Sketcher::Coincident
                    && ((cvals[index]->First == GeoId
                         && cvals[index]->FirstPos == Sketcher::PointPos::mid)
                        || (cvals[index]->Second == GeoId
                            && cvals[index]->SecondPos == Sketcher::PointPos::mid));

                if (otherthancoincident || coincidentonmidpoint)
                    newcVals.erase(newcVals.begin() + index);
            }

            this->Constraints.setValues(std::move(newcVals));
        }

        Geometry.setValues(std::move(newVals));
    }

    // trigger update now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    Geometry.touch();

    return true;
}

bool SketchObject::increaseBSplineDegree(int GeoId, int degreeincrement /*= 1*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return false;

    const Part::Geometry* geo = getGeometry(GeoId);

    if (geo->getTypeId() != Part::GeomBSplineCurve::getClassTypeId())
        return false;

    const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    const Handle(Geom_BSplineCurve) curve = Handle(Geom_BSplineCurve)::DownCast(bsp->handle());

    std::unique_ptr<Part::GeomBSplineCurve> bspline(new Part::GeomBSplineCurve(curve));

    try {
        int cdegree = bspline->getDegree();

        bspline->increaseDegree(cdegree + degreeincrement);
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n", e.what());
        return false;
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    GeometryFacade::copyId(geo, bspline.get());
    newVals[GeoId] = bspline.release();

    // AcceptGeometry called from onChanged
    Geometry.setValues(std::move(newVals));

    return true;
}

bool SketchObject::decreaseBSplineDegree(int GeoId, int degreedecrement /*= 1*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        return false;

    const Part::Geometry* geo = getGeometry(GeoId);

    if (geo->getTypeId() != Part::GeomBSplineCurve::getClassTypeId())
        return false;

    const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    const Handle(Geom_BSplineCurve) curve = Handle(Geom_BSplineCurve)::DownCast(bsp->handle());

    std::unique_ptr<Part::GeomBSplineCurve> bspline(new Part::GeomBSplineCurve(curve));

    try {
        int cdegree = bspline->getDegree();

        // degree must be >= 1
        int maxdegree = cdegree - degreedecrement;
        if (maxdegree == 0)
            return false;
        // approximate() now reports failure by throwing; keep this path silent.
        try {
            bspline->approximate(Precision::Confusion(), 20, maxdegree, GeomAbs_C0);
        }
        catch (const Base::CADKernelError&) {
            return false;
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n", e.what());
        return false;
    }

    // FIXME: Avoid to delete the whole geometry but only delete invalid constraints
    // and unused construction geometries
#if 0
    const std::vector< Part::Geometry * > &vals = getInternalGeometry();

    std::vector< Part::Geometry * > newVals(vals);

    newVals[GeoId] = bspline.release();

    // AcceptGeometry called from onChanged
    Geometry.setValues(newVals);
#else
    delGeometry(GeoId);
    int newId = addGeometry(bspline.release());
    exposeInternalGeometry(newId);
#endif

    return true;
}

bool SketchObject::modifyBSplineKnotMultiplicity(int GeoId, int knotIndex, int multiplicityincr)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        THROWMT(Base::ValueError,
                QT_TRANSLATE_NOOP("Exceptions", "BSpline Geometry Index (GeoID) is out of bounds."))

    if (multiplicityincr == 0)// no change in multiplicity
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "You are requesting no change in knot multiplicity."))

    const Part::Geometry* geo = getGeometry(GeoId);

    if (geo->getTypeId() != Part::GeomBSplineCurve::getClassTypeId())
        THROWMT(Base::TypeError,
                QT_TRANSLATE_NOOP("Exceptions",
                                  "The Geometry Index (GeoId) provided is not a B-spline curve."))

    const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    int degree = bsp->getDegree();

    if (knotIndex > bsp->countKnots() || knotIndex < 1)// knotindex in OCC 1 -> countKnots
        THROWMT(Base::ValueError,
                QT_TRANSLATE_NOOP("Exceptions",
                                  "The knot index is out of bounds. Note that in accordance with "
                                  "OCC notation, the first knot has index 1 and not zero."))

    std::unique_ptr<Part::GeomBSplineCurve> bspline;

    int curmult = bsp->getMultiplicity(knotIndex);

    // zero is removing the knot, degree is just positional continuity
    if ((curmult + multiplicityincr) > degree)
        THROWMT(Base::ValueError,
                QT_TRANSLATE_NOOP(
                    "Exceptions",
                    "The multiplicity cannot be increased beyond the degree of the B-spline."))

    // zero is removing the knot, degree is just positional continuity
    if ((curmult + multiplicityincr) < 0)
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "The multiplicity cannot be decreased beyond zero."))

    try {
        bspline.reset(static_cast<Part::GeomBSplineCurve*>(bsp->clone()));

        if (multiplicityincr > 0) {// increase multiplicity
            bspline->increaseMultiplicity(knotIndex, curmult + multiplicityincr);
        }
        else {// decrease multiplicity
            bool result = bspline->removeKnot(knotIndex, curmult + multiplicityincr, 1E6);

            if (!result)
                THROWMT(
                    Base::CADKernelError,
                    QT_TRANSLATE_NOOP(
                        "Exceptions",
                        "OCC is unable to decrease the multiplicity within the maximum tolerance."))
        }
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n", e.what());
        return false;
    }

    // we succeeded with the multiplicity modification, so alignment geometry may be
    // invalid/inconsistent for the new bspline

    std::vector<int> delGeoId;

    std::vector<Base::Vector3d> poles = bsp->getPoles();
    std::vector<Base::Vector3d> newpoles = bspline->getPoles();
    std::vector<int> prevpole(bsp->countPoles());

    for (int i = 0; i < int(poles.size()); i++)
        prevpole[i] = -1;

    int taken = 0;
    for (int j = 0; j < int(poles.size()); j++) {
        for (int i = taken; i < int(newpoles.size()); i++) {
            if (newpoles[i] == poles[j]) {
                prevpole[j] = i;
                taken++;
                break;
            }
        }
    }

    // on fully removing a knot the knot geometry changes
    std::vector<double> knots = bsp->getKnots();
    std::vector<double> newknots = bspline->getKnots();
    std::vector<int> prevknot(bsp->countKnots());

    for (int i = 0; i < int(knots.size()); i++)
        prevknot[i] = -1;

    taken = 0;
    for (int j = 0; j < int(knots.size()); j++) {
        for (int i = taken; i < int(newknots.size()); i++) {
            if (newknots[i] == knots[j]) {
                prevknot[j] = i;
                taken++;
                break;
            }
        }
    }

    const std::vector<Sketcher::Constraint*>& cvals = Constraints.getValues();

    std::vector<Constraint*> newcVals(0);

    // modify pole constraints
    for (std::vector<Sketcher::Constraint*>::const_iterator it = cvals.begin(); it != cvals.end();
         ++it) {
        if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
            if ((*it)->AlignmentType == Sketcher::BSplineControlPoint) {
                if (prevpole[(*it)->InternalAlignmentIndex] != -1) {
                    assert(prevpole[(*it)->InternalAlignmentIndex] < bspline->countPoles());
                    Constraint* newConstr = (*it)->clone();
                    newConstr->InternalAlignmentIndex = prevpole[(*it)->InternalAlignmentIndex];
                    newcVals.push_back(newConstr);
                }
                else {
                    // it is an internal alignment geometry that is no longer valid => delete it and
                    // the pole circle
                    delGeoId.push_back((*it)->First);
                }
            }
            else if ((*it)->AlignmentType == Sketcher::BSplineKnotPoint) {
                if (prevknot[(*it)->InternalAlignmentIndex] != -1) {
                    assert(prevknot[(*it)->InternalAlignmentIndex] < bspline->countKnots());
                    Constraint* newConstr = (*it)->clone();
                    newConstr->InternalAlignmentIndex = prevknot[(*it)->InternalAlignmentIndex];
                    newcVals.push_back(newConstr);
                }
                else {
                    // it is an internal alignment geometry that is no longer valid => delete it and
                    // the knot point
                    delGeoId.push_back((*it)->First);
                }
            }
            else {// it is a bspline geometry, but not a controlpoint or knot
                newcVals.push_back(*it);
            }
        }
        else {
            newcVals.push_back(*it);
        }
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    GeometryFacade::copyId(geo, bspline.get());
    newVals[GeoId] = bspline.release();

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);
        Geometry.setValues(std::move(newVals));

        this->Constraints.setValues(std::move(newcVals));
    }

    // Trigger update now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    if (!delGeoId.empty()) {
        delGeometriesExclusiveList(delGeoId);
    }
    else {
        Geometry.touch();
    }

    // * DOCUMENTING OCC ISSUE OCC < 6.9.0
    // https://forum.freecad.org/viewtopic.php?f=10&t=9364&start=330#p162528
    //
    // A segmentation fault is generated:
    // Program received signal SIGSEGV, Segmentation fault.
    // #0 /lib/x86_64-linux-gnu/libc.so.6(+0x36cb0) [0x7f4b933bbcb0]
    // #1  0x7f4b0300ea14 in BSplCLib::BuildCache(double, double, bool, int, TColStd_Array1OfReal
    // const&, TColgp_Array1OfPnt const&, TColStd_Array1OfReal const&, TColgp_Array1OfPnt&,
    // TColStd_Array1OfReal&) from /usr/lib/x86_64-linux-gnu/libTKMath.so.10+0x484 #2 0x7f4b033f9582
    // in Geom_BSplineCurve::ValidateCache(double) from
    // /usr/lib/x86_64-linux-gnu/libTKG3d.so.10+0x202 #3  0x7f4b033f2a7e in
    // Geom_BSplineCurve::D0(double, gp_Pnt&) const from
    // /usr/lib/x86_64-linux-gnu/libTKG3d.so.10+0xde #4  0x7f4b033de1b5 in Geom_Curve::Value(double)
    // const from /usr/lib/x86_64-linux-gnu/libTKG3d.so.10+0x25 #5  0x7f4b03423d73 in
    // GeomLProp_CurveTool::Value(Handle(Geom_Curve) const&, double, gp_Pnt&) from
    // /usr/lib/x86_64-linux-gnu/libTKG3d.so.10+0x13 #6  0x7f4b03427175 in
    // GeomLProp_CLProps::SetParameter(double) from /usr/lib/x86_64-linux-gnu/libTKG3d.so.10+0x75 #7
    // 0x7f4b0342727d in GeomLProp_CLProps::GeomLProp_CLProps(Handle(Geom_Curve) const&, double,
    // int, double) from /usr/lib/x86_64-linux-gnu/libTKG3d.so.10+0xcd #8  0x7f4b11924b53 in
    // Part::GeomCurve::pointAtParameter(double) const from
    // /home/abdullah/github/freecad-build/Mod/Part/Part.so+0xa7


    return true;
}

bool SketchObject::insertBSplineKnot(int GeoId, double param, int multiplicity)
{
    // TODO: Check if this is still valid: no need to check input data validity as this is an
    // sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // handling unacceptable cases
    if (GeoId < 0 || GeoId > getHighestCurveIndex())
        THROWMT(
            Base::ValueError,
            QT_TRANSLATE_NOOP("Exceptions", "BSpline Geometry Index (GeoID) is out of bounds."));

    if (multiplicity == 0)
        THROWMT(Base::ValueError,
                QT_TRANSLATE_NOOP("Exceptions", "Knot cannot have zero multiplicity."));

    const Part::Geometry* geo = getGeometry(GeoId);

    if (geo->getTypeId() != Part::GeomBSplineCurve::getClassTypeId())
        THROWMT(Base::TypeError,
                QT_TRANSLATE_NOOP("Exceptions",
                                  "The Geometry Index (GeoId) provided is not a B-spline curve."));

    const Part::GeomBSplineCurve* bsp = static_cast<const Part::GeomBSplineCurve*>(geo);

    int degree = bsp->getDegree();
    double firstParam = bsp->getFirstParameter();
    double lastParam = bsp->getLastParameter();

    if (multiplicity > degree)
        THROWMT(Base::ValueError,
                QT_TRANSLATE_NOOP(
                    "Exceptions",
                    "Knot multiplicity cannot be higher than the degree of the BSpline."));

    if (param > lastParam || param < firstParam)
        THROWMT(Base::ValueError,
                QT_TRANSLATE_NOOP("Exceptions",
                                  "Knot cannot be inserted outside the BSpline parameter range."));

    std::unique_ptr<Part::GeomBSplineCurve> bspline;

    // run the command
    try {
        bspline.reset(static_cast<Part::GeomBSplineCurve*>(bsp->clone()));

        bspline->insertKnot(param, multiplicity);
    }
    catch (const Base::Exception& e) {
        Base::Console().Error("%s\n", e.what());
        return false;
    }

    // once command is run update the internal geometries
    std::vector<int> delGeoId;

    std::vector<Base::Vector3d> poles = bsp->getPoles();
    std::vector<Base::Vector3d> newpoles = bspline->getPoles();
    std::vector<int> prevpole(bsp->countPoles());

    for (int i = 0; i < int(poles.size()); i++)
        prevpole[i] = -1;

    int taken = 0;
    for (int j = 0; j < int(poles.size()); j++) {
        for (int i = taken; i < int(newpoles.size()); i++) {
            if (newpoles[i] == poles[j]) {
                prevpole[j] = i;
                taken++;
                break;
            }
        }
    }

    // on fully removing a knot the knot geometry changes
    std::vector<double> knots = bsp->getKnots();
    std::vector<double> newknots = bspline->getKnots();
    std::vector<int> prevknot(bsp->countKnots());

    for (int i = 0; i < int(knots.size()); i++)
        prevknot[i] = -1;

    taken = 0;
    for (int j = 0; j < int(knots.size()); j++) {
        for (int i = taken; i < int(newknots.size()); i++) {
            if (newknots[i] == knots[j]) {
                prevknot[j] = i;
                taken++;
                break;
            }
        }
    }

    const std::vector<Sketcher::Constraint*>& cvals = Constraints.getValues();

    std::vector<Constraint*> newcVals(0);

    // modify pole constraints
    for (std::vector<Sketcher::Constraint*>::const_iterator it = cvals.begin(); it != cvals.end();
         ++it) {
        if ((*it)->Type == Sketcher::InternalAlignment && (*it)->Second == GeoId) {
            if ((*it)->AlignmentType == Sketcher::BSplineControlPoint) {
                if (prevpole[(*it)->InternalAlignmentIndex] != -1) {
                    assert(prevpole[(*it)->InternalAlignmentIndex] < bspline->countPoles());
                    Constraint* newConstr = (*it)->clone();
                    newConstr->InternalAlignmentIndex = prevpole[(*it)->InternalAlignmentIndex];
                    newcVals.push_back(newConstr);
                }
                else {
                    // it is an internal alignment geometry that is no longer valid => delete it and
                    // the pole circle
                    delGeoId.push_back((*it)->First);
                }
            }
            else if ((*it)->AlignmentType == Sketcher::BSplineKnotPoint) {
                if (prevknot[(*it)->InternalAlignmentIndex] != -1) {
                    assert(prevknot[(*it)->InternalAlignmentIndex] < bspline->countKnots());
                    Constraint* newConstr = (*it)->clone();
                    newConstr->InternalAlignmentIndex = prevknot[(*it)->InternalAlignmentIndex];
                    newcVals.push_back(newConstr);
                }
                else {
                    // it is an internal alignment geometry that is no longer valid => delete it and
                    // the knot point
                    delGeoId.push_back((*it)->First);
                }
            }
            else {
                // it is a bspline geometry, but not a controlpoint or knot
                newcVals.push_back(*it);
            }
        }
        else {
            newcVals.push_back(*it);
        }
    }

    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    std::vector<Part::Geometry*> newVals(vals);

    newVals[GeoId] = bspline.release();

    // Block acceptGeometry in OnChanged to avoid unnecessary checks and updates
    {
        Base::StateLocker lock(internaltransaction, true);
        Geometry.setValues(std::move(newVals));

        this->Constraints.setValues(std::move(newcVals));
    }

    // Trigger update now
    // Update geometry indices and rebuild vertexindex now via onChanged, so that
    // ViewProvider::UpdateData is triggered.
    if (!delGeoId.empty()) {
        // NOTE: There have been a couple of instances when knot insertion has
        // led to a segmentation fault: see
        // https://forum.freecad.org/viewtopic.php?f=19&t=64962&sid=10272db50a635c633260517b14ecad37.
        // If a segfault happens again and a `Geometry.touch()` here fixes it,
        // it is possible that `delGeometriesExclusiveList` is causing an update
        // in constraint GUI features during an intermediate step.
        // See 247a9f0876a00e08c25b07d1f8802479d8623e87 for suggestions.
        // Geometry.touch();
        delGeometriesExclusiveList(delGeoId);
    }
    else {
        Geometry.touch();
    }

    // handle this last return
    return true;
}

bool SketchObject::simplifyBSpline(Part::GeomBSplineCurve *bspline, const std::string &key)
{
    if (int maxDegree = ExternalBSplineMaxDegree.getValue()) {
        if (bspline->getDegree() > maxDegree) {
            std::string err;
            try {
                bspline->approximate(ExternalBSplineTolerance.getValue(), 20,
                                     ExternalBSplineMaxDegree.getValue(), GeomAbs_C0);
                return true;
            } catch (Base::Exception &e) {
                err = e.what();
            }
            if (err.size())
                FC_WARN("Failed to simplify external imported bspline "
                        << getFullName() << ": " << key << ", " << err);
        }
    }
    return false;
}

// clang-format on
