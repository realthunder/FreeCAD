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
'''Auto code generator for Part related parameters
'''
import sys
import cog
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.dirname(path.dirname(path.abspath(__file__))))), 'Tools'))
import params_utils

from params_utils import Property, ParamBool, ParamInt, ParamString, ParamUInt, ParamFloat, ParamColor

NameSpace = 'Part'
ClassName = 'PartParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Part'
ClassDoc = 'Convenient class to obtain Part/PartDesign related parameters'

# import ../Gui/PartGuiParams.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))),
'Gui'))
import PartGuiParams
_PartGuiParams = { param.name : param for param in PartGuiParams.Params }

# import the following parameters in PartGuiParams.py so that we don't need to
# maintain the same definition in two places. These parameters need to be in Gui
# namespace because we need realtime changes in view providers in response to
# changes in these parameters
_MinimumDeviation = _PartGuiParams['MinimumDeviation']
_MinimumDeviation.on_change = False
_MeshDeviation = _PartGuiParams['MeshDeviation']
_MeshDeviation.on_change = False
_MeshAngularDeflection = _PartGuiParams['MeshAngularDeflection']
_MeshAngularDeflection.on_change = False
_MinimumAngularDeflection = _PartGuiParams['MinimumAngularDeflection']
_MinimumAngularDeflection.on_change = False

Params = [
    ParamBool("ShapePropertyCopy", False,
        doc = "Make a full geometric copy whenever a shape property is copied,\n"
              "instead of sharing the shape. Uses much more memory on complex\n"
              "models."),
    ParamBool("DisableShapeCache", False,
        doc = "Do not keep the shapes computed for an object and its sub-objects\n"
              "for reuse. They are rebuilt on every request, which is slower;\n"
              "meant for troubleshooting."),
    ParamInt("CommandOverride", 2,
        doc = "Run the PartDesign equivalent when a Part command is used with a\n"
              "PartDesign body active or one of its features selected. 0 never,\n"
              "1 always, 2 ask each time."),
    ParamInt("EnableWrapFeature", 2,
        doc = "Bring a non-PartDesign object that references features of the\n"
              "active body into that body through a wrap feature. 0 never,\n"
              "1 always, 2 ask each time."),
    ParamBool("CopySubShape", False,
        doc = "Copy the geometry when a placed sub-shape of an object is handed\n"
              "to Python, instead of only moving it. Slower, but avoids kernel\n"
              "errors on some transformed shapes."),
    ParamBool("UseBrepToolsOuterWire", True,
        doc = "Find the outer wire of a face in Python (Face.OuterWire) with the\n"
              "kernel's BRepTools. When off its ShapeAnalysis is used; the two\n"
              "can differ on unusual faces."),
    ParamBool("UseBaseObjectName", False,
        doc = "Label a new body after the object selected as its base feature.\n"
              "The question asked when the body is created has the same checkbox."),
    ParamBool("AutoGroupSolids", False,
        doc = "Turn on Auto Group Solids in new bodies, which groups the features\n"
              "of each solid under its latest feature."),
    ParamBool("SingleSolid", False,
        doc = "Turn on Single Solid in new bodies, so that every feature must\n"
              "result in one solid."),
    ParamBool("UsePipeForExtrusionDraft", False,
        doc = "Build the draft angle of new pads, pockets and Part extrusions\n"
              "with a sweep instead of a loft. Each object keeps its own switch."),
    ParamBool("LinearizeExtrusionDraft", True,
        doc = "Turn flat spline faces into planes and straight spline edges into\n"
              "lines in new lofts, sweeps and drafted extrusions, in Part and\n"
              "PartDesign. Each object keeps its own switch."),
    ParamBool("AutoCorrectLink", False,
        doc = "While a PartDesign feature is edited, replace a reference it is\n"
              "given by a sub-shape binder imported into the body automatically."),
    ParamBool("RefineModel", False,
        doc = "Turn on Refine in new sub-shape binders, which merges faces lying\n"
              "on the same surface. Part booleans and PartDesign features have\n"
              "their own settings."),
    ParamBool("AuxGroupUniqueLabel", False,
        doc = "Give the Sketches, Datums and Misc groups of each body a unique\n"
              "label such as Datums001. When off they can all carry the same\n"
              "label."),
    ParamBool("SplitEllipsoid", True,
        doc = "Turn on Split in new ellipsoids, which cuts the surface in the\n"
              "middle to avoid errors in later boolean operations."),
    ParamInt("ParallelRunThreshold", 100,
        doc = "Run boolean operations on several processor threads. Any value\n"
              "above 0 turns this on, 0 or less turns it off."),
    ParamBool("AutoValidateShape", False,
        doc = "Turn on Validate Shape in new PartDesign features. An invalid\n"
              "result then gets a warning icon in the tree. Can slow down complex\n"
              "models."),
    ParamBool("FixShape", False,
        doc = "Set Fix Shape to Enabled in new Part objects, so that a result\n"
              "found invalid is repaired. When off new objects are left as they\n"
              "are computed."),
    ParamBool("ShareStoredSubShapes", True,
        "Let a stored shape borrow a sub-shape from another object's file instead\n"
        "of writing its geometry again (docs/SharedShapeStorage.md sec 12.4).\n"
        "Turning this off writes every file whole, which is what the format did\n"
        "before external references; the files stay readable either way."),
    # The long form, kept here; the documentation shown is the short one below.
    # Which sub-shapes may be borrowed below a shell, as a sum
    # (docs/SharedShapeStorage.md sec 12.15): 0 none, which is what ships,
    # 1 a face inside a shell, 2 an edge inside a face or a wire, 4 a vertex
    # inside an edge. Each of those associations is keyed on the identity of
    # a geometry object -- a face's edges hold their 2D curve against the
    # surface the face carries -- so this is sound only where the geometry is
    # shared too, and it is off wherever DedupCrossFileGeometry is.
    ParamInt("BorrowBelowFace", 0,
        "Which sub-shapes a shape file may borrow from another below the level\n"
        "of a shell, as a sum: 1 a face in a shell, 2 an edge in a face or\n"
        "wire, 4 a vertex in an edge. 0, the default, none."),
    ParamUInt("LoftMaxDegree", 5,
        doc = "Maximum surface degree given to new PartDesign lofts. Kept between\n"
              "2 and the highest degree the kernel supports."),
    # The long form, kept here; the documentation shown is the short one below.
    # Report a shape operation whose input shapes carry no element map, so
    # the result cannot be named either. This is off by default because an
    # absent element map is frequently correct -- program generated and
    # imported geometry has none -- and because a genuine naming failure is
    # developer information that an end user cannot act on. Turn it on when
    # writing a workbench that builds shapes and wants its element names to
    # survive a recompute. 0 off, 1 report each operation once per document
    # recompute, 2 report every occurrence. Raising the Part module's log
    # level to LOG reports every occurrence too, without this preference.
    ParamInt("WarnUnnamedInput", 0,
        "Report shape operations whose inputs carry no element names, so the\n"
        "result cannot be named either. For workbench developers. 0 off, 1 once\n"
        "per operation and recompute, 2 every occurrence."),
    _MinimumDeviation,
    _MeshDeviation,
    _MeshAngularDeflection,
    _MinimumAngularDeflection,
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

PropertyGroup = 'ShapeContent'
Properties = [
    Property('ShapeContents',
             'App::PropertyLinkList',
             'Stores the expanded sub shape content objects',
             PropertyGroup),
    Property('ShapeContentSuppressed',
             'App::PropertyBool',
             'Suppress this sub shape content',
             PropertyGroup),
    Property('ShapeContentReplacement',
             'App::PropertyLinkHidden',
             'Refers to a shape replacement',
             PropertyGroup),
    Property('ShapeContentReplacementSuppressed',
             'App::PropertyBool',
             'Suppress shape content replacement',
             PropertyGroup),
    Property('ShapeContentDetached',
             'App::PropertyBool',
             'If detached, than the shape content will not be auto removed and parent shape is removed',
             PropertyGroup),
    Property('_ShapeContentOwner',
             'App::PropertyLinkHidden',
             'Refers to the shape owner',
             PropertyGroup,
             prop_flags='App::Prop_Hidden'),
]

def declare_properties():
    params_utils.declare_properties(Properties)

def define_properties():
    params_utils.define_properties(Properties, 'Part::Feature')
