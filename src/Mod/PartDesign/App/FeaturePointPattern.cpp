// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PreCompiled.h"

#include <Mod/Part/App/TopoShape.h>

#include "FeaturePointPattern.h"

using namespace PartDesign;

PROPERTY_SOURCE_WITH_EXTENSIONS(PartDesign::PointPattern, PartDesign::Transformed)

PointPattern::PointPattern()
{
    Part::PointPatternExtension::initExtension(this);
}

std::list<gp_Trsf> PointPattern::getTransformations(const std::vector<Part::TopoShape> &)
{
    const auto points = calculatePlacements(false);
    if (points.empty()) {
        return {};
    }

    // The points are in the body's frame, the originals in the feature's.
    // An original moves from the base feature's origin to each point, and
    // positionBySupport() has already put the feature, and with it the
    // support, on the first one. Without a base, as inside a MultiTransform,
    // the copies are relative to the first point.
    Base::Vector3d origin = points.front().getPosition();
    if (auto base = getBaseObject(/*silent=*/true)) {
        origin = base->Placement.getValue().getPosition();
    }
    const Base::Rotation inverse = Placement.getValue().getRotation().inverse();

    std::list<gp_Trsf> res;
    for (const auto& point : points) {
        Base::Vector3d offset;
        inverse.multVec(point.getPosition() - origin, offset);
        gp_Trsf trsf;
        trsf.SetTranslation(gp_Vec(offset.x, offset.y, offset.z));
        res.push_back(trsf);
    }
    return res;
}

void PointPattern::positionBySupport()
{
    Transformed::positionBySupport();

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
