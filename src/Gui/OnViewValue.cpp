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

#include <algorithm>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/sensors/SoNodeSensor.h>

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
    std::vector<Gizmo*> handles;
    /// Where each handle stands, in world coordinates; by the handle's place
    std::vector<Base::Vector3d> anchors;
    MenuProvider menu;
    int group = 0;
    QMetaObject::Connection boxConnection;
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

/// The camera's turn as a placement's rotation: a plane facing the camera
Base::Rotation facingCamera(SoCamera* camera)
{
    float q0 {}, q1 {}, q2 {}, q3 {};
    camera->orientation.getValue().getValue(q0, q1, q2, q3);
    return {q0, q1, q2, q3};
}
}  // namespace

OnViewValueSet::OnViewValueSet(QObject* parent)
    : QObject(parent)
{}

OnViewValueSet::~OnViewValueSet()
{
    attach(nullptr);
    truncate(0);
}

void OnViewValueSet::setupHandle(int index, Gizmo* handle)
{
    // The view keeps the focus: a value is typed at the handle, not in the
    // panel's box
    handle->setFocusBoxOnRelease(false);
    handle->setReleaseCallback([this, index, handle](bool) {
        setActive(index, handle);
    });
    // A right click on the handle opens its menu, and the pointer over it
    // says so (entry 135, the reporter 2026-10-15 11:30 and 11:35)
    handle->setMenu(
        [self = QPointer<OnViewValueSet>(this), index, handle]() {
            if (!self || index >= self->count()) {
                return;
            }
            self->setActive(index, handle);
            self->openMenu(handle, true);
        },
        tr("Drag to change the value; click to type it.\n"
           "Right click for the menu."));
    // A drag is one undo step (entry 136)
    handle->setDragCallback([self = QPointer<OnViewValueSet>(this), handle](Gizmo::DragEvent event) {
        if (!self) {
            return;
        }
        switch (event) {
            case Gizmo::DragEvent::Started:
                self->dragHandle = handle;
                Q_EMIT self->stepBegun();
                break;
            case Gizmo::DragEvent::Finished:
            case Gizmo::DragEvent::Cancelled:
                if (self->dragHandle == handle) {
                    self->dragHandle = nullptr;
                }
                Q_EMIT self->stepEnded(event == Gizmo::DragEvent::Cancelled);
                break;
        }
    });
}

int OnViewValueSet::addValue(QuantitySpinBox* box, const QString& title, Place place,
                             Gizmo* handle)
{
    auto entry = std::make_unique<Entry>();
    entry->box = box;
    entry->title = title;
    entry->place = place;
    const int index = static_cast<int>(entries.size());
    // The label follows the box, whoever changes it: the panel, a drag, a
    // value typed here
    entry->boxConnection =
        connect(box, qOverload<double>(&QuantitySpinBox::valueChanged), this, [this, index]() {
            updateLabel(index);
        });
    entries.push_back(std::move(entry));
    if (handle) {
        addHandle(index, handle);
    }
    if (view) {
        makeLabels();
        placeLabels();
    }
    return index;
}

void OnViewValueSet::addHandle(int index, Gizmo* handle)
{
    if (index < 0 || index >= count() || !handle) {
        return;
    }
    auto& entry = entries[index];
    entry->handles.push_back(handle);
    entry->anchors.emplace_back();
    setupHandle(index, handle);
    handle->setHighlighted(index == active);
}

void OnViewValueSet::clearHandles(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    auto& entry = entries[index];
    for (Gizmo* handle : entry->handles) {
        if (handle == activeHandle) {
            activeHandle = nullptr;
        }
        handle->setReleaseCallback(nullptr);
        handle->setMenu(nullptr);
        handle->setDragCallback(nullptr);
        if (handle == rightPressHandle) {
            rightPressHandle = nullptr;
        }
        // taken away in its drag: the drag's step ends here
        if (handle == dragHandle) {
            dragHandle = nullptr;
            Q_EMIT stepEnded(false);
        }
        handle->setFocusBoxOnRelease(true);
    }
    entry->handles.clear();
    entry->anchors.clear();
}

void OnViewValueSet::setHandleAnchor(Gizmo* handle, const Base::Vector3d& world)
{
    for (auto& entry : entries) {
        for (std::size_t i = 0; i < entry->handles.size(); ++i) {
            if (entry->handles[i] == handle) {
                entry->anchors[i] = world;
                if (handle == activeHandle) {
                    placeLabels();
                }
                return;
            }
        }
    }
}

void OnViewValueSet::setMenu(int index, MenuProvider provider)
{
    if (index >= 0 && index < count()) {
        entries[index]->menu = std::move(provider);
    }
}

void OnViewValueSet::setGroup(int index, int group)
{
    if (index >= 0 && index < count()) {
        entries[index]->group = group;
        highlight();
    }
}

void OnViewValueSet::truncate(int keep)
{
    keep = std::max(0, keep);
    if (keep >= count()) {
        return;
    }
    if (typing >= keep) {
        endTyping(false);
    }
    if (active >= keep) {
        setActive(-1);
    }
    while (count() > keep) {
        disconnect(entries.back()->boxConnection);
        // The handles stay with their owner, who deletes them next; what the
        // set gave them goes
        clearHandles(count() - 1);
        entries.pop_back();
    }
}

int OnViewValueSet::count() const
{
    return static_cast<int>(entries.size());
}

int OnViewValueSet::valueOf(const Gizmo* handle) const
{
    for (int i = 0; i < count(); ++i) {
        const auto& handles = entries[i]->handles;
        if (std::find(handles.begin(), handles.end(), handle) != handles.end()) {
            return i;
        }
    }
    return -1;
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
        entry->label = std::move(label);
    }
    for (int i = 0; i < count(); ++i) {
        updateLabel(i);
    }
    showLabels();
}

void OnViewValueSet::dropLabels()
{
    for (auto& entry : entries) {
        entry->label.reset();
    }
}

void OnViewValueSet::showLabels()
{
    // The top middle ones always, a handle's while its value is active
    for (int i = 0; i < count(); ++i) {
        auto& label = entries[i]->label;
        if (!label) {
            continue;
        }
        const bool shown = entries[i]->place == Place::TopMiddle || i == active;
        if (shown && !label->isActive()) {
            label->activate();
        }
        else if (!shown && label->isActive()) {
            label->deactivate();
        }
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
    const Base::Rotation rotation = facingCamera(camera);

    int row = 0;
    for (int i = 0; i < count(); ++i) {
        auto& entry = entries[i];
        if (!entry->label) {
            continue;
        }
        SbVec2s pixel;
        if (entry->place == Place::TopMiddle) {
            // One under the other, from the top middle down
            pixel = SbVec2s(short(size[0] / 2), short(size[1] - int((TopMargin + row * TopMargin) * ratio)));
            ++row;
        }
        else {
            // Beside the active handle, up and to the right of it
            if (i != active || entry->handles.empty()) {
                continue;
            }
            std::size_t h = 0;
            auto it = std::find(entry->handles.begin(), entry->handles.end(), activeHandle);
            if (it != entry->handles.end()) {
                h = std::distance(entry->handles.begin(), it);
            }
            const Base::Vector3d& anchor = entry->anchors[h];
            const SbVec2s at = view->getPointOnViewport(SbVec3f(float(anchor.x), float(anchor.y), float(anchor.z)));
            const int offset = int(HandleOffset * ratio);
            pixel = SbVec2s(short(at[0] + offset), short(at[1] + offset));
        }
        const SbVec3f p = view->getPointOnFocalPlane(pixel);
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

void OnViewValueSet::highlight()
{
    // Every handle of the active value -- a default drives all its edges --
    // and of every value of its group
    const int group = active >= 0 ? entries[active]->group : 0;
    for (int i = 0; i < count(); ++i) {
        const bool lit = i == active || (group > 0 && entries[i]->group == group);
        for (Gizmo* handle : entries[i]->handles) {
            handle->setHighlighted(lit);
        }
    }
}

void OnViewValueSet::refresh()
{
    highlight();
    for (int i = 0; i < count(); ++i) {
        updateLabel(i);
    }
    showLabels();
    placeLabels();
}

void OnViewValueSet::onCamera()
{
    placeLabels();
    Q_EMIT cameraChanged();
}

void OnViewValueSet::setActive(int index, Gizmo* handle)
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
    activeHandle = nullptr;
    if (active >= 0) {
        const auto& handles = entries[active]->handles;
        if (handle && std::find(handles.begin(), handles.end(), handle) != handles.end()) {
            activeHandle = handle;
        }
        else if (!handles.empty()) {
            activeHandle = handles.front();
        }
    }
    highlight();
    showLabels();
    placeLabels();
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

Gizmo* OnViewValueSet::getActiveHandle() const
{
    return active >= 0 ? activeHandle : nullptr;
}

int OnViewValueSet::getTyping() const
{
    return typing;
}

void OnViewValueSet::openMenu(Gizmo* handle, bool atPointer)
{
    if (!handle) {
        handle = getActiveHandle();
    }
    const int index = handle ? valueOf(handle) : active;
    if (index < 0 || !entries[index]->menu) {
        return;
    }
    QMenu menu(keyWidget);
    entries[index]->menu(menu, handle);
    if (menu.isEmpty()) {
        return;
    }
    // Opened by a click, where the pointer is; by a key, at the handle
    QPoint where = QCursor::pos();
    if (!atPointer && handle && view && keyWidget) {
        const auto& entry = entries[index];
        auto it = std::find(entry->handles.begin(), entry->handles.end(), handle);
        if (it != entry->handles.end()) {
            const Base::Vector3d& anchor = entry->anchors[std::distance(entry->handles.begin(), it)];
            const SbVec2s at = view->getPointOnViewport(
                SbVec3f(float(anchor.x), float(anchor.y), float(anchor.z)));
            where = keyWidget->mapToGlobal(view->toQPoint(at));
        }
    }
    menu.exec(where);
    focusView();
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
    // An open label is one undo step, however often Enter commits in it
    Q_EMIT stepBegun();
    // This set's filter on the label's box, ahead of the label's own: Enter,
    // Tab and Escape are handled here (takeLabelKey)
    label->startEdit(value.getValue(), this);
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
    // Emptied: a default has no "no value of its own", the box keeps it
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::parameterUnset, this, [this, closedBy]() {
            closedBy({Qt::Key_Return, Qt::Key_Enter, Qt::Key_Tab});
            endTyping(false);
        }));
    typingConnections.push_back(
        connect(label, &EditableDatumLabel::focusLost, this, [this]() {
            // Later: the focus may be going to what Enter or Tab opens. The
            // focus going elsewhere commits what was typed and closes the
            // label (the reporter, 10:13: "typed then click away without
            // enter will commit and dismiss").
            QTimer::singleShot(0, this, [this]() {
                if (typing >= 0) {
                    writeTyped();
                    endTyping(true);
                }
            });
        }));

    if (first) {
        label->sendKeyEvent(first);
    }
    return true;
}

void OnViewValueSet::commit(int index, double value)
{
    QPointer<QuantitySpinBox> box = entries[index]->box;
    // Written while the label is open: inside its step
    if (box) {
        value = std::clamp(value, box->minimum(), box->maximum());
        // The box's owner writes the property from here, as from the panel
        box->setValue(value);
    }
    endTyping(true);
    if (index < count()) {
        updateLabel(index);
    }
    focusView();
}

void OnViewValueSet::writeTyped()
{
    if (typing < 0 || typing >= count()) {
        return;
    }
    auto& entry = entries[typing];
    // What the label's box parsed as it was typed (isSet: something valid
    // was), in the value's own unit -- its text read again lost the unit
    if (!entry->label || !entry->box || !entry->label->isSet) {
        return;
    }
    const double value = std::clamp(entry->label->value, entry->box->minimum(),
                                    entry->box->maximum());
    if (value != entry->box->rawValue()) {
        // The box's owner writes the property from here, as from the panel
        entry->box->setValue(value);
    }
}

bool OnViewValueSet::takeLabelKey(QKeyEvent* event)
{
    if (typing < 0 || typing >= count()) {
        return false;
    }
    const int key = event->key();
    auto taken = [this](int k) {
        if (std::find(takenKeys.begin(), takenKeys.end(), k) == takenKeys.end()) {
            takenKeys.push_back(k);
        }
    };
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Committed, the label left open, its number selected for the next
        // (the reporter, 10:07: "Press enter, commit change and trigger
        // fillet partial recompute, but shall not dismiss the edit box")
        writeTyped();
        taken(key);
        auto& label = entries[typing]->label;
        if (label && entries[typing]->box) {
            const Base::Quantity value = entries[typing]->box->value();
            label->setSpinboxValue(value.getValue(), value.getUnit());
            label->setFocusToSpinbox();
        }
        return true;
    }
    if (key == Qt::Key_Escape) {
        // What was typed since the last Enter goes; what Enter committed stays
        taken(key);
        endTyping(false);
        return true;
    }
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        // Committed, closed, and on to the next value (10:13)
        const bool back = key == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
        taken(key);
        writeTyped();
        endTyping(true);
        moveActive(back);
        return true;
    }
    return false;
}

void OnViewValueSet::endTyping(bool keep)
{
    for (const auto& connection : typingConnections) {
        disconnect(connection);
    }
    typingConnections.clear();
    const int index = typing;
    typing = -1;
    if (index < 0) {
        return;
    }
    // the label's step ends, open since startTyping()
    Q_EMIT stepEnded(false);
    if (index >= count()) {
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

void OnViewValueSet::moveActive(bool back)
{
    // from none to the first or the last; each value an expression drives is
    // passed over
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
            return;
        }
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
        moveActive(key == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier));
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
    // Space: the menu of the active handle, as a right click on it opens it.
    // Otherwise Space keeps its own meaning (toggling visibility).
    if (key == Qt::Key_Space && plain && entries[active]->menu) {
        if (!overrideOnly) {
            // after the key: the menu runs a loop of its own
            QTimer::singleShot(0, this, [this]() {
                openMenu();
            });
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
    // The open label's box: Enter, Tab and Escape are this set's, ahead of
    // the label (it is the only box this set watches)
    if (watched != keyWidget && typing >= 0 && qobject_cast<QuantitySpinBox*>(watched)) {
        if (event->type() == QEvent::ShortcutOverride) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape
                || key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                event->accept();
                return true;
            }
        }
        else if (event->type() == QEvent::KeyPress
                 && takeLabelKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
    if (watched == keyWidget && dragHandle) {
        // During a drag Esc or another mouse button cancels it, the value back
        // to where it began (entry 136); the key and the button are taken,
        // so neither leaves the edit nor opens a menu
        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            if (std::find(takenKeys.begin(), takenKeys.end(), int(Qt::Key_Escape))
                == takenKeys.end()) {
                takenKeys.push_back(Qt::Key_Escape);
            }
            dragHandle->cancelDrag();
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress
            && static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton) {
            cancelButton = static_cast<QMouseEvent*>(event)->button();
            dragHandle->cancelDrag();
            return true;
        }
    }
    if (watched == keyWidget && cancelButton && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == cancelButton) {
        cancelButton = 0;
        return true;
    }
    // A right click on a handle: its menu (entry 135), not the view's
    if (watched == keyWidget && !dragHandle && event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent*>(event)->button() == Qt::RightButton) {
        rightPressHandle = nullptr;
        for (const auto& entry : entries) {
            for (Gizmo* handle : entry->handles) {
                if (handle->isHovered()) {
                    rightPressHandle = handle;
                }
            }
        }
        if (rightPressHandle) {
            return true;
        }
    }
    if (watched == keyWidget && rightPressHandle && event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent*>(event)->button() == Qt::RightButton) {
        Gizmo* handle = rightPressHandle;
        rightPressHandle = nullptr;
        if (valueOf(handle) >= 0 && handle->isHovered()) {
            // after the event: the menu runs a loop of its own
            QTimer::singleShot(0, this, [this, handle]() {
                if (valueOf(handle) >= 0) {
                    handle->openMenu();
                }
            });
        }
        return true;
    }
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
