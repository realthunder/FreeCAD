"""Ctrl+F in the tree view and in the property view opens the omni search.

docs/HandsOnQueue.md entry 78: "retire current tree view search object
function. hook the ctrl+f shortcut to omni search / followed by space. also
hook ctrl+f in property view to omni search", then "no new mode, just launch
omni search with pre typed text leads to the current object/document/view,
ending with dot, so the list shows all the properties ... only applies to
tree and property view ... [the menu item] keep, trigger omni search ...
when launched by tree view, yes, auto scroll like before, set and clear item
background as before".

The tree view had a search box of its own under it (Ctrl+F, and "Search..."
in a document's context menu). It is gone; both open the omni search with
"/ " typed, its objects' prefix. In the property view the key opens it with
what leads to the thing the view shows, and a dot.

Claims:
  - the tree's panel has no line edit under the tree any more;
  - Ctrl+F in the tree view opens the omni search with "/ " typed;
  - a name typed after it: the tree scrolls to that object's item and marks
    it, and with the box closed the mark is gone;
  - the tree's "Search..." action opens the box with "/ " too;
  - Ctrl+F in the property view's Data tab, one object selected: "/ Crate.",
    and the list under the box has that object's properties (Length among
    them);
  - in its View tab: "/ Crate.ViewObject.", the list has the view provider's
    properties (ShapeColor or Visibility among them);
  - two objects selected: "/ ." in the Data tab, "/ .ViewObject." in the
    View tab;
  - nothing selected, the document selected in the tree: "/ <document>#."
    in the Data tab and "/ <document>#.ActiveView." in the View tab;
  - Ctrl+F in the 3D view opens nothing.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "OmniFind"
STEPS = []


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(ms=200):
    loop = QtCore.QEventLoop()
    QtCore.QTimer.singleShot(ms, loop.quit)
    loop.exec()
    for _ in range(5):
        QtCore.QCoreApplication.processEvents()


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def all_widgets(name):
    out = []
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == name:
                out.append(w)
        except RuntimeError:
            pass
    return out


def named(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def find_key(widget):
    """Ctrl+F delivered to a widget"""
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(
            widget, QtGui.QKeyEvent(kind, QtCore.Qt.Key_F, QtCore.Qt.ControlModifier, "\x06"))
    settle(300)


def box_text():
    """What the box shows, or None when it is not open"""
    box = named("OmniSearchBox")
    edit = named("OmniSearchEdit")
    if box is None or edit is None or not box.isVisible():
        return None
    return edit.text()


def popup_rows():
    """The rows of the list under the box. The list waits for the box to be
    given the focus, which a test run beside other windows may never be: the
    text is said again as the box says it then."""
    edit = named("OmniSearchEdit")
    for attempt in range(2):
        for w in QtWidgets.QApplication.topLevelWidgets():
            if (isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible()
                    and w.model() is not None):
                model = w.model()
                return [str(model.index(i, 0).data()) for i in range(model.rowCount())]
        if attempt == 0:
            typed(edit.text())
    return []


def typed(text):
    edit = named("OmniSearchEdit")
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(400)


def dismiss():
    box = named("OmniSearchBox")
    if box is not None and box.isVisible():
        box.hide()
    settle(200)


def tree_view():
    for w in all_widgets("Gui::TreeWidget"):
        if w.isVisible():
            return w
    return None


def editors():
    """The property view's two editors: (data, view)"""
    data = view = None
    for w in all_widgets("Gui::PropertyEditor::PropertyEditor"):
        parent = w.parentWidget()
        while parent is not None and cls(parent) != "Gui::PropertyView":
            parent = parent.parentWidget()
        if parent is None or not parent.isVisible():
            continue
        tabs = parent.findChild(QtWidgets.QTabWidget)
        # the View tab is the first, the Data tab the second
        if tabs is not None and tabs.indexOf(w) == 0:
            view = w
        else:
            data = w
    return data, view


def marked(tree, label):
    """Whether the item of that label has a background of its own, and
    whether it is in sight"""
    for index in rows(tree):
        if index.data(QtCore.Qt.DisplayRole) == label:
            brush = index.data(QtCore.Qt.BackgroundRole)
            has = brush is not None and QtGui.QBrush(brush).style() != QtCore.Qt.NoBrush
            rect = tree.visualRect(index)
            seen = 0 <= rect.top() and rect.bottom() <= tree.viewport().height()
            return has, seen
    return False, False


def rows(view):
    out = []
    index = view.model().index(0, 0)
    while index.isValid():
        out.append(index)
        index = view.indexBelow(index)
    return out


def start():
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Crate")
    doc.addObject("Part::Cylinder", "Pillar")
    for i in range(40):
        doc.addObject("Part::Sphere", "Ball%02d" % i)
    doc.recompute()
    settle(800)


def from_the_tree():
    tree = tree_view()
    if not check("the tree view is shown", tree is not None):
        return
    panel = tree.parentWidget()
    check("the tree's panel has no line edit under the tree",
          cls(panel) == "Gui::TreePanel" and not panel.findChildren(QtWidgets.QLineEdit),
          cls(panel))

    tree.scrollToTop()
    find_key(tree)
    check("Ctrl+F in the tree view opens the omni search with '/ ' typed",
          box_text() == "/ ", box_text())
    typed("/ Ball39")
    has, seen = marked(tree, "Ball39")
    check("a name typed after it: the tree has scrolled to that item", seen)
    check("and marked it", has)
    dismiss()
    has, seen = marked(tree, "Ball39")
    check("the box closed, the mark is gone", not has)

    action = None
    for a in tree.findChildren(QtGui.QAction):
        if a.text().replace("&", "").startswith("Search"):
            action = a
    if check("the tree has its 'Search...' action", action is not None):
        action.trigger()
        settle(300)
        check("which opens the omni search with '/ ' typed", box_text() == "/ ", box_text())
        dismiss()


def from_the_property_view():
    doc = FreeCAD.getDocument(DOC)
    data, view = editors()
    if not check("the property view has its two editors", data is not None and view is not None):
        return
    FreeCADGui.Selection.clearSelection()
    FreeCADGui.Selection.addSelection(doc.getObject("Crate"))
    settle(600)

    find_key(data)
    check("one object selected, Ctrl+F in the Data tab: '/ Crate.'",
          box_text() == "/ Crate.", box_text())
    listed = popup_rows()
    check("the list under the box has the object's properties",
          any("Length" in r for r in listed), listed[:8])
    dismiss()

    find_key(view)
    check("in the View tab: '/ Crate.ViewObject.'",
          box_text() == "/ Crate.ViewObject.", box_text())
    listed = popup_rows()
    check("the list has the view provider's properties",
          any("ShapeColor" in r or "Visibility" in r for r in listed), listed[:8])
    dismiss()

    FreeCADGui.Selection.addSelection(doc.getObject("Pillar"))
    settle(600)
    find_key(data)
    check("two objects selected, the Data tab: '/ .'", box_text() == "/ .", box_text())
    dismiss()
    find_key(view)
    check("the View tab: '/ .ViewObject.'", box_text() == "/ .ViewObject.", box_text())
    dismiss()

    # nothing selected: the property view shows the document the tree has selected
    FreeCADGui.Selection.clearSelection()
    tree = tree_view()
    for index in rows(tree):
        if index.data(QtCore.Qt.DisplayRole) in (DOC, DOC + " *"):
            tree.setCurrentIndex(index)
            tree.selectionModel().select(index, QtCore.QItemSelectionModel.ClearAndSelect)
    settle(800)
    find_key(data)
    check("nothing selected, the document selected in the tree, the Data tab: '/ %s#.'" % DOC,
          box_text() == "/ %s#." % DOC, box_text())
    dismiss()
    find_key(view)
    check("the View tab: '/ %s#.ActiveView.'" % DOC,
          box_text() == "/ %s#.ActiveView." % DOC, box_text())
    dismiss()


def elsewhere():
    view = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
    find_key(view.graphicsView())
    check("Ctrl+F in the 3D view opens nothing", box_text() is None, box_text())
    dismiss()


def finish():
    dismiss()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, mw().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("ABORT in %s: %s" % (fn.__name__, traceback.format_exc().replace("\n", " | ")))
        advance()

    QtCore.QTimer.singleShot(delay, run)


STEPS.append((2500, start))
STEPS.append((500, from_the_tree))
STEPS.append((300, from_the_property_view))
STEPS.append((300, elsewhere))
advance()
