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
    // Transforming features, an original moves from its own origin to each
    // point, and the base stays where it is: Transformed rewrites the
    // history so that the original is not left in place as well. Whole
    // shapes move from the base feature's origin, as upstream's do, and
    // positionBySupport() has already put the feature, and with it the
    // support, on the first point. Without a reference, as inside a
    // MultiTransform, the copies are relative to the first point.
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

bool PointPattern::isFirstInstanceTransformed() const
{
    return SubTransform.getValue() && PointObject.getValue();
}

void PointPattern::positionBySupport()
{
    Transformed::positionBySupport();
    if (SubTransform.getValue()) {
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
