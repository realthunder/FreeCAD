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

namespace
{

// Every uniform and program init() builds is LifeProgram: only
// destroyPrograms() releases them, and a keepShared init deliberately
// does not run it -- so the resize path re-executes the whole of init()
// with that entire set still live. bgfx does dedupe a re-creation
// (createUniform by name, createProgram by its shader pair), but it
// dedupes by refcounting UP and hands back the same handle; the single
// destroy at teardown decrements once, so every resize permanently
// consumes nothing while the process runs and leaks one reference per
// handle at the end. A shutdown leak report reading "s_texVol (count
// 3)" is exactly a view that was resized twice.
//
// So creation goes through these two: they are the guard the five
// LifeProgram *texture* sites already carry, applied to the uniforms
// and programs beside them.

void ensureUniform(bgfx::UniformHandle &h, const char *name,
                   bgfx::UniformType::Enum type, uint16_t num = 1)
{
    if (!bgfx::isValid(h))
        h = bgfx::createUniform(name, type, num);
}

void ensureProgram(bgfx::ProgramHandle &h, const char *vsName,
                   const char *fsName)
{
    if (!bgfx::isValid(h))
        h = fcLoadProgram(vsName, fsName, _BGFXLib.shaderPath().c_str());
}

}  // namespace

BGFXView::~BGFXView()
{
    // Inactive sub-view banks hold sized targets of their own; sweep
    // each before the shared teardown (docs/SplitViews.md sec 9.2).
    // Their id blocks are the lib's to give back (releaseBlock).
    if (!subBanks.empty()) {
        stashSubView(subBanks[activeSub]);
        for (auto &v : subBanks) {
            loadSubView(v.second);
            destroyTargets();
        }
        subBanks.clear();
    }
    destroy();
    for (auto &v : particles)
        v.second.destroy();
    particles.clear();
    // What destroy() leaves behind is the per-view-lifetime tail
    // of the registry: the view's end is the one place it goes.
    sweepHandles(LifeView);
}

void BGFXView::destroy()
{
    destroyTargets();
    destroySceneCaches();
    destroyPrograms();
}

/// The uploaded scene: mesh and shared-geometry buffers, their textures
/// and the white color stream.
///
/// Nothing here follows the viewport — a vertex buffer does not care how
/// big the framebuffer is — and the set manages its own residency anyway:
/// getMesh() uploads on demand and collectMeshes() retires whatever no
/// draw call has referenced for two frames. So a resize leaves it alone;
/// dropping it there only bought a full re-upload of the scene on the
/// next frame.
///
/// meshes and geometries must be cleared together: GpuMesh::geom is a raw
/// pointer into the geometries map.
void BGFXView::destroySceneCaches()
{
    for (auto &v : meshes)
        v.second.destroy();
    meshes.clear();
    for (auto &v : geometries)
        v.second.destroy();
    geometries.clear();
    // The white color stream and the count that gates its recreation go
    // together. whiteColors() only rebuilds the buffer when a request
    // exceeds the count, so a live count standing over a released handle
    // is handed straight to bgfx as an invalid vertex buffer.
    if (bgfx::isValid(whiteColorVb))
        bgfx::destroy(whiteColorVb);
    whiteColorVb = BGFX_INVALID_HANDLE;
    whiteColorCount = 0;
    for (auto &v : textures)
        v.second.destroy();
    textures.clear();
}

/// GPU resources whose extent follows the viewport (or the host widget's
/// framebuffer): the scene/OIT/AO/volumetric/medium targets. A plain
/// resize drops exactly this set and nothing else.
void BGFXView::destroyTargets()
{
    // Who gave the targets back, when the question is why they keep
    // going. FC_BGFX_TARGET_DEBUG=1, because a rebuild reported at the
    // top of the frame says only that they were gone by then, and the
    // destroy that made them gone can be anywhere between two frames.
    static const bool targetDebug =
        std::getenv("FC_BGFX_TARGET_DEBUG") != nullptr;
    if (targetDebug)
        std::printf("bgfx: destroyTargets view %p widget %p sub %d fbo %d %ux%u\n",
                    (void *)this, (void *)widget,
                    int(activeSub), int(bgfxFbo.idx),
                    unsigned(width), unsigned(height));
    // The recreated moments texture starts empty, so the cached-map
    // hash resets with it (same for the AO/prepass cache and the
    // bulb shadow tiles).
    shadowMapHash = 0;
    // The sweep below takes the shadow set with the rest of the sized
    // handles, so the size it was built at is gone with it.
    shadowSize = 0;
    aoMapHash = 0;
    reflSampleIndex = -1;
    mediumSampleIndex = -1;
    camFrameHash = 0;
    for (int t = 0; t < kBulbShadowTiles; ++t) {
        bulbShadowValid[t] = false;
        bulbShadowHash[t] = 0;
    }
    aoMipCount = 0;
    sweepHandles(LifeSized);
    // The sink framebuffer owned its attachments (created with
    // destroyTextures): the sweep released them with it.
    sinkColor = BGFX_INVALID_HANDLE;
    sinkDepth = BGFX_INVALID_HANDLE;
    // Every demand-allocated group just lost its framebuffer to the
    // sweep, so each is "not allocated" again and the next frame that
    // wants one rebuilds it at the new size. The failure latches clear
    // with them: a group that could not fit at the old size is owed a
    // fresh try at the new one, which is usually smaller.
    for (int g = 0; g < NumEffectGroups; ++g)
        effectFailed[g] = false;
    // The glass sighting speaks for targets that no longer exist, and
    // a resize is the one moment a document that has moved on from
    // glass can stop paying for it: the next frame that still has a
    // glass body sets it again.
    glassSeen = false;
#ifndef FC_RENDERER_STANDALONE
    dropBlitCache();
#endif
}

void BGFXView::dropBlitCache()
{
#ifndef FC_RENDERER_STANDALONE
    if (!hasFBO)
        return;
    _BGFXLib.freeFBO(fbo);
    if (fboDepth)
        _BGFXLib.freeFBO(fboDepth);
    fboDepth = 0;
    hasFBO = false;
#endif
}

// ---------------------------------------------------------------------
// Demand-allocated effect groups.
//
// A view's render targets used to be decided by what the GPU *can* do:
// m_shadow / m_oit / m_ssao / m_vol are capability tests, so a session
// with volumetrics, water, bloom and reflections all switched off still
// paid for every one of them. At 1080p that was ~352MB of a ~554MB
// per-view target footprint, on every open 3D view, for passes that
// never ran. (The shadow maps were the one set that at least waited
// for a first use -- and then kept their 117MB for the life of the
// view, however long ago the light was switched off.)
//
// Capability still gates absolutely -- a group whose m_* flag is false
// is never allocated whatever the configuration asks for. On top of it
// each group below is built by the first frame that wants it and
// released when the configuration that wanted it goes away.
//
// ! Release is driven by CONFIGURATION, never by scene content. The
// frame's *Active flags fold in things like "this frame's scene has a
// water body" or "the shadow pass ran"; releasing on those would free
// and rebuild across ordinary editing -- the same churn collectMeshes()
// had to be taught out of, where a two-frame TTL re-uploaded 198 draws
// every fourth frame. updateEffect() is therefore called with the
// config predicate alone, and the one scene-derived group (the bulb
// atlas) is only ever asked to allocate.

bool BGFXView::effectAllocated(EffectGroup g) const
{
    switch (g) {
    case EffectVolumetric: return bgfx::isValid(volFbo);
    case EffectBulbShadow: return bgfx::isValid(bulbShadowFbo);
    case EffectReflection: return bgfx::isValid(reflFbo);
    case EffectBloom:      return bgfx::isValid(bloomFbo);
    case EffectSSAO:       return bgfx::isValid(aoPrepassFbo);
    case EffectShadow:     return bgfx::isValid(shadowFbo);
    case EffectPresent:    return bgfx::isValid(presentFbo);
    case EffectAccum:      return bgfx::isValid(accumFbo);
    default:               return false;
    }
}

/// Build one group's sized targets. Returns false if any handle came
/// back invalid -- the pools are shared with every other view, so this
/// is an ordinary outcome, not an error.
bool BGFXView::allocEffect(EffectGroup g)
{
    switch (g) {
    case EffectPresent: {
        // One full-res 8-bit target: the encoded frame. Point-sampled
        // and clamped because the blit that reads it is 1:1 -- there is
        // no filtering to be had and a linear sampler would only invite
        // a half-texel error.
        presentTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(presentTex))
            return false;
        presentFbo = bgfx::createFrameBuffer(1, &presentTex, false);
        return bgfx::isValid(presentFbo);
    }
    case EffectAccum: {
        // One full-res float target: the running average. Point-sampled
        // and clamped like the present target -- the blend that writes
        // it and the copy that reads it are both 1:1.
        //
        // Float even when the scene target is 8-bit: see accumTex for
        // why an 8-bit history stops converging after a few samples.
        accumTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(accumTex))
            return false;
        accumFbo = bgfx::createFrameBuffer(1, &accumTex, false);
        // Nothing in a fresh target is worth averaging into.
        accumFrames = 0;
        return bgfx::isValid(accumFbo);
    }
    case EffectVolumetric: {
        if (!m_vol)
            return false;
        const uint16_t hw = std::max<uint16_t>(1, width / 2);
        const uint16_t hh = std::max<uint16_t>(1, height / 2);
        // Half-res raymarch target + the front-of-water inscatter
        // segment (rgb) and its transmittance (a). The main apply does
        // its own bilateral upsample from point taps; the front apply
        // reads linearly, hence the two sampler flag sets.
        const uint64_t pointFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const uint64_t linearFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        volTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F, pointFlags);
        volFrontTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F, linearFlags);
        // History pair for the temporal accumulation; sampling flags
        // mirror the current-frame targets.
        volHistTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F, pointFlags);
        volHistFrontTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F, linearFlags);
        if (!bgfx::isValid(volTex) || !bgfx::isValid(volFrontTex)
                || !bgfx::isValid(volHistTex)
                || !bgfx::isValid(volHistFrontTex))
            return false;
        bgfx::TextureHandle volAtt[2] = {volTex, volFrontTex};
        volFbo = bgfx::createFrameBuffer(2, volAtt, false);
        bgfx::TextureHandle volHistAtt[2] = {volHistTex,
                                             volHistFrontTex};
        volHistFbo = bgfx::createFrameBuffer(2, volHistAtt, false);

        // The three medium interval pairs: full-res front/back depths
        // of water, cloud and fire body draws, written by the prepass
        // programs (only .z viewZ and .w validity are consumed). Each
        // medium kind gets its own pair -- the per-medium-kind slot
        // scheme -- so they stand or fall together with the raymarch.
        const uint64_t medFlags = pointFlags;
        struct MediumTargets {
            bgfx::TextureHandle *frontTex, *backTex;
            bgfx::TextureHandle *frontDepth, *backDepth;
            bgfx::FrameBufferHandle *frontFbo, *backFbo;
        } media[3] = {
            {&waterFrontTex, &waterBackTex, &waterFrontDepth,
             &waterBackDepth, &waterFrontFbo, &waterBackFbo},
            {&cloudFrontTex, &cloudBackTex, &cloudFrontDepth,
             &cloudBackDepth, &cloudFrontFbo, &cloudBackFbo},
            {&fireFrontTex, &fireBackTex, &fireFrontDepth,
             &fireBackDepth, &fireFrontFbo, &fireBackFbo},
        };
        for (auto &m : media) {
            *m.frontTex = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::RGBA16F, medFlags);
            *m.backTex = bgfx::createTexture2D(width, height, false, 1,
                bgfx::TextureFormat::RGBA16F, medFlags);
            *m.frontDepth = bgfx::createTexture2D(width, height, false,
                1, bgfx::TextureFormat::D24S8,
                medFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
            *m.backDepth = bgfx::createTexture2D(width, height, false,
                1, bgfx::TextureFormat::D24S8,
                medFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
            if (!bgfx::isValid(*m.frontTex) || !bgfx::isValid(*m.backTex)
                    || !bgfx::isValid(*m.frontDepth)
                    || !bgfx::isValid(*m.backDepth))
                return false;
            bgfx::TextureHandle fatt[2] = {*m.frontTex, *m.frontDepth};
            *m.frontFbo = bgfx::createFrameBuffer(2, fatt, false);
            bgfx::TextureHandle batt[2] = {*m.backTex, *m.backDepth};
            *m.backFbo = bgfx::createFrameBuffer(2, batt, false);
            if (!bgfx::isValid(*m.frontFbo) || !bgfx::isValid(*m.backFbo))
                return false;
        }
        return bgfx::isValid(volFbo) && bgfx::isValid(volHistFbo);
    }
    case EffectBulbShadow: {
        if (!m_shadow)
            return false;
        // A fixed 2048x2048 atlas: it does not follow the viewport,
        // which is why the two textures are LifeProgram and survive
        // destroyTargets(). Both creations stay guarded -- unguarded,
        // every keepShared init (i.e. every resize) overwrote handles
        // whose textures were still live, orphaning RG32F+D24S8 at
        // 2048^2, 50MB a resize for the life of the process. After a
        // resize only bulbShadowFbo is gone and only it is rebuilt.
        const uint16_t side =
            uint16_t(kBulbShadowGrid * kBulbShadowTileSize);
        if (!bgfx::isValid(bulbShadowTex))
            bulbShadowTex = bgfx::createTexture2D(side, side, false, 1,
                shadowFormat,
                BGFX_TEXTURE_RT
                | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(bulbShadowDepth))
            bulbShadowDepth = bgfx::createTexture2D(side, side, false, 1,
                bgfx::TextureFormat::D24S8,
                BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(bulbShadowTex)
                || !bgfx::isValid(bulbShadowDepth))
            return false;
        bgfx::TextureHandle bsAtt[2] = {bulbShadowTex, bulbShadowDepth};
        bulbShadowFbo = bgfx::createFrameBuffer(2, bsAtt, false);
        return bgfx::isValid(bulbShadowFbo);
    }
    case EffectReflection: {
        // The mirrored-camera scene re-render target. This full opaque
        // re-render is the priciest effect on a large window, so it
        // scales to effW/effH (Render_EffectResolution); LINEAR
        // sampling upscales it smoothly onto the full-res consumers
        // (ground overlay, water surface), which read normalized UV.
        reflTex = bgfx::createTexture2D(effW, effH, false, 1,
            bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        reflDepth = bgfx::createTexture2D(effW, effH, false, 1,
            bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (!bgfx::isValid(reflTex) || !bgfx::isValid(reflDepth))
            return false;
        bgfx::TextureHandle ratt[2] = {reflTex, reflDepth};
        reflFbo = bgfx::createFrameBuffer(2, ratt, false);
        return bgfx::isValid(reflFbo);
    }
    case EffectSSAO: {
        if (!m_ssao)
            return false;
        const auto *caps = bgfx::getCaps();
        const uint64_t aoFlags = 0
            | BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT
            | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP
            | BGFX_SAMPLER_V_CLAMP;
        // The geometry prepass (aoNormalZ/aoDepth) stays full-res and
        // POINT-sampled: refraction/glass reject, volumetric ray-ends and
        // water span all read its exact eye-space depth, which bilinear
        // upscaling would corrupt at silhouettes. The AO resolve targets
        // (aoTex raw, aoBlurTex blurred) scale to ssaoW/ssaoH -- their OWN
        // Render_SSAOResolution, independent of the reflection scale
        // (effW/effH) -- and sample LINEAR, so a reduced-res AO upsamples
        // smoothly. SSAO is resolution-sensitive (contact/crevice detail),
        // so it defaults to full-res rather than sharing effectResolution,
        // whose reduction produced visibly blocky occlusion.
        // Full-float normal+depth when available: fp16 viewZ (~10-bit
        // mantissa) quantizes zoomed-in depths so hard that the GTAO
        // horizon estimate bands along iso-depth contours (ripples on
        // curved faces, stair strips on oblique flat ones near
        // contacts). Point-sampled, so fp32 is safe on WebGL2/mobile
        // (their float32 restriction is LINEAR filtering).
        aoNormalZFp16 = !(caps->formats[bgfx::TextureFormat::RGBA32F]
                          & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        aoNormalZ = bgfx::createTexture2D(width, height, false, 1,
            aoNormalZFp16 ? bgfx::TextureFormat::RGBA16F
                          : bgfx::TextureFormat::RGBA32F, aoFlags);
        aoDepth = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::D24S8,
            aoFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        const uint64_t aoResFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;  // linear
        aoTex = bgfx::createTexture2D(ssaoW, ssaoH, false, 1,
            bgfx::TextureFormat::R8, aoResFlags);
        aoBlurTex = bgfx::createTexture2D(ssaoW, ssaoH, false, 1,
            bgfx::TextureFormat::R8, aoResFlags);
        if (!bgfx::isValid(aoNormalZ) || !bgfx::isValid(aoDepth)
                || !bgfx::isValid(aoTex) || !bgfx::isValid(aoBlurTex))
            return false;
        bgfx::TextureHandle preatt[2] = {aoNormalZ, aoDepth};
        aoPrepassFbo = bgfx::createFrameBuffer(2, preatt, false);
        aoGenFbo = bgfx::createFrameBuffer(1, &aoTex, false);
        aoBlurFbo = bgfx::createFrameBuffer(1, &aoBlurTex, false);
        if (!bgfx::isValid(aoPrepassFbo) || !bgfx::isValid(aoGenFbo)
                || !bgfx::isValid(aoBlurFbo))
            return false;

        // 4x4 tiled random rotation vectors (xy packed *0.5+0.5),
        // fixed values so frames are deterministic. The .z channel packs a
        // 4x4 Bayer dither (0..255): the gen pass uses it to jitter the
        // per-pixel sample radius, so neighbouring pixels sample at
        // different distances and the 4x4 blur (one Bayer tile) averages
        // all 16 sub-radii -- decorrelating the RADIAL occlusion banding
        // that the azimuthal-only rotation leaves behind.
        static const uint8_t noise[64] = {
            0xa2, 0x05, 0x00, 0xff, 0x11, 0xc0, 0x88, 0xff,
            0xee, 0xc0, 0x22, 0xff, 0x25, 0xd9, 0xaa, 0xff,
            0x27, 0x23, 0xcc, 0xff, 0x63, 0x03, 0x44, 0xff,
            0x20, 0xd4, 0xee, 0xff, 0x2a, 0xde, 0x66, 0xff,
            0x90, 0xfe, 0x33, 0xff, 0x9b, 0xfc, 0xbb, 0xff,
            0xae, 0x09, 0x11, 0xff, 0xef, 0xbe, 0x99, 0xff,
            0xd8, 0x23, 0xff, 0xff, 0x3a, 0x15, 0x77, 0xff,
            0x00, 0x87, 0xdd, 0xff, 0xe8, 0xc9, 0x55, 0xff,
        };
        aoNoiseTex = bgfx::createTexture2D(4, 4, false, 1,
            bgfx::TextureFormat::RGBA8,
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT,
            bgfx::copy(noise, sizeof(noise)));
        if (!bgfx::isValid(aoNoiseTex))
            return false;

        // GTAO depth pyramid: single-channel viewZ, POINT-sampled
        // (the gen pass picks a discrete level per tap). R32F when
        // renderable, else R16F -- coarse levels only serve FAR taps,
        // whose coplanarity guard already tolerates fp16 steps; the
        // near taps keep reading the full-precision prepass.
        //
        // The pyramid is the one part that degrades on its own: with no
        // levels the GTAO gen reads the prepass at every tap, so a
        // failure here costs quality, not the group.
        const bool mipR32 = 0 != (caps->formats[bgfx::TextureFormat::R32F]
                                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        const bool mipR16 = 0 != (caps->formats[bgfx::TextureFormat::R16F]
                                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        aoMipCount = (mipR32 || mipR16)
                && bgfx::isValid(m_progGtaoDepth) ? kAOMipLevels : 0;
        for (int m = 0; m < aoMipCount; ++m) {
            const uint16_t mw = uint16_t(std::max(1, width >> (m + 1)));
            const uint16_t mh = uint16_t(std::max(1, height >> (m + 1)));
            aoMipTex[m] = bgfx::createTexture2D(mw, mh, false, 1,
                mipR32 ? bgfx::TextureFormat::R32F
                       : bgfx::TextureFormat::R16F, aoFlags);
            if (bgfx::isValid(aoMipTex[m]))
                aoMipFbo[m] = bgfx::createFrameBuffer(1, &aoMipTex[m],
                                                      false);
            if (!bgfx::isValid(aoMipFbo[m])) {
                // Keep the levels that did land; the gen pass reads
                // aoMipCount, so the tail simply is not there.
                aoMipCount = m;
                break;
            }
        }

        // Glass body absorption interval: full-res front/back
        // depths of glass draws, written by the prepass programs
        // like the water medium interval (only .z viewZ and .w
        // validity are consumed). In this group because the depth
        // writer is the prepass shader family -- and because a glass
        // body is what asks for the prepass in the first place when
        // nothing else does.
        const uint64_t glassFlags = 0
            | BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        glassFrontTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, glassFlags);
        glassBackTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, glassFlags);
        glassFrontDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            glassFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        glassBackDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            glassFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(glassFrontTex) || !bgfx::isValid(glassBackTex)
                || !bgfx::isValid(glassFrontDepth)
                || !bgfx::isValid(glassBackDepth))
            return false;
        // The line distance field the glass surface resamples. LINEAR,
        // unlike every other target here: the whole method rests on the
        // field interpolating smoothly between texels, and a point
        // sample would give it stair-stepped values to reconstruct
        // coverage from. The aux target rides along with the line's
        // perpendicular axis, view depth and half width
        // (fc_line_sdf_fs.sh has the layout).
        const uint64_t sdfFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        lineSdfTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, sdfFlags);
        lineSdfAuxTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, sdfFlags);
        lineSdfDepth = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::D24S8,
            sdfFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(lineSdfTex) || !bgfx::isValid(lineSdfAuxTex)
                || !bgfx::isValid(lineSdfDepth))
            return false;
        bgfx::TextureHandle sdfatt[3] = {lineSdfTex, lineSdfAuxTex,
                                         lineSdfDepth};
        lineSdfFbo = bgfx::createFrameBuffer(3, sdfatt, false);
        if (!bgfx::isValid(lineSdfFbo))
            return false;
        bgfx::TextureHandle gfatt[2] = {glassFrontTex, glassFrontDepth};
        glassFrontFbo = bgfx::createFrameBuffer(2, gfatt, false);
        bgfx::TextureHandle gbatt[2] = {glassBackTex, glassBackDepth};
        glassBackFbo = bgfx::createFrameBuffer(2, gbatt, false);
        return bgfx::isValid(glassFrontFbo) && bgfx::isValid(glassBackFbo);
    }
    case EffectBloom: {
        // Quarter-res RGBA16F halo source + blur ping target.
        const uint16_t qw = uint16_t(std::max(1, int(width) / 4));
        const uint16_t qh = uint16_t(std::max(1, int(height) / 4));
        const uint64_t bloomFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        bloomTex = bgfx::createTexture2D(qw, qh, false, 1,
            bgfx::TextureFormat::RGBA16F, bloomFlags);
        bloomBlurTex = bgfx::createTexture2D(qw, qh, false, 1,
            bgfx::TextureFormat::RGBA16F, bloomFlags);
        if (!bgfx::isValid(bloomTex) || !bgfx::isValid(bloomBlurTex))
            return false;
        bloomFbo = bgfx::createFrameBuffer(1, &bloomTex, false);
        bloomBlurFbo = bgfx::createFrameBuffer(1, &bloomBlurTex, false);
        return bgfx::isValid(bloomFbo) && bgfx::isValid(bloomBlurFbo);
    }
    case EffectShadow: {
        if (!m_shadow)
            return false;
        // The one group sized by a setting, not by the viewport:
        // ensureShadowTargets() builds the whole set (moments + depth,
        // the blur ping and its write-back, the glass tint pair) at
        // the extent ShadowPrecision asked for, and is also what a
        // later precision change comes back through.
        ensureShadowTargets(shadowSizeWanted);
        return bgfx::isValid(shadowFbo);
    }
    default:
        return false;
    }
}

/// Release one group's handles. Framebuffers strictly before the
/// textures they reference, like forEachHandle's order.
void BGFXView::freeEffect(EffectGroup g)
{
    auto drop = [](auto &h) {
        if (bgfx::isValid(h)) {
            bgfx::destroy(h);
            h = BGFX_INVALID_HANDLE;
        }
    };
    switch (g) {
    case EffectVolumetric:
        for (auto *h : {&volFbo, &volHistFbo, &waterFrontFbo,
                        &waterBackFbo, &cloudFrontFbo, &cloudBackFbo,
                        &fireFrontFbo, &fireBackFbo})
            drop(*h);
        for (auto *h : {&volTex, &volFrontTex, &volHistTex,
                        &volHistFrontTex, &waterFrontTex, &waterBackTex,
                        &waterFrontDepth, &waterBackDepth,
                        &cloudFrontTex, &cloudBackTex, &cloudFrontDepth,
                        &cloudBackDepth, &fireFrontTex, &fireBackTex,
                        &fireFrontDepth, &fireBackDepth})
            drop(*h);
        break;
    case EffectBulbShadow:
        drop(bulbShadowFbo);
        drop(bulbShadowTex);
        drop(bulbShadowDepth);
        // The tiles cached in the released atlas are gone with it.
        for (int t = 0; t < kBulbShadowTiles; ++t) {
            bulbShadowValid[t] = false;
            bulbShadowHash[t] = 0;
        }
        break;
    case EffectReflection:
        drop(reflFbo);
        reflSampleIndex = -1;
        drop(reflTex);
        drop(reflDepth);
        break;
    case EffectPresent:
        drop(presentFbo);
        drop(presentTex);
#ifndef FC_RENDERER_STANDALONE
        // The desktop blit caches a GL framebuffer around whichever
        // texture it reads; that one is gone, so the cache has to be
        // rebuilt. (Standalone presents onto the default backbuffer and
        // has no such cache -- nor this group, which it never allocates.)
        hasFBO = false;
#endif
        break;
    case EffectAccum:
        drop(accumFbo);
        drop(accumTex);
        // The history is the target; without it there is nothing
        // accumulated, whatever the counter last said.
        accumFrames = 0;
        break;
    case EffectBloom:
        drop(bloomFbo);
        drop(bloomBlurFbo);
        drop(bloomTex);
        drop(bloomBlurTex);
        break;
    case EffectSSAO:
        drop(aoPrepassFbo);
        drop(aoGenFbo);
        drop(aoBlurFbo);
        for (auto &h : aoMipFbo)
            drop(h);
        drop(glassFrontFbo);
        drop(glassBackFbo);
        drop(aoNormalZ);
        drop(aoDepth);
        drop(aoTex);
        drop(aoBlurTex);
        drop(aoNoiseTex);
        for (auto &h : aoMipTex)
            drop(h);
        drop(glassFrontTex);
        drop(glassBackTex);
        drop(glassFrontDepth);
        drop(glassBackDepth);
        // The pyramid is gone, and so is the cached prepass the AO
        // chain would otherwise be told to reuse.
        aoMipCount = 0;
        aoMapHash = 0;
        break;
    case EffectShadow:
        for (auto *h : {&shadowFbo, &shadowBlurFbo, &shadowBlurBackFbo,
                        &shadowTintFbo, &shadowTintBlurFbo,
                        &shadowTintBlurBackFbo})
            drop(*h);
        for (auto *h : {&shadowTex, &shadowDepth, &shadowBlurTex,
                        &shadowTintTex, &shadowTintBlurTex})
            drop(*h);
        // No map, no size and no cached moments: the next allocation
        // is a first build again, at whatever precision then asks for.
        shadowSize = 0;
        shadowMapHash = 0;
        break;
    default:
        break;
    }
}

/// Reconcile one group against this frame's demand. \a want must be a
/// pure configuration predicate -- see the note above.
void BGFXView::updateEffect(EffectGroup g, bool want)
{
    if (want) {
        // A group is normally either there or not, but the shadow set
        // also has an extent of its own (ShadowPrecision). Changing it
        // is a configuration change like switching the group off and on
        // again, so the old set goes back -- including its failure
        // latch, since a smaller map is a real chance to fit where the
        // previous one did not.
        if (g == EffectShadow && shadowSize != shadowSizeWanted
                && effectAllocated(g)) {
            freeEffect(g);
            effectFailed[g] = false;
        }
        if (effectAllocated(g) || effectFailed[g])
            return;
        if (!allocEffect(g)) {
            // ! Latch it. A failed allocation must not be retried every
            // frame: the retry is what turned a full pool into a spin
            // last time -- a bailed frame never reaches bgfx::frame(),
            // which is the only place bgfx reclaims, so the view sat
            // waiting on handles it was itself preventing from coming
            // back. The latch clears on a resize (destroyTargets) and
            // whenever the configuration turns the group off and on
            // again, both of which are real chances to succeed.
            effectFailed[g] = true;
            freeEffect(g);   // drop whatever part of the set did land
            static const char *const kNames[NumEffectGroups] = {
                "volumetric", "bulb shadow", "reflection", "bloom",
                "AO prepass", "shadow map", "output transform",
                "temporal accumulation"};
            std::printf("bgfx: no render target handles for the %s "
                        "effect -- it stays off in this view\n",
                        kNames[g]);
        }
    } else if (effectAllocated(g)) {
        freeEffect(g);
        effectFailed[g] = false;
    }
}

/// Shader programs, uniforms and the stand-in textures. Linking them is
/// the single most expensive thing this class does — Intel's GL driver
/// JITs this set for ~5 s — so a plain resize must never come through
/// here; only a shader-generation or MSAA change does, which is what
/// init(keepShared) selects. The environment and hatch textures live with
/// the programs, so their built-state flags reset with them.
void BGFXView::destroyPrograms()
{
    m_envBuilt = false;
    m_hatchVersion = 0;
    sweepHandles(LifeProgram);
}

void BGFXView::sweepHandles(HandleLife life)
{
    forEachHandle([life](auto &h, HandleLife l) {
        if (l == life)
            releaseHandle(h);
    });
}

bgfx::TextureHandle BGFXView::createTexture(bgfx::TextureFormat::Enum format, uint64_t flags,
                                  bool sampled)
{
    // A sampled attachment drops WRITE_ONLY: with MSAA bgfx then
    // backs it with a multisampled renderbuffer plus a resolve
    // texture (auto-resolved on framebuffer switch, the WBOIT
    // pattern), without MSAA it becomes a plain sampleable texture.
    const uint64_t tsFlags = 0
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
        | (sampled ? 0 : BGFX_TEXTURE_RT_WRITE_ONLY);
    return bgfx::createTexture2D(width, height, false, 1, format, tsFlags | flags);
}

/// Rebuild the view's GPU resources.
///
/// With \a keepShared everything that does not follow the viewport is left
/// in place — the shader programs, uniforms and stand-in textures, plus
/// the uploaded scene — and only the sized targets are recreated. That is
/// the resize path. Every creation below is guarded on the handle still
/// being invalid, so the kept handles are simply skipped, and the scene
/// caches repopulate through getMesh() as draws reference them.
///
/// Pass false whenever the programs themselves must change: a shader
/// generation bump, or an MSAA change (which re-decides m_oit and so which
/// program set exists at all).
void BGFXView::init(bool keepShared)
{
    if (keepShared)
        destroyTargets();
    else
        destroy();
#ifdef FC_RENDERER_STANDALONE
    // The sub-view rect while a renderSubViews submit is sizing us,
    // the canvas otherwise (docs/SplitViews.md sec 9.2).
    width = _BGFXLib.viewTargetWidth();
    height = _BGFXLib.viewTargetHeight();
    // The sampled scene color under MSAA becomes a multisampled
    // renderbuffer plus a resolve texture; the present pass samples
    // the resolve (WebGL2 backs both via renderbufferStorageMultisample
    // + blitFramebuffer).
    int samples = _BGFXLib.standaloneSamples;
    msaaSamples = samples;
#else
    width = uint16_t(_BGFXLib.viewWidth(widget));
    height = uint16_t(_BGFXLib.viewHeight(widget));

    // bgfx owns MSAA in its own offscreen target: prefer the preference
    // override (BGFXRenderer::setMSAASamples) over the host widget's GL
    // format, so an AntiAliasing change need not recreate the Qt view.
    int samples = _BGFXLib.desktopSamples >= 0
        ? _BGFXLib.desktopSamples
        : widget->format().samples();
    msaaSamples = samples;
#endif
    std::printf("bgfx: view init %ux%u msaa %d\n",
                unsigned(width), unsigned(height), msaaSamples);
    shaderGen = _BGFXLib.shaderGeneration;
    // Re-judged at the bottom, against the pack this generation loads.
    shaderFailed = false;
    // Resolution of the scaled effect targets (reflection re-render, SSAO
    // resolve). effectScale clamps to [0.25, 1]; the targets and their
    // view rects use effW/effH while the main scene / geometry prepass use
    // width/height. The half-res volumetric follows the same pattern.
    effectScale = _BGFXLib.effectResolution;
    {
        float es = std::min(std::max(effectScale, 0.25f), 1.0f);
        effW = uint16_t(std::max(1, int(width * es + 0.5f)));
        effH = uint16_t(std::max(1, int(height * es + 0.5f)));
    }
    // SSAO resolve resolution is independent of effectScale (its own
    // Render_SSAOResolution): AO is resolution-sensitive, so it does not
    // share the reflection scale.
    ssaoScale = _BGFXLib.ssaoResolution;
    {
        float ss = std::min(std::max(ssaoScale, 0.25f), 1.0f);
        ssaoW = uint16_t(std::max(1, int(width * ss + 0.5f)));
        ssaoH = uint16_t(std::max(1, int(height * ss + 0.5f)));
    }
    uint64_t flags = 0;
    if (samples >= 8)
        flags = BGFX_TEXTURE_RT_MSAA_X8;
    else if (samples >= 4)
        flags = BGFX_TEXTURE_RT_MSAA_X4;
    else if (samples >= 2)
        flags = BGFX_TEXTURE_RT_MSAA_X2;
    else
        flags = BGFX_TEXTURE_RT;

    // The scene color stays sampleable for the water surface
    // refraction copy (and future screen-space effects).
    //
    // Floating point while colour managed: what this target holds is
    // then LINEAR light, and eight bits of linear light spend most of
    // their codes on highlights the eye cannot separate while banding
    // the darks it can. It also clips at one, which would throw away
    // exactly the headroom the exposure stage exists to recover. The
    // answer is resolved once, here, so every consumer sees one format
    // for the life of these targets.
    hdrScene = hdrSceneWanted();
    const bgfx::TextureFormat::Enum sceneFormat = hdrScene
        ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;
    bgfxColor = createTexture(sceneFormat, flags, true);
    //GL_DEPTH24_STENCIL8
    // NOTE: the MSAA levels are an enum in the RT flag nibble, not
    // orthogonal bits — masking BGFX_TEXTURE_RT out of them would
    // turn MSAA_X4 (0x3) into MSAA_X2 (0x2) and desync the depth
    // sample count from the color attachment's.
    // Sampleable, always: the ViewCaptureDepth encode reads this
    // attachment, and it must never be a REBUILD that makes it
    // readable. accumTex is life-sized, so a rebuild destroys the idle
    // temporal accumulation and zeroes accumFrames -- and latching the
    // demand on the first capture put that rebuild AFTER the run had
    // settled, where nothing waits for the average to re-converge:
    // frameComplete means every user shader compiled and every
    // deferred shape arrived, and says nothing about convergence.
    //
    // ! That is a real hazard and NOT a measured defect. It was
    // written to explain a 1-LSB drift on the raster/mode0 golden, and
    // that explanation was WRONG -- the image is byte-identical with
    // the latch, without it, and at the commit before the capture work
    // existed, and it is bit-stable across runs. The drift predates all
    // of this and is still unattributed. So do not reintroduce the
    // latch on the grounds that its cost was never observed; the reason
    // to build it up front is that a mid-run rebuild of the
    // accumulation is not something a capture should be able to cause,
    // whether or not a golden happens to show it.
    //
    // The cost is a write-only attachment becoming a plain sampleable
    // one, which is the same memory without MSAA -- and MSAA is off by
    // default (View3DInventorViewer::getNumSamples), analytic line
    // coverage having removed the reason it used to be mandatory. With
    // MSAA on it adds a resolve texture, which is the honest price of a
    // capture that works on every backend.
    bgfxDepth = createTexture(bgfx::TextureFormat::D24S8, flags, true);
    bgfx::Attachment attachment[2];
    // No mip chain on these render targets; the default resolve flag
    // (BGFX_RESOLVE_AUTO_GEN_MIPS) is also rejected for depth attachments.
    attachment[0].init(bgfxColor, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    attachment[1].init(bgfxDepth, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    // ! An exhausted handle pool must not be a crash. bgfx returns an
    // invalid handle from createTexture, and createFrameBuffer ASSERTS
    // on an invalid attachment ("Invalid texture attachment"), which in
    // a debug build is a SIGTRAP -- so running out of framebuffers took
    // the application down rather than costing a frame. Leaving the
    // scene framebuffer invalid is the outcome the frame path already
    // knows how to read: it bails to the host and Coin draws.
    if (bgfx::isValid(bgfxColor) && bgfx::isValid(bgfxDepth))
        bgfxFbo = bgfx::createFrameBuffer(2, attachment, true);
    else
        bgfxFbo = BGFX_INVALID_HANDLE;
    // Either pool can be the one that ran dry -- an invalid attachment
    // (textures) or an invalid framebuffer -- and both end here, so the
    // latch is read off the result rather than off which call failed.
    targetsFailed = !bgfx::isValid(bgfxFbo);
    if (targetsFailed) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::printf("bgfx: out of render target handles -- this view "
                        "falls back to Coin rendering\n");
        }
    }

    // The scene framebuffer is bound per view id per frame (the pass
    // map moves ids between passes from one frame to the next), so
    // there is deliberately no bind-the-whole-block loop here: it
    // would bind stale ids, and with a block narrower than NUM_VIEWS
    // it would reach into the next viewer's block.
    //
    // The discard target every unmarked pass maps to. 1x1 with its
    // own depth: a draw that lands here is clipped to a pixel of
    // scratch instead of the scene, so a mispredicted pass shows up
    // as missing pixels and a warning rather than corruption.
    sinkColor = bgfx::createTexture2D(1, 1, false, 1,
                                      bgfx::TextureFormat::RGBA8,
                                      BGFX_TEXTURE_RT);
    sinkDepth = bgfx::createTexture2D(1, 1, false, 1,
                                      bgfx::TextureFormat::D24S8,
                                      BGFX_TEXTURE_RT);
    {
        bgfx::TextureHandle sinkAtt[2] = {sinkColor, sinkDepth};
        sinkFbo = bgfx::createFrameBuffer(2, sinkAtt, true);
    }

    // Bloom (glow) chain. The quarter-res halo/blur pair is
    // demand-allocated by ensureEffect(EffectBloom); only the programs
    // and uniforms are built here, because bgfx shares those across
    // every view and relinking them is the expensive part.
    {
        ensureProgram(m_progBloomBright, "vs_fc_comp", "fs_fc_bloom_bright");
        ensureProgram(m_progBloomEmit, "vs_fc_mesh", "fs_fc_bloom_emit");
        ensureProgram(m_progBloomBlur, "vs_fc_comp", "fs_fc_bloom_blur");
        ensureProgram(m_progBloomApply, "vs_fc_comp", "fs_fc_bloom_apply");
        ensureUniform(s_texBloom, "s_texBloom", bgfx::UniformType::Sampler);
        ensureUniform(u_bloomParams, "u_bloomParams", bgfx::UniformType::Vec4);
        ensureUniform(u_bloomTexel, "u_bloomTexel", bgfx::UniformType::Vec4);
        ensureUniform(u_bloomBlur, "u_bloomBlur", bgfx::UniformType::Vec4);
    }

    // Visible sun disc along the directional scene light.
    ensureProgram(m_progSun, "vs_fc_comp", "fs_fc_sun");
    ensureUniform(u_sunParams, "u_sunParams", bgfx::UniformType::Vec4);

    // PBR environment drawn as the visible background.
    ensureProgram(m_progEnvBg, "vs_fc_comp", "fs_fc_env");

    // The present pass: the output colour transform, and standalone
    // also the only thing that puts the frame on the default backbuffer
    // (no Qt framebuffer to GL-blit into there).
    ensureProgram(m_progPresent, "vs_fc_comp", "fs_fc_present");
    ensureUniform(s_texScene, "s_texScene", bgfx::UniformType::Sampler);
    // The capture depth encode (readbackCapture). A program and a
    // sampler cost nothing until a capture asks for the pass, and
    // building them here keeps every program creation in one place.
    ensureProgram(m_progDepthEnc, "vs_fc_comp", "fs_fc_depthenc");
    ensureUniform(s_texSceneDepth, "s_texSceneDepth",
                  bgfx::UniformType::Sampler);
    ensureUniform(u_outputParams, "u_outputParams",
                  bgfx::UniformType::Vec4);
    // The other half of the same setting: the vertex stages decode
    // the authored 8-bit colour streams through it (fc_color.sh).
    ensureUniform(u_colorSpace, "u_colorSpace",
                  bgfx::UniformType::Vec4);

    ensureProgram(m_progMesh, "vs_fc_mesh", "fs_fc_mesh");
    ensureProgram(m_progFlat, "vs_fc_flat", "fs_fc_flat");
    ensureProgram(m_progMeshClip, "vs_fc_mesh_clip", "fs_fc_mesh_clip");
    ensureProgram(m_progFlatClip, "vs_fc_flat_clip", "fs_fc_flat_clip");
    ensureProgram(m_progMeshTex, "vs_fc_mesh_tex", "fs_fc_mesh_tex");
    ensureProgram(m_progMeshTexClip, "vs_fc_mesh_tex_clip",
                  "fs_fc_mesh_tex_clip");
    ensureUniform(s_texColor, "s_texColor", bgfx::UniformType::Sampler);
    ensureUniform(u_texMatrix, "u_texMatrix", bgfx::UniformType::Mat4);
    ensureUniform(u_texParams, "u_texParams", bgfx::UniformType::Vec4);
    ensureUniform(u_texBlendColor, "u_texBlendColor", bgfx::UniformType::Vec4);

    // Thick lines: instanced screen-space quad expansion (there is no
    // fixed-function line width in modern APIs). Without instancing
    // support every line falls back to 1px primitives.
    m_instancing =
        (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) != 0;
    // Publish the capability so geometry producers (Part tessellation)
    // know whether shared-instance scene structure will actually batch.
    Render::Renderer::setInstancingHint(m_instancing);
    if (m_instancing) {
        // Cross-object instancing: identical placements of one shared
        // geometry cache (Link arrays) collapse into a single
        // instanced submit carrying {model matrix, diffuse} per
        // instance.
        ensureProgram(m_progMeshInst, "vs_fc_mesh_inst", "fs_fc_mesh");
        ensureProgram(m_progMeshInstTex, "vs_fc_mesh_tex_inst",
                      "fs_fc_mesh_tex");
        ensureUniform(u_instParams, "u_instParams", bgfx::UniformType::Vec4);
        ensureProgram(m_progLine, "vs_fc_line", "fs_fc_line");
        ensureProgram(m_progLineClip, "vs_fc_line_clip",
                      "fs_fc_line_clip");
        ensureProgram(m_progLinePat, "vs_fc_line_pat", "fs_fc_line_pat");
        ensureProgram(m_progLinePatClip, "vs_fc_line_pat_clip",
                      "fs_fc_line_pat_clip");
        ensureProgram(m_progLineSdf, "vs_fc_line_sdf", "fs_fc_line_sdf");
        ensureProgram(m_progLineSdfClip, "vs_fc_line_sdf_clip",
                      "fs_fc_line_sdf_clip");
        ensureProgram(m_progPointSdf, "vs_fc_point_sdf",
                      "fs_fc_point_sdf");
        ensureProgram(m_progPointSdfClip, "vs_fc_point_sdf_clip",
                      "fs_fc_point_sdf_clip");
        ensureProgram(m_progPoint, "vs_fc_point", "fs_fc_flat");
        ensureProgram(m_progPointClip, "vs_fc_point_clip", "fs_fc_flat_clip");
        LineQuadVertex::init();
        static const LineQuadVertex quad[4] = {
            {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
        };
        static const uint16_t quadIndices[6] = {0, 1, 2, 1, 3, 2};
        m_lineQuadVb = bgfx::createVertexBuffer(
            bgfx::makeRef(quad, sizeof(quad)),
            LineQuadVertex::ms_layout);
        m_lineQuadIb = bgfx::createIndexBuffer(
            bgfx::makeRef(quadIndices, sizeof(quadIndices)));
    }

    // Section caps: the cap quad fill with hatch texture modulation.
    // The stencil mark and cleanup passes reuse the flat programs.
    ensureProgram(m_progCap, "vs_fc_cap", "fs_fc_cap");
    ensureProgram(m_progCapClip, "vs_fc_cap_clip", "fs_fc_cap_clip");
    ensureUniform(s_texHatch, "s_texHatch", bgfx::UniformType::Sampler);
    // 1x1 white stand-in so the cap program samples neutrally when
    // hatching is disabled (and for the stencil cleanup pass).
    //
    // ! Guarded, like every LifeProgram creation on this path must be.
    // A keepShared init runs only destroyTargets(), which sweeps
    // LifeSized and leaves LifeProgram handles live -- so an
    // unguarded create here overwrites a handle that still owns its
    // texture, and the old one is orphaned for the life of the
    // process. Cheap at 1x1; the same omission cost 50MB a resize on
    // the bulb shadow atlas below.
    static const uint32_t white = 0xffffffff;
    if (!bgfx::isValid(m_whiteTex))
        m_whiteTex = bgfx::createTexture2D(1, 1, false, 1,
            bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(&white, sizeof(white)));
    // 1x1 fully transparent black: a stand-in whose *coverage* is
    // zero, for debug binds where the white texture would read as a
    // target full of opaque white (RenderDebug mode 9).
    static const uint32_t black = 0x00000000;
    if (!bgfx::isValid(m_blackTex))
        m_blackTex = bgfx::createTexture2D(1, 1, false, 1,
            bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(&black, sizeof(black)));
    CapVertex::init();

    // PBR: the mesh programs always carry the environment sampler
    // (the branch is uniform-selected); a 1x1 black cube stands in
    // while PBR is off or unavailable. The real environment is built
    // on demand (ensureEnvironment).
    ensureUniform(s_texEnv, "s_texEnv", bgfx::UniformType::Sampler);
    ensureUniform(u_pbrParams, "u_pbrParams", bgfx::UniformType::Vec4);
    ensureUniform(u_matcapParams, "u_matcapParams", bgfx::UniformType::Vec4);
    ensureUniform(u_envSH, "u_envSH", bgfx::UniformType::Vec4, kEnvSH);
    // Bump mapping of the textured mesh programs (unit 2; the 1x1
    // white cap texture stands in when a draw has no bump map).
    ensureUniform(s_texBump, "s_texBump", bgfx::UniformType::Sampler);
    ensureUniform(u_bumpParams, "u_bumpParams", bgfx::UniformType::Vec4);
    // Machined surface finish of the mesh programs (all of them, not
    // just the textured ones: the pattern is procedural over object
    // space and needs no texture coordinates).
    // An array: entry 0 is the draw's own finish, and a per-face-finished
    // draw fills the rest with its palette (Render::MaxFinishPalette
    // must match the shader's FC_FINISH_PALETTE).
    ensureUniform(u_finishParams, "u_finishParams", bgfx::UniformType::Vec4,
                  Render::MaxFinishPalette);
    // The projection frames that finish is laid out in: three vec4 per
    // frame, entry 0 the draw's own. Kind 0 is the unframed frame --
    // the triplanar projection that predates these -- so a zero upload
    // reads as the old behaviour. Render::MaxFramePalette must match
    // the shader's FC_FRAME_PALETTE.
    ensureUniform(u_frameParams, "u_frameParams", bgfx::UniformType::Vec4,
                  Render::MaxFramePalette * 3);
    // Emissive/occlusion material maps of the textured mesh programs
    // (units 4/5; u_texParams.zw flag their presence, the white
    // stand-in is never sampled).
    ensureUniform(s_texEmissive, "s_texEmissive", bgfx::UniformType::Sampler);
    ensureUniform(s_texOcclusion, "s_texOcclusion",
                  bgfx::UniformType::Sampler);
    // Metallic-roughness map at unit 6; u_pbrParams.x = 2 flags it.
    ensureUniform(s_texMetallicRoughness, "s_texMetallicRoughness",
                  bgfx::UniformType::Sampler);
    // Per-face texture palette at unit 10: an array texture whose
    // layers are the images the draw's faces carry. Bound on every mesh
    // draw, with the 1x1 white array below standing in for the draws
    // (almost all of them) whose faces carry none.
    ensureUniform(s_texFace, "s_texFace", bgfx::UniformType::Sampler);
    ensureUniform(u_faceTexParams, "u_faceTexParams",
                  bgfx::UniformType::Vec4);
    if (!bgfx::isValid(m_whiteTexArray)
            && (bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_2D_ARRAY)) {
        static const uint32_t whitelayers[2] = {0xffffffff, 0xffffffff};
        m_whiteTexArray = bgfx::createTexture2D(
            1, 1, false, 2, bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(whitelayers, sizeof(whitelayers)));
    }

    // Shadows: variance moments rendered from the scene light of the
    // Shadow draw style (unit 3 of the mesh programs; the white
    // stand-in reads as fully lit). Needs a renderable two-channel
    // float format.
    ensureUniform(s_texShadow, "s_texShadow", bgfx::UniformType::Sampler);
    // Screen-space AO at unit 9 of the mesh programs: the AO chain
    // result, multiplied into their ambient/headlight/IBL terms
    // only (the white stand-in reads as unoccluded).
    ensureUniform(s_texAOScreen, "s_texAOScreen", bgfx::UniformType::Sampler);
    ensureUniform(u_shadowParams, "u_shadowParams", bgfx::UniformType::Vec4);
    ensureUniform(u_lightDir, "u_lightDir", bgfx::UniformType::Vec4);
    ensureUniform(u_lightPos, "u_lightPos", bgfx::UniformType::Vec4);
    ensureUniform(u_lightColor, "u_lightColor", bgfx::UniformType::Vec4);
    ensureUniform(u_shadowMatrix, "u_shadowMatrix", bgfx::UniformType::Mat4);
    ensureUniform(u_evsm, "u_evsm", bgfx::UniformType::Vec4);
    ensureUniform(u_localLight, "u_localLight", bgfx::UniformType::Vec4,
                  kLocalLights);
    ensureUniform(u_localLightColor, "u_localLightColor",
                  bgfx::UniformType::Vec4, kLocalLights);
    ensureUniform(u_viewLight, "u_viewLight", bgfx::UniformType::Vec4,
                  kViewLights);
    ensureUniform(u_viewLightColor, "u_viewLightColor",
                  bgfx::UniformType::Vec4, kViewLights);
    ensureUniform(u_viewLightAtt, "u_viewLightAtt", bgfx::UniformType::Vec4,
                  kViewLights);
    // EVSM: the moments store an exponential warp of the light
    // window depth (exp(c z), exp(c z)^2), which curbs VSM's light
    // bleeding at overlapping occluders. RG32F carries the classic
    // c = 42 warp; the RG16F fallback must keep exp(2 c) inside
    // half-float range, so its warp is much weaker (c = 5 — better
    // than plain VSM, worse than fp32).
    auto shadowFormat = bgfx::TextureFormat::RG32F;
    shadowWarp = 42.0f;
    m_shadow = (bgfx::getCaps()->formats[shadowFormat]
                & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
    // Drop to RG16F both when RG32F is not framebuffer-capable AND when it
    // is renderable but not linearly filterable (WebGL without
    // OES_texture_float_linear -- common on mobile): under the VSM's
    // linear sampling an unfilterable float32 map reads back black (whole
    // scene shadowed), and point-sampling it instead speckles the
    // terminator. RG16F stays linearly filtered there (half-float linear
    // is widely supported), so the moments keep smoothing cleanly. Its
    // fp16 (z, z^2) lacks precision, so this path always runs the EVSM
    // warp (c = 5) rather than plain (z, z^2) moments.
    if (!m_shadow || shadowFloat32NotFilterable()) {
        shadowFormat = bgfx::TextureFormat::RG16F;
        shadowWarp = 5.0f;
        m_shadowForceWarp = true;
        m_shadow = (bgfx::getCaps()->formats[shadowFormat]
                    & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
    }
    if (m_shadow) {
        this->shadowFormat = shadowFormat;
        shadowSize = 0;  // targets created on first use
        ensureProgram(m_progShadow, "vs_fc_shadow", "fs_fc_shadow");
        ensureProgram(m_progShadowClip, "vs_fc_shadow_clip",
                      "fs_fc_shadow_clip");
        if (m_instancing)
            ensureProgram(m_progShadowInst, "vs_fc_shadow_inst",
                          "fs_fc_shadow");
        ensureProgram(m_progShadowBlur, "vs_fc_comp", "fs_fc_shadow_blur");
        ensureUniform(u_shadowBlur, "u_shadowBlur", bgfx::UniformType::Vec4);
        // Glass shadow tint: glass casters render their light
        // transmittance into a color map beside the moments
        // (multiplicative; receivers sample it at unit 7).
        // The tiles store PLAIN (z, z^2) moments, so they take the
        // directional map's resolved format: RG32F wherever it is
        // renderable and linearly filterable. fp16 moments made the
        // Chebyshev variance (mo.y - mo.x^2, a cancellation of two
        // nearly equal numbers) drown in quantization noise near
        // contacts — a resolution-independent speckle band hugging
        // every contact terminator. The RG16F fallback (mobile
        // WebGL without float32-linear) compensates with a raised
        // variance floor (bulbShadowConf.w).
        //
        // The atlas itself is demand-allocated by
        // ensureEffect(EffectBulbShadow) -- 50MB of a fixed 2048x2048
        // that a scene with no shadow-casting bulb never touches.
        ensureUniform(s_texBulbShadow, "s_texBulbShadow",
                      bgfx::UniformType::Sampler);
        ensureUniform(u_bulbShadowMtx, "u_bulbShadowMtx",
                      bgfx::UniformType::Mat4, kBulbShadowTiles);
        ensureUniform(u_bulbShadowConf, "u_bulbShadowConf",
                      bgfx::UniformType::Vec4, kLocalLights - kMediumSlots);
        ensureUniform(u_bulbShadowRot, "u_bulbShadowRot",
                      bgfx::UniformType::Mat4);
        ensureProgram(m_progShadowTint, "vs_fc_shadow", "fs_fc_shadow_tint");
        ensureUniform(s_texShadowTint, "s_texShadowTint",
                      bgfx::UniformType::Sampler);
    }
    static const uint32_t blackCube[6] = {0, 0, 0, 0, 0, 0};
    if (!bgfx::isValid(m_dummyEnvTex))
        m_dummyEnvTex = bgfx::createTextureCube(1, false, 1,
            bgfx::TextureFormat::RGBA8, 0,
            bgfx::copy(blackCube, sizeof(blackCube)));

    ensureUniform(u_matColor, "u_matColor", bgfx::UniformType::Vec4);
    ensureUniform(u_matEmissive, "u_matEmissive", bgfx::UniformType::Vec4);
    ensureUniform(u_matSpecular, "u_matSpecular", bgfx::UniformType::Vec4);
    ensureUniform(u_ambient, "u_ambient", bgfx::UniformType::Vec4);
    ensureUniform(u_envAmbient, "u_envAmbient", bgfx::UniformType::Vec4);
    ensureUniform(u_params, "u_params", bgfx::UniformType::Vec4);
    ensureUniform(u_polyOffset, "u_polyOffset", bgfx::UniformType::Vec4);
    ensureUniform(u_clipParams, "u_clipParams", bgfx::UniformType::Vec4);
    ensureUniform(u_clipPlanes, "u_clipPlanes", bgfx::UniformType::Vec4,
                  Render::Material::MaxClipPlanes);
    ensureUniform(u_linePattern, "u_linePattern", bgfx::UniformType::Vec4);

    // Weighted-blended OIT for the transparent bucket. Needs
    // independent per-target blending and half-float render targets
    // (the WebGL2-compatible set); with MSAA the formats must also
    // be multisample-framebuffer capable. Unavailable -> the
    // transparent view falls back to bbox-sorted alpha blending as
    // before.
    const auto *caps = bgfx::getCaps();
    const uint32_t oitFmtCaps = samples > 1
        ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA
        : BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER;
    m_oit = (caps->supported & BGFX_CAPS_BLEND_INDEPENDENT)
        && (caps->formats[bgfx::TextureFormat::RGBA16F] & oitFmtCaps)
        && (caps->formats[bgfx::TextureFormat::R16F] & oitFmtCaps)
        && !getenv("FC_BGFX_DEBUG_NO_OIT");
    if (m_oit) {
        // With MSAA the accum/reveal targets carry the scene's
        // sample count (all attachments of the OIT framebuffer must
        // match the shared multisampled depth). Created *without*
        // BGFX_TEXTURE_RT_WRITE_ONLY they get both a multisampled
        // renderbuffer and a single-sample resolve texture; bgfx
        // resolves automatically when the transparent view's
        // framebuffer is switched away (before the composite view),
        // so the composite pass samples the resolved images.
        const uint64_t oitFlags = 0
            | flags
            | BGFX_SAMPLER_MIN_POINT
            | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP
            | BGFX_SAMPLER_V_CLAMP;
        oitAccum = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, oitFlags);
        oitReveal = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::R16F, oitFlags);
        // Accumulation renders against the scene depth (test only,
        // no write), so the OIT framebuffer shares bgfxDepth.
        bgfx::Attachment att[3];
        att[0].init(oitAccum, bgfx::Access::Write, 0, 1, 0,
                    BGFX_RESOLVE_NONE);
        att[1].init(oitReveal, bgfx::Access::Write, 0, 1, 0,
                    BGFX_RESOLVE_NONE);
        att[2].init(bgfxDepth, bgfx::Access::Write, 0, 1, 0,
                    BGFX_RESOLVE_NONE);
        oitFbo = bgfx::createFrameBuffer(3, att, false);
        ensureProgram(m_progMeshOit, "vs_fc_mesh", "fs_fc_mesh_oit");
        ensureProgram(m_progMeshOitClip, "vs_fc_mesh_clip",
                      "fs_fc_mesh_oit_clip");
        ensureProgram(m_progMeshOitTex, "vs_fc_mesh_tex",
                      "fs_fc_mesh_oit_tex");
        ensureProgram(m_progMeshOitTexClip, "vs_fc_mesh_tex_clip",
                      "fs_fc_mesh_oit_tex_clip");
        if (m_instancing) {
            // WBOIT accumulation is order-independent, so transparent
            // instance groups are legal — but only while OIT runs
            // (the sorted fallback needs per-draw depth keys).
            ensureProgram(m_progMeshInstOit, "vs_fc_mesh_inst",
                          "fs_fc_mesh_oit");
            ensureProgram(m_progMeshInstOitTex, "vs_fc_mesh_tex_inst",
                          "fs_fc_mesh_oit_tex");
        }
        ensureProgram(m_progComp, "vs_fc_comp", "fs_fc_comp");
        ensureUniform(s_texAccum, "s_texAccum", bgfx::UniformType::Sampler);
        ensureUniform(s_texReveal, "s_texReveal", bgfx::UniformType::Sampler);
    }

    // Render debugging buffer visualization (docs/RenderDebug.md).
    ensureProgram(m_progDebug, "vs_fc_comp", "fs_fc_debug");
    ensureUniform(u_debugParams, "u_debugParams", bgfx::UniformType::Vec4);
    ensureProgram(m_progDebugScene, "vs_fc_debug_scene", "fs_fc_debug_scene");
    ensureProgram(m_progDebugSceneClip, "vs_fc_debug_scene_clip",
                  "fs_fc_debug_scene_clip");
    ensureUniform(s_texDebugScene, "s_texDebugScene",
                  bgfx::UniformType::Sampler);

    // SSAO: depth+normal prepass + AO generation/blur targets, all
    // non-MSAA at viewport size (the multiply pass samples at pixel
    // centers under MSAA). Needs renderable-and-samplable RGBA16F
    // (the WebGL2 float-buffer set, like OIT) and R8.
    m_ssao = (caps->formats[bgfx::TextureFormat::RGBA16F]
                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
        && (caps->formats[bgfx::TextureFormat::R8]
                & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
    if (m_ssao) {
        // The targets themselves -- the prepass pair, the AO chain, the
        // GTAO pyramid and the glass interval, ~98MB at 1080p -- are
        // demand-allocated by updateEffect(EffectSSAO). Only the
        // programs and uniforms are built here, as everywhere else.
        ensureProgram(m_progGtaoDepth, "vs_fc_comp", "fs_fc_gtao_depths");
        static const char *const mipSamplerNames[kAOMipLevels] = {
            "s_texAOMip1", "s_texAOMip2", "s_texAOMip3",
            "s_texAOMip4", "s_texAOMip5", "s_texAOMip6"};
        for (int m = 0; m < kAOMipLevels; ++m)
            ensureUniform(s_texAOMip[m], mipSamplerNames[m],
                          bgfx::UniformType::Sampler);

        ensureProgram(m_progPrepass, "vs_fc_prepass", "fs_fc_prepass");
        ensureProgram(m_progPrepassClip, "vs_fc_prepass_clip",
                      "fs_fc_prepass_clip");
        // Medium interval depth writers: prepass layout with the
        // body's appearance slot in .x.
        ensureProgram(m_progMedDepth, "vs_fc_prepass", "fs_fc_meddepth");
        ensureProgram(m_progMedDepthClip, "vs_fc_prepass_clip",
                      "fs_fc_meddepth_clip");
        ensureUniform(u_mediumSlot, "u_mediumSlot", bgfx::UniformType::Vec4);
        if (m_instancing)
            ensureProgram(m_progPrepassInst, "vs_fc_prepass_inst",
                          "fs_fc_prepass");
        ensureProgram(m_progSsao, "vs_fc_comp", "fs_fc_ssao");
        ensureProgram(m_progGtao, "vs_fc_comp", "fs_fc_gtao");
        ensureProgram(m_progGtaoBlur, "vs_fc_comp", "fs_fc_gtao_blur");
        ensureProgram(m_progSsaoBlur, "vs_fc_comp", "fs_fc_ssao_blur");
        // Cavity shares the prepass resources, so it is built with them
        // (m_ssao gates the whole block); the pass itself is gated by
        // its own config in render().
        ensureProgram(m_progCavity, "vs_fc_comp", "fs_fc_cavity");
        ensureUniform(u_cavityParams, "u_cavityParams",
                      bgfx::UniformType::Vec4);
        ensureUniform(s_texNormalZ, "s_texNormalZ",
                      bgfx::UniformType::Sampler);
        ensureUniform(s_texAONoise, "s_texAONoise",
                      bgfx::UniformType::Sampler);
        ensureUniform(s_texAO, "s_texAO", bgfx::UniformType::Sampler);
        ensureUniform(u_aoParams, "u_aoParams", bgfx::UniformType::Vec4);
        ensureUniform(u_aoParams2, "u_aoParams2", bgfx::UniformType::Vec4);
        ensureUniform(u_aoKernel, "u_aoKernel", bgfx::UniformType::Vec4,
                      kAOSamples);
    }

    // Volumetric light shafts: the half-res raymarch reads the SSAO
    // prepass depth (ray end) and the shadow moments (light
    // visibility), so it needs both resource sets. Point-sampled —
    // the apply pass does its own bilateral 4-tap upsample.
    m_vol = m_ssao && m_shadow;
    if (m_vol) {
        // The raymarch pair, its history pair and the water/cloud/fire
        // interval targets are demand-allocated together by
        // ensureEffect(EffectVolumetric) -- they are the single
        // largest block a view owns (~166MB at 1080p) and none of it
        // is touched while Render_Volumetric is off. Programs and
        // uniforms are built here as usual.
        ensureProgram(m_progVol, "vs_fc_comp", "fs_fc_volume");
        ensureProgram(m_progVolAccum, "vs_fc_comp", "fs_fc_volume_accum");
        ensureUniform(s_texVolFront, "s_texVolFront",
                      bgfx::UniformType::Sampler);
        ensureProgram(m_progVolApply, "vs_fc_comp", "fs_fc_volume_apply");
        ensureUniform(s_texVol, "s_texVol", bgfx::UniformType::Sampler);
        ensureUniform(u_volParams, "u_volParams", bgfx::UniformType::Vec4);
        ensureUniform(u_volMedium, "u_volMedium", bgfx::UniformType::Vec4);
        ensureUniform(u_volTexel, "u_volTexel", bgfx::UniformType::Vec4);

        ensureProgram(m_progVolExt, "vs_fc_comp", "fs_fc_volume_ext");
        ensureUniform(s_texWaterFront, "s_texWaterFront",
                      bgfx::UniformType::Sampler);
        ensureUniform(s_texWaterBack, "s_texWaterBack",
                      bgfx::UniformType::Sampler);
        ensureUniform(u_waterSigma, "u_waterSigma", bgfx::UniformType::Vec4,
                      kMediumSlots);
        // Water caustics: a fullscreen light-space pattern splat
        // over the prepass surfaces inside the water interval.
        ensureProgram(m_progCaustics, "vs_fc_comp", "fs_fc_caustics");
        ensureUniform(u_causticParams, "u_causticParams",
                      bgfx::UniformType::Vec4, kMediumSlots);
        ensureUniform(s_texCloudFront, "s_texCloudFront",
                      bgfx::UniformType::Sampler);
        ensureUniform(s_texCloudBack, "s_texCloudBack",
                      bgfx::UniformType::Sampler);
        ensureUniform(u_cloudParams, "u_cloudParams", bgfx::UniformType::Vec4,
                      kMediumSlots);
        ensureUniform(s_texFireFront, "s_texFireFront",
                      bgfx::UniformType::Sampler);
        ensureUniform(s_texFireBack, "s_texFireBack",
                      bgfx::UniformType::Sampler);
        ensureUniform(u_fireParams, "u_fireParams", bgfx::UniformType::Vec4,
                      kMediumSlots);
        ensureUniform(u_fireParams2, "u_fireParams2", bgfx::UniformType::Vec4,
                      kMediumSlots);
        ensureUniform(u_fireFrame, "u_fireFrame", bgfx::UniformType::Mat4,
                      kMediumSlots);
        ensureUniform(u_fountainParams, "u_fountainParams",
                      bgfx::UniformType::Vec4, kMediumSlots);
        ensureUniform(u_fountainFrame, "u_fountainFrame",
                      bgfx::UniformType::Mat4, kMediumSlots);
    }

    // Water surface refraction: the scene color copies into a
    // linearly-sampled texture the surface shader offsets into.
    // Independent of the volumetric resource sets.
    // Matched to the scene colour it copies -- an 8-bit copy of a
    // floating point scene would clip the refraction back down.
    sceneCopyTex = bgfx::createTexture2D(width, height, false, 1,
        hdrScene ? bgfx::TextureFormat::RGBA16F
                 : bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    sceneCopyFbo = bgfx::createFrameBuffer(1, &sceneCopyTex, false);
    ensureProgram(m_progWaterCopy, "vs_fc_comp", "fs_fc_copy");
    ensureProgram(m_progWater, "vs_fc_mesh", "fs_fc_water");
    ensureUniform(s_texScene, "s_texScene", bgfx::UniformType::Sampler);
    ensureUniform(s_texRefl, "s_texRefl", bgfx::UniformType::Sampler);
    ensureUniform(u_waterSurf, "u_waterSurf", bgfx::UniformType::Vec4);
    ensureUniform(u_waterAbsorb, "u_waterAbsorb", bgfx::UniformType::Vec4);
    ensureUniform(u_waterRipple, "u_waterRipple", bgfx::UniformType::Vec4);
    ensureUniform(u_waterSplash, "u_waterSplash", bgfx::UniformType::Vec4,
                  kMediumSlots);
    // The surface shader's refraction depth reject samples the SSAO
    // prepass; without those resources the sampler uniform still
    // has to exist for the (disabled) stage binding.
    ensureUniform(s_texNormalZ, "s_texNormalZ", bgfx::UniformType::Sampler);

    // Glass surface: refraction from the same scene copy, plus the
    // absorption interval targets of the SSAO resource set (the
    // glass pass is gated on both).
    ensureProgram(m_progGlass, "vs_fc_mesh", "fs_fc_glass");
    ensureUniform(s_texGlassFront, "s_texGlassFront",
                  bgfx::UniformType::Sampler);
    ensureUniform(s_texGlassBack, "s_texGlassBack",
                  bgfx::UniformType::Sampler);
    ensureUniform(u_glassParams, "u_glassParams", bgfx::UniformType::Vec4);
    ensureUniform(u_glassTint, "u_glassTint", bgfx::UniformType::Vec4);
    ensureUniform(s_texLineSdf, "s_texLineSdf",
                  bgfx::UniformType::Sampler);
    ensureUniform(s_texLineSdfAux, "s_texLineSdfAux",
                  bgfx::UniformType::Sampler);

    // Ground/planar reflection. The mirrored-camera re-render target is
    // demand-allocated by ensureEffect(EffectReflection); the programs
    // and uniforms are built here.
    ensureProgram(m_progReflMedia, "vs_fc_comp", "fs_fc_refl_media");
    ensureProgram(m_progGroundRefl, "vs_fc_mesh", "fs_fc_groundrefl");
    // The shadow-only ground rides the same vertex program as both
    // of the above, for the same reason: one quad, one depth.
    ensureProgram(m_progGroundShadow, "vs_fc_mesh",
                  "fs_fc_groundshadow");
    // ... and the quad-less form of it, which needs the screen triangle
    // instead and the plane it is to find as a uniform.
    ensureProgram(m_progGroundShadowPlane, "vs_fc_comp",
                  "fs_fc_groundshadow_plane");
    ensureUniform(u_groundPlane, "u_groundPlane", bgfx::UniformType::Vec4);
    // The drawn ground's own mesh variants, which fade their rim out.
    ensureProgram(m_progGroundFade, "vs_fc_mesh", "fs_fc_mesh_ground");
    ensureProgram(m_progGroundFadeTex, "vs_fc_mesh_tex",
                  "fs_fc_mesh_ground_tex");
    ensureProgram(m_progGroundFadePrepass, "vs_fc_prepass",
                  "fs_fc_prepass_ground");
    ensureUniform(u_groundFadeU, "u_groundFadeU", bgfx::UniformType::Vec4);
    ensureUniform(u_groundFadeV, "u_groundFadeV", bgfx::UniformType::Vec4);
    ensureUniform(u_reflParams, "u_reflParams", bgfx::UniformType::Vec4);

    // Stateful particle resources (docs/RenderEngine.md §5.8).
    // Size-independent, so they are created once and kept across
    // the resize-driven rebuilds around them — hence the validity
    // guards rather than plain assignment.
    if (!bgfx::isValid(m_progPSimInit)) {
        ensureProgram(m_progPSimInit, "vs_fc_comp", "fs_fc_psim_init");
        ensureUniform(s_pstate0, "s_pstate0", bgfx::UniformType::Sampler);
        ensureUniform(s_pstate1, "s_pstate1", bgfx::UniformType::Sampler);
        ensureUniform(u_pgrid, "u_pgrid", bgfx::UniformType::Vec4);
        ensureUniform(u_pboxMin, "u_pboxMin", bgfx::UniformType::Vec4);
        ensureUniform(u_pboxMax, "u_pboxMax", bgfx::UniformType::Vec4);
        const uint16_t fmtCaps = bgfx::getCaps()->formats[
            bgfx::TextureFormat::RGBA32F];
        particleStateOk = bgfx::isValid(m_progPSimInit)
            && (fmtCaps & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
        if (!particleStateOk)
            std::printf("bgfx: no RGBA32F render target — stateful "
                        "particle emitters fall back to stateless\n");
        ensureProgram(m_progPImpact, "vs_fc_pimpact", "fs_fc_pimpact");
        ensureUniform(s_pimpsrc, "s_pimpsrc", bgfx::UniformType::Sampler);
        ensureUniform(u_impactFrame, "u_impactFrame", bgfx::UniformType::Vec4);
        ensureUniform(u_impactNow, "u_impactNow", bgfx::UniformType::Vec4);
        ensureUniform(s_texImpact, "s_texImpact", bgfx::UniformType::Sampler);
        ensureUniform(u_waterImpact, "u_waterImpact", bgfx::UniformType::Vec4);
        ensureUniform(u_waterImpactCfg, "u_waterImpactCfg",
                      bgfx::UniformType::Vec4);
    }

    // The programs without which this view cannot draw the scene at
    // all. Every other program is a feature that degrades on its own
    // (an invalid handle makes bgfx drop the submit), but a missing
    // one of these means the stock shader pack is not the one this
    // build expects -- report it and tear the view back down, so the
    // frame path reports failure and the desktop composites through
    // Coin instead of showing an empty window.
    const struct { bgfx::ProgramHandle prog; const char *name; } core[] = {
        {m_progMesh, "fc_mesh"},
        {m_progFlat, "fc_flat"},
        {m_progMeshClip, "fc_mesh_clip"},
        {m_progFlatClip, "fc_flat_clip"},
        {m_progMeshTex, "fc_mesh_tex"},
        {m_progMeshTexClip, "fc_mesh_tex_clip"},
        {m_progCap, "fc_cap"},
        {m_progCapClip, "fc_cap_clip"},
#ifdef FC_RENDERER_STANDALONE
        // Nothing else copies the scene onto the backbuffer here. On
        // the desktop the GL blit does, so a missing present program
        // there costs the colour transform and nothing else.
        {m_progPresent, "fc_present"},
#endif
    };
    for (const auto &c : core) {
        if (bgfx::isValid(c.prog))
            continue;
        qCritical() << "bgfx: core program" << c.name
                    << "missing from the shader pack at"
                    << _BGFXLib.shaderPath().c_str()
                    << "- the renderer cannot draw";
        shaderFailed = true;
        shaderFailedDrain = true;
        destroy();
        return;
    }
}

bgfx::VertexBufferHandle BGFXView::whiteColors(int numVertices)
{
    if (numVertices > whiteColorCount) {
        if (bgfx::isValid(whiteColorVb))
            bgfx::destroy(whiteColorVb);
        whiteColorCount = std::max(numVertices, 4096);
        ColorVertex::init();
        const bgfx::Memory *mem =
            bgfx::alloc(uint32_t(whiteColorCount) * 4);
        memset(mem->data, 0xff, size_t(whiteColorCount) * 4);
        whiteColorVb = bgfx::createVertexBuffer(
            mem, ColorVertex::ms_layout);
    }
    return whiteColorVb;
}

/// Stream 2 (a_texcoord0) for a draw paired with a textured vertex
/// stage outside the texture path: the mesh's own coordinates when it
/// has any, under the identity texture matrix. A mesh with none leaves
/// the stream unbound, and only GL then reads the (0, 0) constant a
/// generated material saw before: on Vulkan an unbound attribute reads
/// the vertex position instead (see MatVertex in BGFXRendererP.h), so
/// treat what it carries as arbitrary rather than as zero.
void BGFXView::bindMeshTexCoord(GpuMesh *gpu, const Render::MeshData &mesh)
{
    gpu->geom->ensureTexCoord(mesh);
    if (bgfx::isValid(gpu->geom->texcoord))
        bgfx::setVertexBuffer(2, gpu->geom->texcoord);
    float texmat[16];
    bx::mtxIdentity(texmat);
    bgfx::setUniform(u_texMatrix, texmat);
}

void BGFXView::setMeshVertexBuffers(GpuMesh *gpu, const Render::MeshData &mesh)
{
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    bgfx::setVertexBuffer(1, bgfx::isValid(gpu->color)
                                 ? gpu->color
                                 : whiteColors(mesh.numVertices));
    // Per-face material stream, bound only when the mesh carries one:
    // draws without it leave a_color1/a_color2 unbound, and the shader
    // only reads them when u_matEmissive.w flags the stream in -- which
    // BGFXViewSubmit gates on this same handle being valid. It has to
    // stay that way: an unbound attribute is a constant on GL but the
    // vertex position on Vulkan, so the flag is the only thing keeping
    // geometry out of the material slots.
    if (bgfx::isValid(gpu->mats))
        bgfx::setVertexBuffer(3, gpu->mats);
}

void BGFXView::collectMeshes(const std::unordered_map<uint64_t, uint64_t> &kept,
                             const std::unordered_set<uint64_t> &gatedOnly)
{
    for (auto it = meshes.begin(); it != meshes.end();) {
        auto pub = kept.find(it->first);
        const bool stale =
            pub == kept.end() || gatedOnly.count(it->first)
            ? it->second.lastUsed + 2 < frame
            : (it->second.geom
               && it->second.generation != pub->second);
        if (stale) {
            it->second.destroy();
            it = meshes.erase(it);
        } else
            ++it;
    }
    // Geometries are shared behind the meshes, so their rule is
    // REFERENCE, not age: any geometry a surviving mesh points at
    // stays with it; the rest are orphans of swaps and publishes
    // and go after the same in-flight grace.
    std::unordered_set<const GpuGeometry *> referenced;
    referenced.reserve(meshes.size());
    for (const auto &m : meshes)
        if (m.second.geom)
            referenced.insert(m.second.geom);
    for (auto it = geometries.begin(); it != geometries.end();) {
        if (!referenced.count(&it->second)
                && it->second.lastUsed + 2 < frame) {
            it->second.destroy();
            it = geometries.erase(it);
        } else
            ++it;
    }
    for (auto it = textures.begin(); it != textures.end();) {
        if (it->second.lastUsed + 2 < frame) {
            it->second.destroy();
            it = textures.erase(it);
        } else
            ++it;
    }
    for (auto it = textureArrays.begin(); it != textureArrays.end();) {
        if (it->second.lastUsed + 2 < frame) {
            it->second.destroy();
            it = textureArrays.erase(it);
        } else
            ++it;
    }
}

void BGFXView::present()
{
    if (!bgfx::isValid(m_progPresent))
        return;
    // The transform this frame asked for, as the shader's own selector.
    // An out-of-range value passes the frame through in the shader, so
    // a snapshot written by a later build degrades rather than failing.
    float params[4] = {float(outputTransform),
                       std::max(outputExposure, 0.0f), 0.0f, 0.0f};
    bgfx::setUniform(u_outputParams, params);
    bgfx::setTexture(0, s_texScene, bgfxColor);
    fullscreen(ViewPresent, m_progPresent,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
}

#ifndef FC_RENDERER_STANDALONE

bool BGFXView::writeDumpImage(const std::string &path,
                           const unsigned char *color,
                           int width, int height)
{
    auto ext = path.rfind('.');
    if (ext != std::string::npos
            && path.compare(ext, std::string::npos, ".ppm") == 0) {
        FILE *fp = fopen(path.c_str(), "wb");
        if (!fp)
            return false;
        fprintf(fp, "P6\n%d %d\n255\n", width, height);
        // Both are top-down: the caller has already applied whatever
        // flip its backend's origin called for.
        std::vector<unsigned char> row(size_t(width) * 3);
        for (int y = 0; y < height; ++y) {
            const unsigned char *src = color + size_t(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                row[size_t(x)*3] = src[size_t(x)*4];
                row[size_t(x)*3 + 1] = src[size_t(x)*4 + 1];
                row[size_t(x)*3 + 2] = src[size_t(x)*4 + 2];
            }
            fwrite(row.data(), 1, row.size(), fp);
        }
        fclose(fp);
        return true;
    }
    QImage img(color, width, height, width * 4,
               QImage::Format_RGBA8888);
    return img.save(QString::fromStdString(path));
}

void BGFXView::blit(Render::RenderStats *stats,
          int dstX, int dstY, int dstH)
{
    (void)stats;
    // The composite is GL, all the way down: it wraps bgfx's colour and
    // depth attachments in a GL framebuffer and glBlitFramebuffers them
    // into the widget's. bgfx::getInternal only yields a GL texture
    // name while bgfx is running on GL -- on Metal it is an
    // id<MTLTexture>, which the FBO then reports as an incomplete
    // attachment. Nothing here can be salvaged for another backend, so
    // it stands aside and Coin draws the view instead.
    if (bgfx::getRendererType() != bgfx::RendererType::OpenGL
            && bgfx::getRendererType() != bgfx::RendererType::OpenGLES)
        return;
    // Only GL 1.1 is exported by Windows' opengl32, so the framebuffer entry
    // points below must be resolved against the current context rather than
    // called directly -- ELF systems get them from libGL and never noticed.
    // QOpenGLExtraFunctions (not the base QOpenGLFunctions set) is what
    // carries glBlitFramebuffer.
    auto *f = QOpenGLContext::currentContext()->extraFunctions();
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *) &prevFbo);
    // With a colour transform selected, what belongs on screen is the
    // ENCODED frame the present pass wrote, not the linear scene colour.
    // Its handle is dropped whenever the group is released, which resets
    // the cache below (freeEffect), so the cached GL framebuffer can
    // never outlive the texture it wraps.
    const bool encoded = outputTransform != Render::OutputConfig::None
        && bgfx::isValid(presentTex);
    const bgfx::TextureHandle source = encoded ? presentTex : bgfxColor;
    if (hasFBO && blitSourceEncoded != encoded)
        hasFBO = false;
    if (!hasFBO) {
        hasFBO = true;
        blitSourceEncoded = encoded;
        GLuint colorBuffer = bgfx::getInternal(source);
        GLuint depthBuffer = bgfx::getInternal(bgfxDepth);
        blitColorId = colorBuffer;
        std::printf("bgfx: blit cache create msaa %d color %u (isTex %d) "
                    "depth %u (isTex %d)\n", msaaSamples, colorBuffer,
                    int(glIsTexture(colorBuffer)), depthBuffer,
                    int(glIsTexture(depthBuffer)));
        // The sampleable scene color is a texture (with MSAA it is
        // bgfx's single-sample resolve texture, resolved by the
        // frame-end framebuffer restore), while the write-only depth
        // stays a renderbuffer — under MSAA their sample counts
        // differ, so the color and depth transfers use separate
        // read framebuffers.
        f->glGenFramebuffers(1, &fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (glIsTexture(colorBuffer))
            f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                      GL_TEXTURE_2D, colorBuffer, 0);
        else
            f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_RENDERBUFFER, colorBuffer);
        if (!checkFramebufferStatus()) {
            f->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
            dropBlitCache();
            return;
        }
        f->glGenFramebuffers(1, &fboDepth);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboDepth);
        // Asked, not assumed -- the same question the colour attachment
        // above already asks. bgfx backs a write-only D24S8 target with
        // a renderbuffer on some drivers and a TEXTURE on others, and
        // `glFramebufferRenderbuffer` handed a texture name attaches
        // nothing: the framebuffer comes out "incomplete, missing
        // attachment" and every frame took the failure path below.
        // Measured on Windows/GL (RTX 2000 Ada, 2026-09-09), where
        // that cost ~430ms of a 450ms frame and, on a 17k-object
        // assembly, meant the scene never survived to be drawn at all.
        if (glIsTexture(depthBuffer)) {
            f->glFramebufferTexture2D(GL_FRAMEBUFFER,
                                      GL_DEPTH_STENCIL_ATTACHMENT,
                                      GL_TEXTURE_2D, depthBuffer, 0);
        }
        else {
            f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                        GL_RENDERBUFFER, depthBuffer);
            f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                        GL_RENDERBUFFER, depthBuffer);
        }
        // No color attachment: complete only with the draw/read
        // buffers off.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (!checkFramebufferStatus()) {
            f->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
            dropBlitCache();
            return;
        }
    }

    // A sub-view lands at its rect of the destination surface; the
    // caller's y is top-left, GL's is bottom-left.
    const int dx0 = dstX;
    const int dy0 = dstH > 0 ? dstH - dstY - height : 0;
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    f->glBlitFramebuffer(0, 0, width, height,
                         dx0, dy0, dx0 + width, dy0 + height,
                         GL_COLOR_BUFFER_BIT,
                         GL_NEAREST);
    if (glGetError() != GL_NO_ERROR) {
        static int logged = 0;
        if (logged++ < 4) {
            GLuint cb = bgfx::getInternal(bgfxColor);
            std::printf("bgfx: blit color FAILED msaa %d readfbo %u "
                        "status 0x%x cached-color tex %u (isTex %d) "
                        "current-internal %u\n",
                        msaaSamples, fbo,
                        f->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER),
                        blitColorId, int(glIsTexture(blitColorId)), cb);
        }
    }
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, fboDepth);
    f->glBlitFramebuffer(0, 0, width, height,
                         dx0, dy0, dx0 + width, dy0 + height,
                         GL_DEPTH_BUFFER_BIT,
                         GL_NEAREST);
    checkGLError("blit depth");

    f->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
}
#endif // !FC_RENDERER_STANDALONE
