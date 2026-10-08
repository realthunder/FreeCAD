"""The 3D view's settings are listed, and a setting that is not stored is its default to every reader.

Sixty-one more keys of Preferences/View are behind ViewParams
(docs/HandsOnQueue.md entry 24): the 3D view's display, background and
lights, navigation, and a few others. They were read at about a hundred
places, each with a default of its own, and not always the same one:

  - the zoom step was 0.2 where a view is made and 0 where a view is told
    that the key changed. A zoom step that was stored and then removed --
    "back to the default" -- left the open views with a step of 0: the
    mouse wheel and Zoom In did nothing until the next start;
  - the Home view reads an unset camera orientation as Top, where a new
    document opens in Trimetric and the Navigation page shows Trimetric.
    That is upstream's, on purpose, and stays (it was made Trimetric here
    for a day; docs/HandsOnLog.md entry 24, decision A3).

Every reader takes the default from the class now. The preference pages'
defaults are held to the definitions by preferences-ok-keeps-defaults.py.

Claims:

  - "/param background colour", "/param zoom step" and "/param navigation
    style" list the settings of those names;
  - with the zoom step stored and then removed, Zoom In still zooms the
    open view;
  - on a profile that stores no camera orientation, Home shows the model as
    a new document does.

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


def camera_height(view):
    return float(view.getCameraNode().height.getValue())


def orientation(view):
    return tuple(round(v, 4) for v in view.getCameraOrientation().Q)


def zoom_in():
    """One step of the navigation style's zoom, as the mouse wheel and the Zoom In command make it."""
    FreeCADGui.runCommand("Std_ViewZoomIn")
    settle(0.5)


def run():
    group = FreeCAD.ParamGet(PREFS + "View")
    doc = None
    try:
        rows = param_rows("background colour")
        check("the omni search lists the background colour", any(r.endswith("View/BackgroundColor") for r in rows),
              rows[:6])
        rows = param_rows("zoom step")
        check("the zoom step", any(r.endswith("View/ZoomStep") for r in rows), rows[:6])
        rows = param_rows("navigation style")
        check("and the navigation style", any(r.endswith("View/NavigationStyle") for r in rows), rows[:6])

        doc = FreeCAD.newDocument("Entry24View")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(1.0)
        view = FreeCADGui.ActiveDocument.ActiveView
        view.setCameraType("Orthographic")
        view.fitAll()
        settle(0.5)

        before = camera_height(view)
        zoom_in()
        moved = camera_height(view)
        if check("Zoom In zooms the view by a step", abs(moved - before) > 1e-6 * before, (before, moved)):
            group.SetFloat("ZoomStep", 0.3)
            settle(0.3)
            group.RemFloat("ZoomStep")
            settle(0.3)
            before = camera_height(view)
            zoom_in()
            moved = camera_height(view)
            check("and still does after a zoom step was stored and removed again",
                  abs(moved - before) > 1e-6 * before, (before, moved))

        view.viewTop()
        settle(0.5)
        top = orientation(view)
        view.viewDefaultOrientation()
        settle(0.5)
        new_document = orientation(view)
        view.viewFront()
        settle(0.5)
        front = orientation(view)
        FreeCADGui.runCommand("Std_ViewHome")
        settle(1.0)
        check("with no camera orientation stored, Home is Top and a new document opens otherwise, as upstream",
              orientation(view) == top and new_document != top and front != top,
              (orientation(view), new_document, top))
        group.SetString("NewDocumentCameraOrientation", "Front")
        settle(0.3)
        FreeCADGui.runCommand("Std_ViewHome")
        settle(1.0)
        check("and once an orientation is stored Home takes it", orientation(view) == front,
              (orientation(view), front))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        group.RemFloat("ZoomStep")
        group.RemString("NewDocumentCameraOrientation")
        if doc is not None:
            FreeCAD.closeDocument(doc.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
