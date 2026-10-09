"""The Display style menu: the style as a combo box, the anti-aliasing, and
the lights of the view.

docs/HandsOnQueue.md entry 63: "make the existing default display style
options into a combobox with corresonding icons shown in the menu. also put
antialising and its combobox in that menu. also move the preference page for
light sources configuration there, but not the light manipulator. add a
button (only enabled if active view is 3d) to activate light direction
manipulate in the active 3d view. also besides the button add a checkbox to
sync all 3d view's light direction. remove the light source configuration
page from preference window." And as answered: the style is the active
view's; the anti-aliasing and the lights apply at once; the manipulation is
a toggle, by the button or Escape; the check box is a remembered setting and
decides for all the light settings; "the chage is store in the active view
properties. the button is for save it into the settings".

The menu is brought to the state it has when shown by its own aboutToShow,
which is what builds its sections; nothing is popped up.

Claims, with two 3D views of one document open:
  - the menu has the style combo, an entry a style with its icon, and none
    of the styles' own rows is shown any more;
  - the combo shows the active view's style, and picking another sets it on
    the active view and leaves the other view's alone;
  - the anti-aliasing combo shows the setting, and picking another stores it;
  - the lights section shows what lights the view: the headlight on, the
    fill light off, as the preferences have them;
  - the fill light switched on is a property of the ACTIVE view
    (Light_EnableFillLight) and of that view alone; the preference is
    untouched;
  - "All views" ticked is stored (View/SyncLightSettings), and a headlight
    intensity set then is on both views;
  - "Save as default" writes the active view's lights into the preferences;
  - "Direction" lets the pointer turn the active view's headlight (the
    button is down the next time the menu comes up): a press in the middle
    of the view is the light from the eye, a drag up and to the right the
    light from there, the camera is not moved, the other view's light is
    not; Escape in the view ends it, and a click then turns nothing;
  - the preferences have no Light Sources page.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from PySide6.QtTest import QTest

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
DOC = "DisplayStyleMenu"
VIEW = "User parameter:BaseApp/Preferences/View"
STEPS = []
S = {}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")
        f.flush()


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name + (" | " + str(detail) if detail else ""))
    return cond


def settle(seconds=0.4):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.005)


def views():
    return FreeCADGui.getDocument(DOC).mdiViewsOfType("Gui::View3DInventor")


def prop(view, name, default=None):
    return getattr(view, name) if name in view.PropertiesList else default


def menu():
    """A Display style menu, brought to the state it is shown in. The last
    one there is: the tool bars are rebuilt now and then, each time with a
    new menu, and one that was left behind is no longer kept up."""
    found = None
    for m in FreeCADGui.getMainWindow().findChildren(QtWidgets.QMenu):
        texts = [a.text().replace("&", "") for a in m.actions()]
        if m.findChild(QtWidgets.QComboBox, "DrawStyleCombo") is not None or (
                "Wireframe" in texts and "Flat Lines" in texts):
            found = m
    if found is None:
        raise RuntimeError("no Display style menu in the main window")
    found.aboutToShow.emit()
    settle(0.2)
    return found


def child(m, kind, name):
    w = m.findChild(kind, name)
    if w is None:
        raise RuntimeError("the menu has no %s" % name)
    return w


def start():
    doc = FreeCAD.newDocument(DOC)
    doc.addObject("Part::Box", "Box")
    doc.recompute()
    FreeCADGui.runCommand("Std_ViewCreate")
    settle(1.5)
    if len(views()) != 2:
        raise RuntimeError("%d views, wanted 2" % len(views()))
    for v in views():
        v.viewIsometric()
        v.fitAll()
    settle(1.0)
    S["active"] = FreeCADGui.ActiveDocument.ActiveView
    S["other"] = [v for v in views() if v != S["active"]][0]
    S["aliasing"] = FreeCAD.ParamGet(VIEW).GetInt("AntiAliasing", 3)


def style():
    m = menu()
    combo = child(m, QtWidgets.QComboBox, "DrawStyleCombo")
    names = [combo.itemText(i) for i in range(combo.count())]
    icons = sum(1 for i in range(combo.count()) if not combo.itemIcon(i).isNull())
    check("the menu has the style combo, an entry a style, each with its icon",
          combo.count() >= 8 and icons == combo.count() and "Wireframe" in names,
          "%d entries, %d with an icon: %s" % (combo.count(), icons, names))
    rows = [a.text() for a in m.actions()
            if a.isVisible() and a.text().replace("&", "") in names]
    check("none of the styles' own rows is shown", not rows, rows)
    active, other = S["active"], S["other"]
    before = str(other.DrawStyle)
    check("the combo shows the active view's style",
          combo.currentText().replace(" ", "") == str(active.DrawStyle).replace(" ", ""),
          "combo %r, the view %r" % (combo.currentText(), str(active.DrawStyle)))
    combo.setCurrentIndex(names.index("Wireframe"))
    combo.activated.emit(combo.currentIndex())
    settle()
    check("picking Wireframe sets it on the active view",
          str(active.DrawStyle) == "Wireframe", str(active.DrawStyle))
    check("and leaves the other view's style alone", str(other.DrawStyle) == before,
          "%r, was %r" % (str(other.DrawStyle), before))
    m2 = menu()
    check("the combo shows it the next time the menu comes up",
          child(m2, QtWidgets.QComboBox, "DrawStyleCombo").currentText() == "Wireframe")
    combo.setCurrentIndex(names.index("Flat Lines"))
    combo.activated.emit(combo.currentIndex())
    settle()


def lights():
    m = menu()
    active, other = S["active"], S["other"]
    head = child(m, QtWidgets.QCheckBox, "Light_EnableHeadlight")
    fill = child(m, QtWidgets.QCheckBox, "Light_EnableFillLight")
    intensity = child(m, QtWidgets.QSlider, "Light_HeadlightIntensity")
    sync = child(m, QtWidgets.QCheckBox, "LightAllViews")
    prefs = FreeCAD.ParamGet(VIEW)
    check("the lights section shows what lights the view",
          head.isChecked() and not fill.isChecked() and intensity.value() == 100
          and not sync.isChecked(),
          "headlight %s, fill light %s, intensity %d, all views %s" % (
              head.isChecked(), fill.isChecked(), intensity.value(), sync.isChecked()))
    fill.setChecked(True)
    settle()
    check("the fill light switched on is a property of the active view",
          prop(active, "Light_EnableFillLight") is True, prop(active, "Light_EnableFillLight"))
    check("and of that view alone", prop(other, "Light_EnableFillLight") is None,
          prop(other, "Light_EnableFillLight"))
    check("the preference is untouched", "EnableFillLight" not in prefs.GetBools(),
          prefs.GetBools())

    sync.setChecked(True)
    settle()
    check("\"All views\" ticked is stored", prefs.GetBool("SyncLightSettings", False) is True)
    intensity.setValue(70)
    settle()
    got = [prop(v, "Light_HeadlightIntensity") for v in (active, other)]
    check("a headlight intensity set then is on both views",
          all(g is not None and abs(g - 0.7) < 1e-6 for g in got), got)
    sync.setChecked(False)
    settle()

    child(m, QtWidgets.QPushButton, "LightSaveButton").click()
    settle()
    check("\"Save as default\" writes the active view's lights into the preferences",
          prefs.GetBool("EnableFillLight", False) is True
          and prefs.GetInt("HeadlightIntensity", -1) == 70,
          "EnableFillLight %s, HeadlightIntensity %d" % (
              prefs.GetBool("EnableFillLight", False), prefs.GetInt("HeadlightIntensity", -1)))


def direction_on():
    m = menu()
    button = child(m, QtWidgets.QPushButton, "LightDirectionButton")
    check("\"Direction\" is there to press, and up", button.isEnabled() and not button.isChecked())
    button.click()
    settle(1.0)
    check("pressed, it is down the next time the menu comes up",
          child(menu(), QtWidgets.QPushButton, "LightDirectionButton").isChecked())
    FreeCADGui.updateGui()
    settle(0.5)


def drag_light():
    """A drag with the left button, from the middle of the view up and to
    the right: the light stands where the pointer is, so it ends shining
    from the upper right -- down and to the left, and into the screen"""
    gl = S["active"].graphicsView().viewport()
    centre = QtCore.QPoint(gl.width() // 2, gl.height() // 2)
    camera = S["active"].getCameraOrientation()
    QTest.mousePress(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier, centre)
    settle(0.2)
    first = prop(S["active"], "Light_HeadlightDirection")
    check("a press in the middle of the view is the light from the eye",
          first is not None and abs(first.x) < 0.05 and abs(first.y) < 0.05 and first.z < -0.99,
          first)
    for i in range(1, 9):
        QTest.mouseMove(gl, centre + QtCore.QPoint(10 * i, -6 * i))
        settle(0.03)
    QTest.mouseRelease(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier,
                       centre + QtCore.QPoint(80, -48))
    settle(0.3)
    got = prop(S["active"], "Light_HeadlightDirection")
    check("dragged up and to the right, the light shines from there",
          got is not None and got.x < -0.1 and got.y < -0.05 and got.z < 0, got)
    check("the drag turned the light and not the camera",
          S["active"].getCameraOrientation().isSame(camera, 1e-6))
    check("and not the other view's light (\"All views\" is off)",
          prop(S["other"], "Light_HeadlightDirection") is None)


def direction_off():
    view = S["active"].graphicsView()
    view.setFocus()
    QTest.keyClick(view, QtCore.Qt.Key_Escape)
    settle(0.5)
    check("Escape in the view ends the turning",
          not child(menu(), QtWidgets.QPushButton, "LightDirectionButton").isChecked())
    before = prop(S["active"], "Light_HeadlightDirection")
    gl = view.viewport()
    centre = QtCore.QPoint(gl.width() // 2, gl.height() // 2)
    QTest.mousePress(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier, centre + QtCore.QPoint(-60, 40))
    QTest.mouseRelease(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier,
                       centre + QtCore.QPoint(-60, 40))
    settle(0.3)
    after = prop(S["active"], "Light_HeadlightDirection")
    check("and a click in the view no longer turns the light",
          before is not None and after is not None and (before - after).Length < 1e-9,
          "%s, then %s" % (before, after))


def aliasing():
    m = menu()
    combo = child(m, QtWidgets.QComboBox, "AntiAliasingCombo")
    check("the anti-aliasing combo shows the setting",
          combo.count() == 5 and combo.currentIndex() == S["aliasing"],
          "%d entries, index %d, the setting %d" % (
              combo.count(), combo.currentIndex(), S["aliasing"]))
    pick = 0 if S["aliasing"] != 0 else 3
    combo.setCurrentIndex(pick)
    combo.activated.emit(pick)
    settle(3.0)
    check("picking another stores it",
          FreeCAD.ParamGet(VIEW).GetInt("AntiAliasing", -1) == pick,
          FreeCAD.ParamGet(VIEW).GetInt("AntiAliasing", -1))


def no_page():
    FreeCADGui.runCommand("Std_DlgPreferences")
    settle(1.0)
    dlg = None
    for w in QtWidgets.QApplication.topLevelWidgets():
        if w.metaObject().className() == "Gui::Dialog::DlgPreferencesImp" and w.isVisible():
            dlg = w
    if dlg is None:
        raise RuntimeError("no preferences dialog")
    titles = [w.windowTitle() for w in dlg.findChildren(QtWidgets.QWidget)
              if w.inherits("Gui::Dialog::PreferencePage")]
    check("the preferences have no Light Sources page",
          len(titles) > 20 and not [t for t in titles if "ight" in t and "ource" in t],
          "%d pages; with 'light' in the title: %s" % (
              len(titles), [t for t in titles if "ight" in t]))
    dlg.close()
    settle(0.5)


def finish():
    for d in list(FreeCAD.listDocuments().values()):
        FreeCAD.closeDocument(d.Name)
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
            note("FAIL the step %s ran | %s" % (
                fn.__name__, traceback.format_exc().replace("\n", " | ")))
        advance()

    QtCore.QTimer.singleShot(int(delay), run)


STEPS.append((2500, start))
STEPS.append((500, style))
STEPS.append((500, lights))
STEPS.append((500, direction_on))
STEPS.append((500, drag_light))
STEPS.append((500, direction_off))
STEPS.append((500, no_page))
STEPS.append((500, aliasing))
advance()
