"""Cosmetic threads: a tapped hole drawn by the render engine, not modelled.

A PartDesign Hole that is threaded but does not model the thread leaves a
plain bore -- the geometry the part is made with -- and states the thread
as a surface finish of that bore (App::SurfaceFinish::Thread, shaded by
fc_finish.sh's fcFinishThread): the helix about the bore's axis, the
ISO truncated-V profile, a parallax march down the groove, the roots
occluded, and the thread running out where the tap stopped.

The block holds three holes, placed so that each shows one thing:

  A  M12 right-hand, blind, thread 12 of 20 mm, centred ON the block's
     front edge -- so half the bore is open to the camera and the
     thread's helix, its depth and its runout are all in plain view.
  B  the same hole, whole, beside it: what a tapped hole looks like from
     where anyone looks at one.
  C  M16 LEFT-hand, through the block from the side, centred on the top
     edge: the helix leans the other way.

Usage: FreeCAD scripts/demo-thread.py      (GUI)
Env:   THREAD_DOC    save path (default data/examples/render/thread.FCStd)
       THREAD_SHOT   screenshot path prefix (optional): writes
                     <prefix>-overview.png and <prefix>-close.png
       THREAD_EXIT   "1" = quit after save/shots
       THREAD_OFF    "1" = CosmeticThread off: the plain bores, the
                     one-variable control beside the showcase
       THREAD_DUMP   "1" = print the finish palette, extents and face
                     indices the Tip's view provider built
       THREAD_PBR    "0" = Phong headlight instead of PBR + IBL
       THREAD_ORTHO  "1" = orthographic camera, FreeCAD's default
"""
import math, os, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer
from PySide import QtWidgets

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("THREAD_DOC", os.path.join(
    REPO, "data", "examples", "render", "thread.FCStd"))
SHOT = os.environ.get("THREAD_SHOT", "")
EXIT = os.environ.get("THREAD_EXIT", "") == "1"
OFF = os.environ.get("THREAD_OFF", "") == "1"
DUMP = os.environ.get("THREAD_DUMP", "") == "1"
PBR = os.environ.get("THREAD_PBR", "1") != "0"
ORTHO = os.environ.get("THREAD_ORTHO", "") == "1"
METAL = float(os.environ.get("THREAD_METAL", "1.0"))
ROUGH = float(os.environ.get("THREAD_ROUGH", "0.32"))
STEEL = (0.56, 0.57, 0.58)

LENGTH, WIDTH, HEIGHT = 70.0, 36.0, 26.0


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=20):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def render_material(vo):
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


def dump_finish(vo, name):
    node = render_material(vo)
    if not node:
        FreeCAD.Console.PrintMessage("THREAD %s: no SoFCRenderMaterial\n" % name)
        return
    palette = node.getField("finishPalette")
    extents = node.getField("finishExtents")
    indices = node.getField("finishIndices")
    entries = [tuple(round(v, 3) for v in palette[i].getValue())
               for i in range(palette.getNum())]
    ext = [tuple(round(v, 3) for v in extents[i].getValue())
           for i in range(extents.getNum())]
    idx = [indices[i] for i in range(indices.getNum())]
    threaded = [i for i, v in enumerate(idx) if v]
    FreeCAD.Console.PrintMessage(
        "THREAD %s: palette=%s extents=%s threaded faces=%s\n"
        % (name, entries, ext, threaded))


def sketch(doc, body, name, placement, centres):
    import Part
    sk = doc.addObject("Sketcher::SketchObject", name)
    body.addObject(sk)
    sk.MapMode = "Deactivated"
    sk.Placement = placement
    for x, y in centres:
        sk.addGeometry(Part.Circle(FreeCAD.Vector(x, y, 0),
                                   FreeCAD.Vector(0, 0, 1), 3.0))
    doc.recompute()
    return sk


def hole(doc, body, name, profile, size, depth, thread, left=False,
         through=False):
    h = doc.addObject("PartDesign::Hole", name)
    body.addObject(h)
    h.Profile = profile
    h.ThreadType = "ISOMetricProfile"
    h.ThreadSize = size
    h.Threaded = True
    h.ModelThread = False
    h.CosmeticThread = not OFF
    h.ThreadDirection = "Left" if left else "Right"
    if through:
        h.DepthType = "ThroughAll"
        h.ThreadDepthType = "Hole Depth"
    else:
        h.DepthType = "Dimension"
        h.Depth = depth
        h.DrillPoint = "Angled"
        h.ThreadDepthType = "Dimension"
        h.ThreadDepth = thread
    doc.recompute()
    return h


def steel(vo):
    mat = FreeCAD.Material()
    mat.DiffuseColor = STEEL
    mat.SpecularColor = (0.95, 0.95, 0.95)
    mat.Shininess = 0.7
    vo.ShapeAppearance = [mat]
    for prop, value in (("Render_Metallic", METAL), ("Render_Roughness", ROUGH)):
        if not hasattr(vo, prop):
            vo.addProperty("App::PropertyFloat", prop)
        setattr(vo, prop, value)


def camera(view, position, target):
    cam = view.getCameraNode()
    pos = FreeCAD.Vector(*position)
    tgt = FreeCAD.Vector(*target)
    direction = tgt - pos
    dist = direction.Length
    direction.normalize()
    rot = FreeCAD.Rotation(FreeCAD.Vector(0, 0, -1), direction)
    # Keep +Z up on screen: turn about the view axis until the camera's
    # own up vector has no sideways lean
    up = rot.multVec(FreeCAD.Vector(0, 1, 0))
    want = FreeCAD.Vector(0, 0, 1) - direction * direction.dot(FreeCAD.Vector(0, 0, 1))
    if want.Length > 1e-9:
        want.normalize()
        angle = math.atan2(direction.dot(up.cross(want)), up.dot(want))
        rot = FreeCAD.Rotation(direction, math.degrees(angle)).multiply(rot)
    cam.position.setValue(pos.x, pos.y, pos.z)
    cam.orientation.setValue(*rot.Q)
    cam.focalDistance.setValue(dist)


def shoot(view, path):
    view.saveRenderDump(path)
    FreeCAD.Console.PrintMessage("thread shot: %s\n" % path)


def build():
    try:
        doc = FreeCAD.newDocument("Thread")
        body = doc.addObject("PartDesign::Body", "Body")
        box = doc.addObject("PartDesign::AdditiveBox", "Block")
        body.addObject(box)
        box.Length, box.Width, box.Height = LENGTH, WIDTH, HEIGHT
        doc.recompute()

        top = FreeCAD.Placement(FreeCAD.Vector(0, 0, HEIGHT), FreeCAD.Rotation())
        # A on the front edge (y = 0), B whole beside it
        ska = sketch(doc, body, "SketchA", top, [(16.0, 0.0), (38.0, 11.0)])
        hole(doc, body, "HoleA", ska, "M12", 20.0, 12.0)

        # C from the right-hand side (x = LENGTH), along -X, centred on the
        # top edge so its thread is open to the sky
        side = FreeCAD.Placement(FreeCAD.Vector(LENGTH, 0, 0),
                                 FreeCAD.Rotation(FreeCAD.Vector(0, 1, 0), 90))
        skc = sketch(doc, body, "SketchC", side, [(-HEIGHT, 26.0)])
        hole(doc, body, "HoleC", skc, "M16", 0.0, 0.0, left=True, through=True)

        doc.recompute()
        for obj in body.Group:
            if obj.isDerivedFrom("PartDesign::Feature"):
                steel(obj.ViewObject)
        steel(body.ViewObject)
        # A body shows its Tip; the features before it would draw their
        # own, unholed shapes over it
        for obj in body.Group:
            if obj is not body.Tip and obj.ViewObject:
                obj.ViewObject.Visibility = False
        doc.recompute()
        pump()

        tip = body.Tip
        if DUMP:
            dump_finish(tip.ViewObject, tip.Name)
            for h in (doc.HoleA, doc.HoleC):
                FreeCAD.Console.PrintMessage(
                    "THREAD %s: Threaded=%s Cosmetic=%s Model=%s\n"
                    % (h.Name, h.Threaded, h.CosmeticThread, h.ModelThread))

        view = FreeCADGui.ActiveDocument.ActiveView
        if not hasattr(view, "Render_PBR"):
            view.addProperty("App::PropertyBool", "Render_PBR")
        view.Render_PBR = PBR
        if not hasattr(view, "Render_PBREnvBackground"):
            view.addProperty("App::PropertyBool", "Render_PBREnvBackground")
        view.Render_PBREnvBackground = PBR

        view.setCameraType("Orthographic" if ORTHO else "Perspective")
        view.viewAxonometric()
        view.fitAll()
        doc.recompute()
        settle()

        os.makedirs(os.path.dirname(DOC), exist_ok=True)
        doc.saveAs(DOC)
        FreeCAD.Console.PrintMessage("thread saved: %s\n" % DOC)
        if SHOT:
            shoot(view, SHOT + "-overview.png")
            # Into the half-bore of A from the front, a little above
            camera(view, (16.0 + 20.0, -34.0, HEIGHT + 16.0),
                   (16.0, 0.0, HEIGHT - 8.0))
            settle(10)
            shoot(view, SHOT + "-close.png")
            # Down into the whole bore B
            camera(view, (38.0 + 14.0, 11.0 - 20.0, HEIGHT + 30.0),
                   (38.0, 11.0, HEIGHT - 4.0))
            settle(10)
            shoot(view, SHOT + "-down.png")
            # Along the top edge at the left-hand thread C
            camera(view, (LENGTH - 30.0, 26.0 - 30.0, HEIGHT + 24.0),
                   (LENGTH - 30.0, 26.0, HEIGHT - 4.0))
            settle(10)
            shoot(view, SHOT + "-left.png")
    except Exception:
        traceback.print_exc()
        FreeCAD.Console.PrintError("thread demo FAILED\n")
    finally:
        if EXIT:
            for name in list(FreeCAD.listDocuments()):
                FreeCAD.closeDocument(name)
            QTimer.singleShot(200, QtWidgets.QApplication.quit)


QTimer.singleShot(1000, build)
