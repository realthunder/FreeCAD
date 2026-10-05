# GUI check of the Share dialogs' settings (docs/ShareAccess.md sec 2.3, 2.4
# and 4; docs/TransactionLog.md sec 30.26): the most a client may send, as a
# control in the dialog that starts a share and in the sharing panel; an
# invitation by name where no door signs anyone in, with the link that
# carries it; and the header a sign-in door names its user in.
# One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-sp \
#     SHARECHECK_OUT=/tmp/sp/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/share-panel-check.py
#
# It writes PASS/FAIL lines to $SHARECHECK_OUT and exits.
import base64, json, os, sys, threading, time, traceback

import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "gui"))
from wsclient import WS, free_port

OUT = os.environ["SHARECHECK_OUT"]
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


def roster():
    return dict((c["client"], c) for c in Gui.serveClients())


def control(payload, client=7):
    payload = dict(payload)
    payload.setdefault("id", 1)
    return json.loads(Gui.FormWidgets.control(json.dumps(payload), client))


def start(door, port, limit=None, header=None, sign_in=None):
    """Std_ShareDocument with the dialog that starts a share filled in and
    accepted. What the dialog showed comes back."""
    seen = {"found": False}

    def drive():
        dlg = QtWidgets.QApplication.activeModalWidget()
        if dlg is None or dlg.objectName() != "shareStartDialog":
            if dlg is not None:
                dlg.reject()
            return
        seen["found"] = True
        doors = dlg.findChild(QtWidgets.QComboBox)
        seen["doors"] = [doors.itemText(i) for i in range(doors.count())]
        doors.setCurrentIndex(doors.findText(door))
        spin = dlg.findChild(QtWidgets.QSpinBox, "shareUploadLimit")
        seen["limit"] = spin.value()
        seen["limit_most"] = spin.maximum()
        seen["limit_enabled"] = spin.isEnabled()
        if limit is not None:
            spin.setValue(limit)
        for s in dlg.findChildren(QtWidgets.QSpinBox):
            if s is not spin:
                s.setValue(port)
        edit = dlg.findChild(QtWidgets.QLineEdit, "shareIdentityHeader")
        seen["header_shown"] = not edit.isHidden()
        for box in dlg.findChildren(QtWidgets.QCheckBox):
            if "sign in" in box.text() and sign_in is not None:
                box.setChecked(sign_in)
        seen["header_shown_after"] = not edit.isHidden()
        seen["header_was"] = edit.text()
        if header is not None:
            edit.setText(header)
        buttons = dlg.findChild(QtWidgets.QDialogButtonBox)
        buttons.button(QtWidgets.QDialogButtonBox.Ok).click()

    QtCore.QTimer.singleShot(300, drive)
    Gui.runCommand("Std_ShareDocument")
    settle(20)
    return seen


def share_panel():
    Gui.runCommand("Std_ShareDocument")
    settle(20)
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.objectName() == "sharePanel" and w.isVisible():
            return w
    return None


def stop(panel):
    for b in panel.findChildren(QtWidgets.QPushButton):
        if b.text() == "Stop sharing":
            b.click()
    settle(30)


def dismiss(seen):
    box = QtWidgets.QApplication.activeModalWidget()
    seen["box"] = box is not None and isinstance(box, QtWidgets.QMessageBox)
    if box is not None:
        seen["text"] = box.text() if seen["box"] else ""
        box.reject()


def invite(panel, who, mode=0):
    edit = panel.findChild(QtWidgets.QLineEdit, "shareInvite")
    panel.findChild(QtWidgets.QComboBox, "shareInviteMode").setCurrentIndex(mode)
    edit.setText(who)
    seen = {}
    QtCore.QTimer.singleShot(200, lambda: dismiss(seen))
    panel.findChild(QtWidgets.QPushButton, "shareInviteButton").click()
    settle(25)
    return seen


def stored_doors(share):
    out = {}
    doors = share.GetGroup("Doors")
    for name in doors.GetGroups():
        d = doors.GetGroup(name)
        out[d.GetString("Name", "")] = d.GetString("IdentityHeader", "")
    return out


def run():
    share = App.ParamGet("User parameter:BaseApp/Preferences/SceneShare")
    clipboard = QtWidgets.QApplication.clipboard()
    try:
        doc = App.newDocument("Shared")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle()

        # --- a share with no sign-in door: the limit, and a name invited
        port = free_port()
        seen = start("LAN", port, limit=24)
        check("the dialog that starts a share came up %r" % (seen.get("doors"),),
              seen["found"] and "LAN" in seen.get("doors", []))
        check("it has the upload limit, 16 MB unless set, 46 at most %r"
              % ((seen.get("limit"), seen.get("limit_most"), seen.get("limit_enabled")),),
              seen.get("limit") == 16 and seen.get("limit_most") == 46
              and seen.get("limit_enabled") is True)
        check("a LAN door shows no identity header", seen.get("header_shown") is False)
        check("accepted, the limit is the preference %r" % (share.GetInt("UploadLimitMB", 0),),
              share.GetInt("UploadLimitMB", 0) == 24)
        token = share.GetString("Token", "")
        grants = Gui.serveGrants()
        check("the share is up behind its token %r" % (grants,),
              bool(token) and [g["token"] for g in grants] == [token])

        panel = share_panel()
        check("the sharing panel opens", panel is not None)
        spin = panel.findChild(QtWidgets.QSpinBox, "shareUploadLimit")
        check("it shows the limit that holds %r" % (spin.value(),), spin.value() == 24)
        spin.setValue(1)
        settle()
        check("changed there, the preference follows at once %r"
              % (share.GetInt("UploadLimitMB", 0),), share.GetInt("UploadLimitMB", 0) == 1)
        big = base64.b64encode(b"x" * (1024 * 1024 + 4096)).decode()
        reply = control({"op": "requests.send", "name": "big.FCStd", "data": big})
        check("and the next file past it is refused %r" % (reply.get("code"),),
              reply.get("ok") is False and reply.get("code") == "TooLarge")
        spin.setValue(2)
        settle()
        reply = control({"op": "requests.send", "name": "big.FCStd", "data": big})
        check("raised, the same file is taken %r" % (reply.get("ok"),), reply.get("ok") is True)
        for r in Gui.serveRequests():
            Gui.serveDropRequest(r["id"])

        edit = panel.findChild(QtWidgets.QLineEdit, "shareInvite")
        modes = panel.findChild(QtWidgets.QComboBox, "shareInviteMode")
        check("with no sign-in door the Invite row asks for a name %r"
              % (edit.placeholderText(),), edit.placeholderText() == "a name")
        check("and offers no full control",
              not modes.model().item(2).isEnabled() and modes.model().item(0).isEnabled())

        seen = invite(panel, "l*")
        check("a pattern invites nobody %r" % (seen,),
              seen.get("box") is True and len(Gui.serveGrants()) == 1)

        clipboard.setText("nothing")
        seen = invite(panel, "lei")
        grants = Gui.serveGrants()
        named = [g for g in grants if g["client"] == "lei"]
        check("a name is invited: a grant with the share's token and the name %r" % (grants,),
              not seen.get("box") and len(grants) == 2 and len(named) == 1
              and named[0]["token"] == token and named[0]["access"] == 0)
        link = clipboard.text()
        check("and the link on the clipboard carries both %r" % (link,),
              "token=%s" % token in link and "client=lei" in link and "doc=Shared" in link)
        note = panel.findChild(QtWidgets.QLabel, "shareInviteNote")
        check("the panel says so %r" % (note.text(),),
              not note.isHidden() and "lei" in note.text())
        invite(panel, "lei")
        check("the same name again adds nothing", len(Gui.serveGrants()) == 2)

        lei = connect(port, doc.Name, "lei", "?token=%s&client=lei" % token)
        eve = connect(port, doc.Name, "eve", "?token=%s&client=eve" % token)
        settle(25)
        who = roster()
        check("the invited name may edit and is on the roster as invited %r"
              % (dict((k, (v["access"], v["invited"])) for k, v in who.items()),),
              who.get("lei", {}).get("access") == "edit" and who.get("lei", {}).get("invited")
              and who.get("eve", {}).get("access") == "view"
              and not who.get("eve", {}).get("invited"))

        clipboard.setText("nothing")
        tree = panel.findChild(QtWidgets.QTreeWidget, "shareRoster")
        names = [tree.topLevelItem(i).text(0) for i in range(tree.topLevelItemCount())]
        links = [b for b in tree.findChildren(QtWidgets.QPushButton)
                 if b.text() == "Link" and b.isVisible()]
        check("the invitation's row has its link %r" % ((names, len(links)),),
              names.count("lei") == 2 and len(links) == 1)
        if links:
            links[0].click()
            settle()
        check("which copies it again", clipboard.text() == link)

        for ws in (lei, eve):
            off(ws.close)
        settle(20)
        stop(panel)
        check("sharing stopped", Gui.serveGrants() == [] and not panel.isVisible())

        # --- a sign-in door with a header of its own
        port = free_port()
        seen = start("thundereal", port, header="X-Check-Identity:", sign_in=True)
        check("a sign-in door shows the identity header, empty %r" % (seen,),
              seen["found"] and seen.get("header_shown_after") is True
              and seen.get("header_was") == "")
        check("the limit the panel set is what the dialog shows %r" % (seen.get("limit"),),
              seen.get("limit") == 2)
        check("the header is kept with the door, without the colon %r" % (stored_doors(share),),
              stored_doors(share).get("thundereal") == "X-Check-Identity")
        alice = connect(port, doc.Name, "laptop", "?token=%s&client=laptop" % token,
                        "X-Check-Identity: alice@example.com\r\n")
        bob = connect(port, doc.Name, "tablet", "?token=%s&client=tablet" % token,
                      "X-Forwarded-Email: bob@example.com\r\n")
        settle(25)
        who = roster()
        check("the door's header says who signed in, and no other does %r"
              % (dict((k, v["identity"]) for k, v in who.items()),),
              who.get("laptop", {}).get("identity") == "alice@example.com"
              and who.get("tablet", {}).get("identity") == "")
        panel = share_panel()
        edit = panel.findChild(QtWidgets.QLineEdit, "shareInvite")
        modes = panel.findChild(QtWidgets.QComboBox, "shareInviteMode")
        check("behind a sign-in door the Invite row asks for an identity %r"
              % (edit.placeholderText(),),
              edit.placeholderText() == "name@example.com" and modes.model().item(2).isEnabled())
        before = len(Gui.serveGrants())
        invite(panel, "carol@example.com")
        added = [g for g in Gui.serveGrants() if g["identity"] == "carol@example.com"]
        check("and invites it with no token %r" % (added,),
              len(Gui.serveGrants()) == before + 1 and len(added) == 1
              and added[0]["token"] == "")
        for ws in (alice, bob):
            off(ws.close)
        settle(20)
        stop(panel)

        # --- the same door with the sign-in off. The proxy is still trusted
        # for the address, and a client that writes an identity header
        # itself must not become someone a door verified.
        port = free_port()
        seen = start("thundereal", port, sign_in=False)
        check("the door remembers its header %r" % (seen.get("header_was"),),
              seen["found"] and seen.get("header_was") == "X-Check-Identity"
              and seen.get("header_shown_after") is False)
        alice = connect(port, doc.Name, "laptop", "?token=%s&client=laptop" % token,
                        "X-Check-Identity: alice@example.com\r\n")
        bob = connect(port, doc.Name, "tablet", "?token=%s&client=tablet" % token,
                      "X-Forwarded-Email: bob@example.com\r\n")
        settle(25)
        who = roster()
        check("with no sign-in at the door no header is taken for an identity %r"
              % (dict((k, (v["identity"], v["access"])) for k, v in who.items()),),
              who.get("laptop", {}).get("identity") == ""
              and who.get("tablet", {}).get("identity") == ""
              and who.get("tablet", {}).get("access") == "view")
        for ws in (alice, bob):
            off(ws.close)
        settle(20)
        panel = share_panel()
        if panel:
            stop(panel)
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(1500, run)
