# GUI check of docs/ShapeAppearanceDesign.md sec 14.6.11: a view provider that
# shows a shape through a link's child view provider (ChildViewProvider), as
# FreeCAD_assembly3's part group and Draft's fused link arrays do.
#
# Two kinds. A child on a Part::Feature -- both of those -- is a name over the
# object's looks, as the object's own view provider would be. A child on an
# object that is no Part::Feature, a plain App::Link here, has no object to
# keep its looks and keeps them itself, as every view provider did before:
# its properties are values, the names of the elements given a colour are the
# link's ColoredElements and their colours its MappedColors.
#
# What is expected is what this fork's release image of 2025-10-15 showed of
# the same steps, where that was one thing: the faces a name paints, the faces
# taken from what the link shows, what a file keeps. Where that build left a
# face in the colour of a name taken away, the face is the object's again
# here, as it is for every other view provider.
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-cv \
#     CHILDCHECK_OUT=/tmp/cv/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/appearance-child-check.py
#
# It writes PASS/FAIL lines to $CHILDCHECK_OUT and exits.
import os, tempfile, traceback
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["CHILDCHECK_OUT"]
lines = []

RED, GREEN, BLUE, YELLOW = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (1.0, 1.0, 0.0)
NAMES = {RED: "R", GREEN: "G", BLUE: "B", YELLOW: "Y"}


def check(what, ok, got=None):
    lines.append(
        ("PASS " if ok else "FAIL ") + what + ("" if ok or got is None else " -- %s" % (got,))
    )


def rgb(c):
    return tuple(round(x, 2) for x in c[:3])


def faces(vp):
    # A letter a face, and how far it is seen through in percent where it is
    res = []
    for c in vp.DiffuseColor:
        seen = int(round((1.0 - c[3]) * 100))
        res.append(NAMES.get(rgb(c), str(rgb(c))) + (str(seen) if seen else ""))
    return " ".join(res)


def names(vp):
    return sorted(
        (k, NAMES.get(rgb(v), rgb(v)))
        for k, v in vp.getElementColors().items()
        if k not in ("Face", "Edge", "Vertex")
    )


def dismiss():
    w = QtWidgets.QApplication.activeModalWidget()
    if w is not None:
        w.reject()


def opened(path):
    import time

    doc = App.openDocument(path)
    until = time.time() + 60
    while time.time() < until and any(obj.ViewObject is None for obj in doc.Objects):
        QtWidgets.QApplication.processEvents()
    for i in range(20):
        QtWidgets.QApplication.processEvents()
    return doc


class Group:
    """A Part::FeaturePython that is a link, made as Assembly3 makes its part group."""

    def __init__(self):
        self.Object = None

    def getViewProviderName(self, _obj):
        return "Gui::ViewProviderLinkPython"

    def attach(self, obj):
        self.Object = obj
        obj.addExtension("App::LinkExtensionPython")
        obj.addProperty("App::PropertyLinkList", "Group", "Base", "")
        obj.addProperty("App::PropertyPlacementList", "PlacementList", "Base", "")
        obj.addProperty("App::PropertyBoolList", "VisibilityList", "Base", "")
        obj.configLinkProperty("Placement", "PlacementList", "VisibilityList", ElementList="Group")

    def execute(self, obj):
        import Part

        shapes = [Part.getShape(o) for o in obj.Group]
        obj.Shape = shapes[0].fuse(shapes[1:]) if len(shapes) > 1 else Part.makeCompound(shapes)
        return True

    def __getstate__(self):
        return None

    def __setstate__(self, _state):
        return None


def bound(tmp):
    # A child on a Part::Feature made with its proxy (addObject's attach form)
    doc = App.newDocument("ChildBound")
    plain = doc.addObject("Part::FeaturePython", "Plain")
    box = doc.addObject("Part::Box", "Box")
    cyl = doc.addObject("Part::Cylinder", "Cyl")
    cyl.Radius = 3
    cyl.Height = 20
    cyl.Placement.Base = App.Vector(5, 5, -5)
    doc.recompute()
    box.ViewObject.ShapeColor = RED
    box.ViewObject.setElementColors({"Face6": GREEN})
    cyl.ViewObject.ShapeColor = BLUE
    cyl.ViewObject.Transparency = 40
    group = doc.addObject("Part::FeaturePython", "Group", Group(), None, True)
    group.ViewObject.Proxy = 0
    group.Group = [box, cyl]
    doc.recompute()
    check(
        "an object added with its proxy has the look of any new object",
        rgb(group.ElementAppearance.Face.DiffuseColor)
        == rgb(plain.ElementAppearance.Face.DiffuseColor)
        and rgb(group.ShapeColor) == rgb(plain.ShapeColor),
        (rgb(group.ShapeColor), rgb(plain.ShapeColor)),
    )
    vp = group.ViewObject
    vp.ChildViewProvider = "PartGui::ViewProviderPartExt"
    ch = vp.ChildViewProvider
    check(
        "a child made on it is that colour, as a new box is",
        rgb(ch.ShapeColor) == rgb(plain.ViewObject.ShapeColor),
        (rgb(ch.ShapeColor), rgb(plain.ViewObject.ShapeColor)),
    )
    ch.MapTransparency = True
    ch.MapFaceColor = True
    ch.ForceMapColors = True
    vp.DefaultMode = 1
    doc.recompute()
    check("its faces are those of the parts", faces(ch) == "R R G R R R B40 B40 B40 B40", faces(ch))
    box.ViewObject.ShapeColor = YELLOW
    doc.recompute()
    check(
        "a part given another colour is followed",
        faces(ch) == "Y Y G Y Y Y B40 B40 B40 B40",
        faces(ch),
    )
    ch.setElementColors({"Face1": BLUE})
    check(
        "a face of the child painted by name is the object's to keep",
        faces(ch) == "B Y G Y Y Y B40 B40 B40 B40" and group.ElementAppearance.keys() == ["Face1"],
        (faces(ch), group.ElementAppearance.keys()),
    )
    ch.LineWidth = 4
    ch.MapFaceColor = False
    ch.ForceMapColors = False
    ch.ShapeColor = GREEN
    doc.recompute()
    check(
        "with the parts not asked the faces are the child's colour",
        faces(ch) == "B G G G G G G G G G",
        faces(ch),
    )
    ch.MapFaceColor = True
    ch.ForceMapColors = True
    doc.recompute()
    check("asked again they are the parts'", faces(ch) == "B Y G Y Y Y B40 B40 B40 B40", faces(ch))
    for schema in (5, 4):
        doc.SaveSchemaVersion = schema
        path = os.path.join(tmp, "bound%d.FCStd" % schema)
        doc.saveCopy(path)
        other = opened(path)
        c = other.getObject("Group").ViewObject.ChildViewProvider
        check(
            "schema %d keeps the child and its looks" % schema,
            c is not None
            and faces(c) == "B Y G Y Y Y B40 B40 B40 B40"
            and c.LineWidth == 4
            and rgb(c.ShapeColor) == GREEN
            and c.ForceMapColors,
            None if c is None else (faces(c), c.LineWidth, rgb(c.ShapeColor)),
        )
        App.closeDocument(other.Name)
    App.closeDocument(doc.Name)


def unbound(tmp):
    # A child on a plain App::Link: no Part::Feature to keep its looks
    doc = App.newDocument("ChildUnbound")
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()
    box.ViewObject.ShapeColor = BLUE
    box.ViewObject.setElementColors({"Face1": YELLOW})
    link = doc.addObject("App::Link", "Plain")
    link.LinkedObject = box
    link.Placement.Base = App.Vector(0, 40, 0)
    doc.recompute()
    vp = link.ViewObject
    vp.ChildViewProvider = "PartGui::ViewProviderPartExt"
    vp.DefaultMode = 1
    ch = vp.ChildViewProvider
    fresh = rgb(ch.ShapeColor)
    check(
        "a new child is one colour, and not the box's",
        len(ch.DiffuseColor) == 1 and fresh != BLUE,
        faces(ch),
    )
    ch.ShapeColor = RED
    ch.Transparency = 30
    ch.LineWidth = 5
    check("its colour and transparency are its own values", faces(ch) == "R30", faces(ch))
    ch.setElementColors({"Face3": GREEN})
    check(
        "a face painted by name is painted",
        faces(ch) == "R30 R30 G R30 R30 R30" and names(ch) == [("Face3", "G")],
        (faces(ch), names(ch)),
    )
    check(
        "the name is the link's and the colour the child's",
        link.ColoredElements is not None
        and list(link.ColoredElements[1]) == ["Face3"]
        and [rgb(c) for c in ch.MappedColors] == [GREEN],
        link.ColoredElements,
    )
    ch.setElementColors({"Face3": GREEN, "Edge2": YELLOW})
    check(
        "an edge too",
        len(ch.LineColorArray) == 12
        and rgb(ch.LineColorArray[1]) == YELLOW
        and names(ch) == [("Edge2", "Y"), ("Face3", "G")],
        (len(ch.LineColorArray), names(ch)),
    )
    ch.setElementColors({"Face5": GREEN})
    check(
        "what is named no more is the child's colour again",
        faces(ch) == "R30 R30 R30 R30 G R30" and len(ch.LineColorArray) == 1,
        (faces(ch), len(ch.LineColorArray)),
    )
    ch.ShapeColor = YELLOW
    check(
        "another colour for the child leaves the named face",
        faces(ch) == "Y30 Y30 Y30 Y30 G Y30",
        faces(ch),
    )
    ch.ShapeColor = RED
    ch.setElementColors({})
    check(
        "with no names it is one colour",
        len({rgb(c) for c in ch.DiffuseColor}) == 1
        and rgb(ch.DiffuseColor[0]) == RED
        and names(ch) == []
        and not link.ColoredElements,
        (faces(ch), names(ch)),
    )
    ch.ForceMapColors = True
    check(
        "asked to, it takes the faces of what the link shows",
        faces(ch) == "Y30 B30 B30 B30 B30 B30",
        faces(ch),
    )
    box.ViewObject.ShapeColor = GREEN
    doc.recompute()
    check("and follows them", faces(ch) == "Y30 G30 G30 G30 G30 G30", faces(ch))
    ch.MapFaceColor = False
    check(
        "asked no more it is its own colour",
        len({rgb(c) for c in ch.DiffuseColor}) == 1 and rgb(ch.DiffuseColor[0]) == RED,
        faces(ch),
    )
    ch.MapFaceColor = True
    ch.ForceMapColors = False
    ch.setElementColors({"Face3": GREEN})
    ch.DiffuseColor = [RED, GREEN, RED, GREEN, RED, GREEN]
    check(
        "a list by number is kept, the named face over it, and the transparency",
        faces(ch) == "R G G G R G" and ch.Transparency == 30 and rgb(ch.ShapeColor) == RED,
        (faces(ch), ch.Transparency, rgb(ch.ShapeColor)),
    )
    box.Length = 20
    doc.recompute()
    check("a new shape is drawn the same", faces(ch) == "R G G G R G", faces(ch))
    for schema in (5, 4):
        doc.SaveSchemaVersion = schema
        path = os.path.join(tmp, "unbound%d.FCStd" % schema)
        doc.saveCopy(path)
        other = opened(path)
        l = other.getObject("Plain")
        c = l.ViewObject.ChildViewProvider
        check(
            "schema %d keeps the child and its looks" % schema,
            c is not None
            and faces(c) == "R G G G R G"
            and c.LineWidth == 5
            and rgb(c.ShapeColor) == RED
            and names(c) == [("Face3", "G")],
            None if c is None else (faces(c), c.LineWidth, rgb(c.ShapeColor), names(c)),
        )
        if c is None:
            continue
        check("schema %d keeps its transparency" % schema, c.Transparency == 30, c.Transparency)
        c.setElementColors({})
        check(
            "schema %d: the name taken away after the read takes its paint" % schema,
            faces(c) == "R G R30 G R G" and names(c) == [],
            (faces(c), names(c)),
        )
        App.closeDocument(other.Name)
    App.closeDocument(doc.Name)


def run():
    timer = QtCore.QTimer()
    timer.timeout.connect(dismiss)
    timer.start(300)
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        # The log is no part of this
        p.SetInt("TransactionLog", 0)
        with tempfile.TemporaryDirectory() as tmp:
            for part in (bound, unbound):
                try:
                    part(tmp)
                except Exception:
                    lines.append("FAIL %s exception\n%s" % (part.__name__, traceback.format_exc()))
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
