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
'''Auto code generator for parameters in Preferences/Group
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamUInt, ParamFloat

NameSpace = 'App'
ClassName = 'GroupParams'
ParamPath = 'User parameter:BaseApp/Preferences/Group'
ClassDoc = 'Convenient class to obtain group object (App::GroupExtension and GeoFeatureGroupExtension) related parameters'

Params = [
    ParamBool('ClaimAllChildren', True,
        title = 'Claim all children',
        doc = "Claim all children objects in tree view. If disabled, then only claim\n"
              "children that are not claimed by other children."),
    ParamBool('KeepHiddenChildren', True,
        title = 'Remember hidden children',
        doc = "Remember invisible children objects and keep those objects hidden\n"
              "when the group is made visible."),
    ParamBool('ExportChildren', True,
        title = 'Export children by visibility',
        doc = "Export visible children (e.g. when doing STEP export). Note, that once this option\n"
              "is enabled, the group object will be touched when its child toggles visibility."),
    ParamBool('CreateOrigin', False,
        title = 'Always create origin features in origin group',
        doc = "Create all origin features when the origin group is created. If Disabled\n"
              "The origin features will only be created when the origin group is expanded\n"
              "for the first time."),
    ParamBool('GeoGroupAllowCrossLink', False,
        title = 'Allow cross coordinate links in GeoFeatureGroup (App::Part)',
        doc = "Allow objects to be contained in more than one GeoFeatureGroup (e.g. App::Part).\n"
              "If disabled, adding an object to one group will auto remove it from other groups.\n"
              "WARNING! Disabling this option may produce an invalid group after changing its children."),
    ParamBool('CreateGroupInGroup', True,
        title = 'Create new group inside current selected group',
        doc = 'This option only applies to creating a new group when there is a single\n'
              'selected object that is also a group (either plain or App::Part).\n\n'
              'If the option is enabled, then the new group will be created inside the\n'
              'selected group. If disabled, then the selected group will be moved into\n'
              'the newly created group instead.'),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
