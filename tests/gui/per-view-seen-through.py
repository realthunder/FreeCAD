"""A view's own visibility of an object that others draw (Gui::SeenThrough).

What a plain group holds is drawn beside the group in the scene, not under
it, and the features of a PartDesign solid body draw the solid. A view's own
hide of such an object ended at nodes that draw nothing: the group hidden in
one view hid nothing there. The entry now also has the keys of what the
object is seen through, resolved with it and again when what it holds
changes.

Two 3D views of one document; the second must never see any of it. Each box
is watched at the middle of its front face, by a PICK and by the backend's
own frame against a spot of background.

Claims:
  - a path hide of a plain group in an App::Part hides its children there,
    a group in the group included, and leaves the Part's other child;
  - a child shown by name under the hidden group is shown: an entry given
    by name outranks one the group stands for;
  - a top-level group hides its children by its path ('Grp.') and, with
    PerViewVisibilities, by its bare name;
  - a box that joins the group while the view hides the group is hidden
    with it, and is back when it leaves;
  - a view's own SHOW of a group the document hides shows its children,
    except one the group remembers as hidden by hand, and one hidden by
    hand while that show stands goes with it;
  - a solid body hidden by a view hides the solid whichever feature shows
    it, the tip moved included, and leaves the other solid; shown by a view
    where the document hides the solid, its own shape is drawn;
  - a shader bound to the solid body dresses the solid through the feature
    that shows it (Scope 'Object'). Skipped where a user shader does not
    come up on a plain box either.

Scored against the tree before it: every 'is hidden' claim of a group and
of a solid body seen through a feature FAILs, and so does the shader's.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PerViewSeenThrough"
state = {"done": False, "shots": 0}

RED_FS = """$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex
#include <bgfx_shader.sh>
void main()
{
    gl_FragColor = vec4(1.0, 0.1, 0.1, 1.0);
}
"""


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


def frame(view):
    """The backend's own frame of the view"""
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    state["shots"] += 1
    path = os.path.join(OUT, "shot%03d.png" % state["shots"])
    view.saveRenderDump(path, "renderer")
    return QtGui.QImage(path)


def at(view, img, pt):
    x, y = view.getPointOnViewport(pt)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def differ(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) > 30


def seen(view, points, gap):
    """Which of the named points the view draws something at, and picks"""
    img = frame(view)
    ground = at(view, img, gap)
    drawn = sorted(n for n, p in points.items() if differ(at(view, img, p), ground))
    picked = []
    for n, p in points.items():
        x, y = view.getPointOnViewport(p)
        if view.getObjectInfo((int(x), int(y))):
            picked.append(n)
    return drawn, sorted(picked)


def red(view, pt):
    c = at(view, frame(view), pt)
    return c[0] > 200 and c[1] < 90 and c[2] < 90


def two_views(gdoc):
    v1 = gdoc.activeView()
    FreeCADGui.runCommand("Std_ViewCreate")
    settle()
    v2 = gdoc.activeView()
    if len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
        return None, None
    for v in (v1, v2):
        v.viewFront()
        v.fitAll()
    settle()
    return v1, v2


def groups():
    doc = FreeCAD.newDocument(DOC)

    def box(name, x):
        b = doc.addObject("Part::Box", name)
        b.Placement.Base = FreeCAD.Vector(x, 0, 0)
        return b

    asm = doc.addObject("App::Part", "Asm")
    grp = doc.addObject("App::DocumentObjectGroup", "Grp")
    asm.addObject(grp)
    grp.addObject(box("Box0", 0))
    grp.addObject(box("Box1", 20))
    inner = doc.addObject("App::DocumentObjectGroup", "Inner")
    grp.addObject(inner)
    inner.addObject(box("Box2", 40))
    asm.addObject(box("Out", 60))
    top = doc.addObject("App::DocumentObjectGroup", "TopGrp")
    top.addObject(box("TBox", 80))
    late = box("Late", 100)
    asm.addObject(late)
    doc.recompute()
    v1, v2 = two_views(FreeCADGui.getDocument(DOC))
    if not v1:
        note("ABORT no second 3D view")
        return False

    names = ["Box0", "Box1", "Box2", "Out", "TBox", "Late"]
    points = {n: FreeCAD.Vector(doc.getObject(n).Placement.Base.x + 5, 0, 5) for n in names}
    gap = FreeCAD.Vector(15, 0, 5)
    every = sorted(names)

    def without(*gone):
        return sorted(n for n in names if n not in gone)

    def expect(what, view, shown):
        drawn, picked = seen(view, points, gap)
        check(what + ": drawn", drawn == shown, drawn)
        check(what + ": picked", picked == shown, picked)

    expect("baseline", v1, every)
    expect("baseline, the other view", v2, every)

    v1.ObjectVisibilities = {"Asm.Grp.": "0"}
    settle()
    expect("a group in a Part hidden", v1, without("Box0", "Box1", "Box2"))
    expect("a group in a Part hidden, the other view", v2, every)

    v1.ObjectVisibilities = {"Asm.Grp.": "0", "Asm.Box1.": "1"}
    settle()
    expect("a child shown by name under the hidden group", v1, without("Box0", "Box2"))

    v1.ObjectVisibilities = {"Asm.Inner.": "0"}
    settle()
    expect("the group in the group hidden", v1, without("Box2"))

    v1.ObjectVisibilities = {"TopGrp.": "0"}
    settle()
    expect("a top-level group hidden by its path", v1, without("TBox"))

    v1.ObjectVisibilities = {"TopGrp": "0"}
    settle()
    expect("a bare entry while PerViewVisibilities is off", v1, every)
    v1.PerViewVisibilities = True
    settle()
    expect("a top-level group hidden by its bare name", v1, without("TBox"))
    v1.PerViewVisibilities = False

    # what the group holds changes while the view hides the group
    v1.ObjectVisibilities = {"Asm.Grp.": "0"}
    settle()
    grp.addObject(late)
    doc.recompute()
    settle(0.6)
    expect("a box that joined the hidden group", v1, without("Box0", "Box1", "Box2", "Late"))
    expect("a box that joined the hidden group, the other view", v2, every)
    grp.removeObject(late)
    doc.recompute()
    settle(0.6)
    expect("the box out of the group again", v1, without("Box0", "Box1", "Box2"))
    v1.ObjectVisibilities = {}
    settle()
    expect("no entry", v1, every)

    # a view's own show of a group the document hides
    doc.getObject("Box1").Visibility = False
    settle()
    grp.Visibility = False
    settle()
    expect("the group hidden in the document", v1, without("Box0", "Box1", "Box2"))
    v1.ObjectVisibilities = {"Asm.Grp.": "1"}
    settle(0.6)
    expect("the hidden group shown by the view", v1, without("Box1"))
    expect("the hidden group shown by the view, the other view", v2,
           without("Box0", "Box1", "Box2"))
    # and a child hidden by hand while that show stands: what the group
    # remembers is no link, and the entry is resolved again all the same
    grp.setElementVisible("Box0", False)
    settle(0.6)
    expect("a child hidden by hand under the group the view shows", v1,
           without("Box0", "Box1"))
    expect("a child hidden by hand, the other view", v2,
           without("Box0", "Box1", "Box2"))
    v1.ObjectVisibilities = {}
    settle()
    FreeCAD.closeDocument(DOC)
    return True


def solids():
    doc = FreeCAD.newDocument(DOC)
    body = doc.addObject("PartDesign::Body", "Body")

    def feature(name, x, newSolid=False):
        f = body.newObjectAt("PartDesign::AdditiveBox", name)
        f.Length = f.Width = f.Height = 10
        f.Placement.Base = FreeCAD.Vector(x, 0, 0)
        if newSolid:
            f.NewSolid = True
        # the one shown of its solid, as after the command that makes it
        f.Visibility = False
        f.Visibility = True
        return f

    a = feature("Box", 0)
    b = feature("Box001", 5)
    feature("Box002", 40, True)
    feature("Box003", 45)
    plain = doc.addObject("Part::Box", "Plain")
    plain.Placement.Base = FreeCAD.Vector(80, 0, 0)
    doc.recompute()
    solid = body.SolidBodies[0]
    key = "Body.%s." % solid.Name
    v1, v2 = two_views(FreeCADGui.getDocument(DOC))
    if not v1:
        note("ABORT no second 3D view")
        return False

    # where only the first box is, where only the second is, the other solid
    points = {
        "first": FreeCAD.Vector(2, 0, 5),
        "second": FreeCAD.Vector(13, 0, 5),
        "other": FreeCAD.Vector(47, 0, 5),
    }
    gap = FreeCAD.Vector(28, 0, 5)

    def expect(what, view, shown):
        drawn, picked = seen(view, points, gap)
        check(what + ": drawn", drawn == sorted(shown), drawn)
        check(what + ": picked", picked == sorted(shown), picked)

    every = ["first", "second", "other"]
    expect("baseline", v1, every)

    v1.ObjectVisibilities = {key: "0"}
    settle()
    expect("a solid body hidden, seen through its tip", v1, ["other"])
    expect("a solid body hidden, the other view", v2, every)

    solid.Tip = a
    a.Visibility = True
    doc.recompute()
    settle(0.6)
    expect("the tip moved, the other view", v2, ["first", "other"])
    expect("a solid body hidden, seen through an earlier feature", v1, ["other"])
    v1.ObjectVisibilities = {}
    settle()
    expect("no entry, seen through an earlier feature", v1, ["first", "other"])
    solid.Tip = b
    b.Visibility = True
    doc.recompute()
    settle(0.6)
    expect("the tip back", v1, every)

    # shown by the view where the document hides the solid
    b.Visibility = False
    settle()
    expect("the solid hidden in the document", v1, ["other"])
    v1.ObjectVisibilities = {key: "1"}
    settle(0.6)
    expect("the hidden solid shown by the view", v1, every)
    expect("the hidden solid shown by the view, the other view", v2, ["other"])
    v1.ObjectVisibilities = {}
    b.Visibility = True
    settle()

    # a shader bound to the solid body
    prog = doc.addObject("App::ShaderProgram", "RedProg")
    prog.FragmentProgram = RED_FS
    shader = doc.addObject("App::Shader", "Fx")
    shader.Programs = [prog]
    shader.Demo = "None"

    def bind(target):
        link = doc.addObject("App::Link", "Target")
        link.LinkedObject = target
        look = doc.addObject("App::ShaderBinding", "Look")
        look.Scope = "Object"
        look.ElementList = [shader, link]
        doc.recompute()
        link.ViewObject.Visibility = False
        return look, link

    def unbind(look, link):
        doc.removeObject(look.Name)
        doc.removeObject(link.Name)
        doc.recompute()
        settle(0.6)

    def comes_up(pt):
        until = time.time() + 20.0
        while time.time() < until:
            settle(0.5)
            if red(v1, pt):
                return True
        return False

    on_plain = FreeCAD.Vector(85, 0, 5)
    look, link = bind(plain)
    works = comes_up(on_plain)
    unbind(look, link)
    if not works:
        note("SKIP the shader claims: a user shader does not come up on a plain box here")
    else:
        check("the shader is gone from the plain box", not red(v1, on_plain))
        look, link = bind(solid)
        check("a shader on the solid body, seen through its tip", comes_up(points["second"]))
        check("the other solid is not dressed", not red(v1, points["other"]))
        solid.Tip = a
        a.Visibility = True
        doc.recompute()
        settle(0.6)
        check("a shader on the solid body, seen through an earlier feature",
              comes_up(points["first"]))
        unbind(look, link)
        check("the binding removed, the feature is as it was", not red(v1, points["first"]))
    FreeCAD.closeDocument(DOC)
    return True


def run():
    try:
        if groups():
            solids()
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
