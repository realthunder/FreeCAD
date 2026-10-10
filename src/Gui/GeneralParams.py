# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
# *                                                                         *
# *   This program is free software; you can redistribute it and/or modify  *
# *   it under the terms of the GNU Lesser General Public License (LGPL)    *
# *   as published by the Free Software Foundation; either version 2 of     *
# *   the License, or (at your option) any later version.                   *
# *   for detail see the LICENCE text file.                                 *
# *                                                                         *
# *   This program is distributed in the hope that it will be useful,       *
# *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# *   GNU Library General Public License for more details.                  *
# *                                                                         *
# *   You should have received a copy of the GNU Library General Public     *
# *   License along with this program; if not, write to the Free Software   *
# *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  *
# *   USA                                                                   *
# *                                                                         *
# ***************************************************************************
'''Auto code generator for parameters in Preferences/General
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString

NameSpace = 'Gui'
ClassName = 'GeneralParams'
ParamPath = 'User parameter:BaseApp/Preferences/General'
ClassDoc = 'Convenient class to obtain the general application settings'
Signal = True

# The settings of the group, and at the end what the program keeps there
# for itself: the last module, the last directory and filters of the file
# dialogs, the last answers of a few dialogs.
Params = [
    ParamString('Language', '',
        doc = "User interface language, as its English name (English, German,\n"
              "...). Empty or not set uses the system's language. A change\n"
              "retranslates the interface at once."),
    ParamInt('UseLocaleFormatting', 0,
        title = 'Number format',
        doc = "Decimal and group separators of numbers: 0 the operating system's,\n"
              "1 those of the interface language, 2 C/POSIX. Takes effect shortly\n"
              "after a change."),
    ParamBool('SubstituteDecimalSeparator', False,
        title = 'Substitute decimal separator',
        doc = "Type the locale's decimal separator with the decimal key of the\n"
              "numeric keypad, whatever the keyboard layout sends for it."),
    ParamBool('EnableCursorBlinking', True,
        title = 'Text cursor blinking',
        doc = "Blink the text cursor in text and number input fields. When off it\n"
              "stays steady. Takes effect at once."),
    ParamBool('ShowSplasher', True,
        title = 'Show splash screen',
        doc = "Show the splash screen while the program starts. Takes effect at\n"
              "the next start."),
    ParamBool('ShowSplasherMessages', True,
        title = 'Show splash screen messages',
        doc = "Show the loading messages on the splash screen. When off it shows\n"
              "its image only. Takes effect at the next start."),
    ParamBool('ShowVersionInTitle', True,
        title = 'Show version in title',
        doc = "Show the version number after the application name in the main\n"
              "window's title. Takes effect at the next start."),
    ParamBool('AutoApplyPreference', True,
        title = 'Apply preferences at once',
        doc = "Apply each change made in the Preferences dialog as it is made,\n"
              "without Apply or OK. When off a change is stored when Apply or OK\n"
              "is pressed."),
    ParamBool('SaveUserParameter', True,
        title = 'Save settings on change',
        doc = "Write the settings file when the Preferences dialog is accepted and\n"
              "when the list of recent files or of recent macros changes. When\n"
              "off those do not write it."),
    ParamInt('ToolbarIconSize', 24,
        title = 'Tool bar icon size',
        doc = "Size of tool bar icons in pixels, at least 5. The icons of the menu\n"
              "bar, the status bar and the workbench selector follow it unless\n"
              "given a size of their own. Takes effect shortly after a change."),
    ParamInt('WorkbenchTabIconSize', 0,
        title = 'Workbench tab icon size',
        doc = "Icon size in pixels of the workbench tab bar. 0 uses the tool bar\n"
              "icon size."),
    ParamInt('WorkbenchComboIconSize', 0,
        title = 'Workbench selector icon size',
        doc = "Icon size in pixels of the workbench selector's combo box. 0 uses\n"
              "0.8 times the tool bar icon size."),
    ParamInt('StatusBarIconSize', 0,
        title = 'Status bar icon size',
        doc = "Icon size in pixels of tool bars placed in the status bar. 0 uses\n"
              "0.6 times the tool bar icon size."),
    ParamInt('MenuBarIconSize', 0,
        title = 'Menu bar icon size',
        doc = "Icon size in pixels of tool bars placed beside the menu bar. 0 uses\n"
              "0.8 times the tool bar icon size, or all of it with the custom\n"
              "title bar."),
    ParamBool('LockTitleToolBars', False,
        title = 'Lock menu and status bar tool bars',
        doc = "Keep the tool bars docked in the menu bar and in the status bar\n"
              "from being dragged out. Set from the tool bar lock menu."),
    ParamBool('ComboBoxWheelEventFilter', False,
        title = 'Ignore wheel over unfocused inputs',
        doc = "Ignore the mouse wheel over combo boxes and over spin boxes that\n"
              "do not have the keyboard focus, so that scrolling a panel does not\n"
              "change values. Takes effect at the next start."),
    ParamString('AutoloadModule', '',
        title = 'Start up workbench',
        doc = "Workbench activated when the program starts, by its name, or\n"
              "$LastModule for the one that was active last. Empty or not set\n"
              "uses the configured start workbench. Takes effect at the next\n"
              "start."),
    ParamString('BackgroundAutoloadModules', '',
        title = 'Workbenches loaded at start',
        doc = "Comma separated names of the workbenches loaded in the background\n"
              "at start, without being shown. Takes effect at the next start."),
    ParamString('ExportDefaultFilenameSingle', '%F-%P-',
        title = 'Export file name, one object',
        doc = "Pattern of the file name Export offers when one object is\n"
              "selected. %F is the document, %Lx the object labels joined by x,\n"
              "%Px the parent and object labels joined by x, %U the UTC time, %D\n"
              "the local time."),
    ParamString('ExportDefaultFilenameMultiple', '%F',
        title = 'Export file name, several objects',
        doc = "Pattern of the file name Export offers when the selection is not\n"
              "exactly one object. The codes are those of the pattern for one\n"
              "object: %F, %Lx, %Px, %U, %D."),
    ParamBool('RecentIncludesImported', True,
        title = 'Recent files include imports',
        doc = "Add files opened with Import to the list of recent files."),
    ParamBool('RecentIncludesExported', False,
        title = 'Recent files include exports',
        doc = "Add exported files to the list of recent files, where some module\n"
              "can open that type of file."),
    ParamString('DownloadPath', '',
        title = 'Download directory',
        doc = "Directory downloaded files are saved in. Empty uses a folder named\n"
              "after the program inside the user's Documents folder."),
    ParamInt('AutoloadTab', 0,
        title = 'Report view first tab',
        doc = "Index of the tab the combined report view shows first. Read when\n"
              "the view is made."),
    ParamInt('ProgressDetailLevels', 5,
        title = 'Progress detail levels',
        doc = "How many nested levels of progress per thread the progress\n"
              "detail popup shows. Less than 1 counts as 1."),
    ParamBool('PreferXcbOnWsl', True,
        title = 'Prefer X11 under WSL',
        doc = "Under WSL with both Wayland and X11 at hand, and QT_QPA_PLATFORM\n"
              "not set, start on X11 (xcb) instead of Wayland. Takes effect at\n"
              "the next start."),
    ParamString('AdditionalLanguageDomainEntries', '',
        title = 'Additional languages',
        doc = "Interface languages added to the built-in table, as pairs\n"
              "\"Language Name\"=\"code\"; one after the other. Takes effect at\n"
              "the next start."),
    ParamString('AdditionalTranslationsDirectory', '',
        title = 'Additional translations directory',
        doc = "Directory searched for translation files ahead of the user's and\n"
              "the installation's own. Takes effect at the next start."),
    ParamString('TempPath', '',
        title = 'Temporary files directory',
        doc = "Directory used for temporary files instead of the system's, when\n"
              "it exists. Takes effect at the next start."),
    # --- what the program keeps in this group for itself. Their readers
    # read the group as before; the values here are what they find when
    # nothing is stored yet.
    ParamString('LastModule', '',
        title = 'Last workbench',
        doc = "The workbench that was active when the program last closed,\n"
              "which the next session starts in while the start-up workbench\n"
              "is set to the last one used. Stored by the program; empty until\n"
              "then, when the start workbench is used."),
    ParamString('FileOpenSavePath', '',
        title = 'Last folder of the file dialogs',
        doc = "The folder the file dialogs were last in, which they open on.\n"
              "Stored by the program; empty until then, when the home folder\n"
              "is used."),
    ParamString('FileImportFilter', '',
        title = 'Last file type of the Import dialog',
        doc = "The file type last chosen in the Import dialog, which it opens\n"
              "with. Stored by the program."),
    ParamString('FileExportFilter', '',
        title = 'Last file type of the Export dialog',
        doc = "The file type last chosen in the Export dialog, which it opens\n"
              "with. Stored by the program."),
    ParamString('OffscreenImageFormat', '',
        title = 'Last format of Save picture',
        doc = "The file format last chosen in the Save picture dialog. Stored\n"
              "by the program."),
    ParamInt('OffscreenImageBackground', 0,
        title = 'Last background of Save picture',
        doc = "The background last chosen in the options of the Save picture\n"
              "dialog, as the number of its entry. Stored by the program."),
    ParamBool('ConfirmAll', False,
        title = "Last 'Apply answer to all' of the save question",
        doc = "The state the box 'Apply answer to all' was last left in, in the\n"
              "question about unsaved documents at closing. Stored by the\n"
              "program."),
    ParamBool('ObjectSelectionAutoDeps', True,
        title = 'Object selection: auto select dependencies',
        doc = "The state the box that selects the dependencies of an object with\n"
              "it was last left in, in the object selection dialog. Stored by\n"
              "the program."),
    ParamBool('ObjectSelectionShowDeps', False,
        title = 'Object selection: show dependencies',
        doc = "Whether the object selection dialog last showed its dependency\n"
              "lists. Stored by the program."),
    ParamInt('UserEditMode', 0,
        title = 'Edit mode',
        doc = "What a double click on an object in the tree does, as the number\n"
              "of the entry of Edit > Edit mode: 0 the object's default, 1\n"
              "transform, 2 cutting, 3 colour. Stored when the menu is used and\n"
              "read at start."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
