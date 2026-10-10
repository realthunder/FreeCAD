"""The overlay preset is the layout alone, its colours are the theme's, and no
theme leaves a piece of another behind.

docs/HandsOnQueue.md entry 30. "switching in combinations got some setting
stuck at a bad combination, such as the python editor colors": the presets
"Overlay dark theme" and "Overlay light theme" were a layout WITH colours
and style sheets of their own, written over whatever theme was there -- a
light grey console under the Dark theme's light text was one result. They
were made two themes first (2026-10-10 midday), and then, the same day: "I
have second thought. Can you make one overlay preset that can auto change
its color to match the main theme, so the overlay preset only change the
overlay pattern and their colors, like before".

So there is ONE preset, "Overlay" (Tools > Preset configurations): where the
panels are, and nothing else. What an overlay looks like is the theme's:
Dark and Light name the overlay sheet made for see-through panels ("color
shall follow the Dark/Light theme"), carry the backing behind a tree item,
and give the console the page their sheet draws an editor on ("need to
modify python editor part to make it more suitable to the theme"). Classic
names the dark overlay sheet: its window is light but its 3D view, which an
overlay lies over, is dark, and left to the colour scheme in effect the
overlay took the light sheet, dark text on dark blue.

And a theme writes the keys it lists and no others, so a key only one theme
listed stayed that theme's under the next: the editor's current line stayed
the Dark theme's dark blue under Light.

Claims:
  - the presets menu has "Overlay" and neither of the two old presets; its
    Themes menu has Classic, Dark and Light;
  - the preset's file lists the layout and nothing else: no colour, no
    style sheet;
  - every key Light or Dark lists is listed by all three themes;
  - under Dark, the preset applied through its menu: the tree is in an
    overlay; the theme is still Dark, not marked as changed by it, and the
    application styled exactly as before; the overlay's sheet is Dark's
    choice;
  - Light applied after it: the panels stay where they are, and the
    application, the overlay's sheet, the console's background and the
    editor's current line are Light's -- the overlay has followed;
  - Classic after that: the overlay's sheet is the dark one;
  - with the old preset's console background and a foreign tree backing in
    place, each theme puts its own there, and the console's is the page the
    theme's sheet draws an editor on.
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
THEMES = ("Light", "Dark", "Classic")
PRESET = "Overlay"
CONSOLE_BACKGROUND = 3368601600  # what the old dark overlay preset wrote, 0xC8C8C800
ITEM_BACKGROUND = 0x12345678  # no theme's


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


def keys_of(path):
    """The keys a parameter file lists, group/key from under BaseApp, with their values"""
    found = {}

    def walk(node, trail):
        for child in node:
            if child.tag == "FCParamGroup":
                walk(child, trail + [child.get("Name")])
            elif child.get("Name") is not None and len(trail) >= 2:
                value = child.get("Value")
                found["/".join(trail[2:] + [child.get("Name")])] = (
                    child.text or "" if value is None else value)

    walk(ET.parse(path).getroot(), [])
    return found


def listed(theme):
    return keys_of(os.path.join(FreeCAD.getResourceDir(), "Gui", "PreferencePacks", theme,
                                theme + ".cfg"))


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
    """The presets menu as it would open: its own entries (text -> action), and Themes'"""
    action = FreeCADGui.Command.get("Std_CmdPresets").getAction()[0]
    menu = action.menu()
    menu.aboutToShow.emit()
    own, themes = {}, []
    for a in menu.actions():
        if a.menu() is not None and a.text().replace("&", "") == "Themes":
            themes = [b.text().replace("&", "") for b in a.menu().actions()]
        elif not a.isSeparator() and a.menu() is None:
            own[a.text().replace("&", "")] = a
    return own, themes


def sheet():
    return QtWidgets.QApplication.instance().styleSheet()


def apply(theme):
    ok = FreeCADGui.applyTheme(theme)
    settle(3.5)
    return ok


def files():
    preset = keys_of(os.path.join(FreeCAD.getResourceDir(), "settings", "Overlay.FCParam"))
    other = sorted(k for k in preset if not layout(k))
    check("the preset's file lists the layout and nothing else",
          len(preset) >= 30 and not other, "%d keys; not layout: %s" % (len(preset), other))
    themes = dict((t, listed(t)) for t in THEMES)
    owned = set(themes["Light"]) | set(themes["Dark"])
    for name in THEMES:
        missing = sorted(owned - set(themes[name]))
        check("%s lists every key another theme sets" % name, not missing,
              "%d of %d missing: %s" % (len(missing), len(owned), missing[:8]))
        check("%s lists nothing of the layout" % name,
              not [k for k in themes[name] if layout(k)], [k for k in themes[name] if layout(k)])


def run():
    try:
        own, themes = menus()
        check("the presets menu has the one overlay preset", PRESET in own, sorted(own))
        check("and neither of the two old ones",
              not [t for t in own if "verlay" in t and t != PRESET], sorted(own))
        check("its Themes menu has Classic, Dark and Light",
              themes == ["Classic", "Dark", "Light"], themes)
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
        dark, light = listed("Dark"), listed("Light")

        # The preset, under Dark, as its menu applies it.
        own[PRESET].trigger()
        settle(4.0)
        check("the preset applied: the tree is in an overlay", in_overlay("Tree view") is True,
              in_overlay("Tree view"))
        check("the theme is still Dark", main.GetString("Theme", "") == "Dark",
              main.GetString("Theme", ""))
        check("and the application is styled exactly as before", sheet() == look["Dark"],
              "%d characters against %d" % (len(sheet()), len(look["Dark"])))
        check("the overlay's sheet is Dark's choice",
              main.GetString("OverlayActiveStyleSheet", "?")
              == dark["Preferences/MainWindow/OverlayActiveStyleSheet"] == "Dark-Outline.qss",
              main.GetString("OverlayActiveStyleSheet", "?"))
        check("the console's background is Dark's",
              editor.GetUnsigned("Background", 1) == int(dark["Preferences/Editor/Background"]),
              "0x%08X" % editor.GetUnsigned("Background", 1))

        # Light after it: the overlay follows.
        if not check("Light applies with the panels in their overlays", apply("Light")):
            return
        check("the panels stay where they are", in_overlay("Tree view") is True,
              in_overlay("Tree view"))
        check("the application is styled exactly as under Light before", sheet() == look["Light"],
              "%d characters against %d" % (len(sheet()), len(look["Light"])))
        check("the overlay's sheet is Light's",
              main.GetString("OverlayActiveStyleSheet", "?") == "Light-Outline.qss",
              main.GetString("OverlayActiveStyleSheet", "?"))
        check("the console's background is Light's",
              editor.GetUnsigned("Background", 1) == int(light["Preferences/Editor/Background"]),
              "0x%08X" % editor.GetUnsigned("Background", 1))
        line = editor.GetUnsigned("Current line highlight", 1)
        check("the editor's current line is Light's",
              line == int(light["Preferences/Editor/Current line highlight"]), "0x%08X" % line)

        if check("Classic applies after that", apply("Classic")):
            check("the overlay's sheet is the dark one, for Classic's dark 3D view",
                  main.GetString("OverlayActiveStyleSheet", "") == "Dark-Outline.qss",
                  main.GetString("OverlayActiveStyleSheet", ""))
            check("and the panels stay where they are", in_overlay("Tree view") is True,
                  in_overlay("Tree view"))

        py = console()
        if not check("the Python console is there", py is not None):
            return
        for theme in THEMES:
            editor.SetUnsigned("Background", CONSOLE_BACKGROUND)
            tree.SetUnsigned("ItemBackground", ITEM_BACKGROUND)
            settle(1.0)
            check("%s: before it, the console has the old preset's background" % theme,
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
