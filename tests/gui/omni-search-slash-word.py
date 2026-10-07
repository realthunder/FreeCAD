"""The omni search box: '/word' with no space is an object query.

After the slash, a word that is not a keyword is an object query as it
stands; '/ word' with the space still is one, and is how to ask for an object
named like a keyword. A keyword in full ('/cmd') is the keyword. The
beginning of a keyword ('/c', '/par') could be either, so the chooser lists
the modes it could be and, after them, the objects it matches
(docs/HandsOnQueue.md entry 22, docs/OmniSearch.md sec 1). Before, anything
after the slash that was not a full prefix was the chooser, so '/Crate'
listed nothing.

Claims:
  - '/' lists the three modes and no object;
  - '/c' lists the /cmd mode and the object Crate, and not /param;
  - '/cmd' lists the /cmd mode alone;
  - Down and Tab on the Crate row of that list make the text '/Crate';
  - '/Pillar.Height', no space, resolves: the object is preselected, as it
    is for '/ Pillar.Height';
  - '/ cmd' is an object query: the chooser is not what answers it.
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "OmniSlash"


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + (" | " + str(detail) if detail else ""))
    return cond


def settle(n=30):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()


def key(widget, which):
    for kind in (QtCore.QEvent.KeyPress, QtCore.QEvent.KeyRelease):
        QtWidgets.QApplication.sendEvent(widget, QtGui.QKeyEvent(kind, which, QtCore.Qt.NoModifier))


def popups():
    """The completer popups on screen: (view, the texts of its rows)"""
    res = []
    for w in QtWidgets.QApplication.topLevelWidgets():
        if isinstance(w, QtWidgets.QAbstractItemView) and w.isVisible() and w.model() is not None:
            model = w.model()
            res.append((w, [str(model.index(i, 0).data()) for i in range(model.rowCount())]))
    return res


def chooser_rows():
    """Rows of the popup that holds mode rows, or None when no such popup is up"""
    for view, rows in popups():
        if any(r.startswith("/") for r in rows):
            return view, rows
    return None, None


def preselected():
    try:
        sel = FreeCADGui.Selection.getPreselection()
        return sel.Object.Name if sel and sel.Object else None
    except Exception:
        return None


def run():
    doc = None
    try:
        doc = FreeCAD.newDocument(DOC)
        doc.addObject("Part::Box", "Crate")
        doc.addObject("Part::Cylinder", "Pillar")
        doc.recompute()
        settle()
        FreeCADGui.runCommand("Std_OmniSearch")
        settle()
        mw = FreeCADGui.getMainWindow()
        edit = None
        for w in QtWidgets.QApplication.allWidgets():
            if w.objectName() == "OmniSearchEdit":
                edit = w
        if not check("the box is up", edit is not None and edit.isVisible()):
            return

        def typed(text):
            edit.setText(text)
            edit.setCursorPosition(len(text))
            edit.textEdited.emit(text)
            settle()

        typed("/")
        view, rows = chooser_rows()
        check("'/' lists the three modes and no object", rows == ["/ ", "/cmd ", "/param "], rows)

        typed("/c")
        view, rows = chooser_rows()
        rows = rows or []
        check("'/c' lists the /cmd mode", "/cmd " in rows, rows)
        check("'/c' lists the object Crate after it",
              "/Crate" in rows and "/cmd " in rows and rows.index("/Crate") > rows.index("/cmd "), rows)
        check("'/c' does not list /param", "/param " not in rows, rows)

        if view is not None and "/Crate" in rows:
            for _ in range(rows.index("/Crate")):
                key(view, QtCore.Qt.Key_Down)
            settle()
            key(view, QtCore.Qt.Key_Tab)
            settle()
        check("Down and Tab on the Crate row make the text '/Crate'", edit.text() == "/Crate", edit.text())

        typed("/cmd")
        view, rows = chooser_rows()
        check("'/cmd' lists the /cmd mode alone", rows == ["/cmd "], rows)

        FreeCADGui.Selection.clearPreselection()
        typed("/ Pillar.Height")
        check("'/ Pillar.Height' preselects the object", preselected() == "Pillar", preselected())
        typed("/")
        FreeCADGui.Selection.clearPreselection()
        typed("/Pillar.Height")
        check("'/Pillar.Height', no space, preselects it too", preselected() == "Pillar", preselected())

        typed("/ cmd")
        view, rows = chooser_rows()
        check("'/ cmd' is not answered by the chooser", rows is None, rows)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            for w in QtWidgets.QApplication.allWidgets():
                if w.objectName() == "OmniSearchBox":
                    w.hide()
            if doc is not None:
                FreeCAD.closeDocument(DOC)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
