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

#include <App/PropertyUnits.h>
#include "FeatureTransformed.h"

class gp_Dir;
class gp_Vec;

namespace PartDesign
{
enum class LinearPatternMode {
    Extent,
    Spacing
};

enum class LinearPatternDirection {
    First,
    Second
};

class PartDesignExport LinearPattern : public PartDesign::Transformed
{
    PROPERTY_HEADER_WITH_OVERRIDE(PartDesign::LinearPattern);

public:
    LinearPattern();

    App::PropertyLinkSub     Direction;
    App::PropertyBool        Reversed;
    App::PropertyEnumeration Mode;
    App::PropertyLength      Length;
    App::PropertyLength      Offset;
    App::PropertyIntegerConstraint Occurrences;
    App::PropertyFloatList   Spacings;
    App::PropertyFloatList   SpacingPattern;

    App::PropertyLinkSub     Direction2;
    App::PropertyBool        Reversed2;
    App::PropertyEnumeration Mode2;
    App::PropertyLength      Length2;
    App::PropertyLength      Offset2;
    App::PropertyIntegerConstraint Occurrences2;
    App::PropertyFloatList   Spacings2;
    App::PropertyFloatList   SpacingPattern2;

   /** @name methods override feature */
    //@{
    short mustExecute() const override;

    /// returns the type name of the view provider
    const char* getViewProviderName() const override {
        return "PartDesignGui::ViewProviderLinearPattern";
    }
    //@}

    /** Create transformations
      * Returns Occurrences x Occurrences2 transformations, a grid over the two
      * directions, the first one the identity for the untransformed original.
      *
      * Per direction, Mode decides the steps:
      * 1. "Extent": each step is Length / (Occurrences - 1), so that the
      *    steps cover the total Length.
      * 2. "Spacing": the gap before occurrence i + 1 is Spacings[i] when that
      *    is not -1, else SpacingPattern[i % n] when the pattern has more than
      *    one value, else Offset.
      *
      * If Direction contains a feature and a face name, then the transformation direction will be
      *   the normal of the given face, which must be planar. If it contains an edge name, then the
      *   transformation direction will be parallel to the given edge, which must be linear
      *
      * If Reversed is true, the direction of transformation will be opposite
      */
    std::list<gp_Trsf> getTransformations(const std::vector<Part::TopoShape> &) override;

    /// The gap before occurrence \a index + 1 of a direction in Spacing mode
    double getSpacing(LinearPatternDirection dir, int index) const;

protected:
    void handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName, App::Property* prop) override;
    void onChanged(const App::Property* prop) override;
    void onDocumentRestored() override;

    static const App::PropertyIntegerConstraint::Constraints intOccurrences;

public:
    /// The most gaps Spacings is grown to when Occurrences changes
    static constexpr int MaxListedSpacings = 1000;

private:
    static const char* ModeEnums[];

    struct DirectionProps {
        App::PropertyLinkSub& direction;
        App::PropertyBool& reversed;
        App::PropertyEnumeration& mode;
        App::PropertyLength& length;
        App::PropertyLength& offset;
        App::PropertyIntegerConstraint& occurrences;
        App::PropertyFloatList& spacings;
        App::PropertyFloatList& spacingPattern;
    };
    DirectionProps props(LinearPatternDirection dir);
    const DirectionProps props(LinearPatternDirection dir) const {
        return const_cast<LinearPattern*>(this)->props(dir);
    }

    gp_Dir getDirection(const App::PropertyLinkSub& prop) const;
    std::vector<gp_Vec> getSteps(LinearPatternDirection dir) const;
    void setReadWriteStatusForMode(LinearPatternDirection dir);
    void syncLengthAndOffset(LinearPatternDirection dir);
    void resizeSpacings(LinearPatternDirection dir);
};

} //namespace PartDesign


#endif // PARTDESIGN_FeatureLinearPattern_H
