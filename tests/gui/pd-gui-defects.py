"""The PartDesign Gui defects of docs/PartDesignPort.md sec 7, each as the
user meets it.

  - Move object after other object keeps the moved features in the order
    the body had them (upstream 1c8ca27f28 -- they came out reversed), in
    whichever order they were selected;
  - closing the document after such a move, then building another one, does
    not crash: the closed document's active-object list ran a timer over
    the freed body (found reproducing the move, fixed in ActiveObjectList);
  - deleting a Loft or a Pipe shows its sketches again, as a Pad does
    (upstream cf951bae6b);
  - a PartDesign feature's double click opens the user's edit mode, as a
    Part feature's does (upstream f34f15dc60);
  - a Body's Transparency leaves its Tip's MapFaceColor on, so the next
    feature still maps the colours (upstream 1844fdd443), and sticks when
    the Tip maps transparency too;
  - PartDesign_NewSketch takes a B-spline face that is planar within 2e-7
    (upstream eebb7f7829);
  - a body made on a box away from the origin keeps it there (upstream
    123a1c80e1 -- it jumped to the origin), and its base looks like the
    box, per-face colours too (upstream aea8919598 -- it came out grey);
  - an LCS attached to a vertex leaves a gap at its origin, so the vertex
    can still be picked (upstream 51f546f1f6);
  - a sketch picked as a Draft's neutral plane is taken (upstream
    51f4ad7432);
  - the Hole panel chooses what a hole is centred on, a sketch's points
    among them (upstream 774ec2cc93);
  - Space on a face picked in the view toggles the body, on a feature
    selected in the tree the feature (upstream bd03414893, 20c01000a1).

Each was reproduced on the tree before its fix by the same steps, driven
through the MCP console. The Pad delete is the control, which showed its
sketch before; a Hole did too, by an override of its own that is gone now.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
V = FreeCAD.Vector
state = {"done": False, "input": None, "boxes": []}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def events(n=10):
    for _ in range(n):
        QtWidgets.QApplication.processEvents()


def settle(ms):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def sweep():
    """Answer the modal dialogs the commands raise: the move's target list,
    and Yes to the rest. An access violation that GUIApplication::notify
    caught shows up here as a message box too."""
    w = QtWidgets.QApplication.activeModalWidget()
    if w is None:
        return
    if isinstance(w, QtWidgets.QInputDialog):
        if state["input"] is not None:
            w.setTextValue(state["input"])
        w.accept()
    elif isinstance(w, QtWidgets.QMessageBox):
        state["boxes"].append(w.text())
        b = w.button(QtWidgets.QMessageBox.Yes)
        (b.click() if b else w.accept())
    else:
        w.reject()


def crashes():
    return [t for t in state["boxes"] if "Illegal storage access" in t]


def new_body(name):
    doc = FreeCAD.newDocument(name)
    body = doc.addObject("PartDesign::Body", "Body")
    FreeCADGui.ActiveDocument.ActiveView.setActiveObject("pdbody", body)
    return doc, body


def features(body):
    return [o.Name for o in body.Group if o.isDerivedFrom("PartDesign::Feature")]


def test_move():
    for order in (("Box002", "Box003"), ("Box003", "Box002")):
        doc, body = new_body("PDMove")
        for i in range(4):
            box = doc.addObject("PartDesign::AdditiveBox", "Box")
            body.addObject(box)
            box.Placement.Base.x = 20 * i
        doc.recompute()
        FreeCADGui.Selection.clearSelection()
        for name in order:
            FreeCADGui.Selection.addSelection(doc.Name, name)
        state["input"] = "Box"
        FreeCADGui.runCommand("PartDesign_MoveFeatureInTree")
        state["input"] = None
        got = features(body)
        check("move after Box, selected %s, keeps the body's order" % (order,),
              got == ["Box", "Box002", "Box003", "Box001"], got)
        FreeCAD.closeDocument(doc.Name)
        events(20)
        doc, body = new_body("PDMoveNext")
        doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addObject(doc.Box)
        doc.recompute()
        events(20)
        check("closing after the move and opening another document does not crash",
              not crashes(), crashes())
        FreeCAD.closeDocument(doc.Name)
        events()


def sketch(doc, body, z, geo, rotation=None):
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    body.addObject(sk)
    sk.MapMode = "Deactivated"
    sk.Placement = FreeCAD.Placement(V(0, 0, z), rotation or FreeCAD.Rotation())
    for g in geo:
        sk.addGeometry(g)
    return sk


def circle(r, x=0, y=0):
    return [Part.Circle(V(x, y, 0), V(0, 0, 1), r)]


def test_delete():
    for kind in ("Pad", "Loft", "Pipe", "Hole"):
        doc, body = new_body("PDDelete")
        if kind == "Pad":
            sks = [sketch(doc, body, 0, circle(5))]
            feat = doc.addObject("PartDesign::Pad", "Feature")
            body.addObject(feat)
            feat.Profile = sks[0]
            feat.Length = 10
        elif kind == "Loft":
            sks = [sketch(doc, body, 0, circle(5)), sketch(doc, body, 10, circle(3)),
                   sketch(doc, body, 20, circle(4))]
            feat = doc.addObject("PartDesign::AdditiveLoft", "Feature")
            body.addObject(feat)
            feat.Profile = sks[0]
            feat.Sections = sks[1:]
        elif kind == "Pipe":
            sks = [sketch(doc, body, 0, circle(2)),
                   sketch(doc, body, 0, [Part.LineSegment(V(0, 0, 0), V(0, 30, 0))],
                          FreeCAD.Rotation(V(1, 0, 0), 90))]
            feat = doc.addObject("PartDesign::AdditivePipe", "Feature")
            body.addObject(feat)
            feat.Profile = sks[0]
            feat.Spine = (sks[1], ["Edge1"])
        else:
            box = doc.addObject("PartDesign::AdditiveBox", "Box")
            body.addObject(box)
            box.Length = box.Width = 20
            box.Height = 10
            sks = [sketch(doc, body, 10, circle(2, 10, 10))]
            feat = doc.addObject("PartDesign::Hole", "Feature")
            body.addObject(feat)
            feat.Profile = sks[0]
        doc.recompute()
        if not check("%s builds" % kind, feat.isValid() and feat.Shape.Volume > 0,
                     feat.State):
            FreeCAD.closeDocument(doc.Name)
            continue
        for sk in sks:
            sk.ViewObject.Visibility = False
        feat.ViewObject.Visibility = True
        name = feat.Name
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, name)
        FreeCADGui.runCommand("Std_Delete")
        events()
        shown = [sk.ViewObject.Visibility for sk in sks]
        check("deleting a %s shows its sketches" % kind,
              doc.getObject(name) is None and all(shown), shown)
        FreeCAD.closeDocument(doc.Name)
        events()


def test_edit_mode():
    # A document and a feature for each mode: a second edit of the same
    # primitive, started from a script while a Python QTimer runs (the
    # sweep above), does not enter edit -- its panel shows, and the console
    # has "name '_tv_Cylinder' is not defined" from TaskAttacher's
    # visibility snippet. Not the defect under test, not seen by hand, and
    # open (docs/PartDesignPort.md sec 7).
    for mode, expect in (("Default", 0), ("Transform", 1)):
        doc, body = new_body("PDEditMode")
        cyl = doc.addObject("PartDesign::AdditiveCylinder", "Cylinder")
        body.addObject(cyl)
        doc.recompute()
        gdoc = FreeCADGui.ActiveDocument
        try:
            FreeCADGui.setUserEditMode(mode)
            cyl.ViewObject.doubleClicked()
            events()
            got = gdoc.EditMode if gdoc.getInEdit() else None
            check("double click with user edit mode %s edits in mode %d" % (mode, expect),
                  got == expect, got)
            gdoc.resetEdit()
            if FreeCADGui.Control.activeDialog():
                FreeCADGui.Control.closeDialog()
            settle(300)
        finally:
            FreeCADGui.setUserEditMode("Default")
        FreeCAD.closeDocument(doc.Name)
        events()


def test_transparency():
    doc, body = new_body("PDTransparency")
    box = doc.addObject("PartDesign::AdditiveBox", "Box")
    body.addObject(box)
    doc.recompute()
    events()
    red = (1.0, 0.0, 0.0)
    box.ViewObject.DiffuseColor = [red + (1.0,)] + [(0.0, 0.0, 1.0, 1.0)] * 5
    events()
    body.ViewObject.Transparency = 50
    events()
    check("a body's Transparency reaches its Tip",
          box.ViewObject.Transparency == 50, box.ViewObject.Transparency)
    check("a body's Transparency leaves the Tip's MapFaceColor on",
          box.ViewObject.MapFaceColor, box.ViewObject.MapFaceColor)
    fillet = doc.addObject("PartDesign::Fillet", "Fillet")
    body.addObject(fillet)
    fillet.Base = (box, ["Edge1"])
    fillet.Radius = 1
    doc.recompute()
    events()
    reds = [c for c in fillet.ViewObject.DiffuseColor
            if tuple(round(x, 2) for x in c[:3]) == red]
    check("the feature after it maps the Box's colours",
          fillet.ViewObject.MapFaceColor and reds,
          "MapFaceColor=%s, %d red of %d" % (fillet.ViewObject.MapFaceColor, len(reds),
                                             len(fillet.ViewObject.DiffuseColor)))
    FreeCAD.closeDocument(doc.Name)
    events()


def test_transparency_mapped():
    """The body maps its colours from the Tip (its MapTransparency is on).
    With the Tip's on as well -- a preference -- a body Transparency did not
    stick: switching the Tip's mapping off remapped the body from the Tip's
    old colours before the value was handed down, and both ended at 0."""
    for tip_maps in (True, False):
        doc, body = new_body("PDTransparencyMapped")
        box = doc.addObject("PartDesign::AdditiveBox", "Box")
        body.addObject(box)
        doc.recompute()
        box.ViewObject.MapTransparency = tip_maps
        events()
        body.ViewObject.Transparency = 50
        events()
        got = (body.ViewObject.Transparency, box.ViewObject.Transparency,
               box.ViewObject.MapTransparency)
        check("Tip MapTransparency %s: the body's Transparency sticks, and the Tip "
              "stops mapping transparency" % tip_maps, got == (50, 50, False), got)
        FreeCAD.closeDocument(doc.Name)
        events()
    # and a colour set on the body is still one: the Tip takes it and stops
    # mapping colours
    doc, body = new_body("PDBodyColour")
    box = doc.addObject("PartDesign::AdditiveBox", "Box")
    body.addObject(box)
    doc.recompute()
    events()
    body.ViewObject.ShapeColor = (0.0, 1.0, 0.0)
    events()
    colours = set(tuple(round(x, 2) for x in c[:3]) for c in box.ViewObject.DiffuseColor)
    check("a ShapeColor set on the body paints the Tip and ends its mapping",
          not box.ViewObject.MapFaceColor and colours == {(0.0, 1.0, 0.0)},
          "MapFaceColor=%s %s" % (box.ViewObject.MapFaceColor, colours))
    FreeCAD.closeDocument(doc.Name)
    events()


def test_planar_sketch():
    surf = Part.BSplineSurface()
    poles = [[V(10 * i, 10 * j, 0) for j in range(4)] for i in range(4)]
    poles[1][1] = V(10, 10, 2e-6)  # planar within 2e-7, not within 1e-7
    surf.buildFromPolesMultsKnots(poles, [4, 4], [4, 4], [0, 1], [0, 1], False, False, 3, 3)
    doc, body = new_body("PDPlanar")
    face = doc.addObject("Part::Feature", "Face")
    face.Shape = surf.toShape()
    doc.recompute()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(doc.Name, face.Name, "Face1")
    boxes = len(state["boxes"])
    FreeCADGui.runCommand("PartDesign_NewSketch")
    events()
    gdoc = FreeCADGui.ActiveDocument
    if gdoc.getInEdit():
        gdoc.resetEdit()
    events()
    doc.recompute()
    sketches = [o for o in doc.Objects if o.isDerivedFrom("Sketcher::SketchObject")]
    check("a sketch goes on a face planar within 2e-7",
          len(sketches) == 1 and "Invalid" not in sketches[0].State,
          "%s %s" % ([(s.Name, s.State) for s in sketches], state["boxes"][boxes:]))
    FreeCAD.closeDocument(doc.Name)
    events()


def rgb(colors):
    return sorted(set(tuple(round(c, 2) for c in col[:3]) for col in colors))


def test_body_from_base():
    FreeCADGui.activateWorkbench("PartDesignWorkbench")
    for perface in (False, True):
        doc = FreeCAD.newDocument("PDBase")
        box = doc.addObject("Part::Box", "Box")
        box.Placement.Base = V(25, 0, 0)
        doc.recompute()
        vo = box.ViewObject
        vo.ShapeColor = (1.0, 0.0, 0.0)
        vo.LineColor = (0.0, 0.0, 1.0)
        vo.Transparency = 30
        if perface:
            vo.DiffuseColor = [(1.0, 0.0, 0.0)] * 5 + [(0.0, 1.0, 0.0)]
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, box.Name)
        seen = len(state["boxes"])
        picked = [(o.ObjectName, o.SubElementNames) for o in FreeCADGui.Selection.getSelectionEx("", 0)]
        active = FreeCAD.ActiveDocument.Name if FreeCAD.ActiveDocument else None
        FreeCADGui.runCommand("PartDesign_Body")
        events()
        doc.recompute()
        events()
        body = [o for o in doc.Objects if o.isDerivedFrom("PartDesign::Body")][0]
        if not body.Group:
            check("PartDesign_Body takes the selected box as its base", False,
                  "doc %s active %s selection %s boxes %s objects %s"
                  % (doc.Name, active, picked, state["boxes"][seen:], [o.Name for o in doc.Objects]))
            FreeCAD.closeDocument(doc.Name)
            events()
            continue
        base = body.Group[0]
        bb = body.Shape.BoundBox
        what = "per-face coloured" if perface else "coloured"
        check("a body on a %s box at x=25 keeps it there" % what,
              base.isDerivedFrom("PartDesign::FeatureBase")
              and abs(bb.XMin - 25) < 1e-6 and abs(bb.XMax - 35) < 1e-6,
              "%s x[%g,%g]" % (base.TypeId, bb.XMin, bb.XMax))
        bvo = base.ViewObject
        if perface:
            ok = rgb(bvo.DiffuseColor) == rgb(vo.DiffuseColor)
            detail = "%s vs %s" % (rgb(bvo.DiffuseColor), rgb(vo.DiffuseColor))
        else:
            ok = (rgb([bvo.ShapeColor]) == rgb([vo.ShapeColor])
                  and rgb([bvo.LineColor]) == rgb([vo.LineColor])
                  and bvo.Transparency == vo.Transparency)
            detail = "%s %s %s" % (rgb([bvo.ShapeColor]), rgb([bvo.LineColor]), bvo.Transparency)
        check("the base of a body on a %s box looks like the box" % what, ok, detail)
        FreeCAD.closeDocument(doc.Name)
        events()


def test_lcs_vertex_pick():
    doc, body = new_body("PDLcsPick")
    box = doc.addObject("PartDesign::AdditiveBox", "Box")
    body.addObject(box)
    doc.recompute()
    corner = V(10, 10, 10)
    vertex = [i for i, v in enumerate(box.Shape.Vertexes, 1)
              if (v.Point - corner).Length < 1e-6][0]
    lcs = doc.addObject("PartDesign::CoordinateSystem", "LCS")
    body.addObject(lcs)
    lcs.Support = [(box, "Vertex%d" % vertex)]
    lcs.MapMode = "Translate"
    doc.recompute()
    view = FreeCADGui.ActiveDocument.ActiveView
    view.viewIsometric()
    view.fitAll()
    events()
    settle(300)
    x, y = view.getPointOnScreen(corner)
    hits = [view.getObjectInfo((x + dx, y + dy)) for dx, dy in ((0, 0), (1, 0), (0, 1), (2, 2))]
    hits = [h and h.get("Object") for h in hits]
    check("an LCS on a vertex leaves the vertex to pick", "LCS" not in hits, hits)
    FreeCAD.closeDocument(doc.Name)
    events()


def test_draft_sketch_plane():
    doc, body = new_body("PDDraftSketch")
    box = doc.addObject("PartDesign::AdditiveBox", "Box")
    body.addObject(box)
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    body.addObject(sk)
    sk.MapMode = "Deactivated"
    sk.Placement = FreeCAD.Placement(V(0, 0, 3), FreeCAD.Rotation())
    doc.recompute()
    side = [i for i, f in enumerate(box.Shape.Faces, 1) if abs(f.normalAt(0, 0).x + 1) < 1e-9][0]
    draft = doc.addObject("PartDesign::Draft", "Draft")
    body.addObject(draft)
    draft.Base = (box, ["Face%d" % side])
    draft.Angle = 10
    doc.recompute()
    FreeCADGui.ActiveDocument.setEdit(draft.Name)
    settle(800)
    button = [b for b in QtWidgets.QApplication.allWidgets()
              if isinstance(b, QtWidgets.QAbstractButton) and b.isVisible()
              and b.objectName() == "buttonPlane"]
    if check("the Draft panel has its neutral plane button", len(button) == 1, len(button)):
        button[0].click()
        events()
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, body.Name, sk.Name + ".")
        events()
        settle(300)
        doc.recompute()
        check("a sketch picked as the Draft's neutral plane is taken",
              draft.NeutralPlane and draft.NeutralPlane[0] == sk and "Invalid" not in draft.State,
              "%s %s" % (draft.NeutralPlane, draft.State))
        # The pick ends the pick mode: a click after it replaced the plane,
        # and the gate outlived the panel
        top = [i for i, f in enumerate(box.Shape.Faces, 1) if abs(f.normalAt(0, 0).z - 1) < 1e-9][0]
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, body.Name, "%s.Face%d" % (box.Name, top))
        events()
        settle(300)
        check("a click after the pick leaves the neutral plane alone",
              draft.NeutralPlane and draft.NeutralPlane[0] == sk, draft.NeutralPlane)
    FreeCADGui.ActiveDocument.resetEdit()
    events()
    FreeCAD.closeDocument(doc.Name)
    events()


def test_hole_on_points():
    doc, body = new_body("PDHolePoints")
    box = doc.addObject("PartDesign::AdditiveBox", "Box")
    body.addObject(box)
    box.Length = box.Width = 40
    sk = doc.addObject("Sketcher::SketchObject", "Sketch")
    body.addObject(sk)
    sk.MapMode = "Deactivated"
    sk.Placement = FreeCAD.Placement(V(0, 0, 10), FreeCAD.Rotation())
    sk.addGeometry(Part.Circle(V(10, 10, 0), V(0, 0, 1), 1))
    sk.setConstruction(sk.addGeometry(Part.Point(V(30, 30, 0))), False)
    doc.recompute()
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(doc.Name, body.Name, sk.Name + ".")
    FreeCADGui.runCommand("PartDesign_Hole")
    settle(800)
    hole = [o for o in doc.Objects if o.isDerivedFrom("PartDesign::Hole")][0]
    combo = [w for w in QtWidgets.QApplication.allWidgets()
             if isinstance(w, QtWidgets.QComboBox) and w.isVisible()
             and w.objectName() == "BaseProfileType"]
    if check("the Hole panel has its base profile choice", len(combo) == 1, len(combo)):
        # the panel previews the tool; the cut waits for OK
        got = []
        for index in (1, 0, 2):
            combo[0].setCurrentIndex(index)
            events()
            doc.recompute()
            got.append((hole.BaseProfileType, len(hole.AddSubShape.Solids)))
        check("a new hole drills on points, circles and arcs as the panel chooses",
              got == [(7, 2), (6, 1), (1, 1)], got)
    FreeCADGui.Control.closeDialog()
    events()
    FreeCAD.closeDocument(doc.Name)
    events()


def test_space_toggle():
    doc, body = new_body("PDSpace")
    box = doc.addObject("PartDesign::AdditiveBox", "Box")
    body.addObject(box)
    doc.recompute()
    cyl = doc.addObject("PartDesign::SubtractiveCylinder", "Cyl")
    body.addObject(cyl)
    doc.recompute()

    def space(sub):
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(doc.Name, body.Name, sub)
        FreeCADGui.runCommand("Std_ToggleVisibility")
        events()
        return (body.Visibility, box.Visibility, cyl.Visibility)

    got = space("Box.")
    check("Space on a feature selected in the tree toggles the feature",
          got == (True, False, True), got)
    before = space("Box.")
    got = space("Cyl.Face1")
    check("Space on a face picked in the view toggles the body",
          got == (not before[0],) + before[1:], "%s -> %s" % (before, got))
    FreeCAD.closeDocument(doc.Name)
    events()


def run():
    timer = QtCore.QTimer()
    timer.timeout.connect(sweep)
    timer.start(50)
    try:
        for test in (test_move, test_delete, test_edit_mode, test_transparency,
                     test_transparency_mapped, test_planar_sketch, test_body_from_base,
                     test_lcs_vertex_pick, test_draft_sketch_plane,
                     test_hole_on_points, test_space_toggle):
            try:
                test()
            except Exception:
                note("FAIL %s raised:\n%s" % (test.__name__, traceback.format_exc()))
        check("no access violation along the way", not crashes(), crashes())
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    timer.stop()
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
