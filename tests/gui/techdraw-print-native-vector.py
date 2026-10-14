"""A TechDraw page printed to a native printer stays lines and text.

docs/HandsOnQueue.md entry 129: "make Techdraw printing output vector so that
it can scale losslessly"; and "used techdraw tool button print all pages, used
pdf printer. Every page is rasterized". Qt prints to a Windows printer through
GDI and rasterizes whatever is drawn with a pen or brush it counts as
transparent or advanced; a page drew such pens that draw nothing (QtSvg's
stroke="none", items' fully transparent pens), and the whole sheet became one
image.

A page with a view of a box, printed by TechDrawGui.printPages -- Print All's
path with no dialog -- to "Microsoft Print to PDF", its output a file.
Claims: the file has no image; it has text runs (the template's text came
through as text).

Windows only, with that printer installed; anywhere else it says so and
passes nothing. On the screen.
"""
import os
import re
import sys
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PRINTER = "Microsoft Print to PDF"


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


def run():
    try:
        from PySide6 import QtPrintSupport
        names = [p.printerName() for p in QtPrintSupport.QPrinterInfo.availablePrinters()]
        if sys.platform != "win32" or PRINTER not in names:
            note("NOT RUN: no %r here" % PRINTER)
            return
        import TechDrawGui
        FreeCADGui.activateWorkbench("TechDrawWorkbench")
        settle(800)
        doc = FreeCAD.newDocument("PrintNative")
        box = doc.addObject("Part::Box", "Box")
        page = doc.addObject("TechDraw::DrawPage", "Page")
        template = doc.addObject("TechDraw::DrawSVGTemplate", "Template")
        template.Template = os.path.join(FreeCAD.getResourceDir(), "Mod", "TechDraw",
                                         "Templates", "A4_LandscapeTD.svg")
        page.Template = template
        view = doc.addObject("TechDraw::DrawViewPart", "View")
        page.addView(view)
        view.Source = [box]
        doc.recompute()
        page.ViewObject.doubleClicked()
        settle(3000)
        spool = os.path.join(OUT, "page.spool")
        TechDrawGui.printPages([page], PRINTER, spool)
        for _ in range(80):
            settle(250)
            if os.path.exists(spool) and os.path.getsize(spool) > 0:
                break
        settle(1500)
        if not check("the printer wrote its file", os.path.exists(spool)):
            return
        data = open(spool, "rb").read()
        os.replace(spool, os.path.join(OUT, "page.pdf"))
        images = len(re.findall(rb"/Subtype\s*/Image", data))
        texts = data.count(b"Tj") + data.count(b"TJ")
        check("the printed page has no image: lines, not pixels", images == 0,
              "%d images, %d bytes" % (images, len(data)))
        check("and text runs: the template's text came through as text", texts > 1,
              "%d text runs" % texts)
    except Exception:
        note("ABORT " + traceback.format_exc().replace("\n", " | "))
    finally:
        for name in list(FreeCAD.listDocuments()):
            FreeCAD.closeDocument(name)
        note("DONE")
        QtCore.QTimer.singleShot(300, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(3000, run)
