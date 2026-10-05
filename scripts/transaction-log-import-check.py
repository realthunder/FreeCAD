# GUI check of docs/TransactionLog.md sec 30.13-30.17: another copy of the
# file brought in through the log panel's "Merge from file..." -- the copy's
# rows as a branch named after it, each under its author, then the merge
# dialog any branch gets, a side picked for a conflict, the view providers
# following, a second import continuing the branch, and a copy with no
# history coming as one row (sec 30.19). One GUI run in a fresh user home,
# given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-ic \
#     IMPORTCHECK_OUT=/tmp/ic/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-import-check.py
#
# It writes PASS/FAIL lines to $IMPORTCHECK_OUT and exits. The log setting
# is put back before it exits.
import os, shutil, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["IMPORTCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def colour(obj):
    return tuple(round(c, 3) for c in obj.ViewObject.ShapeColor[:3])


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
        if t.headerItem().text(1) == "Seq":
            tree = t
    return dock, tree


def column(tree, title):
    header = tree.headerItem()
    for c in range(header.columnCount()):
        if header.text(c) == title:
            return c
    return -1


def drive(seen, pick):
    # The merge dialog is modal: this runs inside its event loop.
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    tree = dialog.findChild(QtWidgets.QTreeWidget)
    rows = []
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        side = tree.itemWidget(item, column(tree, "Takes"))
        rows.append({"kind": item.text(0), "object": item.text(1), "prop": item.text(2)})
        if side is not None and item.text(0).startswith("conflict") and item.text(2) == pick:
            side.setCurrentIndex(side.findText("theirs"))
    seen["rows"] = rows
    seen["title"] = dialog.windowTitle()
    for b in dialog.findChildren(QtWidgets.QPushButton):
        if b.objectName() == "merge":
            b.click()
            return
    dialog.reject()


def bring(dock, path, pick="Width"):
    seen = {}
    QtCore.QTimer.singleShot(300, lambda: drive(seen, pick))
    QtCore.QMetaObject.invokeMethod(
        dock,
        "importFile",
        QtCore.Qt.DirectConnection,
        QtCore.Q_ARG(str, path),
        QtCore.Q_ARG(str, ""),
    )
    settle()
    # With no dialog the call is back before the timer fires: let it, or it
    # answers the next dialog instead of its own.
    waited = QtCore.QElapsedTimer()
    waited.start()
    while "found" not in seen and waited.elapsed() < 5000:
        QtWidgets.QApplication.processEvents(QtCore.QEventLoop.AllEvents, 50)
    return seen


def status(dock):
    return [l.text() for l in dock.findChildren(QtWidgets.QLabel)]


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 2)   # the file carries its history
        where = tempfile.mkdtemp()
        doc = App.newDocument("Ours")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        doc.openTransaction("red")
        box.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        doc.commitTransaction()
        path = os.path.join(where, "ours.FCStd")
        copy = os.path.join(where, "theirs.FCStd")
        doc.saveAs(path)
        shutil.copyfile(path, copy)
        settle()

        # This file goes on.
        doc.openTransaction("lower, narrow")
        box.Height = 5
        box.Width = 7
        doc.recompute()
        doc.commitTransaction()

        # The copy goes on, elsewhere.
        fork = App.openDocument(copy)
        settle()
        fork.openTransaction("theirs longer, narrower")
        fork.Box.Length = 30
        fork.Box.Width = 5
        fork.recompute()
        fork.commitTransaction()
        fork.openTransaction("theirs blue")
        fork.Box.ViewObject.ShapeColor = (0.0, 0.0, 1.0)
        fork.commitTransaction()
        fork.openTransaction("theirs cylinder")
        fork.addObject("Part::Cylinder", "Cylinder")
        fork.recompute()
        fork.commitTransaction()
        settle()
        fork.save()
        App.closeDocument(fork.Name)
        App.setActiveDocument(doc.Name)
        settle()

        offered = [b for b in doc.getTransactionForkBranches(copy) if b["current"]]
        check("the copy offers its branch, 3 ahead (%r)" % offered,
              len(offered) == 1 and offered[0]["base"] > 0 and offered[0]["ahead"] == 3)

        dock, tree = panel()
        check("panel found", dock is not None and tree is not None)
        if dock is not None:
            buttons = [b for b in dock.findChildren(QtWidgets.QPushButton)
                       if b.text().replace("&", "") == "Merge from file..."]
            check("panel: Merge from file... is there and enabled",
                  len(buttons) == 1 and buttons[0].isEnabled())
            documents = len(App.listDocuments())
            undos = doc.UndoCount
            seen = bring(dock, copy)
            check("dialog shown (%r)" % seen.get("title"), seen.get("found") is True)
            check("dialog: merges the branch named after the file",
                  "theirs" in (seen.get("title") or ""))
            rows = seen.get("rows", [])
            check("dialog: the width conflicts",
                  bool(rows) and rows[0]["kind"].startswith("conflict")
                  and rows[0]["prop"] == "Width")
            check("dialog: the cylinder is one row",
                  len([r for r in rows if r["object"] == "Cylinder"]) == 1)
            check("the replay's document is closed", len(App.listDocuments()) == documents)
            check("this document is still the active one", App.ActiveDocument is doc)

            box = doc.getObject("Box")
            cyl = doc.getObject("Cylinder")
            check("merged: length 30, theirs", abs(box.Length.Value - 30) < 1e-9)
            check("merged: height 5, ours", abs(box.Height.Value - 5) < 1e-9)
            check("merged: width 5, the side picked", abs(box.Width.Value - 5) < 1e-9)
            check("merged: recomputed, volume %g" % box.Shape.Volume,
                  abs(box.Shape.Volume - 30 * 5 * 5) < 1e-6)
            check("merged: box blue, theirs, ours not having changed it %r" % (colour(box),),
                  colour(box) == (0.0, 0.0, 1.0))
            check("merged: cylinder in, computed",
                  cyl is not None and cyl.Shape.Volume > 0 and "Touched" not in cyl.State)
            check("merged: one step", doc.UndoCount == undos + 1)

            branches = [b["name"] for b in doc.getTransactionBranches()]
            check("a branch named after the file (%r)" % branches, "theirs" in branches)
            for c in dock.findChildren(QtWidgets.QCheckBox):
                if c.text().replace("&", "") == "All branches":
                    c.setChecked(True)
            settle()
            acol = column(tree, "Author")
            bcol = column(tree, "Branch")
            kcol = column(tree, "Kind")
            ncol = column(tree, "Name")
            items = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())]
            came = [i for i in items if i.text(ncol).startswith("theirs")]
            check("panel: the copy's rows are on its branch (%d)" % len(came),
                  len(came) == 3 and {i.text(bcol) for i in came} == {"theirs"})
            check("panel: under the copy's own user (%r)" % {i.text(acol) for i in came},
                  bool(came) and all("theirs" in i.text(acol) for i in came))
            check("panel: the import's record is this user's",
                  len([i for i in items if i.text(kcol) == "import"
                       and "theirs" not in i.text(acol)]) == 1)
            log = {t["name"]: t for t in doc.getTransactionLog()}
            check("log: the rows' authors are of kind fork",
                  all(log[n]["author_kind"] == "fork" for n in log if n.startswith("theirs")))

            doc.undo()
            settle()
            box = doc.getObject("Box")
            check("undone: width 7, length 10, red, no cylinder",
                  abs(box.Width.Value - 7) < 1e-9 and abs(box.Length.Value - 10) < 1e-9
                  and colour(box) == (1.0, 0.0, 0.0) and doc.getObject("Cylinder") is None)
            doc.redo()
            settle()
            box = doc.getObject("Box")
            check("redone: volume %g, blue" % box.Shape.Volume,
                  abs(box.Shape.Volume - 750) < 1e-6 and colour(box) == (0.0, 0.0, 1.0))

            # Nothing more of the copy: no dialog, no row, and the panel says so.
            before = len(doc.getTransactionLog())
            seen = bring(dock, copy)
            check("again: no dialog, nothing written",
                  seen.get("found") is not True and len(doc.getTransactionLog()) == before)
            check("again: the panel says nothing new",
                  any("Nothing new" in text for text in status(dock)))

            # The copy goes on: a second import continues the branch.
            fork = App.openDocument(copy)
            settle()
            fork.openTransaction("theirs taller cylinder")
            fork.Cylinder.Height = 25
            fork.recompute()
            fork.commitTransaction()
            fork.save()
            App.closeDocument(fork.Name)
            App.setActiveDocument(doc.Name)
            settle()
            seen = bring(dock, copy)
            check("second: dialog shown", seen.get("found") is True)
            check("second: the same branch, continued (%r)"
                  % [b["name"] for b in doc.getTransactionBranches()],
                  [b["name"] for b in doc.getTransactionBranches()] == branches)
            cyl = doc.getObject("Cylinder")
            check("second: the cylinder is the one that came, taller",
                  cyl is not None and abs(cyl.Height.Value - 25) < 1e-9
                  and abs(doc.getObject("Box").Width.Value - 5) < 1e-9)

            # A copy handed out without its history (sec 30.19), edited
            # where there is no log, comes back as one row.
            plain = os.path.join(where, "plain.FCStd")
            doc.saveCopy(plain, False)
            doc.openTransaction("ours taller")
            doc.getObject("Box").Height = 12
            doc.recompute()
            doc.commitTransaction()
            p.SetInt("TransactionLog", 0)
            other = App.openDocument(plain)
            settle()
            other.Box.Length = 44
            other.recompute()
            other.save()
            App.closeDocument(other.Name)
            p.SetInt("TransactionLog", 2)
            App.setActiveDocument(doc.Name)
            settle()
            offered = doc.getTransactionForkBranches(plain)
            check("no history: the file offers itself, one thing to bring (%r)" % offered,
                  len(offered) == 1 and offered[0]["base"] > 0 and offered[0]["ahead"] == 1)
            seen = bring(dock, plain, pick="Length")
            check("no history: dialog shown (%r)" % seen.get("title"),
                  seen.get("found") is True and "plain" in (seen.get("title") or ""))
            box = doc.getObject("Box")
            check("no history: its length, this file's height, volume %g" % box.Shape.Volume,
                  abs(box.Length.Value - 44) < 1e-9 and abs(box.Height.Value - 12) < 1e-9
                  and abs(box.Shape.Volume - 44 * 5 * 12) < 1e-6)
            for c in dock.findChildren(QtWidgets.QCheckBox):
                if c.text().replace("&", "") == "All branches":
                    c.setChecked(True)
            settle()
            found = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())
                     if tree.topLevelItem(i).text(ncol) == "As found: plain"]
            check("no history: one row, the file as found, its author the file (%r)"
                  % [i.text(acol) for i in found],
                  len(found) == 1 and "(plain)" in found[0].text(acol)
                  and found[0].text(bcol) == "plain")
            before = len(doc.getTransactionLog())
            seen = bring(dock, plain)
            check("no history, again: nothing",
                  seen.get("found") is not True and len(doc.getTransactionLog()) == before
                  and any("Nothing new" in text for text in status(dock)))

            # A file that shares nothing is not refused (sec 30.22): it
            # comes as an independent branch and is merged by what it holds.
            alone = App.newDocument("Alone")
            alone.addObject("Part::Box", "Box")
            alone.addObject("Part::Cone", "Cone")
            alone.recompute()
            strange = os.path.join(where, "alone.FCStd")
            alone.saveAs(strange)
            App.closeDocument(alone.Name)
            App.setActiveDocument(doc.Name)
            settle()
            offered = doc.getTransactionForkBranches(strange)
            current = [b for b in offered if b["current"]]
            check("a stranger: offered as independent (%r)" % current,
                  len(current) == 1 and current[0]["independent"]
                  and current[0]["base"] == 0 and current[0]["ahead"] == 1)
            objects = len(doc.Objects)
            seen = bring(dock, strange, pick="")
            check("a stranger: dialog shown (%r)" % seen.get("title"),
                  seen.get("found") is True and "alone" in (seen.get("title") or ""))
            branch = [b for b in doc.getTransactionBranches() if b["name"] == "alone"]
            check("a stranger: a branch that hangs off no row",
                  len(branch) == 1 and branch[0]["from_seq"] == 0)
            # Its box is not this file's box: both are there, and its cone.
            check("a stranger: its objects beside this file's (%r)"
                  % sorted(o.Name for o in doc.Objects),
                  len(doc.Objects) == objects + 2 and doc.getObject("Cone") is not None
                  and abs(doc.getObject("Box").Length.Value - 44) < 1e-9)
            check("a stranger: everything computed",
                  not [o.Name for o in doc.Objects if "Touched" in o.State or "Invalid" in o.State])
            before = len(doc.getTransactionLog())
            seen = bring(dock, strange)
            check("a stranger, again: nothing",
                  seen.get("found") is not True and len(doc.getTransactionLog()) == before
                  and any("Nothing new" in text for text in status(dock)))
        # Sec 30.33, 30.34: a copy that took from this file, and comes back.
        # This file's cone, green, is taken by a copy that has work of its
        # own -- so under another id there, and in a merge row of the
        # copy's -- and made taller. Back here it is this file's cone, the
        # copy's merge is a merge, and nothing of this file is taken back:
        # the colour is a view provider's, which a replay with no Gui would
        # have left out.
        ours = App.newDocument("RoundOurs")
        ours.UndoMode = 1
        ours.openTransaction("base")
        ours.addObject("Part::Box", "Box")
        ours.recompute()
        ours.commitTransaction()
        settle()
        mine = os.path.join(where, "round.FCStd")
        copy = os.path.join(where, "roundcopy.FCStd")
        ours.saveAs(mine)
        shutil.copyfile(mine, copy)
        ours.openTransaction("ours cone")
        cone = ours.addObject("Part::Cone", "Cone")
        ours.recompute()
        ours.commitTransaction()
        settle()
        ours.openTransaction("ours green")
        cone.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
        ours.commitTransaction()
        settle()
        ours.save()
        coneId = cone.ID
        green = [t["seq"] for t in ours.getTransactionLog() if t["name"] == "ours green"]
        theirs = App.openDocument(copy)
        theirs.UndoMode = 1
        # Made and coloured in one row: every view value of it is in that
        # row, and they are one colour under several names.
        theirs.openTransaction("theirs cyl")
        theirs.addObject("Part::Cylinder", "Cyl")
        theirs.recompute()
        theirs.Cyl.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        theirs.commitTransaction()
        settle()
        took = theirs.importTransactionFork(mine)
        theirs.mergeTransactionBranch(took["branch"])
        settle()
        check("round trip: the copy has the cone, green, under an id of its own (%r, %r)"
              % (colour(theirs.Cone), theirs.Cone.ID),
              colour(theirs.Cone) == (0.0, 1.0, 0.0) and theirs.Cone.ID != coneId)
        theirs.openTransaction("theirs taller")
        theirs.Cone.Height = 20
        theirs.recompute()
        theirs.commitTransaction()
        settle()
        theirs.save()
        App.closeDocument(theirs.Name)
        App.setActiveDocument(ours.Name)
        settle()
        res = ours.importTransactionFork(copy)
        came = [t for t in ours.getTransactionLog() if t["branch"] == res["branch"]]
        merges = [t for t in came if t["kind"] == "merge"]
        check("round trip: nothing stopped, nothing renamed (%r)" % res,
              res["stopped_at"] == 0 and res["renamed"] == {} and res["rows"] == 3)
        check("round trip: the copy's merge is a merge here, of this file's last row (%r, %r)"
              % ([(t["kind"], t["name"]) for t in came], green),
              len(merges) == 1 and len(green) == 1 and merges[0]["merge_from"] == green[0])
        made = [(o["ckind"], o["cid"], o.get("cname")) for t in merges
                for o in ours.getTransactionOps(t["seq"]) if o["op"] == "create"]
        check("round trip: the row makes this file's cone, and its view provider (%r)" % made,
              sorted(made) == [("obj", coneId, "Cone"), ("view", coneId, "Cone")])
        preview = ours.previewTransactionMerge(res["branch"])
        check("round trip: the merge starts where the copy took this file (%r)"
              % preview["base"], len(green) == 1 and preview["base"] == green[0]
              and preview["conflicts"] == 0)
        merged = ours.mergeTransactionBranch(res["branch"])
        settle()
        check("round trip: merged with nothing to ask (%r)" % merged["unresolved"],
              merged["unresolved"] == [] and merged["failed"] == [])
        check("round trip: one cone, this file's (%r)" % sorted(o.Name for o in ours.Objects),
              sorted(o.Name for o in ours.Objects) == ["Box", "Cone", "Cyl"]
              and ours.Cone.ID == coneId)
        check("round trip: the copy's change is on it, and computed (%r)" % ours.Cone.Height,
              abs(ours.Cone.Height.Value - 20) < 1e-9
              and not [o.Name for o in ours.Objects
                       if "Touched" in o.State or "Invalid" in o.State])
        check("round trip: the colour it was given here is still its (%r)" % (colour(ours.Cone),),
              colour(ours.Cone) == (0.0, 1.0, 0.0))
        check("round trip: the copy's cylinder has the colour it was made with (%r)"
              % (colour(ours.Cyl),), colour(ours.Cyl) == (1.0, 0.0, 0.0))
        App.closeDocument(ours.Name)
        settle()
        shutil.rmtree(where, ignore_errors=True)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
