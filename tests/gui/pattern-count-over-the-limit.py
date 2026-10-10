"""A pattern keeps the count its file holds, over the limit on typed-in
counts, through a load and through its panel.

docs/HandsOnQueue.md entry 58: scanner.FCStd's code wheel is a polar pattern
of 1024 occurrences, and it came back with 1000 ("now I remembered, yes it
is 1024", 2026-10-10). The error was the limit's:
Mod/Part/MaximumPatternOccurrences (1000) is there to stop a typo ("a typo
asked for two billion copies", upstream 293726c5d8), and the first port of
it cut a count read from a file as well, and recomputed the object with it.
Leaving the count alone at the load is not enough: the panel's OK and the
property editor store a count again by assigning it from Python, which the
constraint clamps -- the panel showed 1000 for 1024 and stored that. So an
object restored with more than the limit has a range of its own, which ends
at what it holds.

A link array (elements are links, so a thousand of them cost nothing) saved
with 3 occurrences, the file's count rewritten to the limit and 24 more,
loaded again. Claims:
  - the array holds that count after the load, and is not marked for a
    recompute by it;
  - its panel's box shows that count;
  - OK in the panel, nothing typed: the array still holds it;
  - recomputed, the array has that many elements;
  - it cannot be raised past what the file held, and a new array is still
    stopped at the limit.

And the panel SAYS so (the same entry, 2026-10-10: "add some warning when
enter edit if it exceeds ... mention this setting in warning message"): a
count that stops at an odd number with no word of why reads as a defect.
  - the panel of the array over the limit carries a note with the count, the
    limit and the setting's name, Mod/Part/MaximumPatternOccurrences;
  - so does the panel of an array along a path, whose count is another
    widget's;
  - the panel of an array within the limit carries none.
"""
import os
import re
import time
import traceback
import zipfile

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
LIMIT = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Mod/Part").GetInt(
    "MaximumPatternOccurrences", 1000)
HELD = LIMIT + 24
STEPS = []
SEEN = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def make():
    FreeCADGui.getMainWindow().showMaximized()
    doc = FreeCAD.newDocument("CountOverLimit")
    source = doc.addObject("Part::Box", "Source")
    array = doc.addObject("App::LinkArray", "Array")
    array.LinkedObject = source
    array.Occurrences = 3
    array.Length = 30
    line = doc.addObject("Part::Line", "Line")
    line.X2, line.Y2, line.Z2 = 100, 0, 0
    along = doc.addObject("App::LinkArray", "Along")
    along.LinkedObject = source
    along.PatternType = "Path"
    along.Path = (line, ["Edge1"])
    along.Count = 3
    doc.recompute()
    src = os.path.join(OUT, "three.FCStd")
    dst = os.path.join(OUT, "over-the-limit.FCStd")
    doc.saveAs(src)
    FreeCAD.closeDocument(doc.Name)
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, "w") as zo:
        for item in zi.infolist():
            data = zi.read(item.filename)
            if item.filename == "Document.xml":
                xml = data.decode("utf-8")
                start = xml.index('<Object name="Array"')
                prop = xml.index('<Property name="Occurrences" ', start)
                xml = xml[:prop] + re.sub(r'<Integer value="3"/>', '<Integer value="%d"/>' % HELD,
                                          xml[prop:], count=1)
                start = xml.index('<Object name="Along"')
                prop = xml.index('<Property name="Count" ', start)
                xml = xml[:prop] + re.sub(r'<Integer value="3"/>', '<Integer value="%d"/>' % HELD,
                                          xml[prop:], count=1)
                data = xml.encode("utf-8")
            zo.writestr(item, data)
    doc = FreeCAD.openDocument(dst)
    SEEN["doc"] = doc.Name
    array = doc.getObject("Array")
    check("the array holds the file's count after the load", array.Occurrences == HELD,
          "%d, the file's %d, the limit %d" % (array.Occurrences, HELD, LIMIT))
    check("and the load asks for no recompute of it", "Touched" not in array.State, array.State)
    along = doc.getObject("Along")
    check("the array along a path holds the file's count too", along.Count == HELD, along.Count)


def widen():
    """The panel at a width it is read at: the test profile's dock is narrower than the panel,
    and a picture of it cuts every row short."""
    mw = FreeCADGui.getMainWindow()
    for w in mw.findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::TaskView::TaskView":
            dock = w
            while dock is not None and not isinstance(dock, QtWidgets.QDockWidget):
                dock = dock.parentWidget()
            if dock is not None:
                mw.resizeDocks([dock], [460], QtCore.Qt.Horizontal)
            return


def edit(name):
    def fn():
        doc = FreeCAD.getDocument(SEEN["doc"])
        gdoc = FreeCADGui.getDocument(doc.Name)
        gdoc.resetEdit()
        gdoc.setEdit(doc.getObject(name), 0)
        widen()
    fn.__name__ = "edit_" + name
    return fn


def said():
    """The notes naming the setting that the panel shows."""
    return [w.text() for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QLabel)
            if w.isVisible() and "MaximumPatternOccurrences" in w.text()]


def picture(tag):
    for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QWidget):
        if w.metaObject().className() == "Gui::TaskView::TaskView" and w.isVisible():
            w.grab().save(os.path.join(OUT, "panel-%s.png" % tag))
            note("NOTE picture panel-%s.png" % tag)
            return


def says(tag, what):
    def fn():
        texts = said()
        check("the panel of %s says the count is over the limit, once" % what, len(texts) == 1,
              texts)
        if texts:
            check("naming the count and the limit",
                  str(HELD) in texts[0] and str(LIMIT) in texts[0], texts[0])
        picture(tag)
    fn.__name__ = "says_" + tag
    return fn


def says_nothing():
    values = [w.value() + (1 << 31) for w in boxes()]
    check("the panel of an array within the limit is up", LIMIT in values, values)
    check("and says nothing of a limit", not said(), said())
    picture("within")


def boxes():
    return [w for w in FreeCADGui.getMainWindow().findChildren(QtWidgets.QAbstractSpinBox)
            if w.metaObject().className() == "Gui::UIntSpinBox" and w.isVisible()]


def look():
    array = FreeCAD.getDocument(SEEN["doc"]).getObject("Array")
    # a UIntSpinBox is a QSpinBox underneath, its value shifted by 2^31
    values = [w.value() + (1 << 31) for w in boxes()]
    if not check("the panel is up, with its count boxes", len(values) >= 1, values):
        STEPS.clear()
        return
    check("the panel's box shows the count the array holds", HELD in values, values)
    check("and opening the panel has not changed it", array.Occurrences == HELD, array.Occurrences)


def accept():
    for box in FreeCADGui.getMainWindow().findChildren(QtWidgets.QDialogButtonBox):
        ok = box.button(QtWidgets.QDialogButtonBox.Ok)
        if ok is not None and ok.isVisible():
            ok.click()
            SEEN["accepted"] = True
            return
    check("the panel has an OK button", False)
    STEPS.clear()


def after():
    doc = FreeCAD.getDocument(SEEN["doc"])
    array = doc.getObject("Array")
    check("after OK, nothing typed, the array still holds it", array.Occurrences == HELD,
          array.Occurrences)
    array.touch()
    doc.recompute()
    check("recomputed, it has that many elements",
          array.ElementCount == HELD and array.getStatusString() == "Valid",
          "%d elements, %s" % (array.ElementCount, array.getStatusString()))
    array.Occurrences = HELD + 1
    check("it cannot be raised past what the file held", array.Occurrences == HELD,
          array.Occurrences)
    other = doc.addObject("App::LinkArray", "Other")
    other.LinkedObject = doc.getObject("Source")
    other.Occurrences = HELD
    check("and a new array's count is still stopped at the limit", other.Occurrences == LIMIT,
          other.Occurrences)


def finish():
    try:
        gdoc = FreeCADGui.getDocument(SEEN.get("doc", "")) if SEEN.get("doc") else None
        if gdoc is not None:
            gdoc.resetEdit()
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


STEPS.append((1500, make))
STEPS.append((1500, edit("Array")))
STEPS.append((1500, look))
STEPS.append((300, says("over", "an array over the limit")))
STEPS.append((300, accept))
STEPS.append((1500, after))
STEPS.append((500, edit("Along")))
STEPS.append((1500, says("along", "an array along a path")))
STEPS.append((500, edit("Other")))
STEPS.append((1500, says_nothing))
advance()
