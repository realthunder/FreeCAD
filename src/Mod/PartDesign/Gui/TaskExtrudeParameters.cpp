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
#include <Gui/Widgets.h>
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

    QWidget* lineFaceName = Gui::FwQt::widgetOf(ui->lineFaceName);
    addBlinkWidget(lineFaceName);

    lineFaceName->installEventFilter(this);
    lineFaceName->setMouseTracking(true);

    Gui::FwQt::widgetOf(ui->directionCB)->installEventFilter(this);

    this->initUI(proxy);
    if (vp && vp->getObject()) {
        hookPropertyBool(vp->getObject(), "Linearize",
                         Gui::FwQt::widgetOf(ui->checkBoxLinearize), "Linearize");
    }

    Gui::ButtonGroup* group = new Gui::ButtonGroup(this);
    group->addButton(qobject_cast<QAbstractButton*>(Gui::FwQt::widgetOf(ui->checkBoxMidplane)));
    group->addButton(qobject_cast<QAbstractButton*>(Gui::FwQt::widgetOf(ui->checkBoxReversed)));
    group->setExclusive(true);

    this->groupLayout()->addWidget(proxy);
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
    ui->offsetEdit->bind(extrude->Offset);
    ui->taperAngleEdit->bind(extrude->TaperAngle);
    ui->taperAngleEdit2->bind(extrude->TaperAngle2);
    ui->innerTaperEdit->bind(extrude->TaperInnerAngle);
    ui->innerTaperEdit2->bind(extrude->TaperInnerAngleRev);
    ui->XDirectionEdit->bind(App::ObjectIdentifier::parse(extrude, std::string("Direction.x")));
    ui->YDirectionEdit->bind(App::ObjectIdentifier::parse(extrude, std::string("Direction.y")));
    ui->ZDirectionEdit->bind(App::ObjectIdentifier::parse(extrude, std::string("Direction.z")));

    this->propReferenceAxis = &(extrude->ReferenceAxis);

    translateModeList(extrude->Type.getValue());
    translateFaceNamePlaceHolder();
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
    bool midplane = extrude->Midplane.getValue();
    bool reversed = extrude->Reversed.getValue();
    int index = extrude->Type.getValue(); // must extract value here, clear() kills it!
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
    ui->XDirectionEdit->setEnabled(useCustom);
    ui->YDirectionEdit->setEnabled(useCustom);
    ui->ZDirectionEdit->setEnabled(useCustom);
    ui->XDirectionEdit->setValue(xs);
    ui->YDirectionEdit->setValue(ys);
    ui->ZDirectionEdit->setValue(zs);
    ui->offsetEdit->setValue(off);
    ui->taperAngleEdit->setValue(angle);
    ui->taperAngleEdit2->setValue(angle2);
    ui->innerTaperEdit->setValue(innerAngle);
    ui->innerTaperEdit2->setValue(innerAngle2);

    ui->checkBoxMidplane->setChecked(midplane);
    // According to bug #0000521 the reversed option
    // shouldn't be de-activated if the pad has a support face
    ui->checkBoxReversed->setChecked(reversed);

    ui->checkBoxUsePipe->setChecked(extrude->UsePipeForDraft.getValue());

    // Set object labels
    App::DocumentObject* obj = extrude->UpToFace.getValue();
    std::vector<std::string> subStrings = extrude->UpToFace.getSubValues(false);
    if (obj && (subStrings.empty() || subStrings.front().empty())) {
        ui->lineFaceName->setText(QString::fromUtf8(obj->Label.getValue()));
        ui->lineFaceName->setProperty("FeatureName", QByteArray(obj->getNameInDocument()));
        ui->lineFaceName->setProperty("FaceName", QVariant());
    }
    else if (obj) {
        ui->lineFaceName->setText(QStringLiteral("%1:%2")
                                  .arg(QString::fromUtf8(obj->Label.getValue()))
                                  .arg(QString::fromUtf8(subStrings.front().c_str())));
        ui->lineFaceName->setProperty("FeatureName", QByteArray(obj->getNameInDocument()));
        ui->lineFaceName->setProperty("FaceName", QByteArray(subStrings.front().c_str()));

    }
    else {
        ui->lineFaceName->clear();
        ui->lineFaceName->setProperty("FeatureName", QVariant());
        ui->lineFaceName->setProperty("FaceName", QVariant());
    }

    ui->changeMode->setCurrentIndex(index);

    ui->checkFaceLimits->setChecked(extrude->CheckUpToFaceLimits.getValue());

    ui->autoInnerTaperAngle->setChecked(extrude->AutoTaperInnerAngle.getValue());

    for (auto* child : form->findChildren<Gui::Fw::Widget*>())
        child->blockSignals(false);

    setCheckboxes();
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
    Base::connect(ui->groupBoxDirection, &Gui::Fw::QGroupBox::toggled,
        this, &TaskExtrudeParameters::onDirectionToggled);
    Base::connect(ui->XDirectionEdit, qOverload<double>(&Gui::Fw::DoubleSpinBox::valueChanged),
        this, &TaskExtrudeParameters::onXDirectionEditChanged);
    Base::connect(ui->YDirectionEdit, qOverload<double>(&Gui::Fw::DoubleSpinBox::valueChanged),
        this, &TaskExtrudeParameters::onYDirectionEditChanged);
    Base::connect(ui->ZDirectionEdit, qOverload<double>(&Gui::Fw::DoubleSpinBox::valueChanged),
        this, &TaskExtrudeParameters::onZDirectionEditChanged);
    Base::connect(ui->checkBoxMidplane, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onMidplaneChanged);
    Base::connect(ui->checkBoxReversed, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onReversedChanged);
    Base::connect(ui->checkBoxUsePipe, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onUsePipeChanged);
    Base::connect(ui->checkFaceLimits, &Gui::Fw::QCheckBox::toggled,
        this, &TaskExtrudeParameters::onCheckFaceLimitsChanged);
    Base::connect(ui->changeMode, qOverload<int>(&Gui::Fw::QComboBox::currentIndexChanged),
        this, &TaskExtrudeParameters::onModeChanged);
    Base::connect(ui->buttonFace, &Gui::Fw::QPushButton::toggled,
        this, &TaskExtrudeParameters::onButtonFace);
    Base::connect(ui->lineFaceName, &Gui::Fw::QLineEdit::textEdited,
        this, &TaskExtrudeParameters::onFaceName);
    Base::connect(static_cast<Gui::Fw::QAbstractButton*>(ui->autoInnerTaperAngle),
                  &Gui::Fw::QAbstractButton::toggled, [this](bool checked) {
        PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
        setupTransaction();
        extrude->AutoTaperInnerAngle.setValue(checked);
        setCheckboxes();
        recomputeFeature();
    });
}

void TaskExtrudeParameters::onButtonFace(const bool pressed)
{
    if (!vp || !pressed) {
        exitSelectionMode();
        return;
    }

    AllowSelectionFlags flags = AllowSelection::FACE
                                | AllowSelection::OTHERBODY
                                | AllowSelection::WIRE
                                | AllowSelection::CIRCLE;
    if (vp->getObject()->isDerivedFrom(PartDesign::Extrusion::getClassTypeId())) {
        flags |= AllowSelection::POINT | AllowSelection::EDGE;
    }
    TaskSketchBasedParameters::onSelectReference(Gui::FwQt::widgetOf(ui->buttonFace), flags);
}

void TaskExtrudeParameters::onSelectionModeChanged(SelectionMode)
{
    if (getSelectionMode() == SelectionMode::refAdd) {
        ui->buttonFace->setChecked(true);
    } else {
        ui->buttonFace->setChecked(false);
    }
}

void TaskExtrudeParameters::_onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (msg.Type == Gui::SelectionChanges::AddSelection) {
        // if we have an edge selection for the pad direction
        if (getSelectionMode() == SelectionMode::refAxis) {
            selectedReferenceAxis(msg);
        }
        else if (getSelectionMode() == SelectionMode::refAdd) {
            QString refText = onSelectUpToFace(msg);
            if (refText.length() > 0) {
                QSignalBlocker guard(ui->lineFaceName);
                ui->lineFaceName->setText(refText);
                QStringList list(refText.split(QLatin1Char(':')));
                ui->lineFaceName->setProperty("FeatureName", list[0].toUtf8());
                ui->lineFaceName->setProperty("FaceName", list.size()>1 ? list[1].toUtf8() : QByteArray());
                // Turn off reference selection mode
                onButtonFace(false);
            } else {
                clearFaceName();
            }
        }
    } else if (msg.Type == Gui::SelectionChanges::ClrSelection) {
        if (getSelectionMode() == SelectionMode::refAdd) {
            clearFaceName();
        }
    }
}

bool TaskExtrudeParameters::eventFilter(QObject *o, QEvent *ev)
{
    switch(ev->type()) {
    case QEvent::Leave:
        Gui::Selection().rmvPreselect();
        break;
    case QEvent::Enter:
        if (vp && o == Gui::FwQt::widgetOf(ui->lineFaceName)) {
            auto extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
            auto obj = extrude->UpToFace.getValue();
            if (obj) {
                const auto &subs = extrude->UpToFace.getSubValues(true);
                PartDesignGui::highlightObjectOnTop(
                        App::SubObjectT(obj, subs.size()?subs[0].c_str():""));
            }
        }
        break;
    default:
        break;
    }
    return false;
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

void TaskExtrudeParameters::clearFaceName()
{
    QSignalBlocker block(ui->lineFaceName);
    ui->lineFaceName->clear();
    ui->lineFaceName->setProperty("FeatureName", QVariant());
    ui->lineFaceName->setProperty("FaceName", QVariant());
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

    ui->checkBoxAlongDirection->setEnabled(ui->directionCB->currentIndex() != 0);
    ui->XDirectionEdit->setEnabled(hasCustom);
    ui->YDirectionEdit->setEnabled(hasCustom);
    ui->ZDirectionEdit->setEnabled(hasCustom);

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

    auto mode = static_cast<Modes>(getMode());

    // disable/hide everything unless we are sure we don't need it
    // exception: the direction parameters are in any case visible
    bool isLengthEditVisible = false;
    bool isLengthEdit2Visible = false;
    bool isOffsetEditVisible = false;
    bool isOffsetEditEnabled = true;
    bool isMidplaneEnabled = false;
    bool isMidplaneVisible = false;
    bool isReversedEnabled = false;
    bool isFaceEditVisible = false;
    bool isTaperEditVisible = false;
    bool isTaperEdit2Visible = false;

    if (mode == Modes::Dimension) {
        isLengthEditVisible = true;
        ui->lengthEdit->selectNumber();
        QMetaObject::invokeMethod(ui->lengthEdit, "setFocus", Qt::QueuedConnection);
        isTaperEditVisible = true;
        isMidplaneVisible = true;
        isMidplaneEnabled = true;
        // Reverse only makes sense if Midplane is not true
        isReversedEnabled = !ui->checkBoxMidplane->isChecked();
    }
    else if (mode == Modes::ThroughAll && isPocket()) {
        isOffsetEditVisible = true;
        isOffsetEditEnabled = false; // offset may have some meaning for through all but it doesn't work
        isMidplaneEnabled = true;
        isMidplaneVisible = true;
        isReversedEnabled = !ui->checkBoxMidplane->isChecked();
    }
    else if (mode == Modes::ToLast && !isPocket()) {
        isOffsetEditVisible = true;
        isReversedEnabled = true;
    }
    else if (mode == Modes::ToFirst) {
        isOffsetEditVisible = true;
        isReversedEnabled = true;
    }
    else if (mode == Modes::ToFace) {
        isOffsetEditVisible = true;
        isReversedEnabled = true;
        isFaceEditVisible = true;
        QMetaObject::invokeMethod(ui->lineFaceName, "setFocus", Qt::QueuedConnection);
        // Go into reference selection mode if no face has been selected yet
        if (ui->lineFaceName->property("FeatureName").isNull())
            ui->buttonFace->setChecked(true);
    }
    else if (mode == Modes::TwoDimensions) {
        isLengthEditVisible = true;
        isLengthEdit2Visible = true;
        isTaperEditVisible = true;
        isTaperEdit2Visible = true;
        isReversedEnabled = true;
    }

    ui->lengthEdit->setVisible(isLengthEditVisible);
    ui->lengthEdit->setEnabled(isLengthEditVisible);
    ui->labelLength->setVisible(isLengthEditVisible);
    ui->checkBoxAlongDirection->setVisible(isLengthEditVisible);

    ui->lengthEdit2->setVisible(isLengthEdit2Visible);
    ui->lengthEdit2->setEnabled(isLengthEdit2Visible);
    ui->labelLength2->setVisible(isLengthEdit2Visible);

    ui->offsetEdit->setVisible(isOffsetEditVisible);
    ui->offsetEdit->setEnabled(isOffsetEditVisible && isOffsetEditEnabled);
    ui->labelOffset->setVisible(isOffsetEditVisible);

    ui->taperAngleEdit->setVisible(isTaperEditVisible);
    ui->taperAngleEdit->setEnabled(isTaperEditVisible);
    ui->labelTaperAngle->setVisible(isTaperEditVisible);

    ui->taperAngleEdit2->setVisible(isTaperEdit2Visible);
    ui->taperAngleEdit2->setEnabled(isTaperEdit2Visible);
    ui->labelTaperAngle2->setVisible(isTaperEdit2Visible);

    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());

    if (extrude->AutoTaperInnerAngle.getValue()) {
        ui->innerTaperEdit->setEnabled( false );
        ui->innerTaperEdit2->setEnabled( false );
    } else {
        ui->innerTaperEdit->setEnabled( isTaperEditVisible );
        ui->innerTaperEdit2->setEnabled( isTaperEdit2Visible );
    }

    ui->innerTaperEdit->setVisible( isTaperEditVisible );
    ui->innerTaperEdit2->setVisible( isTaperEdit2Visible );
    ui->labelInnerTaperAngle->setVisible( isTaperEditVisible );
    ui->labelInnerTaperAngle2->setVisible( isTaperEdit2Visible );

    ui->checkBoxMidplane->setEnabled(isMidplaneEnabled);
    ui->checkBoxMidplane->setVisible(isMidplaneVisible);

    ui->checkBoxReversed->setEnabled(isReversedEnabled);

    ui->buttonFace->setVisible(isFaceEditVisible);
    ui->lineFaceName->setVisible(isFaceEditVisible);
    if (!isFaceEditVisible) {
        onButtonFace(false);
    }
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

void TaskExtrudeParameters::onDirectionToggled(bool on)
{
    if (on)
        ui->groupBoxDirection->show();
    else
        ui->groupBoxDirection->hide();
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
    // disable AlongSketchNormal when the direction is already normal
    if (index == DirectionModes::Normal)
        ui->checkBoxAlongDirection->setEnabled(false);
    else
        ui->checkBoxAlongDirection->setEnabled(true);

    setupTransaction();
    // if custom direction is used, show it
    if (index == DirectionModes::Custom) {
        ui->groupBoxDirection->setChecked(true);
        extrude->UseCustomVector.setValue(true);
    }
    else {
        extrude->UseCustomVector.setValue(false);
    }

    // if we don't use custom direction, only allow to show its direction
    if (index != DirectionModes::Custom) {
        ui->XDirectionEdit->setEnabled(false);
        ui->YDirectionEdit->setEnabled(false);
        ui->ZDirectionEdit->setEnabled(false);
    }
    else {
        ui->XDirectionEdit->setEnabled(true);
        ui->YDirectionEdit->setEnabled(true);
        ui->ZDirectionEdit->setEnabled(true);
    }

}

void TaskExtrudeParameters::onMidplaneChanged(bool on)
{
    setupTransaction();
    PartDesign::FeatureExtrude* extrude = static_cast<PartDesign::FeatureExtrude*>(vp->getObject());
    extrude->Midplane.setValue(on);
    ui->checkBoxReversed->setEnabled(!on);
    recomputeFeature();
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
    ui->checkBoxMidplane->setEnabled(!on);
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

void TaskExtrudeParameters::onFaceName(const QString& text)
{
    if (text.isEmpty()) {
        // if user cleared the text field then also clear the properties
        ui->lineFaceName->setProperty("FeatureName", QVariant());
        ui->lineFaceName->setProperty("FaceName", QVariant());
    }
    else {
        // expect that the label of an object is used
        QStringList parts = text.split(QChar::fromLatin1(':'));
        QString label = parts[0];
        QVariant name = objectNameByLabel(label, ui->lineFaceName->property("FeatureName"));
        if (name.isValid()) {
            parts[0] = name.toString();
            QString uptoface = parts.join(QStringLiteral(":"));
            ui->lineFaceName->setProperty("FeatureName", name);
            ui->lineFaceName->setProperty("FaceName", setUpToFace(uptoface));
        }
        else {
            ui->lineFaceName->setProperty("FeatureName", QVariant());
            ui->lineFaceName->setProperty("FaceName", QVariant());
        }
    }
}

void TaskExtrudeParameters::translateFaceNamePlaceHolder()
{
    ui->lineFaceName->setPlaceholderText(tr("No face selected"));
}

void TaskExtrudeParameters::translateFaceName()
{
    QVariant featureName = ui->lineFaceName->property("FeatureName");
    if (featureName.isValid()) {
        QStringList parts = ui->lineFaceName->text().split(QChar::fromLatin1(':'));
        QByteArray upToFace = ui->lineFaceName->property("FaceName").toByteArray();
        int faceId = -1;
        bool ok = false;
        if (upToFace.indexOf("Face") == 0) {
            faceId = upToFace.remove(0,4).toInt(&ok);
        }

        if (ok) {
            ui->lineFaceName->setText(QStringLiteral("%1:%2%3")
                                      .arg(parts[0], tr("Face")).arg(faceId));
        }
        else {
            ui->lineFaceName->setText(parts[0]);
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

bool TaskExtrudeParameters::getMidplane() const
{
    return ui->checkBoxMidplane->isChecked();
}

int TaskExtrudeParameters::getMode() const
{
    return ui->changeMode->currentIndex();
}

QString TaskExtrudeParameters::getFaceName() const
{
    QVariant featureName = ui->lineFaceName->property("FeatureName");
    if (featureName.isValid()) {
        QString faceName = ui->lineFaceName->property("FaceName").toString();
        return getFaceReference(featureName.toString(), faceName);
    }

    return QStringLiteral("None");
}

void TaskExtrudeParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        QSignalBlocker length(ui->lengthEdit);
        QSignalBlocker length2(ui->lengthEdit2);
        QSignalBlocker offset(ui->offsetEdit);
        QSignalBlocker taper(ui->taperAngleEdit);
        QSignalBlocker taper2(ui->taperAngleEdit2);
        QSignalBlocker innerTaper(ui->innerTaperEdit);
        QSignalBlocker innerTaper2(ui->innerTaperEdit2);
        QSignalBlocker xdir(ui->XDirectionEdit);
        QSignalBlocker ydir(ui->YDirectionEdit);
        QSignalBlocker zdir(ui->ZDirectionEdit);
        QSignalBlocker dir(ui->directionCB);
        QSignalBlocker face(ui->lineFaceName);
        QSignalBlocker mode(ui->changeMode);

        addBlinkWidget(Gui::FwQt::widgetOf(ui->lineFaceName));

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
        translateModeList(ui->changeMode->currentIndex());

        translateFaceName();
        translateFaceNamePlaceHolder();
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
    ui->taperAngleEdit->pushToHistory();
    ui->taperAngleEdit2->pushToHistory();
    ui->innerTaperEdit->pushToHistory();
    ui->innerTaperEdit2->pushToHistory();

    TaskSketchBasedParameters::saveHistory();
}

void TaskExtrudeParameters::applyParameters(QString facename)
{
    if (!vp)
        return;
    auto obj = vp->getObject();

    ui->lengthEdit->apply();
    ui->lengthEdit2->apply();
    ui->taperAngleEdit->apply();
    ui->taperAngleEdit2->apply();
    ui->innerTaperEdit->apply();
    ui->innerTaperEdit2->apply();
    FCMD_OBJ_CMD(obj, "UseCustomVector = " << (getCustom() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "Direction = ("
        << getXDirection() << ", " << getYDirection() << ", " << getZDirection() << ")");
    FCMD_OBJ_CMD(obj, "ReferenceAxis = " << getReferenceAxis());
    FCMD_OBJ_CMD(obj, "AlongSketchNormal = " << (getAlongSketchNormal() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "Type = " << getMode());
    FCMD_OBJ_CMD(obj, "UpToFace = " << facename.toUtf8().data());
    FCMD_OBJ_CMD(obj, "Reversed = " << (getReversed() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "Midplane = " << (getMidplane() ? 1 : 0));
    FCMD_OBJ_CMD(obj, "Offset = " << getOffset());
}

void TaskExtrudeParameters::onModeChanged(int)
{
    // implement in sub-class
}

void TaskExtrudeParameters::translateModeList(int)
{
    // implement in sub-class
}

void TaskExtrudeParameters::translateTooltips()
{
    ui->offsetEdit->setToolTip(tr("Offset from face at which pad will end"));
    ui->checkBoxReversed->setToolTip(tr("Reverses pad direction"));
}


#include "moc_TaskExtrudeParameters.cpp"
