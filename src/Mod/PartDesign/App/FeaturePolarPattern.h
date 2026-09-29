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


#ifndef PARTDESIGN_FeaturePolarPattern_H
#define PARTDESIGN_FeaturePolarPattern_H

#include "FeaturePattern.h"

namespace PartDesign
{

/** A pattern around an axis (upstream c334ac5062)
 *
 * PatternFeature preset to the polar kind: Occurrences rotations, the first
 * one the identity. In "Extent" mode they are spread over Angle, evenly
 * around a full turn; in "Spacing" mode they are Offset apart, or as
 * Spacings and SpacingPattern say. A negative angle turns the other way.
 *
 * The axis is a straight edge, a circular edge (its center and normal), a
 * datum line or a sketch axis. Reversed turns the other way.
 */
class PartDesignExport PolarPattern : public PartDesign::PatternFeature
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::PolarPattern);

public:
    PolarPattern();
};

} //namespace PartDesign


#endif // PARTDESIGN_FeaturePolarPattern_H
