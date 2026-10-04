// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Max Wilfinger
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#include <Gui/Selection.h>
#include <Mod/PartDesign/App/FeatureDefeaturing.h>

#include "ui_TaskDefeaturingParameters.h"
#include "TaskDefeaturingParameters.h"

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskDefeaturingParameters */

TaskDefeaturingParameters::TaskDefeaturingParameters(ViewProviderDressUp *DressUpView, QWidget *parent)
    : TaskDressUpParameters(DressUpView, false, true, parent)
    , ui(new Ui_TaskDefeaturingParameters)
{
    // we need a separate container widget to add all controls to
    proxy = new QWidget(this);
    ui->setupUi(proxy);
    this->groupLayout()->addWidget(proxy);

    setup(ui->message, ui->treeWidgetReferences, ui->buttonRefAdd);
}

TaskDefeaturingParameters::~TaskDefeaturingParameters()
{
    Gui::Selection().rmvSelectionGate();
}

void TaskDefeaturingParameters::changeEvent(QEvent *e)
{
    TaskBox::changeEvent(e);
    if (e->type() == QEvent::LanguageChange)
        ui->retranslateUi(proxy);
}


//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgDefeaturingParameters::TaskDlgDefeaturingParameters(ViewProviderDefeaturing *DressUpView)
    : TaskDlgDressUpParameters(DressUpView)
{
    parameter = new TaskDefeaturingParameters(DressUpView);

    Content.push_back(parameter);
}

TaskDlgDefeaturingParameters::~TaskDlgDefeaturingParameters() = default;

#include "moc_TaskDefeaturingParameters.cpp"
