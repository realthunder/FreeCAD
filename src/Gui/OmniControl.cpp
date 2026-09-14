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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cstring>
# include <deque>
# include <map>
# include <set>
# include <QAction>
# include <QCoreApplication>
# include <QJsonArray>
# include <QJsonDocument>
# include <QJsonObject>
# include <QJsonValue>
# include <QRandomGenerator>
# include <QTimer>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/ParamRegistry.h>
#include <App/PropertyContainer.h>
#include <Base/Exception.h>

#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "OmniControl.h"
#include "OmniSearch.h"
#include "SceneControl.h"
#include "SceneControlP.h"
#include "ShortcutManager.h"
#include "ViewProviderDocumentObject.h"
#include "Renderer/SceneServer.h"

using namespace Gui;
using namespace Gui::SceneControlDetail;
using App::ParamInfo;
using App::ParamRegistry;

namespace {

// ---------------------------------------------------------------------------
// The catalogs: a list shipped whole once and then by difference.
//
// The model is the scene stream's (docs/SceneStreaming.md sec 5): the
// viewer states the version it holds, the server answers with what
// changed since -- merged last-wins on the row's key from a bounded
// history -- or with the whole list when the version is out of the
// history or from another run. A version means nothing without the
// session that issued it, so every reply names the session and a
// request from another session is answered whole.

struct Catalog {
    struct Change {
        uint64_t version = 0;
        std::set<std::string> changed;  // added or edited
        std::set<std::string> removed;
    };
    static const size_t kHistory = 64;

    uint64_t version = 0;
    std::map<std::string, QJsonObject> rows;
    std::deque<Change> history;

    /// Install \a fresh as the current rows; a difference is a new version.
    bool update(std::map<std::string, QJsonObject> &&fresh)
    {
        Change change;
        for (auto &v : fresh) {
            auto it = rows.find(v.first);
            if (it == rows.end() || it->second != v.second)
                change.changed.insert(v.first);
        }
        for (auto &v : rows) {
            if (!fresh.count(v.first))
                change.removed.insert(v.first);
        }
        if (change.changed.empty() && change.removed.empty())
            return false;
        rows = std::move(fresh);
        change.version = ++version;
        history.push_back(std::move(change));
        while (history.size() > kHistory)
            history.pop_front();
        return true;
    }

    /// The reply for a viewer holding \a held (~0: nothing) of this session
    void reply(uint64_t held, QJsonObject &out) const
    {
        out[QLatin1String("version")] = double(version);
        QJsonArray add, remove;
        bool full = true;
        if (held == version) {
            full = false;
        }
        else if (held < version && !history.empty() && held + 1 >= history.front().version) {
            full = false;
            std::set<std::string> changed, removed;
            for (const auto &h : history) {
                if (h.version <= held)
                    continue;
                for (const auto &k : h.changed) {
                    changed.insert(k);
                    removed.erase(k);
                }
                for (const auto &k : h.removed) {
                    removed.insert(k);
                    changed.erase(k);
                }
            }
            for (const auto &k : changed) {
                auto it = rows.find(k);
                if (it != rows.end())
                    add.push_back(it->second);
            }
            for (const auto &k : removed)
                remove.push_back(QString::fromStdString(k));
        }
        if (full) {
            for (const auto &v : rows)
                add.push_back(v.second);
        }
        out[QLatin1String("full")] = full;
        out[QLatin1String("add")] = add;
        out[QLatin1String("remove")] = remove;
    }
};

const char *paramTypeName(ParamInfo::Type type)
{
    switch (type) {
    case ParamInfo::Bool: return "Bool";
    case ParamInfo::Int: return "Int";
    case ParamInfo::UInt: return "UInt";
    case ParamInfo::Hex: return "Hex";
    case ParamInfo::Float: return "Float";
    case ParamInfo::String: return "String";
    }
    return "String";
}

QString translated(const ParamInfo &info, const char *text)
{
    if (!text || !text[0])
        return {};
    return QCoreApplication::translate(info.className, text);
}

/// One parameter as the browser's row: what the desktop row shows plus
/// what its editor is built from (ParamInfo's proxy fields). The value
/// is not here -- it changes without the catalog changing -- see
/// "omni.rows".
QJsonObject paramRow(const ParamInfo &info)
{
    QJsonObject row;
    row[QLatin1String("key")] = QString::fromStdString(info.fullPath());
    row[QLatin1String("path")] = QString::fromStdString(info.displayPath());
    row[QLatin1String("group")] = QString::fromUtf8(info.path);
    row[QLatin1String("entry")] = QString::fromUtf8(info.entry);
    row[QLatin1String("name")] = QString::fromStdString(info.fullName());
    row[QLatin1String("title")] = translated(info, info.title);
    row[QLatin1String("doc")] = translated(info, info.doc);
    row[QLatin1String("type")] = QString::fromUtf8(paramTypeName(info.type));
    row[QLatin1String("default")] = QString::fromStdString(info.defaultValue);
    if (info.proxy && info.proxy[0])
        row[QLatin1String("proxy")] = QString::fromUtf8(info.proxy);
    if (info.step != 0.0 || info.minimum != info.maximum) {
        row[QLatin1String("min")] = info.minimum;
        row[QLatin1String("max")] = info.maximum;
        row[QLatin1String("step")] = info.step;
        row[QLatin1String("decimals")] = info.decimals;
    }
    if (info.transparency)
        row[QLatin1String("transparency")] = true;
    if (!info.items.empty()) {
        QJsonArray items;
        for (const auto &item : info.items) {
            QJsonObject o;
            o[QLatin1String("text")] = info.translateItems
                ? translated(info, item.text) : QString::fromUtf8(item.text ? item.text : "");
            if (item.tooltip && item.tooltip[0])
                o[QLatin1String("tooltip")] = translated(info, item.tooltip);
            if (info.comboDataIsString && item.data)
                o[QLatin1String("data")] = QString::fromUtf8(item.data);
            items.push_back(o);
        }
        row[QLatin1String("items")] = items;
        if (info.comboDataIsString)
            row[QLatin1String("dataIsString")] = true;
    }
    return row;
}

/// One command as the browser's row: the desktop row's title, name,
/// shortcut and tooltip, and whether it owns a group. Active or not is
/// volatile and rides "omni.rows".
QJsonObject commandRow(Command *cmd)
{
    QJsonObject row;
    row[QLatin1String("name")] = QString::fromUtf8(cmd->getName());
    row[QLatin1String("title")] = Action::commandMenuText(cmd);
    row[QLatin1String("desc")] = Action::commandToolTip(cmd, false);
    QString shortcut = cmd->getShortcut();
    if (!shortcut.isEmpty())
        row[QLatin1String("shortcut")] = shortcut;
    bool group = false;
    if (auto action = cmd->getAction())
        group = qobject_cast<ActionGroup*>(action) != nullptr;
    else
        group = dynamic_cast<GroupCommand*>(cmd) != nullptr;
    if (group)
        row[QLatin1String("group")] = true;
    return row;
}

struct Catalogs {
    Catalog commands;
    Catalog params;
    // What the rows were last built from; a change rebuilds them
    int commandRevision = -1;
    unsigned shortcutGen = 0;      // bumped by ShortcutManager::shortcutChanged
    unsigned builtShortcutGen = ~0u;
    size_t paramCount = ~size_t(0);

    /// Bring \a list up to date; true when its version advanced
    bool refresh(const QString &list)
    {
        if (list == QLatin1String("commands")) {
            if (!Application::Instance)
                return false;
            auto &manager = Application::Instance->commandManager();
            if (manager.getRevision() == commandRevision && shortcutGen == builtShortcutGen)
                return false;
            commandRevision = manager.getRevision();
            builtShortcutGen = shortcutGen;
            std::map<std::string, QJsonObject> rows;
            for (auto &v : manager.getCommands())
                rows[v.first] = commandRow(v.second);
            return commands.update(std::move(rows));
        }
        if (list == QLatin1String("params")) {
            auto &reg = ParamRegistry::instance();
            if (reg.entries().size() == paramCount)
                return false;
            paramCount = reg.entries().size();
            std::map<std::string, QJsonObject> rows;
            for (auto info : reg.entries())
                rows[info->fullPath()] = paramRow(*info);
            return params.update(std::move(rows));
        }
        return false;
    }

    Catalog *get(const QString &list)
    {
        if (list == QLatin1String("commands"))
            return &commands;
        if (list == QLatin1String("params"))
            return &params;
        return nullptr;
    }
};

Catalogs &catalogs()
{
    static Catalogs instance;
    return instance;
}

QJsonValue idOf(const QJsonObject &req)
{
    return req.value(QLatin1String("id"));
}

QJsonObject okReply(const QJsonObject &req)
{
    QJsonObject reply;
    reply[QLatin1String("id")] = idOf(req);
    reply[QLatin1String("ok")] = true;
    return reply;
}

void broadcastChanged(const QString &list, uint64_t version)
{
    QJsonObject msg;
    msg[QLatin1String("op")] = QStringLiteral("omni.changed");
    msg[QLatin1String("list")] = list;
    msg[QLatin1String("session")] = QString::fromStdString(OmniControl::sessionId());
    msg[QLatin1String("version")] = double(version);
    Render::SceneStreamServer::instance().broadcastControl(
            QJsonDocument(msg).toJson(QJsonDocument::Compact).toStdString());
}

/// Refresh both catalogs and tell every viewer about the one that moved
void checkCatalogs()
{
    for (const char *name : {"commands", "params"}) {
        QString list = QString::fromLatin1(name);
        if (catalogs().refresh(list))
            broadcastChanged(list, catalogs().get(list)->version);
    }
}

// ---------------------------------------------------------------------------
// the ops

QJsonObject opCatalog(const QJsonObject &req)
{
    const QString list = req.value(QLatin1String("list")).toString();
    auto catalog = catalogs().get(list);
    if (!catalog)
        return errorReply(idOf(req), "UnknownList", list);
    // Another viewer may be told first; the asker learns from the reply
    if (catalogs().refresh(list))
        broadcastChanged(list, catalog->version);
    uint64_t held = ~uint64_t(0);
    if (req.value(QLatin1String("session")).toString().toStdString() == OmniControl::sessionId()
            && req.value(QLatin1String("version")).isDouble())
        held = uint64_t(req.value(QLatin1String("version")).toDouble());
    QJsonObject reply = okReply(req);
    reply[QLatin1String("list")] = list;
    reply[QLatin1String("session")] = QString::fromStdString(OmniControl::sessionId());
    catalog->reply(held, reply);
    return reply;
}

const ParamInfo *findParam(const QString &key)
{
    // key is ParamInfo::fullPath(): group path, '/', entry
    int slash = key.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return nullptr;
    return ParamRegistry::instance().find(key.left(slash).toUtf8().constData(),
                                          key.mid(slash + 1).toUtf8().constData());
}

/// The volatile bits of the rows a viewer shows: a parameter's value and
/// whether it is stored, a command's active state.
QJsonObject opRows(const QJsonObject &req)
{
    const QString list = req.value(QLatin1String("list")).toString();
    if (!catalogs().get(list))
        return errorReply(idOf(req), "UnknownList", list);
    QJsonObject rows;
    auto &reg = ParamRegistry::instance();
    CommandManager *manager = Application::Instance
        ? &Application::Instance->commandManager() : nullptr;
    for (const auto &k : req.value(QLatin1String("keys")).toArray()) {
        const QString key = k.toString();
        QJsonObject row;
        if (list == QLatin1String("params")) {
            auto info = findParam(key);
            if (!info)
                continue;
            row[QLatin1String("value")] = QString::fromStdString(reg.getValue(*info));
            row[QLatin1String("set")] = reg.isSet(*info);
        }
        else {
            auto cmd = manager ? manager->getCommandByName(key.toUtf8().constData()) : nullptr;
            if (!cmd)
                continue;
            row[QLatin1String("active")] = cmd->isActive();
        }
        rows[key] = row;
    }
    QJsonObject reply = okReply(req);
    reply[QLatin1String("list")] = list;
    reply[QLatin1String("rows")] = rows;
    return reply;
}

/// The document's objects and views, for the object mode's completion
QJsonObject opObjects(const QJsonObject &req, const std::string &boundDoc)
{
    auto doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(idOf(req), "UnknownDocument",
                          req.value(QLatin1String("doc")).toString());
    QJsonObject reply = okReply(req);
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    reply[QLatin1String("label")] = QString::fromUtf8(doc->Label.getValue());
    QJsonArray objects;
    for (auto obj : doc->getObjects()) {
        if (!obj || !obj->isAttachedToDocument())
            continue;
        QJsonObject o;
        o[QLatin1String("name")] = QString::fromUtf8(obj->getNameInDocument());
        const char *label = obj->Label.getValue();
        if (label && std::strcmp(label, obj->getNameInDocument()) != 0)
            o[QLatin1String("label")] = QString::fromUtf8(label);
        o[QLatin1String("type")] = QString::fromUtf8(obj->getTypeId().getName());
        QJsonArray children;
        for (const auto &sub : obj->getSubObjects()) {
            QString name = QString::fromUtf8(sub.c_str());
            if (name.endsWith(QLatin1Char('.')))
                name.chop(1);
            if (!name.isEmpty())
                children.push_back(name);
        }
        if (!children.isEmpty())
            o[QLatin1String("children")] = children;
        objects.push_back(o);
    }
    reply[QLatin1String("objects")] = objects;
    // The views a "#." can name. The served view is what "ActiveView"
    // means to a viewer, whichever MDI view the desktop has active.
    auto served = sceneView(boundDoc);
    QJsonArray views;
    for (const auto &v : OmniSearch::documentViews(doc)) {
        QJsonObject o;
        o[QLatin1String("name")] = QString::fromStdString(v.name);
        o[QLatin1String("title")] = v.title;
        if (served && OmniSearch::documentView(doc, v.name) == served)
            o[QLatin1String("served")] = true;
        views.push_back(o);
    }
    reply[QLatin1String("views")] = views;
    return reply;
}

/// What the typed text names, in the box's grammar (OmniSearch::resolveObject)
QJsonObject opResolve(const QJsonObject &req, const std::string &boundDoc)
{
    auto doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(idOf(req), "UnknownDocument",
                          req.value(QLatin1String("doc")).toString());
    const QString query = req.value(QLatin1String("query")).toString().trimmed();
    QJsonObject reply = okReply(req);
    reply[QLatin1String("query")] = query;

    // "#.ActiveView.X" is the served view's property: the view this
    // connection looks at, not the desktop's active MDI view
    static const QString activePrefix = QStringLiteral("#.ActiveView.");
    if (query.startsWith(activePrefix)) {
        const QString name = query.mid(activePrefix.size());
        auto view = sceneView(boundDoc);
        auto prop = view ? view->getPropertyByName(name.toUtf8().constData()) : nullptr;
        if (!prop || name.contains(QLatin1Char('.')))
            return errorReply(idOf(req), "NoMatch", query);
        reply[QLatin1String("kind")] = QStringLiteral("property");
        reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
        reply[QLatin1String("obj")] = QString();
        reply[QLatin1String("scope")] = QStringLiteral("view3d");
        reply[QLatin1String("prop")] = describeProperty(view, name.toUtf8().constData(), prop, "view3d");
        return reply;
    }

    OmniSearch::ObjectMatch match;
    if (!OmniSearch::resolveInDocument(query, doc, match))
        return errorReply(idOf(req), "NoMatch", query);

    if (match.doc) {
        // A member of the document itself or of one of its views. The
        // grammar accepts "Other#.Comment", so what it landed on is
        // checked against this connection's reach, not just what the
        // request named.
        if (!documentAllowed(match.doc, boundDoc))
            return errorReply(idOf(req), "NoMatch", query);
        App::PropertyContainer *container = match.doc;
        const char *scope = "document";
        if (!match.view.empty()) {
            container = OmniSearch::documentView(match.doc, match.view);
            scope = "view3d";
            if (container && container == sceneView(boundDoc))
                match.view.clear();   // the served view: no name needed
            else
                reply[QLatin1String("view")] = QString::fromStdString(match.view);
        }
        if (!container || !match.prop)
            return errorReply(idOf(req), "NoMatch", query);
        const char *name = container->getPropertyName(match.prop);
        if (!name)
            return errorReply(idOf(req), "NoMatch", query);
        reply[QLatin1String("kind")] = QStringLiteral("property");
        reply[QLatin1String("doc")] = QString::fromUtf8(match.doc->getName());
        reply[QLatin1String("obj")] = QString();
        reply[QLatin1String("scope")] = QString::fromUtf8(scope);
        reply[QLatin1String("prop")] = describeProperty(container, name, match.prop, scope);
        return reply;
    }

    auto obj = match.obj.getObject();
    auto target = match.obj.getSubObject();
    if (!obj || !target)
        return errorReply(idOf(req), "NoMatch", query);
    // "Other#Box.Length" parses the same way an in-document path does,
    // so both ends of the resolved path are checked. A sub-object in
    // another document is in reach when the served one links out to it,
    // which is the case that has to keep working.
    if (!documentAllowed(obj->getDocument(), boundDoc)
            || !documentAllowed(target->getDocument(), boundDoc))
        return errorReply(idOf(req), "NoMatch", query);
    reply[QLatin1String("doc")] = QString::fromUtf8(target->getDocument()->getName());
    reply[QLatin1String("obj")] = QString::fromUtf8(target->getNameInDocument());
    reply[QLatin1String("label")] = QString::fromUtf8(target->Label.getValue());
    reply[QLatin1String("type")] = QString::fromUtf8(target->getTypeId().getName());
    if (!match.prop) {
        reply[QLatin1String("kind")] = QStringLiteral("object");
        reply[QLatin1String("top")] = QString::fromUtf8(obj->getNameInDocument());
        reply[QLatin1String("sub")] = QString::fromStdString(match.obj.getSubName());
        return reply;
    }
    // The property is the target's own, or its view provider's
    App::PropertyContainer *container = target;
    const char *scope = "object";
    const char *name = target->getPropertyName(match.prop);
    if (!name && Application::Instance) {
        if (auto vp = Application::Instance->getViewProvider(target)) {
            name = vp->getPropertyName(match.prop);
            container = vp;
            scope = "view";
        }
    }
    if (!name)
        return errorReply(idOf(req), "NoMatch", query);
    reply[QLatin1String("kind")] = QStringLiteral("property");
    reply[QLatin1String("scope")] = QString::fromUtf8(scope);
    reply[QLatin1String("prop")] = describeProperty(container, name, match.prop, scope);
    return reply;
}

QJsonObject opCommandRun(const QJsonObject &req)
{
    if (!Application::Instance)
        return errorReply(idOf(req), "NoGui");
    const QByteArray name = req.value(QLatin1String("name")).toString().toUtf8();
    auto &manager = Application::Instance->commandManager();
    auto cmd = manager.getCommandByName(name.constData());
    if (!cmd)
        return errorReply(idOf(req), "UnknownCommand", QString::fromUtf8(name));
    const QJsonValue child = req.value(QLatin1String("child"));
    // What will RUN is judged, not the name asked for: a row of a group
    // runs its member's command. An editing connection is held to the
    // browser allowlist and a host is not (docs/ShareAccess.md sec 2.2) --
    // and the refusal comes before whether the command is active, which is
    // the desktop's state, not the catalog's.
    if (sceneControlAccess() < Render::ClientAccess::Host) {
        const QString runs = child.isDouble()
            ? groupMemberCommand(cmd, int(child.toDouble()) + 1)
            : QString::fromUtf8(name);
        if (!isBrowserSafeCommand(runs))
            return errorReply(idOf(req), "Forbidden",
                              runs.isEmpty() ? QString::fromUtf8(name) : runs);
    }
    if (!cmd->isActive())
        return errorReply(idOf(req), "Inactive", QString::fromUtf8(name));
    try {
        if (child.isDouble()) {
            // One row of a group command's menu, as a click on it
            cmd->initAction();
            auto group = qobject_cast<ActionGroup*>(cmd->getAction());
            if (!group)
                return errorReply(idOf(req), "NotGroup", QString::fromUtf8(name));
            auto actions = group->actions();
            int index = int(child.toDouble());
            if (index < 0 || index >= actions.size())
                return errorReply(idOf(req), "BadValue", QStringLiteral("child"));
            if (!actions[index]->isEnabled())
                return errorReply(idOf(req), "Inactive", actions[index]->text());
            actions[index]->trigger();
        }
        else {
            manager.runCommandByName(name.constData());
            CmdHistoryAction::onInvokeCommand(name.constData(), true);
        }
    }
    catch (Base::Exception &e) {
        return errorReply(idOf(req), "CommandFailed", QString::fromUtf8(e.what()));
    }
    catch (std::exception &e) {
        return errorReply(idOf(req), "CommandFailed", QString::fromUtf8(e.what()));
    }
    return okReply(req);
}

/// The rows of a group command's menu: what ActionGroup::populateMenu()
/// would show, as data
QJsonObject opCommandChildren(const QJsonObject &req)
{
    if (!Application::Instance)
        return errorReply(idOf(req), "NoGui");
    const QByteArray name = req.value(QLatin1String("name")).toString().toUtf8();
    auto cmd = Application::Instance->commandManager().getCommandByName(name.constData());
    if (!cmd)
        return errorReply(idOf(req), "UnknownCommand", QString::fromUtf8(name));
    cmd->initAction();
    auto group = qobject_cast<ActionGroup*>(cmd->getAction());
    if (!group)
        return errorReply(idOf(req), "NotGroup", QString::fromUtf8(name));
    QJsonObject reply = okReply(req);
    reply[QLatin1String("name")] = QString::fromUtf8(name);
    reply[QLatin1String("exclusive")] = group->isExclusive();
    QJsonArray items;
    int index = 0;
    for (auto action : group->actions()) {
        QJsonObject o;
        o[QLatin1String("index")] = index++;
        if (action->isSeparator()) {
            o[QLatin1String("separator")] = true;
            items.push_back(o);
            continue;
        }
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        o[QLatin1String("text")] = text;
        const QString tip = action->toolTip();
        if (!tip.isEmpty() && tip != text)
            o[QLatin1String("tooltip")] = tip;
        if (action->isCheckable()) {
            o[QLatin1String("checkable")] = true;
            o[QLatin1String("checked")] = action->isChecked();
        }
        if (!action->isEnabled())
            o[QLatin1String("enabled")] = false;
        if (!action->isVisible())
            o[QLatin1String("visible")] = false;
        items.push_back(o);
    }
    reply[QLatin1String("items")] = items;
    return reply;
}

QJsonObject paramState(const QJsonObject &req, const ParamInfo &info)
{
    auto &reg = ParamRegistry::instance();
    QJsonObject reply = okReply(req);
    reply[QLatin1String("key")] = QString::fromStdString(info.fullPath());
    reply[QLatin1String("value")] = QString::fromStdString(reg.getValue(info));
    reply[QLatin1String("set")] = reg.isSet(info);
    return reply;
}

QJsonObject opParam(const QString &op, const QJsonObject &req)
{
    const QString key = req.value(QLatin1String("key")).toString();
    auto info = findParam(key);
    if (!info)
        return errorReply(idOf(req), "UnknownParam", key);
    auto &reg = ParamRegistry::instance();
    if (op == QLatin1String("param.set")) {
        // The value in ParamRegistry::getValue() form; a JSON bool or
        // number is accepted and spelled out
        const QJsonValue v = req.value(QLatin1String("value"));
        std::string text;
        if (v.isString())
            text = v.toString().toStdString();
        else if (v.isBool())
            text = v.toBool() ? "true" : "false";
        else if (v.isDouble())
            text = QString::number(v.toDouble(), 'g', 15).toStdString();
        else
            return errorReply(idOf(req), "BadValue", key);
        if (!reg.setValue(*info, text))
            return errorReply(idOf(req), "BadValue", QString::fromStdString(text));
    }
    else if (op == QLatin1String("param.reset")) {
        reg.reset(*info);
    }
    return paramState(req, *info);
}

} // namespace

// ---------------------------------------------------------------------------

const std::string &OmniControl::sessionId()
{
    static const std::string id = [] {
        return QString::number(QRandomGenerator::global()->generate64(), 36).toStdString();
    }();
    return id;
}

uint64_t OmniControl::catalogVersion(const QString &list)
{
    auto catalog = catalogs().get(list);
    if (!catalog)
        return 0;
    catalogs().refresh(list);
    return catalog->version;
}

Render::ClientAccess OmniControl::requiredAccess(const QString &op)
{
    if (op == QLatin1String("param.set") || op == QLatin1String("param.reset"))
        return Render::ClientAccess::Host;
    if (op == QLatin1String("command.run"))
        return Render::ClientAccess::Edit;
    return Render::ClientAccess::View;
}

bool OmniControl::handle(const QString &op, const QJsonObject &req, const std::string &boundDoc,
                         QJsonObject &reply)
{
    if (op == QLatin1String("omni.catalog"))
        reply = opCatalog(req);
    else if (op == QLatin1String("omni.rows"))
        reply = opRows(req);
    else if (op == QLatin1String("omni.objects"))
        reply = opObjects(req, boundDoc);
    else if (op == QLatin1String("omni.resolve"))
        reply = opResolve(req, boundDoc);
    else if (op == QLatin1String("command.run"))
        reply = opCommandRun(req);
    else if (op == QLatin1String("command.children"))
        reply = opCommandChildren(req);
    else if (op == QLatin1String("param.get") || op == QLatin1String("param.set")
             || op == QLatin1String("param.reset"))
        reply = opParam(op, req);
    else
        return false;
    return true;
}

void OmniControl::install()
{
    static bool installed = false;
    if (installed || !Application::Instance)
        return;
    installed = true;
    // Deferred: the workbench signal fires while commands are still
    // being registered, and several cues can land in one event loop
    // turn
    static bool pending = false;
    auto schedule = [] {
        if (pending)
            return;
        pending = true;
        QTimer::singleShot(0, [] {
            pending = false;
            checkCatalogs();
        });
    };
    Application::Instance->signalActivateWorkbench.connect([schedule](const char *) {
        schedule();
    });
    QObject::connect(ShortcutManager::instance(), &ShortcutManager::shortcutChanged,
                     [schedule](const char *, const QKeySequence &) {
        ++catalogs().shortcutGen;
        schedule();
    });
}
