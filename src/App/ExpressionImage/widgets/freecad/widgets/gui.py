# SPDX-License-Identifier: LGPL-2.1-or-later
"""What `FreeCADGui` carries for the forms in the sandbox guest:
`Control` (the task panel), `UiLoader` and `PySideUic` (the .ui loader
and `createWidget`).  Reached through the guest FreeCADGui module's
`__getattr__`, so nothing here loads before a form is made
(docs/Sandbox.md 7.11)."""

from . import models
from . import uic


class _PySideUic:
    """`FreeCADGui.PySideUic`."""

    @staticmethod
    def loadUi(path, base=None):
        return uic.loadUi(path, base)

    @staticmethod
    def loadUiType(path):
        raise TypeError("PySideUic.loadUiType is not in the sandbox's subset: use loadUi")

    @staticmethod
    def createCustomWidget(*args):
        raise TypeError("PySideUic.createCustomWidget is not in the sandbox's subset")


PySideUic = _PySideUic()


class UiLoader:
    """`FreeCADGui.UiLoader()`: `createWidget(className)` is the model of
    that Qt class, the host makes the real widget of it; `load(path)`
    is `loadUi`."""

    def createWidget(self, class_name, parent=None, name=""):
        w = models.make(str(class_name), parent)
        if name:
            w.setObjectName(name)
        return w

    def load(self, path, parent=None):
        form = uic.loadUi(path)
        if parent is not None:
            form.setParent(parent)
        return form

    def availableWidgets(self):
        return sorted(models.CLASSES)


class _Control:
    """`FreeCADGui.Control`: the task panel.  `showDialog(panel)`
    registers the panel as a guest proxy (its hooks are what the host's
    TaskDialogPython reads) and shows its `form` models on the host."""

    def showDialog(self, panel):
        import FreeCADGui

        form = getattr(panel, "form", None)
        forms = form if isinstance(form, (list, tuple)) else [form]
        ids = []
        for f in forms:
            if not isinstance(f, models.QWidget):
                raise TypeError("Control.showDialog: the panel's form must be a widget made in"
                                " the sandbox, not %r" % (f,))
            ids.append(f.model_id)
        FreeCADGui._show_panel(panel, ids)

    def closeDialog(self):
        import _fcx

        _fcx.op("gui.control.close", 0)

    def activeDialog(self):
        import _fcx

        return _fcx.op("gui.control.active", 0)

    def isAllowedAlterDocument(self):
        import _fcx

        return _fcx.op("gui.control.query", 0, "isAllowedAlterDocument")

    def isAllowedAlterView(self):
        import _fcx

        return _fcx.op("gui.control.query", 0, "isAllowedAlterView")

    def isAllowedAlterSelection(self):
        import _fcx

        return _fcx.op("gui.control.query", 0, "isAllowedAlterSelection")

    def clearTaskWatcher(self):
        import _fcx

        _fcx.op("gui.control.clear_watcher", 0)

    def closeDialogIfActive(self):
        import _fcx

        if _fcx.op("gui.control.active", 0):
            _fcx.op("gui.control.close", 0)

    def addTaskWatcher(self, watchers):
        raise TypeError("Control.addTaskWatcher is not in the sandbox's subset yet (G3c)")

    def showTaskView(self):
        pass

    def showModelView(self):
        pass


Control = _Control()

PANEL_HOOKS = ("accept", "reject", "clicked", "open", "getStandardButtons",
               "modifyStandardButtons", "needsFullSpace", "isAllowedAlterDocument",
               "isAllowedAlterView", "isAllowedAlterSelection", "helpRequested", "shouldShow")


class GuiDocument:
    """`FreeCADGui.ActiveDocument` in the guest: what a task panel's
    finish() needs (`resetEdit`, `Document`); the rest of the GUI
    document is U4 (docs/Sandbox.md 7.1)."""

    def resetEdit(self):
        import _fcx

        return _fcx.op("gui.control.query", 0, "resetEdit")

    @property
    def Document(self):
        import FreeCAD

        return FreeCAD.ActiveDocument

    def setEdit(self, *args):
        raise AttributeError("FreeCADGui.ActiveDocument.setEdit is not in the sandbox yet (U4)")

    def getObject(self, name):
        raise AttributeError("FreeCADGui.ActiveDocument.getObject is not in the sandbox yet"
                             " (U4: view providers)")

    def __repr__(self):
        return "<sandbox GUI document>"


def active_document():
    """`FreeCADGui.ActiveDocument`: a GuiDocument while the host has an
    active document, else None (as natively)."""
    import _fcx

    if _fcx.op("gui.control.query", 0, "activeDocument"):
        return GuiDocument()
    return None
