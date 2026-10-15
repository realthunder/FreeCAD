"""Fillet edges in groups: joined from the arrow's menu, mirrored, lit together.

docs/HandsOnQueue.md entry 117, docs/OnViewValueEditing.md section 6 (the
reporter's rule of 2026-10-14 13:46): every customised edge carries a group
id, a new one when it first leaves the default; the root button's menu has a
"Group" sub-menu -- each group's id then its summary, the edge's own checked,
and "New group" -- and joining takes the group's values; a change to one
member is made to all; activating a handle highlights its whole group; the
panel shows the group in a column.

A 10 mm PartDesign box, a fillet on Edge1 and Edge2, each with one radius of
its own: Edge1 2 mm, Edge2 3 mm. In edit. Claims:
  - the two edges are in groups 1 and 2, and their arrows have different
    colours;
  - Tab, Tab, Space: the menu's Group sub-menu lists "1   R 2 mm" checked and
    "2   R 3 mm", and "New group" cannot be chosen (Edge1 is alone);
  - choosing group 2: Edge1 is in group 2 with 3 mm, the panel's Group
    column says 2;
  - Tab, Tab (Edge1's value), 4, Enter: Edge1 and Edge2 are both 4 mm;
  - with Edge1's value active, both arrows are highlighted -- the same
    colour, which neither had before.

Runs with QT_QPA_PLATFORM=offscreen; menus are answered from a timer. With
GT_PICTURE set it also saves the view before the join.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets
from PySide.QtCore import Qt

try:
    from PySide6 import QtTest
except ImportError:
    from PySide2 import QtTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "FilletGroupsInView"

state = {"done": False}


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def pump(turns=30):
    for _ in range(turns):
        QtWidgets.QApplication.processEvents()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)
        time.sleep(0.01)


def mw():
    return FreeCADGui.getMainWindow()


def view():
    return FreeCADGui.getDocument(DOC).ActiveView


def viewer_widget():
    return view().graphicsView()


def field_text(node, name):
    value = node.getField(name).get()
    return value.getString() if hasattr(value, "getString") else str(value)


def arrow_colours():
    """The colour of each shown arrow, as text"""
    from pivy import coin

    # The dragger is the container's part, behind a switch a search does not
    # go into: asked of the container
    root = view().getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setType(coin.SoType.fromName("SoLinearDraggerContainer"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.setSearchingAll(True)
    search.apply(root)
    colours = []
    for p in search.getPaths():
        container = p.getTail()
        if field_text(container, "visible") not in ("TRUE", "1"):
            continue
        dragger = container.getPart("dragger", False)
        if dragger is not None:
            colours.append(field_text(dragger, "color"))
    note("INFO arrows found: %d" % len(colours))
    return colours


def real_key(widget, k):
    QtTest.QTest.keyPress(widget, k, Qt.NoModifier)
    pump(5)
    target = QtWidgets.QApplication.focusWidget() or widget
    QtTest.QTest.keyRelease(target, k, Qt.NoModifier)
    pump(10)


def key(k):
    real_key(viewer_widget(), k)


def open_box():
    for w in mw().findChildren(QtWidgets.QAbstractSpinBox):
        if not w.isVisible() or w.metaObject().className() != "Gui::QuantitySpinBox":
            continue
        parent = w
        while parent is not None and parent.metaObject().className() != "Gui::TaskView::TaskView":
            parent = parent.parentWidget()
        if parent is None:
            return w
    return None


def group_menu_by_space(choice):
    """Space, the Group sub-menu read; the item starting with `choice` taken"""
    seen = {"items": None}

    def answer():
        for w in QtWidgets.QApplication.topLevelWidgets():
            if isinstance(w, QtWidgets.QMenu) and w.isVisible():
                for a in w.actions():
                    if a.menu() is not None and a.text().replace("&", "") == "Group":
                        sub = a.menu()
                        seen["items"] = [(b.text().replace("&", ""), b.isChecked(), b.isEnabled())
                                         for b in sub.actions() if not b.isSeparator()]
                        for b in sub.actions():
                            if choice and b.text().replace("&", "").startswith(choice):
                                b.trigger()
                                break
                w.close()
                return
        QtCore.QTimer.singleShot(100, answer)

    QtCore.QTimer.singleShot(300, answer)
    key(Qt.Key_Space)
    pump(80)
    return seen["items"]


def radii(fillet):
    return [[round(s[1], 6) for s in edge] for edge in fillet.Segments]


def group_cell(edge):
    for tree in mw().findChildren(QtWidgets.QTreeWidget):
        if not tree.isVisible() or tree.columnCount() < 6:
            continue
        for i in range(tree.topLevelItemCount()):
            item = tree.topLevelItem(i)
            if item.text(0) == edge:
                return item.text(5)
    return None


def run():
    try:
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        mw().activateWindow()
        doc = FreeCAD.newDocument(DOC)
        body = doc.addObject("PartDesign::Body", "Body")
        box = body.newObject("PartDesign::AdditiveBox", "Box")
        box.Length = box.Width = box.Height = 10
        doc.recompute()
        fillet = body.newObject("PartDesign::Fillet", "Fillet")
        fillet.Base = (box, ["Edge1", "Edge2"])
        fillet.Radius = 1
        fillet.Segments = [("Edge1", [(0.0, 2.0)]), ("Edge2", [(0.0, 3.0)])]
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        FreeCADGui.activeDocument().activeView().viewIsometric()
        pump()

        groups = [fillet.evalExpression("Segments.%s.Group" % e) for e in ("Edge1", "Edge2")]
        check("Edge1 and Edge2 are in groups 1 and 2", groups == [1, 2], groups)

        gdoc = FreeCADGui.getDocument(DOC)
        gdoc.setEdit(fillet, 0)
        pump(80)
        check("the fillet is in edit", gdoc.getInEdit() is not None)
        colours = arrow_colours()
        check("two arrows of different colours", len(colours) == 2 and colours[0] != colours[1],
              colours)
        if os.environ.get("GT_PICTURE"):
            view().saveRenderDump(os.path.join(OUT, "fillet-groups.png"), metadata=False)

        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        key(Qt.Key_Tab)
        items = group_menu_by_space("2 ")
        note("INFO the Group sub-menu: %s" % items)
        names = [t for t, _, _ in (items or [])]
        check("the Group sub-menu lists both groups, its own checked",
              items is not None and ("1   R 2 mm", True, True) in items
              and any(t.startswith("2   R 3 mm") for t in names), items)
        check("'New group' cannot be chosen for an edge alone in its group",
              items is not None and ("New group", False, False) in items, items)
        pump(40)
        group = fillet.evalExpression("Segments.Edge1.Group")
        check("joined: Edge1 is in group 2 with 3 mm", group == 2 and radii(fillet) == [[3.0], [3.0]],
              (group, radii(fillet)))
        check("the panel's Group column says 2", group_cell("Edge1") == "2", group_cell("Edge1"))

        viewer_widget().setFocus()
        pump(5)
        key(Qt.Key_Tab)
        key(Qt.Key_Tab)
        lit = arrow_colours()
        note("INFO the arrows with Edge1's value active: %s" % lit)
        check("both arrows of the group are lit alike, unlike before",
              len(lit) == 2 and lit[0] == lit[1] and lit[0] not in colours, (lit, colours))
        key(Qt.Key_4)
        b = open_box()
        if b is not None:
            real_key(b, Qt.Key_Return)
            pump(80)
        check("4, Enter: both edges are 4 mm", radii(fillet) == [[4.0], [4.0]], radii(fillet))
        check("still in edit", gdoc.getInEdit() is not None)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


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
    QtCore.QTimer.singleShot(300, mw().close)


QtCore.QTimer.singleShot(3000, run)
