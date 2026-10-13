"""A feature in edit shows its base feature whether its body is shown or hidden.

docs/HandsOnQueue.md entry 108: "when double click to edit a feature with
invisible body, the feature (previous history step of the editing one) is
not shown on top. old fork has it show on top regardless of body visibility"
-- "not shown at all"; "should be the base feature. just make it the same
regardless of body visible or not".

With the preview on, the view of an edit shows the base feature in the place
of the feature in edit, by an entry of its own visibility table; and with the
body hidden the edit puts that base in the view's on-top group besides
(PartDesignGui's Monitor, the rule the reporter names: "show on top is
supposed to be shown regardless of normal visibility"). Either is drawn only
if the scene's capture gets to it, and the capture stopped at a body that is
hidden in the document: the base was not drawn, only the preview. The
containers a shown occurrence is reached through count as shown for the
capture now; what else they hold stays hidden.

A body with a pad and a pocket in it, left as the commands leave one: the tip
shown, the pad and the sketches hidden. Claims, the camera still:
  - the pocket in edit with the body shown: the edit draws on the view (the
    premise);
  - the body hidden and nothing in edit: nothing of it is drawn;
  - the pocket in edit with the body hidden: the base is drawn, the whole of
    it -- wherever the edit with the body shown drew something, this one
    draws something too. Not the same picture: what is shown on top is drawn
    see-through, its far edges with it;
  - the edit left, the body still hidden: nothing of it is drawn again.

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
V = FreeCAD.Vector
STEPS = []
S = {}
LEVELS = 8
SAME = 0.004
# see-through over the ground, a twentieth of it is within LEVELS of the ground
WHOLE = 0.9


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
    """Where a picture holds something other than the view's ground: the
    ground is a gradient down the view, each row in the colour of its left
    end, which the model does not reach. (Not against a picture of the empty
    view: a task panel changes the view's size, so the pictures in an edit
    and out of one do not lie on each other.)"""
    return np.abs(image - image[:, :1, :]).max(axis=2) > LEVELS


def covered(shown, hidden):
    """Of the pixels the edit with the body shown drew on, the share the
    edit with the body hidden drew on too"""
    if shown.shape != hidden.shape:
        return 0.0
    drawn = drawn_on(shown)
    return float((drawn & drawn_on(hidden)).sum()) / max(float(drawn.sum()), 1.0)


def rect(sketch, x0, y0, x1, y1):
    c = [V(x0, y0, 0), V(x1, y0, 0), V(x1, y1, 0), V(x0, y1, 0)]
    for i in range(4):
        sketch.addGeometry(Part.LineSegment(c[i], c[(i + 1) % 4]), False)
    for i in range(4):
        sketch.addConstraint(Sketcher.Constraint("Coincident", i, 2, (i + 1) % 4, 1))


def edit(on):
    gdoc = FreeCADGui.getDocument("EditBase")
    if on:
        gdoc.setEdit("Pocket")
        settle(2.5)
    else:
        FreeCADGui.Control.closeDialog()
        gdoc.resetEdit()
        settle(1.2)


def start():
    part = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
    part.SetBool("PreviewOnEdit", True)
    part.SetBool("EditOnTop", False)
    # the edit that swaps what its own view shows, the reporter's setting
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool("PerViewEdit", True)
    doc = FreeCAD.newDocument("EditBase")
    body = doc.addObject("PartDesign::Body", "Body")
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
    if not check("the pocket is made on the pad", pocket.isValid()
                 and pocket.BaseFeature is not None and pocket.BaseFeature.Name == "Pad",
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
    picture("1-nothing-in-edit")


def shown():
    edit(True)
    picture("2-edit-body-shown")
    share = float(drawn_on(S["2-edit-body-shown"]).mean())
    check("the pocket in edit, its body shown: the edit draws on the view", share > 0.05,
          "%.4f of the view" % share)
    edit(False)


def hidden():
    doc = FreeCAD.getDocument("EditBase")
    doc.getObject("Body").ViewObject.Visibility = False
    settle(1.0)
    picture("3-body-hidden")
    # the ground alone: its top left corner's colour all along the top row
    empty = S["3-body-hidden"]
    middle = empty[empty.shape[0] // 2, :, :]
    check("the body hidden and nothing in edit: nothing of it is drawn",
          differ(S["1-nothing-in-edit"], empty) > 0.01
          and float(np.abs(middle - middle[0]).max()) <= 2 * LEVELS,
          "against the body shown %.4f; the middle row's spread %.0f"
          % (differ(S["1-nothing-in-edit"], empty), float(np.abs(middle - middle[0]).max())))
    edit(True)
    picture("4-edit-body-hidden")
    share = covered(S["2-edit-body-shown"], S["4-edit-body-hidden"])
    check("the pocket in edit with its body hidden: the base is drawn, the whole of it",
          share >= WHOLE,
          "%.4f of what the edit with the body shown drew on is drawn on; that is %.4f of the "
          "view, and with the body hidden %.4f"
          % (share, float(drawn_on(S["2-edit-body-shown"]).mean()),
             float(drawn_on(S["4-edit-body-hidden"]).mean())))
    edit(False)
    picture("5-body-hidden-again")
    share = differ(S["3-body-hidden"], S["5-body-hidden-again"])
    check("the edit left, the body still hidden: nothing of it is drawn again", share < SAME,
          "%.4f of the pixels differ" % share)


def finish():
    try:
        FreeCADGui.Control.closeDialog()
    except Exception:
        pass
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


STEPS[:] = [start, shown, hidden]
QtCore.QTimer.singleShot(3000, advance)
