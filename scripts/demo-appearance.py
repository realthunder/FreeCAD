"""Every appearance preset on one sphere chart.

Reads the shipped Appearance library
(src/Mod/Material/Resources/Materials/Appearance/*.FCMat) straight off
disk and puts one sphere per preset on a slab, so the whole table can be
judged in a single frame under whichever shading model the view is set
to. The presets are data, so an edit to a .FCMat shows up on the next
run with nothing rebuilt -- which is what makes this the A/B harness for
retuning them.

The nine metals were re-authored on measured F0 and are the control:
if they read wrong, the pipeline moved, not the table.

Usage: FreeCAD scripts/demo-appearance.py            (GUI or xvfb)
Env:   AP_DOC    save path (default data/examples/render/appearance-chart.FCStd)
       AP_SHOT   screenshot path (optional, saveRenderDump)
       AP_PBR    "0" = classic Phong shading instead of PBR (default "1")
       AP_ENVBG  "0" = keep the gradient background (default "1")
       AP_ENV    environment image (.hdr/.exr/.png) replacing the built-in
                 procedural studio; empty = procedural
       AP_EXPOSURE  frame exposure (default 1.0; an HDR environment wants
                 well under one)
       AP_ENVINT tune the environment lighting brightness (default 1.0)
       AP_AO     "1" = ambient occlusion on (default follows the preference)
       AP_EXIT   "1" = exit after save/shot (for scripted runs)
"""
import os, re, time, glob, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")
# The cube's pixels have faked a pass before now; it has no business in
# a material chart.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
    "ShowNaviCube", False)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESETS = os.path.join(REPO, "src", "Mod", "Material", "Resources",
                       "Materials", "Appearance")
DOC = os.environ.get("AP_DOC", os.path.join(
    REPO, "data", "examples", "render", "appearance-chart.FCStd"))
SHOT = os.environ.get("AP_SHOT", "")
PBR = os.environ.get("AP_PBR", "1") != "0"
ENVBG = os.environ.get("AP_ENVBG", "1") != "0"
ENV = os.environ.get("AP_ENV", "")
EXPOSURE = float(os.environ.get("AP_EXPOSURE", "1.0"))
ENVINT = float(os.environ.get("AP_ENVINT", "1.0"))
AO = os.environ.get("AP_AO", "")
EXIT = os.environ.get("AP_EXIT", "") == "1"

COLS = 5
RADIUS = 4.0
STEP = 13.0
SLAB_OVERHANG = 0.6 * STEP   # slab margin around the sphere grid


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def read_preset(path):
    """The four BasicRendering values this chart shades with."""
    text = open(path, encoding="utf-8").read()

    def color(key, fallback=(0.0, 0.0, 0.0)):
        m = re.search(key + r':\s*"\(([^)]*)\)"', text)
        if not m:
            return fallback
        return tuple(float(x) for x in m.group(1).split(",")[:3])

    m = re.search(r'Shininess:\s*"([\d.]+)"', text)
    return {
        "diffuse": color("DiffuseColor", (0.8, 0.8, 0.8)),
        "specular": color("SpecularColor"),
        "ambient": color("AmbientColor"),
        "emissive": color("EmissiveColor"),
        "shininess": float(m.group(1)) if m else 0.2,
    }


def label(doc, name, text, x, z, size=11):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = [text]
    ann.Position = FreeCAD.Vector(x, 0, z)
    ann.ViewObject.FontSize = size
    ann.ViewObject.TextColor = (0.85, 0.85, 0.85)
    return ann


def build():
    try:
        names = sorted(os.path.basename(p)[:-6]
                       for p in glob.glob(os.path.join(PRESETS, "*.FCMat")))
        if not names:
            raise RuntimeError("no presets under %s" % PRESETS)
        rows = (len(names) + COLS - 1) // COLS

        doc = FreeCAD.newDocument("AppearanceChart")

        for i, name in enumerate(names):
            preset = read_preset(os.path.join(PRESETS, name + ".FCMat"))
            col, row = i % COLS, i // COLS
            x = col * STEP
            z = (rows - 1 - row) * STEP

            sph = doc.addObject("Part::Sphere",
                                "S%02d_%s" % (i, re.sub(r"\W", "", name)))
            sph.Radius = RADIUS
            sph.Placement.Base = FreeCAD.Vector(x, 0, z)
            vo = sph.ViewObject
            # ShapeColor and the appearance's diffuse are the same
            # channel; set the colour first so the material assignment
            # below is what survives.
            vo.ShapeColor = preset["diffuse"]
            mat = vo.ShapeAppearance[0]
            mat.DiffuseColor = preset["diffuse"]
            mat.SpecularColor = preset["specular"]
            mat.AmbientColor = preset["ambient"]
            mat.EmissiveColor = preset["emissive"]
            mat.Shininess = preset["shininess"]
            vo.ShapeAppearance = mat

            label(doc, "L%02d" % i, name, x - RADIUS - 1.5,
                  z - RADIUS - 3.4, size=9)

        # A slab under the spheres: contact darkening, cast shadows and
        # the grazing reflection of a dielectric all need something for
        # the spheres to sit ON. Neutral mid grey, deliberately matte.
        span_x = (COLS - 1) * STEP
        span_z = (rows - 1) * STEP
        slab = doc.addObject("Part::Box", "Slab")
        slab.Length = span_x + 2 * SLAB_OVERHANG + 2 * RADIUS
        slab.Width = 4 * RADIUS
        slab.Height = 2 * RADIUS
        # Top face exactly tangent to the bottom row, so the spheres
        # REST on it -- a floating sphere has no contact shading to read.
        slab.Placement.Base = FreeCAD.Vector(
            -RADIUS - SLAB_OVERHANG, -2 * RADIUS, -3 * RADIUS)
        svo = slab.ViewObject
        svo.ShapeColor = (0.35, 0.35, 0.36)
        smat = svo.ShapeAppearance[0]
        smat.DiffuseColor = (0.35, 0.35, 0.36)
        smat.SpecularColor = (0.22, 0.22, 0.22)   # sRGB(0.04) dielectric F0
        smat.Shininess = 0.08
        svo.ShapeAppearance = smat

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()

        view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = PBR
        view.addProperty("App::PropertyBool", "Render_PBREnvBackground")
        view.Render_PBREnvBackground = ENVBG
        if ENV:
            view.addProperty("App::PropertyFile", "Render_PBREnvImage")
            view.Render_PBREnvImage = ENV
        view.addProperty("App::PropertyFloat", "Render_Exposure")
        view.Render_Exposure = EXPOSURE
        view.addProperty("App::PropertyFloat", "Render_PBREnvIntensity")
        view.Render_PBREnvIntensity = ENVINT
        if AO:
            # On the VIEW, which outranks the preference every probe
            # would otherwise inherit from the real user.cfg.
            view.addProperty("App::PropertyBool", "Render_AO")
            view.Render_AO = AO != "0"

        # Pinned, never fitAll: a staged A/B reframes on the smallest
        # scene change otherwise, and the diff then reads as a material
        # difference (docs/RenderDebug.md; render-ab-harness).
        view.setCameraType("Perspective")
        view.viewFront()
        cam = view.getCameraNode()
        dist = 118.0
        cam.position.setValue(span_x / 2.0, -dist, span_z / 2.0 - 2.0)
        cam.focalDistance.setValue(dist)
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("appearance chart saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("appearance chart shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("appearance chart FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
