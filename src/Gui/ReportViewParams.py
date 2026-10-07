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
'''Auto code generator for parameters in Preferences/OutputWindow
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamQString, ParamUInt, ParamFloat

NameSpace = 'Gui'
ClassName = 'ReportViewParams'
ParamPath = 'User parameter:BaseApp/Preferences/OutputWindow'
ClassDoc = 'Convenient class to obtain ReportView related parameters'
Signal = True

Params = [
    ParamBool('checkShowReportViewOnWarning', True,
        title = 'Show report view on warning',
        doc = "Bring the report view on screen when a warning arrives."),
    ParamBool('checkShowReportViewOnError', True,
        title = 'Show report view on error',
        doc = "Bring the report view on screen when an error arrives."),
    ParamBool('checkShowReportViewOnNormalMessage', False,
        title = 'Show report view on normal message',
        doc = "Bring the report view on screen when a normal message arrives."),
    ParamBool('checkShowReportViewOnLogMessage', False,
        title = 'Show report view on log message',
        doc = "Bring the report view on screen when a log message arrives."),
    ParamBool('checkShowReportViewOnCritical', False,
        title = 'Show report view on critical message',
        doc = "Bring the report view on screen when a critical message arrives."),
    ParamBool("checkShowReportTimecode", True,
        title = 'Show time code',
        doc = "Put the time a message arrived in front of each line of the report\n"
              "view."),

    ParamInt("LogMessageSize", 0,
        doc = "Largest number of characters of one log message shown in the report\n"
              "view. A longer message is cut off. 0 uses the built-in limit of 2048\n"
              "characters."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many of the most recently shown lines a new line is compared against
    # before it is shown. A line that repeats any of them is held back instead,
    # and shown once - the first one held, carrying (xN) for the number it
    # stands in for, and clickable to expand the ones that were kept back -
    # when a different line has to be shown or DuplicateTimeout expires.
    # Set to 0 to show every line as it arrives.
    # This affects the Report view only. The log file, the Python console and
    # every other console observer still receive every message.
    ParamInt("DuplicateWindow", 3,
        doc="How many of the most recent lines a new line is compared with. A line\n"
            "that repeats one of them is held back and shown once with a count (xN)\n"
            "that can be clicked to expand. 0 shows every line. Affects the Report\n"
            "view only; the log file and other consoles get every message."),
    ParamInt("DuplicateKeyLength", 100,
        doc='How many leading non-digit characters two messages must share to count\n'
            'as the same message. Digits are skipped rather than compared, so the same\n'
            'sentence carrying a different source line, element index or coordinate\n'
            'collapses into one entry instead of one entry per number.'),
    ParamInt("DuplicateTimeout", 1000,
        doc='Milliseconds a held duplicate line waits before it is shown anyway, timed\n'
            'from the first repeat rather than the last, so a continuous storm still\n'
            'reports at this interval. Set to 0 to hold until another line arrives.'),
    ParamQString('CommandRedirect', '',
        doc='Prefix for marking python command in message to be redirected to Python console\n'
            'This is used as a debug help for output command from external libraries'),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
