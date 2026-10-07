# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2022 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
'''Auto code generator for parameters in Preferences/TreeView
'''
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamUInt,\
                         ParamFloat, ParamSpinBox, ParamColor, ParamHex

NameSpace = 'Gui'
ClassName = 'TreeParams'
ParamPath = 'User parameter:BaseApp/Preferences/TreeView'
ClassDoc = 'Convenient class to obtain tree view related parameters'


Params = [
    ParamBool('SyncSelection', True, on_change=True,
        title='Auto expand tree item when the corresponding object is selected in 3D view',
        doc = "Expand the tree view to show the item of an object when it is\n"
              "selected in the 3D view, and scroll to it. When off, the item is\n"
              "selected without expanding its parents."),
    ParamBool('CheckBoxesSelection',False, on_change=True,
        title='Add checkboxes for selection in document tree',
        doc = "Show a checkbox on every object in the tree view. Ticking the box\n"
              "selects the object and clearing it deselects the object."),
    ParamBool('SyncView', True,
        title='Auto switch to the 3D view containing the selected item',
        doc = "Switch to the 3D view that shows an object when its item is selected\n"
              "in the tree view."),
    ParamBool('PreSelection', True,
        title='Preselect the object in 3D view when mouse over the tree item',
        doc = "Preselect an object in the 3D view while the mouse rests on its item\n"
              "in the tree view."),
    ParamBool('SyncPlacement', False,
        doc = "Adjust the placement of an object dragged and dropped in the tree\n"
              "view into another coordinate system, so that it stays where it was\n"
              "in space."),
    ParamBool('RecordSelection', True,
        title='Record selection in tree view in order to go back/forward using navigation button',
        doc = "Record every selection, so that the selection back and forward\n"
              "buttons can step through earlier selections."),
    ParamInt('DocumentMode', 2, on_change=True,
        doc = "How open documents are listed in the tree view. 0 shows only the\n"
              "active document, 1 shows all documents, 2 shows all and expands the\n"
              "active one while collapsing the others."),
    ParamInt('StatusTimeout', 100,
        doc = "Milliseconds the tree view waits before it refreshes its items after\n"
              "objects change. Changes within that time are handled in one pass."),
    ParamInt('SelectionTimeout', 100,
        doc = "Milliseconds the tree view waits before it follows a change of the\n"
              "selection. Changes within that time are handled in one pass."),
    ParamInt('PreSelectionTimeout', 500,
        doc = "Milliseconds the mouse must rest on a tree view item before its\n"
              "object is preselected in the 3D view. Applies when nothing was\n"
              "preselected from the tree within PreSelectionDelay."),
    ParamInt('PreSelectionDelay', 700,
        doc = "Milliseconds after a preselection from the tree view during which\n"
              "moving to another item preselects it at once. After that the mouse\n"
              "has to rest for PreSelectionTimeout again."),
    ParamInt('PreSelectionMinDelay', 200,
        doc = "Shortest time in milliseconds between two preselections from the\n"
              "tree view. Moving across items faster than this waits before the\n"
              "next one is preselected. 0 sets no limit."),
    ParamBool('RecomputeOnDrop', True,
        doc = "Recompute the document after objects are dragged and dropped in the\n"
              "tree view."),
    ParamBool('KeepRootOrder', True,
        doc = "Keep the top level objects of the tree view in the order they were\n"
              "created. An object that returns to the top level goes back to its\n"
              "place instead of to the end."),
    ParamBool('TreeActiveAutoExpand', True,
        doc = "Expand the tree view item of an object when it becomes the active\n"
              "one, such as the active body or part. Objects that ask for it are\n"
              "collapsed again when they stop being active."),
    ParamUInt('TreeActiveColor',  0xe6e6ffff, on_change=True,
        doc = "Background colour of the tree view item of an active object, such\n"
              "as the active body or part."),
    ParamUInt('TreeEditColor',  0x929200ff, on_change=True,
        title = 'Tree Edit Color',
        doc = "Background colour of the tree view item of the object being edited."),
    ParamUInt('SelectingGroupColor',  0x408081ff, on_change=True,
        doc = "Background colour of the tree view item marked with 'Toggle\n"
              "selecting group'. A pick in the 3D view inside that group selects\n"
              "its child object as a whole."),
    ParamBool('TreeActiveBold', True, on_change=True,
        doc = "Show the label of an active object, such as the active body or part,\n"
              "in bold in the tree view."),
    ParamBool('TreeActiveItalic', False, on_change=True,
        doc = "Show the label of an active object, such as the active body or part,\n"
              "in italics in the tree view."),
    ParamBool('TreeActiveUnderlined', False, on_change=True,
        doc = "Underline the label of an active object, such as the active body or\n"
              "part, in the tree view."),
    ParamBool('TreeActiveOverlined', False, on_change=True,
        doc = "Draw a line over the label of an active object, such as the active\n"
              "body or part, in the tree view."),
    ParamInt('Indentation', 0, on_change=True,
        doc = "Width in pixels by which each level of the tree view is indented. 0\n"
              "uses the default of the style. Applies to tree views created\n"
              "afterwards."),
    ParamBool('LabelExpression', False,
        doc = "Edit an object's label in the tree view with an editor that accepts\n"
              "an expression, so that the label can be bound to one."),
    ParamInt('IconSize', 0, on_change=True,
        doc = "Size in pixels of the icons in the tree view, which also sets the\n"
              "row height. 0 uses the system default size."),
    ParamInt('FontSize', 0, on_change=True,
        doc = "Point size of the label font in the tree view. 0 uses the\n"
              "application font size."),
    ParamInt('ItemSpacing', 0, on_change=True,
        title = 'Item Spacing',
        doc = "Extra height in pixels added to every row of the tree view."),
    ParamHex('ItemBackground', 0, on_change=True, title='Item background color', proxy=ParamColor(),
        doc = "Tree view item background. Only effective in overlay."),
    ParamInt('ItemBackgroundPadding', 0, on_change=True, title="Item background padding", proxy=ParamSpinBox(0, 100, 1),
        doc = "Tree view item background padding."),
    ParamBool('HideColumn', True, on_change=True, title="Hide extra column",
        doc = "Hide extra tree view column for item description."),
    ParamBool('HideScrollBar', True, title="Hide scroll bar",
        doc = "Hide tree view scroll bar in dock overlay."),
    ParamBool('HideHeaderView', True, title="Hide header",
        doc = "Hide tree view header view in dock overlay."),
    ParamBool('ResizableColumn', False, on_change=True, title="Resizable columns",
        doc = "Allow tree view columns to be manually resized."),
    ParamInt('ColumnSize1', 0,
        doc = "Width in pixels of the first tree view column, remembered when the\n"
              "column is resized by hand. Used only with resizable columns. 0\n"
              "leaves the width alone."),
    ParamInt('ColumnSize2', 0,
        doc = "Width in pixels of the second tree view column, remembered when the\n"
              "column is resized by hand. Used only with resizable columns. 0\n"
              "leaves the width alone."),
    ParamBool('TreeToolTipIcon', False, title='Show icon in tool tip',
        doc = "Show the icon of the object in the tool tip of its tree view item."),
]

def declare_begin():
    params_utils.declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
