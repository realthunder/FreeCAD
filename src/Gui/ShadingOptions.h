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

#ifndef GUI_SHADINGOPTIONS_H
#define GUI_SHADINGOPTIONS_H

#include <QWidget>
#include <FCGlobal.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QRadioButton;
class QSlider;

namespace App { class PropertyContainer; }

namespace Gui {

class View3DInventorViewer;

/**
 * The render engine's shading options, shown as a section of the Display
 * style tool button's drop-down.
 *
 * The draw styles above it are an exclusive list -- one override mode on
 * the viewer -- which is the wrong shape for these: only the shading
 * model (classic / physically based / matcap / external) is a choice, while cavity,
 * occlusion, shadows and bloom compose freely with it and with each
 * other. So they live here instead, as a popover beside the list, the
 * split Blender's viewport shading and SolidWorks' display style +
 * RealView both arrive at.
 *
 * Every control writes a Render_* property of the active 3D view, which
 * the per-frame config feed re-reads (SoFCRendererBridge::translate*).
 * Those properties exist only once a renderer backend has been selected,
 * so the whole section disables itself and says why when it is not.
 *
 * Each control writes TWICE: that property, and the preference the
 * property was seeded from. Showing the menu reads the view back, so
 * what is ticked is always what the window in front of you is drawn
 * with; changing it here says "this is how I want it drawn", which
 * covers the next view too --
 * View3DInventorViewer::materializeRenderProps starts every Render_*
 * from its RenderParams twin. Without the second write, a look chosen
 * here had to be chosen again in every new window, and made to stick in
 * a preference page nobody would think to visit for it.
 *
 * Editing the same property in the property editor deliberately does
 * NOT touch the preference. That is an edit to one view, which is what
 * a property editor is for.
 */
class GuiExport ShadingOptionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ShadingOptionsWidget(QWidget *parent = nullptr);

    /// Re-read the active view into the controls. Call before showing:
    /// the active view, and its properties, change underneath us.
    void refresh();

    /// Append this section to \a menu (separator + widget action) unless
    /// it is already there, and refresh it. Menus built by ActionGroup
    /// are rebuilt with the toolbar, hence the lookup.
    static void install(QMenu *menu);

private:
    App::PropertyContainer *activeView() const;
    /// Select a shading model (a View3DInventor::ShadingModel value):
    /// writes the view's declared ShadingType -- the facade pair and
    /// the external session follow it -- and, for the raster models,
    /// the preference pair behind it. External deliberately leaves the
    /// preferences alone: it cannot be a default, so they keep the
    /// raster model to fall back to.
    void setModel(long model);
    /// The Cycles session options, in a dialog beside the radio that
    /// needs them: renderer, device, samples, time limit, denoise,
    /// pixel size. Every control applies immediately and writes twice
    /// (view property + preference), like the rest of the section.
    void openExternalSettings();
    /// Grey the Settings... button unless the External model is on and
    /// available.
    void updateExternalSettingsEnabled();
    /// Grey the radius row unless the cavity pass is on and available.
    void updateCavityRadiusEnabled();
    /// Grey the tint row unless the matcap model is on and available.
    void updateMatcapTintEnabled();
    /// Grey the copy-the-image box unless there is an image to copy.
    void updateEnvEmbedEnabled();
    /// Set a Render_<name> bool on the active view and, when \a pref is
    /// given, the preference behind it -- see the class comment.
    void setFlag(const char *name, bool value,
                 void (*pref)(const bool &) = nullptr);
    /// What the "Image..." entry says on hover: how to choose a file
    /// that works, with \a current named above it when there is one.
    QString envImageToolTip(const QString &current = QString()) const;
    /// Ask for an environment image and hand it to Render_PBREnvImage.
    /// Cancelling leaves the preset that is in effect selected.
    void chooseEnvImage();
    /// Drop both the path and any embedded copy, so a preset chosen in
    /// the same combo is what actually lights the scene.
    void clearEnvImage();
    /// The environment image in effect on the active view: the embedded
    /// copy's original name, else the path. Empty when there is none.
    QString envImageName() const;
    /// Enter or leave the Shadow draw style, carrying the current style
    /// in and handing it back on the way out. The draw style is what puts
    /// a scene light in the graph at all, so it -- not Render_Shadow
    /// alone -- is what a shadow toggle has to drive.
    void setShadow(bool on);

private:
    QRadioButton *classicRadio;
    QRadioButton *pbrRadio;
    QRadioButton *matcapRadio;
    QRadioButton *externalRadio;
    QPushButton *externalSettings;
    QLabel *envLabel;
    QComboBox *envCombo;
    QCheckBox *envBgCheck;
    QCheckBox *envEmbedCheck;
    /// Index of the combo's trailing "Image..." entry -- the presets are
    /// the enumeration's own values and sit at 0..n-1 before it.
    int envImageIndex = -1;
    QLabel *matcapLabel;
    QComboBox *matcapCombo;
    QLabel *matcapTintLabel;
    QSlider *matcapTintSlider;
    QLabel *matcapTintValue;
    QCheckBox *cavityCheck;
    QLabel *cavityRadiusLabel;
    QSlider *cavityRadiusSlider;
    QLabel *cavityRadiusValue;
    QCheckBox *aoCheck;
    QCheckBox *shadowCheck;
    QCheckBox *bloomCheck;
    QLabel *hint;
    /// Set while refresh() writes the controls, so their change signals
    /// do not write straight back into the properties.
    bool loading = false;
};

} // namespace Gui

#endif // GUI_SHADINGOPTIONS_H
