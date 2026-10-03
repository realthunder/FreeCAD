"""A pattern panel's on-view markers and labels follow an original edited elsewhere.

The instance toggles (Gui::PatternInstanceMarkers) and the spacing labels
(EditableDatumLabel) sit at the middle of what is patterned. The panels
put them there after their own edits, their own recompute and an undo --
and, before this was fixed, after nothing else: with the PD pattern panel
or the link array panel open, an original resized from the console or the
property view, then recomputed, left both where the old shape's middle
was. Now every recompute moves them, once it is over.

What is asserted, for a PD LinearPattern and for a Part::LinkArrayLinear,
each over a 10 mm box patterned 100 mm along X:

  - with the panel open the two markers stand at x = 5 and x = 105, and
    the label starts at x = 5;
  - the box made 30 long from Python and the document recomputed -- the
    panel asked nothing -- they stand at x = 15 and x = 115, and the label
    starts at x = 15.

Run by hand as `FreeCAD <this script>` with GT_OUT set (or through
scripts/gui-test.sh).
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "PatternMarkersFollowRecompute"

state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=30):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        # A closed panel goes by deleteLater, which processEvents leaves to
        # the event loop this runs inside of
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        time.sleep(0.01)


def on_view_root():
    from pivy import coin

    view = FreeCADGui.getDocument(DOC).ActiveView
    search = coin.SoSearchAction()
    search.setName(coin.SbName("OnViewRoot"))
    search.setInterest(coin.SoSearchAction.FIRST)
    search.apply(view.getAuxSceneGraph())
    path = search.getPath()
    return path.getTail() if path else None


def marker_xs():
    """The x of each instance toggle, in the order the panel gave them."""
    from pivy import coin

    root = on_view_root()
    if root is None:
        return None
    for i in range(root.getNumChildren()):
        child = root.getChild(i)
        if child.getName().getString() != "PatternInstanceMarkers":
            continue
        xs = []
        # After the event callback and the pick style: one separator each,
        # a translation then the marker
        for j in range(2, child.getNumChildren()):
            group = child.getChild(j)
            translation = group.getChild(0)
            if not translation.isOfType(coin.SoTranslation.getClassTypeId()):
                continue
            xs.append(round(translation.translation.getValue()[0], 3))
        return xs
    return []


def label_xs():
    """The x each spacing label is placed at: its transform's translation,
    which is where the dimension starts."""
    from pivy import coin

    root = on_view_root()
    if root is None:
        return None
    search = coin.SoSearchAction()
    search.setType(coin.SoDatumLabel.getClassTypeId()
                   if hasattr(coin, "SoDatumLabel") else coin.SoType.fromName("SoDatumLabel"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    xs = []
    for path in search.getPaths():
        parent = path.getNodeFromTail(1)
        transform = parent.getChild(0)
        if transform.isOfType(coin.SoTransform.getClassTypeId()):
            xs.append(round(transform.translation.getValue()[0], 3))
    return xs


def near(values, expected):
    return (values is not None and len(values) == len(expected)
            and all(abs(a - b) < 1e-3 for a, b in zip(values, expected)))


def exercise(what, obj, box):
    gdoc = FreeCADGui.getDocument(DOC)
    gdoc.setEdit(obj, 0)
    pump(60)
    check(what + ": the panel is open", gdoc.getInEdit() is not None)

    markers, labels = marker_xs(), label_xs()
    check(what + ": the markers stand at the instances' middles", near(markers, [5, 105]),
          markers)
    check(what + ": the label starts at the original's middle", near(labels, [5]), labels)

    # Not through the panel: the console, as the property view would
    box.Length = 30
    FreeCAD.getDocument(DOC).recompute()
    pump(60)

    markers, labels = marker_xs(), label_xs()
    check(what + ": after a recompute the panel did not ask for, the markers moved",
          near(markers, [15, 115]), markers)
    check(what + ": and the label moved", near(labels, [15]), labels)

    gdoc.resetEdit()
    pump()
    check(what + ": closing the panel takes the markers and the label away",
          not marker_xs() and not label_xs(), (marker_xs(), label_xs()))
    box.Length = 10
    FreeCAD.getDocument(DOC).recompute()
    pump()


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        FreeCADGui.getDocument(DOC).resetEdit()
        pump()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        doc = FreeCAD.newDocument(DOC)

        body = doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = 10
        box.Width = 10
        box.Height = 10
        pattern = body.newObject("PartDesign::LinearPattern", "LinearPattern")
        pattern.Originals = [box]
        pattern.Direction = (doc.getObject("X_Axis"), [""])
        pattern.Length = 100
        pattern.Occurrences = 2
        doc.recompute()
        pump()
        exercise("PD pattern", pattern, box)

        body.Visibility = False
        source = doc.addObject("Part::Box", "Source")
        source.Length = 10
        source.Width = 10
        source.Height = 10
        array = doc.addObject("Part::LinkArrayLinear", "Array")
        array.LinkedObject = source
        array.Occurrences = 2
        array.Length = 100
        source.Visibility = False
        doc.recompute()
        pump()
        exercise("link array", array, source)
    except Exception:
        note("ABORT run:\n" + traceback.format_exc())
    finish()


QtCore.QTimer.singleShot(1500, run)
