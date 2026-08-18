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
'''Auto code generator for parameters in Preferences/View
'''
import cog
import inspect, sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamInt, ParamString, ParamUInt, ParamHex, \
                         ParamFloat, ParamProxy, ParamLinePattern, ParamFile, \
                         ParamComboBox, ParamColor, ParamSpinBox, auto_comment

NameSpace = 'Gui'
ClassName = 'ViewParams'
ParamPath = 'User parameter:BaseApp/Preferences/View'
ClassDoc = 'Convenient class to obtain view provider related parameters'
UserOnChange = 'ViewParams::onViewParamChanged(sReason);'

ParamHiddenLineOverrideFaceColor = ParamBool(
        'HiddenLineOverrideFaceColor', True,
        doc="Enable preselection and highlight by specified color.",
        title='Override face color')

ParamHiddenLineOverrideColor = ParamBool(
        'HiddenLineOverrideColor', True,
        doc="Enable selection highlighting and use specified color",
        title='Override line color')

ParamHiddenLineOverrideBackground = ParamBool(
        'HiddenLineOverrideBackground', True,
        title='Override background color')

ParamHiddenLineOverrideTransparency = ParamBool(
        'HiddenLineOverrideTransparency', True,
        "Whether to override transparency of all objects in the scene.",
        title='Override transparency')

DrawStyles = (
    ("As Is", "Display style, normal display mode", 'V,1'),
    ("Points", "Display style, show points only", 'V,2'),
    ("Wireframe", "Display style, show wire frame only", "V,3"),
    ("Hidden Line", "Display style, show hidden line by display object as transparent", "V,4"),
    ("No Shading", "Display style, shading forced off", "V,5"),
    ("Shaded", "Display style, shading force on", "V,6"),
    ("Flat Lines", "Display style, show both wire frame and face with shading", "V,7"),
    ("Tessellation", "Display style, show tessellation wire frame", "V,8"),
    # No "Shadow" entry: shadows are the renderer's scene light and its
    # map (Render_Light / Render_Shadow, the Shading section of the
    # display style drop-down), not a display style that swallows the
    # one you were looking at. docs/CoinRetirement.md stage 4e.
    #
    # It was the LAST entry, which is the only reason removing it
    # renumbers nothing: App::PropertyEnumeration persists as an index,
    # so dropping any other name would silently restyle every saved
    # document. Keep it that way -- add new styles at the end, and see
    # kLegacyShadowDrawStyle in View3DInventorViewer.cpp, which asserts
    # this list's length because a document may still hold index 8.
)

PreSelectionToolTipCorners = (
    'Top Left',
    'Top Right',
    'Bottom Left',
    'Bottom Right',
)

DrawStyleSync = (
    ("None", "No change to opened document"),
    ("Apply to active view", "Auto apply changed setting to the current active view"),
    ("Apply to active document", "Auto apply changed setting to all views of the current active document"),
    ("Apply to all open documents", "Auto apply changed setting to all opened documents"),
)

AnimationCurveTypes = (
    "Linear",
    "InQuad",
    "OutQuad",
    "InOutQuad",
    "OutInQuad",
    "InCubic",
    "OutCubic",
    "InOutCubic",
    "OutInCubic",
    "InQuart",
    "OutQuart",
    "InOutQuart",
    "OutInQuart",
    "InQuint",
    "OutQuint",
    "InOutQuint",
    "OutInQuint",
    "InSine",
    "OutSine",
    "InOutSine",
    "OutInSine",
    "InExpo",
    "OutExpo",
    "InOutExpo",
    "OutInExpo",
    "InCirc",
    "OutCirc",
    "InOutCirc",
    "OutInCirc",
    "InElastic",
    "OutElastic",
    "InOutElastic",
    "OutInElastic",
    "InBack",
    "OutBack",
    "InOutBack",
    "OutInBack",
    "InBounce",
    "OutBounce",
    "InOutBounce",
    "OutInBounce",
)

class ParamAnimationCurve(ParamProxy):
    WidgetType = 'Gui::PrefComboBox'

    def widget_setter(self, _param):
        return None

    def init_widget(self, param, row, group_name):
        super().init_widget(param, row, group_name)
        cog.out(f'''
    {auto_comment()}
    for (const auto &item : ViewParams::AnimationCurveTypes)
        {param.widget_name}->addItem(item);''')
        cog.out(f'''
    {param.widget_name}->setCurrentIndex({param.namespace}::{param.class_name}::default{param.name}());''')

Params = [
    ParamBool('UseNewSelection', True),
    ParamBool('UseSelectionRoot', True),
    ParamBool('EnableSelection', True,
        title='Enable selection',
        doc='Enable selection, highlighted with specified color'),
    ParamBool('EnablePreselection', True,
        title='Enable preselection',
        doc='Enable preselection, highlighted with specified color'),
    ParamInt('RenderCache', 3, on_change=True,
        doc="Which render path draws a 3D view: 0 auto, 1 distributed,\n"
        "2 centralized Coin caching, 3 the render cache that feeds the\n"
        "render engine. NOT a user setting -- the path is chosen at\n"
        "startup (RenderParams::selectRenderPath), which overrides\n"
        "whatever a config carries. Set it at runtime to compare paths."),
    ParamBool('RandomColor', False),
    ParamHex('BoundingBoxColor', 0xffffffff),
    ParamHex('AnnotationTextColor', 0xffffffff),
    ParamHex('HighlightColor', 0xe1e114ff,
        doc='Pre-selection highlight color', no_label=True),
    ParamHex('SelectionColor', 0x1cad1cff,
        doc='Selection highlight color', no_label=True),
    ParamInt('MarkerSize', 9),
    ParamHex('DefaultLinkColor', 0x66FFFFFF),
    ParamHex('DefaultShapeLineColor', 0x191919FF),
    ParamHex('DefaultShapeVertexColor', 0x191919FF),
    ParamHex('DefaultShapeColor', 0xCCCCCCFF),
    ParamInt('DefaultShapeTransparency', 0),
    ParamInt('DefaultShapeLineWidth', 2),
    ParamInt('DefaultShapePointSize', 2),
    ParamBool('CoinCycleCheck', True),
    ParamBool('EnablePropertyViewForInactiveDocument', True),
    ParamBool('ShowSelectionBoundingBox', False,
        doc='Show selection bounding box instead of highlight'),
    ParamInt('ShowSelectionBoundingBoxThreshold', 0,
       doc="Threshold for showing bounding box instead of selection highlight"),
    ParamBool('UpdateSelectionVisual', True),
    ParamBool('LinkChildrenDirect', True),
    ParamBool('ShowSelectionOnTop', True, on_change=True, doc='Show selection always on top'),
    ParamBool('ShowPreSelectedFaceOnTop', True, doc="Show pre-selected face always on top"),
    ParamBool('ShowPreSelectedFaceOutline', True, doc="Show pre-selected face outline"),
    ParamBool('ShowSelectedFaceOutline', True, doc="Show selected face outline"),
    ParamFloat('OutlineThicken', 4,
       title='Outline width multiplier',
       doc="Muplication factor to increase outline width of the selected face."),
    ParamBool('NoSelFaceHighlightWithOutline', False,
        title='No face selection highlight with outline',
        doc='Do not highlight selected face if outline is enabled'),
    ParamBool('NoPreSelFaceHighlightWithOutline', True,
        title='No face pre-selection highlight with outline',
        doc='Do not highlight pre-selected face if outline is enabled'),
    ParamBool('AutoTransparentPick', False, "Make pre-selected object transparent for picking hidden lines"),
    ParamBool('SelectElementOnTop', False,
       "Do box/lasso element selection on already selected objects if SelectionOnTop is enabled."),
    ParamFloat('TransparencyOnTop', 0.5,
       title='Transparency',
       doc="Transparency for the selected object when being shown on top."),
    ParamInt('HiddenLineSync', 1,
        title='Synchronize', doc="Specifies how to sync hidden line display style settings to opened document",
        proxy=ParamComboBox(items=[(item[0], item[1]) for item in DrawStyleSync])),
    ParamBool('HiddenLineSelectionOnTop', True,
       "Enable hidden line/point selection when SelectionOnTop is active."),
    ParamBool('PartialHighlightOnFullSelect', False,
       "Enable partial highlight on full selection for object that supports it."),
    ParamFloat('SelectionLineThicken',  1.5,
       title='Line width multiplier',
       doc="Muplication factor to increase the width of the selected line."),
    ParamFloat('SelectionLineMaxWidth',  4.0,
       title='Maximum line width',
       doc="Limit the selected line width when applying line thickening."),
    ParamFloat('SelectionPointScale',  2.5,
       title='Point size multiplier',
       doc="Muplication factor to increase the size of the selected point.\n"
       "If zero, then use line multiplication factor."),
    ParamFloat('SelectionPointMaxSize',  6.0,
       title='Maximum point size',
       doc="Limit the selected point size when applying size scale."),
    ParamFloat('PickRadius', 5.0,
        title='Pick radius (px)',
        doc='Area for picking elements in 3D view. Larger value make it easy to pick things,\n'
            'but can also make small features impossible to select.',
        proxy=ParamSpinBox(0.5, 200.0, 1.0, 1)),
    ParamFloat('TouchLoupeLift', 28.0,
        title='Touch loupe lift (px)',
        doc='How far above the fingertip the touch loupe picks, in CSS pixels.\n'
            'The pick ring and its centre dot sit this far above the contact\n'
            'point so the finger never covers what it is aiming at.',
        proxy=ParamSpinBox(0.0, 200.0, 1.0, 1)),
    ParamFloat('SelectionTransparency', 0.5),
    ParamInt('SelectionLinePattern', 0, title='Selected hidden line pattern', proxy=ParamLinePattern()),
    ParamInt('SelectionLinePatternScale', 1, title='Selected line pattern scale'),
    ParamFloat('SelectionHiddenLineWidth', 1.0,
        title='Selected hidden line width',
        doc="Width of the hidden line."),
    ParamFloat('SelectionBBoxLineWidth', 3.0),
    ParamBool('ShowHighlightEdgeOnly', False,
       "Show pre-selection highlight edge only"),
    ParamFloat('PreSelectionDelay', 0.1),
    ParamInt('PickBackFaceDelay', 2),
    ParamBool('UseNewRayPick', True),
    ParamFloat('ViewSelectionExtendFactor', 0.5),
    ParamBool('UseTightBoundingBox', True,
        "Show more accurate bounds when using bounding box selection style"),
    ParamBool('UseBoundingBoxCache', True),
    ParamBool('RenderProjectedBBox', True,
        "Show projected bounding box that is aligned to axes of\n"
        "global coordinate space"),
    ParamBool('SelectionFaceWire', False,
        "Show hidden tirangulation wires for selected face"),
    ParamFloat('NewDocumentCameraScale', 100.0),
    ParamInt('MaxOnTopSelections', 100),
    ParamInt('MaxViewSelections', 100),
    ParamInt('MaxSelectionNotification', 100),
    ParamBool('MapChildrenPlacement', False, on_change=True, doc=
        "Map child object into parent's coordinate space when showing on top.\n"
        "Note that once activated, this option will also activate option ShowOnTop.\n"
        "WARNING! This is an experimental option. Please use with caution."),
    ParamFloat('EditingTransparency', 0.5,
        "Automatically make all object transparent except the one in edit"),
    ParamFloat('DraggerScale', 0.03,
        title='Transform dragger scale',
        doc="Size of the transform dragger relative to the viewport."),
    ParamFloat('HiddenLineTransparency', 0.4,
        doc="Overridden transparency value of all objects in the scene.",
        proxy=ParamProxy(ParamHiddenLineOverrideTransparency)),
    ParamHiddenLineOverrideTransparency,
    ParamHex('HiddenLineFaceColor', 0xffffffff, proxy=ParamColor(ParamHiddenLineOverrideFaceColor)),
    ParamHiddenLineOverrideFaceColor,
    ParamHex('HiddenLineColor', 0x000000ff, proxy=ParamColor(ParamHiddenLineOverrideColor)),
    ParamHiddenLineOverrideColor,
    ParamHex('HiddenLineBackground', 0xffffffff, proxy=ParamColor(ParamHiddenLineOverrideBackground)),
    ParamHiddenLineOverrideBackground,
    ParamBool('HiddenLineShaded',  False, title='Shaded',
        doc='Whether to enable shading in hidden line display style'),
    ParamBool('HiddenLineShowOutline',  True,
        "Show outline in hidden line display style (only works in experiemental renderer),.",
        title='Draw outline'),
    ParamBool('HiddenLinePerFaceOutline',  False,
        "Render per face outline in hidden line display style (Warning! this may cause slow down),.",
        title='Draw per face outline'),
    ParamBool('HiddenLineSceneOutline',  False,
        "Render outline of the whole scene.", title='Draw scene outline'),
    ParamFloat('HiddenLineOutlineWidth',  0.0, title='Outline width',
        proxy=ParamSpinBox(0.0, 100.0, 0.5)),
    ParamFloat('HiddenLineWidth',  1.5, title='Line width'),
    ParamFloat('HiddenLinePointSize',  2, title='Point size'),
    ParamBool('HiddenLineHideSeam',  True,
        "Hide seam edges in hidden line display style.",
        title='Hide seam edge'),
    ParamBool('HiddenLineHideVertex',  True,
        "Hide vertex in hidden line display style.",
        title='Hide vertex'),
    ParamBool('HiddenLineHideFace', False,
       "Hide face in hidden line display style.",
       title='Hide face'),
    ParamInt('StatusMessageTimeout',  5000),
    ParamInt('ShadowSync', 1,
       title='Synchronize', doc="Specifies how to sync shadow display style settings to opened document",
        proxy=ParamComboBox(items=[(item[0], item[1]) for item in DrawStyleSync])),
    ParamBool('ShadowFlatLines',  True,
       "Draw object with 'Flat lines' style when shadow is enabled."),
    ParamInt('ShadowDisplayMode',  2,
       "Override view object display mode when shadow is enabled.",
       title='Override display mode', property_type='PropertyEnumeration',
       proxy=ParamComboBox(items=['Flat Lines', 'Shaded', 'As Is'])),
    ParamBool('ShadowSpotLight',  False,
       doc="Whether to use spot light or directional light.",
       title='Use spot light'),
    ParamFloat('ShadowLightIntensity',  0.8, title='Light intensity'),
    ParamFloat('ShadowLightDirectionX',  -1.0),
    ParamFloat('ShadowLightDirectionY',  -1.0),
    ParamFloat('ShadowLightDirectionZ',  -1.0),
    ParamHex('ShadowLightColor',  0xf0fdffff, title='Light color', proxy=ParamColor()),
    ParamBool('ShadowShowGround',  True,
       "Whether to show auto generated ground face. You can specify you own ground\n"
       "object by changing its view property 'ShadowStyle' to 'Shadowed', meaning\n"
       "that it will only receive but not cast shadow.",
       title='Show ground'),
    ParamBool('ShadowGroundBackFaceCull',  True,
       "Whether to show the ground when viewing from under the ground face",
       title='Ground back face culling'),
    ParamFloat('ShadowGroundScale',  2.0,
       "The auto generated ground face is determined by the scene bounding box\n"
       "multiplied by this scale",
       title='Ground scale', proxy=ParamSpinBox(0.0, 1e7, 0.5)),
    ParamHex('ShadowGroundColor',  0x7d7d7dff, title='Ground color', proxy=ParamColor()),
    ParamString('ShadowGroundBumpMap', '', title='Ground bump map', proxy=ParamFile()),
    ParamString('ShadowGroundTexture', '', title='Ground texture', proxy=ParamFile()),
    ParamFloat('ShadowGroundTextureSize',  100.0,
       "Specifies the physcal length of the ground texture image size.\n"
       "Texture mappings beyond this size will be wrapped around",
       title='Ground texture size', proxy=ParamSpinBox(0.0, 1e7, 10.0)),
    ParamFloat('ShadowGroundTransparency',  0.0,
       "Specifics the ground transparency. When set to 0, the non-shadowed part\n"
       "of the ground will be complete transparent, showing only the shadowed part\n"
       "of the ground with some transparency.",
       title='Ground transparency', proxy=ParamSpinBox(0.0, 1.0, 0.1)),
    ParamBool('ShadowGroundShading',  True,
        "Render ground with shading. If disabled, the ground and the shadow casted\n"
        "on ground will not change shading when viewing in different angle.",
        title='Ground shading'),
    ParamBool('ShadowExtraRedraw',  True),
    ParamInt('ShadowSmoothBorder',  40,
        "Specifies the blur raidus of the shadow edge. Higher number will result in\n"
        "slower rendering speed on scene change. Use a lower 'Precision' value to\n"
        "counter the effect.",
        title='Smooth border'),
    ParamInt('ShadowSpreadSize',  0,
        "Specifies the spread size for a soft shadow. The resulting spread size is\n"
        "dependent on the model scale",
        title='Spread size', proxy=ParamSpinBox(0, 1e7, 500)),
    ParamInt('ShadowSpreadSampleSize',  0,
        "Specifies the sample size used for rendering shadow spread. A value 0\n"
        "corresponds to a sampling square of 2x2. And 1 corresponds to 3x3, etc.\n"
        "The bigger the size the slower the rendering speed. You can use a lower\n"
        "'Precision' value to counter the effect.",
        title='Spread sample size'),
    ParamFloat('ShadowPrecision',  1.0,
        "Specifies shadow precision. This parameter affects the internal texture\n"
        "size used to hold the casted shadows. You might want a bigger texture if\n"
        "you want a hard shadow but a smaller one for soft shadow.",
        title='Precision', proxy=ParamSpinBox(0.0, 1.0, 0.1)),
    ParamFloat('ShadowEpsilon',  1e-5,
        "Epsilon is used to offset the shadow map depth from the model depth.\n"
        "Should be set to as low a number as possible without causing flickering\n"
        "in the shadows or on non-shadowed objects.",
        title='Epsilon', proxy=ParamSpinBox(0.0, 1.0, 1e-5, 10)),
    ParamFloat('ShadowEpsilonMinimum',  1e-6,
        "Lower bound enforced on the shadow Epsilon (both the per-view\n"
        "Shadow_Epsilon property constraint and the render-cache backend).\n"
        "The variance shadow map needs a small non-zero epsilon or its\n"
        "Chebyshev bound is numerically unstable and speckles the\n"
        "self-shadowed side of curved surfaces. Zero disables the floor.",
        title='Epsilon minimum', proxy=ParamSpinBox(0.0, 1.0, 1e-6, 10)),
    ParamFloat('ShadowThreshold',  0.0,
        "Can be used to avoid light bleeding in merged shadows cast from different objects.",
        title='Threshold', proxy=ParamSpinBox(0.0, 1.0, 0.1)),
    ParamFloat('ShadowBoundBoxScale',  1.2,
        "Scene bounding box is used to determine the scale of the shadow texture.\n"
        "You can increase the bounding box scale to avoid execessive clipping of\n"
        "shadows when viewing up close in certain angle.",
        title='Bounding box scale', proxy=ParamSpinBox(0.0, 1e7, 0.5)),
    ParamFloat('ShadowMaxDistance',  0.0,
        "Specifics the clipping distance for when rendering shadows.\n"
        "You can increase the bounding box scale to avoid execessive\n"
        "clipping of shadows when viewing up close in certain angle.",
        title='Maximum distance', proxy=ParamSpinBox(0.0, 1e7, 0.5)),
    ParamBool('ShadowTransparentShadow',  False,
        "Whether to cast shadow from transparent objects.",
        title='Transparent shadow'),
    ParamBool('ShadowUpdateGround',  True,
        "Auto update shadow ground on scene changes. You can manually\n"
        "update the ground by using the 'Fit view' command",
        title='Update ground on scene change'),
    ParamUInt('PropertyViewTimer',  100),
    ParamBool('HierarchyAscend',  False,
        "Enable selection of upper hierarchy by repeatedly click some already\n"
        "selected sub-element."),
    ParamInt('CommandHistorySize',  20, "Maximum number of commands saved in history"),
    ParamInt('PieMenuIconSize',  24, "Pie menu icon size", title='Icon size', proxy=ParamSpinBox(0, 64, 1)),
    ParamInt('PieMenuRadius',  100, "Pie menu radius", title='Radius', proxy=ParamSpinBox(10, 500, 10)),
    ParamInt('PieMenuTriggerRadius',  60, "Pie menu hover trigger radius", title='Trigger radius', proxy=ParamSpinBox(10, 500, 10)),
    ParamInt('PieMenuFontSize',  0, "Pie menu font size", title='Font size', proxy=ParamSpinBox(0, 32, 1)),
    ParamInt('PieMenuTriggerDelay',  200,
        "Pie menu sub-menu hover trigger delay, 0 to disable", title="Trigger delay (ms)", proxy=ParamSpinBox(0, 10000, 100)),
    ParamBool('PieMenuTriggerAction',  False, "Pie menu action trigger on hover", title='Trigger action'),
    ParamInt('PieMenuAnimationDuration',  250, "Pie menu animation duration, 0 to disable", title="Animation duration (ms)", proxy=ParamSpinBox(0, 5000, 100)),
    ParamInt('PieMenuAnimationCurve',  38, "Pie menu animation curve type", title='Animation curve type', proxy=ParamAnimationCurve()),
    ParamInt('PieMenuCenterRadius',  10, "Pie menu center circle radius, 0 to disable", title='Center radius', proxy=ParamSpinBox(0, 250, 1)),
    ParamBool('PieMenuPopup',  False,
        "Show pie menu as a popup widget, disable it to work around some graphics driver problem", title='Show pie menu as popup'),
    ParamBool('StickyTaskControl',  True,
        "Makes the task dialog buttons stay at top or bottom of task view."),
    ParamBool('ColorOnTop',  True, "Show object on top when editing its color."),
    ParamBool('AutoSortWBList',  False, "Sort workbench entries by their names in the combo box."),
    ParamInt('MaxCameraAnimatePeriod',  3000, "Maximum camera move animation duration in milliseconds."),
    ParamBool('TaskNoWheelFocus',  True,
        "Do not accept wheel focus on input fields in task panels."),
    ParamBool('GestureLongPressRotationCenter',  False,
        "Set rotation center on press in gesture navigation mode."),
    ParamBool('CheckWidgetPlacementOnRestore',  True,
        "Check widget position and size on restore to make sure it is within the current screen."),
    ParamInt('TextCursorWidth',  1, on_change=True, doc="Text cursor width in pixel.", title='Text cursor width', proxy=ParamSpinBox(1, 100, 1)),
    ParamInt('PreselectionToolTipCorner',  3,
        title='Corner',
        doc="Preselection tool tip docking corner.",
        proxy=ParamComboBox(items=PreSelectionToolTipCorners)),
    ParamInt('PreselectionToolTipOffsetX',  0,
        title='Offset X',
        doc="Preselection tool tip x offset relative to its docking corner.",
        proxy=ParamSpinBox(0, 4000, 1)),
    ParamInt('PreselectionToolTipOffsetY',  0,
        title='Offset Y',
        doc="Preselection tool tip y offset relative to its docking corner.",
        proxy=ParamSpinBox(0, 4000, 1)),
    ParamInt('PreselectionToolTipFontSize',  0,
        title='Font size',
        doc="Preselection tool tip font size. Set to 0 to use system default.",
        proxy=ParamSpinBox(0, 100, 1)),
    ParamBool('SectionFill',  True, "Fill cross section plane."),
    ParamBool('SectionFillInvert',  True, "Invert cross section plane fill color."),
    ParamBool('SectionConcave',  False, "Cross section in concave."),
    ParamBool('NoSectionOnTop',  True, "Ignore section clip planes when rendering on top."),
    ParamFloat('SectionHatchTextureScale',  1.0, "Section filling texture image scale."),
    ParamString('SectionHatchTexture',  ":icons/section-hatch.png", on_change=True,
        doc="Section filling texture image path."),
    ParamBool('SectionHatchTextureEnable',  True, "Enable section fill texture."),
    ParamBool('SectionFillGroup',  False,
        "Render cross section filling of objects with similar materials together.\n"
        "Intersecting objects will act as boolean cut operation"),
    ParamBool('ShowClipPlane',  False,  "Show clip plane"),
    ParamFloat('ClipPlaneSize',  40.0,  "Clip plane visual size"),
    ParamString('ClipPlaneColor',  "cyan",  "Clip plane color"),
    ParamFloat('ClipPlaneLineWidth',  2.0,  "Clip plane line width"),
    ParamBool('TransformOnTop',  True),
    ParamFloat('SelectionColorDifference',  25.0,
        doc="Color difference threshold for auto making distinct\n"
            "selection highlight color",
        proxy=ParamSpinBox(0, 100, 1, 1)),
    ParamInt('RenderCacheMergeCount',  0,
        "Merge draw caches of multiple objects to reduce number of draw\n"
        "calls and improve render performance. Set zero to disable. Only\n"
        "effective when using experimental render cache."),
    ParamInt('RenderCacheMergeCountMin',  10, "Internal use to limit the render cache merge count"),
    ParamInt('RenderCacheMergeCountMax',  0, "Maximum draw crash merges on any hierarchy. Zero means no limit."),
    ParamInt('RenderCacheMergeDepthMax',  -1,
        "Maximum hierarchy depth that the cache merge can happen. Less than 0 means no limit."),
    ParamInt('RenderCacheMergeDepthMin',  1,
        "Minimum hierarchy depth that the cache merge can happen."),
    ParamInt('RenderCacheKeepMax',  32,
        "Largest vertex cache map, in entries, that an object keeps after\n"
        "its parent has copied it up. A parent flattens its children into\n"
        "one map and then drops theirs, so the next frame re-derives the\n"
        "map of every object in the scene however little moved; keeping the\n"
        "small ones costs a few entries of memory each and is what stops\n"
        "that. The large ones are the copies of whole subtrees, which is the\n"
        "memory this bounds. Set zero to keep none."),
    ParamInt('RenderCacheIncremental',  1,
        "Splice a rebuilt object's flattened vertex cache map from the map\n"
        "of the publish before it, instead of merging every child again. A\n"
        "container holding thousands of objects re-derives all of them on\n"
        "every publish however few moved, and the merge is priced per child\n"
        "rather than per entry. It costs memory, because the map of the\n"
        "previous publish has to survive the traversal that replaces it:\n"
        "on a 17800-object assembly, 49MB against 45% off the flatten.\n"
        "0 rebuilds (the old behaviour), 1 splices, 2 splices and also\n"
        "rebuilds wholesale to compare the two, logging any disagreement\n"
        "-- slow, for checking the splice, not for use."),
    ParamInt('RenderCacheMeshReuse',  1,
        "Reuse the mesh a vertex cache was translated into for the backend,\n"
        "instead of translating it again on every publish. A vertex cache is\n"
        "built once and never changed afterwards -- a shape whose geometry\n"
        "moves gets a new cache -- so the translation is the same work every\n"
        "time, and on a large assembly it is the largest single cost of a\n"
        "publish. Meshes are held only for as long as some draw list still\n"
        "refers to them. 0 translates every publish (the old behaviour), 1\n"
        "reuses, 2 reuses and also translates afresh to compare the two,\n"
        "logging any disagreement -- slow, for checking the reuse, not for\n"
        "use."),
    ParamInt('LiveImportRedrawInterval',  200,
        "Minimum interval in milliseconds between 3D view redraws while a\n"
        "progressive import is filling the document, and the window after\n"
        "any mouse input during which redraws are never held back. Set zero\n"
        "to redraw on every change."),
    ParamInt('LiveImportRedrawBudget',  10,
        "Percentage of the time the 3D view may spend redrawing while a\n"
        "progressive import is filling the document. Each new object makes\n"
        "the next frame rebuild the render cache of the whole scene, so on a\n"
        "large import a single frame costs far more than the objects drawn\n"
        "in it; keeping frames to a share of the time is what bounds that\n"
        "cost. The resulting wait scales with the measured frame cost, is\n"
        "never shorter than LiveImportRedrawInterval nor longer than ten\n"
        "times it, and mouse input renders immediately regardless. Set zero\n"
        "to budget nothing and use the plain interval."),
    # The experimental render engine parameters (former Renderer* keys)
    # live in RenderParams.py (Preferences/View/Render); see
    # RenderParams::migrate() for the key migration.
    ParamFloat('RenderHighlightPolygonOffsetFactor', 1),
    ParamFloat('RenderHighlightPolygonOffsetUnits', 1),
    ParamBool('ForceSolidSingleSideLighting',  True, on_change=True, title='Force single side lighting on solid',
        doc="Force single side lighting on solid. This can help visualizing invalid\n"
        "solid shapes with flipped normals."),
    ParamInt('DefaultFontSize',  0, on_change=True),
    ParamBool('EnableTaskPanelKeyTranslate',  False, on_change=True),
    ParamBool('EnableMenuBarCheckBox',  'FC_ENABLE_MENUBAR_CHECKBOX'),
    ParamBool('EnableBacklight',  False),
    ParamHex('BacklightColor',  0xffffffff),
    ParamInt('BacklightIntensity',  100,
        "Backlight intensity, as a percentage. An integer because that is the\n"
        "slot everything else uses: the Clipping dialog's slider, the 3D view\n"
        "preference page and the viewer, which divides it by a hundred. This\n"
        "class used to read a Float fraction from the same name -- a second,\n"
        "separate slot that nothing ever wrote; see ViewParams::migrate()."),
    ParamBool('OverrideSelectability',  False, "Override object selectability to enable selection"),
    ParamUInt('SelectionStackSize', 30, "Maximum selection history record size"),
    ParamInt('DefaultDrawStyle', 0, 'Default display style of a new document',
        title='Default display style',
        proxy=ParamComboBox(items=[(item[0], item[1]) for item in DrawStyles])),
    ParamInt('ToolTipIconSize', 64,
        title="Tool tip icon size",
        doc='Specifies the size of static icon image in tooltip. GIF animation\n'
            'will be shown in its original size. You can disable all images in\n'
            'the tooltip by setting this option to zero.',
        proxy=ParamSpinBox(0, 512, 10)),
    ParamBool('ToolTipDisable', False),
    ParamHex('AxisXColor', 0xCC333300),
    ParamHex('AxisYColor', 0x33CC3300),
    ParamHex('AxisZColor', 0x3333CC00),
]

def declare_begin():
    cog.out(f'''
{auto_comment()}
#include <QString>
''')

    params_utils.declare_begin(sys.modules[__name__])
    cog.out(f'''
    {auto_comment()}
    static const std::vector<QString> AnimationCurveTypes;

    static void onViewParamChanged(const char *sReason);

    /// One-time migration of keys that changed type or name.
    static void migrate();
''')

def declare_end():
    params_utils.declare_end(sys.modules[__name__])

    cog.out(f'''
{auto_comment()}
namespace {NameSpace} {{
/// Obtain all display style names, terminated by nullptr entry.
{NameSpace}Export const char **drawStyleNames();
/// Obtain display style name from index. Returns nullptr if out of range.
{NameSpace}Export const char *drawStyleNameFromIndex(int index);
/// Obtain display style index from name. Returns -1 for invalid name.
{NameSpace}Export int drawStyleIndexFromName(const char *);
/// Obtain documentation of a display style.
{NameSpace}Export const char *drawStyleDocumentation(int index);
}} // namespace Gui
''')

def define():
    params_utils.define(sys.modules[__name__])
    cog.out(f'''
{auto_comment()}
const std::vector<QString> ViewParams::AnimationCurveTypes = {{''')
    for item in AnimationCurveTypes:
        cog.out(f'''
    QStringLiteral("{item}"),''')
    cog.out(f'''
}};

{auto_comment()}
static const char *DrawStyleNames[] = {{''')
    for item in DrawStyles:
        cog.out(f'''
    QT_TRANSLATE_NOOP("DrawStyle", "{item[0]}"),''')
    cog.out(f'''
    nullptr,
}};
''')
    cog.out(f'''
{auto_comment()}
static const char *DrawStyleDocs[] = {{''')
    for item in DrawStyles:
        cog.out(f'''
    QT_TRANSLATE_NOOP("DrawStyle", "{item[1]}"),''')
    cog.out(f'''
}};
''')
    cog.out(f'''
namespace Gui {{
{auto_comment()}
const char **drawStyleNames()
{{
    return DrawStyleNames;
}}
''')
    cog.out(f'''
{auto_comment()}
const char *drawStyleNameFromIndex(int i)
{{
    if (i < 0 || i>= {len(DrawStyles)})
        return nullptr;
    return DrawStyleNames[i];
}}
''')
    cog.out(f'''
{auto_comment()}
int drawStyleIndexFromName(const char *name)
{{
    if (!name)
        return -1;
    for (int i=0; i< {len(DrawStyles)}; ++i) {{
        if (strcmp(name, DrawStyleNames[i]) == 0)
            return i;
    }}
    return -1;
}}
''')
    cog.out(f'''
{auto_comment()}
const char *drawStyleDocumentation(int i)
{{
    if (i < 0 || i>= {len(DrawStyles)})
        return "";
    return DrawStyleDocs[i];
}}

}} // namespace Gui
''')

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
