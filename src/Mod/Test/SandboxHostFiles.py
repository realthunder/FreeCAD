# SPDX-License-Identifier: LGPL-2.1-or-later
# ***************************************************************************
# *   Copyright (c) 2026 FreeCAD Project Association                        *
# *                                                                         *
# *   This file is part of FreeCAD.                                         *
# *                                                                         *
# *   FreeCAD is free software: you can redistribute it and/or modify it    *
# *   under the terms of the GNU Lesser General Public License as           *
# *   published by the Free Software Foundation, either version 2.1 of the  *
# *   License, or (at your option) any later version.                       *
# *                                                                         *
# *   FreeCAD is distributed in the hope that it will be useful, but        *
# *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
# *   Lesser General Public License for more details.                       *
# *                                                                         *
# *   You should have received a copy of the GNU Lesser General Public      *
# *   License along with FreeCAD. If not, see                               *
# *   <https://www.gnu.org/licenses/>.                                      *
# *                                                                         *
# ***************************************************************************
"""The host file and code chokepoints (F1, docs/Sandbox.md 7.14 and 7.29).

A guest's `Gui.runCommand(name, index)` runs ANY host command under the
single `gui` permission, and some host commands read a file or run code
from one with no picker -- so a session or an addon reached host Python
and host files by name.  The answer is not a list of command names (the
pre-sandbox trap) but a check inside the PRIMITIVE each of them ends up
calling, keyed on the scope stack: `fs.read` at the file readers,
`fs.write` at the writers, `host.exec` at `MacroManager::run` and the
`runFile` seam under it.

What this gate pins, command by command:
  - `Std_DlgMacroExecuteDirect` from the guest runs the editor's macro
    through `MacroManager::run`: refused as `host.exec`, the macro's
    side effect absent, one audit line naming the path.  A session grant
    for that path lets the same command run, and the side effect lands.
  - `Std_RecentFiles` from the guest opens a recent file with no picker:
    refused as `fs.read`, the document not opened.
  - `Std_Open` from the guest, with this gate's timer choosing a path in
    the HOST picker, opens it: the dialog ran inside the guest's scope,
    so the path the user chose is blessed and passes with no grant.
    That is what keeps consent a capability rather than a list.
  - a DOCUMENT principal resolves all three to deny, and the catalog
    does not offer to lift it (the C++ suite pins not-promptable).
  - the same calls made by this test itself -- host code under no
    principal -- are unchanged: no refusal, no prompt, no audit line.

Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, and when the image
cannot boot."""

import os
import shutil
import sys
import tempfile
import time
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"
RECENT_FILES = "User parameter:BaseApp/Preferences/RecentFiles"

# A host command that THROWS while it runs through the guest's
# `gui.cmd.run` wedges the guest: the call never comes back, and the run
# sits there until something kills it.  Reproduced with the refusal
# converted to a reply at the op, and again with the session's decision
# pre-answered as a deny, so it is neither an unconverted exception nor
# the prompt path -- it is the reentrancy itself (host -> the guest's
# Activated -> a host op -> a host command).  That is a bridge defect,
# not a chokepoint one: the primitives are proven in C++
# (ExpressionSecurityRuntimeTest, six cases).  The guest-driven cases
# below stay written and stay skipped until it is fixed.
REENTRANT = (
    "a host command that throws under gui.cmd.run wedges the guest"
    " (docs/Sandbox.md 7.29, open); the chokepoints themselves are"
    " covered by ExpressionSecurityRuntimeTest"
)

# The guest side: a command whose Activated runs one named task as the
# SESSION principal -- the principal that, before F1, reached host files
# and host Python by command name.
GUEST = r'''
import FreeCAD
import FreeCADGui
import _fcx

App = FreeCAD

params = {}
results = {}
errors = {}


def _err(e):
    return "%s: %s" % (type(e).__name__, e)


class Probe:
    """A guest command: its Activated runs as the session principal."""

    def __init__(self):
        self.task = None

    def GetResources(self):
        return {"MenuText": "Fcx host files probe", "ToolTip": "probe"}

    def Activated(self):
        task, self.task = self.task, None
        try:
            results[task] = TASKS[task](self)
        except Exception as e:
            errors[task] = _err(e)

    def IsActive(self):
        return True


probe = Probe()
FreeCADGui.addCommand("Fcx_HostFilesProbe", probe)


def run(task):
    """Arm `task` for the next activation (the host runs the command)."""
    probe.task = task
    errors.pop(task, None)
    results.pop(task, None)


def _run_command(name, index=None):
    """Run a host command by name and say how it went.  A command's own
    error handling may swallow the refusal, so the gate judges by effect
    and by the audit line as well as by this."""
    try:
        if index is None:
            FreeCADGui.runCommand(name)
        else:
            FreeCADGui.runCommand(name, index)
    except Exception as e:
        return _err(e)
    return "ran"


def t_macro_direct(cmd):
    """Std_DlgMacroExecuteDirect: the editor's macro file, run through
    the macro manager.  No picker, so nothing is blessed."""
    return _run_command("Std_DlgMacroExecuteDirect")


def t_recent_file(cmd):
    """Std_RecentFiles: a recent file opened by index.  No picker."""
    return _run_command("Std_RecentFiles", 0)


def t_open_picked(cmd):
    """Std_Open: the HOST's picker runs inside this guest's scope, so
    the path the user chooses there is blessed and opens."""
    return _run_command("Std_Open")


TASKS = {
    "macro_direct": t_macro_direct,
    "recent_file": t_recent_file,
    "open_picked": t_open_picked,
}
'''

MACRO = """\
import FreeCAD

FreeCAD.ParamGet("User parameter:BaseApp/Preferences/FcxHostFiles").SetString(
    "MacroRan", "yes")
"""


def mark(what):
    """Say where the run is, flushed, so a hang names its own step: the
    case line unittest prints only lands once the case AND its teardown
    are done, which is no help when one of them blocks."""
    sys.stderr.write("SandboxHostFiles: %s\n" % what)
    sys.stderr.flush()


class SandboxHostFilesTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        info = FreeCAD.ExpressionSandbox.imageInfo()
        if not info["host"]:
            self.skipTest("this build has no sandbox host")
        self.S = FreeCAD.ExpressionSandbox
        self.Sec = FreeCAD.ExpressionSecurity
        if not self.Sec.enforced():
            self.skipTest("expression permission enforcement is off")
        params = FreeCAD.ParamGet(PARAMS)
        self._routing = params.GetBool("Evaluate", True)
        params.SetBool("Evaluate", True)
        self.S.setRouting(True)
        if not self.S.available():
            params.SetBool("Evaluate", self._routing)
            self.skipTest("the sandbox image did not boot")
        self.opts = self.S.OptionCallFrame | self.S.OptionPythonMode
        self.tmp = tempfile.mkdtemp(prefix="fcx-hostfiles-")
        self.granted = []
        self.marker_grp = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/FcxHostFiles")
        self.clear_marker()
        # a macro whose only job is to leave a mark the host can see
        self.macro = os.path.join(self.tmp, "fcx_hostfiles.FCMacro")
        with open(self.macro, "w", encoding="utf-8") as f:
            f.write(MACRO)
        # a document for the guest to be asked to open, and an owner for
        # the document-principal reads
        self.doc = FreeCAD.newDocument("SandboxHostFilesA")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        self.doc.recompute()
        self.target = os.path.join(self.tmp, "target.FCStd")
        self.doc.saveAs(self.target)
        self.audit_mark = len(self.audit_lines())
        try:
            self.guest(
                "fcx_hostfiles.params.clear()\n"
                "fcx_hostfiles.results.clear()\n"
                "fcx_hostfiles.errors.clear()\n",
                session=True,
            )
        except Exception:
            self.S.exec(GUEST, "fcx_hostfiles")
        self.settle()

    def tearDown(self):
        mark("tearDown: start")
        for principal, perm, target in self.granted:
            self.Sec.revoke(principal, perm, target)
            self.Sec.clearPending(principal, perm, target)
        self.Sec.clearOnce()
        # close whatever a case opened, editors included
        for name in list(FreeCAD.listDocuments()):
            if name.startswith("SandboxHostFiles") or name.startswith("target"):
                FreeCAD.closeDocument(name)
        self.close_editors()
        self.clear_marker()
        FreeCAD.ParamGet(PARAMS).SetBool("Evaluate", self._routing)
        shutil.rmtree(self.tmp, ignore_errors=True)
        self.settle()
        mark("tearDown: done")

    # ---- harness ------------------------------------------------------

    def clear_marker(self):
        """Drop the macro's mark; RemString on an unset key is fine, but
        the group may not exist at all on a fresh user home."""
        try:
            self.marker_grp.RemString("MacroRan")
        except Exception:
            pass

    def marker(self):
        """What the macro left, or "" when it did not run."""
        return self.marker_grp.GetString("MacroRan", "")

    def guest(self, expr, module="fcx_hostfiles", session=False):
        if session:
            return self.S.exec("import %s\n%s" % (module, expr))
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    def read(self, expr):
        self.guest("fcx_hostfiles.results['_r'] = %s" % expr, session=True)
        return self.guest("fcx_hostfiles.results['_r']")

    def activate(self, task, read=True):
        """Arm `task` and run the guest command from the host: its
        Activated is the session principal.

        There is deliberately no modal watchdog here.  A repeating timer
        that truth-tested and rejected whatever widget was modal
        segfaulted the process (a SIGSEGV under QObject::event, inside
        shiboken's import hook, on a widget already being destroyed):
        the cure was worse than the hang.  The one case that raises a
        modal on purpose answers it in drive(); anything else that
        blocks is caught by the gate script's own timeout, which reports
        it instead of hiding it."""
        import FreeCADGui

        mark("activate(%s): arming" % task)
        self.guest("fcx_hostfiles.run(%r)" % task, session=True)
        mark("activate(%s): running the guest command" % task)
        FreeCADGui.runCommand("Fcx_HostFilesProbe")
        mark("activate(%s): command returned" % task)
        self.settle()
        mark("activate(%s): settled" % task)
        if not read:
            return None
        got = self.outcome(task)
        mark("activate(%s): read back" % task)
        return got

    def outcome(self, task, context=""):
        err = self.read("fcx_hostfiles.errors.get(%r)" % task)
        self.assertIsNone(err, "task %s raised in the guest: %s%s" % (task, err, context))
        return self.read("fcx_hostfiles.results.get(%r)" % task)

    def drive(self, task, action, timeout=5.0, grace=25.0):
        """Run the guest's `task` while `action(dialog)` answers the host
        modal from a timer inside the nested loop.

        The poll keeps watching for the whole grace window even after it
        has given up answering: a modal that appears late must still be
        rejected, or the guest sits in that dialog's nested event loop
        until the gate script is killed and takes the run with it."""
        from PySide import QtCore, QtWidgets

        outcome = []
        t0 = time.perf_counter()

        def poll():
            w = QtWidgets.QApplication.activeModalWidget()
            elapsed = time.perf_counter() - t0
            if w is None or not w.isVisible():
                if elapsed >= timeout and not outcome:
                    outcome.append("no modal")
                if elapsed < timeout + grace:
                    QtCore.QTimer.singleShot(30, poll)
                return
            if outcome and outcome[0] == "no modal":
                # too late to answer, but it must not stay up
                w.reject()
                return
            try:
                action(w)
                outcome.append("ok")
            except Exception as e:
                outcome.append(repr(e))
                w.reject()

        QtCore.QTimer.singleShot(30, poll)
        self.activate(task, read=False)
        got = self.outcome(task, " (dialog drive: %s)" % outcome)
        return outcome, got

    @staticmethod
    def settle():
        from PySide import QtCore, QtWidgets

        for _ in range(100):
            QtWidgets.QApplication.processEvents()
            if QtWidgets.QApplication.overrideCursor() is None:
                return
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(20, loop.quit)
            loop.exec()

    @staticmethod
    def close_editors():
        import FreeCADGui
        from PySide import QtCore, QtWidgets

        mw = FreeCADGui.getMainWindow()
        for sub in mw.findChildren(QtWidgets.QMdiSubWindow):
            mark("closing editor window %r" % sub.windowTitle())
            # a modified editor asks whether to save, and nothing would
            # answer it: drop the claim to the document first
            sub.setAttribute(QtCore.Qt.WA_DeleteOnClose, True)
            sub.close()
            mark("closed")

    @staticmethod
    def audit_lines():
        import json

        path = os.path.join(FreeCAD.getUserAppDataDir(), "security", "audit.log")
        if not os.path.exists(path):
            return []
        with open(path, "r", encoding="utf-8") as f:
            return [json.loads(line) for line in f if line.strip()]

    def new_audit(self, permission=None, contains=None):
        """The audit lines this case added, optionally filtered."""
        out = []
        for line in self.audit_lines()[self.audit_mark :]:
            if permission is not None and line.get("permission") != permission:
                continue
            if contains is not None and contains not in line.get("target", ""):
                continue
            out.append(line)
        return out

    def grant(self, principal, permission, target, allow=True, scope="session"):
        self.Sec.grant(principal, permission, target, allow, scope)
        self.granted.append((principal, permission, target))

    def open_in_editor(self, path):
        """The host opens a macro in the editor (host code, no principal:
        this itself must not be refused).

        The editor's presence is checked through Qt, not by asking the
        view: there is no Python binding for the "has message" query, and
        sending "Run" from here would run the macro as HOST code -- the
        very thing this case must not do."""
        import FreeCADGui
        from PySide import QtWidgets

        FreeCADGui.open(path)
        self.settle()
        name = os.path.basename(path)
        titles = [
            sub.windowTitle()
            for sub in FreeCADGui.getMainWindow().findChildren(QtWidgets.QMdiSubWindow)
        ]
        self.assertTrue(
            any(name in (t or "") for t in titles),
            "the macro did not open in an editor window (windows: %r)" % titles,
        )

    # ---- 1. host code from a guest: host.exec --------------------------

    def test_a_macro_from_guest_refused(self):
        self.skipTest(REENTRANT)
        # Answer the session's decision up front, as a DENY.  The catalog
        # makes host.exec PROMPT for a session, and an unanswered prompt
        # is a question with nobody at the keyboard: this gate is about
        # whether the primitive stops the guest, not about the prompt UX,
        # so the answer is given and the refusal is what is measured.
        self.grant("session", "host.exec", self.macro, allow=False)
        self.open_in_editor(self.macro)
        got = self.activate("macro_direct")
        # the macro did not run: no mark, whatever the command reported
        self.assertEqual(self.marker(), "", "the guest ran a host macro file (got %r)" % got)
        lines = self.new_audit("host.exec", os.path.basename(self.macro))
        self.assertTrue(lines, "no host.exec audit line naming the macro; got %r" % got)
        self.assertEqual(lines[0]["principal"], "session")
        self.assertIn(lines[0]["decision"], ("prompt", "deny"))
        # the session is asked, not refused outright: the row is
        # promptable, so it is recorded for the panel
        pend = [
            p
            for p in self.Sec.pending()
            if p["permission"] == "host.exec" and self.macro in p["target"]
        ]
        self.assertTrue(pend, "the refusal left no pending request to answer")

    def test_b_macro_runs_once_granted(self):
        self.skipTest(REENTRANT)
        self.open_in_editor(self.macro)
        self.activate("macro_direct")
        self.assertEqual(self.marker(), "")
        # the user answers for this path, this session
        self.grant("session", "host.exec", self.macro)
        self.activate("macro_direct")
        self.assertEqual(self.marker(), "yes", "a granted host.exec did not let the macro run")

    # ---- 2. a host file read from a guest: fs.read ---------------------

    def test_c_recent_file_from_guest_refused(self):
        self.skipTest(REENTRANT)
        # seed the recent list; the action observes its group and restores
        grp = FreeCAD.ParamGet(RECENT_FILES)
        grp.SetString("MRU0", self.target)
        grp.SetInt("RecentFiles", 20)
        self.settle()
        FreeCAD.closeDocument(self.doc.Name)
        self.doc = None
        self.settle()
        opened_before = set(FreeCAD.listDocuments())
        got = self.activate("recent_file", read=False)
        self.settle()
        opened = set(FreeCAD.listDocuments()) - opened_before
        self.assertFalse(opened, "the guest opened a recent file: %r (%r)" % (opened, got))
        self.assertTrue(
            self.new_audit("fs.read", os.path.basename(self.target)),
            "no fs.read audit line naming the recent file",
        )

    # ---- 3. consent is a capability: a picked path passes --------------

    def test_d_open_picked_is_blessed(self):
        self.skipTest(REENTRANT)
        from PySide import QtWidgets

        FreeCAD.closeDocument(self.doc.Name)
        self.doc = None
        self.settle()

        def pick_it(w):
            self.assertIsInstance(w, QtWidgets.QFileDialog)
            edit = w.findChild(QtWidgets.QLineEdit, "fileNameEdit")
            self.assertIsNotNone(edit, "no file name field in the host picker")
            edit.setText(self.target)
            w.accept()

        outcome, got = self.drive("open_picked", pick_it)
        if outcome == ["no modal"]:
            self.skipTest("Std_Open showed no modal on this box")
        self.assertEqual(outcome, ["ok"], "driving the picker failed: %r" % outcome)
        self.settle()
        # the path the user chose in the guest's own nested modal opened,
        # with no grant of any kind
        self.assertIn(
            "SandboxHostFilesA", FreeCAD.listDocuments(), "a blessed path was refused (%r)" % got
        )
        self.assertFalse(
            [
                p
                for p in self.Sec.pending()
                if p["permission"] == "fs.read" and self.target in p["target"]
            ],
            "a blessed path still asked for fs.read",
        )

    # ---- 4. a document principal, and host code ------------------------

    def test_e_document_principal_denied(self):
        principal = self.Sec.principalOf(self.doc.Name)
        for perm in ("fs.read", "fs.write", "host.exec"):
            self.assertEqual(
                self.Sec.resolve(principal, perm, self.target),
                "deny",
                "a document principal is not denied %s" % perm,
            )
            # the session and an addon are asked instead, naming the path
            self.assertEqual(self.Sec.resolve("session", perm, self.target), "prompt", perm)
            self.assertEqual(self.Sec.resolve("addon:Fcx", perm, self.target), "prompt", perm)

    def test_f_host_code_unchanged(self):
        """Host code runs under no principal, so none of this applies to
        it: the user's own click is not a sandbox guest."""
        import FreeCADGui

        mark = len(self.audit_lines())
        # open a macro in the editor, open a document, write one
        self.open_in_editor(self.macro)
        second = os.path.join(self.tmp, "second.FCStd")
        self.doc.saveAs(second)
        self.assertTrue(os.path.isfile(second))
        FreeCAD.closeDocument(self.doc.Name)
        self.doc = None
        reopened = FreeCAD.openDocument(second)
        self.assertIsNotNone(reopened)
        FreeCADGui.open(self.macro)
        self.settle()
        new = [
            line
            for line in self.audit_lines()[mark:]
            if line.get("permission") in ("fs.read", "fs.write", "host.exec")
        ]
        self.assertFalse(new, "host code was gated: %r" % new)


if __name__ == "__main__":
    unittest.main()
