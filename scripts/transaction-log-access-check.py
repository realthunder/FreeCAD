# GUI check of docs/TransactionLog.md sec 32.25, the desktop's side: who may
# what of a branch is said in the log panel's "Access..." dialog -- the users
# the log knows that are not the desktop's own, read or write, the branch,
# and for a write the version it starts at -- "Serve branch..." makes a
# branch where the document is and serves it in a document of its own, and
# the merges asked for are in the panel's request list, done by "Merge..."
# where the document is on the branch they go into and ended by "Reject".
# And two things of the log itself that a client's ops lean on: a point to
# write from is a version of the branch, and an apply refused for a conflict
# leaves a merge that was undone to be redone.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-ax \
#     ACCESSCHECK_OUT=/tmp/ax/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-access-check.py
#
# It writes PASS/FAIL lines to $ACCESSCHECK_OUT and exits. The setting it
# changes is put back before it exits.
import json, os, shutil, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["ACCESSCHECK_OUT"]
lines = []

BOB = "bob (invited)"
ALICE = "alice@example.com (verified)"


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle(ms=0):
    waited = QtCore.QElapsedTimer()
    waited.start()
    for _ in range(5):
        QtWidgets.QApplication.processEvents()
    while waited.elapsed() < ms:
        QtWidgets.QApplication.processEvents(QtCore.QEventLoop.AllEvents, 50)


def named(parent, kind, name):
    for w in parent.findChildren(kind):
        if w.objectName() == name:
            return w
    return None


def panel():
    dock = None
    for w in Gui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::DockWnd::TransactionLogView":
            dock = w
    if not dock:
        return None
    parent = dock.parentWidget()
    if isinstance(parent, QtWidgets.QDockWidget):
        parent.show()
    dock.show()
    settle()
    return dock


def rows(tree):
    out = []
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        out.append([item.text(c) for c in range(tree.columnCount())])
    return out


def texts(combo):
    return [combo.itemText(i) for i in range(combo.count())]


def told(dock):
    return [l.text() for l in dock.findChildren(QtWidgets.QLabel)]


def unstick(seen):
    # Whatever is still up, so a dialog nobody answered ends the run with a
    # failure and not with a hang.
    modal = QtWidgets.QApplication.activeModalWidget()
    if modal is not None:
        seen["left open"] = modal.objectName() or modal.metaObject().className()
        modal.reject()
    popup = QtWidgets.QApplication.activePopupWidget()
    if popup is not None:
        seen["left open"] = "menu"
        popup.close()


def withAccess(dock, act):
    """Press "Access...", and have `act(dialog, seen)` work the dialog."""
    seen = {}

    def drive():
        dialog = QtWidgets.QApplication.activeModalWidget()
        if dialog is None or dialog.objectName() != "TransactionAccessDialog":
            seen["found"] = False
            if dialog is not None:
                dialog.reject()
            return
        seen["found"] = True
        try:
            act(dialog, seen)
        except Exception:
            seen["error"] = traceback.format_exc()
        dialog.accept()

    QtCore.QTimer.singleShot(300, drive)
    QtCore.QTimer.singleShot(6000, lambda: unstick(seen))
    named(dock, QtWidgets.QPushButton, "TransactionAccess").click()
    settle()
    return seen


def say(dialog, user, level, branch, point=None):
    """One line of the dialog: the user, reads or writes, the branch, and
    for a write the point by the version's number; then "Set"."""
    who = named(dialog, QtWidgets.QComboBox, "TransactionAccessUser")
    may = named(dialog, QtWidgets.QComboBox, "TransactionAccessLevel")
    where = named(dialog, QtWidgets.QComboBox, "TransactionAccessBranch")
    start = named(dialog, QtWidgets.QComboBox, "TransactionAccessFrom")
    who.setCurrentIndex(who.findText(user))
    may.setCurrentIndex(may.findText(level))
    where.setCurrentIndex(where.findText(branch))
    if point is not None:
        start.setCurrentIndex(start.findData(point))
    named(dialog, QtWidgets.QPushButton, "TransactionAccessSet").click()
    settle()


def serveBranch(dock, text):
    """Press "Serve branch...", and answer the name it asks for."""
    seen = {}

    def answer():
        dialog = QtWidgets.QApplication.activeModalWidget()
        seen["found"] = isinstance(dialog, QtWidgets.QInputDialog)
        if seen["found"]:
            dialog.setTextValue(text)
            dialog.accept()
        elif dialog is not None:
            dialog.reject()

    QtCore.QTimer.singleShot(300, answer)
    QtCore.QTimer.singleShot(6000, lambda: unstick(seen))
    named(dock, QtWidgets.QPushButton, "TransactionServeBranch").click()
    settle(300)
    return seen


def requestItem(requests, text):
    for i in range(requests.topLevelItemCount()):
        if requests.topLevelItem(i).text(0) == text:
            return requests.topLevelItem(i)
    return None


def menuOf(dock, requests, item, pick=None):
    """The context menu of a request: its entries, and `pick` chosen."""
    seen = {}

    def drive():
        menu = QtWidgets.QApplication.activePopupWidget()
        seen["found"] = isinstance(menu, QtWidgets.QMenu)
        if not seen["found"]:
            return
        seen["entries"] = [(a.text(), a.isEnabled()) for a in menu.actions()]
        for a in menu.actions():
            if pick and a.text() == pick and a.isEnabled():
                a.trigger()
        menu.close()

    QtCore.QTimer.singleShot(300, drive)
    QtCore.QTimer.singleShot(6000, lambda: unstick(seen))
    QtCore.QMetaObject.invokeMethod(
        dock, "onRequestContextMenu", QtCore.Qt.DirectConnection,
        QtCore.Q_ARG(QtCore.QPoint, requests.visualItemRect(item).center()),
    )
    settle(100)
    return seen


def driveMerge(seen):
    # The merge's preview is not modal (sec 32.15): found among the windows.
    dialog = None
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == "TransactionMergeDialog" and w.isVisible():
            dialog = w
    seen["found"] = dialog is not None
    if dialog is None:
        modal = QtWidgets.QApplication.activeModalWidget()
        if modal is not None:
            modal.reject()
        return
    seen["title"] = dialog.windowTitle()
    for b in dialog.findChildren(QtWidgets.QPushButton):
        if b.objectName() == "merge":
            b.click()
            return
    dialog.reject()


def control(payload, client=7, access="edit"):
    # An op as a client of a served document would send it
    payload = dict(payload)
    payload.setdefault("id", 1)
    return json.loads(Gui.FormWidgets.control(json.dumps(payload), client, False, access))


def named_row(doc, name):
    for t in doc.getTransactionLog():
        if t["name"] == name:
            return t["seq"]
    return 0


def asked(doc, number):
    for r in doc.getTransactionMergeRequests():
        if r["id"] == number:
            return r
    return {}


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 2)   # the file carries its history
        where = tempfile.mkdtemp()
        doc = App.newDocument("Access")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        doc.saveAs(os.path.join(where, "access.FCStd"))
        settle()

        # Two who came to it and wrote a row each: the log knows them.
        for kind, name, prop, value in (("invited", "bob", "Width", 12),
                                        ("verified", "alice@example.com", "Height", 14)):
            doc.pushTransactionActor(kind, name)
            try:
                doc.openTransaction("%s by %s" % (prop, name))
                setattr(box, prop, value)
                doc.commitTransaction()
            finally:
                doc.popTransactionActor()
        doc.recompute()
        handover = doc.snapshotTransactionLog()
        doc.nameTransactionVersion(handover, "handover")
        doc.createTransactionBranch("side")
        doc.openTransaction("longer")
        box.Length = 30
        doc.commitTransaction()
        doc.recompute()
        doc.switchTransactionBranch("main")
        settle()

        dock = panel()
        check("panel found", dock is not None)
        if dock is None:
            raise RuntimeError("no log panel")
        requests = named(dock, QtWidgets.QTreeWidget, "TransactionRequests")
        access = named(dock, QtWidgets.QPushButton, "TransactionAccess")
        serve = named(dock, QtWidgets.QPushButton, "TransactionServeBranch")
        check("the panel has Access... and Serve branch...",
              access is not None and serve is not None and requests is not None)
        check("a document nobody is served: access is said, no branch is served (%r, %r)"
              % (access.isEnabled(), serve.isEnabled()),
              access.isEnabled() and not serve.isEnabled())

        # -- Access... ----------------------------------------------------
        def first(dialog, seen):
            who = named(dialog, QtWidgets.QComboBox, "TransactionAccessUser")
            may = named(dialog, QtWidgets.QComboBox, "TransactionAccessLevel")
            branch = named(dialog, QtWidgets.QComboBox, "TransactionAccessBranch")
            start = named(dialog, QtWidgets.QComboBox, "TransactionAccessFrom")
            said = named(dialog, QtWidgets.QTreeWidget, "TransactionAccessList")
            seen["users"] = texts(who)
            seen["branches"] = texts(branch)
            seen["levels"] = texts(may)
            seen["empty"] = rows(said)
            may.setCurrentIndex(0)
            seen["from off"] = start.isEnabled()

            say(dialog, BOB, "reads", "side")
            seen["read"] = rows(said)
            seen["read said"] = doc.getTransactionAccess()

            may.setCurrentIndex(1)
            branch.setCurrentIndex(branch.findText("main"))
            settle()
            seen["from on"] = start.isEnabled()
            seen["points"] = [(start.itemText(i), start.itemData(i))
                              for i in range(start.count())]
            say(dialog, ALICE, "writes", "main", handover)
            seen["write"] = rows(said)
            seen["write said"] = doc.getTransactionAccess()

            # Said again of the same user and branch: in place of the first
            say(dialog, BOB, "writes", "side", 0)
            seen["again"] = rows(said)

            for i in range(said.topLevelItemCount()):
                if said.topLevelItem(i).text(0) == BOB:
                    said.setCurrentItem(said.topLevelItem(i))
            named(dialog, QtWidgets.QPushButton, "TransactionAccessRemove").click()
            settle()
            seen["removed"] = rows(said)

        seen = withAccess(dock, first)
        check("Access... opens its dialog (%r)" % (seen.get("error") or seen.get("left open"),),
              seen.get("found") is True and "error" not in seen and "left open" not in seen)
        check("it offers the users the log knows, not the desktop's own (%r)"
              % (seen.get("users"),),
              sorted(seen.get("users", [])) == sorted([BOB, ALICE]))
        check("and the branches, to read or to write (%r, %r)"
              % (seen.get("branches"), seen.get("levels")),
              seen.get("branches") == ["main", "side"]
              and seen.get("levels") == ["reads", "writes"])
        check("nothing is said to start with (%r)" % (seen.get("empty"),),
              seen.get("empty") == [])
        check("a read has no point to start at", seen.get("from off") is False)
        check("a read set is listed, and is in the log (%r, %r)"
              % (seen.get("read"), seen.get("read said")),
              seen.get("read") == [[BOB, "side", "reads", ""]]
              and [(a["kind"], a["name"], a["branch"], a["write"])
                   for a in seen.get("read said", [])] == [("invited", "bob", "side", False)])
        points = seen.get("points", [])
        check("a write starts where the branch forks or at a version of it (%r)" % (points,),
              seen.get("from on") is True and len(points) >= 2
              and points[0] == ("where it forks", 0)
              and ("version %d (handover)" % handover, handover) in points)
        wrote = [a for a in seen.get("write said", []) if a["name"] == "alice@example.com"]
        check("a write from a version is listed with it, and is in the log (%r, %r)"
              % (seen.get("write"), wrote),
              [ALICE, "main", "writes", "version %d (handover)" % handover]
              in seen.get("write", []) and len(seen.get("write", [])) == 2
              and len(wrote) == 1 and wrote[0]["write"] is True
              and wrote[0]["from_version"] == handover and wrote[0]["from_seq"] > 0)
        check("said again of the same user and branch, it is in place of the first (%r)"
              % (seen.get("again"),),
              len(seen.get("again", [])) == 2
              and [BOB, "side", "writes", "where it forks"] in seen.get("again", []))
        check("removed, it is gone from the list and from the log (%r, %r)"
              % (seen.get("removed"), doc.getTransactionAccess()),
              seen.get("removed") == [[ALICE, "main", "writes",
                                       "version %d (handover)" % handover]]
              and [a["name"] for a in doc.getTransactionAccess()] == ["alice@example.com"])

        def second(dialog, seen):
            seen["listed"] = rows(named(dialog, QtWidgets.QTreeWidget, "TransactionAccessList"))

        seen = withAccess(dock, second)
        check("opened again, it lists what the log says (%r)" % (seen.get("listed"),),
              seen.get("listed") == [[ALICE, "main", "writes",
                                      "version %d (handover)" % handover]])

        # -- A group (sec 32.32) -------------------------------------------
        def grouped(dialog, seen):
            who = named(dialog, QtWidgets.QComboBox, "TransactionAccessUser")
            group = named(dialog, QtWidgets.QComboBox, "TransactionAccessGroup")
            who.setCurrentIndex(who.findText(BOB))
            settle()
            seen["before"] = group.currentText()
            group.setCurrentText("team")
            named(dialog, QtWidgets.QPushButton, "TransactionAccessPutIn").click()
            settle()
            seen["users"] = [who.itemText(i) for i in range(who.count())]
            seen["chosen"] = (who.currentText(), group.currentText())
            seen["groups"] = [group.itemText(i) for i in range(group.count())]

        seen = withAccess(dock, grouped)
        users = {(u["kind"], u["name"]): u["group"] for u in doc.getTransactionUsers()}
        check("a user put in a group by a new name: the group is made, and listed among the "
              "users; the user's line says its group, and stays chosen (%r)" % (seen,),
              seen.get("before") == "" and "team (group)" in seen.get("users", [])
              and "bob (invited), in team" in seen.get("users", [])
              and seen.get("chosen") == ("bob (invited), in team", "team")
              and "team" in seen.get("groups", [])
              and users.get(("invited", "bob")) == "team"
              and users.get(("group", "team")) == "")
        doc.setTransactionUserGroup("invited", "bob", "")
        check("and taken out again, from Python",
              {(u["kind"], u["name"]): u["group"]
               for u in doc.getTransactionUsers()}.get(("invited", "bob")) == "")

        # -- Serve branch... ----------------------------------------------
        served = Gui.serveDocument(doc, 0)
        settle(300)
        check("the document shared, a branch of it can be served (%r, %r)"
              % (served, serve.isEnabled()), served is True and serve.isEnabled())
        before = set(App.listDocuments())
        seen = serveBranch(dock, "handed")
        made = sorted(set(App.listDocuments()) - before)
        names = [b["name"] for b in doc.getTransactionBranches()]
        other = App.getDocument(made[0]) if len(made) == 1 else None
        on = {b["id"]: b["name"] for b in doc.getTransactionBranches()}
        check("Serve branch... asks its name (%r)" % (seen,),
              seen.get("found") is True and "left open" not in seen)
        check("the branch is made, in a document of its own that is on it (%r, %r)"
              % (made, names),
              other is not None and "handed" in names
              and on.get(other.getTransactionCursor()["branch"]) == "handed")
        check("the first document stays where it is",
              on.get(doc.getTransactionCursor()["branch"]) == "main")
        check("the panel says the document a client joins (%r)" % (told(dock),),
              other is not None
              and any("handed" in t and other.Name in t for t in told(dock)))
        settle(1500)
        check("and goes on saying it, refreshed (%r)" % (told(dock),),
              other is not None
              and any("handed served in " + other.Name in t for t in told(dock)))
        count = len(App.listDocuments())
        seen = serveBranch(dock, "handed")
        check("the branch named again is the one served already: no document more, and the "
              "panel says where (%r)" % (told(dock),),
              len(App.listDocuments()) == count and other is not None
              and any(t.startswith("Branch handed is served in document " + other.Name)
                      for t in told(dock)))

        # -- The merges asked for -----------------------------------------
        if other is not None:
            other.openTransaction("taller")
            other.getObject("Box").Height = 40
            other.commitTransaction()
            other.recompute()
            settle()
            # And something here since, so that the merge is one: a row of
            # its own, not the other branch's rows taken as they are
            doc.openTransaction("deeper")
            box.Length = 22
            doc.commitTransaction()
            doc.recompute()
            settle()
            doc.pushTransactionActor("invited", "bob")
            try:
                back = doc.requestTransactionMerge("handed", "main", "my work")
                try:
                    doc.requestTransactionMerge("handed", "main")
                    twice = True
                except Exception:
                    twice = False
            finally:
                doc.popTransactionActor()
            away = doc.requestTransactionMerge("side", "handed")
            settle(300)
            listed = rows(requests)
            mine = [r for r in listed if r[0] == "merge handed into main"]
            theirs = [r for r in listed if r[0] == "merge side into handed"]
            check("a merge asked for is in the panel's list, with who asked and the note (%r)"
                  % (listed,),
                  requests.isVisible() and len(mine) == 1 and mine[0][1] == BOB
                  and mine[0][3] == "my work" and len(theirs) == 1
                  and theirs[0][1] != "" and "bob" not in theirs[0][1]
                  and theirs[0][3] == "asked")
            check("the same asked twice is refused", twice is False)

            # One that goes into another branch than the document's
            item = requestItem(requests, "merge side into handed")
            if item is not None:
                requests.itemActivated.emit(item, 0)
                settle(300)
                check("one into another branch is not merged here, and the panel says where "
                      "(%r)" % (told(dock),),
                      asked(doc, away).get("state") == "open"
                      and any("merge into handed" in t for t in told(dock)))
                seen = menuOf(dock, requests, item, "Reject")
                settle(300)
                entries = seen.get("entries", [])
                check("its menu: Merge... is not offered here, Reject is (%r)" % (entries,),
                      seen.get("found") is True and len(entries) == 2
                      and entries[0][0].startswith("Merge...") and entries[0][1] is False
                      and entries[1] == ("Reject", True))
                ended = asked(doc, away)
                check("rejected: it says so and who did, and is out of the list (%r, %r)"
                      % ((ended.get("state"), ended.get("closed_by")), rows(requests)),
                      ended.get("state") == "rejected" and ended.get("closed_by", "") != ""
                      and requestItem(requests, "merge side into handed") is None
                      and requestItem(requests, "merge handed into main") is not None)

            # One into the branch the document is on: Merge... does it
            item = requestItem(requests, "merge handed into main")
            if item is not None:
                seen = {}
                QtCore.QTimer.singleShot(300, lambda: driveMerge(seen))
                QtCore.QTimer.singleShot(8000, lambda: unstick(seen))
                requests.itemActivated.emit(item, 0)
                settle(1500)
                ended = asked(doc, back)
                merges = [t for t in doc.getTransactionLog() if t["kind"] == "merge"]
                check("activated where the document is on its target, the merge's preview "
                      "opens (%r)" % (seen,),
                      seen.get("found") is True and "left open" not in seen)
                check("merged: the document has the work, and the request the row (%r, %r)"
                      % (box.Height.Value, (ended.get("state"), ended.get("result"))),
                      abs(box.Height.Value - 40) < 1e-9 and abs(box.Length.Value - 22) < 1e-9
                      and ended.get("state") == "merged"
                      and len(merges) == 1 and ended.get("result") == merges[-1]["seq"]
                      and requestItem(requests, "merge handed into main") is None)

        # A merge done another way answers what asked for it
        once = doc.requestTransactionMerge("side", "main", "the length")
        settle(300)
        check("asked from the desktop, it is listed too (%r)" % (rows(requests),),
              requestItem(requests, "merge side into main") is not None)
        result = doc.mergeTransactionBranch("side", {}, "theirs")
        settle(300)
        ended = asked(doc, once)
        check("merged from Python, the request is answered by it (%r, %r)"
              % (result.get("seq"), (ended.get("state"), ended.get("result"))),
              result.get("seq", 0) > 0 and ended.get("state") == "merged"
              and ended.get("result") == result.get("seq")
              and abs(box.Length.Value - 30) < 1e-9
              and requestItem(requests, "merge side into main") is None)

        # A branch deleted takes what was said of it with it
        doc.setTransactionAccess("invited", "bob", "side", True)
        said = [(a["name"], a["branch"]) for a in doc.getTransactionAccess()]
        doc.deleteTransactionBranch("side")
        settle(300)
        check("a branch deleted takes what was said of it (%r -> %r)"
              % (said, doc.getTransactionAccess()),
              ("bob", "side") in said
              and [(a["name"], a["branch"]) for a in doc.getTransactionAccess()]
              == [("alice@example.com", "main")])

        # A point to write from is a version of the branch itself
        try:
            doc.setTransactionAccess("invited", "bob", "handed", True, handover)
            foreign = True
        except Exception:
            foreign = False
        check("a write is not said from a version of another branch (%r)"
              % (doc.getTransactionAccess(),),
              foreign is False
              and not any(a["branch"] == "handed" for a in doc.getTransactionAccess()))

        # What is refused moves nothing: an apply that meets a conflict
        # with no side said leaves a merge that was undone to be redone
        two = App.newDocument("Refusal")
        two.openTransaction("box")
        part = two.addObject("Part::Box", "Box")
        two.commitTransaction()
        two.recompute()
        two.saveAs(os.path.join(where, "refusal.FCStd"))

        def step(name, prop, value):
            two.openTransaction(name)
            setattr(part, prop, value)
            two.commitTransaction()
            two.recompute()

        two.createTransactionBranch("b")
        step("b longer", "Length", 30)
        two.switchTransactionBranch("main")
        step("main longer", "Length", 20)
        two.createTransactionBranch("c")
        step("c taller", "Height", 7)
        two.switchTransactionBranch("main")
        step("main wider", "Width", 6)
        merged = two.mergeTransactionBranch("c")
        settle()
        two.undo()
        settle()
        kinds = [t["kind"] for t in two.getTransactionLog()]
        redo = two.RedoCount
        who = control({"op": "log.rows", "doc": two.Name, "limit": 1})
        two.setTransactionAccess("declared", "guest", "b")
        reply = control({"op": "log.apply", "doc": two.Name, "rows": [named_row(two, "b longer")]})
        settle()
        check("a merge made and undone, with nothing since (%r, %r)" % (merged.get("seq"), redo),
              merged.get("seq", 0) > 0 and redo == 1 and abs(part.Height.Value - 10) < 1e-9)
        check("an apply that meets a conflict with no side is refused with how many (%r)"
              % (reply,), reply.get("code") == "Conflicts" and reply.get("conflicts") == 1)
        check("and moved nothing: the rows are there, and the merge is redone (%r, %r)"
              % (two.RedoCount, (who.get("host"), who.get("write"))),
              [t["kind"] for t in two.getTransactionLog()] == kinds and two.RedoCount == redo
              and abs(part.Length.Value - 20) < 1e-9)
        two.redo()
        settle()
        check("redone, the merge is back (%r)" % (part.Height.Value,),
              abs(part.Height.Value - 7) < 1e-9)

        Gui.serveStop()
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
