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
'''Auto code generator for Spreadsheet related parameters
'''
import sys
import cog
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamUInt,\
                         ParamFloat, ParamSpinBox, ParamColor, ParamHex,\
                         auto_comment, quote

NameSpace = 'Spreadsheet'
ClassName = 'SheetParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Spreadsheet'
ClassDoc = 'Convenient class to obtain spreadsheet related parameters'

Params = [
    ParamBool('showAliasName', False),
    ParamString('DisplayAliasFormatString', '%V = %A'),
    ParamString('AliasedCellBackgroundColor', '#feff9e'),
    ParamString('LockedAliasedCellColor', '#9effff'),
    ParamString('TextColor', '#000000'),
    ParamString('PositiveNumberColor', ''),
    ParamString('NegativeNumberColor', ''),
    ParamBool('VerticalConfTable', False),
    ParamBool('DoubleBindConfTable', False),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

class CellEditMode:
    def __init__(self, name, title, doc):
        self.name = name
        self.title = title
        self.doc = doc

CellEditModes = [
    CellEditMode('Normal', 'Normal', "Reset edit mode"),
    CellEditMode('Button', 'Button',
            "Make a button with the current cell. Expects the cell to define a callable.\n"
            "The button label is defined by the doc string of the callable. If empty,\n"
            "then use the alias. If no alias, then use the cell address."),
    CellEditMode('Combo', 'ComboBox',
            "Edit the cell using a ComboBox. This mode Expects the cell to contain a \n"
            "list(dict, string), where the keys of dict defines the item list, and the\n"
            "string defines the current item.\n\n"
            "The cell also accepts list(list, int), where the inner list defines the item\n"
            "list, and the int is the index of the current item.\n\n"
            "In both caes, there can be a third optional item that defines a callable with\n"
            "arguments (spreadsheet, cell_address, current_value, old_value). It will be\n"
            "invoked after the user makes a new selection in the ComboBox."),
    CellEditMode('Label', 'Label',
            "Edit the cell using a plain text box. This edit mode is used to hide expression\n"
            "details in the cell. The cell is expected to contain a list. And only the first\n"
            "item will be shown, and the rest of items hidden\n\n"
            "It can also be used to edit string property from other object using the double\n"
            "binding function, e.g. dbind(Box.Label2)."),
    CellEditMode('Quantity', 'Quantity',
            "Edit the cell using a unit aware SpinBox. This mode expects the cell\n"
            "to contain either a simple number, a 'quantity' (i.e. number with unit)\n"
            "or a list(quantity, dict). The dict contains optional keys ('step','max',\n"
            "'min','unit'). All keys are expects to have 'double' type of value, except\n"
            "'unit' which must be a string.\n\n"
            "If no 'unit' setting is found, the 'display unit' setting of the current cell\n"
            "will be used"),
    CellEditMode('CheckBox', 'CheckBox',
            "Edit the cell using a CheckBox. The cell is expected to contain any value\n"
            "that can be converted to boolean. If you want a check box with a title, use\n"
            "a list(boolean, title)."),
    CellEditMode('AutoAlias', 'Auto alias',
            "A pseudo edit mode that expects the content of the cell to be plain text.\n"
            "It will use the first line of the text to set alias of the right sibling cell.\n"
            "space is converted to '_'.\n\n"
            "Moreover, a new cell added below an existing cell with 'Auto alias' edit mode\n"
            "will inherit this edit mode."),
    CellEditMode('AutoAliasV', 'Auto alias vertical',
            "Similar to 'Auto alias' edit mode but works in vertical, i.e. assign alias to\n"
            "the bottom sibling cell."),
    CellEditMode('Color', 'Color',
            "Edit the cell using a color button. The cell is expected to contain\n"
            "a tuple of three or four floating numbers"),
]

def declare_edit_modes():
    cog.out(f'''
    {auto_comment()}
    enum EditMode {{''')
    for i,mode in enumerate(CellEditModes):
        cog.out(f'''
        Edit{mode.name} = {i},''')
    cog.out(f'''
        EditModeMax,
    }};

    {auto_comment()}
    static const char *editModeName(EditMode mode);
    static const char *editModeLabel(EditMode mode);
    static const char *editModeToolTips(EditMode mode);
    bool setEditMode(const char *name, bool silent=false);
''')

def define_edit_modes():
    cog.out(f'''
{auto_comment()}
const char *Cell::editModeName(EditMode mode)
{{
    switch(mode) {{''')
    for mode in CellEditModes:
        cog.out(f'''
    case Edit{mode.name}:
        return "{mode.name}";''')

    cog.out(f'''
    default:
        return "?";
    }}
}}
''')

    cog.out(f'''
{auto_comment()}
const char *Cell::editModeLabel(EditMode mode)
{{
    switch(mode) {{''')
    for mode in CellEditModes:
        cog.out(f'''
    case Edit{mode.name}:
        return QT_TRANSLATE_NOOP("Spreadsheet", "{mode.title}");''')

    cog.out(f'''
    default:
        return "?";
    }}
}}
''')

    cog.out(f'''
{auto_comment()}
const char *Cell::editModeToolTips(EditMode mode)
{{
    switch(mode) {{''')
    for mode in CellEditModes:
        cog.out(f'''
    case Edit{mode.name}:
        return QT_TRANSLATE_NOOP("Spreadsheet",
{quote(mode.doc)});''')

    cog.out(f'''
    default:
        return "?";
    }}
}}
''')

    cog.out(f'''
{auto_comment()}
bool Cell::setEditMode(const char *name, bool silent)
{{
    EditMode mode;''')
    for i,mode in enumerate(CellEditModes):
        cog.out(f'''
    {"if" if not i else "else if"} (boost::equals(name, "{mode.name}"))
        mode = Edit{mode.name};''')
    cog.out(f'''
    else {{
        if(silent)
            FC_THROWM(Base::ValueError, "Unknown edit mode: " << (name?name:"?"));
        FC_WARN("Unknown edit mode " << (name?name:"?"));
        return false;
    }}
    return setEditMode(mode, silent);
}}''')


def init_edit_modes_actions():
    for mode in CellEditModes:
        cog.out(f'''
    {auto_comment()}
    actionEdit{mode.name} = new QAction(QApplication::translate("Spreadsheet", Cell::editModeLabel(Cell::Edit{mode.name})), this);
    actionEdit{mode.name}->setData(QVariant((int)Cell::Edit{mode.name}));
    actionEdit{mode.name}->setCheckable(true);
    actionEdit{mode.name}->setToolTip(QApplication::translate("Spreadsheet", Cell::editModeToolTips(Cell::Edit{mode.name})));
    editGroup->addAction(actionEdit{mode.name});''')

def declare_edit_modes_actions():
    cog.out(f'''
    {auto_comment()}''')
    for mode in CellEditModes:
        cog.out(f'''
    QAction *actionEdit{mode.name};''')

def pick_edit_mode_action():
    cog.out(f'''
    {auto_comment()}
    switch(cell->getEditMode()) {{''')
    for mode in CellEditModes:
        cog.out(f'''
    case Cell::Edit{mode.name}:
        action = actionEdit{mode.name};
        break;''')
    cog.out(f'''
    default:
        action = actionEditNormal;
        break;
    }}''')
