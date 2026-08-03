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

from params_utils import ParamBool, ParamString, ParamFloat, ParamInt, \
                         ParamComboBox, auto_comment

NameSpace = 'Gui'
ClassName = 'RenderParams'
ParamPath = 'User parameter:BaseApp/Preferences/View/Render'
ClassDoc = 'Convenient class to obtain the experimental render engine parameters'
UserOnChange = 'RenderParams::onRenderParamChanged(sReason);'

Params = [
    ParamString('Type', 'Default', title='Renderer type',
        doc="Type of the experimental render engine backend. 'Default' keeps\n"
        "the plain GL pipeline. Only effective with render cache mode 3."),
    ParamInt('CoarseTessellation',  2, title='Coarse tessellation level',
        doc="Ladder level shapes are tessellated at when the scene is being\n"
        "served to streaming viewers (docs/SceneStreaming.md #7,\n"
        "coarse-first publish): the display mesh is built at this rung of\n"
        "the fidelity ladder and the exact tessellation is declared\n"
        "unbuilt, generated on demand where a viewer's camera asks. 0 is\n"
        "the coarsest rung, each level halves the error; -1 always\n"
        "tessellates exact up front (pre-ladder behavior). Only consulted\n"
        "while a scene stream server is active - plain desktop display\n"
        "keeps the exact tessellation, which is also why this engages\n"
        "automatically for a headless serving process. The\n"
        "FC_COARSE_TESSELLATION environment variable overrides it for a\n"
        "whole process. Takes effect when a shape (re)tessellates."),
    ParamInt('CoarseDeferFaces',  1000, title='Coarse defer face threshold',
        doc="During a progressive import on the bgfx renderer, a shape with\n"
        "more faces than this gets a bounding-box stand-in immediately and\n"
        "even its coarse tessellation is built on the refine worker pool,\n"
        "swapped in when it arrives (docs/SceneStreaming.md #13) - the\n"
        "import stall otherwise scales with the largest single part. -1\n"
        "disables the stand-in so every shape tessellates inline."),
    ParamInt('LevelThreads',  0, title='Level build threads',
        doc="How many mesh level builds (the scene server's on-demand\n"
        "re-tessellations, docs/SceneStreaming.md #7) may run at once.\n"
        "0 sizes the pool automatically - modest, because each BRepMesh\n"
        "build already parallelizes internally over OCCT's shared thread\n"
        "pool. The FC_LEVEL_THREADS environment variable overrides it.\n"
        "Read when the server spawns its first level worker."),
    ParamInt('LevelMemoryFloorMB',  0, title='Level memory floor (MB)',
        doc="Available system memory below which an exact re-tessellation\n"
        "will not start (docs/SceneStreaming.md #13): the desktop refine\n"
        "worker checks the system's own estimate of allocatable memory\n"
        "before each exact build, and dropping under this floor counts as\n"
        "a memory-ceiling observation - the same as a caught allocation\n"
        "failure - after which the level plans also demote exact meshes\n"
        "the camera would not miss back to their resident coarse rung.\n"
        "0 sizes the floor automatically (at least 512 MB, or 1/16 of\n"
        "physical memory if that is more). Read when the first refine is\n"
        "queued."),
    ParamInt('GpuMemoryBudgetMB',  0, title='GPU memory budget (MB)',
        doc="GPU geometry budget of the desktop mesh-level plan\n"
        "(docs/SceneStreaming.md #13): while the uploaded geometry exceeds\n"
        "it, a camera pause downgrades the *displayed* mesh of objects the\n"
        "camera would not miss - off screen, or coarse within half the\n"
        "Level tolerance - back to their coarse rung. Their exact meshes\n"
        "stay in CPU RAM, so zooming back in re-activates them instantly,\n"
        "with no re-tessellation. 0 means automatic: the graphics API's\n"
        "own reported GPU memory limit where it states one (Direct3D and\n"
        "Vulkan do; OpenGL reports nothing, and then no budget applies)."),
    ParamFloat('LevelTolerance',  2.0, title='Level tolerance',
        doc="Screen-space error, in pixels, a coarse tessellation may\n"
        "commit before the exact one is built (docs/SceneStreaming.md\n"
        "#13): on a coarse-first desktop view (render cache mode 3 with\n"
        "a backend that drives the level plan), a camera pause re-plans\n"
        "the scene and only objects whose coarse mesh errs by more than\n"
        "this many pixels on screen re-tessellate exactly - off-screen\n"
        "and distant objects stay at the cheap coarse mesh until the\n"
        "camera makes them matter. 0 or less refines everything\n"
        "immediately; larger keeps more of the scene coarse. The\n"
        "streamed viewer's own tolerance is its lodpx URL parameter\n"
        "(same meaning, same default)."),
    ParamFloat('EffectResolution',  1.0, title='Effect resolution',
        doc="Resolution scale (0.25-1.0) of the expensive screen-space effect\n"
        "passes -- the planar/ground reflection scene re-render, the water\n"
        "body depth prepass and screen-space ambient occlusion -- relative to\n"
        "the main view resolution. Lowering it trades effect sharpness for\n"
        "speed on large windows, where those per-pixel passes dominate the\n"
        "frame; the main geometry, edges, text and overlays stay full\n"
        "resolution. 1.0 renders the effects at full resolution. The\n"
        "volumetric light shafts already render at half resolution."),
    ParamBool('AO',  False, title='Ambient occlusion',
        doc="Enable screen space ambient occlusion of the experimental render\n"
        "engine (render cache mode 3 with a selected renderer type)."),
    ParamBool('Shadow',  True, title='Shadow',
        doc="Render the shadow map cast by the Shadow draw style's scene\n"
        "light (and the god-ray shafts / caustic occlusion that depend on\n"
        "it). A convenience switch to drop shadows without leaving the\n"
        "Shadow draw style; the base headlight and environment lighting\n"
        "stay, so the scene remains lit, just flatter. Has no effect unless\n"
        "the Shadow draw style provides a scene light."),
    ParamInt('AOMethod',  0, title='AO method',
        proxy=ParamComboBox(items=['SSAO (hemisphere)', 'GTAO (horizon)']),
        doc="Ambient occlusion algorithm. 0 = classic hemisphere-kernel\n"
        "SSAO (screen-space depth-difference sampling). 1 = GTAO\n"
        "(ground-truth ambient occlusion, XeGTAO-style horizon-based\n"
        "visibility integration): physically correct occlusion falloff,\n"
        "tight contact shadows without the wide low-contrast wash of\n"
        "classic SSAO at large radii."),
    ParamInt('AOSlices',  9, title='GTAO slices',
        doc="GTAO only: number of screen-space slice directions per pixel\n"
        "(XeGTAO High preset = 9). The dominant quality/cost dial —\n"
        "direction variance shows as blotchy grain the denoiser cannot\n"
        "fully flatten. Cost scales linearly."),
    ParamInt('AOSteps',  3, title='GTAO steps',
        doc="GTAO only: horizon-march samples per slice side. More steps\n"
        "resolve distant occluders more stably (less mid-frequency blotch\n"
        "on grazing surfaces), at linear cost."),
    ParamFloat('AORadius',  0.0, title='Sample radius',
        doc="Ambient occlusion sample radius in world units.\n"
        "Zero means automatic (a fraction of the scene size)."),
    ParamFloat('AOIntensity',  0.6, title='Intensity',
        doc="Ambient occlusion darkening strength."),
    ParamFloat('AOResolution',  1.0, title='AO resolution',
        doc="Resolution scale (0.25-1.0) of the ambient occlusion resolve\n"
        "targets relative to the main view resolution, independent of the\n"
        "shared Effect resolution. Ambient occlusion is resolution-sensitive\n"
        "(contact and crevice detail), so it has its own control; the shared\n"
        "Effect resolution drives only the costlier reflection re-render.\n"
        "1.0 renders the occlusion at full resolution; lower trades AO\n"
        "sharpness for speed."),
    ParamBool('PBR',  False, title='Physically based shading',
        doc="Enable physically based shading with image based lighting of\n"
        "the experimental render engine (render cache mode 3 with a\n"
        "selected renderer type). Replaces the default headlight shading\n"
        "of lit surfaces with a metallic/roughness material lit by a\n"
        "built-in studio environment."),
    ParamFloat('PBRMetallic',  0.0, title='Metallic',
        doc="Metalness of physically based shaded surfaces, 0 to 1."),
    ParamFloat('PBRRoughness',  0.0, title='Roughness',
        doc="Roughness of physically based shaded surfaces, 0 to 1.\n"
        "Zero means automatic (derived from each material's shininess)."),
    ParamFloat('PBREnvIntensity',  1.0, title='Environment brightness',
        doc="Brightness of the image based lighting environment."),
    ParamString('PBREnvImage', '', title='Environment image',
        doc="Image file used as the image based lighting environment,\n"
        "replacing the built-in procedural studio environment. A 2:1\n"
        "image is read as equirectangular (lat-long), anything squarer\n"
        "as a sphere map — the same convention as the Texture mapping\n"
        "dialog's Environment mode, so the same file works in both.\n"
        "Empty falls back to that dialog's current image, then to the\n"
        "procedural environment."),
    ParamBool('PBREnvEmbed', False, title='Embed environment image',
        doc="Store a copy of the environment image inside the document,\n"
        "so it travels with the file instead of depending on the\n"
        "original path. The copy lives in the view's\n"
        "Render_PBREnvImageData property and takes precedence over the\n"
        "image path while set."),
    ParamBool('PBREnvBackground', False, title='Environment background',
        doc="Show the image based lighting environment itself as the view\n"
        "background while PBR shading is active, so reflective surfaces\n"
        "visibly mirror their surroundings."),
    ParamFloat('BumpScale',  1.0, title='Bump strength',
        doc="Strength of bump/normal mapped surfaces (SoBumpMap) of the\n"
        "experimental render engine: scales the slope of normal maps and\n"
        "the height amplitude of grayscale bump maps."),
    ParamBool('Parallax',  True, title='Parallax occlusion mapping',
        doc="Parallax-occlusion map grayscale bump maps (SoBumpMap) of the\n"
        "experimental render engine, shifting the texture with the view\n"
        "angle for a strong relief impression."),
    ParamBool('Volumetric',  False, title='Light shafts',
        doc="Enable volumetric lighting (light shafts) of the experimental\n"
        "render engine: raymarch the shadow map of the Shadow draw style\n"
        "through a homogeneous scattering medium. Only effective while\n"
        "the Shadow draw style provides a scene light."),
    ParamFloat('VolumetricIntensity',  1.0, title='Intensity',
        doc="Brightness of the inscattered (light shaft) light."),
    ParamFloat('VolumetricDensity',  0.0, title='Medium density',
        doc="Scattering medium density in inverse world units.\n"
        "Zero means automatic (a fraction of the scene size)."),
    ParamBool('Caustics',  False, title='Water caustics',
        doc="Project an animated caustic light pattern onto surfaces\n"
        "below the water body (objects with the Render_Water property),\n"
        "modulated by the shadow map. Only effective while volumetric\n"
        "lighting and the Shadow draw style are active."),
    ParamFloat('CausticsIntensity',  1.0, title='Caustics intensity',
        doc="Brightness of the projected caustic pattern."),
    ParamFloat('CausticsScale',  0.0, title='Caustics scale',
        doc="Caustic pattern cell frequency in inverse world units.\n"
        "Zero means automatic (a fraction of the water body size)."),
    ParamFloat('CausticsSpeed',  1.0, title='Caustics speed',
        doc="Animation speed of the caustic pattern; zero freezes it."),
    ParamBool('WaterSurface',  False, title='Water surface',
        doc="Shade water bodies (objects with the Render_Water property)\n"
        "as an animated water surface: screen-space refraction of the\n"
        "scene behind it, Fresnel-blended environment reflection and a\n"
        "sun glint from the Shadow draw style light."),
    ParamFloat('WaterWaveStrength',  0.3, title='Wave strength',
        doc="Amplitude of the animated wave perturbation of the water\n"
        "surface normal; zero gives a flat mirror-like surface."),
    ParamFloat('WaterWaveScale',  0.0, title='Wave scale',
        doc="Wave frequency in inverse world units.\n"
        "Zero means automatic (a fraction of the water body size)."),
    ParamFloat('WaterWaveSpeed',  1.0, title='Wave speed',
        doc="Animation speed of the water surface waves; zero freezes\n"
        "them."),
    ParamFloat('WaterAbsorption',  0.2, title='Absorption',
        doc="Beer-Lambert absorption strength of the water surface\n"
        "refraction: the refracted scene is dimmed and tinted by the\n"
        "water column it travels through (channels the water color lacks\n"
        "are absorbed most), so the water gains body and the bottom\n"
        "recedes with depth. Zero = crystal clear."),
    ParamFloat('WaterInscatter',  0.5, title='In-scatter',
        doc="How much the water's own color is added back into the\n"
        "depth-absorbed refraction (in-scattering); zero leaves absorbed\n"
        "regions dark, one fills them with the water color."),
    ParamBool('WaterRefraction',  True, title='Refraction',
        doc="Screen-space refraction of the scene behind the water\n"
        "surface. When off the surface shows a flat water colour instead\n"
        "of the see-through refracted scene."),
    ParamBool('WaterReflection',  True, title='Reflection',
        doc="Reflection on the water surface (Fresnel-blended). When off\n"
        "the surface only refracts. See WaterPlanarReflection for the\n"
        "reflection method."),
    ParamBool('WaterPlanarReflection',  True, title='Planar reflection',
        doc="Reflection method when WaterReflection is on: planar (a\n"
        "mirror-camera re-render of the scene about the water plane -\n"
        "exact, no taper) when true, else screen-space reflection (a\n"
        "cheaper per-pixel ray march that can only reflect on-screen\n"
        "geometry and tapers past it). The environment cubemap is the\n"
        "fallback for both."),
    ParamBool('WaterShadow',  True, title='Water shadow',
        doc="Receive the scene light's shadow on the water surface: a\n"
        "shadow band on the water where a caster blocks the light and\n"
        "the sun glint killed there. Requires the Shadow draw style\n"
        "with an active shadow map; off leaves the surface fully lit.\n"
        "The refracted scene below the surface keeps its own shadow\n"
        "regardless."),
    ParamInt('WaterRippleType',  0, title='Ripple type',
        proxy=ParamComboBox(items=['Waves (directional)', 'Rain (drops)']),
        doc="The animated ripple pattern on the water surface. 0 = waves:\n"
        "the default sum of directional wind waves. 1 = rain: circular\n"
        "rings expanding from randomly placed, randomly timed drop\n"
        "impacts, as on a pond in rainfall."),
    ParamFloat('WaterRippleDensity',  1.0, title='Ripple density',
        doc="Drop density of the rain ripple type: how many drop cells\n"
        "fit per wave-scale unit. Higher rains harder - more, smaller\n"
        "rings; lower gives sparse large rings. The wave ripple type\n"
        "ignores it."),
    ParamFloat('WaterShadowWobble',  1.0, title='Shadow wobble',
        doc="How much the shadow band on the water surface wobbles with\n"
        "the wave field: the shadow is looked up at the wave-displaced\n"
        "surface point scaled by this factor. Zero pins the shadow\n"
        "boundary to the flat surface (a straight edge), one is the\n"
        "physical wave height, larger values exaggerate the ripple."),
    ParamBool('Bloom',  False, title='Bloom',
        doc="Bleed a blurred glow halo from bright pixels and from\n"
        "light-source bodies (objects with the Render_Light property)\n"
        "over their surroundings."),
    ParamFloat('BloomThreshold',  0.9, title='Bloom threshold',
        doc="Scene brightness above which a pixel feeds the glow halo\n"
        "(with a soft knee below it). Light-source bodies always feed\n"
        "it regardless, scaled by their intensity."),
    ParamFloat('BloomIntensity',  1.0, title='Bloom intensity',
        doc="Brightness multiplier of the composited glow halo."),
    ParamFloat('BloomRadius',  1.0, title='Bloom radius',
        doc="Radius scale of the glow halo. One is the default gaussian\n"
        "footprint; larger blooms wider."),
    ParamBool('SunDisc',  False, title='Sun disc',
        doc="Draw a visible sun -- a bright disc with a limb glow -- in\n"
        "the sky along the Shadow draw style's directional scene light,\n"
        "occluded by geometry and feeding the bloom glow. Perspective\n"
        "cameras only; spot lights have no sky direction."),
    ParamFloat('SunDiscSize',  1.5, title='Sun disc size',
        doc="Angular radius of the sun disc in degrees (the real sun is\n"
        "about 0.27; larger reads better in a CAD scene)."),
    ParamBool('GroundReflection',  False, title='Ground reflection',
        doc="Mirror the model in the shadow ground plane of the\n"
        "experimental render engine: the opaque scene is re-rendered\n"
        "with a reflected camera and blended onto the ground. Only\n"
        "effective while the Shadow draw style shows a ground plane."),
    ParamFloat('GroundReflectionIntensity',  0.4, title='Reflection intensity',
        doc="Blend factor of the mirrored model on the ground plane."),
    ParamInt('DebugViewMode',  0, title='Debug view mode',
        proxy=ParamComboBox(items=['Off', 'Depth', 'Normal', 'AO', 'Shadow',
                                   'ShadowTile', 'Overdraw', 'ShadowFilter',
                                   'UV']),
        doc="Render debugging buffer visualization (docs/RenderDebug.md).\n"
        "Routes an intermediate render target to the screen instead of the\n"
        "shaded scene: 1 = linearized scene depth, 2 = view-space normals,\n"
        "3 = ambient occlusion term only, 4 = shadow term only, 5 = shadow\n"
        "map / bulb-tile coverage as color, 6 = overdraw heatmap, 7 =\n"
        "shadow-moment filtering-precision probe, 8 = UV / texcoord.\n"
        "0 renders normally. The on-top, highlight and overlay passes\n"
        "still draw on top so the view stays navigable."),
    ParamBool('DebugFreezeFrame',  False, title='Debug freeze frame',
        doc="Freeze every intentionally time- or history-dependent render\n"
        "input: temporal accumulation and per-frame sampling jitter, and\n"
        "time-driven animation (water waves, fire). Two frames of the same\n"
        "scene, camera and parameters then render identically -- the\n"
        "determinism switch for golden-image comparison\n"
        "(docs/RenderDebug.md)."),
    ParamBool('DebugLabel',  False, title='Debug capture label',
        doc="Burn a self-describing label into a corner of the rendered\n"
        "frame while render debugging: the active debug view mode, the\n"
        "freeze-frame state and any custom RenderDebug_* parameter values.\n"
        "A captured PNG then documents its own settings without its\n"
        "sidecar (docs/RenderDebug.md)."),
    ParamBool('DebugTiming',  False, title='Render stage timing',
        doc="Log where the time of a rendered frame goes, by pipeline\n"
        "stage: the Coin traversal, the flattening of the vertex caches,\n"
        "the draw-entry build, the translation to the backend, the\n"
        "backend's own bookkeeping and the draw itself. One summary line\n"
        "per second, so a long operation shows how each stage grows with\n"
        "the scene rather than one average (docs/IncrementalPublish.md)."),
    ParamBool('DebugCoverage',  False, title='Screen coverage histogram',
        doc="Log how much of the screen each drawn object actually covers,\n"
        "as a histogram over its projected size in pixels. A camera that\n"
        "sees a whole assembly draws most of it at a few pixels, and every\n"
        "one of those parts still costs a full object; the histogram says\n"
        "how much of the model is in that state, which is what decides\n"
        "whether aggregating distant parts is worth building\n"
        "(docs/FarFieldProxies.md §9)."),
]

def declare_begin():
    params_utils.declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
