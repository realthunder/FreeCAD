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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <QAbstractSpinBox>
# include <QActionEvent>
# include <QApplication>
# include <QCursor>
# include <QLabel>
# include <QLineEdit>
# include <QPointer>
# include <QPushButton>
# include <QStackedWidget>
# include <QTimer>
# include <QComboBox>
#endif

#include <optional>

#include <App/Document.h>
#include <Gui/ActionFunction.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Control.h>
#include <Gui/Document.h>
#include <Gui/MainWindow.h>
#include <Gui/MDIView.h>
#include <Gui/ViewerContext.h>
#include <Gui/ViewParams.h>
#include <Gui/ViewProviderDocumentObject.h>
#include <Gui/Widgets.h>

#include "TaskView.h"
#include "TaskDialog.h"
#include "TaskEditControl.h"
#include <Gui/Control.h>

#include <Gui/QSint/actionpanel/taskgroup_p.h>
#include <Gui/QSint/actionpanel/taskheader_p.h>
#include <Gui/QSint/actionpanel/freecadscheme.h>


using namespace Gui::TaskView;
namespace sp = std::placeholders;

namespace {
/// The view the main window's user is working in. Not the view being
/// handled (TaskOwner::current()): a client's request does not change
/// what the desktop's task view shows.
Gui::TaskOwner activeOwner()
{
    Gui::MainWindow *mw = Gui::getMainWindow();
    return Gui::TaskOwner(mw ? mw->activeWindow() : nullptr);
}
} // namespace


//**************************************************************************
//**************************************************************************
// TaskWidget
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskWidget::TaskWidget( QWidget *parent)
    : QWidget(parent)
{

}

TaskWidget::~TaskWidget() = default;

//**************************************************************************
//**************************************************************************
// TaskGroup
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskGroup::TaskGroup(QWidget *parent)
    : QSint::ActionBox(parent)
{
}

TaskGroup::TaskGroup(const QString & headerText, QWidget *parent)
    : QSint::ActionBox(headerText, parent)
{
}

TaskGroup::TaskGroup(const QPixmap & icon, const QString & headerText, QWidget *parent)
    : QSint::ActionBox(icon, headerText, parent)
{
}

TaskGroup::~TaskGroup() = default;

void TaskGroup::actionEvent (QActionEvent* e)
{
    QAction *action = e->action();
    switch (e->type()) {
    case QEvent::ActionAdded:
        {
            this->createItem(action);
            break;
        }
    case QEvent::ActionChanged:
        {
            break;
        }
    case QEvent::ActionRemoved:
        {
            // cannot change anything
            break;
        }
    default:
        break;
    }
}

//**************************************************************************
//**************************************************************************
// TaskBox
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskBox::TaskBox(QWidget *parent)
  : QSint::ActionGroup(parent), wasShown(false)
{
    // override vertical size policy because otherwise task dialogs
    // whose needsFullSpace() returns true won't take full space.
    myGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	connect(myHeader, SIGNAL(activated()), this, SIGNAL(toggledExpansion()));
    m_foldDirection = 1;
}

TaskBox::TaskBox(const QString &title, bool expandable, QWidget *parent)
  : QSint::ActionGroup(title, expandable, parent), wasShown(false)
{
    // override vertical size policy because otherwise task dialogs
    // whose needsFullSpace() returns true won't take full space.
    myGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	connect(myHeader, SIGNAL(activated()), this, SIGNAL(toggledExpansion()));
    m_foldDirection = 1;
}

TaskBox::TaskBox(const QPixmap &icon, const QString &title, bool expandable, QWidget *parent)
    : QSint::ActionGroup(icon, title, expandable, parent), wasShown(false)
{
    // override vertical size policy because otherwise task dialogs
    // whose needsFullSpace() returns true won't take full space.
    myGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	connect(myHeader, SIGNAL(activated()), this, SIGNAL(toggledExpansion()));
    m_foldDirection = 1;
}

QSize TaskBox::minimumSizeHint() const
{
    // ActionGroup returns a size of 200x100 which leads to problems
    // when there are several task groups in a panel and the first
    // one is collapsed. In this case the task panel doesn't expand to
    // the actually required size and all the remaining groups are
    // squeezed into the available space and thus the widgets in there
    // often can't be used any more.
    // To fix this problem minimumSizeHint() is implemented to again
    // respect the layout's minimum size.
    QSize s1 = QSint::ActionGroup::minimumSizeHint();
    QSize s2 = QWidget::minimumSizeHint();
    return {qMax(s1.width(), s2.width()), qMax(s1.height(), s2.height())};
}

TaskBox::~TaskBox() = default;

void TaskBox::showEvent(QShowEvent*)
{
    wasShown = true;
}

void TaskBox::hideGroupBox()
{
    if (!wasShown) {
        // get approximate height
        int h=0;
        int ct = groupLayout()->count();
        for (int i=0; i<ct; i++) {
            QLayoutItem* item = groupLayout()->itemAt(i);
            if (item && item->widget()) {
                QWidget* w = item->widget();
                h += w->height();
            }
        }

        m_tempHeight = m_fullHeight = h;
        // For the very first time the group gets shown
        // we cannot do the animation because the layouting
        // is not yet fully done
        m_foldDelta = 0;
    }
    else {
        m_tempHeight = m_fullHeight = myGroup->height();
        m_foldDelta = m_fullHeight / myScheme->groupFoldSteps;
    }

    m_foldStep = 0.0;
    m_foldDirection = -1;

    // make sure to have the correct icon
    bool block = myHeader->blockSignals(true);
    myHeader->fold();
    myHeader->blockSignals(block);

    myDummy->setFixedHeight(0);
    myDummy->hide();
    myGroup->hide();

    m_foldPixmap = QPixmap();
    setFixedHeight(myHeader->height());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    Q_EMIT toggledExpansion();
}

int TaskBox::foldDirection() const
{
    return m_foldDirection;
}

bool TaskBox::isGroupVisible() const
{
    return myGroup->isVisible();
}

void TaskBox::actionEvent (QActionEvent* e)
{
    QAction *action = e->action();
    switch (e->type()) {
    case QEvent::ActionAdded:
        {
            auto label = new QSint::ActionLabel(action, this);
            this->addActionLabel(label, true, false);
            break;
        }
    case QEvent::ActionChanged:
        {
            break;
        }
    case QEvent::ActionRemoved:
        {
            // cannot change anything
            break;
        }
    default:
        break;
    }
}

//**************************************************************************
//**************************************************************************
// TaskPanel
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskPanel::TaskPanel(QWidget *parent)
  : QSint::ActionPanel(parent)
{
}

TaskPanel::~TaskPanel() = default;

QSize TaskPanel::minimumSizeHint() const
{
    // ActionPanel returns a size of 200x150 which leads to problems
    // when there are several task groups in the panel and the first
    // one is collapsed. In this case the task panel doesn't expand to
    // the actually required size and all the remaining groups are
    // squeezed into the available space and thus the widgets in there
    // often can't be used any more.
    // To fix this problem minimumSizeHint() is implemented to again
    // respect the layout's minimum size.
    QSize s1 = QSint::ActionPanel::minimumSizeHint();
    QSize s2 = QWidget::minimumSizeHint();
    return {qMax(s1.width(), s2.width()), qMax(s1.height(), s2.height())};
}

//**************************************************************************
//**************************************************************************
// TaskPage
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskPage::TaskPage(QWidget *parent)
    : QWidget(parent)
{
    // Laid out as the task view lays out its own scroll area, so that a
    // dialog's page stands exactly where the one shared panel stood.
    this->layout = new QVBoxLayout(this);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    this->scrollarea = new QScrollArea(this);
    this->layout->addWidget(scrollarea, 1);

    panel = new TaskPanel(this);
    QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(panel->sizePolicy().hasHeightForWidth());
    panel->setSizePolicy(sizePolicy);
    panel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());
    this->scrollarea->setWidget(panel);
    this->scrollarea->setWidgetResizable(true);
    this->scrollarea->setMinimumWidth(200);
    // The task view does not ask its dock for room: what does not fit is
    // scrolled. That was never written down anywhere. QScrollArea takes
    // its size hint from its widget the FIRST time it is asked and keeps
    // it, and the one panel every dialog used to share was first asked at
    // start-up, empty. A page asked with its dialog's content in it would
    // have its dock widened to fit, so it is asked here, empty as well.
    (void)this->scrollarea->sizeHint();
}

TaskPage::~TaskPage() = default;

//**************************************************************************
//**************************************************************************
// TaskView
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskView::TaskView(QWidget *parent)
    : QWidget(parent)
{
    //addWidget(new TaskEditControl(this));
    //addWidget(new TaskAppearance(this));
    //addStretch();
    
    this->setAutoFillBackground(true);
    this->layout = new QVBoxLayout(this);
    this->layout->setSpacing(0);
    this->stack = new QStackedWidget(this);
    this->layout->addWidget(stack, 1);
    // Page 0, the watchers'. A page per open dialog follows it.
    this->scrollarea = new QScrollArea(stack);
    this->stack->addWidget(scrollarea);

    taskPanel = new TaskPanel(this);
    QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(taskPanel->sizePolicy().hasHeightForWidth());
    taskPanel->setSizePolicy(sizePolicy);
    taskPanel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());
    this->scrollarea->setWidget(taskPanel);
    this->scrollarea->setWidgetResizable(true);
    // this->scrollarea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->scrollarea->setMinimumWidth(200);

    // Where the panels are, when the page shown is the watchers' and a
    // dialog is open for another view: a box above the watchers with a
    // button per view. Hidden while it has nothing to say.
    auto hintBox = new TaskBox(Gui::BitmapFactory().pixmap("edit-edit.svg"),
                               tr("Task panel in another view"), false, nullptr);
    hintBox->setObjectName(QStringLiteral("taskPanelElsewhere"));
    auto hintBody = new QWidget(hintBox);
    this->hintRows = new QVBoxLayout(hintBody);
    this->hintRows->setContentsMargins(0, 0, 0, 0);
    hintBox->groupLayout()->addWidget(hintBody);
    this->hint = hintBox;
    taskPanel->addWidget(hint);
    taskPanel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());
    hint->hide();

    this->parking = new QWidget(this);
    this->parking->hide();

    Gui::SelectionRoom().Attach(this);

    //NOLINTBEGIN
    connectApplicationActiveDocument =
    App::GetApplication().signalActiveDocument.connect
        (std::bind(&Gui::TaskView::TaskView::slotActiveDocument, this, sp::_1));
    connectApplicationDeleteDocument = 
    App::GetApplication().signalDeletedDocument.connect
        (std::bind(&Gui::TaskView::TaskView::slotDeletedDocument, this));
    connectApplicationUndoDocument = 
    App::GetApplication().signalUndoDocument.connect
        (std::bind(&Gui::TaskView::TaskView::slotUndoDocument, this, sp::_1));
    connectApplicationRedoDocument = 
    App::GetApplication().signalRedoDocument.connect
        (std::bind(&Gui::TaskView::TaskView::slotRedoDocument, this, sp::_1));
    if (Gui::Application::Instance) {
        // The view, not the document: two views of one document are two
        // owners (docs/TaskPanelPerView.md sec 5.1)
        connectApplicationActivateView =
        Gui::Application::Instance->signalActivateView.connect
            (std::bind(&Gui::TaskView::TaskView::slotActivateView, this, sp::_1));
        connectApplicationCloseView =
        Gui::Application::Instance->signalCloseView.connect
            (std::bind(&Gui::TaskView::TaskView::slotViewClosed, this, sp::_1));
        connectApplicationResetEdit =
        Gui::Application::Instance->signalResetEdit.connect
            (std::bind(&Gui::TaskView::TaskView::slotResetEdit, this, sp::_1));
    }
    // Ahead of every other listener, the GUI's own above all: that one
    // closes the document's views (Gui::Document::beforeDelete) before it
    // says anything itself, and a view closed with a dialog rejects it.
    connectGuiDeleteDocument =
    App::GetApplication().signalDeleteDocument.connect(
        [this](const App::Document &doc) {
            Gui::Document *gdoc = Gui::Application::Instance
                ? Gui::Application::Instance->getDocument(&doc) : nullptr;
            if (gdoc)
                slotDeleteDocument(*gdoc);
        }, fastsignals::at_front);

    this->timer = new QTimer(this);
    this->timer->setSingleShot(true);
    connect(this->timer, &QTimer::timeout, this, &TaskView::onUpdateWatcher);
    //NOLINTEND

    updateWatcher();
}

TaskView::~TaskView()
{
    connectApplicationActiveDocument.disconnect();
    connectApplicationDeleteDocument.disconnect();
    connectApplicationUndoDocument.disconnect();
    connectApplicationRedoDocument.disconnect();
    connectApplicationActivateView.disconnect();
    connectApplicationCloseView.disconnect();
    connectApplicationResetEdit.disconnect();
    connectGuiDeleteDocument.disconnect();
    Gui::SelectionRoom().Detach(this);

    if (ActiveWatcher.size()) {
        auto panel = Gui::Control().taskPanel();
        if (panel && panel != this)
            panel->takeTaskWatcher(this);
    }
    clearTaskWatcher();
}

bool TaskView::isEmpty(bool includeWatcher) const
{
    if (!taskInfos.empty())
        return false;
    // A contextual panel counts while it is shown, which is while a view
    // of its document is the active one
    for (const ContextualPanel &panel : contextualPanels) {
        if (panel.widget->parentWidget() != parking)
            return false;
    }

    if (includeWatcher) {
        for (auto * watcher : ActiveWatcher) {
            if (watcher->shouldShow())
                return false;
        }
    }
    return true;
}

bool TaskView::event(QEvent* event)
{
    // Workaround for a limitation in Qt (#0003794)
    // Line edits and spin boxes don't handle the key combination
    // Shift+Keypad button (if NumLock is activated)
    if (event->type() == QEvent::ShortcutOverride) {
        QWidget* focusWidget = qApp->focusWidget();
        bool isLineEdit = qobject_cast<QLineEdit*>(focusWidget);
        bool isSpinBox = qobject_cast<QAbstractSpinBox*>(focusWidget);

        if (isLineEdit || isSpinBox) {
            QKeyEvent * kevent = static_cast<QKeyEvent*>(event);
            Qt::KeyboardModifiers ShiftKeypadModifier = Qt::ShiftModifier | Qt::KeypadModifier;
            if (kevent->modifiers() == Qt::NoModifier ||
                kevent->modifiers() == Qt::ShiftModifier ||
                kevent->modifiers() == Qt::KeypadModifier ||
                kevent->modifiers() == ShiftKeypadModifier) {
                switch (kevent->key()) {
                case Qt::Key_Delete:
                case Qt::Key_Home:
                case Qt::Key_End:
                case Qt::Key_Backspace:
                case Qt::Key_Left:
                case Qt::Key_Right:
                    kevent->accept();
                default:
                    break;
                }
            }
        }
    }
    return QWidget::event(event);
}

void TaskView::keyPressEvent(QKeyEvent* ke)
{
    // The page that is shown, by its own pointers: a click below may
    // close the dialog and take its entry with it.
    TaskInfo *info = currentTaskInfo();
    TaskDialog *ActiveDialog = info ? info->ActiveDialog : nullptr;
    TaskEditControl *ActiveCtrl = info ? info->ActiveCtrl : nullptr;
    QWidget *page = info ? info->page : nullptr;
    if (ActiveCtrl && ActiveDialog) {
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            // spin box uses Key_Return to signal finish editing. At least for
            // PartDesign, most task panel disable spin box keyboard tracking,
            // therefore it expects the user press enter key to confirm.
            QWidget* focusWidget = qApp->focusWidget();
            if(qobject_cast<QAbstractSpinBox*>(focusWidget))
                return;

            // get all buttons of the complete task dialog
            QList<QPushButton*> list = page->findChildren<QPushButton*>();
            for (int i=0; i<list.size(); ++i) {
                QPushButton *pb = list.at(i);
                if (pb->isDefault() && pb->isVisible()) {
                    if (pb->isEnabled()) {
#if defined(FC_OS_MACOSX)
                        // #0001354: Crash on using Enter-Key for confirmation of chamfer or fillet entries
                        QPoint pos = QCursor::pos();
                        QCursor::setPos(pb->parentWidget()->mapToGlobal(pb->pos()));
#endif
                        pb->click();
#if defined(FC_OS_MACOSX)
                        QCursor::setPos(pos);
#endif
                    }
                    return;
                }
            }
        }
        else if (ke->key() == Qt::Key_Escape && ActiveDialog->isEscapeButtonEnabled()) {
            // get only the buttons of the button box
            QDialogButtonBox* box = ActiveCtrl->standardButtons();
            QList<QAbstractButton*> list = box->buttons();
            for (int i=0; i<list.size(); ++i) {
                QAbstractButton *pb = list.at(i);
                if (box->buttonRole(pb) == ActiveDialog->roleOnEscape) {
                    if (pb->isEnabled()) {
#if defined(FC_OS_MACOSX)
                        // #0001354: Crash on using Enter-Key for confirmation of chamfer or fillet entries
                        QPoint pos = QCursor::pos();
                        QCursor::setPos(pb->parentWidget()->mapToGlobal(pb->pos()));
#endif
                        pb->click();
#if defined(FC_OS_MACOSX)
                        QCursor::setPos(pos);
#endif
                    }
                    return;
                }
            }

            // In case a task panel has no Close or Cancel button
            // then invoke resetEdit() directly
            // See also ViewProvider::eventCallback
            auto func = new Gui::TimerFunction();
            func->setAutoDelete(true);
            Gui::Document* doc = Gui::Application::Instance->getDocument(ActiveDialog->getDocumentName().c_str());
            if (doc) {
                func->setFunction([doc](){
                    doc->resetEdit();
                });
                func->singleShot(0);
            }
        }
    }
    else {
        QWidget::keyPressEvent(ke);
    }
}

void TaskView::triggerMinimumSizeHint()
{
    // NOLINTNEXTLINE
    QTimer::singleShot(100, this, &TaskView::adjustMinimumSizeHint);
}

void TaskView::adjustMinimumSizeHint()
{
    QScrollArea *area = scrollarea;
    if (TaskInfo *info = currentTaskInfo())
        area = info->page->scrollarea;
    QSize ms = area->minimumSizeHint();
    area->setMinimumWidth(ms.width());
}

QSize TaskView::minimumSizeHint() const
{
    QSize ms = inherited::minimumSizeHint();
    // int spacing = 0;
    // if (QLayout* layout = taskPanel->layout()) {
    //     spacing = 2 * layout->spacing();
    // }
    //
    // ms.setWidth(taskPanel->minimumSizeHint().width() + spacing);
    return ms;
}

void TaskView::slotActiveDocument(const App::Document& doc)
{
    Q_UNUSED(doc); 
    if (watchersShown())
        updateWatcher();
}

void TaskView::slotDeletedDocument()
{
    if (watchersShown())
        updateWatcher();
}

void TaskView::slotActivateView(const Gui::MDIView *view)
{
    Q_UNUSED(view);
    showForActiveView();
    // A view that went without being closed leaves its dialog to nobody,
    // and while only one dialog may be open nothing else could be shown.
    // Not from here: this is in the middle of an activation.
    for (const TaskInfo &info : taskInfos) {
        if (!info.owner.isNull() && !info.owner.isValid()) {
            // NOLINTNEXTLINE
            QTimer::singleShot(0, this, &TaskView::closeOrphans);
            break;
        }
    }
}

void TaskView::closeLost(TaskDialog *dlg)
{
    if (dlg->isAutoCloseOnClosedView())
        dlg->autoClosedOnClosedView();
    else
        reject(dlg);
    // Whatever it answered: there is nobody left to answer it
    if (infoOf(dlg))
        removeDialog(dlg);
}

void TaskView::closeOrphans()
{
    // By the dialogs' own pointers: removing one takes its entry out
    std::vector<TaskDialog*> lost;
    for (const TaskInfo &info : taskInfos) {
        if (!info.owner.isNull() && !info.owner.isValid())
            lost.push_back(info.ActiveDialog);
    }
    for (TaskDialog *dlg : lost) {
        if (infoOf(dlg))
            closeLost(dlg);
    }
}

void TaskView::ownerClosed(const TaskOwner &owner)
{
    if (owner.isNull())
        return;
    std::vector<TaskDialog*> lost;
    for (const TaskInfo &info : taskInfos) {
        if (info.owner == owner)
            lost.push_back(info.ActiveDialog);
    }
    if (lost.empty())
        return;
    // As the going view: what a dialog does on its way out -- to the
    // selection above all -- is that view's business, and it need not be
    // the active one.
    std::optional<ViewerScope> scope;
    if (ViewerContext *context = owner.context())
        scope.emplace(context);
    for (TaskDialog *dlg : lost) {
        if (infoOf(dlg))
            closeLost(dlg);
    }
}

void TaskView::slotViewClosed(const Gui::MDIView *view)
{
    ownerClosed(TaskOwner(const_cast<Gui::MDIView*>(view)));
}

void TaskView::slotResetEdit(const Gui::ViewProviderDocumentObject &vp)
{
    // The dialog of the EDIT's view, which need not be the active one. A
    // dialog nobody owns goes by the document it names.
    Gui::Document *gdoc = vp.getDocument();
    if (!gdoc)
        return;
    const TaskOwner owner(gdoc->editingViewer());
    const std::string name = gdoc->getDocument()->getName();
    std::vector<TaskDialog*> closing;
    for (const TaskInfo &info : taskInfos) {
        TaskDialog *dlg = info.ActiveDialog;
        if (!dlg->isAutoCloseOnResetEdit())
            continue;
        if (info.owner.isNull() ? (dlg->getDocumentName().empty() || dlg->getDocumentName() == name)
                                : info.owner == owner)
            closing.push_back(dlg);
    }
    for (TaskDialog *dlg : closing) {
        if (!infoOf(dlg))
            continue;
        dlg->autoClosedOnResetEdit();
        if (infoOf(dlg))
            removeDialog(dlg);
    }
}

void TaskView::slotDeleteDocument(const Gui::Document &gdoc)
{
    const std::string name = gdoc.getDocument()->getName();
    std::vector<TaskDialog*> closing;
    for (const TaskInfo &info : taskInfos) {
        TaskDialog *dlg = info.ActiveDialog;
        if (dlg->isAutoCloseOnDeletedDocument()
                && (dlg->getDocumentName() == name || info.owner.document() == &gdoc))
            closing.push_back(dlg);
    }
    for (TaskDialog *dlg : closing) {
        if (!infoOf(dlg))
            continue;
        dlg->autoClosedOnDeletedDocument();
        if (infoOf(dlg))
            removeDialog(dlg);
    }
}

void TaskView::transactionChange()
{
    // By the dialogs' own pointers: removing one takes its entry out
    std::vector<TaskDialog*> closing;
    for (const TaskInfo &info : taskInfos) {
        if (info.ActiveDialog && info.ActiveDialog->isAutoCloseOnTransactionChange())
            closing.push_back(info.ActiveDialog);
    }
    for (TaskDialog *dlg : closing) {
        if (!infoOf(dlg))
            continue;
        dlg->autoClosedOnTransactionChange();
        removeDialog(dlg);
    }

    if (watchersShown())
        updateWatcher();
}

void TaskView::slotUndoDocument(const App::Document&)
{
    transactionChange();
}

void TaskView::slotRedoDocument(const App::Document&)
{
    transactionChange();
}

/// @cond DOXERR
void TaskView::OnChange(Gui::SelectionSingleton::SubjectType &rCaller,
                        Gui::SelectionSingleton::MessageType Reason)
{
    Q_UNUSED(rCaller); 
    std::string temp;

    if (Reason.Type == SelectionChanges::AddSelection ||
        Reason.Type == SelectionChanges::ClrSelection || 
        Reason.Type == SelectionChanges::SetSelection ||
        Reason.Type == SelectionChanges::RmvSelection) {

        if (watchersShown())
            updateWatcher();
    }

}
/// @endcond

TaskInfo *TaskView::infoOf(const TaskDialog *dlg)
{
    for (TaskInfo &info : taskInfos) {
        if (info.ActiveDialog == dlg)
            return &info;
    }
    return nullptr;
}

TaskInfo *TaskView::currentTaskInfo()
{
    QWidget *shown = stack->currentWidget();
    for (TaskInfo &info : taskInfos) {
        if (info.page == shown)
            return &info;
    }
    return nullptr;
}

const TaskInfo *TaskView::currentTaskInfo() const
{
    return const_cast<TaskView*>(this)->currentTaskInfo();
}

TaskInfo *TaskView::theTaskInfo()
{
    if (TaskInfo *info = currentTaskInfo())
        return info;
    return taskInfos.empty() ? nullptr : &taskInfos.front();
}

TaskDialog *TaskView::dialog(const TaskOwner &owner) const
{
    for (const TaskInfo &info : taskInfos) {
        if (info.owner == owner)
            return info.ActiveDialog;
    }
    return nullptr;
}

QSint::ActionPanel *TaskView::shownPanel() const
{
    if (const TaskInfo *info = currentTaskInfo())
        return info->page->panel;
    return taskPanel;
}

bool TaskView::watchersShown() const
{
    return stack->currentWidget() == scrollarea;
}

bool TaskView::showsFor(const TaskInfo &info, const TaskOwner &view)
{
    if (info.owner.isNull() || info.owner == view)
        return true;
    // An edit every view of its document takes part in is every one of
    // those views' own, and so is its panel (PerViewEdit off): asked of
    // the session, which was told when it began who it is for.
    ViewerContext *its = info.owner.context();
    ViewerContext *that = view.context();
    if (!its || !that || !its->isEditingInitiator() || !that->isEditingViewProvider())
        return false;
    EditingRoot *root = its->editingRoot();
    return root && root->isShared() && that->editingRoot() == root;
}

TaskInfo *TaskView::infoFor(const TaskOwner &view)
{
    // Nobody's first: it shows over everything
    for (TaskInfo &info : taskInfos) {
        if (info.owner.isNull())
            return &info;
    }
    for (TaskInfo &info : taskInfos) {
        if (info.owner == view)
            return &info;
    }
    for (TaskInfo &info : taskInfos) {
        if (showsFor(info, view))
            return &info;
    }
    return nullptr;
}

void TaskView::placeContextualPanels(QSint::ActionPanel *to)
{
    // A panel is for the views of one document: shown while one of them
    // is the active view, kept out of sight otherwise.
    Gui::Document *gdoc = activeOwner().document();
    App::Document *doc = gdoc ? gdoc->getDocument() : nullptr;
    // They sit alongside whatever is shown, above it; on the watchers'
    // page below the line that says where the panels are.
    auto box = qobject_cast<QBoxLayout*>(to->layout());
    int at = (box && hint->parentWidget() == to) ? box->indexOf(hint) + 1 : 0;
    for (const ContextualPanel &entry : contextualPanels) {
        QWidget *panel = entry.widget;
        const bool here = !entry.doc || entry.doc == doc;
        QWidget *home = here ? static_cast<QWidget*>(to) : parking;
        if (panel->parentWidget() == home) {
            if (here)
                ++at;
            continue;
        }
        // A new parent hides a widget: one its owner had not hidden is
        // shown again where it lands.
        const bool hidden = panel->isHidden();
        if (auto from = qobject_cast<QSint::ActionPanel*>(panel->parentWidget()))
            from->removeWidget(panel);
        if (!here) {
            panel->setParent(parking);
            continue;
        }
        if (box)
            box->insertWidget(at++, panel);
        else
            to->addWidget(panel);
        if (!hidden)
            panel->show();
    }
}

void TaskView::updateHint()
{
    // Made anew each time: a line for every dialog whose page is not the
    // one shown. They are read on the watchers' page only.
    while (QLayoutItem *item = hintRows->takeAt(0)) {
        if (QWidget *row = item->widget()) {
            row->hide();
            row->deleteLater();
        }
        delete item;
    }
    const TaskInfo *shown = currentTaskInfo();
    int rows = 0;
    for (const TaskInfo &info : taskInfos) {
        if (&info == shown)
            continue;
        ++rows;
        MDIView *view = info.owner.mdiView();
        if (!view) {
            // A view the main window cannot go to
            auto label = new QLabel(info.owner.isRemote()
                    ? tr("A remote client has a task panel open.")
                    : tr("A task panel is open for a view that is not in this window."), hint);
            label->setWordWrap(true);
            hintRows->addWidget(label);
            continue;
        }
        QString title = view->windowTitle();
        title.remove(QLatin1String("[*]"));
        auto button = new QPushButton(tr("Go to %1").arg(title), hint);
        button->setToolTip(tr("Make %1 the active view, and show its task panel").arg(title));
        // A long title is cut short rather than widening the dock
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const TaskOwner owner = info.owner;
        connect(button, &QPushButton::clicked, this, [owner] {
            MDIView *view = owner.mdiView();
            if (view && getMainWindow())
                getMainWindow()->setActiveWindow(view);
        });
        hintRows->addWidget(button);
    }
    if (hint->isHidden() == (rows > 0)) {
        hint->setVisible(rows > 0);
        triggerMinimumSizeHint();
    }
}

void TaskView::showPage(TaskInfo *info)
{
    QWidget *page = info ? static_cast<QWidget*>(info->page) : scrollarea;
    QSint::ActionPanel *panel = info ? static_cast<QSint::ActionPanel*>(info->page->panel)
                                     : taskPanel;
    if (stack->currentWidget() != page) {
        // The watchers are on their page only while it is the one shown,
        // as they were in the one shared panel only while no dialog was
        if (watchersShown())
            removeTaskWatcher();
        placeContextualPanels(panel);
        stack->setCurrentWidget(page);
        if (!info) {
            taskPanel->removeStretch();
            // put the watcher back in control
            addTaskWatcher();
        }
        Q_EMIT shownDialogChanged(info != nullptr);
        Control().signalDialogActivated(info ? info->owner : TaskOwner());
    }
    else {
        // The same page, for a view of another document perhaps
        placeContextualPanels(panel);
    }
    updateHint();
    if (info && !info->raised) {
        info->raised = true;
        Q_EMIT dialogShown();
    }
}

void TaskView::syncActivation()
{
    // A dialog is active while its page is the one shown, and a client's
    // from open() to closed(): its view is the only one its client has.
    // By the dialogs' own pointers, and looked up again each time: what a
    // dialog does when it is told may close one.
    const TaskInfo *shownInfo = currentTaskInfo();
    const TaskDialog *shown = shownInfo ? shownInfo->ActiveDialog : nullptr;
    std::vector<TaskDialog*> ending, starting;
    for (const TaskInfo &info : taskInfos) {
        const bool wanted = info.opened && (info.ActiveDialog == shown || info.owner.isRemote());
        if (info.active && !wanted)
            ending.push_back(info.ActiveDialog);
        else if (!info.active && wanted)
            starting.push_back(info.ActiveDialog);
    }
    for (TaskDialog *dlg : ending) {
        TaskInfo *info = infoOf(dlg);
        if (info && info->active) {
            info->active = false;
            dlg->deactivate();
        }
    }
    for (TaskDialog *dlg : starting) {
        TaskInfo *info = infoOf(dlg);
        if (info && !info->active) {
            info->active = true;
            dlg->activate();
        }
    }
}

void TaskView::setShownTaskInfo(TaskInfo *info)
{
    showPage(info);
    syncActivation();
}

void TaskView::showForActiveView()
{
    setShownTaskInfo(infoFor(activeOwner()));
}

void TaskView::showDialog(TaskDialog *dlg)
{
    // if trying to open the same dialog twice nothing needs to be done,
    // but to bring it to the front as the first time
    if (TaskInfo *again = infoOf(dlg)) {
        if (currentTaskInfo() == again)
            Q_EMIT dialogShown();
        return;
    }

    // Every dialog has a page of its own. That only one may be open is
    // not decided here: ControlSingleton::exclusive().
    TaskInfo info;
    info.ActiveDialog = dlg;
    info.owner = dlg->owner();
    info.page = new TaskPage(stack);
    // As narrow as the watchers' page may be now: the minimum the one
    // shared scroll area had when a dialog came up in it.
    info.page->scrollarea->setMinimumWidth(scrollarea->minimumWidth());
    TaskPanel *panel = info.page->panel;

    // first create the control element, set it up and wire it:
    info.ActiveCtrl = new TaskEditControl(info.page);
    info.ActiveCtrl->buttonBox->setStandardButtons(dlg->getStandardButtons());
    // What TaskDialogPy's accept() and reject() press
    TaskDialogAttorney::setButtonBox(dlg, info.ActiveCtrl->buttonBox);

    // clang-format off
    // make connection to the needed signals
    connect(info.ActiveCtrl->buttonBox, &QDialogButtonBox::accepted,
            this, [this, dlg] { accept(dlg); });
    connect(info.ActiveCtrl->buttonBox, &QDialogButtonBox::rejected,
            this, [this, dlg] { reject(dlg); });
    connect(info.ActiveCtrl->buttonBox, &QDialogButtonBox::helpRequested,
            this, [this, dlg] { helpRequested(dlg); });
    connect(info.ActiveCtrl->buttonBox, &QDialogButtonBox::clicked,
            this, [this, dlg](QAbstractButton *button) { clicked(button, dlg); });
    // clang-format on

    info.contents = dlg->getDialogContent();

    if (ViewParams::getTaskNoWheelFocus()) {
        // Since task dialog contains mlutiple panels which often require
        // scrolling up and down to access. Using wheel focus in any input
        // field may cause accidental change of value while scrolling.
        for (auto widget : info.contents) {
            for(auto child : widget->findChildren<QWidget*>()) {
                
                if (child->focusPolicy() == Qt::WheelFocus
                        && !qobject_cast<QAbstractItemView*>(child))
                {
                    child->setFocusPolicy(Qt::StrongFocus);
                    // It's not enough for some widget. We must use a eventFilter
                    // to actively filter out wheel event if not in focus.
                    child->installEventFilter(this);
                }
            }
        }
    }

    Control().signalShowDialog(info.page, info.contents, dlg->owner());

    // give to task dialog to customize the button box
    dlg->modifyStandardButtons(info.ActiveCtrl->buttonBox);

    if (ViewParams::getStickyTaskControl() && parentWidget()) {
        if (dlg->buttonPosition() == TaskDialog::North)
            info.page->layout->insertWidget(0, info.ActiveCtrl);
        else
            info.page->layout->addWidget(info.ActiveCtrl);
        for (auto widget : info.contents)
            panel->addWidget(widget);
    }
    else if (dlg->buttonPosition() == TaskDialog::North) {
        panel->addWidget(info.ActiveCtrl);
        for (auto widget : info.contents)
            panel->addWidget(widget);
    }
    else {
        for (auto widget : info.contents)
            panel->addWidget(widget);
        panel->addWidget(info.ActiveCtrl);
    }

    panel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());

    if (!dlg->needsFullSpace())
        panel->addStretch();

    // Shown at once when its view is the one being worked in. Otherwise
    // its page waits for that view, and the watchers' page says where it
    // is (docs/TaskPanelPerView.md sec 5.1).
    TaskPage *page = info.page;
    stack->addWidget(page);
    taskInfos.push_back(std::move(info));
    showPage(infoFor(activeOwner()));

    dlg->open();
    // Opened, then activated; open() may have closed it
    if (TaskInfo *opened = infoOf(dlg)) {
        opened->opened = true;
        syncActivation();
    }

    getMainWindow()->updateActions();

    Gui::LineEditStyle::setupChildren(page);
    triggerMinimumSizeHint();

    Q_EMIT taskUpdate();
}

bool TaskView::eventFilter(QObject *o, QEvent *ev)
{
    if (o->isWidgetType()) {
        auto widget = static_cast<QWidget*>(o);
        switch(ev->type()) {
        case QEvent::Wheel:
            if (!widget->hasFocus()) {
                ev->setAccepted(false);
                return true;
            }
            break;
        case QEvent::FocusIn:
            widget->setFocusPolicy(Qt::WheelFocus);
            break;
        case QEvent::FocusOut:
            widget->setFocusPolicy(Qt::StrongFocus);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(o, ev);
}

void TaskView::removeDialog(void)
{
    TaskInfo *info = theTaskInfo();
    removeDialog(info ? info->ActiveDialog : nullptr);
}

void TaskView::removeDialog(TaskDialog *dlg)
{
    getMainWindow()->updateActions();

    TaskInfo *info = dlg ? infoOf(dlg) : nullptr;
    if (info && info->ActiveCtrl) {
        info->page->panel->removeWidget(info->ActiveCtrl);
        delete info->ActiveCtrl;
        info->ActiveCtrl = nullptr;
    }

    TaskDialog* remove = nullptr;
    TaskPage *page = nullptr;
    if (info) {
        // See 'accept' and 'reject'
        if (dlg->property("taskview_accept_or_reject").isNull()) {
            // Its last activation ends before it is told it is closed
            if (info->active) {
                info->active = false;
                dlg->deactivate();
                info = infoOf(dlg);
            }
        }
        else {
            dlg->setProperty("taskview_remove_dialog", true);
            info = nullptr;
        }
    }

    bool wasShown = false;
    if (info) {
        Control().signalRemoveDialog(info->page, info->contents, dlg->owner());
        for (auto widget : info->contents)
            info->page->panel->removeWidget(widget);
        remove = dlg;
        page = info->page;
        wasShown = info->raised;
        taskInfos.erase(taskInfos.begin() + (info - taskInfos.data()));
        // What the active view calls for comes up: another dialog's page,
        // else the watchers', which get their place back with it
        showForActiveView();
        stack->removeWidget(page);
        page->hide();
    }

    if (remove) {
        // The page goes when the dialog has: the dialog owns its content
        // widgets and deletes them, and until then they are the page's
        // children as they were the one shared panel's.
        connect(remove, &QObject::destroyed, page, &QObject::deleteLater);
        remove->closed();
        if (wasShown && watchersShown())
            Q_EMIT shownDialogClosed();
        remove->emitDestructionSignal();
        if (getMainWindow()->isClosingAll())
            delete remove;
        else
            remove->deleteLater();
    }

    triggerMinimumSizeHint();
}

void TaskView::updateWatcher(void)
{
    this->timer->start(200);
}

void TaskView::onUpdateWatcher(void)
{
    if (!watchersShown())
        return;

    if (ActiveWatcher.empty()) {
        auto panel = Gui::Control().taskPanel();
        if (panel && panel->ActiveWatcher.size())
            takeTaskWatcher(panel);
    }
    this->timer->stop();

    // In case a child of the TaskView has the focus and get hidden we have
    // to make sure to set the focus on a widget that won't be hidden or
    // deleted because otherwise Qt may forward the focus via focusNextPrevChild()
    // to the mdi area which may switch to another mdi view which is not an
    // acceptable behaviour.
    QWidget *fw = QApplication::focusWidget();
    if (!fw)
        this->setFocus();
    QPointer<QWidget> fwp = fw;
    while (fw &&  !fw->isWindow()) {
        if (fw == this) {
            this->setFocus();
            break;
        }
        fw = fw->parentWidget();
    }

    // add all widgets for all watcher to the task view
    for (const auto & it : ActiveWatcher) {
        bool match = it->shouldShow();
        std::vector<QWidget*> &cont = it->getWatcherContent();
        for (auto & it2 : cont) {
            if (match)
                it2->show();
            else
                it2->hide();
        }
    }

    // In case the previous widget that had the focus is still visible
    // give it the focus back.
    if (fwp && fwp->isVisible())
        fwp->setFocus();

    triggerMinimumSizeHint();

    Q_EMIT taskUpdate();
}

void TaskView::addTaskWatcher(const std::vector<TaskWatcher*> &Watcher)
{
    // remove and delete the old set of TaskWatcher
    for (TaskWatcher* tw : ActiveWatcher)
        tw->deleteLater();

    ActiveWatcher = Watcher;
    if (watchersShown())
        addTaskWatcher();
}

void TaskView::takeTaskWatcher(TaskView *other)
{
    clearTaskWatcher();
    ActiveWatcher.swap(other->ActiveWatcher);
    other->clearTaskWatcher();
    if (watchersShown())
        addTaskWatcher();
}

void TaskView::clearTaskWatcher()
{
    std::vector<TaskWatcher*> watcher;
    removeTaskWatcher();
    // make sure to delete the old watchers
    addTaskWatcher(watcher);
}

void TaskView::addTaskWatcher()
{
    // add all widgets for all watcher to the task view
    for (TaskWatcher* tw : ActiveWatcher) {
        std::vector<QWidget*> &cont = tw->getWatcherContent();
        for (QWidget* w : cont) {
            taskPanel->addWidget(w);
        }
    }

    if (!ActiveWatcher.empty())
        taskPanel->addStretch();
    updateWatcher();

#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    // Workaround to avoid a crash in Qt. See also
    // https://forum.freecad.org/viewtopic.php?f=8&t=39187
    //
    // Notify the button box about a style change so that it can
    // safely delete the style animation of its push buttons.
    auto box = taskPanel->findChild<QDialogButtonBox*>();
    if (box) {
        QEvent event(QEvent::StyleChange);
        QApplication::sendEvent(box, &event);
    }
#endif

    taskPanel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());
}

void TaskView::removeTaskWatcher()
{
    // In case a child of the TaskView has the focus and get hidden we have
    // to make sure that set the focus on a widget that won't be hidden or
    // deleted because otherwise Qt may forward the focus via focusNextPrevChild()
    // to the mdi area which may switch to another mdi view which is not an
    // acceptable behaviour.
    QWidget *fw = QApplication::focusWidget();
    if (!fw)
        this->setFocus();
    while (fw &&  !fw->isWindow()) {
        if (fw == this) {
            this->setFocus();
            break;
        }
        fw = fw->parentWidget();
    }

    // remove all widgets
    for (TaskWatcher* tw : ActiveWatcher) {
        std::vector<QWidget*> &cont = tw->getWatcherContent();
        for (QWidget* w : cont) {
            w->hide();
            taskPanel->removeWidget(w);
        }
    }

    taskPanel->removeStretch();
}

void TaskView::accept()
{
    TaskInfo *info = theTaskInfo();
    accept(info ? info->ActiveDialog : nullptr);
}

void TaskView::accept(TaskDialog *dlg)
{
    if (!dlg || !infoOf(dlg)) { // Protect against segfaults due to out-of-order deletions
        Base::Console().Warning("ActiveDialog was null in call to TaskView::accept()\n");
        return;
    }

    // Make sure that if 'accept' calls 'closeDialog' the deletion is postponed until
    // the dialog leaves the 'accept' method
    dlg->setProperty("taskview_accept_or_reject", true);
    bool success = dlg->accept();
    dlg->setProperty("taskview_accept_or_reject", QVariant());
    if (success || dlg->property("taskview_remove_dialog").isValid())
        removeDialog(dlg);
}

void TaskView::reject()
{
    TaskInfo *info = theTaskInfo();
    reject(info ? info->ActiveDialog : nullptr);
}

void TaskView::reject(TaskDialog *dlg)
{
    if (!dlg || !infoOf(dlg)) { // Protect against segfaults due to out-of-order deletions
        Base::Console().Warning("ActiveDialog was null in call to TaskView::reject()\n");
        return;
    }

    // Make sure that if 'reject' calls 'closeDialog' the deletion is postponed until
    // the dialog leaves the 'reject' method
    dlg->setProperty("taskview_accept_or_reject", true);
    bool success = dlg->reject();
    dlg->setProperty("taskview_accept_or_reject", QVariant());
    if (success || dlg->property("taskview_remove_dialog").isValid())
        removeDialog(dlg);
}

void TaskView::helpRequested(TaskDialog *dlg)
{
    if (infoOf(dlg))
        dlg->helpRequested();
}

void TaskView::clicked(QAbstractButton *button, TaskDialog *dlg)
{
    TaskInfo *info = infoOf(dlg);
    if (!info || !info->ActiveCtrl)
        return;
    int id = info->ActiveCtrl->buttonBox->standardButton(button);
    dlg->clicked(id);
}

void TaskView::clearActionStyle()
{
    static_cast<QSint::FreeCADPanelScheme*>(QSint::FreeCADPanelScheme::defaultScheme())->clearActionStyle();
    taskPanel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());
}

void TaskView::restoreActionStyle()
{
    static_cast<QSint::FreeCADPanelScheme*>(QSint::FreeCADPanelScheme::defaultScheme())->restoreActionStyle();
    taskPanel->setScheme(QSint::FreeCADPanelScheme::defaultScheme());
}

void TaskView::addContextualPanel(QWidget* panel, App::Document* doc)
{
    if (!panel)
        return;
    for (const ContextualPanel &entry : contextualPanels) {
        if (entry.widget == panel)
            return;
    }

    contextualPanels.push_back({panel, doc});
    placeContextualPanels(shownPanel());
    if (panel->parentWidget() != parking)
        panel->show();
    triggerMinimumSizeHint();
    Q_EMIT taskUpdate();
}

void TaskView::removeContextualPanel(QWidget* panel, App::Document* doc)
{
    (void)doc;
    auto it = std::find_if(contextualPanels.begin(), contextualPanels.end(),
            [panel](const ContextualPanel &entry) { return entry.widget == panel; });
    if (!panel || it == contextualPanels.end())
        return;

    if (auto in = qobject_cast<QSint::ActionPanel*>(panel->parentWidget()))
        in->removeWidget(panel);
    contextualPanels.erase(it);
    panel->deleteLater();
    triggerMinimumSizeHint();
    Q_EMIT taskUpdate();
}

#include "moc_TaskView.cpp"
