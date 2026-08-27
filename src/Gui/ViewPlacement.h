/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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

#ifndef GUI_VIEWPLACEMENT_H
#define GUI_VIEWPLACEMENT_H

#include <FCGlobal.h>

namespace Gui {

class Document;
class MDIView;

/** Placement policy for freshly created views (docs/ViewPlacement.md).
 *
 * Decides WHERE a new view appears -- a new MDI tab, a cell of the
 * document's split-view area, or a floating window -- from the user's
 * preferences (BaseApp/Preferences/View/OpenView) and the view's
 * category. Opening something that already has a view never comes
 * here: reveal-if-open is handled by the openers themselves.
 */
namespace ViewPlacement {

enum class Category {
    Document,  ///< the first view of a new/opened document
    DocView,   ///< an additional view of a document that already has one
    Utility,   ///< documentless or meta content
};

/** Host a freshly created, not yet hosted view per the placement
 * preferences, then activate it (docs/ViewPlacement.md sec 3.2).
 * Split placement never replaces a 3D view's cell: non-3D content
 * reuses the last-used non-3D cell, everything else splits.
 */
GuiExport void place(MDIView *view, Category cat, Gui::Document *doc);

/** Host \a view in a new MDI tab unconditionally, without activating
 * it -- the pre-policy behavior, kept for document restore, which must
 * stay byte-stable and never consults the placement preferences. A 3D
 * view is born inside a fresh single-cell ViewArea when the container
 * is the default viewer widget (View/UseViewArea).
 */
GuiExport void placeTab(MDIView *view, Gui::Document *doc);

} // namespace ViewPlacement
} // namespace Gui

#endif // GUI_VIEWPLACEMENT_H
