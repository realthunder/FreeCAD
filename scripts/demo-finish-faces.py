"""Per-face machined surface finish (docs/ShapeAppearanceDesign.md 9, rung 2).

One block, six faces, six different finishes -- the thing rung 1 could
not do: a finish is four numbers, so a per-face one travels as a PALETTE
of the distinct finishes plus one index per face, baked into the mesh's
per-vertex material stream and resolved in the shader.

Two blocks stand side by side, both carrying the same six-entry
appearance, the second turned 180 degrees about z so that the three
faces the first one hides face the same camera.

Which finish goes on which face is decided by the face's DIRECTION
(FACES below), not by its number: a picture read against OCCT's face
numbering proves nothing to a reader who cannot see the numbering.

Usage: FreeCAD scripts/demo-finish-faces.py       (GUI or xvfb)
Env:   FACES_DOC   save path (default
                   data/examples/render/finish-faces.FCStd)
       FACES_SHOT  screenshot path (optional, saveRenderDump)
       FACES_EXIT  "1" = exit after save/shot (for scripted runs)
       FACES_ONLY  finish ONE face (top/front/right/left/back/bottom)
                   with a coarse knurl and leave the rest bare -- the
                   picture that says WHICH face an index names
       FACES_DUMP  "1" = print the palette and index array the view
                   provider built (SoFCRenderMaterial)
"""
import os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")
# Matcap shading paints one procedural studio material over the whole
# scene, which is exactly the material a finish is not: state the
# preference this scene depends on rather than inheriting it.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetBool(
    "Matcap", False)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("FACES_DOC", os.path.join(
    REPO, "data", "examples", "render", "finish-faces.FCStd"))
SHOT = os.environ.get("FACES_SHOT", "")
EXIT = os.environ.get("FACES_EXIT", "") == "1"
# Which face carries a finish by direction (top/front/right/left/
# back/bottom), "" = all six. One face alone is
# what settles WHICH face an index names: a picture of six patterns
# shows that they differ, not that each one landed where it was
# authored.
ONLY = os.environ.get("FACES_ONLY", "")
# Print the palette and per-face indices the view provider built
# (SoFCRenderMaterial), i.e. the producer's half of the mapping.
DUMP = os.environ.get("FACES_DUMP", "") == "1"

SIZE = 40.0
GAP = 26.0
STEEL = (0.62, 0.64, 0.67)

# Keyed by which way the face points, NOT by face number: which face
# Part::Box calls Face3 is OCCT's business, and a picture that has to be
# read against a numbering nobody can see proves nothing. Each face is
# matched to its direction from its own centre (a box face's outward
# normal is where its centre sits relative to the body's).
#
# Pitches are exaggerated (the honest millimetre sizes are what
# demo-finish.py shows) so every pattern resolves at a whole-block
# camera distance.
# direction: name, pitch (mm), depth (mm), lay angle (deg)
FACES = {
    "top":    ("knurl",          3.0, 0.90, 0.0),
    "front":  ("knurl-straight", 2.4, 0.70, 0.0),
    "right":  ("blasted",        1.6, 0.24, 0.0),
    "left":   ("turned",         2.0, 0.32, 0.0),
    "back":   ("brushed",        1.4, 0.30, 90.0),
    "bottom": ("",               0.0, 0.00, 0.0),   # unfinished
}

# The face directions in the object's own frame.
DIRECTIONS = [
    ("right",  FreeCAD.Vector(1, 0, 0)),
    ("left",   FreeCAD.Vector(-1, 0, 0)),
    ("back",   FreeCAD.Vector(0, 1, 0)),
    ("front",  FreeCAD.Vector(0, -1, 0)),
    ("top",    FreeCAD.Vector(0, 0, 1)),
    ("bottom", FreeCAD.Vector(0, 0, -1)),
]


def face_direction(obj, face):
    """Which way a box face points, as one of the DIRECTIONS names.

    In the OBJECT's own frame, not the world's: both blocks are to carry
    the same appearance, and the second one is turned so that the faces
    the first hides come round to the camera. Reading the direction in
    world coordinates would hand the turned block the same three
    finishes and cost the picture its other half.
    """
    offset = obj.Placement.Rotation.inverted().multVec(
        face.CenterOfMass - obj.Shape.BoundBox.Center)
    best, bestdot = "", -1e30
    for name, vec in DIRECTIONS:
        dot = offset.dot(vec)
        if dot > bestdot:
            best, bestdot = name, dot
    return best


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def label(doc, name, text, pos, size=15):
    ann = doc.addObject("App::Annotation", name)
    ann.LabelText = [text]
    ann.Position = pos
    ann.ViewObject.FontSize = size
    ann.ViewObject.TextColor = (0.88, 0.88, 0.88)
    return ann


def dump_node(vo, name):
    """The palette and index array the view provider handed the renderer.

    SoFCRenderMaterial is a fork node pivy has no wrapper for, so the
    search is by type NAME and the fields are read generically -- which
    is enough to print them.
    """
    from pivy import coin

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

    # The material BINDING decides what a per-face index even means: a
    # face's finish rides the shape's material index, which is the face
    # index only while the binding is per part. An appearance whose
    # entries differ in nothing a colour can express is exactly the case
    # that used to collapse to one material, and with it to one finish.
    def bindings(node, out):
        name = node.getTypeId().getName()
        # Coin registers its own nodes without the So prefix
        # ("MaterialBinding"); only the fork's own nodes carry it.
        if name in ("MaterialBinding", "SoMaterialBinding"):
            out.append("bind=%d" % int(node.value.getValue()))
        elif name in ("Material", "SoMaterial"):
            out.append("mat[%d]" % node.diffuseColor.getNum())
        children = getattr(node, "getChildren", None)
        kids = children() if children else None
        if kids:
            for i in range(kids.getLength()):
                bindings(kids[i], out)
        return out

    FreeCAD.Console.PrintMessage(
        "FACES %s: bindings=%s\n" % (name, bindings(vo.RootNode, [])))

    node = walk(vo.RootNode)
    if not node:
        FreeCAD.Console.PrintMessage("FACES %s: no SoFCRenderMaterial\n" % name)
        return
    palette = node.getField("finishPalette")
    indices = node.getField("finishIndices")
    entries = [tuple(palette[i].getValue()) for i in range(palette.getNum())]
    idx = [indices[i] for i in range(indices.getNum())]
    FreeCAD.Console.PrintMessage(
        "FACES %s: palette=%s indices=%s\n" % (name, entries, idx))


def appearance(obj):
    """The six-entry per-face appearance both blocks carry.

    One App.Material per face, differing ONLY in the finish: the colour
    stays uniform on purpose, so anything the picture shows face to face
    is the finish and not a paint job.
    """
    vo = obj.ViewObject
    mats = []
    for face in obj.Shape.Faces:
        where = face_direction(obj, face)
        name, pitch, depth, angle = FACES[where]
        mat = FreeCAD.Material()
        mat.DiffuseColor = STEEL
        mat.SpecularColor = (0.9, 0.9, 0.9)
        mat.Shininess = 0.55
        if ONLY:
            # One face carries a knurl coarse enough to be unmistakable
            # and every other face is left bare, so the picture names
            # the face rather than merely showing six patterns.
            name, pitch, depth, angle = (
                ("knurl", 4.0, 1.2, 0.0) if where == ONLY
                else ("", 0.0, 0.0, 0.0))
        if name:
            mat.Finish = name
            mat.FinishPitch = pitch
            mat.FinishDepth = depth
            mat.FinishAngle = angle
        mats.append(mat)
    vo.ShapeAppearance = mats
    # A machined finish is a specular effect; a duller steel is what
    # lets it read (a near-mirror saturates and swallows the relief --
    # docs/ShapeAppearanceDesign.md 9.8).
    vo.addProperty("App::PropertyFloat", "Render_Metallic")
    vo.addProperty("App::PropertyFloat", "Render_Roughness")
    vo.Render_Metallic = 0.2
    vo.Render_Roughness = 0.45


def block(doc, name, x, yaw):
    box = doc.addObject("Part::Box", name)
    box.Length = SIZE
    box.Width = SIZE
    box.Height = SIZE
    box.Placement = FreeCAD.Placement(
        FreeCAD.Vector(x, 0.0, 0.0),
        FreeCAD.Rotation(FreeCAD.Vector(0, 0, 1), yaw))
    # The appearance is per FACE, so the shape has to exist first.
    doc.recompute()
    appearance(box)
    return box


def build():
    try:
        doc = FreeCAD.newDocument("FinishFaces")

        # The second block is turned so the three faces the first one
        # hides face the same camera.
        block(doc, "BlockA", 0.0, 0.0)
        block(doc, "BlockB", SIZE + GAP, 180.0)

        label(doc, "LabelA", "as placed",
              FreeCAD.Vector(-4.0, 0.0, -18.0))
        label(doc, "LabelB", "turned 180 deg (the other faces)",
              FreeCAD.Vector(SIZE + GAP - 4.0, 0.0, -18.0))
        label(doc, "LabelTitle", "one appearance, six faces, six finishes",
              FreeCAD.Vector(-4.0, 0.0, SIZE + 26.0), 17)

        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        if DUMP:
            for obj in doc.Objects:
                if obj.isDerivedFrom("Part::Box"):
                    dump_node(obj.ViewObject, obj.Name)

        # PBR with the image-based studio environment: a machined
        # finish barely reads at all under a single headlight.
        view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = True

        view.setCameraType("Perspective")
        view.viewAxonometric()
        view.fitAll()
        # fitAll frames generously and the pattern is what this picture
        # is about, so pull in along the direction it just set: move the
        # camera itself, since the focal distance alone only moves what
        # the camera is focused on. (docs/RenderDebug: a staging race
        # reframes a scene if the camera is set before the geometry
        # settles, which is why this comes after the fit.)
        cam = view.getCameraNode()
        pos = FreeCAD.Vector(*cam.position.getValue().getValue())
        rot = FreeCAD.Rotation(*cam.orientation.getValue().getValue())
        far = cam.focalDistance.getValue()
        direction = rot.multVec(FreeCAD.Vector(0, 0, -1))
        focal = pos + direction * far
        near = far * 0.42
        cam.position.setValue(*(focal - direction * near))
        cam.focalDistance.setValue(near)
        doc.recompute()
        settle()

        # The second block shows the bottom face only from below, so
        # tilt the whole scene rather than the camera: keep it simple
        # and let the picture show five of the six.
        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("finish faces saved: %s\n" % DOC)
        if SHOT:
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("finish faces shot: %s\n" % SHOT)
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("finish faces FAILED\n")
    finally:
        if EXIT:
            os._exit(0)


QTimer.singleShot(1000, build)
