/****************************************************************************
 *   Copyright (c) 2024 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
import DlgSettingsSelection
DlgSettingsSelection.define()
]]]*/

// Auto generated code (Tools/params_utils.py:601)
#ifndef _PreComp_
#   include <QApplication>
#   include <QLabel>
#   include <QGroupBox>
#   include <QGridLayout>
#   include <QVBoxLayout>
#   include <QHBoxLayout>
#endif
#include <Gui/TreeParams.h>
#include <Gui/ViewParams.h>
// Auto generated code (Tools/params_utils.py:623)
#include "Gui/PreferencePages/DlgSettingsSelection.h"
using namespace Gui::Dialog;
/* TRANSLATOR Gui::Dialog::DlgSettingsSelection */

// Auto generated code (Tools/params_utils.py:632)
DlgSettingsSelection::DlgSettingsSelection(QWidget* parent)
    : PreferencePage( parent )
{

    QHBoxLayout *layoutRow = nullptr;
    auto layout = new QVBoxLayout(this);


    // Auto generated code (Tools/params_utils.py:445)
    groupTreeViewSelection = new QGroupBox(this);
    layout->addWidget(groupTreeViewSelection);
    auto layoutHorizTreeViewSelection = new QHBoxLayout(groupTreeViewSelection);
    auto layoutTreeViewSelection = new QVBoxLayout();
    layoutHorizTreeViewSelection->addLayout(layoutTreeViewSelection);
    layoutHorizTreeViewSelection->addStretch();

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutTreeViewSelection->addLayout(layoutRow);
    SyncView = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(SyncView);
    SyncView->setChecked(Gui::TreeParams::defaultSyncView());
    SyncView->setEntryName("SyncView");
    SyncView->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutTreeViewSelection->addLayout(layoutRow);
    SyncSelection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(SyncSelection);
    SyncSelection->setChecked(Gui::TreeParams::defaultSyncSelection());
    SyncSelection->setEntryName("SyncSelection");
    SyncSelection->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutTreeViewSelection->addLayout(layoutRow);
    CheckBoxesSelection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(CheckBoxesSelection);
    CheckBoxesSelection->setChecked(Gui::TreeParams::defaultCheckBoxesSelection());
    CheckBoxesSelection->setEntryName("CheckBoxesSelection");
    CheckBoxesSelection->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutTreeViewSelection->addLayout(layoutRow);
    RecordSelection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(RecordSelection);
    RecordSelection->setChecked(Gui::TreeParams::defaultRecordSelection());
    RecordSelection->setEntryName("RecordSelection");
    RecordSelection->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutTreeViewSelection->addLayout(layoutRow);
    PreSelection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(PreSelection);
    PreSelection->setChecked(Gui::TreeParams::defaultPreSelection());
    PreSelection->setEntryName("PreSelection");
    PreSelection->setParamGrpPath("TreeView");


    // Auto generated code (Tools/params_utils.py:445)
    groupDViewSelection = new QGroupBox(this);
    layout->addWidget(groupDViewSelection);
    auto layoutHorizDViewSelection = new QHBoxLayout(groupDViewSelection);
    auto layoutDViewSelection = new QVBoxLayout();
    layoutHorizDViewSelection->addLayout(layoutDViewSelection);
    layoutHorizDViewSelection->addStretch();

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    EnablePreselection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(EnablePreselection);
    EnablePreselection->setChecked(Gui::ViewParams::defaultEnablePreselection());
    EnablePreselection->setEntryName("EnablePreselection");
    EnablePreselection->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    HighlightColor = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(HighlightColor);
    HighlightColor->setValue(Gui::ViewParams::defaultHighlightColor());
    HighlightColor->setEntryName("HighlightColor");
    HighlightColor->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    EnableSelection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(EnableSelection);
    EnableSelection->setChecked(Gui::ViewParams::defaultEnableSelection());
    EnableSelection->setEntryName("EnableSelection");
    EnableSelection->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    SelectionColor = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(SelectionColor);
    SelectionColor->setValue(Gui::ViewParams::defaultSelectionColor());
    SelectionColor->setEntryName("SelectionColor");
    SelectionColor->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    labelPickRadius = new QLabel(this);
    layoutRow->addWidget(labelPickRadius);
    PickRadius = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(PickRadius);
    PickRadius->setValue(Gui::ViewParams::defaultPickRadius());
    PickRadius->setEntryName("PickRadius");
    PickRadius->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1192)
    PickRadius->setMinimum(0.5);
    PickRadius->setMaximum(200.0);
    PickRadius->setSingleStep(1.0);
    PickRadius->setDecimals(1);

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    ShowSelectionOnTop = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShowSelectionOnTop);
    ShowSelectionOnTop->setChecked(Gui::ViewParams::defaultShowSelectionOnTop());
    ShowSelectionOnTop->setEntryName("ShowSelectionOnTop");
    ShowSelectionOnTop->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    ShowPreSelectedFaceOnTop = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShowPreSelectedFaceOnTop);
    ShowPreSelectedFaceOnTop->setChecked(Gui::ViewParams::defaultShowPreSelectedFaceOnTop());
    ShowPreSelectedFaceOnTop->setEntryName("ShowPreSelectedFaceOnTop");
    ShowPreSelectedFaceOnTop->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    ShowSelectionBoundingBox = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ShowSelectionBoundingBox);
    ShowSelectionBoundingBox->setChecked(Gui::ViewParams::defaultShowSelectionBoundingBox());
    ShowSelectionBoundingBox->setEntryName("ShowSelectionBoundingBox");
    ShowSelectionBoundingBox->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    labelShowSelectionBoundingBoxThreshold = new QLabel(this);
    layoutRow->addWidget(labelShowSelectionBoundingBoxThreshold);
    ShowSelectionBoundingBoxThreshold = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(ShowSelectionBoundingBoxThreshold);
    ShowSelectionBoundingBoxThreshold->setValue(Gui::ViewParams::defaultShowSelectionBoundingBoxThreshold());
    ShowSelectionBoundingBoxThreshold->setEntryName("ShowSelectionBoundingBoxThreshold");
    ShowSelectionBoundingBoxThreshold->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    HiddenLineSelectionOnTop = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HiddenLineSelectionOnTop);
    HiddenLineSelectionOnTop->setChecked(Gui::ViewParams::defaultHiddenLineSelectionOnTop());
    HiddenLineSelectionOnTop->setEntryName("HiddenLineSelectionOnTop");
    HiddenLineSelectionOnTop->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    SelectElementOnTop = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(SelectElementOnTop);
    SelectElementOnTop->setChecked(Gui::ViewParams::defaultSelectElementOnTop());
    SelectElementOnTop->setEntryName("SelectElementOnTop");
    SelectElementOnTop->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutDViewSelection->addLayout(layoutRow);
    labelSelectionColorDifference = new QLabel(this);
    layoutRow->addWidget(labelSelectionColorDifference);
    SelectionColorDifference = new Gui::PrefDoubleSpinBox(this);
    layoutRow->addWidget(SelectionColorDifference);
    SelectionColorDifference->setValue(Gui::ViewParams::defaultSelectionColorDifference());
    SelectionColorDifference->setEntryName("SelectionColorDifference");
    SelectionColorDifference->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1192)
    SelectionColorDifference->setMinimum(0);
    SelectionColorDifference->setMaximum(100);
    SelectionColorDifference->setSingleStep(1);
    SelectionColorDifference->setDecimals(1);


    // Auto generated code (Tools/params_utils.py:445)
    groupPreselectionToolTip = new QGroupBox(this);
    layout->addWidget(groupPreselectionToolTip);
    auto layoutHorizPreselectionToolTip = new QHBoxLayout(groupPreselectionToolTip);
    auto layoutPreselectionToolTip = new QVBoxLayout();
    layoutHorizPreselectionToolTip->addLayout(layoutPreselectionToolTip);
    layoutHorizPreselectionToolTip->addStretch();

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutPreselectionToolTip->addLayout(layoutRow);
    labelPreselectionToolTipCorner = new QLabel(this);
    layoutRow->addWidget(labelPreselectionToolTipCorner);
    PreselectionToolTipCorner = new Gui::PrefComboBox(this);
    layoutRow->addWidget(PreselectionToolTipCorner);
    PreselectionToolTipCorner->setEntryName("PreselectionToolTipCorner");
    PreselectionToolTipCorner->setParamGrpPath("View");
    for (int i=0; i<4; ++i) // Auto generated code (Tools/params_utils.py:1100)
        PreselectionToolTipCorner->addItem(QString());
    PreselectionToolTipCorner->setCurrentIndex(Gui::ViewParams::defaultPreselectionToolTipCorner());

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutPreselectionToolTip->addLayout(layoutRow);
    labelPreselectionToolTipOffsetX = new QLabel(this);
    layoutRow->addWidget(labelPreselectionToolTipOffsetX);
    PreselectionToolTipOffsetX = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PreselectionToolTipOffsetX);
    PreselectionToolTipOffsetX->setValue(Gui::ViewParams::defaultPreselectionToolTipOffsetX());
    PreselectionToolTipOffsetX->setEntryName("PreselectionToolTipOffsetX");
    PreselectionToolTipOffsetX->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1192)
    PreselectionToolTipOffsetX->setMinimum(0);
    PreselectionToolTipOffsetX->setMaximum(4000);
    PreselectionToolTipOffsetX->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:464)
    layoutPreselectionToolTip->addLayout(layoutRow);
    labelPreselectionToolTipOffsetY = new QLabel(this);
    layoutRow->addWidget(labelPreselectionToolTipOffsetY);
    PreselectionToolTipOffsetY = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PreselectionToolTipOffsetY);
    PreselectionToolTipOffsetY->setValue(Gui::ViewParams::defaultPreselectionToolTipOffsetY());
    PreselectionToolTipOffsetY->setEntryName("PreselectionToolTipOffsetY");
    PreselectionToolTipOffsetY->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1192)
    PreselectionToolTipOffsetY->setMinimum(0);
    PreselectionToolTipOffsetY->setMaximum(4000);
    PreselectionToolTipOffsetY->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:458)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:464)
    layoutPreselectionToolTip->addLayout(layoutRow);
    labelPreselectionToolTipFontSize = new QLabel(this);
    layoutRow->addWidget(labelPreselectionToolTipFontSize);
    PreselectionToolTipFontSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PreselectionToolTipFontSize);
    PreselectionToolTipFontSize->setValue(Gui::ViewParams::defaultPreselectionToolTipFontSize());
    PreselectionToolTipFontSize->setEntryName("PreselectionToolTipFontSize");
    PreselectionToolTipFontSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1192)
    PreselectionToolTipFontSize->setMinimum(0);
    PreselectionToolTipFontSize->setMaximum(100);
    PreselectionToolTipFontSize->setSingleStep(1);
    layout->addItem(new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding));
    retranslateUi();
    // Auto generated code (Tools/params_utils.py:641)
    
}

// Auto generated code (Tools/params_utils.py:648)
DlgSettingsSelection::~DlgSettingsSelection()
{
}

// Auto generated code (Tools/params_utils.py:656)
void DlgSettingsSelection::saveSettings()
{
    // Auto generated code (Tools/params_utils.py:494)
    SyncView->onSave();
    SyncSelection->onSave();
    CheckBoxesSelection->onSave();
    RecordSelection->onSave();
    PreSelection->onSave();
    EnablePreselection->onSave();
    HighlightColor->onSave();
    EnableSelection->onSave();
    SelectionColor->onSave();
    PickRadius->onSave();
    ShowSelectionOnTop->onSave();
    ShowPreSelectedFaceOnTop->onSave();
    ShowSelectionBoundingBox->onSave();
    ShowSelectionBoundingBoxThreshold->onSave();
    HiddenLineSelectionOnTop->onSave();
    SelectElementOnTop->onSave();
    SelectionColorDifference->onSave();
    PreselectionToolTipCorner->onSave();
    PreselectionToolTipOffsetX->onSave();
    PreselectionToolTipOffsetY->onSave();
    PreselectionToolTipFontSize->onSave();
}

// Auto generated code (Tools/params_utils.py:665)
void DlgSettingsSelection::loadSettings()
{
    // Auto generated code (Tools/params_utils.py:481)
    SyncView->onRestore();
    SyncSelection->onRestore();
    CheckBoxesSelection->onRestore();
    RecordSelection->onRestore();
    PreSelection->onRestore();
    EnablePreselection->onRestore();
    HighlightColor->onRestore();
    EnableSelection->onRestore();
    SelectionColor->onRestore();
    PickRadius->onRestore();
    ShowSelectionOnTop->onRestore();
    ShowPreSelectedFaceOnTop->onRestore();
    ShowSelectionBoundingBox->onRestore();
    ShowSelectionBoundingBoxThreshold->onRestore();
    HiddenLineSelectionOnTop->onRestore();
    SelectElementOnTop->onRestore();
    SelectionColorDifference->onRestore();
    PreselectionToolTipCorner->onRestore();
    PreselectionToolTipOffsetX->onRestore();
    PreselectionToolTipOffsetY->onRestore();
    PreselectionToolTipFontSize->onRestore();
}

// Auto generated code (Tools/params_utils.py:674)
void DlgSettingsSelection::retranslateUi()
{
    setWindowTitle(QObject::tr("Selection"));
    groupTreeViewSelection->setTitle(QObject::tr("Tree View Selection"));
    SyncView->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docSyncView()));
    SyncView->setText(QObject::tr("Auto switch to the 3D view containing the selected item"));
    SyncSelection->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docSyncSelection()));
    SyncSelection->setText(QObject::tr("Auto expand tree item when the corresponding object is selected in 3D view"));
    CheckBoxesSelection->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docCheckBoxesSelection()));
    CheckBoxesSelection->setText(QObject::tr("Add checkboxes for selection in document tree"));
    RecordSelection->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docRecordSelection()));
    RecordSelection->setText(QObject::tr("Record selection in tree view in order to go back/forward using navigation button"));
    PreSelection->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docPreSelection()));
    PreSelection->setText(QObject::tr("Preselect the object in 3D view when mouse over the tree item"));
    groupDViewSelection->setTitle(QObject::tr("3D View Selection"));
    EnablePreselection->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docEnablePreselection()));
    EnablePreselection->setText(QObject::tr("Enable preselection"));
    HighlightColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHighlightColor()));
    EnableSelection->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docEnableSelection()));
    EnableSelection->setText(QObject::tr("Enable selection"));
    SelectionColor->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionColor()));
    PickRadius->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPickRadius()));
    labelPickRadius->setText(QObject::tr("Pick radius (px)"));
    labelPickRadius->setToolTip(PickRadius->toolTip());
    ShowSelectionOnTop->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShowSelectionOnTop()));
    ShowSelectionOnTop->setText(QObject::tr("Show selection always on top"));
    ShowPreSelectedFaceOnTop->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShowPreSelectedFaceOnTop()));
    ShowPreSelectedFaceOnTop->setText(QObject::tr("Show pre-selected face always on top"));
    ShowSelectionBoundingBox->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShowSelectionBoundingBox()));
    ShowSelectionBoundingBox->setText(QObject::tr("Show selection bounding box instead of highlight"));
    ShowSelectionBoundingBoxThreshold->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docShowSelectionBoundingBoxThreshold()));
    labelShowSelectionBoundingBoxThreshold->setText(QObject::tr("Threshold for showing bounding box instead of selection highlight"));
    labelShowSelectionBoundingBoxThreshold->setToolTip(ShowSelectionBoundingBoxThreshold->toolTip());
    HiddenLineSelectionOnTop->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docHiddenLineSelectionOnTop()));
    HiddenLineSelectionOnTop->setText(QObject::tr("Enable hidden line/point selection when SelectionOnTop is active."));
    SelectElementOnTop->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectElementOnTop()));
    SelectElementOnTop->setText(QObject::tr("Do box/lasso element selection on already selected objects if SelectionOnTop is enabled."));
    SelectionColorDifference->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docSelectionColorDifference()));
    labelSelectionColorDifference->setText(QObject::tr("Selection Color Difference"));
    labelSelectionColorDifference->setToolTip(SelectionColorDifference->toolTip());
    groupPreselectionToolTip->setTitle(QObject::tr("Pre-selection Tool Tip"));
    PreselectionToolTipCorner->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPreselectionToolTipCorner()));
    labelPreselectionToolTipCorner->setText(QObject::tr("Corner"));
    labelPreselectionToolTipCorner->setToolTip(PreselectionToolTipCorner->toolTip());
    // Auto generated code (Tools/params_utils.py:1125)
    PreselectionToolTipCorner->setItemText(0, QObject::tr("Top Left"));
    PreselectionToolTipCorner->setItemText(1, QObject::tr("Top Right"));
    PreselectionToolTipCorner->setItemText(2, QObject::tr("Bottom Left"));
    PreselectionToolTipCorner->setItemText(3, QObject::tr("Bottom Right"));
    PreselectionToolTipOffsetX->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPreselectionToolTipOffsetX()));
    labelPreselectionToolTipOffsetX->setText(QObject::tr("Offset X"));
    labelPreselectionToolTipOffsetX->setToolTip(PreselectionToolTipOffsetX->toolTip());
    PreselectionToolTipOffsetY->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPreselectionToolTipOffsetY()));
    labelPreselectionToolTipOffsetY->setText(QObject::tr("Offset Y"));
    labelPreselectionToolTipOffsetY->setToolTip(PreselectionToolTipOffsetY->toolTip());
    PreselectionToolTipFontSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPreselectionToolTipFontSize()));
    labelPreselectionToolTipFontSize->setText(QObject::tr("Font size"));
    labelPreselectionToolTipFontSize->setToolTip(PreselectionToolTipFontSize->toolTip());
}

// Auto generated code (Tools/params_utils.py:692)
void DlgSettingsSelection::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(e);
}

// Auto generated code (Tools/params_utils.py:705)
#include "moc_DlgSettingsSelection.cpp"
//[[[end]]]
