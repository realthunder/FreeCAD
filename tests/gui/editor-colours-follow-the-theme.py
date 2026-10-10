"""The Python editor and the Python console take a theme's colours when it is
applied, all of them and at once, and the colours suit the theme's page.

docs/HandsOnQueue.md entry 30: "switching in combinations got some setting
stuck at a bad combination, such as the python editor colors", and, on the
two overlay themes, "need to modify python editor part to make it more
suitable to the theme" (2026-10-10). Three things were behind it:

  - the Light theme had the Dark theme's syntax colours, pale ones made for
    a dark page, with the text colour alone changed: operators #D4D4D4 and
    function names #DCDCAA on a page of #FAFAFA;
  - the band on the editor's current line was drawn when the cursor moved
    and not when its colour changed, so after a change of theme the line
    under the cursor kept the last theme's band -- the Dark theme's light
    text on the Light theme's light band -- until a key was pressed;
  - the console never took a changed colour for what was in it already:
    every line typed or printed before a change of theme stayed in the last
    theme's colours, the Classic theme's black on the Dark theme's page.

With a Python file open in the editor, the cursor on a line of it, and a
statement, its output and an error in the console, Dark is applied and then
Light, nothing else touched. Claims, for each:
  - the band on the editor's current line is the theme's;
  - every colour the console's text is drawn in is one the theme lists, the
    statement typed before the theme included; its output is in the theme's
    output colour and the error in its error colour;
  - each syntax colour of the theme stands 4 to 1 or better against the
    page the editor is drawn on, and so does the text on the current
    line's band.
"""
import os
import time
import traceback
import xml.etree.ElementTree as ET

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
SYNTAX = ("Text", "Keyword", "Comment", "Block comment", "Number", "String", "Class name",
          "Define name", "Operator", "Python output", "Python error")
SAMPLE = '''# a comment
"""A block comment."""
import math

class Mirror(object):
    def angle(self, turns=2, name='m'):
        value = 3.25 * turns + 0x10
        return [math.sin(value), None, True]
'''
STEPS = []
SEEN = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def find(cls):
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget)
            if w.metaObject().className() == cls]


def listed(theme):
    """The Editor colours a theme's file lists, as #rrggbb"""
    path = os.path.join(FreeCAD.getResourceDir(), "Gui", "PreferencePacks", theme, theme + ".cfg")
    found = {}
    for group in ET.parse(path).getroot().iter("FCParamGroup"):
        if group.get("Name") == "Editor":
            for child in group:
                if child.tag == "FCUInt":
                    found[child.get("Name")] = "#%06x" % (int(child.get("Value")) >> 8)
    return found


def luminance(colour):
    c = QtGui.QColor(colour)
    parts = []
    for v in (c.redF(), c.greenF(), c.blueF()):
        parts.append(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4)
    return 0.2126 * parts[0] + 0.7152 * parts[1] + 0.0722 * parts[2]


def contrast(a, b):
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def open_editor():
    path = os.path.join(OUT, "sample.py")
    with open(path, "w") as f:
        f.write(SAMPLE)
    FreeCADGui.open(path)


def place_cursor():
    editors = find("Gui::PythonEditor")
    if not check("the file is open in the Python editor", len(editors) == 1, len(editors)):
        STEPS.clear()
        return
    e = editors[0]
    c = e.textCursor()
    c.movePosition(QtGui.QTextCursor.Start)
    c.movePosition(QtGui.QTextCursor.Down, QtGui.QTextCursor.MoveAnchor, 6)
    e.setTextCursor(c)
    SEEN["editor"] = e


def console_lines():
    consoles = find("Gui::PythonConsole")
    if not check("the Python console is there", len(consoles) == 1, len(consoles)):
        STEPS.clear()
        return
    py = SEEN["console"] = consoles[0]
    for line in ("value = [1, 2.5, 'text']  # a comment", "print(value)", "1/0"):
        QTest.keyClicks(py, line)
        QTest.keyClick(py, QtCore.Qt.Key_Return)


def console_colours(py):
    """The colours the console's blocks are drawn in: all of them, and those of the lines
    that are the statement, its output and the error"""
    every, by_line = set(), {}
    block = py.document().begin()
    while block.isValid():
        mine = set(r.format.foreground().color().name() for r in block.layout().formats()
                   if r.format.foreground().style() != QtCore.Qt.NoBrush)
        every |= mine
        text = block.text()
        if "value = [1, 2.5" in text:
            by_line["statement"] = mine
        elif text.startswith("[1, 2.5"):
            by_line["output"] = mine
        elif "ZeroDivisionError" in text:
            by_line["error"] = mine
        block = block.next()
    return every, by_line


def apply(theme):
    def fn():
        if not check("%s applies" % theme, FreeCADGui.applyTheme(theme)):
            STEPS.clear()
    fn.__name__ = "apply_" + theme
    return fn


def look(theme):
    def fn():
        want = listed(theme)
        e, py = SEEN["editor"], SEEN["console"]
        e.grab().save(os.path.join(OUT, "%s-editor.png" % theme))
        py.grab().save(os.path.join(OUT, "%s-console.png" % theme))

        bands = e.extraSelections()
        band = bands[0].format.background().color().name() if bands else None
        check("%s: the band on the editor's current line is the theme's, the cursor not moved"
              % theme, band == want["Current line highlight"],
              "%s, the theme's %s" % (band, want["Current line highlight"]))

        every, by_line = console_colours(py)
        palette = set(want[k] for k in SYNTAX)
        check("%s: every colour in the console is one the theme lists" % theme,
              every and every <= palette, "not the theme's: %s" % sorted(every - palette))
        check("%s: the statement typed before the theme is in its colours" % theme,
              by_line.get("statement") and by_line["statement"] <= palette,
              sorted(by_line.get("statement", [])))
        check("%s: its output is in the theme's output colour" % theme,
              by_line.get("output") == set([want["Python output"]]),
              "%s, the theme's %s" % (sorted(by_line.get("output", [])), want["Python output"]))
        check("%s: the error is in the theme's error colour" % theme,
              by_line.get("error") == set([want["Python error"]]),
              "%s, the theme's %s" % (sorted(by_line.get("error", [])), want["Python error"]))

        page = e.palette().color(QtGui.QPalette.Base).name()
        weak = ["%s %s %.1f" % (k, want[k], contrast(want[k], page)) for k in SYNTAX
                if contrast(want[k], page) < 4.0]
        check("%s: each syntax colour stands 4 to 1 or better against the page" % theme,
              not weak, "the page %s; under 4: %s" % (page, weak))
        on_band = contrast(want["Text"], want["Current line highlight"])
        check("%s: and so does the text on the current line's band" % theme, on_band >= 4.0,
              "%.1f" % on_band)
    fn.__name__ = "look_" + theme
    return fn


def finish():
    try:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


def advance():
    if not STEPS:
        finish()
        return
    delay, fn = STEPS.pop(0)

    def run():
        try:
            fn()
        except Exception:
            note("FAIL the test ran, in %s | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
            STEPS.clear()
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((1500, open_editor))
STEPS.append((1000, place_cursor))
STEPS.append((500, console_lines))
for name in ("Dark", "Light"):
    STEPS.append((1500, apply(name)))
    STEPS.append((4000, look(name)))
advance()
