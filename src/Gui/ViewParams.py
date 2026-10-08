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
        title='Override background color',
                                        doc = "Replace the background of a 3D view with the hidden line background\n"
                                              "colour while the hidden line display style is active.")

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
    ParamBool('UseViewArea', True, title="Tile views inside one tab",
        doc="Host views in a split-capable view area, so that several views\n"
            "can share one tab side by side. Off, every view gets its own tab\n"
            "and the split placement choices below do not apply."),
    ParamBool('UseNewSelection', True,
        doc = "Highlight selection and preselection through the selection root of\n"
              "the 3D view, which allows picking sub-elements and objects inside\n"
              "links. When off, only objects that ask for it are handled that way."),
    ParamBool('UseSelectionRoot', True,
        doc = "Give the visual copy a link makes of its linked object a selection\n"
              "root of its own, so that each link is highlighted separately. When\n"
              "off, a plain group without render caching is used."),
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
    ParamBool('UnifiedCanvas', False, on_change=True,
        title='Unified split-view canvas',
        doc="Draw all the 3D cells of a split view (ViewArea) into ONE\n"
        "canvas widget, as sub-views of a single render backend, instead\n"
        "of composing each cell's own widget. One backend instance and\n"
        "one copy of the GPU scene serve every cell (the browser tier's\n"
        "model). Experimental; needs the render engine (render cache\n"
        "mode 3). See docs/SplitViews.md sec 13."),
    ParamBool('RandomColor', False,
        doc = "Give every new object a random shape colour instead of the default\n"
              "shape colour."),
    ParamHex('BoundingBoxColor', 0xffffffff,
        doc = "Colour of the bounding box drawn for an object that has its bounding\n"
              "box display turned on."),
    ParamHex('AnnotationTextColor', 0xffffffff,
        title = 'Annotation Text Color',
        doc = "Default text colour of new annotation objects."),
    ParamHex('HighlightColor', 0xe1e114ff, proxy=ParamColor(transparency=False),
        doc='Pre-selection highlight color', no_label=True),
    ParamHex('SelectionColor', 0x1cad1cff, proxy=ParamColor(transparency=False),
        doc='Selection highlight color', no_label=True),
    ParamInt('MarkerSize', 7,
        doc = "Size in pixels of the point markers drawn in the 3D view, such as\n"
              "sketch vertices and the end points of a measurement."),
    ParamHex('DefaultLinkColor', 0x66FFFFFF,
        doc = "Default colour of the material of a new link, which is used when the\n"
              "link overrides the material of its linked object."),
    ParamHex('DefaultShapeLineColor', 0x191919FF,
        title = 'Default Shape Line Color',
        doc = "Default line colour of new shapes."),
    ParamHex('DefaultShapeVertexColor', 0x191919FF,
        title = 'Default Shape Vertex Color',
        doc = "Default vertex colour of new shapes."),
    ParamHex('DefaultShapeColor', 0xCCCCE6FF,
        doc = "Default face colour of new shapes. Not used while random colours are\n"
              "turned on."),
    ParamInt('DefaultShapeTransparency', 0,
        doc = "Default transparency of new shapes in percent. 0 is opaque, 100 is\n"
              "fully transparent."),
    ParamInt('DefaultShapeLineWidth', 2,
        title = 'Default Shape Line Width',
        doc = "Default line width of new shapes, in pixels."),
    ParamInt('DefaultShapePointSize', 2,
        title = 'Default Shape Point Size',
        doc = "Default vertex size of new shapes, in pixels."),
    ParamBool('CoinCycleCheck', True,
        doc = "Check the 3D scene for an object that contains itself while the\n"
              "scene is traversed. A cycle is reported and skipped instead of being\n"
              "followed without end."),
    ParamBool('EnablePropertyViewForInactiveDocument', True,
        doc = "Keep the property view usable when the selected objects belong to a\n"
              "document other than the active one. When off, the property view is\n"
              "disabled for such a selection."),
    ParamBool('ShowSelectionBoundingBox', False,
        doc='Show selection bounding box instead of highlight'),
    ParamInt('ShowSelectionBoundingBoxThreshold', 0,
       doc="Threshold for showing bounding box instead of selection highlight"),
    ParamBool('UpdateSelectionVisual', True,
        doc = "Bring back the selection highlight of a selected object when it is\n"
              "shown again after being hidden."),
    ParamBool('LinkChildrenDirect', True,
        doc = "Show the children of a group with its own coordinate system, such as\n"
              "a part or body, through a link view. A link to the group then shows\n"
              "the children's visuals directly."),
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
    ParamFloat('SelectionTransparency', 0.5,
        doc = "Transparency given to a selected face so that what lies behind it\n"
              "stays visible, used when picking through objects and when the\n"
              "highlight is drawn on top. 0 is opaque, 1 is invisible."),
    ParamInt('SelectionLinePattern', 0, title='Selected hidden line pattern', proxy=ParamLinePattern(),
        doc = "Dash pattern of the hidden part of a selected line that is shown on\n"
              "top of the scene, as a 16 bit mask. 0 draws it solid."),
    ParamInt('SelectionLinePatternScale', 1, title='Selected line pattern scale',
        doc = "Number of times each bit of the dash pattern of a selected hidden\n"
              "line is repeated. Larger values give longer dashes. 1 or less uses\n"
              "the pattern as it is."),
    ParamFloat('SelectionHiddenLineWidth', 1.0,
        title='Selected hidden line width',
        doc="Width of the hidden line."),
    ParamFloat('SelectionBBoxLineWidth', 3.0,
        doc = "Line width in pixels of the bounding box drawn around a selected\n"
              "object when selection is shown by bounding box."),
    ParamBool('ShowHighlightEdgeOnly', False,
       "Show pre-selection highlight edge only"),
    ParamFloat('PreSelectionDelay', 0.1,
        doc = "Shortest time in seconds between two preselection picks in the 3D\n"
              "view while the mouse moves. 0 picks on every mouse move."),
    ParamInt('PickBackFaceDelay', 2,
        doc = "Number of mouse wheel steps, with Shift and Ctrl held, that it takes\n"
              "to move the pick one object further behind or back toward the\n"
              "front."),
    ParamBool('UseNewRayPick', True,
        doc = "Stop a single pick in the 3D view at the nearest hit instead of\n"
              "collecting everything along the pick ray. Off is the older and\n"
              "slower way."),
    ParamFloat('ViewSelectionExtendFactor', 0.5,
        doc = "Scale applied to the bounding box of the selection when testing\n"
              "whether it is already in view, before the view is extended to\n"
              "include it. Currently has no effect."),
    ParamBool('UseTightBoundingBox', True,
        "Show more accurate bounds when using bounding box selection style"),
    ParamBool('UseBoundingBoxCache', True,
        doc = "Remember the bounding boxes of objects instead of computing them\n"
              "again on every request."),
    ParamBool('RenderProjectedBBox', True,
        "Show projected bounding box that is aligned to axes of\n"
        "global coordinate space"),
    ParamBool('SelectionFaceWire', False,
        "Show hidden tirangulation wires for selected face"),
    ParamFloat('NewDocumentCameraScale', 100.0,
        doc = "Camera zoom of a new document, as the diameter of the sphere that\n"
              "fits on the screen. A quarter of it is the default size of a new\n"
              "coordinate system."),
    ParamInt('MaxOnTopSelections', 100,
        doc = "Largest number of selected objects that are drawn on top of the\n"
              "scene. A larger selection is highlighted in place, and the tree view\n"
              "does not expand to show its items."),
    ParamInt('MaxViewSelections', 100,
        doc = "Largest number of selected objects taken into account when the view\n"
              "is fitted or aligned to the selection."),
    ParamInt('MaxSelectionNotification', 100,
        doc = "Number of pending add and remove selection notices after which they\n"
              "are replaced by one notice that the whole selection changed. 0 sets\n"
              "no limit."),
    ParamBool('MapChildrenPlacement', False, on_change=True, doc=
        "Map child object into parent's coordinate space when showing on top.\n"
        "Note that once activated, this option will also activate option ShowOnTop.\n"
        "WARNING! This is an experimental option. Please use with caution."),
    ParamFloat('EditingTransparency', 0.5,
        "Automatically make all object transparent except the one in edit"),
    ParamBool('PerViewEdit', False, title='Edit in one view only', doc=
        "Run an edit in the one view it is started in. The other 3D views of\n"
        "the document, and the other viewers of a served one, keep showing the\n"
        "document as it is and take no input for the edit. When off, every view\n"
        "of the document joins the edit and can work in it. Takes effect with\n"
        "the next edit."),
    ParamFloat('DraggerScale', 0.03,
        title='Transform dragger scale',
        doc="Size of the transform dragger relative to the viewport."),
    ParamFloat('HiddenLineTransparency', 0.4,
        doc="Overridden transparency value of all objects in the scene.",
        proxy=ParamProxy(ParamHiddenLineOverrideTransparency)),
    ParamHiddenLineOverrideTransparency,
    ParamHex('HiddenLineFaceColor', 0xffffffff, proxy=ParamColor(ParamHiddenLineOverrideFaceColor),
        doc = "Colour all faces are drawn in by the hidden line display style when\n"
              "it overrides the face colour."),
    ParamHiddenLineOverrideFaceColor,
    ParamHex('HiddenLineColor', 0x000000ff, proxy=ParamColor(ParamHiddenLineOverrideColor),
        doc = "Colour all lines and outlines are drawn in by the hidden line\n"
              "display style when it overrides the line colour."),
    ParamHiddenLineOverrideColor,
    ParamHex('HiddenLineBackground', 0xffffffff, proxy=ParamColor(ParamHiddenLineOverrideBackground),
        doc = "Background colour of a 3D view in the hidden line display style,\n"
              "used when overriding the background is turned on."),
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
        proxy=ParamSpinBox(0.0, 100.0, 0.5),
        doc = "Width in pixels of the outlines drawn by the hidden line display\n"
              "style. 0 uses the default width."),
    ParamFloat('HiddenLineWidth',  1.5, title='Line width',
        doc = "Width in pixels of the lines of all objects in the hidden line\n"
              "display style. A value below 1 keeps each object's own line width."),
    ParamFloat('HiddenLinePointSize',  2, title='Point size',
        doc = "Size in pixels of the vertices of all objects in the hidden line\n"
              "display style. A value below 1 keeps each object's own point size."),
    ParamBool('HiddenLineHideSeam',  True,
        "Hide seam edges in hidden line display style.",
        title='Hide seam edge'),
    ParamBool('HiddenLineHideVertex',  True,
        "Hide vertex in hidden line display style.",
        title='Hide vertex'),
    ParamBool('HiddenLineHideFace', False,
       "Hide face in hidden line display style.",
       title='Hide face'),
    ParamInt('StatusMessageTimeout',  5000,
        doc = "Milliseconds a message stays in the status bar when the command\n"
              "showing it gives no time of its own. 0 keeps it until the next\n"
              "message."),
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
    ParamFloat('ShadowLightIntensity',  0.8, title='Light intensity',
        doc = "Brightness of the light that casts the shadow."),
    ParamFloat('ShadowLightDirectionX',  -1.0,
        title = 'Shadow Light Direction X',
        doc = "X component of the direction of the light that casts the shadow."),
    ParamFloat('ShadowLightDirectionY',  -1.0,
        title = 'Shadow Light Direction Y',
        doc = "Y component of the direction of the light that casts the shadow."),
    ParamFloat('ShadowLightDirectionZ',  -1.0,
        title = 'Shadow Light Direction Z',
        doc = "Z component of the direction of the light that casts the shadow."),
    ParamHex('ShadowLightColor',  0xf0fdffff, title='Light color', proxy=ParamColor(),
        doc = "Colour of the light that casts the shadow."),
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
    ParamHex('ShadowGroundColor',  0x7d7d7dff, title='Ground color', proxy=ParamColor(),
        doc = "Colour of the ground that receives the shadow."),
    ParamString('ShadowGroundBumpMap', '', title='Ground bump map', proxy=ParamFile(),
        doc = "Image file used as a bump map that gives the shadow ground a surface\n"
              "relief. Empty for a flat ground."),
    ParamString('ShadowGroundTexture', '', title='Ground texture', proxy=ParamFile(),
        doc = "Image file drawn as a texture on the ground that receives the\n"
              "shadow. Empty for a ground of plain colour."),
    ParamFloat('ShadowGroundTextureSize',  100.0,
       "Specifies the physcal length of the ground texture image size.\n"
       "Texture mappings beyond this size will be wrapped around",
       title='Ground texture size', proxy=ParamSpinBox(0.0, 1e7, 10.0)),
    ParamFloat('ShadowTransparency',  0.2,
       "How transparent the shadow itself is, where the ground carries the\n"
       "shadow and nothing else (Ground transparency at 1). 0 paints a solid\n"
       "shadow, 1 an invisible one; the unshadowed ground is hidden either\n"
       "way. Coin spelled this SoShadowTransparency and defaulted it to the\n"
       "same 0.2.\n"
       "\n"
       "A drawn ground ignores it -- there the shadow is the ground shaded,\n"
       "and how dark it goes is a matter of the light.",
       title='Shadow transparency', proxy=ParamSpinBox(0.0, 1.0, 0.1)),
    # The long form, kept here; the documentation shown is the short one below.
    # How much of the shadow receiver plane is drawn beside the shadow
    # itself.
    #
    # 1 (the default) is the receiver a view of a part usually wants: the
    # ground carries the shadow and nothing else, so there is no plane in
    # the frame and no horizon behind the model -- only the shadow, at a
    # fixed 0.8 opacity where it is fully dark. Anything below 1 draws a
    # solid ground of that transparency and shades it, which is what a
    # presentation image of a whole scene wants.
    #
    # A ground reflection needs a surface to blend onto, so it keeps the
    # solid ground whatever this says.
    ParamFloat('ShadowGroundTransparency',  1.0,
       "Transparency of the ground that receives the shadow. 1 (the default)\n"
       "draws the shadow only, with no ground plane; lower values draw a\n"
       "shaded ground of that transparency. A ground reflection always draws\n"
       "the ground.",
       title='Ground transparency', proxy=ParamSpinBox(0.0, 1.0, 0.1)),
    ParamBool('ShadowGroundShading',  True,
        "Render ground with shading. If disabled, the ground and the shadow casted\n"
        "on ground will not change shading when viewing in different angle.",
        title='Ground shading'),
    ParamBool('ShadowExtraRedraw',  True,
        doc = "Redraw the 3D view once more after a change while shadows are shown,\n"
              "so that the shadow catches up with the scene. Currently has no\n"
              "effect."),
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
    ParamUInt('PropertyViewTimer',  100,
        doc = "Milliseconds the property view waits before it refreshes after the\n"
              "selection or a property changes."),
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
    ParamBool('TransformOnTop',  True,
        doc = "Show an object on top of the scene while it is moved with the\n"
              "transform dragger. Currently has no effect."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Largest vertex cache map, in entries, that an object keeps after
    # its parent has copied it up. A parent flattens its children into
    # one map and then drops theirs, so the next frame re-derives the
    # map of every object in the scene however little moved; keeping the
    # small ones costs a few entries of memory each and is what stops
    # that. The large ones are the copies of whole subtrees, which is the
    # memory this bounds. Set zero to keep none.
    ParamInt('RenderCacheKeepMax',  32,
        "Largest render cache, in entries, an object keeps after its parent has\n"
        "merged it. Keeping the small ones avoids rebuilding them every frame.\n"
        "0 keeps none."),
    # The long form, kept here; the documentation shown is the short one below.
    # Splice a rebuilt object's flattened vertex cache map from the map
    # of the publish before it, instead of merging every child again. A
    # container holding thousands of objects re-derives all of them on
    # every publish however few moved, and the merge is priced per child
    # rather than per entry. It costs memory, because the map of the
    # previous publish has to survive the traversal that replaces it:
    # on a 17800-object assembly, 49MB against 45% off the flatten.
    # 0 rebuilds (the old behaviour), 1 splices, 2 splices and also
    # rebuilds wholesale to compare the two, logging any disagreement
    # -- slow, for checking the splice, not for use.
    ParamInt('RenderCacheIncremental',  1,
        "Update a container's render cache from its previous one instead of\n"
        "merging all its children again. Faster on large assemblies, at the\n"
        "cost of some memory. 0 off, 1 on, 2 does both and logs any difference\n"
        "(slow, for checking)."),
    # The long form, kept here; the documentation shown is the short one below.
    # Reuse the mesh a vertex cache was translated into for the backend,
    # instead of translating it again on every publish. A vertex cache is
    # built once and never changed afterwards -- a shape whose geometry
    # moves gets a new cache -- so the translation is the same work every
    # time, and on a large assembly it is the largest single cost of a
    # publish. Meshes are held only for as long as some draw list still
    # refers to them. 0 translates every publish (the old behaviour), 1
    # reuses, 2 reuses and also translates afresh to compare the two,
    # logging any disagreement -- slow, for checking the reuse, not for
    # use.
    ParamInt('RenderCacheMeshReuse',  1,
        "Reuse the mesh a render cache was converted to for the render backend\n"
        "instead of converting it at every update. 0 off, 1 on, 2 does both and\n"
        "logs any difference (slow, for checking)."),
    ParamInt('LiveImportRedrawInterval',  200,
        "Minimum interval in milliseconds between 3D view redraws while a\n"
        "progressive import is filling the document, and the window after\n"
        "any mouse input during which redraws are never held back. Set zero\n"
        "to redraw on every change."),
    # The long form, kept here; the documentation shown is the short one below.
    # Percentage of the time the 3D view may spend redrawing while a
    # progressive import is filling the document. Each new object makes
    # the next frame rebuild the render cache of the whole scene, so on a
    # large import a single frame costs far more than the objects drawn
    # in it; keeping frames to a share of the time is what bounds that
    # cost. The resulting wait scales with the measured frame cost, is
    # never shorter than LiveImportRedrawInterval nor longer than ten
    # times it, and mouse input renders immediately regardless. Set zero
    # to budget nothing and use the plain interval.
    ParamInt('LiveImportRedrawBudget',  10,
        "Percentage of time the 3D view may spend redrawing while a progressive\n"
        "import fills the document. The wait between frames is never shorter\n"
        "than LiveImportRedrawInterval nor longer than ten times it. 0 uses the\n"
        "plain interval."),
    # The long form, kept here; the documentation shown is the short one below.
    # Minimum interval in milliseconds between two turns of the event
    # loop while a live import fills the document. The import holds the
    # main thread, so the view only sees input and paints where the
    # import hands the loop a slice, and on its own the progress bar
    # does that on a 200 ms update throttle -- a slideshow to someone
    # orbiting the model. Offering the loop a turn costs nothing when
    # nothing is queued, and what a frame costs is bounded by
    # LiveImportRedrawBudget rather than by how often a turn is
    # offered. Set zero to pump at every offer.
    ParamInt('LiveImportPumpInterval',  50,
        "Minimum milliseconds between two chances for the window to process\n"
        "input and repaint while a live import fills the document. 0 offers one\n"
        "at every opportunity."),
    # The experimental render engine parameters (former Renderer* keys)
    # live in RenderParams.py (Preferences/View/Render); see
    # RenderParams::migrate() for the key migration.
    ParamFloat('RenderHighlightPolygonOffsetFactor', 1,
        doc = "Slope scaled depth offset that pulls selection and preselection\n"
              "highlights toward the viewer, so that the faces under them do not\n"
              "hide them. Preselection gets twice the offset."),
    ParamFloat('RenderHighlightPolygonOffsetUnits', 1,
        doc = "Constant depth offset, in depth buffer units, that pulls selection\n"
              "and preselection highlights toward the viewer, so that the faces\n"
              "under them do not hide them. Preselection gets twice the offset."),
    ParamBool('ForceSolidSingleSideLighting',  True, on_change=True, title='Force single side lighting on solid',
        doc="Force single side lighting on solid. This can help visualizing invalid\n"
        "solid shapes with flipped normals."),
    ParamInt('DefaultFontSize',  0, on_change=True,
        doc = "Point size of the application font. 0 uses the system default.\n"
              "Sizes from 1 to 7 are raised to 8."),
    ParamBool('EnableTaskPanelKeyTranslate',  False, on_change=True,
        doc = "Let the Up and Down arrow keys move the keyboard focus through the\n"
              "task panel, the way Shift+Tab and Tab do."),
    ParamBool('EnableMenuBarCheckBox',  'FC_ENABLE_MENUBAR_CHECKBOX',
        doc = "Show the entries of the toolbar and dock window menus of the menu\n"
              "bar as checkboxes, the way the right-click menu of the main window\n"
              "shows them. Off by default on macOS."),
    ParamBool('EnableBacklight',  False,
        doc = "Turn on the backlight of the 3D view, a second light that shines on\n"
              "the faces turned away from the viewer."),
    ParamHex('BacklightColor',  0xffffffff,
        doc = "Colour of the backlight, the light that shines on the faces turned\n"
              "away from the viewer."),
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
    ParamBool('ToolTipDisable', False,
        doc = "Turn off the tool tips of the application. Tips shown as an overlay\n"
              "in the 3D view, such as the preselection tip, still appear."),
    ParamHex('AxisXColor', 0xCC333300,
        title = 'Axis X color',
        doc = "Colour of the X axis of the transform dragger and of other axis\n"
              "markers in the 3D view."),
    ParamHex('AxisYColor', 0x33CC3300,
        title = 'Axis Y color',
        doc = "Colour of the Y axis of the transform dragger and of other axis\n"
              "markers in the 3D view."),
    ParamHex('AxisZColor', 0x3333CC00,
        title = 'Axis Z color',
        doc = "Colour of the Z axis of the transform dragger and of other axis\n"
              "markers in the 3D view."),
    ParamBool('DatumScreenSize', True, title='Constant datum size on screen', doc=
        "Draw origins, coordinate systems and datum elements at a constant size on\n"
        "screen, the way upstream FreeCAD does. When off, an origin is sized to the\n"
        "objects of its body or part."),
    ParamFloat('DatumScale', 100.0, title='Datum scale',
        doc="Size in percent of origins, coordinate systems and datum elements drawn\n"
            "at a constant size on screen.",
        proxy=ParamSpinBox(1.0, 1000.0, 10.0, 0)),
    ParamFloat('DatumPlaneSize', 62.0, title='Datum plane size',
        doc="On-screen size of a datum plane, before the datum scale.",
        proxy=ParamSpinBox(1.0, 1000.0, 1.0, 0)),
    ParamFloat('DatumLineSize', 70.0, title='Datum line size',
        doc="On-screen length of a datum axis, before the datum scale.",
        proxy=ParamSpinBox(1.0, 1000.0, 1.0, 0)),
    ParamFloat('DatumTemporaryScaleFactor', 2.0, title='Datum temporary scale',
        doc="How much datum planes grow while a reference is picked from them.",
        proxy=ParamSpinBox(1.0, 10.0, 0.5, 1)),

    # ------------------------------------------------------------------
    # The settings of this group that used to be read straight from it,
    # each with a default of its own at every reader (docs/HandsOnQueue.md
    # entry 24). Most of them reach the 3D views through View3DSettings,
    # one observer per view, which is told by the parameter group itself:
    # it keeps reading the group and takes its DEFAULTS from here.
    #
    # Not listed, because the program keeps them for itself: SavePicture,
    # DimensionsVisible, Dimensions3dVisible, DimensionsDeltaVisible, the
    # HeadlightRotation quaternion of the Light sources page, the icon
    # browser's two fields. Not listed, because nothing reads them:
    # UseAutoRotation, ColorRecompute. Not listed, because their default
    # is not a constant: GestureMoveThreshold and GestureTapHoldTimeout
    # (the system's), the three colours of the default appearance (the
    # material card's).

    # --- the 3D view
    ParamFloat('EyeDistance', 5.0, title='Eye distance for stereo modes',
        doc="Offset between the left and the right eye image of a stereo 3D\n"
            "view. 0.1 to 1000. Applies at once to all open 3D views."),
    ParamBool('CornerCoordSystem', True, title='Show coordinate system in the corner',
        doc="Show the small coordinate system in the corner of every 3D view.\n"
            "Applies at once."),
    ParamInt('CornerCoordSystemSize', 10, title='Corner coordinate system size',
        doc="Size of the coordinate system in the corner of the 3D views, 2 to\n"
            "100. Applies at once."),
    ParamBool('ShowAxisCross', False, title='Show axis cross',
        doc="Show the axis cross at the origin of the 3D views. Applies at once\n"
            "to the open views and to new ones."),
    ParamBool('ShowFPS', False, title='Show counter of frames per second',
        doc="Show a frames per second counter in the 3D views. Applies at once."),
    ParamBool('UseVBO', False, title='Use vertex buffer objects',
        doc="Let Coin draw with vertex buffer objects. Applies at once to the\n"
            "open 3D views except split views; the driver override that goes\n"
            "with it is set at startup only."),
    ParamBool('Orthographic', True, title='Orthographic rendering',
        doc="Use an orthographic camera in the 3D views; the opposite of\n"
            "Perspective."),
    ParamBool('Perspective', False, title='Perspective rendering',
        doc="Use a perspective camera in the 3D views. Read when a view is\n"
            "created; with ApplyCameraTypeToAll on, a change switches the open\n"
            "views too."),
    ParamBool('ApplyCameraTypeToAll', False, title='Apply camera type to existing views',
        doc="When the camera type setting changes, switch every open 3D view\n"
            "to it as well."),
    ParamInt('AntiAliasing', 0, title='Anti-aliasing',
        doc="Anti-aliasing of the 3D views: 0 none, 1 line smoothing, 2 MSAA\n"
            "2x, 3 MSAA 4x, 4 MSAA 8x. Read when a view is created; a change\n"
            "rebuilds the open views."),
    ParamInt('TransparentObjectRenderType', 0, title='Transparent object render type',
        doc="How Coin draws transparent objects: 0 in one pass, 1 with the back\n"
            "faces of non-solid objects in a pass of their own. Applies at once\n"
            "to the open 3D views except split views."),
    ParamString('InternalTextureFormat', 'Default', title='Offscreen buffer format',
        doc="Pixel format of the offscreen buffer a 3D view is drawn into:\n"
            "Default, GL_RGB, GL_RGBA, GL_RGB8, GL_RGBA8, GL_RGB10,\n"
            "GL_RGB10_A2, GL_RGB16, GL_RGBA16, GL_RGB32F or GL_RGBA32F. Read\n"
            "each time a buffer is created."),

    # --- its background
    ParamBool('Gradient', True, title='Linear background gradient',
        doc="Fill the background of the 3D views with a linear gradient from\n"
            "BackgroundColor2 (top) to BackgroundColor3 (bottom). Wins over\n"
            "RadialGradient. Applies at once."),
    ParamBool('RadialGradient', False, title='Radial background gradient',
        doc="Fill the background of the 3D views with a radial gradient; used\n"
            "when Gradient is off. With both off the plain BackgroundColor is\n"
            "used. Applies at once."),
    ParamBool('Simple', False, title='Simple background colour',
        doc="The 'Simple color' choice of the Colors page. The views use a\n"
            "plain background whenever Gradient and RadialGradient are both\n"
            "off, whatever this says."),
    ParamHex('BackgroundColor', 0xEAE5DCFF, title='Background colour',
        proxy=ParamColor(transparency=False),
        doc="Colour of the 3D view background when no gradient is used.\n"
            "Applies at once."),
    ParamHex('BackgroundColor2', 0x333365FF, title='Background gradient, first colour',
        proxy=ParamColor(transparency=False),
        doc="First colour of the background gradient of the 3D views: the top\n"
            "of a linear one, the centre of a radial one. Applies at once."),
    ParamHex('BackgroundColor3', 0xABABC1FF, title='Background gradient, last colour',
        proxy=ParamColor(transparency=False),
        doc="Last colour of the background gradient of the 3D views: the\n"
            "bottom of a linear one, the rim of a radial one. Applies at once."),
    ParamHex('BackgroundColor4', 0x6F6F93FF, title='Background gradient, middle colour',
        proxy=ParamColor(transparency=False),
        doc="Middle colour of the background gradient of the 3D views; used\n"
            "only with UseBackgroundColorMid on. Applies at once."),
    ParamBool('UseBackgroundColorMid', False, title='Use a middle background colour',
        doc="Give the background gradient of the 3D views a third, middle\n"
            "colour (BackgroundColor4). Applies at once."),

    # --- its lights (a view with a light setting of its own keeps that)
    ParamBool('EnableHeadlight', True, title='Enable headlight',
        doc="Light the 3D views with the headlight, which follows the camera.\n"
            "Applies at once to every view with no light setting of its own."),
    ParamHex('HeadlightColor', 0xFFFFFFFF, title='Headlight colour',
        proxy=ParamColor(transparency=False),
        doc="Colour of the headlight of the 3D views. Applies at once."),
    ParamInt('HeadlightIntensity', 100, title='Headlight intensity',
        doc="Intensity of the headlight of the 3D views in percent, 0 to 100.\n"
            "Applies at once."),
    ParamString('HeadlightDirection', '', title='Headlight direction',
        doc="Direction of the headlight relative to the camera, as (x,y,z).\n"
            "Empty keeps the built-in direction. Set by dragging the light on\n"
            "the Light sources page. Applies at once."),
    ParamString('BacklightDirection', '', title='Backlight direction',
        doc="Direction of the backlight relative to the camera, as (x,y,z).\n"
            "Empty keeps the built-in direction. Applies at once."),
    ParamBool('EnableFillLight', False, title='Enable fill light',
        doc="Light the 3D views with an extra fill light from the side.\n"
            "Applies at once."),
    ParamHex('FillLightColor', 0xE6FAFFFF, title='Fill light colour',
        proxy=ParamColor(transparency=False),
        doc="Colour of the fill light of the 3D views. Applies at once."),
    ParamInt('FillLightIntensity', 60, title='Fill light intensity',
        doc="Intensity of the fill light of the 3D views in percent, 0 to 100.\n"
            "Applies at once."),
    ParamString('FillLightDirection', '', title='Fill light direction',
        doc="Direction of the fill light relative to the camera, as (x,y,z).\n"
            "Empty keeps the built-in direction. Applies at once."),
    ParamHex('AmbientLightColor', 0xFFFFFFFF, title='Ambient light colour',
        proxy=ParamColor(transparency=False),
        doc="Colour of the ambient light of the 3D views. Applies at once."),
    ParamInt('AmbientLightIntensity', 20, title='Ambient light intensity',
        doc="Intensity of the ambient light of the 3D views in percent, 0 to\n"
            "100. Applies at once."),

    # --- navigation
    ParamString('NavigationStyle', 'Gui::CADNavigationStyle', title='3D navigation style',
        doc="Mouse navigation style of the 3D views, as a class name such as\n"
            "Gui::CADNavigationStyle. Applies at once to all open 3D views;\n"
            "TechDraw pages follow it as well."),
    ParamBool('SameStyleForAllViews', True, title='Same navigation style for all views',
        doc="A navigation style picked from a 3D view's context menu becomes\n"
            "the NavigationStyle setting, so that every view follows. When off\n"
            "it changes that view only."),
    ParamInt('OrbitStyle', 1, title='Orbit style',
        doc="How dragging rotates the 3D view: 0 turntable, 1 trackball, 2 free\n"
            "turntable. Applies at once."),
    ParamInt('RotationMode', 1, title='Rotation mode',
        doc="Centre of rotation in the 3D views: 0 the window centre, 1 the\n"
            "point under the cursor, 2 the centre of the objects. Applies at\n"
            "once."),
    ParamFloat('Sensitivity', 2.0, title='Rotation sensitivity',
        doc="A value above 1 multiplies the angle of a mouse rotation of the 3D\n"
            "view. Applies at once."),
    ParamBool('ResetCursorPosition', False, title='Reset cursor position on rotation',
        doc="Move the mouse cursor to the rotation centre when a rotation of\n"
            "the 3D view starts. Applies at once."),
    ParamBool('InvertZoom', True, title='Invert zoom',
        doc="Invert the direction of zooming with the mouse wheel. The 3D\n"
            "views and TechDraw pages follow at once; the dependency graph\n"
            "reads it when it is opened."),
    ParamBool('ZoomAtCursor', True, title='Zoom at cursor',
        doc="Zoom towards the point under the mouse cursor instead of the\n"
            "centre of the view. Applies at once."),
    ParamFloat('ZoomStep', 0.2, title='Zoom step',
        doc="Zoom factor of one step of the mouse wheel, 0.01 to 1. Applies at\n"
            "once to the 3D views and TechDraw pages."),
    ParamBool('UseNavigationAnimations', True, title='Animate camera moves',
        doc="Animate camera moves such as switching to a standard view.\n"
            "Applies at once."),
    ParamBool('UseSpinningAnimations', False, title='Spin after a rotation',
        doc="Let the model keep spinning when the mouse button is released\n"
            "during a rotation. Applies at once."),
    ParamInt('AnimationDuration', 250, title='Animation duration',
        doc="Duration of an animated camera move in milliseconds, 100 to\n"
            "10000. Read each time an animation starts."),
    ParamInt('stopAnimatingIfDeactivated', 3000, title='Stop spinning when hidden after',
        doc="Milliseconds after which a spinning 3D view stops once it is\n"
            "hidden or minimized. A negative value never stops it."),
    ParamBool('ShowRotationCenter', True, title='Show rotation centre',
        doc="Show a marker at the centre of rotation while a 3D view is\n"
            "rotated. Read at each rotation."),
    ParamFloat('RotationCenterSize', 5.0, title='Rotation centre size',
        doc="Size of the rotation centre marker, 1 to 100. Read when the marker\n"
            "is next created."),
    ParamHex('RotationCenterColor', 0xFF000033, title='Rotation centre colour',
        proxy=ParamColor(),
        doc="Colour and opacity of the rotation centre marker: red and mostly\n"
            "see-through unless set. Read when the marker is next created."),
    ParamString('NewDocumentCameraOrientation', 'Trimetric', title='Default camera orientation',
        doc="Camera orientation of a new document: Isometric, Dimetric,\n"
            "Trimetric, Top, Front, Left, Right, Rear, Bottom, or Custom. Once\n"
            "set, the Home view takes it too; until then Home is Top. Read at\n"
            "each use."),
    ParamBool('AutoFitToView', True, title='Fit view after opening a file',
        doc="Fit the 3D view to the model after a file is opened or imported."),
    ParamBool('ShowNaviCube', True, title='Show navigation cube',
        doc="Show the navigation cube in the 3D views. Applies at once."),
    ParamBool('DisableTouchTilt', True, title='Disable touchscreen tilt gesture',
        doc="Gesture navigation: ignore the rotation part of a two-finger\n"
            "gesture on a touchscreen. Read at the start of each gesture."),
    ParamBool('NavigationDebug', False, title='Log gesture navigation',
        doc="Gesture navigation: write its state changes to the log. Read when\n"
            "the Gesture style is created."),
    ParamString('GestureRollFwdCommand', 'Std_SelForward', title='Roll forward gesture command',
        doc="Gesture navigation: command run by the forward roll gesture."),
    ParamString('GestureRollBackCommand', 'Std_SelBack', title='Roll back gesture command',
        doc="Gesture navigation: command run by the backward roll gesture."),

    # --- the rest
    ParamBool('SaveWBbyTab', False, title='Remember active workbench by tab',
        doc="Remember the active workbench separately for each view tab and\n"
            "switch back to it when the tab is activated."),
    ParamHex('CbLabelColor', 0xFFFFFFFF, title='Colour bar label colour',
        proxy=ParamColor(transparency=False),
        doc="Colour of the value labels of a colour bar in the 3D view. Read\n"
            "when the labels are next rebuilt."),
    ParamInt('CbLabelTextSize', 13, title='Colour bar label size',
        doc="Text size of the value labels of a colour bar in the 3D view, 4 to\n"
            "36. Read when the labels are next rebuilt."),
    ParamFloat('BoundingBoxFontSize', 10.0, title='Bounding box font size',
        doc="Font size of the dimension labels on an object's bounding box, 2\n"
            "to 64. Read when a bounding box is first shown for an object."),
    ParamFloat('DatumPointSize', 2.5, title='Datum point size',
        doc="Radius of the sphere drawn for a datum point."),
    ParamFloat('LocalCoordinateSystemSize', 1.0, title='Datum scale factor',
        doc="Scale factor of datum objects -- origin axes, planes, points --\n"
            "when they are drawn at a fixed size on screen."),
    ParamInt('DefaultShapeShininess', 37, title='Default shape shininess',
        doc="Shininess of the appearance given to new objects, in percent.\n"
            "Read each time a default appearance is made."),
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
