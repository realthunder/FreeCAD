"""The Sketcher's General preference page and the tool bars it decides.

Three options on the page decide which commands the Sketcher's tool bars
carry: one coincident tool or two, one horizontal/vertical tool or two, the
polyline and line commands in a group or apart. Workbench.cpp reads them
when it builds the bars.

Claims:

  - the page shows the coincident option as the workbench reads it. Its
    check box was unchecked by default while the workbench's default is the
    unified tool, so opening the preferences and pressing OK, with nothing
    touched, wrote "not unified";
  - the polyline and line group has a check box (upstream 8ae1d9bbde,
    255949134f; the workbench read the option, nothing could set it);
  - a change of any of the three reaches the tool bars when the page is
    saved, with no restart (upstream asks for one, 4f429e3288; here the
    workbench installs its bars again, as it did for the dimensioning mode
    alone);
  - "Reset page" takes back what the page keeps outside its own widgets too:
    the dimensioning tools, the radius/diameter mode, the scaling mode, the
    on-view parameters (upstream 09209436d2);
  - the Grid page's line pattern entries are painted in the theme's text
    colour (they were black, whatever the theme) and are upstream's seven
    (00228821d0, b4de78d3d7, ab9188a5dc, a00fe1e886).
  - the Display page can turn off the helper lines a line tool draws for a
    parallel or perpendicular direction (upstream bfe1295b6b): the tools
    read the option, and nothing could set it.

The preferences dialog is modal: every step into it is a timer that fires
inside its event loop, does its part and presses OK.

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
SKETCHER = "User parameter:BaseApp/Preferences/Mod/Sketcher"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.3):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtWidgets.QApplication.processEvents()
        time.sleep(0.01)


def bar(name):
    """The commands on one of the main window's tool bars, in order."""
    for tb in FreeCADGui.getMainWindow().findChildren(QtWidgets.QToolBar):
        if tb.objectName() == name:
            return [a.objectName() for a in tb.actions() if a.objectName()]
    return None


def clear():
    for group, names in (("Constraints", ("UnifiedCoincident", "AutoHorVer")),
                         ("Commands", ("UnifiedLineCommands",)),
                         ("dimensioning", ("SingleDimensioningTool",
                                           "SeparatedDimensioningTools",
                                           "DimensioningDiameter", "DimensioningRadius"))):
        grp = FreeCAD.ParamGet(SKETCHER + "/" + group)
        for name in names:
            grp.RemBool(name)
    FreeCAD.ParamGet(SKETCHER + "/dimensioning").RemInt("AutoScaleMode")
    FreeCAD.ParamGet(SKETCHER + "/Tools").RemInt("OnViewParameterVisibility")


def in_preferences(action, page=0):
    """Open one of the Sketcher's pages (0 General, 1 Grid, 2 Display, 3 Appearance), run
    action(dialog) inside the dialog's event loop and press OK. Returns
    what action returned."""
    got = {}

    def act():
        dialog = QtWidgets.QApplication.activeModalWidget()
        try:
            if dialog is None:
                got["error"] = "no modal dialog"
                return
            got["value"] = action(dialog)
        except Exception:
            got["error"] = traceback.format_exc()
        finally:
            if dialog is not None:
                box = dialog.findChild(QtWidgets.QDialogButtonBox)
                box.button(QtWidgets.QDialogButtonBox.Ok).click()

    QtCore.QTimer.singleShot(800, act)
    FreeCADGui.showPreferences("Sketcher", page)
    settle(0.5)
    if "error" in got:
        note("in the dialog: " + got["error"])
    return got.get("value")


def widget(dialog, name):
    found = dialog.findChild(QtWidgets.QWidget, name)
    if found is None:
        raise RuntimeError("no widget named " + name)
    return found


def reset_page(dialog):
    """Press the dialog's reset button and take the menu's first entry,
    "Reset page"."""
    button = None
    for b in dialog.findChildren(QtWidgets.QAbstractButton):
        if "Reset" in b.text():
            button = b
    if button is None:
        raise RuntimeError("no reset button")

    def pick():
        menu = QtWidgets.QApplication.activePopupWidget()
        if menu is None:
            note("no reset menu")
            return
        entry = menu.actions()[0]
        note("reset menu entry: " + entry.text())
        entry.trigger()
        menu.close()

    QtCore.QTimer.singleShot(400, pick)
    button.click()
    settle(0.3)


def run():
    try:
        clear()
        FreeCADGui.getMainWindow().showMaximized()
        FreeCADGui.activateWorkbench("SketcherWorkbench")
        settle(1.0)
        cons, geom = bar("Sketcher constraints"), bar("Sketcher geometries")
        start = (cons, geom)
        check("at the start: one coincident tool, the horizontal/vertical group, "
              "polyline and line in a group (upstream's defaults)",
              cons is not None and geom is not None
              and "Sketcher_ConstrainCoincidentUnified" in cons
              and "Sketcher_CompHorVer" in cons
              and "Sketcher_CompLine" in geom
              and "Sketcher_CreatePolyline" not in geom and "Sketcher_CreateLine" not in geom,
              (cons, geom))

        # -- nothing touched ---------------------------------------------
        shown = in_preferences(
            lambda d: widget(d, "checkBoxUnifiedCoincident").isChecked())
        check("the page shows the coincident tools unified, as the tool bar has them",
              shown is True, shown)
        constraints = FreeCAD.ParamGet(SKETCHER + "/Constraints")
        check("OK with nothing touched leaves them unified",
              constraints.GetBool("UnifiedCoincident", True) is True
              and "Sketcher_ConstrainCoincidentUnified" in bar("Sketcher constraints"),
              (constraints.GetBool("UnifiedCoincident", True), bar("Sketcher constraints")))

        # -- the three options -------------------------------------------
        def regroup(dialog):
            widget(dialog, "checkBoxLineGroup").setChecked(False)
            widget(dialog, "checkBoxHorVerAuto").setChecked(False)
            widget(dialog, "checkBoxUnifiedCoincident").setChecked(False)
            return True

        check("the page has the line group's check box", in_preferences(regroup) is True)
        cons, geom = bar("Sketcher constraints"), bar("Sketcher geometries")
        check("saved: the polyline and line commands are two buttons on the tool bar",
              geom is not None and "Sketcher_CreatePolyline" in geom
              and "Sketcher_CreateLine" in geom and "Sketcher_CompLine" not in geom, geom)
        check("saved: horizontal and vertical are two tools",
              cons is not None and "Sketcher_ConstrainHorizontal" in cons
              and "Sketcher_ConstrainVertical" in cons
              and "Sketcher_CompHorVer" not in cons, cons)
        check("saved: coincident and point-on-object are two tools",
              cons is not None and "Sketcher_ConstrainCoincident" in cons
              and "Sketcher_ConstrainPointOnObject" in cons
              and "Sketcher_ConstrainCoincidentUnified" not in cons, cons)

        def swapped(names, old, new):
            at = names.index(old[0])
            return names[:at] + new + names[at + len(old):]

        want_geom = swapped(start[1], ["Sketcher_CompLine"],
                            ["Sketcher_CreatePolyline", "Sketcher_CreateLine"])
        want_cons = swapped(swapped(start[0], ["Sketcher_ConstrainCoincidentUnified"],
                                    ["Sketcher_ConstrainCoincident",
                                     "Sketcher_ConstrainPointOnObject"]),
                            ["Sketcher_CompHorVer"],
                            ["Sketcher_ConstrainHorizontal", "Sketcher_ConstrainVertical"])
        check("each in the place of what it replaces, not at the end of its bar",
              geom == want_geom and cons == want_cons, (cons, geom))

        # -- reset page ----------------------------------------------------
        def other_settings(dialog):
            widget(dialog, "dimensioningMode").setCurrentIndex(1)
            widget(dialog, "radiusDiameterMode").setCurrentIndex(2)
            widget(dialog, "autoScaleMode").setCurrentIndex(1)
            widget(dialog, "ovpVisibility").setCurrentIndex(2)
            return True

        in_preferences(other_settings)
        dim = FreeCAD.ParamGet(SKETCHER + "/dimensioning")
        tools = FreeCAD.ParamGet(SKETCHER + "/Tools")

        def stored():
            # with the defaults the page and the workbench read them with
            return {"single": dim.GetBool("SingleDimensioningTool", True),
                    "separated": dim.GetBool("SeparatedDimensioningTools", False),
                    "diameter": dim.GetBool("DimensioningDiameter", True),
                    "radius": dim.GetBool("DimensioningRadius", True),
                    "scale": dim.GetInt("AutoScaleMode", 2),
                    "onview": tools.GetInt("OnViewParameterVisibility", 1),
                    "lines": FreeCAD.ParamGet(SKETCHER + "/Commands").GetBool(
                        "UnifiedLineCommands", True)}

        defaults = {"single": True, "separated": False, "diameter": True, "radius": True,
                    "scale": 2, "onview": 1, "lines": True}
        before = stored()
        check("the dimensioning, scaling and on-view settings are stored, none at its default",
              all(before[k] != defaults[k] for k in ("single", "separated", "diameter",
                                                     "scale", "onview", "lines")), before)
        # The OK that follows saves the page the dialog made anew, so the
        # values are there again afterwards -- as the defaults.
        in_preferences(reset_page)
        after = stored()
        check("Reset page puts every one of them back to its default", after == defaults,
              after)
        cons, geom = bar("Sketcher constraints"), bar("Sketcher geometries")
        check("and the tool bars are as at the start, button for button",
              (cons, geom) == start, (cons, geom))

        # -- the grid page's line patterns ---------------------------------
        # Light text, as a dark theme has it: an icon painted black would
        # not be the text's colour.
        app = QtWidgets.QApplication.instance()
        palette = app.palette()
        light = QtGui.QPalette(palette)
        light.setColor(QtGui.QPalette.WindowText, QtGui.QColor(240, 240, 240))
        app.setPalette(light)
        settle(0.3)
        try:
            patterns = in_preferences(pattern_icons, page=1)
        finally:
            app.setPalette(palette)
        note("grid patterns: %s" % (patterns,))
        check("the grid's line pattern has upstream's seven entries, the old three among them",
              patterns is not None and len(patterns["entries"]) == 7
              and {0xffff, 0x0f0f, 0xaaaa} <= {e[0] for e in patterns["entries"]}, patterns)
        check("every entry has an icon, painted in the page's text colour",
              patterns is not None and patterns["text"] == (240, 240, 240)
              and all(e[1] > 0 and e[2] == [patterns["text"]] for e in patterns["entries"]),
              patterns)

        # -- the appearance page: a line type and a width per kind ---------
        view = FreeCAD.ParamGet(SKETCHER + "/View")
        for name in ("ConstructionPattern", "ConstructionWidth"):
            view.RemInt(name)
        shown = in_preferences(appearance, page=3)
        note("appearance page: %s" % (shown,))
        check("the appearance page has a line type and a width for each of the eight kinds",
              shown is not None and shown["boxes"] == [7] * 8 and shown["widths"] == 8, shown)
        check("with upstream's defaults shown",
              shown is not None and shown["construction"] == (0xFCFC, 2), shown)
        check("and no vertex colour: a point is coloured as its geometry",
              shown is not None and shown["vertex"] is False, shown)
        stored = (view.GetInt("ConstructionPattern", 0), view.GetInt("ConstructionWidth", 0))
        check("OK stores what was chosen", stored == (0xAAAA, 4), stored)
        in_preferences(reset_page, page=3)
        stored = (view.GetInt("ConstructionPattern", 0xFCFC), view.GetInt("ConstructionWidth", 2))
        check("Reset page takes the line types back too, which the page stores by hand",
              stored == (0xFCFC, 2), stored)
        for name in ("ConstructionPattern", "ConstructionWidth"):
            view.RemInt(name)

        # -- the display page: the directional helper lines ----------------
        general = FreeCAD.ParamGet(SKETCHER + "/General")
        general.RemBool("ShowDirectionalAutoConstraintHints")

        def helpers(dialog):
            box = widget(dialog, "checkBoxShowDirectionalAutoConstraintHints")
            was = box.isChecked()
            box.setChecked(False)
            return was

        was = in_preferences(helpers, page=2)
        check("the display page has a check box for the directional helper lines, "
              "ticked by default", was is True, was)
        check("unticking it and OK stores it off, where the drawing tools read it",
              general.GetBool("ShowDirectionalAutoConstraintHints", True) is False)
        general.RemBool("ShowDirectionalAutoConstraintHints")
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    finish()


def appearance(dialog):
    """Read the appearance page, then choose a construction line type and
    width for the OK to store."""
    kinds = ("Edge", "Construction", "Internal", "External", "ExternalDefining",
             "Information", "DimensionalConstraintLine", "AxisLine")
    boxes = [widget(dialog, k + "Pattern").count() for k in kinds]
    widths = sum(1 for k in kinds if widget(dialog, k + "Width") is not None)
    combo, spin = widget(dialog, "ConstructionPattern"), widget(dialog, "ConstructionWidth")
    shown = (combo.itemData(combo.currentIndex()), spin.value())
    combo.setCurrentIndex(combo.findData(0xAAAA))
    spin.setValue(4)
    return {"boxes": boxes, "widths": widths, "construction": shown,
            "vertex": dialog.findChild(QtWidgets.QWidget, "EditedVertexColor") is not None}


def pattern_icons(dialog):
    """The grid page's minor line pattern entries: (pattern, opaque pixels
    of its icon, their colours), and the page's text colour."""
    combo = widget(dialog, "gridLinePattern")
    page = combo
    while page is not None and "SketcherSettingsGrid" not in page.metaObject().className():
        page = page.parentWidget()
    text = (page or combo).palette().color(QtGui.QPalette.WindowText)
    entries = []
    for i in range(combo.count()):
        image = combo.itemIcon(i).pixmap(combo.iconSize()).toImage()
        colours = set()
        count = 0
        for y in range(image.height()):
            for x in range(image.width()):
                c = QtGui.QColor.fromRgba(image.pixel(x, y))
                if c.alpha() == 255:
                    count += 1
                    colours.add((c.red(), c.green(), c.blue()))
        entries.append((combo.itemData(i), count, sorted(colours)))
    return {"text": (text.red(), text.green(), text.blue()), "entries": entries}


def finish():
    if state["done"]:
        return
    state["done"] = True
    try:
        clear()
    except Exception:
        pass
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
