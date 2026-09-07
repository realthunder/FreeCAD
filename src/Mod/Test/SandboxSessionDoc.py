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
keeps its own document, always.
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


TASKS = {
    "active": t_active, "switch": t_switch, "writes": t_writes, "keep": t_keep,
    "reuse": t_reuse, "reuse_closed": t_reuse_closed, "two": t_two, "save": t_save,
    "saveas_picked": t_saveas_picked, "saveas_madeup": t_saveas_madeup,
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
