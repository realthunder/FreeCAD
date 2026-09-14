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

#include <algorithm>
#include <map>

#include <set>
#include <vector>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <App/PropertyGeo.h>
#include <Base/Exception.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>

#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "MirrorViewer.h"
#include "OmniControl.h"
#include "OmniSearch.h"
#include "SceneControl.h"
#include "SceneWidgets.h"
#include "SceneControlP.h"
#include "SceneServeSource.h"
#include "Selection.h"
#include "View3DInventor.h"
#include "ViewProvider.h"
#include "ViewerContext.h"
#include "ViewProviderDocumentObject.h"
#include "Renderer/SceneServer.h"

using namespace Gui;

namespace Gui {
namespace SceneControlDetail {

/// The 3D view whose properties the client edits. A request bound to a
/// served document (\a boundDoc, the group the connection is joined to)
/// resolves that document's serving container first: the publisher that
/// owns the stream owns its container (docs/MultiDocServe.md §5) -- an
/// edit landing anywhere else republishes nothing, because only the
/// source's own container notifies it. Otherwise "the active one" as a
/// windowed session means it, then the active document's first 3D view,
/// then the first-served source's container -- the headless case where
/// nothing was ever activated by a user.
App::PropertyContainer *sceneView(const std::string &boundDoc)
{
    if (!boundDoc.empty()) {
        if (auto *doc =
                App::GetApplication().getDocument(boundDoc.c_str())) {
            if (auto *props = SceneServeSource::renderProperties(doc))
                return props;
        }
    }
    if (!Application::Instance)   // a test, or a console session
        return SceneServeSource::renderProperties();
    if (auto v = dynamic_cast<View3DInventor *>(
                Application::Instance->activeView()))
        return v;
    auto doc = Application::Instance->activeDocument();
    if (doc) {
        for (auto view : doc->getMDIViews()) {
            if (auto v = dynamic_cast<View3DInventor *>(view))
                return v;
        }
    }
    return SceneServeSource::renderProperties();
}

/// Ops registered by workbenches (registerSceneControlOp). A plain
/// static map: registration happens once per module at Gui init and
/// dispatch is GUI-thread only, so there is nothing to race with.
struct RegisteredOp {
    bool mutating;
    SceneControlOpHandler handler;
    SceneControlClientOpHandler clientHandler;
};

std::map<QString, RegisteredOp> &registeredOps()
{
    static std::map<QString, RegisteredOp> ops;
    return ops;
}

App::Document *homeDocument(const std::string &boundDoc)
{
    // An unnamed document means the one this connection's group serves
    // when the handler is bound (a headless backend has no meaningful
    // "active" document); the active document remains the windowed
    // fallback.
    if (!boundDoc.empty())
        return App::GetApplication().getDocument(boundDoc.c_str());
    return App::GetApplication().getActiveDocument();
}

bool documentAllowed(App::Document *doc, const std::string &boundDoc)
{
    if (!doc)
        return false;
    auto home = homeDocument(boundDoc);
    if (!home)
        return false;   // nothing to scope to: nothing is in reach
    if (doc == home)
        return true;
    // What the served scene already shows: the documents the home one
    // links out to, transitively. Walked over the objects' own out-
    // lists rather than PropertyXLink::getDocumentOutList(), which
    // knows only links whose target document has a file name -- both
    // documents unsaved and it would answer nothing, which is when a
    // link is newest, not when it is least real. The out-lists are
    // cached on the objects, so this is a walk over pointers.
    //
    // The in-list is deliberately not followed: that another document
    // links *into* this one says nothing about whether this connection
    // may read it.
    std::set<App::Document*> seen {home};
    std::vector<App::Document*> pending {home};
    while (!pending.empty()) {
        auto current = pending.back();
        pending.pop_back();
        for (auto obj : current->getObjects()) {
            for (auto dep : obj->getOutList()) {
                if (!dep || !dep->isAttachedToDocument())
                    continue;
                auto other = dep->getDocument();
                if (!other || !seen.insert(other).second)
                    continue;
                if (other == doc)
                    return true;
                pending.push_back(other);
            }
        }
    }
    return false;
}

App::Document *requestDocument(const QJsonObject &req, const std::string &boundDoc)
{
    const QString docName = req.value(QLatin1String("doc")).toString();
    if (docName.isEmpty())
        return homeDocument(boundDoc);
    auto doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    return documentAllowed(doc, boundDoc) ? doc : nullptr;
}

/// The container a "view3d" subject or target names: the served view,
/// or with \a req naming a "view" ("View2") that view of the request's
/// document (docs/OmniSearch.md sec 6). Null with \a code set when
/// there is none.
App::PropertyContainer *requestView(const QJsonObject &req, const std::string &boundDoc,
                                    QString &viewName, const char *&code)
{
    viewName = req.value(QLatin1String("view")).toString();
    code = nullptr;
    if (viewName.isEmpty() || viewName == QLatin1String("ActiveView")) {
        viewName.clear();
        auto view = sceneView(boundDoc);
        if (!view)
            code = "no 3D view";
        return view;
    }
    auto doc = requestDocument(req, boundDoc);
    auto view = doc ? OmniSearch::documentView(doc, viewName.toUtf8().constData()) : nullptr;
    if (!view)
        code = "no such view";
    return view;
}

QJsonObject errorReply(const QJsonValue &id, const char *code,
                       const QString &message)
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
        d[QLatin1String("unit")] = QString::fromStdString(p->getUnit().getString());
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

} // namespace SceneControlDetail
} // namespace Gui

namespace {

using namespace Gui::SceneControlDetail;

QJsonObject getProperties(const QJsonObject &req,
                          const std::string &boundDoc)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    // What is being inspected. The default is the object, which is
    // what every v0 client asks for without saying so. The other two
    // subjects are the containers a client cannot reach by picking:
    // nothing in the scene stands for the 3D view or the document.
    const QString subject = req.value(QLatin1String("subject")).toString();

    if (subject == QLatin1String("view3d")) {
        QString viewName;
        const char *why = nullptr;
        auto view = requestView(req, boundDoc, viewName, why);
        if (!view)
            return errorReply(id, "UnknownObject", QString::fromUtf8(why));
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("doc")] = QString();
        reply[QLatin1String("obj")] = QString();
        reply[QLatin1String("subject")] = subject;
        if (!viewName.isEmpty())
            reply[QLatin1String("view")] = viewName;
        reply[QLatin1String("label")] = viewName.isEmpty()
            ? QStringLiteral("3D view") : viewName;
        reply[QLatin1String("type")] =
            QString::fromUtf8(view->getTypeId().getName());
        QJsonArray props;
        describeContainer(view, "view3d", props);
        reply[QLatin1String("props")] = props;
        return reply;
    }

    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument",
                          req.value(QLatin1String("doc")).toString());

    if (subject == QLatin1String("document")) {
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
        reply[QLatin1String("obj")] = QString();
        reply[QLatin1String("subject")] = subject;
        reply[QLatin1String("label")] =
            QString::fromUtf8(doc->Label.getValue());
        reply[QLatin1String("type")] =
            QString::fromUtf8(doc->getTypeId().getName());
        QJsonArray props;
        describeContainer(doc, "document", props);
        reply[QLatin1String("props")] = props;
        return reply;
    }

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
    reply[QLatin1String("subject")] = QStringLiteral("object");
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

namespace Gui {
namespace SceneControlDetail {

/// Assign \a value to \a prop, mirroring describeProperty's type set.
/// Returns an error code, or null on success.
const char *assignProperty(App::Property *prop, const QJsonValue &value)
{
    auto tid = prop->getTypeId();
    if (tid.isDerivedFrom(App::PropertyBool::getClassTypeId())) {
        if (!value.isBool())
            return "BadValue";
        static_cast<App::PropertyBool *>(prop)->setValue(value.toBool());
    }
    else if (tid.isDerivedFrom(App::PropertyEnumeration::getClassTypeId())) {
        auto p = static_cast<App::PropertyEnumeration *>(prop);
        if (value.isDouble()) {
            int idx = int(value.toDouble());
            if (idx < 0 || idx >= int(p->getEnumVector().size()))
                return "ConstraintViolation";
            p->setValue(long(idx));
        }
        else if (value.isString()) {
            const QByteArray choice = value.toString().toUtf8();
            if (!p->isPartOf(choice.constData()))
                return "ConstraintViolation";
            p->setValue(choice.constData());
        }
        else
            return "BadValue";
    }
    else if (tid.isDerivedFrom(App::PropertyInteger::getClassTypeId())) {
        if (!value.isDouble())
            return "BadValue";
        long v = long(value.toDouble());
        if (tid.isDerivedFrom(
                    App::PropertyIntegerConstraint::getClassTypeId())) {
            if (const auto *c = static_cast<App::PropertyIntegerConstraint *>(
                        prop)->getConstraints()) {
                if (v < c->LowerBound || v > c->UpperBound)
                    return "ConstraintViolation";
            }
        }
        static_cast<App::PropertyInteger *>(prop)->setValue(v);
    }
    else if (tid.isDerivedFrom(App::PropertyFloat::getClassTypeId())) {
        // Covers PropertyQuantity: its value is set in the property's
        // own unit, which is exactly what the descriptor published.
        if (!value.isDouble())
            return "BadValue";
        double v = value.toDouble();
        bool constrained = false;
        double lower = 0, upper = 0;
        if (tid.isDerivedFrom(
                    App::PropertyQuantityConstraint::getClassTypeId())) {
            if (const auto *c = static_cast<
                        App::PropertyQuantityConstraint *>(prop)
                            ->getConstraints()) {
                constrained = true;
                lower = c->LowerBound;
                upper = c->UpperBound;
            }
        }
        else if (tid.isDerivedFrom(
                         App::PropertyFloatConstraint::getClassTypeId())) {
            if (const auto *c =
                        static_cast<App::PropertyFloatConstraint *>(prop)
                            ->getConstraints()) {
                constrained = true;
                lower = c->LowerBound;
                upper = c->UpperBound;
            }
        }
        if (constrained && (v < lower || v > upper))
            return "ConstraintViolation";
        static_cast<App::PropertyFloat *>(prop)->setValue(v);
    }
    else if (tid.isDerivedFrom(App::PropertyString::getClassTypeId())) {
        if (!value.isString())
            return "BadValue";
        static_cast<App::PropertyString *>(prop)->setValue(
                value.toString().toUtf8().constData());
    }
    else if (tid.isDerivedFrom(App::PropertyColor::getClassTypeId())) {
        const QString s = value.toString();
        if (s.size() != 7 || s[0] != QLatin1Char('#'))
            return "BadValue";
        bool ok = false;
        const uint rgb = s.mid(1).toUInt(&ok, 16);
        if (!ok)
            return "BadValue";
        App::Color c(float((rgb >> 16) & 0xff) / 255.0f,
                     float((rgb >> 8) & 0xff) / 255.0f,
                     float(rgb & 0xff) / 255.0f);
        static_cast<App::PropertyColor *>(prop)->setValue(c);
    }
    else if (tid.isDerivedFrom(App::PropertyVector::getClassTypeId())) {
        if (!value.isObject())
            return "BadValue";
        const QJsonObject o = value.toObject();
        static_cast<App::PropertyVector *>(prop)->setValue(
                o.value(QLatin1String("x")).toDouble(),
                o.value(QLatin1String("y")).toDouble(),
                o.value(QLatin1String("z")).toDouble());
    }
    else {
        return "NotEditable";
    }
    return nullptr;
}

} // namespace SceneControlDetail
} // namespace Gui

namespace {

QJsonObject setProperty(const QJsonObject &req,
                        const std::string &boundDoc)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    const QString target = req.value(QLatin1String("target")).toString();
    // The target names the container the descriptor came out of, so a
    // client can hand back the scope getProperties gave it and reach
    // the same property. The 3D view is the one container with no
    // document behind it: its properties are the session's, not the
    // model's, which is also why they are outside the transaction and
    // the recompute below. A named "view" is another view of the
    // request's document, addressed the same way.
    if (target == QLatin1String("view3d")) {
        QString viewName;
        const char *why = nullptr;
        auto view = requestView(req, boundDoc, viewName, why);
        if (!view)
            return errorReply(id, "UnknownObject", QString::fromUtf8(why));
        const QByteArray vname =
            req.value(QLatin1String("name")).toString().toUtf8();
        App::Property *vprop = view->getPropertyByName(vname.constData());
        if (!vprop)
            return errorReply(id, "UnknownProperty", QString::fromUtf8(vname));
        if ((view->getPropertyType(vprop) & App::Prop_ReadOnly)
                || vprop->testStatus(App::Property::ReadOnly)
                || vprop->testStatus(App::Property::Immutable))
            return errorReply(id, "ReadOnly", QString::fromUtf8(vname));
        if (const char *code =
                    assignProperty(vprop, req.value(QLatin1String("value"))))
            return errorReply(id, code, QString::fromUtf8(vname));
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("recomputed")] = false;
        return reply;
    }

    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument",
                          req.value(QLatin1String("doc")).toString());

    App::PropertyContainer *container = nullptr;
    if (target == QLatin1String("document"))
        container = doc;
    else {
        App::DocumentObject *obj =
            doc->getObject(req.value(QLatin1String("obj")).toString()
                               .toUtf8().constData());
        if (!obj)
            return errorReply(id, "UnknownObject",
                              req.value(QLatin1String("obj")).toString());
        container = obj;
        if (target == QLatin1String("view")) {
            container = Application::Instance->getViewProvider(obj);
            if (!container)
                return errorReply(id, "UnknownObject",
                                  QStringLiteral("no view provider"));
        }
    }
    const QByteArray name =
        req.value(QLatin1String("name")).toString().toUtf8();
    App::Property *prop = container->getPropertyByName(name.constData());
    if (!prop)
        return errorReply(id, "UnknownProperty",
                          QString::fromUtf8(name));
    short type = container->getPropertyType(prop);
    if ((type & App::Prop_ReadOnly)
            || prop->testStatus(App::Property::ReadOnly)
            || prop->testStatus(App::Property::Immutable))
        return errorReply(id, "ReadOnly", QString::fromUtf8(name));

    QJsonObject reply;
    try {
        // The transaction closes when this scope does, which puts the
        // whole edit-plus-recompute on the undo stack as one step.
        App::AutoTransaction transaction("Edit property");
        if (const char *code =
                    assignProperty(prop, req.value(QLatin1String("value"))))
            return errorReply(id, code, QString::fromUtf8(name));
        doc->recompute();
    }
    catch (Base::Exception &e) {
        return errorReply(id, "RecomputeFailed",
                          QString::fromUtf8(e.what()));
    }
    catch (std::exception &e) {
        return errorReply(id, "RecomputeFailed", QString::fromUtf8(e.what()));
    }
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("recomputed")] = true;
    return reply;
}

/// Enter an edit mode from a client (docs/ThinClient.md sec 8.9 step 4).
///
/// The one thing this has to get right that a desktop command does not
/// is WHICH view the session belongs to. Gui::Document::setEdit finds
/// one by asking the main window what is active, which in a process
/// serving several browsers names either nothing or somebody else's --
/// so the connection's own mirror is made current for the call
/// (Gui::ViewerScope) and setEdit binds to that.
///
/// A served document with no mirror for this connection is refused
/// rather than let through: setEdit's fallback would CREATE a 3D view
/// for the document, which is a thing only a desktop may do and which
/// in a serving process would open a GL context nobody asked for.
QJsonObject setEditOp(const QJsonObject &req, const std::string &boundDoc,
                      uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));

    // A named document only when it is in this connection's reach
    const QString docName = req.value(QLatin1String("doc")).toString();
    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

    Gui::Document *gdoc = Application::Instance->getDocument(doc);
    if (!gdoc)
        return errorReply(id, "UnknownDocument",
                          QString::fromUtf8(doc->getName()));

    const QString objName = req.value(QLatin1String("obj")).toString();
    App::DocumentObject *obj = doc->getObject(objName.toUtf8().constData());
    if (!obj)
        return errorReply(id, "UnknownObject", objName);
    ViewProvider *vp = Application::Instance->getViewProvider(obj);
    if (!vp)
        return errorReply(id, "NoViewProvider", objName);

    ViewerContext *viewer = nullptr;
    if (SceneServeSource *source = SceneServeSource::sourceFor(doc)) {
        viewer = source->viewerFor(client);
        if (!viewer)
            return errorReply(id, "NoView",
                              QStringLiteral("state a camera before editing"));
    }

    const int mode = req.value(QLatin1String("mode")).toInt(0);
    const QString subname = req.value(QLatin1String("subname")).toString();
    const QByteArray sub = subname.toUtf8();

    // The desktop's edit modes clear the selection as they start, as a
    // convenience -- and that convenience belongs to the room, not to the
    // client: what stops being highlighted is the object every viewer can
    // see, while everything the edit mode does with selection afterwards
    // is this client's own (docs/ThinClient.md sec 8.4). Inside the scope
    // below it would have cleared an instance that was empty anyway, and
    // left the sketch green in everybody's scene for the whole session.
    Gui::SelectionRoom().rmvPreselect();
    Gui::SelectionRoom().clearSelection();

    bool ok = false;
    try {
        // In the client's view, and so in the client's selection: an edit
        // mode's own observers attach while this is open, and an observer
        // that attached to the room here would hear nothing this browser
        // picked (SelectionObserver::attachSelectionToCurrent).
        ViewerScope scope(viewer);
        ok = gdoc->setEdit(vp, mode, subname.isEmpty() ? nullptr : sub.constData());
    }
    catch (Base::Exception &e) {
        return errorReply(id, "EditFailed",
                          QString::fromUtf8(e.what()));
    }
    if (!ok)
        return errorReply(id, "EditRefused", objName);

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    reply[QLatin1String("obj")] = objName;
    reply[QLatin1String("mode")] = mode;
    return reply;
}

/// Leave the edit mode this document is in, whoever started it. One
/// editing view provider per document is the first cut (sec 8.10), so
/// there is only ever one to leave.
QJsonObject resetEditOp(const QJsonObject &req, const std::string &boundDoc)
{
    const QJsonValue id = req.value(QLatin1String("id"));

    // A named document only when it is in this connection's reach
    const QString docName = req.value(QLatin1String("doc")).toString();
    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

    Gui::Document *gdoc = Application::Instance->getDocument(doc);
    if (!gdoc)
        return errorReply(id, "UnknownDocument",
                          QString::fromUtf8(doc->getName()));

    // No scope opened here: Gui::Document::resetEdit opens one over the
    // view it was running in, for every caller. It has to, because the
    // path that matters most does not come through this op at all --
    // Escape defers resetEdit through a timer, with no scope open
    // (docs/ThinClient.md sec 8.4 and sec 8.10).
    gdoc->resetEdit();

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    return reply;
}

} // namespace

namespace Gui {
namespace SceneControlDetail {

/// The browser allowlist, for the `command` op and `command.run` on a
/// connection that is not a host (the rationale is on runCommandOp below).
/// The web client keeps a copy to draw a button disabled rather than let
/// it be refused (web/src/control.ts); this one decides.
bool isBrowserSafeCommand(const QString &name)
{
    return name.startsWith(QLatin1String("Sketcher_Create"))
        || name == QLatin1String("Sketcher_External")
        || name == QLatin1String("Sketcher_CarbonCopy");
}

/// The command a group's member `index` (1-based) runs, by the route the
/// tool bar mirror names it: the member action's owning Action. Empty when
/// `group` is no group or the member is a separator or out of range.
QString groupMemberCommand(Command *group, int index)
{
    group->initAction();
    auto actions = qobject_cast<ActionGroup *>(group->getAction());
    if (!actions)
        return {};
    const QList<QAction *> members = actions->actions();
    if (index < 1 || index > members.size())
        return {};
    auto owner = qobject_cast<Action *>(members.at(index - 1)->parent());
    Command *cmd = owner ? owner->command() : nullptr;
    return cmd ? QString::fromUtf8(cmd->getName()) : QString();
}

} // namespace SceneControlDetail
} // namespace Gui

namespace {

/// Run a sketch tool in this client's view (docs/ThinClient.md sec 8.7).
///
/// A tool is what puts on-view parameters on the screen, and a browser has
/// no other way to start one: the sketcher's own shortcuts are Qt shortcuts
/// on a main window, and its ShortcutListener answers to Delete alone.
///
/// **The name is allowlisted, and narrowly.** A command in a serving process
/// is not the same authority as a command on a desktop: many of them open a
/// MODAL dialog, and a modal dialog on the GUI thread of a process serving
/// several browsers stops serving all of them, with nobody at the machine to
/// dismiss it. So this admits the Sketcher_Create* family -- which is exactly
/// the family that drives a DrawSketchHandler, and so exactly the family this
/// section is about -- plus the two pick tools of 8.11 item 3,
/// Sketcher_External and Sketcher_CarbonCopy, which activate a handler the
/// same way and open nothing. Widening it further is gated on an answer to
/// modality, not on taste.
///
/// A host connection (docs/ShareAccess.md sec 2.2) is not held to it: that
/// is the desktop's owner, who takes the modal risk as at the machine.
QJsonObject runCommandOp(const QJsonObject &req, const std::string &boundDoc,
                         uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));
    const QString name = req.value(QLatin1String("name")).toString();
    // "index": a group command's member, 1-based as the widget layer's
    // `commandIndex` counts (docs/Sandbox.md 7.18). The allowlist judges
    // what will RUN: a member of a group -- the Sketcher's create tools
    // sit in Sketcher_Comp* groups on its tool bars -- is admitted by its
    // own name, and a group named without an index by the group's, which
    // no group passes (the default member is the caller's to name).
    const int index = req.value(QLatin1String("index")).toInt(0);
    QByteArray cmd = name.toUtf8();
    QString member;
    const bool host = sceneControlAccess() == Render::ClientAccess::Host;
    if (index > 0) {
        Command *group = Application::Instance->commandManager()
                             .getCommandByName(cmd.constData());
        if (!group)
            return errorReply(id, "UnknownCommand", name);
        member = groupMemberCommand(group, index);
        if (member.isEmpty() || (!host && !isBrowserSafeCommand(member)))
            return errorReply(id, "CommandRefused",
                              member.isEmpty() ? name : member);
    }
    else if (!host && !isBrowserSafeCommand(name))
        return errorReply(id, "CommandRefused", name);

    // A named document only when it is in this connection's reach
    const QString docName = req.value(QLatin1String("doc")).toString();
    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

    ViewerContext *viewer = nullptr;
    if (SceneServeSource *source = SceneServeSource::sourceFor(doc)) {
        viewer = source->viewerFor(client);
        if (!viewer)
            return errorReply(id, "NoView",
                              QStringLiteral("state a camera before editing"));
    }
    // The tool asks its view for a cursor, for the on-view parameters and
    // for the editing root, and every one of those questions has a wrong
    // answer in a process with several browsers connected.
    ViewerScope scope(viewer);
    try {
        if (index > 0) {
            // A click on a member is two things on the desktop: the
            // member's own action runs its command, and the group's
            // ActionGroup::onActivated moves the default with
            // invoke(index, TriggerChildAction) -- which ONLY moves it, a
            // group's activated() runs nothing on that trigger. Both, then,
            // or the default moves and no tool starts.
            auto &manager = Application::Instance->commandManager();
            manager.runCommandByName(member.toUtf8().constData());
            if (Command *group = manager.getCommandByName(cmd.constData()))
                group->invoke(index - 1, Command::TriggerChildAction);
        }
        else
            Application::Instance->commandManager().runCommandByName(cmd.constData());
    }
    catch (Base::Exception &e) {
        return errorReply(id, "CommandFailed", QString::fromUtf8(e.what()));
    }

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("name")] = name;
    return reply;
}

/// Give one on-view entry box the keys, at the client's asking.
///
/// The only thing about those boxes a client decides. What is typed into
/// one is decided here -- the key goes up as an ordinary input frame and
/// reaches the box through DrawSketchKeyboardManager, the same rule and the
/// same widget the desktop uses (sec 8.7).
QJsonObject onViewFocusOp(const QJsonObject &req, const std::string &boundDoc,
                          uint64_t client)
{
    const QJsonValue id = req.value(QLatin1String("id"));

    // A named document only when it is in this connection's reach
    const QString docName = req.value(QLatin1String("doc")).toString();
    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

    SceneServeSource *source = SceneServeSource::sourceFor(doc);
    MirrorViewer *mirror = source ? source->mirrorViewerFor(client) : nullptr;
    if (!mirror)
        return errorReply(id, "NoView",
                          QStringLiteral("state a camera before editing"));

    const int index = req.value(QLatin1String("index")).toInt(-1);
    if (!mirror->focusOnViewParameter(index))
        return errorReply(id, "NoSuchParameter", QString::number(index));

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("index")] = index;
    return reply;
}


/// The `undo` and `redo` ops (docs/ThinClient.md 8.11 item 2): the
/// document's transactions, which under the shared session are everyone's
/// -- one document, one undo stack. What Ctrl+Z does on the desktop,
/// through Gui::Document::undo, and nothing else: no scope is opened, so
/// the room's selection is cleared as the desktop's is; then every
/// client's own instance is cleared too, and told, since a selection
/// made in the state being undone is as stale in one as in the other.
///
/// Refused, not prompted: Gui::Document::checkTransactionID asks through
/// a QMessageBox when a grouped transaction in another document has
/// others in front of it, and a modal on the GUI thread of a process
/// serving several browsers stops serving all of them. The reply carries
/// the stacks after the op, by transaction name, for a client's menu.
QJsonObject undoRedoOp(const QJsonObject &req, const std::string &boundDoc, bool redo)
{
    const QJsonValue id = req.value(QLatin1String("id"));

    // A named document only when it is in this connection's reach
    const QString docName = req.value(QLatin1String("doc")).toString();
    App::Document *doc = requestDocument(req, boundDoc);
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);
    Gui::Document *gdoc = Application::Instance->getDocument(doc);
    if (!gdoc)
        return errorReply(id, "UnknownDocument", QString::fromUtf8(doc->getName()));

    const QJsonValue stepsValue = req.value(QLatin1String("steps"));
    const int steps = stepsValue.isUndefined() ? 1 : stepsValue.toInt(0);
    if (steps < 1)
        return errorReply(id, "BadRequest", QStringLiteral("steps must be >= 1"));
    const int available = redo ? doc->getAvailableRedos() : doc->getAvailableUndos();
    if (available < steps)
        return errorReply(id, redo ? "NothingToRedo" : "NothingToUndo",
                          QStringLiteral("%1 of %2 available").arg(steps).arg(available));
    if (gdoc->undoRedoWouldPrompt(!redo, steps))
        return errorReply(id, "GroupedTransactions",
                          QStringLiteral("grouped transactions in other documents "
                                         "need the desktop user's answer"));

    if (redo)
        gdoc->redo(steps);
    else
        gdoc->undo(steps);
    if (SceneServeSource *source = SceneServeSource::sourceFor(doc))
        source->clearClientSelections();

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    QJsonArray undos;
    for (const auto &name : gdoc->getUndoVector())
        undos.append(QString::fromUtf8(name.c_str()));
    QJsonArray redos;
    for (const auto &name : gdoc->getRedoVector())
        redos.append(QString::fromUtf8(name.c_str()));
    reply[QLatin1String("undos")] = undos;
    reply[QLatin1String("redos")] = redos;
    return reply;
}

} // namespace

void Gui::registerSceneControlOp(const QString &op, bool mutating,
                                 SceneControlOpHandler handler)
{
    registeredOps()[op] = RegisteredOp{mutating, std::move(handler), nullptr};
}

void Gui::registerSceneControlOp(const QString &op, bool mutating,
                                 SceneControlClientOpHandler handler)
{
    registeredOps()[op] = RegisteredOp{mutating, nullptr, std::move(handler)};
}

QJsonObject Gui::sceneControlError(const QJsonValue &id, const char *code,
                                   const QString &message)
{
    return errorReply(id, code, message);
}

App::Document *Gui::sceneControlDocument(const QJsonObject &req,
                                         const std::string &boundDoc)
{
    return requestDocument(req, boundDoc);
}

namespace {

/// The access of the request being answered (sceneControlAccess). GUI
/// thread only, like the dispatch; a scope puts the outer value back, as
/// an op may run a nested event loop that answers another request.
Render::ClientAccess &currentAccess()
{
    static Render::ClientAccess access = Render::ClientAccess::Host;
    return access;
}

struct AccessScope
{
    explicit AccessScope(Render::ClientAccess access)
        : saved(currentAccess())
    {
        currentAccess() = access;
    }
    ~AccessScope()
    {
        currentAccess() = saved;
    }
    AccessScope(const AccessScope &) = delete;
    AccessScope &operator=(const AccessScope &) = delete;
    Render::ClientAccess saved;
};

} // namespace

Render::ClientAccess Gui::sceneControlAccess()
{
    return currentAccess();
}

std::string Gui::handleSceneControlRequest(const std::string &json,
                                           const std::string &boundDoc,
                                           Render::ClientAccess access,
                                           uint64_t client)
{
    // The catalogs the omni search mirror keeps announce themselves
    // when they change (docs/OmniSearch.md sec 6). Hooked from here,
    // not only from installSceneControlHandler(): a served document
    // routes its requests through its own handler (SceneServeSource),
    // and the first request is early enough. The widget stream's ops
    // likewise (docs/Sandbox.md 7.18): registered only by the default
    // group's handler, every widgets.* op on a served document was an
    // UnknownOp, and the tool bars never reached a browser.
    OmniControl::install();
    installSceneWidgetOps();
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
        // Only the semantic layer knows which ops write, which is why
        // the mode rides the request rather than being enforced by the
        // transport (docs/MultiDocServe.md §8). Reads stay answered —
        // a view-only client's property inspector keeps working.
        auto registered = registeredOps().find(op);
        const bool mutating = op == QLatin1String("setProperty")
            || op == QLatin1String("edit")
            || op == QLatin1String("resetEdit")
            || op == QLatin1String("command")
            || op == QLatin1String("onViewFocus")
            || op == QLatin1String("undo")
            || op == QLatin1String("redo")
            || (registered != registeredOps().end() && registered->second.mutating);
        // The few that reach beyond the document -- the host's
        // preferences -- need a host (docs/ShareAccess.md sec 2.2)
        const Render::ClientAccess required =
            std::max(mutating ? Render::ClientAccess::Edit : Render::ClientAccess::View,
                     OmniControl::requiredAccess(op));
        AccessScope scope(access);
        if (access < required && access == Render::ClientAccess::View)
            reply = errorReply(req.value(QLatin1String("id")), "ViewOnly",
                               QStringLiteral("this connection may not edit"));
        else if (access < required)
            reply = errorReply(req.value(QLatin1String("id")), "Forbidden",
                               QStringLiteral("this connection may not act on the host"));
        else if (op == QLatin1String("getProperties"))
            reply = getProperties(req, boundDoc);
        else if (op == QLatin1String("setProperty"))
            reply = setProperty(req, boundDoc);
        else if (registered != registeredOps().end())
            reply = registered->second.clientHandler
                ? registered->second.clientHandler(req, boundDoc, client)
                : registered->second.handler(req, boundDoc);
        else if (op == QLatin1String("edit"))
            reply = setEditOp(req, boundDoc, client);
        else if (op == QLatin1String("resetEdit"))
            reply = resetEditOp(req, boundDoc);
        else if (op == QLatin1String("command"))
            reply = runCommandOp(req, boundDoc, client);
        else if (op == QLatin1String("onViewFocus"))
            reply = onViewFocusOp(req, boundDoc, client);
        else if (op == QLatin1String("undo"))
            reply = undoRedoOp(req, boundDoc, false);
        else if (op == QLatin1String("redo"))
            reply = undoRedoOp(req, boundDoc, true);
        else if (OmniControl::handle(op, req, boundDoc, reply))
            ;
        else
            reply = errorReply(req.value(QLatin1String("id")), "UnknownOp", op);
    }
    return QJsonDocument(reply)
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

void Gui::installSceneControlHandler(const std::string &docName)
{
    // the widget stream's ops ride this channel (docs/Sandbox.md 7.18)
    installSceneWidgetOps();
    OmniControl::install();
    // Installed on the named document's group (empty = the default
    // group). The document is bound by NAME and re-resolved per request
    // on the GUI thread: a queued request must not carry a pointer
    // across the document's deletion.
    Render::SceneStreamServer::instance().setControlHandler(
            [docName](Render::SceneControlRequest &&req) {
                // Server connection thread: hop to the GUI thread (the
                // document is main-thread only), answer from there. The
                // reply hook is thread-safe and drops silently if the
                // viewer left meanwhile.
                auto shared = std::make_shared<Render::SceneControlRequest>(
                        std::move(req));
                QMetaObject::invokeMethod(qApp, [shared, docName]() {
                    shared->reply(
                            handleSceneControlRequest(shared->json,
                                                      docName,
                                                      shared->access,
                                                      shared->client));
                }, Qt::QueuedConnection);
            }, docName);
}
