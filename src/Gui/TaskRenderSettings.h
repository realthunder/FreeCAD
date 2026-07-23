/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#ifndef GUI_TASKRENDERSETTINGS_H
#define GUI_TASKRENDERSETTINGS_H

#include <vector>
#include <QWidget>
#include "TaskView/TaskDialog.h"
#include "TaskView/TaskView.h"

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;

namespace Gui {

class ViewProviderGeometryObject;

/// Editor of the per-object render engine settings: creates, updates
/// or removes the optional Render_* dynamic properties (PBR
/// metallic/roughness, water body, base color / normal map textures
/// with their transform, shadow casting flags) on the selected
/// geometry view providers — the friendly front end to what the
/// property editor's add-property dialog can do by hand.
class RenderSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RenderSettingsWidget(
            std::vector<ViewProviderGeometryObject*> &&vps);

    bool accept();

private:
    void load();
    void apply(ViewProviderGeometryObject *vp);
    void browse(QLineEdit *edit);

private:
    std::vector<ViewProviderGeometryObject*> vps;

    QCheckBox *metallicCheck = nullptr;
    QDoubleSpinBox *metallicSpin = nullptr;
    QCheckBox *roughnessCheck = nullptr;
    QDoubleSpinBox *roughnessSpin = nullptr;
    QCheckBox *waterCheck = nullptr;
    QDoubleSpinBox *waterDensitySpin = nullptr;
    QCheckBox *glassCheck = nullptr;
    QDoubleSpinBox *glassIORSpin = nullptr;
    QDoubleSpinBox *glassDensitySpin = nullptr;
    QDoubleSpinBox *glassRoughSpin = nullptr;
    QCheckBox *cloudCheck = nullptr;
    QDoubleSpinBox *cloudDensitySpin = nullptr;
    QDoubleSpinBox *cloudDetailSpin = nullptr;
    QDoubleSpinBox *cloudSpeedSpin = nullptr;
    QCheckBox *fireCheck = nullptr;
    QDoubleSpinBox *fireIntensitySpin = nullptr;
    QDoubleSpinBox *fireDetailSpin = nullptr;
    QDoubleSpinBox *fireSpeedSpin = nullptr;
    QCheckBox *fountainCheck = nullptr;
    QDoubleSpinBox *fountainDensitySpin = nullptr;
    QDoubleSpinBox *fountainDetailSpin = nullptr;
    QDoubleSpinBox *fountainSpeedSpin = nullptr;
    QCheckBox *lightCheck = nullptr;
    QDoubleSpinBox *lightIntensitySpin = nullptr;
    QDoubleSpinBox *lightRangeSpin = nullptr;
    QCheckBox *lightShadowCheck = nullptr;
    QCheckBox *lightShadowExtCheck = nullptr;
    QCheckBox *baseColorCheck = nullptr;
    QLineEdit *baseColorEdit = nullptr;
    QCheckBox *normalMapCheck = nullptr;
    QLineEdit *normalMapEdit = nullptr;
    QCheckBox *emissiveMapCheck = nullptr;
    QLineEdit *emissiveMapEdit = nullptr;
    QCheckBox *occlusionMapCheck = nullptr;
    QLineEdit *occlusionMapEdit = nullptr;
    QCheckBox *metallicRoughnessMapCheck = nullptr;
    QLineEdit *metallicRoughnessMapEdit = nullptr;
    QCheckBox *texScaleCheck = nullptr;
    QDoubleSpinBox *texScaleX = nullptr;
    QDoubleSpinBox *texScaleY = nullptr;
    QCheckBox *texOffsetCheck = nullptr;
    QDoubleSpinBox *texOffsetX = nullptr;
    QDoubleSpinBox *texOffsetY = nullptr;
    QCheckBox *texRotationCheck = nullptr;
    QDoubleSpinBox *texRotationSpin = nullptr;
    QCheckBox *castShadowCheck = nullptr;
    QCheckBox *receiveShadowCheck = nullptr;
};

/// Task panel wrapper shown from the geometry object context menu
/// ("Render settings...").
class GuiExport TaskRenderSettings : public TaskView::TaskDialog
{
    Q_OBJECT

public:
    TaskRenderSettings();
    ~TaskRenderSettings() override;

    bool accept() override;
    bool reject() override;

    QDialogButtonBox::StandardButtons getStandardButtons() const override
    { return QDialogButtonBox::Ok | QDialogButtonBox::Cancel; }

    /// Selected geometry view providers the panel would edit.
    static std::vector<ViewProviderGeometryObject*> selectedViewProviders();

private:
    RenderSettingsWidget *widget;
    TaskView::TaskBox *taskbox;
};

} // namespace Gui

#endif // GUI_TASKRENDERSETTINGS_H
