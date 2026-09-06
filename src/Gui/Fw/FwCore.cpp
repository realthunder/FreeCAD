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

#include "Fw/FwCore.h"

using namespace Gui::Fw;

// ---- Widget ---------------------------------------------------------------

Widget::Widget(Widget* parent)
    : QObject(parent)
    , _qtClass(QStringLiteral("QWidget"))
{
    declare(QStringLiteral("objectName"), QString());
    declare(QStringLiteral("visible"), true);
    declare(QStringLiteral("enabled"), true);
    declare(QStringLiteral("toolTip"), QString());
    declare(QStringLiteral("statusTip"), QString());
    declare(QStringLiteral("whatsThis"), QString());
    declare(QStringLiteral("windowTitle"), QString());
    declare(QStringLiteral("windowIcon"), QString());
    declare(QStringLiteral("styleSheet"), QString());
    declare(QStringLiteral("minimumWidth"), 0);
    declare(QStringLiteral("minimumHeight"), 0);
    declare(QStringLiteral("maximumWidth"), 16777215);
    declare(QStringLiteral("maximumHeight"), 16777215);
    declare(QStringLiteral("prefEntry"), QString());
    declare(QStringLiteral("prefPath"), QString());
    // G3c (docs/Sandbox.md 7.11): a font as data (bold, italic,
    // pointSize, family), the actions a widget carries (`addAction`),
    // the QEvent types the backend relays (`watchEvents`), and the
    // focus the backend reports
    declare(QStringLiteral("font"), QVariantMap());
    declare(QStringLiteral("actions"), QVariantList());
    declare(QStringLiteral("watchEvents"), QVariantList());
    declare(QStringLiteral("focus"), false);
}

Widget::~Widget() = default;

void Widget::setQtClass(const QString& name)
{
    if (!name.isEmpty())
        _qtClass = name;
}

void Widget::setObjectName(const QString& name)
{
    setProperty(QStringLiteral("objectName"), name);
}

void Widget::declare(const QString& name, const QVariant& defaultValue)
{
    _props.insert(name, defaultValue);
}

QVariant Widget::coerce(const QString& name, const QVariant& value) const
{
    auto it = _props.constFind(name);
    if (it == _props.constEnd() || !value.isValid())
        return value;
    switch (it->typeId()) {
        case QMetaType::Int:
        case QMetaType::LongLong:
            if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::LongLong
                || value.typeId() == QMetaType::UInt || value.typeId() == QMetaType::ULongLong
                || value.typeId() == QMetaType::Bool)
                return QVariant(static_cast<int>(value.toLongLong()));
            return value;
        case QMetaType::Double:
            if (value.canConvert<double>() && value.typeId() != QMetaType::QString)
                return QVariant(value.toDouble());
            return value;
        case QMetaType::Bool:
            return QVariant(value.toBool());
        case QMetaType::QString:
            if (value.typeId() != QMetaType::QString && value.canConvert<QString>())
                return QVariant(value.toString());
            return value;
        case QMetaType::QStringList:
            if (value.typeId() != QMetaType::QStringList)
                return QVariant(value.toStringList());
            return value;
        default:
            return value;
    }
}

QVariant Widget::property(const char* name) const
{
    return property(QString::fromUtf8(name));
}

QVariant Widget::property(const QString& name) const
{
    auto it = _props.constFind(name);
    if (it != _props.constEnd())
        return *it;
    return _dynamic.value(name);
}

bool Widget::setProperty(const char* name, const QVariant& value)
{
    return setProperty(QString::fromUtf8(name), value);
}

bool Widget::setProperty(const QString& name, const QVariant& value)
{
    if (!has(name)) {
        _dynamic.insert(name, value);
        return false;
    }
    QVariantMap one;
    one.insert(name, value);
    setProperties(one, Source::Native);
    return true;
}

void Widget::setProperties(const QVariantMap& values, Source source)
{
    QStringList written;
    QVariantMap changed;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!has(it.key()))
            continue;
        QVariant v = coerce(it.key(), it.value());
        QVariant& slot = _props[it.key()];
        if (slot != v)
            changed.insert(it.key(), v);
        slot = v;
        written.append(it.key());
        // only native code's setters mark touched: a backend reports what
        // the user did, and the guest syncs its own touched list
        if (source == Source::Native)
            _touched.insert(it.key());
        if (it.key() == QLatin1String("objectName"))
            QObject::setObjectName(v.toString());
    }
    if (written.isEmpty())
        return;
    // the backend first, so the widget shows the value before the typed
    // signals fire and a slot reads it back
    if (_backend)
        _backend->propertiesWritten(written, static_cast<int>(source));
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
        propertyDidChange(it.key(), it.value());
    Q_EMIT propertiesChanged(written, static_cast<int>(source));
}

void Widget::setInitial(const QString& name, const QVariant& value)
{
    if (!has(name))
        return;
    _props[name] = coerce(name, value);
    if (name == QLatin1String("objectName"))
        QObject::setObjectName(value.toString());
}

void Widget::setTouched(const QStringList& names)
{
    _touched = QSet<QString>(names.begin(), names.end());
}

void Widget::propertyDidChange(const QString&, const QVariant&)
{}

void Widget::dispatchEvent(const QString&, const QVariantList&)
{}

Widget* Widget::parentWidget() const
{
    return qobject_cast<Widget*>(parent());
}

void Widget::setParent(Widget* parent)
{
    if (parent == parentWidget() && parent == QObject::parent())
        return;
    QObject::setParent(parent);
    request(QStringLiteral("setParent"),
            QVariantList {QVariant::fromValue<QObject*>(parent)});
}

QList<Widget*> Widget::childWidgets() const
{
    QList<Widget*> out;
    for (QObject* c : children())
        if (auto w = qobject_cast<Widget*>(c))
            out.append(w);
    return out;
}

Widget* Widget::findChildWidget(const QString& name) const
{
    return findChild<Widget*>(name);
}

void Widget::setLayout(Layout* layout)
{
    _layout = layout;
    if (layout) {
        layout->_parentLayout = nullptr;
        layout->QObject::setParent(this);
    }
}

Layout* Widget::findLayout(const QString& name) const
{
    return findChild<Layout*>(name);
}

void Widget::addAction(Widget* action, int position)
{
    if (!action)
        return;
    QVariantList list = property("actions").toList();
    QVariantMap entry;
    entry.insert(QStringLiteral("action"), QVariant::fromValue<QObject*>(action));
    entry.insert(QStringLiteral("position"), position);
    list.append(entry);
    setProperty("actions", list);
}

void Widget::removeAction(Widget* action)
{
    QVariantList list = property("actions").toList();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toMap().value(QStringLiteral("action")).value<QObject*>() == action) {
            list.removeAt(i);
            setProperty("actions", list);
            return;
        }
    }
}

QList<Widget*> Widget::actions() const
{
    QList<Widget*> out;
    for (const QVariant& v : property("actions").toList()) {
        QObject* o = v.toMap().value(QStringLiteral("action")).value<QObject*>();
        if (auto a = qobject_cast<Widget*>(o))
            out.append(a);
    }
    return out;
}

void Widget::setFixedWidth(int w)
{
    QVariantMap m;
    m.insert(QStringLiteral("minimumWidth"), w);
    m.insert(QStringLiteral("maximumWidth"), w);
    setProperties(m);
}

void Widget::setFixedHeight(int h)
{
    QVariantMap m;
    m.insert(QStringLiteral("minimumHeight"), h);
    m.insert(QStringLiteral("maximumHeight"), h);
    setProperties(m);
}

void Widget::setMinimumSize(int w, int h)
{
    QVariantMap m;
    m.insert(QStringLiteral("minimumWidth"), w);
    m.insert(QStringLiteral("minimumHeight"), h);
    setProperties(m);
}

void Widget::setMaximumSize(int w, int h)
{
    QVariantMap m;
    m.insert(QStringLiteral("maximumWidth"), w);
    m.insert(QStringLiteral("maximumHeight"), h);
    setProperties(m);
}

void Widget::request(const QString& name, const QVariantList& args)
{
    if (_backend)
        _backend->requested(name, args);
    Q_EMIT requested(name, args);
}

void Widget::notify(const QString& name, const QVariantList& args)
{
    dispatchEvent(name, args);
    Q_EMIT eventEmitted(name, args);
}

// ---- Layout ---------------------------------------------------------------

Layout::Layout(Kind kind, Widget* owner)
    : QObject(owner)
    , _kind(kind)
{
    if (owner)
        owner->setLayout(this);
}

Layout::~Layout() = default;

QString Layout::className() const
{
    switch (_kind) {
        case VBox:
            return QStringLiteral("QVBoxLayout");
        case HBox:
            return QStringLiteral("QHBoxLayout");
        case Grid:
            return QStringLiteral("QGridLayout");
        case Form:
            return QStringLiteral("QFormLayout");
        case Bar:
            return QStringLiteral("_bar");
        default:
            return QStringLiteral("QBoxLayout");
    }
}

Widget* Layout::parentWidget() const
{
    const Layout* l = this;
    while (l->_parentLayout)
        l = l->_parentLayout;
    return qobject_cast<Widget*>(l->parent());
}

const LayoutItem* Layout::itemAt(int index) const
{
    if (index < 0 || index >= _items.size())
        return nullptr;
    return &_items.at(index);
}

void Layout::notify(const QString& op, QVariantMap fields)
{
    Widget* owner = parentWidget();
    if (!owner || objectName().isEmpty())
        return;
    fields.insert(QStringLiteral("layout"), objectName());
    fields.insert(QStringLiteral("op"), op);
    owner->emitLayoutOp(fields);
}

void Layout::attach(Widget* widget)
{
    Widget* owner = parentWidget();
    if (owner && widget && widget->parentWidget() != owner)
        widget->setParent(owner);
}

LayoutItem Layout::takeAt(int index)
{
    if (index < 0 || index >= _items.size())
        return LayoutItem();
    LayoutItem item = _items.takeAt(index);
    if (item.layout)
        item.layout->_parentLayout = nullptr;
    QVariantMap f;
    f.insert(QStringLiteral("index"), index);
    notify(QStringLiteral("takeAt"), f);
    return item;
}

int Layout::indexOf(const Widget* widget) const
{
    for (int i = 0; i < _items.size(); ++i)
        if (_items.at(i).widget == widget)
            return i;
    return -1;
}

int Layout::indexOf(const Layout* layout) const
{
    for (int i = 0; i < _items.size(); ++i)
        if (_items.at(i).layout == layout)
            return i;
    return -1;
}

void Layout::removeWidget(Widget* widget)
{
    int i = indexOf(widget);
    if (i < 0)
        return;
    _items.removeAt(i);
    QVariantMap f;
    f.insert(QStringLiteral("widget"), QVariant::fromValue<QObject*>(widget));
    notify(QStringLiteral("removeWidget"), f);
}

void Layout::addWidget(Widget* widget, const QVariantList& position)
{
    if (!widget)
        return;
    attach(widget);
    LayoutItem item;
    item.widget = widget;
    item.position = position;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("widget"), QVariant::fromValue<QObject*>(widget));
    f.insert(QStringLiteral("args"), position);
    notify(QStringLiteral("addWidget"), f);
}

void Layout::insertWidget(int index, Widget* widget, const QVariantList& position)
{
    if (!widget)
        return;
    attach(widget);
    LayoutItem item;
    item.widget = widget;
    item.position = position;
    if (index < 0 || index > _items.size())
        index = _items.size();
    _items.insert(index, item);
    QVariantMap f;
    f.insert(QStringLiteral("widget"), QVariant::fromValue<QObject*>(widget));
    f.insert(QStringLiteral("index"), index);
    f.insert(QStringLiteral("args"), position);
    notify(QStringLiteral("insertWidget"), f);
}

void Layout::addLayout(Layout* layout, const QVariantList& position)
{
    if (!layout)
        return;
    layout->_parentLayout = this;
    layout->QObject::setParent(this);
    LayoutItem item;
    item.layout = layout;
    item.position = position;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("sublayout"), layout->objectName());
    f.insert(QStringLiteral("args"), position);
    notify(QStringLiteral("addLayout"), f);
}

void Layout::addStretch(int stretch)
{
    LayoutItem item;
    item.spacer = true;
    item.stretch = stretch;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {stretch});
    notify(QStringLiteral("addStretch"), f);
}

void Layout::addSpacing(int size)
{
    LayoutItem item;
    item.spacer = true;
    item.spacing = size;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {size});
    notify(QStringLiteral("addSpacing"), f);
}

void Layout::addSpacer(int w, int h, const QVariantList& position, int hPolicy, int vPolicy)
{
    LayoutItem item;
    item.spacer = true;
    item.width = w;
    item.height = h;
    item.hPolicy = hPolicy;
    item.vPolicy = vPolicy;
    item.position = position;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {w, h});
    notify(QStringLiteral("addSpacing"), f);
}

void Layout::addAction(Widget* action)
{
    if (!action)
        return;
    LayoutItem item;
    item.action = action;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("action"), QVariant::fromValue<QObject*>(action));
    notify(QStringLiteral("addAction"), f);
}

void Layout::insertAction(int index, Widget* action)
{
    if (!action)
        return;
    LayoutItem item;
    item.action = action;
    if (index < 0 || index > _items.size())
        index = _items.size();
    _items.insert(index, item);
    QVariantMap f;
    f.insert(QStringLiteral("action"), QVariant::fromValue<QObject*>(action));
    f.insert(QStringLiteral("index"), index);
    notify(QStringLiteral("insertAction"), f);
}

void Layout::removeAction(Widget* action)
{
    for (int i = 0; i < _items.size(); ++i) {
        if (_items.at(i).action == action) {
            _items.removeAt(i);
            QVariantMap f;
            f.insert(QStringLiteral("action"), QVariant::fromValue<QObject*>(action));
            notify(QStringLiteral("removeAction"), f);
            return;
        }
    }
}

void Layout::addSeparator()
{
    LayoutItem item;
    item.separator = true;
    _items.append(item);
    notify(QStringLiteral("addSeparator"));
}

void Layout::clear()
{
    _items.clear();
    notify(QStringLiteral("clear"));
}

void Layout::setContentsMargins(int left, int top, int right, int bottom)
{
    _margins = QVariantList {left, top, right, bottom};
    QVariantMap f;
    f.insert(QStringLiteral("args"), _margins);
    notify(QStringLiteral("setContentsMargins"), f);
}

void Layout::setSpacing(int spacing)
{
    _spacing = spacing;
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {spacing});
    notify(QStringLiteral("setSpacing"), f);
}

QVariantMap Layout::spec() const
{
    QVariantMap out;
    out.insert(QStringLiteral("class"), className());
    out.insert(QStringLiteral("name"), objectName());
    QVariantList items;
    for (const auto& item : _items) {
        QVariantMap m;
        if (item.widget)
            m.insert(QStringLiteral("widget"), QVariant::fromValue<QObject*>(item.widget));
        else if (item.layout)
            m.insert(QStringLiteral("layout"), item.layout->spec());
        else if (item.action)
            m.insert(QStringLiteral("action"), QVariant::fromValue<QObject*>(item.action));
        else if (item.separator)
            m.insert(QStringLiteral("separator"), true);
        else if (item.stretch)
            m.insert(QStringLiteral("stretch"), item.stretch);
        else if (item.spacing)
            m.insert(QStringLiteral("spacing"), item.spacing);
        else
            m.insert(QStringLiteral("spacer"),
                     QVariantList {item.width, item.height, item.hPolicy, item.vPolicy});
        if (!item.position.isEmpty())
            m.insert(QStringLiteral("pos"), item.position);
        items.append(m);
    }
    out.insert(QStringLiteral("items"), items);
    if (!_margins.isEmpty())
        out.insert(QStringLiteral("margins"), _margins);
    if (_spacing >= 0)
        out.insert(QStringLiteral("spacing"), _spacing);
    return out;
}

void Layout::addRow(Widget* label, Widget* field)
{
    int row = rowCount();
    addWidget(label, QVariantList {row, 0});
    addWidget(field, QVariantList {row, 1});
}

void Layout::addRow(Widget* field)
{
    addWidget(field);
}

int Layout::rowCount() const
{
    int rows = 0;
    for (const auto& item : _items) {
        if (item.position.size() < 2)
            continue;
        int span = (_kind == Grid && item.position.size() > 2) ? item.position.at(2).toInt() : 1;
        rows = std::max(rows, item.position.at(0).toInt() + span);
    }
    return rows;
}

int Layout::columnCount() const
{
    int cols = 0;
    for (const auto& item : _items) {
        if (item.position.size() < 2)
            continue;
        int span = (_kind == Grid && item.position.size() > 3) ? item.position.at(3).toInt() : 1;
        cols = std::max(cols, item.position.at(1).toInt() + span);
    }
    return cols;
}

QList<Layout*> Layout::nested() const
{
    QList<Layout*> out;
    out.append(const_cast<Layout*>(this));
    for (const auto& item : _items)
        if (item.layout)
            out.append(item.layout->nested());
    return out;
}

#include "moc_FwCore.cpp"
