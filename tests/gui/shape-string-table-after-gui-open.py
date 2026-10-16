"""A shape read in the GUI hashes with the document's string table.

docs/HandsOnQueue.md entry 162 (the reporter, 2026-10-16): "check what's
those invalid name length warning when opening the scanner document".
Every GUI open of scanner.FCStd printed 21 "<ElementMap> invalid name
length" warnings, none headless. The GUI hashes some strings into a new
document's table, the one the document shares with its file's history,
before the file is read (99 of them on the scanner). Document::Restore
then finds the file's table disagreeing with it and takes the file's --
but every shape is given its table by the index the file wrote
(HasherIndex 0), and index 0 still named the table set aside. So every
shape's element map was built against the wrong table, and its names read
back as other features' (Pad's first face, a face of a Thickness).

Claims, on a small file written by a headless FreeCADCmd -- two boxes and
a cut -- opened in the GUI:
  - the case is met: the open took the file's table, which disagrees with
    the one the GUI began (Document.cpp logs it; the test turns the log on);
  - each shape's string table is the document's;
  - each face of the cut walks back through its history to an object of
    the document, and every name on the way says where it comes from.

Runs with QT_QPA_PLATFORM=offscreen.
"""
import os
import re
import traceback

import FreeCAD
import FreeCADGui
import Part
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "TableAfterOpen"
PATH = os.path.join(OUT, DOC + ".FCStd")
# a name whose op codes take all of it before its tag: ';:G2;THK;:H7ff:8,E'
TAG = re.compile(r";:H(-?[0-9a-f]*)(?::([0-9a-f]+))?,([A-Z])$")


def note(msg):
    with open(RESULT, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def no_source(name):
    m = TAG.search(name)
    return bool(m and m.group(2) and int(m.group(2), 16) >= m.start())


MAKER = '''
import FreeCAD
doc = FreeCAD.newDocument("%s")
a = doc.addObject("Part::Box", "A")
b = doc.addObject("Part::Box", "B")
b.Placement.Base = FreeCAD.Vector(5, 5, 5)
cut = doc.addObject("Part::Cut", "Cut")
cut.Base = a
cut.Tool = b
doc.recompute()
doc.saveAs(r"%s")
FreeCAD.closeDocument(doc.Name)
'''


def make():
    # Written by another process, headless, as a file from elsewhere is:
    # its string table does not hold what this GUI hashes into a new
    # document first, and the two disagree. One written by this session
    # agrees with it, and the restore never takes another table.
    import subprocess
    script = os.path.join(OUT, "make_table_file.py")
    with open(script, "w", encoding="utf-8") as f:
        f.write(MAKER % (DOC, PATH))
    exe = os.path.join(FreeCAD.getHomePath(), "bin", "FreeCADCmd.exe")
    if not os.path.exists(exe):
        exe = os.path.join(FreeCAD.getHomePath(), "bin", "FreeCADCmd")
    proc = subprocess.run([exe, script], capture_output=True, text=True, timeout=180)
    if not os.path.exists(PATH):
        note("ABORT the file was not written: %s %s" % (proc.returncode, proc.stderr[-500:]))
        finish()
        return
    QtCore.QTimer.singleShot(1000, reopen)


def reopen():
    try:
        # the case is the restore taking the file's table: Document.cpp says
        # so in the log, which is made to show it
        FreeCAD.setLogLevel("App", "Log")
        doc = FreeCAD.openDocument(PATH)
        FreeCAD.setLogLevel("App", "Warning")
        QtCore.QTimer.singleShot(3000, lambda: claims(doc.Name))
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
        finish()


def claims(name):
    try:
        doc = FreeCAD.getDocument(name)
        log = os.path.join(OUT, "FreeCAD.log")
        text = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
        check("the case: the open took the file's string table, which disagrees with "
              "the one the GUI began", "string table disagrees with the file's" in text,
              "FreeCAD.log %s" % ("read" if text else "missing"))
        # a shape with no element map has no table: left out, but the cut's
        # has one, so the claim is never empty
        tables = [(o.Name, o.Shape.Hasher) for o in doc.Objects
                  if hasattr(o, "Shape") and o.Shape.Hasher is not None]
        apart = [n for n, h in tables if not doc.Hasher.isSame(h)]
        check("each shape's string table is the document's",
              "Cut" in [n for n, _ in tables] and not apart,
              "with a table %s, apart %s" % ([n for n, _ in tables], apart))
        cut = doc.getObject("Cut")
        byid = {o.ID: o for o in doc.Objects}
        bad = []
        for i in range(1, len(cut.Shape.Faces) + 1):
            mapped = cut.Shape.getElementMappedName("Face%d" % i)
            mapped = mapped[0] if isinstance(mapped, (tuple, list)) else mapped
            shape, step = cut.Shape, mapped
            for _ in range(8):
                h = shape.getElementHistory(step)
                if not h or not h[0]:
                    break
                tag, original = h[0], h[1]
                src = byid.get(abs(tag))
                if src is None or no_source(original):
                    bad.append(("Face%d" % i, mapped, hex(tag), original))
                    break
                shape, step = Part.getShape(src), original
        check("each face of the cut walks back to the document's objects", not bad, bad[:4])
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        finish()


def finish():
    for n in list(FreeCAD.listDocuments()):
        FreeCAD.closeDocument(n)
    note("DONE")
    QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(3000, make)
