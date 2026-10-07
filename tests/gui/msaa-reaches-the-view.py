"""Switching anti-aliasing on gives the view multisampled targets.

The backend builds its scene targets with the sample count the
AntiAliasing preference asks for, and when a multisampled target cannot be
built it falls back to none and keeps drawing -- for the rest of the
session, saying so once on the console. From 2026-09-07 that fallback was
every MSAA view's fate on every backend: the scene depth was built
readable as a texture whatever the sample count, and bgfx refuses a
framebuffer whose multisampled depth is neither write-only nor sampled per
sample. So "MSAA 4x" in the preferences drew exactly what "None" drew
(found 2026-10-07 while looking at docs/HandsOnQueue.md entry 26).

Claims, on a view of a box drawn by the backend:

  - without anti-aliasing the view's targets have no multisampling, and a
    capture reads the depth (it counts the pixels the box covers);
  - with "MSAA 4x" chosen they are built with 4 samples;
  - a capture of that view still returns its picture, and reports the
    pixel count as unknown (-1) instead of a number: a multisampled depth
    cannot be read back;
  - with "MSAA 2x" they are built with 2, and with "None" again with none
    -- the fallback was not taken on the way.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/View"
# the values of the AntiAliasing key (View3DInventorViewer::AntiAliasing)
NONE, MSAA2, MSAA4 = 0, 2, 3


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


def stats(view, mode):
    FreeCAD.ParamGet(VIEW).SetInt("AntiAliasing", mode)
    settle(2.5)
    view.redraw()
    settle(0.5)
    return view.getRenderStats()


def run():
    doc = None
    try:
        doc = FreeCAD.newDocument("MsaaReaches")
        doc.addObject("Part::Box", "Box")
        doc.recompute()
        view = FreeCADGui.ActiveDocument.ActiveView
        view.viewIsometric()
        view.fitAll()
        settle(3.0)
        try:
            first = stats(view, NONE)
        except Exception as e:
            check("the view is drawn by the backend", False, e)
            return
        check("a capture says how many samples the view was built with", "msaaSamples" in first, sorted(first)[:12])
        check("without anti-aliasing there is no multisampling", first.get("msaaSamples", -1) <= 1,
              first.get("msaaSamples"))
        check("and a capture counts the pixels the box covers", first.get("geometryPixels", -1) > 1000,
              first.get("geometryPixels"))

        four = stats(view, MSAA4)
        check("with MSAA 4x the targets are built with 4 samples", four.get("msaaSamples") == 4,
              four.get("msaaSamples"))
        check("a capture of that view has its picture", four.get("width", 0) > 100 and four.get("height", 0) > 100,
              "%s x %s" % (four.get("width"), four.get("height")))
        check("and reports the covered pixels as unknown", four.get("geometryPixels") == -1,
              four.get("geometryPixels"))
        path = os.path.join(OUT, "msaa4.png")
        view.saveRenderDump(path)
        check("the picture can be saved", os.path.exists(path) and os.path.getsize(path) > 2000)

        two = stats(view, MSAA2)
        check("with MSAA 2x they are built with 2", two.get("msaaSamples") == 2, two.get("msaaSamples"))
        none = stats(view, NONE)
        check("with None again there is no multisampling", none.get("msaaSamples", -1) <= 1, none.get("msaaSamples"))
        check("and the capture counts pixels again", none.get("geometryPixels", -1) > 1000, none.get("geometryPixels"))
        again = stats(view, MSAA4)
        check("MSAA 4x a second time is 4 samples again", again.get("msaaSamples") == 4, again.get("msaaSamples"))
        FreeCAD.ParamGet(VIEW).RemInt("AntiAliasing")
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            if doc is not None:
                FreeCAD.closeDocument(doc.Name)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
