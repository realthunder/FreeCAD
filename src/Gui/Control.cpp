/***************************************************************************
 *   Copyright 2011 (c) Jürgen Riegel <juergen.riegel@web.de>              *
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


#include "PreCompiled.h"

#ifndef _PreComp_
# include <QAction>
# include <QApplication>
# include <QDebug>
# include <QDockWidget>
# include <QPointer>
#endif

#include <App/Application.h>
#include <App/AutoTransaction.h>
#include <Gui/ComboView.h>
#include <Gui/DockWindowManager.h>
#include <Gui/MainWindow.h>

#include "Control.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Document.h"
#include "MDIView.h"
#include "Selection.h"
#include "Tree.h"
#include "TaskView/TaskView.h"


using namespace Gui;
using namespace std;

/* TRANSLATOR Gui::ControlSingleton */

ControlSingleton* ControlSingleton::_pcSingleton = nullptr;
static QPointer<Gui::TaskView::TaskView> _taskPanel = nullptr;

namespace {
/// An open dialog as Control keeps it
struct OpenDialog
{
    Gui::TaskView::TaskDialog *dialog {nullptr};
    /// The view that took a selection instance of its own for it
    QPointer<Gui::MDIView> selectionView;
    /// Handed to the task view: until then it is nobody's answer, as a
    /// dialog was not the active one while its own open() ran
    bool shown {false};
};
/// The open dialogs. One at most while ControlSingleton::exclusive().
std::vector<OpenDialog> openDialogs;

OpenDialog *recordOf(const Gui::TaskView::TaskDialog *dlg)
{
    for (OpenDialog &open : openDialogs) {
        if (open.dialog == dlg)
            return &open;
    }
    return nullptr;
}

/** The observers of a dialog go where its view selects.
 *
 * A dialog is built before it is shown, by whoever shows it, so its task
 * boxes attached to whatever was current then. Every selection observer
 * found in the dialog and in the widgets it shows is given the owner
 * view's instance as its home: the ones listening move there, the ones
 * that only listen while a button of theirs is down attach there when
 * they do.
 */
void adoptDialogObservers(Gui::TaskView::TaskDialog *dlg)
{
    Gui::SelectionSingleton *sel = dlg->owner().selectionInstance();
    if (!sel)
        return;
    auto adopt = [sel](QObject *object) {
        if (auto observer = dynamic_cast<Gui::SelectionObserver*>(object))
            observer->adoptSelection(*sel);
    };
    adopt(dlg);
    for (QObject *child : dlg->findChildren<QObject*>())
        adopt(child);
    for (QWidget *widget : dlg->getDialogContent()) {
        adopt(widget);
        for (QObject *child : widget->findChildren<QObject*>())
            adopt(child);
    }
}
} // namespace

ControlSingleton::ControlSingleton()
  : oldTabIndex(-1)
{

}

ControlSingleton::~ControlSingleton() = default;

Gui::TaskView::TaskView* ControlSingleton::taskPanel() const
{
    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    // should return the pointer to combo view
    if (pcComboView)
        return pcComboView->getTaskPanel();
    // not all workbenches have the combo view enabled
    else if (_taskPanel)
        return _taskPanel;
    // no task panel available
    else
        return nullptr;
}

void ControlSingleton::showDockWidget(QWidget* widget)
{
    QWidget* parent = widget->parentWidget();
    if (parent) {
        parent->show();
        parent->raise();
    }
}

QTabBar* ControlSingleton::findTabBar(QDockWidget* widget) const
{
    int count = getMainWindow()->tabifiedDockWidgets(widget).size() + 1;
    if (count > 1) {
        QList<QTabBar*> bars = getMainWindow()->findChildren<QTabBar*>();
        for (auto it : bars) {
            if (it->count() <= count) {
                for (int i = 0; i < count; i++) {
                    if (it->tabText(i) == widget->windowTitle()) {
                        return it;
                    }
                }
            }
        }
    }

    return nullptr;
}

void ControlSingleton::aboutToShowDialog(QDockWidget* widget)
{
    static QIcon icon = Gui::BitmapFactory().pixmap("edit-edit.svg");
    QTabBar* bar = findTabBar(widget);
    if (bar) {
        oldTabIndex = bar->currentIndex();
        for (int i = 0; i < bar->count(); i++) {
            if (bar->tabText(i) == widget->windowTitle()) {
                bar->setTabIcon(i, icon);
                break;
            }
        }
    }

    widget->show();
    widget->raise();
}

void ControlSingleton::aboutToHideDialog(QDockWidget* widget)
{
    QTabBar* bar = findTabBar(widget);
    if (bar) {
        bar->setCurrentIndex(oldTabIndex);
        for (int i = 0; i < bar->count(); i++) {
            if (bar->tabText(i) == widget->windowTitle()) {
                bar->setTabIcon(i, QIcon());
                break;
            }
        }
    }
}

Gui::TaskView::TaskView* ControlSingleton::taskWatcherPanel() const
{
    auto panel = qobject_cast<TaskView::TaskView*>(
            Gui::DockWindowManager::instance()->getDockWindow("Task List"));
    if (panel)
        return panel;
    return taskPanel();
}

void ControlSingleton::showTaskView()
{
    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    if (pcComboView)
        pcComboView->showTaskView();
    else if (auto taskView = taskPanel())
        showDockWidget(taskView);
}

void ControlSingleton::showModelView()
{
    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    if (pcComboView)
        pcComboView->showTreeView();
    else if (auto treeView = qobject_cast<Gui::TreeDockWidget*>
        (Gui::DockWindowManager::instance()->getDockWindow("Tree view"))) {
        showDockWidget(treeView);
    }
}

bool ControlSingleton::exclusive() const
{
    static ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/TaskView");
    return !hGrp->GetBool("TaskPanelAllowConcurrent", false);
}

bool ControlSingleton::isOpen(const Gui::TaskView::TaskDialog *dlg) const
{
    return recordOf(dlg) != nullptr;
}

Gui::TaskView::TaskDialog* ControlSingleton::dialogOf(const TaskOwner &owner) const
{
    // Asked by every command that wants to know whether it may run
    if (openDialogs.empty())
        return nullptr;
    // A dialog shown with no view to name is everybody's, as every dialog
    // was before one had an owner.
    for (const OpenDialog &open : openDialogs) {
        if (open.shown && open.dialog->owner().isNull())
            return open.dialog;
    }
    // Asked for nobody in particular: THE dialog while there can be only
    // one, else the dialog of the view being handled.
    const bool any = owner.isNull() && exclusive();
    const TaskOwner whose = (owner.isNull() && !any) ? TaskOwner::current() : owner;
    for (const OpenDialog &open : openDialogs) {
        if (open.shown && (any || open.dialog->owner() == whose))
            return open.dialog;
    }
    return nullptr;
}

Gui::TaskView::TaskDialog* ControlSingleton::blockerOf(const TaskOwner &owner) const
{
    const bool one = exclusive();
    for (const OpenDialog &open : openDialogs) {
        if (!open.shown)
            continue;
        // One at a time. And where several may be open, one for a view:
        // a dialog nobody owns is everybody's, so it shares the task view
        // with no other, whichever of the two comes first.
        const TaskOwner &its = open.dialog->owner();
        if (one || owner.isNull() || its.isNull() || its == owner)
            return open.dialog;
    }
    return nullptr;
}

bool ControlSingleton::mayShowDialog(const TaskOwner &owner) const
{
    return !blockerOf(owner.isNull() ? TaskOwner::current() : owner);
}

bool ControlSingleton::mayShowDialog(App::Document *attachTo) const
{
    return attachTo ? !blockerOf(ownerOf(attachTo)) : mayShowDialog();
}

TaskOwner ControlSingleton::ownerOf(App::Document *doc) const
{
    Gui::Document *gdoc = doc ? Application::Instance->getDocument(doc) : nullptr;
    if (!gdoc)
        return TaskOwner();
    // The view being handled, when it is one of this document's: a served
    // document may have no desktop view at all, and under a client's scope
    // the view that asks is that client's.
    TaskOwner current = TaskOwner::current();
    if (current.document() == gdoc)
        return current;
    return TaskOwner(gdoc->getActiveView());
}

void ControlSingleton::showDialog(Gui::TaskView::TaskDialog *dlg, const TaskOwner &owner)
{
    showDialogFor(dlg, owner.isNull() ? TaskOwner::current() : owner);
}

void ControlSingleton::showDialog(Gui::TaskView::TaskDialog *dlg, App::Document *attachTo)
{
    if (attachTo)
        showDialogFor(dlg, ownerOf(attachTo));
    else
        showDialog(dlg);
}

void ControlSingleton::showDialogFor(Gui::TaskView::TaskDialog *dlg, const TaskOwner &owner)
{
    if (!dlg) {
        qWarning() << "ControlSingleton::showDialog: Task dialog is null";
        return;
    }
    // only one dialog at a time, print a warning instead of raising an assert
    if (!isOpen(dlg) && blockerOf(owner)) {
        qWarning() << "ControlSingleton::showDialog: Can't show "
                   << dlg->metaObject()->className()
                   << " since there is already an active task dialog";
        return;
    }

    // Since the caller sets up a modeless task panel, it indicates intention
    // for prolonged editing. So disable auto transaction in the current call
    // stack.
    // Do this before showing the dialog because its open() function is called
    // which may open a transaction but fails when auto transaction is still active.
    App::AutoTransaction::setEnable(false);

    // The owner is named once, before the dialog is opened: its open() and
    // whoever hears signalShowDialog may ask for it. A dialog shown again
    // keeps the view it was first shown for.
    if (!isOpen(dlg)) {
        TaskView::TaskDialogAttorney::setOwner(dlg, owner);
        OpenDialog open;
        open.dialog = dlg;
        // And the view selects into an instance of its own for as long as
        // the dialog is open (docs/TaskPanelPerView.md sec 12), unless the
        // dialog says it works on the selection every view shares.
        if (dlg->usesOwnSelection()) {
            if (MDIView *view = owner.mdiView()) {
                view->takeOwnSelection();
                open.selectionView = view;
            }
        }
        openDialogs.push_back(open);
        // Heard from the start: a dialog may be closed by its own open()
        connect(dlg, &TaskView::TaskDialog::aboutToBeDestroyed,
                this, [this, dlg] { closedDialog(dlg); });
        adoptDialogObservers(dlg);
    }

    bool handed = false;
    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    // should return the pointer to combo view
    if (pcComboView) {
        pcComboView->showDialog(dlg);
        handed = true;

        // make sure that the combo view is shown -- for a page it shows:
        // a page that went into its view needs no dock brought up for it
        auto dw = qobject_cast<QDockWidget*>(pcComboView->parentWidget());
        if (dw && !dw->toggleViewAction()->isChecked()
                && !pcComboView->getTaskPanel()->hostOf(dlg)) {
            aboutToShowDialog(dw);
            dw->toggleViewAction()->activate(QAction::Trigger);
            dw->setFeatures(QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        }
    }
    // not all workbenches have the combo view enabled
    else if (!_taskPanel) {
        auto dw = new QDockWidget();
        dw->setWindowTitle(tr("Task panel"));
        dw->setFeatures(QDockWidget::DockWidgetMovable);
        _taskPanel = new Gui::TaskView::TaskView(dw);
        dw->setWidget(_taskPanel);
        _taskPanel->showDialog(dlg);
        handed = true;
        // Opposite the tree, which owns the left side.
        getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, dw);
        connect(dlg, &TaskView::TaskDialog::destroyed, dw, &ControlSingleton::deleteLater);
        dw->show();
        dw->raise();
    }

    // Still there: its own open() may have closed it
    if (OpenDialog *open = recordOf(dlg)) {
        if (handed)
            open->shown = true;
        else
            closedDialog(dlg);
    }
}

Gui::TaskView::TaskDialog* ControlSingleton::activeDialog() const
{
    return dialogOf(TaskOwner());
}

Gui::TaskView::TaskDialog* ControlSingleton::activeDialog(const TaskOwner &owner) const
{
    return dialogOf(owner);
}

Gui::TaskView::TaskDialog* ControlSingleton::activeDialog(App::Document *attachedTo) const
{
    if (!attachedTo)
        return activeDialog();
    // The dialog of one of the document's views: of the view being
    // handled first, when it is one of them.
    const TaskOwner current = TaskOwner::current();
    Gui::TaskView::TaskDialog *found = nullptr;
    for (const OpenDialog &open : openDialogs) {
        if (!open.shown)
            continue;
        const TaskOwner &its = open.dialog->owner();
        if (its.isNull())
            return open.dialog;
        Gui::Document *gdoc = its.document();
        if (!gdoc || gdoc->getDocument() != attachedTo)
            continue;
        if (its == current)
            return open.dialog;
        if (!found)
            found = open.dialog;
    }
    return found;
}

void ControlSingleton::accept(const TaskOwner &owner)
{
    if (auto dlg = dialogOf(owner))
        acceptDialog(dlg);
}

void ControlSingleton::reject(const TaskOwner &owner)
{
    if (auto dlg = dialogOf(owner))
        rejectDialog(dlg);
}

void ControlSingleton::closeDialog(const TaskOwner &owner)
{
    if (auto dlg = dialogOf(owner))
        removeDialog(dlg);
}

void ControlSingleton::accept(App::Document *attachedTo)
{
    if (auto dlg = activeDialog(attachedTo))
        acceptDialog(dlg);
}

void ControlSingleton::reject(App::Document *attachedTo)
{
    if (auto dlg = activeDialog(attachedTo))
        rejectDialog(dlg);
}

void ControlSingleton::closeDialog(App::Document *attachedTo)
{
    if (auto dlg = activeDialog(attachedTo))
        removeDialog(dlg);
}

void ControlSingleton::accept()
{
    acceptDialog(dialogOf(TaskOwner()));
}

void ControlSingleton::reject()
{
    rejectDialog(dialogOf(TaskOwner()));
}

void ControlSingleton::closeDialog()
{
    removeDialog(dialogOf(TaskOwner()));
}

// The dialog these three act on is named to the task view: left to itself
// it takes the one whose page it shows, which need not be the one asked
// about.

void ControlSingleton::acceptDialog(Gui::TaskView::TaskDialog *dlg)
{
    Gui::TaskView::TaskView* taskView = taskPanel();
    if (taskView) {
        if (dlg)
            taskView->accept(dlg);
        else
            taskView->accept();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents |
                            QEventLoop::ExcludeSocketNotifiers);
    }
}

void ControlSingleton::rejectDialog(Gui::TaskView::TaskDialog *dlg)
{
    Gui::TaskView::TaskView* taskView = taskPanel();
    if (taskView) {
        if (dlg)
            taskView->reject(dlg);
        else
            taskView->reject();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents |
                            QEventLoop::ExcludeSocketNotifiers);
    }
}

void ControlSingleton::removeDialog(Gui::TaskView::TaskDialog *dlg)
{
    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    // should return the pointer to combo view
    if (pcComboView) {
        pcComboView->closeDialog(dlg);
    } else if (_taskPanel) {
        if (dlg)
            _taskPanel->removeDialog(dlg);
        else
            _taskPanel->removeDialog();
    }
}

void ControlSingleton::ownerClosed(const TaskOwner &owner)
{
    if (Gui::TaskView::TaskView* taskView = taskPanel())
        taskView->ownerClosed(owner);
}

void ControlSingleton::closedDialog(Gui::TaskView::TaskDialog *dlg)
{
    QPointer<MDIView> selectionView;
    for (auto it = openDialogs.begin(); it != openDialogs.end(); ++it) {
        if (it->dialog == dlg) {
            selectionView = it->selectionView;
            openDialogs.erase(it);
            break;
        }
    }
    // After the dialog's own closed(): what it left selected is what its
    // view hands back to the room.
    if (MDIView *view = selectionView)
        view->releaseOwnSelection();
    if (auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"))) {
        pcComboView->closedDialog();
        // make sure that the combo view is shown
        auto dw = qobject_cast<QDockWidget*>(pcComboView->parentWidget());
        if (dw && openDialogs.empty()) {
            dw->setFeatures(QDockWidget::DockWidgetClosable
                            | QDockWidget::DockWidgetMovable
                            | QDockWidget::DockWidgetFloatable);
        }
    }
}

bool ControlSingleton::isAllowedAlterDocument() const
{
    return isAllowedAlterDocument(TaskOwner());
}

bool ControlSingleton::isAllowedAlterView() const
{
    return isAllowedAlterView(TaskOwner());
}

bool ControlSingleton::isAllowedAlterSelection() const
{
    return isAllowedAlterSelection(TaskOwner());
}

bool ControlSingleton::isAllowedAlterDocument(const TaskOwner &owner) const
{
    if (auto dlg = dialogOf(owner))
        return dlg->isAllowedAlterDocument();
    return true;
}

bool ControlSingleton::isAllowedAlterView(const TaskOwner &owner) const
{
    if (auto dlg = dialogOf(owner))
        return dlg->isAllowedAlterView();
    return true;
}

bool ControlSingleton::isAllowedAlterSelection(const TaskOwner &owner) const
{
    if (auto dlg = dialogOf(owner))
        return dlg->isAllowedAlterSelection();
    return true;
}

bool ControlSingleton::isAllowedAlterDocument(App::Document *attachedTo) const
{
    if (auto dlg = activeDialog(attachedTo))
        return dlg->isAllowedAlterDocument();
    return true;
}

bool ControlSingleton::isAllowedAlterView(App::Document *attachedTo) const
{
    if (auto dlg = activeDialog(attachedTo))
        return dlg->isAllowedAlterView();
    return true;
}

bool ControlSingleton::isAllowedAlterSelection(App::Document *attachedTo) const
{
    if (auto dlg = activeDialog(attachedTo))
        return dlg->isAllowedAlterSelection();
    return true;
}

// -------------------------------------------

ControlSingleton& ControlSingleton::instance()
{
    if (!_pcSingleton)
        _pcSingleton = new ControlSingleton;
    return *_pcSingleton;
}

void ControlSingleton::destruct ()
{
    if (_pcSingleton)
        delete _pcSingleton;
    _pcSingleton = nullptr;
}


// -------------------------------------------


#include "moc_Control.cpp"

