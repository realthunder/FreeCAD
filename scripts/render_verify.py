"""Render-verification capture driver (docs/RenderDebug.md §5, phase 3).

Runs inside FreeCAD *after* a demo/scene script (pass both to the binary:
``FreeCAD scene.py render_verify.py``); scripts/render-verify.sh does the
launching, isolation and log collection. For every (camera, mode) pair it
writes ``<scene>--<camera>--mode<N>.png`` plus the reproducibility sidecar
JSON via ``View3DInventor.saveRenderDump``, with ``RenderDebug_FreezeFrame``
on so captures are deterministic. ``render_diff.py`` compares two such
capture directories stage by stage.

Staging comes from one of two sources:

- **Manifest** (default): the RV_CAMERAS named standard views, staged as
  ``view<Name>()`` + ``fitAll()`` — deterministic for a fixed scene.
- **Golden restage** (RV_GOLDEN set): cameras, render properties *and*
  the recorded preferences are re-applied 1:1 from the golden captures'
  sidecar JSONs, so goldens stay valid when defaults change (§5
  "sidecars are the test manifest"). The preferences matter for what has
  no property form -- the enumerations, and the viewer's light rig,
  ambient, background and cache mode. Output basenames mirror the
  golden's so diffing pairs up.

Environment contract (all optional except RV_OUT):
  RV_OUT       capture output directory (required)
  RV_RESULT    result log (default RV_OUT/result.txt); ends with DONE/ABORT
  RV_SCENE_NAME  scene id used in filenames (default "scene")
  RV_CAMERAS   comma list of named views (default "iso,front,top")
  RV_MODES     comma list of RenderDebug_ViewMode ints (default "0,1,2,3,4")
  RV_GOLDEN    golden capture dir -> restage from its sidecars
  RV_VIEWER    "1" -> also capture the browser leg (source='viewer') per
               mode; waits up to RV_VIEWER_TIMEOUT s (default 120) for a
               viewer to connect. The viewer keeps its own camera (pin it
               with the page's &cam= URL parameter), so viewer captures are
               named <scene>--viewercam--mode<N>--viewer.png.
"""
import glob
import json
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["RV_OUT"]
RESULT = os.environ.get("RV_RESULT", os.path.join(OUT, "result.txt"))
SCENE = os.environ.get("RV_SCENE_NAME", "scene")
CAMERAS = [c for c in os.environ.get("RV_CAMERAS", "iso,front,top").split(",") if c]
MODES = [int(m) for m in os.environ.get("RV_MODES", "0,1,2,3,4").split(",") if m != ""]
GOLDEN = os.environ.get("RV_GOLDEN", "")
VIEWER = os.environ.get("RV_VIEWER", "") == "1"
VIEWER_TIMEOUT = float(os.environ.get("RV_VIEWER_TIMEOUT", "120"))

# Named standard views the manifest may use (View3DInventorPy methods).
VIEW_METHODS = {
    "iso": "viewIsometric",
    "front": "viewFront",
    "top": "viewTop",
    "rear": "viewRear",
    "right": "viewRight",
    "left": "viewLeft",
    "bottom": "viewBottom",
    "dimetric": "viewDimetric",
    "trimetric": "viewTrimetric",
}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))


def view():
    return FreeCADGui.ActiveDocument.ActiveView


def apply_properties(v, props):
    """Re-apply a sidecar's properties dict onto the view, best-effort.

    Sidecar values are JSON-native (bool/number) or str() of the Python
    value — the enum string for enum properties (directly assignable), a
    tuple repr like "(0.2, 0.2, 0.2, 1.0)" for color/vector properties
    (re-parsed here). Numbers arrive as float; retry as int for int-typed
    properties.
    """
    import ast

    applied, failed = 0, []
    for name, value in sorted(props.items()):
        if not hasattr(v, name):
            failed.append(name + ":missing")
            continue
        candidates = [value]
        if isinstance(value, float):
            candidates.append(int(value))
        elif isinstance(value, str) and "(" in value:
            # "(r, g, b, a)" colors, "Vector (x, y, z)" vectors, ...
            try:
                candidates.insert(0, ast.literal_eval(value[value.index("("):]))
            except Exception:
                pass
        for c in candidates:
            try:
                setattr(v, name, c)
                applied += 1
                break
            except Exception:
                pass
        else:
            failed.append(name)
    return applied, failed


PREF_SETTERS = {
    "bool": ("SetBool", bool),
    "int": ("SetInt", int),
    "unsigned": ("SetUnsigned", int),
    "float": ("SetFloat", float),
    "string": ("SetString", str),
}


def apply_preferences(prefs):
    """Re-apply a sidecar's preference groups, best-effort.

    The sidecar records the parameters a Render_* view property does not
    cover: the enumerations that never become properties (AOMethod,
    MatcapPreset, WaterRippleType), and the viewer's own rig -- lights,
    ambient, background, chrome, cache mode -- which has no property
    form at all.

    Values arrive bucketed by parameter type, because a group is a set
    of typed maps and JSON cannot tell an int from an unsigned: writing
    a colour back with SetInt files it under Integer while the reader
    goes on taking the stale Unsigned one.
    """
    import FreeCAD

    applied, failed = 0, []
    for group, buckets in sorted(prefs.items()):
        grp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/" + group)
        for typename, values in sorted(buckets.items()):
            entry = PREF_SETTERS.get(typename)
            if not entry:
                failed.append("%s/<%s>" % (group, typename))
                continue
            setter, cast = entry
            for name, value in sorted(values.items()):
                try:
                    getattr(grp, setter)(name, cast(value))
                    applied += 1
                except Exception:
                    failed.append("%s/%s" % (group, name))
    return applied, failed


def golden_stagings():
    """(prefix, camera-string, properties, preferences) per camera found
    in RV_GOLDEN, read from the beauty (mode0) desktop-leg sidecars."""
    stagings = []
    for sidecar in sorted(glob.glob(os.path.join(GOLDEN, "*--mode0.png.json"))):
        base = os.path.basename(sidecar)
        if "--viewer" in base:
            continue
        data = json.load(open(sidecar))
        prefix = re.sub(r"--mode0\.png\.json$", "", base)
        stagings.append((prefix, data.get("camera", ""),
                         data.get("properties", {}),
                         data.get("preferences", {})))
    return stagings


# ---- QTimer step chain (a bare timer in a script namespace gets GC'd; a
# ---- singleShot chain with module-level state does not).
_steps = []
_state = {"i": 0}


def add_step(delay_ms, fn, auto=True):
    """auto=False steps poll: they re-arm their own timer and call
    run_next() themselves when finished."""
    _steps.append((delay_ms, fn, auto))


def run_next():
    if _state["i"] >= len(_steps):
        note("DONE")
        QtCore.QTimer.singleShot(500, lambda: os._exit(0))
        return
    delay, fn, auto = _steps[_state["i"]]
    _state["i"] += 1

    def wrapped():
        try:
            fn()
        except Exception:
            note("FAIL step %d:\n%s" % (_state["i"], traceback.format_exc()))
            run_next()  # a crashed step (even a poller) must not stall the chain
            return
        if auto:
            run_next()

    QtCore.QTimer.singleShot(delay, wrapped)


def wait_renderer():
    """Poll until the external renderer produces stats (or give up)."""
    deadline = [40]

    def poll():
        v = view()
        ok = False
        try:
            stats = v.getRenderStats()
            ok = isinstance(stats, dict) and stats.get("geometryPixels", 0) > 0
        except Exception:
            pass
        if ok and hasattr(v, "RenderDebug_ViewMode"):
            note("renderer ready")
            run_next()
        elif deadline[0] <= 0:
            note("ABORT renderer never became ready")
            os._exit(1)
        else:
            deadline[0] -= 1
            QtCore.QTimer.singleShot(1000, poll)

    return poll


def freeze():
    view().RenderDebug_FreezeFrame = True
    note("freeze on")


def settle_state():
    """Run the frozen frame until stateful content has reached its
    target, before anything is captured.

    A stateful particle emitter (docs/RenderEngine.md §5.8) reaches a
    frozen frame's warm-up in whole fixed steps, only kParticleSteps of
    them per frame -- a 2.5s warm-up at 60Hz is ~75 frames. Capturing
    on a wall-clock delay instead lands on whatever step count the run
    happened to reach, so two runs of the same scene put the sparks in
    different places and every golden diff shows it. The state is not
    camera-dependent and stops advancing once it is at the warm-up, so
    one settle here covers every staging that follows.
    """
    v = view()
    for _ in range(150):
        v.redraw()
        FreeCADGui.updateGui()
    note("state settled")


def stage_named(cam):
    def fn():
        v = view()
        method = VIEW_METHODS.get(cam)
        if not method:
            raise ValueError("unknown camera name: %s" % cam)
        getattr(v, method)()
        v.fitAll()
        note("staged camera %s" % cam)
    return fn


def stage_golden(prefix, camera, props, prefs):
    def fn():
        v = view()
        # Preferences first, properties second: a Render_* view property
        # outranks the parameter it was seeded from, so applying them
        # the other way round would let a stale property win.
        pref_applied, pref_failed = apply_preferences(prefs)
        applied, failed = apply_properties(v, props)
        if camera:
            v.setCamera(camera)
        note("restaged %s (props %d applied%s; prefs %d applied%s)" % (
            prefix, applied, ", failed: %s" % failed if failed else "",
            pref_applied,
            ", failed: %s" % pref_failed if pref_failed else ""))
    return fn


def capture(prefix, m):
    def fn():
        path = os.path.join(OUT, "%s--mode%d.png" % (prefix, m))
        r = view().saveRenderDump(path, mode=m) if m > 0 \
            else view().saveRenderDump(path)
        check("capture %s" % os.path.basename(path),
              r == path and os.path.getsize(path) > 1000)
    return fn


def viewer_wait():
    """Wait for a connected viewer to answer a probe dump."""
    deadline = [max(1, int(VIEWER_TIMEOUT / 3))]
    probe = os.path.join(OUT, "viewer-probe.png")

    def poll():
        try:
            view().saveRenderDump(probe, source="viewer", metadata=False)
            for p in glob.glob(probe.replace(".png", "*")):
                os.remove(p)
            note("viewer connected")
            run_next()
        except Exception as exc:
            if deadline[0] <= 0:
                note("FAIL viewer leg | no viewer connected: %s" % exc)
                run_next()  # desktop captures still stand
            else:
                deadline[0] -= 1
                QtCore.QTimer.singleShot(3000, poll)

    return poll


def capture_viewer(m):
    def fn():
        path = os.path.join(OUT, "%s--viewercam--mode%d--viewer.png" % (SCENE, m))
        paths = view().saveRenderDump(path, source="viewer", mode=m) if m > 0 \
            else view().saveRenderDump(path, source="viewer")
        check("capture viewer mode%d" % m,
              bool(paths) and all(os.path.getsize(p) > 1000 for p in paths),
              paths)
    return fn


def build_steps():
    add_step(3000, wait_renderer(), auto=False)  # polls; advances itself
    add_step(200, freeze)
    add_step(200, settle_state)
    if GOLDEN:
        stagings = golden_stagings()
        if not stagings:
            note("ABORT no *--mode0.png.json sidecars in golden dir " + GOLDEN)
            os._exit(1)
        note("restaging %d cameras from golden %s" % (len(stagings), GOLDEN))
        for prefix, camera, props, prefs in stagings:
            add_step(300, stage_golden(prefix, camera, props, prefs))
            for m in MODES:
                add_step(700 if m == MODES[0] else 200, capture(prefix, m))
    else:
        for cam in CAMERAS:
            add_step(300, stage_named(cam))
            for m in MODES:
                add_step(700 if m == MODES[0] else 200, capture("%s--%s" % (SCENE, cam), m))
    if VIEWER:
        add_step(500, viewer_wait(), auto=False)  # polls; advances itself
        for m in MODES:
            add_step(300, capture_viewer(m))


try:
    os.makedirs(OUT, exist_ok=True)
    build_steps()
    QtCore.QTimer.singleShot(2000, run_next)
except Exception:
    note("ABORT setup:\n" + traceback.format_exc())
    os._exit(1)
