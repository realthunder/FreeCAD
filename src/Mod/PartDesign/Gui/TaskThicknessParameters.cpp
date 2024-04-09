/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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
#endif

#include <Base/Interpreter.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Selection.h>
#include <Gui/Command.h>
#include <Mod/PartDesign/App/FeatureThickness.h>

#include "ui_TaskThicknessParameters.h"
#include "TaskThicknessParameters.h"

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskThicknessParameters */

TaskThicknessParameters::TaskThicknessParameters(ViewProviderDressUp *DressUpView,QWidget *parent)
    : TaskDressUpParameters(DressUpView, false, true, parent)
    , ui(new Ui_TaskThicknessParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    this->groupLayout()->addWidget(proxy);

    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    double a = pcThickness->Value.getValue();

    ui->Value->setMinimum(0.0);
    ui->Value->setMaximum(DBL_MAX);
    ui->Value->setValue(a);
    ui->Value->selectAll();
    QMetaObject::invokeMethod(ui->Value, "setFocus", Qt::QueuedConnection);

    // Bind input fields to properties
    ui->Value->bind(pcThickness->Value);

    bool r = pcThickness->Reversed.getValue();
    ui->checkReverse->setChecked(r);

    bool i = pcThickness->MakeOffset.getValue();
    ui->checkMakeOffset->setToolTip(QApplication::translate("PartDesign", pcThickness->MakeOffset.getDocumentation()));

    ui->checkMakeOffset->setChecked(i);
    QMetaObject::connectSlotsByName(this);

    connect(ui->Value, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        this, &TaskThicknessParameters::onValueChanged);
    connect(ui->checkReverse, &QCheckBox::toggled,
        this, &TaskThicknessParameters::onReversedChanged);
    connect(ui->checkMakeOffset, &QCheckBox::toggled,
            this, &TaskThicknessParameters::onMakeOffsetChanged);
    connect(ui->modeComboBox, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &TaskThicknessParameters::onModeChanged);
    connect(ui->joinComboBox, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &TaskThicknessParameters::onJoinTypeChanged);
    connect(ui->checkIntersection, &QCheckBox::toggled,
        this, &TaskThicknessParameters::onIntersectionChanged);

    int mode = pcThickness->Mode.getValue();
    ui->modeComboBox->setCurrentIndex(mode);

    int join = pcThickness->Join.getValue();
    ui->joinComboBox->setCurrentIndex(join);

    setup(ui->message, ui->treeWidgetReferences, ui->buttonRefAdd);
}

void TaskThicknessParameters::refresh()
{
    TaskDressUpParameters::refresh();
    if (!DressUpView)
        return;

    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    bool r = pcThickness->Reversed.getValue();
    {
        QSignalBlocker blocker(ui->checkReverse);
        ui->checkReverse->setChecked(r);
    }

    bool i = pcThickness->MakeOffset.getValue();
    {
        QSignalBlocker blocker(ui->checkMakeOffset);
        ui->checkMakeOffset->setChecked(i);
    }

    int mode = pcThickness->Mode.getValue();
    {
        QSignalBlocker blocker(ui->modeComboBox);
        ui->modeComboBox->setCurrentIndex(mode);
    }

    int join = pcThickness->Join.getValue();
    {
        QSignalBlocker blocker(ui->joinComboBox);
        ui->joinComboBox->setCurrentIndex(join);
    }
}

void TaskThicknessParameters::onValueChanged(double angle)
{
    clearButtons(none);
    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    setupTransaction();
    pcThickness->Value.setValue(angle);
    recompute();
}

void TaskThicknessParameters::onJoinTypeChanged(int join) {

    clearButtons(none);
    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    setupTransaction();
    pcThickness->Join.setValue(join);
    recompute();
}

void TaskThicknessParameters::onModeChanged(int mode) {

    clearButtons(none);
    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    setupTransaction();
    pcThickness->Mode.setValue(mode);
    recompute();
}

double TaskThicknessParameters::getValue() const
{
    return ui->Value->value().getValue();
}

void TaskThicknessParameters::onReversedChanged(const bool on) {
    clearButtons(none);
    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    setupTransaction();
    pcThickness->Reversed.setValue(on);
    recompute();
}

bool TaskThicknessParameters::getReversed() const
{
    return ui->checkReverse->isChecked();
}

void TaskThicknessParameters::onIntersectionChanged(const bool on) {
    clearButtons(none);
    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    setupTransaction();
    pcThickness->Intersection.setValue(on);
    recompute();
}

bool TaskThicknessParameters::getIntersection() const
{
    return ui->checkIntersection->isChecked();
}

void TaskThicknessParameters::onMakeOffsetChanged(const bool on) {
    clearButtons(none);
    PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
    setupTransaction();
    pcThickness->MakeOffset.setValue(on);
    recompute();
}

bool TaskThicknessParameters::getMakeOffset() const
{
    return ui->checkMakeOffset->isChecked();
}

int TaskThicknessParameters::getJoinType() const
{
    return ui->joinComboBox->currentIndex();
}

int TaskThicknessParameters::getMode() const {

    return ui->modeComboBox->currentIndex();
}

TaskThicknessParameters::~TaskThicknessParameters()
{
    Gui::Selection().rmvSelectionGate();
}

void TaskThicknessParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
        if (DressUpView) {
            PartDesign::Thickness* pcThickness = static_cast<PartDesign::Thickness*>(DressUpView->getObject());
            ui->checkMakeOffset->setToolTip(QApplication::translate("PartDesign", pcThickness->MakeOffset.getDocumentation()));
        }
    }
}


//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgThicknessParameters::TaskDlgThicknessParameters(ViewProviderThickness *DressUpView)
    : TaskDlgDressUpParameters(DressUpView)
{
    parameter  = new TaskThicknessParameters(DressUpView);

    Content.push_back(parameter);
}

TaskDlgThicknessParameters::~TaskDlgThicknessParameters() = default;

//==== calls from the TaskView ===============================================================


//void TaskDlgThicknessParameters::open()
//{
//    // a transaction is already open at creation time of the draft
//    if (!Gui::Command::hasPendingCommand()) {
//        QString msg = QObject::tr("Edit draft");
//        Gui::Command::openCommand((const char*)msg.toUtf8());
//    }
//}
//
//void TaskDlgThicknessParameters::clicked(int)
//{
//
//}

bool TaskDlgThicknessParameters::accept()
{
    TaskThicknessParameters* draftparameter = static_cast<TaskThicknessParameters*>(parameter);
    auto obj = vp->getObject();

    FCMD_OBJ_CMD(obj,"Value = " << draftparameter->getValue());
    FCMD_OBJ_CMD(obj,"Reversed = " << draftparameter->getReversed());
    FCMD_OBJ_CMD(obj,"Mode = " << draftparameter->getMode());
    FCMD_OBJ_CMD(obj,"MakeOffset = " << draftparameter->getMakeOffset());
    FCMD_OBJ_CMD(obj,"Intersection = " << draftparameter->getIntersection());
    FCMD_OBJ_CMD(obj,"Join = " << draftparameter->getJoinType());

    return TaskDlgDressUpParameters::accept();
}

#include "moc_TaskThicknessParameters.cpp"
