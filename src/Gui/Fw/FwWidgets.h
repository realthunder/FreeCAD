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
#include <QStringList>

#include <Base/Quantity.h>

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
    /// The text for `value` in the unit schema at the user's decimals.
    QString formatValue(double value) const;

Q_SIGNALS:
    void valueChanged(double value);
    void parseError(const QString& text);

protected:
    void propertyDidChange(const QString& name, const QVariant& value) override;
    void dispatchEvent(const QString& name, const QVariantList& args) override;
};

class GuiExport QuantitySpinBox : public InputField
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

// ---- the form loaded from a .ui file ------------------------------------------

/// The root a form has: the file it came from and its named widgets.
/// A backend realizes it by loading the same file through its own
/// loader (uic on the Qt side: the layout exact, the strings
/// translated, FreeCAD's widgets real) and binding each named child
/// to the model of that name.
class GuiExport UiForm : public Widget
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
