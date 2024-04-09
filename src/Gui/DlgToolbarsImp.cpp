/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QHeaderView>
# include <QInputDialog>
# include <QMenu>
# include <QMessageBox>
# include <QToolBar>
# include <QToolButton>
#endif

#include <set>
#include <boost/algorithm/string/predicate.hpp>

#include <Base/Tools.h>
#include "DlgKeyboardImp.h"
#include "DlgToolbarsImp.h"
#include "DlgKeyboardImp.h"
#include "ui_DlgToolbars.h"
#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "Action.h"
#include "ToolBarManager.h"
#include "MainWindow.h"
#include "ToolBarManager.h"
#include "Widgets.h"
#include "PieMenu.h"
#include "Workbench.h"
#include "WorkbenchManager.h"
#include "Action.h"
#include "PrefWidgets.h"
#include "ShortcutManager.h"


using namespace Gui::Dialog;

/* TRANSLATOR Gui::Dialog::DlgCustomToolbars */

/**
 *  Constructs a DlgCustomToolbars which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgCustomToolbars::DlgCustomToolbars(DlgCustomToolbars::Type t, QWidget* parent)
    : CustomizeActionPage(parent)
    , ui(new Ui_DlgCustomToolbars)
    , type(t)
    , widgetStates(new Gui::PrefWidgetStates(this, false))
{
    ui->setupUi(this);

    ui->deleteButton->setEnabled(false);
    ui->renameButton->setEnabled(false);

    widgetStates->addSplitter(ui->splitter);
    widgetStates->addSplitter(ui->splitterVertical);

    setupConnections();

    ui->moveActionRightButton->setIcon(BitmapFactory().iconFromTheme("button_right"));
    ui->moveActionLeftButton->setIcon(BitmapFactory().iconFromTheme("button_left"));
    ui->moveActionDownButton->setIcon(BitmapFactory().iconFromTheme("button_down"));
    ui->moveActionUpButton->setIcon(BitmapFactory().iconFromTheme("button_up"));

    auto sepItem = new QTreeWidgetItem;
    sepItem->setText(1, tr("<Separator>"));
    sepItem->setData(1, Qt::UserRole, QByteArray("Separator"));
    sepItem->setSizeHint(0, QSize(32, 32));

    conn = DlgCustomKeyboardImp::initCommandWidgets(ui->commandTreeWidget,
                                                    sepItem,
                                                    ui->categoryBox,
                                                    ui->editCommand,
                                                    ui->assignedTreeWidget,
                                                    ui->buttonUp,
                                                    ui->buttonDown,
                                                    ui->editShortcut);

    onToolbarTreeWidgetCurrentItemChanged(nullptr, nullptr);

    // fills the combo box with all available workbenches
    QStringList workbenches = Application::Instance->workbenches();
    workbenches.sort();
    int index = 1;
    ui->workbenchBox->addItem(QApplication::windowIcon(), tr("Global"));
    ui->workbenchBox->setItemData(0, QVariant(QStringLiteral("Global")), Qt::UserRole);
    for (const auto & workbench : workbenches) {
        QPixmap px = Application::Instance->workbenchIcon(workbench);
        QString mt = Application::Instance->workbenchMenuText(workbench);
        if (mt != QStringLiteral("<none>")) {
            if (px.isNull())
                ui->workbenchBox->addItem(mt);
            else
                ui->workbenchBox->addItem(px, mt);
            ui->workbenchBox->setItemData(index, QVariant(workbench), Qt::UserRole);
            index++;
        }
    }

    QStringList labels;
    labels << tr("Name") << tr("Shortcut") << tr("Pie Menu");
    ui->toolbarTreeWidget->setColumnCount(3);
    ui->toolbarTreeWidget->setHeaderLabels(labels);
    ui->toolbarTreeWidget->header()->show();
    ui->toolbarTreeWidget->header()->setStretchLastSection(false);
#if QT_VERSION >= 0x050000
    ui->toolbarTreeWidget->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
#else
    ui->toolbarTreeWidget->header()->setResizeMode(QHeaderView::ResizeToContents);
#endif

    Workbench* w = WorkbenchManager::instance()->active();
    if (w) {
        QString name = QString::fromUtf8(w->name().c_str());
        int index = ui->workbenchBox->findData(name);
        ui->workbenchBox->setCurrentIndex(index);
    } else
        ui->workbenchBox->setCurrentIndex(0);

    onWorkbenchBoxActivated(ui->workbenchBox->currentIndex());
}

/** Destroys the object and frees any allocated resources */
DlgCustomToolbars::~DlgCustomToolbars() = default;

void DlgCustomToolbars::setupConnections()
{
    connect(ui->workbenchBox, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &DlgCustomToolbars::onWorkbenchBoxActivated);
    connect(ui->moveActionRightButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onMoveActionRightButtonClicked);
    connect(ui->moveActionLeftButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onMoveActionLeftButtonClicked);
    connect(ui->moveActionUpButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onMoveActionUpButtonClicked);
    connect(ui->moveActionDownButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onMoveActionDownButtonClicked);
    connect(ui->newButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onNewButtonClicked);
    connect(ui->renameButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onRenameButtonClicked);
    connect(ui->deleteButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onDeleteButtonClicked);
    connect(ui->recentButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onRecentButtonClicked);
    connect(ui->assignButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onAssignButtonClicked);
    connect(ui->resetButton, &QPushButton::clicked,
            this, &DlgCustomToolbars::onResetButtonClicked);
    connect(ui->toolbarTreeWidget, &QTreeWidget::currentItemChanged,
            this, &DlgCustomToolbars::onToolbarTreeWidgetCurrentItemChanged);
    connect(ui->toolbarTreeWidget, &QTreeWidget::itemChanged,
            this, &DlgCustomToolbars::onToolbarTreeWidgetItemChanged);
    connect(ui->editShortcut, &QLineEdit::textChanged,
            this, &DlgCustomToolbars::onEditShortcutTextChanged);
}

void DlgCustomToolbars::addCustomToolbar(QString, const QString&)
{
}

void DlgCustomToolbars::removeCustomToolbar(QString)
{
}

void DlgCustomToolbars::renameCustomToolbar(QString, const QString&)
{
}

void DlgCustomToolbars::addCustomCommand(QString, const QByteArray&)
{
}

void DlgCustomToolbars::removeCustomCommand(QString, const QByteArray&)
{
}

void DlgCustomToolbars::moveUpCustomCommand(QString, const QByteArray&)
{
}

void DlgCustomToolbars::moveDownCustomCommand(QString, const QByteArray&)
{
}

void DlgCustomToolbars::hideEvent(QHideEvent * event)
{
    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());

    CustomizeActionPage::hideEvent(event);
}

void DlgCustomToolbars::onWorkbenchBoxActivated(int index)
{
    QVariant data = ui->workbenchBox->itemData(index, Qt::UserRole);
    QString workbench = data.toString();
    ui->toolbarTreeWidget->clear();

    ui->recentButton->setEnabled(index==0);

    QByteArray workbenchname = workbench.toUtf8();
    importCustomToolbars(workbenchname);
}

void DlgCustomToolbars::onRecentButtonClicked()
{
    std::set<QTreeWidgetItem*> items;
    for (int i=0;i<ui->toolbarTreeWidget->topLevelItemCount();++i)
        items.insert(ui->toolbarTreeWidget->topLevelItem(i));

    onNewButtonClicked();

    for (int i=0;i<ui->toolbarTreeWidget->topLevelItemCount();++i) {
        QTreeWidgetItem *item = ui->toolbarTreeWidget->topLevelItem(i);
        if (items.count(item))
            continue;

        for (Command *cmd : CmdHistoryAction::recentCommands()) {
            QTreeWidgetItem *cmdItem = new QTreeWidgetItem(item);
            cmdItem->setText(0, qApp->translate(cmd->className(), cmd->getMenuText()));
            if (cmd->getPixmap())
                cmdItem->setIcon(0, BitmapFactory().iconFromTheme(cmd->getPixmap()));

            QByteArray data(cmd->getName());
            cmdItem->setData(0, Qt::UserRole, data);
            addCustomCommand(item->data(0, Qt::UserRole).toString(), data);
        }
        QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
        QString workbench = data.toString();
        exportCustomToolbars(workbench.toUtf8());
        break;
    }
}

struct GroupComp
{
    bool operator()(const ParameterGrp::handle &a, const ParameterGrp::handle &b) const
    {
        const char *na = a->GetGroupName();
        const char *nb = b->GetGroupName();
        return strcmp(na, nb) < 0;
    }
};

void DlgCustomToolbars::importCustomToolbars(const QByteArray& name)
{
    Application::Instance->initializeWorkbench(name);

    ParameterGrp::handle hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")->GetGroup("Workbench");
    const char* subgroup = (type == Toolbar ? "Toolbar" : "Toolboxbar");
    if (!hGrp->HasGroup(name.constData()))
        return;
    hGrp = hGrp->GetGroup(name.constData());
    if (!hGrp->HasGroup(subgroup))
        return;
    hGrp = hGrp->GetGroup(subgroup);
    std::string separator = "Separator";

    std::multiset<ParameterGrp::handle, GroupComp> hGrps;
    for (auto &h : hGrp->GetGroups())
        hGrps.insert(h);

    ParameterGrp::handle hShortcut = WindowParameter::getDefaultParameter()->GetGroup("Shortcut");

    QSignalBlocker blocker(ui->toolbarTreeWidget);

    CommandManager& rMgr = Application::Instance->commandManager();
    for (const auto &h : hGrps) {
        QString toolbarName = QString::fromUtf8(h->GetASCII("Name","").c_str());
        if (toolbarName.isEmpty()) {
            hGrp->RemoveGrp(h->GetGroupName());
            continue;
        }

        // create a toplevel item
        QTreeWidgetItem* toplevel = new QTreeWidgetItem(ui->toolbarTreeWidget);
        toplevel->setText(0, toolbarName);

        QString id = QString::fromUtf8(h->GetGroupName());
        toplevel->setData(0, Qt::UserRole, id);

        bool active = h->GetBool("Active", true);

        if (ToolBarManager::isCustomToolBarName(h->GetGroupName()))
            id = ToolBarManager::generateToolBarID(name.constData(), h->GetGroupName());

        auto toolbar = getMainWindow()->findChild<QToolBar*>(id);
        if (toolbar)
            active = toolbar->toggleViewAction()->isVisible();
        toplevel->setCheckState(0, (active ? Qt::Checked : Qt::Unchecked));

        std::string paramName = ToolbarMenuAction::paramName(h->GetGroupName(), name.constData());
        toplevel->setCheckState(2, (PieMenu::isEnabled(paramName.c_str()) ? Qt::Checked : Qt::Unchecked));

        std::string shortcut = hShortcut->GetASCII(paramName.c_str());
        toplevel->setText(1, QString::fromUtf8(shortcut.c_str()));
        // get the elements of the subgroups
        std::vector<std::pair<std::string,std::string> > items = h->GetASCIIMap();
        for (const auto & it2 : items) {
            // since we have stored the separators to the user parameters as (key, pair) we had to
            // make sure to use a unique key because otherwise we cannot store more than
            // one.
            if (it2.first.substr(0, separator.size()) == separator) {
                auto item = new QTreeWidgetItem(toplevel);
                item->setText(0, tr("<Separator>"));
                item->setData(0, Qt::UserRole, QByteArray("Separator"));
            }
            else if (it2.first == "Name") {
                continue;
            } else {
                Command* pCmd = rMgr.getCommandByName(it2.first.c_str());
                if (pCmd) {
                    // command name
                    QTreeWidgetItem* item = new QTreeWidgetItem(toplevel);
                    item->setText(0, Action::commandMenuText(pCmd));
                    item->setToolTip(0, Action::commandToolTip(pCmd));
                    item->setData(0, Qt::UserRole, QByteArray(it2.first.c_str()));
                    if (pCmd->getPixmap())
                        item->setIcon(0, BitmapFactory().iconFromTheme(pCmd->getPixmap()));
                }
                else {
                    // If corresponding module is not yet loaded do not lose the entry
                    QTreeWidgetItem* item = new QTreeWidgetItem(toplevel);
                    item->setText(0, tr("%1:%2 (command not found)").arg(
                                QString::fromStdString(it2.second), QString::fromStdString(it2.first)));
                    item->setData(0, Qt::UserRole, QByteArray(it2.first.c_str()));
                    item->setData(0, Qt::WhatsThisPropertyRole, QByteArray(it2.second.c_str()));
                }
            }
        }
    }
}

void DlgCustomToolbars::exportCustomToolbars(const QByteArray& workbench, QTreeWidgetItem *item)
{
    ParameterGrp::handle hGrp = App::GetApplication().GetUserParameter().GetGroup("BaseApp")->GetGroup("Workbench");
    const char* subgroup = (type == Toolbar ? "Toolbar" : "Toolboxbar");
    hGrp = hGrp->GetGroup(workbench.constData())->GetGroup(subgroup);
    hGrp->Clear();

    CommandManager& rMgr = Application::Instance->commandManager();
    for (int i=0; i<ui->toolbarTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem* toplevel = ui->toolbarTreeWidget->topLevelItem(i);
        if (item && toplevel != item)
            continue;
        QString groupName = toplevel->data(0, Qt::UserRole).toString();
        QString toolbarName = toplevel->text(0);

        ParameterGrp::handle hToolGrp = hGrp->GetGroup(groupName.toUtf8());
        hToolGrp->SetASCII("Name", toolbarName.toUtf8().constData());
        bool checked = (toplevel->checkState(0) == Qt::Checked);
        hToolGrp->SetBool("Active", checked);

        std::string cmdName = ToolbarMenuAction::paramName(
                groupName.toUtf8().constData(), workbench.constData());
        PieMenu::setEnabled(cmdName.c_str(), toplevel->checkState(2)==Qt::Checked);

        QString shortcut = toplevel->text(1);
        ShortcutManager::instance()->setShortcut(cmdName.c_str(), shortcut.toUtf8().constData());

        // since we store the separators to the user parameters as (key, pair) we must
        // make sure to use a unique key because otherwise we cannot store more than
        // one.
        int suffixSeparator = 1;
        for (int j=0; j<toplevel->childCount(); j++) {
            QTreeWidgetItem* child = toplevel->child(j);
            QByteArray commandName = child->data(0, Qt::UserRole).toByteArray();
            if (commandName == "Separator") {
                QByteArray key = commandName + QByteArray::number(suffixSeparator);
                suffixSeparator++;
                hToolGrp->SetASCII(key, commandName);
            }
            else {
                Command* pCmd = rMgr.getCommandByName(commandName);
                if (pCmd) {
                    hToolGrp->SetASCII(pCmd->getName(), pCmd->getAppModuleName());
                }
                else {
                    QByteArray moduleName = child->data(0, Qt::WhatsThisPropertyRole).toByteArray();
                    hToolGrp->SetASCII(commandName, moduleName);
                }
            }
        }
    }

    if (!item)
        ToolbarMenuAction::populate();
}

/** Adds a new action */
void DlgCustomToolbars::onMoveActionRightButtonClicked()
{
    QTreeWidgetItem* item = ui->commandTreeWidget->currentItem();
    if (item) {
        QTreeWidgetItem* current = ui->toolbarTreeWidget->currentItem();
        if (!current)
            current = ui->toolbarTreeWidget->topLevelItem(0);
        else if (current->parent())
            current = current->parent();
        if (current && !current->parent()) {
            current->setExpanded(true);
            auto copy = new QTreeWidgetItem(current);
            copy->setText(0, item->text(1));
            copy->setIcon(0, item->icon(0));
            QByteArray data = item->data(1, Qt::UserRole).toByteArray();
            copy->setData(0, Qt::UserRole, data);
            ui->toolbarTreeWidget->scrollToItem(copy);
            addCustomCommand(current->data(0, Qt::UserRole).toString(), data);
        }
    }

    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());
}

/** Removes an action */
void DlgCustomToolbars::onMoveActionLeftButtonClicked()
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (item && item->parent() && item->isSelected()) {
        QTreeWidgetItem* parent = item->parent();
        int index = parent->indexOfChild(item);
        parent->takeChild(index);

        // In case a separator should be moved we have to count the separators
        // which come before this one.
        // This is needed so that we can distinguish in removeCustomCommand
        // which separator it is.
        QByteArray data = item->data(0, Qt::UserRole).toByteArray();
        if (data == "Separator") {
            int countSep = 1;
            for (int i=0; i<index-1; i++) {
                QByteArray d = parent->child(i)->data(0, Qt::UserRole).toByteArray();
                if (d == "Separator") {
                    countSep++;
                }
            }

            data += QByteArray::number(countSep);
        }
        removeCustomCommand(parent->data(0, Qt::UserRole).toString(), data);
        delete item;
    }

    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());
}

/** Moves up an action */
void DlgCustomToolbars::onMoveActionUpButtonClicked()
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (item && item->parent() && item->isSelected()) {
        QTreeWidgetItem* parent = item->parent();
        int index = parent->indexOfChild(item);
        if (index > 0) {
            // In case a separator should be moved we have to count the separators
            // which come before this one.
            // This is needed so that we can distinguish in moveUpCustomCommand
            // which separator it is.
            QByteArray data = item->data(0, Qt::UserRole).toByteArray();
            if (data == "Separator") {
                int countSep = 1;
                for (int i=0; i<index; i++) {
                    QByteArray d = parent->child(i)->data(0, Qt::UserRole).toByteArray();
                    if (d == "Separator") {
                        countSep++;
                    }
                }

                data += QByteArray::number(countSep);
            }

            parent->takeChild(index);
            parent->insertChild(index-1, item);
            ui->toolbarTreeWidget->setCurrentItem(item);

            moveUpCustomCommand(parent->data(0, Qt::UserRole).toString(), data);
        }
    }

    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());
}

/** Moves down an action */
void DlgCustomToolbars::onMoveActionDownButtonClicked()
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (item && item->parent() && item->isSelected()) {
        QTreeWidgetItem* parent = item->parent();
        int index = parent->indexOfChild(item);
        if (index < parent->childCount()-1) {
            // In case a separator should be moved we have to count the separators
            // which come before this one.
            // This is needed so that we can distinguish in moveDownCustomCommand
            // which separator it is.
            QByteArray data = item->data(0, Qt::UserRole).toByteArray();
            if (data == "Separator") {
                int countSep = 1;
                for (int i=0; i<index; i++) {
                    QByteArray d = parent->child(i)->data(0, Qt::UserRole).toByteArray();
                    if (d == "Separator") {
                        countSep++;
                    }
                }

                data += QByteArray::number(countSep);
            }

            parent->takeChild(index);
            parent->insertChild(index+1, item);
            ui->toolbarTreeWidget->setCurrentItem(item);

            moveDownCustomCommand(parent->data(0, Qt::UserRole).toString(), data);
        }
    }

    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());
}

void DlgCustomToolbars::onNewButtonClicked()
{
    bool ok;

    std::string name("Custom");

    std::vector<std::string> reserved;
    for (int i=0; i<ui->toolbarTreeWidget->topLevelItemCount(); i++) {
        QTreeWidgetItem *item = ui->toolbarTreeWidget->topLevelItem(i);
        reserved.push_back(item->data(0, Qt::UserRole).toString().toUtf8().constData());
    }
    name = Base::Tools::getUniqueName(name, reserved, 3);
    QString id = QString::fromUtf8(name.c_str());

    QString text = QInputDialog::getText(this, tr("New toolbar"), tr("Toolbar name:"),
            QLineEdit::Normal, id, &ok);
    if (ok) {
        int i = 0;
        for (i=0; i<ui->toolbarTreeWidget->topLevelItemCount(); i++) {
            QTreeWidgetItem* toplevel = ui->toolbarTreeWidget->topLevelItem(i);
            if(id < toplevel->data(0, Qt::UserRole).toString())
                break;
        }

        QSignalBlocker blocker(ui->toolbarTreeWidget);
        auto item = new QTreeWidgetItem;
        ui->toolbarTreeWidget->insertTopLevelItem(i, item);
        item->setText(0, text);
        item->setData(0, Qt::UserRole, id);
        item->setCheckState(0, Qt::Checked);
        item->setCheckState(2, Qt::Checked);
        item->setExpanded(true);
        item->setSelected(true);
        ui->toolbarTreeWidget->setCurrentItem(item);
        ui->toolbarTreeWidget->scrollToItem(item);

        QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
        QString workbench = data.toString();
        exportCustomToolbars(workbench.toUtf8());
        addCustomToolbar(id, text);
    }
}

void DlgCustomToolbars::onDeleteButtonClicked()
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (item && !item->parent() && item->isSelected()) {
        int index = ui->toolbarTreeWidget->indexOfTopLevelItem(item);
        ui->toolbarTreeWidget->takeTopLevelItem(index);
        removeCustomToolbar(item->data(0, Qt::UserRole).toString());
        delete item;
    }

    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());
}

void DlgCustomToolbars::onRenameButtonClicked()
{
    bool renamed = false;
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (item && !item->parent() && item->isSelected()) {
        bool ok;
        QString old_text = item->text(0);
        QString text = QInputDialog::getText(this, tr("Rename toolbar"), tr("Toolbar name:"),
            QLineEdit::Normal, old_text, &ok, Qt::MSWindowsFixedSizeDialogHint);
        if (ok && text != old_text) {
            // Check for duplicated name
            for (int i=0; i<ui->toolbarTreeWidget->topLevelItemCount(); i++) {
                QTreeWidgetItem* toplevel = ui->toolbarTreeWidget->topLevelItem(i);
                QString groupName = toplevel->text(0);
                if (groupName == text && toplevel != item) {
                    QMessageBox::warning(this, tr("Duplicated name"), tr("The toolbar name '%1' is already used").arg(text));
                    return;
                }
            }

            item->setText(0, text);
            renameCustomToolbar(item->data(0, Qt::UserRole).toString(), text);
            renamed = true;
        }
    }

    if (renamed) {
        QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
        QString workbench = data.toString();
        exportCustomToolbars(workbench.toUtf8());
    }
}

void DlgCustomToolbars::onEditShortcutTextChanged(const QString& sc)
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (!item || item->parent() == ui->toolbarTreeWidget->invisibleRootItem())
        return;

    QString shortcut = item->text(1);
    ui->assignButton->setEnabled(shortcut != sc);
    ui->resetButton->setEnabled(shortcut != sc);
}

void DlgCustomToolbars::onToolbarTreeWidgetItemChanged(QTreeWidgetItem *item, int)
{
    if (item && !item->parent()) {
        QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);
        exportCustomToolbars(data.toString().toUtf8(), item);
    }
    QString id = item->data(0, Qt::UserRole).toString();
    if (!checkWorkbench(&id))
        return;
    auto toolbar = getMainWindow()->findChild<QToolBar*>(id);
    bool checked = (item->checkState(0) == Qt::Checked);
    if (toolbar && toolbar->isVisible() != checked)
        toolbar->toggleViewAction()->trigger();
}

void DlgCustomToolbars::onToolbarTreeWidgetCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *)
{
    bool enabled = current && !current->parent();
    ui->assignButton->setEnabled(enabled);
    ui->resetButton->setEnabled(enabled);
    ui->editShortcut->setEnabled(enabled);
    ui->labelShortcut->setEnabled(enabled);
    ui->deleteButton->setEnabled(enabled);
    ui->renameButton->setEnabled(enabled);

    if (enabled) {
        QString shortcut = current->text(1);
        ui->editShortcut->setText(shortcut);
        ui->assignButton->setEnabled(false);
        ui->resetButton->setEnabled(false);
    }
    else
        ui->editShortcut->clear();
}

void DlgCustomToolbars::onAssignButtonClicked()
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (!item || item->parent() == ui->toolbarTreeWidget->invisibleRootItem())
        return;

    QString shortcut;
    if (!ui->editShortcut->isNone())
        shortcut = ui->editShortcut->text();

    if (shortcut == item->text(1)) {
        ui->assignButton->setEnabled(false);
        ui->resetButton->setEnabled(false);
        return;
    }

    QVariant data = ui->workbenchBox->itemData(ui->workbenchBox->currentIndex(), Qt::UserRole);

    std::string prefix, cmdPrefix;
    if (ui->workbenchBox->currentIndex() != 0) {
        std::string id = item->data(0, Qt::UserRole).toString().toUtf8().constData();
        std::string workbenchName = data.toByteArray().constData();
        prefix = ToolbarMenuAction::paramName("","");
        cmdPrefix = ToolbarMenuAction::paramName("",workbenchName.c_str());
    }

    item->setText(1, shortcut);

    QString workbench = data.toString();
    exportCustomToolbars(workbench.toUtf8());

    onResetButtonClicked();
}

void DlgCustomToolbars::onResetButtonClicked()
{
    QTreeWidgetItem* item = ui->toolbarTreeWidget->currentItem();
    if (!item || item->parent() == ui->toolbarTreeWidget->invisibleRootItem())
        return;

    QString shortcut = item->text(1);
    ui->editShortcut->setText(shortcut);
    ui->resetButton->setEnabled(false);
    ui->assignButton->setEnabled(false);
}

void DlgCustomToolbars::onAddMacroAction(const QByteArray&)
{
}

void DlgCustomToolbars::onRemoveMacroAction(const QByteArray&)
{
}

void DlgCustomToolbars::onModifyMacroAction(const QByteArray& macro)
{
    QVariant data = ui->categoryBox->itemData(ui->categoryBox->currentIndex(), Qt::UserRole);
    QString group = data.toString();
    if (group == QStringLiteral("Macros"))
    {
        CommandManager & cCmdMgr = Application::Instance->commandManager();
        Command* pCmd = cCmdMgr.getCommandByName(macro);
        // the right side
        for (int i=0; i<ui->toolbarTreeWidget->topLevelItemCount(); i++) {
            QTreeWidgetItem* toplevel = ui->toolbarTreeWidget->topLevelItem(i);
            for (int j=0; j<toplevel->childCount(); j++) {
                QTreeWidgetItem* item = toplevel->child(j);
                QByteArray command = item->data(0, Qt::UserRole).toByteArray();
                if (command == macro) {
                    item->setText(0, Action::commandMenuText(pCmd));
                    item->setToolTip(0, Action::commandToolTip(pCmd));
                    if (pCmd->getPixmap())
                        item->setIcon(0, BitmapFactory().iconFromTheme(pCmd->getPixmap()));
                }
            }
        }
        ui->categoryBox->activated(ui->categoryBox->currentIndex());
    }
}

void DlgCustomToolbars::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        int count = ui->categoryBox->count();

        CommandManager & cCmdMgr = Application::Instance->commandManager();
        for (int i=0; i<count; i++) {
            QVariant data = ui->categoryBox->itemData(i, Qt::UserRole);
            std::vector<Command*> aCmds = cCmdMgr.getGroupCommands(data.toByteArray());
            if (!aCmds.empty()) {
                QString text = aCmds[0]->translatedGroupName();
                ui->categoryBox->setItemText(i, text);
            }
        }
        ui->categoryBox->activated(ui->categoryBox->currentIndex());
    }
    else if (e->type() == QEvent::StyleChange) {
        ui->categoryBox->activated(ui->categoryBox->currentIndex());
    }

    QWidget::changeEvent(e);
}

// -------------------------------------------------------------

/* TRANSLATOR Gui::Dialog::DlgCustomToolbarsImp */

/**
 *  Constructs a DlgCustomToolbarsImp which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgCustomToolbarsImp::DlgCustomToolbarsImp( QWidget* parent )
    : DlgCustomToolbars(DlgCustomToolbars::Toolbar, parent)
{
}

/** Destroys the object and frees any allocated resources */
DlgCustomToolbarsImp::~DlgCustomToolbarsImp() = default;

void DlgCustomToolbarsImp::addCustomToolbar(QString id, const QString& name)
{
    if (checkWorkbench(&id)) {
        QToolBar* bar = getMainWindow()->addToolBar(name);
        bar->setObjectName(id);
    }
}

bool DlgCustomToolbars::checkWorkbench(QString *id) const
{
    QByteArray data = ui->workbenchBox->itemData(
            ui->workbenchBox->currentIndex(), Qt::UserRole).toByteArray();

    if (ui->workbenchBox->currentIndex() != 0) { // non global toolbar
        Workbench* w = WorkbenchManager::instance()->active();
        if(!w  || w->name() != data.constData())
            return false;
    }

    if (id && ToolBarManager::isCustomToolBarName(id->toUtf8().constData()))
        *id = QStringLiteral("Custom_%1_%2").arg(
                QString::fromUtf8(data.constData()), *id);
    return true;
}

void DlgCustomToolbarsImp::removeCustomToolbar(QString id)
{
    checkWorkbench(&id);
    ToolBarManager::getInstance()->removeToolBar(id);
}

void DlgCustomToolbarsImp::renameCustomToolbar(QString id, const QString& new_name)
{
    if (checkWorkbench(&id)) {
        auto tb = getMainWindow()->findChild<QToolBar*>(id);
        if (tb)
            tb->setWindowTitle(new_name);
    }
}

QList<QAction*> DlgCustomToolbarsImp::getActionGroup(QAction* action)
{
    QList<QAction*> group;
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QList<QWidget*> widgets = action->associatedWidgets();
#else
    QList<QObject*> widgets = action->associatedObjects();
#endif
    for (const auto & widget : widgets) {
        auto tb = qobject_cast<QToolButton*>(widget);
        if (tb) {
            QMenu* menu = tb->menu();
            if (menu) {
                group = menu->actions();
                break;
            }
        }
    }
    return group;
}

void DlgCustomToolbarsImp::setActionGroup(QAction* action, const QList<QAction*>& group)
{
    // See also ActionGroup::addTo()
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QList<QWidget*> widgets = action->associatedWidgets();
#else
    QList<QObject*> widgets = action->associatedObjects();
#endif
    for (const auto & widget : widgets) {
        auto tb = qobject_cast<QToolButton*>(widget);
        if (tb) {
            QMenu* menu = tb->menu();
            if (!menu) {
                tb->setPopupMode(QToolButton::MenuButtonPopup);
                tb->setObjectName(QStringLiteral("qt_toolbutton_menubutton"));
                auto menu = new QMenu(tb);
                menu->addActions(group);
                tb->setMenu(menu);
            }
        }
    }
}

void DlgCustomToolbarsImp::addCustomCommand(QString id, const QByteArray& cmd)
{
    if (checkWorkbench(&id)) {
        auto tb = getMainWindow()->findChild<QToolBar*>(id);
        if (!tb)
            return;

        if (cmd == "Separator") {
            QAction* action = tb->addSeparator();
            action->setData(QByteArray("Separator"));
        }
        else {
            CommandManager& mgr = Application::Instance->commandManager();
            if (mgr.addTo(cmd, tb)) {
                QAction* action = tb->actions().last();
                // See ToolBarManager::setup(ToolBarItem* , QToolBar* )
                // We have to add the user data in order to identify the command in
                // removeCustomCommand(), moveUpCustomCommand() or moveDownCustomCommand()
                if (action && action->data().isNull())
                    action->setData(cmd);
            }
        }
    }
}

void DlgCustomToolbarsImp::removeCustomCommand(QString id, const QByteArray& userdata)
{
    if (checkWorkbench(&id)) {
        auto tb = getMainWindow()->findChild<QToolBar*>(id);
        if (!tb)
            return;

        QByteArray cmd = userdata;
        int numSep = 0, indexSep = 0;
        if (cmd.startsWith("Separator")) {
            numSep = cmd.mid(9).toInt();
            cmd = "Separator";
        }
        QList<QAction*> actions = tb->actions();
        for (const auto & action : actions) {
            if (action->data().toByteArray() == cmd) {
                // if we move a separator then make sure to pick up the right one
                if (numSep > 0) {
                    if (++indexSep < numSep)
                        continue;
                }
                tb->removeAction(action);
                break;
            }
        }
    }
}

void DlgCustomToolbarsImp::moveUpCustomCommand(QString id, const QByteArray& userdata)
{
    if (checkWorkbench(&id)) {
        auto tb = getMainWindow()->findChild<QToolBar*>(id);
        if (!tb)
            return;

        QByteArray cmd = userdata;
        int numSep = 0, indexSep = 0;
        if (cmd.startsWith("Separator")) {
            numSep = cmd.mid(9).toInt();
            cmd = "Separator";
        }
        QList<QAction*> actions = tb->actions();
        QAction* before=nullptr;
        for (const auto & action : actions) {
            if (action->data().toByteArray() == cmd) {
                // if we move a separator then make sure to pick up the right one
                if (numSep > 0) {
                    if (++indexSep < numSep) {
                        before = action;
                        continue;
                    }
                }
                if (before) {
                    QList<QAction*> group = getActionGroup(action);
                    tb->removeAction(action);
                    tb->insertAction(before, action);
                    if (!group.isEmpty())
                        setActionGroup(action, group);
                    break;
                }
            }

            before = action;
        }
    }
}

void DlgCustomToolbarsImp::moveDownCustomCommand(QString id, const QByteArray& userdata)
{
    if (checkWorkbench(&id)) {
        auto tb = getMainWindow()->findChild<QToolBar*>(id);
        if (!tb)
            return;

        QByteArray cmd = userdata;
        int numSep = 0, indexSep = 0;
        if (cmd.startsWith("Separator")) {
            numSep = cmd.mid(9).toInt();
            cmd = "Separator";
        }
        QList<QAction*> actions = tb->actions();
        for (QList<QAction*>::ConstIterator it = actions.cbegin(); it != actions.cend(); ++it) {
            if ((*it)->data().toByteArray() == cmd) {
                // if we move a separator then make sure to pick up the right one
                if (numSep > 0) {
                    if (++indexSep < numSep)
                        continue;
                }
                QAction* act = *it;
                if (*it == actions.back())
                    break; // we're already on the last element
                ++it;
                // second last item
                if (*it == actions.back()) {
                    QList<QAction*> group = getActionGroup(act);
                    tb->removeAction(act);
                    tb->addAction(act);
                    if (!group.isEmpty())
                        setActionGroup(act, group);
                    break;
                }
                ++it;
                QList<QAction*> group = getActionGroup(act);
                tb->removeAction(act);
                tb->insertAction(*it, act);
                if (!group.isEmpty())
                    setActionGroup(act, group);
                break;
            }
        }
    }
}

void DlgCustomToolbarsImp::changeEvent(QEvent *e)
{
    DlgCustomToolbars::changeEvent(e);
}

void DlgCustomToolbarsImp::createRecentToolbar()
{
    for (QWidget *w=this; w; w=w->parentWidget()) {
        auto tabWidget = qobject_cast<QTabWidget*>(w->parentWidget());
        if (tabWidget) {
            tabWidget->setCurrentWidget(this);
            break;
        }
    }

    Workbench *wb = nullptr;
    if (QApplication::queryKeyboardModifiers() == Qt::ShiftModifier) {
        wb = WorkbenchManager::instance()->active();
        if (wb) {
            QString name = QString::fromUtf8(wb->name().c_str());
            int index = ui->workbenchBox->findData(name);
            ui->workbenchBox->setCurrentIndex(index);
        }
    }
    if (!wb)
        ui->workbenchBox->setCurrentIndex(0);

    onRecentButtonClicked();
}

/* TRANSLATOR Gui::Dialog::DlgCustomToolBoxbarsImp */

/**
 *  Constructs a DlgCustomToolBoxbarsImp which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgCustomToolBoxbarsImp::DlgCustomToolBoxbarsImp( QWidget* parent )
    : DlgCustomToolbars(DlgCustomToolbars::Toolboxbar, parent)
{
    setWindowTitle( tr( "Toolbox bars" ) );
}

/** Destroys the object and frees any allocated resources */
DlgCustomToolBoxbarsImp::~DlgCustomToolBoxbarsImp() = default;

void DlgCustomToolBoxbarsImp::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        setWindowTitle( tr( "Toolbox bars" ) );
    }
    DlgCustomToolbars::changeEvent(e);
}

#include "moc_DlgToolbarsImp.cpp"
