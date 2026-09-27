/***************************************************************************
 *   Copyright (c) 2010 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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
#ifndef _PreComp_
# include <algorithm>
# include <cmath>
# include <cstring>
# include <BRepFeat_MakeRevol.hxx>
# include <gp_Ax1.hxx>
# include <gp_Ax2.hxx>
# include <gp_Lin.hxx>
# include <gp_Pln.hxx>
# include <Precision.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS.hxx>
#endif

#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Mod/Part/App/PartFeature.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include "FeatureRevolved.h"

using namespace PartDesign;

namespace PartDesign {

PROPERTY_SOURCE_ABSTRACT(PartDesign::Revolved, PartDesign::ProfileBased)

const char* Revolved::SideTypeEnums[] = {"One side", "Two sides", "Symmetric", nullptr};

// Signed: a negative angle turns the other way, and on two sides runs back
// into the other side (upstream b2da06bfe0)
const App::PropertyAngle::Constraints Revolved::floatAngle = { -360.0, 360.0, 1.0 };

namespace {

constexpr double fullTurn = 2.0 * M_PI;

bool isValue(const App::PropertyEnumeration &prop, const char *value)
{
    const char *current = prop.isValid() ? prop.getValueAsString() : nullptr;
    return current && strcmp(current, value) == 0;
}

/// The centre the profile's centre of gravity turns about, and the radial
/// vector from there to it
bool getProfileOrbit(const TopoShape& profileShape, const gp_Ax1& axis, gp_Pnt& center, gp_Vec& radial)
{
    Base::Vector3d cog;
    if (!profileShape.getCenterOfGravity(cog))
        return false;
    const gp_Pnt point(cog.x, cog.y, cog.z);
    const gp_Vec axisVector(axis.Direction());
    center = axis.Location().Translated(axisVector * gp_Vec(axis.Location(), point).Dot(axisVector));
    radial = gp_Vec(center, point);
    return radial.Magnitude() > Precision::Confusion();
}

double normalizeAngle(double angle)
{
    angle = Base::fmod(angle, fullTurn);
    return fullTurn - angle < Precision::Angular() ? 0.0 : angle;
}

/** The smallest turn about axis that brings the profile's centre of gravity
 * onto plane, if the circle it runs on meets the plane (upstream 06a4b1db99)
 */
bool getPlanarStartAngle(const TopoShape& profileShape, const gp_Pln& plane,
                         const gp_Ax1& axis, double& angle)
{
    gp_Pnt center;
    gp_Vec radial;
    if (!getProfileOrbit(profileShape, axis, center, radial))
        return false;

    // The point turned by t is center + cos(t) radial + sin(t) tangent;
    // on the plane when a cos(t) + b sin(t) + c = 0
    const gp_Vec normal(plane.Axis().Direction());
    const gp_Vec tangent = gp_Vec(axis.Direction()).Crossed(radial);
    const double a = radial.Dot(normal);
    const double b = tangent.Dot(normal);
    const double c = gp_Vec(plane.Location(), center).Dot(normal);
    const double amplitude = std::hypot(a, b);

    if (amplitude <= Precision::Confusion()) {
        // The circle is parallel to the plane: in it, or never meeting it
        if (std::fabs(c) <= Precision::Confusion()) {
            angle = 0.0;
            return true;
        }
        return false;
    }

    const double solution = -c / amplitude;
    if (solution < -1.0 - Precision::Confusion() || solution > 1.0 + Precision::Confusion())
        return false;

    const double phase = std::atan2(b, a);
    const double delta = std::acos(std::clamp(solution, -1.0, 1.0));
    angle = std::min(normalizeAngle(phase + delta), normalizeAngle(phase - delta));
    return true;
}

} // anonymous namespace

Revolved::Revolved()
{
    ADD_PROPERTY_TYPE(StartType, (0L), "Start", App::Prop_None,
                      "How to define the start of the revolution");
    StartType.setEnums(StartTypesEnums);
    ADD_PROPERTY_TYPE(StartOffset, (0.0), "Start", App::Prop_None,
                      "Angular offset from the profile, or from the start reference");
    ADD_PROPERTY_TYPE(StartReference, (nullptr), "Start", App::Prop_None,
                      "Face, plane or sketch the revolution starts at, with StartType Reference");
    StartOffset.setConstraints(&floatAngle);
    StartOffset.setReadOnly(true);
    StartReference.setReadOnly(true);
    Angle.setConstraints(&floatAngle);
    Angle2.setConstraints(&floatAngle);
}

short Revolved::mustExecute() const
{
    if (Placement.isTouched() ||
        SideType.isTouched() ||
        Type.isTouched() ||
        Type2.isTouched() ||
        ReferenceAxis.isTouched() ||
        Axis.isTouched() ||
        Base.isTouched() ||
        UpToFace.isTouched() ||
        UpToFace2.isTouched() ||
        Angle.isTouched() ||
        Angle2.isTouched() ||
        StartType.isTouched() ||
        StartOffset.isTouched() ||
        StartReference.isTouched())
        return 1;
    return ProfileBased::mustExecute();
}

Revolved::RevolMethod Revolved::methodOf(const App::PropertyEnumeration &type)
{
    auto method = static_cast<RevolMethod>(type.getValue());
    return method == RevolMethod::TwoAngles ? RevolMethod::Angle : method;
}

bool Revolved::isAdditive() const
{
    return const_cast<Revolved*>(this)->getAddSubType() == FeatureAddSub::Additive;
}

App::DocumentObjectExecReturn *Revolved::executeRevolved()
{
    // What the class is, not the Operation the panel may switch: a Groove
    // needs a base, a Revolution may turn an open profile into a shell
    const bool groove = isGroove();
    const bool symmetric = isValue(SideType, "Symmetric");
    // Type TwoAngles is two sides, should a script get it past onChanged()
    const bool twoSides = isValue(SideType, "Two sides") || isValue(Type, "TwoAngles");
    const RevolMethod method = methodOf(Type);
    const RevolMethod method2 = twoSides ? methodOf(Type2) : RevolMethod::Angle;
    // Revolution's second Type is UpToLast where Groove's is ThroughAll
    auto isThroughAll = [groove](RevolMethod m) {
        return groove && m == RevolMethod::ThroughAll;
    };
    auto isUpTo = [&isThroughAll](RevolMethod m) {
        return m != RevolMethod::Angle && !isThroughAll(m);
    };

    // Validate parameters
    // All angles are in radians unless explicitly stated
    if (std::fabs(Angle.getValue()) > 360.0 || std::fabs(Angle2.getValue()) > 360.0)
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Angle of revolution too large"));

    const double angle = Base::toRadians<double>(Angle.getValue());
    const double angle2 = twoSides ? Base::toRadians<double>(Angle2.getValue()) : 0.0;
    if (method == RevolMethod::Angle && std::fabs(angle) < Precision::Angular()
            && (!twoSides || (method2 == RevolMethod::Angle && std::fabs(angle2) < Precision::Angular())))
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Angle of revolution too small"));
    if (twoSides && method == RevolMethod::Angle && method2 == RevolMethod::Angle
            && std::fabs(angle + angle2) < Precision::Angular())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                    "The two revolution angles cancel each other"));

    TopoShape sketchshape;
    try {
        // A revolution may turn an open profile into a shell
        sketchshape = getVerifiedFace(/*silent*/false, /*doFit*/true, /*allowOpen*/!groove);
    } catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    // if the Base property has a valid shape, fuse the AddShape into it
    TopoShape base;
    if (!groove)
        base = getBaseShape(/*silent*/true, /*force*/false, /*checkSolid*/false);
    else {
        try {
            if (!NewSolid.getValue())
                base = getBaseShape();
        }
        catch (const Base::Exception&) {
            std::string text(QT_TRANSLATE_NOOP("Exception", "The requested feature cannot be created. The reason may be that:\n"
                                        "  - the active Body does not contain a base shape, so there is no\n"
                                        "  material to be removed;\n"
                                        "  - the selected sketch does not belong to the active Body."));
            return new App::DocumentObjectExecReturn(text);
        }
    }

    // update Axis from ReferenceAxis
    try {
        updateAxis();
    } catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

    // get revolve axis
    Base::Vector3d b = Base.getValue();
    gp_Pnt pnt(b.x,b.y,b.z);
    Base::Vector3d v = Axis.getValue();
    if (v.Length() < Precision::Confusion())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Reference axis is invalid"));
    gp_Dir dir(v.x,v.y,v.z);

    try {
        if (sketchshape.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Creating a face from sketch failed"));

        TopLoc_Location invObjLoc = this->positionByPrevious();
        pnt.Transform(invObjLoc.Transformation());
        dir.Transform(invObjLoc.Transformation());
        base.move(invObjLoc);
        sketchshape.move(invObjLoc);
        if (Reversed.getValue())
            dir.Reverse();
        const gp_Ax1 axis(pnt, dir);

        // Turn the profile to its start once, before any branch, so that
        // the angles, the up-to faces and the sides all build from there
        const double start = startAngle(sketchshape, axis, invObjLoc);
        if (std::fabs(start) >= Precision::Angular()) {
            gp_Trsf mov;
            mov.SetRotation(startAxis(axis), start);
            sketchshape = sketchshape.moved(TopLoc_Location(mov));
        }

        // Check distance between sketchshape and axis - to avoid failures and crashes
        TopExp_Explorer xp;
        xp.Init(sketchshape.getShape(), TopAbs_FACE);
        for (;xp.More(); xp.Next()) {
            if (checkLineCrossesFace(gp_Lin(pnt, dir), TopoDS::Face(xp.Current())))
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Revolve axis intersects the sketch"));
        }

        // The support face glues an up-to side onto the base; it is where
        // the profile was, so a profile turned off it goes without
        TopoShape supportface;
        if ((isUpTo(method) || (twoSides && isUpTo(method2))) && std::fabs(start) < Precision::Angular()) {
            try {
                supportface = getSupportFace();
            } catch (const Base::Exception&) {
                supportface = TopoShape();
            }
            if (supportface.countSubShapes(TopAbs_WIRE) == 0)
                supportface = TopoShape();
            else
                supportface.move(invObjLoc);
        }

        TopoShape result(0,getDocument()->getStringHasher());
        // One side of a Revolution up to a face: the base with the side
        // fused in, which the Shape is made of as it always was, so that
        // its element names stay
        TopoShape baseResult;
        std::vector<TopoShape> sides;
        auto addSide = [&sides](const TopoShape &side) {
            if (!side.isNull())
                sides.push_back(side);
        };
        // Up-to sides meet the other side at the profile; XOR would work
        // too, but a fuse keeps the preview in one piece (upstream)
        bool fuseSides = false;

        if (twoSides) {
            const bool angles = method == RevolMethod::Angle && method2 == RevolMethod::Angle;
            if (isThroughAll(method) || isThroughAll(method2)
                    || (angles && std::fabs(angle + angle2) >= fullTurn - Precision::Angular())) {
                // Through all makes the other side irrelevant; two angles
                // that cover the circle between them are a full turn
                generateRevolution(result, sketchshape, axis, fullTurn, 0.0, false, RevolMethod::Angle);
            }
            else if (angles) {
                // One sweep between the two limits, as the old TwoAngles
                // made it, so its element names stay. For signed angles it
                // is the XOR of the sides: 50 and -20 is 20 to 50.
                generateRevolution(result, sketchshape, axis, angle, angle2, false, RevolMethod::TwoAngles);
            }
            else {
                fuseSides = isUpTo(method) || isUpTo(method2);
                addSide(makeSide(method, angle, UpToFace, sketchshape, base,
                                 supportface, invObjLoc, axis));
                addSide(makeSide(method2, angle2, UpToFace2, sketchshape, base,
                                 supportface, invObjLoc, axis.Reversed()));
            }
        }
        else if (symmetric) {
            if (method == RevolMethod::Angle)
                generateRevolution(result, sketchshape, axis, angle, 0.0, true, RevolMethod::Angle);
            else if (isThroughAll(method))
                generateRevolution(result, sketchshape, axis, fullTurn, 0.0, false, RevolMethod::Angle);
            else {
                // Up to the face one way, and up to its mirror in the
                // profile plane the other
                fuseSides = true;
                TopoShape upToFace = getRevolutionUpToFace(method, UpToFace, base, sketchshape,
                                                           invObjLoc, axis);
                addSide(revolveUpTo(base, sketchshape, supportface, upToFace, axis));

                gp_Pln plane;
                if (!sketchshape.findPlane(plane))
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                                "Could not determine the sketch plane"));
                TopoShape mirrored(0, getDocument()->getStringHasher());
                mirrored.makEMirror(upToFace,
                                    gp_Ax2(plane.Location(), plane.Axis().Direction()));
                addSide(revolveUpTo(base, sketchshape, supportface, mirrored, axis.Reversed()));
            }
        }
        else if (isThroughAll(method))
            generateRevolution(result, sketchshape, axis, fullTurn, 0.0, false, RevolMethod::Angle);
        else
            result = makeSide(method, angle, UpToFace, sketchshape, base, supportface, invObjLoc,
                              axis, &baseResult);

        if (sides.size() == 1)
            result = sides.front();
        else if (sides.size() > 1) {
            if (fuseSides)
                result.makEBoolean(Part::OpCodes::Fuse, sides, Part::OpCodes::Revolve);
            else
                result = xorSides(sides, getDocument()->getStringHasher(), Part::OpCodes::Revolve);
        }
        if (result.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Could not revolve the sketch!"));

        if (!groove)
            result = refineShapeIfActive(result);

        // eventually disable some settings that are not valid for the current method
        updateProperties();

        // set the additive shape property for later usage in e.g. pattern
        this->AddSubShape.setValue(result);
        if (isRecomputePaused())
            return App::DocumentObject::StdReturn;

        this->Shape.setValue(makeBoolean(base, baseResult.isNull() ? result : baseResult));

        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {

        if (std::string(e.GetMessageString()) == "TopoDS::Face")
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Could not create face from sketch.\n"
                "Intersecting sketch entities in a sketch are not allowed."));
        else
            return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }
}

TopoShape Revolved::makeSide(RevolMethod method,
                             double angle,
                             const App::PropertyLinkSub& upToFace,
                             const TopoShape& sketchshape,
                             const TopoShape& base,
                             const TopoShape& supportface,
                             const TopLoc_Location& invObjLoc,
                             const gp_Ax1& axis,
                             TopoShape* baseResult) const
{
    TopoShape revol(0, getDocument()->getStringHasher());
    if (method == RevolMethod::Angle) {
        if (std::fabs(angle) < Precision::Angular())
            return TopoShape();
        generateRevolution(revol, sketchshape, axis, angle, 0.0, false, RevolMethod::Angle);
    }
    else if (method == RevolMethod::ThroughAll && isGroove())
        generateRevolution(revol, sketchshape, axis, fullTurn, 0.0, false, RevolMethod::Angle);
    else {
        TopoShape face = getRevolutionUpToFace(method, upToFace, base, sketchshape, invObjLoc, axis);
        revol = revolveUpTo(base, sketchshape, supportface, face, axis, baseResult);
    }
    return revol;
}

TopoShape Revolved::revolveUpTo(const TopoShape& base,
                                const TopoShape& sketchshape,
                                const TopoShape& supportface,
                                const TopoShape& upToFace,
                                const gp_Ax1& axis,
                                TopoShape* baseResult) const
{
    auto hasher = getDocument()->getStringHasher();

    // To cut, or to keep what is common, cut up to the face from the base:
    // what went is the tool (upstream b2da06bfe0)
    if (!isAdditive() && !base.isNull()) {
        try {
            TopoShape cut(0, hasher);
            generateRevolution(cut, base, sketchshape, supportface, upToFace, axis,
                               BRepFeatMode::CutFromBase);
            TopoShape removed(0, hasher);
            removed.makECut({base, cut}, Part::OpCodes::Revolve);
            if (removed.hasSubShape(TopAbs_SOLID))
                return removed;
        } catch (const Base::Exception&) {
        } catch (const Standard_Failure&) {
        }
    }

    // BRepFeat gives the base with the side fused in, not the side alone
    TopoShape fused(0, hasher);
    generateRevolution(fused, base, sketchshape, supportface, upToFace, axis, BRepFeatMode::None);
    if (baseResult && isAdditive())
        *baseResult = fused;
    TopoShape tool(0, hasher);
    tool.makECut({fused, base}, Part::OpCodes::Revolve);
    return tool;
}

TopoShape Revolved::getRevolutionUpToFace(RevolMethod method,
                                          const App::PropertyLinkSub& upToFace,
                                          const TopoShape& base,
                                          const TopoShape& sketchshape,
                                          const TopLoc_Location& invObjLoc,
                                          const gp_Ax1& axis) const
{
    TopoShape face;
    if (method == RevolMethod::ToFace) {
        getUpToFaceFromLinkSub(face, upToFace);
        face.move(invObjLoc);
        if (face.shapeType(true) != TopAbs_FACE)
            face = face.getSubTopoShape(TopAbs_FACE, 1);
    }
    else if (method == RevolMethod::ToFirst || method == RevolMethod::ToLast) {
        // Upstream 80664a0d30: the faces of the base the profile meets
        // turning about the axis
        getUpToFace(face, base, sketchshape,
                    method == RevolMethod::ToFirst ? "UpToFirst" : "UpToLast", axis);
    }
    else
        THROWM(Base::RuntimeError, "ProfileBased: Internal error: Unknown method for getRevolutionUpToFace()")
    return face;
}

bool Revolved::suggestReversed()
{
    try {
        updateAxis();
    } catch (const Base::Exception&) {
        return false;
    }

    return suggestReversedAngle(ProfileBased::getReversedAngle(Base.getValue(), Axis.getValue()));
}

void Revolved::updateAxis()
{
    App::DocumentObject *pcReferenceAxis = ReferenceAxis.getValue();
    const std::vector<std::string> &subReferenceAxis = ReferenceAxis.getSubValues();
    Base::Vector3d base;
    Base::Vector3d dir;
    getAxis(pcReferenceAxis, subReferenceAxis, base, dir, ForbiddenAxis::NotParallelWithNormal);

    if (dir.Length() > Precision::Confusion()) {
        Base.setValue(base.x,base.y,base.z);
        Axis.setValue(dir.x,dir.y,dir.z);
    }
}

gp_Ax1 Revolved::startAxis(const gp_Ax1& revolutionAxis) const
{
    // Reversed does not turn a symmetric revolution, so neither does it
    // turn its start
    if (Reversed.getValue() && isValue(SideType, "Symmetric"))
        return revolutionAxis.Reversed();
    return revolutionAxis;
}

double Revolved::startAngle(const TopoShape& profile, const gp_Ax1& axis,
                            const TopLoc_Location& invObjLoc) const
{
    if (isValue(StartType, "Offset"))
        return Base::toRadians<double>(StartOffset.getValue());
    if (isValue(StartType, "Reference"))
        return getStartReferenceAngle(profile, startAxis(axis),
                                      Base::toRadians<double>(StartOffset.getValue()), invObjLoc);
    return 0.0;
}

double Revolved::getStartOffset() const
{
    // In global coordinates, where the profile, the reference and the axis
    // the last recompute stated all are
    if (isValue(StartType, "Profile plane"))
        return 0.0;
    const Base::Vector3d base = Base.getValue();
    const Base::Vector3d dir = Axis.getValue();
    gp_Ax1 axis(gp_Pnt(base.x, base.y, base.z), gp_Dir(dir.x, dir.y, dir.z));
    if (Reversed.getValue())
        axis.Reverse();
    return Base::toDegrees<double>(startAngle(getVerifiedFace(), axis, TopLoc_Location()));
}

double Revolved::getStartReferenceAngle(const TopoShape& profileShape,
                                        const gp_Ax1& axis,
                                        double offset,
                                        const TopLoc_Location& invObjLoc) const
{
    // Upstream starts at the profile until a reference is picked
    if (!StartReference.getValue())
        return 0.0;

    TopoShape referenceShape = getStartReferenceShape(StartReference, invObjLoc);

    // A plane is met wherever the circle crosses it; anything else must be
    // cut by the circle
    gp_Pln plane;
    double angle = 0.0;
    if (referenceShape.findPlane(plane) && getPlanarStartAngle(profileShape, plane, axis, angle))
        return angle + offset;

    gp_Pnt center;
    gp_Vec radial;
    if (!getProfileOrbit(profileShape, axis, center, radial))
        THROWM(Base::ValueError, "Revolved: Cannot determine the profile path around the axis")

    auto faces = Part::findAllFacesCutBy(referenceShape, profileShape, axis);
    if (faces.empty())
        THROWM(Base::ValueError, "Revolved: Start reference does not intersect the profile path")

    double arc = faces.front().distsq;
    for (const auto& face : faces)
        arc = std::min(arc, face.distsq);
    return arc / radial.Magnitude() + offset;
}

void Revolved::generateRevolution(TopoShape& revol,
                                  const TopoShape& sketchshape,
                                  const gp_Ax1& axis,
                                  double angle,
                                  double angle2,
                                  bool midplane,
                                  RevolMethod method) const
{
    double angleTotal = angle;
    double angleOffset = 0.;

    if (method == RevolMethod::TwoAngles) {
        // Rotate the face by `angle2`/`angle` to get "second" angle
        angleTotal += angle2;
        angleOffset = angle2 * -1.0;
    }
    else if (midplane) {
        // Rotate the face by half the angle to get Revolution symmetric to sketch plane
        angleOffset = -angle / 2;
    }

    if (fabs(angleTotal) < Precision::Angular())
        THROWM(Base::ValueError, "Cannot create a revolution with zero angle.")

    TopoShape from = sketchshape;
    if (method == RevolMethod::TwoAngles || midplane) {
        gp_Trsf mov;
        mov.SetRotation(axis, angleOffset);
        TopLoc_Location loc(mov);
        from.move(loc);
    }

    // revolve the face to a solid
    try {
        revol.makERevolve(from, axis, angleTotal);
    }catch(Standard_Failure &) {
        THROWM(Base::RuntimeError, "ProfileBased: RevolMaker failed! Could not revolve the sketch!")
    }
}

void Revolved::generateRevolution(TopoShape& revol,
                                  const TopoShape& baseshape,
                                  const TopoShape& profileshape,
                                  const TopoShape& supportface,
                                  const TopoShape& uptoface,
                                  const gp_Ax1& axis,
                                  BRepFeatMode mode) const
{
    if (baseshape.isNull())
        THROWM(Base::ValueError, "Revolving up to a face needs a base shape to revolve onto")

    BRepFeat_MakeRevol RevolMaker;
    TopoShape base = baseshape;
    // A face of the base itself is not taken as the end: the revolution
    // runs through it, a full turn round. A copy is.
    TopoDS_Shape until = uptoface.makECopy().getShape();
    for (const auto &face : profileshape.getSubShapes(TopAbs_FACE)) {
        RevolMaker.Init(base.getShape(),
                        TopoDS::Face(face),
                        TopoDS::Face(supportface.getShape()),
                        axis, static_cast<int>(mode), Standard_True);
        RevolMaker.Perform(TopoDS::Face(until));
        if (!RevolMaker.IsDone())
            THROWM(Base::RuntimeError, "ProfileBased: Up to face: Could not revolve the sketch!")

        std::vector<TopoShape> sources {base, profileshape};
        if (!supportface.isNull())
            sources.push_back(supportface);
        revol.makEShape(RevolMaker, sources, Part::OpCodes::Revolve);

        if (mode == BRepFeatMode::None)
            mode = BRepFeatMode::FuseWithBase;

        base = revol;
    }
    revol = base;
}

void Revolved::updateProperties()
{
    // disable settings that are not valid on the current method
    const bool twoSides = isValue(SideType, "Two sides");
    const RevolMethod method = methodOf(Type);
    const RevolMethod method2 = methodOf(Type2);

    Angle.setReadOnly(method != RevolMethod::Angle);
    UpToFace.setReadOnly(method != RevolMethod::ToFace);
    Type2.setReadOnly(!twoSides);
    Angle2.setReadOnly(!twoSides || method2 != RevolMethod::Angle);
    UpToFace2.setReadOnly(!twoSides || method2 != RevolMethod::ToFace);
    // Midplane is SideType's alias; Reversed turns every side
    Midplane.setReadOnly(false);
    Reversed.setReadOnly(false);
}

void Revolved::onDocumentRestored()
{
    {
        Base::StateLocker guard(syncingSides);
        if (isValue(Type, "TwoAngles")) {
            // The old TwoAngles ignored Midplane
            Type.setValue("Angle");
            Type2.setValue("Angle");
            SideType.setValue("Two sides");
        }
        else if (Midplane.getValue() && isValue(SideType, "One side")) {
            // Midplane was read only for the angles: an up-to feature built
            // one side, whatever it said
            RevolMethod method = methodOf(Type);
            if (method == RevolMethod::Angle
                    || (method == RevolMethod::ThroughAll && isGroove()))
                SideType.setValue("Symmetric");
        }
        if (isValue(Type2, "TwoAngles"))
            Type2.setValue("Angle");
        // Midplane is Symmetric's alias; upstream saves it false
        bool symmetric = isValue(SideType, "Symmetric");
        if (Midplane.getValue() != symmetric)
            Midplane.setValue(symmetric);
    }
    updateProperties();
    const std::string type = StartType.getValueAsString();
    StartOffset.setReadOnly(type == "Profile plane");
    StartReference.setReadOnly(type != "Reference");
    ProfileBased::onDocumentRestored();
}

void Revolved::onChanged(const App::Property* prop)
{
    // Keep the aliases in step: Midplane with Symmetric, TwoAngles as two
    // sides. Undo and redo bring back values that were in step already.
    if (!isRestoring() && !syncingSides
            && !(getDocument() && getDocument()->isPerformingTransaction())) {
        Base::StateLocker guard(syncingSides);
        if (prop == &Midplane) {
            if (Midplane.getValue())
                SideType.setValue("Symmetric");
            else if (isValue(SideType, "Symmetric"))
                SideType.setValue("One side");
        }
        else if (prop == &SideType) {
            bool symmetric = isValue(SideType, "Symmetric");
            if (Midplane.getValue() != symmetric)
                Midplane.setValue(symmetric);
        }
        else if (prop == &Type) {
            if (isValue(Type, "TwoAngles")) {
                Type.setValue("Angle");
                Type2.setValue("Angle");
                SideType.setValue("Two sides");
                Midplane.setValue(false);
            }
        }
        else if (prop == &Type2) {
            if (isValue(Type2, "TwoAngles"))
                Type2.setValue("Angle");
        }
    }

    if (prop == &StartType && StartType.isValid()) {
        const std::string type = StartType.getValueAsString();
        StartOffset.setReadOnly(type == "Profile plane");
        StartReference.setReadOnly(type != "Reference");
    }

    ProfileBased::onChanged(prop);
}

} // namespace PartDesign
