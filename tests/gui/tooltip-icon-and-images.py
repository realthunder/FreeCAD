"""A tip shows its whole icon, and a tip's own pictures stay in it.

Once the 3D view has preselected anything, tips are drawn by Gui::TipLabel
instead of Qt (Gui::ToolTip installs itself on the application at its first
use). It lays the text out, and draws the icon an action put in its tip --
an <img> floated right -- beside the text, larger. Two things went wrong
with that, both seen by hand as "clipped" tips:

  - a tip of two lines is lower than its icon, and the label was sized to
    the icon's height alone while the icon is drawn inside the label's
    margin: the last rows of the picture were cut off;
  - the icon was found as the FIRST <img> of any tip. The navigation
    indicator's tips are a table of mouse-button pictures, so the first
    cell of the table was emptied and its picture hung in the corner.

Claims:

  - after a preselection a toolbar button's tip is a Gui::TipLabel (the
    instrument);
  - every toolbar tip with an icon is at least as high as the icon and the
    label's margins, and at least one of them has text lower than its icon;
  - a toolbar tip's icon is beside the text, not in it;
  - a navigation style's tip keeps every picture of its table in the text,
    and has no icon beside it.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def shown_tip():
    for w in QtWidgets.QApplication.allWidgets():
        if w.metaObject().className() in ("Gui::TipLabel", "QTipLabel") and w.isVisible():
            return w
    return None


def hover(widget, pos):
    """What a hover sends; the tip it brought up, or None."""
    event = QtGui.QHelpEvent(QtCore.QEvent.ToolTip, pos, widget.mapToGlobal(pos))
    QtWidgets.QApplication.sendEvent(widget, event)
    settle(0.6)
    return shown_tip()


def text_of(tip):
    labels = [c for c in tip.children() if isinstance(c, QtWidgets.QLabel)]
    return labels[0] if labels else None


def run():
    menu = None
    try:
        mw = FreeCADGui.getMainWindow()
        # Gui::ToolTip comes alive at the first preselection.
        doc = FreeCAD.newDocument("Tips")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        settle(1.0)
        FreeCADGui.Selection.setPreselection(box, "Face1")
        settle(0.3)
        FreeCADGui.Selection.clearPreselection()
        settle(0.3)

        buttons = []
        for toolbar in mw.findChildren(QtWidgets.QToolBar):
            if toolbar.isVisible():
                buttons += [b for b in toolbar.findChildren(QtWidgets.QToolButton)
                            if b.isVisible() and b.toolTip()]
        if not buttons:
            raise RuntimeError("no toolbar button with a tip")

        first = hover(buttons[0], buttons[0].rect().center())
        if not check("after a preselection a toolbar button's tip is a Gui::TipLabel",
                     first is not None and first.metaObject().className() == "Gui::TipLabel",
                     first and first.metaObject().className()):
            raise RuntimeError("the tips are not the fork's; nothing to judge")

        cut = []
        in_text = []
        with_icon = 0
        low = 0
        for button in buttons[:60]:
            tip = hover(button, button.rect().center())
            if tip is None:
                continue
            pixmap = tip.pixmap()
            if pixmap is None or pixmap.isNull():
                continue
            with_icon += 1
            name = button.defaultAction().objectName() if button.defaultAction() else "?"
            text = text_of(tip)
            if text is not None and text.sizeHint().height() < pixmap.height():
                low += 1
            if tip.height() < pixmap.height() + 2 * tip.margin():
                cut.append((name, tip.height(), pixmap.height(), tip.margin()))
            if text is not None and "<img" in text.text():
                in_text.append(name)
        check("every toolbar tip with an icon is as high as the icon and the margins",
              with_icon > 0 and not cut, "%d tips with an icon, cut: %s" % (with_icon, cut[:4]))
        check("at least one of them has text lower than its icon", low > 0, low)
        check("a toolbar tip's icon is beside the text, not in it", not in_text, in_text[:4])

        nav = mw.findChild(QtWidgets.QWidget, "NavigationIndicator")
        menu = nav.menu() if nav is not None and hasattr(nav, "menu") else None
        if menu is None:
            raise RuntimeError("no navigation indicator in the status bar")
        menu.popup(nav.mapToGlobal(QtCore.QPoint(0, -menu.sizeHint().height())))
        settle(0.5)
        styles = 0
        lost = []
        beside = []
        for action in menu.actions():
            wanted = action.toolTip().count("<img")
            if wanted < 2:
                continue
            tip = hover(menu, menu.actionGeometry(action).center())
            if tip is None:
                continue
            styles += 1
            text = text_of(tip)
            got = text.text().count("<img") if text is not None else -1
            if got != wanted:
                lost.append((action.text(), got, wanted))
            pixmap = tip.pixmap()
            if pixmap is not None and not pixmap.isNull():
                beside.append(action.text())
        check("a navigation style's tip keeps every picture of its table in the text",
              styles > 0 and not lost, "%d styles, short: %s" % (styles, lost[:3]))
        check("and has no icon beside it", styles > 0 and not beside, beside[:3])
    except Exception:
        note("ABORT:\n" + traceback.format_exc())

    if menu is not None:
        menu.close()
    for name in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


# Deferred: a script handed to FreeCAD runs before the event loop is up.
QtCore.QTimer.singleShot(3000, run)
