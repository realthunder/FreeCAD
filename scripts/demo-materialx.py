"""MaterialX material balls (docs/CyclesIntegration.md sec 8 item 15).

A grid of spheres, each one shaded by a MaterialX document EMBEDDED in
the saved file: the `.mtlx` text rides an `App::ShaderProgram` with
`Dialect = MATERIALX`, an `App::Shader` groups it, and an
`App::Appearance` with Object scope binds it to one ball. Nothing points
at a file on disk, so the `.FCStd` carries every material in it and
opens the same anywhere.

The documents come from `scripts/materialx/` (see the README there for
where each one is from and why these ones). They are all imageless, so
they render in the bgfx rasterizer as well as in the Cycles path tracer
-- an image bound to a user shader is engine work the raster half has
not done yet (sec 6.10).

The first ball is the one to poke at: `fc_declared_interface.mtlx`
declares five inputs on its node graph, and FreeCAD materializes each as
a `Param_*` property on its ShaderProgram (sec 6.11). Select
`Prog_fc_declared_interface` in the tree, drag "Stripe scale" in the
property editor, and the ball follows -- no shader is regenerated, the
value is a uniform. The script prints which balls carry parameters.

Usage: FreeCAD scripts/demo-materialx.py       (GUI or xvfb)
Env:   MTLX_DOC   save path (default data/examples/render/materialx-showcase.FCStd)
       MTLX_SHOT  screenshot path (optional, saveRenderDump)
       MTLX_EXIT  "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists: the MaterialX splice only
# happens in the bgfx renderer, which only exists in render-cache mode 3.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "scripts", "materialx")
DOC = os.environ.get("MTLX_DOC", os.path.join(
    REPO, "data", "examples", "render", "materialx-showcase.FCStd"))
SHOT = os.environ.get("MTLX_SHOT", "")
EXIT = os.environ.get("MTLX_EXIT", "") == "1"

COLS = 6
RADIUS = 5.0
STEP = 21.0         # wide enough for the labels, which are screen-space
ROW_STEP = 25.0     # taller than STEP: the labels live between the rows

# Reading order, the interface showcase first. Labels are kept short
# because they are drawn in SCREEN space: a long one runs into its
# neighbour's whatever the grid spacing is. Anything else found in
# the asset directory is appended alphabetically, so dropping a .mtlx in
# there is all it takes to add a ball.
ORDER = [
    ("fc_declared_interface.mtlx", "Stripes"),
    ("open_pbr_default.mtlx", "OpenPBR"),
    ("open_pbr_carpaint.mtlx", "Car paint"),
    ("open_pbr_aluminum_brushed.mtlx", "Brushed Al"),
    ("open_pbr_pearl.mtlx", "Pearl"),
    ("open_pbr_velvet.mtlx", "Velvet"),
    ("open_pbr_honey.mtlx", "Honey"),
    ("open_pbr_ketchup.mtlx", "Ketchup"),
    ("open_pbr_glass.mtlx", "Glass"),
    ("standard_surface_gold.mtlx", "Gold"),
    ("standard_surface_copper.mtlx", "Copper"),
    ("standard_surface_chrome.mtlx", "Chrome"),
    ("standard_surface_jade.mtlx", "Jade"),
    ("standard_surface_plastic.mtlx", "Plastic"),
    ("standard_surface_thin_film.mtlx", "Thin film"),
    ("standard_surface_velvet.mtlx", "Velvet SS"),
    ("standard_surface_marble_solid.mtlx", "Marble"),
]


def say(text):
    # PrintMessage lands in the report view and print() in the Python
    # console widget -- the GUI redirects sys.stdout -- so neither
    # reaches the log a scripted run is read from. Writing the file
    # descriptor does.
    FreeCAD.Console.PrintMessage(text + "\n")
    os.write(1, (text + "\n").encode("utf-8"))


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def materials():
    """(file name, label, document text), in reading order."""
    listed = [f for f, _ in ORDER]
    extra = sorted(f for f in os.listdir(ASSETS)
                   if f.endswith(".mtlx") and f not in listed)
    out = []
    for name, label in ORDER + [(f, os.path.splitext(f)[0]) for f in extra]:
        path = os.path.join(ASSETS, name)
        if not os.path.exists(path):
            FreeCAD.Console.PrintWarning("materialx demo: missing %s\n" % path)
            continue
        with open(path, "r") as handle:
            out.append((name, label, handle.read()))
    return out


def label(doc, name, text, x, z, size=12):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = text if isinstance(text, list) else [text]
    ann.Position = FreeCAD.Vector(x, 0, z)
    ann.ViewObject.FontSize = size
    # Dark text: the PBR studio environment is drawn as the
    # background and it is a bright one.
    ann.ViewObject.TextColor = (0.12, 0.12, 0.14)
    return ann


def ball(doc, stem, text, x, z):
    """One sphere with a MaterialX document embedded on it."""
    sphere = doc.addObject("Part::Sphere", "Ball_" + stem)
    sphere.Radius = RADIUS
    sphere.Placement.Base = FreeCAD.Vector(x, 0, z)

    prog = doc.addObject("App::ShaderProgram", "Prog_" + stem)
    prog.Stage = "material"
    prog.Dialect = "MATERIALX"
    # The document itself, not a path to it: this is what makes the
    # saved file self-contained.
    prog.FragmentProgram = text

    shader = doc.addObject("App::Shader", "Fx_" + stem)
    # The preview shape would put a second object in the scene at the
    # origin; the ball IS the preview here.
    shader.Demo = "None"
    shader.Programs = [prog]

    look = doc.addObject("App::Appearance", "Look_" + stem)
    look.Scope = "Object"
    look.ElementList = [shader, sphere]
    return sphere, prog


def build():
    try:
        doc = FreeCAD.newDocument("MaterialXShowcase")
        entries = materials()
        rows = (len(entries) + COLS - 1) // COLS
        span_x = (COLS - 1) * STEP
        report = []

        for i, (name, text_label, text) in enumerate(entries):
            stem = os.path.splitext(name)[0]
            col = i % COLS
            row = i // COLS
            x = col * STEP
            z = (rows - 1 - row) * ROW_STEP
            sphere, prog = ball(doc, stem, text, x, z)
            sphere.Label = text_label
            # Annotation text is drawn in screen space from its
            # anchor, so it is nudged left by its own length to sit
            # roughly under the ball rather than off to one side.
            label(doc, "Text_" + stem, text_label,
                  x - RADIUS - 0.30 * len(text_label), z - RADIUS - 4.0)
            report.append((text_label, prog))

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()

        # The user programs compile asynchronously and the stock program
        # stands in until they land, so the first frames are the plain
        # spheres. A generous settle before the screenshot.
        settle(40)

        # Every ball's parameters, which is the point of the first one.
        for text_label, prog in report:
            params = [n for n in prog.PropertiesList if n.startswith("Param_")]
            say("materialx demo: %-22s %s"
                % (text_label,
                   ", ".join(p[len("Param_"):] for p in params) if params
                   else "(no declared interface)"))

        # PBR shading with the image-based studio environment drawn as
        # the background, so the metals have something to mirror.
        view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = True
        view.addProperty("App::PropertyBool", "Render_PBREnvBackground")
        view.Render_PBREnvBackground = True
        view.setCameraType("Perspective")
        view.viewFront()
        view.fitAll()
        cam = view.getCameraNode()
        cx = span_x / 2.0
        cz = ((rows - 1) * ROW_STEP) / 2.0
        dist = max(90.0, span_x * 1.15)
        cam.position.setValue(cx, -dist, cz)
        cam.focalDistance.setValue(dist)
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        say("materialx showcase saved: %s (%d materials, %.0f kB)"
            % (DOC, len(entries), os.path.getsize(DOC) / 1024.0))
        if SHOT:
            view.saveRenderDump(SHOT)
            say("materialx showcase shot: %s" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("materialx showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
