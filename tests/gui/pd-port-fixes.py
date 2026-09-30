"""The small PartDesign fixes of docs/PartDesignPort.md sec 8 that only the
Gui shows, each as the user meets it.

  - a transformation added inside a MultiTransform goes to the
    MultiTransform's body, whichever body is active, or none (upstream
    3604e57d6d -- it went to the active body, or nowhere);
  - a PartDesign feature can be dragged to another body, not out of its
    body to the document or a Part (upstream 288255f074 -- a first
    feature could be);
  - a datum line of the body's own, lone or of a coordinate system in it,
    is taken as a Pad's direction (upstream 9504b7e569 -- only the
    origin's were);
  - the Sprocket panel sizes the sprocket from a translated reference
    (upstream 0de4c053a6 -- it looked the translated text up, KeyError);
  - the shaft wizard's constraint type combo reaches the shaft (Qt 6 has
    no currentIndexChanged(QString); upstream e91c16aae1's fixes with it).

Run: FreeCAD <this script>, GT_OUT set to a directory; result.txt there.
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
state = {"done": False}


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
    """Yes to any modal box a command raises."""
    w = QtWidgets.QApplication.activeModalWidget()
    if isinstance(w, QtWidgets.QMessageBox):
        note("NOTE message box: " + w.text())
        w.accept()


def widgets(cls, name):
    out = []
    for w in FreeCADGui.getMainWindow().findChildren(cls, name):
        try:
            if w.isVisible():
                out.append(w)
        except RuntimeError:
            pass
    return out


def box_body(doc, name, x=0):
    body = doc.addObject("PartDesign::Body", name)
    body.Placement.Base = V(x, 0, 0)
    box = body.newObject("PartDesign::AdditiveBox", name + "Box")
    doc.recompute()
    return body, box


def body_of(obj):
    for o in obj.InList:
        if o.TypeId == "PartDesign::Body" and obj in o.Group:
            return o
    return None


def close_panel():
    """Close the task dialog and wait for its widgets to go."""
    FreeCADGui.Control.closeDialog()
    settle(300)
    if FreeCADGui.ActiveDocument:
        FreeCADGui.ActiveDocument.resetEdit()
    settle(300)
    # The panels go by deleteLater, which a nested event loop does not run
    QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
    events()


def set_active_body(body):
    FreeCADGui.ActiveDocument.ActiveView.setActiveObject("pdbody", body)
    events()


def test_multitransform_body():
    doc = FreeCAD.newDocument("PDFixMTBody")
    body1, box1 = box_body(doc, "Body1")
    body2, box2 = box_body(doc, "Body2", 50)
    mt = body1.newObject("PartDesign::MultiTransform", "MultiTransform")
    mt.Originals = [box1]
    doc.recompute()
    for label, active in (("another body active", body2), ("no body active", None)):
        set_active_body(active)
        FreeCADGui.ActiveDocument.setEdit(mt.Name)
        settle(500)
        views = widgets(QtWidgets.QListView, "listTransformFeatures")
        before = set(o.Name for o in doc.Objects)
        if check("the MultiTransform panel is open (%s)" % label, len(views) == 1, len(views)):
            acts = [a for a in views[0].actions() if a.text() == "Add mirrored transformation"]
            if check("it has the add mirrored action", len(acts) == 1):
                acts[0].trigger()
                settle(500)
        added = [doc.getObject(n) for n in set(o.Name for o in doc.Objects) - before]
        mirrored = [o for o in added if o.TypeId == "PartDesign::Mirrored"]
        where = body_of(mirrored[0]).Name if mirrored and body_of(mirrored[0]) else None
        check("the mirrored transformation goes to the MultiTransform's body (%s)" % label,
              len(mirrored) == 1 and where == "Body1",
              "added %s in %s" % ([o.Name for o in added], where))
        check("the other body's tip is left alone (%s)" % label,
              body2.Tip == box2, body2.Tip.Name if body2.Tip else None)
        close_panel()
    FreeCAD.closeDocument(doc.Name)
    events()


def can_drag(body, obj, target):
    """As the tree asks at a drop: the target under the dragged item in the
    selection context stack."""
    sel = FreeCADGui.Selection
    depth = 0
    if target is not None:
        sel.pushContext(target)
        depth += 1
    sel.pushContext(obj)
    depth += 1
    try:
        return body.ViewObject.canDragObject(obj)
    finally:
        for _ in range(depth):
            sel.popContext()


def test_drag_out_of_body():
    doc = FreeCAD.newDocument("PDFixDrag")
    body1, box1 = box_body(doc, "Body1")
    body2, box2 = box_body(doc, "Body2", 50)
    part = doc.addObject("App::Part", "Part")
    sk = body1.newObject("Sketcher::SketchObject", "Sketch")
    doc.recompute()
    check("a first feature cannot be dragged out to the document",
          not can_drag(body1, box1, None))
    check("nor into a Part", not can_drag(body1, box1, part))
    check("it can be dragged to another body", can_drag(body1, box1, body2))
    check("or onto a feature of another body", can_drag(body1, box1, box2))
    check("a sketch can still be dragged out of the body", can_drag(body1, sk, None))
    FreeCAD.closeDocument(doc.Name)
    events()


def test_datum_direction():
    doc = FreeCAD.newDocument("PDFixDatumRef")
    body, box = box_body(doc, "Body")
    line = doc.addObject("App::Line", "LoneLine")
    line.Placement = FreeCAD.Placement(V(0, 0, 0), FreeCAD.Rotation(V(0, 1, 0), 30))
    body.addObject(line)
    lcs = doc.addObject("Part::LocalCoordinateSystem", "LCS")
    lcs.Placement = FreeCAD.Placement(V(0, 0, 0), FreeCAD.Rotation(V(0, 1, 0), 20))
    body.addObject(lcs)
    lcs_axis = [o for o in lcs.OriginFeatures if o.Role == "Z_Axis"][0]
    sk = body.newObject("Sketcher::SketchObject", "Sketch")
    sk.Placement.Base = V(0, 0, 10)
    sk.addGeometry(Part.Circle(V(5, 5, 0), V(0, 0, 1), 3), False)
    doc.recompute()
    pad = body.newObject("PartDesign::Pad", "Pad")
    pad.Profile = sk
    pad.Length = 5
    doc.recompute()
    # As a user has it: the pick goes through importExternalObject(), which
    # falls back on the active body when it missed the edit's
    set_active_body(body)
    # The control: the origin's Z axis, square to the sketch
    zaxis = [o for o in body.Origin.OriginFeatures if o.Role == "Z_Axis"][0]
    for label, pick, want in (("the origin's axis, the control", "%s.%s." % (body.Origin.Name,
                                                                            zaxis.Name), zaxis),
                              ("a lone datum line", "LoneLine.", line),
                              ("a coordinate system's axis", "LCS.%s." % lcs_axis.Name,
                               lcs_axis)):
        FreeCADGui.ActiveDocument.setEdit(pad.Name)
        settle(600)
        combos = widgets(QtWidgets.QComboBox, "directionCB")
        if not check("the Pad panel has its direction combo (%s)" % label, len(combos) == 1,
                     len(combos)):
            close_panel()
            continue
        cb = combos[0]
        idx = [i for i in range(cb.count()) if cb.itemText(i).startswith("Select reference")]
        if check("it offers Select reference (%s)" % label, len(idx) == 1):
            cb.setCurrentIndex(idx[0])
            # the panel listens to activated, which only a user's pick emits
            cb.activated.emit(idx[0])
            events()
            FreeCADGui.Selection.clearSelection()
            FreeCADGui.Selection.addSelection(doc.Name, body.Name, pick)
            events()
            settle(300)
            ref = pad.ReferenceAxis
            # an element of a coordinate system is kept through its parent
            got = ref[0].getSubObject(ref[1][0], retType=1) if ref and ref[1][0] else None
            check("%s is taken as the Pad's direction" % label,
                  bool(ref) and (ref[0] == want or got == want) and "Invalid" not in pad.State,
                  str((ref[0].Name, ref[1]) if ref else None))
        close_panel()
    FreeCAD.closeDocument(doc.Name)
    events()


def test_sprocket_translated():
    doc = FreeCAD.newDocument("PDFixSprocket")
    body = doc.addObject("PartDesign::Body", "Body")
    set_active_body(body)
    FreeCADGui.runCommand("PartDesign_Sprocket")
    settle(800)
    combos = widgets(QtWidgets.QComboBox, "comboBox_SprocketReference")
    sprockets = [o for o in doc.Objects if o.Name.startswith("Sprocket")]
    if check("the Sprocket panel is open", len(combos) == 1 and sprockets,
             "%d %s" % (len(combos), [o.Name for o in sprockets])):
        cb = combos[0]
        obj = sprockets[0]
        idx = cb.findText("Bicycle with Derailleur")
        # As a locale that translates it shows it
        cb.setItemText(idx, "Bicicleta con desviador")
        cb.setCurrentIndex(idx)
        events()
        check("a translated reference sizes the sprocket",
              obj.SprocketReference == "Bicycle with Derailleur"
              # the panel writes the value back as its field shows it
              and abs(obj.RollerDiameter.Value - 0.3125 * 25.4) < 0.01,
              "%s %s" % (obj.SprocketReference, obj.RollerDiameter))
    close_panel()
    FreeCAD.closeDocument(doc.Name)
    events()


def test_shaft_constraint_type():
    doc = FreeCAD.newDocument("PDFixShaft")
    FreeCADGui.runCommand("PartDesign_WizardShaft")
    settle(1500)
    from PartDesign.WizardShaft import WizardShaft as W
    dlg = W.WizardShaftDlg
    if check("the shaft wizard is open", dlg is not None and dlg.table is not None):
        table = dlg.table
        row = table.rowDict["ConstraintType"]
        cb = table.widget.cellWidget(row, 0)
        table.widget.setCurrentCell(row, 0)
        cb.setFocus()
        events()
        cb.setCurrentIndex(cb.findData("Bearing") if cb.findData("Bearing") >= 0
                           else cb.findText("Bearing"))
        events()
        settle(300)
        seg = table.shaft.segments[0]
        bearings = [o for o in doc.Objects if o.TypeId == "Fem::ConstraintBearing"]
        check("the constraint type combo reaches the shaft",
              seg.constraintType == "Bearing" and len(bearings) == 1,
              "%s %s" % (seg.constraintType, [o.Name for o in bearings]))
        check("the table reads the untranslated type back",
              table.getConstraintType(0) == "Bearing", table.getConstraintType(0))
    close_panel()
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    events()


def run():
    timer = QtCore.QTimer()
    timer.timeout.connect(sweep)
    timer.start(50)
    try:
        for test in (test_multitransform_body, test_drag_out_of_body, test_datum_direction,
                     test_sprocket_translated, test_shaft_constraint_type):
            try:
                test()
            except Exception:
                note("FAIL %s raised:\n%s" % (test.__name__, traceback.format_exc()))
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
