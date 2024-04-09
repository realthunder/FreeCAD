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
# include <QMessageBox>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/MetaTypes.h>
#include <Gui/Selection.h>
#include <Mod/PartDesign/App/FeatureLoft.h>

#include "ui_TaskLoftParameters.h"
#include "TaskLoftParameters.h"
#include "TaskSketchBasedParameters.h"
#include "ReferenceSelection.h"
#include "Utils.h"

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskLoftParameters */

TaskLoftParameters::TaskLoftParameters(ViewProviderLoft *LoftView, bool /*newObj*/, QWidget *parent)
    : TaskSketchBasedParameters(LoftView, parent, "PartDesign_AdditiveLoft", tr("Loft parameters"))
    , ui(new Ui_TaskLoftParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    QMetaObject::connectSlotsByName(this);

    if (LoftView) {
        PartDesign::Loft* pcLoft = static_cast<PartDesign::Loft*>(LoftView->getObject());
        ui->checkBoxRuled->setToolTip(QApplication::translate(
                    "Property", pcLoft->Ruled.getDocumentation()));
        ui->checkBoxClosed->setToolTip(QApplication::translate(
                    "Property", pcLoft->Closed.getDocumentation()));
        ui->checkBoxSplitProfile->setToolTip(QApplication::translate(
                    "Property", pcLoft->SplitProfile.getDocumentation()));
    }

    connect(ui->checkBoxRuled, &QCheckBox::toggled,
            this, &TaskLoftParameters::onRuled);
    connect(ui->checkBoxClosed, &QCheckBox::toggled,
            this, &TaskLoftParameters::onClosed);
    connect(ui->checkBoxSplitProfile, &QCheckBox::toggled,
            this, &TaskLoftParameters::onSplitProfile);


    PartDesign::Loft* loft = static_cast<PartDesign::Loft*>(vp->getObject());
    sectionWidget = new LinkSubListWidget(this, tr("Sections"), tr("Add Section"), loft->Sections);
    proxy->layout()->addWidget(sectionWidget);

    this->initUI(proxy);
    this->groupLayout()->addWidget(proxy);
    refresh();

    // call updateUI until Gui::Document::setEdit() finishes;
    QMetaObject::invokeMethod(this, "updateUI", Qt::QueuedConnection);

    if (!loft->Sections.getValue())
        sectionWidget->onButton(true);
}

TaskLoftParameters::~TaskLoftParameters() = default;

void TaskLoftParameters::updateUI()
{
}

void TaskLoftParameters::refresh()
{
    if (!vp || !vp->getObject())
        return;

    // Temporarily prevent unnecessary feature recomputes
    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(true);

    //add the profiles
    PartDesign::Loft* loft = static_cast<PartDesign::Loft*>(vp->getObject());

    sectionWidget->refresh();

    // get options
    ui->checkBoxRuled->setChecked(loft->Ruled.getValue());
    ui->checkBoxClosed->setChecked(loft->Closed.getValue());
    ui->checkBoxSplitProfile->setChecked(loft->SplitProfile.getValue());

    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(false);
}

void TaskLoftParameters::onClosed(bool val) {
    if (!vp)
        return;
    try {
        setupTransaction();
        static_cast<PartDesign::Loft*>(vp->getObject())->Closed.setValue(val);
        recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void TaskLoftParameters::onRuled(bool val) {
    if (!vp)
        return;
    try {
        setupTransaction();
        static_cast<PartDesign::Loft*>(vp->getObject())->Ruled.setValue(val);
        recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}

void TaskLoftParameters::onSplitProfile(bool val) {
    if (!vp)
        return;
    try {
        setupTransaction();
        static_cast<PartDesign::Loft*>(vp->getObject())->SplitProfile.setValue(val);
        recomputeFeature();
    } catch (Base::Exception &e) {
        e.ReportException();
    }
}


//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgLoftParameters::TaskDlgLoftParameters(ViewProviderLoft *LoftView,bool newObj)
   : TaskDlgSketchBasedParameters(LoftView)
{
    assert(LoftView);
    parameter  = new TaskLoftParameters(LoftView,newObj);

    Content.push_back(parameter);
}

TaskDlgLoftParameters::~TaskDlgLoftParameters() = default;

bool TaskDlgLoftParameters::accept()
{
    PartDesign::Loft* pcLoft = static_cast<PartDesign::Loft*>(getLoftView()->getObject());
    try {
        parameter->setupTransaction();
        Gui::cmdGuiDocument(pcLoft, "resetEdit()");
        Gui::cmdAppDocument(pcLoft, "recompute()");
        if (!vp->getObject()->isValid())
            throw Base::RuntimeError(vp->getObject()->getStatusString());
        Gui::Command::commitCommand();
    }
    catch (const Base::Exception& e) {
        QMessageBox::warning(parameter, tr("Input error"), QString::fromUtf8(e.what()));
        return false;
    }

    return true;
}

//==== calls from the TaskView ===============================================================


#include "moc_TaskLoftParameters.cpp"
