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

#include "PreCompiled.h"

/*[[[cog
import DlgSettingsRender
DlgSettingsRender.define()
]]]*/

// Auto generated code (Tools/params_utils.py:605)
#ifndef _PreComp_
#   include <QApplication>
#   include <QLabel>
#   include <QGroupBox>
#   include <QGridLayout>
#   include <QVBoxLayout>
#   include <QHBoxLayout>
#endif
#include <Gui/RenderParams.h>
// Auto generated code (Tools/params_utils.py:627)
#include "Gui/PreferencePages/DlgSettingsRender.h"
using namespace Gui::Dialog;
/* TRANSLATOR Gui::Dialog::DlgSettingsRender */

// Auto generated code (Tools/params_utils.py:636)
DlgSettingsRender::DlgSettingsRender(QWidget* parent)
    : PreferencePage( parent )
{

    QHBoxLayout *layoutRow = nullptr;
    auto layout = new QVBoxLayout(this);


    // Auto generated code (Tools/params_utils.py:448)
    groupGeneral = new QGroupBox(this);
    layout->addWidget(groupGeneral);
    auto layoutHorizGeneral = new QHBoxLayout(groupGeneral);
    auto layoutGeneral = new QVBoxLayout();
    layoutHorizGeneral->addLayout(layoutGeneral);
    layoutHorizGeneral->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutGeneral->addLayout(layoutRow);
    labelType = new QLabel(this);
    layoutRow->addWidget(labelType);
    Type = new Gui::PrefLineEdit(this);
    layoutRow->addWidget(Type);
    Type->setText(QString::fromUtf8(Gui::RenderParams::defaultType().c_str()));
    Type->setEntryName("Type");
    Type->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupAmbientocclusion = new QGroupBox(this);
    layout->addWidget(groupAmbientocclusion);
    auto layoutHorizAmbientocclusion = new QHBoxLayout(groupAmbientocclusion);
    auto layoutAmbientocclusion = new QVBoxLayout();
    layoutHorizAmbientocclusion->addLayout(layoutAmbientocclusion);
    layoutHorizAmbientocclusion->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    AO = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(AO);
    AO->setChecked(Gui::RenderParams::defaultAO());
    AO->setEntryName("AO");
    AO->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelAOMethod = new QLabel(this);
    layoutRow->addWidget(labelAOMethod);
    AOMethod = new Gui::PrefComboBox(this);
    layoutRow->addWidget(AOMethod);
    AOMethod->setEntryName("AOMethod");
    AOMethod->setParamGrpPath("View/Render");
    for (int i=0; i<2; ++i) // Auto generated code (Tools/params_utils.py:1127)
        AOMethod->addItem(QString());
    AOMethod->setCurrentIndex(Gui::RenderParams::defaultAOMethod());

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelAOSlices = new QLabel(this);
    layoutRow->addWidget(labelAOSlices);
    AOSlices = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(AOSlices);
    AOSlices->setValue(Gui::RenderParams::defaultAOSlices());
    AOSlices->setEntryName("AOSlices");
    AOSlices->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelAOSteps = new QLabel(this);
    layoutRow->addWidget(labelAOSteps);
    AOSteps = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(AOSteps);
    AOSteps->setValue(Gui::RenderParams::defaultAOSteps());
    AOSteps->setEntryName("AOSteps");
    AOSteps->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelAORadius = new QLabel(this);
    layoutRow->addWidget(labelAORadius);
    AORadius = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(AORadius);
    AORadius->setValue(Gui::RenderParams::defaultAORadius());
    AORadius->setEntryName("AORadius");
    AORadius->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelAOIntensity = new QLabel(this);
    layoutRow->addWidget(labelAOIntensity);
    AOIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(AOIntensity);
    AOIntensity->setValue(Gui::RenderParams::defaultAOIntensity());
    AOIntensity->setEntryName("AOIntensity");
    AOIntensity->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupPhysicallybasedshading = new QGroupBox(this);
    layout->addWidget(groupPhysicallybasedshading);
    auto layoutHorizPhysicallybasedshading = new QHBoxLayout(groupPhysicallybasedshading);
    auto layoutPhysicallybasedshading = new QVBoxLayout();
    layoutHorizPhysicallybasedshading->addLayout(layoutPhysicallybasedshading);
    layoutHorizPhysicallybasedshading->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPhysicallybasedshading->addLayout(layoutRow);
    PBR = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(PBR);
    PBR->setChecked(Gui::RenderParams::defaultPBR());
    PBR->setEntryName("PBR");
    PBR->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPhysicallybasedshading->addLayout(layoutRow);
    labelPBRMetallic = new QLabel(this);
    layoutRow->addWidget(labelPBRMetallic);
    PBRMetallic = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(PBRMetallic);
    PBRMetallic->setValue(Gui::RenderParams::defaultPBRMetallic());
    PBRMetallic->setEntryName("PBRMetallic");
    PBRMetallic->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPhysicallybasedshading->addLayout(layoutRow);
    labelPBRRoughness = new QLabel(this);
    layoutRow->addWidget(labelPBRRoughness);
    PBRRoughness = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(PBRRoughness);
    PBRRoughness->setValue(Gui::RenderParams::defaultPBRRoughness());
    PBRRoughness->setEntryName("PBRRoughness");
    PBRRoughness->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPhysicallybasedshading->addLayout(layoutRow);
    labelPBREnvIntensity = new QLabel(this);
    layoutRow->addWidget(labelPBREnvIntensity);
    PBREnvIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(PBREnvIntensity);
    PBREnvIntensity->setValue(Gui::RenderParams::defaultPBREnvIntensity());
    PBREnvIntensity->setEntryName("PBREnvIntensity");
    PBREnvIntensity->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupBumpmapping = new QGroupBox(this);
    layout->addWidget(groupBumpmapping);
    auto layoutHorizBumpmapping = new QHBoxLayout(groupBumpmapping);
    auto layoutBumpmapping = new QVBoxLayout();
    layoutHorizBumpmapping->addLayout(layoutBumpmapping);
    layoutHorizBumpmapping->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutBumpmapping->addLayout(layoutRow);
    labelBumpScale = new QLabel(this);
    layoutRow->addWidget(labelBumpScale);
    BumpScale = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(BumpScale);
    BumpScale->setValue(Gui::RenderParams::defaultBumpScale());
    BumpScale->setEntryName("BumpScale");
    BumpScale->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutBumpmapping->addLayout(layoutRow);
    Parallax = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(Parallax);
    Parallax->setChecked(Gui::RenderParams::defaultParallax());
    Parallax->setEntryName("Parallax");
    Parallax->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupVolumetriclighting = new QGroupBox(this);
    layout->addWidget(groupVolumetriclighting);
    auto layoutHorizVolumetriclighting = new QHBoxLayout(groupVolumetriclighting);
    auto layoutVolumetriclighting = new QVBoxLayout();
    layoutHorizVolumetriclighting->addLayout(layoutVolumetriclighting);
    layoutHorizVolumetriclighting->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    Volumetric = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(Volumetric);
    Volumetric->setChecked(Gui::RenderParams::defaultVolumetric());
    Volumetric->setEntryName("Volumetric");
    Volumetric->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    labelVolumetricIntensity = new QLabel(this);
    layoutRow->addWidget(labelVolumetricIntensity);
    VolumetricIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(VolumetricIntensity);
    VolumetricIntensity->setValue(Gui::RenderParams::defaultVolumetricIntensity());
    VolumetricIntensity->setEntryName("VolumetricIntensity");
    VolumetricIntensity->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    labelVolumetricDensity = new QLabel(this);
    layoutRow->addWidget(labelVolumetricDensity);
    VolumetricDensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(VolumetricDensity);
    VolumetricDensity->setValue(Gui::RenderParams::defaultVolumetricDensity());
    VolumetricDensity->setEntryName("VolumetricDensity");
    VolumetricDensity->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    Caustics = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(Caustics);
    Caustics->setChecked(Gui::RenderParams::defaultCaustics());
    Caustics->setEntryName("Caustics");
    Caustics->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    labelCausticsIntensity = new QLabel(this);
    layoutRow->addWidget(labelCausticsIntensity);
    CausticsIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(CausticsIntensity);
    CausticsIntensity->setValue(Gui::RenderParams::defaultCausticsIntensity());
    CausticsIntensity->setEntryName("CausticsIntensity");
    CausticsIntensity->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    labelCausticsScale = new QLabel(this);
    layoutRow->addWidget(labelCausticsScale);
    CausticsScale = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(CausticsScale);
    CausticsScale->setValue(Gui::RenderParams::defaultCausticsScale());
    CausticsScale->setEntryName("CausticsScale");
    CausticsScale->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutVolumetriclighting->addLayout(layoutRow);
    labelCausticsSpeed = new QLabel(this);
    layoutRow->addWidget(labelCausticsSpeed);
    CausticsSpeed = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(CausticsSpeed);
    CausticsSpeed->setValue(Gui::RenderParams::defaultCausticsSpeed());
    CausticsSpeed->setEntryName("CausticsSpeed");
    CausticsSpeed->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupWatersurface = new QGroupBox(this);
    layout->addWidget(groupWatersurface);
    auto layoutHorizWatersurface = new QHBoxLayout(groupWatersurface);
    auto layoutWatersurface = new QVBoxLayout();
    layoutHorizWatersurface->addLayout(layoutWatersurface);
    layoutHorizWatersurface->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutWatersurface->addLayout(layoutRow);
    WaterSurface = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(WaterSurface);
    WaterSurface->setChecked(Gui::RenderParams::defaultWaterSurface());
    WaterSurface->setEntryName("WaterSurface");
    WaterSurface->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutWatersurface->addLayout(layoutRow);
    labelWaterWaveStrength = new QLabel(this);
    layoutRow->addWidget(labelWaterWaveStrength);
    WaterWaveStrength = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(WaterWaveStrength);
    WaterWaveStrength->setValue(Gui::RenderParams::defaultWaterWaveStrength());
    WaterWaveStrength->setEntryName("WaterWaveStrength");
    WaterWaveStrength->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutWatersurface->addLayout(layoutRow);
    labelWaterWaveScale = new QLabel(this);
    layoutRow->addWidget(labelWaterWaveScale);
    WaterWaveScale = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(WaterWaveScale);
    WaterWaveScale->setValue(Gui::RenderParams::defaultWaterWaveScale());
    WaterWaveScale->setEntryName("WaterWaveScale");
    WaterWaveScale->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutWatersurface->addLayout(layoutRow);
    labelWaterWaveSpeed = new QLabel(this);
    layoutRow->addWidget(labelWaterWaveSpeed);
    WaterWaveSpeed = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(WaterWaveSpeed);
    WaterWaveSpeed->setValue(Gui::RenderParams::defaultWaterWaveSpeed());
    WaterWaveSpeed->setEntryName("WaterWaveSpeed");
    WaterWaveSpeed->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupBloom = new QGroupBox(this);
    layout->addWidget(groupBloom);
    auto layoutHorizBloom = new QHBoxLayout(groupBloom);
    auto layoutBloom = new QVBoxLayout();
    layoutHorizBloom->addLayout(layoutBloom);
    layoutHorizBloom->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutBloom->addLayout(layoutRow);
    Bloom = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(Bloom);
    Bloom->setChecked(Gui::RenderParams::defaultBloom());
    Bloom->setEntryName("Bloom");
    Bloom->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutBloom->addLayout(layoutRow);
    labelBloomThreshold = new QLabel(this);
    layoutRow->addWidget(labelBloomThreshold);
    BloomThreshold = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(BloomThreshold);
    BloomThreshold->setValue(Gui::RenderParams::defaultBloomThreshold());
    BloomThreshold->setEntryName("BloomThreshold");
    BloomThreshold->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutBloom->addLayout(layoutRow);
    labelBloomIntensity = new QLabel(this);
    layoutRow->addWidget(labelBloomIntensity);
    BloomIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(BloomIntensity);
    BloomIntensity->setValue(Gui::RenderParams::defaultBloomIntensity());
    BloomIntensity->setEntryName("BloomIntensity");
    BloomIntensity->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutBloom->addLayout(layoutRow);
    labelBloomRadius = new QLabel(this);
    layoutRow->addWidget(labelBloomRadius);
    BloomRadius = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(BloomRadius);
    BloomRadius->setValue(Gui::RenderParams::defaultBloomRadius());
    BloomRadius->setEntryName("BloomRadius");
    BloomRadius->setParamGrpPath("View/Render");


    // Auto generated code (Tools/params_utils.py:448)
    groupGroundreflection = new QGroupBox(this);
    layout->addWidget(groupGroundreflection);
    auto layoutHorizGroundreflection = new QHBoxLayout(groupGroundreflection);
    auto layoutGroundreflection = new QVBoxLayout();
    layoutHorizGroundreflection->addLayout(layoutGroundreflection);
    layoutHorizGroundreflection->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutGroundreflection->addLayout(layoutRow);
    GroundReflection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(GroundReflection);
    GroundReflection->setChecked(Gui::RenderParams::defaultGroundReflection());
    GroundReflection->setEntryName("GroundReflection");
    GroundReflection->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutGroundreflection->addLayout(layoutRow);
    labelGroundReflectionIntensity = new QLabel(this);
    layoutRow->addWidget(labelGroundReflectionIntensity);
    GroundReflectionIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(GroundReflectionIntensity);
    GroundReflectionIntensity->setValue(Gui::RenderParams::defaultGroundReflectionIntensity());
    GroundReflectionIntensity->setEntryName("GroundReflectionIntensity");
    GroundReflectionIntensity->setParamGrpPath("View/Render");
    layout->addItem(new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding));
    retranslateUi();
    // Auto generated code (Tools/params_utils.py:645)
    
}

// Auto generated code (Tools/params_utils.py:652)
DlgSettingsRender::~DlgSettingsRender()
{
    
}

// Auto generated code (Tools/params_utils.py:661)
void DlgSettingsRender::saveSettings()
{
    // Auto generated code (Tools/params_utils.py:497)
    Type->onSave();
    AO->onSave();
    AOMethod->onSave();
    AOSlices->onSave();
    AOSteps->onSave();
    AORadius->onSave();
    AOIntensity->onSave();
    PBR->onSave();
    PBRMetallic->onSave();
    PBRRoughness->onSave();
    PBREnvIntensity->onSave();
    BumpScale->onSave();
    Parallax->onSave();
    Volumetric->onSave();
    VolumetricIntensity->onSave();
    VolumetricDensity->onSave();
    Caustics->onSave();
    CausticsIntensity->onSave();
    CausticsScale->onSave();
    CausticsSpeed->onSave();
    WaterSurface->onSave();
    WaterWaveStrength->onSave();
    WaterWaveScale->onSave();
    WaterWaveSpeed->onSave();
    Bloom->onSave();
    BloomThreshold->onSave();
    BloomIntensity->onSave();
    BloomRadius->onSave();
    GroundReflection->onSave();
    GroundReflectionIntensity->onSave();
}

// Auto generated code (Tools/params_utils.py:670)
void DlgSettingsRender::loadSettings()
{
    // Auto generated code (Tools/params_utils.py:484)
    Type->onRestore();
    AO->onRestore();
    AOMethod->onRestore();
    AOSlices->onRestore();
    AOSteps->onRestore();
    AORadius->onRestore();
    AOIntensity->onRestore();
    PBR->onRestore();
    PBRMetallic->onRestore();
    PBRRoughness->onRestore();
    PBREnvIntensity->onRestore();
    BumpScale->onRestore();
    Parallax->onRestore();
    Volumetric->onRestore();
    VolumetricIntensity->onRestore();
    VolumetricDensity->onRestore();
    Caustics->onRestore();
    CausticsIntensity->onRestore();
    CausticsScale->onRestore();
    CausticsSpeed->onRestore();
    WaterSurface->onRestore();
    WaterWaveStrength->onRestore();
    WaterWaveScale->onRestore();
    WaterWaveSpeed->onRestore();
    Bloom->onRestore();
    BloomThreshold->onRestore();
    BloomIntensity->onRestore();
    BloomRadius->onRestore();
    GroundReflection->onRestore();
    GroundReflectionIntensity->onRestore();
}

// Auto generated code (Tools/params_utils.py:679)
void DlgSettingsRender::retranslateUi()
{
    setWindowTitle(QObject::tr("Render engine"));
    groupGeneral->setTitle(QObject::tr("General"));
    Type->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docType()));
    labelType->setText(QObject::tr("Renderer type"));
    labelType->setToolTip(Type->toolTip());
    groupAmbientocclusion->setTitle(QObject::tr("Ambient occlusion"));
    AO->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docAO()));
    AO->setText(QObject::tr("Ambient occlusion"));
    AOMethod->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docAOMethod()));
    labelAOMethod->setText(QObject::tr("AO method"));
    labelAOMethod->setToolTip(AOMethod->toolTip());
    // Auto generated code (Tools/params_utils.py:1152)
    AOMethod->setItemText(0, QObject::tr("SSAO (hemisphere)"));
    AOMethod->setItemText(1, QObject::tr("GTAO (horizon)"));
    AOSlices->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docAOSlices()));
    labelAOSlices->setText(QObject::tr("GTAO slices"));
    labelAOSlices->setToolTip(AOSlices->toolTip());
    AOSteps->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docAOSteps()));
    labelAOSteps->setText(QObject::tr("GTAO steps"));
    labelAOSteps->setToolTip(AOSteps->toolTip());
    AORadius->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docAORadius()));
    labelAORadius->setText(QObject::tr("Sample radius"));
    labelAORadius->setToolTip(AORadius->toolTip());
    AOIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docAOIntensity()));
    labelAOIntensity->setText(QObject::tr("Intensity"));
    labelAOIntensity->setToolTip(AOIntensity->toolTip());
    groupPhysicallybasedshading->setTitle(QObject::tr("Physically based shading"));
    PBR->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docPBR()));
    PBR->setText(QObject::tr("Physically based shading"));
    PBRMetallic->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docPBRMetallic()));
    labelPBRMetallic->setText(QObject::tr("Metallic"));
    labelPBRMetallic->setToolTip(PBRMetallic->toolTip());
    PBRRoughness->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docPBRRoughness()));
    labelPBRRoughness->setText(QObject::tr("Roughness"));
    labelPBRRoughness->setToolTip(PBRRoughness->toolTip());
    PBREnvIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docPBREnvIntensity()));
    labelPBREnvIntensity->setText(QObject::tr("Environment brightness"));
    labelPBREnvIntensity->setToolTip(PBREnvIntensity->toolTip());
    groupBumpmapping->setTitle(QObject::tr("Bump mapping"));
    BumpScale->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docBumpScale()));
    labelBumpScale->setText(QObject::tr("Bump strength"));
    labelBumpScale->setToolTip(BumpScale->toolTip());
    Parallax->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docParallax()));
    Parallax->setText(QObject::tr("Parallax occlusion mapping"));
    groupVolumetriclighting->setTitle(QObject::tr("Volumetric lighting"));
    Volumetric->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docVolumetric()));
    Volumetric->setText(QObject::tr("Light shafts"));
    VolumetricIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docVolumetricIntensity()));
    labelVolumetricIntensity->setText(QObject::tr("Intensity"));
    labelVolumetricIntensity->setToolTip(VolumetricIntensity->toolTip());
    VolumetricDensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docVolumetricDensity()));
    labelVolumetricDensity->setText(QObject::tr("Medium density"));
    labelVolumetricDensity->setToolTip(VolumetricDensity->toolTip());
    Caustics->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docCaustics()));
    Caustics->setText(QObject::tr("Water caustics"));
    CausticsIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docCausticsIntensity()));
    labelCausticsIntensity->setText(QObject::tr("Caustics intensity"));
    labelCausticsIntensity->setToolTip(CausticsIntensity->toolTip());
    CausticsScale->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docCausticsScale()));
    labelCausticsScale->setText(QObject::tr("Caustics scale"));
    labelCausticsScale->setToolTip(CausticsScale->toolTip());
    CausticsSpeed->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docCausticsSpeed()));
    labelCausticsSpeed->setText(QObject::tr("Caustics speed"));
    labelCausticsSpeed->setToolTip(CausticsSpeed->toolTip());
    groupWatersurface->setTitle(QObject::tr("Water surface"));
    WaterSurface->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docWaterSurface()));
    WaterSurface->setText(QObject::tr("Water surface"));
    WaterWaveStrength->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docWaterWaveStrength()));
    labelWaterWaveStrength->setText(QObject::tr("Wave strength"));
    labelWaterWaveStrength->setToolTip(WaterWaveStrength->toolTip());
    WaterWaveScale->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docWaterWaveScale()));
    labelWaterWaveScale->setText(QObject::tr("Wave scale"));
    labelWaterWaveScale->setToolTip(WaterWaveScale->toolTip());
    WaterWaveSpeed->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docWaterWaveSpeed()));
    labelWaterWaveSpeed->setText(QObject::tr("Wave speed"));
    labelWaterWaveSpeed->setToolTip(WaterWaveSpeed->toolTip());
    groupBloom->setTitle(QObject::tr("Bloom"));
    Bloom->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docBloom()));
    Bloom->setText(QObject::tr("Bloom"));
    BloomThreshold->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docBloomThreshold()));
    labelBloomThreshold->setText(QObject::tr("Bloom threshold"));
    labelBloomThreshold->setToolTip(BloomThreshold->toolTip());
    BloomIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docBloomIntensity()));
    labelBloomIntensity->setText(QObject::tr("Bloom intensity"));
    labelBloomIntensity->setToolTip(BloomIntensity->toolTip());
    BloomRadius->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docBloomRadius()));
    labelBloomRadius->setText(QObject::tr("Bloom radius"));
    labelBloomRadius->setToolTip(BloomRadius->toolTip());
    groupGroundreflection->setTitle(QObject::tr("Ground reflection"));
    GroundReflection->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docGroundReflection()));
    GroundReflection->setText(QObject::tr("Ground reflection"));
    GroundReflectionIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docGroundReflectionIntensity()));
    labelGroundReflectionIntensity->setText(QObject::tr("Reflection intensity"));
    labelGroundReflectionIntensity->setToolTip(GroundReflectionIntensity->toolTip());
}

// Auto generated code (Tools/params_utils.py:697)
void DlgSettingsRender::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(e);
}

// Auto generated code (Tools/params_utils.py:710)
#include "moc_DlgSettingsRender.cpp"
//[[[end]]]
