""""Reset all" in the preferences leaves the session whole.

docs/HandsOnQueue.md entries 49 and 50, one trigger: Preferences > Reset >
"Reset all...", which clears every user parameter of the running session.

Entry 49: "I choose 'reset all' in preference dialog. and then select light
theme [...] The theme changed, but the workbench toolbar got somehow hidden."
On a MAXIMIZED window. The reset drops the custom title bar, and the toolbar
manager, told that the title bar areas' entries are gone, took the workbench
toolbar out of the title bar itself -- a move that hides a toolbar -- and
then asked the toolbar whether it was visible, to know whether to show it.
A window that is not maximized swaps its title bar at once and moves the
toolbar properly before that runs; a maximized one first leaves the maximized
state, a tenth of a second, and lost the race every time.

Entry 50: "After reset all preference [...] the edge rendering is jagged".
The render type fell back to its definition's default, "Default", which
meant no render engine; the engine was only ever chosen at startup. "Default"
now MEANS the render engine on the platform's backend (the reporter's
decision), so a session with no type stored draws with it.

Claims, on a maximized window under the Light theme with a document open,
"Reset all" driven through the dialog itself:
  - before: the workbench toolbar is shown, in the title bar, and the 3D
    view has a render backend (the test stands on what it means to test);
  - after the reset: the workbench toolbar is shown, and the 3D view still
    has its backend;
  - after the Light theme is applied again: the workbench toolbar is shown,
    in the title bar; the view has its backend.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ResetAll"
STEPS = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def mw():
    return FreeCADGui.getMainWindow()


def toolbar():
    # the window can hold a second, empty one of the name: a toolbar that was
    # recorded is made ahead of its workbench (an empty one has its extension
    # button and nothing else)
    for tb in mw().findChildren(QtWidgets.QToolBar):
        if tb.objectName() == "Workbench" and len(tb.findChildren(QtWidgets.QWidget)) > 1:
            return tb
    return None


def where(tb):
    p = tb.parentWidget()
    return p.objectName() or p.metaObject().className()


def backend():
    """The 3D view's render backend, as its statistics name it; None without one"""
    views = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
    if not views:
        return None
    try:
        views[0].getRenderStats()
    except RuntimeError as e:
        return None if "No external renderer" in str(e) else "a backend (%s)" % e
    return "a backend"


def start():
    mw().showMaximized()
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    FreeCADGui.getDocument(DOC).activeView().viewIsometric()
    FreeCADGui.SendMsgToActiveView("ViewFit")


def theme():
    if not FreeCADGui.applyTheme("Light"):
        raise RuntimeError("the Light theme was refused")


def before():
    tb = toolbar()
    if not check("the window is maximized", mw().isMaximized()):
        raise RuntimeError("not maximized")
    check("before: the workbench toolbar is shown, in the title bar",
          tb is not None and tb.isVisible() and where(tb) == "MenuBarLeftArea",
          "visible %s in %s" % (tb.isVisible(), where(tb)) if tb else "no toolbar")
    check("before: the 3D view has a render backend", backend() is not None)


def open_prefs():
    FreeCADGui.runCommand("Std_DlgPreferences")


def on_screen(kind):
    """The widget of this kind that is up. Not asked of the application's
    active popup or modal widget: those are None while another program's
    window has the keyboard, which a test run beside other work meets."""
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, kind) and w.isVisible():
            return w
    return None


def answer_yes():
    box = on_screen(QtWidgets.QMessageBox)
    if box is not None:
        box.button(QtWidgets.QMessageBox.Yes).click()
    else:
        note("FAIL the reset asked its question")


def pick_reset_all():
    menu = on_screen(QtWidgets.QMenu)
    if menu is None:
        note("FAIL the Reset button opened its menu")
        return
    for act in menu.actions():
        if act.text().replace("&", "").startswith("Reset all"):
            QtCore.QTimer.singleShot(800, answer_yes)
            menu.setActiveAction(act)
            QTest.keyClick(menu, QtCore.Qt.Key_Return)
            return
    note("FAIL the menu has 'Reset all' | %s" % [a.text() for a in menu.actions()])
    menu.close()


def reset_all():
    dlg = None
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.metaObject().className() == "Gui::Dialog::DlgPreferencesImp" and w.isVisible():
            dlg = w
    if dlg is None:
        raise RuntimeError("no preferences dialog")
    button = dlg.findChild(QtWidgets.QPushButton, "buttonReset")
    if button is None:
        raise RuntimeError("no Reset button")
    QtCore.QTimer.singleShot(800, pick_reset_all)
    button.click()
    check("the reset closed the dialog", not dlg.isVisible())
    stored = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow").GetBools()
    if not check("the reset cleared the settings", "TitleBarToolBars" not in stored, stored):
        # nothing after this would be a claim about a reset
        raise RuntimeError("the reset was not carried out")


def after_reset():
    tb = toolbar()
    check("after the reset: the workbench toolbar is shown",
          tb is not None and tb.isVisible(),
          "visible %s, hidden in its own right %s, in %s" % (
              tb.isVisible(), tb.isHidden(), where(tb)) if tb else "no toolbar")
    check("after the reset: the 3D view still has its render backend", backend() is not None,
          "render type stored: %r" % FreeCAD.ParamGet(
              "User parameter:BaseApp/Preferences/View/Render").GetString("Type", "(none)"))


def after_theme():
    tb = toolbar()
    check("after the theme: the workbench toolbar is shown, in the title bar",
          tb is not None and tb.isVisible() and where(tb) == "MenuBarLeftArea",
          "visible %s, hidden in its own right %s, in %s" % (
              tb.isVisible(), tb.isHidden(), where(tb)) if tb else "no toolbar")
    check("after the theme: the 3D view has its render backend", backend() is not None)


def finish():
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("FAIL the step %s ran | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((3000, start))
STEPS.append((2000, theme))
STEPS.append((6000, before))
STEPS.append((500, open_prefs))
STEPS.append((2500, reset_all))
STEPS.append((4000, after_reset))
STEPS.append((500, theme))
STEPS.append((6000, after_theme))
advance()
