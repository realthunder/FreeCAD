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
'''Auto code generator for the parameters of the 3D mouse, in BaseApp/Spaceball
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamSpinBox

NameSpace = 'Gui'
ClassName = 'SpaceballParams'
ParamPath = 'User parameter:BaseApp/Spaceball/Motion'
ClassDoc = 'Convenient class to obtain the settings of the 3D mouse'

# The group is not under Preferences: it is the one the "Spaceball Motion"
# page of the Customize dialog has always stored to, and the page only shows
# its widgets while a device is present. Every motion event of the device
# reads these settings (GUIApplicationNativeEventAware::importSettings), so a
# change counts from the next movement.
#
# The page's Default button clears the whole group -- the calibration and
# Remapping go with the settings.
#
# What a device's buttons run is in BaseApp/Spaceball/Buttons, a group per
# button, and is not here.

_SENSITIVITY = ("-50 slows it to a tenth, 0 leaves it as the device gives it,\n"
                "50 makes it three and a half times as fast.")

# name, title, what the axis does to the view
_AXES = [
    ('PanLR', 'pan left/right', 'pans the view left and right', 'Translations'),
    ('PanUD', 'pan up/down', 'pans the view up and down', 'Translations'),
    ('Zoom', 'zoom', 'zooms the view', 'Translations'),
    ('Tilt', 'tilt', 'tilts the view about its horizontal axis', 'Rotations'),
    ('Roll', 'roll', 'rolls the view about the axis into the screen', 'Rotations'),
    ('Spin', 'spin', 'spins the view about its vertical axis', 'Rotations'),
]

Params = [
    ParamInt('GlobalSensitivity', 0, title='3D mouse: global sensitivity',
        proxy=ParamSpinBox(-50, 50, 1),
        doc="How fast the view follows the 3D mouse, for all six movements at\n"
            "once: " + _SENSITIVITY),
    ParamBool('Dominant', False, title='3D mouse: dominant mode',
        doc="Of the six movements of the 3D mouse only the strongest one is\n"
            "followed at a time, so that a push does not also turn the view."),
    ParamBool('FlipYZ', False, title='3D mouse: flip Y/Z',
        doc="Swaps the Y and Z axes of the 3D mouse: pushing the cap forward\n"
            "zooms where it panned up and down, and the same for the two\n"
            "rotations about them."),
    ParamBool('Translations', True, title='3D mouse: enable translations',
        doc="The view follows the three pushes of the 3D mouse -- pan left and\n"
            "right, pan up and down, zoom. Off, all three are ignored whatever\n"
            "each of them is set to."),
    ParamBool('Rotations', True, title='3D mouse: enable rotations',
        doc="The view follows the three turns of the 3D mouse -- tilt, roll,\n"
            "spin. Off, all three are ignored whatever each of them is set to."),
]

for _name, _title, _does, _group in _AXES:
    Params += [
        ParamBool(_name + 'Enable', True, title='3D mouse: enable ' + _title,
            doc="The movement of the 3D mouse that " + _does + " is followed.\n"
                "It also needs '" + ('Enable translations' if _group == 'Translations'
                                     else 'Enable rotations') + "'."),
        ParamBool(_name + 'Reverse', False, title='3D mouse: reverse ' + _title,
            doc="Turns round the direction of the movement of the 3D mouse that\n"
                + _does + "."),
        ParamInt(_name + 'Sensitivity', 0, title='3D mouse: ' + _title + ' sensitivity',
            proxy=ParamSpinBox(-50, 50, 1),
            doc="How fast the movement of the 3D mouse that " + _does + "\n"
                "is followed: " + _SENSITIVITY + " The global sensitivity\n"
                "multiplies it."),
    ]

Params += [
    ParamInt('Remapping', 12345, title='3D mouse: axis remapping',
        doc="Which axis of the device each of the six movements is taken from:\n"
            "six digits, each of 0 to 5 once, in the order pan left/right, pan\n"
            "up/down, zoom, tilt, roll, spin. 12345 (that is 012345) takes them\n"
            "as the device gives them; anything that is no such number is\n"
            "ignored. On no page."),
    ParamBool('Calibrate', False, title='3D mouse: calibrate at the next movement',
        doc="A request more than a setting: the Calibrate button of the\n"
            "Spaceball Motion page sets it, and the next event of the device\n"
            "stores what the device reports at rest as the calibration and\n"
            "takes this away again."),
]

for _axis, _what in (('X', 'pan left/right'), ('Y', 'pan up/down'), ('Z', 'zoom'),
                     ('Xr', 'tilt'), ('Yr', 'roll'), ('Zr', 'spin')):
    Params.append(
        ParamInt('Calibration' + _axis, 0, title='3D mouse: calibration, ' + _what,
            doc="What the device reported for " + _what + " while at rest when it\n"
                "was last calibrated; taken off every reading. Stored by the\n"
                "program after Calibrate, not meant to be set by hand."))

Params.append(
    ParamString('Model', '', subpath='User parameter:BaseApp/Spaceball',
        title='3D mouse: device model',
        doc="The device chosen in the Spaceball Buttons page of the Customize\n"
            "dialog, whose buttons the page lists. Stored when the page's\n"
            "Reset button is pressed, not when the choice changes."))

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
