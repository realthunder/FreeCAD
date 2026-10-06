/***************************************************************************
 *   Copyright (c) 2009 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef GUI_TASKVIEW_TASKVIEW_H
#define GUI_TASKVIEW_TASKVIEW_H

#include <vector>
#include <QScrollArea>

#include <Gui/QSint/include/QSint>
#include <Gui/Selection.h>
#include <Gui/TaskOwner.h>
#include "TaskWatcher.h"

class QAbstractButton;
class QBoxLayout;
class QStackedWidget;
class QTimer;

namespace App {
class Document;
class Property;
}

namespace Gui {
class ControlSingleton;
namespace DockWnd{
class ComboView;
}
namespace TaskView {

using Connection = fastsignals::connection;
class TaskEditControl;
class TaskDialog;

/// Father class of all content in TaskView
class GuiExport TaskContent 
{

public:
    //TaskContent();
    //~TaskContent();
};

class GuiExport TaskGroup : public QSint::ActionBox, public TaskContent
{
    Q_OBJECT

public:
    explicit TaskGroup(QWidget *parent = nullptr);
    explicit TaskGroup(const QString & headerText, QWidget *parent = nullptr);
    explicit TaskGroup(const QPixmap & icon, const QString & headerText, QWidget *parent = nullptr);
    ~TaskGroup() override;

protected:
    void actionEvent (QActionEvent*) override;
};

/// Father class of content with header and Icon
class GuiExport TaskBox : public QSint::ActionGroup, public TaskContent
{
    Q_OBJECT

public:
    /** Constructor. Creates TaskBox without header.
      */
    explicit TaskBox(QWidget *parent = nullptr);

    /** Constructor. Creates TaskBox with header's
        text set to \a title, but with no icon.

        If \a expandable set to \a true (default), the group can be expanded/collapsed by the user.
      */
    explicit TaskBox(const QString& title,
                     bool expandable = true,
                     QWidget *parent = nullptr);

    /** Constructor. Creates TaskBox with header's
        text set to \a title and icon set to \a icon.

        If \a expandable set to \a true (default), the group can be expanded/collapsed by the user.
      */
    explicit TaskBox(const QPixmap& icon,
                     const QString& title,
                     bool expandable = true,
                     QWidget *parent = nullptr);
    QSize minimumSizeHint() const override;

    ~TaskBox() override;
    void hideGroupBox();
    bool isGroupVisible() const;
    int foldDirection() const;

Q_SIGNALS:
    void toggledExpansion();

protected:
    void showEvent(QShowEvent*) override;
    void actionEvent (QActionEvent*) override;

private:
    bool wasShown;
};

class GuiExport TaskPanel : public QSint::ActionPanel
{
    Q_OBJECT

public:
    explicit TaskPanel(QWidget *parent = nullptr);
    ~TaskPanel() override;
    QSize minimumSizeHint() const override;
};

/// Father class of content of a Free widget (without header and Icon), shut be an exception!
class GuiExport TaskWidget : public QWidget, public TaskContent
{
    Q_OBJECT

public:
    explicit TaskWidget(QWidget *parent=nullptr);
    ~TaskWidget() override;
};

/** One open dialog's page (docs/TaskPanelPerView.md sec 4.2).
  *
  * Self-contained: a scrolled panel for the dialog's content, and the
  * button box either in that panel or pinned outside the scrolling (the
  * StickyTaskControl preference). Content and buttons travel together,
  * which is what lets a page stand in the task view's stack or be handed
  * to another host.
  */
class GuiExport TaskPage : public QWidget
{
    Q_OBJECT

public:
    explicit TaskPage(QWidget *parent = nullptr);
    ~TaskPage() override;

    QBoxLayout *layout;
    QScrollArea *scrollarea;
    TaskPanel *panel;
};

/// One open dialog as the task view keeps it
struct TaskInfo
{
    TaskPage *page {nullptr};
    TaskDialog *ActiveDialog {nullptr};
    TaskEditControl *ActiveCtrl {nullptr};
    TaskOwner owner;
    /// The dialog's content widgets as shown: a listener to
    /// Control().signalShowDialog may have added its own.
    std::vector<QWidget*> contents;
};

/** TaskView class
  * handles the FreeCAD task view panel. Keeps track of the inserted content elements.
  * This elements get injected mostly by the ViewProvider classes of the selected
  * DocumentObjects. 
  */
class GuiExport TaskView : public QWidget, public Gui::SelectionSingleton::ObserverType
{
    using inherited = QWidget;

    Q_OBJECT

public:
    explicit TaskView(QWidget *parent = nullptr);
    ~TaskView() override;

    /// Observer message from the Selection
    void OnChange(Gui::SelectionSingleton::SubjectType &rCaller,
                          Gui::SelectionSingleton::MessageType Reason) override;

    friend class Gui::DockWnd::ComboView;
    friend class Gui::ControlSingleton;

    void addTaskWatcher(const std::vector<TaskWatcher*> &Watcher);
    void clearTaskWatcher();
    void takeTaskWatcher(TaskView *other);

    bool isEmpty(bool includeWatcher = true) const;

    void clearActionStyle();
    void restoreActionStyle();

    QSize minimumSizeHint() const override;

    /// The dialog whose page is shown, or null while the watchers are
    const TaskInfo *currentTaskInfo() const;
    /// The open dialog of \a owner's view, or null
    TaskDialog *dialog(const TaskOwner &owner) const;

Q_SIGNALS:
    void taskUpdate();

protected Q_SLOTS:
    /// Accept or reject THE dialog: the shown one, else the only one
    void accept();
    void reject();
    void onUpdateWatcher();

protected:
    void accept(TaskDialog *dlg);
    void reject(TaskDialog *dlg);
    void helpRequested(TaskDialog *dlg);
    void clicked(QAbstractButton *button, TaskDialog *dlg);

private:
    void triggerMinimumSizeHint();
    void adjustMinimumSizeHint();
    TaskInfo *infoOf(const TaskDialog *dlg);
    TaskInfo *currentTaskInfo();
    /// The dialog an argument-less entry point acts on
    TaskInfo *theTaskInfo();
    /// Show \a info's page, or the watchers' with null
    void setShownTaskInfo(TaskInfo *info);
    /// The panel of the page that is shown
    QSint::ActionPanel *shownPanel() const;
    /// Carry the contextual panels over to the page now shown
    void moveContextualPanels(QSint::ActionPanel *to);
    void transactionChange();

protected:
    bool eventFilter(QObject *, QEvent*) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

    void addTaskWatcher();
    void removeTaskWatcher();
    /// update the visibility of the TaskWatcher accordant to the selection
    void updateWatcher();
    /// used by Gui::Control to register Dialogs
    void showDialog(TaskDialog *dlg);
    // removes the running dialog after accept() or reject() from the TaskView
    void removeDialog();
    void removeDialog(TaskDialog *dlg);

public:
    /** Add a widget to the task panel alongside whatever else is shown
     *
     * Upstream keeps one task panel per document and attaches the widget to the
     * one belonging to \a doc. This fork's task view follows the active document
     * instead, so \a doc is accepted for source compatibility and not used.
     */
    void addContextualPanel(QWidget* panel, App::Document* doc = nullptr);
    /// Remove a widget added by addContextualPanel and delete it
    void removeContextualPanel(QWidget* panel, App::Document* doc = nullptr);

protected:

    void slotActiveDocument(const App::Document&);
    void slotDeletedDocument();
    void slotUndoDocument(const App::Document&);
    void slotRedoDocument(const App::Document&);

    std::vector<TaskWatcher*> ActiveWatcher;

    /** The stack: page 0 is \ref scrollarea with the watchers' panel
     * \ref taskPanel in it, and every open dialog has a page of its own
     * after it (docs/TaskPanelPerView.md sec 4.2).
     */
    QStackedWidget *stack;
    QSint::ActionPanel* taskPanel;
    QBoxLayout *layout;
    QScrollArea *scrollarea;
    QTimer *timer;

    /// The open dialogs. One at most while ControlSingleton::exclusive().
    std::vector<TaskInfo> taskInfos;
    /// Widgets added by addContextualPanel, which sit in the panel of the
    /// page that is shown.
    std::vector<QWidget*> contextualPanels;

    Connection connectApplicationActiveDocument;
    Connection connectApplicationDeleteDocument;
    Connection connectApplicationUndoDocument;
    Connection connectApplicationRedoDocument;
};

} //namespace TaskView
} //namespace Gui

#endif // GUI_TASKVIEW_TASKVIEW_H
