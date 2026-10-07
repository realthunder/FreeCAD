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
'''Auto code generator for parameters in Preferences/Units
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt

NameSpace = 'App'
ClassName = 'UnitsParams'
ParamPath = 'User parameter:BaseApp/Preferences/Units'
ClassDoc = 'Convenient class to obtain the settings of units and number display'
Signal = True

# The number of decimals and the inch fraction are the same for every
# document, and App puts a change in force when it is made
# (Application::initApplication). The unit system is the active document's
# to say unless IgnoreProjectSchema is set; Gui::Application follows a
# change of either.
Params = [
    ParamInt('UserSchema', 0,
        title = 'Unit system',
        doc = "Unit system quantities are shown in, and the one a new document\n"
              "starts with: 0 mm/kg/s, 1 m/kg/s, 2 US customary, 3 imperial\n"
              "decimal, 4 building Euro, 5 building US, 6 metric CNC (mm,\n"
              "mm/min), 7 imperial civil, 8 FEM (mm, N, s), 9 meter decimal. A\n"
              "saved document shows its own unless told otherwise."),
    ParamInt('Decimals', 2,
        title = 'Number of decimals',
        doc = "Number of decimals numbers and dimensions are shown with. Takes\n"
              "effect at once; a field already on screen follows when it is next\n"
              "redrawn."),
    ParamInt('FracInch', 8,
        title = 'Minimum fractional inch',
        doc = "Smallest fraction of an inch the building US unit system shows,\n"
              "as its denominator: 2, 4, 8, 16, 32, 64 or 128."),
    ParamBool('IgnoreProjectSchema', False,
        title = 'Ignore project unit system',
        doc = "Show every document in the unit system of the UserSchema setting\n"
              "and ignore the one stored in the document."),
    ParamInt('DecimalsPreSel', -1,
        title = 'Decimals of preselected coordinates',
        doc = "Number of decimals of the coordinates shown in the status bar for\n"
              "the point under the mouse. -1 uses the general number of\n"
              "decimals."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
