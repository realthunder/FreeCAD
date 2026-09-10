"""MaterialX material balls (docs/CyclesIntegration.md sec 8 item 15).

A grid of spheres, each one shaded by a MaterialX document EMBEDDED in
the saved file: the `.mtlx` text rides an `App::ShaderProgram` with
`Dialect = MATERIALX`, an `App::Shader` groups it, and an
`App::ShaderBinding` with Object scope binds it to one ball. Nothing points
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

`MTLX_CYCLES=1` renders the same frame with the Cycles path tracer as
well. It goes through the VIEWPORT rather than a standalone render, so
the capture is the ordinary 3D view with the path-traced image blitted
into it -- the same frame, the same chrome, the labels included -- and
the two shots can be held side by side. It needs a build with
BUILD_CYCLES; without one it says so and the raster shot still stands.

Usage: FreeCAD scripts/demo-materialx.py       (GUI or xvfb)
Env:   MTLX_DOC     save path (default data/examples/render/materialx-showcase.FCStd)
       MTLX_SHOT    screenshot path (optional, saveRenderDump)
       MTLX_CYCLES  "1" = also render the frame with Cycles
       MTLX_CYCLES_SHOT  where that goes (default: MTLX_SHOT with a
                         "-cycles" suffix, else beside the document)
       MTLX_CYCLES_DEV   "CPU" (default) or "CUDA"
       MTLX_CYCLES_SPP   samples, default 128
       MTLX_EXIT    "1" = exit after save/shot (for scripted runs)
"""
import os, time, traceback
import xml.etree.ElementTree as ET
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
CYCLES = os.environ.get("MTLX_CYCLES", "") == "1"
CYCLES_DEV = os.environ.get("MTLX_CYCLES_DEV", "CPU")
CYCLES_SPP = int(os.environ.get("MTLX_CYCLES_SPP", "128"))
CYCLES_SHOT = os.environ.get("MTLX_CYCLES_SHOT", "") or (
    os.path.splitext(SHOT)[0] + "-cycles.png" if SHOT
    else os.path.splitext(DOC)[0] + "-cycles.png")
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


def glass_from_document(text):
    """What a document says about being SEE-THROUGH, in the terms the
    rasterizer's glass pass takes.

    A MaterialX document states transmission on its surface, and the
    raster half cannot render that from the material: refraction is the
    glass pass's business (docs/CyclesIntegration.md sec 6.10), and the
    pass is driven by Render_Glass* view properties on the object. So
    the document is read once here and the properties are set from it --
    the bridge the engine does not make on its own.

    None when the document is opaque. Only values stated directly on
    the surface node are read: a transmission driven by a pattern graph
    has no single number to hand a pass that takes one.
    """
    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        return None
    surface = None
    for node in root:
        if node.get("type") == "surfaceshader":
            surface = node
            break
    if surface is None:
        return None
    stated = {}
    for child in surface:
        if child.tag == "input" and child.get("value") is not None:
            stated[child.get("name")] = child.get("value")

    def number(name, fallback):
        try:
            return float(stated[name])
        except (KeyError, ValueError):
            return fallback

    def colour(name):
        try:
            parts = [float(v) for v in stated[name].split(",")]
        except (KeyError, ValueError):
            return None
        return tuple(parts[:3]) if len(parts) >= 3 else None

    if number("transmission_weight", 0.0) <= 0.0:
        return None
    # OpenPBR states the tint as a colour reached at a DEPTH; the pass
    # states it as a Beer-Lambert density over the object colour, the
    # same sigma = (1 - colour) * density the path tracer integrates.
    # One over the depth is the honest reading of "reached at", and it
    # is an approximation either way -- the two engines agree on the
    # colour, not on the falloff.
    depth = number("transmission_depth", 0.0)
    return {
        "ior": number("specular_ior", 1.5),
        "roughness": number("specular_roughness", 0.0),
        "density": (1.0 / depth) if depth > 0.0 else 0.0,
        "tint": colour("transmission_color") if depth > 0.0 else None,
    }


def cycles_shot(view, width, height):
    """The same frame, path traced, captured from the view itself.

    cyclesRender() would write the path-traced image alone; the
    viewport blits it into the ordinary 3D view instead, so saving the
    view gives the frame WITH the annotation labels and the rest of the
    chrome, which is what makes it comparable to the raster shot.
    """
    try:
        view.cyclesViewport(True, device=CYCLES_DEV, samples=CYCLES_SPP,
                            denoise=True)
    except Exception as exc:
        say("materialx demo: no Cycles in this build (%s)" % (exc,))
        return
    # It renders progressively into the view, so the capture waits for
    # the sample budget rather than for a call to return.
    deadline = time.time() + 900
    progress = None
    while time.time() < deadline:
        pump(3)
        time.sleep(0.2)
        status = view.cyclesViewportStatus()
        if not status:
            continue
        if status["error"]:
            say("materialx demo: cycles viewport error: %s" % status["error"])
            return
        if status["progress"] != progress:
            progress = status["progress"]
            say("materialx demo: cycles %.0f%% (%s)"
                % (100.0 * progress, status["status"]))
        if progress is not None and progress >= 0.999:
            break
    pump(6)
    view.saveImage(CYCLES_SHOT, width, height, "Current")
    status = view.cyclesViewportStatus() or {}
    say("materialx showcase cycles shot: %s (%s, %d spp, %s shaders)"
        % (CYCLES_SHOT, CYCLES_DEV, CYCLES_SPP, status.get("shaders")))


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
    # Shaded, not the default Flat Lines: a material ball is the
    # surface, and the sphere's seam and pole edges drawn over it are
    # nothing the material has to say.
    sphere.ViewObject.DisplayMode = "Shaded"

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

    look = doc.addObject("App::ShaderBinding", "Look_" + stem)
    look.Scope = "Object"
    look.ElementList = [shader, sphere]

    glass = glass_from_document(text)
    if glass:
        vo = sphere.ViewObject
        for kind, name, value in (
                ("App::PropertyBool", "Render_Glass", True),
                ("App::PropertyFloat", "Render_GlassIOR", glass["ior"]),
                ("App::PropertyFloat", "Render_GlassRoughness",
                 glass["roughness"]),
                ("App::PropertyFloat", "Render_GlassDensity",
                 glass["density"])):
            if not hasattr(vo, name):
                vo.addProperty(kind, name, "Render")
            setattr(vo, name, value)
        # The pass takes its absorption tint from the object colour,
        # which is where the document's transmission colour lands.
        if glass["tint"]:
            vo.ShapeColor = glass["tint"]
        say("materialx demo: %-22s glass ior=%.3f density=%.2f"
            % (stem, glass["ior"], glass["density"]))
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
        # Studio -- four soft boxes on a dark surround, the product-shot
        # rig. The default Gradient is the flattest of the six on
        # purpose (it stays out of the way of a model being worked on),
        # and flat is the one thing a material ball cannot afford: a
        # surround with no bright sources and no edges puts the same
        # grey on every roughness, which is what made physically based
        # shading look like painted plastic in the first place.
        view.Render_PBREnvPreset = "Studio"
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
        if CYCLES:
            width, height = view.getSize()
            cycles_shot(view, width, height)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("materialx showcase FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
