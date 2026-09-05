#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""How far Draft's and BIM's InitGui.py get in the sandbox guest (docs/Sandbox.md
7.9, G2b measured): the real files exec'd unmodified in the guest with the InitGui
runner's globals, the native workbench removed first; Draft is then activated from
the host, BIM's Initialize is called in the guest (a host activation whose
Initialize raises ends in a modal, a hang under Xvfb).  Run under the GUI on Xvfb
exactly like scripts/sandbox-gui-gate.py; the result goes to $INITGUI_PROBE_RESULT
(default /tmp/fchome/initgui-probe.txt), the guest's stderr to the console."""
import os
import sys
import traceback

import FreeCAD
import FreeCADGui as Gui
from PySide import QtCore

OUT = os.environ.get("INITGUI_PROBE_RESULT", "/tmp/fchome/initgui-probe.txt")

PRELUDE = """
import FreeCAD, FreeCADGui
App = FreeCAD
Gui = FreeCADGui
Workbench = FreeCADGui.Workbench
Log = FreeCAD.Console.PrintLog
Err = FreeCAD.Console.PrintError
Msg = FreeCAD.Console.PrintMessage
if not hasattr(FreeCAD, '__unit_test__'):
    FreeCAD.__unit_test__ = []
"""


def main():
    class Lines(list):
        def append(self, line):
            list.append(self, line)
            with open(OUT, "a") as f:
                f.write(line + "\n")
    lines = Lines()
    S = FreeCAD.ExpressionSandbox
    FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Expression/Sandbox").SetBool("Evaluate", True)
    S.setRouting(True)
    lines.append("available %s" % S.available())
    for mod, wb in (("Draft", "DraftWorkbench"), ("BIM", "BIMWorkbench")):
        path = os.path.join(FreeCAD.getHomePath(), "Mod", mod, "InitGui.py")
        src = PRELUDE + open(path, encoding="utf-8").read()
        try:
            Gui.removeWorkbench(wb)
            lines.append("%s: native workbench removed" % wb)
        except Exception as e:
            lines.append("%s: remove failed: %s" % (wb, e))
        S.resetStats()
        try:
            S.exec(src, "fcx_initgui_%s" % mod.lower())
            lines.append("%s: InitGui module level OK" % mod)
        except Exception as e:
            lines.append("%s: InitGui module level FAILED: %s" % (mod, str(e)[:1500]))
        lines.append("%s: stats after module level: %s" % (mod, S.stats()["ops"]))
        lines.append("%s: registered on host: %s" % (mod, wb in Gui.listWorkbenches()))
        if wb in Gui.listWorkbenches() and mod != "Draft":
            # a host activation whose Initialize raises ends in a modal: call it in the guest
            S.resetStats()
            try:
                S.exec("import FreeCADGui\ntry:\n    FreeCADGui.getWorkbench(%r).Initialize()\nexcept Exception as e:\n    import sys, traceback\n    sys.stderr.write('guest Initialize: ' + traceback.format_exc())\n" % wb)
                lines.append("%s: guest Initialize call returned" % mod)
            except Exception as e:
                lines.append("%s: guest Initialize FAILED: %s" % (mod, str(e)[:800]))
            lines.append("%s: stats after guest Initialize: %s" % (mod, S.stats()["ops"]))
        if wb in Gui.listWorkbenches() and mod == "Draft":
            S.resetStats()
            try:
                Gui.activateWorkbench(wb)
                lines.append("%s: activateWorkbench OK; toolbars %s" % (mod, Gui.getWorkbench(wb).listToolbars()))
            except Exception as e:
                lines.append("%s: activateWorkbench FAILED: %s" % (mod, str(e)[:1500]))
            lines.append("%s: stats after activate: %s" % (mod, S.stats()["ops"]))
            try:
                Gui.activateWorkbench("NoneWorkbench")
            except Exception as e:
                lines.append("%s: back to None FAILED: %s" % (mod, e))
    sys.stderr.write("\n".join(lines) + "\n")


def finish():
    try:
        main()
    except Exception:
        with open(OUT, "a") as f:
            f.write(traceback.format_exc())
    finally:
        Gui.getMainWindow().close()


QtCore.QTimer.singleShot(0, finish)
