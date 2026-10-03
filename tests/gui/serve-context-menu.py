"""A browser's right click gets the desktop's 3D-view context menu.

docs/ThinClient.md sec 8.11b. The `contextMenu` op builds the menu the
desktop's popup would show for what the client's ray hits -- the active
workbench's "View" entries and the object's own submenu -- on the host, in
the client's view, and sends it; `contextMenu.trigger` runs an entry the
way the desktop runs it, judged again by what the connection may do.

What is asserted, on an edit (non-host) connection:

  - before a camera is stated there is no view to build in: NoView;
  - a ray onto a box gives a menu whose first entry is the box's own
    submenu, titled by its label, holding an edit entry ("Transform")
    that is allowed, and an opaque entry that is refused with a reason;
  - Std_ViewFitAll is the browser's own (kind local, "fitAll");
  - a ray onto nothing gives no object submenu and no target;
  - a refused entry is refused on trigger too, and changes nothing;
  - the edit entry enters the edit in the client's view: the document is
    in edit and this client is told, with no 3D view opened for it;
  - a used menu is gone (Stale); so is one replaced by a new right click;
  - a PartDesign Body's menu, whose building asks for an active view,
    opens no 3D view either.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout), or by hand as `FreeCAD <this script>` with GT_OUT set and this
directory on PYTHONPATH.
"""
import json
import math
import os
import threading
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

import wsclient
from wsclient import WS, free_port

clock = time.perf_counter

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ServeContextMenu"
CLIENT_WAIT_S = 120

# Straight down onto the box (0..10 cube) and the body's pad beside it
EYE = (5.0, 5.0, 200.0)
QUAT = (0.0, 0.0, 0.0, 1.0)
HEIGHT_ANGLE = math.radians(45.0)
NEAR, FAR = 10.0, 400.0
VW, VH = 800, 600

state = {"doc": None, "client": None, "done": False, "t0": clock(), "views": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def reply_of(raw):
    return json.loads(raw.decode("utf-8")) if raw else None


def ray_down(x, y):
    return '[%g,%g,%g,0,0,-1]' % (x, y, EYE[2])


def walk(items):
    """Every entry of a menu, submenus flattened"""
    for item in items or []:
        if "items" in item:
            yield from walk(item["items"])
        elif not item.get("separator"):
            yield item


class Client(threading.Thread):
    def __init__(self, port):
        super().__init__(daemon=True)
        self.port = port
        self.error = None
        self.snapshot = False
        self.r = {}
        self.read = threading.Event()
        self.asked = 0

    def run(self):
        try:
            self.talk()
        except Exception:
            self.error = traceback.format_exc()

    def sample(self):
        """Let the GUI thread read the document"""
        self.read.clear()
        self.asked += 1
        self.read.wait(30.0)

    def talk(self):
        ws = WS(self.port)
        ws.hello("serve-context-menu")
        self.snapshot = ws.next_binary(20.0) is not None
        if not self.snapshot:
            return
        ws.next_binary(0.5)
        r = self.r

        r["no_camera"] = reply_of(ws.op(
            '{"id":1,"op":"contextMenu","ray":%s}' % ray_down(5, 5)))

        ws.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        ws.drain(0.3)

        r["box"] = reply_of(ws.op(
            '{"id":2,"op":"contextMenu","ray":%s}' % ray_down(5, 5)))
        r["nothing"] = reply_of(ws.op(
            '{"id":3,"op":"contextMenu","ray":%s}' % ray_down(500, 500)))
        # The second right click replaced the first
        token = (r["box"] or {}).get("menu", 0)
        r["replaced"] = reply_of(ws.op(
            '{"id":4,"op":"contextMenu.trigger","menu":%d,"item":1}' % token))

        r["box2"] = reply_of(ws.op(
            '{"id":5,"op":"contextMenu","ray":%s}' % ray_down(5, 5)))
        menu = r["box2"] or {}
        entries = list(walk(menu.get("items")))
        refused = [e for e in entries if e.get("kind") == "action" and not e.get("allowed")]
        if refused:
            r["refused"] = reply_of(ws.op(
                '{"id":6,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (menu["menu"], refused[0]["id"])))
        transform = [e for e in entries
                     if e.get("kind") == "edit" and e.get("text", "").startswith("Transform")]
        if transform:
            since = len(ws.pushes)
            r["transform"] = reply_of(ws.op(
                '{"id":7,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (menu["menu"], transform[0]["id"])))
            r["edit_push"] = ws.next_push("edit", 5.0, since=since)
            ws.drain(0.5)
            self.sample()
            r["used"] = reply_of(ws.op(
                '{"id":8,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (menu["menu"], transform[0]["id"])))
            ws.op('{"id":9,"op":"resetEdit"}')
            ws.drain(0.5)

        # The body's pad, beside the box
        r["body"] = reply_of(ws.op(
            '{"id":10,"op":"contextMenu","ray":%s}' % ray_down(55, 5)))
        ws.drain(0.3)
        self.sample()
        ws.op('{"id":11,"op":"contextMenu.close","menu":%d}'
              % (r["body"] or {}).get("menu", 0))
        ws.close()


def views_3d():
    return len(FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor"))


def build():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
            "RenderCache", 3)
        doc = FreeCAD.newDocument(DOC, hidden=True)
        state["doc"] = doc
        box = doc.addObject("Part::Box", "Box")
        box.Label = "Crate"
        body = doc.addObject("PartDesign::Body", "Body")
        pad = body.newObject("PartDesign::AdditiveBox", "Block")
        pad.Length = 10
        pad.Width = 10
        pad.Height = 10
        body.Placement.Base = FreeCAD.Vector(50, 0, 0)
        doc.recompute()

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served headless", ok, "port %d" % port):
            finish()
            return
        state["client"] = Client(port)
        state["client"].start()
        QtCore.QTimer.singleShot(50, poll)
    except Exception:
        note("FAIL build:\n" + traceback.format_exc())
        finish()


def poll():
    client = state["client"]
    if client.asked > len(state["views"]):
        gdoc = FreeCADGui.getDocument(DOC)
        state["views"].append((gdoc.getInEdit() is not None, views_3d()))
        client.read.set()
    if client.is_alive():
        if clock() - state["t0"] > CLIENT_WAIT_S:
            check("the client finished", False, "still talking after %ds" % CLIENT_WAIT_S)
            finish()
            return
        QtCore.QTimer.singleShot(20, poll)
        return
    QtCore.QTimer.singleShot(300, verify)


def verify():
    client = state["client"]
    r = client.r
    views = state["views"]
    try:
        check("the client ran without error", client.error is None, client.error or "")
        check("the hello was answered with a snapshot", client.snapshot)
        check("no menu before a camera: NoView",
              (r.get("no_camera") or {}).get("code") == "NoView", r.get("no_camera"))

        box = r.get("box") or {}
        check("a ray onto the box gives a menu", box.get("ok") is True, box)
        target = box.get("target") or {}
        check("the menu is about the box", target.get("obj") == "Box", target)
        items = box.get("items") or []
        first = items[0] if items else {}
        check("its first entry is the box's own submenu, by label",
              first.get("text") == "Crate" and "items" in first, first.get("text"))
        entries = list(walk(items))
        transform = [e for e in entries if e.get("text", "").startswith("Transform")]
        check("Transform is an edit entry, allowed",
              bool(transform) and transform[0].get("kind") == "edit"
              and transform[0].get("allowed") is True, transform[:1])
        default = [e for e in entries if e.get("text") == "Edit Crate"]
        check("the default edit entry, a lambda that says what it does, is allowed",
              bool(default) and default[0].get("kind") == "edit"
              and default[0].get("allowed") is True, default[:1])
        opaque = [e for e in entries if e.get("kind") == "action"]
        check("an opaque entry is refused, with a reason",
              bool(opaque) and all(not e.get("allowed") and e.get("reason") for e in opaque),
              [(e.get("text"), e.get("allowed")) for e in opaque])
        fit = [e for e in entries if e.get("command") == "Std_ViewFitAll"]
        check("Std_ViewFitAll is the browser's own",
              bool(fit) and fit[0].get("kind") == "local" and fit[0].get("local") == "fitAll",
              fit[:1])

        nothing = r.get("nothing") or {}
        check("a ray onto nothing gives a menu with no target",
              nothing.get("ok") is True and nothing.get("target") is None, nothing.get("target"))
        check("and no object submenu",
              not any(i.get("text") == "Crate" for i in nothing.get("items") or []))
        check("a replaced menu is stale",
              (r.get("replaced") or {}).get("code") == "Stale", r.get("replaced"))

        check("a refused entry is refused on trigger",
              (r.get("refused") or {}).get("code") == "Refused", r.get("refused"))
        check("the edit entry runs", (r.get("transform") or {}).get("ok") is True,
              r.get("transform"))
        check("the client is told of its edit", r.get("edit_push") is not None)
        check("the document is in edit, with no 3D view opened",
              len(views) > 0 and views[0] == (True, 0), views)
        check("a used menu is stale", (r.get("used") or {}).get("code") == "Stale", r.get("used"))

        body = r.get("body") or {}
        check("the body's pad has a menu", body.get("ok") is True
              and (body.get("target") or {}).get("obj") == "Body", body.get("target"))
        check("building it opened no 3D view", len(views) > 1 and views[1][1] == 0, views)
    except Exception:
        note("ABORT verify:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, build)
