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
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QTimer>

#include <optional>

#include <App/DocumentObject.h>
#include <App/ExpressionParser.h>
#include <App/ExpressionSecurityRuntime.h>
#include <App/ObjectIdentifier.h>
#include <App/PropertyExpressionEngine.h>
#include <Base/Quantity.h>

#include "BitmapFactory.h"
#include "ExpressionBinding.h"
#include "ExpressionCompleter.h"
#include "Fw/FwImage.h"
#include "Fw/FwQtView.h"
#include "Fw/FwPanelMirror.h"
#include "Fw/FwStore.h"
#include "Fw/FwToolBarMirror.h"
#include "Fw/FwWidgets.h"
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

/// Where a field's binding lives (docs/Sandbox.md 7.23).
///
/// It depends on how the field got here, and both ops need the answer.
/// A guest-built form binds the MODEL. A mirrored desktop panel (7.19)
/// does not: its model carries `binding` as a string read off the real
/// widget, and the widget is what is actually bound -- so the live Pad
/// panel answered an empty completion list, and would have answered
/// "NotBound" to an expression, until this asked the widget too.
std::optional<App::ObjectIdentifier> boundPathOf(Fw::Widget* model)
{
    if (auto bound = dynamic_cast<Fw::ExpressionBound*>(model)) {
        if (bound->isBound())
            return bound->boundPath();
    }
    auto real = dynamic_cast<Gui::ExpressionBinding*>(Gui::FwQt::widgetOf(model));
    if (real && real->isBound())
        return real->boundPath();
    return std::nullopt;
}

/// The live result line for an expression being typed, evaluated but
/// NOT set -- what the desktop's expression dialog shows under the
/// editor.  Never throws: an expression is invalid for most of the time
/// it is being typed, so the reason is something to show.
QJsonObject previewExpression(const QJsonValue& id, const App::ObjectIdentifier& path,
                              const QString& text)
{
    QJsonObject reply = okReply(id);
    QString result;
    QString severity = QStringLiteral("ok");
    QString message;
    App::DocumentObject* obj = path.getDocumentObject();
    if (obj && !text.trimmed().isEmpty()) {
        try {
            // A remote client must not cause side effects merely by
            // typing, so function calls stay disabled in a preview
            // whatever the desktop's own "Evaluate function" switch says
            // -- that switch is the desktop user's choice for their own
            // keyboard, not for everyone holding a link.
            App::ExpressionFunctionCallDisabler noCalls(true);
            App::ExpressionSecurity::Runtime::Scope scope("session");
            std::shared_ptr<App::Expression> expr(
                App::Expression::parse(obj, text.toUtf8().constData()));
            if (!expr)
                throw Base::RuntimeError("not an expression");
            const std::string invalid = obj->ExpressionEngine.validateExpression(path, expr);
            if (!invalid.empty())
                throw Base::RuntimeError(invalid.c_str());
            std::unique_ptr<App::Expression> value(expr->eval());
            if (auto number = dynamic_cast<App::NumberExpression*>(value.get()))
                result = QString::fromStdString(number->getQuantity().getUserString());
            else if (value)
                result = QString::fromStdString(value->toString());
        }
        catch (App::ExpressionFunctionDisabledException&) {
            severity = QStringLiteral("warning");
            message = QStringLiteral("Functions are not evaluated in a preview.");
        }
        catch (Base::Exception& e) {
            const QString what = QString::fromUtf8(e.what());
            // Half an expression is not an error to shout about. A
            // trailing dot -- the moment completion is most wanted --
            // parses as "unexpected end of input", and a red line under
            // every keystroke would make the dialog unusable. The
            // desktop's own dialog blanks exactly this case
            // (DlgExpressionInput::onTimer), so this does too.
            if (!what.startsWith(QLatin1String("syntax error, unexpected end of input"))) {
                severity = QStringLiteral("error");
                message = what;
            }
        }
        catch (...) {
            severity = QStringLiteral("error");
            message = QStringLiteral("could not be evaluated");
        }
    }
    reply[QLatin1String("result")] = result;
    reply[QLatin1String("severity")] = severity;
    reply[QLatin1String("message")] = message;
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
        connect(&Fw::Store::instance(), &Fw::Store::messageTo, this,
                &SceneWidgetStream::onMessageTo);
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

// The host's answer to one client's own write (docs/Sandbox.md 7.22):
// the corrected keys, to that client only.  Never an `open`, so the
// content always nests.
void SceneWidgetStream::onMessageTo(quint64 client, const QString& id, const QString& method,
                                    const QVariantMap& content)
{
    if (!isSubscribed(client) || !wants(client, id))
        return;
    QJsonObject msg;
    msg[QLatin1String("content")] = QJsonObject::fromVariantMap(content);
    msg[QLatin1String("op")] = QStringLiteral("widgets");
    msg[QLatin1String("method")] = method;
    msg[QLatin1String("id")] = id;
    send(client, compact(msg));
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
        // and the top-level dialogs up (M3), in show order: a dialog a
        // client subscribes under is mirrored at once too
        QJsonArray dialogs;
        for (const QString& did : Fw::PanelMirror::instance().dialogIds())
            dialogs.append(did);
        reply[QLatin1String("dialogs")] = dialogs;
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
    // Completion for a bound field (docs/Sandbox.md 7.23).  NOT
    // mutating: a view-only client may complete -- it reads names the
    // inspector already shows it -- and still may not write.
    //
    // The completer is built HERE, for this request, on the field's
    // bound object.  Never the one a desktop widget is using:
    // setCompletionPrefix and the tokenizer are mutable state, and the
    // desktop user's caret is not this client's.  Per request rather
    // than cached, because what a completer knows is the document's
    // object and property tree, and a cached one is stale exactly when
    // a completion matters.  It raises no popup -- that is what
    // ExpressionCompleter::complete() is for.
    registerSceneControlOp(QStringLiteral("widgets.complete"), false,
                           [](const QJsonObject& req, const std::string&) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString objectId = req.value(QLatin1String("target")).toString(
            req.value(QLatin1String("model")).toString());
        if (objectId.isEmpty())
            return sceneControlError(id, "BadRequest", QStringLiteral("target required"));
        Fw::Widget* model = Fw::Store::instance().object(objectId);
        if (!model)
            return sceneControlError(id, "UnknownObject", objectId);
        const QString text = req.value(QLatin1String("text")).toString();
        int pos = req.value(QLatin1String("pos")).toInt(text.size());
        if (pos < 0 || pos > text.size())
            pos = text.size();
        int start = 0;
        int end = 0;
        QJsonArray items;
        QJsonArray details;
        const std::optional<App::ObjectIdentifier> path = boundPathOf(model);
        App::DocumentObject* obj = path ? path->getDocumentObject() : nullptr;
        if (obj) {
            // leadChar 0: the tokenizer skips a leading '=' whatever the
            // lead char is, so `Pad.Length` and `=Pad.Length` both
            // tokenize
            ExpressionCompleter completer(obj);
            QStringList tips;
            const QStringList found = completer.completionsFor(text, pos, start, end, &tips);
            for (const QString& s : found)
                items.append(s);
            for (const QString& s : tips)
                details.append(s);
        }
        // A target with nothing to complete against -- an unbound field,
        // a label -- answers an empty list rather than an error: a
        // client asks before it can know, and an error would read as a
        // fault in the panel.
        QJsonObject reply = okReply(id);
        reply[QLatin1String("items")] = items;
        reply[QLatin1String("details")] = details;
        reply[QLatin1String("start")] = start;
        reply[QLatin1String("end")] = end;
        return reply;
    });
    // Set or clear a bound field's expression (docs/Sandbox.md 7.23).
    // Its own op rather than a `q_expression` write: nothing routes that
    // key to the binding, wiring it into the property path would
    // re-enter syncExpression (which writes that same key), and a parse
    // error needs somewhere to go that a property write has not got.
    registerSceneControlOp(QStringLiteral("widgets.expression"), true,
                           [](const QJsonObject& req, const std::string&, uint64_t) {
        const QJsonValue id = req.value(QLatin1String("id"));
        const QString objectId = req.value(QLatin1String("target")).toString(
            req.value(QLatin1String("model")).toString());
        if (objectId.isEmpty())
            return sceneControlError(id, "BadRequest", QStringLiteral("target required"));
        Fw::Widget* model = Fw::Store::instance().object(objectId);
        if (!model)
            return sceneControlError(id, "UnknownObject", objectId);
        const std::optional<App::ObjectIdentifier> path = boundPathOf(model);
        if (!path)
            return sceneControlError(id, "NotBound", objectId);
        const QString text = req.value(QLatin1String("text")).toString();
        // The live result line, evaluated and not set: the dialog asks
        // for this on every keystroke while someone types.
        if (req.value(QLatin1String("preview")).toBool())
            return previewExpression(id, *path, text);

        auto bound = dynamic_cast<Fw::ExpressionBound*>(model);
        if (bound && bound->isBound()) {
            QString error;
            if (!bound->setExpressionText(text, &error))
                return sceneControlError(id, "BadExpression", error);
            return okReply(id);
        }
        // A mirrored panel's model is not bound, so the expression goes
        // to the document, which is where the model's own path would
        // have put it: the real widget and every client hear it back
        // through the expression-changed signal.
        App::DocumentObject* obj = path->getDocumentObject();
        if (!obj)
            return sceneControlError(id, "NotBound", objectId);
        try {
            std::shared_ptr<App::Expression> expr;
            if (!text.trimmed().isEmpty()) {
                // parse() hands back an App::ExpressionPtr, not a raw
                // pointer; the document wants a shared one.
                App::ExpressionPtr parsed(
                    App::Expression::parse(obj, text.toUtf8().constData()));
                if (!parsed)
                    throw Base::RuntimeError("not an expression");
                expr = std::move(parsed);
                const std::string invalid = obj->ExpressionEngine.validateExpression(*path, expr);
                if (!invalid.empty())
                    throw Base::RuntimeError(invalid.c_str());
            }
            obj->setExpression(*path, expr);   // an empty expression clears it
        }
        catch (Base::Exception& e) {
            return sceneControlError(id, "BadExpression", QString::fromUtf8(e.what()));
        }
        return okReply(id);
    });
}

#include "moc_SceneWidgets.cpp"
