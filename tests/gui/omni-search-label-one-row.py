"""The omni search lists an object once, its label in the description; Tab
takes the <<label>> where the typing is the label's; /error lists the objects
in error.

docs/HandsOnQueue.md entry 165 (the reporter, 2026-10-16): "do not show the
same object with their name and label in separate items ... put the lable,
with << and >> in the description if they are different. Note, when user
typing starts with << and then tab, or the user typing matched the label that
is different from name even without leading <<, tab complete shall use the
<<labe_name>> instead of internal name"; "in omni search add a new type
/error to filter on all objects marked as recompute error".

A box Crate labelled "Lid", a cylinder Pillar, and a cut with nothing to cut,
in error. Claims:
  - "/ Li": one row for Crate, its description led by <<Lid>>;
  - Tab on "/ Li": "/ <<Lid>>"; on "/ Cra": "/ Crate"; on "/ <<L": "/ <<Lid>>";
  - "/error ": the cut is listed, Crate and Pillar are not.

Run with QT_QPA_PLATFORM=offscreen, as omni-search-object-typing.py.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
QTest = QtTest.QTest
DOC = "OmniLabelRow"
STEPS = []
DESCRIPTION = QtCore.Qt.UserRole + 2


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def mw():
    return FreeCADGui.getMainWindow()


def named(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def find_key(widget):
    """Ctrl+F delivered to a widget"""
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            widget, QtGui.QKeyEvent(kind, QtCore.Qt.Key_F, QtCore.Qt.ControlModifier, "\x06"))
    settle(400)


def box_text():
    box = named("OmniSearchBox")
    edit = named("OmniSearchEdit")
    if box is None or edit is None or not box.isVisible():
        return None
    return edit.text()


def popup():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            return w
    return None


def popup_rows():
    view = popup()
    if view is None:
        return []
    model = view.model()
    return [str(model.index(i, 0).data()) for i in range(model.rowCount())]


def focus_box():
    """The box the active window and its line with the focus: the lists come
    up only for a line that has it, and a window opened from a script is not
    made the active one by this platform"""
    box = named("OmniSearchBox")
    edit = named("OmniSearchEdit")
    if box is None or edit is None:
        return
    if not edit.hasFocus() or QtWidgets.QApplication.activeWindow() is not box:
        box.activateWindow()
        box.raise_()
        handle = box.windowHandle()
        if handle is not None:
            handle.requestActivate()
        edit.setFocus(QtCore.Qt.OtherFocusReason)
        settle(300)


def typed(text):
    """The text as if typed: set, and said to have been edited"""
    focus_box()
    edit = named("OmniSearchEdit")
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(500)


def dismiss():
    box = named("OmniSearchBox")
    if box is not None and box.isVisible():
        box.hide()
    settle(300)


def tree_view():
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if w.metaObject().className() == "Gui::TreeWidget" and w.isVisible():
                return w
        except RuntimeError:
            pass
    return None


def has_focus(widget):
    w = QtWidgets.QApplication.focusWidget()
    return w is not None and (w is widget or widget.isAncestorOf(w))


def start():
    platform = QtGui.QGuiApplication.platformName()
    if platform != "offscreen":
        note("NOT RUN: the platform is %r; run with QT_QPA_PLATFORM=offscreen" % platform)
        STEPS[:] = []
        return
    doc = FreeCAD.newDocument(DOC)
    crate = doc.addObject("Part::Box", "Crate")
    crate.Label = "Lid"
    doc.addObject("Part::Cylinder", "Pillar")
    doc.addObject("Part::Cut", "Broken")
    doc.recompute()
    note("INFO in error: %s" % [o.Name for o in doc.Objects if "Invalid" in o.State])
    settle(800)
    tree = tree_view()
    tree.setFocus()
    find_key(tree)
    named("OmniSearchEdit").setFocus()
    settle(300)


def rows_of(text):
    typed(text)
    view = popup()
    if view is None:
        return None, []
    model = view.model()
    rows = []
    for i in range(model.rowCount()):
        index = model.index(i, 0)
        rows.append((str(index.data()), str(index.data(DESCRIPTION) or "")))
    return view, rows


def one_row():
    view, rows = rows_of("/ Li")
    crates = [r for r in rows if r[0] == "Crate"]
    check("'/ Li': one row for Crate", len(crates) == 1, rows[:6])
    check("... its description led by <<Lid>>",
          bool(crates) and crates[0][1].startswith("<<Lid>>"), crates[:1])


def tab(text, expected):
    view, rows = rows_of(text)
    if not check("%r typed: a list is up" % text, view is not None, rows[:4]):
        return
    QTest.keyClick(view, QtCore.Qt.Key_Tab)
    settle(600)
    check("Tab on %r: %r" % (text, expected), box_text() == expected, box_text())


def tabs():
    tab("/ Li", "/ <<Lid>>")
    tab("/ Cra", "/ Crate")
    tab("/ <<L", "/ <<Lid>>")


def errors():
    view, rows = rows_of("/error ")
    names = [r[0] for r in rows]
    check("'/error ': the cut in error is listed", any(n.endswith("Broken") for n in names), rows[:6])
    check("... and nothing that is not in error",
          not any(n.endswith("Crate") or n.endswith("Pillar") for n in names), names)


def finish():
    dismiss()
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
    QtCore.QTimer.singleShot(600, advance)


STEPS[:] = [start, one_row, tabs, errors]
QtCore.QTimer.singleShot(2500, advance)
