/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

/*[[[cog
import DlgSettingsObjects
DlgSettingsObjects.define()
]]]*/

// Auto generated code (Tools/params_utils.py:601)
#ifndef _PreComp_
#   include <QApplication>
#   include <QLabel>
#   include <QGroupBox>
#   include <QGridLayout>
#   include <QVBoxLayout>
#   include <QHBoxLayout>
#endif
#include <App/GroupParams.h>
#include <App/Application.h>
#include <App/Document.h>
#include <App/Origin.h>
#include <App/AutoTransaction.h>
#include <App/LinkParams.h>
// Auto generated code (Tools/params_utils.py:623)
#include "Gui/PreferencePages/DlgSettingsObjects.h"
using namespace Gui::Dialog;
/* TRANSLATOR Gui::Dialog::DlgSettingsObjects */

// Auto generated code (Tools/params_utils.py:632)
DlgSettingsObjects::DlgSettingsObjects(QWidget* parent)
    : PreferencePage( parent )
{

    QHBoxLayout *layoutRow = nullptr;
    auto layout = new QVBoxLayout(this);


    // Auto generated code (Tools/params_utils.py:445)
    groupGroupobjects = new QGroupBox(this);
    layout->addWidget(groupGroupobjects);
    auto layoutHorizGroupobjects = new QHBoxLayout(groupGroupobjects);
    auto layoutGroupobjects = new QVBoxLayout();
    layoutHorizGroupobjects->addLayout(layoutGroupobjects);
    layoutHorizGroupobjects->addStretch();

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutGroupobjects->addLayout(layoutRow);
    ClaimAllChildren = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ClaimAllChildren);
    ClaimAllChildren->setChecked(App::GroupParams::defaultClaimAllChildren());
    ClaimAllChildren->setEntryName("ClaimAllChildren");
    ClaimAllChildren->setParamGrpPath("Group");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutGroupobjects->addLayout(layoutRow);
    KeepHiddenChildren = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(KeepHiddenChildren);
    KeepHiddenChildren->setChecked(App::GroupParams::defaultKeepHiddenChildren());
    KeepHiddenChildren->setEntryName("KeepHiddenChildren");
    KeepHiddenChildren->setParamGrpPath("Group");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutGroupobjects->addLayout(layoutRow);
    ExportChildren = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ExportChildren);
    ExportChildren->setChecked(App::GroupParams::defaultExportChildren());
    ExportChildren->setEntryName("ExportChildren");
    ExportChildren->setParamGrpPath("Group");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutGroupobjects->addLayout(layoutRow);
    // Auto generated code (Gui/PreferencePages/DlgSettingsObjects.py:64)
    CreateOrigin = new Gui::PrefCheckBox(this);
    buttonCreateOrigin = new QPushButton(this);
    {
        layoutRow->addWidget(CreateOrigin);
        layoutRow->addWidget(buttonCreateOrigin);
    }
    buttonCreateOrigin->setEnabled(CreateOrigin->isChecked());
    connect(CreateOrigin, SIGNAL(toggled(bool)), buttonCreateOrigin, SLOT(setEnabled(bool)));

    // Auto generated code (Gui/PreferencePages/DlgSettingsObjects.py:75)
    QObject::connect(buttonCreateOrigin, &QPushButton::clicked, []() {
        for (auto doc : App::GetApplication().getDocuments()) {
            if (doc->testStatus(App::Document::TempDoc)
                || doc->testStatus(App::Document::PartialDoc))
                continue;
            App::GetApplication().setActiveDocument(doc);
            App::AutoTransaction guard("Init origins");
            // getObjects() below returns a const reference, so we must copy
            // objects here in order to iterate it safely while adding objects
            auto objs = doc->getObjects();
            for (auto obj : objs) {
                if (auto origin = Base::freecad_dynamic_cast<App::Origin>(obj))
                    origin->getX(); // make sure all origin features are created
            }
        }
    });
    // Auto generated code (Gui/PreferencePages/DlgSettingsObjects.py:93)
    CreateOrigin->setEntryName("CreateOrigin");
    CreateOrigin->setParamGrpPath("Group");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutGroupobjects->addLayout(layoutRow);
    GeoGroupAllowCrossLink = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(GeoGroupAllowCrossLink);
    GeoGroupAllowCrossLink->setChecked(App::GroupParams::defaultGeoGroupAllowCrossLink());
    GeoGroupAllowCrossLink->setEntryName("GeoGroupAllowCrossLink");
    GeoGroupAllowCrossLink->setParamGrpPath("Group");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutGroupobjects->addLayout(layoutRow);
    CreateGroupInGroup = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(CreateGroupInGroup);
    CreateGroupInGroup->setChecked(App::GroupParams::defaultCreateGroupInGroup());
    CreateGroupInGroup->setEntryName("CreateGroupInGroup");
    CreateGroupInGroup->setParamGrpPath("Group");


    // Auto generated code (Tools/params_utils.py:445)
    groupAppLink = new QGroupBox(this);
    layout->addWidget(groupAppLink);
    auto layoutHorizAppLink = new QHBoxLayout(groupAppLink);
    auto layoutAppLink = new QVBoxLayout();
    layoutHorizAppLink->addLayout(layoutAppLink);
    layoutHorizAppLink->addStretch();

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutAppLink->addLayout(layoutRow);
    ShowElement = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShowElement);
    ShowElement->setChecked(App::LinkParams::defaultShowElement());
    ShowElement->setEntryName("ShowElement");
    ShowElement->setParamGrpPath("Link");
    layout->addItem(new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding));
    retranslateUi();
    // Auto generated code (Tools/params_utils.py:641)
    
}

// Auto generated code (Tools/params_utils.py:648)
DlgSettingsObjects::~DlgSettingsObjects()
{
}

// Auto generated code (Tools/params_utils.py:656)
void DlgSettingsObjects::saveSettings()
{
    // Auto generated code (Tools/params_utils.py:494)
    ClaimAllChildren->onSave();
    KeepHiddenChildren->onSave();
    ExportChildren->onSave();
    CreateOrigin->onSave();
    GeoGroupAllowCrossLink->onSave();
    CreateGroupInGroup->onSave();
    ShowElement->onSave();
}

// Auto generated code (Tools/params_utils.py:665)
void DlgSettingsObjects::loadSettings()
{
    // Auto generated code (Tools/params_utils.py:481)
    ClaimAllChildren->onRestore();
    KeepHiddenChildren->onRestore();
    ExportChildren->onRestore();
    CreateOrigin->onRestore();
    GeoGroupAllowCrossLink->onRestore();
    CreateGroupInGroup->onRestore();
    ShowElement->onRestore();
}

// Auto generated code (Tools/params_utils.py:674)
void DlgSettingsObjects::retranslateUi()
{
    setWindowTitle(QObject::tr("Objects"));
    groupGroupobjects->setTitle(QObject::tr("Group objects"));
    ClaimAllChildren->setToolTip(QApplication::translate("GroupParams", App::GroupParams::docClaimAllChildren()));
    ClaimAllChildren->setText(QObject::tr("Claim all children"));
    KeepHiddenChildren->setToolTip(QApplication::translate("GroupParams", App::GroupParams::docKeepHiddenChildren()));
    KeepHiddenChildren->setText(QObject::tr("Remember hidden children"));
    ExportChildren->setToolTip(QApplication::translate("GroupParams", App::GroupParams::docExportChildren()));
    ExportChildren->setText(QObject::tr("Export children by visibility"));
    CreateOrigin->setToolTip(QApplication::translate("GroupParams", App::GroupParams::docCreateOrigin()));
    CreateOrigin->setText(QObject::tr("Always create origin features in origin group"));
    // Auto generated code (Gui/PreferencePages/DlgSettingsObjects.py:99)
    buttonCreateOrigin->setText(tr("Apply to existing objects"));
    GeoGroupAllowCrossLink->setToolTip(QApplication::translate("GroupParams", App::GroupParams::docGeoGroupAllowCrossLink()));
    GeoGroupAllowCrossLink->setText(QObject::tr("Allow cross coordinate links in GeoFeatureGroup (App::Part)"));
    CreateGroupInGroup->setToolTip(QApplication::translate("GroupParams", App::GroupParams::docCreateGroupInGroup()));
    CreateGroupInGroup->setText(QObject::tr("Create new group inside current selected group"));
    groupAppLink->setTitle(QObject::tr("App::Link"));
    ShowElement->setToolTip(QApplication::translate("LinkParams", App::LinkParams::docShowElement()));
    ShowElement->setText(QObject::tr("Show array element in Link array"));
}

// Auto generated code (Tools/params_utils.py:692)
void DlgSettingsObjects::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(e);
}

// Auto generated code (Tools/params_utils.py:705)
#include "moc_DlgSettingsObjects.cpp"
//[[[end]]]
