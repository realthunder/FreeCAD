/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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
# include <QMessageBox>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/PrefWidgets.h>

#include "DlgSettings3DViewPartImp.h"
#include "ui_DlgSettings3DViewPart.h"
#include "PartParams.h"
#include "ViewProvider.h"

using namespace PartGui;

/**
 *  Constructs a DlgSettings3DViewPart which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 */
DlgSettings3DViewPart::DlgSettings3DViewPart(QWidget* parent)
  : PreferencePage(parent), ui(new Ui_DlgSettings3DViewPart), checkValue(false)
{
    ui->setupUi(this);

    ui->maxDeviation->setMinimum(PartParams::getMinimumDeviation());
    ui->maxDeviation->setValue(PartParams::getMeshDeviation());
    ui->maxAngularDeflection->setMinimum(PartParams::getMinimumAngularDeflection());
    ui->maxAngularDeflection->setValue(PartParams::getMeshAngularDeflection());
    ui->deviationLowerBound->setValue(PartParams::getMinimumDeviation());
    ui->angularDeflectionLowerBound->setValue(PartParams::getMinimumAngularDeflection());
    ui->checkBoxMapFaceColor->setChecked(PartParams::getMapFaceColor());
    ui->checkBoxMapLineColor->setChecked(PartParams::getMapLineColor());
    ui->checkBoxMapPointColor->setChecked(PartParams::getMapPointColor());
    ui->checkBoxMapTransparency->setChecked(PartParams::getMapTransparency());

    connect(ui->deviationLowerBound, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &DlgSettings3DViewPart::onLowerBoundChanged);
    connect(ui->angularDeflectionLowerBound, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &DlgSettings3DViewPart::onLowerBoundChanged);
    connect(ui->maxDeviation, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &DlgSettings3DViewPart::onMaxDeviationValueChanged);
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettings3DViewPart::~DlgSettings3DViewPart() = default;

void DlgSettings3DViewPart::onMaxDeviationValueChanged(double v)
{
    if (!this->isVisible())
        return;
    if (v < 0.01 && !checkValue) {
        checkValue = true;
        QMessageBox::warning(this, tr("Deviation"),
            tr("Setting a too small deviation causes the tessellation to take longer"
               "and thus freezes or slows down the GUI."));
    }
}

void DlgSettings3DViewPart::onLowerBoundChanged(double)
{
    ui->maxDeviation->setMinimum(ui->deviationLowerBound->value());
    ui->maxAngularDeflection->setMinimum(ui->angularDeflectionLowerBound->value());
}

void DlgSettings3DViewPart::saveSettings()
{
    ui->maxDeviation->onSave();
    ui->maxAngularDeflection->onSave();
    ui->deviationLowerBound->onSave();
    ui->angularDeflectionLowerBound->onSave();
    ui->checkBoxOverride->onSave();
    ui->checkBoxMapFaceColor->onSave();
    ui->checkBoxMapLineColor->onSave();
    ui->checkBoxMapPointColor->onSave();
    ui->checkBoxMapTransparency->onSave();
    ui->checkBoxColorRecompute->onSave();
}

void DlgSettings3DViewPart::loadSettings()
{
    ui->maxDeviation->onRestore();
    ui->maxAngularDeflection->onRestore();
    ui->deviationLowerBound->onRestore();
    ui->angularDeflectionLowerBound->onRestore();
    ui->checkBoxOverride->onRestore();
    ui->checkBoxMapFaceColor->onRestore();
    ui->checkBoxMapLineColor->onRestore();
    ui->checkBoxMapPointColor->onRestore();
    ui->checkBoxMapTransparency->onRestore();
    ui->checkBoxColorRecompute->onRestore();
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgSettings3DViewPart::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

#include "moc_DlgSettings3DViewPartImp.cpp"
