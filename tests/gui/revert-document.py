"""File > Revert reads the document in again, and the program is still there.

docs/HandsOnQueue.md entry 97: "new crash ... the crash happen when I click
menu File -> Revert to rever the loaded scanner document".

A revert takes the document's objects down and reads the file again. What
the teardown wrote -- to a view provider, which with the transaction log
on is document data -- opened an implicit transaction naming objects that
were deleted a moment later. Its commit is queued; it ran from the progress
bar of the load, or after it, and the log asked a deleted object what it
was. The writer, in the reporter's file: a TechDraw page whose view is
open is hidden when the teardown closes that view -- Page.Visibility, and
HiddenChildren of the group the page is in. So the document here has a
page, in a group, its view open.

Claims:
  - a saved document of three boxes and a drawing page in a group, the
    page's view open, a box changed since: reverted through the command, it
    has its objects again and the change is gone;
  - no transaction is left open by it, and nothing to undo;
  - a second revert, straight after: the same;
  - a change after it is undone as any other.
A run that ends without its last line has crashed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
ASKED = []


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


def sweep():
    """The question the command asks is answered Yes; any other box No"""
    box = QtWidgets.QApplication.activeModalWidget()
    if not isinstance(box, QtWidgets.QMessageBox):
        return
    ASKED.append(box.windowTitle())
    yes = box.button(QtWidgets.QMessageBox.Yes)
    if box.windowTitle() == "Revert document" and yes is not None:
        yes.click()
        return
    no = box.button(QtWidgets.QMessageBox.No)
    if no is not None:
        no.click()
    else:
        box.close()


SWEEPER = QtCore.QTimer()
SWEEPER.timeout.connect(sweep)


def start():
    doc = FreeCAD.newDocument("Reverted")
    for i in range(3):
        box = doc.addObject("Part::Box", "Box%d" % i)
        box.Placement.Base = FreeCAD.Vector(i * 15, 0, 0)
    group = doc.addObject("App::DocumentObjectGroup", "Group")
    page = doc.addObject("TechDraw::DrawPage", "Page")
    template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
    template.Template = os.path.join(FreeCAD.getResourceDir(), "Mod", "TechDraw", "Templates",
                                     "A4_LandscapeTD.svg")
    page.Template = template
    view = doc.addObject("TechDraw::DrawViewPart", "View")
    view.Source = [doc.Box0]
    page.addView(view)
    group.addObject(page)
    doc.recompute()
    FreeCADGui.SendMsgToActiveView("ViewFit")
    doc.saveAs(os.path.join(OUT, "Reverted.FCStd"))
    settle(800)
    open_page()
    SWEEPER.start(300)


def open_page():
    """The page's own view, as a double click on the page opens it"""
    FreeCADGui.getDocument("Reverted").getObject("Page").doubleClicked()
    settle(1200)
    shown = [w.metaObject().className() for w in FreeCADGui.getMainWindow().findChildren(
        QtWidgets.QWidget) if "MDIViewPage" in w.metaObject().className()]
    return bool(shown)


def revert(tag):
    doc = FreeCAD.getDocument("Reverted")
    doc.Box1.Length = 77
    doc.addObject("Part::Cylinder", "Extra")
    doc.recompute()
    settle(400)
    check("%s: the page's view is open before it" % tag, open_page())
    del ASKED[:]
    FreeCADGui.runCommand("Std_Revert")
    # the queued commit of whatever the revert left open runs here
    settle(1500)
    doc = FreeCAD.getDocument("Reverted")
    check("%s: the command asked its question" % tag, ASKED == ["Revert document"], ASKED)
    names = sorted(o.Name for o in doc.Objects)
    check("%s: the document has its objects again" % tag,
          names == ["Box0", "Box1", "Box2", "Group", "Page", "Template", "View"], names)
    check("%s: and the change is gone" % tag,
          doc.getObject("Box1") is not None and abs(doc.Box1.Length.Value - 10.0) < 1e-9,
          doc.Box1.Length if doc.getObject("Box1") is not None else None)
    check("%s: no transaction is left open, nothing to undo" % tag,
          not doc.HasPendingTransaction and doc.UndoCount == 0,
          "pending %s, undo %d %s" % (doc.HasPendingTransaction, doc.UndoCount, doc.UndoNames))


def first():
    revert("a revert")


def second():
    revert("a second revert")


def after():
    doc = FreeCAD.getDocument("Reverted")
    doc.openTransaction("Longer")
    doc.Box2.Length = 33
    doc.commitTransaction()
    doc.recompute()
    settle(300)
    check("a change after it is one step to undo", doc.UndoCount >= 1 and "Longer" in doc.UndoNames,
          doc.UndoNames)
    doc.undo()
    settle(300)
    check("and undone", abs(doc.Box2.Length.Value - 10.0) < 1e-9, doc.Box2.Length)


def finish():
    SWEEPER.stop()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


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
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, first, second, after]
QtCore.QTimer.singleShot(2500, advance)
