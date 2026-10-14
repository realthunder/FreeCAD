"""Three things of the overlaid panels asked 2026-10-14 (docs/HandsOnQueue.md):

Entry 120: "check what is gating the transparent overlay mode. with item
background feature, we can enable transparency mode in all cases" -- a panel
went opaque while the active view was a maximized one that cannot be panned
(a spreadsheet, a page). Claim: with a spreadsheet's view the active one and
the focus in the tree, its panel in transparent mode keeps the tree without
a ground of its own (an opaque panel gives it back). Offscreen the old
gate's condition is not met -- its sheet view is not seen as maximized --
so this claim held before the change too: it guards the eye, it does not
show the gate gone.

Entry 121: "check why the report view has a different background in overlay
non-transparency mode than non-overlay mode. Python console shares the same
background color" -- the overlay sheets gave every text view a grey of
their own. Claims, under Dark: the report view's ground overlaid with the eye
off is its ground docked; the console's too.

Entry 123: "for selection view in overlay mode, set the same background color
and alpha for its list column as the property editor". Claims, two lines in
the selection view, its overlay see-through: the first row's ground is the
items' ground, the second the palette's alternate colour, both at the
overlays' opacity, across the row's width.

Runs with QT_QPA_PLATFORM=offscreen; the pointer is not moved.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
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


def dock_of(widget):
    while widget is not None and not isinstance(widget, QtWidgets.QDockWidget):
        widget = widget.parentWidget()
    return widget


def ground(widget):
    """The commonest colour of a view's lower half, as its viewport paints
    itself: the colour its style sheet gives it (an overlay with its eye off
    reads clear when grabbed whole here)"""
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


def start():
    check("the Dark theme applies", FreeCADGui.applyTheme("Dark"))
    settle(2000)
    doc = FreeCAD.newDocument("PanelGrounds")
    doc.addObject("Part::Box", "Box")
    doc.addObject("Part::Cylinder", "Cylinder")
    doc.recompute()
    settle(800)
    for name in ("Report view", "Python console", "Selection view"):
        for dock in mw().findChildren(QtWidgets.QDockWidget):
            if dock.objectName() == name:
                dock.toggleViewAction().setChecked(True)
                dock.show()
    settle(1000)
    report = find("Gui::DockWnd::ReportOutput")
    console = find("Gui::PythonConsole")
    S["report"], S["console"] = report, console
    S["docked"] = (ground(report), ground(console))
    note("INFO docked: report %s, console %s" % S["docked"])


def overlaid_eye_off():
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)
    report, console = S["report"], S["console"]
    for w in (report, console):
        tab = overlay_of(w)
        if tab is not None:
            eye(tab, False)
    settle(800)
    for name, w, docked in (("report view", report, S["docked"][0]),
                            ("Python console", console, S["docked"][1])):
        tab = overlay_of(w)
        if tab is not None:
            front(tab, w)
        # the panel active (the focus in it): an opaque panel gives its
        # widgets their own ground back -- the look the entry is about; not
        # active, a panel shows them without one whatever its eye
        w.setFocus()
        settle(1200)
        got = ground(w)
        check("121: the %s's ground overlaid, eye off, is its ground docked" % name,
              overlay_of(w) is not None and got[:3] == docked[:3], "%s, docked %s" % (got, docked))


def selection_rows():
    FreeCADGui.Selection.clearSelection()
    doc = FreeCAD.ActiveDocument
    FreeCADGui.Selection.addSelection(doc.Name, "Box")
    FreeCADGui.Selection.addSelection(doc.Name, "Cylinder")
    settle(1000)
    holder = find("Gui::DockWnd::SelectionView")
    lists = [w for w in holder.findChildren(QtWidgets.QTreeWidget) if w.isVisible()] if holder else []
    view = lists[0] if lists else None
    tab = overlay_of(view)
    if not check("123: the selection view is overlaid, two lines in it",
                 tab is not None and view.topLevelItemCount() == 2,
                 view.topLevelItemCount() if view is not None else None):
        return
    front(tab, view)
    check("123: its overlay is see-through", eye(tab, True))
    settle(800)
    image = tab.grab().toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    image.save(os.path.join(OUT, "selection.png"))
    alpha = VIEW.GetInt("DockOverlayAlpha", 180)
    first = rgb_of(VIEW.GetUnsigned("BackgroundColor", 0x3C3C3CFF))
    if VIEW.GetBool("Gradient", False) or VIEW.GetBool("RadialGradient", False):
        note("INFO the background is a gradient here: the first row is not compared by colour")
        first = None
    alternate = view.palette().color(QtGui.QPalette.AlternateBase)
    alternate = (alternate.red(), alternate.green(), alternate.blue())
    for row, want in ((0, first), (1, alternate)):
        rect = view.visualItemRect(view.topLevelItem(row))
        header = view.header()
        right = header.sectionPosition(header.count() - 1) + header.sectionSize(header.count() - 1)
        seen = {}
        y = rect.center().y()
        for x in range(0, min(right, view.viewport().width()), 2):
            at = view.viewport().mapTo(tab, QtCore.QPoint(x, y))
            # what the narrow panel shows of the row, not what lies past it
            if not image.rect().contains(at) or at.x() >= tab.width() - 2:
                continue
            v = image.pixel(at)
            if (v >> 24) & 255 == 0:
                continue  # between the columns, nothing painted
            seen[v] = seen.get(v, 0) + 1
        top = sorted(seen, key=seen.get, reverse=True)[:3]
        note("INFO row %d at y %d, x 0..%d: the commonest %s" % (
            row + 1, y, min(right, view.viewport().width()),
            ["#%08x x%d" % (c, seen[c]) for c in top]))
        v = top[0] if top else 0
        got = ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)
        good = abs(got[3] - alpha) <= 6 and (
            want is None or max(abs(got[i] - want[i]) for i in range(3)) <= 6)
        check("123: row %d is backed across its columns, %s, at the overlays' opacity"
              % (row + 1, "the items' ground" if row == 0 else "the alternate colour"),
              good, "commonest %s for %s at %d" % (got, want, alpha))


def rgb_of(packed):
    return ((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255)


def spreadsheet():
    doc = FreeCAD.ActiveDocument
    sheet = doc.addObject("Spreadsheet::Sheet", "Sheet")
    doc.recompute()
    sheet.ViewObject.doubleClicked()
    settle(1500)
    views = [v for v in mw().getWindows() if "Sheet" in str(v) or "Spreadsheet" in str(v)]
    note("INFO the windows: %s" % [str(v) for v in mw().getWindows()])
    if views:
        mw().setActiveWindow(views[0])
        settle(1500)
    active = mw().getActiveWindow()
    if not check("120: a spreadsheet's view is the active one",
                 active is not None and "Sheet" in str(active), str(active)):
        return
    tree = find("Gui::TreeWidget")
    tab = overlay_of(tree)
    if not check("120: the tree is overlaid", tab is not None):
        return
    front(tab, tree)
    eye(tab, True)
    settle(1000)
    image = tab.grab().toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
    image.save(os.path.join(OUT, "tree-over-sheet.png"))
    clear = 0
    for y in range(0, image.height(), 4):
        for x in range(0, image.width(), 4):
            if (image.pixel(x, y) >> 24) & 255 == 0:
                clear += 1
    note("INFO %d clear sample points in the tree's overlay" % clear)
    # The eye tells once the panel is the active one (the focus in it): a
    # see-through panel keeps its widgets without a ground of their own
    # (OverlayOption::ShowTab), an opaque one gives them theirs back
    # (Disable). Over a maximized view that cannot be panned the eye was
    # overruled -- the gate of entry 120.
    tree.setFocus()
    settle(1500)
    check("120: over the spreadsheet's view, the tree's panel active, it stays see-through",
          tree.testAttribute(QtCore.Qt.WA_NoSystemBackground),
          "the tree without a ground of its own: %s" % tree.testAttribute(
              QtCore.Qt.WA_NoSystemBackground))


def finish():
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
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, overlaid_eye_off, selection_rows, spreadsheet]
QtCore.QTimer.singleShot(3000, advance)
