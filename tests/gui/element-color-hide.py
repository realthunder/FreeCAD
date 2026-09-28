"""A container's colour-dialog hide (Std_SetAppearance on elements, the
hidden marker "!hide") and what it reaches. The dialog overrides what lies
BELOW a container's own children -- a child's own content -- never a direct
child, whose own Visibility is the way to hide it; so the hides here are of
a grandchild, Sub.Box2, with Sub an App::Part inside Asm.

App::Part and App::Link both apply their element colours through
ViewProviderLink::applyColorsTo: a secondary Hide down the path their
OWN view provider resolves without append (getDetailPath(sub, .., false)),
so the key is the container's content, not its occurrence:
  - a Part's hide is the Part's content: a Link to the Part shows it
    the same way, since the Link replaces the Part's root and switch but
    reuses its children root;
  - a Link's hide is the Link's own: the key starts at the Link's
    snapshot, which the Part never passes through.

Claims, each by a pick, a pick after a bounding box pass (the cache that
culls a pick is shared by both occurrences of the box's root), and the
pixel the backend draws (mode 3):
  - Asm's hide of Sub.Box2 takes it out of Asm AND out of Link2 -> Asm
    (with LinkChildrenDirect on; off, the Links keep it, see scenario());
    Box4 beside it stays in every occurrence;
  - Link2's hide of Sub.Box2 takes it out of Link2 only, not out of Asm
    nor out of Link3 -> Asm;
  - clearing either brings everything back;
  - a per-view path hide of Asm's Box2 on top of Link2's colour hide
    leaves the colour hide alone, and clearing it restores Asm's Box2
    only;
  - the other view follows the colour hides (they are the document's,
    not a view's) and never the per-view one;
  - a force show ("!show") of a grandchild whose own Visibility is off
    brings it back exactly where the matching hide would take it out;
  - all of it in both LinkChildrenDirect modes (see scenario()).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "ElementColorHide"
HIDE = {"Sub.Box2.!hide": (0.0, 0.0, 0.0, 0.0)}
SHOW = {"Sub.Box2.!show": (0.0, 0.0, 0.0, 0.0)}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def xbox(view):
    """Bounding box pass over the view's scene: the pass that builds the
    caches a later pick culls with."""
    from pivy import coin
    vp = view.getViewer().getSoRenderManager().getViewportRegion()
    action = coin.SoGetBoundingBoxAction(vp)
    action.apply(view.getSceneGraph())


def hit(view, p):
    x, y = view.getPointOnViewport(p)
    info = view.getObjectInfo((int(x), int(y)))
    if not info:
        return None
    return (info.get("Object"), info.get("SubName", ""))


def pixel(view, p, tag):
    """The backend's own framebuffer at the projection of 3D point p."""
    FreeCADGui.getMainWindow().setActiveWindow(view)
    settle()
    path = os.path.join(OUT, "%s.png" % tag)
    view.saveRenderDump(path, "renderer")
    img = QtGui.QImage(path)
    x, y = view.getPointOnViewport(p)
    c = QtGui.QColor(img.pixel(int(x), int(img.height() - 1 - y)))
    return (c.red(), c.green(), c.blue())


def differ(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) > 30


def scenario(direct):
    """The whole run in one LinkChildrenDirect mode. It decides what a Link
    to a Part shares with the Part (docs/CoinRetirement.md 5.21): on, the
    default, the Part holds its children's own roots and a Link replaces
    only the Part's root and switch, so everything below the Part's children
    root is one set of nodes for Asm, Link2 and Link3; off, each Link
    mirrors the children with snapshots of its own -- one mirror per linked
    object, shared by Link2 and Link3. The mirror is the original linking
    scheme, which lets a Link override its children; direct linking
    (96e6abfaf2) is the simplified one that overrides nothing below the
    Part. So a Part's own hide reaches the Links to it only when direct: a
    mirror is the Links' own, by design."""
    mode = "direct" if direct else "mirror"
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
        "LinkChildrenDirect", direct)
    name = DOC + mode.capitalize()
    doc = FreeCAD.newDocument(name)
    try:
        asm = doc.addObject("App::Part", "Asm")
        sub = doc.addObject("App::Part", "Sub")
        asm.addObject(sub)
        box2 = doc.addObject("Part::Box", "Box2")
        box4 = doc.addObject("Part::Box", "Box4")
        box4.Placement.Base = FreeCAD.Vector(20, 0, 0)
        sub.addObjects([box2, box4])
        link2 = doc.addObject("App::Link", "Link2")
        link2.LinkedObject = asm
        link2.Placement.Base = FreeCAD.Vector(0, 0, 30)
        link3 = doc.addObject("App::Link", "Link3")
        link3.LinkedObject = asm
        link3.Placement.Base = FreeCAD.Vector(0, 0, 60)
        doc.recompute()
        gdoc = FreeCADGui.getDocument(name)

        v1 = gdoc.activeView()
        FreeCADGui.runCommand("Std_ViewCreate")
        settle()
        v2 = gdoc.activeView()
        if len(gdoc.mdiViewsOfType("Gui::View3DInventor")) != 2:
            note("ABORT %s: no second 3D view" % mode)
            return
        for v in (v1, v2):
            v.viewFront()
            v.fitAll()
        settle()
        try:
            v1.saveRenderDump(os.path.join(OUT, "mode.png"))
        except Exception:
            note("ABORT not in render mode 3 (no backend to dump)")
            return

        # Box centres: Box2 and Box4 in Asm, and the same through the Links.
        pts = {}
        for occ, z in (("asm", 0), ("link2", 30), ("link3", 60)):
            pts[occ + ".box2"] = FreeCAD.Vector(5, 5, z + 5)
            pts[occ + ".box4"] = FreeCAD.Vector(25, 5, z + 5)

        def picks(view):
            res = {k: hit(view, p) for k, p in pts.items()}
            xbox(view)
            res.update({k + "@bbox": hit(view, p) for k, p in pts.items()})
            return res

        def drawn(view, tag):
            return {k: pixel(view, p, "%s-%s-%s" % (mode, tag, k))
                    for k, p in pts.items()}

        base = picks(v1)
        base2 = picks(v2)
        pix = drawn(v1, "base")
        note("%s baseline %s" % (mode, base))
        if not all(base[k] for k in pts):
            note("ABORT %s: baseline picks miss: %s" % (mode, base))
            return
        for occ in ("asm", "link2", "link3"):
            check("%s baseline: %s picks Box2" % (mode, occ),
                  "Box2" in str(base[occ + ".box2"]), base[occ + ".box2"])

        def expect(tag, view, gone, b, bpix=None):
            tag = "%s %s" % (mode, tag)
            s = picks(view)
            for k in pts:
                for suffix in ("", "@bbox"):
                    key = k + suffix
                    if k in gone:
                        check("%s: %s not picked" % (tag, key), s[key] is None, s[key])
                    else:
                        check("%s: %s picks as before" % (tag, key), s[key] == b[key],
                              (s[key], b[key]))
            if bpix is None:
                return
            d = drawn(view, tag.replace(" ", "_"))
            for k in pts:
                if k in gone:
                    check("%s: %s not drawn" % (tag, k), differ(d[k], bpix[k]),
                          (d[k], bpix[k]))
                else:
                    check("%s: %s drawn as before" % (tag, k), not differ(d[k], bpix[k]),
                          (d[k], bpix[k]))

        every = ("asm.box2", "link2.box2", "link3.box2") if direct else ("asm.box2",)

        # 1. The Part's own hide of a grandchild: its content, so the Links
        # to it show the same -- when they link it directly.
        asm.ViewObject.setElementColors(HIDE)
        settle()
        check("%s: Asm's element colours hold the hidden marker" % mode,
              "Sub.Box2.!hide" in asm.ViewObject.getElementColors(),
              asm.ViewObject.getElementColors())
        expect("Asm hide", v1, every, base, pix)
        expect("Asm hide, other view", v2, every, base2)
        asm.ViewObject.setElementColors({})
        settle()
        expect("Asm hide cleared", v1, (), base, pix)

        # 2. Link2's own hide of the grandchild: keyed from its own root, so
        # Asm and Link3 keep Box2 -- through nodes they share with Link2.
        link2.ViewObject.setElementColors(HIDE)
        settle()
        check("%s: Link2's element colours hold the hidden marker" % mode,
              "Sub.Box2.!hide" in link2.ViewObject.getElementColors(),
              link2.ViewObject.getElementColors())
        expect("Link2 hide", v1, ("link2.box2",), base, pix)
        expect("Link2 hide, other view", v2, ("link2.box2",), base2)

        # 3. A per-view path hide of Asm's Sub.Box2 on top: both gone in
        # v1, the other view keeps Asm's; clearing it leaves Link2's hide.
        v1.setObjectVisibility(asm, False, "Sub.Box2.")
        settle()
        expect("Link2 hide + v1 path hide", v1, ("asm.box2", "link2.box2"), base, pix)
        expect("Link2 hide + v1 path hide, other view", v2, ("link2.box2",), base2)
        v1.setObjectVisibility(asm, None, "Sub.Box2.")
        settle()
        expect("v1 path hide cleared", v1, ("link2.box2",), base, pix)

        link2.ViewObject.setElementColors({})
        settle()
        expect("Link2 hide cleared", v1, (), base, pix)
        expect("Link2 hide cleared, other view", v2, (), base2)

        # 4. Force show ("!show"): Box2's own Visibility off takes it out of
        # every occurrence; a container's show brings it back where the
        # hide above would take it out -- captured tagged, since every
        # occurrence shares the capture, and admitted per key.
        box2.Visibility = False
        settle()
        allbox2 = ("asm.box2", "link2.box2", "link3.box2")
        expect("Box2 Visibility off", v1, allbox2, base, pix)
        asm.ViewObject.setElementColors(SHOW)
        settle()
        check("%s: Asm's element colours hold the shown marker" % mode,
              "Sub.Box2.!show" in asm.ViewObject.getElementColors(),
              asm.ViewObject.getElementColors())
        shown = every
        expect("Asm show", v1, [k for k in allbox2 if k not in shown], base, pix)
        expect("Asm show, other view", v2, [k for k in allbox2 if k not in shown], base2)
        asm.ViewObject.setElementColors({})
        settle()
        expect("Asm show cleared", v1, allbox2, base, pix)
        link2.ViewObject.setElementColors(SHOW)
        settle()
        expect("Link2 show", v1, ("asm.box2", "link3.box2"), base, pix)
        expect("Link2 show, other view", v2, ("asm.box2", "link3.box2"), base2)
        link2.ViewObject.setElementColors({})
        settle()
        expect("Link2 show cleared", v1, allbox2, base, pix)
        box2.Visibility = True
        settle()
        expect("Box2 Visibility back on", v1, (), base, pix)
    finally:
        FreeCAD.closeDocument(name)
        settle()


def run():
    try:
        for direct in (True, False):
            scenario(direct)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View").SetBool(
            "LinkChildrenDirect", True)
        finish()


def finish():
    note("DONE")
    QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
