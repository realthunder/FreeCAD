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
        # invitation issued to his name, and an open invitation to look.
        Gui.serveSetGrants([
            {"identity": "alice@example.com", "access": 0},
            {"token": "t-bob", "client": "bob", "access": 0},
            {"token": "t-open", "client": "*", "access": 1},
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

        for ws in (alice, bob, carol):
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
