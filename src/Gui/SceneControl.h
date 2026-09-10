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

#ifndef GUI_SCENECONTROL_H
#define GUI_SCENECONTROL_H

#include <functional>
#include <cstdint>
#include <string>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <FCGlobal.h>

namespace Gui {

/// The semantic control channel of the scene stream (docs/ThinClient.md
/// §4.2): id-correlated JSON operations from a remote viewer, answered
/// against the live document. getProperties today; setProperty and
/// modeling operations grow here.

/// Answer one control request. GUI thread only — callers on any other
/// thread marshal first (installSceneControlHandler does). \a boundDoc
/// names the served document the request's connection is joined to
/// (empty = unbound, windowed behavior): it resolves the "view3d"
/// subject to that document's serving container and stands in for an
/// unnamed document (docs/MultiDocServe.md §5). \a viewOnly refuses
/// every mutating op with a ViewOnly error — the per-client mode the
/// sharing host sets (docs/MultiDocServe.md §8); reads stay answered.
/// \a client is the connection the request arrived on, which the ops
/// that need a view -- entering an edit mode -- resolve to that
/// client's mirror viewer (docs/ThinClient.md sec 8.9 step 4).
std::string handleSceneControlRequest(const std::string &json,
                                      const std::string &boundDoc = {},
                                      bool viewOnly = false,
                                      uint64_t client = 0);

/// One control op implemented outside core Gui. \a req is the parsed
/// request (its "id" must be echoed in the reply, which
/// sceneControlError does for a refusal); \a boundDoc is the served
/// document the connection is joined to, empty when unbound.
using SceneControlOpHandler =
    std::function<QJsonObject(const QJsonObject &req,
                              const std::string &boundDoc)>;

/// Register a handler for one op. This is how a workbench adds control
/// ops that core Gui must not link against to implement -- Spreadsheet's
/// sheet.get/sheet.set are the first (docs/SpreadsheetRemote.md sec 3).
/// Call it from the module's Gui init; a second registration of the
/// same name replaces the first. \a mutating ops are refused on a
/// view-only connection before the handler runs, so a handler never
/// has to check (docs/MultiDocServe.md sec 8).
GuiExport void registerSceneControlOp(const QString &op, bool mutating,
                                      SceneControlOpHandler handler);

/// The error reply shape the built-in ops use, for handlers to match:
/// {"id": <echoed>, "ok": false, "code": ..., "message": ...}.
GuiExport QJsonObject sceneControlError(const QJsonValue &id,
                                        const char *code,
                                        const QString &message);

/// Route the scene stream server's control requests ("op" JSON text
/// frames) through handleSceneControlRequest on the GUI thread.
/// Idempotent; safe to call whenever a serving renderer comes up.
/// \a docName installs on that document's server group and binds the
/// handler to it; empty installs on the default group, unbound.
void installSceneControlHandler(const std::string &docName = {});

} // namespace Gui

#endif // GUI_SCENECONTROL_H
