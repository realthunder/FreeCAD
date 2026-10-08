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
'''Auto code generator for parameters in Preferences/Mod/PartDesign
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamHex, ParamColor

NameSpace = 'PartDesign'
ClassName = 'PartDesignParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/PartDesign'
ClassDoc = 'Convenient class to obtain the settings of PartDesign'

# Every one of these is read when it is used: when a feature, a datum or a
# body's view is made, when a command runs. The class lives in the App
# library because features read RefineModel; the settings of the Gui
# library are here with it.
Params = [
    ParamBool('RefineModel', False,
        title = 'Refine model after sketch-based operation',
        doc = "New PartDesign features get Refine switched on: faces that lie\n"
              "on the same surface are merged after the feature is computed.\n"
              "Read when a feature is created."),
    ParamInt('defaultBaseTypeHole', 1,
        title = 'Default profile type for holes',
        doc = "What a new Hole takes from its sketch: 0 circles and arcs, 1\n"
              "points, circles and arcs, 2 points. Read when a Hole is created."),
    ParamBool('NewSketchUseAttachmentDialog', False,
        title = 'Always ask how to attach a new sketch',
        doc = "Open the attachment panel for every new sketch, also when a\n"
              "single planar face or plane is selected, which is otherwise\n"
              "sketched on at once."),
    ParamBool('BooleanDeleteOnRemove', True,
        title = 'Delete a body removed from a Boolean',
        doc = "Removing a body from a PartDesign Boolean also deletes the body.\n"
              "The check box of the Boolean task panel."),
    ParamBool('SwitchToWB', True,
        title = 'Switch to PartDesign when a body is edited',
        doc = "Activate the PartDesign workbench when a body is double-clicked."),
    ParamBool('SwitchToTask', True,
        title = 'Show the task panel in PartDesign',
        doc = "Bring the task view to the front when the PartDesign workbench is\n"
              "activated."),
    # The colour of new datums is Part's DefaultDatumColor, in Part's group
    # (PartGuiParams.py): the binders take the same one. There was a second
    # one here.
    ParamInt('CoordinateSystemFontSize', 10,
        title = 'Local coordinate system font size',
        doc = "Font size of the axis labels of a new local coordinate system."),
    ParamFloat('CoordinateSystemZoom', 1.0,
        title = 'Local coordinate system zoom',
        doc = "Size factor of a new local coordinate system."),
    ParamBool('CoordinateSystemShowLabel', False,
        title = 'Local coordinate system labels',
        doc = "Show the axis labels of a new local coordinate system."),
    ParamBool('CoordinateSystemSelectOnTop', True,
        title = 'Local coordinate system on top when selected',
        doc = "Draw a new local coordinate system on top of everything else\n"
              "while it is selected."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
