// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include "FeatureCircularPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::CircularPattern, PartDesign::Transformed)

CircularPattern::CircularPattern()
{
    Part::CircularPatternExtension::initExtension(this);
}

std::list<gp_Trsf> CircularPattern::getTransformations(const std::vector<Part::TopoShape> &)
{
    return calculateTransformations();
}
