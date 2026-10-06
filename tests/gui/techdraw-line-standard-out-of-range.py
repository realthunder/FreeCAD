"""A TechDraw view comes up with the line standard preference out of range.

The preference Mod/TechDraw/Standards/LineStandard is an index into the
line standards found, and everything that reads it indexed with it as it
was. The annotation preference page stored -1 in it: refilling its list on
a language change emptied the combo box, whose "no current item" went
straight into the parameter, and reading the definitions of standard -1
then threw before the index was put back. From then on every view of a
part threw "invalid vector subscript" out of its view provider's
constructor. Opening a document, the progressive restore gave up on the
first of them and left every object after it without its saved view
properties (docs/HandsOnQueue.md entry 18).

Claims:
  - with the preference at -1 and at 99, a view of a part gets its view
    provider, and its page claims it;
  - the annotation preference page, where it can be made outside the
    preferences dialog, keeps a standard through two language changes (a
    NOTE says so when it cannot be made, and the claim is not scored).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "LineStandard"
GROUP = "User parameter:BaseApp/Preferences/Mod/TechDraw/Standards"


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


def view_of_a_box(tag):
    doc = FreeCAD.newDocument(DOC)
    try:
        box = doc.addObject("Part::Box", "Box")
        page = doc.addObject("TechDraw::DrawPage", "Page")
        template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
        template.Template = os.path.join(
            FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
        page.Template = template
        view = doc.addObject("TechDraw::DrawViewPart", "View")
        view.Source = [box]
        page.addView(view)
        doc.recompute()
        settle()
        gdoc = FreeCADGui.getDocument(DOC)
        vp = gdoc.getObject("View")
        check("%s: the view has its view provider" % tag, vp is not None,
              type(vp).__name__)
        claimed = [c.Name for c in gdoc.getObject("Page").claimChildren()]
        check("%s: the page claims the view" % tag, "View" in claimed, claimed)
    finally:
        FreeCAD.closeDocument(DOC)
        settle()


def run():
    param = FreeCAD.ParamGet(GROUP)
    had = param.GetInt("LineStandard", 12345)
    try:
        import TechDrawGui  # the view providers
        for value in (-1, 99):
            param.SetInt("LineStandard", value)
            view_of_a_box("LineStandard %d" % value)

        from PySide import QtWidgets
        page = None
        try:
            ui = FreeCADGui.UiLoader()
            page = ui.createWidget("TechDrawGui::DlgPrefsTechDrawAnnotationImp")
        except Exception:
            page = None
        if page is None:
            note("NOTE the annotation preference page cannot be made from here;"
                 " its slot is not tried")
        else:
            param.SetInt("LineStandard", 0)
            combo = page.findChild(QtWidgets.QComboBox, "pcbLineStandard")
            QtWidgets.QApplication.sendEvent(page, QtCore.QEvent(QtCore.QEvent.LanguageChange))
            settle()
            QtWidgets.QApplication.sendEvent(page, QtCore.QEvent(QtCore.QEvent.LanguageChange))
            settle()
            check("the preference page keeps a standard through a language change",
                  param.GetInt("LineStandard", 12345) >= 0 and combo.currentIndex() >= 0,
                  (param.GetInt("LineStandard", 12345), combo.currentIndex()))
            page.deleteLater()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        if had == 12345:
            param.RemInt("LineStandard")
        else:
            param.SetInt("LineStandard", had)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
