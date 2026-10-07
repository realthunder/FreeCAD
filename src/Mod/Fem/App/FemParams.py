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
'''Auto code generator for parameters in Preferences/Mod/Fem
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString

NameSpace = 'Fem'
ClassName = 'FemParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Fem'
ClassDoc = 'Convenient class to obtain the settings of the Fem workbench that C++ reads'

# Only what C++ reads. Most of Fem's settings are read by its Python code
# and its Python pages, and are not listed here. Gmsh/NumOfThreads is not
# listed either: its default is the number of processor threads.
Params = [
    ParamBool('PostAutoRecompute', True,
        title = "Apply changes automatically",
        doc = "Recomputes a post-processing object as soon as one of its settings\n"
              "is changed in a task panel. Takes effect at once."),
    ParamInt('DefaultSolver', 0, subpath='General',
        title = "Default solver",
        doc = "Solver added to a new analysis container: 0 none, 1 CalculiX, 2\n"
              "Elmer, 3 Mystran, 4 Z88. Takes effect when the next analysis is\n"
              "created."),
    ParamString('MeshExportLevel', 'Highest', subpath='InOutVtk',
        title = "VTK mesh export level",
        doc = "Which mesh elements are written to a VTK file: All, or Highest for\n"
              "only those of the highest dimension. Takes effect at the next\n"
              "export."),
    ParamInt('AbaqusElementChoice', 2, subpath='Abaqus',
        title = "INP elements to export",
        doc = "Which mesh elements are written to an Abaqus INP file: 0 all, 1\n"
              "only the highest, 2 only the FEM elements. Takes effect at the\n"
              "next export."),
    ParamBool('AbaqusWriteGroups', True, subpath='Abaqus',
        title = "Export INP group data",
        doc = "Writes the mesh groups as well when a mesh is exported to an\n"
              "Abaqus INP file. Takes effect at the next export."),
    ParamString('GmshLogVerbosity', '3', subpath='Gmsh', param_name='LogVerbosity',
        title = "Gmsh log verbosity",
        doc = "How much Gmsh reports in the task panel while it meshes, as its\n"
              "verbosity level: from 0, silent, to 99, debug. Takes effect at the\n"
              "next meshing run."),
    ParamString('Z88Solver', 'sorcg', subpath='Z88', param_name='Solver',
        title = "Z88 solver method",
        doc = "Solver method given to a new Z88 solver object. Applies to solver\n"
              "objects created afterwards."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
