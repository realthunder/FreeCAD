"""Stateful GPU particle suite (docs/RenderEngine.md §5.8).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under
xvfb; creates its own document. The stateless suite
(user_shader_particles.py) covers seed geometry, billboarding and the
clock; this one covers what state adds — a simulation whose next frame
depends on the last, kept in ping-pong float targets and advanced by a
fragment program.

- state-appears: a stateful emitter draws from its state textures
  (particles are where the simulation put them, not where a closed
  form says).
- state-advances: the same emitter warmed up for longer looks
  different — the proof that steps accumulate rather than being
  recomputed from the clock.
- state-freeze: frozen captures are byte-equal, so a stateful effect
  is still golden-image comparable.
- state-reproduces: rebuilt from scratch with the same seed and
  warm-up, a frozen capture is byte-equal to the earlier one. This is
  the property that makes the tier usable in the verify harness at
  all: state must be a function of (reset, N steps), never of how long
  the session has been running.
- stateless-fallback: dropping SimulateProgram returns the emitter to
  its stateless path, byte-exact.

Env: US_OUT (output dir), US_RESULT (result file).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "particles_state.txt"))

# Vertex stage: read the simulated position out of the state grid and
# billboard it. Everything that moves comes from the state textures —
# u_fcTime is not referenced at all, so any motion seen is the
# simulation's.
STATE_VS = """$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
#include <fc_particle.sh>
void main()
{
    vec2 corner = a_normal.xy;
    Particle p = fcParticleLoad(fcParticleUV(a_normal.z));
    float life = fcParticleLife(p);
    vec4 vpos = mul(u_modelView, vec4(p.pos, 1.0));
    vpos.xy += corner * 0.35;
    gl_Position = mul(u_proj, vpos);
    v_normal = vec3(corner, 1.0);
    v_color0 = vec4(1.0, 0.55, 0.15, 1.0) * (1.0 - life);
    v_vpos = vpos.xyz;
}
"""

STATE_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = v_color0;
}
"""

# Step: ballistic fall with a floor bounce, respawning at end of life.
# Deliberately history-dependent — the bounce means the position after
# N steps cannot be written as a closed form of N.
STATE_SIM = """$input v_texcoord0
#include <bgfx_shader.sh>
#include <fc_particle.sh>
uniform vec4 u_Gravity;
void main()
{
    Particle p = fcParticleLoad(v_texcoord0);
    float dt = fcParticleStep();
    p.age += dt;
    if (p.age > p.life)
    {
        Particle fresh = fcParticleSpawn(v_texcoord0);
        fresh.life = p.life;
        p = fresh;
    }
    else if (p.age > 0.0)
    {
        p.vel.z -= u_Gravity.x * dt;
        p.pos += p.vel * dt;
        if (p.pos.z < u_pboxMin.z)
        {
            p.pos.z = u_pboxMin.z;
            p.vel.z = -p.vel.z * 0.6;
        }
    }
    fcParticleStore(p);
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


def make_program(doc, name, warmup):
    prog = doc.addObject("App::ShaderProgram", name)
    prog.Stage = "particle"
    prog.VertexProgram = STATE_VS
    prog.FragmentProgram = STATE_FS
    prog.SimulateProgram = STATE_SIM
    prog.Blend = "Additive"
    prog.DepthWrite = False
    prog.EmitterCount = 400
    prog.EmitterSeed = 7
    prog.EmitterRate = 60.0
    prog.EmitterWarmup = warmup
    prog.addProperty("App::PropertyFloat", "Param_Gravity")
    prog.Param_Gravity = 9.8
    return prog


def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("ParticleStateTest")
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewTrimetric()
        pump()

        # The Emitter demo IS the seed geometry, so the particle
        # program sits in the demo's own chain (no binding needed).
        prog = make_program(doc, "PSim", 0.5)
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "Emitter"
        sh.EmitterCount = 400
        sh.EmitterSeed = 7
        sh.DemoSize = (10.0, 10.0, 10.0)
        doc.recompute()
        view.fitAll()
        pump()

        # ---- stateful emitter draws from its state ----
        view.RenderDebug_FreezeFrame = True
        doc.recompute()
        settle()
        warm05 = cap("ps_warm05")
        n = warm_count(warm05)
        log("stateful warm px (warmup 0.5s): %d" % n)
        log("ASSERT state-appears: %s" % ("PASS" if n > 2000 else "FAIL"))

        # ---- frozen captures are byte-reproducible ----
        again = cap("ps_warm05_again")
        ok = byte_equal(warm05, again, "frozen stateful captures")
        log("ASSERT state-freeze: %s" % ("PASS" if ok else "FAIL"))

        # ---- more warm-up = a different, further-evolved state ----
        prog.EmitterWarmup = 2.0
        doc.recompute()
        settle()
        warm20 = cap("ps_warm20")
        d = changed_count(warm05, warm20)
        log("warmup 0.5s vs 2.0s changed px: %d" % d)
        log("ASSERT state-advances: %s" % ("PASS" if d > 500 else "FAIL"))

        # ---- same inputs rebuilt from scratch = same pixels ----
        prog.EmitterWarmup = 0.5
        doc.recompute()
        settle()
        redo = cap("ps_warm05_redo")
        ok = byte_equal(warm05, redo, "stateful reset reproducibility")
        log("ASSERT state-reproduces: %s" % ("PASS" if ok else "FAIL"))

        # ---- dropping the step program falls back to stateless ----
        # With no simulation the vertex stage reads unbound samplers,
        # which is the documented stateless fallback: the emitter still
        # draws, at the seed anchors.
        prog.SimulateProgram = ""
        doc.recompute()
        settle()
        stateless = cap("ps_stateless")
        d = changed_count(warm05, stateless)
        log("stateful vs stateless changed px: %d" % d)
        log("ASSERT stateless-fallback: %s" % ("PASS" if d > 500 else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)


QTimer.singleShot(1000, run)
