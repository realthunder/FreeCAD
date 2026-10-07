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
'''Auto code generator for parameters in Preferences/Macro
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamString

NameSpace = 'Gui'
ClassName = 'MacroParams'
ParamPath = 'User parameter:BaseApp/Preferences/Macro'
ClassDoc = 'Convenient class to obtain the settings of macro recording and running'
Signal = True

# Not listed: what the program keeps here for itself (ShowWalkthroughMessage,
# the answer to a one-time hint), and two keys the Macro page still stores
# that nothing has ever read (ScriptToFile, ScriptFile).
Params = [
    ParamString('MacroPath', '',
        title = 'Macro path',
        doc = "Directory where the user's macros are stored and looked for.\n"
              "Empty or not set means the Macro directory of the user's\n"
              "application data. Read each time macros are listed, recorded or\n"
              "run."),
    ParamBool('LocalEnvironment', True,
        title = 'Run macros in local environment',
        doc = "Run a macro in an environment of its own, so that the variables\n"
              "it defines do not stay in the Python interpreter afterwards.\n"
              "Applies to the next macro run."),
    ParamBool('RecordGui', True,
        title = 'Record GUI commands',
        doc = "Write commands that only affect the user interface -- selection,\n"
              "view changes -- to a macro being recorded as well."),
    ParamBool('GuiAsComment', True,
        title = 'Record as comment',
        doc = "Write the recorded user interface commands as comment lines, so\n"
              "that the macro shows them and does not run them."),
    ParamBool('ScriptToPyConsole', True,
        title = 'Show script commands in Python console',
        doc = "Show every command the program issues for a menu or tool bar\n"
              "action in the Python console, as it is carried out."),
    ParamBool('ReplaceSpaces', True,
        title = 'Replace spaces in macro names',
        doc = "Replace spaces by underscores in a macro file name entered in the\n"
              "Macro dialog: on create, rename and duplicate."),
    ParamBool('DuplicateFrom001', False,
        title = 'Number duplicates from 001',
        doc = "When a macro whose name ends in @ and three digits is duplicated,\n"
              "look for a free name starting at @001 instead of continuing from\n"
              "that macro's own number."),
    ParamBool('DuplicateIgnoreExtraNote', False,
        title = 'Drop the note when duplicating',
        doc = "When a macro is duplicated, leave out of the suggested name\n"
              "whatever stands between the number and the file extension."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
