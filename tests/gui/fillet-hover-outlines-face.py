"""Hovering an edge item of a fillet's task panel outlines that edge's fillet face.

docs/HandsOnQueue.md entry 160 (the reporter, 2026-10-16 11:00): "highlight
normal and missing fillet face with mouse over the edge item in task panel.
use the same silloute outline following the selection style". Under a per
view edit the preview is the edit session's own node, in each view's editing
root, which the normal preselection never reaches: the panel lit the face by
recolouring it. The face now goes as a preselection through each view's
edit highlight, and the renderer outlines it there as it outlines a
preselected face of the scene -- by default the outline alone, no fill
(ShowPreSelectedFaceOutline, NoPreSelFaceHighlightWithOutline).

A PartDesign wedge, a fillet of radius 2 on a top edge and a bottom edge;
the top then collapses to a line, so the top edge's reference is gone and
its face is kept as last made (FailedShape). In edit. Claims, each from a
picture of the view (saveRenderDump):
  - the pointer on no item: no highlight colour in the view;
  - on the bottom edge's item: the highlight colour around its fillet face,
    and not at the face's middle (the outline, not a fill);
  - on the missing top edge's item: the same for its kept face;
  - the pointer off the items again: the highlight colour gone.

Needs the view really drawn by the bgfx renderer: NOT for QT_QPA_PLATFORM
=offscreen, where saveRenderDump times out. It moves the mouse pointer over
the task panel.
"""
import os
import re
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FilletHoverOutline"
RADIUS = 2.0

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


def highlight_rgb():
    packed = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").GetUnsigned(
        "HighlightColor", 0xE1E114FF)
    return ((packed >> 24) & 0xFF, (packed >> 16) & 0xFF, (packed >> 8) & 0xFF)


def picture(tag):
    path = os.path.join(OUT, "%s.png" % tag)
    view().saveRenderDump(path)
    image = QtGui.QImage(path)
    return image


def lit(image, x, y, rgb):
    c = QtGui.QColor(image.pixel(x, y))
    return abs(c.red() - rgb[0]) + abs(c.green() - rgb[1]) + abs(c.blue() - rgb[2]) < 60


def count_lit(image, rgb):
    n = 0
    for y in range(0, image.height(), 2):
        for x in range(0, image.width(), 2):
            if lit(image, x, y, rgb):
                n += 1
    return n


def middle_lit(image, face, rgb):
    # the face's middle, by its parameters, on the picture
    u0, u1, v0, v1 = face.ParameterRange
    point = face.valueAt((u0 + u1) / 2, (v0 + v1) / 2)
    # in the viewport's own pixels, y up from the bottom
    x, y = view().getPointOnScreen(point)
    widget = view().graphicsView()
    ratio = widget.devicePixelRatioF()
    scale_x = image.width() / float(widget.width() * ratio)
    scale_y = image.height() / float(widget.height() * ratio)
    px = int(x * scale_x)
    py = int(image.height() - 1 - y * scale_y)
    hits = 0
    for dy in (-2, 0, 2):
        for dx in (-2, 0, 2):
            xx, yy = px + dx, py + dy
            if 0 <= xx < image.width() and 0 <= yy < image.height() and lit(image, xx, yy, rgb):
                hits += 1
    return hits, (px, py)


def face_on(face):
    # looked at square on: a fillet is a narrow strip seen from the side,
    # and its middle would be inside its own outline
    u0, u1, v0, v1 = face.ParameterRange
    n = face.normalAt((u0 + u1) / 2, (v0 + v1) / 2)
    view().setViewDirection((-n.x, -n.y, -n.z))
    view().fitAll()
    pump(40)


def tree():
    for w in mw().findChildren(QtWidgets.QTreeWidget):
        if w.objectName() == "treeWidgetReferences" and w.isVisible():
            return w
    return None


def point_at(widget, item):
    if item is None:
        # below the last item: no item there
        rect = widget.viewport().rect()
        return widget.viewport().mapToGlobal(QtCore.QPoint(rect.width() // 2, rect.height() - 3))
    return widget.viewport().mapToGlobal(widget.visualItemRect(item).center())


def hover(widget, item):
    target = point_at(widget, item)
    QtGui.QCursor.setPos(target)
    pump(10)
    anchor = item or widget.topLevelItem(0)
    widget.itemEntered.emit(anchor, 0)
    pump(60)
    time.sleep(0.3)
    pump(30)
    now = QtGui.QCursor.pos()
    under = widget.itemAt(widget.viewport().mapFromGlobal(now))
    note("INFO hover %s: target %s, cursor %s, item there %s"
         % (item.text(0) if item else None, (target.x(), target.y()), (now.x(), now.y()),
            under.text(0) if under else None))


def preselected():
    # the element the panel preselects: a face of the preview, not the
    # base's edge
    sel = FreeCADGui.Selection.getPreselection()
    names = list(sel.SubElementNames) if sel and sel.Object else []
    return names[0] if names else ""


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        # on the screen whole: the pointer is clamped to it, and a panel past
        # its edge is never under it
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
        check("the fillet is made on both edges", fillet.isValid(), fillet.State)
        # the top collapses to a line: the top edge is gone
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

        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()
        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        FreeCADGui.SendMsgToActiveView("ViewFit")
        pump(40)

        widget = tree()
        if not check("the panel's edge list is shown", widget is not None):
            return
        items = [widget.topLevelItem(i) for i in range(widget.topLevelItemCount())]
        texts = [it.text(0) for it in items]
        note("INFO items %s" % texts)
        missing = [it for it in items if it.text(0).startswith("?")]
        # (renumbered by the collapse: the name the reference has now)
        present = [it for it in items if not it.text(0).startswith("?")]
        if not check("an item for each edge", missing and present, texts):
            return

        rgb = highlight_rgb()
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        note("INFO highlight %s, ShowPreSelectedFaceOutline %s, NoPreSelFaceHighlightWithOutline %s"
             % (rgb, params.GetBool("ShowPreSelectedFaceOutline", True),
                params.GetBool("NoPreSelFaceHighlightWithOutline", True)))

        hover(widget, None)
        base = count_lit(picture("0-none"), rgb)
        check("on no item: no highlight colour in the view", base < 20, base)

        face_on(normal[0])
        hover(widget, present[0])
        name = preselected()
        check("on the bottom edge's item: a face of the preview is preselected, not the edge",
              re.search(r"Face[0-9]+$", name) is not None, name)
        image = picture("1-bottom")
        n = count_lit(image, rgb)
        mid = middle_lit(image, normal[0], rgb)
        check("on the bottom edge's item: the highlight colour around its face",
              n - base > 40, "lit %d (none %d)" % (n, base))
        check("... and not at the face's middle: an outline, not a fill", mid[0] == 0,
              "middle %s lit %d of 9" % (mid[1], mid[0]))

        face_on(kept.Faces[0])
        hover(widget, missing[0])
        name = preselected()
        check("on the missing top edge's item: a face of the preview is preselected",
              re.search(r"Face[0-9]+$", name) is not None, name)
        image = picture("2-missing")
        n = count_lit(image, rgb)
        mid = middle_lit(image, kept.Faces[0], rgb)
        check("on the missing top edge's item: the highlight colour around its kept face",
              n - base > 40, "lit %d (none %d)" % (n, base))
        check("... and not at the face's middle", mid[0] == 0,
              "middle %s lit %d of 9" % (mid[1], mid[0]))

        hover(widget, None)
        n = count_lit(picture("3-off"), rgb)
        check("off the items: the highlight colour gone", n < base + 20, n)
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
