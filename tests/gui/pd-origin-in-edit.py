"""The origin a panel shows for picking is shown in the edit's views only.

A PartDesign panel that takes an axis or a plane as a reference shows the
body's origin while it is open (ViewProviderCoordinateSystem::
setTemporaryVisibility): Revolution, Helix, the patterns, Mirrored, the
primitives and the attacher they embed. It did so by writing Visibility
-- the origin's and its six features' -- which is the document's: every
other view of it, every served client and every Link to the body showed
the origin for as long as the panel was open, and a save in the middle
kept it. Where the views of the edit have a visibility table of their
own the same is now an entry of each of them, on the path of the body
being edited, and ends with the edit.

A body of a ring (a revolution about Z, clear of the X axis) and a box
above it, and a Link to the body beside it.

Claims, editing the revolution (its panel shows the axes):
  - entering the edit writes no Visibility;
  - the view draws the X and Z axes at the body, and no axis at the
    Link's occurrence of the body;
  - leaving leaves the Visibility it found and no axis drawn.
Claims, editing the box (its panel shows the planes, and the attacher in
it hides whatever depends on the box, the Link included):
  - no Visibility is written, the Link's included;
  - the view draws the planes at the body, and not the Z axis;
  - after leaving, the Visibility is as found and the Link's occurrence
    is drawn again.

Each step returns to the event loop before the next, as a user's does: a
panel puts things back in its destructor, and a deleteLater() is not run
by a loop nested in the function that asked for it.

Mode 3 only; in modes 0-2 Visibility is written as before.

Scored against the tree before the change: both edits write Visibility
(Origin on, and the axes off for the box), and the X axis is drawn at
the Link's occurrence too.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtGui

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PdOriginInEdit"
V = FreeCAD.Vector
LINK_AT = 100.0
state = {"done": False, "shot": 0}

PART = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part")
PART.SetBool("PreviewOnEdit", False)
PART.SetBool("EditOnTop", False)
FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetBool(
    "AutoSaveEnabled", False)


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def shot(view, tag):
    """The backend's own framebuffer."""
    state["shot"] += 1
    path = os.path.join(OUT, "%02d-%s.png" % (state["shot"], tag))
    view.saveRenderDump(path, "renderer")
    return QtGui.QImage(path)


def at(view, img, pt):
    x, y = view.getPointOnViewport(pt)
    return int(x), int(img.height() - 1 - y)


def reds(view, img, x_mid):
    """Pixels of the X axis' red in the strip along it, 18 either side of
    the origin drawn at x_mid."""
    x0, y0 = at(view, img, V(x_mid - 18, 0, 1.5))
    x1, y1 = at(view, img, V(x_mid + 18, 0, -1.5))
    count = 0
    for y in range(max(0, min(y0, y1)), min(img.height(), max(y0, y1) + 1)):
        for x in range(max(0, min(x0, x1)), min(img.width(), max(x0, x1) + 1)):
            c = QtGui.QColor(img.pixel(x, y))
            if c.red() > 150 and c.green() < 110 and c.blue() < 110:
                count += 1
    return count


def blues(view, img):
    """Pixels of the planes' outline blue around the body's origin, below
    the ring."""
    x0, y0 = at(view, img, V(-12, 0, 4))
    x1, y1 = at(view, img, V(12, 0, -12))
    count = 0
    for y in range(max(0, min(y0, y1)), min(img.height(), max(y0, y1) + 1)):
        for x in range(max(0, min(x0, x1)), min(img.width(), max(x0, x1) + 1)):
            c = QtGui.QColor(img.pixel(x, y))
            if c.blue() > 200 and c.red() < 120 and 100 < c.green() < 200:
                count += 1
    return count


def zblues(view, img):
    """Pixels of the Z axis' blue between the body's origin and the ring.
    The X axis' strip will not do for the box: the attacher draws its own
    red arrows there."""
    x0, y0 = at(view, img, V(-1.5, 0, 5))
    x1, y1 = at(view, img, V(1.5, 0, -2))
    count = 0
    for y in range(max(0, min(y0, y1)), min(img.height(), max(y0, y1) + 1)):
        for x in range(max(0, min(x0, x1)), min(img.width(), max(x0, x1) + 1)):
            c = QtGui.QColor(img.pixel(x, y))
            if c.blue() > 170 and c.red() < 90 and c.green() < 90:
                count += 1
    return count


def solid(view, img, pt):
    """Green: a feature's own shape at 3D point pt."""
    x, y = at(view, img, pt)
    c = QtGui.QColor(img.pixel(x, y))
    return c.green() - c.red() > 40 and c.green() - c.blue() > 40


def run():
    try:
        params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
        if params.GetInt("RenderCache", 3) != 3:
            note("ABORT render cache is not mode 3")
            finish()
            return

        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        sketch = body.newObject("Sketcher::SketchObject", "Sketch")
        sketch.Support = (doc.getObject("XZ_Plane"), [""])
        sketch.MapMode = "FlatFace"
        corners = [V(20, 5, 0), V(30, 5, 0), V(30, 15, 0), V(20, 15, 0)]
        for i in range(4):
            sketch.addGeometry(Part.LineSegment(corners[i], corners[(i + 1) % 4]))
        ring = body.newObject("PartDesign::Revolution", "Ring")
        ring.Profile = sketch
        ring.ReferenceAxis = (sketch, ["V_Axis"])
        ring.Angle = 360
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = box.Height = 10
        box.AttachmentOffset = FreeCAD.Placement(V(20, -5, 20), FreeCAD.Rotation())
        link = doc.addObject("App::Link", "Link")
        link.LinkedObject = body
        link.Placement.Base = V(LINK_AT, 0, 0)
        doc.recompute()
        for obj in (body, ring, box):
            obj.ViewObject.ShapeColor = (0.0, 0.8, 0.0)
        # As the commands leave a body: its last feature shown alone
        for obj in (ring, sketch):
            obj.Visibility = False
        gdoc = FreeCADGui.getDocument(DOC)
        view = gdoc.activeView()
        view.setCameraType("Orthographic")
        view.viewFront()
        view.fitAll()
        settle()

        origin = body.Origin
        watched = [origin] + list(origin.OriginFeatures) + [link, ring, box, sketch]

        def visibility():
            return tuple(bool(o.Visibility) for o in watched)

        def named(values):
            return ", ".join("%s=%d" % (o.Name, v) for o, v in zip(watched, values))

        # The ring where the Link draws it, and the box's bounds for the camera
        p_link_ring = V(LINK_AT + 25, 0, 10)
        found = visibility()
        img = shot(view, "base")
        base = (reds(view, img, 0), reds(view, img, LINK_AT), blues(view, img),
                zblues(view, img), solid(view, img, p_link_ring))
        note("baseline: X axis red at body %d, at link %d; plane blue %d; Z axis blue %d; "
             "link ring drawn %s; %s" % (base + (named(found),)))
        if not (ring.Shape.isValid() and base == (0, 0, 0, 0, True) and not origin.Visibility):
            note("ABORT the baseline is not a body with its origin hidden, and a Link drawn")
            finish()
            return

        def enter(feature):
            def step():
                gdoc.setEdit(feature, 0)
            return step

        def leave():
            gdoc.resetEdit()

        def ring_in_edit():
            if not check("ring: its edit is on", gdoc.getInEdit() is not None):
                raise RuntimeError("no edit")
            check("ring: entering the edit writes no Visibility",
                  visibility() == found, named(visibility()))
            img = shot(view, "ring-edit")
            body_reds, link_reds = reds(view, img, 0), reds(view, img, LINK_AT)
            check("ring: drawn, the X axis at the body", body_reds > 10, body_reds)
            check("ring: drawn, no X axis at the Link's occurrence", link_reds == 0, link_reds)
            z_axis = zblues(view, img)
            check("ring: drawn, the Z axis at the body", z_axis > 5, z_axis)

        def ring_left():
            check("ring: edit left", gdoc.getInEdit() is None)
            check("ring: leaving leaves the Visibility it found",
                  visibility() == found, named(visibility()))
            img = shot(view, "ring-after")
            counts = (reds(view, img, 0), reds(view, img, LINK_AT))
            check("ring: drawn, no axis after the edit", counts == (0, 0), counts)

        def box_in_edit():
            if not check("box: its edit is on", gdoc.getInEdit() is not None):
                raise RuntimeError("no edit")
            check("box: entering the edit writes no Visibility, the Link's included",
                  visibility() == found, named(visibility()))
            img = shot(view, "box-edit")
            planes, z_axis = blues(view, img), zblues(view, img)
            check("box: drawn, the planes at the body", planes > 40, planes)
            check("box: drawn, no Z axis (the attacher hides the axes)", z_axis == 0, z_axis)

        def box_left():
            check("box: edit left", gdoc.getInEdit() is None)
            check("box: leaving leaves the Visibility it found",
                  visibility() == found, named(visibility()))
            img = shot(view, "box-after")
            check("box: drawn, the Link's occurrence again", solid(view, img, p_link_ring))

        steps = [enter(ring), ring_in_edit, leave, ring_left,
                 enter(box), box_in_edit, leave, box_left]

        def advance():
            try:
                if not steps:
                    finish()
                    return
                steps.pop(0)()
            except Exception:
                note("ABORT " + traceback.format_exc().replace("\n", " | "))
                finish()
                return
            QtCore.QTimer.singleShot(1000, advance)

        QtCore.QTimer.singleShot(0, advance)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        for name in list(FreeCAD.listDocuments().keys()):
            FreeCADGui.getDocument(name).resetEdit()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
