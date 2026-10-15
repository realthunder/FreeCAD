// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Sayantan Deb <sayantandebin[at]gmail.com>           *
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

#include "Gizmo.h"

#include <cmath>
#include <utility>
#include <numbers>
#include <QApplication>

#include <QCursor>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolTip>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoHandleEventAction.h>
#include <Inventor/draggers/SoDragger.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoPickStyle.h>
#include <Inventor/nodes/SoScale.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSphere.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoTranslation.h>
#include <algorithm>

#include <App/Application.h>
#include <Base/Converter.h>
#include <Base/Parameter.h>
#include <Base/Precision.h>
#include <Base/ServiceProvider.h>
#include <Base/Tools.h>
#include <Gui/Document.h>

#include <Gui/MiscParams.h>
#include <Gui/Inventor/Draggers/GizmoStyleParameters.h>
#include <Gui/Inventor/So3DAnnotation.h>
#include <Gui/Inventor/SoToggleSwitch.h>
#include <Gui/ViewProviderDragger.h>
#include <Gui/QuantitySpinBox.h>
#include <Gui/Utilities.h>
#include <Gui/ViewerContext.h>

#include "SoLinearDragger.h"
#include "SoLinearDraggerGeometry.h"
#include "SoRotationDragger.h"
#include "SoRotationDraggerGeometry.h"

using namespace Gui;

namespace
{
enum class DefaultDragBehavior
{
    Coarse = 0,
    Fine = 1,
};


int getCoarseLinearSnapMultiplier()
{
    int multiplier = static_cast<int>(
        MiscParams::getCoarseLinearSnapMultiplier()
    );
    return std::max(1, multiplier);
}

int getCoarseRotationSnapMultiplier()
{
    int multiplier = static_cast<int>(
        MiscParams::getCoarseRotationSnapMultiplier()
    );
    return std::max(1, multiplier);
}

double snapToStep(double value, double step)
{
    if (step <= 0.0) {
        return value;
    }

    return std::round(value / step) * step;
}

double getRotationPeriod(double multFactor)
{
    const double factor = std::abs(multFactor);
    if (factor <= Base::Precision::Confusion()) {
        return 360.0;
    }

    return 2.0 * std::numbers::pi / factor;
}

double getClosestEquivalentAngle(double angle, double reference, double period)
{
    if (period <= Base::Precision::Confusion()) {
        return angle;
    }

    return angle + std::round((reference - angle) / period) * period;
}

double clampDragAngle(double value, double min, double max, double period)
{
    if (max - min >= period - Base::Precision::Confusion()) {
        return std::clamp(value, min, max);
    }

    return Base::clampAngle(value, min, max, Base::Precision::Confusion());
}
}  // namespace

void Gizmo::setDraggerPlacement(const Base::Vector3d& pos, const Base::Vector3d& dir)
{
    setDraggerPlacement(Base::convertTo<SbVec3f>(pos), Base::convertTo<SbVec3f>(dir));
}

bool Gizmo::isDelayedUpdateEnabled()
{
    return MiscParams::getDelayedGizmoUpdate();
}

double Gizmo::getMultFactor()
{
    return multFactor;
}

double Gizmo::getAddFactor()
{
    return addFactor;
}

bool Gizmo::getVisibility()
{
    return visible;
}

void Gizmo::otherDragEvent(void* data, SoDragger* dragger)
{
    // While the dragger holds the pointer, the events that are no motion and
    // no release of its button: Esc, or another mouse button, cancels
    auto self = static_cast<Gizmo*>(data);
    const SoEvent* event = dragger->getEvent();
    if (!self || !event || !self->dragging) {
        return;
    }
    bool cancel = false;
    if (event->isOfType(SoKeyboardEvent::getClassTypeId())) {
        const auto* key = static_cast<const SoKeyboardEvent*>(event);
        cancel = key->getKey() == SoKeyboardEvent::ESCAPE;
    }
    else if (event->isOfType(SoMouseButtonEvent::getClassTypeId())) {
        const auto* button = static_cast<const SoMouseButtonEvent*>(event);
        cancel = button->getButton() != SoMouseButtonEvent::BUTTON1;
    }
    if (!cancel) {
        return;
    }
    // Taken: Escape would leave the edit, another button open a menu
    if (SoHandleEventAction* action = dragger->getHandleEventAction()) {
        action->setHandled();
    }
    // On the press; its release is only taken
    if (static_cast<const SoButtonEvent*>(event)->getState() == SoButtonEvent::DOWN) {
        self->cancelDrag();
    }
}

SoNode* Gizmo::getExtraNode()
{
    return eventNode;
}

void Gizmo::setMenu(MenuCallback callback, const QString& toolTip)
{
    menuCallback = std::move(callback);
    menuToolTip = toolTip;
}

void Gizmo::buildEventNode(SoNode* own)
{
    releaseEventNode();
    ownNode = own;
    eventNode = new SoEventCallback;
    eventNode->ref();
    eventNode->addEventCallback(SoMouseButtonEvent::getClassTypeId(), eventNodeCallback, this);
    eventNode->addEventCallback(SoLocation2Event::getClassTypeId(), eventNodeCallback, this);
}

void Gizmo::releaseEventNode()
{
    if (hovered) {
        QToolTip::hideText();
    }
    hovered = false;
    menuPressed = false;
    ownNode = nullptr;
    if (eventNode) {
        eventNode->unref();
        eventNode = nullptr;
    }
}

void Gizmo::eventNodeCallback(void* data, SoEventCallback* cb)
{
    auto self = static_cast<Gizmo*>(data);
    if (!self->ownNode || !self->menuCallback) {
        return;
    }
    const SoPickedPoint* picked = cb->getPickedPoint();
    const bool over = picked && picked->getPath()->containsNode(self->ownNode);
    if (cb->getEvent()->isOfType(SoLocation2Event::getClassTypeId())) {
        // The hint, while the pointer is over the handle (entry 135)
        if (over != self->hovered) {
            self->hovered = over;
            if (over && !self->menuToolTip.isEmpty()) {
                QToolTip::showText(QCursor::pos(), self->menuToolTip);
            }
            else if (!over) {
                QToolTip::hideText();
            }
        }
        return;
    }
    const auto* event = static_cast<const SoMouseButtonEvent*>(cb->getEvent());
    if (SoMouseButtonEvent::isButtonPressEvent(event, SoMouseButtonEvent::BUTTON2)) {
        self->menuPressed = over;
        if (over) {
            QToolTip::hideText();
            cb->setHandled();
            // the release comes back here, not to the view
            cb->grabEvents();
        }
        return;
    }
    if (SoMouseButtonEvent::isButtonReleaseEvent(event, SoMouseButtonEvent::BUTTON2)) {
        const bool pressed = self->menuPressed;
        self->menuPressed = false;
        if (pressed) {
            cb->releaseEvents();
            cb->setHandled();
            if (over) {
                // After the event: the menu runs a loop of its own, and what
                // it does may take this handle away
                auto callback = self->menuCallback;
                QTimer::singleShot(0, [callback]() {
                    callback();
                });
            }
        }
    }
}

LinearGizmo::LinearGizmo(QuantitySpinBox* property)
{
    this->property = property;
}

LinearGizmo::~LinearGizmo()
{
    QuantitySpinBox::disconnect(quantityChangedConnection);
    QuantitySpinBox::disconnect(formulaDialogConnection);
    releaseEventNode();
}

SoInteractionKit* LinearGizmo::initDragger()
{
    draggerContainer = new SoLinearDraggerContainer;
    draggerContainer->color.setValue(1, 0, 0);
    dragger = draggerContainer->getDragger();

    dragger->addStartCallback(
        [](void* data, SoDragger*) { static_cast<LinearGizmo*>(data)->draggingStarted(); },
        this
    );
    dragger->addFinishCallback(
        [](void* data, SoDragger*) { static_cast<LinearGizmo*>(data)->draggingFinished(); },
        this
    );
    dragger->addMotionCallback(
        [](void* data, SoDragger*) { static_cast<LinearGizmo*>(data)->draggingContinued(); },
        this
    );
    dragger->addOtherEventCallback(&Gizmo::otherDragEvent, static_cast<Gizmo*>(this));

    dragger->labelVisible = false;

    if (draggerStyle == LinearDraggerStyle::Sphere) {
        dragger->setPart("arrow", new SoSphereGeometry);
    }
    else {
        dragger->instantiateBaseGeometry();

        auto arrow = SO_GET_PART(dragger, "arrow", SoArrowGeometry);
        arrow->cylinderHeight = 3.5;
        arrow->cylinderRadius = 0.2;
    }

    updateColorTheme();

    setProperty(property);

    buildEventNode(draggerContainer);

    return draggerContainer;
}

void LinearGizmo::uninitDragger()
{
    dragger = nullptr;
    draggerContainer = nullptr;
    releaseEventNode();
}

void LinearGizmo::setColor(const std::optional<Base::Color>& color)
{
    ownColor = color;
    if (dragger) {
        updateColorTheme();
    }
}

void LinearGizmo::updateColorTheme()
{
    auto* styleParameterManager = Base::provideService<Gui::StyleParameters::ParameterManager>();
    Base::Color baseColor = styleParameterManager->resolve(StyleParameters::LinearGizmoBaseColor);
    Base::Color activeColor = styleParameterManager->resolve(StyleParameters::LinearGizmoActiveColor);

    // a handle of its own colour (a customised edge's) keeps it
    if (ownColor) {
        baseColor = *ownColor;
    }
    // the active handle wears the colour of one being dragged
    dragger->color = (highlighted ? activeColor : baseColor).asValue<SbColor>();
    dragger->activeColor = activeColor.asValue<SbColor>();

    auto baseGeom = SO_GET_PART(dragger, "baseGeom", SoArrowBase);
    if (!baseGeom) {
        return;
    }
    Base::Color baseGeomColor = styleParameterManager->resolve(
        StyleParameters::DimensionVisualizerColor
    );
    baseGeom->color = baseGeomColor.asValue<SbColor>();
}

void LinearGizmo::setHighlighted(bool highlighted)
{
    if (this->highlighted == highlighted) {
        return;
    }
    this->highlighted = highlighted;
    if (dragger) {
        updateColorTheme();
    }
}

void LinearGizmo::setFocusBoxOnRelease(bool focus)
{
    focusBoxOnRelease = focus;
}

void LinearGizmo::setReleaseCallback(ReleaseCallback callback)
{
    releaseCallback = std::move(callback);
}

GizmoPlacement LinearGizmo::getDraggerPlacement()
{
    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    return {draggerContainer->translation.getValue(), draggerContainer->getPointerDirection()};
}

void LinearGizmo::setDraggerPlacement(const SbVec3f& pos, const SbVec3f& dir)
{
    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    draggerContainer->translation = pos;
    draggerContainer->setPointerDirection(dir);
}

void LinearGizmo::reverseDir()
{
    auto dir = getDraggerContainer()->getPointerDirection();
    getDraggerContainer()->setPointerDirection(dir * -1);
}

double LinearGizmo::getDragLength()
{
    double dragLength = dragger->translationIncrementCount.getValue()
        * dragger->translationIncrement.getValue();

    return (dragLength - addFactor) / multFactor;
}

void LinearGizmo::setDragLength(double dragLength)
{
    dragLength = dragLength * multFactor + addFactor;
    dragger->translation = {0, static_cast<float>(dragLength), 0};
}

void LinearGizmo::setGeometryScale(float scale)
{
    dragger->geometryScale = SbVec3f(scale, scale, scale);
    // Scales the dragger increment in exponents of 10 based on the zoom level (scale)
    constexpr float base = 10.0F;
    dragger->translationIncrement = multFactor * std::pow(base, std::floor(std::log10(scale)));
}

SoLinearDraggerContainer* LinearGizmo::getDraggerContainer()
{
    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    return draggerContainer;
}

void LinearGizmo::setProperty(QuantitySpinBox* property)
{
    QuantitySpinBox::disconnect(quantityChangedConnection);
    QuantitySpinBox::disconnect(formulaDialogConnection);

    this->property = property;
    quantityChangedConnection = QuantitySpinBox::connect(
        property,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        [this](double value) { setDragLength(value); }
    );
    formulaDialogConnection
        = QuantitySpinBox::connect(property, &Gui::QuantitySpinBox::showFormulaDialog, [this](bool) {
              // This will set the visibility of the actual geometry to true or false
              // based on if an expression is bound and the externally set visibility
              setVisibility(visible);
          });

    // Updates the gizmo state based on the new property
    setDragLength(property->rawValue());
    setVisibility(visible);
}

void LinearGizmo::setMultFactor(const double val)
{
    multFactor = val;
    setDragLength(property->value().getValue());
}

void LinearGizmo::setAddFactor(const double val)
{
    addFactor = val;
    setDragLength(property->value().getValue());
}

void LinearGizmo::setDraggerStyle(LinearDraggerStyle style)
{
    draggerStyle = style;
}

void LinearGizmo::setClickCallback(ClickCallback callback)
{
    clickCallback = std::move(callback);
}

void LinearGizmo::setVisibility(bool visible)
{
    this->visible = visible;
    getDraggerContainer()->visible = visible && !property->hasExpression();
}

void LinearGizmo::draggingStarted()
{
    initialValue = property->value().getValue();
    hasDragged = false;
    dragging = true;
    dragCancelled = false;
    notifyDrag(DragEvent::Started);
    dragger->translationIncrementCount.setValue(0);

    if (isDelayedUpdateEnabled()) {
        property->blockSignals(true);
    }
}

void LinearGizmo::draggingFinished()
{
    dragging = false;
    if (dragCancelled) {
        // put back at the cancel already; nothing to commit or activate
        dragCancelled = false;
        if (isDelayedUpdateEnabled()) {
            property->blockSignals(false);
        }
        return;
    }
    if (isDelayedUpdateEnabled()) {
        property->blockSignals(false);
        property->valueChanged(property->value().getValue());
    }

    if (!hasDragged && clickCallback) {
        clickCallback();
    }

    if (focusBoxOnRelease) {
        property->setFocus();
        property->selectAll();
    }
    // Last: it may make the handle the active one, which takes the focus
    if (releaseCallback) {
        releaseCallback(hasDragged);
    }
    notifyDrag(DragEvent::Finished);
}

void LinearGizmo::cancelDrag()
{
    if (!dragging || dragCancelled) {
        return;
    }
    dragCancelled = true;
    // The box back first, then the owner told: it rolls the drag's step back
    // and puts every handle of the value where the box says -- the others a
    // default value drives too (entry 145), which the blocked box does not
    // tell
    {
        const QSignalBlocker blocker(property);
        property->setValue(initialValue);
    }
    setDragLength(initialValue);
    notifyDrag(DragEvent::Cancelled);
}

void LinearGizmo::draggingContinued()
{
    if (dragCancelled) {
        return;
    }
    hasDragged = true;
    double value = initialValue + getDragLength();

    auto fineModifier = GizmoContainer::getFineSnapModifier();
    auto modifiers = QApplication::queryKeyboardModifiers();
    bool fineModifierPressed = modifiers == fineModifier;
    bool coarseByDefault = GizmoContainer::isCoarseByDefault();
    bool useCoarseSnap = coarseByDefault != fineModifierPressed;

    if (GizmoContainer::isCoarseSnapEnabled() && useCoarseSnap) {
        double baseStep = std::abs(dragger->translationIncrement.getValue() / multFactor);
        value = snapToStep(value, baseStep * getCoarseLinearSnapMultiplier());
    }

    value = std::clamp(value, property->minimum(), property->maximum());

    property->setValue(value);
    setDragLength(value);
}


RotationGizmo::RotationGizmo(QuantitySpinBox* property)
{
    this->property = property;
}

RotationGizmo::~RotationGizmo()
{
    // Lets go of its spin box, as a linear one does: a box may outlive a
    // gizmo taken out early
    QuantitySpinBox::disconnect(quantityChangedConnection);
    QuantitySpinBox::disconnect(formulaDialogConnection);
    releaseEventNode();
    translationSensor.detach();
    translationSensor.setData(nullptr);
    translationSensor.setFunction(nullptr);
}

SoInteractionKit* RotationGizmo::initDragger()
{
    multFactor = std::numbers::pi_v<float> / 180.0;
    draggerContainer = new SoRotationDraggerContainer;

    draggerContainer->color.setValue(1, 0, 0);
    dragger = draggerContainer->getDragger();
    dragger->rotationIncrement = std::numbers::pi / 180.0;

    auto rotator = new SoRotatorGeometry;
    rotator->arcAngle = std::numbers::pi_v<float> / 6.0f;
    rotator->arcRadius = 16.0f;
    dragger->setPart("rotator", rotator);

    dragger->addStartCallback(
        [](void* data, SoDragger*) { static_cast<RotationGizmo*>(data)->draggingStarted(); },
        this
    );
    dragger->addFinishCallback(
        [](void* data, SoDragger*) { static_cast<RotationGizmo*>(data)->draggingFinished(); },
        this
    );
    dragger->addMotionCallback(
        [](void* data, SoDragger*) { static_cast<RotationGizmo*>(data)->draggingContinued(); },
        this
    );
    dragger->addOtherEventCallback(&Gizmo::otherDragEvent, static_cast<Gizmo*>(this));

    setProperty(property);

    updateColorTheme();

    buildEventNode(draggerContainer);

    return draggerContainer;
}

void RotationGizmo::uninitDragger()
{
    dragger = nullptr;
    draggerContainer = nullptr;
    releaseEventNode();

    translationSensor.detach();
    translationSensor.setData(nullptr);
    translationSensor.setFunction(nullptr);
}

void RotationGizmo::updateColorTheme()
{
    auto* styleParameterManager = Base::provideService<Gui::StyleParameters::ParameterManager>();
    Base::Color baseColor = styleParameterManager->resolve(StyleParameters::RotationGizmoBaseColor);
    Base::Color activeColor = styleParameterManager->resolve(StyleParameters::RotationGizmoActiveColor);

    // a handle of its own colour (a customised edge's) keeps it, and the
    // active one wears the colour of one being dragged
    if (ownColor) {
        baseColor = *ownColor;
    }
    dragger->color = (highlighted ? activeColor : baseColor).asValue<SbColor>();
    dragger->activeColor = activeColor.asValue<SbColor>();
}

void RotationGizmo::setHighlighted(bool highlighted)
{
    if (this->highlighted == highlighted) {
        return;
    }
    this->highlighted = highlighted;
    if (dragger) {
        updateColorTheme();
    }
}

void RotationGizmo::setColor(const std::optional<Base::Color>& color)
{
    ownColor = color;
    if (dragger) {
        updateColorTheme();
    }
}

void RotationGizmo::setReleaseCallback(ReleaseCallback callback)
{
    releaseCallback = std::move(callback);
}

void RotationGizmo::setFocusBoxOnRelease(bool focus)
{
    focusBoxOnRelease = focus;
}

GizmoPlacement RotationGizmo::getDraggerPlacement()
{
    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    return {draggerContainer->translation.getValue(), draggerContainer->getPointerDirection()};
}

void RotationGizmo::setDraggerPlacement(const SbVec3f& pos, const SbVec3f& dir)
{
    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    draggerContainer->translation = pos;
    draggerContainer->setPointerDirection(dir);
}

void RotationGizmo::reverseDir()
{
    auto dir = getDraggerContainer()->getPointerDirection();
    getDraggerContainer()->setPointerDirection(dir * -1);
}

void RotationGizmo::placeOverLinearGizmo(LinearGizmo* gizmo)
{
    linearGizmo = gizmo;

    GizmoPlacement placement = gizmo->getDraggerPlacement();

    draggerContainer->translation = Base::convertTo<SbVec3f>(placement.pos);
    draggerContainer->setPointerDirection(placement.dir);

    translationSensor.setData(this);
    translationSensor.setFunction(translationSensorCB);
    translationSensor.setPriority(0);
    SoSFVec3f& translation = gizmo->getDraggerContainer()->getDragger()->translation;
    translationSensor.attach(&translation);
    translation.touch();

    automaticOrientation = true;
}

void RotationGizmo::translationSensorCB(void* data, SoSensor* sensor)
{
    auto sudoThis = static_cast<RotationGizmo*>(data);
    auto translationSensor = static_cast<SoFieldSensor*>(sensor);

    GizmoPlacement placement = sudoThis->linearGizmo->getDraggerPlacement();

    SbVec3f translation = static_cast<SoSFVec3f*>(translationSensor->getAttachedField())->getValue();
    float yComp = translation.getValue()[1];
    SbVec3f dir = placement.dir;
    dir.normalize();
    sudoThis->draggerContainer->translation = placement.pos + dir * (yComp + sudoThis->sepDistance);
}

void RotationGizmo::placeBelowLinearGizmo(LinearGizmo* gizmo)
{
    linearGizmo = gizmo;

    GizmoPlacement placement = gizmo->getDraggerPlacement();

    draggerContainer->translation = Base::convertTo<SbVec3f>(placement.pos);
    draggerContainer->setPointerDirection(-placement.dir);

    translationSensor.setData(this);
    translationSensor.setFunction(translationSensorCB);
    translationSensor.setPriority(0);
    SoSFVec3f& translation = gizmo->getDraggerContainer()->getDragger()->translation;
    translationSensor.attach(&translation);
    translation.touch();
}

double RotationGizmo::getRotAngle()
{
    double rotAngle = dragger->rotationIncrementCount.getValue()
        * dragger->rotationIncrement.getValue();

    return (rotAngle - addFactor) / multFactor;
}

void RotationGizmo::setRotAngle(double angle)
{
    angle = multFactor * angle + addFactor;
    dragger->rotation = SbRotation({0, 0, 1.0f}, static_cast<float>(angle));
}

void RotationGizmo::setGeometryScale(float scale)
{
    dragger->geometryScale = SbVec3f(scale, scale, scale);
}

SoRotationDraggerContainer* RotationGizmo::getDraggerContainer()
{
    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    return draggerContainer;
}

void RotationGizmo::draggingStarted()
{
    initialValue = property->value().getValue();
    lastDragOffset = 0.0;
    hasDragged = false;
    dragging = true;
    dragCancelled = false;
    notifyDrag(DragEvent::Started);
    dragger->rotationIncrementCount.setValue(0);

    if (isDelayedUpdateEnabled()) {
        property->blockSignals(true);
    }
}

void RotationGizmo::draggingFinished()
{
    dragging = false;
    if (dragCancelled) {
        dragCancelled = false;
        if (isDelayedUpdateEnabled()) {
            property->blockSignals(false);
        }
        return;
    }
    if (isDelayedUpdateEnabled()) {
        property->blockSignals(false);
        property->valueChanged(property->value().getValue());
    }

    if (!hasDragged && clickCallback) {
        clickCallback();
    }

    if (focusBoxOnRelease) {
        property->setFocus();
        property->selectAll();
    }
    // Last: it may make the handle the active one, which takes the focus
    if (releaseCallback) {
        releaseCallback(hasDragged);
    }
    notifyDrag(DragEvent::Finished);
}

void RotationGizmo::cancelDrag()
{
    if (!dragging || dragCancelled) {
        return;
    }
    dragCancelled = true;
    // the box back first, then the owner told (as a linear handle's)
    {
        const QSignalBlocker blocker(property);
        property->setValue(initialValue);
    }
    setRotAngle(initialValue);
    notifyDrag(DragEvent::Cancelled);
}

void RotationGizmo::draggingContinued()
{
    if (dragCancelled) {
        return;
    }
    hasDragged = true;
    const double period = getRotationPeriod(multFactor);
    double dragOffset = getClosestEquivalentAngle(getRotAngle(), lastDragOffset, period);
    lastDragOffset = dragOffset;
    double value = initialValue + dragOffset;

    auto fineModifier = GizmoContainer::getFineSnapModifier();
    auto modifiers = QApplication::queryKeyboardModifiers();
    bool fineModifierPressed = modifiers == fineModifier;
    bool coarseByDefault = GizmoContainer::isCoarseByDefault();
    bool useCoarseSnap = coarseByDefault != fineModifierPressed;

    if (GizmoContainer::isCoarseSnapEnabled() && useCoarseSnap) {
        value = snapToStep(value, getCoarseRotationSnapMultiplier());
    }

    value = clampDragAngle(value, property->minimum(), property->maximum(), period);

    property->setValue(value);
    setRotAngle(value);
}

void RotationGizmo::orientAlongCamera(SoCamera* camera)
{
    if (!automaticOrientation) {
        return;
    }

    SbVec3f cameraDir {0, 0, 1};
    camera->orientation.getValue().multVec(cameraDir, cameraDir);
    SbVec3f pointerDir = getDraggerContainer()->getPointerDirection();

    pointerDir.normalize();
    auto proj = cameraDir - cameraDir.dot(pointerDir) * pointerDir;
    if (proj.equals(SbVec3f {0, 0, 0}, 0.001)) {
        return;
    }

    assert(draggerContainer && "Forgot to call GizmoContainer::initGizmos?");
    draggerContainer->setArcNormalDirection(proj);
}

void RotationGizmo::setProperty(QuantitySpinBox* property)
{
    QuantitySpinBox::disconnect(quantityChangedConnection);
    QuantitySpinBox::disconnect(formulaDialogConnection);

    this->property = property;
    quantityChangedConnection = QuantitySpinBox::connect(
        property,
        qOverload<double>(&Gui::QuantitySpinBox::valueChanged),
        [this](double value) { setRotAngle(value); }
    );
    formulaDialogConnection
        = QuantitySpinBox::connect(property, &Gui::QuantitySpinBox::showFormulaDialog, [this](bool) {
              // This will set the visibility of the actual geometry to true or false
              // based on if an expression is bound and the externally set visibility
              setVisibility(visible);
          });

    // Updates the gizmo state based on the new property
    setRotAngle(property->rawValue());
    setVisibility(visible);
}

void RotationGizmo::setMultFactor(const double val)
{
    multFactor = val;
    setRotAngle(property->value().getValue());
}

void RotationGizmo::setAddFactor(const double val)
{
    addFactor = val;
    setRotAngle(property->value().getValue());
}

void RotationGizmo::setClickCallback(ClickCallback callback)
{
    clickCallback = std::move(callback);
}

void RotationGizmo::setVisibility(bool visible)
{
    this->visible = visible;
    getDraggerContainer()->visible = visible && !property->hasExpression();
}

DirectedRotationGizmo::DirectedRotationGizmo(QuantitySpinBox* property)
    : RotationGizmo(property)
{}

SoInteractionKit* DirectedRotationGizmo::initDragger()
{
    SoInteractionKit* ret = inherited::initDragger();

    auto dragger = getDraggerContainer()->getDragger();
    auto rotator = new SoRotatorGeometry2;
    rotator->arcAngle = std::numbers::pi_v<float> / 6.0f;
    rotator->arcRadius = 16.0f;
    rotator->rightArrowVisible = false;
    dragger->setPart("rotator", rotator);

    updateColorTheme();

    return ret;
}

void DirectedRotationGizmo::flipArrow()
{
    auto dragger = getDraggerContainer()->getDragger();
    auto rotator = SO_GET_PART(dragger, "rotator", SoRotatorGeometry2);

    rotator->toggleArrowVisibility();
}


RadialGizmo::RadialGizmo(QuantitySpinBox* property)
    : RotationGizmo(property)
{}

SoInteractionKit* RadialGizmo::initDragger()
{
    SoInteractionKit* ret = inherited::initDragger();

    auto dragger = getDraggerContainer()->getDragger();
    auto rotator = new SoRotatorArrow;
    rotator->geometryScale.connectFrom(&dragger->geometryScale);
    dragger->setPart("rotator", rotator);

    dragger->instantiateBaseGeometry();

    updateColorTheme();

    return ret;
}

void RadialGizmo::setRadius(float radius)
{
    auto dragger = getDraggerContainer()->getDragger();
    auto rotator = SO_GET_PART(dragger, "rotator", SoRotatorArrow);
    auto baseGeom = SO_GET_PART(dragger, "baseGeom", SoRotatorBase);

    rotator->radius = baseGeom->arcRadius = radius;
}

void RadialGizmo::flipArrow()
{
    auto dragger = getDraggerContainer()->getDragger();
    auto rotator = SO_GET_PART(dragger, "rotator", SoRotatorArrow);

    rotator->flipArrow();
}

void RadialGizmo::updateColorTheme()
{
    auto dragger = getDraggerContainer()->getDragger();

    auto* styleParameterManager = Base::provideService<Gui::StyleParameters::ParameterManager>();
    Base::Color baseColor = styleParameterManager->resolve(StyleParameters::RotationGizmoBaseColor);
    Base::Color activeColor = styleParameterManager->resolve(StyleParameters::RotationGizmoActiveColor);

    dragger->color = baseColor.asValue<SbColor>();
    dragger->activeColor = activeColor.asValue<SbColor>();

    auto baseGeom = SO_GET_PART(dragger, "baseGeom", SoRotatorBase);
    Base::Color baseGeomColor = styleParameterManager->resolve(
        StyleParameters::DimensionVisualizerColor
    );
    baseGeom->color = baseGeomColor.asValue<SbColor>();
}

SO_KIT_SOURCE(GizmoContainer)

void GizmoContainer::initClass()
{
    SO_KIT_INIT_CLASS(GizmoContainer, SoBaseKit, "BaseKit");
}

GizmoContainer::GizmoContainer()
    : viewProvider(nullptr)
{
    SO_KIT_CONSTRUCTOR(GizmoContainer);

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)
    this->ref();
#endif

    FC_ADD_CATALOG_ENTRY(annotation, So3DAnnotation, this);
    FC_ADD_CATALOG_ENTRY(pickStyle, SoPickStyle, annotation);
    FC_ADD_CATALOG_ENTRY(toggleSwitch, SoToggleSwitch, annotation);
    FC_ADD_CATALOG_ENTRY(geometry, SoSeparator, toggleSwitch);

    SO_KIT_INIT_INSTANCE();

    SO_KIT_ADD_FIELD(visible, (1));

    auto pickStyle = SO_GET_ANY_PART(this, "pickStyle", SoPickStyle);
    pickStyle->style = SoPickStyle::SHAPE_ON_TOP;

    auto toggleSwitch = SO_GET_ANY_PART(this, "toggleSwitch", SoToggleSwitch);
    toggleSwitch->on.connectFrom(&visible);

    setPart("geometry", new SoSeparator);

    cameraSensor.setFunction(&GizmoContainer::cameraChangeCallback);
    cameraSensor.setData(this);

    cameraPositionSensor.setData(this);
    cameraPositionSensor.setFunction(cameraPositionChangeCallback);
}

GizmoContainer::~GizmoContainer()
{
    cameraSensor.setData(nullptr);
    cameraSensor.detach();

    cameraPositionSensor.setData(nullptr);
    cameraPositionSensor.detach();

    uninitGizmos();

    if (!viewProvider.expired()) {
        viewProvider->setGizmoContainer(nullptr);
    }
}

void GizmoContainer::initGizmos()
{
    auto geometry = SO_GET_ANY_PART(this, "geometry", SoSeparator);
    gizmoNodes.clear();
    for (auto gizmo : gizmos) {
        SoNode* dragger = gizmo->initDragger();
        geometry->addChild(dragger);
        SoNode* extra = gizmo->getExtraNode();
        if (extra) {
            geometry->addChild(extra);
        }
        gizmoNodes.emplace_back(dragger, extra);
    }
}

void GizmoContainer::uninitGizmos()
{
    for (auto gizmo : gizmos) {
        gizmo->uninitDragger();
        delete gizmo;
    }
    gizmos.clear();
    gizmoNodes.clear();
}

void GizmoContainer::appendGizmo(Gizmo* gizmo)
{
    if (!gizmo || std::ranges::find(gizmos, gizmo) != gizmos.end()) {
        return;
    }
    gizmos.push_back(gizmo);
    auto geometry = SO_GET_ANY_PART(this, "geometry", SoSeparator);
    SoNode* dragger = gizmo->initDragger();
    geometry->addChild(dragger);
    SoNode* extra = gizmo->getExtraNode();
    if (extra) {
        geometry->addChild(extra);
    }
    gizmoNodes.emplace_back(dragger, extra);
    // its size and turn, as the others have them
    calculateScaleAndOrientation();
}

void GizmoContainer::removeGizmo(Gizmo* gizmo)
{
    auto it = std::ranges::find(gizmos, gizmo);
    if (it == gizmos.end()) {
        return;
    }
    const auto index = std::distance(gizmos.begin(), it);
    auto geometry = SO_GET_ANY_PART(this, "geometry", SoSeparator);
    if (index < static_cast<decltype(index)>(gizmoNodes.size())) {
        auto [dragger, extra] = gizmoNodes[index];
        for (SoNode* node : {dragger, extra}) {
            if (!node) {
                continue;
            }
            int child = geometry->findChild(node);
            if (child >= 0) {
                geometry->removeChild(child);
            }
        }
        gizmoNodes.erase(gizmoNodes.begin() + index);
    }
    gizmos.erase(it);
    gizmo->uninitDragger();
    delete gizmo;
}

void GizmoContainer::addGizmos(std::initializer_list<Gui::Gizmo*> gizmos)
{
    assert(this->gizmos.size() == 0 && "Already called GizmoContainer::addGizmos?");

    for (auto gizmo : gizmos) {
        addGizmo(gizmo);
    }
    initGizmos();
}

void GizmoContainer::addGizmo(Gizmo* gizmo)
{
    assert(std::ranges::find(gizmos, gizmo) == gizmos.end() && "this gizmo is already added!");
    gizmos.push_back(gizmo);
}

void GizmoContainer::attachViewer(Gui::ViewerContext* viewer, Base::Placement& origin)
{
    if (!viewer) {
        return;
    }

    setUpAutoScale(viewer->getSoRenderManager()->getCamera());

    auto mat = origin.toMatrix();

    // Upstream also makes `origin` the document's editing transform. Here the
    // edit path has already set that transform for the object being edited,
    // and ViewProviderDragger passes the offset from it, so only the gizmo
    // node is placed.
    So3DAnnotation* annotation = SO_GET_ANY_PART(this, "annotation", So3DAnnotation);
    viewer->setupEditingRoot(annotation, &mat);
}

void GizmoContainer::setUpAutoScale(SoCamera* cameraIn)
{
    if (cameraIn->getTypeId() == SoOrthographicCamera::getClassTypeId()) {
        auto localCamera = dynamic_cast<SoOrthographicCamera*>(cameraIn);
        cameraSensor.attach(&localCamera->height);
        cameraPositionSensor.attach(&localCamera->orientation);
        calculateScaleAndOrientation();
    }
    else if (cameraIn->getTypeId() == SoPerspectiveCamera::getClassTypeId()) {
        auto localCamera = dynamic_cast<SoPerspectiveCamera*>(cameraIn);
        cameraSensor.attach(&localCamera->position);
        cameraPositionSensor.attach(&localCamera->orientation);
        calculateScaleAndOrientation();
    }
}

void GizmoContainer::calculateScaleAndOrientation()
{
    if (cameraSensor.getAttachedField()) {
        cameraChangeCallback(this, nullptr);
        cameraPositionChangeCallback(this, nullptr);
    }
}

void GizmoContainer::cameraChangeCallback(void* data, SoSensor*)
{
    auto sudoThis = static_cast<GizmoContainer*>(data);

    SoField* field = sudoThis->cameraSensor.getAttachedField();
    if (!field) {
        return;
    }

    auto camera = static_cast<SoCamera*>(field->getContainer());

    SbViewVolume viewVolume = camera->getViewVolume();
    for (auto gizmo : sudoThis->gizmos) {
        float localScale = viewVolume.getWorldToScreenScale(gizmo->getDraggerPlacement().pos, 0.015);
        gizmo->setGeometryScale(localScale);
    }
}

void GizmoContainer::cameraPositionChangeCallback(void* data, SoSensor*)
{
    auto sudoThis = static_cast<GizmoContainer*>(data);

    SoField* field = sudoThis->cameraSensor.getAttachedField();
    if (field) {
        auto camera = static_cast<SoCamera*>(field->getContainer());

        for (auto gizmo : sudoThis->gizmos) {
            gizmo->orientAlongCamera(camera);
        }
    }
}

bool GizmoContainer::isEnabled()
{
    return MiscParams::getEnableGizmos();
}

bool GizmoContainer::isCoarseSnapEnabled()
{
    return MiscParams::getEnableCoarseSnap();
}

Qt::KeyboardModifier GizmoContainer::getFineSnapModifier()
{
    auto modifier = static_cast<Qt::KeyboardModifier>(MiscParams::getFineSnapModifier());
    if (modifier == Qt::ControlModifier) {
        return modifier;
    }
    return Qt::ShiftModifier;
}

InputHint::UserInput GizmoContainer::getFineSnapKey()
{
    if (getFineSnapModifier() == Qt::ControlModifier) {
        return InputHint::UserInput::ModifierCtrl;
    }
    return InputHint::UserInput::ModifierShift;
}

bool GizmoContainer::isCoarseByDefault()
{
    return MiscParams::getDefaultCoarseDragBehavior()
        == static_cast<int>(DefaultDragBehavior::Coarse);
}

std::unique_ptr<GizmoContainer> GizmoContainer::create(
    std::initializer_list<Gui::Gizmo*> gizmos,
    ViewProviderDragger* vp
)
{
    auto gizmoContainer = std::make_unique<GizmoContainer>();
    gizmoContainer->addGizmos(gizmos);
    gizmoContainer->viewProvider = vp;

    vp->setGizmoContainer(gizmoContainer.get());

    return gizmoContainer;
}
