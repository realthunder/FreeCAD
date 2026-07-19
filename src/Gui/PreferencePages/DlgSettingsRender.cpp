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
    SSAO = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(SSAO);
    SSAO->setChecked(Gui::RenderParams::defaultSSAO());
    SSAO->setEntryName("SSAO");
    SSAO->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelSSAORadius = new QLabel(this);
    layoutRow->addWidget(labelSSAORadius);
    SSAORadius = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SSAORadius);
    SSAORadius->setValue(Gui::RenderParams::defaultSSAORadius());
    SSAORadius->setEntryName("SSAORadius");
    SSAORadius->setParamGrpPath("View/Render");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutAmbientocclusion->addLayout(layoutRow);
    labelSSAOIntensity = new QLabel(this);
    layoutRow->addWidget(labelSSAOIntensity);
    SSAOIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SSAOIntensity);
    SSAOIntensity->setValue(Gui::RenderParams::defaultSSAOIntensity());
    SSAOIntensity->setEntryName("SSAOIntensity");
    SSAOIntensity->setParamGrpPath("View/Render");


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
    SSAO->onSave();
    SSAORadius->onSave();
    SSAOIntensity->onSave();
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
}

// Auto generated code (Tools/params_utils.py:670)
void DlgSettingsRender::loadSettings()
{
    // Auto generated code (Tools/params_utils.py:484)
    Type->onRestore();
    SSAO->onRestore();
    SSAORadius->onRestore();
    SSAOIntensity->onRestore();
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
    SSAO->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docSSAO()));
    SSAO->setText(QObject::tr("Ambient occlusion"));
    SSAORadius->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docSSAORadius()));
    labelSSAORadius->setText(QObject::tr("Sample radius"));
    labelSSAORadius->setToolTip(SSAORadius->toolTip());
    SSAOIntensity->setToolTip(QApplication::translate("RenderParams", Gui::RenderParams::docSSAOIntensity()));
    labelSSAOIntensity->setText(QObject::tr("Intensity"));
    labelSSAOIntensity->setToolTip(SSAOIntensity->toolTip());
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
