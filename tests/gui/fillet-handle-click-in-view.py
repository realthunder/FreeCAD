"""A fillet's handles under the mouse: a click and a right click on an arrow.

docs/HandsOnQueue.md entry 135 (the reporter, 2026-10-15 09:46): "if the
fillet has missing reference, no arrow is shown ... when I click the arrow,
it registered, but when I release, the click pass through to select the face
behind". Changed at 11:30: "ditch the pressable item at tail of array, show
tooltip on hover the arrow to hint right click for context menu".

A 10 mm PartDesign box, a fillet on Edge1 and Edge2. In edit. The pixels of
an arrow are found by picking the scene (a ray pick at each pixel near the
edge's middle; the one with the most of the arrow around it), then clicked
through the window, as a hand would. Claims:
  - there is no button at the arrows' roots any more;
  - the pointer over an arrow shows a tool tip naming the right click;
  - a click on an arrow selects nothing behind it, and makes its value
    active (the arrow lit);
  - a right click on an arrow opens its menu (the edge's modes), not the
    view's, and selects nothing;
  - a fillet with a reference that is gone (Edge99): in edit, an arrow on
    each edge that is there.

Runs with QT_QPA_PLATFORM=offscreen.
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
DOC = "FilletHandleClick"

state = {"done": False, "doc": DOC}
# what the picks hit, a few of each kind, for the log
seen_paths = []


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
    return FreeCADGui.getDocument(state["doc"]).ActiveView


def viewer_widget():
    return view().graphicsView()


def field_text(node, name):
    value = node.getField(name).get()
    return value.getString() if hasattr(value, "getString") else str(value)


def shown_arrows():
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    count = 0
    for p in search.getPaths():
        # the arrow's own switch, and the container of every handle, which
        # is switched off as a whole
        on = True
        for i in range(p.getLength()):
            node = p.getNode(i)
            kind = node.getTypeId().getName().getString()
            if kind == "SoLinearDraggerContainer":
                on = on and field_text(node, "visible") in ("TRUE", "1")
            elif kind == "SoToggleSwitch":
                on = on and field_text(node, "on") in ("TRUE", "1")
        if on:
            count += 1
    return count


def arrow_colours():
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    colours = []
    for p in search.getPaths():
        container = p.getTail()
        if field_text(container, "visible") not in ("TRUE", "1"):
            continue
        dragger = container.getPart("dragger", False)
        if dragger is not None:
            colours.append(field_text(dragger, "color"))
    return colours


def button_colours():
    """The colour of every root button (an SoBaseColor beside an SoSphere)"""
    from pivy import coin

    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoSphere.getClassTypeId())
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    colours = []
    for p in search.getPaths():
        parent = p.getNodeFromTail(1)
        if not parent.isOfType(coin.SoGroup.getClassTypeId()):
            continue
        for i in range(parent.getNumChildren()):
            child = parent.getChild(i)
            if child.isOfType(coin.SoBaseColor.getClassTypeId()):
                colours.append(field_text(child, "rgb"))
    return colours


def pick_kind(x, y):
    """What a ray at viewport pixel (x, y) hits first: 'arrow', 'button',
    'other' or None"""
    from pivy import coin

    viewer = view().getViewer()
    region = viewer.getSoRenderManager().getViewportRegion()
    action = coin.SoRayPickAction(region)
    action.setPoint(coin.SbVec2s(int(x), int(y)))
    action.setRadius(1)
    action.apply(viewer.getSoRenderManager().getSceneGraph())
    picked = action.getPickedPoint()
    if picked is None:
        return None
    path = picked.getPath()
    names = [path.getNode(i).getTypeId().getName().getString() for i in range(path.getLength())]
    if len(seen_paths) < 6 and names[-3:] not in [s[-3:] for s in seen_paths]:
        seen_paths.append(names)
    # Coin's own types go without "So": a sphere is "Sphere"
    if any("LinearDragger" in n for n in names):
        return "arrow"
    if names and names[-1] in ("Sphere", "SoSphere"):
        return "button"
    return "other"


def find_pixels(center, reach=70, step=2):
    """Viewport pixels of an arrow and of a root button near `center`"""
    cx, cy = center
    found = {}
    arrows = []
    for r in range(0, reach, step):
        for dx in range(-r, r + 1, step):
            for dy in (-r, r) if abs(dx) != r else range(-r, r + 1, step):
                kind = pick_kind(cx + dx, cy + dy)
                if kind == "button" and kind not in found:
                    found[kind] = (cx + dx, cy + dy)
                elif kind == "arrow":
                    arrows.append((cx + dx, cy + dy))
    # The arrow is a pixel or two wide: the pixel with the most of the arrow
    # around it, so that a click a pixel off is still on it
    best = -1
    for x, y in arrows:
        score = sum(pick_kind(x + i, y + j) == "arrow" for i in (-2, -1, 0, 1, 2)
                    for j in (-2, -1, 0, 1, 2))
        if score > best:
            best = score
            found["arrow"] = (x, y)
    if arrows:
        note("INFO arrow pixels %d, the best with %d of 25 around it" % (len(arrows), best))
    return found


def to_window(x, y):
    """A viewport pixel as a point of the main window"""
    width, height = view().getSize()
    w = viewer_widget()
    local = QtCore.QPoint(int(x), int(height - y))
    g = w.viewport().mapToGlobal(local) if hasattr(w, "viewport") else w.mapToGlobal(local)
    return mw().mapFromGlobal(g)


def move_to(x, y):
    QtTest.QTest.mouseMove(mw().windowHandle(), to_window(x, y))
    pump(20)


def click(x, y, button=Qt.LeftButton):
    win = mw().windowHandle()
    p = to_window(x, y)
    QtTest.QTest.mouseMove(win, p)
    pump(5)
    QtTest.QTest.mousePress(win, button, Qt.NoModifier, p)
    pump(5)
    QtTest.QTest.mouseRelease(win, button, Qt.NoModifier, p)
    pump(40)


def selection():
    return [(s.ObjectName, list(s.SubElementNames)) for s in FreeCADGui.Selection.getSelectionEx("", 0)]


def make(name, edges):
    doc = FreeCAD.newDocument(name)
    body = doc.addObject("PartDesign::Body", "Body")
    box = body.newObject("PartDesign::AdditiveBox", "Box")
    box.Length = box.Width = box.Height = 10
    doc.recompute()
    fillet = body.newObject("PartDesign::Fillet", "Fillet")
    fillet.Base = (box, edges)
    fillet.Radius = 1
    doc.recompute()
    state["doc"] = name
    FreeCADGui.SendMsgToActiveView("ViewFit")
    FreeCADGui.activeDocument().activeView().viewIsometric()
    pump()
    return doc, box, fillet


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw().activateWindow()
        doc, box, fillet = make(DOC, ["Edge1", "Edge2"])
        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)

        edge = box.Shape.getElement("Edge1")
        middle = edge.valueAt((edge.FirstParameter + edge.LastParameter) / 2)
        center = view().getPointOnViewport(middle)
        found = find_pixels(center)
        note("INFO the edge's middle at %s of %s; found %s" % (tuple(center), view().getSize(), found))
        for names in seen_paths:
            note("INFO picked: %s" % " / ".join(names[-6:]))
        check("an arrow is found by picking", "arrow" in found, found)
        check("there is no button at the arrows' roots", "button" not in found, found)

        if "arrow" in found:
            QtWidgets.QToolTip.hideText()
            move_to(*found["arrow"])
            pump(40)
            tip = QtWidgets.QToolTip.text()
            note("INFO the tool tip over the arrow: %r" % tip)
            check("the pointer over an arrow shows a tool tip naming the right click",
                  QtWidgets.QToolTip.isVisible() and "right" in tip.lower(), tip)

            FreeCADGui.Selection.clearSelection()
            before = arrow_colours()
            click(*found["arrow"])
            check("a click on an arrow selects nothing behind it", selection() == [], selection())
            lit = arrow_colours()
            note("INFO arrows before %s, after the click %s" % (before, lit))
            check("the arrow clicked is lit", "1 1 0" in lit and lit != before, (before, lit))

            FreeCADGui.Selection.clearSelection()
            seen = {"items": None}

            def answer():
                for w in QtWidgets.QApplication.topLevelWidgets():
                    if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                        seen["items"] = [a.text().replace("&", "") for a in w.actions()
                                         if not a.isSeparator() and a.text()]
                        w.close()
                        return
                if seen.get("tries", 0) < 20:
                    seen["tries"] = seen.get("tries", 0) + 1
                    QtCore.QTimer.singleShot(100, answer)

            QtCore.QTimer.singleShot(100, answer)
            click(*found["arrow"], button=Qt.RightButton)
            pump(60)
            note("INFO the right click's menu: %s" % seen["items"])
            check("a right click on an arrow opens the edge's menu",
                  bool(seen["items"]) and "Default radius" in seen["items"], seen["items"])
            check("a right click on an arrow selects nothing", selection() == [], selection())
        check("still in edit", gdoc.getInEdit() is not None)
        gdoc.resetEdit()
        pump()
        FreeCAD.closeDocument(DOC)
        pump()

        # A reference that is gone: the arrows of the edges that are there
        doc, box, fillet = make(DOC + "Missing", ["Edge1", "Edge2"])
        fillet.Base = (box, ["Edge1", "Edge2", "Edge99"])
        doc.recompute()
        note("INFO the fillet with Edge99: valid %s" % fillet.isValid())
        gdoc = FreeCADGui.getDocument(state["doc"])
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("a reference gone: an arrow on each edge that is there", shown_arrows() == 2,
              shown_arrows())
        check("still in edit", gdoc.getInEdit() is not None)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def start_sweeper(seconds=60):
    """Every second: what top-level windows are up, in the log; a message box
    or a menu left open is closed, so that a step waiting on it goes on"""
    ticks = {"n": 0}

    def sweep():
        ticks["n"] += 1
        shown = []
        for w in QtWidgets.QApplication.topLevelWidgets():
            if w.isVisible() and w is not FreeCADGui.getMainWindow():
                shown.append("%s '%s'" % (w.metaObject().className(), w.windowTitle()))
                if isinstance(w, (QtWidgets.QMessageBox, QtWidgets.QMenu)):
                    note("INFO sweeper closes %s '%s': %s" % (
                        w.metaObject().className(), w.windowTitle(),
                        w.text() if isinstance(w, QtWidgets.QMessageBox) else ""))
                    w.close()
        if shown:
            note("INFO sweeper sees: %s" % shown)
        if ticks["n"] < seconds:
            QtCore.QTimer.singleShot(1000, sweep)

    QtCore.QTimer.singleShot(1000, sweep)


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.getDocument(state["doc"]).resetEdit()
        pump()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


start_sweeper()
QtCore.QTimer.singleShot(3000, run)
