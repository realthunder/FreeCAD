/******************************************************************************
 *   Copyright (c) 2012 Jan Rheinlaender                                      *
 *                                   <jrheinlaender@users.sourceforge.net>    *
 *                                                                            *
 *   This file is part of the FreeCAD CAx development system.                 *
 *                                                                            *
 *   This library is free software; you can redistribute it and/or            *
 *   modify it under the terms of the GNU Library General Public              *
 *   License as published by the Free Software Foundation; either             *
 *   version 2 of the License, or (at your option) any later version.         *
 *                                                                            *
 *   This library  is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *   GNU Library General Public License for more details.                     *
 *                                                                            *
 *   You should have received a copy of the GNU Library General Public        *
 *   License along with this library; see the file COPYING.LIB. If not,       *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,            *
 *   Suite 330, Boston, MA  02111-1307, USA                                   *
 *                                                                            *
 ******************************************************************************/


#include "PreCompiled.h"

#ifndef _PreComp_
# include <QGroupBox>
# include <QMessageBox>
# include <QSignalBlocker>
# include <QVBoxLayout>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Origin.h>
#include <App/Datums.h>
#include <Base/Console.h>
#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/Selection.h>
#include <Gui/ViewProviderCoordinateSystem.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureLinearPattern.h>
#include <Mod/PartDesign/App/FeaturePolarPattern.h>

#include "ui_TaskPatternParameters.h"
#include "TaskPatternParameters.h"
#include "ReferenceSelection.h"
#include "TaskMultiTransformParameters.h"


using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskPatternParameters */

TaskPatternParameters::TaskPatternParameters(ViewProviderTransformed *TransformedView, QWidget *parent)
    : TaskTransformedParameters(TransformedView, parent)
    , ui(new Ui_TaskPatternParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);

    this->groupLayout()->addWidget(proxy);

    ui->buttonOK->hide();
    ui->checkBoxUpdateView->setEnabled(true);

    selectionMode = none;

    blockUpdate = false; // Hack, sometimes it is NOT false although set to false in Transformed::Transformed()!!
    setupUI();
}

TaskPatternParameters::TaskPatternParameters(TaskMultiTransformParameters *parentTask, QLayout *layout)
    : TaskTransformedParameters(parentTask)
    , ui(new Ui_TaskPatternParameters)
{
    proxy = new QWidget(parentTask);
    ui->setupUi(proxy);
    connect(ui->buttonOK, &QPushButton::pressed,
            parentTask, &TaskPatternParameters::onSubTaskButtonOK);

    layout->addWidget(proxy);

    ui->buttonOK->setEnabled(true);
    ui->checkBoxUpdateView->hide();

    selectionMode = none;

    // Hack, sometimes it is NOT false although set to false in Transformed::Transformed()!!
    blockUpdate = false;
    setupUI();
}

bool TaskPatternParameters::isPolar() const
{
    return getObject()->isDerivedFrom<PartDesign::PolarPattern>();
}

void TaskPatternParameters::setupUI()
{
    setupBaseUI();

    auto layout = ui->directionsLayout;
    if (auto polar = dynamic_cast<PartDesign::PolarPattern*>(getObject())) {
        direction1 = new Gui::PatternDirectionWidget(Gui::PatternDirectionWidget::Kind::Polar, proxy);
        layout->addWidget(direction1);
        Gui::PatternDirectionWidget::Properties props;
        props.reference = &polar->Axis;
        props.reversed = &polar->Reversed;
        props.mode = &polar->Mode;
        props.extent = &polar->Angle;
        props.spacing = &polar->Offset;
        props.occurrences = &polar->Occurrences;
        props.spacings = &polar->Spacings;
        props.spacingPattern = &polar->SpacingPattern;
        fillReferenceCombo(direction1);
        direction1->bind(props);
    }
    else {
        auto linear = static_cast<PartDesign::LinearPattern*>(getObject());

        groupDirection1 = new QGroupBox(proxy);
        auto groupLayout1 = new QVBoxLayout(groupDirection1);
        direction1 = new Gui::PatternDirectionWidget(Gui::PatternDirectionWidget::Kind::Linear, groupDirection1);
        groupLayout1->addWidget(direction1);
        layout->addWidget(groupDirection1);
        Gui::PatternDirectionWidget::Properties props;
        props.reference = &linear->Direction;
        props.reversed = &linear->Reversed;
        props.mode = &linear->Mode;
        props.extent = &linear->Length;
        props.spacing = &linear->Offset;
        props.occurrences = &linear->Occurrences;
        props.spacings = &linear->Spacings;
        props.spacingPattern = &linear->SpacingPattern;
        fillReferenceCombo(direction1);
        direction1->bind(props);

        // Checked as long as the second direction has more than one
        // occurrence; unchecking it leaves one (upstream b82505e86c)
        groupDirection2 = new QGroupBox(proxy);
        groupDirection2->setCheckable(true);
        groupDirection2->setChecked(linear->Occurrences2.getValue() > 1);
        auto groupLayout2 = new QVBoxLayout(groupDirection2);
        direction2 = new Gui::PatternDirectionWidget(Gui::PatternDirectionWidget::Kind::Linear, groupDirection2);
        groupLayout2->addWidget(direction2);
        layout->addWidget(groupDirection2);
        Gui::PatternDirectionWidget::Properties props2;
        props2.reference = &linear->Direction2;
        props2.reversed = &linear->Reversed2;
        props2.mode = &linear->Mode2;
        props2.extent = &linear->Length2;
        props2.spacing = &linear->Offset2;
        props2.occurrences = &linear->Occurrences2;
        props2.spacings = &linear->Spacings2;
        props2.spacingPattern = &linear->SpacingPattern2;
        fillReferenceCombo(direction2);
        direction2->bind(props2);

        connect(groupDirection2, &QGroupBox::toggled,
                this, &TaskPatternParameters::onDirection2Toggled);
        connect(direction2, &Gui::PatternDirectionWidget::referenceActivated,
                this, [this]() { onReferenceActivated(direction2); });
        connect(direction2, &Gui::PatternDirectionWidget::changed,
                this, &TaskPatternParameters::onParametersChanged);
    }

    connect(direction1, &Gui::PatternDirectionWidget::referenceActivated,
            this, [this]() { onReferenceActivated(direction1); });
    connect(direction1, &Gui::PatternDirectionWidget::changed,
            this, &TaskPatternParameters::onParametersChanged);
    connect(ui->checkBoxUpdateView, &QCheckBox::toggled,
            this, &TaskPatternParameters::onUpdateView);

    retranslate();
    showOriginAxes(true);
}

void TaskPatternParameters::retranslate()
{
    if (groupDirection1)
        groupDirection1->setTitle(tr("Direction 1"));
    if (groupDirection2)
        groupDirection2->setTitle(tr("Direction 2"));
    if (direction1)
        direction1->retranslate();
    if (direction2)
        direction2->retranslate();
}

void TaskPatternParameters::fillReferenceCombo(Gui::PatternDirectionWidget* widget)
{
    App::DocumentObject* sketch = getSketchObject();
    this->fillAxisCombo(widget->links(), Base::freecad_dynamic_cast<Part::Part2DObject>(sketch));
}

void TaskPatternParameters::showOriginAxes(bool show)
{
    // show the parts coordinate system axis for selection
    try {
        PartDesign::Body * body = PartDesign::Body::findBodyOf(getObject());
        if (!body)
            return;
        App::Origin *origin = body->getOrigin();
        auto vpOrigin = static_cast<ViewProviderCoordinateSystem*>(
            Gui::Application::Instance->getViewProvider(origin));
        if (show)
            vpOrigin->setTemporaryVisibility(true, false);
        else
            vpOrigin->resetTemporaryVisibility();
    }
    catch (const Base::Exception &ex) {
        Base::Console().Error ("%s\n", ex.what () );
    }
}

void TaskPatternParameters::updateUI()
{
    Base::StateLocker lock(blockUpdate);
    if (direction1)
        direction1->updateUI();
    if (direction2) {
        auto linear = static_cast<PartDesign::LinearPattern*>(getObject());
        QSignalBlocker blocker(groupDirection2);
        groupDirection2->setChecked(linear->Occurrences2.getValue() > 1);
        direction2->updateUI();
    }
}

void TaskPatternParameters::onSelectionChanged(const Gui::SelectionChanges& msg)
{
    // Only when picking a reference: in placement mode the selection belongs
    // to the placement dialog (upstream 1b799ad355)
    if (selectionMode == reference && picking
            && msg.Type == Gui::SelectionChanges::AddSelection) {
        std::vector<std::string> subs;
        App::DocumentObject* selObj = nullptr;
        getReferencedSelection(getObject(), msg, selObj, subs);
        // ReferenceSelection has already checked the selection for validity
        if (!selObj)
            return;
        auto widget = picking;
        exitSelectionMode();
        picking = nullptr;
        setupTransaction();
        widget->properties().reference->setValue(selObj, subs);
        recomputeFeature();
        updateUI();
        return;
    }

    TaskTransformedParameters::onSelectionChanged(msg);
}

void TaskPatternParameters::onReferenceActivated(Gui::PatternDirectionWidget* widget)
{
    try {
        if (!widget->links().getCurrentLink().getValue()) {
            // enter reference selection mode
            picking = widget;
            selectionMode = reference;
            Gui::Selection().clearSelection();
            if (isPolar())
                addReferenceSelectionGate(AllowSelection::EDGE | AllowSelection::CIRCLE);
            else
                addReferenceSelectionGate(AllowSelection::EDGE | AllowSelection::FACE
                                          | AllowSelection::PLANAR);
        }
        else {
            exitSelectionMode();
            picking = nullptr;
            widget->properties().reference->Paste(widget->links().getCurrentLink());
        }
    }
    catch (Base::Exception &e) {
        QMessageBox::warning(nullptr, tr("Error"), QApplication::translate("Exception", e.what()));
    }

    kickUpdateViewTimer();
}

void TaskPatternParameters::onParametersChanged()
{
    exitSelectionMode();
    picking = nullptr;
    kickUpdateViewTimer();
}

void TaskPatternParameters::setDefaultDirection2()
{
    // The other in-plane axis of the first direction's sketch or origin, as
    // upstream's command sets V_Axis beside H_Axis; else the first other
    // item of the list
    auto linear = static_cast<PartDesign::LinearPattern*>(getObject());
    App::DocumentObject* obj = linear->Direction.getValue();
    const auto& subs = linear->Direction.getSubValues();
    std::string sub = subs.empty() ? std::string() : subs.front();

    std::string partnerSub;
    std::string partnerRole;
    if (sub == "H_Axis")
        partnerSub = "V_Axis";
    else if (sub == "V_Axis")
        partnerSub = "H_Axis";
    else if (auto feature = Base::freecad_dynamic_cast<App::DatumElement>(obj)) {
        std::string role = feature->Role.getValue();
        partnerRole = role == App::Origin::AxisRoles[0] ? App::Origin::AxisRoles[1]
                                                        : App::Origin::AxisRoles[0];
    }

    ComboLinks& links = direction2->links();
    int fallback = -1;
    for (int i = 0; i < links.combo().count(); ++i) {
        const App::PropertyLinkSub& link = links.getLink(i);
        App::DocumentObject* linkObj = link.getValue();
        if (!linkObj)
            continue;
        const auto& linkSubs = link.getSubValues();
        std::string linkSub = linkSubs.empty() ? std::string() : linkSubs.front();
        if (!partnerSub.empty() && linkObj == obj && linkSub == partnerSub) {
            linear->Direction2.Paste(link);
            return;
        }
        if (!partnerRole.empty()) {
            auto feature = Base::freecad_dynamic_cast<App::DatumElement>(linkObj);
            if (feature && partnerRole == feature->Role.getValue()) {
                linear->Direction2.Paste(link);
                return;
            }
        }
        if (fallback < 0 && (linkObj != obj || linkSub != sub))
            fallback = i;
    }
    if (fallback >= 0)
        linear->Direction2.Paste(links.getLink(fallback));
}

void TaskPatternParameters::onDirection2Toggled(bool on)
{
    auto linear = static_cast<PartDesign::LinearPattern*>(getObject());
    try {
        if (on) {
            if (!linear->Direction2.getValue())
                setDefaultDirection2();
            if (linear->Occurrences2.getValue() < 2)
                linear->Occurrences2.setValue(2);
        }
        else {
            linear->Occurrences2.setValue(1);
        }
    }
    catch (Base::Exception &e) {
        QMessageBox::warning(nullptr, tr("Error"), QApplication::translate("Exception", e.what()));
    }
    direction2->updateUI();
    onParametersChanged();
}

void TaskPatternParameters::onUpdateView(bool on)
{
    blockUpdate = !on;
    if (on) {
        setupTransaction();
        recomputeFeature();
    }
}

TaskPatternParameters::~TaskPatternParameters()
{
    showOriginAxes(false);
    if (proxy)
        delete proxy;
}

void TaskPatternParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(proxy);
        retranslate();
    }
}

void TaskPatternParameters::apply()
{
    auto tobj = getObject();
    direction1->apply(tobj);
    if (direction2) {
        if (groupDirection2->isChecked())
            direction2->apply(tobj);
        else
            FCMD_OBJ_CMD(tobj, "Occurrences2 = 1");
    }
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgPatternParameters::TaskDlgPatternParameters(ViewProviderTransformed *PatternView)
    : TaskDlgTransformedParameters(PatternView, new TaskPatternParameters(PatternView))
{
}

#include "moc_TaskPatternParameters.cpp"
