"""OK in the preferences with nothing changed re-applies nothing.

OK saves every page, so every key is written again; on a profile that
never confirmed the preferences most of them are stored for the first
time, and a key being stored is reported as a change. Three handlers took
that for a change of the thing itself: the application's stylesheet was
set again (Qt polishes every widget, the tree remakes every item's icon),
the active language was activated again (every widget retranslates), and
every Part view provider of every document was reloaded and re-meshed. On
a themed profile with a 686 object document open the first OK held the
program for 11 to 15 seconds (docs/HandsOnQueue.md entry 26).

Claims, on a profile with the Dark theme applied and a document with a
Part object open:

  - storing the icon set key with the value it has while unset, which is
    what a first OK does, sends no widget a style or a palette change (a
    stylesheet being set again shows as both);
  - storing the language key with the language that is active sends no
    language change;
  - the first OK ever, nothing changed in the dialog, sends none of the
    three, nor does a second OK;
  - a real change still arrives: another accent colour sets the stylesheet
    again, and switching the render cache off rebuilds the Part object.

The theme is applied in the session, where the reporter's was there from
the start. A theme switch leaves the palette moving after the stylesheet
was set, and the next apply is a real one for that reason; the test lets
it happen before it starts counting.

The reload of the Part view providers leaves nothing a script can count on
a simple object, so it is not claimed here; it is in the measurement of the
entry, and tests/gui/part-tessellation-reload.py holds the observer to the
changes it still has to act on.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtWidgets

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"


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


class Probe(QtWidgets.QLabel):
    """A widget of the main window that counts what every widget is sent."""

    def __init__(self, parent):
        super().__init__("probe", parent)
        self.appearance_changes = 0
        self.language_changes = 0

    def changeEvent(self, event):
        if event.type() in (QtCore.QEvent.StyleChange, QtCore.QEvent.PaletteChange):
            self.appearance_changes += 1
        elif event.type() == QtCore.QEvent.LanguageChange:
            self.language_changes += 1
        super().changeEvent(event)

    def take(self):
        seen = (self.appearance_changes, self.language_changes)
        self.appearance_changes = self.language_changes = 0
        return seen


def press_ok():
    """Open the preferences and press OK with nothing touched. Returns an error text or None."""
    state = {}

    def go():
        dialog = QtWidgets.QApplication.activeModalWidget()
        if dialog is None:
            state["error"] = "no preferences dialog"
            return
        if isinstance(dialog, QtWidgets.QMessageBox):
            state["error"] = "a question instead of the dialog: " + dialog.text()
            dialog.reject()
            return
        dialog.findChild(QtWidgets.QDialogButtonBox).button(QtWidgets.QDialogButtonBox.Ok).click()

    QtCore.QTimer.singleShot(2500, go)
    FreeCADGui.showPreferences()
    settle(2.5)
    return state.get("error")


def run():
    doc = None
    try:
        themes = FreeCADGui.listThemes()
        dark = [t for t in themes if t == "Dark"] or [t for t in themes if "dark" in t.lower()]
        if not check("there is a dark theme to apply", bool(dark), themes):
            return
        FreeCADGui.applyTheme(dark[0])
        settle(2.0)
        sheet = FreeCAD.ParamGet(PREFS + "MainWindow").GetString("StyleSheet", "")
        check("the theme brought a stylesheet", bool(sheet), sheet)

        import PartGui  # noqa: F401  the Part preference pages and the observer under test

        doc = FreeCAD.newDocument("OkReapplies")
        box = doc.addObject("Part::Box", "Box")
        doc.recompute()
        FreeCADGui.SendMsgToActiveView("ViewFit")
        settle(2.0)
        root = box.ViewObject.RootNode
        probe = Probe(FreeCADGui.getMainWindow())
        probe.show()
        settle(0.5)

        main = FreeCAD.ParamGet(PREFS + "MainWindow")
        general = FreeCAD.ParamGet(PREFS + "General")
        stored = [n for k, n, v in (main.GetContents() or []) if n == "IconSet"] + [
            n for k, n, v in (general.GetContents() or []) if n == "Language"]
        check("the profile stored neither the icon set nor the language", not stored, stored)
        # the apply that the theme switch leaves owing (see above), then as it was
        main.SetString("IconSet", "")
        settle(1.5)
        main.RemString("IconSet")
        settle(1.5)
        probe.take()

        main.SetString("IconSet", "")
        settle(1.5)
        looks, languages = probe.take()
        check("storing the icon set as it is sends no style or palette change", looks == 0, "%d" % looks)
        main.RemString("IconSet")
        general.SetString("Language", FreeCADGui.getLocale())
        settle(1.5)
        looks, languages = probe.take()
        check("storing the active language sends no language change", languages == 0,
              "%d, the language is %s" % (languages, FreeCADGui.getLocale()))
        general.RemString("Language")
        settle(1.5)
        probe.take()

        for which in ("the first OK", "a second OK"):
            error = press_ok()
            check("%s: the preferences opened and closed" % which, error is None, error)
            looks, languages = probe.take()
            check("%s sends no style or palette change" % which, looks == 0, "%d" % looks)
            check("%s sends no language change" % which, languages == 0, "%d" % languages)

        themes_grp = FreeCAD.ParamGet(PREFS + "Themes")
        accent = themes_grp.GetUnsigned("ThemeAccentColor1", 0)
        themes_grp.SetUnsigned("ThemeAccentColor1", 0xC81E1EFF if accent != 0xC81E1EFF else 0x1EC81EFF)
        settle(2.0)
        looks, languages = probe.take()
        check("another accent colour sets the stylesheet again", looks > 0, "%d style and palette changes" % looks)

        nodes = root.getNodeId()
        view = FreeCAD.ParamGet(PREFS + "View")
        cache = view.GetInt("RenderCache", 3)
        view.SetInt("RenderCache", 0 if cache != 0 else 3)
        settle(2.0)
        now = root.getNodeId()
        check("another render cache mode rebuilds the Part object", now != nodes,
              "node id %d, %d before" % (now, nodes))
        view.SetInt("RenderCache", cache)
        settle(1.5)
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        try:
            if doc is not None:
                FreeCAD.closeDocument(doc.Name)
        except Exception:
            pass
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
