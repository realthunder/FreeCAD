/****************************************************************************
 *   Copyright (c) 2026 Zheng, Lei (realthunder) <realthunder.dev@gmail.com>*
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

#include "PreCompiled.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>

#include <QImage>

#include <App/PropertyFile.h>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/elements/SoShapeHintsElement.h>
#include <Inventor/elements/SoViewVolumeElement.h>
#include <Inventor/elements/SoViewportRegionElement.h>
#include <Inventor/nodes/SoClipPlane.h>
#include <Inventor/nodes/SoBumpMap.h>
#include "SoFCRenderMaterial.h"
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/nodes/SoTexture2.h>

#include "SoAutoZoomTranslation.h"
#include "SoFCRendererBridge.h"
#include "SoFCDisplayModeElement.h"
#include "SoFCRenderer.h"
#include "SoFCVertexCache.h"
#include "../ViewParams.h"
#include "../RenderParams.h"
#include "../View3DInventor.h"

using namespace Gui;

typedef SoFCRenderCache::Material CoinMaterial;
typedef SoFCRenderCache::VertexCacheEntry VertexCacheEntry;

namespace {

// Read-only lookup of a per-view dynamic property override
// (<group>_<name>, e.g. Render_SSAO or Shadow_ShowGround). Unlike
// View3DInventor::getProperty this never creates the property - the
// per-frame config feed must not mutate the view; the Render_* properties
// are materialized when the renderer is selected
// (View3DInventorViewer::setRendererType) and the Shadow_* ones by the
// Shadow draw style.
template<class PropT>
const PropT * viewPropOverride(View3DInventor * view,
                               const char * group,
                               const char * name)
{
    if (!view)
        return nullptr;
    char propname[128];
    snprintf(propname, sizeof(propname)-1, "%s_%s", group, name);
    auto prop = view->getPropertyByName(propname);
    if (!prop || !prop->isDerivedFrom(PropT::getClassTypeId()))
        return nullptr;
    return static_cast<const PropT*>(prop);
}

template<class PropT, class ValueT>
ValueT viewParamOverride(View3DInventor * view,
                         const char * group,
                         const char * name,
                         const ValueT & def)
{
    if (auto prop = viewPropOverride<PropT>(view, group, name))
        return ValueT(prop->getValue());
    return def;
}

// MeshData that keeps its SoFCVertexCache alive, plus a token pinning
// the exposed CPU arrays' current storage generation. The cache alone
// is not enough: the arrays are copy-on-write and shared across caches
// (the variant-cache array dedup), so a later write — e.g. an
// equality-shared index array detaching under sort_triangles — frees
// the storage the raw pointers below were captured from.
struct CacheMeshData : Render::MeshData {
    Gui::CoinPtr<SoFCVertexCache> holder;
    std::shared_ptr<const void> arrayRefs;
    // Compacted index subsets of partial caches (see below); the base
    // struct's index pointers alias these when filled.
    std::vector<int32_t> partialTriangles;
    std::vector<int32_t> partialLines;
};

std::shared_ptr<CacheMeshData>
translateCache(SoFCVertexCache * cache)
{
    auto mesh = std::make_shared<CacheMeshData>();
    mesh->holder = cache;
    mesh->arrayRefs = cache->copyArrayRefs();
    mesh->cacheId = cache->getCacheId();

    mesh->numVertices = cache->getNumVertices();
    mesh->positions = reinterpret_cast<const float *>(cache->getVertexArray());
    mesh->normals = reinterpret_cast<const float *>(cache->getNormalArray());
    mesh->colors = cache->getColorArray();

    static_assert(sizeof(GLint) == sizeof(int32_t), "GLint size mismatch");
    mesh->numTriangleIndices = cache->getNumTriangleIndices();
    if (mesh->numTriangleIndices > 0)
        mesh->triangleIndices =
            reinterpret_cast<const int32_t *>(cache->getTriangleIndices());
    mesh->numLineIndices = cache->getNumLineIndices();
    if (mesh->numLineIndices > 0)
        mesh->lineIndices =
            reinterpret_cast<const int32_t *>(cache->getLineIndices());
    mesh->numPointIndices = cache->getNumPointIndices();
    if (mesh->numPointIndices > 0)
        mesh->pointIndices =
            reinterpret_cast<const int32_t *>(cache->getPointIndices());

    mesh->hasTransparency = cache->hasTransparency();
    mesh->hasOpaqueParts = cache->hasOpaqueParts();

    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr,
                "bridge cache=%llx nv=%d nti=%d tri=%p pos=%p\n",
                (unsigned long long)mesh->cacheId, mesh->numVertices,
                mesh->numTriangleIndices,
                (const void *)mesh->triangleIndices,
                (const void *)mesh->positions);

    mesh->texCoords =
        reinterpret_cast<const float *>(cache->getTexCoordArray());

    // Partial subset caches (e.g. the single-edge copy of a partial
    // selection) keep the parent's FULL index array and record the
    // restriction as a part list — the GL renderer draws it part by
    // part (SoFCVertexArrayIndexer::partialindices). Compact those
    // parts into a real index subset so backends (which consume the
    // arrays verbatim) draw only the selected parts. Point subsets
    // rebuild their index array at copy time and never get here.
    const auto &triParts = cache->getPartialTriangleParts();
    if (!triParts.empty() && mesh->triangleIndices) {
        for (int p : triParts) {
            int start = 0, count = 0;
            if (cache->getTrianglePartRange(p, start, count) && count > 0)
                mesh->partialTriangles.insert(mesh->partialTriangles.end(),
                        mesh->triangleIndices + start,
                        mesh->triangleIndices + start + count);
        }
        mesh->triangleIndices = mesh->partialTriangles.data();
        mesh->numTriangleIndices = int(mesh->partialTriangles.size());
    }
    const auto &lineParts = cache->getPartialLineParts();
    if (!lineParts.empty() && mesh->lineIndices) {
        for (int p : lineParts) {
            int start = 0, count = 0;
            if (cache->getLinePartRange(p, start, count) && count > 0)
                mesh->partialLines.insert(mesh->partialLines.end(),
                        mesh->lineIndices + start,
                        mesh->lineIndices + start + count);
        }
        mesh->lineIndices = mesh->partialLines.data();
        mesh->numLineIndices = int(mesh->partialLines.size());
    }
    return mesh;
}

// Shares one TextureImage among all materials referring to the same
// texture node within a translate() call; the pixel copy dedups across
// feeds through the backend's textureId keying (Coin node ids are unique
// per node content revision).
typedef std::unordered_map<const SoNode *,
                           std::shared_ptr<const Render::TextureImage>>
    TextureImageMap;

std::shared_ptr<const Render::TextureImage>
translateTexture(const SoFCRenderCache::TextureInfo & info,
                 TextureImageMap & texmap)
{
    if (!info.texture
            || !info.texture->isOfType(SoTexture2::getClassTypeId()))
        return nullptr;

    auto & res = texmap[info.texture.get()];
    if (res)
        return res;

    auto node = static_cast<const SoTexture2 *>(info.texture.get());
    SbVec2s size;
    int nc = 0;
    const unsigned char * pixels = node->image.getValue(size, nc);
    if (!pixels || size[0] <= 0 || size[1] <= 0 || nc <= 0 || nc > 4)
        return nullptr;

    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = node->getNodeId();
    tex->width = size[0];
    tex->height = size[1];
    tex->numComponents = nc;
    tex->pixels.assign(pixels,
                       pixels + size_t(size[0]) * size[1] * nc);
    tex->wrapS = node->wrapS.getValue() == SoTexture2::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    tex->wrapT = node->wrapT.getValue() == SoTexture2::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    switch (node->model.getValue()) {
    case SoTexture2::DECAL:
        tex->model = Render::TextureImage::Decal; break;
    case SoTexture2::BLEND:
        tex->model = Render::TextureImage::Blend; break;
    case SoTexture2::REPLACE:
        tex->model = Render::TextureImage::Replace; break;
    default:
        tex->model = Render::TextureImage::Modulate; break;
    }
    tex->blendColor = node->blendColor.getValue().getPackedValue(0.0f);
    res = tex;
    return res;
}

std::shared_ptr<const Render::TextureImage>
translateBumpMap(const SoFCRenderCache::TextureInfo & info,
                 TextureImageMap & texmap)
{
    if (!info.texture
            || !info.texture->isOfType(SoBumpMap::getClassTypeId()))
        return nullptr;

    auto & res = texmap[info.texture.get()];
    if (res)
        return res;

    auto node = static_cast<const SoBumpMap *>(info.texture.get());
    SbVec2s size;
    int nc = 0;
    const unsigned char * pixels = node->image.getValue(size, nc);
    if (!pixels || size[0] <= 0 || size[1] <= 0 || nc <= 0 || nc > 4)
        return nullptr;

    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = node->getNodeId();
    tex->width = size[0];
    tex->height = size[1];
    tex->numComponents = nc;
    tex->pixels.assign(pixels,
                       pixels + size_t(size[0]) * size[1] * nc);
    tex->wrapS = node->wrapS.getValue() == SoBumpMap::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    tex->wrapT = node->wrapT.getValue() == SoBumpMap::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    res = tex;
    return res;
}

std::shared_ptr<const Render::TextureImage>
translateRenderTexture(const SoFCRenderCache::TextureInfo & info,
                       TextureImageMap & texmap)
{
    if (!info.texture
            || !info.texture->isOfType(
                    Gui::SoFCRenderTexture::getClassTypeId()))
        return nullptr;

    auto & res = texmap[info.texture.get()];
    if (res)
        return res;

    auto node = static_cast<const Gui::SoFCRenderTexture *>(
            info.texture.get());
    SbVec2s size;
    int nc = 0;
    const unsigned char * pixels = node->image.getValue(size, nc);
    if (!pixels || size[0] <= 0 || size[1] <= 0 || nc <= 0 || nc > 4)
        return nullptr;

    auto tex = std::make_shared<Render::TextureImage>();
    tex->textureId = node->getNodeId();
    tex->width = size[0];
    tex->height = size[1];
    tex->numComponents = nc;
    tex->pixels.assign(pixels,
                       pixels + size_t(size[0]) * size[1] * nc);
    tex->wrapS = node->wrapS.getValue() == Gui::SoFCRenderTexture::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    tex->wrapT = node->wrapT.getValue() == Gui::SoFCRenderTexture::CLAMP
        ? Render::TextureImage::Clamp : Render::TextureImage::Repeat;
    res = tex;
    return res;
}

// Whether a line/point material of this feed renders with GL's
// RenderPassHighlight (selection thickening). Mirrors the bucket routing
// in SoFCRendererP::updateSelection: partial-element selections and full
// (non-partialhighlight) whole-object selections thicken; the mixed
// "partial highlight" whole-object lines (selsontop) do not. Non-on-top
// selections (id < 0) and the preselection highlight always thicken.
bool
useHighlightPass(const CoinMaterial & m, int selId, bool highlight)
{
    if (m.type == CoinMaterial::Triangle)
        return false;
    if (highlight || selId < 0)
        return true;
    if (selId & SoFCRenderer::SelIdPartial)
        return true;
    // The implicit whole-object companion of a partial selection
    // thickens too. For flattened objects this is invisible — the
    // partial id's own (thickened) whole-on-top copies win the
    // backend's dedup — but a TShape-instanced partial selection
    // scopes its id to one instance wrapper, so the companion draws
    // are the only whole-on-top lines and must match the flattened
    // width. (Deviation from the GL renderer's selsontop bucket, which
    // leaves surviving implicit lines thin.)
    if (selId & SoFCRenderer::SelIdImplicit)
        return true;
    return (selId & SoFCRenderer::SelIdFull) && !m.partialhighlight;
}

Render::Material
translateMaterial(const CoinMaterial & m, int selId, bool highlight,
                  TextureImageMap & texmap)
{
    Render::Material res;

    switch (m.type) {
    case CoinMaterial::Line:
        res.type = Render::Material::Line;
        break;
    case CoinMaterial::Point:
        res.type = Render::Material::Point;
        break;
    default:
        res.type = Render::Material::Triangle;
        break;
    }

    res.diffuse = m.diffuse;
    res.emissive = m.emissive;
    res.specular = m.specular;
    res.ambient = m.ambient;
    res.linecolor = m.linecolor;
    res.shininess = m.shininess;
    res.linewidth = m.linewidth;
    res.pointsize = m.pointsize;

    // Hidden-line draw style material: the backend outlines whole-cache
    // triangle draws and applies the per-frame HiddenLineConfig rules.
    res.outline = m.outline;

    // Solid shape hint, one half of the section-cap eligibility test
    // (GL: renderSection checks shapetype and cache->hasSolid()).
    res.solidshape = m.shapetype == SoShapeHintsElement::SOLID;

    // Selection line/point thickening (GL: applyMaterial ~552 under
    // RenderPassHighlight). Applied at translate time so backends see the
    // effective width. Deviation from GL: the dimmed (hidden) pass of
    // whole-on-top preselect lines thickens too, where GL leaves it thin.
    if (useHighlightPass(m, selId, highlight)) {
        float scale = float(ViewParams::getSelectionLineThicken());
        if (scale < 1.0f)
            scale = 1.0f;
        float w = res.linewidth * scale;
        if (ViewParams::getSelectionLineMaxWidth() > 1.0)
            w = std::min<float>(w, std::max<float>(
                    res.linewidth,
                    float(ViewParams::getSelectionLineMaxWidth())));
        res.linewidth = w;

        float pscale = float(ViewParams::getSelectionPointScale());
        if (pscale < 1.0f)
            pscale = scale;
        w = res.pointsize * pscale;
        if (ViewParams::getSelectionPointMaxSize() > 1.0)
            w = std::min<float>(w, std::max<float>(
                    res.pointsize,
                    float(ViewParams::getSelectionPointMaxSize())));
        res.pointsize = w;
    }

    res.pervertexcolor = m.pervertexcolor;
    res.lighting = m.lightmodel != SoLazyElement::BASE_COLOR;
    res.twoside = m.twoside;
    res.culling = m.culling;
    res.ccw = m.vertexordering != SoLazyElement::CW;

    res.depthtest = m.depthtest;
    res.depthwrite = m.depthwrite;
    switch (m.depthfunc) {
    case SoDepthBufferElement::NEVER:
        res.depthfunc = Render::Material::Never; break;
    case SoDepthBufferElement::ALWAYS:
        res.depthfunc = Render::Material::Always; break;
    case SoDepthBufferElement::LESS:
        res.depthfunc = Render::Material::Less; break;
    case SoDepthBufferElement::EQUAL:
        res.depthfunc = Render::Material::Equal; break;
    case SoDepthBufferElement::GEQUAL:
        res.depthfunc = Render::Material::GEqual; break;
    case SoDepthBufferElement::GREATER:
        res.depthfunc = Render::Material::Greater; break;
    case SoDepthBufferElement::NOTEQUAL:
        res.depthfunc = Render::Material::NotEqual; break;
    default:
        res.depthfunc = Render::Material::LEqual; break;
    }

    // Same uniform-transparency rule as SoFCRenderer::setScene().
    res.transparent = m.transptexture
        || (!m.pervertexcolor && (m.diffuse & 0xff) != 0xff);
    res.ontop = m.isOnTop();

    // GL polygon offset only affects filled polygons (the LINES/POINTS
    // styles matter only with glPolygonMode, which the renderer never uses).
    res.polygonoffset =
        (m.polygonoffsetstyle & SoPolygonOffsetElement::FILLED)
        && (m.polygonoffsetfactor != 0.0f || m.polygonoffsetunits != 0.0f);
    res.polygonoffsetfactor = m.polygonoffsetfactor;
    res.polygonoffsetunits = m.polygonoffsetunits;

    // Depth-occluded parts of on-top lines/points are dimmed to this alpha
    // (SoFCRenderer's RenderPassLinePattern pass).
    if (res.ontop && res.type != Render::Material::Triangle)
        res.hiddenlinealpha = float(ViewParams::getTransparencyOnTop());

    // Selected/preselected face outline (GL: renderOutline under the
    // RenderPassSelectionOutline pass, issued for partial triangle
    // draws of on-top selections and of the preselection highlight).
    // The width rules mirror renderOutline ~1458: selection thickening
    // capped by SelectionLineMaxWidth, then max of 1.5x that and
    // linewidth * OutlineThicken.
    if (res.type == Render::Material::Triangle && (highlight || selId > 0)) {
        // The width is computed regardless of the face-outline params:
        // the hidden-line style's whole-object highlight outline uses it
        // even with face outlines disabled (GL: renderOutline ~1458).
        float lw = res.linewidth;
        float scale = float(ViewParams::getSelectionLineThicken());
        if (scale < 1.0f)
            scale = 1.0f;
        float w = lw * scale;
        if (ViewParams::getSelectionLineMaxWidth() > 1.0)
            w = std::min<float>(w, std::max<float>(lw,
                    float(ViewParams::getSelectionLineMaxWidth())));
        res.outlinewidth = std::max(w * 1.5f,
            lw * float(ViewParams::getOutlineThicken()));
        bool show = highlight
            ? ViewParams::getShowPreSelectedFaceOutline()
            : ViewParams::getShowSelectedFaceOutline();
        if (show) {
            res.faceoutline = true;
            res.outlineonly = highlight
                ? ViewParams::getNoPreSelFaceHighlightWithOutline()
                : ViewParams::getNoSelFaceHighlightWithOutline();
        }
    }

    // Line stipple (glLineStipple encoding, factor << 16 | pattern). The
    // dimmed pass of on-top lines falls back to the user-configurable
    // selection pattern when the material has none (GL: applyMaterial
    // ~539 under RenderPassLinePattern).
    if (res.type == Render::Material::Line) {
        res.linepattern = m.linepattern;
        res.hiddenlinepattern = m.linepattern;
        if (res.ontop && !m.hasLinePattern()) {
            uint32_t sellinepattern =
                uint32_t(ViewParams::getSelectionLinePattern()) & 0xffff;
            if (sellinepattern) {
                if (ViewParams::getSelectionLinePatternScale() > 1)
                    sellinepattern |=
                        uint32_t(ViewParams::getSelectionLinePatternScale())
                            << 16;
                res.hiddenlinepattern = sellinepattern;
            }
        }
    }

    // Texture of triangle draws: unit 0 only (GL applies further units
    // on top of it, a known deviation). The texture matrix already
    // carries the merged SoTexture2Transform/SoTextureMatrixTransform
    // state (SoFCRenderCache::addTexture).
    if (res.type == Render::Material::Triangle && m.textures.getNum()) {
        if (const auto * info = m.textures.get(0)) {
            res.texture = translateTexture(*info, texmap);
            if (res.texture && !info->identity) {
                static_assert(sizeof(res.texmatrix) == sizeof(SbMat),
                              "matrix size mismatch");
                res.texidentity = false;
                std::memcpy(res.texmatrix, info->matrix.getValue(),
                            sizeof(res.texmatrix));
            }
        }
    }

    // Shadow participation flags for the backend's shadow caster and
    // receiver routing (only consulted while a scene light is fed).
    if (res.type == Render::Material::Triangle)
        res.shadowstyle = uint8_t(m.shadowstyle);

    // Per-object PBR parameters (SoFCRenderMaterial capture; < 0 = unset).
    if (res.type == Render::Material::Triangle) {
        res.metallic = m.metallic;
        res.roughness = m.roughness;
        res.water = m.water;
        res.waterdensity = m.waterdensity;
        res.glass = m.glass;
        res.glassior = m.glassior;
        res.glassdensity = m.glassdensity;
        res.glassroughness = m.glassroughness;
        res.cloud = m.cloud;
        res.clouddensity = m.clouddensity;
        res.clouddetail = m.clouddetail;
        res.cloudspeed = m.cloudspeed;
        res.fire = m.fire;
        res.fireintensity = m.fireintensity;
        res.firedetail = m.firedetail;
        res.firespeed = m.firespeed;
    }

    // Bump map of triangle draws, unit 0 only like textures (the GL
    // renderer never draws these; SoBumpMap only acts during Coin GL
    // shape rendering, which the cached pipeline bypasses).
    if (res.type == Render::Material::Triangle && m.bumpmaps.getNum()) {
        if (const auto * info = m.bumpmaps.get(0))
            res.bumpmap = translateBumpMap(*info, texmap);
    }

    // Emissive/occlusion/metallic-roughness material maps
    // (SoFCRenderTexture capture), unit 0 only like the maps above.
    if (res.type == Render::Material::Triangle && m.emissivemaps.getNum()) {
        if (const auto * info = m.emissivemaps.get(0))
            res.emissivemap = translateRenderTexture(*info, texmap);
    }
    if (res.type == Render::Material::Triangle && m.occlusionmaps.getNum()) {
        if (const auto * info = m.occlusionmaps.get(0))
            res.occlusionmap = translateRenderTexture(*info, texmap);
    }
    if (res.type == Render::Material::Triangle
            && m.metallicroughnessmaps.getNum()) {
        if (const auto * info = m.metallicroughnessmaps.get(0))
            res.metallicroughnessmap = translateRenderTexture(*info, texmap);
    }

    // Autozoom transforms: mirror the material's node list; the backend
    // replays them per frame (GL: setupMatrix runs the nodes' GLRender).
    if (m.autozoom.getNum()) {
        res.autozoom.reserve(m.autozoom.getNum());
        for (const auto & info : m.autozoom.getData()) {
            res.autozoom.emplace_back();
            Render::Material::AutoZoomEntry & entry = res.autozoom.back();
            auto node = info.cast<SoAutoZoomTranslation>();
            entry.scaleFactor = node->scaleFactor.getValue();
            entry.billboard = node->billboard.getValue();
            entry.datumFlip = node->datumFlip.getValue();
            const SbVec3f & nrm = node->flipNormal.getValue();
            entry.normal[0] = nrm[0];
            entry.normal[1] = nrm[1];
            entry.normal[2] = nrm[2];
            entry.identity = info.identity;
            entry.resetmatrix = info.resetmatrix;
            if (!info.identity) {
                static_assert(sizeof(entry.matrix) == sizeof(SbMat),
                              "matrix size mismatch");
                std::memcpy(entry.matrix, info.matrix.getValue(),
                            sizeof(entry.matrix));
            }
        }
    }

    // Clip planes (sections), as world-space plane equations. Same on-top
    // exception as SoFCRenderer::applyMaterial: on-top draws are not
    // sectioned when NoSectionOnTop is set (default) or in concave mode.
    if (m.clippers.getNum()) {
        bool concave =
            ViewParams::getSectionConcave() && m.clippers.getNum() > 1;
        if (!((ViewParams::getNoSectionOnTop() || concave) && res.ontop)) {
            for (const auto & info : m.clippers.getData()) {
                const SoClipPlane * clipper = info.cast<SoClipPlane>();
                if (!clipper->on.getValue() || clipper->on.isIgnored())
                    continue;
                SbPlane plane = clipper->plane.getValue();
                if (!info.identity)
                    plane.transform(info.matrix);
                // SbPlane: kept points satisfy dot(p, normal) >= distance.
                float * eq = res.clipplanes[res.numclipplanes];
                plane.getNormal().getValue(eq[0], eq[1], eq[2]);
                eq[3] = -plane.getDistanceFromOrigin();
                if (++res.numclipplanes == Render::Material::MaxClipPlanes)
                    break;
            }
            res.clipconcave = concave && res.numclipplanes > 1;
        }
    }
    return res;
}

} // anonymous namespace

Render::DrawCallList
RendererBridge::translate(const SoFCRenderCache::VertexCacheMap & vcachemap,
                          int selId, bool highlight, bool sequentialOrder)
{
    Render::DrawCallList res;

    // Share one MeshData among all entries referring to the same cache,
    // and one TextureImage among all materials with the same texture node.
    std::unordered_map<SoFCVertexCache *,
                       std::shared_ptr<CacheMeshData>> meshes;
    TextureImageMap textures;

    for (const auto & v : vcachemap) {
        const CoinMaterial & material = v.first;
        if (v.second.empty())
            continue;
        if (material.drawstyle == SoDrawStyleElement::INVISIBLE)
            continue;

        Render::Material rmat =
            translateMaterial(material, selId, highlight, textures);

        for (const VertexCacheEntry & ventry : v.second) {
            if (!ventry.cache)
                continue;

            // Resolve a partial draw (single face/edge/point) into an index
            // range of the buffer selected by the material type.
            int indexStart = 0;
            int indexCount = 0;
            if (ventry.partidx >= 0) {
                SbBool ok = FALSE;
                switch (rmat.type) {
                case Render::Material::Line:
                    ok = ventry.cache->getLinePartRange(
                            ventry.partidx, indexStart, indexCount);
                    break;
                case Render::Material::Point:
                    ok = ventry.cache->getPointPartRange(
                            ventry.partidx, indexStart, indexCount);
                    break;
                default:
                    ok = ventry.cache->getTrianglePartRange(
                            ventry.partidx, indexStart, indexCount);
                    break;
                }
                if (!ok)
                    continue;
            }

            auto & mesh = meshes[ventry.cache];
            if (!mesh)
                mesh = translateCache(ventry.cache);

            // Hidden-line extras, filled on demand: the seam-filtered line
            // index set (hideSeam), and per-face-part triangle ranges for
            // outlining geometry part by part (GL: renderOutline switches
            // to getNumFaceParts() under clip planes or perFaceOutline,
            // and to getNonFlatParts() for perFaceOutline+sceneOutline).
            // Per-face triangle ranges for browser-side per-face
            // preselection: the WASM viewer maps a hovered triangle to
            // its face part. Filled for every whole-object triangle mesh
            // (not just outlined ones) so shaded-mode hover highlights the
            // face under the cursor, matching the desktop.
            if (rmat.type == Render::Material::Triangle && ventry.partidx < 0
                    && mesh->triangleParts.empty()) {
                int numparts = ventry.cache->getNumFaceParts();
                mesh->triangleParts.reserve(numparts);
                for (int i = 0; i < numparts; ++i) {
                    int start = 0, count = 0;
                    if (ventry.cache->getTrianglePartRange(i, start, count)
                            && count > 0)
                        mesh->triangleParts.emplace_back(start, count);
                }
            }

            if (rmat.outline && ventry.partidx < 0) {
                if (rmat.type == Render::Material::Line
                        && !mesh->noSeamLineIndices
                        && ventry.cache->getNumNoSeamLineIndices() > 0) {
                    mesh->numNoSeamLineIndices =
                        ventry.cache->getNumNoSeamLineIndices();
                    mesh->noSeamLineIndices = reinterpret_cast<const int32_t *>(
                            ventry.cache->getNoSeamLineIndices());
                }
                if (rmat.type == Render::Material::Triangle
                        && mesh->nonFlatParts.empty()) {
                    int numparts = ventry.cache->getNumNonFlatParts();
                    const int * parts = ventry.cache->getNonFlatParts();
                    mesh->nonFlatParts.reserve(numparts);
                    for (int i = 0; i < numparts; ++i) {
                        int start = 0, count = 0;
                        if (ventry.cache->getTrianglePartRange(
                                    parts[i], start, count) && count > 0)
                            mesh->nonFlatParts.emplace_back(start, count);
                    }
                }
            }

            // Section-cap solids, filled on demand for clipped whole
            // triangle draws: which triangle ranges the stencil cap pass
            // marks (GL: renderSolids from _renderSection).
            if (rmat.type == Render::Material::Triangle
                    && rmat.numclipplanes > 0 && ventry.partidx < 0
                    && mesh->hasSolid == 0) {
                mesh->hasSolid = ventry.cache->hasSolid();
                if (mesh->hasSolid == 1 && mesh->solidParts.empty()) {
                    int numparts = ventry.cache->getNumSolidParts();
                    mesh->solidParts.reserve(numparts);
                    for (int i = 0; i < numparts; ++i) {
                        int start = 0, count = 0;
                        if (ventry.cache->getSolidPartRange(i, start, count)
                                && count > 0)
                            mesh->solidParts.emplace_back(start, count);
                    }
                }
            }

            res.emplace_back();
            Render::DrawCall & draw = res.back();
            draw.material = rmat;
            draw.mesh = mesh;
            draw.objectKey = ventry.key ? ventry.key->hash() : 0;
            draw.wholeObject = ventry.partidx < 0
                && ventry.cache == ventry.cache->getWholeCache();
            draw.partIndex = ventry.partidx;
            draw.indexStart = indexStart;
            draw.indexCount = indexCount;
            draw.identity = ventry.identity;
            if (!ventry.identity) {
                static_assert(sizeof(draw.model) == sizeof(SbMat),
                              "matrix size mismatch");
                std::memcpy(draw.model, ventry.matrix.getValue(),
                            sizeof(draw.model));
            }

            SbBox3f bbox;
            ventry.cache->getBoundingBox(
                    ventry.identity ? nullptr : &ventry.matrix, bbox);
            if (!bbox.isEmpty()) {
                bbox.getMin().getValue(
                        draw.bboxMin[0], draw.bboxMin[1], draw.bboxMin[2]);
                bbox.getMax().getValue(
                        draw.bboxMax[0], draw.bboxMax[1], draw.bboxMax[2]);
            }
            else {
                draw.bboxMin[0] = draw.bboxMin[1] = draw.bboxMin[2] = 1.0f;
                draw.bboxMax[0] = draw.bboxMax[1] = draw.bboxMax[2] = -1.0f;
            }
        }
    }

    // Overlay feeds: restore the scene-graph traversal order (vertex
    // caches are created in traversal order and their ids ascend), so
    // the backend's Sequential overlay view blends like the original GL
    // drawing sequence.
    if (sequentialOrder)
        std::stable_sort(res.begin(), res.end(),
            [](const Render::DrawCall &a, const Render::DrawCall &b) {
                uint64_t ka = a.mesh ? a.mesh->cacheId : 0;
                uint64_t kb = b.mesh ? b.mesh->cacheId : 0;
                return ka < kb;
            });
    return res;
}

Render::HiddenLineConfig
RendererBridge::translateHiddenLineConfig(SoState * state)
{
    Render::HiddenLineConfig res;
    SoFCDisplayModeElement::HiddenLineConfig config;
    if (!SoFCDisplayModeElement::showHiddenLines(state, &config))
        return res;
    res.show = true;
    res.hideFace = config.hideFace;
    res.hideSeam = config.hideSeam;
    res.hideVertex = config.hideVertex;
    res.perFaceOutline = config.perFaceOutline;
    res.sceneOutline = config.sceneOutline;
    res.outlineWidth = config.outlineWidth;
    res.outlineThicken = float(ViewParams::getOutlineThicken());
    if (const SbColor * color = SoFCDisplayModeElement::getLineColor(state))
        res.lineColor = color->getPackedValue(0.0f);
    else
        res.lineColor = uint32_t(ViewParams::getHiddenLineColor());
    return res;
}

Render::SectionConfig
RendererBridge::translateSectionConfig()
{
    Render::SectionConfig res;
    res.fill = ViewParams::getSectionFill();
    res.fillInvert = ViewParams::getSectionFillInvert();
    res.fillGroup = ViewParams::getSectionFillGroup();
    res.concave = ViewParams::getSectionConcave();
    res.hatchEnable = ViewParams::getSectionHatchTextureEnable();
    res.hatchScale = float(ViewParams::getSectionHatchTextureScale());
    return res;
}

Render::AOConfig
RendererBridge::translateAOConfig(View3DInventor * view)
{
    Render::AOConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "SSAO", RenderParams::getSSAO());
    res.radius = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "SSAORadius", RenderParams::getSSAORadius()));
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "SSAOIntensity", RenderParams::getSSAOIntensity()));
    return res;
}

Render::LightConfig
RendererBridge::translateLightConfig(SoState * state, View3DInventor * view)
{
    // The Shadow draw style's light lives above the render-cache
    // traversal root, so it is resolved from the state's accumulated
    // light element instead of the material feed. The viewer headlight
    // is filtered by type: only Coin's shadow directional light and
    // spot lights qualify.
    Render::LightConfig res;
    const SoNodeList & lights = SoLightElement::getLights(state);
    for (int i = 0; i < lights.getLength(); ++i) {
        SoNode * node = lights[i];
        if (!node || !node->isOfType(SoLight::getClassTypeId()))
            continue;
        auto light = static_cast<const SoLight *>(node);
        if (!light->on.getValue())
            continue;
        SbVec3f dir(0.0f, 0.0f, -1.0f);
        SbVec3f pos(0.0f, 0.0f, 0.0f);
        if (node->isOfType(SoShadowDirectionalLight::getClassTypeId())) {
            res.spot = false;
            dir = static_cast<const SoDirectionalLight *>(light)
                ->direction.getValue();
        } else if (node->isOfType(SoSpotLight::getClassTypeId())) {
            auto spot = static_cast<const SoSpotLight *>(light);
            res.spot = true;
            dir = spot->direction.getValue();
            pos = spot->location.getValue();
            res.cutOffAngle = spot->cutOffAngle.getValue();
            res.dropOffRate = spot->dropOffRate.getValue();
        } else {
            continue;
        }
        // SoLightElement matrices map to *view reference* coordinates
        // (model * viewing); the backend expects world space (it
        // re-applies its own per-frame view matrix), so multiply the
        // inverse viewing matrix back in.
        SbMatrix mat = SoLightElement::getMatrix(state, i);
        mat.multRight(SoViewingMatrixElement::get(state).inverse());
        mat.multDirMatrix(dir, dir);
        mat.multVecMatrix(pos, pos);
        dir.normalize();
        res.direction[0] = dir[0];
        res.direction[1] = dir[1];
        res.direction[2] = dir[2];
        res.position[0] = pos[0];
        res.position[1] = pos[1];
        res.position[2] = pos[2];
        res.color = light->color.getValue().getPackedValue(0.0f);
        res.intensity = light->intensity.getValue();
        res.valid = true;
        break;
    }
    if (res.valid) {
        // Render_Shadow (Render group) is a convenience toggle to drop the
        // shadow map while keeping the scene lit; the Shadow draw style
        // still provides the light.
        res.shadow = viewParamOverride<App::PropertyBool>(
                view, "Render", "Shadow", RenderParams::getShadow());
        // Shadow border smoothing scales the backend's variance-map
        // blur; the Shadow draw style materializes Shadow_SmoothBorder
        // (0..100) with the ViewParams default.
        res.smoothBorder = float(viewParamOverride<App::PropertyInteger>(
                view, "Shadow", "SmoothBorder",
                ViewParams::getShadowSmoothBorder()));
        // Coin VsmLookup parameters of the plain-VSM (SmoothBorder 0)
        // path; the Shadow draw style materializes Shadow_Epsilon /
        // Shadow_Threshold like the rest.
        res.epsilon = float(viewParamOverride<App::PropertyFloat>(
                view, "Shadow", "Epsilon",
                ViewParams::getShadowEpsilon()));
        // A zero (or too-small) epsilon collapses the VSM variance floor,
        // so the Chebyshev bound flips per pixel on the self-shadowed
        // terminator (dark-spot acne). Enforce the same configurable
        // minimum the Shadow_Epsilon property clamps to (ViewParams
        // ShadowEpsilonMinimum, see docs/ShaderDesign.md) since a value
        // stored before that constraint can still reach the backend.
        float epsMin = float(ViewParams::getShadowEpsilonMinimum());
        if (res.epsilon < epsMin)
            res.epsilon = epsMin;
        res.threshold = float(viewParamOverride<App::PropertyFloat>(
                view, "Shadow", "Threshold",
                ViewParams::getShadowThreshold()));
        // Coin's N-tap receiver spread kernel (the Shadow draw style's
        // SpreadSize/SpreadSampleSize properties, packed into the Coin
        // smoothBorder field by the viewer; the backend consumes the
        // raw values).
        res.spreadSize = float(viewParamOverride<App::PropertyInteger>(
                view, "Shadow", "SpreadSize",
                ViewParams::getShadowSpreadSize()));
        res.spreadSampleSize = float(viewParamOverride<App::PropertyInteger>(
                view, "Shadow", "SpreadSampleSize",
                ViewParams::getShadowSpreadSampleSize()));
        res.precision = float(viewParamOverride<App::PropertyFloat>(
                view, "Shadow", "Precision",
                ViewParams::getShadowPrecision()));
        // The ground receiver settings honor the per-view Shadow_*
        // dynamic properties (created by the Shadow draw style, which is
        // the only way a shadow light gets here) with ViewParams
        // fallback. The light itself is per-view already: it comes from
        // the Shadow style's Coin light node built from the same
        // properties. Not honored: Shadow_GroundSizeAuto=false explicit
        // ground extents (the backend sizes its ground from the scene
        // bounding box only).
        res.ground = viewParamOverride<App::PropertyBool>(
                view, "Shadow", "ShowGround", ViewParams::getShadowShowGround());
        res.groundScale = float(viewParamOverride<App::PropertyFloat>(
                view, "Shadow", "GroundSizeScale", ViewParams::getShadowGroundScale()));
        if (auto prop = viewPropOverride<App::PropertyColor>(view, "Shadow", "GroundColor"))
            res.groundColor = prop->getValue().getPackedValue();
        else
            res.groundColor = uint32_t(ViewParams::getShadowGroundColor());
        // Ground texture (Shadow_GroundTexture / ShadowGroundTexture):
        // decoded once per path with Qt and cached — the backend keys
        // GPU uploads on the stable textureId.
        std::string texpath;
        if (auto prop = viewPropOverride<App::PropertyFileIncluded>(
                    view, "Shadow", "GroundTexture")) {
            if (prop->getValue())
                texpath = prop->getValue();
        }
        else {
            texpath = ViewParams::getShadowGroundTexture();
        }
        // Image files decoded once per path with Qt and cached — the
        // backend keys GPU uploads on the stable textureId. keepGray
        // preserves grayscale images as one component: a bump map's
        // component count is what tells a height field (1/2) from a
        // tangent-space normal map (3/4).
        auto loadImage = [](const std::string &path, bool keepGray)
                -> std::shared_ptr<const Render::TextureImage> {
            if (path.empty())
                return nullptr;
            static std::map<std::pair<std::string, bool>,
                            std::shared_ptr<const Render::TextureImage>>
                cache;
            auto key = std::make_pair(path, keepGray);
            auto it = cache.find(key);
            if (it == cache.end()) {
                std::shared_ptr<Render::TextureImage> tex;
                QImage img;
                if (img.load(QString::fromUtf8(path.c_str()))) {
                    bool alpha = img.hasAlphaChannel();
                    bool gray = keepGray && !alpha && img.isGrayscale();
                    img = img.convertToFormat(
                        gray ? QImage::Format_Grayscale8
                             : alpha ? QImage::Format_RGBA8888
                                     : QImage::Format_RGB888);
                    // Render::TextureImage rows are bottom-up like GL.
                    img = img.mirrored(false, true);
                    tex = std::make_shared<Render::TextureImage>();
                    // Outside the Coin node-id space the scene textures
                    // key on.
                    static uint64_t nextId = 0;
                    tex->textureId = 0x8000000000000000ULL + ++nextId;
                    tex->width = img.width();
                    tex->height = img.height();
                    tex->numComponents = gray ? 1 : alpha ? 4 : 3;
                    int rowLen = img.width() * tex->numComponents;
                    tex->pixels.resize(size_t(rowLen) * img.height());
                    for (int y = 0; y < img.height(); ++y)
                        std::memcpy(tex->pixels.data()
                                        + size_t(y) * rowLen,
                                    img.constScanLine(y), rowLen);
                }
                it = cache.emplace(key, std::move(tex)).first;
            }
            return it->second;
        };
        res.groundTexture = loadImage(texpath, false);
        if (res.groundTexture) {
            res.groundTextureSize =
                float(viewParamOverride<App::PropertyFloat>(
                    view, "Shadow", "GroundTextureSize",
                    ViewParams::getShadowGroundTextureSize()));
        }

        // Ground transparency and bump map (Shadow_GroundTransparency /
        // Shadow_GroundBumpMap; the Shadow draw style materializes the
        // constrained float, so accept both float property types).
        if (auto prop = viewPropOverride<App::PropertyFloat>(
                    view, "Shadow", "GroundTransparency"))
            res.groundTransparency = float(prop->getValue());
        else
            res.groundTransparency =
                float(ViewParams::getShadowGroundTransparency());
        res.groundTransparency =
            std::min(1.0f, std::max(0.0f, res.groundTransparency));
        std::string bumppath;
        if (auto prop = viewPropOverride<App::PropertyFileIncluded>(
                    view, "Shadow", "GroundBumpMap")) {
            if (prop->getValue())
                bumppath = prop->getValue();
        }
        else {
            bumppath = ViewParams::getShadowGroundBumpMap();
        }
        res.groundBumpMap = loadImage(bumppath, true);
        // Ground reflection is a render-engine extra (no Coin shadow
        // ground counterpart), so it lives in the Render_* family.
        res.groundReflection = viewParamOverride<App::PropertyBool>(
                view, "Render", "GroundReflection",
                RenderParams::getGroundReflection());
        res.groundReflectionIntensity =
            float(viewParamOverride<App::PropertyFloat>(
                view, "Render", "GroundReflectionIntensity",
                RenderParams::getGroundReflectionIntensity()));
    }
    return res;
}

Render::VolumetricConfig
RendererBridge::translateVolumetricConfig(View3DInventor * view)
{
    Render::VolumetricConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "Volumetric", RenderParams::getVolumetric());
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "VolumetricIntensity",
            RenderParams::getVolumetricIntensity()));
    res.density = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "VolumetricDensity",
            RenderParams::getVolumetricDensity()));
    res.caustics = viewParamOverride<App::PropertyBool>(
            view, "Render", "Caustics", RenderParams::getCaustics());
    res.causticsIntensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CausticsIntensity",
            RenderParams::getCausticsIntensity()));
    res.causticsScale = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CausticsScale",
            RenderParams::getCausticsScale()));
    res.causticsSpeed = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "CausticsSpeed",
            RenderParams::getCausticsSpeed()));
    return res;
}

Render::WaterConfig
RendererBridge::translateWaterConfig(View3DInventor * view)
{
    Render::WaterConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterSurface",
            RenderParams::getWaterSurface());
    res.waveStrength = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterWaveStrength",
            RenderParams::getWaterWaveStrength()));
    res.waveScale = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterWaveScale",
            RenderParams::getWaterWaveScale()));
    res.waveSpeed = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterWaveSpeed",
            RenderParams::getWaterWaveSpeed()));
    res.absorption = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterAbsorption",
            RenderParams::getWaterAbsorption()));
    res.inscatter = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterInscatter",
            RenderParams::getWaterInscatter()));
    res.refraction = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterRefraction",
            RenderParams::getWaterRefraction());
    res.reflection = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterReflection",
            RenderParams::getWaterReflection());
    res.planarReflection = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterPlanarReflection",
            RenderParams::getWaterPlanarReflection());
    return res;
}

Render::BumpConfig
RendererBridge::translateBumpConfig(View3DInventor * view)
{
    Render::BumpConfig res;
    res.scale = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BumpScale", RenderParams::getBumpScale()));
    res.parallax = viewParamOverride<App::PropertyBool>(
            view, "Render", "Parallax", RenderParams::getParallax());
    return res;
}

Render::PBRConfig
RendererBridge::translatePBRConfig(View3DInventor * view)
{
    Render::PBRConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "PBR", RenderParams::getPBR());
    res.metallic = float(viewParamOverride<App::PropertyFloatConstraint>(
            view, "Render", "PBRMetallic", RenderParams::getPBRMetallic()));
    res.roughness = float(viewParamOverride<App::PropertyFloatConstraint>(
            view, "Render", "PBRRoughness", RenderParams::getPBRRoughness()));
    res.envIntensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "PBREnvIntensity", RenderParams::getPBREnvIntensity()));
    return res;
}

float
RendererBridge::translateAutoZoomScale(SoState * state)
{
  // SoAutoZoomTranslation::getScaleFactor with a node scaleFactor of 1;
  // each autozoom entry multiplies its own scaleFactor in the backend.
  const SbViewVolume & vv = SoViewVolumeElement::get(state);
  if (vv.getWidth() == 0.0f || vv.getHeight() == 0.0f)
    return 1.0f;
  float aspectRatio =
      SoViewportRegionElement::get(state).getViewportAspectRatio();
  return vv.getWorldToScreenScale(SbVec3f(0.f, 0.f, 0.f), 0.1f)
      / (5.0f * aspectRatio);
}

// vim: noai:ts=2:sw=2
