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
# include <TopoDS.hxx>
# include <TopoDS_Face.hxx>
# include <gp_Lin.hxx>
# include <gp_Circ.hxx>
# include <gp_Ax2.hxx>
# include <BRepAdaptor_Curve.hxx>
#endif

#include "DatumLine.h"
#include <Base/Axis.h>
#include <Base/Exception.h>
#include <Base/Tools.h>
#include <Mod/Part/App/TopoShape.h>
#include <Mod/Part/App/Part2DObject.h>
#include <App/OriginFeature.h>

#include "FeaturePolarPattern.h"

using namespace PartDesign;

namespace PartDesign {


PROPERTY_SOURCE(PartDesign::PolarPattern, PartDesign::Transformed)

const App::PropertyIntegerConstraint::Constraints PolarPattern::intOccurrences = { 1, INT_MAX, 1 };
// A negative angle turns the other way (upstream 5ae67ee2f9); it was
// clamped to nothing, and the occurrences fell on each other
const App::PropertyAngle::Constraints PolarPattern::floatAngle = { -360.0, 360.0, 1.0 };

// Upstream renamed "angle" and "offset" with the spacings (5d2037c820); a
// file stores the index, which stays the same
const char* PolarPattern::ModeEnums[] = {"Extent", "Spacing", nullptr};

PolarPattern::PolarPattern()
{
    auto initialMode = PolarPatternMode::Extent;

    ADD_PROPERTY_TYPE(Axis, (nullptr), "PolarPattern", (App::PropertyType)(App::Prop_None), "Direction");
    ADD_PROPERTY(Reversed, (0));
    ADD_PROPERTY(Mode, (long(initialMode)));
    Mode.setEnums(PolarPattern::ModeEnums);
    ADD_PROPERTY(Angle, (360.0));
    ADD_PROPERTY(Offset, (120.0));
    Angle.setConstraints(&floatAngle);
    Offset.setConstraints(&floatAngle);
    ADD_PROPERTY(Occurrences, (3));
    Occurrences.setConstraints(&intOccurrences);
    ADD_PROPERTY_TYPE(Spacings, (std::vector<double>()), "PolarPattern", App::Prop_None,
        "Individual angles in 'Spacing' mode, one per gap: item i is the angle before\n"
        "occurrence i + 2. -1 uses SpacingPattern, or Offset.");
    ADD_PROPERTY_TYPE(SpacingPattern, (std::vector<double>()), "PolarPattern", App::Prop_None,
        "Angles repeated around the axis in 'Spacing' mode, e.g. [10, 20] alternates\n"
        "10 and 20 degrees. Used when it has more than one value.");

    setReadWriteStatusForMode(initialMode);
    resizeSpacings();
}

short PolarPattern::mustExecute() const
{
    if (Axis.isTouched() ||
        Reversed.isTouched() ||
        Mode.isTouched() ||
        // Angle and Offset are mutually exclusive, only one could be updated at once
        Angle.isTouched() || 
        Offset.isTouched() || 
        Occurrences.isTouched() ||
        Spacings.isTouched() ||
        SpacingPattern.isTouched())
        return 1;
    return Transformed::mustExecute();
}

std::list<gp_Trsf> PolarPattern::getTransformations(const std::vector<Part::TopoShape> &)
{
    int occurrences = Occurrences.getValue();
    if (occurrences < 1)
        THROWM(Base::ValueError, "At least one occurrence required")

    if (occurrences == 1)
        return {gp_Trsf()};

    bool reversed = Reversed.getValue();

    App::DocumentObject* refObject = Axis.getValue();
    if (!refObject)
        THROWM(Base::ValueError, "No axis reference specified")
    std::vector<std::string> subStrings = Axis.getSubValues();
    if (subStrings.empty())
        THROWM(Base::ValueError, "No axis reference specified")

    gp_Pnt axbase;
    gp_Dir axdir;
    if (refObject->isDerivedFrom<Part::Part2DObject>()) {
        Part::Part2DObject* refSketch = static_cast<Part::Part2DObject*>(refObject);
        if (subStrings[0].compare(0, 4, "Edge") == 0) {
            // A sketch edge: the sketch's Shape is already placed. Without
            // this case the axis stayed a default Base::Axis, and the
            // pattern failed on its zero direction (upstream b0331ed979).
            TopoDS_Shape ref = refSketch->Shape.getShape().getSubShape(subStrings[0].c_str());
            if (ref.IsNull() || ref.ShapeType() != TopAbs_EDGE)
                THROWM(Base::ValueError, "Failed to extract axis edge")
            BRepAdaptor_Curve adapt(TopoDS::Edge(ref));
            if (adapt.GetType() == GeomAbs_Line) {
                axbase = adapt.Line().Location();
                axdir = adapt.Line().Direction();
            } else if (adapt.GetType() == GeomAbs_Circle) {
                axbase = adapt.Circle().Location();
                axdir = adapt.Circle().Axis().Direction();
            } else {
                THROWM(Base::TypeError, "Rotation edge must be a straight line, circle or arc of circle")
            }
        } else {
            Base::Axis axis;
            if (subStrings[0] == "H_Axis")
                axis = refSketch->getAxis(Part::Part2DObject::H_Axis);
            else if (subStrings[0] == "V_Axis")
                axis = refSketch->getAxis(Part::Part2DObject::V_Axis);
            else if (subStrings[0] == "N_Axis")
                axis = refSketch->getAxis(Part::Part2DObject::N_Axis);
            else if (subStrings[0].compare(0, 4, "Axis") == 0) {
                int AxId = std::atoi(subStrings[0].substr(4,4000).c_str());
                if (AxId >= 0 && AxId < refSketch->getAxisCount())
                    axis = refSketch->getAxis(AxId);
            }
            axis *= refSketch->Placement.getValue();
            axbase = gp_Pnt(axis.getBase().x, axis.getBase().y, axis.getBase().z);
            axdir = gp_Dir(axis.getDirection().x, axis.getDirection().y, axis.getDirection().z);
        }
    } else if (refObject->isDerivedFrom<PartDesign::Line>()) {
        PartDesign::Line* line = static_cast<PartDesign::Line*>(refObject);
        Base::Vector3d base = line->getBasePoint();
        axbase = gp_Pnt(base.x, base.y, base.z);
        Base::Vector3d dir = line->getDirection();
        axdir = gp_Dir(dir.x, dir.y, dir.z);
    } else if (refObject->isDerivedFrom<App::Line>()) {
        // Through the line's own base point, and with the rotation of the
        // coordinate system holding it: a line of a moved or turned LCS
        App::Line* line = static_cast<App::Line*>(refObject);
        Base::Vector3d base = line->getBasePoint();
        axbase = gp_Pnt(base.x, base.y, base.z);
        Base::Vector3d d = line->getDirection();
        axdir = gp_Dir(d.x, d.y, d.z);
    } else if (refObject->isDerivedFrom<Part::Feature>()) {
        if (subStrings[0].empty())
            THROWM(Base::ValueError, "No axis reference specified")
        Part::Feature* refFeature = static_cast<Part::Feature*>(refObject);
        Part::TopoShape refShape = refFeature->Shape.getShape();
        TopoDS_Shape ref = refShape.getSubShape(subStrings[0].c_str());

        if (ref.ShapeType() == TopAbs_EDGE) {
            TopoDS_Edge refEdge = TopoDS::Edge(ref);
            if (refEdge.IsNull())
                THROWM(Base::ValueError, "Failed to extract axis edge")
            BRepAdaptor_Curve adapt(refEdge);
            if (adapt.GetType() == GeomAbs_Line) {
                axbase = adapt.Line().Location();
                axdir = adapt.Line().Direction();
            } else if (adapt.GetType() == GeomAbs_Circle) {
                axbase = adapt.Circle().Location();
                axdir = adapt.Circle().Axis().Direction();
            } else {
                THROWM(Base::TypeError, "Rotation edge must be a straight line, circle or arc of circle")
            }
         } else {
            THROWM(Base::TypeError, "Axis reference must be an edge")
        }
    } else {
        THROWM(Base::TypeError, "Axis reference must be edge of a feature or datum line")
    }
    TopLoc_Location invObjLoc = this->getLocation().Inverted();
    axbase.Transform(invObjLoc.Transformation());
    axdir.Transform(invObjLoc.Transformation());

    gp_Ax2 axis(axbase, axdir);

    if (reversed)
        axis.SetDirection(axis.Direction().Reversed());

    bool extent = Mode.getValue() == static_cast<long>(PolarPatternMode::Extent);
    double offset = 0.0;
    if (extent) {
        double angle = Angle.getValue();

        if (std::fabs(std::fabs(angle) - 360.0) < Precision::Confusion())
            angle /= occurrences; // Because e.g. two occurrences in 360 degrees need to be 180 degrees apart
        else
            angle /= occurrences - 1;

        offset = Base::toRadians<double>(angle);

        if (std::fabs(offset) < Precision::Angular())
            THROWM(Base::ValueError, "Pattern angle too small")
    }

    std::list<gp_Trsf> transformations;
    gp_Trsf trans;
    transformations.push_back(trans);

    // Note: The original feature is already included in the list of transformations!
    // Therefore we start with occurrence number 1
    double cumulative = 0.0;
    for (int i = 1; i < occurrences; i++) {
        if (extent)
            cumulative = i * offset;
        else
            cumulative += Base::toRadians<double>(getSpacing(i - 1));
        trans.SetRotation(axis.Axis(), cumulative);
        transformations.push_back(trans);
    }

    return transformations;
}

double PolarPattern::getSpacing(int index) const
{
    // Individual spacing > spacing pattern > Offset (upstream 5d2037c820,
    // indexed as 0f07a936d9 fixed it). A list of another size than the gaps
    // reads -1 where it is short.
    const auto& spacings = Spacings.getValues();
    if (index >= 0 && index < static_cast<int>(spacings.size()) && spacings[index] != -1.0)
        return spacings[index];
    const auto& pattern = SpacingPattern.getValues();
    if (pattern.size() > 1)
        return pattern[index % pattern.size()];
    return Offset.getValue();
}

void PolarPattern::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName, App::Property* prop)
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


void PolarPattern::onChanged(const App::Property* prop)
{
    if (prop == &Mode) {
        auto mode = static_cast<PolarPatternMode>(Mode.getValue());
        setReadWriteStatusForMode(mode);
    }
    else if (prop == &Occurrences && !isRestoring()) {
        resizeSpacings();
    }

    Transformed::onChanged(prop);
}

void PolarPattern::onDocumentRestored()
{
    setReadWriteStatusForMode(static_cast<PolarPatternMode>(Mode.getValue()));
    Transformed::onDocumentRestored();
}

void PolarPattern::setReadWriteStatusForMode(PolarPatternMode mode)
{
    Offset.setReadOnly(mode != PolarPatternMode::Spacing);
    Angle.setReadOnly(mode != PolarPatternMode::Extent);
}

void PolarPattern::resizeSpacings()
{
    // One item per gap, so that the property editor shows them all, up to
    // MaxListedSpacings as LinearPattern::resizeSpacings() explains. Not on
    // recompute, as upstream does, which touches the feature there.
    int gaps = std::max(0L, Occurrences.getValue() - 1);
    int size = Spacings.getSize();
    int target = size > gaps ? gaps : std::min(gaps, std::max(size, MaxListedSpacings));
    if (size == target)
        return;
    std::vector<double> spacings = Spacings.getValues();
    spacings.resize(target, -1.0);
    Spacings.setValues(spacings);
}

}
