"""A view cell's menu button and its two corner handles move out from under a
panel laid over the cell.

docs/HandsOnQueue.md entry 83: "view cell menu and handle should be aware of
overlay panel position and slide out to make accessible just like navicube";
and as answered: "only view cell menu and two corner handle. only if there
are actually areas not blocked by overlay panel. If they are fully covered or
less than 40 in dimension, don't bother. check separately for each
handle/menu", "change that 40 pixels to twice the size of the handle. in
short, when they slide, there shouldn't be overlap among handles"; always, no
setting; every cell.

Claims, one 3D view in one cell:
  - with the docks docked the menu button is in the cell's top left corner,
    one handle in its top right and one in its bottom left;
  - the docks overlaid, a dock over the cell's top left corner: the menu
    button is shown clear of every dock, inside the cell, and what is under
    its middle is the button itself -- it can be reached;
  - each handle whose corner a dock covers is clear of every dock too, and
    one whose corner no dock covers has not moved from its corner;
  - no two of the three overlap;
  - the docks docked again: all three are back in their corners.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
STEPS = []
S = {}
NAMES = ("menu button", "top right handle", "bottom left handle")


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
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def all_widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                out.append(w)
        except RuntimeError:
            pass
    return out


def on_screen(w):
    return QtCore.QRect(w.mapToGlobal(QtCore.QPoint(0, 0)), w.size())


def docks():
    out = {}
    for w in all_widgets("Gui::OverlayTabWidget"):
        if w.isVisible() and w.count():
            out[w.objectName().replace("Overlay", "")] = on_screen(w)
    return out


def corners(cell, size):
    """Where the three are at home, on the screen: the cell's corners."""
    r = on_screen(cell)
    return (QtCore.QRect(r.left(), r.top(), size[0], size[0]),
            QtCore.QRect(r.right() + 1 - size[1], r.top(), size[1], size[1]),
            QtCore.QRect(r.left(), r.bottom() + 1 - size[2], size[2], size[2]))


def at_home(tag):
    cell = S["cell"]
    home = corners(cell, [w.width() for w in S["three"]])
    for name, w, h in zip(NAMES, S["three"], home):
        check("%s: the %s is in its corner" % (tag, name), on_screen(w) == h,
              (on_screen(w).getRect(), h.getRect()))


def start():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("AutoSaveTimeout", 0)
    VIEW.SetBool("UseViewArea", True)
    doc = FreeCAD.newDocument("CellHandles")
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    settle(1000)
    cells = [c for c in all_widgets("Gui::ViewAreaCell") if c.isVisible()]
    if not check("one cell shows the view", len(cells) == 1, len(cells)):
        raise RuntimeError("not one cell")
    cell = cells[0]
    menu = cell.findChild(QtWidgets.QWidget, "ViewAreaMenuButton")
    zones = [w for w in cell.children()
             if isinstance(w, QtWidgets.QWidget) and cls(w) == "Gui::ViewAreaZone"]
    if not check("it has a menu button and two handles", menu is not None and len(zones) == 2,
                 (menu is not None, len(zones))):
        raise RuntimeError("the cell's handles were not found")
    zones.sort(key=lambda w: -w.x())  # the top right one first
    S["cell"] = cell
    S["three"] = [menu] + zones


def docked():
    check("no dock is overlaid", docks() == {}, list(docks()))
    at_home("docked")
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)


def overlaid():
    d = docks()
    cell = S["cell"]
    area = on_screen(cell)
    home = corners(cell, [w.width() for w in S["three"]])
    note("the cell %s; docks %s" % (area.getRect(), {s: r.getRect() for s, r in d.items()}))
    if not check("a dock is laid over the cell's top left corner",
                 any(r.intersects(home[0]) for r in d.values()), sorted(d)):
        raise RuntimeError("no dock over the menu button's corner: nothing to move out of")
    rects = []
    for name, w, h in zip(NAMES, S["three"], home):
        r = on_screen(w)
        rects.append(r)
        covered = [s for s, dr in d.items() if dr.intersects(h)]
        if covered:
            hit = [s for s, dr in d.items() if dr.intersects(r)]
            check("the %s is shown clear of every dock" % name, not hit,
                  (r.getRect(), hit, covered))
            check("and inside the cell", area.contains(r), (r.getRect(), area.getRect()))
        else:
            check("the %s, its corner not covered, has not moved" % name, r == h,
                  (r.getRect(), h.getRect()))
    middle = rects[0].center()
    under = QtWidgets.QApplication.widgetAt(middle)
    menu = S["three"][0]
    check("what is under the menu button's middle is the button",
          under is not None and (under is menu or menu.isAncestorOf(under)),
          (middle.x(), middle.y(), cls(under) if under else None,
           under.objectName() if under else None))
    pairs = [(NAMES[i], NAMES[j]) for i in range(3) for j in range(i + 1, 3)
             if rects[i].intersects(rects[j])]
    check("no two of the three overlap", not pairs, pairs)
    mw().grab().save(os.path.join(OUT, "overlaid.png"))
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)


def docked_again():
    check("no dock is overlaid any more", docks() == {}, list(docks()))
    at_home("docked again")


def finish():
    try:
        if docks():
            FreeCADGui.runCommand("Std_DockOverlayAll")
            settle(1500)
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


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
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, docked, overlaid, docked_again]
QtCore.QTimer.singleShot(2500, advance)
