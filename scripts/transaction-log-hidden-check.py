# GUI check of docs/TransactionLog.md sec 32.30, the desktop's side of a
# branch served in a document of its own, which has no window: closing the
# last document that has one, while such a document is open, says so and
# offers to show it -- "Show Them", "Leave Hidden", or Cancel -- and saving
# it says first that it is not the branch the file is checked out on.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-hd \
#     HIDDENCHECK_OUT=/tmp/hd/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-hidden-check.py
#
# It writes PASS/FAIL lines to $HIDDENCHECK_OUT and exits. The setting it
# changes is put back before it exits.
import os, shutil, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["HIDDENCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle(ms=0):
    waited = QtCore.QElapsedTimer()
    waited.start()
    for _ in range(5):
        QtWidgets.QApplication.processEvents()
    while waited.elapsed() < ms:
        QtWidgets.QApplication.processEvents(QtCore.QEventLoop.AllEvents, 50)


def views(name):
    """How many 3D windows document `name` has; None when it is gone."""
    if name not in App.listDocuments():
        return None
    return len(Gui.getDocument(name).mdiViewsOfType("Gui::View3DInventor"))


def unstick(seen):
    # Whatever is still up, so a dialog nobody answered ends the run with a
    # failure and not with a hang.
    modal = QtWidgets.QApplication.activeModalWidget()
    if modal is not None:
        seen["left open"] = modal.objectName() or modal.metaObject().className()
        modal.reject()


def answering(answer, act):
    """Do `act`, answering the box it raises: the button of object name
    `answer`, or Cancel when that is None. What the box was and said."""
    seen = {"box": None, "count": 0}

    def drive():
        if seen.get("done"):
            return   # (the next step's box is the next step's)
        box = QtWidgets.QApplication.activeModalWidget()
        if box is None:
            if seen["count"] == 0 and not seen.get("done"):
                QtCore.QTimer.singleShot(200, drive)
            return
        seen["count"] += 1
        QtCore.QTimer.singleShot(400, drive)   # one asked twice is counted
        seen["box"] = box.objectName() or box.metaObject().className()
        if isinstance(box, QtWidgets.QMessageBox):
            seen["text"] = box.text()
            seen["buttons"] = sorted(b.text() for b in box.buttons())
        button = None
        for b in box.findChildren(QtWidgets.QPushButton):
            if answer and b.objectName() == answer:
                button = b
        if button is not None:
            button.click()
        else:
            box.reject()

    QtCore.QTimer.singleShot(300, drive)
    # (a step's own: one that has ended leaves the next step's box alone)
    QtCore.QTimer.singleShot(6000, lambda: None if seen.get("done") else unstick(seen))
    act()
    settle(1500)
    seen["done"] = True
    return seen


def titles():
    area = Gui.getMainWindow().findChild(QtWidgets.QMdiArea)
    return [sub.windowTitle() for sub in area.subWindowList()]


def closeWindow(name):
    """Close the window of document `name` as a click on its close button
    does: the view is asked, and through it the document."""
    label = App.getDocument(name).Label
    area = Gui.getMainWindow().findChild(QtWidgets.QMdiArea)
    for sub in area.subWindowList():
        if sub.windowTitle().startswith(label):
            return sub.close()
    lines.append("NOTE no window titled %s among %r" % (label, titles()))
    return False


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 0)
    where = tempfile.mkdtemp(prefix="fc-hidden-check-")
    try:
        p.SetInt("TransactionLog", 2)
        doc = App.newDocument("Outer")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.saveAs(os.path.join(where, "outer.FCStd"))
        Gui.getDocument(doc.Name).save()   # as the desktop saves: not modified
        settle(300)
        outer = doc.Name
        served = Gui.serveDocument(doc, 0)
        hidden = Gui.serveBranch(doc, "away")
        settle(500)
        inner = hidden.Name
        check("the outer document has a window, the branch's none (%r, %r, %r)"
              % (served, views(outer), views(inner)),
              served is True and views(outer) == 1 and views(inner) == 0)

        # -- Saving the branch's document -----------------------------------
        hidden.openTransaction("taller")
        hidden.getObject("Box").Height = 30
        hidden.commitTransaction()
        hidden.recompute()
        settle()
        seen = answering(None, lambda: Gui.getDocument(inner).save())
        text = seen.get("text", "")
        check("saving it says first that it is not the branch the file is checked out on, "
              "and which is (%r)" % (text,),
              text.startswith("This is branch away of outer.FCStd, not the branch the file is "
                              "checked out on: that is main, in %s." % doc.Label)
              and "left open" not in seen)
        check("and still offers to save it over the file, or to history (%r)"
              % (seen.get("buttons"),),
              "Save over File" in (seen.get("buttons") or [])
              and "Save to History" in (seen.get("buttons") or []))

        # -- Closing the last window ----------------------------------------
        seen = answering(None, lambda: closeWindow(outer))
        text = seen.get("text", "")
        check("closing the outer document's window says what has none, by name (%r)"
              % (seen,),
              seen.get("box") == "HiddenDocuments" and hidden.Label in text
              and "leaves a document open" in text
              and sorted(seen.get("buttons") or []) == ["Cancel", "Leave Hidden", "Show Them"])
        check("cancelled, it stays, with its window, and the other with none (%r, %r)"
              % (views(outer), views(inner)), views(outer) == 1 and views(inner) == 0)

        seen = answering("HiddenDocumentsLeave", lambda: closeWindow(outer))
        settle(500)
        check("left hidden: the outer document closes, the branch's stays with no window "
              "(%r, %r)" % (views(outer), views(inner)),
              seen.get("box") == "HiddenDocuments" and views(outer) is None
              and views(inner) == 0)

        other = App.newDocument("Other").Name
        third = App.newDocument("Third").Name
        settle(300)
        seen = answering(None, lambda: closeWindow(third))
        check("a window closed while another document has one says nothing (%r)" % (seen,),
              seen.get("box") is None and views(third) is None and views(other) == 1)

        seen = answering("HiddenDocumentsShow", lambda: closeWindow(other))
        settle(800)
        check("the last window closed again, shown: the branch's document has a window now "
              "(%r, %r, %r)" % (seen.get("box"), views(other), views(inner)),
              seen.get("box") == "HiddenDocuments" and views(other) is None
              and views(inner) == 1)

        Gui.serveStop()
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
        shutil.rmtree(where, ignore_errors=True)
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
