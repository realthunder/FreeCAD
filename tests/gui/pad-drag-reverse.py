"""A pad's length arrow dragged back past its start reverses the pad (entry 170).

The reporter 2026-10-18 12:10: "Yes no longer flip, but support auto
reverse on drag beyond". A 10 x 10 sketch, a pad of 5 mm up. In edit:
  - a plain click on the length arrow changes nothing (no flip);
  - the arrow dragged 10 mm down -- 5 mm past the start: the pad is
    Reversed, its length about 5 mm, and after the drag the arrow stands
    on the reversed pad (pointing down);
  - the same drag the other way (10 mm up from the reversed pad's arrow):
    Reversed off again;
  - a drag past the start cancelled with Escape: Reversed and the length
    as before it;
  - still in edit, the pad valid.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtWidgets
from PySide.QtCore import Qt

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PadDragReverse"

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


def viewer_widget():
    return view().graphicsView()


def real_key(widget, k):
    QtTest.QTest.keyPress(widget, k, Qt.NoModifier)
    pump(5)
    target = QtWidgets.QApplication.focusWidget() or widget
    QtTest.QTest.keyRelease(target, k, Qt.NoModifier)
    pump(10)


def field_text(node, name):
    value = node.getField(name).get()
    return value.getString() if hasattr(value, "getString") else str(value)


def arrow_way():
    """The z of the shown length arrow's direction, in the gizmos' frame"""
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    for p in search.getPaths():
        node = p.getTail()
        if field_text(node, "visible") not in ("TRUE", "1"):
            continue
        x, y, z, angle = [float(v) for v in field_text(node, "rotation").split()[:4]]
        import math
        way = FreeCAD.Rotation(FreeCAD.Vector(x, y, z), math.degrees(angle)).multVec(
            FreeCAD.Vector(0, 1, 0))
        return round(way.z, 3)
    return None


def pick_arrow(center, reach=80, step=2):
    """A viewport pixel well inside an arrow near `center`"""
    from pivy import coin

    viewer = view().getViewer()
    region = viewer.getSoRenderManager().getViewportRegion()

    def kind(x, y):
        action = coin.SoRayPickAction(region)
        action.setPoint(coin.SbVec2s(int(x), int(y)))
        action.setRadius(1)
        action.apply(viewer.getSoRenderManager().getSceneGraph())
        picked = action.getPickedPoint()
        if picked is None:
            return False
        path = picked.getPath()
        return any("LinearDragger" in path.getNode(i).getTypeId().getName().getString()
                   for i in range(path.getLength()))

    cx, cy = center
    best, found = -1, None
    for r in range(0, reach, step):
        for dx in range(-r, r + 1, step):
            for dy in (-r, r) if abs(dx) != r else range(-r, r + 1, step):
                if not kind(cx + dx, cy + dy):
                    continue
                score = sum(kind(cx + dx + i, cy + dy + j) for i in (-2, 0, 2) for j in (-2, 0, 2))
                if score > best:
                    best, found = score, (cx + dx, cy + dy)
        if found is not None and r > 6:
            break
    return found


def to_window(x, y):
    width, height = view().getSize()
    w = viewer_widget()
    local = QtCore.QPoint(int(x), int(height - y))
    g = w.viewport().mapToGlobal(local) if hasattr(w, "viewport") else w.mapToGlobal(local)
    return mw().mapFromGlobal(g)


def vp(v):
    x, y = view().getPointOnViewport(v)
    return (x, y)


def drag(start, end, escape=False, steps=20):
    """From viewport pixel `start` to `end`; Escape before letting go"""
    win = mw().windowHandle()
    p = to_window(*start)
    q = to_window(*end)
    QtTest.QTest.mouseMove(win, p)
    pump(5)
    QtTest.QTest.mousePress(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(5)
    for i in range(1, steps + 1):
        m = QtCore.QPoint(p.x() + (q.x() - p.x()) * i // steps, p.y() + (q.y() - p.y()) * i // steps)
        QtTest.QTest.mouseMove(win, m)
        pump(8)
    if escape:
        real_key(viewer_widget(), Qt.Key_Escape)
        pump(20)
    QtTest.QTest.mouseRelease(win, Qt.LeftButton, Qt.NoModifier, q)
    pump(100)


def click(at):
    win = mw().windowHandle()
    p = to_window(*at)
    QtTest.QTest.mouseMove(win, p)
    pump(5)
    QtTest.QTest.mouseClick(win, Qt.LeftButton, Qt.NoModifier, p)
    pump(80)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw().activateWindow()
        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        sk = body.newObject("Sketcher::SketchObject", "Sketch")
        pts = [FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 0, 0),
               FreeCAD.Vector(10, 10, 0), FreeCAD.Vector(0, 10, 0)]
        for i in range(4):
            sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]), False)
        pad = body.newObject("PartDesign::Pad", "Pad")
        pad.Profile = sk
        pad.Length = 5
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(pad, 0)
        pump(100)
        check("the pad is in edit", gdoc.getInEdit() is not None)
        FreeCADGui.SendMsgToActiveView("ViewFit")
        pump(40)
        note("INFO arrow direction z %s" % arrow_way())

        top = FreeCAD.Vector(5, 5, 5)
        arrow = pick_arrow(vp(top))
        note("INFO the arrow at %s, the top at %s" % (arrow, vp(top)))
        if not check("the length arrow is found", arrow is not None):
            return

        click(arrow)
        check("a plain click on the arrow: no flip", not pad.Reversed and
              abs(pad.Length.Value - 5) < 1e-6, (pad.Reversed, pad.Length))

        # 10 mm down along the arrow: 5 mm past the start
        a, b = vp(FreeCAD.Vector(5, 5, 5)), vp(FreeCAD.Vector(5, 5, -5))
        end = (arrow[0] + b[0] - a[0], arrow[1] + b[1] - a[1])
        drag(arrow, end)
        note("INFO after the drag down: Reversed %s, Length %s, arrow z %s"
             % (pad.Reversed, pad.Length, arrow_way()))
        check("dragged 5 mm past the start: the pad is reversed", bool(pad.Reversed),
              pad.Reversed)
        check("... its length about 5 mm", abs(pad.Length.Value - 5) < 1.5, pad.Length)
        # the solid the pad adds, as the feature made it
        pad.recompute()
        pump(40)
        note("INFO Shape %s, AddSubShape %s, touched %s"
             % (pad.Shape.BoundBox, pad.AddSubShape.BoundBox, "Touched" in pad.State))
        check("... the pad valid, its solid below the sketch", pad.isValid()
              and pad.AddSubShape.BoundBox.ZMax < 1e-3, pad.AddSubShape.BoundBox)
        check("... the arrow now points down", (arrow_way() or 0) < 0, arrow_way())

        # back up, from the reversed pad's arrow
        bottom = FreeCAD.Vector(5, 5, -pad.Length.Value)
        arrow2 = pick_arrow(vp(bottom))
        note("INFO the arrow at %s" % (arrow2,))
        if check("the reversed arrow is found", arrow2 is not None):
            a, b = vp(FreeCAD.Vector(5, 5, -5)), vp(FreeCAD.Vector(5, 5, 5))
            end = (arrow2[0] + b[0] - a[0], arrow2[1] + b[1] - a[1])
            drag(arrow2, end)
            note("INFO after the drag up: Reversed %s, Length %s" % (pad.Reversed, pad.Length))
            check("dragged back past the start: Reversed off again", not pad.Reversed,
                  pad.Reversed)

        # a drag past the start, cancelled
        length = pad.Length.Value
        reversed_ = bool(pad.Reversed)
        tip = FreeCAD.Vector(5, 5, -length if reversed_ else length)
        arrow3 = pick_arrow(vp(tip))
        if check("the arrow is found again", arrow3 is not None):
            sign = -1 if reversed_ else 1
            a = vp(FreeCAD.Vector(5, 5, sign * length))
            b = vp(FreeCAD.Vector(5, 5, -sign * (length + 4)))
            end = (arrow3[0] + b[0] - a[0], arrow3[1] + b[1] - a[1])
            drag(arrow3, end, escape=True)
            note("INFO after the cancelled drag: Reversed %s, Length %s" % (pad.Reversed, pad.Length))
            check("a drag past the start cancelled with Escape: as before it",
                  bool(pad.Reversed) == reversed_ and abs(pad.Length.Value - length) < 1e-6,
                  (pad.Reversed, pad.Length))

        check("still in edit", gdoc.getInEdit() is not None)
        check("the pad is valid", pad.isValid())
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
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
