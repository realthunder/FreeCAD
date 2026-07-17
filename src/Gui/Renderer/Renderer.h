/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#ifndef RENDERER_RENDERER_H
#define RENDERER_RENDERER_H

#include <FCConfig.h>

#ifdef FreeCADRenderer_STATIC
#   define RendererExport
#elif defined(FreeCADRenderer_EXPORTS)
#   define RendererExport   FREECAD_DECL_EXPORT
#else
#   define RendererExport   FREECAD_DECL_IMPORT
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class QOpenGLWidget;
class QColor;

namespace Render {

class RenderLib;

/// CPU-side snapshot of one geometry cache (SoFCVertexCache on the Gui side).
/// All array pointers stay valid for as long as `owner` is held. Backends key
/// GPU uploads on `cacheId`: the same id always refers to identical content.
struct MeshData {
    uint64_t cacheId = 0;
    std::shared_ptr<const void> owner;

    int numVertices = 0;
    const float *positions = nullptr;   ///< xyz per vertex, never null
    const float *normals = nullptr;     ///< xyz per vertex, may be null
    const uint8_t *colors = nullptr;    ///< rgba8 per vertex, may be null

    const int32_t *triangleIndices = nullptr;
    int numTriangleIndices = 0;
    const int32_t *lineIndices = nullptr;   ///< GL_LINES style vertex pairs
    int numLineIndices = 0;
    const int32_t *pointIndices = nullptr;
    int numPointIndices = 0;

    /// Line indices with seam lines (e.g. the closing edge of a cylinder
    /// face) filtered out; null when the cache has no seams. Used by the
    /// hidden-line draw style's hideSeam option.
    const int32_t *noSeamLineIndices = nullptr;
    int numNoSeamLineIndices = 0;

    /// Per-face-part {start, count} ranges into triangleIndices, filled
    /// for hidden-line outline materials: the GL renderer outlines
    /// geometry face part by face part under clip planes and in the
    /// perFaceOutline hidden-line mode. Empty when the cache carries no
    /// face part table (then those modes outline nothing, like GL).
    std::vector<std::pair<int, int>> triangleParts;
    /// The subset of triangleParts belonging to non-flat (curved) faces;
    /// the perFaceOutline mode outlines only these when combined with
    /// sceneOutline or a zero outline width (GL: getNonFlatParts()).
    std::vector<std::pair<int, int>> nonFlatParts;

    /// Solid-geometry knowledge for section capping (SoFCShapeInfo):
    /// 0 = none, 1 = some face parts belong to solids (solidParts holds
    /// their {start, count} ranges into triangleIndices), 2 = the whole
    /// triangle set is solid. Filled for clipped triangle materials.
    int hasSolid = 0;
    std::vector<std::pair<int, int>> solidParts;

    bool hasTransparency = false;   ///< some per-vertex colors are transparent
    bool hasOpaqueParts = false;    ///< some per-vertex colors are opaque

    /// Texture coordinates, xyzw per vertex (Coin's SbVec4f layout; the
    /// default texgen and 2D texcoord nodes produce (s, t, 0, 1)). Null
    /// when the cache was built without an active texture.
    const float *texCoords = nullptr;
};

/// CPU-side snapshot of a texture image applied to triangle draws
/// (unit-0 SoTexture2 on the Gui side). Pixels are copied at translate
/// time; backends key GPU uploads on `textureId`: the same id always
/// refers to identical content.
struct TextureImage {
    uint64_t textureId = 0;
    int width = 0;
    int height = 0;
    /// 1 = luminance, 2 = luminance+alpha, 3 = rgb, 4 = rgba; rows are
    /// tightly packed, bottom-up like GL.
    int numComponents = 0;
    std::vector<uint8_t> pixels;

    enum Wrap : uint8_t { Repeat, Clamp };
    uint8_t wrapS = Repeat;
    uint8_t wrapT = Repeat;

    /// GL texture environment (SoTexture2::model).
    enum Model : uint8_t { Modulate, Decal, Blend, Replace };
    uint8_t model = Modulate;
    /// rgb used by the Blend model, packed 0xRRGGBBAA.
    uint32_t blendColor = 0;
};

/// Window background drawn behind the scene, mirroring the Coin-side
/// gradient background node. Backends that consume the scene must draw it
/// themselves so transparent geometry blends against the real background
/// (and the Coin node is skipped for backend-rendered frames). Colors are
/// packed 0xRRGGBBAA.
struct Background {
    enum Type : uint8_t { Flat, LinearGradient, RadialGradient };
    uint8_t type = Flat;
    uint32_t fromColor = 0;  ///< flat color / gradient top / radial center
    uint32_t toColor = 0;    ///< gradient bottom / radial edge
    uint32_t midColor = 0;   ///< optional intermediate color
    bool hasMid = false;
};

/// Per-frame hidden-line draw style configuration, mirroring the Coin-side
/// SoFCDisplayModeElement::HiddenLineConfig resolved at render time. Only
/// meaningful while `show` is true; scene materials carry the matching
/// Material::outline flag.
struct HiddenLineConfig {
    bool show = false;           ///< hidden-line draw style active
    bool hideFace = false;       ///< skip triangle face fills
    bool hideSeam = false;       ///< skip seam lines (MeshData no-seam set)
    bool hideVertex = false;     ///< skip point draws; outline corner caps
    bool perFaceOutline = false; ///< outline each face part separately
    bool sceneOutline = false;   ///< one silhouette around the whole scene
    float outlineWidth = 0.0f;   ///< minimum outline width in pixels
    /// ViewParams::OutlineThicken — outline width is at least
    /// linewidth * outlineThicken (GL: renderOutline ~1468).
    float outlineThicken = 4.0f;
    /// Resolved scene-outline line color (display-mode line color or
    /// ViewParams::HiddenLineColor), packed 0xRRGGBBAA.
    uint32_t lineColor = 0;

    bool operator==(const HiddenLineConfig &o) const {
        return show == o.show && hideFace == o.hideFace
            && hideSeam == o.hideSeam && hideVertex == o.hideVertex
            && perFaceOutline == o.perFaceOutline
            && sceneOutline == o.sceneOutline
            && outlineWidth == o.outlineWidth
            && outlineThicken == o.outlineThicken
            && lineColor == o.lineColor;
    }
    bool operator!=(const HiddenLineConfig &o) const { return !(*this == o); }
};

/// Per-frame section (clip plane) fill configuration, mirroring the
/// ViewParams the GL renderer reads in renderSection/_renderSection.
/// Resolved by the bridge each render like HiddenLineConfig.
struct SectionConfig {
    bool fill = true;        ///< cap the cross section of clipped solids
    bool fillInvert = true;  ///< invert the cap fill color
    bool fillGroup = false;  ///< cap same-material solids together
    bool concave = false;    ///< SectionConcave (union of half-spaces)
    bool hatchEnable = true; ///< modulate the cap with the hatch texture
    float hatchScale = 1.0f; ///< hatch texture scale

    bool operator==(const SectionConfig &o) const {
        return fill == o.fill && fillInvert == o.fillInvert
            && fillGroup == o.fillGroup && concave == o.concave
            && hatchEnable == o.hatchEnable && hatchScale == o.hatchScale;
    }
    bool operator!=(const SectionConfig &o) const { return !(*this == o); }
};

/// Per-frame screen-space ambient occlusion configuration (there is no
/// GL-renderer counterpart; the effect exists only in external backends).
/// Resolved by the bridge each render like SectionConfig.
struct AOConfig {
    bool enabled = false;    ///< ambient occlusion pass active
    /// Sample radius in world units; 0 = automatic (a fraction of the
    /// scene bounding-sphere size, resolved by the backend).
    float radius = 0.0f;
    float intensity = 1.0f;  ///< occlusion darkening strength

    bool operator==(const AOConfig &o) const {
        return enabled == o.enabled && radius == o.radius
            && intensity == o.intensity;
    }
    bool operator!=(const AOConfig &o) const { return !(*this == o); }
};

/// Per-frame bump/normal mapping configuration (like AOConfig there is
/// no GL-renderer counterpart; the GL renderer never draws scene bump
/// maps). Applies to triangle draws carrying a Material::bumpmap.
struct BumpConfig {
    /// Strength multiplier: scales the slope of normal maps and the
    /// height amplitude of grayscale bump maps.
    float scale = 1.0f;
    /// Parallax-occlusion map grayscale height maps (UV offset along
    /// the view ray) instead of plain height-to-normal shading.
    bool parallax = true;

    bool operator==(const BumpConfig &o) const {
        return scale == o.scale && parallax == o.parallax;
    }
    bool operator!=(const BumpConfig &o) const { return !(*this == o); }
};

/// Per-frame scene light, resolved from the traversal state's light
/// element. Only the Shadow draw style places a shadow light into the
/// scene graph (the viewer headlight is filtered out); while valid,
/// backends replace their headlight with this light and render a shadow
/// map from it, honoring each draw's Material::shadowstyle.
struct LightConfig {
    bool valid = false;
    bool spot = false;       ///< spot light; directional otherwise
    float direction[3] = {0.0f, 0.0f, -1.0f};  ///< world, normalized
    float position[3] = {0.0f, 0.0f, 0.0f};    ///< world, spot only
    /// Spot cone half angle in radians (SoSpotLight::cutOffAngle) and
    /// intensity falloff inside the cone (SoSpotLight::dropOffRate,
    /// 0..1 mapping to a GL spot exponent of dropOffRate * 128).
    float cutOffAngle = 0.78539816f;
    float dropOffRate = 0.0f;
    uint32_t color = 0xffffffff;               ///< packed 0xRRGGBBAA
    float intensity = 1.0f;

    /// Shadow border smoothing (ShadowSmoothBorder / the Shadow draw
    /// style's SmoothBorder property, 0..100): scales the gaussian blur
    /// of the variance shadow map moments; 0 = no blur.
    float smoothBorder = 0.0f;

    /// Shadow ground plane (ShadowShowGround*): the Coin-side ground
    /// lives outside the captured scene graph, so backends draw their
    /// own — a receiving quad under the scene bounds.
    bool ground = false;
    float groundScale = 2.0f;    ///< times the scene extent
    uint32_t groundColor = 0x7d7d7dff;
    /// Ground texture (ShadowGroundTexture), modulated by the ground
    /// color and tiled every groundTextureSize world units
    /// (<= 0 = stretched once over the quad); null = plain color.
    std::shared_ptr<const TextureImage> groundTexture;
    float groundTextureSize = 100.0f;

    bool operator==(const LightConfig &o) const {
        return valid == o.valid && spot == o.spot
            && direction[0] == o.direction[0]
            && direction[1] == o.direction[1]
            && direction[2] == o.direction[2]
            && position[0] == o.position[0]
            && position[1] == o.position[1]
            && position[2] == o.position[2]
            && color == o.color && intensity == o.intensity
            && cutOffAngle == o.cutOffAngle
            && dropOffRate == o.dropOffRate
            && smoothBorder == o.smoothBorder
            && ground == o.ground && groundScale == o.groundScale
            && groundColor == o.groundColor
            && groundTexture == o.groundTexture
            && groundTextureSize == o.groundTextureSize;
    }
    bool operator!=(const LightConfig &o) const { return !(*this == o); }
};

/// Per-frame volumetric lighting (light shaft) configuration (like
/// AOConfig there is no GL-renderer counterpart). While enabled and a
/// scene light with a shadow map is active (LightConfig::valid), the
/// backend raymarches the shadow map through a homogeneous scattering
/// medium and composites the inscattered light over the opaque scene.
struct VolumetricConfig {
    bool enabled = false;
    float intensity = 1.0f;  ///< inscattered light brightness
    /// Medium density (extinction coefficient) in inverse world units;
    /// 0 = automatic (a fraction of the scene size, resolved by the
    /// backend).
    float density = 0.0f;

    bool operator==(const VolumetricConfig &o) const {
        return enabled == o.enabled && intensity == o.intensity
            && density == o.density;
    }
    bool operator!=(const VolumetricConfig &o) const { return !(*this == o); }
};

/// Per-frame physically based shading configuration (like AOConfig there
/// is no GL-renderer counterpart). While enabled, lit triangle surfaces
/// use a metallic/roughness BRDF with image based lighting from a
/// backend-built environment instead of the default headlight shading.
struct PBRConfig {
    bool enabled = false;
    float metallic = 0.0f;      ///< surface metalness, 0..1
    /// Surface roughness, 0..1; 0 = automatic (derived per draw from the
    /// material's shininess).
    float roughness = 0.0f;
    float envIntensity = 1.0f;  ///< environment lighting brightness

    bool operator==(const PBRConfig &o) const {
        return enabled == o.enabled && metallic == o.metallic
            && roughness == o.roughness && envIntensity == o.envIntensity;
    }
    bool operator!=(const PBRConfig &o) const { return !(*this == o); }
};

/// Flattened per-draw render state, translated from the Coin-side material
/// (SoFCRenderCache::Material). Colors are packed 0xRRGGBBAA.
struct Material {
    enum Type : uint8_t { Triangle, Line, Point };
    enum DepthFunc : uint8_t {
        Never, Always, Less, LEqual, Equal, GEqual, Greater, NotEqual
    };

    uint8_t type = Triangle;
    uint8_t depthfunc = LEqual;
    bool depthtest = true;
    bool depthwrite = true;
    bool pervertexcolor = false;
    bool lighting = true;        ///< false = flat base color (no light model)
    bool twoside = false;
    bool culling = false;
    bool ccw = true;             ///< front face vertex ordering
    bool transparent = false;    ///< uniform-color / texture transparency
    bool ontop = false;          ///< render after (over) the normal scene
    bool polygonoffset = false;  ///< glPolygonOffset on filled triangles
    uint32_t diffuse = 0xCCCCCCFF;
    uint32_t emissive = 0;
    uint32_t specular = 0;
    uint32_t ambient = 0;
    /// Edge color override of a triangle material (hidden-line outline
    /// color falls back to diffuse when 0, like the GL renderer).
    uint32_t linecolor = 0;
    float shininess = 0.0f;
    float linewidth = 1.0f;
    float pointsize = 1.0f;
    /// Line stipple, glLineStipple encoding: low 16 bits = pattern (LSB
    /// drawn first), bits 16+ = pixel repeat factor (0/1 = one pixel per
    /// bit). 0xffff in the low bits = solid.
    uint32_t linepattern = 0xffff;
    /// Pattern of the depth-occluded (dimmed) pass of on-top lines; GL
    /// substitutes ViewParams::SelectionLinePattern there when the
    /// material has no pattern of its own.
    uint32_t hiddenlinepattern = 0xffff;
    /// glPolygonOffset(factor, units); positive pushes away from the viewer
    float polygonoffsetfactor = 0.0f;
    float polygonoffsetunits = 0.0f;
    /// Alpha used to dim the depth-occluded part of on-top lines/points
    /// (ViewParams::TransparencyOnTop); 1 = no dimming.
    float hiddenlinealpha = 1.0f;

    /// Stencil face outline of partial (per-face) triangle draws in the
    /// selection/highlight feeds (GL: RenderPassSelectionOutline): the
    /// face is drawn into the stencil buffer, then its triangle edges
    /// redraw as thick lines where the stencil does not match, leaving
    /// the boundary. The outline color is the material's emissive.
    bool faceoutline = false;   ///< outline partial triangle draws
    bool outlineonly = false;   ///< and skip their face fill
    float outlinewidth = 1.0f;  ///< outline width in pixels

    /// Hidden-line draw style material (SoFCRenderCache::Material::outline):
    /// whole-cache triangle draws get a stencil outline, and the active
    /// HiddenLineConfig's hideFace/hideSeam/hideVertex rules apply.
    bool outline = false;

    /// Shadow participation of a triangle draw, the Coin
    /// SoShadowStyleElement bitmask: 1 = casts shadows, 2 = receives
    /// (is shadowed). Only meaningful while a scene light is fed
    /// (LightConfig::valid).
    uint8_t shadowstyle = 3;

    /// Coin shape hints declare the geometry a closed solid
    /// (SoShapeHintsElement::SOLID); together with MeshData::hasSolid this
    /// gates the stencil section cap of clipped draws.
    bool solidshape = false;

    /// Per-object PBR parameters of a triangle draw (SoFCRenderMaterial,
    /// typically fed from ViewProvider Render_* properties); < 0 = unset,
    /// the frame's PBRConfig values apply. Only used while the PBR
    /// shading path is active.
    float metallic = -1.0f;
    float roughness = -1.0f;

    /// Water body flag of a triangle draw (SoFCRenderMaterial, typically
    /// fed from a ViewProvider Render_Water property): while the
    /// volumetric lighting pass is active, the draw's closed volume
    /// becomes a scattering medium tinted by the diffuse color instead
    /// of an ordinary surface — it neither ends volumetric rays nor
    /// casts shadows, and its front/back depths bound the underwater
    /// stretch of each view ray. waterdensity is the extinction density
    /// in inverse world units, <= 0 = automatic (from the draw bounds).
    bool water = false;
    float waterdensity = 0.0f;

    /// Texture of a triangle draw (unit 0 only; GL applies further units
    /// on top, a known deviation) with its texture matrix, applied to
    /// MeshData::texCoords. Only sampled when the mesh carries texture
    /// coordinates.
    std::shared_ptr<const TextureImage> texture;
    float texmatrix[16];        ///< GL-layout, valid when !texidentity
    bool texidentity = true;

    /// Bump map of a triangle draw (unit-0 SoBumpMap): 1/2-component
    /// images perturb as grayscale height maps (parallax-occlusion
    /// mapped when BumpConfig::parallax), 3/4-component images are
    /// tangent-space normal maps (Coin's convention). The tangent frame
    /// comes from screen-space derivatives, so any UV source works —
    /// but the mesh must carry MeshData::texCoords, which only an
    /// enabled texture unit provides (pair bump-only scenes with a
    /// plain white texture). The wrap fields apply; model/blendColor
    /// are ignored.
    std::shared_ptr<const TextureImage> bumpmap;

    /// Autozoom transforms (SoAutoZoomTranslation): the draw's model
    /// matrix is rebuilt every frame by replaying these entries like the
    /// GL renderer's setupMatrix — accumulate each entry's matrix (or
    /// reset to it), then substitute the accumulated scale with
    /// scaleFactor times the per-frame world-to-screen scale fed through
    /// Renderer::setAutoZoomScale() (a scaleFactor of 0 keeps scale 1) —
    /// and DrawCall::model multiplies in last. Matrices are GL-layout
    /// like DrawCall::model.
    struct AutoZoomEntry {
        float matrix[16];
        float scaleFactor = 1.0f;
        bool identity = true;
        bool resetmatrix = false;
    };
    std::vector<AutoZoomEntry> autozoom;

    /// World-space clip plane equations (sections). A fragment survives
    /// when dot(pos, plane.xyz) + plane.w >= 0 holds for every plane, or,
    /// in concave mode, for at least one plane (GL parity: SectionConcave
    /// renders one enabled plane per pass, i.e. the union of half-spaces).
    static constexpr int MaxClipPlanes = 6;
    uint8_t numclipplanes = 0;
    bool clipconcave = false;
    float clipplanes[MaxClipPlanes][4];
};

/// One draw of (a part of) a mesh with a material and model transform.
struct DrawCall {
    Material material;
    std::shared_ptr<const MeshData> mesh;
    float model[16];        ///< GL-style layout, valid when !identity
    bool identity = true;
    /// Content hash of the scene-graph node path that produced this draw;
    /// the same object yields the same key in the scene and the
    /// selection/highlight feeds. 0 = unknown.
    uint64_t objectKey = 0;
    /// True when this draw covers the object's whole geometry of its
    /// primitive type (a whole-object on-top selection/highlight draw
    /// replaces — hides — the object's scene draws with equal objectKey).
    bool wholeObject = false;
    int partIndex = -1;     ///< -1 = whole mesh, >= 0 = single face/edge part
    /// Index range of the draw inside the index buffer selected by
    /// material.type. Resolved from partIndex by the producer; count 0
    /// means the whole buffer.
    int indexStart = 0;
    int indexCount = 0;
    float bboxMin[3] = {0.0f, 0.0f, 0.0f};  ///< world space bounds,
    float bboxMax[3] = {0.0f, 0.0f, 0.0f};  ///< empty if min > max
};

typedef std::vector<DrawCall> DrawCallList;

class RendererExport Renderer
{
public:
    virtual ~Renderer() {}
    virtual const std::string &type() const = 0;
    virtual bool render(const QColor &bg,
                        const void *viewMatrix,
                        const void *projMatrix) = 0;
    virtual bool boundBox(float &xmin, float &ymin, float &zmin,
                          float &xmax, float &ymax, float &zmax) = 0;

    /// \name Scene API
    /// Mirrors SoFCRenderer's feed. Backends that don't consume scene data
    /// keep the default no-ops and canSkipInternal() == false, so the
    /// existing GL pipeline continues to draw the scene.
    //@{
    /// Replace the whole scene. An empty list clears it.
    virtual void setScene(DrawCallList &&draws) { (void)draws; }
    /// Describe the window background for the next render(). The bg color
    /// passed to render() stays the clear-color fallback for backends that
    /// ignore this.
    virtual void setBackground(const Background &bg) { (void)bg; }
    /// Add/replace one selection identified by id (SoFCRenderer::SelIdBits).
    virtual void addSelection(int id, DrawCallList &&draws)
    { (void)id; (void)draws; }
    virtual void removeSelection(int id) { (void)id; }
    /// Set the preselection highlight geometry.
    virtual void setHighlight(DrawCallList &&draws, bool wholeOnTop)
    { (void)draws; (void)wholeOnTop; }
    virtual void clearHighlight() {}
    /// Per-frame hidden-line draw style state (resolved from the traversal
    /// state each render, like the GL renderer does).
    virtual void setHiddenLineConfig(const HiddenLineConfig &config)
    { (void)config; }
    /// Per-frame section fill (cap) configuration.
    virtual void setSectionConfig(const SectionConfig &config)
    { (void)config; }
    /// Per-frame ambient occlusion configuration.
    virtual void setAOConfig(const AOConfig &config) { (void)config; }
    /// Per-frame physically based shading configuration.
    virtual void setPBRConfig(const PBRConfig &config) { (void)config; }
    /// Per-frame bump/normal mapping configuration.
    virtual void setBumpConfig(const BumpConfig &config) { (void)config; }
    /// Per-frame scene light (Shadow draw style).
    virtual void setLightConfig(const LightConfig &config) { (void)config; }
    /// Per-frame volumetric lighting (light shaft) configuration.
    virtual void setVolumetricConfig(const VolumetricConfig &config)
    { (void)config; }
    /// Per-frame world-to-screen scale at the world origin consumed by
    /// Material::autozoom draws (Coin: SoAutoZoomTranslation's
    /// getWorldToScreenScale((0,0,0), 0.1) / (5 * viewport aspect)).
    /// Resolved from the traversal state each render like the
    /// hidden-line config, so it applies one frame late too.
    virtual void setAutoZoomScale(float scale) { (void)scale; }
    /// Section cap hatch texture pixels; \a nc-component 8-bit rows,
    /// tightly packed. Null data clears the texture. The pixels are copied.
    virtual void setHatchImage(const void *data, int nc,
                               int width, int height)
    { (void)data; (void)nc; (void)width; (void)height; }
    /// True if scene data changed after the last render() and another
    /// frame should be scheduled.
    virtual bool needsRedraw() const { return false; }
    /// True when this backend has rendered the current scene and the
    /// internal fixed-function GL pass can be skipped.
    virtual bool canSkipInternal() const { return false; }
    //@}
};

class RendererLib
{
public:
    virtual ~RendererLib() {}
    virtual const std::string &name() const = 0;
    virtual const std::vector<std::string> &types() const = 0;
    virtual std::unique_ptr<Renderer> create(
            const std::string &type, QOpenGLWidget *widget) const = 0;
};

class RendererExport RendererFactory
{
public:
    static std::vector<std::string> types();
    static std::unique_ptr<Renderer> create(const std::string &type, QOpenGLWidget *widget);
    static void registerLib(RendererLib *);
    static void setResourcePath(const std::string &path);
    static const std::string &resourcePath();
};

} // namespace Render

#endif // RENDERER_RENDERER_H
