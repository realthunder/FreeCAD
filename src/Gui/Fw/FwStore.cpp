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

#include <algorithm>
#include <functional>

#include "Fw/FwStore.h"
#include "Fw/FwWidgets.h"

using namespace Gui::Fw;

namespace
{
const QString kPrefix = QStringLiteral("q_");
const QString kModelRef = QStringLiteral("IPY_MODEL_");

/// A model ref (`IPY_MODEL_<id>`) anywhere in a value -- a string, a
/// list, a map -- becomes the object it names (a property whose value
/// is a widget: a line edit's actions, a bar's toggle action).
QVariant resolveRefs(const Store& store, const QVariant& v)
{
    switch (v.typeId()) {
        case QMetaType::QString: {
            QString s = v.toString();
            if (s.startsWith(kModelRef))
                return QVariant::fromValue<QObject*>(store.resolve(s));
            return v;
        }
        case QMetaType::QVariantList: {
            QVariantList out;
            for (const QVariant& x : v.toList())
                out.append(resolveRefs(store, x));
            return out;
        }
        case QMetaType::QVariantMap: {
            QVariantMap out;
            QVariantMap in = v.toMap();
            for (auto it = in.constBegin(); it != in.constEnd(); ++it)
                out.insert(it.key(), resolveRefs(store, it.value()));
            return out;
        }
        default:
            return v;
    }
}

/// The reverse: an object in an event's arguments crosses as its ref.
QVariant refsOf(const Store& store, const QVariant& v)
{
    if (v.canConvert<QObject*>() && v.typeId() != QMetaType::QString) {
        QObject* o = v.value<QObject*>();
        QString id = o ? store.idOf(o) : QString();
        return id.isEmpty() ? QVariant() : QVariant(kModelRef + id);
    }
    if (v.typeId() == QMetaType::QVariantList) {
        QVariantList out;
        for (const QVariant& x : v.toList())
            out.append(refsOf(store, x));
        return out;
    }
    if (v.typeId() == QMetaType::QVariantMap) {
        QVariantMap out;
        QVariantMap in = v.toMap();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it)
            out.insert(it.key(), refsOf(store, it.value()));
        return out;
    }
    return v;
}

/// The refs (`IPY_MODEL_<id>` strings) anywhere in a value.
void collectRefs(const QVariant& v, QStringList& out)
{
    switch (v.typeId()) {
        case QMetaType::QString: {
            const QString s = v.toString();
            if (s.startsWith(kModelRef))
                out.append(s.mid(kModelRef.size()));
            break;
        }
        case QMetaType::QVariantList:
            for (const QVariant& x : v.toList())
                collectRefs(x, out);
            break;
        case QMetaType::QVariantMap: {
            const QVariantMap m = v.toMap();
            for (auto it = m.constBegin(); it != m.constEnd(); ++it)
                collectRefs(it.value(), out);
            break;
        }
        default:
            break;
    }
}

quint64& originSlot()
{
    static quint64 origin = 0;
    return origin;
}

/// A layout spec from the guest (`{"class", "name", "items", "margins",
/// "spacing"}`, docs/Sandbox.md 7.11, G3c) as a Layout tree, refs
/// resolved; silent (no owner yet, so nothing is notified).
Layout* layoutFromSpec(const Store& store, const QVariantMap& spec)
{
    Layout* lay = createLayout(spec.value(QStringLiteral("class")).toString());
    lay->setObjectName(spec.value(QStringLiteral("name")).toString());
    for (const QVariant& v : spec.value(QStringLiteral("items")).toList()) {
        QVariantMap item = v.toMap();
        QVariantList pos = item.value(QStringLiteral("pos")).toList();
        if (item.contains(QStringLiteral("widget"))) {
            if (Widget* w = store.resolve(item.value(QStringLiteral("widget")).toString()))
                lay->addWidget(w, pos);
        }
        else if (item.contains(QStringLiteral("layout"))) {
            lay->addLayout(layoutFromSpec(store, item.value(QStringLiteral("layout")).toMap()),
                           pos);
        }
        else if (item.contains(QStringLiteral("action"))) {
            if (Widget* a = store.resolve(item.value(QStringLiteral("action")).toString()))
                lay->addAction(a);
        }
        else if (item.contains(QStringLiteral("separator"))) {
            lay->addSeparator();
        }
        else if (item.contains(QStringLiteral("stretch"))) {
            lay->addStretch(item.value(QStringLiteral("stretch")).toInt());
        }
        else if (item.contains(QStringLiteral("spacing"))) {
            lay->addSpacing(item.value(QStringLiteral("spacing")).toInt());
        }
        else if (item.contains(QStringLiteral("spacer"))) {
            QVariantList size = item.value(QStringLiteral("spacer")).toList();
            lay->addSpacer(size.value(0).toInt(), size.value(1).toInt(), pos,
                           size.value(2, 1).toInt(), size.value(3, 1).toInt());
        }
    }
    QVariantList margins = spec.value(QStringLiteral("margins")).toList();
    if (margins.size() == 4)
        lay->setContentsMargins(margins.at(0).toInt(), margins.at(1).toInt(),
                                margins.at(2).toInt(), margins.at(3).toInt());
    if (spec.contains(QStringLiteral("spacing")))
        lay->setSpacing(spec.value(QStringLiteral("spacing")).toInt());
    return lay;
}
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

Store::OriginScope::OriginScope(quint64 origin)
    : _saved(originSlot())
{
    originSlot() = origin;
}

Store::OriginScope::~OriginScope()
{
    originSlot() = _saved;
}

quint64 Store::currentOrigin()
{
    return originSlot();
}

void Store::announce(const QString& id, const QString& method, const QVariantMap& content)
{
    Q_EMIT message(id, method, content, originSlot());
}

void Store::insert(const QString& id, Widget* w)
{
    if (Widget* old = object(id)) {
        Q_EMIT objectClosing(old);
        _ids.remove(old);
        _objects.remove(id);
        _adopted.remove(id);
        delete old;
    }
    _objects.insert(id, w);
    _ids.insert(w, id);
    watch(w, id);
}

Widget* Store::commOpen(const QString& id, const QVariantMap& state)
{
    if (isAdopted(id))
        return nullptr;
    QString modelName = state.value(QStringLiteral("_model_name")).toString();
    Widget* w = createWidget(modelName.isEmpty() ? QStringLiteral("QWidgetModel") : modelName);
    insert(id, w);
    applyState(w, state, true);
    ++_stats.opened;
    Q_EMIT objectOpened(w);
    announce(id, QStringLiteral("open"), snapshot(id));
    return w;
}

void Store::adopt(const QString& id, Widget* w)
{
    if (!w || id.isEmpty())
        return;
    insert(id, w);
    _adopted.insert(id);
    ++_stats.opened;
    Q_EMIT objectOpened(w);
    announce(id, QStringLiteral("open"), snapshot(id));
}

bool Store::release(const QString& id)
{
    if (!isAdopted(id))
        return false;
    Widget* w = object(id);
    _adopted.remove(id);
    _objects.remove(id);
    if (w) {
        Q_EMIT objectClosing(w);
        _ids.remove(w);
        disconnect(w, nullptr, this, nullptr);
    }
    announce(id, QStringLiteral("close"), QVariantMap());
    return true;
}

QVariantMap Store::snapshot(const QString& id) const
{
    Widget* w = object(id);
    if (!w)
        return QVariantMap();
    QVariantMap out;
    out.insert(QStringLiteral("model"), w->modelName());
    out.insert(QStringLiteral("qtClass"), w->qtClass());
    QVariantMap state;
    const QVariantMap& props = w->properties();
    for (auto it = props.constBegin(); it != props.constEnd(); ++it)
        state.insert(kPrefix + it.key(), refsOf(*this, it.value()));
    out.insert(QStringLiteral("state"), state);
    if (Layout* lay = w->layout())
        out.insert(QStringLiteral("layout"), refsOf(*this, lay->spec()));
    if (Widget* parent = w->parentWidget()) {
        const QString pid = idOf(parent);
        if (!pid.isEmpty())
            out.insert(QStringLiteral("parent"), kModelRef + pid);
    }
    return out;
}

QStringList Store::snapshotOrder() const
{
    QStringList order;
    QSet<QString> done;
    QStringList all = ids();
    std::sort(all.begin(), all.end());
    std::function<void(const QString&, int)> visit = [&](const QString& id, int depth) {
        if (done.contains(id) || !_objects.contains(id) || depth > 64)
            return;
        done.insert(id);
        // what the object refers to (its state, its layout) comes
        // first; its parent does NOT: a container names its children
        // through its layout, and following the parent back would put
        // the container before its last child
        QVariantMap snap = snapshot(id);
        snap.remove(QStringLiteral("parent"));
        QStringList refs;
        collectRefs(snap, refs);
        for (const QString& r : refs)
            visit(r, depth + 1);
        order.append(id);
    };
    for (const QString& id : all)
        visit(id, 0);
    return order;
}

bool Store::applyUpdate(const QString& id, const QVariantMap& state, quint64 origin)
{
    Widget* w = object(id);
    if (!w)
        return false;
    OriginScope scope(origin);
    ++_stats.updated;
    applyState(w, state, false, Source::Backend);
    return true;
}

bool Store::applyCustom(const QString& id, const QVariantMap& content, quint64 origin)
{
    OriginScope scope(origin);
    return commCustom(id, content);
}

void Store::notifyLayout(const QString& id)
{
    Widget* w = object(id);
    if (!w)
        return;
    QVariantMap content;
    Layout* lay = w->layout();
    content.insert(QStringLiteral("layoutSpec"), lay ? refsOf(*this, lay->spec()) : QVariant());
    announce(id, QStringLiteral("update"), content);
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
        for (const char* key : {"widget", "action"}) {
            auto ref = op.constFind(QLatin1String(key));
            if (ref != op.constEnd())
                op[QLatin1String(key)] = QVariant::fromValue<QObject*>(resolve(ref->toString()));
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
    if (isAdopted(id))
        return false;
    Widget* w = object(id);
    if (!w) {
        _objects.remove(id);
        return false;
    }
    ++_stats.closed;
    Q_EMIT objectClosing(w);
    _ids.remove(w);
    _objects.remove(id);
    announce(id, QStringLiteral("close"), QVariantMap());
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
        QVariantMap state;
        for (const QString& n : names)
            state.insert(kPrefix + n, refsOf(*this, w->property(n)));
        // the guest's own write is not echoed to it; every subscriber
        // hears it (the guest writes under origin 0, like the desktop)
        if (src != static_cast<int>(Source::Guest) && _sink) {
            ++_stats.sent;
            _sink(id, QStringLiteral("update"), state);
        }
        announce(id, QStringLiteral("update"), state);
    });
    connect(w, &Widget::eventEmitted, this,
            [this, id](const QString& name, const QVariantList& args) {
                QVariantMap content;
                content.insert(QStringLiteral("event"), name);
                content.insert(QStringLiteral("args"), refsOf(*this, args));
                if (_sink) {
                    ++_stats.events;
                    _sink(id, QStringLiteral("custom"), content);
                }
                announce(id, QStringLiteral("custom"), content);
            });
    connect(w, &QObject::destroyed, this, [this, id](QObject* obj) {
        _ids.remove(obj);
        auto it = _objects.find(id);
        if (it != _objects.end() && (it->isNull() || it->data() == obj))
            _objects.erase(it);
    });
}

void Store::applyState(Widget* w, const QVariantMap& state, bool initial, Source source)
{
    QVariantMap props;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        const QString& key = it.key();
        if (key.startsWith(kPrefix)) {
            QString name = key.mid(kPrefix.size());
            QVariant value = resolveRefs(*this, it.value());
            if (initial)
                w->setInitial(name, value);
            else
                props.insert(name, value);
        }
        else if (key == QLatin1String("layoutSpec")) {
            // a code-built layout tree (G3c): the object's Layout is
            // rebuilt from it, silently; a backend realizes it when it
            // builds the widget, and the ops that follow apply to the
            // real layouts by name
            Layout* old = w->layout();
            if (it->typeId() == QMetaType::QVariantMap && !it->toMap().isEmpty())
                w->setLayout(layoutFromSpec(*this, it->toMap()));
            else
                w->setLayout(nullptr);
            delete old;
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
        w->setProperties(props, source);
    // after the values: the guest's own list is the truth
    auto touched = state.constFind(QStringLiteral("_touched"));
    if (touched != state.constEnd() && source == Source::Guest)
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
    // the adopted objects are the desktop's, not the guest's: they stay
    QList<QPair<QString, QPointer<Widget>>> gone;
    for (auto it = _objects.begin(); it != _objects.end();) {
        if (_adopted.contains(it.key())) {
            ++it;
            continue;
        }
        gone.append({it.key(), it.value()});
        if (!it->isNull())
            _ids.remove(it->data());
        it = _objects.erase(it);
    }
    for (const auto& p : gone) {
        if (!p.second.isNull()) {
            Q_EMIT objectClosing(p.second.data());
            for (Widget* child : p.second->childWidgets())
                child->QObject::setParent(nullptr);
            delete p.second.data();
        }
        announce(p.first, QStringLiteral("close"), QVariantMap());
    }
}

#include "moc_FwStore.cpp"
