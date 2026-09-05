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
                         ParamHex, ParamColor, ParamComboBox, auto_comment

NameSpace = 'Gui'
ClassName = 'RenderParams'
ParamPath = 'User parameter:BaseApp/Preferences/View/Render'
ClassDoc = 'Convenient class to obtain the experimental render engine parameters'
UserOnChange = 'RenderParams::onRenderParamChanged(sReason);'

Params = [
    ParamString('Type', 'Default', title='Renderer type',
        doc="Type of the experimental render engine backend. 'Default' keeps\n"
        "the plain GL pipeline. Only effective with render cache mode 3."),
    ParamInt('OutputTransform',  1, title='Output colour transform',
        proxy=ParamComboBox(items=['Off', 'sRGB']),
        doc="Whether the engine is colour managed.\n"
        "\n"
        "The shading is linear -- mixes, the GGX lobe, the image based\n"
        "lighting product are all arithmetic on light, and they are only\n"
        "correct on linear numbers. A colour someone picked is not: it\n"
        "is a display number, which makes it sRGB encoded. And a display\n"
        "reads the byte it is handed as sRGB too.\n"
        "\n"
        "'sRGB' honours both ends. Authored colours -- materials, the\n"
        "lights, the background, the base colour and emissive textures --\n"
        "are decoded to linear as they enter, and the finished frame is\n"
        "encoded once at the last write before it is shown. An UNSHADED\n"
        "authored colour therefore survives the round trip exactly, and\n"
        "so does a fully lit surface; what changes is the shading in\n"
        "between, which is the part that was wrong.\n"
        "\n"
        "'Off' is the older pipeline, which did neither: it fed display\n"
        "numbers to the linear shading and wrote the linear result out\n"
        "raw. The two errors partly cancel -- a fully lit surface comes\n"
        "out right -- but everything in falloff and shadow renders about\n"
        "a gamma too dark. Documents written before this existed are\n"
        "drawn that way, which is how they were authored.",
        ),
    ParamFloat('Exposure',  1.0, title='Exposure',
        doc="How much light the frame is developed with, as a plain\n"
        "multiplier on the linear image before it is encoded for the\n"
        "screen. One leaves it alone.\n"
        "\n"
        "It exists because a colour managed scene is lit in real\n"
        "reflectances, and a mid grey reflects about 18 per cent of what\n"
        "falls on it rather than the 45 per cent its number reads as. A\n"
        "scene whose lights were set before that was true is lit about\n"
        "two to three times too dimly, and this is the control that\n"
        "answers it without touching a single light.\n"
        "\n"
        "Raising it does not clip. Anything the multiplier pushes past\n"
        "the top of the range rolls off smoothly instead, and the roll\n"
        "off is exactly nothing below the knee -- so at an exposure of\n"
        "one the frame is bit for bit what it would have been without\n"
        "this stage at all.\n"
        "\n"
        "Only meaningful while the output colour transform is on: with\n"
        "it off the engine is not working in light, and a multiplier\n"
        "there would scale display numbers rather than exposure.",
        ),
    ParamInt('MaxViewIds',  1024, title='Backend view id budget',
        doc="How many backend view ids the render engine may hand out, which\n"
        "is what decides how many 3D views can draw on it at once: each\n"
        "view takes a block for its pass sequence (about 13 ids for a\n"
        "plain viewer, docs/RenderEngine.md #3.1), and a view that finds\n"
        "no block left falls back to plain GL rather than failing. So the\n"
        "default is roughly 64 viewers, and 0 asks for the build's own\n"
        "ceiling instead, which is four times that.\n"
        "\n"
        "It is worth having a limit below the ceiling because the backend\n"
        "copies its whole view table once a frame and sizes its per-view\n"
        "pools from this number, so ids nobody opens are still paid for\n"
        "in every frame. Measured on a desktop GPU that cost is invisible\n"
        "against a 16ms frame at this width - but at the ceiling, with\n"
        "render stage timing on, it is not: the per-view GPU timer pools\n"
        "take a 59fps session to 19. Raise it for many-viewer work, not\n"
        "as a matter of course.\n"
        "\n"
        "Read once, when the backend starts: a change needs a restart."),
    ParamInt('BackgroundReleaseDelay',  1000,
        title='Background view release delay',
        doc="Milliseconds a 3D view may sit in the background before it gives\n"
        "its render targets back, or 0 to let a hidden view keep them.\n"
        "\n"
        "Targets are what a view mostly costs: 287MB was measured for one\n"
        "1644x653 view with every effect on, and until now it held them\n"
        "whether or not anyone could see it -- so a session with several\n"
        "documents open paid for all of their views to look at one. This\n"
        "gives that back for the views nobody is looking at. What the view\n"
        "keeps is everything a resize keeps: its programs, its uniforms\n"
        "and its uploaded scene, so coming back is the resize path and not\n"
        "a reload.\n"
        "\n"
        "The delay is what stops it firing on a click through the tabs.\n"
        "Coming back costs the one frame that rebuilds the targets (~68ms\n"
        "on the view measured above) and gives a byte-identical picture --\n"
        "the trade is a hitch on return against the memory in between,\n"
        "never a difference in the image. Lower it to release sooner on a\n"
        "machine short of VRAM; raise it if switching back and forth\n"
        "hitches."),
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
    ParamBool('MeshSkipInvariant',  True, title='Skip deflection-invariant tessellation',
        doc="Skip a tessellation call on a shape whose mesh provably cannot\n"
        "depend on the deflection asked: every face planar, every edge\n"
        "curve a straight line (docs/SceneStreaming.md #13e). A plane\n"
        "deviates from its triangulation by zero and a straight edge\n"
        "discretizes to its two endpoints at ANY deflection, so the call\n"
        "would rebuild the identical mesh -- there is no ask, coarser or\n"
        "finer, at which such a shape tessellates differently.\n"
        "This is the geometric statement behind the measured descent\n"
        "waste: most mechanical parts hit their floor immediately, and a\n"
        "mass descent then pays ~19-38ms per object per step (56-60% of\n"
        "all drop-phase mesh time on the rack model) for BRepMesh to\n"
        "rebuild what cannot change. The empirical exhaustion proof the\n"
        "ladder keeps (scaleSpent) cannot be used for a skip -- audited\n"
        "twice, 14-20% of proved shapes resume coarsening at some later\n"
        "ask, and those rebuilds reclaim real memory. The geometric rule\n"
        "is immune to that leak: the shapes that resume are exactly the\n"
        "curved ones it refuses to claim, and an all-linear mesh cannot\n"
        "shrink, so no reclaim is ever forgone.\n"
        "The classification walks surface and curve TYPES once per shape\n"
        "and is cached; conservative on both counts (a trimmed or offset\n"
        "plane, a straight b-spline, count as curved). The skip is also\n"
        "refused while any face is missing its triangulation -- building\n"
        "that is exactly the call's job.\n"
        "With the level plan narrating and this OFF, the rule is still\n"
        "evaluated and scored against every call it would have skipped --\n"
        "read its WRONG column from that arm only; a run with the skip on\n"
        "cannot score calls it never made."),
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
    ParamBool('ClimbHardLimit',  True, title='Hard GPU budget for climbs',
        doc="Whether the GPU budget is an absolute ceiling for the level\n"
        "plan's climbs (docs/SceneStreaming.md #13c.5). With it on, a\n"
        "plan whose allocator-exact uploaded total stands at or above\n"
        "the budget admits NO refine and cancels every climb still in\n"
        "flight -- the existing de-want pass aborts them -- and below\n"
        "the ceiling climbs are admitted in small batches (Climb\n"
        "admission batch) so the total approaches the ceiling in\n"
        "verified steps instead of overshooting it in one plan. Judged\n"
        "against the uploaded TOTAL, not the two-frame live census: the\n"
        "census alternates under churn and is what let climbs land\n"
        "over budget. A crossing is bounded by one batch's bytes;\n"
        "per-climb pre-sizing needs rung-keyed GPU cache entries and is\n"
        "future work. Off restores unadmitted climbing."),
    ParamInt('ClimbAdmitBatch',  64, title='Climb admission batch',
        doc="How many refines one plan may admit while the hard climb\n"
        "limit is on and the uploaded total is under budget. Small\n"
        "keeps the possible overshoot small and lets the next plan\n"
        "re-check the allocator-exact total before admitting more;\n"
        "large climbs faster. The set is not ordered by need within a\n"
        "plan, but every plan re-evaluates the whole scene, so nothing\n"
        "starves across plans."),
    ParamInt('LevelLandBudgetMS',  50, title='Level landing budget (ms)',
        doc="How long one event-loop turn may spend landing finished\n"
        "worker jobs (climb refines and descent coarsenings alike).\n"
        "Landings arrive as queued events, and Qt delivers every\n"
        "pending one in a single sweep -- a batch of 64 landings ran\n"
        "back-to-back for measured 1-2.7s stretches in which no paint,\n"
        "timer or input event was served. The pump runs landings until\n"
        "this budget is spent, then yields the loop and reschedules;\n"
        "a single landing larger than the budget still lands whole\n"
        "(items are not sliceable). Small keeps the UI responsive\n"
        "under a landing storm; large lands a converging scene sooner."),
    ParamBool('MeshSkipLanded',  True, title='Skip mesh call on landing rebuilds',
        doc="Whether the rebuild half of a landing skips its OCCT mesh\n"
        "call. A worker landing (climb, scale-descent, stand-in\n"
        "resolution) or a demote/downgrade installs or re-activates the\n"
        "very triangulation the following rebuild displays, and on\n"
        "every such path the resident rung is never coarser than the\n"
        "ask -- BRepMesh there can only validate: measured 18.3s of a\n"
        "92s budget drop (991 validated-only calls, 0.1-0.8s each on\n"
        "large compounds), plus ~1s per landing of a giant re-FAILING\n"
        "the faces the worker's mesher had already failed. Keyed on\n"
        "the path of the one rebuild the landing just prepared, never\n"
        "on the shape's descent history (the exhaustion-proof leak\n"
        "that killed the spent-keyed skip does not reach a per-rebuild\n"
        "claim). Audited at 94 percent exact no-ops; the rest are\n"
        "BRepMesh re-meshing a few faces within ~5 percent of the\n"
        "triangle count in either direction -- perturbation of a rung\n"
        "the ladder chose to display, not reclaim forgone. The level\n"
        "debug flag scores the claim either way; read the 'landed\n"
        "rule' audit line before trusting a change here."),
    ParamBool('VisualFillOnPool',  True, title='Fill landing rebuilds on the refine pool',
        doc="Whether the display-array fill of a big landing rebuild runs\n"
        "on the refine worker pool instead of the GUI thread. After the\n"
        "mesh call was skipped on landings (Skip mesh call on landing\n"
        "rebuilds), the traversal that copies the resident\n"
        "triangulations into the Coin arrays became the per-item floor\n"
        "of the landing pump: 0.3-0.65s per 15-21k-face compound,\n"
        "unsliceable, against a 200ms interactivity gate. With this on,\n"
        "the rebuild captures handles to the resident triangulations\n"
        "and edge polygons (the only state another thread may swap\n"
        "under it -- the topology itself is immutable at runtime),\n"
        "fills detached arrays on a worker, and lands them back through\n"
        "the landing pump as plain array writes. The landing is\n"
        "guarded by the shape identity and a per-object generation\n"
        "count, so a rebuild that ran for any other reason in between\n"
        "simply wins. Only rebuilds inside the landing pump with at\n"
        "least 'Minimum faces for a pooled fill' faces take this path;\n"
        "everything else fills inline exactly as before."),
    ParamInt('VisualFillMinFaces',  2000, title='Minimum faces for a pooled fill',
        doc="How many faces a landing rebuild must have before its\n"
        "array fill goes to the refine pool (Fill landing rebuilds on\n"
        "the refine pool). The fill measures ~30us per face on the\n"
        "reference model, so the default parks roughly the >60ms\n"
        "items; the thousands of small landings in a budget drop stay\n"
        "on the cheap inline path rather than paying a snapshot, a\n"
        "queue hop and a second landing each."),
    ParamInt('WorkerVertexCache',  1, title='Adopt worker-emitted vertex caches',
        doc="Whether a scene publish adopts the vertex-cache content the\n"
        "fill worker emitted at landing instead of re-capturing the\n"
        "shape by traversal (docs/WorkerVertexCache.md). The capture\n"
        "walks every triangle through a hash-dedup a second time to\n"
        "rebuild exactly the arrays the fill already computed; with\n"
        "this on, the worker emits those arrays next to the display\n"
        "arrays and the publish installs them directly. Uniform-color\n"
        "shapes only -- per-face colors, textures and marker sets fall\n"
        "back to the traversal capture, as does any shape whose nodes\n"
        "were touched after the landing registered the content. 0 is\n"
        "off, 1 adopts, 2 adopts nothing but runs the traversal capture\n"
        "and compares it against the worker's content, logging any\n"
        "disagreement -- slow, for checking the emission, not for use."),
    ParamInt('CaptureBudgetMS',  50, title='Vertex capture budget per publish (ms)',
        doc="How long one scene publish may spend re-capturing changed\n"
        "shapes into vertex caches before the rest are deferred. The\n"
        "capture walks a changed shape's primitives one triangle at a\n"
        "time, and during a descent storm every landed batch pays that\n"
        "on the next paint: mid-paint stack samples put the capture at\n"
        "about half of 250-850ms publish frames. Once this budget is\n"
        "spent, each remaining changed shape keeps its previous vertex\n"
        "cache for this frame (a shape captured for the first time\n"
        "stays out of the frame entirely -- progressive appearance,\n"
        "same as a live import), the caches on its path are left\n"
        "unclosed for reuse, and another publish is scheduled; captured\n"
        "shapes turn valid and prune, so successive frames always make\n"
        "progress. The display is at worst a few frames stale in a\n"
        "scene that is churning anyway; a single changed object never\n"
        "comes near the budget. 0 captures everything in one frame,\n"
        "as before this parameter existed."),
    ParamInt('LevelSlowBuildMS',  200, title='Slow visual build report (ms)',
        doc="A visual rebuild whose own cost passes this many\n"
        "milliseconds reports its time split (traversal, mesh,\n"
        "prologue, instancing, highlight) on one line naming the\n"
        "object, under the level debug flag. The aggregate split says\n"
        "where a mass descent's time goes; the landing pump's worst\n"
        "turn is a single object's whole rebuild, and only a per-build\n"
        "line says what that object spent it on. The same threshold\n"
        "arms the slow-dispatch line in GUIApplication::notify, which\n"
        "names the receiver of any single event-loop dispatch this\n"
        "slow -- the net that catches a stall no timer above\n"
        "bracketed. 0 turns both lines off."),
    ParamInt('DescentOrderBatch',  64, title='Descent order batch',
        doc="How many descents (demotes/downgrades) one plan pass may\n"
        "order, free tier and priced tier together; 0 removes the cap.\n"
        "Each order enqueues a worker job -- the coarsening itself runs\n"
        "on the refine pool -- but the enqueue snapshots the object's\n"
        "display arrays on the GUI thread, so an unbounded pass (the\n"
        "measured 1500-order plans) is itself a stall. Deferred\n"
        "candidates keep their hooks and the replan after the batch\n"
        "lands re-finds them, so nothing is refused, only paced -- the\n"
        "climb admission batch's mirror."),
    ParamBool('DowngradeLedger',  True, title='Downgrade ledger',
        doc="Whether the GPU downgrade sweep carries its own unlanded\n"
        "orders as credit against the next plan's deficit\n"
        "(docs/SceneStreaming.md #13c.4). A downgrade frees exactly the\n"
        "bytes it prices, but not WHEN the plan next looks: the swap\n"
        "uploads the coarse rung immediately while the fine buffers\n"
        "leave the live meter only after the collection window -- on a\n"
        "heavy scene, seconds -- so a plan sampling mid-transition reads\n"
        "old+new at once, computes a larger deficit than the one just\n"
        "covered, and walks other sources further down. Measured on a\n"
        "5455-object model at 64MB with the camera inside the assembly:\n"
        "single plans requesting 1500+ downgrades, live tripling during\n"
        "the storm, and the whole registry drained to its bottom rung\n"
        "while the settled memory was under budget all along.\n"
        "With the ledger, promised bytes hold the sweep until they are\n"
        "observed landing or written off a few frames after the ordered\n"
        "worker jobs have all drained (an order's bytes cannot land\n"
        "before its descent job does); off restores the storming\n"
        "behaviour for comparison."),
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
    ParamFloat('LevelBudgetDeadband',  0.03, title='GPU budget deadband',
        doc="The rest band above the GPU memory budget, as a fraction of\n"
        "it, inside which the level plan orders NO downgrades. The sweep\n"
        "triggers only past budget*(1+this) and still corrects back to\n"
        "the budget itself, so the band is hysteresis, not a higher\n"
        "budget.\n"
        "Without it an equilibrium that lands ON the budget line has\n"
        "nowhere to rest: the plan orders 2-3 downgrades, the release\n"
        "staircase re-wants the quality back, and the ladder dithers\n"
        "0.2-0.4MB across the line for as long as the process lives --\n"
        "measured on the rack model as the difference between a run\n"
        "that settles in ~250s and one that churns its whole 600s\n"
        "window. Climbs already stop AT the budget (Climb hard limit),\n"
        "so inside the band neither direction acts and the plans go\n"
        "genuinely quiet; pressure counts as standing there, which\n"
        "keeps the raised tolerance and the edge gate latched exactly\n"
        "as they were while the equilibrium was reached.\n"
        "The band tolerates standing that fraction over the stated\n"
        "budget (about 2MB at 64MB). 0 restores the bare line and with\n"
        "it the dither."),
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
    ParamBool('ShapeVertices',  True, title='Draw edge-attached vertices',
        doc="Let the vertex points that sit on the ends of a shape's edges\n"
        "draw under the element contract (docs/SceneStreaming.md #13b):\n"
        "an attached point set draws only while its object's line set is\n"
        "shown and memory allows, is the FIRST class dropped under\n"
        "pressure and the LAST taken back. Off suppresses attached point\n"
        "sets outright, memory or not.\n"
        "A point is not cheap: it costs the GPU a 32-byte sprite instance\n"
        "record plus its index, roughly nine times what it occupies in\n"
        "the heap, which is why a CPU-currency measurement made them look\n"
        "negligible.\n"
        "All or nothing per point set, and only ATTACHED sets are ever\n"
        "gated: one floating vertex -- one no edge touches, and every\n"
        "point of a point cloud -- and the whole set ranks with the\n"
        "faces, because nothing else would show it. Objects are in\n"
        "practice all floating or none, so a per-vertex subset would buy\n"
        "nothing and cost an index permutation.\n"
        "It never applies in the Points display mode, where the vertices\n"
        "are what the mode exists to show.\n"
        "Picking, pre-selection and selection highlighting are unaffected:\n"
        "the point geometry stays published and resident, the highlight\n"
        "draws render on top as always, and only the base-pass submission\n"
        "is skipped."),
    ParamBool('PressureDropEdges',  True, title='Drop face edges under pressure',
        doc="Let the pressure stages of the element contract\n"
        "(docs/SceneStreaming.md #13b) stop drawing the edges that bound\n"
        "faces. Under the contract an attached line set draws only while\n"
        "its object's face set is shown and memory allows; pressure\n"
        "spends the classes points -> lines -> faces and takes them back\n"
        "in reverse, and this is the switch on the lines stage. Off\n"
        "exempts line sets from the pressure stages (a loading document\n"
        "still drops them).\n"
        "Edge geometry is the GPU's most expensive geometry per unit of\n"
        "screen information: a segment is 8 bytes of index in the heap\n"
        "and those 8 bytes plus a 64-byte quad-expansion instance record\n"
        "on the GPU.\n"
        "All or nothing per edge set, attached sets only: one floating\n"
        "edge -- a wire, a sketch, a datum line, any edge no face uses --\n"
        "and the whole set ranks with the faces, because it is the\n"
        "object, and dropping it would show nothing at all.\n"
        "It never applies in the Wireframe display mode, where the edges\n"
        "are what the mode exists to show.\n"
        "A display gate, not a residency change -- nothing is demoted and\n"
        "nothing re-tessellates, so entering and leaving it costs one\n"
        "frame, which is why it is spent before any rung is given up.\n"
        "Picking, highlighting and on-top rendering are unaffected."),
    ParamInt('ElementGateStagger',  15, title='Element gate stage frames',
        doc="How many frames the element contract's pressure latch waits\n"
        "between stages (docs/SceneStreaming.md #13b), both escalating\n"
        "(points dropped, then lines if the budget is still exceeded) and\n"
        "releasing (lines back, then points, once the ladder has given\n"
        "back all raised error). The wait is what lets the buffer\n"
        "collector's census answer whether the cheaper stage was enough\n"
        "before the next one is spent, and what keeps the release from\n"
        "re-opening into the memory the collector just freed."),
    ParamInt('TinyElementCutoff',  0, title='Tiny element draw cutoff',
        doc="MEASUREMENT INSTRUMENT, 0 = off. Suppress every line and\n"
        "point draw issuing this many primitives or fewer, regardless of\n"
        "the element contract -- floating sets included, which is the\n"
        "point: the contract deliberately never gates those, and they\n"
        "are what a far-field cut is left drawing\n"
        "(docs/FarFieldProxies.md 11.1i).\n"
        "\n"
        "It exists to price the DRAW axis, which this engine has only\n"
        "ever measured in the opposite regime. docs/DrawSubmission.md\n"
        "dismissed draw count on a frame averaging ~1540 primitives per\n"
        "draw, where the GPU is geometry-bound and a draw is free; the\n"
        "far-field residue is ~12 primitives per draw, where a draw is\n"
        "nearly all overhead. Setting this to ~24 on MiSTer removes\n"
        "about 2% of the primitives and about 44% of the draws, so any\n"
        "frame-time difference is attributable to draw count and not to\n"
        "geometry.\n"
        "\n"
        "Not a display feature: it makes real edges vanish, and picking,\n"
        "highlighting and on-top draws are exempt so the scene stays\n"
        "usable while it is on."),
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
        "RE-MEASURED 2026-08-15, and the earlier reading no longer\n"
        "holds. It used to suppress NOTHING on a .FCStd open: the load\n"
        "parked every visual build and published in one step at the\n"
        "end, so the renderer held an empty scene throughout -- 0\n"
        "drawables across 17.8s on a 5455-object model. The publish is\n"
        "incremental now, so the same open feeds the scene while the\n"
        "drain runs and the gate has real work: on the same model it\n"
        "climbs from 1123 to 5909 point and line draws suppressed, out\n"
        "of 11818 eligible in a 17727-drawable scene, and both edges\n"
        "are logged -- ON with an empty scene, OFF as the drain ends.\n"
        "It overrides both gates while it lasts -- vertices drop even\n"
        "with ShapeVertices on, edges drop with no pressure yet declared\n"
        "-- but it is subject to the same all-or-nothing classification\n"
        "and the same display-mode exemptions: a wire, a sketch, a datum\n"
        "line or a point cloud draws throughout, because nothing else on\n"
        "screen would show it, and neither class is dropped in the mode\n"
        "that exists to show it.\n"
        "Independent of this gate, the contract's dependency rule already\n"
        "holds back an attached point or line set whose companion the\n"
        "publish's capture budget deferred: an adopted vertex cache never\n"
        "draws frames ahead of the face set it decorates, load gate or\n"
        "not.\n"
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
    ParamBool('TemporalAccum',  False, title='Idle temporal accumulation',
        doc="Keep refining the image while the camera holds still.\n"
        "\n"
        "Multisampling antialiases the geometry it rasterizes and nothing\n"
        "else: every sample inside one triangle is shaded once, so a\n"
        "specular highlight crawling across a curved surface, a normal or\n"
        "texture detail below the pixel, and every screen-space pass\n"
        "computed after the resolve -- ambient occlusion, outlines,\n"
        "section caps, the light shafts -- are left exactly as aliased or\n"
        "as noisy as they were drawn. More coverage samples cannot help\n"
        "any of them.\n"
        "\n"
        "This spends time instead. Once the camera stops, each further\n"
        "frame offsets the projection by a fraction of a pixel and\n"
        "averages into what is already on screen, so the whole pipeline\n"
        "converges toward what supersampling it would have given -- and\n"
        "it costs nothing at all while anything is moving.\n"
        "\n"
        "There is no reprojection and no history rejection, because\n"
        "nothing moved: the accumulation is thrown away outright on any\n"
        "camera, scene or highlight change, so a drag or an orbit returns\n"
        "to the ordinary multisampled frame immediately with no ghosting,\n"
        "smearing or trailing on thin edges. It is a refinement on top of\n"
        "multisampling, not a replacement for it -- leave the antialiasing\n"
        "preference where it is.\n"
        "\n"
        "The cost is idle GPU time: a parked view keeps drawing until it\n"
        "has converged (TemporalAccumSamples), then stops and asks for\n"
        "nothing more. On a laptop or a tablet that is battery, which is\n"
        "why this is off by default and why it does not travel in a saved\n"
        "document."),
    ParamInt('TemporalAccumSamples',  32, title='Idle accumulation samples',
        doc="How many jittered samples the idle accumulation converges over\n"
        "before the view goes quiet (2-256, TemporalAccum only).\n"
        "\n"
        "The sequence is a Halton (2,3) pair over the pixel, so it fills\n"
        "the pixel evenly at every count rather than clumping, and it is\n"
        "indexed by sample number -- frame N of an accumulation is the\n"
        "same frame N every time, which is what keeps a rendered\n"
        "comparison reproducible.\n"
        "\n"
        "Most of the visible gain arrives in the first handful of\n"
        "samples, since the error of an average falls with the square\n"
        "root of the count: 32 halves the residual noise of 8, and 128\n"
        "halves it again for four times the work. Raise it for a still\n"
        "worth waiting on, lower it to reach the quiet state sooner."),
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
    ParamBool('OcclusionBenefitProbe',  False, title='Occlusion benefit probe',
        doc="Measure whether the culling pays for itself on THIS scene and\n"
        "camera (docs/FarFieldProxies.md 12.13): alternate stretches of\n"
        "frames with the whole occlusion block on and off, compare median\n"
        "frame cost, and print the verdict with the culling readout\n"
        "(Render_LevelDebug cadence). The probe is an intervention -- its\n"
        "off arm draws everything and pauses the hidden-streak demote\n"
        "feed for those frames -- so it is a measuring instrument, not a\n"
        "mode to leave on. The verdict gates nothing yet; it is the\n"
        "number the wire-or-delete decision for CullBenefitEstimator\n"
        "reads."),
    ParamBool('AO',  False, title='Ambient occlusion',
        doc="Enable screen space ambient occlusion of the experimental render\n"
        "engine (render cache mode 3 with a selected renderer type)."),
    ParamBool('Shadow',  True, title='Shadow',
        doc="Render the shadow map cast by the Shadow display style's scene\n"
        "light (and the god-ray shafts / caustic occlusion that depend on\n"
        "it). A convenience switch to drop shadows without leaving the\n"
        "Shadow display style; the base headlight and environment lighting\n"
        "stay, so the scene remains lit, just flatter. Has no effect unless\n"
        "the Shadow display style provides a scene light."),
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
    ParamBool('Cavity',  True, title='Cavity shading',
        doc="Enable screen space cavity (curvature) shading of the\n"
        "experimental render engine (render cache mode 3 with a selected\n"
        "renderer type). Darkens concave creases and convex ridges found\n"
        "in the geometry prepass normals, which makes surface shape and\n"
        "small features read without relying on the lighting.\n"
        "\n"
        "Best paired with the Shaded draw style, the one that draws no\n"
        "edges: there the darkened crease is the only thing stating where\n"
        "a face ends, so cavity does the job the edge lines do elsewhere,\n"
        "without the wireframe over every tessellated curve. In a style\n"
        "that already draws edges (Flat Lines) the two land on the same\n"
        "pixels and cavity mostly restates them.\n"
        "\n"
        "Independent of ambient occlusion: cavity is a local curvature\n"
        "term, occlusion is a visibility integral over a world-space\n"
        "radius (contact darkening). They compose."),
    ParamFloat('CavityRadius',  1.0, title='Cavity radius',
        doc="Baseline the cavity curvature is measured over, in pixels.\n"
        "\n"
        "This decides which features the pass can see at all. The term\n"
        "reads how far the surface normal turns between the two\n"
        "neighbours, so at the default of 1 it sees only what turns\n"
        "within a single pixel: hard creases, crisply, which is what\n"
        "stands in for the edge lines the Shaded draw style does not\n"
        "draw. Widening it brings broad curvature (fillets, blends, a\n"
        "sculpted face) in, at the cost of spreading a hard crease into a\n"
        "band of this width.\n"
        "\n"
        "Being in pixels it is resolution-relative: the same value covers\n"
        "less of the model on a high-DPI display, so a large model on a\n"
        "dense screen may want more than 1."),
    ParamFloat('CavityValley',  1.0, title='Valley darkening',
        doc="Cavity darkening strength in concave creases (inside corners,\n"
        "fillets, pockets). Zero disables the valley term."),
    ParamFloat('CavityRidge',  0.5, title='Ridge darkening',
        doc="Cavity darkening strength on convex ridges (outside corners,\n"
        "chamfers). Reads as a soft contour along edges. Zero disables the\n"
        "ridge term.\n"
        "\n"
        "Both terms darken: the pass multiplies the finished 8-bit scene\n"
        "color, which cannot brighten past white, so the ridge highlight\n"
        "some workbench renderers use is not available here."),
    ParamBool('Matcap',  False, title='Matcap shading',
        doc="Enable matcap shading of the experimental render engine\n"
        "(render cache mode 3 with a selected renderer type). Replaces\n"
        "the scene's lighting with a fixed studio attached to the camera,\n"
        "looked up by each fragment's view space normal: the shading of a\n"
        "surface then depends only on which way it faces the viewer, so\n"
        "form reads identically wherever the scene light happens to be.\n"
        "The classic inspection shading -- pair it with Cavity for edge\n"
        "definition. Overrides physically based shading while on."),
    ParamInt('MatcapPreset',  0, title='Matcap',
        proxy=ParamComboBox(items=['Studio', 'Clay', 'Metal', 'Pearl']),
        doc="Which matcap to shade with. The presets are computed in the\n"
        "shader rather than sampled from images, so they cost no assets\n"
        "and stay sharp at any resolution. Studio = soft key light with a\n"
        "rim; Clay = matte, no highlight, the most neutral read of form;\n"
        "Metal = banded sweep with a hard edge, exaggerates curvature;\n"
        "Pearl = warm/cool dual tone, shows shallow undulation."),
    ParamFloat('MatcapTint',  1.0, title='Matcap object tint',
        doc="How much each object's own color tints the matcap, 0 to 1.\n"
        "One multiplies the matcap by the object color, so the matcap\n"
        "supplies the shading and the assembly keeps its color coding.\n"
        "Zero shades the whole scene as one uniform material instead,\n"
        "which drops the color coding but makes shape directly\n"
        "comparable across parts."),
    ParamBool('PBR',  False, title='Physically based shading',
        doc="Enable physically based shading with image based lighting of\n"
        "the experimental render engine (render cache mode 3 with a\n"
        "selected renderer type). Replaces the Classic headlight shading\n"
        "of lit surfaces with a metallic/roughness material lit by a\n"
        "built-in studio environment."),
    ParamFloat('PBRMetallic',  0.0, title='Metallic',
        doc="Metalness of physically based shaded surfaces, 0 to 1."),
    ParamFloat('PBRRoughness',  0.0, title='Roughness',
        doc="Roughness of physically based shaded surfaces, 0 to 1.\n"
        "Zero means automatic (derived from each material's shininess)."),
    ParamBool('PBRFromSpecular',  True, title='Specular to metallic',
        doc="Read an ordinary Phong appearance's specular COLOUR as\n"
        "physically based material data, where nothing states a\n"
        "metalness of its own. The metallic/roughness model has no\n"
        "specular slot -- its reflectance follows from the base colour\n"
        "and the metalness -- so a classic Gold, whose gold-ness lives\n"
        "entirely in that colour, otherwise shades as yellow-brown\n"
        "plastic, and the presets built from a black diffuse and a\n"
        "bright specular (Steel, Satin, Metalized) shade as nearly\n"
        "black. Anything authored stands: a stated metalness, a PBR\n"
        "appearance, a metallic-roughness map."),
    ParamInt('ShininessMapping',  1, title='Shininess mapping',
        proxy=ParamComboBox(items=['GL exponent', 'Full range']),
        doc="How a classic Phong appearance's SHININESS becomes a\n"
        "roughness, where the material states no roughness of its own.\n"
        "\n"
        "Either way the conversion itself is the standard match of the\n"
        "GGX lobe width to a Phong exponent n, roughness =\n"
        "(2 / (n + 2)) ^ 1/4. What differs is what shininess MEANS.\n"
        "\n"
        "'GL exponent' reads it the way fixed-function GL did, as the\n"
        "exponent scaled onto 0..128. That is faithful, but 128 is the\n"
        "sharpest exponent GL could state, and it converts to a\n"
        "roughness of 0.35 -- so on this reading a fully shiny Phong\n"
        "material is satin, and the lower half of the roughness range\n"
        "cannot be reached from shininess at all.\n"
        "\n"
        "'Full range' reads shininess as what the Appearance dialog\n"
        "presents, a 0 to 100% appearance control, and maps it onto the\n"
        "whole exponent range instead: n = 128 * s / (1 - s). Matte at\n"
        "zero and a mirror at one, and over the low shininess values\n"
        "real materials use it agrees with the GL reading to within a\n"
        "few percent (FreeCAD's default 0.2 gives 0.49 rather than\n"
        "0.52, the Gold preset 0.66 rather than 0.67).\n"
        "\n"
        "Neither reading touches anything authored: a stated roughness,\n"
        "a PBR appearance, a metallic-roughness map and the per-object\n"
        "Render_Roughness override all stand.",
        ),
    ParamInt('PBREnvPreset',  1, title='Environment',
        proxy=ParamComboBox(items=['Studio', 'Gradient', 'Overcast',
                                   'Sunset', 'Interior', 'Light tent']),
        doc="Which built-in environment lights the scene, where no\n"
        "environment image is set. They are computed rather than\n"
        "sampled from a file, so they cost no assets and work on every\n"
        "tier including the browser.\n"
        "\n"
        "What separates them is contrast and structure, not brightness:\n"
        "all five integrate to the same mean radiance, so the exposure\n"
        "that suits one suits the others. That matters because a\n"
        "surround with no bright sources and no edges cannot put a\n"
        "highlight on anything that reads as a light, and a smooth\n"
        "surface reflecting it shows the same flat grey at every\n"
        "roughness -- which is what made physically based shading look\n"
        "like painted plastic.\n"
        "\n"
        "Interior = a room with one window and a ceiling\n"
        "panel, walls close enough to bounce. One hard key against a\n"
        "dark surround, which is what gives the crispest highlight and\n"
        "the strongest read of form. Studio = four soft boxes on a dark\n"
        "surround, the product-shot rig, gentler and more even than\n"
        "Interior. Gradient (the default) = the smooth three-band dome\n"
        "this engine used before the others existed; the flattest and\n"
        "the most even, which is why it is where a view starts -- it\n"
        "stays out of the way of the model being worked on, and it is\n"
        "the one to pick to have an older document's look back.\n"
        "Overcast = a bright sky weighted to the zenith over dark\n"
        "ground, soft and neutral. Sunset = a low warm sun with a deep\n"
        "sky, the strongest colour separation, and the only one that\n"
        "tints the whole frame. Light tent = a box of white panels,\n"
        "bright BELOW the horizon as well as above it and seamed all\n"
        "the way round; the one to pick when the SIDES of a subject\n"
        "matter, since every other environment here puts a floor under\n"
        "it and a standing wall reflects the floor."),
    ParamFloat('PBREnvIntensity',  1.0, title='Environment brightness',
        doc="Brightness of the image based lighting environment."),
    ParamString('PBREnvImage', '', title='Environment image',
        doc="Image file used as the image based lighting environment,\n"
        "replacing the built-in procedural studio environment. A 2:1\n"
        "image is read as equirectangular (lat-long), anything squarer\n"
        "as a sphere map — the same convention as the Texture mapping\n"
        "dialog's Environment mode, so the same file works in both.\n"
        "\n"
        "A Radiance picture (.hdr, .pic) is read as real radiance and\n"
        "is the format worth using: a sky is thousands of times\n"
        "brighter than the wall beneath it, and an ordinary 8-bit image\n"
        "cannot hold that ratio, which is what makes one light a model\n"
        "like a picture rather than like a place. An HDR environment\n"
        "needs the output colour transform on, since it is the exposure\n"
        "that decides how its range lands on the screen.\n"
        "\n"
        "What to load, in short:\n"
        "\n"
        " - A Radiance .hdr or .pic. OpenEXR is NOT read: anything\n"
        "   that is not Radiance goes through Qt, which has no EXR\n"
        "   plugin, so an .exr loads nothing and the procedural\n"
        "   environment stays on.\n"
        " - 2:1 proportions, so it is taken as a lat-long panorama\n"
        "   and not as a mirror ball. Up is +Z, and the middle of\n"
        "   the image faces +X.\n"
        " - 1K or 2K is plenty. The picture is held as 32-bit float\n"
        "   RGB (2K is about 25 MB, 8K about 400 MB) and is baked\n"
        "   into a 128 pixel per face cubemap, so a larger one\n"
        "   costs memory without showing more.\n"
        " - Free CC0 panoramas: polyhaven.com/hdris.\n"
        "\n"
        "How sharp it is DRAWN behind the model is a separate\n"
        "question, and the answer is Render_PBREnvBlur: the background\n"
        "pass draws that cubemap through a lens aperture, the way a\n"
        "real backdrop is out of focus, and at zero the aperture is\n"
        "shut and it is drawn as baked. The lighting and the\n"
        "reflections read the sharp environment whatever the blur\n"
        "says.\n"
        "\n"
        "Empty falls back to that dialog's current image, then to the\n"
        "procedural environment."),
    ParamBool('PBREnvEmbed', True, title='Embed environment image',
        doc="Store a copy of the environment image inside the document,\n"
        "so it travels with the file instead of depending on the\n"
        "original path. The copy lives in the view's\n"
        "Render_PBREnvImageData property and takes precedence over the\n"
        "image path while set.\n"
        "\n"
        "On by default: a document whose lighting depends on a file\n"
        "somewhere on one machine opens lit differently everywhere\n"
        "else, and the path is the part of the setting least likely\n"
        "to survive the trip."),
    ParamBool('PBREnvBackground', True, title='Environment background',
        doc="Show the image based lighting environment itself as the view\n"
        "background while physically based shading is active, so\n"
        "reflective surfaces visibly mirror their surroundings.\n"
        "\n"
        "On by default, because a reflective object standing in front of\n"
        "a flat gradient reads as fake for a reason that is not the\n"
        "object's fault: the reflection has no visible source, so there\n"
        "is nothing in the frame for the eye to reconcile it against.\n"
        "Affects nothing outside physically based shading -- the\n"
        "Classic and Matcap models keep the background gradient."),
    ParamFloat('PBREnvBlur',  0.25, title='Environment background blur',
        doc="How far out of focus the environment background is, 0 to 1.\n"
        "Zero is sharp -- the resolution it was baked at; one opens the\n"
        "aperture to 45 degrees, and in between it doubles every eighth\n"
        "of the range. Only the BACKGROUND is affected -- the lighting\n"
        "and the reflections read the whole environment whatever this\n"
        "says.\n"
        "\n"
        "It is a defocus, not a smudge: the environment is convolved\n"
        "with the disc of directions an aperture subtends, in linear\n"
        "radiance, so a small bright source spreads into an even bokeh\n"
        "disc that keeps its energy rather than being averaged away.\n"
        "\n"
        "A backdrop wants some of this. A real one is out of focus, and\n"
        "softening also lets a small bright source bleed into a wide\n"
        "gentle falloff instead of sitting in the frame as a hard\n"
        "rectangle. Too much of it and there is nothing left for a\n"
        "reflection to be reconciled against, which is the whole reason\n"
        "the background is drawn at all. Blender's viewport shading\n"
        "carries the same control for the same reasons, and defaults it\n"
        "higher than this does.\n"
        "\n"
        "Both shading models honour it, and at zero the two show the\n"
        "same backdrop: they bake the environment at the same angular\n"
        "resolution. The external path tracer gets there differently,\n"
        "since the world it samples IS the light and softening it\n"
        "would relight the scene -- so a second bake of the same\n"
        "environment through the same aperture is mixed in on CAMERA\n"
        "rays alone, and the lighting, reflections and refractions keep\n"
        "the sharp world. One consequence of that rule: a camera ray\n"
        "stays a camera ray through a transparent surface, so a\n"
        "see-through pass-through shows the soft backdrop as well."),
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
        "render engine: raymarch the shadow map of the Shadow display style\n"
        "through a homogeneous scattering medium. Only effective while\n"
        "the Shadow display style provides a scene light."),
    ParamFloat('VolumetricIntensity',  1.0, title='Intensity',
        doc="Brightness of the inscattered (light shaft) light."),
    ParamFloat('VolumetricDensity',  0.0, title='Medium density',
        doc="Scattering medium density in inverse world units.\n"
        "Zero means automatic (a fraction of the scene size)."),
    ParamBool('Caustics',  False, title='Water caustics',
        doc="Project an animated caustic light pattern onto surfaces\n"
        "below the water body (objects with the Render_Water property),\n"
        "modulated by the shadow map. Only effective while volumetric\n"
        "lighting and the Shadow display style are active."),
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
        "sun glint from the Shadow display style light."),
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
        "the sun glint killed there. Requires the Shadow display style\n"
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
    ParamBool('Light',  False, title='Renderer scene light',
        doc="Let the render engine supply its own directional or spot scene\n"
        "light, described by the Light* settings below, instead of taking\n"
        "one out of the Coin traversal.\n"
        "\n"
        "Everything the engine keys off a light -- shadows, volumetric\n"
        "shafts, the sun disc, ground reflection -- today has exactly one\n"
        "source: the Shadow display style, which is what puts an\n"
        "SoShadowDirectionalLight or SoSpotLight in the scene graph at all\n"
        "(the viewer headlight is a plain SoDirectionalLight, which the\n"
        "engine rejects by type). That makes a draw style the owner of the\n"
        "lighting, and it is why the style cannot simply be retired\n"
        "(docs/CoinRetirement.md 3.4).\n"
        "\n"
        "Off by default, and while off nothing changes. A light found in\n"
        "the traversal still wins when one is there, so the Shadow style\n"
        "keeps behaving exactly as before; these settings supply a light\n"
        "when it does not."),
    ParamFloat('LightIntensity',  0.8, title='Light intensity',
        doc="Brightness of the renderer's own scene light."),
    ParamFloat('LightDirectionX',  -1.0),
    ParamFloat('LightDirectionY',  -1.0),
    ParamFloat('LightDirectionZ',  -1.0),
    ParamHex('LightColor',  0xf0fdffff, title='Light color', proxy=ParamColor(),
        doc="Colour of the renderer's own scene light."),
    ParamBool('LightSpot',  False, title='Use spot light',
        doc="Make the renderer's own light a spot rather than a directional\n"
        "one. A spot has a position and a cone; a directional light has\n"
        "only a direction."),
    ParamFloat('LightPositionX',  0.0),
    ParamFloat('LightPositionY',  0.0),
    ParamFloat('LightPositionZ',  0.0),
    ParamFloat('LightCutOffAngle',  45.0, title='Spot cut-off angle',
        doc="Half angle of the spot cone, in degrees."),
    ParamFloat('LightDropOffRate',  0.0, title='Spot drop-off rate',
        doc="How sharply a spot falls off from the cone axis. Zero is even\n"
        "across the cone."),
    ParamBool('SunDisc',  False, title='Sun disc',
        doc="Draw a visible sun -- a bright disc with a limb glow -- in\n"
        "the sky along the Shadow display style's directional scene light,\n"
        "occluded by geometry and feeding the bloom glow. Perspective\n"
        "cameras only; spot lights have no sky direction."),
    ParamFloat('SunDiscSize',  1.5, title='Sun disc size',
        doc="Angular radius of the sun disc in degrees (the real sun is\n"
        "about 0.27; larger reads better in a CAD scene)."),
    ParamBool('GroundReflection',  False, title='Ground reflection',
        doc="Mirror the model in the ground plane of the experimental\n"
        "render engine: the opaque scene is re-rendered with a reflected\n"
        "camera and blended onto the ground. Brings the ground plane out\n"
        "on its own -- neither the Shadow display style nor its ground\n"
        "switch is needed -- and the ground keeps its own appearance\n"
        "settings (color, size, texture) from the shadow group."),
    ParamFloat('GroundReflectionIntensity',  0.4, title='Reflection intensity',
        doc="Blend factor of the mirrored model on the ground plane."),
    ParamString('CyclesDevice', 'CPU', title='Cycles device',
        doc="Compute device type the External shading model path traces\n"
        "on, as Gui.cyclesDevices() names them: 'CPU' always works, and\n"
        "'CUDA', 'OPTIX' or 'HIP' when this machine has the GPU and the\n"
        "driver for it. Seeds the per-view Cycles_Device property, which\n"
        "offers only the devices the machine actually has -- a document\n"
        "saved elsewhere falls back to the first local device when its\n"
        "choice does not exist here."),
    ParamInt('CyclesSamples',  256, title='Cycles samples',
        doc="Samples per pixel the External shading model refines to\n"
        "before it rests. More is cleaner and slower to settle; the view\n"
        "stays interactive either way, restarting from one sample on\n"
        "every camera move."),
    ParamFloat('CyclesTimeLimit',  0.0, title='Cycles time limit',
        doc="Seconds the External shading model may refine after each\n"
        "change before it rests, whatever the sample budget still says.\n"
        "0 means no limit: the sample count alone decides."),
    ParamBool('CyclesDenoise',  True, title='Cycles denoise',
        doc="Run OpenImageDenoise over the refining External shading\n"
        "frame, trading the raw noise of the early samples for a smooth\n"
        "image that sharpens as samples arrive."),
    ParamInt('CyclesPixelSize',  1, title='Cycles pixel size',
        doc="Render the External shading model at 1/n resolution and\n"
        "scale up -- Blender's preview pixel size. 2 or 4 keeps a large\n"
        "view fluid on a weak device at the cost of a blockier preview."),
    ParamInt('CyclesMaxStreams',  4, title='Cycles served sessions',
        doc="How many path-traced sessions this process serves at once\n"
        "(docs/CyclesIntegration.md sec 7.1). A browser viewer that asks\n"
        "for a path-traced view gets a Cycles session of its own -- one\n"
        "per traced cell, per connection, across every served document --\n"
        "and each holds a device context and the scene on that device.\n"
        "A start made when this many are already running is refused with\n"
        "'TooManyStreams'; the viewer says so and stays on its raster\n"
        "view. 0 or less means no cap, which is what the desktop views\n"
        "and the offline render have always had: this counts served\n"
        "streams only."),
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
