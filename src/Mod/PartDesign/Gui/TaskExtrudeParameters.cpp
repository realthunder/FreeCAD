/***************************************************************************
 *   Copyright (c) 2021 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QSignalBlocker>
#endif

#include <App/Document.h>
#include <Base/ExceptionSafeCall.h>
#include <Base/Tools.h>
#include <Base/UnitsApi.h>
#include <Gui/Command.h>
#include <Gui/Fw/FwQtView.h>
#include <Gui/Fw/FwWidgets.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/Widgets.h>
#include <Mod/Part/App/GizmoHelper.h>
#include <Mod/PartDesign/App/FeatureExtrude.h>
#include <Mod/PartDesign/App/FeatureExtrusion.h>

#include "fwui_TaskPadPocketParameters.h"
#include "TaskExtrudeParameters.h"
#include "ReferenceSelection.h"
#include "Utils.h"


using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskExtrudeParameters */

TaskExtrudeParameters::TaskExtrudeParameters(ViewProviderSketchBased *SketchBasedView,
                                         QWidget *parent,
                                         const std::string& pixmapname,
                                         const QString& parname)
    : TaskSketchBasedParameters(SketchBasedView, parent, pixmapname, parname)
{
    // the form as models, realized by the Qt backend into the proxy
    form.reset(new Gui::Fw::UiForm());
    ui.reset(new Ui_TaskPadPocketParameters());
    ui->setupUi(form.get());
    proxy = Gui::FwQt::realize(form.get(), this);

    Gui::FwQt::widgetOf(ui->directionCB)->installEventFilter(this);

    this->initUI(proxy);
    if (vp && vp->getObject()) {
        hookPropertyBool(vp->getObject(), "Linearize",
                         Gui::FwQt::widgetOf(ui->checkBoxLinearize), "Linearize");
        // The up-to references, one per side: a face, faces, or shapes
        auto extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
        upToWidget = makeUpToWidget(ui->upToShapeHolder, extrude->UpToShape,
                                    SelectionMode::refUpTo);
        upToWidget2 = makeUpToWidget(ui->upToShapeHolder2, extrude->UpToShape2,
                                     SelectionMode::refUpTo2);
        // Where it starts, with StartType Reference (upstream bcc3e296fa):
        // one face, a datum plane or a sketch, picked like the up-to faces
        if (QWidget *holder = Gui::FwQt::widgetOf(ui->startReferenceHolder)) {
            startWidget = new LinkSubWidget(this, tr("Reference"), extrude->StartReference,
                                            /*singleElement*/true);
            startWidget->setSelectionMode(SelectionMode::refStart);
            startWidget->setHideLinked(false);
            startWidget->setSelectionConfig(AllowSelection::FACE
                                            | AllowSelection::OTHERBODY
                                            | AllowSelection::WHOLE);
            startWidget->setPickFilter(
                [this](const Gui::SelectionChanges &msg, App::SubObjectT &objT) {
                    return filterUpToPick(msg, objT);
                });
            auto layout = new QHBoxLayout(holder);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(startWidget);
        }
    }

    this->groupLayout()->addWidget(proxy);
}

LinkSubWidget *TaskExtrudeParameters::makeUpToWidget(Gui::Fw::Widget *holder,
                                                     App::PropertyLinkSubList &prop,
                                                     SelectionMode mode)
{
    QWidget *holderWidget = Gui::FwQt::widgetOf(holder);
    if (!holderWidget)
        return nullptr;
    auto widget = new LinkSubWidget(this, tr("Face"), prop);
    widget->setSelectionMode(mode);
    // what the extrusion goes up to stays in sight
    widget->setHideLinked(false);
    AllowSelectionFlags flags = AllowSelection::FACE
                                | AllowSelection::OTHERBODY
                                | AllowSelection::WIRE
                                | AllowSelection::CIRCLE
                                | AllowSelection::WHOLE;
    if (vp->getObject()->isDerivedFrom(PartDesign::Extrusion::getClassTypeId()))
        flags |= AllowSelection::POINT | AllowSelection::EDGE;
    widget->setSelectionConfig(flags);
    widget->setPickFilter([this](const Gui::SelectionChanges &msg, App::SubObjectT &objT) {
        return filterUpToPick(msg, objT);
    });
    auto layout = new QHBoxLayout(holderWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(widget);
    return widget;
}

TaskExtrudeParameters::~TaskExtrudeParameters() = default;

void TaskExtrudeParameters::setupDialog(bool newObj, const char *historyPath)
{
    // Get the feature data
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());

    // set decimals for the direction edits
    // do this here before the edits are filled to avoid rounding mistakes
    int UserDecimals = Base::UnitsApi::getDecimals();
    ui->XDirectionEdit->setDecimals(UserDecimals);
    ui->YDirectionEdit->setDecimals(UserDecimals);
    ui->ZDirectionEdit->setDecimals(UserDecimals);

    // Fill data into dialog elements
    // the direction combobox is later filled in updateUI()
    ui->taperAngleEdit->setMinimum(extrude->TaperAngle.getMinimum());
    ui->taperAngleEdit->setMaximum(extrude->TaperAngle.getMaximum());
    ui->taperAngleEdit->setSingleStep(extrude->TaperAngle.getStepSize());
    ui->taperAngleEdit2->setMinimum(extrude->TaperAngle2.getMinimum());
    ui->taperAngleEdit2->setMaximum(extrude->TaperAngle2.getMaximum());
    ui->taperAngleEdit2->setSingleStep(extrude->TaperAngle2.getStepSize());

    // Bind input fields to properties
    ui->lengthEdit->bind(extrude->Length);
    ui->lengthEdit2->bind(extrude->Length2);
    ui->startOffsetEdit->bind(extrude->StartOffset);
    ui->offsetEdit->bind(extrude->Offset);
    ui->offsetEdit2->bind(extrude->Offset2);
    ui->taperAngleEdit->bind(extrude->TaperAngle);
    ui->taperAngleEdit2->bind(extrude->TaperAngle2);
    ui->innerTaperEdit->bind(extrude->TaperInnerAngle);
    ui->innerTaperEdit2->bind(extrude->TaperInnerAngleRev);
    ui->XDirectionEdit->bind(App::ObjectIdentifier::parse(extrude, std::string("Direction.x")));
    ui->YDirectionEdit->bind(App::ObjectIdentifier::parse(extrude, std::string("Direction.y")));
    ui->ZDirectionEdit->bind(App::ObjectIdentifier::parse(extrude, std::string("Direction.z")));

    this->propReferenceAxis = &(extrude->ReferenceAxis);

    translateModeList();
    for (auto widget : {upToWidget, upToWidget2}) {
        if (widget)
            widget->setTitle(upToTitle());
    }
    translateTooltips();

    refresh();

    // set the history path
    QByteArray field;
    QByteArray path(historyPath);
    if (!path.endsWith('/'))
        path += "/";
    field = "Length";
    ui->lengthEdit->setEntryName(field);
    ui->lengthEdit->setParamGrpPath(path+field);
    field = "Length2";
    ui->lengthEdit2->setEntryName(field);
    ui->lengthEdit2->setParamGrpPath(path+field);
    field = "Offset";
    ui->offsetEdit->setEntryName(field);
    ui->offsetEdit->setParamGrpPath(path+field);
    field = "Offset2";
    ui->offsetEdit2->setEntryName(field);
    ui->offsetEdit2->setParamGrpPath(path+field);
    field = "TaperAngle";
    ui->taperAngleEdit->setEntryName(field);
    ui->taperAngleEdit->setParamGrpPath(path+field);
    field = "TaperAngle2";
    ui->taperAngleEdit2->setEntryName(field);
    ui->taperAngleEdit2->setParamGrpPath(path+field);
    field = "InnerTaperAngle";
    ui->innerTaperEdit->setEntryName(field);
    ui->innerTaperEdit->setParamGrpPath(path+field);
    field = "InnerTaperAngle2";
    ui->innerTaperEdit2->setEntryName(field);
    ui->innerTaperEdit2->setParamGrpPath(path+field);
    if(newObj)
        readValuesFromHistory();

    connectSlots();
    ui->lengthEdit->selectAll();

    setupGizmos();
}

void TaskExtrudeParameters::setupGizmos()
{
    if (GizmoContainer::isEnabled() == false) {
        return;
    }

    // A gizmo drives a Qt spin box, and this form's fields are Fw models: hand
    // it the widget the model is realized as, the route the panel already
    // takes for its other Qt-only calls. The widget's own sync carries a drag
    // to the model and the property. No widget, no gizmos.
    auto spinBoxOf = [](Gui::Fw::QuantitySpinBox* model) {
        return qobject_cast<Gui::QuantitySpinBox*>(Gui::FwQt::widgetOf(model));
    };
    auto length1 = spinBoxOf(ui->lengthEdit);
    auto length2 = spinBoxOf(ui->lengthEdit2);
    auto taper1 = spinBoxOf(ui->taperAngleEdit);
    auto taper2 = spinBoxOf(ui->taperAngleEdit2);
    auto startOffset = spinBoxOf(ui->startOffsetEdit);
    if (!length1 || !length2 || !taper1 || !taper2 || !startOffset) {
        return;
    }

    const auto toggleReversed = [this] {
        if (ui->checkBoxReversed->isEnabled()) {
            ui->checkBoxReversed->setChecked(!ui->checkBoxReversed->isChecked());
        }
    };

    lengthGizmo1 = new Gui::LinearGizmo(length1);
    lengthGizmo1->setClickCallback(toggleReversed);
    lengthGizmo2 = new Gui::LinearGizmo(length2);
    lengthGizmo2->setClickCallback(toggleReversed);
    startOffsetGizmo = new Gui::LinearGizmo(startOffset);
    startOffsetGizmo->setDraggerStyle(Gui::LinearDraggerStyle::Sphere);
    taperAngleGizmo1 = new Gui::RotationGizmo(taper1);
    taperAngleGizmo2 = new Gui::RotationGizmo(taper2);

    gizmoContainer = GizmoContainer::create(
        {lengthGizmo1, lengthGizmo2, startOffsetGizmo, taperAngleGizmo1, taperAngleGizmo2},
        vp
    );

    setGizmoPositions();
    showDraggerHints();
}

void TaskExtrudeParameters::setGizmoPositions()
{
    if (!gizmoContainer) {
        return;
    }

    auto extrude = vp ? dynamic_cast<PartDesign::FeatureExtrude*>(vp->getObject()) : nullptr;
    if (!extrude || extrude->isError()) {
        gizmoContainer->visible = false;
        return;
    }
    gizmoContainer->visible = true;

    PartDesign::TopoShape shape = extrude->getProfileShape();
    Base::Vector3d center = getMidPointFromProfile(shape);
    std::string sideType(extrude->SideType.getValueAsString());
    const bool twoSides = sideType == "Two sides";
    const bool symmetric = sideType == "Symmetric";
    const bool length1 = strcmp(extrude->Type.getValueAsString(), "Length") == 0;
    const bool length2 = twoSides && strcmp(extrude->Type2.getValueAsString(), "Length") == 0;
    double dir = extrude->Reversed.getValue() && !symmetric ? -1 : 1;

    Base::Vector3d direction = extrude->Direction.getValue() * dir;
    // The lengths are measured from the start, wherever StartType puts it,
    // and the start offset from the plane it is added to: the profile's,
    // or the reference's
    const bool hasStart = strcmp(extrude->StartType.getValueAsString(), "Profile plane") != 0;
    try {
        const Base::Vector3d startDir = direction.Normalized();
        const double start = extrude->getStartOffset();
        startOffsetGizmo->Gizmo::setDraggerPlacement(
            center + startDir * (start - extrude->StartOffset.getValue()), direction);
        center += startDir * start;
    }
    catch (const Base::Exception&) {
        // an unreachable reference: the feature is in error and says so
    }
    startOffsetGizmo->setVisibility(hasStart);

    lengthGizmo1->Gizmo::setDraggerPlacement(center, direction);
    lengthGizmo1->setVisibility(length1);
    taperAngleGizmo1->placeOverLinearGizmo(lengthGizmo1);
    taperAngleGizmo1->setVisibility(length1);
    lengthGizmo2->Gizmo::setDraggerPlacement(center, -direction);
    lengthGizmo2->setVisibility(length2);
    taperAngleGizmo2->placeOverLinearGizmo(lengthGizmo2);
    taperAngleGizmo2->setVisibility(length2);

    Base::Vector3d padDir = extrude->Direction.getValue().Normalized();
    Base::Vector3d sketchDir = extrude->getProfileNormal().Normalized();

    double lengthFactor = padDir.Dot(sketchDir);
    double multFactor = symmetric ? 0.5 : 1.0;

    // Important note: This code assumes that nothing other than alongSketchNormal
    // and symmetric option influence the multFactor. If some custom gizmos changes
    // it then that also should be handled properly here
    if (extrude->AlongSketchNormal.getValue()) {
        lengthGizmo1->setMultFactor(multFactor / lengthFactor);
        lengthGizmo2->setMultFactor(multFactor / lengthFactor);
    }
    else {
        lengthGizmo1->setMultFactor(multFactor);
        lengthGizmo2->setMultFactor(multFactor);
    }

    gizmoContainer->calculateScaleAndOrientation();
}

void TaskExtrudeParameters::finishedRecomputeFeature()
{
    TaskSketchBasedParameters::finishedRecomputeFeature();
    setGizmoPositions();
}

void TaskExtrudeParameters::refresh()
{
    if (!vp || !vp->getObject())
        return;

    // update direction combobox
    fillDirectionCombo();

    // Get the feature data
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    Base::Quantity l = extrude->Length.getQuantityValue();
    Base::Quantity l2 = extrude->Length2.getQuantityValue();
    bool useCustom = extrude->UseCustomVector.getValue();
    double xs = extrude->Direction.getValue().x;
    double ys = extrude->Direction.getValue().y;
    double zs = extrude->Direction.getValue().z;
    Base::Quantity off = extrude->Offset.getQuantityValue();
    Base::Quantity off2 = extrude->Offset2.getQuantityValue();
    bool reversed = extrude->Reversed.getValue();
    int sideType = extrude->SideType.getValue();
    int index = modeOf(extrude->Type); // must extract value here, clear() kills it!
    int index2 = modeOf(extrude->Type2);
    double angle = extrude->TaperAngle.getValue();
    double angle2 = extrude->TaperAngleRev.getValue();
    double innerAngle = extrude->TaperInnerAngle.getValue();
    double innerAngle2 = extrude->TaperInnerAngleRev.getValue();

    // Temporarily prevent unnecessary feature recomputes: the slots hang
    // on the models' signals (the backend's mirror is not a signal and
    // still reaches the widgets)
    for (auto* child : form->findChildren<Gui::Fw::Widget*>())
        child->blockSignals(true);

    // Fill data into dialog elements
    ui->lengthEdit->setValue(l);
    ui->lengthEdit2->setValue(l2);
    ui->startMode->setCurrentIndex(extrude->StartType.getValue());
    ui->startOffsetEdit->setValue(extrude->StartOffset.getQuantityValue());
    ui->XDirectionEdit->setEnabled(useCustom);
    ui->YDirectionEdit->setEnabled(useCustom);
    ui->ZDirectionEdit->setEnabled(useCustom);
    ui->XDirectionEdit->setValue(xs);
    ui->YDirectionEdit->setValue(ys);
    ui->ZDirectionEdit->setValue(zs);
    ui->offsetEdit->setValue(off);
    ui->offsetEdit2->setValue(off2);
    ui->taperAngleEdit->setValue(angle);
    ui->taperAngleEdit2->setValue(angle2);
    ui->innerTaperEdit->setValue(innerAngle);
    ui->innerTaperEdit2->setValue(innerAngle2);

    // According to bug #0000521 the reversed option
    // shouldn't be de-activated if the pad has a support face
    ui->checkBoxReversed->setChecked(reversed);

    ui->checkBoxUsePipe->setChecked(extrude->UsePipeForDraft.getValue());

    // Lost in a merge (bcaa82d71a): the box started unchecked, and accepting
    // the panel wrote that back, so a pad in a custom direction measured
    // along the sketch normal went over to measuring along the direction
    ui->checkBoxAlongDirection->setChecked(extrude->AlongSketchNormal.getValue());

    for (auto widget : {upToWidget, upToWidget2, startWidget}) {
        if (widget)
            widget->refresh();
    }

    ui->sideTypeCB->setCurrentIndex(sideType);
    ui->changeMode->setCurrentIndex(index);
    ui->changeMode2->setCurrentIndex(index2);

    ui->checkFaceLimits->setChecked(extrude->CheckUpToFaceLimits.getValue());

    ui->autoInnerTaperAngle->setChecked(extrude->AutoTaperInnerAngle.getValue());

    for (auto* child : form->findChildren<Gui::Fw::Widget*>())
        child->blockSignals(false);

    setCheckboxes();
    updateStartUI();
    TaskSketchBasedParameters::refresh();
}

void TaskExtrudeParameters::readValuesFromHistory()
{
    ui->lengthEdit->setToLastUsedValue();
    ui->lengthEdit->selectNumber();
    ui->lengthEdit2->setToLastUsedValue();
    ui->lengthEdit2->selectNumber();
    ui->offsetEdit->setToLastUsedValue();
    ui->offsetEdit->selectNumber();
    ui->offsetEdit2->setToLastUsedValue();
    ui->offsetEdit2->selectNumber();
    ui->taperAngleEdit->setToLastUsedValue();
    ui->taperAngleEdit->selectNumber();
    ui->taperAngleEdit2->setToLastUsedValue();
    ui->taperAngleEdit2->selectNumber();
    ui->innerTaperEdit->setToLastUsedValue();
    ui->innerTaperEdit->selectNumber();
    ui->innerTaperEdit2->setToLastUsedValue();
    ui->innerTaperEdit2->selectNumber();
}

void TaskExtrudeParameters::connectSlots()
{
    QMetaObject::connectSlotsByName(this);

    Base::connect(ui->lengthEdit, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onLengthChanged);
    Base::connect(ui->lengthEdit2, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onLength2Changed);
    Base::connect(ui->offsetEdit, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onOffsetChanged);
    Base::connect(ui->offsetEdit2, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onOffset2Changed);
    Base::connect(ui->startMode, qOverload<int>(&Gui::Fw::QComboBox::currentIndexChanged),
        this, &TaskExtrudeParameters::onStartModeChanged);
    Base::connect(ui->startOffsetEdit, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onStartOffsetChanged);
    Base::connect(ui->sideTypeCB, qOverload<int>(&Gui::Fw::QComboBox::currentIndexChanged),
        this, &TaskExtrudeParameters::onSideTypeChanged);
    Base::connect(ui->changeMode2, qOverload<int>(&Gui::Fw::QComboBox::currentIndexChanged),
        this, &TaskExtrudeParameters::onMode2Changed);
    Base::connect(ui->taperAngleEdit, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onTaperChanged);
    Base::connect(ui->taperAngleEdit2, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onTaper2Changed);
    Base::connect(ui->innerTaperEdit, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onInnerAngleChanged);
    Base::connect(ui->innerTaperEdit2, qOverload<double>(&Gui::Fw::QuantitySpinBox::valueChanged),
        this, &TaskExtrudeParameters::onInnerAngle2Changed);
    Base::connect(ui->directionCB, qOverload<int>(&Gui::Fw::QComboBox::activated),
        this, &TaskExtrudeParameters::onDirectionCBChanged);
    Base::connect(ui->directionCB, QOverload<int>::of(&Gui::Fw::QComboBox::highlighted),
        [this](int index) {
            if (index >= 3 && index < (int)axesInList.size())
                PartDesignGui::highlightObjectOnTop(axesInList[index]);
            else
                Gui::Selection().rmvPreselect();
        });
    Base::connect(ui->checkBoxAlongDirection, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onAlongSketchNormalChanged);
    Base::connect(ui->XDirectionEdit, qOverload<double>(&Gui::Fw::DoubleSpinBox::valueChanged),
        this, &TaskExtrudeParameters::onXDirectionEditChanged);
    Base::connect(ui->YDirectionEdit, qOverload<double>(&Gui::Fw::DoubleSpinBox::valueChanged),
        this, &TaskExtrudeParameters::onYDirectionEditChanged);
    Base::connect(ui->ZDirectionEdit, qOverload<double>(&Gui::Fw::DoubleSpinBox::valueChanged),
        this, &TaskExtrudeParameters::onZDirectionEditChanged);
    Base::connect(ui->checkBoxReversed, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onReversedChanged);
    Base::connect(ui->checkBoxUsePipe, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onUsePipeChanged);
    Base::connect(ui->checkFaceLimits, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onCheckFaceLimitsChanged);
    Base::connect(ui->changeMode, qOverload<int>(&Gui::Fw::QComboBox::currentIndexChanged),
        this, &TaskExtrudeParameters::onModeChanged);
    Base::connect(static_cast<Gui::Fw::QAbstractButton*>(ui->autoInnerTaperAngle),
                  &Gui::Fw::QAbstractButton::toggled, [this](bool checked) {
        PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
        setupTransaction();
        extrude->AutoTaperInnerAngle.setValue(checked);
        setCheckboxes();
        recomputeFeature();
    });
}

void TaskExtrudeParameters::_onSelectionChanged(const Gui::SelectionChanges& msg)
{
    // The up-to references pick through their own widgets
    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        // if we have an edge selection for the pad direction
        if (getSelectionMode() == SelectionMode::refAxis) {
            selectedReferenceAxis(msg);
        }
    }
}

bool TaskExtrudeParameters::eventFilter(QObject *o, QEvent *ev)
{
    (void)o;
    if (ev->type() == QEvent::Leave)
        Gui::Selection().rmvPreselect();
    return false;
}

void TaskExtrudeParameters::setSideMode(App::PropertyEnumeration &type,
                                        const App::PropertyLinkSubList &upToShape,
                                        int mode)
{
    if (mode != static_cast<int>(Modes::ToFace)) {
        // the modes before it are the Types of the same index
        type.setValue(static_cast<long>(mode));
        return;
    }
    bool several = upToShape.getSize() && !PartDesign::FeatureExtrude::isSingleUpToFace(upToShape);
    type.setValue(several ? "UpToShape" : "UpToFace");
}

int TaskExtrudeParameters::modeOf(const App::PropertyEnumeration &type)
{
    const char *name = type.getValueAsString();
    if (strcmp(name, "UpToFace") == 0 || strcmp(name, "UpToShape") == 0)
        return static_cast<int>(Modes::ToFace);
    // Only an unrestored file has it: the feature makes it two sides
    if (strcmp(name, "TwoLengths") == 0)
        return static_cast<int>(Modes::Dimension);
    return type.getValue();
}

void TaskExtrudeParameters::updateStartUI()
{
    const int type = ui->startMode->currentIndex();
    ui->labelStartOffset->setVisible(type != 0);
    ui->startOffsetEdit->setVisible(type != 0);
    ui->startReferenceHolder->setVisible(type == 2);
    // Leave the start pick once there is no reference to pick
    if (type != 2 && getSelectionMode() == SelectionMode::refStart)
        exitSelectionMode();
}

void TaskExtrudeParameters::onStartModeChanged(int index)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->StartType.setValue(static_cast<long>(index));
    updateStartUI();
    // nothing to start at yet: pick it first
    if (index == 2 && !extrude->StartReference.getValue()) {
        if (startWidget)
            startWidget->startSelection();
        return;
    }
    recomputeFeature();
}

void TaskExtrudeParameters::onStartOffsetChanged(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->StartOffset.setValue(len);
    recomputeFeature();
}

void TaskExtrudeParameters::onSideTypeChanged(int index)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->SideType.setValue(static_cast<long>(index));
    setCheckboxes();
    recomputeFeature();
}

void TaskExtrudeParameters::onMode2Changed(int index)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    setSideMode(extrude->Type2, extrude->UpToShape2, index);
    setCheckboxes();
    // nothing to go up to yet: pick it first
    if (static_cast<Modes>(index) == Modes::ToFace && !extrude->UpToShape2.getSize()) {
        if (upToWidget2)
            upToWidget2->startSelection();
        return;
    }
    recomputeFeature();
}

void TaskExtrudeParameters::onOffset2Changed(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Offset2.setValue(len);
    recomputeFeature();
}

void TaskExtrudeParameters::selectedReferenceAxis(const Gui::SelectionChanges& msg)
{
    std::vector<std::string> edge;
    App::DocumentObject* selObj;
    if (getReferencedSelection(vp->getObject(), msg, selObj, edge) && selObj) {
        propReferenceAxis->setValue(selObj, edge);
        // update direction combobox
        fillDirectionCombo();
        exitSelectionMode();
        recomputeFeature();
    }
}

void TaskExtrudeParameters::onLengthChanged(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Length.setValue(len);
    recomputeFeature();
}

void TaskExtrudeParameters::onLength2Changed(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Length2.setValue(len);
    recomputeFeature();
}

void TaskExtrudeParameters::onOffsetChanged(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Offset.setValue(len);
    recomputeFeature();
}

void TaskExtrudeParameters::onTaperChanged(double angle)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->TaperAngle.setValue(angle);
    recomputeFeature();
}

void TaskExtrudeParameters::onTaper2Changed(double angle)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->TaperAngle2.setValue(angle);
    recomputeFeature();
}

void TaskExtrudeParameters::onInnerAngleChanged(double angle)
{
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->TaperInnerAngle.setValue(angle);
    recomputeFeature();
}

void TaskExtrudeParameters::onInnerAngle2Changed(double angle)
{
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->TaperInnerAngleRev.setValue(angle);
    recomputeFeature();
}

bool TaskExtrudeParameters::hasProfileFace(PartDesign::ProfileBased* profile) const
{
    try {
        Part::Feature* pcFeature = profile->getVerifiedObject();
        Base::Vector3d SketchVector = profile->getProfileNormal();
        Q_UNUSED(pcFeature)
        Q_UNUSED(SketchVector)
        return true;
    }
    catch (const Base::Exception&) {
    }

    return false;
}

void TaskExtrudeParameters::fillDirectionCombo()
{
    bool oldVal_blockUpdate = blockUpdate;
    blockUpdate = true;

    QSignalBlocker blocker(ui->directionCB);

    if (axesInList.empty()) {
        ui->directionCB->clear();
        // add sketch normal
        addAxisToCombo(nullptr, "", QObject::tr("Profile normal"));
        // add the other entries
        addAxisToCombo(nullptr, "", tr("Select reference..."));
        // we start with the sketch normal as proposal for the custom direction
        addAxisToCombo(nullptr, "", QObject::tr("Custom direction"));
    }

    // add current link, if not in list
    // first, figure out the item number for current axis
    int indexOfCurrent = -1;
    App::DocumentObject* ax = propReferenceAxis->getValue();
    const std::vector<std::string>& subList = propReferenceAxis->getSubValues(false);
    if (!ax)
        indexOfCurrent = 0;
    else {
        int i = -1;
        for (const auto &objT : axesInList) {
            ++i;
            if (ax != objT.getObject()) continue;
            if (subList.empty()) {
                if (objT.getSubName().size()) 
                    continue;
            }
            else if (subList[0] != objT.getSubName())
                continue;
            indexOfCurrent = i;
            break;
        }
    }
    // if the axis is not yet listed in the combobox
    if (indexOfCurrent == -1 && ax) {
        assert(subList.size() <= 1);
        std::string sub;
        if (!subList.empty())
            sub = subList[0];
        addAxisToCombo(ax, sub, getRefStr(ax, subList));
        indexOfCurrent = axesInList.size() - 1;
    }

    // highlight either current index or set custom direction
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    bool hasCustom = extrude->UseCustomVector.getValue();
    if (indexOfCurrent != -1 && !hasCustom)
        ui->directionCB->setCurrentIndex(indexOfCurrent);
    if (hasCustom)
        ui->directionCB->setCurrentIndex(DirectionModes::Custom);

    updateDirectionUI(ui->directionCB->currentIndex());

    blockUpdate = oldVal_blockUpdate;
}

void TaskExtrudeParameters::addAxisToCombo(App::DocumentObject* linkObj, const std::string &linkSubname, const QString &itemText)
{
    this->ui->directionCB->addItem(itemText);
    this->axesInList.emplace_back(linkObj, linkSubname.c_str());
}

void TaskExtrudeParameters::setCheckboxes()
{
    if (!vp)
        return;

    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    std::string sideType(extrude->SideType.getValueAsString());
    bool twoSides = sideType == "Two sides";
    bool symmetric = sideType == "Symmetric";

    // disable/hide everything unless we are sure we don't need it
    // exception: the direction parameters are in any case visible
    struct SideUi {
        bool length = false;
        bool offset = false;
        bool upTo = false;
        bool taper = false;
    };
    auto sideUi = [this](Modes mode) {
        SideUi side;
        switch (mode) {
        case Modes::Dimension:
            side.length = true;
            side.taper = true;
            break;
        case Modes::ThroughAll:
            // Through all (Pocket): no offset, it has no meaning through all,
            // and a disabled field only said so (upstream 6d238a93e1); a
            // taper does (d52260b2f4). To last (Pad): an offset.
            if (isPocket())
                side.taper = true;
            else
                side.offset = true;
            break;
        case Modes::ToFirst:
            side.offset = true;
            break;
        case Modes::ToFace:
            side.offset = true;
            side.upTo = true;
            break;
        }
        return side;
    };
    auto mode = static_cast<Modes>(getMode());
    SideUi side1 = sideUi(mode);
    SideUi side2;
    if (twoSides)
        side2 = sideUi(static_cast<Modes>(ui->changeMode2->currentIndex()));

    if (mode == Modes::Dimension) {
        ui->lengthEdit->selectNumber();
        QMetaObject::invokeMethod(ui->lengthEdit, "setFocus", Qt::QueuedConnection);
    }

    ui->lengthEdit->setVisible(side1.length);
    ui->lengthEdit->setEnabled(side1.length);
    ui->labelLength->setVisible(side1.length);
    lengthShown = side1.length || side2.length;
    updateDirectionUI(ui->directionCB->currentIndex());

    ui->offsetEdit->setVisible(side1.offset);
    ui->offsetEdit->setEnabled(side1.offset);
    ui->labelOffset->setVisible(side1.offset);
    ui->upToShapeHolder->setVisible(side1.upTo);

    ui->groupBoxSide2->setVisible(twoSides);
    ui->lengthEdit2->setVisible(side2.length);
    ui->lengthEdit2->setEnabled(side2.length);
    ui->labelLength2->setVisible(side2.length);
    ui->offsetEdit2->setVisible(side2.offset);
    ui->offsetEdit2->setEnabled(side2.offset);
    ui->labelOffset2->setVisible(side2.offset);
    ui->upToShapeHolder2->setVisible(side2.upTo);

    ui->taperAngleEdit->setVisible(side1.taper);
    ui->taperAngleEdit->setEnabled(side1.taper);
    ui->labelTaperAngle->setVisible(side1.taper);

    ui->taperAngleEdit2->setVisible(side2.taper);
    ui->taperAngleEdit2->setEnabled(side2.taper);
    ui->labelTaperAngle2->setVisible(side2.taper);

    if (extrude->AutoTaperInnerAngle.getValue()) {
        ui->innerTaperEdit->setEnabled( false );
        ui->innerTaperEdit2->setEnabled( false );
    } else {
        ui->innerTaperEdit->setEnabled( side1.taper );
        ui->innerTaperEdit2->setEnabled( side2.taper );
    }

    ui->innerTaperEdit->setVisible( side1.taper );
    ui->innerTaperEdit2->setVisible( side2.taper );
    ui->labelInnerTaperAngle->setVisible( side1.taper );
    ui->labelInnerTaperAngle2->setVisible( side2.taper );

    // Symmetric goes both ways; the checkbox would say nothing
    ui->checkBoxReversed->setEnabled(!symmetric);
    ui->checkFaceLimits->setVisible(side1.offset || side2.offset);

    // Leave a side's up-to picking once that side is not up to a face
    if ((getSelectionMode() == SelectionMode::refUpTo && !side1.upTo)
            || (getSelectionMode() == SelectionMode::refUpTo2 && !side2.upTo))
        exitSelectionMode();
}

void TaskExtrudeParameters::onDirectionCBChanged(int num)
{
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());

    if (axesInList.empty() || !extrude)
        return;

    if (num == DirectionModes::Normal || num == DirectionModes::Custom) {
        exitSelectionMode();
        setupTransaction();
        propReferenceAxis->setValue(nullptr);
        setDirectionMode(num);
    }
    else if (num == DirectionModes::Select) {
        // enter reference selection mode
        // to distinguish that this is the direction selection
        setDirectionMode(num);
        TaskSketchBasedParameters::onSelectReference(Gui::FwQt::widgetOf(ui->labelEdge), SelectionMode::refAxis,
                                                     AllowSelection::EDGE |
                                                     AllowSelection::PLANAR |
                                                     AllowSelection::CIRCLE);
        return;
    }
    else {
        const auto & objT = axesInList[num];
        if (auto obj = objT.getObject()) {
            setupTransaction();
            if (objT.getSubName().empty())
                propReferenceAxis->setValue(obj);
            else
                propReferenceAxis->setValue(obj, {objT.getSubName()});
            // in case user is in selection mode, but changed his mind before selecting anything
            exitSelectionMode();

            setDirectionMode(num);
            updateDirectionEdits();
            recomputeFeature();
        }
        else {
            Base::Console().Error("Object was deleted\n");
            return;
        }
    }
}

void TaskExtrudeParameters::onAlongSketchNormalChanged(bool on)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->AlongSketchNormal.setValue(on);
    recomputeFeature();
}

void TaskExtrudeParameters::onXDirectionEditChanged(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Direction.setValue(len, extrude->Direction.getValue().y, extrude->Direction.getValue().z);
    // checking for case of a null vector is done in FeatureExtrude.cpp
    // if there was a null vector, the normal vector of the sketch is used.
    // therefore the vector component edits must be updated
    updateDirectionEdits();
    recomputeFeature();
}

void TaskExtrudeParameters::onYDirectionEditChanged(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Direction.setValue(extrude->Direction.getValue().x, len, extrude->Direction.getValue().z);
    updateDirectionEdits();
    recomputeFeature();
}

void TaskExtrudeParameters::onZDirectionEditChanged(double len)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Direction.setValue(extrude->Direction.getValue().x, extrude->Direction.getValue().y, len);
    recomputeFeature();
    updateDirectionEdits();
}

void TaskExtrudeParameters::updateDirectionEdits()
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    // we don't want to execute the onChanged edits, but just update their contents
    QSignalBlocker xdir(ui->XDirectionEdit);
    QSignalBlocker ydir(ui->YDirectionEdit);
    QSignalBlocker zdir(ui->ZDirectionEdit);
    ui->XDirectionEdit->setValue(extrude->Direction.getValue().x);
    ui->YDirectionEdit->setValue(extrude->Direction.getValue().y);
    ui->ZDirectionEdit->setValue(extrude->Direction.getValue().z);
}

void TaskExtrudeParameters::setDirectionMode(int index)
{
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    setupTransaction();
    extrude->UseCustomVector.setValue(index == DirectionModes::Custom);
    updateDirectionUI(index);
}

void TaskExtrudeParameters::updateDirectionUI(int index)
{
    // The profile normal needs nothing more; a reference shows its vector,
    // read only; a custom direction is typed in (upstream 873fa449ce)
    bool normal = index == DirectionModes::Normal;
    bool custom = index == DirectionModes::Custom;
    ui->groupBoxDirection->setVisible(!normal);
    ui->XDirectionEdit->setEnabled(custom);
    ui->YDirectionEdit->setEnabled(custom);
    ui->ZDirectionEdit->setEnabled(custom);
    // Measuring along the normal is the same thing when the direction is it
    ui->checkBoxAlongDirection->setVisible(!normal && lengthShown);
    ui->checkBoxAlongDirection->setEnabled(!normal);
}

void TaskExtrudeParameters::onUsePipeChanged(bool on)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->UsePipeForDraft.setValue(on);
    recomputeFeature();
}

void TaskExtrudeParameters::onCheckFaceLimitsChanged(bool on)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->CheckUpToFaceLimits.setValue(on);
    recomputeFeature();
}

void TaskExtrudeParameters::onReversedChanged(bool on)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Reversed.setValue(on);
    // update the direction
    updateDirectionEdits();
    recomputeFeature();
}

void TaskExtrudeParameters::getReferenceAxis(App::DocumentObject*& obj, std::vector<std::string>& sub) const
{
    if (axesInList.empty())
        THROWM(Base::RuntimeError, "Not initialized!")

    int num = ui->directionCB->currentIndex();
    const auto& objT = axesInList[num];
    if (objT.getObjectName().empty()) {
        // Note: It is possible that a face of an object is directly padded without defining a profile shape
        obj = nullptr;
        sub.clear();
        //throw Base::RuntimeError("Still in reference selection mode; reference wasn't selected yet");
    }
    else {
        obj = objT.getObject();
        if (!obj)
            THROWM(Base::RuntimeError, "Object was deleted")

        if (objT.getSubName().size()) {
            sub.resize(1);
            sub[0] = objT.getSubName();
        }
    }
}

double TaskExtrudeParameters::getOffset() const
{
    return ui->offsetEdit->value().getValue();
}

bool TaskExtrudeParameters::getAlongSketchNormal() const
{
    return ui->checkBoxAlongDirection->isChecked();
}

bool TaskExtrudeParameters::getCustom() const
{
    return (ui->directionCB->currentIndex() == DirectionModes::Custom);
}

std::string TaskExtrudeParameters::getReferenceAxis() const
{
    std::vector<std::string> sub;
    App::DocumentObject* obj;
    getReferenceAxis(obj, sub);
    return buildLinkSingleSubPythonStr(obj, sub);
}

double TaskExtrudeParameters::getXDirection() const
{
    return ui->XDirectionEdit->value();
}

double TaskExtrudeParameters::getYDirection() const
{
    return ui->YDirectionEdit->value();
}

double TaskExtrudeParameters::getZDirection() const
{
    return ui->ZDirectionEdit->value();
}

bool TaskExtrudeParameters::getReversed() const
{
    return ui->checkBoxReversed->isChecked();
}

int TaskExtrudeParameters::getMode() const
{
    return ui->changeMode->currentIndex();
}

void TaskExtrudeParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        QSignalBlocker length(ui->lengthEdit);
        QSignalBlocker length2(ui->lengthEdit2);
        QSignalBlocker offset(ui->offsetEdit);
        QSignalBlocker offset2(ui->offsetEdit2);
        QSignalBlocker taper(ui->taperAngleEdit);
        QSignalBlocker taper2(ui->taperAngleEdit2);
        QSignalBlocker innerTaper(ui->innerTaperEdit);
        QSignalBlocker innerTaper2(ui->innerTaperEdit2);
        QSignalBlocker xdir(ui->XDirectionEdit);
        QSignalBlocker ydir(ui->YDirectionEdit);
        QSignalBlocker zdir(ui->ZDirectionEdit);
        QSignalBlocker dir(ui->directionCB);
        QSignalBlocker startMode(ui->startMode);
        QSignalBlocker sideType(ui->sideTypeCB);
        QSignalBlocker mode(ui->changeMode);
        QSignalBlocker mode2(ui->changeMode2);

        // Save all items
        QStringList items;
        for (int i = 0; i < ui->directionCB->count(); i++)
            items << ui->directionCB->itemText(i);

        // Translate direction items
        int index = ui->directionCB->currentIndex();
        // (the realized form retranslates itself on the language change)

        // Keep custom items
        for (int i = 0; i < ui->directionCB->count(); i++)
            items.pop_front();
        ui->directionCB->addItems(items);
        ui->directionCB->setCurrentIndex(index);

        // Translate mode items
        translateModeList();
        for (auto widget : {upToWidget, upToWidget2}) {
            if (widget)
                widget->setTitle(upToTitle());
        }
        if (startWidget)
            startWidget->setTitle(tr("Reference"));
        translateTooltips();

        axesInList.clear();
        fillDirectionCombo();
    }
}

void TaskExtrudeParameters::saveHistory()
{
    // save the user values to history
    ui->lengthEdit->pushToHistory();
    ui->lengthEdit2->pushToHistory();
    ui->offsetEdit->pushToHistory();
    ui->offsetEdit2->pushToHistory();
    ui->taperAngleEdit->pushToHistory();
    ui->taperAngleEdit2->pushToHistory();
    ui->innerTaperEdit->pushToHistory();
    ui->innerTaperEdit2->pushToHistory();

    TaskSketchBasedParameters::saveHistory();
}

void TaskExtrudeParameters::applyParameters()
{
    if (!vp)
        return;
    auto obj = vp->getObject();
    auto extrude = static_cast<PartDesign::FeatureExtrude*>(obj);

    ui->lengthEdit->apply();
    ui->lengthEdit2->apply();
    ui->startOffsetEdit->apply();
    ui->taperAngleEdit->apply();
    ui->taperAngleEdit2->apply();
    ui->innerTaperEdit->apply();
    ui->innerTaperEdit2->apply();
    FCMD_OBJ_CMD(obj, "UseCustomVector = " << (getCustom() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "Direction = ("
        << getXDirection() << ", " << getYDirection() << ", " << getZDirection() << ")");
    FCMD_OBJ_CMD(obj, "ReferenceAxis = " << getReferenceAxis());
    FCMD_OBJ_CMD(obj, "AlongSketchNormal = " << (getAlongSketchNormal() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "SideType = '" << extrude->SideType.getValueAsString() << "'");
    // The reference first: setting it sets Type between UpToFace and UpToShape
    FCMD_OBJ_CMD(obj, "UpToShape = " << buildLinkSubListPythonStr(
                extrude->UpToShape.getValues(), extrude->UpToShape.getSubValues()));
    FCMD_OBJ_CMD(obj, "UpToShape2 = " << buildLinkSubListPythonStr(
                extrude->UpToShape2.getValues(), extrude->UpToShape2.getSubValues()));
    FCMD_OBJ_CMD(obj, "Type = '" << extrude->Type.getValueAsString() << "'");
    FCMD_OBJ_CMD(obj, "Type2 = '" << extrude->Type2.getValueAsString() << "'");
    FCMD_OBJ_CMD(obj, "Reversed = " << (getReversed() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "Offset = " << getOffset());
    FCMD_OBJ_CMD(obj, "Offset2 = " << ui->offsetEdit2->value().getValue());
    FCMD_OBJ_CMD(obj, "StartType = '" << extrude->StartType.getValueAsString() << "'");
    FCMD_OBJ_CMD(obj, "StartReference = " << buildLinkSingleSubPythonStr(
                extrude->StartReference.getValue(), extrude->StartReference.getSubValues()));
}

void TaskExtrudeParameters::onModeChanged(int)
{
    // implement in sub-class
}

void TaskExtrudeParameters::translateModeList()
{
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    fillModeList(ui->changeMode);
    ui->changeMode->setCurrentIndex(modeOf(extrude->Type));
    fillModeList(ui->changeMode2);
    ui->changeMode2->setCurrentIndex(modeOf(extrude->Type2));
}

void TaskExtrudeParameters::fillModeList(Gui::Fw::QComboBox *)
{
    // implement in sub-class
}

QString TaskExtrudeParameters::upToTitle() const
{
    return tr("Face");
}

void TaskExtrudeParameters::translateTooltips()
{
    ui->offsetEdit->setToolTip(tr("Offset from face at which pad will end"));
    ui->checkBoxReversed->setToolTip(tr("Reverses pad direction"));
}


#include "moc_TaskExtrudeParameters.cpp"
