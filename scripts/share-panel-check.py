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


def connect(port, doc, name, query="", headers="", device=""):
    """A client, as the viewer page would be one: `device` the id its browser
    keeps (docs/TransactionLog.md sec 30.32), none when empty."""

    def talk():
        ws = WS(port, "/scene" + query, headers)
        ws.hello(name, ',"doc":"%s"%s' % (doc, ',"device":"%s"' % device if device else ""))
        ws.next_binary(20.0)
        return ws

    return off(talk)


def turned_away(port, doc, name, query="", device=""):
    """What the door says to a hello it refuses; None when it says nothing."""

    def talk():
        ws = WS(port, "/scene" + query, "")
        ws.hello(name, ',"doc":"%s"%s' % (doc, ',"device":"%s"' % device if device else ""))
        try:
            for _ in range(6):
                text = ws.next_text(3.0)
                if text is None:
                    return None
                if b'"cmd":"error"' in text:
                    return json.loads(text.decode()).get("code")
        except RuntimeError:
            return "closed"
        return None

    return off(talk)


def write(ws, doc, value, rid):
    def talk():
        raw = ws.op(json.dumps({"id": rid, "op": "setProperty", "doc": doc, "obj": "Box",
                                "target": "object", "name": "Length", "value": value},
                               separators=(",", ":")))
        return json.loads(raw.decode("utf-8")) if raw else None

    return off(talk)


def browsers(panel, grant_row, act):
    """Open the browsers dialog of the grant on `grant_row` of the roster
    (its "Users..." button), and let `act(dialog)` use it."""
    seen = {"found": False}

    def drive():
        dlg = QtWidgets.QApplication.activeModalWidget()
        if dlg is None or dlg.objectName() != "shareBrowsers":
            if dlg is not None:
                dlg.reject()
            return
        seen["found"] = True
        try:
            act(dlg, seen)
        finally:
            dlg.accept()

    tree = panel.findChild(QtWidgets.QTreeWidget, "shareRoster")
    buttons = [b for b in tree.findChildren(QtWidgets.QPushButton)
               if b.text() == "Users..." and b.isVisible()]
    seen["buttons"] = len(buttons)
    if buttons:
        QtCore.QTimer.singleShot(300, drive)
        buttons[grant_row].click()
        settle(25)
    return seen


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
        seen["total"] = dlg.findChild(QtWidgets.QSpinBox, "shareRequestsTotal").value()
        for s in dlg.findChildren(QtWidgets.QSpinBox):
            if not s.objectName().startswith("share"):
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
        # What waits is bounded too: all of one document's files together.
        total = panel.findChild(QtWidgets.QSpinBox, "shareRequestsTotal")
        check("the panel has what may wait, 64 MB unless set %r" % (total.value(),),
              total.value() == 64 and total.isEnabled())
        total.setValue(2)
        settle()
        reply = control({"op": "requests.send", "name": "more.FCStd", "data": big})
        check("a file that would take it past is refused %r"
              % ((share.GetInt("RequestsTotalMB", 0), reply.get("code")),),
              share.GetInt("RequestsTotalMB", 0) == 2 and reply.get("ok") is False
              and reply.get("code") == "TooMany")
        for r in Gui.serveRequests():
            Gui.serveDropRequest(r["id"], r["doc"])
        reply = control({"op": "requests.send", "name": "more.FCStd", "data": big})
        check("and taken once the one that waited is dealt with %r" % (reply.get("ok"),),
              reply.get("ok") is True)
        total.setValue(64)
        settle()
        for r in Gui.serveRequests():
            Gui.serveDropRequest(r["id"], r["doc"])
        check("nothing waits", Gui.serveRequests() == [])

        edit = panel.findChild(QtWidgets.QLineEdit, "shareInvite")
        modes = panel.findChild(QtWidgets.QComboBox, "shareInviteMode")
        check("with no sign-in door the Invite row asks for a name %r"
              % (edit.placeholderText(),), edit.placeholderText() == "a name")
        check("and offers no full control",
              not modes.model().item(2).isEnabled() and modes.model().item(0).isEnabled())

        seen = invite(panel, "l*")
        check("a pattern invites nobody %r" % (seen,),
              seen.get("box") is True and len(Gui.serveGrants()) == 1)

        users = panel.findChild(QtWidgets.QSpinBox, "shareInviteUsers")
        check("the Invite row says how many browsers it is for, one unless set %r"
              % ((users.value(), users.isVisible()),), users.value() == 1 and users.isVisible())
        clipboard.setText("nothing")
        seen = invite(panel, "lei")
        grants = Gui.serveGrants()
        mine = [g for g in grants if g["maxUsers"] > 0]
        check("a name is invited: a grant with a token of its own, for one browser %r" % (grants,),
              not seen.get("box") and len(grants) == 2 and len(mine) == 1
              and mine[0]["token"] not in ("", token) and mine[0]["maxUsers"] == 1
              and mine[0]["devices"] == [] and mine[0]["access"] == 0)
        own = mine[0]["token"] if mine else ""
        link = clipboard.text()
        check("and the link on the clipboard carries that token and the name %r" % (link,),
              "token=%s" % own in link and "client=lei" in link and "doc=Shared" in link
              and token not in link)
        note = panel.findChild(QtWidgets.QLabel, "shareInviteNote")
        check("the panel says so %r" % (note.text(),),
              not note.isHidden() and "lei" in note.text())
        invite(panel, "lei")
        check("the same name again adds nothing, and gives the same link",
              len(Gui.serveGrants()) == 2 and clipboard.text() == link)

        # The link, opened in a browser: the id the browser keeps is the
        # other half of who it is.
        lei_browser = "browser-of-lei-0123456789abcdef"
        eve_browser = "browser-of-eve-0123456789abcdef"
        lei = connect(port, doc.Name, "lei", "?token=%s&client=lei" % own, device=lei_browser)
        settle(25)
        who = roster()
        check("the first browser to open it is enrolled, and may edit %r"
              % (dict((k, (v["access"], v["enrolled"], v["invited"])) for k, v in who.items()),),
              who.get("lei", {}).get("access") == "edit" and who.get("lei", {}).get("enrolled")
              and not who.get("lei", {}).get("invited"))
        key = who.get("lei", {}).get("device", "")
        check("the grant holds its browser, by a hash of the id %r"
              % ([g["devices"] for g in Gui.serveGrants() if g["maxUsers"] > 0],),
              len(key) == 40 and lei_browser not in key
              and [g["devices"] for g in Gui.serveGrants() if g["maxUsers"] > 0] == [[key]])
        code = turned_away(port, doc.Name, "lei", "?token=%s&client=lei" % own, eve_browser)
        check("the same link in another browser is refused %r" % (code,), code == "Refused")
        code = turned_away(port, doc.Name, "lei", "?token=%s&client=lei" % own)
        check("and so is a client that says no browser %r" % (code,), code == "Refused")
        eve = connect(port, doc.Name, "eve", "?token=%s&client=eve" % token, device=eve_browser)
        settle(25)
        who = roster()
        check("the plain link still admits to look, and its browser is not counted %r"
              % (dict((k, (v["access"], v["enrolled"])) for k, v in who.items()),),
              who.get("eve", {}).get("access") == "view" and not who.get("eve", {}).get("enrolled"))
        record = dict((d["enrolledAs"], d) for d in Gui.serveDevices())
        check("the record has both browsers, by what each called itself %r" % (sorted(record),),
              sorted(record) == ["eve", "lei"] and record["lei"]["key"] == key)

        # What it writes is recorded under the browser, and its login says
        # which one.
        reply = write(lei, doc.Name, 23, 5)
        settle(25)
        authors = [(t["author"], t["author_kind"]) for t in doc.getTransactionLog()
                   if t["kind"] == "user" and t["author_kind"] != "local"]
        logins = [json.loads(t["script"]) for t in doc.getTransactionLog()
                  if t["kind"] == "login"]
        check("its write is its own, under the name it came by and its browser %r %r"
              % (reply, authors),
              bool(reply) and reply.get("ok") is True
              and authors == [("lei~%s" % key[:6], "enrolled")])
        check("a login says which browser, whoever it is %r" % (logins,),
              sorted(l.get("device") for l in logins) == sorted([key[:12], record["eve"]["key"][:12]]))

        # Stored: the grant with its browser, and the record.
        settle(120)
        stored = share.GetGroup("Grants")
        kept = [stored.GetGroup(n) for n in stored.GetGroups()]
        counted = [g for g in kept if g.GetInt("MaxUsers", 0) > 0]
        devices = share.GetGroup("Devices")
        check("the grant is stored with its browser, and the record with both %r"
              % ([g.GetString("Devices", "") for g in counted],),
              len(counted) == 1 and counted[0].GetString("Devices", "") == key
              and counted[0].GetString("Note", "") == "lei"
              and sorted(devices.GetGroup(n).GetString("EnrolledAs", "")
                         for n in devices.GetGroups()) == ["eve", "lei"])

        clipboard.setText("nothing")
        tree = panel.findChild(QtWidgets.QTreeWidget, "shareRoster")
        names = [tree.topLevelItem(i).text(0) for i in range(tree.topLevelItemCount())]
        counts = [tree.topLevelItem(i).text(3) for i in range(tree.topLevelItemCount())]
        links = [b for b in tree.findChildren(QtWidgets.QPushButton)
                 if b.text() == "Link" and b.isVisible()]
        check("the roster says which browser, the grant how many it has %r"
              % ((names, counts, len(links)),),
              "lei ~%s" % key[:6] in names and "lei" in names
              and "1 of 1 browser(s)" in counts and len(links) == 1)
        if links:
            links[0].click()
            settle()
        check("and its Link copies the link again", clipboard.text() == link)

        # The grant's browsers: one named by the host, then turned off.
        def name_it(dlg, seen):
            rows = dlg.findChild(QtWidgets.QTreeWidget, "shareBrowserList")
            seen["rows"] = [rows.topLevelItem(i).text(0) for i in range(rows.topLevelItemCount())]
            seen["most"] = dlg.findChild(QtWidgets.QSpinBox, "shareGrantUsers").value()

        seen = browsers(panel, 0, name_it)
        check("the grant lists its one browser %r" % (seen,),
              seen["found"] and seen.get("rows") == ["lei ~%s" % key[:6]] and seen.get("most") == 1)

        def turn_off(dlg, seen):
            rows = dlg.findChild(QtWidgets.QTreeWidget, "shareBrowserList")
            rows.setCurrentItem(rows.topLevelItem(0))
            for b in dlg.findChildren(QtWidgets.QPushButton):
                if b.text() == "Turn off / on":
                    b.click()
            seen["state"] = rows.topLevelItem(0).text(5)

        seen = browsers(panel, 0, turn_off)

        def farewell():
            # A page reads what it is told, which is how it hears it is out.
            try:
                for _ in range(8):
                    text = lei.next_text(3.0)
                    if text is None:
                        return None
                    if b'"cmd":"error"' in text:
                        return json.loads(text.decode()).get("code")
            except RuntimeError:
                return "closed"
            return None

        told = off(farewell)
        off(lei.close)
        settle(40)
        who = roster()
        check("turned off, the browser is told and is out, and the others are where they were "
              "%r %r %r" % (seen.get("state"), told, sorted(who)),
              seen.get("state") == "off" and told == "Refused" and "lei" not in who
              and "eve" in who)
        code = turned_away(port, doc.Name, "lei", "?token=%s&client=lei" % own, lei_browser)
        check("and stays out when it comes back %r" % (code,), code == "Refused")
        seen = browsers(panel, 0, turn_off)
        settle(20)
        lei = connect(port, doc.Name, "lei", "?token=%s&client=lei" % own, device=lei_browser)
        settle(25)
        check("turned on again, it is the browser it was",
              roster().get("lei", {}).get("enrolled") is True
              and [g["devices"] for g in Gui.serveGrants() if g["maxUsers"] > 0] == [[key]])

        for ws in (lei, eve):
            off(ws.close)
        settle(20)
        stop(panel)
        check("sharing stopped", Gui.serveGrants() == [] and not panel.isVisible())

        # A new share: what was stored is the door again.
        port = free_port()
        start("LAN", port)
        lei = connect(port, doc.Name, "lei", "?token=%s&client=lei" % own, device=lei_browser)
        settle(25)
        check("shared again, the browser is still the invitation's, and no other is %r"
              % ([g["devices"] for g in Gui.serveGrants() if g["maxUsers"] > 0],),
              roster().get("lei", {}).get("enrolled") is True
              and turned_away(port, doc.Name, "lei", "?token=%s&client=lei" % own, eve_browser)
              == "Refused")
        off(lei.close)
        settle(20)
        panel = share_panel()
        stop(panel)

        # --- a sign-in door with a header of its own
        port = free_port()
        seen = start("thundereal", port, header="X-Check-Identity:", sign_in=True)
        check("a sign-in door shows the identity header, empty %r" % (seen,),
              seen["found"] and seen.get("header_shown_after") is True
              and seen.get("header_was") == "")
        check("the limit the panel set is what the dialog shows %r"
              % ((seen.get("limit"), seen.get("total")),),
              seen.get("limit") == 2 and seen.get("total") == 64)
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
