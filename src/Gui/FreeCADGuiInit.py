#***************************************************************************
#*   Copyright (c) 2002,2003 Jürgen Riegel <juergen.riegel@web.de>         *
#*                                                                         *
#*   This file is part of the FreeCAD CAx development system.              *
#*                                                                         *
#*   This program is free software; you can redistribute it and/or modify  *
#*   it under the terms of the GNU Lesser General Public License (LGPL)    *
#*   as published by the Free Software Foundation; either version 2 of     *
#*   the License, or (at your option) any later version.                   *
#*   for detail see the LICENCE text file.                                 *
#*                                                                         *
#*   FreeCAD is distributed in the hope that it will be useful,            *
#*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
#*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
#*   GNU Lesser General Public License for more details.                   *
#*                                                                         *
#*   You should have received a copy of the GNU Library General Public     *
#*   License along with FreeCAD; if not, write to the Free Software        *
#*   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
#*   USA                                                                   *
#*                                                                         *
#***************************************************************************/

# FreeCAD gui init module
#
# Gathering all the information to start FreeCAD
# This is the second one of three init scripts, the third one
# runs when the gui is up

# imports the one and only
import FreeCAD, FreeCADGui
from enum import IntEnum

# shortcuts
Gui = FreeCADGui

# this is to keep old code working
Gui.listCommands = Gui.Command.listAll
Gui.isCommandActive = lambda cmd: Gui.Command.get(cmd).isActive()

# The values must match with that of the C++ enum class ResolveMode
class ResolveMode(IntEnum):
    NoResolve = 0
    OldStyleElement = 1
    NewStyleElement = 2
    FollowLink = 3

Gui.Selection.ResolveMode = ResolveMode

# The values must match with that of the C++ enum class SelectionStyle
class SelectionStyle(IntEnum):
    NormalSelection = 0
    GreedySelection = 1

Gui.Selection.SelectionStyle = SelectionStyle

# Gui.InputHint / Gui.HintManager
#
# Gui.UserInput itself is registered from C++ by
# registerUserInputEnumInPython(), see src/Gui/InputHintPy.cpp. These two are
# Python upstream as well, so they are taken from upstream verbatim.


class InputHint:
    """
    Represents a single input hint (shortcut suggestion).

    The message is a Qt formatting string with placeholders like %1, %2, ...
    The placeholders are replaced with input representations - be it keys, mouse buttons etc.
    Each placeholder corresponds to one input sequence. Sequence can either be:
     - one input from Gui.UserInput enum
     - tuple of mentioned enum values representing the input sequence

    >>> InputHint("%1 change mode", Gui.UserInput.KeyM)
    will result in a hint displaying `[M] change mode`

    >>> InputHint("%1 new line", (Gui.UserInput.KeyControl, Gui.UserInput.KeyEnter))
    will result in a hint displaying `[ctrl][enter] new line`

    >>> InputHint("%1/%2 increase/decrease ...", Gui.UserInput.KeyU, Gui.UserInput.KeyJ)
    will result in a hint displaying `[U]/[J] increase / decrease ...`
    """

    def __init__(self, message, *sequences):
        self.message = message
        self.sequences = list(sequences)


class HintManager:
    """
    A convenience class for managing input hints (shortcut suggestions) displayed to the user.
    It is here mostly to provide well-defined and easy to reach API from python without developers
    needing to call low-level functions on the main window directly.
    """

    def show(self, *hints):
        """
        Displays the specified input hints to the user.

        :param hints: List of hints to show.
        """
        Gui.getMainWindow().showHint(*hints)

    def hide(self):
        """
        Hides all currently displayed input hints.
        """
        Gui.getMainWindow().hideHint()


Gui.InputHint = InputHint
Gui.HintManager = HintManager()


# Important definitions
class Workbench:
    """The workbench base class."""
    MenuText = ""
    ToolTip = ""
    Icon = None

    def Initialize(self):
        """Initializes this workbench."""
        App.Console.PrintWarning(str(self) + ": Workbench.Initialize() not implemented in subclass!")
    def ContextMenu(self, recipient):
        pass
    def appendToolbar(self,name,cmds):
        self.__Workbench__.appendToolbar(name, cmds)
    def removeToolbar(self,name):
        self.__Workbench__.removeToolbar(name)
    def listToolbars(self):
        return self.__Workbench__.listToolbars()
    def getToolbarItems(self):
        return self.__Workbench__.getToolbarItems()
    def appendCommandbar(self,name,cmds):
        self.__Workbench__.appendCommandbar(name, cmds)
    def removeCommandbar(self,name):
        self.__Workbench__.removeCommandbar(name)
    def listCommandbars(self):
        return self.__Workbench__.listCommandbars()
    def appendMenu(self,name,cmds):
        self.__Workbench__.appendMenu(name, cmds)
    def removeMenu(self,name):
        self.__Workbench__.removeMenu(name)
    def listMenus(self):
        return self.__Workbench__.listMenus()
    def appendContextMenu(self,name,cmds):
        self.__Workbench__.appendContextMenu(name, cmds)
    def removeContextMenu(self,name):
        self.__Workbench__.removeContextMenu(name)
    def reloadActive(self):
        self.__Workbench__.reloadActive()
    def name(self):
        return self.__Workbench__.name()
    def GetClassName(self):
        """Return the name of the associated C++ class."""
        # as default use this to simplify writing workbenches in Python
        return "Gui::PythonWorkbench"


class StandardWorkbench ( Workbench ):
    """A workbench defines the tool bars, command bars, menus,
context menu and dockable windows of the main window.
    """
    def Initialize(self):
        """Initialize this workbench."""
        # load the module
        Log ('Init: Loading FreeCAD GUI\n')
    def GetClassName(self):
        """Return the name of the associated C++ class."""
        return "Gui::StdWorkbench"

class NoneWorkbench ( Workbench ):
    """An empty workbench."""
    MenuText = "<none>"
    ToolTip = "The default empty workbench"
    def Initialize(self):
        """Initialize this workbench."""
        # load the module
        Log ('Init: Loading FreeCAD GUI\n')
    def GetClassName(self):
        """Return the name of the associated C++ class."""
        return "Gui::NoneWorkbench"

# ---- the InitGui runner of the Python sandbox (docs/Sandbox.md 7.9, G2b) ----
#
# A module whose GUI side is bundled for the sandbox guest as a wheel
# (fcx_draft, fcx_bim: docs/Sandbox.md 5.6) runs its InitGui.py IN THE
# GUEST instead of natively when the preference asks for it: its
# workbench and commands then exist on the host as stand-ins (the G2a
# mechanism, src/Gui/SandboxGui.cpp), and every hook the host calls on
# them -- Initialize, IsActive, Activated -- crosses to the guest.  The
# module's compiled Qt resources (<Mod>/*_rc.py) are imported on the
# host first: a guest cannot register a qrc, and the icons the guest
# names have to exist here.  A guest reset drops every stand-in, so the
# runs are recorded and repeated after the next boot (_onGuestBoot,
# called by the host's boot listener).

_guestInitGui = []   # [Dir, module name, the boot it ran in]


def GuestInitGuiWanted(Dir):
    """Does this module's InitGui.py run in the sandbox guest?  With
    Expression/Sandbox:InitGuiInGuest on, a build with the sandbox
    host, and a bundled wheel named fcx_<module> (lower case)."""
    import os
    S = getattr(FreeCAD, "ExpressionSandbox", None)
    if S is None:
        return False
    params = FreeCAD.ParamGet("User parameter:BaseApp/Preferences/Expression/Sandbox")
    env = os.environ.get("FCX_INITGUI_IN_GUEST")   # the rig's switch (docs/Sandbox.md 10)
    if not (env == "1" if env in ("0", "1") else params.GetBool("InitGuiInGuest", False)):
        return False
    try:
        if not S.imageInfo()["host"]:
            return False
        return GuestWheelOf(Dir) is not None
    except Exception:
        return False


def GuestWheelOf(Dir):
    """The bundled wheel carrying this module's code for the guest, or None."""
    import os
    name = "fcx_" + os.path.basename(os.path.normpath(Dir)).lower()
    return FreeCAD.ExpressionSandbox.pyodideLayout().get("bundled", {}).get(name)


def RunInitGuiInGuest(Dir) -> bool:
    """Run Dir's InitGui.py in the sandbox guest (the guest's
    FreeCADGui._run_initgui: the file's text exec'd there with the same
    globals RunInitGuiPy gives it natively).  Logged, never fatal, as
    the native run is; True when it ran.  Reachable as
    FreeCADGui._runInitGuiInGuest for the gates."""
    import glob, importlib, os, traceback, zipfile
    S = FreeCAD.ExpressionSandbox
    InstallFile = os.path.join(Dir, "InitGui.py")
    name = os.path.basename(os.path.normpath(Dir))
    wheel = GuestWheelOf(Dir)
    if wheel is None:
        Err("Init: %s has no bundled sandbox wheel\n" % name)
        return False
    try:
        # the host's half: the module's compiled Qt resources (data);
        # its directory is on sys.path since Init.py ran
        for rc in sorted(glob.glob(os.path.join(Dir, "*_rc.py"))):
            importlib.import_module(os.path.basename(rc)[:-3])
        # the wheel's top-level names: the group its commands register under
        tops = set()
        with zipfile.ZipFile(wheel) as z:
            for entry in z.namelist():
                top = entry.split("/", 1)[0]
                if top.endswith(".dist-info") or top == "fcx_resources":
                    continue
                tops.add(top[:-3] if top.endswith(".py") else top)
        with open(InstallFile, "rt", encoding="utf-8") as f:
            source = f.read()
        S.setRouting(True)
        boot_before = S.bootCount()
        S.exec("import FreeCADGui\nFreeCADGui._run_initgui(%r, %r, %r, %r)\n"
               % (source, InstallFile, name, sorted(tops)))
    except Exception as inst:
        Log('Init:      Initializing ' + Dir + ' in the sandbox guest... failed\n')
        Log('-'*100+'\n')
        Log(traceback.format_exc())
        Log('-'*100+'\n')
        Err('During initialization in the sandbox guest the error "' + str(inst)[:2000]
            + '" occurred in ' + InstallFile + '\n')
        Err('Please look into the log file for further information\n')
        return False
    boot = max(boot_before, S.bootCount())
    for entry in _guestInitGui:
        if entry[0] == Dir:
            entry[2] = boot
            break
    else:
        _guestInitGui.append([Dir, name, boot])
    Log('Init:      Initializing ' + Dir + ' in the sandbox guest... done\n')
    return True


def _onGuestBoot(boot):
    """The host's boot listener (SandboxGui.cpp): a fresh guest has no
    stand-in of the previous one, so every InitGui.py run in an
    earlier guest runs again, replacing its commands and workbench."""
    try:
        active = FreeCADGui.activeWorkbench().name()
    except Exception:
        active = None
    rerun = [entry for entry in list(_guestInitGui) if entry[2] < boot]
    for entry in rerun:
        RunInitGuiInGuest(entry[0])
    # a replaced workbench is a new handler (the manager dropped the
    # old one, the active slot with it): the active one is activated
    # again so its Initialize runs on the new guest
    if rerun and active and active in FreeCADGui.listWorkbenches() \
            and hasattr(FreeCADGui.getWorkbench(active), "_standin"):
        FreeCADGui.activateWorkbench(active)


FreeCADGui._runInitGuiInGuest = RunInitGuiInGuest
FreeCADGui._guestInitGuiWanted = GuestInitGuiWanted
FreeCADGui._onGuestBoot = _onGuestBoot
FreeCADGui._guestInitGui = _guestInitGui


def InitApplications():
    import sys,os,traceback
    import io as cStringIO
  
    # Searching modules dirs +++++++++++++++++++++++++++++++++++++++++++++++++++
    # (additional module paths are already cached)
    ModDirs = FreeCAD.__ModDirs__
    #print ModDirs
    Log('Init:   Searching modules...\n')

    def RunInitGuiPy(Dir) -> bool:
        InstallFile = os.path.join(Dir,"InitGui.py")
        if os.path.exists(InstallFile) and GuestInitGuiWanted(Dir):
            return RunInitGuiInGuest(Dir)
        if os.path.exists(InstallFile):
            Gui._setExecFile(InstallFile)
            try:
                with open(InstallFile, 'rt', encoding='utf-8') as f:
                    exec(compile(f.read(), InstallFile, 'exec'))
            except Exception as inst:
                Log('Init:      Initializing ' + Dir + '... failed\n')
                Log('-'*100+'\n')
                Log(traceback.format_exc())
                Log('-'*100+'\n')
                Err('During initialization the error "' + str(inst) + '" occurred in '\
                    + InstallFile + '\n')
                Err('Please look into the log file for further information\n')
            else:
                Log('Init:      Initializing ' + Dir + '... done\n')
                return True
            finally:
                Gui._setExecFile()
        else:
            Log('Init:      Initializing ' + Dir + '(InitGui.py not found)... ignore\n')
        return False

    def processMetadataFile(Dir, MetadataFile):
        meta = FreeCAD.Metadata(MetadataFile)
        if not meta.supportsCurrentFreeCAD():
            return None
        content = meta.Content
        if "workbench" in content:
            FreeCAD.Gui.addIconPath(Dir)
            workbenches = content["workbench"]
            for workbench_metadata in workbenches:
                if not workbench_metadata.supportsCurrentFreeCAD():
                    return None
                subdirectory = workbench_metadata.Name\
                    if not workbench_metadata.Subdirectory\
                    else workbench_metadata.Subdirectory
                subdirectory = subdirectory.replace("/",os.path.sep)
                subdirectory = os.path.join(Dir, subdirectory)
                ran_init = RunInitGuiPy(subdirectory)

                if ran_init:
                    # Try to generate a new icon from the metadata-specified information
                    classname = workbench_metadata.Classname
                    if classname:
                        try:
                            wb_handle = FreeCAD.Gui.getWorkbench(classname)
                        except Exception:
                            Log(f"Failed to get handle to {classname} -- no icon\
                                can be generated,\n check classname in package.xml\n")
                        else:
                            GeneratePackageIcon(Dir, subdirectory, workbench_metadata,
                                                wb_handle)

    def tryProcessMetadataFile(Dir, MetadataFile):
        try:
            processMetadataFile(Dir, MetadataFile)
        except Exception as exc:
            Err(str(exc))

    def InitApplication(Dir):
        """Run the InitGui.py of one module directory -- or, for a packaged addon, that
        of every workbench its package.xml declares. Returns False if Dir was skipped."""
        if (Dir == '') | (Dir == 'CVS') | (Dir == '__init__.py'):
            return False
        stopFile = os.path.join(Dir, "ADDON_DISABLED")
        if os.path.exists(stopFile):
            Msg(f'NOTICE: Addon "{Dir}" disabled by presence of ADDON_DISABLED stopfile\n')
            return False
        MetadataFile = os.path.join(Dir, "package.xml")
        if os.path.exists(MetadataFile):
            tryProcessMetadataFile(Dir, MetadataFile)
        else:
            RunInitGuiPy(Dir)
        return True

    def InitNamespacePackages():
        """Import the init_gui module of every freecad.* namespace package that does not
        have it imported yet -- at startup that is all of them, later on it is whatever a
        live addon installation has brought in."""
        try:
            import pkgutil
            import importlib
            import freecad
        except ImportError as inst:
            Err('During initialization the error "' + str(inst) + '" occurred\n')
            return

        freecad.gui = FreeCADGui
        for _, freecad_module_name,\
            freecad_module_ispkg in pkgutil.iter_modules(freecad.__path__, "freecad."):
            if not freecad_module_ispkg\
                or freecad_module_name + '.init_gui' in sys.modules:
                continue

            # Check for a stopfile
            stopFile = os.path.join(FreeCAD.getUserAppDataDir(), "Mod",
                                    freecad_module_name[8:], "ADDON_DISABLED")
            if os.path.exists(stopFile):
                continue

            # Make sure that package.xml (if present) does not exclude this version of FreeCAD
            MetadataFile = os.path.join(FreeCAD.getUserAppDataDir(), "Mod",
                                        freecad_module_name[8:], "package.xml")
            if os.path.exists(MetadataFile):
                meta = FreeCAD.Metadata(MetadataFile)
                if not meta.supportsCurrentFreeCAD():
                    continue

            Log('Init: Initializing ' + freecad_module_name + '\n')
            try:
                freecad_module = importlib.import_module(freecad_module_name)
                if any (module_name == 'init_gui' for _, module_name,
                        ispkg in pkgutil.iter_modules(freecad_module.__path__)):
                    importlib.import_module(freecad_module_name + '.init_gui')
                    Log('Init: Initializing ' + freecad_module_name + '... done\n')
                else:
                    Log('Init: No init_gui module found in ' + freecad_module_name\
                        + ', skipping\n')
            except Exception as inst:
                Err('During initialization the error "' + str(inst) + '" occurred in '\
                    + freecad_module_name + '\n')
                Err('-'*80+'\n')
                Err(traceback.format_exc())
                Err('-'*80+'\n')
                Log('Init:      Initializing ' + freecad_module_name + '... failed\n')
                Log('-'*80+'\n')
                Log(traceback.format_exc())
                Log('-'*80+'\n')

    def initApplication(Dir):
        """Bring one module directory into the already running session, both halves: the
        App one (search paths and Init.py) and then this one (InitGui.py, which is what
        registers a workbench and its commands). This is what lets a freshly installed
        addon be used without restarting FreeCAD; startup does the same work through the
        loops below.

        Returns True if the directory was initialized, False if it was skipped -- notably
        when the session already knows it, since re-running an InitGui.py would register
        its workbench and commands a second time."""
        if not FreeCAD.initApplication(Dir):
            return False
        InitApplication(os.path.realpath(Dir))
        InitNamespacePackages()
        return True

    # Keep the entry point alive past the "del InitApplications" below: the addon
    # manager calls it to make a freshly installed addon usable without a restart.
    FreeCADGui.initApplication = initApplication

    for Dir in ModDirs:
        InitApplication(Dir)
    Log("All modules with GUIs using InitGui.py are now initialized\n")

    InitNamespacePackages()

    Log("All modules with GUIs initialized using pkgutil are now initialized\n")

def GeneratePackageIcon(dir:str, subdirectory:str, workbench_metadata:FreeCAD.Metadata,
                        wb_handle:Workbench) -> None:
    relative_filename = workbench_metadata.Icon
    if not relative_filename:
        # Although a required element, this content item does not have an icon. Just bail out
        return
    absolute_filename = os.path.join(subdirectory, relative_filename)
    if hasattr(wb_handle, "Icon") and wb_handle.Icon:
        Log(f"Init:      Packaged workbench {workbench_metadata.Name} specified icon\
            in class {workbench_metadata.Classname}")
        Log(f" ... replacing with icon from package.xml data.\n")
    wb_handle.__dict__["Icon"] = absolute_filename


Log ('Init: Running FreeCADGuiInit.py start script...\n')



# init the gui

# signal that the gui is up
App.GuiUp = 1
App.Gui = FreeCADGui
FreeCADGui.Workbench = Workbench

Gui.addWorkbench(NoneWorkbench())

# Monkey patching pivy.coin.SoGroup.removeAllChildren to work around a bug
# https://bitbucket.org/Coin3D/coin/pull-requests/119/fix-sochildlist-auditing/diff

def _SoGroup_init(self,*args):
    import types
    _SoGroup_init_orig(self,*args)
    self.removeAllChildren = \
        types.MethodType(FreeCADGui.coinRemoveAllChildren,self)
try:
    from pivy import coin
    _SoGroup_init_orig = coin.SoGroup.__init__
    coin.SoGroup.__init__ = _SoGroup_init
except Exception:
    pass

# Monkey patching QtWidgets.QFileDialog static function to enforce not
# using native dialog to work around snap build file access problem
try:
    from PySide import QtWidgets
    QtWidgets.QFileDialog.getOpenFileName = FreeCADGui.FileDialog.getOpenFileName
    QtWidgets.QFileDialog.getOpenFileNames = FreeCADGui.FileDialog.getOpenFileNames
    QtWidgets.QFileDialog.getSaveFileName = FreeCADGui.FileDialog.getSaveFileName
    QtWidgets.QFileDialog.getExistingDirectory = FreeCADGui.FileDialog.getExistingDirectory
except Exception:
    pass

# init modules
InitApplications()

# set standard workbench (needed as fallback)
Gui.activateWorkbench("NoneWorkbench")

# Register .py, .FCScript and .FCMacro
FreeCAD.addImportType("Inventor V2.1 (*.iv *.IV)","FreeCADGui")
FreeCAD.addImportType("VRML V2.0 (*.wrl *.WRL *.vrml *.VRML *.wrz *.WRZ *.wrl.gz *.WRL.GZ)","FreeCADGui")
FreeCAD.addImportType("Python (*.py *.FCMacro *.FCScript *.fcmacro *.fcscript)","FreeCADGui")
FreeCAD.addExportType("Inventor V2.1 (*.iv)","FreeCADGui")
FreeCAD.addExportType("VRML V2.0 (*.wrl *.vrml *.wrz *.wrl.gz)","FreeCADGui")
FreeCAD.addExportType("X3D Extensible 3D (*.x3d *.x3dz)","FreeCADGui")
FreeCAD.addExportType("WebGL/X3D (*.xhtml)","FreeCADGui")
#FreeCAD.addExportType("IDTF (for 3D PDF) (*.idtf)","FreeCADGui")
#FreeCAD.addExportType("3D View (*.svg)","FreeCADGui")
FreeCAD.addExportType("Portable Document Format (*.pdf)","FreeCADGui")

del InitApplications
del NoneWorkbench
del StandardWorkbench

Log ('Init: Running FreeCADGuiInit.py start script... done\n')
