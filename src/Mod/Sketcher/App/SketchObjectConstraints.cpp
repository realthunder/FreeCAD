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

int SketchObject::hasConflicts() const
{
    if (lastDoF < 0)// over-constrained sketch
        return -2;
    if (solvedSketch.hasConflicts())// conflicting constraints
        return -1;

    return 0;
}

void SketchObject::retrieveSolverDiagnostics()
{
    lastHasConflict = solvedSketch.hasConflicts();
    lastHasRedundancies = solvedSketch.hasRedundancies();
    lastHasPartialRedundancies = solvedSketch.hasPartialRedundancies();
    lastHasMalformedConstraints = solvedSketch.hasMalformedConstraints();
    lastConflicting = solvedSketch.getConflicting();
    lastRedundant = solvedSketch.getRedundant();
    lastPartiallyRedundant = solvedSketch.getPartiallyRedundant();
    lastMalformedConstraints = solvedSketch.getMalformedConstraints();
}

int SketchObject::solve(bool updateGeoAfterSolving /*=true*/)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // Reset the initial movement in case of a dragging operation was ongoing on the solver.
    solvedSketch.resetInitMove();

    // if updateGeoAfterSolving=false, the solver information is updated, but the Sketch is nothing
    // updated. It is useful to avoid triggering an OnChange when the goeometry did not change but
    // the solver needs to be updated.

    // We should have an updated Sketcher (sketchobject) geometry or this solve() should not have
    // happened therefore we update our sketch solver geometry with the SketchObject one.
    //
    // set up a sketch (including dofs counting and diagnosing of conflicts)
    lastDoF = solvedSketch.setUpSketch(
        getCompleteGeometry(), Constraints.getValues(), getExternalGeometryCount());

    // At this point we have the solver information about conflicting/redundant/over-constrained,
    // but the sketch is NOT solved. Some examples: Redundant: a vertical line, a horizontal line
    // and an angle constraint of 90 degrees between the two lines Conflicting: a 80 degrees angle
    // between a vertical line and another line, then adding a horizontal constraint to that other
    // line OverConstrained: a conflicting constraint when all other DoF are already constrained (it
    // has more constraints than parameters and the extra constraints are not redundant)

    solverNeedsUpdate = false;

    retrieveSolverDiagnostics();

    lastSolveTime = 0.0;

    // Failure is default for notifying the user unless otherwise proven
    lastSolverStatus = GCS::Failed;

    int err = 0;

    // redundancy is a lower priority problem than conflict/over-constraint/solver error
    // we set it here because we are indeed going to solve, as we can. However, we still want to
    // provide the right error code.
    if (lastHasRedundancies) {// redundant constraints
        err = -2;
    }

    if (lastDoF < 0) {// over-constrained sketch
        err = -4;
    }
    else if (lastHasConflict) {// conflicting constraints
        // The situation is exactly the same as in the over-constrained situation.
        err = -3;
    }
    else if (lastHasMalformedConstraints) {
        err = -5;
    }
    else {
        lastSolverStatus = solvedSketch.solve();
        if (lastSolverStatus != 0) {// solving
            err = -1;
        }
    }

    if (lastHasMalformedConstraints) {
        Base::Console().Error(
            this->getFullLabel(),
            QT_TRANSLATE_NOOP("Notifications", "The Sketch has malformed constraints!") "\n");
    }

    if (lastHasPartialRedundancies) {
        Base::Console().Warning(
            this->getFullLabel(),
            QT_TRANSLATE_NOOP("Notifications",
                              "The Sketch has partially redundant constraints!") "\n");
    }

    lastSolveTime = solvedSketch.getSolveTime();

    // In uncommon situations, the analysis of QR decomposition leads to full rank, but the result
    // does not converge. We avoid marking a sketch as fully constrained when no convergence is
    // achieved.
    if (err == 0) {
        FullyConstrained.setValue(lastDoF == 0);
    }

    if (err == 0 && updateGeoAfterSolving) {
        // set the newly solved geometry
        std::vector<Part::Geometry*> geomlist = solvedSketch.extractGeometry();
        Part::PropertyGeometryList tmp;
        tmp.setValues(std::move(geomlist));
        // Only set values if there is actual changes
        if (!Geometry.isSame(tmp))
            Geometry.moveValues(std::move(tmp));
    }
    else if (err < 0) {
        // if solver failed, invalid constraints were likely added before solving
        // (see solve in addConstraint), so solver information is definitely invalid.
        //
        // Update: ViewProviderSketch shall now rely on the signalSolverUpdate below for update
        // this->Constraints.touch();
    }

    signalSolverUpdate();

    return err;
}

int SketchObject::setDatum(int ConstrId, double Datum)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    // set the changed value for the constraint
    if (this->Constraints.hasInvalidGeometry())
        return -6;
    const std::vector<Constraint*>& vals = this->Constraints.getValues();
    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;
    ConstraintType type = vals[ConstrId]->Type;
    // for tangent, value==0 is autodecide, value==Pi/2 is external and value==-Pi/2 is internal
    if (!vals[ConstrId]->isDimensional() && type != Tangent && type != Perpendicular)
        return -1;

    if ((type == Radius || type == Diameter || type == Weight) && Datum <= 0)
        return (Datum == 0) ? -5 : -4;

    if (type == Distance && Datum == 0)
            return -5;

    // copy the list
    std::vector<Constraint*> newVals(vals);
    double oldDatum = newVals[ConstrId]->getValue();
    newVals[ConstrId] = newVals[ConstrId]->clone();
    newVals[ConstrId]->setValue(Datum);

    this->Constraints.setValues(std::move(newVals));

    int err = solve();

    if (err)
        this->Constraints.getValues()[ConstrId]->setValue(oldDatum);// newVals is a shell now

    return err;
}

int SketchObject::setDriving(int ConstrId, bool isdriving)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    int ret = testDrivingChange(ConstrId, isdriving);

    if (ret < 0)
        return ret;

    // copy the list
    std::vector<Constraint*> newVals(vals);
    newVals[ConstrId] = newVals[ConstrId]->clone();
    newVals[ConstrId]->isDriving = isdriving;

    this->Constraints.setValues(std::move(newVals));

    if (!isdriving)
        setExpression(Constraints.createPath(ConstrId), std::shared_ptr<App::Expression>());

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::getDriving(int ConstrId, bool& isdriving)
{
    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    if (!vals[ConstrId]->isDimensional())
        return -1;

    isdriving = vals[ConstrId]->isDriving;
    return 0;
}

int SketchObject::testDrivingChange(int ConstrId, bool isdriving)
{
    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    if (!vals[ConstrId]->isDimensional())
        return -2;

    if (!(vals[ConstrId]->First >= 0 || vals[ConstrId]->Second >= 0 || vals[ConstrId]->Third >= 0)
        && isdriving) {
        // a constraint that does not have at least one element as not-external-geometry can never
        // be driving.
        return -3;
    }

    return 0;
}

int SketchObject::setActive(int ConstrId, bool isactive)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    // copy the list
    std::vector<Constraint*> newVals(vals);
    // clone the changed Constraint
    Constraint* constNew = vals[ConstrId]->clone();
    constNew->isActive = isactive;
    newVals[ConstrId] = constNew;
    this->Constraints.setValues(std::move(newVals));

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::getActive(int ConstrId, bool& isactive)
{
    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    isactive = vals[ConstrId]->isActive;

    return 0;
}

int SketchObject::toggleActive(int ConstrId)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    // copy the list
    std::vector<Constraint*> newVals(vals);
    // clone the changed Constraint
    Constraint* constNew = vals[ConstrId]->clone();
    constNew->isActive = !constNew->isActive;
    newVals[ConstrId] = constNew;
    this->Constraints.setValues(std::move(newVals));

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

/// Make all dimensionals Driving/non-Driving
int SketchObject::setDatumsDriving(bool isdriving)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();
    std::vector<Constraint*> newVals(vals);

    for (size_t i = 0; i < newVals.size(); i++) {
        if (!testDrivingChange(i, isdriving)) {
            newVals[i] = newVals[i]->clone();
            newVals[i]->isDriving = isdriving;
        }
    }

    this->Constraints.setValues(std::move(newVals));

    // newVals is a shell now
    const std::vector<Constraint*>& uvals = this->Constraints.getValues();

    for (size_t i = 0; i < uvals.size(); i++) {
        if (!isdriving && uvals[i]->isDimensional())
            setExpression(Constraints.createPath(i), std::shared_ptr<App::Expression>());
    }

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::moveDatumsToEnd()
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    std::vector<Constraint*> copy(vals);
    std::vector<Constraint*> newVals(vals.size());

    int addindex = copy.size() - 1;

    // add the dimensionals at the end
    for (int i = copy.size() - 1; i >= 0; i--) {
        if (copy[i]->isDimensional()) {
            newVals[addindex] = copy[i];
            addindex--;
        }
    }

    // add the non-dimensionals
    for (int i = copy.size() - 1; i >= 0; i--) {
        if (!copy[i]->isDimensional()) {
            newVals[addindex] = copy[i];
            addindex--;
        }
    }

    this->Constraints.setValues(std::move(newVals));

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

void SketchObject::reverseAngleConstraintToSupplementary(Constraint* constr, int constNum)
{
    std::swap(constr->First, constr->Second);
    std::swap(constr->FirstPos, constr->SecondPos);
    if (constr->FirstPos == constr->SecondPos) {
        constr->FirstPos = (constr->FirstPos == Sketcher::PointPos::start) ? Sketcher::PointPos::end : Sketcher::PointPos::start;
    }
    else {
        constr->SecondPos = (constr->SecondPos == Sketcher::PointPos::start) ? Sketcher::PointPos::end : Sketcher::PointPos::start;
    }

    // Edit the expression if any, else modify constraint value directly
    if (constraintHasExpression(constNum)) {
        std::string expression = getConstraintExpression(constNum);
        setConstraintExpression(constNum, reverseAngleConstraintExpression(expression));
    }
    else {
        double actAngle = constr->getValue();
        constr->setValue(M_PI - actAngle);
    }
}

void SketchObject::inverseAngleConstraint(Constraint* constr)
{
    constr->FirstPos = (constr->FirstPos == Sketcher::PointPos::start) ? Sketcher::PointPos::end : Sketcher::PointPos::start;
    constr->SecondPos = (constr->SecondPos == Sketcher::PointPos::start) ? Sketcher::PointPos::end : Sketcher::PointPos::start;
}

bool SketchObject::constraintHasExpression(int constNum) const
{
    App::ObjectIdentifier path = Constraints.createPath(constNum);
    auto info = getExpression(path);
    if (info.expression) {
        return true;
    }
    return false;
}

std::string SketchObject::getConstraintExpression(int constNum) const
{
    App::ObjectIdentifier path = Constraints.createPath(constNum);
    auto info = getExpression(path);
    if (info.expression) {
        std::string expression = info.expression->toString();
        return expression;
    }

    return {};
}

void SketchObject::setConstraintExpression(int constNum, const std::string& newExpression)
{
    App::ObjectIdentifier path = Constraints.createPath(constNum);
    auto info = getExpression(path);
    if (info.expression) {
        try {
            std::shared_ptr<App::Expression> expr(App::Expression::parse(this, newExpression));
            setExpression(path, expr);
        }
        catch (const Base::Exception&) {
            Base::Console().Error("Failed to set constraint expression.");
        }
    }
}

std::string SketchObject::reverseAngleConstraintExpression(std::string expression)
{
    // Check if expression contains units (°, deg, rad)
    if (expression.find("°") != std::string::npos
        || expression.find("deg") != std::string::npos
        || expression.find("rad") != std::string::npos) {
        if (expression.substr(0, 9) == "180 ° - ") {
            expression = expression.substr(9, expression.size() - 9);
        }
        else {
            expression = "180 ° - (" + expression + ")";
        }
    }
    else {
        if (expression.substr(0, 6) == "180 - ") {
            expression = expression.substr(6, expression.size() - 6);
        }
        else {
            expression = "180 - (" + expression + ")";
        }
    }
    return expression;
}

int SketchObject::setVirtualSpace(int ConstrId, bool isinvirtualspace)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    // copy the list
    std::vector<Constraint*> newVals(vals);

    // clone the changed Constraint
    Constraint* constNew = vals[ConstrId]->clone();
    constNew->isInVirtualSpace = isinvirtualspace;
    newVals[ConstrId] = constNew;

    this->Constraints.setValues(std::move(newVals));

    // Solver didn't actually update, but we need this to inform view provider
    // to redraw
    signalSolverUpdate();

    return 0;
}

int SketchObject::setVirtualSpace(std::vector<int> constrIds, bool isinvirtualspace)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    if (constrIds.empty())
        return 0;

    std::sort(constrIds.begin(), constrIds.end());

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (constrIds.front() < 0 || constrIds.back() >= int(vals.size()))
        return -1;

    std::vector<Constraint*> newVals(vals);

    for (auto cid : constrIds) {
        // clone the changed Constraint
        if (vals[cid]->isInVirtualSpace != isinvirtualspace) {
            Constraint* constNew = vals[cid]->clone();
            constNew->isInVirtualSpace = isinvirtualspace;
            newVals[cid] = constNew;
        }
    }

    this->Constraints.setValues(std::move(newVals));

    return 0;
}

int SketchObject::getVirtualSpace(int ConstrId, bool& isinvirtualspace) const
{
    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    isinvirtualspace = vals[ConstrId]->isInVirtualSpace;
    return 0;
}

int SketchObject::toggleVirtualSpace(int ConstrId)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    // copy the list
    std::vector<Constraint*> newVals(vals);

    // clone the changed Constraint
    Constraint* constNew = vals[ConstrId]->clone();
    constNew->isInVirtualSpace = !constNew->isInVirtualSpace;
    newVals[ConstrId] = constNew;

    this->Constraints.setValues(std::move(newVals));

    // Solver didn't actually update, but we need this to inform view provider
    // to redraw
    signalSolverUpdate();

    return 0;
}

int SketchObject::setUpSketch()
{
    lastDoF = solvedSketch.setUpSketch(
        getCompleteGeometry(), Constraints.getValues(), getExternalGeometryCount());

    retrieveSolverDiagnostics();

    if (lastHasRedundancies || lastDoF < 0 || lastHasConflict || lastHasMalformedConstraints
        || lastHasPartialRedundancies)
        Constraints.touch();

    return lastDoF;
}

int SketchObject::diagnoseAdditionalConstraints(
    std::vector<Sketcher::Constraint*> additionalconstraints)
{
    auto objectconstraints = Constraints.getValues();

    std::vector<Sketcher::Constraint*> allconstraints;
    allconstraints.reserve(objectconstraints.size() + additionalconstraints.size());

    std::copy(objectconstraints.begin(), objectconstraints.end(), back_inserter(allconstraints));
    std::copy(
        additionalconstraints.begin(), additionalconstraints.end(), back_inserter(allconstraints));

    lastDoF =
        solvedSketch.setUpSketch(getCompleteGeometry(), allconstraints, getExternalGeometryCount());

    retrieveSolverDiagnostics();

    return lastDoF;
}

int SketchObject::deleteAllConstraints()
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    std::vector<Constraint*> newConstraints(0);

    this->Constraints.setValues(newConstraints);

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

void SketchObject::addGeometryState(const Constraint* cstr) const
{
    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    Sketcher::InternalType::InternalType constraintInternalAlignment = InternalType::None;
    bool constraintBlockedState = false;

    if (getInternalTypeState(cstr, constraintInternalAlignment)) {
        auto gf = GeometryFacade::getFacade(vals[cstr->First]);
        gf->setInternalType(constraintInternalAlignment);
    }
    else if (getBlockedState(cstr, constraintBlockedState)) {
        auto gf = GeometryFacade::getFacade(vals[cstr->First]);
        gf->setBlocked(constraintBlockedState);
    }
}

void SketchObject::removeGeometryState(const Constraint* cstr) const
{
    const std::vector<Part::Geometry*>& vals = getInternalGeometry();

    // Assign correct Internal Geometry Type (see SketchGeometryExtension)
    if (cstr->Type == InternalAlignment) {
        auto gf = GeometryFacade::getFacade(vals[cstr->First]);
        gf->setInternalType(InternalType::None);
    }

    // Assign Blocked geometry mode (see SketchGeometryExtension)
    if (cstr->Type == Block) {
        auto gf = GeometryFacade::getFacade(vals[cstr->First]);
        gf->setBlocked(false);
    }
}

// ConstraintList is used only to make copies.
int SketchObject::addConstraints(const std::vector<Constraint*>& ConstraintList)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    std::vector<Constraint*> newVals(vals);
    newVals.insert(newVals.end(), ConstraintList.begin(), ConstraintList.end());
    for (std::size_t i = newVals.size() - ConstraintList.size(); i < newVals.size(); i++) {
        Constraint* cnew = newVals[i]->clone();
        newVals[i] = cnew;

        if (cnew->Type == Tangent || cnew->Type == Perpendicular) {
            AutoLockTangencyAndPerpty(cnew);
        }

        addGeometryState(cnew);
    }

    this->Constraints.setValues(std::move(newVals));

    return this->Constraints.getSize() - 1;
}

int SketchObject::addCopyOfConstraints(const SketchObject& orig)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    const std::vector<Constraint*>& origvals = orig.Constraints.getValues();

    std::vector<Constraint*> newVals(vals);

    newVals.reserve(vals.size() + origvals.size());

    for (auto& v : origvals)
        newVals.push_back(v->copy());

    this->Constraints.setValues(std::move(newVals));

    auto& uvals = this->Constraints.getValues();

    std::size_t uvalssize = uvals.size();

    for (std::size_t i = uvalssize, j = 0; i < uvals.size(); i++, j++) {
        if (uvals[i]->isDriving && uvals[i]->isDimensional()) {

            App::ObjectIdentifier spath = orig.Constraints.createPath(j);

            App::PropertyExpressionEngine::ExpressionInfo expr_info = orig.getExpression(spath);

            if (expr_info.expression) {// if there is an expression on the source dimensional
                App::ObjectIdentifier dpath = this->Constraints.createPath(i);
                setExpression(dpath,
                              std::shared_ptr<App::Expression>(expr_info.expression->copy()));
            }
        }
    }

    if (noRecomputes) // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
        solve();

    return this->Constraints.getSize() - 1;
}

int SketchObject::addConstraint(const Constraint* constraint)
{
    auto constraint_ptr = std::unique_ptr<Constraint>(constraint->clone());

    return addConstraint(std::move(constraint_ptr));
}

int SketchObject::addConstraint(std::unique_ptr<Constraint> constraint)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    std::vector<Constraint*> newVals(vals);

    Constraint* constNew = constraint.release();

    if (constNew->Type == Tangent || constNew->Type == Perpendicular)
        AutoLockTangencyAndPerpty(constNew);

    addGeometryState(constNew);

    newVals.push_back(constNew);// add new constraint at the back

    this->Constraints.setValues(std::move(newVals));

    return this->Constraints.getSize() - 1;
}

int SketchObject::delConstraint(int ConstrId)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();
    if (ConstrId < 0 || ConstrId >= int(vals.size()))
        return -1;

    std::vector<Constraint*> newVals(vals);
    auto ctriter = newVals.begin() + ConstrId;
    removeGeometryState(*ctriter);
    newVals.erase(ctriter);
    this->Constraints.setValues(std::move(newVals));

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve();

    return 0;
}

int SketchObject::delConstraints(std::vector<int> ConstrIds, bool updategeometry)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);
    if (ConstrIds.empty())
        return 0;

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    std::vector<Constraint*> newVals(vals);

    std::sort(ConstrIds.begin(), ConstrIds.end());

    if (ConstrIds.front() < 0 || ConstrIds.back() >= int(vals.size()))
        return -1;

    for (auto rit = ConstrIds.rbegin(); rit != ConstrIds.rend(); rit++) {
        auto ctriter = newVals.begin() + *rit;
        removeGeometryState(*ctriter);
        newVals.erase(ctriter);
    }

    this->Constraints.setValues(std::move(newVals));

    // if we do not have a recompute, the sketch must be solved to update the DoF of the solver
    if (noRecomputes)
        solve(updategeometry);

    return 0;
}

int SketchObject::delConstraintOnPoint(int VertexId, bool onlyCoincident)
{
    int GeoId;
    PointPos PosId;
    if (VertexId == GeoEnum::RtPnt) {// RootPoint
        GeoId = Sketcher::GeoEnum::RtPnt;
        PosId = PointPos::start;
    }
    else
        getGeoVertexIndex(VertexId, GeoId, PosId);

    return delConstraintOnPoint(GeoId, PosId, onlyCoincident);
}

int SketchObject::delConstraintOnPoint(int GeoId, PointPos PosId, bool onlyCoincident)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    // check if constraints can be redirected to some other point
    int replaceGeoId = GeoEnum::GeoUndef;
    PointPos replacePosId = Sketcher::PointPos::none;
    if (!onlyCoincident) {
        for (std::vector<Constraint*>::const_iterator it = vals.begin(); it != vals.end(); ++it) {
            if ((*it)->Type == Sketcher::Coincident) {
                if ((*it)->First == GeoId && (*it)->FirstPos == PosId) {
                    replaceGeoId = (*it)->Second;
                    replacePosId = (*it)->SecondPos;
                    break;
                }
                else if ((*it)->Second == GeoId && (*it)->SecondPos == PosId) {
                    replaceGeoId = (*it)->First;
                    replacePosId = (*it)->FirstPos;
                    break;
                }
            }
        }
    }

    // remove or redirect any constraints associated with the given point
    std::vector<Constraint*> newVals(0);
    for (std::vector<Constraint*>::const_iterator it = vals.begin(); it != vals.end(); ++it) {
        if ((*it)->Type == Sketcher::Coincident) {
            if ((*it)->First == GeoId && (*it)->FirstPos == PosId) {
                // redirect this constraint
                if (replaceGeoId != GeoEnum::GeoUndef
                    && (replaceGeoId != (*it)->Second || replacePosId != (*it)->SecondPos)) {
                    (*it)->First = replaceGeoId;
                    (*it)->FirstPos = replacePosId;
                }
                else
                    continue;// skip this constraint
            }
            else if ((*it)->Second == GeoId && (*it)->SecondPos == PosId) {
                // redirect this constraint
                if (replaceGeoId != GeoEnum::GeoUndef
                    && (replaceGeoId != (*it)->First || replacePosId != (*it)->FirstPos)) {
                    (*it)->Second = replaceGeoId;
                    (*it)->SecondPos = replacePosId;
                }
                else
                    continue;// skip this constraint
            }
        }
        else if (!onlyCoincident) {
            if ((*it)->Type == Sketcher::Distance || (*it)->Type == Sketcher::DistanceX
                || (*it)->Type == Sketcher::DistanceY) {
                if ((*it)->First == GeoId && (*it)->FirstPos == PointPos::none
                    && (PosId == PointPos::start || PosId == PointPos::end)) {
                    // remove the constraint even if it is not directly associated
                    // with the given point
                    continue;// skip this constraint
                }
                else if ((*it)->First == GeoId && (*it)->FirstPos == PosId) {
                    if (replaceGeoId != GeoEnum::GeoUndef) {// redirect this constraint
                        (*it)->First = replaceGeoId;
                        (*it)->FirstPos = replacePosId;
                    }
                    else
                        continue;// skip this constraint
                }
                else if ((*it)->Second == GeoId && (*it)->SecondPos == PosId) {
                    if (replaceGeoId != GeoEnum::GeoUndef) {// redirect this constraint
                        (*it)->Second = replaceGeoId;
                        (*it)->SecondPos = replacePosId;
                    }
                    else
                        continue;// skip this constraint
                }
            }
            else if ((*it)->Type == Sketcher::PointOnObject) {
                if ((*it)->First == GeoId && (*it)->FirstPos == PosId) {
                    if (replaceGeoId != GeoEnum::GeoUndef) {// redirect this constraint
                        (*it)->First = replaceGeoId;
                        (*it)->FirstPos = replacePosId;
                    }
                    else
                        continue;// skip this constraint
                }
            }
            else if ((*it)->Type == Sketcher::Tangent || (*it)->Type == Sketcher::Perpendicular) {
                if (((*it)->First == GeoId && (*it)->FirstPos == PosId)
                    || ((*it)->Second == GeoId && (*it)->SecondPos == PosId)) {
                    // we could keep the tangency constraint by converting it
                    // to a simple one but it is not really worth
                    continue;// skip this constraint
                }
            }
            else if ((*it)->Type == Sketcher::Symmetric) {
                if (((*it)->First == GeoId && (*it)->FirstPos == PosId)
                    || ((*it)->Second == GeoId && (*it)->SecondPos == PosId)) {
                    continue;// skip this constraint
                }
            }
            else if ((*it)->Type == Sketcher::Vertical || (*it)->Type == Sketcher::Horizontal) {
                if (((*it)->First == GeoId && (*it)->FirstPos == PosId)
                    || ((*it)->Second == GeoId && (*it)->SecondPos == PosId)) {
                    continue;// skip this constraint
                }
            }
        }
        newVals.push_back(*it);
    }
    if (newVals.size() < vals.size()) {
        this->Constraints.setValues(std::move(newVals));

        return 0;
    }

    return -1;// no such constraint
}

int SketchObject::transferConstraints(int fromGeoId, PointPos fromPosId, int toGeoId,
                                      PointPos toPosId, bool doNotTransformTangencies)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& vals = this->Constraints.getValues();
    std::vector<Constraint*> newVals(vals);
    bool changed = false;
    for (int i = 0; i < int(newVals.size()); i++) {
        if (vals[i]->First == fromGeoId && vals[i]->FirstPos == fromPosId
            && !(vals[i]->Second == toGeoId && vals[i]->SecondPos == toPosId)
            && !(toGeoId < 0 && vals[i]->Second < 0)) {

            std::unique_ptr<Constraint> constNew(newVals[i]->clone());
            constNew->First = toGeoId;
            constNew->FirstPos = toPosId;

            // If not explicitly confirmed, nothing guarantees that a tangent can be freely
            // transferred to another coincident point, as the transfer destination edge most likely
            // won't be intended to be tangent. However, if it is an end to end point tangency, the
            // user expects it to be substituted by a coincidence constraint.
            if (vals[i]->Type == Sketcher::Tangent || vals[i]->Type == Sketcher::Perpendicular) {
                if (!doNotTransformTangencies) {
                    constNew->Type = Sketcher::Coincident;
                }
            }
            // With respect to angle constraints, if it is a DeepSOIC style angle constraint
            // (segment+segment+point), then no problem arises as the segments are PosId=none. In
            // this case there is no call to this function.
            //
            // However, other angle constraints are problematic because they are created on
            // segments, but internally operate on vertices, PosId=start Such constraint may not be
            // successfully transferred on deletion of the segments.
            else if (vals[i]->Type == Sketcher::Angle) {
                continue;
            }

            Constraint* constPtr = constNew.release();
            newVals[i] = constPtr;
            changed = true;
        }
        else if (vals[i]->Second == fromGeoId && vals[i]->SecondPos == fromPosId
                 && !(vals[i]->First == toGeoId && vals[i]->FirstPos == toPosId)
                 && !(toGeoId < 0 && vals[i]->First < 0)) {

            std::unique_ptr<Constraint> constNew(newVals[i]->clone());
            constNew->Second = toGeoId;
            constNew->SecondPos = toPosId;
            // If not explicitly confirmed, nothing guarantees that a tangent can be freely
            // transferred to another coincident point, as the transfer destination edge most likely
            // won't be intended to be tangent. However, if it is an end to end point tangency, the
            // user expects it to be substituted by a coincidence constraint.
            if (vals[i]->Type == Sketcher::Tangent || vals[i]->Type == Sketcher::Perpendicular) {
                if (!doNotTransformTangencies) {
                    constNew->Type = Sketcher::Coincident;
                }
            }
            else if (vals[i]->Type == Sketcher::Angle) {
                continue;
            }

            Constraint* constPtr = constNew.release();
            newVals[i] = constPtr;
            changed = true;
        }
    }

    // assign the new values only if something has changed
    if (changed) {
        this->Constraints.setValues(std::move(newVals));
    }
    return 0;
}

std::unique_ptr<Constraint> SketchObject::createConstraint(
    Sketcher::ConstraintType constrType, int firstGeoId, Sketcher::PointPos firstPos,
    int secondGeoId, Sketcher::PointPos secondPos, int thirdGeoId, Sketcher::PointPos thirdPos)
{
    auto newConstr = std::make_unique<Sketcher::Constraint>();

    newConstr->Type = constrType;
    newConstr->First = firstGeoId;
    newConstr->FirstPos = firstPos;
    newConstr->Second = secondGeoId;
    newConstr->SecondPos = secondPos;
    newConstr->Third = thirdGeoId;
    newConstr->ThirdPos = thirdPos;
    return newConstr;
}

void SketchObject::addConstraint(Sketcher::ConstraintType constrType, int firstGeoId,
                                 Sketcher::PointPos firstPos, int secondGeoId,
                                 Sketcher::PointPos secondPos, int thirdGeoId,
                                 Sketcher::PointPos thirdPos)
{
    auto newConstr = createConstraint(
        constrType, firstGeoId, firstPos, secondGeoId, secondPos, thirdGeoId, thirdPos);

    this->addConstraint(std::move(newConstr));
}

int SketchObject::removeAxesAlignment(const std::vector<int>& geoIdList)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    const std::vector<Constraint*>& constrvals = this->Constraints.getValues();

    unsigned int nhoriz = 0;
    unsigned int nvert = 0;

    bool changed = false;

    std::vector<std::pair<size_t, Sketcher::ConstraintType>> changeConstraintIndices;

    for (size_t i = 0; i < constrvals.size(); i++) {
        for (auto geoid : geoIdList) {
            if (constrvals[i]->First == geoid || constrvals[i]->Second == geoid
                || constrvals[i]->Third == geoid) {
                switch (constrvals[i]->Type) {
                    case Sketcher::Horizontal:
                        if (constrvals[i]->FirstPos == Sketcher::PointPos::none
                            && constrvals[i]->SecondPos == Sketcher::PointPos::none) {
                            changeConstraintIndices.emplace_back(i, constrvals[i]->Type);
                            nhoriz++;
                        }
                        break;
                    case Sketcher::Vertical:
                        if (constrvals[i]->FirstPos == Sketcher::PointPos::none
                            && constrvals[i]->SecondPos == Sketcher::PointPos::none) {
                            changeConstraintIndices.emplace_back(i, constrvals[i]->Type);
                            nvert++;
                        }
                        break;
                    case Sketcher::Symmetric:// only remove symmetric to axes
                        if ((constrvals[i]->Third == GeoEnum::HAxis
                             || constrvals[i]->Third == GeoEnum::VAxis)
                            && constrvals[i]->ThirdPos == Sketcher::PointPos::none)
                            changeConstraintIndices.emplace_back(i, constrvals[i]->Type);
                        break;
                    case Sketcher::PointOnObject:
                        if ((constrvals[i]->Second == GeoEnum::HAxis
                             || constrvals[i]->Second == GeoEnum::VAxis)
                            && constrvals[i]->SecondPos == Sketcher::PointPos::none)
                            changeConstraintIndices.emplace_back(i, constrvals[i]->Type);
                        break;
                    case Sketcher::DistanceX:
                    case Sketcher::DistanceY:
                        changeConstraintIndices.emplace_back(i, constrvals[i]->Type);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (changeConstraintIndices.empty())
        return 0;// nothing to be done

    std::vector<Constraint*> newconstrVals;
    newconstrVals.reserve(constrvals.size());

    int referenceHorizontal = GeoEnum::GeoUndef;
    int referenceVertical = GeoEnum::GeoUndef;

    int cindex = 0;
    for (size_t i = 0; i < constrvals.size(); i++) {
        if (i == changeConstraintIndices[cindex].first) {
            if (changeConstraintIndices[cindex].second == Sketcher::Horizontal && nhoriz > 0) {
                changed = true;
                if (referenceHorizontal == GeoEnum::GeoUndef) {
                    referenceHorizontal = constrvals[i]->First;
                }
                else {

                    auto newConstr = new Constraint();

                    newConstr->Type = Sketcher::Parallel;
                    newConstr->First = referenceHorizontal;
                    newConstr->Second = constrvals[i]->First;

                    newconstrVals.push_back(newConstr);
                }
            }
            else if (changeConstraintIndices[cindex].second == Sketcher::Vertical && nvert > 0) {
                changed = true;
                if (referenceVertical == GeoEnum::GeoUndef) {
                    referenceVertical = constrvals[i]->First;
                    ;
                }
                else {
                    auto newConstr = new Constraint();

                    newConstr->Type = Sketcher::Parallel;
                    newConstr->First = referenceVertical;
                    newConstr->Second = constrvals[i]->First;

                    newconstrVals.push_back(newConstr);
                }
            }
            else if (changeConstraintIndices[cindex].second == Sketcher::Symmetric
                     || changeConstraintIndices[cindex].second == Sketcher::PointOnObject) {
                changed = true;// We remove symmetric on axes
            }
            else if (changeConstraintIndices[cindex].second == Sketcher::DistanceX
                     || changeConstraintIndices[cindex].second == Sketcher::DistanceY) {
                changed = true;// We remove symmetric on axes
                newconstrVals.push_back(constrvals[i]->clone());
                newconstrVals.back()->Type = Sketcher::Distance;
            }

            cindex++;
        }
        else {
            newconstrVals.push_back(constrvals[i]);
        }
    }

    if (nhoriz > 0 && nvert > 0) {
        auto newConstr = new Constraint();

        newConstr->Type = Sketcher::Perpendicular;
        newConstr->First = referenceVertical;
        newConstr->Second = referenceHorizontal;

        newconstrVals.push_back(newConstr);
    }

    if (changed)
        Constraints.setValues(std::move(newconstrVals));

    return 0;
}

const std::vector<std::map<int, Sketcher::PointPos>> SketchObject::getCoincidenceGroups()
{
    // this function is different from that in getCoincidentPoints in that:
    // - getCoincidentPoints only considers direct coincidence (the points that are linked via a
    // single coincidence)
    // - this function provides an array of maps of points, each map containing the points that are
    // coincident by virtue
    //   of any number of interrelated coincidence constraints (if coincidence 1-2 and coincidence
    //   2-3, {1,2,3} are in that set)

    const std::vector<Sketcher::Constraint*>& vals = Constraints.getValues();

    std::vector<std::map<int, Sketcher::PointPos>> coincidenttree;
    // push the constraints
    for (std::vector<Sketcher::Constraint*>::const_iterator it = vals.begin(); it != vals.end();
         ++it) {
        if ((*it)->Type == Sketcher::Coincident) {
            int firstpresentin = -1;
            int secondpresentin = -1;

            int i = 0;

            for (std::vector<std::map<int, Sketcher::PointPos>>::const_iterator iti =
                     coincidenttree.begin();
                 iti != coincidenttree.end();
                 ++iti, i++) {
                // First
                std::map<int, Sketcher::PointPos>::const_iterator filiterator;
                filiterator = (*iti).find((*it)->First);
                if (filiterator != (*iti).end()) {
                    if ((*it)->FirstPos == (*filiterator).second)
                        firstpresentin = i;
                }
                // Second
                filiterator = (*iti).find((*it)->Second);
                if (filiterator != (*iti).end()) {
                    if ((*it)->SecondPos == (*filiterator).second)
                        secondpresentin = i;
                }
            }

            if (firstpresentin != -1 && secondpresentin != -1) {
                // we have to merge those sets into one
                coincidenttree[firstpresentin].insert(coincidenttree[secondpresentin].begin(),
                                                      coincidenttree[secondpresentin].end());
                coincidenttree.erase(coincidenttree.begin() + secondpresentin);
            }
            else if (firstpresentin == -1 && secondpresentin == -1) {
                // we do not have any of the values, so create a setCursor
                std::map<int, Sketcher::PointPos> tmp;
                tmp.insert(std::pair<int, Sketcher::PointPos>((*it)->First, (*it)->FirstPos));
                tmp.insert(std::pair<int, Sketcher::PointPos>((*it)->Second, (*it)->SecondPos));
                coincidenttree.push_back(tmp);
            }
            else if (firstpresentin != -1) {
                // add to existing group
                coincidenttree[firstpresentin].insert(
                    std::pair<int, Sketcher::PointPos>((*it)->Second, (*it)->SecondPos));
            }
            else {// secondpresentin != -1
                // add to existing group
                coincidenttree[secondpresentin].insert(
                    std::pair<int, Sketcher::PointPos>((*it)->First, (*it)->FirstPos));
            }
        }
    }

    return coincidenttree;
}

void SketchObject::isCoincidentWithExternalGeometry(int GeoId, bool& start_external,
                                                    bool& mid_external, bool& end_external)
{

    start_external = false;
    mid_external = false;
    end_external = false;

    const std::vector<std::map<int, Sketcher::PointPos>> coincidenttree = getCoincidenceGroups();

    for (std::vector<std::map<int, Sketcher::PointPos>>::const_iterator it = coincidenttree.begin();
         it != coincidenttree.end();
         ++it) {

        std::map<int, Sketcher::PointPos>::const_iterator geoId1iterator;

        geoId1iterator = (*it).find(GeoId);

        if (geoId1iterator != (*it).end()) {
            // If First is in this set and the first key in this ordered element key is external
            if ((*it).begin()->first < 0) {
                if ((*geoId1iterator).second == Sketcher::PointPos::start)
                    start_external = true;
                else if ((*geoId1iterator).second == Sketcher::PointPos::mid)
                    mid_external = true;
                else if ((*geoId1iterator).second == Sketcher::PointPos::end)
                    end_external = true;
            }
        }
    }
}

const std::map<int, Sketcher::PointPos> SketchObject::getAllCoincidentPoints(int GeoId,
                                                                             PointPos PosId)
{

    const std::vector<std::map<int, Sketcher::PointPos>> coincidenttree = getCoincidenceGroups();

    for (std::vector<std::map<int, Sketcher::PointPos>>::const_iterator it = coincidenttree.begin();
         it != coincidenttree.end();
         ++it) {

        std::map<int, Sketcher::PointPos>::const_iterator geoId1iterator;

        geoId1iterator = (*it).find(GeoId);

        if (geoId1iterator != (*it).end()) {
            // If GeoId is in this set

            if ((*geoId1iterator).second == PosId)// and posId matches
                return (*it);
        }
    }

    std::map<int, Sketcher::PointPos> empty;

    return empty;
}

void SketchObject::getDirectlyCoincidentPoints(int GeoId, PointPos PosId,
                                               std::vector<int>& GeoIdList,
                                               std::vector<PointPos>& PosIdList)
{
    const std::vector<Constraint*>& constraints = this->Constraints.getValues();

    GeoIdList.clear();
    PosIdList.clear();
    GeoIdList.push_back(GeoId);
    PosIdList.push_back(PosId);
    for (std::vector<Constraint*>::const_iterator it = constraints.begin(); it != constraints.end();
         ++it) {
        if ((*it)->Type == Sketcher::Coincident) {
            if ((*it)->First == GeoId && (*it)->FirstPos == PosId) {
                GeoIdList.push_back((*it)->Second);
                PosIdList.push_back((*it)->SecondPos);
            }
            else if ((*it)->Second == GeoId && (*it)->SecondPos == PosId) {
                GeoIdList.push_back((*it)->First);
                PosIdList.push_back((*it)->FirstPos);
            }
        }
    }
    if (GeoIdList.size() == 1) {
        GeoIdList.clear();
        PosIdList.clear();
    }
}

void SketchObject::getDirectlyCoincidentPoints(int VertexId, std::vector<int>& GeoIdList,
                                               std::vector<PointPos>& PosIdList)
{
    int GeoId;
    PointPos PosId;
    getGeoVertexIndex(VertexId, GeoId, PosId);
    getDirectlyCoincidentPoints(GeoId, PosId, GeoIdList, PosIdList);
}

bool SketchObject::arePointsCoincident(int GeoId1, PointPos PosId1, int GeoId2, PointPos PosId2)
{
    if (GeoId1 == GeoId2 && PosId1 == PosId2)
        return true;

    const std::vector<std::map<int, Sketcher::PointPos>> coincidenttree = getCoincidenceGroups();

    for (std::vector<std::map<int, Sketcher::PointPos>>::const_iterator it = coincidenttree.begin();
         it != coincidenttree.end();
         ++it) {

        std::map<int, Sketcher::PointPos>::const_iterator geoId1iterator;

        geoId1iterator = (*it).find(GeoId1);

        if (geoId1iterator != (*it).end()) {
            // If First is in this set
            std::map<int, Sketcher::PointPos>::const_iterator geoId2iterator;

            geoId2iterator = (*it).find(GeoId2);

            if (geoId2iterator != (*it).end()) {
                // If Second is in this set
                if ((*geoId1iterator).second == PosId1 && (*geoId2iterator).second == PosId2)
                    return true;
            }
        }
    }

    return false;
}

void SketchObject::getConstraintIndices(int GeoId, std::vector<int>& constraintList)
{
    const std::vector<Constraint*>& constraints = this->Constraints.getValues();
    int i = 0;

    for (std::vector<Constraint*>::const_iterator it = constraints.begin(); it != constraints.end();
         ++it) {
        if ((*it)->First == GeoId || (*it)->Second == GeoId || (*it)->Third == GeoId) {
            constraintList.push_back(i);
        }
        ++i;
    }
}

void SketchObject::appendConflictMsg(const std::vector<int>& conflicting, std::string& msg)
{
    appendConstraintsMsg(conflicting,
                         "Please remove the following conflicting constraint:\n",
                         "Please remove at least one of the following conflicting constraints:\n",
                         msg);
}

void SketchObject::appendRedundantMsg(const std::vector<int>& redundant, std::string& msg)
{
    appendConstraintsMsg(redundant,
                         "Please remove the following redundant constraint:",
                         "Please remove the following redundant constraints:",
                         msg);
}

void SketchObject::appendMalformedConstraintsMsg(const std::vector<int>& malformed,
                                                 std::string& msg)
{
    appendConstraintsMsg(malformed,
                         "Please remove the following malformed constraint:",
                         "Please remove the following malformed constraints:",
                         msg);
}

void SketchObject::appendConstraintsMsg(const std::vector<int>& vector,
                                        const std::string& singularmsg,
                                        const std::string& pluralmsg, std::string& msg)
{
    std::stringstream ss;
    if (msg.length() > 0)
        ss << msg;
    if (!vector.empty()) {
        if (vector.size() == 1)
            ss << singularmsg << std::endl;
        else
            ss << pluralmsg;
        ss << vector[0] << std::endl;
        for (unsigned int i = 1; i < vector.size(); i++)
            ss << ", " << vector[i];
        ss << "\n";
    }
    msg = ss.str();
}

bool SketchObject::evaluateConstraint(const Constraint* constraint) const
{
    // if requireXXX,  GeoUndef is treated as an error. If not requireXXX,
    // GeoUndef is accepted. Index range checking is done on everything regardless.

    // constraints always require a First!!
    bool requireSecond = false;
    bool requireThird = false;

    switch (constraint->Type) {
        case Radius:
        case Diameter:
        case Weight:
        case Horizontal:
        case Vertical:
        case Distance:
        case DistanceX:
        case DistanceY:
        case Coincident:
        case Perpendicular:
        case Parallel:
        case Equal:
        case PointOnObject:
        case Angle:
            break;
        case Tangent:
            requireSecond = true;
            break;
        case Symmetric:
        case SnellsLaw:
            requireSecond = true;
            requireThird = true;
            break;
        default:
            break;
    }

    int intGeoCount = getHighestCurveIndex() + 1;
    int extGeoCount = getExternalGeometryCount();

    // the actual checks
    bool ret = true;
    int geoId;

    // First is always required and GeoId must be within range
    geoId = constraint->First;
    ret = ret && (geoId >= -extGeoCount && geoId < intGeoCount);

    geoId = constraint->Second;
    ret = ret
        && ((geoId == GeoEnum::GeoUndef && !requireSecond)
            || (geoId >= -extGeoCount && geoId < intGeoCount));

    geoId = constraint->Third;
    ret = ret
        && ((geoId == GeoEnum::GeoUndef && !requireThird)
            || (geoId >= -extGeoCount && geoId < intGeoCount));

    return ret;
}

bool SketchObject::evaluateConstraints() const
{
    int intGeoCount = getHighestCurveIndex() + 1;
    int extGeoCount = getExternalGeometryCount();

    std::vector<Part::Geometry*> geometry = getCompleteGeometry();
    const std::vector<Sketcher::Constraint*>& constraints = Constraints.getValuesForce();
    if (static_cast<int>(geometry.size()) != extGeoCount + intGeoCount)
        return false;
    if (geometry.size() < 2)
        return false;

    std::vector<Sketcher::Constraint*>::const_iterator it;
    for (it = constraints.begin(); it != constraints.end(); ++it) {
        if (!evaluateConstraint(*it))
            return false;
    }

    if (!constraints.empty()) {
        if (!Constraints.scanGeometry(geometry))
            return false;
    }

    return true;
}

void SketchObject::validateConstraints()
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    std::vector<Part::Geometry*> geometry = getCompleteGeometry();
    const std::vector<Sketcher::Constraint*>& constraints = Constraints.getValuesForce();

    std::vector<Sketcher::Constraint*> newConstraints;
    newConstraints.reserve(constraints.size());
    std::vector<Sketcher::Constraint*>::const_iterator it;
    for (it = constraints.begin(); it != constraints.end(); ++it) {
        bool valid = evaluateConstraint(*it);
        if (valid)
            newConstraints.push_back(*it);
    }

    if (newConstraints.size() != constraints.size()) {
        Constraints.setValues(std::move(newConstraints));
        acceptGeometry();
    }
    else if (!Constraints.scanGeometry(geometry)) {
        Constraints.acceptGeometry(geometry);
    }
}

std::string SketchObject::validateExpression(const App::ObjectIdentifier& path,
                                             std::shared_ptr<const App::Expression> expr)
{
    const App::Property* prop = path.getProperty();

    assert(expr);

    if (!prop)
        return "Property not found";

    if (prop == &Constraints) {
        const Constraint* constraint = Constraints.getConstraint(path);

        if (!constraint->isDriving)
            return "Reference constraints cannot be set!";
    }

    auto deps = expr->getDeps();
    auto it = deps.find(this);
    if (it != deps.end()) {
        auto it2 = it->second.find("Constraints");
        if (it2 != it->second.end()) {
            for (auto& oid : it2->second) {
                const Constraint* constraint = Constraints.getConstraint(oid);

                if (!constraint->isDriving)
                    return "Reference constraint from this sketch cannot be used in this "
                           "expression.";
            }
        }
    }
    return "";
}

// This function is necessary for precalculation of an angle when adding
//  an angle constraint. It is also used here, in SketchObject, to
//  lock down the type of tangency/perpendicularity.
double SketchObject::calculateAngleViaPoint(int GeoId1, int GeoId2, double px, double py)
{
    // Temporary sketch based calculation. Slow, but guaranteed consistency with constraints.
    Sketcher::Sketch sk;

    const Part::Geometry* p1 = this->getGeometry(GeoId1);
    const Part::Geometry* p2 = this->getGeometry(GeoId2);

    if (p1 && p2) {
        int i1 = sk.addGeometry(this->getGeometry(GeoId1));
        int i2 = sk.addGeometry(this->getGeometry(GeoId2));

        return sk.calculateAngleViaPoint(i1, i2, px, py);
    }
    else
        THROWM(Base::ValueError, "Null geometry in calculateAngleViaPoint")
}

void SketchObject::constraintsRenamed(
    const std::map<App::ObjectIdentifier, App::ObjectIdentifier>& renamed)
{
    ExpressionEngine.renameExpressions(renamed);

    for (auto doc : App::GetApplication().getDocuments())
        doc->renameObjectIdentifiers(renamed);
}

void SketchObject::constraintsRemoved(const std::set<App::ObjectIdentifier>& removed)
{
    std::set<App::ObjectIdentifier>::const_iterator i = removed.begin();

    while (i != removed.end()) {
        ExpressionEngine.setValue(*i, std::shared_ptr<App::Expression>());
        ++i;
    }
}

// Tests if the provided point lies exactly in a curve (satisfies
//  point-on-object constraint). It is used to decide whether it is nesessary to
//  constrain a point onto curves when 3-element selection tangent-via-point-like
//  constraints are applied.
bool SketchObject::isPointOnCurve(int geoIdCurve, double px, double py)
{
    // DeepSOIC: this may be slow, but I wanted to reuse the existing code
    Sketcher::Sketch sk;
    int icrv = sk.addGeometry(this->getGeometry(geoIdCurve));
    Base::Vector3d pp;
    pp.x = px;
    pp.y = py;
    Part::GeomPoint p(pp);
    int ipnt = sk.addPoint(p);
    int icstr = sk.addPointOnObjectConstraint(ipnt, Sketcher::PointPos::start, icrv);
    double err = sk.calculateConstraintError(icstr);
    return err * err < 10.0 * sk.getSolverPrecision();
}

// This one was done just for fun to see to what precision the constraints are solved.
double SketchObject::calculateConstraintError(int ConstrId)
{
    Sketcher::Sketch sk;
    const std::vector<Constraint*>& clist = this->Constraints.getValues();
    if (ConstrId < 0 || ConstrId >= int(clist.size()))
        return std::numeric_limits<double>::quiet_NaN();

    Constraint* cstr = clist[ConstrId]->clone();
    double result = 0.0;
    try {
        std::vector<int> GeoIdList;
        int g;
        GeoIdList.push_back(cstr->First);
        GeoIdList.push_back(cstr->Second);
        GeoIdList.push_back(cstr->Third);

        // add only necessary geometry to the sketch
        for (std::size_t i = 0; i < GeoIdList.size(); i++) {
            g = GeoIdList[i];
            if (g != GeoEnum::GeoUndef) {
                GeoIdList[i] = sk.addGeometry(this->getGeometry(g));
            }
        }

        cstr->First = GeoIdList[0];
        cstr->Second = GeoIdList[1];
        cstr->Third = GeoIdList[2];
        int icstr = sk.addConstraint(cstr);
        result = sk.calculateConstraintError(icstr);
    }
    catch (...) {// cleanup
        delete cstr;
        throw;
    }
    delete cstr;
    return result;
}

bool SketchObject::getInternalTypeState(
    const Constraint* cstr, Sketcher::InternalType::InternalType& internaltypestate) const
{
    if (cstr->Type == InternalAlignment) {

        switch (cstr->AlignmentType) {
            case Undef:
            case NumInternalAlignmentType:
                internaltypestate = InternalType::None;
                break;
            case EllipseMajorDiameter:
                internaltypestate = InternalType::EllipseMajorDiameter;
                break;
            case EllipseMinorDiameter:
                internaltypestate = InternalType::EllipseMinorDiameter;
                break;
            case EllipseFocus1:
                internaltypestate = InternalType::EllipseFocus1;
                break;
            case EllipseFocus2:
                internaltypestate = InternalType::EllipseFocus2;
                break;
            case HyperbolaMajor:
                internaltypestate = InternalType::HyperbolaMajor;
                break;
            case HyperbolaMinor:
                internaltypestate = InternalType::HyperbolaMinor;
                break;
            case HyperbolaFocus:
                internaltypestate = InternalType::HyperbolaFocus;
                break;
            case ParabolaFocus:
                internaltypestate = InternalType::ParabolaFocus;
                break;
            case BSplineControlPoint:
                internaltypestate = InternalType::BSplineControlPoint;
                break;
            case BSplineKnotPoint:
                internaltypestate = InternalType::BSplineKnotPoint;
                break;
            case ParabolaFocalAxis:
                internaltypestate = InternalType::ParabolaFocalAxis;
                break;
        }

        return true;
    }

    return false;
}

bool SketchObject::getBlockedState(const Constraint* cstr, bool& blockedstate) const
{
    if (cstr->Type == Block) {
        blockedstate = true;
        return true;
    }

    return false;
}

/// changeConstraintsLocking locks or unlocks all tangent and perpendicular
///  constraints. (Constraint locking prevents it from flipping to another valid
///  configuration, when e.g. external geometry is updated from outside.) The
///  sketch solve is not triggered by the function, but the SketchObject is
///  touched (a recompute will be necessary). The geometry should not be affected
///  by the function.
/// The bLock argument specifies, what to do. If true, all constraints are
///  unlocked and locked again. If false, all tangent and perp. constraints are
///  unlocked.
int SketchObject::changeConstraintsLocking(bool bLock)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    int cntSuccess = 0;
    int cntToBeAffected = 0;//==cntSuccess+cntFail
    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    std::vector<Constraint*> newVals(vals);// modifiable copy of pointers array

    for (size_t i = 0; i < newVals.size(); i++) {
        if (newVals[i]->Type == Tangent || newVals[i]->Type == Perpendicular) {
            // create a constraint copy, affect it, replace the pointer
            cntToBeAffected++;
            Constraint* constNew = newVals[i]->clone();
            bool ret = AutoLockTangencyAndPerpty(newVals[i], /*bForce=*/true, bLock);

            if (ret)
                cntSuccess++;

            newVals[i] = constNew;
            Base::Console().Log("Constraint%i will be affected\n", i + 1);
        }
    }

    this->Constraints.setValues(std::move(newVals));

    Base::Console().Log("ChangeConstraintsLocking: affected %i of %i tangent/perp constraints\n",
                        cntSuccess,
                        cntToBeAffected);

    return cntSuccess;
}

/*!
 * \brief SketchObject::port_reversedExternalArcs finds constraints that link to endpoints of
 * external-geometry arcs, and swaps the endpoints in the constraints. This is needed after CCW
 * emulation was introduced, to port old sketches. \param justAnalyze if true, nothing is actually
 * done - only the number of constraints to be affected is returned. \return the number of
 * constraints changed/to be changed.
 */
int SketchObject::port_reversedExternalArcs(bool justAnalyze)
{
    // no need to check input data validity as this is an sketchobject managed operation.
    Base::StateLocker lock(managedoperation, true);

    int cntToBeAffected = 0;//==cntSuccess+cntFail
    const std::vector<Constraint*>& vals = this->Constraints.getValues();

    std::vector<Constraint*> newVals(vals);// modifiable copy of pointers array

    for (std::size_t ic = 0; ic < newVals.size(); ic++) {// ic = index of constraint
        bool affected = false;
        Constraint* constNew = nullptr;
        for (int ig = 1; ig <= 3; ig++) {// cycle through constraint.first, second, third
            int geoId = 0;
            Sketcher::PointPos posId = PointPos::none;
            switch (ig) {
                case 1:
                    geoId = newVals[ic]->First;
                    posId = newVals[ic]->FirstPos;
                    break;
                case 2:
                    geoId = newVals[ic]->Second;
                    posId = newVals[ic]->SecondPos;
                    break;
                case 3:
                    geoId = newVals[ic]->Third;
                    posId = newVals[ic]->ThirdPos;
                    break;
            }

            if (geoId <= GeoEnum::RefExt
                && (posId == Sketcher::PointPos::start || posId == Sketcher::PointPos::end)) {
                // we are dealing with a link to an endpoint of external geom
                Part::Geometry* g = this->ExternalGeo[-geoId - 1];
                if (g->is<Part::GeomArcOfCircle>()) {
                    const Part::GeomArcOfCircle* segm =
                        static_cast<const Part::GeomArcOfCircle*>(g);
                    if (segm->isReversed()) {
                        // Gotcha! a link to an endpoint of external arc that is reversed.
                        // create a constraint copy, affect it, replace the pointer
                        if (!affected)
                            constNew = newVals[ic]->clone();
                        affected = true;
                        // Do the fix on temp vars
                        if (posId == Sketcher::PointPos::start)
                            posId = Sketcher::PointPos::end;
                        else if (posId == Sketcher::PointPos::end)
                            posId = Sketcher::PointPos::start;
                    }
                }
            }
            if (!affected)
                continue;
            // Propagate the fix made on temp vars to the constraint
            switch (ig) {
                case 1:
                    constNew->First = geoId;
                    constNew->FirstPos = posId;
                    break;
                case 2:
                    constNew->Second = geoId;
                    constNew->SecondPos = posId;
                    break;
                case 3:
                    constNew->Third = geoId;
                    constNew->ThirdPos = posId;
                    break;
            }
        }
        if (affected) {
            cntToBeAffected++;
            newVals[ic] = constNew;
            Base::Console().Log("Constraint%i will be affected\n", ic + 1);
        };
    }

    if (!justAnalyze) {
        this->Constraints.setValues(std::move(newVals));
        Base::Console().Log("Swapped start/end of reversed external arcs in %i constraints\n",
                            cntToBeAffected);
    }

    return cntToBeAffected;
}

/// Locks tangency/perpendicularity type of such a constraint.
/// The constraint passed must be writable (i.e. the one that is not
///  yet in the constraint list).
/// Tangency type (internal/external) is derived from current geometry
///  the constraint refers to.
/// Same for perpendicularity type.
///
/// This function catches exceptions, because it's not a reason to
///  not create a constraint if tangency/perp-ty type cannot be determined.
///
/// Arguments:
///  cstr - pointer to a constraint to be locked/unlocked
///  bForce - specifies whether to ignore the already locked constraint or not.
///  bLock - specifies whether to lock the constraint or not (if bForce is
///   true, the constraint gets unlocked, otherwise nothing is done at all).
///
/// Return values:
///  true - success.
///  false - fail (this indicates an error, or that a constraint locking isn't supported).
bool SketchObject::AutoLockTangencyAndPerpty(Constraint* cstr, bool bForce, bool bLock)
{
    try {
        // assert ( cstr->Type == Tangent  ||  cstr->Type == Perpendicular);
        /*tangency type already set. If not bForce - don't touch.*/
        if (cstr->getValue() != 0.0 && !bForce)
            return true;
        if (!bLock) {
            cstr->setValue(0.0);// reset
        }
        else {
            // decide on tangency type. Write the angle value into the datum field of the
            // constraint.
            int geoId1, geoId2, geoIdPt;
            PointPos posPt;
            geoId1 = cstr->First;
            geoId2 = cstr->Second;
            geoIdPt = cstr->Third;
            posPt = cstr->ThirdPos;
            if (geoIdPt == GeoEnum::GeoUndef) {// not tangent-via-point, try endpoint-to-endpoint...

                // First check if it is a tangency at knot constraint, if not continue with checking
                // for endpoints. Endpoint constraints make use of the AngleViaPoint framework at
                // solver level, so they need locking angle calculation, tangency at knot constraint
                // does not.
                auto geof = getGeometryFacade(cstr->First);
                if (geof->isInternalType(InternalType::BSplineKnotPoint)) {
                    // there is point that is a B-Spline knot in a two element constraint
                    // this is not implement using AngleViaPoint (TangencyViaPoint)
                    return false;
                }

                geoIdPt = cstr->First;
                posPt = cstr->FirstPos;
            }
            if (posPt == PointPos::none) {
                // not endpoint-to-curve and not endpoint-to-endpoint tangent (is simple tangency)
                // no tangency lockdown is implemented for simple tangency. Do nothing.
                return false;
            }
            else {
                Base::Vector3d p = getPoint(geoIdPt, posPt);

                // this piece of code is also present in Sketch.cpp, correct for offset
                // and to do the autodecision for old sketches.
                // the difference between the datum value and the actual angle to apply.
                // (datum=angle+offset)
                double angleOffset = 0.0;
                // the desired angle value (and we are to decide if 180* should be added to it)
                double angleDesire = 0.0;
                if (cstr->Type == Tangent) {
                    angleOffset = -M_PI / 2;
                    angleDesire = 0.0;
                }
                if (cstr->Type == Perpendicular) {
                    angleOffset = 0;
                    angleDesire = M_PI / 2;
                }

                double angleErr = calculateAngleViaPoint(geoId1, geoId2, p.x, p.y) - angleDesire;

                // bring angleErr to -pi..pi
                if (angleErr > M_PI)
                    angleErr -= M_PI * 2;
                if (angleErr < -M_PI)
                    angleErr += M_PI * 2;

                // the autodetector
                if (fabs(angleErr) > M_PI / 2)
                    angleDesire += M_PI;

                // external tangency. The angle stored is offset by Pi/2 so that a value of 0.0 is
                // invalid and treated as "undecided".
                cstr->setValue(angleDesire + angleOffset);
            }
        }
    }
    catch (Base::Exception& e) {
        // failure to determine tangency type is not a big deal, so a warning.
        Base::Console().Warning("Error in AutoLockTangency. %s \n", e.what());
        return false;
    }
    return true;
}

int SketchObject::autoRemoveRedundants(bool updategeo)
{
    auto redundants = getLastRedundant();

    if (redundants.empty())
        return 0;

    // getLastRedundant is base 1, while delConstraints is base 0
    for (size_t i = 0; i < redundants.size(); i++)
        redundants[i]--;

    delConstraints(redundants, updategeo);

    return redundants.size();
}

int SketchObject::renameConstraint(int GeoId, std::string name)
{
    // only change the constraint item if the names are different
    const Constraint* item = Constraints[GeoId];

    if (item->Name != name) {
        // no need to check input data validity as this is an sketchobject managed operation.
        Base::StateLocker lock(managedoperation, true);

        Constraint* copy = item->clone();
        copy->Name = name;

        Constraints.set1Value(GeoId, copy);
        delete copy;

        // make sure any prospective solver access updates the constraint pointer that just got
        // invalidated
        solverNeedsUpdate = true;

        return 0;
    }
    return -1;
}

// clang-format on
