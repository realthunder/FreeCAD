"""Return in an edit box of the preferences commits that setting; it does not
close the dialog.

docs/HandsOnQueue.md entry 71: "press enter in an edit box in preference page
shall only commit that setting instead of dismiss the preference dialog", and
on what "commit" is: "commit only 'auto apply' option is on, else do nothing."
A line edit and a spin box take the key and leave it unaccepted, and a dialog
presses its default button for a Return nobody accepted: OK, which closed the
preferences under the edit.

Claims, on the Document page (General, the second page), which has a text
field (the author) and a number box (the undo steps):
  - with "Apply preferences at once" on: Return in the text field stores what
    was typed and the dialog stays; the same in the number box;
  - with it off: Return stores nothing, and the dialog stays;
  - Return with the focus on the OK button closes the dialog, as it did.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
GENERAL = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/General")
STEPS = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def dialog():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.metaObject().className() == "Gui::Dialog::DlgPreferencesImp" and w.isVisible():
            return w
    return None


def group_of(widget):
    """The parameter group a preference widget stores into, and its entry."""
    path = bytes(widget.property("prefPath")).decode()
    entry = bytes(widget.property("prefEntry")).decode()
    return FreeCAD.ParamGet("User parameter:BaseApp/Preferences/" + path), entry


def press_return(widget):
    widget.setFocus(QtCore.Qt.OtherFocusReason)
    QTest.keyClick(widget, QtCore.Qt.Key_Return)
    # a preference widget that stores at once does so a tenth of a second
    # after the change (PrefWidget::autoSave)
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(500, loop.quit)
    loop.exec()


def step(delay_ms):
    def deco(fn):
        STEPS.append((delay_ms, fn))
        return fn
    return deco


@step(2000)
def open_the_page():
    GENERAL.SetBool("AutoApplyPreference", True)
    # the dialog is modal from here: the steps after this one run inside it,
    # and this call returns when the last of them has closed it
    QtCore.QTimer.singleShot(0, advance)
    FreeCADGui.showPreferences("General", 1)


@step(2500)
def at_once():
    dlg = dialog()
    if not check("the preferences are open", dlg is not None):
        raise RuntimeError("no preferences dialog")
    author = dlg.findChild(QtWidgets.QLineEdit, "prefAuthor")
    undo = dlg.findChild(QtWidgets.QSpinBox, "prefUndoRedoSize")
    if not check("the page has its text field and its number box, shown",
                 author is not None and undo is not None
                 and author.isVisible() and undo.isVisible()):
        raise RuntimeError("the Document page is not the one shown")
    check("the settings are applied at once", GENERAL.GetBool("AutoApplyPreference", False))

    grp, entry = group_of(author)
    author.setText("entry 71")
    press_return(author)
    stays = check("Return in the text field: the dialog stays", dialog() is not None)
    check("and what was typed is stored", grp.GetString(entry, "") == "entry 71",
          "%s = %r" % (entry, grp.GetString(entry, "")))
    if not stays:
        raise RuntimeError("the dialog closed; nothing after this can be asked of it")

    grp, entry = group_of(undo)
    was = grp.GetInt(entry, -1)
    want = undo.value() + 7
    undo.lineEdit().setText(str(want))
    press_return(undo)
    check("Return in the number box: the dialog stays", dialog() is not None)
    check("and the number typed is stored", grp.GetInt(entry, -1) == want,
          "%s = %d, was %d, typed %d" % (entry, grp.GetInt(entry, -1), was, want))


@step(500)
def not_at_once():
    dlg = dialog()
    if dlg is None:
        raise RuntimeError("the dialog is gone")
    GENERAL.SetBool("AutoApplyPreference", False)
    author = dlg.findChild(QtWidgets.QLineEdit, "prefAuthor")
    grp, entry = group_of(author)
    author.setText("not yet")
    press_return(author)
    check("applied at OK only, Return in the text field: the dialog stays",
          dialog() is not None)
    check("and nothing is stored", grp.GetString(entry, "") == "entry 71",
          "%s = %r" % (entry, grp.GetString(entry, "")))


@step(500)
def on_the_button():
    dlg = dialog()
    if dlg is None:
        raise RuntimeError("the dialog is gone")
    box = dlg.findChild(QtWidgets.QDialogButtonBox)
    ok = box.button(QtWidgets.QDialogButtonBox.Ok) if box else None
    if not check("the dialog has its OK button", ok is not None):
        dlg.close()
        return
    author = dlg.findChild(QtWidgets.QLineEdit, "prefAuthor")
    grp, entry = group_of(author)
    press_return(ok)
    check("Return on the OK button closes the dialog", dialog() is None)
    check("and OK stores what the text field holds", grp.GetString(entry, "") == "not yet",
          "%s = %r" % (entry, grp.GetString(entry, "")))


def finish():
    dlg = dialog()
    if dlg is not None:
        dlg.close()
    GENERAL.RemBool("AutoApplyPreference")
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
            note("ABORT in %s: %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
            if fn is not open_the_page:
                advance()
            return
        if fn is not open_the_page:
            advance()

    QtCore.QTimer.singleShot(delay, run)


advance()
