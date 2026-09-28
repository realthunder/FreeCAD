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
 * The originals do not stay where they are: each copy lands on its own
 * point with its orientation kept. Transforming features (SubTransform),
 * an original moves from its own origin and the base stays in place. Whole
 * shapes move as upstream's do: the support is moved so that the base
 * feature's origin lands on the first point.
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
    bool isFirstInstanceTransformed() const override;

protected:
    void positionBySupport() override;
};

} //namespace PartDesign

#endif // PARTDESIGN_FeaturePointPattern_H
