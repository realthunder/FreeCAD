/// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2023 Ondsel <development@ondsel.com>                     *
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
# include <algorithm>
# include <QApplication>
# include <QKeyEvent>
# include <QLineEdit>
# include <Inventor/SoPath.h>
# include <Inventor/SoPickedPoint.h>
# include <Inventor/events/SoMouseButtonEvent.h>
# include <Inventor/sensors/SoNodeSensor.h>
# include <Inventor/nodes/SoAnnotation.h>
# include <Inventor/nodes/SoEventCallback.h>
# include <Inventor/nodes/SoGroup.h>
# include <Inventor/nodes/SoOrthographicCamera.h>
# include <Inventor/nodes/SoPickStyle.h>
# include <Inventor/nodes/SoTransform.h>
#endif // _PreComp_

#include <Gui/Application.h>
#include <Gui/View3DInventor.h>
#include <Gui/View3DInventorViewer.h>
#include <Gui/ViewerContext.h>

#include "EditableDatumLabel.h"



using namespace Gui;


struct NodeData {
    EditableDatumLabel* label;
};

namespace {
/// The colour a parameter wears before the user gives it a value. Needed
/// before the object exists, for the constructor that picks its own.
SbColor deactivatedDimColor()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");
    SbColor color(0.5f, 0.5f, 0.5f);  // NOLINT
    unsigned long packed = (unsigned long)(color.getPackedValue());
    packed = hGrp->GetUnsigned("DeactivatedConstrDimColor", packed);
    float transparency = 0.f;  // SbColor takes it by reference
    color.setPackedValue((uint32_t)packed, transparency);
    return color;
}
}  // namespace

EditableDatumLabel::EditableDatumLabel(ViewerContext* view,
                                       const Base::Placement& plc,
                                       bool autoDistance,
                                       bool avoidMouseCursor)
    : EditableDatumLabel(view, plc, deactivatedDimColor(), autoDistance, avoidMouseCursor)
{}

EditableDatumLabel::EditableDatumLabel(ViewerContext* view,
                                       const Base::Placement& plc,
                                       SbColor color,
                                       bool autoDistance,
                                       bool avoidMouseCursor)
    : isSet(false)
    , hasFinishedEditing(false)
    , autoDistance(autoDistance)
    , autoDistanceReverse(false)
    , avoidMouseCursor(avoidMouseCursor)
    , value(0.0)
    , viewer(view)
    , spinBox(nullptr)
    , cameraSensor(nullptr)
    , editStartValue(0.0)
    , lockedAppearance(false)
    , function(Function::Positioning)
{
    initColors();
    if (viewer) {
        viewer->onViewLabels.push_back(this);
        viewer->addOnViewParameter(this);
    }
    // NOLINTBEGIN
    root = new SoAnnotation;
    root->ref();
    root->renderCaching = SoSeparator::OFF;

    transform = new SoTransform();
    transform->ref();
    root->addChild(transform);

    // A click on the label, once it is pickable. Inside the annotation, so
    // the pick style reaches the label alone.
    clickCallback = new SoEventCallback;
    clickCallback->ref();
    clickCallback->addEventCallback(SoMouseButtonEvent::getClassTypeId(), eventCallback, this);
    root->addChild(clickCallback);
    pickStyle = new SoPickStyle;
    pickStyle->ref();
    pickStyle->style = SoPickStyle::UNPICKABLE;
    root->addChild(pickStyle);

    label = new SoDatumLabel();
    label->ref();
    label->string = " ";
    label->textColor = color;
    label->size.setValue(17);
    label->lineWidth = 2.0;
    label->useAntialiasing = false;
    label->datumtype = SoDatumLabel::DISTANCE;
    label->param1 = 0.;
    label->param2 = 0.;
    label->param3 = 0.;
    if (autoDistance) {
        setLabelRecommendedDistance();
    }
    root->addChild(label);
    // The lines and the number as the render cache captures them: the label
    // itself only draws on the GL path, so without this an external backend
    // -- and a served client, which is fed the same capture -- has nothing to
    // draw (the Sketcher hangs the same companion by its constraints)
    root->addChild(label->getImageNode());

    setPlacement(plc);
    // NOLINTEND
}

EditableDatumLabel::~EditableDatumLabel()
{
    forgetViewer(true);
    transform->unref();
    clickCallback->removeEventCallback(SoMouseButtonEvent::getClassTypeId(), eventCallback, this);
    clickCallback->unref();
    pickStyle->unref();
    root->unref();
    label->unref();
}

void EditableDatumLabel::activate()
{
    if (!viewer || isActive()) {
        return;
    }

    // Where the view draws it from: on a mirror the served root, which is
    // how the dimension line reaches a browser at all
    if (SoGroup* parent = viewer->getOnViewParameterRoot()) {
        parent->addChild(root);
    }

    //track camera movements to update spinbox position.
    auto info = new NodeData{ this };
    cameraSensor = new SoNodeSensor([](void* data, SoSensor* sensor) {
        Q_UNUSED(sensor);
        auto info = static_cast<NodeData*>(data);
        info->label->positionSpinbox();
        if (info->label->autoDistance) {
            info->label->setLabelRecommendedDistance();
        }
    }, info);
    attachCameraSensor();
    // A change of projection replaces the camera node, and the sensor would
    // be left on one nobody moves any more
    connCameraReplaced = viewer->signalCameraReplaced.connect([this]() {
        attachCameraSensor();
        if (autoDistance) {
            setLabelRecommendedDistance();
        }
        positionSpinbox();
    });
    notifyChanged();
}

void EditableDatumLabel::attachCameraSensor()
{
    if (!cameraSensor) {
        return;
    }
    cameraSensor->detach();
    if (SoCamera* camera = viewer->getCamera()) {
        cameraSensor->attach(camera);
    }
}

void EditableDatumLabel::dropCameraSensor()
{
    connCameraReplaced.disconnect();

    if (cameraSensor) {
        auto data = static_cast<NodeData*>(cameraSensor->getData());
        delete data;
        cameraSensor->detach();
        delete cameraSensor;
        cameraSensor = nullptr;
    }
}

void EditableDatumLabel::forgetViewer(bool viewAlive)
{
    if (!viewer) {
        deactivate();
        return;
    }
    if (viewAlive) {
        deactivate();
        viewer->removeOnViewParameter(this);
    }
    else {
        // The camera node goes with the view
        dropCameraSensor();
    }
    auto& labels = viewer->onViewLabels;
    labels.erase(std::remove(labels.begin(), labels.end(), this), labels.end());
    viewer = nullptr;
}

void EditableDatumLabel::deactivate()
{
    stopEdit();
    dropCameraSensor();

    if (viewer) {
        if (SoGroup* parent = viewer->getOnViewParameterRoot()) {
            if (parent->findChild(root) >= 0) {
                parent->removeChild(root);
            }
        }
        notifyChanged();
    }
}

void EditableDatumLabel::startEdit(double val, QObject* eventFilteringObj, bool visibleToMouse)
{
    if (isInEdit() || !viewer) {
        return;
    }

    // Null on a mirror, and that is the whole of the difference between
    // the two tiers: the box below is built, validated, typed into and
    // read back identically, it is simply never shown and its text is
    // streamed to the client instead (docs/ThinClient.md section 8.7).
    QWidget* mdi = viewer->datumEditorParent();

    // A fresh edit is not a committed one, and Escape has to know what the
    // value was before it started.
    resetLockedState();
    editStartValue = val;

    label->string = " ";

    spinBox = new QuantitySpinBox(mdi);
    spinBox->setUnit(Base::Unit::Length);
    spinBox->setMinimum(-INT_MAX);
    spinBox->setMaximum(INT_MAX);
    spinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinBox->setKeyboardTracking(false);
    spinBox->setFocusPolicy(Qt::ClickFocus); // prevent passing focus with tab.
    // Installed first, so it runs LAST: Qt calls a widget's filters in
    // reverse order of installation, and the sketcher's keyboard manager
    // decides which keys reach the box at all. Enter and Tab it passes
    // through (it only notes the mode and returns false), so they arrive
    // here; Escape it claims for the tool, which is why the branch below
    // is not what cancels a sketch tool.
    spinBox->installEventFilter(this);
    if (eventFilteringObj) {
        spinBox->installEventFilter(eventFilteringObj);
    }

    if (!visibleToMouse) {
        setSpinboxVisibleToMouse(visibleToMouse);
    }

    // The number's size where the label draws it, so the box sits over the
    // number it replaces -- never smaller than the application's own text
    QFont font = spinBox->font();
    font.setPointSizeF(std::max<qreal>(getFontPointSize(), QApplication::font().pointSizeF()));
    spinBox->setFont(font);

    if (mdi) {
        spinBox->show();
    }
    setSpinboxValue(val);
    //Note: adjustSize apparently uses the Min/Max values to set the size. So if we don't set them to INT_MAX, the spinbox are much too big.
    spinBox->adjustSize();
    setFocusToSpinbox();

    connect(spinBox, qOverload<double>(&QuantitySpinBox::valueChanged),
            this, &EditableDatumLabel::handleSpinBoxValueChanged);
}

void EditableDatumLabel::stopEdit(bool writeChanges)
{
    if (spinBox) {
        // write the spinbox value in the label, or the one it started from
        Base::Quantity quantity = writeChanges ? spinBox->value()
                                               : Base::Quantity(editStartValue, spinBox->unit());

        double factor{};
        std::string unitStr;
        std::string valueStr;
        valueStr = quantity.getUserString(factor, unitStr);
        label->string = SbString(valueStr.c_str());

        spinBox->deleteLater();
        spinBox = nullptr;
        notifyChanged();
    }
}

bool EditableDatumLabel::isActive() const
{
    return cameraSensor != nullptr;
}

bool EditableDatumLabel::isInEdit() const
{
    return spinBox != nullptr;
}


double EditableDatumLabel::getValue() const
{
    // We use value rather than spinBox->rawValue() in case edit stopped.
    return value;
}

void EditableDatumLabel::setSpinboxValue(double val, const Base::Unit& unit)
{
    if (!spinBox) {
        Base::Console().DeveloperWarning("EditableDatumLabel::setSpinboxValue", "Spinbox doesn't exist in");
        return;
    }

    QSignalBlocker block(spinBox);
    spinBox->setValue(Base::Quantity(val, unit));
    value = val;
    positionSpinbox();

    if (spinBox->hasFocus()) {
        spinBox->selectNumber();
    }
    notifyChanged();
}

void EditableDatumLabel::setFocusToSpinbox()
{
    if (!spinBox) {
        Base::Console().DeveloperWarning("EditableDatumLabel::setFocusToSpinbox", "Spinbox doesn't exist in");
        return;
    }
    // Told to the view either way. Where the box is shown Qt's own focus is
    // the answer and this is ignored; where it is not, Qt would never focus
    // an unshown widget and the view keeps the record instead.
    if (viewer) {
        viewer->onViewParameterFocused(this);
    }
    if (!spinBox->hasFocus()) {
        spinBox->setFocus();
        spinBox->selectNumber();
    }
    notifyChanged();
}

void EditableDatumLabel::positionSpinbox()
{
    if (!spinBox) {
        return;
    }
    if (!viewer || !viewer->datumEditorParent()) {
        // Nowhere to move it to. Where the box belongs is streamed as the
        // world point below and placed by the client, which is the only
        // side that knows where its camera is between two of its own
        // frames (docs/ThinClient.md section 8.7).
        notifyChanged();
        return;
    }

    if (spinBox->hasFocus()) {
        spinBox->raise();
    }

    QSize wSize = spinBox->size();
    QWidget* canvas = viewer->getWidget();
    QSize vSize = canvas ? canvas->size() : viewer->datumEditorParent()->size();
    QPoint pxCoord = viewer->toQPoint(viewer->getPointOnViewport(getTextCenterPoint()));

    int posX = std::min(std::max(pxCoord.x() - wSize.width() / 2, 0), vSize.width() - wSize.width());
    int posY = std::min(std::max(pxCoord.y() - wSize.height() / 2, 0), vSize.height() - wSize.height());

    if (avoidMouseCursor) {
        QPoint cursorPos = viewer->datumEditorParent()->mapFromGlobal(QCursor::pos());
        int margin = static_cast<int>(wSize.height() * 0.7); // NOLINT
        if ((cursorPos.x() > posX - margin && cursorPos.x() < posX + wSize.width() + margin)
            && (cursorPos.y() > posY - margin && cursorPos.y() < posY + wSize.height() + margin)) {
            posY = cursorPos.y() + ((cursorPos.y() > pxCoord.y()) ? - wSize.height() - margin : margin);
        }
    }

    pxCoord.setX(posX);
    pxCoord.setY(posY);
    spinBox->move(pxCoord);
}

SbVec3f EditableDatumLabel::getTextCenterPoint() const
{
    //Here we need the 3d point and not the 2d point as are the SoLabel points.
    // First we get the 2D point (on the sketch/image plane) of the middle of the text label.
    SbVec3f point2D = label->getLabelTextCenter();
    // Get the translation and rotation values from the transform
    SbVec3f translation = transform->translation.getValue();
    SbRotation rotation = transform->rotation.getValue();

    // Calculate the inverse transformation
    SbVec3f invTranslation = -translation;
    SbRotation invRotation = rotation.inverse();

    // Transform the 2D coordinates to 3D
    // Plane form
    SbVec3f RX(1, 0, 0);
    SbVec3f RY(0, 1, 0);

    // move to position of Sketch
    invRotation.multVec(RX, RX);
    invRotation.multVec(RY, RY);
    invRotation.multVec(invTranslation, invTranslation);

    // we use invTranslation as the Base because in setPlacement we set transform->translation using
    // placement.getPosition() to fix the Zoffset. But this applies the X & Y translation too.
    Base::Vector3d pos(invTranslation[0], invTranslation[1], invTranslation[2]);
    Base::Vector3d RXb(RX[0], RX[1], RX[2]);
    Base::Vector3d RYb(RY[0], RY[1], RY[2]);
    Base::Vector3d P2D(point2D[0], point2D[1], point2D[2]);
    P2D.TransformToCoordinateSystem(pos, RXb, RYb);

    return {float(P2D.x), float(P2D.y), float(P2D.z)};
}

bool EditableDatumLabel::syncValueFromSpinBox(bool emitParameterUnset)
{
    if (!spinBox) {
        return false;
    }

    // An emptied box is not a zero: the parameter goes back to being unset,
    // so the tool takes its value from the pointer again.
    if (spinBox->hasValidInput()) {
        // With keyboard tracking off the box holds what was typed back until
        // Enter or the focus leaving, and Enter is taken here before the box
        // has seen it: take it now. Not for an emptied box, which has
        // nothing to take.
        QMetaObject::invokeMethod(spinBox, "handlePendingEmit", Qt::DirectConnection);
    }
    else {
        if (emitParameterUnset) {
            resetLockedState();
            Q_EMIT parameterUnset();
        }
        return false;
    }

    value = spinBox->rawValue();
    isSet = true;

    if (hasFinishedEditing) {
        setLockedAppearance(true);
    }

    return true;
}

void EditableDatumLabel::handleSpinBoxValueChanged()
{
    if (syncValueFromSpinBox()) {
        Q_EMIT valueChanged(value);
    }
}

bool EditableDatumLabel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Escape) {
            if (qobject_cast<QAbstractSpinBox*>(watched)) {
                this->value = this->editStartValue;
                this->isSet = false;
                this->hasFinishedEditing = false;
                this->setLockedAppearance(false);
                this->setSpinboxValue(this->editStartValue);
                this->stopEdit();
                Q_EMIT this->editingCanceled(this->value);
                return true;
            }
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter
            || keyEvent->key() == Qt::Key_Tab) {

            if (qobject_cast<QAbstractSpinBox*>(watched)) {
                // Tab with nothing typed just moves on; it does not commit a
                // value the user never entered.
                if (keyEvent->key() == Qt::Key_Tab && !this->isSet) {
                    if (!this->spinBox->hasValidInput()) {
                        syncValueFromSpinBox();
                        return true;
                    }
                    return false;
                }

                // Ctrl+Enter takes every visible parameter as it stands.
                if (keyEvent->modifiers() & Qt::ControlModifier) {
                    Q_EMIT this->finishEditingOnAllOVPs();
                    return true;
                }

                this->hasFinishedEditing = true;

                if (!syncValueFromSpinBox()) {
                    return true;
                }

                const double finishedValue = value;
                Q_EMIT this->editingFinished(finishedValue);
                return true;
            }
        }
        else if (this->hasFinishedEditing && keyEvent->key() != Qt::Key_Tab) {
            // typing again after committing reopens the parameter
            this->resetLockedState();
            return false;
        }
    }
    else if (event->type() == QEvent::FocusOut && watched == spinBox) {
        // Left to the box as well: it is only told about
        Q_EMIT focusLost();
    }

    return QObject::eventFilter(watched, event);
}

void EditableDatumLabel::setLockedAppearance(bool locked)
{
    // Upstream draws a lock icon inside the box here. That needs
    // QuantitySpinBox::addIconSpace and getMargin, which this fork does not
    // carry, and a mirror has no widget to draw one on in any case -- a
    // client would need it as streamed state beside getText() and
    // getSelection() (docs/ThinClient.md sec 8.7). The state is real and is
    // what the controller reads; only the affordance is missing.
    lockedAppearance = locked;
}

void EditableDatumLabel::resetLockedState()
{
    hasFinishedEditing = false;
    setLockedAppearance(false);
}

void EditableDatumLabel::initColors()
{
    ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");

    dimConstrColor = SbColor(1.0f, 0.149f, 0.0f);           // NOLINT
    dimConstrDeactivatedColor = SbColor(0.5f, 0.5f, 0.5f);  // NOLINT

    float transparency = 0.f;
    unsigned long color = (unsigned long)(dimConstrColor.getPackedValue());
    color = hGrp->GetUnsigned("ConstrainedDimColor", color);
    dimConstrColor.setPackedValue((uint32_t)color, transparency);

    color = (unsigned long)(dimConstrDeactivatedColor.getPackedValue());
    color = hGrp->GetUnsigned("DeactivatedConstrDimColor", color);
    dimConstrDeactivatedColor.setPackedValue((uint32_t)color, transparency);
}

void EditableDatumLabel::setActivatedColor()
{
    setColor(dimConstrColor);
}

void EditableDatumLabel::setDeactivatedColor()
{
    setColor(dimConstrDeactivatedColor);
}

void EditableDatumLabel::setPlacement(const Base::Placement& plc)
{
    double x{}, y{}, z{}, w{}; // NOLINT
    plc.getRotation().getValue(x, y, z, w);
    transform->rotation.setValue(x, y, z, w); // NOLINT

    Base::Vector3d pos = plc.getPosition();
    transform->translation.setValue(float(pos.x), float(pos.y), float(pos.z));

    Base::Vector3d RN(0, 0, 1);
    RN = plc.getRotation().multVec(RN);
    label->norm.setValue(SbVec3f(float(RN.x), float(RN.y), float(RN.z)));
}

// NOLINTNEXTLINE
void EditableDatumLabel::setColor(SbColor color)
{
    label->textColor = color;
    notifyChanged();
}

void EditableDatumLabel::setFocus()
{
    if (spinBox) {
        spinBox->selectNumber();
    }
}

void EditableDatumLabel::setPoints(SbVec3f p1, SbVec3f p2)
{
    label->setPoints(p1, p2);
    //TODO: here the position of the spinbox is not going to be center of p1, p2 because the point given by getTextCenterPoint
    // is not updated yet. it will be only on redraw so it is actually positioning on previous position.

    positionSpinbox();
    if (autoDistance) {
        setLabelRecommendedDistance();
    }
}

void EditableDatumLabel::setPoints(Base::Vector3d p1, Base::Vector3d p2)
{
    setPoints(SbVec3f(float(p1.x), float(p1.y), float(p1.z)),
              SbVec3f(float(p2.x), float(p2.y), float(p2.z)));
}

// NOLINTNEXTLINE
void EditableDatumLabel::setLabelType(SoDatumLabel::Type type, Function funct)
{
    label->datumtype = type;
    function = funct;
}

// NOLINTNEXTLINE
void EditableDatumLabel::setLabelDistance(double val)
{
    label->param1 = float(val);
}

// NOLINTNEXTLINE
void EditableDatumLabel::setLabelStartAngle(double val)
{
    label->param2 = float(val);
}

// NOLINTNEXTLINE
void EditableDatumLabel::setLabelRange(double val)
{
    label->param3 = float(val);
}

void EditableDatumLabel::setLabelRecommendedDistance()
{
    // Takes the 3d view size, and set the label distance to a % of that, such that the distance does not depend on the zoom level.
    if (!viewer) {
        return;
    }
    float width = -1.;
    float length = -1.;
    viewer->getDimensions(width, length);

    if (width == -1. || length == -1.) {
        return;
    }

    label->param1 = (autoDistanceReverse ? -1.0F : 1.0F) * (width + length) * 0.03F; // NOLINT
}

void EditableDatumLabel::setLabelAutoDistanceReverse(bool val)
{
    autoDistanceReverse = val;
}

void EditableDatumLabel::setSpinboxVisibleToMouse(bool val)
{
    spinBox->setAttribute(Qt::WA_TransparentForMouseEvents, !val);
}

void EditableDatumLabel::setPickable(bool val)
{
    pickStyle->style = val ? SoPickStyle::SHAPE_ON_TOP : SoPickStyle::UNPICKABLE;
}

void EditableDatumLabel::eventCallback(void* data, SoEventCallback* cb)
{
    static_cast<EditableDatumLabel*>(data)->handleEvent(cb);
}

void EditableDatumLabel::handleEvent(SoEventCallback* cb)
{
    // Before asking for the pick: every label of a set sits in the scene
    // with a callback of its own
    if (pickStyle->style.getValue() == SoPickStyle::UNPICKABLE) {
        return;
    }
    const auto* event = static_cast<const SoMouseButtonEvent*>(cb->getEvent());
    if (event->getButton() != SoMouseButtonEvent::BUTTON1) {
        return;
    }
    const SoPickedPoint* picked = cb->getPickedPoint();
    if (!picked || !picked->getPath()->containsNode(root)) {
        return;
    }
    // The press too, so that nothing behind the label takes it for a
    // selection; the release is the click
    cb->setHandled();
    if (event->getState() == SoButtonEvent::UP) {
        Q_EMIT clicked(this);
    }
}

EditableDatumLabel::Function EditableDatumLabel::getFunction()
{
    return function;
}

SbVec3f EditableDatumLabel::getAnchorPoint() const
{
    return getTextCenterPoint();
}

double EditableDatumLabel::getFontPointSize() const
{
    // SoDatumLabel::drawImage takes its size field as a point size
    return label->size.getValue();
}

QString EditableDatumLabel::getText() const
{
    return spinBox ? spinBox->text() : QString();
}

void EditableDatumLabel::getSelection(int& start, int& length) const
{
    start = 0;
    length = 0;
    if (spinBox) {
        spinBox->getSelection(start, length);
    }
}

bool EditableDatumLabel::sendKeyEvent(QKeyEvent* event)
{
    if (!spinBox) {
        return false;
    }
    // Straight at the box, because on a mirror nothing else would take it
    // there -- Qt delivers keys to the focused widget and an unshown one is
    // never focused. The event filter the controller installed still runs
    // (sendEvent applies the receiver's filters), so which keys the box
    // claims is decided by DrawSketchKeyboardManager, once, for both tiers.
    const bool handled = QApplication::sendEvent(spinBox, event);
    notifyChanged();
    return handled;
}

void EditableDatumLabel::notifyChanged()
{
    if (viewer) {
        viewer->onViewParametersChanged();
    }
}

#include "moc_EditableDatumLabel.cpp" // NOLINT

