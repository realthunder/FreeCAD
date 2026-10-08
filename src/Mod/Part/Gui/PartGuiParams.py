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
'''Auto code generator for PartGui related parameters
'''
import sys
import cog
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import Property, ParamBool, ParamInt, ParamHex, ParamUInt, ParamFloat, ParamColor, \
                         ParamString

NameSpace = 'PartGui'
ClassName = 'PartParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Part'
ClassDoc = 'Convenient class to obtain Part/PartDesign visual related parameters'

Params = [
    ParamBool("NormalsFromUVNodes", True,
        doc = "Take the shading normals of a shape from its exact surface instead\n"
              "of from the display triangles. Gives smoother shading of curved\n"
              "faces."),
    ParamBool("TwoSideRendering", True,
        doc = "Light new shapes from both sides, so the back of a face looks like\n"
              "the front. When off the back shows the backlight colour or black."),
    ParamFloat("MinimumDeviation", 0.05, on_change=True,
        doc = "Lower limit of the tessellation deviation of shapes, in percent of\n"
              "the object size. Objects asking for a finer mesh are drawn with\n"
              "this value instead."),
    ParamFloat("MeshDeviation", 0.2, on_change=True,
        doc = "Accuracy of the mesh that shapes are drawn with, as the largest\n"
              "deviation in percent of the object size. Lower is finer and\n"
              "slower. Sets the Deviation of new objects; a change is applied to\n"
              "all open objects."),
    ParamFloat("MeshAngularDeflection", 28.65, on_change=True,
        doc = "Largest angle between neighbouring segments of the mesh that\n"
              "shapes are drawn with, in degrees. Lower is smoother and slower.\n"
              "Sets the Angular Deflection of new objects; a change is applied to\n"
              "all open objects."),
    ParamFloat("MinimumAngularDeflection", 5.0, on_change=True,
        doc = "Lower limit of the angular deflection used to mesh shapes, in\n"
              "degrees. Objects asking for a smaller angle are drawn with this\n"
              "value instead."),
    ParamBool("OverrideTessellation", False, on_change=True,
        doc = "Draw every shape with the deviation and angular deflection set\n"
              "here, ignoring the values stored in each object. When off a change\n"
              "of those two settings is written into the open objects instead."),
    ParamBool("MapFaceColor", True,
        doc = "Let new shapes take their face colours from the shapes they were\n"
              "made from. Turn off to give all faces of an object one colour."),
    ParamBool("MapLineColor", False,
        doc = "Let new shapes take their edge colours from the shapes they were\n"
              "made from. Turn off to give all edges of an object one colour."),
    ParamBool("MapPointColor", False,
        doc = "Let new shapes take their vertex colours from the shapes they were\n"
              "made from. Turn off to give all vertices of an object one colour."),
    ParamBool("MapTransparency", False,
        doc = "Let new shapes take the transparency of their faces from the\n"
              "shapes they were made from. Turn off for one transparency per\n"
              "object."),
    ParamBool("AutoGridScale", False,
        doc = "Double or halve the grid size of a sketch being edited as the view\n"
              "is zoomed, so that the grid keeps a similar spacing on screen."),
    ParamHex("PreviewAddColor", 0x64ffff30, proxy=ParamColor(),
        doc = "Colour of the preview of a feature that adds material. Its alpha\n"
              "part sets how transparent the preview is."),
    ParamHex("PreviewSubColor", 0xff646430, proxy=ParamColor(),
        doc = "Colour of the preview of a feature that removes material. Its\n"
              "alpha part sets how transparent the preview is."),
    ParamHex("PreviewDressColor", 0xff64ff30, proxy=ParamColor(),
        doc = "Colour of the preview of a dress-up feature such as a fillet or a\n"
              "chamfer. Its alpha part sets how transparent the preview is."),
    ParamHex("PreviewIntersectColor", 0x6464ff30, proxy=ParamColor(),
        doc = "Colour of the preview of a feature that keeps what it has in\n"
              "common with the body. Its alpha part sets how transparent the\n"
              "preview is."),
    ParamBool("PreviewOnEdit", True,
        doc = "Show a preview of the result while a PartDesign feature is edited,\n"
              "and hold back the recompute of the feature until the preview is\n"
              "turned off or the edit ends."),
    ParamBool("PreviewWithTransparency", True,
        doc = "Draw the preview of an edited PartDesign feature transparent. When\n"
              "off it is drawn opaque."),
    ParamBool("EditOnTop", False,
        doc = "Draw the PartDesign feature being edited on top of everything else\n"
              "in the 3D view."),
    ParamInt("EditRecomputeWait", 300,
        doc = "Delay between a change in a PartDesign task panel and the update\n"
              "of the feature, in milliseconds. A third of it is used while the\n"
              "preview is shown."),
    ParamBool("AdjustCameraForNewFeature", True,
        doc = "Move the camera to bring a newly created feature into view. Used\n"
              "by Part offset and thickness, by PartDesign features made from a\n"
              "selected profile, and by new bodies."),
    # One colour for datums and binders, kept in PartDesign's group, where
    # upstream has it and where its themes store it. It is defined here
    # because the sub-shape binder of this fork lives in Part. There used
    # to be a second DefaultDatumColor in Part's own group, which the
    # binders read while the datums read PartDesign's.
    ParamHex("DefaultDatumColor", 0xFFD70066, proxy=ParamColor(),
        subpath="User parameter:BaseApp/Preferences/Mod/PartDesign",
        title = "Default datum colour",
        doc = "Colour and transparency of new datum planes, lines and points, of\n"
              "shape binders, of sub-shape binders shown in binder style, and of\n"
              "PartDesign extrusions: golden yellow, mostly see-through, unless\n"
              "set."),
    ParamHex("DefaultDatumLineColor", 0xFA9600FF, proxy=ParamColor(),
        subpath="User parameter:BaseApp/Preferences/Mod/PartDesign",
       doc="Line and point color of a shape binder, darker than DefaultDatumColor\n"
           "so that its outline shows against the model (upstream 5dbb4d7c7e)"),
    ParamBool("RespectSystemDPI", False, on_change=True,
        title = 'Respect system DPI',
        doc = "Scale the line width and point size of shapes by the pixel ratio\n"
              "of the display. May look wrong with monitors of different scaling."),
    ParamBool("ShapeInstancing", True, on_change=True,
       doc="Share the tessellation of repeated sub-shapes (same TopoDS_TShape)\n"
           "inside a compound and render them as GPU instances. Only takes\n"
           "effect when the renderer supports instanced draws; otherwise the\n"
           "geometry is flattened as before."),
    ParamInt("SelectionPickThreshold", 1000,
        doc = "Size of a shape above which picking first narrows the search with\n"
              "bounding boxes, counted in face, edge or point indices. Smaller\n"
              "shapes are tested whole. 0 or less always tests everything."),
    ParamInt("SelectionPickThreshold2", 500,
        doc = "Size above which a single face or edge gets a search structure of\n"
              "its own for picking: the triangles of a face, the points of an\n"
              "edge. 0 or less turns it off for faces."),
    # The long form, kept here; the documentation shown is the short one below.
    # Pick with a per-triangle R-tree instead of walking every
    # triangle of a part. Without it the only spatial filter is the
    # per-part bounding box, so a ray that reaches a dense part
    # sends all of its triangles through Coin's primitive callbacks:
    # on an imported mesh (one part carrying everything) a selecting
    # click cost 116 ms, and 18 ms with this on. The tree is built
    # lazily, per part, on the first pick that reaches it -- that
    # first pick pays about 15 ms more, every one after it is the
    # cheap one. Parts smaller than SelectionPickThreshold2 are
    # picked directly either way.
    ParamBool("SelectionPickRTree", True,
       doc="Pick with a spatial index of a part's triangles instead of testing\n"
           "every triangle. Much faster on dense parts; the index is built on the\n"
           "first pick that reaches a part. Parts smaller than\n"
           "SelectionPickThreshold2 are picked directly."),
    ParamHex("Dimensions3dColor", 0xff0000ff, on_change=True, proxy=ParamColor(transparency=False),
        title = "Measurement colour",
        doc = "Colour of the direct distance of a Part measurement in the 3D view.\n"
              "The measurements shown follow a change."),
    ParamHex("DimensionsDeltaColor", 0x00ff00ff, on_change=True, proxy=ParamColor(transparency=False),
        title = "Measurement delta colour",
        doc = "Colour of the X, Y and Z components of a Part distance measurement\n"
              "in the 3D view. The measurements shown follow a change."),
    ParamHex("DimensionsAngularColor", 0x0000ffff, on_change=True, proxy=ParamColor(transparency=False),
        title = "Angle measurement colour",
        doc = "Colour of a Part angle measurement in the 3D view. The\n"
              "measurements shown follow a change."),
    ParamInt("DimensionsFontSize", 30, on_change=True,
        title = "Measurement font size",
        doc = "Size of the text of Part measurements in the 3D view. The\n"
              "measurements shown follow a change."),
    ParamString("DimensionsFontName", "defaultFont", on_change=True,
        title = "Measurement font",
        doc = "Font of the text of Part measurements in the 3D view; defaultFont\n"
              "is the 3D view's own. The measurements shown follow a change."),
    ParamBool("DimensionsFontStyleBold", False, on_change=True,
        title = "Measurement font bold",
        doc = "Draw the text of Part measurements in the 3D view in bold."),
    ParamBool("DimensionsFontStyleItalic", False, on_change=True,
        title = "Measurement font italic",
        doc = "Draw the text of Part measurements in the 3D view in italic."),

    # --- The options of Check Geometry, in the sub-group CheckGeometry. Their
    # names here carry the group's, their keys do not.
    ParamBool("CheckGeometryAutoRun", False, subpath='CheckGeometry', param_name='AutoRun',
        title = "Run the geometry check at once",
        doc = "Run the geometry check as soon as its panel opens, without the Run\nCheck button."),
    ParamBool("CheckGeometryRunBOPCheck", False, subpath='CheckGeometry', param_name='RunBOPCheck',
        title = "Run the Boolean operation check",
        doc = "Run the Boolean operation check on shapes the basic check finds\nvalid. It finds more, and can be very slow."),
    ParamBool("CheckGeometryRunSingleThreaded", False, subpath='CheckGeometry', param_name='RunSingleThreaded',
        title = "Geometry check in a single thread",
        doc = "Run the Boolean operation check of the geometry check in a single\nthread: slower, and more stable."),
    ParamBool("CheckGeometryLogErrors", True, subpath='CheckGeometry', param_name='LogErrors',
        title = "Log geometry check errors",
        doc = "Write the errors the geometry check finds to the report view."),
    ParamBool("CheckGeometryExpandShapeContent", False, subpath='CheckGeometry', param_name='ExpandShapeContent',
        title = "Expand shape content",
        doc = "Open the shape content of the geometry check's result when it is\nshown."),
    ParamBool("CheckGeometryAdvancedShapeContent", True, subpath='CheckGeometry', param_name='AdvancedShapeContent',
        title = "Advanced shape content",
        doc = "Show more about the shape in the geometry check's shape content."),
    ParamBool("CheckGeometryArgumentTypeMode", True, subpath='CheckGeometry', param_name='ArgumentTypeMode',
        title = "Check for bad argument types",
        doc = "Boolean operation check: look for shapes of a kind the operation\ncannot take."),
    ParamBool("CheckGeometrySelfInterMode", True, subpath='CheckGeometry', param_name='SelfInterMode',
        title = "Check for self-intersections",
        doc = "Boolean operation check: look for shapes that intersect themselves."),
    ParamBool("CheckGeometrySmallEdgeMode", True, subpath='CheckGeometry', param_name='SmallEdgeMode',
        title = "Check for small edges",
        doc = "Boolean operation check: look for edges that are too small."),
    ParamBool("CheckGeometryRebuildFaceMode", True, subpath='CheckGeometry', param_name='RebuildFaceMode',
        title = "Check for faces that cannot be rebuilt",
        doc = "Boolean operation check: look for faces that cannot be rebuilt."),
    ParamBool("CheckGeometryContinuityMode", True, subpath='CheckGeometry', param_name='ContinuityMode',
        title = "Check for continuity",
        doc = "Boolean operation check: look for edges that are not continuous."),
    ParamBool("CheckGeometryTangentMode", True, subpath='CheckGeometry', param_name='TangentMode',
        title = "Check for tangency",
        doc = "Boolean operation check: look for shapes that only touch."),
    ParamBool("CheckGeometryMergeVertexMode", True, subpath='CheckGeometry', param_name='MergeVertexMode',
        title = "Check for mergeable vertices",
        doc = "Boolean operation check: look for vertices that should be one."),
    ParamBool("CheckGeometryMergeEdgeMode", True, subpath='CheckGeometry', param_name='MergeEdgeMode',
        title = "Check for mergeable edges",
        doc = "Boolean operation check: look for edges that should be one."),
    ParamBool("CheckGeometryCurveOnSurfaceMode", True, subpath='CheckGeometry', param_name='CurveOnSurfaceMode',
        title = "Check curves on surfaces",
        doc = "Boolean operation check: look for edges whose curve on a face does\nnot follow the edge."),
    ParamBool("ParametricRefine", True,
        title = "Refine shape makes a feature",
        doc = "Refine Shape makes a parametric Refine feature that follows its\n"
              "source. When off it makes a plain copy of the refined shape."),
    ParamBool("AddBaseObjectName", False,
        title = "Add the base object's name",
        doc = "Extrude and Scale label their result with the name of the object\n"
              "it was made from."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
