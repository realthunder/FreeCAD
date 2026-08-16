"""Stress the RELEASE path of the shared instanced tessellation
(_InstGeomTable in ViewProviderExt.cpp): what happens when the last
sharer of a leaf goes away while the ladder still has work armed
against the shared nodes.

demo-instanced.py builds the scene that reaches the instanced path at
all -- a compound of leaves sharing one TShape -- and shows that the
shared build registers worker vertex-cache content. This script is
about the other end of that entry's life. The entry is global and
refcounted; erasing it destroys the shared nodes, and three things are
keyed on those raw node pointers: the worker vertex-cache registry
(SoFCVertexCache::setPrebuilt), the mesh-level registration, and the
deferred GUI work the level pump queues under the same tag. Anything
left behind is held on a dead pointer until some future node lands at
the same address.

Cases the schedule covers, in order:

  1. sharers deleted one at a time DURING a publish storm, so the
     refcount walks 3 -> 2 -> 1 -> 0 while renders, climbs and pump
     items are in flight (not from a quiet scene);
  2. the level cycle live at the moment of release -- a simulated
     memory floor (LevelCeilingSimulateMB) and a tiny GPU budget
     (GpuMemoryBudgetMB) put the entries through exact climbs and
     downgrades, so the last sharer goes away with those callbacks
     registered against the nodes about to be destroyed;
  3. ADDRESS REUSE -- fresh objects are built after the erase, so new
     nodes land where the destroyed ones were. Unconsumed content held
     on a stale pointer shows up here as an adoption that should not
     have happened; run with VCACHE=2 (verify arm) and any content
     that does not match what the traversal capture computes is
     reported as a mismatch.
  4. document close with instanced objects still live, then a rebuild
     in a new document, so the release runs through the document
     teardown path as well as through removeObject.

Read the log for:

  capture budget: ... N adopted of M offered, ...
      must keep reporting normally after each release; a crash, a
      hang, or a sudden "stale nodeid"/mismatch is the failure.
  RELEASE STEP n ...
      this script's own trace, also written to SMOKE_RESULT.

Getting the ladder to act at all took three corrections, all of them
recorded in the code below where they bite: instancing makes a scene
too cheap to pressure (the bytes have to come from KINDS distinct
shared leaves, not from more instances of one), LevelCeilingSimulateMB
is a floor on AVAILABLE memory rather than a cap on the scene, and
arming that ceiling before the sources have climbed leaves nothing at
the exact rung to demote.

Measured 2026-08-16 (native desktop, RTX 3060, 150 kinds x 3 sharers):

  adopt arm  -- 24 steps, 23 publishes, no crash, no stale entry, no
                adoption refused for any reason but "no entry".
                Instanced sources reached the exact rung with both
                hooks live: "instanced-leaf-exact:60(dn 60/dm 60)",
                and a downgrade pass took 60 of them "under pressure"
                while the drops were running.
  verify arm  -- VCACHE=2 over the same schedule: 1500 verified, 0
                mismatches, including the rebuilds onto freed
                addresses.
  demote      -- armed but never chosen: the plan's own pass reports
                "no fallback rung ... offscreen 0", so no source was
                ever both exact and unmissable in a scene where every
                leaf is drawn by some visible sharer.

Env knobs: COUNT (leaves per object), KINDS (distinct shared leaves;
what the bytes scale with), SHARERS (objects sharing every leaf),
BUDGET (capture budget ms), VCACHE (WorkerVertexCache: 1 adopts, 2
verifies), DEVIATION (view deviation percent), CEILING_MB / GPU_MB (the
simulated memory floor and GPU budget), STEP_MS (storm period), QUIT
(1 = exit when the schedule finishes).
"""
import os
import traceback

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")


def note(msg):
    FreeCAD.Console.PrintMessage("RELEASE %s\n" % msg)
    if _out:
        with open(_out, "a") as f:
            f.write(str(msg) + "\n")


try:
    import Part
    from PySide6 import QtCore

    COUNT = int(os.environ.get("COUNT", "450"))
    SHARERS = int(os.environ.get("SHARERS", "3"))
    BUDGET = int(os.environ.get("BUDGET", "50"))
    VCACHE = int(os.environ.get("VCACHE", "1"))
    STEP_MS = int(os.environ.get("STEP_MS", "700"))
    QUIT = int(os.environ.get("QUIT", "1"))
    SPACING = float(os.environ.get("SPACING", "30"))
    DEVIATION = float(os.environ.get("DEVIATION", "0.02"))
    KINDS = int(os.environ.get("KINDS", "150"))
    # LevelCeilingSimulateMB is a FLOOR on available system memory, not
    # a cap on what the scene may hold: a build is refused when the
    # system reports less available than this. 1 refuses nothing, so the
    # demote half never runs -- it has to sit above real RAM.
    CEILING_MB = int(os.environ.get("CEILING_MB", "1000000"))
    GPU_MB = int(os.environ.get("GPU_MB", "1"))

    doc_param = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document")
    doc_param.SetBool("AutoSaveEnabled", False)

    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)
    view.SetBool("ShowNaviCube", False)
    view.SetBool("ShowFPS", False)

    render = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
    render.SetString("Type", "bgfx - OpenGL")
    render.SetInt("CaptureBudgetMS", BUDGET)
    render.SetInt("WorkerVertexCache", VCACHE)
    render.SetBool("ShapeVertices", True)
    render.SetBool("LevelDebug", True)
    render.SetBool("AO", False)
    render.SetBool("Volumetric", False)
    render.SetBool("WaterSurface", False)
    render.SetBool("Bloom", False)
    render.SetBool("GroundReflection", False)
    render.SetBool("PBR", False)

    _state = {"step": 0, "timers": [], "doc": None, "names": [], "gen": 0,
              "frame": 0}

    def build(gen):
        """One document, SHARERS objects whose shape is the SAME
        compound: every leaf TShape in it is shared by every sharer.
        Identical shapes on purpose:
        the instance key is (tshape, orientation, deflection, angular
        deflection), and deflection follows the shape's bounding box, so
        objects that differ in extent would land in different entries and
        never share one. They are separated by Placement, which the
        instance table does not key on."""
        name = "Inst%d" % gen
        doc = FreeCAD.newDocument(name)
        # KINDS distinct leaves, each repeated COUNT/KINDS times: one
        # _InstGeomTable entry per kind, every entry shared by every
        # sharer. Instancing is what makes a scene cheap -- 600 copies
        # of ONE leaf report "live 0.0MB ... (no pressure)" and the
        # ladder hooks then stay armed forever without firing -- so the
        # bytes have to come from the number of distinct entries, not
        # from the number of instances. A finer view deviation does not
        # buy them: it left the live figure at 0.4MB unchanged.
        bases = [Part.makeTorus(8 + 0.05 * k, 3) for k in range(KINDS)]
        side = int(COUNT ** 0.5) + 1
        leaves = [bases[i % KINDS].translated(
                      FreeCAD.Vector((i % side) * SPACING,
                                     (i // side) * SPACING, 0))
                  for i in range(COUNT)]
        comp = Part.Compound(leaves)
        names = []
        for k in range(SHARERS):
            obj = doc.addObject("Part::Feature", "Sharer%d" % k)
            obj.Shape = comp
            obj.Placement.Base = FreeCAD.Vector(0, 0, k * 40.0)
            # Same deviation on every sharer: the instance key carries
            # the deflection, so sharers that disagree never share.
            obj.ViewObject.Deviation = DEVIATION
            names.append(obj.Name)
        doc.recompute()
        _state["doc"] = doc
        _state["names"] = names
        note("BUILT %s: %d sharers x compound of %d leaves over %d shared "
             "TShapes" % (name, SHARERS, COUNT, KINDS))
        return doc

    def paint():
        """A publish only happens inside a render, and this scene is
        otherwise static -- so force one. saveImage renders synchronously
        into the current view."""
        try:
            v = FreeCADGui.activeDocument().activeView()
            _state["frame"] += 1
            v.saveImage(os.path.join(os.environ.get("TMPDIR", "/tmp"),
                                     "demo-inst-release-frame.png"),
                        320, 240, "Current")
        except Exception:
            note("PAINT FAILED\n" + traceback.format_exc())

    def touch():
        """Repaint every live sharer so it rebuilds and republishes: a
        publish reports only what CHANGED."""
        doc = _state["doc"]
        if not doc:
            return
        for name in list(_state["names"]):
            obj = doc.getObject(name)
            if not obj:
                continue
            r, g, b = obj.ViewObject.ShapeColor[:3]
            obj.ViewObject.ShapeColor = (g, b, r)
        FreeCADGui.updateGui()

    def drop(index):
        """Delete one sharer WITHOUT letting the scene settle first: the
        touch above is still unpublished and the ladder still has work
        armed, which is the interesting moment for a release."""
        doc = _state["doc"]
        if not doc or index >= len(_state["names"]):
            return
        name = _state["names"].pop(index)
        doc.removeObject(name)
        note("dropped %s, %d sharers left" % (name, len(_state["names"])))

    def undraw(keep=0):
        """Stop drawing all but \a keep sharers. The DEMOTE half of the
        plan only takes exact meshes "the camera would not miss", so a
        scene fully on screen observes the memory ceiling and still
        demotes nothing: measured, a zoom that pushed the grid out of
        the frustum still reported "offscreen 0 | no fallback rung 300".
        Hiding is the reliable way to make a drawn source undrawn while
        its entry stays alive -- but it must hide EVERY sharer: the
        sources are shared, so one visible sharer keeps all of them
        drawn, which is why hiding all but one changed nothing."""
        doc = _state["doc"]
        if not doc:
            return
        for name in _state["names"][keep:]:
            obj = doc.getObject(name)
            if obj:
                obj.ViewObject.Visibility = False

    def close_doc():
        doc = _state["doc"]
        if not doc:
            return
        name = doc.Name
        _state["doc"] = None
        _state["names"] = []
        FreeCAD.closeDocument(name)
        note("closed document %s with instanced objects live" % name)

    # (label, action) -- every step touches the live sharers and forces a
    # render around the action, so each one lands in a publish storm.
    def schedule():
        return [
            # Order matters and cost a run to learn: the simulated
            # ceiling REFUSES every exact build, so arming it first
            # leaves every source at its coarse rung and the demote pass
            # reports "no fallback rung 300" forever -- there is no exact
            # mesh to drop. The climb has to happen first, unpressured;
            # only then does arming the ceiling produce both halves, a
            # refused refine (the observation) and exact meshes to demote.
            ("warm", lambda: None),
            ("warm: let the sources climb to exact", lambda: None),
            ("warm", lambda: None),
            ("warm", lambda: None),
            ("hide every sharer: undrawn exact meshes are the demote "
             "candidates", undraw),
            ("settle after the hide", lambda: None),
            ("arm demote: LevelCeilingSimulateMB=%d" % CEILING_MB,
             lambda: render.SetInt("LevelCeilingSimulateMB", CEILING_MB)),
            ("arm downgrade: GpuMemoryBudgetMB=%d" % GPU_MB,
             lambda: render.SetInt("GpuMemoryBudgetMB", GPU_MB)),
            ("cycle", lambda: None),
            ("cycle", lambda: None),
            ("drop sharer (refcount N-1)", lambda: drop(0)),
            ("storm after first drop", lambda: None),
            ("drop sharer (refcount N-2)", lambda: drop(0)),
            ("storm", lambda: None),
            ("drop LAST sharer -- entry erased under armed hooks",
             lambda: drop(0)),
            ("storm on the empty scene", lambda: None),
            ("rebuild: fresh nodes may reuse the freed addresses",
             lambda: build(_state["gen"])),
            ("storm on the rebuilt scene", lambda: None),
            ("storm", lambda: None),
            ("close document with instanced objects live", close_doc),
            ("rebuild after close", lambda: build(_state["gen"])),
            ("storm on the third scene", lambda: None),
            ("release the forced budgets",
             lambda: (render.SetInt("LevelCeilingSimulateMB", 0),
                      render.SetInt("GpuMemoryBudgetMB", 0))),
            ("final storm", lambda: None),
        ]

    _steps = schedule()

    def step():
        n = _state["step"]
        if n >= len(_steps):
            note("DONE %d steps, %d frames -- process still alive"
                 % (len(_steps), _state["frame"]))
            for t in _state["timers"]:
                t.stop()
            if QUIT:
                # Close the documents first: a dirty document makes the
                # window close raise a modal save prompt, and the run
                # then hangs with no trailer instead of exiting.
                def quit_now():
                    try:
                        for name in list(FreeCAD.listDocuments()):
                            FreeCAD.closeDocument(name)
                    except Exception:
                        note("CLOSE ON QUIT FAILED\n" + traceback.format_exc())
                    FreeCADGui.getMainWindow().close()
                QtCore.QTimer.singleShot(1500, quit_now)
            return
        _state["step"] = n + 1
        label, action = _steps[n]
        try:
            touch()
            if action is build or label.startswith("rebuild"):
                _state["gen"] += 1
            action()
            paint()
            note("STEP %d %s -- ok" % (n + 1, label))
        except Exception:
            note("STEP %d %s -- EXCEPTION\n%s"
                 % (n + 1, label, traceback.format_exc()))

    def setup():
        try:
            build(_state["gen"])
            FreeCADGui.activeDocument().activeView()
            FreeCADGui.SendMsgToActiveView("ViewFit")
            note("SETUP OK")
            timer = QtCore.QTimer()
            timer.timeout.connect(step)
            timer.start(STEP_MS)
            _state["timers"].append(timer)   # unheld timers are collected
        except Exception:
            note("SETUP FAILED\n" + traceback.format_exc())

    QtCore.QTimer.singleShot(2500, setup)
except Exception:
    note("STARTUP FAILED\n" + traceback.format_exc())
