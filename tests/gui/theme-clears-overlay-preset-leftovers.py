"""A theme takes away what an overlay preset left behind.

docs/HandsOnQueue.md entry 30, its third task: "add python console
stylesheet setting to dark and light theme. right now it seem to only
appear in overlay theme, which once applied there is no way to un-apply it
even switching to classic theme". The presets "Overlay dark theme" and
"Overlay light theme" (Tools > Preset configurations) write colours no
theme listed: the Python console's background (Editor/Background), the
tree's item background (TreeView/ItemBackground) and, for the Light theme,
the 3D cursor's cross-hair colour. A theme only writes the keys it lists,
so those stayed whatever theme came next.

Claims:
  - every colour an overlay preset writes under Preferences is a key that
    Light, Dark and Classic each list;
  - with the dark overlay preset's console background and tree item
    background in place (the console really takes the background), each of
    the three themes puts both back to none, and the console has no
    background of its own left;
  - under Light the cross-hair is dark, as the light 3D background wants.
"""
import os
import time
import traceback
import xml.etree.ElementTree as ET

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
THEMES = ("Light", "Dark", "Classic")
CONSOLE_BACKGROUND = 3368601600  # the dark overlay preset's, 0xC8C8C800
ITEM_BACKGROUND = 20


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


def colours(path):
    """The colour keys (FCUInt) a parameter file has under Preferences, as group/key"""
    found = set()

    def walk(node, trail):
        for child in node:
            if child.tag == "FCParamGroup":
                walk(child, trail + [child.get("Name")])
            elif child.tag == "FCUInt" and "Preferences" in trail:
                tail = trail[trail.index("Preferences") + 1:]
                found.add("/".join(tail + [child.get("Name")]))

    walk(ET.parse(path).getroot(), [])
    return found


def console():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::PythonConsole":
            return w
    return None


def run():
    try:
        res = FreeCAD.getResourceDir()
        preset = set()
        for name in ("OverlayDark.FCParam", "OverlayLight.FCParam"):
            preset |= colours(os.path.join(res, "settings", name))
        check("the overlay presets write colours", len(preset) >= 6, sorted(preset))
        for theme in THEMES:
            listed = colours(os.path.join(res, "Gui", "PreferencePacks", theme, theme + ".cfg"))
            check("%s lists every colour an overlay preset writes" % theme, preset <= listed,
                  "not listed: %s" % sorted(preset - listed))

        editor = FreeCAD.ParamGet(PREFS + "Editor")
        tree = FreeCAD.ParamGet(PREFS + "TreeView")
        view = FreeCAD.ParamGet(PREFS + "View")
        py = console()
        if not check("the Python console is there", py is not None):
            return
        for theme in THEMES:
            editor.SetUnsigned("Background", CONSOLE_BACKGROUND)
            tree.SetUnsigned("ItemBackground", ITEM_BACKGROUND)
            settle(1.0)
            check("%s: before it, the console has the preset's background" % theme,
                  "background" in py.styleSheet(), py.styleSheet())
            if not check("%s: the theme applies" % theme, FreeCADGui.applyTheme(theme)):
                continue
            settle(2.5)
            check("%s: the console's background is back to none" % theme,
                  editor.GetUnsigned("Background", 99) == 0 and py.styleSheet() == "",
                  (editor.GetUnsigned("Background", 99), py.styleSheet()))
            check("%s: the tree's item background is back to none" % theme,
                  tree.GetUnsigned("ItemBackground", 99) == 0, tree.GetUnsigned("ItemBackground", 99))
            if theme == "Light":
                cross = view.GetUnsigned("CursorCrosshairColor", 0xFFFFFFFF)
                r, g, b = (cross >> 24) & 255, (cross >> 16) & 255, (cross >> 8) & 255
                check("Light: the cross-hair is dark", max(r, g, b) < 128, "0x%08X" % cross)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
