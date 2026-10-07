# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************
'''Auto code generator for parameters in Preferences/MainWindow
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString

NameSpace = 'Gui'
ClassName = 'MainWindowParams'
ParamPath = 'User parameter:BaseApp/Preferences/MainWindow'
ClassDoc = 'Convenient class to obtain the main window and theme settings'
Signal = True

# The SETTINGS of the group. Its state is not listed: the window's geometry
# and layout (Geometry, MainWindowState, Maximized, StatusBar,
# WindowStateRestored) and the theme book-keeping (Theme, ThemeAutoApplied,
# ThemeIconSet).
#
# Most of these are applied by handlers that the parameter manager tells of a
# change (DlgSettingsTheme::attachObserver, MainWindow, ToolBarManager,
# OverlayStyleSheet). Those read the group themselves, with the defaults
# defined here: the manager's signal arrives before this class has brought
# its own values up to date.
Params = [
    ParamString('ColorScheme', 'Light',
        title = 'Colour scheme',
        doc = "Palette the interface is drawn with: Light, Dark, or empty to\n"
              "follow the desktop. Not set at all means Light. Ignored while the\n"
              "theme follows the desktop. Takes effect at once."),
    ParamString('StyleSheet', '',
        title = 'Style sheet',
        doc = "Style sheet of the interface: a file of the 'qss' search path, or\n"
              "a full path. Empty means none. Applied shortly after a change."),
    ParamString('OverlayActiveStyleSheet', '',
        title = 'Overlay style sheet',
        doc = "Style sheet of the overlay dock panels: a file of the 'overlay'\n"
              "search path, or a path. Empty picks the light or the dark outline\n"
              "sheet to match the interface. Takes effect at once."),
    ParamString('MenuStyleSheet', '',
        title = 'View menu style sheet',
        doc = "Style sheet of the menus that pop up over the 3D view: a file of\n"
              "the 'qssm' search path, or a path. Empty gives ordinary menus.\n"
              "Read each time such a menu is set up."),
    ParamString('IconSet', '',
        title = 'Icon set',
        doc = "Icon set that replaces built-in icons: a file of the 'iconset'\n"
              "search path or a path, or several separated by ';'. Empty means\n"
              "none. Applied with the style sheet shortly after a change."),
    ParamString('ThemeIconSetPolicy', 'Reset',
        title = 'Theme icon set policy',
        doc = "What applying a theme does to the icon set: Reset takes the\n"
              "theme's (none if it names none), Merge puts the theme's over the\n"
              "user's, Keep leaves it alone."),
    ParamBool('TiledBackground', False,
        title = 'Tiled background',
        doc = "Fill the empty area of the main window with a tiled image instead\n"
              "of plain grey. Drawn only while no style sheet is chosen."),
    ParamString('QtStyle', '',
        title = 'Widget style',
        doc = "Qt widget style the interface is drawn with: FreeCAD for the\n"
              "built-in one, System for the platform's, or any style name Qt\n"
              "knows. Empty leaves the style as it is. Themes set it."),
    ParamBool('ThemeAuto', False,
        title = 'Theme follows the desktop',
        doc = "Follow the desktop's light or dark mode: the Light or the Dark\n"
              "theme is applied at start and when the desktop changes. Set by\n"
              "Match Desktop on the Start page; cleared when a theme is applied."),
    ParamString('ThemeStyleParametersFile', '',
        title = 'Style parameters file',
        doc = "Path of a YAML file of style parameters used instead of the\n"
              "current theme's own. Empty uses the theme's. Read at start and\n"
              "each time the style sheet is applied."),
    ParamBool('CustomTitleBar', False,
        title = 'Custom title bar',
        doc = "Draw the title bar inside the application instead of the\n"
              "platform's, so that the menu and tool bars can share its row.\n"
              "Themes set it. Takes effect at once."),
    ParamBool('TitleBarToolBars', True,
        title = 'Tool bars in the title bar',
        doc = "With the custom title bar, put the workbench tool bar into the\n"
              "title bar instead of under the menu bar. No effect with the\n"
              "platform's title bar. Takes effect at once."),
    ParamBool('FoldTitleBarMenu', True,
        title = 'Fold the title bar menu',
        doc = "With the custom title bar, hide the menu bar behind the logo\n"
              "button and open it on hover, leaving the row to tool bars. No\n"
              "effect with the platform's title bar or on macOS."),
    ParamInt('TitleBarMenuClickGuard', 1000,
        title = 'Title bar menu click guard',
        doc = "How long, in milliseconds, a click on the title bar's logo is\n"
              "ignored after hovering has opened the folded menu, so that a\n"
              "habitual click does not close it again. 0 turns the guard off."),
    ParamString('DefaultToolBarArea', 'Top',
        title = 'Workbench tool bar area',
        doc = "Side of the main window where workbench tool bars are docked by\n"
              "default: Top, Left, Right or Bottom. Anything else means Top. The\n"
              "tool bars are moved shortly after a change."),
    ParamString('GlobalToolBarArea', 'Top',
        title = 'Global tool bar area',
        doc = "Side of the main window where the global tool bars (File,\n"
              "Structure, Macro, View and those made global) are docked by\n"
              "default: Top, Left, Right or Bottom. Anything else means Top."),
    ParamString('WSPosition', 'WSToolbar',
        title = 'Workbench selector position',
        doc = "Where the workbench selector goes. WSToolbar puts it into a tool\n"
              "bar of its own. Read each time a workbench sets its tool bars up."),
    ParamString('DockableWindowShortcut', 'D, D',
        title = 'Dock window shortcut prefix',
        doc = "Key sequence the entries of the dockable window menu get as a\n"
              "shortcut, each followed by its number. Empty turns these shortcuts\n"
              "off. Read each time the menu opens."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
