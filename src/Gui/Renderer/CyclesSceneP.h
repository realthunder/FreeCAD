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
#include <unordered_map>
#include <vector>

#include "CyclesRenderer.h"

namespace ccl {
class Scene;
class Shader;
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
    };
    Surface resolveSurface(const Material &material,
                           const PBRConfig &pbr,
                           const uint8_t *vertexColor,
                           const uint8_t *materialStream) const;

    /// The shader of a uniformly-coloured draw, keyed on everything but
    /// the base colour and alpha (those ride the object).
    ccl::Shader *uniformShader(const Surface &surface);
    /// The one shader of every per-vertex-attribute draw.
    ccl::Shader *attributeShader();

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
                       Spare &spare,
                       RenderReport &report,
                       bool &changed);

    ccl::Scene *scene;
    bool managed;
    std::unordered_map<std::string, ccl::Shader *> shaders;
    std::unordered_map<std::string, MeshEntry> meshes;
    std::vector<Instance> instances;
    ccl::Shader *attrShader = nullptr;

    bool cameraStated = false;
    CameraInput lastCamera;
    bool worldStated = false;
    PBRConfig worldPbr;
    OutputConfig worldOutput;
    bool lightStated = false;
    LightConfig lastLight;
    float lightMin[3] = {0.0f, 0.0f, 0.0f};
    float lightMax[3] = {-1.0f, -1.0f, -1.0f};
    ccl::Object *lightObject = nullptr;
    ccl::Light *lightNode = nullptr;
};

}  // namespace Render::Cycles

#endif  // RENDER_CYCLES_SCENE_P_H
