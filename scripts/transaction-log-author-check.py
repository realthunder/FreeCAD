# GUI check of docs/TransactionLog.md sec 30.3 S.b and 30.6: the author of a
# row. A document is served, three clients connect -- one a front door
# verified, one holding an invitation issued to its name, one with nothing
# but the name it gives -- and the log says who came and who wrote what.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-ac \
#     AUTHORCHECK_OUT=/tmp/ac/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/transaction-log-author-check.py
#
# It writes PASS/FAIL lines to $AUTHORCHECK_OUT and exits. The log setting is
# put back before it exits.
import json, os, sys, threading, time, traceback

# The header a front door would assert an identity in, believed from a
# loopback peer (docs/ShareAccess.md sec 4). Read by the server at its first
# use, so before anything is served.
os.environ["FC_SERVE_IDENTITY_HEADER"] = "X-Check-Identity"
os.environ["FC_SERVE_TRUST_PROXY"] = "1"

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "gui"))
from wsclient import WS, free_port

OUT = os.environ["AUTHORCHECK_OUT"]
lines = []


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def settle(n=10):
    for _ in range(n):
        QtWidgets.QApplication.processEvents()
        time.sleep(0.02)


def off(fn, *args):
    """Run a blocking call of a client off the GUI thread, which keeps
    serving it meanwhile."""
    box = {}

    def work():
        try:
            box["r"] = fn(*args)
        except Exception:
            box["e"] = traceback.format_exc()

    t = threading.Thread(target=work, daemon=True)
    t.start()
    while t.is_alive():
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)
    settle()
    if "e" in box:
        raise RuntimeError(box["e"])
    return box.get("r")


def connect(port, doc, name, query="", headers=""):
    def talk():
        ws = WS(port, "/scene" + query, headers)
        ws.hello(name, ',"doc":"%s"' % doc)
        ws.next_binary(20.0)
        return ws

    return off(talk)


def set_length(ws, doc, value, rid):
    def talk():
        raw = ws.op(json.dumps({"id": rid, "op": "setProperty", "doc": doc, "obj": "Box",
                                "target": "object", "name": "Length", "value": value},
                               separators=(",", ":")))
        return json.loads(raw.decode("utf-8")) if raw else None

    return off(talk)


def step(ws, doc, op, rid):
    """A client's undo or redo: its own steps (sec 30.10)."""

    def talk():
        raw = ws.op(json.dumps({"id": rid, "op": op, "doc": doc}, separators=(",", ":")))
        return json.loads(raw.decode("utf-8")) if raw else None

    return off(talk)


def rows(doc, kind=None):
    return [t for t in doc.getTransactionLog() if kind is None or t["kind"] == kind]


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


def run():
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        p.SetInt("TransactionLog", 1)
        doc = App.newDocument("Author")
        doc.UndoMode = 1
        doc.openTransaction("host box")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        doc.commitTransaction()
        settle()

        port = free_port()
        check("the document is served", Gui.serveDocument(doc, port))
        # The door: alice by the identity a front door asserts, bob by an
        # invitation issued to his name, an open invitation to look, and an
        # open invitation that says edit.
        Gui.serveSetGrants([
            {"identity": "alice@example.com", "access": 0},
            {"token": "t-bob", "client": "bob", "access": 0},
            {"token": "t-open", "client": "*", "access": 1},
            {"token": "t-any", "client": "*", "access": 0},
        ])
        settle()

        alice = connect(port, doc.Name, "laptop", "?client=laptop",
                        "X-Check-Identity: alice@example.com\r\n")
        bob = connect(port, doc.Name, "bob", "?token=t-bob&client=bob")
        carol = connect(port, doc.Name, "carol", "?token=t-open&client=carol")
        settle(25)
        roster = dict((c["client"], c) for c in Gui.serveClients())
        check("three clients are on the roster %r" % (sorted(roster),),
              sorted(roster) == ["bob", "carol", "laptop"])

        # A login is a row, view-only ones too (U2).
        logins = rows(doc, "login")
        who = sorted((t["author"], t["author_kind"]) for t in logins)
        check("a login row each, under who it is %r" % (who,),
              who == [("alice@example.com", "verified"), ("bob", "invited"),
                      ("carol", "declared")])
        scripts = dict((t["author"], json.loads(t["script"])) for t in logins)
        check("the login says the access and whether it was verified %r" % (scripts,),
              scripts.get("carol", {}).get("access") == "view"
              and scripts.get("carol", {}).get("verified") is False
              and scripts.get("alice@example.com", {}).get("verified") is True
              and scripts.get("bob", {}).get("access") == "edit"
              and scripts.get("bob", {}).get("kind") == "invited")
        check("a login is no undo step %r" % (doc.UndoNames,), doc.UndoNames == ["host box"])
        sessions = doc.getTransactionSessions()
        check("a session each besides the desktop's %r"
              % ([(s["name"], s["access"]) for s in sessions],),
              sorted((s["kind"], s["name"], s["access"]) for s in sessions)
              == [("declared", "carol", "view"), ("invited", "bob", "edit"),
                  ("local", "host", ""), ("verified", "alice@example.com", "edit")])

        # What each writes is theirs; the desktop's is the host's (P3).
        reply = set_length(alice, doc.Name, 21.0, 11)
        check("alice edits %r" % (reply,), bool(reply) and reply.get("ok") is True
              and doc.Box.Length.Value == 21.0)
        reply = set_length(bob, doc.Name, 22.0, 12)
        check("bob edits %r" % (reply,), bool(reply) and reply.get("ok") is True
              and doc.Box.Length.Value == 22.0)
        doc.openTransaction("host edit")
        doc.Box.Width = 5
        doc.recompute()
        doc.commitTransaction()
        settle()
        edits = [(t["name"], t["author"], t["author_kind"]) for t in rows(doc)
                 if t["kind"] in ("user", "implicit")]
        check("each row under its author %r" % (edits,),
              edits == [("host box", "host", "local"),
                        ("Edit property", "alice@example.com", "verified"),
                        ("Edit property", "bob", "invited"),
                        ("host edit", "host", "local")])
        recomputes = [t["author"] for t in rows(doc, "recompute")]
        check("the recompute an edit ran is its author's %r" % (recomputes,),
              "alice@example.com" in recomputes and "bob" in recomputes)

        # Undo is each author's own (S.c). Alice's edit of the length has
        # bob's on top of it: refused, saying what changed (P2).
        reply = step(alice, doc.Name, "undo", 21)
        check("alice's undo over bob's write is refused %r" % (reply,),
              bool(reply) and reply.get("ok") is False and reply.get("code") == "Refused"
              and "changed since" in reply.get("message", "")
              and doc.Box.Length.Value == 22.0)
        # Bob's is not the last thing written -- the host's edit is -- and
        # is undone all the same; the stacks told back are his.
        reply = step(bob, doc.Name, "undo", 22)
        check("bob undoes his own past the host's edit %r" % (reply,),
              bool(reply) and reply.get("ok") is True and reply.get("undos") == []
              and reply.get("redos") == ["Edit property"]
              and doc.Box.Length.Value == 21.0 and doc.Box.Width.Value == 5.0
              and abs(doc.Box.Shape.Volume - 21.0 * 5 * 10) < 1e-6)
        reply = step(alice, doc.Name, "undo", 23)
        check("alice undoes hers now %r" % (reply,),
              bool(reply) and reply.get("ok") is True and reply.get("undos") == []
              and reply.get("redos") == ["Edit property"] and doc.Box.Length.Value == 10.0)
        check("the desktop's steps are the desktop's %r" % (doc.UndoNames,),
              doc.UndoNames == ["host edit", "host box"] and doc.RedoNames == [])
        undone = [(t["kind"], t["author"]) for t in rows(doc) if t["kind"] in ("undo", "redo")]
        check("the undo rows are the undoers' %r" % (undone,),
              undone == [("undo", "bob"), ("undo", "alice@example.com")])
        reply = step(alice, doc.Name, "redo", 24)
        check("alice redoes %r" % (reply,), bool(reply) and reply.get("ok") is True
              and doc.Box.Length.Value == 21.0)
        reply = step(bob, doc.Name, "redo", 25)
        check("bob redoes on what she put back %r" % (reply,),
              bool(reply) and reply.get("ok") is True and doc.Box.Length.Value == 22.0
              and abs(doc.Box.Shape.Volume - 22.0 * 5 * 10) < 1e-6)
        reply = step(carol, doc.Name, "undo", 26)
        check("carol may not undo %r" % (reply,),
              bool(reply) and reply.get("ok") is False and reply.get("code") == "ViewOnly")

        # A view-only connection writes nothing.
        count = len(rows(doc))
        reply = set_length(carol, doc.Name, 23.0, 13)
        check("carol is refused %r" % (reply,), bool(reply) and reply.get("ok") is False
              and reply.get("code") == "ViewOnly" and doc.Box.Length.Value == 22.0
              and len(rows(doc)) == count)

        # Leaving closes the session and writes no row; coming back is
        # another session of the same user (U1).
        user = [s["user"] for s in sessions if s["name"] == "alice@example.com"][0]
        off(alice.close)
        settle(40)
        closed = [s for s in doc.getTransactionSessions()
                  if s["name"] == "alice@example.com" and s["closed"] > 0]
        check("alice left: her session is closed, no row for it",
              len(closed) == 1 and len(rows(doc)) == count)
        alice = connect(port, doc.Name, "phone", "?client=phone",
                        "X-Check-Identity: alice@example.com\r\n")
        settle(25)
        hers = [s for s in doc.getTransactionSessions() if s["name"] == "alice@example.com"]
        check("alice is back: a second session of the one user %r"
              % ([(s["id"], s["user"], s["closed"] > 0) for s in hers],),
              len(hers) == 2 and all(s["user"] == user for s in hers)
              and sorted(s["closed"] > 0 for s in hers) == [False, True])
        reply = set_length(alice, doc.Name, 24.0, 14)
        last = [t for t in rows(doc) if t["kind"] == "user"][-1]
        check("what she writes now is the new session's, the same author",
              bool(reply) and reply.get("ok") is True
              and last["author"] == "alice@example.com"
              and last["session"] == [s["id"] for s in hers if s["closed"] == 0][0])

        # The panel: an Author column, the logins hidden until asked for.
        dock, tree = panel()
        check("the log panel is there", tree is not None)
        if tree is not None:
            acol = column(tree, "Author")
            kcol = column(tree, "Kind")
            check("the panel has an Author column", acol >= 0)
            items = [tree.topLevelItem(i) for i in range(tree.topLevelItemCount())]
            shown = [i for i in items if not i.isHidden()]
            authors = set(i.text(acol) for i in shown)
            check("the panel names the authors %r" % (sorted(authors),),
                  set(["host", "alice@example.com", "bob (invited)"]) <= authors)
            check("the logins are hidden",
                  not [i for i in shown if i.text(kcol) == "login"]
                  and len([i for i in items if i.text(kcol) == "login"]) == 4)
            box = [b for b in dock.findChildren(QtWidgets.QCheckBox) if b.text() == "Logins"]
            check("the panel has a Logins box", len(box) == 1)
            if box:
                box[0].setChecked(True)
                settle()
                shown = [i for i in items if not i.isHidden() and i.text(kcol) == "login"]
                check("the logins are shown when asked for %r"
                      % (sorted(i.text(acol) for i in shown),),
                      sorted(i.text(acol) for i in shown)
                      == ["alice@example.com", "alice@example.com", "bob (invited)",
                          "carol (declared)"])

        # The door (S.d; U4, U6). An open invitation makes nobody someone:
        # the grant says edit and its holder may look.
        dave = connect(port, doc.Name, "dave", "?token=t-any&client=dave")
        settle(25)
        roster = dict((c["client"], c) for c in Gui.serveClients())
        check("an open invitation to edit admits to look %r"
              % ((roster.get("dave", {}).get("access"), roster.get("dave", {}).get("invited")),),
              roster.get("dave", {}).get("access") == "view"
              and roster.get("dave", {}).get("invited") is False
              and roster.get("bob", {}).get("invited") is True
              and roster.get("bob", {}).get("access") == "edit")
        count = len(rows(doc, "user"))
        reply = set_length(dave, doc.Name, 30.0, 31)
        check("its holder's write is refused %r" % (reply,),
              bool(reply) and reply.get("ok") is False and reply.get("code") == "ViewOnly"
              and len(rows(doc, "user")) == count)
        check("and the host cannot make it an editor by hand",
              Gui.serveSetClientMode(roster["dave"]["id"], "edit") is False
              and Gui.serveSetClientMode(roster["carol"]["id"], "edit") is False)
        logins = [(t["author"], t["author_kind"]) for t in rows(doc, "login")]
        check("it is logged as the name it gave, declared %r" % (logins[-1:],),
              logins[-1:] == [("dave", "declared")])
        # An invitation is to one name: under another, bob is whoever he
        # says he is, and no longer writes.
        off(lambda: bob.send(1, b'{"cmd":"client","name":"bobby"}'))
        settle(25)
        roster = dict((c["client"], c) for c in Gui.serveClients())
        check("renamed, the invited client may only look %r"
              % ((roster.get("bobby", {}).get("access"), roster.get("bobby", {}).get("invited")),),
              roster.get("bobby", {}).get("access") == "view"
              and roster.get("bobby", {}).get("invited") is False)
        reply = set_length(bob, doc.Name, 31.0, 32)
        check("and his write is refused %r" % (reply,),
              bool(reply) and reply.get("ok") is False and reply.get("code") == "ViewOnly")
        names = dict((s["name"], s) for s in doc.getTransactionSessions() if s["closed"] == 0)
        check("the log has him as another user from there on %r" % (sorted(names),),
              "bobby" in names and names["bobby"]["kind"] == "declared" and "bob" not in names)

        for ws in (alice, bob, carol, dave):
            off(ws.close)
        settle(40)
        still = [s["name"] for s in doc.getTransactionSessions()
                 if s["closed"] == 0 and s["kind"] != "local"]
        check("everyone left: only the desktop's session is open %r" % (still,), not still)
        Gui.serveStop()
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
