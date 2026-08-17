/***************************************************************************
 *   Copyright (c) 2015 Stefan Tröger <stefantroeger@gmx.net>              *
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

#ifndef GUI_TASKVIEW_TaskPipeParameters_H
#define GUI_TASKVIEW_TaskPipeParameters_H

#include <fastsignals/signal.h>

#include "TaskSketchBasedParameters.h"
#include "ViewProviderPipe.h"
#include "TaskDressUpParameters.h"

class QListWidget;
class QListWidgetItem;
class QTreeWidgetItem;

namespace App {
class Property;
}

namespace Gui {
class ViewProvider;
}

namespace PartDesignGui {

class Ui_TaskPipeParameters;
class Ui_TaskPipeOrientation;
class Ui_TaskPipeScaling;

class TaskPipeParameters : public TaskSketchBasedParameters
{
    Q_OBJECT

public:
    explicit TaskPipeParameters(ViewProviderPipe *PipeView, bool newObj=false, QWidget *parent = nullptr);
    ~TaskPipeParameters() override;

private:
    void onTangentChanged(bool checked);
    void onTransitionChanged(int);
    void updateUI();

protected:
    void refresh() override;
    void onProfileOptionChanged();

public:
    ViewProviderPipe* getPipeView() const
    { return static_cast<ViewProviderPipe*>(vp); }

private:
    QWidget* proxy;
    std::unique_ptr<Ui_TaskPipeParameters> ui;
    LinkSubWidget *spineWidget = nullptr;
};

class TaskPipeOrientation : public TaskSketchBasedParameters
{
    Q_OBJECT

public:
    explicit TaskPipeOrientation(ViewProviderPipe *PipeView, bool newObj=false, QWidget *parent = nullptr);
    ~TaskPipeOrientation() override;


private:
    void onOrientationChanged(int);
    void updateUI(int idx);
    void onCurvelinearChanged(bool checked);
    void onBinormalChanged(double);
  
protected:
    void refresh() override;

private:
    QWidget* proxy;
    std::unique_ptr<Ui_TaskPipeOrientation> ui;
    LinkSubWidget *auxSpineWidget = nullptr;
};


class TaskPipeScaling : public TaskSketchBasedParameters
{
    Q_OBJECT

public:
    explicit TaskPipeScaling(ViewProviderPipe *PipeView,bool newObj=false,QWidget *parent = nullptr);
    ~TaskPipeScaling() override;

private:
    void onScalingChanged(int);
    void updateUI(int idx);
    void refresh() override;

private:
    QWidget* proxy;
    std::unique_ptr<Ui_TaskPipeScaling> ui;
    LinkSubListWidget *sectionWidget = nullptr;
};

/// simulation dialog for the TaskView
class TaskDlgPipeParameters : public TaskDlgSketchBasedParameters
{
    Q_OBJECT

public:
    explicit TaskDlgPipeParameters(ViewProviderPipe *PipeView,bool newObj=false);
    ~TaskDlgPipeParameters() override;

    ViewProviderPipe* getPipeView() const
    { return static_cast<ViewProviderPipe*>(vp); }


public:
    /// is called by the framework if the dialog is accepted (Ok)
    bool accept() override;
    /// is called by the framework if the dialog is rejected (Cancel)

    void onSelectionModeChanged(TaskSketchBasedParameters *);

protected:
    TaskPipeParameters  *parameter;
    TaskPipeOrientation *orientation;
    TaskPipeScaling     *scaling;

private:
    bool _busy = false;
};

} //namespace PartDesignGui

#endif // GUI_TASKVIEW_TASKAPPERANCE_H
