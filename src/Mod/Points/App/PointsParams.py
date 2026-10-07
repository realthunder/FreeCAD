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
'''Auto code generator for parameters in Preferences/Mod/Points
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString

NameSpace = 'Points'
ClassName = 'PointsParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Points'
ClassDoc = 'Convenient class to obtain the settings of the Points module'

# How an E57 point cloud is read. No page shows these.
Params = [
    ParamBool('UseColor', True, subpath='E57',
        title = "Read E57 colours",
        doc = "Reads the colour of each point when an E57 file that has colours\n"
              "is imported. Takes effect at the next import."),
    ParamBool('CheckInvalidState', True, subpath='E57',
        title = "Skip invalid E57 points",
        doc = "Takes the invalid-point flag of an E57 file into account when the\n"
              "file is imported. Takes effect at the next import."),
    ParamFloat('MinDistance', -1.0, subpath='E57',
        title = "Minimum E57 point distance",
        doc = "Leaves out a point of an E57 file that lies closer than this\n"
              "distance to the point read before it; a negative value keeps every\n"
              "point. Takes effect at the next import."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
