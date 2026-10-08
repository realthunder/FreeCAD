# GUI check of docs/ShapeAppearanceDesign.md sec 14.6.9 step E: the looks of
# a file another FreeCAD wrote, read here, and of the files this build writes
# for others.
#
# The two files read are not made up. data/tests/LooksByUpstream114.FCStd was
# written by upstream's 1.1.4 release image and data/tests/LooksByFork2025.FCStd
# by this fork's release image of 2025-10-15, each running
# scripts/appearance-files-make.py: a box in a colour, faces and edges given
# colours by number and by name, a cut, a shape with no history, links with a
# look of their own and with one face coloured, an array, an App::Part, a body.
# What each of those builds showed of its own file is what is expected here.
#
# Then each is saved the two ways this build saves -- schema 4, for upstream,
# and schema 5, its own -- and read again: the same looks, and in the file
# what each reader needs and no more. And a body's view provider is written
# to, which found nothing listening once; and an array is collapsed, saved
# and read, which lost the looks of its elements.
#
#   cd build/conda-relwithdebinfo-801
#   QT_QPA_PLATFORM=offscreen FREECAD_USER_HOME=/tmp/fchome-af \
#     FILESCHECK_OUT=/tmp/af/out.txt ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD \
#     ~/works/sw/fcad/scripts/appearance-files-check.py
#
# It writes PASS/FAIL lines to $FILESCHECK_OUT and exits.
import os, re, tempfile, traceback, zipfile
import FreeCAD as App
import FreeCADGui as Gui
from PySide import QtCore, QtWidgets

OUT = os.environ["FILESCHECK_OUT"]
DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "tests")
lines = []

RED, GREEN, BLUE = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
YELLOW, MAGENTA, CYAN = (1.0, 1.0, 0.0), (1.0, 0.0, 1.0), (0.0, 1.0, 1.0)
ORANGE, WHITE = (1.0, 0.5, 0.0), (1.0, 1.0, 1.0)
PALETTE = [RED, GREEN, BLUE, YELLOW, MAGENTA, CYAN]


def check(what, ok):
    lines.append(("PASS " if ok else "FAIL ") + what)


def rgb(c):
    return tuple(round(x, 2) for x in c[:3])


def dismiss():
    # An older file asks whether to recompute: nobody is there to answer
    w = QtWidgets.QApplication.activeModalWidget()
    if w is not None:
        w.reject()


def opened(path):
    # A document read is not all there when the call returns: its view
    # providers are made as the event loop gets to them
    import time

    doc = App.openDocument(path)
    until = time.time() + 60
    while time.time() < until and any(obj.ViewObject is None for obj in doc.Objects):
        QtWidgets.QApplication.processEvents()
    for i in range(20):
        QtWidgets.QApplication.processEvents()
    return doc


def seen(doc):
    # What the view providers say of every object's looks
    res = {}
    for obj in doc.Objects:
        vp = obj.ViewObject
        if vp is None:
            continue
        s = {}
        if obj.isDerivedFrom("Part::Feature"):
            s["shape"] = rgb(vp.ShapeColor)
            s["transparency"] = int(vp.Transparency)
            s["faces"] = [rgb(c) for c in vp.DiffuseColor]
            s["line"] = rgb(vp.LineColor)
            s["lines"] = [rgb(c) for c in vp.LineColorArray]
            s["point"] = rgb(vp.PointColor)
        if "OverrideMaterial" in vp.PropertiesList:
            s["override"] = bool(vp.OverrideMaterial)
            # The look a link gives is its own where it gives one. With the
            # override off the colour it would give is the view's to keep,
            # and a schema 5 file does not (sec 14.6.10, step C)
            if s["override"]:
                s["own"] = rgb(vp.ShapeAppearance[0].DiffuseColor)
            s["element colours"] = [rgb(c) for c in vp.OverrideColorList]
            # An array's elements, where it is collapsed: which of them are
            # given a look, and the looks of those
            if "OverrideMaterialList" in vp.PropertiesList:
                s["array overrides"] = [bool(b) for b in vp.OverrideMaterialList]
                s["array looks"] = [
                    rgb(m.DiffuseColor) if on else None
                    for m, on in zip(vp.MaterialList, vp.OverrideMaterialList)
                ]
        if "ColoredElements" in obj.PropertiesList:
            s["coloured names"] = list(obj.ColoredElements[1]) if obj.ColoredElements else []
        res[obj.Name] = s
    return res


def one(colour, n):
    return [colour] * n


# What the build that wrote each file showed of it. Where the two say another
# thing of the same object it is said: upstream has no names for faces and
# does not colour what is made by what it was made from.
COMMON = [
    ("Plain", "shape", RED),
    ("Plain", "transparency", 40),
    ("Plain", "line", BLUE),
    ("Plain", "point", GREEN),
    ("Faces", "faces", [RED, RED, BLUE, RED, RED, RED]),
    ("Faces", "transparency", 0),
    ("Edges", "line", BLUE),
    ("Edges", "lines", one(BLUE, 4) + [ORANGE] + one(BLUE, 7)),
    ("Edges", "transparency", 0),
    ("CutBox", "shape", YELLOW),
    ("CutCyl", "shape", MAGENTA),
    ("Imp", "faces", [PALETTE[i % 6] for i in range(18)]),
    ("Link", "override", True),
    ("Link", "own", GREEN),
    ("LinkFace", "coloured names", ["Face3"]),
    ("LinkFace", "element colours", [BLUE]),
    ("LinkBoth", "coloured names", ["Face1"]),
    ("LinkBoth", "element colours", [ORANGE]),
    ("Array", "array overrides", [False, True]),
    ("Array", "array looks", [None, MAGENTA]),
    ("ArrayShown_i2", "override", True),
    ("ArrayShown_i2", "own", YELLOW),
    ("GroupBox", "shape", WHITE),
    ("Body", "shape", CYAN),
    ("Body", "transparency", 0),
]
EXPECTED = {
    "LooksByFork2025.FCStd": COMMON
    + [
        ("Named", "faces", [YELLOW, YELLOW, BLUE, YELLOW, GREEN, YELLOW]),
        ("Cut", "faces", one(YELLOW, 6) + [MAGENTA]),
        ("Group", "override", True),
        ("Group", "own", ORANGE),
        ("GroupFace", "coloured names", ["GroupFaceBox.Face6"]),
        ("GroupFace", "element colours", [BLUE]),
    ],
    "LooksByUpstream114.FCStd": COMMON
    + [
        ("Named", "shape", YELLOW),
        # Upstream drew this cut in one colour. Here what is made of a
        # yellow box and a magenta cylinder is those colours, as it would be
        # at the first recompute.
        ("Cut", "faces", one(YELLOW, 6) + [MAGENTA]),
    ],
}


def members(path):
    with zipfile.ZipFile(path) as z:
        return z.read("Document.xml").decode(), z.read("GuiDocument.xml").decode()


def view_block(gui, name):
    m = re.search(r'<ViewProvider name="%s".*?</ViewProvider>' % name, gui, re.S)
    return m.group(0) if m else ""


def prop(block, name):
    m = re.search(r'<Property name="%s" type="([^"]*)"' % name, block)
    return m.group(1) if m else None


def files(name, tmp):
    want = EXPECTED[name]
    doc = opened(os.path.join(DATA, name))
    first = seen(doc)
    for obj, key, value in want:
        got = first.get(obj, {}).get(key, "(nothing)")
        check("%s: %s %s is %r (%r)" % (name, obj, key, value, got), got == value)

    # Written for upstream, and written for this build. A line width is the
    # view's own, and not the default one, which a file does not state
    doc.getObject("Plain").ViewObject.LineWidth = 5
    paths = {}
    for schema in (4, 5):
        doc.SaveSchemaVersion = schema
        paths[schema] = os.path.join(tmp, "%s.s%d.FCStd" % (name[:-6], schema))
        doc.saveAs(paths[schema])
    App.closeDocument(doc.Name)

    model, gui = members(paths[4])
    # The lists an object's ElementAppearance holds are no properties of
    # their own, and upstream steps over them with the property
    theirs = re.sub(r'<Property name="ElementAppearance".*?</Property>', "", model, flags=re.S)
    inline = re.findall(
        r"<(FloatList|VectorList|ColorList|MaterialList|PlacementList) count=", theirs + gui
    )
    check(
        "%s at schema 4: no list upstream reads from a file is in the XML (%r)"
        % (name, sorted(set(inline))),
        not inline,
    )
    link = view_block(gui, "Link")
    check(
        "%s at schema 4: a link's look is under the name upstream has, as its type (%r)"
        % (name, prop(link, "ShapeMaterial")),
        prop(link, "ShapeMaterial") == "App::PropertyMaterial",
    )
    plain = view_block(gui, "Plain")
    check(
        "%s at schema 4: the view provider's names are written (%r, %r)"
        % (name, prop(plain, "ShapeAppearance"), prop(plain, "Transparency")),
        prop(plain, "ShapeAppearance") is not None and prop(plain, "Transparency") is not None,
    )
    # The three an older build of this fork reads the faces by, under the
    # types it has them as
    older = [prop(plain, n) for n in ("ShapeColor", "DiffuseColor", "ShapeMaterial")]
    check(
        "%s at schema 4: an older build's names under its types (%r)" % (name, older),
        older == ["App::PropertyColor", "App::PropertyColorList", "App::PropertyMaterial"],
    )
    m = re.search(r'<Property name="ShapeMaterial".*?diffuseColor="(\d+)"', plain, re.S)
    packed = int(m.group(1)) >> 8 if m else -1
    check(
        "%s at schema 4: and ShapeMaterial says the object's colour (%06x)" % (name, packed),
        packed == 0xFF0000,
    )

    model, gui = members(paths[5])
    plain = view_block(gui, "Plain")
    names = [
        n
        for n in (
            "ShapeAppearance",
            "ShapeColor",
            "DiffuseColor",
            "Transparency",
            "ShapeMaterial",
            "LineColor",
            "LineColorArray",
            "PointColor",
            "MappedColors",
            "MapFaceColor",
        )
        if prop(plain, n) is not None
    ]
    check(
        "%s at schema 5: no name of the object's looks is in the view provider (%r)"
        % (name, names),
        plain != "" and not names,
    )
    check(
        "%s at schema 5: what is the view's is still there (%r)" % (name, prop(plain, "LineWidth")),
        prop(plain, "LineWidth") is not None,
    )
    link = view_block(gui, "LinkFace")
    names = [
        n
        for n in ("ShapeAppearance", "ShapeMaterial", "OverrideMaterial", "OverrideColorList")
        if prop(link, n) is not None
    ]
    check("%s at schema 5: nor of a link's (%r)" % (name, names), link != "" and not names)
    check(
        "%s at schema 5: the looks are the objects'" % name,
        model.count('<Property name="ElementAppearance"') >= 10,
    )

    for schema in (4, 5):
        doc = opened(paths[schema])
        again = seen(doc)
        differ = sorted(
            "%s.%s %r/%r" % (o, k, first[o].get(k), again.get(o, {}).get(k))
            for o in first
            for k in first[o]
            if again.get(o, {}).get(k) != first[o].get(k)
        )
        check(
            "%s saved at schema %d and read: every look as it was (%s)"
            % (name, schema, "; ".join(differ[:4])),
            not differ,
        )
        check(
            "... and the view's own line width (%r)" % doc.getObject("Plain").ViewObject.LineWidth,
            doc.getObject("Plain").ViewObject.LineWidth == 5,
        )
        App.closeDocument(doc.Name)


def body():
    # A colour given to a body's view provider, or to its tip's, as a script
    # written for any FreeCAD gives it
    doc = App.newDocument("BodyLooks")
    body = doc.addObject("PartDesign::Body", "Body")
    body.newObject("PartDesign::AdditiveBox", "Box")
    cyl = body.newObject("PartDesign::SubtractiveCylinder", "Cyl")
    cyl.Radius = 2
    cyl.Height = 20
    doc.recompute()

    def state():
        return (
            rgb(body.ViewObject.ShapeColor),
            rgb(body.ShapeColor),
            rgb(cyl.ViewObject.ShapeColor),
            rgb(cyl.ShapeColor),
            sorted(set(rgb(c) for c in body.ViewObject.DiffuseColor)),
        )

    body.ViewObject.ShapeColor = CYAN
    check(
        "a body's view provider given a colour: the body's, and its tip's (%r)" % (state(),),
        state() == (CYAN, CYAN, CYAN, CYAN, [CYAN]),
    )
    body.ViewObject.Transparency = 30
    check(
        "... a transparency (%r, %r)" % (body.Transparency, cyl.Transparency),
        body.Transparency == 30 and cyl.Transparency == 30,
    )
    body.ViewObject.LineColor = RED
    check(
        "... a line colour (%r, %r)" % (rgb(body.LineColor), rgb(cyl.LineColor)),
        rgb(body.LineColor) == RED and rgb(cyl.LineColor) == RED,
    )
    cyl.ViewObject.ShapeColor = YELLOW
    check(
        "the tip's view provider given a colour: the tip's, and the body is drawn in it (%r)"
        % (state(),),
        state()[2:] == (YELLOW, YELLOW, [YELLOW]),
    )
    cyl.ShapeColor = MAGENTA
    check(
        "the tip object given a colour (%r)" % (state(),),
        state()[2:] == (MAGENTA, MAGENTA, [MAGENTA]),
    )
    body.ShapeColor = GREEN
    check(
        "the body object given a colour: its tip's too (%r)" % (state(),),
        state() == (GREEN, GREEN, GREEN, GREEN, [GREEN]),
    )
    App.closeDocument(doc.Name)


def link():
    # A link's look by the name upstream's scripts know
    doc = App.newDocument("LinkLooks")
    box = doc.addObject("Part::Box", "Box")
    lnk = doc.addObject("App::Link", "Link")
    lnk.LinkedObject = box
    doc.recompute()
    vp = lnk.ViewObject
    m = vp.ShapeMaterial
    m.DiffuseColor = GREEN
    vp.ShapeMaterial = m
    vp.OverrideMaterial = True
    check(
        "a link's ShapeMaterial written: its look (%r, %r)"
        % (rgb(vp.ShapeAppearance[0].DiffuseColor), rgb(lnk.ShapeAppearance[0].DiffuseColor)),
        rgb(vp.ShapeAppearance[0].DiffuseColor) == GREEN
        and rgb(lnk.ShapeAppearance[0].DiffuseColor) == GREEN,
    )
    m = vp.ShapeAppearance[0]
    m.DiffuseColor = BLUE
    vp.ShapeAppearance = (m,)
    check(
        "its look written: ShapeMaterial says it (%r)" % (rgb(vp.ShapeMaterial.DiffuseColor),),
        rgb(vp.ShapeMaterial.DiffuseColor) == BLUE,
    )
    App.closeDocument(doc.Name)


def array():
    # The looks of an array's elements, which its view provider holds in two
    # lists while it is collapsed
    doc = App.newDocument("ArrayLooks")
    box = doc.addObject("Part::Box", "Box")
    arr = doc.addObject("App::Link", "Array")
    arr.LinkedObject = box
    arr.ShowElement = True
    arr.ElementCount = 3
    doc.recompute()
    vp = arr.ElementList[1].ViewObject
    m = vp.ShapeAppearance[0]
    m.DiffuseColor = MAGENTA
    vp.ShapeAppearance = (m,)
    vp.OverrideMaterial = True

    def looks(link):
        v = link.ViewObject
        return [
            rgb(m.DiffuseColor) if on else None
            for m, on in zip(v.MaterialList, v.OverrideMaterialList)
        ]

    arr.ShowElement = False
    doc.recompute()
    check(
        "an array collapsed keeps the looks of its elements (%r)" % (looks(arr),),
        looks(arr)[:2] == [None, MAGENTA],
    )
    with tempfile.TemporaryDirectory() as tmp:
        paths = {}
        for schema in (5, 4):
            paths[schema] = os.path.join(tmp, "array%d.FCStd" % schema)
            doc.SaveSchemaVersion = schema
            doc.saveAs(paths[schema])
        arr.ShowElement = True
        doc.recompute()
        vp = arr.ElementList[1].ViewObject
        check(
            "shown again, the element has its look (%r, %r)"
            % (vp.OverrideMaterial, rgb(vp.ShapeAppearance[0].DiffuseColor)),
            vp.OverrideMaterial and rgb(vp.ShapeAppearance[0].DiffuseColor) == MAGENTA,
        )
        App.closeDocument(doc.Name)
        for schema in (5, 4):
            again = opened(paths[schema])
            check(
                "the collapsed array saved at schema %d and read (%r)"
                % (schema, looks(again.Array)),
                looks(again.Array)[:2] == [None, MAGENTA],
            )
            App.closeDocument(again.Name)


def run():
    timer = QtCore.QTimer()
    timer.timeout.connect(dismiss)
    timer.start(200)
    p = App.ParamGet("User parameter:BaseApp/Preferences/Document")
    mode = p.GetInt("TransactionLog", 2)
    try:
        # The log is no part of this: a file is read and written
        p.SetInt("TransactionLog", 0)
        with tempfile.TemporaryDirectory() as tmp:
            for name in sorted(EXPECTED):
                try:
                    files(name, tmp)
                except Exception:
                    lines.append("FAIL %s exception\n%s" % (name, traceback.format_exc()))
        for part in (body, link, array):
            try:
                part()
            except Exception:
                lines.append("FAIL %s exception\n%s" % (part.__name__, traceback.format_exc()))
    finally:
        p.SetInt("TransactionLog", mode)
        App.saveParameter()
    with open(OUT, "w") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(0)


QtCore.QTimer.singleShot(0, run)
