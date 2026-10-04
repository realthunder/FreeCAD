/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinländer <jrheinlaender@users.sourceforge.net> *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#ifndef PARTDESIGN_FeatureLinearPattern_H
#define PARTDESIGN_FeatureLinearPattern_H

#include "FeaturePattern.h"

namespace PartDesign
{

/** A pattern along one or two directions (upstream c334ac5062)
 *
 * PatternFeature preset to the linear kind: Occurrences x Occurrences2
 * translations, a grid over the two directions, the first one the identity
 * for the untransformed original. Per direction, Mode decides the steps --
 * "Extent" spreads the occurrences over Length, "Spacing" puts them Offset
 * apart, or as Spacings and SpacingPattern say.
 *
 * A direction is a straight edge, the normal of a planar face, a datum line
 * or plane, a sketch axis, or a sketch as a whole for its normal. A second
 * direction with more than one occurrence needs a reference of its own.
 */
class PartDesignExport LinearPattern : public PartDesign::PatternFeature
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::LinearPattern);

public:
    LinearPattern();
};

} //namespace PartDesign


#endif // PARTDESIGN_FeatureLinearPattern_H
