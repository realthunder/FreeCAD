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
'''Auto code generator for preference page of Display/UI
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
import ViewParams, TreeParams, ExprParams, OverlayParams, OpenViewParams

Title = 'UI'
NameSpace = 'Gui'
ClassName = 'DlgSettingsUI'
ClassDoc = 'Preference dialog for various UI related settings'
UserInit = 'init();'

_ViewParams = { param.name : param for param in ViewParams.Params }
_TreeParams = { param.name : param for param in TreeParams.Params }
_ExprParams = { param.name : param for param in ExprParams.Params }
_OverlayParams = { param.name : param for param in OverlayParams.Params }
_OpenViewParams = { param.name : param for param in OpenViewParams.Params }

ParamGroup = (
    ('General', [_ViewParams[name] for name in (
        'TextCursorWidth',
    )]),

    ('Views', [_ViewParams['UseViewArea']]
              + [_OpenViewParams[name] for name in (
        'DocumentTarget',
        'DocViewTarget',
        'UtilityTarget',
        'SplitDirection',
    )]),

    ('Tree view', [_TreeParams[name] for name in (
        'ItemBackground',
        'ItemBackgroundPadding',
        'ResizableColumn',
        'CheckBoxesSelection',
        'HideColumn',
        'HideScrollBar',
        'HideHeaderView',
        'TreeToolTipIcon',
    )]),

    ('Expression', [_ExprParams[name] for name in (
        'AutoHideEditorIcon',
        'EditorTrigger',
        'NoSystemBackground',
        'EditDialogBGAlpha',
    )]),

    ('Pie menu', [_ViewParams[name] for name in (
        'PieMenuIconSize',
        'PieMenuRadius',
        'PieMenuTriggerRadius',
        'PieMenuCenterRadius',
        'PieMenuFontSize',
        'PieMenuTriggerDelay',
        'PieMenuTriggerAction',
        'PieMenuAnimationDuration',
        'PieMenuAnimationCurve',
        'PieMenuPopup',
    )]),

    ('Overlay', [_OverlayParams[name] for name in (
        'DockOverlayHideTabBar',
        'DockOverlayHidePropertyViewScrollBar',
        'DockOverlayAutoView',
        'DockOverlayAutoMouseThrough',
        'DockOverlayWheelPassThrough',
        'DockOverlayWheelDelay',
        'DockOverlayAlphaRadius',
        'DockOverlayCheckNaviCube',
        'DockOverlayHintTriggerSize',
        'DockOverlayHintSize',
        'DockOverlayHintLeftOffset',
        'DockOverlayHintLeftLength',
        'DockOverlayHintRightOffset',
        'DockOverlayHintRightLength',
        'DockOverlayHintTopOffset',
        'DockOverlayHintTopLength',
        'DockOverlayHintBottomOffset',
        'DockOverlayHintBottomLength',
        'DockOverlayHintTabBar',
        'DockOverlayHintDelay',
        'DockOverlaySplitterHandleTimeout',
        'DockOverlayActivateOnHover',
        'DockOverlayDelay',
        'DockOverlayAnimationDuration',
        'DockOverlayAnimationCurve',
    )]),
)

def declare_begin():
    params_utils.preference_dialog_declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.preference_dialog_declare_end(sys.modules[__name__])

def define():
    params_utils.preference_dialog_define(sys.modules[__name__])
