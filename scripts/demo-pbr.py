"""PBR material showcase (docs/RenderEngine.md §4).

Builds the classic metallic × roughness sphere chart twice: 8 rows
sweep metalness 1 → 0 top-to-bottom, 7 columns sweep roughness 0 → 1
left-to-right — a silver grid in front and a gold grid behind it
(offset half a cell so the gold spheres peek through the gaps), so
both a neutral and a tinted base color show every combination. The
view draws the IBL studio environment itself as the background
(`Render_PBREnvBackground`) with a perspective camera, so the smooth
metal rows visibly mirror their surroundings.
Per-sphere values ride the `Render_Metallic` / `Render_Roughness`
dynamic view properties (SoFCRenderMaterial capture); the view's
`Render_PBR` switches the bgfx renderer to the metallic/roughness
BRDF with image-based environment lighting. Annotation labels mark
the axes.

The view-level state (`Render_PBR`, camera) persists in the
document's <View3D> blocks, so the saved .FCStd restores fully
self-contained; only the renderer selection itself (RenderCache=3 +
Render Type "bgfx") is a user preference, not document state.

Usage: FreeCAD scripts/demo-pbr.py            (GUI or xvfb)
Env:   PBR_DOC   save path (default data/examples/render/pbr-showcase.FCStd)
       PBR_SHOT  screenshot path (optional, saveRenderDump)
       PBR_EXIT  "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("PBR_DOC", os.path.join(
    REPO, "data", "examples", "render", "pbr-showcase.FCStd"))
SHOT = os.environ.get("PBR_SHOT", "")
EXIT = os.environ.get("PBR_EXIT", "") == "1"

ROWS = 8        # metallic 1 (top) .. 0 (bottom)
COLS = 7        # roughness 0 (left) .. 1 (right)
RADIUS = 4.0
STEP = 10.0

SILVER = (0.75, 0.77, 0.80)
GOLD = (0.85, 0.70, 0.20)
BACK_GAP = 30.0     # y distance of the gold grid behind the silver one


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def label(doc, name, text, x, z, size):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = [text]
    ann.Position = FreeCAD.Vector(x, 0, z)
    ann.ViewObject.FontSize = size
    ann.ViewObject.TextColor = (0.85, 0.85, 0.85)
    return ann


def build():
    try:
        doc = FreeCAD.newDocument("PBRShowcase")

        # Both charts live in XZ planes so a front view reads them like
        # the reference: metal on top, rough to the right. The gold grid
        # sits behind the silver one, shifted half a cell up-right so
        # its spheres show through the gaps.
        for grid, color in enumerate((SILVER, GOLD)):
            y = grid * BACK_GAP
            off = grid * STEP * 0.5
            for r in range(ROWS):
                metallic = 1.0 - float(r) / (ROWS - 1)
                z = (ROWS - 1 - r) * STEP + off
                for c in range(COLS):
                    roughness = float(c) / (COLS - 1)
                    sph = doc.addObject("Part::Sphere",
                                        "Sphere_%s_m%d_r%d"
                                        % ("au" if grid else "ag", r, c))
                    sph.Radius = RADIUS
                    sph.Placement.Base = FreeCAD.Vector(
                        c * STEP + off, y, z)
                    vo = sph.ViewObject
                    vo.ShapeColor = color
                    vo.addProperty("App::PropertyFloat", "Render_Metallic")
                    vo.addProperty("App::PropertyFloat", "Render_Roughness")
                    vo.Render_Metallic = metallic
                    # roughness 0 means "automatic" to the renderer; pin
                    # the smooth column just above it
                    vo.Render_Roughness = max(0.02, roughness)

        span_x = (COLS - 1) * STEP
        span_z = (ROWS - 1) * STEP
        label(doc, "LabelMetal", "Metal",
              -7 * RADIUS, span_z + RADIUS, 24)
        label(doc, "LabelNonMetal", "Non-metal",
              -8.5 * RADIUS, RADIUS, 24)
        label(doc, "LabelSmooth", "Smooth",
              0, -3 * RADIUS, 24)
        label(doc, "LabelRough", "Rough",
              span_x - RADIUS, -3 * RADIUS, 24)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()

        # The PBR + image-based-lighting shading path, view-level
        # (addProperty is idempotent on the pre-created view props),
        # with the IBL environment drawn as the visible background —
        # a perspective camera gives it its per-pixel direction fan.
        view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = True
        view.addProperty("App::PropertyBool", "Render_PBREnvBackground")
        view.Render_PBREnvBackground = True

        view.setCameraType("Perspective")
        view.viewFront()
        view.fitAll()
        # Tight, centered framing — fitAll leaves generous margins
        # around the chart, so pull the camera straight back from the
        # combined chart center instead (front view orientation kept).
        cx = ((COLS - 1) * STEP + STEP * 0.5) / 2.0
        cz = ((ROWS - 1) * STEP + STEP * 0.5) / 2.0
        dist = 135.0
        cam = view.getCameraNode()
        cam.position.setValue(cx, -dist, cz)
        cam.focalDistance.setValue(dist + BACK_GAP / 2.0)
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("pbr showcase saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("pbr showcase shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("pbr showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
