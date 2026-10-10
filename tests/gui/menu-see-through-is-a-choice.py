"""A menu is see-through only when a menu sheet was chosen.

The menus that pop up over the 3D view -- the pick menus, the command
history, select-up, the tool bar menu, the camera binding -- take the "menu
style sheet" of the Theme preferences. With none chosen they took a
see-through sheet whenever a theme with a stylesheet was active, and
because the same menu object also hangs in the main menu, Tools > Command
history came up see-through there, its text in the palette's bright-text
colour (the desktop's accent, blue, under a dark scheme). Asked by hand
(docs/HandsOnQueue.md entry 32): find out why, and see-through menus off
unless asked for.

Claims, with the Dark theme applied and no menu sheet chosen:

  - Tools > Command history exists as a sub menu;
  - about to be shown, it is not see-through, and its own sheet sets no
    background;
  - with Dark.qss chosen as the menu sheet it is see-through, and its text
    is not the palette's bright text;
  - with the choice taken away again it is not.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
MAIN = "User parameter:BaseApp/Preferences/MainWindow"
Qt = QtCore.Qt


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


def history_menu():
    """The menu of the main menu's "Command history" entry, and the entries looked at."""
    seen = []
    for widget in QtWidgets.QApplication.allWidgets():
        if not isinstance(widget, (QtWidgets.QMenu, QtWidgets.QMenuBar)):
            continue
        for action in widget.actions():
            text = action.text().replace("&", "")
            if action.menu() is None:
                continue
            seen.append(text)
            if text.lower().startswith("command history"):
                return action.menu(), seen
    return None, seen


def state(menu):
    menu.aboutToShow.emit()
    settle(0.3)
    return menu.testAttribute(Qt.WA_TranslucentBackground), "background" in menu.styleSheet()


def run():
    try:
        themes = FreeCADGui.listThemes()
        dark = [t for t in themes if t == "Dark"] or [t for t in themes if "dark" in t.lower()]
        if not check("there is a dark theme to apply", bool(dark), themes):
            return
        FreeCADGui.applyTheme(dark[0])
        settle(2.0)
        main = FreeCAD.ParamGet(MAIN)
        note("stylesheet %r, menu sheet %r; the palette's bright text is %s, its text %s" % (
            main.GetString("StyleSheet", ""), main.GetString("MenuStyleSheet", ""),
            QtWidgets.QApplication.palette().brightText().color().name(),
            QtWidgets.QApplication.palette().text().color().name()))
        menu, seen = history_menu()
        if not check("Tools > Command history is a sub menu", menu is not None,
                     "sub menus found: %s" % sorted(set(seen))[:80]):
            return
        through, background = state(menu)
        check("with no menu sheet chosen it is not see-through", not through)
        check("and its own sheet sets no background", not background,
              " ".join(menu.styleSheet().split())[:120])

        main.SetString("MenuStyleSheet", "Dark.qss")
        through, background = state(menu)
        check("with Dark.qss chosen it is see-through", through and background)
        check("and its text is not the palette's bright text", "bright-text" not in menu.styleSheet())

        main.SetString("MenuStyleSheet", "")
        through, background = state(menu)
        check("with the choice taken away it is not", not through and not background)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
