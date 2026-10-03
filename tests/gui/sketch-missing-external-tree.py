"""A sketch whose external reference broke says so in the tree (upstream
07b2d9973d, the fork's way).

External geometry that has lost the element it refers to is flagged
Missing and drawn in its own colour inside the sketch, which nobody sees
until the sketch is opened. Upstream merges a warning into the sketch's
icon and gives the item a tooltip. This fork's tree already has the means
for a state mark: an extra icon beside the item, with its own tooltip
(getExtraIcons / getToolTip by icon tag), as a suppressed feature or an
invalid shape show theirs. The sketch uses that.

A sketch takes Edge12 of a box as external geometry. The box becomes a
cylinder, which has no Edge12, then a box again.

Claims, read from the tree's model:
  - with the reference whole the item has its plain icon;
  - with it broken the icon is wider, by the extra icon;
  - mended, the icon is plain again.

The tooltip is not driven here: the tree makes it on a hover over the
icon, and it is the three lines of getToolTip.

Scored against the tree before the change: the icon never changed.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "MissingExternalTree"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle():
    for _ in range(20):
        QtWidgets.QApplication.processEvents()


def walk(model, parent):
    for row in range(model.rowCount(parent)):
        index = model.index(row, 0, parent)
        yield index
        for child in walk(model, index):
            yield child


def icon_width(label):
    """The width of the icon the tree's model gives the item so labelled."""
    for tree in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeWidget):
        model = tree.model()
        for index in walk(model, QtCore.QModelIndex()):
            if model.data(index, QtCore.Qt.DisplayRole) == label:
                icon = model.data(index, QtCore.Qt.DecorationRole)
                if icon is None or icon.isNull():
                    continue
                return icon.actualSize(QtCore.QSize(4096, 4096)).width()
    return None


def missing(sk):
    """How many external geometries the sketch itself flags Missing."""
    import Sketcher

    count = 0
    for geo in sk.ExternalGeo:
        facade = Sketcher.ExternalGeometryFacade(geo)
        if facade.Ref and facade.testFlag("Missing"):
            count += 1
    return count


def run():
    try:
        import Part

        doc = FreeCAD.newDocument(DOC)
        base = doc.addObject("Part::Feature", "Base")
        base.Shape = Part.makeBox(10, 10, 10)
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addExternal("Base", "Edge12")
        doc.recompute()
        settle()

        plain = icon_width("Sketch")
        check("the sketch has an icon in the tree", bool(plain), plain)
        check("the sketch flags nothing Missing", missing(sk) == 0, missing(sk))

        base.Shape = Part.makeCylinder(3, 10)
        doc.recompute()
        settle()
        check("the sketch flags the reference Missing", missing(sk) == 1, missing(sk))
        broken = icon_width("Sketch")
        check("a broken reference adds an icon to the item",
              bool(plain) and bool(broken) and broken > plain, (plain, broken))

        base.Shape = Part.makeBox(10, 10, 10)
        doc.recompute()
        settle()
        check("the sketch flags nothing Missing again", missing(sk) == 0, missing(sk))
        mended = icon_width("Sketch")
        check("mended, the item has its plain icon again", mended == plain, (plain, mended))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
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
