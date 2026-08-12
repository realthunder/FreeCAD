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

BGFXView::~BGFXView()
{
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
    // The recreated moments texture starts empty, so the cached-map
    // hash resets with it (same for the AO/prepass cache and the
    // bulb shadow tiles).
    shadowMapHash = 0;
    aoMapHash = 0;
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
#ifndef FC_RENDERER_STANDALONE
    if (hasFBO) {
        _BGFXLib.freeFBO(fbo);
        if (fboDepth)
            _BGFXLib.freeFBO(fboDepth);
        fboDepth = 0;
        hasFBO = false;
    }
#endif
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
    width = _BGFXLib.standaloneWidth;
    height = _BGFXLib.standaloneHeight;
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
    bgfxColor = createTexture(bgfx::TextureFormat::RGBA8, flags, true);
    //GL_DEPTH24_STENCIL8
    // NOTE: the MSAA levels are an enum in the RT flag nibble, not
    // orthogonal bits — masking BGFX_TEXTURE_RT out of them would
    // turn MSAA_X4 (0x3) into MSAA_X2 (0x2) and desync the depth
    // sample count from the color attachment's.
    bgfxDepth = createTexture(bgfx::TextureFormat::D24S8, flags);
    bgfx::Attachment attachment[2];
    // No mip chain on these render targets; the default resolve flag
    // (BGFX_RESOLVE_AUTO_GEN_MIPS) is also rejected for depth attachments.
    attachment[0].init(bgfxColor, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    attachment[1].init(bgfxDepth, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    bgfxFbo = bgfx::createFrameBuffer(2, attachment, true);

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

    // Bloom (glow) chain: quarter-res RGBA16F halo source + blur
    // ping target (small enough to keep unconditionally; the passes
    // only run while the bloom config is enabled).
    {
        uint16_t qw = uint16_t(std::max(1, int(width) / 4));
        uint16_t qh = uint16_t(std::max(1, int(height) / 4));
        const uint64_t bloomFlags = BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        bloomTex = bgfx::createTexture2D(qw, qh, false, 1,
            bgfx::TextureFormat::RGBA16F, bloomFlags);
        bloomBlurTex = bgfx::createTexture2D(qw, qh, false, 1,
            bgfx::TextureFormat::RGBA16F, bloomFlags);
        bloomFbo = bgfx::createFrameBuffer(1, &bloomTex, false);
        bloomBlurFbo = bgfx::createFrameBuffer(1, &bloomBlurTex,
                                               false);
        m_progBloomBright = fcLoadProgram("vs_fc_comp",
                                          "fs_fc_bloom_bright",
                                          _BGFXLib.shaderPath().c_str());
        m_progBloomEmit = fcLoadProgram("vs_fc_mesh",
                                        "fs_fc_bloom_emit",
                                        _BGFXLib.shaderPath().c_str());
        m_progBloomBlur = fcLoadProgram("vs_fc_comp",
                                        "fs_fc_bloom_blur",
                                        _BGFXLib.shaderPath().c_str());
        m_progBloomApply = fcLoadProgram("vs_fc_comp",
                                         "fs_fc_bloom_apply",
                                         _BGFXLib.shaderPath().c_str());
        s_texBloom = bgfx::createUniform("s_texBloom",
                                         bgfx::UniformType::Sampler);
        u_bloomParams = bgfx::createUniform(
            "u_bloomParams", bgfx::UniformType::Vec4);
        u_bloomTexel = bgfx::createUniform(
            "u_bloomTexel", bgfx::UniformType::Vec4);
        u_bloomBlur = bgfx::createUniform(
            "u_bloomBlur", bgfx::UniformType::Vec4);
    }

    // Visible sun disc along the directional scene light.
    m_progSun = fcLoadProgram("vs_fc_comp", "fs_fc_sun",
                              _BGFXLib.shaderPath().c_str());
    u_sunParams = bgfx::createUniform("u_sunParams",
                                      bgfx::UniformType::Vec4);

    // PBR environment drawn as the visible background.
    m_progEnvBg = fcLoadProgram("vs_fc_comp", "fs_fc_env",
                                _BGFXLib.shaderPath().c_str());

#ifdef FC_RENDERER_STANDALONE
    // The standalone present pass copies the scene color onto the
    // default backbuffer (no Qt framebuffer to GL-blit into).
    m_progPresent = fcLoadProgram("vs_fc_comp", "fs_fc_copy",
                                  _BGFXLib.shaderPath().c_str());
    if (!bgfx::isValid(s_texScene))
        s_texScene = bgfx::createUniform("s_texScene",
                                         bgfx::UniformType::Sampler);
#endif

    m_progMesh = fcLoadProgram("vs_fc_mesh", "fs_fc_mesh",
                               _BGFXLib.shaderPath().c_str());
    m_progFlat = fcLoadProgram("vs_fc_flat", "fs_fc_flat",
                               _BGFXLib.shaderPath().c_str());
    m_progMeshClip = fcLoadProgram("vs_fc_mesh_clip", "fs_fc_mesh_clip",
                                   _BGFXLib.shaderPath().c_str());
    m_progFlatClip = fcLoadProgram("vs_fc_flat_clip", "fs_fc_flat_clip",
                                   _BGFXLib.shaderPath().c_str());
    m_progMeshTex = fcLoadProgram("vs_fc_mesh_tex", "fs_fc_mesh_tex",
                                  _BGFXLib.shaderPath().c_str());
    m_progMeshTexClip = fcLoadProgram("vs_fc_mesh_tex_clip",
                                      "fs_fc_mesh_tex_clip",
                                      _BGFXLib.shaderPath().c_str());
    s_texColor = bgfx::createUniform("s_texColor",
                                     bgfx::UniformType::Sampler);
    u_texMatrix = bgfx::createUniform("u_texMatrix",
                                      bgfx::UniformType::Mat4);
    u_texParams = bgfx::createUniform("u_texParams",
                                      bgfx::UniformType::Vec4);
    u_texBlendColor = bgfx::createUniform("u_texBlendColor",
                                          bgfx::UniformType::Vec4);

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
        m_progMeshInst = fcLoadProgram("vs_fc_mesh_inst", "fs_fc_mesh",
                                       _BGFXLib.shaderPath().c_str());
        m_progMeshInstTex = fcLoadProgram("vs_fc_mesh_tex_inst",
                                          "fs_fc_mesh_tex",
                                          _BGFXLib.shaderPath().c_str());
        u_instParams = bgfx::createUniform("u_instParams",
                                           bgfx::UniformType::Vec4);
        m_progLine = fcLoadProgram("vs_fc_line", "fs_fc_flat",
                                   _BGFXLib.shaderPath().c_str());
        m_progLineClip = fcLoadProgram("vs_fc_line_clip", "fs_fc_flat_clip",
                                       _BGFXLib.shaderPath().c_str());
        m_progLinePat = fcLoadProgram("vs_fc_line_pat", "fs_fc_line_pat",
                                      _BGFXLib.shaderPath().c_str());
        m_progLinePatClip = fcLoadProgram("vs_fc_line_pat_clip",
                                          "fs_fc_line_pat_clip",
                                          _BGFXLib.shaderPath().c_str());
        m_progPoint = fcLoadProgram("vs_fc_point", "fs_fc_flat",
                                    _BGFXLib.shaderPath().c_str());
        m_progPointClip = fcLoadProgram("vs_fc_point_clip",
                                        "fs_fc_flat_clip",
                                        _BGFXLib.shaderPath().c_str());
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
    m_progCap = fcLoadProgram("vs_fc_cap", "fs_fc_cap",
                              _BGFXLib.shaderPath().c_str());
    m_progCapClip = fcLoadProgram("vs_fc_cap_clip", "fs_fc_cap_clip",
                                  _BGFXLib.shaderPath().c_str());
    s_texHatch = bgfx::createUniform("s_texHatch",
                                     bgfx::UniformType::Sampler);
    // 1x1 white stand-in so the cap program samples neutrally when
    // hatching is disabled (and for the stencil cleanup pass).
    static const uint32_t white = 0xffffffff;
    m_whiteTex = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, 0,
        bgfx::copy(&white, sizeof(white)));
    // 1x1 fully transparent black: a stand-in whose *coverage* is
    // zero, for debug binds where the white texture would read as a
    // target full of opaque white (RenderDebug mode 9).
    static const uint32_t black = 0x00000000;
    m_blackTex = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, 0,
        bgfx::copy(&black, sizeof(black)));
    CapVertex::init();

    // PBR: the mesh programs always carry the environment sampler
    // (the branch is uniform-selected); a 1x1 black cube stands in
    // while PBR is off or unavailable. The real environment is built
    // on demand (ensureEnvironment).
    s_texEnv = bgfx::createUniform("s_texEnv",
                                   bgfx::UniformType::Sampler);
    u_pbrParams = bgfx::createUniform("u_pbrParams",
                                      bgfx::UniformType::Vec4);
    u_matcapParams = bgfx::createUniform("u_matcapParams",
                                         bgfx::UniformType::Vec4);
    u_envSH = bgfx::createUniform("u_envSH",
                                  bgfx::UniformType::Vec4, kEnvSH);
    // Bump mapping of the textured mesh programs (unit 2; the 1x1
    // white cap texture stands in when a draw has no bump map).
    s_texBump = bgfx::createUniform("s_texBump",
                                    bgfx::UniformType::Sampler);
    u_bumpParams = bgfx::createUniform("u_bumpParams",
                                       bgfx::UniformType::Vec4);
    // Emissive/occlusion material maps of the textured mesh programs
    // (units 4/5; u_texParams.zw flag their presence, the white
    // stand-in is never sampled).
    s_texEmissive = bgfx::createUniform("s_texEmissive",
                                        bgfx::UniformType::Sampler);
    s_texOcclusion = bgfx::createUniform("s_texOcclusion",
                                         bgfx::UniformType::Sampler);
    // Metallic-roughness map at unit 6; u_pbrParams.x = 2 flags it.
    s_texMetallicRoughness =
        bgfx::createUniform("s_texMetallicRoughness",
                            bgfx::UniformType::Sampler);

    // Shadows: variance moments rendered from the scene light of the
    // Shadow draw style (unit 3 of the mesh programs; the white
    // stand-in reads as fully lit). Needs a renderable two-channel
    // float format.
    s_texShadow = bgfx::createUniform("s_texShadow",
                                      bgfx::UniformType::Sampler);
    // Screen-space AO at unit 9 of the mesh programs: the AO chain
    // result, multiplied into their ambient/headlight/IBL terms
    // only (the white stand-in reads as unoccluded).
    s_texAOScreen = bgfx::createUniform("s_texAOScreen",
                                        bgfx::UniformType::Sampler);
    u_shadowParams = bgfx::createUniform("u_shadowParams",
                                         bgfx::UniformType::Vec4);
    u_lightDir = bgfx::createUniform("u_lightDir",
                                     bgfx::UniformType::Vec4);
    u_lightPos = bgfx::createUniform("u_lightPos",
                                     bgfx::UniformType::Vec4);
    u_lightColor = bgfx::createUniform("u_lightColor",
                                       bgfx::UniformType::Vec4);
    u_shadowMatrix = bgfx::createUniform("u_shadowMatrix",
                                         bgfx::UniformType::Mat4);
    u_evsm = bgfx::createUniform("u_evsm", bgfx::UniformType::Vec4);
    u_localLight = bgfx::createUniform("u_localLight",
                                      bgfx::UniformType::Vec4,
                                      kLocalLights);
    u_localLightColor = bgfx::createUniform("u_localLightColor",
                                           bgfx::UniformType::Vec4,
                                           kLocalLights);
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
        m_progShadow = fcLoadProgram("vs_fc_shadow", "fs_fc_shadow",
                                     _BGFXLib.shaderPath().c_str());
        m_progShadowClip = fcLoadProgram("vs_fc_shadow_clip",
                                         "fs_fc_shadow_clip",
                                         _BGFXLib.shaderPath().c_str());
        if (m_instancing)
            m_progShadowInst = fcLoadProgram("vs_fc_shadow_inst",
                                             "fs_fc_shadow",
                                             _BGFXLib.shaderPath().c_str());
        m_progShadowBlur = fcLoadProgram("vs_fc_comp",
                                         "fs_fc_shadow_blur",
                                         _BGFXLib.shaderPath().c_str());
        u_shadowBlur = bgfx::createUniform("u_shadowBlur",
                                           bgfx::UniformType::Vec4);
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
        bulbShadowTex = bgfx::createTexture2D(
            uint16_t(kBulbShadowGrid * kBulbShadowTileSize),
            uint16_t(kBulbShadowGrid * kBulbShadowTileSize), false, 1,
            shadowFormat,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bulbShadowDepth = bgfx::createTexture2D(
            uint16_t(kBulbShadowGrid * kBulbShadowTileSize),
            uint16_t(kBulbShadowGrid * kBulbShadowTileSize), false, 1,
            bgfx::TextureFormat::D24S8,
            BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle bsAtt[2] = {bulbShadowTex,
                                        bulbShadowDepth};
        bulbShadowFbo = bgfx::createFrameBuffer(2, bsAtt, false);
        s_texBulbShadow = bgfx::createUniform(
            "s_texBulbShadow", bgfx::UniformType::Sampler);
        u_bulbShadowMtx = bgfx::createUniform(
            "u_bulbShadowMtx", bgfx::UniformType::Mat4,
            kBulbShadowTiles);
        u_bulbShadowConf = bgfx::createUniform(
            "u_bulbShadowConf", bgfx::UniformType::Vec4,
            kLocalLights - kMediumSlots);
        u_bulbShadowRot = bgfx::createUniform(
            "u_bulbShadowRot", bgfx::UniformType::Mat4);
        m_progShadowTint = fcLoadProgram("vs_fc_shadow",
                                         "fs_fc_shadow_tint",
                                         _BGFXLib.shaderPath().c_str());
        s_texShadowTint = bgfx::createUniform(
            "s_texShadowTint", bgfx::UniformType::Sampler);
    }
    static const uint32_t blackCube[6] = {0, 0, 0, 0, 0, 0};
    m_dummyEnvTex = bgfx::createTextureCube(1, false, 1,
        bgfx::TextureFormat::RGBA8, 0,
        bgfx::copy(blackCube, sizeof(blackCube)));

    u_matColor = bgfx::createUniform("u_matColor", bgfx::UniformType::Vec4);
    u_matEmissive = bgfx::createUniform("u_matEmissive", bgfx::UniformType::Vec4);
    u_matSpecular = bgfx::createUniform("u_matSpecular", bgfx::UniformType::Vec4);
    u_params = bgfx::createUniform("u_params", bgfx::UniformType::Vec4);
    u_polyOffset = bgfx::createUniform("u_polyOffset",
                                       bgfx::UniformType::Vec4);
    u_clipParams = bgfx::createUniform("u_clipParams", bgfx::UniformType::Vec4);
    u_clipPlanes = bgfx::createUniform("u_clipPlanes", bgfx::UniformType::Vec4,
                                       Render::Material::MaxClipPlanes);
    u_linePattern = bgfx::createUniform("u_linePattern", bgfx::UniformType::Vec4);

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
        m_progMeshOit = fcLoadProgram("vs_fc_mesh", "fs_fc_mesh_oit",
                                      _BGFXLib.shaderPath().c_str());
        m_progMeshOitClip = fcLoadProgram("vs_fc_mesh_clip",
                                          "fs_fc_mesh_oit_clip",
                                          _BGFXLib.shaderPath().c_str());
        m_progMeshOitTex = fcLoadProgram("vs_fc_mesh_tex",
                                         "fs_fc_mesh_oit_tex",
                                         _BGFXLib.shaderPath().c_str());
        m_progMeshOitTexClip = fcLoadProgram("vs_fc_mesh_tex_clip",
                                             "fs_fc_mesh_oit_tex_clip",
                                             _BGFXLib.shaderPath().c_str());
        if (m_instancing) {
            // WBOIT accumulation is order-independent, so transparent
            // instance groups are legal — but only while OIT runs
            // (the sorted fallback needs per-draw depth keys).
            m_progMeshInstOit = fcLoadProgram("vs_fc_mesh_inst",
                                              "fs_fc_mesh_oit",
                                              _BGFXLib.shaderPath().c_str());
            m_progMeshInstOitTex = fcLoadProgram("vs_fc_mesh_tex_inst",
                                                 "fs_fc_mesh_oit_tex",
                                                 _BGFXLib.shaderPath().c_str());
        }
        m_progComp = fcLoadProgram("vs_fc_comp", "fs_fc_comp",
                                   _BGFXLib.shaderPath().c_str());
        s_texAccum = bgfx::createUniform("s_texAccum",
                                         bgfx::UniformType::Sampler);
        s_texReveal = bgfx::createUniform("s_texReveal",
                                          bgfx::UniformType::Sampler);
    }

    // Render debugging buffer visualization (docs/RenderDebug.md).
    m_progDebug = fcLoadProgram("vs_fc_comp", "fs_fc_debug",
                                _BGFXLib.shaderPath().c_str());
    u_debugParams = bgfx::createUniform("u_debugParams",
                                        bgfx::UniformType::Vec4);
    m_progDebugScene = fcLoadProgram("vs_fc_debug_scene",
                                     "fs_fc_debug_scene",
                                     _BGFXLib.shaderPath().c_str());
    m_progDebugSceneClip = fcLoadProgram("vs_fc_debug_scene_clip",
                                         "fs_fc_debug_scene_clip",
                                         _BGFXLib.shaderPath().c_str());
    s_texDebugScene = bgfx::createUniform("s_texDebugScene",
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
        // (aoTex raw, aoBlurTex blurred) scale to ssaoW/ssaoH — their OWN
        // Render_SSAOResolution, independent of the reflection scale
        // (effW/effH) — and sample LINEAR, so a reduced-res AO upsamples
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
        bgfx::TextureHandle preatt[2] = {aoNormalZ, aoDepth};
        aoPrepassFbo = bgfx::createFrameBuffer(2, preatt, false);
        aoGenFbo = bgfx::createFrameBuffer(1, &aoTex, false);
        aoBlurFbo = bgfx::createFrameBuffer(1, &aoBlurTex, false);

        // GTAO depth pyramid: single-channel viewZ, POINT-sampled
        // (the gen pass picks a discrete level per tap). R32F when
        // renderable, else R16F — coarse levels only serve FAR taps,
        // whose coplanarity guard already tolerates fp16 steps; the
        // near taps keep reading the full-precision prepass.
        const bool mipR32 = 0 != (caps->formats[bgfx::TextureFormat::R32F]
                                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        const bool mipR16 = 0 != (caps->formats[bgfx::TextureFormat::R16F]
                                  & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER);
        aoMipCount = (mipR32 || mipR16) ? kAOMipLevels : 0;
        for (int m = 0; m < aoMipCount; ++m) {
            const uint16_t mw = uint16_t(std::max(1, width >> (m + 1)));
            const uint16_t mh = uint16_t(std::max(1, height >> (m + 1)));
            aoMipTex[m] = bgfx::createTexture2D(mw, mh, false, 1,
                mipR32 ? bgfx::TextureFormat::R32F
                       : bgfx::TextureFormat::R16F, aoFlags);
            aoMipFbo[m] = bgfx::createFrameBuffer(1, &aoMipTex[m],
                                                  false);
        }
        if (aoMipCount) {
            m_progGtaoDepth = fcLoadProgram("vs_fc_comp",
                                            "fs_fc_gtao_depths",
                                            _BGFXLib.shaderPath().c_str());
            if (!bgfx::isValid(m_progGtaoDepth))
                aoMipCount = 0;
        }
        static const char *const mipSamplerNames[kAOMipLevels] = {
            "s_texAOMip1", "s_texAOMip2", "s_texAOMip3",
            "s_texAOMip4", "s_texAOMip5", "s_texAOMip6"};
        for (int m = 0; m < kAOMipLevels; ++m)
            s_texAOMip[m] = bgfx::createUniform(
                mipSamplerNames[m], bgfx::UniformType::Sampler);

        m_progPrepass = fcLoadProgram("vs_fc_prepass", "fs_fc_prepass",
                                      _BGFXLib.shaderPath().c_str());
        m_progPrepassClip = fcLoadProgram("vs_fc_prepass_clip",
                                          "fs_fc_prepass_clip",
                                          _BGFXLib.shaderPath().c_str());
        // Medium interval depth writers: prepass layout with the
        // body's appearance slot in .x.
        m_progMedDepth = fcLoadProgram("vs_fc_prepass",
                                       "fs_fc_meddepth",
                                       _BGFXLib.shaderPath().c_str());
        m_progMedDepthClip = fcLoadProgram("vs_fc_prepass_clip",
                                           "fs_fc_meddepth_clip",
                                           _BGFXLib.shaderPath().c_str());
        u_mediumSlot = bgfx::createUniform("u_mediumSlot",
                                           bgfx::UniformType::Vec4);
        if (m_instancing)
            m_progPrepassInst = fcLoadProgram("vs_fc_prepass_inst",
                                              "fs_fc_prepass",
                                              _BGFXLib.shaderPath().c_str());
        m_progSsao = fcLoadProgram("vs_fc_comp", "fs_fc_ssao",
                                   _BGFXLib.shaderPath().c_str());
        m_progGtao = fcLoadProgram("vs_fc_comp", "fs_fc_gtao",
                                   _BGFXLib.shaderPath().c_str());
        m_progGtaoBlur = fcLoadProgram("vs_fc_comp", "fs_fc_gtao_blur",
                                       _BGFXLib.shaderPath().c_str());
        m_progSsaoBlur = fcLoadProgram("vs_fc_comp", "fs_fc_ssao_blur",
                                       _BGFXLib.shaderPath().c_str());
        // Cavity shares the prepass resources, so it is built with them
        // (m_ssao gates the whole block); the pass itself is gated by
        // its own config in render().
        m_progCavity = fcLoadProgram("vs_fc_comp", "fs_fc_cavity",
                                     _BGFXLib.shaderPath().c_str());
        u_cavityParams = bgfx::createUniform("u_cavityParams",
                                             bgfx::UniformType::Vec4);
        s_texNormalZ = bgfx::createUniform("s_texNormalZ",
                                           bgfx::UniformType::Sampler);
        s_texAONoise = bgfx::createUniform("s_texAONoise",
                                           bgfx::UniformType::Sampler);
        s_texAO = bgfx::createUniform("s_texAO",
                                      bgfx::UniformType::Sampler);
        u_aoParams = bgfx::createUniform("u_aoParams",
                                         bgfx::UniformType::Vec4);
        u_aoParams2 = bgfx::createUniform("u_aoParams2",
                                          bgfx::UniformType::Vec4);
        u_aoKernel = bgfx::createUniform("u_aoKernel",
                                         bgfx::UniformType::Vec4,
                                         kAOSamples);
        // 4x4 tiled random rotation vectors (xy packed *0.5+0.5),
        // fixed values so frames are deterministic. The .z channel packs a
        // 4x4 Bayer dither (0..255): the gen pass uses it to jitter the
        // per-pixel sample radius, so neighbouring pixels sample at
        // different distances and the 4x4 blur (one Bayer tile) averages
        // all 16 sub-radii — decorrelating the RADIAL occlusion banding
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

        // Glass body absorption interval: full-res front/back
        // depths of glass draws, written by the prepass programs
        // like the water medium interval (only .z viewZ and .w
        // validity are consumed). In the SSAO resource set because
        // the depth writer is the prepass shader family.
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
        bgfx::TextureHandle gfatt[2] = {glassFrontTex, glassFrontDepth};
        glassFrontFbo = bgfx::createFrameBuffer(2, gfatt, false);
        bgfx::TextureHandle gbatt[2] = {glassBackTex, glassBackDepth};
        glassBackFbo = bgfx::createFrameBuffer(2, gbatt, false);
    }

    // Volumetric light shafts: the half-res raymarch reads the SSAO
    // prepass depth (ray end) and the shadow moments (light
    // visibility), so it needs both resource sets. Point-sampled —
    // the apply pass does its own bilateral 4-tap upsample.
    m_vol = m_ssao && m_shadow;
    if (m_vol) {
        uint16_t hw = std::max<uint16_t>(1, width / 2);
        uint16_t hh = std::max<uint16_t>(1, height / 2);
        volTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        // Second attachment: the front-of-water inscatter segment
        // (rgb) + its transmittance (a), linearly filtered for the
        // full-res front apply.
        volFrontTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::TextureHandle volAtt[2] = {volTex, volFrontTex};
        volFbo = bgfx::createFrameBuffer(2, volAtt, false);
        // History pair for the temporal accumulation; sampling
        // flags mirror the current-frame targets (the main apply
        // does its own bilateral upsample from point taps, the
        // front apply reads linearly).
        volHistTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        volHistFrontTex = bgfx::createTexture2D(hw, hh, false, 1,
            bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_RT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        bgfx::TextureHandle volHistAtt[2] = {volHistTex,
                                             volHistFrontTex};
        volHistFbo = bgfx::createFrameBuffer(2, volHistAtt, false);
        m_progVol = fcLoadProgram("vs_fc_comp", "fs_fc_volume",
                                  _BGFXLib.shaderPath().c_str());
        m_progVolAccum = fcLoadProgram("vs_fc_comp",
                                       "fs_fc_volume_accum",
                                       _BGFXLib.shaderPath().c_str());
        s_texVolFront = bgfx::createUniform(
            "s_texVolFront", bgfx::UniformType::Sampler);
        m_progVolApply = fcLoadProgram("vs_fc_comp",
                                       "fs_fc_volume_apply",
                                       _BGFXLib.shaderPath().c_str());
        s_texVol = bgfx::createUniform("s_texVol",
                                       bgfx::UniformType::Sampler);
        u_volParams = bgfx::createUniform("u_volParams",
                                          bgfx::UniformType::Vec4);
        u_volMedium = bgfx::createUniform("u_volMedium",
                                          bgfx::UniformType::Vec4);
        u_volTexel = bgfx::createUniform("u_volTexel",
                                         bgfx::UniformType::Vec4);

        // Water medium: full-res front/back depths of water body
        // draws, written by the prepass programs (only .z viewZ and
        // .w validity are consumed).
        const uint64_t waterFlags = 0
            | BGFX_TEXTURE_RT
            | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
            | BGFX_SAMPLER_MIP_POINT
            | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        waterFrontTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, waterFlags);
        waterBackTex = bgfx::createTexture2D(width, height, false, 1,
            bgfx::TextureFormat::RGBA16F, waterFlags);
        waterFrontDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        waterBackDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle fatt[2] = {waterFrontTex, waterFrontDepth};
        waterFrontFbo = bgfx::createFrameBuffer(2, fatt, false);
        bgfx::TextureHandle batt[2] = {waterBackTex, waterBackDepth};
        waterBackFbo = bgfx::createFrameBuffer(2, batt, false);
        m_progVolExt = fcLoadProgram("vs_fc_comp", "fs_fc_volume_ext",
                                     _BGFXLib.shaderPath().c_str());
        s_texWaterFront = bgfx::createUniform(
            "s_texWaterFront", bgfx::UniformType::Sampler);
        s_texWaterBack = bgfx::createUniform(
            "s_texWaterBack", bgfx::UniformType::Sampler);
        u_waterSigma = bgfx::createUniform("u_waterSigma",
                                           bgfx::UniformType::Vec4,
                                           kMediumSlots);
        // Water caustics: a fullscreen light-space pattern splat
        // over the prepass surfaces inside the water interval.
        m_progCaustics = fcLoadProgram("vs_fc_comp", "fs_fc_caustics",
                                       _BGFXLib.shaderPath().c_str());
        u_causticParams = bgfx::createUniform(
            "u_causticParams", bgfx::UniformType::Vec4,
            kMediumSlots);
        // Cloud body medium: its own front/back interval pair (the
        // per-medium-kind slot scheme), FBM density in the
        // raymarch.
        cloudFrontTex = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::RGBA16F, waterFlags);
        cloudBackTex = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::RGBA16F, waterFlags);
        cloudFrontDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        cloudBackDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle cfatt[2] = {cloudFrontTex, cloudFrontDepth};
        cloudFrontFbo = bgfx::createFrameBuffer(2, cfatt, false);
        bgfx::TextureHandle cbatt[2] = {cloudBackTex, cloudBackDepth};
        cloudBackFbo = bgfx::createFrameBuffer(2, cbatt, false);
        s_texCloudFront = bgfx::createUniform(
            "s_texCloudFront", bgfx::UniformType::Sampler);
        s_texCloudBack = bgfx::createUniform(
            "s_texCloudBack", bgfx::UniformType::Sampler);
        u_cloudParams = bgfx::createUniform("u_cloudParams",
                                            bgfx::UniformType::Vec4,
                                            kMediumSlots);
        // Fire body medium: its own front/back interval pair (the
        // per-medium-kind slot scheme), emissive FBM flame in the
        // raymarch.
        fireFrontTex = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::RGBA16F, waterFlags);
        fireBackTex = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::RGBA16F, waterFlags);
        fireFrontDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        fireBackDepth = bgfx::createTexture2D(width, height, false,
            1, bgfx::TextureFormat::D24S8,
            waterFlags | BGFX_TEXTURE_RT_WRITE_ONLY);
        bgfx::TextureHandle ffatt[2] = {fireFrontTex, fireFrontDepth};
        fireFrontFbo = bgfx::createFrameBuffer(2, ffatt, false);
        bgfx::TextureHandle fbatt[2] = {fireBackTex, fireBackDepth};
        fireBackFbo = bgfx::createFrameBuffer(2, fbatt, false);
        s_texFireFront = bgfx::createUniform(
            "s_texFireFront", bgfx::UniformType::Sampler);
        s_texFireBack = bgfx::createUniform(
            "s_texFireBack", bgfx::UniformType::Sampler);
        u_fireParams = bgfx::createUniform("u_fireParams",
                                           bgfx::UniformType::Vec4,
                                           kMediumSlots);
        u_fireParams2 = bgfx::createUniform("u_fireParams2",
                                            bgfx::UniformType::Vec4,
                                            kMediumSlots);
        u_fireFrame = bgfx::createUniform("u_fireFrame",
                                          bgfx::UniformType::Mat4,
                                          kMediumSlots);
        u_fountainParams = bgfx::createUniform(
            "u_fountainParams", bgfx::UniformType::Vec4,
            kMediumSlots);
        u_fountainFrame = bgfx::createUniform(
            "u_fountainFrame", bgfx::UniformType::Mat4,
            kMediumSlots);
    }

    // Water surface refraction: the scene color copies into a
    // linearly-sampled texture the surface shader offsets into.
    // Independent of the volumetric resource sets.
    sceneCopyTex = bgfx::createTexture2D(width, height, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    sceneCopyFbo = bgfx::createFrameBuffer(1, &sceneCopyTex, false);
    m_progWaterCopy = fcLoadProgram("vs_fc_comp", "fs_fc_copy",
                                    _BGFXLib.shaderPath().c_str());
    m_progWater = fcLoadProgram("vs_fc_mesh", "fs_fc_water",
                                _BGFXLib.shaderPath().c_str());
    s_texScene = bgfx::createUniform("s_texScene",
                                     bgfx::UniformType::Sampler);
    s_texRefl = bgfx::createUniform("s_texRefl",
                                    bgfx::UniformType::Sampler);
    u_waterSurf = bgfx::createUniform("u_waterSurf",
                                      bgfx::UniformType::Vec4);
    u_waterAbsorb = bgfx::createUniform("u_waterAbsorb",
                                        bgfx::UniformType::Vec4);
    u_waterRipple = bgfx::createUniform("u_waterRipple",
                                        bgfx::UniformType::Vec4);
    u_waterSplash = bgfx::createUniform("u_waterSplash",
                                        bgfx::UniformType::Vec4,
                                        kMediumSlots);
    // The surface shader's refraction depth reject samples the SSAO
    // prepass; without those resources the sampler uniform still
    // has to exist for the (disabled) stage binding.
    if (!bgfx::isValid(s_texNormalZ))
        s_texNormalZ = bgfx::createUniform("s_texNormalZ",
                                           bgfx::UniformType::Sampler);

    // Glass surface: refraction from the same scene copy, plus the
    // absorption interval targets of the SSAO resource set (the
    // glass pass is gated on both).
    m_progGlass = fcLoadProgram("vs_fc_mesh", "fs_fc_glass",
                                _BGFXLib.shaderPath().c_str());
    s_texGlassFront = bgfx::createUniform("s_texGlassFront",
                                          bgfx::UniformType::Sampler);
    s_texGlassBack = bgfx::createUniform("s_texGlassBack",
                                         bgfx::UniformType::Sampler);
    u_glassParams = bgfx::createUniform("u_glassParams",
                                        bgfx::UniformType::Vec4);

    // Ground/planar reflection: the mirrored-camera scene re-render
    // target. This full opaque-scene re-render is the priciest effect on
    // a large window, so it scales to effW/effH (Render_EffectResolution);
    // LINEAR sampling upscales it smoothly onto the full-res consumers
    // (ground overlay, water surface), which read it at normalized UV.
    reflTex = bgfx::createTexture2D(effW, effH, false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_RT
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    reflDepth = bgfx::createTexture2D(effW, effH, false, 1,
        bgfx::TextureFormat::D24S8,
        BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::TextureHandle ratt[2] = {reflTex, reflDepth};
    reflFbo = bgfx::createFrameBuffer(2, ratt, false);
    if (!bgfx::isValid(m_progReflMedia))
        m_progReflMedia = fcLoadProgram("vs_fc_comp",
                                        "fs_fc_refl_media",
                                        _BGFXLib.shaderPath().c_str());
    m_progGroundRefl = fcLoadProgram("vs_fc_mesh", "fs_fc_groundrefl",
                                     _BGFXLib.shaderPath().c_str());
    u_reflParams = bgfx::createUniform("u_reflParams",
                                       bgfx::UniformType::Vec4);

    // Stateful particle resources (docs/RenderEngine.md §5.8).
    // Size-independent, so they are created once and kept across
    // the resize-driven rebuilds around them — hence the validity
    // guards rather than plain assignment.
    if (!bgfx::isValid(m_progPSimInit)) {
        m_progPSimInit = fcLoadProgram("vs_fc_comp", "fs_fc_psim_init",
                                       _BGFXLib.shaderPath().c_str());
        s_pstate0 = bgfx::createUniform("s_pstate0",
                                        bgfx::UniformType::Sampler);
        s_pstate1 = bgfx::createUniform("s_pstate1",
                                        bgfx::UniformType::Sampler);
        u_pgrid = bgfx::createUniform("u_pgrid",
                                      bgfx::UniformType::Vec4);
        u_pboxMin = bgfx::createUniform("u_pboxMin",
                                        bgfx::UniformType::Vec4);
        u_pboxMax = bgfx::createUniform("u_pboxMax",
                                        bgfx::UniformType::Vec4);
        const uint16_t fmtCaps = bgfx::getCaps()->formats[
            bgfx::TextureFormat::RGBA32F];
        particleStateOk = bgfx::isValid(m_progPSimInit)
            && (fmtCaps & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
        if (!particleStateOk)
            std::printf("bgfx: no RGBA32F render target — stateful "
                        "particle emitters fall back to stateless\n");
        m_progPImpact = fcLoadProgram("vs_fc_pimpact", "fs_fc_pimpact",
                                      _BGFXLib.shaderPath().c_str());
        s_pimpsrc = bgfx::createUniform("s_pimpsrc",
                                        bgfx::UniformType::Sampler);
        u_impactFrame = bgfx::createUniform("u_impactFrame",
                                            bgfx::UniformType::Vec4);
        u_impactNow = bgfx::createUniform("u_impactNow",
                                          bgfx::UniformType::Vec4);
        s_texImpact = bgfx::createUniform("s_texImpact",
                                          bgfx::UniformType::Sampler);
        u_waterImpact = bgfx::createUniform("u_waterImpact",
                                            bgfx::UniformType::Vec4);
        u_waterImpactCfg = bgfx::createUniform(
            "u_waterImpactCfg", bgfx::UniformType::Vec4);
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
        // Nothing else copies the scene onto the backbuffer here.
        {m_progPresent, "fc_copy"},
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

void BGFXView::setMeshVertexBuffers(GpuMesh *gpu, const Render::MeshData &mesh)
{
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    bgfx::setVertexBuffer(1, bgfx::isValid(gpu->color)
                                 ? gpu->color
                                 : whiteColors(mesh.numVertices));
}

void BGFXView::collectMeshes()
{
    for (auto it = meshes.begin(); it != meshes.end();) {
        if (it->second.lastUsed + 2 < frame) {
            it->second.destroy();
            it = meshes.erase(it);
        } else
            ++it;
    }
    // After the meshes: any geometry a surviving mesh references was
    // touched this frame through it, so an aged geometry has no
    // referencing mesh left and can go.
    for (auto it = geometries.begin(); it != geometries.end();) {
        if (it->second.lastUsed + 2 < frame) {
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
}

#ifdef FC_RENDERER_STANDALONE
void BGFXView::present()
{
    TransientVertex::init();
    if (bgfx::getAvailTransientVertexBuffer(3, TransientVertex::ms_layout)
            < 3)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, 3, TransientVertex::ms_layout);
    auto *v = reinterpret_cast<TransientVertex *>(tvb.data);
    v[0] = {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[1] = { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    v[2] = {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xffffffff};
    bgfx::setTexture(0, s_texScene, bgfxColor);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(vid(ViewPresent), m_progPresent);
    ++drawcount;
}

#else // !FC_RENDERER_STANDALONE

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
        // glReadPixels rows are bottom-up; PPM top-down.
        std::vector<unsigned char> row(size_t(width) * 3);
        for (int y = height - 1; y >= 0; --y) {
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
    return img.mirrored().save(QString::fromStdString(path));
}

void BGFXView::blit(const Render::FrameDumpRequest *dump,
          Render::RenderStats *stats)
{
    // Only GL 1.1 is exported by Windows' opengl32, so the framebuffer entry
    // points below must be resolved against the current context rather than
    // called directly -- ELF systems get them from libGL and never noticed.
    // QOpenGLExtraFunctions (not the base QOpenGLFunctions set) is what
    // carries glBlitFramebuffer.
    auto *f = QOpenGLContext::currentContext()->extraFunctions();
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, (GLint *) &prevFbo);
    if (!hasFBO) {
        hasFBO = true;
        GLuint colorBuffer = bgfx::getInternal(bgfxColor);
        GLuint depthBuffer = bgfx::getInternal(bgfxDepth);
        blitColorId = colorBuffer;
        std::printf("bgfx: blit cache create msaa %d color %u (isTex %d) "
                    "depth %u\n", msaaSamples, colorBuffer,
                    int(glIsTexture(colorBuffer)), depthBuffer);
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
            destroy();
            return;
        }
        f->glGenFramebuffers(1, &fboDepth);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fboDepth);
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                    GL_RENDERBUFFER, depthBuffer);
        f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                    GL_RENDERBUFFER, depthBuffer);
        // No color attachment: complete only with the draw/read
        // buffers off.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (!checkFramebufferStatus()) {
            f->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
            destroy();
            return;
        }
    }

    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevFbo);
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    f->glBlitFramebuffer(0, 0, width, height,
                         0, 0, width, height,
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
                         0, 0, width, height,
                         GL_DEPTH_BUFFER_BIT,
                         GL_NEAREST);
    checkGLError("blit depth");

    // Frame readback: the grandfathered per-frame env gates
    // (FC_BGFX_DEBUG_READBACK stderr stats + FC_BGFX_DEBUG_DUMP_FRAME
    // PPM) and the one-shot requestFrameDump captures share one
    // read; this is the only view of what the DESKTOP GL path
    // actually renders — the streamed viewer renders with its own
    // (WASM) backend, so stream screenshots cannot show
    // desktop-specific artifacts.
    static const bool readback = (getenv("FC_BGFX_DEBUG_READBACK") != nullptr);
    if (readback || dump) {
        std::vector<float> depth(width * height);
        std::vector<unsigned char> color(width * height * 4);
        glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT,
                     depth.data());
        // The color lives in the bgfx color FBO — the read binding
        // still points at the depth-only FBO here (color would read
        // back all zero).
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                     color.data());
        long n = 0, r = 0, g = 0, b = 0;
        for (int i = 0; i < width * height; ++i) {
            if (depth[i] < 0.999f) {
                ++n;
                r += color[i*4];
                g += color[i*4 + 1];
                b += color[i*4 + 2];
            }
        }
        if (stats) {
            stats->width = width;
            stats->height = height;
            stats->geometryPixels = n;
            stats->avgColor[0] = n ? float(r) / float(n) : -1.0f;
            stats->avgColor[1] = n ? float(g) / float(n) : -1.0f;
            stats->avgColor[2] = n ? float(b) / float(n) : -1.0f;
            stats->valid = true;
        }
        if (readback) {
            fprintf(stderr,
                    "bgfx fbo %dx%d: %ld geometry pixels, avg color %ld,%ld,%ld\n",
                    width, height, n,
                    n ? r/n : -1, n ? g/n : -1, n ? b/n : -1);
            static const char *envDump = getenv("FC_BGFX_DEBUG_DUMP_FRAME");
            if (envDump && *envDump)
                writeDumpImage(envDump, color.data(), width, height);
        }
        if (dump && !dump->path.empty()
                && !writeDumpImage(dump->path, color.data(),
                                   width, height))
            fprintf(stderr, "bgfx: frame dump write failed: %s\n",
                    dump->path.c_str());
    }
    f->glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
}
#endif // !FC_RENDERER_STANDALONE
