# GUI check of docs/TransactionLog.md sec 30.20, 30.23, 30.29: the request.
# A file sent over the control lane as a client of a served document would
# send it -- kept in the log and not read, listed in the log panel with its
# sender, refused to a view-only connection, past the upload limit and past
# what may wait -- then saved aside, brought in by the owner with the merge
# or without, merged, and let go; a request that is a branch deleted.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-rq \
#     REQUESTCHECK_OUT=/tmp/rq/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-request-check.py
#
# It writes PASS/FAIL lines to $REQUESTCHECK_OUT and exits. The settings it
# changes are put back before it exits.
import base64, json, os, shutil, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["REQUESTCHECK_OUT"]
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
    requests = None
    for t in dock.findChildren(QtWidgets.QTreeWidget):
        if t.objectName() == "TransactionRequests":
            requests = t
    return dock, requests


def rows(tree):
    out = []
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        out.append([item.text(c) for c in range(tree.columnCount())])
    return out


def control(payload, client=7, access=None):
    payload = dict(payload)
    payload.setdefault("id", 1)
    if access:
        return json.loads(Gui.FormWidgets.control(json.dumps(payload), client, False, access))
    return json.loads(Gui.FormWidgets.control(json.dumps(payload), client))


def send(path, name, client=7, access=None):
    with open(path, "rb") as handle:
        data = base64.b64encode(handle.read()).decode()
    return control({"op": "requests.send", "name": name, "data": data}, client, access)


def drive(seen, pick):
    dialog = QtWidgets.QApplication.activeModalWidget()
    if dialog is None or dialog.objectName() != "TransactionMergeDialog":
        seen["found"] = False
        if dialog is not None:
            dialog.reject()
        return
    seen["found"] = True
    seen["title"] = dialog.windowTitle()
    tree = dialog.findChild(QtWidgets.QTreeWidget)
    header = tree.headerItem()
    takes = [c for c in range(header.columnCount()) if header.text(c) == "Takes"][0]
    for i in range(tree.topLevelItemCount()):
        item = tree.topLevelItem(i)
        side = tree.itemWidget(item, takes)
        if side is not None and item.text(0).startswith("conflict") and item.text(2) == pick:
            side.setCurrentIndex(side.findText("theirs"))
    for b in dialog.findChildren(QtWidgets.QPushButton):
        if b.objectName() == "merge":
            b.click()
            return
    dialog.reject()


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    share = App.ParamGet("User parameter:BaseApp/Preferences/SceneShare")
    mode = p.GetInt("TransactionLog", 2)
    limit = share.GetInt("UploadLimitMB", 16)
    try:
        p.SetInt("TransactionLog", 2)   # the file carries its history
        where = tempfile.mkdtemp()
        doc = App.newDocument("Ours")
        doc.openTransaction("box")
        box = doc.addObject("Part::Box", "Box")
        doc.commitTransaction()
        doc.recompute()
        path = os.path.join(where, "ours.FCStd")
        copy = os.path.join(where, "mine.FCStd")
        doc.saveAs(path)
        shutil.copyfile(path, copy)
        settle()
        doc.openTransaction("narrow")
        box.Width = 7
        doc.recompute()
        doc.commitTransaction()

        # Someone's copy, edited in their own FreeCAD.
        fork = App.openDocument(copy)
        settle()
        fork.openTransaction("theirs longer, narrower")
        fork.Box.Length = 30
        fork.Box.Width = 5
        fork.recompute()
        fork.commitTransaction()
        fork.save()
        App.closeDocument(fork.Name)
        App.setActiveDocument(doc.Name)
        settle()

        dock, requests = panel()
        check("panel and its request list found", dock is not None and requests is not None)
        if dock is not None and requests is not None:
            check("no request, no list", not requests.isVisible() and Gui.serveRequests() == [])

            # H5: a connection that may only look may not send.
            reply = send(copy, "mine.FCStd", 7, "view")
            check("a view-only connection is refused (%r)" % reply.get("code"),
                  reply.get("ok") is False and Gui.serveRequests() == [])
            # H4: the name is a name, the data is the file, the cap is a setting.
            reply = control({"op": "requests.send", "name": "", "data": ""})
            check("no name is refused", reply.get("ok") is False)
            reply = control({"op": "requests.send", "name": "x.FCStd", "data": "!!!!"})
            check("what is not base64 is refused", reply.get("ok") is False)
            big = os.path.join(where, "big.bin")
            with open(big, "wb") as handle:
                handle.write(os.urandom(1024 * 1024 + 4096))
            share.SetInt("UploadLimitMB", 1)
            reply = send(big, "big.FCStd")
            check("past the limit set, refused as too large (%r)" % reply.get("code"),
                  reply.get("ok") is False and reply.get("code") == "TooLarge"
                  and Gui.serveRequests() == [])
            reply = control({"op": "widgets.upload", "name": "big.ttf",
                             "data": base64.b64encode(open(big, "rb").read()).decode()})
            check("the panel's upload is under the same setting (%r)" % reply.get("code"),
                  reply.get("ok") is False and reply.get("code") == "TooLarge")
            share.SetInt("UploadLimitMB", 2)
            reply = control({"op": "widgets.upload", "name": "big.ttf",
                             "data": base64.b64encode(open(big, "rb").read()).decode()})
            check("raised, the same bytes go", reply.get("ok") is True)
            if reply.get("path"):
                os.remove(reply["path"])
            share.SetInt("UploadLimitMB", limit)

            # Sent by an editor: taken, kept in the log, listed -- and not read.
            log = len(doc.getTransactionLog())
            branches = [b["name"] for b in doc.getTransactionBranches()]
            documents = len(App.listDocuments())
            undo = list(doc.UndoNames)
            reply = send(copy, "../../mine.FCStd", 7, "edit")
            check("an editor's file is taken (%r)" % reply,
                  reply.get("ok") is True and reply.get("name") == "mine.FCStd"
                  and reply.get("size") == os.path.getsize(copy) and "path" not in reply)
            sent = Gui.serveRequests(doc.Name)
            held = doc.getTransactionSentFiles()
            check("it is kept in the log, as it came (%r)"
                  % [(r["name"], r["sender"], r["kind"], r["inLog"]) for r in sent],
                  len(sent) == 1 and sent[0]["inLog"] is True and sent[0]["path"] == ""
                  and sent[0]["size"] == os.path.getsize(copy) and len(held) == 1
                  and held[0]["seq"] == sent[0]["id"] == reply.get("request"))
            settle(200)
            now = doc.getTransactionLog()
            check("and not read: one row that says so, no branch, no document, no undo step (%r)"
                  % [(t["kind"], t["name"], t["author"]) for t in now[log:]],
                  len(now) == log + 1 and now[-1]["kind"] == "request"
                  and now[-1]["name"] == "mine.FCStd" and "guest" in now[-1]["author"]
                  and doc.getTransactionOps(now[-1]["seq"]) == []
                  and [b["name"] for b in doc.getTransactionBranches()] == branches
                  and len(App.listDocuments()) == documents and list(doc.UndoNames) == undo)
            listed = rows(requests)
            check("the panel lists it with who sent it (%r)" % listed,
                  requests.isVisible() and len(listed) == 1 and listed[0][0] == "mine.FCStd"
                  and "guest" in listed[0][1] and "not read" in listed[0][3])

            # A copy of it, to look at somewhere else: the bytes as they came.
            aside = os.path.join(where, "aside.FCStd")
            QtCore.QMetaObject.invokeMethod(
                dock, "saveRequestAs", QtCore.Qt.DirectConnection,
                QtCore.Q_ARG("qulonglong", sent[0]["id"]), QtCore.Q_ARG(str, aside),
            )
            check("saved as a copy, and still waiting",
                  os.path.isfile(aside) and open(aside, "rb").read() == open(copy, "rb").read()
                  and len(Gui.serveRequests()) == 1)

            # J4: what waits is bounded, all together.
            total = share.GetInt("RequestsTotalMB", 64)
            share.SetInt("UploadLimitMB", 2)
            share.SetInt("RequestsTotalMB", 1)
            reply = send(big, "big.FCStd")
            check("past what may wait, refused as too many (%r)" % reply.get("code"),
                  reply.get("ok") is False and reply.get("code") == "TooMany"
                  and len(Gui.serveRequests()) == 1)
            share.SetInt("RequestsTotalMB", total)
            share.SetInt("UploadLimitMB", limit)

            # A second one, dropped unread.
            reply = send(copy, "mine.FCStd", 7, "edit")
            other = [r for r in Gui.serveRequests() if r["id"] == reply.get("request")]
            check("the same name again is a second file",
                  len(other) == 1 and other[0]["id"] != sent[0]["id"]
                  and len(Gui.serveRequests()) == 2 and len(rows(requests)) == 2)
            QtCore.QMetaObject.invokeMethod(
                dock, "dropRequest", QtCore.Qt.DirectConnection,
                QtCore.Q_ARG("qulonglong", other[0]["id"]),
            )
            settle(100)
            drops = [t for t in doc.getTransactionLog() if t["kind"] == "drop"]
            check("dropped unread: let go with a record, the first is there (%r)"
                  % [t["script"] for t in drops],
                  [r["id"] for r in Gui.serveRequests()] == [sent[0]["id"]]
                  and len(doc.getTransactionSentFiles()) == 1 and len(rows(requests)) == 1
                  and len(drops) == 1 and '"reason":"unread"' in drops[0]["script"])

            # Brought in without the merge: a branch that waits, its file held.
            QtCore.QMetaObject.invokeMethod(
                dock, "bringRequestOnly", QtCore.Qt.DirectConnection,
                QtCore.Q_ARG("qulonglong", sent[0]["id"]),
            )
            settle(1500)
            held = doc.getTransactionSentFiles()
            listed = rows(requests)
            check("brought in only: a branch in the list, the file held for it (%r, %r)"
                  % (listed, [(f["name"], f["branch"]) for f in held]),
                  Gui.serveRequests() == [] and len(held) == 1 and held[0]["branch"] == "mine"
                  and len(listed) == 1 and listed[0][0] == "mine"
                  and abs(doc.getObject("Box").Length.Value - 10) < 1e-9)
            imports = [t for t in doc.getTransactionLog() if t["kind"] == "import"]
            check("H7: the import's record names the file and who sent it (%r)"
                  % [t["script"] for t in imports],
                  len(imports) == 1 and '"sender":"guest (declared)"' in imports[0]["script"]
                  and '"request":"%s"' % held[0]["hash"] in imports[0]["script"])

            # The merge is the owner's, from the list.
            seen = {}
            QtCore.QTimer.singleShot(300, lambda: drive(seen, "Width"))
            QtCore.QMetaObject.invokeMethod(
                dock, "mergeBranch", QtCore.Qt.DirectConnection, QtCore.Q_ARG(str, "mine"),
            )
            settle()
            check("the merge dialog (%r)" % seen.get("title"), seen.get("found") is True)
            box = doc.getObject("Box")
            check("merged: its length, the width picked, volume %g" % box.Shape.Volume,
                  abs(box.Length.Value - 30) < 1e-9 and abs(box.Width.Value - 5) < 1e-9
                  and abs(box.Shape.Volume - 30 * 5 * 10) < 1e-6)
            check("merged, the file is let go (%r)" % doc.getTransactionSentFiles(),
                  doc.getTransactionSentFiles() == [] and Gui.serveRequests() == [])
            came = [t for t in doc.getTransactionLog() if t["name"].startswith("theirs")]
            check("the rows keep the authors the file gives them",
                  len(came) == 1 and came[0]["author_kind"] == "fork")
            settle(1200)
            check("merged, it is no request: the list is empty and hidden (%r)" % rows(requests),
                  rows(requests) == [] and not requests.isVisible()
                  and doc.getTransactionRequests() == [])

            # Brought in and merged in one go, as before; then every one dropped.
            fork = App.openDocument(copy)
            settle()
            fork.openTransaction("theirs deeper")
            fork.Box.Height = 25
            fork.recompute()
            fork.commitTransaction()
            fork.save()
            App.closeDocument(fork.Name)
            App.setActiveDocument(doc.Name)
            settle()
            reply = send(copy, "mine.FCStd", 7, "edit")
            seen = {}
            QtCore.QTimer.singleShot(300, lambda: drive(seen, "Width"))
            QtCore.QMetaObject.invokeMethod(
                dock, "bringRequest", QtCore.Qt.DirectConnection,
                QtCore.Q_ARG("qulonglong", reply.get("request")),
            )
            settle()
            check("brought in and merged in one go: the dialog, the height, nothing held (%r)"
                  % seen.get("title"),
                  seen.get("found") is True and abs(doc.getObject("Box").Height.Value - 25) < 1e-9
                  and doc.getTransactionSentFiles() == [] and Gui.serveRequests() == [])
            send(copy, "one.FCStd", 7, "edit")
            send(copy, "two.FCStd", 7, "edit")
            settle(100)
            check("two more wait", len(Gui.serveRequests()) == 2 and len(rows(requests)) == 2)
            QtCore.QMetaObject.invokeMethod(dock, "dropAllRequests", QtCore.Qt.DirectConnection)
            settle(300)
            check("all dropped at once",
                  Gui.serveRequests() == [] and doc.getTransactionSentFiles() == []
                  and rows(requests) == [] and not requests.isVisible())

            # A request that is a branch: brought and not merged, then deleted.
            fork = App.openDocument(copy)
            settle()
            fork.openTransaction("theirs taller")
            fork.Box.Height = 40
            fork.recompute()
            fork.commitTransaction()
            fork.save()
            App.closeDocument(fork.Name)
            App.setActiveDocument(doc.Name)
            settle()
            res = doc.importTransactionFork(copy)
            settle(1500)
            listed = rows(requests)
            check("a branch not merged is listed, with what it has (%r)" % listed,
                  requests.isVisible() and len(listed) == 1 and listed[0][0] == res["branch"]
                  and "1 operation" in listed[0][3] and listed[0][4] == "0")
            QtCore.QMetaObject.invokeMethod(
                dock, "deleteRequest", QtCore.Qt.DirectConnection,
                QtCore.Q_ARG(str, res["branch"]),
            )
            settle(300)
            check("deleted: the branch is gone and so is the request",
                  res["branch"] not in [b["name"] for b in doc.getTransactionBranches()]
                  and rows(requests) == [] and not requests.isVisible()
                  and abs(doc.getObject("Box").Height.Value - 25) < 1e-9)
        shutil.rmtree(where, ignore_errors=True)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        share.SetInt("UploadLimitMB", limit)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
