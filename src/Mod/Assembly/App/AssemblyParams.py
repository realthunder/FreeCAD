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
'''Auto code generator for parameters in Preferences/Mod/Assembly
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString, ParamHex, ParamColor

NameSpace = 'Assembly'
ClassName = 'AssemblyParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Assembly'
ClassDoc = 'Convenient class to obtain the settings of the Assembly workbench'

# Assembly's preference page is written in Python and reads and stores its
# two switches itself, with the defaults given here. The other four have no
# page.
Params = [
    ParamBool('SolveOnRecompute', True,
        title = "Solve on recompute",
        doc = "Solves the joints of an assembly every time the assembly is\n"
              "recomputed. Takes effect at the next recompute."),
    ParamBool('SolveOnMove', True,
        title = "Solve while dragging",
        doc = "Solves the joints continuously while a part is dragged, so that\n"
              "connected parts follow. When off, only the joint markers are\n"
              "redrawn during the drag. Takes effect at the next drag."),
    ParamBool('LeaveEditWithEscape', True,
        title = "Esc leaves edit mode",
        doc = "Lets the Esc key leave the edit mode of an assembly when no task\n"
              "dialogue is open. Takes effect at once."),
    ParamBool('SwitchToWB', True,
        title = "Switch to Assembly workbench",
        doc = "Switches to the Assembly workbench when an assembly is double-\n"
              "clicked for editing. Takes effect at the next double-click."),
    ParamHex('JointHighlightColor', 0, proxy=ParamColor(transparency=False),
        title = "Joint highlight colour",
        doc = "Colour the elements a joint connects are shown in while the\n"
              "joint is selected or edited. 0, the value while it is not set,\n"
              "is as upstream: the preselection colour of the 3D view once that\n"
              "is stored, a red until then. Takes effect at the next highlight."),
    ParamBool('LogSolverDebug', False,
        title = "Log dragging steps",
        doc = "Writes the dragging steps of the solver to the files\n"
              "runPreDrag.asmt and dragging.log, which helps when reporting a\n"
              "solver problem. Takes effect the next time the assembly is solved."),
    ParamString('BomMirroredSuffix', ' (mirrored)',
        title = "Mirrored part suffix",
        doc = "Text appended to the name of a mirrored part in a bill of\n"
              "materials. Takes effect the next time the bill of materials is\n"
              "generated."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
