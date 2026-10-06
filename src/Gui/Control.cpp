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

#include <App/AutoTransaction.h>
#include <Gui/ComboView.h>
#include <Gui/DockWindowManager.h>
#include <Gui/MainWindow.h>

#include "Control.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Document.h"
#include "MDIView.h"
#include "Tree.h"
#include "TaskView/TaskView.h"


using namespace Gui;
using namespace std;

/* TRANSLATOR Gui::ControlSingleton */

ControlSingleton* ControlSingleton::_pcSingleton = nullptr;
static QPointer<Gui::TaskView::TaskView> _taskPanel = nullptr;

ControlSingleton::ControlSingleton()
  : ActiveDialog(nullptr)
  , oldTabIndex(-1)
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
    return true;
}

Gui::TaskView::TaskDialog* ControlSingleton::dialogOf(const TaskOwner &owner) const
{
    if (!ActiveDialog)
        return nullptr;
    const TaskOwner &its = ActiveDialog->owner();
    // A dialog shown with no view to name is everybody's, as every dialog
    // was before one had an owner.
    if (its.isNull())
        return ActiveDialog;
    if (owner.isNull())
        return (exclusive() || its == TaskOwner::current()) ? ActiveDialog : nullptr;
    return its == owner ? ActiveDialog : nullptr;
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
    // only one dialog at a time, print a warning instead of raising an assert
    if (ActiveDialog && ActiveDialog != dlg) {
        if (dlg) {
            qWarning() << "ControlSingleton::showDialog: Can't show "
                       << dlg->metaObject()->className()
                       << " since there is already an active task dialog";
        }
        else {
            qWarning() << "ControlSingleton::showDialog: Task dialog is null";
        }
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
    if (dlg && ActiveDialog != dlg)
        TaskView::TaskDialogAttorney::setOwner(dlg, owner);

    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    // should return the pointer to combo view
    if (pcComboView) {
        pcComboView->showDialog(dlg);

        // make sure that the combo view is shown
        auto dw = qobject_cast<QDockWidget*>(pcComboView->parentWidget());
        if (dw && !dw->toggleViewAction()->isChecked()) {
            aboutToShowDialog(dw);
            dw->toggleViewAction()->activate(QAction::Trigger);
            dw->setFeatures(QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        }

        if (ActiveDialog == dlg)
            return; // dialog is already defined
        ActiveDialog = dlg;
        connect(dlg, &TaskView::TaskDialog::aboutToBeDestroyed,
                this, &ControlSingleton::closedDialog);
    }
    // not all workbenches have the combo view enabled
    else if (!_taskPanel) {
        auto dw = new QDockWidget();
        dw->setWindowTitle(tr("Task panel"));
        dw->setFeatures(QDockWidget::DockWidgetMovable);
        _taskPanel = new Gui::TaskView::TaskView(dw);
        dw->setWidget(_taskPanel);
        _taskPanel->showDialog(dlg);
        // Opposite the tree, which owns the left side.
        getMainWindow()->addDockWidget(Qt::RightDockWidgetArea, dw);
        connect(dlg, &TaskView::TaskDialog::destroyed, dw, &ControlSingleton::deleteLater);
        dw->show();
        dw->raise();
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
    Gui::TaskView::TaskDialog *dlg = dialogOf(TaskOwner());
    if (!dlg || dlg->owner().isNull())
        return dlg;
    Gui::Document *gdoc = dlg->owner().document();
    return (gdoc && gdoc->getDocument() == attachedTo) ? dlg : nullptr;
}

void ControlSingleton::accept(const TaskOwner &owner)
{
    if (dialogOf(owner))
        accept();
}

void ControlSingleton::reject(const TaskOwner &owner)
{
    if (dialogOf(owner))
        reject();
}

void ControlSingleton::closeDialog(const TaskOwner &owner)
{
    if (dialogOf(owner))
        closeDialog();
}

void ControlSingleton::accept(App::Document *attachedTo)
{
    if (activeDialog(attachedTo))
        accept();
}

void ControlSingleton::reject(App::Document *attachedTo)
{
    if (activeDialog(attachedTo))
        reject();
}

void ControlSingleton::closeDialog(App::Document *attachedTo)
{
    if (activeDialog(attachedTo))
        closeDialog();
}

void ControlSingleton::accept()
{
    Gui::TaskView::TaskView* taskView = taskPanel();
    if (taskView) {
        taskView->accept();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents |
                            QEventLoop::ExcludeSocketNotifiers);
    }
}

void ControlSingleton::reject()
{
    Gui::TaskView::TaskView* taskView = taskPanel();
    if (taskView) {
        taskView->reject();
        qApp->processEvents(QEventLoop::ExcludeUserInputEvents |
                            QEventLoop::ExcludeSocketNotifiers);
    }
}

void ControlSingleton::closeDialog()
{
    auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"));
    // should return the pointer to combo view
    if (pcComboView) {
        pcComboView->closeDialog();
    } else if (_taskPanel) {
        _taskPanel->removeDialog();
    }
}

void ControlSingleton::closedDialog()
{
    ActiveDialog = nullptr;
    if (auto pcComboView = qobject_cast<Gui::DockWnd::ComboView*>
        (Gui::DockWindowManager::instance()->getDockWindow("Combo View"))) {
        pcComboView->closedDialog();
        // make sure that the combo view is shown
        auto dw = qobject_cast<QDockWidget*>(pcComboView->parentWidget());
        if (dw) {
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

