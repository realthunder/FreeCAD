"""The TechDraw annotation preference page and its line style boxes.

A line style preference is stored as a place in the list, and read as a line
number, one more. The page selected a style only when "count > number", so
the LAST style of a list was never selected again -- the box stayed on the
first, and the next Apply stored that. And a box with no current item stored
its -1, which code that reads the preference as an index then trusted
(docs/HandsOnQueue.md entry 19).

Claims:
  - with the preference naming the last style, the box shows the last style;
  - saving the page leaves the preference where it was;
  - a box with no current item stores nothing.
A NOTE says so when the page cannot be made outside the preferences dialog,
and nothing is scored then.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
GROUP = "User parameter:BaseApp/Preferences/Mod/TechDraw/Decorations"
BOXES = (("pcbSectionStyle", "LineStyleSection"), ("pcbCenterStyle", "LineStyleCenter"),
         ("pcbHighlightStyle", "LineStyleHighlight"), ("pcbHiddenStyle", "LineStyleHidden"))


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=20):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def run():
    param = FreeCAD.ParamGet(GROUP)
    had = {key: param.GetInt(key, 12345) for _, key in BOXES}
    page = None
    try:
        import TechDrawGui  # the page's class
        from PySide import QtWidgets
        try:
            page = FreeCADGui.UiLoader().createWidget("TechDrawGui::DlgPrefsTechDrawAnnotationImp")
        except Exception:
            page = None
        if page is None:
            note("NOTE the annotation preference page cannot be made from here; nothing is scored")
            return
        QtCore.QMetaObject.invokeMethod(page, "loadSettings")
        settle()
        for box_name, key in BOXES:
            box = page.findChild(QtWidgets.QComboBox, box_name)
            last = box.count() - 1
            if not check("%s: the list has styles" % box_name, last > 0, box.count()):
                continue
            param.SetInt(key, last)
            QtCore.QMetaObject.invokeMethod(page, "loadSettings")
            settle()
            check("%s: the last style is selected when the preference names it" % box_name,
                  box.currentIndex() == last, (box.currentIndex(), last))
            QtCore.QMetaObject.invokeMethod(page, "saveSettings")
            settle()
            check("%s: saving leaves the preference on the last style" % box_name,
                  param.GetInt(key, 12345) == last, (param.GetInt(key, 12345), last))
            box.setCurrentIndex(-1)
            QtCore.QMetaObject.invokeMethod(page, "saveSettings")
            settle()
            check("%s: no current item stores nothing" % box_name,
                  param.GetInt(key, 12345) == last, (param.GetInt(key, 12345), last))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        if page is not None:
            page.deleteLater()
        for key, value in had.items():
            if value == 12345:
                param.RemInt(key)
            else:
                param.SetInt(key, value)
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1000, run)
