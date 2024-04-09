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

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/MappedElement.h>
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

FeatureExtrude::FeatureExtrude() = default;

void FeatureExtrude::initProperties(const char *group)
{
    ADD_PROPERTY_TYPE(Length, (10.0), group, App::Prop_None, "Extrusion length");
    ADD_PROPERTY_TYPE(Length2, (10.0), group, App::Prop_None, "Extrusion length in 2nd direction");
    ADD_PROPERTY_TYPE(UseCustomVector, (false), group, App::Prop_None, "Use custom vector for pad direction");
    ADD_PROPERTY_TYPE(Direction, (Base::Vector3d(1.0, 1.0, 1.0)), group, App::Prop_None, "Extrusion direction vector");
    ADD_PROPERTY_TYPE(ReferenceAxis, (nullptr), group, App::Prop_None, "Reference axis of direction");
    ADD_PROPERTY_TYPE(AlongSketchNormal, (true), group, App::Prop_None, "Measure pad length along the sketch normal direction");
    ADD_PROPERTY_TYPE(UpToFace, (nullptr), group, App::Prop_None, "Face where pad will end");
    ADD_PROPERTY_TYPE(Offset, (0.0), group, App::Prop_None, "Offset from face in which pad will end");
    Offset.setConstraints(&signedLengthConstraint);
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
        Type.isTouched() ||
        Length.isTouched() ||
        Length2.isTouched() ||
        TaperAngle.isTouched() ||
        TaperAngle2.isTouched() ||
        UseCustomVector.isTouched() ||
        Direction.isTouched() ||
        ReferenceAxis.isTouched() ||
        AlongSketchNormal.isTouched() ||
        Offset.isTouched() ||
        UpToFace.isTouched())
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
                                   const std::string& method,
                                   const gp_Dir& dir,
                                   const double L,
                                   const double L2,
                                   const bool midplane,
                                   const bool reversed)
{
    auto sketchShape = sketchTopoShape.getShape();
    if (method == "Length" || method == "TwoLengths" || method == "ThroughAll") {
        double Ltotal = L;
        double Loffset = 0.;
        if (method == "ThroughAll")
            Ltotal = getThroughAllLength();


        if (method == "TwoLengths") {
            // midplane makes no sense here
            Ltotal += L2;
            if (reversed)
                Loffset = -L;
            else if (midplane)
                Loffset = -0.5 * (L2 + L);
            else
                Loffset = -L2;
        } else if (midplane)
            Loffset = -Ltotal/2;

        if (method == "TwoLengths" || midplane) {
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
            throw Base::RuntimeError("FeatureExtrusion: Length: Could not extrude the sketch!");
        }
    }
    else {
        std::stringstream str;
        str << "FeatureExtrusion: Internal error: Unknown method '"
            << method << "' for generatePrism()";
        throw Base::RuntimeError(str.str());
    }

}

void FeatureExtrude::updateProperties(const std::string &method)
{
    // disable settings that are not valid on the current method
    // disable everything unless we are sure we need it
    bool isLengthEnabled = false;
    bool isLength2Enabled = false;
    bool isOffsetEnabled = false;
    bool isMidplaneEnabled = false;
    bool isReversedEnabled = false;
    bool isUpToFaceEnabled = false;
    bool isTaperVisible = false;
    bool isTaper2Visible = false;
    if (method == "Length") {
        isLengthEnabled = true;
        isTaperVisible = true;
        isMidplaneEnabled = true;
        isReversedEnabled = !Midplane.getValue();
    }
    else if (method == "UpToLast") {
        isOffsetEnabled = true;
        isReversedEnabled = true;
    }
    else if (method == "ThroughAll") {
        isMidplaneEnabled = true;
        isReversedEnabled = !Midplane.getValue();
    }
    else if (method == "UpToFirst") {
        isOffsetEnabled = true;
        isReversedEnabled = true;
    }
    else if (method == "UpToFace") {
        isOffsetEnabled = true;
        isReversedEnabled = true;
        isUpToFaceEnabled = true;
    }
    else if (method == "TwoLengths") {
        isLengthEnabled = true;
        isLength2Enabled = true;
        isTaperVisible = true;
        isTaper2Visible = true;
        isReversedEnabled = true;
    }
    else if (method == "UpToShape") {
        isReversedEnabled = true;
    }

    Length.setReadOnly(!isLengthEnabled);
    AlongSketchNormal.setReadOnly(!isLengthEnabled);
    Length2.setReadOnly(!isLength2Enabled);
    Offset.setReadOnly(!isOffsetEnabled);
    TaperAngle.setReadOnly(!isTaperVisible);
    TaperAngle2.setReadOnly(!isTaper2Visible);
    TaperAngleRev.setReadOnly(!isTaper2Visible);
    Midplane.setReadOnly(!isMidplaneEnabled);
    Reversed.setReadOnly(!isReversedEnabled);
    UpToFace.setReadOnly(!isUpToFaceEnabled);
}

void FeatureExtrude::setupObject()
{
    ProfileBased::setupObject();
    UsePipeForDraft.setValue(Part::PartParams::getUsePipeForExtrusionDraft());
}

App::DocumentObjectExecReturn *FeatureExtrude::buildExtrusion(ExtrudeOptions options)
{
    bool makeface = options.testFlag(ExtrudeOption::MakeFace);
    bool fuse = options.testFlag(ExtrudeOption::MakeFuse);
    bool legacyPocket = options.testFlag(ExtrudeOption::LegacyPocket);
    bool inverseDirection = options.testFlag(ExtrudeOption::InverseDirection);

    std::string method(Type.getValueAsString());                

    // Validate parameters
    double L = Length.getValue();
    if ((method == "Length") && (L < Precision::Confusion()))
        return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Length too small"));
    double L2 = 0;
    if ((method == "TwoLengths")) {
        L2 = Length2.getValue();
        if (std::abs(L2) < Precision::Confusion())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception", "Second length too small"));
    }

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
    TopoShape base = getBaseShape(true);

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
        if (factor < Precision::Confusion())
            return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                        "Creation failed because direction is orthogonal to sketch's normal vector"));

        // perform the length correction if not along custom vector
        if (AlongSketchNormal.getValue()) {
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

        if (method == "UpToFirst" || method == "UpToLast" || method == "UpToFace") {
            // Note: This will return an unlimited planar face if support is a datum plane
            TopoShape supportface = getSupportFace();
            supportface.move(invObjLoc);

            if (Reversed.getValue())
                dir.Reverse();

            // Find a valid face or datum plane to extrude up to
            TopoShape upToFace;
            if (method == "UpToFace") {
                getUpToFaceFromLinkSub(upToFace, UpToFace);
                upToFace.move(invObjLoc);
            }
            getUpToFace(upToFace, base, supportface, sketchshape, method, dir);
            addOffsetToFace(upToFace, dir, Offset.getValue());

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
            prism.makEPrismUntil(base, sketchshape, supportface, upToFace,
                    dir, TopoShape::PrismMode::None, CheckUpToFaceLimits.getValue());
        } else {
            Part::ExtrusionHelper::Parameters params;
            params.dir = dir;
            params.solid = makeface;
            params.taperAngleFwd = this->TaperAngle.getValue() * M_PI / 180.0;
            params.taperAngleRev = this->TaperAngleRev.getValue() * M_PI / 180.0;
            params.innerTaperAngleFwd = this->TaperInnerAngle.getValue() * M_PI / 180.0;
            params.innerTaperAngleRev = this->TaperInnerAngleRev.getValue() * M_PI / 180.0;
            params.linearize = this->Linearize.getValue();
            if (L2 == 0.0 && Midplane.getValue()) {
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
                generatePrism(prism, sketchshape, method, dir, L, L2,
                            Midplane.getValue(), Reversed.getValue());
        }
        
        // set the additive shape property for later usage in e.g. pattern
        prism = refineShapeIfActive(prism);
        this->AddSubShape.setValue(prism);
        if (isRecomputePaused())
            return App::DocumentObject::StdReturn;

        if (!base.isNull() && fuse) {
            prism.Tag = -this->getID();

            // Let's call algorithm computing a fuse operation:
            TopoShape result(0,getDocument()->getStringHasher());
            try {
                const char *maker;
                switch (getAddSubType()) {
                case Subtractive:
                    maker = Part::OpCodes::Cut;
                    break;
                case Intersecting:
                    maker = Part::OpCodes::Common;
                    break;
                default:
                    maker = Part::OpCodes::Fuse;
                }
                result.makEBoolean(maker, {base,prism});
            }catch(Standard_Failure &){
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                            "Fusion with base feature failed"));
            }
            // we have to get the solids (fuse sometimes creates compounds)
            auto solRes = this->getSolid(result);
            // lets check if the result is a solid
            if (solRes.isNull())
                return new App::DocumentObjectExecReturn(QT_TRANSLATE_NOOP("Exception",
                            "Resulting shape is not a solid"));

            solRes = refineShapeIfActive(solRes);
            this->Shape.setValue(getSolid(solRes));
        } else if (prism.hasSubShape(TopAbs_SOLID)) {
            if (prism.countSubShapes(TopAbs_SOLID) > 1)
                prism.makEFuse(prism.getSubTopoShapes(TopAbs_SOLID));
            prism = refineShapeIfActive(prism);
            this->Shape.setValue(getSolid(prism));
        } else {
            prism = refineShapeIfActive(prism);
            this->Shape.setValue(prism);
        }

        // eventually disable some settings that are not valid for the current method
        updateProperties(method);

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

void FeatureExtrude::handleChangedPropertyName(Base::XMLReader &reader, const char * TypeName, const char *Name)
{
    if (strcmp(TypeName, App::PropertyAngle::getClassTypeId().getName()) == 0) {
        // Deliberately change 'InnerTaperAngle' to TaperAngleInner to identify
        // document from Link branch
        if (strcmp(Name, "InnerTaperAngle")) {
            AutoTaperInnerAngle.setValue(false);
            TaperInnerAngle.Restore(reader);
        } else if (strcmp(Name, "InnerTaperAngleRev")) {
            AutoTaperInnerAngle.setValue(false);
            TaperInnerAngleRev.Restore(reader);
        }
    }
    ProfileBased::handleChangedPropertyName(reader, TypeName, Name);
}

void FeatureExtrude::onChanged(const App::Property *prop)
{
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

