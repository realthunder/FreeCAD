/***************************************************************************
 *   Copyright (c) 2009 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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
# include <QApplication>
# include <QGridLayout>
# include <QGroupBox>
# include <QLabel>
# include <QPushButton>
#endif

#include <App/MaterialAppearance.h>
#include "DlgSettingsViewColor.h"
#include "ui_DlgSettingsViewColor.h"
#include "OverlayParams.h"
#include "PrefWidgets.h"
#include "TreeParams.h"
#include "ViewParams.h"

using namespace Gui::Dialog;

/* TRANSLATOR Gui::Dialog::DlgSettingsViewColor */

/**
 *  Constructs a DlgSettingsViewColor which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 */
DlgSettingsViewColor::DlgSettingsViewColor(QWidget* parent)
    : PreferencePage(parent)
    , ui(new Ui_DlgSettingsViewColor)
{
    ui->setupUi(this);

    ui->SelectionColor->setColor(App::Color(
                (uint32_t)ViewParams::getSelectionColor()).asValue<QColor>());

    ui->HighlightColor->setColor(App::Color(
                (uint32_t)ViewParams::getHighlightColor()).asValue<QColor>());

    ui->TreeEditColor->setColor(App::Color(
                (uint32_t)TreeParams::getTreeEditColor()).asValue<QColor>());
    ui->TreeActiveColor->setColor(App::Color(
                (uint32_t)TreeParams::getTreeActiveColor()).asValue<QColor>());
    connect(ui->SwitchGradientColors, &QPushButton::pressed, this,
        &DlgSettingsViewColor::onSwitchGradientColorsPressed);

    connect(ui->radioButtonSimple, &QRadioButton::toggled, this,
        &DlgSettingsViewColor::onRadioButtonSimpleToggled);

    connect(ui->radioButtonGradient, &QRadioButton::toggled, this,
        &DlgSettingsViewColor::onRadioButtonGradientToggled);

    connect(ui->rbRadialGradient, &QRadioButton::toggled, this,
        &DlgSettingsViewColor::onRadioButtonRadialGradientToggled);

    connect(ui->checkMidColor, &QCheckBox::toggled, this,
        &DlgSettingsViewColor::onCheckMidColorToggled);

    setupOverlay();
    setupDisplayStyle();
    retranslateExtra();
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgSettingsViewColor::~DlgSettingsViewColor() = default;

/// A group box put in the page's column below the others, above its last
/// spacer
static QGroupBox *addGroup(QWidget *page, QVBoxLayout *column)
{
    auto group = new QGroupBox(page);
    group->setLayout(new QGridLayout());
    int at = column->count();
    if (at > 0 && column->itemAt(at - 1)->spacerItem())
        --at;
    column->insertWidget(at, group);
    return group;
}

/// A colour button of a preference, as the generated pages make one
static Gui::PrefColorButton *colourButton(QWidget *parent, const char *entry, const char *group,
                                          unsigned long colour, bool transparency)
{
    auto button = new Gui::PrefColorButton(parent);
    button->setPackedColor(colour);
    button->setEntryName(entry);
    button->setParamGrpPath(group);
    button->setAllowTransparency(transparency);
    return button;
}

static Gui::PrefCheckBox *checkBox(QWidget *parent, const char *entry, const char *group,
                                   bool checked)
{
    auto box = new Gui::PrefCheckBox(parent);
    box->setChecked(checked);
    box->setEntryName(entry);
    box->setParamGrpPath(group);
    return box;
}

void DlgSettingsViewColor::setupOverlay()
{
    // Moved here from Display > UI, "Tree view" (docs/HandsOnQueue.md entry
    // 98, 2026-10-14): the colour backs the text of an overlaid tree, Python
    // console, report view and selection view alike.
    groupOverlay = addGroup(this, ui->verticalLayout);
    groupOverlay->setObjectName(QStringLiteral("groupOverlay"));
    auto grid = static_cast<QGridLayout*>(groupOverlay->layout());
    labelItemBackground = new QLabel(groupOverlay);
    ItemBackground = colourButton(groupOverlay, "ItemBackground", "TreeView",
                                  TreeParams::defaultItemBackground(), true);
    DockOverlayThemeItemColor = checkBox(groupOverlay, "DockOverlayThemeItemColor", "View",
                                         OverlayParams::defaultDockOverlayThemeItemColor());
    grid->addWidget(labelItemBackground, 0, 0);
    grid->addWidget(ItemBackground, 0, 1);
    grid->addWidget(DockOverlayThemeItemColor, 1, 0, 1, 2);
    grid->setColumnStretch(2, 1);
    extraPrefs.insert(extraPrefs.end(), {ItemBackground, DockOverlayThemeItemColor});

    // The colour is used only while "Item background follows the theme" is
    // off: with that on, it cannot be picked (docs/HandsOnQueue.md entry 87)
    auto ownColourEnabled = [this](bool followsTheme) {
        labelItemBackground->setEnabled(!followsTheme);
        ItemBackground->setEnabled(!followsTheme);
    };
    connect(DockOverlayThemeItemColor, &QCheckBox::toggled, this, ownColourEnabled);
    ownColourEnabled(DockOverlayThemeItemColor->isChecked());
}

void DlgSettingsViewColor::setupDisplayStyle()
{
    // The colours of Display > Display styles, here as well: "create a
    // groupbox in that page named 'Display Style', and put color related
    // settings in Display -> Display Style page there (note, do not move,
    // just duplicate those settings)" (docs/HandsOnQueue.md entry 98). The two
    // pages keep each other's widgets up to date through the parameters:
    // whichever is saved first, the other restores what it wrote.
    groupDisplayStyle = addGroup(this, ui->verticalLayout);
    groupDisplayStyle->setObjectName(QStringLiteral("groupDisplayStyle"));
    auto grid = static_cast<QGridLayout*>(groupDisplayStyle->layout());
    int row = 0;
    auto addOverridden = [&](QLabel *&label, Gui::PrefColorButton *&button,
                             Gui::PrefCheckBox *&box, const char *entry, unsigned long colour,
                             const char *override, bool overridden) {
        label = new QLabel(groupDisplayStyle);
        button = colourButton(groupDisplayStyle, entry, "View", colour, true);
        box = checkBox(groupDisplayStyle, override, "View", overridden);
        QWidget *target = button;
        target->setEnabled(box->isChecked());
        connect(box, &QCheckBox::toggled, target, &QWidget::setEnabled);
        grid->addWidget(label, row, 0);
        grid->addWidget(button, row, 1);
        grid->addWidget(box, row, 2);
        ++row;
        extraPrefs.insert(extraPrefs.end(), {button, box});
    };
    addOverridden(labelHiddenLineFaceColor, HiddenLineFaceColor, HiddenLineOverrideFaceColor,
                  "HiddenLineFaceColor", ViewParams::defaultHiddenLineFaceColor(),
                  "HiddenLineOverrideFaceColor", ViewParams::defaultHiddenLineOverrideFaceColor());
    addOverridden(labelHiddenLineColor, HiddenLineColor, HiddenLineOverrideColor,
                  "HiddenLineColor", ViewParams::defaultHiddenLineColor(),
                  "HiddenLineOverrideColor", ViewParams::defaultHiddenLineOverrideColor());
    addOverridden(labelHiddenLineBackground, HiddenLineBackground, HiddenLineOverrideBackground,
                  "HiddenLineBackground", ViewParams::defaultHiddenLineBackground(),
                  "HiddenLineOverrideBackground",
                  ViewParams::defaultHiddenLineOverrideBackground());
    auto addPlain = [&](QLabel *&label, Gui::PrefColorButton *&button, const char *entry,
                        unsigned long colour) {
        label = new QLabel(groupDisplayStyle);
        button = colourButton(groupDisplayStyle, entry, "View", colour, true);
        grid->addWidget(label, row, 0);
        grid->addWidget(button, row, 1);
        ++row;
        extraPrefs.push_back(button);
    };
    addPlain(labelShadowLightColor, ShadowLightColor, "ShadowLightColor",
             ViewParams::defaultShadowLightColor());
    addPlain(labelShadowGroundColor, ShadowGroundColor, "ShadowGroundColor",
             ViewParams::defaultShadowGroundColor());
    grid->setColumnStretch(3, 1);
}

void DlgSettingsViewColor::retranslateExtra()
{
    groupOverlay->setTitle(tr("Overlay"));
    labelItemBackground->setText(tr("Item background color"));
    ItemBackground->setToolTip(
        QApplication::translate("TreeParams", TreeParams::docItemBackground()));
    labelItemBackground->setToolTip(ItemBackground->toolTip());
    DockOverlayThemeItemColor->setText(tr("Item background follows the theme"));
    DockOverlayThemeItemColor->setToolTip(QApplication::translate(
        "OverlayParams", OverlayParams::docDockOverlayThemeItemColor()));

    groupDisplayStyle->setTitle(tr("Display Style"));
    labelHiddenLineFaceColor->setText(tr("Hidden line face color"));
    HiddenLineFaceColor->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docHiddenLineFaceColor()));
    labelHiddenLineFaceColor->setToolTip(HiddenLineFaceColor->toolTip());
    HiddenLineOverrideFaceColor->setText(tr("Override face color"));
    HiddenLineOverrideFaceColor->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docHiddenLineOverrideFaceColor()));
    labelHiddenLineColor->setText(tr("Hidden line color"));
    HiddenLineColor->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docHiddenLineColor()));
    labelHiddenLineColor->setToolTip(HiddenLineColor->toolTip());
    HiddenLineOverrideColor->setText(tr("Override line color"));
    HiddenLineOverrideColor->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docHiddenLineOverrideColor()));
    labelHiddenLineBackground->setText(tr("Hidden line background"));
    HiddenLineBackground->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docHiddenLineBackground()));
    labelHiddenLineBackground->setToolTip(HiddenLineBackground->toolTip());
    HiddenLineOverrideBackground->setText(tr("Override background color"));
    HiddenLineOverrideBackground->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docHiddenLineOverrideBackground()));
    labelShadowLightColor->setText(tr("Shadow light color"));
    ShadowLightColor->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docShadowLightColor()));
    labelShadowLightColor->setToolTip(ShadowLightColor->toolTip());
    labelShadowGroundColor->setText(tr("Shadow ground color"));
    ShadowGroundColor->setToolTip(
        QApplication::translate("ViewParams", ViewParams::docShadowGroundColor()));
    labelShadowGroundColor->setToolTip(ShadowGroundColor->toolTip());
}

void DlgSettingsViewColor::saveSettings()
{
    ui->SelectionColor_Background->onSave();
    ui->backgroundColorFrom->onSave();
    ui->backgroundColorTo->onSave();
    ui->backgroundColorMid->onSave();
    ui->radioButtonSimple->onSave();
    ui->radioButtonGradient->onSave();
    ui->rbRadialGradient->onSave();
    ui->checkMidColor->onSave();
    ui->HighlightColor->onSave();
    ui->SelectionColor->onSave();
    ui->TreeEditColor->onSave();
    ui->TreeActiveColor->onSave();
    ui->SelectingGroupColor->onSave();
    ui->CbLabelColor->onSave();
    ui->CbLabelTextSize->onSave();
    for (auto pref : extraPrefs)
        pref->onSave();
}

void DlgSettingsViewColor::loadSettings()
{
    ui->SelectionColor_Background->onRestore();
    ui->backgroundColorFrom->onRestore();
    ui->backgroundColorTo->onRestore();
    ui->backgroundColorMid->onRestore();
    ui->radioButtonSimple->onRestore();
    ui->radioButtonGradient->onRestore();
    ui->rbRadialGradient->onRestore();
    ui->checkMidColor->onRestore();
    ui->HighlightColor->onRestore();
    ui->SelectionColor->onRestore();
    ui->TreeEditColor->onRestore();
    ui->TreeActiveColor->onRestore();
    ui->SelectingGroupColor->onRestore();

    ui->CbLabelColor->onRestore();
    ui->CbLabelTextSize->onRestore();
    for (auto pref : extraPrefs)
        pref->onRestore();

    if (ui->radioButtonSimple->isChecked())
        onRadioButtonSimpleToggled(true);
    else if(ui->radioButtonGradient->isChecked())
        onRadioButtonGradientToggled(true);
    else
        onRadioButtonRadialGradientToggled(true);
}

/**
 * Sets the strings of the subwidgets using the current language.
 */
void DlgSettingsViewColor::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslateExtra();
    }
    else {
        QWidget::changeEvent(e);
    }
}

void DlgSettingsViewColor::onSwitchGradientColorsPressed()
{
    QColor tempColor = ui->backgroundColorFrom->color();
    ui->backgroundColorFrom->setColor(ui->backgroundColorTo->color());
    ui->backgroundColorTo->setColor(tempColor);
}

void DlgSettingsViewColor::onCheckMidColorToggled(bool val)
{
    ui->color2Label->setEnabled(val);
    ui->backgroundColorMid->setEnabled(val);
}

void DlgSettingsViewColor::onRadioButtonSimpleToggled(bool val)
{
    setGradientColorVisibility(!val);
}

void DlgSettingsViewColor::onRadioButtonGradientToggled(bool val)
{
    setGradientColorVisibility(val);
    ui->color1Label->setText(tr("Top:"));
    ui->color2Label->setText(tr("Middle:"));
    ui->color3Label->setText(tr("Bottom:"));
}

void DlgSettingsViewColor::onRadioButtonRadialGradientToggled(bool val)
{
    setGradientColorVisibility(val);
    ui->color1Label->setText(tr("Central:"));
    ui->color2Label->setText(tr("Midway:"));
    ui->color3Label->setText(tr("End:"));
}

void DlgSettingsViewColor::setGradientColorVisibility(bool val)
{
    ui->SelectionColor_Background->setVisible(!val);
    ui->color1Label->setVisible(val);
    ui->backgroundColorFrom->setVisible(val);
    ui->color2Label->setVisible(val);
    ui->backgroundColorMid->setVisible(val);
    ui->color3Label->setVisible(val);
    ui->backgroundColorTo->setVisible(val);
    ui->checkMidColor->setVisible(val);
    ui->SwitchGradientColors->setVisible(val);

    if (val)
        onCheckMidColorToggled(ui->checkMidColor->isChecked());
}

#include "moc_DlgSettingsViewColor.cpp"
