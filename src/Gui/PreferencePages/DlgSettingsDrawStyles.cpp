/****************************************************************************
 *   Copyright (c) 2020 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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

#include <boost/algorithm/string/predicate.hpp>
#include <App/MaterialAppearance.h>
#include <Gui/Document.h>
#include <Gui/View3DInventor.h>
#include <Gui/Application.h>

/*[[[cog
import DlgSettingsDrawStyles
DlgSettingsDrawStyles.define_begin()
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
#include <Gui/ViewParams.h>
// Auto generated code (Tools/params_utils.py:627)
#include "Gui/PreferencePages/DlgSettingsDrawStyles.h"
using namespace Gui::Dialog;
/* TRANSLATOR Gui::Dialog::DlgSettingsDrawStyles */

// Auto generated code (Tools/params_utils.py:636)
DlgSettingsDrawStyles::DlgSettingsDrawStyles(QWidget* parent)
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
    labelDefaultDrawStyle = new QLabel(this);
    layoutRow->addWidget(labelDefaultDrawStyle);
    DefaultDrawStyle = new Gui::PrefComboBox(this);
    layoutRow->addWidget(DefaultDrawStyle);
    DefaultDrawStyle->setEntryName("DefaultDrawStyle");
    DefaultDrawStyle->setParamGrpPath("View");
    for (int i=0; i<8; ++i) // Auto generated code (Tools/params_utils.py:1141)
        DefaultDrawStyle->addItem(QString());
    DefaultDrawStyle->setCurrentIndex(Gui::ViewParams::defaultDefaultDrawStyle());

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutGeneral->addLayout(layoutRow);
    ForceSolidSingleSideLighting = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ForceSolidSingleSideLighting);
    ForceSolidSingleSideLighting->setChecked(Gui::ViewParams::defaultForceSolidSingleSideLighting());
    ForceSolidSingleSideLighting->setEntryName("ForceSolidSingleSideLighting");
    ForceSolidSingleSideLighting->setParamGrpPath("View");


    // Auto generated code (Tools/params_utils.py:448)
    groupSelection = new QGroupBox(this);
    layout->addWidget(groupSelection);
    auto layoutHorizSelection = new QHBoxLayout(groupSelection);
    auto layoutSelection = new QVBoxLayout();
    layoutHorizSelection->addLayout(layoutSelection);
    layoutHorizSelection->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelTransparencyOnTop = new QLabel(this);
    layoutRow->addWidget(labelTransparencyOnTop);
    TransparencyOnTop = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(TransparencyOnTop);
    TransparencyOnTop->setValue(Gui::ViewParams::defaultTransparencyOnTop());
    TransparencyOnTop->setEntryName("TransparencyOnTop");
    TransparencyOnTop->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionLineThicken = new QLabel(this);
    layoutRow->addWidget(labelSelectionLineThicken);
    SelectionLineThicken = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SelectionLineThicken);
    SelectionLineThicken->setValue(Gui::ViewParams::defaultSelectionLineThicken());
    SelectionLineThicken->setEntryName("SelectionLineThicken");
    SelectionLineThicken->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionLineMaxWidth = new QLabel(this);
    layoutRow->addWidget(labelSelectionLineMaxWidth);
    SelectionLineMaxWidth = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SelectionLineMaxWidth);
    SelectionLineMaxWidth->setValue(Gui::ViewParams::defaultSelectionLineMaxWidth());
    SelectionLineMaxWidth->setEntryName("SelectionLineMaxWidth");
    SelectionLineMaxWidth->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionPointScale = new QLabel(this);
    layoutRow->addWidget(labelSelectionPointScale);
    SelectionPointScale = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SelectionPointScale);
    SelectionPointScale->setValue(Gui::ViewParams::defaultSelectionPointScale());
    SelectionPointScale->setEntryName("SelectionPointScale");
    SelectionPointScale->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionPointMaxSize = new QLabel(this);
    layoutRow->addWidget(labelSelectionPointMaxSize);
    SelectionPointMaxSize = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SelectionPointMaxSize);
    SelectionPointMaxSize->setValue(Gui::ViewParams::defaultSelectionPointMaxSize());
    SelectionPointMaxSize->setEntryName("SelectionPointMaxSize");
    SelectionPointMaxSize->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionLinePattern = new QLabel(this);
    layoutRow->addWidget(labelSelectionLinePattern);
    SelectionLinePattern = new Gui::PrefLinePattern(this);
    layoutRow->addWidget(SelectionLinePattern);
    SelectionLinePattern->setEntryName("SelectionLinePattern");
    SelectionLinePattern->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1191)
    for (int i=1; i<SelectionLinePattern->count(); ++i) {
        if (SelectionLinePattern->itemData(i).toInt() == 0)
            SelectionLinePattern->setCurrentIndex(i);
    }

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionLinePatternScale = new QLabel(this);
    layoutRow->addWidget(labelSelectionLinePatternScale);
    SelectionLinePatternScale = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(SelectionLinePatternScale);
    SelectionLinePatternScale->setValue(Gui::ViewParams::defaultSelectionLinePatternScale());
    SelectionLinePatternScale->setEntryName("SelectionLinePatternScale");
    SelectionLinePatternScale->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelSelectionHiddenLineWidth = new QLabel(this);
    layoutRow->addWidget(labelSelectionHiddenLineWidth);
    SelectionHiddenLineWidth = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SelectionHiddenLineWidth);
    SelectionHiddenLineWidth->setValue(Gui::ViewParams::defaultSelectionHiddenLineWidth());
    SelectionHiddenLineWidth->setEntryName("SelectionHiddenLineWidth");
    SelectionHiddenLineWidth->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutSelection->addLayout(layoutRow);
    labelOutlineThicken = new QLabel(this);
    layoutRow->addWidget(labelOutlineThicken);
    OutlineThicken = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(OutlineThicken);
    OutlineThicken->setValue(Gui::ViewParams::defaultOutlineThicken());
    OutlineThicken->setEntryName("OutlineThicken");
    OutlineThicken->setParamGrpPath("View");


    // Auto generated code (Tools/params_utils.py:448)
    groupHiddenLines = new QGroupBox(this);
    layout->addWidget(groupHiddenLines);
    auto layoutHorizHiddenLines = new QHBoxLayout(groupHiddenLines);
    auto layoutHiddenLines = new QVBoxLayout();
    layoutHorizHiddenLines->addLayout(layoutHiddenLines);
    layoutHorizHiddenLines->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    labelHiddenLineSync = new QLabel(this);
    layoutRow->addWidget(labelHiddenLineSync);
    HiddenLineSync = new Gui::PrefComboBox(this);
    layoutRow->addWidget(HiddenLineSync);
    HiddenLineSync->setEntryName("HiddenLineSync");
    HiddenLineSync->setParamGrpPath("View");
    for (int i=0; i<4; ++i) // Auto generated code (Tools/params_utils.py:1141)
        HiddenLineSync->addItem(QString());
    HiddenLineSync->setCurrentIndex(Gui::ViewParams::defaultHiddenLineSync());

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineFaceColor = new Gui::PrefColorButton(this);
    layoutRow->addWidget(HiddenLineFaceColor);
    HiddenLineFaceColor->setPackedColor(Gui::ViewParams::defaultHiddenLineFaceColor());
    HiddenLineFaceColor->setEntryName("HiddenLineFaceColor");
    HiddenLineFaceColor->setParamGrpPath("View");
    HiddenLineOverrideFaceColor = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineOverrideFaceColor);
    HiddenLineOverrideFaceColor->setChecked(Gui::ViewParams::defaultHiddenLineOverrideFaceColor());
    HiddenLineOverrideFaceColor->setEntryName("HiddenLineOverrideFaceColor");
    HiddenLineOverrideFaceColor->setParamGrpPath("View");
    HiddenLineFaceColor->setEnabled(HiddenLineOverrideFaceColor->isChecked());
    connect(HiddenLineOverrideFaceColor, SIGNAL(toggled(bool)), HiddenLineFaceColor, SLOT(setEnabled(bool)));
    HiddenLineFaceColor->setAllowTransparency(true);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineColor = new Gui::PrefColorButton(this);
    layoutRow->addWidget(HiddenLineColor);
    HiddenLineColor->setPackedColor(Gui::ViewParams::defaultHiddenLineColor());
    HiddenLineColor->setEntryName("HiddenLineColor");
    HiddenLineColor->setParamGrpPath("View");
    HiddenLineOverrideColor = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineOverrideColor);
    HiddenLineOverrideColor->setChecked(Gui::ViewParams::defaultHiddenLineOverrideColor());
    HiddenLineOverrideColor->setEntryName("HiddenLineOverrideColor");
    HiddenLineOverrideColor->setParamGrpPath("View");
    HiddenLineColor->setEnabled(HiddenLineOverrideColor->isChecked());
    connect(HiddenLineOverrideColor, SIGNAL(toggled(bool)), HiddenLineColor, SLOT(setEnabled(bool)));
    HiddenLineColor->setAllowTransparency(true);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineBackground = new Gui::PrefColorButton(this);
    layoutRow->addWidget(HiddenLineBackground);
    HiddenLineBackground->setPackedColor(Gui::ViewParams::defaultHiddenLineBackground());
    HiddenLineBackground->setEntryName("HiddenLineBackground");
    HiddenLineBackground->setParamGrpPath("View");
    HiddenLineOverrideBackground = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineOverrideBackground);
    HiddenLineOverrideBackground->setChecked(Gui::ViewParams::defaultHiddenLineOverrideBackground());
    HiddenLineOverrideBackground->setEntryName("HiddenLineOverrideBackground");
    HiddenLineOverrideBackground->setParamGrpPath("View");
    HiddenLineBackground->setEnabled(HiddenLineOverrideBackground->isChecked());
    connect(HiddenLineOverrideBackground, SIGNAL(toggled(bool)), HiddenLineBackground, SLOT(setEnabled(bool)));
    HiddenLineBackground->setAllowTransparency(true);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineShaded = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineShaded);
    HiddenLineShaded->setChecked(Gui::ViewParams::defaultHiddenLineShaded());
    HiddenLineShaded->setEntryName("HiddenLineShaded");
    HiddenLineShaded->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineShowOutline = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineShowOutline);
    HiddenLineShowOutline->setChecked(Gui::ViewParams::defaultHiddenLineShowOutline());
    HiddenLineShowOutline->setEntryName("HiddenLineShowOutline");
    HiddenLineShowOutline->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLinePerFaceOutline = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLinePerFaceOutline);
    HiddenLinePerFaceOutline->setChecked(Gui::ViewParams::defaultHiddenLinePerFaceOutline());
    HiddenLinePerFaceOutline->setEntryName("HiddenLinePerFaceOutline");
    HiddenLinePerFaceOutline->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineSceneOutline = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineSceneOutline);
    HiddenLineSceneOutline->setChecked(Gui::ViewParams::defaultHiddenLineSceneOutline());
    HiddenLineSceneOutline->setEntryName("HiddenLineSceneOutline");
    HiddenLineSceneOutline->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    labelHiddenLineOutlineWidth = new QLabel(this);
    layoutRow->addWidget(labelHiddenLineOutlineWidth);
    HiddenLineOutlineWidth = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(HiddenLineOutlineWidth);
    HiddenLineOutlineWidth->setValue(Gui::ViewParams::defaultHiddenLineOutlineWidth());
    HiddenLineOutlineWidth->setEntryName("HiddenLineOutlineWidth");
    HiddenLineOutlineWidth->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    HiddenLineOutlineWidth->setMinimum(0.0);
    HiddenLineOutlineWidth->setMaximum(100.0);
    HiddenLineOutlineWidth->setSingleStep(0.5);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineHideFace = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineHideFace);
    HiddenLineHideFace->setChecked(Gui::ViewParams::defaultHiddenLineHideFace());
    HiddenLineHideFace->setEntryName("HiddenLineHideFace");
    HiddenLineHideFace->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineHideSeam = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineHideSeam);
    HiddenLineHideSeam->setChecked(Gui::ViewParams::defaultHiddenLineHideSeam());
    HiddenLineHideSeam->setEntryName("HiddenLineHideSeam");
    HiddenLineHideSeam->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineHideVertex = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineHideVertex);
    HiddenLineHideVertex->setChecked(Gui::ViewParams::defaultHiddenLineHideVertex());
    HiddenLineHideVertex->setEntryName("HiddenLineHideVertex");
    HiddenLineHideVertex->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    HiddenLineTransparency = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(HiddenLineTransparency);
    HiddenLineTransparency->setValue(Gui::ViewParams::defaultHiddenLineTransparency());
    HiddenLineTransparency->setEntryName("HiddenLineTransparency");
    HiddenLineTransparency->setParamGrpPath("View");
    HiddenLineOverrideTransparency = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineOverrideTransparency);
    HiddenLineOverrideTransparency->setChecked(Gui::ViewParams::defaultHiddenLineOverrideTransparency());
    HiddenLineOverrideTransparency->setEntryName("HiddenLineOverrideTransparency");
    HiddenLineOverrideTransparency->setParamGrpPath("View");
    HiddenLineTransparency->setEnabled(HiddenLineOverrideTransparency->isChecked());
    connect(HiddenLineOverrideTransparency, SIGNAL(toggled(bool)), HiddenLineTransparency, SLOT(setEnabled(bool)));

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    labelHiddenLineWidth = new QLabel(this);
    layoutRow->addWidget(labelHiddenLineWidth);
    HiddenLineWidth = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(HiddenLineWidth);
    HiddenLineWidth->setValue(Gui::ViewParams::defaultHiddenLineWidth());
    HiddenLineWidth->setEntryName("HiddenLineWidth");
    HiddenLineWidth->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutHiddenLines->addLayout(layoutRow);
    labelHiddenLinePointSize = new QLabel(this);
    layoutRow->addWidget(labelHiddenLinePointSize);
    HiddenLinePointSize = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(HiddenLinePointSize);
    HiddenLinePointSize->setValue(Gui::ViewParams::defaultHiddenLinePointSize());
    HiddenLinePointSize->setEntryName("HiddenLinePointSize");
    HiddenLinePointSize->setParamGrpPath("View");


    // Auto generated code (Tools/params_utils.py:448)
    groupShadow = new QGroupBox(this);
    layout->addWidget(groupShadow);
    auto layoutHorizShadow = new QHBoxLayout(groupShadow);
    auto layoutShadow = new QVBoxLayout();
    layoutHorizShadow->addLayout(layoutShadow);
    layoutHorizShadow->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowSync = new QLabel(this);
    layoutRow->addWidget(labelShadowSync);
    ShadowSync = new Gui::PrefComboBox(this);
    layoutRow->addWidget(ShadowSync);
    ShadowSync->setEntryName("ShadowSync");
    ShadowSync->setParamGrpPath("View");
    for (int i=0; i<4; ++i) // Auto generated code (Tools/params_utils.py:1141)
        ShadowSync->addItem(QString());
    ShadowSync->setCurrentIndex(Gui::ViewParams::defaultShadowSync());

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    ShadowSpotLight = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShadowSpotLight);
    ShadowSpotLight->setChecked(Gui::ViewParams::defaultShadowSpotLight());
    ShadowSpotLight->setEntryName("ShadowSpotLight");
    ShadowSpotLight->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowLightColor = new QLabel(this);
    layoutRow->addWidget(labelShadowLightColor);
    ShadowLightColor = new Gui::PrefColorButton(this);
    layoutRow->addWidget(ShadowLightColor);
    ShadowLightColor->setPackedColor(Gui::ViewParams::defaultShadowLightColor());
    ShadowLightColor->setEntryName("ShadowLightColor");
    ShadowLightColor->setParamGrpPath("View");
    ShadowLightColor->setAllowTransparency(true);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowLightIntensity = new QLabel(this);
    layoutRow->addWidget(labelShadowLightIntensity);
    ShadowLightIntensity = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowLightIntensity);
    ShadowLightIntensity->setValue(Gui::ViewParams::defaultShadowLightIntensity());
    ShadowLightIntensity->setEntryName("ShadowLightIntensity");
    ShadowLightIntensity->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    ShadowShowGround = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShadowShowGround);
    ShadowShowGround->setChecked(Gui::ViewParams::defaultShadowShowGround());
    ShadowShowGround->setEntryName("ShadowShowGround");
    ShadowShowGround->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    ShadowGroundBackFaceCull = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShadowGroundBackFaceCull);
    ShadowGroundBackFaceCull->setChecked(Gui::ViewParams::defaultShadowGroundBackFaceCull());
    ShadowGroundBackFaceCull->setEntryName("ShadowGroundBackFaceCull");
    ShadowGroundBackFaceCull->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowGroundColor = new QLabel(this);
    layoutRow->addWidget(labelShadowGroundColor);
    ShadowGroundColor = new Gui::PrefColorButton(this);
    layoutRow->addWidget(ShadowGroundColor);
    ShadowGroundColor->setPackedColor(Gui::ViewParams::defaultShadowGroundColor());
    ShadowGroundColor->setEntryName("ShadowGroundColor");
    ShadowGroundColor->setParamGrpPath("View");
    ShadowGroundColor->setAllowTransparency(true);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowGroundScale = new QLabel(this);
    layoutRow->addWidget(labelShadowGroundScale);
    ShadowGroundScale = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowGroundScale);
    ShadowGroundScale->setValue(Gui::ViewParams::defaultShadowGroundScale());
    ShadowGroundScale->setEntryName("ShadowGroundScale");
    ShadowGroundScale->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowGroundScale->setMinimum(0.0);
    ShadowGroundScale->setMaximum(10000000.0);
    ShadowGroundScale->setSingleStep(0.5);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowGroundTransparency = new QLabel(this);
    layoutRow->addWidget(labelShadowGroundTransparency);
    ShadowGroundTransparency = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowGroundTransparency);
    ShadowGroundTransparency->setValue(Gui::ViewParams::defaultShadowGroundTransparency());
    ShadowGroundTransparency->setEntryName("ShadowGroundTransparency");
    ShadowGroundTransparency->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowGroundTransparency->setMinimum(0.0);
    ShadowGroundTransparency->setMaximum(1.0);
    ShadowGroundTransparency->setSingleStep(0.1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowGroundTexture = new QLabel(this);
    layoutRow->addWidget(labelShadowGroundTexture);
    ShadowGroundTexture = new Gui::PrefFileChooser(this);
    layoutRow->addWidget(ShadowGroundTexture);
    ShadowGroundTexture->setFileNameStd(Gui::ViewParams::defaultShadowGroundTexture());
    ShadowGroundTexture->setEntryName("ShadowGroundTexture");
    ShadowGroundTexture->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowGroundTextureSize = new QLabel(this);
    layoutRow->addWidget(labelShadowGroundTextureSize);
    ShadowGroundTextureSize = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowGroundTextureSize);
    ShadowGroundTextureSize->setValue(Gui::ViewParams::defaultShadowGroundTextureSize());
    ShadowGroundTextureSize->setEntryName("ShadowGroundTextureSize");
    ShadowGroundTextureSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowGroundTextureSize->setMinimum(0.0);
    ShadowGroundTextureSize->setMaximum(10000000.0);
    ShadowGroundTextureSize->setSingleStep(10.0);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowGroundBumpMap = new QLabel(this);
    layoutRow->addWidget(labelShadowGroundBumpMap);
    ShadowGroundBumpMap = new Gui::PrefFileChooser(this);
    layoutRow->addWidget(ShadowGroundBumpMap);
    ShadowGroundBumpMap->setFileNameStd(Gui::ViewParams::defaultShadowGroundBumpMap());
    ShadowGroundBumpMap->setEntryName("ShadowGroundBumpMap");
    ShadowGroundBumpMap->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    ShadowGroundShading = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShadowGroundShading);
    ShadowGroundShading->setChecked(Gui::ViewParams::defaultShadowGroundShading());
    ShadowGroundShading->setEntryName("ShadowGroundShading");
    ShadowGroundShading->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowPrecision = new QLabel(this);
    layoutRow->addWidget(labelShadowPrecision);
    ShadowPrecision = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowPrecision);
    ShadowPrecision->setValue(Gui::ViewParams::defaultShadowPrecision());
    ShadowPrecision->setEntryName("ShadowPrecision");
    ShadowPrecision->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowPrecision->setMinimum(0.0);
    ShadowPrecision->setMaximum(1.0);
    ShadowPrecision->setSingleStep(0.1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowSmoothBorder = new QLabel(this);
    layoutRow->addWidget(labelShadowSmoothBorder);
    ShadowSmoothBorder = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(ShadowSmoothBorder);
    ShadowSmoothBorder->setValue(Gui::ViewParams::defaultShadowSmoothBorder());
    ShadowSmoothBorder->setEntryName("ShadowSmoothBorder");
    ShadowSmoothBorder->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowSpreadSize = new QLabel(this);
    layoutRow->addWidget(labelShadowSpreadSize);
    ShadowSpreadSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(ShadowSpreadSize);
    ShadowSpreadSize->setValue(Gui::ViewParams::defaultShadowSpreadSize());
    ShadowSpreadSize->setEntryName("ShadowSpreadSize");
    ShadowSpreadSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowSpreadSize->setMinimum(0);
    ShadowSpreadSize->setMaximum(10000000.0);
    ShadowSpreadSize->setSingleStep(500);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowSpreadSampleSize = new QLabel(this);
    layoutRow->addWidget(labelShadowSpreadSampleSize);
    ShadowSpreadSampleSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(ShadowSpreadSampleSize);
    ShadowSpreadSampleSize->setValue(Gui::ViewParams::defaultShadowSpreadSampleSize());
    ShadowSpreadSampleSize->setEntryName("ShadowSpreadSampleSize");
    ShadowSpreadSampleSize->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowEpsilon = new QLabel(this);
    layoutRow->addWidget(labelShadowEpsilon);
    ShadowEpsilon = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowEpsilon);
    ShadowEpsilon->setValue(Gui::ViewParams::defaultShadowEpsilon());
    ShadowEpsilon->setEntryName("ShadowEpsilon");
    ShadowEpsilon->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowEpsilon->setMinimum(0.0);
    ShadowEpsilon->setMaximum(1.0);
    ShadowEpsilon->setSingleStep(1e-05);
    ShadowEpsilon->setDecimals(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutShadow->addLayout(layoutRow);
    labelShadowThreshold = new QLabel(this);
    layoutRow->addWidget(labelShadowThreshold);
    ShadowThreshold = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(ShadowThreshold);
    ShadowThreshold->setValue(Gui::ViewParams::defaultShadowThreshold());
    ShadowThreshold->setEntryName("ShadowThreshold");
    ShadowThreshold->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    ShadowThreshold->setMinimum(0.0);
    ShadowThreshold->setMaximum(1.0);
    ShadowThreshold->setSingleStep(0.1);
    layout->addItem(new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding));
    retranslateUi();
    // Auto generated code (Tools/params_utils.py:645)
    Active = true;
}

// Auto generated code (Tools/params_utils.py:652)
DlgSettingsDrawStyles::~DlgSettingsDrawStyles()
{
    Active = false;
}

// Auto generated code (Tools/params_utils.py:661)
void DlgSettingsDrawStyles::saveSettings()
{
    // Auto generated code (Tools/params_utils.py:497)
    DefaultDrawStyle->onSave();
    ForceSolidSingleSideLighting->onSave();
    TransparencyOnTop->onSave();
    SelectionLineThicken->onSave();
    SelectionLineMaxWidth->onSave();
    SelectionPointScale->onSave();
    SelectionPointMaxSize->onSave();
    SelectionLinePattern->onSave();
    SelectionLinePatternScale->onSave();
    SelectionHiddenLineWidth->onSave();
    OutlineThicken->onSave();
    HiddenLineSync->onSave();
    HiddenLineFaceColor->onSave();
    HiddenLineOverrideFaceColor->onSave();
    HiddenLineColor->onSave();
    HiddenLineOverrideColor->onSave();
    HiddenLineBackground->onSave();
    HiddenLineOverrideBackground->onSave();
    HiddenLineShaded->onSave();
    HiddenLineShowOutline->onSave();
    HiddenLinePerFaceOutline->onSave();
    HiddenLineSceneOutline->onSave();
    HiddenLineOutlineWidth->onSave();
    HiddenLineHideFace->onSave();
    HiddenLineHideSeam->onSave();
    HiddenLineHideVertex->onSave();
    HiddenLineTransparency->onSave();
    HiddenLineOverrideTransparency->onSave();
    HiddenLineWidth->onSave();
    HiddenLinePointSize->onSave();
    ShadowSync->onSave();
    ShadowSpotLight->onSave();
    ShadowLightColor->onSave();
    ShadowLightIntensity->onSave();
    ShadowShowGround->onSave();
    ShadowGroundBackFaceCull->onSave();
    ShadowGroundColor->onSave();
    ShadowGroundScale->onSave();
    ShadowGroundTransparency->onSave();
    ShadowGroundTexture->onSave();
    ShadowGroundTextureSize->onSave();
    ShadowGroundBumpMap->onSave();
    ShadowGroundShading->onSave();
    ShadowPrecision->onSave();
    ShadowSmoothBorder->onSave();
    ShadowSpreadSize->onSave();
    ShadowSpreadSampleSize->onSave();
    ShadowEpsilon->onSave();
    ShadowThreshold->onSave();
}

// Auto generated code (Tools/params_utils.py:670)
void DlgSettingsDrawStyles::loadSettings()
{
    // Auto generated code (Tools/params_utils.py:484)
    DefaultDrawStyle->onRestore();
    ForceSolidSingleSideLighting->onRestore();
    TransparencyOnTop->onRestore();
    SelectionLineThicken->onRestore();
    SelectionLineMaxWidth->onRestore();
    SelectionPointScale->onRestore();
    SelectionPointMaxSize->onRestore();
    SelectionLinePattern->onRestore();
    SelectionLinePatternScale->onRestore();
    SelectionHiddenLineWidth->onRestore();
    OutlineThicken->onRestore();
    HiddenLineSync->onRestore();
    HiddenLineFaceColor->onRestore();
    HiddenLineOverrideFaceColor->onRestore();
    HiddenLineColor->onRestore();
    HiddenLineOverrideColor->onRestore();
    HiddenLineBackground->onRestore();
    HiddenLineOverrideBackground->onRestore();
    HiddenLineShaded->onRestore();
    HiddenLineShowOutline->onRestore();
    HiddenLinePerFaceOutline->onRestore();
    HiddenLineSceneOutline->onRestore();
    HiddenLineOutlineWidth->onRestore();
    HiddenLineHideFace->onRestore();
    HiddenLineHideSeam->onRestore();
    HiddenLineHideVertex->onRestore();
    HiddenLineTransparency->onRestore();
    HiddenLineOverrideTransparency->onRestore();
    HiddenLineWidth->onRestore();
    HiddenLinePointSize->onRestore();
    ShadowSync->onRestore();
    ShadowSpotLight->onRestore();
    ShadowLightColor->onRestore();
    ShadowLightIntensity->onRestore();
    ShadowShowGround->onRestore();
    ShadowGroundBackFaceCull->onRestore();
    ShadowGroundColor->onRestore();
    ShadowGroundScale->onRestore();
    ShadowGroundTransparency->onRestore();
    ShadowGroundTexture->onRestore();
    ShadowGroundTextureSize->onRestore();
    ShadowGroundBumpMap->onRestore();
    ShadowGroundShading->onRestore();
    ShadowPrecision->onRestore();
    ShadowSmoothBorder->onRestore();
    ShadowSpreadSize->onRestore();
    ShadowSpreadSampleSize->onRestore();
    ShadowEpsilon->onRestore();
    ShadowThreshold->onRestore();
}

// Auto generated code (Tools/params_utils.py:679)
void DlgSettingsDrawStyles::retranslateUi()
{
    setWindowTitle(QObject::tr("Display styles"));
    groupGeneral->setTitle(QObject::tr("General"));
    DefaultDrawStyle->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docDefaultDrawStyle()));
    labelDefaultDrawStyle->setText(QObject::tr("Default display style"));
    labelDefaultDrawStyle->setToolTip(DefaultDrawStyle->toolTip());
    // Auto generated code (Tools/params_utils.py:1166)
    DefaultDrawStyle->setItemText(0, QObject::tr("As Is"));
    DefaultDrawStyle->setItemData(0, QObject::tr("Display style, normal display mode"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(1, QObject::tr("Points"));
    DefaultDrawStyle->setItemData(1, QObject::tr("Display style, show points only"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(2, QObject::tr("Wireframe"));
    DefaultDrawStyle->setItemData(2, QObject::tr("Display style, show wire frame only"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(3, QObject::tr("Hidden Line"));
    DefaultDrawStyle->setItemData(3, QObject::tr("Display style, show hidden line by display object as transparent"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(4, QObject::tr("No Shading"));
    DefaultDrawStyle->setItemData(4, QObject::tr("Display style, shading forced off"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(5, QObject::tr("Shaded"));
    DefaultDrawStyle->setItemData(5, QObject::tr("Display style, shading force on"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(6, QObject::tr("Flat Lines"));
    DefaultDrawStyle->setItemData(6, QObject::tr("Display style, show both wire frame and face with shading"), Qt::ToolTipRole);
    DefaultDrawStyle->setItemText(7, QObject::tr("Tessellation"));
    DefaultDrawStyle->setItemData(7, QObject::tr("Display style, show tessellation wire frame"), Qt::ToolTipRole);
    ForceSolidSingleSideLighting->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docForceSolidSingleSideLighting()));
    ForceSolidSingleSideLighting->setText(QObject::tr("Force single side lighting on solid"));
    groupSelection->setTitle(QObject::tr("Selection"));
    TransparencyOnTop->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docTransparencyOnTop()));
    labelTransparencyOnTop->setText(QObject::tr("Transparency"));
    labelTransparencyOnTop->setToolTip(TransparencyOnTop->toolTip());
    SelectionLineThicken->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionLineThicken()));
    labelSelectionLineThicken->setText(QObject::tr("Line width multiplier"));
    labelSelectionLineThicken->setToolTip(SelectionLineThicken->toolTip());
    SelectionLineMaxWidth->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionLineMaxWidth()));
    labelSelectionLineMaxWidth->setText(QObject::tr("Maximum line width"));
    labelSelectionLineMaxWidth->setToolTip(SelectionLineMaxWidth->toolTip());
    SelectionPointScale->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionPointScale()));
    labelSelectionPointScale->setText(QObject::tr("Point size multiplier"));
    labelSelectionPointScale->setToolTip(SelectionPointScale->toolTip());
    SelectionPointMaxSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionPointMaxSize()));
    labelSelectionPointMaxSize->setText(QObject::tr("Maximum point size"));
    labelSelectionPointMaxSize->setToolTip(SelectionPointMaxSize->toolTip());
    SelectionLinePattern->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionLinePattern()));
    labelSelectionLinePattern->setText(QObject::tr("Selected hidden line pattern"));
    labelSelectionLinePattern->setToolTip(SelectionLinePattern->toolTip());
    SelectionLinePatternScale->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionLinePatternScale()));
    labelSelectionLinePatternScale->setText(QObject::tr("Selected line pattern scale"));
    labelSelectionLinePatternScale->setToolTip(SelectionLinePatternScale->toolTip());
    SelectionHiddenLineWidth->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionHiddenLineWidth()));
    labelSelectionHiddenLineWidth->setText(QObject::tr("Selected hidden line width"));
    labelSelectionHiddenLineWidth->setToolTip(SelectionHiddenLineWidth->toolTip());
    OutlineThicken->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docOutlineThicken()));
    labelOutlineThicken->setText(QObject::tr("Outline width multiplier"));
    labelOutlineThicken->setToolTip(OutlineThicken->toolTip());
    groupHiddenLines->setTitle(QObject::tr("Hidden Lines"));
    HiddenLineSync->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineSync()));
    labelHiddenLineSync->setText(QObject::tr("Synchronize"));
    labelHiddenLineSync->setToolTip(HiddenLineSync->toolTip());
    // Auto generated code (Tools/params_utils.py:1166)
    HiddenLineSync->setItemText(0, QObject::tr("None"));
    HiddenLineSync->setItemData(0, QObject::tr("No change to opened document"), Qt::ToolTipRole);
    HiddenLineSync->setItemText(1, QObject::tr("Apply to active view"));
    HiddenLineSync->setItemData(1, QObject::tr("Auto apply changed setting to the current active view"), Qt::ToolTipRole);
    HiddenLineSync->setItemText(2, QObject::tr("Apply to active document"));
    HiddenLineSync->setItemData(2, QObject::tr("Auto apply changed setting to all views of the current active document"), Qt::ToolTipRole);
    HiddenLineSync->setItemText(3, QObject::tr("Apply to all open documents"));
    HiddenLineSync->setItemData(3, QObject::tr("Auto apply changed setting to all opened documents"), Qt::ToolTipRole);
    HiddenLineFaceColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineFaceColor()));
    HiddenLineOverrideFaceColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineOverrideFaceColor()));
    HiddenLineOverrideFaceColor->setText(QObject::tr("Override face color"));
    HiddenLineColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineColor()));
    HiddenLineOverrideColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineOverrideColor()));
    HiddenLineOverrideColor->setText(QObject::tr("Override line color"));
    HiddenLineBackground->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineBackground()));
    HiddenLineOverrideBackground->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineOverrideBackground()));
    HiddenLineOverrideBackground->setText(QObject::tr("Override background color"));
    HiddenLineShaded->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineShaded()));
    HiddenLineShaded->setText(QObject::tr("Shaded"));
    HiddenLineShowOutline->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineShowOutline()));
    HiddenLineShowOutline->setText(QObject::tr("Draw outline"));
    HiddenLinePerFaceOutline->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLinePerFaceOutline()));
    HiddenLinePerFaceOutline->setText(QObject::tr("Draw per face outline"));
    HiddenLineSceneOutline->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineSceneOutline()));
    HiddenLineSceneOutline->setText(QObject::tr("Draw scene outline"));
    HiddenLineOutlineWidth->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineOutlineWidth()));
    labelHiddenLineOutlineWidth->setText(QObject::tr("Outline width"));
    labelHiddenLineOutlineWidth->setToolTip(HiddenLineOutlineWidth->toolTip());
    HiddenLineHideFace->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineHideFace()));
    HiddenLineHideFace->setText(QObject::tr("Hide face"));
    HiddenLineHideSeam->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineHideSeam()));
    HiddenLineHideSeam->setText(QObject::tr("Hide seam edge"));
    HiddenLineHideVertex->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineHideVertex()));
    HiddenLineHideVertex->setText(QObject::tr("Hide vertex"));
    HiddenLineTransparency->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineTransparency()));
    HiddenLineOverrideTransparency->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineOverrideTransparency()));
    HiddenLineOverrideTransparency->setText(QObject::tr("Override transparency"));
    HiddenLineWidth->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineWidth()));
    labelHiddenLineWidth->setText(QObject::tr("Line width"));
    labelHiddenLineWidth->setToolTip(HiddenLineWidth->toolTip());
    HiddenLinePointSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLinePointSize()));
    labelHiddenLinePointSize->setText(QObject::tr("Point size"));
    labelHiddenLinePointSize->setToolTip(HiddenLinePointSize->toolTip());
    groupShadow->setTitle(QObject::tr("Shadow"));
    ShadowSync->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowSync()));
    labelShadowSync->setText(QObject::tr("Synchronize"));
    labelShadowSync->setToolTip(ShadowSync->toolTip());
    // Auto generated code (Tools/params_utils.py:1166)
    ShadowSync->setItemText(0, QObject::tr("None"));
    ShadowSync->setItemData(0, QObject::tr("No change to opened document"), Qt::ToolTipRole);
    ShadowSync->setItemText(1, QObject::tr("Apply to active view"));
    ShadowSync->setItemData(1, QObject::tr("Auto apply changed setting to the current active view"), Qt::ToolTipRole);
    ShadowSync->setItemText(2, QObject::tr("Apply to active document"));
    ShadowSync->setItemData(2, QObject::tr("Auto apply changed setting to all views of the current active document"), Qt::ToolTipRole);
    ShadowSync->setItemText(3, QObject::tr("Apply to all open documents"));
    ShadowSync->setItemData(3, QObject::tr("Auto apply changed setting to all opened documents"), Qt::ToolTipRole);
    ShadowSpotLight->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowSpotLight()));
    ShadowSpotLight->setText(QObject::tr("Use spot light"));
    ShadowLightColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowLightColor()));
    labelShadowLightColor->setText(QObject::tr("Light color"));
    labelShadowLightColor->setToolTip(ShadowLightColor->toolTip());
    ShadowLightIntensity->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowLightIntensity()));
    labelShadowLightIntensity->setText(QObject::tr("Light intensity"));
    labelShadowLightIntensity->setToolTip(ShadowLightIntensity->toolTip());
    ShadowShowGround->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowShowGround()));
    ShadowShowGround->setText(QObject::tr("Show ground"));
    ShadowGroundBackFaceCull->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundBackFaceCull()));
    ShadowGroundBackFaceCull->setText(QObject::tr("Ground back face culling"));
    ShadowGroundColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundColor()));
    labelShadowGroundColor->setText(QObject::tr("Ground color"));
    labelShadowGroundColor->setToolTip(ShadowGroundColor->toolTip());
    ShadowGroundScale->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundScale()));
    labelShadowGroundScale->setText(QObject::tr("Ground scale"));
    labelShadowGroundScale->setToolTip(ShadowGroundScale->toolTip());
    ShadowGroundTransparency->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundTransparency()));
    labelShadowGroundTransparency->setText(QObject::tr("Ground transparency"));
    labelShadowGroundTransparency->setToolTip(ShadowGroundTransparency->toolTip());
    ShadowGroundTexture->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundTexture()));
    labelShadowGroundTexture->setText(QObject::tr("Ground texture"));
    labelShadowGroundTexture->setToolTip(ShadowGroundTexture->toolTip());
    ShadowGroundTextureSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundTextureSize()));
    labelShadowGroundTextureSize->setText(QObject::tr("Ground texture size"));
    labelShadowGroundTextureSize->setToolTip(ShadowGroundTextureSize->toolTip());
    ShadowGroundBumpMap->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundBumpMap()));
    labelShadowGroundBumpMap->setText(QObject::tr("Ground bump map"));
    labelShadowGroundBumpMap->setToolTip(ShadowGroundBumpMap->toolTip());
    ShadowGroundShading->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowGroundShading()));
    ShadowGroundShading->setText(QObject::tr("Ground shading"));
    ShadowPrecision->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowPrecision()));
    labelShadowPrecision->setText(QObject::tr("Precision"));
    labelShadowPrecision->setToolTip(ShadowPrecision->toolTip());
    ShadowSmoothBorder->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowSmoothBorder()));
    labelShadowSmoothBorder->setText(QObject::tr("Smooth border"));
    labelShadowSmoothBorder->setToolTip(ShadowSmoothBorder->toolTip());
    ShadowSpreadSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowSpreadSize()));
    labelShadowSpreadSize->setText(QObject::tr("Spread size"));
    labelShadowSpreadSize->setToolTip(ShadowSpreadSize->toolTip());
    ShadowSpreadSampleSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowSpreadSampleSize()));
    labelShadowSpreadSampleSize->setText(QObject::tr("Spread sample size"));
    labelShadowSpreadSampleSize->setToolTip(ShadowSpreadSampleSize->toolTip());
    ShadowEpsilon->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowEpsilon()));
    labelShadowEpsilon->setText(QObject::tr("Epsilon"));
    labelShadowEpsilon->setToolTip(ShadowEpsilon->toolTip());
    ShadowThreshold->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShadowThreshold()));
    labelShadowThreshold->setText(QObject::tr("Threshold"));
    labelShadowThreshold->setToolTip(ShadowThreshold->toolTip());
}

// Auto generated code (Tools/params_utils.py:697)
void DlgSettingsDrawStyles::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(e);
}

// Auto generated code (Tools/params_utils.py:710)
#include "moc_DlgSettingsDrawStyles.cpp"

// Auto generated code (Gui/PreferencePages/DlgSettingsDrawStyles.py:133)
bool DlgSettingsDrawStyles::Active;
//[[[end]]]

// -----------------------------------------------------------------------------------
// user code start

template<class PropertyT, class ValueT>
static void setViewProperty(int mode, const char *propertyName, const ValueT value)
{
    static std::vector<Gui::View3DInventor*> views;
    views.clear();
    if (mode == 1) {
        if (auto view = Base::freecad_dynamic_cast<Gui::View3DInventor>(
            Gui::Application::Instance->activeView())) {
            views.push_back(view);
        }
    }
    else if (mode == 2) {
        if (auto gdoc = Gui::Application::Instance->activeDocument()) {
            gdoc->foreachView<Gui::View3DInventor>(
                [&](Gui::View3DInventor *view) {
                    views.push_back(view);
                }
            );
        }
    }
    else if (mode == 3) {
        for (const auto &it : App::GetApplication().getDocumentMap()) {
            if (auto gdoc = Gui::Application::Instance->getDocument(it.second)) {
                gdoc->foreachView<Gui::View3DInventor>(
                    [&](Gui::View3DInventor *view) {
                        views.push_back(view);
                    }
                );
            }
        }
    }
    for (auto view : views) {
        if (auto prop = Base::freecad_dynamic_cast<PropertyT>(view->getPropertyByName(propertyName))) {
            try {
                prop->setValue(value);
            } catch (Base::Exception &e) {
                e.ReportException();
            }
        }
    }
}


// user code end
// -----------------------------------------------------------------------------------

/*[[[cog
import DlgSettingsDrawStyles
DlgSettingsDrawStyles.define_end()
]]]*/

// Auto generated code (Gui/PreferencePages/DlgSettingsDrawStyles.py:139)
void DlgSettingsDrawStyles::onParamChanged(const char *sReason)
{
    if (!Active)
        return;

    if (ViewParams::getHiddenLineSync() != 0 && boost::starts_with(sReason, "HiddenLine")) {
        bool passThrough = boost::equals(sReason+10, "HiddenLineSync");

        if (passThrough || boost::equals(sReason+10, "FaceColor")) {
            setViewProperty<App::PropertyColor>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_FaceColor",
                                                  ViewParams::getHiddenLineFaceColor());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "Color")) {
            setViewProperty<App::PropertyColor>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_Color",
                                                  ViewParams::getHiddenLineColor());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "Background")) {
            setViewProperty<App::PropertyColor>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_Background",
                                                  ViewParams::getHiddenLineBackground());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "Shaded")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_Shaded",
                                                  ViewParams::getHiddenLineShaded());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "ShowOutline")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_ShowOutline",
                                                  ViewParams::getHiddenLineShowOutline());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "PerFaceOutline")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_PerFaceOutline",
                                                  ViewParams::getHiddenLinePerFaceOutline());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "SceneOutline")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_SceneOutline",
                                                  ViewParams::getHiddenLineSceneOutline());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "OutlineWidth")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_OutlineWidth",
                                                  ViewParams::getHiddenLineOutlineWidth());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "HideFace")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_HideFace",
                                                  ViewParams::getHiddenLineHideFace());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "HideSeam")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_HideSeam",
                                                  ViewParams::getHiddenLineHideSeam());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "HideVertex")) {
            setViewProperty<App::PropertyBool>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_HideVertex",
                                                  ViewParams::getHiddenLineHideVertex());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "Transparency")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_Transparency",
                                                  ViewParams::getHiddenLineTransparency());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "Width")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_Width",
                                                  ViewParams::getHiddenLineWidth());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+10, "PointSize")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getHiddenLineSync(),
                                                  "HiddenLine_PointSize",
                                                  ViewParams::getHiddenLinePointSize());
            if (!passThrough)
                return;
        }
    }
    else if (ViewParams::getShadowSync() != 0 && boost::starts_with(sReason, "Shadow")) {
        bool passThrough = boost::equals(sReason+6, "ShadowSync");

        if (passThrough || boost::equals(sReason+6, "SpotLight")) {
            setViewProperty<App::PropertyBool>(ViewParams::getShadowSync(),
                                                  "Shadow_SpotLight",
                                                  ViewParams::getShadowSpotLight());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "LightColor")) {
            setViewProperty<App::PropertyColor>(ViewParams::getShadowSync(),
                                                  "Shadow_LightColor",
                                                  ViewParams::getShadowLightColor());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "LightIntensity")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_LightIntensity",
                                                  ViewParams::getShadowLightIntensity());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "ShowGround")) {
            setViewProperty<App::PropertyBool>(ViewParams::getShadowSync(),
                                                  "Shadow_ShowGround",
                                                  ViewParams::getShadowShowGround());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundBackFaceCull")) {
            setViewProperty<App::PropertyBool>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundBackFaceCull",
                                                  ViewParams::getShadowGroundBackFaceCull());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundColor")) {
            setViewProperty<App::PropertyColor>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundColor",
                                                  ViewParams::getShadowGroundColor());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundScale")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundScale",
                                                  ViewParams::getShadowGroundScale());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundTransparency")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundTransparency",
                                                  ViewParams::getShadowGroundTransparency());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundTexture")) {
            setViewProperty<App::PropertyFileIncluded>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundTexture",
                                                  ViewParams::getShadowGroundTexture());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundTextureSize")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundTextureSize",
                                                  ViewParams::getShadowGroundTextureSize());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundBumpMap")) {
            setViewProperty<App::PropertyFileIncluded>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundBumpMap",
                                                  ViewParams::getShadowGroundBumpMap());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "GroundShading")) {
            setViewProperty<App::PropertyBool>(ViewParams::getShadowSync(),
                                                  "Shadow_GroundShading",
                                                  ViewParams::getShadowGroundShading());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "Precision")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_Precision",
                                                  ViewParams::getShadowPrecision());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "SmoothBorder")) {
            setViewProperty<App::PropertyInteger>(ViewParams::getShadowSync(),
                                                  "Shadow_SmoothBorder",
                                                  ViewParams::getShadowSmoothBorder());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "SpreadSize")) {
            setViewProperty<App::PropertyInteger>(ViewParams::getShadowSync(),
                                                  "Shadow_SpreadSize",
                                                  ViewParams::getShadowSpreadSize());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "SpreadSampleSize")) {
            setViewProperty<App::PropertyInteger>(ViewParams::getShadowSync(),
                                                  "Shadow_SpreadSampleSize",
                                                  ViewParams::getShadowSpreadSampleSize());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "Epsilon")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_Epsilon",
                                                  ViewParams::getShadowEpsilon());
            if (!passThrough)
                return;
        }
        if (passThrough || boost::equals(sReason+6, "Threshold")) {
            setViewProperty<App::PropertyFloat>(ViewParams::getShadowSync(),
                                                  "Shadow_Threshold",
                                                  ViewParams::getShadowThreshold());
            if (!passThrough)
                return;
        }
    }
}
//[[[end]]]
