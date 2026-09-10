/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
 *                                                                          *
 ****************************************************************************/

#include "BGFXRendererP.h"

/// Radical inverse of \a index in \a base -- the Halton sequence, used
/// for the idle accumulation's subpixel offsets. Two coprime bases give
/// a 2D sequence that fills the pixel evenly at EVERY prefix length,
/// which is what lets the accumulation look converged early and still
/// keep improving; a plain grid only looks right at its own count, and
/// random offsets clump. Indexed, so sample N is the same offset every
/// time this runs.
static float haltonInverse(int index, int base)
{
    float f = 1.0f;
    float r = 0.0f;
    for (int i = index; i > 0; i /= base) {
        f /= float(base);
        r += f * float(i % base);
    }
    return r;
}

bool BGFXRenderer::Private::render(const QColor &col,
            const void * viewMatrix,
            const void * projMatrix)
{
    // A publish-only renderer has no view, no target and no device
    // (docs/HeadlessServe.md §3.1). Nothing below can run, and
    // getView() would try to create the very thing this mode
    // exists to avoid.
    if (publishOnly)
        return false;

    // Checkpoint for the pre-submit half of the frame's CPU. Taken
    // after the publish-only bail so it only ever covers a frame
    // that really renders.
    renderInnerT0 = bx::getHPCounter();

    // The pending scene data (whatever its age) is consumed by this
    // frame; needsRedraw() reports false until new data arrives.
    // Two distinct "dirty" signals:
    //  - feedChanged (setScene only) gates the STANDALONE warmup target
    //    rebuild, so cheap per-frame state (AO toggle on drag, selection/
    //    preselect highlight) does NOT force a view->init() stall.
    //  - dirtyChanged (any change, incl. selection/highlight/config) gates
    //    the serve republish, so a click-selection round trip still
    //    streams to the viewer.
    const bool feedChanged = feedDirty;
    bool dirtyChanged = sceneDirty;
#ifndef FC_RENDERER_STANDALONE
    // A finished async user-shader compile is a change too: the
    // frame caches must refresh and — with the scene server up —
    // the snapshot must republish so viewers receive the freshly
    // compiled binaries (userShaderGen re-snapshots below, in the
    // post-pass setup).
    if (userShaderGen != _BGFXLib.userCompileGeneration)
        dirtyChanged = true;
    // The stand-in record is per frame: the first submit of a sub-view
    // sequence starts it, the later ones add to it, the tail reads it.
    if (!subCtx.active || subCtx.first) {
        _BGFXLib.userProgramStoodIn = false;
        frameOwes = false;
    }
#endif
    (void)feedChanged;
    feedDirty = false;
    sceneDirty = false;
    renderOk = false;

    // Coin hands us a GL projection: it clips depth against [-1,1].
    // Every other bgfx backend clips against [0,1], and the rest of
    // the engine already assumes the matrix it is given matches
    // caps->homogeneousDepth -- the shadow crop below, the proxy
    // hierarchy, the masked and query cullers all branch on it. The
    // camera projection was the one matrix that reached bgfx
    // unconverted, so off GL the far half of every scene fell outside
    // the clipper and simply vanished.
    //
    // The remap is z -> (z + w) / 2, i.e. only the z row (indices
    // 2/6/10/14 of the column-major matrix) changes. The w row is left
    // alone on purpose: its z entry is how every shader tells a
    // perspective camera from an orthographic one, and no shader reads
    // the z row at all, so this stays confined to the clip depth.
    //
    // projMatrixFed keeps the matrix as Coin gave it, for the one
    // consumer that must not see the local backend's convention: the
    // scene publish/dump, whose viewer renders on a backend of its own
    // and builds its camera to suit.
    const void *projMatrixFed = projMatrix;
    if (projMatrix) {
        const bgfx::Caps *caps = bgfx::getCaps();
        if (caps && !caps->homogeneousDepth) {
            const float *fed = reinterpret_cast<const float *>(projMatrix);
            std::memcpy(projClip, fed, sizeof(projClip));
            for (int c = 0; c < 4; ++c)
                projClip[4 * c + 2] = 0.5f * (fed[4 * c + 2]
                                              + fed[4 * c + 3]);
            projMatrix = projClip;
        }
    }

    // The camera the shadow ground sizes itself to
    // (LightConfig::groundFollowCamera). Taken here, at the top, for
    // two reasons: the ground is laid out well before the frame stores
    // its matrices for the draw path, and boundBox() -- asked outside
    // any frame -- has to build the same quad this frame does.
    if (viewMatrix && projMatrix) {
        const float *V = reinterpret_cast<const float *>(viewMatrix);
        const float *P = reinterpret_cast<const float *>(projMatrix);
        // The eye in world space: the view matrix is rigid, so its
        // inverse translation is -R^T t.
        for (int c = 0; c < 3; ++c) {
            groundCam.pos[c] = -(V[4 * c] * V[12]
                               + V[4 * c + 1] * V[13]
                               + V[4 * c + 2] * V[14]);
        }
        // The view's -Z in world space: the rotation is orthonormal, so
        // its transpose maps the view axis back out.
        groundCam.dir[0] = -V[2];
        groundCam.dir[1] = -V[6];
        groundCam.dir[2] = -V[10];
        // proj[1][1] and the perspective test, in the fed matrices'
        // column-major layout (the same test fc_prepass_read.sh makes
        // in the shaders).
        groundCam.projY = P[5];
        groundCam.perspective = P[11] != 0.0f;
        groundCam.valid = true;
    }

    if (_deinit)
        return false;

#ifndef FC_RENDERER_STANDALONE
    // Whatever framebuffer the caller asked for this frame: the widget's
    // own on screen, a capture target for a screenshot. Every exit that
    // touches the context has to leave that one bound, because
    // QOpenGLWidget::makeCurrent() binds the WIDGET's instead -- and
    // then the Coin traversal that a failed frame falls back to draws to
    // the screen while the capture it was asked for stays empty. That is
    // what made every screenshot black while the window looked right,
    // whenever the backend was attached but could not draw.
    GLint hostFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &hostFbo);
    auto bailToHost = [this, hostFbo]() {
        widget->makeCurrent();
        if (auto *ctx = QOpenGLContext::currentContext())
            ctx->extraFunctions()->glBindFramebuffer(GL_FRAMEBUFFER,
                                                     GLuint(hostFbo));
        return false;
    };
#endif

    // getView() prepares the device, and its own failures can leave the
    // backend's context current rather than the caller's.
    auto view = _BGFXLib.getView(widget, type);
    if (!view)
#ifndef FC_RENDERER_STANDALONE
        return bailToHost();
#else
        return false;
#endif

    // Split-view frame: swap in the sub-view's state bank
    // (docs/SplitViews.md sec 9.2, sec 13 for the desktop tier). A
    // plain render() is bank 0 -- the full-canvas sub-view -- which
    // also puts the members back after a renderSubViews sequence
    // ended on another bank.
    view->selectSubView(subCtx.active ? subCtx.id : 0);
    // ...and its display style, which unlike the bank is restated
    // rather than carried: the cell owns it, the backend only filters
    // by it (docs/CoinRetirement.md 5.7). A plain render() takes the
    // main-view context stated through setMainViewStyle() -- at rest
    // (StyleAsIs, no superset) except on a view whose per-object
    // override table is non-empty (5.9).
    view->drawStyleMask = subCtx.active ? subCtx.style : mainStyleMask;
    view->drawStyleName = subCtx.active ? subCtx.styleName : mainStyleName;
    view->styleFromSuperset = subCtx.active ? subCtx.fromSuperset
                                            : mainFromSuperset;
    // ...and, when the capture carried this style's mode additively,
    // the id and bit that let the style be resolved from the mode's
    // own tagged draws (docs/CoinRetirement.md 5.11). Both stay zero
    // when the interest list does not carry the mode -- it was never
    // captured, so the mask over the superset child is still the only
    // answer there is.
    view->drawStyleMode = 0;
    view->drawStyleModeBit = 0;
    if (view->styleFromSuperset && captureInterest) {
        const uint16_t sm = subCtx.active ? subCtx.styleMode : mainStyleMode;
        if (const uint16_t bit = sm ? captureInterest->bitOf(sm) : 0) {
            view->drawStyleMode = sm;
            view->drawStyleModeBit = bit;
        }
    }
    // The sub-view's per-object override table (5.9), and its resolved
    // objectKey cache: cleared when the table's content version or the
    // stated object-info version moved, filled lazily at submit
    // (BGFXView::lookupStyleOverride) so keys added by
    // updateObjectInfo -- which deliberately does not bump the info
    // version -- resolve on first sight.
    const Render::StyleOverrideTable *ovt =
        subCtx.active ? subCtx.styleOverrides : mainStyleOverrides;
    if (ovt && !ovt->entries.empty()) {
        const uint32_t interestVersion =
                captureInterest ? captureInterest->version : 0;
        auto &c = view->subOvCaches[subCtx.active ? subCtx.id : 0];
        if (c.tableVersion != ovt->version
                || c.infoVersion != objectInfoStamp
                || c.interestVersion != interestVersion) {
            c.map.clear();
            c.tableVersion = ovt->version;
            c.infoVersion = objectInfoStamp;
            c.interestVersion = interestVersion;
        }
        view->ovCache = &c;
        view->ovTable = ovt;
        view->ovInfo = &objectInfo;
    } else {
        view->ovCache = nullptr;
        view->ovTable = nullptr;
        view->ovInfo = nullptr;
    }
    // Latched whether or not there is an override table: since 5.11 the
    // sub-view's own STYLE can resolve through the interest list too,
    // and lookupStyleOverride guards on ovCache/ovTable of its own.
    view->ovInterest = captureInterest;
    // ...and the frame consumer registered under the sub-view, if
    // any, with whether that consumer supplies the sub-view's shaded
    // image (docs/CyclesIntegration.md sec 5.11). Resolved per submit
    // so a path-traced cell and a rasterized one share the one frame.
    selectConsumer(subCtx.active ? subCtx.id : 0);

    // A shader pack that could not supply a core program keeps the
    // view down: without this the torn-down view (no framebuffer)
    // matches the re-init condition below and every frame would
    // reload the whole broken pack. A reloadShaders() moves the
    // generation and re-arms the attempt.
    if (view->shaderFailed && view->shaderGen == _BGFXLib.shaderGeneration) {
        if (view->shaderFailedDrain) {
            // The failed init() queued a full view's worth of create
            // and destroy commands that no frame has executed. Drain
            // them once, in the context dance a normal frame uses, so
            // the abandoned GPU resources are actually released and
            // bgfx is left idle rather than mid-command-buffer.
            view->shaderFailedDrain = false;
#ifdef FC_RENDERER_STANDALONE
            bgfx::frame();
#else
            widget->doneCurrent();
            _BGFXLib.makeCurrent();
            bgfx::frame();
#endif
        }
#ifndef FC_RENDERER_STANDALONE
        // Hand the context back the way the fb-invalid bail does, so
        // Coin draws the frame on Qt's context -- and into the
        // framebuffer the caller asked for.
        return bailToHost();
#else
        return false;
#endif
    }

#ifdef FC_RENDERER_STANDALONE
    // Warmup rebuild: a few frames after the scene first appears,
    // force a single target re-create to clear the bad
    // first-target overlay artifact (see BGFXView::warmup).
    //
    // **Once per view, not once per feed change.** It was written
    // when a streamed scene was set exactly once, so re-arming on
    // every feed change cost one rebuild per load. A progressively
    // assembled scene sets the feed on every arrival
    // (docs/SceneStreaming.md §6), and a rebuild re-creates every
    // target the view owns — the MSAA scene buffer, the AO
    // pyramid, the shadow maps — so the same rule turned into
    // tens of full pipeline re-creates during a load, each one a
    // stall on the frame that was supposed to be showing progress.
    //
    // What the artifact needs is one rebuild once bgfx's async
    // WebGL2 init has settled, and a scene that has been on screen
    // for a few frames is exactly that. It cannot be the very
    // first frame with a scene, which is what "too early to
    // matter" meant for the bundled snapshot.
    bool warmupReinit = false;
    if (view->warmup >= 0 && !scene.empty() && ++view->warmup >= 3) {
        warmupReinit = true;
        view->warmup = -1;  // fired; never again for this view
    }
    // As on the desktop path: the warmup rebuild and every size or
    // scale change want the sized targets back, not a relink and not a
    // re-upload. Relinking on WebGL2 is just as slow as it is on a
    // native driver, and re-streaming the scene there is worse.
    const bool progChanged =
        _BGFXLib.effectiveSamples(_BGFXLib.standaloneSamples)
            != view->msaaSamples
        || _BGFXLib.shaderGeneration != view->shaderGen;
    // What the scene colour's format follows. Off the frame's config
    // rather than view->outputTransform, which a debug view mode zeroes
    // -- looking at the depth buffer must not reallocate the targets.
    view->hdrWanted = outconf.transform != Render::OutputConfig::None;
    // What the view's targets should measure: the sub-view rect while
    // a renderSubViews submit is active, the canvas otherwise. The
    // backbuffer itself always tracks the canvas -- the two sizes are
    // one and the same only in the plain single-view case.
    if (_BGFXLib.standaloneWidth != _BGFXLib.resetWidth
            || _BGFXLib.standaloneHeight != _BGFXLib.resetHeight) {
        bgfx::reset(_BGFXLib.standaloneWidth,
                    _BGFXLib.standaloneHeight, bgfxResetFlags());
        _BGFXLib.resetWidth = _BGFXLib.standaloneWidth;
        _BGFXLib.resetHeight = _BGFXLib.standaloneHeight;
    }
    if (progChanged
            || _BGFXLib.viewTargetWidth() != view->width
            || _BGFXLib.viewTargetHeight() != view->height
            // The lost-framebuffer case rebuilds once, not every
            // frame, exactly as on the desktop side below: without
            // this a bank whose init failed once stayed torn down
            // (and its sub-view black) for good.
            || (!bgfx::isValid(view->bgfxFbo) && !view->targetsFailed)
            || _BGFXLib.effectResolution != view->effectScale
            || _BGFXLib.ssaoResolution != view->ssaoScale
            || view->hdrScene != view->hdrSceneWanted()
            || warmupReinit) {
        view->init(!progChanged);
    }

    if (!bgfx::isValid(view->bgfxFbo)
            && (!subCtx.active || subCtx.warm)) {
        // The build found the handle pool exhausted: the creates
        // stacked on destroys this un-flushed frame has queued but
        // not reclaimed (a layout change frees old-size targets and
        // dropped banks in one burst). Nothing is queued at this
        // point -- a plain render is the frame's only producer, and a
        // warm submit (prepareSubViews) runs between frames -- so a
        // frame boundary here commits only that backlog. Cross it,
        // and retry the build once in-frame: the one-black-frame
        // retry becomes invisible. Submits of a renderSubViews
        // sequence keep the bail-and-heal-next-frame path instead; a
        // boundary there would commit the queued page-cell and
        // sibling passes and drop their rects from this frame's
        // composite.
        bgfx::frame();
        view->targetsFailed = false;
        view->init(true);
    }
    if (!bgfx::isValid(view->bgfxFbo)) {
        RENDER_ERR("bgfx: sub-view " << subCtx.id
                   << " frame bailed: no scene framebuffer (targetsFailed="
                   << int(view->targetsFailed) << ")");
        return false;
    }
    if (subCtx.warm) {
        // A warm-up submit (prepareSubViews): the point was building
        // the fresh bank's targets ahead of the real sequence, and
        // they are built. Nothing is drawn; the caller crosses the
        // frame boundary that realizes the creates.
        return true;
    }
#else
    // Only a shader-generation or MSAA change actually invalidates the
    // programs (MSAA also re-decides m_oit, i.e. which programs exist).
    // A resize or effect-scale change rebuilds the sized targets and
    // nothing else: it must not relink — the driver's shader compiler
    // takes seconds to do it, Intel's GL JIT ~5 s for this program set,
    // long enough to freeze the UI through a dock-splitter drag — and it
    // must not drop the uploaded scene either, which only bought a full
    // re-upload on the next frame.
    const bool progChanged =
        _BGFXLib.shaderGeneration != view->shaderGen
        || (_BGFXLib.desktopSamples >= 0
            && _BGFXLib.effectiveSamples(_BGFXLib.desktopSamples)
                != view->msaaSamples);
    // The lost-framebuffer case rebuilds once, not every frame:
    // view->targetsFailed says the last attempt found the handle pool
    // full, and a bailed frame never reaches bgfx::frame(), which is
    // the only place bgfx reclaims what the attempt destroyed.
    // As above: the scene colour's format is part of what the sized
    // targets ARE, so changing it is a rebuild like a resize.
    view->hdrWanted = outconf.transform != Render::OutputConfig::None;
    if (progChanged
            || _BGFXLib.viewWidth(widget) != int(view->width)
            || _BGFXLib.viewHeight(widget) != int(view->height)
            || (!bgfx::isValid(view->bgfxFbo) && !view->targetsFailed)
            || _BGFXLib.effectResolution != view->effectScale
            || _BGFXLib.ssaoResolution != view->ssaoScale
            || view->hdrScene != view->hdrSceneWanted())
        view->init(!progChanged);

    if (!bgfx::isValid(view->bgfxFbo))
        return bailToHost();
#endif

    uint16_t width = view->width;
    uint16_t height = view->height;
    // The background's own alpha, not a forced opaque: a screenshot
    // asked for with a transparent background hands one in, and the
    // capture is read back straight out of this target -- geometry
    // writes its material alpha over it, the background keeps the
    // alpha it was given. On screen the value is moot (the widget
    // composites opaque; the plain GL path clears alpha 0 there).
    // A flat background is a hardware CLEAR, not a draw, so nothing
    // downstream would decode it -- and the present pass encodes it
    // like everything else. Decode it here so the colour a person
    // picked is the colour that comes out.
    auto clearChannel = [&](int v) {
        float c = float(v) / 255.0f;
        if (view->colorManaged())
            c = decodeSRGB(c);
        return uint32_t(bx::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
    };
    uint32_t clearColor = (clearChannel(col.red()) << 24)
        | (clearChannel(col.green()) << 16)
        | (clearChannel(col.blue()) << 8)
        | uint32_t(col.alpha());
    if (getenv("FC_BGFX_DEBUG_CLEAR"))
        clearColor = 0xff0000ff;

    maybeDumpScene(viewMatrix, projMatrixFed, width, height, clearColor,
                   dirtyChanged);

#ifndef FC_RENDERER_STANDALONE
    // Desktop level plan (§13 step 2): feed the settle detector
    // this frame's camera; ~300ms after it stops somewhere new the
    // callback asks the registry to refine every coarse-first
    // source erring more than the tolerance on screen. A serving
    // process's own window never plans — its viewers' cameras
    // decide, and the registry arms no refine there anyway.
    if (!getenv("FC_BGFX_SERVE_SCENE") && viewMatrix && projMatrix) {
        // A new memory-ceiling observation replans promptly — and
        // stickily: from the first one on, every plan also demotes
        // what the camera would not miss (§13 step 3).
        const uint64_t ceiling = Render::MeshSourceRegistry::
            instance().memoryCeilingEpoch();
        if (ceiling != levelCeilingSeen) {
            levelCeilingSeen = ceiling;
            levelPlanner.markDirty();
        }
        // Crossing the GPU budget wakes the planner too (§13
        // step 3): the sweep itself runs in the plan callback.
        //
        // The wake reads the whole-accounting total rather than the
        // live half the sweep will judge against: it is one atomic
        // load against a walk of every cache entry, this runs on
        // every frame where the sweep runs on a camera settle, and
        // total >= live -- so the cheap number wakes the planner at
        // least as often as the exact one would, and the plan then
        // decides on the exact one.
        if (const size_t budget = gpuBudgetBytes()) {
            const bool over = gpuUsedBytes() > budget;
            if (over && !gpuOverBudget)
                levelPlanner.markDirty();
            gpuOverBudget = over;
        }
        levelPlanner.observe(
            reinterpret_cast<const float *>(viewMatrix),
            reinterpret_cast<const float *>(projMatrix),
            [this]() {
                // Fired by the planner's own timer 300 ms after the
                // frame that observed the camera -- which can be
                // after the LAST view's release shut bgfx down (the
                // icon generator's exit crashed here every run:
                // gpuBudgetBytes() -> bgfx::getStats() on a dead
                // library). A device that is gone has no budget.
                if (!_BGFXLib.deviceUp())
                    return;
                const float h = float(widget->height()
                                      * widget->devicePixelRatioF());
                auto &reg = Render::MeshSourceRegistry::instance();
                size_t nDemote = 0, nDowngrade = 0;
                // THE TWO METERS (sec 13 step 3). One number cannot be
                // both, and while it tried to be, no budget could
                // be honoured:
                //
                // - GPU = what is UPLOADED. Released by
                //   collectMeshes once the scene stops referencing
                //   it, which is why the sweep judges `live` and
                //   not the total that still carries the rungs the
                //   last descent replaced.
                // - CPU = what is RESIDENT: the mesh arrays the
                //   published scene holds in the heap, per distinct
                //   mesh. Its release point is a demote (drop the
                //   rung), not a downgrade (stop displaying it) --
                //   a different quantity freed by a different move.
                //
                // Reported side by side, and each sweep spends the
                // one its own budget is quoted in.
                BGFXView *view = _BGFXLib.findView(widget);
                const BGFXView::GpuBytes gpu =
                    view ? view->gpuBytes() : BGFXView::GpuBytes();
                uint64_t cpuResident = 0;
                {
                    std::set<const Render::MeshData *> seen;
                    for (const auto &draw : scene) {
                        if (draw.mesh && seen.insert(draw.mesh.get()).second)
                            cpuResident +=
                                Render::meshResidentBytes(draw.mesh.get());
                    }
                }
                // The deficits the two sweeps actually ran with,
                // kept for the readout: re-deriving them after the
                // drops would report a different number, since the
                // upload accounting has not moved yet.
                size_t dmDeficit = 0, dgDeficit = 0;
                // Raw excess a fully-credited ledger held the
                // downgrade sweep against, for the report: an
                // absent pass under standing pressure would
                // otherwise read as a broken sweep.
                size_t dgHeld = 0;
                Render::PlanDemoteStats dmStats, dgStats;
                // Demotions first (§13 step 3), and only ever under
                // an observed CPU-memory ceiling: drop the hidden
                // exact rungs outright (nothing on screen changes),
                // then the displayed exact rungs the camera would
                // not miss — off screen, or coarse within half the
                // tolerance — before spending anything on new
                // builds.
                // Every order is now a worker job whose enqueue
                // snapshots on the GUI thread, so one pass orders a
                // bounded batch (the DescentOrderBatch parameter, the
                // climb admission batch's mirror) and the replan
                // after the batch lands takes the rest.
                const size_t descentBatch =
                    size_t(std::max(0, descentOrderBatch));
                if (reg.memoryCeilingEpoch()) {
                    reg.dropHiddenLevels();
                    // How much RAM the observer wanted back (the
                    // refine worker knows its floor and what the
                    // system had free). Non-zero buys the priced
                    // tier: exact rungs whose coarse replacement
                    // WOULD show, cheapest first, until the
                    // shortfall is covered. Zero -- a ceiling
                    // observed without a quantity, e.g. a
                    // bad_alloc -- leaves the free tier alone.
                    auto drops = Render::planMeshDemotes(
                        scene, levelPlanner.viewMatrix(),
                        levelPlanner.projMatrix(), h,
                        levelPlanner.tolerance(),
                        [&reg](const void *t) {
                            return reg.demoteError(t);
                        },
                        &dmStats, dmDeficit = reg.memoryShortfall(),
                        {}, {}, descentBatch);
                    nDemote = drops.size();
                    for (const void *tag : drops)
                        reg.requestDemote(tag);
                    // A capped pass has not covered the ceiling;
                    // replan once this batch has had its frames.
                    if (dmStats.deferredByCap)
                        levelPlanner.markDirty();
                }
                // The GPU budget's half (§13 step 3): over it,
                // downgrade the *displayed* rung of what the
                // camera would not miss. The exact mesh stays in
                // CPU RAM — the way back up is an instant
                // re-activation through an ordinary refine.
                const size_t gpuBudget = gpuBudgetBytes();
                // The TOTAL -- the allocator's own books -- now
                // that publication-keyed collection (sec 13c.5)
                // made a free a synchronous event: a swapped rung
                // is destroyed at the next collect, drawn or not,
                // so the total no longer carries two-frame ghosts
                // of the plan's own applies. The live census that
                // replaced the total here once (a plan that had
                // just downgraded 2394 sources read its memory as
                // risen) alternated under any redraw cadence
                // longer than its two-frame window -- the measured
                // 45<->120MB wave on a still camera -- and a
                // controller fed by it chased its own sampling.
                const size_t gpuUsed = size_t(gpu.total);
                // The rest band (Render_LevelBudgetDeadband): the
                // sweep triggers only past budget*(1+deadband) and
                // corrects back to the budget, so an equilibrium
                // that lands just over the line may STAND -- climbs
                // already stop at the budget, and inside the band
                // neither direction acts. Correcting to the same
                // line the sweep triggers on is a dither: measured
                // 2-3 downgrades per plan forever when a converged
                // ladder sat 0.2-0.4MB over, deciding whether a
                // run settles or churns to its timeout. Pressure
                // still stands in the band (underPressure below
                // reads the bare budget), holding the raised
                // tolerance and the edge gate as they were.
                const size_t dgTrigger = gpuBudget
                    + size_t(double(gpuBudget)
                             * double(levelBudgetDeadband));
                if (gpuBudget && gpuUsed > dgTrigger) {
                    // ...minus what previous sweeps have already
                    // ordered freed but the meter has not admitted
                    // yet: a drop's fine buffers leave `live` only
                    // after the collection window, while its coarse
                    // swap-in shows immediately, so a plan sampling
                    // the transition reads old+new and would
                    // re-correct off its own correction -- the
                    // measured 1500-request storms. The ledger
                    // (SceneLadder.h) holds the sweep while its
                    // orders are in flight and expires what never
                    // lands.
                    size_t deficit = gpuUsed - gpuBudget;
                    if (downgradeLedgerOn) {
                        // The write-off horizon rides the ordered
                        // chains: an order's bytes cannot land
                        // before its descent generation drains,
                        // so credit stands until the registry
                        // says the last chained job settled, and
                        // the frame window starts there.
                        deficit = size_t(view->dgLedger.deficit(
                            gpuUsed, gpuBudget, view->frame,
                            [&reg](uint64_t g) {
                                return reg.descentGenerationSettled(g);
                            }));
                        if (!deficit)
                            dgHeld = gpuUsed - gpuBudget;
                    }
                    if (deficit) {
                    // Priced in GPU bytes, because that is what the
                    // deficit is quoted in -- see uploadedBytesOf.
                    // The charge is per sweep, so a geometry shared
                    // by several cache ids is promised once.
                    BGFXView::UploadCharge charge;
                    // Occlusion's verdict widens the free tier here
                    // and only here: a downgrade keeps the exact
                    // mesh in CPU RAM, so a verdict the camera
                    // later overturns costs one upload -- the CPU
                    // demote sweep above pays a re-tessellation for
                    // the same mistake, and does not get the feed.
                    const uint32_t needStreak =
                        (cullconf.enabled && cullconf.software)
                        ? cullconf.demoteStreak : 0;
                    auto hiddenOf = [this, needStreak](const void *tag)
                        -> bool {
                        if (!needStreak)
                            return false;
                        auto it = occlHiddenStreak.find(tag);
                        return it != occlHiddenStreak.end()
                            && it->second.fold == occlStreakFold
                            && it->second.frames >= needStreak;
                    };
                    auto drops = Render::planMeshDemotes(
                        scene, levelPlanner.viewMatrix(),
                        levelPlanner.projMatrix(), h,
                        levelPlanner.tolerance(),
                        [&reg](const void *t) {
                            return reg.downgradeError(t);
                        },
                        &dgStats, dgDeficit = deficit,
                        [view, &charge](const Render::MeshData *m)
                            -> uint64_t {
                            return view && m
                                ? view->uploadedBytesOf(*m, &charge) : 0;
                        },
                        hiddenOf, descentBatch);
                    nDowngrade = drops.size();
                    // The generation every job this order's chains
                    // queue will inherit: the hook bodies enqueue
                    // right here (the pace wrapper), and the
                    // landing pump re-enters it for the chained
                    // worker builds and pooled fills.
                    const uint64_t dgGen = drops.empty()
                        ? 0 : reg.openDescentGeneration();
                    {
                        Render::MeshSourceRegistry::DescentGenScope
                            scope(dgGen);
                        for (const void *tag : drops)
                            reg.requestDowngrade(tag);
                    }
                    // What this sweep just promised, carried
                    // against the deficits the next plans compute
                    // off the apply transient.
                    if (downgradeLedgerOn)
                        view->dgLedger.order(dgStats.bytesFreed,
                                             gpuUsed, view->frame,
                                             dgGen);
                    // One pass cannot know it freed enough: what it
                    // counted is what stands uploaded now, and the
                    // rung it swaps in takes some of it back. So
                    // while the budget still stands exceeded,
                    // replan -- but only after a pass that actually
                    // dropped something, or a budget nothing can
                    // satisfy would replan forever. Each drop
                    // consumes its source's hook, so the sequence
                    // terminates.
                    if (nDowngrade)
                        levelPlanner.markDirty();
                    }
                }

                // The climb, and it runs AFTER the descent on
                // purpose -- at the descent's tolerance, not the
                // camera's.
                //
                // Measured on the rack model at a 64 MB budget: the
                // plan downgraded 372 sources, and the very next
                // plan's refine pass asked for 366 of them straight
                // back. Of course it did -- a source demoted to save
                // memory is by definition one erring more than the
                // tolerance on screen, which is exactly the refine
                // pass's own criterion. Two passes reading two
                // different tolerances make the ladder oscillate,
                // and every lap costs a tessellation and an upload.
                //
                // So pressure raises ONE effective tolerance, for
                // both directions. The descent reports the worst
                // error it had to accept; dividing by the demote
                // margin puts the climb's threshold back above it by
                // the same hysteresis band the two passes use with
                // no pressure at all. It applies only for as long as
                // the pressure does: at the first plan that is
                // inside its budget the tolerance is the camera's
                // again and the ladder climbs back.
                //
                // Only the CLIMB reads the raised value. Feeding it
                // back into the descent would run away -- a wider
                // free tier accepts more error, which widens the
                // tolerance, which widens the free tier -- whereas
                // the priced tier is bounded by the deficit and
                // stops on its own.
                // What holds the raised tolerance up is the PRESSURE
                // standing, never the sweep having succeeded. The
                // plan after a successful descent finds only sources
                // with no rung left to drop, so it accepts no error
                // at all -- and reading the tolerance off that would
                // hand it straight back to the camera and re-ask for
                // everything just given up.
                //
                // How it comes back down is Render::PressureTolerance
                // (sec 13c.3), and it is the half that was wrong:
                // clearing the raise at the first plan inside the
                // budget is what made this ladder cycle for 43 plans
                // without a steady state. The controller keeps the
                // fast attack and releases in steps, remembering the
                // step that broke the budget as a floor.
                const bool underPressure =
                    (gpuBudget && gpuUsed > gpuBudget)
                    || (reg.memoryCeilingEpoch() && reg.memoryShortfall());
                const float accepted = std::max(dmStats.acceptedErrorPx,
                                                dgStats.acceptedErrorPx);
                // What the floor it learns is evidence ABOUT: this
                // camera, and this budget. Both change what a rung
                // costs on screen, so a floor measured under either
                // says nothing once it moves.
                if (levelPlanner.cameraMoved()
                    || gpuBudget != levelBudgetSeen)
                    levelPressure.forget();
                levelBudgetSeen = gpuBudget;
                const float refineTolerance = levelPressure.update(
                    underPressure, accepted, levelPlanner.tolerance(), h,
                    levelPressureReleaseFrac);
                // The edge gate's latch (see pressureStanding): held
                // for as long as the ladder holds raised error, and
                // released with it -- the same statement about the
                // same scene, read where the gate can see it.
                pressureStanding = levelPressure.raisedPx > 0.0f;
                // A release is a staircase, and a still camera over a
                // quiet scene raises no event of its own -- so the
                // step that just gave error back has to ask for the
                // next one, or quality stops coming back halfway.
                if (levelPressure.releasing)
                    levelPlanner.markDirty();
                auto tags = Render::planMeshRefines(
                    scene, levelPlanner.viewMatrix(),
                    levelPlanner.projMatrix(), h, refineTolerance);
                // A refine asked for is a mesh drawn coarser than the
                // plan wants: this frame is not the picture yet. Per
                // frame on purpose -- a refine that lands republishes
                // and re-plans, and a plan that stays quiet must not
                // hold a stale "owes" over every later frame.
                if (!tags.empty())
                    frameOwes = true;

                // The hard ceiling (sec 13c.5): the budget is a
                // line climbs may not cross, judged against the
                // allocator-exact uploaded TOTAL -- the two-frame
                // live census alternates under churn, and feeding
                // the admission from it is what let climbs land
                // over budget. At or over the ceiling nothing is
                // admitted, and clearing the wanted set here makes
                // the de-want pass below abort every climb still
                // in flight. Under it, admission is batched: no
                // single plan may move the total by more than one
                // batch before the next plan re-reads the truth.
                // The batch is arbitrary WITHIN a plan (the set is
                // unordered), but every plan re-evaluates the
                // whole scene, so nothing starves across plans.
                if (climbHardLimitOn && gpuBudget && !tags.empty()) {
                    if (gpu.total >= gpuBudget) {
                        tags.clear();
                    } else if (tags.size()
                               > size_t(climbAdmitBatch)) {
                        tags.resize(size_t(climbAdmitBatch));
                    }
                }

                // Cancels next (§13 step 4): every coarse source
                // this plan does not want is de-wanted — a queued
                // tessellation the camera moved away from is work,
                // not a fetch to ignore. Before the requests, so a
                // same-pass flip lands wanted. cancelRefine only
                // fires where an ask actually stands.
                std::set<const void *> wanted(tags.begin(),
                                              tags.end());
                for (const auto &draw : scene) {
                    if (!draw.mesh || !draw.mesh->sourceTag
                        || draw.mesh->levelError <= 0.0f)
                        continue;
                    if (!wanted.count(draw.mesh->sourceTag))
                        reg.cancelRefine(draw.mesh->sourceTag);
                }
                for (const void *tag : tags)
                    reg.requestRefine(tag);

                // What the plan just decided, and the state it
                // decided against. Nothing reported any of this
                // before, so "the ladder is not descending" could
                // not be told apart from "the ladder never ran" --
                // and on the desktop OpenGL backend the second was
                // true, silently: bgfx's GL renderer reports
                // gpuMemoryMax = -INT64_MAX, so the automatic
                // budget is 0, so the downgrade branch had never
                // executed at all. A dormant mechanism must say it
                // is dormant.
                //
                // On the plan's own cadence (a camera pause), not
                // per frame: it is a decision, not a cost.
                if (levelDebug()) {
                    std::set<const void *> coarse, exact;
                    for (const auto &draw : scene) {
                        if (!draw.mesh || !draw.mesh->sourceTag)
                            continue;
                        (draw.mesh->levelError > 0.0f ? coarse : exact)
                            .insert(draw.mesh->sourceTag);
                    }
                    const size_t budget = gpuBudgetBytes();
                    // Why the gates are where they are, in the one
                    // order a reader would ask: the load gate
                    // overrides the stages, then the staged latch,
                    // then the params. A gate that fires for a
                    // reason it does not name is the confusion
                    // this readout exists to end.
                    const char *gateWhy =
                        loadDropElements
                            ? " (LOADING: both dropped)"
                        : elemPressureStage >= 2 && pressureDropEdges
                            ? " (pressure stage 2: points+lines dropped)"
                        : elemPressureStage >= 1
                            ? " (pressure stage 1: points dropped)"
                        : !shapeVerticesOn
                            ? " (points off by param)"
                        // Said before "no pressure", because with the
                        // coarseness rule in force that is the usual
                        // reason a settled scene is undecorated and
                        // "no pressure" would read as a contradiction
                        // of the counts beside it.
                        : gatedByCoarse
                            ? " (faces still coarse: waiting for the "
                              "exact rung)"
                            : " (no pressure)";
                    // Where the pressure controller stands, and it
                    // has to say which of three things a raised
                    // tolerance means: still descending, walking
                    // back down, or STOPPED at the coarsest setting
                    // that fits. The last one is a settled ladder
                    // and the first is not, and for 43 plans the
                    // readout could not tell them apart.
                    char pressWhy[128] = "";
                    if (levelPressure.raisedPx > 0.0f
                        || levelPressure.floorPx > 0.0f)
                        snprintf(pressWhy, sizeof(pressWhy),
                                 " [holding %.2fpx, floor %.2fpx: %s]",
                                 levelPressure.raisedPx,
                                 levelPressure.floorPx,
                                 underPressure ? "under pressure"
                                 : levelPressure.releasing ? "releasing"
                                 : levelPressure.raisedPx > 0.0f
                                     ? "SETTLED at what fits"
                                     : "released");
                    // Two meters, named for what they measure and
                    // for what releases them, never added together:
                    // the same mesh is counted in both, and it has
                    // to be -- it occupies both.
                    Base::Console().Message(
                        "render levels: gpu budget %s live %.1fMB "
                        "(uploaded %.1fMB, %.1fMB stale in %u of %u "
                        "entries) | cpu resident %.1fMB | displayed "
                        "coarse %zu exact %zu | plan: refine %zu demote %zu "
                        "downgrade %zu | cpu ceiling %s | refine tolerance "
                        "%.2fpx%s%s | gates: eligible %zu, suppressed "
                        "%zu point + %zu line draws (%zu by dependency, "
                        "%zu by coarse faces)%s | tiny cutoff %ld: %zu "
                        "draws / %zu prims\n",
                        budget ? (std::to_string(budget / 1048576)
                                  + "MB").c_str()
                               : "NONE (GL reports no limit; set the "
                                 "GpuMemoryBudgetMB parameter to simulate)",
                        double(gpu.live) / 1048576.0,
                        double(gpu.total) / 1048576.0,
                        double(gpu.stale) / 1048576.0,
                        gpu.staleEntries, gpu.entries,
                        double(cpuResident) / 1048576.0,
                        coarse.size(), exact.size(), tags.size(),
                        nDemote, nDowngrade,
                        reg.memoryCeilingEpoch() ? "OBSERVED" : "no",
                        refineTolerance,
                        levelPressure.raisedPx > 0.0f
                            ? " (RAISED BY PRESSURE)" : "",
                        pressWhy,
                        gateEligible, gatedPoints, gatedLines,
                        gatedByDependency, gatedByCoarse, gateWhy,
                        long(tinyElementCutoff),
                        gatedTiny, gatedTinyPrims);
                    // Who holds the uploaded bytes, by drawable
                    // class, with the share no recent frame drew --
                    // the gap between uploaded and live finally
                    // attributed on the allocator's own books.
                    if (view) {
                        const BGFXView::GpuBytesByClass bc =
                            view->gpuBytesByClass();
                        auto part = [](const char *name,
                                       const BGFXView::ClassBytes &c) {
                            char b[128];
                            snprintf(b, sizeof(b),
                                     " %s %.1fMB/%u entries (undrawn "
                                     "%.1fMB/%u) |",
                                     name,
                                     double(c.bytes) / 1048576.0,
                                     c.entries,
                                     double(c.undrawn) / 1048576.0,
                                     c.undrawnEntries);
                            return std::string(b);
                        };
                        Base::Console().Message(
                            "render levels: uploaded by class:%s%s%s%s\n",
                            part("tri", bc.tri).c_str(),
                            part("line", bc.line).c_str(),
                            part("point", bc.point).c_str(),
                            part("other", bc.other).c_str());
                    }
                    // Why a downgrade pass that ran refused
                    // everything. Printed only when it ran, so its
                    // absence is not mistaken for "no candidates".
                    // Where the sources that cannot descend came
                    // from. The runtime counter says how many lack a
                    // fallback rung; this says which registration
                    // site failed to arm one, which is the
                    // difference between a number and a defect with
                    // an address.
                    {
                        std::string line;
                        for (const auto &t : reg.originTally()) {
                            char b[160];
                            snprintf(b, sizeof(b), " %s:%u(dn %u/dm %u)",
                                     t.origin ? t.origin : "unlabelled",
                                     t.sources, t.withDowngrade,
                                     t.withDemote);
                            line += b;
                        }
                        Base::Console().Message(
                            "render levels: sources by origin (dn = "
                            "downgrade armed, dm = demote armed):%s\n",
                            line.c_str());
                    }
                    // Both sweeps, same shape. `under pressure` and
                    // the accepted error are what say whether the
                    // budget was honourable at all: a pass that
                    // freed nothing while a deficit stood has run
                    // out of sources to descend, which is a
                    // different defect from a pass that refused on
                    // policy.
                    auto reportPass = [](const char *what,
                                         const Render::PlanDemoteStats &s,
                                         size_t deficit) {
                        if (!s.considered)
                            return;
                        Base::Console().Message(
                            "render levels: %s pass: considered %u | "
                            "no fallback rung %u | UNREGISTERED %u | on "
                            "screen and too big %u "
                            "| offscreen %u | occluded %u | eligible %u "
                            "| under pressure "
                            "%u | unpriceable %u | deferred by cap %u "
                            "| want %.1fMB freed "
                            "%.1fMB | out of reach %.1fMB (unregistered "
                            "%.1fMB) | accepted "
                            "error %.2fpx\n",
                            what, s.considered, s.noRung,
                            s.unregistered, s.tooBig,
                            s.offscreen, s.occludedFree, s.eligible,
                            s.underPressure,
                            s.unpriceable, s.deferredByCap,
                            double(deficit) / 1048576.0,
                            double(s.bytesFreed) / 1048576.0,
                            double(s.unreachableBytes) / 1048576.0,
                            double(s.unregisteredBytes) / 1048576.0,
                            s.acceptedErrorPx);
                    };
                    reportPass("demote", dmStats, dmDeficit);
                    reportPass("downgrade", dgStats, dgDeficit);
                    if (dgHeld)
                        Base::Console().Message(
                            "render levels: downgrade pass HELD: "
                            "%.1fMB excess covered by %.1fMB still "
                            "in flight\n",
                            double(dgHeld) / 1048576.0,
                            double(view->dgLedger.promised())
                                / 1048576.0);
                }
            });

        // docs/FarFieldProxies.md §9: how much of the model this
        // camera cannot resolve. Reported on the plan's own schedule
        // rather than per frame — it is a property of where the
        // camera settled, and one line a second is what makes it
        // readable while orbiting a large assembly.
        if (debugconf.coverage) {
            const float h = float(widget->height()
                                  * widget->devicePixelRatioF());
            reportCoverage(Render::coverageHistogram(
                    scene, reinterpret_cast<const float *>(viewMatrix),
                    reinterpret_cast<const float *>(projMatrix), h));
        }

        // docs/FarFieldProxies.md §11.1: what a cut would cost, with
        // nothing generated. The partition is rebuilt on every
        // report rather than cached against a scene signature —
        // a measurement that can be stale measures the wrong thing,
        // the rebuild is what phase 3 will have to pay anyway, and
        // the cost is reported rather than hidden.
        const bool cutDue = debugconf.proxyCut && proxyCutDue();
        const bool genDue = debugconf.proxyGen && proxyGenDue();
        if (cutDue || genDue) {
            const float h = float(widget->height()
                                  * widget->devicePixelRatioF());
            const int64_t started = bx::getHPCounter();
            std::vector<Render::ProxyInstance> instances;
            Render::proxyInstances(scene, instances);
            Render::ProxyHierarchy index;
            index.build(instances);
            const double buildMs =
                1000.0 * double(bx::getHPCounter() - started)
                / double(bx::getHPFrequency());
            if (cutDue)
                reportProxyCut(
                        index, reinterpret_cast<const float *>(viewMatrix),
                        reinterpret_cast<const float *>(projMatrix), h,
                        buildMs);
            // §11.1c, on the same partition the cut was measured on
            // — two readouts describing different partitions of the
            // same frame would not compose.
            if (genDue) {
                reportProxyGen(
                        index, scene,
                        reinterpret_cast<const float *>(viewMatrix),
                        reinterpret_cast<const float *>(projMatrix), h);
                // Section 7.1 on the same partition and the same cut,
                // for the same reason.
                reportProxyStore(
                        index, scene,
                        reinterpret_cast<const float *>(viewMatrix),
                        reinterpret_cast<const float *>(projMatrix), h);
                // And what the cut costs once those proxies are real,
                // which needs the whole partition generated rather
                // than a sample of it.
                reportProxyCutPriced(
                        index, scene,
                        reinterpret_cast<const float *>(viewMatrix),
                        reinterpret_cast<const float *>(projMatrix), h);
                proxyGenReported();
            }
        }
    }

    publishScene(viewMatrix, projMatrixFed, width, height, clearColor,
                 dirtyChanged);
#endif

    // Publish-only frame. A serving process with nobody at its own
    // window has, at this point, done the whole of what its frame is
    // for: the feeds are consumed and the snapshot is out. What
    // follows -- every pass, for a fountain several cores of
    // software rasterization under Xvfb -- draws a picture nothing
    // reads. The viewers render the scene themselves from the
    // snapshot; they never receive these pixels.
    //
    // `renderOk` AND `hasScene` are set on purpose, because
    // canSkipInternal() needs both to tell SoFCRenderer to skip its
    // own fixed-function GL pass: this frame IS accounted for, by
    // deliberately drawing nothing. Reporting failure instead would
    // hand the same scene to Coin to rasterize, which is no cheaper
    // -- and a process that has served from launch never reaches
    // the end-of-frame assignments below, so leaving `hasScene`
    // to them means it never turns true and Coin rasterizes every
    // frame anyway.
    //
    // Everything above this line is CPU-side feed work, so the
    // snapshot a connecting viewer receives is exactly the one it
    // would have received while the window was being drawn.
    // A pending local dump (saveRenderDump on this process) is the
    // exception: it asks for these pixels by name, so that frame
    // draws in full.
    if (!localAudience() && !dumpPending) {
        renderOk = true;
        hasScene = !scene.empty();
#ifndef FC_RENDERER_STANDALONE
        // The re-snapshot in the post-pass setup is unreachable
        // from here, and without it one finished async user-shader
        // compile leaves dirtyChanged latched: every later frame
        // would re-serialize and republish the whole snapshot.
        // The publish above already carried the fresh binaries, so
        // the generation is consumed exactly as publishNoDraw()
        // consumes it.
        userShaderGen = _BGFXLib.userCompileGeneration;
#endif
        return true;
    }

    // Reconcile the demand-allocated target groups against the
    // configuration, before anything reads their handles. Each
    // predicate below is configuration ONLY -- no scene content, no
    // this-frame *Active flag -- so an effect's targets appear when it
    // is switched on and go away when it is switched off, and survive
    // everything in between. (BGFXView::updateEffect.)
    //
    // The volumetric group carries the water, cloud and fire interval
    // targets as well: every one of those passes is gated on volActive
    // downstream, so Render_Volumetric is the single switch that owns
    // the whole ~166MB set.
    // m_vol here so a GPU that cannot do it at all is never asked, and
    // so never reports a failure it was always going to have.
    view->updateEffect(BGFXView::EffectVolumetric,
                       view->m_vol && volconf.enabled);
    view->updateEffect(BGFXView::EffectBloom, bloomconf.enabled);
    // Configuration only, like every other group here: whether this
    // frame is one that accumulates is scene state and decided far
    // below, and folding it in would free the history the moment the
    // camera moved and rebuild it the moment it stopped.
    view->updateEffect(BGFXView::EffectAccum, tempconf.enabled);
    // The output colour transform this frame will actually apply --
    // none of it under a debug view mode, which blits a QUANTITY into
    // the scene colour (prepass depth, the AO term, a coverage count)
    // rather than light. Encoding a quantity would change what the
    // picture means, and these modes are read as measurements.
    view->outputTransform = debugconf.viewMode == 0
        ? outconf.transform : int(Render::OutputConfig::None);
    view->outputExposure = outconf.exposure;
    // Its target. Standalone presents through the same pass whatever
    // the transform is (that pass is what reaches the backbuffer at
    // all) but presents onto the DEFAULT backbuffer and needs no target
    // of its own; only the desktop, whose GL blit has to be handed
    // something already encoded, does.
#ifndef FC_RENDERER_STANDALONE
    view->updateEffect(BGFXView::EffectPresent,
                       view->outputTransform
                           != Render::OutputConfig::None);
#endif
    // The scene light's shadow maps, ~117MB at ShadowPrecision 1.0:
    // the 2048^2 moments and their depth, the blur ping and the glass
    // tint pair. Wanted whenever a scene light is fed and Render_Shadow
    // is on -- both configuration, the light being the Shadow draw
    // style's (or Render_Light's), not scene content. Deliberately not
    // the frame's shadowActive, which folds in the scene bound and so
    // would free the set for every document that momentarily has no
    // geometry.
    //
    // The ground receiver and the bulb tiles read as shadow settings
    // but pay for neither: the ground quad draws unshadowed without a
    // map (submitShadowGround), and the bulb atlas is its own group.
    //
    // A MATCAP frame does not tap the map either: its whole shading is
    // a camera-fixed studio looked up by the view normal, with no light
    // and no shadow term, which is the point of the mode -- form reads
    // the same wherever the light sits. So a matcap view of a document
    // whose Shadow setting happens to be on was holding 117MB it could
    // never sample, and re-rendering the map whenever a caster moved.
    //
    // Two things still tap it in a matcap frame and are asked before
    // the set is dropped:
    //   - the volumetric shafts, which REQUIRE shadowActive (volActive
    //     below is gated on it) and are a frame effect rather than a
    //     surface one, so the mode does not exempt them;
    //   - a draw carrying a GENERATED material, which takes the OpenPBR
    //     branch whatever the frame's shading mode says
    //     (FC_USER_MATERIAL in fc_mesh_lighting.sh) and so keeps a
    //     shadow term the matcap branch does not have.
    bool matcapTapsShadow = false;
    const bool matcapFrame = matcapconf.enabled && !hlconfig.show;
    if (matcapFrame) {
        matcapTapsShadow = volconf.enabled;
        for (const auto &d : scene) {
            const auto &sh = d.material.usershader;
            if (sh && sh->dialect == Render::UserShader::Dialect::MaterialX) {
                matcapTapsShadow = true;
                break;
            }
        }
    }
    const bool shadowWanted = view->m_shadow && lightconf.valid
        && lightconf.shadow && (!matcapFrame || matcapTapsShadow);
    {
        // Coin's sizing: the next power of two of precision * the cap.
        float prec = bx::clamp(lightconf.precision, 0.01f, 1.0f);
        uint16_t desired = 1;
        uint16_t want = uint16_t(prec * BGFXView::kShadowMaxSize);
        while (desired < want)
            desired = uint16_t(desired << 1);
        view->shadowSizeWanted = desired;
    }
    view->updateEffect(BGFXView::EffectShadow, shadowWanted);
    // One shared mirror target, wanted by either consumer.
    view->updateEffect(BGFXView::EffectReflection,
                       lightconf.groundReflection
                           || (waterconf.enabled
                               && waterconf.reflection
                               && waterconf.planarReflection));
    // The AO/prepass set: the full-res depth+normal prepass, the AO
    // resolve chain and the glass absorption interval, ~98MB at 1080p.
    // Five consumers read it and each one is its own switch, so the
    // predicate is their union:
    //   - SSAO (Render_SSAO) and cavity shading, which share the chain;
    //   - the volumetric raymarch, whose ray ends are the prepass depth;
    //   - the water surface, which rejects refraction samples by it;
    //   - the debug buffer views that visualize the prepass or the AO.
    // Only the volumetric one is a target group of its own, so this
    // cannot be folded into any of them.
    //
    // The fifth consumer is glass, which has no preference anywhere --
    // it is a material. A glass body can therefore only ADD to the
    // demand (view->glassSeen), never take it away.
    //
    // Glass bodies (Material::glass): the draws leave the ordinary
    // path and re-render in the glass surface pass -- screen-space
    // refraction, thickness absorption from the glass front/back
    // interval, environment reflection. Needs the scene copy, this
    // resource set and the environment; hidden-line mode disables it
    // like the other shading effects. Unlike water the body's
    // edge/vertex draws keep rendering (a glass part keeps its CAD
    // feature lines).
    bool hasGlassBody = false;
    for (const auto &draw : scene) {
        const auto &mat = draw.material;
        if (mat.glass && !mat.ontop && mat.numclipplanes == 0
                && mat.type == Render::Material::Triangle) {
            hasGlassBody = true;
            break;
        }
    }
    if (hasGlassBody)
        view->glassSeen = true;
    view->updateEffect(BGFXView::EffectSSAO,
                       view->m_ssao
                           && (aoconf.enabled || cavityconf.enabled
                               || volconf.enabled || waterconf.enabled
                               || (debugconf.viewMode >= 1
                                   && debugconf.viewMode <= 5)
                               || debugconf.viewMode == 7
                               || view->glassSeen));

    // WBOIT runs when the resources exist and the scene has any
    // transparent (non-on-top) triangles this frame; otherwise the
    // transparent view stays a bbox-sorted alpha blend into the
    // scene framebuffer.
    bool oitActive = false;
    if (view->m_oit) {
        for (const auto &draw : scene) {
            if (!draw.material.ontop
                    && draw.material.type == Render::Material::Triangle
                    && (draw.material.transparent
                        || (draw.material.pervertexcolor && draw.mesh
                            && draw.mesh->hasTransparency))) {
                oitActive = true;
                break;
            }
        }
    }
    view->oitFrame = oitActive;

    // SSAO runs when the resources exist, the per-frame config asks
    // for it, and there is opaque scene geometry to occlude. The
    // hidden-line draw style disables it (a technical drawing mode;
    // its faces may not draw at all).
    // Cavity shading shares every part of that gate: it reads the same
    // prepass (so it needs the same resources), it is just as wrong on a
    // hidden-line technical view, and it needs opaque triangles to state
    // the curvature of. Zero strengths mean the multiply would be a
    // no-op, so the pass is not worth a target switch.
    // isValid(aoPrepassFbo): the set is demand-allocated, so "the config
    // wants AO" and "the targets exist" are no longer the same statement
    // -- a pool with nothing left leaves the group unbuilt and every
    // pass that reads it simply does not run (as for the volumetric).
    // The shaded image is the attached consumer's this frame
    // (Renderer::setExternalBaseLayer, docs/CyclesIntegration.md sec
    // 5.3): the scene rasterizes depth-only and the screen-space
    // shading effects -- AO, cavity, bloom, the ground reflection,
    // the idle accumulation that refines them -- have nothing to
    // work on. The volumetric and water/glass passes stay: they run
    // after the consumer's blit by design and composite over it.
    const bool externalBase = this->externalBase && frameConsumer
        && consumerSurface;
    view->externalBase = externalBase;
    const bool ssaoWanted = view->m_ssao && aoconf.enabled
        && !hlconfig.show && !externalBase
        && bgfx::isValid(view->aoPrepassFbo);
    const bool cavityWanted = view->m_ssao && cavityconf.enabled
        && !hlconfig.show && !externalBase
        && bgfx::isValid(view->aoPrepassFbo)
        && (cavityconf.valley > 0.0f || cavityconf.ridge > 0.0f)
        && bgfx::isValid(view->m_progCavity);
    bool hasOpaqueTri = false;
    if (ssaoWanted || cavityWanted) {
        for (const auto &draw : scene) {
            if (!draw.material.ontop
                    && draw.material.type == Render::Material::Triangle
                    && !draw.material.transparent
                    && !(draw.material.pervertexcolor && draw.mesh
                         && draw.mesh->hasTransparency)) {
                hasOpaqueTri = true;
                break;
            }
        }
    }
    bool ssaoActive = ssaoWanted && hasOpaqueTri;
    const bool cavityActive = cavityWanted && hasOpaqueTri;
    // PBR runs when the per-frame config asks for it and the
    // environment could be built (caps). The hidden-line draw style
    // disables it like SSAO (a technical drawing mode).
    bool pbrActive = false;
    if (pbrconf.enabled && !hlconfig.show) {
        // A changed environment image invalidates the built cubemap
        // (and its irradiance SH) — rebuild on the next ensure.
        if (view->m_envImage != pbrconf.envImage
                || view->m_envPreset != pbrconf.envPreset) {
            view->m_envImage = pbrconf.envImage;
            view->m_envPreset = pbrconf.envPreset;
            view->m_envBuilt = false;
        }
        view->ensureEnvironment();
        pbrActive = bgfx::isValid(view->m_envTex);
    }
    // The debug buffer visualization (docs/RenderDebug.md) reads the
    // prepass normal/depth and AO targets: force the SSAO chain on
    // while a mode that needs it is active, scene geometry
    // permitting (same opaque-triangle test as above). Modes 4/5/7
    // reconstruct positions from the prepass depth, so they force
    // it too (mode 4 previously relied on another prepass consumer
    // being active).
    if (!ssaoActive && view->m_ssao
            && bgfx::isValid(view->aoPrepassFbo)
            && ((debugconf.viewMode >= 1 && debugconf.viewMode <= 5)
                || debugconf.viewMode == 7)) {
        for (const auto &draw : scene) {
            if (!draw.material.ontop
                    && draw.material.type == Render::Material::Triangle
                    && !draw.material.transparent
                    && !(draw.material.pervertexcolor && draw.mesh
                         && draw.mesh->hasTransparency)) {
                ssaoActive = true;
                break;
            }
        }
    }
    view->pbrFrame = pbrActive;
    view->pbrMetallic = pbrconf.metallic;
    view->pbrFromSpecular = pbrconf.fromSpecular;
    view->pbrShininessMapping = pbrconf.shininessMapping;
    view->pbrRoughness = pbrconf.roughness;
    view->pbrEnvIntensity = pbrconf.envIntensity;
    view->pbrEnvBlur = pbrconf.envBlur;
    // Matcap replaces the lit shading outright, so it does not care
    // whether the environment could be built the way PBR does.
    view->matcapFrame = matcapFrame;
    // What the Tessellation draw style fills its faces with, so they
    // occlude without being seen (Coin gets this from the render
    // manager's hidden-line pass, not from the draw style).
    view->bgFillColor = background.type == Render::Background::Flat
        ? background.fromColor : background.toColor;
    view->matcapPreset = matcapconf.preset;
    view->matcapTint = matcapconf.tint;
    view->bumpScale = bumpconf.scale;
    view->bumpParallax = bumpconf.parallax;

    // Whole-object selection/highlight draws replace the object's
    // normal rendering (SoFCRenderer's selectionkeys/highlightkeys
    // skip): collect their object keys and hide matching scene draws.
    // Computed before the shadow setup — hidden draws don't cast, so
    // the cached-map caster hash needs them.
    hiddenKeys.clear();
    for (const auto &sel : selections) {
        for (const auto &draw : sel.second) {
            if (draw.wholeObject && draw.objectKey)
                hiddenKeys.insert(draw.objectKey);
        }
    }
    if (hlWholeOnTop) {
        for (const auto &draw : highlight) {
            if (draw.wholeObject && draw.objectKey)
                hiddenKeys.insert(draw.objectKey);
        }
    }
    auto isHidden = [this](const Render::DrawCall &d) {
        return d.objectKey && !hiddenKeys.empty()
            && hiddenKeys.count(d.objectKey);
    };

    // The scene light of the feed (the Shadow draw style's light, or
    // the one Render_Light supplies), resolved into camera view space
    // for the shaders. Independent of the shadow map below: a fed
    // light lights the frame even when no map is rendered -- because
    // Render_Shadow is off (documented as "drop the shadow map while
    // keeping the scene lit"), because the caps carry no filterable
    // shadow format, or because the scene has no extent to fit a light
    // camera to. Computing it inside the shadow gate made all three of
    // those silently fall back to the fixed headlight, dropping the
    // light itself.
    view->lightFrame = lightconf.valid;
    if (lightconf.valid) {
        const Render::LightConfig &light = lightconf;
        bx::Vec3 dir = bx::normalize(
            bx::Vec3(light.direction[0], light.direction[1],
                     light.direction[2]));
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        float lv[3];
        for (int j = 0; j < 3; ++j)
            lv[j] = dir.x * vm[j] + dir.y * vm[4 + j]
                + dir.z * vm[8 + j];
        float ll = std::sqrt(lv[0]*lv[0] + lv[1]*lv[1] + lv[2]*lv[2]);
        for (int j = 0; j < 3; ++j)
            view->lightDirView[j] = ll > 0.0f ? lv[j] / ll : lv[j];
        unpackAuthoredColor(light.color, view->lightColorI,
                            view->colorManaged());
        for (int j = 0; j < 3; ++j)
            view->lightColorI[j] *= light.intensity;
        // Spot light: position in camera view space, cone cutoff
        // cosine in w (-1 = directional), falloff exponent riding the
        // light color's free channel.
        if (light.spot) {
            for (int j = 0; j < 3; ++j)
                view->lightPosView[j] =
                    light.position[0] * vm[j]
                    + light.position[1] * vm[4 + j]
                    + light.position[2] * vm[8 + j]
                    + vm[12 + j];
            view->lightPosView[3] =
                std::cos(bx::clamp(light.cutOffAngle, 0.01f, 1.55f));
            view->lightColorI[3] =
                bx::clamp(light.dropOffRate, 0.0f, 1.0f) * 128.0f;
        } else {
            view->lightPosView[0] = 0.0f;
            view->lightPosView[1] = 0.0f;
            view->lightPosView[2] = 0.0f;
            view->lightPosView[3] = -1.0f;
            view->lightColorI[3] = 0.0f;
        }
    }

    // The ordinary Coin lights (headlight, backlight, document
    // directional/point lights), world space in the feed, resolved to
    // camera view space here. When the feed carries none the fixed
    // white headlight down the view axis is written into slot 0
    // instead: it is what this renderer has always drawn, so an old
    // dump and a consumer that predates the config keep their look,
    // and the shader gets one loop with no fallback branch.
    {
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        auto toView = [vm](const float *w, bool point, float *out) {
            // A direction ignores the translation; a position takes it.
            for (int j = 0; j < 3; ++j)
                out[j] = w[0] * vm[j] + w[1] * vm[4 + j]
                    + w[2] * vm[8 + j] + (point ? vm[12 + j] : 0.0f);
        };
        auto unit = [](float *d) {
            float len = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
            if (len > 0.0f) {
                for (int j = 0; j < 3; ++j)
                    d[j] /= len;
            }
        };
        // The Realistic branch is lit by the SCENE -- the environment,
        // the scene light, and any light a DOCUMENT adds -- and not by
        // the viewport's own aids. Two of those aids are dropped here.
        //
        // The headlight, backlight and fill light are camera-attached
        // (`eyeSpace`, which is what the bridge computes them to be),
        // and Coin's LIGHT_MODEL_AMBIENT is a Phong-era global fudge.
        // Both exist so that Classic can never show an unlit model,
        // which is what that mode is FOR and stays true of it (Matcap
        // needs neither: its studio is the shading). Adding them on top
        // of image-based lighting instead puts a FLOOR under the
        // picture that no environment setting can remove: with a black
        // environment, no sun and no lights, a 0.5 grey box still drew
        // at byte 68 where the path tracer -- the same shading model,
        // traced -- draws black. Measured, and the floor split exactly
        // into these two: headlight 0.0503, ambient 0.0075 of linear
        // light (docs/MaterialStorage.md sec 17.13).
        //
        // Gated on pbrActive rather than on the config, so a frame that
        // asked for Realistic and fell back to Classic because the
        // environment could not be built keeps the lights it is about
        // to shade with. It is the same flag the shader branches on.
        const bool sceneLitOnly = pbrActive;
        view->viewAmbientFed = viewlightconf.fed && !sceneLitOnly;
        view->viewAmbient = viewlightconf.ambient;
        int n = 0;
        if (viewlightconf.fed) {
            for (int i = 0; i < viewlightconf.count; ++i) {
                const Render::ViewLight &l = viewlightconf.lights[i];
                // A document's own lights are the scene and stay; the
                // viewer's camera-attached ones do not.
                if (sceneLitOnly && l.eyeSpace)
                    continue;
                // A spot light spends TWO slots: its position, colour
                // and attenuation fill a light's twelve floats already,
                // and the cone axis is three more, so it goes in the
                // whole of the following slot. That costs no uniform
                // budget at all -- the alternative was a fourth
                // per-light array, eight vec4 charged to every frame for
                // something a scene almost never has (docs/
                // RenderEngine.md 3.2). Both slots have to be free.
                const int slots = l.spot ? 2 : 1;
                if (n + slots > BGFXView::kViewLights)
                    break;
                toView(l.positional ? l.position : l.direction,
                       l.positional, view->viewLightView[n]);
                if (!l.positional)
                    unit(view->viewLightView[n]);
                view->viewLightView[n][3] =
                    l.spot ? 3.0f : (l.positional ? 2.0f : 1.0f);
                unpackAuthoredColor(l.color, view->viewLightColorI[n],
                                    view->colorManaged());
                for (int j = 0; j < 3; ++j) {
                    view->viewLightColorI[n][j] *= l.intensity;
                    view->viewLightAtt[n][j] = l.attenuation[j];
                }
                if (l.spot) {
                    // The cone rides the two floats the layout does not
                    // otherwise use, converted the way the scene light's
                    // pair above is: Coin's half angle to its cosine,
                    // its 0..1 dropOffRate to a GL falloff exponent.
                    view->viewLightAtt[n][3] =
                        std::cos(bx::clamp(l.cutOffAngle, 0.01f, 1.55f));
                    view->viewLightColorI[n][3] =
                        bx::clamp(l.dropOffRate, 0.0f, 1.0f) * 128.0f;
                    // The continuation slot: axis in xyz, kind 4, which
                    // the shader shades as a light of zero colour so the
                    // loop walks past it without a second exit test.
                    toView(l.direction, false, view->viewLightView[n + 1]);
                    unit(view->viewLightView[n + 1]);
                    view->viewLightView[n + 1][3] = 4.0f;
                    for (int j = 0; j < 4; ++j) {
                        view->viewLightColorI[n + 1][j] = 0.0f;
                        view->viewLightAtt[n + 1][j] = 0.0f;
                    }
                }
                n += slots;
            }
        } else {
            view->viewLightView[0][0] = 0.0f;
            view->viewLightView[0][1] = 0.0f;
            view->viewLightView[0][2] = -1.0f;
            view->viewLightView[0][3] = 1.0f;
            for (int j = 0; j < 3; ++j) {
                view->viewLightColorI[0][j] = 1.0f;
                view->viewLightAtt[0][j] = 0.0f;
            }
            view->viewLightAtt[0][2] = 1.0f;
            n = 1;
        }
        // Zero the tail so a slot left over from a previous frame
        // cannot light this one.
        for (int i = n; i < BGFXView::kViewLights; ++i) {
            for (int j = 0; j < 4; ++j) {
                view->viewLightView[i][j] = 0.0f;
                view->viewLightColorI[i][j] = 0.0f;
                view->viewLightAtt[i][j] = 0.0f;
            }
        }
    }

    // Shadow draw style: a light in the scene feed activates the
    // variance shadow map pass (only that style traverses one — the
    // viewer headlight lives outside the captured graph). The light
    // camera is an orthographic fit of the scene bounds along the
    // light direction; spot lights fall back to their direction
    // (a known first-cut deviation).
    bool shadowActive = false;
    float lightViewMtx[16], lightProjMtx[16];
    if (shadowWanted && bboxValid) {
        const Render::LightConfig &light = lightconf;
        shadowActive = true;
        {
            float dx = bboxMax[0] - bboxMin[0];
            float dy = bboxMax[1] - bboxMin[1];
            float dz = bboxMax[2] - bboxMin[2];
            float diag = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (diag <= 0.0f) {
                shadowActive = false;
            } else {
                // Half diagonal with the GL ShadowBoundBoxScale-like
                // margin.
                float r = 0.6f * diag;
                bx::Vec3 dir = bx::normalize(
                    bx::Vec3(light.direction[0], light.direction[1],
                             light.direction[2]));
                bx::Vec3 center((bboxMin[0] + bboxMax[0]) * 0.5f,
                                (bboxMin[1] + bboxMax[1]) * 0.5f,
                                (bboxMin[2] + bboxMax[2]) * 0.5f);
                bx::Vec3 up = bx::abs(dir.z) > 0.99f
                    ? bx::Vec3(1.0f, 0.0f, 0.0f)
                    : bx::Vec3(0.0f, 0.0f, 1.0f);
                const auto *caps = bgfx::getCaps();
                if (light.spot) {
                    // Spot light: perspective camera at the light
                    // position along its direction, field of view
                    // from the cone cutoff, depth range fit to the
                    // scene bounding sphere.
                    bx::Vec3 eye(light.position[0],
                                 light.position[1],
                                 light.position[2]);
                    bx::mtxLookAt(lightViewMtx, eye,
                                  bx::add(eye, dir), up);
                    float d = bx::length(bx::sub(center, eye));
                    float far = d + r;
                    float near = bx::max(d - r, far * 1.0e-3f);
                    float fovy = bx::clamp(
                        2.0f * light.cutOffAngle, 0.02f, 3.1f)
                        * 180.0f / bx::kPi;
                    bx::mtxProj(lightProjMtx, fovy, 1.0f, near, far,
                                caps->homogeneousDepth);
                } else {
                    bx::Vec3 eye =
                        bx::sub(center, bx::mul(dir, 2.0f * r));
                    bx::mtxLookAt(lightViewMtx, eye, center, up);
                    bx::mtxOrtho(lightProjMtx, -r, r, -r, r,
                                 0.0f, 4.0f * r, 0.0f,
                                 caps->homogeneousDepth);
                }
                // Camera view space -> shadow map uv (xy) and light
                // window depth (z), the matrix the mesh shaders use.
                float invV[16], tmp[16], tmp2[16];
                bx::mtxInverse(invV,
                    reinterpret_cast<const float *>(viewMatrix));
                bx::mtxMul(tmp, invV, lightViewMtx);
                bx::mtxMul(tmp2, tmp, lightProjMtx);
                const float sy = caps->originBottomLeft ? 0.5f : -0.5f;
                const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
                const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
                const float crop[16] = {
                    0.5f, 0.0f, 0.0f, 0.0f,
                    0.0f, sy,   0.0f, 0.0f,
                    0.0f, 0.0f, sz,   0.0f,
                    0.5f, 0.5f, tz,   1.0f,
                };
                bx::mtxMul(view->shadowMtx, tmp2, crop);
            }
        }
    }
    // The targets themselves were reconciled with the rest of the
    // demand-allocated groups (EffectShadow) before anything read a
    // handle; a frame that wants a shadow but found no room for the
    // maps simply has none. shadowFrame follows that outcome rather
    // than the intent, so no consumer is told there is a map when the
    // allocation did not land.
    if (shadowActive)
        shadowActive = bgfx::isValid(view->shadowFbo);
    view->shadowFrame = shadowActive;
    // On RG32F the map stores plain (z, z^2) moments and the
    // receivers run Coin's exact VsmLookup — the GL Shadow style's
    // soft penumbra — at every SmoothBorder setting.
    // The reduced RG16F moment path (float32 not linearly filterable)
    // always runs the EVSM warp -- fp16 plain (z, z^2) moments lose too
    // much precision on the self-shadowed terminator, and the warp is what
    // makes RG16F usable.
    // The warp is fundamentally at odds with the blur: even a
    // scaled-down exponent reconstructs a near-binary edge from a
    // widely blurred moments map — exp(c·z) at the receiver dwarfs
    // the variance the blur added within a texel or two, and the
    // penumbra the blur paid for disappears. RG32F therefore stays
    // plain VSM (warp 0) with the blur too: blurring plain (z, z²)
    // widens the variance across the edge, which is exactly the
    // penumbra gradient. Only the reduced-precision RG16F path
    // keeps its fixed warp (plain fp16 moments are unusable on the
    // self-shadowed terminator), so its smoothing stays tighter.
    view->shadowWarpFrame =
        view->m_shadowForceWarp ? view->shadowWarp : 0.0f;
    view->shadowEpsilon = lightconf.epsilon;
    view->shadowThreshold = lightconf.threshold;
    // Coin's N-tap receiver spread kernel (ShadowSpreadSize /
    // SpreadSampleSize). The viewer packs both into the Coin
    // smoothBorder field as spread * 1e-6 + sample * 1e-2 digits;
    // Coin decodes swidth = (packed % 100000) * 5e-5 — i.e. the
    // spread wraps every 10000 (replicated for parity) — and taps
    // at coord + offset * swidth * 0.001 (spot lights * 0.1). The
    // viewer also shrinks a spot light's spread by 256 / the scene
    // extent on large scenes before packing (the backend uses the
    // raw scene bbox where the viewer's box includes the ground).
    {
        float spread = std::max(lightconf.spreadSize, 0.0f);
        spread = std::fmod(spread * 10.0f, 100000.0f) * 0.1f;
        if (lightconf.spot && bboxValid) {
            float maxSize = std::max(
                bboxMax[0] - bboxMin[0],
                std::max(bboxMax[1] - bboxMin[1],
                         bboxMax[2] - bboxMin[2]));
            if (maxSize > 256.0f)
                spread *= 256.0f / maxSize;
        }
        int sample = int(bx::clamp(lightconf.spreadSampleSize,
                                   0.0f, 7.0f) + 0.5f);
        float mode = 0.0f;
        if (spread > 0.0f)
            mode = sample >= 1
                ? float(std::min(2 * sample + 1, 8))
                : 1.0f;
        float sw = spread * 5.0e-4f * 0.001f;
        if (lightconf.spot)
            sw *= 0.1f;
        view->shadowSpreadUv = sw;
        view->shadowSpreadMode = mode;
    }
    static const bool dbgshadow =
        (getenv("FC_BGFX_DEBUG_SHADOW") != nullptr);
    if (dbgshadow)
        fprintf(stderr,
                "bgfx shadow active=%d valid=%d dir=%g,%g,%g"
                " ldirview=%g,%g,%g bbox=%d hd=%d obl=%d\n",
                shadowActive, lightconf.valid,
                lightconf.direction[0], lightconf.direction[1],
                lightconf.direction[2],
                view->lightDirView[0], view->lightDirView[1],
                view->lightDirView[2], bboxValid,
                bgfx::getCaps()->homogeneousDepth,
                bgfx::getCaps()->originBottomLeft);
    if (dbgshadow && shadowActive) {
        // Cross-check the receiver chain on the scene bbox center:
        // world -> camera view -> shadowMtx should equal
        // world -> lightView -> lightProj -> crop.
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        float w[4] = {(bboxMin[0] + bboxMax[0]) * 0.5f,
                      (bboxMin[1] + bboxMax[1]) * 0.5f,
                      (bboxMin[2] + bboxMax[2]) * 0.5f, 1.0f};
        auto xform = [](const float *m, const float *v, float *o) {
            for (int j = 0; j < 4; ++j)
                o[j] = v[0]*m[j] + v[1]*m[4+j] + v[2]*m[8+j]
                    + v[3]*m[12+j];
        };
        float vv[4], sp[4], lv[4], lp[4];
        xform(vm, w, vv);
        xform(view->shadowMtx, vv, sp);
        xform(lightViewMtx, w, lv);
        xform(lightProjMtx, lv, lp);
        fprintf(stderr,
                "bgfx shadow chk sp=%g,%g,%g,%g direct ndc=%g,%g,%g"
                " -> uvz=%g,%g,%g\n",
                sp[0], sp[1], sp[2], sp[3], lp[0], lp[1], lp[2],
                lp[0]*0.5f + 0.5f, lp[1]*0.5f + 0.5f,
                lp[2]*0.5f + 0.5f);
    }

    // Zero radius = automatic: a fraction of the scene bounding
    // sphere, the scale-free default.
    float aoRadius = aoconf.radius;
    if (ssaoActive && aoRadius <= 0.0f) {
        if (bboxValid) {
            float dx = bboxMax[0] - bboxMin[0];
            float dy = bboxMax[1] - bboxMin[1];
            float dz = bboxMax[2] - bboxMin[2];
            aoRadius = 0.05f * std::sqrt(dx*dx + dy*dy + dz*dz);
        }
        if (aoRadius <= 0.0f)
            ssaoActive = false;
    }
    // The mesh programs fold the AO chain result into their
    // ambient/headlight/IBL terms (sampled at unit 9, bound per
    // draw in setTriangleFrameState). GTAO's denoise ping-pongs
    // its final result back into aoTex; the classic path ends in
    // aoBlurTex. Invalid = AO off, the white stand-in binds.
    view->aoMeshTex = BGFX_INVALID_HANDLE;
    if (ssaoActive)
        view->aoMeshTex = aoconf.method == 1
                && bgfx::isValid(view->m_progGtao)
                && bgfx::isValid(view->m_progGtaoBlur)
            ? view->aoTex : view->aoBlurTex;

    // Volumetric light shafts raymarch the shadow map with ray ends
    // from the SSAO prepass, so they need the shadow pass active
    // this frame; hidden-line mode disables them like the other
    // shading effects. The medium is a sphere around the scene
    // bounds — bounding it keeps the camera's stand-off distance
    // out of the optical depth.
    // isValid(volFbo): the targets are demand-allocated, so "the
    // config wants volumetrics" and "the targets exist" are no longer
    // the same statement -- a pool that had nothing left leaves the
    // group unbuilt and every volumetric pass simply does not run.
    bool volActive = view->m_vol && volconf.enabled && shadowActive
        && !hlconfig.show && bgfx::isValid(view->volFbo)
        && bgfx::isValid(view->aoPrepassFbo);
    float volDensity = volconf.density;
    float volMaxDist = 0.0f;
    float volMedium[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (volActive) {
        float dx = bboxMax[0] - bboxMin[0];
        float dy = bboxMax[1] - bboxMin[1];
        float dz = bboxMax[2] - bboxMin[2];
        float diag = std::sqrt(dx*dx + dy*dy + dz*dz);
        // Medium sphere: the scene bounding sphere with a margin
        // (shafts reach a bit of the surrounding ground plane).
        float r = 0.75f * diag;
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        float c[3] = {(bboxMin[0] + bboxMax[0]) * 0.5f,
                      (bboxMin[1] + bboxMax[1]) * 0.5f,
                      (bboxMin[2] + bboxMax[2]) * 0.5f};
        for (int j = 0; j < 3; ++j)
            volMedium[j] = c[0] * vm[j] + c[1] * vm[4 + j]
                + c[2] * vm[8 + j] + vm[12 + j];
        volMedium[3] = r;
        volMaxDist = std::sqrt(volMedium[0] * volMedium[0]
                               + volMedium[1] * volMedium[1]
                               + volMedium[2] * volMedium[2]) + r;
        // Zero density = automatic: unit optical depth over the
        // medium radius, the scale-free default.
        if (volDensity <= 0.0f)
            volDensity = 1.0f / r;
        if (getenv("FC_BGFX_DEBUG_VOL"))
            fprintf(stderr,
                    "bgfx vol density=%g maxdist=%g medium="
                    "%g,%g,%g r=%g\n",
                    volDensity, volMaxDist, volMedium[0],
                    volMedium[1], volMedium[2], volMedium[3]);
    }
    // Water medium: scene draws flagged Material::water become
    // water bodies of the volumetric pass. Each body (object key)
    // gets an appearance slot — extinction sigma from its diffuse
    // color (absorption of the complement) plus a
    // wavelength-independent scattering term, density auto = from
    // its own bounds. The interval depth writer stamps the slot per
    // pixel; bodies beyond the slot count share slot 0's
    // appearance. slotOf resolves a draw's slot at submit time.
    constexpr int kSlots = BGFXView::kMediumSlots;
    bool waterActive = false;
    bool hasWaterBody = false;
    float waterSigma[kSlots][4] = {};
    float waterDiagSlot[kSlots] = {};
    float waterDiag = 0.0f;   // first body's, for the wave-scale auto
    float waterPlaneZ = 0.0f; // top of the water body/bodies, the plane
    bool waterPlaneSet = false; // the planar reflection mirrors about
    // World xy the water covers, which is the footprint the particle
    // impact map is framed on: {xmin, ymin, xmax, ymax}. Every body
    // counts, since a droplet may land in any of them.
    float waterFoot[4] = {FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX};
    std::unordered_map<uint64_t, int> waterSlots;
    auto slotOf = [](const std::unordered_map<uint64_t, int> &slots,
                     uint64_t key) {
        auto it = slots.find(key);
        return it == slots.end() ? 0 : it->second;
    };
    int waterSlotCount = 0;
    for (const auto &draw : scene) {
        const auto &mat = draw.material;
        if (!mat.water || mat.ontop
                || mat.type != Render::Material::Triangle)
            continue;
        // Footprint before the slot bookkeeping: a body past the
        // slot budget, or a second draw of one already slotted,
        // still holds water a droplet can land in.
        waterFoot[0] = std::min(waterFoot[0], draw.bboxMin[0]);
        waterFoot[1] = std::min(waterFoot[1], draw.bboxMin[1]);
        waterFoot[2] = std::max(waterFoot[2], draw.bboxMax[0]);
        waterFoot[3] = std::max(waterFoot[3], draw.bboxMax[1]);
        if (waterSlots.count(draw.objectKey))
            continue;
        if (waterSlotCount >= kSlots) {
            waterSlots.emplace(draw.objectKey, 0);
            continue;
        }
        int slot = waterSlotCount++;
        waterSlots.emplace(draw.objectKey, slot);
        float diag;
        {
            float dx = draw.bboxMax[0] - draw.bboxMin[0];
            float dy = draw.bboxMax[1] - draw.bboxMin[1];
            float dz = draw.bboxMax[2] - draw.bboxMin[2];
            diag = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
        waterDiagSlot[slot] = diag;
        if (waterDiag <= 0.0f)
            waterDiag = diag;
        if (!waterPlaneSet || draw.bboxMax[2] > waterPlaneZ) {
            waterPlaneZ = draw.bboxMax[2];
            waterPlaneSet = true;
        }
        hasWaterBody = true;
        float dens = mat.waterdensity;
        if (dens <= 0.0f) {
            if (diag <= 0.0f)
                continue;   // degenerate body: slot stays a no-op
            dens = 3.0f / diag;
        }
        float color[4];
        unpackAuthoredColor(mat.diffuse, color, view->colorManaged());
        float sigmaS = 0.35f * dens;
        for (int j = 0; j < 3; ++j)
            waterSigma[slot][j] = sigmaS + dens * (1.0f - color[j]);
        waterSigma[slot][3] = sigmaS;
        // The volumetric water medium needs the volumetric pass; the
        // body detection itself also serves the surface pass below.
        waterActive = volActive;
        if (volActive && getenv("FC_BGFX_DEBUG_VOL"))
            fprintf(stderr,
                    "bgfx water slot=%d dens=%g sigma=%g,%g,%g s=%g\n",
                    slot, dens, waterSigma[slot][0],
                    waterSigma[slot][1], waterSigma[slot][2],
                    waterSigma[slot][3]);
    }
    // Water surface (refraction/reflection) pass: independent of the
    // volumetric medium — it only needs a water body, the scene copy
    // resources, and the environment for the reflection. Hidden-line
    // mode disables it like the other shading effects.
    bool waterSurfActive = waterconf.enabled && hasWaterBody
        && !hlconfig.show
        && bgfx::isValid(view->m_progWater)
        && bgfx::isValid(view->sceneCopyFbo);
    if (waterSurfActive) {
        view->ensureEnvironment();
        if (!bgfx::isValid(view->m_envTex))
            waterSurfActive = bgfx::isValid(view->m_dummyEnvTex);
    }
    // Glass bodies: found with the SSAO group's demand above, since
    // that is what a glass body asks for when nothing else does.
    bool glassActive = hasGlassBody && !hlconfig.show
        && view->m_ssao
        && bgfx::isValid(view->m_progGlass)
        && bgfx::isValid(view->glassFrontFbo)
        && bgfx::isValid(view->sceneCopyFbo);
    if (glassActive) {
        view->ensureEnvironment();
        if (!bgfx::isValid(view->m_envTex))
            glassActive = bgfx::isValid(view->m_dummyEnvTex);
    }
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr, "bgfx glass: body=%d active=%d\n",
                hasGlassBody, glassActive);
    // Body-local "up" frame shared by the fire taper and fountain
    // flow frames: up = the model placement's local z axis (world z
    // for identity transforms), the model x axis Gram-Schmidt'd
    // into a lateral right vector. The world AABB corners projected
    // onto the basis give the bottom-center origin, the up-extent
    // and the max lateral half-extent (conservative for tilted
    // bodies -- a flow frame, not a fit). Row-vector convention
    // like the shadow matrix: lp = [wp, 1] * M with the frame axes
    // as columns.
    auto buildBodyFrame = [](const Render::DrawCall &draw,
                             float frame[16], float base[3],
                             float up[3], float &height,
                             float &radius) -> bool {
        for (int j = 0; j < 3; ++j) {
            base[j] = 0.0f;
            up[j] = 0.0f;
        }
        height = 0.0f;
        radius = 0.0f;
        float u[3] = {0.0f, 0.0f, 1.0f};
        float r[3] = {1.0f, 0.0f, 0.0f};
        if (!draw.identity) {
            for (int j = 0; j < 3; ++j) {
                u[j] = draw.model[8 + j];
                r[j] = draw.model[j];
            }
            float ul = std::sqrt(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
            if (ul > 1.0e-6f)
                for (int j = 0; j < 3; ++j)
                    u[j] /= ul;
            else
                u[0] = 0.0f, u[1] = 0.0f, u[2] = 1.0f;
            float ru = r[0]*u[0] + r[1]*u[1] + r[2]*u[2];
            for (int j = 0; j < 3; ++j)
                r[j] -= ru * u[j];
            float rl = std::sqrt(r[0]*r[0] + r[1]*r[1] + r[2]*r[2]);
            if (rl > 1.0e-6f) {
                for (int j = 0; j < 3; ++j)
                    r[j] /= rl;
            } else {
                // up nearly parallel to the model x axis: any
                // stable perpendicular does for the lateral frame.
                r[0] = -u[2]; r[1] = 0.0f; r[2] = u[0];
                rl = std::sqrt(r[0]*r[0] + r[2]*r[2]);
                if (rl > 1.0e-6f) {
                    r[0] /= rl; r[2] /= rl;
                } else {
                    r[0] = 1.0f; r[2] = 0.0f;
                }
            }
        }
        float f[3] = {u[1]*r[2] - u[2]*r[1],
                      u[2]*r[0] - u[0]*r[2],
                      u[0]*r[1] - u[1]*r[0]};
        float pmin[3], pmax[3];
        for (int c = 0; c < 8; ++c) {
            float wp[3] = {
                (c & 1) ? draw.bboxMax[0] : draw.bboxMin[0],
                (c & 2) ? draw.bboxMax[1] : draw.bboxMin[1],
                (c & 4) ? draw.bboxMax[2] : draw.bboxMin[2]};
            float pr[3] = {
                wp[0]*r[0] + wp[1]*r[1] + wp[2]*r[2],
                wp[0]*f[0] + wp[1]*f[1] + wp[2]*f[2],
                wp[0]*u[0] + wp[1]*u[1] + wp[2]*u[2]};
            for (int j = 0; j < 3; ++j) {
                if (c == 0 || pr[j] < pmin[j]) pmin[j] = pr[j];
                if (c == 0 || pr[j] > pmax[j]) pmax[j] = pr[j];
            }
        }
        height = pmax[2] - pmin[2];
        radius = 0.5f * std::max(pmax[0] - pmin[0],
                                 pmax[1] - pmin[1]);
        // Bottom center of the body in the frame, back in world
        // coordinates (r/f/u are an orthonormal world basis).
        float rc = 0.5f * (pmin[0] + pmax[0]);
        float fc = 0.5f * (pmin[1] + pmax[1]);
        for (int j = 0; j < 3; ++j) {
            base[j] = rc * r[j] + fc * f[j] + pmin[2] * u[j];
            up[j] = u[j];
            frame[j * 4 + 0] = r[j];
            frame[j * 4 + 1] = f[j];
            frame[j * 4 + 2] = u[j];
            frame[j * 4 + 3] = 0.0f;
        }
        frame[12] = -rc;
        frame[13] = -fc;
        frame[14] = -pmin[2];
        frame[15] = 1.0f;
        return height > 1.0e-6f && radius > 1.0e-6f;
    };
    // Cloud bodies (Material::cloud): the closed volume raymarches
    // as a procedural-density medium of the volumetric pass and the
    // geometry itself is not rendered. Each body gets an appearance
    // slot like the water medium (bodies beyond the slot count
    // share slot 0); density/detail auto = from the body's own
    // bounds. Fountain bodies (Material::fountain) share the cloud
    // medium channel -- same interval targets and slots -- with the
    // slot's w flagging the fountain density field (2.0) instead of
    // the cloud FBM (1.0), plus a flow frame and geometry entry.
    bool hasCloudBody = false;
    // Per slot: x = density, y = detail, z = speed, w = flavor
    // (0 = inactive, 1 = cloud, 2 = fountain).
    float cloudSlot[kSlots][4] = {};
    float fountainFrame[kSlots][16] = {};
    // Per slot: x = 1/height, y = 1/lateral radius.
    float fountainGeom[kSlots][4] = {};
    // Per slot splash source for the water surface: xyz = world
    // base center, w = impact ring radius (0 = inactive).
    float fountainSplash[kSlots][4] = {};
    std::unordered_map<uint64_t, int> cloudSlots;
    int cloudSlotCount = 0;
    // Cloud slots bound to a user "volume"-stage scatter medium
    // (docs/RenderEngine.md §5.11) — feeds the spliced program
    // variants assembled with the fire-slot users below.
    std::shared_ptr<const Render::UserShader> cloudSlotUser[kSlots];
    for (const auto &draw : scene) {
        const auto &mat = draw.material;
        if ((!mat.cloud && !mat.fountain) || mat.ontop
                || mat.type != Render::Material::Triangle)
            continue;
        if (cloudSlots.count(draw.objectKey))
            continue;
        if (cloudSlotCount >= kSlots) {
            cloudSlots.emplace(draw.objectKey, 0);
            continue;
        }
        int slot = cloudSlotCount++;
        cloudSlots.emplace(draw.objectKey, slot);
        float dx = draw.bboxMax[0] - draw.bboxMin[0];
        float dy = draw.bboxMax[1] - draw.bboxMin[1];
        float dz = draw.bboxMax[2] - draw.bboxMin[2];
        float diag = (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f)
            ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
        if (mat.fountain) {
            // Fountain body: spray density field in the body's
            // flow frame; denser auto defaults than the cloud
            // (spray is a tight plume, not a room-filling puff).
            float height = 0.0f, radius = 0.0f;
            float base[3], up[3];
            if (!buildBodyFrame(draw, fountainFrame[slot], base,
                                up, height, radius))
                continue;   // degenerate: slot stays inactive
            float density = mat.fountaindensity;
            if (density <= 0.0f && diag > 0.0f)
                density = 40.0f / diag;
            float detail = mat.fountaindetail;
            if (detail <= 0.0f && diag > 0.0f)
                detail = 12.0f / diag;
            if (density <= 0.0f || detail <= 0.0f)
                continue;
            cloudSlot[slot][0] = density;
            cloudSlot[slot][1] = detail;
            cloudSlot[slot][2] = mat.fountainspeed;
            cloudSlot[slot][3] = 2.0f;
            fountainGeom[slot][0] = 1.0f / height;
            fountainGeom[slot][1] = 1.0f / radius;
            fountainSplash[slot][0] = base[0];
            fountainSplash[slot][1] = base[1];
            fountainSplash[slot][2] = base[2];
            fountainSplash[slot][3] = radius * 0.85f;
            hasCloudBody = true;
            if (getenv("FC_BGFX_DEBUG_FEED"))
                fprintf(stderr,
                        "bgfx fountain slot=%d dens=%g detail=%g"
                        " h=%g r=%g\n",
                        slot, density, detail, height, radius);
            continue;
        }
        float density = mat.clouddensity;
        if (density <= 0.0f && diag > 0.0f)
            density = 6.0f / diag;
        float detail = mat.clouddetail;
        if (detail <= 0.0f && diag > 0.0f)
            detail = 4.0f / diag;
        if (density <= 0.0f || detail <= 0.0f)
            continue;   // degenerate body: slot stays inactive
        cloudSlot[slot][0] = density;
        cloudSlot[slot][1] = detail;
        cloudSlot[slot][2] = mat.cloudspeed;
        cloudSlot[slot][3] = 1.0f;
        hasCloudBody = true;
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr,
                    "bgfx cloud slot=%d dens=%g detail=%g\n",
                    slot, density, detail);
    }
    bool cloudActive = hasCloudBody && volActive;
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr, "bgfx cloud: body=%d active=%d\n",
                hasCloudBody, cloudActive);
    // The cloud body's own draws (fills and feature lines) are
    // suppressed entirely while the medium renders, matched by
    // object key like the water surface line suppression.
    std::unordered_set<uint64_t> cloudObjects;
    if (cloudActive) {
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if ((mat.cloud || mat.fountain) && !mat.ontop
                    && draw.objectKey
                    && mat.type == Render::Material::Triangle)
                cloudObjects.insert(draw.objectKey);
        }
    }
    // Fire bodies (Material::fire): the closed volume raymarches as
    // an emissive flame medium of the volumetric pass and the
    // geometry itself is not rendered. Each body gets an appearance
    // slot like the water/cloud media (bodies beyond the slot count
    // share slot 0), carrying its own emission/detail/speed, taper
    // frame and effect-light anchor.
    struct FireSlot {
        float emission = 0.0f, detail = 0.0f, speed = 1.0f;
        float invHeight = 0.0f, invRadius = 0.0f, soot = 0.0f;
        float intensity = 1.0f, diag = 0.0f;
        // World -> fire-local frame (z = the body placement's up
        // axis, origin at the bottom center) and the effect-light
        // anchor.
        float frame[16] = {1.0f, 0.0f, 0.0f, 0.0f,
                           0.0f, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, 1.0f, 0.0f,
                           0.0f, 0.0f, 0.0f, 1.0f};
        float lightWorld[3] = {0.0f, 0.0f, 0.0f};
        bool valid = false;
    };
    bool hasFireBody = false;
    FireSlot fireSlot[kSlots];
    std::unordered_map<uint64_t, int> fireSlots;
    int fireSlotCount = 0;
    // Fire slots bound to a user "volume"-stage medium function
    // (docs/RenderEngine.md §5.11) — feeds the spliced program
    // variants assembled after the loop.
    std::shared_ptr<const Render::UserShader> fireSlotUser[kSlots];
    for (const auto &draw : scene) {
        const auto &mat = draw.material;
        if (!mat.fire || mat.ontop
                || mat.type != Render::Material::Triangle)
            continue;
        if (fireSlots.count(draw.objectKey))
            continue;
        if (fireSlotCount >= kSlots) {
            fireSlots.emplace(draw.objectKey, 0);
            continue;
        }
        FireSlot &fs = fireSlot[fireSlotCount];
        fireSlots.emplace(draw.objectKey, fireSlotCount++);
        float dx = draw.bboxMax[0] - draw.bboxMin[0];
        float dy = draw.bboxMax[1] - draw.bboxMin[1];
        float dz = draw.bboxMax[2] - draw.bboxMin[2];
        float diag = (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f)
            ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
        float intensity = mat.fireintensity > 0.0f
            ? mat.fireintensity : 1.0f;
        if (diag > 0.0f)
            fs.emission = intensity * 4.0f / diag;
        fs.detail = mat.firedetail;
        if (fs.detail <= 0.0f && diag > 0.0f)
            fs.detail = 5.0f / diag;
        fs.speed = mat.firespeed;
        fs.intensity = intensity;
        fs.diag = diag;
        // Mild soot absorption scaled like the other auto
        // densities; FC_BGFX_NO_FIRESOOT keeps the old purely
        // additive flame for A/B comparisons.
        static const bool noSoot =
            (getenv("FC_BGFX_NO_FIRESOOT") != nullptr);
        if (!noSoot && diag > 0.0f)
            fs.soot = 1.5f / diag;
        // The taper frame comes from the body's placement (shared
        // buildBodyFrame helper above).
        float base[3], up[3];
        float height = 0.0f, radius = 0.0f;
        if (buildBodyFrame(draw, fs.frame, base, up, height,
                           radius)) {
            fs.invHeight = 1.0f / height;
            fs.invRadius = 1.0f / radius;
        }
        // The effect light sits a third up the flame — the ramp's
        // bright zone — along the body's up axis.
        for (int j = 0; j < 3; ++j)
            fs.lightWorld[j] = base[j] + 0.35f * height * up[j];
        fs.valid = fs.emission > 0.0f && fs.detail > 0.0f
            && fs.invHeight > 0.0f;
        hasFireBody = hasFireBody || fs.valid;
        if (getenv("FC_BGFX_DEBUG_FEED"))
            fprintf(stderr,
                    "bgfx fire slot=%d valid=%d emit=%g detail=%g\n",
                    int(&fs - fireSlot), fs.valid, fs.emission,
                    fs.detail);
    }
    bool fireActive = hasFireBody && volActive;
    // Assemble (or drop) the user medium splice variants when the
    // slot→user-source tuple changed since the last frame; the
    // compile itself is async through the shared user-shader cache,
    // stock media stand in until the binaries land.
    {
        if (fireActive || cloudActive)
            collectMediumUsers(scene, fireSlotUser, cloudSlotUser,
                               kSlots);
        if (!fireActive)
            for (int i = 0; i < kSlots; ++i)
                fireSlotUser[i].reset();
        if (!cloudActive)
            for (int i = 0; i < kSlots; ++i)
                cloudSlotUser[i].reset();
        std::array<const void *, 8> key {};
        for (int i = 0; i < kSlots && i < 4; ++i) {
            key[i] = fireSlotUser[i].get();
            key[i + 4] = cloudSlotUser[i].get();
        }
        // A republished splice table is a new chance to adopt what
        // an earlier, binary-less one could not.
        size_t fingerprint = usershaderconf.splices.size();
        for (const auto &sp : usershaderconf.splices)
            fingerprint = fingerprint * 131u + sp.compiled.size();
        const bool retry = !view->volUserAdopted
            && fingerprint != view->volSpliceFingerprint;
        view->volSpliceFingerprint = fingerprint;
        if (key != view->volUserKey || retry) {
            view->volUserKey = key;
            view->volUserVol = assembleMediumVariant(
                "fc_volume_fs.sh", fireSlotUser, cloudSlotUser,
                kSlots);
            view->volUserExt = assembleMediumVariant(
                "fc_volume_ext_fs.sh", fireSlotUser, cloudSlotUser,
                kSlots);
            view->volUserRefl = assembleMediumVariant(
                "fc_refl_media_fs.sh", fireSlotUser, cloudSlotUser,
                kSlots);
#ifdef FC_RENDERER_STANDALONE
            // Compiler-less tier: adopt the snapshot-shipped
            // assembled variant (server-compiled binaries) when its
            // source matches what was just assembled — source
            // equality guarantees the same slot binding
            // (docs/RenderEngine.md §5.11 transport).
            // The server compiles the viewer binaries asynchronously
            // and republishes when they land, so the first snapshot
            // after a binding — a cold shader cache above all — can
            // carry the splice with no binaries at all. Adopting
            // that empty variant must not be final: a binary-less
            // splice keeps the stock stand-in and marks the
            // adoption incomplete, so the next republished table
            // is tried again instead of leaving the medium stock
            // for the rest of the session.
            bool usable = true;
            // Usable means "carries a binary THIS tier can load":
            // the variants are compiled per target and the loader
            // matches on the profile label, so a table holding only
            // the other tier's binary is as unusable as an empty
            // one and must be retried the same way.
            std::string plat, prof, apiDir;
            const bool haveTarget = shadercTarget(plat, prof, apiDir);
            auto adopt =
                [this, &usable, &prof, haveTarget](
                        std::shared_ptr<const Render::UserShader> &s,
                        const char *tag) {
                if (!s)
                    return;
                for (const auto &sp : usershaderconf.splices) {
                    if (sp.fragmentSource == s->fragmentSource) {
                        bool mine = false;
                        for (const auto &c : sp.compiled)
                            if (!haveTarget
                                    || (c.profile == prof
                                        && !c.fsBin.empty())) {
                                mine = true;
                                break;
                            }
                        std::printf("fcviewer: splice %s adopted "
                                    "(%zu bins, %s)\n",
                                    tag, sp.compiled.size(),
                                    mine ? "mine" : "none for this tier");
                        if (!mine)
                            usable = false;
                        else
                            s = std::make_shared<Render::UserShader>(sp);
                        return;
                    }
                }
                std::printf("fcviewer: splice %s NOT shipped "
                            "(%zu candidates)\n",
                            tag, usershaderconf.splices.size());
                usable = false;
            };
            adopt(view->volUserVol, "vol");
            adopt(view->volUserExt, "ext");
            adopt(view->volUserRefl, "refl");
            view->volUserAdopted = usable;
#endif
        }
    }
    // Time-animated content (fire flicker, cloud drift, water waves,
    // caustics): a client's idle frame skip must keep rendering
    // while any of these replay per frame; everything else in the
    // frame is camera/scene-driven and a repeat frame is identical.
    sceneAnimated = fireActive || cloudActive || waterSurfActive
        || (volActive && waterActive);
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr, "bgfx fire: body=%d active=%d\n",
                hasFireBody, fireActive);
    // The fire body's own draws (fills and feature lines) are
    // suppressed entirely while the medium renders, matched by
    // object key like the cloud body.
    std::unordered_set<uint64_t> fireObjects;
    if (fireActive) {
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (mat.fire && !mat.ontop && draw.objectKey
                    && mat.type == Render::Material::Triangle)
                fireObjects.insert(draw.objectKey);
        }
    }
    // The water body's edge/vertex draws are suppressed while the
    // surface renders (a water surface has no CAD feature lines, and
    // the black edges would smear through the screen-space
    // refraction). The line/point draws don't reliably carry the
    // material's water flag (node-order dependent capture), so they
    // are matched by the body's object key.
    std::unordered_set<uint64_t> waterSurfObjects;
    if (waterSurfActive) {
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (mat.water && !mat.ontop && draw.objectKey
                    && mat.type == Render::Material::Triangle
                    && mat.numclipplanes == 0)
                waterSurfObjects.insert(draw.objectKey);
        }
    }
    // Shared animation clock of the water effects (caustics, surface
    // waves); FC_BGFX_CAUSTIC_TIME freezes it for deterministic
    // comparisons. animating() reports live animation so the viewer
    // keeps scheduling redraws.
    animatedFrame = false;
    float animTime = 0.0f;
    bool animLive = false;
    static const char *fixedTime = getenv("FC_BGFX_CAUSTIC_TIME");
    if (fixedTime) {
        animTime = float(atof(fixedTime));
    } else {
        using animclock = std::chrono::steady_clock;
        static const animclock::time_point start = animclock::now();
        animTime = std::chrono::duration<float>(
                       animclock::now() - start).count();
        animLive = true;
    }
    // The RenderDebug freeze-frame determinism switch
    // (docs/RenderDebug.md): a frozen clock renders every
    // time-animated effect (water waves, fire, caustics, splashes)
    // at t = 0, and animLive stays false so idle viewers stop
    // re-rendering — two frames of the same scene/camera/params
    // are then identical.
    if (debugconf.freezeFrame) {
        animTime = 0.0f;
        animLive = false;
    }
    // Publish the clock to user shaders: u_fcTime is recorded with
    // every consuming user draw (pushUserParams), and a user shader
    // referencing it keeps the animation loop alive like the stock
    // timed effects below.
    _BGFXLib.userTime[0] = animTime;
    _BGFXLib.userTime[1] = animLive ? 1.0f : 0.0f;
    _BGFXLib.userAnimatedDraw = false;
    // Fire lights the scene: an unshadowed point light per fire
    // body slot at the flame centroid, its brightness flickered on
    // the shared animation clock (frozen clocks stay deterministic)
    // and its color the flame ramp's bright zone. The mesh FS adds
    // them on top of the frame's lighting model — no shadow maps
    // from them, the usual engine effect-light shortcut.
    static const bool noFireLight =
        (getenv("FC_BGFX_NO_FIRELIGHT") != nullptr);
    for (int slot = 0; slot < kSlots; ++slot) {
        const FireSlot &fs = fireSlot[slot];
        if (!(fireActive && !noFireLight && fs.valid
              && fs.diag > 0.0f)) {
            view->localLightView[slot][3] = 0.0f;
            continue;
        }
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        // The light anchor (a third up the flame along the body's
        // up axis, the ramp's bright zone) came out of the fire
        // scan's taper frame.
        for (int j = 0; j < 3; ++j)
            view->localLightView[slot][j] = fs.lightWorld[0] * vm[j]
                + fs.lightWorld[1] * vm[4 + j]
                + fs.lightWorld[2] * vm[8 + j]
                + vm[12 + j];
        float range = 2.5f * fs.diag;
        view->localLightView[slot][3] = 1.0f / (range * range);
        // Flicker: a few incommensurate sines on the flame clock
        // (same 2.0 rise rate as the noise scroll), amplitude kept
        // above zero so the fire never blacks out. The slot index
        // offsets the phases so several fires don't pulse in step.
        float t = animTime * fs.speed * 2.0f + 3.1f * float(slot);
        float flicker = 0.80f
            + 0.20f * (0.55f * std::sin(t * 11.7f)
                       + 0.33f * std::sin(t * 7.3f + 1.7f)
                       + 0.12f * std::sin(t * 23.9f + 0.5f));
        float glow = fs.intensity * flicker;
        // fireRamp(0.6) of the volume shader: the flame's dominant
        // orange.
        view->localLightColorI[slot][0] = 1.00f * glow;
        view->localLightColorI[slot][1] = 0.72f * glow;
        view->localLightColorI[slot][2] = 0.13f * glow;
        view->localLightColorI[slot][3] = 0.0f;
    }
    // Light-source bodies (Render_Light): the upper half of the
    // local-light array — an unshadowed point light at each body's
    // bounds center, color = diffuse * intensity, steady (no
    // flicker). The draws are also collected for the bloom emit
    // pass (their HDR halo source re-render).
    std::vector<const Render::DrawCall *> bulbDraws;
    constexpr int kBulbSlots = BGFXView::kLocalLights
        - BGFXView::kMediumSlots;
    float bulbPos[kBulbSlots][3] = {};
    float bulbRangeW[kBulbSlots] = {};
    float bulbDiagW[kBulbSlots] = {};
    bool bulbWantShadow[kBulbSlots] = {};
    bool bulbWantShadowExt[kBulbSlots] = {};
    int bulbCount = 0;
    {
        int slot = 0;
        std::unordered_set<uint64_t> bulbObjects;
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (!mat.lightsource || mat.ontop
                    || mat.type != Render::Material::Triangle)
                continue;
            bulbDraws.push_back(&draw);
            if (slot >= BGFXView::kLocalLights
                            - BGFXView::kMediumSlots)
                continue;
            // One light per object (a body's face/edge draws share
            // the object key).
            if (draw.objectKey
                    && !bulbObjects.insert(draw.objectKey).second)
                continue;
            float dx = draw.bboxMax[0] - draw.bboxMin[0];
            float dy = draw.bboxMax[1] - draw.bboxMin[1];
            float dz = draw.bboxMax[2] - draw.bboxMin[2];
            float diag = (dx >= 0.0f && dy >= 0.0f && dz >= 0.0f)
                ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
            if (diag <= 0.0f)
                continue;
            float cx = 0.5f * (draw.bboxMin[0] + draw.bboxMax[0]);
            float cy = 0.5f * (draw.bboxMin[1] + draw.bboxMax[1]);
            float cz = 0.5f * (draw.bboxMin[2] + draw.bboxMax[2]);
            float intensity = mat.lightintensity > 0.0f
                ? mat.lightintensity : 1.0f;
            float range = mat.lightrange > 0.0f ? mat.lightrange
                                                : 8.0f * diag;
            bulbPos[slot][0] = cx;
            bulbPos[slot][1] = cy;
            bulbPos[slot][2] = cz;
            bulbRangeW[slot] = range;
            bulbDiagW[slot] = diag;
            bulbWantShadow[slot] = mat.lightshadow;
            bulbWantShadowExt[slot] = mat.lightshadowext;
            bulbCount = slot + 1;
            int li = BGFXView::kMediumSlots + slot++;
            const float *vm =
                reinterpret_cast<const float *>(viewMatrix);
            for (int j = 0; j < 3; ++j)
                view->localLightView[li][j] = cx * vm[j]
                    + cy * vm[4 + j] + cz * vm[8 + j] + vm[12 + j];
            view->localLightView[li][3] = 1.0f / (range * range);
            float color[4];
            unpackAuthoredColor(mat.diffuse, color, view->colorManaged());
            for (int j = 0; j < 3; ++j)
                view->localLightColorI[li][j] = color[j] * intensity;
            view->localLightColorI[li][3] = 0.0f;
        }
        for (; slot < BGFXView::kLocalLights - BGFXView::kMediumSlots;
             ++slot)
            view->localLightView[BGFXView::kMediumSlots + slot][3]
                = 0.0f;
    }
    // Demand-allocated, like the volumetric set: an unbuilt chain
    // leaves the passes out of the frame rather than binding nothing.
    bool bloomActive = bloomconf.enabled && !externalBase
        && bgfx::isValid(view->bloomFbo);
    float waterWaveStrength = waterconf.waveStrength;
    float waterWaveScale = waterconf.waveScale;
    if (waterWaveScale <= 0.0f)
        waterWaveScale = waterDiag > 0.0f ? 4.0f / waterDiag : 1.0f;
    float waterSurfTime = animTime * waterconf.waveSpeed;
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr,
                "bgfx water surf: conf=%d body=%d active=%d prog=%d"
                " scale=%g strength=%g time=%g\n",
                waterconf.enabled, hasWaterBody, waterSurfActive,
                bgfx::isValid(view->m_progWater), waterWaveScale,
                waterWaveStrength, waterSurfTime);
    // Cached shadow map: the moments in shadowTex stay valid while
    // the light camera, the smoothing, and the caster set (mesh
    // content, transforms, ranges, clipping) are unchanged — camera
    // moves don't touch them, so the caster pass and blur only
    // re-run on scene/light edits (the large-assembly policy of the
    // GL Shadow style's cached SoShadowGroup map). The hash loop
    // mirrors the caster predicate of the submit loop below; the
    // hidden-line style adds fill-hiding rules resolved later, so
    // it just disables the caching.
    static const bool shadowNoCache =
        (getenv("FC_BGFX_SHADOW_NOCACHE") != nullptr);
    // Water bodies neither cast shadows (the medium needs the light
    // inside) nor act as ordinary surfaces while either water mode
    // (volumetric medium or surface) is active. Glass bodies are
    // exempt the same way while the glass pass runs (a tinting
    // colored-shadow approximation is future work — light passes
    // through for now).
    bool waterExempt = waterActive || waterSurfActive;
    auto mediumExempt = [&](const Render::Material &mat) {
        return (waterExempt && mat.water)
            || (glassActive && mat.glass)
            || (cloudActive && (mat.cloud || mat.fountain))
            || (fireActive && mat.fire);
    };
    // Caster-set content hash, shared by the scene shadow map and
    // the bulb shadow tiles (both re-render only when it changes).
    auto hashCasters = [&](uint64_t &h) {
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            // On-top draws cast too: a selected-on-top object's
            // scene draws are hidden and re-rendered on top, so
            // excluding them dropped its whole shadow (GL keeps it).
            // Glass draws stay in the hash: they feed the tint
            // map beside the moments (with their color).
            if (mat.type != Render::Material::Triangle
                    || !(mat.shadowstyle & 1)
                    || (waterExempt && mat.water)
                    || (cloudActive && (mat.cloud || mat.fountain))
                    || (fireActive && mat.fire)
                    || !draw.mesh)
                continue;
            if (glassActive && mat.glass)
                hashBytes(h, &mat.diffuse, sizeof(mat.diffuse));
            hashBytes(h, &draw.mesh->cacheId,
                      sizeof(draw.mesh->cacheId));
            if (!draw.identity)
                hashBytes(h, draw.model, sizeof(float) * 16);
            hashBytes(h, &draw.indexStart, sizeof(draw.indexStart));
            hashBytes(h, &draw.indexCount, sizeof(draw.indexCount));
            if (mat.numclipplanes) {
                hashBytes(h, &mat.numclipplanes, 1);
                hashBytes(h, &mat.clipconcave, 1);
                hashBytes(h, mat.clipplanes,
                          sizeof(float) * 4 * mat.numclipplanes);
            }
            // Autozoom casters rebuild their transform from the
            // per-frame world-to-screen scale.
            if (!mat.autozoom.empty())
                hashBytes(h, &autozoomScale, sizeof(autozoomScale));
        }
    };
    bool shadowRender = shadowActive;
    if (shadowActive && !hlconfig.show && !shadowNoCache) {
        uint64_t h = 1469598103934665603ULL;
        hashBytes(h, lightViewMtx, sizeof(float) * 16);
        hashBytes(h, lightProjMtx, sizeof(float) * 16);
        hashBytes(h, &lightconf.smoothBorder,
                  sizeof(lightconf.smoothBorder));
        hashCasters(h);
        shadowRender = h != view->shadowMapHash;
        if (shadowRender)
            view->shadowMapHash = h;
        if (dbgshadow && !shadowRender)
            fprintf(stderr, "bgfx shadow map cached (%llx)\n",
                    (unsigned long long)h);
    } else if (shadowActive) {
        view->shadowMapHash = 0;
    }

    // Bulb shadow tiles (Render_LightShadow): each shadow-casting
    // bulb renders either one wide downward-cone tile, or — with
    // Render_LightShadowExtended — six world-axis cube faces, into
    // sequentially allocated atlas tiles. The camera-space receiver
    // matrices refresh every frame; a tile itself re-renders only
    // when its hash (bulb pose + face + caster set) changes — on a
    // static scene the steady-state cost is zero.
    bool bulbShadowRender[BGFXView::kBulbShadowTiles] = {};
    bool anyBulbShadow = false;
    {
        // The 50MB atlas is built by the first frame that has a
        // shadow-casting bulb to put in it.
        //
        // ! Allocate-only, deliberately. Unlike the volumetric, bloom
        // and reflection groups, this demand is SCENE state -- a light
        // object carrying Material::lightshadow -- not configuration,
        // and updateEffect's contract is that only configuration may
        // release. Freeing on "no bulb in this frame's scene" would
        // drop and rebuild 50MB across a document switch, whose
        // intermediate feeds are legitimately empty. It goes away with
        // the view's programs instead.
        bool wantBulbAtlas = false;
        if (shadowActive) {
            for (int sl = 0; sl < kBulbSlots; ++sl) {
                if (sl < bulbCount && bulbWantShadow[sl]
                        && bulbRangeW[sl] > 0.0f) {
                    wantBulbAtlas = true;
                    break;
                }
            }
        }
        if (wantBulbAtlas)
            view->updateEffect(BGFXView::EffectBulbShadow, true);

        uint64_t casterH = 0;
        const auto *caps = bgfx::getCaps();
        // Camera view -> world rotation for the shader's cube-face
        // pick (directions only; w = 0 drops the translation).
        bx::mtxInverse(view->bulbShadowRotMtx,
            reinterpret_cast<const float *>(viewMatrix));
        // World-axis cube faces, order matched by the shader's
        // dominant-axis pick: +X -X +Y -Y +Z -Z.
        static const float kFaceFwd[6][3] = {
            {1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
            {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
        static const float kFaceUp[6][3] = {
            {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
            {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
        float invV[16];
        bx::mtxInverse(invV,
            reinterpret_cast<const float *>(viewMatrix));
        int nextTile = 0;
        for (int sl = 0; sl < kBulbSlots; ++sl) {
            bool want = shadowActive && sl < bulbCount
                && bulbWantShadow[sl] && bulbRangeW[sl] > 0.0f
                && bgfx::isValid(view->bulbShadowFbo);
            // 6 cube faces when extended; fall back to the plain
            // downward tile when the atlas can't fit them all.
            int faces = want ? (bulbWantShadowExt[sl] ? 6 : 1) : 0;
            if (faces > BGFXView::kBulbShadowTiles - nextTile)
                faces = BGFXView::kBulbShadowTiles - nextTile >= 1
                    ? 1 : 0;
            view->bulbShadowConf[sl][0] = float(nextTile);
            view->bulbShadowConf[sl][1] = float(faces);
            view->bulbShadowConf[sl][2] = 0.0f;
            // .w = the format-dependent variance floor of the
            // plain-VSM tap: fp16 moments quantize the z^2 moment
            // to ~2^-12 steps, so the RG16F fallback needs a floor
            // well above that noise or the Chebyshev test speckles
            // along contact terminators.
            view->bulbShadowConf[sl][3] =
                view->shadowFormat == bgfx::TextureFormat::RG32F
                    ? 1.0e-5f : 3.0e-4f;
            if (!faces)
                continue;
            // Light camera per face. Near starts outside the bulb
            // body so the emitter doesn't shadow itself.
            bx::Vec3 eye(bulbPos[sl][0], bulbPos[sl][1],
                         bulbPos[sl][2]);
            float near = bx::max(0.55f * bulbDiagW[sl],
                                 0.01f * bulbRangeW[sl]);
            float far = bx::max(1.5f * bulbRangeW[sl], near * 4.0f);
            // Cube faces cover 90 deg; 100 leaves margin past the
            // diagonal so the bounds guard never opens a seam.
            float fovy = faces == 6 ? 100.0f : 130.0f;
            for (int f = 0; f < faces; ++f) {
                int t = nextTile + f;
                bx::Vec3 fwd = faces == 6
                    ? bx::Vec3(kFaceFwd[f][0], kFaceFwd[f][1],
                               kFaceFwd[f][2])
                    : bx::Vec3(0.0f, 0.0f, -1.0f);
                bx::Vec3 up = faces == 6
                    ? bx::Vec3(kFaceUp[f][0], kFaceUp[f][1],
                               kFaceUp[f][2])
                    : bx::Vec3(0.0f, 1.0f, 0.0f);
                bx::mtxLookAt(view->bulbShadowViewMtx[t], eye,
                              bx::add(eye, fwd), up);
                bx::mtxProj(view->bulbShadowProjMtx[t], fovy, 1.0f,
                            near, far, caps->homogeneousDepth);
                // Camera view space -> atlas tile uv/depth.
                float tmp[16], tmp2[16];
                bx::mtxMul(tmp, invV, view->bulbShadowViewMtx[t]);
                bx::mtxMul(tmp2, tmp, view->bulbShadowProjMtx[t]);
                const float sc = 0.5f / float(BGFXView::kBulbShadowGrid);
                const float sy = caps->originBottomLeft ? sc : -sc;
                const float sz = caps->homogeneousDepth ? 0.5f : 1.0f;
                const float tz = caps->homogeneousDepth ? 0.5f : 0.0f;
                const float tx = sc
                    + 2.0f * sc * float(t % BGFXView::kBulbShadowGrid);
                const float ty = sc
                    + 2.0f * sc * float(t / BGFXView::kBulbShadowGrid);
                const float crop[16] = {
                    sc,   0.0f, 0.0f, 0.0f,
                    0.0f, sy,   0.0f, 0.0f,
                    0.0f, 0.0f, sz,   0.0f,
                    tx,   ty,   tz,   1.0f,
                };
                bx::mtxMul(view->bulbShadowMtx[t], tmp2, crop);
                // Tile cache: pose + range + face + caster set.
                if (!casterH) {
                    casterH = 1469598103934665603ULL;
                    hashCasters(casterH);
                }
                uint64_t h = casterH;
                hashBytes(h, bulbPos[sl], sizeof(bulbPos[sl]));
                hashBytes(h, &bulbRangeW[sl], sizeof(bulbRangeW[sl]));
                hashBytes(h, &f, sizeof(f));
                hashBytes(h, &faces, sizeof(faces));
                bulbShadowRender[t] = !view->bulbShadowValid[t]
                    || h != view->bulbShadowHash[t];
                view->bulbShadowHash[t] = h;
                view->bulbShadowValid[t] = true;
                anyBulbShadow = anyBulbShadow || bulbShadowRender[t];
            }
            nextTile += faces;
            // Flag the mesh shader to tap this slot's tiles.
            view->localLightColorI[BGFXView::kMediumSlots + sl][3]
                = 1.0f;
        }
        // Unused tiles: invalidate and zero so nothing taps them.
        for (int t = nextTile; t < BGFXView::kBulbShadowTiles; ++t) {
            view->bulbShadowValid[t] = false;
            view->bulbShadowHash[t] = 0;
            std::memset(view->bulbShadowMtx[t], 0,
                        sizeof(view->bulbShadowMtx[t]));
        }
    }

    // ShadowSmoothBorder > 0 runs the separable blur over the fresh
    // moments right after the caster pass.
    bool shadowBlurActive = shadowRender
        && lightconf.smoothBorder > 0.0f
        && bgfx::isValid(view->shadowBlurFbo);

    // Medium-interval / planar-reflection frame cache: those targets
    // depend only on the camera, the viewport and the scene draws.
    // The fire/cloud/water animation lives in the volume raymarch
    // and the surface shaders, not in the interval depths, and the
    // mirrored-scene render is static per camera (the water waves
    // only distort how the surface samples it). Re-render when the
    // camera or viewport changed or any scene/config change was
    // applied this frame (dirtyChanged covers every mutation path);
    // the mirrored scene additionally re-renders whenever the shadow
    // map did (its shading includes the shadow lookup) and while a
    // fire burns (the flickering fire effect light shades the
    // mirrored geometry per frame).
    static const bool staticNoCache =
        (getenv("FC_BGFX_NO_STATIC_CACHE") != nullptr);
    uint64_t camH = 1469598103934665603ULL;
    hashBytes(camH, reinterpret_cast<const float *>(viewMatrix),
              sizeof(float) * 16);
    hashBytes(camH, reinterpret_cast<const float *>(projMatrix),
              sizeof(float) * 16);
    hashBytes(camH, &width, sizeof(width));
    hashBytes(camH, &height, sizeof(height));
    const bool staticFrame = !dirtyChanged && !hlconfig.show
        && !staticNoCache && camH == view->camFrameHash;
    view->camFrameHash = camH;

    // Idle temporal accumulation (docs/RenderEngine.md sec 3.5).
    //
    // Multisampling resolves coverage and nothing else: one shaded
    // value per triangle per pixel, and every screen-space pass after
    // the resolve at one sample. So a highlight crawling across a
    // curved surface, detail below the pixel and the noise left in the
    // effect passes are all beyond it at any sample count. What is
    // left is to spend time: over a still camera, offset the projection
    // by a fraction of a pixel each frame and average the results, and
    // the whole pipeline converges toward supersampling for free --
    // "free" because a frame that nobody is interacting with was going
    // to be idle anyway.
    //
    // No reprojection, no history rejection, no motion vectors: this
    // runs ONLY over frames where nothing moved, and staticFrame above
    // is that predicate exactly -- it already answers for the camera,
    // the viewport, every scene and config mutation, and the hover
    // highlight. Any of them changing zeroes the count below, which
    // replaces the history outright on the next frame. That is what
    // keeps thin CAD edges from smearing the way ordinary TAA smears
    // them, and why this refines a multisampled frame instead of
    // replacing multisampling with a temporal filter.
    //
    // ! camH above hashes the UNJITTERED projection, and the jitter is
    // applied below it on purpose. Hashing the jittered matrix would
    // make every accumulation frame read as a camera move -- the one
    // thing that resets the accumulation -- so it would reset itself
    // every frame and never converge, at the cost of a full extra
    // render per frame forever.
    const int accumSamples =
        tempconf.enabled && !debugconf.freezeFrame
                && view->effectAllocated(BGFXView::EffectAccum)
            ? std::max(2, std::min(256, tempconf.samples))
            : 0;
    // A capture that drops the viewport chrome (an image export, as
    // against a debug capture) renders a DIFFERENT picture from the one
    // on screen. It must neither be averaged into the history nor be
    // overwritten by it -- the history has the chrome in it, and
    // painting that back would put the navigation cube into an exported
    // image. Stand the accumulation down for that frame alone, without
    // resetting the count, so the view resumes where it left off.
    const bool chromelessDump = dumpPending && !pendingDump.overlays;
    const bool accumActive =
        accumSamples != 0 && staticFrame && !chromelessDump
        && !externalBase;
    if (!accumActive)
        view->accumFrames = 0;
    // What this frame is worth in the running mean. The first frame
    // over a still camera replaces the history outright; the nth
    // arrives with weight 1/(n+1), which makes the history their mean.
    // Once the budget is reached the frame contributes nothing and only
    // the copy back runs, so a redraw the view did not ask for (an
    // expose, a sibling window) still shows the converged image.
    const float accumBlend =
        !accumActive ? 0.0f
        : view->accumFrames >= accumSamples ? 0.0f
        : 1.0f / float(view->accumFrames + 1);
    // Accruing a jittered sample this frame -- as against holding a
    // converged image, or sample 0, which is the pixel centre and so
    // is the ordinary frame. Everything that has to move per sample
    // keys on this one predicate: the camera offset below, and the
    // AO chain, which would otherwise sit on its cache and hold one
    // frame's noise through the whole refinement.
    const bool accumRefining = accumActive && view->accumFrames > 0
        && view->accumFrames < accumSamples;
    // Which sample of a refinement this frame is. Zero on an ordinary
    // frame, which is what keeps every consumer below bit-identical to
    // before the accumulation existed.
    //
    // Three targets key on it, and all for one reason: they are cached
    // against a camera the jitter deliberately does not appear in, so
    // across a refinement their keys never change and they hand back a
    // result rendered at the UNJITTERED camera while everything
    // sampling them moves underneath. The AO chain is one (see the
    // hash below); the mirrored scene and the media interval depths are
    // the other two, and those are cached on staticFrame -- which is
    // the very predicate that says an accumulation MAY run, so during a
    // refinement it is guaranteed to answer "reuse".
    //
    // Comparing a STORED index rather than ORing in "is refining"
    // answers the way back as well: when a refinement ends the index
    // returns to 0 with the camera unmoved, and a bare OR would leave
    // each target holding the last sample's jittered result.
    const int accumSampleIndex = accumRefining ? view->accumFrames : 0;
    // The media interval depths (water/glass/cloud/fire): screen-space
    // depth spans of the medium bodies, so they follow the camera and
    // have to be redrawn per sample like everything else it decides.
    const bool mediumRender = !staticFrame
        || view->mediumSampleIndex != accumSampleIndex;
    if (mediumRender)
        view->mediumSampleIndex = accumSampleIndex;
    // Asked for and not engaging: say so once, with the reason. All
    // three causes look the same on screen -- the picture simply never
    // refines -- and the third one (something in the feed reporting a
    // change every frame) is invisible from outside the renderer
    // entirely. Held for a stretch of frames first, because "not
    // static" is the ordinary state of a view somebody is using.
    if (tempconf.enabled && !view->accumReported) {
        if (accumActive) {
            view->accumReported = true;   // it works; nothing to say
        }
        else if (++view->accumQuietFrames > 240) {
            view->accumReported = true;
            const char *why =
                debugconf.freezeFrame
                    ? "the freeze-frame determinism switch is on"
                : !view->effectAllocated(BGFXView::EffectAccum)
                    ? "its history target could not be allocated"
                : "something reports a change every frame -- a scene or "
                  "config feed, or the hover highlight -- so no two "
                  "frames are ever of the same picture";
            RENDER_ERR("temporal accumulation is on but never engages: "
                       << why);
        }
    }

    // The prepass rasterizes for SSAO and/or the volumetric ray
    // ends; the AO resolve chain itself stays SSAO-gated. The water
    // surface pass also reads its viewZ, to reject refraction
    // samples landing on geometry in front of the surface (the
    // not-submerged parts of protruding objects would smear).
    static const bool noWaterReject =
        (getenv("FC_BGFX_NO_WATER_REJECT") != nullptr);
    bool waterSurfReject = waterSurfActive && view->m_ssao
        && !noWaterReject && bgfx::isValid(view->aoPrepassFbo);
    bool glassReject = glassActive && !noWaterReject;
    bool prepassActive = ssaoActive || volActive || waterSurfReject
        || glassReject || cavityActive;

    // AO/prepass cache (same pattern as the shadow-map hash above):
    // the depth/normal prepass and the whole AO resolve chain
    // (pyramid, gen, denoise) depend only on the camera, the
    // viewport, the AO parameters, the prepass draw set and the
    // accumulation sample index -- never on time. (The index is not
    // time: it is which sample of a refinement this is, and it is 0
    // on every frame that is not one.) When none of those changed,
    // skip them all and keep last frame's targets: animated effects
    // (water waves, fire) re-render every frame but read the cached
    // prepass/AO, so a static camera pays only the cheap AO apply
    // multiply. The interaction fast path needs no special casing --
    // aoconf.fast flips the hash (and the camera moves anyway), and
    // the first idle frame recomputes at full quality and re-primes
    // the cache.
    static const bool aoNoCache =
        (getenv("FC_BGFX_NO_AO_CACHE") != nullptr);
    bool aoRender = prepassActive;
    if (prepassActive && !hlconfig.show && !aoNoCache) {
        uint64_t h = 1469598103934665603ULL;
        hashBytes(h, reinterpret_cast<const float *>(viewMatrix),
                  sizeof(float) * 16);
        hashBytes(h, reinterpret_cast<const float *>(projMatrix),
                  sizeof(float) * 16);
        hashBytes(h, &width, sizeof(width));
        hashBytes(h, &height, sizeof(height));
        hashBytes(h, &view->ssaoW, sizeof(view->ssaoW));
        hashBytes(h, &view->ssaoH, sizeof(view->ssaoH));
        hashBytes(h, &aoRadius, sizeof(aoRadius));
        hashBytes(h, &aoconf.intensity, sizeof(aoconf.intensity));
        hashBytes(h, &aoconf.method, sizeof(aoconf.method));
        hashBytes(h, &aoconf.fast, sizeof(aoconf.fast));
        hashBytes(h, &aoconf.slices, sizeof(aoconf.slices));
        hashBytes(h, &aoconf.steps, sizeof(aoconf.steps));
        // The idle accumulation's sample index (docs/RenderEngine.md
        // sec 3.5). Both AO passes are stochastic, and both draw their
        // noise from screen position, so a refinement that reuses this
        // map averages one noise field with itself -- which is why AO
        // was the one part of the frame the accumulation could not
        // converge. Hashing the index is what re-renders the chain per
        // sample, and it is the honest way to say it: the map really
        // does depend on the index, so the key that decides whether it
        // is still valid has to contain it.
        //
        // Hashing beats forcing the render from outside, which is what
        // this first did, because it also answers the way BACK. When a
        // refinement ends the index returns to 0 while the camera has
        // not moved, so a hash without it says "hit" and the view keeps
        // the LAST SAMPLE's noise -- turning the feature off would
        // leave a frame that is not the frame it had before turning it
        // on. With the index in the key that transition re-renders,
        // and off is once again exactly off.
        hashBytes(h, &accumSampleIndex, sizeof(accumSampleIndex));
        const bool consumers[4] = {ssaoActive, volActive,
                                   waterSurfReject, glassReject};
        hashBytes(h, consumers, sizeof(consumers));
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            // The prepass draw filter (see the submit below):
            // opaque scene triangles, media bodies exempt.
            const bool transp = mat.transparent
                || (mat.pervertexcolor && draw.mesh
                    && draw.mesh->hasTransparency);
            if (mat.type != Render::Material::Triangle
                    || transp || mediumExempt(mat)
                    || !draw.mesh)
                continue;
            hashBytes(h, &draw.mesh->cacheId,
                      sizeof(draw.mesh->cacheId));
            if (!draw.identity)
                hashBytes(h, draw.model, sizeof(float) * 16);
            hashBytes(h, &draw.indexStart, sizeof(draw.indexStart));
            hashBytes(h, &draw.indexCount, sizeof(draw.indexCount));
            if (mat.numclipplanes) {
                hashBytes(h, &mat.numclipplanes, 1);
                hashBytes(h, &mat.clipconcave, 1);
                hashBytes(h, mat.clipplanes,
                          sizeof(float) * 4 * mat.numclipplanes);
            }
            if (!mat.autozoom.empty())
                hashBytes(h, &autozoomScale, sizeof(autozoomScale));
        }
        aoRender = h != view->aoMapHash;
        if (aoRender)
            view->aoMapHash = h;
    } else if (prepassActive) {
        view->aoMapHash = 0;
    }
    // User "post" stage shader (docs/RenderDebug.md §6): the last
    // captured post-stage program wins; resolved through the
    // runtime shaderc compile cache on desktop builds and from the
    // snapshot-shipped server-compiled binaries on the viewer
    // tier. A failed compile keeps the pass off.
    const Render::UserShader *userPost = nullptr;
    for (const auto &s : usershaderconf.shaders) {
        if (s.stage == "post" && !s.fragmentSource.empty())
            userPost = &s;
    }
    bgfx::ProgramHandle userPostProg = BGFX_INVALID_HANDLE;
#ifndef FC_RENDERER_STANDALONE
    userShaderGen = _BGFXLib.userCompileGeneration;
#endif
    if (userPost)
        userPostProg = _BGFXLib.getUserProgram(*userPost,
                                               "vs_fc_comp");
    const bool userPostActive = userPost
        && bgfx::isValid(userPostProg)
        && bgfx::isValid(view->sceneCopyFbo)
        && bgfx::isValid(view->m_progWaterCopy);

    const bool prepassRender = prepassActive && aoRender;

    // Debug scene re-render (docs/RenderDebug.md modes 6/8/11): the
    // counting/UV/id rasterization runs every frame while its mode is
    // active — debug-only work, no caching.
    //
    // The id variant has two independent callers: mode 11 draws it
    // on screen, and RenderDebug_CullAudit reads it back to check
    // the culling against it. The audit deliberately does not
    // require the view mode — measuring what the frame skipped and
    // looking at a false-colour id image are different jobs, and
    // forcing the second to run the first would mean the audit can
    // only be taken while the screen shows something nobody can
    // navigate by.
    const bool idPassRender = (debugconf.viewMode == 11
                               || debugconf.cullAudit)
        && view->ensureDebugScene();
    const bool debugSceneRender = (debugconf.viewMode == 6
                                   || debugconf.viewMode == 8)
        && view->ensureDebugScene();

    // The audit's readback, once a second: it is a full-resolution
    // transfer off the GPU. One in flight at a time — a second would
    // only overwrite the buffer the first is still being written
    // into, and the readback is the cheap half anyway.
    bool idReadbackWanted = false;
    if (debugconf.cullAudit && idPassRender && !idReadyFrame) {
        if (!BGFXView::idReadbackSupported()) {
            if (!idAuditWarned) {
                idAuditWarned = true;
                RENDER_ERR("render cull audit: this backend cannot read "
                           "a texture back, so the audit cannot run "
                           "here. The id image itself still renders "
                           "(DebugViewMode 11) and can be "
                           "captured off the screen.");
            }
        }
        else if (view->ensureIdReadback() && cullAuditDue())
            idReadbackWanted = true;
    }

    // A frame capture wants two passes of its own: the depth encode
    // and the blit-only copy view. Decided here, where the pass table
    // is built, while whether the capture is HELD (a user shader still
    // compiling, the host still feeding shapes) is only known at the
    // end of the frame -- so the passes are declared and then simply
    // not submitted into if the hold turns out to apply. A declared
    // pass nobody draws into costs a view-state assignment.
    //
    // One readback in flight at a time, as with the audit: a second
    // would overwrite the buffers the first is still being written
    // into.
#ifdef FC_RENDERER_STANDALONE
    const bool captureWanted = false;
#else
    static const bool debugReadback =
        getenv("FC_BGFX_DEBUG_READBACK") != nullptr;
    const bool captureWanted = (dumpPending || debugReadback)
        && !captureReadyFrame && view->ensureCaptureTargets();
#endif

    // Ground reflection: mirror the world about the shadow ground
    // plane (z = scene bbox bottom, the plane the ground quad sits
    // on) and re-render the opaque scene with the original camera —
    // the ground then acts as a window into the mirrored world. The
    // shadow lookup of the mirrored draws needs the shadow matrix
    // rebased from the mirrored view space (mirrored-view -> world
    // -> original-view -> shadow uv).
    //
    // What it needs is a ground quad to blend onto, which groundQuad()
    // answers for -- it takes the reflection as its own reason to exist,
    // so RenderShadow_ShowGround need not also be found and switched on.
    // What it does NOT need is a shadow map: the mirrored re-render and
    // the overlay are the same with or without one, and a reflection
    // that could only be seen under the Shadow draw style was the
    // coupling this had until now.
    float groundCorners[4][3];
    const bool groundQuadOk = bboxValid
        && lightconf.groundQuad(bboxMin, bboxMax, groundCam, groundCorners);
    bool groundReflActive = groundQuadOk && lightconf.groundReflection
        && !hlconfig.show && !externalBase
        && bgfx::isValid(view->m_progGroundRefl)
        && bgfx::isValid(view->reflFbo);
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr,
                "bgfx ground refl: conf=%d ground=%d quad=%d shadow=%d"
                " active=%d intensity=%g\n",
                lightconf.groundReflection, lightconf.ground,
                int(groundQuadOk), shadowActive, groundReflActive,
                lightconf.groundReflectionIntensity);
    float reflViewMtx[16], reflShadowMtx[16];
    if (groundReflActive) {
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        float S[16];
        bx::mtxIdentity(S);
        S[10] = -1.0f;
        S[14] = 2.0f * bboxMin[2];
        bx::mtxMul(reflViewMtx, S, vm);
        float invV[16], m1[16], m2[16];
        bx::mtxInverse(invV, vm);
        bx::mtxMul(m1, invV, S);
        bx::mtxMul(m2, m1, vm);
        bx::mtxMul(reflShadowMtx, m2, view->shadowMtx);
    }

    // Water planar reflection: mirror the world about the water body's
    // top plane and re-render the opaque scene into the same reflection
    // target, so the water surface shader samples a true reflection (no
    // SSR taper). Reuses the ground-reflection FBO/pass; only when the
    // ground reflection is not itself using them (single mirror plane
    // per frame). Assumes a horizontal water surface, like the ground.
    bool waterReflActive = waterSurfActive && waterconf.reflection
        && waterconf.planarReflection && waterPlaneSet
        && !groundReflActive && bboxValid && !hlconfig.show
        && bgfx::isValid(view->m_progGroundRefl)
        && bgfx::isValid(view->reflFbo);
    // Reflection mode fed to the water shader: 0 off, 1 environment
    // cubemap, 2 screen-space (march), 3 planar (mirror pass). Planar
    // requested but unavailable (e.g. ground reflection using the
    // shared target) degrades to the environment cubemap.
    int waterReflMode = 0;
    if (waterSurfActive && waterconf.reflection)
        waterReflMode = waterReflActive ? 3
            : (waterconf.planarReflection ? 1 : 2);
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr,
                "bgfx water refl: mode=%d active=%d surf=%d refl=%d "
                "planar=%d planeSet=%d groundRefl=%d bbox=%d hl=%d "
                "prog=%d fbo=%d\n",
                waterReflMode, int(waterReflActive),
                int(waterSurfActive), int(waterconf.reflection),
                int(waterconf.planarReflection), int(waterPlaneSet),
                int(groundReflActive), int(bboxValid),
                int(hlconfig.show),
                int(bgfx::isValid(view->m_progGroundRefl)),
                int(bgfx::isValid(view->reflFbo)));

    // A water surface whose wave normals have nothing to act on.
    // Refraction bends the scene behind the surface by the normal,
    // reflection aims by it, the glint needs it against a light —
    // with all three gone the shader still runs, but every wave,
    // ripple and impact ring resolves to the same flat tinted
    // sheet. Nothing about that is an error (each switch documents
    // "off = flat water color"), yet turning up the wave strength
    // and seeing not one pixel move reads exactly like a broken
    // renderer, and has cost this project a misdiagnosis already.
    // So say it, once when the state is entered rather than every
    // frame, and name the switches that would give the waves
    // something to do.
    const bool waterMute = waterSurfActive
        && !waterconf.refraction && waterReflMode == 0
        && !lightconf.valid;
    if (waterMute != view->waterNoResponse) {
        view->waterNoResponse = waterMute;
        if (waterMute) {
            const char *msg = "water surface: refraction and "
                "reflection are both off and no scene light is "
                "active, so the surface shades flat and the wave, "
                "ripple and impact settings cannot change a pixel "
                "(WaterRefraction / WaterReflection / the Shadow "
                "draw style)\n";
#ifdef FC_RENDERER_STANDALONE
            std::printf("%s", msg);
#else
            Base::Console().Warning("%s", msg);
#endif
        }
    }

    float waterReflViewMtx[16], waterReflShadowMtx[16];
    if (waterReflActive) {
        const float *vm = reinterpret_cast<const float *>(viewMatrix);
        float S[16];
        bx::mtxIdentity(S);
        S[10] = -1.0f;
        S[14] = 2.0f * waterPlaneZ;
        bx::mtxMul(waterReflViewMtx, S, vm);
        float invV[16], m1[16], m2[16];
        bx::mtxInverse(invV, vm);
        bx::mtxMul(m1, invV, S);
        bx::mtxMul(m2, m1, vm);
        bx::mtxMul(waterReflShadowMtx, m2, view->shadowMtx);
    }

    // The accumulation's camera offset. Its state is decided far
    // above, next to the AO cache that also reads it; only the
    // matrix edit belongs here, below everything that must see the
    // unjittered camera.
    if (accumRefining) {
        // Sample 0 is the pixel centre -- the ordinary frame -- so
        // settling the camera shows no jump, and the offsets start
        // from sample 1.
        const float jx = haltonInverse(view->accumFrames, 2) - 0.5f;
        const float jy = haltonInverse(view->accumFrames, 3) - 0.5f;
        const float dx = 2.0f * jx / float(std::max<uint16_t>(1, width));
        const float dy = 2.0f * jy / float(std::max<uint16_t>(1, height));
        std::memcpy(view->accumProj, projMatrix,
                    sizeof(view->accumProj));
        float *P = view->accumProj;
        // The shear terms of a projection are exactly a subpixel
        // offset, so this needs no inverse and no extra matrix
        // multiply. Perspective divides by w = +-z, hence the sign
        // from P[11] (bx builds either handedness); an orthographic
        // projection has w = 1 and carries its offset in the
        // translation row instead.
        if (P[11] != 0.0f) {
            P[8] += dx * P[11];
            P[9] += dy * P[11];
        }
        else {
            P[12] += dx;
            P[13] += dy;
        }
        // Everything from here down renders under the offset camera:
        // the scene, the effect passes that reconstruct position from
        // it, and the in-scene overlays. What must NOT move with it --
        // the level plan, the scene publish, the camera hash -- is all
        // above.
        projMatrix = view->accumProj;
    }

    // Which passes this frame draws (BGFXView's pass map) and how
    // each one's bgfx view is configured, declared as ONE table.
    // Every pass states its liveness predicate exactly once, next
    // to the closure that configures its view: the mark phase and
    // the view-config phase (after the particle step below) both
    // iterate this table, so they cannot drift apart -- the
    // predicate that claims a pass's id is the predicate that
    // configures it, and a pass the table does not declare cannot
    // render at all (it reports itself once instead).
    //
    // Nothing may submit before mapPasses() below: the pass ->
    // bgfx-view-id mapping is decided there, and only the passes
    // declared live get an id of their own. Everything else maps
    // to the discard view, so a submit the table did not predict
    // costs that pass its pixels and prints the pass number -- it
    // cannot bleed into another pass.
    //
    // Passes whose use depends on the draws rather than on
    // configuration (which bucket a mesh lands in, whether anything
    // has an outline) are simply always claimed: one id each, and
    // the point of the exercise is the groups that come in sixes
    // and sixteens.
    using V = BGFXView;
    // Decided after the particle step (it reads the emitter
    // liveness the step updates); the reflection entry's config
    // closure reads it by reference at config time, which runs
    // after the assignment.
    bool reflRender = false;

    // The shared tail of every pass that renders into the scene
    // framebuffer (or falls back to it): viewport rect, camera
    // transforms and submission-order mode.
    auto configTail = [&](int i, uint16_t id) {
        // The SSAO generate/blur passes render into aoTex/aoBlurTex
        // at their own Render_SSAOResolution (ssaoW/ssaoH, default
        // full-res and independent of the reflection scale); the
        // mesh draws sample the result back at normalized uv. The
        // reduced-resolution reflection re-render sets its own rect
        // in its own closure.
        bool aoResolveView = i == V::ViewAOGen
            || i == V::ViewAOBlur
            || i == V::ViewAOBlur2;
        bgfx::setViewRect(id, 0, 0,
            aoResolveView ? view->ssaoW : width,
            aoResolveView ? view->ssaoH : height);
        // The background quad and the OIT composite triangle are
        // submitted in clip space; the AO generation pass keeps the
        // scene projection for its predefined u_proj (position
        // reconstruction).
        // The GTAO denoise passes (AOBlur/AOBlur2) keep the scene
        // projection like the gen pass: their plane-aware bilateral
        // weight reconstructs view positions from u_proj (the shared
        // fullscreen vertex shader ignores the matrices).
        // The environment background keeps the scene matrices on
        // the background view: its fragment shader reconstructs
        // per-pixel world directions from u_proj/u_invView.
        if ((i == V::ViewBackground
                && !(pbrActive && pbrconf.envBackground))
                || i == V::ViewOITComposite)
            bgfx::setViewTransform(id, nullptr, nullptr);
        else
            bgfx::setViewTransform(id, viewMatrix, projMatrix);
        // On-top and highlight draws are blended painter-style: keep
        // submission order (GL pass order) instead of state sorting.
        // With OIT the transparent blend is commutative, so no
        // depth sorting is needed there either. The outline and
        // section-cap views interleave stencil mark/fill/cleanup
        // passes per entry, so they must keep submission order too.
        // The volumetric apply view is sequential too: the
        // per-channel extinction multiply must land before the
        // inscatter add.
        // Blended sprites are painted back to front for the same
        // reason a non-OIT transparent bucket is: alpha blending is
        // not commutative (additive emitters do not care).
        bgfx::setViewMode(id,
            (i == V::ViewTransparent && !oitActive)
                    || i == V::ViewParticles
                ? bgfx::ViewMode::DepthDescending
                : i >= V::ViewOnTop
                        || i == V::ViewSelection
                        || i == V::ViewOutline
                        || i == V::ViewSectionCap
                        || i == V::ViewSectionCapTransp
                        || i == V::ViewAOPrepassCap
                        || i == V::ViewVolApply
                    ? bgfx::ViewMode::Sequential
                    : bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    // Passes that render into the scene framebuffer -- and the
    // benign fallback for a claimed pass whose special target is
    // unavailable this frame (it draws nothing there). The
    // section-cap views clear the stencil: their parity marking
    // (INVERT) needs a zeroed base, and earlier outline passes
    // leave stale marks behind (relevant for the transparent cap
    // view, which runs after ViewOutline). The frame's one clear of
    // the scene framebuffer belongs to the first view that draws
    // into it -- named, not index 0: the particle state views
    // precede it.
    auto configScene = [&](int i, uint16_t id) {
        bgfx::setViewFrameBuffer(id, view->bgfxFbo);
        bgfx::setViewClear(id,
            i == V::ViewBackground
                ? uint16_t(BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
                           |BGFX_CLEAR_STENCIL)
            : (i == V::ViewSectionCap
               || i == V::ViewSectionCapTransp)
                ? uint16_t(BGFX_CLEAR_STENCIL)
                : uint16_t(BGFX_CLEAR_NONE),
            clearColor, 1.0f, 0);
        configTail(i, id);
    };
    auto configTransparent = [&](int i, uint16_t id) {
        if (!oitActive) {
            configScene(i, id);
            return;
        }
        // Accumulation targets: accum clears to 0, revealage
        // to 1; the shared depth attachment is not cleared.
        bgfx::setViewFrameBuffer(id, view->oitFbo);
        bgfx::setPaletteColor(0, 0.0f, 0.0f, 0.0f, 0.0f);
        bgfx::setPaletteColor(1, 1.0f, 1.0f, 1.0f, 1.0f);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_COLOR),
                           1.0f, 0, 0, 1);
        configTail(i, id);
    };
    auto configShadow = [&](int, uint16_t id) {
        // Moments clear to the warped far plane
        // (exp(c), exp(2c)) through the palette (the packed
        // clear color cannot exceed 1), own depth; the caster
        // pass renders under the light camera at the shadow
        // map size.
        float evsmClear[4] = {std::exp(view->shadowWarpFrame),
                              std::exp(2.0f * view->shadowWarpFrame),
                              0.0f, 0.0f};
        bgfx::setPaletteColor(2, evsmClear);
        bgfx::setViewFrameBuffer(id, view->shadowFbo);
        bgfx::setViewClear(id,
            uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
            1.0f, 0, 2);
        bgfx::setViewRect(id, 0, 0, view->shadowSize,
                          view->shadowSize);
        bgfx::setViewTransform(id, lightViewMtx, lightProjMtx);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configShadowTint = [&](int i, uint16_t id) {
        if (!bgfx::isValid(view->shadowTintFbo)) {
            configScene(i, id);
            return;
        }
        // Glass shadow tint map: cleared to white (no glass =
        // full transmittance) under the light camera; glass
        // casters multiply their transmittance in.
        bgfx::setViewFrameBuffer(id, view->shadowTintFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_COLOR),
                           0xffffffffu, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, view->shadowSize,
                          view->shadowSize);
        bgfx::setViewTransform(id, lightViewMtx, lightProjMtx);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configShadowBlur = [&](int i, uint16_t id) {
        // Fullscreen blur passes over the shadow map size; the
        // triangle overwrites every pixel, so no clear.
        bgfx::setViewFrameBuffer(id,
            i == V::ViewShadowBlurH
                ? view->shadowBlurFbo
            : i == V::ViewShadowBlurV
                ? view->shadowBlurBackFbo
            : i == V::ViewShadowTintBlurH
                ? view->shadowTintBlurFbo
                : view->shadowTintBlurBackFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, view->shadowSize,
                          view->shadowSize);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configBulb = [&](int i, uint16_t id) {
        // Bulb shadow tile: plain VSM moments cleared to the
        // far plane (1, 1) through the palette, tile subrect of
        // the atlas, the bulb's light camera.
        int t = i - V::ViewBulbShadow0;
        float vsmClear[4] = {1.0f, 1.0f, 0.0f, 0.0f};
        bgfx::setPaletteColor(3, vsmClear);
        bgfx::setViewFrameBuffer(id, view->bulbShadowFbo);
        bgfx::setViewClear(id,
            uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
            1.0f, 0, 3);
        // The receiver crop matrix addresses tile row t/grid
        // from the bottom of the atlas. On bottom-left-origin
        // backends (GL) the view rect's top-left y is flipped
        // to a GL viewport row from the bottom, so mirror the
        // row for the sampled v to land on the tile.
        int grid = V::kBulbShadowGrid;
        int tileRow = bgfx::getCaps()->originBottomLeft
            ? grid - 1 - t / grid : t / grid;
        bgfx::setViewRect(id,
            uint16_t((t % grid) * V::kBulbShadowTileSize),
            uint16_t(tileRow * V::kBulbShadowTileSize),
            V::kBulbShadowTileSize,
            V::kBulbShadowTileSize);
        bgfx::setViewTransform(id, view->bulbShadowViewMtx[t],
                               view->bulbShadowProjMtx[t]);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configLineSdf = [&](int i, uint16_t id) {
        // Cleared to zero on purpose: the field stores
        // kLineSdfRadius - signedDistance, so an untouched texel decodes
        // as a line one whole radius away, which is no line at all.
        // Depth clears to 1 so the nearest-line min starts empty.
        bgfx::setViewFrameBuffer(id, view->lineSdfFbo);
        bgfx::setViewClear(id,
            uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
            0x00000000u, 1.0f, 0);
        configTail(i, id);
    };
    auto configAOPrepass = [&](int i, uint16_t id) {
        // Prepass target clears to 0 (.w = 0 marks background
        // in the AO pass), with its own depth buffer.
        bgfx::setViewFrameBuffer(id, view->aoPrepassFbo);
        bgfx::setViewClear(id,
            uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
            0x00000000u, 1.0f, 0);
        configTail(i, id);
    };
    auto configAOPrepassCap = [&](int i, uint16_t id) {
        // Same target as the prepass, entered after it: the caps have
        // to depth-test against the geometry already there. Only the
        // stencil is cleared -- clearing colour or depth here would
        // throw away the prepass itself.
        bgfx::setViewFrameBuffer(id, view->aoPrepassFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_STENCIL),
                           0x00000000u, 1.0f, 0);
        configTail(i, id);
    };
    auto configAOMip = [&](int i, uint16_t id) {
        // GTAO depth pyramid downsamples: each level renders a
        // clip-space fullscreen triangle into its own half-stepped
        // single-channel target (no-op views otherwise).
        // Only the levels the frame claimed are here (a level
        // past aoMipCount, or a frame with the AO chain off, is
        // simply not mapped).
        const int m = i - V::ViewAODepthMip1;
        bgfx::setViewFrameBuffer(id, view->aoMipFbo[m]);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0,
            uint16_t(std::max(1, width >> (m + 1))),
            uint16_t(std::max(1, height >> (m + 1))));
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configAOChain = [&](int i, uint16_t id) {
        // Fullscreen passes overwrite their whole target.
        // ViewAOBlur2 ping-pongs the denoise back into aoTex.
        bgfx::setViewFrameBuffer(id,
            i == V::ViewAOBlur ? view->aoBlurFbo
                               : view->aoGenFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        configTail(i, id);
    };
    auto configMedium = [&](int i, uint16_t id) {
        // Water/glass/cloud/fire body interval depth targets:
        // color clears to 0 (.w = 0 = no medium on this pixel);
        // the back-face views keep the farthest depth, so their
        // depth buffer clears to 0 and tests GREATER.
        bool back = i == V::ViewWaterBack || i == V::ViewGlassBack
            || i == V::ViewCloudBack || i == V::ViewFireBack;
        bgfx::setViewFrameBuffer(id,
            i == V::ViewWaterFront ? view->waterFrontFbo
            : i == V::ViewWaterBack ? view->waterBackFbo
            : i == V::ViewGlassFront ? view->glassFrontFbo
            : i == V::ViewGlassBack ? view->glassBackFbo
            : i == V::ViewCloudFront ? view->cloudFrontFbo
            : i == V::ViewCloudBack ? view->cloudBackFbo
            : i == V::ViewFireFront ? view->fireFrontFbo
                                    : view->fireBackFbo);
        bgfx::setViewClear(id,
            uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
            0x00000000u, back ? 0.0f : 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, viewMatrix, projMatrix);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configVolGen = [&](int, uint16_t id) {
        // Half-res raymarch target; the fullscreen triangle
        // overwrites every pixel, and the scene transforms stay
        // bound for the predefined u_proj (ray reconstruction,
        // like the AO generation pass).
        bgfx::setViewFrameBuffer(id, view->volFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0,
            uint16_t(std::max(1, int(width) / 2)),
            uint16_t(std::max(1, int(height) / 2)));
        bgfx::setViewTransform(id, viewMatrix, projMatrix);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configVolAccum = [&](int i, uint16_t id) {
        if (!bgfx::isValid(view->volHistFbo)) {
            configScene(i, id);
            return;
        }
        // History accumulation target, same half-res rect as
        // the raymarch; the blended quad overwrites (or blends
        // into) every pixel, so no clear.
        bgfx::setViewFrameBuffer(id, view->volHistFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0,
            uint16_t(std::max(1, int(width) / 2)),
            uint16_t(std::max(1, int(height) / 2)));
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configRefl = [&](int i, uint16_t id) {
        // A cached mirror frame (reflRender false) re-renders
        // nothing: the target keeps its content and the claimed id
        // keeps the benign default configuration. The media
        // composite additionally needs an analytic medium to march.
        if (!reflRender
                || (i == V::ViewReflMedia
                    && !(volActive && (cloudActive || fireActive)))) {
            configScene(i, id);
            return;
        }
        if (i == V::ViewReflMedia) {
            // Media composite over the just-rendered mirror scene:
            // same target/rect/transforms, no clear (premultiplied
            // over blend).
            bgfx::setViewFrameBuffer(id, view->reflFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                               clearColor, 1.0f, 0);
        } else {
            // Mirrored-scene render: own color (cleared to alpha 0 =
            // nothing reflected) + depth, the original projection
            // over the mirrored view (ground plane or water plane).
            bgfx::setViewFrameBuffer(id, view->reflFbo);
            bgfx::setViewClear(id,
                uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                0x00000000u, 1.0f, 0);
        }
        // Reduced-resolution reflection re-render (matches reflFbo).
        bgfx::setViewRect(id, 0, 0, view->effW, view->effH);
        bgfx::setViewTransform(id,
            groundReflActive ? reflViewMtx : waterReflViewMtx,
            projMatrix);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configWaterCopy = [&](int, uint16_t id) {
        // Fullscreen copy of the scene color; the framebuffer
        // switch also resolves a multisampled scene attachment
        // before the surface pass samples the copy.
        bgfx::setViewFrameBuffer(id, view->sceneCopyFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configUserPostCopy = [&](int, uint16_t id) {
        // User post input: resolve/copy of the composited (post
        // bloom) scene color into the shared sceneCopy target;
        // like the water copy, the framebuffer switch resolves a
        // multisampled scene attachment before the copy samples.
        bgfx::setViewFrameBuffer(id, view->sceneCopyFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configUserPost = [&](int, uint16_t id) {
        // The user post program draws fullscreen back into the
        // scene target.
        bgfx::setViewFrameBuffer(id, view->bgfxFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configAccum = [&](int, uint16_t id) {
        // The history target. Like the user-post copy, rendering into
        // a different framebuffer is also what makes bgfx resolve the
        // multisampled scene attachment before this samples it.
        bgfx::setViewFrameBuffer(id, view->accumFbo);
        // Replacing the history outright (blend factor 1) clears
        // rather than relying on the blend to multiply the old
        // contents away. A fresh target holds whatever the driver
        // left in it, and if that is a NaN then NaN * 0 is still NaN
        // -- one poisoned texel would survive every later average and
        // stay on screen for the life of the view.
        bgfx::setViewClear(id,
                           uint16_t(accumBlend >= 1.0f ? BGFX_CLEAR_COLOR
                                                       : BGFX_CLEAR_NONE),
                           0x00000000u, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configAccumApply = [&](int, uint16_t id) {
        // ... and back over the scene colour, which is what the blit
        // and the present path read.
        bgfx::setViewFrameBuffer(id, view->bgfxFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configBloom = [&](int i, uint16_t id) {
        if (!bgfx::isValid(view->bloomFbo)) {
            configScene(i, id);
            return;
        }
        // Quarter-res bloom chain: bright/emit into the halo
        // source, blur ping-pongs through the second target.
        // The fullscreen passes overwrite every pixel but the
        // emit pass blends into the bright result, so only the
        // source clears (via the bright overwrite) -- no view
        // clear needed anywhere. The emit pass renders the
        // light bodies under the scene camera.
        bgfx::setViewFrameBuffer(id,
            i == V::ViewBloomBlurH
                ? view->bloomBlurFbo : view->bloomFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0,
            uint16_t(std::max(1, int(width) / 4)),
            uint16_t(std::max(1, int(height) / 4)));
        if (i == V::ViewBloomEmit)
            bgfx::setViewTransform(id, viewMatrix, projMatrix);
        else
            bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };
    auto configDebugScene = [&](int i, uint16_t id) {
        // Fresh count/UV/id target every frame: the overdraw
        // counts accumulate from zero, .w = 0 marks pixels the
        // UV re-render did not cover, and id 0 is "no draw owns
        // this pixel" -- all three want a cleared target and none
        // of them may inherit last frame's.
        bgfx::setViewFrameBuffer(id, view->debugSceneFbo);
        bgfx::setViewClear(id,
            uint16_t(BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
            0x00000000u, 1.0f, 0);
        // The id pass resolves coincident geometry by draw order
        // (on-top last), so it cannot be state-sorted; the
        // counting/UV modes do not care either way.
        if (idPassRender)
            bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
        configTail(i, id);
    };
    auto configCaptureDepth = [&](int, uint16_t id) {
        // The depth encode writes a full-viewport colour target of its
        // own; nothing samples it but the blit that follows, so no
        // clear is needed -- the fullscreen triangle covers every
        // texel.
        bgfx::setViewFrameBuffer(id, view->captureDepthFbo);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
    };
    auto configCapture = [&](int, uint16_t id) {
        // Blit-only, exactly like configIdReadback: no framebuffer,
        // nothing drawn. It is last so the copies see the finished
        // frame.
        bgfx::setViewFrameBuffer(id, BGFX_INVALID_HANDLE);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::touch(id);
    };
    auto configIdReadback = [&](int, uint16_t id) {
        // Blit-only view: no framebuffer of its own, nothing drawn
        // into it. It exists to place the copy after the pass it
        // copies -- bgfx runs a view's blits BEFORE its draws, so
        // asking for the copy on ViewDebugScene itself would read
        // the previous frame's image.
        bgfx::setViewFrameBuffer(id, BGFX_INVALID_HANDLE);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, 0, 0, width, height);
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::touch(id);
    };
    // The overlay feeds THIS frame draws, resolved once.
    //
    // Two filters, and they must be applied in ONE place: the slot a
    // pass is configured for (configOverlay) and the slot the
    // submission loop assigns have to name the same feed, and they
    // agreed only by both walking the whole map. They did not agree on
    // a chromeless dump, where the submission loop skipped chrome and
    // the config loop did not -- so every slot past the first skipped
    // one was set up from the wrong anchor.
    //
    //  - sub-view: OverlayAnchor::subView 0 is every sub-view (one
    //    viewer's chrome in all of them, and what a plain render()
    //    draws); a non-zero id is one cell's own, drawn only in that
    //    cell's sub-view frame (docs/SplitViews.md sec 16.3).
    //  - chrome: a dump asked for without overlays keeps the in-scene
    //    and foreground feeds and drops the viewport chrome (the
    //    reasoning is at the submission loop below).
    std::vector<std::pair<int, const OverlayFeed *>> frameOverlays;
    frameOverlays.reserve(overlays.size());
    for (const auto &ov : overlays) {
        const auto &a = ov.second.anchor;
        if (a.subView != 0 && a.subView != subCtx.id)
            continue;
        const bool isChrome =
            !a.sceneCamera
            && (a.corner != Render::OverlayAnchor::FullViewport
                || a.pixelSpace);
        if (chromelessDump && isChrome)
            continue;
        frameOverlays.emplace_back(ov.first, &ov.second);
    }
    {
        // Same knob as dumpFeed: which feeds a sub-view frame admitted,
        // against how many the backend holds. A cell drawing no chrome
        // while its feed is present is the difference between these two.
        static const bool dbg = getenv("FC_BGFX_DEBUG_FEED") != nullptr;
        if (dbg) {
            fprintf(stderr, "bgfx frame sub=%d admits %zu of %zu overlays:",
                    subCtx.id, frameOverlays.size(), overlays.size());
            for (const auto &f : frameOverlays)
                fprintf(stderr, " %d", f.first);
            fprintf(stderr, "\n");
        }
    }

    auto configOverlay = [&](int i, uint16_t id) {
        // Overlay feed slot: derive the viewport rect and the
        // camera from the declarative anchor each frame, so
        // overlays re-anchor on resize and (orientFromScene)
        // follow the current camera -- including the WASM
        // viewer's own orbit camera.
        // Only the slots the frame's overlays fill are mapped.
        int slot = i - V::ViewOverlay0;
        const Render::OverlayAnchor *anchor = &frameOverlays[slot].second->anchor;
        if (anchor->sceneCamera) {
            // In-scene overlay (editing graph, dimensions): draw over
            // the whole viewport with the main scene camera so the
            // world-space geometry lines up with the finished scene,
            // on a fresh depth buffer so it sits on top but still
            // depth-tests within itself.
            bgfx::setViewFrameBuffer(id, view->bgfxFbo);
            bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_DEPTH),
                               clearColor, 1.0f, 0);
            bgfx::setViewRect(id, 0, 0, width, height);
            bgfx::setViewTransform(id, viewMatrix, projMatrix);
            bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
            bgfx::touch(id);
            return;
        }
        uint16_t rx = 0, ry = 0, rw = width, rh = height;
        if (anchor->corner != Render::OverlayAnchor::FullViewport) {
            uint16_t edge = uint16_t(std::max(1.0f,
                anchor->sizeFraction
                    * float(std::min(width, height))));
            rw = rh = edge;
            bool right =
                anchor->corner == Render::OverlayAnchor::BottomRight
                || anchor->corner == Render::OverlayAnchor::TopRight;
            bool top =
                anchor->corner == Render::OverlayAnchor::TopLeft
                || anchor->corner == Render::OverlayAnchor::TopRight;
            // bgfx view rects are top-left anchored.
            int mx = int(anchor->marginX);
            int my = int(anchor->marginY);
            rx = uint16_t(std::max(0,
                right ? width - edge - mx : mx));
            ry = uint16_t(std::max(0,
                top ? my : height - edge - my));
        }
        const auto *caps = bgfx::getCaps();
        float ovProj[16];
        float aspect = float(rw) / float(rh);
        // Right-handed like the GL view matrix convention the
        // anchor camera follows (bx defaults to left-handed).
        if (anchor->pixelSpace) {
            // One unit == one pixel, origin top-left, y down (Qt
            // widget coordinates); z=0 content sits mid-range.
            bx::mtxOrtho(ovProj, 0.0f, float(rw), float(rh), 0.0f,
                         -1.0f, 1.0f, 0.0f, caps->homogeneousDepth,
                         bx::Handedness::Right);
        } else if (anchor->fovDeg > 0.0f) {
            bx::mtxProj(ovProj, anchor->fovDeg, aspect,
                        std::max(anchor->nearPlane, 1.0e-3f),
                        anchor->farPlane, caps->homogeneousDepth,
                        bx::Handedness::Right);
        } else {
            float hh = 0.5f * anchor->orthoHeight;
            float hw = hh * aspect;
            bx::mtxOrtho(ovProj, -hw, hw, -hh, hh,
                         anchor->nearPlane, anchor->farPlane,
                         0.0f, caps->homogeneousDepth,
                         bx::Handedness::Right);
        }
        float ovView[16];
        bx::mtxIdentity(ovView);
        if (!anchor->pixelSpace
            && anchor->orientFromScene && viewMatrix) {
            // Rotation part of the scene view matrix (rigid:
            // upper-left 3x3), translation dropped -- the axis
            // cross tracks the camera orientation only.
            const float *v =
                reinterpret_cast<const float *>(viewMatrix);
            for (int c = 0; c < 3; ++c)
                for (int r = 0; r < 3; ++r)
                    ovView[c * 4 + r] = v[c * 4 + r];
        }
        if (!anchor->pixelSpace)
            ovView[14] = -anchor->cameraDistance;
        bgfx::setViewFrameBuffer(id, view->bgfxFbo);
        // Fresh depth inside the overlay rect: overlays draw on
        // top of the finished frame but depth-test within
        // themselves.
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_DEPTH),
                           clearColor, 1.0f, 0);
        bgfx::setViewRect(id, rx, ry, rw, rh);
        bgfx::setViewTransform(id, ovView, ovProj);
        bgfx::setViewMode(id, bgfx::ViewMode::Sequential);
        bgfx::touch(id);
    };
    auto configPresent = [&](int, uint16_t id) {
        // Standalone present: the default backbuffer; the
        // fullscreen triangle overwrites every pixel.
        //
        // On desktop the frame leaves through the Qt GL blit instead,
        // so this view draws only when there is an output colour
        // transform to apply -- into presentFbo, which is what the
        // blit then reads.
        //
        // Either way the view still targets a framebuffer that is NOT
        // bgfxFbo, and on desktop it does so even with nothing to draw,
        // ON PURPOSE: bgfx only resolves an MSAA framebuffer
        // (multisampled renderbuffer -> resolve texture) when the frame
        // transitions AWAY from it, and every desktop content view
        // targets bgfxFbo -- so without this trailing view the resolve
        // texture the composite blit reads stayed stale under MSAA (an
        // empty viewport). That is also what orders the resolve BEFORE
        // the present pass samples the scene colour.
        bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
#ifndef FC_RENDERER_STANDALONE
        if (view->outputTransform != Render::OutputConfig::None
                && bgfx::isValid(view->presentFbo))
            target = view->presentFbo;
#endif
        bgfx::setViewFrameBuffer(id, target);
        bgfx::setViewClear(id, uint16_t(BGFX_CLEAR_NONE),
                           clearColor, 1.0f, 0);
#ifdef FC_RENDERER_STANDALONE
        // A split-view submit presents into its sub-view's rect of the
        // backbuffer; the source UV stays 0..1 of this sub-view's own
        // full scene target (vs_fc_comp.sc), so this one rect is the
        // whole composition step (docs/SplitViews.md sec 9.2).
        if (subCtx.active)
            bgfx::setViewRect(id, uint16_t(subCtx.x), uint16_t(subCtx.y),
                              uint16_t(subCtx.w), uint16_t(subCtx.h));
        else
            bgfx::setViewRect(id, 0, 0, width, height);
#else
        bgfx::setViewRect(id, 0, 0, width, height);
#endif
        bgfx::setViewTransform(id, nullptr, nullptr);
        bgfx::setViewMode(id, bgfx::ViewMode::Default);
        bgfx::touch(id);
    };

    // The table. One entry per pass (or per contiguous group that
    // shares a predicate and a config closure), in enum -- i.e.
    // draw -- order. A null config is a pass that configures its
    // own views (the particle steps).
    using ConfigFn = std::function<void(int, uint16_t)>;
    struct PassDecl {
        bool live;       ///< does this frame draw the pass?
        ConfigFn config; ///< configure the pass's bgfx view id
    };
    std::vector<PassDecl> passTable;
    passTable.reserve(V::NUM_VIEWS);
    int16_t declOf[V::NUM_VIEWS];
    std::memset(declOf, 0xff, sizeof(declOf));
    auto declPasses = [&](int first, int last, bool live,
                          ConfigFn config) {
        for (int p = first; p <= last; ++p)
            declOf[p] = int16_t(passTable.size());
        passTable.push_back({live, std::move(config)});
    };
    auto declPass = [&](int p, bool live, ConfigFn config) {
        declPasses(p, p, live, std::move(config));
    };

    // Stateful particle simulation: two step ids per emitter
    // slot, claimed as a group whenever the scene carries an
    // emitter that could hold state -- stepParticles picks the
    // slots itself, after the map is decided.
    bool statefulParticles = false;
    for (const auto &d : scene) {
        const auto &sh = d.material.usershader;
        if (sh && sh->stage == "particle"
                && !sh->simulateSource.empty() && d.objectKey) {
            statefulParticles = true;
            break;
        }
    }
    // Non-on-top selections reroute their opaque draws into
    // ViewSelection (submit(), selPass) -- claim it whenever such
    // a feed exists or those draws land in the discard view.
    bool nonOntopSel = false;
    for (const auto &sel : selections) {
        if (sel.first <= 0 && !sel.second.empty()) {
            nonOntopSel = true;
            break;
        }
    }
    const bool reflActive = groundReflActive || waterReflActive;

    declPasses(V::ViewParticleSim0,
               int(V::ViewParticleSim0) + int(V::kParticleViews) - 1,
               statefulParticles, nullptr);
    declPass(V::ViewParticleImpact,
             statefulParticles && hasWaterBody && waterSurfActive,
             nullptr);
    declPass(V::ViewBackground, true, configScene);
    declPass(V::ViewSunDisc, true, configScene);
    declPass(V::ViewShadow, shadowRender, configShadow);
    declPasses(V::ViewShadowBlurH, V::ViewShadowBlurV,
               shadowBlurActive, configShadowBlur);
    declPass(V::ViewShadowTint, shadowRender, configShadowTint);
    declPasses(V::ViewShadowTintBlurH, V::ViewShadowTintBlurV,
               shadowBlurActive, configShadowBlur);
    for (int t = 0; t < V::kBulbShadowTiles; ++t)
        declPass(V::ViewBulbShadow0 + t, bulbShadowRender[t],
                 configBulb);
    declPass(V::ViewAOPrepass, prepassRender, configAOPrepass);
    declPass(V::ViewAOPrepassCap, prepassRender, configAOPrepassCap);
    for (int m = 0; m < 6; ++m)
        declPass(V::ViewAODepthMip1 + m,
                 ssaoActive && aoRender && m < view->aoMipCount,
                 configAOMip);
    declPasses(V::ViewWaterFront, V::ViewWaterBack,
               waterActive && mediumRender, configMedium);
    declPasses(V::ViewGlassFront, V::ViewGlassBack,
               glassActive && mediumRender, configMedium);
    declPasses(V::ViewCloudFront, V::ViewCloudBack,
               cloudActive && mediumRender, configMedium);
    declPasses(V::ViewFireFront, V::ViewFireBack,
               fireActive && mediumRender, configMedium);
    declPasses(V::ViewAOGen, V::ViewAOBlur2,
               ssaoActive && aoRender, configAOChain);
    declPass(V::ViewVolGen, volActive, configVolGen);
    declPass(V::ViewVolAccum, volActive, configVolAccum);
    declPasses(V::ViewGroundRefl, V::ViewReflMedia, reflActive,
               configRefl);
    declPass(V::ViewOpaque, true, configScene);
    declPass(V::ViewSelection, nonOntopSel, configScene);
    declPass(V::ViewSectionCap, true, configScene);
    declPass(V::ViewDebugScene, debugSceneRender || idPassRender,
             configDebugScene);
    declPass(V::ViewIdReadback, idReadbackWanted, configIdReadback);
    declPass(V::ViewCaptureDepth, captureWanted, configCaptureDepth);
    declPass(V::ViewCapture, captureWanted, configCapture);
    // Shared by the measurement and the culling that acts on it: both
    // rasterize boxes against the finished opaque depth, and it is the
    // view id that places them there. The software oracle rasterizes no
    // boxes at all, so it does not want this pass -- that is the point
    // of it, and reserving a view for it would leave the box draws in
    // the frame the culling is being priced against.
    declPass(V::ViewOcclusionProbe,
             debugconf.occlusion
                     || (cullconf.enabled && !cullconf.software),
             configScene);
    declPass(V::ViewCavity, cavityActive, configScene);
    declPass(V::ViewGroundReflApply, groundReflActive, configScene);
    declPass(V::ViewOutline, true, configScene);
    // An attached consumer's passes, scene run (docs/CAMSimRenderPort
    // .md sec 10.2): placed while the scene is still being composed,
    // so the caustics, the volumetric apply, water and glass and the
    // transparent bucket all see the consumer's output and the depth
    // its composite writes. A null config: the consumer states its
    // own targets, rects, clears and transforms through the facade's
    // setPass* calls, which run later in the frame than this loop and
    // so have the last word anyway. Declared one at a time rather
    // than as a group so that the ones it did not ask for are
    // declared-but-dead instead of undeclared, which the pass table
    // reports as a bug.
    const int consumerPassCount =
        (frameConsumer && consumerSurface && !subCtx.warm)
            ? int(consumerPasses) : 0;
    const int consumerOverlayCount =
        consumerPassCount ? int(consumerOverlayPasses) : 0;
    const int consumerSceneCount = consumerPassCount - consumerOverlayCount;
    for (int c = 0; c < int(V::NumConsumerSceneViews); ++c)
        declPass(V::ViewConsumerScene0 + c, c < consumerSceneCount,
                 nullptr);
    declPass(V::ViewCaustics, waterActive && volconf.caustics,
             configScene);
    declPass(V::ViewVolApply, volActive, configScene);
    declPass(V::ViewWaterCopy, waterSurfActive || glassActive,
             configWaterCopy);
    declPass(V::ViewWaterSurface, waterSurfActive, configScene);
    declPass(V::ViewGlassLineSdf, glassActive, configLineSdf);
    declPass(V::ViewGlassSurface, glassActive, configScene);
    declPass(V::ViewGlassLine, glassActive, configScene);
    declPass(V::ViewParticles, true, configScene);
    declPass(V::ViewTransparent, true, configTransparent);
    declPass(V::ViewOITComposite, oitActive, configScene);
    declPass(V::ViewSectionCapTransp, true, configScene);
    // The consumer's overlay run: translucent output testing the
    // frame's FINISHED depth, after the WBOIT resolve (sec 10.2).
    for (int c = 0; c < int(V::NumConsumerOverlayViews); ++c)
        declPass(V::ViewConsumerOverlay0 + c, c < consumerOverlayCount,
                 nullptr);
    declPasses(V::ViewBloomBright, V::ViewBloomBlurV, bloomActive,
               configBloom);
    declPass(V::ViewBloomApply, bloomActive, configScene);
    declPass(V::ViewUserPostCopy, userPostActive, configUserPostCopy);
    declPass(V::ViewUserPost, userPostActive, configUserPost);
    declPass(V::ViewDebug, true, configScene);
    declPass(V::ViewOnTop, true, configScene);
    declPass(V::ViewHighlight, true, configScene);
    for (int s = 0; s < int(V::NumOverlayViews); ++s)
        declPass(V::ViewOverlay0 + s, s < int(frameOverlays.size()),
                 configOverlay);
    // After the overlays: everything ahead of these two passes is what
    // the accumulation covers. An overlay drawn from its own camera is
    // bit-identical every frame and so averages to itself exactly,
    // while the in-scene overlays move with the jitter and converge
    // like the rest of the scene.
    declPass(V::ViewAccum, accumActive, configAccum);
    declPass(V::ViewAccumApply, accumActive, configAccumApply);
    declPass(V::ViewPresent, true, configPresent);

    {
        view->beginPasses();
        for (int p = 0; p < V::NUM_VIEWS; ++p) {
            if (declOf[p] < 0) {
                // A pass missing from the table can never render:
                // nothing marks it, so every submit to it lands in
                // the discard view. Say so once -- this catches a
                // new PassView added without a declaration.
                static bool warnedUndeclared = false;
                if (!warnedUndeclared) {
                    warnedUndeclared = true;
                    RENDER_ERR("bgfx pass table: pass " << p
                               << " has no declaration and cannot "
                                  "render");
                }
                continue;
            }
            view->markPass(p, passTable[size_t(declOf[p])].live);
        }

        if (!_BGFXLib.reserveBlock(view, view->passesNeeded())) {
            // The pool is full. Same answer as an unfittable viewer:
            // sit the frame out and let Coin composite the scene,
            // rather than submitting ids that belong to somebody
            // else or that bgfx would abort on.
            if (!_BGFXLib.warnedViewBudget) {
                _BGFXLib.warnedViewBudget = true;
                RENDER_ERR("Out of bgfx view ids: this 3D view needs "
                           << view->passesNeeded() << " of "
                           << _BGFXLib.poolSize()
                           << " and the open views hold the rest. It "
                              "falls back to Coin rendering; close "
                              "another 3D view to get it back.");
            }
            return false;
        }
        view->mapPasses();
    }

    // Stateful particle emitters advance before anything draws:
    // their views come first in id order and configure/submit
    // themselves, so the loop below leaves them alone. A frame
    // that still owes simulation steps keeps the view animating
    // (docs/RenderEngine.md §5.8) — that is how a frozen frame
    // reaches its warm-up state.
    if (view->stepParticles(scene, animTime, debugconf.freezeFrame)) {
        animatedFrame = true;
        // A frozen frame short of its warm-up is not the picture yet;
        // a live one is animating, which is never "incomplete".
        if (debugconf.freezeFrame)
            frameOwes = true;
    }
    // ... and immediately hand what they hit to the water, which is
    // the only consumer that has to see it before anything draws.
    view->splatImpacts(animTime, debugconf.freezeFrame,
                       hasWaterBody && waterSurfActive ? waterFoot
                                                       : nullptr);
    // The mirrored render is cached against a static frame, and a
    // live emitter changes the picture every frame without ever
    // touching the dirty flags — its motion is in state textures,
    // not in the scene. Without this the reflection keeps whatever
    // it was rendered from, which for a fountain means a pool
    // reflecting everything except the jet standing in it. Same
    // escape hatch the analytic media already take.
    reflRender = !staticFrame || shadowRender || fireActive
        || cloudActive || view->particlesLive
        // Per accumulation sample, for the reason at accumSampleIndex.
        // This is the costliest of the three and the one worth the
        // most: the mirror is a second full render of the scene, and it
        // also covers the most pixels. Measured on a ground-reflection
        // scene where it touched 63% of the frame, leaving it frozen
        // did not merely fail to refine it -- it held the WHOLE frame
        // back, closing 0.067 of the distance to a supersampled render
        // against 0.295 with the reflection switched off.
        || view->reflSampleIndex != accumSampleIndex;
    if (reflRender)
        view->reflSampleIndex = accumSampleIndex;

    // Configure the ids the pass map handed out, through the same
    // table that declared them. A pass the frame did not claim has
    // no id to configure: it is not drawn. A null config is a pass
    // that configures its own views (the particle steps, in
    // stepParticles/splatImpacts above).
    for (int i = 0; i < V::NUM_VIEWS; ++i) {
        if (!view->passLive(i) || declOf[i] < 0)
            continue;
        const auto &decl = passTable[size_t(declOf[i])];
        if (decl.config)
            decl.config(i, view->vid(i));
    }

    // The discard view every unclaimed pass maps to: a 1x1 scratch
    // target, configured but not touched, so a draw that lands here
    // is thrown away instead of reaching the scene or an id that
    // belongs to another pass. Nothing should submit to it -- the
    // count is reported after the frame.
    bgfx::setViewFrameBuffer(view->sinkView, view->sinkFbo);
    bgfx::setViewClear(view->sinkView, uint16_t(BGFX_CLEAR_NONE),
                       clearColor, 1.0f, 0);
    bgfx::setViewRect(view->sinkView, 0, 0, 1, 1);
    bgfx::setViewTransform(view->sinkView, nullptr, nullptr);
    bgfx::setViewMode(view->sinkView, bgfx::ViewMode::Default);

    // One tick per wall-clock frame: a multi-sub-view frame stamps
    // every submit with the same number, so the mesh TTL
    // (lastUsed + 2 < frame) keeps meaning frames, not submits.
    if (!subCtx.active || subCtx.first)
        ++view->frame;
    view->drawcount = 0;
    // The submission phase starts here. A bgfx uniform holds its
    // value for the rest of the frame once set, so setting the
    // colour space ONCE, ahead of every submit, is what covers the
    // draws that never pass through submit() -- the background
    // quad, the overlays, the section caps. The hot paths set it
    // again per draw rather than depend on that reasoning.
    view->setColorSpaceUniform();
    view->bufferDeniedSubmits = view->bufferDeniedMeshes = 0;
    view->autozoomScale = autozoomScale;
    if (pbrActive && pbrconf.envBackground)
        view->submitEnvBackground();
    else
        view->submitBackground(background);
    // Visible sun along the directional scene light (needs the
    // view-space light state the light section filled above -- the
    // sun is where the light is, with or without a shadow map).
    if (getenv("FC_BGFX_DEBUG_FEED"))
        fprintf(stderr,
                "bgfx sun: valid=%d spot=%d disc=%d size=%g frame=%d"
                " dir=%g,%g,%g\n",
                lightconf.valid, lightconf.spot, lightconf.sunDisc,
                lightconf.sunDiscSize, view->lightFrame,
                view->lightDirView[0], view->lightDirView[1],
                view->lightDirView[2]);
    if (lightconf.valid && !lightconf.spot && lightconf.sunDisc
            && view->lightFrame)
        view->submitSunDisc(lightconf.sunDiscSize);
    const float *viewMat = reinterpret_cast<const float *>(viewMatrix);
    view->viewMatrix = viewMat;
    view->projMatrix = reinterpret_cast<const float *>(projMatrix);

    // Frustum culling: world-space clip planes extracted from the
    // camera view-projection (Gribb-Hartmann, against this backend's
    // clip volume -- the projection was remapped to it at the top of
    // the frame). A scene draw whose world bounds
    // lie fully outside any plane skips its color/water/prepass
    // submits below. Shadow casters are exempt — off-screen geometry
    // still casts into the view — as are autozoom draws (their model
    // matrix rebuilds per frame, so the fed bounds are stale) and
    // draws without bounds. Hidden-line frames skip culling like
    // instancing does (the outline passes rework the fill submits).
    static const bool noCulling =
        getenv("FC_BGFX_NO_CULLING") != nullptr;
    std::vector<uint8_t> sceneCulled;
    // Which node's verdict cut each row, -1 for rows occlusion did
    // not cut (docs/FarFieldProxies.md §12.10). Filled by the
    // culler only when the audit asks for it.
    std::vector<int32_t> cullOwner;
    if (!noCulling && !hlconfig.show && !scene.empty()) {
        // Frustum + occlusion together: both walk the whole draw
        // list, and the question this answers is how much of the
        // fixed per-frame cost is the cull at all.
        CpuScope cullTiming(this, CpuCull);
        float vp[16];
        bx::mtxMul(vp, viewMat,
                   reinterpret_cast<const float *>(projMatrix));
        // Row-vector convention (v' = v * M): clip component i is
        // dot(v, column i); plane k folds column 3 with the column
        // of its axis. That holds for five of the six planes on both
        // conventions -- only the near one differs, being w + z under
        // GL's [-1,1] clip volume and plain z under the [0,1] volume
        // every other backend uses.
        const bgfx::Caps *clipCaps = bgfx::getCaps();
        const bool glClip = !clipCaps || clipCaps->homogeneousDepth;
        float planes[6][4];
        for (int k = 0; k < 6; ++k) {
            int axis = k >> 1;
            float sign = (k & 1) ? -1.0f : 1.0f;
            const bool nearPlane = axis == 2 && !(k & 1);
            for (int r = 0; r < 4; ++r)
                planes[k][r] = (nearPlane && !glClip ? 0.0f
                                                     : vp[r * 4 + 3])
                    + sign * vp[r * 4 + axis];
        }
        sceneCulled.assign(scene.size(), 0);
        size_t nculled = 0;
        for (size_t i = 0; i < scene.size(); ++i) {
            const auto &d = scene[i];
            if (d.bboxMin[0] > d.bboxMax[0]
                    || !d.material.autozoom.empty())
                continue;
            // An emitter draws outside its own bounds: test the
            // box its billboards can actually reach, not the one
            // its anchors sit in.
            const float pad = drawHeadroom(d);
            const float lo[3] = {d.bboxMin[0] - pad,
                                 d.bboxMin[1] - pad,
                                 d.bboxMin[2] - pad};
            const float hi[3] = {d.bboxMax[0] + pad,
                                 d.bboxMax[1] + pad,
                                 d.bboxMax[2] + pad};
            for (const auto &pl : planes) {
                // Positive-vertex test: the bbox corner farthest
                // along the plane normal decides containment.
                float dist = pl[3]
                    + pl[0] * (pl[0] >= 0.0f ? hi[0] : lo[0])
                    + pl[1] * (pl[1] >= 0.0f ? hi[1] : lo[1])
                    + pl[2] * (pl[2] >= 0.0f ? hi[2] : lo[2]);
                if (dist < 0.0f) {
                    sceneCulled[i] = 1;
                    ++nculled;
                    break;
                }
            }
        }
        if (getenv("FC_BGFX_DEBUG_CULL"))
            fprintf(stderr, "bgfx cull: %zu of %zu scene draws\n",
                    nculled, scene.size());

        // The frame-level A/B (12.13): is the culling paying for
        // itself on THIS scene and camera? The probe alternates
        // stretches of frames with the whole occlusion block on
        // and off and compares median frame cost -- the off arm
        // skips the oracle entirely, so its frames cost what not
        // deciding costs, which is the alternative actually on
        // offer. Fed bgfx's cpuTimeFrame, the whole application
        // frame: it lags the arm by one frame, which the arms'
        // warm-up discards. The verdict gates nothing here beyond
        // the probe's own arms; it is printed for the
        // wire-or-delete decision to read.
        bool benefitCull = true;
        if (cullconf.enabled && cullconf.benefitProbe) {
            if (!cullBenefitOn) {
                cullBenefit.reset();
                cullBenefitOn = true;
            }
            const bgfx::Stats *bs = bgfx::getStats();
            if (bs && bs->cpuTimerFreq > 0)
                cullBenefit.frame(
                        float(1000.0 * double(bs->cpuTimeFrame)
                              / double(bs->cpuTimerFreq)),
                        double(bx::getHPCounter())
                            / double(bx::getHPFrequency()));
            benefitCull = cullBenefit.cullThisFrame();
        }
        else {
            cullBenefitOn = false;
        }

        // Occlusion culling (docs/FarFieldProxies.md §12), on top of
        // the frustum rejections just computed and into the same
        // mask. It runs under the same conditions for the same
        // reasons: a hidden-line frame reworks the fill submits
        // wholesale, so a mask over them means nothing.
        if (cullconf.enabled && benefitCull) {
            const bgfx::Caps *caps = bgfx::getCaps();
            culler.configure(cullconf);
            if (cullBuiltVersion != cullSceneVersion
                    || culler.empty()) {
                const int64_t started = bx::getHPCounter();
                std::vector<Render::ProxyInstance> instances;
                cullInstances(scene, instances, &cullExemptOnTop);
                cullIndexed = uint32_t(instances.size());
                culler.build(instances);
                // ⚠️ And drop every test in flight with the index it
                // was asked about. A lease names a node by *index*,
                // and a rebuild renumbers them — OcclusionCuller's
                // own rule is that verdicts never survive a rebuild,
                // for exactly this reason: an answer applied to the
                // wrong node hides geometry that was never tested.
                // The answers are already gone with the state; these
                // are the questions.
                cullQueries.releaseAll();
                cullBuildMs = 1000.0
                    * double(bx::getHPCounter() - started)
                    / double(bx::getHPFrequency());
                cullBuiltVersion = cullSceneVersion;
            }
            // Handles are created per test inside, not pooled here:
            // a bgfx query handle is an object's identity, and one
            // reassigned to a second node answers with the first
            // one's verdict (OcclusionLeases). A backend that
            // refuses to create them costs culling and not
            // correctness -- an untested node draws.
            // The attribution is only maintained while the audit is
            // on: it is a second vector the width of the draw list,
            // written on every masked row of every frame, and
            // nothing but the readout reads it.
            std::vector<int32_t> *owner =
                    debugconf.cullAudit ? &cullOwner : nullptr;
            const float *projf =
                    reinterpret_cast<const float *>(projMatrix);
            if (cullconf.software) {
                // KEY: Rasterize this camera's occluders and spend
                // them in the same breath. No handles, no in-flight
                // tests, no verdicts carried across the frame
                // boundary -- the answer is used where it is
                // computed, which is the whole of what section 12.12
                // changes. It also needs nothing of the backend but
                // the depth convention, so it is the path that
                // survives into WebGL2 unaltered.
                Render::MaskedCullConfig mc;
                mc.resolutionDivisor = int(cullconf.softwareDivisor);
                mc.triangleBudget = cullconf.occluderTriangles;
                mc.minOccluderPx = cullconf.minOccluderPx;
                mc.threads = cullconf.softwareThreads;
                mc.simdFilter = cullconf.softwareSimd;
                mc.testInstances = cullconf.perInstance;
                mc.coarse.enabled = cullconf.coarseOccluders;
                mc.coarse.level = cullconf.coarseLevel;
                mc.coarse.minTriangles = cullconf.coarseMinTriangles;
                mc.coarse.buildsPerFrame = cullconf.coarseBuilds;
                mc.coarse.memoryCap = cullconf.coarseMemory;
                mc.coarseBias = cullconf.coarseBias;
                maskedCull.configure(mc);
                maskedCull.build(scene, viewMat, projf,
                                 caps ? caps->homogeneousDepth : true,
                                 int(view->width), int(view->height));
                maskedCull.cull(culler.hierarchy(), viewMat, projf,
                                float(view->height), sceneCulled, owner);
            }
            else if (caps && (caps->supported & BGFX_CAPS_OCCLUSION_QUERY)
                     && !subCtx.active) {
                // Bypassed for sub-view frames: a query's verdict is
                // per-camera and lands frames later -- alternating N
                // cameras through the shared lease pool would apply
                // one cell's answers to another's frame. The software
                // masked cull above is stateless per pass and keeps
                // working (docs/SplitViews.md sec 9.2).
                driveOcclusionCull(*view, culler, cullBatch, cullQueries,
                                   viewMat, projf,
                                   float(view->height), sceneCulled,
                                   owner);
            }
            else if (!cullUnsupported) {
                cullUnsupported = true;
                RENDER_ERR("render culling: this backend reports no "
                           "occlusion query support; occlusion culling "
                           "is off and only frustum culling applies");
            }

            // Fold the finished mask per SOURCE for the level plan
            // (occlusion as a memory mechanism). Software oracle
            // only: its verdicts are exact per frame, where the
            // query path's flap -- fed into demotes -- would become
            // an upload per flip. A source's streak advances only
            // on a frame where EVERY draw carrying its tag was
            // culled; one visible row resets it.
            if (cullconf.software && cullconf.demoteStreak) {
                ++occlStreakFold;
                for (size_t i = 0; i < scene.size(); ++i) {
                    const auto &d = scene[i];
                    if (!d.mesh || !d.mesh->sourceTag)
                        continue;
                    const bool cut = i < sceneCulled.size()
                        && sceneCulled[i] != 0;
                    auto &st = occlHiddenStreak[d.mesh->sourceTag];
                    if (st.fold == occlStreakFold) {
                        // A later row of the same tag can only
                        // revoke, never extend.
                        if (!cut)
                            st.frames = 0;
                    }
                    else {
                        const bool consecutive =
                            st.fold + 1 == occlStreakFold;
                        st.frames = cut
                            ? (consecutive ? st.frames + 1 : 1) : 0;
                        st.fold = occlStreakFold;
                    }
                }
                // Prune what the scene no longer names, on a slow
                // cadence: tag addresses can be reused, and the
                // fold-generation check above is what keeps a
                // stale entry inert in the meantime.
                if ((occlStreakFold & 1023) == 0) {
                    for (auto it = occlHiddenStreak.begin();
                         it != occlHiddenStreak.end();) {
                        if (it->second.fold != occlStreakFold)
                            it = occlHiddenStreak.erase(it);
                        else
                            ++it;
                    }
                }
            }
        }
    }
    // Scene draws only — the argument must reference into `scene`.
    auto culled = [&](const Render::DrawCall &d) {
        if (sceneCulled.empty())
            return false;
        size_t idx = size_t(&d - scene.data());
        return idx < sceneCulled.size() && sceneCulled[idx] != 0;
    };

    auto isTriangle = [](const Render::DrawCall &d) {
        return d.material.type == Render::Material::Triangle;
    };
    auto isTransp = [](const Render::DrawCall &d) {
        return d.material.transparent
            || (d.material.pervertexcolor
                && d.mesh && d.mesh->hasTransparency);
    };

    // Decide whether on-top lines/points need the two-pass hidden-line
    // rendering (SoFCRenderer's hassel/hasontop/hlwholeontop check).
    bool sceneOnTopTri = false, sceneOnTopLine = false;
    for (const auto &draw : scene) {
        if (!draw.material.ontop)
            continue;
        (isTriangle(draw) ? sceneOnTopTri : sceneOnTopLine) = true;
    }
    bool selOnTopLine = false;
    for (const auto &sel : selections) {
        if (sel.first <= 0)
            continue;
        for (const auto &draw : sel.second) {
            if (!isTriangle(draw)) {
                selOnTopLine = true;
                break;
            }
        }
    }
    bool sceneTwoPass = sceneOnTopTri && sceneOnTopLine;
    bool twoPass = sceneTwoPass || selOnTopLine || hlWholeOnTop;


    // The same object selected through several ids draws its
    // whole-object geometry only once (GL's renderkeys dedup in
    // SoFCRendererP::updateSelection): the first draw of a given
    // (objectKey, cacheId, primitive type, model matrix) wins, on-top
    // selections considered first like the GL loop order. The matrix
    // hash keeps distinct placements of one shared geometry cache
    // (TShape instancing) apart — only true duplicates collapse.
    dupDraws.clear();
    {
        auto matrixHash = [](const Render::DrawCall &d) -> uint64_t {
            if (d.identity)
                return 0;
            uint64_t h = 1469598103934665603ull;
            const uint8_t *p =
                reinterpret_cast<const uint8_t *>(d.model);
            for (size_t i = 0; i < sizeof(d.model); ++i) {
                h ^= p[i];
                h *= 1099511628211ull;
            }
            return h ? h : 1;
        };
        std::set<std::tuple<uint64_t, uint64_t, uint8_t, uint64_t>> seen;
        auto dedup = [&](const Render::DrawCallList &draws) {
            for (const auto &draw : draws) {
                if (!draw.wholeObject || !draw.objectKey || !draw.mesh)
                    continue;
                if (!seen.emplace(draw.objectKey, draw.mesh->cacheId,
                                  draw.material.type,
                                  matrixHash(draw)).second)
                    dupDraws.insert(&draw);
            }
        };
        // Explicitly colored selections (SelIdSelected) win over the
        // implicit whole-on-top companions an element selection adds
        // — both can carry whole-object draws of the same geometry
        // (e.g. the instanced whole-highlight degradation), and the
        // tinted one is the visible signal.
        for (const auto &sel : selections) {
            if (sel.first > 0 && (sel.first & Render::SelIdSelected))
                dedup(sel.second);
        }
        for (const auto &sel : selections) {
            if (sel.first > 0 && !(sel.first & Render::SelIdSelected))
                dedup(sel.second);
        }
        for (const auto &sel : selections) {
            if (sel.first <= 0)
                dedup(sel.second);
        }
    }
    auto isDup = [this](const Render::DrawCall &d) {
        return !dupDraws.empty() && dupDraws.count(&d);
    };
    // A partial face whose outline replaces its fill (GL's
    // NoPreSelFaceHighlightWithOutline / NoSelFaceHighlightWith-
    // Outline skip in renderHighlight ~2159).
    auto outlineOnly = [](const Render::DrawCall &d) {
        return d.material.faceoutline && d.material.outlineonly
            && d.partIndex >= 0;
    };

    // Hidden-line draw style rules for scene draws (GL: renderOpaque
    // ~2012 / renderTransparency's notriangle + renderLines/
    // renderPoints + renderOutline with highlight=false). The
    // materials carry the outline flag; the per-frame config decides
    // face/seam/vertex hiding and the outline shape.
    const Render::HiddenLineConfig &hl = hlconfig;
    uint32_t outlineRef = 0;
    auto hideFill = [&](const Render::DrawCall &d) {
        // GL skips opaque fills of outline materials and, with the
        // transparency override, every transparent scene fill — and
        // with them their outlines.
        return hl.show && hl.hideFace && isTriangle(d)
            && (d.material.outline || isTransp(d));
    };
    auto hidePoints = [&](const Render::DrawCall &d) {
        return hl.hideVertex && d.material.outline
            && d.material.type == Render::Material::Point
            && d.partIndex < 0;
    };
    auto sceneNoSeam = [&](const Render::DrawCall &d) {
        return hl.hideSeam && d.material.outline
            && d.material.type == Render::Material::Line
            && d.partIndex < 0;
    };

    // The memory gates (docs/SceneStreaming.md #13b): edge and point
    // drawables are the GPU's most expensive geometry per unit of
    // screen information -- a segment is 8 bytes of index in the
    // heap and those 8 bytes plus a 64-byte quad-expansion instance
    // record on the GPU, a point 4 against 4 + 32 -- and measured on
    // the rack model the GPU/CPU ratio climbs to 8-12x as the faces
    // coarsen away, which is that signature and nothing else.
    //
    // Suppressing a draw frees the memory, but no longer as a side
    // effect: publication-keyed retention keeps every published
    // mesh, so the gate walk below hands the collector the set
    // whose draws are ALL suppressed (gatedOnlyMeshes), and those
    // fall back to the recency grace they always fail. No rebuild,
    // no re-tessellation, and the way back is one on-demand upload
    // -- which is why these are spent before any rung is given up.
    //
    // THE ELEMENT CONTRACT, strictly ordered:
    //
    // - A FLOATING point or line set (attachedOnly false: it holds
    //   a vertex no edge attaches, or an edge no face attaches)
    //   ranks WITH THE FACES. It is the object; it is never gated
    //   here.
    // - An attached LINE set draws only while its object's face
    //   set is shown and memory allows.
    // - An attached POINT set draws only while its object's line
    //   set is shown and memory allows.
    // - Under pressure the classes are spent points -> lines ->
    //   faces, and taken back faces -> lines -> points (the
    //   staged latch below; faces move on the plan's cadence).
    // - On-top and highlight draws are never gated: picking and
    //   selection must look the same under pressure as without.
    //
    // "Shown" is decided inside this frame, dependency-ordered:
    // faces first, lines against the face verdict, points against
    // the line verdict. An object with no companion draw AT ALL
    // splits on objectIncomplete: a capture-budget-deferred
    // companion is LATE and the dependent set waits for it, while
    // an absent one is a display mode showing its own subject
    // (Points/Wireframe), which cannot be allowed to show nothing.
    // That split is what stops an adopted point or line cache from
    // drawing frames ahead of the face set the publish budget held
    // back -- the dots-first load storm.
    std::set<uint64_t> objectsWithTriangles, objectsWithAttLines,
        objectsWithFloatLines, incompleteObjects, objectsCoarseFaces;
    gatedPoints = gatedLines = gateEligible = gatedByDependency = 0;
    gatedByCoarse = 0;
    gatedTiny = 0;
    gatedTinyPrims = 0;
    auditDrawn = auditNoFaces = auditCoarse = 0;
    auditFloating = auditFloatingNoFaces = auditFloatingDrawn = 0;
    for (const auto &d : scene) {
        if (d.mesh && d.mesh->attachedOnly)
            ++gateEligible;
        if (!d.objectKey)
            continue;
        if (d.objectIncomplete)
            incompleteObjects.insert(d.objectKey);
        if (d.material.ontop)
            continue;
        if (isTriangle(d)) {
            objectsWithTriangles.insert(d.objectKey);
            // An object is coarse if ANY of its face meshes is: the
            // decoration describes the whole object, and half of it
            // being a rough rung is enough to make the description
            // wrong. levelError is 0 only on the exact tessellation.
            if (d.mesh && d.mesh->levelError > 0.0f)
                objectsCoarseFaces.insert(d.objectKey);
        }
        else if (d.material.type == Render::Material::Line)
            (d.mesh && d.mesh->attachedOnly ? objectsWithAttLines
                                            : objectsWithFloatLines)
                .insert(d.objectKey);
    }
    // The staged pressure latch. gpuOverBudget ARMS a stage;
    // pressureStanding HOLDS every armed stage: the collector
    // retires what a stage suppresses, so the moment it fires the
    // total falls back under budget, and a latch reading only the
    // instantaneous bit would re-open into the memory it just
    // freed and oscillate with the collector (the period-4, 75MB
    // wave by another route). Escalation waits elemGateStagger
    // frames so the collector's census can answer whether points
    // alone were enough before the lines go too; release walks
    // the same stairs backwards, one stage per stagger, and only
    // once the ladder has given back ALL raised error -- the
    // cheapest thing to give up is the last thing taken back.
    //
    // MEASURED, and this is why the release is PRICED and not merely
    // staggered (docs/SceneStreaming.md 13b): handing the line class
    // back on the strength of the stagger alone re-admitted 126MB on
    // ONE frame -- the whole settled scene again, 55MB past the
    // restored budget -- so the pressure controller slammed the
    // tolerance to its 1654px ceiling, the latch re-escalated, and the
    // scene ended up worse than before it released (6.46px at 82.6MB
    // where it had held 2.00px at 76.3MB), at a cost of 104 event-loop
    // gaps over 200ms. A stagger DELAYS a re-admission; it never asks
    // what it costs. This does, on the uploader's own arithmetic.
    auto readmitBytes = [&](Render::Material::Type type) {
        std::unordered_set<uint64_t> counted;
        uint64_t bytes = 0;
        for (const auto &d : scene) {
            if (!d.mesh || !d.mesh->attachedOnly || !d.objectKey
                    || d.material.ontop || d.material.highlightline
                    || d.material.type != type)
                continue;
            // Per MESH, not per draw: the upload is keyed by cacheId
            // and several draws share one, so a per-draw sum prices
            // the same buffer many times over.
            if (!counted.insert(d.mesh->cacheId).second)
                continue;
            bytes += GpuMesh::readmitCost(*d.mesh,
                                          type == Render::Material::Line);
        }
        return bytes;
    };
    {
        const bool pressed = gpuOverBudget || pressureStanding;
        if (gpuOverBudget) {
            if (elemPressureStage == 0) {
                elemPressureStage = 1;
                elemStageFrames = 0;
            }
            else if (elemPressureStage == 1
                     && ++elemStageFrames >= elemGateStagger) {
                elemPressureStage = 2;
                elemStageFrames = 0;
            }
        }
        else if (!pressed && elemPressureStage > 0) {
            if (++elemStageFrames >= elemGateStagger) {
                // Stage 2 hands the LINES back, stage 1 the POINTS --
                // the reverse of the order they were spent in.
                const uint64_t price = readmitBytes(
                    elemPressureStage >= 2 ? Render::Material::Line
                                           : Render::Material::Point);
                // The budget accessors are the desktop's: the browser
                // tier keeps its memory on the client ladder and never
                // arms gpuOverBudget in the first place, so its latch
                // never escalates and this branch never runs there.
                // No budget to weigh it against means no reason to
                // hold -- the gates exist to serve a budget.
                uint64_t headroom = 0;
                bool affordable = true;
#ifndef FC_RENDERER_STANDALONE
                const size_t budget = gpuBudgetBytes();
                const uint64_t used = gpuUsedBytes();
                headroom = budget > used ? budget - used : 0;
                affordable = !budget || price <= headroom;
#else
                (void)price;
#endif
                if (affordable) {
                    --elemPressureStage;
                    elemStageFrames = 0;
                }
                else if (levelDebug() && !elemReleaseHeld) {
                    // Said once per crossing, not per frame: a latch
                    // that stays put for a REASON is the thing the
                    // measurement could not distinguish from a latch
                    // that was stuck.
                    FC_RENDER_MSG(
                        "render levels: element gate stage %d holds -- "
                        "handing back %s would upload %.1fMB into "
                        "%.1fMB of headroom\n",
                        elemPressureStage,
                        elemPressureStage >= 2 ? "the lines" : "the points",
                        double(price) / 1048576.0,
                        double(headroom) / 1048576.0);
                }
                elemReleaseHeld = !affordable;
            }
        }
        else {
            // Under budget but raised error still standing: hold
            // every armed stage where it is.
            elemStageFrames = 0;
        }
    }
    // A document still arriving forces both drops for the load's
    // duration (13b.1): it is the moment the tier can least afford
    // the two classes and least use them, and the way back is one
    // frame. ShapeVertices off keeps attached points dark outright
    // (the pre-contract default); PressureDropEdges off exempts
    // lines from the pressure stages (not from the load).
    // Measurement only; see the parameter's own documentation.
    const int tinyCutoff = tinyElementCutoff;
    const bool dropPoints = !shapeVerticesOn || loadDropElements
        || elemPressureStage >= 1;
    const bool dropLines = loadDropElements
        || (pressureDropEdges && elemPressureStage >= 2);
    // The line-set verdict per object, which the point rule chains
    // on: a gated line set gates the points that lean on it.
    auto lineSetShown = [&](uint64_t obj) {
        if (objectsWithFloatLines.count(obj))
            return true;
        if (!objectsWithAttLines.count(obj))
            return false;
        if (!objectsWithTriangles.count(obj))
            return incompleteObjects.count(obj) == 0;
        return !dropLines && !objectsCoarseFaces.count(obj);
    };
    // `dependency`, when passed, comes back true if the DEPENDENCY
    // rule alone held this draw back -- its companion class is absent
    // and incomplete, or present and itself gated -- as opposed to
    // the memory pressure and the parameters, which would have gated
    // it whatever its companions were doing. The two are worth
    // telling apart: the dependency half is the part that needs
    // objectIncomplete carried to a tier, and a tier need not carry
    // what never fires.
    auto gatedForMemory = [&](const Render::DrawCall &d,
                              bool *dependency = nullptr) {
        // The measurement instrument (Render_TinyElementCutoff, 0 =
        // off), and it sits ABOVE the contract on purpose: the
        // population it exists to price is the FLOATING sets, which
        // the contract never gates by design, so a cutoff applied
        // after the attachment test would suppress nothing that
        // matters (docs/FarFieldProxies.md 11.1i). Picking, highlight
        // and on-top draws stay exempt, as they are for every other
        // gate here.
        if (tinyCutoff > 0 && d.mesh && !d.material.ontop
                && !d.material.highlightline
                && (d.material.type == Render::Material::Line
                    || d.material.type == Render::Material::Point)) {
            const uint32_t prims = Render::drawPrimitives(d);
            // A draw of zero primitives is one whose fill has not run;
            // it paints nothing and suppressing it would flatter the
            // measurement with draws that were never costing anything.
            if (prims > 0 && prims <= uint32_t(tinyCutoff))
                return true;
        }
        if (!d.mesh || !d.mesh->attachedOnly || !d.objectKey
                || d.material.ontop || d.material.highlightline)
            return false;
        if (d.material.type == Render::Material::Point) {
            if (!objectsWithAttLines.count(d.objectKey)
                    && !objectsWithFloatLines.count(d.objectKey)) {
                // No line set in the scene at all: a late companion
                // is waited for, Points mode draws its subject.
                const bool late = incompleteObjects.count(d.objectKey) != 0;
                if (dependency)
                    *dependency = late;
                return late;
            }
            if (dependency)
                *dependency = !dropPoints && !lineSetShown(d.objectKey);
            return dropPoints || !lineSetShown(d.objectKey);
        }
        if (d.material.type == Render::Material::Line) {
            if (!objectsWithTriangles.count(d.objectKey)) {
                // The same split: late face set vs Wireframe.
                const bool late = incompleteObjects.count(d.objectKey) != 0;
                if (dependency)
                    *dependency = late;
                return late;
            }
            // The COARSENESS half of the dependency: an edge set
            // describes the shape its faces approximate, so drawing it
            // over a rough rung decorates geometry that is not the
            // answer yet -- and it is exactly what put edges and dots
            // on screen the moment a load finished, with 61% of the
            // model still coarse. Memory has nothing to do with it,
            // which is why no pressure stage could express it.
            const bool coarse = objectsCoarseFaces.count(d.objectKey) != 0;
            if (dependency)
                *dependency = coarse && !dropLines;
            return dropLines || coarse;
        }
        return false;
    };
    // Tallied HERE, once per draw, and not inside the predicate:
    // the predicate is asked by the id pass and the submit loop
    // both, so counting inside it reported every draw two and three
    // times over -- the first reading had all three counters equal,
    // which is arithmetically impossible for a population split
    // between points and lines.
    //
    // The same walk decides what the gates hand the collector: a
    // mesh EVERY scene draw of which is gated cannot be submitted,
    // so keeping its buffers "because it is published" holds
    // memory no frame can use -- measured at 74.5MB of edge
    // buffers on the rack model's inside camera, uploaded through
    // the gate's open moments and then retained forever. Meshes a
    // single ungated draw still names are left alone: that draw's
    // submission advances lastUsed and the recency grace never
    // bites them.
    gatedOnlyMeshes.clear();
    {
        std::unordered_set<uint64_t> submittable;
        for (const auto &d : scene) {
            if (!d.mesh)
                continue;
            bool byDependency = false;
            if (gatedForMemory(d, &byDependency)) {
                ++(d.material.type == Render::Material::Point
                       ? gatedPoints : gatedLines);
                if (byDependency)
                    ++gatedByDependency;
                // What the measurement cutoff took, kept apart from
                // what the contract took. Recomputed rather than
                // reported out of the predicate: it is asked by the id
                // pass and the submit loop as well, and a counter
                // inside it counts every draw two and three times over
                // -- the trap the comment below this block records.
                // A silent cap reads as "the contract did this", which
                // is exactly the misattribution the knob is meant to
                // avoid.
                if (tinyCutoff > 0) {
                    const uint32_t prims = Render::drawPrimitives(d);
                    if (prims > 0 && prims <= uint32_t(tinyCutoff)) {
                        ++gatedTiny;
                        gatedTinyPrims += prims;
                    }
                }
                // The coarseness half on its own, because it is the
                // one a user reads off the screen: these are sets held
                // back by unfinished geometry, not by memory.
                if (d.objectKey && objectsCoarseFaces.count(d.objectKey))
                    ++gatedByCoarse;
                gatedOnlyMeshes.insert(d.mesh->cacheId);
            }
            // THE AUDIT: every attached point or line set this frame
            // will actually submit, checked against the contract it is
            // supposed to obey -- its faces present, and EXACT. A
            // count of what the gate suppressed cannot answer "why is
            // there a dot on screen"; only the surviving draws can,
            // and they are the ones nobody was counting.
            //
            // Edge-triggered, because the failure being chased is a
            // FLASH: a violation that lasts three frames is invisible
            // to anything printed on the plan's cadence.
            else if (d.mesh && d.objectKey
                     && !d.material.ontop && !d.material.highlightline
                     && (d.material.type == Render::Material::Point
                         || d.material.type == Render::Material::Line)) {
                ++auditDrawn;
                // The population BOTH the gate and the first version
                // of this audit were blind to: attachedOnly is false
                // BY DEFAULT, meaning "the producer has not classified
                // this", and an unclassified set is treated as
                // floating -- never gated, always drawn. A drawable
                // published before its attachment is known therefore
                // draws over nothing at all, which is what a dots-only
                // screen at the start of a load looks like. Counted
                // separately because "floating" and "not classified
                // yet" are indistinguishable in the flag and could not
                // be more different on screen.
                if (!d.mesh->attachedOnly) {
                    ++auditFloating;
                    // ...and of those, the ones that actually PUT
                    // SOMETHING ON SCREEN. A drawable whose fill has
                    // not run yet is submitted empty and paints
                    // nothing, so counting submissions and calling
                    // them visible dots is how a red herring gets
                    // mistaken for a diagnosis.
                    const bool hasGeom =
                        d.material.type == Render::Material::Point
                            ? d.mesh->numPointIndices > 0
                            : d.mesh->numLineIndices > 0;
                    if (hasGeom)
                        ++auditFloatingDrawn;
                    if (!objectsWithTriangles.count(d.objectKey))
                        ++auditFloatingNoFaces;
                    submittable.insert(d.mesh->cacheId);
                    continue;
                }
                if (!objectsWithTriangles.count(d.objectKey))
                    // Drawn with NO face set in the scene at all: this
                    // is the display-mode exemption firing. Legitimate
                    // for a real Wireframe/Points object, and a
                    // contract violation for an ordinary solid whose
                    // faces merely have not arrived in this frame.
                    ++auditNoFaces;
                else if (objectsCoarseFaces.count(d.objectKey))
                    ++auditCoarse;
                submittable.insert(d.mesh->cacheId);
            }
            else
                submittable.insert(d.mesh->cacheId);
        }
        for (uint64_t id : submittable)
            gatedOnlyMeshes.erase(id);
    }
    // The audit's verdict, on every change of it. A violation that
    // appears for three frames and clears is exactly the "flash of all
    // the edges just before it settles" a user reports and no
    // plan-cadence readout can catch, so this prints on the crossing
    // and prints the clearing too -- the frame number makes the two
    // ends of a flash measurable.
    if (auditNoFaces != auditSeenNoFaces || auditCoarse != auditSeenCoarse
            || auditFloatingNoFaces != auditSeenFloating) {
        auditSeenNoFaces = auditNoFaces;
        auditSeenCoarse = auditCoarse;
        auditSeenFloating = auditFloatingNoFaces;
        if (levelDebug())
            FC_RENDER_MSG(
                "render levels: element audit frame %llu: %zu point/line "
                "draws submitted | attached: %zu OVER COARSE FACES, %zu "
                "with NO FACE SET | unclassified (floating or not yet "
                "classified): %zu, of which %zu have NO FACE SET and %zu "
                "actually CARRY GEOMETRY -- these pass every gate\n",
                (unsigned long long)view->frame, auditDrawn, auditCoarse,
                auditNoFaces, auditFloating, auditFloatingNoFaces,
                auditFloatingDrawn);
    }
    // The dependency rule's own edges. It is the half that needs
    // objectIncomplete carried to a tier (SceneDump v55), so it is the
    // half that has to be observable ON that tier: the desktop prints
    // the counter with the level plan, and the browser has no plan.
    // It reports the whole tally, not the dependency count alone, and
    // fires on a change to any of it: `eligible` is what separates "the
    // rule refused to hold anything" from "nobody classified anything",
    // and a dependency counter that reads zero means opposite things in
    // those two worlds. Reading zero next to a healthy eligible count is
    // a verdict; reading it next to eligible zero is a broken wire.
    if (gatedByDependency != gatedDepSeen || gatedPoints != gatedPointsSeen
            || gatedLines != gatedLinesSeen
            || gateEligible != gateEligibleSeen) {
        const bool first = gatedDepSeen == kNeverReported;
        const size_t from = first ? 0 : gatedDepSeen;
        gatedDepSeen = gatedByDependency;
        gatedPointsSeen = gatedPoints;
        gatedLinesSeen = gatedLines;
        gateEligibleSeen = gateEligible;
        if (levelDebug())
            FC_RENDER_MSG(
                "render levels: element gates%s: %zu eligible, suppressed "
                "%zu point + %zu line (%zu by dependency, was %zu; %zu by "
                "coarse faces)\n",
                first ? " (first frame)" : "",
                gateEligible, gatedPoints, gatedLines, gatedByDependency,
                from, gatedByCoarse);
    }
    // Both edges of the load gate, with what it cost on the frame
    // it crossed. The closing edge matters as much as the opening
    // one: a gate that never lifts is the failure this design has
    // to rule out, and "loading OFF" arriving with the load is the
    // evidence that it does.
    if (loadDropElements != loadDropSeen) {
        loadDropSeen = loadDropElements;
        // Through the cross-tier macro: this sits outside the
        // desktop guard with the gate it reports, and the console
        // is Gui-only -- but on the desktop it must still reach
        // --log-file beside the plan readout the harnesses read.
        if (levelDebug())
            FC_RENDER_MSG(
                "render levels: load gate %s -- %zu of %zu drawables "
                "eligible, %zu point + %zu line draws suppressed "
                "this frame\n",
                loadDropElements ? "ON (a document is arriving)"
                                 : "OFF (loads finished)",
                gateEligible, scene.size(), gatedPoints, gatedLines);
    }
    // Every edge of the pressure latch, for the same reason -- and
    // this one needs its own line more than the load gate does. The
    // latch's state was readable only from the plan readout, and a
    // settled ladder STOPS PLANNING: the frames in which the release
    // walks back are exactly the frames that print nothing, so the
    // one question the staged design has to answer ("do the classes
    // come back?") was the one question the log could not. Measured
    // on the storm gate: 90s of settled run, not a single plan line.
    if (elemPressureStage != elemStageSeen) {
        const int from = elemStageSeen;
        elemStageSeen = elemPressureStage;
        if (levelDebug())
            FC_RENDER_MSG(
                "render levels: element gate stage %d -> %d (%s) -- "
                "%zu point + %zu line draws suppressed this frame\n",
                from, elemPressureStage,
                elemPressureStage > from ? "escalating: points, then lines"
                                         : "releasing: lines, then points",
                gatedPoints, gatedLines);
    }

    // ⭐ The per-instance id image (docs/RenderDebug.md §2.3b, view
    // mode 11 / RenderDebug_CullAudit): every scene draw rasterized
    // into the debug target as its own identity, with the cull mask
    // NOT consulted. That last part is the whole point — the image
    // has to be what the frame would have drawn had nothing been
    // skipped, or intersecting it with the mask proves nothing.
    //
    // Submitted here, before any pass reads `culled`, so the ground
    // truth is taken from the same draw list under the same camera
    // as the verdict it is about to be compared against.
    //
    // Two rounds: on-top draws render with the depth test off and
    // win their pixels in the beauty frame by arriving in a later
    // view. This pass has one view, so the order has to supply what
    // the view ids otherwise would.
    if (idPassRender) {
        for (int round = 0; round < 2; ++round) {
            for (size_t i = 0; i < scene.size(); ++i) {
                const auto &draw = scene[i];
                if (draw.material.ontop != (round == 1))
                    continue;
                // gatedForMemory belongs here with the other
                // display rules and NOT with the cull mask: the id
                // pass is ground truth for what the frame would
                // draw had nothing been *culled*, so a drawable the
                // display gate suppressed must be absent from it
                // too, or the audit compares a picture against a
                // scene the frame never had.
                if (isHidden(draw) || isDup(draw) || hideFill(draw)
                        || hidePoints(draw) || outlineOnly(draw)
                        || gatedForMemory(draw))
                    continue;
                view->submitId(draw, int(i), sceneNoSeam(draw));
            }
        }
        // Snapshot the verdict with the image it is an answer
        // about. Both change under the readback's latency, and
        // checking a picture against a mask that was walked again
        // in between would reintroduce, inside the instrument, the
        // exact one-frame skew it was built to find.
        if (idReadbackWanted) {
            idPixW = view->idReadW;
            idPixH = view->idReadH;
            idPixels.assign(size_t(idPixW) * size_t(idPixH) * 4, 0);
            idMask = sceneCulled;
            idOwner = cullOwner;
            // The node states go with the image too, for the same
            // reason the mask does. A verdict read back a second
            // later has been re-tested many times over; the one
            // that deleted these rows is the one standing now.
            idNodeAudit.clear();
            if (!cullOwner.empty()) {
                const size_t n = culler.hierarchy().nodes().size();
                idNodeAudit.reserve(n);
                for (size_t i = 0; i < n; ++i)
                    idNodeAudit.push_back(culler.nodeAudit(int(i)));
            }
            idKeys.clear();
            idKeys.reserve(scene.size());
            for (const auto &d : scene)
                idKeys.push_back(d.objectKey);

            // The tight-bound arms, on this frame and no other
            // (docs/FarFieldProxies.md §12.19). Here rather than
            // beside the cull for the same reason the mask snapshot
            // is here: the arms are answers about *this* image, and
            // the occluder buffer still holds the occluders that
            // produced it. Only the software pass owns a buffer that
            // can be re-asked at all.
            idTight = TightBoundAudit();
            idTightJudged.clear();
            idTightAabb.clear();
            idTightObb.clear();
            idTightTri.clear();
            if (debugconf.cullBounds && cullconf.enabled
                    && cullconf.software) {
                auditTightBounds(scene, sceneCulled, maskedCull.depth(),
                                 kTightPrimitiveCap,
                                 cullconf.softwareThreads,
                                 idTightJudged, idTightAabb, idTightObb,
                                 idTightTri, idTight);
            }
            idReadyFrame = view->readbackId(idPixels.data());
        }
    }

    // Which face-part set a whole-cache outline splits into
    // (GL: renderOutline ~1400). Clipped geometry and the
    // perFaceOutline mode (with a positive outline width, unless the
    // whole-scene silhouette runs) outline every face part; the
    // remaining perFaceOutline combinations outline only the
    // non-flat (curved) parts, whose silhouette edges are not in the
    // line set. Null = one whole-cache outline. A cache without a
    // face-part table outlines nothing in the per-part modes,
    // like GL's zero-iteration loop.
    auto outlineParts = [&](const Render::DrawCall &d, bool highlight)
            -> const std::vector<std::pair<int, int>> * {
        if (!d.mesh)
            return nullptr;
        if (d.material.numclipplanes > 0
                || (!highlight && hl.perFaceOutline && !hl.sceneOutline
                    && hl.outlineWidth > 0.0f))
            return &d.mesh->triangleParts;
        if (hl.perFaceOutline && !d.mesh->nonFlatParts.empty())
            return &d.mesh->nonFlatParts;
        return nullptr;
    };
    // Per-part outlines overlap their own object's fills (only the
    // part's interior is stencil-killed), so they skip the depth
    // write to let those fills dim them like GL's ordered draw does.
    auto submitOutlineOrParts = [&](const Render::DrawCall &d,
                                    BGFXView::OutlineSpec &spec,
                                    bool highlight) {
        if (const auto *parts = outlineParts(d, highlight)) {
            spec.depthWrite = false;
            for (const auto &part : *parts) {
                spec.start = part.first;
                spec.count = part.second;
                view->submitOutline(d, ++outlineRef, spec);
            }
        } else {
            view->submitOutline(d, ++outlineRef, spec);
        }
    };
    // Per-entry stencil outline of a whole-cache triangle draw of the
    // scene or a selection (GL: renderOutline with highlight=false,
    // reached from renderOpaque/renderTransparency right after the
    // fill). The whole-scene outline mode (sceneOutline without
    // perFaceOutline) suppresses these; its single-silhouette pass
    // runs at the end of the frame instead. viewOverride routes
    // on-top selection outlines into the highlight view where their
    // fills draw.
    auto submitSceneOutline = [&](const Render::DrawCall &d,
                                  int viewOverride = -1) {
        if (!hl.show || !d.material.outline || !isTriangle(d)
                || d.partIndex >= 0
                || (hl.sceneOutline && !hl.perFaceOutline))
            return;
        BGFXView::OutlineSpec spec;
        bool ontop = d.material.ontop
            || viewOverride == BGFXView::ViewHighlight;
        spec.view = viewOverride >= 0 ? uint16_t(viewOverride)
            : d.material.ontop ? BGFXView::ViewOnTop
                               : BGFXView::ViewOutline;
        spec.depthTest = !ontop;
        spec.depthWrite = spec.depthTest;
        spec.caps = hl.hideVertex;
        spec.color = d.material.linecolor ? d.material.linecolor
                                          : d.material.diffuse;
        // GL renderOutline ~1458: raise the line width to the
        // configured outline width, then thicken.
        float lw = qMax(d.material.linewidth, hl.outlineWidth);
        spec.width = qMax(lw * 1.5f,
                          d.material.linewidth * hl.outlineThicken);
        submitOutlineOrParts(d, spec, false);
    };
    // Whole-object outline of the preselection highlight in
    // hidden-line mode (GL: renderOutline with highlight=true and
    // partidx < 0): drawn on top with the selection-thickened width
    // the bridge resolved into Material::outlinewidth, raised to the
    // configured outline width like GL's in-place formula.
    auto submitHighlightOutline = [&](const Render::DrawCall &d) {
        if (!hl.show || !d.material.outline || !isTriangle(d)
                || d.partIndex >= 0)
            return;
        BGFXView::OutlineSpec spec;
        spec.view = BGFXView::ViewHighlight;
        spec.depthTest = false;
        spec.depthWrite = false;
        spec.caps = true;
        spec.color = d.material.linecolor ? d.material.linecolor
                                          : d.material.diffuse;
        spec.width = qMax(d.material.outlinewidth,
                          hl.outlineWidth * 1.5f);
        submitOutlineOrParts(d, spec, true);
    };

    // Cross-object instancing: each precomputed group of identical
    // draws (matching geometry content and material bar the diffuse)
    // collapses into one instanced submit of the opaque — or, on
    // WBOIT frames, transparent — pass, carrying {model matrix,
    // diffuse} per instance. The
    // depth-only side submissions batch too, over the same instance
    // layout: the SSAO/volumetric prepass with the visible members,
    // the shadow caster pass with visible, frustum-culled AND
    // selection-hidden members (a hidden scene draw still casts —
    // its geometry re-renders in the on-top pass). Hidden-line
    // frames stay
    // per-draw (the stencil outline pass reworks the fill submits
    // wholesale).
    static const bool noInstancing =
        getenv("FC_BGFX_NO_INSTANCING") != nullptr;
    std::vector<uint8_t> drawInstanced;
    std::vector<uint8_t> prepassInstanced;
    std::vector<uint8_t> casterInstanced;
    // ⭐ What the instancing actually collapsed, counted rather than
    // assumed (docs/DrawSubmission.md phase 0.5). Nothing reported
    // this before, so "the scene has repeated geometry" and "the
    // renderer is batching it" were the same belief with no number
    // between them — and the draw count is what every submission
    // decision below is scoped against.
    instStats = InstancingStats();
    instStats.groups = uint32_t(instGroups.size());
    instStats.eligible = uint32_t(scene.size());
    if (noInstancing)
        instStats.why = "env FC_BGFX_NO_INSTANCING";
    else if (hl.show)
        instStats.why = "hidden-line frame";
    else if (instGroups.empty())
        instStats.why = "no group has two members";
    else if (!view->instancingActive())
        instStats.why = "backend reports no instancing";
    if (!noInstancing && !hl.show && !instGroups.empty()
            && view->instancingActive()) {
        drawInstanced.assign(scene.size(), 0);
        prepassInstanced.assign(scene.size(), 0);
        casterInstanced.assign(scene.size(), 0);
        std::vector<float> instData;
        std::vector<int> vis, visOut, hidden, styledOut;
        auto appendInstance = [&](int i) {
            const auto &d = scene[i];
            if (d.identity) {
                static const float ident[16] = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};
                instData.insert(instData.end(), ident, ident + 16);
            } else {
                instData.insert(instData.end(), d.model, d.model + 16);
            }
            float c[4];
            unpackColor(d.material.diffuse, c);
            instData.insert(instData.end(), c, c + 4);
        };
        for (const auto &group : instGroups) {
            if (group.members.size() < 2) {
                ++instStats.groupsSingleton;
                continue;
            }
            ++instStats.groupsUsable;
            instStats.membersUsable += uint32_t(group.members.size());
            vis.clear();
            visOut.clear();
            hidden.clear();
            styledOut.clear();
            for (int i : group.members) {
                if (isHidden(scene[i]))
                    hidden.push_back(i);
                else if (culled(scene[i]))
                    visOut.push_back(i);
                else if (!view->styleAdmits(scene[i]))
                    // A group merges draws by geometry and material,
                    // not by objectKey, so its members can resolve to
                    // different display styles (docs/CoinRetirement.md
                    // 5.8, 5.9) -- one box overridden Shaded beside an
                    // identical one left in Wireframe. A member the
                    // style drops must not ride the group's submit; it
                    // falls to the per-draw loop, whose submit filters
                    // it the same way. It still CASTS, like the
                    // per-draw path, whose caster submit is not style
                    // filtered.
                    styledOut.push_back(i);
                else
                    vis.push_back(i);
            }
            instData.clear();
            instData.reserve(group.members.size() * 20);
            for (int i : vis)
                appendInstance(i);
            if (vis.size() >= 2) {
                const auto &proto = scene[vis[0]];
                if (view->submitInstanced(proto, instData.data(),
                                          uint32_t(vis.size()))) {
                    for (int i : vis)
                        drawInstanced[i] = 1;
                    ++instStats.submits;
                    instStats.drawsReplaced += uint32_t(vis.size());
                }
                else {
                    // ⚠️ Counted separately because it is silent: the
                    // group was formed and then refused at submit
                    // (no program, no transient instance space, or a
                    // transparent group on a sorted-transparency
                    // frame) and every member fell back to a draw of
                    // its own. A group that forms is not a group
                    // that batches.
                    ++instStats.refused;
                    instStats.refusedMembers += uint32_t(vis.size());
                }
                // Transparent geometry neither occludes nor receives
                // AO — it stays out of the prepass like the per-draw
                // path.
                if (prepassRender && !isTransp(proto)
                        && view->submitPrepassInstanced(
                            proto, instData.data(),
                            uint32_t(vis.size()))) {
                    for (int i : vis)
                        prepassInstanced[i] = 1;
                }
            }
            else {
                // The group exists but this frame's culling and
                // visibility left it with fewer than two members on
                // screen. Not a defect — but it is where a scene
                // full of repeated parts can still submit per draw.
                instStats.thinnedMembers += uint32_t(vis.size());
            }
            // Casters append the frustum-culled and hidden members
            // after the visible ones — off-screen geometry still
            // casts, and the instance order does not matter for a
            // depth pass. Group members share the material bar the
            // diffuse, so one shadowstyle check covers them all.
            if (shadowRender
                    && (scene[group.members[0]].material.shadowstyle
                        & 1)) {
                for (int i : visOut)
                    appendInstance(i);
                for (int i : hidden)
                    appendInstance(i);
                // Style-dropped members cast like the per-draw path's.
                for (int i : styledOut)
                    appendInstance(i);
                uint32_t total = uint32_t(vis.size() + visOut.size()
                                          + hidden.size()
                                          + styledOut.size());
                if (total >= 2
                        && view->submitShadowCasterInstanced(
                            scene[group.members[0]], instData.data(),
                            total)) {
                    for (int i : group.members)
                        casterInstanced[i] = 1;
                }
            }
        }
    }
    auto instancedThisFrame = [&](int i) {
        return !drawInstanced.empty() && drawInstanced[i];
    };
    auto prepassInstancedThisFrame = [&](int i) {
        return !prepassInstanced.empty() && prepassInstanced[i];
    };
    auto casterInstancedThisFrame = [&](int i) {
        return !casterInstanced.empty() && casterInstanced[i];
    };

    // 1. Normal scene draws, then on-top triangle fills (opaque before
    // transparent), mirroring the GL delayed-pass order. On-top lines
    // are deferred below so they draw over the selection fills.
    // Hidden-line entries get their stencil outline right after the
    // fill and honor the face/seam/vertex hiding rules.
    view->ontop = false;
    // How far each object's decoration reaches from its own geometry
    // (Private::buildDecorReach, resolved once per setScene). The view
    // gets a copy rather than deriving its own: buildInstanceGroups
    // keys on these values, and a batch whose members disagreed with
    // the reach its prototype binds is exactly the bug the key closes.
    view->decorReach = decorReach;
    // Glass on screen: scene lines and points move to ViewGlassLine so
    // the refraction cannot magnify them (BGFXView::ViewGlassLine).
    // Set before the first submit and left set: submit() makes the
    // routing decision last, by which point the overlay, reflection and
    // on-top draws have already claimed views of their own, so this
    // never has to be turned back off for them.
    view->glassLines = glassActive;
    // Whether a line/point draw of the scene wants the extra dimmed
    // pass behind the glass. Only worth submitting when there is glass
    // to be behind; the pass is stencil-gated to the glass surface, so
    // without one it would draw nothing at full cost.
    auto glassDim = [&](const Render::DrawCall &d) {
        return glassActive && !isTriangle(d) && !d.material.ontop;
    };
    // The per-draw C++ the plan calls "submit": this walks every row
    // of the draw list, culled or not, and decides per row what to
    // submit. Braced so the scope covers the loop and nothing after.
    {
    if (debugconf.frameTiming)
        cpuPhaseMs[CpuPreSubmit] += 1000.0
            * double(bx::getHPCounter() - renderInnerT0)
            / double(bx::getHPFrequency());
    CpuScope submitTiming(this, CpuSubmitLoop);
    for (int drawIdx = 0; drawIdx < int(scene.size()); ++drawIdx) {
        const auto &draw = scene[drawIdx];
        if (draw.material.ontop || isHidden(draw)) {
            // A selection-hidden scene draw still casts its shadow —
            // the same geometry re-renders in the on-top pass, and
            // GL's SoShadowGroup shadow map render is oblivious to
            // the selection re-render. This also keeps the cached
            // shadow map valid across select/deselect.
            if (shadowRender && !draw.material.ontop
                    && isTriangle(draw)
                    && (draw.material.shadowstyle & 1)
                    && !mediumExempt(draw.material)
                    && !casterInstancedThisFrame(drawIdx))
                view->submitShadowCaster(draw);
            // Bulb shadow tiles re-rendering this frame take every
            // caster individually (no instanced caster path there).
            if (anyBulbShadow && !draw.material.ontop
                    && isTriangle(draw)
                    && (draw.material.shadowstyle & 1)
                    && !mediumExempt(draw.material)) {
                for (int t = 0; t < BGFXView::kBulbShadowTiles; ++t)
                    if (bulbShadowRender[t])
                        view->submitBulbShadowCaster(draw, t);
            }
            if (shadowRender && !draw.material.ontop
                    && isTriangle(draw)
                    && (draw.material.shadowstyle & 1)
                    && glassActive && draw.material.glass)
                view->submitShadowTint(draw);
            // Likewise the depth+normal prepass and the debug scene
            // re-render: the replacing draw (e.g. a shader override,
            // docs/RenderDebug.md §6.5) only substitutes the beauty
            // fill — depth, AO, volumetrics and the debug modes must
            // keep seeing the geometry.
            if (!draw.material.ontop && isTriangle(draw)
                    && !isTransp(draw)
                    && !mediumExempt(draw.material)) {
                if (prepassRender && !culled(draw)
                        && !prepassInstancedThisFrame(drawIdx))
                    view->submitPrepass(draw);
                if (debugSceneRender && !culled(draw))
                    view->submitDebugScene(draw, debugconf.viewMode);
                // The external base layer's depth too: a selected
                // (hidden, re-drawn as its selection) object still
                // occludes the lines behind it.
                if (externalBase && !culled(draw))
                    view->submit(draw, viewMat, BGFXView::PassDepthOnly);
            }
            continue;
        }
        if (hideFill(draw) || hidePoints(draw) || gatedForMemory(draw))
            continue;
        // A frustum-culled draw skips its color/water/prepass
        // submits but still casts its shadow below.
        bool cullDraw = culled(draw);
        // Water surface pass: the water body's triangles leave the
        // ordinary (transparent-bucket) path and re-render in the
        // dedicated surface view; clipped bodies keep the normal
        // path (no clip variant of the surface shader). The body's
        // edge/vertex draws are dropped entirely — a water surface
        // has no CAD feature lines, and the black edges would also
        // smear through the screen-space refraction.
        bool surfWater = waterSurfActive && isTriangle(draw)
            && draw.material.water
            && draw.material.numclipplanes == 0;
        bool surfWaterLine = waterSurfActive && !isTriangle(draw)
            && draw.objectKey
            && waterSurfObjects.count(draw.objectKey);
        // Glass pass: the body's triangles re-render as glass
        // (their front/back depths bound the absorption interval);
        // clipped bodies keep the normal path like water. The
        // edge/vertex draws keep rendering — a glass part keeps
        // its CAD feature lines.
        bool surfGlass = glassActive && isTriangle(draw)
            && draw.material.glass
            && draw.material.numclipplanes == 0;
        // Cloud body: neither the fills nor the feature lines
        // render — the volume raymarches as a medium instead. The
        // triangles rasterize the interval depth targets only.
        bool cloudFill = cloudActive && isTriangle(draw)
            && (draw.material.cloud || draw.material.fountain);
        bool cloudPart = cloudActive
            && (cloudFill
                || (!isTriangle(draw) && draw.objectKey
                    && cloudObjects.count(draw.objectKey)));
        // Fire body: like the cloud, only the interval depth
        // targets see the triangles.
        bool fireFill = fireActive && isTriangle(draw)
            && draw.material.fire;
        bool firePart = fireActive
            && (fireFill
                || (!isTriangle(draw) && draw.objectKey
                    && fireObjects.count(draw.objectKey)));
        if (!cullDraw && !instancedThisFrame(drawIdx) && !surfWater
                && !surfWaterLine && !surfGlass && !cloudPart
                && !firePart) {
            view->submit(draw, viewMat, BGFXView::PassNormal,
                         sceneNoSeam(draw));
            // ...and, where the glass hides it, into the distance
            // field the glass pass resamples. That is what makes an
            // edge behind the body warp with the face it lies on
            // instead of sitting undistorted over it; the undistorted
            // pass this replaces was correct in width and wrong in
            // place, which read worse than the magnification it fixed.
            if (glassDim(draw))
                view->submitLineSdf(draw, viewMat, sceneNoSeam(draw));
        }
        if (cloudFill && !cullDraw && mediumRender) {
            int slot = slotOf(cloudSlots, draw.objectKey);
            view->submitWaterDepth(draw, false, 2, slot);
            view->submitWaterDepth(draw, true, 2, slot);
        }
        if (fireFill && !cullDraw && mediumRender) {
            int slot = slotOf(fireSlots, draw.objectKey);
            view->submitWaterDepth(draw, false, 3, slot);
            view->submitWaterDepth(draw, true, 3, slot);
        }
        if (surfWater && !cullDraw)
            view->submitWaterSurface(draw, waterWaveStrength,
                                     waterWaveScale, waterSurfTime,
                                     waterSurfReject, waterReflMode,
                                     waterconf.refraction, waterActive,
                                     waterconf.absorption,
                                     waterconf.inscatter,
                                     waterconf.shadow,
                                     waterconf.shadowWobble,
                                     waterconf.rippleType,
                                     waterconf.rippleDensity,
                                     waterconf.impactStrength,
                                     waterconf.impactLife,
                                     volActive && cloudActive
                                         ? fountainSplash : nullptr,
                                     volActive && waterActive);
        if (surfGlass && !cullDraw) {
            // The interval depths cache with the medium targets; the
            // surface pass reads the per-frame scene copy, so it
            // always re-renders.
            if (mediumRender) {
                view->submitWaterDepth(draw, false, 1);
                view->submitWaterDepth(draw, true, 1);
            }
            view->submitGlassSurface(draw, glassReject);
        }
        // Water body draws bound the medium instead of acting as
        // ordinary surfaces: their front/back depths rasterize into
        // the water targets (whatever their transparency), and they
        // are excluded from the volumetric ray ends and from shadow
        // casting so light and shafts enter the water.
        bool isWater = waterActive && isTriangle(draw)
            && draw.material.water;
        if (isWater && !cullDraw && mediumRender) {
            int slot = slotOf(waterSlots, draw.objectKey);
            view->submitWaterDepth(draw, false, 0, slot);
            view->submitWaterDepth(draw, true, 0, slot);
        }
        // The SSAO prepass re-rasterizes the opaque fills into the
        // depth+normal target (transparent geometry neither occludes
        // nor receives AO; the multiply pass runs before the
        // transparent bucket). The volumetric raymarch shares it as
        // its ray-end depth source.
        if (prepassRender && isTriangle(draw) && !isTransp(draw)
                && !mediumExempt(draw.material) && !cullDraw
                && !prepassInstancedThisFrame(drawIdx))
            view->submitPrepass(draw);
        // Debug scene re-render (modes 6/8): every triangle fill
        // that rasterizes in the main color passes — opaque,
        // transparent and the water/glass surface re-renders alike;
        // cloud/fire bodies raymarch instead of rasterizing, so
        // they stay out. Instanced members count per-draw (same
        // fragments either way).
        if (debugSceneRender && isTriangle(draw) && !cullDraw
                && !cloudFill && !fireFill)
            view->submitDebugScene(draw, debugconf.viewMode);
        // Shadow casters — transparent geometry casts like an opaque
        // one, matching Coin's SoShadowGroup (its depth-map pass
        // ignores alpha); water is the one exception (light must
        // enter the medium). Skipped while the cached map is valid.
        if (shadowRender && isTriangle(draw)
                && (draw.material.shadowstyle & 1)
                && !mediumExempt(draw.material)
                && !casterInstancedThisFrame(drawIdx))
            view->submitShadowCaster(draw);
        // Bulb shadow tiles re-rendering this frame take every
        // caster individually (no instanced caster path there).
        if (anyBulbShadow && isTriangle(draw)
                && (draw.material.shadowstyle & 1)
                && !mediumExempt(draw.material)) {
            for (int t = 0; t < BGFXView::kBulbShadowTiles; ++t)
                if (bulbShadowRender[t])
                    view->submitBulbShadowCaster(draw, t);
        }
        // Glass casts through the tint map instead of the moments:
        // a softer shadow, tinted when the glass is colored.
        if (shadowRender && isTriangle(draw)
                && (draw.material.shadowstyle & 1)
                && glassActive && draw.material.glass)
            view->submitShadowTint(draw);
        if (!cullDraw)
            submitSceneOutline(draw);
    }
    }  // CpuSubmitLoop
    // Start of the post-submit checkpoint chain.
    if (debugconf.frameTiming)
        cpuMarkT = bx::getHPCounter();
    if (view->passLive(V::ViewShadowBlurH))
        view->submitShadowBlur(lightconf.smoothBorder);
    // The ground receiver. Its historical reason is the shadow -- the
    // Shadow draw style draws the plane its scene casts onto, which is
    // why RenderShadow_ShowGround alone still draws nothing -- and its
    // second is the reflection, which needs the quad whether or not a
    // shadow map exists. groundQuad() decides whether there is a quad at
    // all (either switch, and not fully transparent).
    if ((shadowActive || lightconf.groundReflection) && groundQuadOk) {
        view->submitShadowGround(bboxMin, bboxMax, lightconf, groundCam,
                                 volActive && aoRender);
    }
    // Ground reflection: the opaque scene triangles re-submit into
    // the mirrored view through the ordinary submit path (redirected
    // by reflPass, culling flipped, shadow matrix rebased), then the
    // overlay quad blends the result onto the ground. Frustum
    // culling is skipped — the mirrored camera sees a different
    // volume; hidden and on-top draws stay out like the water
    // bodies and transparent geometry (single-bounce opaque only).
    // Marshalled per-slot medium parameters: consumed by the
    // reflection media pass right below and by the volumetric
    // raymarch submit later in the frame.
    float cloudParams[kSlots][4] = {};
    float fireParams[kSlots][4] = {};
    float fireParams2[kSlots][4] = {};
    float fireFrames[kSlots][16] = {};
    if (volActive) {
        for (int s = 0; s < kSlots; ++s) {
            if (cloudActive && cloudSlot[s][3] > 0.0f) {
                cloudParams[s][0] = cloudSlot[s][0];
                cloudParams[s][1] = cloudSlot[s][1];
                cloudParams[s][2] = animTime * cloudSlot[s][2];
                // w keeps the flavor: 1 = cloud FBM, 2 = fountain.
                cloudParams[s][3] = cloudSlot[s][3];
                animatedFrame = animatedFrame
                    || (animLive && cloudSlot[s][2] != 0.0f);
            }
            const FireSlot &fsl = fireSlot[s];
            if (fireActive && fsl.valid) {
                // The 2.0 rise rate makes the flame climb a couple
                // of noise cells per second at the default speed.
                fireParams[s][0] = fsl.emission;
                fireParams[s][1] = fsl.detail;
                fireParams[s][2] = animTime * fsl.speed * 2.0f;
                fireParams[s][3] = 1.0f;
                fireParams2[s][0] = fsl.invRadius;
                fireParams2[s][1] = fsl.invHeight;
                fireParams2[s][2] = fsl.soot;
                animatedFrame = animatedFrame
                    || (animLive && fsl.speed != 0.0f);
            }
            std::memcpy(fireFrames[s], fsl.frame,
                        sizeof(fsl.frame));
        }
    }
    if (view->passLive(V::ViewGroundRefl) && reflRender) {
        view->reflPass = true;
        // Keep only what is above the mirror plane; everything
        // under it would otherwise fold up over the reflection.
        view->reflClip = true;
        view->reflClipPlane[0] = 0.0f;
        view->reflClipPlane[1] = 0.0f;
        view->reflClipPlane[2] = 1.0f;
        view->reflClipPlane[3] = -(groundReflActive ? bboxMin[2]
                                                    : waterPlaneZ);
        float savedShadowMtx[16];
        std::memcpy(savedShadowMtx, view->shadowMtx,
                    sizeof(savedShadowMtx));
        std::memcpy(view->shadowMtx,
                    groundReflActive ? reflShadowMtx : waterReflShadowMtx,
                    sizeof(savedShadowMtx));
        for (const auto &draw : scene) {
            const auto &mat = draw.material;
            if (!isTriangle(draw) || mat.ontop || isHidden(draw)
                    || hideFill(draw) || isTransp(draw)
                    || mediumExempt(mat))
                continue;
            view->submit(draw, viewMat);
        }
        // Non-on-top whole-object selection fills replace hidden
        // scene draws (shader overrides, §6.5) — mirror them into
        // the reflection so the object doesn't vanish from it.
        for (const auto &sel : selections) {
            if (sel.first > 0)
                continue;
            for (const auto &draw : sel.second) {
                if (!isTriangle(draw) || draw.partIndex >= 0
                        || !draw.wholeObject || isTransp(draw)
                        || hideFill(draw)
                        || mediumExempt(draw.material)
                        || isDup(draw))
                    continue;
                view->submit(draw, viewMat);
            }
        }
        std::memcpy(view->shadowMtx, savedShadowMtx,
                    sizeof(savedShadowMtx));
        view->reflPass = false;
        view->reflClip = false;
        // The reflection shows the fountain plume / flame too: an
        // analytic media march composited over the mirrored scene.
        if (volActive && (cloudActive || fireActive))
            view->submitReflMedia(cloudParams, fireParams,
                                  fireParams2, fireFrames,
                                  fountainGeom, fountainFrame);
    }
    // Curvature darkening lands on the finished opaque scene, before
    // the outlines and the transparent bucket draw over it.
    if (view->passLive(V::ViewCavity))
        view->submitCavity(cavityconf.valley, cavityconf.ridge,
                           cavityconf.radius);
    // Ground blends its (possibly cached) reflection with a quad
    // every frame; the water surface pass samples reflTex itself
    // (s_texRefl) below.
    if (view->passLive(V::ViewGroundReflApply))
        view->submitGroundReflOverlay(bboxMin, bboxMax, lightconf,
                                      groundCam);
    for (const auto &draw : scene) {
        if (draw.material.ontop && isTriangle(draw) && !isTransp(draw)
                && !isHidden(draw) && !hideFill(draw)) {
            bool cullDraw = culled(draw);
            if (!cullDraw)
                view->submit(draw, viewMat);
            // On-top geometry keeps casting its shadow (the view
            // order still lands these in the caster pass).
            if (shadowRender && (draw.material.shadowstyle & 1)
                    && !mediumExempt(draw.material))
                view->submitShadowCaster(draw);
            if (!cullDraw)
                submitSceneOutline(draw);
        }
    }
    for (const auto &draw : scene) {
        if (draw.material.ontop && isTriangle(draw) && isTransp(draw)
                && !isHidden(draw) && !hideFill(draw)) {
            bool cullDraw = culled(draw);
            if (!cullDraw)
                view->submit(draw, viewMat);
            if (shadowRender && (draw.material.shadowstyle & 1)
                    && !mediumExempt(draw.material))
                view->submitShadowCaster(draw);
            if (!cullDraw)
                submitSceneOutline(draw);
        }
    }

    // 1b. Stencil section caps of clipped solids, in their own
    // sequential views (opaque caps between the opaque and outline
    // passes, transparent caps after the OIT composite — GL's
    // grouped section pass order).
    view->updateHatchTexture(hatchVersion,
                             hatchTex ? hatchTex->pixels.data()
                                      : nullptr,
                             hatchTex ? hatchTex->width : 0,
                             hatchTex ? hatchTex->height : 0);
    submitSectionCaps(view, reinterpret_cast<const float *>(projMatrix));
    cpuMark(CpuPostCaps);

    // 1c. SSAO resolve: generate and blur the AO (the gen/blur
    // views run before ViewOpaque, whose mesh draws sample the
    // result into their ambient terms — see aoMeshTex). A cached
    // frame (aoRender false) skips the chain outright: the targets
    // still hold this camera/scene's result.
    if (view->passLive(V::ViewAOGen))
        view->submitAOResolve(aoRadius, aoconf.intensity,
                              aoconf.method, aoconf.fast,
                              aoconf.slices, aoconf.steps,
                              accumSampleIndex);

    // 1d. Volumetric light shafts: half-res raymarch of the shadow
    // map, bilateral-upsampled and composited onto the opaque scene
    // after the outlines, before the transparent bucket.
    if (view->passLive(V::ViewVolGen)) {
        // Temporal accumulation factor: while the camera holds
        // still, successive jittered marches blend into the
        // history (k = 1/frames, floored so animated media keep
        // ~1/16 of fresh signal per frame); any camera or scene
        // change replaces the history outright — no reprojection,
        // no ghosting, interaction just returns to single-frame
        // noise.
        if (!staticFrame)
            view->volAccumFrames = 0;
        else if (view->volAccumFrames < 1024)
            ++view->volAccumFrames;
        // The freeze-frame determinism switch (docs/RenderDebug.md)
        // replaces the history outright every frame: k = 1 also
        // zeroes the golden-ratio jitter phase (accum < 1 gates it),
        // so a repeat frame raymarches identically.
        float volAccum = debugconf.freezeFrame ? 1.0f
            : view->volAccumFrames == 0
            ? 1.0f
            : std::max(1.0f / float(view->volAccumFrames + 1),
                       1.0f / 16.0f);
        view->submitVolumetric(volDensity, volconf.intensity,
                               volMaxDist, volMedium,
                               waterActive, waterSurfActive,
                               volAccum,
                               waterSigma, cloudParams,
                               fireParams, fireParams2, fireFrames,
                               fountainGeom, fountainFrame);
    }

    // 1e. Water caustics: additive light-space pattern splat over
    // the prepass surfaces inside the water interval, before the
    // extinction multiply of the volumetric apply.
    if (view->passLive(V::ViewCaustics)) {
        float causticParams[kSlots][4] = {};
        bool anyCaustics = false;
        for (int s = 0; s < waterSlotCount; ++s) {
            float scale = volconf.causticsScale;
            if (scale <= 0.0f && waterDiagSlot[s] > 0.0f)
                scale = 6.0f / waterDiagSlot[s];
            if (scale <= 0.0f)
                continue;
            causticParams[s][0] = volconf.causticsIntensity;
            causticParams[s][1] = scale;
            causticParams[s][2] = animTime * volconf.causticsSpeed;
            anyCaustics = true;
        }
        if (anyCaustics) {
            view->submitCaustics(causticParams, waterSigma);
            animatedFrame = animatedFrame
                || (animLive && volconf.causticsSpeed != 0.0f);
        }
    }

    // 1f. Water surface / glass: copy the scene color (post
    // volumetric composite) into the refraction source; the
    // per-draw surface submits happened in the scene loop above
    // (their views render after the copy).
    if (view->passLive(V::ViewWaterCopy))
        view->submitWaterCopy();
    if (waterSurfActive) {
        animatedFrame = animatedFrame
            || (animLive && waterconf.waveSpeed != 0.0f);
    }

    // 1g. Bloom: bright-pass + light-source emit + blur + additive
    // composite, over the finished scene (its views sit after the
    // transparent/water buckets, before the on-top/UI passes).
    if (view->passLive(V::ViewBloomBright))
        view->submitBloom(bloomconf.threshold, bloomconf.intensity,
                          bloomconf.radius, bulbDraws,
                          // Rendered this frame, or the AO cache
                          // matched — either way the prepass
                          // targets describe the current camera.
                          prepassActive);

    // 1g'. User post-stage shader (docs/RenderDebug.md §6): copy the
    // composited color, then the user program draws fullscreen over
    // the scene reading the copy — before the debug visualization
    // and the on-top/highlight/overlay passes.
    if (view->passLive(V::ViewUserPost))
        view->submitUserPost(*userPost, userPostProg);

    // 1h. Render debugging buffer visualization (docs/RenderDebug.md):
    // overwrite the scene color with the selected intermediate target.
    // Depth (mode 1) normalizes by the farthest scene-bbox corner in
    // view space so the whole model spans the visible ramp.
    cpuMark(CpuPostEffects);
    if (debugconf.viewMode > 0) {
        float maxDepth = 0.0f;
        if (bboxValid) {
            for (int c = 0; c < 8; ++c) {
                float x = (c & 1) ? bboxMax[0] : bboxMin[0];
                float y = (c & 2) ? bboxMax[1] : bboxMin[1];
                float z = (c & 4) ? bboxMax[2] : bboxMin[2];
                float viewZ = viewMat[2] * x + viewMat[6] * y
                    + viewMat[10] * z + viewMat[14];
                maxDepth = std::max(maxDepth, -viewZ);
            }
        }
        view->submitDebug(debugconf, maxDepth, aoconf.method,
                          shadowActive && bgfx::isValid(view->shadowTex),
                          waterconf.impactLife);
    }

    // 2. Selection whole-object fills; positive ids are on-top
    // selections (SoFCRenderer::addSelection). Their lines/points are
    // deferred to the two-pass loop when it runs; single-part (e.g.
    // selected face) triangle draws come last of all.
    for (const auto &sel : selections) {
        view->ontop = sel.first > 0;
        view->selPass = sel.first <= 0;
        for (const auto &draw : sel.second) {
            if (isTriangle(draw) && draw.partIndex >= 0)
                continue;
            if (twoPass && sel.first > 0 && !isTriangle(draw))
                continue;
            if (isDup(draw))
                continue;
            // Over an external base layer a depth-tested selection
            // fill dims where the scene hides it rather than vanish:
            // the depth-off dimmed pass first, the tested fill over
            // it (Blender's alpha_occlu, docs/CyclesIntegration.md
            // sec 5.3).
            if (externalBase && sel.first <= 0 && isTriangle(draw))
                view->submit(draw, viewMat, BGFXView::PassLineHidden);
            view->submit(draw, viewMat);
            // A non-on-top selection's lines follow the scene's into
            // ViewGlassLine, so they need the field too or a selected
            // edge behind glass would vanish where it used to show
            // through it.
            if (sel.first <= 0 && glassDim(draw))
                view->submitLineSdf(draw, viewMat, false);
            // Hidden-line outline of a whole-object selection fill
            // (GL: renderOutline from renderOpaque/renderTransparency
            // over slentries). On-top selections outline in the
            // highlight view where their fills draw.
            if (isTriangle(draw))
                submitSceneOutline(draw, sel.first > 0
                        ? int(BGFXView::ViewHighlight) : -1);
        }
    }
    view->selPass = false;

    // 3. Whole-object preselection fills before the depth prepass.
    view->ontop = true;
    if (hlWholeOnTop) {
        for (const auto &draw : highlight) {
            if (isTriangle(draw) && !outlineOnly(draw)) {
                view->submit(draw, viewMat);
                submitHighlightOutline(draw);
            }
        }
    }

    if (twoPass) {
        // 4. Depth-write-only prepass of on-top fills: on-top draws
        // render without depth test and thus never write depth, so the
        // solid line pass below needs this to tell hidden from visible.
        if (sceneTwoPass) {
            for (const auto &draw : scene) {
                if (draw.material.ontop && isTriangle(draw)
                        && !isHidden(draw) && !hideFill(draw)
                        && !culled(draw))
                    view->submit(draw, viewMat, BGFXView::PassDepthOnly);
            }
        }
        if (selOnTopLine) {
            for (const auto &sel : selections) {
                if (sel.first <= 0)
                    continue;
                for (const auto &draw : sel.second) {
                    if (isTriangle(draw) && draw.partIndex < 0
                            && !isDup(draw))
                        view->submit(draw, viewMat,
                                     BGFXView::PassDepthOnly);
                }
            }
        }
        if (hlWholeOnTop) {
            for (const auto &draw : highlight) {
                if (isTriangle(draw))
                    view->submit(draw, viewMat, BGFXView::PassDepthOnly);
            }
        }

        // 5. On-top lines/points, dimmed where depth-occluded then
        // solid where visible (GL's RenderPassLinePattern/LineSolid).
        for (int pass : {int(BGFXView::PassLineHidden),
                         int(BGFXView::PassLineSolid)}) {
            for (const auto &draw : scene) {
                if (draw.material.ontop && !isTriangle(draw)
                        && !isHidden(draw) && !hidePoints(draw)
                        && !culled(draw))
                    view->submit(draw, viewMat, pass,
                                 sceneNoSeam(draw));
            }
            // GL bucket order within each pass: the uncolored
            // whole-on-top companions (selsontop) draw before the
            // colored highlight lines (selslineontop) — the
            // highlight must paint last or a companion coincident
            // with it (a selected sketch edge over its own object
            // lines) covers it back with white.
            for (int hlphase = 0; hlphase < 2; ++hlphase) {
                for (const auto &sel : selections) {
                    if (sel.first <= 0)
                        continue;
                    for (const auto &draw : sel.second) {
                        if (!isTriangle(draw) && !isDup(draw)
                                && draw.material.highlightline
                                    == (hlphase == 1))
                            view->submit(draw, viewMat, pass);
                    }
                }
            }
            if (hlWholeOnTop) {
                for (const auto &draw : highlight) {
                    if (!isTriangle(draw) && draw.partIndex < 0)
                        view->submit(draw, viewMat, pass);
                }
            }
        }
    } else {
        // No fills on top: scene on-top lines draw in a single pass.
        for (const auto &draw : scene) {
            if (draw.material.ontop && !isTriangle(draw)
                    && !isHidden(draw) && !hidePoints(draw)
                    && !culled(draw))
                view->submit(draw, viewMat, BGFXView::PassNormal,
                             sceneNoSeam(draw));
        }
    }

    // 6. Single-part selection fills (e.g. the selected face), matching
    // GL's transpselectionsfaceontop position after the line passes.
    for (const auto &sel : selections) {
        view->ontop = sel.first > 0;
        view->selPass = sel.first <= 0;
        for (const auto &draw : sel.second) {
            if (isTriangle(draw) && draw.partIndex >= 0
                    && !outlineOnly(draw)) {
                if (externalBase && sel.first <= 0)
                    view->submit(draw, viewMat, BGFXView::PassLineHidden);
                view->submit(draw, viewMat);
            }
        }
    }
    view->selPass = false;

    // 7. Preselection highlight: whole-on-top fills/lines were handled
    // above, only its single-part lines/points remain; otherwise draw
    // everything here, fills first.
    view->ontop = true;
    if (hlWholeOnTop) {
        for (const auto &draw : highlight) {
            if (!isTriangle(draw) && draw.partIndex >= 0)
                view->submit(draw, viewMat);
        }
    } else {
        for (const auto &draw : highlight) {
            if (isTriangle(draw) && !outlineOnly(draw)) {
                view->submit(draw, viewMat);
                submitHighlightOutline(draw);
            }
        }
        for (const auto &draw : highlight) {
            if (!isTriangle(draw))
                view->submit(draw, viewMat);
        }
    }

    // 8. Whole-scene hidden-line silhouette (GL: renderSceneOutline,
    // issued after all line/highlight passes and before the face
    // outlines): every scene triangle draw stencil-marks under one
    // shared reference — depth-independent, so hidden geometry still
    // counts — then each one's edges redraw where the stencil
    // differs, leaving a single outline around the union of the
    // scene. Deviation from GL: the edge passes apply each entry's
    // own clip planes, where GL leaves whatever clip state its mark
    // loop applied last.
    if (hl.show && hl.sceneOutline) {
        uint32_t silhouetteRef = ++outlineRef;
        bool marked = false;
        for (const auto &draw : scene) {
            if (!isTriangle(draw) || isHidden(draw))
                continue;
            marked |= view->submitOutlineMark(draw, silhouetteRef,
                    BGFXView::ViewHighlight, true);
        }
        if (marked) {
            BGFXView::OutlineSpec spec;
            spec.view = BGFXView::ViewHighlight;
            spec.color = hl.lineColor;
            float lw = qMax(1.0f, hl.outlineWidth);
            spec.width = lw * 1.5f;
            spec.capWidth = lw;
            spec.depthTest = true;
            spec.depthWrite = false;
            spec.caps = hl.hideVertex;
            for (const auto &draw : scene) {
                if (!isTriangle(draw) || isHidden(draw))
                    continue;
                view->submitOutlineEdges(draw, silhouetteRef, spec);
            }
        }
    }

    // 9. Selected/preselected face outlines, last of all (GL draws
    // them at the very end of the frame under
    // RenderPassSelectionOutline; selections before preselection).
    auto faceOutlineSpec = [](const Render::DrawCall &draw) {
        BGFXView::OutlineSpec spec;
        spec.view = BGFXView::ViewHighlight;
        spec.color = draw.material.emissive;
        spec.width = draw.material.outlinewidth;
        spec.depthTest = false;
        spec.caps = true;
        spec.start = draw.indexStart;
        spec.count = draw.indexCount;
        return spec;
    };
    for (const auto &sel : selections) {
        if (sel.first <= 0)
            continue;
        for (const auto &draw : sel.second) {
            if (isTriangle(draw) && draw.partIndex >= 0
                    && draw.material.faceoutline)
                view->submitOutline(draw, ++outlineRef,
                                    faceOutlineSpec(draw));
        }
    }
    for (const auto &draw : highlight) {
        if (isTriangle(draw) && draw.partIndex >= 0
                && draw.material.faceoutline)
            view->submitOutline(draw, ++outlineRef,
                                faceOutlineSpec(draw));
    }
    view->ontop = false;

    // 10. Overlay feeds (foreground superimposition, corner axis
    // cross): each slot draws late into its own view — anchor-derived
    // camera and viewport, fresh depth — through the normal submit
    // path with the target view overridden.
    {
        // A capture taken for an image export drops the viewport
        // chrome: the corner-anchored and pixel-space feeds are the
        // navigation cube, the corner axis cross and on-screen text,
        // which belong to the viewport rather than to the model.
        //
        // Two kinds of feed are NOT chrome and stay in an export. The
        // scene-camera feeds are in-scene content (editing overlays,
        // dimensions). And a full-viewport feed that is not pixel-space
        // is the foreground root: the front-root graphs view providers
        // publish, which is where the scalar colour bars live (FEM's
        // post-processing legend, Mesh curvature, Inspection). A legend
        // is what makes the exported colours mean anything, so an export
        // of a coloured result without it is the wrong picture -- and
        // the Coin path this stands in for always kept it.
        // Which feeds these are, and why some are dropped, is resolved
        // in frameOverlays above.
        int slot = 0;
        for (const auto &ov : frameOverlays) {
            if (slot >= BGFXView::NumOverlayViews) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    fprintf(stderr,
                            "bgfx: overlay feed %d dropped (only %d"
                            " overlay views)\n", ov.first,
                            int(BGFXView::NumOverlayViews));
                }
                break;
            }
            view->overlayView = int(BGFXView::ViewOverlay0) + slot;
            // Billboard text in an overlay with its OWN mini camera (corner
            // axis cross, NaviCube axis labels) must size against that
            // camera. sceneCamera overlays (editing / dimension feeds) draw
            // with the main view+proj, so their billboard text keeps the
            // main-scene sizing (overlayAnchor stays null); pixelSpace
            // overlays carry no billboard text.
            const Render::OverlayAnchor &anchor = ov.second->anchor;
            if (!anchor.sceneCamera && !anchor.pixelSpace) {
                view->overlayAnchor = &anchor;
                // Rect pixel height the overlay renders into (mirrors the
                // view-config loop), so fixed-pixel glyph sizing lands.
                view->overlayRectHeight =
                    anchor.corner == Render::OverlayAnchor::FullViewport
                    ? float(height)
                    : std::max(1.0f, anchor.sizeFraction
                                     * float(std::min(width, height)));
            }
            else {
                view->overlayAnchor = nullptr;
                view->overlayRectHeight = 0.f;
            }
            for (const auto &draw : ov.second->draws)
                view->submit(draw, viewMat);
            ++slot;
        }
        view->overlayView = -1;
        view->overlayAnchor = nullptr;
        view->overlayRectHeight = 0.f;
    }

    // The attached frame consumer (docs/CAMSimRenderPort.md sec 8,
    // runs split per sec 10.2). Submitted here for reading order
    // only: the scene run's ids sit before the caustics and the
    // volumetric apply, the overlay run's between the transparent
    // bucket and bloom, and it is those ids -- not this position --
    // that place its draws in the frame. The consumer's pass numbers
    // are contiguous 0..N-1; the ids array concatenates the two runs
    // in that order.
    //
    // Bound for the duration of the call and unbound after, so a
    // consumer that squirrelled the surface away cannot submit
    // outside it. Everything the callback may need about where it is
    // drawing rides the bind: the scene target, its attachments, its
    // pixel size (which is the SCENE target's, not the widget's),
    // whether its colour is linear, and the camera the target was
    // drawn with -- the last so a consumer can put its own image into
    // the depth buffer this frame shares. The projection is the one
    // the scene actually used, jitter included while the idle
    // accumulator is refining.
    if (consumerPassCount > 0) {
        uint16_t ids[BGFXView::NumConsumerSceneViews
                     + BGFXView::NumConsumerOverlayViews];
        int nids = 0;
        for (int c = 0; c < consumerSceneCount; ++c)
            ids[nids++] = view->vid(V::ViewConsumerScene0 + c);
        for (int c = 0; c < consumerOverlayCount; ++c)
            ids[nids++] = view->vid(V::ViewConsumerOverlay0 + c);
        BGFXHostSurface::FrameBind bind;
        bind.ids = ids;
        bind.numIds = unsigned(consumerPassCount);
        bind.target = view->bgfxFbo;
        bind.color = view->bgfxColor;
        bind.depth = view->bgfxDepth;
        bind.width = int(view->width);
        bind.height = int(view->height);
        bind.linearColor = view->hdrScene;
        bind.viewMtx = view->viewMatrix;
        bind.projMtx = view->projMatrix;
        consumerSurface->bindFrame(bind);
        frameConsumer->drawFrame(consumerSurface->surface());
        consumerSurface->unbindFrame();
    }

    // Idle temporal accumulation. Submitted here for reading order --
    // it is the pass ids, assigned in enum order above, that place
    // these two after everything else in the frame.
    if (view->passLive(V::ViewAccum)) {
        view->submitTemporalAccum(accumBlend);
        if (view->accumFrames < accumSamples) {
            ++view->accumFrames;
            // Ask the viewer for the next sample. Nothing else would:
            // the scene is unchanged, which is the whole premise. At
            // the budget this stops, and the view goes quiet holding
            // the converged image.
            animatedFrame = true;
        }
    }

    if (view->passLive(V::ViewOITComposite))
        view->submitComposite();

    // docs/FarFieldProxies.md §10.1: the occluded fraction, tested
    // against the depth this frame just finished writing. Submitted
    // last so that every pass that writes depth has had its say --
    // the pass's view id, not its submission order, is what places
    // the draws in the frame.
    if (debugconf.occlusion) {
        const float h = float(view->height);
        driveOcclusionProbe(*view, occlusionProbe, occlusionBatch,
                            occlusionLeases, scene,
                            reinterpret_cast<const float *>(viewMatrix),
                            reinterpret_cast<const float *>(projMatrix), h);
    }

    cpuMark(CpuPostSel);
    // Collect once per wall frame, after the LAST sub-view has stamped
    // what it uses -- an earlier submit would sweep meshes a later
    // sub-view still draws this very frame.
    if (!subCtx.active || subCtx.last)
        view->collectMeshes(publishedMeshes(), gatedOnlyMeshes);

    // Anything that reached the discard view drew nothing: the pass
    // declaration above missed a case the submission side takes.
    // Name the passes -- once per view, since a mis-declared pass
    // repeats every frame -- because the symptom on its own (a
    // missing shadow, an overlay that stopped appearing) says
    // nothing about view ids.
    if (view->sinkHits && !view->sinkReported) {
        view->sinkReported = true;
        std::string passes;
        for (int p = 0; p < BGFXView::NUM_VIEWS; ++p) {
            if (view->sinkPasses[p / 64] & (uint64_t(1) << (p % 64)))
                passes += " " + std::to_string(p);
        }
        RENDER_ERR("bgfx pass map: " << view->sinkHits
                   << " draw(s) went to the discard view from pass(es)"
                   << passes.c_str()
                   << " -- those passes were not declared for this "
                      "frame and did not render");
    }

    cpuMark(CpuPostTail);
    // Timed on its own: in single-threaded mode bgfx::frame() runs
    // the whole backend inline, so this call is where the `submit`
    // figure lives. What it leaves over inside render() is our
    // per-draw C++, which is what phase 2 would attack. Only the
    // call is timed -- the GL context switches around it are Qt's
    // cost, not bgfx's, and folding them in would flatter phase 2.
    auto timedBgfxFrame = [&]() {
        const int64_t t0 = bx::getHPCounter();
        const uint32_t n = bgfx::frame();
        if (debugconf.frameTiming)
            frameStats.bgfxFrameMs += 1000.0
                * double(bx::getHPCounter() - t0)
                / double(bx::getHPFrequency());
        return n;
    };
    uint32_t frameNum = 0;
#ifdef FC_RENDERER_STANDALONE
    view->present();
    if (subCtx.active && !subCtx.last) {
        // A mid-sequence sub-view submit: its passes (present
        // included) are queued; the frame boundary and the whole
        // post-frame tail belong to the last submit
        // (docs/SplitViews.md sec 9.2).
        renderOk = true;
        hasScene = !scene.empty();
        return true;
    }
    frameNum = timedBgfxFrame();
    _BGFXLib.sweepUserCaches();
#else
    // The output colour transform, when one is selected: encode the
    // finished frame into presentTex so the blit below transfers the
    // encoded image rather than the linear one. Submitted here, with
    // the rest of the frame already queued, because ViewPresent is the
    // last view id -- the same place the standalone present sits.
    if (view->outputTransform != Render::OutputConfig::None
            && bgfx::isValid(view->presentFbo))
        view->present();
    // The capture rides on ViewCapture, the last view id, so it has to
    // be QUEUED before the frame boundary that executes it -- the
    // opposite of the GL composite below, which reads a framebuffer
    // bgfx has already filled. Everything the frame draws is submitted
    // by now, present included, so what gets copied is the finished
    // image.
    //
    // The completeness verdict is needed here rather than after the
    // boundary because the hold decides whether to copy at all; its
    // inputs are all settled by submission, and the assignments that
    // publish it stay where they were.
    const bool frameWasComplete = !_BGFXLib.userProgramStoodIn
        && !hostHold && !frameOwes;
    const bool holdDump = dumpPending && !frameWasComplete
        && pendingDump.waitComplete;
    if (captureWanted && !holdDump) {
        // Encode the scene depth into a colour target this pass owns,
        // then ask for both it and the finished colour back. Depth is
        // what no backend blits, and geometryPixels is measured from
        // it, so this pass is the whole reason a capture is portable.
        bgfx::setTexture(0, view->s_texSceneDepth, view->bgfxDepth);
        view->fullscreen(BGFXView::ViewCaptureDepth, view->m_progDepthEnc,
                         BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        // Sized off the staging texture's own format, which
        // ensureCaptureTargets picked to match the blit source -- the
        // RGBA8 present output when a colour transform is on, the
        // scene colour otherwise.
        captureHdr = view->captureColorFormat
            == bgfx::TextureFormat::RGBA16F;
        captureColor.assign(size_t(view->width) * view->height
                            * (captureHdr ? 8 : 4), 0);
        captureDepth.assign(size_t(view->width) * view->height * 4, 0);
        capturePixW = view->width;
        capturePixH = view->height;
        captureIsDump = dumpPending;
        captureRequest = pendingDump;
        captureReadyFrame = view->readbackCapture(captureColor.data(),
                                                  captureDepth.data());
        if (!captureReadyFrame) {
            captureColor.clear();
            captureDepth.clear();
            captureIsDump = false;
        }
        // dumpPending deliberately STAYS set until the readback
        // lands. It is what frameDumpPending() reports and what
        // pumpFrameDump loops on, and the request is not served until
        // the pixels are actually here -- clearing it at the queue
        // would hand the caller the PREVIOUS capture's statistics,
        // which is a wrong answer rather than a slow one. A second
        // request cannot start a second readback meanwhile:
        // captureWanted requires captureReadyFrame to be clear.
    }
    // The finished frame belongs in whatever framebuffer the caller had
    // bound when it asked for it: the widget's own for an on-screen
    // frame, a capture target for a screenshot (renderOffscreen).
    // QOpenGLWidget::makeCurrent() binds its own framebuffer, so the
    // caller's -- remembered at the top of this frame, where every bail
    // reads it too -- is restored before the blit, which transfers into
    // whatever is bound.
    widget->doneCurrent();
    cpuMark(CpuCtxDone);
    _BGFXLib.makeCurrent();
    cpuMark(CpuCtxOut);
    frameNum = timedBgfxFrame();
    _BGFXLib.sweepUserCaches();
    // bgfx::frame() has its own timer; restart the chain past it so
    // it is not counted twice.
    if (debugconf.frameTiming)
        cpuMarkT = bx::getHPCounter();
    widget->makeCurrent();
    cpuMark(CpuCtxIn);
    QOpenGLContext::currentContext()->extraFunctions()
        ->glBindFramebuffer(GL_FRAMEBUFFER, GLuint(hostFbo));
    // A sub-view rect is stated in the DESTINATION framebuffer's own
    // pixels -- the widget's device pixels -- because that is what the
    // blit writes into and what the y-flip has to measure against.
    // A draw that asked for a user program still compiling drew with
    // the stock program standing in (getUserProgram). That frame is
    // right to show and wrong to capture: a one-shot dump is a picture
    // of the scene with its materials, and a surface drawn without one
    // is not that -- it is how a chess piece came back plain white in a
    // golden capture, always the piece whose compile finished last
    // (docs/RenderDebug.md sec 5.2a). So the dump is held for a later
    // frame: needsRedraw() keeps reporting it, the compile's finish
    // bumps the generation, and the frame that draws with the program
    // consumes it. Bounded by the compile itself -- a failed or killed
    // compile is recorded and its draw stands in without asking again.
    // And the host's word (holdFrameDump): the publish that fed this
    // frame deferred shapes under its capture budget, so the scene is
    // still arriving. Same hold, same release -- the follow-up publish
    // the viewer schedules catches the deferred shapes up.
    // The verdict is the frame's, dump or no dump: it is what
    // Renderer::frameComplete reports and what the counters below
    // let a waiter wait for. Two flags and two increments; nothing
    // here waits.
    // Both were decided before the frame boundary, where the capture
    // had to read them (the copy is queued, not read back, so it
    // cannot wait until here).
    hostHold = false;
    lastFrameComplete = frameWasComplete;
    ++renderedFrameCount;
    if (frameWasComplete)
        ++completeFrameCount;
    dumpHeld = holdDump;
    // The composite only. The frame readback that used to live inside
    // this call is now the portable capture queued above -- which is
    // what lets a golden render test gate a backend that has no GL
    // framebuffer to read.
    view->blit(&lastStats,
               subCtx.active ? subCtx.x : 0,
               subCtx.active ? subCtx.y : 0,
               subCtx.active
                   ? int(widget->height() * widget->devicePixelRatioF() + 0.5)
                   : 0);
    cpuMark(CpuBlit);
#endif

    // The capture lands a frame or two after the copy was queued, the
    // same arrangement and for the same reason as the cull audit
    // below: bgfx says which frame will have filled the buffers, and
    // they must not be touched before it. pumpFrameDump is a loop over
    // frames, so the wait costs the caller nothing but the frames it
    // was already driving.
    if (captureReadyFrame && frameNum >= captureReadyFrame) {
        const size_t n = size_t(capturePixW) * capturePixH;
        // Decode to the tight RGBA8, top-down image writeDumpImage and
        // the stats both want. Two things vary by backend and this is
        // where both are answered: the colour may be RGBA16F (linear
        // light, while colour managed) and the origin may be bottom
        // left. The old path hard-coded "glReadPixels rows are
        // bottom-up", which is true of GL and of nothing else.
        const bool flip = bgfx::getCaps()->originBottomLeft;
        std::vector<unsigned char> rgba(n * 4);
        long long nonFinite = 0;
        for (uint16_t y = 0; y < capturePixH; ++y) {
            const size_t sy = flip ? size_t(capturePixH - 1 - y) : y;
            unsigned char *dst = &rgba[size_t(y) * capturePixW * 4];
            if (captureHdr) {
                const uint16_t *src = reinterpret_cast<const uint16_t *>(
                        captureColor.data()) + sy * capturePixW * 4;
                for (uint16_t x = 0; x < capturePixW; ++x)
                    for (int c = 0; c < 4; ++c) {
                        const float v = bx::halfToFloat(src[x * 4 + c]);
                        // NaN walks straight through min/max -- every
                        // comparison against it is false, so both return
                        // it -- and std::lround(NaN) is undefined, which
                        // turned a NaN channel into an arbitrary byte.
                        // Measured: a shader NaN reached a golden as a
                        // saturated primary and read as a shading
                        // difference rather than as the NaN it was. Zero
                        // is what the clamp was already asking for.
                        float f;
                        if (v == v && v - v == 0.0f) {
                            f = std::min(std::max(v, 0.0f), 1.0f);
                        }
                        else {
                            f = 0.0f;
                            ++nonFinite;
                        }
                        dst[x * 4 + c] = (unsigned char)
                            std::lround(f * 255.0f);
                    }
            }
            else {
                std::memcpy(dst,
                            captureColor.data() + sy * capturePixW * 4,
                            size_t(capturePixW) * 4);
            }
        }
        // geometryPixels against the same 0.999 threshold the GL
        // readback used, so the number means what it always meant --
        // and now means it on every backend, which is the point of
        // encoding depth into a colour target at all.
        const float *depth = reinterpret_cast<const float *>(
                captureDepth.data());
        long ng = 0, r = 0, g = 0, b = 0;
        for (uint16_t y = 0; y < capturePixH; ++y) {
            const size_t sy = flip ? size_t(capturePixH - 1 - y) : y;
            const float *drow = depth + sy * capturePixW;
            const unsigned char *crow = &rgba[size_t(y) * capturePixW * 4];
            for (uint16_t x = 0; x < capturePixW; ++x) {
                if (drow[x] < 0.999f) {
                    ++ng;
                    r += crow[x * 4];
                    g += crow[x * 4 + 1];
                    b += crow[x * 4 + 2];
                }
            }
        }
        lastStats.width = capturePixW;
        lastStats.height = capturePixH;
        lastStats.temporalSamples = view->accumFrames;
        lastStats.geometryPixels = ng;
        lastStats.nonFiniteChannels = captureHdr ? nonFinite : -1;
        // Said out loud as well as recorded: this is a broken frame,
        // and the capture that carries it should not become a golden.
        if (nonFinite)
            RENDER_ERR("capture read back " << nonFinite
                       << " non-finite channels (forced to 0) -- the"
                          " shading produced NaN or infinity, so this"
                          " frame is not fit to bless");
        lastStats.avgColor[0] = ng ? float(r) / float(ng) : -1.0f;
        lastStats.avgColor[1] = ng ? float(g) / float(ng) : -1.0f;
        lastStats.avgColor[2] = ng ? float(b) / float(ng) : -1.0f;
        lastStats.valid = true;
        static const char *const envDump =
            getenv("FC_BGFX_DEBUG_DUMP_FRAME");
        if (getenv("FC_BGFX_DEBUG_READBACK")) {
            fprintf(stderr,
                    "bgfx capture %dx%d: %ld geometry pixels,"
                    " avg color %ld,%ld,%ld\n",
                    int(capturePixW), int(capturePixH), ng,
                    ng ? r/ng : -1, ng ? g/ng : -1, ng ? b/ng : -1);
#ifndef FC_RENDERER_STANDALONE
            if (envDump && *envDump)
                BGFXView::writeDumpImage(envDump, rgba.data(),
                                         capturePixW, capturePixH);
#endif
        }
        // Writing the frame to a path is a desktop errand. The browser
        // tier has no filesystem anybody could fetch from and answers a
        // dump request by sending the pixels back over the wire, which
        // the caller below is already pumping on.
#ifndef FC_RENDERER_STANDALONE
        if (captureIsDump && !captureRequest.path.empty()
                && !BGFXView::writeDumpImage(captureRequest.path,
                                             rgba.data(),
                                             capturePixW, capturePixH))
            fprintf(stderr, "bgfx: frame dump write failed: %s\n",
                    captureRequest.path.c_str());
#endif
        captureReadyFrame = 0;
        dumpHeld = false;
        if (captureIsDump) {
            // Served: the pixels are here and lastStats describes them,
            // so this is where the request the caller is pumping on
            // ends.
            dumpPending = false;
            if (!captureRequest.overlays) {
                // That frame went to the screen as well as to the
                // capture, and it is missing the chrome the capture
                // asked to leave out. It would stay on screen until
                // something else happened to dirty the scene, so
                // redraw it whole.
                sceneDirty = true;
            }
        }
        captureIsDump = false;
    }

    // The cull audit's id image lands a frame or two after the copy
    // was queued — bgfx says which frame, and the buffer must not be
    // touched before it. Checked here rather than at the top of the
    // next publishScene because publishScene returns early on
    // several paths, and an audit that only ran on frames that got
    // all the way through would silently sample a subset.
    if (idReadyFrame && frameNum >= idReadyFrame) {
        reportCullAudit(idPixels, idPixW, idPixH, idMask, idOwner,
                        idNodeAudit, idKeys, objectInfo, idHist);
        // After it, and off its histogram: the arms are a question
        // about the same image, and reportCullAudit is what decodes
        // it. It bails before filling the histogram on a broken id
        // pass, which is exactly when these must not report either.
        reportTightBounds(idHist, idMask, idTightJudged, idTightAabb,
                          idTightObb, idTightTri, idTight);
        idReadyFrame = 0;
    }

    // docs/FarFieldProxies.md §10.1: what that frame cost the CPU
    // against what it cost the GPU. Sampled here rather than at the
    // top of the next frame so that the numbers belong to a frame
    // that was actually submitted -- publishScene returns early on
    // several paths, and a sample taken on one of those would
    // average a frame that drew nothing into the mean.
    // ⚠️ bgfx fills `viewStats` only while this is set, and it is a
    // whole-context switch rather than a per-view one — so it is
    // turned on with the timing readout and off with it, never left
    // on for a frame nobody is measuring.
    if (debugconf.frameTiming != profilerOn) {
        profilerOn = debugconf.frameTiming;
        bgfx::setDebug(profilerOn ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
        // The Gui-side scopes read the same switch, so `outside` is
        // broken down exactly on the windows the frame line prints.
        Render::FrameOutside::setEnabled(profilerOn);
    }
    if (debugconf.frameTiming) {
        accumulateFrameStats(frameStats, view->width, view->height,
                             [&](uint16_t id) {
                                 return view->passIndexOf(id);
                             });
        const bool due = frameStatsDue();
        // Snapshot before the report, which resets the accumulator.
        std::map<int, std::pair<double, double>> viewMs;
        uint32_t viewFrames = 0;
        double phaseMs[CpuPhaseCount] = {};
        double outMs[Render::FrameOutside::PhaseCount] = {};
        double ourMs = 0.0, bgfxMs = 0.0, outsideMs = 0.0;
        if (due) {
            viewMs = frameStats.viewMs;
            viewFrames = frameStats.frames;
            for (int i = 0; i < CpuPhaseCount; ++i)
                phaseMs[i] = cpuPhaseMs[i];
            ourMs = frameStats.renderMs;
            bgfxMs = frameStats.bgfxFrameMs;
            // The same subtraction the frame line prints, taken
            // before the report resets the accumulator.
            outsideMs = frameStats.frameMs - frameStats.renderMs;
            Render::FrameOutside::drain(outMs);
            std::memset(cpuPhaseMs, 0, sizeof(cpuPhaseMs));
        }
        if (due)
            reportFrameStats(frameStats);
        // Which part of our own C++ the fixed per-frame cost is in.
        // `rest` is derived rather than measured on purpose: it is
        // everything inside render() that is neither scoped above nor
        // bgfx's, so a breakdown can be incomplete but never wrong
        // about how much it failed to account for.
        //
        // ! renderMs for the reporting frame itself is added after
        // this runs (the outer render() adds it on the way out), so
        // `ours` here trails the window by one frame. Over 13-20
        // frames that is under a frame and it does not accumulate.
        if (due && viewFrames) {
            const double f = double(viewFrames);
            // pre excludes the cull, which it contains; post is what
            // is left over once every measured region is removed.
            const double pre = phaseMs[CpuPreSubmit] - phaseMs[CpuCull];
            const double post = ourMs - bgfxMs
                - phaseMs[CpuPreSubmit] - phaseMs[CpuSubmitLoop];
            // `unattr` is post minus the four measured sub-spans: if
            // it is not ~0 the chain has a gap and the split below
            // is not to be believed.
            const double unattr = post - phaseMs[CpuPostCaps]
                - phaseMs[CpuPostEffects] - phaseMs[CpuPostSel]
                - phaseMs[CpuPostTail] - phaseMs[CpuCtxOut]
                - phaseMs[CpuCtxIn] - phaseMs[CpuBlit]
                - phaseMs[CpuCtxDone];
            FC_RENDER_MSG(
                    "render cpu phases (ms/frame): pre %.2f | cull %.2f | "
                    "submitloop %.2f | post %.2f [caps %.2f effects %.2f "
                    "sel %.2f tail %.2f done %.2f ctxout %.2f ctxin %.2f blit %.2f "
                    "unattr %.2f] | bgfx::frame %.2f | ours %.2f\n",
                    pre / f, phaseMs[CpuCull] / f,
                    phaseMs[CpuSubmitLoop] / f, post / f,
                    phaseMs[CpuPostCaps] / f, phaseMs[CpuPostEffects] / f,
                    phaseMs[CpuPostSel] / f, phaseMs[CpuPostTail] / f,
                    phaseMs[CpuCtxDone] / f,
                    phaseMs[CpuCtxOut] / f, phaseMs[CpuCtxIn] / f,
                    phaseMs[CpuBlit] / f, unattr / f, bgfxMs / f,
                    ourMs / f);
        }
        // * The other side of the same frame: what the *rest* of the
        // process spends between one backend frame and the next.
        // Natively this is the largest of the three terms and had no
        // instrument at all (docs/DrawSubmission.md).
        //
        // `unattr` is the whole point. The phases below are Gui code
        // we chose to bracket; `outside` is a subtraction that
        // includes everything we did not -- Qt's paint plumbing, the
        // swap, the event loop between frames. A small remainder
        // says the brackets found the cost; a large one says the
        // cost is somewhere nobody has looked yet, and either answer
        // is worth more than the six numbers on their own.
        //
        // ! Like `ours` above, these trail the window by one frame:
        // the reporting frame's outside phases run after this line
        // is printed. Over 13-20 frames that is under a frame and it
        // does not accumulate.
        if (due && viewFrames) {
            const double f = double(viewFrames);
            double sum = 0.0;
            for (double v : outMs)
                sum += v;
            FC_RENDER_MSG(
                    "render outside (ms/frame): outside %.2f [pre %.2f | "
                    "background %.2f | coin %.2f | foreground %.2f | "
                    "captures %.2f | chrome %.2f | paintpre %.2f | "
                    "delayq %.2f | gview %.2f | paintpost %.2f | "
                    "unattr %.2f]\n",
                    outsideMs / f,
                    outMs[Render::FrameOutside::Pre] / f,
                    outMs[Render::FrameOutside::Background] / f,
                    outMs[Render::FrameOutside::Coin] / f,
                    outMs[Render::FrameOutside::Foreground] / f,
                    outMs[Render::FrameOutside::Captures] / f,
                    outMs[Render::FrameOutside::Chrome] / f,
                    outMs[Render::FrameOutside::PaintPre] / f,
                    outMs[Render::FrameOutside::DelayQueue] / f,
                    outMs[Render::FrameOutside::GraphicsView] / f,
                    outMs[Render::FrameOutside::PaintPost] / f,
                    (outsideMs - sum) / f);
        }
        // ⭐ Where the frame line's milliseconds actually go, per
        // pass, for both processors (docs/DrawSubmission.md phase
        // 0). Sorted by CPU cost and capped, with the number
        // dropped stated: a readout that silently shows the top few
        // reads as though the rest were nothing.
        if (due && !viewMs.empty() && viewFrames) {
            std::vector<std::pair<int, std::pair<double, double>>>
                    ranked(viewMs.begin(), viewMs.end());
            std::sort(ranked.begin(), ranked.end(),
                      [](const auto &a, const auto &b) {
                          return a.second.first > b.second.first;
                      });
            const double f = double(viewFrames);
            double cpuAll = 0.0, gpuAll = 0.0;
            for (const auto &r : ranked) {
                cpuAll += r.second.first;
                gpuAll += r.second.second;
            }
            std::string line;
            const size_t show = std::min<size_t>(ranked.size(), 8);
            for (size_t i = 0; i < show; ++i) {
                char b[128];
                snprintf(b, sizeof(b), " %s %.2f/%.2f",
                         BGFXView::passNameOfIndex(ranked[i].first).c_str(),
                         ranked[i].second.first / f,
                         ranked[i].second.second / f);
                line += b;
            }
            FC_RENDER_MSG(
                    "render passes (cpu/gpu ms, top %zu of %zu, "
                    "totals %.2f/%.2f):%s\n",
                    show, ranked.size(), cpuAll / f, gpuAll / f,
                    line.c_str());
        }
        // ⭐ What instancing collapsed, on the same cadence. Read
        // `replaced` against the frame line's draw count: that is
        // the share of submission the batching already removes, and
        // therefore the ceiling of anything built on top of it
        // (docs/DrawSubmission.md phase 0.5).
        if (due) {
            const InstancingStats &is = instStats;
            FC_RENDER_MSG(
                    "render instancing: %u groups (%u usable, %u "
                    "singleton) over %u rows | %u submits replaced %u "
                    "draws | refused %u groups / %u draws | thinned to "
                    "one: %u draws%s%s\n",
                    is.groups, is.groupsUsable, is.groupsSingleton,
                    is.eligible, is.submits, is.drawsReplaced,
                    is.refused, is.refusedMembers, is.thinnedMembers,
                    is.why ? " | NOT RUN: " : "",
                    is.why ? is.why : "");
        }
        // What the culling actually did, on the same cadence and
        // from the same switch: the frame line reports the draws
        // that survived, and without this there is no way to tell a
        // scene that hides nothing from a mechanism that is not
        // working. The test boxes are themselves draws and are
        // counted in that line, so a win has to be net of them.
        if (due && cullconf.enabled && cullconf.software) {
            // A different mechanism reports different things, and
            // saying so in the same line under different names would
            // make two runs look comparable when the numbers mean
            // different work. What carries over is the left half --
            // instances and nodes -- which is what a comparison
            // between the oracles is actually about.
            const auto &ms = maskedCull.lastFrame();
            const auto &bs = maskedCull.depth().stats();
            FC_RENDER_MSG(
                    "render culling: instances hidden %u / drawn %u / "
                    "offscreen %u | nodes visited %u hidden %u offscreen %u "
                    "tested %u | occluders %u of %u draws, %u tris, "
                    "dropped %u, %u threads | buffer %dx%d, tris drawn %u clipped %u "
                    "culled %u (offbuf %u subpx %u degen %u), blocks %u "
                    "| %s filtered %u guarded %u "
                    "| nearclip %u rootrefused %u "
                    "| perinst tested %u hid %u redundant %u in %.2fms "
                    "| hulls %u of %u draws, saved %llu tris "
                    "(held %u, built %u, pending %u, %.1fMB, %.2fms) "
                    "| raster %.2fms (select %.2f shard %.2f merge %.2f "
                    "| worst clear %.2f raster %.2f merge %.2f "
                    "| sum raster %.2f) walk %.2fms | indexed %u of %u draws "
                    "(%u on-top exempt) | index %u nodes, build %.1fms\n",
                    ms.hiddenInstances, ms.drawnInstances,
                    ms.offscreenInstances, ms.nodesVisited, ms.nodesHidden,
                    ms.nodesOffscreen, ms.nodesTested, ms.occluderDraws,
                    ms.occluderCandidates, ms.occluderTriangles,
                    ms.occludersDropped, ms.occluderThreads,
                    maskedCull.depth().width(),
                    maskedCull.depth().height(), bs.trianglesDrawn,
                    bs.trianglesClipped, bs.trianglesCulled(),
                    bs.trianglesOffBuffer, bs.trianglesSubPixel,
                    bs.trianglesDegenerate,
                    bs.blocksUpdated,
                    // Which of the four backends in Simd4.h was
                    // compiled in: "scalar" here means the pre-pass
                    // is running four lanes one at a time, and a
                    // timing taken against it is not a timing of
                    // SIMD.
                    cullconf.softwareSimd ? Render::simd4Name() : "off",
                    bs.trianglesFiltered, bs.trianglesGuarded,
                    ms.nearExempt, ms.rootRefused,
                    // What asking per object rather than per group
                    // added, and what it cost to ask (section 12.17).
                    // "hid" is the whole of what the mode buys: draws
                    // whose own box was hidden inside a group that
                    // had already answered visible.
                    ms.instancesTested, ms.instancesHiddenAlone,
                    ms.instancesRedundant, ms.instanceMs,
                    // What the coarse path did, beside what it cost.
                    // "saved" is the budget the hulls did not spend,
                    // which is the budget that went to a candidate
                    // that would otherwise have been dropped.
                    ms.coarseDraws, ms.occluderDraws,
                    (unsigned long long)ms.coarseTrianglesSaved,
                    ms.coarseEntries, ms.coarseBuilt, ms.coarsePending,
                    double(ms.coarseBytes) / (1024.0 * 1024.0),
                    ms.coarseBuildMs,
                    ms.rasterMs, ms.selectMs, ms.shardMs, ms.mergeMs,
                    ms.worstClearMs, ms.worstRasterMs, ms.worstMergeMs,
                    ms.sumRasterMs, ms.walkMs,
                    cullIndexed, unsigned(scene.size()), cullExemptOnTop,
                    unsigned(culler.hierarchy().nodes().size()),
                    cullBuildMs);
        }
        else if (due && cullconf.enabled) {
            const auto &cs = culler.lastFrame();
            FC_RENDER_MSG(
                    "render culling: instances hidden %u / drawn %u / "
                    "offscreen %u | nodes visited %u hidden %u offscreen %u "
                    "| tests offered %u budgeted %u sent %u | queries "
                    "inflight %zu held %u expired %u refused %u "
                    "| nearclip %u forced %u "
                    "rootrefused %u rootpx %d | indexed %u of %u draws "
                    "(%u on-top exempt) | index %u nodes, build %.1fms\n",
                    cs.hiddenInstances, cs.drawnInstances,
                    cs.offscreenInstances, cs.nodesVisited, cs.nodesHidden,
                    cs.nodesOffscreen, cs.nodesOffered, cs.nodesTested,
                    cullQueries.lastSent, cullQueries.inflight.size(),
                    cullQueries.leases.held(), cullQueries.expired,
                    cullQueries.leases.refusals(),
                    cs.nearExempt, cs.forcedVisible, cs.rootRefused,
                    culler.nodePixels(culler.hierarchy().root()),
                    cullIndexed, unsigned(scene.size()), cullExemptOnTop,
                    unsigned(culler.hierarchy().nodes().size()),
                    cullBuildMs);
        }
        // The A/B probe's readout (12.13), on the same cadence: the
        // one number the wire-or-delete decision needs is `gain`,
        // the fraction of the frame the whole occlusion block saves
        // net of what it costs to decide. Negative means deciding
        // costs more than not drawing saves.
        if (due && cullconf.enabled && cullconf.benefitProbe) {
            const auto rep = cullBenefit.report();
            FC_RENDER_MSG(
                    "render culling benefit: %s | culled %.2fms vs "
                    "unculled %.2fms | gain %+.1f%% | probes %u%s "
                    "(arm seen %u sampled %u)\n",
                    rep.culling ? "WORTH IT" : "not worth it",
                    rep.culledMs, rep.uncalledMs, rep.gain * 100.0f,
                    rep.probes,
                    rep.probing ? " (probing)" : "",
                    rep.armSeen, rep.armSamples);
        }
    }

    // Geometry the GPU handle pool refused this frame. Unconditional --
    // not behind levelDebug -- because this is not a quality decision
    // the renderer made, it is geometry missing from the screen.
    if (view->bufferDeniedMeshes != bufferDeniedSeen) {
        const size_t was = bufferDeniedSeen;
        bufferDeniedSeen = view->bufferDeniedMeshes;
        if (view->bufferDeniedMeshes)
            FC_RENDER_MSG(
                "render levels: GPU handle pool REFUSED %zu meshes "
                "(%zu submissions skipped) -- that geometry is NOT on "
                "screen; retrying as handles come back\n",
                view->bufferDeniedMeshes, view->bufferDeniedSubmits);
        else if (was != kNeverReported)
            FC_RENDER_MSG("render levels: GPU handle pool recovered -- "
                          "every mesh asked for has its buffers\n");
    }

    if (!hasScene && !scene.empty())
        qDebug() << "bgfx: scene consumed:" << view->drawcount
                 << "draws," << view->meshes.size() << "meshes";

    if (getenv("FC_BGFX_DEBUG_READBACK"))
        fprintf(stderr, "bgfx frame %llu: scene=%zu sel=%zu hl=%zu"
                " dups=%zu draws=%d\n",
                (unsigned long long)view->frame, scene.size(),
                selections.size(), highlight.size(),
                dupDraws.size(), view->drawcount);

    // A submitted live time-referencing user draw keeps the
    // animation loop alive like the stock timed effects.
    animatedFrame = animatedFrame || _BGFXLib.userAnimatedDraw;

    renderOk = true;
    hasScene = !scene.empty();
    return true;
}

uint32_t BGFXRenderer::Private::invertCapColor(uint32_t col)
{
    auto inv = [](uint32_t c) -> uint32_t {
        return (c > 120 && c < 140) ? 180 : 255 - c;
    };
    uint32_t r = inv((col >> 24) & 0xff);
    uint32_t g = inv((col >> 16) & 0xff);
    uint32_t b = inv((col >> 8) & 0xff);
    if (r + g + b < 10)
        r = g = b = 50;
    return (r << 24) | (g << 16) | (b << 8) | (col & 0xff);
}

void BGFXRenderer::Private::buildCapQuad(const float plane[4], const float bmin[3],
                  const float bmax[3], const float *projMat,
                  int vpWidth, CapVertex verts[4]) const
{
    float n[3] = {plane[0], plane[1], plane[2]};
    float nlen = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
    if (nlen < 1e-12f)
        nlen = 1.0f;
    n[0] /= nlen; n[1] /= nlen; n[2] /= nlen;

    float center[3], ext[3];
    for (int i = 0; i < 3; ++i) {
        center[i] = 0.5f * (bmin[i] + bmax[i]);
        ext[i] = bmax[i] - bmin[i];
    }
    float radius = 0.5f * std::sqrt(
        ext[0]*ext[0] + ext[1]*ext[1] + ext[2]*ext[2]);

    // Project the center onto the plane (GL: center += -normal *
    // plane.getDistance(center)).
    float dist = center[0]*n[0] + center[1]*n[1] + center[2]*n[2]
        + plane[3] / nlen;
    for (int i = 0; i < 3; ++i)
        center[i] -= n[i] * dist;

    // Coin SbRotation(z-axis -> normal): quaternion from the cross
    // product, with the antiparallel fallback about the y axis.
    float q[4];  // x, y, z, w
    float dot = n[2];
    float cx = -n[1], cy = n[0], cz = 0.0f;  // cross(z, n)
    float crosslen = std::sqrt(cx*cx + cy*cy);
    if (crosslen < 1e-12f) {
        if (dot > 0.0f) {
            q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f;
        } else {
            q[0] = 0.0f; q[1] = 1.0f; q[2] = 0.0f; q[3] = 0.0f;
        }
    } else {
        float s = std::sqrt(0.5f * std::fabs(1.0f - dot)) / crosslen;
        q[0] = cx * s; q[1] = cy * s; q[2] = cz * s;
        q[3] = std::sqrt(0.5f * std::fabs(1.0f + dot));
    }
    // u = q * x-axis * radius, v = q * y-axis * radius.
    auto rotate = [&q](const float in[3], float out[3]) {
        // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + w * v)
        float t[3] = {
            q[1]*in[2] - q[2]*in[1] + q[3]*in[0],
            q[2]*in[0] - q[0]*in[2] + q[3]*in[1],
            q[0]*in[1] - q[1]*in[0] + q[3]*in[2],
        };
        out[0] = in[0] + 2.0f * (q[1]*t[2] - q[2]*t[1]);
        out[1] = in[1] + 2.0f * (q[2]*t[0] - q[0]*t[2]);
        out[2] = in[2] + 2.0f * (q[0]*t[1] - q[1]*t[0]);
    };
    static const float xaxis[3] = {1.0f, 0.0f, 0.0f};
    static const float yaxis[3] = {0.0f, 1.0f, 0.0f};
    float u[3], v[3];
    rotate(xaxis, u);
    rotate(yaxis, v);
    for (int i = 0; i < 3; ++i) {
        u[i] *= radius;
        v[i] *= radius;
    }

    // Hatch texture scale (GL: _renderSection ~1852): pixels per
    // world unit at mid view depth from the projection matrix (the
    // stand-in for Coin's getWorldToScreenScale at the sight point),
    // times the bounding radius, over the texture width.
    float texscale = 0.0f;
    if (secconf.hatchEnable && hatchTex) {
        float hs = std::max(1e-4f, 0.3f * secconf.hatchScale);
        float worldPerVp;
        if (projMat[15] == 1.0f) {  // orthographic
            worldPerVp = 2.0f / projMat[0];
        } else {
            float near_ = projMat[14] / (projMat[10] - 1.0f);
            float far_ = projMat[14] / (projMat[10] + 1.0f);
            float wmid = near_ + 0.5f * (far_ - near_);
            worldPerVp = 2.0f * wmid / projMat[0];
        }
        float pixelsize = float(vpWidth) / (hs * worldPerVp);
        texscale = std::max(1e-3f, radius * pixelsize
                                       / float(hatchTex->width));
    }

    // GL vertex/texcoord assignment: v1=(0,s) v2=(0,0) v3=(s,0)
    // v4=(s,s) around center +/- u/v.
    auto set = [&](CapVertex &vert, float su, float sv,
                   float tu, float tv) {
        vert.px = center[0] + sv*v[0] + su*u[0];
        vert.py = center[1] + sv*v[1] + su*u[1];
        vert.pz = center[2] + sv*v[2] + su*u[2];
        // The plane's own normal, for the prepass copy of this quad;
        // the prepass shader faces it toward the viewer itself, so the
        // sign does not matter here.
        vert.nx = n[0];
        vert.ny = n[1];
        vert.nz = n[2];
        vert.u = tu;
        vert.v = tv;
    };
    set(verts[0], -1.0f,  1.0f, 0.0f, texscale);
    set(verts[1],  1.0f,  1.0f, 0.0f, 0.0f);
    set(verts[2],  1.0f, -1.0f, texscale, 0.0f);
    set(verts[3], -1.0f, -1.0f, texscale, texscale);
}

void BGFXRenderer::Private::submitSectionCaps(BGFXView *view, const float *projMat)
{
    auto isTransp = [](const Render::DrawCall &d) {
        return d.material.transparent
            || (d.material.pervertexcolor
                && d.mesh && d.mesh->hasTransparency);
    };
    auto eligible = [this](const Render::DrawCall &d) {
        return d.material.type == Render::Material::Triangle
            && d.partIndex < 0
            && d.material.numclipplanes > 0
            && !d.material.ontop
            && d.mesh
            && (d.material.solidshape || d.mesh->hasSolid)
            && (secconf.fill || d.material.clipconcave);
    };

    // Opaque and transparent cap sources, following the buckets the
    // fills render in (GL collects section entries in renderOpaque
    // and renderTransparency): the scene minus draws replaced by
    // whole-object selections, plus the (deduplicated) whole-object
    // selection draws themselves.
    std::vector<const Render::DrawCall *> items[2];
    for (const auto &draw : scene) {
        if (eligible(draw)
                && !(draw.objectKey && !hiddenKeys.empty()
                     && hiddenKeys.count(draw.objectKey)))
            items[isTransp(draw) ? 1 : 0].push_back(&draw);
    }
    for (const auto &sel : selections) {
        for (const auto &draw : sel.second) {
            if (eligible(draw)
                    && !(!dupDraws.empty() && dupDraws.count(&draw)))
                items[isTransp(draw) ? 1 : 0].push_back(&draw);
        }
    }

    auto samePlanes = [](const Render::Material &a,
                         const Render::Material &b) {
        if (a.numclipplanes != b.numclipplanes)
            return false;
        return std::memcmp(a.clipplanes, b.clipplanes,
                sizeof(a.clipplanes[0]) * a.numclipplanes) == 0;
    };

    for (int bucket = 0; bucket < 2; ++bucket) {
        const auto &list = items[bucket];
        uint16_t capView = bucket ? BGFXView::ViewSectionCapTransp
                                  : BGFXView::ViewSectionCap;
        for (size_t head = 0; head < list.size();) {
            const Render::Material &mat = list[head]->material;
            // Consecutive same-color same-planes run (GL groups on
            // diffuse + clippers; autozoom is not translated).
            size_t tail = head + 1;
            if (secconf.fillGroup && !mat.clipconcave) {
                while (tail < list.size()
                        && list[tail]->material.diffuse == mat.diffuse
                        && samePlanes(list[tail]->material, mat))
                    ++tail;
            }

            // Union world bbox -> cap quad placement.
            float bmin[3], bmax[3];
            bool bvalid = false;
            for (size_t k = head; k < tail; ++k) {
                const auto &d = *list[k];
                if (d.bboxMin[0] > d.bboxMax[0])
                    continue;
                if (!bvalid) {
                    bvalid = true;
                    for (int i = 0; i < 3; ++i) {
                        bmin[i] = d.bboxMin[i];
                        bmax[i] = d.bboxMax[i];
                    }
                } else {
                    for (int i = 0; i < 3; ++i) {
                        bmin[i] = qMin(bmin[i], d.bboxMin[i]);
                        bmax[i] = qMax(bmax[i], d.bboxMax[i]);
                    }
                }
            }
            if (!bvalid) {
                head = tail;
                continue;
            }

            uint32_t color = secconf.fillInvert
                ? invertCapColor(mat.diffuse) : mat.diffuse;
            for (int i = 0; i < mat.numclipplanes; ++i) {
                bool marked = false;
                for (size_t k = head; k < tail; ++k)
                    marked |= view->submitCapMark(
                        *list[k], mat.clipplanes[i], capView);
                if (!marked)
                    continue;
                CapVertex verts[4];
                buildCapQuad(mat.clipplanes[i], bmin, bmax, projMat,
                             view->width, verts);
                // The cap of one plane is clipped by the remaining
                // planes; concave mode leaves it unclipped (GL's
                // clip state there).
                float others[Render::Material::MaxClipPlanes][4];
                int numother = 0;
                if (!mat.clipconcave) {
                    for (int j = 0; j < mat.numclipplanes; ++j) {
                        if (j != i)
                            std::memcpy(others[numother++],
                                        mat.clipplanes[j],
                                        sizeof(others[0]));
                    }
                }
                view->submitCapQuad(verts, color, others, numother,
                                    secconf.hatchEnable
                                        && hatchTex != nullptr,
                                    bucket == 1, capView);
                view->submitCapCleanup(verts, capView);

                // The same cap into the depth+normal prepass, so the
                // passes that read it stop shading what the cap hides.
                // Opaque only, matching the scene's own prepass feed
                // (transparent geometry neither occludes nor receives).
                // Its parity has to be marked again: the prepass target
                // carries its own stencil.
                const uint16_t preView = BGFXView::ViewAOPrepassCap;
                if (bucket == 0 && view->passLive(preView)) {
                    bool premarked = false;
                    for (size_t k = head; k < tail; ++k)
                        premarked |= view->submitCapMark(
                            *list[k], mat.clipplanes[i], preView);
                    if (premarked) {
                        view->submitCapPrepass(verts, others, numother,
                                               preView);
                        view->submitCapCleanup(verts, preView);
                    }
                }
            }
            head = tail;
        }
    }
}
