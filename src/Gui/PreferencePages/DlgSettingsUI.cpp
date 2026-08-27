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

#include <QPropertyAnimation>
#include <QStandardItemModel>
#include <QTimer>

/*[[[cog
import DlgSettingsUI
DlgSettingsUI.define()
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
#include <Gui/OpenViewParams.h>
#include <Gui/TreeParams.h>
#include <Gui/ExprParams.h>
#include <Gui/OverlayParams.h>
// Auto generated code (Tools/params_utils.py:627)
#include "Gui/PreferencePages/DlgSettingsUI.h"
using namespace Gui::Dialog;
/* TRANSLATOR Gui::Dialog::DlgSettingsUI */

// Auto generated code (Tools/params_utils.py:636)
DlgSettingsUI::DlgSettingsUI(QWidget* parent)
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
    labelTextCursorWidth = new QLabel(this);
    layoutRow->addWidget(labelTextCursorWidth);
    TextCursorWidth = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(TextCursorWidth);
    TextCursorWidth->setValue(Gui::ViewParams::defaultTextCursorWidth());
    TextCursorWidth->setEntryName("TextCursorWidth");
    TextCursorWidth->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    TextCursorWidth->setMinimum(1);
    TextCursorWidth->setMaximum(100);
    TextCursorWidth->setSingleStep(1);


    // Auto generated code (Tools/params_utils.py:448)
    groupViews = new QGroupBox(this);
    layout->addWidget(groupViews);
    auto layoutHorizViews = new QHBoxLayout(groupViews);
    auto layoutViews = new QVBoxLayout();
    layoutHorizViews->addLayout(layoutViews);
    layoutHorizViews->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutViews->addLayout(layoutRow);
    UseViewArea = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(UseViewArea);
    UseViewArea->setChecked(Gui::ViewParams::defaultUseViewArea());
    UseViewArea->setEntryName("UseViewArea");
    UseViewArea->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutViews->addLayout(layoutRow);
    labelDocumentTarget = new QLabel(this);
    layoutRow->addWidget(labelDocumentTarget);
    DocumentTarget = new Gui::PrefComboBox(this);
    layoutRow->addWidget(DocumentTarget);
    DocumentTarget->setEntryName("DocumentTarget");
    DocumentTarget->setParamGrpPath("View/OpenView");

    // Auto generated code (Gui/OpenViewParams.py:78)
    DocumentTarget->setProperty("prefType", QByteArray());
    DocumentTarget->addItem(QString(), QByteArray("Tab"));
    DocumentTarget->addItem(QString(), QByteArray("Split"));
    DocumentTarget->addItem(QString(), QByteArray("Floating"));
    DocumentTarget->setCurrentIndex(DocumentTarget->findData(QByteArray(
                Gui::OpenViewParams::defaultDocumentTarget().c_str())));

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutViews->addLayout(layoutRow);
    labelDocViewTarget = new QLabel(this);
    layoutRow->addWidget(labelDocViewTarget);
    DocViewTarget = new Gui::PrefComboBox(this);
    layoutRow->addWidget(DocViewTarget);
    DocViewTarget->setEntryName("DocViewTarget");
    DocViewTarget->setParamGrpPath("View/OpenView");

    // Auto generated code (Gui/OpenViewParams.py:78)
    DocViewTarget->setProperty("prefType", QByteArray());
    DocViewTarget->addItem(QString(), QByteArray("Tab"));
    DocViewTarget->addItem(QString(), QByteArray("Split"));
    DocViewTarget->addItem(QString(), QByteArray("NewSplit"));
    DocViewTarget->addItem(QString(), QByteArray("Floating"));
    DocViewTarget->setCurrentIndex(DocViewTarget->findData(QByteArray(
                Gui::OpenViewParams::defaultDocViewTarget().c_str())));

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutViews->addLayout(layoutRow);
    labelUtilityTarget = new QLabel(this);
    layoutRow->addWidget(labelUtilityTarget);
    UtilityTarget = new Gui::PrefComboBox(this);
    layoutRow->addWidget(UtilityTarget);
    UtilityTarget->setEntryName("UtilityTarget");
    UtilityTarget->setParamGrpPath("View/OpenView");

    // Auto generated code (Gui/OpenViewParams.py:78)
    UtilityTarget->setProperty("prefType", QByteArray());
    UtilityTarget->addItem(QString(), QByteArray("Tab"));
    UtilityTarget->addItem(QString(), QByteArray("Split"));
    UtilityTarget->addItem(QString(), QByteArray("Floating"));
    UtilityTarget->setCurrentIndex(UtilityTarget->findData(QByteArray(
                Gui::OpenViewParams::defaultUtilityTarget().c_str())));

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutViews->addLayout(layoutRow);
    labelSplitDirection = new QLabel(this);
    layoutRow->addWidget(labelSplitDirection);
    SplitDirection = new Gui::PrefComboBox(this);
    layoutRow->addWidget(SplitDirection);
    SplitDirection->setEntryName("SplitDirection");
    SplitDirection->setParamGrpPath("View/OpenView");

    // Auto generated code (Gui/OpenViewParams.py:78)
    SplitDirection->setProperty("prefType", QByteArray());
    SplitDirection->addItem(QString(), QByteArray("Auto"));
    SplitDirection->addItem(QString(), QByteArray("Right"));
    SplitDirection->addItem(QString(), QByteArray("Down"));
    SplitDirection->setCurrentIndex(SplitDirection->findData(QByteArray(
                Gui::OpenViewParams::defaultSplitDirection().c_str())));
    hintSplitDirection = new QLabel(this);
    hintSplitDirection->setWordWrap(true);
    layoutViews->addWidget(hintSplitDirection);


    // Auto generated code (Tools/params_utils.py:448)
    groupTreeview = new QGroupBox(this);
    layout->addWidget(groupTreeview);
    auto layoutHorizTreeview = new QHBoxLayout(groupTreeview);
    auto layoutTreeview = new QVBoxLayout();
    layoutHorizTreeview->addLayout(layoutTreeview);
    layoutHorizTreeview->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    labelItemBackground = new QLabel(this);
    layoutRow->addWidget(labelItemBackground);
    ItemBackground = new Gui::PrefColorButton(this);
    layoutRow->addWidget(ItemBackground);
    ItemBackground->setPackedColor(Gui::TreeParams::defaultItemBackground());
    ItemBackground->setEntryName("ItemBackground");
    ItemBackground->setParamGrpPath("TreeView");
    ItemBackground->setAllowTransparency(true);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    labelItemBackgroundPadding = new QLabel(this);
    layoutRow->addWidget(labelItemBackgroundPadding);
    ItemBackgroundPadding = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(ItemBackgroundPadding);
    ItemBackgroundPadding->setValue(Gui::TreeParams::defaultItemBackgroundPadding());
    ItemBackgroundPadding->setEntryName("ItemBackgroundPadding");
    ItemBackgroundPadding->setParamGrpPath("TreeView");
    // Auto generated code (Tools/params_utils.py:1240)
    ItemBackgroundPadding->setMinimum(0);
    ItemBackgroundPadding->setMaximum(100);
    ItemBackgroundPadding->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    ResizableColumn = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(ResizableColumn);
    ResizableColumn->setChecked(Gui::TreeParams::defaultResizableColumn());
    ResizableColumn->setEntryName("ResizableColumn");
    ResizableColumn->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    CheckBoxesSelection = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(CheckBoxesSelection);
    CheckBoxesSelection->setChecked(Gui::TreeParams::defaultCheckBoxesSelection());
    CheckBoxesSelection->setEntryName("CheckBoxesSelection");
    CheckBoxesSelection->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    HideColumn = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HideColumn);
    HideColumn->setChecked(Gui::TreeParams::defaultHideColumn());
    HideColumn->setEntryName("HideColumn");
    HideColumn->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    HideScrollBar = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HideScrollBar);
    HideScrollBar->setChecked(Gui::TreeParams::defaultHideScrollBar());
    HideScrollBar->setEntryName("HideScrollBar");
    HideScrollBar->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    HideHeaderView = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(HideHeaderView);
    HideHeaderView->setChecked(Gui::TreeParams::defaultHideHeaderView());
    HideHeaderView->setEntryName("HideHeaderView");
    HideHeaderView->setParamGrpPath("TreeView");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutTreeview->addLayout(layoutRow);
    TreeToolTipIcon = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(TreeToolTipIcon);
    TreeToolTipIcon->setChecked(Gui::TreeParams::defaultTreeToolTipIcon());
    TreeToolTipIcon->setEntryName("TreeToolTipIcon");
    TreeToolTipIcon->setParamGrpPath("TreeView");


    // Auto generated code (Tools/params_utils.py:448)
    groupExpression = new QGroupBox(this);
    layout->addWidget(groupExpression);
    auto layoutHorizExpression = new QHBoxLayout(groupExpression);
    auto layoutExpression = new QVBoxLayout();
    layoutHorizExpression->addLayout(layoutExpression);
    layoutHorizExpression->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutExpression->addLayout(layoutRow);
    AutoHideEditorIcon = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(AutoHideEditorIcon);
    AutoHideEditorIcon->setChecked(Gui::ExprParams::defaultAutoHideEditorIcon());
    AutoHideEditorIcon->setEntryName("AutoHideEditorIcon");
    AutoHideEditorIcon->setParamGrpPath("Expression");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutExpression->addLayout(layoutRow);
    labelEditorTrigger = new QLabel(this);
    layoutRow->addWidget(labelEditorTrigger);
    EditorTrigger = new Gui::PrefAccelLineEdit(this);
    layoutRow->addWidget(EditorTrigger);
    EditorTrigger->setDisplayText(Gui::ExprParams::defaultEditorTrigger());
    EditorTrigger->setEntryName("EditorTrigger");
    EditorTrigger->setParamGrpPath("Expression");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutExpression->addLayout(layoutRow);
    NoSystemBackground = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(NoSystemBackground);
    NoSystemBackground->setChecked(Gui::ExprParams::defaultNoSystemBackground());
    NoSystemBackground->setEntryName("NoSystemBackground");
    NoSystemBackground->setParamGrpPath("Expression");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutExpression->addLayout(layoutRow);
    labelEditDialogBGAlpha = new QLabel(this);
    layoutRow->addWidget(labelEditDialogBGAlpha);
    EditDialogBGAlpha = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(EditDialogBGAlpha);
    EditDialogBGAlpha->setValue(Gui::ExprParams::defaultEditDialogBGAlpha());
    EditDialogBGAlpha->setEntryName("EditDialogBGAlpha");
    EditDialogBGAlpha->setParamGrpPath("Expression");
    // Auto generated code (Tools/params_utils.py:1240)
    EditDialogBGAlpha->setMinimum(0);
    EditDialogBGAlpha->setMaximum(255);
    EditDialogBGAlpha->setSingleStep(1);


    // Auto generated code (Tools/params_utils.py:448)
    groupPiemenu = new QGroupBox(this);
    layout->addWidget(groupPiemenu);
    auto layoutHorizPiemenu = new QHBoxLayout(groupPiemenu);
    auto layoutPiemenu = new QVBoxLayout();
    layoutHorizPiemenu->addLayout(layoutPiemenu);
    layoutHorizPiemenu->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuIconSize = new QLabel(this);
    layoutRow->addWidget(labelPieMenuIconSize);
    PieMenuIconSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuIconSize);
    PieMenuIconSize->setValue(Gui::ViewParams::defaultPieMenuIconSize());
    PieMenuIconSize->setEntryName("PieMenuIconSize");
    PieMenuIconSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuIconSize->setMinimum(0);
    PieMenuIconSize->setMaximum(64);
    PieMenuIconSize->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuRadius = new QLabel(this);
    layoutRow->addWidget(labelPieMenuRadius);
    PieMenuRadius = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuRadius);
    PieMenuRadius->setValue(Gui::ViewParams::defaultPieMenuRadius());
    PieMenuRadius->setEntryName("PieMenuRadius");
    PieMenuRadius->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuRadius->setMinimum(10);
    PieMenuRadius->setMaximum(500);
    PieMenuRadius->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuTriggerRadius = new QLabel(this);
    layoutRow->addWidget(labelPieMenuTriggerRadius);
    PieMenuTriggerRadius = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuTriggerRadius);
    PieMenuTriggerRadius->setValue(Gui::ViewParams::defaultPieMenuTriggerRadius());
    PieMenuTriggerRadius->setEntryName("PieMenuTriggerRadius");
    PieMenuTriggerRadius->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuTriggerRadius->setMinimum(10);
    PieMenuTriggerRadius->setMaximum(500);
    PieMenuTriggerRadius->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuCenterRadius = new QLabel(this);
    layoutRow->addWidget(labelPieMenuCenterRadius);
    PieMenuCenterRadius = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuCenterRadius);
    PieMenuCenterRadius->setValue(Gui::ViewParams::defaultPieMenuCenterRadius());
    PieMenuCenterRadius->setEntryName("PieMenuCenterRadius");
    PieMenuCenterRadius->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuCenterRadius->setMinimum(0);
    PieMenuCenterRadius->setMaximum(250);
    PieMenuCenterRadius->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuFontSize = new QLabel(this);
    layoutRow->addWidget(labelPieMenuFontSize);
    PieMenuFontSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuFontSize);
    PieMenuFontSize->setValue(Gui::ViewParams::defaultPieMenuFontSize());
    PieMenuFontSize->setEntryName("PieMenuFontSize");
    PieMenuFontSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuFontSize->setMinimum(0);
    PieMenuFontSize->setMaximum(32);
    PieMenuFontSize->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuTriggerDelay = new QLabel(this);
    layoutRow->addWidget(labelPieMenuTriggerDelay);
    PieMenuTriggerDelay = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuTriggerDelay);
    PieMenuTriggerDelay->setValue(Gui::ViewParams::defaultPieMenuTriggerDelay());
    PieMenuTriggerDelay->setEntryName("PieMenuTriggerDelay");
    PieMenuTriggerDelay->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuTriggerDelay->setMinimum(0);
    PieMenuTriggerDelay->setMaximum(10000);
    PieMenuTriggerDelay->setSingleStep(100);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    PieMenuTriggerAction = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(PieMenuTriggerAction);
    PieMenuTriggerAction->setChecked(Gui::ViewParams::defaultPieMenuTriggerAction());
    PieMenuTriggerAction->setEntryName("PieMenuTriggerAction");
    PieMenuTriggerAction->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuAnimationDuration = new QLabel(this);
    layoutRow->addWidget(labelPieMenuAnimationDuration);
    PieMenuAnimationDuration = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(PieMenuAnimationDuration);
    PieMenuAnimationDuration->setValue(Gui::ViewParams::defaultPieMenuAnimationDuration());
    PieMenuAnimationDuration->setEntryName("PieMenuAnimationDuration");
    PieMenuAnimationDuration->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    PieMenuAnimationDuration->setMinimum(0);
    PieMenuAnimationDuration->setMaximum(5000);
    PieMenuAnimationDuration->setSingleStep(100);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    labelPieMenuAnimationCurve = new QLabel(this);
    layoutRow->addWidget(labelPieMenuAnimationCurve);
    PieMenuAnimationCurve = new Gui::PrefComboBox(this);
    layoutRow->addWidget(PieMenuAnimationCurve);
    PieMenuAnimationCurve->setEntryName("PieMenuAnimationCurve");
    PieMenuAnimationCurve->setParamGrpPath("View");
    // Auto generated code (Gui/ViewParams.py:150)
    for (const auto &item : ViewParams::AnimationCurveTypes)
        PieMenuAnimationCurve->addItem(item);
    PieMenuAnimationCurve->setCurrentIndex(Gui::ViewParams::defaultPieMenuAnimationCurve());

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutPiemenu->addLayout(layoutRow);
    PieMenuPopup = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(PieMenuPopup);
    PieMenuPopup->setChecked(Gui::ViewParams::defaultPieMenuPopup());
    PieMenuPopup->setEntryName("PieMenuPopup");
    PieMenuPopup->setParamGrpPath("View");


    // Auto generated code (Tools/params_utils.py:448)
    groupOverlay = new QGroupBox(this);
    layout->addWidget(groupOverlay);
    auto layoutHorizOverlay = new QHBoxLayout(groupOverlay);
    auto layoutOverlay = new QVBoxLayout();
    layoutHorizOverlay->addLayout(layoutOverlay);
    layoutHorizOverlay->addStretch();

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayHideTabBar = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayHideTabBar);
    DockOverlayHideTabBar->setChecked(Gui::OverlayParams::defaultDockOverlayHideTabBar());
    DockOverlayHideTabBar->setEntryName("DockOverlayHideTabBar");
    DockOverlayHideTabBar->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayHidePropertyViewScrollBar = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayHidePropertyViewScrollBar);
    DockOverlayHidePropertyViewScrollBar->setChecked(Gui::OverlayParams::defaultDockOverlayHidePropertyViewScrollBar());
    DockOverlayHidePropertyViewScrollBar->setEntryName("DockOverlayHidePropertyViewScrollBar");
    DockOverlayHidePropertyViewScrollBar->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayAutoView = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayAutoView);
    DockOverlayAutoView->setChecked(Gui::OverlayParams::defaultDockOverlayAutoView());
    DockOverlayAutoView->setEntryName("DockOverlayAutoView");
    DockOverlayAutoView->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayAutoMouseThrough = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayAutoMouseThrough);
    DockOverlayAutoMouseThrough->setChecked(Gui::OverlayParams::defaultDockOverlayAutoMouseThrough());
    DockOverlayAutoMouseThrough->setEntryName("DockOverlayAutoMouseThrough");
    DockOverlayAutoMouseThrough->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayWheelPassThrough = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayWheelPassThrough);
    DockOverlayWheelPassThrough->setChecked(Gui::OverlayParams::defaultDockOverlayWheelPassThrough());
    DockOverlayWheelPassThrough->setEntryName("DockOverlayWheelPassThrough");
    DockOverlayWheelPassThrough->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayWheelDelay = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayWheelDelay);
    DockOverlayWheelDelay = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayWheelDelay);
    DockOverlayWheelDelay->setValue(Gui::OverlayParams::defaultDockOverlayWheelDelay());
    DockOverlayWheelDelay->setEntryName("DockOverlayWheelDelay");
    DockOverlayWheelDelay->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayWheelDelay->setMinimum(0);
    DockOverlayWheelDelay->setMaximum(99999);
    DockOverlayWheelDelay->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayAlphaRadius = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayAlphaRadius);
    DockOverlayAlphaRadius = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayAlphaRadius);
    DockOverlayAlphaRadius->setValue(Gui::OverlayParams::defaultDockOverlayAlphaRadius());
    DockOverlayAlphaRadius->setEntryName("DockOverlayAlphaRadius");
    DockOverlayAlphaRadius->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayAlphaRadius->setMinimum(1);
    DockOverlayAlphaRadius->setMaximum(100);
    DockOverlayAlphaRadius->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayCheckNaviCube = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayCheckNaviCube);
    DockOverlayCheckNaviCube->setChecked(Gui::OverlayParams::defaultDockOverlayCheckNaviCube());
    DockOverlayCheckNaviCube->setEntryName("DockOverlayCheckNaviCube");
    DockOverlayCheckNaviCube->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintTriggerSize = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintTriggerSize);
    DockOverlayHintTriggerSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintTriggerSize);
    DockOverlayHintTriggerSize->setValue(Gui::OverlayParams::defaultDockOverlayHintTriggerSize());
    DockOverlayHintTriggerSize->setEntryName("DockOverlayHintTriggerSize");
    DockOverlayHintTriggerSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintTriggerSize->setMinimum(1);
    DockOverlayHintTriggerSize->setMaximum(100);
    DockOverlayHintTriggerSize->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintSize = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintSize);
    DockOverlayHintSize = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintSize);
    DockOverlayHintSize->setValue(Gui::OverlayParams::defaultDockOverlayHintSize());
    DockOverlayHintSize->setEntryName("DockOverlayHintSize");
    DockOverlayHintSize->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintSize->setMinimum(1);
    DockOverlayHintSize->setMaximum(100);
    DockOverlayHintSize->setSingleStep(1);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintLeftOffset = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintLeftOffset);
    DockOverlayHintLeftOffset = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintLeftOffset);
    DockOverlayHintLeftOffset->setValue(Gui::OverlayParams::defaultDockOverlayHintLeftOffset());
    DockOverlayHintLeftOffset->setEntryName("DockOverlayHintLeftOffset");
    DockOverlayHintLeftOffset->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintLeftOffset->setMinimum(0);
    DockOverlayHintLeftOffset->setMaximum(10000);
    DockOverlayHintLeftOffset->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintLeftLength = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintLeftLength);
    DockOverlayHintLeftLength = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintLeftLength);
    DockOverlayHintLeftLength->setValue(Gui::OverlayParams::defaultDockOverlayHintLeftLength());
    DockOverlayHintLeftLength->setEntryName("DockOverlayHintLeftLength");
    DockOverlayHintLeftLength->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintLeftLength->setMinimum(0);
    DockOverlayHintLeftLength->setMaximum(10000);
    DockOverlayHintLeftLength->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintRightOffset = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintRightOffset);
    DockOverlayHintRightOffset = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintRightOffset);
    DockOverlayHintRightOffset->setValue(Gui::OverlayParams::defaultDockOverlayHintRightOffset());
    DockOverlayHintRightOffset->setEntryName("DockOverlayHintRightOffset");
    DockOverlayHintRightOffset->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintRightOffset->setMinimum(0);
    DockOverlayHintRightOffset->setMaximum(10000);
    DockOverlayHintRightOffset->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintRightLength = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintRightLength);
    DockOverlayHintRightLength = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintRightLength);
    DockOverlayHintRightLength->setValue(Gui::OverlayParams::defaultDockOverlayHintRightLength());
    DockOverlayHintRightLength->setEntryName("DockOverlayHintRightLength");
    DockOverlayHintRightLength->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintRightLength->setMinimum(0);
    DockOverlayHintRightLength->setMaximum(10000);
    DockOverlayHintRightLength->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintTopOffset = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintTopOffset);
    DockOverlayHintTopOffset = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintTopOffset);
    DockOverlayHintTopOffset->setValue(Gui::OverlayParams::defaultDockOverlayHintTopOffset());
    DockOverlayHintTopOffset->setEntryName("DockOverlayHintTopOffset");
    DockOverlayHintTopOffset->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintTopOffset->setMinimum(0);
    DockOverlayHintTopOffset->setMaximum(10000);
    DockOverlayHintTopOffset->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintTopLength = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintTopLength);
    DockOverlayHintTopLength = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintTopLength);
    DockOverlayHintTopLength->setValue(Gui::OverlayParams::defaultDockOverlayHintTopLength());
    DockOverlayHintTopLength->setEntryName("DockOverlayHintTopLength");
    DockOverlayHintTopLength->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintTopLength->setMinimum(0);
    DockOverlayHintTopLength->setMaximum(10000);
    DockOverlayHintTopLength->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintBottomOffset = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintBottomOffset);
    DockOverlayHintBottomOffset = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintBottomOffset);
    DockOverlayHintBottomOffset->setValue(Gui::OverlayParams::defaultDockOverlayHintBottomOffset());
    DockOverlayHintBottomOffset->setEntryName("DockOverlayHintBottomOffset");
    DockOverlayHintBottomOffset->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintBottomOffset->setMinimum(0);
    DockOverlayHintBottomOffset->setMaximum(10000);
    DockOverlayHintBottomOffset->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintBottomLength = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintBottomLength);
    DockOverlayHintBottomLength = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintBottomLength);
    DockOverlayHintBottomLength->setValue(Gui::OverlayParams::defaultDockOverlayHintBottomLength());
    DockOverlayHintBottomLength->setEntryName("DockOverlayHintBottomLength");
    DockOverlayHintBottomLength->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintBottomLength->setMinimum(0);
    DockOverlayHintBottomLength->setMaximum(10000);
    DockOverlayHintBottomLength->setSingleStep(10);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayHintTabBar = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayHintTabBar);
    DockOverlayHintTabBar->setChecked(Gui::OverlayParams::defaultDockOverlayHintTabBar());
    DockOverlayHintTabBar->setEntryName("DockOverlayHintTabBar");
    DockOverlayHintTabBar->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayHintDelay = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayHintDelay);
    DockOverlayHintDelay = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayHintDelay);
    DockOverlayHintDelay->setValue(Gui::OverlayParams::defaultDockOverlayHintDelay());
    DockOverlayHintDelay->setEntryName("DockOverlayHintDelay");
    DockOverlayHintDelay->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayHintDelay->setMinimum(0);
    DockOverlayHintDelay->setMaximum(1000);
    DockOverlayHintDelay->setSingleStep(100);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlaySplitterHandleTimeout = new QLabel(this);
    layoutRow->addWidget(labelDockOverlaySplitterHandleTimeout);
    DockOverlaySplitterHandleTimeout = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlaySplitterHandleTimeout);
    DockOverlaySplitterHandleTimeout->setValue(Gui::OverlayParams::defaultDockOverlaySplitterHandleTimeout());
    DockOverlaySplitterHandleTimeout->setEntryName("DockOverlaySplitterHandleTimeout");
    DockOverlaySplitterHandleTimeout->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlaySplitterHandleTimeout->setMinimum(0);
    DockOverlaySplitterHandleTimeout->setMaximum(99999);
    DockOverlaySplitterHandleTimeout->setSingleStep(100);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    DockOverlayActivateOnHover = new Gui::PrefCheckBox(this);
    layoutRow->addWidget(DockOverlayActivateOnHover);
    DockOverlayActivateOnHover->setChecked(Gui::OverlayParams::defaultDockOverlayActivateOnHover());
    DockOverlayActivateOnHover->setEntryName("DockOverlayActivateOnHover");
    DockOverlayActivateOnHover->setParamGrpPath("View");

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayDelay = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayDelay);
    DockOverlayDelay = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayDelay);
    DockOverlayDelay->setValue(Gui::OverlayParams::defaultDockOverlayDelay());
    DockOverlayDelay->setEntryName("DockOverlayDelay");
    DockOverlayDelay->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayDelay->setMinimum(0);
    DockOverlayDelay->setMaximum(5000);
    DockOverlayDelay->setSingleStep(100);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayAnimationDuration = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayAnimationDuration);
    DockOverlayAnimationDuration = new Gui::PrefSpinBox(this);
    layoutRow->addWidget(DockOverlayAnimationDuration);
    DockOverlayAnimationDuration->setValue(Gui::OverlayParams::defaultDockOverlayAnimationDuration());
    DockOverlayAnimationDuration->setEntryName("DockOverlayAnimationDuration");
    DockOverlayAnimationDuration->setParamGrpPath("View");
    // Auto generated code (Tools/params_utils.py:1240)
    DockOverlayAnimationDuration->setMinimum(0);
    DockOverlayAnimationDuration->setMaximum(5000);
    DockOverlayAnimationDuration->setSingleStep(100);

    // Auto generated code (Tools/params_utils.py:461)
    layoutRow = new QHBoxLayout();

    // Auto generated code (Tools/params_utils.py:467)
    layoutOverlay->addLayout(layoutRow);
    labelDockOverlayAnimationCurve = new QLabel(this);
    layoutRow->addWidget(labelDockOverlayAnimationCurve);
    DockOverlayAnimationCurve = new Gui::PrefComboBox(this);
    layoutRow->addWidget(DockOverlayAnimationCurve);
    DockOverlayAnimationCurve->setEntryName("DockOverlayAnimationCurve");
    DockOverlayAnimationCurve->setParamGrpPath("View");
    // Auto generated code (Gui/OverlayParams.py:94)
    for (const auto &item : OverlayParams::AnimationCurveTypes)
        DockOverlayAnimationCurve->addItem(item);
    DockOverlayAnimationCurve->setCurrentIndex(Gui::OverlayParams::defaultDockOverlayAnimationCurve());
    layout->addItem(new QSpacerItem(40, 20, QSizePolicy::Fixed, QSizePolicy::Expanding));
    retranslateUi();
    // Auto generated code (Tools/params_utils.py:645)
    init();
}

// Auto generated code (Tools/params_utils.py:652)
DlgSettingsUI::~DlgSettingsUI()
{
    
}

// Auto generated code (Tools/params_utils.py:661)
void DlgSettingsUI::saveSettings()
{
    // Auto generated code (Tools/params_utils.py:497)
    TextCursorWidth->onSave();
    UseViewArea->onSave();
    DocumentTarget->onSave();
    DocViewTarget->onSave();
    UtilityTarget->onSave();
    SplitDirection->onSave();
    ItemBackground->onSave();
    ItemBackgroundPadding->onSave();
    ResizableColumn->onSave();
    CheckBoxesSelection->onSave();
    HideColumn->onSave();
    HideScrollBar->onSave();
    HideHeaderView->onSave();
    TreeToolTipIcon->onSave();
    AutoHideEditorIcon->onSave();
    EditorTrigger->onSave();
    NoSystemBackground->onSave();
    EditDialogBGAlpha->onSave();
    PieMenuIconSize->onSave();
    PieMenuRadius->onSave();
    PieMenuTriggerRadius->onSave();
    PieMenuCenterRadius->onSave();
    PieMenuFontSize->onSave();
    PieMenuTriggerDelay->onSave();
    PieMenuTriggerAction->onSave();
    PieMenuAnimationDuration->onSave();
    PieMenuAnimationCurve->onSave();
    PieMenuPopup->onSave();
    DockOverlayHideTabBar->onSave();
    DockOverlayHidePropertyViewScrollBar->onSave();
    DockOverlayAutoView->onSave();
    DockOverlayAutoMouseThrough->onSave();
    DockOverlayWheelPassThrough->onSave();
    DockOverlayWheelDelay->onSave();
    DockOverlayAlphaRadius->onSave();
    DockOverlayCheckNaviCube->onSave();
    DockOverlayHintTriggerSize->onSave();
    DockOverlayHintSize->onSave();
    DockOverlayHintLeftOffset->onSave();
    DockOverlayHintLeftLength->onSave();
    DockOverlayHintRightOffset->onSave();
    DockOverlayHintRightLength->onSave();
    DockOverlayHintTopOffset->onSave();
    DockOverlayHintTopLength->onSave();
    DockOverlayHintBottomOffset->onSave();
    DockOverlayHintBottomLength->onSave();
    DockOverlayHintTabBar->onSave();
    DockOverlayHintDelay->onSave();
    DockOverlaySplitterHandleTimeout->onSave();
    DockOverlayActivateOnHover->onSave();
    DockOverlayDelay->onSave();
    DockOverlayAnimationDuration->onSave();
    DockOverlayAnimationCurve->onSave();
}

// Auto generated code (Tools/params_utils.py:670)
void DlgSettingsUI::loadSettings()
{
    // Auto generated code (Tools/params_utils.py:484)
    TextCursorWidth->onRestore();
    UseViewArea->onRestore();
    DocumentTarget->onRestore();
    DocViewTarget->onRestore();
    UtilityTarget->onRestore();
    SplitDirection->onRestore();
    ItemBackground->onRestore();
    ItemBackgroundPadding->onRestore();
    ResizableColumn->onRestore();
    CheckBoxesSelection->onRestore();
    HideColumn->onRestore();
    HideScrollBar->onRestore();
    HideHeaderView->onRestore();
    TreeToolTipIcon->onRestore();
    AutoHideEditorIcon->onRestore();
    EditorTrigger->onRestore();
    NoSystemBackground->onRestore();
    EditDialogBGAlpha->onRestore();
    PieMenuIconSize->onRestore();
    PieMenuRadius->onRestore();
    PieMenuTriggerRadius->onRestore();
    PieMenuCenterRadius->onRestore();
    PieMenuFontSize->onRestore();
    PieMenuTriggerDelay->onRestore();
    PieMenuTriggerAction->onRestore();
    PieMenuAnimationDuration->onRestore();
    PieMenuAnimationCurve->onRestore();
    PieMenuPopup->onRestore();
    DockOverlayHideTabBar->onRestore();
    DockOverlayHidePropertyViewScrollBar->onRestore();
    DockOverlayAutoView->onRestore();
    DockOverlayAutoMouseThrough->onRestore();
    DockOverlayWheelPassThrough->onRestore();
    DockOverlayWheelDelay->onRestore();
    DockOverlayAlphaRadius->onRestore();
    DockOverlayCheckNaviCube->onRestore();
    DockOverlayHintTriggerSize->onRestore();
    DockOverlayHintSize->onRestore();
    DockOverlayHintLeftOffset->onRestore();
    DockOverlayHintLeftLength->onRestore();
    DockOverlayHintRightOffset->onRestore();
    DockOverlayHintRightLength->onRestore();
    DockOverlayHintTopOffset->onRestore();
    DockOverlayHintTopLength->onRestore();
    DockOverlayHintBottomOffset->onRestore();
    DockOverlayHintBottomLength->onRestore();
    DockOverlayHintTabBar->onRestore();
    DockOverlayHintDelay->onRestore();
    DockOverlaySplitterHandleTimeout->onRestore();
    DockOverlayActivateOnHover->onRestore();
    DockOverlayDelay->onRestore();
    DockOverlayAnimationDuration->onRestore();
    DockOverlayAnimationCurve->onRestore();
}

// Auto generated code (Tools/params_utils.py:679)
void DlgSettingsUI::retranslateUi()
{
    setWindowTitle(QObject::tr("UI"));
    groupGeneral->setTitle(QObject::tr("General"));
    TextCursorWidth->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docTextCursorWidth()));
    labelTextCursorWidth->setText(QObject::tr("Text cursor width"));
    labelTextCursorWidth->setToolTip(TextCursorWidth->toolTip());
    groupViews->setTitle(QObject::tr("Views"));
    UseViewArea->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docUseViewArea()));
    UseViewArea->setText(QObject::tr("Tile views inside one tab"));
    DocumentTarget->setToolTip(QApplication::translate("OpenViewParams", Gui::OpenViewParams::docDocumentTarget()));
    labelDocumentTarget->setText(QObject::tr("New documents open in"));
    labelDocumentTarget->setToolTip(DocumentTarget->toolTip());
    // Auto generated code (Gui/OpenViewParams.py:95)
    DocumentTarget->setItemText(0, QObject::tr("Their own tab"));
    DocumentTarget->setItemText(1, QObject::tr("A split beside the current view"));
    DocumentTarget->setItemText(2, QObject::tr("A floating window"));
    DocViewTarget->setToolTip(QApplication::translate("OpenViewParams", Gui::OpenViewParams::docDocViewTarget()));
    labelDocViewTarget->setText(QObject::tr("Additional views of a document open in"));
    labelDocViewTarget->setToolTip(DocViewTarget->toolTip());
    // Auto generated code (Gui/OpenViewParams.py:95)
    DocViewTarget->setItemText(0, QObject::tr("Their own tab"));
    DocViewTarget->setItemText(1, QObject::tr("A split beside the current view"));
    DocViewTarget->setItemText(2, QObject::tr("A new split, never reusing a cell"));
    DocViewTarget->setItemText(3, QObject::tr("A floating window"));
    UtilityTarget->setToolTip(QApplication::translate("OpenViewParams", Gui::OpenViewParams::docUtilityTarget()));
    labelUtilityTarget->setText(QObject::tr("Utility windows open in"));
    labelUtilityTarget->setToolTip(UtilityTarget->toolTip());
    // Auto generated code (Gui/OpenViewParams.py:95)
    UtilityTarget->setItemText(0, QObject::tr("Their own tab"));
    UtilityTarget->setItemText(1, QObject::tr("A split beside the current view"));
    UtilityTarget->setItemText(2, QObject::tr("A floating window"));
    SplitDirection->setToolTip(QApplication::translate("OpenViewParams", Gui::OpenViewParams::docSplitDirection()));
    labelSplitDirection->setText(QObject::tr("New splits go"));
    labelSplitDirection->setToolTip(SplitDirection->toolTip());
    // Auto generated code (Gui/OpenViewParams.py:95)
    SplitDirection->setItemText(0, QObject::tr("Along the longer side"));
    SplitDirection->setItemText(1, QObject::tr("To the right"));
    SplitDirection->setItemText(2, QObject::tr("Below"));
    hintSplitDirection->setText(QObject::tr("Hold Alt while opening to invert tab/split for that one view."));
    groupTreeview->setTitle(QObject::tr("Tree view"));
    ItemBackground->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docItemBackground()));
    labelItemBackground->setText(QObject::tr("Item background color"));
    labelItemBackground->setToolTip(ItemBackground->toolTip());
    ItemBackgroundPadding->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docItemBackgroundPadding()));
    labelItemBackgroundPadding->setText(QObject::tr("Item background padding"));
    labelItemBackgroundPadding->setToolTip(ItemBackgroundPadding->toolTip());
    ResizableColumn->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docResizableColumn()));
    ResizableColumn->setText(QObject::tr("Resizable columns"));
    CheckBoxesSelection->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docCheckBoxesSelection()));
    CheckBoxesSelection->setText(QObject::tr("Add checkboxes for selection in document tree"));
    HideColumn->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docHideColumn()));
    HideColumn->setText(QObject::tr("Hide extra column"));
    HideScrollBar->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docHideScrollBar()));
    HideScrollBar->setText(QObject::tr("Hide scroll bar"));
    HideHeaderView->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docHideHeaderView()));
    HideHeaderView->setText(QObject::tr("Hide header"));
    TreeToolTipIcon->setToolTip(QApplication::translate("TreeParams", Gui::TreeParams::docTreeToolTipIcon()));
    TreeToolTipIcon->setText(QObject::tr("Show icon in tool tip"));
    groupExpression->setTitle(QObject::tr("Expression"));
    AutoHideEditorIcon->setToolTip(QApplication::translate("ExprParams", Gui::ExprParams::docAutoHideEditorIcon()));
    AutoHideEditorIcon->setText(QObject::tr("Auto hide editor icon"));
    EditorTrigger->setToolTip(QApplication::translate("ExprParams", Gui::ExprParams::docEditorTrigger()));
    labelEditorTrigger->setText(QObject::tr("Editor trigger shortcut"));
    labelEditorTrigger->setToolTip(EditorTrigger->toolTip());
    NoSystemBackground->setToolTip(QApplication::translate("ExprParams", Gui::ExprParams::docNoSystemBackground()));
    NoSystemBackground->setText(QObject::tr("In place editing"));
    EditDialogBGAlpha->setToolTip(QApplication::translate("ExprParams", Gui::ExprParams::docEditDialogBGAlpha()));
    labelEditDialogBGAlpha->setText(QObject::tr("Background opacity"));
    labelEditDialogBGAlpha->setToolTip(EditDialogBGAlpha->toolTip());
    groupPiemenu->setTitle(QObject::tr("Pie menu"));
    PieMenuIconSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuIconSize()));
    labelPieMenuIconSize->setText(QObject::tr("Icon size"));
    labelPieMenuIconSize->setToolTip(PieMenuIconSize->toolTip());
    PieMenuRadius->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuRadius()));
    labelPieMenuRadius->setText(QObject::tr("Radius"));
    labelPieMenuRadius->setToolTip(PieMenuRadius->toolTip());
    PieMenuTriggerRadius->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuTriggerRadius()));
    labelPieMenuTriggerRadius->setText(QObject::tr("Trigger radius"));
    labelPieMenuTriggerRadius->setToolTip(PieMenuTriggerRadius->toolTip());
    PieMenuCenterRadius->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuCenterRadius()));
    labelPieMenuCenterRadius->setText(QObject::tr("Center radius"));
    labelPieMenuCenterRadius->setToolTip(PieMenuCenterRadius->toolTip());
    PieMenuFontSize->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuFontSize()));
    labelPieMenuFontSize->setText(QObject::tr("Font size"));
    labelPieMenuFontSize->setToolTip(PieMenuFontSize->toolTip());
    PieMenuTriggerDelay->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuTriggerDelay()));
    labelPieMenuTriggerDelay->setText(QObject::tr("Trigger delay (ms)"));
    labelPieMenuTriggerDelay->setToolTip(PieMenuTriggerDelay->toolTip());
    PieMenuTriggerAction->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuTriggerAction()));
    PieMenuTriggerAction->setText(QObject::tr("Trigger action"));
    PieMenuAnimationDuration->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuAnimationDuration()));
    labelPieMenuAnimationDuration->setText(QObject::tr("Animation duration (ms)"));
    labelPieMenuAnimationDuration->setToolTip(PieMenuAnimationDuration->toolTip());
    PieMenuAnimationCurve->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuAnimationCurve()));
    labelPieMenuAnimationCurve->setText(QObject::tr("Animation curve type"));
    labelPieMenuAnimationCurve->setToolTip(PieMenuAnimationCurve->toolTip());
    PieMenuPopup->setToolTip(QApplication::translate("ViewParams", Gui::ViewParams::docPieMenuPopup()));
    PieMenuPopup->setText(QObject::tr("Show pie menu as popup"));
    groupOverlay->setTitle(QObject::tr("Overlay"));
    DockOverlayHideTabBar->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHideTabBar()));
    DockOverlayHideTabBar->setText(QObject::tr("Hide tab bar"));
    DockOverlayHidePropertyViewScrollBar->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHidePropertyViewScrollBar()));
    DockOverlayHidePropertyViewScrollBar->setText(QObject::tr("Hide property view scroll bar"));
    DockOverlayAutoView->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayAutoView()));
    DockOverlayAutoView->setText(QObject::tr("Auto hide in non 3D view"));
    DockOverlayAutoMouseThrough->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayAutoMouseThrough()));
    DockOverlayAutoMouseThrough->setText(QObject::tr("Auto mouse pass through"));
    DockOverlayWheelPassThrough->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayWheelPassThrough()));
    DockOverlayWheelPassThrough->setText(QObject::tr("Auto mouse wheel pass through"));
    DockOverlayWheelDelay->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayWheelDelay()));
    labelDockOverlayWheelDelay->setText(QObject::tr("Delay mouse wheel pass through (ms)"));
    labelDockOverlayWheelDelay->setToolTip(DockOverlayWheelDelay->toolTip());
    DockOverlayAlphaRadius->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayAlphaRadius()));
    labelDockOverlayAlphaRadius->setText(QObject::tr("Alpha test radius"));
    labelDockOverlayAlphaRadius->setToolTip(DockOverlayAlphaRadius->toolTip());
    DockOverlayCheckNaviCube->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayCheckNaviCube()));
    DockOverlayCheckNaviCube->setText(QObject::tr("Check Navigation Cube"));
    DockOverlayHintTriggerSize->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintTriggerSize()));
    labelDockOverlayHintTriggerSize->setText(QObject::tr("Hint trigger size"));
    labelDockOverlayHintTriggerSize->setToolTip(DockOverlayHintTriggerSize->toolTip());
    DockOverlayHintSize->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintSize()));
    labelDockOverlayHintSize->setText(QObject::tr("Hint width"));
    labelDockOverlayHintSize->setToolTip(DockOverlayHintSize->toolTip());
    DockOverlayHintLeftOffset->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintLeftOffset()));
    labelDockOverlayHintLeftOffset->setText(QObject::tr("Left panel hint offset"));
    labelDockOverlayHintLeftOffset->setToolTip(DockOverlayHintLeftOffset->toolTip());
    DockOverlayHintLeftLength->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintLeftLength()));
    labelDockOverlayHintLeftLength->setText(QObject::tr("Left panel hint length"));
    labelDockOverlayHintLeftLength->setToolTip(DockOverlayHintLeftLength->toolTip());
    DockOverlayHintRightOffset->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintRightOffset()));
    labelDockOverlayHintRightOffset->setText(QObject::tr("Right panel hint offset"));
    labelDockOverlayHintRightOffset->setToolTip(DockOverlayHintRightOffset->toolTip());
    DockOverlayHintRightLength->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintRightLength()));
    labelDockOverlayHintRightLength->setText(QObject::tr("Right panel hint length"));
    labelDockOverlayHintRightLength->setToolTip(DockOverlayHintRightLength->toolTip());
    DockOverlayHintTopOffset->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintTopOffset()));
    labelDockOverlayHintTopOffset->setText(QObject::tr("Top panel hint offset"));
    labelDockOverlayHintTopOffset->setToolTip(DockOverlayHintTopOffset->toolTip());
    DockOverlayHintTopLength->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintTopLength()));
    labelDockOverlayHintTopLength->setText(QObject::tr("Top panel hint length"));
    labelDockOverlayHintTopLength->setToolTip(DockOverlayHintTopLength->toolTip());
    DockOverlayHintBottomOffset->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintBottomOffset()));
    labelDockOverlayHintBottomOffset->setText(QObject::tr("Bottom panel hint offset"));
    labelDockOverlayHintBottomOffset->setToolTip(DockOverlayHintBottomOffset->toolTip());
    DockOverlayHintBottomLength->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintBottomLength()));
    labelDockOverlayHintBottomLength->setText(QObject::tr("Bottom panel hint length"));
    labelDockOverlayHintBottomLength->setToolTip(DockOverlayHintBottomLength->toolTip());
    DockOverlayHintTabBar->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintTabBar()));
    DockOverlayHintTabBar->setText(QObject::tr("Hint show tab bar"));
    DockOverlayHintDelay->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayHintDelay()));
    labelDockOverlayHintDelay->setText(QObject::tr("Hint delay (ms)"));
    labelDockOverlayHintDelay->setToolTip(DockOverlayHintDelay->toolTip());
    DockOverlaySplitterHandleTimeout->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlaySplitterHandleTimeout()));
    labelDockOverlaySplitterHandleTimeout->setText(QObject::tr("Splitter auto hide delay (ms)"));
    labelDockOverlaySplitterHandleTimeout->setToolTip(DockOverlaySplitterHandleTimeout->toolTip());
    DockOverlayActivateOnHover->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayActivateOnHover()));
    DockOverlayActivateOnHover->setText(QObject::tr("Activate on hover"));
    DockOverlayDelay->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayDelay()));
    labelDockOverlayDelay->setText(QObject::tr("Layout delay (ms)"));
    labelDockOverlayDelay->setToolTip(DockOverlayDelay->toolTip());
    DockOverlayAnimationDuration->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayAnimationDuration()));
    labelDockOverlayAnimationDuration->setText(QObject::tr("Animation duration (ms)"));
    labelDockOverlayAnimationDuration->setToolTip(DockOverlayAnimationDuration->toolTip());
    DockOverlayAnimationCurve->setToolTip(QApplication::translate("OverlayParams", Gui::OverlayParams::docDockOverlayAnimationCurve()));
    labelDockOverlayAnimationCurve->setText(QObject::tr("Animation curve type"));
    labelDockOverlayAnimationCurve->setToolTip(DockOverlayAnimationCurve->toolTip());
}

// Auto generated code (Tools/params_utils.py:697)
void DlgSettingsUI::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(e);
}

// Auto generated code (Tools/params_utils.py:710)
#include "moc_DlgSettingsUI.cpp"
//[[[end]]]

// -----------------------------------------------------------------------------------
// user code start

void DlgSettingsUI::init()
{
    // Every split placement needs the view area (docs/ViewPlacement.md
    // sec 4.1): with it off each view gets its own tab, so the choices
    // that ask for a split stay visible -- they explain what the
    // checkbox buys -- but cannot be picked.
    auto splitChoicesEnabled = [this](bool on) {
        labelSplitDirection->setEnabled(on);
        SplitDirection->setEnabled(on);
        for (auto combo : {DocumentTarget, DocViewTarget, UtilityTarget}) {
            auto model = qobject_cast<QStandardItemModel*>(combo->model());
            if (!model)
                continue;
            for (int i = 0; i < combo->count(); ++i) {
                const QByteArray value = combo->itemData(i).toByteArray();
                if (value != "Split" && value != "NewSplit")
                    continue;
                if (auto item = model->item(i))
                    item->setEnabled(on);
            }
        }
    };
    QObject::connect(UseViewArea, &QCheckBox::toggled, this, splitChoicesEnabled);
    splitChoicesEnabled(UseViewArea->isChecked());

    timer = new QTimer(this);
    timer->setSingleShot(true);

    animator1 = new QPropertyAnimation(this, "offset1", this);
    QObject::connect(animator1, &QPropertyAnimation::stateChanged, [this]() {
        if (animator1->state() != QAbstractAnimation::Running)
            timer->start(1000);
    });
    animator2 = new QPropertyAnimation(this, "offset2", this);
    QObject::connect(animator2, &QPropertyAnimation::stateChanged, [this]() {
        if (animator2->state() != QAbstractAnimation::Running)
            timer->start(1000);
    });

    QObject::connect(DockOverlayAnimationCurve, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, &DlgSettingsUI::onCurveChange);
    QObject::connect(PieMenuAnimationCurve, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, &DlgSettingsUI::onCurveChange);

    QObject::connect(timer, &QTimer::timeout, [=]() {
        if (animator1->state() != QAbstractAnimation::Running) {
            this->setOffset1(1);
            this->a1 = this->b1 = 0;
        }
        if (animator2->state() != QAbstractAnimation::Running) {
            this->setOffset2(0);
            this->a2 = this->b2 = 0;
        }
    });
}

qreal DlgSettingsUI::offset1() const
{
    return this->t1;
}

void DlgSettingsUI::setOffset1(qreal t)
{
    if (t == this->t1)
        return;
    this->t1 = t;
    QLabel *label = this->labelDockOverlayAnimationCurve;
    if (this->a1 == this->b1) {
        this->a1 = label->x();
        QPoint pos(width(), 0);
        this->b1 = width() - label->fontMetrics().boundingRect(label->text()).width() - 5;
    }
    label->move(this->a1 * (1-t) + this->b1 * t, label->y());
}

qreal DlgSettingsUI::offset2() const
{
    return this->t2;
}

void DlgSettingsUI::setOffset2(qreal t)
{
    if (t == this->t2)
        return;
    this->t2 = t;
    QLabel *label = this->labelPieMenuAnimationCurve;
    if (this->a2 == this->b2) {
        this->a2 = label->x();
        QPoint pos(width(), 0);
        this->b2 = width() - label->fontMetrics().boundingRect(label->text()).width();
    }
    label->move(this->a2 * (1-t) + this->b2 * t, label->y());
}

void DlgSettingsUI::onCurveChange(int index)
{
    auto animator = sender() == DockOverlayAnimationCurve ? animator1 : animator2;
    animator->setStartValue(0.0);
    animator->setEndValue(1.0);
    animator->setEasingCurve((QEasingCurve::Type)index);
    animator->setDuration(animator == animator1 ?
            DockOverlayAnimationDuration->value()*2 : PieMenuAnimationDuration->value()*2);
    animator->start();
}

// user code end
// -----------------------------------------------------------------------------------
