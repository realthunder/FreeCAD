// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "FeaturePathPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::PathPattern, PartDesign::PatternFeature)

PathPattern::PathPattern()
    : PatternFeature(App::Pattern::Type::Path)
{}
