"""A click on a fillet's edit preview selects the row of the edge it was made on.

docs/HandsOnQueue.md entry 169 (the reporter, 2026-10-17 12:18): "I want all
dress up preview shape to be pickable in 3d view. So lift that unpickable
style. When (pre)selected in 3d view, resolve the element name to base edge
name. For missing element resolve to the ?Edge name. So I am expecting the
edit panel list item selection sync." The preview of a dress-up in edit was
left out of the picks (its view not Selectable); it is picked now, and the
panel takes a pick of it for one of the reference that made it.

A PartDesign wedge, a fillet of radius 2 on a top edge and a bottom edge;
the top then collapses to a line, so the top edge's reference is gone and
its face is kept as last made (FailedShape). In edit. The reporter, 13:57:
"Hover face should resolve the edge name too, which should trigger edge
preselection highlight and edit panel list item hover plus scroll to view.
Same if preselect edge in 3d view or hover list item, the other two shall
react". Claims:
  - the pointer on the bottom edge's fillet face: the base's edge is
    preselected, and its row shown hovered; off the model, no row;
  - the base's edge preselected: its row shown hovered, and not once that
    goes;
  - a click there: the base feature's edge is the selection, the preview's
    face is not, and the bottom edge's row is the one selected;
  - a click on the kept face of the missing top edge: its "?Edge" row is the
    one selected;
  - in the add mode, a click on that kept face removes the missing edge's
    row and its reference;
  - after the edit, the fillet's preview takes no pick (outside the edit,
    unchanged).

Moves the mouse pointer and clicks in the 3D view: run on a real screen.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

Qt = QtCore.Qt
OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FilletPreviewPick"
RADIUS = 2.0
PREFIX = ";AddSub;"

state = {"done": False}


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=30):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        time.sleep(0.01)


def mw():
    return FreeCADGui.getMainWindow()


def view():
    return FreeCADGui.getDocument(DOC).ActiveView


def face_on(face):
    u0, u1, v0, v1 = face.ParameterRange
    n = face.normalAt((u0 + u1) / 2, (v0 + v1) / 2)
    view().setViewDirection((-n.x, -n.y, -n.z))
    view().fitAll()
    pump(40)


def on_handle(x, y):
    """Whether the first thing a ray at viewport pixel (x, y) hits is a
    handle of the edit (a fillet's radius arrow sits at its face's middle,
    and a click there drags it)"""
    from pivy import coin

    viewer = view().getViewer()
    rm = viewer.getSoRenderManager()
    action = coin.SoRayPickAction(rm.getViewportRegion())
    action.setPoint(coin.SbVec2s(int(x), int(y)))
    action.setRadius(4)
    action.apply(rm.getSceneGraph())
    picked = action.getPickedPoint()
    if picked is None:
        return False
    path = picked.getPath()
    for i in range(path.getLength()):
        if "Dragger" in path.getNode(i).getTypeId().getName().getString():
            return True
    return False


def spot(face):
    """A point of the face in viewport pixels, y up, off the edit's handles"""
    u0, u1, v0, v1 = face.ParameterRange
    for fu, fv in ((0.5, 0.2), (0.5, 0.8), (0.2, 0.5), (0.8, 0.5), (0.3, 0.3), (0.5, 0.5)):
        point = face.valueAt(u0 + (u1 - u0) * fu, v0 + (v1 - v0) * fv)
        x, y = view().getPointOnScreen(point)
        if not on_handle(x, y):
            note("INFO a spot at %s of the face: %s" % ((fu, fv), (int(x), int(y))))
            return int(x), int(y)
    note("INFO every spot tried is on a handle")
    return int(x), int(y)


def to_window(x, y):
    width, height = view().getSize()
    w = view().graphicsView()
    local = QtCore.QPoint(int(x), int(height - y))
    g = w.viewport().mapToGlobal(local) if hasattr(w, "viewport") else w.mapToGlobal(local)
    return mw().mapFromGlobal(g)


def move_to(x, y):
    win = mw().windowHandle()
    QtTest.QTest.mouseMove(win, to_window(x, y))
    pump(10)
    QtTest.QTest.mouseMove(win, to_window(x + 1, y))
    pump(30)


def click(x, y):
    win = mw().windowHandle()
    p = to_window(x, y)
    QtTest.QTest.mouseMove(win, p)
    pump(10)
    QtTest.QTest.mousePress(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(5)
    QtTest.QTest.mouseRelease(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(60)


def selection():
    return [(s.ObjectName, list(s.SubElementNames))
            for s in FreeCADGui.Selection.getSelectionEx("", 0)]


def preselected():
    # (no preselection: an object that raises on its Object)
    try:
        sel = FreeCADGui.Selection.getPreselection()
        names = list(sel.SubElementNames) if sel and sel.Object else []
        return (sel.Object.Name if sel and sel.Object else "") + ":" + (names[0] if names else "")
    except RuntimeError:
        return ""


def tree():
    for w in mw().findChildren(QtWidgets.QTreeWidget):
        if w.objectName() == "treeWidgetReferences" and w.isVisible():
            return w
    return None


def add_button():
    for w in mw().findChildren(QtWidgets.QCheckBox):
        if w.objectName() == "buttonRefAdd" and w.isVisible():
            return w
    return None


def selected_rows(widget):
    return [it.text(0) for it in widget.selectedItems()]


def hovered_rows(widget):
    """The rows the panel shows as hovered: a background of their own"""
    out = []
    for i in range(widget.topLevelItemCount()):
        item = widget.topLevelItem(i)
        if item.background(0).style() != Qt.NoBrush:
            out.append(item.text(0))
    return out


def rows(widget):
    return [widget.topLevelItem(i).text(0) for i in range(widget.topLevelItemCount())]


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool("PerViewEdit", True)
        mw().showNormal()
        mw().setGeometry(QtGui.QGuiApplication.primaryScreen().availableGeometry())
        pump(30)
        mw().activateWindow()
        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        wedge = body.newObject("PartDesign::AdditiveWedge", "Wedge")
        doc.recompute()
        shape = wedge.Shape
        top = bottom = None
        for i, e in enumerate(shape.Edges):
            ends = e.Vertexes
            if len(ends) != 2:
                continue
            if (all(abs(v.Point.y - shape.BoundBox.YMax) < 1e-7 for v in ends)
                    and all(abs(v.Point.z - wedge.Z2min.Value) < 1e-7 for v in ends)):
                top = "Edge%d" % (i + 1)
            if (all(abs(v.Point.y - shape.BoundBox.YMin) < 1e-7 for v in ends)
                    and all(abs(v.Point.z - wedge.Zmin.Value) < 1e-7 for v in ends)):
                bottom = "Edge%d" % (i + 1)
        note("INFO the top edge %s, the bottom edge %s" % (top, bottom))
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (wedge, [top, bottom])
        fillet.Radius = RADIUS
        doc.recompute()
        wedge.X2min = 5
        wedge.X2max = 5
        doc.recompute()
        kept = fillet.FailedShape
        check("the top edge is missing, its face kept",
              len(fillet.FailedEdges) == 1 and not kept.isNull() and len(kept.Faces) >= 1,
              "FailedEdges %s" % list(fillet.FailedEdges))
        normal = [f for f in fillet.Shape.Faces
                  if type(f.Surface).__name__ in ("Cylinder", "SurfaceOfExtrusion", "BSplineSurface")
                  and abs(f.CenterOfMass.y - shape.BoundBox.YMin) < RADIUS * 1.5]
        check("the bottom edge's fillet face is found", bool(normal), len(normal))

        gdoc = FreeCADGui.getDocument(DOC)
        FreeCADGui.SendMsgToActiveView("ViewFit")
        pump()
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        widget = tree()
        if not check("the panel's edge list is shown", widget is not None):
            return
        texts = rows(widget)
        note("INFO rows %s" % texts)
        missing = [t for t in texts if t.startswith("?")]
        present = [t for t in texts if not t.startswith("?")]
        if not check("a row for each edge", missing and present, texts):
            return

        # the present edge's fillet face
        FreeCADGui.Selection.clearSelection()
        face_on(normal[0])
        x, y = spot(normal[0])
        move_to(x, y)
        pre = preselected()
        check("the pointer on the bottom edge's fillet face: the base feature's edge is "
              "preselected (the reporter 13:57: 'hover face should resolve the edge name too')",
              pre.startswith("Body:Wedge.") and pre.endswith("." + present[0]), pre)
        check("... and its row is shown hovered", hovered_rows(widget) == [present[0]],
              hovered_rows(widget))
        # off the model: nothing preselected, no row hovered
        move_to(3, 3)
        check("the pointer off the model: no row hovered", hovered_rows(widget) == [],
              "%s, preselected %s" % (hovered_rows(widget), preselected()))
        # the base's edge preselected in the 3D view: its row hovered too
        FreeCADGui.Selection.setPreselection(
            FreeCAD.getDocument(DOC).getObject("Body"), "Wedge." + present[0])
        pump(30)
        check("the base's edge preselected: its row is shown hovered",
              hovered_rows(widget) == [present[0]], hovered_rows(widget))
        FreeCADGui.Selection.clearPreselection()
        pump(30)
        check("... and no longer when that goes", hovered_rows(widget) == [], hovered_rows(widget))
        move_to(x, y)
        click(x, y)
        sel = selection()
        names = [n for _, subs in sel for n in subs]
        check("a click there: the base feature's edge is the selection",
              # (by its new style name as well: "Wedge.;FrontB.Edge8")
              any(n.startswith("Wedge.") and n.endswith("." + present[0]) for n in names), sel)
        check("... and not the preview's face", not any(PREFIX in n for n in names), sel)
        check("... and the bottom edge's row is the one selected",
              selected_rows(widget) == [present[0]], selected_rows(widget))

        # the missing edge's kept face
        FreeCADGui.Selection.clearSelection()
        pump(20)
        face_on(kept.Faces[0])
        x, y = spot(kept.Faces[0])
        move_to(x, y)
        pre = preselected()
        check("the pointer on the missing top edge's kept face: the face itself is preselected",
              PREFIX in pre, pre)
        check("... and the '?Edge' row is shown hovered", hovered_rows(widget) == [missing[0]],
              hovered_rows(widget))
        click(x, y)
        sel = selection()
        check("a click on the missing top edge's kept face: its '?Edge' row is the one selected",
              selected_rows(widget) == [missing[0]], "rows %s, selection %s"
              % (selected_rows(widget), sel))

        # the add mode: a click on it toggles the reference
        FreeCADGui.Selection.clearSelection()
        pump(20)
        button = add_button()
        if check("the add button is shown", button is not None):
            button.click()
            pump(40)
            click(x, y)
            pump(60)
            check("in the add mode, a click on the kept face removes the missing edge's row",
                  missing[0] not in rows(widget), rows(widget))
            subs = fillet.Base[1] if fillet.Base else []
            check("... and its reference", not any(s.lstrip("?") == missing[0].lstrip("?")
                                                   for s in subs), list(subs))
            button.click()
            pump(20)

        gdoc.resetEdit()
        pump(60)
        # outside the edit: the preview is shown again for a check of the
        # pick only by the hidden fillet's preview node, which stays out
        FreeCADGui.Selection.clearSelection()
        face_on(normal[0])
        x, y = spot(normal[0])
        move_to(x, y)
        pre = preselected()
        check("after the edit, no pick names the preview", PREFIX not in pre, pre)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").RemBool("PerViewEdit")
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
        pump()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


QtCore.QTimer.singleShot(3000, run)
