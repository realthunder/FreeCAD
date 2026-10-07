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
'''Auto code generator for the settings of several small parameter groups
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamUInt, ParamString

NameSpace = 'Gui'
ClassName = 'MiscParams'
ParamPath = 'User parameter:BaseApp/Preferences'
ClassDoc = 'Convenient class to obtain the settings of several small parameter groups'

# Groups that hold two or three settings each and would not fill a class of
# their own. Each setting names its group; some share it with what the
# program keeps there for itself (the recent macros themselves, the
# shortcuts, the list of workbenches), which is not listed.
Params = [
    # --- Preferences/RecentMacros
    ParamInt('RecentMacros', 12, subpath='RecentMacros',
        title = 'Size of recent macro list',
        doc = "Number of macros the recent macros menu lists."),
    ParamInt('ShortcutCount', 3, subpath='RecentMacros',
        title = 'Recent macros with a shortcut',
        doc = "Number of entries of the recent macros menu that get a keyboard\n"
              "shortcut, the modifiers below and a digit. At most 9."),
    ParamString('ShortcutModifiers', 'Ctrl+Shift+', subpath='RecentMacros',
        title = 'Recent macro shortcut modifiers',
        doc = "Modifier keys of the shortcuts of the recent macros menu, written\n"
              "as in a shortcut and ending in +, such as Ctrl+Shift+."),
    # --- Preferences/Gui/Gizmos
    ParamInt('CoarseLinearSnapMultiplier', 5, subpath='Gui/Gizmos',
        title = 'Coarse linear step of a gizmo',
        doc = "How many times larger the step of a linear gizmo is while the\n"
              "key for coarse steps is held."),
    ParamInt('CoarseRotationSnapMultiplier', 5, subpath='Gui/Gizmos',
        title = 'Coarse rotation step of a gizmo',
        doc = "How many times larger the step of a rotation gizmo is while the\n"
              "key for coarse steps is held."),
    # --- Preferences/CacheDirectory
    ParamUInt('CacheLimit', 500, subpath='CacheDirectory', param_name='Limit',
        title = 'Cache size limit',
        doc = "Size in megabytes the cache directory may grow to before the\n"
              "program offers to clean it."),
    ParamInt('CachePeriod', 2, subpath='CacheDirectory', param_name='Period',
        title = 'Cache check period',
        doc = "How often the size of the cache directory is checked: 0 always,\n"
              "1 daily, 2 weekly, 3 monthly, 4 yearly, 5 never."),
    # --- Preferences/Shortcut/Settings
    ParamInt('ShortcutTimeout', 300, subpath='Shortcut/Settings',
        title = 'Shortcut sequence timeout',
        doc = "Milliseconds the program waits for the next key of a shortcut\n"
              "made of several keys before it acts on what was typed."),
    # --- Preferences/Workbenches
    ParamBool('ShowTabBar', False, subpath='Workbenches',
        title = 'Workbench tab bar',
        doc = "Show the workbenches as a bar of tabs instead of a drop-down\n"
              "list."),
    ParamBool('TabBarShowText', False, subpath='Workbenches',
        title = 'Workbench tab bar text',
        doc = "Show the name of each workbench on its tab, beside its icon."),
    ParamInt('TabBarMaxLength', 0, subpath='Workbenches',
        title = 'Workbench tab bar length',
        doc = "Room in pixels the workbench tab bar may take along the way its\n"
              "tabs run. 0 takes what its tabs need."),
    # --- Preferences/HighDPI and Preferences/OpenGL, both read at startup
    ParamBool('DisableDpiScaling', False, subpath='HighDPI',
        title = 'Disable high DPI scaling',
        doc = "Switch Qt's scaling for high resolution screens off. Read at\n"
              "startup."),
    ParamBool('UseSoftwareOpenGL', False, subpath='OpenGL',
        title = 'Use software OpenGL',
        doc = "Draw with a software implementation of OpenGL instead of the\n"
              "graphics driver. Read at startup."),
    # --- Preferences/DependencyGraph
    ParamBool('Unflatten', True, subpath='DependencyGraph',
        title = 'Unflatten the dependency graph',
        doc = "Run the dependency graph through Graphviz's unflatten, which\n"
              "makes wide graphs narrower."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
