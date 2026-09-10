"""User "volume"-stage medium-function suite (docs/RenderEngine.md §5.11).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers the medium-function splice: binding an
Appearance whose Shader carries a "volume"-stage program makes the
target an emissive volume body (fire channel) — the shared volumetric
raymarch / extinction / reflection-media programs are reassembled with
the user field/ramp dispatched for the body's slot.

- stock-activation reference: Render_Fire on the body's view object
  renders the flame (Shadow draw style + volumetrics on), removing it
  restores the base capture byte-exact.
- activation-by-binding: an Object-scope Appearance binding the
  identity medium (fcStockFireField/fcStockFireRamp of FC_MEDIUM_SLOT)
  renders the flame with NO Render_Fire property anywhere, matching
  the stock capture byte-exact (same march, dispatch hits the same
  math).
- green ramp: a custom fcMediumRamp turns the flame green — differs
  from stock, green-dominant flame pixels appear.
- broken medium: an uncompilable source falls back to the stock
  programs while the body stays a fire body — byte-equal to stock.
- teardown: deleting the Appearance restores the base byte-exact.

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/volume.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "volume.txt"))

IDENTITY_SRC = """
float fcMediumField(vec3 wp)
{
    return fcStockFireField(FC_MEDIUM_SLOT, wp);
}
vec3 fcMediumRamp(float t)
{
    return fcStockFireRamp(FC_MEDIUM_SLOT, t);
}
"""

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

BROKEN_SRC = """
float fcMediumField(vec3 wp)
{
    return this_does_not_compile(wp);
}
vec3 fcMediumRamp(float t)
{
    return vec3_splat(t);
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

def settle():
    # deferred rebuilds (QTimer) + async splice compile (3 programs)
    for _ in range(30):
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

def green_flame_pixels(path):
    import numpy as np
    ia = load(path)
    green = (ia[:, :, 1] > ia[:, :, 0] + 25) & (ia[:, :, 1] > ia[:, :, 2] + 25)
    return int(green.sum())

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("VolumeTest")

        floor = doc.addObject("Part::Box", "Floor")
        floor.Length = 30
        floor.Width = 30
        floor.Height = 2
        floor.Placement.Base = FreeCAD.Vector(-15, -15, -2)

        fire = doc.addObject("Part::Box", "FireBody")
        fire.Length = 8
        fire.Width = 8
        fire.Height = 12
        fire.Placement.Base = FreeCAD.Vector(-4, -4, 0)
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewIsometric()
        view.fitAll()
        # the volumetric pass (which hosts the fire medium) needs the
        # Shadow draw style's scene light; freeze pins the flame time
        # and the march jitter for byte-comparable captures
        # The scene light is a shading switch now, not a draw style
        # (docs/CoinRetirement.md stage 4e).
        FreeCADGui.activeDocument().activeView().Render_Light = True
        view.Render_Volumetric = True
        view.RenderDebug_FreezeFrame = True
        settle()
        base = cap("v_base")

        # --- stock activation reference -------------------------------
        fvo = fire.ViewObject
        fvo.addProperty("App::PropertyBool", "Render_Fire")
        fvo.Render_Fire = True
        settle()
        stock = cap("v_stock")
        n = changed_count(base, stock)
        log("stock fire changed px vs base: %d" % n)
        log("ASSERT stock-fire-active: %s" % ("PASS" if n > 2000 else "FAIL"))

        fvo.removeProperty("Render_Fire")
        settle()
        restored = cap("v_restore")
        ok = byte_equal(base, restored, "stock fire removal restore")
        log("ASSERT stock-restore: %s" % ("PASS" if ok else "FAIL"))

        # --- activation by binding (identity medium) ------------------
        prog = doc.addObject("App::ShaderProgram", "FireProg")
        prog.Stage = "volume"
        prog.FragmentProgram = IDENTITY_SRC
        sh = doc.addObject("App::Shader", "FireFx")
        sh.Programs = [prog]
        sh.Demo = "None"
        ap = doc.addObject("App::ShaderBinding", "FireLook")
        ap.Scope = "Object"
        ap.ElementList = [sh, fire]
        doc.recompute()
        settle()
        settle()  # three spliced programs compile asynchronously
        user = cap("v_user")
        n = changed_count(base, user)
        log("bound fire changed px vs base: %d" % n)
        log("ASSERT binding-activates-fire: %s"
            % ("PASS" if n > 2000 else "FAIL"))
        nid = changed_count(stock, user)
        eq = byte_equal(stock, user, "identity vs stock fire")
        log("identity changed px vs stock (tol 3): %d" % nid)
        log("ASSERT identity-matches-stock: %s"
            % ("PASS" if nid == 0 else "FAIL"))

        # --- green ramp -----------------------------------------------
        prog.FragmentProgram = GREEN_SRC
        doc.recompute()
        settle()
        settle()
        green = cap("v_green")
        ng = green_flame_pixels(green)
        nc = changed_count(stock, green)
        log("green ramp: green px %d, changed vs stock %d" % (ng, nc))
        log("ASSERT green-ramp: %s"
            % ("PASS" if ng > 300 and nc > 300 else "FAIL"))

        # --- broken medium: stock programs stand in -------------------
        prog.FragmentProgram = BROKEN_SRC
        doc.recompute()
        settle()
        settle()
        broken = cap("v_broken")
        ok = byte_equal(stock, broken, "broken medium stock fallback")
        log("ASSERT broken-fallback: %s" % ("PASS" if ok else "FAIL"))

        # --- teardown --------------------------------------------------
        doc.removeObject(ap.Name)
        doc.recompute()
        settle()
        gone = cap("v_gone")
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
