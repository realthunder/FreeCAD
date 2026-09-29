// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <cstring>
#endif

#include <App/Document.h>
#include <App/Origin.h>
#include <App/PropertyLinks.h>
#include <Mod/Part/App/Part2DObject.h>
#include <Mod/Part/App/TopoShape.h>

#include "Body.h"
#include "FeaturePattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::PatternFeature, PartDesign::Transformed)

const char* PatternFeature::KindLabels[] = {"LinearPattern",
                                            "PolarPattern",
                                            "CircularPattern",
                                            "PathPattern",
                                            "PointPattern",
                                            nullptr};

const char* PatternFeature::KindTypes[] = {"PartDesign::LinearPattern",
                                           "PartDesign::PolarPattern",
                                           "PartDesign::CircularPattern",
                                           "PartDesign::PathPattern",
                                           "PartDesign::PointPattern",
                                           nullptr};

PatternFeature::PatternFeature()
    : PatternFeature(App::Pattern::Type::Linear)
{}

PatternFeature::PatternFeature(App::Pattern::Type type)
    : App::PatternExtension(type)
{
    setPatternNames(KindLabels, KindTypes);
    App::PatternExtension::initExtension(this);
}

Base::Type PatternFeature::getSaveType() const
{
    return App::PatternExtension::getSaveType(getTypeId());
}

std::vector<Base::Placement> PatternFeature::calculatePlacements(bool relativeToFirst) const
{
    App::Pattern::Context context;
    context.placement = Placement.getValue();
    context.relativeToFirst = relativeToFirst;
    return App::Pattern::getPlacements(getPatternType(), *this, context);
}

std::list<gp_Trsf> PatternFeature::getTransformations(const std::vector<Part::TopoShape>&)
{
    std::list<gp_Trsf> res;
    switch (getPatternType()) {
        case App::Pattern::Type::Path: {
            // The path is resolved in the body's frame and the originals are
            // placed in the feature's, so each step is conjugated into the
            // latter. Upstream applies the steps as they are, which is the
            // same while the feature sits at the body's origin, as a box does
            // and a pad on a sketch that is not in the XY plane does not.
            const Base::Placement placement = Placement.getValue();
            const Base::Placement inverse = placement.inverse();
            for (const auto& step : calculatePlacements(true)) {
                gp_Trsf trsf;
                Part::TopoShape::convertTogpTrsf((inverse * step * placement).toMatrix(), trsf);
                res.push_back(trsf);
            }
            return res;
        }
        case App::Pattern::Type::Point: {
            const auto points = calculatePlacements(false);
            if (points.empty()) {
                return res;
            }
            // The points are in the body's frame, the originals in the
            // feature's. Transforming features, an original moves from its
            // own origin to each point, and the base stays where it is:
            // Transformed rewrites the history so that the original is not
            // left in place as well. Whole shapes move from the base
            // feature's origin, as upstream's do, and positionBySupport() has
            // already put the feature, and with it the support, on the first
            // point. Without a reference, as inside a MultiTransform, the
            // copies are relative to the first point.
            Base::Vector3d origin = points.front().getPosition();
            if (SubTransform.getValue()) {
                for (auto obj : OriginalSubs.getValues()) {
                    if (auto feature = Base::freecad_dynamic_cast<Part::Feature>(obj)) {
                        origin = feature->Placement.getValue().getPosition();
                        break;
                    }
                }
            }
            else if (auto base = getBaseObject(/*silent=*/true)) {
                origin = base->Placement.getValue().getPosition();
            }
            const Base::Rotation inverse = Placement.getValue().getRotation().inverse();
            for (const auto& point : points) {
                Base::Vector3d offset;
                inverse.multVec(point.getPosition() - origin, offset);
                gp_Trsf trsf;
                trsf.SetTranslation(gp_Vec(offset.x, offset.y, offset.z));
                res.push_back(trsf);
            }
            return res;
        }
        default:
            for (const auto& placement : calculatePlacements(true)) {
                gp_Trsf trsf;
                Part::TopoShape::convertTogpTrsf(placement.toMatrix(), trsf);
                res.push_back(trsf);
            }
            return res;
    }
}

bool PatternFeature::isFirstInstanceTransformed() const
{
    if (getPatternType() != App::Pattern::Type::Point || !SubTransform.getValue()) {
        return false;
    }
    auto points =
        dynamic_cast<App::PropertyLinkSub*>(App::Pattern::getProperty(*this, "PointObject"));
    return points && points->getValue();
}

void PatternFeature::positionBySupport()
{
    Transformed::positionBySupport();
    if (getPatternType() != App::Pattern::Type::Point || SubTransform.getValue()) {
        return;
    }
    std::vector<Base::Placement> points;
    try {
        points = calculatePlacements(false);
    }
    catch (const Base::Exception&) {
        // getTransformations() reports it
        return;
    }
    if (points.empty()) {
        return;
    }
    Base::Placement placement = Placement.getValue();
    placement.setPosition(points.front().getPosition());
    Placement.setValue(placement);
}

void PatternFeature::onChanged(const App::Property* prop)
{
    Transformed::onChanged(prop);
    if (prop == &PatternType && !isRestoring() && getDocument()
        && !getDocument()->isPerformingTransaction()) {
        setDefaultReferences();
    }
}

void PatternFeature::setDefaultReferences()
{
    auto sketch = Base::freecad_dynamic_cast<Part::Part2DObject>(getSketchObject());
    auto body = Body::findBodyOf(this);
    auto origin = body ? body->getOrigin() : nullptr;
    auto setDefault = [this](const char* name, App::DocumentObject* obj, const char* sub) {
        auto link = dynamic_cast<App::PropertyLinkSub*>(App::Pattern::getProperty(*this, name));
        if (link && !link->getValue() && obj) {
            link->setValue(obj, {std::string(sub)});
        }
    };
    switch (getPatternType()) {
        case App::Pattern::Type::Linear:
            if (sketch) {
                setDefault("Direction", sketch, "H_Axis");
                setDefault("Direction2", sketch, "V_Axis");
            }
            else if (origin) {
                setDefault("Direction", origin->getX(), "");
                setDefault("Direction2", origin->getY(), "");
            }
            break;
        case App::Pattern::Type::Polar:
        case App::Pattern::Type::Circular:
            if (sketch) {
                setDefault("Axis", sketch, "N_Axis");
            }
            else if (origin) {
                setDefault("Axis", origin->getZ(), "");
            }
            break;
        default:
            // A path or the points are picked
            break;
    }
}

void PatternFeature::onUndoRedoFinished()
{
    onPatternUndoRedoFinished();
    Transformed::onUndoRedoFinished();
}

void PatternFeature::handleChangedPropertyType(Base::XMLReader& reader,
                                               const char* TypeName,
                                               App::Property* prop)
{
    // An input of another kind first, before Transformed takes an angle for
    // a length as it takes an old float for either
    if (App::PatternExtension::extensionHandleChangedPropertyType(reader, TypeName, prop)) {
        return;
    }
    // Occurrences was an App::PropertyInteger
    if (prop->getName() && std::strcmp(prop->getName(), "Occurrences") == 0
        && std::strcmp(TypeName, "App::PropertyInteger") == 0) {
        if (auto occurrences = dynamic_cast<App::PropertyIntegerConstraint*>(prop)) {
            App::PropertyInteger value;
            value.Restore(reader);
            occurrences->setValue(value.getValue());
            return;
        }
    }
    Transformed::handleChangedPropertyType(reader, TypeName, prop);
}
