"""The omni search's highlighted row can be read, whatever paints it.

docs/HandsOnQueue.md entry 43: "the omni search list box's highlighted text
color is white, which does not look good with light blue highlight
background". The box's row delegate wrote a selected row in the palette's
HighlightedText. That goes with the palette's Highlight -- and the native
Windows style, which a profile that has chosen no theme runs under, does
not paint the Highlight behind a selected row: it paints a pale blue panel
and writes on it in the ordinary text colour itself. White on pale blue,
a contrast of 1.3.

Claims, for a profile that has chosen no theme and then under each theme
(Light, Dark, Classic): with a row of the command list highlighted, the
text on it stands out from what is painted behind it -- a contrast of 3 or
more (WCAG's ratio; what it asks of large text).
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
Qt = QtCore.Qt
READABLE = 3.0


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


def widget(name):
    for w in QtWidgets.QApplication.allWidgets():
        if w.objectName() == name:
            return w
    return None


def popup_lists():
    """The completers' lists: top-level list views with a model."""
    return [w for w in QtWidgets.QApplication.allWidgets()
            if isinstance(w, QtWidgets.QListView) and w.isWindow() and w.model() is not None]


def luminance(rgb):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return (0.2126 * lin((rgb >> 16) & 255) + 0.7152 * lin((rgb >> 8) & 255)
            + 0.0722 * lin(rgb & 255))


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def look(tag):
    FreeCADGui.runCommand("Std_OmniSearch")
    settle(1.0)
    edit = widget("OmniSearchEdit")
    if not check("%s: the box comes up" % tag, edit is not None and edit.isVisible()):
        return
    text = "/cmd pad"
    edit.setText(text)
    edit.setCursorPosition(len(text))
    edit.textEdited.emit(text)
    settle(0.8)
    shown = [w for w in popup_lists() if w.isVisible()]
    if check("%s: asking for a command shows a list" % tag, len(shown) == 1,
             "%d lists shown" % len(shown)):
        view = shown[0]
        first = view.model().index(0, 0)
        view.setCurrentIndex(first)
        settle(0.3)
        image = view.grab().toImage()
        image.save(os.path.join(OUT, "list-%s.png" % tag))
        rect = view.visualRect(first)
        # Past the icon. Behind the text: the colour most pixels have. The
        # text: the colour that stands out most from it.
        counts = {}
        for y in range(rect.top() + 2, rect.bottom() - 1):
            for x in range(rect.left() + 40, min(rect.right() - 2, rect.left() + 400)):
                p = image.pixel(x, y) & 0xFFFFFF
                counts[p] = counts.get(p, 0) + 1
        if check("%s: the highlighted row is painted" % tag, len(counts) > 1, len(counts)):
            back = max(counts, key=counts.get)
            ink = max(counts, key=lambda p: contrast(p, back))
            ratio = contrast(ink, back)
            check("%s: the text of the highlighted row stands out from the row" % tag,
                  ratio >= READABLE,
                  "text #%06x on #%06x, contrast %.1f" % (ink, back, ratio))
    QtWidgets.QApplication.sendEvent(
        edit, QtGui.QKeyEvent(QtCore.QEvent.KeyPress, Qt.Key_Escape, Qt.NoModifier))
    settle(0.5)


def run():
    try:
        settle(1.0)
        mw = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/MainWindow")
        check("the profile has chosen no theme", mw.GetString("Theme", "") == "",
              mw.GetString("Theme", ""))
        look("no theme")
        for theme in ("Light", "Dark", "Classic"):
            if check("%s: the theme applies" % theme, FreeCADGui.applyTheme(theme)):
                settle(3.0)
                look(theme)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
