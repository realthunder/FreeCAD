/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#ifndef GUI_OWNNODESPATH_H
#define GUI_OWNNODESPATH_H

#include <vector>

#include <Base/Type.h>
#include <FCGlobal.h>

namespace App {
class DocumentObject;
}

namespace Gui {

/** Asks ViewProvider::getDetailPath() for the nodes of the object itself
 *
 * A view provider may answer getDetailPath() with the nodes its object is
 * SEEN through in place of its own. A PartDesign solid body does: its own
 * shape is hidden, the features draw the solid, and a selection of it has to
 * highlight something. That answer follows what is shown at the moment it is
 * asked for.
 *
 * A caller that keeps something on the path -- a view's own visibility
 * table, a shader binding -- resolves it when the structure of the document
 * changes and not when an object is shown or hidden, and means the object,
 * not the look of it. It holds one of these while it asks, and gets the
 * object's own nodes, which move with the structure only.
 */
class GuiExport OwnNodesPath
{
public:
    OwnNodesPath();
    ~OwnNodesPath();
    OwnNodesPath(const OwnNodesPath &) = delete;
    OwnNodesPath &operator=(const OwnNodesPath &) = delete;

    /// Whether the path asked for now is that of the object's own nodes
    static bool wanted();
};

/** The objects an object is seen through, beside its own nodes
 *
 * The other half of OwnNodesPath. Some objects are drawn by others that sit
 * BESIDE them in the scene, not under them: what a plain group holds, and
 * the features of a PartDesign solid body. Whoever keeps something on the
 * nodes of such an object -- a view's own visibility of it, a shader bound
 * to it -- has to keep it on those of the others too, or it acts on nodes
 * that draw nothing.
 *
 * The answer depends on the structure of the document only (which objects
 * the group holds, which features the solid has), never on what is shown at
 * the moment, so it is right for as long as a path resolved with
 * OwnNodesPath is. The others are named as their owner is, one step up:
 * where the owner is 'Part.Group.' they are 'Part.Box.', and top-level
 * objects where it is one.
 */
class GuiExport SeenThrough
{
public:
    enum class For {
        /// Hidden with the object
        Hide,
        /// Shown with it, the object itself among them where its own nodes
        /// are what a show of it shows: a solid body shows its own shape and
        /// none of its features, a plain group what it holds and never
        /// itself, which would draw each child again and one hidden by hand
        /// with them
        Show,
        /// Dressed as it is: a shader bound to it
        Look,
    };

    using Members = std::vector<App::DocumentObject*> (*)(const App::DocumentObject *, For);

    /// What objects of \a type are seen through. A plain group needs none:
    /// it is seen through what it holds.
    static void add(const Base::Type &type, Members members);

    /// The objects \a obj is seen through, none for most
    static std::vector<App::DocumentObject*> of(const App::DocumentObject *obj, For what);
};

} // namespace Gui

#endif // GUI_OWNNODESPATH_H
