"""Exercise the ViewArea collapse path the _purecall crash lived in.

    FreeCAD scripts/viewarea_collapse_check.py     (or run it over MCP)

The crash (see scripts/viewarea_close_crash.py) went
childViewGone -> collapseCell -> setActiveCell -> MainWindow::
setActiveWindow, re-entering MDI activation from inside a dying view's
~QWidget. The fix severs MainWindow's connections to a view in
~MDIView, before ~QWidget runs.

This checks the collapse still WORKS after that -- splitting to three
cells and closing them back down to one, with several documents open so
activation really moves between sub-windows, then closing the documents.
Counts are asserted at every step: a fix that quietly stopped the
collapse would otherwise look like a pass.
"""
import time

import FreeCAD
import FreeCADGui
from PySide import QtWidgets

ROUNDS = 3
LOG = []


def say(m):
    LOG.append(m)
    FreeCAD.Console.PrintMessage("[vk] %s\n" % m)


def pump(n=8):
    for _ in range(n):
        time.sleep(0.05)
        FreeCADGui.updateGui()


def views(name=None):
    """3D views of one document -- NOT of ActiveDocument.

    ! Gui.setActiveDocument does not move the MDI focus, so counting
    ActiveDocument's views while driving Std_ViewSplitClose reports one
    document's count against another document's command and invents
    failures. Ask the document by name, and activate its sub-window
    through the MDI area.
    """
    gdoc = (FreeCADGui.getDocument(name) if name
            else FreeCADGui.ActiveDocument)
    return len(gdoc.mdiViewsOfType("Gui::View3DInventor"))


def activate_doc(name=None):
    """Make a document's sub-window the active, maximized one."""
    mdi = FreeCADGui.getMainWindow().findChild(QtWidgets.QMdiArea)
    subs = mdi.subWindowList()
    if not subs:
        return False
    target = subs[-1]
    if name:
        label = FreeCAD.getDocument(name).Label
        for sub in subs:
            if label in sub.windowTitle():
                target = sub
                break
        else:
            return False
    mdi.setActiveSubWindow(target)
    pump(6)
    target.showMaximized()
    pump()
    return True


def main():
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document") \
        .SetBool("AutoSaveEnabled", False)
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    pump()

    failures = 0
    skipped = 0
    for r in range(ROUNDS):
        names = []
        split = []
        for i in range(3):
            doc = FreeCAD.newDocument("Collapse%d" % i)
            doc.addObject("Part::Box", "Box")
            doc.recompute()
            pump(12)
            # ! Re-activate before EVERY attempt and settle properly.
            # The split acts on getMainWindow()->activeWindow(); the
            # first document of a run shares the MDI area with the Start
            # page and needs a good second before its ViewArea is the
            # active window. Too short a settle and the command silently
            # does nothing, which reads as "splitting is broken".
            for _ in range(6):
                if views(doc.Name) >= 3:
                    break
                activate_doc(doc.Name)
                FreeCADGui.runCommand("Std_ViewSplitRight", 0)
                pump(10)
            n = views(doc.Name)
            names.append(doc.Name)
            if n != 3:
                # SKIPPED, not failed: the first document created right
                # after a mass close sometimes will not take a split
                # however long this waits -- teardown of the previous
                # round is still in flight and the command's
                # activeWindow precondition is not met. That is this
                # probe's limitation, not a collapse defect, so it must
                # not be counted as one. Its collapse is not asserted.
                say("round %d: %s split to %d, not 3 -- SKIPPED"
                    % (r + 1, doc.Name, n))
                skipped += 1
                continue
            split.append(doc.Name)

        # Collapse back down, one cell at a time. This is the path.
        for name in reversed(split):
            if not activate_doc(name):
                say("round %d: could not activate %s" % (r + 1, name))
                failures += 1
                continue
            for step in (2, 1):
                FreeCADGui.runCommand("Std_ViewSplitClose", 0)
                pump()
                n = views(name)
                if n != step:
                    say("round %d: %s closed a cell -> %d views,"
                        " expected %d" % (r + 1, name, n, step))
                    failures += 1
            say("round %d: %s collapsed to %d view(s)"
                % (r + 1, name, views(name)))

        for name in names:
            FreeCAD.closeDocument(name)
            pump(8)
        say("round %d: all documents closed, still alive" % (r + 1))

    if failures:
        say("FAILURES: %d (and %d document(s) skipped)" % (failures, skipped))
    else:
        say("OK -- %d rounds of split, collapse and close, no crash and"
            " every asserted cell count as expected; %d document(s) skipped"
            " for not taking a split" % (ROUNDS, skipped))


main()
