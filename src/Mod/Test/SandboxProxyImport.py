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
"""The native Proxy import restriction on a VIEW PROVIDER's Proxy
(docs/Sandbox.md sec 11 item 1, sec 13).  A document's GuiDocument.xml
names the module of every view provider Proxy, and the Gui side
restores it natively -- PropertyPythonObject::Restore, the same code as
a document object's Proxy, with a Gui::ViewProvider as the container.
The rule is Base::Type::importModule's: a module already loaded, or one
resolving into a registered Mod root; anything else is refused before
importing and the view provider is left without a Proxy.  The C++ suite
(ProxyImport.*) covers the rule on both container kinds without the
GUI; this case opens a real saved document whose view provider names a
module outside every root, and one whose module lives under the user's
Mod directory.
Needs the GUI: run it through scripts/sandbox-gui-gate.py under Xvfb.
Does not need the sandbox guest; the restore is native (routing OFF)."""

import importlib
import os
import shutil
import sys
import tempfile
import unittest

import FreeCAD

PARAMS = "User parameter:BaseApp/Preferences/Expression/Sandbox"

OK_MODULE = "fcxproxyimport_gui_ok"
OUTSIDE_MODULE = "fcxproxyimport_gui_outside"

SOURCE = '''
class Obj:
    def __init__(self, obj):
        obj.Proxy = self

    def execute(self, obj):
        pass


class VP:
    def __init__(self, vobj):
        vobj.Proxy = self

    def attach(self, vobj):
        pass
'''


class SandboxProxyImportTest(unittest.TestCase):
    def setUp(self):
        if not FreeCAD.GuiUp:
            self.skipTest("needs the GUI")
        grp = FreeCAD.ParamGet(PARAMS)
        self._routing = grp.GetBool("Evaluate", False)
        grp.SetBool("Evaluate", False)
        # a module under the user's Mod directory (a registered root) and
        # one in a temporary directory outside every root, both on sys.path
        self.userMod = os.path.join(FreeCAD.getUserAppDataDir(), "Mod", "FcxProxyImport")
        os.makedirs(self.userMod, exist_ok=True)
        self.tmp = tempfile.mkdtemp(prefix="fcx-proxyimport-")
        with open(os.path.join(self.userMod, OK_MODULE + ".py"), "w") as f:
            f.write(SOURCE)
        with open(os.path.join(self.tmp, OUTSIDE_MODULE + ".py"), "w") as f:
            f.write(SOURCE)
        sys.path.insert(0, self.userMod)
        sys.path.insert(0, self.tmp)
        importlib.invalidate_caches()
        self.docs = []

    def tearDown(self):
        for name in self.docs:
            try:
                FreeCAD.closeDocument(name)
            except Exception:
                pass
        for name in (OK_MODULE, OUTSIDE_MODULE):
            sys.modules.pop(name, None)
        for p in (self.userMod, self.tmp):
            if p in sys.path:
                sys.path.remove(p)
        shutil.rmtree(self.userMod, ignore_errors=True)
        shutil.rmtree(self.tmp, ignore_errors=True)
        FreeCAD.ParamGet(PARAMS).SetBool("Evaluate", self._routing)

    def saveWith(self, appModule, guiModule):
        """A document with one FeaturePython whose Proxy is appModule.Obj and
        whose view provider Proxy is guiModule.VP, saved and closed; both
        modules dropped from sys.modules so the reopen has to import."""
        app = importlib.import_module(appModule)
        gui = importlib.import_module(guiModule)
        doc = FreeCAD.newDocument("FcxProxyImportSave")
        obj = doc.addObject("App::FeaturePython", "Owner")
        app.Obj(obj)
        gui.VP(obj.ViewObject)
        doc.recompute()
        path = os.path.join(self.tmp, "proxyimport.FCStd")
        doc.saveAs(path)
        FreeCAD.closeDocument(doc.Name)
        for name in (appModule, guiModule):
            sys.modules.pop(name, None)
        return path

    def reopen(self, path):
        """Open the file and wait for its Gui side: this fork restores a
        document's view providers off the event loop, after openDocument
        returns (docs/DocumentLoad.md), so the object has no ViewObject
        until the loop has run."""
        from PySide import QtCore, QtWidgets

        doc = FreeCAD.openDocument(path)
        self.docs.append(doc.Name)
        obj = doc.getObject("Owner")
        for _ in range(250):
            QtWidgets.QApplication.processEvents()
            if obj.ViewObject is not None and QtWidgets.QApplication.overrideCursor() is None:
                break
            loop = QtCore.QEventLoop()
            QtCore.QTimer.singleShot(20, loop.quit)
            loop.exec()
        self.assertIsNotNone(obj.ViewObject, "the Gui side of the open never arrived")
        return obj

    def test_viewProviderOutsideEveryRootIsRefused(self):
        path = self.saveWith(OK_MODULE, OUTSIDE_MODULE)
        self.assertNotIn(OUTSIDE_MODULE, sys.modules)
        obj = self.reopen(path)
        # the document object's Proxy, under the user's Mod: restored
        self.assertIsNotNone(obj.Proxy)
        self.assertEqual(type(obj.Proxy).__module__, OK_MODULE)
        self.assertIn(OK_MODULE, sys.modules)
        # the view provider's Proxy, outside every root: refused, and the
        # module never imported.  Without a Proxy the Gui side reads 1:
        # ViewProviderFeaturePythonImp::finishRestoring's stock sentinel
        # for a view provider restored with none, as after any failed
        # import natively.
        self.assertIn(obj.ViewObject.Proxy, (None, 1))
        self.assertNotIn(OUTSIDE_MODULE, sys.modules)

    def test_viewProviderUnderTheUserModRestores(self):
        path = self.saveWith(OK_MODULE, OK_MODULE)
        self.assertNotIn(OK_MODULE, sys.modules)
        obj = self.reopen(path)
        self.assertIsNotNone(obj.Proxy)
        self.assertIsNotNone(obj.ViewObject.Proxy)
        self.assertEqual(type(obj.ViewObject.Proxy).__module__, OK_MODULE)
