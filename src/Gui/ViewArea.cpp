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
# include <cmath>
# include <map>
# include <string>
# include <QApplication>
# include <QCloseEvent>
# include <QElapsedTimer>
# include <QContextMenuEvent>
# include <QKeyEvent>
# include <QMdiSubWindow>
# include <QMenu>
# include <QMouseEvent>
# include <QTimer>
# include <QPainter>
# include <QPainterPath>
# include <QScrollBar>
# include <QSplitter>
# include <QVBoxLayout>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>

#include "ViewArea.h"
#include "ViewAreaCanvas.h"

#include "Application.h"
#include "Document.h"
#include "MainWindow.h"
#include "OpenViewParams.h"
#include "OverlayWidgets.h"
#include "View3DInventor.h"
#include "ViewPlacement.h"
#include "ViewProviderDocumentObject.h"

using namespace Gui;

namespace {

/** The ground of a piece of cell chrome that is shown -- the menu button
 * under the cursor, a corner zone: the accent colour of the drag frames
 * (OverlayDragFrame::accentColor) with a white rim, to be drawn on in
 * white. The chrome lies over whatever the cell shows, and a ground in
 * the window's colour with strokes in the accent could not be told from
 * a light grey or a white view -- a drawing page, a spreadsheet, a light
 * 3D background. Filled, it stands out on a light ground by its face and
 * on a dark or a like-coloured one by its rim.
 */
void paintChromeGround(QPainter &p, const QWidget *widget, int alpha = 255)
{
    QColor face = Gui::OverlayDragFrame::accentColor(widget);
    face.setAlpha(alpha);
    p.setPen(QPen(QColor(255, 255, 255, alpha), 1));
    p.setBrush(face);
    p.drawRoundedRect(QRectF(widget->rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
}

/// True while \a ev is the Escape key going down, or asking whether it
/// is a shortcut.
bool isEscape(const QEvent *ev)
{
    return (ev->type() == QEvent::KeyPress || ev->type() == QEvent::ShortcutOverride)
        && static_cast<const QKeyEvent*>(ev)->key() == Qt::Key_Escape;
}

} // namespace

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
        // Hovered, on a ground of its own like the corner zones; at rest
        // it stays the faint mark that does not compete with the scene.
        if (_hover)
            paintChromeGround(p, this);
        QColor c = _hover ? QColor(Qt::white) : palette().color(QPalette::WindowText);
        if (!_hover)
            c.setAlpha(90);
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
    static constexpr int Width = 2;

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
        // Subtle, and seen: the accent the drag frames have, not quite
        // opaque, two pixels. One pixel of the palette's highlight could
        // not be told from the border between cells under a dark theme.
        QPainter p(this);
        QColor accent = OverlayDragFrame::accentColor(this);
        accent.setAlpha(190);
        QPen pen(accent);
        pen.setWidth(Width);
        pen.setJoinStyle(Qt::MiterJoin);
        p.setPen(pen);
        p.drawRect(QRectF(rect()).adjusted(Width / 2.0, Width / 2.0, -Width / 2.0, -Width / 2.0));
    }

private:
    ViewAreaCell *_cell;
};

} // namespace Gui

namespace {

/** The frames of a drag under way (ViewArea::showDragFrames).
 *
 * One widget over the whole container, raised, masked to the frames it
 * draws so that it overlaps the 3D surfaces underneath by no more than
 * that. Each frame says what becomes of the place it covers:
 *   - a cell that stays, at the size it will have: a frame, the look
 *     of the overlay's drag frame (OverlayDragFrame::paintFrame) -- the
 *     accent colour, see-through, inside a white border;
 *   - the cell a split makes: the same frame, and a plus;
 *   - a cell that is closed -- a join's neighbor, the cell a border is
 *     pushed past its minimum: a red frame, crossed out, and the face of
 *     the frame of the cell that stays is left off it.
 * A split that is refused shows no frame at all: the cursor says so
 * (ViewAreaZone::armSplit).
 * What it shows is also set as dynamic properties -- "operation",
 * "frames" (rectangles) and "kinds" (words) -- which is how a test reads
 * a class that lives in this file.
 */
class ViewAreaDragFrames : public QWidget
{
public:
    explicit ViewAreaDragFrames(ViewArea *area)
        : QWidget(area)
    {
        setObjectName(QStringLiteral("ViewAreaDragFrames"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

    void setFrames(const char *operation, const QList<ViewArea::DragFrame> &list)
    {
        frames = list;
        static const char *words[] = {"kept", "fresh", "going", "refused"};
        QVariantList rects;
        QStringList kinds;
        QRegion region;
        for (const auto &frame : frames) {
            rects.append(frame.rect);
            kinds.append(QString::fromLatin1(words[frame.kind]));
            region += frame.rect;
        }
        setProperty("operation", QString::fromLatin1(operation));
        setProperty("frames", rects);
        setProperty("kinds", kinds);
        if (frames.isEmpty()) {
            hide();
            return;
        }
        setGeometry(parentWidget()->rect());
        setMask(region);
        show();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent = Gui::OverlayDragFrame::accentColor(this);
        for (const auto &frame : frames) {
            const QRect r = frame.rect.adjusted(1, 1, -1, -1);
            const QPointF c(r.center());
            const double s = qMin(qMin(r.width(), r.height()) / 6.0, 28.0);
            switch (frame.kind) {
            case ViewArea::DragFrame::Kept:
            case ViewArea::DragFrame::Fresh: {
                // Over a cell that goes the red frame is the one that
                // speaks: two faces one on the other are neither colour.
                QRegion own(frame.rect);
                for (const auto &other : frames) {
                    if (other.kind == ViewArea::DragFrame::Going)
                        own -= other.rect;
                }
                p.save();
                p.setClipRegion(own);
                Gui::OverlayDragFrame::paintFrame(p, frame.rect, accent);
                p.restore();
                if (frame.kind == ViewArea::DragFrame::Fresh) {
                    p.setRenderHint(QPainter::Antialiasing);
                    p.setPen(QPen(QColor(255, 255, 255, 220), 3));
                    p.drawLine(c - QPointF(s, 0), c + QPointF(s, 0));
                    p.drawLine(c - QPointF(0, s), c + QPointF(0, s));
                }
                break;
            }
            // A cell that is closed, and (not shown any more, see
            // armSplit) a split that is refused: red, crossed out. "I
            // feel this hints more like a close" -- it was the refusal's
            // look, and a stop sign the closing cell's.
            case ViewArea::DragFrame::Going:
            case ViewArea::DragFrame::Refused: {
                const QColor red(200, 40, 40);
                QColor fill = red;
                fill.setAlpha(70);
                p.setPen(QPen(red, 2));
                p.setBrush(fill);
                p.drawRect(r);
                p.setPen(QPen(red, 3));
                p.drawLine(c - QPointF(s, s), c + QPointF(s, s));
                p.drawLine(c - QPointF(s, -s), c + QPointF(s, -s));
                break;
            }
            }
        }
    }

private:
    QList<ViewArea::DragFrame> frames;
};

/// Splitter handle with a small management menu.
class ViewAreaSplitterHandle : public QSplitterHandle
{
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    // A drag of the border is shown, not carried out: frames over every
    // cell it changes, at the sizes they will have, and the border moves
    // when the button is released (docs/SplitViews.md sec 5.4). Qt's own
    // choice is between moving it at every mouse move -- a resize of
    // each 3D view per move -- and a rubber band that says nothing
    // about the cells.
    void mousePressEvent(QMouseEvent *ev) override
    {
        if (_dragging) {
            // Any other button while the left one is held gives the drag
            // up; only the release of the left button carries it out.
            cancelDrag();
            _swallowMenu = (ev->button() == Qt::RightButton);
            ev->accept();
            return;
        }
        if (ev->button() != Qt::LeftButton || !ViewArea::areaOf(splitter()))
            return QSplitterHandle::mousePressEvent(ev);
        const QPoint local = ev->position().toPoint();
        _grab = (orientation() == Qt::Horizontal) ? local.x() : local.y();
        _pos = (orientation() == Qt::Horizontal) ? x() : y();
        _dragging = true;
        _swallowMenu = false;
        // Escape goes to whatever has the keyboard, not to this handle
        qApp->installEventFilter(this);
        ev->accept();
    }
    /// Give the drag up: the frames go, the border stays where it was.
    void cancelDrag()
    {
        if (!_dragging)
            return;
        _dragging = false;
        _closing = nullptr;
        qApp->removeEventFilter(this);
        if (ViewArea *area = ViewArea::areaOf(splitter()))
            area->hideDragFrames();
    }
    bool eventFilter(QObject *watched, QEvent *ev) override
    {
        if (_dragging) {
            if (isEscape(ev)) {
                if (ev->type() == QEvent::KeyPress)
                    cancelDrag();
                ev->accept();
                return true;
            }
            // Another application coming to the front, or anything else
            // that takes the mouse away, ends the drag without a release
            // ever arriving here: the frames would stay on the screen.
            if (ev->type() == QEvent::ApplicationDeactivate
                    || (ev->type() == QEvent::WindowDeactivate && watched == window())
                    || (ev->type() == QEvent::UngrabMouse && watched == this))
                cancelDrag();
        }
        return QSplitterHandle::eventFilter(watched, ev);
    }
    void mouseMoveEvent(QMouseEvent *ev) override
    {
        if (!_dragging)
            return QSplitterHandle::mouseMoveEvent(ev);
        const QPoint p = splitter()->mapFromGlobal(ev->globalPosition().toPoint());
        int pos = ((orientation() == Qt::Horizontal) ? p.x() : p.y()) - _grab;
        _pos = closestLegalPosition(pos);
        _closing = nullptr;
        ViewArea *area = ViewArea::areaOf(splitter());
        if (!area) {
            ev->accept();
            return;
        }
        // Dragged on past what the cells on that side can give -- they are
        // at the minimum cell size -- the drag means closing the cell the
        // border is pushed into, and says so as a join does: the frame of
        // what takes its room, and a stop sign on it. A few pixels past
        // the limit are still the limit, or the two would flicker there.
        if (qAbs(pos - _pos) > CloseSlack) {
            const int idx = splitter()->indexOf(this);  // handle i sits after widget i-1
            const bool before = pos < _pos;
            QWidget *taker = splitter()->widget(before ? idx : idx - 1);
            _closing = qobject_cast<ViewAreaCell*>(splitter()->widget(before ? idx - 1 : idx));
            if (_closing && taker) {
                ViewArea::DragFrame gone;
                gone.rect = QRect(_closing->mapTo(area, QPoint(0, 0)), _closing->size());
                gone.kind = ViewArea::DragFrame::Going;
                gone.axis = orientation();
                gone.after = !before;
                ViewArea::DragFrame kept;
                kept.rect = QRect(taker->mapTo(area, QPoint(0, 0)), taker->size())
                                .united(gone.rect);
                area->showDragFrames("close", {kept, gone});
                ev->accept();
                return;
            }
        }
        area->showDragFrames("resize",
                area->resizeFrames(splitter(), splitter()->indexOf(this), _pos));
        ev->accept();
    }
    void mouseReleaseEvent(QMouseEvent *ev) override
    {
        if (!_dragging || ev->button() != Qt::LeftButton)
            return QSplitterHandle::mouseReleaseEvent(ev);
        const int pos = _pos;
        QPointer<ViewAreaCell> closing = _closing;
        ViewArea *area = ViewArea::areaOf(splitter());
        cancelDrag();
        ev->accept();
        // closing a cell may delete this handle with its splitter: last
        if (closing && area)
            area->closeCell(closing);
        else
            moveSplitter(pos);
    }
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
        if (_swallowMenu) {
            // the right click that gave a drag up is not a call for the menu
            _swallowMenu = false;
            ev->accept();
            return;
        }
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

private:
    bool _dragging = false;
    bool _swallowMenu = false;  // the right click that cancelled a drag
    int _grab = 0;  // where in the handle it was taken
    int _pos = 0;   // where the border will go, in the splitter
    /// How far past the limit a drag goes before it means closing a cell
    static constexpr int CloseSlack = 12;
    /// The cell the release will close, the border pushed past its minimum
    QPointer<ViewAreaCell> _closing;
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
    // Thinner than the style's or the stylesheet's splitter (5 to 7
    // pixels): a border between views, still wide enough to take and to
    // right-click for its menu.
    setHandleWidth(HandleWidth);
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

QSize ViewAreaCell::minimumSizeHint() const
{
    const int least = static_cast<int>(OpenViewParams::getMinimumCellSize());
    if (least <= 0)
        return {24, 24};
    return {qMin(least, 400), qMin(least, 300)};
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
    placeChrome();
    update();
}

void ViewAreaCell::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    placeChrome();
    _highlight->refit();
    _highlight->update();
}

void ViewAreaCell::placeChrome()
{
    const int z = ViewAreaZone::Size;
    // A view that scrolls -- a drawing page, a spreadsheet -- has its bars
    // along the right and the bottom edge, which is where the two corner
    // zones are: on top of a bar's arrow a zone can neither be seen well
    // nor the arrow be reached. Each steps aside by the bar it would lie
    // on.
    int right = 0;
    int bottom = 0;
    if (_child) {
        const QRect topRight(width() - z, 0, z, z);
        const QRect bottomLeft(0, height() - z, z, z);
        const auto bars = _child->findChildren<QScrollBar*>();
        for (QScrollBar *bar : bars) {
            // told when one comes or goes (eventFilter); once is enough,
            // Qt keeps a filter installed twice only once
            bar->installEventFilter(this);
            if (!bar->isVisibleTo(this))
                continue;
            const QRect at(bar->mapTo(this, QPoint(0, 0)), bar->size());
            // only a bar the zone's corner would lie on: a view's bars
            // need not run into the cell's corners (a spreadsheet's stand
            // in from the edge and begin below its header)
            if (bar->orientation() == Qt::Vertical) {
                if (at.intersects(topRight) && at.width() < width() / 2)
                    right = qMax(right, width() - at.left());
            }
            else if (at.intersects(bottomLeft) && at.height() < height() / 2) {
                bottom = qMax(bottom, height() - at.top());
            }
        }
    }
    _zoneTopRight->setGeometry(width() - z - right, 0, z, z);
    _zoneBottomLeft->setGeometry(0, height() - z - bottom, z, z);
    _menuButton->setGeometry(0, 0, ViewAreaMenuButton::Size,
                             ViewAreaMenuButton::Size);
}

bool ViewAreaCell::eventFilter(QObject *watched, QEvent *ev)
{
    switch (ev->type()) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Resize:
    case QEvent::Move:
        if (qobject_cast<QScrollBar*>(watched)) {
            // after the view has laid the bar out, not in the middle of it
            QTimer::singleShot(0, this, &ViewAreaCell::placeChrome);
        }
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, ev);
}

void ViewAreaCell::enterEvent(QEnterEvent *ev)
{
    QWidget::enterEvent(ev);
    // a view makes its scroll bars when it pleases: look again whenever
    // the cursor comes in, which is before a zone can be reached
    placeChrome();
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
    // pages and spreadsheets by type, open or not.
    Gui::Document *doc = area->getGuiDocument();
    MDIView *child = _child;
    QAction *act3d = nullptr;
    // The object views that are open, by the object each one is of. Read
    // off the views, which carry their object's name as their own, and
    // NOT by asking every view provider for its view: a spreadsheet's
    // answers by making one, so that opening this menu opened a
    // spreadsheet -- into the nearest non-3D cell, closing what was
    // there -- and every TechDraw view object answers with its page's,
    // which listed dimensions and details as if a cell could show them.
    std::map<std::string, MDIView*> objectViews;
    if (doc) {
        for (auto view : doc->getMDIViews()) {
            if (qobject_cast<View3DInventor*>(view) || qobject_cast<ViewArea*>(view))
                continue;
            const QByteArray name = view->objectName().toUtf8();
            if (!name.isEmpty() && doc->getDocument()->getObject(name.constData()))
                objectViews.emplace(name.constData(), view);
        }
        act3d = menu.addAction(tr("3D view"));
        act3d->setCheckable(true);
        act3d->setChecked(qobject_cast<View3DInventor*>(child) != nullptr);
        // The kinds of object that have a view of their own to show, open
        // or not: by NAME, so that Gui links to neither module, and a
        // module not loaded has no objects to list anyway.
        std::vector<Base::Type> viewTypes;
        for (const char *typeName : {"TechDraw::DrawPage", "Spreadsheet::Sheet"}) {
            const Base::Type type = Base::Type::fromName(typeName);
            if (type != Base::Type::badType())
                viewTypes.push_back(type);
        }
        for (auto obj : doc->getDocument()->getObjects()) {
            auto vp = dynamic_cast<ViewProviderDocumentObject*>(
                    Application::Instance->getViewProvider(obj));
            if (!vp)
                continue;
            auto found = objectViews.find(obj->getNameInDocument());
            MDIView *objView = found == objectViews.end() ? nullptr : found->second;
            bool typed = false;
            for (const auto &type : viewTypes)
                typed = typed || obj->getTypeId().isDerivedFrom(type);
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
        auto found = objectViews.find(objName.constData());
        MDIView *view = found == objectViews.end() ? nullptr : found->second;
        if (!view) {
            // Not open yet: opened for THIS cell. Left to the placement
            // policy it went into the last non-3D cell instead, and the
            // cell asked could not have it any more.
            ViewPlacement::IntoCell here(area, this);
            view = vp->getOrCreateMDIView();
        }
        if (!view || view == childView())
            return;
        if (!area->setCellView(this, view)) {
            // It sits in another cell, which keeps it: go there
            getMainWindow()->setActiveWindow(view);
        }
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
    if (_dragging) {
        // Any other button while the left one is held gives the drag up;
        // only the release of the left button carries it out.
        cancelDrag();
        ev->accept();
        return;
    }
    if (ev->button() != Qt::LeftButton)
        return QWidget::mousePressEvent(ev);
    _dragging = true;
    _pressGlobal = ev->globalPosition().toPoint();
    // Escape goes to whatever has the keyboard, not to this zone
    qApp->installEventFilter(this);
    ev->accept();
}

void ViewAreaZone::cancelDrag()
{
    if (_dragging)
        endDrag();
}

bool ViewAreaZone::eventFilter(QObject *watched, QEvent *ev)
{
    if (_dragging) {
        if (isEscape(ev)) {
            if (ev->type() == QEvent::KeyPress)
                cancelDrag();
            ev->accept();
            return true;
        }
        // Another application coming to the front, or anything else that
        // takes the mouse away, ends the drag without a release ever
        // arriving here: the frames would stay on the screen.
        if (ev->type() == QEvent::ApplicationDeactivate
                || (ev->type() == QEvent::WindowDeactivate && watched == window())
                || (ev->type() == QEvent::UngrabMouse && watched == this))
            cancelDrag();
    }
    return QWidget::eventFilter(watched, ev);
}

void ViewAreaZone::contextMenuEvent(QContextMenuEvent *ev)
{
    // A zone has no menu, and the right click that gave a drag up is not
    // a call for the menu of whatever the zone lies on.
    ev->accept();
}

void ViewAreaZone::mouseMoveEvent(QMouseEvent *ev)
{
    if (!_dragging)
        return QWidget::mouseMoveEvent(ev);
    QPoint g = ev->globalPosition().toPoint();

    // Nothing is split, joined or resized while the button is down: the
    // drag is shown as frames over the cells it will change, and
    // carried out at the release (docs/SplitViews.md sec 5.4).
    QPoint d = g - _pressGlobal;
    ViewArea *area = _cell->area();
    QRect cellRect(_cell->mapToGlobal(QPoint(0, 0)), _cell->size());
    if (cellRect.contains(g)) {
        // Back inside always cancels an armed join, even right at the
        // press point where the split threshold below is not met.
        disarmJoin();
        // ... and back at the press point cancels an armed split.
        if (d.manhattanLength() < 12) {
            disarmSplit();
            return;
        }
        // Inward drag: a split along the dominant axis, the new border
        // under the cursor.
        armSplit(g, d);
    }
    else {
        disarmSplit();
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
    if (ev->button() != Qt::LeftButton) {
        ev->accept();
        return;
    }
    ViewAreaCell *target = _joinTarget;
    ViewArea *area = _cell->area();
    // a refused split has said so already, when the cursor turned
    const bool split = _splitArmed && !_splitRefused;
    const Qt::Orientation orientation = _splitOrientation;
    const int at = _splitAt;
    endDrag();
    if (target) {
        area->closeCell(target);
    }
    else if (split) {
        area->splitCell(_cell, orientation, nullptr, at);
    }
    ev->accept();
}

void ViewAreaZone::armJoin(ViewAreaCell *target, Qt::Orientation axis, bool after)
{
    _joinTarget = target;
    // The cell that stays, over the room it will have, which takes in
    // the cell that goes; and that one, which is drawn red and crossed
    // out.
    ViewArea *area = _cell->area();
    const QRect source(_cell->mapTo(area, QPoint(0, 0)), _cell->size());
    const QRect going(target->mapTo(area, QPoint(0, 0)), target->size());
    ViewArea::DragFrame kept;
    kept.rect = source.united(going);
    ViewArea::DragFrame gone;
    gone.rect = going;
    gone.kind = ViewArea::DragFrame::Going;
    gone.axis = axis;
    gone.after = after;
    area->showDragFrames("join", {kept, gone});
}

void ViewAreaZone::disarmJoin()
{
    if (_joinTarget)
        _cell->area()->hideDragFrames();
    _joinTarget = nullptr;
}

void ViewAreaZone::armSplit(const QPoint &global, const QPoint &delta)
{
    ViewArea *area = _cell->area();
    _splitArmed = true;
    _splitOrientation = (qAbs(delta.x()) >= qAbs(delta.y()))
        ? Qt::Horizontal : Qt::Vertical;
    const auto frames = area->splitFrames(_cell, _splitOrientation, global, &_splitAt);
    const bool refused = (frames.size() == 1);
    if (refused) {
        // A split that cannot be: no frame, the forbidden cursor, and the
        // reason said the moment the cursor turns -- at every turn from
        // the splitting cursor to this one, so once for a drag that stays
        // refused. (It was a red frame, crossed out; that look now means
        // a cell that is closed.)
        area->hideDragFrames();
        if (!_splitRefused) {
            setCursor(Qt::ForbiddenCursor);
            area->reportRefusedSplit(_cell, _splitOrientation, true);
        }
    }
    else {
        if (_splitRefused)
            setCursor(Qt::CrossCursor);
        area->showDragFrames("split", frames);
    }
    _splitRefused = refused;
}

void ViewAreaZone::disarmSplit()
{
    if (_splitArmed)
        _cell->area()->hideDragFrames();
    if (_splitRefused)
        setCursor(Qt::CrossCursor);
    _splitArmed = false;
    _splitRefused = false;
}

void ViewAreaZone::endDrag()
{
    disarmJoin();
    disarmSplit();
    _dragging = false;
    qApp->removeEventFilter(this);
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
    // On a ground of its own: a few strokes straight onto whatever the
    // cell shows cannot be seen over a busy or a like-coloured view.
    // A pointed-out zone is fainter than one under the cursor.
    const int alpha = _hover ? 255 : 150;
    paintChromeGround(p, this, alpha);
    QPen pen(QColor(255, 255, 255, alpha));
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
    // A view there is only one of -- a drawing page, a spreadsheet: the
    // new cell cannot show it a second time, and a split that did nothing
    // and said nothing was what that came to. It gets a 3D view of the
    // same document instead, at the camera of the one the document has.
    if (Gui::Document *doc = child->getGuiDocument()) {
        // createView3D is written for a document's first view and marks
        // the document unmodified; a split is not a save
        const bool modified = doc->isModified();
        MDIView *view = doc->createView3D();
        if (modified)
            doc->setModified(true);
        return view;
    }
    return nullptr;
}

ViewAreaCell *ViewArea::splitCell(ViewAreaCell *cell, Qt::Orientation orientation,
                                  MDIView *newChild, int share)
{
    if (!cell || cell->area() != this)
        return nullptr;
    // Asked of the cell as it stands: one maximized is judged at the
    // size it has on screen, its own share being unknown until the
    // layout it comes back to has been done.
    if (!canSplitCell(cell, orientation, true))
        return nullptr;
    if (_maximizedCell) {
        toggleMaximizeCell(_maximizedCell);
        share = -1;  // measured on the maximized cell
    }
    MDIView *child = newChild ? newChild : cloneChildFor(cell);
    if (!child)
        return nullptr;

    auto splitter = qobject_cast<QSplitter*>(cell->parentWidget());
    assert(splitter);
    auto newCell = new ViewAreaCell(this);
    int idx = splitter->indexOf(cell);
    // What the cell keeps and what the new one gets, of the cell's
    // extent less the border that comes between them.
    auto shares = [share](int extent) {
        const int room = qMax(extent - ViewAreaSplitter::HandleWidth, 2);
        const int first = share < 0 ? room - room / 2 : qBound(1, share, room - 1);
        return QList<int>{first, room - first};
    };

    if (splitter->count() < 2 || splitter->orientation() == orientation) {
        // Same direction (or a splitter that has not committed to one
        // yet): insert as a sibling, dividing the split cell's share.
        if (splitter->count() < 2)
            splitter->setOrientation(orientation);
        QList<int> sizes = splitter->sizes();
        splitter->insertWidget(idx + 1, newCell);
        if (idx < sizes.size()) {
            const QList<int> two = shares(sizes[idx]);
            sizes[idx] = two[0];
            sizes.insert(idx + 1, two[1]);
            splitter->setSizes(sizes);
        }
    }
    else {
        // Crossing direction: nest a new splitter in the cell's place.
        QList<int> sizes = splitter->sizes();
        auto nested = new ViewAreaSplitter(orientation);
        const QList<int> two = shares(orientation == Qt::Horizontal ? cell->width()
                                                                    : cell->height());
        // insertWidget, then the cell moved into the nested splitter:
        // replaceWidget would take the cell out of the window on its way
        // there (see collapseCell on what that costs a 3D view).
        splitter->insertWidget(idx, nested);
        nested->addWidget(cell);
        cell->show();
        nested->addWidget(newCell);
        nested->show();
        splitter->setSizes(sizes);
        nested->setSizes(two);
        // ... and once more when the nested splitter has its real place:
        // sizes set before that are handed out again in equal shares
        // (see ViewAreaSplitter::initialSizes).
        if (share >= 0 && (nested->width() <= 0 || nested->height() <= 0
                           || !nested->isVisible()))
            nested->initialSizes = two;
    }

    newCell->hostView(child);
    setActiveCell(cell, false);
    syncCanvas();
    return newCell;
}

bool ViewArea::canSplitCell(const ViewAreaCell *cell, Qt::Orientation orientation,
                            bool report) const
{
    if (!cell)
        return false;
    const int least = static_cast<int>(OpenViewParams::getMinimumCellSize());
    // No geometry yet -- a layout coming back with its document, a
    // container not shown: there is nothing to measure, and a saved
    // layout is not refused for the size of the window it returns to.
    if (least <= 0 || !cell->isVisible() || cell->width() <= 0 || cell->height() <= 0)
        return true;
    const bool horiz = (orientation == Qt::Horizontal);
    const int along = horiz ? cell->width() : cell->height();
    const int across = horiz ? cell->height() : cell->width();
    // Both halves, and the side the new cell inherits: "any existing (or
    // the new) view" is not to fall below the limit.
    if ((along - ViewAreaSplitter::HandleWidth) / 2 >= least && across >= least)
        return true;
    if (report)
        reportRefusedSplit(cell, orientation, false);
    return false;
}

void ViewArea::reportRefusedSplit(const ViewAreaCell *cell, Qt::Orientation orientation,
                                  bool always) const
{
    if (!cell)
        return;
    // Said as an ERROR -- a line of warning in the report view was not
    // seen by the one whose drag did nothing; an error is shown by the
    // notification area as well. (Not a translated message for the user
    // alone: the report view takes none of those.)
    //
    // A corner drag says it each time its cursor turns to the forbidden
    // one (`always`). A command or a view opening by itself says it once
    // in a while only: a script asks in a loop.
    static QElapsedTimer last;
    if (!always && last.isValid() && last.elapsed() <= 5000)
        return;
    last.start();
    const int least = static_cast<int>(OpenViewParams::getMinimumCellSize());
    Base::Console().Error(
        "A view of %d x %d is not split %s: no view cell is made smaller than "
        "%d x %d (the minimum view cell size, in the preferences).\n",
        cell->width(), cell->height(),
        orientation == Qt::Horizontal ? "side by side" : "top and bottom", least, least);
}

QList<ViewArea::DragFrame> ViewArea::splitFrames(ViewAreaCell *cell,
                                                 Qt::Orientation orientation,
                                                 const QPoint &global, int *at) const
{
    QList<DragFrame> frames;
    if (!cell || cell->area() != this)
        return frames;
    auto self = const_cast<ViewArea*>(this);
    const QRect place(cell->mapTo(self, QPoint(0, 0)), cell->size());
    DragFrame kept;
    kept.rect = place;
    if (!canSplitCell(cell, orientation)) {
        kept.kind = DragFrame::Refused;
        frames.append(kept);
        return frames;
    }
    const bool horiz = (orientation == Qt::Horizontal);
    const int handle = ViewAreaSplitter::HandleWidth;
    const int extent = horiz ? place.width() : place.height();
    // The border follows the cursor as far as the minimum cell size
    // lets it; with no minimum set, as far as a cell can still be seen.
    const int least = qMax(static_cast<int>(OpenViewParams::getMinimumCellSize()), 24);
    const QPoint local = cell->mapFromGlobal(global);
    int lo = least;
    int hi = extent - handle - least;
    if (hi < lo)
        lo = hi = (extent - handle) / 2;
    const int border = qBound(lo, horiz ? local.x() : local.y(), hi);
    if (at)
        *at = border;
    DragFrame fresh;
    fresh.kind = DragFrame::Fresh;
    if (horiz) {
        kept.rect.setWidth(border);
        fresh.rect = QRect(place.left() + border + handle, place.top(),
                           extent - border - handle, place.height());
    }
    else {
        kept.rect.setHeight(border);
        fresh.rect = QRect(place.left(), place.top() + border + handle,
                           place.width(), extent - border - handle);
    }
    frames.append(kept);
    frames.append(fresh);
    return frames;
}

namespace {

/// The least a splitter gives a widget along its axis (QSplitter's own
/// notion, near enough for a preview: a set minimum, else the hint).
int leastExtent(const QWidget *w, bool horiz)
{
    const QSize set = w->minimumSize();
    const QSize hint = w->minimumSizeHint();
    const int least = horiz ? (set.width() > 0 ? set.width() : hint.width())
                            : (set.height() > 0 ? set.height() : hint.height());
    return qMax(least, 0);
}

/// The frames of every cell under \a w once \a w has the place \a place
/// (in the container's coordinates): a cell that changes gets one; a
/// nested splitter hands the change on -- to all its children across
/// its axis, in proportion along it, as QSplitter does on a resize.
void collectFrames(ViewArea *area, QWidget *w, const QRect &place,
                   QList<ViewArea::DragFrame> &frames)
{
    if (auto cell = qobject_cast<ViewAreaCell*>(w)) {
        const QRect now(cell->mapTo(area, QPoint(0, 0)), cell->size());
        if (place != now) {
            ViewArea::DragFrame frame;
            frame.rect = place;
            frames.append(frame);
        }
        return;
    }
    auto sp = qobject_cast<QSplitter*>(w);
    if (!sp)
        return;
    const bool horiz = (sp->orientation() == Qt::Horizontal);
    QList<QWidget*> shown;
    for (int i = 0; i < sp->count(); ++i) {
        if (!sp->widget(i)->isHidden())
            shown.append(sp->widget(i));
    }
    if (shown.isEmpty())
        return;
    const int handles = sp->handleWidth() * (shown.size() - 1);
    const int before = (horiz ? sp->width() : sp->height()) - handles;
    const int after = (horiz ? place.width() : place.height()) - handles;
    const double scale = before > 0 ? double(after) / before : 1.0;
    int at = 0;
    int left = after;
    for (int i = 0; i < shown.size(); ++i) {
        QWidget *child = shown[i];
        // Changed across its axis only, the splitter keeps every child's
        // share to the pixel; along it, the last takes what rounding left.
        int extent = (after == before || i < shown.size() - 1)
            ? qRound((horiz ? child->width() : child->height()) * scale) : left;
        left -= extent;
        const QRect r = horiz
            ? QRect(place.left() + at, place.top(), extent, place.height())
            : QRect(place.left(), place.top() + at, place.width(), extent);
        collectFrames(area, child, r, frames);
        at += extent + sp->handleWidth();
    }
}

} // anonymous namespace

QList<ViewArea::DragFrame> ViewArea::resizeFrames(const QSplitter *sp, int index,
                                                  int pos) const
{
    QList<DragFrame> frames;
    if (!sp || index <= 0 || index >= sp->count())
        return frames;
    const bool horiz = (sp->orientation() == Qt::Horizontal);
    const int n = sp->count();
    const QSplitterHandle *handle = sp->handle(index);
    const int delta = pos - (horiz ? handle->x() : handle->y());
    if (delta == 0)
        return frames;

    QList<int> extent;
    QList<int> least;
    for (int i = 0; i < n; ++i) {
        const QWidget *w = sp->widget(i);
        const bool hidden = w->isHidden();
        extent.append(hidden ? 0 : (horiz ? w->width() : w->height()));
        least.append(hidden ? 0 : leastExtent(w, horiz));
    }
    // The side the border moves into gives way from the border outwards,
    // each widget down to its least before the next one is pushed; what
    // they give, the widget on the other side of the border takes.
    const int want = qAbs(delta);
    int given = 0;
    const int step = delta > 0 ? 1 : -1;
    for (int i = (delta > 0 ? index : index - 1); i >= 0 && i < n && given < want; i += step) {
        const int give = qMin(want - given, qMax(extent[i] - least[i], 0));
        extent[i] -= give;
        given += give;
    }
    extent[delta > 0 ? index - 1 : index] += given;

    auto self = const_cast<ViewArea*>(this);
    int at = -1;
    for (int i = 0; i < n; ++i) {
        QWidget *w = sp->widget(i);
        if (w->isHidden())
            continue;
        const QRect now = w->geometry();
        if (at < 0)
            at = horiz ? now.left() : now.top();
        QRect then = now;
        if (horiz) {
            then.moveLeft(at);
            then.setWidth(extent[i]);
        }
        else {
            then.moveTop(at);
            then.setHeight(extent[i]);
        }
        at += extent[i] + sp->handleWidth();
        if (then != now)
            collectFrames(self, w, QRect(sp->mapTo(self, then.topLeft()), then.size()), frames);
    }
    return frames;
}

void ViewArea::showDragFrames(const char *operation, const QList<DragFrame> &frames)
{
    if (frames.isEmpty() && !_dragFrames)
        return;
    if (!_dragFrames)
        _dragFrames = new ViewAreaDragFrames(this);
    static_cast<ViewAreaDragFrames*>(_dragFrames.data())->setFrames(operation, frames);
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

bool ViewArea::removeView(MDIView *view)
{
    ViewAreaCell *cell = cellOf(view);
    if (!cell)
        return false;
    // The container is going down and takes its cells and their views
    // with it (~ViewAreaCell).
    if (_closing)
        return true;
    if (_maximizedCell)
        toggleMaximizeCell(_maximizedCell);
    // Out of the cell first: releaseView drops the `destroyed`
    // connection that would collapse the cell later, and the collapse
    // is done here instead, so that the active view is a live one again
    // by the time the caller goes on -- as it is after an MDI tab was
    // removed. A view left active for one more turn of the event loop
    // is one whose view provider may already be freed.
    const bool wasActive = (getMainWindow()->activeWindow() == view);
    cell->releaseView();
    view->hide();
    view->deleteLater();
    if (cellCount() > 1) {
        childViewGone(cell);
    }
    else {
        // The last cell stays, empty, with its menu to fill it again.
        // Closing the container instead (what childViewGone does when a
        // last child is destroyed) closes the last view of its document
        // and with that the document -- for a page that was only hidden.
        cell->update();
        if (wasActive)
            getMainWindow()->setActiveWindow(this);
    }
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
        // Not QSplitter::replaceWidget(): it takes the widget it replaces
        // out of the window FIRST, and here that is `s` with the lone cell
        // still inside it. A QOpenGLWidget that leaves its window drops
        // the texture the window composes it from, and one that is
        // already initialized gets it back only at its next resize -- so
        // the view that took the closed cell's room was drawn, correctly,
        // into a framebuffer nothing put on the screen: black until the
        // user resized it (docs/HandsOnQueue.md entry 60). Moved straight
        // into the parent the cell never leaves the window.
        parent->insertWidget(idx, lone);
        lone->show();
        s->hide();
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
