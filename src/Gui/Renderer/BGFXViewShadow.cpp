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

void BGFXView::submitShadowGround(const float bmin[3], const float bmax[3],
                        const Render::LightConfig &light,
                        bool prepass)
{
    // Sizing, placement and the fully-transparent case (Coin then
    // switches to a shadow-only ground rendering, not ported) all live
    // in LightConfig::groundQuad, which is what the Coin quad does.
    float corners[4][3];
    float halfExtent[2];
    if (!light.groundQuad(bmin, bmax, corners, halfExtent))
        return;
    uint32_t colorPacked = light.groundColor;

    TransientVertex::init();
    if (bgfx::getAvailTransientVertexBuffer(6, TransientVertex::ms_layout)
            < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, TransientVertex::ms_layout);
    auto verts = reinterpret_cast<TransientVertex *>(tvb.data);
    // Two triangles over the four corners, wound as they come.
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; ++i) {
        const float *p = corners[order[i]];
        verts[i].px = p[0];
        verts[i].py = p[1];
        verts[i].pz = p[2];
        verts[i].nx = verts[i].ny = 0.0f;
        verts[i].nz = 1.0f;
        verts[i].rgba = 0xffffffffu;
    }

    float color[4], zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    unpackColor(colorPacked, color);
    // Ground transparency (ShadowGroundTransparency): plain alpha
    // blend over whatever lies behind in the depth order (the
    // background; the quad still writes depth like Coin's ground).
    color[3] = 1.0f - light.groundTransparency;
    float params[4] = {0.0f, 1.0f, 1.0f, 0.0f};  // lit, two-sided
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    // Backend geometry with no polygon offset of its own; the slope
    // term is a global uniform, so it has to be cleared here too.
    setPolygonOffsetUniform(nullptr);
    float pbrOff[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_pbrParams, pbrOff);
    // Backend geometry, not scene geometry: the ground stays lit the
    // ordinary way even while the scene is matcap-shaded.
    bgfx::setUniform(u_matcapParams, pbrOff);
    bgfx::setTexture(1, s_texEnv, m_dummyEnvTex);
    static const bool dbgvis =
        getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
    float shadowParams[4] = {1.0f, shadowEpsilon, 0.003f,
                             dbgvis ? 1.0f : 0.0f};
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2], 1.0f};
    bgfx::setUniform(u_shadowParams, shadowParams);
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     shadowSpreadUv, shadowSpreadMode};
    bgfx::setUniform(u_evsm, evsm);
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightPos, lightPosView);
    bgfx::setUniform(u_lightColor, lightColorI);
    bgfx::setUniform(u_shadowMatrix, shadowMtx);
    bgfx::setTexture(3, s_texShadow, shadowTex);
    if (bgfx::isValid(s_texShadowTint))
        bgfx::setTexture(7, s_texShadowTint,
                         bgfx::isValid(shadowTintTex) ? shadowTintTex
                                                      : m_whiteTex);
    // The ground is a main-pass opaque draw of the mesh program:
    // bind the screen AO like any other (the ground stays out of
    // the AO prepass, so its own pixels read ~1 — matching the
    // old fullscreen multiply).
    bgfx::setTexture(9, s_texAOScreen,
                     bgfx::isValid(aoMeshTex) ? aoMeshTex
                                              : m_whiteTex);

    // Ground texture (ShadowGroundTexture): tiled every
    // groundTextureSize world units, modulated by the ground color
    // like Coin's default SoTexture2 model on the ground material.
    // A ground bump map (ShadowGroundBumpMap) rides the same
    // textured program with the same tiled UVs (white color
    // stand-in when there is no ground texture, the scene's
    // lone-bump-map pattern).
    bool textured = (light.groundTexture || light.groundBumpMap)
        && bgfx::isValid(m_progMeshTex);
    bgfx::TransientVertexBuffer uvb;
    if (textured) {
        TexCoordVertex::init();
        if (bgfx::getAvailTransientVertexBuffer(
                    6, TexCoordVertex::ms_layout) < 6) {
            textured = false;
        }
        else {
            bgfx::allocTransientVertexBuffer(
                &uvb, 6, TexCoordVertex::ms_layout);
            auto *uv = reinterpret_cast<TexCoordVertex *>(uvb.data);
            // One span per axis, as Coin does (w = width*2/textureSize,
            // l = length*2/textureSize): they are equal only while the
            // ground is sized automatically, and an explicitly sized one
            // would otherwise stretch its texture along the longer side.
            const float spanU = light.groundTextureSize > 1.0e-5f
                ? 2.0f * halfExtent[0] / light.groundTextureSize : 1.0f;
            const float spanV = light.groundTextureSize > 1.0e-5f
                ? 2.0f * halfExtent[1] / light.groundTextureSize : 1.0f;
            // Unit-square coordinates of the four corners, in the
            // winding groundQuad returns them.
            static const float un[4] = {0.0f, 1.0f, 1.0f, 0.0f};
            static const float vn[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            for (int i = 0; i < 6; ++i) {
                uv[i].u = un[order[i]] * spanU;
                uv[i].v = vn[order[i]] * spanV;
            }
        }
    }
    if (textured) {
        float texParams[4] = {
            float(Render::TextureImage::Modulate),
            0.0f, 0.0f, 0.0f};
        bgfx::TextureHandle colorTex = m_whiteTex;
        if (light.groundTexture) {
            colorTex = getTexture(*light.groundTexture)->handle;
            texParams[1] =
                light.groundTexture->numComponents == 4 ? 1.0f
                                                        : 0.0f;
        }
        float texmat[16];
        bx::mtxIdentity(texmat);
        bgfx::setTexture(0, s_texColor, colorTex);
        bgfx::setUniform(u_texParams, texParams);
        bgfx::setUniform(u_texBlendColor, zero);
        bgfx::setUniform(u_texMatrix, texmat);
        float bumpParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        bgfx::TextureHandle bump = m_whiteTex;
        if (light.groundBumpMap) {
            const auto &bm = *light.groundBumpMap;
            bool heightmap = bm.numComponents <= 2;
            bumpParams[0] = heightmap
                ? (bumpParallax ? 3.0f : 2.0f) : 1.0f;
            bumpParams[1] = heightmap
                ? 0.04f * bumpScale : bumpScale;
            bumpParams[2] = 1.0f / float(bm.width);
            bumpParams[3] = 1.0f / float(bm.height);
            bump = getTexture(bm)->handle;
        }
        bgfx::setUniform(u_bumpParams, bumpParams);
        bgfx::setTexture(2, s_texBump, bump);
        bgfx::setTexture(4, s_texEmissive, m_whiteTex);
        bgfx::setTexture(5, s_texOcclusion, m_whiteTex);
    }

    float identity[16];
    bx::mtxIdentity(identity);
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, &tvb);
    if (textured)
        bgfx::setVertexBuffer(1, &uvb);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA;
    if (light.groundTransparency > 0.0f)
        state |= BGFX_STATE_BLEND_ALPHA;
    bgfx::setState(state);
    bgfx::submit(vid(ViewOpaque),
                 textured ? m_progMeshTex : m_progMesh);
    ++drawcount;

    // The volumetric raymarch ends rays at the prepass depth, so the
    // ground must be a prepass source too or shafts would continue
    // through it (SSAO alone keeps the ground out of the prepass —
    // it neither receives nor casts AO, preserved behavior).
    if (prepass && bgfx::isValid(m_progPrepass)) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(vid(ViewAOPrepass), m_progPrepass);
        ++drawcount;
    }
}

void BGFXView::submitShadowCaster(const Render::DrawCall &draw)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS);
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     0.0f, 0.0f};
    bgfx::setUniform(u_evsm, evsm);
    bgfx::submit(vid(ViewShadow),
                 clipped ? m_progShadowClip : m_progShadow);
    ++drawcount;
}

void BGFXView::submitBulbShadowCaster(const Render::DrawCall &draw, int tile)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS);
    float evsm[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_evsm, evsm);
    bgfx::submit(vid(ViewBulbShadow0 + tile),
                 clipped ? m_progShadowClip : m_progShadow);
    ++drawcount;
}

void BGFXView::submitShadowTint(const Render::DrawCall &draw)
{
    if (!draw.mesh || !draw.mesh->triangleIndices
            || !bgfx::isValid(m_progShadowTint))
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    float color[4];
    unpackColor(mat.diffuse, color);
    bgfx::setUniform(u_matColor, color);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    bgfx::setState(BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO,
                                           BGFX_STATE_BLEND_SRC_COLOR)
                   | (mat.ccw ? BGFX_STATE_CULL_CW
                              : BGFX_STATE_CULL_CCW));
    bgfx::submit(vid(ViewShadowTint), m_progShadowTint);
    ++drawcount;
}

void BGFXView::ensureShadowTargets(uint16_t size)
{
    if (!m_shadow || (size == shadowSize && bgfx::isValid(shadowFbo)))
        return;
    for (auto fb : {&shadowFbo, &shadowBlurFbo, &shadowBlurBackFbo,
                    &shadowTintFbo, &shadowTintBlurFbo,
                    &shadowTintBlurBackFbo}) {
        if (bgfx::isValid(*fb)) {
            bgfx::destroy(*fb);
            *fb = BGFX_INVALID_HANDLE;
        }
    }
    for (auto tex : {&shadowTex, &shadowDepth, &shadowBlurTex,
                     &shadowTintTex, &shadowTintBlurTex}) {
        if (bgfx::isValid(*tex)) {
            bgfx::destroy(*tex);
            *tex = BGFX_INVALID_HANDLE;
        }
    }
    shadowSize = size;
    shadowMapHash = 0;
    // The variance moments are linearly filtered (VSM samples the
    // filtered map). The format was already chosen so this is always a
    // filterable one on this GPU (RG32F where float32 linear is
    // available, else RG16F -- see the format selection), so the sampler
    // stays linear everywhere and nothing is point-sampled.
    const uint64_t shadowSamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    shadowTex = bgfx::createTexture2D(size, size,
        false, 1, shadowFormat,
        BGFX_TEXTURE_RT | shadowSamp);
    shadowDepth = bgfx::createTexture2D(size, size,
        false, 1, bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
    bgfx::TextureHandle att[2] = {shadowTex, shadowDepth};
    shadowFbo = bgfx::createFrameBuffer(2, att, false);
    // ShadowSmoothBorder: separable gaussian blur of the moments,
    // horizontal into the ping texture and vertical back into
    // shadowTex (a second color-only framebuffer over the same
    // texture; no depth needed for fullscreen passes).
    shadowBlurTex = bgfx::createTexture2D(size, size,
        false, 1, shadowFormat,
        BGFX_TEXTURE_RT | shadowSamp);
    shadowBlurFbo = bgfx::createFrameBuffer(1, &shadowBlurTex,
                                            false);
    shadowBlurBackFbo = bgfx::createFrameBuffer(1, &shadowTex,
                                                false);
    // Glass shadow tint map: color only (multiplicative blending
    // needs no depth), cleared to white each caster pass.
    shadowTintTex = bgfx::createTexture2D(size, size,
        false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP);
    shadowTintFbo = bgfx::createFrameBuffer(1, &shadowTintTex,
                                            false);
    // ShadowSmoothBorder blurs the tint map with the same separable
    // pass as the moments (ping texture + write-back framebuffer).
    shadowTintBlurTex = bgfx::createTexture2D(size, size,
        false, 1, bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP);
    shadowTintBlurFbo = bgfx::createFrameBuffer(
        1, &shadowTintBlurTex, false);
    shadowTintBlurBackFbo = bgfx::createFrameBuffer(
        1, &shadowTintTex, false);
}

void BGFXView::submitShadowBlur(float smoothBorder)
{
    if (!bgfx::isValid(m_progShadowBlur)
            || !bgfx::isValid(shadowBlurFbo)
            || !bgfx::isValid(shadowBlurBackFbo))
        return;
    // Gaussian sigma in texels, scaled by the map size so the
    // penumbra is a resolution-independent fraction of the light
    // window (a fixed texel width on a 2048 map covering the whole
    // scene came out ~4 screen px — invisible): 100 -> 6 texels at
    // a 512 map, 24 at 2048. The kernel is dense (2-texel bilinear
    // fetch pairs out to 3 sigma) whatever the width — stretching
    // a fixed tap count instead replicates the shadow edge at each
    // tap (staircase ghosting on real GPUs).
    float sigma = std::max(
        smoothBorder * 0.06f * float(shadowSize) / 512.0f, 0.5f);
    float pairs = std::min(std::ceil(sigma * 1.5f), 64.0f);
    float dirH[4] = {1.0f, 0.0f, sigma, pairs};
    bgfx::setUniform(u_shadowBlur, dirH);
    bgfx::setTexture(0, s_texShadow, shadowTex);
    fullscreen(ViewShadowBlurH, m_progShadowBlur,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    float dirV[4] = {0.0f, 1.0f, sigma, pairs};
    bgfx::setUniform(u_shadowBlur, dirV);
    bgfx::setTexture(0, s_texShadow, shadowBlurTex);
    fullscreen(ViewShadowBlurV, m_progShadowBlur,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    // The glass shadow tint map gets the same blur — an unblurred
    // tint edge would keep a hard (map-texel) border inside the
    // smoothed penumbra wherever a glass/water caster shadows the
    // receiver.
    if (bgfx::isValid(shadowTintBlurFbo)
            && bgfx::isValid(shadowTintBlurBackFbo)) {
        bgfx::setUniform(u_shadowBlur, dirH);
        bgfx::setTexture(0, s_texShadow, shadowTintTex);
        fullscreen(ViewShadowTintBlurH, m_progShadowBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::setUniform(u_shadowBlur, dirV);
        bgfx::setTexture(0, s_texShadow, shadowTintBlurTex);
        fullscreen(ViewShadowTintBlurV, m_progShadowBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    }
}

void BGFXView::submitPrepass(const Render::DrawCall &draw)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    if (mat.culling && !mat.twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    bgfx::setState(state);
    bgfx::submit(vid(ViewAOPrepass),
                 clipped ? m_progPrepassClip : m_progPrepass);
    ++drawcount;
}

void BGFXView::submitWaterDepth(const Render::DrawCall &draw, bool back,
                      int kind, int slot)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh) || !bgfx::isValid(gpu->geom->tri))
        return;

    const Render::Material &mat = draw.material;
    bool clipped = mat.numclipplanes > 0;
    setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z;
    if (back) {
        state |= BGFX_STATE_DEPTH_TEST_GREATER
            | (mat.ccw ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW);
    } else {
        state |= BGFX_STATE_DEPTH_TEST_LESS
            | (mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW);
    }
    bgfx::setState(state);
    uint16_t pass = kind == 1
        ? (back ? ViewGlassBack : ViewGlassFront)
        : kind == 2 ? (back ? ViewCloudBack : ViewCloudFront)
        : kind == 3 ? (back ? ViewFireBack : ViewFireFront)
                    : (back ? ViewWaterBack : ViewWaterFront);
    float slotv[4] = {float(slot), 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_mediumSlot, slotv);
    if (getenv("FC_BGFX_DEBUG_SUBMIT"))
        fprintf(stderr,
                "bgfx meddepth kind=%d back=%d slot=%d key=%llx\n",
                kind, back, slot,
                (unsigned long long)draw.objectKey);
    bgfx::submit(vid(pass),
                 clipped ? m_progMedDepthClip : m_progMedDepth);
    ++drawcount;
}
