#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run the sandbox GUI gates (docs/Sandbox.md 7.9) under the FreeCAD GUI.

The Gui binary has no -t mode, so the unittest modules that need a GUI
(SandboxGui) run from this script, which FreeCAD executes at startup:

    cd build/conda-relwithdebinfo-801
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb FREECAD_USER_HOME=/tmp/fchome \
      xvfb-run -a -s "-screen 0 1280x800x24" timeout -k 5 600 \
      ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD ~/works/sw/fcad/scripts/sandbox-gui-gate.py

The result goes to $SANDBOX_GUI_GATE_RESULT (default: sandbox-gui-gate.txt
in the user data directory) and the last line is `RESULT OK` or `RESULT
FAILED`; judge by that file, not by the exit code (the GUI's exit is not
clean on every box).  $SANDBOX_GUI_GATE_MODULES selects the modules
(comma-separated, default SandboxGui).
"""

import io
import os
import sys
import traceback
import unittest

import FreeCAD
import FreeCADGui


def main():
    modules = os.environ.get("SANDBOX_GUI_GATE_MODULES", "SandboxGui").split(",")
    out = os.environ.get("SANDBOX_GUI_GATE_RESULT") or os.path.join(
        FreeCAD.getUserAppDataDir(), "sandbox-gui-gate.txt"
    )
    stream = io.StringIO()
    verdict = "FAILED"
    try:
        suite = unittest.TestSuite()
        for name in modules:
            suite.addTests(unittest.defaultTestLoader.loadTestsFromName(name.strip()))
        result = unittest.TextTestRunner(stream=stream, verbosity=2).run(suite)
        if result.wasSuccessful():
            verdict = "OK"
    except Exception:
        stream.write(traceback.format_exc())
    text = stream.getvalue()
    with open(out, "w") as f:
        f.write(text)
        f.write("\nRESULT %s\n" % verdict)
    sys.stderr.write(text)
    sys.stderr.write("RESULT %s (%s)\n" % (verdict, out))


def finish():
    try:
        main()
    finally:
        FreeCADGui.getMainWindow().close()


# run inside the event loop, after the main window is up
from PySide import QtCore  # noqa: E402

QtCore.QTimer.singleShot(0, finish)
