"""Select All (Ctrl+A) in a sketch edit selects the sketch's elements.

Upstream 3b76d77ed8 and its follow-ups (95840a79d3, 061e185e7f,
ca8bfc6180, e278d22d42) give a sketch edit its own Select All: Ctrl+A,
or Edit > Select All, selects the sketch's geometry and constraints
rather than the document's objects. With one of the task panel's lists
focused it selects what that list shows. The fork had none of it: the
core half (Std_SelectAll asking the edited view provider first, and
Ctrl+A bound to it) was never taken either, so Ctrl+A in a sketch edit
selected every object in the document.

Driven with QTest key clicks, which go through Qt's shortcut machinery
(ShortcutOverride, then the window shortcut) the way a real key does.

The sketch: three lines, a circle, an arc and a point, a Horizontal and a
Radius constraint. Expected from the 3D view: 11 vertices (2 per line, the
circle's centre, the arc's 3, the point), 5 edges (the point has none),
the root point and both constraints -- 19 names.

Checks:

  - Ctrl+A with the 3D view focused selects exactly those 19, and no
    document object;
  - Edit > Select All (the command itself) does the same;
  - with the constraints list focused, only the two constraints;
  - with the elements list focused, only the geometry: no root point, no
    constraint;
  - outside an edit, Ctrl+A in the 3D view still selects the document's
    objects.

Scored against the tree before the change: Ctrl+A with the 3D view focused,
and Edit > Select All, select the document's objects (Box, Sketch) and no
element; with the elements list focused nothing is selected. The two
constraints-list checks pass before as well -- with no window shortcut the
list's own Ctrl+A selects its visible rows and the panel passes them on --
and so does the out-of-edit check.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
state = {"done": False}

VERTICES = ["Vertex%d" % i for i in range(1, 12)]
EDGES = ["Edge%d" % i for i in range(1, 6)]
CONSTRAINTS = ["Constraint1", "Constraint2"]
EVERYTHING = sorted(VERTICES + EDGES + ["RootPoint"] + CONSTRAINTS)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def selection():
    """(sorted element names of the sketch, names of other selected objects)"""
    names, others = [], []
    for e in FreeCADGui.Selection.getSelectionEx("*"):
        if e.ObjectName == "Sketch" and e.SubElementNames:
            names += list(e.SubElementNames)
        else:
            others.append(e.ObjectName)
    return sorted(names), sorted(others)


def widget(name):
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.objectName() == name:
            return w
    return None


def viewport():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        cls = w.metaObject().className()
        if ("Quarter" in cls or "View3DInventorViewer" in cls) and w.isVisible():
            return w
    return None


def ctrl_a(w):
    """Focus w and press Ctrl+A there, as a user would."""
    FreeCADGui.Selection.clearSelection()
    w.setFocus(QtCore.Qt.OtherFocusReason)
    settle()
    QtTest.QTest.keyClick(w, QtCore.Qt.Key_A, QtCore.Qt.ControlModifier)
    settle()


def run():
    try:
        import Part
        import Sketcher
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw = FreeCADGui.getMainWindow()
        mw.showMaximized()
        mw.activateWindow()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("SelectAll")
        doc.addObject("Part::Box", "Box")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(10, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(10, 20, 0), V(30, 25, 0)), False)
        sk.addGeometry(Part.LineSegment(V(10, 30, 0), V(30, 40, 0)), False)
        sk.addGeometry(Part.Circle(V(50, 20, 0), V(0, 0, 1), 5), False)
        sk.addGeometry(Part.ArcOfCircle(Part.Circle(V(70, 20, 0), V(0, 0, 1), 5),
                                        0.0, 2.0), False)
        sk.addGeometry(Part.Point(V(90, 20, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
        sk.addConstraint(Sketcher.Constraint("Radius", 3, 5.0))
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        settle(20)
        note("active window: %s" % (QtWidgets.QApplication.activeWindow(),))

        view = viewport()
        ctrl_a(view)
        names, others = selection()
        note("view Ctrl+A: %s others %s" % (names, others))
        check("Ctrl+A in the 3D view selects every element and constraint",
              names == EVERYTHING, "%d of %d" % (len(names), len(EVERYTHING)))
        check("and no document object", others == [], others)

        FreeCADGui.Selection.clearSelection()
        view.setFocus(QtCore.Qt.OtherFocusReason)
        settle()
        FreeCADGui.runCommand("Std_SelectAll")
        settle()
        names, others = selection()
        check("Edit > Select All does the same",
              names == EVERYTHING and others == [], "%d, others %s" % (len(names), others))

        clist = widget("listWidgetConstraints")
        ctrl_a(clist)
        names, others = selection()
        note("constraints list Ctrl+A: %s" % (names,))
        check("with the constraints list focused, only the constraints",
              names == CONSTRAINTS and others == [], names)

        # A filter hides rows; what is hidden is not selected (upstream
        # ca8bfc6180).
        clist.item(1).setHidden(True)
        ctrl_a(clist)
        names, others = selection()
        clist.item(1).setHidden(False)
        check("a constraint row the list hides is not selected",
              names == ["Constraint1"], names)

        elist = widget("elementsWidget")
        ctrl_a(elist)
        names, others = selection()
        note("elements list Ctrl+A: %s" % (names,))
        check("with the elements list focused, only the geometry",
              names == sorted(VERTICES + EDGES) and others == [], names)

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle(20)
        ctrl_a(viewport())
        names, others = selection()
        note("outside edit Ctrl+A: %s %s" % (names, others))
        check("outside an edit Ctrl+A selects the document's objects",
              "Box" in others and "Sketch" in others, others)
        FreeCADGui.Selection.clearSelection()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
