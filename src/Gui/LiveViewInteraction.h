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

#ifndef GUI_LIVEVIEWINTERACTION_H
#define GUI_LIVEVIEWINTERACTION_H

#include <FCGlobal.h>

class QEvent;
class QObject;

namespace Gui
{

/**
 * Lets the camera stay in the user's hands while a long operation runs.
 *
 * A long operation normally freezes input twice over: WaitCursor's filter
 * eats mouse button events, and the progress bar's application-wide filter
 * eats moves and clicks as well (see WaitCursor.cpp and ProgressBar.cpp).
 * That is the right default for an operation that owns the GUI thread —
 * but not for one that deliberately keeps pumping events, like the
 * progressive STEP import, where the model grows on screen and the user
 * has every reason to orbit it while it does.
 *
 * While an instance of this class is alive, both filters let pointer input
 * through, so the window stays alive under the mouse:
 *
 * - Only pointer input (press, release, double click, move, enter/leave,
 *   wheel and native gestures) passes; navigation reads its modifiers off
 *   those events, so no keyboard event has to be let through. Keys stay
 *   blocked, which keeps Escape doing what it does for every other long
 *   operation: cancel it. QEvent::ContextMenu stays blocked too, so a
 *   right-drag orbits without opening a menu of commands, and the tree
 *   cannot offer the document-altering entries that bypass Command.
 * - It passes whatever the pointer is aimed at, not only a 3D view. This
 *   was learned the hard way: an application-level filter sees every mouse
 *   event TWICE, first addressed to the QWidgetWindow and only then to the
 *   widget Qt dispatches it to. Swallowing the window-level event means
 *   the widget-level one never happens, so a rule phrased in terms of the
 *   target widget approves an event that was already eaten -- which left
 *   the whole GUI deaf to the mouse, 3D view included, with only the wheel
 *   working because neither filter blocks it.
 * - Refusing the *edit* is therefore not this class's job, and cannot be:
 *   what protects the document is App::Document::LiveImport, which makes
 *   Command::invoke() refuse every AlterDoc command, and the same status
 *   consulted by the few document mutations that do not go through a
 *   Command (tree drop, tree double-click edit). Everything that only
 *   looks -- view commands, selection, panels -- keeps working, which is
 *   the point: a live view the user cannot click is not live.
 *
 * \code
 * Gui::LiveViewInteraction navigable;  // while this scope pumps events
 * \endcode
 */
class GuiExport LiveViewInteraction
{
public:
    LiveViewInteraction();
    ~LiveViewInteraction();

    LiveViewInteraction(const LiveViewInteraction&) = delete;
    LiveViewInteraction& operator=(const LiveViewInteraction&) = delete;
    LiveViewInteraction(LiveViewInteraction&&) = delete;
    LiveViewInteraction& operator=(LiveViewInteraction&&) = delete;

    /// Whether any instance is alive.
    static bool active();

    /// Whether an input filter should let \a event through to \a target
    /// instead of swallowing it. False whenever no instance is alive.
    static bool passes(QObject* target, QEvent* event);
};

}  // namespace Gui

#endif  // GUI_LIVEVIEWINTERACTION_H
