"""The two overlay themes are themes, whole ones, and no theme leaves a
piece of another behind.

docs/HandsOnQueue.md entry 30. Its first task, as decided 2026-10-09: "I
want two new theme under Tools -> Preset configurations -> Themes, which are
the dark theme merged with overlay dark theme, and light ones too. the
reason I want this is because some conflict/incompleteness in the way these
two themes split at the moment, such that switching in combinations got some
setting stuck at a bad combination, such as the python editor colors"; "two
presets should go. two new themes named Overlay dark/light theme".

Until then "Overlay dark theme" and "Overlay light theme" were presets
(data/settings/OverlayDark.FCParam, OverlayLight.FCParam): a layout with
colours and style sheets of its own, written over whatever theme was there.
And a theme writes the keys it lists and no others, so a key only one theme
listed stayed that theme's under the next: the editor's current line stayed
the Dark theme's dark blue under the Light theme, because Light did not list
it. Its third task ("add python console stylesheet setting to dark and light
theme ... once applied there is no way to un-apply it even switching to
classic theme") was the same thing seen from the console's background.

Claims:
  - the Themes menu of Tools > Preset configurations has "Overlay dark
    theme" and "Overlay light theme", and the presets beside it have neither;
  - an overlay theme lists every key of its base theme, Dark or Light, with
    the base's value -- but for the style sheet's parameter file, which it
    names because the file is found by the theme's name -- and beyond them
    only the layout: the overlay panels, the dock windows, the tree's
    hidden column, two overlay switches;
  - every key that Light, Dark or an overlay theme lists, the layout apart,
    is listed by all five themes: none of them can leave it behind;
  - "Overlay dark theme" applies: the tree is in an overlay, and the
    application is styled exactly as under Dark;
  - Light after it: the editor's current line and the style sheet's
    parameters are Light's, and the application is styled exactly as under
    Light applied first;
  - Dark after an overlay theme leaves the layout alone: the tree stays in
    its overlay (a theme without a layout does not undo one) -- and is
    styled for it: Dark and Light name the overlay sheet made for
    see-through panels, the one the presets named, so a panel in an overlay
    looks the same under a theme and under its overlay theme ("color shall
    follow the Dark/Light theme. port those overlay part", 2026-10-10);
  - with the preset's console background and a foreign tree item background
    in place, every one of the five puts its own there: Classic none, the
    others the page their sheet draws an editor on and the backing a tree
    item has in an overlay ("need to modify python editor part to make it
    more suitable to the theme": the preset's was a light grey that went
    with no theme).
"""
import os
import time
import traceback
import xml.etree.ElementTree as ET

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
PLAIN = ("Light", "Dark", "Classic")
OVERLAY = {"Overlay dark theme": "Dark", "Overlay light theme": "Light"}
THEMES = PLAIN + tuple(OVERLAY)
CONSOLE_BACKGROUND = 3368601600  # what the dark overlay preset wrote, 0xC8C8C800
ITEM_BACKGROUND = 0x12345678  # no theme's
PARAMETERS = "Preferences/MainWindow/ThemeStyleParametersFile"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def listed(theme):
    """The keys a theme's file lists, group/key from under BaseApp, with their values"""
    path = os.path.join(FreeCAD.getResourceDir(), "Gui", "PreferencePacks", theme, theme + ".cfg")
    found = {}

    def walk(node, trail):
        for child in node:
            if child.tag == "FCParamGroup":
                walk(child, trail + [child.get("Name")])
            elif child.get("Name") is not None:
                value = child.get("Value")
                found["/".join(trail[2:] + [child.get("Name")])] = (
                    child.text or "" if value is None else value)

    walk(ET.parse(path).getroot(), [])
    return found


def layout(key):
    return (key.startswith("MainWindow/DockWindows/") or key.startswith("Preferences/DockWindows/")
            or key == "Preferences/TreeView/HideColumn"
            or key.startswith("Preferences/View/DockOverlay"))


def console():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::PythonConsole":
            return w
    return None


def in_overlay(title):
    """Whether the dock window of that name sits in an overlay"""
    for dock in FreeCADGui.getMainWindow().findChildren(QtWidgets.QDockWidget):
        if dock.objectName() == title:
            w = dock.parentWidget()
            while w is not None:
                if w.metaObject().className() == "Gui::OverlayTabWidget":
                    return True
                w = w.parentWidget()
            return False
    return None


def menus():
    """The presets menu as it would open: its own entries, and those of Themes"""
    action = FreeCADGui.Command.get("Std_CmdPresets").getAction()[0]
    menu = action.menu()
    menu.aboutToShow.emit()
    own, themes = [], []
    for a in menu.actions():
        if a.menu() is not None and a.text().replace("&", "") == "Themes":
            themes = [b.text().replace("&", "") for b in a.menu().actions()]
        elif not a.isSeparator():
            own.append(a.text().replace("&", ""))
    return own, themes


def sheet():
    return QtWidgets.QApplication.instance().styleSheet()


def apply(theme):
    ok = FreeCADGui.applyTheme(theme)
    settle(3.5)
    return ok


def files():
    themes = dict((t, listed(t)) for t in THEMES)
    for name, base in OVERLAY.items():
        mine, its = themes[name], themes[base]
        differ = sorted(k for k in its if k != PARAMETERS and mine.get(k) != its[k])
        check("%s lists every key of %s with its value" % (name, base), not differ, differ[:8])
        check("%s names %s's style parameters" % (name, base),
              mine.get(PARAMETERS) == "qss:parameters/%s.yaml" % base, mine.get(PARAMETERS))
        extra = sorted(k for k in mine if k not in its)
        check("and beyond them only the layout", extra and all(layout(k) for k in extra),
              "%d keys; not layout: %s" % (len(extra), [k for k in extra if not layout(k)][:8]))
    owned = set()
    for name in ("Light", "Dark") + tuple(OVERLAY):
        owned |= set(k for k in themes[name] if not layout(k))
    for name in THEMES:
        missing = sorted(owned - set(themes[name]))
        check("%s lists every key another theme sets" % name, not missing,
              "%d of %d missing: %s" % (len(missing), len(owned), missing[:8]))


def run():
    try:
        own, themes = menus()
        check("the Themes menu has the two overlay themes",
              all(t in themes for t in OVERLAY), themes)
        check("and the presets beside it have neither",
              not any("verlay" in t for t in own), own)
        check("both are themes to the application",
              all(t in FreeCADGui.listThemes() for t in OVERLAY), FreeCADGui.listThemes())
        files()

        main = FreeCAD.ParamGet(PREFS + "MainWindow")
        editor = FreeCAD.ParamGet(PREFS + "Editor")
        tree = FreeCAD.ParamGet(PREFS + "TreeView")
        look = {}
        for theme in ("Light", "Dark"):
            if not check("%s applies" % theme, apply(theme)):
                return
            look[theme] = sheet()
            check("under %s the application has a style sheet" % theme, len(look[theme]) > 1000,
                  len(look[theme]))
        check("the tree starts outside any overlay", in_overlay("Tree view") is False,
              in_overlay("Tree view"))
        note("NOTE the two sheets differ: %s" % (look["Light"] != look["Dark"]))

        if not check("Overlay dark theme applies", apply("Overlay dark theme")):
            return
        check("it is the theme in use", main.GetString("Theme", "") == "Overlay dark theme",
              main.GetString("Theme", ""))
        check("the tree is in an overlay", in_overlay("Tree view") is True,
              in_overlay("Tree view"))
        check("and the application is styled exactly as under Dark", sheet() == look["Dark"],
              "%d characters against %d" % (len(sheet()), len(look["Dark"])))

        if not check("Light applies after it", apply("Light")):
            return
        light = listed("Light")
        line = editor.GetUnsigned("Current line highlight", 1)
        check("the editor's current line is Light's",
              line == int(light["Preferences/Editor/Current line highlight"]), "0x%08X" % line)
        check("no style parameters of another theme are named",
              main.GetString("ThemeStyleParametersFile", "") == "",
              main.GetString("ThemeStyleParametersFile", ""))
        check("the application is styled exactly as under Light applied first",
              sheet() == look["Light"],
              "%d characters against %d" % (len(sheet()), len(look["Light"])))
        check("and the layout is left alone: the tree is still in its overlay",
              in_overlay("Tree view") is True, in_overlay("Tree view"))

        if not check("Overlay light theme applies", apply("Overlay light theme")):
            return
        check("and is styled exactly as Light", sheet() == look["Light"],
              "%d characters against %d" % (len(sheet()), len(look["Light"])))
        if check("Dark applies after an overlay theme", apply("Dark")):
            check("styled exactly as Dark applied first", sheet() == look["Dark"],
                  "%d characters against %d" % (len(sheet()), len(look["Dark"])))
            check("the tree still in its overlay", in_overlay("Tree view") is True,
                  in_overlay("Tree view"))
            check("and styled for it, by the overlay sheet for see-through panels",
                  main.GetString("OverlayActiveStyleSheet", "") == "Dark-Outline.qss",
                  main.GetString("OverlayActiveStyleSheet", ""))

        py = console()
        if not check("the Python console is there", py is not None):
            return
        for theme in THEMES:
            editor.SetUnsigned("Background", CONSOLE_BACKGROUND)
            tree.SetUnsigned("ItemBackground", ITEM_BACKGROUND)
            settle(1.0)
            check("%s: before it, the console has the preset's background" % theme,
                  "#c8c8c8" in py.styleSheet(), py.styleSheet())
            if not check("%s: the theme applies" % theme, apply(theme)):
                continue
            want = listed(theme)
            ground = int(want["Preferences/Editor/Background"])
            item = int(want["Preferences/TreeView/ItemBackground"])
            styled = "Gui--PythonConsole {background: #%06x}" % (ground >> 8) if ground else ""
            check("%s: the console's background is the theme's" % theme,
                  editor.GetUnsigned("Background", 99) == ground and py.styleSheet() == styled,
                  (editor.GetUnsigned("Background", 99), py.styleSheet()))
            check("%s: the tree's item background is the theme's" % theme,
                  tree.GetUnsigned("ItemBackground", 99) == item,
                  tree.GetUnsigned("ItemBackground", 99))
            if ground:
                # the colour is written out in the theme: hold it to what the sheet draws
                fresh = QtWidgets.QPlainTextEdit(FreeCADGui.getMainWindow())
                fresh.ensurePolished()
                page = fresh.palette().color(QtGui.QPalette.Base).name()
                fresh.deleteLater()
                check("%s: and it is the page the theme's sheet draws an editor on" % theme,
                      page == "#%06x" % (ground >> 8), "%s, the key #%06x" % (page, ground >> 8))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
