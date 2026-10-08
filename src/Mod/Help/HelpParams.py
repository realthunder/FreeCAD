# SPDX-License-Identifier: LGPL-2.1-or-later
"""The settings of Preferences/Mod/Help.

Help is written in Python and has no generated class: its settings are
described to the settings registry when Init.py imports this file
(freecad.params), so that the omni search lists them with the others. Ten
are on the module's preference page, and the title and the editor of each
are the page's; each default is the one Help.py reads it with. Four more
are where the help panel was last left, and how large.

The page shows two groups of radio buttons, and each button is a setting of
its own. Help.py does not read them as groups: it takes the first that is
on, in the order the documentation of each says.
"""
import sys

from freecad.params import ParamBool, ParamFile, ParamInt, ParamString, register

NameSpace = "Help"
ClassName = "HelpParams"
ParamPath = "User parameter:BaseApp/Preferences/Mod/Help"

_NEXT = " Takes effect at the next help page opened."

Params = [
    ParamBool(
        "optionWiki",
        True,
        title="FreeCAD Wiki (online)",
        doc="Help pages are fetched from the FreeCAD wiki at https://wiki.freecad.org. "
        "Of the four sources the first that is on is used, in this order: the wiki, the "
        "Markdown version, GitHub, the custom location." + _NEXT,
    ),
    ParamBool(
        "optionMarkdown",
        False,
        title="Markdown version (online)",
        doc="Help pages are fetched from the Markdown conversion of the wiki on FreeCAD's "
        "GitHub account, which the custom style sheet can style. Used when the wiki is "
        "off. The 'markdown' or 'pandoc' Python module gives the best rendering." + _NEXT,
    ),
    ParamBool(
        "optionGithub",
        False,
        title="GitHub (online)",
        doc="Help pages are fetched as GitHub renders them. Used when the wiki and the "
        "Markdown version are off. The preference page has this choice disabled." + _NEXT,
    ),
    ParamBool(
        "optionCustom",
        False,
        title="Custom location",
        doc="Help pages are read from the custom location. Used when the three online "
        "sources are off." + _NEXT,
    ),
    ParamString(
        "Location",
        "",
        title="Custom location of the help files",
        doc="A URL, or the folder the help files are in, for the custom location. Empty, "
        "the folder Mod/Documentation/wiki of the user's application data is used, where "
        "the offline documentation addon puts them." + _NEXT,
    ),
    ParamString(
        "Suffix",
        "",
        title="Translation suffix",
        doc='A translation suffix, for example "fr" for the French documentation. Empty, '
        "the pages are in English." + _NEXT,
    ),
    ParamBool(
        "optionTab",
        True,
        title="In a FreeCAD tab",
        doc="The choice of the preference page that a help page opens in a tab of the "
        "main window. Nothing reads it: a tab is what is used when neither the web "
        "browser nor the dialog is on.",
    ),
    ParamBool(
        "optionBrowser",
        False,
        title="In your default web browser",
        doc="A help page opens in the web browser of the desktop. Of the three places "
        "this one is looked at first; it is also what is used when the Qt web engine is "
        "not there to show a page inside FreeCAD." + _NEXT,
    ),
    ParamBool(
        "optionDialog",
        False,
        title="In a separate, embeddable dialog",
        doc="A help page opens in a dockable panel of the main window, which can stay "
        "open beside the 3D view. Used when the web browser is off." + _NEXT,
    ),
    ParamString(
        "StyleSheet",
        "",
        proxy=ParamFile(),
        title="Custom stylesheet",
        doc="A CSS file that styles the help pages FreeCAD renders itself, which are the "
        "Markdown ones. Empty, the module's default.css is used." + _NEXT,
    ),
    ParamInt(
        "dockWidgetArea",
        2,
        title="Help panel: dock area",
        doc="The side of the main window the help panel was last docked at: 1 left, 2 "
        "right, 4 top, 8 bottom. Stored when the panel is moved, read when it is made.",
    ),
    ParamBool(
        "dockWidgetFloat",
        True,
        title="Help panel: floating",
        doc="The help panel was last floating, not docked. Stored when the panel is "
        "moved, read when it is made.",
    ),
    ParamInt(
        "dockWidgetWidth",
        200,
        title="Help panel: width",
        doc="Width, in pixels, the help panel last had. Stored when the panel is "
        "moved, read when it is made.",
    ),
    ParamInt(
        "dockWidgetHeight",
        300,
        title="Help panel: height",
        doc="Height, in pixels, the help panel last had. Stored when the panel is "
        "moved, read when it is made.",
    ),
]

register(sys.modules[__name__])
