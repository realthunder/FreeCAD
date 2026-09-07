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

#ifndef GUI_FW_QTVIEW_H
#define GUI_FW_QTVIEW_H

/* The host widget layer (docs/Sandbox.md 7.12): the Qt backend.
 *
 * One View per realized model: the real Qt widget of the model's
 * `qtClass` -- BUILT (Qt's class by name, FreeCAD's own through the
 * widget factory) or BOUND to a child uic made when the model is a
 * form loaded from a .ui file: the same file through the host's
 * UiLoader, the layout exact, the strings translated, the `Gui::Pref*`
 * and quantity widgets real, each named child adopted as the rendering
 * of the model of that name.  Binding reads the widget's values back
 * into the bag silently (what the DOM tier will show as the file's
 * text), then pushes the properties something SET.  A later write to
 * the bag is applied key by key: Qt's own `setProperty` where the name
 * is a Q_PROPERTY, a handler where it is not (a combo's items, an icon
 * path, a color as four floats, the range before the value).  What the
 * user does in the widget goes into the bag from `Source::Backend`, or
 * out as an event (`clicked`, `editingFinished`, ...).
 *
 * This is the port of the Python Qt manager of G3a
 * (src/Ext/freecad/widgets/qt.py, the Qt-shaped half), which it
 * retires for the `freecad.widgets` models.
 */

#include <QHash>
#include <QPointer>
#include <functional>
#include <QSet>
#include <QStringList>

#include "FwCore.h"

#include <memory>

QT_BEGIN_NAMESPACE
class QWidget;
class QLayout;
class QModelIndex;
class QAction;
QT_END_NAMESPACE

namespace Gui
{
namespace FwQt
{

class GuiExport View : public QObject, public Fw::Backend
{
    Q_OBJECT
public:
    ~View() override;

    /// The view rendering `model`, or nullptr when it is not realized.
    static View* of(const Fw::Widget* model);
    /// Build the Qt widget of `model` under `parent` (a form loads its
    /// .ui file and binds its named children).  Throws Base::Exception
    /// when a form cannot be loaded.  Returns the view (the model's, if
    /// it was realized already).
    static View* build(Fw::Widget* model, QWidget* parent);
    /// Adopt `widget` as the rendering of `model`.
    static View* bind(Fw::Widget* model, QWidget* widget);

    Fw::Widget* model() const
    {
        return _model;
    }
    QWidget* widget() const
    {
        return _widget;
    }
    /// Bound to a widget something else made (uic's), or built here.
    bool isBound() const
    {
        return _bound;
    }
    /// Detach from the widget and the model; the view deletes itself.
    /// `deleteWidget`: the widget goes too (not when a dialog owns it).
    void release(bool deleteWidget);
    /// Apply the bag under `keys` to the widget.
    void apply(const QStringList& keys);

    // Fw::Backend: the model's direct channel (a QSignalBlocker on the
    // model does not reach it)
    void propertiesWritten(const QStringList& names, int source) override;
    void requested(const QString& name, const QVariantList& args) override;
    void layoutChanged(const QVariantMap& op) override;
    void itemsChanged(const QVariantMap& op) override;
    /// An item view's rows, applied to the real view through Qt's
    /// abstract item model -- one path for a tree widget, a list, a
    /// table and a tree view over a QStandardItemModel made here.
    void applyItemOp(const QVariantMap& op);
    /// The real index of a row id (invalid if none).
    QModelIndex indexOf(int id, int column = 0) const;
    /// The row id of a real index (0 if none).
    int idOf(const QModelIndex& index) const;

private:
    friend class EventRelay;
    friend class DockRelay;
    View(Fw::Widget* model, QWidget* widget, bool bound);
    void connectWidget();
    void readBack();
    void applyOne(const QString& key, const QVariant& value);
    void send(const QVariantMap& state);
    void event(const QString& name, const QVariantList& args = QVariantList());
    void onRequest(const QString& name, const QVariantList& args);
    void onLayoutOp(const QVariantMap& op);
    QWidget* widgetOf(const QVariant& ref, QWidget* parent);
    static View* buildForm(Fw::Widget* model, QWidget* parent);
    // containers: pages a model holds before it is realized
    void initContainers();
    // code-built layouts (G3c): the model's Layout tree realized under
    // the widget; a bar (a tool bar's or a menu's content) filled
    void initLayout();
    QLayout* makeLayout(Fw::Layout* lay, QWidget* owner);
    void fillBar(Fw::Layout* lay, QWidget* bar);
    void barOp(const QVariantMap& op);
    void applyActions(const QVariantList& entries);
    void applyWatchEvents(const QVariantList& types);
    QAction* actionOf(const QVariant& ref, QObject* parent);
    void showInLayout(QWidget* child, const QVariant& modelRef);
    // item views
    struct Items;
    void initItems();
    void readBackItems();
    void insertItemRows(int parentId, int index, const QVariantList& rows);
    void applyCell(const QModelIndex& index, const QVariantMap& cell);
    void setItemFlags(const QModelIndex& index, int flags, bool wholeRow);
    void ensureColumns(int count, const QModelIndex& parent);
    void applySelection();
    void applyCurrent();
    void applyCellTypes();
    void headerCall(const QString& which, const QString& method, const QVariantList& args);
    bool onItemRequest(const QString& name, const QVariantList& args);

    Fw::Widget* _model;
    QPointer<QWidget> _widget;
    bool _bound;
    bool _applying = false;
    bool _eventEaten = false;   // the `eventDone` answer to a relayed event
    QPointer<QObject> _relay;   // the event filter relaying `watchEvents`
    QSet<QObject*> _added;      // the actions added to the widget
    QList<QPointer<View>> _children;  // a form's bound children
    std::unique_ptr<Items> _items;
};

/// The real QAction rendering an action model (a `Fw::QAction`), made
/// under `parent` if there is none yet; nullptr for a model that is not
/// an action.  The action dies with its model.
GuiExport QAction* realizeAction(Fw::Widget* model, QObject* parent);
/// The real action of a model, or nullptr when it is not realized.
GuiExport QAction* actionWidgetOf(const Fw::Widget* model);
/// Adopt `action` (a bar's toggle-view action, a widget's own) as the
/// rendering of the model.
GuiExport void bindAction(Fw::Widget* model, QAction* action);
/// Who may WRITE to a host command's action bound through a model's
/// `command` (docs/Sandbox.md 7.15): the filter answers by command
/// name; unset, every write lands.  Reads and triggers always work.
GuiExport void setCommandWriteFilter(std::function<bool(const QString&)> filter);
/// A `QToolButton` model with `forAction` set, whose parent is a
/// realized tool bar: bind it to the bar's button for that action
/// (`QToolBar::widgetForAction`).  True when bound (or bound already).
GuiExport bool bindActionButton(Fw::Widget* button);
/// The model a real action renders, or nullptr.
GuiExport Fw::Widget* modelOfAction(const QAction* action);

/// The real widget of a Qt class name: FreeCAD's own through the widget
/// factory, Qt's by name, else a QWidget (never nullptr).
GuiExport QWidget* makeQtWidget(const QString& className, QWidget* parent);
/// `View::build(model, parent)->widget()`.
GuiExport QWidget* realize(Fw::Widget* model, QWidget* parent = nullptr);
/// The real widget rendering `model`, or nullptr when it is not realized
/// -- for the Qt-only calls a ported panel keeps (an event filter, a
/// button group, a blink target).
GuiExport QWidget* widgetOf(const Fw::Widget* model);
/// Realize a root model as a window of its own: a QDialog root under
/// the main window (a dialog is a window whatever its parent), any other
/// with none.  The widget dies with the model.  The existing rendering
/// if there is one.
GuiExport QWidget* realizeTopLevel(Fw::Widget* root);
/// Run a dialog root modally (a nested event loop); the dialog code.
/// Throws Base::TypeError when the root is not a QDialog.
GuiExport int execDialog(Fw::Widget* root);

}  // namespace FwQt
}  // namespace Gui

#endif  // GUI_FW_QTVIEW_H
