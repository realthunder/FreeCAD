/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef GUI_SCENEWIDGETS_H
#define GUI_SCENEWIDGETS_H

/* The widget stream (docs/Sandbox.md 7.18): the host widget layer's
 * store fanned out to streamed clients over the scene control channel
 * (docs/ThinClient.md 4.2), one op per store message.
 *
 * Ops (all `mutating`, refused on a view-only connection):
 *   widgets.subscribe   {"toolbars": bool, "panels": bool, "all": bool}
 *                       -> ok, "theme", "locale", "panel" (the id of the
 *                       task dialog up, or null); then one "open" push
 *                       per object in reference order, and every later
 *                       message
 *   widgets.unsubscribe                                 -> ok
 *   widgets.icon        {"name", "size"} -> "format" ("svg" | "png"),
 *                       "data" (SVG text, or base64 PNG), "size"
 *   widgets.update      {"id", "state": {"q_...": v}}   -> ok
 *   widgets.custom      {"id", "content": {"event", "args"}} -> ok
 * Pushes: {"op": "widgets", "method": "open" | "update" | "custom" |
 * "close", "id", ... the snapshot's keys for open, "content" otherwise}.
 *
 * A "toolbars" subscription starts the tool bar mirror, a "panels" one
 * the panel mirror (docs/Sandbox.md 7.19); the last one out stops it.  A connection is dropped from the sets when a push to
 * it fails (the server says it is gone).
 */

#include <cstdint>
#include <functional>
#include <string>

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <FCGlobal.h>

namespace Gui
{

class GuiExport SceneWidgetStream : public QObject
{
    Q_OBJECT
public:
    static SceneWidgetStream& instance();

    /// Where a push goes: the scene stream server's sendControl by
    /// default; a test injects its own.
    using Sender = std::function<bool(uint64_t client, const std::string& json)>;
    void setSender(Sender sender);

    /// `toolbars` joins the tool bar mirror's objects, `panels` the
    /// panel mirror's, `all` every store object; all false leaves.  The
    /// snapshot is pushed from the event loop (after the reply).
    void subscribe(uint64_t client, bool toolbars, bool all, bool panels = false);
    void unsubscribe(uint64_t client);
    bool isSubscribed(uint64_t client) const
    {
        return _toolbars.contains(client) || _all.contains(client) || _panels.contains(client);
    }
    int subscriberCount() const
    {
        return (_toolbars | _all | _panels).size();
    }
    /// Push the snapshot to one client now (what the deferred push does).
    void pushSnapshot(uint64_t client);

private:
    SceneWidgetStream();
    void onMessage(const QString& id, const QString& method, const QVariantMap& content,
                   quint64 origin);
    bool wants(uint64_t client, const QString& id) const;
    void send(uint64_t client, const std::string& json);
    void checkMirror();

    Sender _sender;
    QSet<uint64_t> _toolbars;
    QSet<uint64_t> _panels;
    QSet<uint64_t> _all;
    bool _connected = false;
};

/// Register the widgets.* control ops (idempotent).
GuiExport void installSceneWidgetOps();

}  // namespace Gui

#endif  // GUI_SCENEWIDGETS_H
