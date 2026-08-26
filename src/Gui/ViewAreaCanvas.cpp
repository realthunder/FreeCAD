/***************************************************************************
 *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <sstream>
# include <QApplication>
# include <QKeyEvent>
# include <QLayout>
# include <QMouseEvent>
# include <QOpenGLContext>
# include <QOpenGLFunctions>
# include <QWheelEvent>
# include <Inventor/SbViewVolume.h>
# include <Inventor/SbViewportRegion.h>
# include <Inventor/SoRenderManager.h>
# include <Inventor/nodes/SoCamera.h>
#endif

#include <Base/Console.h>

#include "ViewAreaCanvas.h"
#include "RenderParams.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewArea.h"
#include "ViewParams.h"

FC_LOG_LEVEL_INIT("ViewArea", true, true)

using namespace Gui;

namespace {

/// A surface for the canvas that can hold what the Coin residue needs
/// on top of the backend blit: a depth buffer to test against, and the
/// same sample count a plain 3D view would have asked for.
QSurfaceFormat canvasFormat()
{
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    if (fmt.depthBufferSize() < 24)
        fmt.setDepthBufferSize(24);
    if (fmt.stencilBufferSize() < 8)
        fmt.setStencilBufferSize(8);
    const int samples = View3DInventorViewer::getNumSamples();
    if (samples > 1)
        fmt.setSamples(samples);
    return fmt;
}

} // anonymous namespace

ViewAreaCanvas::ViewAreaCanvas(ViewArea *area)
    : QOpenGLWidget(area)
    , _area(area)
{
    setObjectName(QStringLiteral("ViewAreaCanvas"));
    setFormat(canvasFormat());
    // Input belongs to the cells stacked above; the canvas is a
    // surface, not a target.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setFocusPolicy(Qt::NoFocus);
}

ViewAreaCanvas::~ViewAreaCanvas()
{
    releaseAll();
}

bool ViewAreaCanvas::wanted()
{
    if (!ViewParams::getUnifiedCanvas())
        return false;
    // The canvas composites what a backend renders; without the render
    // engine there is nothing for it to draw and the cells must keep
    // their own GL widgets.
    return ViewParams::getRenderCache() == 3
        && !RenderParams::getType().empty()
        && RenderParams::getType() != "Default";
}

View3DInventorViewer *ViewAreaCanvas::viewerOf(const ViewAreaCell *cell)
{
    if (!cell)
        return nullptr;
    auto view = qobject_cast<View3DInventor*>(cell->childView());
    return view ? view->getViewer() : nullptr;
}

bool ViewAreaCanvas::claimable(const ViewAreaCell *cell) const
{
    auto viewer = viewerOf(cell);
    if (!viewer)
        return false;
    // A maximized layout hides every other tile; a hidden cell has no
    // rect to draw into, and the one left has nothing to share.
    if (!cell->isVisibleTo(_area))
        return false;
    // One canvas draws ONE resident scene (the browser tier's rule,
    // sec 9.1): a cell showing another document would need a feed of
    // its own, and two feeds at one backend overwrite each other. It
    // keeps its own widget composition instead.
    MDIView *view = cell->childView();
    return view->getGuiDocument() == _area->getGuiDocument();
}

bool ViewAreaCanvas::claims(const ViewAreaCell *cell) const
{
    for (const auto &c : _cells) {
        if (c.cell == cell)
            return true;
    }
    return false;
}

int ViewAreaCanvas::claimId(const ViewAreaCell *cell) const
{
    for (const auto &c : _cells) {
        if (c.cell == cell)
            return c.id;
    }
    return 0;
}

QRect ViewAreaCanvas::cellRect(const ViewAreaCell *cell) const
{
    if (!cell)
        return {};
    const QPoint tl = cell->mapTo(parentWidget(), QPoint(0, 0)) - pos();
    const qreal dpr = devicePixelRatioF();
    return QRect(int(tl.x() * dpr + 0.5), int(tl.y() * dpr + 0.5),
                 int(cell->width() * dpr + 0.5),
                 int(cell->height() * dpr + 0.5));
}

void ViewAreaCanvas::claim(ViewAreaCell *cell, int id)
{
    MDIView *view = cell->childView();
    auto viewer = viewerOf(cell);
    if (!view || !viewer)
        return;

    // Out of the layout but still a child of the cell: hidden widgets
    // get no space from a layout, and the child has to keep the cell's
    // size or every coordinate the forwarded input carries would be
    // wrong. Not reparented -- ViewAreaCell::childEvent reads a
    // reparent as the view being torn away and collapses the tile.
    if (QLayout *lay = cell->layout())
        lay->removeWidget(view);
    view->setGeometry(cell->rect());
    view->hide();

    // The cell now shows the canvas through itself, and takes the input
    // its hidden child can no longer receive.
    cell->setAttribute(Qt::WA_NoSystemBackground, true);
    cell->setAutoFillBackground(false);
    cell->setFocusPolicy(Qt::StrongFocus);
    cell->installEventFilter(this);

    viewer->adoptRenderer(_renderer, false, id);
    QPointer<ViewAreaCanvas> self(this);
    viewer->setRedrawRedirect([self](bool force) {
        if (!self)
            return;
        if (force)
            self->repaint();
        else
            self->update();
    });

    Claim c;
    c.cell = cell;
    c.child = view;
    c.id = id;
    _cells.push_back(c);
}

void ViewAreaCanvas::release(ViewAreaCell *cell, bool restoreBackend)
{
    if (!cell)
        return;
    cell->removeEventFilter(this);
    cell->setAttribute(Qt::WA_NoSystemBackground, false);
    cell->setFocusPolicy(Qt::NoFocus);
    if (_feeder == cell)
        _feeder = nullptr;
    MDIView *view = cell->childView();
    if (!view)
        return;
    if (auto viewer = viewerOf(cell)) {
        viewer->setRedrawRedirect({});
        if (restoreBackend) {
            viewer->adoptRenderer({}, false);
        }
        else {
            // Teardown: detach only. Handing the viewer a backend of
            // its own here would create one per cell and destroy it
            // with the widget a moment later.
            viewer->setRendererType(std::string());
        }
    }
    if (QLayout *lay = cell->layout())
        lay->addWidget(view);
    view->show();
}

void ViewAreaCanvas::releaseAll(bool restoreBackends)
{
    auto cells = _cells;
    _cells.clear();
    _feeder = nullptr;
    for (auto &c : cells)
        release(c.cell, restoreBackends);
    _renderer.reset();
}

void ViewAreaCanvas::releaseCell(ViewAreaCell *cell)
{
    for (size_t i = 0; i < _cells.size(); ++i) {
        if (_cells[i].cell != cell)
            continue;
        if (_renderer)
            _renderer->dropSubView(_cells[i].id);
        _cells.erase(_cells.begin() + long(i));
        release(cell);
        return;
    }
}

void ViewAreaCanvas::setFeeder(ViewAreaCell *cell)
{
    if (_feeder == cell)
        return;
    // Order matters: the old feed goes first, or the backend would
    // briefly hold two managers' claims on one scene.
    if (auto old = viewerOf(_feeder))
        old->adoptRenderer(_renderer, false, claimId(_feeder));
    _feeder = cell;
    if (auto viewer = viewerOf(cell))
        viewer->adoptRenderer(_renderer, true, claimId(cell));
}

void ViewAreaCanvas::sync()
{
    // Re-entrancy guard. claim() pulls a widget out of a layout, hides
    // it, installs an event filter and adopts a backend -- any of which
    // can deliver a child or activation event that lands back in
    // ViewArea::setActiveCell, which calls sync(). The nested run got
    // there BEFORE claim() recorded its entry (the push_back is its
    // last statement), saw the cell as unclaimed and claimed it a
    // SECOND time under a second id.
    //
    // Two entries for one cell meant the canvas drew a phantom third
    // sub-view; and because a viewer carries only the id of its LAST
    // adoption, that cell fed its chrome under one id while its phantom
    // bank rendered under the other. Invisible until D3c, when the id
    // started deciding CONTENT and not just which bank -- the cell drew
    // no NaviCube and no axis cross at all (docs/SplitViews.md 16.3).
    //
    // Dropped rather than queued would lose a real layout change, so a
    // nested call is remembered and replayed once the outer one is done.
    if (_syncing) {
        _syncAgain = true;
        return;
    }
    _syncing = true;
    syncOnce();
    _syncing = false;
    if (_syncAgain) {
        _syncAgain = false;
        sync();
    }
}

void ViewAreaCanvas::syncOnce()
{
    if (!_area)
        return;
    if (!wanted()) {
        releaseAll();
        hide();
        return;
    }

    // Claim set first: whatever the layout now offers, minus what the
    // canvas cannot draw.
    std::vector<ViewAreaCell*> want;
    for (auto cell : _area->cells()) {
        if (claimable(cell))
            want.push_back(cell);
    }
    // A lone 3D cell has nothing to share and gains nothing from the
    // canvas -- leave it on the plain path, which is also what keeps an
    // unsplit view byte-identical to before.
    if (want.size() < 2) {
        releaseAll();
        hide();
        return;
    }

    if (!_renderer) {
        Render::RendererFactory::setMaxViewIds(int(RenderParams::getMaxViewIds()));
        _renderer = Render::RendererFactory::create(RenderParams::getType(), this);
        if (!_renderer) {
            hide();
            return;
        }
    }

    // Drop the cells that went away or stopped being claimable, then
    // add the new ones. Ids are never reused inside a canvas: a bank
    // carries temporal state, and handing a fresh cell a retired id
    // would resume the vanished one's accumulation.
    for (size_t i = _cells.size(); i-- > 0;) {
        ViewAreaCell *cell = _cells[i].cell;
        const bool keep = cell
            && _cells[i].child == cell->childView()
            && std::find(want.begin(), want.end(), cell) != want.end();
        if (keep)
            continue;
        if (_renderer)
            _renderer->dropSubView(_cells[i].id);
        release(cell);
        _cells.erase(_cells.begin() + long(i));
    }
    for (auto cell : want) {
        if (!claims(cell))
            claim(cell, _nextId++);
    }

    // The feed follows the active cell so the Coin residue -- draggers
    // above all -- lands where the user is working. Moving it restates
    // the scene from the render caches the new feeder already holds:
    // a re-translation, no traversal (SoFCRenderer::feedExternal).
    ViewAreaCell *active = _area->activeCell();
    if (!active || !claims(active))
        active = _cells.empty() ? nullptr : _cells.front().cell.data();
    setFeeder(active);

    // Keep the hidden children on their tiles.
    for (auto &c : _cells) {
        if (c.cell && c.cell->childView())
            c.cell->childView()->setGeometry(c.cell->rect());
    }

    show();
    lower();
    update();
}

void ViewAreaCanvas::paintGL()
{
    if (_painting)
        return;
    if (!_renderer || _cells.empty())
        return;
    auto feeder = viewerOf(_feeder);
    if (!feeder)
        return;

    _painting = true;

    // The camera matrices must outlive the SubViewFrame array that
    // points into them; reserved up front so no push_back moves one.
    std::vector<SbMatrix> mats;
    mats.reserve(_cells.size() * 2);
    std::vector<Render::Renderer::SubViewFrame> subs;
    subs.reserve(_cells.size());
    std::vector<QRect> rects;
    rects.reserve(_cells.size());
    // Parallel to subs/rects: a cell whose camera is missing is skipped,
    // so the sub index is NOT the claim index.
    std::vector<ViewAreaCell*> drawnCells;
    drawnCells.reserve(_cells.size());

    for (auto &c : _cells) {
        auto viewer = viewerOf(c.cell);
        if (!viewer)
            continue;
        SoCamera *cam = viewer->getSoRenderManager()->getCamera();
        const QRect r = cellRect(c.cell);
        if (!cam || r.width() <= 0 || r.height() <= 0)
            continue;
        // The camera is read against the CELL's aspect, not the
        // canvas's: each sub-view is a viewport of its own.
        SbViewportRegion vp(short(r.width()), short(r.height()));
        SbViewVolume vol = cam->getViewVolume(vp.getViewportAspectRatio());
        mats.emplace_back();
        mats.emplace_back();
        SbMatrix &viewMat = mats[mats.size() - 2];
        SbMatrix &projMat = mats[mats.size() - 1];
        vol.getMatrices(viewMat, projMat);

        // This cell's own chrome -- its NaviCube, its corner axis cross
        // -- scoped to its bank. Only the FEEDER paints (renderScene is
        // what normally drives these), so without this every cell would
        // draw the feeder's cube turned by the feeder's camera
        // (docs/SplitViews.md sec 16.3). Done before the frame, so all
        // the feeds are resident by the time any sub-view renders.
        viewer->updateCanvasOverlays();

        Render::Renderer::SubViewFrame s;
        s.id = c.id;
        s.x = r.x();
        s.y = r.y();
        s.width = r.width();
        s.height = r.height();
        s.viewMatrix = &viewMat.getValue();
        s.projMatrix = &projMat.getValue();
        // ...and this cell's own display style. Every cell is a real
        // viewer with its own override mode, which until now could not
        // show: only the feeder traverses, so a non-feeding cell's
        // style change reached nothing. As a backend bucket filter it
        // costs the frame nothing and differs per bank for free
        // (docs/CoinRetirement.md 5.7).
        s.drawStyle = viewer->drawStyleMask();
        subs.push_back(s);
        rects.push_back(r);
        drawnCells.push_back(c.cell);
    }

    if (FC_LOG_INSTANCE.isEnabled(FC_LOGLEVEL_TRACE)) {
        std::ostringstream ids;
        for (const auto &c : _cells)
            ids << ' ' << c.id << (c.cell == _feeder ? "*" : "");
        FC_TRACE("canvas frame: claims" << ids.str() << ", drawing "
                 << subs.size() << " sub-views");
    }

    bool drawn = false;
    QColor col = feeder->feedRendererBackground();
    if (!subs.empty()) {
        // Built up front so the frame itself allocates nothing: a fresh
        // bank's target set created mid-frame can exhaust the handle
        // pool and render its cell black (docs/SplitViews.md sec 11.1).
        _renderer->prepareSubViews(col, subs.data(), int(subs.size()));
        drawn = _renderer->renderSubViews(col, subs.data(), int(subs.size()));
    }
    if (!drawn) {
        auto *f = QOpenGLContext::currentContext()->functions();
        f->glClearColor(col.redF(), col.greenF(), col.blueF(), 1.0F);
        f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // The Coin residue of the cell that feeds: draggers and whatever
    // else the backend does not claim, composited into its own rect and
    // depth-tested against the depth the blit brought along. Only the
    // feeder -- the other cells' render-cache managers are detached, so
    // their traversal would draw the whole scene in fixed-function GL
    // (sec 13.4; per-cell chrome is D3).
    for (size_t i = 0; i < subs.size(); ++i) {
        if (drawnCells[i] != _feeder)
            continue;
        const QRect &r = rects[i];
        // GL counts from the bottom; the sub rects are stated top-left
        // to match the blit.
        const int canvasH = int(height() * devicePixelRatioF() + 0.5);
        const int glY = canvasH - r.y() - r.height();
        feeder->renderCanvasResidue(SbVec2s(short(r.x()), short(glY)),
                                    SbVec2s(short(r.width()), short(r.height())),
                                    drawn);
    }

    _painting = false;

    // Same rule as renderScene(): the feed happens during the traversal
    // above, so anything new there reaches the backend only in the next
    // frame. Ask for it.
    if (_renderer->needsRedraw() || _renderer->animating())
        update();
}

ViewAreaCell *ViewAreaCanvas::cellAt(const QPoint &globalPos) const
{
    for (const auto &c : _cells) {
        if (!c.cell)
            continue;
        const QPoint local = c.cell->mapFromGlobal(globalPos);
        if (c.cell->rect().contains(local))
            return c.cell;
    }
    return nullptr;
}

/// Scope guard for the forwarding re-entrancy flag.
namespace {
struct FlagScope {
    explicit FlagScope(bool &f) : flag(f) { flag = true; }
    ~FlagScope() { flag = false; }
    bool &flag;
};
} // anonymous namespace

bool ViewAreaCanvas::forwardEvent(ViewAreaCell *cell, QEvent *event)
{
    auto viewer = viewerOf(cell);
    if (!viewer)
        return false;
    QWidget *target = viewer->getGLWidget();
    if (!target)
        target = viewer;
    FlagScope guard(_forwarding);

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent*>(event);
        const QPointF global = me->globalPosition();
        const QPointF local = target->mapFromGlobal(global.toPoint());
        QMouseEvent copy(me->type(), local, global, me->button(),
                         me->buttons(), me->modifiers());
        const bool res = QApplication::sendEvent(target, &copy);
        event->setAccepted(copy.isAccepted());
        FC_TRACE("canvas forward mouse " << int(me->type()) << " to "
                 << local.x() << "," << local.y() << " res " << res
                 << " accepted " << copy.isAccepted());
        return res && copy.isAccepted();
    }
    case QEvent::Wheel: {
        auto *we = static_cast<QWheelEvent*>(event);
        const QPointF global = we->globalPosition();
        const QPointF local = target->mapFromGlobal(global.toPoint());
        QWheelEvent copy(local, global, we->pixelDelta(), we->angleDelta(),
                         we->buttons(), we->modifiers(), we->phase(),
                         we->inverted());
        const bool res = QApplication::sendEvent(target, &copy);
        event->setAccepted(copy.isAccepted());
        FC_TRACE("canvas forward wheel to " << local.x() << "," << local.y()
                 << " res " << res << " accepted " << copy.isAccepted());
        return res && copy.isAccepted();
    }
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto *ke = static_cast<QKeyEvent*>(event);
        QKeyEvent copy(ke->type(), ke->key(), ke->modifiers(), ke->text(),
                       ke->isAutoRepeat(), ushort(ke->count()));
        const bool res = QApplication::sendEvent(target, &copy);
        event->setAccepted(copy.isAccepted());
        return res && copy.isAccepted();
    }
    default:
        break;
    }
    return false;
}

bool ViewAreaCanvas::eventFilter(QObject *watched, QEvent *event)
{
    auto cell = qobject_cast<ViewAreaCell*>(watched);
    if (!cell || !claims(cell))
        return QOpenGLWidget::eventFilter(watched, event);
    // An event this filter is already forwarding, come back up from the
    // hidden child unaccepted. Forwarding it again is an infinite
    // regress; let it climb past instead.
    if (_forwarding)
        return QOpenGLWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Resize:
        if (MDIView *view = cell->childView())
            view->setGeometry(cell->rect());
        update();
        return false;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        // A hidden widget cannot take focus, so activation cannot ride
        // the child's focus event any more (ViewArea::onFocusChanged);
        // the press says which tile the user is working in.
        if (_area && _area->activeCell() != cell)
            _area->setCanvasActiveCell(cell);
        return forwardEvent(cell, event);
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        return forwardEvent(cell, event);
    default:
        break;
    }
    return QOpenGLWidget::eventFilter(watched, event);
}

#include "moc_ViewAreaCanvas.cpp"
