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
# include <cctype>
# include <QApplication>
# include <QCloseEvent>
# include <QContextMenuEvent>
# include <QMdiSubWindow>
# include <QMenu>
# include <QMouseEvent>
# include <QTimer>
# include <QPainter>
# include <QPainterPath>
# include <QSplitter>
# include <QVBoxLayout>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>

#include "ViewArea.h"
#include "ViewAreaCanvas.h"

#include "Application.h"
#include "Document.h"
#include "MainWindow.h"
#include "View3DInventor.h"
#include "ViewProviderDocumentObject.h"

using namespace Gui;

namespace Gui {

/** The per-cell menu button (docs/SplitViews.md sec 5.4/5.5).
 *
 * A small grip in the cell's top-left corner opening the cell
 * management + content menu. Subtle until hovered, so it does not
 * compete with the scene; it is the discoverable counterpart of the
 * invisible corner action zones.
 */
class ViewAreaMenuButton : public QWidget
{
public:
    static constexpr int Size = 16;

    explicit ViewAreaMenuButton(ViewAreaCell *cell)
        : QWidget(cell)
        , _cell(cell)
    {
        // No Q_OBJECT here (the class lives in this .cpp), so the
        // object name is what tests and stylesheets can find it by.
        setObjectName(QStringLiteral("ViewAreaMenuButton"));
        setCursor(Qt::ArrowCursor);
        setToolTip(QObject::tr("View cell menu"));
    }

protected:
    void mousePressEvent(QMouseEvent *ev) override
    {
        if (ev->button() != Qt::LeftButton)
            return QWidget::mousePressEvent(ev);
        ev->accept();
        _cell->showCellMenu(mapToGlobal(QPoint(0, height())));
    }
    void enterEvent(QEnterEvent *ev) override
    {
        QWidget::enterEvent(ev);
        _hover = true;
        // The corner zones are invisible until touched, so a user who
        // never guesses they are there never finds them. Reaching the
        // one piece of visible cell chrome shows both of them.
        _cell->showZoneHint(true);
        update();
    }
    void leaveEvent(QEvent *ev) override
    {
        QWidget::leaveEvent(ev);
        _hover = false;
        _cell->showZoneHint(false);
        update();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = palette().color(_hover ? QPalette::Highlight
                                          : QPalette::WindowText);
        c.setAlpha(_hover ? 230 : 90);
        QPen pen(c);
        pen.setWidth(2);
        p.setPen(pen);
        const int m = 4;
        for (int i = 0; i < 3; ++i) {
            int y = m + i * (Size - 2 * m) / 2;
            p.drawLine(m, y, Size - m, y);
        }
    }

private:
    ViewAreaCell *_cell;
    bool _hover = false;
};

/** The active cell's border, drawn as a widget rather than as paint on
 * the cell itself.
 *
 * The cell's own paintEvent cannot show this. Its layout has zero
 * margins and the child view fills it, so the border was painted
 * UNDERNEATH the child and never seen on widget composition; and on the
 * unified canvas the GL sibling covers the cell's backing store as well
 * (docs/SplitViews.md sec 13.4). A raised child widget is drawn over
 * both -- measured, sec 16.1 -- which makes this the one implementation
 * that serves the canvas and widget composition alike.
 *
 * Masked to the border ring, so although it is sized to the whole cell
 * it overlaps the GL surface by only the pixels it draws.
 */
class ViewAreaHighlight : public QWidget
{
public:
    static constexpr int Width = 1;

    explicit ViewAreaHighlight(ViewAreaCell *cell)
        : QWidget(cell)
        , _cell(cell)
    {
        // No Q_OBJECT here (the class lives in this .cpp), so the object
        // name is what tests can find it by -- as for the menu button.
        setObjectName(QStringLiteral("ViewAreaHighlight"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

    /// Re-fit to the cell and re-cut the ring mask. Call on every cell
    /// resize; the mask is in local coordinates, so it does not survive
    /// a size change.
    void refit()
    {
        setGeometry(_cell->rect());
        setMask(QRegion(rect())
                - QRegion(rect().adjusted(Width, Width, -Width, -Width)));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        ViewArea *area = _cell->area();
        if (!area || area->activeCell() != _cell || area->cellCount() < 2)
            return;
        QPainter p(this);
        QPen pen(palette().color(QPalette::Highlight));
        pen.setWidth(Width);
        p.setPen(pen);
        p.drawRect(rect().adjusted(0, 0, -Width, -Width));
    }

private:
    ViewAreaCell *_cell;
};

} // namespace Gui

namespace {

/// The dim-plus-arrow overlay shown over the cell a join will consume.
class ViewAreaJoinOverlay : public QWidget
{
public:
    ViewAreaJoinOverlay(QWidget *target, Qt::Orientation axis, bool after)
        : QWidget(target)
        , axis(axis)
        , after(after)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(target->rect());
        show();
        raise();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 96));
        // Arrow pointing the way the join swallows: from the source
        // cell into this one.
        p.setRenderHint(QPainter::Antialiasing);
        QPointF c(width() / 2.0, height() / 2.0);
        double s = qMin(qMin(width(), height()) / 6.0, 28.0);
        QPainterPath path;
        // tip forward along the join direction, tail behind
        QPointF dir = (axis == Qt::Horizontal)
            ? QPointF(after ? 1 : -1, 0) : QPointF(0, after ? 1 : -1);
        QPointF ortho(-dir.y(), dir.x());
        path.moveTo(c + dir * s);
        path.lineTo(c - dir * s * 0.4 + ortho * s);
        path.lineTo(c - dir * s * 0.4 - ortho * s);
        path.closeSubpath();
        p.fillPath(path, QColor(255, 255, 255, 200));
    }

private:
    Qt::Orientation axis;
    bool after;
};

/// Splitter handle with a small management menu.
class ViewAreaSplitterHandle : public QSplitterHandle
{
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void enterEvent(QEnterEvent *ev) override
    {
        QSplitterHandle::enterEvent(ev);
        if (ViewArea *area = ViewArea::areaOf(splitter()))
            area->showZoneHintAt(this, true);
    }
    void leaveEvent(QEvent *ev) override
    {
        QSplitterHandle::leaveEvent(ev);
        if (ViewArea *area = ViewArea::areaOf(splitter()))
            area->showZoneHintAt(this, false);
    }
    void contextMenuEvent(QContextMenuEvent *ev) override
    {
        auto sp = splitter();
        ViewArea *area = ViewArea::areaOf(sp);
        if (!area)
            return QSplitterHandle::contextMenuEvent(ev);
        int idx = sp->indexOf(this);  // handle i sits after widget i-1
        auto before = qobject_cast<ViewAreaCell*>(sp->widget(idx - 1));
        auto behind = qobject_cast<ViewAreaCell*>(sp->widget(idx));
        bool horiz = (orientation() == Qt::Horizontal);

        QMenu menu;
        QAction *closeBefore = menu.addAction(horiz
            ? QObject::tr("Close left view") : QObject::tr("Close top view"));
        closeBefore->setEnabled(before != nullptr);
        QAction *closeBehind = menu.addAction(horiz
            ? QObject::tr("Close right view") : QObject::tr("Close bottom view"));
        closeBehind->setEnabled(behind != nullptr);
        QAction *picked = menu.exec(ev->globalPos());
        if (picked == closeBefore && before)
            area->closeCell(before);
        else if (picked == closeBehind && behind)
            area->closeCell(behind);
        ev->accept();
    }
};

} // anonymous namespace

// ----------------------------------------------------------------------------
// ViewAreaSplitter
// ----------------------------------------------------------------------------

/// setSizes with a sum below the splitter's extent distributes the
/// missing space EQUALLY, skewing every ratio toward even -- permille
/// lists and sizes recorded at another window size both hit it. Scale
/// to the current total first (when there is one).
static void applySizesScaled(QSplitter *sp, const QList<int> &sizes)
{
    int sum = 0;
    for (int v : sizes)
        sum += v;
    if (sum <= 0)
        return;
    int total = 0;
    for (int v : sp->sizes())
        total += v;
    if (total <= 0) {
        sp->setSizes(sizes);
        return;
    }
    QList<int> scaled;
    for (int v : sizes)
        scaled.append(int(qint64(v) * total / sum));
    sp->setSizes(scaled);
}

ViewAreaSplitter::ViewAreaSplitter(Qt::Orientation orientation, QWidget *parent)
    : QSplitter(orientation, parent)
{
    setChildrenCollapsible(false);
}

QSplitterHandle *ViewAreaSplitter::createHandle()
{
    return new ViewAreaSplitterHandle(orientation(), this);
}

void ViewAreaSplitter::resizeEvent(QResizeEvent *ev)
{
    QSplitter::resizeEvent(ev);
    // The first REAL geometry is where a pre-show setSizes has just
    // been mangled (see initialSizes): re-apply the intended shares,
    // scaled to what the splitter actually got. Only a VISIBLE resize
    // with a real extent counts -- hidden widgets get default-size
    // resizes whose consumption would throw the shares away.
    if (!initialSizes.isEmpty() && isVisible()) {
        int total = 0;
        const QList<int> live = sizes();
        for (int v : live)
            total += v;
        if (total > 0) {
            QList<int> pending = initialSizes;
            initialSizes.clear();
            applySizesScaled(this, pending);
        }
    }
}

// ----------------------------------------------------------------------------
// ViewAreaCell
// ----------------------------------------------------------------------------

ViewAreaCell::ViewAreaCell(ViewArea *area)
    : QWidget(nullptr)
    , _area(area)
{
    auto lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    _zoneTopRight = new ViewAreaZone(this, ViewAreaZone::TopRight);
    _zoneBottomLeft = new ViewAreaZone(this, ViewAreaZone::BottomLeft);
    _menuButton = new ViewAreaMenuButton(this);
    _highlight = new ViewAreaHighlight(this);
}

ViewAreaCell::~ViewAreaCell()
{
    // The hosted view is a Qt child, and ~QWidget deletes children only
    // after this body and the member destructors have run.  The
    // destroyed-lambda hostView connects then still finds `self` alive
    // (the QObject half of the cell dies last) and stores nullptr into
    // `_child` -- a QPointer whose destructor has already released its
    // weak reference.  That second release frees Qt's refcount block
    // under the dying view, which QObject::~QObject then writes into
    // ("shared QObject was deleted directly", and a corrupted heap at
    // the next malloc).  Take the view down while the member is alive.
    if (MDIView *view = _child) {
        _child = nullptr;
        delete view;
    }
}

void ViewAreaCell::updateHighlight()
{
    // Re-fit as well as repaint: the mask is in local coordinates and a
    // resize this cell never saw would leave it cut for the old size.
    _highlight->refit();
    _highlight->update();
}

void ViewAreaCell::showZoneHint(bool on)
{
    _zoneTopRight->setHint(on);
    _zoneBottomLeft->setHint(on);
}

void ViewAreaCell::hostView(MDIView *view)
{
    assert(!_child);
    if (!view)
        return;
    _child = view;
    view->setParent(this);
    layout()->addWidget(view);
    view->show();

    // The connections MainWindow::addWindow makes for top level views;
    // an embedded view still reports to the status bar, and it still
    // hears window-state changes of the other MDI views -- that relay
    // is what arms View3DInventor's stop-spin timer when another tab
    // maximizes over this one (a spinning view nobody can see would
    // keep burning frames without it).
    QObject::connect(view, &MDIView::message,
                     getMainWindow(), &MainWindow::showMessage);
    QObject::connect(getMainWindow(), &MainWindow::windowStateChanged,
                     view, &MDIView::windowStateChanged);
    // Qt-level destruction of the child (e.g. the document is closing
    // and deleteSelf ran) collapses the cell.
    //
    // In two steps, because this signal arrives from inside the dying
    // view's own destructor. The collapse runs childViewGone ->
    // collapseCell -> setActiveCell -> MainWindow::setActiveWindow, so
    // doing it here would re-enter MDI activation while a view is
    // half-destroyed -- which is what `89808fae97` had to sever
    // MainWindow's connections in ~MDIView to survive. Severing the
    // connections closed the crash; deferring the collapse removes the
    // re-entrancy that made it possible.
    //
    // The back-pointer is still cleared SYNCHRONOUSLY: between now and
    // the queued call the cell must not hold a pointer to a view that
    // is being deleted, or anything reading childView() in that window
    // reads freed memory.
    QPointer<ViewAreaCell> self(this);
    ViewArea *area = _area;
    QObject::connect(view, &QObject::destroyed, area, [area, self]() {
        if (!self)
            return;
        self->_child = nullptr;
        QMetaObject::invokeMethod(area, [area, self]() {
            // childViewGone re-checks everything that can have changed
            // in the meantime: _closing, the cell still being in the
            // splitter tree, and the cell count. A cell detached by
            // closeCell or collapseCell in the gap needs nothing.
            if (self)
                area->childViewGone(self);
        }, Qt::QueuedConnection);
    });
    _highlight->raise();
    _zoneTopRight->raise();
    _zoneBottomLeft->raise();
    _menuButton->raise();
    update();
}

void ViewAreaCell::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    const int z = ViewAreaZone::Size;
    _zoneTopRight->setGeometry(width() - z, 0, z, z);
    _zoneBottomLeft->setGeometry(0, height() - z, z, z);
    _menuButton->setGeometry(0, 0, ViewAreaMenuButton::Size,
                             ViewAreaMenuButton::Size);
    _highlight->refit();
    _highlight->update();
}

MDIView *ViewAreaCell::releaseView()
{
    MDIView *view = _child;
    if (!view)
        return nullptr;
    // A canvas-drawn cell holds the view hidden, out of the layout and
    // on the shared backend; hand all of that back before it leaves.
    if (_area && _area->canvas())
        _area->canvas()->releaseCell(this);
    // Cleared first: the reparenting below delivers ChildRemoved, which
    // must read as an intentional release, not the child being torn away.
    _child = nullptr;
    QObject::disconnect(view, nullptr, _area, nullptr);
    QObject::disconnect(view, &MDIView::message,
                        getMainWindow(), &MainWindow::showMessage);
    QObject::disconnect(getMainWindow(), &MainWindow::windowStateChanged,
                        view, &MDIView::windowStateChanged);
    layout()->removeWidget(view);
    view->setParent(nullptr);
    return view;
}

void ViewAreaCell::childEvent(QChildEvent *ev)
{
    QWidget::childEvent(ev);
    // The hosted view can be reparented away without dying -- e.g.
    // MDIView::setCurrentViewMode(TopLevel) pulls it out as a window of
    // its own. An empty tile serves nobody; give its space back.
    if (ev->removed() && _child && ev->child() == _child) {
        _child = nullptr;
        if (_area)
            _area->childViewGone(this);
    }
}

void ViewAreaCell::paintEvent(QPaintEvent *ev)
{
    // The active-cell border is NOT drawn here: the child view fills the
    // cell and is painted over it, so nothing drawn on the cell itself
    // is ever seen. ViewAreaHighlight, a raised child, carries it.
    QWidget::paintEvent(ev);
}

void ViewAreaCell::showCellMenu(const QPoint &globalPos)
{
    ViewArea *area = _area;
    if (!area)
        return;
    QMenu menu;
    QPointer<ViewAreaCell> self(this);

    // Content selector first (docs/SplitViews.md sec 5.5): the 3D
    // view, then one entry per object-provided view -- objects whose
    // view is already materialized wherever it lives, plus TechDraw
    // pages by type. The page type resolves by NAME so Gui keeps no
    // TechDraw link dependency; with the module not loaded there are
    // no pages to list anyway.
    Gui::Document *doc = area->getGuiDocument();
    MDIView *child = _child;
    QAction *act3d = nullptr;
    if (doc) {
        act3d = menu.addAction(tr("3D view"));
        act3d->setCheckable(true);
        act3d->setChecked(qobject_cast<View3DInventor*>(child) != nullptr);
        const Base::Type pageType = Base::Type::fromName("TechDraw::DrawPage");
        for (auto obj : doc->getDocument()->getObjects()) {
            auto vp = dynamic_cast<ViewProviderDocumentObject*>(
                    Application::Instance->getViewProvider(obj));
            if (!vp)
                continue;
            MDIView *objView = vp->getMDIView();
            const bool typed = pageType != Base::Type::badType()
                && obj->getTypeId().isDerivedFrom(pageType);
            if (!objView && !typed)
                continue;
            QAction *act = menu.addAction(
                    QString::fromUtf8(obj->Label.getValue()));
            act->setCheckable(true);
            act->setChecked(objView && objView == child);
            act->setData(QString::fromUtf8(obj->getNameInDocument()));
        }
        menu.addSeparator();
    }

    QAction *splitH = menu.addAction(tr("Split horizontal"));
    QAction *splitV = menu.addAction(tr("Split vertical"));
    QAction *maximize = menu.addAction(area->maximizedCell() == this
            ? tr("Restore layout") : tr("Maximize view"));
    maximize->setEnabled(area->cellCount() > 1
            || area->maximizedCell() == this);
    QAction *close = menu.addAction(tr("Close view"));
    close->setEnabled(area->cellCount() > 1);

    QAction *picked = menu.exec(globalPos);
    if (!picked || !self)
        return;
    if (picked == splitH) {
        area->splitCell(this, Qt::Horizontal);
    }
    else if (picked == splitV) {
        area->splitCell(this, Qt::Vertical);
    }
    else if (picked == maximize) {
        area->toggleMaximizeCell(area->maximizedCell() ? area->maximizedCell()
                                                       : this);
    }
    else if (picked == close) {
        area->closeCell(this);
    }
    else if (picked == act3d) {
        if (qobject_cast<View3DInventor*>(childView()) || !doc)
            return;
        // Clone a 3D view the user already has -- a sibling cell's
        // first (its camera is this area's context), else any of the
        // document's -- or create a bare one when there is none.
        MDIView *fresh = nullptr;
        for (auto cell : area->cells()) {
            if (cell != this
                    && qobject_cast<View3DInventor*>(cell->childView())) {
                fresh = area->cloneChildFor(cell);
                break;
            }
        }
        if (!fresh) {
            for (auto view : doc->getMDIViews()) {
                if (auto view3d = qobject_cast<View3DInventor*>(view)) {
                    if (auto host = ViewArea::areaOf(view3d)) {
                        if (auto cell = host->cellOf(view3d)) {
                            fresh = host->cloneChildFor(cell);
                            break;
                        }
                    }
                }
            }
        }
        if (!fresh)
            fresh = doc->createView3D();
        if (fresh)
            area->setCellView(this, fresh);
    }
    else if (doc && picked->data().isValid()) {
        // An object entry: materialize its view (plain visibility
        // semantics for view-bearing objects) and host it here --
        // the Std_ViewCellShowObject behavior without the selection.
        QByteArray objName = picked->data().toString().toUtf8();
        auto obj = doc->getDocument()->getObject(objName.constData());
        if (!obj)
            return;
        auto vp = dynamic_cast<ViewProviderDocumentObject*>(
                Application::Instance->getViewProvider(obj));
        if (!vp)
            return;
        MDIView *view = vp->getMDIView();
        if (!view) {
            vp->show();
            view = vp->getMDIView();
        }
        if (view && view != childView())
            area->setCellView(this, view);
    }
}

// ----------------------------------------------------------------------------
// ViewAreaZone
// ----------------------------------------------------------------------------

ViewAreaZone::ViewAreaZone(ViewAreaCell *cell, Corner corner)
    : QWidget(cell)
    , _cell(cell)
    , _corner(corner)
{
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

void ViewAreaZone::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(ev);
    _dragging = true;
    _pressGlobal = ev->globalPosition().toPoint();
    ev->accept();
}

void ViewAreaZone::mouseMoveEvent(QMouseEvent *ev)
{
    if (!_dragging)
        return QWidget::mouseMoveEvent(ev);
    QPoint g = ev->globalPosition().toPoint();

    // Once a split happened the rest of the drag adjusts the fresh
    // border, wherever the cursor goes.
    if (_resizeSplitter) {
        int pos = (_resizeOrientation == Qt::Horizontal)
            ? _resizeSplitter->mapFromGlobal(g).x()
            : _resizeSplitter->mapFromGlobal(g).y();
        _resizeSplitter->dragSplitter(pos, _resizeIndex);
        return;
    }

    QPoint d = g - _pressGlobal;
    ViewArea *area = _cell->area();
    QRect cellRect(_cell->mapToGlobal(QPoint(0, 0)), _cell->size());
    if (cellRect.contains(g)) {
        // Back inside always cancels an armed join, even right at the
        // press point where the split threshold below is not met.
        disarmJoin();
        if (d.manhattanLength() < 12)
            return;
        // Inward drag: split along the dominant axis.
        Qt::Orientation o = (qAbs(d.x()) >= qAbs(d.y()))
            ? Qt::Horizontal : Qt::Vertical;
        ViewAreaCell *fresh = area->splitCell(_cell, o);
        if (fresh) {
            auto sp = qobject_cast<ViewAreaSplitter*>(fresh->parentWidget());
            if (sp) {
                _resizeSplitter = sp;
                _resizeOrientation = o;
                // Handle i sits before widget i; the fresh cell's index
                // names the border between it and the split cell.
                _resizeIndex = sp->indexOf(fresh);
            }
        }
    }
    else {
        // Outward drag: arm a join that consumes the neighbor the
        // cursor entered; dragging back disarms.
        Qt::Orientation axis;
        bool after;
        if (g.x() > cellRect.right()) {
            axis = Qt::Horizontal; after = true;
        }
        else if (g.x() < cellRect.left()) {
            axis = Qt::Horizontal; after = false;
        }
        else if (g.y() > cellRect.bottom()) {
            axis = Qt::Vertical; after = true;
        }
        else {
            axis = Qt::Vertical; after = false;
        }
        ViewAreaCell *target = area->joinTargetFor(_cell, axis, after);
        if (target != _joinTarget) {
            disarmJoin();
            if (target)
                armJoin(target, axis, after);
        }
    }
}

void ViewAreaZone::mouseReleaseEvent(QMouseEvent *ev)
{
    if (!_dragging)
        return QWidget::mouseReleaseEvent(ev);
    ViewAreaCell *target = _joinTarget;
    ViewArea *area = _cell->area();
    endDrag();
    if (target)
        area->closeCell(target);
    ev->accept();
}

void ViewAreaZone::armJoin(ViewAreaCell *target, Qt::Orientation axis, bool after)
{
    _joinTarget = target;
    // The arrow points the way the source expands -- into the target.
    _joinOverlay = new ViewAreaJoinOverlay(target, axis, after);
}

void ViewAreaZone::disarmJoin()
{
    if (_joinOverlay)
        _joinOverlay->deleteLater();
    _joinOverlay = nullptr;
    _joinTarget = nullptr;
}

void ViewAreaZone::endDrag()
{
    disarmJoin();
    _dragging = false;
    _resizeSplitter = nullptr;
    _resizeIndex = -1;
}

void ViewAreaZone::setHint(bool on)
{
    if (_hint == on)
        return;
    _hint = on;
    update();
}

void ViewAreaZone::enterEvent(QEnterEvent *ev)
{
    QWidget::enterEvent(ev);
    _hover = true;
    update();
}

void ViewAreaZone::leaveEvent(QEvent *ev)
{
    QWidget::leaveEvent(ev);
    _hover = false;
    update();
}

void ViewAreaZone::paintEvent(QPaintEvent *)
{
    if (!_hover && !_hint)
        return;  // invisible until hovered, like Blender's action zones
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = palette().color(QPalette::Highlight);
    if (!_hover)
        c.setAlpha(130);  // a pointed-out zone, not one under the cursor
    QPen pen(c);
    pen.setWidth(2);
    p.setPen(pen);
    // Diagonal grip strokes facing the cell interior.
    const int s = Size;
    if (_corner == TopRight) {
        p.drawLine(2, 2, s - 3, s - 3);
        p.drawLine(s / 2, 2, s - 3, s / 2);
    }
    else {
        p.drawLine(2, 2, s - 3, s - 3);
        p.drawLine(2, s / 2, s / 2, s - 3);
    }
}

// ----------------------------------------------------------------------------
// ViewArea
// ----------------------------------------------------------------------------

PROPERTY_SOURCE_ABSTRACT(Gui::ViewArea, Gui::MDIView)

ViewArea::ViewArea(Gui::Document* pcDocument, QWidget* parent, Qt::WindowFlags wflags)
    : MDIView(pcDocument, parent, wflags)
{
    _rootSplitter = new ViewAreaSplitter(Qt::Horizontal, this);
    setCentralWidget(_rootSplitter);

    auto cell = new ViewAreaCell(this);
    _rootSplitter->addWidget(cell);
    _activeCell = cell;

    connect(qApp, &QApplication::focusChanged, this, &ViewArea::onFocusChanged);
}

ViewArea::~ViewArea()
{
    _closing = true;
    // Before the cells go: releasing a claim puts a hidden child view
    // back in its cell's layout and hands its viewer its own backend.
    if (_canvas) {
        _canvas->releaseAll(false);
        delete _canvas;
        _canvas = nullptr;
    }
}

const char *ViewArea::getName() const
{
    return "ViewArea";
}

ViewArea *ViewArea::areaOf(const QWidget *w)
{
    for (const QWidget *p = w; p; p = p->parentWidget()) {
        if (auto area = qobject_cast<ViewArea*>(const_cast<QWidget*>(p)))
            return area;
    }
    return nullptr;
}

void ViewArea::stealFromMdiArea(MDIView *view)
{
    auto sub = qobject_cast<QMdiSubWindow*>(view->parentWidget());
    if (!sub)
        return;
    // Undo the addWindow plumbing; hostView re-establishes what an
    // embedded view needs. removeWindow also detaches the sub window
    // from the MDI area without deleting the view.
    getMainWindow()->removeWindow(view, false);
    sub->setWidget(nullptr);
    view->setParent(nullptr);
    sub->deleteLater();
}

ViewArea *ViewArea::wrap(MDIView *view)
{
    if (!view || !qobject_cast<QMdiSubWindow*>(view->parentWidget()))
        return nullptr;

    auto mw = getMainWindow();
    bool wasActive = (mw->activeWindow() == view);

    auto area = new ViewArea(view->getGuiDocument(), mw);
    area->setWindowTitle(view->windowTitle());
    area->setWindowIcon(view->windowIcon());

    stealFromMdiArea(view);
    area->activeCell()->hostView(view);
    mw->addWindow(area);
    if (wasActive)
        mw->setActiveWindow(view);
    return area;
}

bool ViewArea::setCellView(ViewAreaCell *cell, MDIView *view)
{
    if (!cell || cell->area() != this || !view)
        return false;
    if (cell->childView() == view)
        return true;
    if (areaOf(view))
        return false;  // embedded in a cell already (here or elsewhere)
    if (_maximizedCell)
        toggleMaximizeCell(_maximizedCell);

    if (MDIView *old = cell->childView()) {
        if (!old->close())
            return false;
        // The old view is on its way out (delete-on-close); detach it
        // from the cell so the newcomer takes its place immediately.
        cell->releaseView();
    }
    stealFromMdiArea(view);
    cell->hostView(view);
    setActiveCell(cell);
    syncCanvas();
    return true;
}

void ViewArea::showZoneHintAt(const QSplitterHandle *handle, bool on)
{
    QRect h;
    bool horiz = true;
    if (on && handle) {
        h = QRect(handle->mapToGlobal(QPoint(0, 0)), handle->size());
        horiz = handle->orientation() == Qt::Horizontal;
    }
    for (ViewAreaCell *cell : cells()) {
        bool borders = false;
        if (!h.isNull() && cell->isVisible()) {
            const QRect r(cell->mapToGlobal(QPoint(0, 0)), cell->size());
            // Two conditions, because a handle in a nested tree runs
            // past cells it does not touch: the cell must overlap the
            // handle ALONG its length, and one of its edges ACROSS the
            // handle must be the handle's own. Testing only the first
            // lights up the whole subtree; only the second lights up
            // every cell in the row.
            const bool along = horiz
                ? (r.top() <= h.bottom() && r.bottom() >= h.top())
                : (r.left() <= h.right() && r.right() >= h.left());
            const int slack = 2;  // frame width and rounding
            const bool edge = horiz
                ? (qAbs(r.right() + 1 - h.left()) <= slack
                   || qAbs(r.left() - h.right() - 1) <= slack)
                : (qAbs(r.bottom() + 1 - h.top()) <= slack
                   || qAbs(r.top() - h.bottom() - 1) <= slack);
            borders = along && edge;
        }
        cell->showZoneHint(borders);
    }
}

ViewAreaCell *ViewArea::cellOf(const MDIView *view) const
{
    if (!view)
        return nullptr;
    for (const QWidget *p = view->parentWidget(); p; p = p->parentWidget()) {
        if (auto cell = qobject_cast<ViewAreaCell*>(const_cast<QWidget*>(p)))
            return cell->area() == this ? cell : nullptr;
    }
    return nullptr;
}

std::vector<ViewAreaCell*> ViewArea::cells() const
{
    std::vector<ViewAreaCell*> res;
    for (auto cell : findChildren<ViewAreaCell*>()) {
        if (cell->area() == this)
            res.push_back(cell);
    }
    return res;
}

int ViewArea::cellCount() const
{
    int n = 0;
    for (auto cell : findChildren<ViewAreaCell*>()) {
        if (cell->area() == this)
            ++n;
    }
    return n;
}

ViewAreaCell *ViewArea::lastUsedCell(
        const std::function<bool(MDIView*)> &pred) const
{
    ViewAreaCell *best = nullptr;
    int bestStamp = -1;
    for (auto cell : cells()) {
        if (!pred(cell->childView()))
            continue;
        if (cell->_mruStamp > bestStamp) {
            best = cell;
            bestStamp = cell->_mruStamp;
        }
    }
    return best;
}

bool ViewArea::activateCellOf(MDIView *view)
{
    auto cell = cellOf(view);
    if (!cell)
        return false;
    setActiveCell(cell);
    return true;
}

MDIView *ViewArea::cloneChildFor(ViewAreaCell *cell)
{
    MDIView *child = cell->childView();
    if (!child)
        return nullptr;
    if (auto view3d = qobject_cast<View3DInventor*>(child)) {
        Gui::Document *doc = view3d->getGuiDocument();
        if (!doc)
            return nullptr;
        MDIView *clone = doc->cloneView(view3d, /*transferEdit*/ false);
        if (!clone)
            return nullptr;
        const char *camera = nullptr;
        if (view3d->onMsg("GetCamera", &camera) && camera) {
            std::string cmd;
            if (doc->saveCameraSettings(camera, &cmd)) {
                const char *dummy = nullptr;
                clone->onMsg(cmd.c_str(), &dummy);
            }
        }
        return clone;
    }
    return nullptr;
}

ViewAreaCell *ViewArea::splitCell(ViewAreaCell *cell, Qt::Orientation orientation,
                                  MDIView *newChild)
{
    if (!cell || cell->area() != this)
        return nullptr;
    if (_maximizedCell)
        toggleMaximizeCell(_maximizedCell);
    MDIView *child = newChild ? newChild : cloneChildFor(cell);
    if (!child)
        return nullptr;

    auto splitter = qobject_cast<QSplitter*>(cell->parentWidget());
    assert(splitter);
    auto newCell = new ViewAreaCell(this);
    int idx = splitter->indexOf(cell);

    if (splitter->count() < 2 || splitter->orientation() == orientation) {
        // Same direction (or a splitter that has not committed to one
        // yet): insert as a sibling, halving the split cell's share.
        if (splitter->count() < 2)
            splitter->setOrientation(orientation);
        QList<int> sizes = splitter->sizes();
        splitter->insertWidget(idx + 1, newCell);
        if (idx < sizes.size()) {
            int half = sizes[idx] / 2;
            sizes[idx] -= half;
            sizes.insert(idx + 1, half);
            splitter->setSizes(sizes);
        }
    }
    else {
        // Crossing direction: nest a new splitter in the cell's place.
        QList<int> sizes = splitter->sizes();
        auto nested = new ViewAreaSplitter(orientation);
        int half = (orientation == Qt::Horizontal ? cell->width()
                                                  : cell->height()) / 2;
        splitter->replaceWidget(idx, nested);
        nested->addWidget(cell);
        cell->show();  // replaceWidget hides the widget it takes out
        nested->addWidget(newCell);
        splitter->setSizes(sizes);
        nested->setSizes({half, half});
    }

    newCell->hostView(child);
    setActiveCell(cell, false);
    syncCanvas();
    return newCell;
}

ViewAreaCell *ViewArea::joinTargetFor(ViewAreaCell *cell, Qt::Orientation axis,
                                      bool after) const
{
    if (!cell || cell->area() != this)
        return nullptr;
    auto sp = qobject_cast<QSplitter*>(cell->parentWidget());
    if (!sp || sp->orientation() != axis || sp->count() < 2)
        return nullptr;
    int idx = sp->indexOf(cell) + (after ? 1 : -1);
    if (idx < 0 || idx >= sp->count())
        return nullptr;
    // Leaf only: a nested splitter neighbor does not share its full
    // border with this one cell (Blender's aligned-edge rule).
    return qobject_cast<ViewAreaCell*>(sp->widget(idx));
}

void ViewArea::toggleMaximizeCell(ViewAreaCell *cell)
{
    if (_maximizedCell) {
        for (auto sub : findChildren<ViewAreaCell*>()) {
            if (sub->area() == this)
                sub->show();
        }
        for (auto sp : findChildren<QSplitter*>())
            sp->show();
        for (auto &state : _maximizeRestore) {
            if (!state.splitter)
                continue;
            state.splitter->restoreState(state.state);
            // The plain sizes beat the opaque blob when both exist: a
            // state captured before the splitter was laid out (the
            // restored-maximize-on-reopen path) restores to equal
            // shares, while the recorded sizes carry the layout.
            applySizesScaled(state.splitter, state.sizes);
        }
        _maximizeRestore.clear();
        _maximizedCell = nullptr;
        syncCanvas();
        return;
    }
    if (!cell || cell->area() != this || cellCount() < 2)
        return;

    _maximizeRestore.clear();
    for (auto sp : findChildren<QSplitter*>()) {  // includes the root
        // Intended shares beat live ones while a restored layout has
        // not been laid out yet (initialSizes still pending): sizes()
        // then reads the equal-distribution defaults, and both the
        // un-maximize and a save-while-maximized would keep THOSE.
        QList<int> sizes = sp->sizes();
        if (auto vsp = qobject_cast<ViewAreaSplitter*>(sp)) {
            if (!vsp->initialSizes.isEmpty())
                sizes = vsp->initialSizes;
        }
        _maximizeRestore.push_back({sp, sp->saveState(), sizes});
    }

    // Along the path from the cell to the root, hide every sibling; the
    // splitters give hidden widgets no space, so the cell takes it all.
    QWidget *w = cell;
    while (w && w != this) {
        QWidget *parent = w->parentWidget();
        if (auto sp = qobject_cast<QSplitter*>(parent)) {
            for (int i = 0; i < sp->count(); ++i) {
                if (sp->widget(i) != w)
                    sp->widget(i)->hide();
            }
        }
        w = parent;
    }
    _maximizedCell = cell;
    setActiveCell(cell);
    // One visible tile: the canvas has nothing to share and stands
    // down, giving the maximized cell its own widget composition back.
    syncCanvas();
}

void ViewArea::setPendingMaximize(ViewAreaCell *cell)
{
    _pendingMaximize = cell;
    // Already sized (the container was added and laid out before the
    // caller armed this): fire now; otherwise the first real resize
    // does.
    if (width() > 0 && height() > 0)
        armPendingMaximize();
}

void ViewArea::armPendingMaximize()
{
    if (!_pendingMaximize)
        return;
    auto cell = _pendingMaximize;
    _pendingMaximize = nullptr;
    QPointer<ViewArea> self(this);
    QPointer<ViewAreaCell> cellPtr(cell);
    // One tick later: the splitters consume their pending layout
    // sizes DURING the current layout pass, and the maximize capture
    // must read the realized values, not the defaults.
    QTimer::singleShot(0, this, [self, cellPtr]() {
        if (self && cellPtr && !self->maximizedCell())
            self->toggleMaximizeCell(cellPtr);
    });
}

void ViewArea::resizeEvent(QResizeEvent *ev)
{
    MDIView::resizeEvent(ev);
    if (width() > 0 && height() > 0)
        armPendingMaximize();
    syncCanvas();
}

void ViewArea::syncCanvas()
{
    if (_closing)
        return;
    if (!ViewAreaCanvas::wanted()) {
        // Turned off (or the render engine went away): give every cell
        // back its own widget composition and drop the shared backend.
        if (_canvas) {
            _canvas->releaseAll();
            delete _canvas;
            _canvas = nullptr;
        }
        return;
    }
    if (!_canvas)
        _canvas = new ViewAreaCanvas(this);
    // The canvas is the ground the splitter tree stands on: same rect,
    // bottom of the stack, with the claimed cells painting no
    // background of their own so it shows through.
    _canvas->setGeometry(_rootSplitter->geometry());
    _canvas->sync();
}

void ViewArea::setCanvasActiveCell(ViewAreaCell *cell)
{
    setActiveCell(cell);
}

QList<int> ViewArea::preMaximizeSizes(const QSplitter *sp) const
{
    if (!_maximizedCell)
        return {};
    for (const auto &state : _maximizeRestore) {
        if (state.splitter == sp)
            return state.sizes;
    }
    return {};
}

bool ViewArea::closeCell(ViewAreaCell *cell)
{
    if (!cell || cell->area() != this)
        return false;
    if (_maximizedCell)
        toggleMaximizeCell(_maximizedCell);
    if (cellCount() <= 1)
        return close();

    if (MDIView *child = cell->childView()) {
        if (!child->close())
            return false;
    }
    collapseCell(cell);
    return true;
}

void ViewArea::collapseCell(ViewAreaCell *cell)
{
    auto splitter = qobject_cast<QSplitter*>(cell->parentWidget());
    if (!splitter)
        return;
    bool wasActive = (_activeCell == cell);
    cell->setParent(nullptr);
    cell->deleteLater();

    // Un-nest splitters left with a single child.
    QSplitter *s = splitter;
    while (s != _rootSplitter && s->count() == 1) {
        auto parent = qobject_cast<QSplitter*>(s->parentWidget());
        if (!parent)
            break;
        QWidget *lone = s->widget(0);
        int idx = parent->indexOf(s);
        QList<int> sizes = parent->sizes();
        parent->replaceWidget(idx, lone);
        lone->show();
        s->setParent(nullptr);
        s->deleteLater();
        parent->setSizes(sizes);
        s = parent;
    }

    if (wasActive) {
        auto all = cells();
        setActiveCell(all.empty() ? nullptr : all.front());
    }
    syncCanvas();
}

void ViewArea::childViewGone(ViewAreaCell *cell)
{
    if (_closing || !cell)
        return;
    cell->_child = nullptr;
    // Only collapse a cell that is still part of the splitter tree; a
    // cell already detached by closeCell/collapseCell needs nothing.
    if (!cell->parentWidget())
        return;
    if (cellCount() <= 1) {
        // Losing the last child closes the container.
        _closing = true;
        deleteSelf();
        return;
    }
    collapseCell(cell);
}

void ViewArea::setActiveCell(ViewAreaCell *cell, bool activateWindow)
{
    if (_activeCell != cell) {
        auto old = _activeCell;
        _activeCell = cell;
        if (cell)
            cell->_mruStamp = ++_mruCounter;
        // The border lives on a child widget, so updating the cell
        // alone would repaint everything except the thing that changed.
        if (old)
            old->updateHighlight();
        if (cell)
            cell->updateHighlight();
        // The unified canvas feeds its backend from the ACTIVE cell, so
        // the Coin residue -- draggers above all -- lands where the
        // user is working (docs/SplitViews.md sec 13).
        if (_canvas)
            _canvas->sync();
    }
    if (activateWindow && cell && cell->childView())
        getMainWindow()->setActiveWindow(cell->childView());
}

void ViewArea::onFocusChanged(QWidget *old, QWidget *now)
{
    Q_UNUSED(old);
    if (_closing || !now || !isAncestorOf(now))
        return;
    for (QWidget *w = now; w && w != this; w = w->parentWidget()) {
        if (auto cell = qobject_cast<ViewAreaCell*>(w)) {
            if (cell->area() == this && cell->childView())
                setActiveCell(cell);
            break;
        }
    }
}

static std::string layoutNode(const QWidget *w, const ViewArea *area,
        const std::function<std::string(MDIView*)> &leafToken)
{
    if (auto cell = qobject_cast<const ViewAreaCell*>(const_cast<QWidget*>(w))) {
        if (cell->childView())
            return leafToken(cell->childView());
        return {};
    }
    auto sp = qobject_cast<const QSplitter*>(w);
    if (!sp)
        return {};
    std::vector<std::string> parts;
    std::vector<int> sizes;
    // While a cell is maximized the live sizes are degenerate (the
    // hidden siblings report nothing); the recorded pre-maximize
    // sizes are the layout worth keeping.
    QList<int> spSizes = area ? area->preMaximizeSizes(sp) : QList<int>();
    if (spSizes.isEmpty())
        spSizes = const_cast<QSplitter*>(sp)->sizes();
    int total = 0;
    for (int i = 0; i < sp->count(); ++i) {
        std::string sub = layoutNode(sp->widget(i), area, leafToken);
        if (sub.empty())
            continue;
        int px = (i < spSizes.size()) ? spSizes[i] : 1;
        parts.push_back(std::move(sub));
        sizes.push_back(px);
        total += px;
    }
    if (parts.empty())
        return {};
    if (parts.size() == 1)
        return parts.front();
    std::string res(sp->orientation() == Qt::Horizontal ? "H{" : "V{");
    for (size_t i = 0; i < sizes.size(); ++i) {
        // permille, floored at 50 so a save while a cell is collapsed
        // (e.g. maximized) does not restore it invisible
        int f = total > 0 ? (sizes[i] * 1000 + total / 2) / total : 0;
        res += std::to_string(std::max(f, 50));
        res += (i + 1 < sizes.size()) ? "," : "|";
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        res += parts[i];
        if (i + 1 < parts.size())
            res += ",";
    }
    res += "}";
    return res;
}

std::string ViewArea::layoutString(
        const std::function<std::string(MDIView*)> &leafToken) const
{
    return layoutNode(_rootSplitter->count() == 1
            ? _rootSplitter->widget(0) : (QWidget*)_rootSplitter, this,
            leafToken);
}

namespace {

/// Recursive-descent parser for the layoutString format.
struct LayoutParser
{
    const std::string &s;
    size_t pos = 0;
    ViewArea *area;
    const std::function<Gui::MDIView*(const std::string&)> &resolve;

    // Build the node at pos into a widget (a cell or a splitter);
    // returns null when nothing under it resolved.
    QWidget *node(std::function<ViewAreaCell*()> makeCell)
    {
        if (pos >= s.size())
            return nullptr;
        if (s[pos] == 'H' || s[pos] == 'V') {
            Qt::Orientation o = (s[pos] == 'H') ? Qt::Horizontal : Qt::Vertical;
            ++pos;
            if (pos >= s.size() || s[pos] != '{')
                return nullptr;
            ++pos;
            std::vector<int> sizes;
            int cur = 0;
            while (pos < s.size() && s[pos] != '|') {
                if (s[pos] == ',') {
                    sizes.push_back(cur);
                    cur = 0;
                }
                else if (isdigit(static_cast<unsigned char>(s[pos])))
                    cur = cur * 10 + (s[pos] - '0');
                ++pos;
            }
            sizes.push_back(cur);
            if (pos < s.size())
                ++pos;  // '|'
            auto sp = new ViewAreaSplitter(o);
            std::vector<int> childSizes;
            size_t childIdx = 0;
            while (pos < s.size() && s[pos] != '}') {
                QWidget *sub = node(makeCell);
                if (sub) {
                    sp->addWidget(sub);
                    childSizes.push_back(childIdx < sizes.size()
                            ? sizes[childIdx] : 100);
                }
                ++childIdx;
                if (pos < s.size() && s[pos] == ',')
                    ++pos;
            }
            if (pos < s.size())
                ++pos;  // '}'
            if (sp->count() == 0) {
                delete sp;
                return nullptr;
            }
            if (sp->count() == 1) {
                QWidget *lone = sp->widget(0);
                lone->setParent(nullptr);
                delete sp;
                return lone;
            }
            QList<int> qsizes;
            for (int v : childSizes)
                qsizes.append(std::max(v, 1));
            sp->setSizes(qsizes);
            sp->initialSizes = qsizes;
            return sp;
        }
        // leaf token: up to , } or end
        size_t start = pos;
        while (pos < s.size() && s[pos] != ',' && s[pos] != '}')
            ++pos;
        std::string token = s.substr(start, pos - start);
        Gui::MDIView *view = token.empty() ? nullptr : resolve(token);
        if (!view)
            return nullptr;
        ViewArea::detachViewForHosting(view);
        ViewAreaCell *cell = makeCell();
        cell->hostView(view);
        return cell;
    }
};

} // anonymous namespace

bool ViewArea::applyLayout(const std::string &layout,
        const std::function<MDIView*(const std::string&)> &tokenToView)
{
    LayoutParser parser{layout, 0, this, tokenToView};
    QWidget *tree = parser.node([this]() { return new ViewAreaCell(this); });
    if (!tree)
        return false;
    // Replace the fresh container's single empty cell.
    while (_rootSplitter->count()) {
        QWidget *w = _rootSplitter->widget(0);
        w->setParent(nullptr);
        w->deleteLater();
    }
    if (auto sp = qobject_cast<ViewAreaSplitter*>(tree)) {
        // Adopt the parsed tree's root as the container root. The
        // sizes come from what the parser ASKED for -- a never-shown
        // splitter answers sizes() with its defaults, which flattened
        // every restored root to equal shares.
        QList<int> sizes = sp->initialSizes.isEmpty() ? sp->sizes()
                                                      : sp->initialSizes;
        _rootSplitter->setOrientation(sp->orientation());
        while (sp->count()) {
            QWidget *w = sp->widget(0);
            w->setParent(nullptr);
            _rootSplitter->addWidget(w);
            w->show();
        }
        delete sp;
        // Realized already (a live re-apply): scaled now. Fresh (the
        // restore path): the first real resize applies it.
        _rootSplitter->initialSizes = sizes;
        applySizesScaled(_rootSplitter, sizes);
    }
    else
        _rootSplitter->addWidget(tree);
    auto all = cells();
    setActiveCell(all.empty() ? nullptr : all.front(), false);
    return true;
}

void ViewArea::detachViewForHosting(MDIView *view)
{
    if (!view)
        return;
    if (auto area = areaOf(view)) {
        if (auto cell = area->cellOf(view))
            cell->releaseView();
        return;
    }
    stealFromMdiArea(view);
}

MDIView *ViewArea::activeSubView()
{
    if (_activeCell && _activeCell->childView())
        return _activeCell->childView()->activeSubView();
    for (auto cell : cells()) {
        if (cell->childView())
            return cell->childView()->activeSubView();
    }
    return this;
}

bool ViewArea::onMsg(const char* pMsg, const char** ppReturn)
{
    // Camera messages are per-cell; the document's camera persistence
    // talks to the child views directly (docs/SplitViews.md sec 5.2).
    if (strcmp(pMsg, "GetCamera") == 0 || strncmp(pMsg, "SetCamera", 9) == 0)
        return false;
    MDIView *view = activeSubView();
    if (view && view != this)
        return view->onMsg(pMsg, ppReturn);
    return MDIView::onMsg(pMsg, ppReturn);
}

bool ViewArea::onHasMsg(const char* pMsg) const
{
    if (strcmp(pMsg, "GetCamera") == 0 || strncmp(pMsg, "SetCamera", 9) == 0)
        return false;
    MDIView *view = const_cast<ViewArea*>(this)->activeSubView();
    if (view && view != this)
        return view->onHasMsg(pMsg);
    return MDIView::onHasMsg(pMsg);
}

bool ViewArea::canClose()
{
    if (_closing)
        return true;
    // Closing the container closes every cell. If the document has no
    // bound view outside this container this is the document's last
    // view closing, and the document must get asked -- the container
    // level mirror of MDIView::canClose.
    Gui::Document *doc = getGuiDocument();
    if (doc) {
        bool outside = false;
        for (auto view : doc->getMDIViews()) {
            if (view != this && areaOf(view) != this) {
                outside = true;
                break;
            }
        }
        if (!outside)
            return doc->canClose(true, true);
    }
    return true;
}

void ViewArea::closeEvent(QCloseEvent *e)
{
    MDIView::closeEvent(e);
    // Accepted means the container is going away (delete on close); the
    // deletion is deferred, and the children may be torn down first by
    // the document. No cell collapsing on a dying container.
    if (e->isAccepted())
        _closing = true;
}

void ViewArea::deleteSelf()
{
    _closing = true;
    MDIView::deleteSelf();
    // MDIView::deleteSelf closes the QMdiSubWindow shell, but a close
    // only hides it: the TAB it holds in the MDI tab bar lives until
    // the DEFERRED delete runs, which a nested event loop postpones
    // indefinitely -- a dead container tab lingering after its last
    // view moved elsewhere. Detach the shell now, the way
    // MainWindow::removeWindow does.
    if (auto sub = qobject_cast<QMdiSubWindow*>(parentWidget())) {
        if (sub->parent())
            sub->setParent(nullptr);
    }
}

void ViewArea::viewAll()
{
    MDIView *view = activeSubView();
    if (view && view != this)
        view->viewAll();
}

void ViewArea::onUpdate()
{
    update();
}

bool ViewArea::containsViewProvider(const ViewProvider *vp) const
{
    for (auto cell : cells()) {
        if (cell->childView() && cell->childView()->containsViewProvider(vp))
            return true;
    }
    return false;
}

void ViewArea::print()
{
    MDIView *view = activeSubView();
    if (view && view != this)
        view->print();
}

void ViewArea::printPdf()
{
    MDIView *view = activeSubView();
    if (view && view != this)
        view->printPdf();
}

void ViewArea::printPreview()
{
    MDIView *view = activeSubView();
    if (view && view != this)
        view->printPreview();
}

void ViewArea::print(QPrinter *printer)
{
    MDIView *view = activeSubView();
    if (view && view != this)
        view->print(printer);
}

QStringList ViewArea::undoActions() const
{
    MDIView *view = const_cast<ViewArea*>(this)->activeSubView();
    if (view && view != this)
        return view->undoActions();
    return MDIView::undoActions();
}

QStringList ViewArea::redoActions() const
{
    MDIView *view = const_cast<ViewArea*>(this)->activeSubView();
    if (view && view != this)
        return view->redoActions();
    return MDIView::redoActions();
}

void ViewArea::setOverrideCursor(const QCursor &cursor)
{
    for (auto cell : cells()) {
        if (cell->childView())
            cell->childView()->setOverrideCursor(cursor);
    }
}

void ViewArea::restoreOverrideCursor()
{
    for (auto cell : cells()) {
        if (cell->childView())
            cell->childView()->restoreOverrideCursor();
    }
}

#include "moc_ViewArea.cpp"
