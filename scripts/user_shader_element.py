"""Appearance Scope="Element" suite (docs/RenderDebug.md §6.5).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers face-level shader overrides:

- A target link whose subname ends in a face element (A1."Assembly2.
  Box.Face6") shades ONLY that face of every occurrence whose resolved
  chain ends in [A1, A2, Box] — same suffix-anchored matching as
  Scope="Instance" (Assembly4's A2.Box stays stock), but the per-cluster
  changed-pixel counts must be strictly smaller than the whole-occurrence
  counts of the same chain (the other faces stay stock).
- Element and whole-occurrence bindings coexist: a second Appearance
  shading the whole occurrence red keeps the element binding's green
  face on top (element refines whole).
- A nonexistent element (Face99) binds nothing (warn + skip).
- Hiding the Appearance restores the base capture byte-exact.

The assembly roots sit at distinct X; assertions locate each box as a
column cluster of the base capture (mid-height rows only, excluding the
navicube and the target links' own instances parked high on Z).

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/element.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "element.txt"))

RED_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = vec4(1.0, 0.1, 0.1, 1.0);
}
"""

GREEN_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = vec4(0.1, 1.0, 0.1, 1.0);
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

def row_window(h):
    return slice(int(h * 0.30), int(h * 0.85))

def find_clusters(base, n):
    """Column clusters of gray box faces in the base capture's mid rows."""
    import numpy as np
    ia = load(base)[row_window(load(base).shape[0])]
    # the flat-lit iso faces of the default doc render at gray 135
    face = ((np.abs(ia[:, :, 0] - ia[:, :, 1]) < 12)
            & (np.abs(ia[:, :, 1] - ia[:, :, 2]) < 12)
            & (ia[:, :, 0] > 125))
    cols = face.sum(axis=0) >= 5
    clusters = []
    start = None
    for i, c in enumerate(list(cols) + [False]):
        if c and start is None:
            start = i
        elif not c and start is not None:
            if clusters and start - clusters[-1][1] < 15:
                clusters[-1][1] = i
            else:
                clusters.append([start, i])
            start = None
    log("clusters: %s" % clusters)
    if len(clusters) != n:
        raise RuntimeError("expected %d box clusters, found %s"
                           % (n, clusters))
    return clusters

def cluster_changes(base, img, clusters):
    """Changed-pixel count vs base per cluster (mid rows, +-8 px pad)."""
    import numpy as np
    ia, ib = load(base), load(img)
    rw = row_window(ia.shape[0])
    changed = np.any(np.abs(ia[rw] - ib[rw]) > 3, axis=2)
    return [int(changed[:, max(0, c[0] - 8):c[1] + 8].sum())
            for c in clusters]

def cluster_color_counts(img, clusters, idx):
    """(reddish, greenish) pixel counts inside cluster idx (mid rows)."""
    import numpy as np
    ia = load(img)[row_window(load(img).shape[0])]
    c = clusters[idx]
    win = ia[:, max(0, c[0] - 8):c[1] + 8]
    red = int(((win[:, :, 0] > 150) & (win[:, :, 1] < 90)
               & (win[:, :, 2] < 90)).sum())
    green = int(((win[:, :, 1] > 150) & (win[:, :, 0] < 90)
                 & (win[:, :, 2] < 90)).sum())
    return red, green

def assert_clusters(tag, counts, expect):
    ok = True
    for i, (n, e) in enumerate(zip(counts, expect)):
        good = (n > 200) if e else (n == 0)
        ok = ok and good
        log("  box%d(%s): %d px %s" % (
            i, "hit" if e else "miss", n, "ok" if good else "BAD"))
    log("ASSERT %s: %s" % (tag, "PASS" if ok else "FAIL"))

def byte_equal(a, b, label):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        eq = f1.read() == f2.read()
    log("%s: %s" % (label, "BYTE-EQUAL" if eq else "DIFFERS"))
    return eq

def run():
    try:
        from PySide.QtGui import QGuiApplication
        log("platform=" + QGuiApplication.platformName())
        doc = FreeCAD.newDocument("ElemTest")

        # box 0: A1 = Assembly1 { Assembly2 { Box } }
        box = doc.addObject("Part::Box", "Box")
        a2 = doc.addObject("App::LinkGroup", "Assembly2")
        a2.ElementList = [box]
        a1 = doc.addObject("App::LinkGroup", "Assembly1")
        a1.ElementList = [a2]

        # box 1: Assembly3 { LinkA1 -> A1 }  (deeper parent)
        la1 = doc.addObject("App::Link", "LinkA1")
        la1.LinkedObject = a1
        a3 = doc.addObject("App::LinkGroup", "Assembly3")
        a3.ElementList = [la1]
        a3.Placement.Base = FreeCAD.Vector(40, 0, 0)

        # box 2: Assembly4 { LinkA2 -> A2 }  (anchor-mismatch negative)
        la2 = doc.addObject("App::Link", "LinkA2")
        la2.LinkedObject = a2
        a4 = doc.addObject("App::LinkGroup", "Assembly4")
        a4.ElementList = [la2]
        a4.Placement.Base = FreeCAD.Vector(80, 0, 0)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        # isometric: three faces visible, so a one-face override must
        # change strictly fewer pixels than the whole occurrence
        view.viewIsometric()
        view.fitAll()
        pump()
        base = cap("e_base")
        clusters = find_clusters(base, 3)

        prog = doc.addObject("App::ShaderProgram", "GreenProg")
        prog.FragmentProgram = GREEN_FS
        sh = doc.addObject("App::Shader", "FxGreen")
        sh.Programs = [prog]
        sh.Demo = "None"

        # --- Scope=Element: chain [A1, A2, Box] + Face6 (top face).
        # The target link's own instance parks high on Z, outside the
        # asserted rows.
        tgt = doc.addObject("App::Link", "TargetLink")
        tgt.LinkedObject = (a1, ["Assembly2.Box.Face6"])
        tgt.Placement.Base = FreeCAD.Vector(0, 0, 60)
        ap = doc.addObject("App::ShaderBinding", "FaceLook")
        ap.Scope = "Element"
        ap.ElementList = [sh, tgt]
        doc.recompute()
        settle()
        elem = cap("e_face")
        ecounts = cluster_changes(base, elem, clusters)
        # A1 hit, Assembly3.LinkA1 hit, Assembly4 miss
        assert_clusters("element-suffix-anchored", ecounts,
                        [True, True, False])

        # --- Same chain as whole occurrences: Scope=Instance ignores the
        # element ref, so per-cluster changes must strictly exceed the
        # one-face counts (the other faces shade too).
        ap.Scope = "Instance"
        settle()
        whole = cap("e_whole")
        wcounts = cluster_changes(base, whole, clusters)
        ok = all(w > e + 200 for w, e in zip(wcounts[:2], ecounts[:2]))
        log("  whole vs element: %s vs %s" % (wcounts, ecounts))
        log("ASSERT element-subset-of-whole: %s" % ("PASS" if ok else "FAIL"))
        ap.Scope = "Element"
        settle()

        # --- Coexistence: a second Appearance shades the whole
        # occurrence red; the green Face6 override must stay on top.
        prog2 = doc.addObject("App::ShaderProgram", "RedProg")
        prog2.FragmentProgram = RED_FS
        sh2 = doc.addObject("App::Shader", "FxRed")
        sh2.Programs = [prog2]
        sh2.Demo = "None"
        tgt2 = doc.addObject("App::Link", "TargetWhole")
        tgt2.LinkedObject = (a1, ["Assembly2.Box."])
        tgt2.Placement.Base = FreeCAD.Vector(0, 0, 120)
        ap2 = doc.addObject("App::ShaderBinding", "WholeLook")
        ap2.Scope = "Instance"
        ap2.ElementList = [sh2, tgt2]
        doc.recompute()
        settle()
        both = cap("e_both")
        red, green = cluster_color_counts(both, clusters, 0)
        ok = red > 200 and green > 200
        log("  cluster0 red=%d green=%d" % (red, green))
        log("ASSERT element-over-whole: %s" % ("PASS" if ok else "FAIL"))
        doc.removeObject(ap2.Name)
        doc.removeObject(tgt2.Name)
        settle()

        # --- Nonexistent element: warn + bind nothing.
        tgt.LinkedObject = None
        tgt.LinkedObject = (a1, ["Assembly2.Box.Face99"])
        doc.recompute()
        settle()
        none = cap("e_badface")
        ncounts = cluster_changes(base, none, clusters)
        assert_clusters("bad-element-binds-nothing", ncounts,
                        [False, False, False])
        tgt.LinkedObject = None
        tgt.LinkedObject = (a1, ["Assembly2.Box.Face6"])
        doc.recompute()
        settle()

        # --- Hiding deactivates, byte-exact restore.
        ap.ViewObject.Visibility = False
        settle()
        hid = cap("e_hidden")
        ok = byte_equal(base, hid, "element hide restore")
        log("ASSERT element-hide: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
