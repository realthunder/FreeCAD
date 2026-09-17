/***************************************************************************
 *   Copyright (c) 2014 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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

#ifndef SKETCHERGUI_SKETCHERSETTINGS_H
#define SKETCHERGUI_SKETCHERSETTINGS_H

#include <Gui/PropertyPage.h>
#include <memory>


namespace SketcherGui
{
class Ui_SketcherSettings;
class Ui_SketcherSettingsGrid;
class Ui_SketcherSettingsDisplay;
class Ui_SketcherSettingsColors;
class SketcherGeneralWidget;
/**
 * The SketcherSettings class implements a preference page to change sketcher settings.
 * @author Werner Mayer
 */
class SketcherSettings: public Gui::Dialog::PreferencePage
{
    Q_OBJECT

public:
    explicit SketcherSettings(QWidget* parent = nullptr);
    ~SketcherSettings() override;

    void saveSettings() override;
    void loadSettings() override;

protected:
    void changeEvent(QEvent* e) override;
    void dimensioningModeChanged(int index);

private:
    std::unique_ptr<Ui_SketcherSettings> ui;
};

/**
 * The SketcherSettings class implements a preference page to change sketcher grid settings.
 */
class SketcherSettingsGrid: public Gui::Dialog::PreferencePage
{
    Q_OBJECT

public:
    explicit SketcherSettingsGrid(QWidget* parent = nullptr);
    ~SketcherSettingsGrid() override;

    void saveSettings() override;
    void loadSettings() override;

protected:
    void changeEvent(QEvent* e) override;

private:
    std::unique_ptr<Ui_SketcherSettingsGrid> ui;
};

/**
 * The SketcherSettings class implements a preference page to change sketcher display settings.
 * @author Werner Mayer
 */
class SketcherSettingsDisplay: public Gui::Dialog::PreferencePage
{
    Q_OBJECT

public:
    explicit SketcherSettingsDisplay(QWidget* parent = nullptr);
    ~SketcherSettingsDisplay() override;

    void saveSettings() override;
    void loadSettings() override;

protected:
    void changeEvent(QEvent* e) override;

private Q_SLOTS:
    void onBtnTVApplyClicked(bool);

private:
    std::unique_ptr<Ui_SketcherSettingsDisplay> ui;
};

/**
 * The SketcherSettings class implements a preference page to change sketcher settings.
 * @author Werner Mayer
 */
class SketcherSettingsColors: public Gui::Dialog::PreferencePage
{
    Q_OBJECT

public:
    explicit SketcherSettingsColors(QWidget* parent = nullptr);
    ~SketcherSettingsColors() override;

    void saveSettings() override;
    void loadSettings() override;

protected:
    void changeEvent(QEvent* e) override;

private:
    std::unique_ptr<Ui_SketcherSettingsColors> ui;
};

// Mode of the sketch autoscale feature, which scales the geometry and the
// camera when the first scale defining constraint is set
enum class AutoScaleMode : int
{
    Always = 0,
    Never = 1,

    // Looks for scale reference objects in the viewport (such as a 3d body)
    // and disables the feature if it finds one
    WhenNoScaleFeatureIsVisible = 2
};

}  // namespace SketcherGui

#endif  // SKETCHERGUI_SKETCHERSETTINGS_H
