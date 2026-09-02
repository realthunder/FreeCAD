"""User material-shader lighting helper suite (docs/RenderDebug.md §6.5).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers fc_user_lighting.sh, the shipped
include exposing the stock CAD-mesh lighting core to user material
shaders:

- Identity: a user shader whose main() is exactly
  fcLightFragment(fcStockBase(), v_normal, v_vpos) must reproduce the
  stock rendering — the whole-object override capture matches the base
  capture on every face-interior pixel. Pixels within a few px of the
  base's dark edge lines are excluded: the override rides the selection
  channel and draws its edge lines after all triangles, while the scene
  path lets coincident faces eat into edge AA — a line-parity artifact
  of any non-on-top whole-object selection, not a shading difference.
- Lit albedo: a red-albedo fcLightFragment shader must differ from the
  base, stay red-hued, and show the headlight's per-face shading — the
  visible box faces keep >= 2 distinct brightness levels (trimetric
  view: three distinct face angles; isometric would collapse them to
  one), unlike a flat unlit color write.
- Hiding the Appearance restores the base capture byte-exact.

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/lighting.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "lighting.txt"))

IDENTITY_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
#include "fc_user_lighting.sh"
void main()
{
    gl_FragColor = fcLightFragment(fcStockBase(), v_normal, v_vpos);
}
"""

RED_LIT_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
#include "fc_user_lighting.sh"
void main()
{
    gl_FragColor = fcLightFragment(vec4(0.8, 0.1, 0.1, 1.0),
                                   v_normal, v_vpos);
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

def changed_outside_edges(a, b, tol=3, pad=3):
    """Changed pixels excluding a dilated band around base edge lines."""
    import numpy as np
    ia, ib = load(a), load(b)
    changed = np.any(np.abs(ia - ib) > tol, axis=2)
    dark = ia.max(axis=2) < 80
    for _ in range(pad):
        d = dark.copy()
        d[1:, :] |= dark[:-1, :]
        d[:-1, :] |= dark[1:, :]
        d[:, 1:] |= dark[:, :-1]
        d[:, :-1] |= dark[:, 1:]
        dark = d
    return int((changed & ~dark).sum())

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def red_face_levels(path):
    """Distinct brightness levels of red-hued pixels (lit-face count)."""
    import numpy as np
    ia = load(path)
    red = (ia[:, :, 0] > ia[:, :, 1] + 30) & (ia[:, :, 0] > ia[:, :, 2] + 30)
    if red.sum() < 200:
        return red.sum(), []
    vals = ia[:, :, 0][red]
    hist, _ = np.histogram(vals, bins=16, range=(0, 256))
    # a face is a dominant brightness cluster; edges/AA contribute noise
    levels = [i for i, c in enumerate(hist) if c > vals.size * 0.05]
    return int(red.sum()), levels

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("LightTest")

        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        # trimetric: three visible faces at three DISTINCT view angles =
        # three headlight shades (isometric collapses them to one)
        view.viewTrimetric()
        view.fitAll()
        pump()
        base = cap("l_base")

        prog = doc.addObject("App::ShaderProgram", "IdProg")
        prog.FragmentProgram = IDENTITY_FS
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "None"
        tgt = doc.addObject("App::Link", "Target")
        tgt.LinkedObject = box
        tgt.ViewObject.Visibility = False
        ap = doc.addObject("App::ShaderBinding", "Look")
        ap.Scope = "Instance"
        ap.ElementList = [sh, tgt]
        doc.recompute()
        settle()

        ident = cap("l_identity")
        n = changed_outside_edges(base, ident)
        log("identity changed px off-edge (tol 3): %d (raw %d)"
            % (n, changed_count(base, ident)))
        log("ASSERT identity-matches-stock: %s" % ("PASS" if n == 0 else "FAIL"))

        prog.FragmentProgram = RED_LIT_FS
        doc.recompute()
        settle()
        red = cap("l_red")
        n = changed_count(base, red)
        cnt, levels = red_face_levels(red)
        log("red-lit changed px: %d, red px: %d, brightness levels: %s"
            % (n, cnt, levels))
        ok = n > 200 and cnt > 200 and len(levels) >= 2
        log("ASSERT red-albedo-lit: %s" % ("PASS" if ok else "FAIL"))

        ap.ViewObject.Visibility = False
        settle()
        hid = cap("l_hidden")
        ok = byte_equal(base, hid, "lighting hide restore")
        log("ASSERT lighting-hide: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
