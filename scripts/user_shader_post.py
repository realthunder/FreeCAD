"""Scene-level (post) Appearance activation suite (docs/RenderDebug.md §6.5).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers: an empty-target App::ShaderBinding
activates the Shader's post-stage programs view-wide; Param_* on the
program drives the uniform; the Appearance override wins per binding;
Visibility deactivates; a targeted Appearance does NOT apply post
programs; deleting the Appearances clears everything; two empty-target
Appearances follow TreeRank (higher/later wins, hiding the winner falls
back).

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/post.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "post.txt"))

INVERT_FS = """$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texScene, 0);
uniform vec4 u_Mix;
void main()
{
    vec4 c = texture2D(s_texScene, v_texcoord0);
    gl_FragColor = vec4(mix(c.rgb, vec3_splat(1.0) - c.rgb, u_Mix.x), c.a);
}
"""

TINT_FS = """$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texScene, 0);
uniform vec4 u_Tint;
void main()
{
    vec4 c = texture2D(s_texScene, v_texcoord0);
    gl_FragColor = vec4(c.rgb * 0.2 + u_Tint.rgb * 0.8, c.a);
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

def diff_count(a, b, label):
    import numpy as np
    ia, ib = load(a), load(b)
    changed = np.any(np.abs(ia - ib) > 3, axis=2)
    n = int(changed.sum())
    log("%s: %d px changed" % (label, n))
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
        doc = FreeCAD.newDocument("PostTest")
        doc.addObject("Part::Box", "Box")
        ball = doc.addObject("Part::Sphere", "Ball")
        ball.Placement.Base = FreeCAD.Vector(30, 0, 0)
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewAxonometric()
        view.fitAll()
        pump()
        base = cap("q_base")

        # scene-level activation: empty-target Appearance + post program
        prog = doc.addObject("App::ShaderProgram", "Invert")
        prog.Stage = "post"
        prog.FragmentProgram = INVERT_FS
        prog.addProperty("App::PropertyFloat", "Param_Mix")
        prog.Param_Mix = 1.0
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "None"
        ap = doc.addObject("App::ShaderBinding", "Look")
        ap.ElementList = [sh]  # shader-only group = scene-level post
        doc.recompute()
        wait_compile()
        inv = cap("q_invert")
        n = diff_count(base, inv, "base->invert (scene-level post)")
        log("ASSERT post-activates: %s" % ("PASS" if n > 500000 else "FAIL"))

        # program param drives the pass
        prog.Param_Mix = 0.0
        pump()
        mix0 = cap("q_mix0")
        n = diff_count(inv, mix0, "invert->mix0 (param edit)")
        log("ASSERT post-param: %s" % ("PASS" if n > 500000 else "FAIL"))
        prog.Param_Mix = 1.0
        pump()

        # appearance override wins for this binding
        ap.addProperty("App::PropertyFloat", "Param_Mix")
        ap.Param_Mix = 0.0
        pump()
        ovr = cap("q_override")
        n = diff_count(inv, ovr, "invert->override (appearance Param_Mix=0)")
        log("ASSERT post-override: %s" % ("PASS" if n > 500000 else "FAIL"))
        ap.removeProperty("Param_Mix")
        pump()
        time.sleep(0.3)
        pump()
        rest = cap("q_ovr_removed")
        ok = byte_equal(inv, rest, "override removal restore")
        log("ASSERT post-ovr-removal: %s" % ("PASS" if ok else "FAIL"))

        # visibility deactivates
        ap.ViewObject.Visibility = False
        pump()
        hid = cap("q_hidden")
        ok = byte_equal(base, hid, "hidden restores base")
        log("ASSERT post-hide: %s" % ("PASS" if ok else "FAIL"))
        ap.ViewObject.Visibility = True
        pump()

        # a targeted Appearance does not apply post programs
        ap.ElementList = [sh, ball]
        pump()
        targeted = cap("q_targeted")
        ok = byte_equal(base, targeted, "targeted appearance = no post")
        log("ASSERT post-needs-empty-targets: %s" % ("PASS" if ok else "FAIL"))
        ap.ElementList = [sh]
        pump()

        # two empty-target appearances: higher TreeRank (later) wins
        prog2 = doc.addObject("App::ShaderProgram", "Tint")
        prog2.Stage = "post"
        prog2.FragmentProgram = TINT_FS
        prog2.addProperty("App::PropertyColor", "Param_Tint")
        prog2.Param_Tint = (1.0, 0.0, 0.0)
        sh2 = doc.addObject("App::Shader", "Fx2")
        sh2.Programs = [prog2]
        sh2.Demo = "None"
        ap2 = doc.addObject("App::ShaderBinding", "Look2")
        ap2.ElementList = [sh2]
        doc.recompute()
        wait_compile()
        two = cap("q_two")
        n = diff_count(inv, two, "invert->two (later appearance wins)")
        log("ASSERT post-rank: %s" % ("PASS" if n > 500000 else "FAIL"))
        ap2.ViewObject.Visibility = False
        pump()
        back = cap("q_two_hidden")
        ok = byte_equal(inv, back, "hiding the winner falls back")
        log("ASSERT post-rank-fallback: %s" % ("PASS" if ok else "FAIL"))

        # deleting both appearances clears everything
        doc.removeObject(ap.Name)
        doc.removeObject(ap2.Name)
        pump()
        cleared = cap("q_deleted")
        ok = byte_equal(base, cleared, "deletion restores base")
        log("ASSERT post-delete-clears: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
