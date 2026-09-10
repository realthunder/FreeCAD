"""Appearance Scope-mode suite (docs/RenderDebug.md §6.5).

In-FreeCAD driver run by user-shader-verify.sh (desktop leg) under xvfb;
creates its own document. Covers the LinkGroup-based Appearance target
semantics:

Scope="Instance" (suffix-anchored chain override): a Link target child
resolving A1.A2.Box registers the chain [A1, A2, Box]; the shader must
land on every occurrence whose resolved chain ends in that chain —
the plain A1 assembly, A1 under a deeper parent (Assembly3.LinkA1) and
a subname shortcut link (Link001 -> A1."Assembly2.Box.") — but NOT on
Assembly4's A2.Box (anchor mismatch). The Appearance's own target-link
instance also carries the effect (verified by the occurrence log, kept
out of the asserted rows here).

Scope="Object" (direct attachment, merge-down): targeting the A2
group shades Box in every occurrence that renders through A2's real
node — including Assembly4's — while the subname shortcut link, whose
scene graph bypasses A2's root (tail snapshot + baked transform), stays
stock.

The four assembly roots sit at distinct X; assertions locate each box
as a column cluster of the base capture (mid-height rows only, which
excludes the navicube and the appearance's own instance parked high on
Z) instead of trusting fitAll's framing.

A trailing Scope="Instance" leg binds a particle-only effect (no main
program) to the same chain: occurrence-fit emitter seeds must appear
on every matched occurrence and nowhere else, and deletion must
restore byte-exact.

Env: US_OUT (output dir, default this file's dir), US_RESULT (result
file, default <US_OUT>/instancing.txt).
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

OUT = os.environ.get("US_OUT", os.path.dirname(os.path.abspath(__file__)))
RESULT = os.environ.get("US_RESULT", os.path.join(OUT, "instancing.txt"))

RED_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = vec4(1.0, 0.1, 0.1, 1.0);
}
"""

# Static green billboards inside the emitter box: deterministic (no
# u_fcTime), no travel, so EmitterMargin can stay 0 and the seeds add
# no bounds growth (no near/far shift vs the base capture).
PART_VS = """$input a_position, a_normal, a_color0
$output v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    vec4 vpos = mul(u_modelView, vec4(a_position, 1.0));
    vpos.xy += a_normal.xy * 0.8;
    gl_Position = mul(u_proj, vpos);
    v_normal = vec3(a_normal.xy, 1.0);
    v_color0 = vec4(0.1, 1.0, 0.2, 1.0);
    v_vpos = vpos.xyz;
}
"""

PART_FS = """$input v_normal, v_color0, v_vpos
#include <bgfx_shader.sh>
void main()
{
    float r = dot(v_normal.xy, v_normal.xy);
    gl_FragColor = v_color0 * max(0.0, 1.0 - r);
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

def row_window(h):
    return slice(int(h * 0.30), int(h * 0.85))

def find_clusters(base, n):
    """Column clusters of gray box faces in the base capture's mid rows."""
    import numpy as np
    ia = load(base)[row_window(load(base).shape[0])]
    face = ((np.abs(ia[:, :, 0] - ia[:, :, 1]) < 12)
            & (np.abs(ia[:, :, 1] - ia[:, :, 2]) < 12)
            & (ia[:, :, 0] > 170))
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
        doc = FreeCAD.newDocument("InstTest")

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

        # box 2: Link001 -> A1."Assembly2.Box." (subname shortcut)
        l001 = doc.addObject("App::Link", "Link001")
        l001.LinkedObject = (a1, ["Assembly2.Box."])
        l001.Placement.Base = FreeCAD.Vector(80, 0, 0)

        # box 3: Assembly4 { LinkA2 -> A2 }  (anchor-mismatch negative)
        la2 = doc.addObject("App::Link", "LinkA2")
        la2.LinkedObject = a2
        a4 = doc.addObject("App::LinkGroup", "Assembly4")
        a4.ElementList = [la2]
        a4.Placement.Base = FreeCAD.Vector(120, 0, 0)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.viewFront()
        view.fitAll()
        pump()
        base = cap("i_base")
        clusters = find_clusters(base, 4)

        prog = doc.addObject("App::ShaderProgram", "RedProg")
        prog.FragmentProgram = RED_FS
        sh = doc.addObject("App::Shader", "Fx")
        sh.Programs = [prog]
        sh.Demo = "None"

        # --- Scope=Instance: chain [A1, A2, Box] via a Link target child.
        # The target link's own instance parks high on Z, outside the
        # asserted rows.
        tgt = doc.addObject("App::Link", "TargetLink")
        tgt.LinkedObject = (a1, ["Assembly2.Box."])
        tgt.Placement.Base = FreeCAD.Vector(0, 0, 60)
        ap = doc.addObject("App::ShaderBinding", "Look")
        ap.Scope = "Instance"
        ap.ElementList = [sh, tgt]
        doc.recompute()
        settle()
        chained = cap("i_chain")
        counts = cluster_changes(base, chained, clusters)
        # A1 hit, Assembly3.LinkA1 hit, Link001 hit, Assembly4 miss
        assert_clusters("chain-suffix-anchored", counts,
                        [True, True, True, False])

        # hiding deactivates (children hide with the group), byte-exact
        ap.ViewObject.Visibility = False
        settle()
        hid = cap("i_chain_hidden")
        ok = byte_equal(base, hid, "chain hide restore")
        log("ASSERT chain-hide: %s" % ("PASS" if ok else "FAIL"))
        ap.ViewObject.Visibility = True
        settle()

        # --- Scope=Object on the A2 group: direct attach + merge-down.
        # Every occurrence rendering through A2's real root shades —
        # including Assembly4's — while the subname shortcut (Link001)
        # bypasses A2's node and stays stock. Fresh hidden link child: a
        # binding needs no visible instance.
        tgt2 = doc.addObject("App::Link", "TargetA2")
        tgt2.LinkedObject = a2
        ap.Scope = "Object"
        ap.ElementList = [sh, tgt2]
        doc.recompute()
        tgt2.ViewObject.Visibility = False
        # the unclaimed old target link keeps rendering, off the asserted
        # rows; the new hidden child adds nothing visible
        settle()
        direct = cap("i_direct")
        counts = cluster_changes(base, direct, clusters)
        assert_clusters("direct-merge-down", counts,
                        [True, True, False, True])

        # deletion restores the asserted rows byte-exact... except the
        # leftover links; drop them all and compare whole-frame
        doc.removeObject(ap.Name)
        doc.removeObject(tgt.Name)
        doc.removeObject(tgt2.Name)
        settle()
        cleared = cap("i_deleted")
        ok = byte_equal(base, cleared, "deletion restore")
        log("ASSERT delete-restore: %s" % ("PASS" if ok else "FAIL"))

        # --- Scope=Instance particle emitters: a particle-ONLY effect
        # (no main program — the rain pattern) bound to the same chain
        # must grow occurrence-fit seed billboards on every matched
        # occurrence and nowhere else.
        progp = doc.addObject("App::ShaderProgram", "PartProg")
        progp.Stage = "particle"
        progp.VertexProgram = PART_VS
        progp.FragmentProgram = PART_FS
        progp.Blend = "Additive"
        progp.DepthWrite = False
        progp.EmitterCount = 250
        progp.EmitterSeed = 7
        # hover above the box top: seeds inside the solid would be
        # depth-occluded to edge slivers from the front view
        progp.EmitterSpread = FreeCAD.Vector(0.9, 0.9, 0.3)
        progp.EmitterOffset = FreeCAD.Vector(0.0, 0.0, 0.7)
        progp.EmitterMargin = 0.0
        shp = doc.addObject("App::Shader", "PartFx")
        shp.Programs = [progp]
        shp.Demo = "None"
        tgt3 = doc.addObject("App::Link", "TargetLink2")
        tgt3.LinkedObject = (a1, ["Assembly2.Box."])
        tgt3.Placement.Base = FreeCAD.Vector(0, 0, 60)
        app = doc.addObject("App::ShaderBinding", "PartLook")
        app.Scope = "Instance"
        app.ElementList = [shp, tgt3]
        doc.recompute()
        settle()
        part = cap("i_particles")
        counts = cluster_changes(base, part, clusters)
        assert_clusters("instance-particles", counts,
                        [True, True, True, False])

        doc.removeObject(app.Name)
        doc.removeObject(tgt3.Name)
        settle()
        partoff = cap("i_particles_off")
        ok = byte_equal(base, partoff, "particle delete restore")
        log("ASSERT particle-delete-restore: %s" % ("PASS" if ok else "FAIL"))

        log("DONE")
    except Exception:
        traceback.print_exc()
        log("EXCEPTION")
    finally:
        with open(RESULT, "w") as f:
            f.write("\n".join(results) + "\n")
        os._exit(0)

QTimer.singleShot(1000, run)
