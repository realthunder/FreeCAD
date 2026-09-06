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

#include <Base/Console.h>

#include "Fw/FwStore.h"
#include "Fw/FwWidgets.h"

using namespace Gui::Fw;

namespace
{
const QString kPrefix = QStringLiteral("q_");
const QString kModelRef = QStringLiteral("IPY_MODEL_");
}  // namespace

Store::Store() = default;

Store& Store::instance()
{
    static Store store;
    return store;
}

bool Store::owns(const QVariantMap& state)
{
    return state.value(QStringLiteral("_model_module")).toString()
        == QLatin1String(moduleName());
}

Widget* Store::commOpen(const QString& id, const QVariantMap& state)
{
    if (Widget* old = object(id)) {
        Q_EMIT objectClosing(old);
        _ids.remove(old);
        _objects.remove(id);
        delete old;
    }
    QString modelName = state.value(QStringLiteral("_model_name")).toString();
    Widget* w = createWidget(modelName.isEmpty() ? QStringLiteral("QWidgetModel") : modelName);
    _objects.insert(id, w);
    _ids.insert(w, id);
    watch(w, id);
    applyState(w, state, true);
    ++_stats.opened;
    Q_EMIT objectOpened(w);
    return w;
}

bool Store::commUpdate(const QString& id, const QVariantMap& state)
{
    Widget* w = object(id);
    if (!w)
        return false;
    ++_stats.updated;
    applyState(w, state, false);
    return true;
}

bool Store::commCustom(const QString& id, const QVariantMap& content)
{
    Widget* w = object(id);
    if (!w)
        return false;
    ++_stats.customs;
    if (content.contains(QStringLiteral("layout"))) {
        // a layout op: the guest names widgets by comm id, the backend
        // wants the objects
        QVariantMap op = content;
        auto ref = op.constFind(QStringLiteral("widget"));
        if (ref != op.constEnd()) {
            Widget* target = resolve(ref->toString());
            op[QStringLiteral("widget")] = QVariant::fromValue<QObject*>(target);
        }
        w->forwardLayoutOp(op);
        return true;
    }
    if (content.contains(QStringLiteral("item"))) {
        // an item op on a view's rows (docs/Sandbox.md 7.11, G3b)
        if (auto view = qobject_cast<ItemView*>(w))
            view->applyItemOp(content);
        return true;
    }
    QString event = content.value(QStringLiteral("event")).toString();
    QVariantList args = content.value(QStringLiteral("args")).toList();
    if (event.isEmpty())
        return true;
    if (event == QLatin1String("setParent")) {
        QString pid = args.value(0).toString();
        Widget* parent = pid.isEmpty() ? nullptr : resolve(pid);
        w->setParent(parent);
        return true;
    }
    // a widget the guest names in a request (a tab's page, a scroll
    // area's content, an item's widget) is a model ref; the backend
    // wants the object
    for (QVariant& a : args) {
        if (a.typeId() == QMetaType::QString && a.toString().startsWith(kModelRef))
            a = QVariant::fromValue<QObject*>(resolve(a.toString()));
    }
    w->request(event, args);
    return true;
}

bool Store::commClose(const QString& id)
{
    Widget* w = object(id);
    if (!w) {
        _objects.remove(id);
        return false;
    }
    ++_stats.closed;
    Q_EMIT objectClosing(w);
    _ids.remove(w);
    _objects.remove(id);
    // the guest tree may have re-parented other store objects under
    // this one: they stay (their comms are open), as hidden top-levels
    for (Widget* child : w->childWidgets())
        if (_ids.contains(child))
            child->QObject::setParent(nullptr);
    delete w;
    return true;
}

void Store::watch(Widget* w, const QString& id)
{
    connect(w, &Widget::propertiesChanged, this, [this, w, id](const QStringList& names, int src) {
        if (src == static_cast<int>(Source::Guest) || !_sink)
            return;
        QVariantMap state;
        for (const QString& n : names)
            state.insert(kPrefix + n, w->property(n));
        ++_stats.sent;
        _sink(id, QStringLiteral("update"), state);
    });
    connect(w, &Widget::eventEmitted, this,
            [this, id](const QString& name, const QVariantList& args) {
                if (!_sink)
                    return;
                QVariantMap content;
                content.insert(QStringLiteral("event"), name);
                content.insert(QStringLiteral("args"), args);
                ++_stats.events;
                _sink(id, QStringLiteral("custom"), content);
            });
    connect(w, &QObject::destroyed, this, [this, id](QObject* obj) {
        _ids.remove(obj);
        auto it = _objects.find(id);
        if (it != _objects.end() && (it->isNull() || it->data() == obj))
            _objects.erase(it);
    });
}

void Store::applyState(Widget* w, const QVariantMap& state, bool initial)
{
    QVariantMap props;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        const QString& key = it.key();
        if (key.startsWith(kPrefix)) {
            QString name = key.mid(kPrefix.size());
            if (initial)
                w->setInitial(name, it.value());
            else
                props.insert(name, it.value());
        }
        else if (key == QLatin1String("qtClass")) {
            w->setQtClass(it->toString());
        }
        else if (key == QLatin1String("uiFile")) {
            if (auto form = qobject_cast<UiForm*>(w))
                form->setUiFile(it->toString());
        }
        else if (key == QLatin1String("widgets")) {
            auto form = qobject_cast<UiForm*>(w);
            if (!form)
                continue;
            QVariantMap refs = it->toMap();
            for (auto r = refs.constBegin(); r != refs.constEnd(); ++r) {
                Widget* child = resolve(r->toString());
                if (child)
                    form->addNamed(r.key(), child);
                else
                    Base::Console().Warning("Fw::Store: form %s names %s, not an object\n",
                                            qPrintable(form->uiFile()), qPrintable(r.key()));
            }
        }
    }
    if (!props.isEmpty())
        w->setProperties(props, Source::Guest);
    // after the values: the guest's own list is the truth
    auto touched = state.constFind(QStringLiteral("_touched"));
    if (touched != state.constEnd())
        w->setTouched(touched->toStringList());
}

Widget* Store::object(const QString& id) const
{
    auto it = _objects.constFind(id);
    if (it == _objects.constEnd())
        return nullptr;
    return it->data();
}

QString Store::idOf(const QObject* widget) const
{
    return _ids.value(widget);
}

Widget* Store::resolve(const QString& ref) const
{
    if (ref.startsWith(kModelRef))
        return object(ref.mid(kModelRef.size()));
    return object(ref);
}

QStringList Store::ids() const
{
    QStringList out;
    for (auto it = _objects.constBegin(); it != _objects.constEnd(); ++it)
        if (!it->isNull())
            out.append(it.key());
    return out;
}

void Store::reset()
{
    QList<QPointer<Widget>> all = _objects.values();
    _objects.clear();
    _ids.clear();
    for (const auto& p : all) {
        if (!p.isNull()) {
            Q_EMIT objectClosing(p.data());
            for (Widget* child : p->childWidgets())
                child->QObject::setParent(nullptr);
            delete p.data();
        }
    }
}

#include "moc_FwStore.cpp"
