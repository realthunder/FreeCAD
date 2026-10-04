// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTDESIGN_FeatureCircularPattern_H
#define PARTDESIGN_FeatureCircularPattern_H

#include "FeaturePattern.h"

namespace PartDesign
{

/** Concentric rings of copies around an axis (upstream 173276b175)
 *
 * PatternFeature preset to the circular kind: NumberCircles rings
 * RadialDistance apart, each ring as many copies as fit TangentialDistance
 * apart, rounded to a multiple of Symmetry. The axis is resolved as a polar
 * pattern's, a PD datum line included, and brought into the feature's frame
 * by its Placement.
 */
class PartDesignExport CircularPattern : public PartDesign::PatternFeature
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::CircularPattern);

public:
    CircularPattern();
};

} //namespace PartDesign


#endif // PARTDESIGN_FeatureCircularPattern_H
