// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "FeaturePointPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::PointPattern, PartDesign::PatternFeature)

PointPattern::PointPattern()
    : PatternFeature(App::Pattern::Type::Point)
{}
