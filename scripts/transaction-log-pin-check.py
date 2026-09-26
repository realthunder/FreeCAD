# GUI check of docs/TransactionLog.md sec 27.16: links pinned to a version
# of another file through the Gui -- the Std_LinkPin picker and
# Std_LinkUnpin, undo and redo of both, a version document saved over its
# file with the warning, and re-pinning after it. One GUI run in a fresh
# user home and cache, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   XDG_CACHE_HOME=/tmp/pc/cache QT_QPA_PLATFORM=offscreen \
#     FREECAD_USER_HOME=/tmp/fchome-pc PINCHECK_OUT=/tmp/pc/out.txt \
#     ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-pin-check.py
#
# It writes PASS/FAIL lines to $PINCHECK_OUT and exits. The dialogs are
# answered by timers that look for the active modal widget.
import os, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["PINCHECK_OUT"]
lines = []
seen = []   # what each answered dialog said


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def answer(fn, tries=100):
    # Polls from a timer while a modal dialog runs its own event loop; `fn`
    # returns False to be asked again with the next dialog.
    state = {"tries": tries}

    def poll():
        w = QtWidgets.QApplication.activeModalWidget()
        if w is not None and fn(w) is not False:
            return
        if state["tries"] > 0:
            state["tries"] -= 1
            QtCore.QTimer.singleShot(20, poll)
    QtCore.QTimer.singleShot(0, poll)


def pickVersion(num):
    def fn(w):
        tree = w.findChild(QtWidgets.QTreeWidget)
        if tree is None:
            return False
        seen.append("picker %s" % w.objectName())
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if item.text(0) == str(num):
                tree.setCurrentItem(item)
        w.accept()
    return fn


def pressButton(button):
    def fn(w):
        if not isinstance(w, QtWidgets.QMessageBox) or w.button(button) is None \
                or w.property("answered"):
            return False
        w.setProperty("answered", True)
        seen.append(w.text())
        w.button(button).click()
    return fn


def run():
    try:
        base = os.path.dirname(OUT)
        # History written into files, whatever the user's config says:
        # FREECAD_USER_HOME does not keep the GUI off ~/.config/FreeCAD, and
        # os._exit below never writes the setting back.
        App.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt("TransactionLog", 2)
        part = App.newDocument("PinPart")
        part.openTransaction("box")
        box = part.addObject("Part::Box", "Box")
        part.commitTransaction()
        part.recompute()
        partPath = os.path.join(base, "pinpart.FCStd")
        part.saveAs(partPath)
        first = int(part.Version.split()[0])
        part.openTransaction("longer")
        box.Length = 30
        part.commitTransaction()
        part.recompute()
        part.save()

        asm = App.newDocument("PinAsm")
        asm.openTransaction("link")
        link = asm.addObject("App::Link", "L")
        link.LinkedObject = box
        asm.commitTransaction()
        asm.saveAs(os.path.join(base, "pinasm.FCStd"))
        Gui.setActiveDocument(asm.Name)
        settle()

        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(asm.Name, "L")
        pin = Gui.Command.get("Std_LinkPin")
        unpin = Gui.Command.get("Std_LinkUnpin")
        check("pin offered for a link to another file", pin.isActive())
        check("unpin not offered for an unpinned link", not unpin.isActive())

        # Cancelled, the picker leaves nothing -- no empty undo step from a
        # shape cache an isActive() check adds to the link meanwhile.
        before = list(asm.UndoNames)

        def cancel(w):
            if w.findChild(QtWidgets.QTreeWidget) is None:
                return False
            w.reject()
        answer(cancel)
        Gui.runCommand("Std_LinkPin")
        settle()
        check("a cancelled pick leaves no step: %r" % (asm.UndoNames,),
              asm.UndoNames == before and link.getLinkPin("LinkedObject") is None)
        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(asm.Name, "L")

        # The picker, answered with the first save's version.
        answer(pickVersion(first))
        Gui.runCommand("Std_LinkPin")
        settle()
        check("the picker was shown: %r" % (seen,), "picker Std_LinkPin" in seen)
        shown = link.LinkedObject
        pinned = link.getLinkPin("LinkedObject")
        check("pinned to v%d: %r" % (first, pinned), pinned and pinned[0] == first)
        check("the link shows the version's document: %s" % shown.Document.FileName,
              shown.Document.FileName.endswith("@v%d" % first))
        check("with the box as it was: %s" % shown.Length.Value, abs(shown.Length.Value - 10) < 1e-9)
        check("the file's document is untouched", abs(box.Length.Value - 30) < 1e-9)
        check("one undo step: %r" % (asm.UndoNames,), asm.UndoNames[:1] == ["Pin link"]
              and "<implicit>" not in " ".join(asm.UndoNames))

        Gui.Selection.clearSelection()
        Gui.Selection.addSelection(asm.Name, "L")
        check("unpin offered once pinned", unpin.isActive())
        Gui.runCommand("Std_LinkUnpin")
        settle()
        check("unpinned", link.getLinkPin("LinkedObject") is None)
        check("the link shows the file: %s" % link.LinkedObject.Document.FileName,
              link.LinkedObject.Document.FileName == partPath)
        asm.undo()
        settle()
        pinned = link.getLinkPin("LinkedObject")
        check("undo brings the pin back: %r" % (pinned,), pinned and pinned[0] == first)
        check("and the version: %s" % link.LinkedObject.Document.FileName,
              link.LinkedObject.Document.FileName.endswith("@v%d" % first))
        asm.redo()
        settle()
        check("redo unpins again", link.getLinkPin("LinkedObject") is None)
        asm.undo()
        settle()

        # What the pin shows is frozen (sec 27.22): no edit, and the Gui's
        # Save says why instead of saving.
        frozen = link.LinkedObject.Document
        try:
            frozen.getObject("Box").Height = 44
            refused = False
        except Exception:
            refused = True
        check("the pinned version refuses an edit", refused)
        del seen[:]
        answer(pressButton(QtWidgets.QMessageBox.Ok))
        ok = Gui.getDocument(frozen.Name).save()
        settle()
        check("the Gui does not save it, and says why: %r" % (seen,),
              not ok and len(seen) >= 1 and "cannot be changed" in seen[0])

        # The editable instance of the version, edited and saved over the
        # file through the Gui: the warning first, then the offer to re-pin.
        vdoc = App.openFileVersion(partPath, first)
        settle()
        check("the editable instance is another document: %s" % vdoc.FileName,
              vdoc is not frozen and ("@v%d" % first) in vdoc.FileName)
        vdoc.openTransaction("taller")
        vdoc.getObject("Box").Height = 44
        vdoc.commitTransaction()
        vdoc.recompute()
        settle()
        del seen[:]
        answer(pressButton(QtWidgets.QMessageBox.Save))
        # The re-pin question follows the save's own dialog.
        answer(pressButton(QtWidgets.QMessageBox.Yes), tries=500)
        ok = Gui.getDocument(vdoc.Name).save()
        settle()
        check("the Gui saved the version: %r" % (seen,), ok)
        check("after a warning naming it: %r" % (seen[:1],),
              len(seen) >= 1 and ("version %d of pinpart.FCStd" % first) in seen[0])
        check("and naming the file's open document",
              len(seen) >= 1 and part.Label in seen[0])
        check("then the re-pin offer: %r" % (seen[1:2],),
              len(seen) >= 2 and "Pin them to" in seen[1])
        pinned = link.getLinkPin("LinkedObject")
        check("re-pinned to the saved version: %r" % (pinned,), pinned and pinned[0] > first)
        shown = link.LinkedObject
        check("the link shows the saved box: %s" % shown.Height.Value,
              abs(shown.Height.Value - 44) < 1e-9)
        check("the version document is not modified", not Gui.getDocument(vdoc.Name).Modified)
        check("the file's own document still has its box",
              abs(box.Length.Value - 30) < 1e-9 and abs(box.Height.Value - 10) < 1e-9)

        # Save All passes a version document by, asking nothing.
        vdoc.openTransaction("wider")
        vdoc.getObject("Box").Width = 3
        vdoc.commitTransaction()
        settle()
        del seen[:]
        answer(pressButton(QtWidgets.QMessageBox.Cancel), tries=10)
        answer(pressButton(QtWidgets.QMessageBox.No), tries=10)
        Gui.setActiveDocument(asm.Name)
        Gui.runCommand("Std_SaveAll")
        settle()
        check("Save All asked nothing about the version: %r" % (seen,),
              not any("version" in s for s in seen))
        check("and left it modified", Gui.getDocument(vdoc.Name).Modified)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
