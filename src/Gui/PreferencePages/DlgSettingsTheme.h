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

#ifndef GUI_DIALOG_DLGSETTINGSTHEME_H
#define GUI_DIALOG_DLGSETTINGSTHEME_H

#include <Gui/PropertyPage.h>
#include <memory>
#include <string>
#include <vector>

#include <Base/Parameter.h>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace Gui {

class PrefComboBox;

namespace Dialog {
class Ui_DlgSettingsTheme;

/**
 * The preference page that owns the application's appearance.
 *
 * A theme is a preference pack, and every option below the theme combo is one
 * of the parameters that pack writes. Editing one leaves the theme "modified",
 * which the user resolves either by reverting or by saving the result as a
 * theme of their own.
 */
class DlgSettingsTheme : public PreferencePage
{
  Q_OBJECT

public:
  explicit DlgSettingsTheme(QWidget* parent = nullptr);
  ~DlgSettingsTheme() override;

  void saveSettings() override;
  void loadSettings() override;

  static void attachObserver();

  /**
   * Fill a combo with the files reachable under a Qt search path prefix, and
   * select the one the named parameter holds.
   * \param key the parameter under BaseApp/Preferences/MainWindow
   * \param path the Qt search path prefix, e.g. "qss"
   * \param def the label for "none", which stores an empty value
   */
  static void populateStylesheets(const char *key,
                                  const char *path,
                                  PrefComboBox *combo,
                                  const char *def,
                                  QStringList filter = QStringList());

protected:
  void changeEvent(QEvent *e) override;

private Q_SLOTS:
  void onThemeActivated(int index);
  void onRevertClicked();
  void onSaveAsThemeClicked();
  void onThemeFolderClicked();

public:
  /// Re-read every widget from the parameters, without rescanning the packs.
  void refreshFromParameters();

private:
  void loadThemes();
  void loadCustomization();
  void loadVariables();
  void saveVariables();
  void refreshModifiedState();
  void applyTheme(const QString& name);

  /// The directory a named theme is stored in, empty if it has none on disk.
  static QString themeFolder(const std::string& name);
  /// Tell the author what they just saved, and how to hand it to anyone else.
  void showDistributionHelp(const QString& name, const QString& folder);

  /// One editor built for one entry of the current theme's Variables group.
  struct VariableRow
  {
    std::string name;
    ParameterGrp::ParamType type;
    QWidget* editor;
  };

  std::unique_ptr<Ui_DlgSettingsTheme> ui;
  std::vector<VariableRow> variableRows;
};

} // namespace Dialog
} // namespace Gui

#endif // GUI_DIALOG_DLGSETTINGSTHEME_H
