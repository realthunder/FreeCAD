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
                        const Render::GroundCamera &cam,
                        bool prepass)
{
    // A shadow-only ground needs no quad at all -- unless something
    // else needs the quad's depth. A ground reflection does: it
    // depth-tests EQUAL against it, so the surface has to be there to
    // test against even when nothing paints it.
    if (light.groundShadowOnly() && !light.groundReflection
            && submitShadowGroundPlane(bmin, bmax, light, cam))
        return;

    // Sizing, placement and the fully-transparent case (Coin then
    // switches to a shadow-only ground rendering, not ported) all live
    // in LightConfig::groundQuad, which is what the Coin quad does.
    float corners[4][3];
    float halfExtent[2];
    if (!light.groundQuad(bmin, bmax, cam, corners, halfExtent))
        return;
    // A fully transparent ground carries the shadow and nothing else
    // (Coin's TRANSPARENT_SHADOWED). Nothing below changes for it
    // except which program paints the fragments: same quad, same
    // depth, same winding -- so a ground reflection still lines up
    // with it, and the texture rows simply have nothing to modulate.
    const bool shadowOnly = light.groundShadowOnly();
    if (shadowOnly && !bgfx::isValid(m_progGroundShadow))
        return;
    uint32_t colorPacked = light.groundColor;

    // A camera-fitted ground reads as endless only if it does not END
    // anywhere the eye can see, so its rim fades out -- which takes the
    // mesh program's ground variants. Sized by hand, it does not: an
    // explicit extent is a plate somebody asked for, edge included.
    // Decided here because the texture branch below asks which program
    // is going to paint the quad.
    const bool wantFade = !shadowOnly && light.groundAuto
        && light.groundFollowCamera && cam.valid && viewMatrix
        && bgfx::isValid(u_groundFadeU) && bgfx::isValid(u_groundFadeV);
    const bgfx::ProgramHandle progPlain =
        wantFade && bgfx::isValid(m_progGroundFade) ? m_progGroundFade
                                                    : m_progMesh;
    const bgfx::ProgramHandle progTex =
        wantFade && bgfx::isValid(m_progGroundFadeTex)
            ? m_progGroundFadeTex : m_progMeshTex;

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
    // An AUTHORED colour: someone picked the ground's grey, so it is a
    // display number and decodes on the way in like every other picked
    // colour. Handed over encoded it arrived as light 1.5x too bright,
    // which a lit ground half hides and a shadow-only one cannot: the
    // shadow IS this colour, and 0.49 painted as 0.73 is the
    // background.
    unpackAuthoredColor(colorPacked, color, colorManaged());
    // Ground transparency (ShadowGroundTransparency): plain alpha
    // blend over whatever lies behind in the depth order (the
    // background; the quad still writes depth like Coin's ground).
    // A shadow-only ground spends the alpha slot differently: it is
    // how dark the shadow itself lands, since the lit ground is not
    // drawn at all. That is Coin's SoShadowTransparency, and it is what
    // separates the two shadow-only modes: 0 a solid shadow, anything
    // above it a translucent one.
    color[3] = shadowOnly
        ? std::min(1.0f, std::max(0.0f, 1.0f - light.shadowTransparency))
        : 1.0f - light.groundTransparency;
    // Lighting and sidedness are the ground's own properties
    // (ShadowGroundShading, ShadowGroundBackFaceCull), which Coin
    // states as an SoLightModel and an SoShapeHints above the quad
    // rather than on it. Unlit is BASE_COLOR: the flat ground color,
    // shadow still subtracted. A culled ground has no back face to
    // shade, so the two-sided normal flip goes with it.
    float params[4] = {0.0f,
                       light.groundShading ? 1.0f : 0.0f,
                       light.groundBackFaceCull ? 0.0f : 1.0f,
                       0.0f};
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
    // ordinary way even while the scene is matcap-shaded, and carries
    // no machined finish (a global uniform, so leaving it alone would
    // knurl the ground with whatever the last scene draw stated).
    bgfx::setUniform(u_matcapParams, pbrOff);
    bgfx::setUniform(u_finishParams, pbrOff);
    // Nor does the ground carry per-face images -- and the palette is a
    // global uniform too, so leaving it alone would paint the ground
    // with whatever the last scene draw put on its faces.
    float faceTexOff[4] = {0.0f, 0.0f, -1.0f, 0.0f};
    bgfx::setUniform(u_faceTexParams, faceTexOff);
    if (bgfx::isValid(m_whiteTexArray))
        bgfx::setTexture(10, s_texFace, m_whiteTexArray);
    bgfx::setTexture(1, s_texEnv, m_dummyEnvTex);
    static const bool dbgvis =
        getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
    // The ground is also drawn for the ground reflection, which needs
    // no shadow map -- then there are no moments to sample, and the
    // quad is simply a lit plane. u_shadowParams.x = 0 says so; the
    // sampler still needs a valid bind, which the shader never reads
    // (the water surface pass does the same).
    const bool shadowed = shadowFrame && bgfx::isValid(shadowTex);
    float shadowParams[4] = {shadowed ? 1.0f : 0.0f, shadowEpsilon,
                             0.003f, dbgvis ? 1.0f : 0.0f};
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
    bgfx::setTexture(3, s_texShadow, shadowed ? shadowTex : m_whiteTex);
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
    // A shadow-only ground has no lit surface to paint, so a ground
    // texture or bump map has nothing to modulate.
    bool textured = !shadowOnly
        && (light.groundTexture || light.groundBumpMap)
        && bgfx::isValid(progTex);
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

    // The rim fade, in the quad's OWN axes -- read back off the corners
    // rather than recomputed from the extent, so the fade cannot land
    // anywhere but on the quad that was actually built:
    //
    //   corners[1] - corners[0] = 2 hx U     (the quad's u axis)
    //   corners[3] - corners[0] = 2 hy V
    //   centre                  = (corners[0] + corners[2]) / 2
    //
    // The uniform carries U/hx with the centre folded into w, so a
    // fragment's coordinate is dot(xyz, pos) + w: +-1 at the rim. In
    // VIEW space, because that is the position every ground program
    // already has -- the mesh variants for their lighting, the prepass
    // for its depth. A rotation does not change a vector's length, so
    // the axes stay normalized across it.
    const bgfx::ProgramHandle prog = shadowOnly
        ? m_progGroundShadow : (textured ? progTex : progPlain);
    const bool faded = wantFade
        && (prog.idx == m_progGroundFade.idx
            || prog.idx == m_progGroundFadeTex.idx);
    if (faded) {
        const float *V = viewMatrix;
        float centre[3], axU[3], axV[3];
        const float invU = 0.5f / (halfExtent[0] * halfExtent[0]);
        const float invV = 0.5f / (halfExtent[1] * halfExtent[1]);
        for (int i = 0; i < 3; ++i) {
            centre[i] = 0.5f * (corners[0][i] + corners[2][i]);
            axU[i] = (corners[1][i] - corners[0][i]) * invU;
            axV[i] = (corners[3][i] - corners[0][i]) * invV;
        }
        float cv[3], uview[3], vview[3];
        for (int r = 0; r < 3; ++r) {
            cv[r] = V[r] * centre[0] + V[4 + r] * centre[1]
                  + V[8 + r] * centre[2] + V[12 + r];
            uview[r] = V[r] * axU[0] + V[4 + r] * axU[1]
                     + V[8 + r] * axU[2];
            vview[r] = V[r] * axV[0] + V[4 + r] * axV[1]
                     + V[8 + r] * axV[2];
        }
        const float fadeU[4] = {uview[0], uview[1], uview[2],
            -(uview[0] * cv[0] + uview[1] * cv[1] + uview[2] * cv[2])};
        const float fadeV[4] = {vview[0], vview[1], vview[2],
            -(vview[0] * cv[0] + vview[1] * cv[1] + vview[2] * cv[2])};
        bgfx::setUniform(u_groundFadeU, fadeU);
        bgfx::setUniform(u_groundFadeV, fadeV);
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
    // groundQuad winds the corners counter-clockwise about +Z, which is
    // the mat.ccw case of every other cull site here.
    if (light.groundBackFaceCull)
        state |= BGFX_STATE_CULL_CW;
    if (shadowOnly) {
        // Multiplied, not blended: the shadow attenuates the frame
        // behind it (fs_fc_groundshadow), which is the only way it is
        // darker than the background whatever the background is.
        state |= BGFX_STATE_BLEND_MULTIPLY;
    }
    else if (light.groundTransparency > 0.0f || faded) {
        // Faded grounds blend whatever their transparency says: the rim
        // is an alpha ramp, and an opaque ground would draw it as a
        // hard edge one shade lighter.
        state |= BGFX_STATE_BLEND_ALPHA;
    }
    bgfx::setState(state);
    bgfx::submit(vid(ViewOpaque), prog);
    ++drawcount;

    // The volumetric raymarch ends rays at the prepass depth, so the
    // ground must be a prepass source too or shafts would continue
    // through it (SSAO alone keeps the ground out of the prepass —
    // it neither receives nor casts AO, preserved behavior). A
    // shadow-only ground is not a surface: a shaft SHOULD carry on
    // through where there is nothing to stop it.
    // A faded rim is not there, so it must not occupy space here
    // either: the raymarch ends its rays on this depth, and a shaft
    // has to carry on through a ground nobody can see. Same fade
    // uniforms, already set above.
    const bgfx::ProgramHandle prepassProg =
        faded && bgfx::isValid(m_progGroundFadePrepass)
            ? m_progGroundFadePrepass : m_progPrepass;
    if (prepass && !shadowOnly && bgfx::isValid(prepassProg)) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                       | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS
                       | (light.groundBackFaceCull ? BGFX_STATE_CULL_CW
                                                   : 0));
        bgfx::submit(vid(ViewAOPrepass), prepassProg);
        ++drawcount;
    }
}

bool BGFXView::submitShadowGroundPlane(const float bmin[3],
                        const float bmax[3],
                        const Render::LightConfig &light,
                        const Render::GroundCamera &cam)
{
    if (!bgfx::isValid(m_progGroundShadowPlane)
            || !bgfx::isValid(u_groundPlane) || !viewMatrix)
        return false;
    // Same placement as the quad, minus the extent -- an infinite
    // receiver has none, and not having one is what this path is for.
    float point[3], normal[3];
    if (!light.groundPlane(bmin, bmax, cam, point, normal))
        return false;

    // The plane in VIEW space, which is where the shader works and
    // where fcSceneShadow wants its position. The view matrix is
    // rigid, so the normal transforms as a direction with the same
    // 3x3 -- no inverse transpose.
    const float *V = viewMatrix;
    float pv[3], nv[3];
    for (int r = 0; r < 3; ++r) {
        pv[r] = V[r] * point[0] + V[4 + r] * point[1]
              + V[8 + r] * point[2] + V[12 + r];
        nv[r] = V[r] * normal[0] + V[4 + r] * normal[1]
              + V[8 + r] * normal[2];
    }
    // dot(n, p) + w == 0 on the plane, so w is the eye's own signed
    // distance from it negated: the eye is the view-space origin.
    const float planeW = -(nv[0] * pv[0] + nv[1] * pv[1] + nv[2] * pv[2]);

    // A one-sided ground shuts out a camera underneath it. On a plane
    // that is a whole-pass decision rather than a per-fragment one,
    // and which side the camera sees depends on the projection. Every
    // perspective ray fans out from the eye, so the eye's side of the
    // plane (planeW) decides. Orthographic rays all run along the view
    // axis and the eye's own place on that axis is arbitrary -- the
    // navigation code moves it freely, and an eye slid past the plane
    // while the view still looks down on it made the shadow vanish on
    // a boundary unrelated to the horizon -- so the plane's facing
    // (its view-space normal against the view direction) decides, the
    // same answer raster winding gives the quad path. The mode is
    // still HANDLED -- returning false would draw the quad this path
    // exists to avoid.
    const bool persp = projMatrix && projMatrix[11] != 0.0f;
    const bool backFacing = persp ? planeW <= 0.0f : nv[2] <= 0.0f;
    if (light.groundBackFaceCull && backFacing)
        return true;

    static const bool dbgvis =
        getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
    const bool shadowed = shadowFrame && bgfx::isValid(shadowTex);
    // No shadow map, no shadow: the quad would still be submitted for
    // its depth, and this has none to leave behind.
    if (!shadowed && !dbgvis)
        return true;

    float color[4];
    unpackAuthoredColor(light.groundColor, color, colorManaged());
    // The shadow-only ground spends the alpha slot on how dark the
    // shadow itself lands (Coin's SoShadowTransparency), since there
    // is no lit ground to make transparent.
    color[3] = std::min(1.0f, std::max(0.0f,
                1.0f - light.shadowTransparency));
    float plane[4] = {nv[0], nv[1], nv[2], planeW};
    float shadowParams[4] = {shadowed ? 1.0f : 0.0f, shadowEpsilon,
                             0.003f, dbgvis ? 1.0f : 0.0f};
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     shadowSpreadUv, shadowSpreadMode};
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2], 1.0f};
    bgfx::setUniform(u_groundPlane, plane);
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_shadowParams, shadowParams);
    bgfx::setUniform(u_evsm, evsm);
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightPos, lightPosView);
    bgfx::setUniform(u_lightColor, lightColorI);
    bgfx::setUniform(u_shadowMatrix, shadowMtx);
    bgfx::setTexture(3, s_texShadow, shadowed ? shadowTex : m_whiteTex);
    if (bgfx::isValid(s_texShadowTint))
        bgfx::setTexture(7, s_texShadowTint,
                         bgfx::isValid(shadowTintTex) ? shadowTintTex
                                                      : m_whiteTex);
    // Multiplied like the quad -- a shadow darkens what is behind it
    // whatever that is. Depth is TESTED (the shader states the plane's
    // own, so geometry in front of it occludes the shadow) and not
    // WRITTEN: a shadow is not a surface, and the volumetric raymarch
    // ends its rays on the prepass depth, which this is right to stay
    // out of.
    fullscreen(ViewOpaque, m_progGroundShadowPlane,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
               | BGFX_STATE_DEPTH_TEST_LESS
               | BGFX_STATE_BLEND_MULTIPLY);
    return true;
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
    float tint[4];
    // The body's colour as the glass pass reads it, so the shadow
    // carries the tint the body absorbs or tints with -- whichever a
    // MaterialX glass states, the product is both.
    glassBodyColors(mat, color, tint);
    for (int c = 0; c < 3; ++c)
        color[c] *= tint[c];
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
