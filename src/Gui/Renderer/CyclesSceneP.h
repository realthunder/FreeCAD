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

#include "CyclesRenderer.h"

namespace ccl {
class Scene;
class Shader;
class Mesh;
}  // namespace ccl

namespace Render::Cycles {

/// The translation of a SceneInput into a Cycles scene
/// (docs/CyclesIntegration.md sec 6): the backend-neutral draw list
/// becomes Mesh + Object, the material fields become Principled BSDF
/// shaders, the environment becomes the world texture, the light
/// config a sun or spot, the camera matrices the camera. Reads no Gui
/// type (sec 7).
///
/// One instance per Cycles scene. The maps make instances share: a
/// draw list where a hundred bolts reference one MeshData produces one
/// ccl::Mesh and a hundred ccl::Objects, and every draw with the same
/// resolved surface shares one ccl::Shader. Colour lives on the OBJECT
/// (ObjectInfo.Color/Alpha in the shader graph), which is what lets
/// colour variants of one mesh share the mesh: the same trick Blender
/// uses for its object colour, and the same split the TShape render
/// cache already made on the way in.
class SceneTranslator
{
public:
    SceneTranslator(ccl::Scene *scene, bool colorManaged);

    /// Translate everything: geometry, materials, world, light, camera.
    /// The scene must be fresh (no prior translation).
    void translate(const SceneInput &input, RenderReport &report);

private:
    void translateCamera(const CameraInput &camera);
    void translateWorld(const PBRConfig &pbr, const OutputConfig &output);
    void translateLight(const LightConfig &light, const float sceneMin[3], const float sceneMax[3]);
    bool translateDraw(const DrawCall &draw, const PBRConfig &pbr, RenderReport &report);

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

    ccl::Scene *scene;
    bool managed;
    std::unordered_map<std::string, ccl::Shader *> shaders;
    std::unordered_map<std::string, ccl::Mesh *> meshes;
    ccl::Shader *attrShader = nullptr;
};

}  // namespace Render::Cycles

#endif  // RENDER_CYCLES_SCENE_P_H
