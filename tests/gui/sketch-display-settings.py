"""Three display settings of upstream's, taken into the fork's own drawing.

  - the size of a constraint symbol is a preference of its own
    (View/ConstraintSymbolSize; upstream eef738b312, dc22fb4b9b), the
    application font's height until set. It was 0.8 of the label font's
    size and followed that;
  - a dimension's number is drawn in a chosen font
    (View/EditSketcherFontName; b9a89bada1), the label's own while unset;
  - the two axes are partly transparent
    (Mod/Sketcher/General/AxisTransparency, 30 percent; cda241dbd0).

Read from the edit graph: the image of a Horizontal constraint's icon, the
datum label's `name` field, the transparency of `RootCrossMaterials`.

And the Display page: it has the three widgets; the symbol size shows the
font's height while nothing is stored; and an OK on a page nobody touched
stores no font -- a font box always holds one, and storing it would change
what every label is drawn in.

Scored against the tree before the change: the icon does not follow the
preference, the label keeps its font, the axes are opaque, and the page has
none of the widgets.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore, QtGui, QtWidgets
from pivy import coin

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
VIEW = "User parameter:BaseApp/Preferences/View"
GENERAL = "User parameter:BaseApp/Preferences/Mod/Sketcher/General"
state = {"done": False}


def note(msg):
    with open(RESULT, "a") as f:
        f.write(str(msg) + "\n")


def check(name, cond, detail=""):
    note(("PASS " if cond else "FAIL ") + name
         + ("" if detail == "" else " (%s)" % (detail,)))
    return cond


def settle(n=10):
    for _ in range(n):
        QtCore.QCoreApplication.processEvents()
        time.sleep(0.02)


def clear_prefs():
    view = FreeCAD.ParamGet(VIEW)
    view.RemInt("ConstraintSymbolSize")
    view.RemString("EditSketcherFontName")
    FreeCAD.ParamGet(GENERAL).RemInt("AxisTransparency")


def find_all(root, type_name):
    sa = coin.SoSearchAction()
    sa.setType(coin.SoType.fromName(coin.SbName(type_name)))
    sa.setInterest(coin.SoSearchAction.ALL)
    sa.setSearchingAll(True)
    sa.apply(root)
    paths = sa.getPaths()
    return [paths[i].getTail() for i in range(paths.getLength())]


def icon_height(view):
    """the largest image under the constraints, in pixels (pivy cannot
    decode the pixels of an SoSFImage; its size is the head of its text)"""
    best = 0
    for image in find_all(view.getAuxSceneGraph(), "SoImage"):
        head = image.getField("image").get().getString().split()[:2]
        if len(head) == 2:
            best = max(best, int(head[1]))
    return best


def label_font(view):
    labels = find_all(view.getAuxSceneGraph(), "SoDatumLabel")
    return labels[0].getField("name").get().getString().strip('"') if labels else None


def axis_transparency():
    node = coin.SoNode.getByName("RootCrossMaterials")
    return round(node.transparency.getValues()[0], 3) if node.transparency.getNum() else 0.0


def in_preferences(action, page):
    got = {}

    def act():
        dialog = QtWidgets.QApplication.activeModalWidget()
        try:
            got["value"] = action(dialog)
        except Exception:
            got["error"] = traceback.format_exc()
        finally:
            if dialog is not None:
                box = dialog.findChild(QtWidgets.QDialogButtonBox)
                box.button(QtWidgets.QDialogButtonBox.Ok).click()

    QtCore.QTimer.singleShot(800, act)
    FreeCADGui.showPreferences("Sketcher", page)
    settle(25)
    if "error" in got:
        note("in the dialog: " + got["error"])
    return got.get("value")


def display_page(dialog):
    def named(name):
        return dialog.findChild(QtWidgets.QWidget, name)
    size, font, axis = named("ConstraintSymbolSize"), named("fontBoxSketcherFontName"), \
        named("axisTransparency")
    return {"widgets": [w is not None for w in (size, font, axis)],
            "size": size.value() if size is not None else None,
            "axis": axis.value() if axis is not None else None}


def run():
    try:
        import Part
        import Sketcher
        FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Document").SetInt(
            "AutoSaveTimeout", 0)
        clear_prefs()
        FreeCADGui.getMainWindow().showMaximized()
        V = FreeCAD.Vector
        doc = FreeCAD.newDocument("DisplaySettings")
        sk = doc.addObject("Sketcher::SketchObject", "Sketch")
        sk.addGeometry(Part.LineSegment(V(-30, 10, 0), V(30, 10, 0)), False)
        sk.addGeometry(Part.LineSegment(V(-30, 30, 0), V(30, 40, 0)), False)
        sk.addConstraint(Sketcher.Constraint("Horizontal", 0))
        sk.addConstraint(Sketcher.Constraint("Distance", 1, 60.0))
        doc.recompute()
        FreeCADGui.getDocument(doc.Name).setEdit(sk)
        view = FreeCADGui.activeDocument().activeView()
        view.viewTop()
        view.fitAll()
        settle(60)

        font_height = QtGui.QFontMetrics(QtWidgets.QApplication.font()).height()
        dpr = FreeCADGui.getMainWindow().devicePixelRatioF()
        grp = FreeCAD.ParamGet(VIEW)

        unset = icon_height(view)
        grp.SetInt("ConstraintSymbolSize", 40)
        settle(40)
        big = icon_height(view)
        note("icon height: unset %s, at 40: %s (font height %s, ratio %s)"
             % (unset, big, font_height, dpr))
        check("a constraint symbol is as high as the application font until its size is set",
              abs(unset - font_height * dpr) <= 1, (unset, font_height * dpr))
        check("and takes its preference then", abs(big - 40 * dpr) <= 1, big)
        grp.RemInt("ConstraintSymbolSize")

        before = label_font(view)
        family = "DejaVu Serif"
        grp.SetString("EditSketcherFontName", family)
        settle(40)
        after = label_font(view)
        check("a dimension's number is drawn in the chosen font",
              before != family and after == family, (before, after))
        grp.RemString("EditSketcherFontName")
        settle(40)
        check("and in the label's own again when the choice is taken back",
              label_font(view) == before, label_font(view))

        unset = axis_transparency()
        FreeCAD.ParamGet(GENERAL).SetInt("AxisTransparency", 60)
        settle(40)
        check("the axes are 30 percent transparent, or what the preference says",
              unset == 0.3 and axis_transparency() == 0.6, (unset, axis_transparency()))
        FreeCAD.ParamGet(GENERAL).RemInt("AxisTransparency")

        FreeCADGui.getDocument(doc.Name).resetEdit()
        settle()

        shown = in_preferences(display_page, 2)
        note("display page: %s" % (shown,))
        check("the Display page has the symbol size, the font box and the axis transparency",
              shown is not None and shown["widgets"] == [True, True, True], shown)
        check("the symbol size shows the font's height while nothing is stored",
              shown is not None and shown["size"] == font_height and shown["axis"] == 30,
              (shown, font_height))
        check("an OK on the untouched page stores no font",
              grp.GetString("EditSketcherFontName", "") == "",
              grp.GetString("EditSketcherFontName", ""))
    except Exception:
        note("ABORT:\n" + traceback.format_exc())
    clear_prefs()
    finish()


def finish():
    if state["done"]:
        return
    state["done"] = True
    for name in list(FreeCAD.listDocuments().keys()):
        FreeCAD.closeDocument(name)
    note("DONE")
    QtCore.QTimer.singleShot(300, QtCore.QCoreApplication.quit)


QtCore.QTimer.singleShot(1500, run)
