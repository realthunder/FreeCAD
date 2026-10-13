"""An overlaid tree as the SCREEN has it: no rim around an item's background,
the bar under the pointer as wide as that background, a scroll bar that is
its handle and nothing else.

docs/HandsOnQueue.md entry 87: "dark theme tree item background has a darker
border, which is unwanted. in addition, mouse hovering draw some selection
highlight, and if I move mouse over a range of item, I see a trail of
vanishing item borders", and to the points put: "the trail is unwanted. the
hover highlight is ok to have, but do not draw it for the whole row. draw it
like item background and selection highlight." Entry 88: "when dock overlay
in transparent mode, style the scroll bar to have transparent background and
the make the scroll handle respect the command background alpha value" --
all scroll bars, the theme's own handle colours at View/DockOverlayAlpha,
hovered and pressed too, the track's border and its two ends clear.

The overlay sheets switch on an outline: a blurred dark copy of whatever
the panel paints is laid under it, so that text reads over the 3D view. It
was made of the item backgrounds too (entry 69): the dark lay under the
whole of a background and a pixel beyond its edge -- (124,124,124) at 150
came to the screen as (98,98,98) at 232 in a rim of (50,50,50) -- and under
the theme's bar over the row under the pointer, whose rim on the rows beside
it stayed behind when the pointer went on. tests/gui/overlay-see-through.py
paints the tree alone and saw none of it; this one paints the overlay, the
outline with it.

Runs only with QT_QPA_PLATFORM=offscreen: it moves the pointer.

Claims, under Dark and under Light, all docks overlaid, their transparent
mode on (with it off a panel is solid while the pointer is in it):
  - an item's background is on the screen what the tree paints: the first
    colour of the 3D view's background at View/DockOverlayAlpha;
  - left of it and right of it, on the row's middle line, nothing is painted;
  - the pointer on a row: a bar as wide as the item's background, at that
    opacity; right of it the row is still clear; the rows above and below
    are painted as without the pointer; and with the pointer gone the tree
    is painted as before;
  - the tree's scroll bar: nothing of it more opaque than the opacity
    setting, a handle at that opacity, and the rest of it clear.
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
DOC = "ItemLook"
ALPHA = 180
NEAR = 4
STEPS = []


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


def mw():
    return FreeCADGui.getMainWindow()


def cls(w):
    return w.metaObject().className()


def tree_view():
    for w in QtWidgets.QApplication.allWidgets():
        try:
            if cls(w) == "Gui::TreeWidget" and w.isVisible():
                return w
        except RuntimeError:
            pass
    return None


def overlay_of(w):
    while w is not None:
        if cls(w) == "Gui::OverlayTabWidget":
            return w
        w = w.parentWidget()
    return None


def rows(view):
    out = []
    index = view.indexAt(QtCore.QPoint(4, 4))
    while index.isValid():
        out.append(index)
        index = view.indexBelow(index)
    return out


def shot(tag, tab):
    """The overlay as it is painted, its outline with it, on a clear picture"""
    image = QtGui.QImage(tab.size(), QtGui.QImage.Format_ARGB32)
    image.fill(0)
    painter = QtGui.QPainter(image)
    tab.render(painter, QtCore.QPoint(), QtGui.QRegion(), QtWidgets.QWidget.DrawChildren)
    painter.end()
    image.save(os.path.join(OUT, tag + ".png"))
    return image


def px(image, x, y):
    v = image.pixel(x, y)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)


def near(a, b, tolerance=NEAR):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def unpacked(value):
    return ((value >> 24) & 255, (value >> 16) & 255, (value >> 8) & 255, value & 255)


def view_colour():
    if VIEW.GetBool("Gradient", True) or VIEW.GetBool("RadialGradient", False):
        return unpacked(VIEW.GetUnsigned("BackgroundColor2", 0x333365FF))[:3]
    return unpacked(VIEW.GetUnsigned("BackgroundColor", 0xEAE5DCFF))[:3]


def row_rect(tree, tab, label):
    for index in rows(tree):
        if index.data(QtCore.Qt.DisplayRole) == label:
            r = tree.visualRect(index)
            return QtCore.QRect(tree.viewport().mapTo(tab, r.topLeft()), r.size())
    return None


def commonest(image, rect):
    counts = {}
    rect = rect.intersected(image.rect())
    for y in range(rect.top(), rect.bottom() + 1):
        for x in range(rect.left(), rect.right() + 1):
            p = image.pixel(x, y)
            counts[p] = counts.get(p, 0) + 1
    if not counts:
        return (0, 0, 0, 0)
    p = max(counts, key=counts.get)
    return ((p >> 16) & 255, (p >> 8) & 255, p & 255, (p >> 24) & 255)


def painted_span(image, rect, viewport_right):
    """On a row's middle line: the first run of pixels where something is
    painted, from its first x to its last"""
    y = rect.center().y()
    right = min(viewport_right, image.width())
    x = 0
    while x < right and px(image, x, y)[3] == 0:
        x += 1
    if x >= right:
        return (None, None)
    first = x
    while x < right and px(image, x, y)[3] > 0:
        x += 1
    return (first, x - 1)


def differing(a, b, rect):
    rect = rect.intersected(a.rect())
    n = 0
    for y in range(rect.top(), rect.bottom() + 1):
        for x in range(rect.left(), rect.right() + 1):
            if a.pixel(x, y) != b.pixel(x, y):
                n += 1
    return n


def pointer_to(widget, local):
    QtGui.QCursor.setPos(widget.mapToGlobal(local) + QtCore.QPoint(-2, 0))
    settle(80)
    QtGui.QCursor.setPos(widget.mapToGlobal(local))
    settle(450)


def pointer_away():
    QtGui.QCursor.setPos(mw().mapToGlobal(mw().rect().center()))
    settle(900)


def one_theme(theme):
    if not FreeCADGui.applyTheme(theme):
        check("%s: the theme is applied" % theme, False)
        return
    settle(5000)
    tree = tree_view()
    tab = overlay_of(tree) if tree is not None else None
    if tab is None:
        FreeCADGui.runCommand("Std_DockOverlayAll")
        settle(2500)
        tree = tree_view()
        tab = overlay_of(tree) if tree is not None else None
    if not check("%s: the tree view is shown in an overlay" % theme, tab is not None):
        return
    # The transparent mode on: with it off a panel turns solid while the
    # pointer is in it, and what is looked at here is the see-through tree.
    eye = None
    for button in tab.findChildren(QtWidgets.QAbstractButton):
        if button.objectName() == "OBTN Transparent":
            eye = button
    if eye is not None and not eye.isChecked():
        FreeCADGui.runCommand("Std_DockOverlayTransparentAll")
        settle(1500)
    check("%s: the overlay's transparent mode is on" % theme, eye is not None and eye.isChecked())
    FreeCADGui.Selection.clearSelection()
    pointer_away()
    area_right = tree.viewport().mapTo(tab, QtCore.QPoint(tree.viewport().width(), 0)).x()
    base = shot("%s-1-no-pointer" % theme, tab)
    asked = view_colour() + (ALPHA,)

    rect = row_rect(tree, tab, "Box03")
    if not check("%s: the row is found" % theme, rect is not None):
        return
    first, last = painted_span(base, rect, area_right)
    check("%s: the item's background is cut to the item, not the row" % theme,
          first is not None and last < rect.left() + rect.width() - 20,
          "painted from %s to %s, the row from %d to %d" % (first, last, rect.left(),
                                                           rect.left() + rect.width()))
    backing = QtCore.QRect(first, rect.top(), last - first + 1, rect.height())
    ground = commonest(base, backing.adjusted(2, 2, -2, -2))
    check("%s: on the screen the background is the 3D view's first colour at %d" % (theme, ALPHA),
          near(ground, asked), "%s, asked %s" % (ground, asked))
    y = rect.center().y()
    rim = [px(base, first, y), px(base, last, y)]
    check("%s: its first and last pixel on the row's middle line are that too, no rim" % theme,
          all(near(p, asked) for p in rim), rim)

    # the pointer on the row below
    target = row_rect(tree, tab, "Box04")
    above = row_rect(tree, tab, "Box03")
    below = row_rect(tree, tab, "Box05")
    local = tab.mapTo(mw(), QtCore.QPoint(first + (last - first) // 2, target.center().y()))
    pointer_to(mw(), local)
    hovered = shot("%s-2-pointer-on-Box04" % theme, tab)
    h_first, h_last = painted_span(hovered, target, area_right)
    bar = commonest(hovered, QtCore.QRect(h_first, target.top(), h_last - h_first + 1,
                                          target.height()).adjusted(2, 2, -2, -2))
    check("%s: the pointer on a row draws a bar there" % theme,
          not near(bar, ground, 2), "%s, without the pointer %s" % (bar, ground))
    check("%s: at the opacity of the item backgrounds" % theme, abs(bar[3] - ALPHA) <= NEAR, bar)
    check("%s: as wide as the item's background, not the row" % theme,
          abs(h_first - first) <= 1 and abs(h_last - last) <= 1,
          "bar from %s to %s, background from %s to %s" % (h_first, h_last, first, last))
    check("%s: the rows above and below are painted as without the pointer" % theme,
          differing(hovered, base, above) == 0 and differing(hovered, base, below) == 0,
          "%d and %d pixels differ" % (differing(hovered, base, above),
                                       differing(hovered, base, below)))
    check("%s: with the pointer in it the tree keeps its see-through look" % theme,
          tree.testAttribute(QtCore.Qt.WA_NoSystemBackground)
          and px(hovered, min(h_last + 40, area_right - 2), target.center().y())[3] == 0,
          px(hovered, min(h_last + 40, area_right - 2), target.center().y()))
    pointer_away()
    gone = shot("%s-3-pointer-gone" % theme, tab)
    rows_area = QtCore.QRect(0, above.top(), area_right, below.bottom() - above.top() + 1)
    check("%s: the pointer gone, the rows are painted as before it came" % theme,
          differing(gone, base, rows_area) == 0,
          "%d pixels differ" % differing(gone, base, rows_area))

    # the scroll bar
    bar_widget = tree.verticalScrollBar()
    if check("%s: the tree has a scroll bar to look at" % theme,
             bar_widget.isVisible() and bar_widget.maximum() > 0):
        origin = bar_widget.mapTo(tab, QtCore.QPoint(0, 0))
        x = origin.x() + bar_widget.width() // 2
        alphas = [px(base, x, origin.y() + dy)[3] for dy in range(bar_widget.height())]
        solid = [a for a in alphas if a > ALPHA + NEAR]
        handle = [a for a in alphas if abs(a - ALPHA) <= NEAR]
        clear = [a for a in alphas if a == 0]
        check("%s: the scroll bar lies inside the picture" % theme,
              0 <= x < base.width() and origin.y() + bar_widget.height() <= base.height(),
              "x %d of %d, from y %d for %d of %d"
              % (x, base.width(), origin.y(), bar_widget.height(), base.height()))
        check("%s: nothing of the scroll bar is more opaque than the setting" % theme,
              not solid, "%d of %d pixels down its middle, the most %s"
              % (len(solid), len(alphas), max(alphas)))
        check("%s: its handle is at that opacity" % theme, len(handle) >= 15, len(handle))
        check("%s: and the rest of it is clear" % theme,
              len(clear) + len(handle) >= len(alphas) - 4,
              "%d clear, %d handle, of %d" % (len(clear), len(handle), len(alphas)))


def start():
    platform = QtGui.QGuiApplication.platformName()
    if platform != "offscreen":
        note("NOT RUN: the platform is %r; this script moves the pointer and runs only with "
             "QT_QPA_PLATFORM=offscreen" % platform)
        STEPS[:] = []
        return
    TREE.SetBool("HideScrollBar", False)
    # This platform's screen is 800 by 800, and an overlay in a window of that
    # size is narrower than the tree's dock: the rows' ends and the scroll bar
    # would be cut off. A window has no need to fit the screen here.
    mw().showNormal()
    mw().resize(1500, 900)
    settle(500)
    doc = FreeCAD.newDocument(DOC)
    for i in range(1, 61):
        doc.addObject("Part::Box", "Box%02d" % i)
    doc.recompute()
    settle(800)


def themes():
    for theme in ("Dark", "Light"):
        try:
            one_theme(theme)
        except Exception:
            note("ABORT under %s: %s" % (theme, traceback.format_exc().replace("\n", " | ")))


def finish():
    TREE.RemBool("HideScrollBar")
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
            STEPS[:] = []
        advance()

    QtCore.QTimer.singleShot(delay, run)


STEPS.append((2500, start))
STEPS.append((500, themes))
advance()
