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
'''Auto code generator for Mesh related parameters
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

NameSpace = 'Mesh'
ClassName = 'MeshParams'
ParamPath = 'User parameter:BaseApp/Preferences/Mod/Mesh'
ClassDoc = 'Convenient class to obtain Mesh related parameters'

Params = [
    ParamString('AsymptoteWidth', '500', on_change=True, subpath='Asymptote', param_name='Width',
        doc = "Width of the picture in an exported Asymptote (.asy) file, as\n"
              "written to its size() command, in points. Leave empty to write no\n"
              "size at all."),
    ParamString('AsymptoteHeight', '500', on_change=True, subpath='Asymptote', param_name='Height',
        doc = "Height of the picture in an exported Asymptote (.asy) file, in\n"
              "points. Only written when a width is set; leave empty to give the\n"
              "width alone."),
    ParamInt('DefaultShapeType', 0,
        doc = "Shape type hint given to new mesh objects. 0 unknown, 1 solid.\n"
              "Filling the cut of a clip plane only works on a solid mesh."),
    ParamUInt('MeshColor', 0,
        doc = "Default face colour of new mesh objects, as a packed RGBA value.\n"
              "0 keeps the built-in colour."),
    ParamUInt('LineColor', 0,
        doc = "Default line colour of new mesh objects, as a packed RGBA value.\n"
              "0 keeps the built-in colour."),
    ParamInt('MeshTransparency', 0,
        title = 'Mesh Transparency',
        doc = "Default transparency of the faces of new mesh objects, in percent."),
    ParamInt('LineTransparency', 0,
        title = 'Line Transparency',
        doc = "Default transparency of the lines of new mesh objects, in percent."),
    ParamBool('TwoSideRendering', False,
        doc = "Light new mesh objects from both sides, so the back of a surface\n"
              "looks like the front. When off the back shows the backlight colour\n"
              "or black."),
    ParamBool('VertexPerNormals', False,
        doc = "Give new mesh objects the default crease angle, which shades them\n"
              "smoothly across edges flatter than that angle. When off new meshes\n"
              "are shaded flat, one normal per triangle."),
    ParamFloat('CreaseAngle', 0.0,
        doc = "Crease angle given to new mesh objects, in degrees. Faces meeting\n"
              "at less than this angle are shaded smoothly across their edge.\n"
              "Only used when normals per vertex are turned on."),
    ParamString('DisplayAliasFormatString', '%V = %A',
        doc = "Not used by the Mesh workbench. The spreadsheet setting of the\n"
              "same name controls how a cell with an alias is shown."),
    ParamBool('ShowBoundingBox', False,
        doc = "Mark a highlighted or selected mesh with its bounding box instead\n"
              "of colouring the mesh. Applies to new mesh objects."),
    ParamFloat('MaxDeviationExport', 0.1,
        doc = "Maximum deviation between a shape and the mesh made from it when\n"
              "exporting to a mesh file, in mm. Smaller values give finer meshes\n"
              "and larger files."),
    ParamInt('RenderTriangleLimit', -1,
        doc = "Draw large meshes as points while the view is being moved. The\n"
              "value is a power of ten: 5 means meshes of more than 100000\n"
              "triangles. 0 or less always draws the triangles."),
    ParamBool("CheckNonManifoldPoints", False, subpath='Evaluation',
        doc = "Also look for non-manifold points when the mesh evaluation dialog\n"
              "checks for non-manifolds, and remove them on repair."),
    ParamBool("EnableFoldsCheck", False, subpath='Evaluation',
        doc = "Offer the check for folds on the surface in the mesh evaluation\n"
              "dialog, and include it when everything is analysed."),
    ParamBool("StrictlyDegenerated", True, subpath='Evaluation',
        doc = "Count only faces of zero area as degenerated in the mesh\n"
              "evaluation dialog. When off, nearly degenerated faces count too."),
    ParamBool("SubElementSelection", False,
        doc = "Select single facets of a mesh when clicking in the 3D view,\n"
              "instead of the whole mesh object."),
]

def declare():
    params_utils.declare_begin(sys.modules[__name__])
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])
