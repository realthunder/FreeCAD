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


#ifndef GUI_TASKVIEW_TaskPatternParameters_H
#define GUI_TASKVIEW_TaskPatternParameters_H

#include <App/Pattern.h>

#include "TaskTransformedParameters.h"
#include "ViewProviderTransformed.h"

class QComboBox;
class QGroupBox;
class QLabel;
class Ui_TaskPatternParameters;

namespace PartDesign {
class PatternFeature;
}

namespace PartDesignGui {

class TaskMultiTransformParameters;

/// The panel of a pattern of any kind: a linear pattern, one or two
/// directions, a polar pattern (upstream 5d2037c820 merged the two panels as
/// well), and a circular, path or point pattern on the shared editor of those
/// kinds. The kind may be changed here; the editors change with it.
class TaskPatternParameters : public TaskTransformedParameters
{
    Q_OBJECT

public:
    /// Constructor for task with ViewProvider
    explicit TaskPatternParameters(ViewProviderTransformed *TransformedView, QWidget *parent = nullptr);
    /// Constructor for task with parent task (MultiTransform mode)
    TaskPatternParameters(TaskMultiTransformParameters *parentTask, QLayout *layout);
    ~TaskPatternParameters() override;

    void apply() override;

private Q_SLOTS:
    void onUpdateView(bool) override;

protected:
    void changeEvent(QEvent *e) override;
    void onSelectionChanged(const Gui::SelectionChanges& msg) override;

private:
    void setupUI();
    /// The editors of the inputs of the kind the pattern has now
    void buildPatternWidgets();
    void onTypeActivated(int index);
    PartDesign::PatternFeature* getPattern() const;
    App::Pattern::Type getPatternType() const;
    void updateUI() override;
    void retranslate();
    bool isPolar() const;
    void fillReferenceCombo(Gui::ComboLinks& links);
    void showOriginAxes(bool show);
    void onReferenceActivated(Gui::ComboLinks& links, App::PropertyLinkSub* prop);
    void onParametersChanged();
    void onDirection2Toggled(bool on);
    void setDefaultDirection2();
    /// Put the directions' on-view labels where the pattern is now
    void updateLabels();

private:
    std::unique_ptr<Ui_TaskPatternParameters> ui;

    QLabel* labelType = nullptr;
    QComboBox* comboType = nullptr;
    /// The kind the editors were built for
    App::Pattern::Type builtType = App::Pattern::Type::Linear;

    QGroupBox* groupDirection1 = nullptr;
    QGroupBox* groupDirection2 = nullptr;
    Gui::PatternDirectionWidget* direction1 = nullptr;
    Gui::PatternDirectionWidget* direction2 = nullptr;
    /// The editor of a circular, path or point pattern
    Gui::PatternParametersWidget* parameters = nullptr;
    /// The reference being picked in the view
    App::PropertyLinkSub* picking = nullptr;
};


/// simulation dialog for the TaskView
class TaskDlgPatternParameters : public TaskDlgTransformedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgPatternParameters(ViewProviderTransformed *PatternView);
    ~TaskDlgPatternParameters() override = default;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TaskPatternParameters_H
