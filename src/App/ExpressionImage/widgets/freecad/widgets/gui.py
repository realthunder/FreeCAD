# SPDX-License-Identifier: LGPL-2.1-or-later
"""What `FreeCADGui` carries for the forms in the sandbox guest:
`Control` (the task panel), `UiLoader` and `PySideUic` (the .ui loader
and `createWidget`), `getMainWindow()` as a shim, `runCommand`.
Reached through the guest FreeCADGui module's `__getattr__` (`attr`
below), so nothing here loads before a form is made; the models load
on first use (docs/Sandbox.md 7.11)."""


def attr(name):
    """`FreeCADGui.<name>` for a name the prelude does not define: the
    forms' names, else the AttributeError `hasattr` expects."""
    if name in ("Control", "PySideUic", "UiLoader", "getMainWindow", "runCommand",
                "InputHint", "HintManager", "_run_initgui"):
        return globals()[name]
    if name == "ActiveDocument":
        return active_document()
    if name == "UserInput":
        return user_input()
    raise AttributeError("FreeCADGui.%s is not in the sandbox (docs/Sandbox.md 7)" % name)


class _PySideUic:
    """`FreeCADGui.PySideUic`."""

    @staticmethod
    def loadUi(path, base=None):
        from . import uic

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
        """The model of that class; None for a class the subset lacks,
        as this fork's UiLoader answers (DraftGui falls back on it)."""
        from . import models

        if str(class_name) not in models.CLASSES:
            return None
        w = models.make(str(class_name), parent)
        if name:
            w.setObjectName(name)
        return w

    def load(self, path, parent=None):
        from . import uic

        form = uic.loadUi(path)
        if parent is not None:
            form.setParent(parent)
        return form

    def availableWidgets(self):
        from . import models

        return sorted(models.CLASSES)


class _Control:
    """`FreeCADGui.Control`: the task panel.  `showDialog(panel)`
    registers the panel as a guest proxy (its hooks are what the host's
    TaskDialogPython reads) and shows its `form` models on the host."""

    def showDialog(self, panel):
        import FreeCADGui
        from . import models

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
        """Each watcher registers as a guest proxy: the host's
        TaskWatcherPython reads its `title`, `icon`, `commands`,
        `filter` through it and calls `shouldShow` on every selection
        change (docs/Sandbox.md 7.11, G3c)."""
        import _fcx
        import FreeCADGui

        descs = [FreeCADGui._proxy_register(w, WATCHER_HOOKS) for w in watchers]
        _fcx.op("gui.control.add_watcher", 0, descs)

    def showTaskView(self):
        pass

    def showModelView(self):
        pass


Control = _Control()

PANEL_HOOKS = ("accept", "reject", "clicked", "open", "getStandardButtons",
               "modifyStandardButtons", "needsFullSpace", "isAllowedAlterDocument",
               "isAllowedAlterView", "isAllowedAlterSelection", "helpRequested", "shouldShow")
WATCHER_HOOKS = ("shouldShow",)
MAIN_WINDOW_HOOKS = ("mainWindowClosed",)


def runCommand(name, index=0):
    """`FreeCADGui.runCommand(name[, index])`: the host's command by
    name (a guest-registered one crosses back to its `Activated`)."""
    import _fcx

    _fcx.op("gui.cmd.run", 0, [str(name), int(index)])


class _MainWindowSignal:
    """`getMainWindow().mainWindowClosed`: `connect(slot)` registers the
    slots' holder as a guest proxy the host calls when it closes."""

    def __init__(self, owner):
        self._owner = owner
        self._slots = []

    def connect(self, slot):
        import _fcx
        import FreeCADGui

        self._slots.append(slot)
        if len(self._slots) == 1:
            desc = FreeCADGui._proxy_register(self._owner, MAIN_WINDOW_HOOKS)
            _fcx.op("gui.mainwindow", 0, ["watch", desc])

    def disconnect(self, slot=None):
        self._slots = [] if slot is None else [s for s in self._slots if s != slot]

    def emit(self):
        for slot in list(self._slots):
            slot()

    # the host calls the hook by the signal's name on its holder: the
    # signal object itself answers, running the slots
    __call__ = emit


class _StatusBar:
    def showMessage(self, text, timeout=0):
        import _fcx

        _fcx.op("gui.mainwindow", 0, ["showMessage", str(text), int(timeout)])

    def clearMessage(self):
        self.showMessage("", 0)


# ---- input hints (docs/Sandbox.md 7.9, G2b): data ----

_user_input = None


def user_input():
    """`FreeCADGui.UserInput`: the host's IntEnum mirrored by value on
    first use (one op), so a hint built here crosses as the numbers
    the host's `showHint` reads."""
    global _user_input
    if _user_input is None:
        import enum

        import _fcx

        members = _fcx.op("gui.user_input", 0)
        _user_input = enum.IntEnum("UserInput", {str(k): int(v) for k, v in members.items()})
        _user_input.__module__ = "FreeCADGui"
    return _user_input


class InputHint:
    """`FreeCADGui.InputHint(message, *sequences)`, as FreeCADGuiInit.py
    defines it natively: a message with %1, %2 placeholders and one
    input (a UserInput) or a tuple of them per placeholder."""

    def __init__(self, message, *sequences):
        self.message = message
        self.sequences = list(sequences)

    def _fcx_wire(self):
        seqs = []
        for seq in self.sequences:
            if isinstance(seq, (tuple, list)):
                seqs.append([int(v) for v in seq])
            else:
                seqs.append(int(seq))
        return [str(self.message), seqs]


class _HintManager:
    """`FreeCADGui.HintManager`: the hints cross as data to the host's
    main window (`gui.mainwindow showHint`)."""

    def show(self, *hints):
        getMainWindow().showHint(*hints)

    def hide(self):
        getMainWindow().hideHint()


HintManager = _HintManager()


class MainWindow:
    """`FreeCADGui.getMainWindow()` in the guest: a shim over the host's
    main window -- `addToolBar(bar)` realizes a tool bar model there,
    `mainWindowClosed` crosses as a hook, messages, hints and the
    cursor are data.  The document windows (`getActiveWindow`,
    `getWindows`) are not here: they are the mirror's (docs/Sandbox.md
    7.2, G4); `getWindowsOfType` answers none for the same reason."""

    _fcx_mainwindow = True

    def __init__(self):
        self.mainWindowClosed = _MainWindowSignal(self)
        self._status = _StatusBar()

    def addToolBar(self, *args):
        import _fcx

        bar = args[-1]
        _fcx.op("gui.mainwindow", 0, ["addToolBar", bar.model_id])
        bar._attach(None)

    def removeToolBar(self, bar):
        import _fcx

        _fcx.op("gui.mainwindow", 0, ["removeToolBar", bar.model_id])

    def insertToolBar(self, before, bar):
        self.addToolBar(bar)

    def showMessage(self, text, timeout=0):
        self._status.showMessage(text, timeout)

    def statusBar(self):
        return self._status

    def windowTitle(self):
        import _fcx

        return _fcx.op("gui.mainwindow", 0, ["windowTitle"])

    def cursor(self):
        from .qtdata import QCursor

        return QCursor()

    def isVisible(self):
        return True

    def showHint(self, *hints):
        import _fcx

        _fcx.op("gui.mainwindow", 0, ["showHint", [h._fcx_wire() for h in hints]])

    def hideHint(self):
        import _fcx

        _fcx.op("gui.mainwindow", 0, ["hideHint"])

    def getWindowsOfType(self, type_id):
        """No document window lives in the guest (G4): the empty list,
        which is what the snapper's grid walk expects of a session
        without a 3D view."""
        return []

    def setActiveWindow(self, *args):
        raise AttributeError("getMainWindow().setActiveWindow is not in the sandbox yet (G4)")

    def getActiveWindow(self):
        raise AttributeError("getMainWindow().getActiveWindow is not in the sandbox yet (G4)")

    def getWindows(self):
        raise AttributeError("getMainWindow().getWindows is not in the sandbox yet (G4)")

    def findChild(self, *args):
        return None

    def findChildren(self, *args):
        return []

    def __repr__(self):
        return "<sandbox main window>"


_main_window = MainWindow()


def getMainWindow():
    return _main_window


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


# ---- the InitGui runner (docs/Sandbox.md 7.9, G2b) ----

_GROUPS = {}


def _run_initgui(source, path, name, tops):
    """Run a workbench's `InitGui.py` here, the way the host's
    `FreeCADGuiInit.RunInitGuiPy` does natively: the file's text exec'd
    in a scope of its own with `FreeCAD`, `App`, `Gui`, `FreeCADGui`,
    `Workbench`, `Log`, `Err`, `Msg` as globals.  `name` is the module
    (Draft, BIM) and `tops` the top-level packages its wheel carries:
    a command whose class lives in one of them registers under that
    module as its group, as the host derives it from the caller's
    `Mod/<Group>/` path natively.  A failure is the caller's to log."""
    import FreeCAD
    import FreeCADGui

    for top in tops:
        _GROUPS[str(top)] = str(name)
    if not isinstance(getattr(FreeCAD, "__unit_test__", None), list):
        FreeCAD.__unit_test__ = []
    _install_group_rule()
    ns = {
        "__name__": "InitGui",
        "__file__": path,
        "__builtins__": __builtins__,
        "FreeCAD": FreeCAD,
        "App": FreeCAD,
        "Gui": FreeCADGui,
        "FreeCADGui": FreeCADGui,
        "Workbench": FreeCADGui.Workbench,
        "Log": FreeCAD.Console.PrintLog,
        "Err": FreeCAD.Console.PrintError,
        "Msg": FreeCAD.Console.PrintMessage,
    }
    FreeCADGui._fcx_group = str(name)
    try:
        exec(compile(source, path, "exec"), ns)
    finally:
        FreeCADGui._fcx_group = None


def _install_group_rule():
    """`FreeCADGui.addCommand` deriving a command's group from the
    wheel its class came from, once: the prelude's addCommand reads
    `FreeCADGui._fcx_group` at the call, so this wrapper sets it for
    the call from the class's top-level package."""
    import FreeCADGui

    original = FreeCADGui.addCommand
    if getattr(original, "_fcx_grouped", False):
        return

    def addCommand(name, obj, activation=None):
        top = type(obj).__module__.split(".")[0]
        group = _GROUPS.get(top)
        if group is None:
            return original(name, obj, activation)
        prior = getattr(FreeCADGui, "_fcx_group", None)
        FreeCADGui._fcx_group = group
        try:
            return original(name, obj, activation)
        finally:
            FreeCADGui._fcx_group = prior

    addCommand._fcx_grouped = True
    FreeCADGui.addCommand = addCommand
