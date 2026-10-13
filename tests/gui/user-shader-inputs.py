"""A user fragment stage names the varyings it reads, and no more.

A fragment program written for the stock vertex stage says what it reads
of it: '$input v_normal, v_color0, v_vpos' is what docs/RenderDebug.md
shows and what the bundled effects have. Direct3D pairs the two stages by
position, not by name, so that list -- v_vpos third where the vertex stage
has it fifth -- did not link: "user shader: program link failed", and the
object went on being drawn by the stock program. The renderer now hands the
compile the vertex stage's whole list in its order (BGFXRenderer.cpp,
completeInputs()).

Claims, each by the backend's own frame at the middle of a box:
  - a program naming three varyings comes up;
  - so does one naming one, and one naming none;
  - one naming the whole list is as before;
  - one naming a varying the vertex stage does not hand on does not come
    up, and takes nothing else down: the box is drawn by the stock program.

Scored against the tree before it, on Direct3D: the first two claims FAIL.
On OpenGL all of it passed before as well.
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

        for name, inputs, rgb, comes in CASES:
            want = tuple(shown(float(v)) for v in rgb.split(","))
            program = doc.addObject("App::ShaderProgram", "Program")
            program.FragmentProgram = inputs + (BODY % rgb)
            shader = doc.addObject("App::Shader", "Shader")
            shader.Programs = [program]
            shader.Demo = "None"
            link = doc.addObject("App::Link", "Target")
            link.LinkedObject = box
            binding = doc.addObject("App::ShaderBinding", "Binding")
            binding.Scope = "Object"
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
                check("a program naming %s comes up" % name, near(got, want), (got, want))
            else:
                check("a program naming an unknown varying does not come up",
                      not near(got, want), (got, want))
                check("and the box is drawn by the stock program", near(got, stock),
                      (got, stock))
            for obj in (binding, link, shader, program):
                doc.removeObject(obj.Name)
            doc.recompute()
            settle(0.6)
            back = colour(view, middle)
            check("the binding of %s removed, the box is as it was" % name,
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
