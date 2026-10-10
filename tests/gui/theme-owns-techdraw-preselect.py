"""Every theme sets TechDraw's preselection colour.

TechDraw highlights what the pointer is over in its own colour if one is
stored (Mod/TechDraw/Colors/PreSelectColor), and in the 3D view's
(View/HighlightColor) if none is. The Classic theme stored one, yellow;
Dark and Light did not mention the key. So in a profile Classic had been
through, a switch to Dark or Light turned the 3D view's highlight blue and
left TechDraw's yellow, for good; in a profile it had not, TechDraw followed
the 3D view -- "sometimes" (docs/HandsOnQueue.md entry 34). Decided by the
reporter: Dark and Light set the key too.

Claims, on a fresh profile:

  - Classic stores a preselection colour for TechDraw;
  - after Dark, TechDraw's is the colour Dark gives the 3D view, and not
    Classic's;
  - after Light likewise;
  - after Classic again it is Classic's own.

TechDraw reads the colour each time something is hovered
(QGIView::getPreColor), so an open page follows without being reopened;
that is from the code, not claimed here.

Scored against the tree before the change: see the commit message.
"""
import os
import time
import traceback

import FreeCAD
import FreeCADGui
from PySide import QtCore

OUT = os.environ["GT_OUT"]
RESULT = os.environ.get("GT_RESULT", os.path.join(OUT, "result.txt"))
PREFS = "User parameter:BaseApp/Preferences/"
UNSET = 0x01020304


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


def colours():
    """(what TechDraw highlights with, the 3D view's highlight, TechDraw's own key or None)."""
    view = FreeCAD.ParamGet(PREFS + "View").GetUnsigned("HighlightColor", 0xFFFF00FF)
    own = FreeCAD.ParamGet(PREFS + "Mod/TechDraw/Colors").GetUnsigned("PreSelectColor", UNSET)
    return (view if own == UNSET else own), view, (None if own == UNSET else own)


def show(value):
    return "unset" if value is None else "0x%08X" % value


def run():
    try:
        themes = FreeCADGui.listThemes()
        if not check("the three themes are there", all(t in themes for t in ("Classic", "Dark", "Light")), themes):
            return
        FreeCADGui.applyTheme("Classic")
        settle(2.0)
        classic, classic_view, own = colours()
        check("Classic stores a preselection colour for TechDraw", own is not None, show(own))
        for theme in ("Dark", "Light"):
            FreeCADGui.applyTheme(theme)
            settle(2.0)
            techdraw, view, own = colours()
            check("after %s, TechDraw's preselection is the 3D view's" % theme, techdraw == view,
                  "TechDraw %s (its key %s), the 3D view %s" % (show(techdraw), show(own), show(view)))
            check("after %s it is not Classic's" % theme, techdraw != classic,
                  "%s, Classic's %s" % (show(techdraw), show(classic)))
        FreeCADGui.applyTheme("Classic")
        settle(2.0)
        techdraw, view, own = colours()
        check("after Classic again it is Classic's own", techdraw == classic,
              "%s, %s at first" % (show(techdraw), show(classic)))
    except Exception:
        note("FAIL the test ran | " + traceback.format_exc().replace("\n", " | "))
    finally:
        note("DONE")
        QtCore.QTimer.singleShot(0, FreeCADGui.getMainWindow().close)


QtCore.QTimer.singleShot(1500, run)
