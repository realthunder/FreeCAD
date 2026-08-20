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

# Gui.UserInput / Gui.InputHint / Gui.HintManager
#
# Upstream FreeCAD registers the UserInput enum from C++ (src/Gui/InputHint.h)
# and shows the hints in a status bar widget. This fork does not carry that
# subsystem, so the names it publishes are provided here in Python and the
# hints are simply not displayed. Workbenches -- Draft above all -- only need
# the enum members to exist and to compare equal to the matching Qt keys.
#
# The values must match with that of the C++ enum class InputHint::UserInput.

def _build_user_input():
    from PySide.QtCore import Qt

    def _int(value):
        # PySide6 hands out enum members, and the flag types among them do not
        # survive a plain int(): take .value when there is one.
        return int(getattr(value, "value", value))

    def _key(name):
        value = getattr(Qt, "Key_" + name, None)
        if value is None:
            value = getattr(Qt.Key, "Key_" + name)
        return _int(value)

    keypad = getattr(Qt, "KeypadModifier", None)
    if keypad is None:
        keypad = Qt.KeyboardModifier.KeypadModifier
    _keypad = _int(keypad)

    return IntEnum("UserInput", [
        ("ModifierShift", _key("Shift")),
        ("ModifierCtrl", _key("Control")),
        ("ModifierAlt", _key("Alt")),
        ("ModifierMeta", _key("Meta")),
        ("KeySpace", _key("Space")),
        ("KeyExclam", _key("Exclam")),
        ("KeyQuoteDbl", _key("QuoteDbl")),
        ("KeyNumberSign", _key("NumberSign")),
        ("KeyDollar", _key("Dollar")),
        ("KeyPercent", _key("Percent")),
        ("KeyAmpersand", _key("Ampersand")),
        ("KeyApostrophe", _key("Apostrophe")),
        ("KeyParenLeft", _key("ParenLeft")),
        ("KeyParenRight", _key("ParenRight")),
        ("KeyAsterisk", _key("Asterisk")),
        ("KeyPlus", _key("Plus")),
        ("KeyComma", _key("Comma")),
        ("KeyMinus", _key("Minus")),
        ("KeyPeriod", _key("Period")),
        ("KeySlash", _key("Slash")),
        ("Key0", _key("0")),
        ("Key1", _key("1")),
        ("Key2", _key("2")),
        ("Key3", _key("3")),
        ("Key4", _key("4")),
        ("Key5", _key("5")),
        ("Key6", _key("6")),
        ("Key7", _key("7")),
        ("Key8", _key("8")),
        ("Key9", _key("9")),
        ("KeyColon", _key("Colon")),
        ("KeySemicolon", _key("Semicolon")),
        ("KeyLess", _key("Less")),
        ("KeyEqual", _key("Equal")),
        ("KeyGreater", _key("Greater")),
        ("KeyQuestion", _key("Question")),
        ("KeyAt", _key("At")),
        ("KeyA", _key("A")),
        ("KeyB", _key("B")),
        ("KeyC", _key("C")),
        ("KeyD", _key("D")),
        ("KeyE", _key("E")),
        ("KeyF", _key("F")),
        ("KeyG", _key("G")),
        ("KeyH", _key("H")),
        ("KeyI", _key("I")),
        ("KeyJ", _key("J")),
        ("KeyK", _key("K")),
        ("KeyL", _key("L")),
        ("KeyM", _key("M")),
        ("KeyN", _key("N")),
        ("KeyO", _key("O")),
        ("KeyP", _key("P")),
        ("KeyQ", _key("Q")),
        ("KeyR", _key("R")),
        ("KeyS", _key("S")),
        ("KeyT", _key("T")),
        ("KeyU", _key("U")),
        ("KeyV", _key("V")),
        ("KeyW", _key("W")),
        ("KeyX", _key("X")),
        ("KeyY", _key("Y")),
        ("KeyZ", _key("Z")),
        ("KeyBracketLeft", _key("BracketLeft")),
        ("KeyBackslash", _key("Backslash")),
        ("KeyBracketRight", _key("BracketRight")),
        ("KeyAsciiCircum", _key("AsciiCircum")),
        ("KeyUnderscore", _key("Underscore")),
        ("KeyQuoteLeft", _key("QuoteLeft")),
        ("KeyBraceLeft", _key("BraceLeft")),
        ("KeyBar", _key("Bar")),
        ("KeyBraceRight", _key("BraceRight")),
        ("KeyAsciiTilde", _key("AsciiTilde")),
        ("KeyEscape", _key("Escape")),
        ("KeyTab", _key("Tab")),
        ("KeyBacktab", _key("Backtab")),
        ("KeyBackspace", _key("Backspace")),
        ("KeyReturn", _key("Return")),
        ("KeyEnter", _key("Enter")),
        ("KeyInsert", _key("Insert")),
        ("KeyDelete", _key("Delete")),
        ("KeyPause", _key("Pause")),
        ("KeyPrintScr", _key("Print")),
        ("KeySysReq", _key("SysReq")),
        ("KeyClear", _key("Clear")),
        ("KeyHome", _key("Home")),
        ("KeyEnd", _key("End")),
        ("KeyLeft", _key("Left")),
        ("KeyUp", _key("Up")),
        ("KeyRight", _key("Right")),
        ("KeyDown", _key("Down")),
        ("KeyPageUp", _key("PageUp")),
        ("KeyPageDown", _key("PageDown")),
        ("KeyShift", _key("Shift")),
        ("KeyControl", _key("Control")),
        ("KeyMeta", _key("Meta")),
        ("KeyAlt", _key("Alt")),
        ("KeyCapsLock", _key("CapsLock")),
        ("KeyNumLock", _key("NumLock")),
        ("KeyScrollLock", _key("ScrollLock")),
        ("KeyF1", _key("F1")),
        ("KeyF2", _key("F2")),
        ("KeyF3", _key("F3")),
        ("KeyF4", _key("F4")),
        ("KeyF5", _key("F5")),
        ("KeyF6", _key("F6")),
        ("KeyF7", _key("F7")),
        ("KeyF8", _key("F8")),
        ("KeyF9", _key("F9")),
        ("KeyF10", _key("F10")),
        ("KeyF11", _key("F11")),
        ("KeyF12", _key("F12")),
        ("KeyF13", _key("F13")),
        ("KeyF14", _key("F14")),
        ("KeyF15", _key("F15")),
        ("KeyF16", _key("F16")),
        ("KeyF17", _key("F17")),
        ("KeyF18", _key("F18")),
        ("KeyF19", _key("F19")),
        ("KeyF20", _key("F20")),
        ("KeyF21", _key("F21")),
        ("KeyF22", _key("F22")),
        ("KeyF23", _key("F23")),
        ("KeyF24", _key("F24")),
        ("KeyF25", _key("F25")),
        ("KeyF26", _key("F26")),
        ("KeyF27", _key("F27")),
        ("KeyF28", _key("F28")),
        ("KeyF29", _key("F29")),
        ("KeyF30", _key("F30")),
        ("KeyF31", _key("F31")),
        ("KeyF32", _key("F32")),
        ("KeyF33", _key("F33")),
        ("KeyF34", _key("F34")),
        ("KeyF35", _key("F35")),
        ("KeyNum0", _key("0") | _keypad),
        ("KeyNum1", _key("1") | _keypad),
        ("KeyNum2", _key("2") | _keypad),
        ("KeyNum3", _key("3") | _keypad),
        ("KeyNum4", _key("4") | _keypad),
        ("KeyNum5", _key("5") | _keypad),
        ("KeyNum6", _key("6") | _keypad),
        ("KeyNum7", _key("7") | _keypad),
        ("KeyNum8", _key("8") | _keypad),
        ("KeyNum9", _key("9") | _keypad),
        ("MouseMove", 1 << 16),
        ("MouseLeft", 2 << 16),
        ("MouseRight", 3 << 16),
        ("MouseMiddle", 4 << 16),
        ("MouseScroll", 5 << 16),
        ("MouseScrollUp", 6 << 16),
        ("MouseScrollDown", 7 << 16),
        ("Mouse", 8 << 16),
        ("MouseMoveLeft", 9 << 16),
        ("MouseMoveMiddle", 10 << 16),
        ("MouseMoveRight", 11 << 16),
        ("MouseDoubleLeft", 12 << 16),
    ])


UserInput = _build_user_input()
Gui.UserInput = UserInput


class InputHint:
    """One input hint: a message plus the input sequences it refers to.

    The message is a Qt format string with %1, %2, ... placeholders, one per
    sequence. A sequence is either a single Gui.UserInput member or a tuple of
    them. Kept API-compatible with upstream; this fork never displays them.
    """

    def __init__(self, message, *sequences):
        self.message = message
        self.sequences = list(sequences)


class HintManager:
    """Stand-in for upstream's hint manager. Accepts hints and drops them."""

    def show(self, *hints):
        pass

    def hide(self):
        pass


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
