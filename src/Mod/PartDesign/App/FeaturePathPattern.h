// SPDX-License-Identifier: LGPL-2.1-or-later

#ifndef PARTDESIGN_FeaturePathPattern_H
#define PARTDESIGN_FeaturePathPattern_H

#include "FeaturePattern.h"

namespace PartDesign
{

/** Copies along a path of edges (upstream c633c5c88e)
 *
 * PatternFeature preset to the path kind. The first occurrence is the
 * untransformed original; the others move by their frame on the path
 * relative to the first one's.
 */
class PartDesignExport PathPattern : public PartDesign::PatternFeature
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::PathPattern);

public:
    PathPattern();
};

} //namespace PartDesign


#endif // PARTDESIGN_FeaturePathPattern_H
