"""The last line style of a line standard is drawn as what it is.

docs/HandsOnQueue.md entry 37: an edge given the style "Chain" was drawn
continuous, while the style combo box showed its dashed sample. "Chain" is
the last line of the ASME list, and the pen for an edge's line number was
taken only for a number BELOW the number of lines defined -- so the last
line of every standard, ASME's 17, ISO's 15, ANSI's 4, fell through to the
edge's Qt style and came out continuous. The combo box draws its samples
without that test.

Claims, for each of the three standards shipped, on a page with the top
view of a box, its four edges decorated through the line decoration panel:
  - undecorated, the top edge is one unbroken run of ink (which is what
    says the measurement can tell continuous from dashed);
  - with line 2 it is broken into dashes (a style that always worked);
  - with the LAST line of the standard it is broken into dashes too.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "LastLineStyle"
GEN = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/General")
STD = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/Standards")
HAD_VG = GEN.GetBool("PageRendererVg", False)
HAD_STD = STD.GetInt("LineStandard", -1)
VIEW_X, VIEW_Y, SIDE = 150.0, 105.0, 100.0
# the standards as the preference counts them: a place in the sorted list of
# the definition files found; and the name of the last line of each
STANDARDS = [(0, "ANSI", "LongDashedDoubleDashed"),
             (1, "ASME", "Chain"),
             (2, "ISO", "DoubleDashedTripleDotted")]
STEPS = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def page_view():
    views = [v for v in FreeCADGui.getMainWindow().findChildren(QtWidgets.QGraphicsView)
             if v.metaObject().className() == "TechDrawGui::QGVPage" and v.isVisible()]
    return views[-1] if views else None


def runs(tag):
    """The number of separate runs of ink along the top edge of the view, a
    few mm short of its corners: 1 for a continuous line."""
    v = page_view()
    if v is None:
        return -1
    img = v.viewport().grab().toImage()
    img.save(os.path.join(OUT, tag + ".png"))
    left = v.mapFromScene(QtCore.QPointF((VIEW_X - SIDE / 2 + 5) * 10.0,
                                         -(VIEW_Y + SIDE / 2) * 10.0))
    right = v.mapFromScene(QtCore.QPointF((VIEW_X + SIDE / 2 - 5) * 10.0,
                                          -(VIEW_Y + SIDE / 2) * 10.0))
    count = 0
    inside = False
    for x in range(left.x(), right.x() + 1):
        ink = False
        for y in range(left.y() - 2, left.y() + 3):
            if not (0 <= x < img.width() and 0 <= y < img.height()):
                continue
            p = img.pixel(x, y)
            # dark in every channel: the face under the edge is filled, in a colour
            if max((p >> 16) & 255, (p >> 8) & 255, p & 255) < 130:
                ink = True
                break
        if ink and not inside:
            count += 1
        inside = ink
    return count


def decorate():
    FreeCADGui.Selection.clearSelection()
    for i in range(4):
        FreeCADGui.Selection.addSelection(DOC, "View", "Edge%d" % i)
    FreeCADGui.runCommand("TechDraw_DecorateLine")


def style_combo():
    for combo in FreeCADGui.getMainWindow().findChildren(QtWidgets.QComboBox, "cb_Style"):
        if combo.isVisible():
            return combo
    return None


def press_ok():
    for box in FreeCADGui.getMainWindow().findChildren(QtWidgets.QDialogButtonBox):
        parent = box.parent()
        if parent and "TaskEditControl" in parent.metaObject().className():
            button = box.button(QtWidgets.QDialogButtonBox.Ok)
            if button:
                button.click()
                return True
    return False


def close():
    try:
        FreeCADGui.Control.closeDialog()
    except Exception:
        pass
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass


def standard(index, body, last):
    def make():
        import TechDrawGui  # noqa: F401  the view providers
        GEN.SetBool("PageRendererVg", False)
        STD.SetInt("LineStandard", index)
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        box.Length = SIDE
        box.Width = SIDE
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
        page_vp = FreeCADGui.getDocument(DOC).getObject("Page")
        # no view frame: its dashes run a few pixels above the edge measured
        page_vp.ShowFrames = False
        page_vp.show()

    def plain():
        n = runs(body + "-plain")
        if not check("%s: undecorated, the top edge is one run of ink" % body, n == 1, n):
            raise RuntimeError("the edge is not where it is looked for")
        decorate()

    def second():
        combo = style_combo()
        if not check("%s: the line decoration panel is up" % body, combo is not None):
            raise RuntimeError("no panel")
        note("NOTE %s: %d styles, the last is '%s'"
             % (body, combo.count(), combo.itemText(combo.count() - 1)))
        if not check("%s: the standard in use is the one asked for" % body,
                     combo.itemText(combo.count() - 1) == last,
                     combo.itemText(combo.count() - 1)):
            raise RuntimeError("another line standard")
        combo.setCurrentIndex(1)
        press_ok()
        FreeCADGui.Selection.clearSelection()

    def after_second():
        n = runs(body + "-line2")
        check("%s: with line 2 the edge is broken into dashes" % body, n >= 3, n)
        decorate()

    def last_line():
        combo = style_combo()
        if not check("%s: the panel is up again" % body, combo is not None):
            raise RuntimeError("no panel")
        combo.setCurrentIndex(combo.count() - 1)
        press_ok()
        FreeCADGui.Selection.clearSelection()

    def after_last():
        n = runs(body + "-last")
        check("%s: with its last line, '%s', the edge is broken into dashes too" % (body, last),
              n >= 3, n)
        close()

    STEPS.extend([(1500, make), (2500, plain), (1200, second), (1500, after_second),
                  (1200, last_line), (1500, after_last)])


for _index, _body, _last in STANDARDS:
    standard(_index, _body, _last)


def finish():
    GEN.SetBool("PageRendererVg", HAD_VG)
    if HAD_STD < 0:
        STD.RemInt("LineStandard")
    else:
        STD.SetInt("LineStandard", HAD_STD)
    close()
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
            close()
            # the next standard is still worth its own answer
            while STEPS and STEPS[0][1].__name__ != "make":
                STEPS.pop(0)
        advance()

    QtCore.QTimer.singleShot(delay, run)


advance()
