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

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Expression.h>
#include <App/ExpressionParser.h>
#include <App/PropertyGeo.h>
#include <Base/Tools.h>

#include "Command.h"
#include "Fw/FwWidgets.h"

using namespace Gui::Fw;

namespace
{
const QString kText = QStringLiteral("text");
const QString kChecked = QStringLiteral("checked");
const QString kValue = QStringLiteral("value");
const QString kMinimum = QStringLiteral("minimum");
const QString kBinding = QStringLiteral("binding");
const QString kExpression = QStringLiteral("expression");
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

Gui::Fw::QLineEdit::QLineEdit(Widget* parent)
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

Gui::Fw::QLineEdit::QLineEdit(const QString& text, Widget* parent)
    : QLineEdit(parent)
{
    setText(text);
}

void Gui::Fw::QLineEdit::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kText)
        Q_EMIT textChanged(value.toString());
}

void Gui::Fw::QLineEdit::dispatchEvent(const QString& name, const QVariantList& args)
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
    else if (name == QLatin1String("highlighted"))
        Q_EMIT highlighted(args.value(0).toInt());
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

// ---- the expression seam ---------------------------------------------------------

ExpressionBound::ExpressionBound(Widget* owner)
    : _owner(owner)
{
    owner->setInitial(kBinding, QString());
    owner->setInitial(kExpression, QString());
}

ExpressionBound::~ExpressionBound() = default;

void ExpressionBound::bind(const App::ObjectIdentifier& path)
{
    Gui::ExpressionBinding::bind(path);
    // `binding` is written HERE only: a change handler rewriting it
    // would make the backend re-bind the real widget inside the
    // expression-changed signal it is reacting to (a reconnect during
    // the emission -- a crash the Pad gate found)
    _owner->setProperty(kBinding, boundToName());
    syncExpression();
}

QString ExpressionBound::boundToName() const
{
    if (!isBound())
        return QString();
    return QString::fromStdString(getPath().toString());
}

QString ExpressionBound::expressionText() const
{
    return _owner->property(kExpression).toString();
}

void ExpressionBound::syncExpression()
{
    QString text;
    if (isBound()) {
        try {
            if (auto expr = getExpression())
                text = QString::fromStdString(expr->toString());
        }
        catch (const Base::Exception&) {
        }
    }
    _owner->setProperty(kExpression, text);
    if (!text.isEmpty())
        evaluateExpression();
}

void ExpressionBound::evaluateExpression()
{
    if (!isBound())
        return;
    try {
        auto expr = getExpression();
        if (!expr)
            return;
        std::unique_ptr<App::Expression> result(expr->eval());
        if (auto num = freecad_dynamic_cast<App::NumberExpression>(result.get()))
            setEvaluated(num->getQuantity().getValue());
    }
    catch (const Base::Exception& e) {
        _owner->notify(QStringLiteral("expressionError"),
                       QVariantList {QString::fromUtf8(e.what())});
    }
}

void ExpressionBound::setExpression(std::shared_ptr<App::Expression> expr)
{
    if (!isBound())
        return;
    Gui::ExpressionBinding::setExpression(expr);
    syncExpression();
}

bool ExpressionBound::setExpressionText(const QString& text, QString* error)
{
    App::DocumentObject* obj = isBound() ? getPath().getDocumentObject() : nullptr;
    if (!obj) {
        if (error)
            *error = QStringLiteral("not bound");
        return false;
    }
    try {
        std::shared_ptr<App::Expression> expr;
        if (!text.trimmed().isEmpty()) {
            expr = App::Expression::parse(obj, text.toStdString());
            std::string msg = obj->ExpressionEngine.validateExpression(getPath(), expr);
            if (!msg.empty()) {
                if (error)
                    *error = QString::fromStdString(msg);
                return false;
            }
        }
        setExpression(expr);
        return true;
    }
    catch (const Base::Exception& e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    catch (const std::exception& e) {
        if (error)
            *error = QString::fromUtf8(e.what());
    }
    return false;
}

void ExpressionBound::onChange()
{
    syncExpression();
}

bool ExpressionBound::apply(const std::string& propName)
{
    // Gui::QuantitySpinBox::apply, on the bag's value
    if (Gui::ExpressionBinding::apply(propName))
        return false;
    double dValue = boundValue();
    if (isBound()) {
        const App::ObjectIdentifier& path = getPath();
        const App::Property* prop = path.getProperty();
        if (prop && prop->isReadOnly())
            return true;
        if (prop && prop->isDerivedFrom<App::PropertyPlacement>()) {
            if (path.getSubPathStr() == ".Rotation.Angle")
                dValue = Base::toRadians(dValue);
        }
    }
    Gui::Command::doCommand(Gui::Command::Doc, "%s = %f", propName.c_str(), dValue);
    return true;
}

QuantitySpinBox::QuantitySpinBox(Widget* parent)
    : InputField(parent)
    , ExpressionBound(this)
{
    setQtClass(QStringLiteral("Gui::QuantitySpinBox"));
    declare(QStringLiteral("displayUnit"), QString());
    declare(kBinding, QString());
    declare(kExpression, QString());
}

DoubleSpinBox::DoubleSpinBox(Widget* parent)
    : QDoubleSpinBox(parent)
    , ExpressionBound(this)
{
    setQtClass(QStringLiteral("Gui::DoubleSpinBox"));
    declare(kBinding, QString());
    declare(kExpression, QString());
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

// ---- dialogs (G3b) ----------------------------------------------------------

Gui::Fw::QDialog::QDialog(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QDialog"));
    declare(QStringLiteral("modal"), false);
    declare(QStringLiteral("result"), 0);
    declare(QStringLiteral("width"), 0);
    declare(QStringLiteral("height"), 0);
}

void Gui::Fw::QDialog::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("accepted"))
        Q_EMIT accepted();
    else if (name == QLatin1String("rejected"))
        Q_EMIT rejected();
    else if (name == QLatin1String("finished"))
        Q_EMIT finished(args.value(0).toInt());
}

QDialogButtonBox::QDialogButtonBox(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QDialogButtonBox"));
    declare(QStringLiteral("standardButtons"), 0);
    declare(QStringLiteral("orientation"), 1);
    declare(QStringLiteral("centerButtons"), false);
}

void QDialogButtonBox::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("accepted"))
        Q_EMIT accepted();
    else if (name == QLatin1String("rejected"))
        Q_EMIT rejected();
    else if (name == QLatin1String("helpRequested"))
        Q_EMIT helpRequested();
    else if (name == QLatin1String("clicked"))
        Q_EMIT clicked(args.value(0).toInt());
}

// ---- containers (G3b) --------------------------------------------------------

QTabWidget::QTabWidget(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QTabWidget"));
    declare(QStringLiteral("tabs"), QStringList());
    declare(QStringLiteral("currentIndex"), -1);
    declare(QStringLiteral("tabsClosable"), false);
    declare(QStringLiteral("documentMode"), false);
    declare(QStringLiteral("tabPosition"), 0);
}

int QTabWidget::addTab(Widget* page, const QString& title, const QString& iconPath)
{
    return insertTab(_pages.size(), page, title, iconPath);
}

int QTabWidget::insertTab(int index, Widget* page, const QString& title, const QString& iconPath)
{
    if (!page)
        return -1;
    index = std::min(std::max(index, 0), static_cast<int>(_pages.size()));
    page->QObject::setParent(this);
    _pages.insert(index, page);
    QStringList t = tabs();
    t.insert(index, title);
    QVariantMap m;
    m.insert(QStringLiteral("tabs"), t);
    if (currentIndex() < 0)
        m.insert(QStringLiteral("currentIndex"), 0);
    setProperties(m);
    request(QStringLiteral("insertTab"),
            QVariantList {index, QVariant::fromValue<QObject*>(page), title, iconPath});
    return index;
}

void QTabWidget::removeTab(int index)
{
    if (index < 0 || index >= _pages.size())
        return;
    _pages.removeAt(index);
    QStringList t = tabs();
    t.removeAt(index);
    QVariantMap m;
    m.insert(QStringLiteral("tabs"), t);
    m.insert(QStringLiteral("currentIndex"),
             std::min(currentIndex(), static_cast<int>(t.size()) - 1));
    setProperties(m);
    request(QStringLiteral("removeTab"), QVariantList {index});
}

void QTabWidget::setTabText(int index, const QString& text)
{
    QStringList t = tabs();
    if (index < 0 || index >= t.size())
        return;
    t[index] = text;
    setProperty("tabs", t);
}

void QTabWidget::addPage(Widget* page, const QString& title)
{
    _pages.append(page);
    QStringList t = tabs();
    t.append(title);
    setInitial(QStringLiteral("tabs"), t);
    if (currentIndex() < 0)
        setInitial(QStringLiteral("currentIndex"), 0);
}

void QTabWidget::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == QLatin1String("currentIndex"))
        Q_EMIT currentChanged(value.toInt());
}

void QTabWidget::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("tabCloseRequested"))
        Q_EMIT tabCloseRequested(args.value(0).toInt());
    else if (name == QLatin1String("tabBarClicked"))
        Q_EMIT tabBarClicked(args.value(0).toInt());
}

QStackedWidget::QStackedWidget(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QStackedWidget"));
    declare(QStringLiteral("currentIndex"), -1);
}

int QStackedWidget::addWidget(Widget* page)
{
    return insertWidget(_pages.size(), page);
}

int QStackedWidget::insertWidget(int index, Widget* page)
{
    if (!page)
        return -1;
    index = std::min(std::max(index, 0), static_cast<int>(_pages.size()));
    page->QObject::setParent(this);
    _pages.insert(index, page);
    if (currentIndex() < 0)
        setProperty("currentIndex", 0);
    request(QStringLiteral("insertWidget"),
            QVariantList {index, QVariant::fromValue<QObject*>(page)});
    return index;
}

void QStackedWidget::removeWidget(Widget* page)
{
    if (!_pages.removeOne(page))
        return;
    request(QStringLiteral("removeWidget"), QVariantList {QVariant::fromValue<QObject*>(page)});
}

void QStackedWidget::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == QLatin1String("currentIndex"))
        Q_EMIT currentChanged(value.toInt());
}

QScrollArea::QScrollArea(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QScrollArea"));
    declare(QStringLiteral("widgetResizable"), false);
}

void QScrollArea::setWidget(Widget* content)
{
    _content = content;
    if (content)
        content->QObject::setParent(this);
    request(QStringLiteral("setWidget"), QVariantList {QVariant::fromValue<QObject*>(content)});
}

QSplitter::QSplitter(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QSplitter"));
    declare(QStringLiteral("orientation"), 1);
    declare(QStringLiteral("childrenCollapsible"), true);
    declare(QStringLiteral("sizes"), QVariantList());
}

void QSplitter::addWidget(Widget* pane)
{
    insertWidget(_panes.size(), pane);
}

void QSplitter::insertWidget(int index, Widget* pane)
{
    if (!pane)
        return;
    index = std::min(std::max(index, 0), static_cast<int>(_panes.size()));
    pane->QObject::setParent(this);
    _panes.insert(index, pane);
    request(QStringLiteral("insertWidget"),
            QVariantList {index, QVariant::fromValue<QObject*>(pane)});
}

void QSplitter::setSizes(const QList<int>& sizes)
{
    QVariantList v;
    for (int s : sizes)
        v.append(s);
    setProperty("sizes", v);
}

QList<int> QSplitter::sizes() const
{
    QList<int> out;
    for (const QVariant& v : property("sizes").toList())
        out.append(v.toInt());
    return out;
}

void QSplitter::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("splitterMoved"))
        Q_EMIT splitterMoved(args.value(0).toInt(), args.value(1).toInt());
}

FileChooser::FileChooser(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("Gui::FileChooser"));
    declare(QStringLiteral("fileName"), QString());
    declare(QStringLiteral("mode"), 0);
    declare(QStringLiteral("acceptMode"), 0);
    declare(QStringLiteral("filter"), QString());
    declare(QStringLiteral("buttonText"), QString());
}

void FileChooser::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == QLatin1String("fileName"))
        Q_EMIT fileNameChanged(value.toString());
}

void FileChooser::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("fileNameSelected"))
        Q_EMIT fileNameSelected(args.value(0).toString());
}

// ---- item views (G3b) ---------------------------------------------------------

QVariantMap ItemCell::toMap() const
{
    QVariantMap m;
    if (!text.isEmpty())
        m.insert(QStringLiteral("text"), text);
    if (!icon.isEmpty())
        m.insert(QStringLiteral("icon"), icon);
    if (!toolTip.isEmpty())
        m.insert(QStringLiteral("toolTip"), toolTip);
    if (!statusTip.isEmpty())
        m.insert(QStringLiteral("statusTip"), statusTip);
    if (!whatsThis.isEmpty())
        m.insert(QStringLiteral("whatsThis"), whatsThis);
    if (check.isValid())
        m.insert(QStringLiteral("check"), check.toInt());
    if (flags.isValid())
        m.insert(QStringLiteral("flags"), flags.toInt());
    if (!fg.isEmpty())
        m.insert(QStringLiteral("fg"), fg);
    if (!bg.isEmpty())
        m.insert(QStringLiteral("bg"), bg);
    if (bold)
        m.insert(QStringLiteral("bold"), true);
    if (align)
        m.insert(QStringLiteral("align"), align);
    return m;
}

void ItemCell::merge(const QVariantMap& m)
{
    for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
        const QString& k = it.key();
        const QVariant& v = it.value();
        if (k == QLatin1String("text"))
            text = v.toString();
        else if (k == QLatin1String("icon"))
            icon = v.toString();
        else if (k == QLatin1String("toolTip"))
            toolTip = v.toString();
        else if (k == QLatin1String("statusTip"))
            statusTip = v.toString();
        else if (k == QLatin1String("whatsThis"))
            whatsThis = v.toString();
        else if (k == QLatin1String("check"))
            check = v.isNull() ? QVariant() : QVariant(v.toInt());
        else if (k == QLatin1String("flags"))
            flags = v.isNull() ? QVariant() : QVariant(v.toInt());
        else if (k == QLatin1String("fg"))
            fg = v.toList();
        else if (k == QLatin1String("bg"))
            bg = v.toList();
        else if (k == QLatin1String("bold"))
            bold = v.toBool();
        else if (k == QLatin1String("align"))
            align = v.toInt();
    }
}

ItemView::ItemView(Widget* parent)
    : Widget(parent)
{
    declare(QStringLiteral("columns"), QStringList());
    declare(QStringLiteral("columnCount"), 1);
    declare(QStringLiteral("rowLabels"), QStringList());
    declare(QStringLiteral("selection"), QVariantList());
    declare(QStringLiteral("currentId"), 0);
    declare(QStringLiteral("currentColumn"), 0);
    declare(QStringLiteral("selectionMode"), 1);
    declare(QStringLiteral("selectionBehavior"), 0);
    declare(QStringLiteral("editTriggers"), 10);
    declare(QStringLiteral("dragDropMode"), 0);
    declare(QStringLiteral("sortingEnabled"), false);
    declare(QStringLiteral("alternatingRowColors"), false);
    declare(QStringLiteral("headerHidden"), false);
    declare(QStringLiteral("rootIsDecorated"), true);
    declare(QStringLiteral("uniformRowHeights"), false);
    declare(QStringLiteral("itemsExpandable"), true);
    declare(QStringLiteral("indentation"), 20);
    declare(QStringLiteral("showGrid"), true);
    declare(QStringLiteral("wordWrap"), true);
    declare(QStringLiteral("columnWidths"), QVariantList());
    declare(QStringLiteral("cellTypes"), QVariantList());
}

const ItemRow* ItemView::row(int id) const
{
    auto it = _rows.constFind(id);
    return it == _rows.constEnd() ? nullptr : &*it;
}

int ItemView::rowCount(int parentId) const
{
    if (parentId == 0)
        return _top.size();
    const ItemRow* r = row(parentId);
    return r ? r->children.size() : 0;
}

QVariantMap ItemView::rowMap(const ItemRow& r) const
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), r.id);
    QVariantList cells;
    for (const ItemCell& c : r.cells)
        cells.append(c.toMap());
    m.insert(QStringLiteral("cells"), cells);
    if (!r.children.isEmpty()) {
        QVariantList kids;
        for (int cid : r.children)
            if (const ItemRow* c = row(cid))
                kids.append(rowMap(*c));
        m.insert(QStringLiteral("children"), kids);
    }
    if (r.expanded)
        m.insert(QStringLiteral("expanded"), true);
    if (r.hidden)
        m.insert(QStringLiteral("hidden"), true);
    if (r.flags.isValid())
        m.insert(QStringLiteral("flags"), r.flags.toInt());
    return m;
}

QVariantList ItemView::snapshot() const
{
    QVariantList out;
    for (int id : _top)
        if (const ItemRow* r = row(id))
            out.append(rowMap(*r));
    return out;
}

void ItemView::emitItemOp(const QVariantMap& op)
{
    if (backend())
        backend()->itemsChanged(op);
    Q_EMIT itemsChanged(op);
}

void ItemView::insertRows(int parentId, int index, const QVariantList& rows)
{
    if (parentId != 0 && !_rows.contains(parentId))
        return;
    {
        const QList<int>& siblings = parentId == 0 ? _top : _rows[parentId].children;
        if (index < 0 || index > siblings.size())
            index = siblings.size();
    }
    for (const QVariant& v : rows) {
        QVariantMap m = v.toMap();
        ItemRow r;
        r.id = m.value(QStringLiteral("id")).toInt();
        if (r.id <= 0)
            r.id = _nextId++;
        else
            _nextId = std::max(_nextId, r.id + 1);
        r.parent = parentId;
        for (const QVariant& c : m.value(QStringLiteral("cells")).toList()) {
            ItemCell cell;
            cell.merge(c.toMap());
            r.cells.append(cell);
        }
        r.expanded = m.value(QStringLiteral("expanded")).toBool();
        r.hidden = m.value(QStringLiteral("hidden")).toBool();
        if (m.contains(QStringLiteral("flags")))
            r.flags = m.value(QStringLiteral("flags")).toInt();
        const int id = r.id;
        _rows.insert(id, r);
        // fetched anew each time: the recursion below rehashes
        (parentId == 0 ? _top : _rows[parentId].children).insert(index++, id);
        QVariantList kids = m.value(QStringLiteral("children")).toList();
        if (!kids.isEmpty())
            insertRows(id, 0, kids);
    }
}

void ItemView::eraseRow(int id)
{
    auto it = _rows.find(id);
    if (it == _rows.end())
        return;
    QList<int> kids = it->children;
    int parent = it->parent;
    _rows.erase(it);
    for (int k : kids)
        eraseRow(k);
    if (parent == 0)
        _top.removeAll(id);
    else if (_rows.contains(parent))
        _rows[parent].children.removeAll(id);
}

void ItemView::applyItemOp(const QVariantMap& op)
{
    const QString kind = op.value(QStringLiteral("item")).toString();
    const int id = op.value(QStringLiteral("id")).toInt();
    if (kind == QLatin1String("insert")) {
        insertRows(op.value(QStringLiteral("parent")).toInt(),
                   op.contains(QStringLiteral("index")) ? op.value(QStringLiteral("index")).toInt()
                                                        : -1,
                   op.value(QStringLiteral("rows")).toList());
    }
    else if (kind == QLatin1String("set")) {
        auto it = _rows.find(id);
        if (it == _rows.end())
            return;
        int col = op.value(QStringLiteral("col")).toInt();
        while (it->cells.size() <= col)
            it->cells.append(ItemCell());
        it->cells[col].merge(op.value(QStringLiteral("cell")).toMap());
    }
    else if (kind == QLatin1String("row")) {
        auto it = _rows.find(id);
        if (it == _rows.end())
            return;
        QVariantMap r = op.value(QStringLiteral("row")).toMap();
        if (r.contains(QStringLiteral("expanded")))
            it->expanded = r.value(QStringLiteral("expanded")).toBool();
        if (r.contains(QStringLiteral("hidden")))
            it->hidden = r.value(QStringLiteral("hidden")).toBool();
        if (r.contains(QStringLiteral("flags")))
            it->flags = r.value(QStringLiteral("flags")).toInt();
    }
    else if (kind == QLatin1String("remove")) {
        eraseRow(id);
        QVariantList sel = property("selection").toList();
        if (sel.removeAll(QVariant(id)) > 0)
            setProperty("selection", sel);
        if (currentId() == id)
            setProperty("currentId", 0);
    }
    else if (kind == QLatin1String("clear")) {
        _rows.clear();
        _top.clear();
    }
    else if (kind != QLatin1String("sort")) {
        return;
    }
    emitItemOp(op);
}

int ItemView::insertRow(int parentId, int index, const QStringList& texts)
{
    QVariantList cells;
    for (const QString& t : texts) {
        ItemCell c;
        c.text = t;
        cells.append(c.toMap());
    }
    QVariantMap r;
    r.insert(QStringLiteral("id"), _nextId);
    r.insert(QStringLiteral("cells"), cells);
    QVariantMap op;
    op.insert(QStringLiteral("item"), QStringLiteral("insert"));
    op.insert(QStringLiteral("parent"), parentId);
    op.insert(QStringLiteral("index"), index < 0 ? rowCount(parentId) : index);
    op.insert(QStringLiteral("rows"), QVariantList {r});
    int id = _nextId;
    applyItemOp(op);
    return id;
}

void ItemView::removeRow(int id)
{
    QVariantMap op;
    op.insert(QStringLiteral("item"), QStringLiteral("remove"));
    op.insert(QStringLiteral("id"), id);
    applyItemOp(op);
}

void ItemView::clearRows()
{
    QVariantMap op;
    op.insert(QStringLiteral("item"), QStringLiteral("clear"));
    applyItemOp(op);
    QVariantMap m;
    m.insert(QStringLiteral("selection"), QVariantList());
    m.insert(QStringLiteral("currentId"), 0);
    setProperties(m);
}

namespace
{
QVariantMap cellOp(int id, int column, const QString& key, const QVariant& value)
{
    QVariantMap cell;
    cell.insert(key, value);
    QVariantMap op;
    op.insert(QStringLiteral("item"), QStringLiteral("set"));
    op.insert(QStringLiteral("id"), id);
    op.insert(QStringLiteral("col"), column);
    op.insert(QStringLiteral("cell"), cell);
    return op;
}

QVariantMap rowOp(int id, const QString& key, const QVariant& value)
{
    QVariantMap r;
    r.insert(key, value);
    QVariantMap op;
    op.insert(QStringLiteral("item"), QStringLiteral("row"));
    op.insert(QStringLiteral("id"), id);
    op.insert(QStringLiteral("row"), r);
    return op;
}
}  // namespace

QString ItemView::text(int id, int column) const
{
    const ItemRow* r = row(id);
    return r ? r->cells.value(column).text : QString();
}

void ItemView::setText(int id, int column, const QString& text)
{
    if (row(id)) {
        applyItemOp(cellOp(id, column, QStringLiteral("text"), text));
        Q_EMIT itemChanged(id, column);
    }
}

void ItemView::setIcon(int id, int column, const QString& iconPath)
{
    if (row(id))
        applyItemOp(cellOp(id, column, QStringLiteral("icon"), iconPath));
}

void ItemView::setToolTip(int id, int column, const QString& text)
{
    if (row(id))
        applyItemOp(cellOp(id, column, QStringLiteral("toolTip"), text));
}

int ItemView::checkState(int id, int column) const
{
    const ItemRow* r = row(id);
    return r ? r->cells.value(column).check.toInt() : 0;
}

void ItemView::setCheckState(int id, int column, int state)
{
    if (row(id)) {
        applyItemOp(cellOp(id, column, QStringLiteral("check"), state));
        Q_EMIT itemChanged(id, column);
    }
}

void ItemView::setFlags(int id, int flags)
{
    if (row(id))
        applyItemOp(rowOp(id, QStringLiteral("flags"), flags));
}

void ItemView::setExpanded(int id, bool on)
{
    if (row(id))
        applyItemOp(rowOp(id, QStringLiteral("expanded"), on));
}

bool ItemView::isExpanded(int id) const
{
    const ItemRow* r = row(id);
    return r && r->expanded;
}

void ItemView::setRowHidden(int id, bool on)
{
    if (row(id))
        applyItemOp(rowOp(id, QStringLiteral("hidden"), on));
}

void ItemView::setColumns(const QStringList& labels)
{
    QVariantMap m;
    m.insert(QStringLiteral("columns"), labels);
    m.insert(QStringLiteral("columnCount"),
             std::max(columnCount(), static_cast<int>(labels.size())));
    setProperties(m);
}

int ItemView::columnCount() const
{
    return std::max(property("columnCount").toInt(), static_cast<int>(columns().size()));
}

QList<int> ItemView::selection() const
{
    QList<int> out;
    for (const QVariant& v : property("selection").toList())
        out.append(v.toInt());
    return out;
}

void ItemView::setSelection(const QList<int>& ids)
{
    QVariantList v;
    for (int id : ids)
        v.append(id);
    setProperty("selection", v);
}

void ItemView::select(int id, bool on)
{
    QList<int> sel = selection();
    if (on && !sel.contains(id))
        sel.append(id);
    else if (!on)
        sel.removeAll(id);
    else
        return;
    setSelection(sel);
}

void ItemView::setCurrent(int id, int column)
{
    QVariantMap m;
    m.insert(QStringLiteral("currentId"), id);
    m.insert(QStringLiteral("currentColumn"), column);
    setProperties(m);
    if (id)
        select(id, true);
}

void ItemView::setColumnWidth(int column, int width)
{
    QVariantList widths = property("columnWidths").toList();
    while (widths.size() <= column)
        widths.append(100);
    widths[column] = width;
    setProperty("columnWidths", widths);
}

void ItemView::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == QLatin1String("selection")) {
        Q_EMIT itemSelectionChanged();
    }
    else if (name == QLatin1String("currentId")) {
        int prev = _previousCurrent;
        _previousCurrent = value.toInt();
        Q_EMIT currentItemChanged(value.toInt(), prev);
    }
}

void ItemView::dispatchEvent(const QString& name, const QVariantList& args)
{
    const int id = args.value(0).toInt();
    const int col = args.value(1).toInt();
    if (name == QLatin1String("itemClicked"))
        Q_EMIT itemClicked(id, col);
    else if (name == QLatin1String("itemDoubleClicked"))
        Q_EMIT itemDoubleClicked(id, col);
    else if (name == QLatin1String("itemActivated"))
        Q_EMIT itemActivated(id, col);
    else if (name == QLatin1String("itemPressed"))
        Q_EMIT itemPressed(id, col);
    else if (name == QLatin1String("itemEdited")) {
        auto it = _rows.find(id);
        if (it == _rows.end())
            return;
        while (it->cells.size() <= col)
            it->cells.append(ItemCell());
        it->cells[col].merge(args.value(2).toMap());
        Q_EMIT itemChanged(id, col);
    }
    else if (name == QLatin1String("itemExpanded")) {
        auto it = _rows.find(id);
        if (it != _rows.end())
            it->expanded = true;
        Q_EMIT itemExpanded(id);
    }
    else if (name == QLatin1String("itemCollapsed")) {
        auto it = _rows.find(id);
        if (it != _rows.end())
            it->expanded = false;
        Q_EMIT itemCollapsed(id);
    }
}

QListWidget::QListWidget(Widget* parent)
    : ItemView(parent)
{
    setQtClass(QStringLiteral("QListWidget"));
    setInitial(QStringLiteral("rootIsDecorated"), false);
}

QTreeWidget::QTreeWidget(Widget* parent)
    : ItemView(parent)
{
    setQtClass(QStringLiteral("QTreeWidget"));
}

QTreeView::QTreeView(Widget* parent)
    : ItemView(parent)
{
    setQtClass(QStringLiteral("QTreeView"));
}

QTableWidget::QTableWidget(Widget* parent)
    : ItemView(parent)
{
    setQtClass(QStringLiteral("QTableWidget"));
    setInitial(QStringLiteral("rootIsDecorated"), false);
    setInitial(QStringLiteral("columnCount"), 0);
}

void QTableWidget::setRowCount(int n)
{
    while (rowCount() > n)
        removeRow(topLevel().last());
    while (rowCount() < n) {
        QStringList texts;
        for (int c = 0; c < columnCount(); ++c)
            texts.append(QString());
        addRow(texts);
    }
}

void QTableWidget::setItemText(int rowIndex, int column, const QString& text)
{
    if (rowCount() <= rowIndex)
        setRowCount(rowIndex + 1);
    setText(topLevel().value(rowIndex), column, text);
}

QListView::QListView(Widget* parent)
    : ItemView(parent)
{
    setQtClass(QStringLiteral("QListView"));
    setInitial(QStringLiteral("rootIsDecorated"), false);
}

QTableView::QTableView(Widget* parent)
    : ItemView(parent)
{
    setQtClass(QStringLiteral("QTableView"));
    setInitial(QStringLiteral("rootIsDecorated"), false);
}

// ---- the form -------------------------------------------------------------------

UiForm::UiForm(Widget* parent)
    : Gui::Fw::QDialog(parent)
{
    setQtClass(QStringLiteral("QWidget"));
}

void UiForm::addNamed(const QString& name, Widget* widget)
{
    if (!widget || name.isEmpty())
        return;
    _named.insert(name, widget);
    connect(widget, &QObject::destroyed, this, [this, name]() { _named.remove(name); });
}

// ---- actions, tool bars, menus (G3c) -------------------------------------------

Gui::Fw::QAction::QAction(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QAction"));
    declare(kText, QString());
    declare(QStringLiteral("icon"), QString());
    declare(QStringLiteral("checkable"), false);
    declare(kChecked, false);
    declare(QStringLiteral("shortcut"), QString());
    declare(QStringLiteral("separator"), false);
}

Gui::Fw::QAction::QAction(const QString& text, Widget* parent)
    : QAction(parent)
{
    setText(text);
}

void Gui::Fw::QAction::trigger()
{
    if (isCheckable())
        setChecked(!isChecked());
    notify(QStringLiteral("triggered"), QVariantList {isChecked()});
}

void Gui::Fw::QAction::propertyDidChange(const QString& name, const QVariant& value)
{
    if (name == kChecked)
        Q_EMIT toggled(value.toBool());
}

void Gui::Fw::QAction::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("triggered"))
        Q_EMIT triggered(args.isEmpty() ? isChecked() : args.at(0).toBool());
    else if (name == QLatin1String("hovered"))
        Q_EMIT hovered();
}

namespace
{
const QString kBar = QStringLiteral("_fcx_bar");

Layout* barOf(Widget* w)
{
    if (Layout* lay = w->layout())
        return lay;
    auto lay = new Layout(Layout::Bar, w);
    lay->setObjectName(kBar);
    return lay;
}

QList<Widget*> actionsOf(const Widget* w)
{
    QList<Widget*> out;
    Layout* lay = w->layout();
    for (int i = 0; lay && i < lay->count(); ++i)
        if (const LayoutItem* item = lay->itemAt(i))
            if (item->action)
                out.append(item->action);
    return out;
}
}  // namespace

Gui::Fw::QToolBar::QToolBar(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QToolBar"));
    declare(QStringLiteral("iconSize"), 0);
    declare(QStringLiteral("toolButtonStyle"), 0);
    declare(QStringLiteral("movable"), true);
    declare(QStringLiteral("floatable"), true);
    declare(QStringLiteral("orientation"), 1);
    declare(QStringLiteral("toggleViewAction"), QVariant());
}

Gui::Fw::QToolBar::QToolBar(const QString& title, Widget* parent)
    : QToolBar(parent)
{
    setWindowTitle(title);
}

Layout* Gui::Fw::QToolBar::bar()
{
    return barOf(this);
}

void Gui::Fw::QToolBar::addWidget(Widget* widget)
{
    bar()->addWidget(widget);
}

void Gui::Fw::QToolBar::addAction(Widget* action)
{
    bar()->addAction(action);
}

void Gui::Fw::QToolBar::addSeparator()
{
    bar()->addSeparator();
}

void Gui::Fw::QToolBar::clear()
{
    bar()->clear();
}

QList<Widget*> Gui::Fw::QToolBar::actions() const
{
    return actionsOf(this);
}

Gui::Fw::QAction* Gui::Fw::QToolBar::toggleViewAction()
{
    auto a = qobject_cast<QAction*>(property("toggleViewAction").value<QObject*>());
    if (!a) {
        a = new QAction(windowTitle(), this);
        a->setCheckable(true);
        setProperty("toggleViewAction", QVariant::fromValue<QObject*>(a));
    }
    return a;
}

void Gui::Fw::QToolBar::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("actionTriggered"))
        Q_EMIT actionTriggered(qobject_cast<Widget*>(args.value(0).value<QObject*>()));
}

Gui::Fw::QMenu::QMenu(Widget* parent)
    : Widget(parent)
{
    setQtClass(QStringLiteral("QMenu"));
    declare(QStringLiteral("title"), QString());
    declare(QStringLiteral("icon"), QString());
    declare(QStringLiteral("tearOffEnabled"), false);
}

Gui::Fw::QMenu::QMenu(const QString& title, Widget* parent)
    : QMenu(parent)
{
    setTitle(title);
}

Layout* Gui::Fw::QMenu::bar()
{
    return barOf(this);
}

void Gui::Fw::QMenu::addAction(Widget* action)
{
    bar()->addAction(action);
}

void Gui::Fw::QMenu::addMenu(QMenu* menu)
{
    bar()->addWidget(menu);
}

void Gui::Fw::QMenu::addSeparator()
{
    bar()->addSeparator();
}

void Gui::Fw::QMenu::clear()
{
    bar()->clear();
}

QList<Widget*> Gui::Fw::QMenu::actions() const
{
    return actionsOf(this);
}

void Gui::Fw::QMenu::dispatchEvent(const QString& name, const QVariantList& args)
{
    if (name == QLatin1String("triggered"))
        Q_EMIT triggered(qobject_cast<Widget*>(args.value(0).value<QObject*>()));
    else if (name == QLatin1String("aboutToShow"))
        Q_EMIT aboutToShow();
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
        {QStringLiteral("QLineEdit"), maker<Gui::Fw::QLineEdit>()},
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
        {QStringLiteral("Gui::PrefLineEdit"), maker<Gui::Fw::QLineEdit>()},
        {QStringLiteral("Gui::PrefTextEdit"), maker<QTextEdit>()},
        {QStringLiteral("Gui::PrefComboBox"), maker<QComboBox>()},
        {QStringLiteral("Gui::PrefSpinBox"), maker<QSpinBox>()},
        {QStringLiteral("Gui::DoubleSpinBox"), maker<DoubleSpinBox>()},
        {QStringLiteral("Gui::PrefDoubleSpinBox"), maker<DoubleSpinBox>()},
        {QStringLiteral("Gui::PrefSlider"), maker<QSlider>()},
        {QStringLiteral("Gui::PrefCheckableGroupBox"), maker<QGroupBox>()},
        {QStringLiteral("Gui::PrefFontBox"), maker<QFontComboBox>()},
        {QStringLiteral("QDialog"), maker<Gui::Fw::QDialog>()},
        {QStringLiteral("QDialogButtonBox"), maker<QDialogButtonBox>()},
        {QStringLiteral("QTabWidget"), maker<QTabWidget>()},
        {QStringLiteral("QStackedWidget"), maker<QStackedWidget>()},
        {QStringLiteral("QScrollArea"), maker<QScrollArea>()},
        {QStringLiteral("QSplitter"), maker<QSplitter>()},
        {QStringLiteral("Gui::FileChooser"), maker<FileChooser>()},
        {QStringLiteral("Gui::PrefFileChooser"), maker<FileChooser>()},
        {QStringLiteral("QListWidget"), maker<QListWidget>()},
        {QStringLiteral("QTreeWidget"), maker<QTreeWidget>()},
        {QStringLiteral("QTreeView"), maker<QTreeView>()},
        {QStringLiteral("QTableWidget"), maker<QTableWidget>()},
        {QStringLiteral("QListView"), maker<QListView>()},
        {QStringLiteral("QTableView"), maker<QTableView>()},
        {QStringLiteral("QColumnView"), maker<QTreeView>()},
        {QStringLiteral("UiForm"), maker<UiForm>()},
        {QStringLiteral("QAction"), maker<Gui::Fw::QAction>()},
        {QStringLiteral("QToolBar"), maker<Gui::Fw::QToolBar>()},
        {QStringLiteral("QMenu"), maker<Gui::Fw::QMenu>()},
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
    if (base == QLatin1String("FileChooser"))
        return QStringLiteral("Gui::FileChooser");
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
    else if (className == QLatin1String("_bar"))
        kind = Layout::Bar;
    return new Layout(kind, owner);
}

#include "moc_FwWidgets.cpp"
