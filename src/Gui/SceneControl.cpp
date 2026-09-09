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

#include "Application.h"
#include "Document.h"
#include "SceneControl.h"
#include "SceneServeSource.h"
#include "View3DInventor.h"
#include "ViewProvider.h"
#include "ViewerContext.h"
#include "ViewProviderDocumentObject.h"
#include "Renderer/SceneServer.h"

using namespace Gui;

namespace {

/// The 3D view whose properties the client edits. A request bound to a
/// served document (\a boundDoc, the group the connection is joined to)
/// resolves that document's serving container first: the publisher that
/// owns the stream owns its container (docs/MultiDocServe.md §5) -- an
/// edit landing anywhere else republishes nothing, because only the
/// source's own container notifies it. Otherwise "the active one" as a
/// windowed session means it, then the active document's first 3D view,
/// then the first-served source's container -- the headless case where
/// nothing was ever activated by a user.
App::PropertyContainer *sceneView(const std::string &boundDoc = {})
{
    if (!boundDoc.empty()) {
        if (auto *doc =
                App::GetApplication().getDocument(boundDoc.c_str())) {
            if (auto *props = SceneServeSource::renderProperties(doc))
                return props;
        }
    }
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
        auto view = sceneView(boundDoc);
        if (!view)
            return errorReply(id, "UnknownObject", QStringLiteral("no 3D view"));
        QJsonObject reply;
        reply[QLatin1String("id")] = id;
        reply[QLatin1String("ok")] = true;
        reply[QLatin1String("doc")] = QString();
        reply[QLatin1String("obj")] = QString();
        reply[QLatin1String("subject")] = subject;
        reply[QLatin1String("label")] = QStringLiteral("3D view");
        reply[QLatin1String("type")] =
            QString::fromUtf8(view->getTypeId().getName());
        QJsonArray props;
        describeContainer(view, "view3d", props);
        reply[QLatin1String("props")] = props;
        return reply;
    }

    App::Document *doc = nullptr;
    const QString docName = req.value(QLatin1String("doc")).toString();
    // An unnamed document means the one this connection's group serves
    // when the handler is bound (a headless backend has no meaningful
    // "active" document); the active document remains the windowed
    // fallback.
    if (!docName.isEmpty())
        doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    else if (!boundDoc.empty())
        doc = App::GetApplication().getDocument(boundDoc.c_str());
    else
        doc = App::GetApplication().getActiveDocument();
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

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
    // the recompute below.
    if (target == QLatin1String("view3d")) {
        auto view = sceneView(boundDoc);
        if (!view)
            return errorReply(id, "UnknownObject", QStringLiteral("no 3D view"));
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

    App::Document *doc = nullptr;
    const QString docName = req.value(QLatin1String("doc")).toString();
    // An unnamed document means the one this connection's group serves
    // when the handler is bound (a headless backend has no meaningful
    // "active" document); the active document remains the windowed
    // fallback.
    if (!docName.isEmpty())
        doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    else if (!boundDoc.empty())
        doc = App::GetApplication().getDocument(boundDoc.c_str());
    else
        doc = App::GetApplication().getActiveDocument();
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

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

    App::Document *doc = nullptr;
    const QString docName = req.value(QLatin1String("doc")).toString();
    if (!docName.isEmpty())
        doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    else if (!boundDoc.empty())
        doc = App::GetApplication().getDocument(boundDoc.c_str());
    else
        doc = App::GetApplication().getActiveDocument();
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

    bool ok = false;
    try {
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

    App::Document *doc = nullptr;
    const QString docName = req.value(QLatin1String("doc")).toString();
    if (!docName.isEmpty())
        doc = App::GetApplication().getDocument(docName.toUtf8().constData());
    else if (!boundDoc.empty())
        doc = App::GetApplication().getDocument(boundDoc.c_str());
    else
        doc = App::GetApplication().getActiveDocument();
    if (!doc)
        return errorReply(id, "UnknownDocument", docName);

    Gui::Document *gdoc = Application::Instance->getDocument(doc);
    if (!gdoc)
        return errorReply(id, "UnknownDocument",
                          QString::fromUtf8(doc->getName()));

    gdoc->resetEdit();

    QJsonObject reply;
    reply[QLatin1String("id")] = id;
    reply[QLatin1String("ok")] = true;
    reply[QLatin1String("doc")] = QString::fromUtf8(doc->getName());
    return reply;
}

} // namespace

std::string Gui::handleSceneControlRequest(const std::string &json,
                                           const std::string &boundDoc,
                                           bool viewOnly,
                                           uint64_t client)
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
        // Only the semantic layer knows which ops write, which is why
        // the mode rides the request rather than being enforced by the
        // transport (docs/MultiDocServe.md §8). Reads stay answered —
        // a view-only client's property inspector keeps working.
        const bool mutating = op == QLatin1String("setProperty")
            || op == QLatin1String("edit")
            || op == QLatin1String("resetEdit");
        if (viewOnly && mutating)
            reply = errorReply(req.value(QLatin1String("id")), "ViewOnly",
                               QStringLiteral("this connection may not edit"));
        else if (op == QLatin1String("getProperties"))
            reply = getProperties(req, boundDoc);
        else if (op == QLatin1String("setProperty"))
            reply = setProperty(req, boundDoc);
        else if (op == QLatin1String("edit"))
            reply = setEditOp(req, boundDoc, client);
        else if (op == QLatin1String("resetEdit"))
            reply = resetEditOp(req, boundDoc);
        else
            reply = errorReply(req.value(QLatin1String("id")), "UnknownOp", op);
    }
    return QJsonDocument(reply)
        .toJson(QJsonDocument::Compact)
        .toStdString();
}

void Gui::installSceneControlHandler(const std::string &docName)
{
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
                                                      shared->viewOnly,
                                                      shared->client));
                }, Qt::QueuedConnection);
            }, docName);
}
