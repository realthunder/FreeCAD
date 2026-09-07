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
"""The session document (docs/Sandbox.md 7.13, S1): a guest command's
`Activated()` runs as the session principal, and the session reaches
EVERY open document -- `FreeCAD.ActiveDocument` is the host's live
active document, a kept handle re-resolves by its own document's
name, `listDocuments`/`getDocument` are `app.query`, `newDocument`/
`closeDocument`/`setActiveDocument` are `app.write` (DENY for a
document, not promptable), `save()` writes the document's own file and
`saveAs(path)` takes a picker-blessed path only.  A DOCUMENT principal
keeps its own document, always.  S2 stands on it: `Gui.doCommand` /
`doCommandGui` / `addModule` run their source IN the guest's `__main__`
under the caller's principal, one host op (`gui.docommand`) recording
the macro line and the audit line under `gui.doCommand` (DENY for a
document, not promptable) -- Draft's commit through `todo.doTasks` and
BIM's `Arch_Site` end to end.
Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Skips headless, on a build without the sandbox host, and when the image
cannot boot."""

import os
import sys
import tempfile
import time
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

# The guest side of the harness: a command whose Activated runs one
# named task as the session principal, and a document-principal probe.
GUEST = r'''
import FreeCAD
import FreeCADGui
import _fcx

App = FreeCAD

params = {}     # what the host tells the tasks
results = {}    # what a task returned
errors = {}     # what a task raised


def _err(e):
    return "%s: %s" % (type(e).__name__, e)


class Probe:
    """A guest command: its Activated runs as the session principal."""

    def __init__(self):
        self.doc = None
        self.obj = None
        self.task = None

    def GetResources(self):
        return {"MenuText": "Fcx session document probe", "ToolTip": "probe"}

    def Activated(self):
        task, self.task = self.task, None
        try:
            results[task] = TASKS[task](self)
        except Exception as e:
            errors[task] = _err(e)

    def IsActive(self):
        return True


probe = Probe()
FreeCADGui.addCommand("Fcx_SessDocProbe", probe)


def run(task):
    """Arm `task` for the next activation (the host runs the command)."""
    probe.task = task
    errors.pop(task, None)
    results.pop(task, None)


def t_active(cmd):
    d = FreeCAD.ActiveDocument
    if d is None:
        return {"none": True, "call": App.activeDocument(), "gui": FreeCADGui.ActiveDocument,
                "gui_call": FreeCADGui.activeDocument()}
    g = FreeCADGui.ActiveDocument
    return {"none": False, "name": d.Name, "call": App.activeDocument().Name,
            "gui": g.Document.Name, "gui_call": FreeCADGui.activeDocument().Document.Name,
            "same": App.activeDocument() == d and g.Document == d,
            "modified": g.Modified, "in_edit": g.getInEdit()}


def t_switch(cmd):
    FreeCAD.setActiveDocument(params["other"])
    return [FreeCAD.ActiveDocument.Name, App.activeDocument().Name,
            FreeCADGui.ActiveDocument.Document.Name]


def t_writes(cmd):
    import Part

    doc = FreeCAD.ActiveDocument
    doc.openTransaction("Fcx write")
    obj = doc.addObject("Part::Feature", "FcxObj")
    obj.Label = "written from the guest"
    obj.Shape = Part.makeBox(1, 2, 3)
    obj.ViewObject.Visibility = False
    gone = doc.addObject("Part::Feature", "FcxGone")
    doc.removeObject(gone.Name)
    doc.commitTransaction()
    n = doc.recompute()
    vp = FreeCADGui.ActiveDocument.getObject(obj.Name)
    return [obj.Name, obj.Label, round(obj.Shape.Volume, 6), obj.ViewObject.Visibility,
            vp.Visibility, doc.getObject("FcxGone") is None, n, list(doc.UndoNames),
            doc.UndoCount, [o.Name for o in doc.RootObjects]]


def t_keep(cmd):
    doc = FreeCAD.ActiveDocument
    cmd.doc = doc
    cmd.obj = doc.getObject(params["keep"])
    return [cmd.doc.Name, cmd.obj.Name]


def t_reuse(cmd):
    cmd.obj.Label = params["label"]
    return [cmd.doc.Name, cmd.obj.Name, cmd.obj.Label,
            cmd.doc.getObject(cmd.obj.Name) == cmd.obj, FreeCAD.ActiveDocument.Name]


def t_reuse_closed(cmd):
    try:
        return cmd.obj.Label
    except ReferenceError as e:
        return "ReferenceError: %s" % e


def t_two(cmd):
    a, b = params["a"], params["b"]
    docs = FreeCAD.listDocuments()
    out = {"names": sorted(docs), "active": FreeCAD.ActiveDocument.Name}
    db = FreeCAD.getDocument(b)
    o = db.addObject("Part::Feature", "FcxInB")
    o.Label = "from the guest while %s is active" % a
    out["b_obj"] = [o.Name, o.Document.Name, docs[b] == db, FreeCADGui.getDocument(b).Document.Name]
    n = FreeCAD.newDocument("FcxNewDoc")
    new_name = n.Name
    out["new"] = [new_name, new_name in FreeCAD.listDocuments(), FreeCAD.ActiveDocument.Name]
    FreeCAD.closeDocument(new_name)
    out["closed"] = new_name not in FreeCAD.listDocuments()
    try:
        FreeCAD.getDocument("FcxNoSuchDocument")
        out["missing"] = "no error"
    except NameError:
        out["missing"] = "NameError"
    FreeCAD.setActiveDocument(a)
    return out


def doc_probe(what):
    """Run as the DOCUMENT principal (a routed evaluation on an object
    of document a): what a document may reach."""
    try:
        if what == "listDocuments":
            return sorted(FreeCAD.listDocuments())
        if what == "getDocument":
            return FreeCAD.getDocument(params["b"]).Name
        if what == "getOwn":
            return FreeCAD.getDocument(params["a"]).Name
        if what == "newDocument":
            return FreeCAD.newDocument("FcxDocPrincipal").Name
        if what == "closeDocument":
            FreeCAD.closeDocument(params["b"])
            return "closed"
        if what == "setActiveDocument":
            FreeCAD.setActiveDocument(params["b"])
            return "set"
        if what == "active":
            return FreeCAD.ActiveDocument.Name
        if what == "resolve":
            return _fcx.op("resolve", 0, [params["b"], params["b_obj"]])
        if what == "docommand":
            FreeCADGui.doCommand("fcx_doc_marker = 1")
            return "ran"
        if what == "addModule":
            FreeCADGui.addModule("Mesh")
            return "imported"
        if what == "docommand_ran":
            return "fcx_doc_marker" in _main()
    except Exception as e:
        return _err(e)
    return "unknown probe"


def t_save(cmd):
    doc = FreeCAD.ActiveDocument
    doc.save()
    return doc.FileName


def t_saveas_picked(cmd):
    from PySide import QtWidgets

    path, _ = QtWidgets.QFileDialog.getSaveFileName(None, "Fcx save as", params["dir"],
                                                    "FreeCAD (*.FCStd)")
    FreeCAD.ActiveDocument.saveAs(path)
    return [path, FreeCAD.ActiveDocument.FileName]


def t_saveas_madeup(cmd):
    try:
        FreeCAD.ActiveDocument.saveAs(params["madeup"])
    except PermissionError as e:
        return "PermissionError: %s" % e
    return "saved"


def load_corpus():
    """Import Draft's and BIM's command modules in the guest and
    register their commands under Fcx_ names: the host may hold the
    native ones, and a guest registration must not shadow those."""
    recorded = {}
    original = FreeCADGui.addCommand

    def record(name, obj, activation=None):
        recorded[name] = obj

    FreeCADGui.addCommand = record
    try:
        import draftguitools.gui_heal
        import bimcommands.BimTrash
    finally:
        FreeCADGui.addCommand = original
    for name in ("Draft_Heal", "BIM_Trash", "BIM_EmptyTrash"):
        cmd = recorded[name]
        if name.startswith("BIM_"):
            # Command::_invoke consults IsActive; BIM's asks the active
            # window for a scene graph, which is G4 -- the gate stands
            # in for the view (Draft's asks for the active document,
            # which S1 answers)
            type(cmd).IsActive = lambda self: True
        FreeCADGui.addCommand("Fcx_" + name, cmd)
    return sorted(recorded)


def load_corpus_docommand():
    """S2's two: Draft_Upgrade (a Modifier: DraftGui's tool bar, the
    working plane, the commit through todo.doTasks and Gui.doCommand)
    and Arch_Site (addModule, doCommand, a transaction).  Both IsActive
    ask for the 3D view (G4): stubbed, as above."""
    import DraftGui  # noqa: F401  (Gui.draftToolBar, which a Modifier's Activated takes)

    import importlib
    import sys

    if not hasattr(FreeCAD, "activeDraftCommand"):
        FreeCAD.activeDraftCommand = None  # DraftTools.py's, at the workbench's import
    recorded = {}
    original = FreeCADGui.addCommand

    def record(name, obj, activation=None):
        recorded[name] = obj

    FreeCADGui.addCommand = record
    try:
        # (a package's first command module imports them all: the one
        # load_corpus did leaves these imported and unrecorded -- reload)
        for mod in ("draftguitools.gui_upgrade", "bimcommands.BimSite"):
            if mod in sys.modules:
                importlib.reload(sys.modules[mod])
            else:
                importlib.import_module(mod)
    finally:
        FreeCADGui.addCommand = original
    for name in ("Draft_Upgrade", "Arch_Site"):
        cmd = recorded[name]
        type(cmd).IsActive = lambda self: True
        FreeCADGui.addCommand("Fcx_" + name, cmd)
    return sorted(recorded)


def _main():
    import sys

    return sys.modules["__main__"].__dict__


def t_docommand(cmd):
    main = _main()
    main.pop("fcx_marker", None)
    main.pop("_fcx_o", None)
    FreeCADGui.doCommand(params["marker_src"])
    FreeCADGui.addModule("Part")
    FreeCADGui.addModule("Part")  # once: one macro line
    FreeCADGui.doCommand("_fcx_o = App.ActiveDocument.addObject('Part::Feature', 'FcxDoCmd')")
    FreeCADGui.doCommandGui("_fcx_o.ViewObject.Visibility = False")
    FreeCADGui.doCommand("_fcx_o.Shape = Part.makeBox(1, 1, 1)")
    out = {"marker": main.get("fcx_marker"), "part": main.get("Part") is not None,
           "bound": [k for k in ("FreeCAD", "App", "FreeCADGui", "Gui") if k in main],
           "obj": main["_fcx_o"].Name, "vis": main["_fcx_o"].ViewObject.Visibility,
           "volume": round(main["_fcx_o"].Shape.Volume, 6),
           "returns": FreeCADGui.doCommand("pass")}
    try:
        FreeCADGui.doCommand("1 +")
        out["bad"] = "no error"
    except SyntaxError:
        out["bad"] = "SyntaxError"
    try:
        FreeCADGui.doCommand("raise KeyError('fcx')")
        out["raised"] = "no error"
    except KeyError:
        out["raised"] = "KeyError"
    return out


def t_makesite(cmd):
    try:
        FreeCADGui.addModule("Arch")
        FreeCADGui.doCommand("obj = Arch.makeSite()")
        return ["ok", "obj" in _main()]
    except Exception:
        import traceback

        return traceback.format_exc()


TASKS = {
    "active": t_active, "switch": t_switch, "writes": t_writes, "keep": t_keep,
    "reuse": t_reuse, "reuse_closed": t_reuse_closed, "two": t_two, "save": t_save,
    "saveas_picked": t_saveas_picked, "saveas_madeup": t_saveas_madeup,
    "docommand": t_docommand, "makesite": t_makesite,
}
'''


class PrefGuard:
    """Set preferences for the test, put them back after (global state)."""

    def __init__(self):
        self._undo = []

    def set_bool(self, key, value):
        params = FreeCAD.ParamGet(PARAMS)
        had = key in params.GetBools()
        prior = params.GetBool(key, False) if had else None
        self._undo.append((key, had, prior))
        params.SetBool(key, value)

    def restore(self):
        params = FreeCAD.ParamGet(PARAMS)
        for key, had, prior in reversed(self._undo):
            if had:
                params.SetBool(key, prior)
            else:
                params.RemBool(key)
        self._undo = []


class SandboxSessionDocTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        info = FreeCAD.ExpressionSandbox.imageInfo()
        if not info["host"]:
            self.skipTest("this build has no sandbox host")
        self.S = FreeCAD.ExpressionSandbox
        self.prefs = PrefGuard()
        self.prefs.set_bool("Evaluate", True)
        self.S.setRouting(True)
        if not self.S.available():
            self.prefs.restore()
            self.skipTest("the sandbox image did not boot")
        self.opts = self.S.OptionCallFrame | self.S.OptionPythonMode
        self.settle()
        self.doc = FreeCAD.newDocument("SandboxSessDocA")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        self.box = self.doc.addObject("Part::Box", "Box")
        self.doc.recompute()
        self.other = None
        self.tmp = tempfile.mkdtemp(prefix="fcx-sessdoc-")
        import FreeCADGui

        FreeCADGui.Selection.clearSelection()
        try:
            self.guest("fcx_sessdoc.params.clear()\nfcx_sessdoc.results.clear()\n"
                       "fcx_sessdoc.errors.clear()\n", session=True)
        except Exception:
            self.S.exec(GUEST, "fcx_sessdoc")
        FreeCAD.setActiveDocument(self.doc.Name)

    def tearDown(self):
        import FreeCADGui

        FreeCADGui.Selection.clearSelection()
        for name in list(FreeCAD.listDocuments()):
            if name.startswith("SandboxSessDoc") or name.startswith("Fcx"):
                FreeCAD.closeDocument(name)
        self.settle()
        self.prefs.restore()

    def guest(self, expr, module="fcx_sessdoc", session=False):
        """A python-mode read of the probe module's state (the document
        principal), or a session exec."""
        if session:
            return self.S.exec("import %s\n%s" % (module, expr))
        return self.S.evaluate(self.owner, "import %s; %s" % (module, expr), self.opts)

    def read(self, expr):
        """A session read of a probe value (the ops are the session's)."""
        self.guest("fcx_sessdoc.results['_r'] = %s" % expr, session=True)
        return self.guest("fcx_sessdoc.results['_r']")

    def param(self, **kw):
        for k, v in kw.items():
            self.guest("fcx_sessdoc.params[%r] = %r" % (k, v), session=True)

    def activate(self, task, command="Fcx_SessDocProbe", read=True):
        """Arm `task`, run the guest command from the host (its Activated
        is the session principal), and return what the task returned
        (with read=False only run it: the reads need a document for the
        probe's owner)."""
        import FreeCADGui

        self.guest("fcx_sessdoc.run(%r)" % task, session=True)
        FreeCADGui.runCommand(command)
        self.settle()
        if not read:
            return None
        return self.outcome(task)

    def outcome(self, task, context=""):
        """What `task` returned; its guest exception is the failure."""
        err = self.read("fcx_sessdoc.errors.get(%r)" % task)
        self.assertIsNone(err, "task %s raised in the guest: %s%s" % (task, err, context))
        return self.read("fcx_sessdoc.results.get(%r)" % task)

    def doc_probe(self, what):
        """The DOCUMENT principal's view (a routed evaluation on Owner)."""
        return self.guest("fcx_sessdoc.doc_probe(%r)" % what)

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

    def second_document(self):
        self.other = FreeCAD.newDocument("SandboxSessDocB")
        self.other.addObject("Part::Box", "BoxB")
        self.other.recompute()
        FreeCAD.setActiveDocument(self.doc.Name)
        return self.other

    # ---- 1. the active document ---------------------------------------

    def test_a_active_document(self):
        got = self.activate("active")
        self.assertEqual(got["none"], False)
        self.assertEqual(got["name"], self.doc.Name)
        self.assertEqual(got["call"], self.doc.Name)
        self.assertEqual(got["gui"], self.doc.Name)
        self.assertEqual(got["gui_call"], self.doc.Name)
        self.assertTrue(got["same"])
        self.assertIsInstance(got["modified"], bool)
        self.assertIsNone(got["in_edit"])
        # the guest's own setActiveDocument: all three follow, and so
        # does the host
        other = self.second_document()
        self.param(other=other.Name)
        self.assertEqual(self.activate("switch"), [other.Name] * 3)
        self.assertEqual(FreeCAD.ActiveDocument.Name, other.Name)
        # nothing open: None on every form (only when this process has
        # no other document open)
        FreeCAD.closeDocument(other.Name)
        FreeCAD.closeDocument(self.doc.Name)
        self.settle()
        nothing_open = not FreeCAD.listDocuments()
        if nothing_open:
            self.activate("active", read=False)
        # the reads need a document for the probe's owner
        self.doc = FreeCAD.newDocument("SandboxSessDocA2")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        if nothing_open:
            got = self.outcome("active")
            self.assertEqual(got, {"none": True, "call": None, "gui": None, "gui_call": None})

    # ---- 2. writes --------------------------------------------------------

    def test_b_writes(self):
        got = self.activate("writes")
        name, label, volume, visible, vp_visible, gone, n, undo, count, roots = got
        self.assertEqual(name, "FcxObj")
        self.assertEqual(label, "written from the guest")
        self.assertAlmostEqual(volume, 6.0)
        self.assertFalse(visible)
        self.assertFalse(vp_visible)
        self.assertTrue(gone)
        self.assertIsInstance(n, int)
        self.assertIn("Fcx write", undo)
        self.assertGreaterEqual(count, 1)
        self.assertIn("FcxObj", roots)
        obj = self.doc.getObject("FcxObj")
        self.assertIsNotNone(obj)
        self.assertEqual(obj.Label, "written from the guest")
        self.assertFalse(obj.ViewObject.Visibility)
        self.assertIsNone(self.doc.getObject("FcxGone"))
        self.assertIn("Fcx write", self.doc.UndoNames)
        # one undo restores the document
        self.doc.undo()
        self.settle()
        self.assertIsNone(self.doc.getObject("FcxObj"))

    # ---- 3. a handle kept across activations --------------------------

    def test_c_kept_handle(self):
        self.param(keep="Box", label="relabeled after a clear")
        self.assertEqual(self.activate("keep"), [self.doc.Name, "Box"])
        # a routed expression evaluation between the activations clears
        # the handle table: the kept handles re-resolve by their keys
        self.assertEqual(self.S.evaluate(self.owner, "1 + 1", self.opts), 2)
        got = self.activate("reuse")
        self.assertEqual(got, [self.doc.Name, "Box", "relabeled after a clear", True,
                               self.doc.Name])
        self.assertEqual(self.box.Label, "relabeled after a clear")
        # the host activates ANOTHER document: the kept handle still
        # resolves and writes into its own (the ruled behaviour, native
        # parity), while ActiveDocument follows the host
        other = self.second_document()
        FreeCAD.setActiveDocument(other.Name)
        self.assertEqual(self.S.evaluate(self.owner, "1 + 1", self.opts), 2)
        self.param(label="relabeled while B is active")
        got = self.activate("reuse")
        self.assertEqual(got[:3], [self.doc.Name, "Box", "relabeled while B is active"])
        self.assertEqual(got[4], other.Name)
        self.assertEqual(self.box.Label, "relabeled while B is active")
        # the host closes the handle's document: a ReferenceError
        FreeCAD.closeDocument(self.doc.Name)
        self.settle()
        self.doc = FreeCAD.newDocument("SandboxSessDocA2")
        self.owner = self.doc.addObject("App::FeaturePython", "Owner")
        got = self.activate("reuse_closed")
        self.assertIn("ReferenceError", got)
        self.assertIn("SandboxSessDocA", got)

    # ---- 4. two documents -------------------------------------------------

    def test_d_two_documents(self):
        other = self.second_document()
        self.param(a=self.doc.Name, b=other.Name, b_obj="BoxB")
        before = len(FreeCAD.listDocuments())
        got = self.activate("two")
        self.assertEqual(got["names"], sorted([self.doc.Name, other.Name]
                                              + [n for n in FreeCAD.listDocuments()
                                                 if n not in (self.doc.Name, other.Name)]))
        self.assertEqual(got["active"], self.doc.Name)
        self.assertEqual(got["b_obj"], ["FcxInB", other.Name, True, other.Name])
        self.assertEqual(other.getObject("FcxInB").Label,
                         "from the guest while %s is active" % self.doc.Name)
        self.assertEqual(got["new"][:2], ["FcxNewDoc", True])
        self.assertEqual(got["new"][2], "FcxNewDoc")
        self.assertTrue(got["closed"])
        self.assertEqual(got["missing"], "NameError")
        self.assertEqual(len(FreeCAD.listDocuments()), before)
        self.assertEqual(FreeCAD.ActiveDocument.Name, self.doc.Name)
        # a DOCUMENT principal (Owner's evaluation in a) is refused
        # every one of the five: app.query is a prompt for a document
        # (a PermissionError until answered), app.write a denial that
        # no prompt offers
        for what in ("listDocuments", "getDocument", "getOwn"):
            self.assertIn("PermissionError", self.doc_probe(what), what)
        for what in ("newDocument", "closeDocument", "setActiveDocument"):
            got = self.doc_probe(what)
            self.assertIn("PermissionError", got, what)
            self.assertIn("app.write", got, what)
        self.assertEqual(len(FreeCAD.listDocuments()), before)
        self.assertEqual(FreeCAD.ActiveDocument.Name, self.doc.Name)
        principal = FreeCAD.ExpressionSecurity.principalOf(self.doc.Name)
        FreeCAD.ExpressionSecurity.clearPending(principal, "app.query")
        FreeCAD.ExpressionSecurity.clearPending(principal, "app.write")

    # ---- 5. a document principal keeps its own document -------------------

    def test_e_document_principal_regression(self):
        other = self.second_document()
        self.param(a=self.doc.Name, b=other.Name, b_obj="BoxB")
        FreeCAD.setActiveDocument(other.Name)
        # its OWN document is ActiveDocument while another is active
        self.assertEqual(self.doc_probe("active"), self.doc.Name)
        # with app.query granted, its reach is still its own document:
        # listDocuments names it alone, getDocument(b) is refused, and a
        # made-up key into b (the write path's precursor) is refused
        principal = FreeCAD.ExpressionSecurity.principalOf(self.doc.Name)
        FreeCAD.ExpressionSecurity.grant(principal, "app.query", "*", True, "session")
        try:
            self.assertEqual(self.doc_probe("listDocuments"), [self.doc.Name])
            self.assertEqual(self.doc_probe("getOwn"), self.doc.Name)
            got = self.doc_probe("getDocument")
            self.assertIn("PermissionError", got)
            self.assertIn(other.Name, got)
            got = self.doc_probe("resolve")
            self.assertIn("PermissionError", got)
            self.assertIn(other.Name, got)
        finally:
            FreeCAD.ExpressionSecurity.revoke(principal, "app.query")
        self.assertIsNone(other.getObject("FcxInB"))
        self.assertEqual(FreeCAD.ActiveDocument.Name, other.Name)

    # ---- 6. save and saveAs -----------------------------------------------

    def drive(self, task, action, timeout=5.0, repeat=1):
        """Run the guest's `task` (a modal op inside) while `action(dialog)`
        handles the modal on the host from a timer inside the nested
        loop; how it went, then the task's result."""
        from PySide import QtCore, QtWidgets

        outcome = []
        t0 = time.perf_counter()

        def poll():
            w = QtWidgets.QApplication.activeModalWidget()
            if w is None or not w.isVisible():
                if time.perf_counter() - t0 < timeout:
                    QtCore.QTimer.singleShot(30, poll)
                else:
                    outcome.append("no modal")
                return
            try:
                action(w)
                outcome.append("ok")
            except Exception as e:
                outcome.append(repr(e))
                w.reject()
            if len(outcome) < repeat:
                QtCore.QTimer.singleShot(30, poll)

        QtCore.QTimer.singleShot(30, poll)
        self.activate(task, read=False)
        sys.stderr.write("SandboxSessionDoc: %s %.3f s\n" % (task, time.perf_counter() - t0))
        got = self.outcome(task, " (dialog drive: %s)" % outcome)
        return outcome, got

    def test_f_save(self):
        from PySide import QtWidgets

        path = os.path.join(self.tmp, "a.FCStd")
        self.doc.saveAs(path)
        old = time.time() - 100
        os.utime(path, (old, old))
        # save(): the document's own file, the mtime moves
        self.assertEqual(self.activate("save"), path)
        self.assertGreater(os.path.getmtime(path), old + 50)
        # saveAs to a path the guest's getSaveFileName returned: written
        picked = os.path.join(self.tmp, "picked.FCStd")
        self.param(dir=self.tmp)

        def name_it(w):
            self.assertIsInstance(w, QtWidgets.QFileDialog)
            self.assertEqual(w.acceptMode(), QtWidgets.QFileDialog.AcceptMode.AcceptSave)
            w.findChild(QtWidgets.QLineEdit, "fileNameEdit").setText(picked)
            w.accept()

        outcome, got = self.drive("saveas_picked", name_it)
        self.assertEqual(outcome, ["ok"])
        self.assertEqual(got, [picked, picked])
        self.assertTrue(os.path.isfile(picked))
        self.assertEqual(self.doc.FileName, picked)
        # saveAs to a path the guest made up: refused, nothing written
        madeup = os.path.join(self.tmp, "madeup.FCStd")
        self.param(madeup=madeup)
        got = self.activate("saveas_madeup")
        self.assertIn("PermissionError", got)
        self.assertIn("file dialog", got)
        self.assertFalse(os.path.exists(madeup))
        self.assertEqual(self.doc.FileName, picked)

    # ---- 7. the corpus: direct bodies, no doCommand, no view -------------

    def test_g_corpus(self):
        import FreeCADGui

        try:
            import Draft
        except Exception as e:
            self.skipTest("Draft: %s" % e)
        # (importing one command module imports the whole package's
        # commands: every one is recorded, three are registered)
        registered = self.read("fcx_sessdoc.load_corpus()")
        self.assertLessEqual({"BIM_EmptyTrash", "BIM_Trash", "Draft_Heal"}, set(registered))
        for name in ("Fcx_Draft_Heal", "Fcx_BIM_Trash", "Fcx_BIM_EmptyTrash"):
            self.assertIn(name, FreeCADGui.listCommands())

        # Draft_Heal on a selected wire: healed = copied (copyObject)
        # and the original removed, inside the command's own "Heal"
        # transaction
        points = [FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 0, 0), FreeCAD.Vector(10, 10, 0)]
        wire = Draft.make_wire(points)
        self.doc.recompute()
        self.settle()
        wire_name = wire.Name
        wires_before = [o.Name for o in self.doc.Objects if Draft.get_type(o) == "Wire"]
        FreeCADGui.Selection.addSelection(wire)
        undo_before = self.doc.UndoCount
        FreeCADGui.runCommand("Fcx_Draft_Heal")
        self.settle()
        self.doc.recompute()
        self.settle()
        self.assertIsNone(self.doc.getObject(wire_name))
        healed = [o for o in self.doc.Objects if Draft.get_type(o) == "Wire"]
        self.assertEqual(len(healed), len(wires_before))
        self.assertEqual([tuple(p) for p in healed[0].Points], [tuple(p) for p in points])
        self.assertGreater(self.doc.UndoCount, undo_before)
        self.assertIn("Heal", self.doc.UndoNames)

        # BIM_Trash on the selected box: in the Trash group and hidden;
        # BIM_EmptyTrash: gone
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(self.box)
        undo_before = self.doc.UndoCount
        FreeCADGui.runCommand("Fcx_BIM_Trash")
        self.settle()
        trash = self.doc.getObject("Trash")
        self.assertIsNotNone(trash)
        self.assertTrue(trash.isDerivedFrom("App::DocumentObjectGroup"))
        self.assertIn(self.box, trash.Group)
        self.assertFalse(self.box.ViewObject.Visibility)
        self.assertGreater(self.doc.UndoCount, undo_before)
        FreeCADGui.Selection.clearSelection()
        undo_before = self.doc.UndoCount
        FreeCADGui.runCommand("Fcx_BIM_EmptyTrash")
        self.settle()
        self.assertIsNone(self.doc.getObject("Box"))
        self.assertEqual(list(trash.Group), [])
        self.assertIn("Empty Trash", self.doc.UndoNames)
        self.assertGreater(self.doc.UndoCount, undo_before)

    # ---- 8. Gui.doCommand in the guest (S2) -----------------------------

    def python_console_text(self):
        """What the host's Python console shows (the macro manager's
        ScriptToPyConsole echo of every recorded line), or None without
        the console."""
        import FreeCADGui
        from PySide import QtWidgets

        console = FreeCADGui.getMainWindow().findChild(QtWidgets.QPlainTextEdit,
                                                       "Python console")
        return console.toPlainText() if console is not None else None

    @staticmethod
    def audit_lines():
        import json

        path = os.path.join(FreeCAD.getUserAppDataDir(), "security", "audit.log")
        if not os.path.exists(path):
            return []
        with open(path, "r", encoding="utf-8") as f:
            return [json.loads(line) for line in f if line.strip()]

    def test_h_docommand(self):
        import hashlib

        marker_src = "fcx_marker = FreeCAD.ActiveDocument.Name  # %s" % self.doc.Name
        self.param(marker_src=marker_src)
        text_before = self.python_console_text()
        got = self.activate("docommand")
        # the source ran in the guest's __main__, with the console's
        # four names bound, as the session principal in the session
        # document; addModule imported there; a Gui line the same way
        self.assertEqual(got["marker"], self.doc.Name)
        self.assertTrue(got["part"])
        self.assertEqual(got["bound"], ["FreeCAD", "App", "FreeCADGui", "Gui"])
        self.assertEqual(got["obj"], "FcxDoCmd")
        self.assertFalse(got["vis"])
        self.assertEqual(got["volume"], 1.0)
        self.assertIsNone(got["returns"])
        self.assertEqual(got["bad"], "SyntaxError")
        self.assertEqual(got["raised"], "KeyError")
        obj = self.doc.getObject("FcxDoCmd")
        self.assertIsNotNone(obj)
        self.assertFalse(obj.ViewObject.Visibility)
        # nothing ran on the host: its __main__ has no such names
        import __main__

        self.assertFalse(hasattr(__main__, "fcx_marker"))
        self.assertFalse(hasattr(__main__, "_fcx_o"))
        # the macro recorder lines (the console's echo of them), the
        # addModule line once
        text = self.python_console_text()
        if text is not None:
            # (the slice starts after the standing prompt: no ">>> " on
            # its first line)
            text = text[len(text_before):]
            self.assertIn(marker_src + "\n", text)
            self.assertIn("_fcx_o.ViewObject.Visibility = False\n", text)
            self.assertEqual(text.count("import Part\n"), 1, text)
        # the audit line: the source's sha256, allowed, the session's
        sha = hashlib.sha256(marker_src.encode("utf-8")).hexdigest()
        lines = [l for l in self.audit_lines() if l.get("permission") == "gui.doCommand"
                 and l.get("target") == sha]
        self.assertTrue(lines, "no audit line for the doCommand source")
        self.assertEqual(lines[-1]["decision"], "allow")
        self.assertEqual(lines[-1]["principal"], "session")
        self.assertEqual(lines[-1].get("context"), "app:%d" % len(marker_src))
        # a DOCUMENT principal: refused, not promptable, nothing ran
        for what in ("docommand", "addModule"):
            got = self.doc_probe(what)
            self.assertIn("PermissionError", got, what)
            self.assertIn("gui.doCommand", got, what)
        self.assertFalse(self.doc_probe("docommand_ran"))
        pending = [p for p in FreeCAD.ExpressionSecurity.pending()
                   if p.get("permission") == "gui.doCommand"]
        self.assertEqual(pending, [])
        denied = [l for l in self.audit_lines() if l.get("permission") == "gui.doCommand"
                  and l.get("decision") == "deny"]
        self.assertTrue(denied, "no audit line for the refused document call")
        self.assertTrue(denied[-1]["principal"].startswith("document:sha256:"))

    # ---- 9. the corpus through doCommand: Draft_Upgrade, Arch_Site --------

    def test_i_corpus_docommand(self):
        import FreeCADGui

        try:
            import Draft
            import Draft_rc  # noqa: F401  (the icons DraftGui names)
        except Exception as e:
            self.skipTest("Draft: %s" % e)
        registered = self.read("fcx_sessdoc.load_corpus_docommand()")
        self.assertLessEqual({"Draft_Upgrade", "Arch_Site"}, set(registered))
        for name in ("Fcx_Draft_Upgrade", "Fcx_Arch_Site"):
            self.assertIn(name, FreeCADGui.listCommands())

        # Draft_Upgrade on two preselected lines sharing a point: proceed()
        # runs inline, finish() queues the commit, the shim's timer queue
        # drains when Activated returns -- Draft.upgrade wires them into
        # one Part::Feature inside the "Upgrade" transaction, and the
        # recompute line runs after the commit (delayAfter)
        a = Draft.make_line(FreeCAD.Vector(0, 0, 0), FreeCAD.Vector(10, 0, 0))
        b = Draft.make_line(FreeCAD.Vector(10, 0, 0), FreeCAD.Vector(10, 10, 0))
        self.doc.recompute()
        self.settle()
        names = {a.Name, b.Name}
        before = {o.Name for o in self.doc.Objects}
        FreeCADGui.Selection.clearSelection()
        FreeCADGui.Selection.addSelection(a)
        FreeCADGui.Selection.addSelection(b)
        undo_before = self.doc.UndoCount
        text_before = self.python_console_text()
        FreeCADGui.runCommand("Fcx_Draft_Upgrade")
        self.settle()
        self.doc.recompute()
        self.settle()
        after = {o.Name for o in self.doc.Objects}
        self.assertEqual(names & after, set(), "the upgraded lines were not deleted")
        new = after - before
        self.assertEqual(len(new), 1, new)
        wire = self.doc.getObject(new.pop())
        self.assertEqual(wire.TypeId, "Part::Feature")
        self.assertEqual(len(wire.Shape.Edges), 2)
        self.assertEqual(len(wire.Shape.Vertexes), 3)
        self.assertGreater(self.doc.UndoCount, undo_before)
        self.assertIn("Upgrade", self.doc.UndoNames)
        text = self.python_console_text()
        if text is not None:
            text = text[len(text_before):]
            self.assertIn("Draft.upgrade(FreeCADGui.Selection.getSelection(), delete=True)", text)
            self.assertIn("FreeCAD.ActiveDocument.recompute()\n", text)

        # Arch_Site: addModule Arch and Draft, obj = Arch.makeSite() and
        # Draft.autogroup(obj) through doCommand, inside "Create Site"
        FreeCADGui.Selection.clearSelection()
        text_before = self.python_console_text()
        probe = self.activate("makesite")
        self.assertEqual(probe, ["ok", True], probe)
        before = {o.Name for o in self.doc.Objects}
        undo_before = self.doc.UndoCount
        FreeCADGui.runCommand("Fcx_Arch_Site")
        self.settle()
        self.doc.recompute()
        self.settle()
        new = {o.Name for o in self.doc.Objects} - before
        sites = [o for o in (self.doc.getObject(n) for n in new) if Draft.get_type(o) == "Site"]
        self.assertEqual(len(sites), 1, new)
        self.assertEqual(sites[0].IfcType, "Site")
        self.assertGreater(self.doc.UndoCount, undo_before)
        self.assertIn("Create Site", self.doc.UndoNames)
        # the guest's __main__ holds the command's obj; the host's does not
        keys = self.read("[sorted(k for k in fcx_sessdoc._main() if not k.startswith('__')),"
                         " fcx_sessdoc._main() is __import__('freecad.widgets.gui',"
                         " fromlist=['x'])._main_dict()]")
        self.assertIn("obj", keys[0], keys)
        self.assertEqual(self.read("fcx_sessdoc._main()['obj'].Name"), sites[0].Name)
        import __main__

        self.assertFalse(hasattr(__main__, "obj") and getattr(__main__.obj, "Name", None)
                         == sites[0].Name)
        if text is not None:
            # (the probe's addModule recorded `import Arch`; the command's
            # is the guest's second, deduped as Command::addModule dedupes)
            text = self.python_console_text()[len(text_before):]
            self.assertEqual(text.count("import Arch\n"), 1, text)
            self.assertEqual(text.count("obj = Arch.makeSite()\n"), 2, text)
            self.assertIn("Draft.autogroup(obj)\n", text)
