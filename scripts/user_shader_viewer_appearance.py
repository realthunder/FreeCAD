"""WASM-tier user-shader suite, document-object route (§6.5 slice 5).

In-FreeCAD driver run by user-shader-verify.sh (viewer leg) after
scripts/demo-lights.py — same shape as user_shader_viewer.py, but the
post shader arrives via the document-object route: App::ShaderProgram
(stage=post, Param_Mix property) + App::Shader + empty-target
App::ShaderBinding. A passing check proves the merged setAppearanceShaders
config reaches the snapshot's user-shader table, the server-side essl
compile, and the viewer render.

Env: US_OUT (output dir), US_RESULT (default US_OUT/result.txt).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["US_OUT"]
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "result.txt"))

FS_POST = """$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);
uniform vec4 u_Mix;

void main()
{
    vec4 c = texture2D(s_texScene, v_texcoord0);
    gl_FragColor = vec4(mix(c.rgb, vec3_splat(1.0) - c.rgb, u_Mix.x), c.a);
}
"""

_shared = {}
_steps = []
_state = {"i": 0}


def note(msg):
    print("US:", msg, flush=True)
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


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


def diff_stats(tag_a, tag_b):
    a = QtGui.QImage(os.path.join(OUT, tag_a + ".png"))
    b = QtGui.QImage(os.path.join(OUT, tag_b + ".png"))
    if a.size() != b.size() or a.isNull():
        return -1, 0
    a = a.convertToFormat(QtGui.QImage.Format_RGBA8888)
    b = b.convertToFormat(QtGui.QImage.Format_RGBA8888)
    w, h = a.width(), a.height()
    ba = a.constBits().tobytes()
    bb = b.constBits().tobytes()
    changed = 0
    for o in range(0, w * h * 4, 4):
        da = abs(ba[o] - bb[o]) + abs(ba[o + 1] - bb[o + 1]) \
            + abs(ba[o + 2] - bb[o + 2])
        if da > 12:  # SwiftShader noise floor
            changed += 1
    return changed, w * h


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
    v = view()
    v.RenderDebug_FreezeFrame = True
    v.viewIsometric()
    v.fitAll()
    note("staged")


def wait_viewer_baseline():
    deadline = [60]

    def poll():
        view().redraw()
        if capture_viewer("v-baseline"):
            check("viewer baseline capture", True)
            run_next()
        elif deadline[0] <= 0:
            note("ABORT no viewer answered within 60s")
            os._exit(1)
        else:
            deadline[0] -= 1
            QtCore.QTimer.singleShot(1000, poll)

    return poll


def create_appearance():
    doc = FreeCAD.ActiveDocument
    prog = doc.addObject("App::ShaderProgram", "Invert")
    prog.Stage = "post"
    prog.FragmentProgram = FS_POST
    prog.addProperty("App::PropertyFloat", "Param_Mix")
    prog.Param_Mix = 1.0
    sh = doc.addObject("App::Shader", "Fx")
    sh.Programs = [prog]
    sh.Demo = "None"
    ap = doc.addObject("App::ShaderBinding", "Look")
    ap.ElementList = [sh]  # shader-only group = scene-level post
    doc.recompute()
    _shared["prog"] = prog
    _shared["ap"] = ap
    view().redraw()
    note("empty-target Appearance created")


def tweak_param():
    _shared["prog"].Param_Mix = 0.35
    view().redraw()
    note("Param_Mix -> 0.35")


def hide_appearance():
    _shared["ap"].ViewObject.Visibility = False
    view().redraw()
    note("Appearance hidden")


def wait_viewer_effect(tag, ref, expect_diff, name, timeout=45,
                       min_changed=200):
    deadline = [timeout]

    def poll():
        view().redraw()
        ok = capture_viewer(tag)
        if ok:
            changed, total = diff_stats(ref, tag)
            differs = changed >= min_changed
            if differs == expect_diff:
                check(name, True, "changed %d / %d px" % (changed, total))
                run_next()
                return
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
add_step(1500, wait_viewer_baseline(), auto=False)
add_step(200, create_appearance)
add_step(1500, wait_viewer_effect("v-appearance", "v-baseline", True,
                                  "appearance post reaches browser",
                                  min_changed=200000), auto=False)
add_step(200, tweak_param)
add_step(1000, wait_viewer_effect("v-param", "v-appearance", True,
                                  "param edit reaches browser",
                                  min_changed=200000), auto=False)
add_step(200, hide_appearance)
add_step(1000, wait_viewer_effect("v-hidden", "v-baseline", False,
                                  "hiding appearance restores browser"),
         auto=False)

note("viewer appearance verify starting")
run_next()
