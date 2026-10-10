"""A click on a TechDraw section line is not a move, and a move is by what
the line moved.

The section line of a base view can be dragged; the section follows
(docs/HandsOnQueue.md, the section line entry). Two things were wrong:

  - every release of the mouse on the line ended in "Move section line":
    the section was given the line's points again and recomputed, for a
    click that only selected it;
  - the points go to the section in the base view's coordinates as they are
    drawn -- times the view's scale -- and were taken for unscaled. So each
    time, moved or not, a section that does not pass through the centroid
    went away from it by the scale again: at 2:1, a line 4 mm off the
    centre cut 8 mm off after one click, 16 after two.

Claims, on a base view at 2:1 with the section 4 mm off the centroid:
  - a click on the line leaves SectionOrigin where it is, leaves no "Move
    section line" to undo and recomputes nothing (the last is what tells
    a click that is ignored from one that is carried out and happens to
    change nothing);
  - the line dragged 10 mm up the page moves the origin 5 mm, in the base
    view's plane and by nothing else, and that is one step to undo and one
    recompute;
  - a click on the line after that changes nothing again;
  - undo puts the origin back.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SectionLineClick"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
HAD = GEN.GetBool("PageRendererVg", False)
VIEW_X, VIEW_Y, SCALE = 120.0, 110.0, 2.0
ORIGIN = FreeCAD.Vector(10, 14, 10)
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
        if v.metaObject().className() == "TechDrawGui::QGVPage":
            return v
    return None


def section():
    return FreeCAD.getDocument(DOC).getObject("Section")


def moves():
    return [n for n in FreeCAD.getDocument(DOC).UndoNames if "section line" in n.lower()]


class Recomputes:
    """Counts the document's recomputes: a move ends in one, a click must not."""

    def __init__(self):
        self.count = 0

    def slotBeforeRecomputeDocument(self, doc):
        if doc.Name == DOC:
            self.count += 1


RECOMPUTES = Recomputes()


def at(mm_x, mm_y):
    """Viewport position of a point given in page mm."""
    return page_view().mapFromScene(QtCore.QPointF(mm_x * 10.0, -mm_y * 10.0))


def line_y():
    """Where the section line is on the page now, in mm: the origin's
    distance from the centroid along model Y, at the view's scale."""
    return VIEW_Y + SCALE * (section().SectionOrigin.y - 10.0)


def mouse(kind, pos, button=QtCore.Qt.NoButton, buttons=QtCore.Qt.NoButton):
    vp = page_view().viewport()
    ev = QtGui.QMouseEvent(kind, QtCore.QPointF(pos), QtCore.QPointF(vp.mapToGlobal(pos)),
                           button, buttons, QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)


def click(pos):
    mouse(QtCore.QEvent.MouseMove, pos)
    mouse(QtCore.QEvent.MouseButtonPress, pos, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    mouse(QtCore.QEvent.MouseButtonRelease, pos, QtCore.Qt.LeftButton, QtCore.Qt.NoButton)


def click_line():
    """A click on the line: on the row it is drawn on and the one either
    side, the line being a pixel or two thick."""
    p = at(VIEW_X + 12.0, line_y())
    for dy in (0, -1, 1):
        click(QtCore.QPoint(p.x(), p.y() + dy))


@step(1000)
def make():
    import TechDrawGui  # the view providers
    GEN.SetBool("PageRendererVg", False)
    doc = FreeCAD.newDocument(DOC)
    box = doc.addObject("Part::Box", "Box")
    box.Length = 20
    box.Width = 20
    box.Height = 20
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
    view.Scale = SCALE
    view.X = VIEW_X
    view.Y = VIEW_Y
    doc.recompute()
    sec = doc.addObject("TechDraw::DrawViewSection", "Section")
    page.addView(sec)
    sec.Source = [box]
    sec.BaseView = view
    sec.ScaleType = "Custom"
    sec.Scale = SCALE
    sec.Direction = FreeCAD.Vector(0, 1, 0)
    sec.SectionNormal = FreeCAD.Vector(0, 1, 0)
    sec.SectionOrigin = ORIGIN
    sec.X = 230
    sec.Y = 110
    doc.recompute()
    doc.UndoMode = 1
    FreeCADGui.getDocument(DOC).getObject("Page").show()


@step(5000)
def zoom():
    v = page_view()
    if not check("the page has its view", v is not None):
        raise RuntimeError("no page view")
    # enlarged: the line is then thick enough to be met by a pixel
    v.scale(3.0, 3.0)
    v.centerOn(QtCore.QPointF(VIEW_X * 10.0, -VIEW_Y * 10.0))
    doc = FreeCAD.getDocument(DOC)
    doc.clearUndos()
    FreeCAD.addDocumentObserver(RECOMPUTES)
    o = section().SectionOrigin
    check("the section is where it was put", (o - ORIGIN).Length < 1e-9, tuple(o))


@step(1500)
def first_click():
    v = page_view()
    v.grab().save(os.path.join(OUT, "1-before.png"))
    click_line()


@step(2500)
def after_click():
    o = section().SectionOrigin
    check("a click on the line leaves the section where it is",
          (o - ORIGIN).Length < 1e-9, tuple(o))
    check("a click on the line is nothing to undo", not moves(),
          FreeCAD.getDocument(DOC).UndoNames)
    check("a click on the line recomputes nothing", RECOMPUTES.count == 0,
          "%d recomputes" % RECOMPUTES.count)
    RECOMPUTES.count = 0
    # 10 mm up the page, in steps
    p = at(VIEW_X + 12.0, line_y())
    mouse(QtCore.QEvent.MouseMove, p)
    mouse(QtCore.QEvent.MouseButtonPress, p, QtCore.Qt.LeftButton, QtCore.Qt.LeftButton)
    for i in range(1, 11):
        q = at(VIEW_X + 12.0, line_y() + i)
        mouse(QtCore.QEvent.MouseMove, QtCore.QPoint(p.x(), q.y()),
              QtCore.Qt.NoButton, QtCore.Qt.LeftButton)
    STATE["gone"] = (p.y() - q.y()) / (10.0 * page_view().transform().m11())  # mm
    mouse(QtCore.QEvent.MouseButtonRelease, QtCore.QPoint(p.x(), q.y()),
          QtCore.Qt.LeftButton, QtCore.Qt.NoButton)


@step(3000)
def after_drag():
    v = page_view()
    v.grab().save(os.path.join(OUT, "2-dragged.png"))
    o = section().SectionOrigin
    want = ORIGIN.y + STATE["gone"] / SCALE
    check("the line dragged %.2f mm up the page moves the origin by that over the scale"
          % STATE["gone"],
          abs(o.y - want) < 0.05 and abs(o.y - ORIGIN.y) > 4.0,
          "origin y %.4f, wanted %.4f" % (o.y, want))
    check("the drag moves the origin in the base view's plane along the line's normal only",
          abs(o.x - ORIGIN.x) < 1e-6 and abs(o.z - ORIGIN.z) < 1e-6, tuple(o))
    check("the drag is one step to undo, and one recompute",
          len(moves()) == 1 and RECOMPUTES.count == 1,
          (FreeCAD.getDocument(DOC).UndoNames, RECOMPUTES.count))
    RECOMPUTES.count = 0
    STATE["moved"] = FreeCAD.Vector(o)
    click_line()


@step(2500)
def after_second_click():
    o = section().SectionOrigin
    check("a click on the line where it went changes nothing and recomputes nothing",
          (o - STATE["moved"]).Length < 1e-9 and len(moves()) == 1
          and RECOMPUTES.count == 0,
          (tuple(o), FreeCAD.getDocument(DOC).UndoNames, RECOMPUTES.count))
    FreeCAD.removeDocumentObserver(RECOMPUTES)
    for _ in range(4):
        if moves():
            FreeCAD.getDocument(DOC).undo()


@step(2000)
def after_undo():
    o = section().SectionOrigin
    check("undo puts the origin back", (o - ORIGIN).Length < 1e-9, tuple(o))


def finish():
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
