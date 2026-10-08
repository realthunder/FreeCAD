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
'''Auto code generator for parameters in Preferences/Mod/Start
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString, ParamHex, ParamColor

NameSpace = 'Start'
ClassName = 'StartParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Start'
ClassDoc = 'Convenient class to obtain the settings of the Start page'

# The Start page has no preference page: none of these could be set but by
# hand. The colours are packed as 0xRRGGBB00; the last byte is not read.
# FileCardSpacing is read at four places with three defaults -- 16 between
# the file cards, which is upstream's and the one given here, 25 for the
# height of the New File cards and 15 for the page layout; the last two keep
# theirs.
Params = [
    ParamBool('ShowOnStartup', True,
        title = "Show Start page",
        doc = "Opens the Start page when FreeCAD starts. Takes effect at the next\n"
              "start."),
    ParamString('AutoloadModule', '',
        title = "Workbench after Start",
        doc = "Workbench activated after Empty file or Open File is used on the\n"
              "Start page; $LastModule means the workbench used last, empty\n"
              "leaves the workbench as it is. Takes effect at the next use."),
    ParamBool('closeStart', False,
        title = "Close Start page after use",
        doc = "Closes the Start page after a document has been created or opened\n"
              "from it. Takes effect at the next use."),
    ParamInt('FileThumbnailIconsSize', 128,
        title = "Thumbnail size",
        doc = "Size in pixels of the file thumbnails on the Start page. Takes\n"
              "effect at the next repaint of the page."),
    ParamInt('FileCardSpacing', 16,
        title = "Card spacing",
        doc = "Spacing in pixels between the file cards of the Start page. The\n"
              "file lists follow at the next layout. The New File cards and the\n"
              "page layout read the same key, with a spacing of their own while\n"
              "it is not stored."),
    ParamInt('NewFileIconSize', 48,
        title = "New File icon size",
        doc = "Size in pixels of the icons on the New File cards of the Start\n"
              "page. Takes effect when the Start page is next created."),
    ParamInt('FileCardLabelWith', 180,
        title = "New File text width",
        doc = "Width in pixels reserved for the text of a New File card on the\n"
              "Start page. Takes effect when the Start page is next created."),
    ParamInt('FileCardDescriptionLines', 2,
        title = "Card description lines",
        doc = "Number of lines the description of a New File card may wrap to\n"
              "before it is cut short. Takes effect when the Start page is next\n"
              "created."),
    ParamBool('FileCardUseStyleSheet', True,
        title = "Colour New File cards",
        doc = "Gives the New File cards of the Start page their own colours while\n"
              "no theme style sheet is loaded. When off, the cards are drawn as\n"
              "ordinary buttons. Takes effect at the next style change or when\n"
              "the Start page is next created."),
    ParamHex('FileCardBackgroundColor', 0xDDDDDD00, proxy=ParamColor(transparency=False),
        title = "New File card background",
        doc = "Background colour of the New File cards on the Start page. Only\n"
              "used while no style sheet is loaded. Takes effect at the next\n"
              "style change or when the Start page is next created."),
    ParamHex('FileCardBorderColor', 0x62A0EA00, proxy=ParamColor(transparency=False),
        title = "New File card hover border",
        doc = "Border colour of a New File card under the mouse pointer. Only\n"
              "used while no style sheet is loaded. Takes effect at the next\n"
              "style change or when the Start page is next created."),
    ParamHex('FileCardSelectionColor', 0x26A26900, proxy=ParamColor(transparency=False),
        title = "New File card pressed border",
        doc = "Border colour of a New File card while it is pressed. Only used\n"
              "while no style sheet is loaded. Takes effect at the next style\n"
              "change or when the Start page is next created."),
    ParamHex('FileThumbnailBackgroundColor', 0xDDDDDD00, proxy=ParamColor(transparency=False),
        title = "Thumbnail background colour",
        doc = "Background colour of the file thumbnails on the Start page. Only\n"
              "used while no style sheet is loaded. Takes effect at the next\n"
              "repaint."),
    ParamHex('FileThumbnailBorderColor', 0x62A0EA00, proxy=ParamColor(transparency=False),
        title = "Thumbnail hover border",
        doc = "Border colour of a file thumbnail under the mouse pointer on the\n"
              "Start page. Only used while no style sheet is loaded. Takes effect\n"
              "at the next repaint."),
    ParamHex('FileThumbnailSelectionColor', 0x26A26900, proxy=ParamColor(transparency=False),
        title = "Thumbnail selection border",
        doc = "Border colour of the selected file thumbnail on the Start page.\n"
              "Only used while no style sheet is loaded. Takes effect at the next\n"
              "repaint."),
    # what the program keeps for itself; the Start page reads the group
    ParamBool('FirstStart2024', True,
        title = "Start: first start",
        doc = "The Start page still shows its first-start panel, where the\n"
              "language, the units, the navigation style and the theme are\n"
              "chosen. The program switches it off when the panel is dismissed."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
