# SPDX-License-Identifier: LGPL-2.1-or-later
"""The Addon Manager's settings, described to the settings registry.

The Addon Manager keeps the name and the default of every setting it reads
in addonmanager_preferences_defaults.json. That file is the table: it is
read here, not written a second time, and each row is described to
App::ParamRegistry (FreeCAD.registerParam), which is where the omni search
finds a setting. This module adds a title, a short documentation and, where
there is a choice, its items.

A row whose default in the file is not of the kind the code stores has it
in OVERRIDES: the three proxy switches are empty texts in the file and are
stored and read as switches.
"""

import json
import os

import FreeCAD

PATH = "User parameter:BaseApp/Preferences/Addons"

_URL = "Address the Addon Manager fetches %s from."
_STATE = " Kept by the Addon Manager itself."

# The defaults the readers pass where the defaults file gives another kind of value.
OVERRIDES = {"NoProxyCheck": True, "SystemProxyCheck": False, "UserProxyCheck": False}

# (title, documentation) or (title, documentation, items of a choice) by entry
DOCS = {
    "AddonFlagsURL": ("Addon flags address", _URL % "the list of obsolete and blocked addons"),
    "AddonsRemoteCacheURL": ("Addon cache address", _URL % "the metadata of all addons"),
    "AddonsUpdateStatsURL": ("Addon statistics address", _URL % "the update statistics of addons"),
    "MacroGitURL": ("Macro repository address", "Address of the Git repository of macros."),
    "MacroUpdateStatsURL": (
        "Macro statistics address",
        _URL % "the update statistics of macros",
    ),
    "MacroWikiURL": ("Macro wiki address", "Address of the wiki page that lists macros."),
    "PrimaryAddonsSubmoduleURL": (
        "Addon list address",
        _URL % "the list of addon repositories",
    ),
    "RemoteIconCacheURL": ("Icon cache address", _URL % "the icons of all addons"),
    "AutoCheck": (
        "Check for updates automatically",
        "Checks the installed addons for updates when the Addon Manager opens.",
    ),
    "BlockedMacros": (
        "Blocked macros",
        "Names, separated by commas, of wiki pages that are not offered as macros.",
    ),
    "CustomRepoHash": (
        "Custom repositories hash",
        "A hash of the custom repositories the cache was last built with." + _STATE,
    ),
    "CustomRepositories": (
        "Custom repositories",
        "Addon repositories beside the official ones, one on a line: its address, a space "
        "and the branch.",
    ),
    "CustomToolbarName": (
        "Macro tool bar name",
        "Name of the tool bar a button for an installed macro is added to.",
    ),
    "DaysBetweenUpdates": (
        "Days between cache updates",
        "Days between two updates of the addon cache; -1 is never by itself. Follows the "
        "update frequency setting.",
    ),
    "DownloadMacros": (
        "Download macro metadata",
        "Downloads the metadata of all macros when the cache is built, about 10 MB.",
    ),
    "GitExecutable": (
        "Git executable",
        "The Git program the Addon Manager runs. 'Not set' lets it look for one.",
    ),
    "HideNewerFreeCADRequired": (
        "Hide addons that need a newer FreeCAD",
        "Leaves the addons that need a newer version of FreeCAD out of the list.",
    ),
    "HideObsolete": ("Hide obsolete addons", "Leaves the addons marked obsolete out of the list."),
    "HidePy2": (
        "Hide Python 2 addons",
        "Leaves the addons marked as Python 2 only out of the list.",
    ),
    "KnownPythonVersions": (
        "Known Python versions",
        "The Python versions packages were installed for, as a JSON list." + _STATE,
    ),
    "LastCacheUpdate": (
        "Last cache update",
        "When the addon cache was last updated; 'never' at first." + _STATE,
    ),
    "MacroCacheUpdateFrequency": (
        "Macro cache update frequency",
        "Days after which the cache of macros is built again.",
    ),
    "NoProxyCheck": ("No proxy", "The Addon Manager connects without a proxy."),
    "SystemProxyCheck": (
        "System proxy",
        "The Addon Manager connects through the proxy of the system.",
    ),
    "UserProxyCheck": (
        "User-defined proxy",
        "The Addon Manager connects through the proxy given as the proxy address.",
    ),
    "ProxyUrl": ("Proxy address", "The proxy used when the user-defined proxy is chosen."),
    "PackageTypeSelection": (
        "Addon list: type filter",
        "The entry of the type filter of the addon list that was last chosen.",
    ),
    "StatusSelection": (
        "Addon list: status filter",
        "The entry of the status filter of the addon list that was last chosen.",
    ),
    "ViewStyle": (
        "Addon list: view style",
        "How the addon list was last shown, by the place of the style in its list.",
    ),
    "SelectedAddon": ("Selected addon", "The addon last selected in the list." + _STATE),
    "ShowBranchSwitcher": (
        "Show the branch switcher",
        "Shows, for an addon installed with Git, the option to switch its branch.",
    ),
    "UpdateFrequencyComboEntry": (
        "Update frequency",
        "How often the addon cache is updated by itself.",
        ["Manual", "Daily", "Weekly"],
    ),
    "WindowHeight": (
        "Addon Manager height",
        "Height, in pixels, of the Addon Manager. Stored when it closes.",
    ),
    "WindowWidth": (
        "Addon Manager width",
        "Width, in pixels, of the Addon Manager. Stored when it closes.",
    ),
    "alwaysAskForToolbar": (
        "Always ask for the tool bar",
        "Asks each time which tool bar the button of an installed macro goes to.",
    ),
    "devModeLastSelectedLicense": (
        "Developer mode: last licence",
        "The licence last chosen in the developer mode." + _STATE,
    ),
    "developerMode": (
        "Addon developer mode",
        "Shows the tools for writing the metadata of an addon.",
    ),
    "disableGit": (
        "Disable Git",
        "Installs and updates addons from zip files even when Git is there.",
    ),
    "dontShowAddMacroButtonDialog": (
        "Do not offer a tool bar button for a macro",
        "Skips the dialogue that offers a tool bar button after a macro is installed.",
    ),
    "readWarning2022": (
        "First run warning read",
        "Set once the warning shown at the first start of the Addon Manager is confirmed.",
    ),
}


def table():
    """The rows of the defaults file: (entry, type, default) each."""
    name = os.path.join(os.path.dirname(__file__), "addonmanager_preferences_defaults.json")
    try:
        with open(name, encoding="utf-8") as f:
            defaults = json.load(f)
    except (OSError, ValueError):
        # a start is not the place to fail over it; the Addon Manager says so when used
        return []
    rows = []
    for entry, default in defaults.items():
        default = OVERRIDES.get(entry, default)
        if isinstance(default, bool):
            kind = "Bool"
        elif isinstance(default, int):
            kind = "Int"
        elif isinstance(default, float):
            kind = "Float"
        else:
            kind = "String"
        rows.append((entry, kind, default))
    return rows


def register():
    """Describe every row of the defaults file."""
    add = getattr(FreeCAD, "registerParam", None)
    if add is None:
        return
    for entry, kind, default in table():
        written = DOCS.get(entry, (entry, ""))
        spec = {"title": written[0], "doc": written[1], "namespace": "AddonManager"}
        spec["context"] = "AddonManager"
        if len(written) > 2:
            spec.update(proxy="ComboBox", items=list(written[2]), translateItems=False)
        try:
            add(PATH, entry, kind, default, **spec)
        except (TypeError, ValueError) as err:
            FreeCAD.Console.PrintWarning(
                "Addon Manager: the setting %s is not described: %s\n" % (entry, err)
            )


register()
