/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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
#include <QEvent>
#include <QWidget>
#include <QWindow>
#endif

#include "LiveViewInteraction.h"
#include "View3DInventor.h"

using namespace Gui;

namespace
{
// Nesting is allowed (an import inside an import inside...), so count
// rather than flag. Only the GUI thread ever touches this: the filters
// that read it are the ones Qt runs while dispatching, and the guard is
// scoped around GUI-thread work.
int liveInteractionDepth {0};

// Walk the widget chain the way the filters' own isModalDialog() helpers
// do, since an event can arrive addressed to a QWindow rather than to the
// QWidget that owns it.
bool targetsView3D(QObject* target)
{
    auto* widget = qobject_cast<QWidget*>(target);
    if (!widget) {
        if (auto* window = qobject_cast<QWindow*>(target)) {
            widget = QWidget::find(window->winId());
        }
    }
    while (widget) {
        if (qobject_cast<View3DInventor*>(widget)) {
            return true;
        }
        widget = widget->parentWidget();
    }
    return false;
}
}  // namespace

LiveViewInteraction::LiveViewInteraction()
{
    ++liveInteractionDepth;
}

LiveViewInteraction::~LiveViewInteraction()
{
    --liveInteractionDepth;
}

bool LiveViewInteraction::active()
{
    return liveInteractionDepth > 0;
}

bool LiveViewInteraction::passes(QObject* target, QEvent* event)
{
    if (!active() || !event) {
        return false;
    }

    switch (event->type()) {
        // navigation input: everything the two filters would otherwise
        // swallow on the way to a 3D view. QEvent::Wheel is absent because
        // neither filter blocks it (zoom has always worked mid-operation),
        // and ContextMenu is absent on purpose — see the header.
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::NativeGesture:
        case QEvent::Enter:
        case QEvent::Leave:
            break;
        default:
            return false;
    }

    return targetsView3D(target);
}
