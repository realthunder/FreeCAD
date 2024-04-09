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
'''Auto code generator for preference page of General/Objects
'''
import cog, sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Tools'))
import params_utils

from params_utils import ParamProxy, auto_comment

sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'App'))
import GroupParams
import LinkParams

Title = 'Objects'
NameSpace = 'Gui'
ClassName = 'DlgSettingsObjects'
ClassDoc = 'Preference dialog for various document objects related settings'

_GroupParams = { param.name : param for param in GroupParams.Params }
_LinkParams = { param.name : param for param in LinkParams.Params }

class ParamCreateOrigin(ParamProxy):
    def __init__(self, param):
        super().__init__()
        param.proxy = self
        param.header_file = ['App/Application.h',
                             'App/Document.h',
                             'App/Origin.h',
                             'App/AutoTransaction.h']

    def declare_widget(self, param):
        super().declare_widget(param)
        cog.out(f'''
    {auto_comment()}
    QPushButton *button{param.widget_name} = nullptr;''')

    def init_widget(self, param, row, group_name):
        cog.out(f'''
    {auto_comment()}
    {param.widget_name} = new {param.widget_type}(this);
    button{param.widget_name} = new QPushButton(this);
    {{
        layoutRow->addWidget({param.widget_name});
        layoutRow->addWidget(button{param.widget_name});
    }}
    button{param.widget_name}->setEnabled({param.widget_name}->isChecked());
    connect({param.widget_name}, SIGNAL(toggled(bool)), button{param.widget_name}, SLOT(setEnabled(bool)));
''')
        cog.out(f'''
    {auto_comment()}
    QObject::connect(button{param.widget_name}, &QPushButton::clicked, []() {{
        for (auto doc : App::GetApplication().getDocuments()) {{
            if (doc->testStatus(App::Document::TempDoc)
                || doc->testStatus(App::Document::PartialDoc))
                continue;
            App::GetApplication().setActiveDocument(doc);
            App::AutoTransaction guard("Init origins");
            // getObjects() below returns a const reference, so we must copy
            // objects here in order to iterate it safely while adding objects
            auto objs = doc->getObjects();
            for (auto obj : objs) {{
                if (auto origin = Base::freecad_dynamic_cast<App::Origin>(obj))
                    origin->getX(); // make sure all origin features are created
            }}
        }}
    }});''')
        cog.out(f'''
    {auto_comment()}''')
        param._init_pref_widget()

    def retranslate(self, param):
        super().retranslate(param)
        cog.out(f'''
    {auto_comment()}
    button{param.widget_name}->setText(tr("Apply to existing objects"));''')


ParamCreateOrigin(_GroupParams['CreateOrigin'])

ParamGroup = (
    ('Group objects', GroupParams.Params),
    ('App::Link', [_LinkParams['ShowElement']]),
)

def declare():
    params_utils.preference_dialog_declare(sys.modules[__name__])

def define():
    params_utils.preference_dialog_define(sys.modules[__name__])
