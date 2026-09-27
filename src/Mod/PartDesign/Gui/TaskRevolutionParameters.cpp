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

#include <QHBoxLayout>

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Origin.h>
#include <Base/Console.h>
#include <Base/Converter.h>
#include <Base/ExceptionSafeCall.h>
#include <Base/Rotation.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/CommandT.h>
#include <Gui/Selection.h>
#include <Gui/ViewProvider.h>
#include <Gui/ViewProviderCoordinateSystem.h>
#include <Gui/Inventor/Draggers/SoRotationDragger.h>
#include <Gui/Utilities.h>
#include <Mod/PartDesign/App/FeatureRevolution.h>
#include <Mod/PartDesign/App/FeatureGroove.h>
#include <Mod/PartDesign/App/Body.h>

#include "ui_TaskRevolutionParameters.h"
#include "TaskRevolutionParameters.h"
#include "ReferenceSelection.h"
#include "Utils.h"

using namespace PartDesignGui;
using namespace Gui;

namespace {

using RevolMethod = PartDesign::Revolved::RevolMethod;

bool isGrooveView(PartDesignGui::ViewProvider* vp)
{
    return vp && vp->getObject() && vp->getObject()->isDerivedFrom<PartDesign::Groove>();
}

/// A link for Python: an object with no element is written with an empty one
std::string linkStr(const App::PropertyLinkSub &link)
{
    const auto &subs = link.getSubValues();
    return buildLinkSingleSubPythonStr(link.getValue(),
                                       subs.empty() ? std::vector<std::string>{""} : subs);
}

} // anonymous namespace

/* TRANSLATOR PartDesignGui::TaskRevolutionParameters */

TaskRevolutionParameters::TaskRevolutionParameters(PartDesignGui::ViewProvider* RevolutionView, QWidget *parent)
    : TaskSketchBasedParameters(RevolutionView, parent,
                                isGrooveView(RevolutionView) ? "PartDesign_Groove" : "PartDesign_Revolution",
                                isGrooveView(RevolutionView) ? tr("Groove parameters")
                                                             : tr("Revolution parameters")),
      ui(new Ui_TaskRevolutionParameters),
      proxy(new QWidget(this)),
      isGroove(isGrooveView(RevolutionView))
{
    // we need a separate container widget to add all controls to
    ui->setupUi(proxy);

    ui->axis->setMouseTracking(true);
    ui->axis->installEventFilter(this);

    this->initUI(proxy);
    this->groupLayout()->addWidget(proxy);

    PartDesign::Revolved* feat = getRevolved();
    ui->revolveAngle->bind(feat->Angle);
    ui->revolveAngle2->bind(feat->Angle2);
    ui->startOffsetEdit->bind(feat->StartOffset);

    // The up-to faces, one per side, picked like the Pad's
    upToWidget = makeUpToWidget(ui->upToFaceHolder, feat->UpToFace, SelectionMode::refUpTo);
    upToWidget2 = makeUpToWidget(ui->upToFaceHolder2, feat->UpToFace2, SelectionMode::refUpTo2);
    // Where it starts, with StartType Reference (upstream a4b3950ac1): one
    // face, a datum plane or a sketch, picked like the up-to faces
    startWidget = new LinkSubWidget(this, tr("Reference"), feat->StartReference,
                                    /*singleElement*/true);
    startWidget->setSelectionMode(SelectionMode::refStart);
    startWidget->setHideLinked(false);
    startWidget->setSelectionConfig(AllowSelection::FACE
                                    | AllowSelection::OTHERBODY
                                    | AllowSelection::WHOLE);
    startWidget->setPickFilter([this](const Gui::SelectionChanges &msg, App::SubObjectT &objT) {
        return filterUpToPick(msg, objT);
    });
    auto layout = new QHBoxLayout(ui->startReferenceHolder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(startWidget);

    refresh();
    setFocus ();

    //show the parts coordinate system axis for selection
    PartDesign::Body * body = PartDesign::Body::findBodyOf ( vp->getObject () );
    if(body) {
        try {
            App::Origin *origin = body->getOrigin();
            ViewProviderCoordinateSystem* vpOrigin;
            vpOrigin = static_cast<ViewProviderCoordinateSystem*>(Gui::Application::Instance->getViewProvider(origin));
            vpOrigin->setTemporaryVisibility(true, false);
        } catch (const Base::Exception &ex) {
            ex.ReportException();
        }
    }

    onAxisButton(true);

    connectSignals();

    setupGizmos(RevolutionView);
}

PartDesign::Revolved* TaskRevolutionParameters::getRevolved() const
{
    auto feat = vp ? Base::freecad_dynamic_cast<PartDesign::Revolved>(vp->getObject()) : nullptr;
    if (!feat)
        THROWM(Base::TypeError, "The object is neither a Groove nor a Revolution.")
    return feat;
}

LinkSubWidget *TaskRevolutionParameters::makeUpToWidget(QWidget *holder,
                                                        App::PropertyLinkSub &prop,
                                                        SelectionMode mode)
{
    auto widget = new LinkSubWidget(this, tr("Face"), prop, /*singleElement*/true);
    widget->setSelectionMode(mode);
    // what the revolution goes up to stays in sight
    widget->setHideLinked(false);
    widget->setSelectionConfig(AllowSelection::FACE
                               | AllowSelection::OTHERBODY
                               | AllowSelection::WHOLE);
    widget->setPickFilter([this](const Gui::SelectionChanges &msg, App::SubObjectT &objT) {
        return filterUpToPick(msg, objT);
    });
    auto layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(widget);
    return widget;
}

void TaskRevolutionParameters::setupGizmos(ViewProvider* vp)
{
    if (!GizmoContainer::isEnabled()) {
        return;
    }

    // A click on an angle gizmo that does not drag it turns the revolution
    const auto toggleReversed = [this] {
        if (ui->checkBoxReversed->isEnabled())
            ui->checkBoxReversed->setChecked(!ui->checkBoxReversed->isChecked());
    };

    rotationGizmo = new Gui::RadialGizmo(ui->revolveAngle);
    rotationGizmo->setClickCallback(toggleReversed);
    rotationGizmo2 = new Gui::RadialGizmo(ui->revolveAngle2);
    rotationGizmo2->setClickCallback(toggleReversed);
    startOffsetGizmo = new Gui::RotationGizmo(ui->startOffsetEdit);

    gizmoContainer = GizmoContainer::create({rotationGizmo, rotationGizmo2, startOffsetGizmo}, vp);
    rotationGizmo->flipArrow();
    rotationGizmo2->flipArrow();

    defaultGizmoMultFactor = rotationGizmo->getMultFactor();

    setGizmoPositions();
    showDraggerHints();
}

void TaskRevolutionParameters::setGizmoPositions()
{
    if (!gizmoContainer) {
        return;
    }

    auto feature = getRevolved();
    if (!feature || feature->isError()) {
        gizmoContainer->visible = false;
        return;
    }
    gizmoContainer->visible = true;

    Base::Vector3d profileCog;
    Part::TopoShape profile = feature->getProfileShape();
    profile.getCenterOfGravity(profileCog);
    const Base::Vector3d basePos = feature->Base.getValue();
    Base::Vector3d axisDir = feature->Axis.getValue();
    const bool reversed = feature->Reversed.getValue();
    const std::string sideType = feature->SideType.getValueAsString();
    const bool symmetric = sideType == "Symmetric";
    const auto method = PartDesign::Revolved::methodOf(feature->Type);
    const auto method2 = PartDesign::Revolved::methodOf(feature->Type2);

    auto diff = profileCog - basePos;
    axisDir.Normalize();
    auto axisComp = axisDir * diff.Dot(axisDir);
    auto normalComp = diff - axisComp;

    if (reversed) {
        axisDir = -axisDir;
    }
    // Reversed does not turn a symmetric revolution's start (Revolved::startAxis)
    const Base::Vector3d startAxisDir = reversed && symmetric ? -axisDir : axisDir;

    // The angles are measured from the start, wherever StartType puts it;
    // the start offset from where the reference is met
    Base::Vector3d startDirection = normalComp;
    Base::Vector3d referenceDirection = normalComp;
    try {
        const double start = feature->getStartOffset();
        startDirection = Base::Rotation(startAxisDir, Base::toRadians(start)).multVec(normalComp);
        referenceDirection = Base::Rotation(startAxisDir,
                Base::toRadians(start - feature->StartOffset.getValue())).multVec(normalComp);
    }
    catch (const Base::Exception&) {
    }

    const Base::Vector3d axisPosition = basePos + axisComp;
    rotationGizmo->Gizmo::setDraggerPlacement(axisPosition, startDirection);
    rotationGizmo->getDraggerContainer()->setArcNormalDirection(Base::convertTo<SbVec3f>(axisDir));
    rotationGizmo->setVisibility(method == RevolMethod::Angle);

    rotationGizmo2->Gizmo::setDraggerPlacement(axisPosition, startDirection);
    rotationGizmo2->getDraggerContainer()->setArcNormalDirection(Base::convertTo<SbVec3f>(-axisDir));
    rotationGizmo2->setVisibility(sideType == "Two sides" && method2 == RevolMethod::Angle);

    startOffsetGizmo->Gizmo::setDraggerPlacement(axisPosition, referenceDirection);
    startOffsetGizmo->getDraggerContainer()->setArcNormalDirection(Base::convertTo<SbVec3f>(startAxisDir));
    startOffsetGizmo->setVisibility(strcmp(feature->StartType.getValueAsString(), "Profile plane") != 0);

    if (!symmetric) {
        rotationGizmo->setMultFactor(defaultGizmoMultFactor);
    }
    else {
        rotationGizmo->setMultFactor(defaultGizmoMultFactor / 2.0);
    }
}

void TaskRevolutionParameters::finishedRecomputeFeature()
{
    TaskSketchBasedParameters::finishedRecomputeFeature();
    setGizmoPositions();
}

void TaskRevolutionParameters::onAxisButton(bool checked)
{
    if (checked) {
        AllowSelectionFlags conf;
        conf.setFlag(AllowSelection::EDGE);
        conf.setFlag(AllowSelection::FACE, false);
        conf.setFlag(AllowSelection::PLANAR);
        conf.setFlag(AllowSelection::CIRCLE);
        TaskSketchBasedParameters::onSelectReference(ui->buttonAxis, SelectionMode::refAxis, conf);
    } else
        exitSelectionMode();
}

void TaskRevolutionParameters::onSelectionModeChanged(SelectionMode)
{
    // The reference widgets follow the mode themselves
    QSignalBlocker blocker(ui->buttonAxis);
    ui->buttonAxis->setChecked(getSelectionMode() == SelectionMode::refAxis);
}

void TaskRevolutionParameters::refresh()
{
    if (!vp || !vp->getObject())
        return;

    // Temporarily prevent unnecessary feature recomputes
    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(true);

    PartDesign::Revolved* feat = getRevolved();

    ui->startMode->setCurrentIndex(feat->StartType.getValue());
    ui->startOffsetEdit->setValue(feat->StartOffset.getValue());
    ui->sideTypeCB->setCurrentIndex(feat->SideType.getValue());
    ui->checkBoxReversed->setChecked(feat->Reversed.getValue());

    ui->revolveAngle->setMinimum(feat->Angle.getMinimum());
    ui->revolveAngle->setMaximum(feat->Angle.getMaximum());
    ui->revolveAngle->setValue(feat->Angle.getValue());
    ui->revolveAngle2->setMinimum(feat->Angle2.getMinimum());
    ui->revolveAngle2->setMaximum(feat->Angle2.getMaximum());
    ui->revolveAngle2->setValue(feat->Angle2.getValue());

    translateModeList(ui->changeMode, static_cast<int>(PartDesign::Revolved::methodOf(feat->Type)));
    translateModeList(ui->changeMode2, static_cast<int>(PartDesign::Revolved::methodOf(feat->Type2)));

    for (auto widget : {upToWidget, upToWidget2, startWidget}) {
        if (widget)
            widget->refresh();
    }

    blockUpdate = false;

    updateStartUI();
    updateUI();
    TaskSketchBasedParameters::refresh();

    for (QWidget* child : proxy->findChildren<QWidget*>())
        child->blockSignals(false);
}

void TaskRevolutionParameters::translateModeList(QComboBox *combo, int index)
{
    combo->clear();
    combo->addItem(tr("Angle"));
    if (!isGroove) {
        combo->addItem(tr("To last"));
    }
    else {
        combo->addItem(tr("Through all"));
    }
    combo->addItem(tr("To first"));
    combo->addItem(tr("Up to face"));
    combo->setCurrentIndex(index >= 0 && index < combo->count() ? index : 0);
}

void TaskRevolutionParameters::fillAxisCombo(bool forceRefill)
{
    Base::StateLocker lock(blockUpdate, true);

    if (axesInList.empty())
        forceRefill = true;//not filled yet, full refill

    if (forceRefill) {
        ui->axis->clear();
        axesInList.clear();

        auto *pcFeat = Base::freecad_dynamic_cast<PartDesign::ProfileBased>(vp->getObject());
        if (!pcFeat)
            THROWM(Base::TypeError, "The object is not ProfileBased.")

        //add sketch axes
        if (auto *pcSketch = Base::freecad_dynamic_cast<Part::Part2DObject>(pcFeat->Profile.getValue())) {
            addAxisToCombo(pcSketch, "V_Axis", QObject::tr("Vertical sketch axis"));
            addAxisToCombo(pcSketch, "H_Axis", QObject::tr("Horizontal sketch axis"));
            for (int i=0; i < pcSketch->getAxisCount(); i++) {
                QString itemText = QObject::tr("Construction line %1").arg(i+1);
                std::stringstream sub;
                sub << "Axis" << i;
                addAxisToCombo(pcSketch,sub.str(),itemText);
            }
        }

        //add origin axes
        if (PartDesign::Body * body = PartDesign::Body::findBodyOf(pcFeat)) {
            try {
                App::Origin* orig = body->getOrigin();
                addAxisToCombo(orig->getX(), std::string(), tr("Base X axis"));
                addAxisToCombo(orig->getY(), std::string(), tr("Base Y axis"));
                addAxisToCombo(orig->getZ(), std::string(), tr("Base Z axis"));
            } catch (const Base::Exception &ex) {
                ex.ReportException();
            }
        }

        //add "Select reference"
        addAxisToCombo(nullptr, std::string(), tr("Select reference..."));
    }//endif forceRefill

    //add current link, if not in list
    //first, figure out the item number for current axis
    int indexOfCurrent = -1;
    const App::PropertyLinkSub &refAxis = getRevolved()->ReferenceAxis;
    App::DocumentObject* ax = refAxis.getValue();
    const std::vector<std::string> &subList = refAxis.getSubValues();
    for (size_t i = 0; i < axesInList.size(); i++) {
        if (ax == axesInList[i]->getValue() && subList == axesInList[i]->getSubValues())
            indexOfCurrent = i;
    }
    if (indexOfCurrent == -1  &&  ax) {
        assert(subList.size() <= 1);
        std::string sub;
        if (!subList.empty())
            sub = subList[0];
        addAxisToCombo(ax, sub, getRefStr(ax, subList));
        indexOfCurrent = axesInList.size()-1;
    }

    //highlight current.
    if (indexOfCurrent != -1)
        ui->axis->setCurrentIndex(indexOfCurrent);
}

void TaskRevolutionParameters::addAxisToCombo(App::DocumentObject* linkObj,
                                              std::string linkSubname,
                                              QString itemText)
{
    this->ui->axis->addItem(itemText);
    this->axesInList.emplace_back(new App::PropertyLinkSub());
    App::PropertyLinkSub &lnk = *(axesInList[axesInList.size()-1]);
    lnk.setValue(linkObj,std::vector<std::string>(1,linkSubname));
}

void TaskRevolutionParameters::setCheckboxes()
{
    const int sideType = ui->sideTypeCB->currentIndex();
    const bool twoSides = sideType == 1;
    const bool symmetric = sideType == 2;
    const auto method = static_cast<RevolMethod>(ui->changeMode->currentIndex());
    const auto method2 = static_cast<RevolMethod>(ui->changeMode2->currentIndex());

    const bool angle = method == RevolMethod::Angle;
    ui->labelAngle->setVisible(angle);
    ui->revolveAngle->setVisible(angle);
    ui->revolveAngle->setEnabled(angle);
    ui->upToFaceHolder->setVisible(method == RevolMethod::ToFace);

    ui->groupBoxSide2->setVisible(twoSides);
    const bool angle2 = method2 == RevolMethod::Angle;
    ui->labelAngle2->setVisible(angle2);
    ui->revolveAngle2->setVisible(angle2);
    ui->revolveAngle2->setEnabled(twoSides && angle2);
    ui->upToFaceHolder2->setVisible(method2 == RevolMethod::ToFace);

    // Turning a symmetric sweep makes the same solid
    const bool throughAll = isGroove && method == RevolMethod::ThroughAll;
    ui->checkBoxReversed->setEnabled(!(symmetric && (angle || throughAll)));

    // Leave a face pick that has no row to show it
    if (method != RevolMethod::ToFace && getSelectionMode() == SelectionMode::refUpTo)
        exitSelectionMode();
    if ((!twoSides || method2 != RevolMethod::ToFace)
            && getSelectionMode() == SelectionMode::refUpTo2)
        exitSelectionMode();
}

void TaskRevolutionParameters::updateStartUI()
{
    const int type = ui->startMode->currentIndex();
    ui->labelStartOffset->setVisible(type != 0);
    ui->startOffsetEdit->setVisible(type != 0);
    ui->startReferenceHolder->setVisible(type == 2);
    // Leave the start pick once there is no reference to pick
    if (type != 2 && getSelectionMode() == SelectionMode::refStart)
        exitSelectionMode();
}

void TaskRevolutionParameters::connectSignals()
{
    QMetaObject::connectSlotsByName(this);
    Base::connect(ui->revolveAngle, QOverload<double>::of(&Gui::QuantitySpinBox::valueChanged),
                  this, &TaskRevolutionParameters::onAngleChanged);
    Base::connect(ui->revolveAngle2, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
                 this, &TaskRevolutionParameters::onAngle2Changed);
    Base::connect(ui->startOffsetEdit, qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
                 this, &TaskRevolutionParameters::onStartOffsetChanged);
    Base::connect(ui->axis, QOverload<int>::of(&QComboBox::currentIndexChanged),
                  this, &TaskRevolutionParameters::onAxisChanged);
    Base::connect(ui->checkBoxReversed, &QCheckBox::toggled, this, &TaskRevolutionParameters::onReversed);
    Base::connect(ui->buttonAxis, &QPushButton::clicked, this, &TaskRevolutionParameters::onAxisButton);
    Base::connect(ui->changeMode, qOverload<int>(&QComboBox::currentIndexChanged),
                 this, &TaskRevolutionParameters::onModeChanged);
    Base::connect(ui->changeMode2, qOverload<int>(&QComboBox::currentIndexChanged),
                 this, &TaskRevolutionParameters::onMode2Changed);
    Base::connect(ui->sideTypeCB, qOverload<int>(&QComboBox::currentIndexChanged),
                 this, &TaskRevolutionParameters::onSideTypeChanged);
    Base::connect(ui->startMode, qOverload<int>(&QComboBox::currentIndexChanged),
                 this, &TaskRevolutionParameters::onStartModeChanged);
}

void TaskRevolutionParameters::updateUI()
{
    if (blockUpdate)
        return;
    Base::StateLocker lock(blockUpdate, true);
    fillAxisCombo();
    setCheckboxes();
}

void TaskRevolutionParameters::_onSelectionChanged(const Gui::SelectionChanges& msg)
{
    if (msg.Type != Gui::SelectionChanges::AddSelection
            || getSelectionMode() != SelectionMode::refAxis)
        return;
    std::vector<std::string> axis;
    App::DocumentObject* selObj;
    if (getReferencedSelection(vp->getObject(), msg, selObj, axis) && selObj) {
        setupTransaction();
        getRevolved()->ReferenceAxis.setValue(selObj, axis);
        recomputeFeature();
        updateUI();
    }
}

void TaskRevolutionParameters::onAngleChanged(double len)
{
    setupTransaction();
    getRevolved()->Angle.setValue(len);
    recomputeFeature();
}

void TaskRevolutionParameters::onAngle2Changed(double len)
{
    setupTransaction();
    getRevolved()->Angle2.setValue(len);
    exitSelectionMode();
    recomputeFeature();
}

void TaskRevolutionParameters::onStartOffsetChanged(double len)
{
    setupTransaction();
    getRevolved()->StartOffset.setValue(len);
    recomputeFeature();
}

bool TaskRevolutionParameters::eventFilter(QObject *o, QEvent *ev)
{
    if (!vp || o != ui->axis)
        return false;
    switch(ev->type()) {
    case QEvent::Leave:
        Gui::Selection().rmvPreselect();
        break;
    case QEvent::Enter: {
        const App::PropertyLinkSub &refAxis = getRevolved()->ReferenceAxis;
        if (auto obj = refAxis.getValue()) {
            const auto &subs = refAxis.getSubValues();
            PartDesignGui::highlightObjectOnTop(App::SubObjectT(obj, subs.size()?subs.front().c_str():""));
        }
        break;
    }
    default:
        break;
    }
    return false;
}

void TaskRevolutionParameters::onAxisChanged(int num)
{
    if (blockUpdate)
        return;
    PartDesign::Revolved* pcRevolution = getRevolved();

    if (axesInList.empty())
        return;

    App::DocumentObject *oldRefAxis = pcRevolution->ReferenceAxis.getValue();
    std::vector<std::string> oldSubRefAxis = pcRevolution->ReferenceAxis.getSubValues();
    std::string oldRefName;
    if (!oldSubRefAxis.empty())
        oldRefName = oldSubRefAxis.front();

    App::PropertyLinkSub &lnk = *(axesInList[num]);
    if (lnk.getValue()) {
        if (!pcRevolution->getDocument()->isIn(lnk.getValue())){
            Base::Console().Error("Object was deleted\n");
            return;
        }
        setupTransaction();
        pcRevolution->ReferenceAxis.Paste(lnk);
    }

    try {
        App::DocumentObject *newRefAxis = pcRevolution->ReferenceAxis.getValue();
        const std::vector<std::string> &newSubRefAxis = pcRevolution->ReferenceAxis.getSubValues();
        std::string newRefName;
        if (!newSubRefAxis.empty())
            newRefName = newSubRefAxis.front();

        if (oldRefAxis != newRefAxis ||
            oldSubRefAxis.size() != newSubRefAxis.size() ||
            oldRefName != newRefName) {
            bool reversed = pcRevolution->suggestReversed();
            if (reversed != pcRevolution->Reversed.getValue()) {
                pcRevolution->Reversed.setValue(reversed);
                QSignalBlocker blocker(ui->checkBoxReversed);
                ui->checkBoxReversed->setChecked(reversed);
            }
        }

        recomputeFeature();
    }
    catch (const Base::Exception& e) {
        e.ReportException();
    }
}

void TaskRevolutionParameters::onReversed(bool on)
{
    setupTransaction();
    getRevolved()->Reversed.setValue(on);
    recomputeFeature();
}

void TaskRevolutionParameters::onModeChanged(int index)
{
    setupTransaction();
    PartDesign::Revolved* feat = getRevolved();
    feat->Type.setValue(static_cast<long>(index));
    setCheckboxes();
    // nothing to revolve up to yet: pick it first
    if (static_cast<RevolMethod>(index) == RevolMethod::ToFace && !feat->UpToFace.getValue()) {
        if (upToWidget)
            upToWidget->startSelection();
        return;
    }
    if (static_cast<RevolMethod>(index) == RevolMethod::Angle) {
        ui->revolveAngle->selectNumber();
        QMetaObject::invokeMethod(ui->revolveAngle, "setFocus", Qt::QueuedConnection);
    }
    recomputeFeature();
}

void TaskRevolutionParameters::onMode2Changed(int index)
{
    setupTransaction();
    PartDesign::Revolved* feat = getRevolved();
    feat->Type2.setValue(static_cast<long>(index));
    setCheckboxes();
    if (static_cast<RevolMethod>(index) == RevolMethod::ToFace && !feat->UpToFace2.getValue()) {
        if (upToWidget2)
            upToWidget2->startSelection();
        return;
    }
    recomputeFeature();
}

void TaskRevolutionParameters::onSideTypeChanged(int index)
{
    setupTransaction();
    PartDesign::Revolved* feat = getRevolved();
    feat->SideType.setValue(static_cast<long>(index));
    setCheckboxes();
    if (index == 1 && PartDesign::Revolved::methodOf(feat->Type2) == RevolMethod::ToFace
            && !feat->UpToFace2.getValue()) {
        if (upToWidget2)
            upToWidget2->startSelection();
        return;
    }
    recomputeFeature();
}

void TaskRevolutionParameters::onStartModeChanged(int index)
{
    setupTransaction();
    PartDesign::Revolved* feat = getRevolved();
    feat->StartType.setValue(static_cast<long>(index));
    updateStartUI();
    // nothing to start at yet: pick it first
    if (index == 2 && !feat->StartReference.getValue()) {
        if (startWidget)
            startWidget->startSelection();
        return;
    }
    recomputeFeature();
}

void TaskRevolutionParameters::getReferenceAxis(App::DocumentObject*& obj, std::vector<std::string>& sub) const
{
    if (axesInList.empty())
        THROWM(Base::RuntimeError, "Not initialized!")

    int num = ui->axis->currentIndex();
    const App::PropertyLinkSub &lnk = *(axesInList[num]);
    if (!lnk.getValue()) {
        THROWM(Base::RuntimeError, "Still in reference selection mode; reference wasn't selected yet")
    } else {
        PartDesign::ProfileBased* pcRevolution = static_cast<PartDesign::ProfileBased*>(vp->getObject());
        if (!pcRevolution->getDocument()->isIn(lnk.getValue())){
            THROWM(Base::RuntimeError, "Object was deleted")
        }

        obj = lnk.getValue();
        sub = lnk.getSubValues();
    }
}

TaskRevolutionParameters::~TaskRevolutionParameters()
{
    try {
        //hide the parts coordinate system axis for selection
        PartDesign::Body * body = vp ? PartDesign::Body::findBodyOf(vp->getObject()) : nullptr;
        if (body) {
            App::Origin *origin = body->getOrigin();
            ViewProviderCoordinateSystem* vpOrigin;
            vpOrigin = static_cast<ViewProviderCoordinateSystem*>(Gui::Application::Instance->getViewProvider(origin));
            vpOrigin->resetTemporaryVisibility();
        }
    } catch (const Base::Exception &ex) {
        ex.ReportException();
    }

    axesInList.clear();
}

void TaskRevolutionParameters::changeEvent(QEvent *event)
{
    TaskBox::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        const int mode = ui->changeMode->currentIndex();
        const int mode2 = ui->changeMode2->currentIndex();
        ui->retranslateUi(proxy);

        // Translate mode items
        translateModeList(ui->changeMode, mode);
        translateModeList(ui->changeMode2, mode2);
    }
}

void TaskRevolutionParameters::apply()
{
    ui->revolveAngle->apply();
    ui->revolveAngle2->apply();
    ui->startOffsetEdit->apply();
    std::vector<std::string> sub;
    App::DocumentObject* obj;
    getReferenceAxis(obj, sub);
    std::string axis = buildLinkSingleSubPythonStr(obj, sub);
    auto tobj = vp->getObject();
    PartDesign::Revolved* feat = getRevolved();
    FCMD_OBJ_CMD(tobj, "ReferenceAxis = " << axis);
    FCMD_OBJ_CMD(tobj, "SideType = '" << feat->SideType.getValueAsString() << "'");
    FCMD_OBJ_CMD(tobj, "Reversed = " << (feat->Reversed.getValue() ? 1 : 0));
    FCMD_OBJ_CMD(tobj, "Type = '" << feat->Type.getValueAsString() << "'");
    FCMD_OBJ_CMD(tobj, "Type2 = '" << feat->Type2.getValueAsString() << "'");
    // A face a side no longer goes up to is let go
    const bool twoSides = strcmp(feat->SideType.getValueAsString(), "Two sides") == 0;
    const bool upTo = PartDesign::Revolved::methodOf(feat->Type) == RevolMethod::ToFace;
    const bool upTo2 = twoSides && PartDesign::Revolved::methodOf(feat->Type2) == RevolMethod::ToFace;
    FCMD_OBJ_CMD(tobj, "UpToFace = " << (upTo ? linkStr(feat->UpToFace) : std::string("None")));
    FCMD_OBJ_CMD(tobj, "UpToFace2 = " << (upTo2 ? linkStr(feat->UpToFace2) : std::string("None")));
    FCMD_OBJ_CMD(tobj, "StartType = '" << feat->StartType.getValueAsString() << "'");
    FCMD_OBJ_CMD(tobj, "StartReference = " << linkStr(feat->StartReference));
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
TaskDlgRevolutionParameters::TaskDlgRevolutionParameters(PartDesignGui::ViewProvider *RevolutionView)
    : TaskDlgSketchBasedParameters(RevolutionView)
{
    assert(RevolutionView);
    Content.push_back(new TaskRevolutionParameters(RevolutionView));
}


#include "moc_TaskRevolutionParameters.cpp"
