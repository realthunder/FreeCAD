/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef GUI_SOFCRENDERMATERIAL_H
#define GUI_SOFCRENDERMATERIAL_H

#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/nodes/SoNode.h>
#include <Inventor/nodes/SoSubNode.h>
#include <FCGlobal.h>

namespace Gui {

/// Extended (render engine) material parameters of the shapes that follow
/// in the scene graph. The node has no effect on Coin's own GL rendering:
/// it is captured by the mode-3 render cache traversal
/// (SoFCRenderCacheManager) into the per-draw material fed to an external
/// render backend, which uses the metallic/roughness pair in its
/// physically based shading path. Negative field values mean "unset" -
/// the per-view/global settings apply.
class GuiExport SoFCRenderMaterial : public SoNode {
    using inherited = SoNode;

    SO_NODE_HEADER(SoFCRenderMaterial);

public:
    static void initClass();
    SoFCRenderMaterial();

    SoSFFloat metallic;   ///< 0..1 metalness, < 0 = unset
    SoSFFloat roughness;  ///< 0..1 roughness, < 0 = unset (derive from shininess)

protected:
    ~SoFCRenderMaterial() override = default;
};

} // namespace Gui

#endif // GUI_SOFCRENDERMATERIAL_H
// vim: noai:ts=4:sw=4
