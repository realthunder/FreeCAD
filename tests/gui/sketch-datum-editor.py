"""One editor for a constraint's value, which Tab moves.

Ruled 2026-10-03 (docs/SketcherPort.md "One editor for a constraint's
value"): the value, an expression ('=' first), driving or reference, and
the name, in one editor over the constraint's label; Tab applies what is
typed and moves the editor to the next dimension the view shows; the whole
entry is one undo step; Escape is Enter by default (ruled the same day:
Escape means leave; Mod/Sketcher/General/DatumEscapeTakesBack makes it take
the entry back), and Std_Undo while it runs takes all of it back; the
editor's other rows grow away from what the dimension measures.

Sketch: three horizontal lines, a Distance on each (60, 50, 40).

The editor is found as the visible QFrame "DatumValueEditor"; its line is
"DatumValueEditorLine", its name row "DatumValueEditorName".
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
state = {"done": False, "dialogs": 0}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))
    return cond


def settle(n=12):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def watchdog():
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is not None:
        state["dialogs"] += 1
        dialog.reject()


def editor():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QFrame, "DatumValueEditor"):
        if w.isVisible():
            return w
    return None


def line():
    e = editor()
    return e.findChild(QtWidgets.QLineEdit, "DatumValueEditorLine") if e else None


def name_edit():
    e = editor()
    return e.findChild(QtWidgets.QLineEdit, "DatumValueEditorName") if e else None


def result_text():
    e = editor()
    label = e.findChild(QtWidgets.QLabel, "DatumValueEditorResult") if e else None
    return label.text() if label is not None and label.isVisible() else ""


def type_in(text):
    w = line()
    w.setText(text)
    # what typing does: textEdited, which the editor listens to
    w.textEdited.emit(text)
    settle(3)


def key(k, mods=QtCore.Qt.NoModifier, target=None):
    target = target or line()
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(target, QtGui.QKeyEvent(kind, k, mods))
    settle()


def values(sk):
    return [c.Value for c in sk.Constraints]


def open_on(panel, row):
    panel.itemActivated.emit(panel.item(row))
    settle()


def viewer_widget():
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget)
            if "View3DInventorViewer" in w.metaObject().className() and w.isVisible()][0]


def pixel(view, point):
    """a model point in the viewer widget's Qt coordinates"""
    target = viewer_widget()
    p = view.getPointOnViewport(point)
    return QtCore.QPoint(p[0], target.height() - 1 - p[1])


def frame_rect():
    e = editor()
    target = viewer_widget()
    top_left = target.mapFromGlobal(e.mapToGlobal(QtCore.QPoint(0, 0)))
    return QtCore.QRect(top_left, e.size())


def line_centre():
    w = line()
    target = viewer_widget()
    return target.mapFromGlobal(w.mapToGlobal(w.rect().center()))


def run():
    try:
        import Part
        import Sketcher
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet(GENERAL).RemBool("EditDatumInPlace")
        FreeCAD.ParamGet(GENERAL).RemBool("DatumEscapeTakesBack")
        timer = QtCore.QTimer()
        timer.timeout.connect(watchdog)
        timer.start(150)
        state["timer"] = timer

        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("DatumEditor")
        doc.UndoMode = 1
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-30, 20, 0), V(30, 20, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-25, 0, 0), V(25, 0, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-20, -20, 0), V(20, -20, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Distance", 0, 60.0))
        sk.addConstraint(Sketcher.Constraint("Distance", 1, 50.0))
        sk.addConstraint(Sketcher.Constraint("Distance", 2, 40.0))
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(60)
        panel = FreeCADGui.getMainWindow().findChild(QtWidgets.QListWidget,
                                                     "listWidgetConstraints")

        # -- one editor, its line over the number --------------------------
        open_on(panel, 0)
        check("the panel opens ONE editor, holding the value",
              editor() is not None and line().text().startswith("60")
              and len([w for w in FreeCADGui.getMainWindow().findChildren(
                  QtWidgets.QFrame, "DatumValueEditor") if w.isVisible()]) == 1,
              line().text() if line() else None)
        check("the line has the keys", line() is not None and line().hasFocus())
        # the measured line runs through y=20; nothing of the editor on it
        foot = pixel(view, V(0, 20, 0))
        check("the editor does not cover what the dimension measures",
              not frame_rect().contains(foot), (frame_rect(), foot))

        # -- Tab applies and moves; the whole entry one undo step ----------
        undo = doc.UndoCount
        type_in("61 mm")
        key(QtCore.Qt.Key_Tab)
        check("Tab applies the value at once", values(sk)[0] == 61.0, values(sk))
        check("and moves the editor to the next dimension",
              line() is not None and line().text().startswith("50"),
              line().text() if line() else None)
        type_in("51 mm")
        key(QtCore.Qt.Key_Backtab)
        check("Shift+Tab applies and goes back",
              values(sk)[:2] == [61.0, 51.0] and line().text().startswith("61"),
              (values(sk), line().text()))
        key(QtCore.Qt.Key_Return)
        check("Enter closes it", editor() is None)
        check("everything the entry applied is one undo step",
              doc.UndoCount == undo + 1, doc.UndoCount - undo)
        doc.undo()
        settle()
        check("one undo takes all of it back", values(sk) == [60.0, 50.0, 40.0], values(sk))

        # -- Escape is Enter -----------------------------------------------
        undo = doc.UndoCount
        open_on(panel, 1)
        type_in("55 mm")
        key(QtCore.Qt.Key_Tab)
        type_in("45 mm")
        key(QtCore.Qt.Key_Escape)
        check("Escape applies what is typed and closes, as Enter: one step",
              values(sk) == [60.0, 55.0, 45.0] and doc.UndoCount == undo + 1
              and editor() is None, (values(sk), doc.UndoCount - undo))
        doc.undo()
        settle()

        # -- Std_Undo takes back what Tab applied --------------------------
        undo = doc.UndoCount
        open_on(panel, 1)
        type_in("55 mm")
        key(QtCore.Qt.Key_Tab)
        type_in("45 mm")
        FreeCADGui.runCommand("Std_Undo")
        settle()
        check("Std_Undo while it runs takes back what Tab applied, and leaves no step",
              values(sk) == [60.0, 50.0, 40.0] and doc.UndoCount == undo and editor() is None,
              (values(sk), doc.UndoCount - undo))

        # -- the preference: Escape takes back ------------------------------
        FreeCAD.ParamGet(GENERAL).SetBool("DatumEscapeTakesBack", True)
        undo = doc.UndoCount
        open_on(panel, 1)
        type_in("55 mm")
        key(QtCore.Qt.Key_Tab)
        type_in("45 mm")
        key(QtCore.Qt.Key_Escape)
        check("with DatumEscapeTakesBack on, Escape takes back what Tab applied, no step",
              values(sk) == [60.0, 50.0, 40.0] and doc.UndoCount == undo and editor() is None,
              (values(sk), doc.UndoCount - undo))
        FreeCAD.ParamGet(GENERAL).RemBool("DatumEscapeTakesBack")

        # -- '=' typed over the selected number begins an expression -------
        open_on(panel, 2)
        for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
            QtWidgets.QApplication.sendEvent(line(), QtGui.QKeyEvent(
                kind, QtCore.Qt.Key_Equal, QtCore.Qt.NoModifier, "="))
        settle()
        check("'=' typed over the selected number leaves '=' alone, not '= mm'",
              line() is not None and line().text() == "=", line().text() if line() else None)
        key(QtCore.Qt.Key_Escape)
        check("Escape on what is no value keeps the editor open, as Enter does",
              editor() is not None, editor() is not None)
        FreeCADGui.runCommand("Std_Undo")
        settle()

        # -- an expression, and back to a value ----------------------------
        open_on(panel, 2)
        type_in("=10 mm + 25 mm")
        settle(25)  # the result line waits for the typing to pause
        check("an expression's result is shown under the line",
              "35" in result_text(), result_text())
        key(QtCore.Qt.Key_Return)
        bound = [e for e in sk.ExpressionEngine if "Constraints" in e[0]]
        check("Enter binds the expression", len(bound) == 1 and abs(values(sk)[2] - 35.0) < 1e-6,
              (bound, values(sk)))
        open_on(panel, 2)
        check("reopened, the line shows it with its '='",
              line() is not None and line().text().startswith("="),
              line().text() if line() else None)
        type_in("38 mm")
        key(QtCore.Qt.Key_Return)
        bound = [e for e in sk.ExpressionEngine if "Constraints" in e[0]]
        check("a value typed over it removes the expression",
              bound == [] and abs(values(sk)[2] - 38.0) < 1e-6, (bound, values(sk)))
        open_on(panel, 2)
        type_in("=no_such_thing + 1")
        settle(25)
        key(QtCore.Qt.Key_Return)
        check("an expression that cannot be bound keeps the editor open, saying why",
              editor() is not None and result_text() != "", result_text())
        FreeCADGui.runCommand("Std_Undo")
        settle()
        check("Std_Undo closes it, nothing bound",
              editor() is None and abs(values(sk)[2] - 38.0) < 1e-6, values(sk))

        # -- reference: the key, and typing into one -----------------------
        open_on(panel, 1)
        key(QtCore.Qt.Key_D, QtCore.Qt.ControlModifier | QtCore.Qt.ShiftModifier)
        key(QtCore.Qt.Key_Return)
        check("Ctrl+Shift+D and Enter make it a reference",
              not sk.Constraints[1].Driving, sk.Constraints[1].Driving)
        open_on(panel, 1)
        check("a reference opens in the editor too", editor() is not None)
        type_in("52 mm")
        key(QtCore.Qt.Key_Return)
        check("typing a value into a reference makes it driving, with that value",
              sk.Constraints[1].Driving and abs(values(sk)[1] - 52.0) < 1e-6,
              (sk.Constraints[1].Driving, values(sk)))

        check("no modal dialog on the way", state["dialogs"] == 0, state["dialogs"])

        # -- the name ------------------------------------------------------
        dialogs = state["dialogs"]
        open_on(panel, 0)
        key(QtCore.Qt.Key_F2)
        check("F2 gives the name row the keys",
              name_edit() is not None and name_edit().hasFocus())
        name_edit().setText("1bad")
        key(QtCore.Qt.Key_Return, target=name_edit())
        check("a name an expression cannot use is refused, the editor stays",
              editor() is not None and sk.Constraints[0].Name == "", sk.Constraints[0].Name)
        name_edit().setText("Width")
        key(QtCore.Qt.Key_Return, target=name_edit())
        check("Enter renames it", sk.Constraints[0].Name == "Width" and editor() is None,
              sk.Constraints[0].Name)

        note("dialogs closed during the name checks (a refused name may say so "
             "in a message box): %d" % (state["dialogs"] - dialogs,))
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()

        # -- Snell's law: its ratio in the editor, at the refraction point -
        sn = doc.addObject("Sketcher::SketchObject", "Snell")
        sn.addGeometry(Part.LineSegment(V(-20, 15, 0), V(0, 0, 0)), False)
        sn.addGeometry(Part.LineSegment(V(0, 0, 0), V(15, -20, 0)), False)
        sn.addGeometry(Part.LineSegment(V(-30, 0, 0), V(30, 0, 0)), False)
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sn)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(40)
        dialogs = state["dialogs"]
        undo = doc.UndoCount
        FreeCADGui.Selection.clearSelection()
        for sub in ("Vertex2", "Vertex3", "Edge3"):
            FreeCADGui.Selection.addSelection(sn, sub)
        settle(4)
        FreeCADGui.runCommand("Sketcher_ConstrainSnellsLaw")
        settle()
        snells = [c for c in sn.Constraints if c.Type == "SnellsLaw"]
        toggle = editor().findChild(QtWidgets.QToolButton, "DatumValueEditorDriving") \
            if editor() else None
        check("Snell's law opens the editor, not a dialog, with no reference toggle",
              editor() is not None and state["dialogs"] == dialogs and len(snells) == 1
              and toggle is not None and not toggle.isVisible(),
              (editor() is not None, state["dialogs"] - dialogs, len(snells)))
        if editor() is not None:
            refraction = pixel(view, V(0, 0, 0))
            check("at the refraction point", (line_centre() - refraction).manhattanLength() < 40,
                  (line_centre(), refraction))
            type_in("1.5")
            key(QtCore.Qt.Key_Return)
        snells = [c.Value for c in sn.Constraints if c.Type == "SnellsLaw"]
        check("Enter sets the ratio: the constraint and its ratio one undo step",
              snells == [1.5] and doc.UndoCount == undo + 1 and editor() is None,
              (snells, doc.UndoCount - undo))
        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    FreeCAD.ParamGet(GENERAL).RemBool("EditDatumInPlace")
    FreeCAD.ParamGet(GENERAL).RemBool("DatumEscapeTakesBack")
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    if "timer" in state:
        state["timer"].stop()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
