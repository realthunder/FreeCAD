"""The Dot line pattern is two pixels on, two off; a stored old Dot is read as it.

docs/HandsOnQueue.md entry 126, the reporter 2026-10-14 17:04-17:08: at line
pattern scale 1 "the dots are far too dense to distinguish", "the dash looks
ok". The Dot choice was the mask 0xaaaa, one pixel on and one off, which
anti-aliasing blurs into a grey line. The reporter chose 2026-10-14: two on,
two off (0xcccc), with a stored 0xaaaa read as the new Dot.

Run with GT_SEED_CFG naming a user.cfg that stores View/SelectionLinePattern
= 43690 (0xaaaa). Claims:
  - after the start the setting reads 52428 (0xcccc);
  - the "Selected hidden line pattern" combo of the preferences has its Dot
    item at 0xcccc, no item at 0xaaaa, and shows Dot as the current choice.

Runs with QT_QPA_PLATFORM=offscreen; the pointer is not moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def finish():
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def look_at_dialog():
    try:
        dlg = None
        for w in QtWidgets.QApplication.topLevelWidgets():
            if w.metaObject().className() == "Gui::Dialog::DlgPreferencesImp" and w.isVisible():
                dlg = w
        if not check("the preferences are open", dlg is not None):
            return
        combos = [
            w
            for w in dlg.findChildren(QtWidgets.QComboBox)
            if w.metaObject().className() == "Gui::PrefLinePattern"
        ]
        if not check("a line pattern combo is on the page", len(combos) > 0, len(combos)):
            return
        combo = combos[0]
        items = [(combo.itemText(i), combo.itemData(i)) for i in range(combo.count())]
        note("INFO items: %s" % items)
        dot = [d for t, d in items if t == "Dot"]
        check("its Dot is 0xcccc", dot == [0xCCCC], dot)
        check("no item is 0xaaaa", all(d != 0xAAAA for t, d in items))
        check("the stored old Dot shows as Dot", combo.currentText() == "Dot",
              combo.currentText())
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        # close(), not reject(): nothing was touched, and reject() can ask
        # to revert on the user's screen
        for w in QtWidgets.QApplication.topLevelWidgets():
            if w.metaObject().className() == "Gui::Dialog::DlgPreferencesImp":
                w.close()
        QtCore.QTimer.singleShot(500, finish)


def run():
    try:
        hgrp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        value = hgrp.GetInt("SelectionLinePattern", 0)
        check("the stored 0xaaaa reads 0xcccc after the start", value == 0xCCCC, hex(value))
        QtCore.QTimer.singleShot(2500, look_at_dialog)
        # Display > Display styles, the sixth page of the group (resource.cpp)
        FreeCADGui.showPreferences("Display", 5)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


QtCore.QTimer.singleShot(3000, run)
