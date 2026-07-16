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
#include <unordered_map>

#include <Inventor/SbBox3f.h>
#include <Inventor/SbPlane.h>
#include <Inventor/elements/SoDepthBufferElement.h>
#include <Inventor/elements/SoDrawStyleElement.h>
#include <Inventor/elements/SoLazyElement.h>
#include <Inventor/elements/SoPolygonOffsetElement.h>
#include <Inventor/nodes/SoClipPlane.h>

#include "SoFCRendererBridge.h"
#include "SoFCRenderer.h"
#include "SoFCVertexCache.h"
#include "../ViewParams.h"

using namespace Gui;

typedef SoFCRenderCache::Material CoinMaterial;
typedef SoFCRenderCache::VertexCacheEntry VertexCacheEntry;

namespace {

// MeshData that keeps its SoFCVertexCache (and thus all the exposed CPU
// arrays, which are ref-counted and shared across cache generations) alive.
struct CacheMeshData : Render::MeshData {
    Gui::CoinPtr<SoFCVertexCache> holder;
};

std::shared_ptr<const Render::MeshData>
translateCache(SoFCVertexCache * cache)
{
    auto mesh = std::make_shared<CacheMeshData>();
    mesh->holder = cache;
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
    return mesh;
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
    return (selId & SoFCRenderer::SelIdFull) && !m.partialhighlight;
}

Render::Material
translateMaterial(const CoinMaterial & m, int selId, bool highlight)
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
    res.shininess = m.shininess;
    res.linewidth = m.linewidth;
    res.pointsize = m.pointsize;

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
                          int selId, bool highlight)
{
    Render::DrawCallList res;

    // Share one MeshData among all entries referring to the same cache.
    std::unordered_map<SoFCVertexCache *,
                       std::shared_ptr<const Render::MeshData>> meshes;

    for (const auto & v : vcachemap) {
        const CoinMaterial & material = v.first;
        if (v.second.empty())
            continue;
        if (material.drawstyle == SoDrawStyleElement::INVISIBLE)
            continue;

        Render::Material rmat = translateMaterial(material, selId, highlight);

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
    return res;
}

// vim: noai:ts=2:sw=2
