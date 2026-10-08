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
'''Auto code generator for parameters in Preferences/Editor
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamHex, ParamColor

NameSpace = 'Gui'
ClassName = 'EditorParams'
ParamPath = 'User parameter:BaseApp/Preferences/Editor'
ClassDoc = 'Convenient class to obtain the settings of the text editors'
Signal = True

# The macro and Python editors, the Python console and the report view are
# told of a change by the parameter group itself, and keep reading it: this
# class may not have the new value yet when they are told. They take their
# DEFAULTS from here, where there used to be one in each of them, and not
# always the same.
#
# A colour is stored as 0xRRGGBB00. The keys of some of them contain a
# space, which is why their names here differ from their keys. Three keys
# the Editor page used to list are not settings: Bookmark, Breakpoint and
# Character were read and then dropped by the syntax highlighter. Two more
# were stored by the page and never read: Tabs and EnableFolding.
def Color(name, default, title, doc, key=''):
    return ParamHex(name, default, param_name=key, proxy=ParamColor(transparency=False),
                    title=title, doc=doc)

Params = [
    ParamString('Font', 'Courier',
        title = 'Font family',
        doc = "Font family of the macro and Python editors, the Python console\n"
              "and the report view: Courier unless set. An empty value means the\n"
              "system's fixed-pitch font. Applied at once."),
    ParamInt('FontSize', 10,
        title = 'Font size',
        doc = "Font size in points of the macro and Python editors, the Python\n"
              "console and the report view. Applied at once."),
    ParamInt('TabSize', 4,
        title = 'Tab size',
        doc = "Width of a tab stop in the macro and Python editors, in\n"
              "characters of the editor font. Applied at once."),
    ParamInt('IndentSize', 4,
        title = 'Indent size',
        doc = "Number of spaces one step of indentation is in the macro and\n"
              "Python editors, when indentation is done with spaces."),
    ParamBool('Spaces', True,
        title = 'Insert spaces',
        doc = "Indent with spaces in the macro and Python editors: the Tab key\n"
              "and the automatic indentation after Enter insert IndentSize\n"
              "spaces. When off they insert a tab character."),
    ParamBool('EnableLineNumber', True,
        title = 'Enable line numbers',
        doc = "Show line numbers in the left margin of the macro and Python\n"
              "editors. Applied at once."),
    ParamBool('EnableBlockCursor', False,
        title = 'Enable block cursor',
        doc = "Draw the text cursor of the macro and Python editors as a block\n"
              "one character wide instead of a line. Applied at once."),
    ParamBool('CheckSystemExit', True,
        title = 'Ask before exiting from the console',
        doc = "When code run in the Python console raises SystemExit, ask before\n"
              "the program is closed. When off it closes at once."),
    Color('Text', 0, 'Text colour',
        "Colour of plain text in the editors and the Python console. Not\n"
        "set means the window's text colour, which follows the theme."),
    Color('Keyword', 0x0000FF00, 'Keyword colour',
        "Colour of Python keywords in the editors and the Python console."),
    Color('Comment', 0x00AA0000, 'Comment colour',
        "Colour of comments in the editors and the Python console."),
    Color('BlockComment', 0xA0A0A400, 'Block comment colour',
        "Colour of triple-quoted text in the editors and the Python\n"
        "console.", key='Block comment'),
    Color('Number', 0x0000FF00, 'Number colour',
        "Colour of numbers in the editors and the Python console."),
    Color('String', 0xFF000000, 'String colour',
        "Colour of quoted text in the editors and the Python console."),
    Color('ClassName', 0xFFAA0000, 'Class name colour',
        "Colour of the name after 'class' in the editors and the Python\n"
        "console.", key='Class name'),
    Color('DefineName', 0xFFAA0000, 'Define name colour',
        "Colour of the name after 'def' in the editors and the Python\n"
        "console.", key='Define name'),
    Color('Operator', 0xA0A0A400, 'Operator colour',
        "Colour of operators and brackets in the editors and the Python\n"
        "console."),
    Color('PythonOutput', 0xAAAA7F00, 'Python output colour',
        "Colour of what Python prints in the Python console.", key='Python output'),
    Color('PythonError', 0xFF000000, 'Python error colour',
        "Colour of Python's error messages in the Python console.", key='Python error'),
    Color('CurrentLineHighlight', 0xE0E0E000, 'Current line highlight',
        "Background colour of the line the cursor is on in the macro and\n"
        "Python editors.", key='Current line highlight'),
    Color('Background', 0, 'Python console background',
        "Background colour of the Python console. 0, or not set, leaves\n"
        "the background to the theme."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
