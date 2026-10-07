# GUI check of docs/ShapeAppearanceDesign.md sec 14.6.9 step A: the object
# makes what its elements are drawn as (Part::Feature::updateAppearance(),
# kept in ElementAppearance) and nothing reads it yet -- the view provider
# still makes its own (ViewProviderPartExt::updateColors()). So the two are
# compared: the object's store is given what the view provider holds, and
# the faces, edges and vertices the object then makes are held against the
# view provider's ShapeAppearance, LineColorArray and PointColorArray, entry
# by entry. One GUI run in a fresh user home, given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-ap \
#     PARITY_OUT=/tmp/ap/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/appearance-parity-check.py
#
# It writes PASS/FAIL lines to $PARITY_OUT and exits. This check goes when
# the view provider draws what the object made (step B): there is then one
# list and nothing to compare.
import os, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
import Part
from FreeCAD import Vector as V
from PySide import QtCore, QtWidgets

OUT = os.environ["PARITY_OUT"]
lines = []
RED, GREEN, BLUE = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
YELLOW, CYAN = (1.0, 1.0, 0.0), (0.0, 1.0, 1.0)
FLAGS = ("MapFaceColor", "MapLineColor", "MapPointColor", "MapTransparency", "ForceMapColors")
FIELDS = ("AmbientColor", "SpecularColor", "EmissiveColor")
PLAIN = ("Shininess", "PBR", "Metallic", "Roughness", "Finish", "FinishPitch", "FinishDepth",
         "FinishAngle", "Texture", "Image", "ImagePath", "Uuid", "MaterialX")


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def rgb(c):
    return tuple(round(x, 3) for x in c[:3])


def look(m):
    # A material as it is drawn, field by field.
    out = [rgb(m.DiffuseColor), round(m.Transparency, 2)]
    out += [rgb(getattr(m, name)) for name in FIELDS]
    for name in PLAIN:
        value = getattr(m, name)
        out.append(round(value, 3) if isinstance(value, float) else value)
    return tuple(out)


def differing(a, b):
    names = ("DiffuseColor", "Transparency") + FIELDS + PLAIN
    return ["%s %r/%r" % (n, x, y) for n, x, y in zip(names, a, b) if x != y]


def entry(lst, i, fallback):
    # A list of one is every element that one; a list counted for another
    # shape says nothing of an element past its end.
    if lst.Count == 1:
        return lst.Base
    return lst[i] if i < lst.Count else fallback


def ours(obj):
    # Not by the property: a link answers for what it shows.
    return obj.isDerivedFrom("Part::Feature") and hasattr(obj.ViewObject, "ShapeAppearance")


def mapped(obj, kind):
    # Whether the view provider makes the elements of a kind from the
    # sources: what it holds of them by number is then not stated by anybody.
    vp = obj.ViewObject
    flag = {"Face": vp.MapFaceColor, "Edge": vp.MapLineColor, "Vertex": vp.MapPointColor}[kind]
    return flag and (vp.ForceMapColors or bool(vp.claimChildren()))


def state(obj):
    # What the view provider holds, as the object's store states it: the
    # object's own looks, the names with theirs, and -- where the view
    # provider does not make them from the sources -- the elements coloured
    # by their number.
    vp = obj.ViewObject
    sa = vp.ShapeAppearance
    stated = {"Face": sa.Base, "Edge": vp.LineMaterial, "Vertex": vp.PointMaterial}
    subs = list(obj.ColoredElements[1]) if obj.ColoredElements else []
    looks = vp.getElementAppearances()
    colours = bool(vp.MappedAppearance.Count) and vp.MappedAppearance.FollowMaterial
    named = set()
    for sub in subs:
        if sub not in looks:
            continue
        named.add(sub)
        m = looks[sub]
        stated[sub] = tuple(m.DiffuseColor[:3]) + (1.0 - m.Transparency,) if colours else m
    base = look(sa.Base)
    if not mapped(obj, "Face") and sa.Count > 1:
        for i in range(sa.Count):
            name = "Face%d" % (i + 1)
            if name in named or look(sa[i]) == base:
                continue
            m = sa[i]
            # A colour and no more where the rest of it is the object's
            if look(m)[2:] == base[2:]:
                stated[name] = tuple(m.DiffuseColor[:3]) + (1.0 - m.Transparency,)
            else:
                stated[name] = m
    for kind, colors, own in (("Edge", vp.LineColorArray, vp.LineColor),
                              ("Vertex", vp.PointColorArray, vp.PointColor)):
        if mapped(obj, kind) or len(colors) <= 1:
            continue
        for i, c in enumerate(colors):
            name = "%s%d" % (kind, i + 1)
            if name not in named and rgb(c) != rgb(own):
                stated[name] = tuple(c[:3]) + (1.0,)
    return stated


def sync(obj):
    vp = obj.ViewObject
    for flag in FLAGS:
        if getattr(obj, flag) != getattr(vp, flag):
            setattr(obj, flag, getattr(vp, flag))
    obj.ElementAppearance = state(obj)


def sync_all(doc):
    for obj in doc.Objects:
        if ours(obj):
            sync(obj)


def diffs(obj):
    # Where what the object made is not what the view provider made.
    vp = obj.ViewObject
    ea = obj.ElementAppearance
    out = []
    want, got = vp.ShapeAppearance, ea.Faces
    for i in range(len(obj.Shape.Faces)):
        w = look(entry(want, i, want.Base))
        g = look(entry(got, i, ea.Face)) if got.Count else look(ea.Face)
        if w != g:
            out.append("Face%d: %s" % (i + 1, "; ".join(differing(w, g))))
    for kind, colors, own, drawn, count in (
            ("Edge", vp.LineColorArray, vp.LineColor, ea.Edges, len(obj.Shape.Edges)),
            ("Vertex", vp.PointColorArray, vp.PointColor, ea.Vertices, len(obj.Shape.Vertexes))):
        mine = getattr(ea, kind)
        for i in range(count):
            w = rgb(colors[i]) if len(colors) > 1 and i < len(colors) else (
                rgb(colors[0]) if len(colors) == 1 else rgb(own))
            g = rgb((entry(drawn, i, mine) if drawn.Count else mine).DiffuseColor)
            if w != g:
                out.append("%s%d: %r/%r" % (kind, i + 1, w, g))
    return out


def remake(doc):
    # Every view provider that makes its faces from its sources made to do
    # so again, by a property whose change does that and no more (a body's
    # view provider passes the others on to its tip's). Twice: one that
    # takes from another made after it has that one's then.
    for _ in range(2):
        for obj in doc.Objects:
            if ours(obj) and any(mapped(obj, kind) for kind in ("Face", "Edge", "Vertex")):
                vp = obj.ViewObject
                vp.ForceMapColors = not vp.ForceMapColors
                vp.ForceMapColors = not vp.ForceMapColors


stale = []


def compare(doc, what):
    bad = {}
    late = []
    objs = [obj for obj in doc.Objects if ours(obj) and not obj.Shape.isNull()]
    seen = len(objs)
    unlike = [obj for obj in objs if diffs(obj)]
    if unlike:
        # A view provider that holds what it made of another shape, or at
        # another time, is not what the object is held against
        remake(doc)
    for obj in unlike:
        d = diffs(obj)
        if not d:
            late.append(obj.Name)
            continue
        bad[obj.Name] = d[:4] + (["... %d more" % (len(d) - 4)] if len(d) > 4 else [])
    if late:
        stale.append("%s: %s" % (what, ", ".join(late)))
    check("%s: %d objects drawn alike%s%s"
          % (what, seen,
             " -- once the view provider of %s made its own again" % ", ".join(late) if late else "",
             " -- view provider/object %r" % bad if bad else ""),
          not bad and seen > 0)


def step(doc, what):
    # After a change made as it is made today, through the view provider:
    # stated to the object's store, and what each made of it compared.
    doc.recompute()
    sync_all(doc)
    compare(doc, what)


def face(obj, **at):
    for i, f in enumerate(obj.Shape.Faces):
        if all(abs(getattr(f.BoundBox, k) - v) < 1e-6 for k, v in at.items()):
            return "Face%d" % (i + 1)
    return None


def colours(obj):
    # The colours the object draws its faces in, each once.
    ea = obj.ElementAppearance
    drawn = ea.Faces
    count = len(obj.Shape.Faces)
    return sorted({rgb((entry(drawn, i, ea.Face) if drawn.Count else ea.Face).DiffuseColor)
                   for i in range(count)})


def cut(name):
    # A box a cylinder is cut from, the cylinder below it: six faces.
    doc = App.newDocument(name)
    box = doc.addObject("Part::Box", "Box")
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 2
    cyl.Height = 30
    cyl.Placement.Base = V(5, 5, -40)
    c = doc.addObject("Part::Cut", "Cut")
    c.Base = box
    c.Tool = cyl
    doc.recompute()
    return doc


def run():
    folder = tempfile.mkdtemp(prefix="fc-parity-")

    # One object, nothing made from it.
    doc = App.newDocument("ParityBox")
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()
    vp = box.ViewObject
    check("a new object has its own look and nothing stated of its elements (%r)"
          % box.ElementAppearance,
          not box.ElementAppearance.keys() and box.ElementAppearance.Faces.Count == 1)
    step(doc, "a box as it is made")
    top, bottom, front = face(box, ZMin=10), face(box, ZMax=0), face(box, XMax=0)
    vp.setElementColors({top: RED, bottom: GREEN})
    step(doc, "two faces painted by name")
    check("and the object draws them (%r)" % colours(box), RED in colours(box) and GREEN in colours(box))
    vp.ShapeColor = BLUE
    step(doc, "the box given another colour")
    vp.Transparency = 40
    step(doc, "and made transparent")
    vp.Transparency = 0
    base = vp.ShapeAppearance.Base
    base.Shininess = 0.5
    vp.ShapeAppearance.Base = base
    step(doc, "the box given another gloss")
    looks = {k: v for k, v in vp.getElementAppearances().items() if k not in ("Face", "Edge", "Vertex")}
    looks[front] = App.Material(DiffuseColor=YELLOW, Shininess=0.25)
    vp.setElementAppearances(looks)
    step(doc, "a face given a material by name")
    base.Shininess = 0.7
    vp.ShapeAppearance.Base = base
    step(doc, "the gloss changed under it")
    vp.setElementColors({top: RED})
    step(doc, "names taken away")
    vp.setElementColors({"Face%d" % (i + 1): RED for i in range(6)})
    step(doc, "six faces painted alike by name")
    vp.setElementColors({})
    step(doc, "no names")
    vp.LineColor = GREEN
    vp.PointColor = RED
    step(doc, "edges and vertices given colours")
    App.closeDocument(doc.Name)

    # Faces coloured by their number, on a shape that has no names for them.
    doc = App.newDocument("ParityNumber")
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()
    vp = box.ViewObject
    colors = [vp.ShapeColor[:3] + (1.0,)] * 6
    colors[1] = RED + (1.0,)
    colors[4] = GREEN + (0.5,)
    vp.DiffuseColor = colors
    step(doc, "two faces coloured by number")
    check("held by number, not by name (%r)" % box.ElementAppearance,
          not box.ElementAppearance.Names and len(box.ElementAppearance.keys()) == 2)
    vp.ShapeColor = BLUE
    step(doc, "the object given another colour under them")
    edges = [vp.LineColor[:3] + (1.0,)] * len(box.Shape.Edges)
    edges[2] = RED + (1.0,)
    vp.LineColorArray = edges
    step(doc, "an edge coloured by number")
    App.closeDocument(doc.Name)

    # A cut: its faces take the looks of the faces they were made from.
    doc = cut("ParityCut")
    box, cyl, c = doc.Box, doc.Cyl, doc.Cut
    step(doc, "a cut as it is made")
    box.ViewObject.ShapeColor = RED
    cyl.ViewObject.ShapeColor = BLUE
    step(doc, "its box red, its cylinder blue")
    check("the cut draws the box's, which is all it is made of yet (%r)" % colours(c),
          colours(c) == [RED])
    box.ViewObject.setElementColors({face(box, XMin=10): GREEN})
    step(doc, "a face of the box painted")
    check("the cut draws that too (%r)" % colours(c), GREEN in colours(c))
    cyl.Radius = 3
    doc.recompute()
    compare(doc, "the cylinder wider, nothing stated again")
    cyl.Placement.Base = V(5, 5, -10)
    doc.recompute()
    check("drilled through: a seventh face", len(c.Shape.Faces) == 7)
    compare(doc, "drilled through, nothing stated again")
    step(doc, "drilled through")
    c.ViewObject.setElementColors({face(c, ZMin=10): YELLOW})
    step(doc, "a face of the cut painted by name")
    cyl.Radius = 2.5
    doc.recompute()
    compare(doc, "the cylinder narrower, nothing stated again")
    # The source alone stated: what was made from it is told, no recompute
    box.ViewObject.ShapeColor = CYAN
    sync(box)
    compare(doc, "the box given another colour, and stated alone")
    check("the cut follows (%r)" % colours(c), CYAN in colours(c) and RED not in colours(c))
    box.ViewObject.Transparency = 50
    step(doc, "the box made transparent")
    c.ViewObject.MapTransparency = True
    step(doc, "the cut taking transparency")
    c.ViewObject.MapTransparency = False
    c.ViewObject.MapFaceColor = False
    step(doc, "the cut taking no colours")
    c.ViewObject.MapFaceColor = True
    step(doc, "the cut taking colours again")
    box.ViewObject.Transparency = 0
    box.ViewObject.LineColor = RED
    c.ViewObject.MapLineColor = True
    c.ViewObject.MapPointColor = True
    box.ViewObject.PointColor = GREEN
    step(doc, "the cut taking the colours of edges and vertices")
    # A whole material handed on
    looks = {k: v for k, v in box.ViewObject.getElementAppearances().items()
             if k not in ("Face", "Edge", "Vertex")}
    looks[face(box, XMax=0)] = App.Material(DiffuseColor=YELLOW, Shininess=0.25)
    box.ViewObject.setElementAppearances(looks)
    step(doc, "a face of the box given a material")
    glossy = [round(entry(c.ElementAppearance.Faces, i, c.ElementAppearance.Face).Shininess, 3)
              for i in range(len(c.Shape.Faces))]
    check("the cut's face takes the whole of it (%r)" % sorted(set(glossy)), 0.25 in glossy)

    # Saved and read: what is drawn is in the file, and is not made again.
    path = os.path.join(folder, "ParityCut.FCStd")
    doc.saveAs(path)
    was = {o.Name: colours(o) for o in doc.Objects if ours(o)}
    App.closeDocument(doc.Name)
    doc = App.openDocument(path)
    compare(doc, "saved and read")
    check("and the object draws what it drew (%r)" % {o.Name: colours(o) for o in doc.Objects if ours(o)},
          {o.Name: colours(o) for o in doc.Objects if ours(o)} == was)
    doc.Cyl.Radius = 2
    doc.recompute()
    compare(doc, "read and made again, nothing stated again")

    # A copy of the cut's shape, made once (Part.show): what it takes is
    # stated, by number.
    shown = Part.show(doc.Cut)
    doc.recompute()
    check("a copy made once keeps the looks by number (%r)" % shown.ElementAppearance,
          colours(shown) == colours(doc.Cut) and len(shown.ElementAppearance.keys()) > 0)
    step(doc, "a copy made once")
    App.closeDocument(doc.Name)

    # A boolean of painted boxes, and what is made of it in turn.
    doc = App.newDocument("ParityFuse")
    a = doc.addObject("Part::Box", "A")
    b = doc.addObject("Part::Box", "B")
    b.Placement.Base = V(5, 5, 5)
    doc.recompute()
    a.ViewObject.ShapeColor = RED
    b.ViewObject.ShapeColor = BLUE
    a.ViewObject.setElementColors({face(a, ZMax=0): GREEN, face(a, XMax=0): YELLOW})
    b.ViewObject.setElementColors({face(b, ZMin=15): CYAN})
    fuse = doc.addObject("Part::Fuse", "Fuse")
    fuse.Base = a
    fuse.Tool = b
    step(doc, "a fuse of two painted boxes")
    check("the fuse draws all five colours (%r)" % colours(fuse),
          set(colours(fuse)) >= {RED, BLUE, GREEN, YELLOW, CYAN})
    fillet = doc.addObject("Part::Fillet", "Fillet")
    fillet.Base = fuse
    fillet.Edges = [(i + 1, 1.0, 1.0) for i in range(4)]
    step(doc, "a fillet of the fuse")
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 2
    cyl.Height = 40
    cyl.Placement.Base = V(3, 3, -10)
    cyl.ViewObject.ShapeColor = (1.0, 0.0, 1.0)
    c = doc.addObject("Part::Cut", "Cut")
    c.Base = fillet
    c.Tool = cyl
    step(doc, "a cut of the fillet")
    a.ViewObject.setElementColors({face(a, ZMax=0): GREEN})
    sync(a)
    compare(doc, "a name taken from the first box, and stated alone")
    b.Placement.Base = V(4, 6, 5)
    doc.recompute()
    compare(doc, "a box moved, nothing stated again")
    fuse.ViewObject.setElementColors({face(fuse, ZMax=0): CYAN})
    step(doc, "a face of the fuse painted")
    b.Placement.Base = V(5, 5, 4)
    doc.recompute()
    compare(doc, "a box moved again, nothing stated again")

    # Stated in a transaction and undone: the object's alone, since what a
    # view provider holds is not an undo's to put back as things are.
    doc.UndoMode = 1
    was = {o.Name: colours(o) for o in doc.Objects if ours(o)}
    doc.openTransaction("paint")
    a.ElementAppearance[face(a, YMax=0)] = BLUE
    doc.commitTransaction()
    now = {o.Name: colours(o) for o in doc.Objects if ours(o)}
    check("a face given a colour through the store: the fuse draws it too (%r)" % now["Fuse"],
          BLUE in now["A"] and now != was)
    doc.undo()
    check("undone: every object draws what it drew (%r)" % colours(a),
          {o.Name: colours(o) for o in doc.Objects if ours(o)} == was)
    doc.redo()
    check("redone (%r)" % colours(a),
          {o.Name: colours(o) for o in doc.Objects if ours(o)} == now)
    doc.undo()
    compare(doc, "and undone again")
    App.closeDocument(doc.Name)

    # A link in the way: what it lays over what it shows is taken.
    doc = App.newDocument("ParityLink")
    box = doc.addObject("Part::Box", "Box")
    box.ViewObject.ShapeColor = RED
    link = doc.addObject("App::Link", "Link")
    link.LinkedObject = box
    link.Placement.Base = V(5, 5, 5)
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 2
    cyl.Height = 40
    cyl.Placement.Base = V(8, 8, -10)
    c = doc.addObject("Part::Cut", "Cut")
    c.Base = link
    c.Tool = cyl
    step(doc, "a cut of a link to a red box")
    check("the cut draws the box's colour (%r)" % colours(c), RED in colours(c))
    link.ViewObject.OverrideMaterial = True
    over = link.ViewObject.ShapeAppearance.Base
    over.DiffuseColor = GREEN
    link.ViewObject.ShapeAppearance.Base = over
    step(doc, "the link given a colour of its own")
    check("the cut draws the link's (%r)" % colours(c), GREEN in colours(c))
    App.closeDocument(doc.Name)

    # A body: a pad and a pocket.
    doc = App.newDocument("ParityBody")
    body = doc.addObject("PartDesign::Body", "Body")
    sketch = body.newObject("Sketcher::SketchObject", "Sketch")
    sketch.Support = (doc.getObject("XY_Plane"), [""]) if doc.getObject("XY_Plane") else None
    for p, q in (((0, 0), (10, 0)), ((10, 0), (10, 10)), ((10, 10), (0, 10)), ((0, 10), (0, 0))):
        sketch.addGeometry(Part.LineSegment(V(p[0], p[1], 0), V(q[0], q[1], 0)))
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sketch
    pad.Length = 10
    doc.recompute()
    step(doc, "a body with a pad")
    pad.ViewObject.setElementColors({face(pad, ZMin=10): RED})
    step(doc, "a face of the pad painted")
    hole = body.newObject("Sketcher::SketchObject", "Hole")
    hole.addGeometry(Part.Circle(V(5, 5, 0), V(0, 0, 1), 2))
    pocket = body.newObject("PartDesign::Pocket", "Pocket")
    pocket.Profile = hole
    pocket.Type = 1
    pocket.Reversed = True
    doc.recompute()
    step(doc, "a pocket through the pad")
    body.ViewObject.ShapeColor = BLUE
    step(doc, "the body given a colour")
    pad.Length = 12
    doc.recompute()
    compare(doc, "the pad longer, nothing stated again")
    App.closeDocument(doc.Name)

    # The object's material card: the own look takes it, with no view
    # provider's help, as the view provider's does.
    try:
        import Materials
        manager = Materials.MaterialManager()
        doc = App.newDocument("ParityCard")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        was = look(box.ViewObject.ShapeAppearance.Base)
        found = None
        for uuid in list(manager.Materials)[:200]:
            try:
                box.ShapeMaterial = manager.getMaterial(uuid)
            except Exception:
                continue
            if look(box.ViewObject.ShapeAppearance.Base) != was:
                found = uuid
                break
        if found is None:
            lines.append("SKIP no material card that says anything of a look")
        else:
            doc.recompute()
            check("the object's own look is the card's (%r)" % (rgb(box.ElementAppearance.Face.DiffuseColor),),
                  look(box.ElementAppearance.Face) == look(box.ViewObject.ShapeAppearance.Base))
        App.closeDocument(doc.Name)
    except ImportError:
        lines.append("SKIP no Materials module")


def main():
    try:
        run()
    except Exception:
        lines.append("FAIL exception\n" + traceback.format_exc())
    for item in stale:
        lines.append("NOTE the view provider was behind the object -- " + item)
    with open(OUT, "w") as fp:
        fp.write("\n".join(lines) + "\n")
    for doc in list(App.listDocuments().values()):
        App.closeDocument(doc.Name)
    QtWidgets.QApplication.instance().exit(0)
    os._exit(0)


QtCore.QTimer.singleShot(500, main)
