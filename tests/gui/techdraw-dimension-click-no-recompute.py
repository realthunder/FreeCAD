"""Selecting a TechDraw dimension is not a drag of its label.

docs/HandsOnQueue.md entry 35: in a page, "a seemingly random click will
trigger recompute", a click on a dimension did not select it, and
selecting it in the tree could recompute and clear the selection. The
dimension's label counted ANY change of its position as a drag under way --
and every redraw puts the label in place. From then on the label was
"dragging": the next time it lost the selection, or the mouse was released
on it, the drag "finished", which writes the dimension's X and Y and
recomputes the document. In a document with objects in error that is the
whole failing recompute again, and the redraw after it drops the selection
the click had just made.

Claims, on a page with the top view of a box and one dimension on it:
  - the dimension selected from outside the page (as the tree does) and
    the selection cleared: no recompute, no "Drag Dimension" to undo;
  - a click on its label selects it, with no recompute and nothing to undo;
  - a click on an empty spot of the page after that: no recompute;
  - the label dragged 10 mm up the page: Y is 10 more, one step to undo
    and one recompute;
  - a click on the label where it went: nothing again.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DimensionClick"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
HAD = GEN.GetBool("PageRendererVg", False)
VIEW_X, VIEW_Y = 150.0, 90.0
DIM_X, DIM_Y = 0.0, 40.0
STEPS = []
STATE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def step(delay_ms):
    def deco(fn):
        STEPS.append((delay_ms, fn))
        return fn
    return deco


def page_view():
    for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView):
        if v.metaObject().className() == "TechDrawGui::QGVPage" and v.isVisible():
            return v
    return None


def dimension():
    return FreeCAD.getDocument(DOC).getObject("Dimension")


def drags():
    return [n for n in FreeCAD.getDocument(DOC).UndoNames if "drag dimension" in n.lower()]


def selected():
    return [o.Name for o in FreeCADGui.Selection.getSelection()]


class Recomputes:
    def __init__(self):
        self.count = 0

    def slotBeforeRecomputeDocument(self, doc):
        if doc.Name == DOC:
            self.count += 1


RECOMPUTES = Recomputes()


def label():
    """Viewport position of the middle of the dimension's label: its X and Y
    are in mm from the middle of its view."""
    d = dimension()
    return page_view().mapFromScene(QtCore.QPointF((VIEW_X + float(d.X)) * 10.0,
                                                   -(VIEW_Y + float(d.Y)) * 10.0))


def mouse(kind, pos, button=QtCore.Qt.NoButton, buttons=QtCore.Qt.NoButton):
    vp = page_view().viewport()
    ev = QtGui.QMouseEvent(kind, QtCore.QPointF(pos), QtCore.QPointF(vp.mapToGlobal(pos)),
                           button, buttons, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)


def click(pos):
    mouse(QtCore.QEvent.MouseMove, pos)
    mouse(QtCore.QEvent.MouseButtonPress, pos, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    mouse(QtCore.QEvent.MouseButtonRelease, pos, QtCore.Qt.LeftButton, QtCore.Qt.NoButton)


def quiet(what):
    """No recompute and nothing to undo since the count was last reset"""
    ok = check("%s: no recompute, no drag to undo" % what,
               RECOMPUTES.count == 0 and not drags(),
               (RECOMPUTES.count, FreeCAD.getDocument(DOC).UndoNames))
    RECOMPUTES.count = 0
    return ok


@step(1000)
def make():
    import TechDrawGui  # noqa: F401  the view providers
    GEN.SetBool("PageRendererVg", False)
    doc = FreeCAD.newDocument(DOC)
    box = doc.addObject("Part::Box", "Box")
    box.Length = 60
    box.Width = 40
    box.Height = 10
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [box]
    view.Direction = FreeCAD.Vector(0, 0, 1)
    view.XDirection = FreeCAD.Vector(1, 0, 0)
    view.ScaleType = "Custom"
    view.Scale = 1.0
    view.X = VIEW_X
    view.Y = VIEW_Y
    doc.recompute()
    FreeCADGui.getDocument(DOC).getObject("Page").show()


@step(3000)
def dimension_it():
    # a step of its own: the view's edges are found by a thread
    doc = FreeCAD.getDocument(DOC)
    view = doc.getObject("View")
    page = doc.getObject("Page")
    # the length of whichever edge lies along the page's X
    name = None
    for i in range(4):
        edge = view.getEdgeByIndex(i)
        if abs(edge.Vertexes[0].Point.y - edge.Vertexes[-1].Point.y) < 1e-6:
            name = "Edge%d" % i
            break
    if not check("the view has an edge along the page's X", name is not None):
        raise RuntimeError("no edge")
    dim = doc.addObject("TechDraw::DrawViewDimension", "Dimension")
    dim.Type = "DistanceX"
    dim.References2D = [(view, name)]
    page.addView(dim)
    dim.X = DIM_X
    dim.Y = DIM_Y
    doc.recompute()
    doc.UndoMode = 1


@step(4000)
def select_outside():
    v = page_view()
    if not check("the page has its view", v is not None):
        raise RuntimeError("no page view")
    v.scale(2.0, 2.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -(VIEW_Y + DIM_Y / 2) * 10.0))
    doc = FreeCAD.getDocument(DOC)
    doc.recompute()
    doc.clearUndos()
    FreeCAD.addDocumentObserver(RECOMPUTES)
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(DOC, "Dimension")


@step(1500)
def clear_outside():
    check("selected from outside the page, the dimension is selected",
          selected() == ["Dimension"], selected())
    FreeCADGui.Selection.clearSelection()


@step(1500)
def click_label():
    quiet("the dimension selected from outside the page and the selection cleared")
    page_view().grab().save(os.path.join(OUT, "1-before.png"))
    STATE["pos"] = label()
    click(STATE["pos"])


@step(2000)
def click_empty():
    check("a click on the label selects the dimension", selected() == ["Dimension"],
          (selected(), STATE["pos"]))
    quiet("the click on the label")
    click(QtCore.QPoint(8, 8))


@step(2000)
def drag_label():
    check("a click on an empty spot clears the selection", selected() == [], selected())
    quiet("the click on an empty spot")
    STATE["y"] = float(dimension().Y)
    p = STATE["pos"]
    q = p
    mouse(QtCore.QEvent.MouseMove, p)
    mouse(QtCore.QEvent.MouseButtonPress, p, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    one_mm = 10.0 * page_view().transform().m11()  # pixels
    for i in range(1, 11):
        q = QtCore.QPoint(p.x(), int(round(p.y() - i * one_mm)))
        mouse(QtCore.QEvent.MouseMove, q, QtCore.Qt.NoButton, QtCore.Qt.LeftButton)
    STATE["gone"] = (p.y() - q.y()) / one_mm  # mm
    mouse(QtCore.QEvent.MouseButtonRelease, q, QtCore.Qt.LeftButton, QtCore.Qt.NoButton)


@step(3000)
def after_drag():
    page_view().grab().save(os.path.join(OUT, "2-dragged.png"))
    y = float(dimension().Y)
    check("the label dragged %.2f mm up the page: Y is that much more" % STATE["gone"],
          abs(y - STATE["y"] - STATE["gone"]) < 0.3 and STATE["gone"] > 8.0,
          "Y %.3f, was %.3f" % (y, STATE["y"]))
    check("the drag is one step to undo, and one recompute",
          len(drags()) == 1 and RECOMPUTES.count == 1,
          (FreeCAD.getDocument(DOC).UndoNames, RECOMPUTES.count))
    RECOMPUTES.count = 0
    FreeCAD.getDocument(DOC).clearUndos()
    STATE["y"] = y
    click(label())


@step(2000)
def after_last_click():
    y = float(dimension().Y)
    check("a click on the label where it went leaves it there", abs(y - STATE["y"]) < 1e-9, y)
    quiet("that click")


def finish():
    FreeCAD.removeDocumentObserver(RECOMPUTES)
    GEN.SetBool("PageRendererVg", HAD)
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
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
            note("ABORT in %s: %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


advance()
