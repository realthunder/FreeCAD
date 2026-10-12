# GUI check of docs/TransactionLog.md sec 16.7, the squash, as the log panel
# offers it: the versions list's menu entry, the list of versions to squash
# from, the confirmation, the rows folded into one, and what the log refuses
# said in the status line. One GUI run in a fresh user home, given this
# script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-sq \
#     SQUASHCHECK_OUT=/tmp/sq/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-squash-check.py
#
# It writes PASS/FAIL lines to $SQUASHCHECK_OUT and exits. The log setting
# is put back before it exits.
import os, re, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["SQUASHCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle():
    for _ in range(5):
        QtWidgets.QApplication.processEvents()


def panel():
    dock = None
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            dock = w
    if not dock:
        return None, None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    tree = None
    for t in dock.findChildren(QtWidgets.QTreeWidget):
        if t.headerItem().text(0) == "Num":
            tree = t
    tabs = dock.findChild(QtWidgets.QTabWidget)
    if tabs is not None and tree is not None:
        tabs.setCurrentIndex(tabs.indexOf(tree))
        settle()
    return dock, tree


def status(dock):
    return [l.text() for l in dock.findChildren(QtWidgets.QLabel)]


def says(dock, text):
    return any(text in s for s in status(dock))


def row(tree, num):
    for i in range(tree.topLevelItemCount()):
        if tree.topLevelItem(i).text(0) == str(num):
            return tree.topLevelItem(i)
    return None


def shown(tree):
    return [int(tree.topLevelItem(i).text(0)) for i in range(tree.topLevelItemCount())]


def drive(seen, pick=None, answer=None, tries=12):
    """What the user does once the versions menu is up: the squash entry
    pressed (or the menu left, with no `pick`), version `pick` of the list
    chosen, the confirmation answered. Each step waits for its window."""

    def again():
        if tries > 0:
            QtCore.QTimer.singleShot(150, lambda: drive(seen, pick, answer, tries - 1))

    app = QtWidgets.QApplication
    popup = app.activePopupWidget()
    if isinstance(popup, QtWidgets.QMenu):
        entry = None
        for a in popup.actions():
            if a.text().startswith("Squash"):
                entry = a
        seen["entry"] = entry.text() if entry else None
        seen["enabled"] = bool(entry and entry.isEnabled())
        if entry is None or not entry.isEnabled() or pick is None:
            popup.close()
            return
        popup.setActiveAction(entry)
        app.sendEvent(popup, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, QtCore.Qt.Key_Return,
                                             QtCore.Qt.NoModifier))
        again()
        return
    modal = app.activeModalWidget()
    if isinstance(modal, QtWidgets.QInputDialog):
        seen["label"] = modal.labelText()
        seen["list"] = list(modal.comboBoxItems())
        if pick is None or pick not in seen["list"]:
            modal.reject()
            return
        modal.findChild(QtWidgets.QComboBox).setCurrentIndex(seen["list"].index(pick))
        modal.accept()
        again()
        return
    if isinstance(modal, QtWidgets.QMessageBox):
        seen["asked"] = modal.text()
        button = QtWidgets.QMessageBox.Yes if answer == "yes" else QtWidgets.QMessageBox.No
        modal.button(button).click()
        return
    again()


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Squash")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()

        def step(name, prop, value):
            doc.openTransaction(name)
            setattr(box, prop, value)
            doc.recompute()
            doc.commitTransaction()
            settle()

        step("long", "Length", 20)
        a = doc.snapshotTransactionLog()
        step("wide", "Width", 30)
        step("wider", "Width", 35)
        b = doc.snapshotTransactionLog()
        step("high", "Height", 40)
        step("named", "Label", "Squashed")
        c = doc.snapshotTransactionLog()
        step("longer", "Length", 50)
        d = doc.snapshotTransactionLog()
        settle()
        size = lambda: (box.Length.Value, box.Width.Value, box.Height.Value)
        versions = lambda: {v["num"]: v for v in doc.getTransactionVersions()}
        rows = lambda: [(t["seq"], t["kind"]) for t in doc.getTransactionLog()]
        check("four versions taken (%r)" % ([a, b, c, d],), 0 < a < b < c < d)

        dock, tree = panel()
        check("panel found, the versions listed (%r)" % (tree and shown(tree),),
              dock is not None and tree is not None
              and all(row(tree, n) is not None for n in (a, b, c, d)))

        def menu(num, seen, pick=None, answer=None):
            item = row(tree, num)
            tree.scrollToItem(item)
            settle()
            QtCore.QTimer.singleShot(150, lambda: drive(seen, pick, answer))
            tree.customContextMenuRequested.emit(tree.visualItemRect(item).center())
            settle()

        # The oldest version has nothing behind it to squash from.
        seen = {}
        menu(a, seen)
        check("menu: the entry is there, and off where no version is behind (%r)" % (seen,),
              seen.get("entry") == "Squash to version %d from..." % a
              and seen.get("enabled") is False)

        # Squash to c from a: b, unnamed and between, goes; d still follows.
        before = rows()
        seqs = versions()
        seen = {}
        menu(c, seen, pick="Version %d" % a, answer="yes")
        now = versions()
        after = rows()
        check("menu: the versions behind, newest first (%r, %r)" % (seen.get("label"), seen.get("list")),
              seen.get("enabled") is True
              and seen.get("label") == "Squash to version %d from:" % c
              and seen.get("list") == ["Version %d" % b, "Version %d" % a])
        asked = re.search(r"Fold the (\d+) rows from version (\d+) to version (\d+) into one",
                          seen.get("asked") or "")
        folded = [s for s in status(dock) if s.startswith("Squashed")]
        said = re.search(r"Squashed (\d+) rows from version (\d+) into row (\d+)",
                         folded[0] if folded else "")
        check("asked first, with what it folds (%r), and said after (%r)" % (seen.get("asked"), folded),
              asked is not None and said is not None
              and asked.groups() == (said.group(1), str(a), str(c))
              and int(said.group(1)) > 1
              and said.groups()[1:] == (str(a), str(seqs[c]["seq"])))
        check("squashed: the version between is gone, the two ends stay (%r)" % (sorted(now),),
              a in now and c in now and d in now and b not in now)
        check("squashed: one row of kind squash where the newer version is (%r)" % (after,),
              (seqs[c]["seq"], "squash") in after and len(after) < len(before)
              and not [s for s, _ in after if seqs[a]["seq"] < s < seqs[c]["seq"]]
              and now[c]["seq"] == seqs[c]["seq"] and now[d]["seq"] == seqs[d]["seq"])
        check("squashed: the list follows (%r)" % (shown(tree),),
              row(tree, b) is None and row(tree, a) is not None and row(tree, c) is not None)
        check("squashed: the document is what it was (%r, %r)" % (size(), box.Label),
              size() == (50.0, 35.0, 40.0) and box.Label == "Squashed")

        # The squash is one step: undone after the step that follows it, the
        # document is what it was at the older version; and redone.
        names = list(doc.UndoNames)
        doc.undo()
        doc.undo()
        settle()
        back = (size(), box.Label)
        doc.redo()
        doc.redo()
        settle()
        check("squashed: undone as one step, and redone (%r, %r, %r)" % (names[:3], back, size()),
              back == ((20.0, 10.0, 10.0), "Box") and size() == (50.0, 35.0, 40.0)
              and box.Label == "Squashed"
              and not [o.Name for o in doc.Objects if "Invalid" in o.State])

        # A named version between is refused by the log, said in the status line.
        doc.nameTransactionVersion(c, "keep")
        settle()
        dock, tree = panel()
        before = rows()
        seen = {}
        QtCore.QTimer.singleShot(150, lambda: drive(seen, "Version %d" % a, "yes"))
        QtCore.QMetaObject.invokeMethod(dock, "squashTo", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG("qlonglong", d))
        settle()
        refused = [s for s in status(dock) if s.startswith("Not squashed")]
        check("refused: a named version between, said in the status line (%r, %r)"
              % (seen.get("list"), refused),
              seen.get("list") == ["Version %d (keep)" % c, "Version %d" % a]
              and refused == ["Not squashed: named version %d sits between the versions" % c]
              and rows() == before and sorted(versions()) == sorted(now))

        # The confirmation answered no: nothing goes.
        seen = {}
        QtCore.QTimer.singleShot(150, lambda: drive(seen, "Version %d (keep)" % c, "no"))
        QtCore.QMetaObject.invokeMethod(dock, "squashTo", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG("qlonglong", d))
        settle()
        check("not confirmed: nothing goes (%r)" % (seen.get("asked"),),
              "asked" in seen and rows() == before and size() == (50.0, 35.0, 40.0))

        # Nothing behind: no list, a line in the status.
        seen = {}
        QtCore.QTimer.singleShot(150, lambda: drive(seen, None, None, 3))
        QtCore.QMetaObject.invokeMethod(dock, "squashTo", QtCore.Qt.DirectConnection,
                                        QtCore.Q_ARG("qlonglong", a))
        settle()
        check("nothing behind: no list is put up (%r)" % (seen,),
              "list" not in seen and says(dock, "No version behind version %d" % a))
        for _ in range(8):
            QtCore.QThread.msleep(100)
            settle()
        App.closeDocument(doc.Name)
        settle()
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
