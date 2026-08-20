"""Reproduce the Preferences-dialog combo box whose popup is not dismissed.

Reported: pick an entry in a combo box on a preference page and the drop-down
list stays on screen; it only goes away when the dialog loses focus. Tested by
the user on the General page's tree mode combo, but the handler behind that one
is trivial (three SetBool calls nothing observes), so the suspicion is that the
dialog does it to every combo.

Driving this from outside is hopeless here: the dialog is modal, and a
synthetic XTEST click never takes a popup's grab without a window manager (see
the menu probes of 2026-08-10). So the probe drives the popup the way Qt's own
QComboBoxPrivateContainer does -- show the popup, then post the press/release
pair the container's event filter answers with hidePopup() + itemSelected --
and then simply asks whether the popup window is still visible. No grab, no
window manager, no pointer needed.

The wait between showPopup() and the click is deliberate: the container ignores
a release that lands within a double-click interval of the popup opening (that
is what makes click-and-drag selection work), so a click posted immediately
proves nothing.

Legs:
  dialog   every QComboBox the Preferences dialog can reach
  control  a bare QComboBox in a plain modal QDialog, same procedure

A control that dismisses while the dialog's combos do not puts the cause in
this dialog rather than in Qt or the platform.

Usage: FreeCAD scripts/combo_popup_probe.py
Env:   COMBO_PROBE_OUT   result JSON (default /tmp/combo-popup.json)
       COMBO_PROBE_EXIT  "1" = exit when done
"""
import json
import os
import traceback

import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets

OUT = os.environ.get("COMBO_PROBE_OUT", "/tmp/combo-popup.json")
EXIT = os.environ.get("COMBO_PROBE_EXIT", "") == "1"
TRACE = OUT + ".trace"


def trace(message):
    """print() inside a macro lands in the Report view, not the terminal."""
    with open(TRACE, "a") as handle:
        handle.write(message + "\n")


trace("loaded")

# Longer than QApplication.doubleClickInterval(), which is what the container
# measures the release against.
SETTLE_MS = 800

results = {"legs": [], "error": None}


def click_other_item(combo):
    """Post what the popup's own event filter turns into a selection.

    A different row than the one showing, or the selection is a no-op and the
    index cannot testify that the path ran.
    """
    view = combo.view()
    row = 1 if combo.currentIndex() != 1 else 0
    index = combo.model().index(row, 0)
    view.setCurrentIndex(index)
    pos = QtCore.QPointF(view.visualRect(index).center())
    for kind, buttons in ((QtCore.QEvent.MouseButtonPress, QtCore.Qt.LeftButton),
                          (QtCore.QEvent.MouseButtonRelease, QtCore.Qt.NoButton)):
        event = QtGui.QMouseEvent(kind, pos, QtCore.Qt.LeftButton, buttons,
                                  QtCore.Qt.NoModifier)
        QtWidgets.QApplication.sendEvent(view.viewport(), event)


def popup_visible(combo):
    view = combo.view()
    if view is None:
        return False
    window = view.window()
    return bool(window and window.isVisible())


def run_case(name, combo, done):
    """showPopup, wait, click an item, report whether the popup survived it."""
    trace("case %s" % name)
    combo.showPopup()
    opened = popup_visible(combo)
    before = combo.currentIndex()

    def finish():
        try:
            click_other_item(combo)
            still_up = popup_visible(combo)
        except Exception:
            results["error"] = traceback.format_exc()
            still_up = None
        trace("  %s still_up=%s" % (name, still_up))
        results["legs"].append({
            "name": name,
            "opened": opened,
            "index_before": before,
            "index_after": combo.currentIndex(),
            "still_up": still_up,
        })
        if still_up:
            combo.hidePopup()
        done()

    QtCore.QTimer.singleShot(SETTLE_MS, finish)


def find_dialog():
    for widget in QtWidgets.QApplication.topLevelWidgets():
        if widget.isVisible() and widget.metaObject().className().endswith("DlgPreferencesImp"):
            return widget
    return None


def sweep_dialog(dialog, done):
    """Every reachable combo, one after another, on the page it lives on."""
    combos = [c for c in dialog.findChildren(QtWidgets.QComboBox)
              if c.count() > 1 and c.isEnabled()]
    # The named one the user tested comes first so a partial run still says
    # something about it.
    combos.sort(key=lambda c: (c.objectName() != "treeMode", c.objectName()))
    combos = combos[:6]

    def step(i):
        if i >= len(combos):
            done()
            return
        combo = combos[i]
        name = "dialog:%s" % (combo.objectName() or combo.metaObject().className())
        run_case(name, combo, lambda: step(i + 1))

    step(0)


def control(done):
    """The same procedure against a combo this dialog never touched."""
    dialog = QtWidgets.QDialog(FreeCADGui.getMainWindow())
    dialog.setWindowTitle("combo control")
    combo = QtWidgets.QComboBox(dialog)
    combo.addItems(["one", "two", "three"])
    dialog.resize(200, 80)
    dialog.setModal(True)
    dialog.show()

    def after():
        dialog.close()
        done()

    QtCore.QTimer.singleShot(200, lambda: run_case("control", combo, after))


def report():
    trace("report")
    with open(OUT, "w") as handle:
        json.dump(results, handle, indent=2)
    print("== combo popup probe ==")
    for leg in results["legs"]:
        print("%-34s opened=%s still_up=%s  index %s -> %s"
              % (leg["name"], leg["opened"], leg["still_up"],
                 leg["index_before"], leg["index_after"]))
    if results["error"]:
        print(results["error"])
    print("wrote", OUT)
    if EXIT:
        QtWidgets.QApplication.quit()


def start():
    trace("start")
    try:
        control(lambda: open_preferences())
    except Exception:
        results["error"] = traceback.format_exc()
        report()


def open_preferences():
    trace("open_preferences")

    def inside():
        trace("inside modal loop")
        dialog = find_dialog()
        if dialog is None:
            results["error"] = "preferences dialog not found"
            report()
            return
        sweep_dialog(dialog, lambda: (report(), dialog.reject()))

    QtCore.QTimer.singleShot(1500, inside)
    trace("showPreferences")
    # Blocks in a nested event loop; the timer above fires inside it.
    FreeCADGui.showPreferences("General", 0)


QtCore.QTimer.singleShot(3000, start)
