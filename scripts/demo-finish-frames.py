"""Surface finish projection frames (docs/ShapeAppearanceDesign.md 9, rung 3).

Rungs 1 and 2 gave a face a finish. This one gives it a FRAME: the
pattern is laid out in the surface's own coordinates -- a plane's axes,
or the axis a cylinder or cone was turned about, read off the OCCT
surface at tessellation time -- instead of being projected triplanarly
off the object-space normal.

Four turned parts, each showing something triplanar could not do:

  A  straight knurl on a cylinder   grooves run ALONG the axis.
                                    Triplanar gave circumferential ones
                                    (recorded as the known defect in 9.8).
  B  diamond knurl on a cylinder    what a knurled handle looks like, and
                                    the seam closes: the period is snapped
                                    to a whole number of cycles round the
                                    circumference, as real knurling tooling
                                    is chosen to do.
  C  turned face, axis OFF-CENTRE   the shape is built 25 mm from its own
                                    origin, so triplanar centres the feed
                                    marks on the origin and the frame
                                    centres them on the axis. The sharpest
                                    of the four: run it with FRAMES_OFF=1
                                    and the rings walk off the disc.
  D  straight knurl on a cone       the pattern follows the taper.

KEY: FRAMES_OFF=1 is the one-variable control beside the showcase: it
clears the frame palette off the render material and leaves everything
else standing, so the pair of pictures differs in the frames and in
nothing else. A demo that varies everything cannot localise a fault
(9.9).

Usage: FreeCAD scripts/demo-finish-frames.py      (GUI or xvfb)
Env:   FRAMES_DOC   save path (default
                    data/examples/render/finish-frames.FCStd)
       FRAMES_SHOT  screenshot path (optional, saveRenderDump)
       FRAMES_EXIT  "1" = exit after save/shot (for scripted runs)
       FRAMES_OFF   "1" = clear the frames, i.e. the pre-rung-3 picture
       FRAMES_DUMP  "1" = print the frame palette and index array the
                    view provider built (SoFCRenderMaterial)
       FRAMES_PBR   "0" = Phong headlight instead of PBR + IBL, which is
                    what makes a FLAT face show its relief (part C)
       FRAMES_METAL / FRAMES_ROUGH / FRAMES_ENV
                    material and environment staging
"""
import os, time, traceback
import FreeCAD, FreeCADGui, Part
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")
# Matcap paints one procedural studio material over the whole scene,
# which is exactly the material a finish is not (9.8 / 8.5).
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetBool(
    "Matcap", False)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("FRAMES_DOC", os.path.join(
    REPO, "data", "examples", "render", "finish-frames.FCStd"))
SHOT = os.environ.get("FRAMES_SHOT", "")
EXIT = os.environ.get("FRAMES_EXIT", "") == "1"
OFF = os.environ.get("FRAMES_OFF", "") == "1"
DUMP = os.environ.get("FRAMES_DUMP", "") == "1"

# A real PBR metal: metallic 1 is what steel IS, and the roughness is
# where a machined finish lives or dies. 9.8's warning stands -- at
# roughness 0.25 and below a flat face saturates against the studio
# environment and the highlight swallows the relief -- so this sits at a
# satin-machined 0.34, and both are env-overridable for a sweep.
METAL = float(os.environ.get("FRAMES_METAL", "1.0"))
ROUGH = float(os.environ.get("FRAMES_ROUGH", "0.34"))
# A metal has no diffuse term, so ALL of its brightness is reflected
# environment -- and this renderer's specular IBL carries visibly less
# energy than its diffuse irradiance does, so a metallic 1 part reads
# far darker than a dielectric of the same albedo under the same sky.
# Until that is chased down, the honest staging is to turn the light up
# rather than to turn the metal off.
ENVI = float(os.environ.get("FRAMES_ENV", "3.0"))
# WARNING: a FLAT face cannot show its relief under a smooth sky. Both
# the diffuse and the specular response of a plane vary only with the
# normal, and a small perturbation of it still lands on much the same
# environment -- so the caps here read as plain however deep the pattern
# is cut. A headlight is the opposite: N.L changes directly with the
# perturbed normal, so FRAMES_PBR=0 is the staging that shows a planar
# frame (part C), where PBR metal is the one that shows the curved ones.
PBR = os.environ.get("FRAMES_PBR", "1") != "0"

RADIUS = 11.0
HEIGHT = 46.0
PITCH = 34.0
# The axis of part C sits this far from its own shape origin, which is
# what makes the difference between the two projections a picture rather
# than an argument.
OFFCENTRE = 25.0
# The base colour of a metal is its REFLECTANCE, not a diffuse tint:
# this is steel's.
STEEL = (0.56, 0.57, 0.58)


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def label(doc, name, text, pos, size=14):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = [text]
    ann.Position = pos
    ann.ViewObject.FontSize = size
    ann.ViewObject.TextColor = (0.88, 0.88, 0.88)
    return ann


def render_material(vo):
    """The SoFCRenderMaterial under a view provider, or None.

    A fork node pivy has no wrapper for, so the search is by type NAME
    and the fields are read generically. (Coin's own nodes register
    without the So prefix; only the fork's carry it.)
    """
    def walk(node):
        if node.getTypeId().getName() == "SoFCRenderMaterial":
            return node
        children = getattr(node, "getChildren", None)
        kids = children() if children else None
        if kids:
            for i in range(kids.getLength()):
                found = walk(kids[i])
                if found:
                    return found
        return None

    return walk(vo.RootNode)


def dump_frames(vo, name):
    node = render_material(vo)
    if not node:
        FreeCAD.Console.PrintMessage("FRAMES %s: no SoFCRenderMaterial\n" % name)
        return
    palette = node.getField("framePalette")
    indices = node.getField("frameIndices")
    kinds = {0.0: "unframed", 1.0: "planar", 2.0: "radial"}
    entries = []
    for i in range(0, palette.getNum(), 3):
        origin = palette[i].getValue()
        axis = palette[i + 1].getValue()
        xdir = palette[i + 2].getValue()
        entries.append("%d:%s origin=(%.1f,%.1f,%.1f) axis=(%.2f,%.2f,%.2f)"
                       " r=%.1f xdir=(%.2f,%.2f,%.2f)"
                       % ((i // 3, kinds.get(origin[3], "?"))
                          + tuple(origin[:3]) + tuple(axis[:3])
                          + (axis[3],) + tuple(xdir[:3])))
    idx = [indices[i] for i in range(indices.getNum())]
    FreeCAD.Console.PrintMessage(
        "FRAMES %s: %d entries %s indices=%s\n"
        % (name, len(entries), entries, idx))


def clear_frames(vo, name):
    """The control: the same scene with no frames, i.e. triplanar."""
    node = render_material(vo)
    if not node:
        return
    node.getField("framePalette").setNum(0)
    node.getField("frameIndices").setNum(0)
    FreeCAD.Console.PrintMessage("FRAMES %s: frames cleared\n" % name)


def steel(vo, finish=None, faces=None, shape=None):
    """Machined steel: a real PBR metal, at a roughness that still reads.

    finish = (pattern, pitch, depth, angle) applied to the whole object;
    faces = {face index: the same tuple} for a per-face appearance.
    """
    def material(spec):
        mat = FreeCAD.Material()
        mat.DiffuseColor = STEEL
        mat.SpecularColor = (0.95, 0.95, 0.95)
        mat.Shininess = 0.7
        if spec:
            mat.Finish, mat.FinishPitch, mat.FinishDepth, mat.FinishAngle = spec
        return mat

    if faces:
        vo.ShapeAppearance = [material(faces.get(i))
                              for i in range(len(shape.Faces))]
    else:
        vo.ShapeAppearance = [material(finish)]
    vo.addProperty("App::PropertyFloat", "Render_Metallic")
    vo.addProperty("App::PropertyFloat", "Render_Roughness")
    vo.Render_Metallic = METAL
    vo.Render_Roughness = ROUGH


def feature(doc, name, shape, x, tilt=0.0):
    obj = doc.addObject("Part::Feature", name)
    obj.Shape = shape
    # A tilt is staging, not geometry: a FLAT metal face reflecting a
    # smooth sky shows almost nothing of its relief, because a small
    # change of normal lands on much the same sky (9.8). Turned edge-on
    # to the camera its reflection sweeps the horizon instead, and the
    # feed marks appear. The frame travels in the shape's own
    # coordinates, so turning the object cannot move the pattern.
    obj.Placement = FreeCAD.Placement(
        FreeCAD.Vector(x, 0.0, 0.0),
        FreeCAD.Rotation(FreeCAD.Vector(0, 1, 0), tilt))
    doc.recompute()
    return obj


def topmost_face(shape):
    """The index of the face whose centre sits highest.

    By POSITION, never by OCCT's face numbering: a picture read against
    a numbering nobody can see proves nothing (9.9).
    """
    best, bestz = 0, -1e30
    for i, face in enumerate(shape.Faces):
        z = face.CenterOfMass.z
        if z > bestz:
            best, bestz = i, z
    return best


def build():
    try:
        doc = FreeCAD.newDocument("FinishFrames")
        origin = FreeCAD.Vector(0, 0, 0)
        up = FreeCAD.Vector(0, 0, 1)

        # A: straight knurl -- grooves must run ALONG the axis.
        a = feature(doc, "KnurlStraight",
                    Part.makeCylinder(RADIUS, HEIGHT, origin, up), 0.0)
        steel(a.ViewObject, finish=("knurl-straight", 2.2, 0.55, 0.0))

        # B: diamond knurl -- the handle, and the closing seam.
        b = feature(doc, "KnurlDiamond",
                    Part.makeCylinder(RADIUS, HEIGHT, origin, up), PITCH)
        steel(b.ViewObject, finish=("knurl", 2.8, 0.80, 0.0))

        # C: the axis is 25 mm from the shape's OWN origin, so where the
        # turning marks centre is the whole question. Per-face: only the
        # top face is faced, the side is left plain so nothing else in
        # the picture can be mistaken for the answer.
        cshape = Part.makeCylinder(
            RADIUS * 1.5, HEIGHT * 0.35,
            FreeCAD.Vector(OFFCENTRE, 0.0, 0.0), up)
        c = feature(doc, "TurnedFace", cshape, PITCH * 2.0 - OFFCENTRE,
                    tilt=52.0)
        steel(c.ViewObject, shape=cshape,
              faces={topmost_face(cshape): (
                  os.environ.get("FRAMES_CFINISH", "turned"), 2.2, 0.50, 0.0)})

        # D: a cone -- the pattern follows the taper, its period fixed by
        # the radius at the middle of the face.
        d = feature(doc, "KnurlCone",
                    Part.makeCone(RADIUS * 1.35, RADIUS * 0.45, HEIGHT,
                                  origin, up), PITCH * 3.0)
        steel(d.ViewObject, finish=("knurl-straight", 2.2, 0.55, 0.0))

        parts = [(a, "straight knurl: along the axis"),
                 (b, "diamond knurl: the seam closes"),
                 (c, "turned face: centred on the axis"),
                 (d, "knurled cone: follows the taper")]
        for i, (obj, text) in enumerate(parts):
            # Well below the row and stepped back in depth, so no
            # label lands on a part or on another label once
            # perspective has had its way with them.
            label(doc, "Label%d" % i, text,
                  FreeCAD.Vector(obj.Placement.Base.x - 16.0, 26.0 * i,
                                 -26.0 - 13.0 * i), 10)
        label(doc, "LabelTitle",
              "frames off (triplanar)" if OFF
              else "the finish laid out in each face's own frame",
              FreeCAD.Vector(-12.0, 0.0, HEIGHT + 22.0), 16)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()

        for obj, _ in parts:
            if DUMP:
                dump_frames(obj.ViewObject, obj.Name)
            if OFF:
                clear_frames(obj.ViewObject, obj.Name)

        # A machined finish barely reads under a single headlight.
        view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = PBR
        # The IBL environment as the visible background, the way
        # demo-pbr.py stages metal. It is not decoration: a metal has no
        # diffuse term, so everything it shows is the environment it
        # reflects -- against a flat gradient a metallic 1 surface reads
        # as a black solid, and the relief with it.
        view.addProperty("App::PropertyBool", "Render_PBREnvBackground")
        view.Render_PBREnvBackground = PBR
        view.addProperty("App::PropertyFloat", "Render_PBREnvIntensity")
        view.Render_PBREnvIntensity = ENVI

        view.setCameraType("Perspective")
        view.viewAxonometric()
        view.fitAll()
        # fitAll frames generously and the pattern is what this picture
        # is about, so pull the camera in along the direction the fit
        # just set -- after it, never before, or the staging race
        # reframes the scene (docs/RenderDebug.md).
        cam = view.getCameraNode()
        pos = FreeCAD.Vector(*cam.position.getValue().getValue())
        rot = FreeCAD.Rotation(*cam.orientation.getValue().getValue())
        far = cam.focalDistance.getValue()
        direction = rot.multVec(FreeCAD.Vector(0, 0, -1))
        focal = pos + direction * far
        near = far * 0.46
        cam.position.setValue(*(focal - direction * near))
        cam.focalDistance.setValue(near)
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("finish frames saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("finish frames shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("finish frames FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
