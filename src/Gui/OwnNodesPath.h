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

#include <FCGlobal.h>

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

} // namespace Gui

#endif // GUI_OWNNODESPATH_H
