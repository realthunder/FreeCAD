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
'''Auto code generator for parameters in Preferences/Themes
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamHex, ParamColor

NameSpace = 'Gui'
ClassName = 'ThemeParams'
ParamPath = 'User parameter:BaseApp/Preferences/Themes'
ClassDoc = 'Convenient class to obtain the accent colours of the theme'
Signal = True

# The three accent colours every style sheet resolves as @ThemeAccentColor1
# to 3. Their defaults are Gui::Application::DefaultAccentColor1 to 3; every
# place that needs one for a key that is not stored -- the style sheet, the
# preference page, the style parameter source, a theme being saved -- takes
# it from here, where there used to be four different answers for the second
# and the third. The sub-groups Variables and UserParameters hold what a
# theme names itself and are not settings of a fixed name.
Params = [
    ParamHex('ThemeAccentColor1', 0x557BB6FF, proxy=ParamColor(transparency=False),
        title = 'Accent colour 1',
        doc = "Highlight colour of the style sheets: hovered, selected and\n"
              "checked items. Applied shortly after a change."),
    ParamHex('ThemeAccentColor2', 0x405C89FF, proxy=ParamColor(transparency=False),
        title = 'Accent colour 2',
        doc = "Colour the style sheets use for the engaged state: focus, a\n"
              "pressed button, an open combo box. The Dark theme sets a lighter\n"
              "one. Applied shortly after a change."),
    ParamHex('ThemeAccentColor3', 0x4B6CA0FF, proxy=ParamColor(transparency=False),
        title = 'Accent colour 3',
        doc = "Far end of the gradients the style sheets draw from accent colour\n"
              "1. Applied shortly after a change."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
