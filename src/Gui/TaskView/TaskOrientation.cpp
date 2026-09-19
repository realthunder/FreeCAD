// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2023 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <map>
#endif

#include <Base/Tools.h>
#include <App/Document.h>
#include <Gui/Camera.h>
#include <Gui/Fw/FwQtView.h>
#include <Gui/Fw/FwWidgets.h>
#include <Gui/TaskView/TaskView.h>

#include "TaskOrientation.h"
#include "fwui_TaskOrientation.h"


using namespace Gui;

TaskOrientation::TaskOrientation(App::GeoFeature* obj, QObject* parent)
  : QObject(parent)
  , _form(new Fw::UiForm)
  , ui(new Ui_TaskOrientation)
  , feature(obj)
{
    ui->setupUi(_form.get());

    connect(ui->Reverse_checkBox, &Fw::QCheckBox::clicked,    this, &TaskOrientation::onPreview);
    connect(ui->XY_radioButton  , &Fw::QRadioButton::clicked, this, &TaskOrientation::onPreview);
    connect(ui->XZ_radioButton  , &Fw::QRadioButton::clicked, this, &TaskOrientation::onPreview);
    connect(ui->YZ_radioButton  , &Fw::QRadioButton::clicked, this, &TaskOrientation::onPreview);
    connect(ui->Offset_doubleSpinBox, qOverload<double>(&Fw::QuantitySpinBox::valueChanged),
            this, &TaskOrientation::onPreview);
}

TaskOrientation::~TaskOrientation() = default;

QString TaskOrientation::windowTitle() const
{
    return _form->windowTitle();
}

void TaskOrientation::open()
{
    if (!feature.expired()) {
        App::Document* doc = feature->getDocument();
        doc->openTransaction(QT_TRANSLATE_NOOP("Command", "Edit image"));
        restore(feature->Placement.getValue());
    }
}

void TaskOrientation::accept()
{
    if (!feature.expired()) {
        App::Document* doc = feature->getDocument();
        doc->commitTransaction();
        doc->recompute();
    }
}

void TaskOrientation::reject()
{
    if (!feature.expired()) {
        App::Document* doc = feature->getDocument();
        doc->abortTransaction();
        feature->purgeTouched();
    }
}

void TaskOrientation::onPreview()
{
    updateIcon();
    updatePlacement();
}

void TaskOrientation::restore(const Base::Placement& plm)
{
    auto isReversed = [](Camera::Orientation type) {
        return (type == Camera::Bottom) ||
               (type == Camera::Rear) ||
               (type == Camera::Left);
    };
    std::map<Camera::Orientation, Base::Rotation> rotations {
        {Camera::Top, Camera::convert(Camera::Top)},
        {Camera::Bottom, Camera::convert(Camera::Bottom)},
        {Camera::Front, Camera::convert(Camera::Front)},
        {Camera::Rear, Camera::convert(Camera::Rear)},
        {Camera::Right, Camera::convert(Camera::Right)},
        {Camera::Left, Camera::convert(Camera::Left)}
    };

    Base::Rotation rot = plm.getRotation();
    Base::Vector3d pos = plm.getPosition();

    double prec = 1.0e-5;
    for (const auto& it : rotations) {
        if (rot.isSame(it.second, prec)) {
            if (it.first == Camera::Top ||
                it.first == Camera::Bottom) {
                ui->XY_radioButton->setChecked(true);
                ui->Offset_doubleSpinBox->setValue(pos.z);
            }
            else if (it.first == Camera::Front ||
                     it.first == Camera::Rear) {
                ui->XZ_radioButton->setChecked(true);
                ui->Offset_doubleSpinBox->setValue(pos.y);
            }
            else if (it.first == Camera::Right ||
                     it.first == Camera::Left) {
                ui->YZ_radioButton->setChecked(true);
                ui->Offset_doubleSpinBox->setValue(pos.x);
            }

            if (isReversed(it.first)) {
                ui->Reverse_checkBox->setChecked(true);
            }
        }
    }

    onPreview();
}

void TaskOrientation::updatePlacement()
{
    Base::Placement Pos;
    double offset = ui->Offset_doubleSpinBox->value().getValue();
    bool reverse = ui->Reverse_checkBox->isChecked();
    if (ui->XY_radioButton->isChecked()) {
        if (!reverse) {
            Pos = Base::Placement(Base::Vector3d(0, 0, offset),
                                  Camera::convert(Camera::Top));
        }
        else {
            Pos = Base::Placement(Base::Vector3d(0, 0, offset),
                                  Camera::convert(Camera::Bottom));
        }
    }
    else if (ui->XZ_radioButton->isChecked()) {
        if (!reverse) {
            Pos = Base::Placement(Base::Vector3d(0, offset, 0),
                                  Camera::convert(Camera::Front));
        }
        else {
            Pos = Base::Placement(Base::Vector3d(0, offset, 0),
                                  Camera::convert(Camera::Rear));
        }
    }
    else if (ui->YZ_radioButton->isChecked()) {
        if (!reverse) {
            Pos = Base::Placement(Base::Vector3d(offset, 0, 0),
                                  Camera::convert(Camera::Right));
        }
        else {
            Pos = Base::Placement(Base::Vector3d(offset, 0, 0),
                                  Camera::convert(Camera::Left));
        }
    }

    if (!feature.expired()) {
        feature->Placement.setValue(Pos);
    }
}

void TaskOrientation::updateIcon()
{
    std::string icon;
    bool reverse = ui->Reverse_checkBox->isChecked();
    if (ui->XY_radioButton->isChecked()) {
        icon = reverse ? "view-bottom" : "view-top";
    }
    else if (ui->XZ_radioButton->isChecked()) {
        icon = reverse ? "view-rear" : "view-front";
    }
    else if (ui->YZ_radioButton->isChecked()) {
        icon = reverse ? "view-left" : "view-right";
    }

    // the bag carries the icon's name; the backend renders it at the
    // label's size (an SVG through the BitmapFactory on the Qt side)
    ui->previewLabel->setPixmap(QStringLiteral("bitmap:") + QString::fromStdString(icon));
}

// ----------------------------------------------------------------------------

TaskOrientationDialog::TaskOrientationDialog(App::GeoFeature* obj)
{
    widget = new TaskOrientation(obj, this);
    QWidget* realized = FwQt::realize(widget->form(), nullptr);
    Gui::TaskView::TaskBox* taskbox = new Gui::TaskView::TaskBox(
        QPixmap(), widget->windowTitle(), true, nullptr);
    taskbox->groupLayout()->addWidget(realized);
    Content.push_back(taskbox);
}

TaskOrientationDialog::~TaskOrientationDialog()
{
    // the dialog deletes its content, and the view watches its widget;
    // detach now so the models outlive the widgets cleanly
    if (FwQt::View* v = FwQt::View::of(widget->form()))
        v->release(false);
}

void TaskOrientationDialog::open()
{
    widget->open();
}

bool TaskOrientationDialog::accept()
{
    widget->accept();
    return true;
}

bool TaskOrientationDialog::reject()
{
    widget->reject();
    return true;
}

#include "moc_TaskOrientation.cpp"
