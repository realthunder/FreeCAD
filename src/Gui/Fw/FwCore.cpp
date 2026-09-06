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

void Layout::addSpacer(int w, int h, const QVariantList& position)
{
    LayoutItem item;
    item.spacer = true;
    item.position = position;
    _items.append(item);
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {w, h});
    notify(QStringLiteral("addSpacing"), f);
}

void Layout::setContentsMargins(int left, int top, int right, int bottom)
{
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {left, top, right, bottom});
    notify(QStringLiteral("setContentsMargins"), f);
}

void Layout::setSpacing(int spacing)
{
    QVariantMap f;
    f.insert(QStringLiteral("args"), QVariantList {spacing});
    notify(QStringLiteral("setSpacing"), f);
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
