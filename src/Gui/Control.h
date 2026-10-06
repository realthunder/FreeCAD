/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef GUI_CONTROL_H
#define GUI_CONTROL_H

// Std. configurations

#include <QObject>
#include <bitset>
#include <stack>

#include <fastsignals/signal.h>

#include <Gui/TaskView/TaskDialog.h>

class QDockWidget;
class QTabBar;

namespace App
{
  class DocumentObject;
  class Document;
}

namespace Gui
{
namespace TaskView
{
    class TaskDialog;
    class TaskView;
}


/** The control class
 */
class GuiExport ControlSingleton : public QObject
{
     Q_OBJECT

public:
    static ControlSingleton& instance();
    static void destruct ();

    /** @name dialog handling
     *  These methods are used to control the TaskDialog stuff.
     */
    //@{
    /** Start a task dialog for a view (docs/TaskPanelPerView.md).
     *
     * A task dialog belongs to a view: \a owner, or with none given the
     * view being handled now (TaskOwner::current()). A call that is
     * DEFERRED -- a timer, a queued connection -- must capture
     * TaskOwner::current() when the deferral is made and pass it here,
     * or it names whichever view is active by the time it runs.
     */
    void showDialog(Gui::TaskView::TaskDialog *dlg, const TaskOwner &owner = TaskOwner());
    /** The open dialog, asked for no view in particular.
     *
     * While only one dialog may be open in the process (exclusive()) this
     * is THAT dialog whichever view is active, so that a caller that
     * means "is anything open?" keeps its meaning. When several may be,
     * it is the current view's.
     */
    Gui::TaskView::TaskDialog* activeDialog() const;
    /// The dialog of \a owner's view, or null. A dialog that has no owner
    /// is everybody's.
    Gui::TaskView::TaskDialog* activeDialog(const TaskOwner &owner) const;
    //@}

    /** @name Upstream's signatures
     *  For code ported from there, where a task dialog is keyed by its
     *  document: the dialog of that document's view. Null is the argument
     *  left out.
     */
    //@{
    void showDialog(Gui::TaskView::TaskDialog *dlg, App::Document *attachTo);
    Gui::TaskView::TaskDialog* activeDialog(App::Document *attachedTo) const;
    void accept(App::Document *attachedTo);
    void reject(App::Document *attachedTo);
    void closeDialog(App::Document *attachedTo);
    bool isAllowedAlterDocument(App::Document *attachedTo) const;
    bool isAllowedAlterView(App::Document *attachedTo) const;
    bool isAllowedAlterSelection(App::Document *attachedTo) const;
    //@}

    /** @name task view handling
     */
    //@{
    Gui::TaskView::TaskView* taskPanel() const;
    Gui::TaskView::TaskView* taskWatcherPanel() const;
    /// raising the model view
    void showModelView();
    //@}

    /*!
      If a task dialog is open then it indicates whether this task dialog allows other commands to modify
      the document while it is open. If no task dialog is open true is returned.
     */
    bool isAllowedAlterDocument() const;
    /*!
      If a task dialog is open then it indicates whether this task dialog allows other commands to modify
      the 3d view while it is open. If no task dialog is open true is returned.
     */
    bool isAllowedAlterView() const;
    /*!
      If a task dialog is open then it indicates whether this task dialog allows other commands to modify
      the selection while it is open. If no task dialog is open true is returned.
     */
    bool isAllowedAlterSelection() const;
    /// The same three asked of \a owner's dialog: true when it has none.
    bool isAllowedAlterDocument(const TaskOwner &owner) const;
    bool isAllowedAlterView(const TaskOwner &owner) const;
    bool isAllowedAlterSelection(const TaskOwner &owner) const;

    /// Accept, reject or close the dialog of \a owner's view; nothing
    /// when it has none.
    void accept(const TaskOwner &owner);
    void reject(const TaskOwner &owner);
    void closeDialog(const TaskOwner &owner);

    /// The task view, the dialog's content widgets, the dialog's owner.
    fastsignals::signal<void (QWidget *, std::vector<QWidget*> &, const TaskOwner &)> signalShowDialog;
    fastsignals::signal<void (QWidget *, std::vector<QWidget*> &, const TaskOwner &)> signalRemoveDialog;

public Q_SLOTS:
    /// These three act on the dialog activeDialog() answers.
    void accept();
    void reject();
    void closeDialog();
    /// raises the task view panel
    void showTaskView();

private Q_SLOTS:
    /// This get called by the TaskView when the Dialog is finished
    void closedDialog();

private:
    struct status {
        std::bitset<32> StatusBits;
    } CurrentStatus;

    std::stack<status> StatusStack;

    Gui::TaskView::TaskDialog *ActiveDialog;
    int oldTabIndex;

private:
    /// Construction
    ControlSingleton();
    /// Destruction
    ~ControlSingleton() override;
    void showDockWidget(QWidget*);
    QTabBar* findTabBar(QDockWidget*) const;
    void aboutToShowDialog(QDockWidget* widget);
    void aboutToHideDialog(QDockWidget* widget);
    /** Whether at most one dialog may be open in the process.
     *
     * True until a transaction has an owner too: the application has one
     * active transaction, and a second dialog opening its own would
     * commit the first one's work in progress (docs/TaskPanelPerView.md
     * sec 8). Nothing but dialogOf() depends on which way this says.
     */
    bool exclusive() const;
    /// The dialog an entry point acts on for \a owner; the null owner is
    /// the argument left out.
    Gui::TaskView::TaskDialog* dialogOf(const TaskOwner &owner) const;
    /// showDialog() with the owner settled: null here is nobody.
    void showDialogFor(Gui::TaskView::TaskDialog *dlg, const TaskOwner &owner);
    /// The view a dialog keyed by \a doc belongs to.
    TaskOwner ownerOf(App::Document *doc) const;

    static ControlSingleton* _pcSingleton;
};

/// Get the global instance
inline ControlSingleton& Control()
{
    return ControlSingleton::instance();
}

} //namespace Gui

#endif // GUI_CONTROL_H
