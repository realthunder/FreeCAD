// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTDESIGN_FeaturePointPattern_H
#define PARTDESIGN_FeaturePointPattern_H

#include <Mod/Part/App/PatternExtension.h>
#include "FeatureTransformed.h"

namespace PartDesign
{

/** Copies at the vertices of an object (upstream f5abab2768)
 *
 * The inputs are Part::PointPatternExtension's, the pattern App::Pattern's.
 * As upstream's, the feature does not keep the originals where they are: it
 * moves its support so that the base feature's origin lands on the first
 * point, and each copy lands on its own point with its orientation kept.
 */
class PartDesignExport PointPattern : public PartDesign::Transformed,
                                      public Part::PointPatternExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::PointPattern);

public:
    PointPattern();

    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderPointPattern";
    }

    std::list<gp_Trsf> getTransformations(const std::vector<Part::TopoShape> &) override;

protected:
    void positionBySupport() override;
};

} //namespace PartDesign

#endif // PARTDESIGN_FeaturePointPattern_H
