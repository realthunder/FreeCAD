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
 * model (default / physically based / matcap) is a choice, while cavity,
 * occlusion, shadows and bloom compose freely with it and with each
 * other. So they live here instead, as a popover beside the list, the
 * split Blender's viewport shading and SolidWorks' display style +
 * RealView both arrive at.
 *
 * Every control writes a Render_* property of the active 3D view, which
 * the per-frame config feed re-reads (SoFCRendererBridge::translate*).
 * Those properties exist only once a renderer backend has been selected,
 * so the whole section disables itself and says why when it is not.
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
    View3DInventorViewer *activeViewer() const;
    void setModel(bool pbr, bool matcap);
    /// Grey the radius row unless the cavity pass is on and available.
    void updateCavityRadiusEnabled();
    void setFlag(const char *name, bool value);
    /// Enter or leave the Shadow draw style, carrying the current style
    /// in and handing it back on the way out. The draw style is what puts
    /// a scene light in the graph at all, so it -- not Render_Shadow
    /// alone -- is what a shadow toggle has to drive.
    void setShadow(bool on);

private:
    QRadioButton *defaultRadio;
    QRadioButton *pbrRadio;
    QRadioButton *matcapRadio;
    QLabel *matcapLabel;
    QComboBox *matcapCombo;
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
