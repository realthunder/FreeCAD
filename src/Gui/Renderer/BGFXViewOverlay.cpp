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

#include "BGFXRendererP.h"

float BGFXView::polygonOffsetBias(const Render::Material &mat)
{
    constexpr float kDepthBiasUnit = 2.0f * 16.0f / 16777216.0f;
    return mat.polygonoffset
        ? (mat.polygonoffsetfactor + mat.polygonoffsetunits)
            * kDepthBiasUnit
        : 0.0f;
}

void BGFXView::submitOutline(const Render::DrawCall &draw, uint32_t refCounter,
                   const OutlineSpec &spec)
{
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return;
    // Validate the edge passes up front so a mesh that cannot draw
    // them leaves no stray stencil marks.
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;
    gpu->geom->ensureOutline(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->triEdgeInst))
        return;
    if (!submitOutlineMark(draw, refCounter, spec.view, spec.depthTest,
                           spec.start, spec.count))
        return;
    submitOutlineEdges(draw, refCounter, spec);
}

bool BGFXView::clipActiveFor(const Render::Material &mat) const
{
    return mat.numclipplanes > 0 || reflClipActive(mat);
}

bool BGFXView::reflClipActive(const Render::Material &mat) const
{
    return reflPass && reflClip && !mat.clipconcave;
}

void BGFXView::setClipUniforms(const Render::Material &mat)
{
    const bool refl = reflClipActive(mat);
    if (mat.numclipplanes == 0 && !refl)
        return;
    int n = mat.numclipplanes;
    float planes[Render::Material::MaxClipPlanes][4];
    if (n > 0)
        std::memcpy(planes, mat.clipplanes, sizeof(float) * 4 * n);
    if (refl && n < Render::Material::MaxClipPlanes) {
        std::memcpy(planes[n], reflClipPlane, sizeof(float) * 4);
        ++n;
    }
    float clipParams[4] = {float(n),
                           mat.clipconcave ? 1.0f : 0.0f,
                           0.0f, 0.0f};
    bgfx::setUniform(u_clipParams, clipParams);
    bgfx::setUniform(u_clipPlanes, planes, uint16_t(n));
}

bool BGFXView::submitOutlineMark(const Render::DrawCall &draw,
                       uint32_t refCounter, uint16_t view,
                       bool depthTest, int start, int count)
{
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return false;
    const Render::MeshData &mesh = *draw.mesh;
    if (count <= 0) {
        start = 0;
        count = mesh.numTriangleIndices;
    }
    if (count < 3)
        return false;
    GpuMesh *gpu = getMesh(mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return false;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    uint32_t ref = ((refCounter - 1) % 255) + 1;
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bgfx::setUniform(u_matColor, zero);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    setMeshVertexBuffers(gpu, mesh);
    bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(start), uint32_t(count));
    bgfx::setState(BGFX_STATE_MSAA
        | (depthTest ? BGFX_STATE_DEPTH_TEST_LEQUAL : 0));
    bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS
        | BGFX_STENCIL_FUNC_REF(ref) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_REPLACE
        | BGFX_STENCIL_OP_PASS_Z_REPLACE);
    bgfx::submit(vid(view),
                 clipped ? m_progFlatClip : m_progFlat);
    ++drawcount;
    return true;
}

void BGFXView::submitOutlineEdges(const Render::DrawCall &draw,
                        uint32_t refCounter, const OutlineSpec &spec)
{
    if (!m_instancing || !draw.mesh || !draw.mesh->triangleIndices)
        return;
    const Render::MeshData &mesh = *draw.mesh;
    int start = spec.start;
    int count = spec.count;
    if (count <= 0) {
        start = 0;
        count = mesh.numTriangleIndices;
    }
    if (count < 3)
        return;
    GpuMesh *gpu = getMesh(mesh);
    if (!bgfx::isValid(gpu->geom->vbh))
        return;
    gpu->geom->ensureOutline(mesh);
    if (!bgfx::isValid(gpu->geom->triEdgeInst))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    uint32_t ref = ((refCounter - 1) % 255) + 1;
    const uint64_t depthtest =
        spec.depthTest ? BGFX_STATE_DEPTH_TEST_LEQUAL : 0;
    const uint64_t depthstate = depthtest
        | (spec.depthWrite ? BGFX_STATE_WRITE_Z : 0);
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    // Shared uniforms of the edge and corner passes: flat outline
    // color, forced opaque. When the edges write depth, they get
    // twice the fill's polygon-offset bias so the owning fill
    // (biased away from the viewer) still passes LEQUAL and blends
    // over its outline like GL's ordered draw does, while fills of
    // objects genuinely behind the outline stay depth-killed.
    float color[4];
    unpackColor((spec.color & 0xffffff00) | 0xff, color);
    params[1] = qMax(1.0f, std::floor(spec.width + 0.5f));
    params[2] = spec.depthWrite
        ? 2.0f * polygonOffsetBias(draw.material) : 0.0f;
    const uint64_t outlinestate = BGFX_STATE_WRITE_RGB
        | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA | depthstate;
    const uint32_t outlinestencil = BGFX_STENCIL_TEST_NOTEQUAL
        | BGFX_STENCIL_FUNC_REF(ref) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_KEEP;
    LineQuadVertex::init();

    // Pass 2: the triangle edges as instanced thick lines where the
    // stencil differs — the boundary outline. Instances map 1:1 onto
    // triangle index positions, so the index range is the instance
    // range.
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    bgfx::setInstanceDataBuffer(gpu->geom->triEdgeInst, uint32_t(start),
                                uint32_t(count));
    bgfx::setState(outlinestate);
    bgfx::setStencil(outlinestencil);
    bgfx::submit(vid(spec.view),
                 clipped ? m_progLineClip : m_progLine);
    ++drawcount;

    // Pass 3: point-sprite corner caps (GL's GL_POINT polygon-mode
    // pass), patching the notches thick quads leave at corners.
    if (!spec.caps)
        return;
    if (spec.capWidth > 0.0f)
        params[1] = qMax(1.0f, std::floor(spec.capWidth + 0.5f));
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    bgfx::setInstanceDataBuffer(gpu->geom->triCornerInst, uint32_t(start),
                                uint32_t(count));
    bgfx::setState(outlinestate);
    bgfx::setStencil(outlinestencil);
    bgfx::submit(vid(spec.view),
                 clipped ? m_progPointClip : m_progPoint);
    ++drawcount;
}

void BGFXView::updateHatchTexture(uint64_t version, const uint8_t *rgba,
                        int width, int height)
{
    if (version == m_hatchVersion)
        return;
    m_hatchVersion = version;
    if (bgfx::isValid(m_hatchTex)) {
        bgfx::destroy(m_hatchTex);
        m_hatchTex = BGFX_INVALID_HANDLE;
    }
    if (!rgba || width <= 0 || height <= 0)
        return;
    m_hatchTex = bgfx::createTexture2D(uint16_t(width), uint16_t(height),
        false, 1, bgfx::TextureFormat::RGBA8, 0,
        bgfx::copy(rgba, uint32_t(width) * uint32_t(height) * 4));
}

bool BGFXView::submitCapMark(const Render::DrawCall &draw,
                   const float plane[4], uint16_t view)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return false;
    const Render::MeshData &mesh = *draw.mesh;
    GpuMesh *gpu = getMesh(mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return false;

    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float clipParams[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const uint32_t markstencil = BGFX_STENCIL_TEST_ALWAYS
        | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_INVERT;

    auto submitRange = [&](int start, int count) {
        bgfx::setUniform(u_matColor, zero);
        bgfx::setUniform(u_matEmissive, zero);
        bgfx::setUniform(u_matSpecular, zero);
        bgfx::setUniform(u_params, params);
        bgfx::setUniform(u_clipParams, clipParams);
        bgfx::setUniform(u_clipPlanes, plane, 1);
        setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
        setMeshVertexBuffers(gpu, mesh);
        if (count > 0)
            bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(start),
                                 uint32_t(count));
        else
            bgfx::setIndexBuffer(gpu->geom->tri);
        bgfx::setState(BGFX_STATE_MSAA);
        bgfx::setStencil(markstencil);
        bgfx::submit(vid(view), m_progFlatClip);
        ++drawcount;
    };

    if (mesh.solidParts.empty())
        submitRange(0, 0);
    else
        for (const auto &part : mesh.solidParts)
            submitRange(part.first, part.second);
    return true;
}

void BGFXView::submitCapQuad(const CapVertex verts[4], uint32_t color,
                   const float (*otherPlanes)[4], int numOther,
                   bool hatch, bool blend, uint16_t view)
{
    if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
    auto *v = reinterpret_cast<CapVertex *>(tvb.data);
    v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
    v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

    float col[4];
    unpackColor(color, col);
    bgfx::setUniform(u_matColor, col);
    if (numOther > 0) {
        float clipParams[4] = {float(numOther), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_clipParams, clipParams);
        bgfx::setUniform(u_clipPlanes, otherPlanes, numOther);
    }
    // GL only textures the cap when hatching is enabled; the white
    // stand-in keeps the shader uniform otherwise.
    bgfx::setTexture(0, s_texHatch,
        hatch && bgfx::isValid(m_hatchTex) ? m_hatchTex : m_whiteTex);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA
        | (blend ? BGFX_STATE_BLEND_ALPHA : 0));
    bgfx::setStencil(BGFX_STENCIL_TEST_EQUAL
        | BGFX_STENCIL_FUNC_REF(1) | BGFX_STENCIL_FUNC_RMASK(0x01)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_KEEP
        | BGFX_STENCIL_OP_PASS_Z_KEEP);
    bgfx::submit(vid(view),
                 numOther > 0 ? m_progCapClip : m_progCap);
    ++drawcount;
}

void BGFXView::submitCapCleanup(const CapVertex verts[4], uint16_t view)
{
    if (bgfx::getAvailTransientVertexBuffer(6, CapVertex::ms_layout) < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, CapVertex::ms_layout);
    auto *v = reinterpret_cast<CapVertex *>(tvb.data);
    v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
    v[3] = verts[0]; v[4] = verts[2]; v[5] = verts[3];

    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_matColor, zero);
    bgfx::setTexture(0, s_texHatch, m_whiteTex);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_MSAA);
    bgfx::setStencil(BGFX_STENCIL_TEST_ALWAYS
        | BGFX_STENCIL_FUNC_REF(0) | BGFX_STENCIL_FUNC_RMASK(0xff)
        | BGFX_STENCIL_OP_FAIL_S_KEEP
        | BGFX_STENCIL_OP_FAIL_Z_REPLACE
        | BGFX_STENCIL_OP_PASS_Z_REPLACE);
    bgfx::submit(vid(view), m_progCap);
    ++drawcount;
}

void BGFXView::submitComposite()
{
    TransientVertex::init();
    if (bgfx::getAvailTransientVertexBuffer(3, TransientVertex::ms_layout)
            < 3)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 3, TransientVertex::ms_layout);
    auto *v = reinterpret_cast<TransientVertex *>(tvb.data);
    // Clip-space triangle covering the viewport.
    v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    bgfx::setTexture(0, s_texAccum, oitAccum);
    bgfx::setTexture(1, s_texReveal, oitReveal);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                BGFX_STATE_BLEND_SRC_ALPHA));
    bgfx::submit(vid(ViewOITComposite), m_progComp);
    ++drawcount;
}

void BGFXView::submitDebug(const Render::RenderDebugConfig &conf, float maxDepth,
                 int aoMethod, bool shadowValid, float impactLife)
{
    if (!bgfx::isValid(m_progDebug) || !bgfx::isValid(aoNormalZ))
        return;
    float params[4] = {float(conf.viewMode),
                       maxDepth > 0.0f ? 1.0f / maxDepth : 1.0f,
                       shadowValid ? 1.0f : 0.0f,
                       float(shadowSize)};
    bgfx::setUniform(u_debugParams, params);
    // Dynamically bound named uniforms (docs/RenderDebug.md §2.5),
    // set against this pass's draw: uniform updates recorded before
    // an EMPTY submit (bgfx::touch, the view clears) are discarded
    // with the dropped draw, so the push must precede a real draw.
    // The u_userParams bootstrap pool merges the user value onto
    // its identity default (x = output scale, y = bias) and is set
    // exactly once — a second setUniform of one handle before the
    // same submit is fatal in bgfx debug builds.
    {
        float pool[16] = {1.0f};
        for (const auto &p : conf.userParams) {
            if (p.name == "u_userParams") {
                std::memcpy(pool, p.values.data(),
                            std::min(p.values.size(), size_t(16))
                                * sizeof(float));
                continue;
            }
            _BGFXLib.setUserUniform(p.name, p.values.data(),
                                    uint16_t(p.values.size() / 4));
        }
        _BGFXLib.setUserUniform("u_userParams", pool, 4);
    }
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    // The finished AO term: GTAO denoises back into aoTex, the
    // classic blur lands in aoBlurTex (see submitAOResolve).
    bgfx::TextureHandle ao = aoMethod == 1 ? aoTex : aoBlurTex;
    bgfx::setTexture(2, s_texAO,
                     bgfx::isValid(ao) ? ao : m_whiteTex);
    if (shadowValid) {
        // The shadow-map sampling state of fc_volume_shadow.sh —
        // the same set the volumetric/caustics passes bind.
        float lightDir[4] = {lightDirView[0], lightDirView[1],
                             lightDirView[2], 1.0f};
        float evsm[4] = {shadowWarpFrame, shadowThreshold,
                         shadowSpreadUv, shadowSpreadMode};
        bgfx::setUniform(u_evsm, evsm);
        bgfx::setUniform(u_lightDir, lightDir);
        bgfx::setUniform(u_lightPos, lightPosView);
        bgfx::setUniform(u_lightColor, lightColorI);
        bgfx::setUniform(u_shadowMatrix, shadowMtx);
        bgfx::setTexture(1, s_texShadow, shadowTex);
    }
    else {
        bgfx::setTexture(1, s_texShadow, m_whiteTex);
    }
    // The tile-coverage mode (5) replicates the mesh receivers'
    // bulb-tile selection: it needs the local-light state and the
    // atlas matrices with this draw (frame-global pushes recorded
    // against other submits do not reach this program).
    if (bgfx::isValid(u_bulbShadowMtx)) {
        bgfx::setUniform(u_localLight, localLightView, kLocalLights);
        bgfx::setUniform(u_localLightColor, localLightColorI,
                         kLocalLights);
        bgfx::setUniform(u_bulbShadowMtx, bulbShadowMtx,
                         kBulbShadowTiles);
        bgfx::setUniform(u_bulbShadowConf, bulbShadowConf,
                         kLocalLights - kMediumSlots);
        bgfx::setUniform(u_bulbShadowRot, bulbShadowRotMtx);
        bgfx::setTexture(3, s_texBulbShadow,
                         bgfx::isValid(bulbShadowTex)
                             ? bulbShadowTex : m_whiteTex);
    }
    if (bgfx::isValid(s_texDebugScene))
        bgfx::setTexture(4, s_texDebugScene,
                         bgfx::isValid(debugSceneTex)
                             ? debugSceneTex : m_whiteTex);
    // Mode 9 shows the mirror target itself. Binding the black
    // texture when there is none keeps "no reflection pass ran"
    // distinguishable from "the pass ran and mirrored nothing":
    // the mode tints zero coverage, and a black bind reads as
    // coverage 0 everywhere.
    if (bgfx::isValid(s_texRefl))
        bgfx::setTexture(5, s_texRefl,
                         bgfx::isValid(reflTex) ? reflTex : m_blackTex);
    // Mode 10 shows the particle impact map. Black when there is
    // none, which the mode reads as "no cell was ever struck" — the
    // same picture a map that nothing has reported into gives, and
    // the distinction that matters (map vs surface) is elsewhere.
    if (bgfx::isValid(s_texImpact)) {
        bgfx::setTexture(6, s_texImpact,
                         bgfx::isValid(impactTex) ? impactTex
                                                  : m_blackTex);
        // Recorded with this draw: a uniform pushed for the water
        // pass belongs to that draw, not to the frame.
        const float cfg[4] = {float(kImpactRes), impactNow,
                              impactLife, 1.0f};
        bgfx::setUniform(u_waterImpactCfg, cfg);
    }
    fullscreen(ViewDebug, m_progDebug,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

bool BGFXView::ensureDebugScene()
{
    if (!bgfx::isValid(m_progDebugScene))
        return false;
    if (bgfx::isValid(debugSceneFbo) && debugSceneW == width
            && debugSceneH == height)
        return true;
    if (bgfx::isValid(debugSceneFbo)) {
        bgfx::destroy(debugSceneFbo);
        debugSceneFbo = BGFX_INVALID_HANDLE;
    }
    for (auto tex : {&debugSceneTex, &debugSceneDepth}) {
        if (bgfx::isValid(*tex)) {
            bgfx::destroy(*tex);
            *tex = BGFX_INVALID_HANDLE;
        }
    }
    const bgfx::Caps *caps = bgfx::getCaps();
    if (!(caps->formats[bgfx::TextureFormat::RGBA16F]
          & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER))
        return false;
    // RGBA16F: the overdraw counts accumulate additively well past
    // 8-bit range, and the UV mode wants more than 8-bit texcoords.
    // POINT-sampled — the blit reads pixel centers 1:1.
    const uint64_t flags = 0
        | BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP;
    debugSceneTex = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::RGBA16F, flags);
    debugSceneDepth = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::D24S8,
        flags | BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle att[2] = {debugSceneTex, debugSceneDepth};
    debugSceneFbo = bgfx::createFrameBuffer(2, att, false);
    debugSceneW = width;
    debugSceneH = height;
    return bgfx::isValid(debugSceneFbo);
}

void BGFXView::submitDebugScene(const Render::DrawCall &draw, int mode)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    setClipUniforms(mat);
    float params[4] = {float(mode), 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_debugParams, params);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                     (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (mode == 6)
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE);
    else
        state |= BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    if (mat.culling && !mat.twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    bgfx::setState(state);
    bgfx::submit(vid(ViewDebugScene),
                 clipped ? m_progDebugSceneClip : m_progDebugScene);
    ++drawcount;
}
