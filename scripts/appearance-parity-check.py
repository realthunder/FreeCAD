# GUI check of docs/ShapeAppearanceDesign.md sec 14.6.3: what a
# Part::Feature's faces, edges and vertices look like is the object's
# (ElementAppearance, made by Part::Feature::updateAppearance()), and its
# view provider's ShapeAppearance, DiffuseColor, ShapeColor, LineColor,
# LineColorArray, PointColorArray and Map* properties are names over that.
# So a change made through the view provider, as every script and panel
# makes it, is the object's -- and after each one here the view provider's
# list is held against what the object draws, entry by entry, with nothing
# done in between to make them agree. One GUI run in a fresh user home,
# given this script at startup:
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-ap \
#     PARITY_OUT=/tmp/ap/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/appearance-parity-check.py
#
# It writes PASS/FAIL lines to $PARITY_OUT and exits.
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
    # The object's own names of the same (sec 14.6.1), against the view
    # provider's: two names over one value.
    for name in ("ShapeColor", "LineColor", "PointColor"):
        if rgb(getattr(obj, name)) != rgb(getattr(vp, name)):
            out.append("%s: %r/%r" % (name, rgb(getattr(vp, name)), rgb(getattr(obj, name))))
    if obj.Transparency != vp.Transparency:
        out.append("Transparency: %r/%r" % (vp.Transparency, obj.Transparency))
    mine = obj.ShapeAppearance
    if mine.Count != want.Count:
        out.append("ShapeAppearance: %d/%d entries" % (want.Count, mine.Count))
    else:
        for i in range(mine.Count):
            if look(mine[i]) != look(want[i]):
                out.append("ShapeAppearance[%d] of the object" % i)
    return out


def compare(doc, what):
    bad = {}
    objs = [obj for obj in doc.Objects if ours(obj) and not obj.Shape.isNull()]
    for obj in objs:
        d = diffs(obj)
        if d:
            bad[obj.Name] = d[:4] + (["... %d more" % (len(d) - 4)] if len(d) > 4 else [])
    check("%s: %d objects drawn as they are%s"
          % (what, len(objs), " -- view provider/object %r" % bad if bad else ""),
          not bad and len(objs) > 0)


def step(doc, what):
    doc.recompute()
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

    # The same by the object's own names, which is what the property editor
    # shows; the view provider's are names over them, and say so.
    box.ShapeColor = YELLOW
    box.Transparency = 30
    box.LineColor = CYAN
    box.PointColor = BLUE
    step(doc, "the object's own names written")
    check("and the view provider says what they say (%r, %r)" % (rgb(vp.ShapeColor), vp.Transparency),
          rgb(vp.ShapeColor) == YELLOW and vp.Transparency == 30 and rgb(vp.LineColor) == CYAN
          and rgb(vp.PointColor) == BLUE)
    box.ShapeAppearance[1] = App.Material(DiffuseColor=RED, Shininess=0.25)
    step(doc, "a face given a material through the object's list")
    check("which is the object's to state (%r)" % box.ElementAppearance.keys(),
          len(box.ElementAppearance.keys()) == 1 and rgb(box.ShapeColor) == YELLOW)
    legacy = [name for name in ("ShapeAppearance", "ShapeColor", "Transparency", "DiffuseColor",
                                "LineColor", "PointColor", "MapFaceColor")
              if "Legacy" not in vp.getPropertyStatus(name)]
    check("the view provider's names are names over the object's (%r), a line width is not (%r)"
          % (legacy, vp.getPropertyStatus("LineWidth")),
          not legacy and "Legacy" not in vp.getPropertyStatus("LineWidth"))
    App.closeDocument(doc.Name)

    # Faces coloured by their number, on a shape that has no names for them:
    # one nobody modelled, as an import's is. A box has names (Top, Left) and
    # is held by them.
    doc = App.newDocument("ParityNumber")
    box = doc.addObject("Part::Feature", "Box")
    box.Shape = Part.makeBox(10, 10, 10)
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
    compare(doc, "the cylinder wider")
    cyl.Placement.Base = V(5, 5, -10)
    doc.recompute()
    check("drilled through: a seventh face", len(c.Shape.Faces) == 7)
    compare(doc, "drilled through")
    step(doc, "drilled through")
    c.ViewObject.setElementColors({face(c, ZMin=10): YELLOW})
    step(doc, "a face of the cut painted by name")
    cyl.Radius = 2.5
    doc.recompute()
    compare(doc, "the cylinder narrower")
    # The source alone stated: what was made from it is told, no recompute
    box.ViewObject.ShapeColor = CYAN
    compare(doc, "the box given another colour, with no recompute")
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
    compare(doc, "read and made again")

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
    compare(doc, "a name taken from the first box, with no recompute")
    b.Placement.Base = V(4, 6, 5)
    doc.recompute()
    compare(doc, "a box moved")
    fuse.ViewObject.setElementColors({face(fuse, ZMax=0): CYAN})
    step(doc, "a face of the fuse painted")
    b.Placement.Base = V(5, 5, 4)
    doc.recompute()
    compare(doc, "a box moved again")

    # Painted in a transaction and undone: the object's, so an undo's.
    doc.UndoMode = 1
    was = {o.Name: colours(o) for o in doc.Objects if ours(o)}
    doc.openTransaction("paint")
    a.ViewObject.setElementColors({face(a, ZMax=0): GREEN, face(a, YMax=0): BLUE})
    doc.commitTransaction()
    now = {o.Name: colours(o) for o in doc.Objects if ours(o)}
    check("a face painted: the fuse draws it too (%r)" % now["Fuse"],
          BLUE in now["A"] and now != was)
    doc.undo()
    check("undone: every object draws what it drew (%r)" % colours(a),
          {o.Name: colours(o) for o in doc.Objects if ours(o)} == was)
    doc.redo()
    check("redone (%r)" % colours(a),
          {o.Name: colours(o) for o in doc.Objects if ours(o)} == now)
    compare(doc, "redone")
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
    # Which is the link's to hold (sec 14.6.4), under its own names too
    lea = link.ElementAppearance
    check("and the link holds it (%r)" % (rgb(lea.Face.DiffuseColor) if "Face" in lea else None,),
          link.OverrideMaterial and "Face" in lea and rgb(lea.Face.DiffuseColor) == GREEN
          and rgb(link.ShapeAppearance.Base.DiffuseColor) == GREEN)
    link.OverrideMaterial = False
    step(doc, "the link's own look taken away, by the object's name")
    check("the view provider follows, and the cut draws the box's again (%r)" % colours(c),
          not link.ViewObject.OverrideMaterial and "Face" not in lea
          and RED in colours(c) and GREEN not in colours(c))
    link.OverrideMaterial = True
    over = link.ShapeAppearance.Base
    over.DiffuseColor = YELLOW
    link.ShapeAppearance.Base = over
    step(doc, "and given another, by the object's names")
    check("the view provider says it, and the cut draws it (%r)" % colours(c),
          link.ViewObject.OverrideMaterial
          and rgb(link.ViewObject.ShapeAppearance.Base.DiffuseColor) == YELLOW
          and YELLOW in colours(c))
    # The look it would give, where it gives none, is the link's to hold as
    # well: the one it gave, or one it is given meanwhile
    link.ViewObject.OverrideMaterial = False
    check("the look it gave is the one it would give (%r)"
          % (rgb(link.ViewObject.ShapeAppearance.Base.DiffuseColor),),
          not link.OverrideMaterial and "Face" not in lea
          and rgb(link.ShapeAppearance.Base.DiffuseColor) == YELLOW
          and rgb(link.ViewObject.ShapeAppearance.Base.DiffuseColor) == YELLOW)
    over = link.ViewObject.ShapeAppearance.Base
    over.DiffuseColor = CYAN
    link.ViewObject.ShapeAppearance.Base = over
    step(doc, "a look given through the view provider while the link gives none")
    check("the object keeps it and gives none (%r)" % (rgb(link.ShapeAppearance.Base.DiffuseColor),),
          not link.OverrideMaterial and "Face" not in lea
          and rgb(link.ShapeAppearance.Base.DiffuseColor) == CYAN
          and RED in colours(c) and CYAN not in colours(c))
    kept = []
    for schema in (5, 4):
        doc.SaveSchemaVersion = schema
        kept.append(os.path.join(folder, "kept%d.FCStd" % schema))
        doc.saveAs(kept[-1])
    App.closeDocument(doc.Name)
    for schema, path in zip((5, 4), kept):
        doc = App.openDocument(path)
        link = doc.getObject("Link")
        vp = link.ViewObject
        check("read from a file at schema %d the link would give it still (%r)"
              % (schema, rgb(vp.ShapeAppearance.Base.DiffuseColor)),
              not vp.OverrideMaterial and "Face" not in link.ElementAppearance
              and rgb(vp.ShapeAppearance.Base.DiffuseColor) == CYAN
              and rgb(link.ShapeAppearance.Base.DiffuseColor) == CYAN)
        vp.OverrideMaterial = True
        check("and gives it (%r)" % colours(doc.getObject("Cut")),
              "Face" in link.ElementAppearance
              and rgb(link.ElementAppearance.Face.DiffuseColor) == CYAN
              and CYAN in colours(doc.getObject("Cut")))
        App.closeDocument(doc.Name)

    # An App::Part, which holds its looks as a link does. It has no material
    # card, and nothing puts one's look back over what its view provider is
    # given.
    doc = App.newDocument("ParityPart")
    box = doc.addObject("Part::Box", "Box")
    part = doc.addObject("App::Part", "Part")
    part.addObject(box)
    doc.recompute()
    pvp, pea = part.ViewObject, part.ElementAppearance
    over = pvp.ShapeAppearance.Base
    over.DiffuseColor = GREEN
    pvp.ShapeAppearance = (over,)
    check("a list of looks assigned to a part's view provider stays (%r)"
          % (rgb(pvp.ShapeAppearance.Base.DiffuseColor),),
          rgb(pvp.ShapeAppearance.Base.DiffuseColor) == GREEN and rgb(pvp.ShapeColor) == GREEN
          and not part.OverrideMaterial and "Face" not in pea)
    for colour in (RED, BLUE):
        pvp.setElementColors({"Face": colour})
        check("Set Colors' colour for all of a part is the part's (%r, %r)"
              % (rgb(pvp.ShapeAppearance.Base.DiffuseColor),
                 rgb(pea.Face.DiffuseColor) if "Face" in pea else None),
              pvp.OverrideMaterial and part.OverrideMaterial
              and rgb(pvp.ShapeAppearance.Base.DiffuseColor) == colour
              and rgb(pvp.ShapeColor) == colour
              and "Face" in pea and rgb(pea.Face.DiffuseColor) == colour
              and rgb(pvp.getElementColors().get("Face", (0, 0, 0))) == colour)
    pvp.setElementColors({})
    check("and taken away it is the one the part would give (%r)"
          % (rgb(part.ShapeAppearance.Base.DiffuseColor),),
          not pvp.OverrideMaterial and not part.OverrideMaterial and "Face" not in pea
          and rgb(part.ShapeAppearance.Base.DiffuseColor) == BLUE
          and rgb(pvp.ShapeAppearance.Base.DiffuseColor) == BLUE)
    App.closeDocument(doc.Name)

    # A shape with no history, as an import is: every face held by its
    # number, and many given a colour in one call.
    doc = App.newDocument("ParityMany")
    one = Part.makeBox(1, 1, 1)
    imp = doc.addObject("Part::Feature", "Imp")
    imp.Shape = Part.makeCompound([one.translated(V(2 * i, 0, 0)) for i in range(40)])
    doc.recompute()
    ivp = imp.ViewObject
    count = len(imp.Shape.Faces)

    def shade(i, k):
        return (round((i * 37 + k) % 255 / 255.0, 3), round((i * 91) % 255 / 255.0, 3), 0.5)

    def painted(which, k):
        return {"Face%d" % (i + 1): shade(i, k) for i in which}

    own = rgb(ivp.ShapeColor)
    for what, which, k in (("every face of an import given a colour by its name", range(count), 0),
                           ("other colours for a part of them", range(60, 200), 3),
                           ("and for a few", (5, 17, 230), 5)):
        ivp.setElementColors(painted(which, k))
        step(doc, what)
        got = [rgb(col) for col in ivp.DiffuseColor]
        want = [own] * count
        for i in which:
            want[i] = rgb(shade(i, k))
        check("%s: each face is the colour it was given, the rest the object's (%d faces)"
              % (what, count), got == want or (len(set(want)) == 1 and len(got) == 1))
        check("%s: held by number" % what,
              all(isinstance(key, int) or key.startswith("Face") for key in imp.ElementAppearance.keys())
              and len(imp.ElementAppearance.keys()) == len(list(which)))
    ivp.setElementColors({})
    step(doc, "and for none")
    check("nothing is stated of an element (%r)" % (imp.ElementAppearance.keys()[:3],),
          imp.ElementAppearance.keys() == [])
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
    compare(doc, "the pad longer")
    App.closeDocument(doc.Name)

    # The object's material card: the own look takes it, and the view
    # provider draws that.
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
            compare(doc, "a box given a material card")
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
    with open(OUT, "w") as fp:
        fp.write("\n".join(lines) + "\n")
    for doc in list(App.listDocuments().values()):
        App.closeDocument(doc.Name)
    QtWidgets.QApplication.instance().exit(0)
    os._exit(0)


QtCore.QTimer.singleShot(500, main)
