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

#include "PreCompiled.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTimer>

#include "BitmapFactory.h"
#include "Fw/FwImage.h"
#include "Fw/FwPanelMirror.h"
#include "Fw/FwStore.h"
#include "Fw/FwToolBarMirror.h"
#include "MainWindow.h"
#include "Renderer/SceneServer.h"
#include "SceneControl.h"
#include "SceneWidgets.h"

using namespace Gui;

namespace
{
std::string compact(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

QJsonObject okReply(const QJsonValue& id)
{
    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    return reply;
}
}  // namespace

SceneWidgetStream& SceneWidgetStream::instance()
{
    static SceneWidgetStream stream;
    return stream;
}

SceneWidgetStream::SceneWidgetStream()
{
    _sender = [](uint64_t client, const std::string& json) {
        return Render::SceneStreamServer::instance().sendControl(client, json);
    };
}

void SceneWidgetStream::setSender(Sender sender)
{
    _sender = std::move(sender);
}

void SceneWidgetStream::subscribe(uint64_t client, bool toolbars, bool all, bool panels)
{
    if (!toolbars && !all && !panels) {
        unsubscribe(client);
        return;
    }
    if (!_connected) {
        _connected = true;
        connect(&Fw::Store::instance(), &Fw::Store::message, this,
                &SceneWidgetStream::onMessage);
    }
    const bool fresh = !isSubscribed(client);
    if (toolbars)
        _toolbars.insert(client);
    else
        _toolbars.remove(client);
    if (all)
        _all.insert(client);
    else
        _all.remove(client);
    if (panels)
        _panels.insert(client);
    else
        _panels.remove(client);
    checkMirror();
    if (fresh) {
        // after the reply has gone out: the snapshot follows it
        QTimer::singleShot(0, this, [this, client]() {
            if (isSubscribed(client))
                pushSnapshot(client);
        });
    }
}

void SceneWidgetStream::unsubscribe(uint64_t client)
{
    _toolbars.remove(client);
    _all.remove(client);
    _panels.remove(client);
    checkMirror();
}

void SceneWidgetStream::checkMirror()
{
    Fw::ToolBarMirror& mirror = Fw::ToolBarMirror::instance();
    if (_toolbars.isEmpty()) {
        if (mirror.isRunning())
            mirror.stop();
    }
    else if (!mirror.isRunning()) {
        mirror.start();
    }
    Fw::PanelMirror& panels = Fw::PanelMirror::instance();
    if (_panels.isEmpty()) {
        if (panels.isRunning())
            panels.stop();
    }
    else if (!panels.isRunning()) {
        panels.start();
    }
}

bool SceneWidgetStream::wants(uint64_t client, const QString& id) const
{
    if (_all.contains(client))
        return true;
    if (_toolbars.contains(client) && Fw::ToolBarMirror::owns(id))
        return true;
    return _panels.contains(client) && Fw::PanelMirror::owns(id);
}

void SceneWidgetStream::send(uint64_t client, const std::string& json)
{
    if (_sender && _sender(client, json))
        return;
    // gone: out of the sets, and the mirror stops with the last one
    _toolbars.remove(client);
    _all.remove(client);
    _panels.remove(client);
    checkMirror();
}

void SceneWidgetStream::pushSnapshot(uint64_t client)
{
    Fw::Store& store = Fw::Store::instance();
    for (const QString& id : store.snapshotOrder()) {
        if (!wants(client, id))
            continue;
        QJsonObject msg = QJsonObject::fromVariantMap(store.snapshot(id));
        msg[QLatin1String("op")] = QStringLiteral("widgets");
        msg[QLatin1String("method")] = QStringLiteral("open");
        msg[QLatin1String("id")] = id;
        send(client, compact(msg));
        if (!isSubscribed(client))
            return;
    }
}

void SceneWidgetStream::onMessage(const QString& id, const QString& method,
                                  const QVariantMap& content, quint64 origin)
{
    if (_toolbars.isEmpty() && _all.isEmpty() && _panels.isEmpty())
        return;
    QJsonObject msg;
    if (method == QLatin1String("open"))
        msg = QJsonObject::fromVariantMap(content);
    else
        msg[QLatin1String("content")] = QJsonObject::fromVariantMap(content);
    msg[QLatin1String("op")] = QStringLiteral("widgets");
    msg[QLatin1String("method")] = method;
    msg[QLatin1String("id")] = id;
    const std::string json = compact(msg);
    const QSet<uint64_t> clients = _toolbars | _all | _panels;
    for (uint64_t client : clients) {
        if (client == origin || !wants(client, id))
            continue;
        send(client, json);
    }
}

void Gui::installSceneWidgetOps()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    registerSceneControlOp(QStringLiteral("widgets.subscribe"), true,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const bool toolbars = req.value(QLatin1String("toolbars")).toBool(false);
        const bool all = req.value(QLatin1String("all")).toBool(false);
        const bool panels = req.value(QLatin1String("panels")).toBool(false);
        SceneWidgetStream::instance().subscribe(client, toolbars, all, panels);
        QJsonObject reply = okReply(id);
        reply[QLatin1String("subscribed")] = toolbars || all || panels;
        // whether a task dialog is up, so a client tells "none" from
        // "pending" (the mirror starts with the subscription, and the
        // dialog up is mirrored at once)
        const QString panel = Fw::PanelMirror::instance().panelId();
        reply[QLatin1String("panel")] = panel.isEmpty() ? QJsonValue() : QJsonValue(panel);
        reply[QLatin1String("theme")] =
            getMainWindow() ? getMainWindow()->overrideIcons() : QString();
        reply[QLatin1String("locale")] = QLocale().name();
        return reply;
    });
    registerSceneControlOp(QStringLiteral("widgets.unsubscribe"), true,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        SceneWidgetStream::instance().unsubscribe(client);
        return okReply(req.value(QLatin1String("id")));
    });
    registerSceneControlOp(QStringLiteral("widgets.icon"), false,
                           [](const QJsonObject& req, const std::string&) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString name = req.value(QLatin1String("name")).toString();
        const int size = req.value(QLatin1String("size")).toInt(24);
        if (name.isEmpty())
            return sceneControlError(id, "BadRequest", QStringLiteral("name required"));
        QString format;
        const QByteArray data =
            BitmapFactory().iconSource(name.toUtf8().constData(), size, format);
        if (format.isEmpty())
            return sceneControlError(id, "UnknownIcon", name);
        QJsonObject reply = okReply(id);
        reply[QLatin1String("name")] = name;
        reply[QLatin1String("format")] = format;
        if (format == QLatin1String("svg")) {
            reply[QLatin1String("data")] = QString::fromUtf8(data);
        }
        else {
            reply[QLatin1String("data")] = QString::fromLatin1(data.toBase64());
            reply[QLatin1String("size")] = size > 0 ? size : 24;
        }
        return reply;
    });
    // an image a mirrored widget carries by id (docs/Sandbox.md 7.19
    // M2): a picture leaf's `pixmap`, a button's `icon`, a cell's
    // `icon` -- `img:<sha1>`, the PNG fetched once
    registerSceneControlOp(QStringLiteral("widgets.image"), false,
                           [](const QJsonObject& req, const std::string&) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString name = req.value(QLatin1String("name")).toString();
        if (name.isEmpty())
            return sceneControlError(id, "BadRequest", QStringLiteral("name required"));
        int width = 0, height = 0;
        const QByteArray data = Fw::ImageStore::instance().png(name, &width, &height);
        if (data.isEmpty())
            return sceneControlError(id, "UnknownImage", name);
        QJsonObject reply = okReply(id);
        reply[QLatin1String("name")] = name;
        reply[QLatin1String("format")] = QStringLiteral("png");
        reply[QLatin1String("data")] = QString::fromLatin1(data.toBase64());
        reply[QLatin1String("width")] = width;
        reply[QLatin1String("height")] = height;
        return reply;
    });
    registerSceneControlOp(QStringLiteral("widgets.update"), true,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString objectId = req.value(QLatin1String("target")).toString(
            req.value(QLatin1String("model")).toString());
        const QVariantMap state = req.value(QLatin1String("state")).toObject().toVariantMap();
        if (objectId.isEmpty())
            return sceneControlError(id, "BadRequest", QStringLiteral("target required"));
        if (!Fw::Store::instance().applyUpdate(objectId, state, client))
            return sceneControlError(id, "UnknownObject", objectId);
        return okReply(id);
    });
    registerSceneControlOp(QStringLiteral("widgets.custom"), true,
                           [](const QJsonObject& req, const std::string&, uint64_t client) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString objectId = req.value(QLatin1String("target")).toString(
            req.value(QLatin1String("model")).toString());
        const QVariantMap content =
            req.value(QLatin1String("content")).toObject().toVariantMap();
        if (objectId.isEmpty())
            return sceneControlError(id, "BadRequest", QStringLiteral("target required"));
        if (!Fw::Store::instance().applyCustom(objectId, content, client))
            return sceneControlError(id, "UnknownObject", objectId);
        return okReply(id);
    });
}

#include "moc_SceneWidgets.cpp"
