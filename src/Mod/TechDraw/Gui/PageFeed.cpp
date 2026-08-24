/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
#include <cmath>
#endif

#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>

#include <Gui/Renderer/Page2D.h>
#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawViewPart.h>
#include <Mod/TechDraw/App/Geometry.h>

#include "PageFeed.h"
#include "Rez.h"

using namespace TechDrawGui;
using Render::Page2D;

namespace {

// Stable item ids: FNV-1a over the view's document name, a kind tag and
// the geometry index. Re-feeding an edited view overwrites (= damages)
// exactly its own items.
uint64_t itemId(const char* viewName, char kindTag, uint32_t index)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    for (const char* p = viewName; *p; ++p)
        mix((uint8_t)*p);
    mix((uint8_t)kindTag);
    for (int i = 0; i < 4; ++i)
        mix((uint8_t)(index >> (i * 8)));
    return h;
}

struct Pt
{
    float x;
    float y;
};

// Discretize one projected edge into view-local Rez points. The
// occEdge lives in the same y-inverted, scaled space as the stored
// geometry attributes; deflection is given in Rez units.
void discretize(const TechDraw::BaseGeomPtr& geom, float deflection,
                std::vector<Pt>& out)
{
    out.clear();
    if (geom->getGeomType() == TechDraw::GENERIC) {
        auto generic = std::static_pointer_cast<TechDraw::Generic>(geom);
        out.reserve(generic->points.size());
        for (const Base::Vector3d& v : generic->points)
            out.push_back({(float)Rez::guiX(v.x), (float)Rez::guiX(v.y)});
        return;
    }
    try {
        BRepAdaptor_Curve adapt(geom->getOCCEdge());
        GCPnts_QuasiUniformDeflection pnts(adapt, Rez::appX(deflection));
        if (!pnts.IsDone())
            return;
        int n = pnts.NbPoints();
        out.reserve(n);
        for (int i = 1; i <= n; ++i) {
            gp_Pnt p = pnts.Value(i);
            out.push_back({(float)Rez::guiX(p.X()), (float)Rez::guiX(p.Y())});
        }
    }
    catch (...) {
        out.clear();
    }
}

void emitPolyline(Page2D::Recorder& rec, const std::vector<Pt>& pts,
                  float ox, float oy)
{
    if (pts.size() < 2)
        return;
    std::vector<float> xy;
    xy.reserve(pts.size() * 2);
    for (const Pt& p : pts) {
        xy.push_back(ox + p.x);
        xy.push_back(oy + p.y);
    }
    rec.polyline(xy.data(), (uint32_t)pts.size());
}

// One standalone edge as a stroked path. Circles and beziers keep their
// exact vg form (re-flattened per zoom band by vg); the rest becomes a
// polyline at the style deflection for now.
bool emitEdge(Page2D::Recorder& rec, const TechDraw::BaseGeomPtr& geom,
              float ox, float oy, float deflection)
{
    using namespace TechDraw;
    switch (geom->getGeomType()) {
    case CIRCLE: {
        auto circle = std::static_pointer_cast<Circle>(geom);
        rec.beginPath();
        rec.circle(ox + (float)Rez::guiX(circle->center.x),
                   oy + (float)Rez::guiX(circle->center.y),
                   (float)Rez::guiX(circle->radius));
        return true;
    }
    case BEZIER: {
        auto bez = std::static_pointer_cast<BezierSegment>(geom);
        const auto& p = bez->pnts;
        auto X = [&](int i) { return ox + (float)Rez::guiX(p[i].x); };
        auto Y = [&](int i) { return oy + (float)Rez::guiX(p[i].y); };
        rec.beginPath();
        if (bez->poles == 2) {
            rec.moveTo(X(0), Y(0));
            rec.lineTo(X(1), Y(1));
        }
        else if (bez->poles == 3) {
            rec.moveTo(X(0), Y(0));
            rec.quadraticTo(X(1), Y(1), X(2), Y(2));
        }
        else if (bez->poles == 4) {
            rec.moveTo(X(0), Y(0));
            rec.cubicTo(X(1), Y(1), X(2), Y(2), X(3), Y(3));
        }
        else
            return false;
        return true;
    }
    case BSPLINE: {
        auto spline = std::static_pointer_cast<BSpline>(geom);
        if (spline->segments.empty())
            return false;
        rec.beginPath();
        bool first = true;
        for (const BezierSegment& seg : spline->segments) {
            const auto& p = seg.pnts;
            auto X = [&](int i) { return ox + (float)Rez::guiX(p[i].x); };
            auto Y = [&](int i) { return oy + (float)Rez::guiX(p[i].y); };
            if (first) {
                rec.moveTo(X(0), Y(0));
                first = false;
            }
            if (seg.poles == 2)
                rec.lineTo(X(1), Y(1));
            else if (seg.poles == 3)
                rec.quadraticTo(X(1), Y(1), X(2), Y(2));
            else if (seg.poles == 4)
                rec.cubicTo(X(1), Y(1), X(2), Y(2), X(3), Y(3));
            else
                return false;
        }
        return true;
    }
    default: {
        std::vector<Pt> pts;
        discretize(geom, deflection, pts);
        if (pts.size() < 2)
            return false;
        rec.beginPath();
        emitPolyline(rec, pts, ox, oy);
        return true;
    }
    }
}

// A face as closed polyline sub-paths (one per wire), filled even-odd:
// holes come out as holes without native hole support in vg. Wire edges
// are stitched with the same nearest-endpoint heuristic the Qt path
// builder uses.
void emitFace(Page2D::Recorder& rec, const TechDraw::FacePtr& face, float ox,
              float oy, float deflection, uint32_t color)
{
    rec.beginPath();
    bool any = false;
    std::vector<Pt> pts;
    for (TechDraw::Wire* wire : face->wires) {
        std::vector<Pt> contour;
        for (const TechDraw::BaseGeomPtr& geom : wire->geoms) {
            discretize(geom, deflection, pts);
            if (pts.size() < 2)
                continue;
            if (!contour.empty()) {
                const Pt& end = contour.back();
                auto d2 = [&end](const Pt& p) {
                    float dx = p.x - end.x, dy = p.y - end.y;
                    return dx * dx + dy * dy;
                };
                if (d2(pts.front()) > d2(pts.back()))
                    std::reverse(pts.begin(), pts.end());
                contour.insert(contour.end(), pts.begin() + 1, pts.end());
            }
            else
                contour = pts;
        }
        if (contour.size() < 3)
            continue;
        rec.moveTo(ox + contour[0].x, oy + contour[0].y);
        for (size_t i = 1; i < contour.size(); ++i)
            rec.lineTo(ox + contour[i].x, oy + contour[i].y);
        rec.closePath();
        any = true;
    }
    if (any)
        rec.fillConcave(color, /*evenOdd*/ true);
}

} // namespace

void PageFeed::feedViewPart(TechDraw::DrawViewPart* dvp, Page2D& out,
                            const Style& style, uint32_t layer)
{
    if (!dvp)
        return;
    const char* name = dvp->getNameInDocument();
    if (!name)
        return;
    // The scene position of the view: page coordinates are y-up mm,
    // the page (and Qt scene) y-down Rez units.
    const float ox = (float)Rez::guiX(dvp->X.getValue());
    const float oy = (float)-Rez::guiX(dvp->Y.getValue());

    if ((style.faceColor & 0xff) != 0) {
        uint32_t index = 0;
        for (const TechDraw::FacePtr& face : dvp->getFaceGeometry()) {
            Page2D::Recorder rec;
            emitFace(rec, face, ox, oy, style.deflection, style.faceColor);
            if (!rec.empty())
                out.setItem(itemId(name, 'f', index), Page2D::Kind::Face,
                            layer, std::move(rec));
            ++index;
        }
    }

    uint32_t index = 0;
    const bool showHidden = dvp->HardHidden.getValue();
    for (const TechDraw::BaseGeomPtr& geom : dvp->getEdgeGeometry()) {
        Page2D::Recorder rec;
        bool visible = geom->getHlrVisible();
        if (!visible && !showHidden) {
            ++index;
            continue;
        }
        if (emitEdge(rec, geom, ox, oy, style.deflection))
            rec.stroke(visible ? style.edgeColor : style.hiddenColor,
                       visible ? style.edgeWidth : style.hiddenWidth);
        if (!rec.empty())
            out.setItem(itemId(name, 'e', index), Page2D::Kind::Edge, layer,
                        std::move(rec));
        ++index;
    }

    index = 0;
    for (const TechDraw::VertexPtr& vert : dvp->getVertexGeometry()) {
        if (vert->isCenter() || vert->isReference()) {
            ++index;
            continue;
        }
        Page2D::Recorder rec;
        rec.beginPath();
        rec.circle(ox + (float)Rez::guiX(vert->x()),
                   oy + (float)Rez::guiX(vert->y()), style.vertexRadius);
        rec.fillConvex(style.vertexColor);
        out.setItem(itemId(name, 'v', index), Page2D::Kind::Vertex, layer,
                    std::move(rec));
        ++index;
    }
}

void PageFeed::feedPage(TechDraw::DrawPage* page, Page2D& out)
{
    feedPage(page, out, Style());
}

void PageFeed::feedPage(TechDraw::DrawPage* page, Page2D& out,
                        const Style& style)
{
    if (!page)
        return;
    uint32_t layer = 0;
    for (App::DocumentObject* obj : page->getAllViews()) {
        if (auto dvp = dynamic_cast<TechDraw::DrawViewPart*>(obj))
            feedViewPart(dvp, out, style, layer);
        ++layer;
    }
}
