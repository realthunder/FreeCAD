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

#ifndef APP_LINKARRAY_H
#define APP_LINKARRAY_H

#include "Link.h"
#include "PatternExtension.h"

namespace App
{

/** A link array whose elements are placed by a pattern
 *
 * PatternType picks the kind of pattern -- linear, polar, circular, along a
 * path, on points -- and may be changed at any time; App::PatternExtension
 * swaps the inputs with it. The label follows the kind until the user
 * renames it.
 *
 * Like App::Link it knows no geometry, so it arrays meshes as well as solids:
 * references are resolved through App::Pattern's resolvers.
 *
 * An element is suppressed by hiding it, in VisibilityList. A hidden element
 * is left out of getSubObjects(), and so out of the shape of the array. A
 * linear pattern keeps its suppressed elements by grid position in
 * SuppressedPositions, so that they stay where they were when the counts
 * change.
 */
class AppExport LinkArray: public App::Link, public App::PatternExtension
{
    PROPERTY_HEADER_WITH_EXTENSIONS(App::LinkArray);
    using inherited = App::Link;

public:
    LinkArray();

    /// Occurrences2 of the elements as they were generated, the stride of
    /// their grid positions
    App::PropertyInteger GeneratedOccurrences2;

    /// The labels of the kinds, which a label follows until it is renamed
    static const char* KindLabels[];

    Base::Type getSaveType() const override;

    const char* getViewProviderName() const override
    {
        return "Gui::ViewProviderLinkArray";
    }

    DocumentObjectExecReturn* execute() override;
    short mustExecute() const override;
    std::vector<std::string> getSubObjects(int reason = GS_DEFAULT) const override;

    /// Whether element \a index is suppressed, i.e. hidden
    bool isElementSuppressed(int index) const;
    /// Suppress element \a index, or restore it
    void setElementSuppressed(int index, bool suppressed);

protected:
    /** For the classes presetting the kind of pattern, as Part's
     *
     * @param saveTypes: the classes of the kinds, indexed by Pattern::Type,
     * which a file names for the array as its kind changes
     */
    LinkArray(Pattern::Type type, const char* const* saveTypes);

    void onChanged(const Property* prop) override;
    void onDocumentRestored() override;
    void onUndoRedoFinished() override;

private:
    void setupStatus();
    void syncElements(const std::vector<Base::Placement>& placements);
    void applySuppression();
    void readSuppression();
    void restoreElementSuppression();

    bool syncing = false;
};

}  // namespace App

#endif  // APP_LINKARRAY_H
