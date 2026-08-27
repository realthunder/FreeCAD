# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2026 realthunder <realthunder.dev@gmail.com>            *
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
'''Auto code generator for the view placement policy parameters

Where a newly opened view lands: docs/ViewPlacement.md sec 4.1. The
values are ASCII words, which ViewPlacement.cpp turns into a target.
'''
import cog, sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamString, ParamProxy

NameSpace = 'Gui'
ClassName = 'OpenViewParams'
ParamPath = 'User parameter:BaseApp/Preferences/View/OpenView'
ClassDoc = 'Convenient class to obtain view placement parameters'


class ParamTargetCombo(ParamProxy):
    '''A combo box whose stored value is the item's ASCII data.

    params_utils' own ParamComboBox stores the item POSITION, which is
    what every index trap in this code base is made of, and it cannot
    serve these parameters anyway: their values are words, shared with
    the reader in ViewPlacement.cpp and written into user.cfg. The
    "prefType" property is what makes Gui::PrefComboBox read and write
    item data as ASCII instead (PrefWidgets.cpp, restore/savePreferences).

    An optional hint line is appended below the combo -- the design
    ruled the preferences hint to BE the discoverability story for the
    Alt inversion, so it has to live next to these settings.
    '''

    WidgetType = "Gui::PrefComboBox"

    def __init__(self, items, hint=None):
        super().__init__()
        self.items = items
        self.hint = hint

    def widget_setter(self, _param):
        return None

    def declare_widget(self, param):
        param._declare_widget()
        if self.hint:
            cog.out(f"""
    QLabel *hint{param.name} = nullptr;""")

    def init_widget(self, param, row, group_name):
        param._init_widget(row, group_name)
        widget = param.widget_name
        cog.out(f"""

    {params_utils.trace_comment()}
    {widget}->setProperty("prefType", QByteArray());""")
        for value, _text in self.items:
            cog.out(f"""
    {widget}->addItem(QString(), QByteArray("{value}"));""")
        cog.out(f"""
    {widget}->setCurrentIndex({widget}->findData(QByteArray(
                {param.namespace}::{param.class_name}::default{param.name}().c_str())));""")
        if self.hint:
            cog.out(f"""
    hint{param.name} = new QLabel(this);
    hint{param.name}->setWordWrap(true);
    layout{group_name}->addWidget(hint{param.name});""")

    def retranslate(self, param):
        param._retranslate()
        cog.out(f"""
    {params_utils.trace_comment()}""")
        for i, (_value, text) in enumerate(self.items):
            cog.out(f"""
    {param.widget_name}->setItemText({i}, QObject::tr("{text}"));""")
        if self.hint:
            cog.out(f"""
    hint{param.name}->setText(QObject::tr("{self.hint}"));""")


Params = [
    ParamString('DocumentTarget', "Tab", title="New documents open in",
        proxy=ParamTargetCombo([
            ('Tab', 'Their own tab'),
            ('Split', 'A split beside the current view'),
            ('Floating', 'A floating window'),
        ]),
        doc="Where the first view of a newly opened document lands"),

    ParamString('DocViewTarget', "Split",
        title="Additional views of a document open in",
        proxy=ParamTargetCombo([
            ('Tab', 'Their own tab'),
            ('Split', 'A split beside the current view'),
            ('NewSplit', 'A new split, never reusing a cell'),
            ('Floating', 'A floating window'),
        ]),
        doc="Where a second 3D view, a drawing page, a spreadsheet or the\n"
            "CAM simulator of an already open document lands"),

    ParamString('UtilityTarget', "Tab", title="Utility windows open in",
        proxy=ParamTargetCombo([
            ('Tab', 'Their own tab'),
            ('Split', 'A split beside the current view'),
            ('Floating', 'A floating window'),
        ]),
        doc="Where a window that shows no document data lands, such as the\n"
            "dependency graph"),

    ParamString('SplitDirection', "Auto", title="New splits go",
        proxy=ParamTargetCombo([
            ('Auto', 'Along the longer side'),
            ('Right', 'To the right'),
            ('Down', 'Below'),
        ],
        hint="Hold Alt while opening to invert tab/split for that one view."),
        doc="Which way a cell is divided when a view opens in a split"),
]


def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])


def define():
    params_utils.define(sys.modules[__name__])


params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
