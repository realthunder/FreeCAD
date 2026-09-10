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

void BGFXView::fullscreen(uint16_t pass, bgfx::ProgramHandle prog,
                uint64_t state, uint32_t blendFactor)
{
    TransientVertex::init();
    if (bgfx::getAvailTransientVertexBuffer(
                3, TransientVertex::ms_layout) < 3)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 3,
                                     TransientVertex::ms_layout);
    auto *v = reinterpret_cast<TransientVertex *>(tvb.data);
    v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(state, blendFactor);
    bgfx::submit(vid(pass), prog);
    ++drawcount;
}

void BGFXView::submitAOResolve(float radius, float intensity, int method,
                     bool fast, int slices, int steps,
                     int temporalIndex)
{
    // Fixed hemisphere kernel (unit radius, z >= 0, clustered near
    // the origin), deterministic across frames like the noise.
    static const float kernel[kAOSamples][4] = {
        {-0.058091f, 0.018602f, 0.079242f, 0.0f},
        {-0.016977f, 0.100367f, 0.018809f, 0.0f},
        {-0.042287f, 0.079676f, 0.069813f, 0.0f},
        {0.010341f, 0.119322f, 0.054631f, 0.0f},
        {0.012528f, 0.147272f, 0.050676f, 0.0f},
        {-0.131686f, -0.100976f, 0.088122f, 0.0f},
        {0.120937f, 0.161185f, 0.103557f, 0.0f},
        {0.024414f, -0.112444f, 0.246757f, 0.0f},
        {-0.050206f, -0.180815f, 0.265349f, 0.0f},
        {0.057177f, 0.368457f, 0.094947f, 0.0f},
        {0.223564f, 0.320370f, 0.226476f, 0.0f},
        {0.173264f, -0.484121f, 0.107897f, 0.0f},
        {0.046909f, 0.076361f, 0.599589f, 0.0f},
        {0.263978f, 0.433148f, 0.473845f, 0.0f},
        {-0.768430f, 0.171215f, 0.053107f, 0.0f},
        {0.429038f, 0.201413f, 0.754499f, 0.0f},
    };
    // .w for the classic pass = occlusion contrast/power: concentrates the
    // darkening near real contacts (visible width scales with occlusion
    // depth) so weak, distant occlusion does not wash a wide low-contrast
    // band up open faces. GTAO's horizon integral has correct falloff by
    // construction and only takes XeGTAO's mild FinalValuePower (~2.2).
    const bool gtao = method == 1 && bgfx::isValid(m_progGtao);
    const float aoPower = gtao ? 2.2f : 2.5f;
    // GTAO depth pyramid (XeGTAO depth MIP chain): weighted 2x2
    // downsamples of the prepass viewZ — level 1 reads the prepass,
    // each further level the previous one. Far horizon taps of the
    // gen pass read the coarse levels, so the fixed step count keeps
    // long-range occlusion instead of the hard screen-radius cap.
    const bool depthMips = gtao && aoMipCount > 0;
    if (depthMips) {
        uint16_t sw = width;
        uint16_t sh = height;
        for (int m = 0; m < aoMipCount; ++m) {
            float dparams[4] = {m == 0 ? 0.0f : 1.0f, radius,
                                float(sw), float(sh)};
            bgfx::setUniform(u_aoParams, dparams);
            bgfx::setTexture(0, s_texNormalZ,
                             m == 0 ? aoNormalZ : aoMipTex[m - 1]);
            fullscreen(uint16_t(ViewAODepthMip1 + m), m_progGtaoDepth,
                       BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            sw = uint16_t(std::max(1, width >> (m + 1)));
            sh = uint16_t(std::max(1, height >> (m + 1)));
        }
    }
    // .z: classic pass depth bias; for GTAO two flag bits — bit0 the
    // interaction fast path (fewer slices/steps while the camera
    // moves), bit1 fp16 prepass depth (widens the coplanarity guard
    // to the fp16 quantization step) -- and above them the idle
    // accumulation's sample index, which walks GTAO's noise along
    // the R2 sequence so successive samples decorrelate. Packed
    // rather than given a uniform of its own: both other vec4s are
    // full, the field is already a bitfield, and 4*63+3 = 255 is
    // exact in float. The index stays a SAMPLE number, so the frame
    // remains reproducible -- the same argument that keeps the
    // Halton camera jitter deterministic.
    const float paramZ = gtao
        ? (fast ? 1.0f : 0.0f) + (aoNormalZFp16 ? 2.0f : 0.0f)
            + 4.0f * float(temporalIndex & 63)
        : 0.02f * radius;
    float params[4] = {radius, intensity, paramZ, aoPower};
    bgfx::setUniform(u_aoParams, params);
    if (gtao) {
        // GTAO tuning (Render_GTAOSlices/Steps; 0 = defaults). Clamped
        // here so a wild property value cannot explode the pass.
        // .z = depth pyramid level count (0 = none, fall back to the
        // hard screen-radius cap); .w = AO-target-to-full-res pixel
        // scale (the gen pass may run at Render_AOResolution, but the
        // pyramid levels halve from FULL res, so the per-tap level
        // pick needs full-res pixel distances).
        float params2[4] = {
            float(slices > 0 ? std::min(slices, 32) : 9),
            float(steps > 0 ? std::min(steps, 16) : 6),
            depthMips ? float(aoMipCount) : 0.0f,
            ssaoW > 0 ? float(width) / float(ssaoW) : 1.0f};
        bgfx::setUniform(u_aoParams2, params2);
        for (int m = 0; m < kAOMipLevels; ++m)
            bgfx::setTexture(uint8_t(2 + m), s_texAOMip[m],
                             depthMips ? aoMipTex[m] : aoNormalZ);
    }
    else {
        // The classic pass carries the accumulation sample index in a
        // vec4 of its own rather than packing it the way GTAO does:
        // GTAO's .z is a bitfield with room above it, while every lane
        // of u_aoParams means a real value here (radius, intensity,
        // depth bias, power). u_aoParams2 has no other use in this
        // pass, so its meaning is per-program -- as u_aoParams itself
        // already is between this pass and fs_fc_gtao_depths.
        //
        // Unmasked, unlike GTAO's index: the 64-entry wrap there is
        // XeGTAO's own, a property of the sequence it steps through,
        // and nothing in the golden-ratio rotation below wants it.
        float params2[4] = {float(temporalIndex), 0.0f, 0.0f, 0.0f};
        bgfx::setUniform(u_aoParams2, params2);
    }
    if (!gtao)
        bgfx::setUniform(u_aoKernel, kernel, kAOSamples);
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    bgfx::setTexture(1, s_texAONoise, aoNoiseTex);
    fullscreen(ViewAOGen, gtao ? m_progGtao : m_progSsao,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

    // GTAO swaps the plain box blur for an edge-aware (depth+normal
    // weighted) denoise reading the prepass beside the raw AO.
    bgfx::setTexture(0, s_texAO, aoTex);
    if (gtao && bgfx::isValid(m_progGtaoBlur)) {
        bgfx::setTexture(1, s_texNormalZ, aoNormalZ);
        fullscreen(ViewAOBlur, m_progGtaoBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        // Second denoise pass (XeGTAO DenoisePasses > 1), ping-ponged
        // back into aoTex: composes to an effective ~9x9 edge-aware
        // kernel, flattening the spatial-noise grain the single 5x5
        // leaves visible.
        bgfx::setTexture(0, s_texAO, aoBlurTex);
        bgfx::setTexture(1, s_texNormalZ, aoNormalZ);
        fullscreen(ViewAOBlur2, m_progGtaoBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    }
    else {
        fullscreen(ViewAOBlur, m_progSsaoBlur,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    }
}

void BGFXView::submitCavity(float valley, float ridge, float radius)
{
    // The neighbour offsets are the *prepass* texel size (aoNormalZ is
    // always full viewport res, unlike the AO resolve targets, which
    // carry their own scale) times the radius.
    //
    // The radius is what decides which features the pass can see at all.
    // The estimator reads how far the normal turns between the two
    // neighbours, so over a one-pixel baseline it only ever sees what
    // turns within one pixel: a hard crease, and essentially nothing of
    // a smooth surface, whose normal moves by a fraction of a degree per
    // pixel. That was the whole of the effect before this was tunable,
    // and it left broad curvature invisible -- and, on a high-DPI
    // display, ever more so, because the feature is the same size in
    // millimetres while the pixel gets smaller.
    const float r = radius > 0.0f ? radius : 1.0f;
    float params[4] = {valley, ridge,
                       r / float(width), r / float(height)};
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    bgfx::setUniform(u_cavityParams, params);
    // dst *= src, alpha untouched: the darkening rides on top of
    // whatever the opaque passes left, and the background multiplies
    // by white.
    fullscreen(ViewCavity, m_progCavity,
               BGFX_STATE_WRITE_RGB
               | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO,
                                       BGFX_STATE_BLEND_SRC_COLOR));
}

void BGFXView::submitVolumetric(float density, float intensity, float maxDist,
                      const float medium[4], bool water,
                      bool surfaceSplit, float accum,
                      const float waterSigma[][4],
                      const float cloudParams[][4],
                      const float fireParams[][4],
                      const float fireParams2[][4],
                      const float fireFrames[][16],
                      const float fountainParams[][4],
                      const float fountainFrames[][16])
{
    static const float noSigma[kMediumSlots][4] = {};
    // w: 0 = no water medium, 1 = water, 2 = water + the surface
    // re-renders after the apply — the raymarch then splits its
    // output at the water entry so the surface pass can composite
    // the front segment over itself.
    float params[4] = {density, intensity, maxDist,
                       water ? (surfaceSplit ? 2.0f : 1.0f)
                             : 0.0f};
    bgfx::setUniform(u_volParams, params);
    bgfx::setUniform(u_volMedium, medium);
    bgfx::setUniform(u_waterSigma, water ? waterSigma : noSigma,
                     kMediumSlots);
    bgfx::setUniform(u_cloudParams, cloudParams, kMediumSlots);
    bgfx::setUniform(u_fireParams, fireParams, kMediumSlots);
    bgfx::setUniform(u_fireParams2, fireParams2, kMediumSlots);
    bgfx::setUniform(u_fireFrame, fireFrames, kMediumSlots);
    bgfx::setUniform(u_fountainParams, fountainParams, kMediumSlots);
    bgfx::setUniform(u_fountainFrame, fountainFrames, kMediumSlots);
    bgfx::setUniform(u_lightColor, lightColorI);
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2], 1.0f};
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightPos, lightPosView);
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     0.0f, 0.0f};
    bgfx::setUniform(u_evsm, evsm);
    // The raymarch's shadow tap (fc_volume_shadow.sh) reads the
    // mesh receivers' epsilon/bias, so the tunables act on the
    // shafts too.
    float shadowParams[4] = {1.0f, shadowEpsilon, 0.003f, 0.0f};
    bgfx::setUniform(u_shadowParams, shadowParams);
    bgfx::setUniform(u_shadowMatrix, shadowMtx);
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    bgfx::setTexture(1, s_texShadow, shadowTex);
    bgfx::setTexture(2, s_texWaterFront, waterFrontTex);
    bgfx::setTexture(3, s_texWaterBack, waterBackTex);
    bgfx::setTexture(4, s_texCloudFront, cloudFrontTex);
    bgfx::setTexture(5, s_texCloudBack, cloudBackTex);
    bgfx::setTexture(6, s_texFireFront, fireFrontTex);
    bgfx::setTexture(7, s_texFireBack, fireBackTex);
    // Per-frame jitter phase (golden-ratio sequence) so the
    // accumulated frames sample different march offsets; the apply
    // pass re-sets u_volTexel with its own values below.
    float phase[4] = {0.0f, 0.0f,
                      accum < 1.0f
                          ? float(frame % 4096) * 0.618034f : 0.0f,
                      0.0f};
    bgfx::setUniform(u_volTexel, phase);
    // User medium splice (docs/RenderEngine.md §5.11): the
    // assembled raymarch variant replaces the stock program while
    // its async compile is done; stock media stand in meanwhile.
    bgfx::ProgramHandle progVol = m_progVol;
    if (volUserVol) {
        bgfx::ProgramHandle p = _BGFXLib.getUserProgram(
            *volUserVol, "vs_fc_comp");
        if (bgfx::isValid(p)) {
            _BGFXLib.pushUserParams(*volUserVol);
            progVol = p;
            if (_BGFXLib.userTime[1] != 0.0f
                    && userShaderAnimated(*volUserVol))
                _BGFXLib.userAnimatedDraw = true;
        }
    }
    fullscreen(ViewVolGen, progVol,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

    // Temporal accumulation: blend the fresh raymarch into the
    // history pair (hist = cur * k + hist * (1 - k)); the apply
    // passes read the history. k = 1 replaces it outright (camera
    // or scene changed).
    if (bgfx::isValid(m_progVolAccum)
            && bgfx::isValid(volHistFbo)) {
        uint32_t k8 = uint32_t(
            bx::clamp(accum, 0.0f, 1.0f) * 255.0f + 0.5f);
        uint32_t kRgba = (k8 << 24) | (k8 << 16) | (k8 << 8) | k8;
        bgfx::setTexture(0, s_texVol, volTex);
        bgfx::setTexture(1, s_texVolFront, volFrontTex);
        fullscreen(ViewVolAccum, m_progVolAccum,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_FUNC(
                       BGFX_STATE_BLEND_FACTOR,
                       BGFX_STATE_BLEND_INV_FACTOR),
                   kRgba);
    }

    // Analytic per-channel surface extinction (multiply; the
    // sequential apply view keeps it before the inscatter add).
    bgfx::setUniform(u_volParams, params);
    bgfx::setUniform(u_volMedium, medium);
    bgfx::setUniform(u_waterSigma, water ? waterSigma : noSigma,
                     kMediumSlots);
    bgfx::setUniform(u_cloudParams, cloudParams, kMediumSlots);
    bgfx::setUniform(u_fireParams, fireParams, kMediumSlots);
    bgfx::setUniform(u_fireParams2, fireParams2, kMediumSlots);
    bgfx::setUniform(u_fireFrame, fireFrames, kMediumSlots);
    bgfx::setUniform(u_fountainParams, fountainParams, kMediumSlots);
    bgfx::setUniform(u_fountainFrame, fountainFrames, kMediumSlots);
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    bgfx::setTexture(1, s_texWaterFront, waterFrontTex);
    bgfx::setTexture(2, s_texWaterBack, waterBackTex);
    bgfx::setTexture(3, s_texCloudFront, cloudFrontTex);
    bgfx::setTexture(4, s_texCloudBack, cloudBackTex);
    bgfx::setTexture(5, s_texFireFront, fireFrontTex);
    bgfx::setTexture(6, s_texFireBack, fireBackTex);
    bgfx::ProgramHandle progExt = m_progVolExt;
    if (volUserExt) {
        bgfx::ProgramHandle p = _BGFXLib.getUserProgram(
            *volUserExt, "vs_fc_comp");
        if (bgfx::isValid(p)) {
            _BGFXLib.pushUserParams(*volUserExt);
            progExt = p;
        }
    }
    fullscreen(ViewVolApply, progExt,
               BGFX_STATE_WRITE_RGB
               | BGFX_STATE_BLEND_FUNC_SEPARATE(
                   BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_SRC_COLOR,
                   BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE));

    float hw = std::max(1.0f, std::floor(width / 2.0f));
    float hh = std::max(1.0f, std::floor(height / 2.0f));
    float texel[4] = {1.0f / hw, 1.0f / hh, hw, hh};
    bgfx::setUniform(u_volParams, params);
    bgfx::setUniform(u_volMedium, medium);
    bgfx::setUniform(u_volTexel, texel);
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    bgfx::setTexture(1, s_texVol,
                     bgfx::isValid(volHistTex) ? volHistTex
                                               : volTex);
    fullscreen(ViewVolApply, m_progVolApply,
               BGFX_STATE_WRITE_RGB
               | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE));
}

void BGFXView::submitCaustics(const float causticParams[][4],
                    const float waterSigma[][4])
{
    float params[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bgfx::setUniform(u_volParams, params);
    bgfx::setUniform(u_waterSigma, waterSigma, kMediumSlots);
    bgfx::setUniform(u_lightColor, lightColorI);
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2], 1.0f};
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightPos, lightPosView);
    float evsm[4] = {shadowWarpFrame, shadowThreshold, 0.0f, 0.0f};
    bgfx::setUniform(u_evsm, evsm);
    // Same epsilon/bias as the mesh receivers (fc_volume_shadow.sh).
    float shadowParams[4] = {1.0f, shadowEpsilon, 0.003f, 0.0f};
    bgfx::setUniform(u_shadowParams, shadowParams);
    bgfx::setUniform(u_shadowMatrix, shadowMtx);
    bgfx::setUniform(u_causticParams, causticParams, kMediumSlots);
    bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
    bgfx::setTexture(1, s_texShadow, shadowTex);
    bgfx::setTexture(2, s_texWaterFront, waterFrontTex);
    bgfx::setTexture(3, s_texWaterBack, waterBackTex);
    fullscreen(ViewCaustics, m_progCaustics,
               BGFX_STATE_WRITE_RGB
               | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE));
}

void BGFXView::submitWaterCopy()
{
    bgfx::setTexture(0, s_texScene, bgfxColor);
    fullscreen(ViewWaterCopy, m_progWaterCopy,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

void BGFXView::submitTemporalAccum(float blend)
{
    if (!bgfx::isValid(m_progWaterCopy) || !bgfx::isValid(accumFbo))
        return;
    // hist = cur * k + hist * (1 - k), the volumetric accumulation's
    // idiom at full resolution: a plain copy of the scene colour under
    // a constant-factor blend. k comes from the count of frames already
    // averaged, so the history is their running mean.
    //
    // The factor is 8-bit, which is why the sample count is capped
    // where it is: below 1/255 the factor rounds to zero and a further
    // frame would contribute nothing at all.
    if (blend > 0.0f) {
        uint32_t k8 = uint32_t(
            bx::clamp(blend, 0.0f, 1.0f) * 255.0f + 0.5f);
        uint32_t kRgba = (k8 << 24) | (k8 << 16) | (k8 << 8) | k8;
        bgfx::setTexture(0, s_texScene, bgfxColor);
        fullscreen(ViewAccum, m_progWaterCopy,
                   BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_FUNC(
                       BGFX_STATE_BLEND_FACTOR,
                       BGFX_STATE_BLEND_INV_FACTOR),
                   kRgba);
    }
    // Back over the scene colour, so everything downstream -- the
    // desktop blit, the output transform, the standalone present --
    // reads the converged image without knowing this pass exists.
    bgfx::setTexture(0, s_texScene, accumTex);
    fullscreen(ViewAccumApply, m_progWaterCopy,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

void BGFXView::submitUserPost(const Render::UserShader &shader,
                    bgfx::ProgramHandle prog)
{
    bgfx::setTexture(0, s_texScene, bgfxColor);
    fullscreen(ViewUserPostCopy, m_progWaterCopy,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    _BGFXLib.pushUserParams(shader);
    bgfx::setTexture(0, s_texScene, sceneCopyTex);
    fullscreen(ViewUserPost, prog,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    if (_BGFXLib.userTime[1] != 0.0f && userShaderAnimated(shader))
        _BGFXLib.userAnimatedDraw = true;
}

void BGFXView::submitBloom(float threshold, float intensity, float radius,
                 const std::vector<const Render::DrawCall *> &bulbs,
                 bool prepassCurrent)
{
    if (!bgfx::isValid(m_progBloomBright)
            || !bgfx::isValid(bloomFbo))
        return;
    float qw = std::max(1.0f, std::floor(width / 4.0f));
    float qh = std::max(1.0f, std::floor(height / 4.0f));
    float texel[4] = {1.0f / width, 1.0f / height,
                      1.0f / qw, 1.0f / qh};
    // w = soft knee width as a fraction of the threshold.
    float params[4] = {threshold, intensity, radius, 0.5f};
    bgfx::setUniform(u_bloomParams, params);
    bgfx::setUniform(u_bloomTexel, texel);
    bgfx::setTexture(0, s_texScene, bgfxColor);
    fullscreen(ViewBloomBright, m_progBloomBright,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

    // Light-source bodies re-rendered additively at diffuse *
    // intensity; the manual depth reject needs the prepass, without
    // it the bodies still glow via the bright pass alone. Handle
    // validity is not enough: with AO/volumetrics/water off the
    // prepass never ran this frame, and aoNormalZ holds another
    // camera's depths (or nothing at all) — skip the emit rather
    // than reject against garbage.
    if (prepassCurrent && bgfx::isValid(m_progBloomEmit)
            && bgfx::isValid(aoNormalZ)) {
        for (const auto *draw : bulbs) {
            if (!draw->mesh || !draw->mesh->triangleIndices)
                continue;
            GpuMesh *gpu = getMesh(*draw->mesh);
            if (!bgfx::isValid(gpu->geom->vbh)
                    || !bgfx::isValid(gpu->geom->tri))
                continue;
            const Render::Material &mat = draw->material;
            float color[4];
            // An authored colour, decoded like the mesh pass decodes
            // the same diffuse: the halo must be the hue the body
            // shades in, not its sRGB numbers scaled as light.
            unpackAuthoredColor(mat.diffuse, color, colorManaged());
            float inten = mat.lightintensity > 0.0f
                ? mat.lightintensity : 1.0f;
            for (int j = 0; j < 3; ++j)
                color[j] *= inten;
            bgfx::setUniform(u_matColor, color);
            bgfx::setUniform(u_bloomTexel, texel);
            // vs_fc_mesh reads u_params.w as an NDC depth bias (see
            // the water surface pass) — zero it explicitly, and the
            // slope term beside it.
            float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(u_params, zero);
            setPolygonOffsetUniform(nullptr);
            bgfx::setTexture(0, s_texNormalZ, aoNormalZ);
            setDrawTransform(*draw, autozoomScale, viewMatrix,
                             projMatrix, (float)height);
            setMeshVertexBuffers(gpu, *draw->mesh);
            if (draw->indexCount > 0)
                bgfx::setIndexBuffer(gpu->geom->tri,
                                     uint32_t(draw->indexStart),
                                     uint32_t(draw->indexCount));
            else
                bgfx::setIndexBuffer(gpu->geom->tri);
            uint64_t state = BGFX_STATE_WRITE_RGB
                | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                        BGFX_STATE_BLEND_ONE);
            if (mat.culling && !mat.twoside)
                state |= mat.ccw ? BGFX_STATE_CULL_CW
                                 : BGFX_STATE_CULL_CCW;
            bgfx::setState(state);
            bgfx::submit(vid(ViewBloomEmit), m_progBloomEmit);
            ++drawcount;
        }
    }

    // Separable gaussian over the halo source (dense bilinear-pair
    // kernel, the shadow blur pattern; u_viewTexel is the quarter
    // res of these views).
    float sigma = 6.0f * std::max(radius, 0.01f);
    float pairs = std::min(64.0f, std::ceil(sigma * 1.5f));
    float blurH[4] = {1.0f, 0.0f, sigma, pairs};
    bgfx::setUniform(u_bloomBlur, blurH);
    bgfx::setTexture(0, s_texBloom, bloomTex);
    fullscreen(ViewBloomBlurH, m_progBloomBlur,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    float blurV[4] = {0.0f, 1.0f, sigma, pairs};
    bgfx::setUniform(u_bloomBlur, blurV);
    bgfx::setTexture(0, s_texBloom, bloomBlurTex);
    fullscreen(ViewBloomBlurV, m_progBloomBlur,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

    // Additive composite onto the scene, scaled by the intensity.
    bgfx::setUniform(u_bloomParams, params);
    bgfx::setTexture(0, s_texBloom, bloomTex);
    fullscreen(ViewBloomApply, m_progBloomApply,
               BGFX_STATE_WRITE_RGB
               | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE));
}

void BGFXView::submitWaterSurface(const Render::DrawCall &draw,
                        float waveStrength, float waveScale,
                        float time, bool depthReject, int reflMode,
                        bool refraction, bool absorb, float absorption,
                        float inscatter, bool shadow,
                        float shadowWobble,
                        int rippleType, float rippleDensity,
                        float impactStrength, float impactLife,
                        const float (*splash)[4], bool volFront)
{
    bool planarRefl = reflMode == 3;
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh)
            || !bgfx::isValid(gpu->geom->tri))
        return;
    if (getenv("FC_BGFX_DEBUG_SUBMIT"))
        fprintf(stderr,
                "bgfx submit water surf cache=%llx start=%d num=%d\n",
                (unsigned long long)draw.mesh->cacheId,
                draw.indexStart, draw.indexCount);

    const Render::Material &mat = draw.material;
    float color[4];
    // Authored: the tint is a Beer-Lambert sigma in the shader, which
    // is arithmetic on light and wants the linear colour.
    unpackAuthoredColor(mat.diffuse, color, colorManaged());
    // The alpha channel flags the shader that a planar reflection is
    // rendered into s_texRefl (mirror-camera scene) — otherwise it
    // falls back to the environment cubemap.
    color[3] = planarRefl ? 1.0f : 0.0f;
    bgfx::setUniform(u_matColor, color);
    // The mesh vertex shader reads u_params.w as an NDC depth bias;
    // bgfx uniforms are global (commit uploads the last-set value),
    // so an unset u_params would inherit a line draw's dim-alpha 1.0
    // and push every fragment past the far plane. Same for the slope
    // term that rides beside it.
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_params, params);
    setPolygonOffsetUniform(nullptr);
    // w encodes the prepass/absorption state: 0 = no prepass,
    // 1 = prepass bound (refraction depth reject), 2 = prepass bound
    // AND the water back-face depth is available (depth absorption).
    float surf[4] = {waveStrength, waveScale, time,
                     depthReject ? (absorb ? 2.0f : 1.0f) : 0.0f};
    bgfx::setUniform(u_waterSurf, surf);
    float absorbP[4] = {absorption, inscatter, float(reflMode),
                         refraction ? 1.0f : 0.0f};
    bgfx::setUniform(u_waterAbsorb, absorbP);
    float ripple[4] = {float(rippleType), rippleDensity, 0.0f, 0.0f};
    bgfx::setUniform(u_waterRipple, ripple);
    // Particle impact map: the rings the surface raises where
    // droplets actually landed (docs/RenderEngine.md §5.8). Config
    // x = 0 disables the lookup — the sampler still needs a valid
    // bind, and any texture will do since the shader skips it.
    const bool haveImpact = impactActive && bgfx::isValid(impactTex)
        && impactStrength > 0.0f;
    bgfx::setUniform(u_waterImpact, impactFrame);
    const float impactCfg[4] = {
        haveImpact ? float(kImpactRes) : 0.0f, impactNow,
        std::max(impactLife, 0.05f), impactStrength};
    bgfx::setUniform(u_waterImpactCfg, impactCfg);
    bgfx::setTexture(7, s_texImpact,
                     haveImpact ? impactTex : sceneCopyTex);
    // Fountain splash sources: xyz = world base center, w = impact
    // ring radius (0 = slot inactive).
    static const float noSplash[kMediumSlots][4] = {};
    bgfx::setUniform(u_waterSplash, splash ? splash : noSplash,
                     kMediumSlots);
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2],
                         lightFrame ? 1.0f : 0.0f};
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightColor, lightColorI);
    bgfx::setTexture(0, s_texScene, sceneCopyTex);
    bgfx::setTexture(1, s_texEnv, m_envBuilt ? m_envTex
                                             : m_dummyEnvTex);
    // Prepass viewZ for the refraction depth reject (u_waterSurf.w
    // flags it valid; without the prepass the stage still needs a
    // bound texture, any will do since the shader skips the read).
    bgfx::setTexture(2, s_texNormalZ,
                     depthReject ? aoNormalZ : sceneCopyTex);
    // Planar reflection source (mirror-camera scene); when off, bind
    // the scene copy so the sampler is valid (the shader skips it).
    bgfx::setTexture(3, s_texRefl, planarRefl ? reflTex : sceneCopyTex);
    // Water back-face depth (pool bottom along each ray) for the
    // Beer-Lambert depth absorption of the refraction.
    bgfx::setTexture(4, s_texWaterBack, absorb ? waterBackTex : sceneCopyTex);
    // Scene-light shadow received on the surface (a shadow band + a
    // killed glint). Only when the Shadow draw style has an active
    // shadow map and the water shadow toggle is on; otherwise
    // u_shadowParams.x = 0 keeps the surface fully lit but the sampler
    // still needs a valid bind (any texture will do, the shader skips).
    bool waterShadow = shadow && shadowFrame;
    static const bool dbgvis =
        getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
    float shadowParams[4] = {waterShadow ? 1.0f : 0.0f, shadowEpsilon,
                             0.003f, dbgvis ? 1.0f : 0.0f};
    bgfx::setUniform(u_shadowParams, shadowParams);
    // The water pass has no receiver spread kernel, so u_evsm.z
    // carries the shadow wobble factor instead (the mesh receivers
    // use .zw for the Coin spread parameters).
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     shadowWobble, 0.0f};
    bgfx::setUniform(u_evsm, evsm);
    bgfx::setUniform(u_shadowMatrix, shadowMtx);
    bgfx::setTexture(5, s_texShadow,
                     waterShadow ? shadowTex : sceneCopyTex);
    // Front-segment volumetric target (raymarch output 1, or its
    // temporal-accumulation history): the media stretch between the
    // eye and the water entry, composited by this pass itself so
    // exactly the drawn pixels get it. u_volTexel.x <= 0 = off (the
    // sampler still needs a valid bind, the shader skips the read).
    bool haveVolFront = volFront && bgfx::isValid(volFrontTex);
    float hw = std::max(1.0f, std::floor(width / 2.0f));
    float hh = std::max(1.0f, std::floor(height / 2.0f));
    float volTexel[4] = {haveVolFront ? 1.0f / hw : 0.0f,
                         1.0f / hh, hw, hh};
    bgfx::setUniform(u_volTexel, volTexel);
    bgfx::setTexture(6, s_texVolFront,
                     haveVolFront
                         ? (bgfx::isValid(volHistFrontTex)
                                ? volHistFrontTex : volFrontTex)
                         : sceneCopyTex);

    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    // The mesh vertex shader needs the color stream too (bgfx drops
    // draws with unbound attributes) — bind like the normal path.
    setMeshVertexBuffers(gpu, *draw.mesh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA;
    if (mat.culling && !mat.twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    bgfx::setState(state);
    // User "water"-stage shader (docs/RenderEngine.md §5.11): the
    // user fragment program replaces fs_fc_water for this body —
    // every uniform and sampler recorded above stays available (the
    // fc_water_surface.sh core declares them; bgfx uniforms are
    // global by name). While the async compile is pending or failed
    // the stock surface stands in, never a black body.
    bgfx::ProgramHandle prog = m_progWater;
    if (mat.usershader && !mat.usershader->fragmentSource.empty()
            && mat.usershader->stage == "water") {
        bgfx::ProgramHandle uprog = _BGFXLib.getUserProgram(
            *mat.usershader, "vs_fc_mesh");
        if (bgfx::isValid(uprog)) {
            _BGFXLib.pushUserParams(*mat.usershader);
            prog = uprog;
            if (_BGFXLib.userTime[1] != 0.0f
                    && userShaderAnimated(*mat.usershader))
                _BGFXLib.userAnimatedDraw = true;
            applyUserState(*mat.usershader, state, 0, false);
        }
    }
    bgfx::submit(vid(ViewWaterSurface), prog);
    ++drawcount;
}

void BGFXView::submitLineSdf(const Render::DrawCall &draw,
                             const float *viewMatrix, bool noseam)
{
    if (!m_instancing || !draw.mesh)
        return;
    const Render::Material &mat = draw.material;
    const bool isPoint = mat.type == Render::Material::Point;
    if (!isPoint && mat.type != Render::Material::Line)
        return;
    GpuMesh *mesh = getMesh(*draw.mesh);
    if (!bgfx::isValid(mesh->geom->vbh))
        return;
    if (noseam && !isPoint)
        mesh->ensureNoSeam(*draw.mesh);
    noseam = noseam && !isPoint && bgfx::isValid(mesh->geom->lineNoSeam);
    bgfx::VertexBufferHandle inst = isPoint
        ? mesh->pointInst
        : (noseam ? mesh->lineNoSeamInst : mesh->lineInst);
    if (!bgfx::isValid(inst))
        return;

    const bool clipped = clipActiveFor(mat);
    bgfx::ProgramHandle prog = isPoint
        ? (clipped ? m_progPointSdfClip : m_progPointSdf)
        : (clipped ? m_progLineSdfClip : m_progLineSdf);
    if (!bgfx::isValid(prog))
        return;

    float color[4];
    unpackAuthoredColor(mat.diffuse, color, colorManaged());
    // u_params.y is the width the field is thresholded against, so it
    // carries exactly what the beauty pass would draw at: an unrounded
    // line width, a rounded point size.
    float params[4] = {mat.pervertexcolor ? 1.0f : 0.0f,
                       isPoint
                           ? qMax(1.0f, std::floor(mat.pointsize + 0.5f))
                           : qMax(1.0f, mat.linewidth),
                       0.0f, 1.0f};
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_params, params);
    bgfx::setTexture(3, s_texGlassFront, glassFrontTex);
    if (clipped)
        setClipUniforms(mat);
    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                     (float)height);

    uint32_t start = 0;
    uint32_t count = isPoint
        ? uint32_t(draw.mesh->numPointIndices)
        : uint32_t(noseam ? draw.mesh->numNoSeamLineIndices
                          : draw.mesh->numLineIndices) / 2;
    if (draw.indexCount > 0) {
        start = isPoint ? uint32_t(draw.indexStart)
                        : uint32_t(draw.indexStart) / 2;
        count = isPoint ? uint32_t(draw.indexCount)
                        : uint32_t(draw.indexCount) / 2;
    }
    if (count == 0)
        return;
    LineQuadVertex::init();
    bgfx::setVertexBuffer(0, m_lineQuadVb);
    bgfx::setIndexBuffer(m_lineQuadIb);
    bgfx::setInstanceDataBuffer(inst, start, count);
    // The nearest decoration wins each texel: the fragment stage puts
    // the distance to its edge on gl_FragDepth and this LESS test
    // resolves the min. No colour blending -- the winner's colour is
    // the answer, not a mixture of everyone who overlapped.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(vid(ViewGlassLineSdf), prog);
    ++drawcount;
}

void BGFXView::glassBodyColors(const Render::Material &mat, float absorb[4],
                               float tint[4]) const
{
    tint[0] = tint[1] = tint[2] = tint[3] = 1.0f;
    if (!mat.glassmtlx) {
        // Authored, decoded when the pipeline is colour managed -- the
        // same rule as the mesh pass, and the one Cycles applies to the
        // same colour before its absorption volume. Handed over encoded,
        // fs_fc_glass.sc absorbed with (1 - sRGB) where the tracer
        // absorbed with (1 - linear), 0.30 against 0.55 for a 0.7
        // channel, and its frosted scatter wash multiplied linear
        // irradiance by a display number.
        unpackAuthoredColor(mat.diffuse, absorb, colorManaged());
        return;
    }
    // The document's colour is linear already and is never decoded.
    // With a depth it is the absorption colour; without one OpenPBR
    // says it tints the transmitted light once at the surface, so the
    // body absorbs nothing and the tint carries it.
    float *dst = mat.glassdensity > 0.0f ? absorb : tint;
    absorb[0] = absorb[1] = absorb[2] = absorb[3] = 1.0f;
    for (int c = 0; c < 3; ++c)
        dst[c] = mat.glasscolor[c];
}

void BGFXView::submitGlassSurface(const Render::DrawCall &draw, bool depthReject)
{
    if (!draw.mesh || !draw.mesh->triangleIndices)
        return;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh)
            || !bgfx::isValid(gpu->geom->tri))
        return;
    if (getenv("FC_BGFX_DEBUG_SUBMIT"))
        fprintf(stderr,
                "bgfx submit glass cache=%llx start=%d num=%d\n",
                (unsigned long long)draw.mesh->cacheId,
                draw.indexStart, draw.indexCount);

    const Render::Material &mat = draw.material;
    float color[4];
    float tint[4];
    glassBodyColors(mat, color, tint);
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_glassTint, tint);
    // Like every vs_fc_mesh pairing: u_params is a global uniform,
    // an unset value would inherit a line draw's depth bias.
    float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_params, params);
    setPolygonOffsetUniform(nullptr);
    float ior = mat.glassior > 0.0f ? mat.glassior : 1.5f;
    // Automatic absorption density from the body extent: about one
    // optical depth across the diagonal (before the diffuse tint
    // weighting), like the water medium's automatic density. Not for
    // a MaterialX glass: there a zero density is the document's own
    // statement (no depth, a surface tint) and stays zero.
    float density = mat.glassdensity;
    if (mat.glassmtlx)
        density = std::max(density, 0.0f);
    else if (density <= 0.0f) {
        density = 0.0f;
        float dx = draw.bboxMax[0] - draw.bboxMin[0];
        float dy = draw.bboxMax[1] - draw.bboxMin[1];
        float dz = draw.bboxMax[2] - draw.bboxMin[2];
        if (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f) {
            float diag = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (diag > 0.0f)
                density = 3.0f / diag;
        }
    }
    float rough = std::min(std::max(mat.glassroughness, 0.0f), 1.0f);
    float glassParams[4] = {ior, density, rough,
                            depthReject ? 1.0f : 0.0f};
    bgfx::setUniform(u_glassParams, glassParams);
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2],
                         lightFrame ? 1.0f : 0.0f};
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightColor, lightColorI);
    bgfx::setTexture(0, s_texScene, sceneCopyTex);
    bgfx::setTexture(1, s_texEnv, m_envBuilt ? m_envTex
                                             : m_dummyEnvTex);
    bgfx::setTexture(2, s_texNormalZ,
                     depthReject ? aoNormalZ : sceneCopyTex);
    bgfx::setTexture(3, s_texGlassFront, glassFrontTex);
    bgfx::setTexture(4, s_texGlassBack, glassBackTex);
    bgfx::setTexture(5, s_texLineSdf, lineSdfTex);
    bgfx::setTexture(6, s_texLineSdfAux, lineSdfAuxTex);

    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height);
    setMeshVertexBuffers(gpu, *draw.mesh);

    // A MaterialX glass (docs/MaterialStorage.md sec 17.22): the body
    // stage spliced with the document's generated material function,
    // which reads the colour, roughness, IOR, depth, weight and normal
    // per fragment where the uniforms above carry one of each. The
    // flat program stands in while the compile is pending or failed,
    // and on the viewer tier, so the body is glass either way. Paired
    // with vs_fc_mesh_tex like the mesh splice, so the mesh's texture
    // coordinates ride stream 2 under the identity texture matrix --
    // the document's own uv transforms are in its graph.
    bgfx::ProgramHandle prog = m_progGlass;
    if (mat.glassmtlx && mat.usershader
            && mat.usershader->dialect == Render::UserShader::Dialect::MaterialX
            && mat.usershader->stage == "material"
            && !mat.usershader->fragmentSource.empty()) {
        bgfx::ProgramHandle uprog = _BGFXLib.getUserProgram(
            *mat.usershader, "vs_fc_mesh", false,
            BGFXRendererLibP::GlassSplice);
        if (bgfx::isValid(uprog)) {
            _BGFXLib.pushUserParams(*mat.usershader);
            pushUserImages(*mat.usershader);
            bindMeshTexCoord(gpu, *draw.mesh);
            prog = uprog;
        }
    }

    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
        | BGFX_STATE_MSAA;
    if (mat.culling && !mat.twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    bgfx::setState(state);
    bgfx::submit(vid(ViewGlassSurface), prog);
    ++drawcount;
}

void BGFXView::submitReflMedia(const float cloudParams[][4],
                     const float fireParams[][4],
                     const float fireParams2[][4],
                     const float fireFrames[][16],
                     const float fountainParams[][4],
                     const float fountainFrames[][16])
{
    if (!bgfx::isValid(m_progReflMedia))
        return;
    bgfx::setUniform(u_cloudParams, cloudParams, kMediumSlots);
    bgfx::setUniform(u_fireParams, fireParams, kMediumSlots);
    bgfx::setUniform(u_fireParams2, fireParams2, kMediumSlots);
    bgfx::setUniform(u_fireFrame, fireFrames, kMediumSlots);
    bgfx::setUniform(u_fountainParams, fountainParams,
                     kMediumSlots);
    bgfx::setUniform(u_fountainFrame, fountainFrames,
                     kMediumSlots);
    bgfx::setUniform(u_lightColor, lightColorI);
    bgfx::ProgramHandle progRefl = m_progReflMedia;
    if (volUserRefl) {
        bgfx::ProgramHandle p = _BGFXLib.getUserProgram(
            *volUserRefl, "vs_fc_comp");
        if (bgfx::isValid(p)) {
            _BGFXLib.pushUserParams(*volUserRefl);
            progRefl = p;
        }
    }
    fullscreen(ViewReflMedia, progRefl,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
               | BGFX_STATE_BLEND_FUNC(
                   BGFX_STATE_BLEND_ONE,
                   BGFX_STATE_BLEND_INV_SRC_ALPHA));
}

void BGFXView::submitGroundReflOverlay(const float bmin[3], const float bmax[3],
                             const Render::LightConfig &light,
                             const Render::GroundCamera &cam)
{
    // The same quad as submitShadowGround, and it has to be exactly the
    // same: this overlay depth-tests EQUAL against it, so a corner that
    // disagreed by a float would drop the reflection. Hence one shared
    // generator rather than a repeated formula.
    float corners[4][3];
    if (!light.groundQuad(bmin, bmax, cam, corners))
        return;
    TransientVertex::init();
    if (bgfx::getAvailTransientVertexBuffer(6, TransientVertex::ms_layout)
            < 6)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 6, TransientVertex::ms_layout);
    auto verts = reinterpret_cast<TransientVertex *>(tvb.data);
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
    float params[4] = {light.groundReflectionIntensity,
                       0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_reflParams, params);
    // Zero the mesh VS's global u_params (its .w depth bias would
    // otherwise carry over from the last line/point draw and break
    // the EQUAL depth test against the ground quad) and the slope
    // term beside it.
    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_params, zero);
    setPolygonOffsetUniform(nullptr);
    bgfx::setTexture(0, s_texScene, reflTex);
    float identity[16];
    bx::mtxIdentity(identity);
    bgfx::setTransform(identity);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB
                   | BGFX_STATE_DEPTH_TEST_EQUAL
                   | BGFX_STATE_BLEND_ALPHA
                   | BGFX_STATE_MSAA);
    bgfx::submit(vid(ViewGroundReflApply), m_progGroundRefl);
    ++drawcount;
}
