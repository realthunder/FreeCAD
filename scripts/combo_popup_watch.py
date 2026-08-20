"""Watch what a combo box popup does when it is dismissed by a real click.

The reported defect -- pick an entry in a Preferences combo box and the
drop-down stays on screen until the dialog loses focus -- cannot be driven from
a script here. Synthetic events never take the popup grab (the menu probes of
2026-08-10 found the same), and a popup driven by sendEvent() dismisses
correctly in both xvfb and WSLg, so the failing state is only entered by a real
pointer.

So this does not drive anything. It opens the Preferences dialog and then just
watches, 20 times a second, and writes a line whenever anything about a combo
box popup changes. The user clicks; the log says what happened.

What the log separates, which is the whole point:

  qt=1   the popup widget still considers itself shown -- hidePopup() either
         did not run or did not take, and the popup is live and grabbing.
  qt=0 plat=1
         Qt hid it and the platform window is still mapped -- the popup is a
         ghost, nothing is grabbing, and the bug is in the compositor handoff
         rather than in any handler.

Also logged: the application's idea of the active popup, the combo's index (so
the line can be tied to the selection that produced it), and the widget that
holds the mouse grab.

What it found, 2026-08-19: at the moment the popup is visibly stuck, the
reading is `qt=0 plat=0 activePopup=- grab=-`. The popup is gone at every
level Qt can see; what remains is an image nobody cleared. A bare PySide6
dialog with one QComboBox ghosts the same way, and the same FreeCAD build on
QT_QPA_PLATFORM=xcb does not, so the defect belongs to the Qt Wayland plugin
or WSLg's weston, not to FreeCAD. See docs/DevEnvironment.md.

Usage: FreeCAD scripts/combo_popup_watch.py
       then click a preference page combo box and pick an entry.
Env:   COMBO_WATCH_LOG   line log (default /tmp/combo-popup-watch.log)
"""
import os
import time

import shiboken6
from PySide import QtCore, QtWidgets

import FreeCADGui

LOG = os.environ.get("COMBO_WATCH_LOG", "/tmp/combo-popup-watch.log")

_start = time.time()
_last = {}


def write(line):
    with open(LOG, "a") as handle:
        handle.write("%7.2f  %s\n" % (time.time() - _start, line))


def state_of(combo):
    view = combo.view()
    if view is None:
        return None
    container = view.window()
    if container is None:
        return None
    handle = container.windowHandle()
    return (
        1 if container.isVisible() else 0,
        1 if (handle is not None and handle.isVisible()) else 0,
        combo.currentIndex(),
    )


def poll():
    active = QtWidgets.QApplication.activePopupWidget()
    grabber = QtWidgets.QWidget.mouseGrabber()
    for combo in QtWidgets.QApplication.allWidgets():
        if not isinstance(combo, QtWidgets.QComboBox):
            continue
        state = state_of(combo)
        if state is None:
            continue
        # id() is the Python wrapper's, and allWidgets() hands out a new
        # wrapper every poll; the C++ address is what stays put.
        key = shiboken6.getCppPointer(combo)[0]
        if _last.get(key) == state:
            continue
        was = _last.get(key)
        _last[key] = state
        # Only a popup that is up, or has just come down, says anything.
        if was is None or not (state[0] or state[1] or was[0] or was[1]):
            continue
        write("%-24s qt=%d plat=%d index=%d  activePopup=%s grab=%s"
              % (combo.objectName() or combo.metaObject().className(),
                 state[0], state[1], state[2],
                 active.metaObject().className() if active else "-",
                 grabber.metaObject().className() if grabber else "-"))


def start():
    write("watching -- open a preference page combo and pick an entry")
    timer = QtCore.QTimer(FreeCADGui.getMainWindow())
    timer.timeout.connect(poll)
    timer.start(50)
    globals()["_timer"] = timer
    FreeCADGui.showPreferences("General", 0)
    write("preferences dialog closed")


QtCore.QTimer.singleShot(4000, start)
