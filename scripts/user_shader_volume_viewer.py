"""WASM-tier volume-splice transport suite (docs/RenderEngine.md §5.11).

In-FreeCAD driver run by user-shader-verify.sh (viewer leg) after
scripts/demo-lights.py. Binds a GREEN-ramp volume-stage medium to a
fire proxy in the backend scene and asserts the connected browser
viewer shows it: without the v24 splice transport the compiler-less
viewer falls back to the STOCK orange flame (visually identical to
the baseline), so a baseline difference — plus green-dominant flame
pixels — proves the assembled variant's server-compiled binaries
arrived and were adopted. Removing the binding restores the baseline.

Env: US_OUT (output dir), US_RESULT (default US_OUT/result.txt).
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["US_OUT"]
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "result.txt"))

GREEN_SRC = """
float fcMediumField(vec3 wp)
{
    return fcStockFireField(FC_MEDIUM_SLOT, wp);
}
vec3 fcMediumRamp(float t)
{
    return vec3(0.0, smoothstep(0.0, 0.5, t), 0.0);
}
"""

_shared = {}
_steps = []
_state = {"i": 0}


def note(msg):
    # DONE/ABORT terminate the harness wait (anchored ^DONE$ grep) —
    # they must go out unstamped.
    stamp = "" if str(msg) in ("DONE",) or str(msg).startswith("ABORT") \
        else time.strftime("%H:%M:%S") + " "
    print("US: %s%s" % (stamp, msg), flush=True)
    with open(RESULT, "a") as f:
        f.write("%s%s\n" % (stamp, msg))


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))


def view():
    return FreeCADGui.ActiveDocument.ActiveView


def add_step(delay_ms, fn, auto=True):
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
            run_next()
            return
        if auto:
            run_next()

    QtCore.QTimer.singleShot(delay, wrapped)


def capture_viewer(tag):
    path = os.path.join(OUT, tag + ".png")
    try:
        r = view().saveRenderDump(path, source="viewer", metadata=False)
    except Exception as e:
        note("viewer capture %s failed: %s" % (tag, e))
        return False
    # source='viewer' returns a LIST (one path per connected viewer).
    paths = r if isinstance(r, list) else [r]
    return path in paths and os.path.getsize(path) > 1000


def load_rgba(tag):
    img = QtGui.QImage(os.path.join(OUT, tag + ".png"))
    if img.isNull():
        return None, 0, 0
    img = img.convertToFormat(QtGui.QImage.Format_RGBA8888)
    return img.constBits().tobytes(), img.width(), img.height()


def diff_stats(tag_a, tag_b):
    ba, wa, ha = load_rgba(tag_a)
    bb, wb, hb = load_rgba(tag_b)
    if ba is None or bb is None or (wa, ha) != (wb, hb):
        return -1, 0
    changed = 0
    for o in range(0, wa * ha * 4, 4):
        da = abs(ba[o] - bb[o]) + abs(ba[o + 1] - bb[o + 1]) \
            + abs(ba[o + 2] - bb[o + 2])
        if da > 12:  # SwiftShader noise floor
            changed += 1
    return changed, wa * ha


def green_count(tag):
    """Pixels whose green channel clearly dominates red and blue.
    The NaviCube is disabled for this suite (stage() switches it off):
    its shaded faces read greenish and once faked a PASS."""
    b, w, h = load_rgba(tag)
    if b is None:
        return -1
    n = 0
    for o in range(0, w * h * 4, 4):
        if b[o + 1] > b[o] + 24 and b[o + 1] > b[o + 2] + 24:
            n += 1
    return n


def wait_renderer():
    deadline = [40]

    def poll():
        v = view()
        ok = False
        try:
            stats = v.getRenderStats()
            ok = isinstance(stats, dict) and stats.get("geometryPixels", 0) > 0
        except Exception:
            pass
        if ok and hasattr(v, "Render_AO"):
            note("renderer ready")
            run_next()
        elif deadline[0] <= 0:
            note("ABORT renderer never became ready")
            os._exit(1)
        else:
            deadline[0] -= 1
            QtCore.QTimer.singleShot(1000, poll)

    return poll


def stage():
    # No NaviCube in captures: its warm/green corner pixels pollute
    # pixel-count asserts (the View3DSettings handler applies live).
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
        "ShowNaviCube", False)
    doc = FreeCAD.ActiveDocument
    fire = doc.addObject("Part::Box", "FireBody")
    fire.Length = 6
    fire.Width = 6
    fire.Height = 9
    fire.Placement.Base = FreeCAD.Vector(4, 4, 0)
    doc.recompute()
    _shared["fire"] = fire
    v = view()
    # The scene light is a shading switch now, not a draw style
    # (docs/CoinRetirement.md stage 4e).
    FreeCADGui.activeDocument().activeView().Render_Light = True
    v.Render_Volumetric = True
    v.RenderDebug_FreezeFrame = True
    v.viewIsometric()
    v.fitAll()
    note("staged (fire proxy + shadow style + volumetric + freeze)")


def wait_viewer_baseline():
    deadline = [90]

    def poll():
        view().redraw()
        if capture_viewer("v-baseline"):
            check("viewer baseline capture", True)
            run_next()
        elif deadline[0] <= 0:
            note("ABORT no viewer answered within 90s")
            os._exit(1)
        else:
            deadline[0] -= 1
            QtCore.QTimer.singleShot(1000, poll)

    return poll


def bind_green_medium():
    doc = FreeCAD.ActiveDocument
    prog = doc.addObject("App::ShaderProgram", "GreenMedium")
    prog.Stage = "volume"
    prog.FragmentProgram = GREEN_SRC
    sh = doc.addObject("App::Shader", "GreenFx")
    sh.Programs = [prog]
    sh.Demo = "None"
    ap = doc.addObject("App::ShaderBinding", "GreenLook")
    ap.ElementList = [sh, _shared["fire"]]
    doc.recompute()
    _shared["ap"] = ap
    view().redraw()
    note("green volume medium bound to the fire proxy")


def delete_binding():
    # Deletion, not hiding: the Appearance is a LinkGroup, so hiding it
    # hides its claimed target box too (LinkGroup semantics) — deletion
    # is the Object-scope teardown that restores the baseline.
    doc = FreeCAD.ActiveDocument
    doc.removeObject(_shared["ap"].Name)
    doc.removeObject("GreenFx")
    doc.removeObject("GreenMedium")
    doc.recompute()
    view().redraw()
    note("binding deleted")


def wait_viewer_effect(tag, ref, expect_diff, name, timeout=120,
                       min_changed=300, want_green=False):
    deadline = [timeout]

    def poll():
        view().redraw()
        ok = capture_viewer(tag)
        if ok:
            changed, total = diff_stats(ref, tag)
            differs = changed >= min_changed
            greens = green_count(tag) if want_green else 0
            green_ok = (not want_green) or greens >= 100
            if differs == expect_diff and green_ok:
                check(name, True, "changed %d / %d px, green %d"
                      % (changed, total, greens))
                run_next()
                return
            # keep the wait visible — a silent poll loop is
            # indistinguishable from a hang in the logs
            if deadline[0] % 10 == 0:
                note("... %s waiting (changed %d, green %d, %d polls "
                     "left)" % (name, changed, greens, deadline[0]))
        if deadline[0] <= 0:
            check(name, False, "timeout waiting for %s vs %s"
                  % ("difference" if expect_diff else "equality", ref))
            run_next()
        else:
            deadline[0] -= 1
            QtCore.QTimer.singleShot(1500, poll)

    return poll


os.makedirs(OUT, exist_ok=True)
open(RESULT, "w").close()

add_step(4000, wait_renderer(), auto=False)
add_step(800, stage)
add_step(2500, wait_viewer_baseline(), auto=False)
add_step(200, bind_green_medium)
# The splice compile is async on the backend; the republished snapshot
# with the v24 binaries can take a while on this box.
add_step(2000, wait_viewer_effect("v-green", "v-baseline", True,
                                  "green splice reaches browser",
                                  want_green=True), auto=False)
add_step(200, delete_binding)
add_step(1500, wait_viewer_effect("v-deleted", "v-baseline", False,
                                  "deleting binding restores browser",
                                  timeout=90),
         auto=False)

note("viewer volume-splice verify starting")
run_next()
