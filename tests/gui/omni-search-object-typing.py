"""The omni search on an object: opened from a selected tree item, names
corrected at a dot, Tab completing without taking the typing away.

docs/HandsOnQueue.md entry 93: "when Ctrl+F and tree view has any selection
... launch omni search with the object name pre typed" -- "/ " then the name
and a dot; the internal name; the path from the top; of several selected:
"tree's last selection if it is in view, or else the last selection in
view, or else the current item, or else the last selection".

Entry 95: "when type object name in small case and find one match with
captial start letter, press dot will resolve its property bot still leave
the typed object name small case. should auto correct object name to the
resolved object" -- at the dot whenever the property list is shown, at each
step.

Entry 94: "when use Tab to complete in omni search resolving a property, the
edit box lost its focus and cannot type anymore." Tab took the row as a
click would, and a property picked by a click gets its editor and the
focus. Tab completes what is being typed; Return hands the focus over.

Run with QT_QPA_PLATFORM=offscreen: who has the focus is asked, and beside
other windows on a real screen that is not this program's to say.
The box stays open from one step to the next: hidden and shown again it is
not made the active window by that platform, and its lists come up only for
a line that has the focus.

Claims:
  - nothing selected: Ctrl+F in the tree opens the box with "/ ";
  - an object inside a container selected: "/ Group.Crate." -- its path from
    the top by internal names -- and the list has its properties;
  - two selected, both in view: the one selected last;
  - "/ crate." typed: the text reads "/ Crate." and the list has Length;
    "/ pillar.": "/ Pillar."; and where no list comes up -- a further step
    typed small, "/ Crate.placement.", is not resolved by the completer
    today -- the text stays as typed;
  - "/ Crate.Len" and Tab: the text reads "/ Crate.Length", the focus is
    still in the box, and a letter typed after it lands in the box;
  - Return on "/ Crate.Length": the focus leaves the box for the editor of
    the value.
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
DOC = "OmniTyping"
STEPS = []


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
        note("NOT RUN: the platform is %r; this script asks who has the focus and runs only "
             "with QT_QPA_PLATFORM=offscreen" % platform)
        STEPS[:] = []
        return
    doc = FreeCAD.newDocument(DOC)
    group = doc.addObject("App::Part", "Group")
    crate = doc.addObject("Part::Box", "Crate")
    group.addObject(crate)
    doc.addObject("Part::Cylinder", "Pillar")
    doc.addObject("Part::Sphere", "Ball")
    doc.recompute()
    settle(800)
    tree = tree_view()
    if tree is not None:
        tree.expandAll()
    settle(400)


def from_the_tree():
    tree = tree_view()
    if not check("the tree view is shown", tree is not None):
        return
    FreeCADGui.Selection.clearSelection()
    settle(200)
    tree.setFocus()
    find_key(tree)
    check("the box's line has the focus", has_focus(named("OmniSearchEdit")))
    check("nothing selected: Ctrl+F in the tree opens the box with '/ '",
          box_text() == "/ ", box_text())

    FreeCADGui.Selection.addSelection(DOC, "Group", "Crate.")
    settle(300)
    tree.setFocus()
    find_key(tree)
    check("an object in a container selected: its path from the top and a dot",
          box_text() == "/ Group.Crate.", box_text())
    typed(box_text())
    rows = popup_rows()
    check("and the list has that object's properties", "Length" in rows, rows[:8])

    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(DOC, "Pillar")
    FreeCADGui.Selection.addSelection(DOC, "Ball")
    settle(300)
    tree.setFocus()
    find_key(tree)
    check("two selected, both in view: the one selected last", box_text() == "/ Ball.", box_text())
    FreeCADGui.Selection.clearSelection()


def names_corrected():
    tree = tree_view()
    tree.setFocus()
    find_key(tree)
    typed("/ crate.")
    check("'/ crate.' typed: the text reads '/ Crate.'", box_text() == "/ Crate.", box_text())
    rows = popup_rows()
    check("and the list has the object's properties", "Length" in rows, rows[:8])
    typed("/ pillar.")
    check("'/ pillar.' typed: the text reads '/ Pillar.'", box_text() == "/ Pillar.", box_text())
    # Where no list comes up the text has not resolved and stays as typed:
    # a further step typed small is one, today
    typed("/ Crate.placement.")
    if popup() is None:
        check("no list for '/ Crate.placement.': the text stays as typed",
              box_text() == "/ Crate.placement.", box_text())
    else:
        check("a list for '/ Crate.placement.': the text reads '/ Crate.Placement.'",
              box_text() == "/ Crate.Placement.", box_text())


def tab_completes():
    tree = tree_view()
    tree.setFocus()
    find_key(tree)
    edit = named("OmniSearchEdit")
    box = named("OmniSearchBox")
    edit.setFocus()
    settle(200)
    typed("/ Crate.Len")
    view = popup()
    if not check("'/ Crate.Len' typed: a list is up", view is not None, popup_rows()[:5]):
            return
    QTest.keyClick(view, QtCore.Qt.Key_Tab)
    settle(600)
    check("Tab: the text reads '/ Crate.Length'", box_text() == "/ Crate.Length", box_text())
    check("the focus is still in the box's line", has_focus(edit),
          QtWidgets.QApplication.focusWidget().metaObject().className()
          if QtWidgets.QApplication.focusWidget() else None)
    target = QtWidgets.QApplication.focusWidget()
    if target is not None:
        QTest.keyClicks(target, "x")
        settle(300)
    check("a letter typed after it lands in the box", box_text() == "/ Crate.Lengthx", box_text())

    typed("/ Crate.Length")
    for w in (popup(),):
        if w is not None:
            w.hide()
    settle(200)
    edit.setFocus()
    QTest.keyClick(edit, QtCore.Qt.Key_Return)
    settle(700)
    check("Return on '/ Crate.Length': the box is still up", box is not None and box.isVisible())
    check("and the focus has left its line for the editor of the value",
          box is not None and has_focus(box) and not has_focus(edit),
          QtWidgets.QApplication.focusWidget().metaObject().className()
          if QtWidgets.QApplication.focusWidget() else None)


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


STEPS[:] = [start, from_the_tree, names_corrected, tab_completes]
QtCore.QTimer.singleShot(2500, advance)
