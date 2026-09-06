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
#include <QStringList>

#include "FwCore.h"

QT_BEGIN_NAMESPACE
class QWidget;
class QLayout;
QT_END_NAMESPACE

namespace Gui
{
namespace FwQt
{

class GuiExport View : public QObject
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

private:
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

    Fw::Widget* _model;
    QPointer<QWidget> _widget;
    bool _bound;
    bool _applying = false;
    QList<QPointer<View>> _children;  // a form's bound children
};

/// The real widget of a Qt class name: FreeCAD's own through the widget
/// factory, Qt's by name, else a QWidget (never nullptr).
GuiExport QWidget* makeQtWidget(const QString& className, QWidget* parent);
/// `View::build(model, parent)->widget()`.
GuiExport QWidget* realize(Fw::Widget* model, QWidget* parent = nullptr);

}  // namespace FwQt
}  // namespace Gui

#endif  // GUI_FW_QTVIEW_H
