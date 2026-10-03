"""In render cache mode 3 an SoImage is drawn pixel for pixel, as GL does.

GL puts an SoImage on screen with glDrawPixels: the anchor's window
position truncated to a whole pixel, less a whole number of pixels for
the alignment, one texel to one pixel, no filtering. The bgfx backend
draws it as a textured quad, and left that quad wherever the anchor
projected -- half a pixel off, the filter spread every texel over two
pixels. A Sketcher Horizontal constraint icon is a 2-pixel red stroke;
it came out as three rows at a third of its colour, a pale dash.

The measurement: the backend's own framebuffer (saveRenderDump, source
"renderer", read before Coin composites anything), around images of a
10x2 opaque bar on a transparent ground. Each must cover exactly 10x2
pixels, every one of them the bar's colour.

Scored against the tree before the fix: 9 of 16 checks failed. Four
bars covered 10x3 or 11x3 pixels, and all five drew pixels off the
bar's colour -- the one at (-20, 0) a body of d3 3b 3c against ff 26 00,
fringed with rows darker than the ground. Putting the quad on the pixel
grid alone still left the anisotropic sampler's fringe; both halves are
needed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ImagePixels"
BAR = (255, 38, 0)
# (anchor, image side): an even and an odd side, for the centring.
IMAGES = [((-20.0, 0.0), 16), ((0.0, 0.0), 15), ((20.0, 0.0), 16),
          ((7.3, 4.1), 15), ((-11.6, -3.7), 16)]
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def bar_image(side):
    """side x side RGBA, a 10x2 opaque bar across the middle."""
    out = bytearray()
    mid = side // 2
    for y in range(side):
        for x in range(side):
            on = y in (mid - 1, mid) and 3 <= x < 13
            out += bytes(BAR + (255,)) if on else bytes(4)
    return bytes(out)


def run():
    try:
        from pivy import coin

        FreeCADGui.getMainWindow().showMaximized()
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        box.Placement.Base = FreeCAD.Vector(-30, -10, -20)
        box.Length, box.Width, box.Height = 60, 20, 1
        doc.recompute()
        FreeCADGui.getDocument(DOC).getObject("Box").Visibility = False
        view = FreeCADGui.activeDocument().activeView()
        root = coin.SoSeparator()
        for (x, y), side in IMAGES:
            sep = coin.SoSeparator()
            t = coin.SoTranslation()
            t.translation = (x, y, 0)
            img = coin.SoImage()
            img.image.setValue(coin.SbVec2s(side, side), 4, bar_image(side))
            img.vertAlignment = coin.SoImage.HALF
            img.horAlignment = coin.SoImage.CENTER
            sep.addChild(t)
            sep.addChild(img)
            root.addChild(sep)
        view.getSceneGraph().addChild(root)
        state["root"] = root
        view.viewTop()
        view.fitAll()
        state["view"] = view
        QtCore.QTimer.singleShot(2000, measure)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
        finish()


def measure():
    try:
        view = state["view"]
        path = os.path.join(OUT, "backend.png")
        try:
            view.saveRenderDump(path, "renderer")
            active, detail = True, ""
        except Exception as e:
            active, detail = False, str(e)
        if not check("the bgfx renderer draws the view", active, detail):
            finish()
            return
        img = QtGui.QImage(path)
        h = img.height()
        for (x, y), side in IMAGES:
            vx, vy = view.getPointOnViewport(FreeCAD.Vector(x, y, 0))
            cx, cy = int(vx), int(h - 1 - vy)
            bg = QtGui.QColor(img.pixel(cx - 12, cy - 12))
            on, off_colour = set(), []
            for yy in range(cy - 12, cy + 13):
                for xx in range(cx - 12, cx + 13):
                    c = QtGui.QColor(img.pixel(xx, yy))
                    if (abs(c.red() - bg.red()) + abs(c.green() - bg.green())
                            + abs(c.blue() - bg.blue())) <= 12:
                        continue
                    on.add((xx, yy))
                    if max(abs(c.red() - BAR[0]), abs(c.green() - BAR[1]),
                           abs(c.blue() - BAR[2])) > 3:
                        off_colour.append("%02x%02x%02x" % (c.red(), c.green(), c.blue()))
            where = "at (%g, %g), side %d" % (x, y, side)
            if not check("the image %s is drawn" % where, on):
                continue
            xs = [p[0] for p in on]
            ys = [p[1] for p in on]
            w, hh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
            check("the image %s covers exactly its 10x2 bar" % where,
                  (w, hh) == (10, 2) and len(on) == 20,
                  "%dx%d box, %d px" % (w, hh, len(on)))
            check("the image %s draws every pixel in the bar's colour" % where,
                  not off_colour, " ".join(sorted(set(off_colour))[:6]))
        view.getSceneGraph().removeChild(state["root"])
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
