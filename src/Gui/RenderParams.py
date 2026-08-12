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
        doc="Ladder level shapes are tessellated at under coarse-first\n"
        "(docs/SceneStreaming.md #7): the display mesh is built at this\n"
        "rung of the fidelity ladder and the exact tessellation is\n"
        "declared unbuilt, generated on demand when a camera asks for it.\n"
        "0 is the coarsest rung, each level halves the error; -1 always\n"
        "tessellates exact up front (pre-ladder behavior).\n"
        "\n"
        "Consulted whenever something can deliver the exact rung on\n"
        "demand, which is a scene stream server (its viewers ask) OR a\n"
        "desktop view in render cache mode 3 whose backend drives mesh\n"
        "levels (its own level plan asks when the camera settles) - see\n"
        "PartGui::coarseTessellationLevel. Plain Coin display has neither\n"
        "and keeps the exact tessellation, because a coarse build there\n"
        "would stay coarse forever. This is not a serving-only feature,\n"
        "and it does engage for geometry built while a document loads.\n"
        "\n"
        "What a serving process lacks is not this setting but the local\n"
        "level plan, which is disabled there - its rungs refine only where\n"
        "a connected viewer's camera asks, so with no viewer attached they\n"
        "stay coarse, while a desktop view refines its own. That, not the\n"
        "setting, is why the two publish different geometry for one\n"
        "document (measured on a 40-object scene: 8310 vertices serving,\n"
        "25595 on the desktop). Desktop refinement is tolerance-limited,\n"
        "so what it settles at is a property of the framing.\n"
        "\n"
        "A level whose rung is already finer than a shape's exact\n"
        "tessellation coarsens nothing, so on small shapes the low levels\n"
        "do nothing visible - the default 2 is a no-op on a scene of small\n"
        "ellipsoids that level 0 visibly coarsens.\n"
        "\n"
        "The FC_COARSE_TESSELLATION environment variable overrides this for\n"
        "a whole process and returns before the gate is evaluated, so it\n"
        "forces coarse-first on where the gate would have refused. Takes\n"
        "effect when a shape (re)tessellates."),
    ParamInt('CoarseDeferFaces',  1000, title='Coarse defer face threshold',
        doc="During a progressive import on the bgfx renderer, a shape with\n"
        "more faces than this gets a bounding-box stand-in immediately and\n"
        "even its coarse tessellation is built on the refine worker pool,\n"
        "swapped in when it arrives (docs/SceneStreaming.md #13) - the\n"
        "import stall otherwise scales with the largest single part. -1\n"
        "disables the stand-in so every shape tessellates inline."),
    ParamBool('MeshSkipRedundant',  True, title='Skip redundant tessellation',
        doc="Ask the shape whether it is already tessellated the way this\n"
        "rebuild wants it, and skip the tessellation call outright when it\n"
        "is (docs/SceneStreaming.md #13e).\n"
        "A visual rebuild always called BRepMesh_IncrementalMesh, on the\n"
        "assumption that a mesh already resident makes the call nearly\n"
        "free. Measured, it does not: half the calls of a mass descent --\n"
        "2462 of 4942 -- changed no triangle at all and still cost about\n"
        "19ms each, 27% of the whole descent's rebuild time, because\n"
        "reaching the conclusion means building OCCT's internal mesh model\n"
        "of the shape first.\n"
        "The check asks the same question that model would have answered,\n"
        "off the triangulations already hanging on the faces: OCCT's own\n"
        "consistency rule (BRepMesh_ModelPreProcessor), per face, plus the\n"
        "3D polygon of every free edge. It is all-or-nothing per shape and\n"
        "deliberately the stricter test -- one face that would be\n"
        "re-tessellated, one triangulation with an index out of range, and\n"
        "the call runs exactly as before, because the fallback is the real\n"
        "thing and there is nothing to gain by guessing.\n"
        "A resident mesh FINER than the ask is not adequate. That is not\n"
        "an oversight: the descent asks for a coarser mesh on purpose, to\n"
        "give memory back, and OCCT would coarsen it. Skipping there would\n"
        "quietly hold the memory the plan asked for.\n"
        "Off, the call is made unconditionally, as it always was. With the\n"
        "level plan narrating, the off arm also reports how often the\n"
        "check and the call agreed, which is what says the check is safe."),
    ParamBool('MeshSkipFinerResident',  False, title='Skip when the mesh is finer than asked',
        doc="Count a resident mesh FINER than the rebuild asked for as\n"
        "adequate, instead of re-tessellating to coarsen it\n"
        "(docs/SceneStreaming.md #13e). Only consulted when redundant\n"
        "tessellation is being skipped at all.\n"
        "Strictly, finer is not adequate: the descent asks coarse on\n"
        "purpose to hand memory back, and OCCT coarsens the mesh when\n"
        "asked with quality decrease allowed. That is why the check\n"
        "refuses it by default -- accepting it would be the feature\n"
        "quietly holding the memory the level plan asked for.\n"
        "Measured on the descent, though, that is what the refusal is\n"
        "actually costing and it is nearly all of it: 2599 of the 2765\n"
        "refused calls had a resident mesh exactly twice as fine as the\n"
        "ask -- the previous ladder rung, one dynamic scale step back --\n"
        "and every one of them changed no triangle when the call was\n"
        "made anyway. The faces were already at their floor; a face of\n"
        "two triangles does not coarsen.\n"
        "So this trades a coarsening that mostly achieves nothing for the\n"
        "~19ms it costs to find that out. What it risks is the minority\n"
        "where the coarsening WOULD have removed triangles, which is\n"
        "memory the plan then has to recover some other way -- through\n"
        "the refine pool's own coarser rung, where it was always meant to\n"
        "come from.\n"
        "OFF BY DEFAULT, and the reason is that risk, measured. Audited\n"
        "with every call still made so the check can be scored against\n"
        "what the call actually did, this rule predicted 3381 calls\n"
        "redundant and 753 of them -- 22%, better than one in five --\n"
        "rebuilt anyway. Those are real coarsenings it would have\n"
        "skipped, and real memory the plan would not get back. The\n"
        "strict rule's own score on the same instrument is 1 in 7403.\n"
        "/!\\ Never read that count from a run with the skip ON: a call\n"
        "that is skipped is never made, so nothing can say whether it\n"
        "would have rebuilt, and the wrong-verdict column can only\n"
        "count calls the check refused. A zero there is guaranteed by\n"
        "construction rather than earned."),
    ParamBool('ProgressiveLoad', True, title='Progressive document load',
        doc="Build the visual representation of a restored document after\n"
        "the load instead of inline inside it. Opening a large document\n"
        "otherwise tessellates every shape on the main thread while\n"
        "nothing paints - the visual build is the largest single stage of\n"
        "a load. Deferred, the window comes up first and the parts appear\n"
        "in bounded slices with the view painting between them. Read as\n"
        "each restored shape asks for its visual."),
    ParamInt('ProgressiveLoadBudgetMS',  100, title='Progressive load slice (ms)',
        doc="How long one slice of deferred visual building may run before\n"
        "returning to the event loop, when Progressive document load is\n"
        "on. Larger finishes the document sooner, smaller keeps the window\n"
        "more responsive while it fills in. Each slice is paid for with a\n"
        "repaint of a large scene, which is why slices this long are worth\n"
        "it - much smaller and the fill is paced by redraws rather than by\n"
        "the building. Read at each slice."),
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
    ParamBool('LevelDebug',  False, title='Level plan debug',
        doc="Narrate what each mesh-level plan decides (docs/SceneStreaming.md\n"
        "#13): the GPU budget it decided against and the bytes in use, how\n"
        "many displayed sources stand at their coarse and exact rungs, and\n"
        "how many refines, demotes and downgrades the plan asked for.\n"
        "Reported on the plan's own cadence - a camera pause - because it\n"
        "is a decision, not a per-frame cost.\n"
        "Needed to tell a ladder that will not descend apart from one that\n"
        "never ran: on the desktop OpenGL backend the automatic GPU budget\n"
        "is 0 (bgfx's GL renderer reports no limit), so the downgrade half\n"
        "of the plan never executed at all and nothing said so.\n"
        "The FC_LEVEL_DEBUG environment variable also turns it on. Read\n"
        "once, at the first plan."),
    ParamInt('LevelCeilingSimulateMB',  0, title='Simulate memory ceiling below (MB)',
        doc="Pretend the system ran out of memory for exact re-tessellation\n"
        "(docs/SceneStreaming.md #13), so the CPU-side half of the level\n"
        "plan can be exercised on a machine that has memory to spare.\n"
        "Non-zero raises the floor that the refine worker compares\n"
        "available memory against, so builds are refused and a memory\n"
        "ceiling is observed - after which the plans start demoting exact\n"
        "meshes the camera would not miss back to their coarse rung.\n"
        "A simulation knob, not a tuning one: LevelMemoryFloorMB is the\n"
        "real floor, and this overrides it upward only.\n"
        "Read when a refine is dequeued, so it takes effect live."),
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
    ParamFloat('LevelPressureRelease',  0.5, title='Level pressure release',
        doc="How much of the raised refine tolerance the plan keeps each\n"
        "time it comes in under the GPU budget (docs/SceneStreaming.md\n"
        "#13c.3). While the budget is exceeded the plan accepts visible\n"
        "error to fit the scene, and it must not hand that error straight\n"
        "back the moment one plan fits: measured on a 5455-object model at\n"
        "a 64MB budget, clearing it in one step took the tolerance from\n"
        "51 pixels to 2, asked 946 objects to re-tessellate at once, broke\n"
        "the budget again and cycled -- 43 plans in 611 seconds with no\n"
        "steady state at any point.\n"
        "So quality comes back in steps: each plan that fits keeps this\n"
        "fraction of the standing tolerance, and a step that puts the\n"
        "scene back over budget is remembered as a floor the release never\n"
        "passes again, so the ladder settles at the coarsest tolerance\n"
        "that actually fits instead of oscillating around it. The floor is\n"
        "forgotten when the camera moves or the budget changes, which is\n"
        "when what a rung costs on screen changes.\n"
        "Smaller gives quality back faster and risks the cycle; larger is\n"
        "gentler and slower. 0 or less restores the immediate snap."),
    ParamInt('LevelCount',  8, title='Ladder rung count',
        doc="How many rungs the fidelity ladder declares\n"
        "(docs/SceneStreaming.md #13). Rung n is tessellated at a\n"
        "deflection of the shape diagonal over 8<<n, so rung 0 is the\n"
        "coarsest and each further rung halves the error; this bounds\n"
        "what Coarse tessellation level may select and how far a source\n"
        "may climb. Raising it adds finer rungs, not coarser ones -- to\n"
        "go below rung 0 the plan scales an object's error instead, see\n"
        "Level scale."),
    ParamFloat('LevelScale',  2.0, title='Dynamic coarseness scale',
        doc="What the level plan multiplies an object's error by when it\n"
        "must free memory and every ordinary descent is exhausted\n"
        "(docs/SceneStreaming.md #13). Rung 0 is not the floor: under a\n"
        "budget the plan keeps picking objects -- individually, cheapest\n"
        "visible error first, never the whole scene at once -- and\n"
        "re-tessellates each one this much coarser again, until the\n"
        "model fits. An object whose scaled error reaches Level scale\n"
        "box error is replaced by its bounding box, which is the real\n"
        "floor: coarsening a deflection cannot drop a planar face below\n"
        "the two triangles it always has, and on a measured STEP\n"
        "assembly a 4x coarser tessellation removed only 19% of the\n"
        "primitives. 1 or less turns dynamic scaling off, and then a\n"
        "budget under what rung 0 costs cannot be honoured."),
    ParamFloat('LevelScaleBoxError',  0.25, title='Level scale box error',
        doc="The scaled error at which an object stops being tessellated\n"
        "at all and is drawn as its bounding box (12 triangles whatever\n"
        "its face count), expressed relative to the shape diagonal. This\n"
        "is where the ladder stops paying for topology it can no longer\n"
        "resolve: past roughly a quarter of the diagonal a re-tessellated\n"
        "shape and its box commit similar error, and only the box\n"
        "actually removes the faces. 0 or less never substitutes a box."),
    ParamBool('SimplifyExhausted',  True, title='Decimate when tessellation is spent',
        doc="When re-tessellating an object coarser stops removing\n"
        "geometry, decimate the mesh it already has instead of dropping\n"
        "straight to its bounding box (docs/SceneStreaming.md #13c).\n"
        "The descent coarsens an object by asking OCCT for a larger\n"
        "deflection, and that saturates: a planar face is two triangles\n"
        "at any deflection, so a shape of flat faces answers the same\n"
        "mesh however coarse the ask. Past that point the only thing\n"
        "that removes geometry is a representation with fewer faces.\n"
        "Vertex clustering is the rung between the two: it keeps the\n"
        "object's shape, where the bounding box does not.\n"
        "Rewrites the display nodes only. Nothing re-tessellates and the\n"
        "OCCT triangulation is untouched, so the way back is one ordinary\n"
        "rebuild, and each further step down clusters on a coarser grid.\n"
        "Face and edge numbering survive: a face that decimates away to\n"
        "nothing keeps its (empty) slot, because those tables are read by\n"
        "element number.\n"
        "What it gives up is exactness of the decimated rung -- section\n"
        "caps through it can be rough, since clustering does not preserve\n"
        "watertightness, and the hidden-line seam filter is dropped\n"
        "because a welded edge may fold a seam and a non-seam together."),
    ParamBool('SimplifyMergeParts',  False, title='Decimate across faces',
        doc="Let the decimator weld vertices across face boundaries\n"
        "instead of clustering each face on its own grid.\n"
        "Off, no output triangle spans two faces, so a modelled crease\n"
        "stays a crease and each face keeps at least the triangles its\n"
        "own cells produce. That floor is the catch: this rung is reached\n"
        "precisely when a shape is mostly flat faces, and per-face\n"
        "clustering cannot take a two-triangle face below two triangles.\n"
        "On, positions and attributes cluster once over the whole mesh,\n"
        "which is what actually removes geometry there -- at the cost of\n"
        "shading round creases the model really has.\n"
        "Face identity survives either way: a triangle still belongs to\n"
        "the face it came from, so per-face colour and selection keep\n"
        "working. Only the geometry is shared."),
    ParamFloat('SimplifyMinReduction',  20.0, title='Decimation worth doing (%)',
        doc="How much of an object's triangle count a decimation pass has\n"
        "to remove for the result to be kept, as a percentage.\n"
        "Below it the pass is refused and the descent takes its next step\n"
        "instead, which is the bounding box. A rung that removes almost\n"
        "nothing is worse than not having one: it costs a node rewrite\n"
        "and still holds the memory that made the plan ask.\n"
        "This is also what stops the descent looping. Each step clusters\n"
        "on a coarser grid, so a mesh that has run out of things to merge\n"
        "keeps answering no and the object moves on to the box."),
    ParamBool('ShapeVertices',  False, title='Draw edge-attached vertices',
        doc="Draw the vertex points that sit on the ends of a shape's\n"
        "edges (docs/SceneStreaming.md #13b). Off by default: such a point\n"
        "lands exactly on an edge that is already drawn, so it adds\n"
        "nothing to look at -- and it is not cheap. A point costs the GPU\n"
        "a 32-byte sprite instance record plus its index, roughly nine\n"
        "times what the same point occupies in the heap, which is why a\n"
        "CPU-currency measurement made them look negligible.\n"
        "All or nothing per point set: it is skipped only when EVERY one\n"
        "of its vertices is an edge endpoint. One floating vertex -- one\n"
        "no edge touches, and every point of a point cloud -- and the\n"
        "whole set draws, because nothing else would show it. Objects are\n"
        "in practice all floating or none, so a per-vertex subset would\n"
        "buy nothing and cost an index permutation.\n"
        "It never applies in the Points display mode, where the vertices\n"
        "are what the mode exists to show.\n"
        "Picking, pre-selection and selection highlighting are unaffected:\n"
        "the point geometry stays published and resident, the highlight\n"
        "draws render on top as always, and only the base-pass submission\n"
        "is skipped."),
    ParamBool('PressureDropEdges',  True, title='Drop face edges under pressure',
        doc="Stop drawing the edges that bound faces while the GPU memory\n"
        "budget stands exceeded (docs/SceneStreaming.md #13b), and draw\n"
        "them again as soon as it does not. Edge geometry is the GPU's\n"
        "most expensive geometry per unit of screen information: a segment\n"
        "is 8 bytes of index in the heap and those 8 bytes plus a 64-byte\n"
        "quad-expansion instance record on the GPU.\n"
        "All or nothing per edge set: it is skipped only when EVERY one\n"
        "of its edges bounds a face, because those faces still draw and\n"
        "their silhouettes still read. One floating edge -- a wire, a\n"
        "sketch, a datum line, any edge no face uses -- and the whole set\n"
        "draws: it is the object, and dropping it would show nothing at\n"
        "all.\n"
        "It never applies in the Wireframe display mode, where the edges\n"
        "are what the mode exists to show.\n"
        "A display gate, not a residency change -- nothing is demoted and\n"
        "nothing re-tessellates, so entering and leaving it costs one\n"
        "frame, which is why it is spent before any rung is given up.\n"
        "Picking, highlighting and on-top rendering are unaffected."),
    ParamBool('LoadDropElements',  True, title='Drop elements while loading',
        doc="Stop drawing edges AND vertices for as long as a document is\n"
        "still arriving (docs/SceneStreaming.md #13b), and let the two\n"
        "standing gates above decide again the moment it has finished.\n"
        "A load is when the tier can least afford those two classes and\n"
        "can least use them: the faces are arriving coarse-first and\n"
        "being replaced under the camera, nobody inspects a vertex of a\n"
        "model that is still half there, and every byte not uploaded to\n"
        "an edge instance buffer now is one the arriving geometry gets\n"
        "instead.\n"
        "MEASURED: on a .FCStd open this currently suppresses NOTHING,\n"
        "and not because it fails to fire. Progressive document load\n"
        "parks every visual build and publishes the scene in one step\n"
        "when the load is done, so the renderer holds an empty scene for\n"
        "the whole load -- 0 drawables across 17.8s on a 5455-object\n"
        "model. It is kept for the case that is still real, a live\n"
        "progressive import, which builds its visuals inline as objects\n"
        "appear; that case is not yet measured.\n"
        "It overrides both gates while it lasts -- vertices drop even\n"
        "with ShapeVertices on, edges drop with no pressure yet declared\n"
        "-- but it is subject to the same all-or-nothing classification\n"
        "and the same display-mode exemptions: a wire, a sketch, a datum\n"
        "line or a point cloud draws throughout, because nothing else on\n"
        "screen would show it, and neither class is dropped in the mode\n"
        "that exists to show it.\n"
        "Costs one frame to leave, like the pressure gate, so what it\n"
        "holds back comes straight back when the load lets go.\n"
        "Applies only where coarse-first is on (CoarseTessellation 0 or\n"
        "above): with everything tessellated exact up front there is no\n"
        "progressive arrival for this to make room for.\n"
        "A load here means a document restoring, a progressive import\n"
        "filling one, or the deferred view-provider drain that follows a\n"
        "restore -- geometry is still being built into the view in all\n"
        "three."),
    ParamFloat('EffectResolution',  1.0, title='Effect resolution',
        doc="Resolution scale (0.25-1.0) of the expensive screen-space effect\n"
        "passes -- the planar/ground reflection scene re-render, the water\n"
        "body depth prepass and screen-space ambient occlusion -- relative to\n"
        "the main view resolution. Lowering it trades effect sharpness for\n"
        "speed on large windows, where those per-pixel passes dominate the\n"
        "frame; the main geometry, edges, text and overlays stay full\n"
        "resolution. 1.0 renders the effects at full resolution. The\n"
        "volumetric light shafts already render at half resolution."),
    ParamBool('Occlusion',  False, title='Occlusion culling',
        doc="Skip drawing what the depth buffer proves could not have\n"
        "reached the screen (docs/FarFieldProxies.md §12). Bounding boxes\n"
        "of the spatial index's nodes are tested against the depth the\n"
        "occluders leave behind -- by default in a software depth buffer\n"
        "on the CPU (Render_OcclusionSoftware), which answers within the\n"
        "frame that asked -- and a node that puts no pixel through has\n"
        "its whole subtree skipped, one test standing for thousands of\n"
        "draws.\n"
        "\n"
        "Exact, not approximate: only geometry that could not have been\n"
        "seen is removed, so the image is unchanged and what is saved is\n"
        "the draw call, which measures ~1.2-1.5us of CPU submission plus\n"
        "~1.5-1.7us of GPU time whatever it contains (§10.2). It pays on\n"
        "assemblies that hide themselves -- an enclosed chassis, a\n"
        "populated rack, any interior -- and does nothing for a model\n"
        "that is mostly silhouette. Expect roughly a fifth of the draws\n"
        "from a camera inside a large assembly (§10.3); the far larger\n"
        "figure from outside a closed model is a bound, not a promise.\n"
        "\n"
        "Casters and reflections are judged separately: geometry hidden\n"
        "from the eye still casts its shadow and still appears in the\n"
        "ground reflection."),
    ParamInt('OcclusionVisibleTtl',  6, title='Occlusion visible lifetime',
        doc="How many frames a node found visible is believed before it is\n"
        "tested again. Higher spends fewer queries and keeps drawing\n"
        "geometry that has since become hidden for a little longer; lower\n"
        "tracks the camera more closely at the cost of more tests. Purely\n"
        "a cost trade -- being late here draws too much, never too\n"
        "little, so it cannot affect the image."),
    ParamInt('OcclusionBudget',  128, title='Occlusion query budget',
        doc="How many occlusion tests one frame may issue. The GPU offers\n"
        "256 for the whole process and the RenderDebug_Occlusion\n"
        "measurement is the other claimant, so the default leaves that\n"
        "measurement room to run alongside. Asking for more tests than\n"
        "the budget allows is not an error: hidden nodes are offered\n"
        "first, since a test is the only way one can come back, and the\n"
        "rest are offered again next frame."),
    ParamInt('OcclusionMinSubtree',  8, title='Occlusion minimum subtree',
        doc="Do not test an index node standing for fewer drawn instances\n"
        "than this. A test is itself a draw, so testing a node that could\n"
        "save one draw loses whether it answers hidden or visible."),
    ParamInt('OcclusionMaxHidden',  120, title='Occlusion hidden lifetime',
        doc="How many frames a hidden node may go without an answer before\n"
        "it is drawn again. A hidden node is re-tested continuously and\n"
        "the answer is its only way back, so if answers stop arriving --\n"
        "no query handles left, a dropped batch -- this is what returns\n"
        "the geometry instead of leaving it missing. Answers that keep\n"
        "confirming the node is hidden keep it hidden indefinitely, so\n"
        "this never flickers a node the tests are still reaching."),
    ParamInt('OcclusionDepthPad',  16, title='Occlusion depth padding',
        doc="How far a test box is pushed towards the viewer before it is\n"
        "tested, in steps of the 24-bit depth buffer. A test box has to\n"
        "be a conservative bound, and at the last bit of the depth buffer\n"
        "it is not: a small part lying flush on a large panel quantizes\n"
        "to the same stored depth as the panel, LEQUAL loses the tie\n"
        "whichever way the rasterizer rounds, and the node reports itself\n"
        "hidden while in plain view. Measured that way, the components on\n"
        "a board disappeared while the board stayed. Too large costs\n"
        "frame time by testing visible what could have been skipped; too\n"
        "small deletes geometry, so err high."),
    ParamInt('OcclusionConfirm',  2, title='Occlusion confirmations',
        doc="How many consecutive answers of 'no pixels' a node must give\n"
        "before its geometry is actually skipped. 1 acts on every\n"
        "answer, and is what an occlusion test naively does.\n"
        "\n"
        "A test is issued against one frame's depth and read against a\n"
        "later one -- it does not block, because stalling for it would\n"
        "cost the frame time the culling exists to save -- so while an\n"
        "answer is in flight, other geometry is culled and the occluders\n"
        "move underneath it. Acted on singly, a node tested while an\n"
        "occluder was still drawn gets skipped after that occluder has\n"
        "gone; the hole it leaves tests visible; it comes back; and it\n"
        "oscillates, which is a picture that flickers rather than one\n"
        "that is merely wrong.\n"
        "\n"
        "Confirmations DILUTE that oscillation; measured, they do not\n"
        "remove it (docs/FarFieldProxies.md #12.7): the false answers\n"
        "arrive in runs, so tripling the confirmations bought a factor\n"
        "of two, and the residual damage tracks how often nodes are\n"
        "re-tested, which this setting cannot reach. The query path is\n"
        "therefore not image-stable at any value here; occlusion on the\n"
        "CPU (the default oracle) does not read this setting at all."),
    ParamBool('OcclusionSoftware',  True, title='Occlusion on the CPU',
        doc="Answer the occlusion question with a software depth buffer on\n"
        "the CPU instead of hardware occlusion queries\n"
        "(docs/FarFieldProxies.md #12.12). The default, because it is\n"
        "the one oracle whose picture holds still.\n"
        "\n"
        "A hardware query cannot be asked at the moment its answer would\n"
        "be right. It is issued against one frame's depth and read a\n"
        "frame or two later, so a node is tested after the pass that drew\n"
        "its own geometry and is asked to win a depth comparison against\n"
        "itself -- measured as boxes returning no samples at all while\n"
        "their contents were plainly on screen. The confirmations,\n"
        "lifetimes and padding beside this setting all exist to contain\n"
        "that, and none of them reach it.\n"
        "\n"
        "On the CPU, occluders are rasterized and nodes tested against\n"
        "the same buffer in one pass, so a node is asked before its own\n"
        "geometry joins the buffer and the answer arrives in the frame\n"
        "that asked. There is no latency to age, no verdict to confirm\n"
        "and no query pool to run out of. It costs CPU time in a frame\n"
        "that is already CPU-bound, which is the trade to measure, and it\n"
        "behaves identically in the browser, where hardware queries do\n"
        "not."),
    ParamInt('OcclusionOccluderTris',  250000, title='Occlusion occluder budget',
        doc="How many triangles the CPU occlusion buffer may rasterize in one\n"
        "frame. Only used when occlusion runs on the CPU.\n"
        "\n"
        "Occluders are spent largest-on-screen first, so what the budget\n"
        "drops is what would have hidden least. Dropping them costs\n"
        "culling and never pixels: an occluder that was not rasterized\n"
        "simply hides nothing."),
    ParamInt('OcclusionMinOccluder',  24, title='Occlusion minimum occluder',
        doc="How large a draw must appear on screen, in pixels across its\n"
        "bounding box diagonal, before it is worth rasterizing into the\n"
        "CPU occlusion buffer. Smaller draws can hide almost nothing and\n"
        "spend budget that a larger one could use."),
    ParamInt('OcclusionThreads',  0, title='Occlusion occluder threads',
        doc="How many worker threads the CPU occlusion buffer may rasterize\n"
        "its occluders on. 0 picks automatically, leaving the submitting\n"
        "thread and one other alone -- this runs in the middle of a\n"
        "frame, not on an idle machine.\n"
        "\n"
        "Each worker rasterizes its own slice of the occluder list into\n"
        "its own buffer and the buffers are merged afterwards, so there\n"
        "is no locking. The merge is slightly lossy -- two two-layer\n"
        "blocks cannot combine into one without loss -- so a higher\n"
        "worker count can hide marginally less. Never more."),
    ParamBool('OcclusionSimd',  True, title='Occlusion vector pre-pass',
        doc="Let the CPU occlusion buffer discard triangles four at a time\n"
        "with SIMD before its exact rasterizer looks at them\n"
        "(docs/FarFieldProxies.md #12.14).\n"
        "\n"
        "Two thirds of the triangles offered to the buffer cover no pixel\n"
        "at all -- a full-detail CAD tessellation is mostly triangles\n"
        "smaller than the pixel grid -- and every one of them is paid for\n"
        "in full before being thrown away. The pre-pass transforms and\n"
        "projects four at once in single precision and drops the ones that\n"
        "land on no pixel centre.\n"
        "\n"
        "It cannot make the buffer claim a surface that is not there:\n"
        "everything it does not discard is handed to the same exact path\n"
        "as before, recomputed from the original vertices, and a triangle\n"
        "it drops in error is occlusion lost rather than geometry deleted.\n"
        "Turn it off to measure what it saves, not to work around a\n"
        "suspected fault."),
    ParamInt('OcclusionResolution',  1, title='Occlusion buffer divisor',
        doc="Resolution of the CPU occlusion buffer, as a divisor of the\n"
        "viewport. 1 matches the viewport.\n"
        "\n"
        "Above 1 this can remove geometry that was visible, which is the\n"
        "one failure this mechanism exists to avoid: a coarse pixel is\n"
        "marked covered when an occluder reaches its centre, but it\n"
        "stands for several real pixels, and the ones the occluder missed\n"
        "are claimed with it. Reduce it only to measure what it costs, not\n"
        "as a setting."),
    ParamBool('OcclusionPerInstance',  True, title='Occlusion per instance',
        doc="Test each object against the CPU occlusion buffer, not just the\n"
        "group it was partitioned into\n"
        "(docs/FarFieldProxies.md #12.17). Only used when occlusion runs\n"
        "on the CPU.\n"
        "\n"
        "The cull walk tests boxes of groups, and a group is skipped only\n"
        "when all of it is hidden -- so one visible object keeps its\n"
        "hidden neighbours on screen. Measured, that is what limits the\n"
        "culling rather than the quality of the depth buffer: after a\n"
        "cull, 91% of what is still drawn reaches no pixel, and making\n"
        "the occluders ten times better barely moved it.\n"
        "\n"
        "The extra tests are read-only against a buffer that is already\n"
        "finished, so they run on the same worker threads the occluders\n"
        "used and add no state, no latency and nothing the backend has to\n"
        "support.\n"
        "\n"
        "On by default: measured on the benchmark it hides 17% more for\n"
        "0.4ms, against 3% for 3.4ms from making the occluders ten times\n"
        "better, and it over-culls nothing. It can only ever be more\n"
        "correct than testing the group -- a draw is skipped when its own\n"
        "box is covered rather than when its neighbours' collectively\n"
        "are."),
    ParamInt('OcclusionDemoteStreak',  8, title='Occlusion demote streak',
        doc="How many consecutive frames every draw of an object must have\n"
        "been culled before the level plan's downgrade sweep may treat\n"
        "it as free -- give its GPU upload back without charging the\n"
        "camera any visible error. 0 never does. Only used when\n"
        "occlusion runs on the CPU, whose verdicts are exact per frame.\n"
        "\n"
        "This is occlusion acting as a MEMORY mechanism: an enclosed\n"
        "assembly's interior is inside the view frustum, so without a\n"
        "hidden verdict the plan prices its downgrade as visible error\n"
        "and pays for it in quality somewhere that actually shows. What\n"
        "the sweep drops stays resident in CPU RAM; the way back is an\n"
        "ordinary refine, so a verdict the camera later overturns costs\n"
        "one upload. The streak is the hysteresis that keeps a drifting\n"
        "camera from paying that upload per flap."),
    ParamBool('OcclusionCoarse',  False, title='Occlusion coarse occluders',
        doc="Rasterize the CPU occlusion buffer's occluders from coarse\n"
        "hulls instead of from their meshes\n"
        "(docs/FarFieldProxies.md #12.16). Only used when occlusion runs\n"
        "on the CPU.\n"
        "\n"
        "An occluder does not need the mesh, it needs the surface, and a\n"
        "hull carries that at a fraction of the triangles. What the\n"
        "triangle budget above buys is what this changes: measured, 1285\n"
        "of 1322 candidate occluders never entered the buffer because 37\n"
        "full-detail draws spent the whole allowance, and the buffer then\n"
        "hid 45% of what was there to hide.\n"
        "\n"
        "The hulls are built by vertex clustering from the meshes the\n"
        "renderer already holds -- no shape, no tessellator -- a few per\n"
        "frame, and cached. A hull recedes by its own measured error\n"
        "before it is rasterized, so it cannot claim to be nearer than\n"
        "the surface it stands for."),
    ParamInt('OcclusionCoarseLevel',  2, title='Occlusion hull level',
        doc="Which rung of the decimation ladder an occluder hull is built\n"
        "at, coarsest first: the clustering grid is an eighth of the\n"
        "mesh's diagonal at 0 and halves per level, so 2 is a\n"
        "thirty-second of it. Lower is cheaper to rasterize and further\n"
        "from the surface; higher approaches the mesh itself."),
    ParamInt('OcclusionCoarseMinTris',  512, title='Occlusion hull minimum',
        doc="How many triangles a draw must carry before it is worth a\n"
        "hull. Below this it is rasterized from its mesh: a hull of a\n"
        "small mesh saves triangles that were never what spent the\n"
        "budget."),
    ParamInt('OcclusionCoarseBuilds',  8, title='Occlusion hull builds',
        doc="How many occluder hulls may be built in one frame. Building is\n"
        "parallel but not free, so a scene that has just come into view\n"
        "acquires its hulls over several frames rather than stalling one.\n"
        "0 freezes the cache at what it already holds."),
    ParamInt('OcclusionCoarseBias',  100, title='Occlusion hull bias',
        doc="How far an occluder hull recedes from the camera before it is\n"
        "rasterized, as a percentage of its own measured displacement.\n"
        "\n"
        "Every point of a hull lies within that displacement of a point of\n"
        "the mesh it was built from, so at 100 the hull cannot be nearer\n"
        "than the surface it stands for -- which is what makes an\n"
        "approximate occluder admissible at all. Below 100 it hides more\n"
        "and may hide geometry that was visible; above 100 it hides\n"
        "progressively less for nothing. 0 rasterizes the hull where it\n"
        "sits, which is the measurement that says whether the bias is\n"
        "needed."),
    ParamInt('OcclusionCoarseMemory',  64, title='Occlusion hull memory',
        doc="What the occluder hull cache may hold, in megabytes, before\n"
        "the least recently used hulls are dropped. A dropped hull costs a\n"
        "rebuild when its occluder comes back into view, never\n"
        "correctness."),
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
        proxy=ParamComboBox(items=['Waves (directional)', 'Rain (drops)',
                                   'None (still)']),
        doc="The ambient ripple pattern on the water surface - the motion\n"
        "the surface has of its own accord. 0 = waves: the default sum\n"
        "of directional wind waves. 1 = rain: circular rings expanding\n"
        "from randomly placed, randomly timed drop impacts, as on a pond\n"
        "in rainfall. 2 = none: a still surface, which leaves only what\n"
        "the scene disturbs - fountain splash rings and the impact rings\n"
        "of particles striking the water still show."),
    ParamFloat('WaterRippleDensity',  1.0, title='Ripple density',
        doc="Drop density of the rain ripple type: how many drop cells\n"
        "fit per wave-scale unit. Higher rains harder - more, smaller\n"
        "rings; lower gives sparse large rings. The wave ripple type\n"
        "ignores it."),
    ParamFloat('WaterImpactStrength',  1.0, title='Impact ring strength',
        doc="Height of the rings raised where particles actually strike\n"
        "the water - a fountain's droplets landing in its own basin.\n"
        "Unlike the rain ripple type these are not a pattern: nothing\n"
        "appears unless something hits the surface, and it appears\n"
        "where it hit. Zero turns them off. Needs a stateful emitter\n"
        "whose step program reports its impacts."),
    ParamFloat('WaterImpactLife',  1.1, title='Impact ring life',
        doc="How long an impact ring lives, in seconds - which is also\n"
        "how far it travels, since a ring is sized to have crossed two\n"
        "cells of the impact map when it dies. Longer makes slower,\n"
        "wider-travelling rings out of the same hits."),
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
                                   'UV', 'Reflection', 'ImpactMap']),
        doc="Render debugging buffer visualization (docs/RenderDebug.md).\n"
        "Routes an intermediate render target to the screen instead of the\n"
        "shaded scene: 1 = linearized scene depth, 2 = view-space normals,\n"
        "3 = ambient occlusion term only, 4 = shadow term only, 5 = shadow\n"
        "map / bulb-tile coverage as color, 6 = overdraw heatmap, 7 =\n"
        "shadow-moment filtering-precision probe, 8 = UV / texcoord,\n"
        "9 = the planar reflection target, 10 = the particle impact map\n"
        "(green where a hit is recorded, brightness its age, red where\n"
        "nothing has ever struck).\n"
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
        "the scene rather than one average (docs/IncrementalPublish.md).\n"
        "Those stages end at submission, so a second line reports what\n"
        "happens after it: the frame's cost on the CPU issuing draw\n"
        "commands against its cost on the GPU drawing them, and the same\n"
        "pair per draw call (docs/FarFieldProxies.md §10.1). Which of the\n"
        "two a scene is bound by is what decides whether a culling scheme\n"
        "has to remove the draw or may leave it to the GPU to reject."),
    ParamBool('DebugDelta',  False, title='Publish change set',
        doc="Log what each published frame actually changed: how many of\n"
        "the scene cache's children the publish reused, how many it added\n"
        "or dropped, and how many separators the traversal below it reused\n"
        "against how many it rebuilt. One summary line per second. A\n"
        "publish rebuilds the whole scene however little moved, and these\n"
        "counts are how much of that rebuild was avoidable\n"
        "(docs/IncrementalPublish.md §5)."),
    ParamBool('DebugCoverage',  False, title='Screen coverage histogram',
        doc="Log how much of the screen each drawn object actually covers,\n"
        "as a histogram over its projected size in pixels. A camera that\n"
        "sees a whole assembly draws most of it at a few pixels, and every\n"
        "one of those parts still costs a full object; the histogram says\n"
        "how much of the model is in that state, which is what decides\n"
        "whether aggregating distant parts is worth building\n"
        "(docs/FarFieldProxies.md §9)."),
    ParamBool('DebugProxyCut',  False, title='Far-field cut estimate',
        doc="Log what a far-field cut would cost this camera, without\n"
        "generating anything: the drawn instances are partitioned into the\n"
        "spatial index of docs/FarFieldProxies.md §3, a frontier is chosen\n"
        "by projected error at several tolerances, and the draws that cut\n"
        "would issue -- one per (cell, material) proxy plus whatever stays\n"
        "exact -- are reported against the draws issued today. This is the\n"
        "number that says whether generating proxies is worth building\n"
        "(§11.1). Also reports the distributions that size the partition:\n"
        "instances and material buckets per cell, per level."),
    ParamBool('DebugOcclusion',  False, title='Occluded fraction',
        doc="Measure how much of what the frame draws could not have\n"
        "reached the screen (docs/FarFieldProxies.md §10.1). Bounding\n"
        "boxes of the spatial index's nodes are re-rasterized against the\n"
        "finished depth buffer under hardware occlusion queries, writing\n"
        "neither colour nor depth, and every instance is attributed to the\n"
        "highest node that rejects it -- so a hidden subtree is counted\n"
        "once, not at every level it is hidden at. Boxes bound their\n"
        "contents loosely and the frustum's own rejections are reported\n"
        "separately, so the hidden share it prints is a floor rather than\n"
        "an estimate. A GPU offers 256 queries at a time, so a large model\n"
        "takes several frames to walk and a line is printed per completed\n"
        "walk, never for a partial one."),
    ParamBool('DebugProxyGen',  False, title='Far-field proxy generation',
        doc="Generate real proxies for a sample of the nodes a far-field\n"
        "cut stops on, and report what they cost and what they commit\n"
        "(docs/FarFieldProxies.md §11.1c). The cut estimate above selects\n"
        "by a node's projected *extent* because no proxy exists yet to\n"
        "have an error; this one merges each (cell, material) group and\n"
        "decimates it, so the error it commits can be measured as a\n"
        "fraction of that extent -- which is the ratio that says whether\n"
        "the estimate reads as its 16px row or its 64px row. Reports\n"
        "alongside it the triangle cost against what instancing already\n"
        "achieves (§7.1) and how much surface area survives, since\n"
        "clustering deletes geometry smaller than a cell rather than\n"
        "shrinking it. Expensive: it builds meshes. Samples a bounded\n"
        "number of nodes and reports how many it skipped."),
    ParamBool('DebugCullAudit',  False, title='Occlusion cull audit',
        doc="Check what the occlusion culling skipped against what the\n"
        "geometry actually put on screen (docs/FarFieldProxies.md §12.9).\n"
        "Every other measurement of the culling compares two pictures and\n"
        "reports how many pixels differ, which says that something is\n"
        "wrong without saying what: this re-rasterizes the scene with the\n"
        "cull mask ignored and each draw writing its own identity instead\n"
        "of a colour, so the ids that own a pixel are an exact answer to\n"
        "which draws reach the screen. Their intersection with the mask is\n"
        "a list of proven over-culls -- each one a named draw with a pixel\n"
        "count -- and the ids that own nothing while being drawn are the\n"
        "converse: the headroom the culling has not taken. Reads the image\n"
        "back to the CPU once a second, so it costs a full-resolution\n"
        "transfer on the frames it reports and nothing while off. Needs a\n"
        "backend with texture readback, which WebGL2 is not."),
    ParamBool('DebugCullBounds',  False, title='Occludee bound diagnostic',
        doc="Measure whether a tighter occludee volume would cull more\n"
        "(docs/FarFieldProxies.md §12.19). After per-instance testing, 90%\n"
        "of the draws a frame still submits reach no pixel while each was\n"
        "tested and answered visible -- so the geometry is hidden and the\n"
        "box around it is not. This re-asks every still-drawn row three\n"
        "ways against the same occluder buffer: with the world box that\n"
        "ships, with the mesh's own box through the model matrix (an\n"
        "oriented box, where the shipping one is the axis-aligned box\n"
        "around it), and with every triangle asked separately -- which is\n"
        "far too slow to ship and is here as the ceiling, since nothing\n"
        "asked about the occludee can beat asking about its geometry. The\n"
        "verdicts are counted against the cull audit's id image, never\n"
        "acted on, so an arm that would have deleted something visible\n"
        "reports itself instead of being believed.\n"
        "Needs the cull audit on (it supplies the image) and the software\n"
        "occluder pass, which owns the buffer being asked. Runs on the\n"
        "audit's frame only, and costs far more than a frame: it is a\n"
        "measurement, not a mode to leave on."),
]

def declare_begin():
    params_utils.declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
