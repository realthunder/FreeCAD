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
'''Auto code generator for preference page of Display/Draw styles
'''
import sys
import cog
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Tools'))
import params_utils
from params_utils import auto_comment

sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Gui'))
import ViewParams

Title = 'Draw styles'
NameSpace = 'Gui'
ClassName = 'DlgSettingsDrawStyles'
ClassDoc = 'Preference dialog for various draw styles related settings'
UserInit = 'Active = true;'
UserFini = 'Active = false;'

_ViewParams = { param.name : param for param in ViewParams.Params }

HiddenLineParams = ('Hidden Lines', [_ViewParams[name] for name in (
        'HiddenLineSync',
        'HiddenLineFaceColor',
        'HiddenLineColor',
        'HiddenLineBackground',
        'HiddenLineShaded',
        'HiddenLineShowOutline',
        'HiddenLinePerFaceOutline',
        'HiddenLineSceneOutline',
        'HiddenLineOutlineWidth',
        'HiddenLineHideFace',
        'HiddenLineHideSeam',
        'HiddenLineHideVertex',
        'HiddenLineTransparency',
        'HiddenLineWidth',
        'HiddenLinePointSize',
    )],

    'HiddenLine',
)

ShadowParams = ('Shadow', [_ViewParams[name] for name in (
        'ShadowSync',
        'ShadowSpotLight',
        'ShadowLightColor',
        'ShadowLightIntensity',
        'ShadowShowGround',
        'ShadowGroundBackFaceCull',
        'ShadowGroundColor',
        'ShadowGroundScale',
        'ShadowGroundTransparency',
        'ShadowGroundTexture',
        'ShadowGroundTextureSize',
        'ShadowGroundBumpMap',
        'ShadowGroundShading',
        'ShadowUpdateGround',
        'ShadowDisplayMode',
        'ShadowPrecision',
        'ShadowSmoothBorder',
        'ShadowSpreadSize',
        'ShadowSpreadSampleSize',
        'ShadowEpsilon',
        'ShadowThreshold',
        'ShadowBoundBoxScale',
        'ShadowMaxDistance',
        'ShadowTransparentShadow',
    )],

    'Shadow'
)

ParamGroup = (
    ('General', [_ViewParams[name] for name in (
        'DefaultDrawStyle',
        'ForceSolidSingleSideLighting',
    )]),

    ('Selection', [_ViewParams[name] for name in (
        'TransparencyOnTop',
        'SelectionLineThicken',
        'SelectionLineMaxWidth',
        'SelectionPointScale',
        'SelectionPointMaxSize',
        'SelectionLinePattern',
        'SelectionLinePatternScale',
        'SelectionHiddenLineWidth',
        'OutlineThicken',
    )]),

    HiddenLineParams[:2],
    ShadowParams[:2],
)

def declare_begin():
    params_utils.preference_dialog_declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.preference_dialog_declare_end(sys.modules[__name__])

def define_begin():
    params_utils.preference_dialog_define(sys.modules[__name__])
    cog.out(f'''
{auto_comment()}
bool DlgSettingsDrawStyles::Active;
''')

def define_end():
    cog.out(f'''
{auto_comment()}
void DlgSettingsDrawStyles::onParamChanged(const char *sReason)
{{
    if (!Active)
        return;
''')
    for j, (params, prefix) in enumerate((HiddenLineParams[1:], ShadowParams[1:])):
        cog.out(f'''
    {'else ' if j else ''}if (ViewParams::get{prefix}Sync() != 0 && boost::starts_with(sReason, "{prefix}")) {{
        bool passThrough = boost::equals(sReason+{len(prefix)}, "{prefix}Sync");
''')
        for i, param in enumerate(params[1:]):
            param_name = param.name[len(prefix):]
            cog.out(f'''
        if (passThrough || boost::equals(sReason+{len(prefix)}, "{param_name}")) {{
            setViewProperty<{param.PropertyType}>(ViewParams::get{prefix}Sync(),
                                                  "{prefix}_{param_name}",
                                                  ViewParams::get{param.name}());
            if (!passThrough)
                return;
        }}''')
        cog.out(f'''
    }}''')
    cog.out(f'''
}}
''')
