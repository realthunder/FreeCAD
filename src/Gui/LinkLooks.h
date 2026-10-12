/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_LINKLOOKS_H
#define GUI_LINKLOOKS_H

#include <vector>

#include <FCGlobal.h>

#include <App/LinkAppearance.h>

namespace App
{
class DocumentObject;
class Property;
class PropertyAppearanceList;
class PropertyBool;
class PropertyBoolList;
class PropertyColorList;
}  // namespace App

namespace Gui
{

/** A view provider's names over the looks its object holds
 *
 * docs/ShapeAppearanceDesign.md sec 14.6.4. What a link, or an App::Part,
 * lays over what it shows is the object's, in its ElementAppearance
 * (App::LinkAppearance). Its view provider has always had properties for
 * the same -- OverrideMaterial and ShapeAppearance, OverrideColorList
 * beside the object's ColoredElements, and for an array MaterialList and
 * OverrideMaterialList -- and keeps them, for the scripts that say them and
 * for the files that have them, as names over the object's: each takes
 * what the store has when that changes, and a write to one is given to the
 * store. The view provider goes on drawing from them.
 *
 * A name carries App::Property::Legacy while it is one, so that no undo
 * holds it and no row of the log is made for it or put back into it: the
 * object's is.
 *
 * An object that holds no looks -- a link made in Python that has no store
 * -- leaves its view provider's properties the values they were.
 */
class GuiExport LinkLooks
{
public:
    /** @name The view provider's properties: null where it has none */
    //@{
    App::PropertyBool *overrideMaterial {nullptr};
    App::PropertyAppearanceList *shapeAppearance {nullptr};
    App::PropertyColorList *overrideColorList {nullptr};
    App::PropertyAppearanceList *materialList {nullptr};
    App::PropertyBoolList *overrideMaterialList {nullptr};
    /// What else of the view provider's says the same as one of those
    std::vector<App::Property *> others;
    //@}

    bool isBound() const
    {
        return names.store != nullptr;
    }
    /// What is being set is what the store has, and no write to it
    bool isMirroring() const
    {
        return mirroring;
    }
    /** Take the object's store, if it holds one
     *
     * @param obj: the object shown
     * @param restored: a document was read. A file that has no store has
     *                  the view provider's values, which are then taken
     *                  into it as a script's write would be (sec 14.6.6).
     */
    void bind(const App::DocumentObject *obj, bool restored);
    void unbind();
    /// A property of the object changed: the names take the store's
    void updateData(const App::Property *prop);
    /// A property of the view provider changed: a write to a name is
    /// given to the store. Nothing while \a restoring.
    void onChanged(const App::Property *prop, bool restoring);

private:
    void mark(bool on);
    void mirror();
    void adopt();
    /// The look here, where the link gives none and the object says another
    /// one, is the one it would give: kept by the object
    void keepChosen();

    App::LinkAppearance::Names names;
    bool mirroring {false};
    /// A write to one of the array's two lists is being given: the other
    /// is not set from the store meanwhile, it is written next
    bool writingArray {false};
};

}  // namespace Gui

#endif  // GUI_LINKLOOKS_H
