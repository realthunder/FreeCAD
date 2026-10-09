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

#ifndef GUI_DISPLAYOPTIONS_H
#define GUI_DISPLAYOPTIONS_H

#include <QColor>
#include <QWidget>
#include <FCGlobal.h>

class QCheckBox;
class QComboBox;
class QMenu;
class QPushButton;
class QSlider;
class QToolButton;

namespace Gui {

class View3DInventorViewer;

/**
 * The top of the Display style menu: the draw style of the active 3D view
 * as one combo box, each style with its icon, and the anti-aliasing of the
 * 3D views as another (docs/HandsOnQueue.md entry 63).
 *
 * The menu is the group command's -- Std_DrawStyle, one sub command a
 * style, which ActionGroup lays out as a row each. Those rows are hidden
 * here and the combo stands in for them: picking an entry runs the style's
 * own command, so the shortcuts, the macro names and what the tool button
 * shows are what they were. The style is the ACTIVE view's, as it was for
 * the rows.
 *
 * The anti-aliasing is the setting, View/AntiAliasing: the open views take
 * a change at once.
 */
class GuiExport DrawStyleOptionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DrawStyleOptionsWidget(QWidget *parent = nullptr);

    /// Re-read the active view and the setting into the two combo boxes
    void refresh();

    /// Put this section at the top of \a menu, in place of the style rows,
    /// unless it is there already; and refresh it.
    static void install(QMenu *menu);

private:
    QComboBox *styleCombo = nullptr;
    QComboBox *aliasingCombo = nullptr;
};

/**
 * The lights of the active 3D view, as a section of the Display style
 * menu: what the Light Sources preference page held, but for the page's
 * own little view with the light to drag (docs/HandsOnQueue.md entry 63).
 *
 * A change is the VIEW's: it is stored in the active view's properties
 * (Light_*, View3DInventorViewer::setLightSetting), which a view keeps
 * with its document, and with "All views" ticked in those of every open
 * 3D view. The setting behind that box, View/SyncLightSettings, is
 * remembered. Nothing here writes the preferences but "Save as default",
 * which writes what lights the active view into them: what a view with no
 * lights of its own is lit by.
 *
 * The direction of the headlight is not a number to type. "Direction"
 * closes the menu and lets the pointer turn it in the active view
 * (View3DInventorViewer::setLightManipulator); pressing it again, or
 * Escape in the view, ends that.
 */
class GuiExport LightOptionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LightOptionsWidget(QWidget *parent = nullptr);

    /// Re-read the active view into the controls
    void refresh();

    /// Append this section to \a menu unless it is there already; and
    /// refresh it.
    static void install(QMenu *menu);

private:
    /// One light's row: a switch (none for the ambient light), a colour
    /// and an intensity, and the rig keys they stand for
    struct Row {
        QCheckBox *enable = nullptr;
        QToolButton *colour = nullptr;
        QSlider *intensity = nullptr;
        const char *enableKey = nullptr;
        const char *colourKey = nullptr;
        const char *intensityKey = nullptr;
        QColor shown;
    };
    enum { Headlight, FillLight, Ambient, RowCount };

    View3DInventorViewer *activeViewer() const;
    void addRow(int which, int gridRow, const QString &name, const char *enableKey,
                const char *colourKey, const char *intensityKey);
    void showColour(Row &row, const QColor &colour);
    /// Ask for a colour of this row's light: the menu is closed first,
    /// as a dialog under a menu's grab gets no input
    void chooseColour(int which);

    Row rows[RowCount];
    QPushButton *direction = nullptr;
    QCheckBox *allViews = nullptr;
    QPushButton *save = nullptr;
    bool refreshing = false;
};

} // namespace Gui

#endif // GUI_DISPLAYOPTIONS_H
