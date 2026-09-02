"""User vertex-shader motion suite (docs/RenderDebug.md §6, particle
groundwork).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under
xvfb; creates its own document. Covers the user VERTEX shader path of
the material stage (App::ShaderProgram.VertexProgram -> SoVertexShader
BGFX_SC -> bridge -> getUserProgram) and the u_fcTime animation clock:

- vs-identity: a user VS replicating fc_mesh_vs.sh math + a flat-color
  FS must render identically (tol-3) to the SAME flat-color FS with
  the stock vertex stage. Isolates the VS swap itself.
- vs-displace: Param_Offset (PropertyVector -> u_Offset) added to
  a_position must move the silhouette: green-pixel centroid shifts.
  (Known artifacts, by design: stock line/point draws keep the stock
  VS so wireframes stay put, and Coin's auto near/far planes fit the
  undisplaced bbox so large displacements clip.)
- blend-additive / blend-restore: App::ShaderProgram Blend/DepthWrite
  ride the reserved "fc_state" parameter to the backend, which
  re-records the beauty-draw state (background bleeds through an
  additive write); Default/true restores the stock state byte-exact.
- vs-motion: sweeping Param_Offset produces distinct frames each step
  (CPU-driven animation through the VS).
- time-animates: a VS referencing u_fcTime (engine clock, seconds in
  .x, live flag in .y) changes frame to frame.
- self-animating: with no forced redraws, a time-referencing user
  shader keeps the viewer's redraw loop alive by itself
  (render -> animating() -> scheduleRedraw).
- time-freeze-deterministic: RenderDebug_FreezeFrame pins u_fcTime to
  0 — consecutive captures byte-equal.
- delete restore: byte-equal to base (deletion is the Object-scope
  teardown — hiding an Appearance hides its claimed group children,
  including a directly-targeted model object, LinkGroup semantics).

Env: US_OUT (output dir), US_RESULT (result file).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "motion.txt"))

FLAT_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = vec4(0.0, 0.7, 0.1, 1.0);
}
"""

# Stock-equivalent transform (fc_mesh_vs.sh non-instanced path) plus a
# uniform-driven position offset. u_params is the engine uniform (global
# by name); u_Offset comes from the Param_Offset dynamic property.
OFFSET_VS = """$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
uniform vec4 u_params;
uniform vec4 u_Offset;
void main()
{
    vec3 pos = a_position + u_Offset.xyz;
    gl_Position = mul(u_modelViewProj, vec4(pos, 1.0));
    gl_Position.z += u_params.w * gl_Position.w;
    v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
    v_color0 = a_color0;
    v_vpos = mul(u_modelView, vec4(pos, 1.0)).xyz;
}
"""

# Bounded oscillation on the engine clock: u_fcTime.x = seconds (0 when
# frozen), y = 1 while the clock advances.
TIME_VS = """$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
uniform vec4 u_params;
uniform vec4 u_fcTime;
void main()
{
    vec3 pos = a_position;
    pos.z += 4.0 * sin(u_fcTime.x * 5.0) + 4.0;
    gl_Position = mul(u_modelViewProj, vec4(pos, 1.0));
    gl_Position.z += u_params.w * gl_Position.w;
    v_normal = mul(u_modelView, vec4(a_normal, 0.0)).xyz;
    v_color0 = a_color0;
    v_vpos = mul(u_modelView, vec4(pos, 1.0)).xyz;
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

def green_stats(path):
    """(count, centroid_x, centroid_y) of the flat-green override pixels."""
    import numpy as np
    ia = load(path)
    green = (ia[:, :, 1] > ia[:, :, 0] + 40) & (ia[:, :, 1] > ia[:, :, 2] + 40)
    n = int(green.sum())
    if n == 0:
        return 0, -1.0, -1.0
    ys, xs = np.nonzero(green)
    return n, float(xs.mean()), float(ys.mean())

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("MotionTest")

        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewTrimetric()
        view.fitAll()
        # Zoom out so a +Z displacement stays in frame.
        for _ in range(4):
            view.zoomOut()
        pump()
        base = cap("m_base")

        # ---- flat FS with STOCK vertex stage = reference ----
        prog = doc.addObject("App::ShaderProgram", "MotionProg")
        prog.FragmentProgram = FLAT_FS
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "None"
        ap = doc.addObject("App::ShaderBinding", "Look")
        ap.ElementList = [sh, box]
        doc.recompute()
        settle()
        stockvs = cap("m_stockvs")
        n, cx, cy = green_stats(stockvs)
        log("flat-FS/stock-VS green px: %d centroid (%.1f, %.1f)"
            % (n, cx, cy))
        ok = n > 500
        log("ASSERT flat-fs-applied: %s" % ("PASS" if ok else "FAIL"))

        # ---- same FS + custom stock-equivalent VS ----
        prog.addProperty("App::PropertyVector", "Param_Offset")
        prog.Param_Offset = (0.0, 0.0, 0.0)
        prog.VertexProgram = OFFSET_VS
        doc.recompute()
        settle()
        ident = cap("m_vs_identity")
        n = changed_count(stockvs, ident)
        log("custom-VS identity changed px vs stock-VS (tol 3): %d" % n)
        log("ASSERT vs-identity: %s" % ("PASS" if n == 0 else "FAIL"))

        # ---- render-state override (fc_state): additive blend ----
        # Additive adds the fragment onto the background, so the box
        # region keeps the background's red component (a flat opaque
        # green write leaves red near 0).
        import numpy as np
        prog.Blend = "Additive"
        prog.DepthWrite = False
        doc.recompute()
        settle()
        add = cap("m_additive")
        d = changed_count(ident, add)
        ia, ib = load(ident), load(add)
        green = (ia[:, :, 1] > ia[:, :, 0] + 40) \
            & (ia[:, :, 1] > ia[:, :, 2] + 40)
        red_flat = float(ia[:, :, 0][green].mean())
        red_add = float(ib[:, :, 0][green].mean())
        log("additive changed px: %d; box-region mean red flat=%.1f "
            "additive=%.1f" % (d, red_flat, red_add))
        ok = d > 500 and red_add > red_flat + 15
        log("ASSERT blend-additive: %s" % ("PASS" if ok else "FAIL"))
        prog.Blend = "Default"
        prog.DepthWrite = True
        doc.recompute()
        settle()
        ok = byte_equal(ident, cap("m_blend_off"), "blend restore")
        log("ASSERT blend-restore: %s" % ("PASS" if ok else "FAIL"))

        # ---- displacement through the VS ----
        prog.Param_Offset = (0.0, 0.0, 12.0)
        doc.recompute()
        settle()
        disp = cap("m_vs_displaced")
        n2, cx2, cy2 = green_stats(disp)
        n1, cx1, cy1 = green_stats(ident)
        log("displaced green px: %d centroid (%.1f, %.1f); undisplaced "
            "%d (%.1f, %.1f)" % (n2, cx2, cy2, n1, cx1, cy1))
        moved = n2 > 500 and abs(cx2 - cx1) + abs(cy2 - cy1) > 8.0
        log("ASSERT vs-displace-moves: %s" % ("PASS" if moved else "FAIL"))
        if n1 > 0:
            log("displaced/undisplaced px ratio: %.3f (big loss => "
                "prepass depth culling suspect)" % (float(n2) / n1))

        # ---- motion: sweep the offset, frames must keep changing ----
        prev = disp
        distinct = 0
        for i, z in enumerate((4.0, 8.0)):
            prog.Param_Offset = (0.0, 0.0, z)
            doc.recompute()
            settle()
            frame = cap("m_step%d" % i)
            d = changed_count(prev, frame)
            log("motion step z=%.0f changed px vs prev: %d" % (z, d))
            if d > 200:
                distinct += 1
            prev = frame
        log("ASSERT vs-motion: %s"
            % ("PASS" if distinct == 2 else "FAIL"))

        # ---- engine time uniform (u_fcTime) drives motion ----
        prog.VertexProgram = TIME_VS
        doc.recompute()
        settle()
        t0 = cap("m_time0")
        time.sleep(0.7)
        pump(3)
        t1 = cap("m_time1")
        d = changed_count(t0, t1)
        log("u_fcTime frames 0.7s apart changed px: %d" % d)
        log("ASSERT time-animates: %s" % ("PASS" if d > 200 else "FAIL"))

        # animated user shader keeps the redraw loop alive: with NO
        # forced view.redraw(), the viewer must self-schedule frames
        # (render -> animating() -> scheduleRedraw)
        for _ in range(5):
            time.sleep(0.1)
            FreeCADGui.updateGui()
        s0 = cap("m_self0")
        for _ in range(7):
            time.sleep(0.1)
            FreeCADGui.updateGui()
        s1 = cap("m_self1")
        d = changed_count(s0, s1)
        log("self-scheduled frames 0.7s apart changed px: %d" % d)
        log("ASSERT self-animating: %s" % ("PASS" if d > 200 else "FAIL"))

        # freeze-frame pins the clock: consecutive captures byte-equal
        view.RenderDebug_FreezeFrame = True
        settle(5)
        f0 = cap("m_freeze0")
        time.sleep(0.7)
        pump(3)
        f1 = cap("m_freeze1")
        ok = byte_equal(f0, f1, "frozen time captures")
        log("ASSERT time-freeze-deterministic: %s" % ("PASS" if ok else "FAIL"))
        view.RenderDebug_FreezeFrame = False
        pump(3)

        # ---- deletion restores base ----
        # (hide would blank the box too: an Appearance is a LinkGroup,
        # so a direct model-object target is a claimed group child and
        # hides with it; deletion is the Object-scope teardown, matching
        # user_shader_instancing.py)
        doc.removeObject(ap.Name)
        settle()
        hid = cap("m_removed")
        ok = byte_equal(base, hid, "motion delete restore")
        log("ASSERT motion-delete-restore: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
