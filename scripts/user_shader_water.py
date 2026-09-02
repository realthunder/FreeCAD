"""User "water"-stage shader suite (docs/RenderEngine.md §5.11).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers the water stage: binding an Appearance
whose Shader carries a "water"-stage program IS the water activation —
the target becomes a water body (the engine runs its water pass set)
with the user fragment program replacing fs_fc_water.

- stock-activation reference: Render_Water on the body's view object
  changes the render (the surface pass runs), removing it restores the
  base capture byte-exact.
- surface-responds: the wave field moves pixels at all. Guards the rest
  of the suite against a surface with refraction and reflection off,
  which shades flat and passes every other assert here unchanged.
- activation-by-binding: an Object-scope Appearance binding the
  identity water shader (fcWaterFragment of fc_user_water.sh) renders
  the body as water with NO Render_Water property anywhere.
- identity-matches-stock: that capture matches the stock Render_Water
  capture on every pixel (same scene draws, same pass set, same
  shading core — only the program object differs). Captures are taken
  under RenderDebug_FreezeFrame (the water surface animates).
- tint: a variant that keeps red and quarters green and blue differs
  from stock, and its water pixels carry exactly that arithmetic. The
  check is the arithmetic and not the hue: the pool is dark blue, so a
  quarter of its blue still sits level with its red and the tinted
  water does not read as red at all.
- broken shader: an uncompilable program falls back to the stock
  surface program while the body stays a water body — byte-equal to
  the stock capture.
- teardown: deleting the Appearance restores the base capture
  byte-exact (Object scope claims the target as a group child, so
  deletion, not hiding, is the teardown).

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/water.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "water.txt"))

IDENTITY_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
#include "fc_user_water.sh"
void main()
{
    gl_FragColor = fcWaterFragment(v_normal, v_vpos);
}
"""

RED_TINT_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
#include "fc_user_water.sh"
void main()
{
    vec4 c = fcWaterFragment(v_normal, v_vpos);
    gl_FragColor = vec4(c.r, c.g * 0.25, c.b * 0.25, c.a);
}
"""

BROKEN_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = this_does_not_compile(v_vpos);
}
"""

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
_R = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
_R.SetString("Type", "bgfx - OpenGL")
# Pin what the surface responds WITH, rather than inheriting whatever
# this box last saved. With refraction and reflection both off the
# surface is a flat tinted sheet by design — every capture below still
# differs from base, so the suite goes on passing while testing a
# surface that no wave, ripple or impact ring can alter. A developer
# profile that had switched them off is what made an earlier session
# read this suite's tint failure as a renderer bug.
_R.SetBool("WaterRefraction", True)
_R.SetBool("WaterReflection", True)
_R.SetBool("WaterPlanarReflection", True)
_R.SetFloat("WaterWaveStrength", 0.3)
_R.SetInt("WaterRippleType", 0)

results = []

def log(msg):
    results.append(str(msg))
    print("US:", msg, flush=True)

def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()

def settle():
    # deferred rebuilds (QTimer) + async shader compile
    for _ in range(25):
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

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def tinted_water_pixels(a_stock, b_red):
    """Pixels carrying the tint program's own arithmetic: it keeps red
    and quarters green and blue, so a water pixel must hold its red
    still while those two land on a quarter of what stock had.

    Deliberately not "looks red": the pool is a dark blue, and a
    quarter of its blue still sits about level with its red, so a
    hue test reports failure over a capture the shader tinted
    exactly as written.
    """
    import numpy as np
    ia, ib = load(a_stock), load(b_red)
    # Enough green to quarter visibly — excludes the background and
    # anything the water does not cover.
    lit = ia[:, :, 1] > 40
    kept = np.abs(ib[:, :, 0] - ia[:, :, 0]) <= 3
    quartered = (np.abs(ib[:, :, 1] - ia[:, :, 1] * 0.25) <= 8) \
        & (np.abs(ib[:, :, 2] - ia[:, :, 2] * 0.25) <= 8)
    return int((lit & kept & quartered).sum())

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("WaterTest")

        floor = doc.addObject("Part::Box", "Floor")
        floor.Length = 30
        floor.Width = 30
        floor.Height = 2
        floor.Placement.Base = FreeCAD.Vector(-15, -15, -2)
        floor.ViewObject.ShapeColor = (0.6, 0.5, 0.4)
        pool = doc.addObject("Part::Box", "Pool")
        pool.Length = 16
        pool.Width = 16
        pool.Height = 4
        pool.Placement.Base = FreeCAD.Vector(-8, -8, 0)
        pool.ViewObject.ShapeColor = (0.15, 0.35, 0.55)
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewIsometric()
        view.fitAll()
        pump()
        # the global water-surface toggle defaults OFF; both the stock
        # Render_Water path and activation-by-binding sit behind it
        view.Render_WaterSurface = True
        # the water surface animates: pin the effect clock for
        # byte-comparable captures
        view.RenderDebug_FreezeFrame = True
        pump()
        base = cap("w_base")

        # --- stock activation reference -------------------------------
        pvo = pool.ViewObject
        pvo.addProperty("App::PropertyBool", "Render_Water")
        pvo.Render_Water = True
        settle()
        stock = cap("w_stock")
        n = changed_count(base, stock)
        log("stock water changed px vs base: %d" % n)
        log("ASSERT stock-water-active: %s" % ("PASS" if n > 2000 else "FAIL"))

        # The surface must actually RESPOND to its wave field before any
        # shader claim below means anything. With refraction and
        # reflection off the surface is a flat sheet that no normal can
        # alter, and every other assert here still passes over it — the
        # trap that made an earlier session read a bad assert as a
        # broken renderer. Rain at high strength rather than the default
        # directional waves: those are deliberately subtle and move
        # almost nothing on a pool this size.
        view.Render_WaterWaveStrength = 0.0
        settle()
        flat = cap("w_flat")
        view.Render_WaterRippleType = "Rain"
        view.Render_WaterWaveStrength = 2.0
        settle()
        rippled = cap("w_rippled")
        n = changed_count(flat, rippled)
        log("wave field changed px vs a still surface: %d" % n)
        log("ASSERT surface-responds: %s" % ("PASS" if n > 500 else "FAIL"))
        # Back to the pinned defaults; the byte-equality asserts below
        # are themselves the check that this restored exactly.
        view.Render_WaterRippleType = "Waves"
        view.Render_WaterWaveStrength = 0.3
        settle()

        pvo.removeProperty("Render_Water")
        settle()
        restored = cap("w_restore")
        ok = byte_equal(base, restored, "stock water removal restore")
        log("ASSERT stock-restore: %s" % ("PASS" if ok else "FAIL"))

        # --- activation by binding (Object scope, identity program) ---
        prog = doc.addObject("App::ShaderProgram", "WaterProg")
        prog.Stage = "water"
        prog.FragmentProgram = IDENTITY_FS
        sh = doc.addObject("App::Shader", "WaterFx")
        sh.Programs = [prog]
        sh.Demo = "None"
        ap = doc.addObject("App::ShaderBinding", "WaterLook")
        ap.Scope = "Object"
        ap.ElementList = [sh, pool]
        doc.recompute()
        settle()
        user = cap("w_user")
        n = changed_count(base, user)
        log("bound water changed px vs base: %d" % n)
        log("ASSERT binding-activates-water: %s"
            % ("PASS" if n > 2000 else "FAIL"))
        nid = changed_count(stock, user)
        eq = byte_equal(stock, user, "identity vs stock water")
        log("identity changed px vs stock (tol 3): %d" % nid)
        log("ASSERT identity-matches-stock: %s"
            % ("PASS" if nid == 0 else "FAIL"))

        # --- tinted variant -------------------------------------------
        prog.FragmentProgram = RED_TINT_FS
        doc.recompute()
        settle()
        red = cap("w_red")
        nred = tinted_water_pixels(stock, red)
        log("red-tint tinted px vs stock: %d" % nred)
        log("ASSERT tint-applies: %s" % ("PASS" if nred > 500 else "FAIL"))

        # --- broken shader: stock surface stands in -------------------
        prog.FragmentProgram = BROKEN_FS
        doc.recompute()
        settle()
        broken = cap("w_broken")
        ok = byte_equal(stock, broken, "broken shader stock fallback")
        log("ASSERT broken-fallback: %s" % ("PASS" if ok else "FAIL"))

        # --- teardown: deletion restores the base ---------------------
        doc.removeObject(ap.Name)
        doc.recompute()
        settle()
        gone = cap("w_gone")
        ok = byte_equal(base, gone, "appearance delete restore")
        log("ASSERT delete-restore: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
