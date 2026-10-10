"""The render type alone says whether the render engine draws a 3D view.

docs/HandsOnQueue.md entry 50, the reporter's decisions: "Render type
Default should be bgfx plus a platform dependent default", "let's make
render type 'Legacy' to mean the old coin rendering", "Legacy controls
whether to use engine and render cache keep its original meaning. with
'Default', i.e. new render engine, render cache always fixed to 3"; and of
the render cache: "can still be set by program mostly for testing [...] but
in engine rendering, the program shall always go to the cache mode 3 route
regardless of the setting".

Before, View/Render/Type "Default" meant NO backend, the engine was chosen
once at startup by writing a backend's name over it, and a session whose
parameters were cleared was left without the engine until the next start.

Claims, on one 3D view (it has a backend when its statistics can be asked):
  - with "Default" stored, and with nothing stored, the view has a backend;
  - with a name no backend has, it has the platform's (a name from another
    build cannot leave a view pointing at nothing);
  - with "Legacy" it has none, and the render cache mode is the user's: set
    to 0 and to 2 it stays what it was set to;
  - back to "Default" from "Legacy" with the mode at 0: the view has a
    backend again, and the setting is left as it was, 0 -- it is not looked
    at with the engine, not rewritten;
  - under "Default" a mode set by program changes nothing: set to 3, to 0
    and to 2, the backend stays.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "RenderType"
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
STEPS = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def backend():
    views = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")
    try:
        views[0].getRenderStats()
    except RuntimeError as e:
        if "No external renderer" in str(e):
            return False
    return True


def state():
    return "type %r, render cache %d, backend %s" % (
        RENDER.GetString("Type", "(nothing stored)"), VIEW.GetInt("RenderCache", 3), backend())


def start():
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    FreeCADGui.SendMsgToActiveView("ViewFit")


def step(change, name, expect):
    """One change, and what holds a moment after it"""
    def do():
        change()

    def look():
        check(name, expect(), state())

    do.__name__ = look.__name__ = name.split(":")[0]
    return [(300, do), (1800, look)]


def plan():
    steps = []
    steps += step(lambda: RENDER.SetString("Type", "Default"),
                  "Default: the view has a backend", backend)
    steps += step(lambda: RENDER.RemString("Type"),
                  "nothing stored: the view has a backend", backend)
    steps += step(lambda: RENDER.SetString("Type", "no such backend"),
                  "a name no backend has: the view has the platform's", backend)
    steps += step(lambda: RENDER.SetString("Type", "Legacy"),
                  "Legacy: the view has no backend", lambda: not backend())
    steps += step(lambda: VIEW.SetInt("RenderCache", 0),
                  "Legacy: a render cache of 0 stays 0",
                  lambda: VIEW.GetInt("RenderCache", 3) == 0 and not backend())
    steps += step(lambda: VIEW.SetInt("RenderCache", 2),
                  "Legacy: a render cache of 2 stays 2",
                  lambda: VIEW.GetInt("RenderCache", 3) == 2 and not backend())
    steps += step(lambda: VIEW.SetInt("RenderCache", 3),
                  "Legacy: with the render cache at 3 there is still no backend",
                  lambda: VIEW.GetInt("RenderCache", 0) == 3 and not backend())
    steps += step(lambda: VIEW.SetInt("RenderCache", 0), "Legacy: back to 0",
                  lambda: VIEW.GetInt("RenderCache", 3) == 0)
    steps += step(lambda: RENDER.SetString("Type", "Default"),
                  "Default again: the view has a backend, the setting left at 0",
                  lambda: VIEW.GetInt("RenderCache", 3) == 0 and backend())
    steps += step(lambda: VIEW.SetInt("RenderCache", 3),
                  "Default: the render cache set to 3, the backend stays", backend)
    steps += step(lambda: VIEW.SetInt("RenderCache", 0),
                  "Default: the render cache set to 0, the backend stays",
                  lambda: VIEW.GetInt("RenderCache", 3) == 0 and backend())
    steps += step(lambda: VIEW.SetInt("RenderCache", 2),
                  "Default: the render cache set to 2, the backend stays",
                  lambda: VIEW.GetInt("RenderCache", 3) == 2 and backend())
    steps += step(lambda: RENDER.SetString("Type", "Legacy"),
                  "Legacy again: no backend, and the render cache of 2 counts",
                  lambda: VIEW.GetInt("RenderCache", 3) == 2 and not backend())
    return steps


def finish():
    RENDER.RemString("Type")
    VIEW.SetInt("RenderCache", 3)
    try:
        FreeCAD.closeDocument(DOC)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("FAIL the step %s ran | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((3000, start))
STEPS.append((3000, lambda: note("NOTE at the start: " + state())))
STEPS.extend(plan())
advance()
