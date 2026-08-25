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
# include <QApplication>
# include <QCloseEvent>
# include <QMdiSubWindow>
# include <QPainter>
# include <QSplitter>
# include <QVBoxLayout>
#endif

#include "ViewArea.h"

#include "Application.h"
#include "Document.h"
#include "MainWindow.h"
#include "View3DInventor.h"

using namespace Gui;

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
}

ViewAreaCell::~ViewAreaCell() = default;

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
    // an embedded view still reports to the status bar.
    QObject::connect(view, &MDIView::message,
                     getMainWindow(), &MainWindow::showMessage);
    // Qt-level destruction of the child (e.g. the document is closing
    // and deleteSelf ran) collapses the cell.
    QPointer<ViewAreaCell> self(this);
    ViewArea *area = _area;
    QObject::connect(view, &QObject::destroyed, area, [area, self]() {
        if (self)
            area->childViewGone(self);
    });
    update();
}

MDIView *ViewAreaCell::releaseView()
{
    MDIView *view = _child;
    if (!view)
        return nullptr;
    QObject::disconnect(view, nullptr, _area, nullptr);
    QObject::disconnect(view, &MDIView::message,
                        getMainWindow(), &MainWindow::showMessage);
    layout()->removeWidget(view);
    view->setParent(nullptr);
    _child = nullptr;
    return view;
}

void ViewAreaCell::paintEvent(QPaintEvent *ev)
{
    QWidget::paintEvent(ev);
    if (_area && _area->activeCell() == this && _area->cellCount() > 1) {
        QPainter p(this);
        QPen pen(palette().color(QPalette::Highlight));
        pen.setWidth(1);
        p.setPen(pen);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

// ----------------------------------------------------------------------------
// ViewArea
// ----------------------------------------------------------------------------

PROPERTY_SOURCE_ABSTRACT(Gui::ViewArea, Gui::MDIView)

ViewArea::ViewArea(Gui::Document* pcDocument, QWidget* parent, Qt::WindowFlags wflags)
    : MDIView(pcDocument, parent, wflags)
{
    _rootSplitter = new QSplitter(Qt::Horizontal, this);
    _rootSplitter->setChildrenCollapsible(false);
    setCentralWidget(_rootSplitter);

    auto cell = new ViewAreaCell(this);
    _rootSplitter->addWidget(cell);
    _activeCell = cell;

    connect(qApp, &QApplication::focusChanged, this, &ViewArea::onFocusChanged);
}

ViewArea::~ViewArea()
{
    _closing = true;
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

ViewArea *ViewArea::wrap(MDIView *view)
{
    if (!view)
        return nullptr;
    auto sub = qobject_cast<QMdiSubWindow*>(view->parentWidget());
    if (!sub)
        return nullptr;

    auto mw = getMainWindow();
    bool wasActive = (mw->activeWindow() == view);

    auto area = new ViewArea(view->getGuiDocument(), mw);
    area->setWindowTitle(view->windowTitle());
    area->setWindowIcon(view->windowIcon());

    // Undo the addWindow plumbing; hostView re-establishes what an
    // embedded view needs. removeWindow also detaches the sub window
    // from the MDI area without deleting the view.
    mw->removeWindow(view, false);
    sub->setWidget(nullptr);
    view->setParent(nullptr);
    sub->deleteLater();

    area->activeCell()->hostView(view);
    mw->addWindow(area);
    if (wasActive)
        mw->setActiveWindow(view);
    return area;
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

MDIView *ViewArea::cloneChildFor(ViewAreaCell *cell)
{
    MDIView *child = cell->childView();
    if (!child)
        return nullptr;
    if (auto view3d = qobject_cast<View3DInventor*>(child)) {
        Gui::Document *doc = view3d->getGuiDocument();
        if (!doc)
            return nullptr;
        MDIView *clone = doc->cloneView(view3d);
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
        auto nested = new QSplitter(orientation);
        nested->setChildrenCollapsible(false);
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
    return newCell;
}

bool ViewArea::closeCell(ViewAreaCell *cell)
{
    if (!cell || cell->area() != this)
        return false;
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
        if (old)
            old->update();
        if (cell)
            cell->update();
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

void ViewArea::deleteSelf()
{
    _closing = true;
    MDIView::deleteSelf();
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
