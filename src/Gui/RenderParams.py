# -*- coding: utf-8 -*-
# ***************************************************************************
# *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
'''Auto code generator for parameters in Preferences/View/Render

Parameters of the experimental render engine (Gui/Renderer, active with
render cache mode 3 and a selected renderer type). Split out of ViewParams;
RenderParams::migrate() moves the pre-split Renderer* keys of the parent
View group into this child group.
'''
import cog
import sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(path.dirname(path.abspath(__file__))), 'Tools'))
import params_utils

from params_utils import ParamBool, ParamString, ParamFloat, auto_comment

NameSpace = 'Gui'
ClassName = 'RenderParams'
ParamPath = 'User parameter:BaseApp/Preferences/View/Render'
ClassDoc = 'Convenient class to obtain the experimental render engine parameters'
UserOnChange = 'RenderParams::onRenderParamChanged(sReason);'

Params = [
    ParamString('Type', 'Default',
        "Type of the experimental render engine backend. 'Default' keeps\n"
        "the plain GL pipeline. Only effective with render cache mode 3."),
    ParamBool('SSAO',  False,
        "Enable screen space ambient occlusion of the experimental render\n"
        "engine (render cache mode 3 with a selected renderer type)."),
    ParamFloat('SSAORadius',  0.0,
        "Ambient occlusion sample radius in world units.\n"
        "Zero means automatic (a fraction of the scene size)."),
    ParamFloat('SSAOIntensity',  1.0,
        "Ambient occlusion darkening strength."),
    ParamBool('PBR',  False,
        "Enable physically based shading with image based lighting of\n"
        "the experimental render engine (render cache mode 3 with a\n"
        "selected renderer type). Replaces the default headlight shading\n"
        "of lit surfaces with a metallic/roughness material lit by a\n"
        "built-in studio environment."),
    ParamFloat('PBRMetallic',  0.0,
        "Metalness of physically based shaded surfaces, 0 to 1."),
    ParamFloat('PBRRoughness',  0.0,
        "Roughness of physically based shaded surfaces, 0 to 1.\n"
        "Zero means automatic (derived from each material's shininess)."),
    ParamFloat('PBREnvIntensity',  1.0,
        "Brightness of the image based lighting environment."),
    ParamFloat('BumpScale',  1.0,
        "Strength of bump/normal mapped surfaces (SoBumpMap) of the\n"
        "experimental render engine: scales the slope of normal maps and\n"
        "the height amplitude of grayscale bump maps."),
    ParamBool('Parallax',  True,
        "Parallax-occlusion map grayscale bump maps (SoBumpMap) of the\n"
        "experimental render engine, shifting the texture with the view\n"
        "angle for a strong relief impression."),
]

def declare_begin():
    params_utils.declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
