"""Property-bound user shader parameter suite (docs/RenderDebug.md §6.4).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers: an App::ShaderProgram Param_* dynamic
property drives the like-named uniform on the consuming draw; live edit;
byte-exact restore; a dynamic property outside the Param group does NOT
bind; App::ShaderBinding per-binding override of a like-named property;
override-only (appended) uniform; dynamic property removal restoring the
program value; two Appearances of one shader with distinct overrides on
distinct instances (per-binding proof).

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/params.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "params.txt"))

TINT_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
uniform vec4 u_Tint;
uniform vec4 u_Boost;
void main()
{
    gl_FragColor = vec4(u_Tint.rgb + u_Boost.rgb, 1.0);
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

def wait_compile():
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

def diff_stats(a, b, label):
    import numpy as np
    ia, ib = load(a), load(b)
    changed = np.any(np.abs(ia - ib) > 3, axis=2)
    n = int(changed.sum())
    if n:
        mean_new = ib[changed].mean(axis=0)
        log("%s: %d px changed, mean new color RGB=(%.0f,%.0f,%.0f)"
            % (label, n, mean_new[0], mean_new[1], mean_new[2]))
    else:
        log("%s: no change beyond tolerance" % label)
    return n

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("ParamTest")
        box = doc.addObject("Part::Box", "Box")
        ball = doc.addObject("Part::Sphere", "Ball")
        ball.Placement.Base = FreeCAD.Vector(30, 0, 0)
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewAxonometric()
        view.fitAll()
        pump()
        base = cap("p_base")

        prog = doc.addObject("App::ShaderProgram", "TintProg")
        prog.FragmentProgram = TINT_FS
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "None"
        ap = doc.addObject("App::ShaderBinding", "Look")
        ap.ElementList = [sh, box]  # child 0 = effect, rest = targets
        doc.recompute()
        wait_compile()
        cap("p_attached")  # uniforms unset -> zero-filled, not asserted

        # 1. program parameter property drives the uniform
        prog.addProperty("App::PropertyColor", "Param_Tint")
        prog.Param_Tint = (1.0, 0.0, 0.0)
        pump()
        red = cap("p_red")
        n = diff_stats(base, red, "base->red (program Param_Tint)")
        log("ASSERT program-param-lands: %s" % ("PASS" if n > 500 else "FAIL"))

        # 2. live edit + byte-exact restore
        prog.Param_Tint = (0.0, 1.0, 0.0)
        pump()
        green = cap("p_green")
        n = diff_stats(red, green, "red->green (live edit)")
        log("ASSERT live-edit: %s" % ("PASS" if n > 500 else "FAIL"))
        prog.Param_Tint = (1.0, 0.0, 0.0)
        pump()
        red2 = cap("p_red2")
        ok = byte_equal(red, red2, "restore red")
        log("ASSERT restore: %s" % ("PASS" if ok else "FAIL"))

        # 2b. a dynamic property outside the Param group must NOT bind
        prog.addProperty("App::PropertyColor", "Other_Tint")
        prog.Other_Tint = (0.0, 0.0, 1.0)
        pump()
        time.sleep(0.3)
        pump()
        noother = cap("p_nonparam")
        ok = byte_equal(red, noother, "non-Param group ignored")
        log("ASSERT non-param-ignored: %s" % ("PASS" if ok else "FAIL"))

        # 3. Appearance-appended uniform (u_Boost has no program property)
        ap.addProperty("App::PropertyColor", "Param_Boost")
        ap.Param_Boost = (0.0, 0.0, 1.0)
        pump()
        boost = cap("p_boost")
        n = diff_stats(red, boost, "red->boost (appearance-only u_Boost)")
        log("ASSERT appearance-append: %s" % ("PASS" if n > 500 else "FAIL"))

        # 4. Appearance override of the like-named program parameter
        ap.addProperty("App::PropertyColor", "Param_Tint")
        ap.Param_Tint = (0.0, 1.0, 0.0)
        pump()
        ovr = cap("p_override")
        n = diff_stats(boost, ovr, "boost->override (appearance Param_Tint)")
        log("ASSERT appearance-override: %s" % ("PASS" if n > 500 else "FAIL"))

        # 5. removing the overrides restores the program value (deferred
        # resync through the event loop)
        ap.removeProperty("Param_Tint")
        ap.removeProperty("Param_Boost")
        pump()
        time.sleep(0.3)
        pump()
        removed = cap("p_removed")
        ok = byte_equal(red, removed, "override removal restore")
        log("ASSERT removal-restore: %s" % ("PASS" if ok else "FAIL"))

        # 6. per-binding: second Appearance, same shader, own override
        ap2 = doc.addObject("App::ShaderBinding", "Look2")
        ap2.ElementList = [sh, ball]
        ap2.addProperty("App::PropertyColor", "Param_Tint")
        ap2.Param_Tint = (1.0, 1.0, 0.0)
        doc.recompute()
        pump()
        time.sleep(0.3)
        pump()
        two = cap("p_two")
        n = diff_stats(red, two, "red->two (ball binding, yellow override)")
        log("ASSERT per-binding: %s" % ("PASS" if n > 500 else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
