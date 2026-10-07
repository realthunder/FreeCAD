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
'''Auto code generator for parameters in Preferences/Mod/CAM
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString, ParamHex, ParamColor

NameSpace = 'Path'
ClassName = 'CAMParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/CAM'
ClassDoc = 'Convenient class to obtain the settings of the CAM workbench that C++ reads'

# Only what C++ reads. Most of CAM's settings are read by its Python code,
# through Path/Preferences.py, and are not listed here.
#
# DefaultProbePathColor is the reader's, 255,235,0; the Colors page showed
# 255,255,5 and stored that at OK. DefaultBBoxSelectionColor was read with a
# last byte of 0 where the page stores 255; that byte is not used.
# WarningSuppressAllSpeeds is the key the Advanced page stores; the cycle
# time estimate read WarningsSuppressAllSpeeds, which nothing stores, so its
# warning was suppressed whatever the page said.
Params = [
    ParamHex('DefaultNormalPathColor', 0x00AA00FF, proxy=ParamColor(transparency=False),
        title = "Feed move colour",
        doc = "Colour of the feed moves of a toolpath. Path objects take it when\n"
              "they are created; the CAM simulator follows a change at once."),
    ParamHex('DefaultRapidPathColor', 0xAA0000FF, proxy=ParamColor(transparency=False),
        title = "Rapid move colour",
        doc = "Colour of the rapid moves of a toolpath. Paths in the 3D view pick\n"
              "it up the next time their colours are rebuilt; the CAM simulator\n"
              "follows a change at once."),
    ParamHex('DefaultProbePathColor', 0xFFEB00FF, proxy=ParamColor(transparency=False),
        title = "Probe move colour",
        doc = "Colour of the probe moves of a toolpath. Paths in the 3D view pick\n"
              "it up the next time their colours are rebuilt."),
    ParamHex('DefaultPathMarkerColor', 0x55FF00FF, proxy=ParamColor(transparency=False),
        title = "Path marker colour",
        doc = "Colour of the node markers of a toolpath. Applies to path objects\n"
              "created afterwards."),
    ParamInt('DefaultPathLineWidth', 1,
        title = "Path line width",
        doc = "Line width in pixels of a toolpath in the 3D view. Applies to path\n"
              "objects created afterwards."),
    ParamFloat('DefaultArrowScale', 3.0,
        title = "Path arrow size",
        doc = "Size factor of the direction arrow drawn on a toolpath. Applies to\n"
              "path objects created afterwards."),
    ParamInt('DefaultSelectionStyle', 0,
        title = "Path selection style",
        doc = "How a toolpath is selected in the 3D view: 0 by its shape, 1 by\n"
              "its bounding box, 2 not at all. Applies to path objects created\n"
              "afterwards."),
    ParamHex('DefaultBBoxNormalColor', 0xFFFFFFFF, proxy=ParamColor(transparency=False),
        title = "Bounding box colour",
        doc = "Colour of the bounding box of a toolpath that is selected by shape\n"
              "or cannot be selected. Takes effect the next time the bounding box\n"
              "is drawn."),
    ParamHex('DefaultBBoxSelectionColor', 0xC8FFFFFF, proxy=ParamColor(transparency=False),
        title = "Bounding box selection colour",
        doc = "Colour of the bounding box of a toolpath that is selected by its\n"
              "bounding box. Takes effect the next time the bounding box is\n"
              "drawn."),
    ParamBool('HideFirstRapid', False,
        title = "Hide first rapid move",
        doc = "Hides the initial rapid move of a toolpath by starting the display\n"
              "at the first feed move. Takes effect the next time the path\n"
              "changes."),
    ParamBool('WarningSuppressAllSpeeds', True,
        title = "Suppress missing speeds warning",
        doc = "Suppresses the warning that a tool controller has no feed rate\n"
              "when the cycle time of a toolpath is estimated. Takes effect at\n"
              "the next estimate."),
    ParamBool('SimulatorShowInDocumentView', True,
        title = "Simulate in document view",
        doc = "Shows the simulated stock in the 3D view of the document while the\n"
              "CAM simulator runs. The button in the simulator follows a change\n"
              "at once; the view is attached at the next simulation."),
    ParamBool('ForceLegacyGLRender', False,
        title = "Force legacy simulator rendering",
        doc = "Makes the CAM simulator draw with its legacy OpenGL renderer\n"
              "instead of the render backend. Takes effect the next time the\n"
              "simulator is opened."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
