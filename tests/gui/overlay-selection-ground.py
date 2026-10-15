"""The selection view's ground in an overlay that is not see-through.

docs/HandsOnQueue.md entry 140 (the reporter, 2026-10-15 13:58): "make
selection view background the same as others (python console/report view)
in non-transparent overlay mode".

Every panel overlaid, the eye off on the selection view's and the report
view's overlays, the panel active (the focus in it). Each view's viewport is
rendered by itself and its commonest colour read, as entry 121's test reads
the report view's. Claims, under Dark and under Light:
  - the selection list's ground overlaid is the report view's ground
    overlaid;
  - docked, the selection list's ground is what it was before (unchanged).

Runs with QT_QPA_PLATFORM=offscreen; the pointer is not moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                out.append(w)
        except RuntimeError:
            pass
    return out


def overlay_of(w):
    while w is not None:
        if cls(w) == "Gui::OverlayTabWidget":
            return w
        w = w.parentWidget()
    return None


def eye(tab, on):
    for b in tab.findChildren(QtWidgets.QAbstractButton):
        if b.objectName() == "OBTN Transparent":
            if b.isChecked() != on:
                b.click()
                settle(1000)
            return b.isChecked() == on
    return False


def front(tab, widget):
    for i in range(tab.count()):
        if tab.widget(i) is not None and tab.widget(i).isAncestorOf(widget):
            tab.setCurrentIndex(i)
    settle(400)


def ground(widget):
    """The commonest colour of a view's lower half, as its viewport paints
    itself"""
    vp = widget.viewport()
    img = QtGui.QImage(vp.size(), QtGui.QImage.Format_ARGB32)
    img.fill(QtCore.Qt.transparent)
    vp.render(img)
    seen = {}
    for y in range(img.height() // 2, img.height(), 3):
        for x in range(0, img.width(), 3):
            v = img.pixel(x, y)
            seen[v] = seen.get(v, 0) + 1
    v = max(seen, key=seen.get) if seen else 0
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)


def selection_list():
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) != "QTreeWidget":
                continue
            p = w.parentWidget()
            while p is not None and cls(p) != "Gui::DockWnd::SelectionView":
                p = p.parentWidget()
            if p is not None:
                return w
        except RuntimeError:
            pass
    return None


def report_view():
    found = widgets("Gui::DockWnd::ReportOutput")
    return found[0] if found else None


def measure(theme):
    check("the %s theme applies" % theme, FreeCADGui.applyTheme(theme))
    settle(2500)
    if FreeCADGui.getMainWindow().findChild(QtWidgets.QWidget, "OverlayLeft") is None:
        pass
    sel, report = selection_list(), report_view()
    if not check("%s: a selection list and a report view" % theme,
                 sel is not None and report is not None, (sel, report)):
        return
    docked = ground(sel)
    note("INFO %s docked: the selection list %s, the report view %s" % (theme, docked, ground(report)))
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)
    sel, report = selection_list(), report_view()
    got = {}
    for name, w in (("selection list", sel), ("report view", report)):
        tab = overlay_of(w)
        if tab is not None:
            front(tab, w)
            eye(tab, False)
        w.setFocus()
        settle(1200)
        got[name] = ground(w)
    note("INFO %s overlaid, eye off: %s" % (theme, got))
    check("%s: overlaid, eye off, the selection list's ground is the report view's" % theme,
          got["selection list"][:3] == got["report view"][:3], got)
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)
    sel = selection_list()
    again = ground(sel) if sel is not None else None
    check("%s: docked again, the selection list's ground as before" % theme,
          again == docked, (again, docked))


def run():
    try:
        doc = FreeCAD.newDocument("SelectionGround")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(800)
        for name in ("Selection view", "Report view"):
            for dock in mw().findChildren(QtWidgets.QDockWidget):
                if dock.objectName() == name:
                    dock.toggleViewAction().setChecked(True)
                    dock.show()
                    dock.raise_()
        settle(800)
        FreeCADGui.Selection.addSelection(doc.Name, "Box")
        settle(500)
        measure("Dark")
        measure("Light")
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(300, mw().close)


QtCore.QTimer.singleShot(3000, run)
