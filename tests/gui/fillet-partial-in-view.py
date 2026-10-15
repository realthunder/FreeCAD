"""A fillet keeps the fillets it can make; a failed edge's faces stay, red.

docs/HandsOnQueue.md entry 143 (the reporter, 2026-10-15 15:29, 15:42,
15:52): "skip any missing or failed fillet and compute as many fillets as
possible. keep the previous preview faces cooresponding to the failed edges
for visual inspection along with the newly made preview fillet face. still
mark the object for error. when dragging if recompute fails for that
dragging fillet, change cursor to stop sign"; "color the failed face red if
possible. failed face stays failed after release"; the cursor "goes back".

A PartDesign box 10 by 10 by 2, a fillet at 0.5 on a vertical edge (2 long,
between faces 10 wide) and on a top edge away from it (its side 2 high). In
edit. Claims:
  - at 0.5 nothing failed; the preview has the two fillets' faces;
  - the radius typed in the view as 3: the fillet is in error, the top edge
    failed, the result holds the vertical edge's fillet, and the preview has
    its faces and the top edge's from 0.5 (FailedShape);
  - 0.5 again: nothing failed, the feature valid;
  - a drag of the top edge's arrow out past 2: the stop sign is the pointer
    while the button is down; after the release it is not, and the top edge
    stays failed.

Runs with QT_QPA_PLATFORM=offscreen; the drag goes through the window. That
the kept faces are red is not read here: the preview's colours are not
reachable from Python. With GT_PICTURE set it saves the view after the
failure.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide.QtCore import Qt

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FilletPartial"

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


def key(k):
    real_key(viewer_widget(), k)


def open_box():
    for w in mw().findChildren(QtWidgets.QAbstractSpinBox):
        if not w.isVisible() or w.metaObject().className() != "Gui::QuantitySpinBox":
            continue
        parent = w
        while parent is not None and parent.metaObject().className() != "Gui::TaskView::TaskView":
            parent = parent.parentWidget()
        if parent is None:
            return w
    return None


def type_radius(text):
    """The default radius typed in the view: Tab makes it active, the first
    digit opens its label, Enter commits, a click away closes it"""
    viewer_widget().setFocus()
    pump(10)
    key(Qt.Key_Tab)
    key(Qt.Key_Escape)
    key(Qt.Key_Tab)
    key(getattr(Qt, "Key_" + text[0]))
    box = open_box()
    if box is None:
        return False
    for ch in text[1:]:
        QtTest.QTest.keyClicks(box, ch)
    pump(5)
    real_key(box, Qt.Key_Return)
    pump(20)
    viewer_widget().setFocus()
    pump(120)
    return True


def pick_arrow(center, reach=70, step=2):
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
    return found


def to_window(x, y):
    width, height = view().getSize()
    w = viewer_widget()
    local = QtCore.QPoint(int(x), int(height - y))
    g = w.viewport().mapToGlobal(local) if hasattr(w, "viewport") else w.mapToGlobal(local)
    return mw().mapFromGlobal(g)


def cursor_shape():
    c = QtWidgets.QApplication.overrideCursor()
    return c.shape() if c is not None else None


def faces(shape):
    try:
        return len(shape.Faces)
    except Exception:
        return 0


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw().activateWindow()
        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = 10
        box.Height = 2
        doc.recompute()
        shape = box.Shape
        vertical = top = None
        for i, e in enumerate(shape.Edges):
            d = e.Vertexes[1].Point - e.Vertexes[0].Point
            if abs(d.z) > 1e-7:
                vertical = vertical or (i + 1, e)
        for i, e in enumerate(shape.Edges):
            d = e.Vertexes[1].Point - e.Vertexes[0].Point
            if abs(d.z) < 1e-7 and all(abs(v.Point.z - 2.0) < 1e-7 for v in e.Vertexes) \
                    and not any(v.isSame(w) for v in e.Vertexes for w in vertical[1].Vertexes):
                top = top or (i + 1, e)
        topEdge = top[1]
        vertical, top = "Edge%d" % vertical[0], "Edge%d" % top[0]
        note("INFO the vertical edge %s, the top edge %s" % (vertical, top))
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (box, [vertical, top])
        fillet.Radius = 0.5
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        whole = faces(fillet.DressUpShape)
        check("at 0.5 nothing failed; the preview has the fillets' faces",
              list(fillet.FailedEdges) == [] and whole >= 2, (list(fillet.FailedEdges), whole))

        type_radius("3")
        note("INFO at 3: Radius %s, valid %s, failed %s, kept faces %d, preview faces %d"
             % (fillet.Radius, fillet.isValid(), list(fillet.FailedEdges),
                faces(fillet.FailedShape), faces(fillet.DressUpShape)))
        check("typed 3: the radius is 3", abs(fillet.Radius.Value - 3.0) < 1e-9, fillet.Radius)
        check("typed 3: the fillet is in error, the top edge failed",
              not fillet.isValid() and list(fillet.FailedEdges) == [top],
              (fillet.isValid(), list(fillet.FailedEdges)))
        check("typed 3: the result holds the vertical edge's fillet",
              fillet.Shape.isValid() and fillet.Shape.Volume < 200.0 - 1.0, fillet.Shape.Volume)
        kept = faces(fillet.FailedShape)
        check("typed 3: the top edge's faces from 0.5 are kept and in the preview",
              kept > 0 and faces(fillet.DressUpShape) >= kept + 1,
              (kept, faces(fillet.DressUpShape)))
        if os.environ.get("GT_PICTURE"):
            view().saveRenderDump(os.path.join(OUT, "fillet-partial.png"), metadata=False)

        type_radius("0.5")
        check("0.5 again: nothing failed, the feature valid",
              fillet.isValid() and list(fillet.FailedEdges) == [] and faces(fillet.FailedShape) == 0,
              (fillet.isValid(), list(fillet.FailedEdges)))

        # A drag of the top edge's arrow out past 2
        middle = topEdge.valueAt((topEdge.FirstParameter + topEdge.LastParameter) / 2)
        arrow = pick_arrow(view().getPointOnViewport(middle))
        note("INFO the top edge's arrow at %s" % (arrow,))
        if check("an arrow is found to drag", arrow is not None):
            # its way on the screen: from the edge's middle along the arrow
            inward = FreeCAD.Vector(0, 0, -1)
            for f in shape.Faces:
                if abs(f.normalAt(0, 0).z) < 1e-7 and f.isInside(middle, 1e-6, True):
                    inward = inward - f.normalAt(0, 0)
            inward.normalize()
            p0 = view().getPointOnViewport(middle)
            p1 = view().getPointOnViewport(middle + inward * 3)
            way = (p1[0] - p0[0], p1[1] - p0[1])
            # 3 mm along the arrow: a radius of about 7, well past 2 and short
            # of the vertical edge's 10
            offset = (int(way[0]), int(way[1]))
            note("INFO the drag: %s pixels" % (offset,))
            win = mw().windowHandle()
            p = to_window(*arrow)
            QtTest.QTest.mouseMove(win, p)
            pump(5)
            QtTest.QTest.mousePress(win, Qt.LeftButton, Qt.NoModifier, p)
            pump(5)
            for i in range(1, 11):
                q = QtCore.QPoint(p.x() + offset[0] * i // 10, p.y() - offset[1] * i // 10)
                QtTest.QTest.mouseMove(win, q)
                pump(5)
            pump(120)
            during = cursor_shape()
            note("INFO in the drag: Radius %s, failed %s, the cursor %s"
                 % (fillet.Radius, list(fillet.FailedEdges), during))
            check("a drag that fails the top edge: the stop sign",
                  during == Qt.ForbiddenCursor, (during, fillet.Radius, list(fillet.FailedEdges)))
            QtTest.QTest.mouseRelease(win, Qt.LeftButton, Qt.NoModifier, q)
            pump(80)
            after = cursor_shape()
            check("released: the stop sign gone", after != Qt.ForbiddenCursor, after)
            check("released: the top edge stays failed", top in list(fillet.FailedEdges)
                  and faces(fillet.FailedShape) > 0,
                  (list(fillet.FailedEdges), faces(fillet.FailedShape)))
        check("still in edit", gdoc.getInEdit() is not None)
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
