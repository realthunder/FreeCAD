"""The settings and the state C++ read without a definition are listed by the omni search.

Entry 42 of docs/HandsOnQueue.md. After entry 24 about 300 keys that C++ reads
still had no definition: what the program keeps for itself (state), and
settings that entry had not reached. They go behind generated classes group
by group, and a claim is added here for each.

The 3D mouse, Gui::SpaceballParams. Its group is BaseApp/Spaceball/Motion,
not under Preferences, and its page -- Customize, Spaceball Motion -- only
shows its widgets while a device is present, so on most machines these
settings could not be seen at all:

  - "/param 3d mouse dominant" lists Spaceball/Motion/Dominant, and
    "/param spin sensitivity" Spaceball/Motion/SpinSensitivity;
  - "/param axis remapping" lists Spaceball/Motion/Remapping, which no page
    has, and "/param device model" Spaceball/Model, of the group above;
  - the calibration the program stores is listed too:
    Spaceball/Motion/CalibrationXr;
  - all 32 are in the registry once, each with a title and a documentation.

The expression sandbox, App::SandboxParams (Preferences/Expression/Sandbox
and Security). Only its switch could be reached from the interface:

  - "/param sandbox time budget" lists Expression/Sandbox/BudgetMs and
    "/param enforce document permissions" Expression/Security/Enforce;
  - the class follows its group at once: with Security/Enforce stored off,
    FreeCAD.ExpressionSecurity.enforced() is False, and True again with the
    key removed -- the reader asks the class now, where it read the group;
  - all 14 are in the registry once, each with a title and a documentation.

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
Qt = QtCore.Qt


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))


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
        if w.isVisible() or any("/" in r for r in found):
            rows += found
    QtWidgets.QApplication.sendEvent(
        edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier)
    )
    settle(0.5)
    return rows


def described(context):
    rows = FreeCAD.listParams() if hasattr(FreeCAD, "listParams") else []
    return [r for r in rows if r["context"] == context]


def spaceball():
    found = [param_rows(q) for q in ("3d mouse dominant", "spin sensitivity")]
    check("the omni search lists the 3D mouse's settings, with no device present",
          "Spaceball/Motion/Dominant" in found[0]
          and "Spaceball/Motion/SpinSensitivity" in found[1], [f[:3] for f in found])
    found = [param_rows(q) for q in ("axis remapping", "device model")]
    check("and the two no page of it shows",
          "Spaceball/Motion/Remapping" in found[0] and "Spaceball/Model" in found[1],
          [f[:3] for f in found])
    rows = param_rows("calibration tilt")
    check("and the calibration the program stores",
          "Spaceball/Motion/CalibrationXr" in rows, rows[:3])
    rows = described("SpaceballParams")
    keys = {(r["path"], r["entry"]) for r in rows}
    bare = [r["entry"] for r in rows if not r["title"] or not 0 < len(r["doc"]) <= 400]
    check("its 32 are in the registry once, each with a title and a documentation",
          len(rows) == 32 and len(keys) == 32 and not bare, (len(rows), len(keys), bare[:4]))


def sandbox():
    found = [param_rows(q) for q in ("sandbox time budget", "enforce document permissions")]
    check("the omni search lists the expression sandbox's settings",
          "Preferences/Expression/Sandbox/BudgetMs" in found[0]
          and "Preferences/Expression/Security/Enforce" in found[1], [f[:3] for f in found])
    security = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Expression/Security")
    had = "Enforce" in security.GetBools()
    old = security.GetBool("Enforce", True)
    try:
        security.SetBool("Enforce", False)
        off = FreeCAD.ExpressionSecurity.enforced()
        security.RemBool("Enforce")
        back = FreeCAD.ExpressionSecurity.enforced()
    finally:
        if had:
            security.SetBool("Enforce", old)
    check("and the class follows its group at once", off is False and back is True, (off, back))
    rows = described("SandboxParams")
    keys = {(r["path"], r["entry"]) for r in rows}
    bare = [r["entry"] for r in rows if not r["title"] or not 0 < len(r["doc"]) <= 400]
    check("its 14 are in the registry once, each with a title and a documentation",
          len(rows) == 14 and len(keys) == 14 and not bare, (len(rows), len(keys), bare[:4]))


def run():
    try:
        spaceball()
        sandbox()
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
