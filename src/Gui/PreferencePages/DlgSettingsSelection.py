# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2024 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
'''Auto code generator for preference page of tree and 3D view selection
'''
import cog, sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Tools'))
import params_utils
from params_utils import auto_comment

sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Gui'))
import ViewParams, TreeParams

Title = 'Selection'
NameSpace = 'Gui'
ClassName = 'DlgSettingsSelection'
ClassDoc = 'Preference dialog for various tree and 3D view selection related settings'

_ViewParams = { param.name : param for param in ViewParams.Params }
_TreeParams = { param.name : param for param in TreeParams.Params }

_ViewParams['EnablePreselection'] = [_ViewParams[name] for name in (
    'EnablePreselection',
    'HighlightColor',
)]

_ViewParams['EnableSelection'] = [_ViewParams[name] for name in (
    'EnableSelection',
    'SelectionColor',
)]

_ViewParams['PreselectionToolTipOffsetX'] = [_ViewParams[name] for name in (
    'PreselectionToolTipOffsetX',
    'PreselectionToolTipOffsetY',
)]

ParamGroup = (
    ('Tree View Selection', [_TreeParams[name] for name in (
        'SyncView',
        'SyncSelection',
        'CheckBoxesSelection',
        'RecordSelection',
        'PreSelection',
    )]),

    ('3D View Selection', [_ViewParams[name] for name in (
        'EnablePreselection',
        'EnableSelection',
        'PickRadius',
        'ShowSelectionOnTop',
        'ShowPreSelectedFaceOnTop',
        'ShowSelectionBoundingBox',
        'ShowSelectionBoundingBoxThreshold',
        'HiddenLineSelectionOnTop',
        'SelectElementOnTop',
        'SelectionColorDifference'
    )]),

    ('Pre-selection Tool Tip', [_ViewParams[name] for name in (
        'PreselectionToolTipCorner',
        'PreselectionToolTipOffsetX',
        'PreselectionToolTipFontSize',
    )]),
)

def declare():
    params_utils.preference_dialog_declare(sys.modules[__name__])

def define():
    params_utils.preference_dialog_define(sys.modules[__name__])
