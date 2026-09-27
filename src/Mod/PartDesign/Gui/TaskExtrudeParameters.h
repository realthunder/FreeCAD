/***************************************************************************
 *   Copyright (c) 2021 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef GUI_TASKVIEW_TaskExtrudeParameters_H
#define GUI_TASKVIEW_TaskExtrudeParameters_H

#include <Gui/Inventor/Draggers/Gizmo.h>

#include "TaskSketchBasedParameters.h"
#include "ViewProviderSketchBased.h"


namespace App {
class Property;
}

namespace Gui {
namespace Fw {
class QComboBox;
class UiForm;
class Widget;
}
}

namespace PartDesign {
class ProfileBased;
}

namespace PartDesignGui {

class Ui_TaskPadPocketParameters;

/* Pad's and Pocket's form is models of the host widget layer
 * (docs/Sandbox.md 7.12, H1b): `ui` is generated from
 * TaskPadPocketParameters.ui by src/Tools/fwuic.py, its fields
 * `Gui::Fw::QuantitySpinBox` and `Gui::Fw::DoubleSpinBox` with the
 * expression binding on the model; `proxy` is the Qt backend's
 * rendering of `form`.  The box around it, and the widgets
 * TaskSketchBasedParameters builds in code, stay Qt. */
class TaskExtrudeParameters : public TaskSketchBasedParameters
{
    Q_OBJECT

    enum DirectionModes {
        Normal,
        Select,
        Custom,
        Reference
    };

public:
    TaskExtrudeParameters(ViewProviderSketchBased *SketchBasedView, QWidget *parent,
                          const std::string& pixmapname, const QString& parname);
    ~TaskExtrudeParameters() override;

    void saveHistory() override;
    void refresh() override;

    void fillDirectionCombo();
    void applyParameters();

    virtual bool isPocket() {
        return false;
    }

    /// The types of a side, as the panel lists them: the first four Types of
    /// Pad and Pocket, and ToFace for both UpToFace and UpToShape, which the
    /// feature sets by the reference (one face, or more)
    enum class Modes {
        Dimension,
        ThroughAll,
        ToLast = ThroughAll,
        ToFirst,
        ToFace,
    };

protected:
    void onStartModeChanged(int);
    void onStartOffsetChanged(double);
    void onSideTypeChanged(int);
    void onMode2Changed(int);
    void onLengthChanged(double);
    void onLength2Changed(double);
    void onOffsetChanged(double);
    void onOffset2Changed(double);
    void onTaperChanged(double);
    void onTaper2Changed(double);
    void onInnerAngleChanged(double);
    void onInnerAngle2Changed(double);
    void onDirectionCBChanged(int);
    void onAlongSketchNormalChanged(bool);
    void onDirectionToggled(bool);
    void onXDirectionEditChanged(double);
    void onYDirectionEditChanged(double);
    void onZDirectionEditChanged(double);
    void onReversedChanged(bool);
    void onUsePipeChanged(bool);
    void onCheckFaceLimitsChanged(bool);

protected:
    void changeEvent(QEvent *e) override;
    bool eventFilter(QObject *o, QEvent *ev) override;
    void _onSelectionChanged(const Gui::SelectionChanges& msg) override;

    void setCheckboxes();
    /// Show the start rows StartType asks for
    void updateStartUI();
    void setupDialog(bool newObj, const char *historyPath);
    void readValuesFromHistory();
    App::PropertyLinkSub* propReferenceAxis;
    void getReferenceAxis(App::DocumentObject*& obj, std::vector<std::string>& sub) const;

    double getOffset() const;
    bool   getAlongSketchNormal() const;
    bool   getCustom() const;
    std::string getReferenceAxis() const;
    double getXDirection() const;
    double getYDirection() const;
    double getZDirection() const;
    bool   getReversed() const;
    int    getMode() const;
    void updateDirectionEdits();
    void setDirectionMode(int index);
    void addAxisToCombo(App::DocumentObject* linkObj, const std::string &linkSubname, const QString &itemText);

    /// Set one side's Type for a mode of the panel's list
    static void setSideMode(App::PropertyEnumeration &type,
                            const App::PropertyLinkSubList &upToShape, int mode);
    /// The mode of the panel's list for one side's Type
    static int modeOf(const App::PropertyEnumeration &type);

    virtual void onModeChanged(int);
    virtual void translateTooltips();
    /// Fill both sides' type lists
    void translateModeList();
    /// Fill one side's type list, in the order of Modes
    virtual void fillModeList(Gui::Fw::QComboBox *combo);
    /// The text of the up-to reference's pick button
    virtual QString upToTitle() const;

    LinkSubWidget *upToWidget = nullptr;
    LinkSubWidget *upToWidget2 = nullptr;
    /// The start reference: a face, a datum plane or a sketch
    LinkSubWidget *startWidget = nullptr;

private:
    void tryRecomputeFeature();
    void connectSlots();
    bool hasProfileFace(PartDesign::ProfileBased*) const;
    void selectedReferenceAxis(const Gui::SelectionChanges& msg);
    LinkSubWidget *makeUpToWidget(Gui::Fw::Widget *holder, App::PropertyLinkSubList &prop,
                                  SelectionMode mode);

    std::unique_ptr<Gui::GizmoContainer> gizmoContainer;
    Gui::LinearGizmo* lengthGizmo1 = nullptr;
    Gui::LinearGizmo* lengthGizmo2 = nullptr;
    /// Drags StartOffset from the plane it is measured from (upstream a01fad4f53)
    Gui::LinearGizmo* startOffsetGizmo = nullptr;
    Gui::RotationGizmo* taperAngleGizmo1 = nullptr;
    Gui::RotationGizmo* taperAngleGizmo2 = nullptr;
    void setupGizmos();
    void setGizmoPositions();

protected:
    void finishedRecomputeFeature() override;

    QWidget* proxy;
    std::unique_ptr<Gui::Fw::UiForm> form;
    std::unique_ptr<Ui_TaskPadPocketParameters> ui;
    bool selectionFace;
    std::vector<App::SubObjectT> axesInList;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskExtrudeParameters_H
