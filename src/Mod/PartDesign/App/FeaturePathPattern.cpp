// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Mod/Part/App/TopoShape.h>

#include "FeaturePathPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::PathPattern, PartDesign::Transformed)

PathPattern::PathPattern()
{
    Part::PathPatternExtension::initExtension(this);
}

std::list<gp_Trsf> PathPattern::getTransformations(const std::vector<Part::TopoShape> &)
{
    // The path is resolved in the body's frame and the originals are placed
    // in the feature's, so each step is conjugated into the latter. Upstream
    // applies the steps as they are, which is the same while the feature
    // sits at the body's origin, as a box does and a pad on a sketch that is
    // not in the XY plane does not.
    const Base::Placement placement = Placement.getValue();
    const Base::Placement inverse = placement.inverse();
    std::list<gp_Trsf> res;
    for (const auto& step : calculatePlacements(true)) {
        gp_Trsf trsf;
        Part::TopoShape::convertTogpTrsf((inverse * step * placement).toMatrix(), trsf);
        res.push_back(trsf);
    }
    return res;
}
