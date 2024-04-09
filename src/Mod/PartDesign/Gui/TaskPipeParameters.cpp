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
# include <QMetaObject>
#endif

#include <boost/algorithm/string/predicate.hpp>

#include "ui_TaskPipeParameters.h"
#include "ui_TaskPipeOrientation.h"
#include "ui_TaskPipeScaling.h"
#include "TaskPipeParameters.h"
#include <App/Application.h>
#include <App/DocumentObject.h>
#include <App/Origin.h>
#include <App/Part.h>
#include <Base/Console.h>
#include <Base/ExceptionSafeCall.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/CommandT.h>
#include <Gui/MetaTypes.h>
#include <Gui/Selection.h>
#include <Gui/ViewProvider.h>
#include <Gui/Widgets.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeaturePipe.h>

#include "ui_TaskPipeParameters.h"
#include "ui_TaskPipeOrientation.h"
#include "ui_TaskPipeScaling.h"

#include "Utils.h"


Q_DECLARE_METATYPE(App::PropertyLinkSubList::SubSet)

FC_LOG_LEVEL_INIT("PartDesign", true, true)

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskPipeParameters */

//**************************************************************************
//**************************************************************************
// Task Parameter
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskPipeParameters::TaskPipeParameters(ViewProviderPipe *PipeView, bool /*newObj*/, QWidget *parent)
    : TaskSketchBasedParameters(PipeView, parent, "PartDesign_AdditivePipe", tr("Pipe parameters"))
    , ui(new Ui_TaskPipeParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);

    PartDesign::Pipe* pcPipe = static_cast<PartDesign::Pipe*>(PipeView->getObject());

    QBoxLayout * boxLayout = qobject_cast<QBoxLayout*>(proxy->layout());
    if (boxLayout) {
        spineWidget = new LinkSubWidget(this, tr("Sweep Path"), pcPipe->Spine);
        spineWidget->setSelectionMode(SelectionMode::refSpine);
        boxLayout->addWidget(spineWidget);
    }


    if (auto doc = pcPipe->MoveProfile.getDocumentation())
        ui->checkBoxMoveProfile->setToolTip(QString::fromUtf8(doc));
    if (auto doc = pcPipe->RotateProfile.getDocumentation())
        ui->checkBoxRotateProfile->setToolTip(QString::fromUtf8(doc));

    this->initUI(proxy);
    this->groupLayout()->addWidget(proxy);

    refresh();

    QMetaObject::invokeMethod(this, "updateUI", Qt::QueuedConnection);

    if (!pcPipe->Spine.getValue())
        spineWidget->onButton(true);

    Base::connect(ui->checkBoxMoveProfile, &QCheckBox::toggled, this, &TaskPipeParameters::onProfileOptionChanged);
    Base::connect(ui->checkBoxRotateProfile, &QCheckBox::toggled, this, &TaskPipeParameters::onProfileOptionChanged);
    Base::connect(ui->comboBoxTransition, QOverload<int>::of(&QComboBox::currentIndexChanged),
                  this, &TaskPipeParameters::onTransitionChanged);
    QMetaObject::connectSlotsByName(this);
}

TaskPipeParameters::~TaskPipeParameters()
{
    exitSelectionMode();
}

void TaskPipeParameters::updateUI()
{
}

void TaskPipeParameters::onProfileOptionChanged()
{
    if (!vp)
        return;
    setupTransaction();
    auto pcPipe = static_cast<PartDesign::Pipe*>(vp->getObject());
    pcPipe->MoveProfile.setValue(ui->checkBoxMoveProfile->isChecked());
    pcPipe->RotateProfile.setValue(ui->checkBoxRotateProfile->isChecked());
    recomputeFeature();
}

void TaskPipeParameters::refresh()
{
    if (!vp || !vp->getObject())
        return;

    QSignalBlocker transition(ui->comboBoxTransition);
    PartDesign::Pipe* pipe = static_cast<PartDesign::Pipe*>(vp->getObject());
    ui->comboBoxTransition->setCurrentIndex(pipe->Transition.getValue());
    QSignalBlocker moveProfile(ui->checkBoxMoveProfile);
    ui->checkBoxMoveProfile->setChecked(pipe->MoveProfile.getValue());
    QSignalBlocker rotateProfile(ui->checkBoxRotateProfile);
    ui->checkBoxRotateProfile->setChecked(pipe->RotateProfile.getValue());

    spineWidget->refresh();
}

void TaskPipeParameters::onTransitionChanged(int idx) {
    if (!vp)
        return;
    setupTransaction();
    static_cast<PartDesign::Pipe*>(vp->getObject())->Transition.setValue(idx);
    recomputeFeature();
}

void TaskPipeParameters::onTangentChanged(bool checked) {
    setupTransaction();
    static_cast<PartDesign::Pipe*>(vp->getObject())->SpineTangent.setValue(checked);
    recomputeFeature();
}

//**************************************************************************
//**************************************************************************
// Task Orientation
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskPipeOrientation::TaskPipeOrientation(ViewProviderPipe* PipeView, bool /*newObj*/, QWidget* parent)
    : TaskSketchBasedParameters(PipeView, parent, "PartDesign_AdditivePipe", tr("Section orientation"))
    , ui(new Ui_TaskPipeOrientation)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);

    QBoxLayout * boxLayout = qobject_cast<QBoxLayout*>(ui->auxiliary->layout());
    if (boxLayout) {
        PartDesign::Pipe* pcPipe = static_cast<PartDesign::Pipe*>(PipeView->getObject());
        auxSpineWidget = new LinkSubWidget(this, tr("Aux Path"), pcPipe->AuxillerySpine);
        auxSpineWidget->setSelectionMode(SelectionMode::refAuxSpine);
        boxLayout->addWidget(auxSpineWidget);
    }

    this->groupLayout()->addWidget(proxy);
    PartDesign::Pipe* pipe = static_cast<PartDesign::Pipe*>(vp->getObject());
    // should be called after panel has become visible
    QMetaObject::invokeMethod(this, "updateUI", Qt::QueuedConnection,
        QGenericReturnArgument(), Q_ARG(int,pipe->Mode.getValue()));

    refresh();

    QMetaObject::connectSlotsByName(this);
    Base::connect(ui->comboBoxMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                  this, &TaskPipeOrientation::onOrientationChanged);
    Base::connect(ui->stackedWidget, &QStackedWidget::currentChanged, this, &TaskPipeOrientation::updateUI);
    Base::connect(ui->curvelinear, &QCheckBox::toggled, this, &TaskPipeOrientation::onCurvelinearChanged);
    Base::connect(ui->doubleSpinBoxX, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                  this, &TaskPipeOrientation::onBinormalChanged);
    Base::connect(ui->doubleSpinBoxY, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                  this, &TaskPipeOrientation::onBinormalChanged);
    Base::connect(ui->doubleSpinBoxZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                  this, &TaskPipeOrientation::onBinormalChanged);

}

TaskPipeOrientation::~TaskPipeOrientation()
{
    exitSelectionMode();
}

void TaskPipeOrientation::refresh()
{
    if (!vp || !vp->getObject())
        return;

    // Temporarily prevent unnecessary feature recomputes
    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(true);

    PartDesign::Pipe* pipe = static_cast<PartDesign::Pipe*>(vp->getObject());

    ui->curvelinear->setChecked(pipe->AuxilleryCurvelinear.getValue());

    int idx = pipe->Mode.getValue();
    ui->comboBoxMode->setCurrentIndex(idx);
    ui->stackedWidget->setCurrentIndex(idx);
    updateUI(idx);

    if (auxSpineWidget)
        auxSpineWidget->refresh();

    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(false);

    TaskSketchBasedParameters::refresh();
}

void TaskPipeOrientation::onOrientationChanged(int idx) {
    if (!vp)
        return;
    ui->stackedWidget->setCurrentIndex(idx);
    setupTransaction();
    auto pipe = static_cast<PartDesign::Pipe*>(vp->getObject());
    pipe->Mode.setValue(idx);
    if (idx != 3 || pipe->AuxillerySpine.getValue()) {
        recomputeFeature();
    }
    if (idx == 3) {
        if (getSelectionMode() != SelectionMode::refAuxSpine)
            auxSpineWidget->onButton(true);
    } else {
        exitSelectionMode();
    }
}

void TaskPipeOrientation::onCurvelinearChanged(bool checked)
{
    setupTransaction();
    static_cast<PartDesign::Pipe*>(vp->getObject())->AuxilleryCurvelinear.setValue(checked);
    recomputeFeature();
}

void TaskPipeOrientation::onBinormalChanged(double)
{
    Base::Vector3d vec(ui->doubleSpinBoxX->value(),
                       ui->doubleSpinBoxY->value(),
                       ui->doubleSpinBoxZ->value());

    setupTransaction();
    static_cast<PartDesign::Pipe*>(vp->getObject())->Binormal.setValue(vec);
    recomputeFeature();
}

void TaskPipeOrientation::updateUI(int idx)
{
    //make sure we resize to the size of the current page
    for (int i=0; i<ui->stackedWidget->count(); ++i)
        ui->stackedWidget->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    if (idx < ui->stackedWidget->count())
        ui->stackedWidget->widget(idx)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}


//**************************************************************************
//**************************************************************************
// Task Scaling
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
TaskPipeScaling::TaskPipeScaling(ViewProviderPipe* PipeView, bool /*newObj*/, QWidget* parent)
    : TaskSketchBasedParameters(PipeView, parent, "PartDesign_AdditivePipe", tr("Section transformation"))
    , ui(new Ui_TaskPipeScaling)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);

    PartDesign::Pipe* pipe = static_cast<PartDesign::Pipe*>(PipeView->getObject());
    sectionWidget = new LinkSubListWidget(this, tr("Sections"), tr("Add Section"), pipe->Sections);
    ui->multisection->layout()->addWidget(sectionWidget);

    this->groupLayout()->addWidget(proxy);


    // should be called after panel has become visible
    QMetaObject::invokeMethod(this, "updateUI", Qt::QueuedConnection,
        Q_ARG(int,pipe->Transformation.getValue()));

    refresh();

    QMetaObject::connectSlotsByName(this);

    // some buttons are handled in a buttongroup
    Base::connect(ui->comboBoxScaling, QOverload<int>::of(&QComboBox::currentIndexChanged),
                  this, &TaskPipeScaling::onScalingChanged);
    Base::connect(ui->stackedWidget, &QStackedWidget::currentChanged,
                  this, &TaskPipeScaling::updateUI);
}

TaskPipeScaling::~TaskPipeScaling()
{
    exitSelectionMode();
}

void TaskPipeScaling::refresh()
{
    if (!vp || !vp->getObject())
        return;

    // Temporarily prevent unnecessary feature recomputes
    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(true);

    sectionWidget->refresh();

    PartDesign::Pipe* pipe = static_cast<PartDesign::Pipe*>(vp->getObject());
    int idx = pipe->Transformation.getValue();
    ui->comboBoxScaling->setCurrentIndex(idx);
    ui->stackedWidget->setCurrentIndex(idx);
    updateUI(idx);

    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(false);

    TaskSketchBasedParameters::refresh();
}

void TaskPipeScaling::onScalingChanged(int idx) {
    if (!vp)
        return;
    ui->stackedWidget->setCurrentIndex(idx);
    updateUI(idx);
    setupTransaction();
    static_cast<PartDesign::Pipe*>(vp->getObject())->Transformation.setValue(idx);
    recomputeFeature();
    if (idx == 1) {
        sectionWidget->onButton(true);
    } else {
        exitSelectionMode();
    }
}

void TaskPipeScaling::updateUI(int idx)
{
    //make sure we resize to the size of the current page
    for (int i=0; i<ui->stackedWidget->count(); ++i)
        ui->stackedWidget->widget(i)->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    if (idx < ui->stackedWidget->count())
        ui->stackedWidget->widget(idx)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgPipeParameters::TaskDlgPipeParameters(ViewProviderPipe *PipeView,bool newObj)
   : TaskDlgSketchBasedParameters(PipeView)
{
    assert(PipeView);
    parameter    = new TaskPipeParameters(PipeView,newObj);
    orientation  = new TaskPipeOrientation(PipeView,newObj);
    scaling      = new TaskPipeScaling(PipeView,newObj);

    parameter->signalSelectionModeChanged.connect(
            [this](){onSelectionModeChanged(this->parameter);});
    orientation->signalSelectionModeChanged.connect(
            [this](){onSelectionModeChanged(this->orientation);});
    scaling->signalSelectionModeChanged.connect(
            [this](){onSelectionModeChanged(this->scaling);});

    Content.push_back(parameter);
    Content.push_back(orientation);
    Content.push_back(scaling);

    if (PipeView->getObject()->isTouched() || PipeView->getObject()->mustExecute()) {
        parameter->setupTransaction();
        parameter->recomputeFeature();
    }
}

TaskDlgPipeParameters::~TaskDlgPipeParameters()
{
}

void TaskDlgPipeParameters::onSelectionModeChanged(TaskSketchBasedParameters *source)
{
    if (_busy)
        return;
    Base::StateLocker guard(_busy);
    parameter->setSelectionMode(source->getSelectionMode(), nullptr);
    orientation->setSelectionMode(source->getSelectionMode(), nullptr);
    scaling->setSelectionMode(source->getSelectionMode(), nullptr);
}

//==== calls from the TaskView ===============================================================


bool TaskDlgPipeParameters::accept()
{
    PartDesign::Pipe* pcPipe = static_cast<PartDesign::Pipe*>(getPipeView()->getObject());
    try {
        parameter->setupTransaction();
        Gui::cmdGuiDocument(pcPipe, "resetEdit()");
        Gui::cmdAppDocument(pcPipe, "recompute()");
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

#include "moc_TaskPipeParameters.cpp"
