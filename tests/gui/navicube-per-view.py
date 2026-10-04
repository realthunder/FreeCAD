"""Each 3D view places its own navigation cube, and keeps it placed.

A view carries ShowNaviCube, NaviCubeX and NaviCubeY (View3DInventor.h):
the position as a fraction of the room the view leaves the cube, x from
the left and y from the top. The CornerNaviCube preference only seeds a
new view (docs/HeadlessServe.md sec 3.5).

What is asserted, on two views of one document:

  - both start at the preference corner;
  - moving one view's cube leaves the other's alone;
  - a click where a view's properties put its cube turns that view (a
    tilted Top view snaps to Top), and a click where the cube used to be
    does nothing -- so the pick zone moved with the drawing;
  - dragging a cube writes the dragged view's properties only, and no
    preference;
  - after a resize the cube answers at the same fraction;
  - save and reopen restores each view's position.

Run by hand in a live FreeCAD, e.g. through scripts/mcp_run.py, with
GT_OUT (or GT_RESULT) naming where the result lines go. Sets the View
preferences it depends on, so use an isolated FREECAD_USER_HOME.
"""
import math
import os
import tempfile
import traceback

import FreeCAD as App
import FreeCADGui as Gui
from PySide6 import QtCore, QtGui, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ.get("GT_OUT", tempfile.gettempdir())
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "NaviPerView"
LINES = []


def check(name, cond, detail=""):
    LINES.append(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def wait(ms=300):
    QTest.qWait(ms)


hGrp = App.ParamGet("User parameter:BaseApp/Preferences/View")
hNav = App.ParamGet("User parameter:BaseApp/Preferences/NaviCube")
SIZE = hNav.GetInt("CubeSize", 132)
hGrp.SetBool("ShowNaviCube", True)
hGrp.SetInt("CornerNaviCube", 1)
hGrp.SetBool("UseNavigationAnimations", False)


def viewport_of(title):
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if (w.metaObject().className() == "Gui::View3DInventor"
                and w.windowTitle().startswith(title)):
            return w.findChildren(QtWidgets.QGraphicsView)[0].viewport()
    return None


def expected_centre(view, title):
    """Where OverlayAnchor::cornerRect puts the cube, in widget coordinates."""
    vp = viewport_of(title)
    dpr = vp.devicePixelRatioF()
    w, h = int(round(vp.width() * dpr)), int(round(vp.height() * dpr))
    m = int(0.05 * SIZE)
    x = m + int(round(view.NaviCubeX * max(0, w - SIZE - 2 * m)))
    y = m + int(round(view.NaviCubeY * max(0, h - SIZE - 2 * m)))
    return vp, QtCore.QPointF((x + SIZE / 2.0) / dpr, (y + SIZE / 2.0) / dpr)


def mouse(vp, kind, pos, buttons):
    btn = QtCore.Qt.LeftButton if kind != QtCore.QEvent.MouseMove else QtCore.Qt.NoButton
    ev = QtGui.QMouseEvent(kind, pos, vp.mapToGlobal(pos.toPoint()), btn, buttons,
                           QtCore.Qt.NoModifier)
    QtWidgets.QApplication.sendEvent(vp, ev)


def click(vp, pos):
    mouse(vp, QtCore.QEvent.MouseMove, pos, QtCore.Qt.NoButton)
    wait(50)
    mouse(vp, QtCore.QEvent.MouseButtonPress, pos, QtCore.Qt.LeftButton)
    wait(50)
    mouse(vp, QtCore.QEvent.MouseButtonRelease, pos, QtCore.Qt.NoButton)
    wait(300)


def angle(a, b):
    return abs(math.degrees(a.inverted().multiply(b).Angle)) % 360


def tilted_top(view):
    """Top, tilted 20 deg: the cube's centre is still its Top face."""
    view.viewTop()
    wait(200)
    top = view.getCameraOrientation()
    view.setCameraOrientation(top.multiply(App.Rotation(App.Vector(1, 0, 0), 20)))
    wait(300)
    return top


def run():
    for name in list(App.listDocuments()):
        if name.startswith(DOC):
            App.closeDocument(name)
    doc = App.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    Gui.runCommand("Std_ViewCreate")
    wait()
    views = {v.getName().replace("[*]", ""): v
             for v in Gui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")}
    t1, t2 = DOC + " : 1", DOC + " : 2"
    if not check("two 3D views", t1 in views and t2 in views, sorted(views)):
        return
    v1, v2 = views[t1], views[t2]
    check("new views start at the preference corner",
          (v1.NaviCubeX, v1.NaviCubeY, v2.NaviCubeX, v2.NaviCubeY) == (1.0, 0.0, 1.0, 0.0),
          (v1.NaviCubeX, v1.NaviCubeY, v2.NaviCubeX, v2.NaviCubeY))
    wait(600)

    old_vp, old_pos = expected_centre(v1, t1)
    v1.NaviCubeX = 0.0
    v1.NaviCubeY = 1.0
    check("view 2 untouched by view 1's move", (v2.NaviCubeX, v2.NaviCubeY) == (1.0, 0.0),
          (v2.NaviCubeX, v2.NaviCubeY))
    wait(500)

    top = tilted_top(v1)
    before = v1.getCameraOrientation()
    click(old_vp, old_pos)
    check("view 1: a click where its cube was does nothing",
          angle(before, v1.getCameraOrientation()) < 0.01,
          angle(before, v1.getCameraOrientation()))
    vp, pos = expected_centre(v1, t1)
    click(vp, pos)
    check("view 1: a click where its properties put the cube turns to Top",
          angle(top, v1.getCameraOrientation()) < 0.5, angle(top, v1.getCameraOrientation()))

    top2 = tilted_top(v2)
    vp2, pos2 = expected_centre(v2, t2)
    click(vp2, pos2)
    check("view 2: its cube still answers at the top-right",
          angle(top2, v2.getCameraOrientation()) < 0.5, angle(top2, v2.getCameraOrientation()))

    # Drag view 2's cube to the middle of its view.
    target = QtCore.QPointF(vp2.width() / 2.0, vp2.height() / 2.0)
    mouse(vp2, QtCore.QEvent.MouseMove, pos2, QtCore.Qt.NoButton)
    wait(50)
    mouse(vp2, QtCore.QEvent.MouseButtonPress, pos2, QtCore.Qt.LeftButton)
    wait(50)
    for i in range(1, 11):
        p = pos2 + (target - pos2) * (i / 10.0)
        mouse(vp2, QtCore.QEvent.MouseMove, p, QtCore.Qt.LeftButton)
        wait(30)
    mouse(vp2, QtCore.QEvent.MouseButtonRelease, target, QtCore.Qt.NoButton)
    wait(400)
    check("a drag moves view 2's cube to the middle",
          abs(v2.NaviCubeX - 0.5) < 0.05 and abs(v2.NaviCubeY - 0.5) < 0.05,
          (round(v2.NaviCubeX, 3), round(v2.NaviCubeY, 3)))
    check("the drag left view 1 alone", (v1.NaviCubeX, v1.NaviCubeY) == (0.0, 1.0),
          (v1.NaviCubeX, v1.NaviCubeY))
    check("the drag left the preferences alone",
          hGrp.GetInt("CornerNaviCube", 1) == 1 and hNav.GetInt("OffsetX", 0) == 0,
          (hGrp.GetInt("CornerNaviCube", 1), hNav.GetInt("OffsetX", 0)))

    # A resize keeps the fraction.
    mw = Gui.getMainWindow()
    mw.showNormal()
    wait(300)
    mw.resize(mw.width() - 200, mw.height() - 120)
    wait(800)
    top2 = tilted_top(v2)
    vp2, pos2 = expected_centre(v2, t2)
    click(vp2, pos2)
    check("after a resize view 2's cube answers at its fraction",
          angle(top2, v2.getCameraOrientation()) < 0.5, angle(top2, v2.getCameraOrientation()))

    # Save and reopen: the positions ride GuiDocument.xml.
    x2, y2 = v2.NaviCubeX, v2.NaviCubeY
    path = os.path.join(OUT, DOC + ".FCStd")
    doc.saveAs(path)
    App.closeDocument(doc.Name)
    wait(300)
    doc = App.openDocument(path)
    wait(800)
    got = sorted((round(v.NaviCubeX, 3), round(v.NaviCubeY, 3))
                 for v in Gui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor"))
    want = sorted([(0.0, 1.0), (round(x2, 3), round(y2, 3))])
    check("save and reopen restores each view's position", got == want, (got, want))
    App.closeDocument(doc.Name)


try:
    run()
except Exception:
    LINES.append("FAIL exception | " + traceback.format_exc())
with open(RESULT, "w") as f:
    f.write("\n".join(LINES) + "\n")
