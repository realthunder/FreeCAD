/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#ifndef GUI_OMNI_CONTROL_H
#define GUI_OMNI_CONTROL_H

#include <cstdint>
#include <string>

#include <QJsonObject>
#include <QString>

#include <FCGlobal.h>

namespace Gui {

/** The omni search box over the scene stream's control channel.
 *
 * The browser viewer mirrors Std_OmniSearch (docs/OmniSearch.md sec 6)
 * with the ops handled here: the command and parameter lists shipped
 * once and kept current by version ("omni.catalog"), the volatile bits
 * of the visible rows ("omni.rows"), the document's objects and views
 * ("omni.objects"), the box's grammar for what was typed
 * ("omni.resolve"), and the actions -- "command.run",
 * "command.children", "param.get", "param.set", "param.reset". All of
 * it calls the same widget-free layer the desktop box does
 * (Gui::OmniSearch, App::ParamRegistry, the command manager).
 *
 * Every function runs on the GUI thread, from SceneControl.cpp's
 * handler.
 */
namespace OmniControl {

/// Whether \a op writes: the transport refuses it on a view-only connection
GuiExport bool isMutating(const QString &op);

/** Answer \a op when it is one of ours; false leaves \a reply untouched.
 * \a boundDoc names the served document the connection is joined to, as
 * handleSceneControlRequest() has it.
 */
GuiExport bool handle(const QString &op, const QJsonObject &req, const std::string &boundDoc,
                      QJsonObject &reply);

/** Connect the cues that a catalog may have changed -- a workbench
 * activated (commands registered, libraries with parameters loaded), a
 * shortcut reassigned -- so that every viewer is told "omni.changed" and
 * can fetch the delta. Idempotent; no-op without a Gui::Application.
 */
GuiExport void install();

/// The id every catalog version belongs to; a new one per process
GuiExport const std::string &sessionId();

/** The current version of a catalog ("commands" or "params"), after
 * bringing it up to date. Zero for an unknown list.
 */
GuiExport uint64_t catalogVersion(const QString &list);

} // namespace OmniControl
} // namespace Gui

#endif // GUI_OMNI_CONTROL_H
