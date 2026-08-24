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

#include <QColor>
#include <QPen>

#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Renderer/Page2D.h>
#include <Mod/TechDraw/App/CenterLine.h>
#include <Mod/TechDraw/App/Cosmetic.h>
#include <Mod/TechDraw/App/DrawGeomHatch.h>
#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/DrawViewPart.h>
#include <Mod/TechDraw/App/Geometry.h>
#include <Mod/TechDraw/App/HatchLine.h>
#include <Mod/TechDraw/App/LineGenerator.h>
#include <Mod/TechDraw/App/Preferences.h>

#include "PageFeed.h"
#include "PreferencesGui.h"
#include "Rez.h"
#include "ViewProviderGeomHatch.h"
#include "ViewProviderPage.h"
#include "ViewProviderViewPart.h"

using namespace TechDrawGui;
using Render::Page2D;

// BaseGeom::source() values (same local convention as QGIViewPart.cpp)
#define COSMETICEDGE 1
#define CENTERLINE 2

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

uint32_t packColor(const QColor& c)
{
    return ((uint32_t)c.red() << 24) | ((uint32_t)c.green() << 16)
        | ((uint32_t)c.blue() << 8) | (uint32_t)c.alpha();
}

// Walk a polyline alternating pen-down/pen-up runs of the given lengths
// (page units), phase starting pen-down at the path start like Qt.
void emitDashedPolyline(Page2D::Recorder& rec, const std::vector<Pt>& pts,
                        float ox, float oy, const std::vector<float>& dashes)
{
    if (pts.size() < 2 || dashes.empty())
        return;
    size_t di = 0;
    float remain = dashes[0];
    bool down = true;
    float cx = ox + pts[0].x, cy = oy + pts[0].y;
    rec.moveTo(cx, cy);
    for (size_t i = 1; i < pts.size(); ++i) {
        const float ex = ox + pts[i].x, ey = oy + pts[i].y;
        float segLen = std::hypot(ex - cx, ey - cy);
        while (segLen > 0.0f) {
            if (remain <= segLen) {
                const float t = remain / segLen;
                cx += (ex - cx) * t;
                cy += (ey - cy) * t;
                segLen -= remain;
                if (down)
                    rec.lineTo(cx, cy);
                down = !down;
                if (down)
                    rec.moveTo(cx, cy);
                di = (di + 1) % dashes.size();
                remain = dashes[di];
            }
            else {
                remain -= segLen;
                if (down)
                    rec.lineTo(ex, ey);
                cx = ex;
                cy = ey;
                segLen = 0.0f;
            }
        }
    }
}

// The edge-class show/hide matrix (QGIViewPart::showThisEdge).
bool showEdgeClass(TechDraw::DrawViewPart* dvp,
                   const TechDraw::BaseGeomPtr& geom)
{
    using namespace TechDraw;
    const edgeClass cls = geom->getClassOfEdge();
    if (geom->getHlrVisible()) {
        return cls == ecHARD || cls == ecOUTLINE
            || (cls == ecSMOOTH && dvp->SmoothVisible.getValue())
            || (cls == ecSEAM && dvp->SeamVisible.getValue())
            || (cls == ecUVISO && dvp->IsoVisible.getValue());
    }
    return ((cls == ecHARD || cls == ecOUTLINE) && dvp->HardHidden.getValue())
        || (cls == ecSMOOTH && dvp->SmoothHidden.getValue())
        || (cls == ecSEAM && dvp->SeamHidden.getValue())
        || (cls == ecUVISO && dvp->IsoHidden.getValue());
}

struct EdgeStroke
{
    uint32_t color = 0x000000ff;
    float width = 6.0f;         // Rez
    std::vector<float> dashes;  // Rez; empty = solid
    bool show = true;
};

// Per-edge appearance, mirroring QGIViewPart::drawAllEdges: cosmetic
// edge / centerline formats, GeomFormat overrides, the hidden-line pen
// and the iso-line width. Dash patterns come from the same
// LineGenerator pens the Qt tier uses; Qt specifies them in pen-width
// units, so page-unit lengths scale by the stroke width.
EdgeStroke resolveEdgeStroke(TechDraw::DrawViewPart* dvp,
                             ViewProviderViewPart* vp,
                             const TechDraw::BaseGeomPtr& geom, int iEdge,
                             const PageFeed::Style& style)
{
    // Loads the ISO/ANSI line descriptions from disk once.
    static TechDraw::LineGenerator lineGen;

    EdgeStroke es;
    const double lineWidthMm = vp ? vp->LineWidth.getValue() : 0.0;
    es.color = vp ? packColor(PreferencesGui::getAccessibleQColor(
                        PreferencesGui::normalQColor()))
                  : style.edgeColor;
    es.width = vp ? (float)Rez::guiX(lineWidthMm) : style.edgeWidth;
    QPen pen(Qt::SolidLine);
    bool formatVisible = true;

    const TechDraw::LineFormat* format = nullptr;
    if (geom->getCosmetic()) {
        const int source = geom->source();
        if (source == COSMETICEDGE) {
            auto ce = dvp->getCosmeticEdge(geom->getCosmeticTag());
            if (ce)
                format = &ce->m_format;
        }
        else if (source == CENTERLINE) {
            auto cl = dvp->getCenterLine(geom->getCosmeticTag());
            if (cl)
                format = &cl->m_format;
        }
    }
    else {
        TechDraw::GeomFormat* gf = dvp->getGeomFormatBySelection(iEdge);
        if (gf)
            format = &gf->m_format;
    }
    if (format) {
        es.color = packColor(
            TechDraw::Preferences::getAccessibleColor(format->m_color)
                .asValue<QColor>());
        pen = lineGen.getBestPen(format->getLineNumber(),
                                 (Qt::PenStyle)format->m_style,
                                 format->m_weight);
        es.width = (float)Rez::guiX(format->m_weight);
        formatVisible = format->m_visible;
    }

    if (!geom->getHlrVisible()) {
        if (vp) {
            pen = lineGen.getLinePen(TechDraw::Preferences::HiddenLineStyle(),
                                     lineWidthMm);
            es.width = (float)Rez::guiX(vp->HiddenWidth.getValue());
        }
        else {
            es.color = style.hiddenColor;
            es.width = style.hiddenWidth;
        }
    }

    if (geom->getClassOfEdge() == TechDraw::ecUVISO && vp) {
        pen = QPen(Qt::SolidLine);
        es.width = (float)Rez::guiX(vp->IsoWidth.getValue());
    }

    es.show = showEdgeClass(dvp, geom)
        && (formatVisible || (vp && vp->ShowAllEdges.getValue()));

    if (pen.style() != Qt::SolidLine && es.width > 0.0f) {
        for (qreal d : pen.dashPattern())
            es.dashes.push_back((float)d * es.width);
    }
    return es;
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
    case ARCOFCIRCLE: {
        // Native vg arc: re-flattened per zoom band like circles, and
        // (M3) ~22 bytes on the wire instead of a point list. vg's arc
        // angles are clockwise-from-x-axis in the same y-down space the
        // geometry lives in; (start, end, cw) and its reversed twin
        // denote the same circle segment, so getReversed() is moot for
        // a stroke.
        auto aoc = std::static_pointer_cast<AOC>(geom);
        rec.beginPath();
        rec.arc(ox + (float)Rez::guiX(aoc->center.x),
                oy + (float)Rez::guiX(aoc->center.y),
                (float)Rez::guiX(aoc->radius), (float)aoc->startAngle,
                (float)aoc->endAngle, aoc->cw);
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

// The hatch object claiming face i, if any (QGIViewPart::faceIsGeomHatched).
TechDraw::DrawGeomHatch*
geomHatchForFace(int i, const std::vector<TechDraw::DrawGeomHatch*>& objs)
{
    for (TechDraw::DrawGeomHatch* h : objs) {
        for (const std::string& s : h->Source.getSubValues()) {
            if (TechDraw::DrawUtil::getIndexFromName(s) == i)
                return h;
        }
    }
    return nullptr;
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

    // Every index gets a setItem even when its geometry is skipped (an
    // empty recorder draws nothing): id presence then stays contiguous
    // per kind, which is what lets the trailing sweep below find and
    // remove everything a previous, larger feed of this view created.
    // Without it, a model edit that shrinks the drawing would keep the
    // stale items rendering pre-edit geometry in a long-lived page.
    auto sweep = [&](char kindTag, uint32_t from) {
        for (uint32_t i = from;; ++i) {
            Page2D::ItemId id = itemId(name, kindTag, i);
            if (!out.hasItem(id))
                break;
            out.removeItem(id);
        }
    };

    // The per-view appearance lives on the Gui side: the view provider
    // (line widths, face color, show-all-edges) and the page provider
    // (the frame toggle that also hides vertices). Both may be absent
    // in odd hosts; the Style fallbacks then apply.
    Gui::Document* gdoc =
        Gui::Application::Instance->getDocument(dvp->getDocument());
    ViewProviderViewPart* vp = nullptr;
    ViewProviderPage* vpp = nullptr;
    if (gdoc) {
        vp = dynamic_cast<ViewProviderViewPart*>(gdoc->getViewProvider(dvp));
        if (TechDraw::DrawPage* page = dvp->findParentPage())
            vpp = dynamic_cast<ViewProviderPage*>(gdoc->getViewProvider(page));
    }
    const bool frames = !vpp || vpp->getFrameState();

    uint32_t faceColor = style.faceColor;
    if (vp) {
        QColor qc = vp->FaceColor.getValue().asValue<QColor>();
        qc.setAlpha((100 - vp->FaceTransparency.getValue()) * 255 / 100);
        faceColor = packColor(qc);
    }
    // Faces: the fill, then any PAT geometric hatch as a Decoration item
    // riding the same index (drawn between fills and edges by Kind
    // order). PAT dash specifications draw solid for now; SVG/bitmap
    // hatches (DrawHatch) are not represented yet and leave the plain
    // fill.
    std::vector<TechDraw::DrawGeomHatch*> geomHatches = dvp->getGeomHatches();
    uint32_t index = 0;
    for (const TechDraw::FacePtr& face : dvp->getFaceGeometry()) {
        Page2D::Recorder rec;
        if ((faceColor & 0xff) != 0)
            emitFace(rec, face, ox, oy, style.deflection, faceColor);
        out.setItem(itemId(name, 'f', index), Page2D::Kind::Face, layer,
                    std::move(rec));

        Page2D::Recorder hrec;
        if (TechDraw::DrawGeomHatch* gh =
                geomHatchForFace((int)index, geomHatches)) {
            uint32_t hatchColor = 0x000000ff;
            float hatchWidth = 1.0f;
            if (auto ghvp = gdoc ? dynamic_cast<ViewProviderGeomHatch*>(
                                       gdoc->getViewProvider(gh))
                                 : nullptr) {
                hatchColor = packColor(
                    TechDraw::Preferences::getAccessibleColor(
                        ghvp->ColorPattern.getValue())
                        .asValue<QColor>());
                hatchWidth = (float)Rez::guiX(ghvp->WeightPattern.getValue());
            }
            hrec.beginPath();
            bool any = false;
            for (TechDraw::LineSet& ls : gh->getTrimmedLines((int)index)) {
                for (const TechDraw::BaseGeomPtr& g : ls.getGeoms()) {
                    const Base::Vector3d s = g->getStartPoint();
                    const Base::Vector3d e = g->getEndPoint();
                    hrec.moveTo(ox + (float)Rez::guiX(s.x),
                                oy + (float)Rez::guiX(s.y));
                    hrec.lineTo(ox + (float)Rez::guiX(e.x),
                                oy + (float)Rez::guiX(e.y));
                    any = true;
                }
            }
            if (any && hatchWidth > 0.0f)
                hrec.stroke(hatchColor, hatchWidth);
        }
        out.setItem(itemId(name, 'h', index), Page2D::Kind::Decoration,
                    layer, std::move(hrec));
        ++index;
    }
    sweep('f', index);
    sweep('h', index);

    index = 0;
    int iEdge = 0;
    for (const TechDraw::BaseGeomPtr& geom : dvp->getEdgeGeometry()) {
        Page2D::Recorder rec;
        EdgeStroke es = resolveEdgeStroke(dvp, vp, geom, iEdge, style);
        if (es.show && es.width > 0.0f) {
            if (es.dashes.empty()) {
                if (emitEdge(rec, geom, ox, oy, style.deflection))
                    rec.stroke(es.color, es.width);
            }
            else {
                // A dashed stroke flattens first: the dash walker needs
                // arc length, and vg has no dashing of its own.
                std::vector<Pt> pts;
                discretize(geom, style.deflection, pts);
                if (pts.size() >= 2) {
                    rec.beginPath();
                    emitDashedPolyline(rec, pts, ox, oy, es.dashes);
                    rec.stroke(es.color, es.width);
                }
            }
        }
        out.setItem(itemId(name, 'e', index), Page2D::Kind::Edge, layer,
                    std::move(rec));
        ++index;
        ++iEdge;
    }
    sweep('e', index);

    // Vertex dots and arc center marks (QGIViewPart::drawAllVertexes):
    // radius LineWidth * VertexScale, never in CoarseView or with
    // frames off; center marks are crosses, gated by ArcCenterMarks.
    const double vertexScale = TechDraw::Preferences::getPreferenceGroup(
        "General")->GetFloat("VertexScale", 3.0);
    const double lineWidthMm = vp ? vp->LineWidth.getValue() : 0.42;
    const bool showVerts = !dvp->CoarseView.getValue() && frames;
    const bool showCenters = vp && vp->ArcCenterMarks.getValue();
    const uint32_t vertexColor = vp
        ? packColor(PreferencesGui::getAccessibleQColor(
              PreferencesGui::vertexQColor()))
        : style.vertexColor;
    index = 0;
    for (const TechDraw::VertexPtr& vert : dvp->getVertexGeometry()) {
        Page2D::Recorder rec;
        const float vx = ox + (float)Rez::guiX(vert->x());
        const float vy = oy + (float)Rez::guiX(vert->y());
        if (vert->isCenter()) {
            if (showCenters) {
                const float half = (float)Rez::guiX(
                    lineWidthMm * vertexScale * vp->CenterScale.getValue()
                    / 2.0);
                rec.beginPath();
                rec.moveTo(vx - half, vy);
                rec.lineTo(vx + half, vy);
                rec.moveTo(vx, vy - half);
                rec.lineTo(vx, vy + half);
                rec.stroke(vertexColor, (float)Rez::guiX(lineWidthMm * 0.5));
            }
        }
        else if (showVerts && !vert->isReference()) {
            rec.beginPath();
            rec.circle(vx, vy,
                       vp ? (float)Rez::guiX(lineWidthMm * vertexScale)
                          : style.vertexRadius);
            rec.fillConvex(vertexColor);
        }
        out.setItem(itemId(name, 'v', index), Page2D::Kind::Vertex, layer,
                    std::move(rec));
        ++index;
    }
    sweep('v', index);
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
