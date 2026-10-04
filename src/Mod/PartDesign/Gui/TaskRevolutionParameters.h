/***************************************************************************
 *   Copyright (c) 2011 Juergen Riegel <FreeCAD@juergen-riegel.net>        *
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

#ifndef GUI_TASKVIEW_TaskRevolutionParameters_H
#define GUI_TASKVIEW_TaskRevolutionParameters_H

#include <Mod/PartDesign/App/FeatureRevolved.h>
#include <Gui/Inventor/Draggers/Gizmo.h>

#include "TaskSketchBasedParameters.h"
#include "ViewProviderRevolution.h"


class Ui_TaskRevolutionParameters;
class QComboBox;

namespace App {
class Property;
}

namespace Gui {
class ViewProvider;
}

namespace PartDesignGui {

class LinkSubWidget;

class TaskRevolutionParameters : public TaskSketchBasedParameters
{
    Q_OBJECT

public:
    explicit TaskRevolutionParameters(ViewProvider* RevolutionView, QWidget* parent = nullptr);
    ~TaskRevolutionParameters() override;

    void apply() override;

    /**
     * @brief fillAxisCombo fills the combo and selects the item according to
     * current value of revolution object's axis reference.
     * @param forceRefill if true, the combo box will be completely refilled. If
     * false, the current value of revolution object's axis will be added to the
     * list (if necessary), and selected. If the list is empty, it will be refilled anyway.
     */
    void fillAxisCombo(bool forceRefill = false);
    void addAxisToCombo(App::DocumentObject *linkObj, std::string linkSubname, QString itemText);

private Q_SLOTS:
    void onAngleChanged(double);
    void onAngle2Changed(double);
    void onAxisChanged(int);
    void onReversed(bool);
    void onProjectAxisChanged(bool);
    void onModeChanged(int);
    void onMode2Changed(int);
    void onSideTypeChanged(int);
    void onStartModeChanged(int);
    void onStartOffsetChanged(double);

protected:
    void onSelectionModeChanged(SelectionMode) override;
    void _onSelectionChanged(const Gui::SelectionChanges& msg) override;
    void changeEvent(QEvent *e) override;
    void getReferenceAxis(App::DocumentObject *&obj, std::vector<std::string> &sub) const;
    void onAxisButton(bool checked);

    bool eventFilter(QObject *o, QEvent *ev) override;

    void refresh() override;
    void finishedRecomputeFeature() override;

private:
    PartDesign::Revolved* getRevolved() const;
    void connectSignals();
    void updateUI();
    /// Show the rows the side type and the types use, and pick an up-to
    /// face that is wanted and missing
    void setCheckboxes();
    void updateStartUI();
    /// One side's type list: the Type values but TwoAngles, which is two sides
    void translateModeList(QComboBox *combo, int index);
    LinkSubWidget *makeUpToWidget(QWidget *holder, App::PropertyLinkSub &prop,
                                  SelectionMode mode);

private:
    std::unique_ptr<Ui_TaskRevolutionParameters> ui;
    QWidget *proxy;
    bool isGroove;

    /// The up-to faces, one per side, and the start reference
    LinkSubWidget *upToWidget = nullptr;
    LinkSubWidget *upToWidget2 = nullptr;
    LinkSubWidget *startWidget = nullptr;

    double defaultGizmoMultFactor = 1.0;
    std::unique_ptr<Gui::GizmoContainer> gizmoContainer;
    Gui::RadialGizmo* rotationGizmo = nullptr;
    Gui::RadialGizmo* rotationGizmo2 = nullptr;
    Gui::RotationGizmo* startOffsetGizmo = nullptr;
    void setupGizmos(ViewProvider* vp);
    void setGizmoPositions();

    /**
     * @brief axesInList is the list of links corresponding to axis combo; must
     * be kept in sync with the combo. A special value of zero-pointer link is
     * for "Select axis" item.
     *
     * It is a list of pointers, because properties prohibit assignment. Use new
     * when adding stuff, and delete when removing stuff.
     */
    std::vector<std::unique_ptr<App::PropertyLinkSub>> axesInList;
};

/// simulation dialog for the TaskView
class TaskDlgRevolutionParameters : public TaskDlgSketchBasedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgRevolutionParameters(PartDesignGui::ViewProvider *RevolutionView);

    ViewProvider* getRevolutionView() const
    {
        return vp;
    }
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TASKAPPERANCE_H
