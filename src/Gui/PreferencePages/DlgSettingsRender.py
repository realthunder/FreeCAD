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
'''Auto code generator for preference page of Display/Render engine
'''
import cog, sys
from os import sys, path

# import Tools/params_utils.py
sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Tools'))
import params_utils

sys.path.append(path.join(path.dirname(
    path.dirname(path.dirname(path.abspath(__file__)))), 'Gui'))
import RenderParams

Title = 'Render engine'
NameSpace = 'Gui'
ClassName = 'DlgSettingsRender'
ClassDoc = 'Preference dialog for the experimental render engine settings'

_RenderParams = { param.name : param for param in RenderParams.Params }

ParamGroup = (
    ('General', [_RenderParams[name] for name in (
        'Type',
        'OutputTransform',
        'Exposure',
    )]),

    ('Idle refinement', [_RenderParams[name] for name in (
        'TemporalAccum',
        'TemporalAccumSamples',
    )]),

    ('Scene streaming', [_RenderParams[name] for name in (
        'CoarseTessellation',
        'LevelTolerance',
        'LevelThreads',
        'LevelMemoryFloorMB',
        'GpuMemoryBudgetMB',
    )]),

    ('Ambient occlusion', [_RenderParams[name] for name in (
        'AO',
        'AOMethod',
        'AOSlices',
        'AOSteps',
        'AORadius',
        'AOIntensity',
    )]),

    ('Physically based shading', [_RenderParams[name] for name in (
        'PBR',
        'PBRMetallic',
        'PBRRoughness',
        'PBRFromSpecular',
        'ShininessMapping',
        'PBREnvIntensity',
    )]),

    ('Bump mapping', [_RenderParams[name] for name in (
        'BumpScale',
        'Parallax',
    )]),

    ('Volumetric lighting', [_RenderParams[name] for name in (
        'Volumetric',
        'VolumetricIntensity',
        'VolumetricDensity',
        'Caustics',
        'CausticsIntensity',
        'CausticsScale',
        'CausticsSpeed',
    )]),

    ('Water surface', [_RenderParams[name] for name in (
        'WaterSurface',
        'WaterWaveStrength',
        'WaterWaveScale',
        'WaterWaveSpeed',
    )]),

    ('Bloom', [_RenderParams[name] for name in (
        'Bloom',
        'BloomThreshold',
        'BloomIntensity',
        'BloomRadius',
    )]),

    ('Scene light extras', [_RenderParams[name] for name in (
        'SunDisc',
        'SunDiscSize',
    )]),

    ('Ground reflection', [_RenderParams[name] for name in (
        'GroundReflection',
        'GroundReflectionIntensity',
    )]),

    ('External shading (Cycles)', [_RenderParams[name] for name in (
        'CyclesDevice',
        'CyclesSamples',
        'CyclesTimeLimit',
        'CyclesDenoise',
        'CyclesPixelSize',
        'CyclesMaxStreams',
    )]),
)

def declare():
    params_utils.preference_dialog_declare(sys.modules[__name__])

def define():
    params_utils.preference_dialog_define(sys.modules[__name__])
