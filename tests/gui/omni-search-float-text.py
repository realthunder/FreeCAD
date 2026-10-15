"""The omni search writes a float without the zeros its decimals would end it
with, a unit kept.

docs/HandsOnQueue.md entry 125, the reporter 2026-10-14 about 16:41: "do not
show trailing zeros for float property values in omni search"; asked whether
settings too and a value with a unit: "yes both, yes same rule".

Claims:
  - a float setting (View/TransparencyOnTop, 0.5) opened from the box: its
    editor reads "0.5", not "0.500000";
  - an object's length (a box's Length, 10 mm) opened from the box: its
    editor reads "10 mm";
  - a plain float property (0.25) opened from the box: "0.25";
  - the property view's own editor keeps its decimals (the rule is the box's
    alone): the box's Length shown there is not trimmed.

Runs with QT_QPA_PLATFORM=offscreen; the pointer is not moved.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
DOC = "OmniFloats"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=40):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def wait(ms):
    end = time.monotonic() + ms / 1000.0
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.01)


def key(widget, which):
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(widget, QtGui.QKeyEvent(kind, which, QtCore.Qt.NoModifier))


def named(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def popup():
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            model = w.model()
            return w, [str(model.index(i, 0).data()) for i in range(model.rowCount())]
    return None, []


def typed(edit, text):
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle()


def bring_up():
    FreeCADGui.runCommand("Std_OmniSearch")
    settle()
    edit = named("OmniSearchEdit")
    if edit is None or not edit.isVisible():
        raise RuntimeError("the box did not come up")
    if edit.text() != "/":
        typed(edit, "/")
    return edit


def dismiss():
    box = named("OmniSearchBox")
    if box is not None and box.isVisible():
        box.hide()
    wait(500)


def editor_text(panel_name):
    """The text of the spin box in a panel of the box"""
    panel = named(panel_name)
    if panel is None or not panel.isVisible():
        return None
    for box in panel.findChildren(QtWidgets.QAbstractSpinBox):
        if box.isVisible():
            return box.lineEdit().text() if box.lineEdit() else box.text()
    return None


def open_property(edit, text):
    typed(edit, text)
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible():
            w.hide()
    key(edit, QtCore.Qt.Key_Return)
    settle()
    wait(300)


def run():
    try:
        VIEW.SetFloat("TransparencyOnTop", 0.5)
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Crate")
        feature = doc.addObject("App::FeaturePython", "Gauge")
        feature.addProperty("App::PropertyFloat", "Ratio", "Base", "a plain float")
        feature.Ratio = 0.25
        doc.recompute()
        settle()

        edit = bring_up()
        typed(edit, "/param TransparencyOnTop")
        view, rows = popup()
        if check("the setting is found", view is not None and rows, rows[:3]):
            key(view, QtCore.Qt.Key_Tab)
            settle()
            wait(300)
            text = editor_text("OmniParamPanel")
            check("a float setting reads '0.5' in its editor", text == "0.5", repr(text))
        dismiss()

        edit = bring_up()
        open_property(edit, "/Crate.Length")
        text = editor_text("OmniPropertyPanel")
        number = (text or "").split(" ")[0]
        check("a length reads '10 mm' in its editor: no trailing zeros, the unit kept",
              text is not None and number == "10" and "mm" in text, repr(text))
        # opening the editor changes nothing (a first try wrote the editor's
        # initial 0 into the property)
        check("and the length itself is untouched, 10 mm", abs(box.Length.Value - 10.0) < 1e-9,
              box.Length.Value)
        dismiss()

        edit = bring_up()
        open_property(edit, "/Gauge.Ratio")
        text = editor_text("OmniPropertyPanel")
        check("a plain float property reads '0.25' in its editor", text == "0.25", repr(text))
        check("and the float itself is untouched, 0.25", abs(feature.Ratio - 0.25) < 1e-12,
              feature.Ratio)
        dismiss()

        # the property view's own editor is left alone
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, box.Name)
        settle()
        wait(500)
        shown = []
        for w in QtWidgets.QApplication.allWidgets():
            if w.metaObject().className() == "Gui::PropertyEditor::PropertyEditor" and w.isVisible():
                model = w.model()
                for i in range(model.rowCount()):
                    group = model.index(i, 0)
                    for j in range(model.rowCount(group)):
                        if str(model.index(j, 0, group).data()) == "Length":
                            shown.append(str(model.index(j, 1, group).data()))
        note("INFO the property view shows Length as %s" % shown)
        check("the property view keeps its decimals for the length",
              any("." in s or "," in s for s in shown), shown)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        VIEW.RemFloat("TransparencyOnTop")
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(2500, run)
