"""A TechDraw colour written after a page's view is gone does not end the
session.

docs/HandsOnQueue.md entry 114: "new crash ... I am switching theme to dark
using Tools -> Preset configurations -> Themes -> Dark theme" -- an access
violation in Base::Subject<const char*>::Notify, under
ParameterGrp::SetUnsigned of "PreSelectColor" in Mod/TechDraw/Colors, as the
theme's values were written.

A page's view watches three parameter groups and took itself off two of them
when it went: the third, TechDraw's Colors (watched since the page view's
ground follows the 3D view's, entry 112), kept calling an observer that was
no longer there, at the next write of any key in that group. A theme writes
them; so does TechDraw's own colour page.

Claims:
  - a page's view opened and its document closed: the view is gone;
  - a key of Mod/TechDraw/Colors written after that: the session is still
    there to say so (the fault ended it at this line);
  - the same with a theme applied, which is how it was met.

Runs with QT_QPA_PLATFORM=offscreen as well as on a screen; the pointer is not
moved. A session that ends at the second claim leaves no DONE in the result.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
COLORS = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/TechDraw/Colors")
STEPS = []
S = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def page_views():
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget)
            if w.metaObject().className() == "TechDrawGui::QGVPage"]


def open_page(name):
    doc = FreeCAD.newDocument(name)
    box = doc.addObject("Part::Box", "Box")
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(
        FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates", "A4_Landscape_blank.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    page.addView(view)
    view.Source = [box]
    doc.recompute()
    page.ViewObject.doubleClicked()
    settle(1500)
    return doc


def first():
    S["key"] = COLORS.GetUnsigned("PreSelectColor", 0)
    S["had"] = "PreSelectColor" in dict((k, v) for _, k, v in COLORS.GetContents() or [])
    doc = open_page("ColourAfterClose")
    S["views"] = len(page_views())
    check("a page's view is open", S["views"] >= 1, S["views"])
    FreeCAD.closeDocument(doc.Name)


def second():
    # (a step of its own: the view is deleted when the event loop comes round)
    settle(800)
    check("its document closed: the view is gone", len(page_views()) == 0, len(page_views()))
    COLORS.SetUnsigned("PreSelectColor", 0x11223300)
    settle(300)
    check("a TechDraw colour written after that: the session goes on",
          COLORS.GetUnsigned("PreSelectColor", 0) == 0x11223300)


def third():
    doc = open_page("ThemeAfterClose")
    check("a second page's view is open", len(page_views()) >= 1, len(page_views()))
    FreeCAD.closeDocument(doc.Name)


def fourth():
    settle(800)
    ok = FreeCADGui.applyTheme("Dark")
    settle(2500)
    check("a theme applied after a page's view is gone: the session goes on", ok is not False, ok)
    ok = FreeCADGui.applyTheme("Light")
    settle(2500)
    check("and another", ok is not False, ok)


def finish():
    if S.get("had"):
        COLORS.SetUnsigned("PreSelectColor", S["key"])
    else:
        COLORS.RemUnsigned("PreSelectColor")
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(1000, advance)


STEPS[:] = [first, second, third, fourth]
QtCore.QTimer.singleShot(3000, advance)
