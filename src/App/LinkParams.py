# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
'''Auto code generator for parameters in Preferences/Link
'''
import sys
from os import sys, path

# Actual code generation is done in Tools/param_utils.py.

# The following code is to import Tools/param_util.py without needing __init__.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamUInt, ParamFloat

NameSpace = 'App'
ClassName = 'LinkParams'
ParamPath = 'User parameter:BaseApp/Preferences/Link'
ClassDoc = 'Convenient class to obtain App::Link related parameters'

Params = [
    ParamBool('HideScaleVector', True,
        doc = "Hide the per-axis ScaleVector property of links in the property\n"
              "view, leaving the single Scale value. Applies to links created\n"
              "afterwards."),
    ParamBool('CreateInPlace', True,
        doc = "Place a new link where the selected object appears in the 3D view,\n"
              "counting the placements of its parents. When off the link is left\n"
              "with no placement of its own."),
    ParamBool('CreateInContainer', True,
        doc = "Create new links inside the active container, such as the active\n"
              "part, instead of at the top level of the document."),
    ParamString('ActiveContainerKey', "",
        doc = "Name of the active object slot searched for the container that new\n"
              "links are created in. Empty uses the active part."),
    ParamBool('CopyOnChangeApplyToAll', True, doc='''\
Stores the last user choice of whether to apply CopyOnChange setup to all link
that links to the same configurable object'''),
    ParamBool('ShowElement', True,
        title = 'Show array element in Link array',
        doc = 'Default value of the "ShowElement" property in an App::Link object,\n'
              'which specifies whether to show the link array element as individual\n'
              'object in the tree view.'),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
