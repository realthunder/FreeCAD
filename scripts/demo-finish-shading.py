"""Every surface finish under BOTH shading models (docs/ShapeAppearanceDesign.md 9).

`demo-finish.py` shows what the five `App::SurfaceFinish` patterns are.
This one shows what they cost the two shading models the engine has:
the same scene is captured twice, once with `Render_PBR` on and once
off, and nothing else differs between the two pictures. A finish is a
specular effect -- it perturbs the shading normal and coarsens the
highlight -- so it is exactly the kind of authoring whose look is not
portable between a metallic/roughness BRDF with an environment and a
single-lobe Blinn-Phong under a headlight.

The chart, six columns by two rows:

  columns   none (the control), knurl, knurl-straight, brushed,
            blasted, turned. The control column is what makes the rest
            readable: whatever a column does that `none` does not is
            the finish, in that shading model.
  top row   CYLINDERS carrying the finish through the `Render_Finish*`
            view properties at the pattern's own default pitch -- the
            size that finish really has on a part, which at a
            whole-part camera distance is mostly finer than a pixel.
            What survives there is the roughness the filter converts
            the unresolved relief into, and BOTH models take it: the
            Phong path converts its shininess to a roughness for the
            finish and back again (`fc_mesh_fs.sh`), so a finish
            coarsens a Phong highlight exactly as it coarsens a PBR
            one.
  bottom    PLATES carrying it on the appearance itself
            (`App.Material` Finish/FinishPitch/FinishDepth/FinishAngle)
            at an exaggerated pitch, so the relief is resolved and the
            pattern geometry itself is visible.

Both legs are stated, not inherited. Each part carries a Phong
specular/shininess AND a `Render_Metallic`/`Render_Roughness` pair
computed to be the same surface: the shininess is the exact inverse of
the shader's own shininess<->roughness fit, so neither model is being
shown a material tuned for the other.

Usage: FreeCAD scripts/demo-finish-shading.py     (GUI or xvfb)
       Runs persistently; SHADING_EXIT=1 to close after the shots.
Env:   SHADING_DOC    save path (default
                      data/examples/render/finish-shading.FCStd)
       SHADING_SHOT   screenshot path; the mode is inserted before the
                      extension, so shot.png writes shot-pbr.png and
                      shot-phong.png
       SHADING_EXIT   "1" = exit after save/shots (for scripted runs)
       SHADING_PBR    "0" = leave the view in Phong at the end
                      (default 1 = PBR), which is also the single leg
                      an interactive run starts in
       SHADING_METAL / SHADING_ROUGH
                      the surface, default 0.85 / 0.35
       SHADING_ENV    Render_PBREnvIntensity, default 1. The scene
                      ambient reaches the PBR branch now, so a metal
                      no longer needs this cranked to read -- and
                      cranking it flattens the relief this chart is
                      about.
"""
import os, math, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")
# Matcap paints one procedural studio material over the whole scene,
# which is exactly the material a finish is not: state the preference
# this scene depends on rather than inheriting it.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetBool(
    "Matcap", False)
# The viewer light rig this chart is lit by, stated for the same reason.
# One headlight, no fill, and the ambient at its default 0.2 -- which
# the PBR branch takes as a uniform-radiance environment and Phong adds
# outright, so both legs see the same room.
_VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
_VIEW.SetBool("EnableHeadlight", True)
_VIEW.SetInt("HeadlightIntensity", 100)
_VIEW.SetBool("EnableBacklight", False)
_VIEW.SetBool("EnableFillLight", False)
_VIEW.SetUnsigned("AmbientLightColor", 0xFFFFFFFF)
_VIEW.SetInt("AmbientLightIntensity", 20)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("SHADING_DOC", os.path.join(
    REPO, "data", "examples", "render", "finish-shading.FCStd"))
SHOT = os.environ.get("SHADING_SHOT", "")
EXIT = os.environ.get("SHADING_EXIT", "") == "1"
PBR = os.environ.get("SHADING_PBR", "1") == "1"
METAL = float(os.environ.get("SHADING_METAL", "0.85"))
ROUGH = float(os.environ.get("SHADING_ROUGH", "0.35"))
ENVI = float(os.environ.get("SHADING_ENV", "1.0"))

# name, exaggerated pitch (mm), exaggerated depth (mm), lay angle (deg).
# "" is the control: the same steel with no finish at all.
PATTERNS = [
    ("",               0.0, 0.00, 0.0),
    ("knurl",          2.5, 0.75, 0.0),
    ("knurl-straight", 2.0, 0.60, 0.0),
    ("brushed",        1.2, 0.25, 90.0),
    ("blasted",        1.0, 0.18, 0.0),
    ("turned",         1.5, 0.25, 0.0),
]

STEP = 60.0         # column spacing: wide enough for the longest label
RADIUS = 11.0
HEIGHT = 34.0
PLATE = 34.0        # plate width and height
THICK = 6.0
STEEL = (0.62, 0.64, 0.67)


def shininess_for(roughness):
    """The Phong shininess that means this roughness.

    The inverse of the fit `fc_mesh_fs.sh` uses when it hands a Phong
    material to the finish code: rough = sqrt(2 / (shininess*128 + 2)).
    Going through it here is what makes the two legs the same surface
    rather than two guesses.
    """
    r = max(min(roughness, 1.0), 1.0e-3)
    return max(min((2.0 / (r * r) - 2.0) / 128.0, 1.0), 0.0)


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def label(doc, name, text, x, z, size=15):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = [text]
    ann.Position = FreeCAD.Vector(x, 0, z)
    ann.ViewObject.FontSize = size
    ann.ViewObject.TextColor = (0.88, 0.88, 0.88)
    return ann


def steel(vo, metallic, roughness):
    """One surface, stated to both shading models.

    Render_Metallic/Render_Roughness are what the metallic/roughness
    branch reads; the appearance's specular colour and shininess are
    what Blinn-Phong reads. Setting only one of the two would leave the
    other model showing whatever the default happened to be, and the
    A/B would be measuring that instead of the finish.
    """
    vo.ShapeColor = STEEL
    vo.addProperty("App::PropertyFloat", "Render_Metallic")
    vo.addProperty("App::PropertyFloat", "Render_Roughness")
    vo.Render_Metallic = metallic
    vo.Render_Roughness = roughness
    mat = vo.ShapeAppearance[0]
    mat.DiffuseColor = STEEL
    # The Phong specular colour is the same F0 the PBR branch computes,
    # mix(0.04, base, metallic): a metal tints what it reflects, a
    # dielectric reflects a few percent of white. Handing Phong a
    # near-white specular instead -- the obvious reading of "not a
    # metal" -- saturates every flat face to paper white and swallows
    # the relief, which is the whole subject of the picture.
    f0 = tuple(0.04 * (1.0 - metallic) + STEEL[i] * metallic
               for i in range(3))
    mat.SpecularColor = f0
    mat.Shininess = shininess_for(roughness)
    vo.ShapeAppearance = mat
    return mat


def finish_on_appearance(vo, name, pitch, depth, angle):
    mat = vo.ShapeAppearance[0]
    mat.Finish = name
    mat.FinishPitch = pitch
    mat.FinishDepth = depth
    mat.FinishAngle = angle
    vo.ShapeAppearance = mat


def set_mode(view, pbr):
    view.Render_PBR = pbr
    pump(6)


def shot_path(mode):
    root, ext = os.path.splitext(SHOT)
    return "%s-%s%s" % (root, mode, ext or ".png")


def build():
    try:
        doc = FreeCAD.newDocument("FinishShading")

        for col, (name, pitch, depth, angle) in enumerate(PATTERNS):
            x = col * STEP

            # Top row: the finish through the Render_Finish* knobs at
            # the pattern's own default size (pitch/depth left at 0).
            cyl = doc.addObject("Part::Cylinder", "Real_%d" % col)
            cyl.Radius = RADIUS
            cyl.Height = HEIGHT
            cyl.Placement.Base = FreeCAD.Vector(x, 0, 26.0)
            vo = cyl.ViewObject
            steel(vo, METAL, ROUGH)
            if name:
                vo.addProperty("App::PropertyString", "Render_Finish")
                vo.Render_Finish = name

            # Bottom row: the finish on the appearance, exaggerated so
            # the relief resolves. A plate standing in the XZ plane, so
            # a front view sees the pattern face on.
            box = doc.addObject("Part::Box", "Shown_%d" % col)
            box.Length = PLATE
            box.Width = THICK
            box.Height = PLATE
            box.Placement.Base = FreeCAD.Vector(x - PLATE / 2.0,
                                                -THICK / 2.0, -50.0)
            vo = box.ViewObject
            # Duller and less metallic than the cylinders: a near-mirror
            # flat face saturates against the environment and a
            # saturated highlight swallows the relief, which is the one
            # thing the bottom row exists to show.
            steel(vo, 0.15, 0.50)
            if name:
                finish_on_appearance(vo, name, pitch, depth, angle)

            # An annotation anchors its text at the point, so centring a
            # column label means stepping back by half its own width.
            # 1.35 mm per character is what font size 15 measures at
            # this camera distance.
            text = name or "none (control)"
            label(doc, "Label_%d" % col, text, x - 1.35 * len(text), -62.0)

        # Row captions centred over the chart rather than off its left
        # edge, where the camera framing below would cut them.
        cx = (len(PATTERNS) - 1) * STEP / 2.0
        label(doc, "LabelReal", "as machined (default pitch)",
              cx - 1.5 * len("as machined (default pitch)"), 70.0, 17)
        label(doc, "LabelShown", "pattern exaggerated",
              cx - 1.5 * len("pattern exaggerated"), -6.0, 17)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()

        view.addProperty("App::PropertyBool", "Render_PBR")
        view.addProperty("App::PropertyFloat", "Render_PBREnvIntensity")
        view.Render_PBREnvIntensity = ENVI

        view.setCameraType("Perspective")
        view.viewFront()
        view.fitAll()
        # fitAll leaves generous margins; pull straight back from the
        # chart centre instead, keeping the front orientation -- after
        # the fit, never before, or the staging race reframes it.
        # The chart runs from the column labels at z = -62 to the top
        # caption at z = 70, so its centre is a little above zero.
        dist = 300.0
        cam = view.getCameraNode()
        cam.position.setValue(cx, -dist, 4.0)
        cam.focalDistance.setValue(dist)
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("finish shading saved: %s\n" % DOC)

        if SHOT:
            # The A/B: one variable, the shading model. The camera is
            # already pinned, so the two frames differ in nothing else.
            for mode, pbr in (("pbr", True), ("phong", False)):
                set_mode(view, pbr)
                settle(8)
                path = shot_path(mode)
                view.saveRenderDump(path)
                FreeCAD.Console.PrintMessage(
                    "finish shading shot (%s): %s\n" % (mode, path))

        set_mode(view, PBR)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("finish shading FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
