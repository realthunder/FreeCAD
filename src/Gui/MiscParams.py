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
'''Auto code generator for the settings of several small parameter groups
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamUInt, ParamString, ParamFloat, \
                         ParamComboBox

NameSpace = 'Gui'
ClassName = 'MiscParams'
ParamPath = 'User parameter:BaseApp/Preferences'
ClassDoc = 'Convenient class to obtain the settings of several small parameter groups'

# Groups that hold two or three settings each and would not fill a class of
# their own. Each setting names its group; some share it with what the
# program keeps there for itself (the recent macros themselves, the
# shortcuts, the list of workbenches), which is not listed.
Params = [
    # --- Preferences/RecentMacros
    ParamInt('RecentMacros', 12, subpath='RecentMacros',
        title = 'Size of recent macro list',
        doc = "Number of macros the recent macros menu lists."),
    ParamInt('ShortcutCount', 3, subpath='RecentMacros',
        title = 'Recent macros with a shortcut',
        doc = "Number of entries of the recent macros menu that get a keyboard\n"
              "shortcut, the modifiers below and a digit. At most 9."),
    ParamString('ShortcutModifiers', 'Ctrl+Shift+', subpath='RecentMacros',
        title = 'Recent macro shortcut modifiers',
        doc = "Modifier keys of the shortcuts of the recent macros menu, written\n"
              "as in a shortcut and ending in +, such as Ctrl+Shift+."),
    # --- Preferences/Gui/Gizmos
    ParamInt('CoarseLinearSnapMultiplier', 5, subpath='Gui/Gizmos',
        title = 'Coarse linear step of a gizmo',
        doc = "How many times larger the step of a linear gizmo is while the\n"
              "key for coarse steps is held."),
    ParamInt('CoarseRotationSnapMultiplier', 5, subpath='Gui/Gizmos',
        title = 'Coarse rotation step of a gizmo',
        doc = "How many times larger the step of a rotation gizmo is while the\n"
              "key for coarse steps is held."),
    # --- Preferences/CacheDirectory
    ParamUInt('CacheLimit', 500, subpath='CacheDirectory', param_name='Limit',
        title = 'Cache size limit',
        doc = "Size in megabytes the cache directory may grow to before the\n"
              "program offers to clean it."),
    ParamInt('CachePeriod', 2, subpath='CacheDirectory', param_name='Period',
        title = 'Cache check period',
        doc = "How often the size of the cache directory is checked: 0 always,\n"
              "1 daily, 2 weekly, 3 monthly, 4 yearly, 5 never."),
    # --- Preferences/Shortcut/Settings
    ParamInt('ShortcutTimeout', 300, subpath='Shortcut/Settings',
        title = 'Shortcut sequence timeout',
        doc = "Milliseconds the program waits for the next key of a shortcut\n"
              "made of several keys before it acts on what was typed."),
    # --- Preferences/Workbenches
    ParamBool('ShowTabBar', False, subpath='Workbenches',
        title = 'Workbench tab bar',
        doc = "Show the workbenches as a bar of tabs instead of a drop-down\n"
              "list."),
    ParamBool('TabBarShowText', False, subpath='Workbenches',
        title = 'Workbench tab bar text',
        doc = "Show the name of each workbench on its tab, beside its icon."),
    ParamInt('TabBarMaxLength', 0, subpath='Workbenches',
        title = 'Workbench tab bar length',
        doc = "Room in pixels the workbench tab bar may take along the way its\n"
              "tabs run. 0 takes what its tabs need."),
    # --- Preferences/HighDPI and Preferences/OpenGL, both read at startup
    ParamBool('DisableDpiScaling', False, subpath='HighDPI',
        title = 'Disable high DPI scaling',
        doc = "Switch Qt's scaling for high resolution screens off. Read at\n"
              "startup."),
    ParamBool('UseSoftwareOpenGL', False, subpath='OpenGL',
        title = 'Use software OpenGL',
        doc = "Draw with a software implementation of OpenGL instead of the\n"
              "graphics driver. Read at startup."),
    # --- Preferences/DependencyGraph
    ParamBool('Unflatten', True, subpath='DependencyGraph',
        title = 'Unflatten the dependency graph',
        doc = "Run the dependency graph through Graphviz's unflatten, which\n"
              "makes wide graphs narrower."),
    ParamBool('GeoFeatureSubgraphs', True, subpath='DependencyGraph',
        title = 'Sub-graphs in the dependency graph',
        doc = "Draws the objects of each coordinate system -- a Part, a Body --\n"
              "inside a box of its own in the dependency graph. On no page.\n"
              "Takes effect when the graph is next drawn."),
    # --- Preferences/Gui/Gizmos, the rest of the group: the Part page
    # "General" shows these. The gizmos read them at each drag.
    ParamBool('EnableGizmos', True, subpath='Gui/Gizmos',
        title = 'Show interactive draggers when editing features',
        doc = "Enables on-screen handles (draggers) in the 3D view for\n"
              "interactively modifying dimensions and parameters of the feature\n"
              "being edited by dragging."),
    ParamBool('DelayedGizmoUpdate', False, subpath='Gui/Gizmos',
        title = 'Disable recompute while dragging',
        doc = "Prevents the model from recalculating while manipulating\n"
              "draggers. The shape updates only after release of the mouse\n"
              "button."),
    ParamBool('EnableCoarseSnap', True, subpath='Gui/Gizmos',
        title = 'Enable coarse snapping while dragging',
        doc = "Enables larger snapping increments while manipulating draggers."),
    ParamInt('FineSnapModifier', 0x02000000, subpath='Gui/Gizmos',
        title = 'Fine snap modifier',
        doc = "Defines the modifier key used for fine snapping while dragging,\n"
              "as Qt numbers it: 33554432 is Shift, 67108864 is Ctrl. Anything\n"
              "else is taken for Shift."),
    ParamInt('DefaultCoarseDragBehavior', 0, subpath='Gui/Gizmos',
        proxy = ParamComboBox(['Coarse', 'Fine']),
        title = 'Default coarse drag behavior',
        doc = "Determines whether the drag is coarse or fine without holding\n"
              "the modifier key."),
    # --- Preferences/PropertyView: the property view's settings, which no
    # page shows, and what it and the Add Property dialog keep for
    # themselves. The view applies a change a moment later.
    ParamBool('PropertyViewAutoTransactionView', False, subpath='PropertyView',
        param_name='AutoTransactionView',
        title = 'Property view: undo for the View tab',
        doc = "An edit in the View tab of the property view opens an undo step of\n"
              "its own and recomputes the document when it ends, as one in the\n"
              "Data tab does. On no page."),
    ParamBool('PropertyViewAutoTransactionData', True, subpath='PropertyView',
        param_name='AutoTransactionData',
        title = 'Property view: undo for the Data tab',
        doc = "An edit in the Data tab of the property view opens an undo step of\n"
              "its own and recomputes the document when it ends. On no page."),
    ParamBool('PropertyViewAutoExpandView', False, subpath='PropertyView',
        param_name='AutoExpandView',
        title = 'Property view: expand the View tab',
        doc = "The View tab of the property view starts a session with\n"
              "everything unfolded: the groups, and the parts of a property that\n"
              "has some. 'Auto expand' of the view's context menu switches it\n"
              "for the session. On no page."),
    ParamBool('PropertyViewAutoExpandData', False, subpath='PropertyView',
        param_name='AutoExpandData',
        title = 'Property view: expand the Data tab',
        doc = "The Data tab of the property view starts a session with\n"
              "everything unfolded: the groups, and the parts of a property that\n"
              "has some. 'Auto expand' of the view's context menu switches it\n"
              "for the session. On no page."),
    ParamBool('PropertyViewHideHeader', False, subpath='PropertyView',
        param_name='HideHeader',
        title = 'Property view: hide header',
        doc = "Hides the header row, Property and Value, of the property view.\n"
              "Set by 'Hide header' of the view's context menu."),
    ParamInt('PropertyViewViewSectionSize', 150, subpath='PropertyView',
        param_name='ViewSectionSize',
        title = 'Property view: name column of the View tab',
        doc = "Width in pixels of the Property column of the View tab. Stored\n"
              "by the property view when the column is resized."),
    ParamInt('PropertyViewDataSectionSize', 150, subpath='PropertyView',
        param_name='DataSectionSize',
        title = 'Property view: name column of the Data tab',
        doc = "Width in pixels of the Property column of the Data tab. Stored\n"
              "by the property view when the column is resized."),
    ParamInt('PropertyViewLastTabIndex', 1, subpath='PropertyView',
        param_name='LastTabIndex',
        title = 'Property view: last tab',
        doc = "The tab the property view was last on: 0 View, 1 Data. Stored\n"
              "when the tab changes, read when the view is made."),
    ParamString('NewPropertyType', 'App::PropertyString', subpath='PropertyView',
        title = 'Add property: last type',
        doc = "The property type the Add Property dialog was last used with,\n"
              "which it opens on. Stored when the dialog is accepted."),
    ParamString('NewPropertyGroup', 'Base', subpath='PropertyView',
        title = 'Add property: last group',
        doc = "The group the Add Property dialog last put a property in, which\n"
              "it opens with. Stored when the dialog is accepted."),
    ParamBool('NewPropertyAppend', True, subpath='PropertyView',
        title = 'Add property: prefix the name with the group',
        doc = "The Add Property dialog opens with its box checked that puts the\n"
              "group's name in front of the property's. Stored when the dialog\n"
              "is accepted."),
    # --- Preferences/Fw: the panel mirror of a served session
    ParamString('PanelMirror', 'all', subpath='Fw',
        title = 'Mirrored task dialogs',
        doc = "Which task dialogs a served session mirrors to its viewers: all,\n"
              "none, or the class names of the dialogs separated by commas. On\n"
              "no page. Takes effect at the next dialog."),
    ParamInt('PanelPollMs', 500, subpath='Fw',
        title = 'Panel mirror poll interval (ms)',
        doc = "Milliseconds between two looks the panel mirror takes at a\n"
              "mirrored task dialog for changes its widgets did not announce; 0\n"
              "switches the polling off. On no page. Takes effect at the next\n"
              "dialog."),
    # --- Preferences/Selection
    ParamBool('AutoShowSelectionView', False, subpath='Selection',
        title = 'Show the selection view on selection',
        doc = "Brings the selection view up when something is selected and\n"
              "puts it away when the selection is empty. On no page. Takes\n"
              "effect at the next change of the selection."),
    ParamBool('SingleClickFeatureSelect', True, subpath='Selection',
        param_name='singleClickFeatureSelect',
        title = 'Feature picker: accept on one click',
        doc = "In PartDesign's dialog that asks for a feature to work on, a\n"
              "click on a feature picks it and goes on; off, the choice has to\n"
              "be confirmed. On no page."),
    # --- Preferences/DAGView: the dependency graph view. The view reads
    # them when it is made and stores what it read.
    ParamInt('DAGViewSelectionMode', 0, subpath='DAGView', param_name='SelectionMode',
        proxy = ParamComboBox(['Single', 'Multiple']),
        title = 'DAG view: selection mode',
        doc = "Whether a click in the DAG view selects one object in place of\n"
              "the selection or adds to it. On no page. Read when the view is\n"
              "made."),
    ParamInt('DAGViewFontPointSize', 0, subpath='DAGView', param_name='FontPointSize',
        title = 'DAG view: font size',
        doc = "Font size of the DAG view in points; 0 is the application's. On\n"
              "no page. Read when the view is made."),
    ParamFloat('DAGViewDirection', 1.0, subpath='DAGView', param_name='Direction',
        title = 'DAG view: direction',
        doc = "The direction the DAG view lists the objects in: 1, or -1 for the\n"
              "other way up. Anything else is taken for 1. On no page. Read when\n"
              "the view is made."),
    # --- Preferences/View/Custom: the orientation "Custom" of a new document
    ParamFloat('CustomViewQ0', 0.0, subpath='View/Custom', param_name='Q0',
        title = 'Custom view orientation: q0',
        doc = "First component of the quaternion a new document's view is turned\n"
              "to when its camera orientation is 'Custom'. Set by the dialog of\n"
              "the Navigation page."),
    ParamFloat('CustomViewQ1', 0.0, subpath='View/Custom', param_name='Q1',
        title = 'Custom view orientation: q1',
        doc = "Second component of the quaternion of the 'Custom' camera\n"
              "orientation of a new document."),
    ParamFloat('CustomViewQ2', 0.0, subpath='View/Custom', param_name='Q2',
        title = 'Custom view orientation: q2',
        doc = "Third component of the quaternion of the 'Custom' camera\n"
              "orientation of a new document."),
    ParamFloat('CustomViewQ3', 1.0, subpath='View/Custom', param_name='Q3',
        title = 'Custom view orientation: q3',
        doc = "Fourth component of the quaternion of the 'Custom' camera\n"
              "orientation of a new document."),
    # --- Preferences/RecentFiles
    ParamInt('RecentFiles', 4, subpath='RecentFiles',
        title = 'Size of the recent file list',
        doc = "How many files the recent files menu shows. The General page\n"
              "has it. The list itself is kept beside it, a key per file, and is\n"
              "not listed."),
    # --- Preferences/Websites: the one address that is not a translated text
    ParamString('DonatePage', 'https://wiki.freecad.org/Donate', subpath='Websites',
        title = 'Donation page',
        doc = "The address Help > Donate opens. The command stores what it read,\n"
              "so the key is there after its first use."),
    # --- Preferences/Paths
    ParamString('Graphviz', '', subpath='Paths',
        title = 'Graphviz folder',
        doc = "Folder of the Graphviz programs the dependency graph is drawn\n"
              "with. Not set, /usr/bin is tried on Linux and the search path\n"
              "elsewhere; when that fails the program asks for the folder and\n"
              "stores the answer here."),
    # --- Preferences/Bitmaps/Theme: the icon theme, read at start
    ParamString('IconThemeName', '', subpath='Bitmaps/Theme', param_name='Name',
        title = 'Icon theme',
        doc = "Name of the icon theme Qt is told to use. Empty, the program's own\n"
              "icons are used. On no page. Read at start."),
    ParamString('IconThemeSearchPath', '', subpath='Bitmaps/Theme', param_name='SearchPath',
        title = 'Icon theme search path',
        doc = "A folder put in front of the places Qt looks for icon themes in.\n"
              "On no page. Read at start."),
    ParamBool('IconThemeSearchPaths', False, subpath='Bitmaps/Theme',
        param_name='_ThemeSearchPaths',
        title = "Use the desktop's icon themes",
        doc = "Linux only: leaves the desktop's icon theme and its search paths\n"
              "in place, where the program otherwise uses its own icons alone.\n"
              "It rarely works, the common themes lack most of the icons. On no\n"
              "page. Read at start."),
    # --- Preferences/DockWindows: which of the panels has a dock of its
    # own. The main window reads these while it lays its docks out and
    # stores what it found; where one is not stored the old flag of the
    # panel under BaseApp/MainWindow/DockWindows decides.
    ParamBool('TreeViewEnabled', True, subpath='DockWindows/TreeView', param_name='Enabled',
        title = 'Tree view in a dock of its own',
        doc = "The model tree has a dock window of its own, with the property\n"
              "view in another. 'Tree view mode' of the General page stores it\n"
              "with the two below. Read when the main window sets its dock\n"
              "windows up."),
    ParamBool('PropertyViewEnabled', True, subpath='DockWindows/PropertyView',
        param_name='Enabled',
        title = 'Property view in a dock of its own',
        doc = "The property view has a dock window of its own. It always has\n"
              "one while the tree view does. Read when the main window sets its\n"
              "dock windows up."),
    ParamBool('ComboViewEnabled', False, subpath='DockWindows/ComboView', param_name='Enabled',
        title = 'Combo view',
        doc = "The combo view -- model tree and property view in one dock window\n"
              "-- is there. Read when the main window sets its dock windows up."),
    ParamBool('TaskWatcherEnabled', False, subpath='DockWindows/TaskWatcher',
        param_name='Enabled',
        title = 'Separate task list from task view',
        doc = "The list of tasks of the active workbench has a dock window of its\n"
              "own, apart from the task view. The General page has it. Read\n"
              "when the main window sets its dock windows up."),
    ParamBool('DAGViewEnabled', False, subpath='DockWindows/DAGView', param_name='Enabled',
        title = 'DAG view',
        doc = "The DAG view, a dock window that shows the objects of the document\n"
              "as a dependency graph, is there. On no page. Read when the main\n"
              "window sets its dock windows up."),
    ParamInt('ComboViewTreeViewSize', 0, subpath='DockWindows/ComboView',
        param_name='TreeViewSize',
        title = 'Combo view: height of the tree',
        doc = "Height in pixels the model tree last had in the combo view; 0\n"
              "leaves it to the layout. Stored by the program."),
    ParamInt('ComboViewPropertyViewSize', 0, subpath='DockWindows/ComboView',
        param_name='PropertyViewSize',
        title = 'Combo view: height of the property view',
        doc = "Height in pixels the property view last had in the combo view; 0\n"
              "leaves it to the layout. Stored by the program."),
    # --- Preferences/Placement
    ParamInt('PlacementRotationMethod', 0, subpath='Placement', param_name='RotationMethod',
        title = 'Placement dialog: last rotation input',
        doc = "The way of giving a rotation the Placement dialog was last on, as\n"
              "the number of its entry. Stored by the dialog."),
    # --- Preferences/SceneShare: the fields of the Share dialog, stored
    # when it is accepted. The share token, which is a secret, the doors,
    # the grants and the clients are kept beside them and are not listed.
    ParamInt('SharePort', 8210, subpath='SceneShare', param_name='Port',
        title = 'Share: port',
        doc = "The port the scene server listens on when a document is shared."),
    ParamString('ShareDoor', '', subpath='SceneShare', param_name='Door',
        title = 'Share: front door',
        doc = "Name of the front door last chosen in the Share dialog: one of\n"
              "the doors kept beside this setting, each a way viewers reach\n"
              "this machine."),
    ParamString('ShareExternalHost', '', subpath='SceneShare', param_name='ExternalHost',
        title = 'Share: address of this machine',
        doc = "The address viewers reach this machine at, as the share links\n"
              "carry it. Empty, the address the program finds itself is used."),
    ParamString('ShareViewerPage', '', subpath='SceneShare', param_name='ViewerPage',
        title = 'Share: viewer page',
        doc = "Address of the viewer page the share links point at. Empty, the\n"
              "page the program serves itself is used."),
    ParamBool('ShareTrustProxy', False, subpath='SceneShare', param_name='TrustProxy',
        title = 'Share: trust the proxy for client addresses',
        doc = "Takes the address of a viewer from the X-Forwarded-For header a\n"
              "proxy in front of this machine adds, instead of the address the\n"
              "connection comes from. Only for a door on the local network."),
    ParamBool('ShareDoorsSeeded', False, subpath='SceneShare', param_name='DoorsSeeded',
        title = 'Share: doors made',
        doc = "The program has made the first front doors of the Share dialog.\n"
              "Stored by the program so that it does it once."),
    ParamBool('ShareGrantsMigrated', False, subpath='SceneShare', param_name='GrantsMigrated',
        title = 'Share: grants taken over',
        doc = "The program has turned what an older version kept -- one token\n"
              "and a list of clients -- into grants. Stored by the program so\n"
              "that it does it once."),
    # --- BaseApp/History/Dragger, outside Preferences
    ParamFloat('DraggerLastTranslationIncrement', 1.0,
        subpath='User parameter:BaseApp/History/Dragger', param_name='LastTranslationIncrement',
        title = 'Transform: last translation increment',
        doc = "The translation increment the Transform task panel was last left\n"
              "with. Stored when the panel is accepted."),
    ParamFloat('DraggerLastRotationIncrement', 15.0,
        subpath='User parameter:BaseApp/History/Dragger', param_name='LastRotationIncrement',
        title = 'Transform: last rotation increment',
        doc = "The rotation increment, in degrees, the Transform task panel was\n"
              "last left with. Stored when the panel is accepted."),
    # --- BaseApp/MainWindow/DockWindows, outside Preferences. The group
    # also holds a switch per dock window, named after the window, for
    # whether it is shown; two of them are read by name.
    ParamBool('ActivateOverlay', True, subpath='User parameter:BaseApp/MainWindow/DockWindows',
        title = 'Overlay docks',
        doc = "Sets the overlay management of the dock windows up: with it a\n"
              "dock window can be laid over the 3D view. On no page. Read at\n"
              "start."),
    ParamInt('DockCursorMargin', 5, subpath='User parameter:BaseApp/MainWindow/DockWindows',
        param_name='CursorMargin',
        title = 'Dock window edge margin',
        doc = "Distance in pixels from the edge of a dock window within which\n"
              "the mouse counts as on the edge, for resizing an overlaid one.\n"
              "On no page. Takes effect at once."),
    ParamBool('DockStdTreeView', True, subpath='User parameter:BaseApp/MainWindow/DockWindows',
        param_name='Std_TreeView',
        title = 'Tree view dock shown',
        doc = "The tree view dock was last shown. Kept by the program; it also\n"
              "decides whether the tree view has a dock of its own while that\n"
              "setting is not stored."),
    ParamBool('DockStdPropertyView', False,
        subpath='User parameter:BaseApp/MainWindow/DockWindows', param_name='Std_PropertyView',
        title = 'Property view dock shown',
        doc = "The property view dock was last shown. Kept by the program; it\n"
              "also decides whether the property view has a dock of its own\n"
              "while that setting is not stored."),
]

# --- BaseApp/MainWindow/DockWindows/Overlay<side>: what each of the four
# overlay panels keeps. A panel reads its group when it is made and stores
# it when the layout is saved.
_OVERLAY_KEYS = [
    (ParamString, 'Widgets', '', 'dock windows',
        "The names of the dock windows laid over this side of the 3D view,\n"
        "separated by commas, in the order of their tabs."),
    (ParamInt, 'Width', 0, 'width',
        "Width in pixels of the overlay panel of this side; 0 while it has\n"
        "never been sized."),
    (ParamInt, 'Height', 0, 'height',
        "Height in pixels of the overlay panel of this side; 0 while it has\n"
        "never been sized."),
    (ParamInt, 'Offset1', 0, 'first offset',
        "First of the two offsets, in pixels, the overlay panel of this side\n"
        "is placed with."),
    (ParamInt, 'Offset3', 0, 'second offset',
        "Second of the two offsets, in pixels, the overlay panel of this side\n"
        "is placed with."),
    (ParamInt, 'Offset2', 0, 'size change',
        "Pixels the size of the overlay panel of this side is changed by."),
    (ParamString, 'Sizes', '', 'sizes of its dock windows',
        "The sizes in pixels of the dock windows in the overlay panel of this\n"
        "side, separated by commas, in the order of their tabs."),
    (ParamBool, 'AutoHide', False, 'auto hide',
        "The overlay panel of this side hides while the mouse is away from\n"
        "it. Of the four modes -- this one, EditHide, EditShow, TaskShow --\n"
        "the first that is on counts."),
    (ParamBool, 'EditHide', False, 'hide on editing',
        "The overlay panel of this side hides while an object is edited."),
    (ParamBool, 'EditShow', False, 'show on editing',
        "The overlay panel of this side shows only while an object is\n"
        "edited."),
    (ParamBool, 'TaskShow', False, 'show on task',
        "The overlay panel of this side shows only while a task dialog is\n"
        "open."),
    (ParamBool, 'Closed', False, 'closed',
        "The overlay panel of this side was last hidden by the user. Counts\n"
        "only while none of its automatic modes is on."),
    (ParamBool, 'Transparent', False, 'transparent',
        "The overlay panel of this side lets the 3D view show through."),
]

for _side in ('Left', 'Right', 'Top', 'Bottom'):
    for _kind, _key, _default, _title, _doc in _OVERLAY_KEYS:
        Params.append(_kind('Overlay' + _side + _key, _default,
            subpath='User parameter:BaseApp/MainWindow/DockWindows/Overlay' + _side,
            param_name=_key,
            title='Overlay ' + _side.lower() + ': ' + _title,
            doc=_doc.replace('this side', 'the ' + _side.lower() + ' side')))


def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
