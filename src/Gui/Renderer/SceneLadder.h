/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 ****************************************************************************/

#ifndef RENDERER_SCENE_LADDER_H
#define RENDERER_SCENE_LADDER_H

/// Which rung of docs/SceneStreaming.md §6's ladder each object is
/// drawn at, and what that costs — the policy half of progressive
/// rendering, with none of the transport.
///
/// The split this header exists to make is between **what to have** and
/// **how to get it**. Ranking a payload by what the camera can see of
/// the objects that want it, and giving one back when the budget is
/// full, are the same decisions whether the rung arrives over a socket
/// or comes out of a tessellator: both are a queue of known-size
/// payloads, ordered by a camera, bounded by memory. Only the
/// acquisition differs, and that is behind RungProvider.
///
/// So nothing here knows about fetches, IndexedDB, emscripten or bgfx,
/// and the arithmetic is plain float rather than a math library's
/// vectors — the two tiers should not have to agree on a vector type to
/// share a policy.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "SceneDump.h"

namespace Render {

/// The camera as one round of ordering sees it.
///
/// \a right may be either the camera's right axis or its negation: the
/// only test that uses it is symmetric about the view centre, so the
/// sign cannot matter, and requiring a convention of two callers that
/// disagree would only invite a bug that never shows.
struct LadderView {
    float eye[3] {0.0f, 0.0f, 0.0f};
    float at[3] {0.0f, 0.0f, 0.0f};
    float right[3] {1.0f, 0.0f, 0.0f};
    float up[3] {0.0f, 1.0f, 0.0f};
    /// Vertical field of view in degrees.
    float fovY = 45.0f;
    /// Width over height. Zero or negative is read as 1.
    float aspect = 1.0f;
};

/// Where an object is, by the key its chunks name it with.
///
/// A lookup rather than a map, because only the consumer knows where
/// bounds live: the viewer has to look in both the objects it has
/// applied and the ones a staged publish has only announced, and the
/// desktop has the view providers. Returning null means the bounds are
/// not known — which scores zero and sorts last, rather than failing.
using LadderBounds = std::function<const float *(uint64_t objectKey)>;

/// How strongly a payload's size counts against it, for the two
/// decisions that rank chunks. 0 ignores size and ranks purely by what
/// the camera sees; 1 ranks strictly per byte; above 1 discounts size
/// steeper than per byte, and is honored rather than clamped — a tuning
/// knob that silently saturates lies to the person turning it.
///
/// **Acquiring leans on size, keeping does not.** What to get next is a
/// question about a rate — the appearance layer costs a thousandth of
/// the geometry and lifts every object a whole rung, so discounting by
/// size is what puts a model in its real colours in a fraction of a
/// second. What to keep is a question about a stock, and there size is
/// beside the point: the near, detailed object is the one worth its
/// memory even though it is the expensive one.
///
/// The acquire default is a *square root* rather than the full per-byte
/// discount it started as. Per byte, a payload a thousand times smaller
/// was a thousand times preferred, which is far more than "colour
/// first" needs and left a large near mesh queued behind every trivial
/// distant one — visible as boxes in the foreground of a half-loaded
/// model. At 0.5 the same payload is thirty times preferred: the
/// appearance layer still arrives first by a wide margin, and geometry
/// is ordered much more by where it is.
struct LadderWeights {
    float acquire = 0.5f;
    float keep = 0.0f;
};

/// The camera, and the priority of each object it has already had to
/// work out.
///
/// Both halves are memoization and both are needed. The camera frame
/// costs four trigonometric functions, and a chunk shared by every
/// object in the scene carries every one of those objects — so scoring
/// a queue of a few hundred chunks against a few hundred owners apiece
/// re-derived the same camera millions of times per load. Measured, it
/// was the whole cost of the fetch: a load that should have been
/// network-bound spent its time in `sin`.
class RendererExport RungRanker {
public:
    RungRanker(const LadderView &view, LadderBounds bounds,
               const LadderWeights &weights = LadderWeights());

    /// Priority of one object: larger is wanted sooner.
    ///
    /// Projected size, which is distance and extent in one number, and
    /// is what "get what matters" means on a screen: a near wall and a
    /// far building can be equally worth having. Off-screen is a
    /// penalty rather than an exclusion — the object is still acquired,
    /// just behind everything visible, so a viewer that never moves
    /// still ends up holding the whole scene, and one that turns around
    /// finds the work already started.
    float owner(uint64_t objectKey);

    /// What a chunk is worth, discounted by what it costs: **the best
    /// of the objects that need it**, never the first of them, over its
    /// payload size raised to \a sizeWeight.
    ///
    /// The best owner, because content addressing means one material
    /// can back a whole scene and one mesh every instance of a part,
    /// and such a chunk is named by whichever manifest happened to be
    /// read first — which may be the smallest object in the far
    /// distance. Taken at that object's priority it holds up every
    /// object that shares it, and since an object is only drawn once
    /// its whole appearance is resident, one starved material is a
    /// scene that never leaves its boxes.
    ///
    /// Note it keeps the rule the deferred list is built on: what a
    /// consumer does with a chunk follows from its size and never from
    /// what is inside it (SceneDump.h). Nothing here knows a material
    /// from a mesh — it does not need to, because the appearance layer
    /// *is* the small one.
    float value(const SceneSnapshot::DeferredChunk &chunk, float sizeWeight);

    /// What to ask for next: value per byte spent, because bandwidth —
    /// or a tessellator's throughput — is a rate, and the question is
    /// what to spend the next byte on.
    float acquisition(const SceneSnapshot::DeferredChunk &chunk);
    /// The same, priced at \a bytes instead of the entry's own size:
    /// a ladder's ask is for ONE rung, and the entry's size names the
    /// finest built one, which is not what the next byte is being
    /// spent on when the plan targets a coarser rung (§7, "the ladder
    /// owns its fetch state" — fetch identity stopped riding on
    /// entry.key/size).
    float acquisition(const SceneSnapshot::DeferredChunk &chunk,
                      uint32_t bytes);

    /// What to keep, which is a different question: memory is a stock,
    /// and what belongs in it is what the camera is looking at.
    ///
    /// Measured on the 200-object scene under an 8 MB budget, keeping
    /// per byte gave 494 resident chunks averaging 16 KB against 146
    /// refused averaging 146 KB — the refused set was precisely the
    /// detailed geometry, wherever it was, so the foreground kept boxes
    /// while the distance was fully modelled. Ignoring size instead:
    /// 234 resident averaging 35 KB against 406 refused averaging
    /// 52 KB, and distance decides membership.
    float residency(const SceneSnapshot::DeferredChunk &chunk);

    const LadderView &view() const { return m_view; }

private:
    /// Projected size of a bounding box, penalised if it is off-screen.
    float project(const float *bbox) const;

    LadderView m_view;
    LadderBounds m_bounds;
    LadderWeights m_weights;
    /// Forward axis, normalised, derived once from eye and at.
    float m_fwd[3] {0.0f, 0.0f, 1.0f};
    /// Tangent of the half vertical field of view.
    float m_tanHalfFov = 0.0f;
    std::map<uint64_t, float> m_memo;
};

/// The ladder as the planner walks it, whether or not the entry
/// declares one: an entry with no `levels` list is a ladder of one
/// rung — its own exact content. These accessors are what keep that
/// case from being an `if` at every use site.
RendererExport size_t planRungs(const SceneSnapshot::DeferredChunk &entry);
RendererExport const std::string &planRungKey(
    const SceneSnapshot::DeferredChunk &entry, size_t rung);
RendererExport uint32_t planRungSize(
    const SceneSnapshot::DeferredChunk &entry, size_t rung);
RendererExport float planRungError(
    const SceneSnapshot::DeferredChunk &entry, size_t rung);
/// The finest / coarsest rung with a key — fetchable now. The finest
/// built always exists (v36: the entry's own key names it), so these
/// never answer -1 on a well-formed entry.
RendererExport int finestBuiltRung(const SceneSnapshot::DeferredChunk &entry);
RendererExport int coarsestBuiltRung(const SceneSnapshot::DeferredChunk &entry);

/// The coarsest rung of \a entry whose stated error is within \a err.
/// The exact rung closes every ladder at error 0, so this always
/// answers on a well-formed entry.
RendererExport int rungWithin(const SceneSnapshot::DeferredChunk &entry,
                              float err);

/// What one plan is asked to respect (docs/SceneStreaming.md §7,
/// "Selection is a plan, not a reaction").
struct PlanParams {
    /// Payload bytes the planned geometry may total. The view's own
    /// (ownerless) chunks are outside it, as they are outside
    /// eviction: a fixed few hundred kilobytes the scene holds the way
    /// it holds its own bookkeeping.
    size_t budgetBytes = 0;
    /// The screen-space error a coarser rung may commit before a finer
    /// one is wanted, in pixels. Non-positive means every object
    /// desires its exact content — the budget still bounds what it
    /// gets.
    float tolerancePx = 0.0f;
    /// Viewport height in pixels, which is what turns a relative error
    /// into a screen-space one.
    float viewportPx = 0.0f;
    /// Diagnostic tap, called once per planned object after the greedy
    /// has run: the object's key, its projected diameter in pixels
    /// (off-screen penalty included), the tier it was granted (-1 is
    /// the box), and how many tiers it wanted.
    std::function<void(uint64_t key, float diamPx, int tier,
                       size_t tiers)> trace;
    /// In/out: the error tier each object was granted, keyed by
    /// object — what per-instance rung binding consumes (§7, "one rung
    /// per instance"): an entry's plan is the FINEST owner's rung, and
    /// which rung THIS owner stands on is rungWithin(entry, its err).
    /// Negative = the plan left the object on its box. Cleared and
    /// refilled by every plan. Null = not wanted.
    ///
    /// Read before it is refilled: the incoming grants are the
    /// PREVIOUS plan's, and an object they placed on a finer tier
    /// keeps it unless the newly desired error clears the boundary
    /// tier's error by a margin — the hysteresis that stops a
    /// drifting camera from walking an object across a rung boundary
    /// and back every replan (the tier-cut twin of kPlanKeepBonus).
    /// A caller that passes a fresh map each plan simply gets no
    /// hysteresis.
    std::map<uint64_t, float> *objectErr = nullptr;
};

/// What a plan did, for the one report worth printing: how much of the
/// scene the budget admitted.
struct PlanStats {
    /// Objects the plan assigned a rung.
    size_t objects = 0;
    /// Objects held below their desired rung by the budget — the
    /// honest meaning of "at the geometry budget".
    size_t capped = 0;
    /// Payload bytes the plan targets, budget-bounded geometry only.
    size_t plannedBytes = 0;
};

/// Decide what the whole scene should hold: one target rung per owned
/// geometry entry, written to `DeferredChunk::plan`.
///
/// Global and greedy: every object starts at its box, every candidate
/// upgrade — this object, its next-finer error tier — is scored by
/// screen-space error removed per byte added, and the best is taken
/// until the budget is spent or every object has reached the coarsest
/// rung the camera cannot tell from exact. Planned per *object*, so an
/// object's face and edge chunks land on the same rung, and a chunk
/// shared by several objects is planned at the finest rung any of them
/// needs with its bytes counted once.
///
/// Deterministic: the same camera, ladders and budget produce the same
/// plan, which is what there is instead of damping — a plan cannot
/// oscillate with itself. Runs on events (camera settled, publish
/// staged, ladder announced, budget moved), never per arrival; that
/// separation is the entire point of the design.
///
/// A rung declared but unbuilt may be targeted — the executor asks the
/// producer for it and stands on a built neighbour meanwhile. Its
/// unknown size enters the budget as an estimate scaled from the
/// nearest built rung (four to one per grid level, the generator's own
/// ratio), corrected by a re-plan when the announcement states it.
///
/// Ownerless geometry — the view's own — is not planned against the
/// budget at all: it always targets its finest built rung.
RendererExport PlanStats planLevels(SceneSnapshot &snap, RungRanker &ranker,
                                    const PlanParams &params);

/// The executor's half: what to do about ONE entry, given the rung it
/// currently holds. Pure — the caller owns every side effect — and
/// idempotent by construction: acting on the answer moves the entry
/// toward its plan, and an entry at plan answers "nothing".
/// The rungs a ladder should hold — the union, over its owners, of the
/// rung each owner's granted error maps to (§7, "one rung per
/// instance"). \a errOf answers an owner's granted tier error:
/// negative = the plan left it on its box (it needs nothing), NaN =
/// the plan has not seen it (it needs the finest built rung, the
/// pre-plan default). Ownerless entries — the view's own — always
/// need their finest built rung.
RendererExport uint16_t planNeeded(
    const SceneSnapshot::DeferredChunk &entry,
    const std::function<float(uint64_t)> &errOf);

struct PlanStep {
    /// Rung to acquire now, -1 for none: the coarsest needed rung not
    /// yet resident — and when nothing at all is resident, the
    /// coarsest built rung outright: a few kilobytes on screen this
    /// round beat the target in flight over a box — the consumer-side
    /// echo of the producer's coarse-first publish (§7).
    int fetch = -1;
    /// Rung to ask the producer to build (RungProvider::generate), -1
    /// for none: some owner needs it and no bytes exist yet.
    int generate = -1;
    /// Resident rungs no owner needs, as a mask — reported, never
    /// shed here. Releasing at replan is what made the camera's every
    /// drift a fetch/release cycle (measured: one object re-asking the
    /// same rung seven times in a minute of orbiting); eviction is the
    /// reverse of arrival, driven by the budget, so the executor keeps
    /// a surplus rung resident until its bytes are actually wanted.
    /// Only ever non-zero once every needed rung is resident (or
    /// nothing is needed at all — the box): until then a surplus rung
    /// is the stand-in some owner is drawing.
    uint16_t surplus = 0;
};
RendererExport PlanStep planStep(const SceneSnapshot::DeferredChunk &entry,
                                 uint16_t neededMask);

/// The desktop tier's plan pass (docs/SceneStreaming.md §13 step 2),
/// pure policy: among \a draws, the source tags whose coarse-first
/// tessellation commits more screen-space error than \a tolerancePx.
///
/// A draw participates when its mesh names a source (MeshData::
/// sourceTag) and states a coarse error (MeshData::levelError, relative
/// to the shape diagonal); the error on screen is that fraction of the
/// bounding box diagonal projected at the box centre. Off-screen draws
/// never refine — that is the residency bill this pass exists to stop
/// paying; the camera that turns toward them is a new plan. A
/// non-positive tolerance refines every coarse source, the same "every
/// object desires exact" reading as PlanParams::tolerancePx — the
/// step-1 behavior, kept reachable.
///
/// Matrices are GL-layout 4x4 (what Renderer::render receives). Each
/// tag appears at most once; the caller feeds them to
/// MeshSourceRegistry::requestRefine, which is idempotent anyway.
RendererExport std::vector<const void *> planMeshRefines(
    const DrawCallList &draws, const float *viewMatrix,
    const float *projMatrix, float viewportHeightPx, float tolerancePx);

/// A demotion only ever runs when the camera would not notice it by a
/// margin: the coarse rung must err at most this fraction of the
/// tolerance. Demoting at the refine boundary itself would make a
/// drifting camera trade a full tessellation back and forth across it
/// — the tier-cut hysteresis lesson (§7), with both directions costing
/// work here.
constexpr float kPlanDemoteMargin = 0.5f;

/// The way back down (§13 step 3), pure policy: among \a draws, the
/// *exact*-resident sources (levelError 0) whose coarse rung — its
/// error answered by \a demoteErrOf, 0 = not demotable — would commit
/// at most kPlanDemoteMargin × \a tolerancePx on screen, plus every
/// demotable source off screen or wholly behind the camera. Only
/// consulted under an observed memory ceiling: without one the desktop
/// keeps every rung it built ("keep both"), and a non-positive
/// tolerance demotes nothing — everything desires exact.
RendererExport std::vector<const void *> planMeshDemotes(
    const DrawCallList &draws, const float *viewMatrix,
    const float *projMatrix, float viewportHeightPx, float tolerancePx,
    const std::function<float(const void *)> &demoteErrOf);

/// A rung that may not exist yet, named by what would *produce* it
/// rather than by what it will contain.
///
/// This is the awkward part of generating levels on demand, and it does
/// not go away by being ignored. A chunk's key is the SHA-1 of its
/// bytes (docs/SceneStreaming.md §9, invariant 1), so a level nobody
/// has tessellated has no bytes, therefore no key, therefore cannot be
/// asked for the way every other payload is. A request for work is not
/// a request for bytes and cannot share the channel.
///
/// So a level is named by its source and its index instead: stable
/// across publishes, identical for every viewer that wants it, and
/// derived from content rather than from who is asking — which is what
/// keeps the server free of per-viewer state (invariant 7) even once it
/// has a queue of work. What comes back is the key, and from there it
/// is an ordinary chunk with an ordinary content address.
struct LevelRequest {
    /// Content identity of the geometry the level would be generated
    /// from — any *built* level's key of the same ladder (the producer
    /// canonicalizes, so siblings name the same job). Not the key of
    /// the level — that is what this asks for.
    std::string source;
    /// Which rung, coarsest first. What a level *means* — a tessellation
    /// deviation, a decimation ratio — is the producer's business, and
    /// deliberately not encoded here.
    uint32_t level = 0;
};

/// The level index that names the *exact* mesh (§7, coarse-first
/// publish): a producer that published a coarse tessellation declares
/// the exact one at error 0, unbuilt, and this is how it is asked for.
/// A sentinel rather than a ladder index, because ladder positions of
/// the coarser rungs double as generator grid levels and the exact
/// mesh is not on that grid — its deviation is the display formula's.
constexpr uint32_t kExactMeshLevel = 255;

/// How a rung is actually obtained. The ladder decides *what* to
/// acquire and what to give back; this is the tier's answer to *how*.
///
/// The two implementations differ in everything except their shape. The
/// browser asks a server for bytes over a link whose cost is a round
/// trip; the desktop asks a tessellator for a mesh at a deviation,
/// where the cost is CPU and the bytes never travel. Both are
/// asynchronous, both answer with a payload the ladder can rebuild
/// from, and both have a sensible limit on how much may be in flight —
/// which is the entire reason one policy can drive them.
///
/// Nothing here returns a payload: acquisition completes by the chunk's
/// own fill running, so a provider that answers immediately and one
/// that answers in a second are the same to the caller.
class RendererExport RungProvider {
public:
    virtual ~RungProvider() = default;

    /// Begin acquiring the payload with this content key. Called only
    /// for keys a manifest already names.
    virtual void request(const std::string &key, uint32_t size) = 0;

    /// Requests begun and not yet answered.
    virtual size_t outstanding() const = 0;

    /// How many may be outstanding at once. A window is what makes an
    /// order an order: acquire everything the moment it is named and
    /// the ranking decides nothing, because it is all issued in the
    /// same tick and answered in whatever order it happens to finish.
    virtual size_t window() const = 0;

    /// The end of a round: issue whatever has been accumulated but not
    /// yet sent. A provider that batches must not wait for the next
    /// round to flush a partial batch — left to a timer that cost a
    /// 4.6 s load 58 s (§6 phase 4a).
    virtual void flush() {}

    /// Ask for a level that may not exist yet. False — the default —
    /// means this tier can only fetch what already exists, which is
    /// every provider until the producer-side work of §7 lands.
    virtual bool generate(const LevelRequest &request);

    /// The acquisition for \a key is no longer wanted — the camera
    /// moved, the rung was displaced, the publish it belonged to was
    /// superseded. Advisory, and a no-op by default: a provider whose
    /// acquisition is a download may let it complete and land through
    /// the ordinary path, because a fetch already in flight costs
    /// nothing to ignore. A provider whose acquisition is *work* — a
    /// tessellation at a deviation — should stop the job, because CPU
    /// spent on a level nobody wants anymore is not free the way an
    /// ignored payload is. Cancelling changes no bookkeeping: the chunk
    /// stays outstanding until its fill runs or the caller's timeout
    /// presumes it lost, so a provider that ignores this is merely
    /// slower, never wrong.
    ///
    /// Declared before any caller exists, deliberately: the seam is the
    /// part that fossilizes, and the desktop tier's cost model needs
    /// this expressible from its first implementation.
    virtual void cancel(const std::string &key) { (void)key; }
};

/// What the scene may hold, and how that number is arrived at on a
/// machine nobody measured.
///
/// The budget is stated in the **payload bytes every chunk already
/// declares** (SceneDump.h), because that is what is known before a
/// rung is acquired and what both tiers can rank by. What actually
/// runs a device out of memory is the expanded arrays those payloads
/// become, which is a different number by a factor nobody can name in
/// advance: it depends on the compression the wire happened to get and
/// on what the backend keeps beside each buffer.
///
/// So rather than assume that factor, this measures it. Both numbers
/// are already sampled on a heartbeat — what the scene says it holds
/// and what the heap actually is — and the ratio between them is the
/// expansion. A heap ceiling then converts into a payload budget by
/// dividing, and a device the author never saw gets a budget fitted to
/// what payloads cost *on it*.
///
/// This is deliberately not `navigator.deviceMemory` and friends. Those
/// exist on Chrome and Android, are absent on Safari — where the
/// ceiling matters most — and are rounded to powers of two and capped
/// at 8 GB besides. They are worth a starting guess and nothing more:
/// what a tab may hold is decided by the device, the other fifty tabs
/// and the OS, and the only honest way to learn it is to watch.
class RendererExport MemoryBudget {
public:
    /// The largest heap this process could ever have, as the platform
    /// states it: the wasm growth cap in the browser, physical memory
    /// on a desktop. 0 when nothing will say.
    static size_t systemMemory();

    /// A device hint where one exists (navigator.deviceMemory), in
    /// bytes. 0 when the browser does not answer or this is not one.
    static size_t deviceHint();

    /// Memory the system estimates it can still hand out without
    /// swapping (Linux MemAvailable, Windows available physical), in
    /// bytes; 0 when the platform will not say. The desktop refine
    /// worker's pre-emptive ceiling estimate (§13 step 3): a build
    /// started under a low number here is a bad_alloc that has not
    /// happened yet.
    static size_t availableMemory();

    /// Start adapting from a guess for this machine. \a explicitBytes
    /// non-zero pins the budget instead and stops all adaptation — the
    /// `?membudget=` case, where the point is to say what it is.
    void reset(size_t explicitBytes = 0);

    /// One heartbeat: \a payloadBytes is the resident geometry by the
    /// scene's own accounting, \a rawBytes everything else the stream
    /// is holding that the budget does not bound — the local payload
    /// cache — and \a heapBytes what the process actually occupies.
    ///
    /// \a rawBytes is separated out because it is held roughly one for
    /// one and would otherwise be charged to geometry as expansion.
    /// Measured on the 200-object scene, folding it in reported a
    /// factor of six and falling where the real steady-state ratio was
    /// about one: the estimate was tracking the download cache, and the
    /// budget it produced was wrong by that much.
    ///
    /// Cheap enough to call as often as the heap is sampled, and it has
    /// to be: the expansion is only learnable while the scene is
    /// growing, which is exactly when nothing wants to spend time
    /// measuring itself.
    void observe(size_t payloadBytes, size_t rawBytes, size_t heapBytes);

    /// A hard ceiling observation: the process just *failed* to obtain
    /// memory with the heap at \a heapBytes, so the wall is not where
    /// the platform said — it is here. The ceiling drops to a share
    /// below the observed wall (monotonically: a second observation can
    /// only lower it further) and the budget is cut against it at once,
    /// so eviction starts making room before the next allocation walks
    /// into the same wall.
    ///
    /// No caller exists in the wasm tier yet, honestly: with aborting
    /// malloc there is nothing left to call it from. The desktop tier
    /// runs with exceptions, where a caught bad_alloc is exactly this
    /// observation — which is why the seam is here and not in a
    /// platform file.
    void observeCeiling(size_t heapBytes);

    /// The budget as it currently stands, in payload bytes.
    size_t value() const { return m_budget; }

    /// Heap bytes observed per payload byte, as measured. 0 before
    /// enough of a scene has arrived to tell.
    float expansion() const { return m_expansion; }

    /// False once a budget has been pinned explicitly.
    bool adaptive() const { return !m_pinned; }

    /// The heap this budget is trying to keep the process under — what
    /// the adaptation is solving for, reported so that "the viewer is
    /// holding too much" and "something else is" stay distinguishable.
    size_t ceiling() const { return m_ceiling; }

private:
    size_t m_budget = 0;
    size_t m_ceiling = 0;
    /// The heap before the scene paid for anything: everything that is
    /// not geometry — the backend, the runtime, the canvas. Subtracted
    /// before dividing, or the expansion absorbs it and the budget
    /// shrinks on a machine that merely has a large baseline.
    size_t m_baseHeap = 0;
    /// The previous sample the slope is measured against: resident
    /// payload, and the heap net of the raw cache.
    size_t m_prevPayload = 0;
    size_t m_prevNet = 0;
    /// Decayed sums of payload growth and net heap growth — the slope
    /// is their ratio, so a heap that grows in slabs between samples
    /// averages out instead of biasing the estimate (see observe()).
    double m_sumPayload = 0.0;
    double m_sumNet = 0.0;
    float m_expansion = 0.0f;
    bool m_pinned = false;
    bool m_havePrev = false;
};

}  // namespace Render

#endif  // RENDERER_SCENE_LADDER_H
