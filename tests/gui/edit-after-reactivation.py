"""An edit started after the main window comes back is still bound to
the 3D view.

A 3D view sits in a cell of a Gui::ViewArea, and it is the area that is
the MDI tab. The main window names the embedded view as the active one
everywhere but two places, and both handed out the area instead:

  - MainWindow::changeEvent, on the window being activated again -- a
    modal dialog closed, or the user came back from another program --
    recorded the tab's widget, the area;
  - Document::getActiveView, when the main window's active view belongs
    to another document, falls back on the document's last view, and the
    area is made after the view it holds.

Document::setEdit casts the active view to a 3D view. With the area it
got nothing, found the area again as "the view that shows the object",
and started the edit with no viewer: the sketch's task panel opened, the
view showed no edit, getInEdit() said None, and leaving logged "Object
not found" for a selection of nothing. Whether the first case shows
depends on where the keyboard focus is when the window comes back (a
focus change into the view puts the embedded view back); the symptoms
are those of tests/gui/sketch-new-in-group.py failing 3 runs of about
25 on its second New Sketch (docs/SplitViews.md sec 20).

Claims, for a sketch in an App::Part:
  - after the main window is activated again the document's active view
    is the 3D view, an edit started then is being edited, and leaving it
    selects the sketch;
  - the same with another document's view the active one.

Scored against the tree before the change: the active view is the area
both times, neither edit is being edited and neither selects the sketch.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "Reactivate"
OTHER = "ReactivateOther"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=20):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def is_3d(view):
    return view is not None and hasattr(view, "getCameraNode")


def edit_and_leave(tag, sketch):
    gdoc = FreeCADGui.getDocument(DOC)
    view = gdoc.ActiveView
    check("%s: the document's active view is the 3D view" % tag,
          is_3d(view), type(view).__name__)
    FreeCADGui.Selection.clearSelection()
    started = gdoc.setEdit(sketch)
    check("%s: the edit starts" % tag, bool(started))
    check("%s: it is being edited" % tag, gdoc.getInEdit() is not None)
    settle()
    check("%s: it is still being edited after the events" % tag,
          gdoc.getInEdit() is not None)
    gdoc.resetEdit()
    settle()
    selected = [o.Name for o in FreeCADGui.Selection.getSelection(DOC)]
    check("%s: leaving selects the sketch" % tag,
          selected == [sketch.Name], selected)


def run():
    try:
        mw = FreeCADGui.getMainWindow()
        doc = FreeCAD.newDocument(DOC)
        part = doc.addObject("App::Part", "Holder")
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        part.addObject(sketch)
        doc.recompute()
        settle()

        # What QApplication sends the main window when it becomes the
        # active one again. Without a window manager (a bare xvfb) the
        # window may not be active at all, and then the handler does
        # nothing and the claims below hold either way.
        if not mw.isActiveWindow():
            note("NOTE the main window is not active: its activation "
                 "handler is not reached")
        QtWidgets.QApplication.sendEvent(
            mw, QtCore.QEvent(QtCore.QEvent.ActivationChange))
        edit_and_leave("window activated again", sketch)

        FreeCAD.newDocument(OTHER)
        settle()
        check("another document's view is the active one",
              FreeCADGui.ActiveDocument.Document.Name == OTHER,
              FreeCADGui.ActiveDocument.Document.Name)
        edit_and_leave("another document active", sketch)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    for name in (OTHER, DOC):
        try:
            FreeCAD.closeDocument(name)
        except Exception:
            pass
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
