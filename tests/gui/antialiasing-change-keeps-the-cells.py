"""A change of the anti-aliasing setting leaves a split view as it is.

docs/HandsOnQueue.md entry 50, its second part: "then I tested changing
msaa setting. the view with two splits, one 3d, one techdraw turned into two
tab window". A 3D view that Coin draws (the render type "Legacy"; and, when
this was reported, any view after "Reset all" had taken the render engine
away) cannot change its sample count in place: the setting's handler makes
a copy of the view on a new surface and deletes the old one. The copy was
handed to the main window as a new tab, and the cell the old view sat in
went with it. The copy takes the old view's cell now.

Claims, a document with a 3D view and a TechDraw page in two cells side by
side, the anti-aliasing changed to MSAA 4x and back:
  - under the render type "Legacy", after each change: the two cells are
    there, 3D and page, and the main window has as many tabs as before;
    the 3D view is a NEW one (the measurement would pass on a handler that
    did nothing);
  - under "Default", the render engine: the same, the view the same one
    (the engine changes its sample count in place).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import shiboken6
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "AntiAliasCells"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
PAGE_VIEW = "TechDrawGui::MDIViewPage"
VIEW_3D = "Gui::View3DInventor"
STEPS = []
SEEN = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def cells():
    """What each cell on screen holds, left to right: "3D", "page" or "empty" """
    res = []
    for button in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget,
                                                          "ViewAreaMenuButton"):
        cell = button.parentWidget()
        if cell is None or not cell.isVisible():
            continue
        what = "empty"
        for w in cell.findChildren(QtWidgets.QMainWindow):
            if not shiboken6.isValid(w) or not w.isVisible():
                continue
            name = w.metaObject().className()
            if name == VIEW_3D:
                what = "3D"
            elif name == PAGE_VIEW:
                what = "page"
            else:
                continue
            break
        res.append((cell.mapToGlobal(QtCore.QPoint(0, 0)).x(), what))
    res.sort(key=lambda r: r[0])
    return [k for _, k in res]


def tabs():
    mdi = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
    return len(mdi.subWindowList()) if mdi else -1


def view_3d():
    """The 3D view on screen, as something that tells one from its copy"""
    found = []
    for w in QtWidgets.QApplication.allWidgets():
        if shiboken6.isValid(w) and w.metaObject().className() == VIEW_3D and w.isVisible():
            found.append(shiboken6.getCppPointer(w)[0])
    return found


def backend():
    views = FreeCADGui.getDocument(DOC).mdiViewsOfType(VIEW_3D)
    try:
        views[0].getRenderStats()
    except RuntimeError as e:
        if "No external renderer" in str(e):
            return False
    return True


def legacy():
    RENDER.SetString("Type", "Legacy")
    VIEW.SetInt("AntiAliasing", 0)


def make():
    import TechDrawGui  # noqa: F401  the view providers
    doc = FreeCAD.newDocument(DOC)
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
    FreeCADGui.getDocument(DOC).getObject("Page").doubleClicked()


def remember(tag):
    def fn():
        SEEN[tag] = (cells(), tabs(), view_3d())
        note("NOTE %s: cells %s, %d tabs, backend %s" % (tag, SEEN[tag][0], SEEN[tag][1],
                                                         backend()))
        if tag == "Legacy":
            check("Legacy: the page opened beside the 3D view, which has no backend",
                  sorted(SEEN[tag][0]) == ["3D", "page"] and not backend(), SEEN[tag][0])
        else:
            check("Default: two cells, and the 3D view has a backend",
                  sorted(SEEN[tag][0]) == ["3D", "page"] and backend(), SEEN[tag][0])
    fn.__name__ = "remember"
    return fn


def set_aa(value):
    def fn():
        VIEW.SetInt("AntiAliasing", value)
    fn.__name__ = "set_aa"
    return fn


def look(tag, what, copied):
    def fn():
        was_cells, was_tabs, was_view = SEEN[tag]
        now = view_3d()
        check("%s, %s: the two cells are there" % (tag, what), cells() == was_cells,
              "%s, %s before" % (cells(), was_cells))
        check("%s, %s: as many tabs as before" % (tag, what), tabs() == was_tabs,
              "%d, %d before" % (tabs(), was_tabs))
        check("%s, %s: one 3D view on screen" % (tag, what), len(now) == 1, len(now))
        if copied:
            check("%s, %s: it is a copy of the view (the change was carried out)" % (tag, what),
                  now != was_view)
        else:
            check("%s, %s: it is the same view (changed in place)" % (tag, what),
                  now == was_view)
        SEEN[tag] = (was_cells, was_tabs, now)
    fn.__name__ = "look"
    return fn


def to_default():
    RENDER.SetString("Type", "Default")


def finish():
    RENDER.RemString("Type")
    VIEW.SetInt("AntiAliasing", 0)
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
            note("FAIL the step %s ran | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((2000, legacy))
STEPS.append((1500, make))
STEPS.append((5000, remember("Legacy")))
STEPS.append((500, set_aa(3)))
STEPS.append((3000, look("Legacy", "to MSAA 4x", True)))
STEPS.append((500, set_aa(0)))
STEPS.append((3000, look("Legacy", "back to none", True)))
STEPS.append((500, to_default))
STEPS.append((3000, remember("Default")))
STEPS.append((500, set_aa(3)))
STEPS.append((3000, look("Default", "to MSAA 4x", False)))
STEPS.append((500, set_aa(0)))
STEPS.append((3000, look("Default", "back to none", False)))
advance()
