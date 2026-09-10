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

namespace {
/// Does this draw's per-face palette want the mesh's own texture
/// coordinates? A tile size <= 0 says so (Material::facetexscale), and
/// only the shader's TEXTURE variant carries v_texcoord0 to answer it.
bool faceTexOnMeshUV(const Render::Material &mat)
{
    return mat.texturepalette && !mat.texturepalette->entries.empty()
        && mat.facetexscale <= 0.0f;
}

/// How well one override entry matches a draw's container chain
/// (docs/CoinRetirement.md 5.9): -1 = no match, otherwise a score
/// ordering DEEPEST decision point first -- an entry that names an
/// object deeper on the chain beats one naming an ancestor, whatever
/// their forms -- with a rooted entry beating a bare one at equal
/// depth.
///
/// A rooted entry must anchor at path[0] and its remaining elements
/// must follow IN ORDER, but not contiguously: a subname elides
/// objects the scene chain contains (a Link's target has a chain step
/// but no subname token), so the pattern is an ordered subsequence.
/// A bare entry's single element may sit anywhere on the path.
int matchStyleOverride(const Render::StyleOverride &ov,
                       const std::vector<Render::ObjectRef> &path)
{
    if (ov.path.empty() || path.empty())
        return -1;
    if (!ov.rooted) {
        for (int i = int(path.size()) - 1; i >= 0; --i) {
            if (ov.path[0] == path[size_t(i)])
                return i * 2;
        }
        return -1;
    }
    if (!(ov.path[0] == path[0]))
        return -1;
    size_t p = 1;
    size_t depth = 0;
    for (size_t i = 1; p < ov.path.size() && i < path.size(); ++i) {
        if (ov.path[p] == path[i]) {
            depth = i;
            ++p;
        }
    }
    if (p != ov.path.size())
        return -1;
    return int(depth) * 2 + 1;
}
} // namespace

bool BGFXStyleState::styleAdmits(const Render::DrawCall &draw)
{
    // This sub-view's Class-A display style, resolved PER OBJECT the
    // way Rhino and SolidWorks resolve a display mode
    // (docs/CoinRetirement.md 5.8, 5.9), in order:
    //
    // - the per-object per-view override, when an entry names this
    //   draw's object or a container above it. It replaces the view's
    //   style for that object; a pin ("As Is") holds the object to its
    //   own mode, escaping the view style. Only ever present under a
    //   superset capture.
    // - this sub-view's style, where the object's display-mode switch
    //   carries a child of that style's NAME -- a style is an override,
    //   and that is what an override does. Only under a superset
    //   capture; without one the mask is applied flat, which is only
    //   ever asked for where every cell's style provably removes
    //   rather than adds (ViewAreaCanvas::styleConflicts). Where the
    //   capture carried the style's mode additively (drawStyleMode,
    //   5.11) the mode's own tagged draws serve it instead of a mask
    //   over the superset child.
    // - the object's OWN mode otherwise: a sub-view showing "As Is",
    //   and equally an object whose switch has no child of the asked
    //   name, which today's traversal already leaves in its own mode.
    // - nothing, when the own mode is StyleUnknown: a mode no mask can
    //   describe must not be filtered by one.
    //
    // The bucket test is what the draw RENDERS as, not mat.type
    // (Render::styleBitOf): Mesh re-styles one node into its Wireframe
    // and Point modes. Gizmo draws are exempt -- a style selects
    // display-mode children and the navigation gizmos sit under no
    // such switch; skipbounds marks them.
    //
    // Both the per-draw submit and the instanced group partition ask
    // this question: an instance group merges draws by geometry and
    // material, NOT by objectKey, so its members can resolve
    // differently and each must be admitted on its own.
    if (draw.skipbounds)
        return true;
    const OvStyle *ov = lookupStyleOverride(draw.objectKey);
    if (ov && ov->modeId) {
        // The override names a mode (5.9 "Non-standard modes" -- and
        // since the Mesh finding, Class-A values too): the mode is its
        // own SUBGRAPH, captured additively and tagged, not a mask
        // over the superset -- a mask cannot serve a mode whose
        // buckets the superset child does not contain (Mesh's
        // "Points": its Flat Lines child has no point rendering).
        //
        // - a tagged draw is the mode's own subgraph: admitted exactly
        //   when it is THIS mode's;
        // - an untagged draw whose switch normally traversed this very
        //   mode already IS it -- no tagged copy exists, draw as is;
        // - otherwise, when the switch has a child of the mode's name
        //   (the interest bit), the tagged subgraph replaces the
        //   normal draws: suppress them;
        // - a Class-A entry the additive capture did not cover (an
        //   interest list past its budget) falls back to the mask
        //   over the superset, where the name is registered;
        // - and an object with no child of the name keeps its own
        //   mode, the same fallback a Class-A style takes.
        if (draw.capturedMode)
            return draw.capturedMode == ov->modeId;
        if (draw.traversedMode == ov->modeId)
            return true;
        if (ov->interestBit && (draw.interestBits & ov->interestBit))
            return false;
        uint8_t effective = draw.ownStyle;
        if (!ov->interestBit && ov->nameBit
                && (draw.registeredStyles & ov->nameBit))
            effective = ov->mask;
        return effective == Render::StyleAsIs
            || effective == Render::StyleUnknown
            || (effective & Render::styleBitOf(draw.material)) != 0;
    }
    if (!ov && drawStyleMode) {
        // No override entry names this object, and this sub-view's own
        // style is one the capture carried ADDITIVELY (5.11): a style
        // IS an override, so it resolves by the same three rules the
        // clause above uses -- the mode's tagged draws are it, the
        // normal flow already is it where the switch traversed that
        // very child, and the untagged draws step aside where a tagged
        // copy exists. This is what lets a cell whose style names a
        // mode the superset child cannot produce (Mesh's "Points")
        // stay on a shared canvas instead of being evicted from it.
        //
        // Falling through means the switch has no child of the name:
        // the object keeps its own mode, the same fallback the mask
        // path below reaches through registeredStyles.
        if (draw.capturedMode)
            return draw.capturedMode == drawStyleMode;
        if (draw.traversedMode == drawStyleMode)
            return true;
        if (draw.interestBits & drawStyleModeBit)
            return false;
    }
    if (draw.capturedMode) {
        // An additively captured draw serves exactly one thing: an
        // override -- or, since 5.11, a view style -- resolving to its
        // very mode. Every other resolution must drop it, or the
        // object double-draws.
        return false;
    }
    uint8_t effective = drawStyleMask;
    if (ov) {
        effective = ov->pin ? draw.ownStyle
            : (draw.registeredStyles & ov->nameBit) ? ov->mask
                                                    : draw.ownStyle;
    }
    else if (styleFromSuperset) {
        effective = (drawStyleName
                     && (draw.registeredStyles & drawStyleName))
                ? drawStyleMask : draw.ownStyle;
    }
    return effective == Render::StyleAsIs
        || effective == Render::StyleUnknown
        || (effective & Render::styleBitOf(draw.material)) != 0;
}

const BGFXStyleState::OvStyle *
BGFXStyleState::lookupStyleOverride(uint64_t objectKey)
{
    if (!ovCache || !ovTable || !objectKey)
        return nullptr;
    auto it = ovCache->map.find(objectKey);
    if (it == ovCache->map.end()) {
        // First sight of this key under the current table: walk the
        // entries once and cache the outcome either way, so the table
        // costs one hash lookup per draw after this.
        OvStyle s;
        if (ovInfo) {
            auto oit = ovInfo->find(objectKey);
            if (oit != ovInfo->end() && !oit->second.path.empty()) {
                int best = -1;
                for (const auto &ov : ovTable->entries) {
                    int score = matchStyleOverride(ov, oit->second.path);
                    if (score > best) {
                        best = score;
                        s.mask = ov.mask;
                        s.nameBit = ov.nameBit;
                        s.pin = ov.pin;
                        s.modeId = ov.modeId;
                        // The mode's interestBits bit under the
                        // capture's interest list; 0 when the list
                        // does not carry the mode (never captured:
                        // the entry falls back to the own mode).
                        s.interestBit = (ov.modeId && ovInterest)
                                ? ovInterest->bitOf(ov.modeId) : 0;
                        s.has = true;
                    }
                }
            }
        }
        it = ovCache->map.emplace(objectKey, s).first;
    }
    return it->second.has ? &it->second : nullptr;
}

void
BGFXView::pushUserImages(const Render::UserShader &shader)
{
    if (shader.dialect != Render::UserShader::Dialect::MaterialX)
        return;
    // The document's images stacked as the layers of ONE array, in the
    // order the generated code names them -- which is what lets a
    // material carry more maps than the mesh shader has units free
    // (docs/CyclesIntegration.md sec 6.12).
    //
    // Joined on the PATH, because the two halves are produced by sides
    // that cannot see each other's naming: the generator resolved the
    // file, the capture decoded it. A layer whose pixels never arrived
    // is left null and uploads white, which is the map missing and
    // nothing else -- never another draw's texture.
    Render::TexturePalette layers;
    std::string samplerName;
    int unit = 0;
#ifndef FC_RENDERER_STANDALONE
    const auto &variant = _BGFXLib.materialXVariant(shader);
    if (variant.images.empty() || variant.imageSampler.empty())
        return;
    samplerName = variant.imageSampler;
    unit = variant.imageUnit;
    layers.entries.resize(variant.images.size());
    for (const auto &want : variant.images) {
        if (want.layer < 0 || want.layer >= int(layers.entries.size()))
            continue;
        for (const auto &have : shader.images) {
            if (have.path == want.path && have.image) {
                layers.entries[want.layer] = have.image;
                break;
            }
        }
    }
#else
    // No generator on this tier: the producer made the join once and
    // shipped the answer on each image (UserShader::Image::layer) with
    // the sampler and unit its program declares (docs/MaterialStorage.md
    // sec 17.23). A snapshot older than that carries no images at all,
    // and no program to bind them to either.
    if (shader.imageSampler.empty())
        return;
    samplerName = shader.imageSampler;
    unit = shader.imageUnit;
    int count = 0;
    for (const auto &have : shader.images)
        count = std::max(count, have.layer + 1);
    if (count <= 0)
        return;
    layers.entries.resize(size_t(count));
    for (const auto &have : shader.images) {
        if (have.layer >= 0 && have.image)
            layers.entries[size_t(have.layer)] = have.image;
    }
#endif
    const bgfx::UniformHandle sampler = _BGFXLib.userSampler(samplerName);
    if (!bgfx::isValid(sampler))
        return;
    // The white 1x1 stand-in when the array cannot be built at all (no
    // array-texture support, or the upload was refused): a sampler2DArray
    // left unbound is undefined rather than merely blank.
    bgfx::TextureHandle tex = m_whiteTexArray;
    if (GpuTextureArray *array =
            getTextureArray(layers, int(layers.entries.size()),
                            GpuTextureArray::MaterialSide))
        tex = array->handle;
    if (bgfx::isValid(tex))
        bgfx::setTexture(uint8_t(unit), sampler, tex);
}

void BGFXView::bindTextureStage(const Render::Material &mat, bool bumped,
                      bool mapped)
{
    // u_texParams: x = texture environment (TextureImage::Model),
    // y = the source format carries alpha (GL's REPLACE keeps the
    // fragment alpha for alpha-less formats; the RGBA8 expansion
    // hides that distinction from the sampler), z = emissive map
    // present, w = occlusion map present. The bump-only
    // stand-in modulates by opaque white.
    float texParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float blend[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::TextureHandle color = m_whiteTex;
    if (mat.texture) {
        color = getTexture(*mat.texture)->handle;
        texParams[0] = float(mat.texture->model);
        texParams[1] = mat.texture->numComponents == 2
                || mat.texture->numComponents == 4
            ? 1.0f : 0.0f;
        unpackAuthoredColor(mat.texture->blendColor, blend, colorManaged());
    }
    if (mapped && mat.emissivemap)
        texParams[2] = 1.0f;
    if (mapped && mat.occlusionmap)
        texParams[3] = 1.0f;
    bool mrmapped = mapped && mat.metallicroughnessmap;
    bgfx::setTexture(0, s_texColor, color);
    bgfx::setUniform(u_texParams, texParams);
    bgfx::setUniform(u_texBlendColor, blend);
    float texmat[16];
    if (mat.texidentity)
        bx::mtxIdentity(texmat);
    else
        std::memcpy(texmat, mat.texmatrix, sizeof(texmat));
    bgfx::setUniform(u_texMatrix, texmat);

    // Bump map at unit 2: x = mode (1 normal map, 2 height,
    // 3 height + parallax), y = strength (normal-map slope
    // multiplier / height amplitude in UV units), zw = one
    // texel in UV. White stand-in when off (branch not taken).
    float bumpParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::TextureHandle bump = m_whiteTex;
    if (bumped) {
        const auto &bm = *mat.bumpmap;
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

    // Emissive/occlusion maps at units 4/5 (flagged in
    // u_texParams.zw above; white stand-ins are never sampled).
    bgfx::setTexture(4, s_texEmissive,
                     texParams[2] > 0.5f
                         ? getTexture(*mat.emissivemap)->handle
                         : m_whiteTex);
    bgfx::setTexture(5, s_texOcclusion,
                     texParams[3] > 0.5f
                         ? getTexture(*mat.occlusionmap)->handle
                         : m_whiteTex);

    // Metallic-roughness map at unit 6 (flagged via
    // u_pbrParams.x = 2 above; the white stand-in is never
    // sampled).
    bgfx::setTexture(
        6, s_texMetallicRoughness,
        mrmapped ? getTexture(*mat.metallicroughnessmap)->handle
                 : m_whiteTex);
}

void BGFXView::setColorSpaceUniform()
{
    // What the VERTEX stages need to know: whether the 8-bit colour
    // streams they carry are authored (sRGB) or already linear.
    //
    // ! Called EXACTLY ONCE per frame, at the head of the view's
    // submission phase. A bgfx uniform holds its value for every
    // later draw in the frame, and setting one twice for the same
    // draw call is an assert, not a redundancy -- which is what a
    // second belt-and-braces call from the draw paths tripped.
    const float cs[4] = {colorManaged() ? 1.0f : 0.0f,
                         0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_colorSpace, cs);
}

void BGFXView::setAmbientUniform(const Render::Material &mat)
{
    // GL's ambient term is the material's ambient colour times
    // LIGHT_MODEL_AMBIENT (Coin: SoEnvironment), added outright -- it
    // is not a fraction of the diffuse, and it does not scale with any
    // light. Feeding it is what makes an ambient colour mean anything
    // here; before, Material::ambient crossed the bridge and was
    // dropped.
    float amb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (viewAmbientFed) {
        float m[4], g[4];
        unpackAuthoredColor(mat.ambient, m, colorManaged());
        unpackAuthoredColor(viewAmbient, g, colorManaged());
        for (int i = 0; i < 3; ++i)
            amb[i] = m[i] * g[i];
        amb[3] = 1.0f;
    }
    // w = 0 leaves the shader on the legacy floor (0.2 * base with the
    // 0.8 diffuse weight), which is what an old scene dump was drawn
    // with and has to keep being drawn with.
    bgfx::setUniform(u_ambient, amb);

    // The metallic/roughness branch wants the light-model ambient on its
    // own: there the surface is stated by the BRDF, so multiplying in a
    // Phong ambient colour would state it twice -- and a material read as
    // metallic/roughness has no meaningful ambient slot to read anyway.
    float envAmb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (viewAmbientFed) {
        unpackAuthoredColor(viewAmbient, envAmb, colorManaged());
        envAmb[3] = 1.0f;
    }
    bgfx::setUniform(u_envAmbient, envAmb);
}

void BGFXView::setTriangleFrameState(const Render::Material &mat, int pass,
                           bool mapped, bool aoDraw)
{
    float pbrParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::TextureHandle env = m_dummyEnvTex;
    if (pbrFrame && mat.lighting && pass != PassDepthOnly) {
        // x = 2 flags a metallic-roughness map on top of the
        // branch (u_texParams has no free component; the map
        // only matters to the PBR path anyway).
        pbrParams[0] = mapped && mat.metallicroughnessmap
            ? 2.0f : 1.0f;
        // Per-object overrides (SoFCRenderMaterial, from
        // ViewProvider Render_* properties) beat the frame config.
        float metal = mat.metallic >= 0.0f ? mat.metallic
                                           : pbrMetallic;
        pbrParams[1] = bx::clamp(metal, 0.0f, 1.0f);
        // Nothing states a metalness -- not the material, not the frame,
        // and no metallic-roughness map states one per texel either. Ask
        // the shader to read the Phong specular colour as material data
        // instead of dropping it (fcBaseFromSpecular); a NEGATIVE
        // metalness is that request, and it is the only way the question
        // can be asked per fragment, which is what a per-vertex or
        // per-face colour needs.
        if (pbrFromSpecular && mat.metallic < 0.0f && pbrMetallic <= 0.0f
                && pbrParams[0] < 1.5f)
            pbrParams[1] = -1.0f;
        float rough = mat.roughness >= 0.0f ? mat.roughness
                                            : pbrRoughness;
        if (rough <= 0.0f) {
            // Derive from the material shininess with the usual
            // Blinn-Phong-to-GGX conversion. That match is on the GGX
            // WIDTH -- alpha = sqrt(2 / (n + 2)) -- and the shader
            // squares this value to get the width back, so the fourth
            // root is what belongs here. The square root handed the
            // shader an alpha to square a second time, which is why
            // every Phong appearance read as a mirror (shininess 0.2,
            // FreeCAD's default, arrived at alpha 0.073 instead of
            // 0.269).
            //
            // What the shininess MEANS is the frame's to say
            // (PBRConfig::shininessMapping). Coin's 0..1 is the
            // fixed-function GL exponent scaled onto 0..128, and read
            // that way it is faithful but cannot express a sharp
            // surface: 128 is the sharpest exponent GL could state and
            // converts to roughness 0.35, so the lower half of the
            // range is unreachable from shininess however hard the
            // slider is pushed. Read instead as the 0..100% appearance
            // control the dialog presents, the odds transform
            // s / (1 - s) spends the same 128 at the HALFWAY point and
            // runs to infinity at one -- matte at zero, a mirror at
            // one, and within a few percent of the GL reading over the
            // low values real materials actually carry.
            const float shininess =
                bx::clamp(mat.shininess, 0.0f, 1.0f);
            float exponent;
            if (pbrShininessMapping == 1) {
                // 1 - s underflows to zero at the very top; the clamp
                // is what makes that a very sharp surface rather than
                // an infinity, and the shader's own lower clamp
                // finishes the job.
                const float denom = std::max(1.0f - shininess, 1.0e-4f);
                exponent = 128.0f * shininess / denom;
            } else
                exponent = shininess * 128.0f;
            rough = std::pow(2.0f / (exponent + 2.0f), 0.25f);
        }
        pbrParams[2] = bx::clamp(rough, 0.02f, 1.0f);
        pbrParams[3] = std::max(pbrEnvIntensity, 0.0f);
        env = m_envTex;
        bgfx::setUniform(u_envSH, envSH, kEnvSH);
    }
    bgfx::setUniform(u_pbrParams, pbrParams);
    // Machined surface finish: a statement about how the surface shades,
    // so an unlit draw and the depth-only pass leave it off: the depth
    // prepass has to see the same geometry the beauty pass does, and a
    // perturbed normal never moves a fragment anyway. A pattern this
    // build's shader does not know reaches it unchanged and shades as
    // none, the same way the cache passes one through.
    float finishParams[Render::MaxFinishPalette][4] = {};
    uint16_t numFinish = 1;
    const bool finishOn = mat.lighting && pass != PassDepthOnly;
    // Per-face images: a colour, not a shading trick, so unlike the
    // finish they are drawn on an unlit draw too -- but not in the
    // depth-only pass, which writes no colour and must see exactly the
    // geometry the beauty pass does.
    const bool faceTexOn = mat.texturepalette
        && !mat.texturepalette->entries.empty()
        && pass != PassDepthOnly;
    auto setFinish = [](float (&dst)[4], uint8_t pattern, float pitch,
                        float depth, float angle) {
        if (pattern == 0 || pitch <= 0.0f || depth <= 0.0f)
            return;
        dst[0] = float(pattern);
        dst[1] = pitch;
        dst[2] = depth;
        dst[3] = bx::toRad(angle);
    };
    if (finishOn) {
        setFinish(finishParams[0], mat.finish, mat.finishpitch,
                  mat.finishdepth, mat.finishangle);
        // A per-face finish: the palette the vertex stream's index slot
        // names. Uploaded whole (entries the palette does not fill stay
        // zero, so a stale index from an earlier draw shades as none)
        // and only for a draw that actually consumes the stream.
        if (mat.finishpalette && mat.perfacematerial) {
            const auto &entries = mat.finishpalette->entries;
            const size_t num = std::min(entries.size(),
                                        size_t(Render::MaxFinishPalette));
            for (size_t i = 0; i < num; ++i) {
                finishParams[i][0] = 0.0f;
                finishParams[i][1] = 0.0f;
                finishParams[i][2] = 0.0f;
                finishParams[i][3] = 0.0f;
                setFinish(finishParams[i], entries[i].pattern,
                          entries[i].pitch, entries[i].depth,
                          entries[i].angle);
            }
            numFinish = Render::MaxFinishPalette;
        }
    }
    bgfx::setUniform(u_finishParams, finishParams, numFinish);
    // The projection frames that finish is laid out in. Uploaded on the
    // same terms and only when a finish is actually shading: three vec4
    // an entry, entry 0 being the draw's own -- which is what a mesh
    // with no index stream reads, and what an unframed draw leaves at
    // zero so the shader keeps its triplanar projection.
    float frameParams[Render::MaxFramePalette][3][4] = {};
    uint16_t numFrame = 3;
    auto setFrame = [](float (&dst)[3][4], const Render::SurfaceFrame &f) {
        if (f.kind == Render::SurfaceFrame::Unframed)
            return;
        for (int i = 0; i < 3; ++i) {
            dst[0][i] = f.origin[i];
            dst[1][i] = f.axis[i];
            dst[2][i] = f.xdir[i];
        }
        dst[0][3] = float(f.kind);
        dst[1][3] = f.radius;
    };
    // The frames serve the per-face images as well: they are what the
    // images are laid out ON, so a draw that states a tile size needs
    // them uploaded whether or not anything finished it.
    if (finishOn || (faceTexOn && mat.facetexscale > 0.0f)) {
        setFrame(frameParams[0], mat.frame);
        if (mat.framepalette && mat.perfacematerial) {
            const auto &entries = mat.framepalette->entries;
            const size_t num = std::min(entries.size(),
                                        size_t(Render::MaxFramePalette));
            for (size_t i = 0; i < num; ++i)
                setFrame(frameParams[i], entries[i]);
            numFrame = Render::MaxFramePalette * 3;
        }
    }
    bgfx::setUniform(u_frameParams, frameParams, numFrame);
    float matcapParams[4] = {matcapFrame ? 1.0f : 0.0f,
                             float(matcapPreset), matcapTint, 0.0f};
    bgfx::setUniform(u_matcapParams, matcapParams);
    bgfx::setTexture(1, s_texEnv, env);

    // The per-face texture palette (unit 10). Bound on every mesh draw,
    // whether or not this one has a palette: a bgfx uniform holds its
    // value for the rest of the frame, so a draw that left these alone
    // would paint itself with the previous draw's images.
    float faceTexParams[4] = {0.0f, 0.0f, -1.0f, 0.0f};
    bgfx::TextureHandle faceTex = m_whiteTexArray;
    if (faceTexOn) {
        if (GpuTextureArray *arr = getTextureArray(*mat.texturepalette)) {
            faceTex = arr->handle;
            faceTexParams[0] = 1.0f;
            faceTexParams[1] = mat.facetexscale;
            // Which layer this draw's fragments read: the one the
            // producer resolved (a uniformly imaged shape, or a
            // single-face draw), or -1 to read it per vertex out of the
            // material stream -- which only a draw that consumes the
            // stream can do.
            faceTexParams[2] = mat.facetexlayer >= 0
                ? float(mat.facetexlayer)
                : (mat.perfacematerial ? -1.0f : 0.0f);
            faceTexParams[3] =
                float(std::min(mat.texturepalette->entries.size(),
                               std::size_t(Render::MaxFaceTexturePalette)));
        }
    }
    bgfx::setUniform(u_faceTexParams, faceTexParams);
    if (bgfx::isValid(faceTex))
        bgfx::setTexture(10, s_texFace, faceTex);

    // The scene light replaces the headlight for every lit draw of
    // the frame; the VSM lookup runs only on receivers (the white
    // stand-in reads as fully lit), and only while a map was actually
    // rendered -- a lit frame without one keeps the light and drops
    // the shadow.
    float shadowParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lightDir[4] = {0.0f, 0.0f, -1.0f, 0.0f};
    bgfx::TextureHandle shadow = m_whiteTex;
    if (lightFrame) {
        lightDir[0] = lightDirView[0];
        lightDir[1] = lightDirView[1];
        lightDir[2] = lightDirView[2];
        lightDir[3] = 1.0f;
        bgfx::setUniform(u_lightColor, lightColorI);
    }
    if (shadowFrame) {
        bgfx::setUniform(u_shadowMatrix, shadowMtx);
        if ((mat.shadowstyle & 2) && pass != PassDepthOnly) {
            shadowParams[0] = 1.0f;
            // Coin's epsilon (plain VSM adds it to the
            // variance; EVSM scales it by the warped moment)
            shadowParams[1] = shadowEpsilon;
            shadowParams[2] = 0.003f;   // depth bias
            static const bool dbgvis =
                getenv("FC_BGFX_DEBUG_SHADOW_VIS") != nullptr;
            if (dbgvis)
                shadowParams[3] = 1.0f;
            shadow = shadowTex;
        }
    }
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                 shadowSpreadUv, shadowSpreadMode};
    bgfx::setUniform(u_evsm, evsm);
    bgfx::setUniform(u_lightDir, lightDir);
    static const float noSpot[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    bgfx::setUniform(u_lightPos,
                     lightFrame ? lightPosView : noSpot);
    bgfx::setUniform(u_shadowParams, shadowParams);
    // Local effect lights (fire flames + Render_Light bulbs):
    // frame-wide state computed in render() (zeroed w on inactive
    // slots); set on every mesh submit because bgfx uniforms are
    // global per program. Overlays (NaviCube, axis cross, …) are UI
    // chrome and must not flicker with the scene's effect lights,
    // so they get a zeroed array.
    if (overlayView >= 0) {
        static const float noLights[kLocalLights][4] = {};
        bgfx::setUniform(u_localLight, noLights, kLocalLights);
    }
    else {
        bgfx::setUniform(u_localLight, localLightView, kLocalLights);
    }
    // The ordinary Coin lights. Overlays are UI chrome shaded flat
    // (params.y = 0 below), so they never reach the lighting loop and
    // do not need the zeroed-array treatment the effect lights get.
    bgfx::setUniform(u_viewLight, viewLightView, kViewLights);
    bgfx::setUniform(u_viewLightColor, viewLightColorI, kViewLights);
    bgfx::setUniform(u_viewLightAtt, viewLightAtt, kViewLights);
    bgfx::setUniform(u_localLightColor, localLightColorI,
                     kLocalLights);
    // Bulb shadow tiles: matrices + atlas for the shadowed bulb
    // slots (the per-slot color .w flags the tap; a white fallback
    // reads as fully lit).
    if (bgfx::isValid(u_bulbShadowMtx)) {
        bgfx::setUniform(u_bulbShadowMtx, bulbShadowMtx,
                         kBulbShadowTiles);
        bgfx::setUniform(u_bulbShadowConf, bulbShadowConf,
                         kLocalLights - kMediumSlots);
        bgfx::setUniform(u_bulbShadowRot, bulbShadowRotMtx);
        bgfx::setTexture(8, s_texBulbShadow,
                         bgfx::isValid(bulbShadowTex)
                             ? bulbShadowTex : m_whiteTex);
    }
    bgfx::setTexture(3, s_texShadow, shadow);
    if (bgfx::isValid(s_texShadowTint))
        bgfx::setTexture(7, s_texShadowTint,
                         shadowFrame && bgfx::isValid(shadowTintTex)
                             ? shadowTintTex : m_whiteTex);
    // Screen-space AO into the ambient/headlight/IBL terms — only
    // for main-pass opaque draws (the old fullscreen multiply ran
    // on the opaque scene before outlines/transparency; the
    // reflection re-render and the overlays see other pixels).
    bgfx::setTexture(9, s_texAOScreen,
                     aoDraw && bgfx::isValid(aoMeshTex)
                         ? aoMeshTex : m_whiteTex);
}

bool BGFXView::submitInstanced(const Render::DrawCall &draw, const float *data,
                     uint32_t count)
{
    const Render::Material &mat = draw.material;
    GpuMesh *mesh = getMesh(*draw.mesh);
    if (!bgfx::isValid(mesh->geom->vbh)
            || !bgfx::isValid(mesh->geom->tri))
        return false;

    // Texture routing mirrors submit(): a bump/material map rides
    // the textured programs with a white unit-0 stand-in.
    bool bumped = mat.bumpmap && mat.lighting && draw.mesh->texCoords;
    bool mapped = (mat.emissivemap || mat.occlusionmap
                   || mat.metallicroughnessmap)
        && draw.mesh->texCoords;
    bool faceuv = faceTexOnMeshUV(mat) && draw.mesh->texCoords;
    bool textured = (mat.texture && draw.mesh->texCoords)
        || bumped || mapped || faceuv;
    if (textured) {
        mesh->geom->ensureTexCoord(*draw.mesh);
        textured = bgfx::isValid(mesh->geom->texcoord);
        bumped = bumped && textured;
        mapped = mapped && textured;
    }

    bool transparent = mat.transparent
        || (mat.pervertexcolor && draw.mesh->hasTransparency);
    if (transparent && !oitFrame)
        return false;
    bgfx::ProgramHandle prog = transparent
        ? (textured ? m_progMeshInstOitTex : m_progMeshInstOit)
        : (textured ? m_progMeshInstTex : m_progMeshInst);
    if (!bgfx::isValid(prog))
        return false;

    if (bgfx::getAvailInstanceDataBuffer(count, InstanceStride)
            < count)
        return false;
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer(&idb, count, InstanceStride);
    std::memcpy(idb.data, data, size_t(count) * InstanceStride);

    float color[4], emissive[4], specular[4], params[4];
    unpackAuthoredColor(mat.diffuse, color, colorManaged());
    unpackAuthoredColor(mat.emissive, emissive, colorManaged());
    unpackAuthoredColor(mat.specular, specular, colorManaged());
    specular[3] = mat.shininess;
    // Never per-face material here: those draws are excluded from
    // instancing, and the flag must not leak from a previous draw.
    emissive[3] = 0.0f;
    // The fragment stage always reads v_color0 in the instanced
    // path — the vertex stage selects the per-vertex stream or the
    // per-instance color by u_instParams.x.
    params[0] = 1.0f;
    bool shaded = mat.lighting
        || (lightFrame && (mat.shadowstyle & 2));
    // UI overlays (NaviCube faces, etc.) are flat chrome with baked
    // textures: light them uniformly so faces don't darken by angle.
    if (overlayView >= 0)
        shaded = false;
    // Light-source bodies render unshaded at their diffuse color (a
    // glowing bulb face is its own light, not a lit surface).
    if (mat.lightsource)
        shaded = false;
    params[1] = shaded ? 1.0f : 0.0f;
    // GL parity (applyMaterial ~745): transparent draws are lit on
    // both faces and never culled (never on-top here).
    bool twoside = mat.twoside || transparent;
    params[2] = twoside ? 1.0f : 0.0f;
    params[3] = polygonOffsetBias(mat);
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, emissive);
    bgfx::setUniform(u_matSpecular, specular);
    bgfx::setUniform(u_params, params);
    setAmbientUniform(mat);
    setPolygonOffsetUniform(&mat, draw.objectKey);
    float instParams[4] = {mat.pervertexcolor ? 1.0f : 0.0f,
                           0.0f, 0.0f, 0.0f};
    bgfx::setUniform(u_instParams, instParams);
    setTriangleFrameState(mat, PassNormal, mapped, !transparent);
    if (textured)
        bindTextureStage(mat, bumped, mapped);

    // External base layer: an instanced opaque group lays down depth
    // only, like submit()'s PassDepthOnly; a transparent group is
    // refused so its members fall back to submit(), which skips them.
    if (externalBase && transparent)
        return false;
    uint64_t state = BGFX_STATE_MSAA;
    if (!externalBase)
        state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    uint32_t blendRt = 0;
    if (mat.depthtest)
        state |= depthFuncState(mat.depthfunc);
    if (mat.depthwrite && !transparent)
        state |= BGFX_STATE_WRITE_Z;
    if (transparent) {
        // WBOIT accumulation blending (submit()'s oitDraw state).
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE)
            | BGFX_STATE_BLEND_INDEPENDENT;
        blendRt = uint32_t(
            BGFX_STATE_BLEND_FUNC_RT_1(BGFX_STATE_BLEND_ZERO,
                                       BGFX_STATE_BLEND_INV_SRC_COLOR));
    }
    if (mat.culling && !transparent && !twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;

    setMeshVertexBuffers(mesh, *draw.mesh);
    if (textured)
        bgfx::setVertexBuffer(2, mesh->geom->texcoord);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(mesh->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(mesh->geom->tri);
    bgfx::setInstanceDataBuffer(&idb);
    bgfx::setState(state, blendRt);

    static const bool dbgsubmit =
        (getenv("FC_BGFX_DEBUG_SUBMIT") != nullptr);
    if (dbgsubmit)
        fprintf(stderr,
                "bgfx submit instanced cache=%llx n=%u range=%d+%d"
                " state=%llx pvc=%d tex=%d transp=%d\n",
                (unsigned long long)draw.mesh->cacheId, count,
                draw.indexStart, draw.indexCount,
                (unsigned long long)state, mat.pervertexcolor,
                textured, transparent);

    bgfx::submit(vid(transparent ? ViewTransparent : ViewOpaque),
                 prog);
    return true;
}

bool BGFXView::submitShadowCasterInstanced(const Render::DrawCall &draw,
                                 const float *data, uint32_t count)
{
    if (!bgfx::isValid(m_progShadowInst) || !draw.mesh
            || !draw.mesh->triangleIndices)
        return false;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh)
            || !bgfx::isValid(gpu->geom->tri))
        return false;
    if (bgfx::getAvailInstanceDataBuffer(count, InstanceStride)
            < count)
        return false;
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer(&idb, count, InstanceStride);
    std::memcpy(idb.data, data, size_t(count) * InstanceStride);

    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    bgfx::setInstanceDataBuffer(&idb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS);
    float evsm[4] = {shadowWarpFrame, shadowThreshold,
                     0.0f, 0.0f};
    bgfx::setUniform(u_evsm, evsm);
    if (getenv("FC_BGFX_DEBUG_SUBMIT"))
        fprintf(stderr,
                "bgfx submit instanced shadow cache=%llx n=%u\n",
                (unsigned long long)draw.mesh->cacheId, count);
    bgfx::submit(vid(ViewShadow), m_progShadowInst);
    ++drawcount;
    return true;
}

bool BGFXView::submitPrepassInstanced(const Render::DrawCall &draw,
                            const float *data, uint32_t count)
{
    if (!bgfx::isValid(m_progPrepassInst) || !draw.mesh
            || !draw.mesh->triangleIndices)
        return false;
    GpuMesh *gpu = getMesh(*draw.mesh);
    if (!bgfx::isValid(gpu->geom->vbh)
            || !bgfx::isValid(gpu->geom->tri))
        return false;
    if (bgfx::getAvailInstanceDataBuffer(count, InstanceStride)
            < count)
        return false;
    bgfx::InstanceDataBuffer idb;
    bgfx::allocInstanceDataBuffer(&idb, count, InstanceStride);
    std::memcpy(idb.data, data, size_t(count) * InstanceStride);

    const Render::Material &mat = draw.material;
    bgfx::setVertexBuffer(0, gpu->geom->vbh);
    if (draw.indexCount > 0)
        bgfx::setIndexBuffer(gpu->geom->tri, uint32_t(draw.indexStart),
                             uint32_t(draw.indexCount));
    else
        bgfx::setIndexBuffer(gpu->geom->tri);
    bgfx::setInstanceDataBuffer(&idb);
    uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    if (mat.culling && !mat.twoside)
        state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
    bgfx::setState(state);
    if (getenv("FC_BGFX_DEBUG_SUBMIT"))
        fprintf(stderr,
                "bgfx submit instanced prepass cache=%llx n=%u\n",
                (unsigned long long)draw.mesh->cacheId, count);
    bgfx::submit(vid(ViewAOPrepass), m_progPrepassInst);
    ++drawcount;
    return true;
}

void BGFXView::submit(const Render::DrawCall &draw, const float *viewMatrix,
            int pass, bool noseam)
{
    const Render::Material &mat = draw.material;
    if (!draw.mesh || draw.mesh->numVertices == 0)
        return;
    // The per-object per-view display style resolution
    // (docs/CoinRetirement.md 5.8, 5.9) -- see styleAdmits, which the
    // instanced group partition shares.
    if (!styleAdmits(draw))
        return;

    GpuMesh *mesh = getMesh(*draw.mesh);
    // An exhausted handle pool leaves the upload invalid; binding
    // it would be fatal, skipping the draw is not.
    if (!bgfx::isValid(mesh->geom->vbh))
        return;
    // Tessellation draw style: filled triangles carrying
    // SoDrawStyleElement::LINES draw as their edges instead. Only in the
    // ordinary scene pass — the depth prepass wants the solid, and an
    // on-top or selection draw is not what the style is about.
    if (mat.type == Render::Material::Triangle
            && mat.drawstyle == Render::Material::DrawLines
            && pass == PassNormal && !ontop && !mat.ontop && !selPass) {
        submitTessellation(draw, viewMatrix,
                           externalBase ? ViewOnTop : ViewOpaque);
        return;
    }
    // Points draw style: filled triangles carrying SoDrawStyleElement::
    // POINTS -- Mesh's "Points" display mode re-styles its face set --
    // draw as their corner points, which GL gets from glPolygonMode
    // POINT. Unlike Tessellation this returns in EVERY other pass:
    // dots must not occupy the depth prepass as a solid or cast a
    // solid shadow. Not for a scene-wide drawstyle override -- that is
    // Tessellation's discriminator and no display mode overrides to
    // POINTS.
    if (mat.type == Render::Material::Triangle
            && mat.drawstyle == Render::Material::DrawPoints
            && !mat.drawstyleoverride) {
        if (pass == PassNormal && !ontop && !mat.ontop && !selPass)
            submitVertexPoints(draw, viewMatrix, ViewOpaque);
        return;
    }
    // Hidden-line hideSeam: whole-cache line draws switch to the
    // seam-filtered index set (GL: renderLines' noseam argument).
    if (noseam && mat.type == Render::Material::Line)
        mesh->ensureNoSeam(*draw.mesh);
    if (noseam && mat.type == Render::Material::Line
            && getenv("FC_BGFX_DEBUG_SUBMIT"))
        fprintf(stderr, "bgfx noseam cache=%llx num=%d valid=%d\n",
                (unsigned long long)draw.mesh->cacheId,
                draw.mesh->numNoSeamLineIndices,
                bgfx::isValid(mesh->geom->lineNoSeam));
    noseam = noseam && mat.type == Render::Material::Line
        && bgfx::isValid(mesh->geom->lineNoSeam);
    bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
    switch (mat.type) {
    case Render::Material::Triangle: ibh = mesh->geom->tri; break;
    case Render::Material::Line:
        ibh = noseam ? mesh->geom->lineNoSeam : mesh->geom->line;
        break;
    case Render::Material::Point: ibh = mesh->geom->point; break;
    }
    if (!bgfx::isValid(ibh))
        return;

    // Textured triangle fill (unit-0 SoTexture2 fed by the bridge):
    // sampled only when the mesh carries texture coordinates; the
    // depth prepass stays untextured like GL's depthwriteonly path.
    // A bump map routes the draw through the textured programs too
    // (they carry the texcoord stream and the bump sampler); a 1x1
    // white texture stands in at unit 0 when there is no color
    // texture (modulate by white = unchanged).
    bool bumped = mat.type == Render::Material::Triangle
        && mat.bumpmap && mat.lighting && draw.mesh->texCoords
        && pass != PassDepthOnly;
    // Emissive/occlusion/metallic-roughness material maps ride the
    // textured programs too, with the same white unit-0 stand-in as
    // a lone bump map.
    bool mapped = mat.type == Render::Material::Triangle
        && (mat.emissivemap || mat.occlusionmap
            || mat.metallicroughnessmap)
        && draw.mesh->texCoords && pass != PassDepthOnly;
    // Per-face images laid out on the mesh's OWN texture coordinates
    // (a negative tile size -- what a shape that really was UV mapped
    // states, and what the glTF reader writes): that fallback lives
    // inside the shader's TEXTURE variant, so the draw needs the
    // textured programs and the texcoord stream even when nothing else
    // about it is textured. The white unit-0 stand-in a lone bump map
    // already uses modulates by nothing.
    bool faceuv = mat.type == Render::Material::Triangle
        && pass != PassDepthOnly && faceTexOnMeshUV(mat)
        && draw.mesh->texCoords;
    bool textured = (mat.type == Render::Material::Triangle
        && mat.texture && draw.mesh->texCoords
        && pass != PassDepthOnly) || bumped || mapped || faceuv;
    if (textured) {
        mesh->geom->ensureTexCoord(*draw.mesh);
        textured = bgfx::isValid(mesh->geom->texcoord);
        bumped = bumped && textured;
        mapped = mapped && textured;
    }

    // Line stipple: the hidden (dimmed) pass of on-top lines uses the
    // pattern resolved by the bridge (material's own or the selection
    // fallback, GL's RenderPassLinePattern); every other pass uses the
    // material's own pattern.
    uint32_t linepattern = pass == PassLineHidden
        ? mat.hiddenlinepattern : mat.linepattern;
    bool patterned = mat.type == Render::Material::Line
        && (linepattern & 0xffff) != 0xffff;

    // Every line renders as instanced screen-space quads (vs_fc_line*):
    // plain line primitives have no width, no pattern and no coverage
    // control in modern APIs. 1px lines used to take the hardware path,
    // which rasterizes by a different rule than the quad expansion --
    // so an edge changed character, not just size, whenever anything
    // pushed it across 1px (a highlight, outlineThicken), and the two
    // could not be made to agree at any angle. One path, one rule.
    // The instance buffer this needs is built for every mesh with lines
    // at upload time regardless (GpuGeom::upload), so routing the 1px
    // case through it costs no memory and no upload.
    // Without instancing support lines fall back to 1px primitives.
    bool thickline = mat.type == Render::Material::Line
        && m_instancing
        && bgfx::isValid(noseam ? mesh->lineNoSeamInst
                                : mesh->lineInst);
    patterned = patterned && thickline;

    // Points larger than 1px render as instanced screen-space quads
    // as well: BGFX_STATE_POINT_SIZE only exists on the OpenGL
    // backend, the quad path is the portable one.
    bool thickpoint = mat.type == Render::Material::Point
        && mat.pointsize > 1.001f
        && m_instancing && bgfx::isValid(mesh->pointInst);

    bool transparent = mat.transparent
        || (mat.pervertexcolor && draw.mesh->hasTransparency);

    // GL parity (applyMaterial ~745): transparent and on-top draws
    // are lit on both faces, transparent draws are never culled —
    // back faces are visible layers of a transparent solid.
    bool twoside = mat.twoside || transparent || mat.ontop;
    // Overlay feeds keep explicit backface culling even when
    // transparent: overlay widgets (NaviCube) are closed solids whose
    // semi-transparent faces must not double-blend with their own
    // back faces, matching their original GL draw.
    bool culling = mat.culling && (!transparent || overlayView >= 0);

    // A blended particle emitter is routed by its program, not by
    // the material: the seed geometry is opaque as far as the
    // material is concerned, so without this the sprites sit in the
    // opaque bucket and are fogged at the depth of the background
    // behind them (see ViewParticles).
    const bool particleDraw = mat.usershader
        && mat.usershader->stage == "particle";
    const bool blendedParticles = particleDraw
        && userDrawBlends(*mat.usershader);
    uint16_t passView = ontop ? ViewHighlight
        : mat.ontop ? ViewOnTop
        : blendedParticles ? ViewParticles
        : transparent && mat.type == Render::Material::Triangle
            ? ViewTransparent
            : ViewOpaque;
    // Non-on-top selection draws: keep transparent fills in the
    // transparent bucket (GL: transpselections), move the opaque ones
    // into the sequential post-scene view (GL: opaqueselections).
    if (selPass && passView == ViewOpaque)
        passView = ViewSelection;
    // The shaded image is the attached consumer's (externalBase,
    // docs/CyclesIntegration.md sec 5.3). Scene triangles lay down
    // depth only; transparent ones not even that, since the consumer's
    // image already carries their alpha and their depth would hide
    // what shows through them. Everything the host still draws in
    // colour -- lines, points, the non-on-top selections -- moves to
    // the sequential on-top view, which runs after the consumer's
    // blit, with its depth test intact against the depth-only scene.
    const bool externalScene = externalBase && overlayView < 0
        && !reflPass && !ontop && !mat.ontop
        && (passView == ViewOpaque || passView == ViewTransparent
            || passView == ViewSelection);
    if (externalScene) {
        if (mat.type != Render::Material::Triangle || selPass)
            passView = ViewOnTop;
        else if (transparent)
            return;
        else if (pass == PassNormal)
            pass = PassDepthOnly;
    }
    // Ground reflection pass: the same submit path renders into the
    // mirrored-scene view (the caller feeds opaque scene triangles
    // only); the mirror flips the winding, so culling flips too.
    if (reflPass)
        passView = ViewGroundRefl;
    // Overlay feeds render into their own late view (anchor camera,
    // fresh depth) regardless of material routing.
    if (overlayView >= 0)
        passView = uint16_t(overlayView);

    // Glass on screen: scene lines and points render after the
    // refraction instead of into the copy it samples, so a screen-space
    // lens cannot magnify them (see ViewGlassLine). Last of the routing
    // decisions on purpose -- the checks above have already moved every
    // draw that is not in the refraction source (overlays, the ground
    // reflection, the external base layer's on-top run) out of the two
    // views this claims from, so testing those two is the whole test.
    const bool glassLinePass = glassLines && !ontop && !mat.ontop
        && mat.type != Render::Material::Triangle
        && (passView == ViewOpaque || passView == ViewSelection);
    if (glassLinePass)
        passView = ViewGlassLine;

    // GL parity (SoFCRenderer::applyMaterial ~520): on-top draws ignore
    // the depth test, only non-on-top transparent draws drop the depth
    // write. Disabling the depth test also disables depth writes (in GL
    // and every bgfx backend alike), which is why on-top fills need the
    // PassDepthOnly prepass before the line passes.
    bool depthtest = mat.ontop ? false : mat.depthtest;
    // Overlay widgets keep their depth writes even when blended
    // (NaviCube faces GL-parity: glDepthMask stays on), so their
    // depth-tested elements resolve against each other.
    bool depthwrite = (!mat.ontop && transparent && overlayView < 0)
        ? false : mat.depthwrite;
    uint8_t depthfunc = mat.depthfunc;
    // GL quirk (renderHighlight ~2187): a selected face drawn with
    // its outline keeps the depth test at LEQUAL so the outline
    // remains readable where the fill is hidden.
    if (mat.faceoutline && !mat.outlineonly && draw.partIndex >= 0
            && mat.type == Render::Material::Triangle
            && pass == PassNormal) {
        depthtest = true;
        depthfunc = Render::Material::LEqual;
    }
    bool blend = transparent;
    float dimalpha = 1.0f;
    switch (pass) {
    case PassDepthOnly:
        depthtest = true;
        depthwrite = true;
        depthfunc = Render::Material::Less;
        blend = false;
        break;
    case PassLineHidden:
        depthtest = false;
        blend = true;
        dimalpha = mat.hiddenlinealpha;
        // A fill dimmed this way (the external base layer's selection
        // fill) has no hidden-line alpha of its own to follow.
        if (mat.type == Render::Material::Triangle
                && !(dimalpha > 0.0f && dimalpha < 1.0f))
            dimalpha = 0.4f;
        break;
    case PassLineSolid:
        depthtest = true;
        depthfunc = Render::Material::LEqual;
        depthwrite = false;
        blend = true;
        break;
    default:
        break;
    }

    // Analytic line coverage is an alpha ramp, so the AA quad path has
    // to blend: unblended, the feather writes its ramp value as an
    // opaque colour and the line lands a pixel wider and harder-edged
    // than it asked for -- the exact artifact the coverage exists to
    // remove. The 1px primitive fallback has no ramp and keeps its
    // opaque state. This costs the line draws their early-Z, which is
    // affordable because line fragments are a flat colour and a clamp;
    // the feather is bounded at half a pixel per side, so the fragment
    // count grows by at most 2/width.
    if (thickline)
        blend = true;

    // GL disables depth writes together with the depth test; bgfx does
    // not — WRITE_Z with no depth-test bits renders as func ALWAYS with
    // writes on (renderer_gl.cpp, DEPTH_TEST_MASK handling). The on-top
    // machinery depends on the GL rule: a depth-off draw (the on-top
    // fills, the dimmed hidden-line pass) must not lay down its own
    // depth, or it stomps the scene depth and the LEQUAL solid pass
    // that follows passes everywhere against the just-written line
    // depth, painting every hidden edge solid.
    if (!depthtest)
        depthwrite = false;

    // Weighted-blended OIT accumulation: RT0 sums the depth-weighted
    // premultiplied color, RT1 multiplies up the revealage. Draw
    // order becomes irrelevant (commutative blending).
    bool oitDraw = oitFrame && passView == ViewTransparent;

    uint64_t state = BGFX_STATE_MSAA;
    uint32_t blendRt = 0;
    if (pass != PassDepthOnly)
        state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (depthtest)
        state |= depthFuncState(depthfunc);
    if (depthwrite)
        state |= BGFX_STATE_WRITE_Z;
    if (oitDraw) {
        state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE)
            | BGFX_STATE_BLEND_INDEPENDENT;
        blendRt = uint32_t(
            BGFX_STATE_BLEND_FUNC_RT_1(BGFX_STATE_BLEND_ZERO,
                                       BGFX_STATE_BLEND_INV_SRC_COLOR));
    }
    // The "over" blend with its alpha half split off: BLEND_ALPHA
    // applies (SRC_ALPHA, INV_SRC_ALPHA) to the alpha channel as well,
    // which leaves a * a + dst * (1 - a) as the coverage -- wrong
    // wherever the destination is transparent, i.e. an offscreen
    // capture with a transparent background (the material icons).
    else if (blend)
        state |= BGFX_STATE_BLEND_FUNC_SEPARATE(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
            BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
    // A particle emitter's sprites are built in the vertex stage as
    // camera-facing quads in view space, so their winding does not
    // follow the model the way a mesh triangle's does. The mirror
    // pass flips culling to compensate for the mirrored handedness
    // — which for those quads flips them from facing to backfacing
    // and culls the whole emitter, leaving a reflection with the
    // scene in it but no spray. Winding carries no meaning for a
    // billboard; do not cull it in any pass.
    if (culling && !twoside && !particleDraw
                    && mat.type == Render::Material::Triangle)
        state |= (mat.ccw != reflPass) ? BGFX_STATE_CULL_CW
                                       : BGFX_STATE_CULL_CCW;
    if (mat.type == Render::Material::Line) {
        if (!thickline)
            state |= BGFX_STATE_PT_LINES;
    }
    else if (mat.type == Render::Material::Point) {
        if (!thickpoint)
            state |= BGFX_STATE_PT_POINTS
                | BGFX_STATE_POINT_SIZE(
                    uint32_t(qMax(mat.pointsize, 1.0f)));
    }

    float color[4], emissive[4], specular[4], params[4];
    unpackAuthoredColor(mat.diffuse, color, colorManaged());
    unpackAuthoredColor(mat.emissive, emissive, colorManaged());
    unpackAuthoredColor(mat.specular, specular, colorManaged());
    specular[3] = mat.shininess;
    // u_matEmissive.w: per-face material flag — the fragment stage
    // shades emissive/specular/shininess from the v_color1/v_color2
    // stream instead of these scalars. Requires the stream upload to
    // actually be bindable (handle pool exhaustion leaves it invalid).
    // 2 says the stream's two alpha slots carry the PBR factor pair
    // (metallic, roughness) rather than a constant and the shininess.
    emissive[3] = !(mat.perfacematerial && bgfx::isValid(mesh->mats))
        ? 0.0f : mat.perfacepbr ? 2.0f : 1.0f;
    params[0] = mat.pervertexcolor ? 1.0f : 0.0f;
    // u_params.y: mesh program = lighting flag; line program = line
    // width in pixels; point program = point size in pixels (unused
    // by the flat program). Line widths pass through unrounded now
    // that fs_fc_line resolves coverage analytically -- a fractional
    // width is a real weight there, and rounding is what used to make
    // outlineThicken and the highlight widths quantize. Point sizes
    // still round: the sprite has no coverage ramp, so a 1.5px quad
    // would cover its second pixel row only partially and drop it
    // without MSAA.
    // While the scene light is on, unlit receivers (the Shadow draw
    // style's BASE_COLOR ground) light up too — Coin's SoShadowGroup
    // shades and shadows the ground with its own shaders regardless
    // of the light model.
    bool shaded = mat.lighting
        || (lightFrame && (mat.shadowstyle & 2));
    // UI overlays render unlit (see the instanced path above).
    if (overlayView >= 0)
        shaded = false;
    // Light-source bodies render unshaded at their diffuse color (a
    // glowing bulb face is its own light, not a lit surface).
    if (mat.lightsource)
        shaded = false;
    params[1] = mat.type == Render::Material::Line
        ? qMax(1.0f, mat.linewidth)
        : mat.type == Render::Material::Point
            ? qMax(1.0f, std::floor(mat.pointsize + 0.5f))
            : shaded ? 1.0f : 0.0f;
    // u_params.z: mesh program = two-sided lighting; line/point
    // programs = NDC depth bias (the outline passes bias, and the
    // depth-tested passes of highlight lines/points get a tiny
    // toward-viewer pull: GL's native line rasterization gives a
    // thickened selection line exactly the depth of the object's own
    // thinner scene line, so its LEQUAL solid pass wins the tie; the
    // width-dependent subpixel snapping of the quad expansion breaks
    // that tie by ulps, consistently depth-failing the highlight
    // where it coincides with its own object's lines).
    if (mat.type == Render::Material::Triangle)
        params[2] = twoside ? 1.0f : 0.0f;
    else
        params[2] = (mat.highlightline && depthtest)
            ? -2.0f * (2.0f * 16.0f / 16777216.0f) : 0.0f;
    // u_params.w: mesh program = NDC depth bias (polygon offset
    // approximation, no per-pixel slope term); flat program = alpha
    // ceiling used to dim depth-occluded on-top lines.
    if (mat.type == Render::Material::Triangle) {
        params[3] = polygonOffsetBias(mat);
        // The mesh program has no alpha ceiling: a dimmed fill dims
        // through its material alpha.
        if (pass == PassLineHidden)
            color[3] *= dimalpha;
    } else {
        params[3] = dimalpha;
    }
    bgfx::setUniform(u_matColor, color);
    bgfx::setUniform(u_matEmissive, emissive);
    bgfx::setUniform(u_matSpecular, specular);
    bgfx::setUniform(u_params, params);
    setAmbientUniform(mat);
    setPolygonOffsetUniform(&mat, draw.objectKey);

    // PBR branch of the mesh programs: every one of them carries the
    // environment sampler (the branch is uniform-selected), so bind
    // the dummy cube whenever the branch is off for this draw.
    if (mat.type == Render::Material::Triangle)
        setTriangleFrameState(mat, pass, mapped,
                              passView == ViewOpaque
                                  && pass != PassDepthOnly);

    // Clipped draws use the discard shader variants; the unclipped
    // programs contain no discard so the rest of the scene keeps
    // early-Z. The depth prepass clips too (unlike the stateful GL
    // path, which leaves whatever planes happen to be enabled).
    bool clipped = clipActiveFor(mat);
    if (clipped)
        setClipUniforms(mat);

    if (textured)
        bindTextureStage(mat, bumped, mapped);

    if (patterned) {
        // glLineStipple clamps the repeat factor to [1, 256].
        uint32_t factor = linepattern >> 16;
        factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
        float patParams[4] = {float(linepattern & 0xffff),
                              float(factor), 0.0f, 0.0f};
        bgfx::setUniform(u_linePattern, patParams);
    }

    setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix, (float)height,
                     overlayView >= 0 ? overlayAnchor : nullptr,
                     overlayRectHeight);
    if (thickline) {
        // One quad per line segment; a partial (per-edge) index range
        // maps 1:1 onto an instance range (two indices per segment).
        uint32_t startSeg = 0;
        uint32_t numSeg = uint32_t(noseam
            ? draw.mesh->numNoSeamLineIndices
            : draw.mesh->numLineIndices) / 2;
        if (draw.indexCount > 0) {
            startSeg = uint32_t(draw.indexStart) / 2;
            numSeg = uint32_t(draw.indexCount) / 2;
        }
        bgfx::setVertexBuffer(0, m_lineQuadVb);
        bgfx::setIndexBuffer(m_lineQuadIb);
        bgfx::setInstanceDataBuffer(
            noseam ? mesh->lineNoSeamInst : mesh->lineInst,
            startSeg, numSeg);
    }
    else if (thickpoint) {
        // One quad per point; a partial index range maps 1:1 onto an
        // instance range (one index per point).
        uint32_t startPt = 0;
        uint32_t numPt = uint32_t(draw.mesh->numPointIndices);
        if (draw.indexCount > 0) {
            startPt = uint32_t(draw.indexStart);
            numPt = uint32_t(draw.indexCount);
        }
        bgfx::setVertexBuffer(0, m_lineQuadVb);
        bgfx::setIndexBuffer(m_lineQuadIb);
        bgfx::setInstanceDataBuffer(mesh->pointInst, startPt, numPt);
    }
    else {
        setMeshVertexBuffers(mesh, *draw.mesh);
        if (textured)
            bgfx::setVertexBuffer(2, mesh->geom->texcoord);
        if (draw.indexCount > 0)
            bgfx::setIndexBuffer(ibh, uint32_t(draw.indexStart),
                                 uint32_t(draw.indexCount));
        else
            bgfx::setIndexBuffer(ibh);
    }
    bgfx::setState(state, blendRt);

    static const bool dbgsubmit =
        (getenv("FC_BGFX_DEBUG_SUBMIT") != nullptr);
    if (dbgsubmit)
        fprintf(stderr,
                "bgfx submit view=%d pass=%d type=%d part=%d state=%llx"
                " color=%.2f,%.2f,%.2f,%.2f params=%g,%g,%g,%g"
                " clip=%d%s lp=%08x\n",
                passView, pass, mat.type, draw.partIndex,
                (unsigned long long)state,
                color[0], color[1], color[2], color[3],
                params[0], params[1], params[2], params[3],
                mat.numclipplanes, mat.clipconcave ? " concave" : "",
                patterned ? linepattern : 0xffffu);

    uint32_t depth = 0;
    if (passView == ViewTransparent && !oitDraw
             && draw.bboxMin[0] <= draw.bboxMax[0]) {
        // Sort key: distance of the world-space bbox center along the
        // view axis; the view is in DepthDescending mode (far first).
        float cx = (draw.bboxMin[0] + draw.bboxMax[0]) * 0.5f;
        float cy = (draw.bboxMin[1] + draw.bboxMax[1]) * 0.5f;
        float cz = (draw.bboxMin[2] + draw.bboxMax[2]) * 0.5f;
        const float *m = viewMatrix;
        float eyez = m[2]*cx + m[6]*cy + m[10]*cz + m[14];
        depth = bx::floatToBits(bx::max(-eyez, 0.0f));
    }

    bgfx::ProgramHandle prog =
        mat.type == Render::Material::Triangle
            ? (oitDraw
                ? (clipped
                    ? (textured ? m_progMeshOitTexClip
                                : m_progMeshOitClip)
                    : (textured ? m_progMeshOitTex
                                : m_progMeshOit))
                : (clipped
                    ? (textured ? m_progMeshTexClip
                                : m_progMeshClip)
                    : (textured ? m_progMeshTex
                                : m_progMesh)))
            : thickline
                ? (patterned
                    ? (clipped ? m_progLinePatClip
                               : m_progLinePat)
                    : (clipped ? m_progLineClip : m_progLine))
                : thickpoint
                    ? (clipped ? m_progPointClip : m_progPoint)
                    : (clipped ? m_progFlatClip : m_progFlat);

    // User "material"-stage shader (docs/RenderDebug.md §6): replace
    // the mesh fragment stage in the scene beauty passes only — the
    // depth prepass, shadow casters and the highlight/on-top views
    // keep the stock programs, and a WBOIT draw keeps the stock OIT
    // outputs (the user contract is a single color output). While
    // the async compile is pending (or failed) — or, on the viewer
    // tier, until a republished snapshot ships the server-compiled
    // binary — the standard program stands in, never a black
    // object. Parameter uniforms must be recorded with the
    // consuming draw (see submitDebug).
    if (mat.usershader && !mat.usershader->fragmentSource.empty()
            && (mat.usershader->stage == "material"
                || mat.usershader->stage == "particle")
            && mat.type == Render::Material::Triangle
            && pass == PassNormal && !oitDraw
            && (passView == ViewOpaque || passView == ViewTransparent
                || passView == ViewParticles
                || passView == ViewGroundRefl)) {
        bgfx::ProgramHandle uprog = _BGFXLib.getUserProgram(
            *mat.usershader, "vs_fc_mesh");
        if (bgfx::isValid(uprog)) {
            _BGFXLib.pushUserParams(*mat.usershader);
            // A MaterialX document's maps, bound to the samplers its
            // generated code declared (docs/CyclesIntegration.md sec
            // 6.12). Units 13 and up, which is why the state textures
            // below can still claim 10 and 11.
            pushUserImages(*mat.usershader);
            // A generated material pairs with the TEXTURED vertex
            // stage whatever the draw's own texturing says, so the
            // mesh's texture coordinates have to be on stream 2 for
            // it -- an unbound stream reads a constant and every map
            // samples one texel. A draw the texture path already set
            // up has them there (with its own texture matrix); the
            // rest get them here, under the identity.
            if (!textured
                    && mat.usershader->dialect
                        == Render::UserShader::Dialect::MaterialX)
                bindMeshTexCoord(mesh, *draw.mesh);
            // A stateful emitter's vertex stage reads this frame's
            // particle state by vertex texture fetch — the same
            // texels the step passes wrote a few views ago
            // (docs/RenderEngine.md §5.8). With no state (no float
            // render targets, over the slot budget, or the step
            // program still compiling) the samplers stay unbound
            // and the vertex stage runs its stateless path.
            auto pit = particles.find(draw.objectKey);
            if (pit != particles.end()
                    && bgfx::isValid(pit->second.pos[pit->second.cur])) {
                const auto &pst = pit->second;
                bgfx::setTexture(10, s_pstate0, pst.pos[pst.cur]);
                bgfx::setTexture(11, s_pstate1, pst.vel[pst.cur]);
                const float grid[4] = {float(pst.gridW),
                                       float(pst.gridH),
                                       float(pst.count), 0.0f};
                bgfx::setUniform(u_pgrid, grid);
            }
            prog = uprog;
            if (_BGFXLib.userTime[1] != 0.0f
                    && userShaderAnimated(*mat.usershader))
                _BGFXLib.userAnimatedDraw = true;
            applyUserState(*mat.usershader, state, blendRt,
                           passView == ViewGroundRefl);
        }
    }

    bgfx::submit(vid(passView), prog, depth);
    ++drawcount;
}

bool BGFXView::userDrawBlends(const Render::UserShader &shader)
{
    for (const auto &p : shader.params) {
        if (p.name != "fc_state" || p.values.empty())
            continue;
        return int(p.values[0]) != 0;
    }
    return false;
}

void BGFXView::applyUserState(const Render::UserShader &shader,
                           uint64_t state, uint32_t blendRt,
                           bool coverage)
{
    for (const auto &p : shader.params) {
        if (p.name != "fc_state" || p.values.size() < 2)
            continue;
        uint64_t ustate = state;
        if (p.values[1] == 0.0f)
            ustate &= ~BGFX_STATE_WRITE_Z;
        int blend = int(p.values[0]);
        if (blend == 1) {
            ustate &= ~BGFX_STATE_BLEND_MASK;
            ustate |= coverage
                ? BGFX_STATE_BLEND_ALPHA
                : BGFX_STATE_BLEND_FUNC_SEPARATE(
                    BGFX_STATE_BLEND_SRC_ALPHA,
                    BGFX_STATE_BLEND_INV_SRC_ALPHA,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
        }
        else if (blend == 2) {
            ustate &= ~BGFX_STATE_BLEND_MASK;
            ustate |= coverage
                ? BGFX_STATE_BLEND_ADD
                : BGFX_STATE_BLEND_FUNC_SEPARATE(
                    BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE,
                    BGFX_STATE_BLEND_ZERO, BGFX_STATE_BLEND_ONE);
        }
        if (ustate != state)
            bgfx::setState(ustate, blendRt);
        break;
    }
}

uint64_t BGFXView::depthFuncState(uint8_t func)
{
    switch (func) {
    case Render::Material::Never:    return BGFX_STATE_DEPTH_TEST_NEVER;
    case Render::Material::Always:   return BGFX_STATE_DEPTH_TEST_ALWAYS;
    case Render::Material::Less:     return BGFX_STATE_DEPTH_TEST_LESS;
    case Render::Material::Equal:    return BGFX_STATE_DEPTH_TEST_EQUAL;
    case Render::Material::GEqual:   return BGFX_STATE_DEPTH_TEST_GEQUAL;
    case Render::Material::Greater:  return BGFX_STATE_DEPTH_TEST_GREATER;
    case Render::Material::NotEqual: return BGFX_STATE_DEPTH_TEST_NOTEQUAL;
    default:                         return BGFX_STATE_DEPTH_TEST_LEQUAL;
    }
}
