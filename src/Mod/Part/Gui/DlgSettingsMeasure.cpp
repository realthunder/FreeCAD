/***************************************************************************
 *   Copyright (c) 2022                                                    *
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

#include <Gui/Command.h>

#include "DlgSettingsMeasure.h"
#include "ui_DlgSettingsMeasure.h"
#include "PartParams.h"


using namespace PartGui;

DlgSettingsMeasure::DlgSettingsMeasure(QWidget* parent)
  : PreferencePage(parent) , ui(new Ui_DlgSettingsMeasure)
{
    ui->setupUi(this);
    connect(ui->pushButtonRefresh, &QPushButton::clicked, this, &DlgSettingsMeasure::onMeasureRefresh);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettingsMeasure::~DlgSettingsMeasure() = default;

void DlgSettingsMeasure::saveSettings()
{
    ui->dim3dColorButton->onSave();
    ui->dimDeltaColorButton->onSave();
    ui->dimAngularColorButton->onSave();

    ui->fontSizeSpinBox->onSave();
    ui->fontNameComboBox->onSave();

    ui->fontStyleBoldCheckBox->onSave();
    ui->fontStyleItalicCheckBox->onSave();
}

void DlgSettingsMeasure::loadSettings()
{
    ui->dim3dColorButton->onRestore();
    ui->dimDeltaColorButton->onRestore();
    ui->dimAngularColorButton->onRestore();

    ui->fontSizeSpinBox->onRestore();
    ui->fontNameComboBox->onRestore();
    ui->fontNameComboBox->addItems(QStringList({QString::fromUtf8("defaultFont")}));
    // "defaultFont" is the setting's default and no font of the system. The
    // box rebuilds its list from the system's when it is given a font, so
    // the entry can only be added after the restore -- and has to be made
    // current here when it is the setting, or the box shows the first
    // system font in its place and OK stores that one. Making it current
    // is not the user choosing it: with preferences applied as they are
    // made the box would store the key at once, and Cancel on a dialog
    // nothing was changed in then asks whether to revert the changes. So
    // the box does not save while it is done. Its signals cannot simply
    // be blocked: a font box learns its current font from one of them,
    // and OK would store the font it showed before.
    if (PartParams::getDimensionsFontName() == "defaultFont") {
        ui->fontNameComboBox->setAutoSave(false);
        ui->fontNameComboBox->setCurrentIndex(
            ui->fontNameComboBox->findText(QString::fromUtf8("defaultFont")));
        ui->fontNameComboBox->setAutoSave(Gui::PrefParam::AutoSave());
    }

    ui->fontStyleBoldCheckBox->onRestore();
    ui->fontStyleItalicCheckBox->onRestore();
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgSettingsMeasure::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

void DlgSettingsMeasure::onMeasureRefresh()
{
    DlgSettingsMeasure::saveSettings();
    Gui::Command::runCommand(Gui::Command::Gui, "Gui.runCommand('Part_Measure_Refresh',0)");
}

#include "moc_DlgSettingsMeasure.cpp"
