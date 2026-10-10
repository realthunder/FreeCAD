"""What a theme leaves dark on dark, or light on light.

Asked by hand (docs/HandsOnQueue.md entry 11): in the Dark theme the check
box border and the custom title bar's minimize and maximize icons had a
bad colour, and an audit for more of the kind. Each of these was drawn in
a colour fixed for a light surface, or in the colour of the theme the
session had when the thing was made.

Claims, each made in Dark, in Light after it, and in Dark again -- a
theme applied to a running session is the harder case, and going back is
what shows a colour that only ever got set one way:

  - the glyph of each window button of the custom title bar stands out
    from the bar;
  - the edge of an unchecked check box and of a radio button stands out
    from the dialog it is in;
  - the navigation style icon in the status bar stands out from the bar;
  - the text of a spreadsheet cell stands out from the sheet, in a sheet
    that was open when the theme changed;
  - a line the report view held before the theme changed is in the
    colour its text has now.

"Stands out" is a difference in lightness of at least 50 of 255 between a
colour and the surface it is on. For a drawn line -- a glyph, an edge --
it is read from what the widget paints: lighter than a dark surface or
darker than a light one, the way text is, by at least 30 (a one pixel
line is antialiased down to part of its colour). The edge of the dark
theme's check box was DARKER than its dark dialog, which is the defect:
nothing of it was lighter.

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
DOC = "ThemeContrast"
Qt = QtCore.Qt
ENOUGH = 50
LINE = 30


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def lightness(rgb):
    return QtGui.QColor(rgb).lightness()


def drawn(image, rect=None):
    """Whether something is drawn on a surface the way its text would be:
    lighter than a dark surface, darker than a light one, by LINE at least.
    The surface is the lightness most of the image has. A one pixel line is
    antialiased down to a part of its colour, hence the smaller measure."""
    rect = rect or image.rect()
    count = {}
    for y in range(rect.top(), rect.bottom() + 1):
        for x in range(rect.left(), rect.right() + 1):
            v = lightness(image.pixel(x, y))
            count[v] = count.get(v, 0) + 1
    surface = max(count, key=count.get)
    off = max(count) - surface if surface < 128 else surface - min(count)
    return off >= LINE, "%d %s than a surface of %d" % (
        off, "lighter" if surface < 128 else "darker", surface)


def grab(widget, name, within=None):
    """What a widget paints. One that paints no background of its own (a
    title bar button, a check box) is read through 'within', the parent
    that does: alone it comes out on black, whatever the theme."""
    if within is None:
        pixmap = widget.grab()
    else:
        pixmap = within.grab(QtCore.QRect(widget.mapTo(within, QtCore.QPoint(0, 0)), widget.size()))
    image = pixmap.toImage().convertToFormat(QtGui.QImage.Format_RGB32)
    image.save(os.path.join(OUT, name + ".png"))
    return image


def window_buttons():
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QPushButton)
            if w.metaObject().className() == "WindowDecorationButton" and w.isVisible()]


def report_edit():
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QTextEdit):
        if "ReportOutput" in w.metaObject().className():
            return w
    return None


def look(tag, theme, dialog, sheet_view):
    mw = FreeCADGui.getMainWindow()
    buttons = window_buttons()
    check("%s: the custom title bar has its window buttons" % tag, len(buttons) >= 3, len(buttons))
    for b in buttons:
        ok, detail = drawn(grab(b, "%s-%s" % (tag, b.objectName()), mw))
        check("%s: the glyph of %s stands out from the title bar" % (tag, b.objectName()), ok, detail)

    for name in ("unchecked", "radio"):
        w = dialog.findChild(QtWidgets.QWidget, name)
        image = grab(w, "%s-%s" % (tag, name), dialog)
        # the indicator is at the left; the text is left out of the reading
        ok, detail = drawn(image, QtCore.QRect(0, 0, 18, image.height()))
        check("%s: the edge of the %s box stands out from the dialog" % (tag, name), ok, detail)

    indicator = None
    for w in mw.statusBar().findChildren(QtWidgets.QPushButton):
        menu = w.menu()
        if menu is not None and any(a.objectName().startswith("Indicator_Navigation")
                                    for a in menu.actions()):
            indicator = w
    if check("%s: the status bar has the navigation indicator" % tag, indicator is not None):
        icon = indicator.icon().pixmap(32, 32).toImage().convertToFormat(QtGui.QImage.Format_ARGB32)
        ink = [lightness(icon.pixel(x, y)) for y in range(icon.height()) for x in range(icon.width())
               if QtGui.qAlpha(icon.pixel(x, y)) > 200]
        bar = lightness(grab(mw.statusBar(), tag + "-statusbar").pixel(4, 4))
        # the icon's strokes: its lightest or darkest opaque pixels, whichever are further off
        d = max(abs(v - bar) for v in ink) if ink else 0
        mid = sorted(ink)[len(ink) // 2] if ink else bar
        check("%s: the navigation icon stands out from the status bar" % tag,
              abs(mid - bar) >= ENOUGH, "its median ink %d, its furthest %d from the bar's %d" % (mid, d, bar))

    table = sheet_view.findChild(QtWidgets.QTableView)
    model = table.model()
    fg = model.data(model.index(0, 0), Qt.ForegroundRole)
    fg = QtGui.QColor(fg.color() if isinstance(fg, QtGui.QBrush) else fg)
    cell = grab(table.viewport(), tag + "-sheet")
    back = lightness(cell.pixel(table.columnViewportPosition(2) + 8, table.rowViewportPosition(3) + 8))
    check("%s: a cell's text stands out from the open sheet" % tag,
          abs(fg.lightness() - back) >= ENOUGH,
          "text %s (%d) on a sheet of %d" % (fg.name(), fg.lightness(), back))

    edit = report_edit()
    if check("%s: there is a report view" % tag, edit is not None):
        block = edit.document().begin()
        found = None
        while block.isValid():
            if "written before any theme" in block.text():
                found = block
            block = block.next()
        if check("%s: the report view still has the line written at the start" % tag, found is not None):
            formats = found.layout().formats()
            colours = sorted(set(f.format.foreground().color().name() for f in formats))
            back = edit.palette().color(edit.viewport().backgroundRole())
            image = grab(edit.viewport(), tag + "-report")
            back_l = lightness(image.pixel(image.width() - 6, image.height() - 6))
            ok = bool(colours) and all(
                abs(QtGui.QColor(c).lightness() - back_l) >= ENOUGH for c in colours)
            check("%s: that line is in a colour that stands out from the view" % tag, ok,
                  "%s on a view of %d" % (colours, back_l))


def run():
    dialog = None
    try:
        mw = FreeCADGui.getMainWindow()
        mw.showNormal()
        mw.resize(1300, 850)
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow").SetBool("CustomTitleBar", True)
        FreeCAD.Console.PrintMessage("written before any theme was applied\n")
        settle(1.5)

        doc = FreeCAD.newDocument(DOC)
        sheet = doc.addObject("Spreadsheet::Sheet", "Sheet")
        sheet.set("A1", "text")
        doc.recompute()
        sheet.ViewObject.doubleClicked()
        settle(1.5)
        sheet_view = None
        for w in mw.findChildren(QtWidgets.QWidget):
            if w.metaObject().className() == "SpreadsheetGui::SheetView":
                sheet_view = w
        if sheet_view is None:
            raise RuntimeError("no spreadsheet view")

        dialog = QtWidgets.QDialog(mw)
        layout = QtWidgets.QVBoxLayout(dialog)
        box = QtWidgets.QCheckBox("unchecked")
        box.setObjectName("unchecked")
        box.setFocusPolicy(Qt.NoFocus)
        radio = QtWidgets.QRadioButton("radio")
        radio.setObjectName("radio")
        radio.setAutoExclusive(False)
        radio.setFocusPolicy(Qt.NoFocus)
        layout.addWidget(box)
        layout.addWidget(radio)
        dialog.show()
        settle(0.5)

        for tag, theme in (("1-dark", "Dark"), ("2-light", "Light"), ("3-dark", "Dark")):
            if not check("%s: the theme applies" % tag, FreeCADGui.applyTheme(theme), theme):
                continue
            settle(3.0)
            look(tag, theme, dialog, sheet_view)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            if dialog is not None:
                dialog.close()
            FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
