"""A progressive load builds the same Gui state an eager one does.

ProgressiveLoad (default on) creates a restored document's view providers
AFTER the App load, in slices, and replays their properties without the
per-property Gui signals an eager load raises. Whatever used to be put
right by those signals, or by the order the eager path creates things in,
is suspect -- it has already lost a Part's children on reopen (002c5c1d7f)
and, found by this test, a Body's Origin, a sketch's internal-face view and
the Origin's size (docs/DocumentLoad.md, "Progressive load against eager").

Differential: each file of a corpus built here is opened eagerly
(ProgressiveLoad off: the reference), then progressively PL_RUNS times --
which view provider is built first follows allocation order, so one green
run proves little -- then eagerly again, the noise floor: an item that
differs between the two eager opens is not judged. Compared per run, for
every document the file brings in:
  - per view provider: Visibility, isShow, isShowable, the display switch,
    display mode, claimChildren, ClaimedChildren / ClaimedBy, element
    colours, bounding box (to 1% of its size);
  - the scene: every node path to every SoFCSelectionRoot, as the chain of
    objects whose roots it passes -- per OCCURRENCE, not per name;
  - a 32x24 pick grid from one camera (an edge or vertex hit is a boundary
    cell a pixel flips, and is not judged);
  - the backend's frame (saveRenderDump), under 0.5% of pixels differing.

Corpus: children created after their Part and before it, nested Parts and
Links to Links, a LinkGroup with a hidden element, a link array with a
hidden element and a flat one, a PartDesign Body with a Link to it,
groups with a hidden, a wireframe, a face-coloured and a transparent box,
booleans, a sketch attached to a face with a Link to it, a user-sized
reference image, 327 objects in interleaved creation order, and a
cross-document pair -- placements off
identity throughout (identity is what hid the lost Part children).

Then, on a 600-box document whose drain spans many slices: an edit and
recompute, a delete, a move between Parts, an undo, hides, a recompute of
everything and a revert, each made right after the open while the drain
still runs, against the same made after an eager open; and closes during
the drain, after which the document opens whole.

  scripts/gui-test.sh tests/gui/progressive-load-diff.py /tmp/pld --timeout 1500
PL_FILES=<list> (one path per line) diffs those files instead, without the
operations; PL_RUNS sets the progressive runs (default 2), PL_GRID the pick
grid (default 32x24); a document past PL_CHAIN_LIMIT objects (300) is
compared by occurrence depths per root instead of object chains.
"""
import math
import os
import time
import traceback
from collections import Counter

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui
from pivy import coin

V = FreeCAD.Vector
R = FreeCAD.Rotation
P = FreeCAD.Placement
OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
CORPUS = os.path.join(OUT, "corpus")
RUNS = int(os.environ.get("PL_RUNS", "2"))
GRID = tuple(int(x) for x in os.environ.get("PL_GRID", "32x24").split("x"))
# The level ladder refines a coarse first tessellation on idle: waited
# out, or a coarser polygon reads as a different bounding box.
SETTLE_S = float(os.environ.get("PL_SETTLE", "3"))
PIX_TOL = 12
CHAIN_LIMIT = int(os.environ.get("PL_CHAIN_LIMIT", "300"))
PATHS_LIMIT = int(os.environ.get("PL_PATHS_LIMIT", "1500"))
DERIVED_SIZE = {"App::Plane", "App::Line", "App::Point", "App::Origin",
                "PartDesign::Plane", "PartDesign::Line", "PartDesign::Point",
                "PartDesign::CoordinateSystem"}
RENDER = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render")
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetInt("RenderCache", 3)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)
# An old file asks, modally, whether to recompute for migration.
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "WarnRecomputeOnRestore", False)


def dismiss_modal():
    """Any other modal dialog a file raises would hold the run until its
    timeout: logged, and dismissed. Fires inside the dialog's own loop."""
    from PySide import QtWidgets
    w = QtWidgets.QApplication.activeModalWidget()
    if w is not None:
        text = w.text() if hasattr(w, "text") else ""
        note("  modal dismissed: %s | %s" % (w.windowTitle(), str(text)[:200]))
        w.reject() if hasattr(w, "reject") else w.close()


_WATCH = QtCore.QTimer()
_WATCH.timeout.connect(dismiss_modal)
_WATCH.start(2000)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=5):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


# -- the corpus ---------------------------------------------------------

def off(x, y=0.0, z=0.0, ang=17.0):
    return P(V(x, y, z), R(V(0.3, 0.2, 1), ang))


def box(doc, name, x=0.0, y=0.0, z=0.0, s=2.0):
    b = doc.addObject("Part::Box", name)
    b.Length = b.Width = b.Height = s
    b.Placement = off(x, y, z, 9)
    return b


def save(doc, name):
    doc.recompute()
    settle()
    path = os.path.join(CORPUS, name + ".FCStd")
    doc.saveAs(path)
    return path


def s_part_child_later(doc):
    p = doc.addObject("App::Part", "PLate")
    p.Placement = off(0, 0, 0, 23)
    p.addObjects([box(doc, "Late%d" % i, i * 4) for i in range(4)])
    # The other order: children first.
    kids = [box(doc, "Early%d" % i, i * 4, 12) for i in range(4)]
    q = doc.addObject("App::Part", "PEarly")
    q.Placement = off(0, 10, 3, -31)
    q.addObjects(kids)
    for n, t in (("LLate", p), ("LEarly", q)):
        link = doc.addObject("App::Link", n)
        link.LinkedObject = t
        link.Placement = off(30, 0 if t is p else 20, 0, 41)


def s_nested(doc):
    outer = doc.addObject("App::Part", "Outer")
    outer.Placement = off(1, 2, 3, 12)
    inner = doc.addObject("App::Part", "Inner")
    inner.Placement = off(5, 0, 0, 33)
    c = doc.addObject("Part::Cylinder", "Cyl")
    c.Radius, c.Height = 1, 3
    c.Placement = off(0, 4, 0, 5)
    inner.addObjects([box(doc, "IB"), c])
    outer.addObjects([inner, box(doc, "OB", 0, -5)])
    l1 = doc.addObject("App::Link", "LInner")
    l1.LinkedObject = inner
    l1.Placement = off(20, 0, 0, 50)
    l2 = doc.addObject("App::Link", "LLink")
    l2.LinkedObject = l1
    l2.Placement = off(40, 0, 0, -20)
    l3 = doc.addObject("App::Link", "LOuter")
    l3.LinkedObject = outer
    l3.Placement = off(0, 30, 0, 70)


def s_linkgroup(doc):
    boxes = [box(doc, "G%d" % i, i * 5, -10) for i in range(3)]
    links = []
    for i, b in enumerate(boxes):
        link = doc.addObject("App::Link", "GL%d" % i)
        link.LinkedObject = b
        link.Placement = off(i * 5, 10, 0, 10 * i)
        links.append(link)
    grp = doc.addObject("App::LinkGroup", "LG")
    grp.setLink(links + [box(doc, "GDirect", 0, 20)])
    grp.Placement = off(3, 3, 3, 27)
    grp.setElementVisible("GL1", False)
    for b in boxes:
        b.Visibility = False


def s_array(doc):
    p = doc.addObject("App::Part", "ArrPart")
    p.addObjects([box(doc, "AB0"), box(doc, "AB1", 4)])
    p.Placement = off(0, 0, 0, 15)
    arr = doc.addObject("App::Link", "Arr")
    arr.LinkedObject = p
    arr.ElementCount = 3
    arr.ShowElement = True
    doc.recompute()
    for i, e in enumerate(arr.ElementList):
        e.Placement = off(0, 12 * (i + 1), 0, 20 * i)
    arr.setElementVisible("1", False)
    arr2 = doc.addObject("App::Link", "ArrFlat")
    arr2.LinkedObject = doc.getObject("AB0")
    arr2.ElementCount = 4
    arr2.ShowElement = False
    arr2.PlacementList = [off(30 + i * 3, 0, 0, i * 10) for i in range(4)]


def s_body(doc):
    import Part
    import Sketcher
    body = doc.addObject("PartDesign::Body", "Body")
    body.Placement = off(2, 3, 4, 21)
    sk = body.newObject("Sketcher::SketchObject", "Sk")
    sk.AttachmentSupport = (body.Origin.OriginFeatures[3], [""])
    sk.MapMode = "FlatFace"
    pts = [V(0, 0, 0), V(10, 0, 0), V(10, 6, 0), V(0, 6, 0)]
    for i in range(4):
        sk.addGeometry(Part.LineSegment(pts[i], pts[(i + 1) % 4]))
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sk
    pad.Length = 4
    doc.recompute()
    sk2 = body.newObject("Sketcher::SketchObject", "Sk2")
    sk2.AttachmentSupport = (pad, ["Face6"])
    sk2.MapMode = "FlatFace"
    sk2.addGeometry(Part.Circle(V(3, 3, 0), V(0, 0, 1), 1.2))
    doc.recompute()
    pocket = body.newObject("PartDesign::Pocket", "Pocket")
    pocket.Profile = sk2
    pocket.Length = 2
    doc.recompute()
    link = doc.addObject("App::Link", "LBody")
    link.LinkedObject = body
    link.Placement = off(25, 0, 0, 60)


def s_groups(doc):
    g = doc.addObject("App::DocumentObjectGroup", "Grp")
    bs = [box(doc, "C%d" % i, i * 4, 0, 0, 2 + i * 0.3) for i in range(5)]
    g.addObjects(bs)
    gd = FreeCADGui.getDocument(doc.Name)
    gd.getObject("C1").Visibility = False
    gd.getObject("C2").DisplayMode = "Wireframe"
    gd.getObject("C3").setElementColors({"Face1": (1.0, 0.0, 0.0), "Face3": (0.0, 0.0, 1.0)})
    gd.getObject("C4").Transparency = 60
    sub = doc.addObject("App::DocumentObjectGroup", "SubGrp")
    g.addObject(sub)
    sub.addObject(box(doc, "CS", 0, 10))


def s_booleans(doc):
    a, b = box(doc, "BA", 0, 0, 0, 4), box(doc, "BB", 1, 1, 1, 4)
    cut = doc.addObject("Part::Cut", "Cut")
    cut.Base, cut.Tool = a, b
    c = doc.addObject("Part::Cylinder", "BC")
    c.Placement = off(10, 0, 0, 5)
    s = doc.addObject("Part::Sphere", "BS")
    s.Placement = off(11, 1, 1, 0)
    fus = doc.addObject("Part::MultiFuse", "Fuse")
    fus.Shapes = [c, s]
    comp = doc.addObject("Part::Compound", "Comp")
    comp.Links = [box(doc, "BK0", 0, 20), box(doc, "BK1", 5, 20)]
    doc.recompute()
    mir = doc.addObject("Part::Mirroring", "Mir")
    mir.Source = cut
    mir.Normal = V(1, 0, 0)
    mir.Base = V(-3, 0, 0)
    link = doc.addObject("App::Link", "LCut")
    link.LinkedObject = cut
    link.Placement = off(0, -20, 0, 30)


def s_sketch(doc):
    import Part
    b = box(doc, "SB", 0, 0, 0, 6)
    doc.recompute()
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    sk.AttachmentSupport = (b, ["Face6"])
    sk.MapMode = "FlatFace"
    sk.addGeometry(Part.Circle(V(3, 3, 0), V(0, 0, 1), 1.5))
    doc.recompute()
    link = doc.addObject("App::Link", "LSketch")
    link.LinkedObject = sk
    link.Placement = off(12, 0, 0, 20)


def s_many(doc):
    parts = []
    # Interleaved: some Parts before their children, some after, links
    # in between -- document order is what the drain follows.
    for p in range(10):
        if p % 2:
            part = doc.addObject("App::Part", "MP%d" % p)
            kids = [box(doc, "M%d_%d" % (p, i), i * 3, p * 8) for i in range(30)]
        else:
            kids = [box(doc, "M%d_%d" % (p, i), i * 3, p * 8) for i in range(30)]
            part = doc.addObject("App::Part", "MP%d" % p)
        part.Placement = off(0, 0, p, p * 7)
        part.addObjects(kids)
        parts.append(part)
        if p % 3 == 0:
            link = doc.addObject("App::Link", "ML%d" % p)
            link.LinkedObject = part
            link.Placement = off(120, p * 8, 0, 11)
    for p in range(1, 10, 3):
        link = doc.addObject("App::Link", "ML%d" % p)
        link.LinkedObject = parts[p]
        link.Placement = off(120, p * 8, 0, 11)


def s_image(doc):
    # A reference image sized by the user, not by its pixels: the drain's
    # property sweep once wrote the image's own size over it.
    png = os.path.join(CORPUS, "ref.png")
    img = QtGui.QImage(200, 100, QtGui.QImage.Format_RGB32)
    img.fill(QtGui.QColor(40, 120, 200))
    img.save(png)
    plane = doc.addObject("Image::ImagePlane", "Ref")
    plane.ImageFile = png
    plane.XSize = 50
    plane.YSize = 30
    plane.Placement = off(5, 5, 0, 12)
    box(doc, "RefBox", 0, 0, 0, 3)
    # A Link to it, in a group: the image's view provider names an archive
    # entry, so the load restores the Link's at once, and it was finished
    # before the plane had a view provider -- linked nothing, drew nothing.
    link = doc.addObject("App::Link", "RefLink")
    link.LinkedObject = plane
    link.Placement = off(5, 45, 0, -8)
    doc.addObject("App::DocumentObjectGroup", "RefGroup").addObject(link)


def s_shown_input(doc):
    # A fusion's input shown again after fusing, and the fusion coloured by
    # hand: the fusion's view provider hides its inputs on an update, and a
    # drain that swept the updates after the records let that overrule the
    # saved visibility (and a colour mapping overrule the saved colour).
    a, b = box(doc, "FA", 0, 0, 0, 4), box(doc, "FB", 2, 2, 2, 4)
    fus = doc.addObject("Part::MultiFuse", "Fus")
    fus.Shapes = [a, b]
    doc.recompute()
    gd = FreeCADGui.getDocument(doc.Name)
    gd.getObject("FA").Visibility = True
    gd.getObject("FA").ShapeColor = (0.9, 0.2, 0.1)
    gd.getObject("Fus").ShapeColor = (0.2, 0.7, 0.3)


def s_unbounded(doc):
    # A binder of an origin plane is an unbounded face, drawn as a bounded
    # patch, and a datum plane sized over it. Asked for its box while its
    # visual was still parked, the binder answered with the face's +-1e100
    # and the bounding-box cache kept that past the drain's build.
    part = doc.addObject("App::Part", "UPart")
    part.Placement = off(0, 30, 0, 11)
    body = doc.addObject("PartDesign::Body", "UBody")
    part.addObject(body)
    xz = [f for f in part.Origin.OriginFeatures if f.Role == "XZ_Plane"][0]
    binder = body.newObject("PartDesign::SubShapeBinder", "UBinder")
    binder.Support = [(part, "%s.%s." % (part.Origin.Name, xz.Name))]
    doc.recompute()
    plane = body.newObject("PartDesign::Plane", "UPlane")
    plane.AttachmentSupport = [(binder, "")]
    plane.MapMode = "FlatFace"
    plane.AttachmentOffset = P(V(0, 0, 8), R())
    doc.recompute()


SCENES = [s_part_child_later, s_nested, s_linkgroup, s_array, s_body, s_groups,
          s_booleans, s_sketch, s_many, s_image, s_shown_input, s_unbounded]


def build_corpus():
    os.makedirs(CORPUS, exist_ok=True)
    for fn in SCENES:
        name = fn.__name__[2:]
        doc = FreeCAD.newDocument(name)
        fn(doc)
        save(doc, name)
        FreeCAD.closeDocument(doc.Name)
        settle()
    # Cross-document: B links into A.
    a = FreeCAD.newDocument("xdocA")
    pa = a.addObject("App::Part", "XPart")
    pa.Placement = off(0, 0, 0, 19)
    pa.addObjects([box(a, "XB%d" % i, i * 4) for i in range(3)])
    save(a, "xdocA")
    b = FreeCAD.newDocument("xdocB")
    l1 = b.addObject("App::Link", "XL")
    l1.LinkedObject = pa
    l1.Placement = off(0, 20, 0, 44)
    l2 = b.addObject("App::Link", "XLBox")
    l2.LinkedObject = a.getObject("XB1")
    l2.Placement = off(20, 0, 0, -12)
    own = b.addObject("App::Part", "XOwn")
    own.addObject(box(b, "XOB", 0, -10))
    own.Placement = off(0, -10, 0, 8)
    save(b, "xdocB")
    FreeCAD.closeDocument(b.Name)
    FreeCAD.closeDocument(a.Name)
    return sorted(os.path.join(CORPUS, n) for n in os.listdir(CORPUS)
                  if n.endswith(".FCStd"))


# -- the differential ---------------------------------------------------

def ptr(node):
    return int(node.this)


def rnd(v, n=3):
    return round(float(v), n)


def views_of(doc):
    g = FreeCADGui.getDocument(doc.Name)
    return g.mdiViewsOfType("Gui::View3DInventor") if g else []


def fullname(o):
    """An object, or a view provider's object, as Doc#Name."""
    o = getattr(o, "Object", o)
    return getattr(o, "FullName", str(o))


def vp_state(doc):
    gdoc = FreeCADGui.getDocument(doc.Name)
    out = {}
    for obj in doc.Objects:
        key = "%s#%s" % (doc.Name, obj.Name)
        vp = gdoc.getObject(obj.Name) if gdoc else None
        if vp is None:
            out[key] = {"vp": None}
            continue
        d = {"type": obj.TypeId}
        # Each read on its own: one attribute a view provider lacks must
        # not blank the rest (isShow, which does not exist, once hid every
        # field below it).
        reads = (
            ("vis", lambda: bool(obj.Visibility)),
            ("show", lambda: bool(vp.isVisible())),
            ("showable", lambda: bool(vp.isShowable())),
            ("which", lambda: vp.SwitchNode.whichChild.getValue() if vp.SwitchNode else None),
            ("mode", lambda: getattr(vp, "DisplayMode", None)),
            ("claimChildren", lambda: [fullname(o) for o in vp.claimChildren()]),
            ("claimed", lambda: sorted(fullname(o) for o in (vp.ClaimedChildren or []))),
            ("claimedBy", lambda: sorted(fullname(o) for o in (vp.ClaimedBy or []))),
        )
        for field, read in reads:
            try:
                d[field] = read()
            except Exception as e:
                d[field] = "ERR " + type(e).__name__
        try:
            cols = vp.getElementColors() or {}
            d["colors"] = {k: tuple(rnd(c, 3) for c in v) for k, v in sorted(cols.items())}
        except Exception:
            pass
        try:
            bb = vp.getBoundingBox()
            if bb.isValid():
                d["bbox"] = [rnd(x, 2) for x in (bb.XMin, bb.YMin, bb.ZMin,
                                                  bb.XMax, bb.YMax, bb.ZMax)]
        except Exception:
            pass
        out[key] = d
    return out


def scene_paths(docs, view):
    names = {}
    for doc in docs:
        gdoc = FreeCADGui.getDocument(doc.Name)
        for obj in doc.Objects:
            vp = gdoc.getObject(obj.Name) if gdoc else None
            if vp is not None and vp.RootNode is not None:
                names[ptr(vp.RootNode)] = "%s#%s" % (doc.Name, obj.Name)
    SELROOT = coin.SoType.fromName("SoFCSelectionRoot")
    paths = Counter()
    if len(names) > PATHS_LIMIT:
        # Links and arrays multiply occurrences: one search over such a
        # scene ran for half an hour. Per view provider and pick grid and
        # frame still judge it.
        return {"(scene paths not compared: %d roots)" % len(names): 1}
    if len(names) > CHAIN_LIMIT:
        # Past a few hundred objects the chains cost more than the load:
        # pivy casts every node it hands back through a linear type
        # lookup. Per root, the depth of each of its occurrences instead --
        # two calls a path, not one a node -- and a root lost from the
        # scene (no occurrence) or moved to another parent (another depth)
        # still shows.
        sa = coin.SoSearchAction()
        sa.setType(SELROOT)
        sa.setInterest(coin.SoSearchAction.ALL)
        sa.setSearchingAll(True)
        sa.apply(view.getSceneGraph())
        pl = sa.getPaths()
        depths = {}
        for i in range(pl.getLength()):
            p = pl[i]
            key = names.get(ptr(p.getTail()), "?")
            depths.setdefault(key, []).append(p.getLength())
        for key, ds in depths.items():
            paths["%s depths %s" % (key, sorted(ds))] += 1
        return dict(paths)
    sa = coin.SoSearchAction()
    sa.setType(coin.SoType.fromName("SoFCSelectionRoot"))
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(view.getSceneGraph())
    for p in sa.getPaths():
        chain = []
        for i in range(p.getLength()):
            n = p.getNode(i)
            if n.isOfType(SELROOT):
                chain.append(names.get(ptr(n), "?"))
        paths[" > ".join(chain)] += 1
    return dict(paths)


def picks(view):
    w, h = view.getSize()
    out = {}
    for j in range(GRID[1]):
        for i in range(GRID[0]):
            x = int((i + 0.5) * w / GRID[0])
            y = int((j + 0.5) * h / GRID[1])
            info = view.getObjectInfo((x, y))
            if info:
                par = info.get("ParentObject")
                sub = info.get("SubName", "") if par is not None else info.get("Component", "")
                top = par.Name if par is not None else info.get("Object")
                # Occurrence and element kind; an edge or vertex hit is a
                # boundary cell, where a pixel's offset flips the answer.
                elem = sub.rsplit(".", 1)[-1] if "." in sub else sub
                occ = sub[:len(sub) - len(elem)]
                kind = elem.rstrip("0123456789")
                out["%d,%d" % (i, j)] = "%s|%s|%s%s" % (info.get("Document"), top, occ,
                                                      "" if kind == "Face" else "~" + kind)
    return out


def image_diff(a, b):
    ia, ib = QtGui.QImage(a), QtGui.QImage(b)
    if ia.size() != ib.size():
        return 1.0
    ia = ia.convertToFormat(QtGui.QImage.Format_RGB32)
    ib = ib.convertToFormat(QtGui.QImage.Format_RGB32)
    w, h = ia.width(), ia.height()
    bad = 0
    step = 2
    n = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            pa, pb = ia.pixel(x, y), ib.pixel(x, y)
            n += 1
            if pa != pb:
                if max(abs(((pa >> s) & 255) - ((pb >> s) & 255)) for s in (0, 8, 16)) > PIX_TOL:
                    bad += 1
    return bad / float(n)


def open_file(path, progressive):
    RENDER.SetBool("ProgressiveLoad", progressive)
    before = set(FreeCAD.listDocuments())
    t = time.perf_counter()
    doc = FreeCAD.openDocument(path)
    while FreeCADGui.isBuildingVisuals():
        QtCore.QCoreApplication.processEvents()
    settle(10)
    wall = time.perf_counter() - t
    # The level ladder refines coarse first tessellations on idle: wait it
    # out, or a coarser polygon reads as a different bounding box.
    t2 = time.perf_counter()
    while time.perf_counter() - t2 < SETTLE_S:
        QtCore.QCoreApplication.processEvents()
    docs = [FreeCAD.getDocument(n) for n in FreeCAD.listDocuments() if n not in before]
    if doc not in docs:
        docs.insert(0, doc)
    return doc, docs, wall


def snapshot(doc, docs, cam, tag):
    snap = {"vp": {}, "paths": {}, "picks": {}, "img": {}}
    for d in docs:
        snap["vp"].update(vp_state(d))
    for d in docs:
        vs = views_of(d)
        if not vs:
            continue
        v = vs[0]
        try:
            v.getViewer().setEnabledNaviCube(False)
        except Exception:
            pass
        if cam.get(d.Name) is None:
            v.viewIsometric()
            v.fitAll()
            settle(30)
            cam[d.Name] = v.getCamera()
        v.setCamera(cam[d.Name])
        settle(10)
        # A frame drawn after the state set above: saveRenderDump waits for
        # a complete frame, not for one that postdates these changes, and
        # a busy box handed back one staged before them (the NaviCube in
        # it, the faces still unlit).
        v.redraw()
        v.waitFrameComplete()
        settle(5)
        snap["paths"][d.Name] = scene_paths(docs, v)
        snap["picks"][d.Name] = picks(v)
        img = os.path.join(OUT, "img", "%s_%s.png" % (tag, d.Name))
        try:
            v.saveRenderDump(img)
            snap["img"][d.Name] = img
        except Exception as e:
            snap["img"][d.Name] = "ERR " + str(e)
    return snap


def diff(ref, got):
    out = []
    for key in sorted(set(ref["vp"]) | set(got["vp"])):
        a, b = ref["vp"].get(key), got["vp"].get(key)
        if a is None or b is None:
            out.append(("vp-missing", key, a is None, b is None))
            continue
        for f in sorted(set(a) | set(b)):
            if f == "bbox" and a.get(f) and b.get(f):
                size = max(abs(a[f][i + 3] - a[f][i]) for i in range(3)) or 1.0
                if max(abs(x - y) for x, y in zip(a[f], b[f])) <= 0.01 * size + 0.01:
                    continue
            if a.get(f) != b.get(f):
                # A datum's or an origin feature's size is derived from the
                # content at whatever moment it was last asked for -- an
                # eager load asks mid-load, before all of it is built --
                # so it is reported apart and not judged.
                cat = "vp." + f
                if f == "bbox" and a.get("type") in DERIVED_SIZE:
                    cat = "size." + f
                out.append((cat, key, a.get(f), b.get(f)))
    for dn in sorted(set(ref["paths"]) | set(got["paths"])):
        a, b = ref["paths"].get(dn, {}), got["paths"].get(dn, {})
        for p in sorted(set(a) | set(b)):
            if a.get(p) != b.get(p):
                out.append(("path", dn, p, (a.get(p), b.get(p))))
        pa, pb = ref["picks"].get(dn, {}), got["picks"].get(dn, {})
        bad = [c for c in sorted(set(pa) | set(pb)) if pa.get(c) != pb.get(c)
               and "~" not in str(pa.get(c)) and "~" not in str(pb.get(c))]
        if bad:
            out.append(("picks", dn, len(bad), [(c, pa.get(c), pb.get(c)) for c in bad[:6]]))
        ia, ib = ref["img"].get(dn), got["img"].get(dn)
        if ia and ib and not ia.startswith("ERR") and not ib.startswith("ERR"):
            frac = image_diff(ia, ib)
            if frac > 0.005:
                out.append(("pixels", dn, rnd(frac, 4), ib))
        elif ia != ib:
            out.append(("img", dn, ia, ib))
    return out


def close_all():
    for n in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(n)
    settle(10)


# -- operations while the drain runs ------------------------------------

STRESS_PARTS, STRESS_BOXES = 12, 50


def build_stress(path):
    """Large enough that the drain spans many slices after the open."""
    doc = FreeCAD.newDocument("Stress")
    parts = []
    for p in range(STRESS_PARTS):
        part = doc.addObject("App::Part", "SP%d" % p)
        part.Placement = P(V(p * 60, 0, 0), R(V(0, 0, 1), p * 3))
        boxes = []
        for i in range(STRESS_BOXES):
            b = doc.addObject("Part::Box", "S%d_%d" % (p, i))
            b.Length = b.Width = b.Height = 1
            b.Placement.Base = V((i % 10) * 4, (i // 10) * 4, 0)
            boxes.append(b)
        part.addObjects(boxes)
        parts.append(part)
    for p in range(0, STRESS_PARTS, 4):
        link = doc.addObject("App::Link", "SL%d" % p)
        link.LinkedObject = parts[p]
        link.Placement.Base = V(p * 60, 60, 0)
    doc.recompute()
    doc.saveAs(path)
    FreeCAD.closeDocument(doc.Name)
    settle(10)


def op_edit(doc):
    doc.getObject("S3_5").Height = 5
    doc.recompute()


def op_delete(doc):
    doc.removeObject("S4_7")


def op_move(doc):
    b = doc.getObject("S5_9")
    doc.getObject("SP5").removeObject(b)
    doc.getObject("SP6").addObject(b)


def op_undo(doc):
    doc.openTransaction("t")
    doc.getObject("S6_1").Height = 4
    doc.getObject("SP7").removeObject(doc.getObject("S7_2"))
    doc.commitTransaction()
    doc.undo()


def op_visibility(doc):
    # A Part with a Link to it, one without, two boxes and a Link: the
    # containers and the Link came back visible when the drain replayed
    # their records over the hide.
    for name in ("SP8", "SP9", "S9_3", "S10_3", "SL4"):
        doc.getObject(name).Visibility = False


def op_touch_all(doc):
    for o in doc.Objects:
        if o.TypeId == "Part::Box":
            o.touch()
    doc.recompute()


def op_revert(doc):
    doc.getObject("S2_2").Height = 3
    doc.restore()


OPS = [op_edit, op_delete, op_move, op_undo, op_visibility, op_touch_all, op_revert]


def open_and(path, progressive, op):
    """Open, apply \a op at once -- while a progressive drain has barely
    begun -- then let everything finish."""
    RENDER.SetBool("ProgressiveLoad", progressive)
    before = set(FreeCAD.listDocuments())
    doc = FreeCAD.openDocument(path)
    draining = FreeCADGui.isBuildingVisuals()
    op(doc)
    while FreeCADGui.isBuildingVisuals():
        QtCore.QCoreApplication.processEvents()
    settle(10)
    t = time.perf_counter()
    while time.perf_counter() - t < SETTLE_S:
        QtCore.QCoreApplication.processEvents()
    docs = [FreeCAD.getDocument(n) for n in FreeCAD.listDocuments() if n not in before]
    return (docs[0] if docs else doc), docs, draining


def run_ops():
    path = os.path.join(OUT, "stress.FCStd")
    build_stress(path)
    for op in OPS:
        name = op.__name__[3:]
        cam = {}
        try:
            doc, docs, _ = open_and(path, False, op)
            ref = snapshot(doc, docs, cam, "op_%s_E1" % name)
            close_all()
            found, drained = [], []
            for r in range(RUNS):
                doc, docs, draining = open_and(path, True, op)
                drained.append(draining)
                d = diff(ref, snapshot(doc, docs, cam, "op_%s_P%d" % (name, r + 1)))
                close_all()
                found += [("P%d" % (r + 1),) + tuple(x) for x in d
                          if not x[0].startswith("size.")]
            doc, docs, _ = open_and(path, False, op)
            noise = diff(ref, snapshot(doc, docs, cam, "op_%s_E2" % name))
            close_all()
        except Exception:
            note("ABORT op %s %s" % (name, traceback.format_exc().replace("\n", " | ")))
            close_all()
            continue
        nkeys = {(x[0], x[1], str(x[2]) if x[0] == "path" else "") for x in noise}
        found = [x for x in found if (x[1], x[2], str(x[3]) if x[1] == "path" else "")
                 not in nkeys]
        check("op %s during the drain ends as it does after an eager open "
              "(drain running when applied: %s)" % (name, drained), not found and all(drained),
              "; ".join(str(x)[:300] for x in found[:6]))
    # Closing while the drain runs: no crash, the drain stops, and the next
    # open is whole.
    for r in range(3):
        RENDER.SetBool("ProgressiveLoad", True)
        doc = FreeCAD.openDocument(path)
        draining = FreeCADGui.isBuildingVisuals()
        FreeCAD.closeDocument(doc.Name)
        settle(20)
        check("close during the drain %d: the drain stops" % r,
              draining and not FreeCADGui.isBuildingVisuals(), draining)
    doc, docs, _ = open_and(path, True, lambda d: None)
    gdoc = FreeCADGui.getDocument(doc.Name)
    missing = [o.Name for o in doc.Objects if gdoc.getObject(o.Name) is None]
    check("open after closes during the drain: every view provider", not missing, missing[:5])
    close_all()


def run():
    try:
        os.makedirs(os.path.join(OUT, "img"), exist_ok=True)
        if os.environ.get("PL_FILES"):
            with open(os.environ["PL_FILES"]) as f:
                paths = [l.strip() for l in f if l.strip()]
        else:
            paths = build_corpus()
        # One open first, so the reference is not the one a cold process
        # (shaders compiling, caches empty) took.
        open_file(paths[0], False)
        close_all()
        for path in paths:
            base = os.path.basename(path)[:-6]
            cam = {}
            try:
                doc, docs, _ = open_file(path, False)
                ref = snapshot(doc, docs, cam, base + "_E1")
                close_all()
                runs, runs_snaps = [], [("E1", ref)]
                for r in range(RUNS):
                    doc, docs, _ = open_file(path, True)
                    got = snapshot(doc, docs, cam, "%s_P%d" % (base, r + 1))
                    runs.append(("P%d" % (r + 1), diff(ref, got)))
                    runs_snaps.append(("P%d" % (r + 1), got))
                    close_all()
                doc, docs, _ = open_file(path, False)
                noise = diff(ref, snapshot(doc, docs, cam, base + "_E2"))
                close_all()
            except Exception:
                note("ABORT %s %s" % (base, traceback.format_exc().replace("\n", " | ")))
                close_all()
                continue
            nkeys = {(x[0], x[1], str(x[2]) if x[0] == "path" else "") for x in noise}
            found, derived = [], []
            for tag, d in runs:
                for x in d:
                    if (x[0], x[1], str(x[2]) if x[0] == "path" else "") not in nkeys:
                        # Sizes derived from content are timing-dependent in a
                        # user's file; the generated corpus sizes them the same
                        # way every time, so there they are judged.
                        derived_only = x[0].startswith("size.") and os.environ.get("PL_FILES")
                        (derived if derived_only else found).append((tag,) + tuple(x))
            for tag, snap in runs_snaps:
                for key, st in snap["vp"].items():
                    bb = st.get("bbox")
                    if bb and not all(math.isfinite(v) for v in bb):
                        found.append((tag, "nonfinite", key, bb))
            check("%s: %d progressive runs build what the eager load does (%d objects)"
                  % (base, RUNS, len(ref["vp"])), not found,
                  "; ".join(str(x)[:300] for x in found[:6]))
            if derived:
                note("  derived sizes differ (datums, origin features; not judged in a "
                     "user's file): %d"
                     % len(derived))
            if noise:
                note("  noise (eager against eager, not judged): %s"
                     % Counter(x[0] for x in noise))
        if not os.environ.get("PL_FILES"):
            run_ops()
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        RENDER.SetBool("ProgressiveLoad", True)
        close_all()
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
