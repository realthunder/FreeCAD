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

#ifndef GUI_SCENECONTROL_P_H
#define GUI_SCENECONTROL_P_H

// Shared between the translation units of the control channel
// (SceneControl.cpp, OmniControl.cpp): the property descriptor of
// docs/ThinClient.md sec 4.2 and the request plumbing around it. Not
// installed, not exported.

#include <string>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace App {
class Document;
class Property;
class PropertyContainer;
}

namespace Gui {
class Command;

namespace SceneControlDetail {

/// The 3D view whose properties a request bound to \a boundDoc edits:
/// the served document's container, else the active view, else the
/// first-served source's container (SceneControl.cpp).
App::PropertyContainer *sceneView(const std::string &boundDoc = {});

/// The document a connection is scoped to: the one its group serves
/// (\a boundDoc), else the active one. Null when there is none.
App::Document *homeDocument(const std::string &boundDoc);

/** Whether a connection bound to \a boundDoc may address \a doc at all.
 *
 * A control connection sees one document -- the one it is looking at --
 * and the documents that one links out to, which are the external
 * objects its own scene already shows. Anything else open in the same
 * process is another user's business and is not reachable from here
 * (docs/MultiDocServe.md sec 5.1, docs/OmniSearch.md sec 6.4).
 */
bool documentAllowed(App::Document *doc, const std::string &boundDoc);

/// The document a request names ("doc"), else the bound one, else the
/// active one; null when none of them exists -- or when the named one
/// is out of this connection's reach (documentAllowed()), which is
/// reported the same way, so the channel is no oracle for what else
/// the process has open.
App::Document *requestDocument(const QJsonObject &req, const std::string &boundDoc);

/// The browser allowlist (SceneControl.cpp): the commands a connection that
/// is not a host may run -- the tools that start in the client's view and
/// open nothing modal (docs/ThinClient.md sec 8.7). The web client keeps a
/// copy to draw a button disabled; this one decides.
bool isBrowserSafeCommand(const QString &name);

/// The command a group's member \a index (1-based) runs: the member action's
/// owning Action's. Empty when \a group is no group, or the member is a
/// separator or out of range.
QString groupMemberCommand(Command *group, int index);

QJsonObject errorReply(const QJsonValue &id, const char *code, const QString &message = QString());

/// One property serialized to the descriptor the DOM inspector renders
/// from: name, scope, group, doc, type, value, unit, enums,
/// constraints, readonly, hidden.
QJsonObject describeProperty(const App::PropertyContainer *container,
                             const std::string &name,
                             const App::Property *prop,
                             const char *scope);

/// Every property of \a container, in name order, appended to \a out.
void describeContainer(const App::PropertyContainer *container, const char *scope, QJsonArray &out);

/// Assign a descriptor-form value; an error code, or null on success.
const char *assignProperty(App::Property *prop, const QJsonValue &value);

} // namespace SceneControlDetail
} // namespace Gui

#endif // GUI_SCENECONTROL_P_H
