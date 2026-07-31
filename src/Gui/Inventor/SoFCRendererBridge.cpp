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
#include <set>
#include <sstream>
#include <unordered_map>

#include <QImage>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyFile.h>
#include <App/PropertyGeo.h>
#include <App/PropertyStandard.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>

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
#include "../Renderer/MeshSource.h"
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoSpotLight.h>
#include <Inventor/annex/FXViz/nodes/SoShadowDirectionalLight.h>
#include <Inventor/elements/SoLightElement.h>
#include <Inventor/elements/SoViewingMatrixElement.h>
#include <Inventor/nodes/SoTexture2.h>
#include <Inventor/nodes/SoShaderProgram.h>
#include <Inventor/nodes/SoShaderObject.h>
#include <Inventor/nodes/SoVertexShader.h>
#include <Inventor/nodes/SoFragmentShader.h>
#include <Inventor/nodes/SoShaderParameter.h>

#include "SoAutoZoomTranslation.h"
#include "SoFCRendererBridge.h"
#include "SoFCDisplayModeElement.h"
#include "SoFCRenderer.h"
#include "SoFCVertexCache.h"
#include "../ViewParams.h"
#include "../RenderParams.h"
#include "../View3DInventor.h"

FC_LOG_LEVEL_INIT("Renderer", true, true)

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
    if (SoNode *node = cache->getNode()) {
        // Proto node preferred: color variants of one geometry carry
        // the same source tag as their base, so a shape-backed level
        // generator registered on the base claims them all.
        SoNode *proto = SoFCVertexCache::getProtoNode(node);
        mesh->sourceTag = proto ? proto : node;
        // A producer running coarse-first registered what the display
        // tessellation itself is; the serializer places the mesh on
        // its ladder by this and declares the exact rung above it.
        mesh->levelError = Render::MeshSourceRegistry::instance()
                               .publishedError(mesh->sourceTag);
    }

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
        res.fountain = m.fountain;
        res.fountaindensity = m.fountaindensity;
        res.fountaindetail = m.fountaindetail;
        res.fountainspeed = m.fountainspeed;
        res.lightsource = m.lightsource;
        res.lightintensity = m.lightintensity;
        res.lightrange = m.lightrange;
        res.lightshadow = m.lightshadow;
        res.lightshadowext = m.lightshadowext;
        // User "material"/"water"-stage shader (docs/RenderDebug.md §6);
        // the shared translation's pointer identity is the batch key.
        res.usershader = m.usershader;
        // Binding a "water"-stage effect IS the water activation
        // (docs/RenderEngine.md §5.11): the draw becomes a water body
        // exactly as if Render_Water were set, so the whole pass set
        // (body detection, scene copy, planar reflection, back depth,
        // medium exemptions) engages unchanged, with the user program
        // replacing fs_fc_water at the surface submit.
        if (res.usershader && res.usershader->stage == "water")
            res.water = true;
        // Likewise a "volume"-stage medium function makes the draw a
        // medium body: the proxy volume raymarches in the shared
        // volumetric pass, which dispatches the user functions for the
        // body's slot. The source picks its channel by contract
        // function — fcMediumScatter = the scattering channel (a
        // fountain body: flow frame + splash machinery engage),
        // fcMediumField/fcMediumRamp = the emissive channel (a fire
        // body).
        if (res.usershader && res.usershader->stage == "volume") {
            if (res.usershader->fragmentSource.find("fcMediumScatter")
                    != std::string::npos)
                res.fountain = true;
            else
                res.fire = true;
        }
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
                          int selId, bool highlight, bool sequentialOrder,
                          Render::ObjectInfoMap * objectInfo)
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

            // Per-edge / per-vertex ranges for browser-side edge/vertex
            // preselection, mirroring the triangleParts face table above.
            // No count API exists for line/point parts, so enumerate until
            // the range getter reports the part is out of range.
            if (rmat.type == Render::Material::Line && ventry.partidx < 0
                    && mesh->lineParts.empty()) {
                for (int i = 0; ; ++i) {
                    int start = 0, count = 0;
                    if (!ventry.cache->getLinePartRange(i, start, count))
                        break;
                    mesh->lineParts.emplace_back(start, count);
                }
            }
            if (rmat.type == Render::Material::Point && ventry.partidx < 0
                    && mesh->pointParts.empty()) {
                for (int i = 0; ; ++i) {
                    int start = 0, count = 0;
                    if (!ventry.cache->getPointPartRange(i, start, count))
                        break;
                    mesh->pointParts.emplace_back(start, count);
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
            if (objectInfo && draw.objectKey
                    && !objectInfo->count(draw.objectKey)) {
                if (const auto & org = ventry.key->getOrigin()) {
                    Render::ObjectInfo info;
                    info.doc = org->doc;
                    info.obj = org->obj;
                    // Label and type are read fresh: the origin was
                    // captured at cache build, and a label can change
                    // without touching the scene graph.
                    if (auto doc = App::GetApplication().getDocument(
                                org->doc.c_str())) {
                        if (auto obj = doc->getObject(org->obj.c_str())) {
                            info.label = obj->Label.getValue();
                            info.type = obj->getTypeId().getName();
                        }
                    }
                    (*objectInfo)[draw.objectKey] = std::move(info);
                }
            }
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
            view, "Render", "AO", RenderParams::getAO());
    res.radius = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "AORadius", RenderParams::getAORadius()));
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "AOIntensity", RenderParams::getAOIntensity()));
    res.method = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "AOMethod", RenderParams::getAOMethod()));
    res.slices = int(viewParamOverride<App::PropertyInteger>(
            view, "Render", "AOSlices", RenderParams::getAOSlices()));
    res.steps = int(viewParamOverride<App::PropertyInteger>(
            view, "Render", "AOSteps", RenderParams::getAOSteps()));
    return res;
}

bool
RendererBridge::translateShaderParamValues(const App::Property * prop,
                                           std::vector<float> & values)
{
    std::vector<float> res;
    if (auto p = dynamic_cast<const App::PropertyBool*>(prop))
        res = {p->getValue() ? 1.0f : 0.0f};
    else if (auto p = dynamic_cast<const App::PropertyEnumeration*>(prop))
        res = {float(p->getValue())};
    else if (auto p = dynamic_cast<const App::PropertyInteger*>(prop))
        res = {float(p->getValue())};
    else if (auto p = dynamic_cast<const App::PropertyFloat*>(prop))
        res = {float(p->getValue())};
    else if (auto p = dynamic_cast<const App::PropertyColor*>(prop)) {
        App::Color c = p->getValue();
        res = {c.r, c.g, c.b, c.a};
    }
    else if (auto p = dynamic_cast<const App::PropertyVector*>(prop)) {
        Base::Vector3d vec = p->getValue();
        res = {float(vec.x), float(vec.y), float(vec.z)};
    }
    else if (auto p = dynamic_cast<const App::PropertyFloatList*>(prop)) {
        for (double d : p->getValues())
            res.push_back(float(d));
    }
    else if (auto p = dynamic_cast<const App::PropertyIntegerList*>(prop)) {
        for (long l : p->getValues())
            res.push_back(float(l));
    }
    else
        return false;
    if (res.empty())
        return false;
    res.resize((res.size() + 3) & ~size_t(3), 0.0f);
    values = std::move(res);
    return true;
}

std::string
RendererBridge::shaderParamUniformName(const char * propName)
{
    std::string name(propName ? propName : "");
    auto pos = name.find('_');
    if (pos != std::string::npos && pos > 0)
        name = name.substr(pos + 1);
    if (name.compare(0, 2, "u_") != 0)
        name = "u_" + name;
    return name;
}

Render::RenderDebugConfig
RendererBridge::translateRenderDebugConfig(View3DInventor * view)
{
    Render::RenderDebugConfig res;
    res.viewMode = int(viewParamOverride<App::PropertyEnumeration>(
            view, "RenderDebug", "ViewMode",
            RenderParams::getDebugViewMode()));
    res.freezeFrame = viewParamOverride<App::PropertyBool>(
            view, "RenderDebug", "FreezeFrame",
            RenderParams::getDebugFreezeFrame());

    // Dynamic named shader parameters (docs/RenderDebug.md §2.5): every
    // further RenderDebug_* property becomes a like-named vec4(-array)
    // uniform — RenderDebug_myKnob feeds "uniform vec4 u_myKnob"; list
    // properties span multiple vec4 lanes (RenderDebug_userParams with
    // 16 floats fills the stock shaders' u_userParams[4] fallback
    // pool). The property map is name-ordered, keeping the vector
    // deterministic for the config-change comparison.
    if (view) {
        static const char prefix[] = "RenderDebug_";
        static const size_t prefixLen = sizeof(prefix) - 1;
        std::map<std::string, App::Property*> props;
        view->getPropertyMap(props);
        for (const auto &v : props) {
            if (v.first.compare(0, prefixLen, prefix) != 0)
                continue;
            std::string name = v.first.substr(prefixLen);
            if (name.empty() || name == "ViewMode" || name == "FreezeFrame"
                    || name == "Label")   // the §4.3 burn-in toggle
                continue;
            Render::RenderDebugConfig::UserParam param;
            param.name = name.compare(0, 2, "u_") == 0 ? name : "u_" + name;
            App::Property *prop = v.second;
            if (!translateShaderParamValues(prop, param.values)) {
                static std::set<std::string> warned;
                if (warned.insert(v.first).second)
                    FC_WARN("render debug parameter " << v.first
                            << ": unsupported property type "
                            << prop->getTypeId().getName());
                continue;
            }
            res.userParams.push_back(std::move(param));
        }
    }
    return res;
}

// Extract a typed SoShaderParameter value as floats; empty = unsupported.
static std::vector<float> shaderParamValues(const SoNode * node)
{
    std::vector<float> res;
    if (auto p = dynamic_cast<const SoShaderParameter1f*>(node))
        res = {p->value.getValue()};
    else if (auto p = dynamic_cast<const SoShaderParameter1i*>(node))
        res = {float(p->value.getValue())};
    else if (auto p = dynamic_cast<const SoShaderParameter2f*>(node)) {
        const SbVec2f &v = p->value.getValue();
        res = {v[0], v[1]};
    }
    else if (auto p = dynamic_cast<const SoShaderParameter3f*>(node)) {
        const SbVec3f &v = p->value.getValue();
        res = {v[0], v[1], v[2]};
    }
    else if (auto p = dynamic_cast<const SoShaderParameter4f*>(node)) {
        const SbVec4f &v = p->value.getValue();
        res = {v[0], v[1], v[2], v[3]};
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray1f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i)
            res.push_back(p->value[i]);
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray2f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i) {
            res.push_back(p->value[i][0]);
            res.push_back(p->value[i][1]);
        }
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray3f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i) {
            res.push_back(p->value[i][0]);
            res.push_back(p->value[i][1]);
            res.push_back(p->value[i][2]);
        }
    }
    else if (auto p = dynamic_cast<const SoShaderParameterArray4f*>(node)) {
        for (int i = 0; i < p->value.getNum(); ++i)
            for (int c = 0; c < 4; ++c)
                res.push_back(p->value[i][c]);
    }
    return res;
}

// Fetch a shader object's bgfx .sc source: inline for BGFX_SC, read from
// disk for FILENAME with a .sc suffix. Empty = not consumable.
static std::string shaderObjectSource(const SoShaderObject * obj)
{
    SbString src = obj->sourceProgram.getValue();
    if (src.getLength() == 0)
        return {};
    int type = obj->sourceType.getValue();
    if (type == SoShaderObject::BGFX_SC)
        return src.getString();
    if (type == SoShaderObject::FILENAME) {
        int len = src.getLength();
        if (len <= 3 || src.getSubString(len - 3) != ".sc")
            return {};
        Base::FileInfo fi(src.getString());
        Base::ifstream file(fi);
        if (!file) {
            FC_WARN("user shader source not found: " << src.getString());
            return {};
        }
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
    return {};
}

bool
RendererBridge::translateShaderProgram(const SoNode * node,
                                       Render::UserShader & out)
{
    auto prog = dynamic_cast<const SoShaderProgram*>(node);
    if (!prog)
        return false;
    out.stage = prog->stage.getValue().getString();
    for (int i = 0; i < prog->shaderObject.getNum(); ++i) {
        SoNode * child = prog->shaderObject[i];
        auto obj = dynamic_cast<SoShaderObject*>(child);
        if (!obj || !obj->isActive.getValue())
            continue;
        std::string src = shaderObjectSource(obj);
        if (src.empty())
            continue;
        if (obj->isOfType(SoVertexShader::getClassTypeId()))
            out.vertexSource = std::move(src);
        else if (obj->isOfType(SoFragmentShader::getClassTypeId()))
            out.fragmentSource = std::move(src);
        else
            continue;   // geometry shaders: not consumable by bgfx
        for (int j = 0; j < obj->parameter.getNum(); ++j) {
            auto pnode = obj->parameter[j];
            auto sp = dynamic_cast<const SoShaderParameter*>(pnode);
            if (!sp || sp->name.getValue().getLength() == 0)
                continue;
            Render::RenderDebugConfig::UserParam param;
            param.name = sp->name.getValue().getString();
            param.values = shaderParamValues(pnode);
            if (param.values.empty()) {
                static std::set<std::string> warned;
                if (warned.insert(param.name).second)
                    FC_WARN("user shader parameter " << param.name
                            << ": unsupported parameter node type "
                            << pnode->getTypeId().getName().getString());
                continue;
            }
            param.values.resize((param.values.size() + 3) & ~size_t(3),
                                0.0f);
            out.params.push_back(std::move(param));
        }
    }
    // A post program only needs a fragment stage (the backend supplies
    // the full-screen vertex shader); nothing at all means nothing to do.
    return !out.fragmentSource.empty() || !out.vertexSource.empty();
}

/// Image file (ground texture/bump map, PBR environment) decoded once
/// per path with Qt and cached — the backend keys GPU uploads on the
/// stable textureId. keepGray preserves grayscale images as one
/// component: a bump map's component count is what tells a height field
/// (1/2) from a tangent-space normal map (3/4).
static std::shared_ptr<const Render::TextureImage>
loadParamImage(const std::string &path, bool keepGray)
{
    if (path.empty())
        return nullptr;
    static std::map<std::pair<std::string, bool>,
                    std::shared_ptr<const Render::TextureImage>> cache;
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
            // Outside the Coin node-id space the scene textures key on.
            static uint64_t nextId = 0;
            tex->textureId = 0x8000000000000000ULL + ++nextId;
            tex->width = img.width();
            tex->height = img.height();
            tex->numComponents = gray ? 1 : alpha ? 4 : 3;
            int rowLen = img.width() * tex->numComponents;
            tex->pixels.resize(size_t(rowLen) * img.height());
            for (int y = 0; y < img.height(); ++y)
                std::memcpy(tex->pixels.data() + size_t(y) * rowLen,
                            img.constScanLine(y), rowLen);
        }
        it = cache.emplace(key, std::move(tex)).first;
    }
    return it->second;
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
        res.groundTexture = loadParamImage(texpath, false);
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
        res.groundBumpMap = loadParamImage(bumppath, true);
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
    // The visible sun disc is a render-engine extra like the ground
    // reflection (no Coin counterpart).
    res.sunDisc = viewParamOverride<App::PropertyBool>(
            view, "Render", "SunDisc", RenderParams::getSunDisc());
    res.sunDiscSize = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "SunDiscSize",
            RenderParams::getSunDiscSize()));
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
    res.shadow = viewParamOverride<App::PropertyBool>(
            view, "Render", "WaterShadow",
            RenderParams::getWaterShadow());
    res.shadowWobble = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterShadowWobble",
            RenderParams::getWaterShadowWobble()));
    res.rippleType = int(viewParamOverride<App::PropertyEnumeration>(
            view, "Render", "WaterRippleType",
            RenderParams::getWaterRippleType()));
    res.rippleDensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "WaterRippleDensity",
            RenderParams::getWaterRippleDensity()));
    return res;
}

Render::BloomConfig
RendererBridge::translateBloomConfig(View3DInventor * view)
{
    Render::BloomConfig res;
    res.enabled = viewParamOverride<App::PropertyBool>(
            view, "Render", "Bloom", RenderParams::getBloom());
    res.threshold = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BloomThreshold",
            RenderParams::getBloomThreshold()));
    res.intensity = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BloomIntensity",
            RenderParams::getBloomIntensity()));
    res.radius = float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "BloomRadius",
            RenderParams::getBloomRadius()));
    return res;
}

// The per-draw face-outline width formula (see the selection block above)
// with a nominal 1px base line width: the viewer applies it to the hovered /
// selected face, which carries no explicit outline width.
static float preselOutlineWidth()
{
    float lw = 1.0f;
    float scale = float(ViewParams::getSelectionLineThicken());
    if (scale < 1.0f)
        scale = 1.0f;
    float w = lw * scale;
    if (ViewParams::getSelectionLineMaxWidth() > 1.0)
        w = std::min<float>(w, std::max<float>(lw,
                float(ViewParams::getSelectionLineMaxWidth())));
    return std::max(w * 1.5f, lw * float(ViewParams::getOutlineThicken()));
}

Render::PreselHighlightConfig
RendererBridge::translatePreselConfig()
{
    Render::PreselHighlightConfig res;
    res.color = (uint32_t)ViewParams::getHighlightColor();
    res.faceOutline = ViewParams::getShowPreSelectedFaceOutline();
    res.outlineOnly = ViewParams::getNoPreSelFaceHighlightWithOutline();
    res.outlineWidth = preselOutlineWidth();
    res.pickRadius = (float)ViewParams::getPickRadius();
    return res;
}

Render::PreselHighlightConfig
RendererBridge::translateSelConfig()
{
    Render::PreselHighlightConfig res;
    res.color = (uint32_t)ViewParams::getSelectionColor();
    res.faceOutline = ViewParams::getShowSelectedFaceOutline();
    res.outlineOnly = ViewParams::getNoSelFaceHighlightWithOutline();
    res.outlineWidth = preselOutlineWidth();
    res.pickRadius = (float)ViewParams::getPickRadius();
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
    res.envBackground = viewParamOverride<App::PropertyBool>(
            view, "Render", "PBREnvBackground",
            RenderParams::getPBREnvBackground());
    // User environment image. With no explicit path, fall back to the
    // image the Texture mapping dialog (Std_TextureMapping) holds — its
    // Environment mode sphere-maps that same file over the scene
    // through Coin, so picking one there also lights the backend.
    std::string envpath;
    // An embedded copy (Render_PBREnvEmbed) wins: it travels with the
    // document, while the path may not resolve on another machine.
    if (auto prop = viewPropOverride<App::PropertyFileIncluded>(
                view, "Render", "PBREnvImageData")) {
        if (prop->getValue())
            envpath = prop->getValue();
    }
    if (envpath.empty()) {
        if (auto prop = viewPropOverride<App::PropertyFile>(
                    view, "Render", "PBREnvImage")) {
            if (prop->getValue())
                envpath = prop->getValue();
        }
        else {
            envpath = RenderParams::getPBREnvImage();
        }
    }
    if (envpath.empty())
        envpath = App::GetApplication().Config()["TextureImage"];
    res.envImage = loadParamImage(envpath, false);
    return res;
}

float
RendererBridge::translateEffectResolution(View3DInventor * view)
{
    return float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "EffectResolution",
            RenderParams::getEffectResolution()));
}

float
RendererBridge::translateSSAOResolution(View3DInventor * view)
{
    return float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "AOResolution",
            RenderParams::getAOResolution()));
}

float
RendererBridge::translateLevelTolerance(View3DInventor * view)
{
    return float(viewParamOverride<App::PropertyFloat>(
            view, "Render", "LevelTolerance",
            RenderParams::getLevelTolerance()));
}

size_t
RendererBridge::translateGpuMemoryBudget(View3DInventor * view)
{
    long mb = long(viewParamOverride<App::PropertyInteger>(
            view, "Render", "GpuMemoryBudgetMB",
            RenderParams::getGpuMemoryBudgetMB()));
    return mb > 0 ? size_t(mb) << 20 : 0;
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
