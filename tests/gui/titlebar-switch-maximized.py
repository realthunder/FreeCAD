"""Switching the custom title bar on a maximized window leaves it in one place.

The custom title bar can be switched at run time -- a theme or a
preference pack does it, and so does Std_ViewTitleBar. On Windows the
switch changes the window's frame, and done in place on a maximized window
it left two accounts of where the client area is: Windows had it on the
work area and Qt 8 px away and 16 px wider. Seen by hand as a title bar
whose menu does not open under the pointer and, "sometimes", no margin at
the top; the window buttons hung off the right edge, and the normal
placement the window later returned to had moved above the screen, further
with every switch.

Claims, with the window maximized and the custom title bar on:

  - as started, Qt's geometry is the available geometry (the instrument);
  - switched off and on again, it still is, and the title bar is at the
    window's top left corner;
  - on Windows, the client area the system reports is the same rectangle;
  - back in the normal state, the window is where it was before any switch;
  - after three more switches it still is, and maximizes to the same place.

Where the window cannot be maximized at all (a display with no window
manager) the test says so and claims nothing.

Scored against the tree before the change: see the commit message.
"""
import os
import sys
import time
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


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


class NothingToJudge(Exception):
    """The display cannot show what the claims are about."""


def native_client(mw):
    """The client rectangle as Windows has it, in screen coordinates."""
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    hwnd = int(mw.winId())
    rect = wintypes.RECT()
    origin = wintypes.POINT(0, 0)
    user32.GetClientRect(hwnd, ctypes.byref(rect))
    user32.ClientToScreen(hwnd, ctypes.byref(origin))
    return (origin.x, origin.y, rect.right, rect.bottom)


def title_bar():
    for w in QtWidgets.QApplication.allWidgets():
        if w.metaObject().className() == "TitleBarWidget" and w.isVisible():
            return w
    return None


def run():
    params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow")
    try:
        mw = FreeCADGui.getMainWindow()

        def switch(on):
            params.SetBool("CustomTitleBar", on)
            settle(1.0)

        def geometry():
            return mw.geometry().getRect()

        switch(True)
        mw.showNormal()
        settle(0.5)
        mw.resize(900, 600)
        mw.move(120, 90)
        settle(0.5)
        normal = geometry()
        mw.showMaximized()
        settle(1.0)
        available = mw.screen().availableGeometry().getRect()
        if not mw.isMaximized() or geometry() != available:
            note("NOTE the window does not maximize to the available geometry here: %s of %s"
                 % (geometry(), available))
            raise NothingToJudge()
        check("as started, Qt's geometry is the available geometry", True, geometry())

        switch(False)
        switch(True)
        bar = title_bar()
        corner = bar.mapToGlobal(QtCore.QPoint(0, 0)).toTuple() if bar else None
        check("switched off and on again, it still is", geometry() == available, geometry())
        check("and the title bar is at the window's top left corner",
              corner == available[:2] and bar.width() == available[2], (corner, bar and bar.width()))
        if sys.platform == "win32":
            check("the client area the system reports is the same rectangle",
                  native_client(mw) == geometry(), (native_client(mw), geometry()))

        mw.showNormal()
        settle(0.8)
        check("back in the normal state, the window is where it was before any switch",
              geometry() == normal, (geometry(), normal))

        mw.showMaximized()
        settle(0.8)
        for _ in range(3):
            switch(False)
            switch(True)
        check("after three more switches it maximizes to the same place",
              mw.isMaximized() and geometry() == available, geometry())
        mw.showNormal()
        settle(0.8)
        check("and returns to the same normal placement", geometry() == normal,
              (geometry(), normal))
    except NothingToJudge:
        pass
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(3000, run)
