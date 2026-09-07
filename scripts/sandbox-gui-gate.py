#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run the sandbox GUI gates (docs/Sandbox.md 7.9) under the FreeCAD GUI.

The Gui binary has no -t mode, so the unittest modules that need a GUI
(SandboxGui) run from this script, which FreeCAD executes at startup:

    cd build/conda-relwithdebinfo-801
    env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb FREECAD_USER_HOME=/tmp/fchome \
      FCX_PYODIDE=$HOME/.local/share/FreeCAD/Pyodide/314.0.6 \
      xvfb-run -a -s "-screen 0 1280x800x24" timeout -k 5 600 \
      ~/works/sw/fcad/.conda/run.sh ./bin/FreeCAD ~/works/sw/fcad/scripts/sandbox-gui-gate.py

(FCX_PYODIDE because that user home has no runtime of its own; without
it every sandbox case skips and the verdict is still OK -- read the
case lines.)

The result goes to $SANDBOX_GUI_GATE_RESULT (default: sandbox-gui-gate.txt
in the user data directory) and the last line is `RESULT OK` or `RESULT
FAILED`; judge by that file, not by the exit code (the GUI's exit is not
clean on every box).  $SANDBOX_GUI_GATE_MODULES selects the modules
(comma-separated, default SandboxGui,SandboxWidgets,SandboxForms,SandboxNative,
SandboxPanels,SandboxDraftGui,SandboxSelection,SandboxSessionDoc,SandboxCorpusGui,SandboxInitGui;
SandboxInitGui
last: it takes the native Draft and BIM workbenches out of the session).
"""

import faulthandler
import io
import os
import sys
import traceback
import unittest

# FreeCAD's sys.stderr is its console (no fileno): the real one for a crash
try:
    faulthandler.enable(file=sys.__stderr__)
except Exception:
    pass


class _Tee(io.StringIO):
    """The runner's stream: kept for the result file, echoed to stderr
    as it goes, so a crash mid-suite still shows which case ran."""

    def write(self, s):
        sys.stderr.write(s)
        sys.stderr.flush()
        return io.StringIO.write(self, s)

import FreeCAD
import FreeCADGui


class _EagerResult(unittest.TextTestResult):
    """A failure's traceback printed as it happens, not at the end: a
    crash later in the run would otherwise take it along."""

    def addError(self, test, err):
        unittest.TextTestResult.addError(self, test, err)
        self.stream.writeln(self._exc_info_to_string(err, test))
        self.stream.flush()

    def addFailure(self, test, err):
        unittest.TextTestResult.addFailure(self, test, err)
        self.stream.writeln(self._exc_info_to_string(err, test))
        self.stream.flush()


def main():
    default_modules = ("SandboxGui,SandboxWidgets,SandboxForms,SandboxNative,SandboxPanels,"
                       "SandboxDraftGui,SandboxSelection,SandboxSessionDoc,SandboxCorpusGui,"
                       "SandboxInitGui")
    modules = os.environ.get("SANDBOX_GUI_GATE_MODULES", default_modules).split(",")
    out = os.environ.get("SANDBOX_GUI_GATE_RESULT") or os.path.join(
        FreeCAD.getUserAppDataDir(), "sandbox-gui-gate.txt"
    )
    stream = _Tee()
    verdict = "FAILED"
    try:
        suite = unittest.TestSuite()
        for name in modules:
            suite.addTests(unittest.defaultTestLoader.loadTestsFromName(name.strip()))
        result = unittest.TextTestRunner(stream=stream, verbosity=2,
                                         resultclass=_EagerResult).run(suite)
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
        # a document a module left open would make the close ask (a
        # modal, a hang under Xvfb)
        for name in list(FreeCAD.listDocuments()):
            try:
                FreeCAD.closeDocument(name)
            except Exception:
                pass
        # a close that does not end the process shows up as this stack
        # on stderr (Python's view of it) two minutes later
        try:
            faulthandler.dump_traceback_later(120, repeat=False, file=2)
        except Exception:
            pass
        sys.stderr.write("gate: closing the main window\n")
        FreeCADGui.getMainWindow().close()
        sys.stderr.write("gate: main window closed\n")
        # The event loop ends with the window and the process leaves
        # through the normal C++ teardown -- the exit() hang this once
        # skipped with os._exit (MeshLevelSource's level workers parked
        # on a static condition variable) is fixed at the source:
        # PartGui::shutdownMeshLevelWorkers() joins them on aboutToQuit.
        sys.stdout.flush()
        sys.stderr.flush()


# run inside the event loop, after the main window is up
from PySide import QtCore  # noqa: E402

QtCore.QTimer.singleShot(0, finish)
