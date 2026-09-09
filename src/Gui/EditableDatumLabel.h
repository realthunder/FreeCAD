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
#include <Gui/QuantitySpinBox.h>

#include "SoDatumLabel.h"

#include <FCGlobal.h>

class SoNodeSensor;
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
        Dimensioning
    };

    EditableDatumLabel(ViewerContext* view, const Base::Placement& plc, SbColor color, bool autoDistance = false, bool avoidMouseCursor = false);

    ~EditableDatumLabel() override;

    void activate();
    void deactivate();

    void startEdit(double val, QObject* eventFilteringObj = nullptr, bool visibleToMouse = false);
    void stopEdit();
    bool isActive() const;
    bool isInEdit() const;
    double getValue() const;
    void setSpinboxValue(double val, const Base::Unit& unit = Base::Unit::Length);
    void setPlacement(const Base::Placement& plc);
    void setColor(SbColor color);
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
    /// What selectNumber() left selected, so the client can show the same.
    void getSelection(int& start, int& length) const;
    /// Deliver a key to the box, the desktop's focus having done it there.
    bool sendKeyEvent(QKeyEvent* event);
    //@}

    // NOLINTBEGIN
    SoDatumLabel* label;
    bool isSet;
    bool autoDistance;
    bool autoDistanceReverse;
    bool avoidMouseCursor;
    double value;
    // NOLINTEND

Q_SIGNALS:
    void valueChanged(double val);

private:
    void positionSpinbox();
    SbVec3f getTextCenterPoint() const;
    /// Tell the view its on-view set moved, so a mirror can restate it.
    void notifyChanged();

private:
    SoSeparator* root;
    SoTransform* transform;
    ViewerContext* viewer;
    QuantitySpinBox* spinBox;
    SoNodeSensor* cameraSensor;
    SbVec3f midpos;

    Function function;
};

}


#endif // GUI_EDITABLEDATUMLABEL_H
