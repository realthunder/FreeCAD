/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 ***************************************************************************/

#ifndef PARTGUI_MESH_LEVEL_SOURCE_H
#define PARTGUI_MESH_LEVEL_SOURCE_H

/// The shape-backed level generator (docs/SceneStreaming.md §7): when
/// the scene server is asked for a coarser level of a published mesh,
/// re-tessellate the source shape at the level's deviation instead of
/// decimating the exact mesh. Edge polylines come from
/// Poly_PolygonOnTriangulation of the same coarse triangulation, so
/// they lie on the coarse surface by construction — the structural fix
/// for edges floating off decimated facets at silhouettes — and the
/// per-face/per-edge part tables fall out of the shape's own element
/// order, index for index with the exact mesh.
///
/// Registration follows the display tessellation: whoever builds the
/// visual nodes for a shape registers that shape under the very node
/// pointers the render feed will carry as MeshData::sourceTag
/// (Render::MeshSourceRegistry). The registered closure owns a
/// refcounted shape handle and runs on the server's level threads,
/// tessellating a fresh structure copy — never the live shape.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class SoNode;
class TopoDS_Shape;

namespace App {
class Document;
}

namespace PartGui {

/// Everything a level build needs beyond the shape and the source
/// chunk — the whole job is a pure function of these (MeshLevelBuild),
/// which is what lets the scene server run level builds on several
/// threads at once (and would let the job move behind a process
/// boundary later, docs/ComputeBoundaries.md).
struct MeshLevelJob {
    uint32_t level = 0;
    bool normalsFromUV = false;
    /// The full display-formula parameters, used when \a level is
    /// Render::kExactMeshLevel (the on-demand exact build of a
    /// coarse-first ladder).
    double exactDeflection = 0;
    double exactAngle = 0;
};

/// Build the bytes of the requested level chunk from \a shape,
/// honoring the source chunk's element tables index for index (false
/// on any mismatch — the caller falls back to decimation). Pure and
/// thread-safe; tessellates a structure copy, never the shape itself.
bool buildMeshLevel(const TopoDS_Shape &shape, const MeshLevelJob &job,
                    const void *sourceChunk, size_t sourceSize,
                    std::vector<uint8_t> &out);

/// Register the level generator for \a shape — the exact shape the
/// display tessellation just meshed (flattened whole shape, or an
/// instanced leaf in its local frame) — under the face and edge shape
/// nodes it was meshed into. Either tag may be null. Replaces any
/// previous registration of the same tags.
///
/// \a builtError states what the display tessellation itself is: 0
/// when it ran at the full display deviation (the usual case), else
/// the ladder error (relative to the shape diagonal) it was
/// deliberately built coarse at — coarse-first publish, in which case
/// \a exactDeflection / \a exactAngle carry the full display
/// parameters the on-demand *exact* build (kExactMeshLevel) will use.
///
/// \a onExactBuilt is the desktop tier's climb back to exact
/// (docs/SceneStreaming.md §13): given for a coarse-first build on a
/// plain desktop process (no scene server — a serving process's
/// viewers drive the exact rung themselves), the worker pool meshes a
/// structure copy of the shape at the exact display parameters
/// off-thread and calls back ON THE GUI THREAD with the meshed copy.
/// The callback transfers the triangulation and rebuilds its nodes.
/// It fires at most once, and never after the tags were re-registered
/// or unregistered — which is also the cancellation: a build obsoleted
/// mid-job completes, fails that check, and is dropped.
///
/// \a onDemote and \a onDowngrade are the two ways back down (§13
/// step 3), given by a source standing at its exact rung with the
/// coarse one still resident beside it (transferMeshLevels keeps it);
/// both fire on the GUI thread, consumed on fire. onDemote drops the
/// exact rung from CPU RAM — fired only against an observed memory
/// ceiling. onDowngrade merely re-activates the coarse rung for
/// display, keeping the exact one resident — fired when the GPU
/// budget wants upload bytes back; the climb back is then instant.
/// \a demoteError states the coarse rung's error so the plan can
/// price either drop against the tolerance. A coarse registration
/// with a finer rung still resident (a downgraded source) arms its
/// own hidden-rung demote internally, and its refine skips the
/// worker.
/// \a doc is the document the shape belongs to: whether *that*
/// document is served is what arms or skips the desktop refine climb
/// (docs/MultiDocServe.md §5) — serving one document must not disarm
/// another's views. Null falls back to the process-wide reading.
///
/// \a onScaleDown / \a scaledError are the dynamic-scale descent (sec 13):
/// what a source ALREADY displaying its coarse rung can still give up
/// when a budget cannot be met any other way. Rung 0 is not the floor;
/// the plan keeps picking objects one at a time and each re-tessellates
/// itself coarser again, so \a scaledError is the error the next step
/// would show and the callback is what takes it. Armed only where a
/// step exists -- past the box-error threshold a shape becomes its
/// bounding box, which has no step after it.
void registerMeshLevelSource(const TopoDS_Shape &shape, bool normalsFromUV,
                             SoNode *faceTag, SoNode *lineTag,
                             float builtError = 0.0f,
                             double exactDeflection = 0.0,
                             double exactAngle = 0.0,
                             std::function<void(const TopoDS_Shape &)>
                                 onExactBuilt = {},
                             std::function<void()> onDemote = {},
                             float demoteError = 0.0f,
                             std::function<void()> onDowngrade = {},
                             App::Document *doc = nullptr,
                             const char *origin = nullptr,
                             std::function<void()> onScaleDown = {},
                             float scaledError = 0.0f);

/// Drop the registration made under these tags (before the nodes die;
/// their addresses may be reused).
void unregisterMeshLevelSource(SoNode *faceTag, SoNode *lineTag);

/// Mesh a structure copy of \a shape at \a deflection / \a angle on the
/// refine worker pool and hand it to \a apply on the GUI thread -- the
/// same job queue, token and memory-ceiling guard the exact climb uses,
/// with the parameters given rather than taken from the registration.
///
/// This is what makes the ladder's DESCENT a build too (sec 13, dynamic
/// scale). Climbing needs the exact parameters and nothing else, but
/// going *below* the rung a shape was built at cannot be a swap: OCCT
/// keeps a resident triangulation that is finer than the one asked for,
/// so a coarser display mesh has to be meshed. The caller's apply is
/// then transferMeshLevels followed by demoteMeshLevels -- the transfer
/// brings the coarser rung in beside the finer one, and the demote,
/// which keeps the triangulation with the fewest nodes, drops the finer
/// one and its edge polygons. That pair is the whole of "adopt this
/// coarser mesh and free what it replaces".
void queueMeshLevelBuild(const void *tag, const TopoDS_Shape &shape,
                         double deflection, double angle,
                         std::function<void(const TopoDS_Shape &)> apply);

/// Run \a work on the refine worker pool and its returned landing on
/// the GUI thread -- the decimation rung's half of the same descent
/// (sec 13c). \a work owns everything it reads (a SNAPSHOT of the
/// display arrays, taken on the GUI thread at enqueue -- Coin nodes
/// are not thread-safe here) and returns the landing closure, or null
/// when the rung refused. The landing runs only while the tag's
/// registration is still the live one (same token as the refine jobs:
/// re-registering or unregistering the tag cancels), which is what
/// makes capturing the view provider sound. Descent jobs are counted
/// in MeshSourceRegistry::descentInFlight() from enqueue to
/// settlement and run ahead of queued climbs.
void queueMeshDescentWork(const void *tag,
                          std::function<std::function<void()>()> work);

/// Cancel the pending worker job and un-run landing queued under \a
/// tag, if any. For jobs whose tag is NOT a registration's primary
/// tag -- the pooled visual fill keys its jobs on the coords node so
/// they never collide with the decimation/mesh jobs on the faceset
/// tag -- unregisterMeshLevelSource cannot cancel them, and the owner
/// must do it here before it dies (the landing captures the view
/// provider).
void cancelMeshLevelWork(const void *tag);

/// Run \a body on the GUI thread under the landing pump's per-turn
/// time budget instead of now. This is how a plan-ordered hook body
/// leaves the plan callback: a sweep fires up to a whole batch of
/// hooks in one callback, and their bodies -- a display-array
/// snapshot, a resident-rung rebuild -- measured second-long bursts
/// when run in place. Items are keyed by \a tag: unregistering or
/// re-registering the tag purges what has not run (the bodies capture
/// their view provider, and the destructor unregisters). A descent
/// item counts as an in-flight descent from enqueue to run/purge, so
/// the downgrade ledger's write-off horizon covers the deferral; a
/// climb body passes \a descent false, because the settle counter it
/// would advance is what the ledger judges its ORDERS' completion by,
/// and a climb settling is not a downgrade landing.
void queueLevelGuiWork(const void *tag, std::function<void()> body,
                       bool descent = true);

/// Whether the caller is executing inside the landing pump's turn --
/// diagnostic context for the slow-build attribution (a pump item, a
/// drain slice and any other queued call all dispatch as the same
/// meta-call to the application object, and naming which one a slow
/// rebuild ran under is what the instruments could not do).
bool inLandingPump();

/// Stop the level worker threads -- the refine pool and the reaper --
/// and join them. Hooked to the application's aboutToQuit and its
/// post routines the first time a worker starts, so the statics they
/// wait on are never destroyed under them (glibc's
/// pthread_cond_destroy blocks on a parked waiter: the exit() hang).
/// Idempotent, safe to call with no worker started. Queued jobs are
/// dropped, their descents settled; a build in flight finishes first.
void shutdownMeshLevelWorkers();

/// The coarse-first tessellation level for display builds; negative
/// means tessellate at the full display deviation as always. Resolved
/// from the CoarseTessellation render parameter — per-view
/// Render_CoarseTessellation overrides it — wherever something can
/// climb the build back to exact: a scene stream server (viewers'
/// cameras ask for the exact rung, docs/SceneStreaming.md §7), or a
/// desktop view on the bgfx renderer (the refine worker rebuilds it,
/// §13). Plain Coin display keeps the exact tessellation — a coarse
/// build there would simply stay coarse. The FC_COARSE_TESSELLATION
/// environment variable overrides everything for a whole process.
/// \a doc scopes the gate to the document being tessellated: it is
/// open when that document is served (or the whole process is, via
/// FC_BGFX_SERVE_SCENE), and the Render_CoarseTessellation override is
/// read from that document's serving container when it has one. Null =
/// process-wide reading (any serve counts, active view's override).
int coarseTessellationLevel(App::Document *doc = nullptr);

/// Mesh a structure copy of \a shape at the given display parameters
/// and return it (null on failure). Pure and thread-safe — the copy
/// shares geometry but owns fresh TShapes, so the live shape is never
/// touched; the worker-pool half of the desktop exact refine.
/// \a outOfMemory, when given, is set if the failure was an
/// allocation failure (std::bad_alloc or OCCT's Standard_OutOfMemory)
/// — the caller's memory-ceiling observation (§13 step 3).
TopoDS_Shape meshLevelExactCopy(const TopoDS_Shape &shape,
                                double deflection, double angle,
                                bool *outOfMemory = nullptr);

/// Move the triangulations of \a from (a meshed structure copy) onto
/// \a to (the live shape it was copied from): face triangulations,
/// their edges' polygons-on-triangulation, and free edges' 3D
/// polygons, matched by the copy's preserved sub-shape order. The
/// coarse triangulation each face already holds is KEPT beside the
/// arriving exact one (exact active) — §13 step 3's "keep both", so
/// a demotion under memory pressure is a drop, not a rebuild. GUI
/// thread — the reader of these is the display build. No-op when the
/// two shapes do not correspond.
void transferMeshLevels(const TopoDS_Shape &from, const TopoDS_Shape &to);

/// Drop every face triangulation of \a shape except its coarsest
/// resident one (re-activated), removing the edge polygon
/// representations bound to the dropped ones so their memory really
/// frees. The way back down for a shape transferMeshLevels refined —
/// meshing after this is a no-op, the coarse rung never left. Returns
/// whether anything was dropped. GUI thread, like the transfer.
bool demoteMeshLevels(const TopoDS_Shape &shape);

/// The GPU's way back down (§13 step 3): move each face's *active*
/// mark to its coarsest resident triangulation, keeping every rung in
/// CPU RAM — the coarse node rebuild then uploads the small arrays,
/// and the climb back is transferMeshLevels(shape, shape): an instant
/// re-activation of the finest rung, no worker, no re-tessellation.
/// Returns whether anything changed. GUI thread.
bool downgradeMeshLevels(const TopoDS_Shape &shape);

/// Total nodes over every face triangulation of \a shape -- the size of
/// what it is currently tessellated at, in the one unit both a
/// re-tessellation and a simplification change. What says whether
/// asking BRepMesh for a coarser mesh actually produced one: for a
/// shape of planar faces it does not, at any deflection.
int meshLevelNodeCount(const TopoDS_Shape &shape);

/// Whether \a shape holds more than one resident triangulation on any
/// face — i.e. a finer rung a refine could activate without the
/// worker, and a hidden rung a CPU-memory ceiling could drop.
bool meshLevelFinerResident(const TopoDS_Shape &shape);

/// The linear / angular deflection of ladder level \a level for a
/// shape of the given bbox diagonal — the generator's own grid
/// (error 1/(8<<level) of the diagonal), exposed so a coarse-first
/// display build tessellates exactly the rung it will publish as.
double meshLevelDeflection(double diagonal, unsigned level);
double meshLevelAngle(unsigned level);

} // namespace PartGui

#endif // PARTGUI_MESH_LEVEL_SOURCE_H
