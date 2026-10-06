/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
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

#include "TaskOwner.h"
#include "MDIView.h"
#include "MainWindow.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "ViewerContext.h"

using namespace Gui;

namespace
{
/// The MDIView a desktop viewer sits in, or null: a client's mirror has
/// no widget at all, and a viewer can be made outside any view.
MDIView* viewOf(ViewerContext* context)
{
    auto viewer = dynamic_cast<View3DInventorViewer*>(context);
    if (!viewer) {
        return nullptr;
    }
    for (QWidget* w = viewer->parentWidget(); w; w = w->parentWidget()) {
        if (auto view = qobject_cast<MDIView*>(w)) {
            return view;
        }
    }
    return nullptr;
}
}  // namespace

TaskOwner::TaskOwner(MDIView* mdi)
{
    if (mdi) {
        mdi = mdi->activeSubView();
    }
    if (mdi) {
        kind = Kind::View;
        view = mdi;
        viewKey = mdi;
    }
}

TaskOwner::TaskOwner(ViewerContext* context)
{
    if (!context) {
        return;
    }
    if (MDIView* mdi = viewOf(context)) {
        *this = TaskOwner(mdi);
        return;
    }
    kind = Kind::Context;
    ctx = context;
    life = context->lifetime();
}

TaskOwner TaskOwner::current()
{
    if (ViewerContext* context = ViewerContext::current()) {
        return TaskOwner(context);
    }
    if (MainWindow* mw = getMainWindow()) {
        return TaskOwner(mw->activeWindow());
    }
    return {};
}

bool TaskOwner::isValid() const
{
    switch (kind) {
        case Kind::View:
            return !view.isNull();
        case Kind::Context:
            return !life.expired();
        default:
            return false;
    }
}

MDIView* TaskOwner::mdiView() const
{
    if (kind != Kind::View || view.isNull()) {
        return nullptr;
    }
    return static_cast<MDIView*>(view.data());
}

ViewerContext* TaskOwner::context() const
{
    if (kind == Kind::Context) {
        return life.expired() ? nullptr : ctx;
    }
    if (auto view3d = qobject_cast<View3DInventor*>(mdiView())) {
        return view3d->getViewer();
    }
    return nullptr;
}

Gui::Document* TaskOwner::document() const
{
    if (MDIView* mdi = mdiView()) {
        return mdi->getGuiDocument();
    }
    if (kind == Kind::Context && !life.expired()) {
        return ctx->getDocument();
    }
    return nullptr;
}

bool TaskOwner::isRemote() const
{
    return kind == Kind::Context && !life.expired() && ctx->cameraIsRemote();
}

bool TaskOwner::operator==(const TaskOwner& other) const
{
    if (kind != other.kind) {
        return false;
    }
    switch (kind) {
        case Kind::View:
            // The address alone would let a dead owner equal a new view
            // that came to stand where its own stood.
            return viewKey == other.viewKey && view.isNull() == other.view.isNull();
        case Kind::Context:
            // By the token, which a dead view's owners still share.
            return !life.owner_before(other.life) && !other.life.owner_before(life);
        default:
            return true;
    }
}
