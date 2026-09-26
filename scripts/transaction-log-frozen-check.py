# GUI check of docs/TransactionLog.md sec 27.22-27.30, the Gui half of
# frozen pinned versions: the tree's labels (the branch suffix of a file's
# own document, a pinned version shown in italics), Std_SaveToHistory, a
# version document's Save offering Save to History, and the prompt to close
# a pinned version when its last pin goes, with its remembered answer. One
# GUI run in a fresh user home and cache, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   XDG_CACHE_HOME=/tmp/fc/cache QT_QPA_PLATFORM=offscreen \
#     FREECAD_USER_HOME=/tmp/fchome-fc FROZENCHECK_OUT=/tmp/fc/out.txt \
#     ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-frozen-check.py
#
# It writes PASS/FAIL lines to $FROZENCHECK_OUT and exits. The dialogs are
# answered by timers that look for the active modal widget.
import os, re, traceback, zipfile
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["FROZENCHECK_OUT"]
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


def pressText(text, remember=False):
    def fn(w):
        if not isinstance(w, QtWidgets.QMessageBox) or w.property("answered"):
            return False
        for b in w.buttons():
            if b.text().replace("&", "") == text:
                w.setProperty("answered", True)
                seen.append(w.text())
                if remember and w.checkBox() is not None:
                    w.checkBox().setChecked(True)
                b.click()
                return True
        return False
    return fn


def treeText(doc):
    for tree in Gui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
        root = tree.invisibleRootItem()
        stack = [root.child(i) for i in range(root.childCount())]
        while stack:
            item = stack.pop()
            text = item.text(0)
            if text.startswith(doc.Label):
                return text, item.font(0).italic()
            stack.extend(item.child(i) for i in range(item.childCount()))
    return None, False


def model(path):
    xml = zipfile.ZipFile(path).read("Document.xml").decode("utf-8")
    return re.sub(r"<History[^>]*/>|<History .*?</History>", "", xml, flags=re.S)


def run():
    try:
        base = os.path.dirname(OUT)
        params = App.ParamGet("User parameter:BaseApp/Preferences/Document")
        # History written into files, whatever the user's config says:
        # FREECAD_USER_HOME does not keep the GUI off ~/.config/FreeCAD, and
        # os._exit below never writes the settings back.
        params.SetInt("TransactionLog", 2)
        params.SetInt("ClosePinnedVersion", 0)

        part = App.newDocument("FrozenPart")
        part.UndoMode = 1
        part.openTransaction("box")
        box = part.addObject("Part::Box", "Box")
        part.commitTransaction()
        part.recompute()
        partPath = os.path.join(base, "frozenpart.FCStd")
        part.saveAs(partPath)
        first = int(part.Version.split()[0])
        part.openTransaction("longer")
        box.Length = 30
        part.commitTransaction()
        part.recompute()
        part.save()
        settle()
        text, _ = treeText(part)
        check("one branch, no suffix: %r" % (text,), text is not None and "@" not in text)
        part.createTransactionBranch("side")
        settle()
        text, _ = treeText(part)
        check("two branches, the suffix: %r" % (text,),
              text is not None and "@side@v" in text)

        asm = App.newDocument("FrozenAsm")
        asm.openTransaction("link")
        link = asm.addObject("App::Link", "L")
        link.LinkedObject = box
        asm.commitTransaction()
        asm.saveAs(os.path.join(base, "frozenasm.FCStd"))
        link.pinLink("LinkedObject", first)
        frozen = link.LinkedObject.Document
        settle()
        text, italic = treeText(frozen)
        check("the pinned version in the tree: %r" % (text,),
              text is not None and text.endswith("@v%d" % first) and italic)

        # Save to History on the file's own document: a kept version, the
        # file as it was.
        Gui.setActiveDocument(part.Name)
        settle()
        cmd = Gui.Command.get("Std_SaveToHistory")
        check("Save to History offered for the file's document", cmd.isActive())
        before = model(partPath)
        count = len(part.getTransactionVersions())
        Gui.runCommand("Std_SaveToHistory")
        settle()
        versions = part.getTransactionVersions()
        check("a version kept: %d -> %d" % (count, len(versions)),
              len(versions) == count + 1 and versions[-1]["kind"] == "named")
        check("the file opens as it did", model(partPath) == before)
        # Opened for the pin with no view of its own: given one, so that it
        # can be the active document.
        App.setActiveDocument(frozen.Name)
        Gui.activateView("Gui::View3DInventor", True)
        Gui.setActiveDocument(frozen.Name)
        settle()
        check("not offered for a pinned version (%s active)" % Gui.ActiveDocument.Document.Name,
              Gui.ActiveDocument.Document is frozen and not cmd.isActive())

        # A version document's Save: Save to History is the default.
        editable = App.openFileVersion(partPath, first)
        settle()
        editable.openTransaction("taller")
        editable.getObject("Box").Height = 44
        editable.commitTransaction()
        editable.recompute()
        settle()
        del seen[:]
        answer(pressText("Save to History"))
        ok = Gui.getDocument(editable.Name).save()
        settle()
        check("saved to its history: %r" % (seen,), ok and len(seen) == 1)
        check("the file opens as it did", model(partPath) == before)
        check("the version document is not modified",
              not Gui.getDocument(editable.Name).Modified)
        tail = int(editable.FileName.rsplit("@v", 1)[1])
        check("its name ends in the saved version: %s" % editable.FileName, tail > first)
        check("and the pin still shows the version",
              abs(link.LinkedObject.Height.Value - 10) < 1e-9)

        # The last pin goes: asked, closed, the answer remembered.
        del seen[:]
        answer(pressText("Close", remember=True), tries=200)
        name = frozen.Name
        link.unpinLink("LinkedObject")
        for _ in range(20):
            settle()
        check("asked: %r" % (seen,), len(seen) == 1 and "any more" in seen[0])
        check("closed", name not in App.listDocuments())
        check("the answer kept", params.GetInt("ClosePinnedVersion") == 1)

        # Remembered: pinned again and let go, it closes without asking.
        link.pinLink("LinkedObject", first)
        name = link.LinkedObject.Document.Name
        del seen[:]
        link.unpinLink("LinkedObject")
        for _ in range(20):
            settle()
        check("closed without asking: %r" % (seen,), not seen and name not in App.listDocuments())
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
