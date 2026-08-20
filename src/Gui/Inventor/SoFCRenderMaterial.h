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
#include <Inventor/fields/SoSFInt32.h>
#include <Inventor/fields/SoMFFloat.h>
#include <Inventor/fields/SoMFInt32.h>
#include <Inventor/fields/SoMFVec4f.h>
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
    /** Per-face form of the pair above (empty = the scalars apply)
     *
     * A per-face PBR appearance states a factor pair per face, and
     * neither has a Coin material field to ride: the lazy element's
     * per-face arrays are SbColor, which drops the alpha the metallic
     * factor occupies in the stored material, and the shininess slot
     * carries the Phong quantity. So the pair travels from here into
     * SoFCPbrElement on traversal, and the render cache bakes it into
     * the per-vertex material stream. Both fields are set or neither
     * is; entry i is face i, a face past the end reads entry 0 (see
     * SoFCPbrElement). The scalars stay the entry-0 values, so a
     * consumer that ignores these arrays keeps the pre-feature look.
     */
    SoMFFloat metallics;
    SoMFFloat roughnesses;
    /** Machined surface finish (App::SurfaceFinish) of the shapes
     *
     * The pattern the surface was given -- knurled, brushed, blasted,
     * turned -- which external backends shade as a procedural normal
     * perturbation that becomes plain roughness once its features fall
     * below the pixel footprint. Fed either from a finish authored on
     * the appearance or from the ViewProvider's Render_Finish*
     * properties. finish is the App::SurfaceFinish::Pattern value, 0 =
     * none; the pitch and depth are in millimetres of OBJECT space (a
     * scaled or instanced copy keeps its finish attached to its
     * geometry) and the angle is the lay direction in degrees.
     */
    SoSFInt32 finish;
    SoSFFloat finishPitch;
    SoSFFloat finishDepth;
    SoSFFloat finishAngle;
    /** Per-face form of the finish above (empty = the scalars apply)
     *
     * A finish is four numbers, so a per-face one would be four arrays --
     * three more than a per-vertex stream should carry. Instead the
     * distinct finishes the appearance holds form a PALETTE, and what
     * travels per face is one index into it: finishPalette holds the
     * entries as (pattern, pitch, depth, angle) exactly as the scalars
     * above state them, and finishIndices holds one palette index per
     * face. Both fields are set or neither is, and entry 0 of the
     * palette is what the scalars repeat -- so a consumer that ignores
     * them keeps the per-object look.
     *
     * finishIndices travels into SoFCFinishElement on traversal and is
     * baked into the per-vertex material stream by the render cache; the
     * palette is read straight off this node by the cache's post
     * callback, since its consumer is the draw material rather than the
     * shape below.
     */
    SoMFVec4f finishPalette;
    SoMFInt32 finishIndices;
    /** The projection frames the finish above is laid out in
     *
     * Where the finish says what was done to the surface, the frame says
     * what the surface IS -- the plane's own axes, or the axis a
     * cylinder or cone was turned about -- so a straight knurl runs
     * along the axis and turning marks centre on it, rather than being
     * projected triplanarly off the object-space normal.
     *
     * These come from the GEOMETRY, so the Part view provider writes
     * them at tessellation time while the analytic OCCT surface is
     * still in hand, and it writes them into these two fields alone --
     * the appearance-derived fields above have a different writer, and
     * neither touches the other's.
     *
     * Three SbVec4f make one frame: (origin, kind), (axis, radius),
     * (xdir, spare), matching Render::SurfaceFrame. Entry 0 is the
     * FIRST face's frame -- what a mesh carrying no per-vertex stream
     * reads -- and a face whose surface could not be classified states
     * the unframed frame, which shades triplanarly as before.
     * frameIndices holds one entry index per face.
     *
     * Published to the shapes below (as SoFCFinishElement's second
     * array) ONLY while a finish is stated somewhere, since a frame with
     * no pattern to lay out would make every analytic shape in the
     * document pay for a per-vertex stream it cannot use.
     */
    SoMFVec4f framePalette;
    SoMFInt32 frameIndices;
    /** Per-face texture layer (empty = the faces carry no images)
     *
     * A face can be given an image of its own, and a draw binds one
     * sampler -- so the images become a PALETTE (the SoFCRenderTexture
     * nodes whose slot is FACE, each holding the layer it occupies) and
     * what travels per face is one index into it. LAYER 0 IS THE
     * UNTEXTURED FACE and has no palette entry, so a shape that images
     * only two of its faces states 0 for all the rest.
     *
     * The array travels into SoFCFaceTextureElement on traversal and is
     * baked into the per-vertex material stream by the render cache; the
     * images are read off the palette nodes by the cache, since their
     * consumer is the draw material rather than the shape below.
     *
     * Indexed by the FACE (the shape's part index) rather than by the
     * material index: an image is put on a face, and two faces sharing
     * one colour may well carry different images.
     */
    SoMFInt32 faceTextureIndices;
    /** How large the per-face images above are laid out, in millimetres
     * of OBJECT space per tile
     *
     * Their coordinates come from the face's own projection frame (the
     * framePalette above), so an image needs a physical size the way a
     * printed decal or a machined marking does -- not a fraction of a
     * bounding box that changes when the part does.
     *
     * <= 0 hands the mesh's own texture coordinates to them instead,
     * which is what a shape that was really UV mapped wants.
     */
    SoSFFloat faceTextureScale;
    /// The shapes form a water body: their closed volume becomes a
    /// scattering medium of the render engine's volumetric lighting
    /// pass (tinted by the material diffuse color), instead of an
    /// ordinary surface. Only external backends consume this.
    SoSFBool water;
    /// Water extinction density in inverse world units; <= 0 = automatic
    /// (from the body extent).
    SoSFFloat waterDensity;
    /// The shapes form a glass body: instead of the ordinary
    /// transparent path, external backends render them with
    /// screen-space refraction, Fresnel-blended environment reflection
    /// and per-channel Beer-Lambert absorption tinted by the material
    /// diffuse over the body thickness.
    SoSFBool glass;
    /// Index of refraction; <= 0 = default (1.5).
    SoSFFloat glassIOR;
    /// Absorption density in inverse world units; <= 0 = automatic
    /// (from the body extent).
    SoSFFloat glassDensity;
    /// 0..1 surface roughness of the glass (blurs the environment
    /// reflection); < 0 = 0.
    SoSFFloat glassRoughness;
    /// The shapes form a cloud body: the closed volume becomes a
    /// procedural-density (FBM) scattering medium of the render
    /// engine's volumetric lighting pass; the body geometry itself is
    /// not rendered.
    SoSFBool cloud;
    /// Cloud extinction density in inverse world units; <= 0 =
    /// automatic (from the body extent).
    SoSFFloat cloudDensity;
    /// Noise domain scale in inverse world units; <= 0 = automatic
    /// (a few puffs across the body).
    SoSFFloat cloudDetail;
    /// Drift speed multiplier of the animated noise domain.
    SoSFFloat cloudSpeed;
    /// The shapes form a fire body: the closed volume becomes an
    /// emissive flame medium of the render engine's volumetric
    /// lighting pass (rising FBM noise through a blackbody-style color
    /// ramp, added on top of the scene); the body geometry itself is
    /// not rendered.
    SoSFBool fire;
    /// Flame brightness multiplier; <= 0 = default (1).
    SoSFFloat fireIntensity;
    /// Noise domain scale in inverse world units; <= 0 = automatic
    /// (a few tongues across the body).
    SoSFFloat fireDetail;
    /// Rise speed multiplier of the animated noise domain.
    SoSFFloat fireSpeed;
    /// The shapes form a fountain body: the closed volume becomes a
    /// water-spray scattering medium of the render engine's volumetric
    /// lighting pass (a rising jet plus a parabolic fall envelope with
    /// streak noise); the body geometry itself is not rendered.
    SoSFBool fountain;
    /// Spray extinction density (1/world units); <= 0 = automatic
    /// (from the body bounds).
    SoSFFloat fountainDensity;
    /// Noise domain scale in inverse world units; <= 0 = automatic
    /// (streaks across the body).
    SoSFFloat fountainDetail;
    /// Flow speed multiplier of the animated spray.
    SoSFFloat fountainSpeed;
    /// The shapes form a light-source body: rendered unshaded at the
    /// diffuse color, fed to the render engine's bloom pass at
    /// lightIntensity, and shining as an unshadowed point light on lit
    /// surfaces around it (bulb, sun disc).
    SoSFBool lightSource;
    /// Emission strength (HDR bloom multiplier and point-light
    /// brightness); <= 0 = 1.
    SoSFFloat lightIntensity;
    /// Point-light range in world units (intensity halves over it);
    /// <= 0 = automatic (from the body bounds).
    SoSFFloat lightRange;
    /// The light casts shadows (a cached shadow-map tile rendered by
    /// the engine; off by default).
    SoSFBool lightShadow;
    /// Extended (omnidirectional) light shadow: six cube-face tiles
    /// instead of the single downward cone (off by default -- costs up
    /// to six cached tiles per light).
    SoSFBool lightShadowExtended;

    /// Hands the per-face factor arrays to SoFCPbrElement. Everything
    /// else on this node is read by the render cache's post callback
    /// straight off the fields; the factor arrays need state because
    /// their consumer is the shape traversal further down.
    void callback(SoCallbackAction *action) override;
    void doAction(SoAction *action) override;

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
        /// One entry of the per-face texture PALETTE: the image the
        /// faces whose SoFCRenderMaterial::faceTextureIndices names
        /// this node's `layer` are painted with. Several such nodes
        /// together make the palette, and the backend uploads them as
        /// the layers of a single array texture -- which is what lets
        /// one draw put different images on different faces.
        FACE,
    };
    enum Wrap {
        REPEAT,
        CLAMP,
    };

    SoSFEnum slot;    ///< which material map this image feeds
    SoSFImage image;  ///< the map pixels (RGB8/RGBA8, bottom-up)
    SoSFEnum wrapS;
    SoSFEnum wrapT;
    /// FACE slot only: which layer of the per-face texture palette this
    /// image occupies. 1 and up -- layer 0 is the untextured face, so a
    /// node claiming it states nothing and is ignored.
    SoSFInt32 layer;

protected:
    ~SoFCRenderTexture() override = default;
};

} // namespace Gui

#endif // GUI_SOFCRENDERMATERIAL_H
// vim: noai:ts=4:sw=4
