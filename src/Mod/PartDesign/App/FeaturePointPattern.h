// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTDESIGN_FeaturePointPattern_H
#define PARTDESIGN_FeaturePointPattern_H

#include "FeaturePattern.h"

namespace PartDesign
{

/** Copies at the vertices of an object (upstream f5abab2768)
 *
 * PatternFeature preset to the point kind. The originals do not stay where
 * they are: each copy lands on its own point with its orientation kept.
 */
class PartDesignExport PointPattern : public PartDesign::PatternFeature
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::PointPattern);

public:
    PointPattern();
};

} //namespace PartDesign


#endif // PARTDESIGN_FeaturePointPattern_H
