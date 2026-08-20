"""Per-face texture: one solid, three faces, three different images.

The thing the per-face appearance could not do until now. A texture is
bound per DRAW -- one sampler -- so the images a shape puts on its
individual faces travel as a PALETTE (uploaded as the layers of one
array texture) plus one layer index per face in the mesh's per-vertex
material stream.

Which image goes on which face is decided by the face's DIRECTION, not
by its number: a picture read against OCCT's face numbering proves
nothing to a reader who cannot see the numbering.

The images are generated here rather than shipped, so the check below
can state exactly what each face must come out as.

Usage: FreeCAD scripts/demo-face-textures.py      (GUI or xvfb)
Env:   FACETEX_DOC    save path ("" = do not save)
       FACETEX_SHOT   screenshot path (saveRenderDump)
       FACETEX_EXIT   "1" = exit after the shot (for scripted runs)
       FACETEX_CHECK  "1" = sample the frame at each face centre and
                      print one PASS/FAIL line per face
       FACETEX_SCALE  millimetres per tile (default: the property's own
                      default, i.e. leave it unset)
"""
import os, sys, time, traceback
import FreeCAD, FreeCADGui
from PySide.QtCore import QTimer
from PySide.QtGui import QImage, QColor

# Must run before the first 3D view exists.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt(
    "RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
    "Type", "bgfx - OpenGL")
# Matcap paints one procedural studio material over the whole scene,
# which would swallow every image here: state the preference this scene
# depends on rather than inheriting whatever the user.cfg holds.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetBool(
    "Matcap", False)
# The cube sits over the corner of the frame and has faked a pass twice
# before (scripts/README): keep it out of a picture that is read by
# sampling pixels.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
    "ShowNaviCube", False)

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.environ.get("FACETEX_DOC", "")
SHOT = os.environ.get("FACETEX_SHOT", "")
EXIT = os.environ.get("FACETEX_EXIT", "") == "1"
CHECK = os.environ.get("FACETEX_CHECK", "") == "1"
SCALE = os.environ.get("FACETEX_SCALE", "")
# Checker cells per image; 1 = a solid image, which is what tells a
# sampling artefact (needs high frequency) from a shading one.
CELLS = int(os.environ.get("FACETEX_CELLS", "4"))

SIZE = 40.0
BASE = (0.75, 0.75, 0.75)   # the paint under the images

# Which image a face carries, by direction. Each is a checker of two
# tones of one hue, so a sampled pixel names the image it came from
# whatever tile it landed in, and the structure still shows that the
# image is being LAID OUT rather than smeared.
#
# The camera shows three faces of a box (top, front, right) and two of
# them carry an image: the third is left bare on purpose, because "the
# image landed on its own face" and "the face nobody imaged is still
# paint" are two different claims and a picture where every face is
# imaged only makes the first. The fourth image goes on a face the
# camera cannot see -- it costs a palette layer, which is what makes
# the visible two prove they are indexed rather than merely present.
IMAGES = {
    "top":   ((230, 40, 40), (120, 10, 10)),     # red
    "front": ((40, 70, 230), (10, 20, 120)),     # blue
    "back":  ((40, 200, 60), (10, 100, 20)),     # green, hidden
}
# Which faces the axonometric camera actually shows.
VISIBLE = ("top", "front", "right")
DIRECTIONS = [
    ("right",  FreeCAD.Vector(1, 0, 0)),
    ("left",   FreeCAD.Vector(-1, 0, 0)),
    ("back",   FreeCAD.Vector(0, 1, 0)),
    ("front",  FreeCAD.Vector(0, -1, 0)),
    ("top",    FreeCAD.Vector(0, 0, 1)),
    ("bottom", FreeCAD.Vector(0, 0, -1)),
]


def face_direction(obj, face):
    offset = face.CenterOfMass - obj.Shape.BoundBox.Center
    best, bestdot = "", -1e30
    for name, vec in DIRECTIONS:
        dot = offset.dot(vec)
        if dot > bestdot:
            best, bestdot = name, dot
    return best


def checker(path, light, dark, cells=4, side=128):
    """Write a two-tone checker PNG and return its path."""
    img = QImage(side, side, QImage.Format_RGB888)
    step = side // cells
    for y in range(side):
        for x in range(side):
            on = ((x // step) + (y // step)) % 2 == 0
            r, g, b = light if on else dark
            img.setPixelColor(x, y, QColor(r, g, b))
    img.save(path, "PNG")
    return path


def pump(n=10):
    view = FreeCADGui.ActiveDocument.ActiveView
    for _ in range(n):
        view.redraw()
        FreeCADGui.updateGui()


def settle(rounds=25):
    for _ in range(rounds):
        time.sleep(0.2)
        pump(3)


def appearance(obj, paths):
    """One App.Material per face, differing ONLY in the image it names.

    The colour stays uniform on purpose: anything the picture shows face
    to face is then the image and not a paint job.
    """
    mats = []
    for face in obj.Shape.Faces:
        mat = FreeCAD.Material()
        mat.DiffuseColor = BASE
        mat.SpecularColor = (0.2, 0.2, 0.2)
        mat.Shininess = 0.2
        where = face_direction(obj, face)
        if where in paths:
            mat.ImagePath = paths[where]
        mats.append(mat)
    obj.ViewObject.ShapeAppearance = mats


def build():
    try:
        tmp = os.path.join(FreeCAD.getUserAppDataDir(), "facetex")
        os.makedirs(tmp, exist_ok=True)
        paths = {}
        for where, (light, dark) in IMAGES.items():
            paths[where] = checker(os.path.join(tmp, where + ".png"),
                                   light, dark, cells=CELLS)

        doc = FreeCAD.newDocument("FaceTextures")
        box = doc.addObject("Part::Box", "Block")
        box.Length = box.Width = box.Height = SIZE
        doc.recompute()          # the appearance is per FACE
        appearance(box, paths)
        if SCALE:
            box.ViewObject.addProperty("App::PropertyFloat",
                                       "Render_FaceTextureScale")
            box.ViewObject.Render_FaceTextureScale = float(SCALE)
        doc.recompute()

        view = FreeCADGui.ActiveDocument.ActiveView
        pump()
        view.setCameraType("Perspective")
        view.viewAxonometric()
        view.fitAll()
        doc.recompute()
        settle()

        if CHECK:
            check(view, box)
        if DOC:
            os.makedirs(os.path.dirname(DOC), exist_ok=True)
            doc.saveAs(DOC)
        if SHOT:
            # The backend draws one frame behind a scene change, and
            # this frame is the whole point: pump before grabbing.
            pump(6)
            view.saveRenderDump(SHOT)
            FreeCAD.Console.PrintMessage("face textures shot: %s\n" % SHOT)
    except Exception:
        FreeCAD.Console.PrintError(traceback.format_exc())
    if EXIT:
        QTimer.singleShot(200, FreeCADGui.getMainWindow().close)


def check(view, box):
    """Sample the drawn frame at each face centre.

    Reads the pixel where the face's own centre projects, so the test
    names the face rather than a screen region someone chose: a face
    wearing an image must come out in that image's hue, and a face
    wearing none must stay grey.
    """
    shot = os.path.join(FreeCAD.getUserAppDataDir(), "facetex", "check.png")
    pump(6)
    view.saveRenderDump(shot)
    img = QImage(shot)
    if img.isNull():
        FreeCAD.Console.PrintError("FACETEX: no frame to check\n")
        return
    # The viewer's pixels may be larger than the widget's (HiDPI or a
    # render scale), so map through the ratio rather than assume 1:1.
    ok = True
    lines = []
    for face in box.Shape.Faces:
        where = face_direction(box, face)
        # A hidden face's centre projects onto whatever is drawn in
        # front of it, so sampling it would read another face's pixels
        # and call them this one's.
        if where not in VISIBLE:
            continue
        c = face.CenterOfMass
        try:
            sx, sy = view.getPointOnScreen(c.x, c.y, c.z)
        except Exception:
            continue
        px = int(sx * img.width() / view.getSize()[0])
        py = int(img.height() - sy * img.height() / view.getSize()[1])
        px = max(0, min(img.width() - 1, px))
        py = max(0, min(img.height() - 1, py))
        # A patch rather than one pixel: the images are checkers, and
        # one texel of the dark tone is still the hue under test but a
        # tile seam or an edge line is not.
        rs = gs = bs = n = 0
        for dy in range(-4, 5, 2):
            for dx in range(-4, 5, 2):
                qx = max(0, min(img.width() - 1, px + dx))
                qy = max(0, min(img.height() - 1, py + dy))
                c2 = QColor(img.pixel(qx, qy))
                rs += c2.red()
                gs += c2.green()
                bs += c2.blue()
                n += 1
        r, g, b = rs // n, gs // n, bs // n
        if where == "top":
            good = r > g + 30 and r > b + 30
        elif where == "front":
            good = b > r + 30 and b > g + 20
        else:
            # The bare face: still the uniform paint underneath.
            good = abs(r - g) < 25 and abs(g - b) < 25
        ok = ok and good
        lines.append("%-6s at %4d,%4d rgb=%3d,%3d,%3d %s"
                     % (where, px, py, r, g, b,
                        "PASS" if good else "FAIL"))
        FreeCAD.Console.PrintMessage("FACETEX %s\n" % lines[-1])
    FreeCAD.Console.PrintMessage(
        "FACETEX result: %s\n" % ("PASS" if ok else "FAIL"))
    # A GUI run sends console messages to the report view, not to the
    # terminal, so the verdict goes somewhere a scripted run can read.
    with open(os.path.join(os.path.dirname(shot), "check.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\nresult: %s\n"
                 % ("PASS" if ok else "FAIL"))


QTimer.singleShot(600, build)
