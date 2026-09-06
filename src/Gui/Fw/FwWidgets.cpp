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

#ifndef _PreComp_
#include <algorithm>
#include <functional>
#include <map>
#endif

#include <Base/Exception.h>
#include <Base/UnitsApi.h>

#include "Fw/FwWidgets.h"

using namespace Gui::Fw;

namespace
{
const QString kText = QStringLiteral("text");
const QString kChecked = QStringLiteral("checked");
const QString kValue = QStringLiteral("value");
const QString kMinimum = QStringLiteral("minimum");
const QString kMaximum = QStringLiteral("maximum");
const QString kSingleStep = QStringLiteral("singleStep");
}  // namespace

// ---- QLabel ---------------------------------------------------------------

QLabel::QLabel(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QLabel"));
    declare(kText, QString());
    declare(QStringLiteral("wordWrap"), false);
    declare(QStringLiteral("alignment"), 0);
    declare(QStringLiteral("openExternalLinks"), false);
    declare(QStringLiteral("pixmap"), QString());
}

QLabel::QLabel(const QString& text, Widget* parent)
    : QLabel(parent)
{
    setText(text);
}

void QLabel::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("linkActivated"))
        Q_EMIT linkActivated(args.value(0).toString());
}

// ---- buttons ----------------------------------------------------------------

QAbstractButton::QAbstractButton(Widget* parent)
    : Widget(parent)
{
    declare(kText, QString());
    declare(QStringLiteral("checkable"), false);
    declare(kChecked, false);
    declare(QStringLiteral("icon"), QString());
    declare(QStringLiteral("autoExclusive"), false);
}

void QAbstractButton::setChecked(bool on)
{
    if (on && autoExclusive()) {
        if (Widget* p = parentWidget()) {
            for (Widget* sib : p->childWidgets()) {
                auto b = qobject_cast<QAbstractButton*>(sib);
                if (b && b != this && b->autoExclusive() && b->isChecked())
                    b->setProperty("checked", false);
            }
        }
    }
    setProperty("checked", on);
}

void QAbstractButton::click()
{
    if (isCheckable())
        setChecked(!isChecked());
    notify(QStringLiteral("clicked"), QVariantList {isChecked()});
}

void QAbstractButton::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kChecked)
        Q_EMIT toggled(value.toBool());
}

void QAbstractButton::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("clicked"))
        Q_EMIT clicked(args.isEmpty() ? isChecked() : args.at(0).toBool());
    else if (name == QLatin1String("pressed"))
        Q_EMIT pressed();
    else if (name == QLatin1String("released"))
        Q_EMIT released();
}

QPushButton::QPushButton(Widget* parent)
    : QAbstractButton(parent)
{
    setQtClass(QStringLiteral("QPushButton"));
    declare(QStringLiteral("flat"), false);
    declare(QStringLiteral("default"), false);
}

QPushButton::QPushButton(const QString& text, Widget* parent)
    : QPushButton(parent)
{
    setText(text);
}

QToolButton::QToolButton(Widget* parent)
    : QAbstractButton(parent)
{
    setQtClass(QStringLiteral("QToolButton"));
    declare(QStringLiteral("autoRaise"), false);
}

QCheckBox::QCheckBox(Widget* parent)
    : QAbstractButton(parent)
{
    setQtClass(QStringLiteral("QCheckBox"));
    declare(QStringLiteral("tristate"), false);
    setInitial(QStringLiteral("checkable"), true);
}

QCheckBox::QCheckBox(const QString& text, Widget* parent)
    : QCheckBox(parent)
{
    setText(text);
}

void QCheckBox::propertyDidChange(const QString& name, const QVariant& value)
{
    QAbstractButton::propertyDidChange(name, value);
    if (name == kChecked) {
        int state = value.toBool() ? 2 : 0;
        Q_EMIT stateChanged(state);
        Q_EMIT checkStateChanged(state);
    }
}

QRadioButton::QRadioButton(Widget* parent)
    : QAbstractButton(parent)
{
    setQtClass(QStringLiteral("QRadioButton"));
    setInitial(QStringLiteral("checkable"), true);
    setInitial(QStringLiteral("autoExclusive"), true);
}

QRadioButton::QRadioButton(const QString& text, Widget* parent)
    : QRadioButton(parent)
{
    setText(text);
}

// ---- containers -----------------------------------------------------------

QGroupBox::QGroupBox(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QGroupBox"));
    declare(QStringLiteral("title"), QString());
    declare(QStringLiteral("checkable"), false);
    declare(kChecked, true);
    declare(QStringLiteral("flat"), false);
}

QGroupBox::QGroupBox(const QString& title, Widget* parent)
    : QGroupBox(parent)
{
    setTitle(title);
}

void QGroupBox::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kChecked)
        Q_EMIT toggled(value.toBool());
}

void QGroupBox::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("clicked"))
        Q_EMIT clicked(args.isEmpty() ? isChecked() : args.at(0).toBool());
}

QFrame::QFrame(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QFrame"));
}

// ---- text inputs ------------------------------------------------------------

QLineEdit::QLineEdit(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QLineEdit"));
    declare(kText, QString());
    declare(QStringLiteral("placeholderText"), QString());
    declare(QStringLiteral("readOnly"), false);
    declare(QStringLiteral("maxLength"), 32767);
    declare(QStringLiteral("echoMode"), 0);
    declare(QStringLiteral("clearButtonEnabled"), false);
}

QLineEdit::QLineEdit(const QString& text, Widget* parent)
    : QLineEdit(parent)
{
    setText(text);
}

void QLineEdit::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kText)
        Q_EMIT textChanged(value.toString());
}

void QLineEdit::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("textEdited"))
        Q_EMIT textEdited(args.value(0).toString());
    else if (name == QLatin1String("returnPressed"))
        Q_EMIT returnPressed();
    else if (name == QLatin1String("editingFinished"))
        Q_EMIT editingFinished();
    else if (name == QLatin1String("selectionChanged"))
        Q_EMIT selectionChanged();
}

QTextEdit::QTextEdit(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QTextEdit"));
    declare(QStringLiteral("plainText"), QString());
    declare(QStringLiteral("html"), QString());
    declare(QStringLiteral("readOnly"), false);
    declare(QStringLiteral("placeholderText"), QString());
}

QTextEdit::QTextEdit(const QString& text, Widget* parent)
    : QTextEdit(parent)
{
    setPlainText(text);
}

void QTextEdit::setPlainText(const QString& text)
{
    QVariantMap m;
    m.insert(QStringLiteral("plainText"), text);
    m.insert(QStringLiteral("html"), QString());
    setProperties(m);
}

void QTextEdit::append(const QString& text)
{
    QString cur = toPlainText();
    setPlainText(cur.isEmpty() ? text : cur + QLatin1Char('\n') + text);
}

void QTextEdit::clear()
{
    setPlainText(QString());
}

void QTextEdit::propertyDidChange(const QString& name, const QVariant&)
{
    if (name == QLatin1String("plainText") || name == QLatin1String("html"))
        Q_EMIT textChanged();
}

QPlainTextEdit::QPlainTextEdit(Widget* parent)
    : QTextEdit(parent)
{
    setQtClass(QStringLiteral("QPlainTextEdit"));
}

QTextBrowser::QTextBrowser(Widget* parent)
    : QTextEdit(parent)
{
    setQtClass(QStringLiteral("QTextBrowser"));
    declare(QStringLiteral("openExternalLinks"), false);
}

void QTextBrowser::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("anchorClicked"))
        Q_EMIT anchorClicked(args.value(0).toString());
}

// ---- numbers ------------------------------------------------------------------

QAbstractSpinBox::QAbstractSpinBox(Widget* parent)
    : Widget(parent)
{
    declare(kMinimum, 0.0);
    declare(kMaximum, 99.0);
    declare(kSingleStep, 1.0);
    declare(QStringLiteral("prefix"), QString());
    declare(QStringLiteral("suffix"), QString());
    declare(QStringLiteral("readOnly"), false);
}

void QAbstractSpinBox::dispatchEvent(const QString& name, const QVariantList&)
{
    if (name == QLatin1String("editingFinished"))
        Q_EMIT editingFinished();
}

void QAbstractSpinBox::clampValue()
{
    QVariant v = property(kValue);
    if (!v.isValid())
        return;
    double lo = property(kMinimum).toDouble(), hi = property(kMaximum).toDouble();
    double x = std::min(std::max(v.toDouble(), lo), hi);
    if (x != v.toDouble())
        setProperty(kValue, x);
}

QSpinBox::QSpinBox(Widget* parent)
    : QAbstractSpinBox(parent)
{
    setQtClass(QStringLiteral("QSpinBox"));
    declare(kValue, 0);
    declare(kMinimum, 0);
    declare(kMaximum, 99);
    declare(kSingleStep, 1);
}

void QSpinBox::setValue(int v)
{
    setProperty(kValue, std::min(std::max(v, minimum()), maximum()));
}

void QSpinBox::setMinimum(int v)
{
    setProperty(kMinimum, v);
    clampValue();
}

void QSpinBox::setMaximum(int v)
{
    setProperty(kMaximum, v);
    clampValue();
}

void QSpinBox::setRange(int lo, int hi)
{
    QVariantMap m;
    m.insert(kMinimum, lo);
    m.insert(kMaximum, hi);
    setProperties(m);
    clampValue();
}

void QSpinBox::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kValue)
        Q_EMIT valueChanged(value.toInt());
}

QDoubleSpinBox::QDoubleSpinBox(Widget* parent)
    : QAbstractSpinBox(parent)
{
    setQtClass(QStringLiteral("QDoubleSpinBox"));
    declare(kValue, 0.0);
    declare(QStringLiteral("decimals"), 2);
}

void QDoubleSpinBox::setValue(double v)
{
    setProperty(kValue, std::min(std::max(v, minimum()), maximum()));
}

void QDoubleSpinBox::setMinimum(double v)
{
    setProperty(kMinimum, v);
    clampValue();
}

void QDoubleSpinBox::setMaximum(double v)
{
    setProperty(kMaximum, v);
    clampValue();
}

void QDoubleSpinBox::setRange(double lo, double hi)
{
    QVariantMap m;
    m.insert(kMinimum, lo);
    m.insert(kMaximum, hi);
    setProperties(m);
    clampValue();
}

void QDoubleSpinBox::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kValue)
        Q_EMIT valueChanged(value.toDouble());
}

QSlider::QSlider(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QSlider"));
    declare(kValue, 0);
    declare(kMinimum, 0);
    declare(kMaximum, 99);
    declare(kSingleStep, 1);
    declare(QStringLiteral("orientation"), 1);
}

QSlider::QSlider(int orientation, Widget* parent)
    : QSlider(parent)
{
    setOrientation(orientation);
}

void QSlider::setValue(int v)
{
    setProperty(kValue, std::min(std::max(v, minimum()), maximum()));
}

void QSlider::setRange(int lo, int hi)
{
    QVariantMap m;
    m.insert(kMinimum, lo);
    m.insert(kMaximum, hi);
    setProperties(m);
}

void QSlider::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kValue)
        Q_EMIT valueChanged(value.toInt());
}

void QSlider::dispatchEvent(const QString& name, const QVariantList&)
{
    if (name == QLatin1String("sliderReleased"))
        Q_EMIT sliderReleased();
}

QProgressBar::QProgressBar(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QProgressBar"));
    declare(kValue, 0);
    declare(kMinimum, 0);
    declare(kMaximum, 100);
    declare(QStringLiteral("format"), QStringLiteral("%p%"));
}

void QProgressBar::setRange(int lo, int hi)
{
    QVariantMap m;
    m.insert(kMinimum, lo);
    m.insert(kMaximum, hi);
    setProperties(m);
}

// ---- choices ------------------------------------------------------------------

QComboBox::QComboBox(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QComboBox"));
    declare(QStringLiteral("items"), QStringList());
    declare(QStringLiteral("itemIcons"), QStringList());
    declare(QStringLiteral("currentIndex"), -1);
    declare(QStringLiteral("editable"), false);
    declare(QStringLiteral("editText"), QString());
}

void QComboBox::setItems(const QStringList& items, const QStringList& icons, int current)
{
    if (current >= items.size())
        current = items.size() - 1;
    if (current < 0 && !items.isEmpty())
        current = 0;
    QStringList ic = icons;
    while (ic.size() < items.size())
        ic.append(QString());
    while (_itemData.size() < items.size())
        _itemData.append(QVariant());
    while (_itemData.size() > items.size())
        _itemData.removeLast();
    QVariantMap m;
    m.insert(QStringLiteral("items"), items);
    m.insert(QStringLiteral("itemIcons"), ic);
    m.insert(QStringLiteral("currentIndex"), current);
    setProperties(m);
}

void QComboBox::addItem(const QString& text, const QVariant& userData)
{
    addItem(QString(), text, userData);
}

void QComboBox::addItem(const QString& iconPath, const QString& text, const QVariant& userData)
{
    QStringList items = this->items(), icons = property("itemIcons").toStringList();
    items.append(text);
    icons.append(iconPath);
    _itemData.append(userData);
    setItems(items, icons, currentIndex());
}

void QComboBox::addItems(const QStringList& texts)
{
    QStringList items = this->items(), icons = property("itemIcons").toStringList();
    for (const QString& t : texts) {
        items.append(t);
        icons.append(QString());
        _itemData.append(QVariant());
    }
    setItems(items, icons, currentIndex());
}

void QComboBox::insertItem(int index, const QString& text, const QVariant& userData)
{
    QStringList items = this->items(), icons = property("itemIcons").toStringList();
    index = std::min(std::max(index, 0), static_cast<int>(items.size()));
    items.insert(index, text);
    icons.insert(index, QString());
    _itemData.insert(index, userData);
    int current = currentIndex();
    if (current >= index)
        ++current;
    setItems(items, icons, current);
}

void QComboBox::removeItem(int index)
{
    QStringList items = this->items(), icons = property("itemIcons").toStringList();
    if (index < 0 || index >= items.size())
        return;
    items.removeAt(index);
    if (index < icons.size())
        icons.removeAt(index);
    if (index < _itemData.size())
        _itemData.removeAt(index);
    int current = currentIndex();
    if (current > index || current >= items.size())
        --current;
    setItems(items, icons, current);
}

void QComboBox::clear()
{
    _itemData.clear();
    QVariantMap m;
    m.insert(QStringLiteral("items"), QStringList());
    m.insert(QStringLiteral("itemIcons"), QStringList());
    m.insert(QStringLiteral("currentIndex"), -1);
    setProperties(m);
}

QString QComboBox::itemText(int index) const
{
    return items().value(index);
}

void QComboBox::setItemText(int index, const QString& text)
{
    QStringList items = this->items();
    if (index < 0 || index >= items.size())
        return;
    items[index] = text;
    setProperty("items", items);
}

QVariant QComboBox::itemData(int index) const
{
    return _itemData.value(index);
}

void QComboBox::setItemData(int index, const QVariant& data)
{
    while (_itemData.size() <= index)
        _itemData.append(QVariant());
    if (index >= 0)
        _itemData[index] = data;
}

void QComboBox::setItemIcon(int index, const QString& path)
{
    QStringList icons = property("itemIcons").toStringList();
    while (icons.size() <= index)
        icons.append(QString());
    if (index >= 0)
        icons[index] = path;
    setProperty("itemIcons", icons);
}

QString QComboBox::currentText() const
{
    if (isEditable()) {
        QString edit = property("editText").toString();
        if (!edit.isEmpty())
            return edit;
    }
    return itemText(currentIndex());
}

void QComboBox::setCurrentIndex(int index)
{
    if (index >= count())
        index = -1;
    setProperty("currentIndex", index);
}

void QComboBox::setCurrentText(const QString& text)
{
    int i = findText(text);
    if (i >= 0)
        setCurrentIndex(i);
    else if (isEditable())
        setEditText(text);
}

int QComboBox::findData(const QVariant& data) const
{
    for (int i = 0; i < _itemData.size(); ++i)
        if (_itemData.at(i) == data)
            return i;
    return -1;
}

void QComboBox::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == QLatin1String("currentIndex")) {
        Q_EMIT currentIndexChanged(value.toInt());
        Q_EMIT currentTextChanged(currentText());
    }
    else if (name == QLatin1String("editText")) {
        Q_EMIT editTextChanged(value.toString());
    }
}

void QComboBox::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("activated"))
        Q_EMIT activated(args.value(0).toInt());
}

QFontComboBox::QFontComboBox(Widget* parent)
    : QComboBox(parent)
{
    setQtClass(QStringLiteral("QFontComboBox"));
}

// ---- FreeCAD's own --------------------------------------------------------------

InputField::InputField(Widget* parent)
    : QLineEdit(parent)
{
    setQtClass(QStringLiteral("Gui::InputField"));
    declare(QStringLiteral("rawValue"), 0.0);
    declare(QStringLiteral("unit"), QString());
    declare(kMinimum, -1e12);
    declare(kMaximum, 1e12);
    declare(kSingleStep, 1.0);
    declare(QStringLiteral("decimals"), 2);
    declare(QStringLiteral("precision"), 2);
    declare(QStringLiteral("historySize"), 5);
    declare(QStringLiteral("format"), QStringLiteral("g"));
    declare(QStringLiteral("quantityString"), QString());
}

QString InputField::formatValue(double value) const
{
    try {
        Base::Quantity q = getUnitText().isEmpty()
            ? Base::Quantity(value)
            : Base::Quantity(value, getUnitText().toStdString());
        Base::QuantityFormat fmt = q.getFormat();
        fmt.format = Base::QuantityFormat::Fixed;
        fmt.precision = Base::UnitsApi::getDecimals();
        q.setFormat(fmt);
        return QString::fromStdString(q.getUserString());
    }
    catch (const Base::Exception&) {
        QString s = QString::number(value, 'g');
        if (!getUnitText().isEmpty())
            s += QLatin1Char(' ') + getUnitText();
        return s;
    }
}

void InputField::setValue(double value)
{
    QVariantMap m;
    m.insert(QStringLiteral("rawValue"), value);
    m.insert(kText, formatValue(value));
    setProperties(m);
}

void InputField::setValue(const Base::Quantity& value)
{
    setValue(value.getValue());
}

Base::Quantity InputField::getQuantity() const
{
    if (getUnitText().isEmpty())
        return Base::Quantity(rawValue());
    try {
        return Base::Quantity(rawValue(), getUnitText().toStdString());
    }
    catch (const Base::Exception&) {
        return Base::Quantity(rawValue());
    }
}

void InputField::setRange(double lo, double hi)
{
    QVariantMap m;
    m.insert(kMinimum, lo);
    m.insert(kMaximum, hi);
    setProperties(m);
}

void InputField::propertyDidChange(const QString& name, const QVariant& value)
{
    QLineEdit::propertyDidChange(name, value);
    if (name == QLatin1String("rawValue"))
        Q_EMIT valueChanged(value.toDouble());
}

void InputField::dispatchEvent(const QString& name, const QVariantList& args)
{
    QLineEdit::dispatchEvent(name, args);
    if (name == QLatin1String("parseError"))
        Q_EMIT parseError(args.value(0).toString());
}

QuantitySpinBox::QuantitySpinBox(Widget* parent)
    : InputField(parent)
{
    setQtClass(QStringLiteral("Gui::QuantitySpinBox"));
    declare(QStringLiteral("displayUnit"), QString());
}

ColorButton::ColorButton(Widget* parent)
    : QPushButton(parent)
{
    setQtClass(QStringLiteral("Gui::ColorButton"));
    declare(QStringLiteral("color"), QVariantList {0.0, 0.0, 0.0, 1.0});
    declare(QStringLiteral("allowTransparency"), false);
    declare(QStringLiteral("allowChangeColor"), true);
    declare(QStringLiteral("drawFrame"), true);
}

void ColorButton::setColor(const QColor& color)
{
    setProperty("color",
                QVariantList {color.redF(), color.greenF(), color.blueF(), color.alphaF()});
}

QColor ColorButton::color() const
{
    QVariantList c = property("color").toList();
    if (c.size() < 3)
        return QColor();
    return QColor::fromRgbF(static_cast<float>(c.at(0).toDouble()),
                            static_cast<float>(c.at(1).toDouble()),
                            static_cast<float>(c.at(2).toDouble()),
                            static_cast<float>(c.size() > 3 ? c.at(3).toDouble() : 1.0));
}

void ColorButton::propertyDidChange(const QString& name, const QVariant& value)
{
    QPushButton::propertyDidChange(name, value);
    if (name == QLatin1String("color"))
        Q_EMIT changed();
}

// ---- the form -------------------------------------------------------------------

UiForm::UiForm(Widget* parent)
    : Widget(parent)
{}

void UiForm::addNamed(const QString& name, Widget* widget)
{
    if (!widget || name.isEmpty())
        return;
    _named.insert(name, widget);
    connect(widget, &QObject::destroyed, this, [this, name]() { _named.remove(name); });
}

// ---- the factory --------------------------------------------------------------

namespace
{
using Maker = std::function<Widget*(Widget*)>;

template<class T>
Maker maker()
{
    return [](Widget* parent) -> Widget* { return new T(parent); };
}

/// Qt class name (as a .ui spells it) -> model class.  A `Gui::Pref*`
/// is its base class here; the backend builds the real one.
const std::map<QString, Maker>& classTable()
{
    static const std::map<QString, Maker> table = {
        {QStringLiteral("QWidget"), maker<Widget>()},
        {QStringLiteral("QLabel"), maker<QLabel>()},
        {QStringLiteral("QPushButton"), maker<QPushButton>()},
        {QStringLiteral("QToolButton"), maker<QToolButton>()},
        {QStringLiteral("QCheckBox"), maker<QCheckBox>()},
        {QStringLiteral("QRadioButton"), maker<QRadioButton>()},
        {QStringLiteral("QGroupBox"), maker<QGroupBox>()},
        {QStringLiteral("QFrame"), maker<QFrame>()},
        {QStringLiteral("QLineEdit"), maker<QLineEdit>()},
        {QStringLiteral("QTextEdit"), maker<QTextEdit>()},
        {QStringLiteral("QPlainTextEdit"), maker<QPlainTextEdit>()},
        {QStringLiteral("QTextBrowser"), maker<QTextBrowser>()},
        {QStringLiteral("QSpinBox"), maker<QSpinBox>()},
        {QStringLiteral("QDoubleSpinBox"), maker<QDoubleSpinBox>()},
        {QStringLiteral("QSlider"), maker<QSlider>()},
        {QStringLiteral("QProgressBar"), maker<QProgressBar>()},
        {QStringLiteral("QComboBox"), maker<QComboBox>()},
        {QStringLiteral("QFontComboBox"), maker<QFontComboBox>()},
        {QStringLiteral("Gui::InputField"), maker<InputField>()},
        {QStringLiteral("Gui::QuantitySpinBox"), maker<QuantitySpinBox>()},
        {QStringLiteral("Gui::PrefQuantitySpinBox"), maker<QuantitySpinBox>()},
        {QStringLiteral("Gui::PrefUnitSpinBox"), maker<QuantitySpinBox>()},
        {QStringLiteral("Gui::ColorButton"), maker<ColorButton>()},
        {QStringLiteral("Gui::PrefColorButton"), maker<ColorButton>()},
        {QStringLiteral("Gui::PrefCheckBox"), maker<QCheckBox>()},
        {QStringLiteral("Gui::PrefRadioButton"), maker<QRadioButton>()},
        {QStringLiteral("Gui::PrefLineEdit"), maker<QLineEdit>()},
        {QStringLiteral("Gui::PrefTextEdit"), maker<QTextEdit>()},
        {QStringLiteral("Gui::PrefComboBox"), maker<QComboBox>()},
        {QStringLiteral("Gui::PrefSpinBox"), maker<QSpinBox>()},
        {QStringLiteral("Gui::PrefDoubleSpinBox"), maker<QDoubleSpinBox>()},
        {QStringLiteral("Gui::PrefSlider"), maker<QSlider>()},
        {QStringLiteral("Gui::PrefCheckableGroupBox"), maker<QGroupBox>()},
        {QStringLiteral("Gui::PrefFontBox"), maker<QFontComboBox>()},
        {QStringLiteral("UiForm"), maker<UiForm>()},
    };
    return table;
}

/// Guest model name ("QLabelModel") -> Qt class name.
QString classOfModel(const QString& modelName)
{
    if (!modelName.endsWith(QLatin1String("Model")))
        return QString();
    QString base = modelName.left(modelName.size() - 5);
    if (base == QLatin1String("InputField"))
        return QStringLiteral("Gui::InputField");
    if (base == QLatin1String("QuantitySpinBox"))
        return QStringLiteral("Gui::QuantitySpinBox");
    if (base == QLatin1String("ColorButton"))
        return QStringLiteral("Gui::ColorButton");
    return base;
}
}  // namespace

Widget* Gui::Fw::createWidget(const QString& className, Widget* parent)
{
    const auto& table = classTable();
    auto it = table.find(className);
    if (it == table.end()) {
        QString cls = classOfModel(className);
        if (!cls.isEmpty())
            it = table.find(cls);
    }
    Widget* w = it != table.end() ? it->second(parent) : new Widget(parent);
    if (!className.endsWith(QLatin1String("Model")))
        w->setQtClass(className);
    return w;
}

QStringList Gui::Fw::knownClasses()
{
    QStringList out;
    for (const auto& kv : classTable())
        out.append(kv.first);
    return out;
}

Layout* Gui::Fw::createLayout(const QString& className, Widget* owner)
{
    Layout::Kind kind = Layout::VBox;
    if (className == QLatin1String("QHBoxLayout"))
        kind = Layout::HBox;
    else if (className == QLatin1String("QGridLayout"))
        kind = Layout::Grid;
    else if (className == QLatin1String("QFormLayout"))
        kind = Layout::Form;
    else if (className == QLatin1String("QBoxLayout"))
        kind = Layout::Box;
    return new Layout(kind, owner);
}

#include "moc_FwWidgets.cpp"
