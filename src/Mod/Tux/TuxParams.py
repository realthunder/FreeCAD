# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Tux, which are kept under "User parameter:Tux".

Tux is written in Python and has no generated class: its settings are
described to the settings registry when Init.py imports this file
(freecad.params), so that the omni search lists them with the others. None
is on a preference page; two are set from the menu of the navigation
indicator. The code that reads them is left as it is -- each default here is
the one its reader passes.

Not here: the tool bar places PersistentToolbars keeps, which are in groups
named after the workbenches, and its 'Deprecated' marker, which notes that
those places were handed over to the main window.
"""
import sys

from freecad.params import ParamBool, register

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
        "workbench. It has handed that over to the main window and leaves the tool "
        "bars alone since. Read at start.",
    ),
]

register(sys.modules[__name__])
