// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#ifndef PART_PATTERNEXTENSION_H
#define PART_PATTERNEXTENSION_H

#include <list>
#include <vector>

#include <gp_Trsf.hxx>

#include <App/DocumentObjectExtension.h>
#include <App/Pattern.h>
#include <App/PropertyGeo.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** The pattern extensions of upstream, a feature's pattern inputs as static
 * members
 *
 * The pattern itself is App::Pattern's: an extension only carries the
 * properties, with upstream's names, and hands out the result as upstream's
 * gp_Trsf list. A reference is resolved in the frame of the feature's parent
 * and brought into the feature's own by its Placement.
 */
class PartExport PatternExtension: public App::DocumentObjectExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::PatternExtension);

public:
    PatternExtension();

    App::Pattern::Type getPatternType() const
    {
        return patternType;
    }

    /** The placements of the pattern, one per occurrence
     *
     * @param relativeToFirst: a path or point pattern puts its first
     * occurrence at the identity, as a feature patterning its own shape
     * needs. The other kinds always start there.
     */
    std::vector<Base::Placement> calculatePlacements(bool relativeToFirst = true) const;
    /// As calculatePlacements(), as transformations
    std::list<gp_Trsf> calculateTransformations(bool relativeToFirst = true) const;

    void initExtension(App::ExtensionContainer* obj) override;
    short extensionMustExecute() override;
    void extensionOnChanged(const App::Property* prop) override;
    void onExtendedDocumentRestored() override;

protected:
    App::Pattern::Type patternType = App::Pattern::Type::Linear;
};

class PartExport LinearPatternExtension: public PatternExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::LinearPatternExtension);

public:
    LinearPatternExtension();

    App::PropertyLinkSub Direction;
    App::PropertyBool Reversed;
    App::PropertyEnumeration Mode;
    App::PropertyLength Length;
    App::PropertyLength Offset;
    App::PropertyIntegerConstraint Occurrences;
    App::PropertyFloatList Spacings;
    App::PropertyFloatList SpacingPattern;

    App::PropertyLinkSub Direction2;
    App::PropertyBool Reversed2;
    App::PropertyEnumeration Mode2;
    App::PropertyLength Length2;
    App::PropertyLength Offset2;
    App::PropertyIntegerConstraint Occurrences2;
    App::PropertyFloatList Spacings2;
    App::PropertyFloatList SpacingPattern2;

    App::PropertyIntPairList SuppressedPositions;
};

class PartExport PolarPatternExtension: public PatternExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::PolarPatternExtension);

public:
    PolarPatternExtension();

    App::PropertyLinkSub Axis;
    App::PropertyBool Reversed;
    App::PropertyEnumeration Mode;
    App::PropertyAngle Angle;
    App::PropertyAngle Offset;
    App::PropertyIntegerConstraint Occurrences;
    App::PropertyFloatList Spacings;
    App::PropertyFloatList SpacingPattern;
};

class PartExport CircularPatternExtension: public PatternExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::CircularPatternExtension);

public:
    CircularPatternExtension();

    App::PropertyLinkSub Axis;
    App::PropertyLength RadialDistance;
    App::PropertyLength TangentialDistance;
    App::PropertyIntegerConstraint NumberCircles;
    App::PropertyIntegerConstraint Symmetry;
};

class PartExport PathPatternExtension: public PatternExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::PathPatternExtension);

public:
    PathPatternExtension();

    App::PropertyLinkSub Path;
    App::PropertyIntegerConstraint Count;
    App::PropertyEnumeration SpacingMode;
    App::PropertyLength Spacing;
    App::PropertyLength StartOffset;
    App::PropertyLength EndOffset;
    App::PropertyBool ReversePath;
    App::PropertyBool Align;
    App::PropertyVector VerticalVector;
};

class PartExport PointPatternExtension: public PatternExtension
{
    EXTENSION_PROPERTY_HEADER_WITH_OVERRIDE(Part::PointPatternExtension);

public:
    PointPatternExtension();

    App::PropertyLinkSub PointObject;
};

}  // namespace Part

#endif  // PART_PATTERNEXTENSION_H
