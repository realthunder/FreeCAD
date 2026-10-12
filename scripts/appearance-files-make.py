# appearance-files-make.py : one document of looks, made the way a script
# written for any FreeCAD makes them -- through the view providers -- and
# saved. What data/tests/LooksByUpstream114.FCStd and LooksByFork2025.FCStd
# were written by, each in a release image of the build it is named for, and
# what scripts/appearance-files-check.py reads them against
# (docs/ShapeAppearanceDesign.md sec 14.6.10, step E). A GUI run, given this
# script at startup:
#
#   E_FILE=/tmp/looks.FCStd E_BUILD=up QT_QPA_PLATFORM=offscreen freecad \
#     appearance-files-make.py
#
#   E_FILE    the file to save
#   E_BUILD   "here" for this build, which then saves at
#   E_SCHEMA  the SaveSchemaVersion, 4 by default
# What a build could not be asked for is said in <file>.made.txt: upstream has
# no look of its own on an App::Part.
import FreeCAD as App, FreeCADGui as Gui, Part, os, traceback
from FreeCAD import Vector as V

PATH = os.environ["E_FILE"]
BUILD = os.environ.get("E_BUILD", "?")
notes = []

RED = (1.0, 0.0, 0.0)
GREEN = (0.0, 1.0, 0.0)
BLUE = (0.0, 0.0, 1.0)
YELLOW = (1.0, 1.0, 0.0)
MAGENTA = (1.0, 0.0, 1.0)
CYAN = (0.0, 1.0, 1.0)
ORANGE = (1.0, 0.5, 0.0)
WHITE = (1.0, 1.0, 1.0)


def case(name):
    def deco(fn):
        try:
            fn()
            notes.append("made %s" % name)
        except Exception:
            notes.append("NOT made %s: %s" % (name, traceback.format_exc().splitlines()[-1]))
        return fn

    return deco


def own_colour(vp, colour):
    # A link's own look: ShapeMaterial where a build has that, else the list
    if "ShapeMaterial" in vp.PropertiesList:
        m = vp.ShapeMaterial
        m.DiffuseColor = colour
        vp.ShapeMaterial = m
    else:
        m = vp.ShapeAppearance[0]
        m.DiffuseColor = colour
        vp.ShapeAppearance = (m,)


doc = App.newDocument("Looks")
x = [0]


def at(obj):
    obj.Placement.Base = V(x[0], 0, 0)
    x[0] += 30
    return obj


@case("Plain: a colour, a transparency, edges and vertices")
def _():
    o = at(doc.addObject("Part::Box", "Plain"))
    doc.recompute()
    vp = o.ViewObject
    vp.ShapeColor = RED
    vp.Transparency = 40
    vp.LineColor = BLUE
    vp.PointColor = GREEN


@case("Faces: one face of six another colour, by number")
def _():
    o = at(doc.addObject("Part::Box", "Faces"))
    doc.recompute()
    vp = o.ViewObject
    vp.ShapeColor = RED
    cols = [RED] * 6
    cols[2] = BLUE
    vp.DiffuseColor = cols


@case("Edges: one edge of twelve another colour")
def _():
    o = at(doc.addObject("Part::Box", "Edges"))
    doc.recompute()
    vp = o.ViewObject
    vp.LineColor = BLUE
    cols = [BLUE] * 12
    cols[4] = ORANGE
    vp.LineColorArray = cols


@case("Named: a face painted by its name (setElementColors)")
def _():
    o = at(doc.addObject("Part::Box", "Named"))
    doc.recompute()
    vp = o.ViewObject
    vp.ShapeColor = YELLOW
    vp.setElementColors({"Face3": BLUE, "Face5": GREEN})


@case("Cut: made of a yellow box and a magenta cylinder")
def _():
    b = at(doc.addObject("Part::Box", "CutBox"))
    c = doc.addObject("Part::Cylinder", "CutCyl")
    c.Radius = 3
    c.Height = 20
    c.Placement.Base = b.Placement.Base + V(5, 5, -5)
    doc.recompute()
    b.ViewObject.ShapeColor = YELLOW
    c.ViewObject.ShapeColor = MAGENTA
    cut = doc.addObject("Part::Cut", "Cut")
    cut.Base = b
    cut.Tool = c
    doc.recompute()


@case("Imp: a shape with no history, every face a colour by number")
def _():
    box = Part.makeBox(4, 4, 4)
    shape = Part.makeCompound([box.translated(V(6 * i, 0, 0)) for i in range(3)])
    o = at(doc.addObject("Part::Feature", "Imp"))
    o.Shape = shape
    doc.recompute()
    pal = [RED, GREEN, BLUE, YELLOW, MAGENTA, CYAN]
    o.ViewObject.DiffuseColor = [pal[i % 6] for i in range(18)]


@case("Link: a link with a look of its own")
def _():
    src = at(doc.addObject("Part::Box", "LinkSrc"))
    doc.recompute()
    src.ViewObject.ShapeColor = RED
    link = at(doc.addObject("App::Link", "Link"))
    link.LinkedObject = src
    doc.recompute()
    vp = link.ViewObject
    own_colour(vp, GREEN)
    vp.OverrideMaterial = True


@case("LinkFace: a link that colours one face of what it shows")
def _():
    src = doc.getObject("LinkSrc")
    link = at(doc.addObject("App::Link", "LinkFace"))
    link.LinkedObject = src
    doc.recompute()
    link.ViewObject.setElementColors({"Face3": BLUE})


@case("LinkBoth: a look of its own and one face over it")
def _():
    src = doc.getObject("LinkSrc")
    link = at(doc.addObject("App::Link", "LinkBoth"))
    link.LinkedObject = src
    doc.recompute()
    vp = link.ViewObject
    own_colour(vp, CYAN)
    vp.OverrideMaterial = True
    vp.setElementColors({"Face1": ORANGE})


@case("Array: three of a link, the second given a material")
def _():
    src = doc.getObject("LinkSrc")
    link = at(doc.addObject("App::Link", "Array"))
    link.LinkedObject = src
    link.ShowElement = False
    link.ElementCount = 3
    link.PlacementList = [App.Placement(V(0, 12 * i, 0), App.Rotation()) for i in range(3)]
    doc.recompute()
    vp = link.ViewObject
    m = App.Material()
    m.DiffuseColor = MAGENTA
    plain = App.Material()
    vp.MaterialList = [plain, m, plain]
    vp.OverrideMaterialList = [False, True, False]


@case("ArrayShown: three of a link shown as elements, the third given a look")
def _():
    src = doc.getObject("LinkSrc")
    link = at(doc.addObject("App::Link", "ArrayShown"))
    link.LinkedObject = src
    link.ShowElement = True
    link.ElementCount = 3
    link.PlacementList = [App.Placement(V(0, 12 * i, 0), App.Rotation()) for i in range(3)]
    doc.recompute()
    vp = link.ElementList[2].ViewObject
    own_colour(vp, YELLOW)
    vp.OverrideMaterial = True


@case("Group: an App::Part with a box in it, given a look of its own")
def _():
    part = at(doc.addObject("App::Part", "Group"))
    inner = doc.addObject("Part::Box", "GroupBox")
    part.addObject(inner)
    doc.recompute()
    inner.ViewObject.ShapeColor = WHITE
    vp = part.ViewObject
    own_colour(vp, ORANGE)
    vp.OverrideMaterial = True


@case("GroupFace: an App::Part that colours a face of a box in it")
def _():
    part = at(doc.addObject("App::Part", "GroupFace"))
    inner = doc.addObject("Part::Box", "GroupFaceBox")
    part.addObject(inner)
    doc.recompute()
    inner.ViewObject.ShapeColor = WHITE
    part.ViewObject.setElementColors({"GroupFaceBox.Face6": BLUE})


@case("Body: a body of a box and a cylinder cut from it, the body given a colour")
def _():
    body = at(doc.addObject("PartDesign::Body", "Body"))
    box = body.newObject("PartDesign::AdditiveBox", "BodyBox")
    cyl = body.newObject("PartDesign::SubtractiveCylinder", "BodyCyl")
    cyl.Radius = 2
    cyl.Height = 20
    doc.recompute()
    body.ViewObject.ShapeColor = CYAN


doc.recompute()
if BUILD == "here":
    doc.SaveSchemaVersion = int(os.environ.get("E_SCHEMA", "4"))
    notes.append("SaveSchemaVersion %d" % doc.SaveSchemaVersion)
try:
    doc.saveAs(PATH)
    notes.append("saved %s" % os.path.basename(PATH))
except Exception:
    notes.append("NOT saved: " + traceback.format_exc())
with open(PATH + ".made.txt", "w") as f:
    f.write("\n".join(notes) + "\n")
os._exit(0)
