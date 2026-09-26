/***************************************************************************
 *   Copyright (c) 2020 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <BRepPrimAPI_MakePrism.hxx>
# include <gp_Dir.hxx>
# include <Precision.hxx>
# include <TopExp_Explorer.hxx>
# include <TopoDS_Compound.hxx>
# include <TopoDS_Face.hxx>
# include <TopoDS_Shape.hxx>
#endif

#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/MappedElement.h>
#include <App/OriginFeature.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Mod/Part/App/ExtrusionHelper.h>
#include <Mod/Part/App/PartParams.h>
#include <Mod/Part/App/TopoShapeOpCode.h>

#include "FeatureExtrude.h"

FC_LOG_LEVEL_INIT("PartDesign", true, true)

using namespace PartDesign;

PROPERTY_SOURCE_ABSTRACT(PartDesign::FeatureExtrude, PartDesign::ProfileBased)

App::PropertyQuantityConstraint::Constraints FeatureExtrude::signedLengthConstraint = { -DBL_MAX, DBL_MAX, 1.0 };
double FeatureExtrude::maxAngle = 90 - Base::toDegrees<double>(Precision::Angular());
App::PropertyAngle::Constraints FeatureExtrude::floatAngle = { -maxAngle, maxAngle, 1.0 };
const char* FeatureExtrude::SideTypeEnums[] = {"One side", "Two sides", "Symmetric", nullptr};

namespace {

bool isUpToMethod(const std::string &method)
{
    return method == "UpToFirst" || method == "UpToLast"
        || method == "UpToFace" || method == "UpToShape";
}

/// A whole object that is one face: an App::Plane, a datum plane
bool isSingleFaceObject(App::DocumentObject *obj)
{
    if (obj->isDerivedFrom<App::Plane>())
        return true;
    return Part::Feature::getTopoShape(obj).countSubShapes(TopAbs_FACE) == 1;
}

bool isWholeObject(const std::vector<std::string> &subs)
{
    return subs.empty() || (subs.size() == 1 && subs[0].empty());
}

/// The sides combined as upstream does (a346c266e7): their union less what
/// they share, so that a side running back into the other cancels it, as a
/// negative length did in the old TwoLengths
TopoShape xorSides(const std::vector<TopoShape> &sides, App::StringHasherRef hasher)
{
    if (sides.size() == 1)
        return sides.front();
    TopoShape common(0, hasher);
    try {
        common.makEBoolean(Part::OpCodes::Common, sides);
    } catch (Base::Exception &) {
        common = TopoShape();
    } catch (Standard_Failure &) {
        common = TopoShape();
    }
    TopoShape result(0, hasher);
    if (common.isNull() || !common.hasSubShape(TopAbs_SOLID)) {
        result.makEBoolean(Part::OpCodes::Fuse, sides, Part::OpCodes::Extrude);
        return result;
    }
    TopoShape fused(0, hasher);
    fused.makEBoolean(Part::OpCodes::Fuse, sides);
    result.makEBoolean(Part::OpCodes::Cut, {fused, common}, Part::OpCodes::Extrude);
    return result;
}

} // anonymous namespace

FeatureExtrude::FeatureExtrude() = default;

void FeatureExtrude::initProperties(const char *group)
{
    ADD_PROPERTY_TYPE(SideType, (0L), group, App::Prop_None,
            "How the extrusion goes from the profile: to one side, to each side\n"
            "by its own type, or symmetric about the profile");
    SideType.setEnums(SideTypeEnums);
    ADD_PROPERTY_TYPE(Type2, (0L), group, App::Prop_None, "Extrusion type of the second side");
    ADD_PROPERTY_TYPE(Length, (10.0), group, App::Prop_None, "Extrusion length");
    ADD_PROPERTY_TYPE(Length2, (10.0), group, App::Prop_None, "Extrusion length in 2nd direction");
    ADD_PROPERTY_TYPE(UseCustomVector, (false), group, App::Prop_None, "Use custom vector for pad direction");
    ADD_PROPERTY_TYPE(Direction, (Base::Vector3d(1.0, 1.0, 1.0)), group, App::Prop_None, "Extrusion direction vector");
    ADD_PROPERTY_TYPE(ReferenceAxis, (nullptr), group, App::Prop_None, "Reference axis of direction");
    ADD_PROPERTY_TYPE(AlongSketchNormal, (true), group, App::Prop_None, "Measure pad length along the sketch normal direction");
    ADD_PROPERTY_TYPE(UpToFace, (nullptr), group, App::Prop_None, "Face where pad will end");
    ADD_PROPERTY_TYPE(UpToShape, (nullptr), group, App::Prop_None, "Faces or shapes where pad will end");
    ADD_PROPERTY_TYPE(Offset, (0.0), group, App::Prop_None, "Offset from face in which pad will end");
    Offset.setConstraints(&signedLengthConstraint);
    ADD_PROPERTY_TYPE(UpToFace2, (nullptr), group, App::Prop_None, "Face where the second side will end");
    ADD_PROPERTY_TYPE(UpToShape2, (nullptr), group, App::Prop_None, "Faces or shapes where the second side will end");
    ADD_PROPERTY_TYPE(Offset2, (0.0), group, App::Prop_None, "Offset from face in which the second side will end");
    Offset2.setConstraints(&signedLengthConstraint);
    ADD_PROPERTY_TYPE(TaperAngle,(0.0), group, App::Prop_None, "Sets the angle of slope (draft) to apply to the sides. The angle is for outward taper; negative value yields inward tapering.");
    TaperAngle.setConstraints(&floatAngle);
    ADD_PROPERTY_TYPE(TaperAngle2, (0.0), group, App::Prop_None, "Alias to TaperAngleRev, for compatibility to upstream");
    TaperAngle2.setConstraints(&floatAngle);

    // Remove the constraints and keep the type to allow to accept negative values
    // https://forum.freecad.org/viewtopic.php?f=3&t=52075&p=448410#p447636
    Length2.setConstraints(nullptr);

    ADD_PROPERTY_TYPE(Offset, (0.0), group, App::Prop_None, "Offset from face in which pad will end");
    static const App::PropertyQuantityConstraint::Constraints signedLengthConstraint = {-DBL_MAX, DBL_MAX, 1.0};
    Offset.setConstraints(&signedLengthConstraint);

    ADD_PROPERTY_TYPE(TaperAngleRev,(0.0), group, App::Prop_None, "Taper angle of reverse part of padding.");
    ADD_PROPERTY_TYPE(TaperInnerAngle,(0.0), group, App::Prop_None, "Taper angle of inner holes.");
    ADD_PROPERTY_TYPE(TaperInnerAngleRev,(0.0), group, App::Prop_None, "Taper angle of the reverse part for inner holes.");
    ADD_PROPERTY_TYPE(AutoTaperInnerAngle,(true), group, App::Prop_None,
            "Automatically set inner taper angle to the negative of (outer) taper angle.\n"
            "If false, then inner taper angle can be set independent of taper angle.");

    ADD_PROPERTY_TYPE(UsePipeForDraft,(false), group, App::Prop_None, "Use pipe (i.e. sweep) operation to create draft angles.");
    ADD_PROPERTY_TYPE(CheckUpToFaceLimits,(true), group, App::Prop_None,
            "When using 'UpToXXXX' method, check whether the sketch shape is within\n"
            "the up-to-face. And remove the up-to-face limitation to make padding/extrusion\n"
            "work. Note that you may want to disable this if the up-to-face is concave.");
}

short FeatureExtrude::mustExecute() const
{
    if (Placement.isTouched() ||
        SideType.isTouched() ||
        Type.isTouched() ||
        Type2.isTouched() ||
        Offset2.isTouched() ||
        UpToFace2.isTouched() ||
        UpToShape2.isTouched() ||
        Length.isTouched() ||
        Length2.isTouched() ||
        TaperAngle.isTouched() ||
        TaperAngle2.isTouched() ||
        UseCustomVector.isTouched() ||
        Direction.isTouched() ||
        ReferenceAxis.isTouched() ||
        AlongSketchNormal.isTouched() ||
        Offset.isTouched() ||
        UpToFace.isTouched() ||
        UpToShape.isTouched())
        return 1;
    return ProfileBased::mustExecute();
}

Base::Vector3d FeatureExtrude::computeDirection(const Base::Vector3d& sketchVector, bool inverse)
{
    Base::Vector3d extrudeDirection;

    if (!UseCustomVector.getValue()) {
        if (!ReferenceAxis.getValue()) {
            // use sketch's normal vector for direction
            extrudeDirection = sketchVector;
            AlongSketchNormal.setReadOnly(true);
        }
        else {
            // update Direction from ReferenceAxis
            App::DocumentObject* pcReferenceAxis = ReferenceAxis.getValue();
            const std::vector<std::string>& subReferenceAxis = ReferenceAxis.getSubValues();
            Base::Vector3d base;
            Base::Vector3d dir;
            getAxis(pcReferenceAxis, subReferenceAxis, base, dir, ForbiddenAxis::NotPerpendicularWithNormal);
            extrudeDirection = inverse ? -dir : dir;
        }
    }
    else {
        // use the given vector
        // if null vector, use sketchVector
        if ((fabs(Direction.getValue().x) < Precision::Confusion())
            && (fabs(Direction.getValue().y) < Precision::Confusion())
            && (fabs(Direction.getValue().z) < Precision::Confusion())) {
            Direction.setValue(sketchVector);
        }
        extrudeDirection = Direction.getValue();
    }

    // disable options of UseCustomVector
    Direction.setReadOnly(!UseCustomVector.getValue());
    ReferenceAxis.setReadOnly(UseCustomVector.getValue());
    // UseCustomVector allows AlongSketchNormal but !UseCustomVector does not forbid it
    if (UseCustomVector.getValue())
        AlongSketchNormal.setReadOnly(false);

    // explicitly set the Direction so that the dialog shows also the used direction
    // if the sketch's normal vector was used
    Direction.setValue(extrudeDirection);
    return extrudeDirection;
}

bool FeatureExtrude::hasTaperedAngle() const
{
    return fabs(TaperAngle.getValue()) > Base::toRadians(Precision::Angular()) ||
           fabs(TaperAngle2.getValue()) > Base::toRadians(Precision::Angular());
}

void FeatureExtrude::generatePrism(TopoShape& prism,
                                   TopoShape sketchTopoShape,
                                   const gp_Dir& dir,
                                   const double L,
                                   const double L2,
                                   const bool twoSides,
                                   const bool midplane,
                                   const bool reversed)
{
    double Ltotal = L;
    double Loffset = 0.;

    if (twoSides) {
        // One prism from -L2 to L, as the old TwoLengths made it, so that its
        // element names stay those of the files that have it
        Ltotal += L2;
        if (reversed)
            Loffset = -L;
        else
            Loffset = -L2;
    } else if (midplane)
        Loffset = -Ltotal/2;

    if (twoSides || midplane) {
        gp_Trsf mov;
        mov.SetTranslation(Loffset * gp_Vec(dir));
        TopLoc_Location loc(mov);
        sketchTopoShape.move(loc);
    } else if (reversed)
        Ltotal *= -1.0;

    // Without taper angle we create a prism because its shells are in every case no B-splines and can therefore
    // be use as support for further features like Pads, Lofts etc. B-spline shells can break certain features,
    // see e.g. https://forum.freecad.org/viewtopic.php?p=560785#p560785
    // It is better not to use BRepFeat_MakePrism here even if we have a support because the
    // resulting shape creates problems with Pocket
    try {
        prism.makEPrism(sketchTopoShape, Ltotal*gp_Vec(dir)); // finite prism
    }catch(Standard_Failure &) {
        THROWM(Base::RuntimeError, "FeatureExtrusion: Length: Could not extrude the sketch!")
    }
}

void FeatureExtrude::updateProperties()
{
    // disable settings that are not valid on the current method
    // disable everything unless we are sure we need it
    std::string sideType(SideType.getValueAsString());
    bool twoSides = sideType == "Two sides";
    bool symmetric = sideType == "Symmetric";

    struct SideFlags {
        bool length = false;
        bool taper = false;
        bool upTo = false;
        bool offset = false;
    };
    auto flagsOf = [](const std::string &method) {
        SideFlags flags;
        if (method == "Length") {
            flags.length = true;
            flags.taper = true;
        }
        else if (method == "ThroughAll")
            flags.taper = true;
        else if (method == "UpToFace" || method == "UpToShape") {
            // One reference edited as either (UpToShape, UpToFace its mirror)
            flags.upTo = true;
            flags.offset = true;
        }
        else if (method == "UpToFirst" || method == "UpToLast")
            flags.offset = true;
        return flags;
    };
    SideFlags side1 = flagsOf(Type.getValueAsString());
    SideFlags side2;
    if (twoSides)
        side2 = flagsOf(Type2.getValueAsString());

    Type2.setReadOnly(!twoSides);
    Length.setReadOnly(!side1.length);
    Length2.setReadOnly(!side2.length);
    AlongSketchNormal.setReadOnly(!side1.length && !side2.length);
    Offset.setReadOnly(!side1.offset);
    Offset2.setReadOnly(!side2.offset);
    TaperAngle.setReadOnly(!side1.taper);
    TaperAngle2.setReadOnly(!side2.taper);
    TaperAngleRev.setReadOnly(!side2.taper);
    Reversed.setReadOnly(symmetric);
    UpToFace.setReadOnly(!side1.upTo);
    UpToShape.setReadOnly(!side1.upTo);
    UpToFace2.setReadOnly(!side2.upTo);
    UpToShape2.setReadOnly(!side2.upTo);
}

void FeatureExtrude::setupObject()
{
    ProfileBased::setupObject();
    UsePipeForDraft.setValue(Part::PartParams::getUsePipeForExtrusionDraft());
}

App::DocumentObjectExecReturn *FeatureExtrude::buildExtrusion(ExtrudeOptions options)
{
    bool makeface = options.testFlag(ExtrudeOption::MakeFace);
    // bool fuse = options.testFlag(ExtrudeOption::MakeFuse);
    bool legacyPocket = options.testFlag(ExtrudeOption::LegacyPocket);
    bool inverseDirection = options.testFlag(ExtrudeOption::InverseDirection);

    std::string sideType(SideType.getValueAsString());
    bool twoSides = sideType == "Two sides";
    bool symmetric = sideType == "Symmetric";
    std::string method(Type.getValueAsString());
    std::string method2(twoSides ? Type2.getValueAsString() : "");
    bool upTo = isUpToMethod(method) || isUpToMethod(method2);

    // Validate parameters
    double L = method == "Length" ? Length.getValue() : 0.0;
    double L2 = method2 == "Length" ? Length2.getValue() : 0.0;
    if (!twoSides && method == "Length" && L < Precision::Confusion())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Length too small"));
    // Two sides take a negative second length, which cuts into the first
    if (twoSides && method == "Length" && method2 == "Length"
            && std::abs(L + L2) < Precision::Confusion())
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Length too small"));

    Part::Feature* obj = nullptr;
    TopoShape sketchshape;
    try {
        obj = getVerifiedObject();
        if (makeface) {
            sketchshape = getVerifiedFace();
        } else {
            std::vector<TopoShape> shapes;
            bool hasEdges = false;
            auto subs = Profile.getSubValues(false);
            if (subs.empty())
                subs.emplace_back("");
            bool failed = false;
            for (auto & sub : subs) {
                if (sub.empty() && subs.size()>1)
                    continue;
                TopoShape shape = Part::Feature::getTopoShape(obj, sub.c_str(), true);
                if (shape.isNull()) {
                    FC_ERR(getFullName() << ": failed to get profile shape "
                                        << obj->getFullName() << "." << sub);
                    failed = true;
                }
                hasEdges = hasEdges || shape.hasSubShape(TopAbs_EDGE);
                shapes.push_back(shape);
            }
            if (failed)
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Failed to obtain profile shape"));
            if (hasEdges)
                sketchshape.makEWires(shapes);
            else
                sketchshape.makECompound(shapes, nullptr, false);
        }
    } catch (const Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    } catch (const Standard_Failure& e) {
        return new App::DocumentObjectExecReturn(e.GetMessageString());
    }

    // if the Base property has a valid shape, fuse the prism into it
    TopoShape base = getBaseShape(/*silent*/true, /*force*/false, /*checkSolid*/!makeface);

    // get the normal vector of the sketch
    Base::Vector3d SketchVector = getProfileNormal();

    try {
        auto invObjLoc = this->positionByPrevious();
        auto invTrsf = invObjLoc.Transformation();

        base.move(invObjLoc);

        Base::Vector3d paddingDirection = computeDirection(SketchVector, inverseDirection);

        // create vector in padding direction with length 1
        gp_Dir dir(paddingDirection.x, paddingDirection.y, paddingDirection.z);

        // The length of a gp_Dir is 1 so the resulting pad would have
        // the length L in the direction of dir. But we want to have its height in the
        // direction of the normal vector.
        // Therefore we must multiply L by the factor that is necessary
        // to make dir as long that its projection to the SketchVector
        // equals the SketchVector.
        // This is the scalar product of both vectors.
        // Since the pad length cannot be negative, the factor must not be negative.

        double factor = fabs(dir * gp_Dir(SketchVector.x, SketchVector.y, SketchVector.z));

        // factor would be zero if vectors are orthogonal
        if (factor < Precision::Confusion()) {
            // For non-solid creation (e.g. pad a line to a face), we should
            // allow orthogonal direction (e.g. the normal of a line is the
            // line, which should be allowed to be extruded in any direction.)
            if (makeface) {
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                            "Creation failed because direction is orthogonal to sketch's normal vector"));
            }
        }
        else if (AlongSketchNormal.getValue()) {
            // perform the length correction if not along custom vector
            L = L / factor;
            L2 = L2 / factor;
        }

        // explicitly set the Direction so that the dialog shows also the used direction
        // if the sketch's normal vector was used
        Direction.setValue(paddingDirection);

        dir.Transform(invTrsf);

        if (sketchshape.isNull())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                        "Creating a face from sketch failed"));
        sketchshape.move(invObjLoc);

        TopoShape prism(0,getDocument()->getStringHasher());

        if (upTo && !twoSides && !symmetric) {
            // Note: This will return an unlimited planar face if support is a datum plane
            TopoShape supportface = getSupportFace();
            supportface.move(invObjLoc);

            if (Reversed.getValue())
                dir.Reverse();

            // Find a valid face or datum plane to extrude up to -- or, up to
            // shape (upstream 309dd6e30d), several faces, or the base when
            // nothing is chosen
            TopoShape upToFace;
            UpToSide side{method, UpToFace, UpToShape, Offset.getValue()};
            int faceCount = getUpToShape(upToFace, side, base, sketchshape, invObjLoc, dir);

            if (!supportface.hasSubShape(TopAbs_WIRE))
                supportface = TopoShape();
            if (legacyPocket) {
                auto mode = base.isNull() ? TopoShape::PrismMode::None
                                          : TopoShape::PrismMode::CutFromBase;
                prism = base.makEPrismUntil(sketchshape, supportface, upToFace,
                        dir, mode, CheckUpToFaceLimits.getValue());
                // DO NOT assign id to the generated prism, because this prism is
                // actually the final result. We obtain the subtracted shape by cut
                // this prism with the original base. Assigning a minus self id here
                // will mess up with preselection highlight. It is enough to re-tag
                // the profile shape above.
                //
                // prism.Tag = -this->getID();

                // And the really expensive way to get the SubShape...
                try {
                    TopoShape result(0,getDocument()->getStringHasher());
                    if (base.isNull())
                        result = prism;
                    else
                        result.makECut({base,prism});
                    result = refineShapeIfActive(result);
                    this->AddSubShape.setValue(result);
                }catch(Standard_Failure &) {
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Up to face: Could not get SubShape!"));
                }

                if (NewSolid.getValue())
                    prism = this->AddSubShape.getShape();
                else if (getAddSubType() == Intersecting)
                    prism.makEBoolean(Part::OpCodes::Common, {base, this->AddSubShape.getShape()});
                else if (getAddSubType() == Additive)
                    prism = base.makEFuse(this->AddSubShape.getShape());
                else
                    prism = refineShapeIfActive(prism);

                this->Shape.setValue(getSolid(prism));
                return App::DocumentObject::StdReturn;
            }
            try {
                prism.makEPrismUntil(base, sketchshape, supportface, upToFace,
                        dir, TopoShape::PrismMode::None, CheckUpToFaceLimits.getValue());
            }
            catch (const Base::Exception &) {
                if (faceCount > 1)
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                        "Unable to reach the selected shape, please select faces"));
                throw;
            }
        } else if (upTo) {
            // A side up to a shape with another side: each is made on its own
            // and the two combined. Symmetric, the other side is the first
            // one's mirror in the profile plane (upstream a346c266e7).
            TopoShape supportface = getSupportFace();
            supportface.move(invObjLoc);
            if (!supportface.hasSubShape(TopAbs_WIRE))
                supportface = TopoShape();

            gp_Dir dir1 = dir;
            if (Reversed.getValue() && !symmetric)
                dir1.Reverse();

            std::vector<TopoShape> sides;
            UpToSide side1{method, UpToFace, UpToShape, Offset.getValue()};
            TopoShape prism1 = makeSide(side1, base, sketchshape, supportface, invObjLoc,
                                        dir1, L, TaperAngle.getValue(),
                                        TaperInnerAngle.getValue(), makeface);
            if (!prism1.isNull())
                sides.push_back(prism1);
            if (symmetric) {
                if (!prism1.isNull()) {
                    gp_Dir normal(SketchVector.x, SketchVector.y, SketchVector.z);
                    normal.Transform(invTrsf);
                    Base::Vector3d center = sketchshape.getBoundBox().GetCenter();
                    sides.push_back(prism1.makEMirror(
                                gp_Ax2(gp_Pnt(center.x, center.y, center.z), normal)));
                }
            }
            else {
                UpToSide side2{method2, UpToFace2, UpToShape2, Offset2.getValue()};
                TopoShape prism2 = makeSide(side2, base, sketchshape, supportface, invObjLoc,
                                            dir1.Reversed(), L2, TaperAngleRev.getValue(),
                                            TaperInnerAngleRev.getValue(), makeface);
                if (!prism2.isNull())
                    sides.push_back(prism2);
            }
            if (sides.empty())
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                            "No extrusion geometry was generated"));
            prism = xorSides(sides, getDocument()->getStringHasher());
        } else {
            // Through all tapers over the through-all length, as the untapered
            // prism in generatePrism() runs. It took Length, so a tapered
            // pocket switched to ThroughAll cut only Length deep (upstream
            // d52260b2f4 lets the taper be set there, too).
            if (method == "ThroughAll")
                L = getThroughAllLength();
            if (method2 == "ThroughAll")
                L2 = getThroughAllLength();

            Part::ExtrusionHelper::Parameters params;
            params.dir = dir;
            params.solid = makeface;
            params.taperAngleFwd = this->TaperAngle.getValue() * M_PI / 180.0;
            params.taperAngleRev = this->TaperAngleRev.getValue() * M_PI / 180.0;
            params.innerTaperAngleFwd = this->TaperInnerAngle.getValue() * M_PI / 180.0;
            params.innerTaperAngleRev = this->TaperInnerAngleRev.getValue() * M_PI / 180.0;
            params.linearize = this->Linearize.getValue();
            if (symmetric) {
                params.lengthFwd = L/2;
                params.lengthRev = L/2;
                if (params.taperAngleRev == 0.0)
                    params.taperAngleRev = params.taperAngleFwd;
            } else {
                params.lengthFwd = L;
                params.lengthRev = L2;
            }
            if (std::fabs(params.taperAngleFwd) >= Precision::Angular()
                    || std::fabs(params.taperAngleRev) >= Precision::Angular()
                    || std::fabs(params.innerTaperAngleFwd) >= Precision::Angular()
                    || std::fabs(params.innerTaperAngleRev) >= Precision::Angular()) {
                if (fabs(params.taperAngleFwd) > M_PI * 0.5 - Precision::Angular()
                        || fabs(params.taperAngleRev) > M_PI * 0.5 - Precision::Angular())
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                            "Magnitude of taper angle matches or exceeds 90 degrees"));
                if (fabs(params.innerTaperAngleFwd) > M_PI * 0.5 - Precision::Angular()
                        || fabs(params.innerTaperAngleRev) > M_PI * 0.5 - Precision::Angular())
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                            "Magnitude of inner taper angle matches or exceeds 90 degrees"));
                if (Reversed.getValue())
                    params.dir.Reverse();
                std::vector<TopoShape> drafts;
                params.usepipe = this->UsePipeForDraft.getValue();
                Part::ExtrusionHelper::makeDraft(params, sketchshape, drafts, getDocument()->getStringHasher());
                if (drafts.empty())
                    return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Padding with draft angle failed"));
                prism.makECompound(drafts, nullptr, false);

            } else
                generatePrism(prism, sketchshape, dir, L, L2, twoSides,
                              symmetric, Reversed.getValue());
        }

        // set the additive shape property for later usage in e.g. pattern
        prism = refineShapeIfActive(prism);
        this->AddSubShape.setValue(prism);
        if (isRecomputePaused())
            return App::DocumentObject::StdReturn;

        this->Shape.setValue(makeBoolean(base, prism));

        // eventually disable some settings that are not valid for the current method
        updateProperties();

        return App::DocumentObject::StdReturn;
    }
    catch (Standard_Failure& e) {
        if (std::string(e.GetMessageString()) == "TopoDS::Face")
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                "Could not create face from sketch.\n"
                "Intersecting sketch entities or multiple faces in a sketch are not allowed."));
        else
            return new App::DocumentObjectExecReturn(e.GetMessageString());
    }
    catch (Base::Exception& e) {
        return new App::DocumentObjectExecReturn(e.what());
    }

}

int FeatureExtrude::getUpToShape(TopoShape &upToShape,
                                 const UpToSide &side,
                                 const TopoShape &base,
                                 const TopoShape &sketchshape,
                                 const TopLoc_Location &invObjLoc,
                                 gp_Dir &dir)
{
    int faceCount = 1;
    // UpToFace holds a single face, as it always did; anything else is in
    // UpToShape, which the panel edits for both types
    if (side.method == "UpToFace" && side.upToFace.getValue()) {
        getUpToFaceFromLinkSub(upToShape, side.upToFace);
        upToShape.move(invObjLoc);
    }
    else if (side.method == "UpToFace" || side.method == "UpToShape") {
        if (side.upToShape.getSubListValues().empty()) {
            if (side.method == "UpToFace")
                THROWM(Base::ValueError, "SketchBased: No face selected")
            if (base.isNull())
                THROWM(Base::ValueError,
                       "Extrude: Up to shape: no shape selected, and no base to extrude up to")
            upToShape = base;
            faceCount = 0;
        }
        else {
            faceCount = getUpToShapeFromLinkSubList(upToShape, side.upToShape);
            upToShape.move(invObjLoc);
        }
    }
    if (faceCount == 1) {
        getUpToFace(upToShape, base, sketchshape, side.method, dir);
        addOffsetToFace(upToShape, dir, side.offset);
        return faceCount;
    }
    if (std::fabs(side.offset) > Precision::Confusion())
        THROWM(Base::ValueError, "Extrude: Can only offset one face")
    // Several faces, a whole shape: without the face furthest along the
    // extrusion the shell is open, and the prism stops at the nearest
    // (upstream 8b9f5bdc4f). Given all of them it ran on.
    std::vector<Part::cutFaces> cfaces = Part::findAllFacesCutBy(upToShape, sketchshape, dir);
    if (cfaces.empty()) {
        // The shape is behind: extrude towards it, as a single face does in
        // getUpToFace() (upstream 17ac7dab3d). Only looking the other way
        // left the prism running away from it.
        dir.Reverse();
        cfaces = Part::findAllFacesCutBy(upToShape, sketchshape, dir);
    }
    if (cfaces.size() > 1) {
        auto farFace = &cfaces.front();
        for (auto &cface : cfaces) {
            if (cface.distsq > farFace->distsq)
                farFace = &cface;
        }
        std::vector<TopoShape> faces;
        for (auto &face : upToShape.getSubTopoShapes(TopAbs_FACE)) {
            if (!face.getShape().IsSame(farFace->face.getShape()))
                faces.push_back(face);
        }
        // One face left is given as the face: a compound of one face does not
        // stop the prism, which ran through all
        upToShape = faces.size() == 1 ? faces.front() : TopoShape().makECompound(faces);
    }
    return faceCount;
}

TopoShape FeatureExtrude::makeSide(const UpToSide &side,
                                   const TopoShape &base,
                                   const TopoShape &sketchshape,
                                   const TopoShape &supportface,
                                   const TopLoc_Location &invObjLoc,
                                   gp_Dir dir,
                                   double length,
                                   double taperAngle,
                                   double innerTaperAngle,
                                   bool makeface)
{
    TopoShape prism(0,getDocument()->getStringHasher());
    if (isUpToMethod(side.method)) {
        TopoShape upToShape;
        int faceCount = getUpToShape(upToShape, side, base, sketchshape, invObjLoc, dir);
        try {
            prism.makEPrismUntil(base, sketchshape, supportface, upToShape,
                    dir, TopoShape::PrismMode::None, CheckUpToFaceLimits.getValue());
        }
        catch (const Base::Exception &) {
            if (faceCount > 1)
                THROWM(Base::RuntimeError, "Unable to reach the selected shape, please select faces")
            throw;
        }
        return prism;
    }

    if (side.method == "ThroughAll")
        length = getThroughAllLength();
    // A side of no length adds nothing
    if (std::fabs(length) < Precision::Confusion())
        return TopoShape();

    Part::ExtrusionHelper::Parameters params;
    params.dir = dir;
    params.solid = makeface;
    params.lengthFwd = length;
    params.lengthRev = 0.0;
    params.taperAngleFwd = taperAngle * M_PI / 180.0;
    params.taperAngleRev = 0.0;
    params.innerTaperAngleFwd = innerTaperAngle * M_PI / 180.0;
    params.innerTaperAngleRev = 0.0;
    params.linearize = this->Linearize.getValue();
    if (std::fabs(params.taperAngleFwd) < Precision::Angular()
            && std::fabs(params.innerTaperAngleFwd) < Precision::Angular()) {
        try {
            prism.makEPrism(sketchshape, length * gp_Vec(dir));
        } catch (Standard_Failure &) {
            THROWM(Base::RuntimeError, "FeatureExtrusion: Length: Could not extrude the sketch!")
        }
        return prism;
    }
    if (fabs(params.taperAngleFwd) > M_PI * 0.5 - Precision::Angular())
        THROWM(Base::ValueError, "Magnitude of taper angle matches or exceeds 90 degrees")
    if (fabs(params.innerTaperAngleFwd) > M_PI * 0.5 - Precision::Angular())
        THROWM(Base::ValueError, "Magnitude of inner taper angle matches or exceeds 90 degrees")
    std::vector<TopoShape> drafts;
    params.usepipe = this->UsePipeForDraft.getValue();
    Part::ExtrusionHelper::makeDraft(params, sketchshape, drafts, getDocument()->getStringHasher());
    if (drafts.empty())
        THROWM(Base::RuntimeError, "Padding with draft angle failed")
    prism.makECompound(drafts, nullptr, false);
    return prism;
}

bool FeatureExtrude::isSingleUpToFace(const App::PropertyLinkSubList &shape)
{
    const auto &objs = shape.getValues();
    if (objs.size() != 1 || !objs.front())
        return false;
    const auto &subs = shape.getSubValues();
    if (!isWholeObject(subs))
        return true;
    return isSingleFaceObject(objs.front());
}

void FeatureExtrude::syncUpToShape(const App::PropertyLinkSub &face, App::PropertyLinkSubList &shape)
{
    auto obj = face.getValue();
    if (!obj) {
        if (shape.getSize())
            shape.setValues(std::vector<App::DocumentObject*>(), std::vector<std::string>());
        return;
    }
    std::vector<std::string> subs = face.getSubValues();
    // UpToFace on a whole object of several faces has always meant its first
    // face (getUpToFace() takes it), where UpToShape means them all
    if (isWholeObject(subs) && !isSingleFaceObject(obj))
        subs = {"Face1"};
    shape.setValue(obj, subs);
}

void FeatureExtrude::syncUpToFace(const App::PropertyLinkSubList &shape,
                                  App::PropertyLinkSub &face,
                                  App::PropertyEnumeration &type)
{
    if (isSingleUpToFace(shape)) {
        face.setValue(shape.getValues().front(), shape.getSubValues());
        if (strcmp(type.getValueAsString(), "UpToShape") == 0)
            type.setValue("UpToFace");
        return;
    }
    if (face.getValue())
        face.setValue(nullptr);
    if (shape.getSize() && strcmp(type.getValueAsString(), "UpToFace") == 0)
        type.setValue("UpToShape");
}

void FeatureExtrude::onDocumentRestored()
{
    {
        Base::StateLocker guard(syncingSides);
        if (strcmp(Type.getValueAsString(), "TwoLengths") == 0) {
            Type.setValue("Length");
            Type2.setValue("Length");
            SideType.setValue("Two sides");
        }
        else if (Midplane.getValue() && strcmp(SideType.getValueAsString(), "One side") == 0) {
            // Midplane was read only for the lengths: an up-to feature built
            // one side, whatever it said
            std::string method(Type.getValueAsString());
            if (method == "Length" || method == "ThroughAll")
                SideType.setValue("Symmetric");
        }
        if (strcmp(Type2.getValueAsString(), "TwoLengths") == 0)
            Type2.setValue("Length");
        // Midplane is Symmetric's alias; upstream saves it false
        bool symmetric = strcmp(SideType.getValueAsString(), "Symmetric") == 0;
        if (Midplane.getValue() != symmetric)
            Midplane.setValue(symmetric);
        // A file from before the panel edited UpToShape has its face in UpToFace
        if (UpToFace.getValue() && !UpToShape.getSize())
            syncUpToShape(UpToFace, UpToShape);
        if (UpToFace2.getValue() && !UpToShape2.getSize())
            syncUpToShape(UpToFace2, UpToShape2);
    }
    ProfileBased::onDocumentRestored();
}

void FeatureExtrude::handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *Name)
{
    if (strcmp(TypeName, App::PropertyAngle::getClassTypeId().getName()) == 0) {
        // Deliberately change 'InnerTaperAngle' to TaperAngleInner to identify
        // document from Link branch
        if (strcmp(Name, "InnerTaperAngle") == 0) {
            AutoTaperInnerAngle.setValue(false);
            TaperInnerAngle.Restore(reader);
            return;
        } else if (strcmp(Name, "InnerTaperAngleRev") == 0) {
            AutoTaperInnerAngle.setValue(false);
            TaperInnerAngleRev.Restore(reader);
            return;
        }
    }
    ProfileBased::handleChangedPropertyName(reader, TypeName, Name);
}

void FeatureExtrude::onChanged(const App::Property *prop)
{
    // Keep the aliases in step: Midplane with Symmetric, TwoLengths as two
    // sides, UpToFace with UpToShape. Undo and redo bring back values that
    // were in step already.
    if (!isRestoring() && !syncingSides
            && !(getDocument() && getDocument()->isPerformingTransaction())) {
        Base::StateLocker guard(syncingSides);
        if (prop == &Midplane) {
            if (Midplane.getValue())
                SideType.setValue("Symmetric");
            else if (strcmp(SideType.getValueAsString(), "Symmetric") == 0)
                SideType.setValue("One side");
        }
        else if (prop == &SideType) {
            bool symmetric = strcmp(SideType.getValueAsString(), "Symmetric") == 0;
            if (Midplane.getValue() != symmetric)
                Midplane.setValue(symmetric);
        }
        else if (prop == &Type) {
            if (strcmp(Type.getValueAsString(), "TwoLengths") == 0) {
                Type.setValue("Length");
                Type2.setValue("Length");
                SideType.setValue("Two sides");
                Midplane.setValue(false);
            }
        }
        else if (prop == &Type2) {
            if (strcmp(Type2.getValueAsString(), "TwoLengths") == 0)
                Type2.setValue("Length");
        }
        else if (prop == &UpToFace)
            syncUpToShape(UpToFace, UpToShape);
        else if (prop == &UpToFace2)
            syncUpToShape(UpToFace2, UpToShape2);
        else if (prop == &UpToShape)
            syncUpToFace(UpToShape, UpToFace, Type);
        else if (prop == &UpToShape2)
            syncUpToFace(UpToShape2, UpToFace2, Type2);
    }

    if (prop == &TaperAngle
            || prop == &TaperAngleRev
            || prop == &AutoTaperInnerAngle)
    {
        if (AutoTaperInnerAngle.getValue()) {
            TaperAngleRev.setStatus(App::Property::ReadOnly, true);
            TaperAngle.setStatus(App::Property::ReadOnly, true);
            TaperInnerAngle.setValue(-TaperAngle.getValue());
            TaperInnerAngleRev.setValue(-TaperAngleRev.getValue());
        }
        else {
            TaperAngleRev.setStatus(App::Property::ReadOnly, false);
            TaperAngle.setStatus(App::Property::ReadOnly, false);
        }
    }

    if (prop == &TaperAngleRev || prop == &TaperAngle2) {
        if (!prop->testStatus(App::Property::User1)) {
            Base::ObjectStatusLocker<App::Property::Status, App::Property> guard(
                    App::Property::User1, const_cast<App::Property*>(prop));
            if (prop == &TaperAngleRev)
                TaperAngle2.setValue(TaperAngleRev.getValue());
            else
                TaperAngleRev.setValue(TaperAngle2.getValue());
        }
    }
    ProfileBased::onChanged(prop);
}

