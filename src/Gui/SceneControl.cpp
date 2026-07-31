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

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/PropertyGeo.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>

#include "Application.h"
#include "SceneControl.h"
#include "ViewProviderDocumentObject.h"
#include "Renderer/SceneServer.h"

using namespace Gui;

namespace {

QJsonObject errorReply(const QJsonValue &id, const char *code,
                       const QString &message = QString())
{
    QJsonObject reply;
    if (!id.isUndefined())
        reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = false;
    reply[QLatin1String("code")] = QString::fromUtf8(code);
    if (!message.isEmpty())
        reply[QLatin1String("message")] = message;
    return reply;
}

/// One property serialized to the descriptor the DOM inspector renders
/// from (docs/ThinClient.md §4.2) — the streamable generalization of
/// the TaskRenderSettings type→widget table.
QJsonObject describeProperty(const App::PropertyContainer *container,
                             const std::string &name,
                             const App::Property *prop,
                             const char *scope)
{
    QJsonObject d;
    d[QLatin1String("name")] = QString::fromStdString(name);
    d[QLatin1String("scope")] = QString::fromUtf8(scope);
    const char *group = container->getPropertyGroup(prop);
    d[QLatin1String("group")] = group && group[0]
        ? QString::fromUtf8(group) : QStringLiteral("Base");
    const char *docu = container->getPropertyDocumentation(prop);
    if (docu && docu[0])
        d[QLatin1String("doc")] = QString::fromUtf8(docu);

    short type = container->getPropertyType(prop);
    bool readonly = (type & App::Prop_ReadOnly)
        || prop->testStatus(App::Property::ReadOnly)
        || prop->testStatus(App::Property::Immutable);
    bool hidden = (type & App::Prop_Hidden)
        || prop->testStatus(App::Property::Hidden);

    auto tid = prop->getTypeId();
    if (tid.isDerivedFrom(App::PropertyBool::getClassTypeId())) {
        d[QLatin1String("type")] = QLatin1String("Bool");
        d[QLatin1String("value")] = static_cast<const App::PropertyBool *>(prop)->getValue();
    }
    else if (tid.isDerivedFrom(App::PropertyEnumeration::getClassTypeId())) {
        auto p = static_cast<const App::PropertyEnumeration *>(prop);
        d[QLatin1String("type")] = QLatin1String("Enum");
        QJsonArray enums;
        for (const auto &s : p->getEnumVector())
            enums.push_back(QString::fromUtf8(s.c_str()));
        d[QLatin1String("enums")] = enums;
        d[QLatin1String("value")] = p->isValid() ? int(p->getValue()) : 0;
    }
    else if (tid.isDerivedFrom(App::PropertyIntegerConstraint::getClassTypeId())) {
        auto p = static_cast<const App::PropertyIntegerConstraint *>(prop);
        d[QLatin1String("type")] = QLatin1String("Int");
        d[QLatin1String("value")] = double(p->getValue());
        if (const auto *c = p->getConstraints()) {
            QJsonObject lim;
            lim[QLatin1String("min")] = double(c->LowerBound);
            lim[QLatin1String("max")] = double(c->UpperBound);
            lim[QLatin1String("step")] = double(c->StepSize);
            d[QLatin1String("constraints")] = lim;
        }
    }
    else if (tid.isDerivedFrom(App::PropertyInteger::getClassTypeId())) {
        d[QLatin1String("type")] = QLatin1String("Int");
        d[QLatin1String("value")] = double(
                static_cast<const App::PropertyInteger *>(prop)->getValue());
    }
    else if (tid.isDerivedFrom(App::PropertyQuantity::getClassTypeId())) {
        auto p = static_cast<const App::PropertyQuantity *>(prop);
        d[QLatin1String("type")] = QLatin1String("Quantity");
        d[QLatin1String("value")] = p->getValue();
        d[QLatin1String("unit")] = p->getUnit().getString();
        if (tid.isDerivedFrom(
                    App::PropertyQuantityConstraint::getClassTypeId())) {
            if (const auto *c = static_cast<
                        const App::PropertyQuantityConstraint *>(prop)
                            ->getConstraints()) {
                QJsonObject lim;
                lim[QLatin1String("min")] = c->LowerBound;
                lim[QLatin1String("max")] = c->UpperBound;
                lim[QLatin1String("step")] = c->StepSize;
                d[QLatin1String("constraints")] = lim;
            }
        }
    }
    else if (tid.isDerivedFrom(App::PropertyFloatConstraint::getClassTypeId())) {
        auto p = static_cast<const App::PropertyFloatConstraint *>(prop);
        d[QLatin1String("type")] = QLatin1String("Float");
        d[QLatin1String("value")] = p->getValue();
        if (const auto *c = p->getConstraints()) {
            QJsonObject lim;
            lim[QLatin1String("min")] = c->LowerBound;
            lim[QLatin1String("max")] = c->UpperBound;
            lim[QLatin1String("step")] = c->StepSize;
            d[QLatin1String("constraints")] = lim;
        }
    }
    else if (tid.isDerivedFrom(App::PropertyFloat::getClassTypeId())) {
        d[QLatin1String("type")] = QLatin1String("Float");
        d[QLatin1String("value")] =
            static_cast<const App::PropertyFloat *>(prop)->getValue();
    }
    else if (tid.isDerivedFrom(App::PropertyString::getClassTypeId())) {
        d[QLatin1String("type")] = QLatin1String("String");
        d[QLatin1String("value")] = QString::fromUtf8(
                static_cast<const App::PropertyString *>(prop)->getValue());
    }
    else if (tid.isDerivedFrom(App::PropertyColor::getClassTypeId())) {
        auto c = static_cast<const App::PropertyColor *>(prop)->getValue();
        d[QLatin1String("type")] = QLatin1String("Color");
        char hex[10];
        std::snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                      int(c.r * 255.0f + 0.5f), int(c.g * 255.0f + 0.5f),
                      int(c.b * 255.0f + 0.5f));
        d[QLatin1String("value")] = QString::fromUtf8(hex);
    }
    else if (tid.isDerivedFrom(App::PropertyVector::getClassTypeId())) {
        auto v = static_cast<const App::PropertyVector *>(prop)->getValue();
        d[QLatin1String("type")] = QLatin1String("Vector");
        QJsonObject vec;
        vec[QLatin1String("x")] = v.x;
        vec[QLatin1String("y")] = v.y;
        vec[QLatin1String("z")] = v.z;
        d[QLatin1String("value")] = vec;
    }
    else if (tid.isDerivedFrom(App::PropertyPlacement::getClassTypeId())) {
        const auto &pl = static_cast<const App::PropertyPlacement *>(prop)
                             ->getValue();
        d[QLatin1String("type")] = QLatin1String("Placement");
        QJsonObject o;
        QJsonObject pos;
        pos[QLatin1String("x")] = pl.getPosition().x;
        pos[QLatin1String("y")] = pl.getPosition().y;
        pos[QLatin1String("z")] = pl.getPosition().z;
        o[QLatin1String("position")] = pos;
        Base::Vector3d axis;
        double angle = 0;
        pl.getRotation().getRawValue(axis, angle);
        QJsonObject rot;
        rot[QLatin1String("axisX")] = axis.x;
        rot[QLatin1String("axisY")] = axis.y;
        rot[QLatin1String("axisZ")] = axis.z;
        rot[QLatin1String("angle")] = angle * 180.0 / M_PI;
        o[QLatin1String("rotation")] = rot;
        d[QLatin1String("value")] = o;
        readonly = true;   // v0: structured display, no editor yet
    }
    else {
        // Everything else renders as an inert row: the raw type tells
        // the client (and us, in its bug reports) what an editor is
        // still missing.
        d[QLatin1String("type")] = QString::fromUtf8(tid.getName());
        d[QLatin1String("value")] = QJsonValue();
        readonly = true;
    }

    d[QLatin1String("readonly")] = readonly;
    d[QLatin1String("hidden")] = hidden;
    return d;
}

void describeContainer(const App::PropertyContainer *container,
                       const char *scope, QJsonArray &out)
{
    std::map<std::string, App::Property *> props;
    container->getPropertyMap(props);
    for (const auto &v : props) {
        if (!v.second)
            continue;
        out.push_back(describeProperty(container, v.first, v.second, scope));
    }
}

QJsonObject getProperties(const QJsonObject &req)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    App::Document *doc = nullptr;
    const QString docName = req.value(QLatin1String("doc")).toString();
    if (docName.isEmpty())
        doc = App::GetApplication().getActiveDocument();
    else
        doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

    const QString objName = req.value(QLatin1String("obj")).toString();
    App::DocumentObject *obj =
        doc->getObject(objName.toUtf8().constData());
    if (!obj)
        return errorReply(id, "UnknownObject", objName);

    bool wantObject = true, wantView = true;
    if (req.value(QLatin1String("scope")).isArray()) {
        wantObject = wantView = false;
        for (const auto &s : req.value(QLatin1String("scope")).toArray()) {
            if (s.toString() == QLatin1String("object"))
                wantObject = true;
            else if (s.toString() == QLatin1String("view"))
                wantView = true;
        }
    }

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    reply[QLatin1String("obj")] = QString::fromUtf8(obj->getNameInDocument());
    reply[QLatin1String("label")] = QString::fromUtf8(obj->Label.getValue());
    reply[QLatin1String("type")] = QString::fromUtf8(obj->getTypeId().getName());

    QJsonArray props;
    if (wantObject)
        describeContainer(obj, "object", props);
    if (wantView) {
        if (auto vp = Application::Instance->getViewProvider(obj))
            describeContainer(vp, "view", props);
    }
    reply[QLatin1String("props")] = props;
    return reply;
}

} // namespace

std::string Gui::handleSceneControlRequest(const std::string &json)
{
    QJsonParseError err;
    QJsonDocument parsed = QJsonDocument::fromJson(
            QByteArray(json.data(), int(json.size())), &err);
    QJsonObject reply;
    if (parsed.isNull() || !parsed.isObject()) {
        reply = errorReply(QJsonValue(), "BadRequest", err.errorString());
    }
    else {
        const QJsonObject req = parsed.object();
        const QString op = req.value(QLatin1String("op")).toString();
        if (op == QLatin1String("getProperties"))
            reply = getProperties(req);
        else
            reply = errorReply(req.value(QLatin1String("id")), "UnknownOp", op);
    }
    return QJsonDocument(reply)
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

void Gui::installSceneControlHandler()
{
    Render::SceneStreamServer::instance().setControlHandler(
            [](Render::SceneControlRequest &&req) {
                // Server connection thread: hop to the GUI thread (the
                // document is main-thread only), answer from there. The
                // reply hook is thread-safe and drops silently if the
                // viewer left meanwhile.
                auto shared = std::make_shared<Render::SceneControlRequest>(
                        std::move(req));
                QMetaObject::invokeMethod(qApp, [shared]() {
                    shared->reply(
                            handleSceneControlRequest(shared->json));
                }, Qt::QueuedConnection);
            });
}
