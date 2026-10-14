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


#ifndef GUI_DIALOG_DLGSETTINGSVIEWCOLOR_H
#define GUI_DIALOG_DLGSETTINGSVIEWCOLOR_H

#include <Gui/PropertyPage.h>
#include <memory>
#include <vector>

class QGroupBox;
class QLabel;

namespace Gui {
class PrefCheckBox;
class PrefColorButton;
class PrefWidget;
namespace Dialog {
class Ui_DlgSettingsViewColor;

/**
 * The DlgSettingsViewColor class implements a preference page to change color settings
 * for the Inventor viewer like background and selection.
 * @author Werner Mayer
 */
class DlgSettingsViewColor : public PreferencePage
{
  Q_OBJECT

public:
  explicit DlgSettingsViewColor(QWidget* parent = nullptr);
  ~DlgSettingsViewColor() override;

  void saveSettings() override;
  void loadSettings() override;

protected:
  void changeEvent(QEvent *e) override;

protected Q_SLOTS:
  void onSwitchGradientColorsPressed();
  void onRadioButtonSimpleToggled(bool val);
  void onRadioButtonGradientToggled(bool val);
  void onRadioButtonRadialGradientToggled(bool val);
  void onCheckMidColorToggled(bool val);

private:
	void setGradientColorVisibility(bool val);
  void setupOverlay();
  void setupDisplayStyle();
  void retranslateExtra();

private:
  std::unique_ptr<Ui_DlgSettingsViewColor> ui;

  // The groups "Overlay" and "Display Style" (docs/HandsOnQueue.md entry 98,
  // 2026-10-14), made in code below the ones of the .ui file
  QGroupBox *groupOverlay = nullptr;
  QLabel *labelItemBackground = nullptr;
  Gui::PrefColorButton *ItemBackground = nullptr;
  Gui::PrefCheckBox *DockOverlayThemeItemColor = nullptr;
  QGroupBox *groupDisplayStyle = nullptr;
  QLabel *labelHiddenLineFaceColor = nullptr;
  Gui::PrefColorButton *HiddenLineFaceColor = nullptr;
  Gui::PrefCheckBox *HiddenLineOverrideFaceColor = nullptr;
  QLabel *labelHiddenLineColor = nullptr;
  Gui::PrefColorButton *HiddenLineColor = nullptr;
  Gui::PrefCheckBox *HiddenLineOverrideColor = nullptr;
  QLabel *labelHiddenLineBackground = nullptr;
  Gui::PrefColorButton *HiddenLineBackground = nullptr;
  Gui::PrefCheckBox *HiddenLineOverrideBackground = nullptr;
  QLabel *labelShadowLightColor = nullptr;
  Gui::PrefColorButton *ShadowLightColor = nullptr;
  QLabel *labelShadowGroundColor = nullptr;
  Gui::PrefColorButton *ShadowGroundColor = nullptr;
  /// every preference widget of the two groups, saved and restored with the page
  std::vector<Gui::PrefWidget*> extraPrefs;
};

} // namespace Dialog
} // namespace Gui

#endif // GUI_DIALOG_DLGSETTINGSVIEWCOLOR_H
