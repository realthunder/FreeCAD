"""A served scene whose object identity and label are both non-ASCII.

What a published object entry carries is its document and internal name
— identity, fixed, read off the render cache for nothing — while the
label a viewer shows a human comes from the metadata feed
(Gui/ObjectMetaFeed.h) that only a rename moves. This scene exercises
both halves with names that are *not* ASCII, which internal names in
this fork are allowed to be: anything a Python identifier may hold.

Renames the object a few seconds in, which is the event the label feed
exists for: it moves no geometry, so the publish it causes and the
`object meta:` line it logs are the whole evidence that a label reaches
a viewer without the mesh path resolving anything.

Usage:  scripts/renderer-serve.sh scripts/demo-names.py 8082
"""
import os
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

DOC = os.environ.get("FC_DOC", "文書")
OBJ = os.environ.get("FC_OBJ", "箱_日本")
LABEL = os.environ.get("FC_LABEL", "Ma « boîte » é")
OUT = os.environ.get("FC_NAMES_RESULT", "/tmp/fc-names-result.txt")
GROW = int(os.environ.get("FC_GROW", "200"))


def report(line):
    FreeCAD.Console.PrintMessage("DEMO-NAMES %s\n" % line)
    with open(OUT, "a") as f:
        f.write(line + "\n")


def build():
    # The label feed reports itself at Log level, and that report is the
    # evidence this scene exists to produce.
    FreeCAD.setLogLevel("Gui", "Log")
    # The renderer path has to be selected before the document's view is
    # made, or the view comes up on plain GL and publishes nothing.
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View/Render").SetString(
        "Type", "bgfx - OpenGL")

    doc = FreeCAD.newDocument(DOC)
    box = doc.addObject("Part::Box", OBJ)
    box.Length = box.Width = box.Height = 10
    box.Label = LABEL
    doc.recompute()
    FreeCADGui.ActiveDocument = FreeCADGui.getDocument(doc.Name)
    view = FreeCADGui.ActiveDocument.ActiveView
    view.viewAxonometric()
    view.fitAll()
    # The names actually assigned: a sanitizing App would show up here
    # rather than in a confusing failure downstream.
    report("doc=%s obj=%s label=%s" % (doc.Name, box.Name, box.Label))
    return view


def rename(view):
    """A rename is the one event that must move a label with no geometry
    behind it: nothing else in the source would even ask for a publish."""
    try:
        doc = FreeCAD.getDocument(DOC)
        obj = doc.getObject(OBJ)
        obj.Label = LABEL + " (renommé)"
        report("renamed to %s" % obj.Label)
    except Exception:
        report("rename failed\n%s" % traceback.format_exc())
    QtCore.QTimer.singleShot(3000, lambda: grow(view))


def grow(view):
    """Objects appearing one at a time, with frames in between — a live
    import in miniature.

    This is the case a whole-table push gets quadratically wrong: every
    frame would re-send every object announced so far. What the log must
    show instead is small deltas.
    """
    try:
        doc = FreeCAD.getDocument(DOC)
        for i in range(GROW):
            box = doc.addObject("Part::Box", "%s_%d" % (OBJ, i))
            box.Length = box.Width = box.Height = 2
            box.Placement.Base = FreeCAD.Vector(i * 3, 0, 0)
            if i % 10 == 0:
                doc.recompute()
                QtCore.QCoreApplication.processEvents()
        doc.recompute()
        QtCore.QCoreApplication.processEvents()
        report("grew by %d objects (%d total)" % (GROW, len(doc.Objects)))
    except Exception:
        report("grow failed\n%s" % traceback.format_exc())


try:
    open(OUT, "w").close()
    _view = build()
    # Publish first, then rename: the log line the feed writes says when
    # the label table was actually rebuilt.
    QtCore.QTimer.singleShot(8000, lambda: rename(_view))
except Exception:
    report("build failed\n%s" % traceback.format_exc())
