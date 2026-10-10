"""The unit settings are listed, and a change of one is in force when it is made.

The keys of Preferences/Units are behind App::UnitsParams
(docs/HandsOnQueue.md entry 24). They used to be read at fixed moments --
the start, a document becoming active, OK on the General page -- so a
change made anywhere else did nothing until one of those came round: the
status bar's unit button showed the new unit system while quantities were
still formatted in the old one, and the number of decimals waited for the
preferences to be confirmed. App puts the number of decimals and the inch
fraction in force when they change, Gui the unit system (the active
document's own when it is a saved one and documents are not told to follow
the preference, the preference otherwise).

Claims, with no document open:

  - "/param unit system" lists the unit system, and "/param number of
    decimals" the number of decimals;
  - the unit system set to 6 is the one in force at once, and the default
    again when the key is removed;
  - the number of decimals set to 5 shows in a quantity's text at once;
  - the inch fraction set to 32 is a new quantity's denominator at once.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
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


def param_rows(query):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(0.6)
    edit = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == "OmniSearchEdit" and w.isVisible():
            edit = w
    if edit is None:
        return []
    text = "/param " + query
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    rows = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if not isinstance(w, QtWidgets.QAbstractItemView) or w.model() is None:
            continue
        found = [str(w.model().index(i, 0).data()) for i in range(w.model().rowCount())]
        # The list is not shown while another application is in front, which a
        # test cannot prevent on a desktop in use; its rows are there all the same.
        if w.isVisible() or any(r.startswith("Preferences/") for r in found):
            rows += found
    QtWidgets.QApplication.sendEvent(edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)
    return rows


def one_mm():
    return FreeCAD.Units.Quantity(1.0, FreeCAD.Units.Length).UserString


def run():
    units = FreeCAD.ParamGet(PREFS + "Units")
    try:
        rows = param_rows("unit system")
        check("the omni search lists the unit system", any(r.endswith("Units/UserSchema") for r in rows), rows[:6])
        rows = param_rows("number of decimals")
        check("and the number of decimals", any(r.endswith("Units/Decimals") for r in rows), rows[:6])

        before = FreeCAD.Units.getSchema()
        units.SetInt("UserSchema", 6)
        settle(0.3)
        check("the unit system set to 6 is in force at once", before == 0 and FreeCAD.Units.getSchema() == 6,
              (before, FreeCAD.Units.getSchema()))
        units.RemInt("UserSchema")
        settle(0.3)
        check("and the default again when the key is removed", FreeCAD.Units.getSchema() == 0,
              FreeCAD.Units.getSchema())

        text = one_mm()
        units.SetInt("Decimals", 5)
        settle(0.3)
        check("five decimals show in a quantity's text at once", "1.00000" in one_mm().replace(",", "."),
              (text, one_mm()))
        units.RemInt("Decimals")
        settle(0.3)
        check("and the default number again when the key is removed", one_mm() == text, (text, one_mm()))

        denominator = FreeCAD.Units.Quantity("1 mm").Format["Denominator"]
        units.SetInt("FracInch", 32)
        settle(0.3)
        now = FreeCAD.Units.Quantity("1 mm").Format["Denominator"]
        check("an inch fraction of 32 is a new quantity's denominator at once", denominator == 8 and now == 32,
              (denominator, now))
        units.RemInt("FracInch")
        settle(0.3)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        units.RemInt("UserSchema")
        units.RemInt("Decimals")
        units.RemInt("FracInch")
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
