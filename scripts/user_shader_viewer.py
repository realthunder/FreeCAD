"""WASM-tier user-shader suite, scene-graph route (docs/RenderDebug.md §6.3).

In-FreeCAD driver run by user-shader-verify.sh (viewer leg) after
scripts/demo-lights.py, with FC_BGFX_SERVE_SCENE set and a headless
browser (scripts/wasm-hold.js) holding the viewer page.

Every "viewer" capture goes saveRenderDump(source='viewer'): the backend
pushes dumpFrame to the connected browser, so a passing check proves the
whole pipeline — server-side essl compile, snapshot shader-table
transport, viewer-side program load from the shipped bins.

Checks:
  1. viewer baseline capture answers
  2. post-stage shader (invert) reaches the browser render
  3. SoShaderParameter live edit reaches the browser
  4. material-stage shader on the Ball reaches the browser, localized
  5. broken material shader falls back to ~baseline on the browser
  6. removal restores ~baseline on the browser

Env: US_OUT (output dir), US_RESULT (default US_OUT/result.txt).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui
from pivy import coin

OUT = os.environ["US_OUT"]
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "result.txt"))

FS_POST = """$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);
uniform vec4 u_postTint;

void main()
{
    vec4 c = texture2D(s_texScene, v_texcoord0);
    vec3 inv = vec3_splat(1.0) - c.rgb;
    gl_FragColor = vec4(mix(c.rgb, inv * u_postTint.rgb, u_postTint.w), c.a);
}
"""

FS_MAT = """$input v_normal, v_color0, v_vpos

#include <bgfx_shader.sh>

uniform vec4 u_userTint;

void main()
{
    vec3 n = normalize(v_normal);
    gl_FragColor = vec4((n * 0.5 + vec3_splat(0.5)) * u_userTint.rgb, 1.0);
}
"""

FS_MAT_BROKEN = FS_MAT.replace("void main()", "void main(")

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
    """One viewer capture; returns True when a browser answered."""
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
    """(changed px count, total, change bbox) between two captures."""
    a = QtGui.QImage(os.path.join(OUT, tag_a + ".png"))
    b = QtGui.QImage(os.path.join(OUT, tag_b + ".png"))
    if a.size() != b.size() or a.isNull():
        return -1, 0, None
    a = a.convertToFormat(QtGui.QImage.Format_RGBA8888)
    b = b.convertToFormat(QtGui.QImage.Format_RGBA8888)
    w, h = a.width(), a.height()
    ba = a.constBits().tobytes()
    bb = b.constBits().tobytes()
    changed = 0
    minx, miny, maxx, maxy = w, h, -1, -1
    for y in range(h):
        row = y * w * 4
        for x in range(w):
            o = row + x * 4
            da = abs(ba[o] - bb[o]) + abs(ba[o + 1] - bb[o + 1]) \
                + abs(ba[o + 2] - bb[o + 2])
            if da > 12:  # SwiftShader noise floor
                changed += 1
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
                if y < miny:
                    miny = y
                if y > maxy:
                    maxy = y
    return changed, w * h, (minx, miny, maxx, maxy)


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
    """Poll until the held browser page answers a dumpFrame."""
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


def make_program(stage_name, source, param_name):
    prog = coin.SoShaderProgram()
    prog.stage.setValue(stage_name)
    fs = coin.SoFragmentShader()
    fs.sourceType = coin.SoShaderObject.BGFX_SC
    fs.sourceProgram.setValue(source)
    tint = coin.SoShaderParameter4f()
    tint.name.setValue(param_name)
    tint.value.setValue(coin.SbVec4f(1.0, 1.0, 1.0, 1.0))
    fs.parameter.set1Value(0, tint)
    prog.shaderObject.set1Value(0, fs)
    _shared["prog"] = prog
    _shared["fs"] = fs
    _shared["tint"] = tint
    return prog


def inject_post():
    root = view().getViewer().getSceneGraph()
    root.insertChild(make_program("post", FS_POST, "u_postTint"), 0)
    _shared["root"] = root
    view().redraw()
    note("post shader injected")


def inject_material():
    ball = FreeCAD.ActiveDocument.getObject("Ball")
    root = ball.ViewObject.RootNode
    root.insertChild(make_program("material", FS_MAT, "u_userTint"), 0)
    _shared["root"] = root
    view().redraw()
    note("material shader injected on Ball")


def remove_node():
    _shared["root"].removeChild(_shared["prog"])
    _shared["prog"] = None
    view().redraw()
    note("program node removed")


def wait_viewer_effect(tag, ref, expect_diff, name, timeout=45,
                       min_changed=200):
    """Poll viewer captures until tag differs (or not) from ref.

    The async server-side compile + republish makes the effect land at
    an unknown frame — poll until the expected state, then judge.
    """
    deadline = [timeout]

    def poll():
        view().redraw()
        ok = capture_viewer(tag)
        if ok:
            changed, total, bbox = diff_stats(ref, tag)
            differs = changed >= min_changed
            if differs == expect_diff:
                check(name, True, "changed %d / %d px, bbox %s"
                      % (changed, total, bbox))
                _shared["last_diff"] = (changed, total, bbox)
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


def tweak_param():
    _shared["tint"].value.setValue(coin.SbVec4f(1.0, 0.15, 0.15, 1.0))
    view().redraw()
    note("param tweaked")


def break_shader():
    _shared["fs"].sourceProgram.setValue(FS_MAT_BROKEN)
    view().redraw()
    note("source -> broken")


def check_localized():
    changed, total, bbox = _shared.get("last_diff", (-1, 0, None))
    ok = 0 < changed < total * 0.30
    check("material change localized", ok,
          "changed %d / %d px, bbox %s" % (changed, total, bbox))


def desktop_capture(tag):
    def fn():
        path = os.path.join(OUT, tag + ".png")
        r = view().saveRenderDump(path, metadata=False)
        check("desktop capture %s" % tag,
              r == path and os.path.getsize(path) > 1000)
    return fn


os.makedirs(OUT, exist_ok=True)
open(RESULT, "w").close()

add_step(4000, wait_renderer(), auto=False)
add_step(800, stage)
add_step(1500, wait_viewer_baseline(), auto=False)
add_step(200, desktop_capture("d-baseline"))
# post stage
add_step(200, inject_post)
add_step(1500, wait_viewer_effect("v-post", "v-baseline", True,
                                  "post shader reaches browser"), auto=False)
add_step(200, desktop_capture("d-post"))
add_step(200, tweak_param)
add_step(1000, wait_viewer_effect("v-post-tint", "v-post", True,
                                  "post param edit reaches browser"),
         auto=False)
add_step(200, remove_node)
add_step(1000, wait_viewer_effect("v-post-off", "v-baseline", False,
                                  "post removal restores browser"),
         auto=False)
# material stage
add_step(200, inject_material)
add_step(1500, wait_viewer_effect("v-mat", "v-baseline", True,
                                  "material shader reaches browser"),
         auto=False)
add_step(100, check_localized)
add_step(200, desktop_capture("d-mat"))
add_step(200, break_shader)
add_step(1500, wait_viewer_effect("v-mat-broken", "v-baseline", False,
                                  "broken material falls back on browser",
                                  timeout=60), auto=False)
add_step(200, remove_node)
add_step(1000, wait_viewer_effect("v-mat-off", "v-baseline", False,
                                  "material removal restores browser"),
         auto=False)

note("viewer user-shader verify starting")
run_next()
