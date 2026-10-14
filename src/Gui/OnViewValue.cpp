// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
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
#include <algorithm>
#include <QApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QWidget>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/sensors/SoNodeSensor.h>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/Expression.h>
#include <Base/Placement.h>
#include <Base/Quantity.h>

#include "OnViewValue.h"
#include "Control.h"
#include "Document.h"
#include "EditableDatumLabel.h"
#include "QuantitySpinBox.h"
#include "SoDatumLabel.h"
#include "SpinBox.h"
#include "ViewerContext.h"
#include "Inventor/Draggers/Gizmo.h"

using namespace Gui;

struct OnViewValueSet::Entry
{
    QPointer<QuantitySpinBox> box;
    QString title;
    Place place = Place::TopMiddle;
    LinearGizmo* handle = nullptr;
    std::unique_ptr<EditableDatumLabel> label;
};

namespace
{
/// The sketcher's colour of a dimension an expression drives
SbColor expressionColor()
{
    auto hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/View");
    SbColor color;
    float transparency = 0.f;
    color.setPackedValue(uint32_t(hGrp->GetUnsigned("ExprBasedConstrDimColor", 0xFF7F00FF)),
                         transparency);
    return color;
}

/// A key that starts a number
bool startsNumber(const QKeyEvent* event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    const QString text = event->text();
    if (text.size() != 1) {
        return false;
    }
    const QChar c = text.at(0);
    return c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('+')
        || c == QLatin1Char('.') || c == QLatin1Char(',');
}
}  // namespace

OnViewValueSet::OnViewValueSet(QObject* parent)
    : QObject(parent)
{}

OnViewValueSet::~OnViewValueSet()
{
    attach(nullptr);
    // The handles outlive the set where their owner declares them first
    for (auto& entry : entries) {
        if (entry->handle) {
            entry->handle->setReleaseCallback(nullptr);
            entry->handle->setFocusBoxOnRelease(true);
        }
    }
}

int OnViewValueSet::addValue(QuantitySpinBox* box, const QString& title, Place place,
                             LinearGizmo* handle)
{
    auto entry = std::make_unique<Entry>();
    entry->box = box;
    entry->title = title;
    entry->place = place;
    entry->handle = handle;
    const int index = static_cast<int>(entries.size());
    if (handle) {
        // The view keeps the focus: a value is typed at the handle, not in
        // the panel's box
        handle->setFocusBoxOnRelease(false);
        handle->setReleaseCallback([this, index](bool) {
            setActive(index);
        });
    }
    entries.push_back(std::move(entry));
    // The label follows the box, whoever changes it: the panel, a drag, a
    // value typed here
    connect(box, qOverload<double>(&QuantitySpinBox::valueChanged), this, [this, index]() {
        updateLabel(index);
    });
    if (view) {
        makeLabels();
        placeLabels();
    }
    return index;
}

int OnViewValueSet::count() const
{
    return static_cast<int>(entries.size());
}

ViewerContext* OnViewValueSet::getView() const
{
    return view;
}

void OnViewValueSet::attach(ViewerContext* newView)
{
    if (newView == view) {
        return;
    }
    if (view) {
        endTyping(false);
        setActive(-1);
        dropLabels();
        connCameraReplaced.disconnect();
        if (cameraSensor) {
            cameraSensor->detach();
            delete cameraSensor;
            cameraSensor = nullptr;
        }
        if (keyWidget) {
            keyWidget->removeEventFilter(this);
        }
        keyWidget = nullptr;
    }
    view = newView;
    if (!view) {
        return;
    }

    cameraSensor = new SoNodeSensor(
        [](void* data, SoSensor*) {
            static_cast<OnViewValueSet*>(data)->onCamera();
        },
        this);
    if (SoCamera* camera = view->getCamera()) {
        cameraSensor->attach(camera);
    }
    // A change of projection replaces the camera node
    connCameraReplaced = view->signalCameraReplaced.connect([this]() {
        if (!cameraSensor || !view) {
            return;
        }
        cameraSensor->detach();
        if (SoCamera* camera = view->getCamera()) {
            cameraSensor->attach(camera);
        }
        onCamera();
    });

    // Keys typed in the view go to the viewer itself (setFocusToView); a view
    // without widgets (a mirror) has none
    keyWidget = view->getWidget();
    if (keyWidget) {
        keyWidget->installEventFilter(this);
    }

    makeLabels();
    placeLabels();
}

void OnViewValueSet::makeLabels()
{
    for (auto& entry : entries) {
        if (entry->label) {
            continue;
        }
        auto label = std::make_unique<EditableDatumLabel>(view, Base::Placement());
        label->setLabelType(SoDatumLabel::TEXT, EditableDatumLabel::Function::Dimensioning);
        label->setPoints(SbVec3f(0, 0, 0), SbVec3f(0, 0, 0));
        label->setPickable(true);
        connect(label.get(), &EditableDatumLabel::clicked, this, &OnViewValueSet::onLabelClicked);
        label->activate();
        entry->label = std::move(label);
    }
    for (int i = 0; i < count(); ++i) {
        updateLabel(i);
    }
}

void OnViewValueSet::dropLabels()
{
    for (auto& entry : entries) {
        entry->label.reset();
    }
}

void OnViewValueSet::placeLabels()
{
    if (!view) {
        return;
    }
    SoCamera* camera = view->getCamera();
    if (!camera) {
        return;
    }
    const SbVec2s size = view->getViewportRegion().getViewportSizePixels();
    const double ratio = std::max(1.0, view->devicePixelRatio());
    // Facing the camera, so that the text is upright on the screen
    const SbRotation orientation = camera->orientation.getValue();
    float q0 {}, q1 {}, q2 {}, q3 {};
    orientation.getValue(q0, q1, q2, q3);
    const Base::Rotation rotation(q0, q1, q2, q3);

    int row = 0;
    for (auto& entry : entries) {
        if (!entry->label || entry->place != Place::TopMiddle) {
            continue;
        }
        // One under the other, from the top middle down
        const int y = size[1] - int((TopMargin + row * TopMargin) * ratio);
        ++row;
        const SbVec3f p = view->getPointOnFocalPlane(SbVec2s(short(size[0] / 2), short(y)));
        entry->label->setPlacement(Base::Placement(Base::Vector3d(p[0], p[1], p[2]), rotation));
        entry->label->setPoints(SbVec3f(0, 0, 0), SbVec3f(0, 0, 0));
    }
}

void OnViewValueSet::updateLabel(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    auto& entry = entries[index];
    if (!entry->label || !entry->box || entry->label->isInEdit()) {
        return;
    }
    const Base::Quantity value = entry->box->value();
    // "Radius 2 mm", not "Radius 2.00 mm" (as the omni search writes it)
    QString text = trimTrailingZeros(QString::fromStdString(value.getUserString()), QLocale());
    if (!entry->title.isEmpty()) {
        text = entry->title + QLatin1Char(' ') + text;
    }
    if (entry->box->hasExpression()) {
        // what drives it, after the value
        const App::ObjectIdentifier& path = entry->box->boundPath();
        if (auto obj = path.getDocumentObject()) {
            auto info = obj->getExpression(path);
            if (info.expression) {
                text += QStringLiteral(" = ") + QString::fromStdString(info.expression->toString());
            }
        }
        entry->label->setColor(expressionColor());
    }
    else {
        entry->label->setActivatedColor();
    }
    entry->label->label->string = text.toUtf8().constData();
}

void OnViewValueSet::refresh()
{
    for (int i = 0; i < count(); ++i) {
        auto& entry = entries[i];
        if (entry->handle) {
            entry->handle->setHighlighted(i == active);
        }
        updateLabel(i);
    }
}

void OnViewValueSet::onCamera()
{
    placeLabels();
    Q_EMIT cameraChanged();
}

void OnViewValueSet::setActive(int index)
{
    if (index < -1 || index >= count()) {
        index = -1;
    }
    // A value an expression drives is not typed in the view
    if (index >= 0 && entries[index]->box && entries[index]->box->hasExpression()) {
        index = -1;
    }
    if (typing >= 0 && typing != index) {
        endTyping(false);
    }
    const bool changed = index != active;
    active = index;
    for (int i = 0; i < count(); ++i) {
        if (entries[i]->handle) {
            entries[i]->handle->setHighlighted(i == active);
        }
    }
    if (active >= 0) {
        focusView();
    }
    if (changed) {
        Q_EMIT activeChanged(active);
    }
}

int OnViewValueSet::getActive() const
{
    return active;
}

int OnViewValueSet::getTyping() const
{
    return typing;
}

bool OnViewValueSet::startTyping(int index, QKeyEvent* first)
{
    if (index < 0 || index >= count() || !view) {
        return false;
    }
    auto& entry = entries[index];
    if (!entry->label || !entry->box || entry->box->hasExpression()) {
        return false;
    }
    endTyping(false);
    if (active != index) {
        setActive(index);
    }
    typing = index;
    EditableDatumLabel* label = entry->label.get();
    const Base::Quantity value = entry->box->value();
    label->startEdit(value.getValue());
    label->setSpinboxValue(value.getValue(), value.getUnit());
    // Setting the value again unselected the number; selected, the first key
    // replaces it
    label->setFocusToSpinbox();

    // The key that closes the label is pressed in its box, and the focus is
    // back in the view by the time it is let go: its release is the view's
    // to ignore -- Escape's would leave the edit
    auto closedBy = [this](std::initializer_list<int> keys) {
        for (int k : keys) {
            if (std::find(takenKeys.begin(), takenKeys.end(), k) == takenKeys.end()) {
                takenKeys.push_back(k);
            }
        }
    };
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::editingFinished, this, [this, index, closedBy](double v) {
            closedBy({Qt::Key_Return, Qt::Key_Enter, Qt::Key_Tab});
            commit(index, v);
        }));
    // Emptied: a default has no "no value of its own", the box keeps it
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::parameterUnset, this, [this, closedBy]() {
            closedBy({Qt::Key_Return, Qt::Key_Enter, Qt::Key_Tab});
            endTyping(false);
        }));
    // Escape: the label has put its value back and closed already
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::editingCanceled, this, [this, closedBy]() {
            closedBy({Qt::Key_Escape});
            endTyping(false);
        }));
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::focusLost, this, [this]() {
            // Later: the box losing the focus to Enter's own handling must
            // not end what that handling is about to commit
            QTimer::singleShot(0, this, [this]() {
                if (typing >= 0) {
                    endTyping(false);
                }
            });
        }));
    // Ctrl+Enter: commit and leave the edit
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::finishEditingOnAllOVPs, this, [this, index, label]() {
            Base::Quantity quantity;
            if (label->getQuantity(quantity)) {
                commit(index, quantity.getValue());
            }
            else {
                endTyping(false);
            }
            Gui::Document* gdoc = view ? view->getDocument() : nullptr;
            App::Document* doc = gdoc ? gdoc->getDocument() : nullptr;
            if (doc) {
                QTimer::singleShot(0, [doc]() {
                    Gui::Control().accept(doc);
                });
            }
        }));

    if (first) {
        label->sendKeyEvent(first);
    }
    return true;
}

void OnViewValueSet::commit(int index, double value)
{
    QPointer<QuantitySpinBox> box = entries[index]->box;
    endTyping(true);
    if (box) {
        value = std::clamp(value, box->minimum(), box->maximum());
        // The box's owner writes the property from here, as from the panel
        box->setValue(value);
    }
    updateLabel(index);
    focusView();
}

void OnViewValueSet::endTyping(bool keep)
{
    for (const auto& connection : typingConnections) {
        disconnect(connection);
    }
    typingConnections.clear();
    const int index = typing;
    typing = -1;
    if (index < 0 || index >= count()) {
        return;
    }
    if (auto& label = entries[index]->label) {
        if (label->isInEdit()) {
            label->stopEdit(keep);
        }
    }
    updateLabel(index);
    focusView();
}

void OnViewValueSet::focusView()
{
    if (view && keyWidget && !keyWidget->hasFocus()) {
        view->setFocusToView();
    }
}

void OnViewValueSet::onLabelClicked(EditableDatumLabel* label)
{
    for (int i = 0; i < count(); ++i) {
        auto& entry = entries[i];
        if (entry->label.get() != label) {
            continue;
        }
        if (entry->box && entry->box->hasExpression()) {
            // its expression is where it is changed: the panel's box shows it
            entry->box->setFocus();
            entry->box->selectAll();
            return;
        }
        if (typing != i) {
            startTyping(i);
        }
        return;
    }
}

bool OnViewValueSet::takeKey(QKeyEvent* event, bool overrideOnly)
{
    // An open label's box has the keys itself
    if (typing >= 0 || entries.empty()) {
        return false;
    }
    const int key = event->key();
    const bool plain = !(event->modifiers()
                         & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && plain) {
        if (overrideOnly) {
            return true;
        }
        const bool back = key == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
        // from none to the first or the last; each value an expression drives
        // is passed over
        int index = active;
        for (int tries = 0; tries < count(); ++tries) {
            if (index < 0) {
                index = back ? count() - 1 : 0;
            }
            else {
                index = (index + (back ? count() - 1 : 1)) % count();
            }
            if (entries[index]->box && !entries[index]->box->hasExpression()) {
                setActive(index);
                return true;
            }
        }
        return true;
    }
    if (active < 0) {
        return false;
    }
    if (key == Qt::Key_Escape && plain) {
        if (!overrideOnly) {
            setActive(-1);
        }
        return true;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && plain) {
        if (!overrideOnly) {
            startTyping(active);
        }
        return true;
    }
    if (startsNumber(event)) {
        if (!overrideOnly) {
            startTyping(active, event);
        }
        return true;
    }
    return false;
}

bool OnViewValueSet::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == keyWidget) {
        switch (event->type()) {
            case QEvent::ShortcutOverride:
                // A key taken here is not a shortcut: digits change the view
                // and Space toggles visibility otherwise
                if (takeKey(static_cast<QKeyEvent*>(event), true)) {
                    event->accept();
                    return true;
                }
                break;
            case QEvent::KeyPress: {
                auto ke = static_cast<QKeyEvent*>(event);
                if (!ke->isAutoRepeat()) {
                    // a fresh press: whatever release was waiting is gone
                    takenKeys.erase(std::remove(takenKeys.begin(), takenKeys.end(), ke->key()),
                                    takenKeys.end());
                }
                if (takeKey(ke, false)) {
                    if (std::find(takenKeys.begin(), takenKeys.end(), ke->key())
                        == takenKeys.end()) {
                        takenKeys.push_back(ke->key());
                    }
                    return true;
                }
                break;
            }
            case QEvent::KeyRelease: {
                // the release of a key taken on its press is not the view's
                auto ke = static_cast<QKeyEvent*>(event);
                auto it = std::find(takenKeys.begin(), takenKeys.end(), ke->key());
                if (it != takenKeys.end()) {
                    if (!ke->isAutoRepeat()) {
                        takenKeys.erase(it);
                    }
                    return true;
                }
                break;
            }
            case QEvent::Resize:
                // the top middle moved
                QTimer::singleShot(0, this, [this]() {
                    placeLabels();
                });
                break;
            default:
                break;
        }
    }
    return QObject::eventFilter(watched, event);
}

#include "moc_OnViewValue.cpp"
