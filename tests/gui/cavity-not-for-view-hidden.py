"""What a view hides of its own accord leaves no creases behind: the cavity
pass does not shade an object that is not in the view.

docs/HandsOnQueue.md entry 104: "when in per view edit, cavity effect still
shows for per view hidden object" -- hidden "by edit only in edit view. the
object in other view is still there"; "if hide the body, the cavity ghost is
gone".

Cavity shading reads the depth and normal prepass. A draw hidden because its
selection is drawn in its place stays in that prepass on purpose; one the
view hides of its own accord (its ObjectVisibilities, which is also how an
edit puts away what it edits) was treated the same, so its creases were
darkened onto whatever was seen where it had been. And the prepass is kept
from frame to frame under a key that did not hold what the view hides: with
the camera still, hiding changed nothing until something else did.

A plate and a block standing on it, the camera still throughout. Claims:
  - the block hidden in the document: the picture differs from the one with
    the block shown (the premise);
  - the block shown in the document and hidden in the view only: the picture
    is the one with the block hidden in the document -- no creases of it on
    the plate;
  - shown in the view again, the camera not moved: the picture is the one
    with the block shown.

Needs a screen: a picture of the 3D view cannot be taken offscreen.
"""
import os
import time
import traceback

import numpy as np

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/View"
# a pixel counts as different beyond this many levels of 255, and two
# pictures as the same with fewer than this share of such pixels
LEVELS = 6
SHARE = 0.0005


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


def picture(view, tag):
    view.redraw()
    settle(1.5)
    path = os.path.join(OUT, tag + ".png")
    view.saveRenderDump(path, metadata=False)
    return pixels(path)


def differing(a, b):
    """The share of pixels that differ, and how many"""
    if a.shape != b.shape:
        return 1.0, -1
    off = np.abs(a - b).max(axis=2) > LEVELS
    return float(off.mean()), int(off.sum())


def run():
    try:
        prefs = FreeCAD.ParamGet(VIEW)
        render = prefs.GetGroup("Render")
        note("NOTE render type '%s', cavity %s" % (render.GetString("Type", ""),
                                                   render.GetBool("Cavity", True)))
        doc = FreeCAD.newDocument("CavityHidden")
        plate = doc.addObject("Part::Box", "Plate")
        plate.Length, plate.Width, plate.Height = 120, 120, 5
        plate.Placement.Base = FreeCAD.Vector(-40, -40, -5)
        block = doc.addObject("Part::Box", "Block")
        block.Length, block.Width, block.Height = 40, 40, 40
        doc.recompute()
        for obj in doc.Objects:
            obj.ViewObject.DisplayMode = "Shaded"
            obj.ViewObject.ShapeColor = (0.8, 0.8, 0.82)
        # the block in another colour than the plate: in one colour, shaded, it
        # is seen by its creases alone, and hiding it changes a pixel in a thousand
        block.ViewObject.ShapeColor = (0.8, 0.35, 0.3)
        view = FreeCADGui.getDocument(doc.Name).mdiViewsOfType("Gui::View3DInventor")[0]
        view.setCameraType("Orthographic")
        view.setCameraOrientation(FreeCAD.Rotation(0.424708, 0.17592, 0.339851, 0.820473))
        settle(1.0)
        view.fitAll()
        settle(2.5)

        shown = picture(view, "1-shown")
        block.ViewObject.Visibility = False
        settle(0.5)
        hidden = picture(view, "2-hidden-in-the-document")
        share, count = differing(shown, hidden)
        if not check("the block hidden in the document changes the picture", share > 0.01,
                     "%.4f of the pixels, %d" % (share, count)):
            return
        block.ViewObject.Visibility = True
        settle(0.5)
        again = picture(view, "3-shown-again")
        share, count = differing(shown, again)
        check("shown again it is the first picture", share < SHARE,
              "%.5f of the pixels, %d" % (share, count))

        # the one occurrence, by its path: the kind of entry an edit makes, which
        # counts whether or not the view's PerViewVisibilities is on
        check("the view takes an entry that hides the block",
              view.setObjectVisibility(block, False, ""))
        settle(0.5)
        in_view = picture(view, "4-hidden-in-the-view")
        share, count = differing(hidden, in_view)
        check("hidden in the view only: the picture with the block hidden in the document",
              share < SHARE, "%.5f of the pixels, %d" % (share, count))

        view.setObjectVisibility(block, None, "")
        settle(0.5)
        back = picture(view, "5-shown-in-the-view-again")
        share, count = differing(shown, back)
        check("shown in the view again, the camera not moved: the first picture",
              share < SHARE, "%.5f of the pixels, %d" % (share, count))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            for name in list(FreeCAD.listDocuments()):
                FreeCAD.closeDocument(name)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
