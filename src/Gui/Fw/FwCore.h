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

#ifndef GUI_FW_CORE_H
#define GUI_FW_CORE_H

/* The host widget layer (docs/Sandbox.md 7.12): the core.
 *
 * A form is a tree of MODELS, not of Qt widgets.  Each model is a
 * QObject in this namespace with Qt's class and method names kept --
 * `Fw::QLabel::setText`, `Fw::QSpinBox::valueChanged(int)` -- whose
 * state is a variant map keyed by Qt's property names (the bag), plus
 * the set of properties something SET (touched: what a backend must
 * push into a widget it realizes; the rest keep the widget's own
 * defaults, or a .ui file's, or uic's translation), and moc'd signals
 * with Qt's names and arguments.  A backend -- the Qt view on the
 * desktop (FwQtView.h), the DOM walker in the browser later -- is a
 * consumer of the bag and nothing else; whatever a backend needs must
 * be in the bag.  That is the one design rule.
 *
 * The sandbox guest's `freecad.widgets` models are these classes with a
 * comm each (FwStore.h); a native task panel is these classes with no
 * comm, its form a `Ui_X::setupUi` generated from the .ui file by
 * src/Tools/fwuic.py.  Both render through the same backend.
 *
 * Three writers exist: native code through the typed setters, a
 * backend reporting what the user did, and the guest through its comm.
 * `propertiesChanged` names the keys written, equal or not (a text
 * something set that a file already had still overrides uic's
 * translation of it); the typed value signals (`valueChanged`,
 * `toggled`, `textChanged`, ...) fire only when a value changed, as
 * Qt's do, and whichever side changed it.
 *
 * Nothing here includes QtWidgets, so a translation unit porting a
 * dialog onto these classes never sees `::QLabel` and there is no
 * ambiguity to qualify away.
 */

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <FCGlobal.h>

namespace Gui
{
namespace Fw
{

class Layout;
class Widget;

/// What a backend hears from a model DIRECTLY, not through the moc'd
/// signals below.  A native panel wraps a field in `QSignalBlocker`
/// while it fills the form (as it did the widget), and that must
/// silence the typed signals its slots hang on -- not the backend's
/// mirror, or the value never reaches the widget.  One backend per
/// model; the signals stay for everything else (the guest store's
/// sink, tests).
class GuiExport Backend
{
public:
    virtual ~Backend() = default;
    virtual void propertiesWritten(const QStringList& names, int source) = 0;
    virtual void requested(const QString& name, const QVariantList& args) = 0;
    virtual void layoutChanged(const QVariantMap& op) = 0;
    /// An item view's rows changed (see `Fw::ItemView::applyItemOp`);
    /// a backend without item views ignores it.
    virtual void itemsChanged(const QVariantMap& op)
    {
        Q_UNUSED(op)
    }
};

/// Who wrote a property.
enum class Source
{
    Native = 0,   ///< a typed setter, or the generated setupUi
    Backend = 1,  ///< a view reporting what the user did
    Guest = 2     ///< the sandbox guest's comm
};

class GuiExport Widget : public QObject
{
    Q_OBJECT
public:
    explicit Widget(Widget* parent = nullptr);
    ~Widget() override;

    // -- identity ---------------------------------------------------------

    /// The Qt class this object stands for, as a .ui file or a
    /// `UiLoader().createWidget` names it: "QLabel", "Gui::PrefCheckBox".
    /// A backend builds THAT (the preference widgets stay native); the
    /// model class is the base the name maps to.
    QString qtClass() const
    {
        return _qtClass;
    }
    void setQtClass(const QString& name);
    /// The guest-side model name of this class ("QLabelModel").
    virtual QString modelName() const
    {
        return QStringLiteral("QWidgetModel");
    }

    // QObject's objectName is the bag's "objectName" too.
    void setObjectName(const QString& name);
    using QObject::objectName;

    // -- the bag ----------------------------------------------------------

    /// Whether `name` is a property of this class (a bag key).
    bool has(const QString& name) const
    {
        return _props.contains(name);
    }
    /// The value under `name`; an invalid variant when there is none.
    /// Hides QObject::property, which would read a Q_PROPERTY of this
    /// QObject instead of the bag -- the bag IS the object's state.
    QVariant property(const char* name) const;
    QVariant property(const QString& name) const;
    /// Write one property as native code does: touched, signalled.
    /// True when `name` is a bag key; a key this class does not have is
    /// kept as a dynamic property (Qt's `setProperty` on an unknown name
    /// does the same) and false is returned.
    bool setProperty(const char* name, const QVariant& value);
    bool setProperty(const QString& name, const QVariant& value);
    /// Write several at once (one `propertiesChanged`).  Unknown keys are
    /// dropped.  Only `Source::Native` marks touched: a backend reports
    /// what the user did, the guest syncs its own list (`setTouched`).
    void setProperties(const QVariantMap& values, Source source = Source::Native);
    /// Write silently: no touched mark, no signal.  For a .ui file's
    /// values (the generated setupUi, the guest's loader) and for what a
    /// backend reads back from the widget it bound (uic's translated
    /// strings), so the bag mirrors the widget without claiming to have
    /// set anything.
    void setInitial(const QString& name, const QVariant& value);
    const QVariantMap& properties() const
    {
        return _props;
    }
    QStringList propertyNames() const
    {
        return _props.keys();
    }
    const QSet<QString>& touched() const
    {
        return _touched;
    }
    bool isTouched(const QString& name) const
    {
        return _touched.contains(name);
    }
    /// Replace the touched set (the guest syncs its own).
    void setTouched(const QStringList& names);
    void markTouched(const QString& name)
    {
        _touched.insert(name);
    }
    /// Dynamic properties: what `setProperty` kept for an unknown name.
    QStringList dynamicPropertyNames() const
    {
        return _dynamic.keys();
    }

    // -- the tree ---------------------------------------------------------

    Widget* parentWidget() const;
    /// Re-parent (hides QObject::setParent): the model tree, and a
    /// request to the backend to re-parent the realized widget too
    /// (nullptr makes it a hidden top-level, as Qt does).
    void setParent(Widget* parent);
    QList<Widget*> childWidgets() const;
    /// The child widget of that object name, depth first; nullptr if none.
    Widget* findChildWidget(const QString& name) const;
    Layout* layout() const
    {
        return _layout;
    }
    void setLayout(Layout* layout);
    /// The layout of that name under this widget (nested ones included).
    Layout* findLayout(const QString& name) const;

    // -- QWidget's API subset (the bag under Qt's names) -------------------

    void show()
    {
        setProperty("visible", true);
    }
    void hide()
    {
        setProperty("visible", false);
    }
    void setVisible(bool on)
    {
        setProperty("visible", on);
    }
    void setHidden(bool on)
    {
        setProperty("visible", !on);
    }
    bool isVisible() const
    {
        return property("visible").toBool();
    }
    bool isHidden() const
    {
        return !isVisible();
    }
    void setEnabled(bool on)
    {
        setProperty("enabled", on);
    }
    void setDisabled(bool on)
    {
        setProperty("enabled", !on);
    }
    bool isEnabled() const
    {
        return property("enabled").toBool();
    }
    void setToolTip(const QString& text)
    {
        setProperty("toolTip", text);
    }
    QString toolTip() const
    {
        return property("toolTip").toString();
    }
    void setStatusTip(const QString& text)
    {
        setProperty("statusTip", text);
    }
    QString statusTip() const
    {
        return property("statusTip").toString();
    }
    void setWhatsThis(const QString& text)
    {
        setProperty("whatsThis", text);
    }
    void setWindowTitle(const QString& text)
    {
        setProperty("windowTitle", text);
    }
    QString windowTitle() const
    {
        return property("windowTitle").toString();
    }
    /// An icon is a path (a resource or a file), the form the DOM tier
    /// can carry; the backend loads it.
    void setWindowIcon(const QString& path)
    {
        setProperty("windowIcon", path);
    }
    QString windowIcon() const
    {
        return property("windowIcon").toString();
    }
    void setStyleSheet(const QString& css)
    {
        setProperty("styleSheet", css);
    }
    QString styleSheet() const
    {
        return property("styleSheet").toString();
    }
    void setMinimumWidth(int w)
    {
        setProperty("minimumWidth", w);
    }
    void setMinimumHeight(int h)
    {
        setProperty("minimumHeight", h);
    }
    void setMaximumWidth(int w)
    {
        setProperty("maximumWidth", w);
    }
    void setMaximumHeight(int h)
    {
        setProperty("maximumHeight", h);
    }
    void setFixedWidth(int w);
    void setFixedHeight(int h);
    void setFixedSize(int w, int h)
    {
        setFixedWidth(w);
        setFixedHeight(h);
    }
    void setMinimumSize(int w, int h);
    void setMaximumSize(int w, int h);
    int minimumWidth() const
    {
        return property("minimumWidth").toInt();
    }
    int minimumHeight() const
    {
        return property("minimumHeight").toInt();
    }
    int maximumWidth() const
    {
        return property("maximumWidth").toInt();
    }
    int maximumHeight() const
    {
        return property("maximumHeight").toInt();
    }
    /// A preference widget's entry and group (`Gui::Pref*`): two bag
    /// keys; the backend's real widget reads and writes the store.
    void setPrefEntry(const QString& entry)
    {
        setProperty("prefEntry", entry);
    }
    void setPrefPath(const QString& path)
    {
        setProperty("prefPath", path);
    }
    QString prefEntry() const
    {
        return property("prefEntry").toString();
    }
    QString prefPath() const
    {
        return property("prefPath").toString();
    }
    /// Focus is a request to the backend, not state.  Invokable: a panel
    /// queues it by name (`QMetaObject::invokeMethod(edit, "setFocus", ...)`).
    Q_INVOKABLE void setFocus()
    {
        request(QStringLiteral("setFocus"));
    }

    /// The backend rendering this model (see `Backend`); nullptr if none.
    Backend* backend() const
    {
        return _backend;
    }
    void setBackend(Backend* backend)
    {
        _backend = backend;
    }

    // -- the two event directions ------------------------------------------

    /// Ask the backend to do something to the realized widget that is
    /// not state: `setFocus`, `selectAll`, `setSelection`,
    /// `setCursorPosition`, `setParent`.
    void request(const QString& name, const QVariantList& args = QVariantList());
    /// The backend (or native code: `click()`) reporting an event the
    /// widget emits: `clicked`, `pressed`, `released`, `returnPressed`,
    /// `editingFinished`, `textEdited`, `activated`, `linkActivated`.
    /// Fires the typed signal of that name and `eventEmitted`.
    void notify(const QString& name, const QVariantList& args = QVariantList());
    /// Relay a layout op performed elsewhere (the guest's own layout
    /// object) to the backend as `layoutChanged`.
    void forwardLayoutOp(const QVariantMap& op)
    {
        emitLayoutOp(op);
    }

Q_SIGNALS:
    /// The keys written (equal or not) and who wrote them (a `Source`).
    void propertiesChanged(const QStringList& names, int source);
    /// A request to the backend (see `request`).
    void requested(const QString& name, const QVariantList& args);
    /// An event the widget emitted (see `notify`).
    void eventEmitted(const QString& name, const QVariantList& args);
    /// A layout of this widget was mutated: `{"layout": name, "op": ...,
    /// "widget": QObject*, "args": [...], "index": i, "sublayout": name}`.
    void layoutChanged(const QVariantMap& op);

protected:
    /// A subclass declares its bag keys with Qt's defaults.
    void declare(const QString& name, const QVariant& defaultValue);
    /// A value actually changed (Qt semantics): fire the typed signal.
    virtual void propertyDidChange(const QString& name, const QVariant& value);
    /// An event arrived (see `notify`): fire the typed signal.
    virtual void dispatchEvent(const QString& name, const QVariantList& args);
    /// Coerce a value to the declared type of `name` (an int key stays
    /// int when a double arrives from JSON).
    QVariant coerce(const QString& name, const QVariant& value) const;

private:
    friend class Layout;
    void emitLayoutOp(const QVariantMap& op)
    {
        if (_backend)
            _backend->layoutChanged(op);
        Q_EMIT layoutChanged(op);
    }

    Backend* _backend = nullptr;
    QString _qtClass;
    QVariantMap _props;
    QVariantMap _dynamic;
    QSet<QString> _touched;
    Layout* _layout = nullptr;
};

/// What a layout holds at a position: a widget, a nested layout, a
/// spacer or a stretch.
struct GuiExport LayoutItem
{
    Widget* widget = nullptr;
    Layout* layout = nullptr;
    bool spacer = false;
    int stretch = 0;
    int spacing = 0;
    /// grid: row, column, rowSpan, columnSpan; form: row, role
    QVariantList position;
    bool isEmpty() const
    {
        return !widget && !layout;
    }
};

/// A layout is not a model: it is an object on its widget holding the
/// items in order, and every mutation is a layout op on the OWNING
/// widget (`Widget::layoutChanged`) that a backend applies to the real
/// layout of that name (a .ui file names its layouts; uic keeps the
/// names).  A layout with no name reaches no backend layout.
class GuiExport Layout : public QObject
{
    Q_OBJECT
public:
    enum Kind
    {
        Box,
        VBox,
        HBox,
        Grid,
        Form
    };
    explicit Layout(Kind kind, Widget* owner = nullptr);
    ~Layout() override;

    Kind kind() const
    {
        return _kind;
    }
    /// The Qt class name of this kind ("QGridLayout").
    QString className() const;
    /// The widget this layout sits on, through its parent layouts.
    Widget* parentWidget() const;
    Layout* parentLayout() const
    {
        return _parentLayout;
    }

    int count() const
    {
        return _items.size();
    }
    const LayoutItem* itemAt(int index) const;
    /// Remove and return the item at `index` (invalid when out of range).
    LayoutItem takeAt(int index);
    int indexOf(const Widget* widget) const;
    int indexOf(const Layout* layout) const;
    void removeWidget(Widget* widget);
    void addWidget(Widget* widget, const QVariantList& position = QVariantList());
    void addWidget(Widget* widget, int row, int column, int rowSpan = 1, int columnSpan = 1)
    {
        addWidget(widget, QVariantList {row, column, rowSpan, columnSpan});
    }
    void insertWidget(int index, Widget* widget, const QVariantList& position = QVariantList());
    void addLayout(Layout* layout, const QVariantList& position = QVariantList());
    void addLayout(Layout* layout, int row, int column, int rowSpan = 1, int columnSpan = 1)
    {
        addLayout(layout, QVariantList {row, column, rowSpan, columnSpan});
    }
    void addStretch(int stretch = 0);
    void addSpacing(int size);
    /// A spacer item: a `<spacer>` of the .ui file.
    void addSpacer(int w, int h, const QVariantList& position = QVariantList());
    void setContentsMargins(int left, int top, int right, int bottom);
    void setSpacing(int spacing);
    // QFormLayout
    void addRow(Widget* label, Widget* field);
    void addRow(Widget* field);
    int rowCount() const;
    int columnCount() const;
    /// Depth first: this layout and every nested one.
    QList<Layout*> nested() const;

private:
    void notify(const QString& op, QVariantMap fields = QVariantMap());
    void attach(Widget* widget);

    Kind _kind;
    QList<LayoutItem> _items;
    Layout* _parentLayout = nullptr;
    friend class Widget;
};

}  // namespace Fw
}  // namespace Gui

Q_DECLARE_METATYPE(Gui::Fw::Widget*)

#endif  // GUI_FW_CORE_H
