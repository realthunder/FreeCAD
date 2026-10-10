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
'''Auto code generator for parameters in Preferences/NotificationArea
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt

NameSpace = 'Gui'
ClassName = 'NotificationAreaParams'
ParamPath = 'User parameter:BaseApp/Preferences/NotificationArea'
ClassDoc = 'Convenient class to obtain the settings of the notification area'
Signal = True

# Every one of these is applied by the notification area at once: it keeps
# what it needs of them in members and refreshes the one that changed
# (NotificationArea::ParameterObserver). The width's key is spelled
# "NotificiationWidth" in every profile there is, so that is its name.
Params = [
    ParamBool('NotificationAreaEnabled', True,
        title = 'Enable notification area',
        doc = "Show the notification area in the status bar and collect\n"
              "notifications in it. Takes effect at once."),
    ParamBool('NonIntrusiveNotificationsEnabled', True,
        title = 'Enable non-intrusive notifications',
        doc = "Show a notification in a bubble next to the notification area\n"
              "instead of a dialog box that has to be answered. When off, a\n"
              "notification that asks for it is shown as a dialog box."),
    ParamInt('NotificationTime', 20,
        title = 'Notification duration',
        doc = "Seconds a non-intrusive notification stays on screen, unless a\n"
              "mouse button is clicked first. 0 to 120."),
    ParamInt('MaxOpenNotifications', 15,
        title = 'Maximum number of notifications',
        doc = "Largest number of non-intrusive notifications on screen at the\n"
              "same time; older ones give way."),
    ParamInt('NotificiationWidth', 800,
        title = 'Notification width',
        doc = "Width of a non-intrusive notification in pixels. At least 300."),
    ParamBool('HideNonIntrusiveNotificationsWhenWindowDeactivated', True,
        title = 'Hide when other window is activated',
        doc = "Open non-intrusive notifications disappear when another window\n"
              "is activated."),
    ParamBool('PreventNonIntrusiveNotificationsWhenWindowNotActive', True,
        title = 'Do not show when inactive',
        doc = "Show no non-intrusive notification while the main window is not\n"
              "the active window. They are still collected in the list."),
    ParamInt('MaxWidgetMessages', 1000,
        title = 'Maximum messages in the list',
        doc = "Largest number of messages the notification area's list keeps;\n"
              "older ones are dropped. 0 means no limit."),
    ParamBool('AutoRemoveUserNotifications', True,
        title = 'Auto-remove user notifications',
        doc = "Remove a user notification from the list once the notification\n"
              "duration has passed."),
    ParamBool('DeveloperErrorSubscriptionEnabled', False,
        title = 'Debug errors',
        doc = "Show errors meant for developers in the notification area too."),
    ParamBool('DeveloperWarningSubscriptionEnabled', False,
        title = 'Debug warnings',
        doc = "Show warnings meant for developers in the notification area too."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
