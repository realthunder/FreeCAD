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
  - a ray through the box lists what it went through under "Pick geometry",
    by element kind: the top and the bottom face, each an allowed pick
    entry naming what it selects; choosing one selects it in this client's
    selection (the client is told), and with extend it joins the first;
  - on a view-only connection the menu is built, but nothing in it runs
    but the browser's own camera entries: a pick is refused, ViewOnly.
  - in a sketch's edit the right click is the sketcher's own menu, not the
    view's: its create tools allowed, its "Leave sketch" the edit's way out
    (and taken, it leaves); with a tool running the click ends the tool and
    there is no menu, the next one being the menu again;
  - and a click onto the sketch's geometry heads that menu with "Pick
    geometry", the line named as the edit names it ("edge2") and marked as
    the edit's to draw: the host previews it (contextMenu.hover) and drops
    the preview, refuses to preview an entry that is not a pick, and
    choosing it selects it in this client's selection, where the sketcher
    takes it as its own (its menu is then the one for a selected line).

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
VIEW_TOKEN = "view-only-ctx"

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

        # "Pick geometry": a pick replaces the selection, an extended one
        # joins it
        r["picks"] = reply_of(ws.op(
            '{"id":11,"op":"contextMenu","ray":%s}' % ray_down(5, 5)))
        menu = r["picks"] or {}
        faces = {e.get("pick", {}).get("sub"): e for e in walk(menu.get("items"))
                 if e.get("kind") == "pick"}
        if "Face6" in faces:
            since = len(ws.pushes)
            r["pick_top"] = reply_of(ws.op(
                '{"id":12,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (menu["menu"], faces["Face6"]["id"])))
            r["sel_top"] = reply_of(ws.next_push("selection", 5.0, since=since))
        menu = reply_of(ws.op(
            '{"id":13,"op":"contextMenu","ray":%s}' % ray_down(5, 5))) or {}
        faces = {e.get("pick", {}).get("sub"): e for e in walk(menu.get("items"))
                 if e.get("kind") == "pick"}
        if "Face5" in faces:
            since = len(ws.pushes)
            r["pick_bottom"] = reply_of(ws.op(
                '{"id":14,"op":"contextMenu.trigger","menu":%d,"item":%d,"extend":true}'
                % (menu["menu"], faces["Face5"]["id"])))
            r["sel_both"] = reply_of(ws.next_push("selection", 5.0, since=since))

        # A sketch in edit: the sketcher's own right click
        since = len(ws.pushes)
        r["sk_edit"] = reply_of(ws.op('{"id":15,"op":"edit","obj":"Sketch","mode":0}'))
        ws.next_push("edit", 5.0, since=since)
        ws.drain(0.5)
        r["sk_menu"] = reply_of(ws.op(
            '{"id":16,"op":"contextMenu","ray":%s}' % ray_down(150, 150)))
        r["sk_tool"] = reply_of(ws.op('{"id":17,"op":"command","name":"Sketcher_CreateLine"}'))
        ws.drain(0.3)
        r["sk_end_tool"] = reply_of(ws.op(
            '{"id":18,"op":"contextMenu","ray":%s}' % ray_down(150, 150)))
        r["sk_menu2"] = reply_of(ws.op(
            '{"id":19,"op":"contextMenu","ray":%s}' % ray_down(150, 150)))
        # "Pick geometry" in the edit: the line under the ray is geometry
        # the edit draws, which the host previews and selects
        r["sk_picks"] = reply_of(ws.op(
            '{"id":30,"op":"contextMenu","ray":%s}' % ray_down(30, 40)))
        menu = r["sk_picks"] or {}
        edges = [e for e in walk(menu.get("items"))
                 if e.get("kind") == "pick" and (e.get("pick") or {}).get("obj") == "Sketch"]
        others = [e for e in walk(menu.get("items")) if e.get("kind") != "pick"]
        if edges:
            r["sk_hover"] = reply_of(ws.op(
                '{"id":31,"op":"contextMenu.hover","menu":%d,"item":%d}'
                % (menu["menu"], edges[0]["id"])))
            ws.drain(0.3)
            self.sample()
            r["sk_unhover"] = reply_of(ws.op(
                '{"id":32,"op":"contextMenu.hover","menu":%d,"item":0}' % menu["menu"]))
            ws.drain(0.3)
            self.sample()
            if others:
                r["sk_hover_other"] = reply_of(ws.op(
                    '{"id":33,"op":"contextMenu.hover","menu":%d,"item":%d}'
                    % (menu["menu"], others[0]["id"])))
            since = len(ws.pushes)
            r["sk_pick"] = reply_of(ws.op(
                '{"id":34,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (menu["menu"], edges[0]["id"])))
            r["sk_sel"] = reply_of(ws.next_push("selection", 5.0, since=since))
            ws.drain(0.3)
            self.sample()
        # With the edge selected the sketcher's menu is the one for it
        r["sk_menu_sel"] = reply_of(ws.op(
            '{"id":36,"op":"contextMenu","ray":%s}' % ray_down(150, 150)))
        # A click on nothing drops it, and the menu has its way out again
        ws.send(2, wsclient.pick_frame((150.0, 150.0, EYE[2]), (0.0, 0.0, -1.0), 0))
        ws.drain(0.5)
        r["sk_menu3"] = reply_of(ws.op(
            '{"id":35,"op":"contextMenu","ray":%s}' % ray_down(150, 150)))
        leave = [e for e in walk((r["sk_menu3"] or {}).get("items"))
                 if e.get("command") == "Sketcher_LeaveSketch"]
        if leave:
            r["sk_leave"] = reply_of(ws.op(
                '{"id":20,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (r["sk_menu3"]["menu"], leave[0]["id"])))
            ws.drain(0.5)
            self.sample()
        ws.close()

        # A view-only connection
        view = WS(self.port, "/scene?token=%s" % VIEW_TOKEN)
        view.hello("serve-context-menu-view")
        view.next_binary(20.0)
        view.next_binary(0.5)
        view.send(2, wsclient.camera_frame(EYE, QUAT, HEIGHT_ANGLE, NEAR, FAR, VW, VH))
        view.drain(0.3)
        r["view"] = reply_of(view.op(
            '{"id":1,"op":"contextMenu","ray":%s}' % ray_down(5, 5)))
        vmenu = r["view"] or {}
        vpicks = [e for e in walk(vmenu.get("items")) if e.get("kind") == "pick"]
        if vpicks:
            r["view_pick"] = reply_of(view.op(
                '{"id":2,"op":"contextMenu.trigger","menu":%d,"item":%d}'
                % (vmenu["menu"], vpicks[0]["id"])))
        view.close()


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
        import Part
        sketch = doc.addObject("Sketcher::SketchObject", "Sketch")
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(100, 100, 0),
                                            FreeCAD.Vector(120, 100, 0)), False)
        # In sight of the camera, clear of the box and the pad
        sketch.addGeometry(Part.LineSegment(FreeCAD.Vector(20, 40, 0),
                                            FreeCAD.Vector(40, 40, 0)), False)
        doc.recompute()

        port = free_port()
        ok = FreeCADGui.serveDocument(doc, port)
        if not check("the document is served headless", ok, "port %d" % port):
            finish()
            return
        # The view-only token first: a grant that names no token matches
        # every connection, and of two equally specific the first wins
        FreeCADGui.serveSetGrants([{"token": VIEW_TOKEN, "access": 1}, {"access": 0}])
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

        picks = r.get("picks") or {}
        top = [i for i in picks.get("items") or [] if "items" in i]
        pick_menu = top[1] if len(top) > 1 else {}
        check("\"Pick geometry\" follows the object's submenu",
              pick_menu.get("text") == "Pick geometry", [i.get("text") for i in top])
        kinds = [i.get("text") for i in pick_menu.get("items") or []]
        check("grouped by element kind", "Face" in kinds, kinds)
        entries = [e for e in walk(pick_menu.get("items")) if e.get("kind") == "pick"]
        names = sorted(e.get("text") for e in entries)
        check("the ray went through the top and the bottom face",
              "Crate (Face6)" in names and "Crate (Face5)" in names, names)
        check("each a pick entry, allowed, naming what it selects",
              entries and all(e.get("allowed") is True and (e.get("pick") or {}).get("obj") == "Box"
                              for e in entries), entries[:2])
        check("choosing one selects it", (r.get("pick_top") or {}).get("ok") is True,
              r.get("pick_top"))
        sel = (r.get("sel_top") or {}).get("items")
        check("in this client's selection, which it is told",
              sel == [{"obj": "Box", "sub": "Face6"}], r.get("sel_top"))
        check("an extended pick runs", (r.get("pick_bottom") or {}).get("ok") is True,
              r.get("pick_bottom"))
        sel = (r.get("sel_both") or {}).get("items") or []
        check("and joins the first",
              sorted(i.get("sub") for i in sel) == ["Face5", "Face6"], r.get("sel_both"))

        view = r.get("view") or {}
        check("a view-only connection gets the menu", view.get("ok") is True, view)
        ventries = list(walk(view.get("items")))
        check("in which only the browser's own camera entries run",
              ventries and all(bool(e.get("allowed")) == (e.get("kind") == "local")
                               for e in ventries),
              [(e.get("text"), e.get("kind"), e.get("allowed")) for e in ventries
               if bool(e.get("allowed")) != (e.get("kind") == "local")])
        check("the pick entries are there, refused as view only",
              any(e.get("kind") == "pick" and e.get("reason") == "View only" for e in ventries))
        sk = r.get("sk_menu") or {}
        check("a sketch is edited from the client", (r.get("sk_edit") or {}).get("ok") is True,
              r.get("sk_edit"))
        sk_entries = list(walk(sk.get("items")))
        sk_commands = [e.get("command") for e in sk_entries]
        check("in its edit the right click is the sketcher's own menu",
              sk.get("ok") is True and "Sketcher_CreatePoint" in sk_commands
              and not any(i.get("text") == "Pick geometry" for i in sk.get("items") or []),
              sk_commands)
        point = [e for e in sk_entries if e.get("command") == "Sketcher_CreatePoint"]
        check("its create tools are allowed", point and point[0].get("allowed") is True, point[:1])
        leave = [e for e in sk_entries if e.get("command") == "Sketcher_LeaveSketch"]
        check("its Leave sketch is the edit's way out, allowed",
              leave and leave[0].get("kind") == "finishEdit" and leave[0].get("allowed") is True,
              leave[:1])
        check("a tool runs", (r.get("sk_tool") or {}).get("ok") is True, r.get("sk_tool"))
        check("with a tool running the right click ends it, and there is no menu",
              (r.get("sk_end_tool") or {}).get("ok") is True
              and not (r.get("sk_end_tool") or {}).get("items"), r.get("sk_end_tool"))
        check("the next right click is the menu again",
              any(e.get("command") == "Sketcher_CreatePoint"
                  for e in walk((r.get("sk_menu2") or {}).get("items"))))
        skp = r.get("sk_picks") or {}
        sk_top = skp.get("items") or [{}]
        check("in the edit a ray onto its geometry heads the menu with \"Pick geometry\"",
              sk_top[0].get("text") == "Pick geometry", [i.get("text") for i in sk_top[:3]])
        sk_edges = [e for e in walk(sk_top[0].get("items")) if e.get("kind") == "pick"]
        check("listing the line as the edit names it, marked as the edit's to draw",
              any(e.get("pick") == {"obj": "Sketch", "sub": "edge2", "edit": True}
                  and e.get("allowed") is True for e in sk_edges), sk_edges)
        check("the host previews it", (r.get("sk_hover") or {}).get("ok") is True,
              r.get("sk_hover"))
        check("and drops the preview", (r.get("sk_unhover") or {}).get("ok") is True,
              r.get("sk_unhover"))
        check("an entry that is not a pick has no preview",
              (r.get("sk_hover_other") or {}).get("code") == "UnknownItem",
              r.get("sk_hover_other"))
        check("choosing it selects it", (r.get("sk_pick") or {}).get("ok") is True,
              r.get("sk_pick"))
        sel = (r.get("sk_sel") or {}).get("items") or []
        check("in this client's selection, which it is told",
              len(sel) == 1 and sel[0].get("obj") == "Sketch"
              and sel[0].get("sub", "").endswith("edge2"), r.get("sk_sel"))
        sel_cmds = [e.get("command") for e in walk((r.get("sk_menu_sel") or {}).get("items"))]
        check("and the sketcher takes it as its own: its menu is the one for a line",
              "Sketcher_ConstrainHorizontal" in sel_cmds, sel_cmds)
        check("Leave sketch leaves the edit", (r.get("sk_leave") or {}).get("ok") is True
              and views and views[-1][0] is False, (r.get("sk_leave"), views))

        check("and a pick is refused on trigger",
              (r.get("view_pick") or {}).get("ok") is not True
              and (r.get("view_pick") or {}).get("code") is not None, r.get("view_pick"))
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
