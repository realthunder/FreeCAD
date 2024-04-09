// SPDX-License-Identifier: LGPL-2.1-or-later

 /****************************************************************************
  *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
  *   Copyright (c) 2023 FreeCAD Project Association                         *
  *                                                                          *
  *   This file is part of FreeCAD.                                          *
  *                                                                          *
  *   FreeCAD is free software: you can redistribute it and/or modify it     *
  *   under the terms of the GNU Lesser General Public License as            *
  *   published by the Free Software Foundation, either version 2.1 of the   *
  *   License, or (at your option) any later version.                        *
  *                                                                          *
  *   FreeCAD is distributed in the hope that it will be useful, but         *
  *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
  *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
  *   Lesser General Public License for more details.                        *
  *                                                                          *
  *   You should have received a copy of the GNU Lesser General Public       *
  *   License along with FreeCAD. If not, see                                *
  *   <https://www.gnu.org/licenses/>.                                       *
  *                                                                          *
  ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <cmath>
# include <limits>
# include <QApplication>
# include <QFileDialog>
# include <QLocale>
# include <QMessageBox>
# include <algorithm>
# include <boost/filesystem.hpp>
#endif

#include <Base/Parameter.h>

#include "DlgSettingsPreferencePack.h"
#include "ui_DlgSettingsPreferencePack.h"
#include "Action.h"
#include "Application.h"
#include "DlgCreateNewPreferencePackImp.h"
#include "DlgPreferencesImp.h"
#include "DlgPreferencePackManagementImp.h"
#include "DlgRevertToBackupConfigImp.h"
#include "MainWindow.h"
#include "PreferencePackManager.h"

using namespace Gui::Dialog;
namespace fs = boost::filesystem;
using namespace Base;

/* TRANSLATOR Gui::Dialog::DlgSettingsPreferencePack */

/**
 *  Constructs a DlgSettingsPreferencePack which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgSettingsPreferencePack::DlgSettingsPreferencePack( QWidget* parent )
  : PreferencePage(parent)
  , ui(new Ui_DlgSettingsPreferencePack)
{
    ui->setupUi(this);

    recreatePreferencePackMenu();

    connect(ui->ImportConfig, &QPushButton::clicked, this, &DlgSettingsPreferencePack::onImportConfigClicked);
    connect(ui->SaveNewPreferencePack, &QPushButton::clicked, this, &DlgSettingsPreferencePack::saveAsNewPreferencePack);

    ui->ManagePreferencePacks->setToolTip(tr("Manage preference packs"));
    connect(ui->ManagePreferencePacks, &QPushButton::clicked, this, &DlgSettingsPreferencePack::onManagePreferencePacksClicked);

    // If there are any saved config file backs, show the revert button, otherwise hide it:
    const auto & backups = Application::Instance->prefPackManager()->configBackups();
    ui->RevertToSavedConfig->setEnabled(backups.empty());
    connect(ui->RevertToSavedConfig, &QPushButton::clicked, this, &DlgSettingsPreferencePack::revertToSavedConfig);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettingsPreferencePack::~DlgSettingsPreferencePack()
{
}

void DlgSettingsPreferencePack::saveSettings()
{
}

void DlgSettingsPreferencePack::loadSettings()
{
}

void DlgSettingsPreferencePack::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
}

void DlgSettingsPreferencePack::recreatePreferencePackMenu()
{
    ui->PreferencePacks->setRowCount(0); // Begin by clearing whatever is there
    ui->PreferencePacks->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->PreferencePacks->setColumnCount(3);
    ui->PreferencePacks->setSelectionMode(QAbstractItemView::SelectionMode::NoSelection);
    ui->PreferencePacks->horizontalHeader()->setStretchLastSection(false);
    ui->PreferencePacks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::Stretch);
    ui->PreferencePacks->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeMode::Stretch);
    ui->PreferencePacks->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);
    QStringList columnHeaders;
    columnHeaders << tr("Preference Pack Name")
                  << tr("Tags")
                  << QString(); // for the "Load" buttons
    ui->PreferencePacks->setHorizontalHeaderLabels(columnHeaders);

    // Populate the Preference Packs list
    Application::Instance->prefPackManager()->rescan();
    auto packs = Application::Instance->prefPackManager()->preferencePacks();

    ui->PreferencePacks->setRowCount(packs.size());

    int row = 0;
    QIcon icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    for (const auto& pack : packs) {
        auto name = new QTableWidgetItem(QString::fromStdString(pack.first));
        name->setToolTip(QString::fromStdString(pack.second.metadata().description()));
        ui->PreferencePacks->setItem(row, 0, name);
        auto tags = pack.second.metadata().tag();
        QString tagString;
        for (const auto& tag : tags) {
            if (tagString.isEmpty())
                tagString.append(QString::fromStdString(tag));
            else
                tagString.append(QStringLiteral(", ") + QString::fromStdString(tag));
        }
        auto kind = new QTableWidgetItem(tagString);
        ui->PreferencePacks->setItem(row, 1, kind);
        auto button = new QPushButton(icon, tr("Apply"));
        button->setToolTip(tr("Apply the %1 preference pack").arg(QString::fromStdString(pack.first)));
        connect(button, &QPushButton::clicked, this, [this, pack]() { onLoadPreferencePackClicked(pack.first); });
        ui->PreferencePacks->setCellWidget(row, 2, button);
        ++row;
    }
}

void DlgSettingsPreferencePack::saveAsNewPreferencePack()
{
    // Create and run a modal New PreferencePack dialog box
    auto packs = Application::Instance->prefPackManager()->preferencePackNames();
    newPreferencePackDialog = std::make_unique<DlgCreateNewPreferencePackImp>(this);
    newPreferencePackDialog->setPreferencePackTemplates(Application::Instance->prefPackManager()->templateFiles());
    newPreferencePackDialog->setPreferencePackNames(packs);
    connect(newPreferencePackDialog.get(), &DlgCreateNewPreferencePackImp::accepted, this, &DlgSettingsPreferencePack::newPreferencePackDialogAccepted);
    newPreferencePackDialog->open();
}

void DlgSettingsPreferencePack::revertToSavedConfig()
{
    revertToBackupConfigDialog = std::make_unique<DlgRevertToBackupConfigImp>(this);
    connect(revertToBackupConfigDialog.get(), &DlgRevertToBackupConfigImp::accepted, this, [this]() {
        auto parentDialog = qobject_cast<DlgPreferencesImp*> (this->window());
        if (parentDialog) {
            parentDialog->reload();
        }
    });
    revertToBackupConfigDialog->open();
}

void DlgSettingsPreferencePack::newPreferencePackDialogAccepted()
{
    auto preferencePackTemplates = Application::Instance->prefPackManager()->templateFiles();
    auto selection = newPreferencePackDialog->selectedTemplates();
    std::vector<PreferencePackManager::TemplateFile> selectedTemplates;
    std::copy_if(preferencePackTemplates.begin(), preferencePackTemplates.end(), std::back_inserter(selectedTemplates), [selection](PreferencePackManager::TemplateFile& tf) {
        for (const auto& item : selection) {
            if (item.group == tf.group && item.name == tf.name) {
                return true;
            }
        }
        return false;
    });
    auto preferencePackName = newPreferencePackDialog->preferencePackName();
    Application::Instance->prefPackManager()->save(preferencePackName, selectedTemplates);
    recreatePreferencePackMenu();
}

void DlgSettingsPreferencePack::onManagePreferencePacksClicked()
{
    if (!this->preferencePackManagementDialog) {
        this->preferencePackManagementDialog = std::make_unique<DlgPreferencePackManagementImp>(this);
        connect(this->preferencePackManagementDialog.get(), &DlgPreferencePackManagementImp::packVisibilityChanged,
            this, &DlgSettingsPreferencePack::recreatePreferencePackMenu);
    }
    this->preferencePackManagementDialog->show();
}

void DlgSettingsPreferencePack::onImportConfigClicked()
{
    auto path = fs::path(QFileDialog::getOpenFileName(this,
        tr("Choose a FreeCAD config file to import"),
        QString(),
        QString::fromUtf8("*.cfg")).toStdString());
    if (!path.empty()) {
        // Create a name from the filename:
        auto packName = path.filename().stem().string();
        std::replace(packName.begin(), packName.end(), '_', ' ');
        auto existingPacks = Application::Instance->prefPackManager()->preferencePackNames();
        if (std::find(existingPacks.begin(), existingPacks.end(), packName)
            != existingPacks.end()) {
            auto result = QMessageBox::question(
                this, tr("File exists"),
                tr("A preference pack with that name already exists. Overwrite?"));
            if (result == QMessageBox::No) { // Maybe someday ask for a new name?
                return;
            }
        }
        Application::Instance->prefPackManager()->importConfig(packName, path);
        recreatePreferencePackMenu();
    }
}

void DlgSettingsPreferencePack::onLoadPreferencePackClicked(const std::string& packName)
{
    if (Application::Instance->prefPackManager()->apply(packName)) {
        auto parentDialog = qobject_cast<DlgPreferencesImp*> (this->window());
        if (parentDialog)
            parentDialog->reload();
    }
}

#include "moc_DlgSettingsPreferencePack.cpp"
