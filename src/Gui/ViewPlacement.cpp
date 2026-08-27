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

#include <App/Application.h>
#include <Base/Parameter.h>

#include "ViewPlacement.h"
#include "Document.h"
#include "MDIView.h"
#include "MainWindow.h"
#include "View3DInventor.h"
#include "ViewArea.h"

using namespace Gui;

namespace {

enum class Target { Tab, Split, NewSplit, Floating };

ParameterGrp::handle openViewGroup()
{
    return App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View/OpenView");
}

bool useViewArea()
{
    return App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/View")
        ->GetBool("UseViewArea", true);
}

Target targetFor(ViewPlacement::Category cat)
{
    const char *key = "DocViewTarget";
    const char *def = "Split";
    switch (cat) {
    case ViewPlacement::Category::Document:
        key = "DocumentTarget";
        def = "Tab";
        break;
    case ViewPlacement::Category::DocView:
        break;
    case ViewPlacement::Category::Utility:
        key = "UtilityTarget";
        def = "Tab";
        break;
    }
    std::string v = openViewGroup()->GetASCII(key, def);
    if (v == "Split")
        return Target::Split;
    if (v == "NewSplit")
        return Target::NewSplit;
    if (v == "Floating")
        return Target::Floating;
    return Target::Tab;
}

// Split orientation for a new split of \a cell: the preference, with
// Auto picking the cell's longer side (docs/ViewPlacement.md sec 4.1).
Qt::Orientation splitDirection(const ViewAreaCell *cell)
{
    std::string v = openViewGroup()->GetASCII("SplitDirection", "Auto");
    if (v == "Right")
        return Qt::Horizontal;
    if (v == "Down")
        return Qt::Vertical;
    return (!cell || cell->width() >= cell->height()) ? Qt::Horizontal
                                                      : Qt::Vertical;
}

bool is3DView(const MDIView *view)
{
    return view->isDerivedFrom(View3DInventor::getClassTypeId());
}

// A candidate area only counts once it hangs in the MDI area: layout
// restore hosts views into a ViewArea it is still assembling (added to
// the MDI area only afterwards), and the policy must not join --
// let alone mutate -- a half-built tree.
ViewArea *mdiHosted(ViewArea *area)
{
    return (area && area->parentWidget()) ? area : nullptr;
}

// The area that hosts (or can start hosting) the request's views.
ViewArea *hostArea(ViewPlacement::Category cat, Gui::Document *doc)
{
    if (cat == ViewPlacement::Category::Document || !doc) {
        // Cross-document by explicit choice (Document=Split): the new
        // document's first view joins the CURRENT area, wherever it
        // belongs (docs/ViewPlacement.md sec 3.3). Only an existing
        // area qualifies -- another document's plain tab is not
        // promoted behind the user's back.
        auto active = getMainWindow()->activeWindow();
        return active ? mdiHosted(ViewArea::areaOf(active)) : nullptr;
    }
    if (auto view = doc->getActiveView()) {
        if (auto area = mdiHosted(ViewArea::areaOf(view)))
            return area;
    }
    for (auto view : doc->getMDIViews()) {
        if (auto area = mdiHosted(ViewArea::areaOf(view)))
            return area;
    }
    // The document's 3D view sits in a bare tab (UseViewArea was off
    // when it was made): promote it, the same step splitActiveView
    // performs for the split commands.
    if (useViewArea()) {
        for (auto view : doc->getMDIViews()) {
            if (!is3DView(view))
                continue;
            if (auto area = ViewArea::wrap(view))
                return area;
        }
    }
    return nullptr;
}

// docs/ViewPlacement.md sec 3.2 steps 5-7. False = fall back to a tab.
bool placeInArea(ViewArea *area, MDIView *view, Target target)
{
    if (auto mc = area->maximizedCell())
        area->toggleMaximizeCell(mc);

    // The reuse step: strictly non-3D content replacing non-3D content
    // -- a 3D view's cell is never taken, and a new 3D view always
    // splits (ruled 2026-08-28).
    if (target == Target::Split && !is3DView(view)) {
        if (auto cell = area->lastUsedCell([](MDIView *child) {
                return child && !is3DView(child);
            })) {
            if (area->setCellView(cell, view))
                return true;
            // The sitting child refused to close: split instead of
            // fighting the veto.
        }
    }

    auto active = area->activeCell();
    if (!active)
        return false;
    if (!area->splitCell(active, splitDirection(active), view))
        return false;
    area->activateCellOf(view);
    return true;
}

} // anonymous namespace

void ViewPlacement::placeTab(MDIView *view, Gui::Document *doc)
{
    if (!view)
        return;
    if (is3DView(view) && useViewArea()) {
        // The default viewer window is a split view container holding
        // the 3D view as its first cell (docs/SplitViews.md sec 5.6),
        // so the user can split it without a wrap step.
        auto area = new ViewArea(doc, getMainWindow());
        area->setWindowTitle(view->windowTitle());
        area->setWindowModified(doc ? doc->isModified()
                                    : view->isWindowModified());
        area->setWindowIcon(view->windowIcon());
        area->resize(400, 300);
        area->activeCell()->hostView(view);
        getMainWindow()->addWindow(area);
    }
    else
        getMainWindow()->addWindow(view);
}

void ViewPlacement::place(MDIView *view, Category cat, Gui::Document *doc)
{
    if (!view)
        return;
    Target target = targetFor(cat);

    if (target == Target::Floating) {
        // Not through placeTab: a floating 3D view must not be wrapped
        // in a ViewArea it is about to leave.
        getMainWindow()->addWindow(view);
        view->setCurrentViewMode(MDIView::TopLevel);
        getMainWindow()->setActiveWindow(view);
        return;
    }

    if (target == Target::Split || target == Target::NewSplit) {
        if (auto area = hostArea(cat, doc)) {
            if (placeInArea(area, view, target))
                return;
        }
        // No area to join (or the split was refused): a tab.
    }

    placeTab(view, doc);
    getMainWindow()->setActiveWindow(view);
}
