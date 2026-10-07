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
'''Auto code generator for parameters in Preferences/Dialog
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool

NameSpace = 'Gui'
ClassName = 'DialogParams'
ParamPath = 'User parameter:BaseApp/Preferences/Dialog'
ClassDoc = 'Convenient class to obtain the settings of the file and colour dialogs'
Signal = True

# The sub-groups of Preferences/Dialog (TaskAttacher, TaskCSysDragger) hold
# what a task panel remembers of its last use, and are not settings.
#
# What an unset DontUseNativeDialog means is decided by the build
# (FREECAD_USE_QT_FILEDIALOG, on where the system's file dialog is not to
# be had): DialogParams.cpp makes FC_QT_FILEDIALOG_DEFAULT of it.
Params = [
    ParamBool('DontUseNativeDialog', 'FC_QT_FILEDIALOG_DEFAULT',
        title = "Use Qt's file dialog",
        doc = "Open and save files with Qt's own file dialog instead of the\n"
              "operating system's. Holding Shift while the dialog is called for\n"
              "gives the other one. Read each time a file dialog opens."),
    ParamBool('DontUseNativeColorDialog', True,
        title = "Use Qt's colour dialog",
        doc = "Pick colours with Qt's own colour dialog instead of the operating\n"
              "system's. Holding Shift gives the other one. Read each time a\n"
              "colour dialog opens."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
