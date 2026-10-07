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
class BaseView;
class ControlSingleton;
class Document;
class MDIView;
class ViewProviderDocumentObject;
namespace DockWnd{
class ComboView;
}
namespace TaskView {

using Connection = fastsignals::connection;
class TaskEditControl;
class TaskDialog;
class TaskPanelHost;

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
    /// Its open() has returned: not activated before that
    bool opened {false};
    /// Told activate() and not yet deactivate()
    bool active {false};
    /// Its page has been shown: the Tasks tab is raised the first time
    bool raised {false};
    /// Where the page is while it is not in the task view's stack: in its
    /// owner view (docs/TaskPanelPerView.md sec 5.2). Null in the stack.
    TaskPanelHost *host {nullptr};
    /// Whether the page belongs in its owner view. Settled when the dialog
    /// is shown, from its view's place (TaskPlacement), and again only
    /// when THAT view's place changes: a panel that is open is not moved by
    /// the preference, nor by what is chosen in another view (sec 15.1).
    bool inView {false};
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
    /** Whether \a info's page is the one to show while \a view is the
     * active view (docs/TaskPanelPerView.md sec 5.1).
     *
     * A dialog's own view, and a dialog nobody owns for every view. And
     * every view that is in the edit the dialog's view started: with
     * PerViewEdit off an edit is every view's of its document, and its
     * panel with it.
     */
    static bool showsFor(const TaskInfo &info, const TaskOwner &view);
    /** \a owner's view is going: close what it owns.
     *
     * A dialog that asked to be told (setAutoCloseOnClosedView) is told;
     * any other is rejected, and removed whatever it answers -- there is
     * nobody left to answer it.
     */
    void ownerClosed(const TaskOwner &owner);

    /// The host \a dlg's page is in, or null while this task view has it
    TaskPanelHost *hostOf(const TaskDialog *dlg) const;
    /** Put every page where it belongs.
     *
     * In its owner view for a dialog a view of this window owns, while
     * that view's place said so when the dialog was shown or last changed
     * (TaskInfo::inView); in the stack here otherwise -- always for a
     * dialog nobody owns, which is shown over everything, and for a served
     * client's, whose view has no widget. No dialog is closed or opened
     * and none is told anything: the view being worked in did not change.
     */
    void applyHosting();
    /** Every open panel goes where its view's place says NOW.
     *
     * What the preference's "the open views too" asks for
     * (TaskPlacement::applyToAll); a panel is otherwise where it was put
     * when it was shown.
     */
    void followPlacement();
    /** Send the panel the stack is showing into its view.
     *
     * The button on the title bar of the dock that holds the task view:
     * it acts on the panel in front of it and on that panel's view alone
     * (docs/TaskPanelPerView.md sec 15.1), which keeps the place. Nothing
     * when the stack shows the watchers, or a dialog no view owns.
     */
    void sendShownToView();
    /** A key pressed in \a page, wherever the page is.
     *
     * Enter presses the default button of that page's dialog and Escape
     * the one its dialog names; a dialog with no such button leaves its
     * edit. Any other key ends here.
     */
    void pageKeyPress(TaskPage *page, QKeyEvent *ke);
    /// The work-around event() applies to a shortcut override, for
    /// whatever else holds a page
    static void acceptEditingKeys(QEvent *event);

Q_SIGNALS:
    void taskUpdate();
    /// A dialog's page is shown for the first time, or the dialog was
    /// asked for again while its page is the one shown: what holds the
    /// task view brings it to the front
    void dialogShown();
    /// What is shown changed: a dialog's page (true) or the watchers'
    void shownDialogChanged(bool dialog);
    /// A dialog whose page had been shown is gone
    void shownDialogClosed();

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
    /// The dialog to show while \a view is the active view, or null
    TaskInfo *infoFor(const TaskOwner &view);
    /// Show what the main window's active view calls for
    void showForActiveView();
    /// Show \a info's page, or the watchers' with null, and tell the
    /// dialogs (syncActivation)
    void setShownTaskInfo(TaskInfo *info);
    /// The same without telling the dialogs
    void showPage(TaskInfo *info);
    /// activate() the dialog that became the one worked in, deactivate()
    /// the ones that stopped being so
    void syncActivation();
    /// The lines on the watchers' page that say where the panels are
    void updateHint();
    /// Put \a info's page where it belongs now: in a host in its view, or
    /// in the stack
    void placePage(TaskInfo &info);
    /// \a host is being destroyed with the place it stood in, and hands
    /// back the page it held
    void hostGone(TaskPanelHost *host, TaskPage *page);
    friend class TaskPanelHost;
    /// Remove a dialog whose view is gone, telling or rejecting it
    void closeLost(TaskDialog *dlg);
    /// The same for every dialog whose view died without a word
    void closeOrphans();
    /// The panel of the page that is shown
    QSint::ActionPanel *shownPanel() const;
    /// Whether the page shown is the watchers'
    bool watchersShown() const;
    /// Put the contextual panels where they belong now: in the panel of
    /// the page that is shown, or out of sight
    void placeContextualPanels(QSint::ActionPanel *to);
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
     * one belonging to \a doc. This fork's task view follows the active VIEW:
     * the widget is shown, above whatever page is, while a view of \a doc is the
     * active one, and with every view when \a doc is null.
     */
    void addContextualPanel(QWidget* panel, App::Document* doc = nullptr);
    /// Remove a widget added by addContextualPanel and delete it
    void removeContextualPanel(QWidget* panel, App::Document* doc = nullptr);

protected:

    void slotActiveDocument(const App::Document&);
    void slotDeletedDocument();
    void slotUndoDocument(const App::Document&);
    void slotRedoDocument(const App::Document&);
    void slotActivateView(const Gui::MDIView*);
    void slotViewClosed(const Gui::MDIView*);
    void slotResetEdit(const Gui::ViewProviderDocumentObject&);
    /// A view's property changed: its panel's place, side or size
    void slotChangedView(const Gui::BaseView&, const App::Property&);
    void slotDeleteDocument(const Gui::Document&);

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
    /// A widget added by addContextualPanel and the document it is for
    struct ContextualPanel
    {
        QWidget *widget {nullptr};
        App::Document *doc {nullptr};
    };
    /// They sit in the panel of the page that is shown while a view of
    /// their document is active, and in \ref parking otherwise.
    std::vector<ContextualPanel> contextualPanels;
    /// Where a contextual panel is kept while it is not shown: never
    /// visible itself.
    QWidget *parking;
    /// On the watchers' page, above them: which views hold a panel that
    /// is not the one shown, each with a button that goes there.
    QWidget *hint;
    QBoxLayout *hintRows;

    Connection connectApplicationActiveDocument;
    Connection connectApplicationDeleteDocument;
    Connection connectApplicationUndoDocument;
    Connection connectApplicationRedoDocument;
    Connection connectApplicationActivateView;
    Connection connectApplicationCloseView;
    Connection connectApplicationResetEdit;
    Connection connectApplicationChangedView;
    Connection connectGuiDeleteDocument;
};

} //namespace TaskView
} //namespace Gui

#endif // GUI_TASKVIEW_TASKVIEW_H
