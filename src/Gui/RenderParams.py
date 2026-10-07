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
    # The long form, kept here; the documentation shown is the short one below.
    # Whether the engine is colour managed.
    #
    # The shading is linear -- mixes, the GGX lobe, the image based
    # lighting product are all arithmetic on light, and they are only
    # correct on linear numbers. A colour someone picked is not: it
    # is a display number, which makes it sRGB encoded. And a display
    # reads the byte it is handed as sRGB too.
    #
    # 'sRGB' honours both ends. Authored colours -- materials, the
    # lights, the background, the base colour and emissive textures --
    # are decoded to linear as they enter, and the finished frame is
    # encoded once at the last write before it is shown. An UNSHADED
    # authored colour therefore survives the round trip exactly, and
    # so does a fully lit surface; what changes is the shading in
    # between, which is the part that was wrong.
    #
    # 'Off' is the older pipeline, which did neither: it fed display
    # numbers to the linear shading and wrote the linear result out
    # raw. The two errors partly cancel -- a fully lit surface comes
    # out right -- but everything in falloff and shadow renders about
    # a gamma too dark. Documents written before this existed are
    # drawn that way, which is how they were authored.
    ParamInt('OutputTransform',  1, title='Output colour transform',
        proxy=ParamComboBox(items=['Off', 'sRGB']),
        doc="Colour management of the engine. 'sRGB' decodes authored colours to\n"
            "linear for shading and encodes the finished frame for the display,\n"
            "which is the correct pipeline. 'Off' is the older one, which shades\n"
            "display numbers and renders falloff and shadow too dark; documents\n"
            "written before this setting existed use it.",
        ),
    # The long form, kept here; the documentation shown is the short one below.
    # How much light the frame is developed with, as a plain
    # multiplier on the linear image before it is encoded for the
    # screen. One leaves it alone.
    #
    # It exists because a colour managed scene is lit in real
    # reflectances, and a mid grey reflects about 18 per cent of what
    # falls on it rather than the 45 per cent its number reads as. A
    # scene whose lights were set before that was true is lit about
    # two to three times too dimly, and this is the control that
    # answers it without touching a single light.
    #
    # Raising it does not clip. Anything the multiplier pushes past
    # the top of the range rolls off smoothly instead, and the roll
    # off is exactly nothing below the knee -- so at an exposure of
    # one the frame is bit for bit what it would have been without
    # this stage at all.
    #
    # Only meaningful while the output colour transform is on: with
    # it off the engine is not working in light, and a multiplier
    # there would scale display numbers rather than exposure.
    ParamFloat('Exposure',  1.0, title='Exposure',
        doc="Brightness multiplier applied to the finished image before it is\n"
            "encoded for the screen. 1 leaves it alone. Bright areas roll off\n"
            "smoothly instead of clipping. Only used while the output colour\n"
            "transform is on.",
        ),
    # The long form, kept here; the documentation shown is the short one below.
    # How many backend view ids the render engine may hand out, which
    # is what decides how many 3D views can draw on it at once: each
    # view takes a block for its pass sequence (about 13 ids for a
    # plain viewer, docs/RenderEngine.md #3.1), and a view that finds
    # no block left falls back to plain GL rather than failing. So the
    # default is roughly 64 viewers, and 0 asks for the build's own
    # ceiling instead, which is four times that.
    #
    # It is worth having a limit below the ceiling because the backend
    # copies its whole view table once a frame and sizes its per-view
    # pools from this number, so ids nobody opens are still paid for
    # in every frame. Measured on a desktop GPU that cost is invisible
    # against a 16ms frame at this width - but at the ceiling, with
    # render stage timing on, it is not: the per-view GPU timer pools
    # take a 59fps session to 19. Raise it for many-viewer work, not
    # as a matter of course.
    #
    # Read once, when the backend starts: a change needs a restart.
    ParamInt('MaxViewIds',  1024, title='Backend view id budget',
        doc="How many view ids the render backend may hand out, which limits how\n"
            "many 3D views can draw with it at once (about 13 ids per view; a view\n"
            "that finds none left falls back to plain GL). 0 asks for the build's\n"
            "maximum. Read when the backend starts: a change needs a restart."),
    # The long form, kept here; the documentation shown is the short one below.
    # How a frame reaches the screen on a backend that gets there by
    # reading the frame back (Direct3D, Vulkan, Metal; not OpenGL).
    # 'Wait' holds each frame until its own copy has arrived, so the
    # screen always shows what was just drawn. 'Pipelined' shows the
    # newest copy that has arrived, a frame or two old, and saves the
    # wait; one waiting frame follows when the redraws stop. 'Pipelined
    # while animating' waits except while the view redraws by itself:
    # a camera animation, a spin, animated content.
    ParamInt('ReadbackFrameMode',  1, title='Frame delivery through read-back',
        proxy=ParamComboBox(items=['Wait', 'Pipelined while animating',
                                   'Pipelined']),
        doc="How a frame reaches the screen on Direct3D, Vulkan and Metal. 'Wait'\n"
            "shows every frame as soon as it is drawn. 'Pipelined' shows it a frame\n"
            "or two late and is faster. 'Pipelined while animating' waits except\n"
            "while the view redraws by itself. Not used with OpenGL."),
    # The long form, kept here; the documentation shown is the short one below.
    # Milliseconds a 3D view may sit in the background before it gives
    # its render targets back, or 0 to let a hidden view keep them.
    #
    # Targets are what a view mostly costs: 287MB was measured for one
    # 1644x653 view with every effect on, and until now it held them
    # whether or not anyone could see it -- so a session with several
    # documents open paid for all of their views to look at one. This
    # gives that back for the views nobody is looking at. What the view
    # keeps is everything a resize keeps: its programs, its uniforms
    # and its uploaded scene, so coming back is the resize path and not
    # a reload.
    #
    # The delay is what stops it firing on a click through the tabs.
    # Coming back costs the one frame that rebuilds the targets (~68ms
    # on the view measured above) and gives a byte-identical picture --
    # the trade is a hitch on return against the memory in between,
    # never a difference in the image. Lower it to release sooner on a
    # machine short of VRAM; raise it if switching back and forth
    # hitches.
    ParamInt('BackgroundReleaseDelay',  1000,
        title='Background view release delay',
        doc="Milliseconds a 3D view may stay in the background before it gives its\n"
            "render targets back to free GPU memory. Returning to the view costs\n"
            "one frame to rebuild them, with the same picture. 0 never releases."),
    # The long form, kept here; the documentation shown is the short one below.
    # Ladder level shapes are tessellated at under coarse-first
    # (docs/SceneStreaming.md #7): the display mesh is built at this
    # rung of the fidelity ladder and the exact tessellation is
    # declared unbuilt, generated on demand when a camera asks for it.
    # 0 is the coarsest rung, each level halves the error; -1 always
    # tessellates exact up front (pre-ladder behavior).
    #
    # Consulted whenever something can deliver the exact rung on
    # demand, which is a scene stream server (its viewers ask) OR a
    # desktop view in render cache mode 3 whose backend drives mesh
    # levels (its own level plan asks when the camera settles) - see
    # PartGui::coarseTessellationLevel. Plain Coin display has neither
    # and keeps the exact tessellation, because a coarse build there
    # would stay coarse forever. This is not a serving-only feature,
    # and it does engage for geometry built while a document loads.
    #
    # What a serving process lacks is not this setting but the local
    # level plan, which is disabled there - its rungs refine only where
    # a connected viewer's camera asks, so with no viewer attached they
    # stay coarse, while a desktop view refines its own. That, not the
    # setting, is why the two publish different geometry for one
    # document (measured on a 40-object scene: 8310 vertices serving,
    # 25595 on the desktop). Desktop refinement is tolerance-limited,
    # so what it settles at is a property of the framing.
    #
    # A level whose rung is already finer than a shape's exact
    # tessellation coarsens nothing, so on small shapes the low levels
    # do nothing visible - the default 2 is a no-op on a scene of small
    # ellipsoids that level 0 visibly coarsens.
    #
    # The FC_COARSE_TESSELLATION environment variable overrides this for
    # a whole process and returns before the gate is evaluated, so it
    # forces coarse-first on where the gate would have refused. Takes
    # effect when a shape (re)tessellates.
    ParamInt('CoarseTessellation',  2, title='Coarse tessellation level',
        doc="Ladder level a shape is first tessellated at: 0 is the coarsest, each\n"
            "level halves the error, and the exact mesh is built on demand when a\n"
            "camera needs it. -1 always tessellates exact up front. Used by a view\n"
            "in render cache mode 3 and by a scene stream server; takes effect when\n"
            "a shape is tessellated again."),
    ParamInt('CoarseDeferFaces',  1000, title='Coarse defer face threshold',
        doc="During a progressive import on the bgfx renderer, a shape with\n"
        "more faces than this gets a bounding-box stand-in immediately and\n"
        "even its coarse tessellation is built on the refine worker pool,\n"
        "swapped in when it arrives (docs/SceneStreaming.md #13) - the\n"
        "import stall otherwise scales with the largest single part. -1\n"
        "disables the stand-in so every shape tessellates inline."),
    # The long form, kept here; the documentation shown is the short one below.
    # Tessellate a restored document's parked shapes on worker
    # threads, before the drain that displays them builds any of them
    # (docs/DocumentLoad.md sec 18).
    # A load's visual build is mostly tessellation -- measured on a
    # 17058-solid assembly, 16.0s of the drain's 25.3s -- and it runs
    # one shape at a time on the GUI thread, which is where the display
    # nodes are. OCCT's own parallelism does not answer that: BRepMesh
    # splits ONE shape over its faces, and a model made of thousands of
    # small parts gives it nothing to split. Measured over such a build
    # the process held 2.04 cores of 28 -- and turning that parallelism
    # off costs 4.8s of a 22s mesh term, so it does help, it just
    # cannot scale.
    # Meshing DIFFERENT shapes at once scales: 7171 shapes took 3.8s of
    # wall time against 16.0s serial, the drain's build fell from 25.3s
    # to 13.5s, and the settled frame arrived about 12s sooner -- with
    # the frame pixel-identical and every triangle count unchanged.
    # Only shapes whose ask can be reproduced exactly are pre-meshed.
    # The claim carries the GEOMETRY bounding box the ask derives from,
    # because BRepBndLib prefers a resident triangulation and enlarges
    # the box by its deflection -- measuring again after the pre-mesh
    # would ask for something coarser than what is resident, and a
    # finer resident mesh is refused by default, so the call would
    # re-tessellate exactly what was just built. Roots sharing a face
    # or an edge with another root, instancing candidates and the
    # oversized shapes that take a stand-in are left alone, and a build
    # whose shape is still being meshed parks itself rather than read
    # a triangulation mid-write.
    #
    ParamBool('PreMeshOnLoad',  True, title='Pre-mesh a restored document in parallel',
        doc="Tessellate the shapes of a document being opened on worker threads,\n"
            "before they are built for display one by one on the GUI thread. Opens\n"
            "a document of many small parts sooner, with the same result. Shapes\n"
            "that share faces or edges with another, and instancing candidates, are\n"
            "left to the normal path."),

    # The long form, kept here; the documentation shown is the short one below.
    # Ask the shape whether it is already tessellated the way this
    # rebuild wants it, and skip the tessellation call outright when it
    # is (docs/SceneStreaming.md #13e).
    # A visual rebuild always called BRepMesh_IncrementalMesh, on the
    # assumption that a mesh already resident makes the call nearly
    # free. Measured, it does not: half the calls of a mass descent --
    # 2462 of 4942 -- changed no triangle at all and still cost about
    # 19ms each, 27% of the whole descent's rebuild time, because
    # reaching the conclusion means building OCCT's internal mesh model
    # of the shape first.
    # The check asks the same question that model would have answered,
    # off the triangulations already hanging on the faces: OCCT's own
    # consistency rule (BRepMesh_ModelPreProcessor), per face, plus the
    # 3D polygon of every free edge. It is all-or-nothing per shape and
    # deliberately the stricter test -- one face that would be
    # re-tessellated, one triangulation with an index out of range, and
    # the call runs exactly as before, because the fallback is the real
    # thing and there is nothing to gain by guessing.
    # A resident mesh FINER than the ask is not adequate. That is not
    # an oversight: the descent asks for a coarser mesh on purpose, to
    # give memory back, and OCCT would coarsen it. Skipping there would
    # quietly hold the memory the plan asked for.
    # Off, the call is made unconditionally, as it always was. With the
    # level plan narrating, the off arm also reports how often the
    # check and the call agreed, which is what says the check is safe.
    ParamBool('MeshSkipRedundant',  True, title='Skip redundant tessellation',
        doc="Check whether a shape is already tessellated the way a rebuild wants\n"
            "it, and skip the tessellation call when it is. The check is strict: a\n"
            "single face that would be re-tessellated and the call runs as before.\n"
            "Off makes the call every time."),
    # The long form, kept here; the documentation shown is the short one below.
    # Count a resident mesh FINER than the rebuild asked for as
    # adequate, instead of re-tessellating to coarsen it
    # (docs/SceneStreaming.md #13e). Only consulted when redundant
    # tessellation is being skipped at all.
    # Strictly, finer is not adequate: the descent asks coarse on
    # purpose to hand memory back, and OCCT coarsens the mesh when
    # asked with quality decrease allowed. That is why the check
    # refuses it by default -- accepting it would be the feature
    # quietly holding the memory the level plan asked for.
    # Measured on the descent, though, that is what the refusal is
    # actually costing and it is nearly all of it: 2599 of the 2765
    # refused calls had a resident mesh exactly twice as fine as the
    # ask -- the previous ladder rung, one dynamic scale step back --
    # and every one of them changed no triangle when the call was
    # made anyway. The faces were already at their floor; a face of
    # two triangles does not coarsen.
    # So this trades a coarsening that mostly achieves nothing for the
    # ~19ms it costs to find that out. What it risks is the minority
    # where the coarsening WOULD have removed triangles, which is
    # memory the plan then has to recover some other way -- through
    # the refine pool's own coarser rung, where it was always meant to
    # come from.
    # OFF BY DEFAULT, and the reason is that risk, measured. Audited
    # with every call still made so the check can be scored against
    # what the call actually did, this rule predicted 3381 calls
    # redundant and 753 of them -- 22%, better than one in five --
    # rebuilt anyway. Those are real coarsenings it would have
    # skipped, and real memory the plan would not get back. The
    # strict rule's own score on the same instrument is 1 in 7403.
    # /!\ Never read that count from a run with the skip ON: a call
    # that is skipped is never made, so nothing can say whether it
    # would have rebuilt, and the wrong-verdict column can only
    # count calls the check refused. A zero there is guaranteed by
    # construction rather than earned.
    ParamBool('MeshSkipFinerResident',  False, title='Skip when the mesh is finer than asked',
        doc="With redundant tessellation skipped, also count a mesh finer than the\n"
            "one asked for as good enough, instead of re-tessellating to coarsen it.\n"
            "Saves time on a descent, but can keep memory the level plan asked to\n"
            "have back, which is why it is off by default."),
    # The long form, kept here; the documentation shown is the short one below.
    # Skip a tessellation call on a shape whose mesh provably cannot
    # depend on the deflection asked: every face planar, every edge
    # curve a straight line (docs/SceneStreaming.md #13e). A plane
    # deviates from its triangulation by zero and a straight edge
    # discretizes to its two endpoints at ANY deflection, so the call
    # would rebuild the identical mesh -- there is no ask, coarser or
    # finer, at which such a shape tessellates differently.
    # This is the geometric statement behind the measured descent
    # waste: most mechanical parts hit their floor immediately, and a
    # mass descent then pays ~19-38ms per object per step (56-60% of
    # all drop-phase mesh time on the rack model) for BRepMesh to
    # rebuild what cannot change. The empirical exhaustion proof the
    # ladder keeps (scaleSpent) cannot be used for a skip -- audited
    # twice, 14-20% of proved shapes resume coarsening at some later
    # ask, and those rebuilds reclaim real memory. The geometric rule
    # is immune to that leak: the shapes that resume are exactly the
    # curved ones it refuses to claim, and an all-linear mesh cannot
    # shrink, so no reclaim is ever forgone.
    # The classification walks surface and curve TYPES once per shape
    # and is cached; conservative on both counts (a trimmed or offset
    # plane, a straight b-spline, count as curved). The skip is also
    # refused while any face is missing its triangulation -- building
    # that is exactly the call's job.
    # With the level plan narrating and this OFF, the rule is still
    # evaluated and scored against every call it would have skipped --
    # read its WRONG column from that arm only; a run with the skip on
    # cannot score calls it never made.
    ParamBool('MeshSkipInvariant',  True, title='Skip deflection-invariant tessellation',
        doc="Skip a tessellation call on a shape whose mesh cannot depend on the\n"
            "deflection asked for: every face planar and every edge a straight\n"
            "line. Such a shape gives the same mesh at any coarseness, so the call\n"
            "would only rebuild what is there."),
    # The long form, kept here; the documentation shown is the short one below.
    # Build the visual representation of a restored document after
    # the load instead of inline inside it. Opening a large document
    # otherwise tessellates every shape on the main thread while
    # nothing paints - the visual build is the largest single stage of
    # a load. Deferred, the window comes up first and the parts appear
    # in bounded slices with the view painting between them. Read as
    # each restored shape asks for its visual.
    ParamBool('ProgressiveLoad', True, title='Progressive document load',
        doc="Build the display of a document after it has opened instead of during\n"
            "the load. The window comes up first and the parts appear in slices,\n"
            "with the view painting in between."),
    # The long form, kept here; the documentation shown is the short one below.
    # How long one slice of deferred visual building may run before
    # returning to the event loop, when Progressive document load is
    # on. Larger finishes the document sooner, smaller keeps the window
    # more responsive while it fills in. Each slice is paid for with a
    # repaint of a large scene, which is why slices this long are worth
    # it - much smaller and the fill is paced by redraws rather than by
    # the building. Read at each slice.
    ParamInt('ProgressiveLoadBudgetMS',  100, title='Progressive load slice (ms)',
        doc="With ProgressiveLoad: milliseconds one slice of building the display\n"
            "may run before the window is updated. Larger finishes sooner, smaller\n"
            "keeps the window more responsive."),
    ParamInt('LevelThreads',  0, title='Level build threads',
        doc="How many mesh level builds (the scene server's on-demand\n"
        "re-tessellations, docs/SceneStreaming.md #7) may run at once.\n"
        "0 sizes the pool automatically - modest, because each BRepMesh\n"
        "build already parallelizes internally over OCCT's shared thread\n"
        "pool. The FC_LEVEL_THREADS environment variable overrides it.\n"
        "Read when the server spawns its first level worker."),
    # The long form, kept here; the documentation shown is the short one below.
    # Available system memory below which an exact re-tessellation
    # will not start (docs/SceneStreaming.md #13): the desktop refine
    # worker checks the system's own estimate of allocatable memory
    # before each exact build, and dropping under this floor counts as
    # a memory-ceiling observation - the same as a caught allocation
    # failure - after which the level plans also demote exact meshes
    # the camera would not miss back to their resident coarse rung.
    # 0 sizes the floor automatically (at least 512 MB, or 1/16 of
    # physical memory if that is more). Read when the first refine is
    # queued.
    ParamInt('LevelMemoryFloorMB',  0, title='Level memory floor (MB)',
        doc="Free system memory, in megabytes, below which no exact\n"
            "re-tessellation is started; from then on exact meshes the camera does\n"
            "not need are given up as well. 0 chooses automatically (512 MB, or a\n"
            "sixteenth of physical memory if that is more)."),
    # The long form, kept here; the documentation shown is the short one below.
    # Narrate what each mesh-level plan decides (docs/SceneStreaming.md
    # #13): the GPU budget it decided against and the bytes in use, how
    # many displayed sources stand at their coarse and exact rungs, and
    # how many refines, demotes and downgrades the plan asked for.
    # Reported on the plan's own cadence - a camera pause - because it
    # is a decision, not a per-frame cost.
    # Needed to tell a ladder that will not descend apart from one that
    # never ran: on the desktop OpenGL backend the automatic GPU budget
    # is 0 (bgfx's GL renderer reports no limit), so the downgrade half
    # of the plan never executed at all and nothing said so.
    # The FC_LEVEL_DEBUG environment variable also turns it on. Read
    # once, at the first plan.
    ParamBool('LevelDebug',  False, title='Level plan debug',
        doc="Diagnostic. Logs what each mesh level plan decides: the GPU budget and\n"
            "the memory in use, how many objects are coarse or exact, and how many\n"
            "refines and downgrades it ordered. The FC_LEVEL_DEBUG environment\n"
            "variable turns it on too. Read at the first plan."),
    # The long form, kept here; the documentation shown is the short one below.
    # Pretend the system ran out of memory for exact re-tessellation
    # (docs/SceneStreaming.md #13), so the CPU-side half of the level
    # plan can be exercised on a machine that has memory to spare.
    # Non-zero raises the floor that the refine worker compares
    # available memory against, so builds are refused and a memory
    # ceiling is observed - after which the plans start demoting exact
    # meshes the camera would not miss back to their coarse rung.
    # A simulation knob, not a tuning one: LevelMemoryFloorMB is the
    # real floor, and this overrides it upward only.
    # Read when a refine is dequeued, so it takes effect live.
    ParamInt('LevelCeilingSimulateMB',  0, title='Simulate memory ceiling below (MB)',
        doc="Testing aid. Pretend the system has less free memory than this many\n"
            "megabytes, so the low-memory behaviour of the level plan can be tried\n"
            "on a machine with memory to spare. 0 turns it off."),
    # The long form, kept here; the documentation shown is the short one below.
    # GPU geometry budget of the desktop mesh-level plan
    # (docs/SceneStreaming.md #13): while the uploaded geometry exceeds
    # it, a camera pause downgrades the *displayed* mesh of objects the
    # camera would not miss - off screen, or coarse within half the
    # Level tolerance - back to their coarse rung. Their exact meshes
    # stay in CPU RAM, so zooming back in re-activates them instantly,
    # with no re-tessellation. 0 means automatic: the graphics API's
    # own reported GPU memory limit where it states one (Direct3D and
    # Vulkan do; OpenGL reports nothing, and then no budget applies).
    ParamInt('GpuMemoryBudgetMB',  0, title='GPU memory budget (MB)',
        doc="GPU memory, in megabytes, the displayed geometry may use. Over it,\n"
            "objects the camera would not miss are shown with their coarse mesh;\n"
            "the exact one stays in main memory and returns at once on zooming in.\n"
            "0 uses the limit the graphics API reports (OpenGL reports none, and\n"
            "then no budget applies)."),
    # The long form, kept here; the documentation shown is the short one below.
    # Screen-space error, in pixels, a coarse tessellation may
    # commit before the exact one is built (docs/SceneStreaming.md
    # #13): on a coarse-first desktop view (render cache mode 3 with
    # a backend that drives the level plan), a camera pause re-plans
    # the scene and only objects whose coarse mesh errs by more than
    # this many pixels on screen re-tessellate exactly - off-screen
    # and distant objects stay at the cheap coarse mesh until the
    # camera makes them matter. 0 or less refines everything
    # immediately; larger keeps more of the scene coarse. The
    # streamed viewer's own tolerance is its lodpx URL parameter
    # (same meaning, same default).
    ParamFloat('LevelTolerance',  2.0, title='Level tolerance',
        doc="Error in pixels on screen a coarse mesh may show before the exact one\n"
            "is built. When the camera stops, only objects that exceed it are\n"
            "refined. 0 or less refines everything at once; larger keeps more of\n"
            "the scene coarse."),
    # The long form, kept here; the documentation shown is the short one below.
    # How much of the raised refine tolerance the plan keeps each
    # time it comes in under the GPU budget (docs/SceneStreaming.md
    # #13c.3). While the budget is exceeded the plan accepts visible
    # error to fit the scene, and it must not hand that error straight
    # back the moment one plan fits: measured on a 5455-object model at
    # a 64MB budget, clearing it in one step took the tolerance from
    # 51 pixels to 2, asked 946 objects to re-tessellate at once, broke
    # the budget again and cycled -- 43 plans in 611 seconds with no
    # steady state at any point.
    # So quality comes back in steps: each plan that fits keeps this
    # fraction of the standing tolerance, and a step that puts the
    # scene back over budget is remembered as a floor the release never
    # passes again, so the ladder settles at the coarsest tolerance
    # that actually fits instead of oscillating around it. The floor is
    # forgotten when the camera moves or the budget changes, which is
    # when what a rung costs on screen changes.
    # Smaller gives quality back faster and risks the cycle; larger is
    # gentler and slower. 0 or less restores the immediate snap.
    ParamFloat('LevelPressureRelease',  0.5, title='Level pressure release',
        doc="After the scene has been coarsened to fit the GPU budget: the fraction\n"
            "of the raised tolerance kept each time a plan fits again, so quality\n"
            "returns in steps instead of all at once and over the budget again.\n"
            "Smaller returns quality faster. 0 or less returns it in one step."),
    # The long form, kept here; the documentation shown is the short one below.
    # Whether the GPU budget is an absolute ceiling for the level
    # plan's climbs (docs/SceneStreaming.md #13c.5). With it on, a
    # plan whose allocator-exact uploaded total stands at or above
    # the budget admits NO refine and cancels every climb still in
    # flight -- the existing de-want pass aborts them -- and below
    # the ceiling climbs are admitted in small batches (Climb
    # admission batch) so the total approaches the ceiling in
    # verified steps instead of overshooting it in one plan. Judged
    # against the uploaded TOTAL, not the two-frame live census: the
    # census alternates under churn and is what let climbs land
    # over budget. A crossing is bounded by one batch's bytes;
    # per-climb pre-sizing needs rung-keyed GPU cache entries and is
    # future work. Off restores unadmitted climbing.
    ParamBool('ClimbHardLimit',  True, title='Hard GPU budget for climbs',
        doc="Treat the GPU memory budget as a hard ceiling for refinement: at or\n"
            "above it no object is refined and refinements under way are\n"
            "cancelled; below it they are admitted in small batches\n"
            "(ClimbAdmitBatch). Off refines without this check."),
    ParamInt('ClimbAdmitBatch',  64, title='Climb admission batch',
        doc="How many refines one plan may admit while the hard climb\n"
        "limit is on and the uploaded total is under budget. Small\n"
        "keeps the possible overshoot small and lets the next plan\n"
        "re-check the allocator-exact total before admitting more;\n"
        "large climbs faster. The set is not ordered by need within a\n"
        "plan, but every plan re-evaluates the whole scene, so nothing\n"
        "starves across plans."),
    # The long form, kept here; the documentation shown is the short one below.
    # How long one event-loop turn may spend landing finished
    # worker jobs (climb refines and descent coarsenings alike).
    # Landings arrive as queued events, and Qt delivers every
    # pending one in a single sweep -- a batch of 64 landings ran
    # back-to-back for measured 1-2.7s stretches in which no paint,
    # timer or input event was served. The pump runs landings until
    # this budget is spent, then yields the loop and reschedules;
    # a single landing larger than the budget still lands whole
    # (items are not sliceable). Small keeps the UI responsive
    # under a landing storm; large lands a converging scene sooner.
    ParamInt('LevelLandBudgetMS',  50, title='Level landing budget (ms)',
        doc="Milliseconds one turn of the event loop may spend installing finished\n"
            "mesh level changes before it returns to painting and input. Smaller\n"
            "keeps the window more responsive, larger finishes sooner."),
    # The long form, kept here; the documentation shown is the short one below.
    # Whether the rebuild half of a landing skips its OCCT mesh
    # call. A worker landing (climb, scale-descent, stand-in
    # resolution) or a demote/downgrade installs or re-activates the
    # very triangulation the following rebuild displays, and on
    # every such path the resident rung is never coarser than the
    # ask -- BRepMesh there can only validate: measured 18.3s of a
    # 92s budget drop (991 validated-only calls, 0.1-0.8s each on
    # large compounds), plus ~1s per landing of a giant re-FAILING
    # the faces the worker's mesher had already failed. Keyed on
    # the path of the one rebuild the landing just prepared, never
    # on the shape's descent history (the exhaustion-proof leak
    # that killed the spent-keyed skip does not reach a per-rebuild
    # claim). Audited at 94 percent exact no-ops; the rest are
    # BRepMesh re-meshing a few faces within ~5 percent of the
    # triangle count in either direction -- perturbation of a rung
    # the ladder chose to display, not reclaim forgone. The level
    # debug flag scores the claim either way; read the 'landed
    # rule' audit line before trusting a change here.
    ParamBool('MeshSkipLanded',  True, title='Skip mesh call on landing rebuilds',
        doc="Skip the tessellation call in the rebuild that follows a mesh level\n"
            "change, where the mesh just installed is the one to display and the\n"
            "call could only confirm it."),
    # The long form, kept here; the documentation shown is the short one below.
    # Whether the display-array fill of a big landing rebuild runs
    # on the refine worker pool instead of the GUI thread. After the
    # mesh call was skipped on landings (Skip mesh call on landing
    # rebuilds), the traversal that copies the resident
    # triangulations into the Coin arrays became the per-item floor
    # of the landing pump: 0.3-0.65s per 15-21k-face compound,
    # unsliceable, against a 200ms interactivity gate. With this on,
    # the rebuild captures handles to the resident triangulations
    # and edge polygons (the only state another thread may swap
    # under it -- the topology itself is immutable at runtime),
    # fills detached arrays on a worker, and lands them back through
    # the landing pump as plain array writes. The landing is
    # guarded by the shape identity and a per-object generation
    # count, so a rebuild that ran for any other reason in between
    # simply wins. Only rebuilds inside the landing pump with at
    # least 'Minimum faces for a pooled fill' faces take this path;
    # everything else fills inline exactly as before.
    ParamBool('VisualFillOnPool',  True, title='Fill landing rebuilds on the refine pool',
        doc="Fill the display arrays of a large object on a worker thread instead\n"
            "of the GUI thread, after a mesh level change. Keeps the window\n"
            "responsive while big objects change level. Applies to objects with at\n"
            "least VisualFillMinFaces faces."),
    ParamInt('VisualFillMinFaces',  2000, title='Minimum faces for a pooled fill',
        doc="How many faces a landing rebuild must have before its\n"
        "array fill goes to the refine pool (Fill landing rebuilds on\n"
        "the refine pool). The fill measures ~30us per face on the\n"
        "reference model, so the default parks roughly the >60ms\n"
        "items; the thousands of small landings in a budget drop stay\n"
        "on the cheap inline path rather than paying a snapshot, a\n"
        "queue hop and a second landing each."),
    # The long form, kept here; the documentation shown is the short one below.
    # Whether a scene publish adopts the vertex-cache content the
    # fill worker emitted at landing instead of re-capturing the
    # shape by traversal (docs/WorkerVertexCache.md). The capture
    # walks every triangle through a hash-dedup a second time to
    # rebuild exactly the arrays the fill already computed; with
    # this on, the worker emits those arrays next to the display
    # arrays and the publish installs them directly. Uniform-color
    # shapes only -- per-face colors, textures and marker sets fall
    # back to the traversal capture, as does any shape whose nodes
    # were touched after the landing registered the content. 0 is
    # off, 1 adopts, 2 adopts nothing but runs the traversal capture
    # and compares it against the worker's content, logging any
    # disagreement -- slow, for checking the emission, not for use.
    ParamInt('WorkerVertexCache',  1, title='Adopt worker-emitted vertex caches',
        doc="Use the vertex arrays a worker thread already computed for a rebuilt\n"
            "shape instead of capturing them again from the scene. 0 off, 1 on,\n"
            "2 does both and logs any difference (slow, for checking). Shapes with\n"
            "one colour only."),
    # The long form, kept here; the documentation shown is the short one below.
    # How long one scene publish may spend re-capturing changed
    # shapes into vertex caches before the rest are deferred. The
    # capture walks a changed shape's primitives one triangle at a
    # time, and during a descent storm every landed batch pays that
    # on the next paint: mid-paint stack samples put the capture at
    # about half of 250-850ms publish frames. Once this budget is
    # spent, each remaining changed shape keeps its previous vertex
    # cache for this frame (a shape captured for the first time
    # stays out of the frame entirely -- progressive appearance,
    # same as a live import), the caches on its path are left
    # unclosed for reuse, and another publish is scheduled; captured
    # shapes turn valid and prune, so successive frames always make
    # progress. The display is at worst a few frames stale in a
    # scene that is churning anyway; a single changed object never
    # comes near the budget. 0 captures everything in one frame,
    # as before this parameter existed.
    ParamInt('CaptureBudgetMS',  50, title='Vertex capture budget per publish (ms)',
        doc="Milliseconds one scene update may spend capturing changed shapes\n"
            "before the rest wait for the next frame. Keeps frames short while many\n"
            "objects change at once; a waiting shape shows its previous state for\n"
            "a frame or two. 0 captures everything in one frame."),
    # The long form, kept here; the documentation shown is the short one below.
    # A visual rebuild whose own cost passes this many
    # milliseconds reports its time split (traversal, mesh,
    # prologue, instancing, highlight) on one line naming the
    # object, under the level debug flag. The aggregate split says
    # where a mass descent's time goes; the landing pump's worst
    # turn is a single object's whole rebuild, and only a per-build
    # line says what that object spent it on. The same threshold
    # arms the slow-dispatch line in GUIApplication::notify, which
    # names the receiver of any single event-loop dispatch this
    # slow -- the net that catches a stall no timer above
    # bracketed. 0 turns both lines off.
    ParamInt('LevelSlowBuildMS',  200, title='Slow visual build report (ms)',
        doc="Diagnostic, with LevelDebug: a display rebuild, or a single event,\n"
            "that takes longer than this many milliseconds is logged with where the\n"
            "time went. 0 turns it off."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many descents (demotes/downgrades) one plan pass may
    # order, free tier and priced tier together; 0 removes the cap.
    # Each order enqueues a worker job -- the coarsening itself runs
    # on the refine pool -- but the enqueue snapshots the object's
    # display arrays on the GUI thread, so an unbounded pass (the
    # measured 1500-order plans) is itself a stall. Deferred
    # candidates keep their hooks and the replan after the batch
    # lands re-finds them, so nothing is refused, only paced -- the
    # climb admission batch's mirror.
    ParamInt('DescentOrderBatch',  64, title='Descent order batch',
        doc="How many downgrades one plan may order at a time. The rest are found\n"
            "again by the next plan, so nothing is lost, only paced. 0 removes the\n"
            "limit."),
    # The long form, kept here; the documentation shown is the short one below.
    # Whether the GPU downgrade sweep carries its own unlanded
    # orders as credit against the next plan's deficit
    # (docs/SceneStreaming.md #13c.4). A downgrade frees exactly the
    # bytes it prices, but not WHEN the plan next looks: the swap
    # uploads the coarse rung immediately while the fine buffers
    # leave the live meter only after the collection window -- on a
    # heavy scene, seconds -- so a plan sampling mid-transition reads
    # old+new at once, computes a larger deficit than the one just
    # covered, and walks other sources further down. Measured on a
    # 5455-object model at 64MB with the camera inside the assembly:
    # single plans requesting 1500+ downgrades, live tripling during
    # the storm, and the whole registry drained to its bottom rung
    # while the settled memory was under budget all along.
    # With the ledger, promised bytes hold the sweep until they are
    # observed landing or written off a few frames after the ordered
    # worker jobs have all drained (an order's bytes cannot land
    # before its descent job does); off restores the storming
    # behaviour for comparison.
    ParamBool('DowngradeLedger',  True, title='Downgrade ledger',
        doc="Count the GPU memory that downgrades already ordered will free as\n"
            "credit against the next plan's shortfall. Without it a plan made while\n"
            "those are still in flight orders them again, and far more than needed.\n"
            "Off is the older behaviour, kept for comparison."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many rungs the fidelity ladder declares
    # (docs/SceneStreaming.md #13). Rung n is tessellated at a
    # deflection of the shape diagonal over 8<<n, so rung 0 is the
    # coarsest and each further rung halves the error; this bounds
    # what Coarse tessellation level may select and how far a source
    # may climb. Raising it adds finer rungs, not coarser ones -- to
    # go below rung 0 the plan scales an object's error instead, see
    # Level scale.
    ParamInt('LevelCount',  8, title='Ladder rung count',
        doc="Number of levels of the mesh ladder. Level 0 is the coarsest and each\n"
            "further level halves the error, so raising this adds finer levels."),
    # The long form, kept here; the documentation shown is the short one below.
    # What the level plan multiplies an object's error by when it
    # must free memory and every ordinary descent is exhausted
    # (docs/SceneStreaming.md #13). Rung 0 is not the floor: under a
    # budget the plan keeps picking objects -- individually, cheapest
    # visible error first, never the whole scene at once -- and
    # re-tessellates each one this much coarser again, until the
    # model fits. An object whose scaled error reaches Level scale
    # box error is replaced by its bounding box, which is the real
    # floor: coarsening a deflection cannot drop a planar face below
    # the two triangles it always has, and on a measured STEP
    # assembly a 4x coarser tessellation removed only 19% of the
    # primitives. 1 or less turns dynamic scaling off, and then a
    # budget under what rung 0 costs cannot be honoured.
    ParamFloat('LevelScale',  2.0, title='Dynamic coarseness scale',
        doc="Factor by which an object is tessellated coarser again when GPU\n"
            "memory is still short at the coarsest ladder level. Applied object by\n"
            "object, least visible error first, until the scene fits. 1 or less\n"
            "turns this off."),
    # The long form, kept here; the documentation shown is the short one below.
    # The rest band above the GPU memory budget, as a fraction of
    # it, inside which the level plan orders NO downgrades. The sweep
    # triggers only past budget*(1+this) and still corrects back to
    # the budget itself, so the band is hysteresis, not a higher
    # budget.
    # Without it an equilibrium that lands ON the budget line has
    # nowhere to rest: the plan orders 2-3 downgrades, the release
    # staircase re-wants the quality back, and the ladder dithers
    # 0.2-0.4MB across the line for as long as the process lives --
    # measured on the rack model as the difference between a run
    # that settles in ~250s and one that churns its whole 600s
    # window. Climbs already stop AT the budget (Climb hard limit),
    # so inside the band neither direction acts and the plans go
    # genuinely quiet; pressure counts as standing there, which
    # keeps the raised tolerance and the edge gate latched exactly
    # as they were while the equilibrium was reached.
    # The band tolerates standing that fraction over the stated
    # budget (about 2MB at 64MB). 0 restores the bare line and with
    # it the dither.
    ParamFloat('LevelBudgetDeadband',  0.03, title='GPU budget deadband',
        doc="Band above the GPU memory budget, as a fraction of it, inside which no\n"
            "downgrades are ordered. Keeps a scene that settles at the budget from\n"
            "going back and forth across it. 0 removes the band."),
    # The long form, kept here; the documentation shown is the short one below.
    # The memory level, as a fraction of the GPU memory budget, above
    # which the level plan evicts RELEASED per-view-shown objects:
    # hidden objects some view showed on its own and none shows any
    # more, which the shared capture keeps for a quick show again.
    # Nothing on screen needs them, so they go first -- below the
    # budget, before any sweep that costs visible quality -- the big
    # and the long released before the recent, until the use is back
    # at the watermark. Under an observed CPU memory ceiling they go
    # first too, against the CPU shortfall. No GPU budget (GL states
    # none) means no GPU trigger. 1 or more waits for the budget
    # itself.
    ParamFloat('PerViewShownEvictWatermark',  0.9, title='Per-view shown eviction watermark',
        doc="GPU memory use, as a fraction of the budget, above which objects that\n"
            "one view had shown on its own and no view shows any more are freed.\n"
            "They go first, before anything that costs visible quality. 1 or more\n"
            "waits for the budget itself."),
    # The long form, kept here; the documentation shown is the short one below.
    # The scaled error at which an object stops being tessellated
    # at all and is drawn as its bounding box (12 triangles whatever
    # its face count), expressed relative to the shape diagonal. This
    # is where the ladder stops paying for topology it can no longer
    # resolve: past roughly a quarter of the diagonal a re-tessellated
    # shape and its box commit similar error, and only the box
    # actually removes the faces. 0 or less never substitutes a box.
    ParamFloat('LevelScaleBoxError',  0.25, title='Level scale box error',
        doc="Error, relative to its diagonal, at which an object is no longer\n"
            "tessellated and is drawn as its bounding box. 0 or less never uses a\n"
            "box."),
    # The long form, kept here; the documentation shown is the short one below.
    # When re-tessellating an object coarser stops removing
    # geometry, decimate the mesh it already has instead of dropping
    # straight to its bounding box (docs/SceneStreaming.md #13c).
    # The descent coarsens an object by asking OCCT for a larger
    # deflection, and that saturates: a planar face is two triangles
    # at any deflection, so a shape of flat faces answers the same
    # mesh however coarse the ask. Past that point the only thing
    # that removes geometry is a representation with fewer faces.
    # Vertex clustering is the rung between the two: it keeps the
    # object's shape, where the bounding box does not.
    # Rewrites the display nodes only. Nothing re-tessellates and the
    # OCCT triangulation is untouched, so the way back is one ordinary
    # rebuild, and each further step down clusters on a coarser grid.
    # Face and edge numbering survive: a face that decimates away to
    # nothing keeps its (empty) slot, because those tables are read by
    # element number.
    # What it gives up is exactness of the decimated rung -- section
    # caps through it can be rough, since clustering does not preserve
    # watertightness, and the hidden-line seam filter is dropped
    # because a welded edge may fold a seam and a non-seam together.
    ParamBool('SimplifyExhausted',  True, title='Decimate when tessellation is spent',
        doc="When tessellating an object coarser no longer removes triangles,\n"
            "decimate the mesh it has instead of going straight to its bounding\n"
            "box. Display only: the shape and its exact mesh are untouched. Section\n"
            "caps through a decimated object can be rough."),
    # The long form, kept here; the documentation shown is the short one below.
    # Let the decimator weld vertices across face boundaries
    # instead of clustering each face on its own grid.
    # Off, no output triangle spans two faces, so a modelled crease
    # stays a crease and each face keeps at least the triangles its
    # own cells produce. That floor is the catch: this rung is reached
    # precisely when a shape is mostly flat faces, and per-face
    # clustering cannot take a two-triangle face below two triangles.
    # On, positions and attributes cluster once over the whole mesh,
    # which is what actually removes geometry there -- at the cost of
    # shading round creases the model really has.
    # Face identity survives either way: a triangle still belongs to
    # the face it came from, so per-face colour and selection keep
    # working. Only the geometry is shared.
    ParamBool('SimplifyMergeParts',  False, title='Decimate across faces',
        doc="Let decimation merge vertices across face boundaries. Removes far\n"
            "more triangles on shapes made of flat faces, but rounds real creases.\n"
            "Per-face colour and selection keep working either way."),
    # The long form, kept here; the documentation shown is the short one below.
    # How much of an object's triangle count a decimation pass has
    # to remove for the result to be kept, as a percentage.
    # Below it the pass is refused and the descent takes its next step
    # instead, which is the bounding box. A rung that removes almost
    # nothing is worse than not having one: it costs a node rewrite
    # and still holds the memory that made the plan ask.
    # This is also what stops the descent looping. Each step clusters
    # on a coarser grid, so a mesh that has run out of things to merge
    # keeps answering no and the object moves on to the box.
    ParamFloat('SimplifyMinReduction',  20.0, title='Decimation worth doing (%)',
        doc="Percentage of an object's triangles a decimation has to remove for the\n"
            "result to be kept. Below it the decimation is dropped and the object\n"
            "goes on to its bounding box."),
    # The long form, kept here; the documentation shown is the short one below.
    # Let the vertex points that sit on the ends of a shape's edges
    # draw under the element contract (docs/SceneStreaming.md #13b):
    # an attached point set draws only while its object's line set is
    # shown and memory allows, is the FIRST class dropped under
    # pressure and the LAST taken back. Off suppresses attached point
    # sets outright, memory or not.
    # A point is not cheap: it costs the GPU a 32-byte sprite instance
    # record plus its index, roughly nine times what it occupies in
    # the heap, which is why a CPU-currency measurement made them look
    # negligible.
    # All or nothing per point set, and only ATTACHED sets are ever
    # gated: one floating vertex -- one no edge touches, and every
    # point of a point cloud -- and the whole set ranks with the
    # faces, because nothing else would show it. Objects are in
    # practice all floating or none, so a per-vertex subset would buy
    # nothing and cost an index permutation.
    # It never applies in the Points display mode, where the vertices
    # are what the mode exists to show.
    # Picking, pre-selection and selection highlighting are unaffected:
    # the point geometry stays published and resident, the highlight
    # draws render on top as always, and only the base-pass submission
    # is skipped.
    ParamBool('ShapeVertices',  True, title='Draw edge-attached vertices',
        doc="Draw the vertex points at the ends of a shape's edges. They are the\n"
            "first thing dropped when GPU memory is short and the last to return.\n"
            "Off never draws them. Free vertices and point clouds are always drawn,\n"
            "as is everything in Points mode; picking and highlighting are\n"
            "unaffected."),
    # The long form, kept here; the documentation shown is the short one below.
    # Let the pressure stages of the element contract
    # (docs/SceneStreaming.md #13b) stop drawing the edges that bound
    # faces. Under the contract an attached line set draws only while
    # its object's face set is shown and memory allows; pressure
    # spends the classes points -> lines -> faces and takes them back
    # in reverse, and this is the switch on the lines stage. Off
    # exempts line sets from the pressure stages (a loading document
    # still drops them).
    # Edge geometry is the GPU's most expensive geometry per unit of
    # screen information: a segment is 8 bytes of index in the heap
    # and those 8 bytes plus a 64-byte quad-expansion instance record
    # on the GPU.
    # All or nothing per edge set, attached sets only: one floating
    # edge -- a wire, a sketch, a datum line, any edge no face uses --
    # and the whole set ranks with the faces, because it is the
    # object, and dropping it would show nothing at all.
    # It never applies in the Wireframe display mode, where the edges
    # are what the mode exists to show.
    # A display gate, not a residency change -- nothing is demoted and
    # nothing re-tessellates, so entering and leaving it costs one
    # frame, which is why it is spent before any rung is given up.
    # Picking, highlighting and on-top rendering are unaffected.
    ParamBool('PressureDropEdges',  True, title='Drop face edges under pressure',
        doc="Allow the edges that bound faces to stop being drawn when GPU memory\n"
            "is short, after the vertices and before any face quality is given up.\n"
            "Wires, sketches and other edges no face uses are never dropped, and\n"
            "nothing is dropped in Wireframe mode. Picking and highlighting are\n"
            "unaffected."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many frames the element contract's pressure latch waits
    # between stages (docs/SceneStreaming.md #13b), both escalating
    # (points dropped, then lines if the budget is still exceeded) and
    # releasing (lines back, then points, once the ladder has given
    # back all raised error). The wait is what lets the buffer
    # collector's census answer whether the cheaper stage was enough
    # before the next one is spent, and what keeps the release from
    # re-opening into the memory the collector just freed.
    ParamInt('ElementGateStagger',  15, title='Element gate stage frames',
        doc="Frames to wait between the steps that drop vertices and then edges\n"
            "when GPU memory is short, and between the steps that bring them back."),
    # The long form, kept here; the documentation shown is the short one below.
    # MEASUREMENT INSTRUMENT, 0 = off. Suppress every line and
    # point draw issuing this many primitives or fewer, regardless of
    # the element contract -- floating sets included, which is the
    # point: the contract deliberately never gates those, and they
    # are what a far-field cut is left drawing
    # (docs/FarFieldProxies.md 11.1i).
    #
    # It exists to price the DRAW axis, which this engine has only
    # ever measured in the opposite regime. docs/DrawSubmission.md
    # dismissed draw count on a frame averaging ~1540 primitives per
    # draw, where the GPU is geometry-bound and a draw is free; the
    # far-field residue is ~12 primitives per draw, where a draw is
    # nearly all overhead. Setting this to ~24 on MiSTer removes
    # about 2% of the primitives and about 44% of the draws, so any
    # frame-time difference is attributable to draw count and not to
    # geometry.
    #
    # Not a display feature: it makes real edges vanish, and picking,
    # highlighting and on-top draws are exempt so the scene stays
    # usable while it is on.
    ParamInt('TinyElementCutoff',  0, title='Tiny element draw cutoff',
        doc="Measuring tool, 0 = off. Stops drawing every line and point set of\n"
            "this many primitives or fewer, to see what the number of draw calls\n"
            "costs. Real edges disappear while it is on; not a display setting."),
    # The long form, kept here; the documentation shown is the short one below.
    # Stop drawing edges AND vertices for as long as a document is
    # still arriving (docs/SceneStreaming.md #13b), and let the two
    # standing gates above decide again the moment it has finished.
    # A load is when the tier can least afford those two classes and
    # can least use them: the faces are arriving coarse-first and
    # being replaced under the camera, nobody inspects a vertex of a
    # model that is still half there, and every byte not uploaded to
    # an edge instance buffer now is one the arriving geometry gets
    # instead.
    # RE-MEASURED 2026-08-15, and the earlier reading no longer
    # holds. It used to suppress NOTHING on a .FCStd open: the load
    # parked every visual build and published in one step at the
    # end, so the renderer held an empty scene throughout -- 0
    # drawables across 17.8s on a 5455-object model. The publish is
    # incremental now, so the same open feeds the scene while the
    # drain runs and the gate has real work: on the same model it
    # climbs from 1123 to 5909 point and line draws suppressed, out
    # of 11818 eligible in a 17727-drawable scene, and both edges
    # are logged -- ON with an empty scene, OFF as the drain ends.
    # It overrides both gates while it lasts -- vertices drop even
    # with ShapeVertices on, edges drop with no pressure yet declared
    # -- but it is subject to the same all-or-nothing classification
    # and the same display-mode exemptions: a wire, a sketch, a datum
    # line or a point cloud draws throughout, because nothing else on
    # screen would show it, and neither class is dropped in the mode
    # that exists to show it.
    # Independent of this gate, the contract's dependency rule already
    # holds back an attached point or line set whose companion the
    # publish's capture budget deferred: an adopted vertex cache never
    # draws frames ahead of the face set it decorates, load gate or
    # not.
    # Costs one frame to leave, like the pressure gate, so what it
    # holds back comes straight back when the load lets go.
    # Applies only where coarse-first is on (CoarseTessellation 0 or
    # above): with everything tessellated exact up front there is no
    # progressive arrival for this to make room for.
    # A load here means a document restoring, a progressive import
    # filling one, or the deferred view-provider drain that follows a
    # restore -- geometry is still being built into the view in all
    # three.
    ParamBool('LoadDropElements',  True, title='Drop elements while loading',
        doc="Stop drawing face edges and edge vertices while a document is still\n"
            "loading, and draw them again once it has arrived. Wires, sketches,\n"
            "datum lines and point clouds are always drawn. Applies only with\n"
            "coarse-first tessellation (CoarseTessellation 0 or above)."),
    # The long form, kept here; the documentation shown is the short one below.
    # Resolution scale (0.25-1.0) of the expensive screen-space effect
    # passes -- the planar/ground reflection scene re-render, the water
    # body depth prepass and screen-space ambient occlusion -- relative to
    # the main view resolution. Lowering it trades effect sharpness for
    # speed on large windows, where those per-pixel passes dominate the
    # frame; the main geometry, edges, text and overlays stay full
    # resolution. 1.0 renders the effects at full resolution. The
    # volumetric light shafts already render at half resolution.
    ParamFloat('EffectResolution',  1.0, title='Effect resolution',
        doc="Resolution of the costly screen-space passes (ground reflection,\n"
            "water depth, ambient occlusion) relative to the view, 0.25 to 1.\n"
            "Lower is faster and softer; geometry, edges and text stay at full\n"
            "resolution."),
    # The long form, kept here; the documentation shown is the short one below.
    # Keep refining the image while the camera holds still.
    #
    # Multisampling antialiases the geometry it rasterizes and nothing
    # else: every sample inside one triangle is shaded once, so a
    # specular highlight crawling across a curved surface, a normal or
    # texture detail below the pixel, and every screen-space pass
    # computed after the resolve -- ambient occlusion, outlines,
    # section caps, the light shafts -- are left exactly as aliased or
    # as noisy as they were drawn. More coverage samples cannot help
    # any of them.
    #
    # This spends time instead. Once the camera stops, each further
    # frame offsets the projection by a fraction of a pixel and
    # averages into what is already on screen, so the whole pipeline
    # converges toward what supersampling it would have given -- and
    # it costs nothing at all while anything is moving.
    #
    # There is no reprojection and no history rejection, because
    # nothing moved: the accumulation is thrown away outright on any
    # camera, scene or highlight change, so a drag or an orbit returns
    # to the ordinary multisampled frame immediately with no ghosting,
    # smearing or trailing on thin edges. It is a refinement on top of
    # multisampling, not a replacement for it -- leave the antialiasing
    # preference where it is.
    #
    # The cost is idle GPU time: a parked view keeps drawing until it
    # has converged (TemporalAccumSamples), then stops and asks for
    # nothing more. On a laptop or a tablet that is battery, which is
    # why this is off by default and why it does not travel in a saved
    # document.
    ParamBool('TemporalAccum',  False, title='Idle temporal accumulation',
        doc="Keep refining the image while the camera holds still: each further\n"
            "frame is shifted by a fraction of a pixel and averaged in, which\n"
            "smooths what multisampling cannot (highlights, ambient occlusion,\n"
            "outlines). Discarded on any change, so nothing ghosts. Costs GPU time\n"
            "while idle, until TemporalAccumSamples frames have been added."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many jittered samples the idle accumulation converges over
    # before the view goes quiet (2-256, TemporalAccum only).
    #
    # The sequence is a Halton (2,3) pair over the pixel, so it fills
    # the pixel evenly at every count rather than clumping, and it is
    # indexed by sample number -- frame N of an accumulation is the
    # same frame N every time, which is what keeps a rendered
    # comparison reproducible.
    #
    # Most of the visible gain arrives in the first handful of
    # samples, since the error of an average falls with the square
    # root of the count: 32 halves the residual noise of 8, and 128
    # halves it again for four times the work. Raise it for a still
    # worth waiting on, lower it to reach the quiet state sooner.
    ParamInt('TemporalAccumSamples',  32, title='Idle accumulation samples',
        doc="How many frames idle accumulation adds up before the view goes quiet,\n"
            "2 to 256. Most of the gain comes in the first few; more gives a\n"
            "cleaner still image and takes longer."),
    # The long form, kept here; the documentation shown is the short one below.
    # Skip drawing what the depth buffer proves could not have
    # reached the screen (docs/FarFieldProxies.md §12). Bounding boxes
    # of the spatial index's nodes are tested against the depth the
    # occluders leave behind -- by default in a software depth buffer
    # on the CPU (Render_OcclusionSoftware), which answers within the
    # frame that asked -- and a node that puts no pixel through has
    # its whole subtree skipped, one test standing for thousands of
    # draws.
    #
    # Exact, not approximate: only geometry that could not have been
    # seen is removed, so the image is unchanged and what is saved is
    # the draw call, which measures ~1.2-1.5us of CPU submission plus
    # ~1.5-1.7us of GPU time whatever it contains (§10.2). It pays on
    # assemblies that hide themselves -- an enclosed chassis, a
    # populated rack, any interior -- and does nothing for a model
    # that is mostly silhouette. Expect roughly a fifth of the draws
    # from a camera inside a large assembly (§10.3); the far larger
    # figure from outside a closed model is a bound, not a promise.
    #
    # Casters and reflections are judged separately: geometry hidden
    # from the eye still casts its shadow and still appears in the
    # ground reflection.
    ParamBool('Occlusion',  False, title='Occlusion culling',
        doc="Skip drawing objects that are completely hidden behind others. The\n"
            "image does not change; what is saved is the draw calls. Helps on\n"
            "assemblies that hide their own insides, does little for a model that\n"
            "is mostly outline. Shadows and reflections of hidden objects are kept."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # How many frames a hidden node may go without an answer before
    # it is drawn again. A hidden node is re-tested continuously and
    # the answer is its only way back, so if answers stop arriving --
    # no query handles left, a dropped batch -- this is what returns
    # the geometry instead of leaving it missing. Answers that keep
    # confirming the node is hidden keep it hidden indefinitely, so
    # this never flickers a node the tests are still reaching.
    ParamInt('OcclusionMaxHidden',  120, title='Occlusion hidden lifetime',
        doc="With GPU occlusion queries: frames a hidden object may go without a\n"
            "new answer before it is drawn again. A safeguard for when answers\n"
            "stop arriving."),
    # The long form, kept here; the documentation shown is the short one below.
    # How far a test box is pushed towards the viewer before it is
    # tested, in steps of the 24-bit depth buffer. A test box has to
    # be a conservative bound, and at the last bit of the depth buffer
    # it is not: a small part lying flush on a large panel quantizes
    # to the same stored depth as the panel, LEQUAL loses the tie
    # whichever way the rasterizer rounds, and the node reports itself
    # hidden while in plain view. Measured that way, the components on
    # a board disappeared while the board stayed. Too large costs
    # frame time by testing visible what could have been skipped; too
    # small deletes geometry, so err high.
    ParamInt('OcclusionDepthPad',  16, title='Occlusion depth padding',
        doc="With GPU occlusion queries: how far a test box is moved towards the\n"
            "viewer, in depth buffer steps, so that a part lying flat on a larger\n"
            "one is not judged hidden. Too small hides visible parts, too large\n"
            "hides less; err high."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many consecutive answers of 'no pixels' a node must give
    # before its geometry is actually skipped. 1 acts on every
    # answer, and is what an occlusion test naively does.
    #
    # A test is issued against one frame's depth and read against a
    # later one -- it does not block, because stalling for it would
    # cost the frame time the culling exists to save -- so while an
    # answer is in flight, other geometry is culled and the occluders
    # move underneath it. Acted on singly, a node tested while an
    # occluder was still drawn gets skipped after that occluder has
    # gone; the hole it leaves tests visible; it comes back; and it
    # oscillates, which is a picture that flickers rather than one
    # that is merely wrong.
    #
    # Confirmations DILUTE that oscillation; measured, they do not
    # remove it (docs/FarFieldProxies.md #12.7): the false answers
    # arrive in runs, so tripling the confirmations bought a factor
    # of two, and the residual damage tracks how often nodes are
    # re-tested, which this setting cannot reach. The query path is
    # therefore not image-stable at any value here; occlusion on the
    # CPU (the default oracle) does not read this setting at all.
    ParamInt('OcclusionConfirm',  2, title='Occlusion confirmations',
        doc="With GPU occlusion queries: how many answers of 'hidden' in a row an\n"
            "object needs before it is skipped. More reduces flicker and does not\n"
            "remove it. Not used when occlusion runs on the CPU, the default."),
    # The long form, kept here; the documentation shown is the short one below.
    # Answer the occlusion question with a software depth buffer on
    # the CPU instead of hardware occlusion queries
    # (docs/FarFieldProxies.md #12.12). The default, because it is
    # the one oracle whose picture holds still.
    #
    # A hardware query cannot be asked at the moment its answer would
    # be right. It is issued against one frame's depth and read a
    # frame or two later, so a node is tested after the pass that drew
    # its own geometry and is asked to win a depth comparison against
    # itself -- measured as boxes returning no samples at all while
    # their contents were plainly on screen. The confirmations,
    # lifetimes and padding beside this setting all exist to contain
    # that, and none of them reach it.
    #
    # On the CPU, occluders are rasterized and nodes tested against
    # the same buffer in one pass, so a node is asked before its own
    # geometry joins the buffer and the answer arrives in the frame
    # that asked. There is no latency to age, no verdict to confirm
    # and no query pool to run out of. It costs CPU time in a frame
    # that is already CPU-bound, which is the trade to measure, and it
    # behaves identically in the browser, where hardware queries do
    # not.
    ParamBool('OcclusionSoftware',  True, title='Occlusion on the CPU',
        doc="Decide what is hidden with a depth buffer drawn on the CPU instead of\n"
            "GPU occlusion queries. The default: its answers belong to the frame\n"
            "that asked, where a GPU query answers a frame or two late and can\n"
            "flicker. Costs some CPU time per frame."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # How many worker threads the CPU occlusion buffer may rasterize
    # its occluders on. 0 picks automatically, leaving the submitting
    # thread and one other alone -- this runs in the middle of a
    # frame, not on an idle machine.
    #
    # Each worker rasterizes its own slice of the occluder list into
    # its own buffer and the buffers are merged afterwards, so there
    # is no locking. The merge is slightly lossy -- two two-layer
    # blocks cannot combine into one without loss -- so a higher
    # worker count can hide marginally less. Never more.
    ParamInt('OcclusionThreads',  0, title='Occlusion occluder threads',
        doc="Worker threads the CPU occlusion buffer may draw its occluders on.\n"
            "0 chooses automatically."),
    # The long form, kept here; the documentation shown is the short one below.
    # Let the CPU occlusion buffer discard triangles four at a time
    # with SIMD before its exact rasterizer looks at them
    # (docs/FarFieldProxies.md #12.14).
    #
    # Two thirds of the triangles offered to the buffer cover no pixel
    # at all -- a full-detail CAD tessellation is mostly triangles
    # smaller than the pixel grid -- and every one of them is paid for
    # in full before being thrown away. The pre-pass transforms and
    # projects four at once in single precision and drops the ones that
    # land on no pixel centre.
    #
    # It cannot make the buffer claim a surface that is not there:
    # everything it does not discard is handed to the same exact path
    # as before, recomputed from the original vertices, and a triangle
    # it drops in error is occlusion lost rather than geometry deleted.
    # Turn it off to measure what it saves, not to work around a
    # suspected fault.
    ParamBool('OcclusionSimd',  True, title='Occlusion vector pre-pass',
        doc="With CPU occlusion, discard triangles too small to cover a pixel four\n"
            "at a time before the exact rasterizer sees them. Faster, and it cannot\n"
            "hide anything visible. Turn off only to measure what it saves."),
    # The long form, kept here; the documentation shown is the short one below.
    # Resolution of the CPU occlusion buffer, as a divisor of the
    # viewport. 1 matches the viewport.
    #
    # Above 1 this can remove geometry that was visible, which is the
    # one failure this mechanism exists to avoid: a coarse pixel is
    # marked covered when an occluder reaches its centre, but it
    # stands for several real pixels, and the ones the occluder missed
    # are claimed with it. Reduce it only to measure what it costs, not
    # as a setting.
    ParamInt('OcclusionResolution',  1, title='Occlusion buffer divisor',
        doc="Resolution of the CPU occlusion buffer, as a divisor of the view size.\n"
            "1 matches the view. Above 1 it can hide visible geometry; change it\n"
            "only to measure."),
    # The long form, kept here; the documentation shown is the short one below.
    # Test each object against the CPU occlusion buffer, not just the
    # group it was partitioned into
    # (docs/FarFieldProxies.md #12.17). Only used when occlusion runs
    # on the CPU.
    #
    # The cull walk tests boxes of groups, and a group is skipped only
    # when all of it is hidden -- so one visible object keeps its
    # hidden neighbours on screen. Measured, that is what limits the
    # culling rather than the quality of the depth buffer: after a
    # cull, 91% of what is still drawn reaches no pixel, and making
    # the occluders ten times better barely moved it.
    #
    # The extra tests are read-only against a buffer that is already
    # finished, so they run on the same worker threads the occluders
    # used and add no state, no latency and nothing the backend has to
    # support.
    #
    # On by default: measured on the benchmark it hides 17% more for
    # 0.4ms, against 3% for 3.4ms from making the occluders ten times
    # better, and it over-culls nothing. It can only ever be more
    # correct than testing the group -- a draw is skipped when its own
    # box is covered rather than when its neighbours' collectively
    # are.
    ParamBool('OcclusionPerInstance',  True, title='Occlusion per instance',
        doc="With CPU occlusion, test each object on its own and not only the group\n"
            "it was sorted into, so one visible object no longer keeps its hidden\n"
            "neighbours drawn. Hides more for little cost; on by default."),
    # The long form, kept here; the documentation shown is the short one below.
    # How many consecutive frames every draw of an object must have
    # been culled before the level plan's downgrade sweep may treat
    # it as free -- give its GPU upload back without charging the
    # camera any visible error. 0 never does. Only used when
    # occlusion runs on the CPU, whose verdicts are exact per frame.
    #
    # This is occlusion acting as a MEMORY mechanism: an enclosed
    # assembly's interior is inside the view frustum, so without a
    # hidden verdict the plan prices its downgrade as visible error
    # and pays for it in quality somewhere that actually shows. What
    # the sweep drops stays resident in CPU RAM; the way back is an
    # ordinary refine, so a verdict the camera later overturns costs
    # one upload. The streak is the hysteresis that keeps a drifting
    # camera from paying that upload per flap.
    ParamInt('OcclusionDemoteStreak',  8, title='Occlusion demote streak',
        doc="With CPU occlusion: how many frames in a row an object must have been\n"
            "completely hidden before its GPU memory may be given back at no\n"
            "quality cost. It stays in main memory and returns when seen again.\n"
            "0 never does this."),
    # The long form, kept here; the documentation shown is the short one below.
    # Rasterize the CPU occlusion buffer's occluders from coarse
    # hulls instead of from their meshes
    # (docs/FarFieldProxies.md #12.16). Only used when occlusion runs
    # on the CPU.
    #
    # An occluder does not need the mesh, it needs the surface, and a
    # hull carries that at a fraction of the triangles. What the
    # triangle budget above buys is what this changes: measured, 1285
    # of 1322 candidate occluders never entered the buffer because 37
    # full-detail draws spent the whole allowance, and the buffer then
    # hid 45% of what was there to hide.
    #
    # The hulls are built by vertex clustering from the meshes the
    # renderer already holds -- no shape, no tessellator -- a few per
    # frame, and cached. A hull recedes by its own measured error
    # before it is rasterized, so it cannot claim to be nearer than
    # the surface it stands for.
    ParamBool('OcclusionCoarse',  False, title='Occlusion coarse occluders',
        doc="With CPU occlusion, draw the occluders from simplified hulls instead\n"
            "of their full meshes, so many more of them fit the triangle budget and\n"
            "more gets hidden. A hull is moved back by its own error, so it cannot\n"
            "hide what is visible."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # How far an occluder hull recedes from the camera before it is
    # rasterized, as a percentage of its own measured displacement.
    #
    # Every point of a hull lies within that displacement of a point of
    # the mesh it was built from, so at 100 the hull cannot be nearer
    # than the surface it stands for -- which is what makes an
    # approximate occluder admissible at all. Below 100 it hides more
    # and may hide geometry that was visible; above 100 it hides
    # progressively less for nothing. 0 rasterizes the hull where it
    # sits, which is the measurement that says whether the bias is
    # needed.
    ParamInt('OcclusionCoarseBias',  100, title='Occlusion hull bias',
        doc="With coarse occluders: how far a hull is moved away from the camera,\n"
            "as a percentage of its own error. 100 guarantees it hides nothing\n"
            "visible; less hides more and may hide visible parts."),
    ParamInt('OcclusionCoarseMemory',  64, title='Occlusion hull memory',
        doc="What the occluder hull cache may hold, in megabytes, before\n"
        "the least recently used hulls are dropped. A dropped hull costs a\n"
        "rebuild when its occluder comes back into view, never\n"
        "correctness."),
    # The long form, kept here; the documentation shown is the short one below.
    # Measure whether the culling pays for itself on THIS scene and
    # camera (docs/FarFieldProxies.md 12.13): alternate stretches of
    # frames with the whole occlusion block on and off, compare median
    # frame cost, and print the verdict with the culling readout
    # (Render_LevelDebug cadence). The probe is an intervention -- its
    # off arm draws everything and pauses the hidden-streak demote
    # feed for those frames -- so it is a measuring instrument, not a
    # mode to leave on. The verdict gates nothing yet; it is the
    # number the wire-or-delete decision for CullBenefitEstimator
    # reads.
    ParamBool('OcclusionBenefitProbe',  False, title='Occlusion benefit probe',
        doc="Diagnostic. Measures whether occlusion culling pays for itself on this\n"
            "scene and camera, by turning it on and off for stretches of frames and\n"
            "comparing their cost. It disturbs the frames it measures; not for\n"
            "normal use."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Resolution scale (0.25-1.0) of the ambient occlusion resolve
    # targets relative to the main view resolution, independent of the
    # shared Effect resolution. Ambient occlusion is resolution-sensitive
    # (contact and crevice detail), so it has its own control; the shared
    # Effect resolution drives only the costlier reflection re-render.
    # 1.0 renders the occlusion at full resolution; lower trades AO
    # sharpness for speed.
    ParamFloat('AOResolution',  1.0, title='AO resolution',
        doc="Resolution of ambient occlusion relative to the view, 0.25 to 1,\n"
            "independent of EffectResolution. Lower is faster and less sharp."),
    # The long form, kept here; the documentation shown is the short one below.
    # Enable screen space cavity (curvature) shading of the
    # experimental render engine (render cache mode 3 with a selected
    # renderer type). Darkens concave creases and convex ridges found
    # in the geometry prepass normals, which makes surface shape and
    # small features read without relying on the lighting.
    #
    # Best paired with the Shaded draw style, the one that draws no
    # edges: there the darkened crease is the only thing stating where
    # a face ends, so cavity does the job the edge lines do elsewhere,
    # without the wireframe over every tessellated curve. In a style
    # that already draws edges (Flat Lines) the two land on the same
    # pixels and cavity mostly restates them.
    #
    # Independent of ambient occlusion: cavity is a local curvature
    # term, occlusion is a visibility integral over a world-space
    # radius (contact darkening). They compose.
    ParamBool('Cavity',  True, title='Cavity shading',
        doc="Darken creases and ridges of the geometry in screen space, so the\n"
            "shape reads without relying on the lighting. Works best with the\n"
            "Shaded draw style, where no edges are drawn. Independent of ambient\n"
            "occlusion. Needs render cache mode 3 with a renderer selected."),
    # The long form, kept here; the documentation shown is the short one below.
    # Baseline the cavity curvature is measured over, in pixels.
    #
    # This decides which features the pass can see at all. The term
    # reads how far the surface normal turns between the two
    # neighbours, so at the default of 1 it sees only what turns
    # within a single pixel: hard creases, crisply, which is what
    # stands in for the edge lines the Shaded draw style does not
    # draw. Widening it brings broad curvature (fillets, blends, a
    # sculpted face) in, at the cost of spreading a hard crease into a
    # band of this width.
    #
    # Being in pixels it is resolution-relative: the same value covers
    # less of the model on a high-DPI display, so a large model on a
    # dense screen may want more than 1.
    ParamFloat('CavityRadius',  1.0, title='Cavity radius',
        doc="Distance in pixels over which cavity shading measures curvature. 1\n"
            "sees only hard creases, sharply. Larger values bring in fillets and\n"
            "broad curvature and widen the creases to a band of that width."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Enable matcap shading of the experimental render engine
    # (render cache mode 3 with a selected renderer type). Replaces
    # the scene's lighting with a fixed studio attached to the camera,
    # looked up by each fragment's view space normal: the shading of a
    # surface then depends only on which way it faces the viewer, so
    # form reads identically wherever the scene light happens to be.
    # The classic inspection shading -- pair it with Cavity for edge
    # definition. Overrides physically based shading while on.
    ParamBool('Matcap',  False, title='Matcap shading',
        doc="Shade surfaces by the direction they face the viewer, with a fixed\n"
            "studio lighting attached to the camera, so shape reads the same\n"
            "wherever the scene light is. Overrides physically based shading.\n"
            "Needs render cache mode 3 with a renderer selected."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Read an ordinary Phong appearance's specular COLOUR as
    # physically based material data, where nothing states a
    # metalness of its own. The metallic/roughness model has no
    # specular slot -- its reflectance follows from the base colour
    # and the metalness -- so a classic Gold, whose gold-ness lives
    # entirely in that colour, otherwise shades as yellow-brown
    # plastic, and the presets built from a black diffuse and a
    # bright specular (Steel, Satin, Metalized) shade as nearly
    # black. Anything authored stands: a stated metalness, a PBR
    # appearance, a metallic-roughness map.
    ParamBool('PBRFromSpecular',  True, title='Specular to metallic',
        doc="Read the specular colour of a classic appearance as metalness when\n"
            "the material states none, so that presets such as Gold or Steel look\n"
            "like metal under physically based shading. A stated metalness is\n"
            "never changed."),
    # The long form, kept here; the documentation shown is the short one below.
    # How a classic Phong appearance's SHININESS becomes a
    # roughness, where the material states no roughness of its own.
    #
    # Either way the conversion itself is the standard match of the
    # GGX lobe width to a Phong exponent n, roughness =
    # (2 / (n + 2)) ^ 1/4. What differs is what shininess MEANS.
    #
    # 'GL exponent' reads it the way fixed-function GL did, as the
    # exponent scaled onto 0..128. That is faithful, but 128 is the
    # sharpest exponent GL could state, and it converts to a
    # roughness of 0.35 -- so on this reading a fully shiny Phong
    # material is satin, and the lower half of the roughness range
    # cannot be reached from shininess at all.
    #
    # 'Full range' reads shininess as what the Appearance dialog
    # presents, a 0 to 100% appearance control, and maps it onto the
    # whole exponent range instead: n = 128 * s / (1 - s). Matte at
    # zero and a mirror at one, and over the low shininess values
    # real materials use it agrees with the GL reading to within a
    # few percent (FreeCAD's default 0.2 gives 0.49 rather than
    # 0.52, the Gold preset 0.66 rather than 0.67).
    #
    # Neither reading touches anything authored: a stated roughness,
    # a PBR appearance, a metallic-roughness map and the per-object
    # Render_Roughness override all stand.
    ParamInt('ShininessMapping',  1, title='Shininess mapping',
        proxy=ParamComboBox(items=['GL exponent', 'Full range']),
        doc="How the shininess of a classic appearance becomes a roughness when the\n"
            "material states none. 'GL exponent' reads it as the OpenGL exponent,\n"
            "where the shiniest material is still satin. 'Full range' reads it as\n"
            "0 to 100%, matte to mirror. A stated roughness is never changed.",
        ),
    # The long form, kept here; the documentation shown is the short one below.
    # Which built-in environment lights the scene, where no
    # environment image is set. They are computed rather than
    # sampled from a file, so they cost no assets and work on every
    # tier including the browser.
    #
    # What separates them is contrast and structure, not brightness:
    # all five integrate to the same mean radiance, so the exposure
    # that suits one suits the others. That matters because a
    # surround with no bright sources and no edges cannot put a
    # highlight on anything that reads as a light, and a smooth
    # surface reflecting it shows the same flat grey at every
    # roughness -- which is what made physically based shading look
    # like painted plastic.
    #
    # Interior = a room with one window and a ceiling
    # panel, walls close enough to bounce. One hard key against a
    # dark surround, which is what gives the crispest highlight and
    # the strongest read of form. Studio = four soft boxes on a dark
    # surround, the product-shot rig, gentler and more even than
    # Interior. Gradient (the default) = the smooth three-band dome
    # this engine used before the others existed; the flattest and
    # the most even, which is why it is where a view starts -- it
    # stays out of the way of the model being worked on, and it is
    # the one to pick to have an older document's look back.
    # Overcast = a bright sky weighted to the zenith over dark
    # ground, soft and neutral. Sunset = a low warm sun with a deep
    # sky, the strongest colour separation, and the only one that
    # tints the whole frame. Light tent = a box of white panels,
    # bright BELOW the horizon as well as above it and seamed all
    # the way round; the one to pick when the SIDES of a subject
    # matter, since every other environment here puts a floor under
    # it and a standing wall reflects the floor.
    ParamInt('PBREnvPreset',  1, title='Environment',
        proxy=ParamComboBox(items=['Studio', 'Gradient', 'Overcast',
                                   'Sunset', 'Interior', 'Light tent']),
        doc="Built-in environment that lights the scene when no environment image\n"
            "is set: Gradient (the default, the most even), Interior, Studio,\n"
            "Overcast, Sunset or Light tent. They differ in contrast and structure,\n"
            "not in brightness, so one exposure suits them all."),
    ParamFloat('PBREnvIntensity',  1.0, title='Environment brightness',
        doc="Brightness of the image based lighting environment."),
    # The long form, kept here; the documentation shown is the short one below.
    # Image file used as the image based lighting environment,
    # replacing the built-in procedural studio environment. A 2:1
    # image is read as equirectangular (lat-long), anything squarer
    # as a sphere map — the same convention as the Texture mapping
    # dialog's Environment mode, so the same file works in both.
    #
    # A Radiance picture (.hdr, .pic) is read as real radiance and
    # is the format worth using: a sky is thousands of times
    # brighter than the wall beneath it, and an ordinary 8-bit image
    # cannot hold that ratio, which is what makes one light a model
    # like a picture rather than like a place. An HDR environment
    # needs the output colour transform on, since it is the exposure
    # that decides how its range lands on the screen.
    #
    # What to load, in short:
    #
    #  - A Radiance .hdr or .pic. OpenEXR is NOT read: anything
    #    that is not Radiance goes through Qt, which has no EXR
    #    plugin, so an .exr loads nothing and the procedural
    #    environment stays on.
    #  - 2:1 proportions, so it is taken as a lat-long panorama
    #    and not as a mirror ball. Up is +Z, and the middle of
    #    the image faces +X.
    #  - 1K or 2K is plenty. The picture is held as 32-bit float
    #    RGB (2K is about 25 MB, 8K about 400 MB) and is baked
    #    into a 128 pixel per face cubemap, so a larger one
    #    costs memory without showing more.
    #  - Free CC0 panoramas: polyhaven.com/hdris.
    #
    # How sharp it is DRAWN behind the model is a separate
    # question, and the answer is Render_PBREnvBlur: the background
    # pass draws that cubemap through a lens aperture, the way a
    # real backdrop is out of focus, and at zero the aperture is
    # shut and it is drawn as baked. The lighting and the
    # reflections read the sharp environment whatever the blur
    # says.
    #
    # Empty falls back to that dialog's current image, then to the
    # procedural environment.
    ParamString('PBREnvImage', '', title='Environment image',
        doc="Image file used as the lighting environment instead of the built-in\n"
            "one. A 2:1 image is read as a lat-long panorama, anything squarer as a\n"
            "sphere map. Radiance files (.hdr, .pic) keep their real brightness and\n"
            "are the format to use; OpenEXR is not read. 1K or 2K is plenty. Empty\n"
            "uses the Texture mapping dialog's image, then the built-in environment."),
    # The long form, kept here; the documentation shown is the short one below.
    # Store a copy of the environment image inside the document,
    # so it travels with the file instead of depending on the
    # original path. The copy lives in the view's
    # Render_PBREnvImageData property and takes precedence over the
    # image path while set.
    #
    # On by default: a document whose lighting depends on a file
    # somewhere on one machine opens lit differently everywhere
    # else, and the path is the part of the setting least likely
    # to survive the trip.
    ParamBool('PBREnvEmbed', True, title='Embed environment image',
        doc="Store a copy of the environment image in the document, so the lighting\n"
            "travels with the file instead of depending on a path on one machine.\n"
            "The copy takes precedence over the path."),
    # The long form, kept here; the documentation shown is the short one below.
    # Show the image based lighting environment itself as the view
    # background while physically based shading is active, so
    # reflective surfaces visibly mirror their surroundings.
    #
    # On by default, because a reflective object standing in front of
    # a flat gradient reads as fake for a reason that is not the
    # object's fault: the reflection has no visible source, so there
    # is nothing in the frame for the eye to reconcile it against.
    # Affects nothing outside physically based shading -- the
    # Classic and Matcap models keep the background gradient.
    ParamBool('PBREnvBackground', True, title='Environment background',
        doc="Show the lighting environment as the view background while physically\n"
            "based shading is active, so reflections have a visible source. Other\n"
            "shading models keep the background gradient."),
    # The long form, kept here; the documentation shown is the short one below.
    # How far out of focus the environment background is, 0 to 1.
    # Zero is sharp -- the resolution it was baked at; one opens the
    # aperture to 45 degrees, and in between it doubles every eighth
    # of the range. Only the BACKGROUND is affected -- the lighting
    # and the reflections read the whole environment whatever this
    # says.
    #
    # It is a defocus, not a smudge: the environment is convolved
    # with the disc of directions an aperture subtends, in linear
    # radiance, so a small bright source spreads into an even bokeh
    # disc that keeps its energy rather than being averaged away.
    #
    # A backdrop wants some of this. A real one is out of focus, and
    # softening also lets a small bright source bleed into a wide
    # gentle falloff instead of sitting in the frame as a hard
    # rectangle. Too much of it and there is nothing left for a
    # reflection to be reconciled against, which is the whole reason
    # the background is drawn at all. Blender's viewport shading
    # carries the same control for the same reasons, and defaults it
    # higher than this does.
    #
    # Both shading models honour it, and at zero the two show the
    # same backdrop: they bake the environment at the same angular
    # resolution. The external path tracer gets there differently,
    # since the world it samples IS the light and softening it
    # would relight the scene -- so a second bake of the same
    # environment through the same aperture is mixed in on CAMERA
    # rays alone, and the lighting, reflections and refractions keep
    # the sharp world. One consequence of that rule: a camera ray
    # stays a camera ray through a transparent surface, so a
    # see-through pass-through shows the soft backdrop as well.
    ParamFloat('PBREnvBlur',  0.25, title='Environment background blur',
        doc="How far out of focus the environment is drawn as the background, 0 to\n"
            "1. 0 is as sharp as it was baked. Only the background is affected:\n"
            "lighting and reflections always read the sharp environment."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # The ambient ripple pattern on the water surface - the motion
    # the surface has of its own accord. 0 = waves: the default sum
    # of directional wind waves. 1 = rain: circular rings expanding
    # from randomly placed, randomly timed drop impacts, as on a pond
    # in rainfall. 2 = none: a still surface, which leaves only what
    # the scene disturbs - fountain splash rings and the impact rings
    # of particles striking the water still show.
    ParamInt('WaterRippleType',  0, title='Ripple type',
        proxy=ParamComboBox(items=['Waves (directional)', 'Rain (drops)',
                                   'None (still)']),
        doc="Ripples the water surface has by itself: 0 wind waves, 1 rain rings,\n"
            "2 none. Rings from fountains and from particles striking the water\n"
            "show in every case."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Let the render engine supply its own directional or spot scene
    # light, described by the Light* settings below, instead of taking
    # one out of the Coin traversal.
    #
    # Everything the engine keys off a light -- shadows, volumetric
    # shafts, the sun disc, ground reflection -- today has exactly one
    # source: the Shadow display style, which is what puts an
    # SoShadowDirectionalLight or SoSpotLight in the scene graph at all
    # (the viewer headlight is a plain SoDirectionalLight, which the
    # engine rejects by type). That makes a draw style the owner of the
    # lighting, and it is why the style cannot simply be retired
    # (docs/CoinRetirement.md 3.4).
    #
    # Off by default, and while off nothing changes. A light found in
    # the traversal still wins when one is there, so the Shadow style
    # keeps behaving exactly as before; these settings supply a light
    # when it does not.
    ParamBool('Light',  False, title='Renderer scene light',
        doc="Let the render engine use a scene light of its own, described by the\n"
            "Light settings below, for shadows, light shafts and ground reflection.\n"
            "Without it the only such light is the one the Shadow display style\n"
            "adds. A light found in the scene still takes precedence."),
    ParamFloat('LightIntensity',  0.8, title='Light intensity',
        doc="Brightness of the renderer's own scene light."),
    ParamFloat('LightDirectionX',  -1.0,
        doc = "X component of the direction the render engine's own scene light\n"
              "shines along, in world coordinates. A direction of zero length\n"
              "falls back to (-1, -1, -1)."),
    ParamFloat('LightDirectionY',  -1.0,
        doc = "Y component of the direction the render engine's own scene light\n"
              "shines along, in world coordinates. A direction of zero length\n"
              "falls back to (-1, -1, -1)."),
    ParamFloat('LightDirectionZ',  -1.0,
        doc = "Z component of the direction the render engine's own scene light\n"
              "shines along, in world coordinates. A direction of zero length\n"
              "falls back to (-1, -1, -1)."),
    ParamHex('LightColor',  0xf0fdffff, title='Light color', proxy=ParamColor(),
        doc="Colour of the renderer's own scene light."),
    ParamBool('LightSpot',  False, title='Use spot light',
        doc="Make the renderer's own light a spot rather than a directional\n"
        "one. A spot has a position and a cone; a directional light has\n"
        "only a direction."),
    ParamFloat('LightPositionX',  0.0,
        doc = "X coordinate of the render engine's own scene light when it is a\n"
              "spot light, in world coordinates. A directional light ignores it."),
    ParamFloat('LightPositionY',  0.0,
        doc = "Y coordinate of the render engine's own scene light when it is a\n"
              "spot light, in world coordinates. A directional light ignores it."),
    ParamFloat('LightPositionZ',  0.0,
        doc = "Z coordinate of the render engine's own scene light when it is a\n"
              "spot light, in world coordinates. A directional light ignores it."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Compute device type the External shading model path traces
    # on, as Gui.cyclesDevices() names them: 'CPU' always works, and
    # 'CUDA', 'OPTIX' or 'HIP' when this machine has the GPU and the
    # driver for it. Seeds the per-view Cycles_Device property, which
    # offers only the devices the machine actually has -- a document
    # saved elsewhere falls back to the first local device when its
    # choice does not exist here.
    ParamString('CyclesDevice', 'CPU', title='Cycles device',
        doc="Device the path tracer runs on: 'CPU' always works; 'CUDA', 'OPTIX' or\n"
            "'HIP' when the machine has the GPU and driver. A document saved with a\n"
            "device this machine lacks uses the first one available."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # How many path-traced sessions this process serves at once
    # (docs/CyclesIntegration.md sec 7.1). A browser viewer that asks
    # for a path-traced view gets a Cycles session of its own -- one
    # per traced cell, per connection, across every served document --
    # and each holds a device context and the scene on that device.
    # A start made when this many are already running is refused with
    # 'TooManyStreams'; the viewer says so and stays on its raster
    # view. 0 or less means no cap, which is what the desktop views
    # and the offline render have always had: this counts served
    # streams only.
    ParamInt('CyclesMaxStreams',  4, title='Cycles served sessions',
        doc="How many path-traced views this process serves to browser viewers at\n"
            "once. A request past the limit is refused and that viewer stays on its\n"
            "raster view. 0 or less means no limit. Desktop views are not counted."),
    # The long form, kept here; the documentation shown is the short one below.
    # Render debugging buffer visualization (docs/RenderDebug.md).
    # Routes an intermediate render target to the screen instead of the
    # shaded scene: 1 = linearized scene depth, 2 = view-space normals,
    # 3 = ambient occlusion term only, 4 = shadow term only, 5 = shadow
    # map / bulb-tile coverage as color, 6 = overdraw heatmap, 7 =
    # shadow-moment filtering-precision probe, 8 = UV / texcoord,
    # 9 = the planar reflection target, 10 = the particle impact map
    # (green where a hit is recorded, brightness its age, red where
    # nothing has ever struck).
    # 0 renders normally. The on-top, highlight and overlay passes
    # still draw on top so the view stays navigable.
    ParamInt('DebugViewMode',  0, title='Debug view mode',
        proxy=ParamComboBox(items=['Off', 'Depth', 'Normal', 'AO', 'Shadow',
                                   'ShadowTile', 'Overdraw', 'ShadowFilter',
                                   'UV', 'Reflection', 'ImpactMap']),
        doc="Diagnostic. Shows an intermediate buffer instead of the shaded scene:\n"
            "1 depth, 2 normals, 3 ambient occlusion, 4 shadow, 5 shadow map\n"
            "coverage, 6 overdraw, 7 shadow precision, 8 texture coordinates,\n"
            "9 reflection target, 10 particle impact map. 0 renders normally."),
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
    # The long form, kept here; the documentation shown is the short one below.
    # Log where the time of a rendered frame goes, by pipeline
    # stage: the Coin traversal, the flattening of the vertex caches,
    # the draw-entry build, the translation to the backend, the
    # backend's own bookkeeping and the draw itself. One summary line
    # per second, so a long operation shows how each stage grows with
    # the scene rather than one average (docs/IncrementalPublish.md).
    # Those stages end at submission, so a second line reports what
    # happens after it: the frame's cost on the CPU issuing draw
    # commands against its cost on the GPU drawing them, and the same
    # pair per draw call (docs/FarFieldProxies.md §10.1). Which of the
    # two a scene is bound by is what decides whether a culling scheme
    # has to remove the draw or may leave it to the GPU to reject.
    ParamBool('DebugTiming',  False, title='Render stage timing',
        doc="Diagnostic. Logs once a second where the time of a rendered frame\n"
            "goes, by pipeline stage, and what the frame costs on the CPU against\n"
            "the GPU."),
    ParamBool('DebugDelta',  False, title='Publish change set',
        doc="Log what each published frame actually changed: how many of\n"
        "the scene cache's children the publish reused, how many it added\n"
        "or dropped, and how many separators the traversal below it reused\n"
        "against how many it rebuilt. One summary line per second. A\n"
        "publish rebuilds the whole scene however little moved, and these\n"
        "counts are how much of that rebuild was avoidable\n"
        "(docs/IncrementalPublish.md §5)."),
    # The long form, kept here; the documentation shown is the short one below.
    # Log how much of the screen each drawn object actually covers,
    # as a histogram over its projected size in pixels. A camera that
    # sees a whole assembly draws most of it at a few pixels, and every
    # one of those parts still costs a full object; the histogram says
    # how much of the model is in that state, which is what decides
    # whether aggregating distant parts is worth building
    # (docs/FarFieldProxies.md §9).
    ParamBool('DebugCoverage',  False, title='Screen coverage histogram',
        doc="Diagnostic. Logs how much of the screen each drawn object covers,\n"
        "as a histogram over its size in pixels. Shows how much of a model is\n"
        "drawn only a few pixels large."),
    # The long form, kept here; the documentation shown is the short one below.
    # Log what a far-field cut would cost this camera, without
    # generating anything: the drawn instances are partitioned into the
    # spatial index of docs/FarFieldProxies.md §3, a frontier is chosen
    # by projected error at several tolerances, and the draws that cut
    # would issue -- one per (cell, material) proxy plus whatever stays
    # exact -- are reported against the draws issued today. This is the
    # number that says whether generating proxies is worth building
    # (§11.1). Also reports the distributions that size the partition:
    # instances and material buckets per cell, per level.
    ParamBool('DebugProxyCut',  False, title='Far-field cut estimate',
        doc="Diagnostic. Logs how many draw calls replacing distant parts by\n"
            "far-field proxies would save for the current camera, without building\n"
            "any."),
    # The long form, kept here; the documentation shown is the short one below.
    # Measure how much of what the frame draws could not have
    # reached the screen (docs/FarFieldProxies.md §10.1). Bounding
    # boxes of the spatial index's nodes are re-rasterized against the
    # finished depth buffer under hardware occlusion queries, writing
    # neither colour nor depth, and every instance is attributed to the
    # highest node that rejects it -- so a hidden subtree is counted
    # once, not at every level it is hidden at. Boxes bound their
    # contents loosely and the frustum's own rejections are reported
    # separately, so the hidden share it prints is a floor rather than
    # an estimate. A GPU offers 256 queries at a time, so a large model
    # takes several frames to walk and a line is printed per completed
    # walk, never for a partial one.
    ParamBool('DebugOcclusion',  False, title='Occluded fraction',
        doc="Diagnostic. Measures how much of what a frame draws is hidden behind\n"
            "something else, using GPU occlusion queries. A large model takes\n"
            "several frames per report."),
    # The long form, kept here; the documentation shown is the short one below.
    # Generate real proxies for a sample of the nodes a far-field
    # cut stops on, and report what they cost and what they commit
    # (docs/FarFieldProxies.md §11.1c). The cut estimate above selects
    # by a node's projected *extent* because no proxy exists yet to
    # have an error; this one merges each (cell, material) group and
    # decimates it, so the error it commits can be measured as a
    # fraction of that extent -- which is the ratio that says whether
    # the estimate reads as its 16px row or its 64px row. Reports
    # alongside it the triangle cost against what instancing already
    # achieves (§7.1) and how much surface area survives, since
    # clustering deletes geometry smaller than a cell rather than
    # shrinking it. Expensive: it builds meshes. Samples a bounded
    # number of nodes and reports how many it skipped.
    ParamBool('DebugProxyGen',  False, title='Far-field proxy generation',
        doc="Diagnostic. Builds real far-field proxies for a sample of nodes and\n"
            "reports their triangle cost and the error they introduce. Expensive:\n"
            "it builds meshes."),
    # The long form, kept here; the documentation shown is the short one below.
    # Check what the occlusion culling skipped against what the
    # geometry actually put on screen (docs/FarFieldProxies.md §12.9).
    # Every other measurement of the culling compares two pictures and
    # reports how many pixels differ, which says that something is
    # wrong without saying what: this re-rasterizes the scene with the
    # cull mask ignored and each draw writing its own identity instead
    # of a colour, so the ids that own a pixel are an exact answer to
    # which draws reach the screen. Their intersection with the mask is
    # a list of proven over-culls -- each one a named draw with a pixel
    # count -- and the ids that own nothing while being drawn are the
    # converse: the headroom the culling has not taken. Reads the image
    # back to the CPU once a second, so it costs a full-resolution
    # transfer on the frames it reports and nothing while off. Needs a
    # backend with texture readback, which WebGL2 is not.
    ParamBool('DebugCullAudit',  False, title='Occlusion cull audit',
        doc="Diagnostic. Checks what occlusion culling skipped against what really\n"
            "reaches the screen, by drawing every object once more with its\n"
            "identity as its colour, and reports objects culled by mistake. Reads\n"
            "the image back once a second. Not available on WebGL2."),
    # The long form, kept here; the documentation shown is the short one below.
    # Measure whether a tighter occludee volume would cull more
    # (docs/FarFieldProxies.md §12.19). After per-instance testing, 90%
    # of the draws a frame still submits reach no pixel while each was
    # tested and answered visible -- so the geometry is hidden and the
    # box around it is not. This re-asks every still-drawn row three
    # ways against the same occluder buffer: with the world box that
    # ships, with the mesh's own box through the model matrix (an
    # oriented box, where the shipping one is the axis-aligned box
    # around it), and with every triangle asked separately -- which is
    # far too slow to ship and is here as the ceiling, since nothing
    # asked about the occludee can beat asking about its geometry. The
    # verdicts are counted against the cull audit's id image, never
    # acted on, so an arm that would have deleted something visible
    # reports itself instead of being believed.
    # Needs the cull audit on (it supplies the image) and the software
    # occluder pass, which owns the buffer being asked. Runs on the
    # audit's frame only, and costs far more than a frame: it is a
    # measurement, not a mode to leave on.
    ParamBool('DebugCullBounds',  False, title='Occludee bound diagnostic',
        doc="Diagnostic. Measures whether tighter bounds around objects would let\n"
            "occlusion culling hide more, by asking again about every object still\n"
            "drawn in three ways. Reports only, changes nothing on screen. Needs\n"
            "the cull audit and CPU occlusion, and is far too slow to leave on."),
]

def declare_begin():
    params_utils.declare_begin(sys.modules[__name__])

def declare_end():
    params_utils.declare_end(sys.modules[__name__])

def define():
    params_utils.define(sys.modules[__name__])

params_utils.init_params(Params, NameSpace, ClassName, ParamPath)
