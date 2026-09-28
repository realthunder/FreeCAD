// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTDESIGN_FeaturePathPattern_H
#define PARTDESIGN_FeaturePathPattern_H

#include <Mod/Part/App/PatternExtension.h>
#include "FeatureTransformed.h"

namespace PartDesign
{

/** Copies along a path of edges (upstream c633c5c88e)
 *
 * The inputs are Part::PathPatternExtension's, the pattern App::Pattern's.
 * The first occurrence is the untransformed original; the others move by
 * their frame on the path relative to the first one's, taken in the body's
 * frame and applied in the feature's.
 */
class PartDesignExport PathPattern : public PartDesign::Transformed,
                                     public Part::PathPatternExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::PathPattern);

public:
    PathPattern();

    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderPathPattern";
    }

    std::list<gp_Trsf> getTransformations(const std::vector<Part::TopoShape> &) override;
};

} //namespace PartDesign

#endif // PARTDESIGN_FeaturePathPattern_H
