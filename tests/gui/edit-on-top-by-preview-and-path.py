"""What the view of an edit shows on top: by the preview, and by whether the
path of the edit is shown.

docs/HandsOnQueue.md entry 108: "when double click to edit a feature with
invisible body, the feature (previous history step of the editing one) is
not shown on top. old fork has it show on top regardless of body visibility"
-- "not shown at all"; and as ruled: "When visible follow the control in edit
panel. If preview is on, show on top, else show normal. If hidden, then
always shown on top", the control being the panel's "Show preview";
"Visible checking not only apply to the body itself, but to the full editing
path".

Two things were in the way. What a view shows of its own accord -- the base
feature an edit puts in the feature's place, or what it puts on top -- is
drawn only if the scene's capture gets to it, and the capture stopped at a
container hidden in the document: with the body hidden only the preview was
drawn (the containers a shown occurrence is reached through count as shown
for the capture now). And what was on top went by a "Show on top" check box
of its own; it goes by the preview and the path now.

A part holding a body with a pad and a pocket, left as the commands leave
one: the tip shown, the pad and the sketches hidden. The pocket in edit by
its path through the part. Claims, the camera still:
  - preview on, everything shown: the edit draws on the view, on top --
    another picture than without the preview;
  - preview on, the body hidden: the same picture;
  - preview on, the part hidden and the body shown: the same picture again
    (the whole path counts, not the body alone);
  - preview off, everything shown: the feature in its place, drawn like any
    other;
  - preview off, the body hidden: something is drawn all the same, and it is
    not the picture of the feature in its place -- it is on top;
  - the edit left, the body hidden: nothing of it is drawn.

Needs a screen: a picture of the 3D view cannot be taken offscreen. The
pointer is not moved.
"""
import os
import time
import traceback

import numpy as np

import FreeCAD
import FreeCADGui
import Part
import Sketcher
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PART = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
V = FreeCAD.Vector
STEPS = []
S = {}
LEVELS = 8
SAME = 0.004


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def pixels(path):
    image = QtGui.QImage(path).convertToFormat(QtGui.QImage.Format_RGB888)
    w, h = image.width(), image.height()
    raw = np.frombuffer(image.constBits(), dtype=np.uint8, count=h * image.bytesPerLine())
    return raw.reshape(h, image.bytesPerLine())[:, :w * 3].reshape(h, w, 3).astype(np.float64)


def picture(tag):
    view = S["view"]
    view.redraw()
    settle(1.5)
    path = os.path.join(OUT, tag + ".png")
    view.saveRenderDump(path, metadata=False)
    S[tag] = pixels(path)
    return S[tag]


def differ(a, b):
    if a.shape != b.shape:
        return 1.0
    return float((np.abs(a - b).max(axis=2) > LEVELS).mean())


def drawn_on(image):
    """The share of a picture that holds something other than the view's
    ground: the ground is a gradient down the view, each row in the colour of
    its left end, which the model does not reach. (Not against a picture of
    the empty view: a task panel changes the view's size, so the pictures in
    an edit and out of one do not lie on each other.)"""
    return float((np.abs(image - image[:, :1, :]).max(axis=2) > LEVELS).mean())


def rect(sketch, x0, y0, x1, y1):
    c = [V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(c[i], c[(i + 1) % 4]), False)
    for i in range(4):
        sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))


def in_edit(tag, preview, hide=()):
    """The pocket in edit by its path through the part, the objects named in
    `hide` hidden in the document meanwhile: a picture of it"""
    doc = FreeCAD.getDocument("EditOnTop")
    gdoc = FreeCADGui.getDocument("EditOnTop")
    PART.SetBool("PreviewOnEdit", preview)
    for name in hide:
        doc.getObject(name).ViewObject.Visibility = False
    settle(1.0)
    try:
        gdoc.setEdit(doc.getObject("Holder"), 0, "Body.Pocket.")
        settle(2.5)
        return picture(tag)
    finally:
        FreeCADGui.Control.closeDialog()
        gdoc.resetEdit()
        settle(1.2)
        for name in hide:
            doc.getObject(name).ViewObject.Visibility = True
        settle(0.8)


def start():
    S["preview"] = PART.GetBool("PreviewOnEdit", True)
    # the edit that swaps what its own view shows, the reporter's setting
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool("PerViewEdit", True)
    doc = FreeCAD.newDocument("EditOnTop")
    holder = doc.addObject("App::Part", "Holder")
    body = doc.addObject("PartDesign::Body", "Body")
    holder.addObject(body)
    s1 = body.newObject("Sketcher::SketchObject", "Sketch")
    rect(s1, 0, 0, 40, 30)
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = s1
    pad.Length = 20
    doc.recompute()
    s2 = body.newObject("Sketcher::SketchObject", "Sketch001")
    s2.AttachmentSupport = [(pad, ("Face6",))]
    s2.MapMode = "FlatFace"
    doc.recompute()
    rect(s2, 10, 8, 30, 22)
    doc.recompute()
    pocket = body.newObject("PartDesign::Pocket", "Pocket")
    pocket.Profile = s2
    pocket.Length = 8
    doc.recompute()
    # as the commands leave a body: its tip shown, the steps before it and
    # the sketches hidden
    for hidden_one in (s1, pad, s2):
        hidden_one.ViewObject.Visibility = False
    if not check("the pocket is made on the pad, in a body in a part", pocket.isValid()
                 and pocket.BaseFeature is not None and pocket.BaseFeature.Name == "Pad"
                 and holder.getSubObject("Body.Pocket.", retType=1) is not None,
                 pocket.getStatusString()):
        STEPS[:] = []
        return
    view = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")[0]
    S["view"] = view
    view.setCameraType("Orthographic")
    view.setCameraOrientation(FreeCAD.Rotation(0.424708, 0.17592, 0.339851, 0.820473))
    settle(1.0)
    view.fitAll()
    settle(2.5)


def with_preview():
    shown = in_edit("1-preview-all-shown", True)
    check("preview on, everything shown: the edit draws on the view", drawn_on(shown) > 0.05,
          "%.4f of the view" % drawn_on(shown))
    body = in_edit("2-preview-body-hidden", True, ("Body",))
    share = differ(shown, body)
    check("preview on, the body hidden: the same picture", share < SAME,
          "%.4f of the pixels differ; %.4f of the view drawn on" % (share, drawn_on(body)))
    part = in_edit("3-preview-part-hidden", True, ("Holder",))
    share = differ(shown, part)
    check("preview on, the part hidden and the body shown: the same picture", share < SAME,
          "%.4f of the pixels differ; %.4f of the view drawn on" % (share, drawn_on(part)))


def without_preview():
    shown = in_edit("4-no-preview-all-shown", False)
    check("preview off, everything shown: the feature is drawn", drawn_on(shown) > 0.05,
          "%.4f of the view" % drawn_on(shown))
    share = differ(S["1-preview-all-shown"], shown)
    check("preview off, everything shown: another picture than with the preview, which is on top",
          share > 0.01, "%.4f of the pixels differ" % share)
    hidden = in_edit("5-no-preview-body-hidden", False, ("Body",))
    check("preview off, the body hidden: something is drawn all the same",
          drawn_on(hidden) > 0.05, "%.4f of the view" % drawn_on(hidden))
    share = differ(shown, hidden)
    check("preview off, the body hidden: not the picture of the feature in its place",
          share > 0.01, "%.4f of the pixels differ" % share)


def left():
    doc = FreeCAD.getDocument("EditOnTop")
    doc.getObject("Body").ViewObject.Visibility = False
    settle(1.2)
    empty = picture("6-left-body-hidden")
    # (the navigation cube is in the picture, a two hundredth of it)
    middle = empty[empty.shape[0] // 2, :, :]
    check("the edit left, the body hidden: nothing of it is drawn",
          drawn_on(empty) < 0.01 and float(np.abs(middle - middle[0]).max()) <= 2 * LEVELS,
          "%.4f of the view drawn on; the middle row's spread %.0f"
          % (drawn_on(empty), float(np.abs(middle - middle[0]).max())))


def finish():
    try:
        FreeCADGui.Control.closeDialog()
    except Exception:
        pass
    PART.SetBool("PreviewOnEdit", S.get("preview", True))
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(1200, advance)


STEPS[:] = [start, with_preview, without_preview, left]
QtCore.QTimer.singleShot(3000, advance)
