/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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

#include <Base/Console.h>
#include <Base/Tools.h>
#include <App/Document.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/Selection.h>
#include <Gui/ViewProvider.h>
#include <Base/Converter.h>
#include <Mod/Part/App/Tools.h>
#include <Mod/PartDesign/App/FeatureHole.h>

#include "ui_TaskHoleParameters.h"
#include "TaskHoleParameters.h"

using namespace PartDesignGui;
using namespace Gui;
namespace sp = std::placeholders;

/* TRANSLATOR PartDesignGui::TaskHoleParameters */

// See Hole::HoleCutType_ISOmetric_Enums
// and Hole::HoleCutType_ISOmetricfine_Enums
#if 0 // needed for Qt's lupdate utility
    qApp->translate("PartDesignGui::TaskHoleParameters", "Counterbore");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Countersink");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Counterdrill");
    // Hole::ClearanceMetricEnums, ClearanceUTSEnums and ClearanceOtherEnums
    qApp->translate("PartDesignGui::TaskHoleParameters", "Medium");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Fine");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Coarse");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Normal");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Close");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Loose");
    qApp->translate("PartDesignGui::TaskHoleParameters", "Wide");
#endif

// The panel's layout is upstream's redesign (114166a0e3, be3ce13a7c and the
// January 2025 series, 69f3dae845): a cut diagram in the middle, and one
// Hole type combo for Threaded, ModelThread and CosmeticThread. Upstream's
// Start controls and Operation selector are not here (the fork's Hole has
// no StartType, and initUI() adds the fork's own operation combo).

namespace
{
// The sizes of a thread type as the panel lists them. An ISO coarse size is
// stored as "M6", the name the head cut tables use; the list says "M6x1.0",
// as the fine sizes say "M6x0.75" (upstream 599f100c4f renamed the sizes
// themselves, which breaks files and scripts that name them).
void fillThreadSizes(QComboBox* combo, const PartDesign::Hole* hole)
{
    combo->clear();
    const bool coarse = std::string(hole->ThreadType.getValueAsString()) == "ISOMetricProfile";
    const int type = hole->ThreadType.getValue();
    int index = 0;
    for (const auto& name : hole->ThreadSize.getEnumVector()) {
        QString text = QString::fromStdString(name);
        double pitch = coarse ? PartDesign::Hole::threadDescription[type][index].pitch : 0.0;
        if (pitch > 0.0) {
            QString digits = QString::number(pitch, 'f', 2);
            if (digits.endsWith(QLatin1String("0")))
                digits.chop(1);
            text += QLatin1String("x") + digits;
        }
        combo->addItem(text);
        ++index;
    }
    combo->setCurrentIndex(hole->ThreadSize.getValue());
}
}  // namespace

TaskHoleParameters::TaskHoleParameters(ViewProviderHole* HoleView, QWidget* parent)
    : TaskSketchBasedParameters(HoleView, parent, "PartDesign_Hole", tr("Hole parameters"))
    , observer(new Observer(this, static_cast<PartDesign::Hole*>(vp->getObject())))
    , ui(new Ui_TaskHoleParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    QMetaObject::connectSlotsByName(this);

    // The data is the family whose clearance names apply (see
    // Hole::ClearanceMetricEnums); the fit combo is filled from the enums
    ui->ThreadType->addItem(tr("None"), QByteArray("None"));
    ui->ThreadType->addItem(tr("ISO metric regular"), QByteArray("ISO"));
    ui->ThreadType->addItem(tr("ISO metric fine"), QByteArray("ISO"));
    ui->ThreadType->addItem(tr("UTS coarse"), QByteArray("UTS"));
    ui->ThreadType->addItem(tr("UTS fine"), QByteArray("UTS"));
    ui->ThreadType->addItem(tr("UTS extra fine"), QByteArray("UTS"));
    ui->ThreadType->addItem(tr("ANSI pipes"), QByteArray("UTS"));
    ui->ThreadType->addItem(tr("ISO/BSP pipes"), QByteArray("ISO"));
    ui->ThreadType->addItem(tr("BSW whitworth"), QByteArray("Other"));
    ui->ThreadType->addItem(tr("BSF whitworth fine"), QByteArray("Other"));
    ui->ThreadType->addItem(tr("ISO tyre valves"), QByteArray("Other"));

    refresh();

    connect(ui->HoleType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::holeTypeChanged);
    connect(ui->ThreadType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::threadTypeChanged);
    connect(ui->ThreadSize, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::threadSizeChanged);
    connect(ui->ThreadClass, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::threadClassChanged);
    connect(ui->ThreadFit, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::threadFitChanged);
    connect(ui->Diameter, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::threadDiameterChanged);
    connect(ui->directionRightHand, &QRadioButton::clicked,
            this, &TaskHoleParameters::threadDirectionChanged);
    connect(ui->directionLeftHand, &QRadioButton::clicked,
            this, &TaskHoleParameters::threadDirectionChanged);
    connect(ui->HoleCutType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::holeCutTypeChanged);
    connect(ui->HoleCutCustomValues, &QCheckBox::clicked,
            this, &TaskHoleParameters::holeCutCustomValuesChanged);
    connect(ui->HoleCutDiameter, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::holeCutDiameterChanged);
    connect(ui->HoleCutDepth, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::holeCutDepthChanged);
    connect(ui->HoleCutCountersinkAngle, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::holeCutCountersinkAngleChanged);
    connect(ui->DepthType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::depthChanged);
    connect(ui->Depth, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::depthValueChanged);
    connect(ui->DrillPointAngled, &QCheckBox::clicked,
            this, &TaskHoleParameters::drillPointChanged);
    connect(ui->DrillPointAngle, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::drillPointAngledValueChanged);
    connect(ui->DrillForDepth, &QCheckBox::clicked,
            this, &TaskHoleParameters::drillForDepthChanged);
    connect(ui->Tapered, &QCheckBox::clicked,
            this, &TaskHoleParameters::taperedChanged);
    connect(ui->Reversed, &QCheckBox::clicked,
            this, &TaskHoleParameters::reversedChanged);
    connect(ui->TaperedAngle, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::taperedAngleChanged);
    connect(ui->ModelThread, &QCheckBox::clicked,
            this, &TaskHoleParameters::modelThreadChanged);
    connect(ui->UseCustomThreadClearance, &QCheckBox::toggled,
            this, &TaskHoleParameters::useCustomThreadClearanceChanged);
    connect(ui->CustomThreadClearance, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::customThreadClearanceChanged);
    connect(ui->ThreadDepthType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::threadDepthTypeChanged);
    connect(ui->ThreadDepth, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
            this, &TaskHoleParameters::threadDepthChanged);
    connect(ui->BaseProfileType, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &TaskHoleParameters::baseProfileTypeChanged);

    PartDesign::Hole* pcHole = getHole();

    ui->Diameter->bind(pcHole->Diameter);
    ui->HoleCutDiameter->bind(pcHole->HoleCutDiameter);
    ui->HoleCutDepth->bind(pcHole->HoleCutDepth);
    ui->HoleCutCountersinkAngle->bind(pcHole->HoleCutCountersinkAngle);
    ui->Depth->bind(pcHole->Depth);
    ui->DrillPointAngle->bind(pcHole->DrillPointAngle);
    ui->TaperedAngle->bind(pcHole->TaperedAngle);
    ui->ThreadDepth->bind(pcHole->ThreadDepth);
    ui->CustomThreadClearance->bind(pcHole->CustomThreadClearance);
    // The property allows a negative clearance; the form's default minimum
    // is 0
    ui->CustomThreadClearance->setMinimum(pcHole->CustomThreadClearance.getMinimum());
    ui->CustomThreadClearance->setMaximum(pcHole->CustomThreadClearance.getMaximum());

    connectPropChanged = App::GetApplication().signalChangePropertyEditor.connect(
            std::bind(&TaskHoleParameters::changedObject, this, sp::_1, sp::_2));

    this->initUI(proxy);
    this->groupLayout()->addWidget(proxy);

    // initUI() makes the Update view box, which only a modelled thread uses
    updateViewBlocking();

    setupGizmos(HoleView);
}

TaskHoleParameters::~TaskHoleParameters() = default;

PartDesign::Hole* TaskHoleParameters::getHole() const
{
    return vp ? dynamic_cast<PartDesign::Hole*>(vp->getObject()) : nullptr;
}

PartDesign::Hole* TaskHoleParameters::editHole()
{
    // Open the edit's transaction before the first change, or Cancel
    // cannot take that change back (the recompute opens it too late)
    setupTransaction();
    return getHole();
}

void TaskHoleParameters::setupGizmos(ViewProviderHole* vp)
{
    if (!GizmoContainer::isEnabled()) {
        return;
    }

    holeDepthGizmo = new LinearGizmo(ui->Depth);
    holeDepthGizmo->setClickCallback([this] {
        if (ui->Reversed->isEnabled()) {
            ui->Reversed->click();
        }
    });

    gizmoContainer = GizmoContainer::create({holeDepthGizmo}, vp);

    setGizmoPositions();
    showDraggerHints();
}

void TaskHoleParameters::setGizmoPositions()
{
    if (!gizmoContainer) {
        return;
    }

    auto hole = getHole();
    if (!hole || hole->isError()) {
        gizmoContainer->visible = false;
        return;
    }
    Part::TopoShape profileShape = hole->getProfileShape();
    // The direction Hole::execute() drills against, flipped by Reversed
    Base::Vector3d dir = hole->guessNormalDirection(profileShape);
    dir *= hole->Reversed.getValue() ? -1 : 1;
    // The first hole Hole::findHoles() makes, on whatever BaseProfileType
    // centres holes on
    std::vector<Base::Vector3d> holePositions;
    hole->forEachHoleCenter(profileShape, [&](const Part::TopoShape&, const gp_Pnt& loc) {
        holePositions.push_back(Base::convertTo<Base::Vector3d>(loc));
    });

    if (holePositions.empty()) {
        gizmoContainer->visible = false;
        return;
    }
    gizmoContainer->visible = true;

    holeDepthGizmo->Gizmo::setDraggerPlacement(
        holePositions[0] - ui->HoleCutDepth->value().getValue() * dir,
        -dir
    );
    holeDepthGizmo->setVisibility(std::string(hole->DepthType.getValueAsString()) == "Dimension");

    holeDepthGizmo->setDragLength(ui->Depth->rawValue());
}

void TaskHoleParameters::finishedRecomputeFeature()
{
    TaskSketchBasedParameters::finishedRecomputeFeature();
    setGizmoPositions();
}

const char *TaskHoleParameters::updateViewParameter() const
{
    return "User parameter:BaseApp/History/HoleUpdateView";
}

void TaskHoleParameters::refresh()
{
    auto pcHole = getHole();
    if (!pcHole)
        return;

    // Temporarily prevent unnecessary feature recomputes
    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(true);

    ui->BaseProfileType->setCurrentIndex(
        PartDesign::Hole::baseProfileOption_bitmaskToIdx(pcHole->BaseProfileType.getValue()));

    ui->ThreadType->setCurrentIndex(pcHole->ThreadType.getValue());
    updateHoleTypeCombo();

    auto fillCombo = [](QComboBox* combo, const std::vector<std::string>& items, int index) {
        combo->clear();
        for (const auto& it : items)
            combo->addItem(tr(it.c_str()));
        combo->setCurrentIndex(index);
    };
    // Sizes are designations, not words to translate
    fillThreadSizes(ui->ThreadSize, pcHole);
    fillCombo(ui->ThreadClass, pcHole->ThreadClass.getEnumVector(), pcHole->ThreadClass.getValue());
    // The clearance names differ between ISO and UTS
    fillCombo(ui->ThreadFit, pcHole->ThreadFit.getEnumVector(), pcHole->ThreadFit.getValue());
    fillCombo(ui->HoleCutType, pcHole->HoleCutType.getEnumVector(), pcHole->HoleCutType.getValue());

    ui->Diameter->setMinimum(pcHole->Diameter.getMinimum());
    ui->Diameter->setValue(pcHole->Diameter.getValue());
    if (pcHole->ThreadDirection.getValue() == 0L)
        ui->directionRightHand->setChecked(true);
    else
        ui->directionLeftHand->setChecked(true);

    ui->HoleCutCustomValues->setChecked(pcHole->HoleCutCustomValues.getValue());
    updateHoleCutLimits();
    ui->HoleCutDiameter->setValue(pcHole->HoleCutDiameter.getValue());
    ui->HoleCutDepth->setValue(pcHole->HoleCutDepth.getValue());
    ui->HoleCutCountersinkAngle->setMinimum(pcHole->HoleCutCountersinkAngle.getMinimum());
    ui->HoleCutCountersinkAngle->setMaximum(pcHole->HoleCutCountersinkAngle.getMaximum());
    ui->HoleCutCountersinkAngle->setValue(pcHole->HoleCutCountersinkAngle.getValue());

    ui->DepthType->setCurrentIndex(pcHole->DepthType.getValue());
    ui->Depth->setValue(pcHole->Depth.getValue());
    ui->DrillPointAngled->setChecked(pcHole->DrillPoint.getValue() != 0L);
    ui->DrillPointAngle->setMinimum(pcHole->DrillPointAngle.getMinimum());
    ui->DrillPointAngle->setMaximum(pcHole->DrillPointAngle.getMaximum());
    ui->DrillPointAngle->setValue(pcHole->DrillPointAngle.getValue());
    ui->DrillForDepth->setChecked(pcHole->DrillForDepth.getValue());

    ui->Tapered->setChecked(pcHole->Tapered.getValue());
    ui->TaperedAngle->setMinimum(pcHole->TaperedAngle.getMinimum());
    ui->TaperedAngle->setMaximum(pcHole->TaperedAngle.getMaximum());
    ui->TaperedAngle->setValue(pcHole->TaperedAngle.getValue());
    ui->Reversed->setChecked(pcHole->Reversed.getValue());

    ui->UseCustomThreadClearance->setChecked(pcHole->UseCustomThreadClearance.getValue());
    ui->CustomThreadClearance->setValue(pcHole->CustomThreadClearance.getValue());
    ui->ThreadDepthType->setCurrentIndex(pcHole->ThreadDepthType.getValue());
    ui->ThreadDepth->setValue(pcHole->ThreadDepth.getValue());

    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(false);

    updateVisibility();
}

void TaskHoleParameters::updateHoleTypeCombo()
{
    auto hole = getHole();
    if (!hole)
        return;

    QSignalBlocker blockType(ui->HoleType);
    QSignalBlocker blockModel(ui->ModelThread);
    // A modelled thread wins over a drawn one, as in Hole::execute()
    bool modeled = hole->ModelThread.getValue();
    if (!hole->Threaded.getValue())
        ui->HoleType->setCurrentIndex(Clearance);
    else if (modeled || hole->CosmeticThread.getValue())
        ui->HoleType->setCurrentIndex(Threaded);
    else
        ui->HoleType->setCurrentIndex(TapDrill);
    ui->ModelThread->setChecked(modeled);
}

void TaskHoleParameters::updateVisibility()
{
    auto hole = getHole();
    if (!hole)
        return;

    const bool isNone = std::string(hole->ThreadType.getValueAsString()) == "None";
    const bool threaded = !isNone && hole->Threaded.getValue();
    const bool modeled = threaded && hole->ModelThread.getValue();
    const bool cosmetic = threaded && !modeled && hole->CosmeticThread.getValue();
    const bool depthIsDimension = std::string(hole->DepthType.getValueAsString()) == "Dimension";
    const bool angled = hole->DrillPoint.getValue() != 0L;

    // Without a standard the hole is a plain one of a stated diameter
    ui->labelSize->setHidden(isNone);
    ui->ThreadSize->setHidden(isNone);
    ui->labelHoleType->setHidden(isNone);
    ui->HoleType->setHidden(isNone);
    ui->Diameter->setEnabled(isNone && !hole->Diameter.isReadOnly());
    // A clearance is for a screw passing through, not for a thread
    ui->labelThreadClearance->setHidden(isNone || threaded);
    ui->ThreadFit->setHidden(isNone || threaded);

    // The thread group is for a thread that is modelled or drawn; a tap
    // drill is only the core hole
    ui->ThreadGroupBox->setVisible(modeled || cosmetic);
    ui->CustomClearanceWidget->setVisible(modeled);
    ui->CustomThreadClearance->setEnabled(hole->UseCustomThreadClearance.getValue());
    ui->ThreadClass->setDisabled(modeled && hole->UseCustomThreadClearance.getValue());
    ui->ThreadDepthDimensionWidget->setVisible(
        std::string(hole->ThreadDepthType.getValueAsString()) == "Dimension");

    // The drill point is only at the bottom of a hole of a stated depth
    ui->Depth->setEnabled(depthIsDimension);
    ui->DrillFrame->setEnabled(depthIsDimension);
    ui->DrillPointAngle->setEnabled(angled);
    ui->DrillForDepth->setEnabled(angled);
    ui->TaperedAngle->setEnabled(hole->Tapered.getValue());

    // Custom head values are for the screw standards; the four plain cuts
    // are always custom (see Hole::HoleCutType_None_Enums)
    ui->HoleCutCustomValues->setHidden(hole->HoleCutType.getValue() < 4);
    ui->HoleCutCustomValues->setDisabled(hole->HoleCutCustomValues.isReadOnly());
    ui->HoleCutDiameter->setDisabled(hole->HoleCutDiameter.isReadOnly());
    ui->HoleCutDepth->setDisabled(hole->HoleCutDepth.isReadOnly());
    ui->HoleCutCountersinkAngle->setDisabled(hole->HoleCutCountersinkAngle.isReadOnly());

    setCutDiagram();
    updateViewBlocking();
}

void TaskHoleParameters::updateViewBlocking()
{
    if (!checkBoxUpdateView)
        return;
    auto hole = getHole();
    if (!hole)
        return;
    // Update view is only offered where a recompute is slow: a modelled
    // thread. This also ensures the feature is recomputed otherwise.
    bool modeled = hole->Threaded.getValue() && hole->ModelThread.getValue();
    checkBoxUpdateView->setEnabled(modeled);
    blockUpdate = modeled && !checkBoxUpdateView->isChecked();
}

void TaskHoleParameters::updateHoleCutLimits()
{
    auto hole = getHole();
    if (!hole)
        return;
    constexpr double minHoleCutDifference = 0.1;
    // HoleCutDiameter must not be smaller or equal than the Diameter
    ui->HoleCutDiameter->setMinimum(hole->Diameter.getValue() + minHoleCutDifference);
}

void TaskHoleParameters::setCutDiagram()
{
    auto hole = getHole();
    if (!hole)
        return;

    const std::string holeCutTypeString = hole->HoleCutType.getValueAsString();
    const std::string threadTypeString = hole->ThreadType.getValueAsString();
    const bool isAngled = std::string(hole->DepthType.getValueAsString()) == "Dimension"
        && hole->DrillPoint.getValue() != 0L;
    const bool isCountersink = holeCutTypeString == "Countersink"
        || hole->isDynamicCountersink(threadTypeString, holeCutTypeString);
    const bool isCounterbore = holeCutTypeString == "Counterbore"
        || hole->isDynamicCounterbore(threadTypeString, holeCutTypeString);
    const bool isCounterdrill = holeCutTypeString == "Counterdrill";
    const bool isNotCut = holeCutTypeString == "None";

    ui->labelHoleCutDiameter->setHidden(isNotCut);
    ui->HoleCutDiameter->setHidden(isNotCut);
    ui->labelHoleCutDepth->setHidden(isNotCut);
    ui->HoleCutDepth->setHidden(isNotCut);

    std::string baseFileName;
    bool hasAngle = false;
    if (isCounterbore) {
        baseFileName = "hole_counterbore";
    }
    else if (isCountersink) {
        baseFileName = "hole_countersink";
        hasAngle = true;
    }
    else if (isCounterdrill) {
        baseFileName = "hole_counterdrill";
        hasAngle = true;
    }
    else {
        baseFileName = "hole_none";
    }
    ui->labelHoleCutCountersinkAngle->setVisible(hasAngle);
    ui->HoleCutCountersinkAngle->setVisible(hasAngle);

    if (isAngled)
        baseFileName += hole->DrillForDepth.getValue() ? "_angled_included" : "_angled";
    else
        baseFileName += "_flat";

    ui->cutDiagram->setSvg(QString::fromStdString(":/images/" + baseFileName + ".svg"));
}

void TaskHoleParameters::holeTypeChanged(int index)
{
    if (index < 0)
        return;
    auto pcHole = editHole();
    if (!pcHole)
        return;

    // Threaded with the Model Thread box off is a thread drawn on the bore
    bool threaded = index != Clearance;
    bool modeled = index == Threaded && ui->ModelThread->isChecked();
    bool cosmetic = index == Threaded && !modeled;
    pcHole->Threaded.setValue(threaded);
    pcHole->ModelThread.setValue(modeled);
    pcHole->CosmeticThread.setValue(cosmetic);

    updateVisibility();
    recomputeFeature();
}

void TaskHoleParameters::modelThreadChanged()
{
    auto pcHole = editHole();
    if (!pcHole)
        return;

    // The App side keeps the two exclusive; set both to say which
    bool modeled = ui->ModelThread->isChecked();
    pcHole->ModelThread.setValue(modeled);
    pcHole->CosmeticThread.setValue(!modeled);

    updateVisibility();
    recomputeFeature();
}

void TaskHoleParameters::baseProfileTypeChanged(int index)
{
    auto pcHole = editHole();
    int bits = PartDesign::Hole::baseProfileOption_idxToBitmask(index);
    if (pcHole && bits > 0) {
        pcHole->BaseProfileType.setValue(bits);
        recomputeFeature();
    }
}

void TaskHoleParameters::threadDepthTypeChanged(int index)
{
    auto pcHole = editHole();
    if (!pcHole)
        return;

    pcHole->ThreadDepthType.setValue(index);
    ui->ThreadDepth->setValue(pcHole->ThreadDepth.getValue());
    updateVisibility();
    recomputeFeature();
}

void TaskHoleParameters::threadDepthChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->ThreadDepth.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::useCustomThreadClearanceChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->UseCustomThreadClearance.setValue(ui->UseCustomThreadClearance->isChecked());
        updateVisibility();
        recomputeFeature();
    }
}

void TaskHoleParameters::customThreadClearanceChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->CustomThreadClearance.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::threadPitchChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->ThreadPitch.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::holeCutTypeChanged(int index)
{
    if (index < 0)
        return;

    auto pcHole = editHole();
    if (!pcHole)
        return;

    // the HoleCutDepth is something different for countersinks and counterbores
    // therefore reset it, it will be reset to sensible values by setting the new HoleCutType
    pcHole->HoleCutDepth.setValue(0.0);
    pcHole->HoleCutType.setValue(index);

    // when holeCutType was changed, reset HoleCutCustomValues to false because it should
    // be a purpose decision to overwrite the normed values
    // we will handle the case that there is no normed value later in this routine
    ui->HoleCutCustomValues->setChecked(false);
    pcHole->HoleCutCustomValues.setValue(false);

    // recompute to get the info about the HoleCutType properties
    recomputeFeature(false);

    // apply the result to the widgets: Hole::updateHoleCutParams() may
    // have forced custom values where a size has no normed ones
    ui->HoleCutCustomValues->setChecked(pcHole->HoleCutCustomValues.getValue());
    updateVisibility();
}

void TaskHoleParameters::holeCutCustomValuesChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->HoleCutCustomValues.setValue(ui->HoleCutCustomValues->isChecked());
        updateVisibility();
        recomputeFeature();
    }
}

void TaskHoleParameters::holeCutDiameterChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->HoleCutDiameter.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::holeCutDepthChanged(double value)
{
    auto pcHole = editHole();
    if (!pcHole)
        return;
    std::string HoleCutTypeString = pcHole->HoleCutType.getValueAsString();

    // The angle is writable for a countersink and a counterdrill only (see
    // Hole::updateHoleCutParams()); the widget may be hidden or collapsed
    if (!pcHole->HoleCutCountersinkAngle.isReadOnly() && HoleCutTypeString != "Counterdrill") {
        // we have a countersink and recalculate the HoleCutDiameter

        // store current depth
        double DepthDifference = value - pcHole->HoleCutDepth.getValue();
        // new diameter is the old one + 2 * tan(angle / 2) * DepthDifference
        double newDiameter = pcHole->HoleCutDiameter.getValue()
            + 2 * tan(Base::toRadians(pcHole->HoleCutCountersinkAngle.getValue() / 2)) * DepthDifference;
        // only apply if the result is not smaller than the hole diameter
        if (newDiameter > pcHole->Diameter.getValue()) {
            pcHole->HoleCutDiameter.setValue(newDiameter);
            pcHole->HoleCutDepth.setValue(value);
        }
    }
    else {
        pcHole->HoleCutDepth.setValue(value);
    }

    recomputeFeature();
}

void TaskHoleParameters::holeCutCountersinkAngleChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->HoleCutCountersinkAngle.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::depthChanged(int index)
{
    auto pcHole = editHole();
    if (!pcHole)
        return;

    pcHole->DepthType.setValue(index);
    updateVisibility();
    recomputeFeature();
}

void TaskHoleParameters::depthValueChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->Depth.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::drillPointChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->DrillPoint.setValue(ui->DrillPointAngled->isChecked() ? 1L : 0L);
        updateVisibility();
        recomputeFeature();
    }
}

void TaskHoleParameters::drillPointAngledValueChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->DrillPointAngle.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::drillForDepthChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->DrillForDepth.setValue(ui->DrillForDepth->isChecked());
        setCutDiagram();
        recomputeFeature();
    }
}

void TaskHoleParameters::taperedChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->Tapered.setValue(ui->Tapered->isChecked());
        updateVisibility();
        recomputeFeature();
    }
}

void TaskHoleParameters::reversedChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->Reversed.setValue(ui->Reversed->isChecked());
        recomputeFeature();
    }
}

void TaskHoleParameters::taperedAngleChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->TaperedAngle.setValue(value);
        recomputeFeature();
    }
}

void TaskHoleParameters::threadTypeChanged(int index)
{
    if (index < 0)
        return;

    auto pcHole = editHole();
    if (!pcHole)
        return;

    // A typical case is that users change from an ISO profile to another one.
    // When they had e.g. the size "M3" in one profile they expect
    // the same size in the other profile if it exists there.
    // Besides the size also the thread class" and hole cut type are affected.

    // store the current class
    QString ThreadClassString = ui->ThreadClass->currentText();
    // store the current type
    QString CutTypeString = ui->HoleCutType->currentText();

    // now set the new type; changedObject() refills the combos, and
    // Hole::onChanged() picks the size nearest the old one
    pcHole->ThreadType.setValue(index);

    // Class and cut type
    // the class and cut types are the same for both TypeClass so we don't need to distinguish between ISO and UTS
    int threadClassIndex = ui->ThreadClass->findText(ThreadClassString, Qt::MatchContains);
    if (threadClassIndex > -1)
        ui->ThreadClass->setCurrentIndex(threadClassIndex);
    int holeCutIndex = ui->HoleCutType->findText(CutTypeString, Qt::MatchContains);
    if (holeCutIndex > -1)
        ui->HoleCutType->setCurrentIndex(holeCutIndex);

    // we must set the read-only state according to the new HoleCutType
    holeCutTypeChanged(ui->HoleCutType->currentIndex());

    recomputeFeature();
}

void TaskHoleParameters::threadSizeChanged(int index)
{
    if (index < 0)
        return;

    auto pcHole = editHole();
    if (!pcHole)
        return;

    pcHole->ThreadSize.setValue(index);
    recomputeFeature();

    // apply the recompute result to the widgets
    ui->HoleCutCustomValues->setChecked(pcHole->HoleCutCustomValues.getValue());
    updateVisibility();
}

void TaskHoleParameters::threadClassChanged(int index)
{
    if (index < 0)
        return;

    if (auto pcHole = editHole()) {
        pcHole->ThreadClass.setValue(index);
        recomputeFeature();
    }
}

void TaskHoleParameters::threadDiameterChanged(double value)
{
    if (auto pcHole = editHole()) {
        pcHole->Diameter.setValue(value);
        updateHoleCutLimits();
        recomputeFeature();
    }
}

void TaskHoleParameters::threadFitChanged(int index)
{
    if (auto pcHole = editHole()) {
        pcHole->ThreadFit.setValue(index);
        recomputeFeature();
    }
}

void TaskHoleParameters::threadDirectionChanged()
{
    if (auto pcHole = editHole()) {
        pcHole->ThreadDirection.setValue(sender() == ui->directionRightHand ? 0L : 1L);
        recomputeFeature();
    }
}

void TaskHoleParameters::changeEvent(QEvent* e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
    }
}

void TaskHoleParameters::changedObject(const App::Document&, const App::Property& Prop)
{
    // happens when aborting the command
    auto pcHole = getHole();
    if (!pcHole)
        return;

    bool ro = Prop.isReadOnly();

    Base::Console().Log("Parameter %s was updated\n", Prop.getName());

    auto updateCheckable = [&](QAbstractButton* widget, bool value) {
        QSignalBlocker blocker(widget);
        widget->setChecked(value);
        widget->setDisabled(ro);
    };
    auto updateComboBox = [&](QComboBox* widget, int value) {
        QSignalBlocker blocker(widget);
        widget->setCurrentIndex(value);
        widget->setDisabled(ro);
    };
    auto updateSpinBox = [&](Gui::PrefQuantitySpinBox* widget, double value) {
        if (widget->value().getValue() != value) {
            QSignalBlocker blocker(widget);
            widget->setValue(value);
        }
        widget->setDisabled(ro);
    };
    auto updateComboBoxItems = [&](QComboBox* widget, const std::vector<std::string>& values,
                                   int selected) {
        QSignalBlocker blocker(widget);
        widget->clear();
        for (const auto& it : values)
            widget->addItem(tr(it.c_str()));
        widget->setCurrentIndex(selected);
    };

    if (&Prop == &pcHole->Threaded || &Prop == &pcHole->ModelThread
        || &Prop == &pcHole->CosmeticThread) {
        updateHoleTypeCombo();
        ui->HoleType->setDisabled(pcHole->Threaded.isReadOnly());
        updateVisibility();
    }
    else if (&Prop == &pcHole->ThreadType) {
        updateComboBox(ui->ThreadType, pcHole->ThreadType.getValue());

        // Thread type also updates the sizes, cut types, classes and fits
        {
            QSignalBlocker blocker(ui->ThreadSize);
            fillThreadSizes(ui->ThreadSize, pcHole);
        }
        updateComboBoxItems(ui->HoleCutType, pcHole->HoleCutType.getEnumVector(),
                            pcHole->HoleCutType.getValue());
        updateComboBoxItems(ui->ThreadClass, pcHole->ThreadClass.getEnumVector(),
                            pcHole->ThreadClass.getValue());
        updateComboBoxItems(ui->ThreadFit, pcHole->ThreadFit.getEnumVector(),
                            pcHole->ThreadFit.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->ThreadSize) {
        updateComboBox(ui->ThreadSize, pcHole->ThreadSize.getValue());
    }
    else if (&Prop == &pcHole->ThreadClass) {
        updateComboBox(ui->ThreadClass, pcHole->ThreadClass.getValue());
    }
    else if (&Prop == &pcHole->ThreadFit) {
        updateComboBox(ui->ThreadFit, pcHole->ThreadFit.getValue());
    }
    else if (&Prop == &pcHole->Diameter) {
        {
            QSignalBlocker blocker(ui->Diameter);
            ui->Diameter->setMinimum(pcHole->Diameter.getMinimum());
            ui->Diameter->setValue(pcHole->Diameter.getValue());
        }
        updateHoleCutLimits();
        updateVisibility();
    }
    else if (&Prop == &pcHole->ThreadDirection) {
        std::string direction(pcHole->ThreadDirection.getValueAsString());
        updateCheckable(ui->directionRightHand, direction == "Right");
        updateCheckable(ui->directionLeftHand, direction == "Left");
    }
    else if (&Prop == &pcHole->HoleCutType) {
        updateComboBox(ui->HoleCutType, pcHole->HoleCutType.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->HoleCutCustomValues) {
        updateCheckable(ui->HoleCutCustomValues, pcHole->HoleCutCustomValues.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->HoleCutDiameter) {
        updateSpinBox(ui->HoleCutDiameter, pcHole->HoleCutDiameter.getValue());
    }
    else if (&Prop == &pcHole->HoleCutDepth) {
        updateSpinBox(ui->HoleCutDepth, pcHole->HoleCutDepth.getValue());
    }
    else if (&Prop == &pcHole->HoleCutCountersinkAngle) {
        updateSpinBox(ui->HoleCutCountersinkAngle, pcHole->HoleCutCountersinkAngle.getValue());
    }
    else if (&Prop == &pcHole->DepthType) {
        updateComboBox(ui->DepthType, pcHole->DepthType.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->Depth) {
        updateSpinBox(ui->Depth, pcHole->Depth.getValue());
    }
    else if (&Prop == &pcHole->DrillPoint) {
        updateCheckable(ui->DrillPointAngled, pcHole->DrillPoint.getValue() != 0L);
        updateVisibility();
    }
    else if (&Prop == &pcHole->DrillPointAngle) {
        updateSpinBox(ui->DrillPointAngle, pcHole->DrillPointAngle.getValue());
    }
    else if (&Prop == &pcHole->DrillForDepth) {
        updateCheckable(ui->DrillForDepth, pcHole->DrillForDepth.getValue());
        setCutDiagram();
    }
    else if (&Prop == &pcHole->Tapered) {
        updateCheckable(ui->Tapered, pcHole->Tapered.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->TaperedAngle) {
        updateSpinBox(ui->TaperedAngle, pcHole->TaperedAngle.getValue());
    }
    else if (&Prop == &pcHole->Reversed) {
        updateCheckable(ui->Reversed, pcHole->Reversed.getValue());
    }
    else if (&Prop == &pcHole->UseCustomThreadClearance) {
        updateCheckable(ui->UseCustomThreadClearance, pcHole->UseCustomThreadClearance.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->CustomThreadClearance) {
        updateSpinBox(ui->CustomThreadClearance, pcHole->CustomThreadClearance.getValue());
    }
    else if (&Prop == &pcHole->ThreadDepthType) {
        updateComboBox(ui->ThreadDepthType, pcHole->ThreadDepthType.getValue());
        updateVisibility();
    }
    else if (&Prop == &pcHole->ThreadDepth) {
        updateSpinBox(ui->ThreadDepth, pcHole->ThreadDepth.getValue());
    }
    else if (&Prop == &pcHole->BaseProfileType) {
        // -1, an unlisted combination set from Python, shows no choice
        updateComboBox(ui->BaseProfileType,
            PartDesign::Hole::baseProfileOption_bitmaskToIdx(pcHole->BaseProfileType.getValue()));
    }
}

bool TaskHoleParameters::getThreaded() const
{
    return ui->HoleType->currentIndex() != Clearance;
}

long TaskHoleParameters::getThreadType() const
{
    return ui->ThreadType->currentIndex();
}

long TaskHoleParameters::getThreadSize() const
{
    if (ui->ThreadSize->currentIndex() == -1)
        return 0;
    else
        return ui->ThreadSize->currentIndex();
}

long TaskHoleParameters::getThreadClass() const
{
    if (ui->ThreadSize->currentIndex() == -1)
        return 0;
    else
        return ui->ThreadClass->currentIndex();
}

long TaskHoleParameters::getThreadFit() const
{
    // the fit (clearance) is independent if the hole is threaded or not
    // since an unthreaded hole for a screw can also have a close fit
    return ui->ThreadFit->currentIndex();
}

Base::Quantity TaskHoleParameters::getDiameter() const
{
    return ui->Diameter->value();
}

long TaskHoleParameters::getThreadDirection() const
{
    if (ui->directionRightHand->isChecked())
        return 0;
    else
        return 1;
}

long TaskHoleParameters::getHoleCutType() const
{
    if (ui->HoleCutType->currentIndex() == -1)
        return 0;
    else
        return ui->HoleCutType->currentIndex();
}

bool TaskHoleParameters::getHoleCutCustomValues() const
{
    return ui->HoleCutCustomValues->isChecked();
}

Base::Quantity TaskHoleParameters::getHoleCutDiameter() const
{
    return ui->HoleCutDiameter->value();
}

Base::Quantity TaskHoleParameters::getHoleCutDepth() const
{
    return ui->HoleCutDepth->value();
}

Base::Quantity TaskHoleParameters::getHoleCutCountersinkAngle() const
{
    return ui->HoleCutCountersinkAngle->value();
}

long TaskHoleParameters::getDepthType() const
{
    return ui->DepthType->currentIndex();
}

Base::Quantity TaskHoleParameters::getDepth() const
{
    return ui->Depth->value();
}

long TaskHoleParameters::getDrillPoint() const
{
    return ui->DrillPointAngled->isChecked() ? 1 : 0;
}

Base::Quantity TaskHoleParameters::getDrillPointAngle() const
{
    return ui->DrillPointAngle->value();
}

bool TaskHoleParameters::getTapered() const
{
    return ui->Tapered->isChecked();
}

bool TaskHoleParameters::getDrillForDepth() const
{
    return ui->DrillForDepth->isChecked();
}

Base::Quantity TaskHoleParameters::getTaperedAngle() const
{
    return ui->TaperedAngle->value();
}

bool TaskHoleParameters::getUseCustomThreadClearance() const
{
    return ui->UseCustomThreadClearance->isChecked();
}

double  TaskHoleParameters::getCustomThreadClearance() const
{
    return ui->CustomThreadClearance->value().getValue();
}

bool TaskHoleParameters::getModelThread() const
{
    return ui->HoleType->currentIndex() == Threaded && ui->ModelThread->isChecked();
}

bool TaskHoleParameters::getCosmeticThread() const
{
    return ui->HoleType->currentIndex() == Threaded && !ui->ModelThread->isChecked();
}

int TaskHoleParameters::getBaseProfileType() const
{
    return PartDesign::Hole::baseProfileOption_idxToBitmask(ui->BaseProfileType->currentIndex());
}

long TaskHoleParameters::getThreadDepthType() const
{
    return ui->ThreadDepthType->currentIndex();
}

double TaskHoleParameters::getThreadDepth() const
{
    return ui->ThreadDepth->value().getValue();
}

void TaskHoleParameters::apply()
{
    auto obj = vp->getObject();
    PartDesign::Hole* pcHole = getHole();

    ui->Diameter->apply();
    ui->HoleCutDiameter->apply();
    ui->HoleCutDepth->apply();
    ui->HoleCutCountersinkAngle->apply();
    ui->Depth->apply();
    ui->DrillPointAngle->apply();
    ui->TaperedAngle->apply();

    if (!pcHole->Threaded.isReadOnly())
        FCMD_OBJ_CMD(obj, "Threaded = " << (getThreaded() ? 1 : 0));
    if (!pcHole->ModelThread.isReadOnly())
        FCMD_OBJ_CMD(obj, "ModelThread = " << (getModelThread() ? 1 : 0));
    if (!pcHole->CosmeticThread.isReadOnly())
        FCMD_OBJ_CMD(obj, "CosmeticThread = " << (getCosmeticThread() ? 1 : 0));
    if (!pcHole->ThreadDepthType.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadDepthType = " << getThreadDepthType());
    if (!pcHole->BaseProfileType.isReadOnly() && getBaseProfileType() > 0)
        FCMD_OBJ_CMD(obj, "BaseProfileType = " << getBaseProfileType());
    if (!pcHole->ThreadDepth.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadDepth = " << getThreadDepth());
    if (!pcHole->UseCustomThreadClearance.isReadOnly())
        FCMD_OBJ_CMD(obj, "UseCustomThreadClearance = " << (getUseCustomThreadClearance() ? 1 : 0));
    if (!pcHole->CustomThreadClearance.isReadOnly())
        FCMD_OBJ_CMD(obj, "CustomThreadClearance = " << getCustomThreadClearance());
    if (!pcHole->ThreadType.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadType = " << getThreadType());
    if (!pcHole->ThreadSize.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadSize = " << getThreadSize());
    if (!pcHole->ThreadClass.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadClass = " << getThreadClass());
    if (!pcHole->ThreadFit.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadFit = " << getThreadFit());
    if (!pcHole->ThreadDirection.isReadOnly())
        FCMD_OBJ_CMD(obj, "ThreadDirection = " << getThreadDirection());
    if (!pcHole->HoleCutType.isReadOnly())
        FCMD_OBJ_CMD(obj, "HoleCutType = " << getHoleCutType());
    if (!pcHole->HoleCutCustomValues.isReadOnly())
        FCMD_OBJ_CMD(obj, "HoleCutCustomValues = " << (getHoleCutCustomValues() ? 1 : 0));
    if (!pcHole->DepthType.isReadOnly())
        FCMD_OBJ_CMD(obj, "DepthType = " << getDepthType());
    if (!pcHole->DrillPoint.isReadOnly())
        FCMD_OBJ_CMD(obj, "DrillPoint = " << getDrillPoint());
    if (!pcHole->DrillForDepth.isReadOnly())
        FCMD_OBJ_CMD(obj, "DrillForDepth = " << (getDrillForDepth() ? 1 : 0));
    if (!pcHole->Tapered.isReadOnly())
        FCMD_OBJ_CMD(obj, "Tapered = " << getTapered());
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgHoleParameters::TaskDlgHoleParameters(ViewProviderHole* HoleView)
    : TaskDlgSketchBasedParameters(HoleView)
{
    assert(HoleView);
    parameter = new TaskHoleParameters(static_cast<ViewProviderHole*>(vp));

    Content.push_back(parameter);
}

TaskDlgHoleParameters::~TaskDlgHoleParameters() = default;

#include "moc_TaskHoleParameters.cpp"

TaskHoleParameters::Observer::Observer(TaskHoleParameters* _owner, PartDesign::Hole* _hole)
    : DocumentObserver(_hole->getDocument())
    , owner(_owner)
    , hole(_hole)
{
}

void TaskHoleParameters::Observer::slotChangedObject(const App::DocumentObject& Obj, const App::Property& Prop)
{
    if (&Obj == hole) {
        Base::Console().Log("Parameter %s was updated with a new value\n", Prop.getName());
        if (Obj.getDocument())
            owner->changedObject(*Obj.getDocument(), Prop);
    }
}
