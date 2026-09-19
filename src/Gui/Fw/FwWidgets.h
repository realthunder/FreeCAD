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

#ifndef GUI_FW_WIDGETS_H
#define GUI_FW_WIDGETS_H

/* The host widget layer (docs/Sandbox.md 7.12): the class set.
 *
 * One model per Qt class, Qt's property names as the state and Qt's
 * method and signal names as the API -- the same 22 classes, the same
 * ~60 properties as the sandbox guest's `freecad.widgets` models, so
 * a guest comm and a native `Ui_X::setupUi` produce the same objects.
 * The subset is the one the workbenches' panels use (docs/Sandbox.md
 * 7.11 counts it); a Qt method the corpus never calls is not here.
 * Icons and pixmaps are paths, a color is four floats, a quantity is
 * its value in internal units plus a unit string: data a DOM tier can
 * carry.
 */

#include <QColor>
#include <QHash>
#include <QStringList>
#include <QVariantMap>

#include <Base/Quantity.h>

#include <Gui/ExpressionBinding.h>
#include "FwCore.h"

namespace Gui
{
namespace Fw
{

#define FW_MODEL(Name)                                                                            \
    QString modelName() const override                                                            \
    {                                                                                             \
        return QStringLiteral(Name);                                                              \
    }

// ---- labels and buttons -----------------------------------------------------

class GuiExport QLabel : public Widget
{
    Q_OBJECT
public:
    explicit QLabel(Widget* parent = nullptr);
    explicit QLabel(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QLabelModel")

    void setText(const QString& text)
    {
        setProperty("text", text);
    }
    QString text() const
    {
        return property("text").toString();
    }
    void clear()
    {
        setText(QString());
    }
    void setWordWrap(bool on)
    {
        setProperty("wordWrap", on);
    }
    bool wordWrap() const
    {
        return property("wordWrap").toBool();
    }
    void setAlignment(int alignment)
    {
        setProperty("alignment", alignment);
    }
    int alignment() const
    {
        return property("alignment").toInt();
    }
    void setOpenExternalLinks(bool on)
    {
        setProperty("openExternalLinks", on);
    }
    /// A path (a resource or a file), or `bitmap:<name>` for one of
    /// FreeCAD's own icons (the BitmapFactory's), rendered at the
    /// label's size.
    void setPixmap(const QString& path)
    {
        setProperty("pixmap", path);
    }
    QString pixmap() const
    {
        return property("pixmap").toString();
    }

Q_SIGNALS:
    void linkActivated(const QString& link);

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport QAbstractButton : public Widget
{
    Q_OBJECT
public:
    explicit QAbstractButton(Widget* parent = nullptr);

    void setText(const QString& text)
    {
        setProperty("text", text);
    }
    QString text() const
    {
        return property("text").toString();
    }
    void setIcon(const QString& path)
    {
        setProperty("icon", path);
    }
    QString icon() const
    {
        return property("icon").toString();
    }
    void setCheckable(bool on)
    {
        setProperty("checkable", on);
    }
    bool isCheckable() const
    {
        return property("checkable").toBool();
    }
    /// Checks; with `autoExclusive`, unchecks the exclusive siblings.
    void setChecked(bool on);
    bool isChecked() const
    {
        return property("checked").toBool();
    }
    void toggle()
    {
        setChecked(!isChecked());
    }
    /// A programmatic click: toggles a checkable button, fires `clicked`.
    void click();
    /// The button's popup menu (a `QMenu` model; the backend sets the
    /// real one on the real button).
    void setMenu(Widget* menu)
    {
        setProperty("menu", QVariant::fromValue<QObject*>(menu));
    }
    Widget* menu() const
    {
        return qobject_cast<Widget*>(property("menu").value<QObject*>());
    }
    void setAutoExclusive(bool on)
    {
        setProperty("autoExclusive", on);
    }
    bool autoExclusive() const
    {
        return property("autoExclusive").toBool();
    }

Q_SIGNALS:
    void clicked(bool checked = false);
    void pressed();
    void released();
    void toggled(bool checked);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport QPushButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit QPushButton(Widget* parent = nullptr);
    explicit QPushButton(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QPushButtonModel")

    void setFlat(bool on)
    {
        setProperty("flat", on);
    }
    bool isFlat() const
    {
        return property("flat").toBool();
    }
    void setDefault(bool on)
    {
        setProperty("default", on);
    }
    bool isDefault() const
    {
        return property("default").toBool();
    }
};

class GuiExport QToolButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit QToolButton(Widget* parent = nullptr);
    FW_MODEL("QToolButtonModel")

    void setAutoRaise(bool on)
    {
        setProperty("autoRaise", on);
    }
    bool autoRaise() const
    {
        return property("autoRaise").toBool();
    }
    /// The tool bar button made for `action` (`QToolBar::widgetForAction`):
    /// a backend binds this model to that real button instead of making one.
    void setForAction(Widget* action)
    {
        setProperty("forAction", QVariant::fromValue<QObject*>(action));
    }
    Widget* forAction() const
    {
        return qobject_cast<Widget*>(property("forAction").value<QObject*>());
    }
    void setDefaultAction(Widget* action)
    {
        setProperty("defaultAction", QVariant::fromValue<QObject*>(action));
    }
};

class GuiExport QCheckBox : public QAbstractButton
{
    Q_OBJECT
public:
    explicit QCheckBox(Widget* parent = nullptr);
    explicit QCheckBox(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QCheckBoxModel")

    void setTristate(bool on = true)
    {
        setProperty("tristate", on);
    }
    bool isTristate() const
    {
        return property("tristate").toBool();
    }
    /// Qt::Unchecked (0) or Qt::Checked (2); no partial state in the bag.
    int checkState() const
    {
        return isChecked() ? 2 : 0;
    }
    void setCheckState(int state)
    {
        setChecked(state == 2);
    }

Q_SIGNALS:
    void stateChanged(int state);
    void checkStateChanged(int state);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
};

class GuiExport QRadioButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit QRadioButton(Widget* parent = nullptr);
    explicit QRadioButton(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QRadioButtonModel")
};

class GuiExport QGroupBox : public Widget
{
    Q_OBJECT
public:
    explicit QGroupBox(Widget* parent = nullptr);
    explicit QGroupBox(const QString& title, Widget* parent = nullptr);
    FW_MODEL("QGroupBoxModel")

    void setTitle(const QString& title)
    {
        setProperty("title", title);
    }
    QString title() const
    {
        return property("title").toString();
    }
    void setCheckable(bool on)
    {
        setProperty("checkable", on);
    }
    bool isCheckable() const
    {
        return property("checkable").toBool();
    }
    void setChecked(bool on)
    {
        setProperty("checked", on);
    }
    bool isChecked() const
    {
        return property("checked").toBool();
    }
    void setFlat(bool on)
    {
        setProperty("flat", on);
    }
    bool isFlat() const
    {
        return property("flat").toBool();
    }

Q_SIGNALS:
    void toggled(bool on);
    void clicked(bool checked = false);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport QFrame : public Widget
{
    Q_OBJECT
public:
    explicit QFrame(Widget* parent = nullptr);
    FW_MODEL("QFrameModel")
};

// ---- text inputs ------------------------------------------------------------

class GuiExport QLineEdit : public Widget
{
    Q_OBJECT
public:
    explicit QLineEdit(Widget* parent = nullptr);
    explicit QLineEdit(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QLineEditModel")

    void setText(const QString& text)
    {
        setProperty("text", text);
    }
    QString text() const
    {
        return property("text").toString();
    }
    void clear()
    {
        setText(QString());
    }
    void setPlaceholderText(const QString& text)
    {
        setProperty("placeholderText", text);
    }
    QString placeholderText() const
    {
        return property("placeholderText").toString();
    }
    void setReadOnly(bool on)
    {
        setProperty("readOnly", on);
    }
    bool isReadOnly() const
    {
        return property("readOnly").toBool();
    }
    void setMaxLength(int n)
    {
        setProperty("maxLength", n);
    }
    void setEchoMode(int mode)
    {
        setProperty("echoMode", mode);
    }
    void setClearButtonEnabled(bool on)
    {
        setProperty("clearButtonEnabled", on);
    }
    void selectAll()
    {
        request(QStringLiteral("selectAll"));
    }
    void setSelection(int start, int length)
    {
        request(QStringLiteral("setSelection"), QVariantList {start, length});
    }
    void setCursorPosition(int pos)
    {
        request(QStringLiteral("setCursorPosition"), QVariantList {pos});
    }

Q_SIGNALS:
    void textChanged(const QString& text);
    void textEdited(const QString& text);
    void returnPressed();
    void editingFinished();
    void selectionChanged();

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport QTextEdit : public Widget
{
    Q_OBJECT
public:
    explicit QTextEdit(Widget* parent = nullptr);
    explicit QTextEdit(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QTextEditModel")

    void setPlainText(const QString& text);
    void setText(const QString& text)
    {
        setPlainText(text);
    }
    void setHtml(const QString& html)
    {
        setProperty("html", html);
    }
    QString toPlainText() const
    {
        return property("plainText").toString();
    }
    QString toHtml() const
    {
        return property("html").toString();
    }
    void insertPlainText(const QString& text)
    {
        setPlainText(toPlainText() + text);
    }
    void append(const QString& text);
    void clear();
    void setReadOnly(bool on)
    {
        setProperty("readOnly", on);
    }
    bool isReadOnly() const
    {
        return property("readOnly").toBool();
    }
    void setPlaceholderText(const QString& text)
    {
        setProperty("placeholderText", text);
    }
    void selectAll()
    {
        request(QStringLiteral("selectAll"));
    }

Q_SIGNALS:
    void textChanged();

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
};

class GuiExport QPlainTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit QPlainTextEdit(Widget* parent = nullptr);
    FW_MODEL("QPlainTextEditModel")
};

class GuiExport QTextBrowser : public QTextEdit
{
    Q_OBJECT
public:
    explicit QTextBrowser(Widget* parent = nullptr);
    FW_MODEL("QTextBrowserModel")

    void setOpenExternalLinks(bool on)
    {
        setProperty("openExternalLinks", on);
    }

Q_SIGNALS:
    void anchorClicked(const QString& url);

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

// ---- numbers ------------------------------------------------------------------

class GuiExport QAbstractSpinBox : public Widget
{
    Q_OBJECT
public:
    explicit QAbstractSpinBox(Widget* parent = nullptr);

    void setPrefix(const QString& s)
    {
        setProperty("prefix", s);
    }
    QString prefix() const
    {
        return property("prefix").toString();
    }
    void setSuffix(const QString& s)
    {
        setProperty("suffix", s);
    }
    QString suffix() const
    {
        return property("suffix").toString();
    }
    void setReadOnly(bool on)
    {
        setProperty("readOnly", on);
    }
    bool isReadOnly() const
    {
        return property("readOnly").toBool();
    }
    void selectAll()
    {
        request(QStringLiteral("selectAll"));
    }

Q_SIGNALS:
    void editingFinished();

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
    /// Bring `value` inside [minimum, maximum].
    void clampValue();
};

class GuiExport QSpinBox : public QAbstractSpinBox
{
    Q_OBJECT
public:
    explicit QSpinBox(Widget* parent = nullptr);
    FW_MODEL("QSpinBoxModel")

    void setValue(int v);
    int value() const
    {
        return property("value").toInt();
    }
    void setMinimum(int v);
    int minimum() const
    {
        return property("minimum").toInt();
    }
    void setMaximum(int v);
    int maximum() const
    {
        return property("maximum").toInt();
    }
    void setRange(int lo, int hi);
    void setSingleStep(int v)
    {
        setProperty("singleStep", v);
    }
    int singleStep() const
    {
        return property("singleStep").toInt();
    }

Q_SIGNALS:
    void valueChanged(int value);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
};

class GuiExport QDoubleSpinBox : public QAbstractSpinBox
{
    Q_OBJECT
public:
    explicit QDoubleSpinBox(Widget* parent = nullptr);
    FW_MODEL("QDoubleSpinBoxModel")

    void setValue(double v);
    double value() const
    {
        return property("value").toDouble();
    }
    void setMinimum(double v);
    double minimum() const
    {
        return property("minimum").toDouble();
    }
    void setMaximum(double v);
    double maximum() const
    {
        return property("maximum").toDouble();
    }
    void setRange(double lo, double hi);
    void setSingleStep(double v)
    {
        setProperty("singleStep", v);
    }
    double singleStep() const
    {
        return property("singleStep").toDouble();
    }
    void setDecimals(int n)
    {
        setProperty("decimals", n);
    }
    int decimals() const
    {
        return property("decimals").toInt();
    }

Q_SIGNALS:
    void valueChanged(double value);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
};

class GuiExport QSlider : public Widget
{
    Q_OBJECT
public:
    explicit QSlider(Widget* parent = nullptr);
    /// `orientation`: Qt::Horizontal (1) or Qt::Vertical (2).
    explicit QSlider(int orientation, Widget* parent = nullptr);
    FW_MODEL("QSliderModel")

    void setValue(int v);
    int value() const
    {
        return property("value").toInt();
    }
    void setMinimum(int v)
    {
        setProperty("minimum", v);
    }
    int minimum() const
    {
        return property("minimum").toInt();
    }
    void setMaximum(int v)
    {
        setProperty("maximum", v);
    }
    int maximum() const
    {
        return property("maximum").toInt();
    }
    void setRange(int lo, int hi);
    void setSingleStep(int v)
    {
        setProperty("singleStep", v);
    }
    void setOrientation(int o)
    {
        setProperty("orientation", o);
    }
    int orientation() const
    {
        return property("orientation").toInt();
    }

Q_SIGNALS:
    void valueChanged(int value);
    void sliderReleased();

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport QProgressBar : public Widget
{
    Q_OBJECT
public:
    explicit QProgressBar(Widget* parent = nullptr);
    FW_MODEL("QProgressBarModel")

    void setValue(int v)
    {
        setProperty("value", v);
    }
    int value() const
    {
        return property("value").toInt();
    }
    void setMinimum(int v)
    {
        setProperty("minimum", v);
    }
    int minimum() const
    {
        return property("minimum").toInt();
    }
    void setMaximum(int v)
    {
        setProperty("maximum", v);
    }
    int maximum() const
    {
        return property("maximum").toInt();
    }
    void setRange(int lo, int hi);
    void setFormat(const QString& f)
    {
        setProperty("format", f);
    }
    QString format() const
    {
        return property("format").toString();
    }
    void reset()
    {
        setValue(minimum());
    }
};

// ---- choices ------------------------------------------------------------------

class GuiExport QComboBox : public Widget
{
    Q_OBJECT
public:
    explicit QComboBox(Widget* parent = nullptr);
    FW_MODEL("QComboBoxModel")

    void addItem(const QString& text, const QVariant& userData = QVariant());
    void addItem(const QString& iconPath, const QString& text,
                 const QVariant& userData = QVariant());
    void addItems(const QStringList& texts);
    void insertItem(int index, const QString& text, const QVariant& userData = QVariant());
    void removeItem(int index);
    void clear();
    int count() const
    {
        return items().size();
    }
    QStringList items() const
    {
        return property("items").toStringList();
    }
    QString itemText(int index) const;
    void setItemText(int index, const QString& text);
    QVariant itemData(int index) const;
    void setItemData(int index, const QVariant& data);
    void setItemIcon(int index, const QString& path);
    int currentIndex() const
    {
        return property("currentIndex").toInt();
    }
    QString currentText() const;
    QVariant currentData() const
    {
        return itemData(currentIndex());
    }
    void setCurrentIndex(int index);
    void setCurrentText(const QString& text);
    void setEditText(const QString& text)
    {
        setProperty("editText", text);
    }
    int findText(const QString& text) const
    {
        return items().indexOf(text);
    }
    int findData(const QVariant& data) const;
    void setEditable(bool on)
    {
        setProperty("editable", on);
    }
    bool isEditable() const
    {
        return property("editable").toBool();
    }

Q_SIGNALS:
    void currentIndexChanged(int index);
    void currentTextChanged(const QString& text);
    void activated(int index);
    void highlighted(int index);
    void editTextChanged(const QString& text);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
    void setItems(const QStringList& items, const QStringList& icons, int current);

private:
    QVariantList _itemData;
};

class GuiExport QFontComboBox : public QComboBox
{
    Q_OBJECT
public:
    explicit QFontComboBox(Widget* parent = nullptr);
    FW_MODEL("QFontComboBoxModel")

    QString currentFont() const
    {
        return currentText();
    }
    void setCurrentFont(const QString& family)
    {
        setCurrentText(family);
    }
};

// ---- FreeCAD's own --------------------------------------------------------------

/// `Gui::InputField`: a quantity input.  `rawValue` is the value in
/// internal units (mm, deg), `text` the string as shown -- formatted
/// here for a value something set, in the user's unit schema.
class GuiExport InputField : public QLineEdit
{
    Q_OBJECT
public:
    explicit InputField(Widget* parent = nullptr);
    FW_MODEL("InputFieldModel")

    void setValue(double value);
    void setValue(const Base::Quantity& value);
    void setRawValue(double value)
    {
        setValue(value);
    }
    double rawValue() const
    {
        return property("rawValue").toDouble();
    }
    Base::Quantity getQuantity() const;
    void setUnitText(const QString& unit)
    {
        setProperty("unit", unit);
    }
    QString getUnitText() const
    {
        return property("unit").toString();
    }
    void setMinimum(double v)
    {
        setProperty("minimum", v);
    }
    double minimum() const
    {
        return property("minimum").toDouble();
    }
    void setMaximum(double v)
    {
        setProperty("maximum", v);
    }
    double maximum() const
    {
        return property("maximum").toDouble();
    }
    void setRange(double lo, double hi);
    void setSingleStep(double v)
    {
        setProperty("singleStep", v);
    }
    double singleStep() const
    {
        return property("singleStep").toDouble();
    }
    void setDecimals(int n)
    {
        setProperty("decimals", n);
    }
    int decimals() const
    {
        return property("decimals").toInt();
    }
    void setPrecision(int n)
    {
        setProperty("precision", n);
    }
    void setHistorySize(int n)
    {
        setProperty("historySize", n);
    }
    void setFormat(const QString& f)
    {
        setProperty("format", f);
    }
    void setQuantityString(const QString& s)
    {
        setProperty("quantityString", s);
    }
    void setParamGrpPath(const QString& path)
    {
        setPrefPath(path);
    }
    void setParamGrpPath(const QByteArray& path)
    {
        setPrefPath(QString::fromUtf8(path));
    }
    void setEntryName(const QByteArray& name)
    {
        setPrefEntry(QString::fromUtf8(name));
    }
    /// The text for `value` in the unit schema at the user's decimals.
    QString formatValue(double value) const;

Q_SIGNALS:
    void valueChanged(double value);
    void parseError(const QString& text);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

/// The expression seam (docs/Sandbox.md 7.12): a model bound to a
/// property OWNS the binding -- `Gui::ExpressionBinding` is already a
/// non-widget mixin -- and mirrors it in two bag keys: `binding`, the
/// bound path as the real widget's `binding` property spells it, and
/// `expression`, the expression's text or empty.  The Qt backend binds
/// the real widget to the same path, so its f(x) label and expression
/// dialog work as they do today; what the dialog sets lands in the
/// document, and the document's change comes back here through the
/// binding (`onChange`) into the bag and the value.  `apply` is the
/// real widgets' `apply`: the expression stands, else the value goes
/// to the property through a command.
class GuiExport ExpressionBound : public Gui::ExpressionBinding
{
public:
    explicit ExpressionBound(Widget* owner);
    ~ExpressionBound() override;

    void bind(const App::ObjectIdentifier& path) override;
    using Gui::ExpressionBinding::bind;
    bool apply(const std::string& propName) override;
    using Gui::ExpressionBinding::apply;
    /// Set (or clear, with nullptr) the bound property's expression.
    void setExpression(std::shared_ptr<App::Expression> expr) override;
    /// Parse and set (empty clears); false, with the message in `error`,
    /// when it did not take.
    bool setExpressionText(const QString& text, QString* error = nullptr);
    /// The bag's `expression`: the text, or empty.
    QString expressionText() const;
    /// The bag's `binding`: the path, or empty.
    QString boundToName() const;
    /// The typed path (a backend binds the real widget to it).
    const App::ObjectIdentifier& boundPath() const
    {
        return getPath();
    }
    /// Evaluate a bound expression into the value.
    void evaluateExpression();

protected:
    void onChange() override;
    /// The value `apply` writes when there is no expression.
    virtual double boundValue() const = 0;
    /// An expression evaluated to a number: the value follows.
    virtual void setEvaluated(double value) = 0;
    /// Write `expression` from the binding's state and evaluate it.
    void syncExpression();

private:
    Widget* _owner;
};

class GuiExport QuantitySpinBox : public InputField, public ExpressionBound
{
    Q_OBJECT
public:
    explicit QuantitySpinBox(Widget* parent = nullptr);
    FW_MODEL("QuantitySpinBoxModel")

    void setDisplayUnit(const QString& unit)
    {
        setProperty("displayUnit", unit);
    }
    QString displayUnit() const
    {
        return property("displayUnit").toString();
    }
    Base::Quantity value() const
    {
        return getQuantity();
    }
    /// `Gui::QuantitySpinBox::selectNumber`: a request.
    void selectNumber()
    {
        request(QStringLiteral("selectNumber"));
    }
    /// `Gui::PrefQuantitySpinBox`'s history, kept on the Qt side: requests.
    void setToLastUsedValue()
    {
        request(QStringLiteral("setToLastUsedValue"));
    }
    void pushToHistory()
    {
        request(QStringLiteral("pushToHistory"));
    }

protected:
    double boundValue() const override
    {
        return rawValue();
    }
    void setEvaluated(double v) override
    {
        setValue(v);
    }
};

/// `Gui::DoubleSpinBox`: a QDoubleSpinBox with the expression seam.
class GuiExport DoubleSpinBox : public QDoubleSpinBox, public ExpressionBound
{
    Q_OBJECT
public:
    explicit DoubleSpinBox(Widget* parent = nullptr);
    FW_MODEL("DoubleSpinBoxModel")

protected:
    double boundValue() const override
    {
        return value();
    }
    void setEvaluated(double v) override
    {
        setValue(v);
    }
};

/// `Gui::ColorButton`: a color as four floats, `changed` when picked.
class GuiExport ColorButton : public QPushButton
{
    Q_OBJECT
public:
    explicit ColorButton(Widget* parent = nullptr);
    FW_MODEL("ColorButtonModel")

    void setColor(const QColor& color);
    QColor color() const;
    void setAllowTransparency(bool on)
    {
        setProperty("allowTransparency", on);
    }
    void setAllowChangeColor(bool on)
    {
        setProperty("allowChangeColor", on);
    }
    void setDrawFrame(bool on)
    {
        setProperty("drawFrame", on);
    }

Q_SIGNALS:
    void changed();

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
};

// ---- dialogs (G3b) ----------------------------------------------------------

/// A dialog: `accept`/`reject`/`done` are requests to the backend,
/// `accepted`/`rejected`/`finished` events from it, `result` state.
/// `exec` is the backend's (`FwQt::execDialog`): a nested event loop
/// is a backend fact, not a model one.
class GuiExport QDialog : public Widget
{
    Q_OBJECT
public:
    explicit QDialog(Widget* parent = nullptr);
    FW_MODEL("QDialogModel")

    enum DialogCode
    {
        Rejected = 0,
        Accepted = 1
    };

    void accept()
    {
        request(QStringLiteral("accept"));
    }
    void reject()
    {
        request(QStringLiteral("reject"));
    }
    void done(int r)
    {
        request(QStringLiteral("done"), QVariantList {r});
    }
    void close()
    {
        request(QStringLiteral("close"));
    }
    int result() const
    {
        return property("result").toInt();
    }
    void setModal(bool on)
    {
        setProperty("modal", on);
    }
    bool isModal() const
    {
        return property("modal").toBool();
    }
    void move(int x, int y)
    {
        request(QStringLiteral("move"), QVariantList {x, y});
    }
    void resize(int w, int h)
    {
        request(QStringLiteral("resize"), QVariantList {w, h});
    }
    void adjustSize()
    {
        request(QStringLiteral("adjustSize"));
    }
    /// The realized window's size, written back by the backend.
    int width() const
    {
        return property("width").toInt();
    }
    int height() const
    {
        return property("height").toInt();
    }

Q_SIGNALS:
    void accepted();
    void rejected();
    void finished(int result);

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

/// The standard buttons of a dialog (Qt's flag values).
class GuiExport QDialogButtonBox : public Widget
{
    Q_OBJECT
public:
    explicit QDialogButtonBox(Widget* parent = nullptr);
    FW_MODEL("QDialogButtonBoxModel")

    enum StandardButton
    {
        NoButton = 0,
        Ok = 0x00000400,
        Save = 0x00000800,
        SaveAll = 0x00001000,
        Open = 0x00002000,
        Yes = 0x00004000,
        YesToAll = 0x00008000,
        No = 0x00010000,
        NoToAll = 0x00020000,
        Abort = 0x00040000,
        Retry = 0x00080000,
        Ignore = 0x00100000,
        Close = 0x00200000,
        Cancel = 0x00400000,
        Discard = 0x00800000,
        Help = 0x01000000,
        Apply = 0x02000000,
        Reset = 0x04000000,
        RestoreDefaults = 0x08000000
    };

    void setStandardButtons(int buttons)
    {
        setProperty("standardButtons", buttons);
    }
    int standardButtons() const
    {
        return property("standardButtons").toInt();
    }
    void setOrientation(int orientation)
    {
        setProperty("orientation", orientation);
    }
    void setCenterButtons(bool on)
    {
        setProperty("centerButtons", on);
    }
    /// One of the box's buttons: `setEnabled`, `setText`, `setDefault`,
    /// `setFocus`, `click` on it, as a request the backend applies.
    void setButtonEnabled(int button, bool on)
    {
        buttonCall(button, QStringLiteral("setEnabled"), QVariantList {on});
    }
    void setButtonText(int button, const QString& text)
    {
        buttonCall(button, QStringLiteral("setText"), QVariantList {text});
    }
    void setButtonDefault(int button, bool on)
    {
        buttonCall(button, QStringLiteral("setDefault"), QVariantList {on});
    }
    void clickButton(int button)
    {
        buttonCall(button, QStringLiteral("click"));
    }

Q_SIGNALS:
    void accepted();
    void rejected();
    void helpRequested();
    /// The standard button clicked (its flag value).
    void clicked(int button);

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;

private:
    void buttonCall(int button, const QString& method, const QVariantList& args = QVariantList())
    {
        request(QStringLiteral("button"), QVariantList {button, method, args});
    }
};

// ---- containers (G3b) --------------------------------------------------------

/// Pages with titles: the pages are child widgets, the titles the
/// `tabs` state, the current one an index.
class GuiExport QTabWidget : public Widget
{
    Q_OBJECT
public:
    explicit QTabWidget(Widget* parent = nullptr);
    FW_MODEL("QTabWidgetModel")

    /// Adds `page` (re-parented here) as a tab; the index.
    int addTab(Widget* page, const QString& title, const QString& iconPath = QString());
    int insertTab(int index, Widget* page, const QString& title,
                  const QString& iconPath = QString());
    void removeTab(int index);
    int count() const
    {
        return tabs().size();
    }
    QStringList tabs() const
    {
        return property("tabs").toStringList();
    }
    Widget* widget(int index) const
    {
        return _pages.value(index);
    }
    int indexOf(const Widget* page) const
    {
        return _pages.indexOf(const_cast<Widget*>(page));
    }
    int currentIndex() const
    {
        return property("currentIndex").toInt();
    }
    void setCurrentIndex(int index)
    {
        setProperty("currentIndex", index);
    }
    Widget* currentWidget() const
    {
        return widget(currentIndex());
    }
    void setCurrentWidget(Widget* page)
    {
        int i = indexOf(page);
        if (i >= 0)
            setCurrentIndex(i);
    }
    QString tabText(int index) const
    {
        return tabs().value(index);
    }
    void setTabText(int index, const QString& text);
    void setTabEnabled(int index, bool on)
    {
        request(QStringLiteral("setTabEnabled"), QVariantList {index, on});
    }
    void setTabToolTip(int index, const QString& text)
    {
        request(QStringLiteral("setTabToolTip"), QVariantList {index, text});
    }
    void setTabsClosable(bool on)
    {
        setProperty("tabsClosable", on);
    }
    void setDocumentMode(bool on)
    {
        setProperty("documentMode", on);
    }
    /// The generated setupUi: a page the file placed (no request).
    void addPage(Widget* page, const QString& title);

Q_SIGNALS:
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void tabBarClicked(int index);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;

private:
    QList<Widget*> _pages;
};

class GuiExport QStackedWidget : public Widget
{
    Q_OBJECT
public:
    explicit QStackedWidget(Widget* parent = nullptr);
    FW_MODEL("QStackedWidgetModel")

    int addWidget(Widget* page);
    int insertWidget(int index, Widget* page);
    void removeWidget(Widget* page);
    int count() const
    {
        return _pages.size();
    }
    Widget* widget(int index) const
    {
        return _pages.value(index);
    }
    int indexOf(const Widget* page) const
    {
        return _pages.indexOf(const_cast<Widget*>(page));
    }
    int currentIndex() const
    {
        return property("currentIndex").toInt();
    }
    void setCurrentIndex(int index)
    {
        setProperty("currentIndex", index);
    }
    Widget* currentWidget() const
    {
        return widget(currentIndex());
    }
    void setCurrentWidget(Widget* page)
    {
        int i = indexOf(page);
        if (i >= 0)
            setCurrentIndex(i);
    }
    void addPage(Widget* page)
    {
        _pages.append(page);
    }

Q_SIGNALS:
    void currentChanged(int index);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;

private:
    QList<Widget*> _pages;
};

class GuiExport QScrollArea : public Widget
{
    Q_OBJECT
public:
    explicit QScrollArea(Widget* parent = nullptr);
    FW_MODEL("QScrollAreaModel")

    void setWidget(Widget* content);
    Widget* widget() const
    {
        return _content;
    }
    void setWidgetResizable(bool on)
    {
        setProperty("widgetResizable", on);
    }
    void addPage(Widget* content)
    {
        _content = content;
    }

private:
    Widget* _content = nullptr;
};

class GuiExport QSplitter : public Widget
{
    Q_OBJECT
public:
    explicit QSplitter(Widget* parent = nullptr);
    FW_MODEL("QSplitterModel")

    void addWidget(Widget* pane);
    void insertWidget(int index, Widget* pane);
    int count() const
    {
        return _panes.size();
    }
    Widget* widget(int index) const
    {
        return _panes.value(index);
    }
    void setOrientation(int orientation)
    {
        setProperty("orientation", orientation);
    }
    int orientation() const
    {
        return property("orientation").toInt();
    }
    void setChildrenCollapsible(bool on)
    {
        setProperty("childrenCollapsible", on);
    }
    void setSizes(const QList<int>& sizes);
    QList<int> sizes() const;
    void setStretchFactor(int index, int factor)
    {
        request(QStringLiteral("setStretchFactor"), QVariantList {index, factor});
    }
    void addPage(Widget* pane)
    {
        _panes.append(pane);
    }

Q_SIGNALS:
    void splitterMoved(int pos, int index);

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;

private:
    QList<Widget*> _panes;
};

/// `Gui::FileChooser`: a path and a browse button.  The path is data
/// the host consumes; a DOM tier would supply its own picker.
class GuiExport FileChooser : public Widget
{
    Q_OBJECT
public:
    explicit FileChooser(Widget* parent = nullptr);
    FW_MODEL("FileChooserModel")

    enum Mode
    {
        File = 0,
        Directory = 1
    };

    QString fileName() const
    {
        return property("fileName").toString();
    }
    void setFileName(const QString& name)
    {
        setProperty("fileName", name);
    }
    void setMode(int mode)
    {
        setProperty("mode", mode);
    }
    int mode() const
    {
        return property("mode").toInt();
    }
    void setAcceptMode(int mode)
    {
        setProperty("acceptMode", mode);
    }
    void setFilter(const QString& filter)
    {
        setProperty("filter", filter);
    }
    QString filter() const
    {
        return property("filter").toString();
    }
    void setButtonText(const QString& text)
    {
        setProperty("buttonText", text);
    }

Q_SIGNALS:
    void fileNameChanged(const QString& name);
    void fileNameSelected(const QString& name);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

// ---- item views (G3b) ---------------------------------------------------------

/// One cell of a row.
struct GuiExport ItemCell
{
    QString text;
    QString icon;
    QString toolTip;
    QString statusTip;
    QString whatsThis;
    QVariant check;  ///< invalid: no check box; else Qt::CheckState as int
    QVariant flags;  ///< invalid: the view class's default
    QVariantList fg;
    QVariantList bg;
    bool bold = false;
    int align = 0;
    QVariantMap toMap() const;
    /// The keys present in `m` overwrite.
    void merge(const QVariantMap& m);
};

/// One row: cells, children (a tree), the row's own state.
struct GuiExport ItemRow
{
    int id = 0;
    int parent = 0;  ///< 0: top level
    QList<ItemCell> cells;
    QList<int> children;
    bool expanded = false;
    bool hidden = false;
    QVariant flags;  ///< a tree item's flags, for every cell
};

/// What the four Qt item views share: ROWS of cells, a tree of them,
/// with the columns, the selection and the current row as state.  The
/// rows are not in the bag -- a thousand-row tree resending itself on
/// every setText is what the bag must not carry -- but a tree this
/// object owns, changed by item OPS (`{"item": "insert" | "set" |
/// "row" | "remove" | "clear" | "sort", ...}`, the guest's wire shape,
/// the native setters below making the same ops) that reach the
/// backend through `Backend::itemsChanged` and the `itemsChanged`
/// signal; `snapshot()` is the tree as data for whoever needs it whole
/// (a DOM tier's first paint, a test).  Events come back with row ids:
/// `itemClicked`, `itemEdited` (the user edited a cell: merged here,
/// `itemChanged` fired), `itemExpanded`, ...
class GuiExport ItemView : public Widget
{
    Q_OBJECT
public:
    explicit ItemView(Widget* parent = nullptr);

    // -- the rows
    const ItemRow* row(int id) const;
    const QList<int>& topLevel() const
    {
        return _top;
    }
    int rowCount(int parentId = 0) const;
    /// The tree as nested maps (`id`, `cells`, `children`, ...).
    QVariantList snapshot() const;
    /// Apply an item op to the tree and pass it to the backend.  The
    /// guest's ops carry their own ids; a native insert mints them.
    void applyItemOp(const QVariantMap& op);

    // -- native setters (each an op)
    /// Insert a row of `texts` under `parentId` (0: top) at `index`
    /// (-1: append); the new id.
    int insertRow(int parentId, int index, const QStringList& texts);
    int addRow(const QStringList& texts, int parentId = 0)
    {
        return insertRow(parentId, -1, texts);
    }
    void removeRow(int id);
    void clearRows();
    QString text(int id, int column = 0) const;
    void setText(int id, int column, const QString& text);
    void setIcon(int id, int column, const QString& iconPath);
    void setToolTip(int id, int column, const QString& text);
    int checkState(int id, int column = 0) const;
    void setCheckState(int id, int column, int state);
    void setFlags(int id, int flags);
    void setExpanded(int id, bool on);
    bool isExpanded(int id) const;
    void setRowHidden(int id, bool on);

    // -- the state
    QStringList columns() const
    {
        return property("columns").toStringList();
    }
    void setColumns(const QStringList& labels);
    int columnCount() const;
    void setColumnCount(int n)
    {
        setProperty("columnCount", n);
    }
    QList<int> selection() const;
    void setSelection(const QList<int>& ids);
    void select(int id, bool on = true);
    int currentId() const
    {
        return property("currentId").toInt();
    }
    int currentColumn() const
    {
        return property("currentColumn").toInt();
    }
    void setCurrent(int id, int column = 0);
    void setSelectionMode(int mode)
    {
        setProperty("selectionMode", mode);
    }
    void setSortingEnabled(bool on)
    {
        setProperty("sortingEnabled", on);
    }
    void setHeaderHidden(bool on)
    {
        setProperty("headerHidden", on);
    }
    void setRootIsDecorated(bool on)
    {
        setProperty("rootIsDecorated", on);
    }
    void setColumnWidth(int column, int width);
    void expandAll()
    {
        request(QStringLiteral("expandAll"));
    }
    void collapseAll()
    {
        request(QStringLiteral("collapseAll"));
    }
    void resizeColumnToContents(int column)
    {
        request(QStringLiteral("resizeColumnToContents"), QVariantList {column});
    }
    void scrollTo(int id)
    {
        request(QStringLiteral("scrollTo"), QVariantList {id});
    }
    void editItem(int id, int column = 0)
    {
        request(QStringLiteral("edit"), QVariantList {id, column});
    }

Q_SIGNALS:
    void itemsChanged(const QVariantMap& op);
    void itemClicked(int id, int column);
    void itemDoubleClicked(int id, int column);
    void itemActivated(int id, int column);
    void itemPressed(int id, int column);
    void itemChanged(int id, int column);
    void itemSelectionChanged();
    void currentItemChanged(int id, int previous);
    void itemExpanded(int id);
    void itemCollapsed(int id);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;

private:
    void insertRows(int parentId, int index, const QVariantList& rows);
    void eraseRow(int id);
    QVariantMap rowMap(const ItemRow& r) const;
    void emitItemOp(const QVariantMap& op);

    QHash<int, ItemRow> _rows;
    QList<int> _top;
    int _nextId = 1;
    int _previousCurrent = 0;
};

class GuiExport QListWidget : public ItemView
{
    Q_OBJECT
public:
    explicit QListWidget(Widget* parent = nullptr);
    FW_MODEL("QListWidgetModel")
    int addItem(const QString& text)
    {
        return addRow(QStringList {text});
    }
    int count() const
    {
        return rowCount();
    }
    int currentRow() const
    {
        return topLevel().indexOf(currentId());
    }
    void setCurrentRow(int index)
    {
        setCurrent(topLevel().value(index));
    }
};

class GuiExport QTreeWidget : public ItemView
{
    Q_OBJECT
public:
    explicit QTreeWidget(Widget* parent = nullptr);
    FW_MODEL("QTreeWidgetModel")
    void setHeaderLabels(const QStringList& labels)
    {
        setColumns(labels);
    }
    int topLevelItemCount() const
    {
        return rowCount();
    }
};

class GuiExport QTreeView : public ItemView
{
    Q_OBJECT
public:
    explicit QTreeView(Widget* parent = nullptr);
    FW_MODEL("QTreeViewModel")
};

class GuiExport QTableWidget : public ItemView
{
    Q_OBJECT
public:
    explicit QTableWidget(Widget* parent = nullptr);
    FW_MODEL("QTableWidgetModel")
    void setHorizontalHeaderLabels(const QStringList& labels)
    {
        setColumns(labels);
    }
    void setVerticalHeaderLabels(const QStringList& labels)
    {
        setProperty("rowLabels", labels);
    }
    void setRowCount(int n);
    int currentRow() const
    {
        return topLevel().indexOf(currentId());
    }
    void setCurrentCell(int row, int column)
    {
        setCurrent(topLevel().value(row), column);
    }
    void setItemText(int row, int column, const QString& text);
    QString itemText(int row, int column) const
    {
        return text(topLevel().value(row), column);
    }
};

class GuiExport QListView : public ItemView
{
    Q_OBJECT
public:
    explicit QListView(Widget* parent = nullptr);
    FW_MODEL("QListViewModel")
};

class GuiExport QTableView : public ItemView
{
    Q_OBJECT
public:
    explicit QTableView(Widget* parent = nullptr);
    FW_MODEL("QTableViewModel")
};

// ---- the form loaded from a .ui file ------------------------------------------

/// The root a form has: the file it came from and its named widgets.
/// A backend realizes it by loading the same file through its own
/// loader (uic on the Qt side: the layout exact, the strings
/// translated, FreeCAD's widgets real) and binding each named child
/// to the model of that name.  A QDialog as well, for the files whose
/// root is one; a QWidget root never calls that half.
// ---- actions, tool bars, menus (G3c, docs/Sandbox.md 7.11) --------------------

/// A `QAction`: not a widget, but the same bag mechanics (text, icon,
/// checkable, checked, enabled, visible, toolTip, shortcut), realized
/// by a backend on the widget, bar or menu that carries it.
class GuiExport QAction : public Widget
{
    Q_OBJECT
public:
    explicit QAction(Widget* parent = nullptr);
    QAction(const QString& text, Widget* parent = nullptr);
    FW_MODEL("QActionModel")

    void setText(const QString& text)
    {
        setProperty("text", text);
    }
    QString text() const
    {
        return property("text").toString();
    }
    void setIcon(const QString& path)
    {
        setProperty("icon", path);
    }
    QString icon() const
    {
        return property("icon").toString();
    }
    void setCheckable(bool on)
    {
        setProperty("checkable", on);
    }
    bool isCheckable() const
    {
        return property("checkable").toBool();
    }
    void setChecked(bool on)
    {
        setProperty("checked", on);
    }
    bool isChecked() const
    {
        return property("checked").toBool();
    }
    void setShortcut(const QString& keys)
    {
        setProperty("shortcut", keys);
    }
    QString shortcut() const
    {
        return property("shortcut").toString();
    }
    void setSeparator(bool on)
    {
        setProperty("separator", on);
    }
    bool isSeparator() const
    {
        return property("separator").toBool();
    }
    /// A host command's own action (docs/Sandbox.md 7.15): the backend
    /// binds this model to `Command::getAction()` -- the member `index`
    /// of an action group -- instead of making an action of its own.
    void setCommand(const QString& name, int index = 0)
    {
        setProperty("command", name);
        setProperty("commandIndex", index);
    }
    QString command() const
    {
        return property("command").toString();
    }
    int commandIndex() const
    {
        return property("commandIndex").toInt();
    }
    /// A group's members (refs to `QAction` models), in order; the
    /// index of the one the button shows (-1 none); exclusive;
    /// drop-down (docs/Sandbox.md 7.18, the tool bar mirror).
    QVariantList members() const
    {
        return property("members").toList();
    }
    int defaultAction() const
    {
        return property("defaultAction").toInt();
    }
    bool isExclusive() const
    {
        return property("exclusive").toBool();
    }
    bool isDropDown() const
    {
        return property("dropDown").toBool();
    }
    /// A member's own command name ("" for a plain member action).
    QString memberCommand() const
    {
        return property("memberCommand").toString();
    }
    /// A programmatic trigger: toggles a checkable, fires `triggered`.
    void trigger();
    /// Ask the backend to trigger the real action.
    void activate()
    {
        request(QStringLiteral("trigger"));
    }

Q_SIGNALS:
    void triggered(bool checked);
    void toggled(bool checked);
    void hovered();

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

/// A tool bar: its content is a `Layout::Bar` (widgets, actions and
/// separators in order); `toggleViewAction` is the `QAction` model the
/// backend binds to the real bar's own.
class GuiExport QToolBar : public Widget
{
    Q_OBJECT
public:
    explicit QToolBar(Widget* parent = nullptr);
    QToolBar(const QString& title, Widget* parent = nullptr);
    FW_MODEL("QToolBarModel")

    /// The content layout (made on first use).
    Layout* bar();
    void addWidget(Widget* widget);
    void addAction(Widget* action);
    void addSeparator();
    void clear();
    QList<Widget*> actions() const;
    /// The action that shows and hides the bar (made on first use; a
    /// backend binds it to the real bar's `toggleViewAction()`).
    QAction* toggleViewAction();
    void setIconSize(int px)
    {
        setProperty("iconSize", px);
    }
    void setToolButtonStyle(int style)
    {
        setProperty("toolButtonStyle", style);
    }
    void setMovable(bool on)
    {
        setProperty("movable", on);
    }
    void setFloatable(bool on)
    {
        setProperty("floatable", on);
    }
    void setOrientation(int orientation)
    {
        setProperty("orientation", orientation);
    }
    /// Where the desktop shows the bar (docs/Sandbox.md 7.18).
    QString area() const
    {
        return property("area").toString();
    }

Q_SIGNALS:
    void actionTriggered(Gui::Fw::Widget* action);

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

/// A menu: its content is a `Layout::Bar` (actions, separators and
/// sub-menus, a sub-menu a `QMenu` widget item); `exec` is the
/// backend's business (a nested loop), the chosen action comes back
/// as `triggered`.
class GuiExport QMenu : public Widget
{
    Q_OBJECT
public:
    explicit QMenu(Widget* parent = nullptr);
    QMenu(const QString& title, Widget* parent = nullptr);
    FW_MODEL("QMenuModel")

    Layout* bar();
    void addAction(Widget* action);
    void addMenu(QMenu* menu);
    void addSeparator();
    void clear();
    QList<Widget*> actions() const;
    void setTitle(const QString& title)
    {
        setProperty("title", title);
    }
    QString title() const
    {
        return property("title").toString();
    }
    void setIcon(const QString& path)
    {
        setProperty("icon", path);
    }

Q_SIGNALS:
    void triggered(Gui::Fw::Widget* action);
    void aboutToShow();

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport UiForm : public QDialog
{
    Q_OBJECT
public:
    explicit UiForm(Widget* parent = nullptr);
    FW_MODEL("UiFormModel")

    QString uiFile() const
    {
        return _uiFile;
    }
    void setUiFile(const QString& path)
    {
        _uiFile = path;
    }
    /// Register a named child (the generated setupUi does, the guest's
    /// `widgets` map does).
    void addNamed(const QString& name, Widget* widget);
    Widget* named(const QString& name) const
    {
        return _named.value(name);
    }
    QStringList names() const
    {
        return _named.keys();
    }
    const QMap<QString, Widget*>& namedWidgets() const
    {
        return _named;
    }

private:
    QString _uiFile;
    QMap<QString, Widget*> _named;
};

/// A dock widget (docs/Sandbox.md 7.15): `widget` is the content model.
/// A backend makes the real dock through the main window's dock manager
/// (a first-class panel: in the Panels menu, in the saved layout), writes
/// back `area`, `floating`, `visible` and the geometry, and reports a
/// close as the `closed` event.
class GuiExport QDockWidget : public Widget
{
    Q_OBJECT
public:
    explicit QDockWidget(Widget* parent = nullptr);
    FW_MODEL("QDockWidgetModel")

    void setWidget(Widget* widget)
    {
        setProperty("widget", QVariant::fromValue<QObject*>(widget));
    }
    Widget* widget() const
    {
        return qobject_cast<Widget*>(property("widget").value<QObject*>());
    }
    void setFloating(bool on)
    {
        setProperty("floating", on);
    }
    bool isFloating() const
    {
        return property("floating").toBool();
    }
    /// The Qt::DockWidgetArea the backend reports.
    int area() const
    {
        return property("area").toInt();
    }
    void setGeometry(int x, int y, int w, int h)
    {
        request(QStringLiteral("setGeometry"), QVariantList {x, y, w, h});
    }
    void close()
    {
        request(QStringLiteral("close"));
    }
    int x() const
    {
        return property("x").toInt();
    }
    int y() const
    {
        return property("y").toInt();
    }
    int width() const
    {
        return property("width").toInt();
    }
    int height() const
    {
        return property("height").toInt();
    }
    /// The action that shows and hides the dock (made on first use; a
    /// backend binds it to the real dock's `toggleViewAction()`).
    QAction* toggleViewAction();

Q_SIGNALS:
    void dockLocationChanged(int area);
    void visibilityChanged(bool visible);
    void topLevelChanged(bool floating);
    void closed();

protected:
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

#undef FW_MODEL

// ---- the factory --------------------------------------------------------------

/// A model for a Qt class name as a .ui file spells it ("QLabel",
/// "Gui::PrefCheckBox") or a guest model name ("QLabelModel"): the
/// class the name maps to, its `qtClass` the name given.  An unknown
/// class is a plain Widget (never nothing: the gap stays visible).
GuiExport Widget* createWidget(const QString& className, Widget* parent = nullptr);
/// The Qt class names the factory knows.
GuiExport QStringList knownClasses();
/// A layout for a Qt layout class name ("QGridLayout"); a VBox if unknown.
GuiExport Layout* createLayout(const QString& className, Widget* owner = nullptr);

}  // namespace Fw
}  // namespace Gui

#endif  // GUI_FW_WIDGETS_H
