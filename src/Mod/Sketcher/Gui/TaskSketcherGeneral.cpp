/***************************************************************************
 *   Copyright (c) 2009 Jürgen Riegel <juergen.riegel@web.de>              *
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

#include <QEvent>

#include <Base/Tools.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/ViewProvider.h>

#include <Mod/Part/Gui/PartParams.h>
#include "ui_TaskSketcherGeneral.h"
#include "TaskSketcherGeneral.h"
#include "ViewProviderSketch.h"


using namespace SketcherGui;
using namespace Gui::TaskView;
namespace bp = boost::placeholders;

SketcherGeneralWidget::SketcherGeneralWidget(QWidget *parent)
  : QWidget(parent), ui(new Ui_TaskSketcherGeneral)
{
    ui->setupUi(this);
    loadSettings();
}

SketcherGeneralWidget::~SketcherGeneralWidget()
{
    saveSettings();
}

void SketcherGeneralWidget::saveSettings()
{
    ui->showOriginalColor->onSave();
    ui->autoTransparentPick->onSave();
    ui->checkBoxAllowFace->onSave();
}

void SketcherGeneralWidget::loadSettings()
{
    ui->showOriginalColor->initAutoSave();
    ui->autoTransparentPick->initAutoSave();
    ui->checkBoxAllowFace->initAutoSave();
}

void SketcherGeneralWidget::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
}

// ----------------------------------------------------------------------------

TaskSketcherGeneral::TaskSketcherGeneral(ViewProviderSketch *sketchView)
    : TaskBox(Gui::BitmapFactory().pixmap("document-new"),tr("Edit controls"),true, nullptr)
    , sketchView(sketchView)
{
    // we need a separate container widget to add all controls to
    widget = new SketcherGeneralWidget(this);
    this->groupLayout()->addWidget(widget);
    QSignalBlocker block(widget);
    
    Gui::SelectionRoom().Attach(this);
}

TaskSketcherGeneral::~TaskSketcherGeneral()
{
    Gui::SelectionRoom().Detach(this);
}

/// @cond DOXERR
void TaskSketcherGeneral::OnChange(Gui::SelectionSingleton::SubjectType &rCaller,
                                   Gui::SelectionSingleton::MessageType Reason)
{
    Q_UNUSED(rCaller);
    Q_UNUSED(Reason);
    //if (Reason.Type == SelectionChanges::AddSelection ||
    //    Reason.Type == SelectionChanges::RmvSelection ||
    //    Reason.Type == SelectionChanges::SetSelection ||
    //    Reason.Type == SelectionChanges::ClrSelection) {
    //}
}
/// @endcond DOXERR

#include "moc_TaskSketcherGeneral.cpp"
