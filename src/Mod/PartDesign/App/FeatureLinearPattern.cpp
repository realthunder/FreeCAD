/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <algorithm>
# include <BRepAdaptor_Curve.hxx>
# include <BRepAdaptor_Surface.hxx>
# include <gp_Dir.hxx>
# include <gp_Pln.hxx>
# include <gp_Vec.hxx>
# include <Precision.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Face.hxx>
#endif

#include <App/OriginFeature.h>
#include <Base/Axis.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/Part2DObject.h>

#include "FeatureLinearPattern.h"
#include "DatumLine.h"
#include "DatumPlane.h"


using namespace PartDesign;

namespace PartDesign {


PROPERTY_SOURCE(PartDesign::LinearPattern, PartDesign::Transformed)

const App::PropertyIntegerConstraint::Constraints LinearPattern::intOccurrences = { 1, INT_MAX, 1 };

// Upstream renamed "length" and "offset" with the second direction
// (5d2037c820); a file stores the index, which stays the same
const char* LinearPattern::ModeEnums[] = { "Extent", "Spacing", nullptr };

LinearPattern::LinearPattern()
{
    auto initialMode = LinearPatternMode::Extent;

    ADD_PROPERTY_TYPE(Direction, (nullptr), "Direction 1", App::Prop_None,
        "The first direction of the pattern: a straight edge, a datum line, a sketch axis,\n"
        "or the normal of a planar face");
    ADD_PROPERTY_TYPE(Reversed, (0), "Direction 1", App::Prop_None,
        "Reverse the first direction");
    ADD_PROPERTY_TYPE(Mode, (long(initialMode)), "Direction 1", App::Prop_None,
        "How the first direction is dimensioned.\n"
        "'Extent': Length from the first to the last occurrence.\n"
        "'Spacing': Offset between consecutive occurrences.");
    ADD_PROPERTY_TYPE(Length, (100.0), "Direction 1", App::Prop_None,
        "Distance from the first to the last occurrence, in 'Extent' mode");
    ADD_PROPERTY_TYPE(Offset, (10.0), "Direction 1", App::Prop_None,
        "Distance between consecutive occurrences, in 'Spacing' mode");
    ADD_PROPERTY_TYPE(Occurrences, (3), "Direction 1", App::Prop_None,
        "Number of occurrences in the first direction, the original included");
    ADD_PROPERTY_TYPE(Spacings, (std::vector<double>()), "Direction 1", App::Prop_None,
        "Individual gaps in 'Spacing' mode, one per gap: item i is the gap before\n"
        "occurrence i + 2. -1 uses SpacingPattern, or Offset.");
    ADD_PROPERTY_TYPE(SpacingPattern, (std::vector<double>()), "Direction 1", App::Prop_None,
        "Gaps repeated along the first direction in 'Spacing' mode, e.g. [10, 20]\n"
        "alternates 10 and 20. Used when it has more than one value.");
    Occurrences.setConstraints(&intOccurrences);
    Mode.setEnums(ModeEnums);

    ADD_PROPERTY_TYPE(Direction2, (nullptr), "Direction 2", App::Prop_None,
        "The second direction of the pattern, used when Occurrences2 is more than one");
    ADD_PROPERTY_TYPE(Reversed2, (0), "Direction 2", App::Prop_None,
        "Reverse the second direction");
    ADD_PROPERTY_TYPE(Mode2, (long(initialMode)), "Direction 2", App::Prop_None,
        "How the second direction is dimensioned.\n"
        "'Extent': Length2 from the first to the last occurrence.\n"
        "'Spacing': Offset2 between consecutive occurrences.");
    ADD_PROPERTY_TYPE(Length2, (100.0), "Direction 2", App::Prop_None,
        "Distance from the first to the last occurrence, in 'Extent' mode");
    ADD_PROPERTY_TYPE(Offset2, (10.0), "Direction 2", App::Prop_None,
        "Distance between consecutive occurrences, in 'Spacing' mode");
    ADD_PROPERTY_TYPE(Occurrences2, (1), "Direction 2", App::Prop_None,
        "Number of occurrences in the second direction, the original included.\n"
        "One leaves the second direction off.");
    ADD_PROPERTY_TYPE(Spacings2, (std::vector<double>()), "Direction 2", App::Prop_None,
        "Individual gaps of the second direction, as Spacings");
    ADD_PROPERTY_TYPE(SpacingPattern2, (std::vector<double>()), "Direction 2", App::Prop_None,
        "Gaps repeated along the second direction, as SpacingPattern");
    Occurrences2.setConstraints(&intOccurrences);
    Mode2.setEnums(ModeEnums);

    setReadWriteStatusForMode(LinearPatternDirection::First);
    setReadWriteStatusForMode(LinearPatternDirection::Second);
    resizeSpacings(LinearPatternDirection::First);
}

LinearPattern::DirectionProps LinearPattern::props(LinearPatternDirection dir)
{
    if (dir == LinearPatternDirection::First)
        return {Direction, Reversed, Mode, Length, Offset, Occurrences, Spacings, SpacingPattern};
    return {Direction2, Reversed2, Mode2, Length2, Offset2, Occurrences2, Spacings2, SpacingPattern2};
}

short LinearPattern::mustExecute() const
{
    for (auto dir : {LinearPatternDirection::First, LinearPatternDirection::Second}) {
        const auto p = props(dir);
        if (p.direction.isTouched() ||
            p.reversed.isTouched() ||
            p.mode.isTouched() ||
            // Length and Offset are mutually exclusive, only one could be updated at once
            p.length.isTouched() ||
            p.offset.isTouched() ||
            p.occurrences.isTouched() ||
            p.spacings.isTouched() ||
            p.spacingPattern.isTouched())
            return 1;
    }
    return Transformed::mustExecute();
}

void LinearPattern::setReadWriteStatusForMode(LinearPatternDirection dir)
{
    auto p = props(dir);
    bool extent = p.mode.getValue() == static_cast<long>(LinearPatternMode::Extent);
    p.length.setReadOnly(!extent);
    p.offset.setReadOnly(extent);
}

gp_Dir LinearPattern::getDirection(const App::PropertyLinkSub& prop) const
{
    App::DocumentObject* refObject = prop.getValue();
    if (!refObject)
        THROWM(Base::ValueError, "No direction reference specified")

    std::vector<std::string> subStrings = prop.getSubValues();
    if (subStrings.empty())
        THROWM(Base::ValueError, "No direction reference specified")

    gp_Dir dir;
    if (refObject->isDerivedFrom<Part::Part2DObject>()) {
        Part::Part2DObject* refSketch = static_cast<Part::Part2DObject*>(refObject);
        Base::Axis axis;
        if (subStrings[0] == "H_Axis") {
            axis = refSketch->getAxis(Part::Part2DObject::H_Axis);
            axis *= refSketch->Placement.getValue();
        }
        else if (subStrings[0] == "V_Axis") {
            axis = refSketch->getAxis(Part::Part2DObject::V_Axis);
            axis *= refSketch->Placement.getValue();
        }
        else if (subStrings[0] == "N_Axis" || subStrings[0].empty()) {
            // the whole sketch, as a planar face, gives its normal; it fell
            // through to an axis never set
            axis = refSketch->getAxis(Part::Part2DObject::N_Axis);
            axis *= refSketch->Placement.getValue();
        }
        else if (subStrings[0].compare(0, 4, "Axis") == 0) {
            int AxId = std::atoi(subStrings[0].substr(4,4000).c_str());
            if (AxId >= 0 && AxId < refSketch->getAxisCount()) {
                axis = refSketch->getAxis(AxId);
                axis *= refSketch->Placement.getValue();
            }
        }
        else if (subStrings[0].compare(0, 4, "Edge") == 0) {
            Part::TopoShape refShape = refSketch->Shape.getShape();
            TopoDS_Shape ref = refShape.getSubShape(subStrings[0].c_str());
            TopoDS_Edge refEdge = TopoDS::Edge(ref);
            if (refEdge.IsNull())
                THROWM(Base::ValueError, "Failed to extract direction edge")
            BRepAdaptor_Curve adapt(refEdge);
            if (adapt.GetType() != GeomAbs_Line)
                THROWM(Base::TypeError, "Direction edge must be a straight line")

            gp_Pnt p = adapt.Line().Location();
            gp_Dir d = adapt.Line().Direction();

            // the axis is not given in local coordinates and mustn't be multiplied with the
            // placement
            axis.setBase(Base::Vector3d(p.X(), p.Y(), p.Z()));
            axis.setDirection(Base::Vector3d(d.X(), d.Y(), d.Z()));
        }
        dir = gp_Dir(axis.getDirection().x, axis.getDirection().y, axis.getDirection().z);
    } else if (refObject->isDerivedFrom<PartDesign::Plane>()) {
        PartDesign::Plane* plane = static_cast<PartDesign::Plane*>(refObject);
        Base::Vector3d d = plane->getNormal();
        dir = gp_Dir(d.x, d.y, d.z);
    } else if (refObject->isDerivedFrom<PartDesign::Line>()) {
        PartDesign::Line* line = static_cast<PartDesign::Line*>(refObject);
        Base::Vector3d d = line->getDirection();
        dir = gp_Dir(d.x, d.y, d.z);
    } else if (refObject->isDerivedFrom<App::Plane>()) {
        // An origin or LCS plane gives its normal (upstream cf0412b7e2);
        // the panel let one be picked, and the pattern refused it
        App::Plane* plane = static_cast<App::Plane*>(refObject);
        Base::Vector3d d = plane->getDirection();
        dir = gp_Dir(d.x, d.y, d.z);
    } else if (refObject->isDerivedFrom<App::Line>()) {
        // With the rotation of the coordinate system holding the line
        App::Line* line = static_cast<App::Line*>(refObject);
        Base::Vector3d d = line->getDirection();
        dir = gp_Dir(d.x, d.y, d.z);
    } else if (refObject->isDerivedFrom<Part::Feature>()) {
        if (subStrings[0].empty())
            THROWM(Base::ValueError, "No direction reference specified")
        Part::Feature* refFeature = static_cast<Part::Feature*>(refObject);
        Part::TopoShape refShape = refFeature->Shape.getShape();
        TopoDS_Shape ref = refShape.getSubShape(subStrings[0].c_str());

        if (ref.ShapeType() == TopAbs_FACE) {
            TopoDS_Face refFace = TopoDS::Face(ref);
            if (refFace.IsNull())
                THROWM(Base::ValueError, "Failed to extract direction plane")
            BRepAdaptor_Surface adapt(refFace);
            if (adapt.GetType() != GeomAbs_Plane)
                THROWM(Base::TypeError, "Direction face must be planar")

            dir = adapt.Plane().Axis().Direction();
        } else if (ref.ShapeType() == TopAbs_EDGE) {
            TopoDS_Edge refEdge = TopoDS::Edge(ref);
            if (refEdge.IsNull())
                THROWM(Base::ValueError, "Failed to extract direction edge")
            BRepAdaptor_Curve adapt(refEdge);
            if (adapt.GetType() != GeomAbs_Line)
                THROWM(Base::ValueError, "Direction edge must be a straight line")

            dir = adapt.Line().Direction();
        } else {
            THROWM(Base::ValueError, "Direction reference must be edge or face")
        }
    } else {
        THROWM(Base::ValueError, "Direction reference must be edge/face of a feature or a datum line/plane")
    }
    TopLoc_Location invObjLoc = this->getLocation().Inverted();
    dir.Transform(invObjLoc.Transformation());
    return dir;
}

double LinearPattern::getSpacing(LinearPatternDirection dir, int index) const
{
    // Individual spacing > spacing pattern > Offset (upstream 5d2037c820).
    // A list of another size than the gaps, as a file or a script may leave
    // it, reads -1 where it is short.
    const auto p = props(dir);
    const auto& spacings = p.spacings.getValues();
    if (index >= 0 && index < static_cast<int>(spacings.size()) && spacings[index] != -1.0)
        return spacings[index];
    const auto& pattern = p.spacingPattern.getValues();
    if (pattern.size() > 1)
        return pattern[index % pattern.size()];
    return p.offset.getValue();
}

std::vector<gp_Vec> LinearPattern::getSteps(LinearPatternDirection dir) const
{
    const auto p = props(dir);
    int occurrences = p.occurrences.getValue();
    std::vector<gp_Vec> steps{gp_Vec()};
    if (occurrences <= 1)
        return steps;
    steps.reserve(occurrences);

    bool extent = p.mode.getValue() == static_cast<long>(LinearPatternMode::Extent);
    if (extent && p.length.getValue() < Precision::Confusion())
        THROWM(Base::ValueError, "Pattern length too small")

    gp_Vec unit(getDirection(p.direction));
    if (p.reversed.getValue())
        unit.Reverse();

    double distance = 0.0;
    for (int i = 1; i < occurrences; ++i) {
        if (extent)
            distance = p.length.getValue() * i / (occurrences - 1);
        else
            distance += getSpacing(dir, i - 1);
        steps.push_back(unit * distance);
    }
    return steps;
}

std::list<gp_Trsf> LinearPattern::getTransformations(const std::vector<Part::TopoShape> &)
{
    if (Occurrences.getValue() < 1 || Occurrences2.getValue() < 1)
        THROWM(Base::ValueError, "At least one occurrence required")

    std::vector<gp_Vec> steps1 = getSteps(LinearPatternDirection::First);
    std::vector<gp_Vec> steps2 = getSteps(LinearPatternDirection::Second);

    // Note: The original feature is already included in the list of
    // transformations, the first one. Row by row along the first direction.
    std::list<gp_Trsf> transformations;
    for (const auto& step1 : steps1) {
        for (const auto& step2 : steps2) {
            gp_Trsf trans;
            trans.SetTranslation(step1 + step2);
            transformations.push_back(trans);
        }
    }
    return transformations;
}

void LinearPattern::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName, App::Property* prop)
// transforms properties that had been changed
{
    // property Occurrences had the App::PropertyInteger and was changed to App::PropertyIntegerConstraint
    if (prop == &Occurrences && strcmp(TypeName, "App::PropertyInteger") == 0) {
        App::PropertyInteger OccurrencesProperty;
        // restore the PropertyInteger to be able to set its value
        OccurrencesProperty.Restore(reader);
        Occurrences.setValue(OccurrencesProperty.getValue());
    }
    else {
        Transformed::handleChangedPropertyType(reader, TypeName, prop);
    }
}

void LinearPattern::syncLengthAndOffset(LinearPatternDirection dir)
{
    // Keep Length in sync with Offset, and with the number of gaps between
    // them, which a change of Occurrences changes too (upstream fa0702956c).
    // One occurrence has no gap: count it as one, as upstream's
    // syncLengthAndOffset() does, instead of dividing by zero.
    auto p = props(dir);
    long gaps = p.occurrences.getValue() > 1 ? p.occurrences.getValue() - 1 : 1;
    if (p.mode.getValue() == static_cast<long>(LinearPatternMode::Spacing)) {
        if (!p.length.testStatus(App::Property::Status::Immutable))
            p.length.setValue(p.offset.getValue() * gaps);
    }
    else if (!p.offset.testStatus(App::Property::Status::Immutable)) {
        p.offset.setValue(p.length.getValue() / gaps);
    }
}

void LinearPattern::resizeSpacings(LinearPatternDirection dir)
{
    // One item per gap, so that the property editor shows them all. Not on
    // recompute, as upstream does, which touches the feature there.
    //
    // Grown to MaxListedSpacings at most: the gaps after it read -1 anyway,
    // and Occurrences set to two billion, as a spin box driven from Python
    // did, made a list of 16 GB on the spot.
    auto p = props(dir);
    int gaps = std::max(0L, p.occurrences.getValue() - 1);
    int size = p.spacings.getSize();
    int target = size > gaps ? gaps : std::min(gaps, std::max(size, MaxListedSpacings));
    if (size == target)
        return;
    std::vector<double> spacings = p.spacings.getValues();
    spacings.resize(target, -1.0);
    p.spacings.setValues(spacings);
}

void LinearPattern::onChanged(const App::Property* prop)
{
    if (!isRestoring()) {
        for (auto dir : {LinearPatternDirection::First, LinearPatternDirection::Second}) {
            auto p = props(dir);
            bool spacing = p.mode.getValue() == static_cast<long>(LinearPatternMode::Spacing);
            if (prop == &p.mode) {
                setReadWriteStatusForMode(dir);
            }
            else if (prop == &p.occurrences) {
                resizeSpacings(dir);
                syncLengthAndOffset(dir);
            }
            else if ((prop == &p.offset && spacing) || (prop == &p.length && !spacing)) {
                syncLengthAndOffset(dir);
            }
        }
    }

    Transformed::onChanged(prop);
}

void LinearPattern::onDocumentRestored()
{
    // Mode is restored with the change handling off
    setReadWriteStatusForMode(LinearPatternDirection::First);
    setReadWriteStatusForMode(LinearPatternDirection::Second);
    Transformed::onDocumentRestored();
}

}
