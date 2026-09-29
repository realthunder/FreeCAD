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

#ifndef GUI_EDITABLEDATUMLABEL_H
#define GUI_EDITABLEDATUMLABEL_H

#include <QObject>
#include <QString>
#include <fastsignals/signal.h>
#include <Gui/QuantitySpinBox.h>

#include "SoDatumLabel.h"

#include <FCGlobal.h>

class SoEventCallback;
class SoNodeSensor;
class SoPickStyle;
class SoTransform;
class QKeyEvent;

namespace Gui {

class ViewerContext;


class GuiExport EditableDatumLabel : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(EditableDatumLabel)

public:
    enum class Function {
        Positioning,
        Dimensioning,
        Forced
    };

    EditableDatumLabel(ViewerContext* view, const Base::Placement& plc, SbColor color, bool autoDistance = false, bool avoidMouseCursor = false);
    EditableDatumLabel(ViewerContext* view, const Base::Placement& plc, bool autoDistance = false, bool avoidMouseCursor = false);

    ~EditableDatumLabel() override;

    void activate();
    void deactivate();

    void startEdit(double val, QObject* eventFilteringObj = nullptr, bool visibleToMouse = false);
    /// End the edit, the label showing the value typed, or with \a writeChanges
    /// false the value the edit started from
    void stopEdit(bool writeChanges = true);
    bool isActive() const;
    bool isInEdit() const;
    double getValue() const;
    void setSpinboxValue(double val, const Base::Unit& unit = Base::Unit::Length);
    void setPlacement(const Base::Placement& plc);
    void setColor(SbColor color);
    /// the colour of a parameter the user has given a value
    void setActivatedColor();
    /// the colour of one still taking its value from the pointer
    void setDeactivatedColor();
    void setFocus();
    void setPoints(SbVec3f p1, SbVec3f p2);
    void setPoints(Base::Vector3d p1, Base::Vector3d p2);
    void setFocusToSpinbox();
    void setLabelType(SoDatumLabel::Type type, Function function = Function::Positioning);
    void setLabelDistance(double val);
    void setLabelStartAngle(double val);
    void setLabelRange(double val);
    void setLabelRecommendedDistance();
    void setLabelAutoDistanceReverse(bool val);
    void setSpinboxVisibleToMouse(bool val);
    /** Whether a click on the label in the view emits clicked(), which also
     * makes the label stop the pointer. Off by default: the sketcher's
     * labels are not to be clicked. A served view's click reaches the label
     * the same way, as a replayed event through the scene.
     */
    void setPickable(bool val);

    /** @name Finished editing, as distinct from set
     *
     * `isSet` says the box holds a value the user typed. `hasFinishedEditing`
     * says they committed it with Enter, which is a different question: a
     * committed parameter is skipped when Tab cycles to the next one, and
     * Ctrl+Enter commits every visible one at once. The on-view parameter
     * controller reads both.
     */
    //@{
    /// Show that the value is committed. The lock icon upstream draws here
    /// needs QuantitySpinBox::addIconSpace and getMargin, which this fork
    /// does not have, and a mirror has no widget to draw it on either -- so
    /// the state is kept and nothing is painted yet.
    void setLockedAppearance(bool locked);
    /// Forget the commitment and its appearance.
    void resetLockedState();
    //@}

    Function getFunction();

    /** @name What a view that cannot show a widget streams instead
     *
     * The entry box is the same QuantitySpinBox on either tier; on a mirror
     * it is simply never parented and never shown, and these are read to
     * put it on the client's screen (docs/ThinClient.md section 8.7). All
     * of it is display state -- the value, its text, where it belongs and
     * what is selected in it -- because every decision about the text is
     * taken on this side.
     */
    //@{
    /// Where the box belongs, in world coordinates. The client projects it.
    SbVec3f getAnchorPoint() const;
    /// The box's text exactly as a desktop user would read it.
    QString getText() const;
    /// The point size the label draws its number in, which the box takes
    /// (the client keeps its own minimum, as the desktop does)
    double getFontPointSize() const;
    /// What selectNumber() left selected, so the client can show the same.
    void getSelection(int& start, int& length) const;
    /// Deliver a key to the box, the desktop's focus having done it there.
    bool sendKeyEvent(QKeyEvent* event);
    //@}

    // NOLINTBEGIN
    SoDatumLabel* label;
    SbColor dimConstrColor, dimConstrDeactivatedColor;
    bool isSet;
    bool hasFinishedEditing;  ///< the user pressed Enter, not merely typed
    bool autoDistance;
    bool autoDistanceReverse;
    bool avoidMouseCursor;
    double value;
    // NOLINTEND

Q_SIGNALS:
    void valueChanged(double val);
    /// the value was committed with Enter
    void editingFinished(double val);
    /// the edit was abandoned and the value put back
    void editingCanceled(double val);
    /// the box was emptied, so the parameter is no longer set
    void parameterUnset();
    /// Ctrl+Enter: commit every visible parameter of this stage
    void finishEditingOnAllOVPs();
    /// the label was clicked in the view, when pickable (upstream 6fa9125919)
    void clicked(Gui::EditableDatumLabel* label);
    /// the entry box lost the focus -- on the desktop only, a box that is
    /// never shown never has it
    void focusLost();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initColors();
    bool syncValueFromSpinBox(bool emitParameterUnset = true);
    void handleSpinBoxValueChanged();
    void positionSpinbox();
    SbVec3f getTextCenterPoint() const;
    /// Tell the view its on-view set moved, so a mirror can restate it.
    void notifyChanged();
    /// Follow the view's camera, the node it has now
    void attachCameraSensor();
    /// Stop following it
    void dropCameraSensor();
    /** The view is going. While it is still itself (\a viewAlive) the
     * label is taken out of it as deactivate() would; after, it only lets
     * go of it: nothing of the view may be reached then.
     */
    void forgetViewer(bool viewAlive);
    friend class ViewerContext;
    static void eventCallback(void* data, SoEventCallback* cb);
    void handleEvent(SoEventCallback* cb);

private:
    SoSeparator* root;
    SoTransform* transform;
    ViewerContext* viewer;
    QuantitySpinBox* spinBox;
    SoNodeSensor* cameraSensor;
    SbVec3f midpos;
    double editStartValue;
    bool lockedAppearance;

    Function function;

    SoEventCallback* clickCallback;
    SoPickStyle* pickStyle;
    fastsignals::scoped_connection connCameraReplaced;
};

}


#endif // GUI_EDITABLEDATUMLABEL_H
