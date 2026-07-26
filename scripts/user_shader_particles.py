"""Stateless GPU particle suite (docs/RenderDebug.md §6.2, particle
framework).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under
xvfb; creates its own document. Ties the four particle extensions
together on the App::Shader "Emitter" demo shape: seed-quad geometry,
a billboarding user vertex shader, the u_fcTime clock and the
Blend/DepthWrite render-state override.

- emitter-invisible: the Emitter demo is N degenerate quads (4
  coincident vertices per particle) — with no user program the frame
  equals the empty scene.
- particles-appear: a particle VS (a_normal.xy = corner, a_normal.z =
  particle index, a_color0 = seed) expands them into additive
  billboards — thousands of warm pixels, asserted under freeze for
  determinism.
- particles-animate: unfrozen, frames 0.7s apart differ.
- particles-freeze: frozen captures are byte-equal.
- emitter-off: Demo=None restores the empty frame byte-exact.

Env: US_OUT (output dir), US_RESULT (result file).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "particles.txt"))

PARTICLE_VS = """$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
uniform vec4 u_fcTime;
void main()
{
    vec2 corner = a_normal.xy;
    float idx = a_normal.z;
    vec3 seed = a_color0.xyz;
    // cycling life: rise and fade, phase-spread by seed and index
    float life = fract(u_fcTime.x * 0.35 + seed.x + idx);
    vec3 base = a_position;
    base.z += life * 6.0;
    vec4 vpos = mul(u_modelView, vec4(base, 1.0));
    vpos.xy += corner * 0.35;
    gl_Position = mul(u_proj, vpos);
    v_normal = vec3(0.0, 0.0, 1.0);
    v_color0 = vec4(1.0, 0.55, 0.15, 1.0) * (1.0 - life);
    v_vpos = vpos.xyz;
}
"""

PARTICLE_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = v_color0;
}
"""

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

results = []

def log(msg):
    results.append(str(msg))
    print("US:", msg, flush=True)

def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()

def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)

def cap(name):
    view = FreeCADGui.ActiveDocument.ActiveView
    path = os.path.join(OUT, name + ".png")
    view.saveRenderDump(path)
    return path

def load(path):
    import numpy as np
    from PySide.QtGui import QImage
    img = QImage(path).convertToFormat(QImage.Format_RGBA8888)
    buf = np.frombuffer(bytes(img.constBits()), dtype=np.uint8)
    return buf.reshape((img.height(), img.width(), 4))[:, :, :3].astype(int)

def changed_count(a, b, tol=3):
    import numpy as np
    return int(np.any(np.abs(load(a) - load(b)) > tol, axis=2).sum())

def warm_count(path):
    """Pixels clearly warmer than the blue-gray background, excluding
    the right-edge overlay column (NaviCube / axis cross)."""
    import numpy as np
    ia = load(path)[:, :-340, :]
    warm = (ia[:, :, 0] > ia[:, :, 2] + 20) & (ia[:, :, 0] > 60)
    return int(warm.sum())

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("ParticleTest")
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewTrimetric()
        pump()
        empty = cap("p_empty")

        # ---- emitter without a user program renders nothing ----
        prog = doc.addObject("App::ShaderProgram", "PProg")
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "Emitter"
        sh.EmitterCount = 300
        sh.DemoSize = (10.0, 10.0, 10.0)
        doc.recompute()
        view.fitAll()
        pump()
        # camera now framed on the (invisible) spread box; re-baseline
        empty = cap("p_emitter_stock")
        n = warm_count(empty)
        log("stock emitter warm px: %d" % n)
        log("ASSERT emitter-invisible: %s" % ("PASS" if n < 50 else "FAIL"))

        # ---- particle program: billboards appear (frozen = exact) ----
        view.RenderDebug_FreezeFrame = True
        prog.VertexProgram = PARTICLE_VS
        prog.FragmentProgram = PARTICLE_FS
        prog.Blend = "Additive"
        prog.DepthWrite = False
        doc.recompute()
        settle()
        frozen = cap("p_particles_frozen")
        n = warm_count(frozen)
        log("frozen particle warm px: %d" % n)
        log("ASSERT particles-appear: %s" % ("PASS" if n > 2000 else "FAIL"))

        f2 = cap("p_particles_frozen2")
        ok = byte_equal(frozen, f2, "frozen particle captures")
        log("ASSERT particles-freeze: %s" % ("PASS" if ok else "FAIL"))

        # ---- live clock animates ----
        view.RenderDebug_FreezeFrame = False
        settle(5)
        t0 = cap("p_anim0")
        time.sleep(0.7)
        pump(3)
        t1 = cap("p_anim1")
        d = changed_count(t0, t1)
        log("live frames 0.7s apart changed px: %d" % d)
        log("ASSERT particles-animate: %s" % ("PASS" if d > 500 else "FAIL"))

        # ---- emitter off restores the empty frame ----
        view.RenderDebug_FreezeFrame = True
        sh.Demo = "None"
        doc.recompute()
        settle(5)
        view.RenderDebug_FreezeFrame = False
        pump(3)
        off = cap("p_emitter_off")
        ok = byte_equal(empty, off, "emitter off restore")
        log("ASSERT emitter-off: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
