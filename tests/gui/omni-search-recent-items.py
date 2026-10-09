"""The omni search lists the items last confirmed in it when it comes up.

docs/HandsOnQueue.md entry 62: "omni search, keep the last 10 confirmed
searched items on the list when it first pop up. once typing is going, then
no need for those recent list".

Confirmed is carried out: a command run from the box, a parameter's editor
opened, an object selected or a property's editor opened. The box comes up
with a lone "/": the items first, the three modes after them, the newest
first, each once, ten at most, and they are gone as soon as anything more is
typed. Picking one carries it out again. They are kept in the user
parameters, so from one session to the next.

Claims:
  - with nothing confirmed yet the box lists the three modes and no more;
  - a command run from the box is listed the next time, by its title;
  - a parameter opened from the box is listed above it, and a property
    opened from the box above that: the newest first;
  - the command run again moves to the top and is listed once;
  - Return on the box as it comes up carries out the item at the top again
    ("omni search recent items come before the three modes");
  - "/c" lists none of them: the list is the query's own;
  - picking the command's row runs it; picking the parameter's opens its
    editor; picking the property's puts its text in the box and opens its
    editor;
  - with the document closed the property is not listed (it names nothing
    now) and the other two are;
  - three items are stored, under Preferences/OmniSearch/Recent.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "OmniRecent"
MODES = ["/ ", "/cmd ", "/param "]
COMMAND = "Std_SelectAll"
STORE = "User parameter:BaseApp/Preferences/OmniSearch/Recent"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=40):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def key(widget, which):
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(widget, QtGui.QKeyEvent(kind, which, QtCore.Qt.NoModifier))


def popup():
    """The completer popup on screen: (view, the texts of its rows)"""
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            model = w.model()
            return w, [str(model.index(i, 0).data()) for i in range(model.rowCount())]
    return None, []


def named(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def shown(name):
    w = named(name)
    return w is not None and w.isVisible()


def bring_up():
    """The box brought up as the command does it; the rows it comes up with"""
    FreeCADGui.runCommand("Std_OmniSearch")
    settle()
    edit = named("OmniSearchEdit")
    if edit is None or not edit.isVisible():
        raise RuntimeError("the box did not come up")
    # the lone slash is put there once the edit has the focus, which a test
    # run beside other windows may never be given: say it as the box does
    if edit.text() != "/":
        typed(edit, "/")
    return edit, popup()


def typed(edit, text):
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle()


def dismiss():
    box = named("OmniSearchBox")
    if box is not None and box.isVisible():
        box.hide()
    settle()


def pick(view, rows, text):
    """Down to the row and Tab on it, as a user at the keyboard"""
    if text not in rows:
        return False
    for _ in range(rows.index(text)):
        key(view, QtCore.Qt.Key_Down)
    settle()
    key(view, QtCore.Qt.Key_Tab)
    settle()
    return True


def run():
    try:
        FreeCAD.ParamGet(STORE).Clear()
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Crate")
        doc.addObject("Part::Cylinder", "Pillar")
        doc.recompute()
        settle()
        title = FreeCADGui.Command.get(COMMAND).getInfo()["menuText"].replace("&", "")

        edit, (view, rows) = bring_up()
        check("with nothing confirmed the box lists the three modes and no more",
              rows == MODES, rows)

        # a command, run from its own list
        typed(edit, "/cmd " + COMMAND)
        view, rows = popup()
        FreeCADGui.Selection.clearSelection()
        if not check("the command is found in the command list", view is not None and len(rows) >= 1,
                     rows[:4]):
            return
        key(view, QtCore.Qt.Key_Tab)
        settle()
        check("the command ran", len(FreeCADGui.Selection.getSelection()) == 2,
              len(FreeCADGui.Selection.getSelection()))
        dismiss()
        edit, (view, rows) = bring_up()
        check("a command run from the box is listed the next time, by its title",
              rows == [title] + MODES, rows)

        # a parameter, opened from its list
        typed(edit, "/param AntiAliasing")
        view, rows = popup()
        if not check("a parameter is found in the parameter list", view is not None and rows,
                     rows[:3]):
            return
        param = rows[0]
        key(view, QtCore.Qt.Key_Tab)
        settle()
        check("its editor is open", shown("OmniParamPanel"))
        dismiss()

        # a property, opened with Return
        edit, (view, rows) = bring_up()
        check("the parameter is listed above the command, the modes last",
              rows == [param, title] + MODES, rows)
        typed(edit, "/Crate.Length")
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible():
                w.hide()
        key(edit, QtCore.Qt.Key_Return)
        settle()
        check("the property's editor is open", shown("OmniPropertyPanel"))
        dismiss()
        edit, (view, rows) = bring_up()
        check("the property is listed first: the newest first",
              rows == ["/Crate.Length", param, title] + MODES, rows)

        # typing takes the list away
        typed(edit, "/c")
        view, rows = popup()
        check("'/c' lists none of them", title not in rows and param not in rows
              and "/Crate.Length" not in rows and "/cmd " in rows, rows)

        # picking a row carries it out again
        typed(edit, "/")
        view, rows = popup()
        FreeCADGui.Selection.clearSelection()
        pick(view, rows, title)
        check("picking the command's row runs it",
              len(FreeCADGui.Selection.getSelection()) == 2,
              len(FreeCADGui.Selection.getSelection()))
        dismiss()
        edit, (view, rows) = bring_up()
        check("the command run again is at the top, and listed once",
              rows == [title, "/Crate.Length", param] + MODES, rows)

        # the box comes up on the newest item: Return alone repeats it
        FreeCADGui.Selection.clearSelection()
        key(view, QtCore.Qt.Key_Return)
        settle()
        check("Return on the box as it comes up carries out the newest item again",
              len(FreeCADGui.Selection.getSelection()) == 2,
              len(FreeCADGui.Selection.getSelection()))
        dismiss()
        edit, (view, rows) = bring_up()

        pick(view, rows, param)
        check("picking the parameter's row opens its editor", shown("OmniParamPanel"))
        dismiss()
        edit, (view, rows) = bring_up()
        pick(view, rows, "/Crate.Length")
        check("picking the property's row puts its text in the box",
              edit.text() == "/Crate.Length", edit.text())
        check("and opens its editor", shown("OmniPropertyPanel"))
        dismiss()

        stored = sorted(FreeCAD.ParamGet(STORE).GetStrings())
        check("three items are stored", stored == ["Item0", "Item1", "Item2"], stored)
        note("NOTE stored: %s" % [FreeCAD.ParamGet(STORE).GetString(k) for k in stored])

        FreeCADGui.Selection.clearSelection()
        FreeCAD.closeDocument(DOC)
        settle()
        edit, (view, rows) = bring_up()
        check("with the document closed the property is not listed, the other two are",
              "/Crate.Length" not in rows and title in rows and param in rows
              and len(rows) == len(MODES) + 2, rows)
        dismiss()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for d in list(FreeCAD.listDocuments().values()):
            FreeCAD.closeDocument(d.Name)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
