"""The property view shows the groups by rank, then by name; the rank is an
ending of a group's name and is not shown.

docs/HandsOnQueue.md entry 106: the looks kept with an object are in its
group "Appearances", which by its name stood first in the property view,
above "Base". The reporter: "add a ranking number for property group,
default 0. sort group by rank + name", "let's encode the rank into group
name with ending postfix #<rank>. strip the postfix before display it in
qt", "group with the same name having different rank will use the largest
rank", "use the name ending form, Appearances#1".

Claims, a box selected and its data shown:
  - the object's looks are declared in the group "Appearances#1";
  - no group is shown with a "#" and a number at its end;
  - "Appearances" is shown, and it is the last group; "Base" stands before
    it;
  - a property added in the group "Zeta#-1" is shown in a group "Zeta",
    which is the first: a rank below 0 comes before the others;
  - one added in "Alpha#2" is shown in "Alpha", after "Appearances": rank
    before name;
  - one added in "Middle" stands among the groups of rank 0 by its name;
    a second added in "Middle#5" is in the same group, one "Middle" only,
    and that group is now the last: the largest rank given;
  - one added in a group "Appearances", with no rank, is in the object's
    group of that name and not in a second one;
  - a name whose ending is no rank, "Size#x", is shown as it is.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
STEPS = []
STATE = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=300):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()


def groups():
    """The groups the data side of the property view shows, in order, each
    with the names of its rows."""
    best = []
    for view in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTreeView):
        if view.metaObject().className() != "Gui::PropertyEditor::PropertyEditor":
            continue
        model = view.model()
        root = QtCore.QModelIndex()
        shown = []
        for row in range(model.rowCount(root)):
            index = model.index(row, 0, root)
            if view.isRowHidden(row, root) or not model.rowCount(index):
                continue
            shown.append((index.data(), [model.index(r, 0, index).data()
                                         for r in range(model.rowCount(index))]))
        # the data side is the one that has the box's Length
        if any("Length" in rows for _, rows in shown):
            best = shown
    return best


def names():
    return [name for name, _ in groups()]


def refresh():
    """Select again: the view lists the properties of what is selected."""
    FreeCADGui.Selection.clearSelection()
    settle(600)
    FreeCADGui.Selection.addSelection(STATE["box"])
    settle(1200)


def start():
    doc = FreeCAD.newDocument("GroupRank")
    box = doc.addObject("Part::Box", "Box")
    doc.recompute()
    STATE["box"] = box
    FreeCADGui.Selection.addSelection(box)
    settle(1500)
    check("the object's looks are declared in the group 'Appearances#1'",
          box.getGroupOfProperty("ShapeAppearance") == "Appearances#1",
          box.getGroupOfProperty("ShapeAppearance"))
    shown = names()
    note("groups shown: %s" % shown)
    if not check("the data of the box is shown, in groups", len(shown) >= 2, shown):
        STEPS[:] = []
        return
    ranked = [n for n in shown if re.search(r"#-?\d+$", n)]
    check("no group is shown with a rank at its end", not ranked, ranked)
    check("'Appearances' is the last group", shown[-1] == "Appearances", shown)
    check("'Base' stands before it",
          "Base" in shown and shown.index("Base") < len(shown) - 1, shown)


def ranks():
    box = STATE["box"]
    box.addProperty("App::PropertyFloat", "Early", "Zeta#-1", "")
    box.addProperty("App::PropertyFloat", "Late", "Alpha#2", "")
    box.addProperty("App::PropertyFloat", "Mid", "Middle", "")
    refresh()
    shown = names()
    note("groups shown: %s" % shown)
    check("'Zeta#-1' is shown as 'Zeta', the first group", shown and shown[0] == "Zeta", shown)
    check("'Alpha#2' is shown as 'Alpha', after 'Appearances'",
          "Alpha" in shown and "Appearances" in shown
          and shown.index("Alpha") > shown.index("Appearances"), shown)
    zero = [n for n in shown if n not in ("Zeta", "Alpha", "Appearances")]
    check("'Middle' stands among the groups of rank 0, by its name",
          "Middle" in zero and zero == sorted(zero), zero)


def largest():
    box = STATE["box"]
    box.addProperty("App::PropertyFloat", "MidRanked", "Middle#5", "")
    box.addProperty("App::PropertyFloat", "MyLook", "Appearances", "")
    box.addProperty("App::PropertyFloat", "Odd", "Size#x", "")
    refresh()
    found = groups()
    shown = [name for name, _ in found]
    note("groups shown: %s" % shown)
    rows = dict(found)
    check("one group 'Middle' holds both properties",
          shown.count("Middle") == 1
          and {"Mid", "MidRanked"} <= {n.replace(" ", "") for n in rows.get("Middle", [])},
          rows.get("Middle"))
    check("'Middle' is now the last group: the largest rank given",
          shown and shown[-1] == "Middle", shown)
    look = rows.get("Appearances", [])
    check("a property added in 'Appearances' is in the object's group, one only",
          shown.count("Appearances") == 1 and any(n.replace(" ", "") == "MyLook" for n in look),
          look)
    check("a name whose ending is no rank is shown as it is", "Size#x" in shown, shown)
    FreeCADGui.getMainWindow().grab().save(os.path.join(OUT, "groups.png"))


def finish():
    FreeCADGui.Selection.clearSelection()
    settle(300)
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    step = STEPS.pop(0)
    try:
        step()
    except Exception:
        note("ABORT in %s: %s" % (step.__name__, traceback.format_exc().replace("\n", " | ")))
        STEPS[:] = []
    QtCore.QTimer.singleShot(800, advance)


STEPS[:] = [start, ranks, largest]
QtCore.QTimer.singleShot(2500, advance)
