"""Closing the preferences dialog does not freeze the program.

docs/HandsOnQueue.md entry 66: "first time preference dialog loads slow. and
if I then choose 'reset all' it freeze for several 10s of seconds."

The freeze was not the reset. Every file chooser of the dialog -- the line
with a "..." button beside it, eleven of them before a module adds its pages
-- made a QFileSystemModel of its own for the completion of what is typed
into it, and such a model has a thread that reads the file system. The
dialog's destruction waited for each of those threads, a second apiece while
they were still at work, and then for the ones that had not stopped: 11 to
59 seconds measured, whenever the dialog was destroyed within about half a
minute of being built. "Reset all" closes the dialog; so do OK and Cancel.

A chooser's line now gets its completer when it first has the focus, and
all of them share one model that lives as long as the application.

Claims, on a session with no document:
  - the dialog has file choosers (the test stands on what it means to test);
  - opening it does not start a thread for each chooser: fewer than half
    as many as it has choosers (it started ten for its eleven);
  - a chooser's line has no completer before it has had the focus, and has
    one after, on a model of the file system; a second chooser's is on the
    same model;
  - closed a second after it was opened, the dialog is gone and the event
    loop answers again within 2 seconds (11 to 17 before, on this profile);
  - opened again and "Reset all" chosen at once, the same: 3 seconds for
    the reset and the close together.
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
SEEN = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def held(seconds):
    """Spin the event loop for `seconds`, longer while it is busy: the sum of
    the turns that took over 50 ms, and the longest"""
    end = time.perf_counter() + seconds
    longest = busy = 0.0
    while time.perf_counter() < end:
        t = time.perf_counter()
        QtCore.QCoreApplication.processEvents()
        d = time.perf_counter() - t
        longest = max(longest, d)
        if d > 0.05:
            busy += d
            end = max(end, time.perf_counter() + 1.5)
        time.sleep(0.005)
    return busy, longest


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG),
                ("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]


def threads():
    """The threads of this process; -1 where that cannot be asked"""
    if sys.platform != "win32":
        try:
            return len(os.listdir("/proc/self/task"))
        except OSError:
            return -1
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    snap = k32.CreateToolhelp32Snapshot(0x4, 0)
    if not snap or snap == wt.HANDLE(-1).value:
        return -1
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    count = 0
    pid = os.getpid()
    more = k32.Thread32First(wt.HANDLE(snap), ctypes.byref(entry))
    while more:
        if entry.th32OwnerProcessID == pid:
            count += 1
        more = k32.Thread32Next(wt.HANDLE(snap), ctypes.byref(entry))
    k32.CloseHandle(wt.HANDLE(snap))
    return count


def dialog():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.metaObject().className() == "Gui::Dialog::DlgPreferencesImp" and w.isVisible():
            return w
    return None


def on_screen(kind):
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, kind) and w.isVisible():
            return w
    return None


def choosers(dlg):
    return [w for w in dlg.findChildren(QtWidgets.QWidget) if w.inherits("Gui::FileChooser")]


def first_open():
    held(1.0)
    before = threads()
    FreeCADGui.runCommand("Std_DlgPreferences")
    QtCore.QCoreApplication.processEvents()
    dlg = dialog()
    if dlg is None:
        raise RuntimeError("no preferences dialog")
    found = choosers(dlg)
    after = threads()
    SEEN["choosers"] = len(found)
    if not check("the dialog has file choosers", len(found) >= 5, len(found)):
        raise RuntimeError("nothing to test")
    if before < 0 or after < 0:
        note("NOTE the threads of the process cannot be counted here")
    else:
        check("opening the dialog starts no thread for each of its file choosers",
              after - before < len(found) / 2.0,
              "%d choosers; %d threads before, %d after" % (len(found), before, after))
    lines = [c.findChild(QtWidgets.QLineEdit) for c in found[:2]]
    check("a chooser's line has no completer before it has had the focus",
          all(line is not None and line.completer() is None for line in lines))
    for line in lines:
        QtWidgets.QApplication.sendEvent(
            line, QtGui.QFocusEvent(QtCore.QEvent.FocusIn, QtCore.Qt.TabFocusReason))
    models = [line.completer().model() if line.completer() else None for line in lines]
    check("it has one after, on a model of the file system",
          all(m is not None and m.inherits("QFileSystemModel") for m in models),
          [m.metaObject().className() if m else None for m in models])
    check("two choosers complete from the same model",
          models[0] is not None and models[0] == models[1])


def first_close():
    dlg = dialog()
    t0 = time.perf_counter()
    dlg.close()
    busy, longest = held(1.5)
    check("closed a second after it was opened, the dialog is gone", dialog() is None)
    check("and the event loop answers again within 2 seconds", busy < 2.0,
          "held %.2f s after the close (the longest turn %.2f s), %.2f s in all" % (
              busy, longest, time.perf_counter() - t0))


def reopen():
    FreeCADGui.runCommand("Std_DlgPreferences")
    QtCore.QCoreApplication.processEvents()
    if dialog() is None:
        raise RuntimeError("no preferences dialog the second time")


def reset_all():
    def answer_yes():
        box = on_screen(QtWidgets.QMessageBox)
        if box is None:
            note("FAIL the reset asked its question")
            return
        SEEN["yes"] = time.perf_counter()
        box.button(QtWidgets.QMessageBox.Yes).click()

    def pick():
        menu = on_screen(QtWidgets.QMenu)
        if menu is None:
            note("FAIL the Reset button opened its menu")
            return
        for act in menu.actions():
            if act.text().replace("&", "").startswith("Reset all"):
                QtCore.QTimer.singleShot(500, answer_yes)
                menu.setActiveAction(act)
                QTest.keyClick(menu, QtCore.Qt.Key_Return)
                return
        note("FAIL the menu has 'Reset all'")
        menu.close()

    dlg = dialog()
    button = dlg.findChild(QtWidgets.QPushButton, "buttonReset")
    QtCore.QTimer.singleShot(500, pick)
    button.click()
    done = time.perf_counter()
    busy, longest = held(1.5)
    if not check("the reset was carried out and closed the dialog",
                 "yes" in SEEN and dialog() is None):
        return
    took = done - SEEN["yes"]
    check("\"Reset all\" chosen at once: the reset and the close take under 3 seconds",
          took + busy < 3.0,
          "%.2f s from the Yes to the return of the click, the event loop held %.2f s after "
          "(the longest turn %.2f s)" % (took, busy, longest))


def finish():
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("FAIL the step %s ran | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((3000, first_open))
STEPS.append((1000, first_close))
STEPS.append((1000, reopen))
STEPS.append((500, reset_all))
advance()
