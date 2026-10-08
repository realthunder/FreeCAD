# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Tux, which are kept under "User parameter:Tux".

Tux is written in Python and has no generated class: its settings are
described to the settings registry when Init.py imports this file
(freecad.params), so that the omni search lists them with the others. None
is on a preference page; two are set from the menu of the navigation
indicator. The code that reads them is left as it is -- each default here is
the one its reader passes.

Not here: the tool bar places PersistentToolbars keeps, which are in groups
named after the workbenches.
"""
import sys

from freecad.params import ParamBool, ParamInt, register

NameSpace = "Tux"
ClassName = "TuxParams"
ParamPath = "User parameter:Tux"

Params = [
    ParamBool(
        "NavigationIndicatorEnabled",
        True,
        subpath="NavigationIndicator",
        param_name="Enabled",
        title="Navigation indicator",
        doc="Shows the navigation indicator in the status bar: a button that names the "
        "navigation style of the 3D view and opens a menu to change it. Read at start.",
    ),
    ParamBool(
        "NavigationIndicatorCompact",
        False,
        subpath="NavigationIndicator",
        param_name="Compact",
        title="Navigation indicator: compact",
        doc="The navigation indicator shows the icon of the navigation style without "
        "its name. Set by 'Compact' in the indicator's Settings menu, and read at "
        "start.",
    ),
    ParamBool(
        "NavigationIndicatorTooltip",
        True,
        subpath="NavigationIndicator",
        param_name="Tooltip",
        title="Navigation indicator: tooltip",
        doc="The navigation indicator and the entries of its menu show a tool tip with "
        "the mouse gestures of the navigation style. Set by 'Tooltip' in the "
        "indicator's Settings menu, and read at start.",
    ),
    ParamBool(
        "PersistentToolbarsEnabled",
        True,
        subpath="PersistentToolbars",
        param_name="Enabled",
        title="Persistent toolbars",
        doc="Loads the part of Tux that kept the place of each tool bar for each "
        "workbench. It has handed that over to the main window; see its 'Deprecated' "
        "setting. Read at start.",
    ),
    ParamInt(
        "PersistentToolbarsDeprecated",
        1,
        subpath="PersistentToolbars",
        param_name="Deprecated",
        title="Persistent toolbars: handed over",
        doc="1: at the next start the tool bar places Tux has kept are applied once "
        "more, so that the main window takes them over, and this becomes 2. 2: handed "
        "over, Tux leaves the tool bars alone. 0: Tux keeps and restores the place of "
        "each tool bar for each workbench itself. Read at start.",
    ),
]

register(sys.modules[__name__])
