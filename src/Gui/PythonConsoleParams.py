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
'''Auto code generator for parameters in Preferences/PythonConsole
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString

NameSpace = 'Gui'
ClassName = 'PythonConsoleParams'
ParamPath = 'User parameter:BaseApp/Preferences/PythonConsole'
ClassDoc = 'Convenient class to obtain the settings of the Python console'
Signal = True

Params = [
    ParamBool('PythonWordWrap', True,
        title = 'Word wrap',
        doc = "Wrap lines of the Python console that are longer than the window\n"
              "is wide. Also in the console's context menu. Takes effect at once."),
    ParamBool('PythonBlockCursor', False,
        title = 'Block cursor',
        doc = "Draw the Python console's text cursor as a block one character\n"
              "wide instead of a line. Takes effect at once."),
    ParamBool('SavePythonHistory', False,
        title = 'Save history',
        doc = "Keep the Python console's command history between sessions: the\n"
              "last 100 entries, written when the program ends and read at the\n"
              "next start. Also in the console's context menu."),
    ParamInt('ProfilerInterval', 200,
        title = 'Python profiler interval',
        doc = "Interval in milliseconds at which running Python code lets the\n"
              "user interface process events, so that it stays responsive. 0\n"
              "turns that off. Read when a script or command starts."),
    ParamString('ExternalPythonExecutable', '',
        title = 'Path to external Python executable',
        doc = "Python executable used for work done outside the program, such as\n"
              "installing packages with pip or debugging with debugpy. Empty lets\n"
              "the program look for one when it is needed, and store what it\n"
              "found."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
