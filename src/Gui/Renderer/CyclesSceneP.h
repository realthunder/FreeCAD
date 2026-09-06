/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef RENDER_CYCLES_SCENE_P_H
#define RENDER_CYCLES_SCENE_P_H

// Internal to the FreeCADRendererCycles object library: the one header
// that names Cycles types on the FreeCAD side. Everything that includes
// it compiles on cycles_embed's terms.

#include <string>
#include <set>
#include <unordered_map>
#include <vector>

#include "CyclesRenderer.h"

namespace ccl {
class Scene;
class Shader;
class ShaderGraph;
class ShaderOutput;
class Mesh;
class Object;
class Light;
class SessionParams;
}  // namespace ccl

namespace Render::Cycles {

/// Process-wide engine setup, once (the data root the GPU devices
/// compile their kernels from). Every entry point calls it first.
void initEngine();

/// The session parameters every render here starts from: the named
/// device, denoising on the same device (a debug build asserts when
/// that is left unset), one tile. The caller sets what differs
/// between an offline render and the viewport. False with \a error
/// set when the device type is unknown or absent.
bool makeSessionParams(const std::string &deviceType,
                       int samples,
                       ccl::SessionParams &params,
                       std::string &error);

/// The translation of a SceneInput into a Cycles scene
/// (docs/CyclesIntegration.md sec 6): the backend-neutral draw list
/// becomes Mesh + Object, the material fields become Principled BSDF
/// shaders, the environment becomes the world texture, the light
/// config a sun or spot, the camera matrices the camera. Reads no Gui
/// type (sec 7).
///
/// One instance per Cycles scene, for its whole life. The first
/// translate() fills a fresh scene; every later one RESTATES it in
/// place (docs/CyclesIntegration.md sec 5.10): the draws are
/// reconciled against the live Mesh and Object nodes -- an instance
/// that is still there keeps its object (its transform and colour
/// restated through the sockets, which tag nothing when equal), one
/// that vanished is deleted, a mesh nobody references any more goes
/// with it -- and the world and the light are redone only when their
/// configs differ. That is the hydra delegate's pattern, and it is
/// what makes an edit under a running session cost a reset instead
/// of a device re-init.
///
/// The maps make instances share: a draw list where a hundred bolts
/// reference one MeshData produces one ccl::Mesh and a hundred
/// ccl::Objects, and every draw with the same resolved surface shares
/// one ccl::Shader. Colour lives on the OBJECT (ObjectInfo.Color/Alpha
/// in the shader graph), which is what lets colour variants of one
/// mesh share the mesh: the same trick Blender uses for its object
/// colour, and the same split the TShape render cache already made on
/// the way in.
class SceneTranslator
{
public:
    SceneTranslator(ccl::Scene *scene, bool colorManaged);

    /// Translate everything: geometry, materials, world, light, camera.
    /// Fills a fresh scene, or restates a translated one in place.
    /// Returns true when anything in the scene changed (a session
    /// running on it owes a reset then, and only then). With a session
    /// running the caller holds the scene's mutex.
    bool translate(const SceneInput &input, RenderReport &report);
    /// Restate only the camera (the viewport's per-move update). True
    /// when it differed from the camera last stated. The caller holds
    /// the scene's mutex when a session is running.
    bool translateCamera(const CameraInput &camera);
    /// Re-run the world statement after translateCamera invalidated it
    /// (the background graph carries an orthographic-only camera-ray
    /// fan, so a projection change rebuilds it). A no-op unless the
    /// world was stated before. Same locking rule as translateCamera.
    void refreshWorld();

    bool colorManaged() const
    {
        return managed;
    }

private:
    bool translateWorld(const PBRConfig &pbr, const OutputConfig &output);
    bool translateLight(const LightConfig &light, const float sceneMin[3], const float sceneMax[3]);

    /// A surface as Cycles wants it, resolved from a Material (and, per
    /// vertex, from the colour and material streams) with the same
    /// arithmetic the bgfx path does in BGFXViewSubmit and
    /// fc_mesh_lighting.sh: the Khronos spec-gloss solve where nothing
    /// states a metalness, the shininess-to-roughness mapping the frame
    /// selects.
    struct Surface {
        float base[3] = {0.8f, 0.8f, 0.8f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float alpha = 1.0f;
        float emissive[3] = {0.0f, 0.0f, 0.0f};
        float emissiveStrength = 0.0f;  ///< 0 = emission off
        bool unlit = false;             ///< emission only, no BSDF
        bool glass = false;
        float ior = 1.5f;
        float glassRoughness = 0.0f;
        /// Absorption density per scene unit, the Beer-Lambert sigma
        /// before the tint weighting (sigma = density * (1 - base),
        /// which is what fs_fc_glass.sc and Cycles' absorption volume
        /// both compute). 0 = no absorption; resolveSurface leaves an
        /// automatic (<= 0) density at 0 and translateDraw resolves it
        /// from the draw's bounds.
        float glassDensity = 0.0f;
    };
    /// The texture maps of a draw as the graph samples them
    /// (docs/CyclesIntegration.md sec 6.5): the unit-0 picture with its
    /// GL texture environment, the bump or normal map, the emissive
    /// and metallic-roughness maps. Every one is sampled through the
    /// mesh's own texture coordinates, so a mesh without them carries
    /// none -- the raster path's rule (BGFXViewSubmit's `mapped`).
    struct Maps {
        std::shared_ptr<const TextureImage> base;
        std::shared_ptr<const TextureImage> bump;
        std::shared_ptr<const TextureImage> emissive;
        std::shared_ptr<const TextureImage> metalrough;
        uint8_t model = 0;         ///< TextureImage::Model of the base picture
        bool alphaSource = false;  ///< the base format carries alpha (Replace keeps it)
        float blendColor[3] = {0.0f, 0.0f, 0.0f};  ///< the Blend model's colour, decoded
        /// Millimetres of object space per texture-coordinate unit over
        /// the draw's range, which turns the raster path's per-UV bump
        /// slope into the per-millimetre distance Cycles' bump node
        /// wants. 0 = not measured (no grayscale bump map).
        float uvScale = 0.0f;
        float bumpScale = 1.0f;  ///< BumpConfig::scale
        bool any() const
        {
            return base || bump || emissive || metalrough;
        }
        /// The part of a shader key this contributes ("" without maps).
        std::string key() const;
    };
    /// The maps a draw samples: what its material carries, when its
    /// mesh has coordinates to sample them with.
    Maps resolveMaps(const Material &m,
                     const MeshData &mesh,
                     const BumpConfig &bump,
                     int start,
                     int count) const;
    /// The sockets the maps rewrite on their way from the surface's own
    /// values to the BSDF. Links, every one of them, so a map multiplies
    /// into whatever fed the socket before it; a null one is the BSDF's
    /// constant (or, for the normal, the geometry's) until a map needs
    /// it, when applyMaps makes the constant a node.
    struct SurfaceLinks {
        ccl::ShaderOutput *base = nullptr;
        ccl::ShaderOutput *alpha = nullptr;
        ccl::ShaderOutput *metallic = nullptr;
        ccl::ShaderOutput *roughness = nullptr;
        ccl::ShaderOutput *emission = nullptr;
        ccl::ShaderOutput *normal = nullptr;
    };
    /// Sample  maps into  links: the base picture through its
    /// texture environment (the four GL models of fc_mesh_fs.sh), the
    /// emissive map added to the emission, the metallic-roughness map's
    /// channels multiplied into the factors, the bump or normal map
    /// into the shading normal. The constants stand in for the links
    /// that are null on entry.
    void applyMaps(ccl::ShaderGraph *graph,
                   const Maps &maps,
                   SurfaceLinks &links,
                   float metallic,
                   float roughness,
                   const float emission[3]);
    /// A machined surface finish as the graph shades it
    /// (docs/CyclesIntegration.md sec 6.6): the pattern of fc_finish.sh
    /// as a HEIGHT field over the draw's object space, laid out in the
    /// face's projection frame (or triplanarly without one) and handed
    /// to a Bump node, which differentiates it against the ray
    /// differentials exactly as the raster shader differentiates its
    /// analytic gradient against the pixel. The draw's own finish and
    /// frame, or one face's out of the palettes (sec 6.7): a palette
    /// entry resolves to one of these per shader variant.
    struct Finish {
        uint8_t pattern = 0;  ///< App::SurfaceFinish::Pattern, 0 = none
        float pitch = 0.0f;   ///< mm of object space, feature spacing
        float depth = 0.0f;   ///< mm of object space, peak to valley
        float angle = 0.0f;   ///< lay direction, radians
        SurfaceFrame frame;   ///< Unframed = triplanar
        bool any() const
        {
            return pattern != 0;
        }
        /// The part of a shader key this contributes ("" without one).
        std::string key() const;
    };
    /// The finish a draw shades: the material's scalars when they name
    /// a pattern this translation knows with a positive pitch and depth
    /// (the bgfx path's own gate), in the draw's frame.
    Finish resolveFinish(const Material &m) const;
    /// The same gate over one palette entry's numbers (the angle in
    /// degrees, as the palette and the material both state it), laid
    /// out in \a frame.
    static Finish resolveFinish(uint8_t pattern,
                                float pitch,
                                float depth,
                                float angleDeg,
                                const SurfaceFrame &frame);
    /// Perturb links.normal by the finish's height field: the pattern
    /// (groove profile, brushed and blasted noise) sampled from baked
    /// periodic tables, the projection frames built in-graph.
    void applyFinish(ccl::ShaderGraph *graph, const Finish &finish, SurfaceLinks &links);
    /// A projection frame's axes the way fc_finish.sh canonicalizes
    /// them: the axis normalized, xdir made perpendicular to it (or
    /// replaced when degenerate), ydir their cross product.
    static void canonicalFrame(const SurfaceFrame &frame,
                               float axis[3],
                               float xdir[3],
                               float ydir[3]);

    /// The face's own image (docs/CyclesIntegration.md sec 6.7): one
    /// layer of the draw's per-face texture palette, laid out in the
    /// face's projection frame at `scale` millimetres per tile -- a
    /// planar face in the plane's axes, a turned one unwrapped about
    /// its axis, an unframed one off its dominant object axis -- or
    /// over the mesh's own coordinates when the draw states no tile
    /// size, as fc_mesh_fs.sh has it.
    struct FaceImage {
        std::shared_ptr<const TextureImage> image;
        float scale = 0.0f;   ///< Material::facetexscale; <= 0 = the mesh's UVs
        SurfaceFrame frame;   ///< Unframed = the dominant-axis projection
        bool meshUV = false;  ///< the mesh has coordinates to sample with
        bool any() const
        {
            return image != nullptr;
        }
        /// The part of a shader key this contributes ("" without one).
        std::string key() const;
    };
    /// The image of palette layer \a layer (0 = none; past the palette
    /// = its last entry, the raster path's clamp) in \a frame.
    FaceImage resolveFaceImage(const Material &m,
                               const MeshData &mesh,
                               int layer,
                               const SurfaceFrame &frame) const;
    /// Modulate links.base and links.alpha by the face's image, sampled
    /// where its frame or the mesh's coordinates put it.
    void applyFaceImage(ccl::ShaderGraph *graph, const FaceImage &face, SurfaceLinks &links);
    Surface resolveSurface(const Material &material,
                           const PBRConfig &pbr,
                           const uint8_t *vertexColor,
                           const uint8_t *materialStream) const;
    /// The automatic glass density of a draw that states none: about
    /// one optical depth across the body's bounds diagonal before the
    /// tint weighting, the same rule as the bgfx glass pass
    /// (BGFXViewEffects.cpp). 0 when the bounds are empty.
    static float autoGlassDensity(const DrawCall &draw);

    /// The draw's world-space section planes, carried into the SHADER
    /// because Cycles has no clip-plane state: the graph itself makes
    /// a clipped shading point transparent
    /// (docs/CyclesIntegration.md sec 6.3).
    struct Clip {
        uint8_t num = 0;
        bool concave = false;
        float planes[Material::MaxClipPlanes][4] = {};
        /// The part of a shader key this contributes ("" when there
        /// are no planes, so an unclipped scene keys as it always did).
        std::string key() const;
    };

    /// The shader of a uniformly-coloured draw, keyed on everything but
    /// the base colour and alpha (those ride the object).
    ccl::Shader *uniformShader(const Surface &surface,
                               const Clip &clip,
                               const Maps &maps,
                               const Finish &finish,
                               const FaceImage &face);
    /// The shader of a draw whose material is a MaterialX document
    /// (docs/CyclesIntegration.md sec 8 item 15 phase B): the document
    /// IS the material, so none of the draw's own surface, maps,
    /// finish or face palette applies -- only the section clip, which
    /// is a property of the scene and not of the material. Null when
    /// the document cannot be interpreted, and the caller then renders
    /// the draw's stock material.
    ccl::Shader *materialXShader(const UserShader &shader, const Clip &clip);
    /// Documents this translator has already had its say about, so a
    /// scene of a thousand draws sharing one material reports its
    /// warnings -- or its refusal -- once and not once per draw.
    std::set<std::string> materialXReported;
    /// The notes already made, keyed by DOCUMENT and message rather
    /// than by surface: a note about the model a document is authored
    /// against is one note, whatever the number of surfaces wearing it.
    std::set<std::string> materialXNoted;
    /// And of those, the ones that failed: the negative cache that
    /// stops the next restate importing the data library all over
    /// again only to fail the same way.
    std::set<std::string> materialXFailed;

    /// The shader of a per-vertex-attribute draw: the surface rides the
    /// mesh, so a scene of vertex-painted meshes with no section, no
    /// maps and no finish shares ONE of these; what does not ride a
    /// vertex (the clip, the maps, a finish, a face image) keys it.
    ccl::Shader *attributeShader(const Clip &clip,
                                 const Maps &maps,
                                 const Finish &finish,
                                 const FaceImage &face);
    /// Connect \a closure to the graph's surface output, through the
    /// clip test when there is one.
    void connectSurface(ccl::ShaderGraph *graph, ccl::ShaderOutput *closure, const Clip &clip);
    /// The surface of debug view 2 (SceneInput::debugView): the shading
    /// normal -- \a normal, or the geometry's when null -- emitted in
    /// GL eye space as n * 0.5 + 0.5, which is what fs_fc_debug.sc
    /// shows for the same mode.
    void debugSurface(ccl::ShaderGraph *graph, ccl::ShaderOutput *normal, const Clip &clip);

    /// A translated mesh and what it cost, keyed by the draw's mesh
    /// identity (the cache contract cacheId + generation, the index
    /// range, the shader).
    struct MeshEntry {
        ccl::Mesh *mesh = nullptr;
        long triangles = 0;
    };
    /// One placed draw: the object, and the key it is matched under on
    /// the next restate (its mesh key plus the draw's objectKey).
    struct Instance {
        std::string key;
        ccl::Object *object = nullptr;
        ccl::Mesh *mesh = nullptr;
    };
    /// The objects of the previous restate not yet claimed by this one,
    /// per instance key; what is left when the draws are done is
    /// deleted.
    using Spare = std::unordered_map<std::string, std::vector<ccl::Object *>>;

    /// Place one draw: reuse a spare object of its key or create one.
    /// Returns false for a draw the translation has no use for. Sets
    ///  changed when a node was created or restated.
    bool translateDraw(const DrawCall &draw,
                       const PBRConfig &pbr,
                       const BumpConfig &bump,
                       const SectionConfig &section,
                       Spare &spare,
                       RenderReport &report,
                       bool &changed);

    /// The cut faces of a clipped solid draw: one filled cross-section
    /// per plane, as real geometry, because a path tracer has no
    /// stencil to fake one with (docs/CyclesIntegration.md sec 6.4).
    /// Placed like any other instance, so the restate reconciles them
    /// with everything else.
    void translateCaps(const DrawCall &draw,
                       const SectionConfig &section,
                       const Surface &uniform,
                       int start,
                       int count,
                       Spare &spare,
                       RenderReport &report,
                       bool &changed);

    /// A shader node cannot be deleted (Cycles does not support it), so
    /// a key nothing shades under any more -- a dragged section plane
    /// keys every shader it clips by its coefficients, so a drag mints
    /// keys without bound -- is RECYCLED instead: releaseUnusedShaders
    /// drops the orphan's graph (freeing its image handles) and parks
    /// the node as a spare, and the next new key re-graphs a spare
    /// before it creates a node.
    struct ShaderEntry {
        ccl::Shader *shader = nullptr;
        int images = 0;  ///< image texture nodes its graph carries
    };
    /// A spare shader node re-graphed, or a fresh one.
    ccl::Shader *acquireShader();
    /// Park every shader no live mesh references; true when one was.
    bool releaseUnusedShaders();

    ccl::Scene *scene;
    bool managed;
    int debugView = 0;  ///< SceneInput::debugView of the last translate
    std::unordered_map<std::string, ShaderEntry> shaders;
    std::vector<ccl::Shader *> spareShaders;
    int imageNodes = 0;  ///< image texture nodes in the live shaders
    std::unordered_map<std::string, MeshEntry> meshes;
    std::vector<Instance> instances;

    bool cameraStated = false;
    CameraInput lastCamera;
    /// Whether the camera last stated was orthographic: the world's
    /// background graph shapes its camera-ray fan by it.
    bool cameraOrtho = false;
    bool worldStated = false;
    /// True once translateWorld ever ran (worldStated goes false again
    /// when a projection change invalidates the graph -- refreshWorld
    /// only rebuilds a world that existed).
    bool worldEverStated = false;
    /// The projection kind the stated background graph was built for.
    bool worldOrtho = false;
    PBRConfig worldPbr;
    OutputConfig worldOutput;
    bool lightStated = false;
    LightConfig lastLight;
    float lightMin[3] = {0.0f, 0.0f, 0.0f};
    float lightMax[3] = {-1.0f, -1.0f, -1.0f};
    ccl::Object *lightObject = nullptr;
    ccl::Light *lightNode = nullptr;
    /// The environment as a sampled light (translateWorld), made once
    /// and never restated: it carries no state of its own, the world
    /// shader it samples is the scene's default_background.
    ccl::Light *envLight = nullptr;
    ccl::Object *envLightObject = nullptr;
};

}  // namespace Render::Cycles

#endif  // RENDER_CYCLES_SCENE_P_H
