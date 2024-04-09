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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <Precision.hxx>
#endif

#include <Mod/PartDesign/App/FeaturePad.h>
#include <Mod/PartDesign/App/FeatureExtrusion.h>

#include "ui_TaskPadPocketParameters.h"
#include "TaskPadParameters.h"
#include "ReferenceSelection.h"
#include "Utils.h"
#include "ViewProviderExtrusion.h"

using namespace PartDesignGui;
using namespace Gui;

/* TRANSLATOR PartDesignGui::TaskPadParameters */

TaskPadParameters::TaskPadParameters(ViewProviderPad *PadView,
                                     QWidget *parent,
                                     bool newObj,
                                     const std::string& pixmapname,
                                     const QString& parname,
                                     const char *historyPath)
    : TaskExtrudeParameters(PadView, parent, pixmapname, parname)
{
    setupDialog(newObj, historyPath);
}

TaskPadParameters::~TaskPadParameters() = default;

void TaskPadParameters::translateModeList(int index)
{
    ui->changeMode->clear();
    ui->changeMode->addItem(tr("Dimension"));
    ui->changeMode->addItem(tr("To last"));
    ui->changeMode->addItem(tr("To first"));
    if (vp && vp->isDerivedFrom(ViewProviderExtrusion::getClassTypeId()))
        ui->changeMode->addItem(tr("Up to Element"));
    else
        ui->changeMode->addItem(tr("Up to face"));
    ui->changeMode->addItem(tr("Two dimensions"));
    ui->changeMode->setCurrentIndex(index);
}


void TaskPadParameters::translateFaceNamePlaceHolder()
{
    if (vp && vp->isDerivedFrom(ViewProviderExtrusion::getClassTypeId())) {
        ui->buttonFace->setText(tr("Element"));
        ui->lineFaceName->setPlaceholderText(tr("No element selected"));
    }
    else {
        TaskExtrudeParameters::translateFaceNamePlaceHolder();
    }
}

void TaskPadParameters::onModeChanged(int index)
{
    PartDesign::Pad* pcPad = static_cast<PartDesign::Pad*>(vp->getObject());

    switch (static_cast<Modes>(index)) {
    case Modes::Dimension:
        pcPad->Type.setValue("Length");
        // Avoid error message
        if (ui->lengthEdit->value() < Base::Quantity(Precision::Confusion(), Base::Unit::Length))
            ui->lengthEdit->setValue(5.0);
        break;
    case Modes::ToLast:
        pcPad->Type.setValue("UpToLast");
        break;
    case Modes::ToFirst:
        pcPad->Type.setValue("UpToFirst");
        break;
    case Modes::ToFace:
        // Note: ui->checkBoxReversed is purposely enabled because the selected face
        // could be a circular one around the sketch
        pcPad->Type.setValue("UpToFace");
        if (ui->lineFaceName->text().isEmpty()) {
            ui->buttonFace->setChecked(true);
        }
        if (!pcPad->UpToFace.getValue()) {
            setCheckboxes();
            return;
        }
        break;
    case Modes::TwoDimensions:
        pcPad->Type.setValue("TwoLengths");
        break;
    }

    setCheckboxes();
    recomputeFeature();
}

void TaskPadParameters::apply()
{
    QString facename = QStringLiteral("None");
    if (static_cast<Modes>(getMode()) == Modes::ToFace) {
        facename = getFaceName();
    }
    applyParameters(facename);
}

//**************************************************************************
//**************************************************************************
// TaskDialog
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TaskDlgPadParameters::TaskDlgPadParameters(ViewProviderPad *PadView, bool newObj)
    : TaskDlgSketchBasedParameters(PadView)
{
    assert(vp);
    if (vp->isDerivedFrom(ViewProviderExtrusion::getClassTypeId()))
        Content.push_back ( new TaskPadParameters(PadView, nullptr, newObj,
                    "PartDesign_Extrusion", tr("Extrusion parameters"), "User parameter:BaseApp/History/Extrusion/"));
    else
        Content.push_back ( new TaskPadParameters(PadView, nullptr, newObj,
                    "PartDesign_Pad", tr("Pad parameters"), "User parameter:BaseApp/History/Pad/"));
}

bool TaskDlgPadParameters::accept() {
    if (vp && vp->isDerivedFrom(ViewProviderExtrusion::getClassTypeId()))
        return TaskDlgFeatureParameters::accept();
    return TaskDlgSketchBasedParameters::accept();
}

//==== calls from the TaskView ===============================================================

#include "moc_TaskPadParameters.cpp"
