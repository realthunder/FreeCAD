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

#include <Inventor/fields/SoSFBool.h>
#include <Inventor/fields/SoSFEnum.h>
#include <Inventor/fields/SoSFFloat.h>
#include <Inventor/fields/SoSFImage.h>
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
    /// The shapes form a water body: their closed volume becomes a
    /// scattering medium of the render engine's volumetric lighting
    /// pass (tinted by the material diffuse color), instead of an
    /// ordinary surface. Only external backends consume this.
    SoSFBool water;
    /// Water extinction density in inverse world units; <= 0 = automatic
    /// (from the body extent).
    SoSFFloat waterDensity;

protected:
    ~SoFCRenderMaterial() override = default;
};

/// Render-engine-only material texture map (emissive/occlusion/
/// metallic-roughness) of the
/// shapes that follow in the scene graph. Like SoFCRenderMaterial the
/// node has no effect on Coin's own GL rendering — it derives straight
/// from SoNode (deriving from SoTexture2 would feed Coin's texture
/// element and the manager's unit-0 texture capture); the mode-3 render
/// cache traversal captures it into the per-draw material of the
/// external render backends, which sample it with the mesh texture
/// coordinates (an enabled color texture unit is what makes shapes
/// generate them — a white stand-in texture accompanies a lone map).
class GuiExport SoFCRenderTexture : public SoNode {
    using inherited = SoNode;

    SO_NODE_HEADER(SoFCRenderTexture);

public:
    static void initClass();
    SoFCRenderTexture();

    enum Slot {
        /// rgb added to the lit (and textured) fragment color
        EMISSIVE,
        /// r multiplies the ambient/environment light contribution
        OCCLUSION,
        /// glTF metallic-roughness map: g multiplies the roughness
        /// factor, b the metallic factor (PBR shading only)
        METALLIC_ROUGHNESS,
    };
    enum Wrap {
        REPEAT,
        CLAMP,
    };

    SoSFEnum slot;    ///< which material map this image feeds
    SoSFImage image;  ///< the map pixels (RGB8/RGBA8, bottom-up)
    SoSFEnum wrapS;
    SoSFEnum wrapT;

protected:
    ~SoFCRenderTexture() override = default;
};

} // namespace Gui

#endif // GUI_SOFCRENDERMATERIAL_H
// vim: noai:ts=4:sw=4
