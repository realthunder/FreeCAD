"""Two ways a user program did not come up: its inputs, and its binding.

A user fragment stage names the varyings it reads, and no more.

A fragment program written for the stock vertex stage says what it reads
of it: '$input v_normal, v_color0, v_vpos' is what docs/RenderDebug.md
shows and what the bundled effects have. Direct3D pairs the two stages by
position, not by name, so that list -- v_vpos third where the vertex stage
has it fifth -- did not link: "user shader: program link failed", and the
object went on being drawn by the stock program. The renderer now hands the
compile the vertex stage's whole list in its order (BGFXRenderer.cpp,
completeInputs()).

And a program bound by a path (Scope 'Instance') replaces the draws of
that occurrence through an entry the renderer draws in a view of its own
(BGFXView::ViewSelection), where the user program was never switched in:
since 2026-08-06 every such binding drew with the stock program, on every
backend.

Claims, each by the backend's own frame at the middle of a box, each for a
binding of Scope 'Object' and one of Scope 'Instance':
  - a program naming three varyings comes up;
  - so does one naming one, and one naming none;
  - one naming the whole list comes up;
  - one naming a varying the vertex stage does not hand on does not come
    up, and takes nothing else down: the box is drawn by the stock program.

Scored against the tree before it: every 'comes up' claim of Scope
'Instance' FAILs; on Direct3D so do three, one and none of Scope 'Object'.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "UserShaderInputs"
state = {"done": False, "shots": 0}

BODY = """
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = vec4(%s, 1.0);
}
"""

# name, the $input line, the colour, whether it is to come up
CASES = [
    ("three", "$input v_normal, v_color0, v_vpos", "1.0, 0.1, 0.1", True),
    ("one", "$input v_vpos", "0.1, 1.0, 0.1", True),
    ("none", "", "0.1, 0.1, 1.0", True),
    ("all", "$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex",
     "1.0, 1.0, 0.1", True),
    ("unknown", "$input v_normal, v_nosuch", "1.0, 0.1, 1.0", False),
]


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(sec=0.3):
    until = time.time() + sec
    while True:
        for _ in range(5):
            QtCore.QCoreApplication.processEvents()
        if time.time() >= until:
            break
        time.sleep(0.02)


def colour(view, pt):
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    state["shots"] += 1
    path = os.path.join(OUT, "shot%03d.png" % state["shots"])
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def shown(value):
    """What the frame has of a linear value the program wrote: sRGB"""
    if value <= 0.0031308:
        return int(round(value * 12.92 * 255))
    return int(round((1.055 * value ** (1 / 2.4) - 0.055) * 255))


def near(a, b):
    return all(abs(x - y) < 40 for x, y in zip(a, b))


def run():
    try:
        doc = FreeCAD.newDocument(DOC)
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        view = FreeCADGui.getDocument(DOC).activeView()
        view.viewFront()
        view.fitAll()
        settle()
        middle = FreeCAD.Vector(5, 0, 5)
        stock = colour(view, middle)
        note("stock %s" % (stock,))

        for scope, name, inputs, rgb, comes in [(s,) + c for s in ("Object", "Instance")
                                                for c in CASES]:
            want = tuple(shown(float(v)) for v in rgb.split(","))
            program = doc.addObject("App::ShaderProgram", "Program")
            program.FragmentProgram = inputs + (BODY % rgb)
            shader = doc.addObject("App::Shader", "Shader")
            shader.Programs = [program]
            shader.Demo = "None"
            link = doc.addObject("App::Link", "Target")
            link.LinkedObject = box
            binding = doc.addObject("App::ShaderBinding", "Binding")
            binding.Scope = scope
            binding.ElementList = [shader, link]
            doc.recompute()
            link.ViewObject.Visibility = False
            # the compile is not waited for: the stock program stands in
            got = None
            until = time.time() + (20.0 if comes else 6.0)
            while time.time() < until:
                settle(0.5)
                got = colour(view, middle)
                if comes and near(got, want):
                    break
            if comes:
                check("%s: a program naming %s comes up" % (scope, name), near(got, want),
                      (got, want))
            else:
                check("%s: a program naming an unknown varying does not come up" % scope,
                      not near(got, want), (got, want))
                check("%s: and the box is drawn by the stock program" % scope,
                      near(got, stock), (got, stock))
            for obj in (binding, link, shader, program):
                doc.removeObject(obj.Name)
            doc.recompute()
            settle(0.6)
            back = colour(view, middle)
            check("%s: the binding of %s removed, the box is as it was" % (scope, name),
                  near(back, stock), (back, stock))
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
