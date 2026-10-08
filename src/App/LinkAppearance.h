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

#ifndef APP_LINKAPPEARANCE_H
#define APP_LINKAPPEARANCE_H

#include <string>
#include <vector>

#include <boost/dynamic_bitset.hpp>

#include <FCGlobal.h>

#include "PropertyElementAppearance.h"

namespace Data
{
class MappedName;
}

namespace App
{

class DocumentObject;
class PropertyAppearanceList;
class PropertyBool;

/** What a link holds of the looks of what it shows
 *
 * docs/ShapeAppearanceDesign.md sec 14.6.4. A link has no shape of its own
 * and nothing is made for it: what it holds is laid over what it shows when
 * that is drawn. It holds it in an ElementAppearance whose names are paths
 * (PropertyElementAppearance::setPathNames()), as an App::Part does:
 *
 *  - the own look of "Face": the look the link gives all it shows. Given at
 *    all is the override on;
 *  - a name for each element given a colour -- "Face3" of what it shows,
 *    "2.Face3" of one of an array, "Pad.Face3" of an object below -- with a
 *    look whose own field is the colour, or more where it was given more;
 *  - a name ending in the hidden marker, which states nothing: the element
 *    is not shown;
 *  - the name of one of an array, "2.", with a look that is all its own.
 *
 * The names of elements come first, in the order they were given, and the
 * array's after them.
 *
 * What a link was known by before is kept, as names over that: on the
 * object ColoredElements, which a file has always had and is written to it
 * still, and OverrideMaterial and ShapeAppearance, which are in no file; on
 * its view provider the same two and OverrideColorList, MaterialList and
 * OverrideMaterialList. The functions here are how each of them reads and
 * writes the store, and onChanged() keeps the object's own in step.
 */
class AppExport LinkAppearance
{
public:
    using Store = PropertyElementAppearance;

    /// The properties one object holds its looks in and knows them by
    struct Names
    {
        Store *store {nullptr};
        PropertyLinkSubHidden *colored {nullptr};
        PropertyBool *overrideMaterial {nullptr};
        PropertyAppearanceList *shapeAppearance {nullptr};
    };
    /// Those of a link or of an App::Part. No store: it keeps no looks, and
    /// what its view provider has are that one's own values.
    static Names namesOf(const DocumentObject *obj);
    static Store *storeOf(const DocumentObject *obj)
    {
        return namesOf(obj).store;
    }

    /// Whether \a name is one of an array's, "2.", and which
    static bool isArrayName(const std::string &name, int *index = nullptr);
    /// Whether \a name is of an element that is not shown
    static bool isHiddenName(const std::string &name);

    /** @name The look the link gives all it shows
     *
     * Given at all is the override on. Turned off, the look it gave is
     * kept (PropertyElementAppearance::setKept()) for when it gives one
     * again, and so is a look it is given while it gives none: what
     * ShapeAppearance says of a link that does not override.
     */
    //@{
    static bool hasOverride(const Store &store);
    static void setOverride(Store &store, bool on, const MaterialAppearance &look);
    /// The look it would give. Nothing while it gives one, which is that.
    static void keepLook(Store &store, const MaterialAppearance &look);
    /// The look given where nobody chose one: the preference's link colour,
    /// and for an App::Part its shape colour
    static MaterialAppearance defaultLook(bool ofPart);
    //@}

    /** @name The elements given a colour, or hidden
     *
     * In the order they were given. A colour is a look whose own field is
     * the colour; a name given what it has keeps the rest of its look.
     */
    //@{
    static void getColored(const Store &store, std::vector<std::string> &names,
                           std::vector<Color> &colors);
    /// \a colors may be shorter than \a names: a name past its end keeps
    /// what it has, or states nothing
    static void setColored(Store &store, const std::vector<std::string> &names,
                           const std::vector<Color> &colors);
    //@}

    /** @name The looks of the elements of an array */
    //@{
    static void getArrayLooks(const Store &store, std::vector<MaterialAppearance> &looks,
                              boost::dynamic_bitset<> &given);
    static void setArrayLooks(Store &store, const std::vector<MaterialAppearance> &looks,
                              const boost::dynamic_bitset<> &given);
    //@}

    /** The object's own names kept in step with the store
     *
     * Called by the owner for every change of one of its properties. A
     * change of the store is given to the names; a write to a name is given
     * to the store. Nothing while a document is read: see onRestored().
     *
     * @param names: the owner's properties
     * @param prop: the one that changed
     * @param busy: the owner's guard, held while the names are given what
     *              the store has
     */
    static void onChanged(const Names &names, const Property *prop, bool &busy);
    /** After a document is read
     *
     * A file that has the store is what it says. One that has not -- an
     * older one, upstream's -- has ColoredElements: its names are taken as
     * names that state nothing, and are given their colours when a view
     * provider has read its own (sec 14.6.6).
     */
    static void onRestored(const Names &names, bool &busy);
    /// A write to the ShapeAppearance name: the look of the override where
    /// that is on, and the look it would have where it is not
    static void writeAppearance(const Names &names, const AppearanceList &after, bool &busy);

    /** What a link lays over an element of what it shows
     *
     * For a face made from a face seen through a link: the colour the link
     * gives all it shows, or the one of an array the element is of.
     *
     * @param mapped: the element, as the object the link shows names it
     * @param obj: the object the element's history leads to; changed to
     *             what the link shows of it
     * @param color: the colour laid over, if any is
     * @return Whether \a color was given
     */
    static bool getLinkColor(const Data::MappedName &mapped, DocumentObject *&obj, Color &color);
};

}  // namespace App

#endif  // APP_LINKAPPEARANCE_H
