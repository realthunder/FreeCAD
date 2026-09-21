"""The constraint tools say what to pick next, and the dimension tool says
what its next mode would make.

The hint framework was already here -- `Gui::ToolHandler::getToolHints()`,
the hint bar in the status bar, ten drawing handlers answering -- but the
constraint commands did not use it: `CommandConstraints.cpp` mentioned
`InputHint` nowhere. Starting a constraint tool left the hint bar blank,
and the dimension tool, whose mode key silently changes which constraint
a click will make, said nothing about the mode at all.

What is asserted here:

  - every constraint command that runs through DrawSketchHandlerGenConstraint
    offers a hint for its first pick. An empty one means a command was
    added without a phrase for it, which is how the point-on-object gap
    was found;
  - the hint follows the selection, and follows what was picked, not just
    how many: an angle after a point asks for the first edge, after an
    edge for the second line or the vertex;
  - the dimension tool names the constraint its next mode would make, in
    the order makeAppropriateConstraint actually walks -- for a plain line
    horizontal, vertical, block, then back to length;
  - and it stays silent when there is no other mode: a line that is
    already horizontal has its horizontal/vertical/block modes refused,
    so promising them would be a lie. That case is the reason the check
    cannot simply read the mode table.

The hints are read back off the status bar widget -- the rendered text,
which is what the user sees. `Gui::InputHintWidget` is a QLabel holding
HTML, with the key pictures as inline images; the message is the text of
its `<td>` cells.

Selections are made through Gui.Selection rather than the viewport: the
constraint handler advances its sequence from selection changes, so this
needs no synthetic mouse. The dimension tool is driven by its initial
selection and by Sketcher_NextToolMode, the command that owns the mode key.

Run through scripts/gui-test.sh (xvfb, isolated configuration, external
timeout); registered in ctest by tests/gui/CMakeLists.txt.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "SketchConstraintHints"
OBJ = "Sketch"

# Every command whose handler is DrawSketchHandlerGenConstraint. Each must
# have something to say before the first pick.
GEN_COMMANDS = [
    "Sketcher_ConstrainCoincidentUnified",
    "Sketcher_ConstrainCoincident",
    "Sketcher_ConstrainPointOnObject",
    "Sketcher_ConstrainDistance",
    "Sketcher_ConstrainDistanceX",
    "Sketcher_ConstrainDistanceY",
    "Sketcher_ConstrainHorVer",
    "Sketcher_ConstrainHorizontal",
    "Sketcher_ConstrainVertical",
    "Sketcher_ConstrainBlock",
    "Sketcher_ConstrainLock",
    "Sketcher_ConstrainParallel",
    "Sketcher_ConstrainPerpendicular",
    "Sketcher_ConstrainTangent",
    "Sketcher_ConstrainEqual",
    "Sketcher_ConstrainRadius",
    "Sketcher_ConstrainDiameter",
    "Sketcher_ConstrainRadiam",
    "Sketcher_ConstrainAngle",
    "Sketcher_ConstrainSymmetric",
]

state = {"doc": None}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def hint_widget():
    mw = FreeCADGui.getMainWindow()
    for label in mw.findChildren(QtWidgets.QLabel):
        if label.metaObject().className() == "Gui::InputHintWidget":
            return label
    return None


def hints():
    """The hint messages currently on the status bar, key pictures stripped."""
    widget = hint_widget()
    if widget is None:
        return None
    html = widget.text()
    if not html:
        return []
    cells = re.findall(r"<td valign=bottom>(.*?)</td>", html, re.S)
    return [re.sub(r"<[^>]+>", "", cell).strip() for cell in cells]


def pick_hint():
    """The first hint line: what to pick."""
    current = hints()
    return current[0] if current else ""


def mode_hint():
    """The second hint line, if any: what the mode key would switch to."""
    current = hints()
    return current[1] if current and len(current) > 1 else ""


def settle():
    QtWidgets.QApplication.processEvents()


def start(command):
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.runCommand(command, 0)
    settle()


def select(sub):
    FreeCADGui.Selection.addSelection(state["doc"].Name, OBJ, sub)
    settle()


def build_sketch():
    import Part
    import Sketcher

    doc = FreeCAD.newDocument(DOC)
    state["doc"] = doc
    sk = doc.addObject("Sketcher::SketchObject", OBJ)

    # Edge1..Edge4: an unconstrained square, so Edge1 is a plain line with
    # every dimension mode still open to it.
    pts = [(0, 0), (10, 0), (10, 10), (0, 10)]
    for i in range(4):
        a, b = pts[i], pts[(i + 1) % 4]
        sk.addGeometry(Part.LineSegment(FreeCAD.Vector(a[0], a[1], 0),
                                        FreeCAD.Vector(b[0], b[1], 0)), False)
    # Edge5: a circle, and Edge6: a line that is already horizontal.
    sk.addGeometry(Part.Circle(FreeCAD.Vector(30, 5, 0), FreeCAD.Vector(0, 0, 1), 4), False)
    sk.addGeometry(Part.LineSegment(FreeCAD.Vector(0, 30, 0),
                                    FreeCAD.Vector(10, 30, 0)), False)
    sk.addConstraint(Sketcher.Constraint("Horizontal", 5))
    doc.recompute()
    return sk


def check_first_pick_hints():
    missing = []
    for command in GEN_COMMANDS:
        start(command)
        if not pick_hint():
            missing.append(command)
        FreeCADGui.Selection.clearSelection()
    check("every constraint tool hints its first pick", not missing,
          "silent: " + ", ".join(missing) if missing else "%d tools" % len(GEN_COMMANDS))


def check_context_hints():
    # An angle wants two edges, or the vertex between them and two edges.
    # Which it asks for next depends on what came first.
    start("Sketcher_ConstrainAngle")
    check("angle, nothing picked", pick_hint() == "pick edge or first point", pick_hint())
    select("Edge1")
    check("angle after an edge", pick_hint() == "pick second line or point", pick_hint())

    start("Sketcher_ConstrainAngle")
    select("Vertex1")
    check("angle after a point", pick_hint() == "pick first edge", pick_hint())

    # Point on object: whichever kind was picked, the other is wanted next.
    start("Sketcher_ConstrainPointOnObject")
    check("point on object, nothing picked", pick_hint() == "pick point or edge", pick_hint())
    select("Vertex1")
    check("point on object after a point", pick_hint() == "pick edge", pick_hint())

    start("Sketcher_ConstrainPointOnObject")
    select("Edge1")
    check("point on object after an edge", pick_hint() == "pick point", pick_hint())

    # Symmetric takes its reference last, or an edge first and the point on it.
    start("Sketcher_ConstrainSymmetric")
    select("Vertex1")
    check("symmetric after a point", pick_hint() == "pick edge or second point", pick_hint())
    select("Vertex3")
    check("symmetric after two points", pick_hint() == "pick symmetry line or point",
          pick_hint())

    start("Sketcher_ConstrainSymmetric")
    select("Edge1")
    check("symmetric after an edge", pick_hint() == "pick symmetry point", pick_hint())

    FreeCADGui.Selection.clearSelection()


def start_dimension(subs):
    FreeCADGui.Selection.clearSelection()
    for sub in subs:
        FreeCADGui.Selection.addSelection(state["doc"].Name, OBJ, sub)
    FreeCADGui.runCommand("Sketcher_Dimension", 0)
    settle()


def next_mode():
    FreeCADGui.runCommand("Sketcher_NextToolMode", 0)
    settle()


def check_dimension_mode_hints():
    # A plain line: length, then horizontal, vertical and block. The hint
    # names the mode the key would move to, so it runs one ahead.
    start_dimension(["Edge1"])
    seen = [mode_hint()]
    for _ in range(3):
        next_mode()
        seen.append(mode_hint())
    expected = ["switch to horizontal", "switch to vertical", "switch to block",
                "switch to length"]
    check("a plain line offers its four modes in order", seen == expected, seen)

    # The same line, already horizontal: makeCts_1Line refuses the other
    # three and restarts the cycle, so there is no next mode to name.
    start_dimension(["Edge6"])
    quiet = [mode_hint()]
    for _ in range(2):
        next_mode()
        quiet.append(mode_hint())
    check("a horizontal line is not promised a mode it cannot have",
          quiet == ["", "", ""], quiet)

    # Whatever the modes, the tool always says what to pick next.
    check("the dimension tool hints the next pick",
          pick_hint().startswith("pick"), pick_hint())

    FreeCADGui.runCommand("Sketcher_Dimension", 0)  # leave the tool
    settle()
    FreeCADGui.Selection.clearSelection()


def run():
    try:
        import SketcherGui  # noqa: F401  -- registers the commands

        sk = build_sketch()
        FreeCADGui.ActiveDocument.setEdit(sk, 0)
        check("the sketch is in edit mode",
              FreeCADGui.ActiveDocument.getInEdit() is not None)
        check("the hint bar is there to read", hint_widget() is not None)

        check_first_pick_hints()
        check_context_hints()
        check_dimension_mode_hints()

        FreeCADGui.Selection.clearSelection()
        FreeCADGui.ActiveDocument.resetEdit()
        settle()
        check("edit mode is left cleanly",
              FreeCADGui.ActiveDocument.getInEdit() is None)
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    try:
        FreeCADGui.Selection.clearSelection()
    except Exception:
        pass
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(1500, run)
