/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_TASKVIEW_TASKPANELHOST_H
#define GUI_TASKVIEW_TASKPANELHOST_H

#include <memory>

#include <QPointer>
#include <QWidget>
#include <FCGlobal.h>

class QBoxLayout;
class QEvent;
class QLabel;
class QToolButton;

namespace Gui
{

class MDIView;
class SelectionScope;
class ViewerScope;

namespace TaskView
{

class TaskPage;
class TaskView;

/** A task dialog's page inside the view it belongs to
 * (docs/TaskPanelPerView.md sec 5.2).
 *
 * A child of the widget the view fills -- its Gui::ViewAreaCell when it is
 * in one, else the view itself -- laid over the picture along one side. It
 * is where the page of a dialog is while the preference TaskPanelInView is
 * on, and so it stays in sight whichever view is the active one.
 *
 * A slim header above the page carries the dialog's title and two
 * buttons: collapse to the header, and send the panels back to the combo
 * view (which turns the preference off). Dragging the header moves the
 * host to the other side of its view. Side and collapsed state are kept
 * per kind of view in the user parameters.
 *
 * The task view makes a host when a page is to be shown in a view and
 * takes the page back before it lets the host go. A host whose place is
 * destroyed under it hands its page back from its own destructor: the
 * page must outlive the dialog, which owns the widgets in it.
 */
class GuiExport TaskPanelHost: public QWidget
{
    Q_OBJECT

public:
    TaskPanelHost(TaskView* taskView, MDIView* view);
    ~TaskPanelHost() override;

    /// The host laid in \a place (a cell, or a view), or null.
    static TaskPanelHost* hostIn(const QWidget* place);
    /// The host \a object is in -- itself, one of its widgets, an object
    /// one of them owns -- or null.
    static TaskPanelHost* hostAround(const QObject* object);
    /// Whether any view hosts a page. Safe from any thread.
    static bool any();

    /// The view this host belongs to; null once it is gone.
    MDIView* view() const;
    TaskPage* page() const
    {
        return _page;
    }
    /// Show \a page here. The host takes it as its child.
    void setPage(TaskPage* page);
    /// Give the page up: still this host's child and now hidden, for the
    /// caller to put elsewhere. Null when there is none.
    TaskPage* takePage();
    /// Let the host go: the page is given up as by takePage(), the host is
    /// hidden for good and deletes itself once control is back in the
    /// event loop -- it may be asked to from one of its own buttons.
    TaskPage* release();
    void setTitle(const QString& title);

    bool isOnRight() const
    {
        return _right;
    }
    void setOnRight(bool right, bool remember = true);
    bool isCollapsed() const
    {
        return _collapsed;
    }
    void setCollapsed(bool collapsed, bool remember = true);
    /// Whether the host takes the whole height of its place. Off, which
    /// is the default, it is as tall as its panel needs and no taller: a
    /// two-line panel does not cover the height of the view. A dialog
    /// that asks for all the room there is (TaskDialog::needsFullSpace)
    /// is given it.
    void setFillsHeight(bool fill);

    /// Lay the host out in its place: along its side, clear of the
    /// place's own chrome, down to its header when collapsed or when the
    /// place is too small for a panel.
    void place();
    /// Move with the cursor while the header is dragged (\a x is the
    /// host's wanted left edge in its place), and settle on the nearer
    /// side when the drag ends.
    void dragTo(int x);
    void dragEnded();

    /// The least width a host is given, and the least its place must
    /// have for a panel to be shown open in it.
    static constexpr int MinWidth = 240;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    /// Stand in the widget the view fills now.
    void attach();
    void updateButtons();
    /// The height the page asks for: its panel, and what is pinned beside
    /// the scrolling
    int pageHeight() const;
    /// place(), once control is back in the event loop
    void placeLater();

    QPointer<TaskView> _taskView;
    QPointer<MDIView> _view;
    QPointer<QWidget> _place;
    TaskPage* _page {nullptr};
    QBoxLayout* _layout;
    QWidget* _header;
    QLabel* _title;
    QToolButton* _collapse;
    QToolButton* _toCombo;
    bool _right {false};
    bool _collapsed {false};
    bool _dragging {false};
    bool _fill {false};
    bool _placePending {false};
    /// Let go (release()): it shows itself nowhere any more
    bool _retired {false};
};

/** Open around the delivery of one event, by GUIApplication.
 *
 * A panel in its view can be used while another view is the active one
 * (docs/TaskPanelPerView.md sec 12.5). What its widgets do then is done
 * for ITS view: for the event's extent the view is the one being handled
 * (Gui::ViewerScope), and Gui::Selection() is the selection that view
 * selects in. A mouse press makes the view the active one first, which is
 * what a click into a panel means (sec 5.2).
 *
 * Nothing at all while no view hosts a page, and nothing for an event of
 * the active view's own panel.
 */
class GuiExport TaskPageEventScope
{
public:
    TaskPageEventScope(QObject* receiver, QEvent* event);
    ~TaskPageEventScope();
    TaskPageEventScope(const TaskPageEventScope&) = delete;
    TaskPageEventScope& operator=(const TaskPageEventScope&) = delete;

private:
    void open(QObject* receiver, QEvent* event);

    std::unique_ptr<ViewerScope> viewer;
    std::unique_ptr<SelectionScope> selection;
};

}  // namespace TaskView
}  // namespace Gui

#endif  // GUI_TASKVIEW_TASKPANELHOST_H
