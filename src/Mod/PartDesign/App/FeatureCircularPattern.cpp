// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "FeatureCircularPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::CircularPattern, PartDesign::PatternFeature)

CircularPattern::CircularPattern()
    : PatternFeature(App::Pattern::Type::Circular)
{}
