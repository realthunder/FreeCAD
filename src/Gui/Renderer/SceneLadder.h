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
#include <deque>
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

/// How much of the screen the drawn objects actually cover
/// (docs/FarFieldProxies.md §9): the measurement that says whether
/// aggregating distant parts would pay, before any of it is built.
///
/// A part costs a full object — a cache entry, a draw entry, a material,
/// an identity — whether it fills the screen or four pixels of it, so
/// what matters is not the triangle count but how many objects the
/// camera cannot resolve. Buckets every drawn object by its projected
/// bounding-box diagonal, using the same projection the plan pass ranks
/// with, so the two agree by construction about what "small on screen"
/// means.
///
/// Off-screen draws are counted separately rather than as size 0: they
/// are the population a cut would stop touching altogether, and lumping
/// them in with the sub-pixel ones would overstate what the near field
/// has to aggregate. Matrices are GL-layout 4x4.
struct CoverageHistogram {
    /// Bucket upper bounds in pixels; the last bucket is everything above.
    static constexpr int kBuckets = 6;
    static constexpr float kEdges[kBuckets - 1] = {1.0f, 4.0f, 16.0f, 64.0f, 256.0f};
    int counts[kBuckets] {};
    int offScreen = 0;
    int noBounds = 0;
    int total = 0;
    /// Objects at or under \a px pixels, off-screen ones excluded.
    int atOrUnder(float px) const;
};
RendererExport CoverageHistogram coverageHistogram(const DrawCallList &draws,
                                                   const float *viewMatrix,
                                                   const float *projMatrix,
                                                   float viewportHeightPx);

/// A demotion only ever runs when the camera would not notice it by a
/// margin: the coarse rung must err at most this fraction of the
/// tolerance. Demoting at the refine boundary itself would make a
/// drifting camera trade a full tessellation back and forth across it
/// — the tier-cut hysteresis lesson (§7), with both directions costing
/// work here.
constexpr float kPlanDemoteMargin = 0.5f;

/// How far the climb is held back while a budget is being met, and --
/// the half that matters -- how that is given back
/// (docs/SceneStreaming.md sec 13c.3).
///
/// The desktop ladder is two incremental sweeps where the streamed
/// viewer has one global plan, and the streamed one is stable for a
/// reason worth quoting from planLevels: it is deterministic, so "a
/// plan cannot oscillate with itself". Two sweeps can, and these did.
/// Measured on the rack model at a 64 MB budget: 43 plans in 611 s,
/// 2567 objects boxed, no steady state at any point.
///
/// The mechanism was the RELEASE, not the descent. The worst error the
/// descent had to accept is held as a running maximum while the
/// pressure stands -- a fast attack, and right, since one plan must not
/// hand back what the last one just gave up -- but it was cleared the
/// instant a plan came in under budget. So the refine tolerance fell
/// from ~51 px to 2.00 px in ONE step, the next plan asked 946 objects
/// to refine, and the budget broke again: a control loop with gain and
/// no hysteresis on the release side.
///
/// So attack fast, release in steps, and REMEMBER WHAT FAILED. Each
/// release keeps `releaseFraction` of the raised error; a step that
/// brings the pressure straight back proves that level too generous and
/// raises a floor the release never passes again. The floor only rises,
/// so the tolerance walks down to the coarsest setting that actually
/// fits and stops there. That is an equilibrium the ladder LEARNS,
/// which is a different thing from a cycle it damps -- and it is why
/// this is not the hysteresis that was built for the occlusion tester
/// and did not help there.
///
/// Holding short of the camera's tolerance is not a failure: it is what
/// "the budget is met" costs. Releasing further is measured to break
/// it.
struct RendererExport PressureTolerance {
    /// Worst error the descent has had to accept in this spell of
    /// pressure, in pixels -- the attack half, and what the climb's
    /// tolerance is derived from. 0 = no pressure standing.
    float raisedPx = 0.0f;
    /// The lowest raised error a release has TRIED and been punished
    /// for, in pixels: releasing to it or below it is known to put the
    /// scene back over budget. Rises only, until forget().
    float floorPx = 0.0f;
    /// Whether the last update() actually gave something back. The
    /// caller must replan when it did: a still camera over a quiet
    /// scene raises no event of its own, so nothing else would take the
    /// next step of the staircase.
    bool releasing = false;
    /// Whether anything has been given back since the pressure last
    /// stood -- which is what makes a returning pressure ATTRIBUTABLE.
    /// Not the same as `releasing`, and the difference is the whole
    /// point: a refine asked for by a release step lands plans later,
    /// so the pressure it causes usually arrives after the staircase
    /// has already stopped. Reading `releasing` there would learn
    /// nothing and the same level would be tried forever.
    bool gaveBack = false;
    /// The level the last release step actually stood at, in pixels --
    /// kept because the step that gives the LAST of it back sets
    /// raisedPx to 0, and a pressure blaming that step must not learn a
    /// floor of nothing.
    float lastStepPx = 0.0f;

    /// The evidence no longer applies. The floor is learned about ONE
    /// camera and ONE budget, and what a rung costs on screen is
    /// exactly what changes when either moves.
    void forget() { floorPx = 0.0f; }

    /// One plan's update; returns the tolerance the CLIMB should run at
    /// (the descent always runs at the camera's own -- feeding the
    /// raised value into the free tier runs away: a wider tier accepts
    /// more error, which widens the tier).
    ///
    /// \a acceptedPx is the worst projected error the descent accepted
    /// this plan (PlanDemoteStats::acceptedErrorPx). Non-finite is not
    /// a measurement and is ignored: one such candidate would pin the
    /// running maximum at infinity, and an infinite refine tolerance is
    /// not a large one -- it is the climb switched OFF, since
    /// `levelError * diagPx > tolerancePx` is then false for every
    /// source in the scene. Seen for real, on 9 of 30 plans.
    ///
    /// \a viewportPx caps it for the same reason at the other end: an
    /// error of a million pixels and an error of the screen height are
    /// the same statement -- the object is not resolvable -- and there
    /// is no rung beyond "already invisible", so admitting the larger
    /// number only destroys the tolerance it is about to become.
    /// Measured before this cap: 4.3e11 px.
    ///
    /// \a releaseFraction 0 or less restores the old immediate snap
    /// exactly, floor and all, so the defect this fixes stays
    /// reachable for a measurement.
    float update(bool underPressure, float acceptedPx, float cameraTolPx,
                 float viewportPx, float releaseFraction);
};

/// Why the plan refused, when it refuses everything. A budget that
/// cannot be honoured looks identical to a budget nobody read, and the
/// two want opposite fixes: `noRung` is plumbing (nothing to fall back
/// to), `tooBig` is policy (the coarse rung would show, and the free
/// tier will not accept visible error to save memory).
///
/// `considered` counts SOURCES, one per distinct tag, which is what its
/// name always claimed: the first version counted a source once per
/// draw carrying it and reported 6287 where 2482 sources stood.
struct PlanDemoteStats {
    uint32_t considered = 0;   ///< exact-resident sources examined
    uint32_t noRung = 0;       ///< registered, but armed no way down
    /// Drawn, but its source is not in the registry at all (the error
    /// callback answered kTagUnknown). Not a missing rung -- a missing
    /// owner, and no generation work would ever reach it.
    uint32_t unregistered = 0;
    /// Resident bytes behind noRung + unregistered together: what the
    /// ladder cannot reach at any pressure. The count says how many
    /// sources are out of reach, this says whether reaching them would
    /// be worth the work.
    uint64_t unreachableBytes = 0;
    /// The unregistered share of unreachableBytes alone. The two
    /// populations under the total want opposite fixes -- a source at
    /// its true bottom is DONE, a tag nobody owns is a defect -- and a
    /// combined figure once priced the defect at whatever the settled
    /// scene happened to weigh.
    uint64_t unregisteredBytes = 0;
    uint32_t tooBig = 0;       ///< over the margin, and pressure never reached it
    uint32_t offscreen = 0;    ///< free outright
    /// In the frustum but proven occluded (the caller's \a hiddenOf):
    /// free outright, like offscreen, and for the same reason -- no
    /// pixel of it reaches the screen -- but counted apart because the
    /// two verdicts come from different mechanisms and fail differently.
    uint32_t occludedFree = 0;
    uint32_t eligible = 0;     ///< on screen and under the margin
    uint32_t underPressure = 0;///< over the margin, taken because the deficit demanded it
    uint32_t unpriceable = 0;  ///< no judgeable bounds, or the camera inside the box
    /// Candidates the per-plan order cap (\a maxOrders) deferred to a
    /// later pass. Not refused -- their hooks stand and the replan
    /// re-finds them; reported so a capped pass never reads as
    /// "covered everything".
    uint32_t deferredByCap = 0;
    /// Bytes the selection gives back, in whatever currency the caller
    /// priced it in (planMeshDemotes' \a bytesOf) -- per distinct mesh,
    /// so an instanced source is not counted once per instance.
    uint64_t bytesFreed = 0;
    /// The worst projected coarse error accepted, in pixels: the
    /// tolerance this plan effectively ran at. Equal to
    /// kPlanDemoteMargin x tolerancePx or below while no deficit
    /// stands, and above it by exactly as much as the deficit forced.
    float acceptedErrorPx = 0.0f;
};

/// The downgrade sweep's memory of its own unlanded orders -- what
/// keeps the plan from storming (docs/SceneStreaming.md sec 13c.4).
///
/// The sweep's pricing is exact: it knows to the byte what each drop
/// frees, and the readout shows it covering its deficit precisely.
/// What it cannot know is WHEN the meter will admit it. A drop applies
/// as a rebuild that uploads the coarse rung immediately, while the
/// fine buffers it replaced stay in `live` until they have gone
/// undrawn for the collection window -- and on a heavy scene that
/// window is SECONDS of wall time, longer than the plan cadence. So
/// the next plan, sampling mid-transition, reads old+new double
/// residency, computes a LARGER deficit than the one just covered,
/// and -- the previous sources' hooks being consumed -- walks other
/// sources another rung down. Measured on the rack model at 64MB with
/// the camera inside: single plans requesting 1500+ downgrades,
/// live TRIPLING during the storm, the registry drained to
/// `no fallback rung` 1700 while the true settled memory was 45MB --
/// far UNDER the budget the storm was still chasing.
///
/// The ledger closes the loop: a sweep's promised bytes are carried as
/// credit against the next deficits until they are OBSERVED landing
/// (the live meter falling since the order -- each fall credited only
/// once) or written off after kSettleFrames rendered frames (frames,
/// not seconds, because collection is frame-clocked). A deficit fully
/// covered by outstanding credit holds the sweep entirely: the orders
/// already in flight are the correction, and re-correcting off their
/// own transient is the storm. Credit that can never land -- shared
/// geometry pinned by sources the sweep cannot reach -- expires, and
/// the truth returns within the window.
///
/// The frame window is a SETTLE margin, not the expiry itself: an
/// order now lands as a CHAIN of jobs (the downgrade's hook body
/// queues a worker build, whose landing queues a pooled visual fill,
/// whose apply is when the bytes actually move -- sec 13c), and its
/// bytes cannot fall before that chain drains -- which on a loaded
/// pool is far past any frame count. So the credit stands until the
/// order's own DESCENT GENERATION has drained (the producer counts
/// every job the chain queues under the generation the sweep opened;
/// MeshSourceRegistry::descentGenerationSettled says when the last
/// one settled -- landed, refused, or purged), and only then does the
/// kSettleFrames clock start on the remainder. Not "while any descent
/// job is in flight": a busy ladder keeps some job queued for minutes
/// on end, credit held on that never expired, and the phantom
/// promises of orders that freed nothing (a decimation that came back
/// "spent" keeps its mesh but was priced in full) accumulated until
/// the sweep crawled 2MB-deficits against 33MB of standing excess.
/// Expiring on frames alone was the opposite failure: the 418-order
/// burst re-ordered the same memory from other sources the moment its
/// 6 frames ran out. And a target on the AGGREGATE settle counter --
/// "this order's jobs are done once the counter advances by the drop
/// count" -- was met by the FIRST settles of every chain (the hook
/// bodies, seconds before the fills), wrote the credit off early, and
/// the re-orders overshot a 64MB budget down to 44.7MB settled.
struct DowngradeLedger {
    /// One sweep's promise, expiring on ITS OWN terms. An aggregate
    /// promise cannot: while small follow-up orders keep flowing --
    /// and a converging ladder orders every plan -- any shared horizon
    /// is perpetually re-stamped, and the phantom credit of drops that
    /// freed nothing (a "spent" decimation keeps its mesh; the second
    /// tag of a shared pair is a structural no-op) accumulates until
    /// the sweep crawls 2MB-deficits against 50MB of standing excess.
    /// This is the "queue for downgrade" design stated plainly: each
    /// order is retired by observed landings (oldest first) or written
    /// off alone, once its own jobs have settled and its grace passed.
    struct Order {
        uint64_t bytes = 0;         ///< promise still unlanded
        uint64_t frame = 0;         ///< rendered-frame stamp at order
        uint64_t gen = 0;           ///< the sweep's descent generation:
                                    ///< drained means "this order's
                                    ///< chain of jobs is done"
    };
    std::deque<Order> orders;
    /// The live meter the newest order was judged against; only falls
    /// below this observe landings, and it ratchets down with them so
    /// a fall is never credited twice.
    uint64_t liveAtOrder = 0;
    /// Write-off horizon AFTER an order's jobs settled: the 2-frame
    /// collection window, plus headroom for the landings to run
    /// between frames on the same thread.
    static constexpr uint64_t kSettleFrames = 6;

    /// Bytes still promised, for the readout.
    uint64_t promised() const
    {
        uint64_t sum = 0;
        for (const Order &o : orders)
            sum += o.bytes;
        return sum;
    }

    /// Outstanding credit, after observing \a liveNow: falls since the
    /// newest order retire promises oldest-first; an order whose
    /// generation has drained (\a genSettled, normally
    /// MeshSourceRegistry::descentGenerationSettled) and whose grace
    /// frames have passed writes off what it still holds -- alone,
    /// however many newer orders stand behind it. No predicate reads
    /// every generation as drained (frame-only expiry).
    uint64_t outstanding(uint64_t liveNow, uint64_t frameNow,
                         const std::function<bool(uint64_t)> &genSettled
                             = {})
    {
        // Landings first: a fall is real memory and must retire
        // promises before any write-off invents a deficit.
        uint64_t observed =
            liveAtOrder > liveNow ? liveAtOrder - liveNow : 0;
        if (observed) {
            liveAtOrder = liveNow;
            while (observed && !orders.empty()) {
                Order &o = orders.front();
                const uint64_t take = observed < o.bytes ? observed
                                                         : o.bytes;
                o.bytes -= take;
                observed -= take;
                if (!o.bytes)
                    orders.pop_front();
            }
        }
        // Expiry is per order: its own chain drained, its own grace
        // out. An order still being worked holds ITS horizon open (the
        // restamp), so the grace effectively starts when its last job
        // settles -- and a newer order's arrival changes nothing for
        // an older one.
        for (auto it = orders.begin(); it != orders.end();) {
            if (genSettled && !genSettled(it->gen)) {
                it->frame = frameNow;
                ++it;
            }
            else if (frameNow >= it->frame + kSettleFrames) {
                it = orders.erase(it);
            }
            else {
                ++it;
            }
        }
        return promised();
    }

    /// The deficit the next sweep should act on: the raw excess minus
    /// what is already in flight. 0 holds the sweep this plan.
    uint64_t deficit(uint64_t liveNow, uint64_t budget, uint64_t frameNow,
                     const std::function<bool(uint64_t)> &genSettled = {})
    {
        const uint64_t raw = liveNow > budget ? liveNow - budget : 0;
        const uint64_t credit = outstanding(liveNow, frameNow, genSettled);
        return raw > credit ? raw - credit : 0;
    }

    /// Record a sweep's order: its PlanDemoteStats::bytesFreed and the
    /// descent generation the sweep opened around its requests --
    /// every job the order's chains queue counts under it, so the
    /// write-off horizon holds exactly while the order is still being
    /// worked.
    void order(uint64_t bytes, uint64_t liveNow, uint64_t frameNow,
               uint64_t gen = 0)
    {
        if (!bytes)
            return;
        orders.push_back({bytes, frameNow, gen});
        liveAtOrder = liveNow;
    }
};

/// The way back down (sec 13 step 3), pure policy: among \a draws, the
/// *exact*-resident sources (levelError 0) whose coarse rung -- its
/// error answered by \a demoteErrOf, 0 = not demotable -- the camera can
/// be given without missing it. Only consulted under an observed memory
/// ceiling: without one the desktop keeps every rung it built ("keep
/// both"), and a non-positive tolerance demotes nothing -- everything
/// desires exact.
///
/// Two tiers, and the second is what makes a budget honourable:
///
/// 1. **Free** -- off screen or wholly behind the camera, or a coarse
///    rung erring at most kPlanDemoteMargin x \a tolerancePx on screen.
///    Always taken; nothing visible is traded.
/// 2. **Priced** -- over that margin, so demoting it *would* show.
///    Admitted only while \a deficitBytes of the pressure that asked
///    for this pass still stands, cheapest projected error first, and
///    stopping the moment the deficit is covered.
///
/// Tier 2 exists because tier 1 alone cannot honour anything: measured
/// on the rack model with 64 MB pinned against 517 MB in use, every one
/// of the 376 sources that carried a fallback rung was refused "on
/// screen and too big", so the plan reported a budget it had no way to
/// meet. A policy that will never trade visible error for memory turns
/// a model too large to display exactly into one that cannot be
/// displayed at all -- where showing it coarse is better than nothing.
///
/// So the effective tolerance is an *outcome* here, not a knob: pressure
/// raises it, and only as far as the deficit reaches (reported as
/// PlanDemoteStats::acceptedErrorPx). \a deficitBytes 0 -- no pressure,
/// or none that can be quantified -- leaves the original free-tier-only
/// behaviour exactly as it was.
///
/// A source is priced by its NEEDIEST owner (the largest projected
/// error over the draws sharing its tag), the mirror of the refine
/// pass's union: one instance close to the camera makes the shared
/// source expensive for every other. It is still a price and not a
/// veto, which is the difference from the free tier, where any owner
/// over the margin kept the source exact outright.
///
/// \a bytesOf is the CURRENCY, and it must be the same one \a
/// deficitBytes is quoted in. Default (empty) is meshResidentBytes --
/// the CPU arrays a mesh occupies -- which is right for the sweep run
/// against a CPU memory ceiling and wrong for the one run against the
/// GPU budget: the same line segment is 8 bytes of index on the CPU and
/// 64 bytes of quad-expansion instance data on the GPU, plus the index
/// buffer uploaded beside it. A sweep that spends GPU bytes while
/// counting CPU bytes reports a deficit covered and leaves the budget
/// standing, which is what one number pretending to be two costs.
///
/// It is called at most ONCE PER DISTINCT MESH per pass, so a stateful
/// implementation may charge a shared upload to the first candidate
/// that reaches it and answer 0 for the rest. Whoever carries it is
/// arbitrary; what matters is that the total over the selection is what
/// dropping all of it actually frees, and never more.
///
/// \a hiddenOf, when given, answers whether a SOURCE is proven to reach
/// no pixel at all -- occlusion's verdict, folded per tag by the caller
/// (docs/FarFieldProxies.md sec 10: "the gain from occlusion is not
/// only about speed, but also gpu memory"). A hidden source joins the
/// free tier beside the offscreen ones: an enclosed chassis's interior
/// is IN the frustum, so the box test above prices its demotion as
/// visible error the camera literally cannot see, and under a budget
/// that error is paid in quality somewhere visible instead. Its errPx
/// does not enter acceptedErrorPx -- an error nobody can see must not
/// raise the tolerance the climb runs at. The verdict must be
/// conservative and hysteresed by the caller (frames-hidden streak):
/// this pass acts on it without judgement, and a flapping verdict here
/// is an upload/rebuild per flap.
///
/// \a maxOrders, when non-zero, caps how many drops one pass returns
/// (free tier and priced tier together). Each order is now a worker
/// job whose enqueue costs the GUI thread a snapshot, so an unbounded
/// pass -- the measured 1500-order plans -- is itself a stall; the
/// deferred candidates keep their hooks and the replan that follows
/// the landed batch re-finds them. stats->bytesFreed counts only what
/// was ordered, so the ledger's credit stays honest under the cap.
RendererExport std::vector<const void *> planMeshDemotes(
    const DrawCallList &draws, const float *viewMatrix,
    const float *projMatrix, float viewportHeightPx, float tolerancePx,
    const std::function<float(const void *)> &demoteErrOf,
    PlanDemoteStats *stats = nullptr, size_t deficitBytes = 0,
    const std::function<uint64_t(const MeshData *)> &bytesOf = {},
    const std::function<bool(const void *)> &hiddenOf = {},
    size_t maxOrders = 0);

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
