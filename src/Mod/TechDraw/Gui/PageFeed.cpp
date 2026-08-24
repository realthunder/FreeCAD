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
#include <algorithm>
#include <cmath>
#endif

#include <BRepAdaptor_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>

#include <QAbstractGraphicsShapeItem>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QPainterPath>
#include <QPen>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTransform>

#include <App/Application.h>
#include <Base/Console.h>
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
#include "QGIDecoration.h"
#include "QGIPrimPath.h"
#include "QGIView.h"
#include "QGSPage.h"
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

// ---- Qt scene capture: the annotation tier ------------------------------

// FC_PAGE2D_TRACE=1 logs what the capture walker sees per item --
// the diagnosis knob for "this view has no ink".
bool traceCapture()
{
    static bool on = [] {
        const char* e = getenv("FC_PAGE2D_TRACE");
        return e && *e && strcmp(e, "0") != 0;
    }();
    return on;
}
//
// Dimensions, balloons, annotations and leaders are laid out by the Qt
// tier (QGIViewDimension alone is 2700 lines of ISO/ASME placement); the
// feed does not duplicate that. It converts the laid-out QGraphicsItem
// subtree: shape items to path ops, text items to fontstash text runs.

// Register the fonts TechDraw ships (the same files loadTechDrawResource
// hands to Qt) with the 2D engine, keyed so vgFontKey below finds them.
void ensureVgFonts()
{
    static bool once = [] {
        const std::string dir = App::Application::getResourceDir()
            + "Mod/TechDraw/Resources/fonts/";
        Page2D::registerFont("osifont", (dir + "osifont-lgpl3fe.ttf").c_str());
        Page2D::registerFont("osifont#italic",
                             (dir + "osifont-italic.ttf").c_str());
        Page2D::registerFont("y14.5-2009", (dir + "Y14.5-2018.ttf").c_str());
        Page2D::registerFont("y14.5-freecad",
                             (dir + "Y14.5-FreeCAD.ttf").c_str());
        return true;
    }();
    (void)once;
}

// Map a Qt font to a registered engine font. Only the shipped TechDraw
// fonts exist engine-side for now; any other family falls back to
// osifont (TechDraw's default), bold has no variant, and italic only
// one for osifont.
const char* vgFontKey(const QFont& font)
{
    const QString family = font.family().toLower();
    if (family.startsWith(QLatin1String("y14.5-freecad")))
        return "y14.5-freecad";
    if (family.startsWith(QLatin1String("y14.5")))
        return "y14.5-2009";
    return font.italic() ? "osifont#italic" : "osifont";
}

float avgScale(const QTransform& t)
{
    const double det = std::abs(t.determinant());
    return det > 0.0 ? (float)std::sqrt(det) : 1.0f;
}

// A pen's stroke width in page units. Width 0 is Qt's cosmetic
// hairline, drawn one device pixel at any zoom -- a concept a retained
// page has no analog for; the ISO 0.35mm standard line reads the same
// at page scale (balloon leaders and bubbles arrive this way).
float penWidth(const QPen& pen, const QTransform& t)
{
    const double w =
        pen.widthF() > 0.0 ? pen.widthF() : Rez::guiX(0.35);
    return (float)w * avgScale(t);
}

// Replay a QPainterPath's elements into the recorder, mapped to page
// coordinates. vg keeps the path current across fill and stroke, so
// one capture serves both.
void capturePainterPath(Page2D::Recorder& rec, const QPainterPath& path,
                        const QTransform& t)
{
    rec.beginPath();
    const int n = path.elementCount();
    for (int i = 0; i < n; ++i) {
        const QPainterPath::Element el = path.elementAt(i);
        const QPointF p = t.map(QPointF(el.x, el.y));
        switch (el.type) {
        case QPainterPath::MoveToElement:
            rec.moveTo((float)p.x(), (float)p.y());
            break;
        case QPainterPath::LineToElement:
            rec.lineTo((float)p.x(), (float)p.y());
            break;
        case QPainterPath::CurveToElement: {
            if (i + 2 >= n)
                return;
            const QPainterPath::Element e1 = path.elementAt(i + 1);
            const QPainterPath::Element e2 = path.elementAt(i + 2);
            const QPointF c2 = t.map(QPointF(e1.x, e1.y));
            const QPointF end = t.map(QPointF(e2.x, e2.y));
            rec.cubicTo((float)p.x(), (float)p.y(), (float)c2.x(),
                        (float)c2.y(), (float)end.x(), (float)end.y());
            i += 2;
            break;
        }
        default:
            break;
        }
    }
}

// Qt dash patterns are specified in pen-width units.
std::vector<float> penDashes(const QPen& pen, const QTransform& t)
{
    std::vector<float> dashes;
    if (pen.style() == Qt::SolidLine || pen.style() == Qt::NoPen)
        return dashes;
    const float w = penWidth(pen, t);
    for (qreal d : pen.dashPattern())
        dashes.push_back((float)d * w);
    return dashes;
}

void captureDashedPath(Page2D::Recorder& rec, const QPainterPath& path,
                       const QTransform& t, const std::vector<float>& dashes)
{
    rec.beginPath();
    for (const QPolygonF& poly : path.toSubpathPolygons(t)) {
        std::vector<Pt> pts;
        pts.reserve(poly.size());
        for (const QPointF& p : poly)
            pts.push_back({(float)p.x(), (float)p.y()});
        emitDashedPolyline(rec, pts, 0.0f, 0.0f, dashes);
    }
}

void emitStyledPath(Page2D::Recorder& rec, const QPainterPath& path,
                    const QTransform& t, const QPen& pen, const QBrush& brush)
{
    if (path.isEmpty())
        return;
    const bool fill = brush.style() != Qt::NoBrush && brush.color().alpha();
    const bool stroke = pen.style() != Qt::NoPen && pen.color().alpha();
    if (!fill && !stroke)
        return;

    const std::vector<float> dashes = penDashes(pen, t);
    if (fill || dashes.empty())
        capturePainterPath(rec, path, t);
    if (fill)
        rec.fillConcave(packColor(brush.color()),
                        path.fillRule() == Qt::OddEvenFill);
    if (stroke) {
        if (!dashes.empty())
            captureDashedPath(rec, path, t, dashes);
        rec.stroke(packColor(pen.color()), penWidth(pen, t));
    }
}

// TechDraw's own path items (edges, dimension lines, arrows, section
// marks, ...) derive from QGIPrimPath, which draws from its own
// pen/brush members on a plain QGraphicsItem.
void capturePrimPath(Page2D::Recorder& rec, QGIPrimPath* item)
{
    emitStyledPath(rec, item->path(), item->sceneTransform(),
                   item->currentPen(), item->currentBrush());
}

void captureShapeItem(Page2D::Recorder& rec, QAbstractGraphicsShapeItem* item)
{
    QPainterPath path;
    if (auto pathItem = dynamic_cast<QGraphicsPathItem*>(item))
        path = pathItem->path();
    else if (auto ellipse = dynamic_cast<QGraphicsEllipseItem*>(item)) {
        if (ellipse->startAngle() == 0 && ellipse->spanAngle() == 360 * 16)
            path.addEllipse(ellipse->rect());
        else {
            path.arcMoveTo(ellipse->rect(), ellipse->startAngle() / 16.0);
            path.arcTo(ellipse->rect(), ellipse->startAngle() / 16.0,
                       ellipse->spanAngle() / 16.0);
        }
    }
    else if (auto rect = dynamic_cast<QGraphicsRectItem*>(item))
        path.addRect(rect->rect());
    else if (auto poly = dynamic_cast<QGraphicsPolygonItem*>(item))
        path.addPolygon(poly->polygon());
    else
        return;
    emitStyledPath(rec, path, item->sceneTransform(), item->pen(),
                   item->brush());
}

void captureLineItem(Page2D::Recorder& rec, QGraphicsLineItem* item)
{
    const QPen pen = item->pen();
    if (pen.style() == Qt::NoPen || !pen.color().alpha())
        return;
    const QTransform t = item->sceneTransform();
    const QPointF p1 = t.map(item->line().p1());
    const QPointF p2 = t.map(item->line().p2());
    rec.beginPath();
    const std::vector<float> dashes = penDashes(pen, t);
    if (dashes.empty()) {
        rec.moveTo((float)p1.x(), (float)p1.y());
        rec.lineTo((float)p2.x(), (float)p2.y());
    }
    else {
        std::vector<Pt> pts {{(float)p1.x(), (float)p1.y()},
                             {(float)p2.x(), (float)p2.y()}};
        emitDashedPolyline(rec, pts, 0.0f, 0.0f, dashes);
    }
    rec.stroke(packColor(pen.color()), penWidth(pen, t));
}

// Text: one run per (line x fragment) of the laid-out document, at its
// baseline, in the fragment's own font and color -- this keeps rich
// text (annotations are HTML) faithful without rendering HTML engine-
// side. A rotated or scaled item wraps its runs in a transform op; the
// common upright label goes out pre-mapped.
void captureTextItem(Page2D::Recorder& rec, QGraphicsTextItem* item)
{
    QTextDocument* doc = item->document();
    if (!doc)
        return;
    const QTransform t = item->sceneTransform();
    const bool upright = t.type() <= QTransform::TxTranslate;
    bool pushed = false;
    for (QTextBlock block = doc->begin(); block != doc->end();
         block = block.next()) {
        QTextLayout* layout = block.layout();
        if (!layout)
            continue;
        const QPointF blockPos = layout->position();
        const QString blockText = block.text();
        for (int li = 0; li < layout->lineCount(); ++li) {
            const QTextLine line = layout->lineAt(li);
            if (!line.isValid())
                continue;
            const int lineStart = line.textStart();
            const int lineEnd = lineStart + line.textLength();
            for (QTextBlock::iterator fi = block.begin(); !fi.atEnd();
                 ++fi) {
                const QTextFragment frag = fi.fragment();
                if (!frag.isValid())
                    continue;
                const int fragStart = frag.position() - block.position();
                const int start = std::max(fragStart, lineStart);
                const int end =
                    std::min(fragStart + frag.length(), lineEnd);
                if (start >= end)
                    continue;
                const QString run = blockText.mid(start, end - start);
                if (run.trimmed().isEmpty())
                    continue;
                // The fragment format carries only what HTML/CSS set
                // explicitly; a plain-text item's font lives on the
                // item. Merge: item font under, set properties over.
                const QTextCharFormat cf = frag.charFormat();
                QFont font = item->font();
                const QFont cff = cf.font();
                if (cf.hasProperty(QTextFormat::FontFamilies))
                    font.setFamily(cff.family());
                if (cf.hasProperty(QTextFormat::FontItalic))
                    font.setItalic(cff.italic());
                if (cf.hasProperty(QTextFormat::FontPixelSize))
                    font.setPixelSize(cff.pixelSize());
                else if (cf.hasProperty(QTextFormat::FontPointSize))
                    font.setPointSizeF(cff.pointSizeF());
                float size = (float)font.pixelSize();
                if (size <= 0.0f && font.pointSizeF() > 0.0)
                    size = (float)(font.pointSizeF() * 96.0 / 72.0);
                if (size <= 0.0f)
                    continue;
                // Qt's pixel size sets the em square; fontstash (stb)
                // scales so ascent-descent equals the size. Hand vg the
                // Qt line height so glyphs come out Qt-sized.
                {
                    const QFontMetricsF fm(font);
                    const double lineH = fm.ascent() + fm.descent();
                    if (lineH > 0.0)
                        size = (float)lineH;
                }
                const QColor color = cf.foreground().style() != Qt::NoBrush
                    ? cf.foreground().color()
                    : item->defaultTextColor();
                const float x =
                    (float)(blockPos.x() + line.cursorToX(start));
                const float y =
                    (float)(blockPos.y() + line.y() + line.ascent());
                const QByteArray utf8 = run.toUtf8();
                if (traceCapture())
                    Base::Console().Message(
                        "PageFeed:     text run %s size=%.1f at %.1f,%.1f "
                        "upright=%d font=%s\n",
                        utf8.constData(), size, x, y, upright ? 1 : 0,
                        vgFontKey(font));
                if (upright) {
                    rec.text(vgFontKey(font), size, packColor(color),
                             x + (float)t.dx(), y + (float)t.dy(),
                             utf8.constData());
                }
                else {
                    if (!pushed) {
                        const float mtx[6] = {
                            (float)t.m11(), (float)t.m12(), (float)t.m21(),
                            (float)t.m22(), (float)t.dx(), (float)t.dy()};
                        rec.pushTransform(mtx);
                        pushed = true;
                    }
                    rec.text(vgFontKey(font), size, packColor(color), x, y,
                             utf8.constData());
                }
            }
        }
    }
    if (pushed)
        rec.popTransform();
}

// Depth-first over the item subtree in paint order (parent under
// children, siblings by z-value). Nested QGIViews are other document
// objects: they feed under their own ids. Visibility is judged
// relative to the captured root (isVisibleTo), never absolutely: a
// host that photographs the vg layer alone hides the Qt items, and
// that must not read as "the drawing is empty".
void captureItemTree(QGraphicsItem* item, Page2D::Recorder& rec,
                     const QGraphicsItem* root)
{
    if (!item)
        return;
    if (item != root) {
        if (!item->isVisibleTo(root)) {
            if (traceCapture())
                Base::Console().Message("PageFeed:   skip hidden %s\n",
                                        typeid(*item).name());
            return;
        }
        if (dynamic_cast<QGIView*>(item))
            return;
    }
    if (traceCapture()) {
        const QRectF r = item->sceneBoundingRect();
        Base::Console().Message(
            "PageFeed:   item %s rect(%.0f,%.0f %.0fx%.0f) bytes=%zu\n",
            typeid(*item).name(), r.x(), r.y(), r.width(), r.height(),
            rec.bytes().size());
    }
    if (auto text = dynamic_cast<QGraphicsTextItem*>(item))
        captureTextItem(rec, text);
    else if (auto prim = dynamic_cast<QGIPrimPath*>(item))
        capturePrimPath(rec, prim);
    else if (auto shape = dynamic_cast<QAbstractGraphicsShapeItem*>(item))
        captureShapeItem(rec, shape);
    else if (auto line = dynamic_cast<QGraphicsLineItem*>(item))
        captureLineItem(rec, line);
    QList<QGraphicsItem*> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](const QGraphicsItem* a, const QGraphicsItem* b) {
                         return a->zValue() < b->zValue();
                     });
    for (QGraphicsItem* child : children)
        captureItemTree(child, rec, root);
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

void PageFeed::feedViewCapture(QGIView* qgiv, Page2D& out, uint32_t layer)
{
    if (!qgiv)
        return;
    TechDraw::DrawView* feature = qgiv->getViewObject();
    const char* name = feature ? feature->getNameInDocument() : nullptr;
    if (!name)
        return;
    ensureVgFonts();
    Page2D::Recorder rec;
    if (traceCapture()) {
        QGraphicsItem* parent = qgiv->parentItem();
        Base::Console().Message(
            "PageFeed: capture %s featVis=%d itemVis=%d parent=%s "
            "pos=%.0f,%.0f\n",
            name, qgiv->isVisible() ? 1 : 0,
            static_cast<QGraphicsItem*>(qgiv)->isVisible() ? 1 : 0,
            parent ? typeid(*parent).name() : "none", qgiv->pos().x(),
            qgiv->pos().y());
    }
    if (qgiv->isVisible())
        captureItemTree(qgiv, rec, qgiv);
    if (traceCapture())
        Base::Console().Message("PageFeed: capture %s -> %zu bytes\n", name,
                                rec.bytes().size());
    out.setItem(itemId(name, 'a', 0), Page2D::Kind::Annotation, layer,
                std::move(rec));
}

void PageFeed::feedViewDecorations(QGIView* qgiv, Page2D& out, uint32_t layer)
{
    if (!qgiv)
        return;
    TechDraw::DrawView* feature = qgiv->getViewObject();
    const char* name = feature ? feature->getNameInDocument() : nullptr;
    if (!name)
        return;
    ensureVgFonts();
    Page2D::Recorder rec;
    if (qgiv->isVisible()) {
        for (QGraphicsItem* child : qgiv->childItems()) {
            if (dynamic_cast<QGIDecoration*>(child))
                captureItemTree(child, rec, qgiv);
        }
    }
    out.setItem(itemId(name, 'd', 0), Page2D::Kind::Decoration, layer,
                std::move(rec));
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
    // The annotation tier converts the laid-out Qt scene items; resolve
    // the page's scene where the Gui side has one (it exists for every
    // page of a Gui document, shown or not).
    QGSPage* qgs = nullptr;
    Gui::Document* gdoc =
        Gui::Application::Instance->getDocument(page->getDocument());
    if (gdoc) {
        if (auto vpp =
                dynamic_cast<ViewProviderPage*>(gdoc->getViewProvider(page)))
            qgs = vpp->getQGSPage();
    }
    uint32_t layer = 0;
    for (App::DocumentObject* obj : page->getAllViews()) {
        QGIView* qgiv = qgs ? qgs->findQViewForDocObj(obj) : nullptr;
        if (auto dvp = dynamic_cast<TechDraw::DrawViewPart*>(obj)) {
            feedViewPart(dvp, out, style, layer);
            if (qgiv)
                feedViewDecorations(qgiv, out, layer);
        }
        else if (qgiv)
            feedViewCapture(qgiv, out, layer);
        ++layer;
    }
}
