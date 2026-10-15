"""The selection view's column header backed as the property editor's, overlaid.

docs/HandsOnQueue.md entry 138 (the reporter, 2026-10-15 11:58): "selection
view column header still has no background (all transparent) in overlay
transparent mode. I remember asked for the same as property editor column
header".

Dark theme, every panel overlaid, the property view and the selection view
see-through (the eye on), something selected. Each one's column header is
rendered by itself onto a clear image and its commonest colour read, the
section's ground. Claims:
  - the property editor's header has a ground (not clear);
  - the selection view's header has the same ground;
  - the same under the Light theme.

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


def find(name):
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                return w
        except RuntimeError:
            pass
    return None


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


def header_ground(view, tag):
    """The commonest colour of a view's header rendered alone on a clear
    image: its sections' ground, the text being the lesser part"""
    # The header by itself: its own sections. (Rendered through the panel's
    # overlay instead, offscreen, every pixel read clear for both headers.)
    header = view.header()
    img = QtGui.QImage(header.size(), QtGui.QImage.Format_ARGB32)
    img.fill(QtCore.Qt.transparent)
    header.render(img)
    img.save(os.path.join(OUT, tag + ".png"))
    seen = {}
    for y in range(2, max(3, img.height() - 2)):
        for x in range(2, img.width() - 2, 2):
            v = img.pixel(x, y)
            seen[v] = seen.get(v, 0) + 1
    v = max(seen, key=seen.get) if seen else 0
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)


def property_view():
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::PropertyEditor::PropertyEditor" and w.isVisible():
                return w
        except RuntimeError:
            pass
    return None


def selection_list():
    """The selection view's lists, each with whether it is shown: read once,
    guarded (a theme's switch leaves wrappers of widgets it deleted)"""
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) != "QTreeWidget":
                continue
            p = w.parentWidget()
            while p is not None and cls(p) != "Gui::DockWnd::SelectionView":
                p = p.parentWidget()
            if p is not None:
                out.append((w, w.isVisible()))
        except RuntimeError:
            pass
    return out


def measure(theme):
    check("the %s theme applies" % theme, FreeCADGui.applyTheme(theme))
    settle(2500)
    props = property_view()
    pairs = selection_list()
    lists = [w for w, _ in pairs]
    shown = [w for w, visible in pairs if visible]
    view = shown[0] if shown else (lists[0] if lists else None)
    if not check("%s: a property editor and a selection list" % theme,
                 props is not None and view is not None, (props, view)):
        return
    for w in (props, view):
        tab = overlay_of(w)
        if tab is not None:
            front(tab, w)
            eye(tab, True)
    settle(1000)
    got_props = header_ground(props, theme + "-property-header")
    got_sel = header_ground(view, theme + "-selection-header")
    note("INFO %s: the property editor's header %s, the selection view's %s"
         % (theme, got_props, got_sel))
    for i, w in enumerate(lists):
        if w is not view:
            note("INFO %s: the pick list's header %s" % (theme, header_ground(w, theme + "-pick-%d" % i)))
    check("%s: the property editor's header has a ground" % theme, got_props[3] > 0, got_props)
    check("%s: the selection view's header has the same ground" % theme,
          got_sel == got_props, (got_sel, got_props))


def run():
    try:
        doc = FreeCAD.newDocument("HeaderGrounds")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(800)
        for name in ("Selection view", "Property view", "Combo View"):
            for dock in mw().findChildren(QtWidgets.QDockWidget):
                if dock.objectName() == name:
                    dock.toggleViewAction().setChecked(True)
                    dock.show()
        settle(800)
        FreeCADGui.Selection.addSelection(doc.Name, "Box")
        settle(500)
        FreeCADGui.runCommand("Std_DockOverlayAll")
        settle(2500)
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
