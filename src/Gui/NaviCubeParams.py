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
'''Auto code generator for parameters in Preferences/NaviCube
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamFloat, ParamString, ParamHex

NameSpace = 'Gui'
ClassName = 'NaviCubeParams'
ParamPath = 'User parameter:BaseApp/Preferences/NaviCube'
ClassDoc = 'Convenient class to obtain the settings of the navigation cube'

# The cube reads all of its settings again a moment after any key of this
# group changes, and redraws.
#
# The colours are stored as 0xAARRGGBB -- the order Qt has, not the
# 0xRRGGBBAA of the other colour settings -- which is why they have no
# colour button here. The cube reads them by name from its own table
# (NaviCube.cpp, m_colors), whose defaults these repeat. The texts on the
# faces and the axes have no fixed default (they are translated) and are
# not listed. Where the cube sits in a view is CornerNaviCube and
# ShowNaviCube of the View group.
Params = [
    ParamInt('CubeSize', 132, title='Navigation cube size',
        doc="Size of the navigation cube in pixels, 10 to 1024."),
    ParamBool('NaviRotateToNearest', True, title='Rotate to nearest',
        doc="A click on a face of the navigation cube turns the view to that\n"
            "face in the nearest of its four upright positions. When off the\n"
            "face is shown the way its text reads."),
    ParamInt('NaviStepByTurn', 8, title='Steps by turn',
        doc="Number of steps a full turn is made in with the arrow buttons of\n"
            "the navigation cube, 4 to 36."),
    ParamBool('ShowCS', True, title='Show coordinate system on the cube',
        doc="Draw the X, Y and Z axes at a corner of the navigation cube."),
    ParamFloat('BorderWidth', 1.5, title='Navigation cube border width',
        doc="Width in pixels of the lines around the faces of the navigation\n"
            "cube."),
    ParamFloat('ChamferSize', 0.12, title='Navigation cube chamfer size',
        doc="Size of the edge and corner faces of the navigation cube, as a\n"
            "fraction of the cube."),
    ParamBool('AutoHideCube', False, title='Auto hide navigation cube',
        doc="Hide the navigation cube while the mouse is away from it."),
    ParamBool('AutoHideButton', True, title='Auto hide navigation cube buttons',
        doc="Hide the arrow buttons around the navigation cube while the mouse\n"
            "is away from it."),
    ParamInt('AutoHideTimeout', 300, title='Auto hide delay',
        doc="Milliseconds the mouse has to be away before the navigation cube\n"
            "or its buttons are hidden."),
    ParamBool('FontAutoSize', True, title='Automatic font size',
        doc="Size the texts on the faces of the navigation cube to the faces,\n"
            "by FontScale. When off FontSize is used."),
    ParamFloat('FontScale', 0.22, title='Font scale',
        doc="Height of the texts on the navigation cube as a fraction of a\n"
            "face, with FontAutoSize on."),
    ParamString('FontString', 'Helvetica', title='Navigation cube font',
        doc="Font family of the texts on the faces of the navigation cube."),
    ParamInt('FontSize', 0, title='Navigation cube font size',
        doc="Font size of the texts on the faces of the navigation cube in\n"
            "points, with FontAutoSize off. 0 sizes them to the faces."),
    ParamInt('FontWeight', 87, title='Navigation cube font weight',
        doc="Weight of the font on the faces of the navigation cube, 0 to 99."),
    ParamBool('FontItalic', False, title='Navigation cube font italic',
        doc="Draw the texts on the faces of the navigation cube in italics."),
    ParamInt('FontStretch', 62, title='Navigation cube font stretch',
        doc="Horizontal stretch of the font on the faces of the navigation\n"
            "cube in percent; 100 is the width the font has."),
    ParamString('AxisFont', 'Monospace', title='Axis label font',
        doc="Font family of the axis labels of the navigation cube."),
    ParamInt('AxisFontSize', 8, title='Axis label font size',
        doc="Font size of the axis labels of the navigation cube in points."),
    ParamInt('AxisFontWeight', 50, title='Axis label font weight',
        doc="Weight of the font of the axis labels of the navigation cube, 0\n"
            "to 99."),
    ParamBool('AxisFontItalic', False, title='Axis label font italic',
        doc="Draw the axis labels of the navigation cube in italics."),
    ParamHex('TextColor', 0xFF000000, title='Navigation cube text colour',
        doc="Colour of the texts on the navigation cube, as 0xAARRGGBB."),
    ParamHex('HiliteColor', 0xFFAAE2FF, title='Navigation cube highlight colour',
        doc="Colour of the part of the navigation cube under the mouse, as\n"
            "0xAARRGGBB."),
    ParamHex('FrontColor', 0xC0E2E9EF, title='Navigation cube face colour',
        doc="Colour of the six main faces of the navigation cube, as\n"
            "0xAARRGGBB."),
    ParamHex('EdgeColor', 0xC0A1A6AB, title='Navigation cube edge colour',
        doc="Colour of the edge faces of the navigation cube, as 0xAARRGGBB."),
    ParamHex('CornerColor', 0xC0CDD4D9, title='Navigation cube corner colour',
        doc="Colour of the corner faces of the navigation cube, as\n"
            "0xAARRGGBB."),
    ParamHex('ButtonColor', 0x80E2E9EF, title='Navigation cube button colour',
        doc="Colour of the arrow buttons around the navigation cube, as\n"
            "0xAARRGGBB."),
    ParamHex('BorderColor', 0xFF323232, title='Navigation cube border colour',
        doc="Colour of the lines around the faces of the navigation cube, as\n"
            "0xAARRGGBB."),
    ParamHex('AxisLabelColor', 0xFF000000, title='Axis label colour',
        doc="Colour of the axis labels of the navigation cube, as 0xAARRGGBB."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
