"""The live Cycles view shows the model the way up the view has it.

docs/HandsOnQueue.md entry 65: "cycle view mirrors the object. looks like
mirrored by xy plane. not sure, but definitly out of place", and "I tried
cycles in wsl linux build before, but didn't notice this problem".

The path tracer's picture is an image uploaded to a texture, bottom row
first, and drawn over the scene target as one quad
(FrameImageConsumer::drawFrame, vs_fc_comp + fs_fc_cycles_blit). The quad's
texture coordinate comes from fc_clipToUv, which is written for render
targets and turns v over on every backend but OpenGL. An uploaded
texture's first row is at v = 0 on all of them, so on Direct3D -- the
Windows default -- the picture was drawn upside down: for a model with Z
up, mirrored in the XY plane. (Found by reading, by the note-taking
session; this is the picture that confirms it.)

A red cone standing on its base, seen from the front: wide at the bottom,
a point at the top. Measured per picture: the width of the model a quarter
of the way down from its top and a quarter of the way up from its bottom.

Claims:
  - in the render engine's own picture the cone is wider near its bottom
    than near its top (the test stands on what it means to test);
  - with the live Cycles view on and its frame complete, the same: wider
    near the bottom, and by about the engine's own ratio.
Skips where the build has no Cycles.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "CyclesWayUp"


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


def rgb_at(image, x, y):
    p = image.pixel(x, y)
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255)


def widths(view, tag):
    """(width near the top, width near the bottom, rows the model covers) of
    the red in the view's picture"""
    view.redraw()
    settle(1.0)
    path = os.path.join(OUT, tag + ".png")
    view.saveRenderDump(path)
    image = QtGui.QImage(path)
    w, h = image.width(), image.height()
    rows = []
    for y in range(h):
        # the cone is red and nothing else is: the path tracer's background is
        # a lit environment, not one colour to tell the model from
        count = 0
        for x in range(0, w, 2):
            c = rgb_at(image, x, y)
            if c[0] - max(c[1], c[2]) > 50:
                count += 1
        rows.append(count * 2)
    covered = [y for y in range(h) if rows[y] > 6]
    if len(covered) < 20:
        return (0, 0, len(covered))
    top, bottom = covered[0], covered[-1]
    span = bottom - top

    def mean(at):
        band = rows[max(at - 3, 0):at + 4]
        return sum(band) / float(len(band))

    return (mean(top + span // 4), mean(bottom - span // 4), len(covered))


def run():
    try:
        prefs = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        prefs.SetBool("ShowNaviCube", False)
        prefs.SetBool("CornerCoordSystem", False)
        doc = FreeCAD.newDocument(DOC)
        cone = doc.addObject("Part::Cone", "Cone")
        cone.Radius1 = 10
        cone.Radius2 = 0
        cone.Height = 30
        doc.recompute()
        cone.ViewObject.ShapeColor = (1.0, 0.0, 0.0)
        view = FreeCADGui.ActiveDocument.mdiViewsOfType("Gui::View3DInventor")[0]
        view.viewFront()
        view.fitAll()
        settle(3.0)
        top, bottom, rows = widths(view, "engine")
        if not check("the engine's picture has the cone wider near its bottom than near its top",
                     rows > 40 and bottom > 1.5 * top > 0,
                     "width %.0f px near the top, %.0f near the bottom, over %d rows" % (
                         top, bottom, rows)):
            return
        try:
            view.cyclesViewport(True, device="CPU", samples=8, denoise=False)
        except Exception as e:
            note("SKIP no live Cycles view here: %s" % str(e)[:160])
            return
        end = time.monotonic() + 180
        status = None
        while time.monotonic() < end:
            settle(0.5)
            status = view.cyclesViewportStatus()
            if status and (status.get("complete") or status.get("error")):
                break
        if not check("the Cycles view's frame is complete",
                     bool(status) and status.get("complete") and not status.get("error"),
                     status and dict((k, status[k]) for k in ("complete", "progress", "error",
                                                              "triangles"))):
            view.cyclesViewport(False)
            return
        settle(1.0)
        ctop, cbottom, crows = widths(view, "cycles")
        view.cyclesViewport(False)
        check("the Cycles view shows the cone the same way up: wider near its bottom",
              crows > 40 and cbottom > 1.5 * ctop > 0,
              "width %.0f px near the top, %.0f near the bottom, over %d rows; the engine's "
              "%.0f and %.0f" % (ctop, cbottom, crows, top, bottom))
        check("... and about as the engine draws it",
              ctop > 0 and top > 0 and 0.6 < (cbottom / ctop) / (bottom / top) < 1.6,
              "bottom to top %.2f, the engine's %.2f" % (
                  cbottom / ctop if ctop else 0.0, bottom / top if top else 0.0))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
