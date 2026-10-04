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

#ifndef GUI_SOTOGGLEMARKER_H
#define GUI_SOTOGGLEMARKER_H

#include <Inventor/fields/SoSFBool.h>
#include <Inventor/fields/SoSFInt32.h>
#include <Inventor/nodes/SoImage.h>

#include <FCGlobal.h>

namespace Gui
{

/** A round on-view toggle at a point: a cross on what is in, to take it
 * out, and a plus on what is out, to bring it back (upstream's pattern
 * instance buttons, e22e537c4b, as a node).
 *
 * It is an SoImage that paints its own glyph whenever a field of its own
 * changes, so it goes wherever a screen-space image goes: drawn by the GL
 * pass, captured by the render cache for an external backend (the companion
 * quad every SoImage gets) and with it streamed to a browser, and ray picked
 * where it is drawn, in whichever view does the picking. Nothing about it
 * needs a widget, which is what a served view has none of.
 */
class GuiExport SoToggleMarker: public SoImage
{
    using inherited = SoImage;

    SO_NODE_HEADER(SoToggleMarker);

public:
    static void initClass();
    SoToggleMarker();

    /// What the marker stands for is in: the glyph offers to take it out
    SoSFBool active;
    /// The pointer is over it
    SoSFBool highlighted;
    /// The diameter, in the pixels of the view drawing it
    SoSFInt32 markerSize;

protected:
    ~SoToggleMarker() override = default;
    void notify(SoNotList* list) override;

private:
    void paint();

    bool painting = false;
};

}  // namespace Gui

#endif  // GUI_SOTOGGLEMARKER_H
