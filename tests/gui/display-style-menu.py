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
properties. the button is for save it into the settings". Changed since:
"change the checkbox 'All view' to a button 'apply all' to apply to all
views. do the light handle with manipulator", and "use the below icon you
designed for display style toolbutton icon for now" (the split cube).

The menu is brought to the state it has when shown by its own aboutToShow,
which is what builds its sections; nothing is popped up.

Claims, with two 3D views of one document open:
  - the tool button's icon is the command's own, and stays when the style
    changes;
  - the menu has the style combo, an entry a style with its icon, and none
    of the styles' own rows is shown any more;
  - the combo shows the active view's style, and picking another sets it on
    the active view and leaves the other view's alone;
  - the anti-aliasing combo shows the setting, and picking another stores it;
  - the lights section shows what lights the view: the headlight on, the
    fill light off, as the preferences have them; it has no "All views"
    check box;
  - the fill light switched on and an intensity set are properties of the
    ACTIVE view (Light_EnableFillLight, ...) and of that view alone; the
    preference is untouched;
  - "Apply all" gives the other view the active view's lights, stores no
    setting, and a change after it is the active view's alone again;
  - "Save as default" writes the active view's lights into the preferences;
  - "Direction" raises a handle in the active view (the button is down the
    next time the menu comes up): Coin's light dragger, one, in what the
    view renders, and on the SCREEN, over the model in the middle of the
    view; a drag of its shaft turns the active view's headlight, the camera
    is not moved, nothing is selected (a press on the handle is not one on
    the model behind it), the other view's light is not turned; Escape in
    the view takes the handle down, and a click then turns nothing;
  - the preferences have no Light Sources page.
"""
import math
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


def button_image():
    """The icon the Display style tool button wears"""
    for button in FreeCADGui.getMainWindow().findChildren(QtWidgets.QToolButton):
        action = button.defaultAction()
        if action is not None and action.text().replace("&", "") == "Display style":
            return button.icon().pixmap(32, 32).toImage()
    return None


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
    button_icon = button_image()
    if button_icon is not None:
        button_icon.save(os.path.join(OUT, "button-icon.png"))
    styles = [combo.itemIcon(i).pixmap(32, 32).toImage() for i in range(combo.count())]
    check("the tool button's icon is the command's own, not one of the styles'",
          button_icon is not None and not button_icon.isNull()
          and not [i for i in styles if i == button_icon])
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
    check("the tool button's icon is the same after the style changed",
          button_image() == button_icon)
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
    apply_all = child(m, QtWidgets.QPushButton, "LightApplyAll")
    prefs = FreeCAD.ParamGet(VIEW)
    check("the lights section shows what lights the view",
          head.isChecked() and not fill.isChecked() and intensity.value() == 100,
          "headlight %s, fill light %s, intensity %d" % (
              head.isChecked(), fill.isChecked(), intensity.value()))
    check("there is no \"All views\" check box any more",
          m.findChild(QtWidgets.QCheckBox, "LightAllViews") is None)
    fill.setChecked(True)
    intensity.setValue(70)
    settle()
    check("the fill light switched on is a property of the active view",
          prop(active, "Light_EnableFillLight") is True, prop(active, "Light_EnableFillLight"))
    check("and of that view alone, as the headlight's intensity is",
          prop(other, "Light_EnableFillLight") is None
          and prop(other, "Light_HeadlightIntensity") is None,
          "%s, %s" % (prop(other, "Light_EnableFillLight"),
                      prop(other, "Light_HeadlightIntensity")))
    check("the preference is untouched", "EnableFillLight" not in prefs.GetBools(),
          prefs.GetBools())

    apply_all.click()
    settle()
    got = (prop(other, "Light_EnableFillLight"), prop(other, "Light_HeadlightIntensity"))
    check("\"Apply all\" gives the other view the active view's lights",
          got[0] is True and got[1] is not None and abs(got[1] - 0.7) < 1e-6, got)
    check("and stores no setting", "SyncLightSettings" not in prefs.GetBools())
    intensity.setValue(65)
    settle()
    got = (prop(active, "Light_HeadlightIntensity"), prop(other, "Light_HeadlightIntensity"))
    check("a change after it is the active view's alone again",
          abs(got[0] - 0.65) < 1e-6 and abs(got[1] - 0.7) < 1e-6, got)

    child(m, QtWidgets.QPushButton, "LightSaveButton").click()
    settle()
    check("\"Save as default\" writes the active view's lights into the preferences",
          prefs.GetBool("EnableFillLight", False) is True
          and prefs.GetInt("HeadlightIntensity", -1) == 65,
          "EnableFillLight %s, HeadlightIntensity %d" % (
              prefs.GetBool("EnableFillLight", False), prefs.GetInt("HeadlightIntensity", -1)))


def handles():
    """How many light handles are in what the active view renders: the
    annotation the viewer hangs up, with Coin's light dragger under it"""
    from pivy import coin
    root = S["active"].getViewer().getSoRenderManager().getSceneGraph()
    search = coin.SoSearchAction()
    search.setName(coin.SbName("LightManipulator"))
    search.setInterest(coin.SoSearchAction.ALL)
    search.apply(root)
    found = 0
    paths = search.getPaths()
    for i in range(paths.getLength()):
        node = paths[i].getTail()
        kinds = [node.getChild(k).getTypeId().getName().getString()
                 for k in range(node.getNumChildren())]
        S["handle kinds"] = kinds
        if [k for k in kinds if k.endswith("FCDirectionalLightDragger")]:
            found += 1
    return found


def on_screen(tag):
    """The active view as the backend drew it, overlays and all: the handle
    is fed to it as one. Not the screen, which may be locked, and not a
    grab of the widget, which shows nothing of a backend's picture."""
    path = os.path.join(OUT, tag + ".png")
    S["active"].saveRenderDump(path, metadata=False)
    return QtGui.QImage(path)


def changed(a, b):
    """The pixels that differ between two grabs of the view, in the view's
    own coordinates"""
    sx = a.width() / float(S["active"].graphicsView().viewport().width())
    out = []
    for y in range(0, a.height(), 2):
        for x in range(0, a.width(), 2):
            p, q = a.pixel(x, y), b.pixel(x, y)
            if max(abs(((p >> s) & 255) - ((q >> s) & 255)) for s in (0, 8, 16)) > 40:
                out.append((x / sx, y / sx))
    return out


def angle(a, b):
    return math.degrees(a.getAngle(b))


def direction_on():
    mw = FreeCADGui.getMainWindow()
    mw.raise_()
    mw.activateWindow()
    # a light from the side, so that the handle's shaft lies across the view
    # and not end on: the preference, which a view with no direction of its
    # own is lit by at once
    FreeCAD.ParamGet(VIEW).SetString("HeadlightDirection", "(-0.9,0.2,-0.4)")
    settle(1.0)
    S["before"] = on_screen("handle-before")
    check("no handle in the view before \"Direction\" is pressed", handles() == 0, handles())
    m = menu()
    button = child(m, QtWidgets.QPushButton, "LightDirectionButton")
    check("\"Direction\" is there to press, and up", button.isEnabled() and not button.isChecked())
    button.click()
    settle(1.0)
    check("pressed, it is down the next time the menu comes up",
          child(menu(), QtWidgets.QPushButton, "LightDirectionButton").isChecked())
    FreeCADGui.updateGui()
    settle(0.8)
    check("the handle is Coin's light dragger, one, in what the view renders",
          handles() == 1, "%d; under the annotation: %s" % (handles(), S.get("handle kinds")))
    S["after"] = on_screen("handle-up")
    S["mask"] = changed(S["before"], S["after"])
    gl = S["active"].graphicsView().viewport()
    cx, cy = gl.width() / 2.0, gl.height() / 2.0
    near = [p for p in S["mask"] if abs(p[0] - cx) < 60 and abs(p[1] - cy) < 60]
    check("the handle shows over the model, in the middle of the view",
          len(near) > 40, "%d of the sampled pixels changed, %d of them within 60 px of "
          "the middle" % (len(S["mask"]), len(near)))


def drag_light():
    """The handle's shaft dragged sideways: where the grab changed furthest
    from the middle of the view is the end of the shaft; a press most of
    the way out to it, and a move across it"""
    active, other = S["active"], S["other"]
    gl = active.graphicsView().viewport()
    cx, cy = gl.width() / 2.0, gl.height() / 2.0
    if not S["mask"]:
        check("the handle's shaft is found on the screen", False, "nothing changed")
        return
    fx, fy = max(S["mask"], key=lambda p: (p[0] - cx) ** 2 + (p[1] - cy) ** 2)
    reach = math.hypot(fx - cx, fy - cy)
    if not check("the handle's shaft is found on the screen", reach > 25,
                 "its end %.0f px from the middle, at (%.0f, %.0f)" % (reach, fx, fy)):
        return
    ux, uy = (fx - cx) / reach, (fy - cy) / reach
    FreeCADGui.Selection.clearSelection()
    camera = active.getCameraOrientation()
    had = prop(active, "Light_HeadlightDirection")
    others = prop(other, "Light_HeadlightDirection")
    check("the view has no direction of its own before the handle is dragged", had is None, had)
    turned = None
    for part in (0.8, 0.65, 0.5, 0.9):
        px, py = cx + ux * reach * part, cy + uy * reach * part
        start = QtCore.QPoint(int(round(px)), int(round(py)))
        QTest.mouseMove(gl, start)
        settle(0.1)
        QTest.mousePress(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier, start)
        settle(0.1)
        for i in range(1, 9):
            QTest.mouseMove(gl, start + QtCore.QPoint(int(round(-uy * 6 * i)),
                                                      int(round(ux * 6 * i))))
            settle(0.03)
        QTest.mouseRelease(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier,
                           start + QtCore.QPoint(int(round(-uy * 48)), int(round(ux * 48))))
        settle(0.3)
        turned = prop(active, "Light_HeadlightDirection")
        note("NOTE a drag from %.0f%% of the shaft: Light_HeadlightDirection %s" % (
            part * 100, turned))
        if turned is not None:
            break
    on_screen("handle-dragged")
    was = FreeCAD.Vector(-0.9, 0.2, -0.4)
    check("a drag of the handle turns the light: the view has a direction of its own",
          turned is not None and angle(turned, was) > 5.0,
          "%s, %.1f degrees from where it was" % (
              turned, angle(turned, was) if turned is not None else 0.0))
    check("the drag turned the light and not the camera",
          active.getCameraOrientation().isSame(camera, 1e-6))
    check("and selected nothing: a press on the handle is not one on the model behind it",
          not FreeCADGui.Selection.getSelectionEx(),
          [s.ObjectName for s in FreeCADGui.Selection.getSelectionEx()])
    now = prop(other, "Light_HeadlightDirection")
    check("the other view's light is where it was",
          (others is None and now is None)
          or (others is not None and now is not None and (others - now).Length < 1e-9),
          "%s, then %s" % (others, now))


def direction_off():
    view = S["active"].graphicsView()
    view.setFocus()
    QTest.keyClick(view, QtCore.Qt.Key_Escape)
    settle(0.5)
    check("Escape in the view takes the handle down: the button is up",
          not child(menu(), QtWidgets.QPushButton, "LightDirectionButton").isChecked())
    check("and no dragger is left in what the view renders", handles() == 0, handles())
    before = prop(S["active"], "Light_HeadlightDirection")
    gl = view.viewport()
    centre = QtCore.QPoint(gl.width() // 2, gl.height() // 2)
    QTest.mousePress(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier, centre + QtCore.QPoint(-60, 40))
    QTest.mouseRelease(gl, QtCore.Qt.LeftButton, QtCore.Qt.NoModifier,
                       centre + QtCore.QPoint(-60, 40))
    settle(0.3)
    after = prop(S["active"], "Light_HeadlightDirection")
    check("and a click in the view turns no light",
          (before is None and after is None)
          or (before is not None and after is not None and (before - after).Length < 1e-9),
          "%s, then %s" % (before, after))
    FreeCADGui.Selection.clearSelection()


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
