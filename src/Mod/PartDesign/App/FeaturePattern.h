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

#ifndef PARTDESIGN_FEATUREPATTERN_H
#define PARTDESIGN_FEATUREPATTERN_H

#include <App/PatternExtension.h>

#include "FeatureTransformed.h"

namespace PartDesign
{

/** A pattern feature of any kind, which may be changed at any time
 *
 * PatternType picks the kind -- linear, polar, circular, along a path, on
 * points -- and App::PatternExtension swaps the inputs with it, upstream's
 * properties of that kind. The placements are App::Pattern's.
 *
 * LinearPattern, PolarPattern, CircularPattern, PathPattern and PointPattern
 * are this feature with the kind preset, so that their files, upstream's
 * among them, load. A feature is saved as the class of the kind it has
 * then, which upstream reads as that kind. The label follows the kind until
 * the user renames it.
 *
 * What differs by kind beside the inputs:
 *  - a path pattern's steps are taken in the body's frame and applied in the
 *    feature's, so each is conjugated into the latter;
 *  - a point pattern moves the originals themselves, each copy landing on a
 *    point: transforming features (SubTransform), an original moves from its
 *    own origin and the base stays in place; whole shapes move as upstream's
 *    do, the support moved so that the base feature's origin lands on the
 *    first point.
 *
 * A reference the new kind needs and does not have is set as the commands
 * set it when the kind changes: an axis of the originals' sketch, else of
 * the body's origin.
 */
class PartDesignExport PatternFeature: public PartDesign::Transformed,
                                       public App::PatternExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(PartDesign::PatternFeature);

public:
    PatternFeature();

    /// The labels of the kinds, and the classes a file names for them
    static const char* KindLabels[];
    static const char* KindTypes[];

    const char* getViewProviderName() const override
    {
        return "PartDesignGui::ViewProviderPattern";
    }
    Base::Type getSaveType() const override;

    std::list<gp_Trsf> getTransformations(const std::vector<Part::TopoShape>&) override;
    bool isFirstInstanceTransformed() const override;

    /** The placements of the pattern, one per occurrence
     *
     * @param relativeToFirst: a path or point pattern puts its first
     * occurrence at the identity, as a feature patterning its own shape
     * needs. The other kinds always start there.
     */
    std::vector<Base::Placement> calculatePlacements(bool relativeToFirst = true) const;

protected:
    /// For the classes presetting the kind
    explicit PatternFeature(App::Pattern::Type type);

    void positionBySupport() override;
    void onChanged(const App::Property* prop) override;
    void onUndoRedoFinished() override;
    void handleChangedPropertyType(Base::XMLReader& reader,
                                   const char* TypeName,
                                   App::Property* prop) override;

private:
    void setDefaultReferences();
};

}  // namespace PartDesign

#endif  // PARTDESIGN_FEATUREPATTERN_H
