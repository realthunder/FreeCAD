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

#include <App/DocumentObject.h>

#include "PatternExtension.h"
#include "TopoShape.h"

using namespace Part;

/// A member of an extension as the pattern table describes it, with its
/// default value and constraints
#define ADD_PATTERN_PROPERTY(_prop_)                                                             \
    do {                                                                                         \
        auto spec = App::Pattern::getPropertySpec(patternType, #_prop_);                         \
        propertyData.addProperty(static_cast<App::Extension*>(this),                             \
                                 #_prop_,                                                        \
                                 &this->_prop_,                                                  \
                                 spec ? spec->group : "Pattern",                                 \
                                 App::Prop_None,                                                 \
                                 spec ? spec->doc : "");                                         \
        App::Pattern::initProperty(patternType, &this->_prop_, #_prop_);                         \
    } while (0)

EXTENSION_PROPERTY_SOURCE(Part::PatternExtension, App::DocumentObjectExtension)

PatternExtension::PatternExtension()
{
    initExtensionType(PatternExtension::getExtensionClassTypeId());
}

void PatternExtension::initExtension(App::ExtensionContainer* obj)
{
    App::DocumentObjectExtension::initExtension(obj);
    App::Pattern::setupProperties(patternType, *obj);
}

std::vector<Base::Placement> PatternExtension::calculatePlacements(bool relativeToFirst) const
{
    App::Pattern::Context context;
    auto obj = getExtendedContainer();
    if (auto placement =
            dynamic_cast<App::PropertyPlacement*>(obj->getPropertyByName("Placement"))) {
        context.placement = placement->getValue();
    }
    context.relativeToFirst = relativeToFirst;
    return App::Pattern::getPlacements(patternType, *obj, context);
}

std::list<gp_Trsf> PatternExtension::calculateTransformations(bool relativeToFirst) const
{
    std::list<gp_Trsf> res;
    for (const auto& placement : calculatePlacements(relativeToFirst)) {
        gp_Trsf trsf;
        TopoShape::convertTogpTrsf(placement.toMatrix(), trsf);
        res.push_back(trsf);
    }
    return res;
}

short PatternExtension::extensionMustExecute()
{
    return App::Pattern::isTouched(patternType, *getExtendedContainer()) ? 1 : 0;
}

void PatternExtension::extensionOnChanged(const App::Property* prop)
{
    auto obj = getExtendedObject();
    if (obj && !obj->isRestoring() && App::Pattern::isPatternProperty(patternType, prop)) {
        App::Pattern::onChanged(patternType, *obj, prop);
    }
    App::DocumentObjectExtension::extensionOnChanged(prop);
}

void PatternExtension::onExtendedDocumentRestored()
{
    // Constraints and the status the modes imply are not in the file
    App::Pattern::setupProperties(patternType, *getExtendedContainer());
    App::DocumentObjectExtension::onExtendedDocumentRestored();
}

// ----------------------------------------------------------------------------

EXTENSION_PROPERTY_SOURCE(Part::LinearPatternExtension, Part::PatternExtension)

LinearPatternExtension::LinearPatternExtension()
{
    initExtensionType(LinearPatternExtension::getExtensionClassTypeId());
    patternType = App::Pattern::Type::Linear;

    ADD_PATTERN_PROPERTY(Direction);
    ADD_PATTERN_PROPERTY(Reversed);
    ADD_PATTERN_PROPERTY(Mode);
    ADD_PATTERN_PROPERTY(Length);
    ADD_PATTERN_PROPERTY(Offset);
    ADD_PATTERN_PROPERTY(Occurrences);
    ADD_PATTERN_PROPERTY(Spacings);
    ADD_PATTERN_PROPERTY(SpacingPattern);
    ADD_PATTERN_PROPERTY(Direction2);
    ADD_PATTERN_PROPERTY(Reversed2);
    ADD_PATTERN_PROPERTY(Mode2);
    ADD_PATTERN_PROPERTY(Length2);
    ADD_PATTERN_PROPERTY(Offset2);
    ADD_PATTERN_PROPERTY(Occurrences2);
    ADD_PATTERN_PROPERTY(Spacings2);
    ADD_PATTERN_PROPERTY(SpacingPattern2);
    ADD_PATTERN_PROPERTY(SuppressedPositions);
}

EXTENSION_PROPERTY_SOURCE(Part::PolarPatternExtension, Part::PatternExtension)

PolarPatternExtension::PolarPatternExtension()
{
    initExtensionType(PolarPatternExtension::getExtensionClassTypeId());
    patternType = App::Pattern::Type::Polar;

    ADD_PATTERN_PROPERTY(Axis);
    ADD_PATTERN_PROPERTY(Reversed);
    ADD_PATTERN_PROPERTY(Mode);
    ADD_PATTERN_PROPERTY(Angle);
    ADD_PATTERN_PROPERTY(Offset);
    ADD_PATTERN_PROPERTY(Occurrences);
    ADD_PATTERN_PROPERTY(Spacings);
    ADD_PATTERN_PROPERTY(SpacingPattern);
}

EXTENSION_PROPERTY_SOURCE(Part::CircularPatternExtension, Part::PatternExtension)

CircularPatternExtension::CircularPatternExtension()
{
    initExtensionType(CircularPatternExtension::getExtensionClassTypeId());
    patternType = App::Pattern::Type::Circular;

    ADD_PATTERN_PROPERTY(Axis);
    ADD_PATTERN_PROPERTY(RadialDistance);
    ADD_PATTERN_PROPERTY(TangentialDistance);
    ADD_PATTERN_PROPERTY(NumberCircles);
    ADD_PATTERN_PROPERTY(Symmetry);
}

EXTENSION_PROPERTY_SOURCE(Part::PathPatternExtension, Part::PatternExtension)

PathPatternExtension::PathPatternExtension()
{
    initExtensionType(PathPatternExtension::getExtensionClassTypeId());
    patternType = App::Pattern::Type::Path;

    ADD_PATTERN_PROPERTY(Path);
    ADD_PATTERN_PROPERTY(Count);
    ADD_PATTERN_PROPERTY(SpacingMode);
    ADD_PATTERN_PROPERTY(Spacing);
    ADD_PATTERN_PROPERTY(StartOffset);
    ADD_PATTERN_PROPERTY(EndOffset);
    ADD_PATTERN_PROPERTY(ReversePath);
    ADD_PATTERN_PROPERTY(Align);
    ADD_PATTERN_PROPERTY(VerticalVector);
}

EXTENSION_PROPERTY_SOURCE(Part::PointPatternExtension, Part::PatternExtension)

PointPatternExtension::PointPatternExtension()
{
    initExtensionType(PointPatternExtension::getExtensionClassTypeId());
    patternType = App::Pattern::Type::Point;

    ADD_PATTERN_PROPERTY(PointObject);
}
