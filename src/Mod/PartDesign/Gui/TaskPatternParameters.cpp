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
# include <QComboBox>
# include <QFormLayout>
# include <QGroupBox>
# include <QLabel>
# include <QMessageBox>
# include <QSignalBlocker>
# include <QTimer>
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
#include <Gui/Document.h>
#include <Mod/PartDesign/App/Body.h>
#include <Mod/PartDesign/App/FeatureAddSub.h>
#include <Mod/PartDesign/App/FeaturePattern.h>

#include "ui_TaskPatternParameters.h"
#include "TaskPatternParameters.h"
#include "ReferenceSelection.h"
#include "TaskMultiTransformParameters.h"


using namespace PartDesignGui;
using namespace Gui;

namespace
{

template<class T>
T* patternProperty(App::DocumentObject* obj, const char* name)
{
    return obj ? dynamic_cast<T*>(App::Pattern::getProperty(*obj, name)) : nullptr;
}

}  // namespace

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

PartDesign::PatternFeature* TaskPatternParameters::getPattern() const
{
    return Base::freecad_dynamic_cast<PartDesign::PatternFeature>(getObject());
}

App::Pattern::Type TaskPatternParameters::getPatternType() const
{
    auto pattern = getPattern();
    return pattern ? pattern->getPatternType() : App::Pattern::Type::Linear;
}

bool TaskPatternParameters::isPolar() const
{
    return getPatternType() == App::Pattern::Type::Polar
        || getPatternType() == App::Pattern::Type::Circular;
}

void TaskPatternParameters::setupUI()
{
    setupBaseUI();

    // The kind, which may be changed: the inputs change with it
    auto typeForm = new QFormLayout();
    labelType = new QLabel(proxy);
    comboType = new QComboBox(proxy);
    fillPatternTypeCombo(comboType);
    typeForm->addRow(labelType, comboType);
    ui->verticalLayout->insertLayout(0, typeForm);
    connect(comboType, qOverload<int>(&QComboBox::activated),
            this, &TaskPatternParameters::onTypeActivated);

    buildPatternWidgets();

    connect(ui->checkBoxUpdateView, &QCheckBox::toggled,
            this, &TaskPatternParameters::onUpdateView);

    showOriginAxes(true);
}

void TaskPatternParameters::buildPatternWidgets()
{
    exitSelectionMode();
    picking = nullptr;

    // The inputs are other properties now: the old editors go
    for (QWidget* widget : {static_cast<QWidget*>(groupDirection1),
                            static_cast<QWidget*>(groupDirection2),
                            static_cast<QWidget*>(direction1),
                            static_cast<QWidget*>(direction2),
                            static_cast<QWidget*>(parameters)}) {
        if (widget) {
            widget->hide();
            widget->deleteLater();
        }
    }
    groupDirection1 = groupDirection2 = nullptr;
    direction1 = direction2 = nullptr;
    parameters = nullptr;

    auto pattern = getPattern();
    if (!pattern)
        return;
    builtType = pattern->getPatternType();

    using Kind = Gui::PatternDirectionWidget::Kind;
    auto layout = ui->directionsLayout;
    switch (builtType) {
    case App::Pattern::Type::Polar:
        direction1 = new Gui::PatternDirectionWidget(Kind::Polar, proxy);
        layout->addWidget(direction1);
        fillReferenceCombo(direction1->links());
        direction1->bind(Gui::PatternDirectionWidget::propertiesOf(*pattern, Kind::Polar, false));
        break;
    case App::Pattern::Type::Linear: {
        groupDirection1 = new QGroupBox(proxy);
        auto groupLayout1 = new QVBoxLayout(groupDirection1);
        direction1 = new Gui::PatternDirectionWidget(Kind::Linear, groupDirection1);
        groupLayout1->addWidget(direction1);
        layout->addWidget(groupDirection1);
        fillReferenceCombo(direction1->links());
        direction1->bind(Gui::PatternDirectionWidget::propertiesOf(*pattern, Kind::Linear, false));

        // Checked as long as the second direction has more than one
        // occurrence; unchecking it leaves one (upstream b82505e86c)
        groupDirection2 = new QGroupBox(proxy);
        groupDirection2->setCheckable(true);
        auto groupLayout2 = new QVBoxLayout(groupDirection2);
        direction2 = new Gui::PatternDirectionWidget(Kind::Linear, groupDirection2);
        groupLayout2->addWidget(direction2);
        layout->addWidget(groupDirection2);
        fillReferenceCombo(direction2->links());
        direction2->bind(Gui::PatternDirectionWidget::propertiesOf(*pattern, Kind::Linear, true));

        connect(groupDirection2, &QGroupBox::toggled,
                this, &TaskPatternParameters::onDirection2Toggled);
        connect(direction2, &Gui::PatternDirectionWidget::referenceActivated,
                this, [this]() { onReferenceActivated(direction2->links(), direction2->properties().reference); });
        connect(direction2, &Gui::PatternDirectionWidget::changed,
                this, &TaskPatternParameters::onParametersChanged);
        break;
    }
    default:
        parameters = new Gui::PatternParametersWidget(
            Gui::PatternParametersWidget::kindOf(builtType), proxy);
        layout->addWidget(parameters);
        fillReferenceCombo(parameters->links());
        parameters->bind(pattern);
        connect(parameters, &Gui::PatternParametersWidget::referenceActivated,
                this, [this]() { onReferenceActivated(parameters->links(), parameters->referenceProperty()); });
        connect(parameters, &Gui::PatternParametersWidget::changed,
                this, &TaskPatternParameters::onParametersChanged);
        break;
    }

    if (direction1) {
        connect(direction1, &Gui::PatternDirectionWidget::referenceActivated,
                this, [this]() { onReferenceActivated(direction1->links(), direction1->properties().reference); });
        connect(direction1, &Gui::PatternDirectionWidget::changed,
                this, &TaskPatternParameters::onParametersChanged);
    }

    retranslate();
    updateUI();
    // Once the edit has started: the panel is built while it starts, before
    // the view it runs in is recorded
    QTimer::singleShot(0, this, [this]() {
        updateLabels();
    });
}

void TaskPatternParameters::onTypeActivated(int index)
{
    auto pattern = getPattern();
    if (blockUpdate || !pattern || index == pattern->PatternType.getValue())
        return;
    try {
        setupTransaction();
        // The feature gives the new kind the references it needs
        pattern->PatternType.setValue(index);
    }
    catch (Base::Exception &e) {
        QMessageBox::warning(nullptr, tr("Error"), QApplication::translate("Exception", e.what()));
    }
    buildPatternWidgets();
    if (parentTask)
        parentTask->refreshTransformItem(pattern);
    recomputeFeature();
}

void TaskPatternParameters::updateLabels()
{
    if (!direction1) {
        return;
    }
    auto vp = getTopTransformedView();
    auto pattern = getObject();
    Gui::ViewerContext* view = vp ? vp->getEditViewer() : nullptr;
    if (!view || !pattern) {
        return;
    }

    // The labels start from the middle of what is patterned (upstream's
    // choice), in the pattern's frame. Inside a MultiTransform the originals
    // are the MultiTransform's, whose placement this pattern shares.
    const Base::Placement placement = pattern->Placement.getValue();
    Base::BoundBox3d box;
    if (auto top = getTopTransformedObject()) {
        for (auto obj : top->OriginalSubs.getValues()) {
            if (auto addsub = Base::freecad_dynamic_cast<PartDesign::FeatureAddSub>(obj)) {
                auto shapeBox = addsub->AddSubShape.getShape().getBoundBox();
                if (shapeBox.IsValid()) {
                    box.Add(shapeBox.Transformed(addsub->Placement.getValue().toMatrix()));
                }
            }
        }
    }
    Base::Vector3d origin;
    if (box.IsValid()) {
        placement.inverse().multVec(box.GetCenter(), origin);
    }

    App::Pattern::Context context;
    context.placement = placement;
    const Base::Matrix4D& toWorld = vp->getDocument()->getEditingTransform();
    const auto kind = getPatternType() == App::Pattern::Type::Polar
        ? Gui::PatternDirectionWidget::Kind::Polar
        : Gui::PatternDirectionWidget::Kind::Linear;

    auto show = [&](Gui::PatternDirectionWidget* widget, bool second) {
        if (!widget) {
            return;
        }
        Gui::PatternLabelFrame frame;
        if (Gui::patternLabelFrame(kind, *pattern, context, second, toWorld, origin, frame)) {
            widget->showLabels(view, frame);
        }
        else {
            widget->clearLabels();
        }
    };
    show(direction1, false);
    show(direction2, true);
}

void TaskPatternParameters::retranslate()
{
    if (labelType)
        labelType->setText(tr("Pattern"));
    if (comboType)
        fillPatternTypeCombo(comboType);
    if (groupDirection1)
        groupDirection1->setTitle(tr("Direction 1"));
    if (groupDirection2)
        groupDirection2->setTitle(tr("Direction 2"));
    if (direction1)
        direction1->retranslate();
    if (direction2)
        direction2->retranslate();
    if (parameters)
        parameters->retranslate();
}

void TaskPatternParameters::fillReferenceCombo(Gui::ComboLinks& links)
{
    // A path or the points come from a pick; a direction or an axis may be
    // one of the sketch's or the origin's as well
    if (getPatternType() == App::Pattern::Type::Path
            || getPatternType() == App::Pattern::Type::Point) {
        links.clear();
        links.addLink(nullptr, std::string(), tr("Select reference..."));
        return;
    }
    App::DocumentObject* sketch = getSketchObject();
    this->fillAxisCombo(links, Base::freecad_dynamic_cast<Part::Part2DObject>(sketch));
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

void TaskPatternParameters::refreshAfterUndo()
{
    // Undo and redo bring back the inputs of a kind as new properties, even
    // of the kind the editors show: bind them anew
    buildPatternWidgets();
    TaskTransformedParameters::refreshAfterUndo();
}

void TaskPatternParameters::updateUI()
{
    // The editors may be of a kind the pattern no longer has
    if (getPattern() && getPatternType() != builtType) {
        buildPatternWidgets();
        return;
    }
    Base::StateLocker lock(blockUpdate);
    if (comboType)
        comboType->setCurrentIndex(static_cast<int>(getPatternType()));
    if (direction1)
        direction1->updateUI();
    if (direction2) {
        auto occurrences2 = patternProperty<App::PropertyInteger>(getObject(), "Occurrences2");
        QSignalBlocker blocker(groupDirection2);
        groupDirection2->setChecked(occurrences2 && occurrences2->getValue() > 1);
        direction2->updateUI();
    }
    if (parameters)
        parameters->updateUI();
    updateLabels();
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
        auto prop = picking;
        setupTransaction();
        if (getPatternType() == App::Pattern::Type::Point) {
            // The points are all of the object's, whatever of it was clicked
            // (upstream f5abab2768)
            subs.clear();
        }
        else if (getPatternType() == App::Pattern::Type::Path) {
            // A path is picked edge by edge, of one object, until something
            // else is done in the panel
            if (prop->getValue() == selObj && !subs.empty() && !subs.front().empty()) {
                auto edges = prop->getSubValues();
                if (std::find(edges.begin(), edges.end(), subs.front()) == edges.end())
                    edges.push_back(subs.front());
                subs = std::move(edges);
            }
        }
        if (getPatternType() != App::Pattern::Type::Path) {
            exitSelectionMode();
            picking = nullptr;
        }
        prop->setValue(selObj, subs);
        recomputeFeature();
        updateUI();
        return;
    }

    TaskTransformedParameters::onSelectionChanged(msg);
}

void TaskPatternParameters::onReferenceActivated(Gui::ComboLinks& links, App::PropertyLinkSub* prop)
{
    if (!prop)
        return;
    try {
        if (!links.getCurrentLink().getValue()) {
            // enter reference selection mode
            picking = prop;
            selectionMode = reference;
            Gui::Selection().clearSelection();
            if (getPatternType() == App::Pattern::Type::Point)
                addReferenceSelectionGate(AllowSelection::POINT | AllowSelection::EDGE
                                          | AllowSelection::FACE | AllowSelection::WHOLE);
            else if (getPatternType() == App::Pattern::Type::Path)
                addReferenceSelectionGate(AllowSelection::EDGE | AllowSelection::WHOLE);
            else if (isPolar())
                addReferenceSelectionGate(AllowSelection::EDGE | AllowSelection::CIRCLE);
            else
                addReferenceSelectionGate(AllowSelection::EDGE | AllowSelection::FACE
                                          | AllowSelection::PLANAR);
        }
        else {
            exitSelectionMode();
            picking = nullptr;
            prop->Paste(links.getCurrentLink());
        }
    }
    catch (Base::Exception &e) {
        QMessageBox::warning(nullptr, tr("Error"), QApplication::translate("Exception", e.what()));
    }

    updateLabels();
    kickUpdateViewTimer();
}

void TaskPatternParameters::onParametersChanged()
{
    exitSelectionMode();
    picking = nullptr;
    updateLabels();
    kickUpdateViewTimer();
}

void TaskPatternParameters::setDefaultDirection2()
{
    // The other in-plane axis of the first direction's sketch or origin, as
    // upstream's command sets V_Axis beside H_Axis; else the first other
    // item of the list
    auto direction = patternProperty<App::PropertyLinkSub>(getObject(), "Direction");
    auto direction2Prop = patternProperty<App::PropertyLinkSub>(getObject(), "Direction2");
    if (!direction || !direction2Prop)
        return;
    App::DocumentObject* obj = direction->getValue();
    const auto& subs = direction->getSubValues();
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
            direction2Prop->Paste(link);
            return;
        }
        if (!partnerRole.empty()) {
            auto feature = Base::freecad_dynamic_cast<App::DatumElement>(linkObj);
            if (feature && partnerRole == feature->Role.getValue()) {
                direction2Prop->Paste(link);
                return;
            }
        }
        if (fallback < 0 && (linkObj != obj || linkSub != sub))
            fallback = i;
    }
    if (fallback >= 0)
        direction2Prop->Paste(links.getLink(fallback));
}

void TaskPatternParameters::onDirection2Toggled(bool on)
{
    auto direction2Prop = patternProperty<App::PropertyLinkSub>(getObject(), "Direction2");
    auto occurrences2 = patternProperty<App::PropertyInteger>(getObject(), "Occurrences2");
    if (!direction2Prop || !occurrences2)
        return;
    try {
        if (on) {
            if (!direction2Prop->getValue())
                setDefaultDirection2();
            if (occurrences2->getValue() < 2)
                occurrences2->setValue(2);
        }
        else {
            occurrences2->setValue(1);
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
    if (auto pattern = getPattern())
        FCMD_OBJ_CMD(tobj, "PatternType = '" << pattern->PatternType.getValueAsString() << "'");
    if (parameters) {
        parameters->apply(tobj);
        return;
    }
    if (direction1)
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
