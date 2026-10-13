"""An overlaid panel is see-through but for what backs its items, and its
transparent mode keeps it so while it has the focus.

docs/HandsOnQueue.md entry 69: "when the panel is moved to overlay mode
change its background to transparent. change default tree view item
background (150 alpha) that is the same as the current theme 3d view
background color ... Same for property view, transparent background for
empty area, keep the alternate item background (150 alpha), and same
(non-alternate) item background as tree view. Also 150 alpha for task panel
background, both overlay and in view mode", then "only overlay mode, add a
setting to auto follow theme color, default true ... the first color ...
theme's own panel colour ... make the overlay alpha a setting. implement it
as qt property that can be override by stylesheet", and on a panel that has
the pointer: "overlay has a setting, 'transparent mode' to control this
behavior, but right now it seems borken, fix it".

Found with it: the see-through look was given to the widgets an overlay
held when its mode was set, so of the docks put there together all but the
first stayed solid until the pointer had been in the overlay and out again.

Every widget is painted onto a clear picture: what it does not paint stays
clear, and what it paints has the colour and the opacity it was painted
with. Under Dark and under Light:
  - all docks put in overlays, the focus in the 3D view: the tree view and
    the property view have their see-through look; the tree's ground below
    its last item is clear, and so is a property view that shows nothing;
  - an item of the tree is backed with the first colour of the 3D view's
    background at View/DockOverlayAlpha, 150;
  - a row of the property view is backed with that same colour, or with the
    alternate rows' colour, both at that opacity, turn about;
  - the focus in the tree, transparent mode off: the tree is solid; mode on:
    it keeps its see-through look, its items backed as before;
  - View/DockOverlayAlpha 80: items and rows at 80;
  - View/DockOverlayThemeItemColor off: the tree view's own item background
    colour as it is stored, and none while that is empty;
  - the tree's overlayAlpha and overlayItemBackground properties, as a style
    sheet would set them, win over the settings;
  - a task panel, in an overlaid Tasks tab and in its view: its ground is
    the colour it has when docked, at View/DockOverlayAlpha.
And then, the docks overlaid, Dark applied and Light after it:
  - the task panel's ground is the colour of the theme just applied, at
    that opacity, not of the theme being left (entry 85).
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
TREE = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/TreeView")
Control = FreeCADGui.Control
DOC = "SeeThrough"
STEPS = []
DOCKED = {}  # theme -> the ground of a docked task panel under it
ALPHA = 150
NEAR = 4    # of 255, how far a painted colour may be from the one asked for


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


def painted(tag, widget):
    """The widget as its parent paints it, onto a clear picture: what it
    does not paint stays clear, a ground of its own is there once. (Painted
    by itself a widget either has no ground of its own at all, or, asked for
    one, has it laid twice where a style sheet gives it.)"""
    parent = widget.parentWidget()
    image = QtGui.QImage(parent.size(), QtGui.QImage.Format_ARGB32)
    image.fill(0)
    painter = QtGui.QPainter(image)
    parent.render(painter, QtCore.QPoint(), QtGui.QRegion(), QtWidgets.QWidget.DrawChildren)
    painter.end()
    image = image.copy(widget.geometry())
    image.save(os.path.join(OUT, tag + ".png"))
    return image


def ground(image, rect):
    """The commonest colour of a part of a picture, as (r, g, b, a), and its
    share of the part: what backs the text there"""
    rect = rect.intersected(image.rect())
    counts = {}
    for y in range(rect.top(), rect.bottom() + 1):
        for x in range(rect.left(), rect.right() + 1):
            p = image.pixel(x, y)
            counts[p] = counts.get(p, 0) + 1
    if not counts:
        return (0, 0, 0, 0), 0.0
    p = max(counts, key=counts.get)
    share = counts[p] / float(sum(counts.values()))
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255, (p >> 24) & 255), share


def near(a, b, tolerance=NEAR):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def unpacked(value):
    return ((value >> 24) & 255, (value >> 16) & 255, (value >> 8) & 255, value & 255)


def view_colour():
    """The first colour of the 3D view's background, or its one colour"""
    if VIEW.GetBool("Gradient", True) or VIEW.GetBool("RadialGradient", False):
        return unpacked(VIEW.GetUnsigned("BackgroundColor2", 0x333365FF))[:3]
    return unpacked(VIEW.GetUnsigned("BackgroundColor", 0xEAE5DCFF))[:3]


def tree_view():
    for w in all_widgets("Gui::TreeWidget"):
        if w.isVisible():
            return w
    return None


def property_view():
    for w in all_widgets("Gui::PropertyEditor::PropertyEditor"):
        if w.isVisible():
            return w
    return None


def rows(view):
    """The indexes of the rows a tree shows, top to bottom. By the model:
    an item of the tree taken hold of from a script is not always there
    when it is asked a second question."""
    out = []
    index = view.indexAt(QtCore.QPoint(4, 4))
    while index.isValid():
        out.append(index)
        index = view.indexBelow(index)
    return out


def item_ground(tag, tree, label="Box3"):
    """What backs an item of the tree that is not selected: the start of its
    row, where the backing is whatever the label's length"""
    for index in rows(tree):
        if index.data(QtCore.Qt.DisplayRole) == label:
            rect = tree.visualRect(index)
            image = painted(tag, tree.viewport())
            return ground(image, QtCore.QRect(rect.left(), rect.top() + 1,
                                              min(rect.width(), 70), rect.height() - 2))
    return (0, 0, 0, 0), 0.0


def below_items(tag, tree):
    shown = rows(tree)
    bottom = tree.visualRect(shown[-1]).bottom() if shown else 0
    image = painted(tag, tree.viewport())
    return ground(image, QtCore.QRect(0, bottom + 6, image.width(),
                                      image.height() - bottom - 6))


def rows_ground(tag, editor):
    """What backs each row of the property view that holds a property and is
    not the current one, top to bottom"""
    image = painted(tag, editor.viewport())
    model = editor.model()
    out = []
    for index in rows(editor):
        rect = editor.visualRect(index)
        if rect.top() >= image.height():
            break
        if model.rowCount(index) == 0 and index != editor.currentIndex():
            out.append(ground(image, QtCore.QRect(rect.left() + 2, rect.top() + 2,
                                                  rect.width() - 4, rect.height() - 4))[0])
        else:
            out.append(None)
    return out


def focus_view():
    view = FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")[0]
    mw().setActiveWindow(view)
    view.graphicsView().setFocus(QtCore.Qt.OtherFocusReason)
    settle(1500)


def focus_tree(tree):
    mw().activateWindow()
    tree.setFocus(QtCore.Qt.OtherFocusReason)
    settle(1500)
    w = QtWidgets.QApplication.focusWidget()
    return w is not None and (w is tree or tree.isAncestorOf(w))


def see_through(w):
    return w.testAttribute(QtCore.Qt.WA_NoSystemBackground)


class Panel:
    def __init__(self):
        self.form = QtWidgets.QWidget()
        self.form.setWindowTitle("a panel")
        layout = QtWidgets.QVBoxLayout(self.form)
        layout.addWidget(QtWidgets.QLabel("a line", self.form))
        layout.addSpacing(60)

    def accept(self):
        return True

    def reject(self):
        return True


def panel_ground(tag):
    """The ground of the content of the task panel that is shown"""
    for frame in mw().findChildren(QtWidgets.QFrame):
        if frame.property("class") != "content" or not frame.isVisible():
            continue
        parent = frame.parentWidget()
        if parent is None or not parent.inherits("QSint::ActionGroup"):
            continue
        if frame.width() < 40 or frame.height() < 40:
            continue
        image = painted(tag, frame)
        return ground(image, image.rect().adjusted(3, 3, -3, -3))
    return None


def one_theme(theme):
    if not FreeCADGui.applyTheme(theme):
        check("%s: the theme is applied" % theme, False)
        return
    settle(6000)
    doc = FreeCAD.getDocument(DOC)
    asked = view_colour()

    # docked: what a task panel's ground is under this theme
    VIEW.SetBool("TaskPanelInView", False)
    focus_view()
    Control.showDialog(Panel())
    settle(800)
    docked = panel_ground("%s-panel-docked" % theme)
    DOCKED[theme] = docked
    Control.closeDialog()
    settle(500)
    check("%s: a docked task panel has a ground to compare with" % theme,
          docked is not None and docked[0][3] > 250, docked)

    # all docks into overlays together, the focus in the 3D view
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)
    FreeCADGui.Selection.clearSelection()
    focus_view()
    tree = tree_view()
    editor = property_view()
    if not check("%s: the tree view and the property view are shown in overlays" % theme,
                 tree is not None and editor is not None
                 and bool(all_widgets("Gui::OverlayTabWidget"))):
        return
    check("%s: put in overlays together, the tree view has its see-through look" % theme,
          see_through(tree))
    check("%s: ... and so has the property view" % theme, see_through(editor))
    colour, share = below_items("%s-tree-below" % theme, tree)
    check("%s: the tree's ground below its last item is clear" % theme,
          colour[3] == 0 and share > 0.95, "%s over %.0f%%" % (colour, 100 * share))
    image = painted("%s-property-empty" % theme, editor.viewport())
    colour, share = ground(image, image.rect())
    check("%s: a property view that shows nothing is clear" % theme,
          colour[3] == 0 and share > 0.95, "%s over %.0f%%" % (colour, 100 * share))

    colour, share = item_ground("%s-tree-item" % theme, tree)
    check("%s: a tree item is backed with the 3D view's first colour at %d" % (theme, ALPHA),
          near(colour, asked + (ALPHA,)), "%s, asked %s" % (colour, asked + (ALPHA,)))
    item = colour

    FreeCADGui.Selection.addSelection(doc.getObject("Box0"))
    settle(800)
    every = rows_ground("%s-property-rows" % theme, editor)
    backed = [r for r in every if r is not None]
    kinds = sorted(set(backed))
    if check("%s: the property view shows rows of two kinds" % theme,
             len(backed) >= 4 and len(kinds) == 2, "%d rows, %s" % (len(backed), kinds)):
        check("%s: one kind is backed as the tree's items are" % theme,
              any(near(k, item) for k in kinds), "%s, the tree's %s" % (kinds, item))
        check("%s: both kinds at %d" % (theme, ALPHA),
              all(abs(k[3] - ALPHA) <= NEAR for k in kinds), kinds)
        # of two rows one under the other; a row left out (a group's title,
        # the current row) is between its neighbours all the same
        check("%s: the kinds take turns" % theme,
              all(every[i] != every[i + 1] for i in range(len(every) - 1)
                  if every[i] is not None and every[i + 1] is not None), every)
    FreeCADGui.Selection.clearSelection()
    settle(300)

    # the focus in the tree: solid without the transparent mode, see-through with it
    check("%s: the focus is in the tree" % theme, focus_tree(tree))
    # the whole of it: with the focus the tree has its header and title, and
    # may have no ground left below its last item
    image = painted("%s-tree-focused-solid" % theme, tree.viewport())
    colour, share = ground(image, image.rect())
    check("%s: transparent mode off, the tree with the focus is solid" % theme,
          not see_through(tree) and colour[3] > 250, "%s over %.0f%%" % (colour, 100 * share))
    focus_view()
    FreeCADGui.runCommand("Std_DockOverlayTransparentAll")
    settle(1500)
    check("%s: the focus is in the tree again" % theme, focus_tree(tree))
    image = painted("%s-tree-focused-transparent" % theme, tree.viewport())
    colour, share = ground(image, image.rect())
    check("%s: transparent mode on, the tree with the focus keeps its see-through look" % theme,
          see_through(tree) and colour[3] < 255,
          "%s over %.0f%%" % (colour, 100 * share))
    check("%s: ... and the property view beside it" % theme, see_through(editor))
    colour, share = item_ground("%s-tree-item-focused" % theme, tree)
    check("%s: ... its items backed as before" % theme, near(colour, item),
          "%s, before %s" % (colour, item))
    focus_view()
    FreeCADGui.runCommand("Std_DockOverlayTransparentAll")
    settle(1500)
    check("%s: transparent mode off again, the focus away: see-through as at first" % theme,
          see_through(tree) and see_through(editor))

    # the two settings
    VIEW.SetInt("DockOverlayAlpha", 80)
    settle(1200)
    colour, share = item_ground("%s-tree-item-80" % theme, tree)
    check("%s: View/DockOverlayAlpha 80, a tree item at 80" % theme,
          near(colour, asked + (80,)), colour)
    FreeCADGui.Selection.addSelection(doc.getObject("Box0"))
    settle(800)
    kinds = sorted(set(r for r in rows_ground("%s-property-rows-80" % theme, editor)
                       if r is not None))
    check("%s: ... and the property view's rows" % theme,
          len(kinds) == 2 and all(abs(k[3] - 80) <= NEAR for k in kinds), kinds)
    FreeCADGui.Selection.clearSelection()
    VIEW.RemInt("DockOverlayAlpha")
    settle(1200)

    own = TREE.GetUnsigned("ItemBackground", 0)
    VIEW.SetBool("DockOverlayThemeItemColor", False)
    TREE.SetUnsigned("ItemBackground", 0)
    settle(1200)
    colour, share = item_ground("%s-tree-item-own-none" % theme, tree)
    check("%s: DockOverlayThemeItemColor off and no colour of the tree's own: no backing" % theme,
          colour[3] == 0, colour)
    TREE.SetUnsigned("ItemBackground", 0x204060C8)
    settle(1200)
    colour, share = item_ground("%s-tree-item-own" % theme, tree)
    check("%s: ... the tree's own colour as it is stored" % theme,
          near(colour, (0x20, 0x40, 0x60, 0xC8)), colour)
    VIEW.RemBool("DockOverlayThemeItemColor")
    TREE.SetUnsigned("ItemBackground", own)
    settle(1200)

    # as a style sheet would set them
    tree.setProperty("overlayAlpha", 200)
    settle(300)
    colour, share = item_ground("%s-tree-item-sheet-alpha" % theme, tree)
    check("%s: the tree's overlayAlpha property wins over the setting" % theme,
          near(colour, asked + (200,)), colour)
    tree.setProperty("overlayAlpha", -1)
    tree.setProperty("overlayItemBackground", QtGui.QBrush(QtGui.QColor(10, 20, 30, 90)))
    settle(300)
    colour, share = item_ground("%s-tree-item-sheet-colour" % theme, tree)
    check("%s: ... and its overlayItemBackground property" % theme,
          near(colour, (10, 20, 30, 90)), colour)
    tree.setProperty("overlayItemBackground", QtGui.QBrush())
    settle(300)
    colour, share = item_ground("%s-tree-item-again" % theme, tree)
    check("%s: both unset, the settings answer again" % theme, near(colour, item), colour)

    # a task panel: in an overlaid Tasks tab, then in its view
    focus_view()
    Control.showDialog(Panel())
    settle(1500)
    focus_view()
    found = panel_ground("%s-panel-overlaid" % theme)
    if check("%s: a task panel is shown in an overlay" % theme, found is not None) and docked:
        check("%s: its ground is the docked panel's colour at %d" % (theme, ALPHA),
              near(found[0], docked[0][:3] + (ALPHA,)),
              "%s over %.0f%%, docked %s" % (found[0], 100 * found[1], docked[0]))
    VIEW.SetBool("TaskPanelInView", True)
    Control.applyPlaceToViews()
    settle(1200)
    hosts = [h for h in all_widgets("Gui::TaskView::TaskPanelHost") if not h.isHidden()]
    found = panel_ground("%s-panel-in-view" % theme)
    if check("%s: the panel is shown in its view" % theme,
             len(hosts) == 1 and found is not None, len(hosts)) and docked:
        check("%s: in its view its ground is that colour at %d too" % (theme, ALPHA),
              near(found[0], docked[0][:3] + (ALPHA,)),
              "%s over %.0f%%, docked %s" % (found[0], 100 * found[1], docked[0]))
    VIEW.SetBool("TaskPanelInView", False)
    Control.applyPlaceToViews()
    settle(500)
    Control.closeDialog()
    settle(500)

    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)


def start():
    VIEW.SetBool("UseViewArea", True)
    VIEW.SetBool("TaskPanelInView", False)
    doc = FreeCAD.newDocument(DOC)
    for i in range(6):
        doc.addObject("Part::Box", "Box%d" % i)
    doc.recompute()
    settle(800)


def themes():
    for theme in ("Dark", "Light"):
        one_theme(theme)


def switched():
    """A theme applied WITH the docks overlaid (docs/HandsOnQueue.md entry
    85): the overlay's sheet was made when the keys it is chosen by were
    written, before the theme's own colours were in force, and a task
    panel's ground stayed in the colour of the theme being left -- Dark's
    under Light, Light's under Dark, until the next start."""
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)
    for theme in ("Dark", "Light"):
        if not FreeCADGui.applyTheme(theme):
            check("%s, with the docks overlaid: the theme is applied" % theme, False)
            continue
        settle(6000)
        focus_view()
        Control.showDialog(Panel())
        settle(1500)
        focus_view()
        found = panel_ground("%s-panel-theme-switched" % theme)
        docked = DOCKED.get(theme)
        if check("%s applied with the docks overlaid: a task panel is shown in an overlay" % theme,
                 found is not None and docked is not None):
            check("%s applied with the docks overlaid: the panel's ground is THIS theme's at %d"
                  % (theme, ALPHA), near(found[0], docked[0][:3] + (ALPHA,)),
                  "%s over %.0f%%, docked under this theme %s"
                  % (found[0], 100 * found[1], docked[0]))
        Control.closeDialog()
        settle(500)
    FreeCADGui.runCommand("Std_DockOverlayAll")
    settle(2500)


def finish():
    VIEW.SetBool("TaskPanelInView", False)
    VIEW.RemInt("DockOverlayAlpha")
    VIEW.RemBool("DockOverlayThemeItemColor")
    try:
        if Control.activeDialog():
            Control.closeDialog()
    except Exception:
        pass
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
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(delay, run)


STEPS.append((2500, start))
STEPS.append((500, themes))
STEPS.append((500, switched))
advance()
