// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>               *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <cstring>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/LinkArray.h>
#include <App/PropertyLinks.h>
#include <App/PropertyStandard.h>
#include <App/PropertyUnits.h>
#include <Base/Tools.h>

#include "Application.h"
#include "BitmapFactory.h"
#include "Command.h"
#include "Document.h"
#include "PatternWidgets.h"
#include "TaskLinkArray.h"
#include "ViewProviderLinkArray.h"

using namespace Gui;

namespace
{

template<class T>
T* patternProperty(App::LinkArray* array, const char* name)
{
    return dynamic_cast<T*>(App::Pattern::getProperty(*array, name));
}

/// The reference a selection names, relative to the group holding \a array:
/// a group of the array's is its parent, and linking to it is a cycle
bool getReference(App::LinkArray* array,
                  const SelectionChanges& msg,
                  App::DocumentObject*& obj,
                  std::string& sub)
{
    App::SubObjectT sel(msg.pDocName, msg.pObjectName, msg.pSubName);
    auto objs = sel.getSubObjectList();
    if (objs.empty()) {
        return false;
    }
    std::size_t start = 0;
    if (auto group = App::GeoFeatureGroupExtension::getGroupOfObject(array)) {
        for (std::size_t i = 0; i < objs.size(); ++i) {
            if (objs[i] == group) {
                start = i + 1;
            }
        }
    }
    if (start >= objs.size()) {
        return false;
    }
    const char* subname = msg.pSubName ? msg.pSubName : "";
    for (std::size_t i = 0; i < start; ++i) {
        const char* dot = std::strchr(subname, '.');
        if (!dot) {
            return false;
        }
        subname = dot + 1;
    }
    obj = objs[start];
    sub = subname;
    // Not the array, nor one of its elements
    for (std::size_t i = start; i < objs.size(); ++i) {
        if (objs[i] == array) {
            return false;
        }
        auto element = freecad_cast<App::LinkElement*>(objs[i]);
        if (element && element->getLinkGroup() == array) {
            return false;
        }
    }
    return true;
}

}  // namespace

TaskLinkArray::TaskLinkArray(ViewProviderLinkArray* vp, QWidget* parent)
    : TaskBox(BitmapFactory().pixmap("Link"), tr("Link array"), true, parent)
    , arrayT(vp->getObject())
{
    auto proxy = new QWidget(this);
    auto layout = new QVBoxLayout(proxy);
    auto form = new QFormLayout();
    layout->addLayout(form);

    labelLinked = new QLabel(proxy);
    buttonLinked = new QPushButton(proxy);
    buttonLinked->setCheckable(true);
    auto linkedLayout = new QHBoxLayout();
    linkedLayout->addWidget(labelLinked, 1);
    linkedLayout->addWidget(buttonLinked);
    form->addRow(tr("Linked object"), linkedLayout);

    labelType = new QLabel(proxy);
    comboType = new QComboBox(proxy);
    for (int i = 0; App::Pattern::TypeEnums[i]; ++i) {
        comboType->addItem(QString());
    }
    form->addRow(labelType, comboType);

    checkShowElement = new QCheckBox(proxy);
    form->addRow(checkShowElement);

    patternBox = new QWidget(proxy);
    patternLayout = new QVBoxLayout(patternBox);
    patternLayout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(patternBox);

    groupLayout()->addWidget(proxy);

    connect(comboType, qOverload<int>(&QComboBox::activated), this, &TaskLinkArray::onTypeActivated);
    connect(buttonLinked, &QPushButton::toggled, this, &TaskLinkArray::onLinkedButtonToggled);
    connect(checkShowElement, &QCheckBox::toggled, this, &TaskLinkArray::onShowElementToggled);

    retranslate();
    buildPatternWidgets();
}

TaskLinkArray::~TaskLinkArray() = default;

App::LinkArray* TaskLinkArray::getArray() const
{
    return freecad_cast<App::LinkArray*>(arrayT.getObject());
}

void TaskLinkArray::retranslate()
{
    buttonLinked->setText(tr("Select"));
    buttonLinked->setToolTip(tr("Pick the object to array in the 3D view or the tree"));
    labelType->setText(tr("Pattern"));
    comboType->setItemText(0, tr("Linear"));
    comboType->setItemText(1, tr("Polar"));
    comboType->setItemText(2, tr("Circular"));
    comboType->setItemText(3, tr("Along a path"));
    comboType->setItemText(4, tr("On points"));
    checkShowElement->setText(tr("Show elements"));
    checkShowElement->setToolTip(tr("Make an object of each element, which the tree shows and "
                                    "which can be hidden, suppressing it"));
    if (groupDirection1) {
        groupDirection1->setTitle(tr("Direction 1"));
    }
    if (groupDirection2) {
        groupDirection2->setTitle(tr("Direction 2"));
    }
}

void TaskLinkArray::changeEvent(QEvent* e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        retranslate();
        if (direction1) {
            direction1->retranslate();
        }
        if (direction2) {
            direction2->retranslate();
        }
        if (parameters) {
            parameters->retranslate();
        }
    }
}

void TaskLinkArray::fillReferenceCombo(ComboLinks& links, bool second)
{
    auto array = getArray();
    links.clear();
    App::PropertyLinkSub none;
    switch (array->getPatternType()) {
        case App::Pattern::Type::Linear:
            links.addLink(none, second ? tr("Local Y axis") : tr("Local X axis"));
            break;
        case App::Pattern::Type::Polar:
        case App::Pattern::Type::Circular:
            links.addLink(none, tr("Local Z axis"));
            break;
        default:
            links.addLink(none, tr("None"));
            break;
    }
    links.addSelectItem(tr("Select reference..."));
}

void TaskLinkArray::buildPatternWidgets()
{
    auto array = getArray();
    if (!array) {
        return;
    }
    exitSelection();

    // The inputs are other properties now: the old editors go
    for (auto child : patternBox->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        child->deleteLater();
        child->hide();
    }
    groupDirection1 = groupDirection2 = nullptr;
    direction1 = direction2 = nullptr;
    parameters = nullptr;

    auto directionProperties = [array](const char* suffix, bool polar) {
        auto name = [suffix](const char* base) {
            return std::string(base) + suffix;
        };
        PatternDirectionWidget::Properties props;
        props.reference = patternProperty<App::PropertyLinkSub>(
            array, polar ? "Axis" : name("Direction").c_str());
        props.reversed = patternProperty<App::PropertyBool>(array, name("Reversed").c_str());
        props.mode = patternProperty<App::PropertyEnumeration>(array, name("Mode").c_str());
        props.extent = patternProperty<App::PropertyQuantity>(
            array, polar ? "Angle" : name("Length").c_str());
        props.spacing = patternProperty<App::PropertyQuantity>(array, name("Offset").c_str());
        props.occurrences =
            patternProperty<App::PropertyIntegerConstraint>(array, name("Occurrences").c_str());
        props.spacings = patternProperty<App::PropertyFloatList>(array, name("Spacings").c_str());
        props.spacingPattern =
            patternProperty<App::PropertyFloatList>(array, name("SpacingPattern").c_str());
        return props;
    };
    auto connectDirection = [this](PatternDirectionWidget* widget) {
        connect(widget, &PatternDirectionWidget::referenceActivated, this, [this, widget]() {
            onReferenceActivated(widget->links(), widget->properties().reference);
        });
        connect(widget, &PatternDirectionWidget::changed, this, &TaskLinkArray::onChanged);
    };

    switch (array->getPatternType()) {
        case App::Pattern::Type::Linear: {
            groupDirection1 = new QGroupBox(patternBox);
            auto layout1 = new QVBoxLayout(groupDirection1);
            direction1 = new PatternDirectionWidget(PatternDirectionWidget::Kind::Linear,
                                                    groupDirection1);
            layout1->addWidget(direction1);
            patternLayout->addWidget(groupDirection1);
            fillReferenceCombo(direction1->links(), false);
            direction1->bind(directionProperties("", false));
            connectDirection(direction1);

            // Checked as long as the second direction has more than one
            // occurrence; unchecking it leaves one
            groupDirection2 = new QGroupBox(patternBox);
            groupDirection2->setCheckable(true);
            auto layout2 = new QVBoxLayout(groupDirection2);
            direction2 = new PatternDirectionWidget(PatternDirectionWidget::Kind::Linear,
                                                    groupDirection2);
            layout2->addWidget(direction2);
            patternLayout->addWidget(groupDirection2);
            fillReferenceCombo(direction2->links(), true);
            direction2->bind(directionProperties("2", false));
            connectDirection(direction2);
            connect(groupDirection2, &QGroupBox::toggled, this, &TaskLinkArray::onDirection2Toggled);
            break;
        }
        case App::Pattern::Type::Polar:
            direction1 = new PatternDirectionWidget(PatternDirectionWidget::Kind::Polar, patternBox);
            patternLayout->addWidget(direction1);
            fillReferenceCombo(direction1->links(), false);
            direction1->bind(directionProperties("", true));
            connectDirection(direction1);
            break;
        default: {
            auto kind = PatternParametersWidget::Kind::Point;
            if (array->getPatternType() == App::Pattern::Type::Circular) {
                kind = PatternParametersWidget::Kind::Circular;
            }
            else if (array->getPatternType() == App::Pattern::Type::Path) {
                kind = PatternParametersWidget::Kind::Path;
            }
            parameters = new PatternParametersWidget(kind, patternBox);
            patternLayout->addWidget(parameters);
            fillReferenceCombo(parameters->links(), false);
            parameters->bind(array);
            connect(parameters, &PatternParametersWidget::referenceActivated, this, [this]() {
                onReferenceActivated(parameters->links(), parameters->referenceProperty());
            });
            connect(parameters, &PatternParametersWidget::changed, this, &TaskLinkArray::onChanged);
            break;
        }
    }
    retranslate();
    updateUI();
    // Once the edit has started: the panel is built while it starts, before
    // the view it runs in is recorded
    QTimer::singleShot(0, this, [this]() {
        updateLabels();
    });
}

void TaskLinkArray::updateUI()
{
    auto array = getArray();
    if (!array) {
        return;
    }
    Base::StateLocker lock(blockUpdate);
    comboType->setCurrentIndex(static_cast<int>(array->getPatternType()));
    checkShowElement->setChecked(array->ShowElement.getValue());
    updateLinkedLabel();
    if (direction1) {
        direction1->updateUI();
    }
    if (direction2) {
        auto occurrences2 = patternProperty<App::PropertyInteger>(array, "Occurrences2");
        QSignalBlocker blocker(groupDirection2);
        groupDirection2->setChecked(occurrences2 && occurrences2->getValue() > 1);
        direction2->updateUI();
    }
    if (parameters) {
        parameters->updateUI();
    }
}

void TaskLinkArray::updateLinkedLabel()
{
    auto array = getArray();
    auto linked = array ? array->LinkedObject.getValue() : nullptr;
    labelLinked->setText(linked ? QString::fromUtf8(linked->Label.getValue()) : tr("None"));
}

void TaskLinkArray::exitSelection()
{
    picking = nullptr;
    pickingLinks = nullptr;
    if (pickingLinked) {
        pickingLinked = false;
        QSignalBlocker blocker(buttonLinked);
        buttonLinked->setChecked(false);
    }
}

void TaskLinkArray::onTypeActivated(int index)
{
    auto array = getArray();
    if (blockUpdate || !array || index == array->PatternType.getValue()) {
        return;
    }
    exitSelection();
    array->PatternType.setValue(index);
    buildPatternWidgets();
    recompute();
}

void TaskLinkArray::onReferenceActivated(ComboLinks& links, App::PropertyLinkSub* prop)
{
    if (!prop) {
        return;
    }
    try {
        if (links.isCurrentSelectItem()) {
            exitSelection();
            picking = prop;
            pickingLinks = &links;
            Selection().clearSelection();
            return;
        }
        exitSelection();
        prop->Paste(links.getCurrentLink());
        recompute();
        updateUI();
    }
    catch (Base::Exception& e) {
        QMessageBox::warning(this, tr("Error"), QApplication::translate("Exception", e.what()));
    }
}

void TaskLinkArray::onLinkedButtonToggled(bool on)
{
    if (blockUpdate) {
        return;
    }
    exitSelection();
    pickingLinked = on;
    if (on) {
        Selection().clearSelection();
    }
}

void TaskLinkArray::onShowElementToggled(bool on)
{
    auto array = getArray();
    if (blockUpdate || !array) {
        return;
    }
    array->ShowElement.setValue(on);
    recompute();
}

void TaskLinkArray::onDirection2Toggled(bool on)
{
    auto array = getArray();
    auto occurrences2 = array ? patternProperty<App::PropertyInteger>(array, "Occurrences2") : nullptr;
    if (blockUpdate || !occurrences2) {
        return;
    }
    if (on) {
        if (occurrences2->getValue() < 2) {
            occurrences2->setValue(2);
        }
    }
    else {
        occurrences2->setValue(1);
    }
    direction2->updateUI();
    onChanged();
}

void TaskLinkArray::onChanged()
{
    exitSelection();
    recompute();
}

void TaskLinkArray::recompute()
{
    // The document, not the array alone: new elements come with a new count
    if (auto array = getArray()) {
        array->getDocument()->recompute();
    }
    updateLabels();
}

void TaskLinkArray::updateLabels()
{
    auto array = getArray();
    auto vp = array ? freecad_cast<ViewProviderDocumentObject*>(
                          Application::Instance->getViewProvider(array))
                    : nullptr;
    ViewerContext* view = vp ? vp->getEditViewer() : nullptr;
    if (!view) {
        return;
    }
    // The labels start from the middle of the first element, in the array's
    // own frame, whatever the element's own placement is taken for
    Base::Vector3d origin;
    try {
        auto box = vp->getBoundingBox("0.", nullptr, /*transform=*/false);
        if (box.IsValid()) {
            origin = box.GetCenter();
        }
    }
    catch (const Base::Exception&) {
        // from the array's origin, then
    }
    App::Pattern::Context context;
    context.placement = array->Placement.getValue();
    context.defaultReferences = true;
    const Base::Matrix4D& toWorld = vp->getDocument()->getEditingTransform();

    auto show = [&](PatternDirectionWidget* widget, bool second) {
        if (!widget) {
            return;
        }
        PatternLabelFrame frame;
        auto kind = array->getPatternType() == App::Pattern::Type::Polar
            ? PatternDirectionWidget::Kind::Polar
            : PatternDirectionWidget::Kind::Linear;
        if (patternLabelFrame(kind, *array, context, second, toWorld, origin, frame)) {
            widget->showLabels(view, frame);
        }
        else {
            widget->clearLabels();
        }
    };
    show(direction1, false);
    show(direction2, true);
}

void TaskLinkArray::onSelectionChanged(const SelectionChanges& msg)
{
    auto array = getArray();
    if (!array || msg.Type != SelectionChanges::AddSelection || (!picking && !pickingLinked)) {
        return;
    }
    try {
        if (pickingLinked) {
            App::SubObjectT sel(msg.pDocName, msg.pObjectName, msg.pSubName);
            auto linked = sel.getSubObject();
            if (!linked || linked == array) {
                return;
            }
            exitSelection();
            // setLink() refuses a cycle, as an object holding the array
            array->setLink(-1, linked);
            updateLinkedLabel();
            recompute();
            return;
        }

        App::DocumentObject* obj = nullptr;
        std::string sub;
        if (!getReference(array, msg, obj, sub)) {
            return;
        }
        std::vector<std::string> subs {sub};
        // A path is picked edge by edge, of one object
        bool isPath = parameters && parameters->getKind() == PatternParametersWidget::Kind::Path;
        if (isPath && picking->getValue() == obj && !sub.empty()) {
            subs = picking->getSubValues();
            if (std::find(subs.begin(), subs.end(), sub) == subs.end()) {
                subs.push_back(sub);
            }
        }
        picking->setValue(obj, subs);
        if (!isPath) {
            exitSelection();
        }
        recompute();
        updateUI();
    }
    catch (Base::Exception& e) {
        exitSelection();
        QMessageBox::warning(this, tr("Error"), QApplication::translate("Exception", e.what()));
    }
}

void TaskLinkArray::apply()
{
    auto array = getArray();
    if (!array) {
        return;
    }
    FCMD_OBJ_CMD(array, "PatternType = '" << array->PatternType.getValueAsString() << "'");
    auto linked = array->LinkedObject.getValue();
    FCMD_OBJ_CMD(array,
                 "LinkedObject = " << (linked ? Command::getObjectCmd(linked) : std::string("None")));
    FCMD_OBJ_CMD(array, "ShowElement = " << (array->ShowElement.getValue() ? "True" : "False"));
    if (direction1) {
        direction1->apply(array);
    }
    if (direction2) {
        if (groupDirection2->isChecked()) {
            direction2->apply(array);
        }
        else {
            FCMD_OBJ_CMD(array, "Occurrences2 = 1");
        }
    }
    if (parameters) {
        parameters->apply(array);
    }
}

// ----------------------------------------------------------------------------

TaskDlgLinkArray::TaskDlgLinkArray(ViewProviderLinkArray* vp)
{
    panel = new TaskLinkArray(vp);
    Content.push_back(panel);
    if (auto obj = vp->getObject()) {
        setDocumentName(obj->getDocument()->getName());
    }
    if (!App::GetApplication().getActiveTransaction()) {
        Command::openCommand(QT_TRANSLATE_NOOP("Command", "Edit link array"));
    }
}

TaskDlgLinkArray::~TaskDlgLinkArray() = default;

bool TaskDlgLinkArray::accept()
{
    panel->exitSelection();
    panel->apply();
    Command::doCommand(Command::Gui, "Gui.getDocument('%s').resetEdit()", getDocumentName().c_str());
    Command::doCommand(Command::Doc, "App.getDocument('%s').recompute()", getDocumentName().c_str());
    Command::commitCommand();
    return true;
}

bool TaskDlgLinkArray::reject()
{
    panel->exitSelection();
    Command::abortCommand();
    Command::doCommand(Command::Gui, "Gui.getDocument('%s').resetEdit()", getDocumentName().c_str());
    // What the abort brought back is not made yet
    if (auto doc = App::GetApplication().getDocument(getDocumentName().c_str())) {
        doc->recompute();
    }
    return true;
}

#include "moc_TaskLinkArray.cpp"
