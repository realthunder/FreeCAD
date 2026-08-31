"""Demo scene for the remote spreadsheet tier (docs/SpreadsheetRemote.md).

A document holding one solid and one Spreadsheet that exercises what the
sheet.get serializer has to carry: plain text, a number, a quantity, a
formula between cells, a formula that reaches OUT of the sheet into the
model (the case a browser cannot evaluate on its own), an alias, styling,
and a deliberately broken cell so the error path is covered too.

Run via scripts/renderer-serve.sh scripts/demo-sheet.py <port>, then drive
it with scripts/control-client.py --port <port> sheet.get obj=Spreadsheet.
Runs persistently so it can be streamed.
"""
import os

import FreeCAD
import FreeCADGui

_out = os.environ.get("SMOKE_RESULT")


def note(msg):
    if _out:
        with open(_out, "a") as f:
            f.write(str(msg) + "\n")


try:
    view = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/View")
    view.SetInt("RenderCache", 3)                 # renderer (bgfx) path

    doc = FreeCAD.newDocument("SheetDemo")
    box = doc.addObject("Part::Box", "Box")
    box.Length = 40
    box.Width = 25
    box.Height = 10

    sheet = doc.addObject("Spreadsheet::Sheet", "Spreadsheet")
    sheet.set("A1", "Part")
    sheet.set("B1", "Value")
    sheet.setStyle("A1:B1", "bold")

    sheet.set("A2", "Length")
    sheet.set("B2", "=Box.Length")                # reaches into the model
    sheet.set("A3", "Width")
    sheet.set("B3", "=Box.Width")
    sheet.set("A4", "Footprint")
    sheet.set("B4", "=B2 * B3")                   # cell-to-cell, pure data
    sheet.setAlias("B4", "footprint")

    sheet.set("A6", "Count")
    sheet.set("B6", "12")
    sheet.set("A7", "Each")
    sheet.set("B7", "2.5kg")                      # a quantity
    sheet.set("A8", "Total")
    sheet.set("B8", "=B6 * B7")
    sheet.setForeground("B8", (0.0, 0.4, 0.0, 1.0))
    sheet.setAlignment("B8", "right")

    sheet.set("A10", "Broken")
    sheet.set("B10", "=NoSuchObject.Nope")        # the error path

    sheet.setColumnWidth("A", 120)
    doc.recompute()

    FreeCADGui.ActiveDocument.ActiveView.viewIsometric()
    FreeCADGui.SendMsgToActiveView("ViewFit")
    note("sheet demo ready: %s cells" % len(sheet.getUsedCells()))
except Exception as exc:  # noqa: BLE001 - the harness wants the text
    import traceback
    note("demo-sheet failed: %s" % exc)
    note(traceback.format_exc())
