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
#include "MaterialXSupport.h"
#include "Vg2D.h"

extern "C" int _main_(int, char**) {
    return 0;
}

// Vertex-layout static members (declared in BGFXRendererP.h).
bgfx::VertexLayout SceneVertex::ms_layout;
bool SceneVertex::ms_initialized = false;
bgfx::VertexLayout ColorVertex::ms_layout;
bool ColorVertex::ms_initialized = false;
bgfx::VertexLayout MatVertex::ms_layout;
bool MatVertex::ms_initialized = false;
bgfx::VertexLayout TransientVertex::ms_layout;
bool TransientVertex::ms_initialized = false;
bgfx::VertexLayout PointVertex::ms_layout;
bool PointVertex::ms_initialized = false;
bgfx::VertexLayout TexCoordVertex::ms_layout;
bool TexCoordVertex::ms_initialized = false;
bgfx::VertexLayout LineQuadVertex::ms_layout;
bgfx::VertexLayout LineQuadVertex::ms_instLayout;
bgfx::VertexLayout LineQuadVertex::ms_pointInstLayout;
bool LineQuadVertex::ms_initialized = false;
bgfx::VertexLayout CapVertex::ms_layout;
bool CapVertex::ms_initialized = false;

namespace Render {
BGFXRendererLibP _BGFXLib;
BGFXRendererLib BGFXLib;
} // namespace Render

BGFXRenderer::BGFXRenderer(QOpenGLWidget *widget, bool publishOnly)
    :pimpl(new Private(widget, publishOnly))
{
}

BGFXRenderer::~BGFXRenderer()
{
#ifndef FC_RENDERER_STANDALONE
    // Whoever publishes owns the stream for as long as it exists
    // (SceneServer.h, beginPublish); hand it back, or closing and
    // reopening a 3D view would leave it claimed by a renderer that is
    // gone and nothing would stream again.
    Render::SceneStreamServer::instance().endPublish(
            pimpl.get(), pimpl->publishGroup);
#endif
}

bool BGFXRenderer::render(const QColor &col,
                          const void * viewMatrix,
                          const void * projMatrix)
{
    // A pending frame dump may override the RenderDebug view mode for
    // just the captured frame (docs/RenderDebug.md §4.2) — swap it in
    // around the render so every consumer (the debug pass, the forced
    // SSAO chain) sees it, then restore.
    int savedMode = -1;
    if (pimpl->dumpPending && pimpl->pendingDump.mode >= 0
            && pimpl->pendingDump.mode != pimpl->debugconf.viewMode) {
        savedMode = pimpl->debugconf.viewMode;
        pimpl->debugconf.viewMode = pimpl->pendingDump.mode;
    }
    // The seam that makes the frame's biggest term visible. Everything
    // this renderer does is inside this call; bgfx's `cpuTimeFrame` is
    // the whole application frame. The difference is Coin's composite,
    // Qt and the app -- see docs/DrawSubmission.md phase 0 item 2, where
    // ~44ms of a 54.8ms frame had no instrument on it at all.
    const int64_t renderT0 = bx::getHPCounter();
    bool ok = pimpl->render(col, viewMatrix, projMatrix);
    if (pimpl->debugconf.frameTiming)
        pimpl->frameStats.renderMs += 1000.0
            * double(bx::getHPCounter() - renderT0)
            / double(bx::getHPFrequency());
    if (savedMode >= 0)
        pimpl->debugconf.viewMode = savedMode;
    return ok;
}

/// With a split layout up no sub is bank 0, so the implicit
/// full-canvas bank's targets -- a whole MSAA scene + AO + OIT set at
/// canvas size -- are dead weight. Release them (the same dance
/// dropSubView does); clearing the layout rebuilds through the
/// ordinary fresh-bank path, and the clear adopts a 3D cell's camera,
/// so the bank carries no state worth keeping either.
static void releaseBankZero(BGFXView *view,
                            const Render::Renderer::SubViewFrame *subs,
                            int count)
{
    for (int i = 0; i < count; ++i) {
        if (subs[i].id == 0)
            return;
    }
    const bool bank0Held = view->activeSub == 0
        ? bgfx::isValid(view->bgfxFbo)
        : (view->subBanks.count(0)
           && bgfx::isValid(view->subBanks.at(0).bgfxFbo));
    if (!bank0Held)
        return;
    const int park = subs[0].id;
    view->selectSubView(0);
    view->destroyTargets();
    _BGFXLib.releaseIds(view->viewId, view->viewSpan);
    view->selectSubView(park);
    view->subBanks.erase(0);
}

bool BGFXRenderer::renderSubViews(const QColor &col,
                                  const SubViewFrame *subs, int count)
{
#ifndef FC_RENDERER_STANDALONE
    // Desktop (docs/SplitViews.md sec 13): the host is ONE canvas
    // widget, and each sub-view submit is an ordinary desktop frame --
    // render into the bank's sized targets (the captureWidth override
    // is the sizing channel, exactly as renderOffscreen uses it), then
    // blit color + depth into the caller's bound framebuffer at the
    // sub-view rect. No wall-frame batching: there is no backbuffer
    // swap on this path, the blit IS the composition, and per-submit
    // frame boundaries reclaim destroys so fresh banks allocate
    // against a drained pool without a warm-up pass.
    if (!subs || count <= 0)
        return false;
    if (count == 1 && subs[0].id == 0)
        return render(col, subs[0].viewMatrix, subs[0].projMatrix);
    {
        auto vit0 = _BGFXLib.views.find(pimpl->widget);
        if (vit0 != _BGFXLib.views.end())
            releaseBankZero(vit0->second.get(), subs, count);
    }
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        const SubViewFrame &s = subs[i];
        if (s.width <= 0 || s.height <= 0) {
            ok = false;
            continue;
        }
        auto &ctx = pimpl->subCtx;
        ctx = {};
        ctx.active = true;
        ctx.first = (i == 0);
        ctx.last = (i == count - 1);
        ctx.id = s.id;
        ctx.x = s.x;
        ctx.y = s.y;
        ctx.w = s.width;
        ctx.h = s.height;
        ctx.style = s.drawStyle;
        ctx.styleName = s.drawStyleName;
        ctx.fromSuperset = s.styleFromSuperset;
        ctx.styleMode = s.drawStyleMode;
        ctx.styleOverrides = s.styleOverrides;
        _BGFXLib.captureWidth = uint16_t(s.width);
        _BGFXLib.captureHeight = uint16_t(s.height);
        ok = render(col, s.viewMatrix, s.projMatrix) && ok;
    }
    pimpl->subCtx = {};
    _BGFXLib.captureWidth = 0;
    _BGFXLib.captureHeight = 0;
    // As on the standalone side: every submit crossed a frame
    // boundary, so a bank that latched targetsFailed is owed its
    // one retry per wall frame.
    auto vit = _BGFXLib.views.find(pimpl->widget);
    if (vit != _BGFXLib.views.end()) {
        vit->second->targetsFailed = false;
        for (auto &b : vit->second->subBanks)
            b.second.targetsFailed = false;
    }
    return ok;
#else
    if (!subs || count <= 0)
        return false;
    if (count == 1 && subs[0].id == 0)
        return render(col, subs[0].viewMatrix, subs[0].projMatrix);
    auto vit0 = _BGFXLib.views.find(pimpl->widget);
    BGFXView *view =
        vit0 != _BGFXLib.views.end() ? vit0->second.get() : nullptr;
    if (view)
        releaseBankZero(view, subs, count);
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        const SubViewFrame &s = subs[i];
        if (s.width <= 0 || s.height <= 0) {
            ok = false;
            continue;
        }
        auto &ctx = pimpl->subCtx;
        ctx.active = true;
        ctx.first = (i == 0);
        ctx.last = (i == count - 1);
        ctx.id = s.id;
        ctx.x = s.x;
        ctx.y = s.y;
        ctx.w = s.width;
        ctx.h = s.height;
        ctx.style = s.drawStyle;
        ctx.styleName = s.drawStyleName;
        ctx.fromSuperset = s.styleFromSuperset;
        ctx.styleMode = s.drawStyleMode;
        ctx.styleOverrides = s.styleOverrides;
        _BGFXLib.standaloneSubWidth = uint16_t(s.width);
        _BGFXLib.standaloneSubHeight = uint16_t(s.height);
        const bool subOk = render(col, s.viewMatrix, s.projMatrix);
        ok = subOk && ok;
        // The frame boundary lives in the LAST submit; if that one
        // bailed before reaching it, cross it here so the earlier
        // sub-views' queued work (and any queued destroys) still
        // executes rather than piling into the next frame.
        if (ctx.last && !subOk)
            bgfx::frame();
    }
    pimpl->subCtx = {};
    _BGFXLib.standaloneSubWidth = 0;
    _BGFXLib.standaloneSubHeight = 0;
    // A bank whose init found the handle pool full latched
    // targetsFailed, and on the desktop nothing clears it until a
    // size or program change -- because a bailed single-view frame
    // never reaches bgfx::frame(). A multi-sub-view frame DOES (the
    // healthy siblings pump it, or the drain above), so the destroyed
    // handles are reclaimed every wall frame and one retry per frame
    // neither spins nor eats the pool it waits on.
    auto vit = _BGFXLib.views.find(pimpl->widget);
    if (vit != _BGFXLib.views.end()) {
        vit->second->targetsFailed = false;
        for (auto &b : vit->second->subBanks)
            b.second.targetsFailed = false;
    }
    return ok;
#endif
}

void BGFXRenderer::dropSubView(int id)
{
    if (id == 0)
        return;
    // The consumer registered under the sub-view goes with it: its
    // surface was bound to the bank's targets, and the host that
    // registered it is being released from the canvas.
    if (pimpl->consumerSlots.erase(id))
        pimpl->clearConsumer();
    auto it = _BGFXLib.views.find(pimpl->widget);
    if (it == _BGFXLib.views.end())
        return;
    BGFXView *view = it->second.get();
    if (view->activeSub != id && !view->subBanks.count(id))
        return;
    // Load the bank, take its targets and id block, and drop it. The
    // queued destroys execute at the next frame boundary.
    view->selectSubView(id);
    view->destroyTargets();
    _BGFXLib.releaseIds(view->viewId, view->viewSpan);
    view->selectSubView(0);
    view->subBanks.erase(id);
    view->subOvCaches.erase(id);
}

void BGFXRenderer::setMainViewStyle(uint8_t styleMask, uint8_t styleNameBit,
                                    bool fromSuperset,
                                    const StyleOverrideTable *overrides,
                                    uint16_t styleMode)
{
    pimpl->mainStyleMask = styleMask;
    pimpl->mainStyleName = styleNameBit;
    pimpl->mainFromSuperset = fromSuperset;
    pimpl->mainStyleOverrides = overrides;
    pimpl->mainStyleMode = styleMode;
}

void BGFXRenderer::setCaptureInterest(const CaptureInterestTable *table)
{
    pimpl->captureInterest = (table && !table->ids.empty()) ? table : nullptr;
}

void BGFXRenderer::prepareSubViews(const QColor &col,
                                   const SubViewFrame *subs, int count)
{
#ifndef FC_RENDERER_STANDALONE
    // Desktop submits each cross their own frame boundary, so fresh
    // banks already allocate against a reclaimed pool -- no warm-up
    // pass; only the full-canvas bank release applies.
    (void)col;
    if (!subs || count <= 0)
        return;
    auto it = _BGFXLib.views.find(pimpl->widget);
    if (it != _BGFXLib.views.end())
        releaseBankZero(it->second.get(), subs, count);
#else
    if (!subs || count <= 0)
        return;
    auto it = _BGFXLib.views.find(pimpl->widget);
    BGFXView *view =
        it != _BGFXLib.views.end() ? it->second.get() : nullptr;
    // No view yet means no first frame yet: that frame builds
    // everything from scratch anyway, with nothing to reclaim.
    if (!view)
        return;
    releaseBankZero(view, subs, count);
    // Warm every unseen id: a fresh bank allocates a full target set,
    // and stacked on the resident banks (plus whatever destroys the
    // layout change just queued -- released old-size targets, dropped
    // banks, bank 0 above) that create burst can exhaust the handle
    // pool mid-frame, latching targetsFailed and rendering the cell
    // black for its first frame ("sub-view N frame bailed"). A warm
    // submit runs the frame path only through target allocation
    // (subCtx.warm, with the in-frame retry available because nothing
    // of an on-screen frame is queued between frames), and the frame
    // boundary after each realizes its creates and reclaims the
    // queued destroys before the next bank allocates. These extra
    // frames draw nothing and, in the browser, never composite.
    for (int i = 0; i < count; ++i) {
        const SubViewFrame &s = subs[i];
        if (s.width <= 0 || s.height <= 0)
            continue;
        if (s.id == view->activeSub || view->subBanks.count(s.id))
            continue;
        auto &ctx = pimpl->subCtx;
        ctx = {};
        ctx.active = true;
        ctx.warm = true;
        ctx.id = s.id;
        ctx.x = s.x;
        ctx.y = s.y;
        ctx.w = s.width;
        ctx.h = s.height;
        ctx.style = s.drawStyle;
        ctx.styleName = s.drawStyleName;
        ctx.fromSuperset = s.styleFromSuperset;
        ctx.styleMode = s.drawStyleMode;
        ctx.styleOverrides = s.styleOverrides;
        _BGFXLib.standaloneSubWidth = uint16_t(s.width);
        _BGFXLib.standaloneSubHeight = uint16_t(s.height);
        pimpl->render(col, s.viewMatrix, s.projMatrix);
        bgfx::frame();
    }
    pimpl->subCtx = {};
    _BGFXLib.standaloneSubWidth = 0;
    _BGFXLib.standaloneSubHeight = 0;
#endif
}

bool BGFXRenderer::renderOffscreen(const QColor &col,
                                   const void *viewMatrix,
                                   const void *projMatrix,
                                   int width, int height)
{
#ifdef FC_RENDERER_STANDALONE
    (void)col; (void)viewMatrix; (void)projMatrix;
    (void)width; (void)height;
    return false;
#else
    if (width <= 0 || height <= 0)
        return false;
    // The view is sized from the widget every frame; while these are
    // set it is sized from them instead, so the capture renders at its
    // own resolution rather than scaling the widget's. The next
    // on-screen frame sees the mismatch and sizes the view back.
    _BGFXLib.captureWidth = uint16_t(width);
    _BGFXLib.captureHeight = uint16_t(height);
    bool ok = false;
    if (pimpl->captureSceneActive)
        ok = renderSwappedScene(Render::DrawCallList(pimpl->captureScene),
                                col, viewMatrix, projMatrix);
    else if (pimpl->captureFilter)
        ok = renderFiltered(col, viewMatrix, projMatrix);
    else
        ok = render(col, viewMatrix, projMatrix);
    _BGFXLib.captureWidth = 0;
    _BGFXLib.captureHeight = 0;
    // TEMPORARY (dots investigation).
    if (getenv("FC_DOTS_DUMP"))
        Base::Console().Message("DOTS renderOffscreen %dx%d ok=%d\n",
                                width, height, int(ok));
    return ok;
#endif
}

bool BGFXRenderer::setCaptureFilter(
        const std::vector<std::pair<std::string, std::string>> &objects)
{
#ifdef FC_RENDERER_STANDALONE
    (void)objects;
    return false;
#else
    pimpl->captureFilter = false;
    pimpl->captureKeys.clear();
    if (objects.empty())
        return false;
    // Resolve identities to objectKeys through the resident table. One
    // object owns any number of keys (one per producing node path).
    //
    // A named object matches anywhere on a key's CONTAINER CHAIN, not
    // just as its leaf: a source that is a group, an assembly or a
    // link owns no draws of its own, and the draws below it name the
    // leaf shape. The chain ends at the leaf, so this still matches a
    // plain object named directly.
    std::unordered_map<uint64_t, size_t> keyOwner;
    for (const auto &entry : pimpl->objectInfo) {
        for (size_t i = 0; i < objects.size(); ++i) {
            bool hit = entry.second.doc == objects[i].first
                    && entry.second.obj == objects[i].second;
            for (auto it = entry.second.path.begin();
                    !hit && it != entry.second.path.end(); ++it)
                hit = it->doc == objects[i].first
                    && it->obj == objects[i].second;
            if (hit) {
                keyOwner.emplace(entry.first, i);
                break;
            }
        }
    }
    if (keyOwner.empty())
        return false;
    // Every named object must have at least one draw in the RESIDENT
    // scene: the info table keeps residue for keys that left, and a
    // 3D-hidden object has no draws at all -- a capture that silently
    // omitted a source would register a wrong picture, so the caller
    // gets a refusal to fall back on instead.
    std::vector<char> present(objects.size(), 0);
    for (const auto &draw : pimpl->scene) {
        auto it = keyOwner.find(draw.objectKey);
        if (it != keyOwner.end())
            present[it->second] = 1;
    }
    for (char c : present)
        if (!c)
            return false;
    pimpl->captureKeys.reserve(keyOwner.size());
    for (const auto &entry : keyOwner)
        pimpl->captureKeys.insert(entry.first);
    pimpl->captureFilter = true;
    return true;
#endif
}

void BGFXRenderer::clearCaptureFilter()
{
    pimpl->captureFilter = false;
    pimpl->captureKeys.clear();
}

bool BGFXRenderer::setCaptureScene(DrawCallList &&draws)
{
#ifdef FC_RENDERER_STANDALONE
    (void)draws;
    return false;
#else
    pimpl->captureScene.clear();
    pimpl->captureSceneActive = false;
    if (draws.empty())
        return false;
    pimpl->captureScene = std::move(draws);
    pimpl->captureSceneActive = true;
    return true;
#endif
}

void BGFXRenderer::clearCaptureScene()
{
    pimpl->captureScene.clear();
    pimpl->captureSceneActive = false;
}

bool BGFXRenderer::shaderCompilePending() const
{
#ifdef FC_RENDERER_STANDALONE
    return false;
#else
    return !_BGFXLib.userShaderInflight.empty();
#endif
}

int BGFXRenderer::shaderCompileGeneration() const
{
#ifdef FC_RENDERER_STANDALONE
    return 0;
#else
    return _BGFXLib.userCompileGeneration;
#endif
}

#ifndef FC_RENDERER_STANDALONE
// The capture frame with the object filter applied: swap in a scene
// reduced to the filtered draws. drawListVersion is deliberately NOT
// bumped by the swap (renderSwappedScene): the mesh collector's
// keep-set stays the full scene's, so no resident buffer is freed
// behind the on-screen view by a capture.
bool BGFXRenderer::renderFiltered(const QColor &col,
                                  const void *viewMatrix,
                                  const void *projMatrix)
{
    auto &p = *pimpl;
    Render::DrawCallList filtered;
    filtered.reserve(p.scene.size());
    for (const auto &draw : p.scene)
        if (p.captureKeys.count(draw.objectKey))
            filtered.push_back(draw);
    return renderSwappedScene(std::move(filtered), col, viewMatrix, projMatrix);
}

// The shared capture-frame body: swap \a scene in for the resident
// feeds with the selection / preselection / overlay feeds stripped and
// a flat transparent background, render -- with settle frames first,
// so temporal accumulation converges on the capture camera before the
// frame that is read back -- then restore every feed. Works for both
// resident draws (the object filter) and freshly translated ones (the
// supplied capture scene): meshes the resident keep-set does not cover
// upload on demand at submission, like the highlight feed's, and
// TTL-collect once the capture stops drawing them.
bool BGFXRenderer::renderSwappedScene(Render::DrawCallList &&sceneDraws,
                                      const QColor &col,
                                      const void *viewMatrix,
                                      const void *projMatrix)
{
    auto &p = *pimpl;
    if (sceneDraws.empty())
        return false;

    auto savedScene = std::move(p.scene);
    p.scene = std::move(sceneDraws);
    auto savedSelections = std::move(p.selections);
    p.selections.clear();
    auto savedOverlays = std::move(p.overlays);
    p.overlays.clear();
    auto savedHighlight = std::move(p.highlight);
    p.highlight.clear();
    // hiddenKeys hides scene draws whose whole-object on-top selection
    // copies draw instead; those copies were just stripped.
    auto savedHidden = std::move(p.hiddenKeys);
    p.hiddenKeys.clear();
    const Render::Background savedBackground = p.background;
    p.background = Render::Background();
    p.background.fromColor = 0xFFFFFFFFu;// opaque white; the caller keys it
    // The viewer feeds its lights for the INTERACTIVE camera; under
    // the capture camera they can point anywhere. The default config
    // asks for the fixed camera-aligned headlight.
    const Render::ViewLightConfig savedLights = p.viewlightconf;
    p.viewlightconf = Render::ViewLightConfig();
    ++p.cullSceneVersion;
    p.buildInstanceGroups();
    p.sceneDirty = true;

    constexpr int kCaptureSettleFrames = 8;
    bool ok = false;
    for (int i = 0; i < kCaptureSettleFrames; ++i)
        ok = render(col, viewMatrix, projMatrix);

    p.scene = std::move(savedScene);
    p.selections = std::move(savedSelections);
    p.overlays = std::move(savedOverlays);
    p.highlight = std::move(savedHighlight);
    p.hiddenKeys = std::move(savedHidden);
    p.background = savedBackground;
    p.viewlightconf = savedLights;
    ++p.cullSceneVersion;
    p.buildInstanceGroups();
    p.sceneDirty = true;
    p.levelPlanner.markDirty();
    return ok;
}
#endif

bool BGFXRenderer::publish(const QColor &col,
                           const void *viewMatrix,
                           const void *projMatrix,
                           int width, int height)
{
#ifdef FC_RENDERER_STANDALONE
    // The standalone/wasm tier consumes snapshots; it does not make
    // them.
    (void)col; (void)viewMatrix; (void)projMatrix;
    (void)width; (void)height;
    return false;
#else
    if (!viewMatrix || !projMatrix || width <= 0 || height <= 0)
        return false;
    return pimpl->publishNoDraw(col, viewMatrix, projMatrix,
                                uint16_t(width), uint16_t(height));
#endif
}

void BGFXRenderer::setPublishGroup(const std::string &doc)
{
    pimpl->publishGroup = doc;
}

bool BGFXRenderer::requestFrameDump(const FrameDumpRequest &req)
{
#ifdef FC_RENDERER_STANDALONE
    // The standalone host owns the backbuffer and reads it back itself
    // (the wasm viewer's dumpFrame protocol).
    (void)req;
    return false;
#else
    pimpl->pendingDump = req;
    pimpl->dumpPending = true;
    pimpl->dumpHeld = false;
    // The capture needs a real frame, and needsRedraw() below reports
    // the pending dump for exactly that reason.
    //
    // ! It must NOT say the scene changed to get one. That claim is
    // read by everything asking "is this the same picture as last
    // frame" -- the idle temporal accumulation above all, which would
    // reset on the very frame being captured, so every capture of a
    // converged view returned the unconverged image and no instrument
    // could see the feature working at all.
    return true;
#endif
}

bool BGFXRenderer::frameDumpPending() const
{
    return pimpl->dumpPending;
}

void BGFXRenderer::holdFrameDump()
{
    pimpl->hostHold = true;
}

bool BGFXRenderer::frameDumpHeld() const
{
    return pimpl->dumpHeld;
}

bool BGFXRenderer::frameComplete() const
{
    return pimpl->lastFrameComplete;
}

uint64_t BGFXRenderer::renderedFrames() const
{
    return pimpl->renderedFrameCount;
}

uint64_t BGFXRenderer::completeFrames() const
{
    return pimpl->completeFrameCount;
}

bool BGFXRenderer::getRenderStats(RenderStats &stats) const
{
    stats = pimpl->lastStats;
    // Pool occupancy is read live rather than carried on lastStats:
    // the readback is per view and per captured frame, while these
    // are one process-wide set that every view contributes to. Taken
    // here they describe the pools as of the frame the caller just
    // pumped, which is what a caller asking "how much headroom is
    // left" means.
    if (const bgfx::Stats *s = _BGFXLib.deviceUp() ? bgfx::getStats() : nullptr) {
        stats.numFrameBuffers = s->numFrameBuffers;
        stats.numTextures = s->numTextures;
        stats.numViews = s->numViews;
        stats.textureMemory = s->textureMemoryUsed;
        stats.renderTargetMemory = s->rtMemoryUsed;
        stats.gpuMemoryUsed = s->gpuMemoryUsed;
        stats.gpuMemoryMax = s->gpuMemoryMax;
    }
    if (const bgfx::Caps *c = bgfx::getCaps()) {
        stats.maxFrameBuffers = int(c->limits.maxFrameBuffers);
        stats.maxTextures = int(c->limits.maxTextures);
        stats.maxViews = int(c->limits.maxViews);
    }
    return stats.valid;
}

bool BGFXRenderer::reloadShaders()
{
    // Every view re-inits (and reloads its programs from shaderPath())
    // at the top of its next render(); force that frame past the
    // idle skip.
    ++_BGFXLib.shaderGeneration;
    pimpl->sceneDirty = true;
    return true;
}

bool BGFXRenderer::releaseTargets()
{
    // A publish-only renderer never asked for a view, and asking would
    // be the one call that brings a graphics device into a process that
    // has none (Private::deinit says the same about removeView).
    if (pimpl->publishOnly)
        return false;
    auto it = _BGFXLib.views.find(pimpl->widget);
    if (it == _BGFXLib.views.end() || !it->second)
        return false;
    BGFXView *view = it->second.get();
    // Nothing built yet, or already given back: the rebuild condition
    // at the top of the next frame is exactly this test, so releasing
    // again would only queue a second round of destroys.
    if (!bgfx::isValid(view->bgfxFbo))
        return false;
    view->destroyTargets();

    // Execute the destroys rather than leaving them queued. bgfx::destroy
    // only writes a command; the memory comes back when a frame executes
    // the buffer -- and the whole point of this call is that this view is
    // about to stop producing frames. With one 3D view open and the user
    // on a spreadsheet tab there is no other frame to ride on either, so
    // an undrained release would give back nothing at all until the view
    // came back. Same context dance a frame uses, and the failed-init
    // drain above it: hand the widget's context back first, since
    // QOpenGLWidget::makeCurrent() binds its own framebuffer.
#ifdef FC_RENDERER_STANDALONE
    bgfx::frame();
#else
    pimpl->widget->doneCurrent();
    // makeCurrent() also flushes the GL framebuffer ids destroyTargets()
    // just handed to pendingRemoves.
    _BGFXLib.makeCurrent();
    bgfx::frame();
    // Not inside a paint: leave no context current rather than the
    // caller's, which there is none of here.
    _BGFXLib.doneCurrent();
#endif
    return true;
}

bool BGFXRenderer::animating() const
{
    // Animated content asks the viewer for another frame; worth asking
    // only if a frame reaches somebody (BGFXRendererP::localAudience).
    return pimpl->animatedFrame && pimpl->localAudience();
}

bool BGFXRenderer::boundBox(float &xmin, float &ymin, float &zmin,
                            float &xmax, float &ymax, float &zmax)
{
    if (!pimpl->bboxValid)
        return false;
    xmin = pimpl->bboxMin[0];
    ymin = pimpl->bboxMin[1];
    zmin = pimpl->bboxMin[2];
    xmax = pimpl->bboxMax[0];
    ymax = pimpl->bboxMax[1];
    zmax = pimpl->bboxMax[2];
    // The shadow ground quad is backend geometry outside the scene
    // draws; include it so the viewer's camera auto-clipping covers it
    // (the Coin GL ground used to do this through the scene graph).
    const Render::LightConfig &light = pimpl->lightconf;
    const float bmin[3] = {xmin, ymin, zmin};
    const float bmax[3] = {xmax, ymax, zmax};
    float corners[4][3];
    // The camera of the last frame, which is the one that laid this
    // quad out. It cannot feed back: a camera-fitted ground is sized
    // from the eye's DISTANCE to the plane and its field of view,
    // neither of which the near/far planes computed from these bounds
    // can move.
    if (light.groundQuad(bmin, bmax, pimpl->groundCam, corners)) {
        // Whatever the quad actually is -- explicitly sized, moved or
        // tilted -- rather than a second copy of the auto formula, which
        // would under-report the moment either differed.
        for (const auto &c : corners) {
            xmin = std::min(xmin, c[0]);
            xmax = std::max(xmax, c[0]);
            ymin = std::min(ymin, c[1]);
            ymax = std::max(ymax, c[1]);
            zmin = std::min(zmin, c[2]);
            zmax = std::max(zmax, c[2]);
        }
    }
    return true;
}

static void dumpFeed(const char *tag, int id,
                     const Render::DrawCallList &draws);

void BGFXRenderer::setScene(DrawCallList &&draws)
{
    dumpFeed("scene", 0, draws);
    pimpl->scene = std::move(draws);
    ++pimpl->drawListVersion;
    noteSceneStated();
    // The occlusion index is partitioned from this list, and a stale
    // partition would mask draws by the bounds of whatever used to
    // occupy those rows. Rebuilt on the next frame that culls, never
    // here: a publish that no view is culling should not pay for one.
    ++pimpl->cullSceneVersion;
    pimpl->buildInstanceGroups();
    pimpl->sceneDirty = true;
    pimpl->feedDirty = true;
    pimpl->updateBBox();
#ifndef FC_RENDERER_STANDALONE
    // New geometry may carry coarse-first sources the level plan has
    // not judged (§13 step 2): replan on the next settled frame even
    // if the camera never moves again.
    pimpl->levelPlanner.markDirty();
#endif
}

void BGFXRenderer::setObjectInfo(ObjectInfoMap &&info)
{
    pimpl->objectInfo = std::move(info);
    noteObjectInfoStated();
    pimpl->objectInfoStamp = objectInfoVersion();
    // Identity rides the published root's object entries; a change to
    // it alone (rename) only reaches viewers with the next publish.
    pimpl->feedDirty = true;
}

void BGFXRenderer::updateObjectInfo(ObjectInfoMap &&added)
{
    if (added.empty())
        return;
    if (pimpl->objectInfo.empty())
        pimpl->objectInfo = std::move(added);
    else {
        for (auto &entry : added)
            pimpl->objectInfo.insert(std::move(entry));
    }
    pimpl->feedDirty = true;
}

void BGFXRenderer::setObjectMeta(ObjectMetaMap &&meta)
{
    pimpl->objectMeta = std::move(meta);
    // A rename changes no geometry and no key, so nothing else marks the
    // feed dirty for it -- without this the new label would sit here
    // until something moved.
    pimpl->feedDirty = true;
}

void BGFXRenderer::updateObjectMeta(
        ObjectMetaMap &&changed,
        const std::vector<std::pair<std::string, std::string>> &removed)
{
    for (auto &doc : changed) {
        auto &byObject = pimpl->objectMeta[doc.first];
        for (auto &obj : doc.second)
            byObject[obj.first] = std::move(obj.second);
    }
    for (const auto &key : removed) {
        auto doc = pimpl->objectMeta.find(key.first);
        if (doc == pimpl->objectMeta.end())
            continue;
        doc->second.erase(key.second);
        // A document whose last object went takes its own entry with
        // it, so the table cannot accumulate empty documents across a
        // session of opening and closing files.
        if (doc->second.empty())
            pimpl->objectMeta.erase(doc);
    }
    pimpl->feedDirty = true;
}

void BGFXRenderer::setBackground(const Background &bg)
{
    pimpl->background = bg;
}

static void dumpFeed(const char *tag, int id, const Render::DrawCallList &draws)
{
    if (!getenv("FC_BGFX_DEBUG_FEED"))
        return;
    fprintf(stderr, "bgfx feed %s id=%d: %zu draws\n", tag, id, draws.size());
    for (const auto &d : draws) {
        const auto &m = d.material;
        fprintf(stderr,
                "  cache=%llx type=%d part=%d range=%d+%d diffuse=%08x emissive=%08x"
                " pvc=%d light=%d transp=%d ontop=%d dtest=%d dwrite=%d"
                " dfunc=%d lw=%.1f po=%d/%.1f/%.1f hla=%.2f lp=%08x/%08x"
                " ol=%d lc=%08x tex=%d bump=%d em=%d occ=%d mr=%d uv=%d"
                " ss=%d water=%d\n",
                d.mesh ? (unsigned long long)d.mesh->cacheId : 0ull,
                m.type, d.partIndex, d.indexStart, d.indexCount,
                m.diffuse, m.emissive, m.pervertexcolor, m.lighting,
                m.transparent, m.ontop, m.depthtest, m.depthwrite,
                m.depthfunc, m.linewidth, m.polygonoffset,
                m.polygonoffsetfactor, m.polygonoffsetunits,
                m.hiddenlinealpha, m.linepattern, m.hiddenlinepattern,
                m.outline, m.linecolor,
                m.texture ? m.texture->numComponents : 0,
                m.bumpmap ? m.bumpmap->numComponents : 0,
                m.emissivemap ? m.emissivemap->numComponents : 0,
                m.occlusionmap ? m.occlusionmap->numComponents : 0,
                m.metallicroughnessmap
                    ? m.metallicroughnessmap->numComponents : 0,
                d.mesh && d.mesh->texCoords ? 1 : 0,
                m.shadowstyle, m.water);
        for (int i = 0; i < m.numclipplanes; ++i)
            fprintf(stderr, "  clip%s %d: %g,%g,%g,%g\n",
                    m.clipconcave ? " (concave)" : "", i,
                    m.clipplanes[i][0], m.clipplanes[i][1],
                    m.clipplanes[i][2], m.clipplanes[i][3]);
    }
}

void BGFXRenderer::addSelection(int id, DrawCallList &&draws)
{
    dumpFeed("sel", id, draws);
    pimpl->selections[id] = std::move(draws);
    pimpl->sceneDirty = true;
}

void BGFXRenderer::removeSelection(int id)
{
    if (pimpl->selections.erase(id))
        pimpl->sceneDirty = true;
}

void BGFXRenderer::setOverlay(int id, DrawCallList &&draws,
                              const OverlayAnchor &anchor)
{
    dumpFeed("overlay", id, draws);
    if (draws.empty()) {
        removeOverlay(id);
        return;
    }
    auto &feed = pimpl->overlays[id];
    feed.draws = std::move(draws);
    feed.anchor = anchor;
    pimpl->sceneDirty = true;
}

void BGFXRenderer::removeOverlay(int id)
{
    if (pimpl->overlays.erase(id))
        pimpl->sceneDirty = true;
}

void BGFXRenderer::setFrameConsumer(FrameConsumer *consumer, int subView)
{
    // The resolved copy may name the slot being replaced; the next
    // submit resolves its own.
    pimpl->clearConsumer();
    auto it = pimpl->consumerSlots.find(subView);
    if (it != pimpl->consumerSlots.end()) {
        it->second.consumer = nullptr;
        it->second.surface.reset();
        it->second.passes = 0;
        it->second.overlayPasses = 0;
        if (!consumer && !it->second.externalBase)
            pimpl->consumerSlots.erase(it);
    }
    if (!consumer)
        return;
    const unsigned want = consumer->framePasses();
    const unsigned overlay = consumer->overlayPasses();
    // Refused, not clamped: a clamp would leave the consumer's last
    // passes submitting into whatever view id follows a block, which
    // is another part of this very frame. An overlay count past the
    // total is a consumer bug and refused the same way.
    auto surface = overlay <= want
        ? fcBGFXCreateHostSurface(want - overlay, overlay) : nullptr;
    if (!surface) {
        RENDER_ERR("frame consumer wants " << want << " passes ("
                   << overlay << " overlay); a host frame offers "
                   << int(BGFXView::NumConsumerSceneViews) << " scene + "
                   << int(BGFXView::NumConsumerOverlayViews)
                   << " overlay and the device must be up. It is not"
                   " attached.");
        return;
    }
    auto &slot = pimpl->consumerSlots[subView];
    slot.surface = std::move(surface);
    slot.passes = want;
    slot.overlayPasses = overlay;
    slot.consumer = consumer;
}

DrawSurface *BGFXRenderer::frameConsumerSurface(int subView)
{
    auto it = pimpl->consumerSlots.find(subView);
    if (it == pimpl->consumerSlots.end() || !it->second.surface)
        return nullptr;
    return &it->second.surface->surface();
}

void BGFXRenderer::setHighlight(DrawCallList &&draws, bool wholeOnTop)
{
    dumpFeed("hl", wholeOnTop, draws);
    pimpl->highlight = std::move(draws);
    pimpl->hlWholeOnTop = wholeOnTop;
    pimpl->sceneDirty = true;
}

void BGFXRenderer::clearHighlight()
{
    if (!pimpl->highlight.empty())
        pimpl->sceneDirty = true;
    pimpl->highlight.clear();
    pimpl->hlWholeOnTop = false;
}

void BGFXRenderer::setHiddenLineConfig(const HiddenLineConfig &config)
{
    if (pimpl->hlconfig != config) {
        pimpl->hlconfig = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setExternalBaseLayer(bool on, int subView)
{
    auto it = pimpl->consumerSlots.find(subView);
    if (it == pimpl->consumerSlots.end()) {
        if (!on)
            return;
        it = pimpl->consumerSlots.emplace(subView, Private::ConsumerSlot()).first;
    }
    if (it->second.externalBase == on)
        return;
    it->second.externalBase = on;
    pimpl->sceneDirty = true;
    if (!on && !it->second.consumer)
        pimpl->consumerSlots.erase(it);
    pimpl->clearConsumer();
}

void BGFXRenderer::setSectionConfig(const SectionConfig &config)
{
    if (pimpl->secconf != config) {
        pimpl->secconf = config;
        pimpl->sceneDirty = true;
    }
}

bool BGFXRenderer::isSceneAnimated() const
{
    return pimpl->sceneAnimated;
}

bool BGFXRenderer::isSceneDirty() const
{
#ifndef FC_RENDERER_STANDALONE
    // A finished async user-shader compile must wake idle-skip clients
    // so the pass appears (or the error fallback settles) without user
    // interaction; render() re-snapshots the generation.
    if (pimpl->userShaderGen != _BGFXLib.userCompileGeneration)
        return true;
#endif
    return pimpl->sceneDirty;
}

void BGFXRenderer::setAOConfig(const AOConfig &config)
{
    if (pimpl->aoconf != config) {
        pimpl->aoconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setCavityConfig(const CavityConfig &config)
{
    if (pimpl->cavityconf != config) {
        pimpl->cavityconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setMatcapConfig(const MatcapConfig &config)
{
    if (pimpl->matcapconf != config) {
        pimpl->matcapconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setPreselConfig(const PreselHighlightConfig &config)
{
    if (pimpl->preselconf != config) {
        pimpl->preselconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setSelConfig(const PreselHighlightConfig &config)
{
    if (pimpl->selconf != config) {
        pimpl->selconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setPBRConfig(const PBRConfig &config)
{
    if (pimpl->pbrconf != config) {
        pimpl->pbrconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setBumpConfig(const BumpConfig &config)
{
    if (pimpl->bumpconf != config) {
        pimpl->bumpconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setLightConfig(const LightConfig &config)
{
    if (pimpl->lightconf != config) {
        pimpl->lightconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setViewLightConfig(const ViewLightConfig &config)
{
    if (pimpl->viewlightconf != config) {
        pimpl->viewlightconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setVolumetricConfig(const VolumetricConfig &config)
{
    if (pimpl->volconf != config) {
        pimpl->volconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setWaterConfig(const WaterConfig &config)
{
    if (pimpl->waterconf != config) {
        pimpl->waterconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setBloomConfig(const BloomConfig &config)
{
    if (pimpl->bloomconf != config) {
        pimpl->bloomconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setTemporalConfig(const TemporalConfig &config)
{
    if (pimpl->tempconf != config) {
        pimpl->tempconf = config;
        // Marking the scene dirty is what discards the accumulation:
        // a frame that changes how the refinement works must not be
        // averaged into the refinement it replaces.
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setOutputConfig(const OutputConfig &config)
{
    if (pimpl->outconf != config) {
        pimpl->outconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setRenderDebugConfig(const RenderDebugConfig &config)
{
    if (pimpl->debugconf != config) {
        pimpl->debugconf = config;
        pimpl->sceneDirty = true;
    }
}

void BGFXRenderer::setOcclusionCullConfig(const OcclusionCullConfig &config)
{
    if (pimpl->cullconf != config) {
        const bool wasEnabled = pimpl->cullconf.enabled;
        pimpl->cullconf = config;
        pimpl->sceneDirty = true;
        // Turning culling off has to give the masked geometry back, and
        // the mask is rebuilt per frame -- so nothing to undo. Turning it
        // on (or changing what a verdict means) starts from no
        // knowledge rather than from verdicts taken under other rules.
        if (!config.enabled || !wasEnabled)
            pimpl->culler.clear();
    }
}

void BGFXRenderer::setUserShaderConfig(const UserShaderConfig &config)
{
    if (pimpl->usershaderconf == config)
        return;
    pimpl->usershaderconf = config;
    pimpl->sceneDirty = true;
    for (const auto &s : config.shaders) {
        if (s.stage != "post") {
            static std::set<std::string> warned;
            if (warned.insert(s.stage).second)
                RENDER_WARN("user shader stage '" << s.stage.c_str()
                            << "' not supported by this backend");
        }
    }
}

void BGFXRenderer::setAutoZoomScale(float scale)
{
    if (pimpl->autozoomScale == scale)
        return;
    pimpl->autozoomScale = scale;
    // Only autozoom draws depend on the scale; they converge one frame
    // late on camera changes like the rest of the feed.
    auto hasAutoZoom = [](const Render::DrawCallList &draws) {
        for (const auto &draw : draws) {
            if (!draw.material.autozoom.empty())
                return true;
        }
        return false;
    };
    if (hasAutoZoom(pimpl->scene) || hasAutoZoom(pimpl->highlight)) {
        pimpl->sceneDirty = true;
        return;
    }
    for (const auto &sel : pimpl->selections) {
        if (hasAutoZoom(sel.second)) {
            pimpl->sceneDirty = true;
            return;
        }
    }
}

void BGFXRenderer::setHatchImage(const void *data, int nc,
                                 int width, int height)
{
    int curWidth = pimpl->hatchTex ? pimpl->hatchTex->width : 0;
    int curHeight = pimpl->hatchTex ? pimpl->hatchTex->height : 0;
    if (data == pimpl->hatchKey && width == curWidth
            && height == curHeight)
        return;
    pimpl->hatchKey = data;
    pimpl->hatchTex.reset();
    if (data && nc > 0 && width > 0 && height > 0) {
        // Expand to RGBA8 (the image comes as tightly packed
        // nc-component rows; 1/2 components are luminance(+alpha)).
        auto tex = std::make_shared<Render::TextureImage>();
        tex->width = width;
        tex->height = height;
        tex->numComponents = 4;
        const uint8_t *src = static_cast<const uint8_t *>(data);
        tex->pixels.resize(size_t(width) * height * 4);
        uint8_t *dst = tex->pixels.data();
        for (size_t i = 0, n = size_t(width) * height; i < n; ++i) {
            const uint8_t *p = src + i * nc;
            switch (nc) {
            case 1: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = 255; break;
            case 2: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = p[1]; break;
            case 3: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                    dst[3] = 255; break;
            default: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                     dst[3] = p[3]; break;
            }
            dst += 4;
        }
        pimpl->hatchTex = std::move(tex);
    }
    ++pimpl->hatchVersion;
    pimpl->sceneDirty = true;
}

bool BGFXRenderer::needsRedraw() const
{
    // A pending capture needs a frame as much as a changed scene does,
    // and says so in its own right rather than by pretending the scene
    // moved. Self-clearing: the flag is dropped once the frame that
    // served it has been read back.
    return pimpl->sceneDirty || pimpl->dumpPending;
}

bool BGFXRenderer::canSkipInternal() const
{
    return pimpl->renderOk && pimpl->hasScene && !pimpl->_deinit;
}

const std::string &BGFXRenderer::type() const
{
    return pimpl->typeName;
}

std::string BGFXRenderer::deviceName() const
{
    // Resolved once at bgfx::init and process-wide, so it is the
    // library's rather than this renderer's -- and it is empty until a
    // device exists, which is the honest answer for a publish-only
    // renderer that never creates one.
    return _BGFXLib.deviceName;
}

#ifdef FC_RENDERER_STANDALONE
void BGFXRenderer::setWindowHandle(void *handle)
{
    _BGFXLib.windowHandle = handle;
}

void BGFXRenderer::setWindowSize(int width, int height)
{
    if (width > 0)
        _BGFXLib.standaloneWidth = uint16_t(width);
    if (height > 0)
        _BGFXLib.standaloneHeight = uint16_t(height);
}
#endif

void BGFXRenderer::setMSAASamples(int samples)
{
    const int s = samples < 2 ? 0 : samples;
#ifdef FC_RENDERER_STANDALONE
    _BGFXLib.standaloneSamples = s;
#else
    _BGFXLib.desktopSamples = s;
#endif
}

void BGFXRenderer::setEffectResolution(float scale)
{
    _BGFXLib.effectResolution = std::min(std::max(scale, 0.25f), 1.0f);
}

void BGFXRenderer::setSSAOResolution(float scale)
{
    _BGFXLib.ssaoResolution = std::min(std::max(scale, 0.25f), 1.0f);
}

// Outside the guard below: the narration it turns on is cross-tier and
// the browser has no environment variable to reach it by. See the
// declaration for what walling this one setter off cost.
void BGFXRenderer::setLevelDebug(bool on)
{
    pimpl->levelDebugOn = on;
}

#ifndef FC_RENDERER_STANDALONE
void BGFXRenderer::setLevelTolerance(float px)
{
    pimpl->levelPlanner.setTolerance(px);
}

bool BGFXRenderer::drivesMeshLevels() const
{
    return true;
}

void BGFXRenderer::setGpuMemoryBudget(size_t bytes)
{
    if (pimpl->gpuBudget == bytes)
        return;
    pimpl->gpuBudget = bytes;
#ifndef FC_RENDERER_STANDALONE
    // A changed budget is a new question and the plan must be made to
    // ask it. It used to be woken by its own boundary dither -- over
    // budget, a sweep, a markDirty, forever -- and the deadband
    // removed exactly that: a live budget drop on a still camera then
    // slept unnoticed for a whole 600s measurement window.
    pimpl->levelPlanner.markDirty();
#endif
}

void BGFXRenderer::setLevelPressureRelease(float fraction)
{
    pimpl->levelPressureReleaseFrac = fraction;
}

void BGFXRenderer::setDowngradeLedger(bool on)
{
    pimpl->downgradeLedgerOn = on;
}

void BGFXRenderer::setClimbAdmission(bool hardLimit, int batch)
{
    pimpl->climbHardLimitOn = hardLimit;
    pimpl->climbAdmitBatch = batch > 0 ? batch : 1;
}

void BGFXRenderer::setDescentOrderBatch(int batch)
{
    pimpl->descentOrderBatch = batch > 0 ? batch : 0;
}

void BGFXRenderer::setLevelBudgetDeadband(float fraction)
{
    const float band = fraction > 0.0f ? fraction : 0.0f;
    if (pimpl->levelBudgetDeadband == band)
        return;
    pimpl->levelBudgetDeadband = band;
    // Same staleness as the budget: narrowing the band can put the
    // standing total outside it, and only a plan pass can act on that.
    pimpl->levelPlanner.markDirty();
}
#endif

void BGFXRenderer::setTinyElementCutoff(int prims)
{
    pimpl->tinyElementCutoff = prims > 0 ? prims : 0;
}

void BGFXRenderer::setElementGates(bool shapeVertices, bool pressureEdges,
                                   bool loadingDrop, int staggerFrames)
{
    pimpl->shapeVerticesOn = shapeVertices;
    pimpl->pressureDropEdges = pressureEdges;
    pimpl->loadDropElements = loadingDrop;
    pimpl->elemGateStagger = staggerFrames > 0 ? staggerFrames : 1;
}

//////////////////////////////////////////////////////////////////////

BGFXRendererLib::BGFXRendererLib()
{
    RendererFactory::registerLib(this);
}

const std::string &BGFXRendererLib::name() const
{
    return _BGFXLib.name;
}

const std::vector<std::string> &BGFXRendererLib::types() const
{
    return _BGFXLib.types;
}

bool BGFXRendererLib::warmup(const AdoptedDevice &device,
                             const std::string &type,
                             WarmupTiming *timing)
{
#ifdef FC_RENDERER_STANDALONE
    // The standalone viewer owns its own canvas and its own device;
    // there is no Qt here to adopt one from.
    (void)device;
    (void)type;
    (void)timing;
    return false;
#else
    auto it = _BGFXLib.typeMap.find(type);
    if (it == _BGFXLib.typeMap.end() || !device.valid())
        return false;
    // The backend the session asked for and the API Qt's device
    // actually is have to be the same thing. They are configured
    // independently -- Render_Type is a preference, the RHI backend is
    // a platform default -- so this is a real mismatch to catch and not
    // a tautology. Adopting across it would hand bgfx's Metal backend a
    // VkDevice.
    static const struct { AdoptedDevice::Api api; RendererType::Enum bgfx; }
    kPairs[] = {
        { AdoptedDevice::Metal,  RendererType::Metal },
        { AdoptedDevice::Vulkan, RendererType::Vulkan },
        { AdoptedDevice::D3D11,  RendererType::Direct3D11 },
        { AdoptedDevice::D3D12,  RendererType::Direct3D12 },
    };
    bool paired = false;
    for (const auto &p : kPairs)
        if (p.api == device.api && p.bgfx == it->second)
            paired = true;
    if (!paired) {
        RENDER_ERR("cannot adopt a " << device.apiName()
                   << " device for '" << type
                   << "'; the backend and the device must be the same API");
        return false;
    }
    QElapsedTimer clock;
    clock.start();
    // The device only. The Qt GL context, the anchor view and the
    // shader programs are NOT built here and are not skipped either --
    // the widget warm-up runs straight after this one, finds the device
    // already up, and does exactly its remaining half. Splitting it
    // this way is what lets Route D change WHOSE device the session
    // runs on without touching the startup ordering that was already
    // right (docs/DeviceAdoption.md section 4, constraint 2).
    if (!_BGFXLib.prepareAdopted(device, it->second))
        return false;
    if (timing) {
        timing->context = 0;
        timing->device = _BGFXLib.msDevice;
        timing->programs = 0;
        timing->flush = 0;
        timing->total = timing->device;
    }
    return true;
#endif
}

bool BGFXRendererLib::warmup(QOpenGLWidget *widget, const std::string &type,
                             WarmupTiming *timing)
{
    auto it = _BGFXLib.typeMap.find(type);
    if (it == _BGFXLib.typeMap.end() || !widget)
        return false;
    QElapsedTimer clock;
    clock.start();
    // Everything one-time lives in prepare(): the GL context bgfx draws
    // through, its offscreen surface, and bgfx::init itself. The view
    // that getView() adds on top is per widget and cheap, and the
    // shader programs are built on demand by whatever first draws with
    // them -- so this is the share of the first 3D view that can be
    // paid in advance, not all of it.
    //
    // ⚠️ Main thread only, and not by accident: prepare() calls
    // bgfx::renderFrame() before bgfx::init(), which is bgfx's
    // documented way of asking for single-threaded mode, and the GL
    // context it hands over belongs to the thread that made it current.
    // There is no worker thread to move this to without changing that
    // decision.
    if (!_BGFXLib.prepare(widget, it->second))
        return false;
    // ⚠️ Read now, not after getView(): getView() calls prepare() too,
    // and prepare() zeroes them on entry -- read late and the context
    // and device phases both report 0 while their time is charged to
    // the programs.
    const double msContext = _BGFXLib.msContext;
    const double msDevice = _BGFXLib.msDevice;

    // The shader programs, through the view's own init() rather than a
    // second list of program names to keep in step with it. init()
    // loads every stock program and builds the render targets, and
    // bgfx only hands that work to the driver when a frame is
    // submitted -- so a frame follows, or nothing would be compiled and
    // the cost would simply move to the first real one.
    //
    // ⚠️ The warm view is kept, not dropped. removeView() of the last
    // view calls shutdown(), so dropping it tears the device down again
    // and warms nothing -- it also freed the context out from under the
    // doneCurrent() below, which is how this was found. Keeping it
    // makes it the process's anchor for the device: bgfx now stays up
    // from here until the application quits, rather than going away
    // whenever the last 3D view closes and being rebuilt for the next
    // one. Its cost is a 1x1 view's render targets and one set of
    // programs.
    if (auto view = _BGFXLib.getView(widget, it->second)) {
        _BGFXLib.makeCurrent();
        view->init();
        double msPrograms = clock.nsecsElapsed() / 1.0e6
            - msContext - msDevice;
        clock.restart();
        bgfx::frame();
        double msFlush = clock.nsecsElapsed() / 1.0e6;
        _BGFXLib.doneCurrent();
        if (timing) {
            timing->context = msContext;
            timing->device = msDevice;
            timing->programs = msPrograms;
            timing->flush = msFlush;
            timing->total = timing->context + timing->device
                + timing->programs + timing->flush;
        }
    }
    else if (timing) {
        timing->context = msContext;
        timing->device = msDevice;
        timing->total = timing->context + timing->device;
    }
    return true;
}

bool BGFXRendererLib::deviceSharesQtGL() const
{
#ifdef FC_RENDERER_STANDALONE
    return false;
#else
    // currentType is OpenGL only when prepare() took the GL path (the
    // context handed to bgfx is the Qt one built against the global
    // share context); a positive view limit is the proof some init
    // actually succeeded -- getCaps() is the zeroed global until then.
    // getRendererType() is the running device's own answer: prepare()
    // leaves currentType set when its init lost the race to a device
    // somebody else (Page2D's headless Vulkan) already brought up, and
    // trusting currentType alone would hand a Vulkan handle to a GL
    // compositor.
    return _BGFXLib.currentType == RendererType::OpenGL
        && _BGFXLib.context != nullptr
        && QOpenGLContext::globalShareContext() != nullptr
        && bgfx::getCaps()->limits.maxViews > 0
        && bgfx::getRendererType() == bgfx::RendererType::OpenGL;
#endif
}

bool BGFXRendererLib::deviceMakeCurrent()
{
#ifdef FC_RENDERER_STANDALONE
    return false;
#else
    if (!deviceSharesQtGL())
        return false;
    _BGFXLib.makeCurrent();
    return QOpenGLContext::currentContext() == _BGFXLib.context.get();
#endif
}

void BGFXRendererLib::deviceDoneCurrent()
{
#ifndef FC_RENDERER_STANDALONE
    if (_BGFXLib.context)
        _BGFXLib.doneCurrent();
#endif
}

DrawDevice *BGFXRendererLib::drawDevice() const
{
    // Null until the device is up (prepare() ran): the facade hands
    // out resources bgfx must exist to create. The consumer treats
    // null as "not yet" and asks again -- warmup or the first 3D view
    // flips it, and the device then stays up until the app quits.
    // The browser tier builds the facade too since the streamed
    // frame's blit is a consumer (docs/CyclesIntegration.md sec 7.1).
    if (_BGFXLib.currentType == RendererType::Noop)
        return nullptr;
    return fcBGFXDrawDevice();
}

std::unique_ptr<Renderer> BGFXRendererLib::create(
        const std::string &type, QOpenGLWidget *widget,
        bool publishOnly) const
{
    std::unique_ptr<Renderer> res;
    auto it = _BGFXLib.typeMap.find(type);
    if (it == _BGFXLib.typeMap.end()) {
        RENDER_WARN("Unsupported renderer type " << type.c_str());
        return res;
    }
    // A publish-only renderer must not disturb the device state, and
    // must not be what decides which backend the process will use: it
    // never creates one (docs/HeadlessServe.md §3.1). The type it is
    // asked for survives only as a label on the snapshot's shader
    // variants, which shipUserShader() compiles offline for the
    // viewer tiers regardless of what runs here.
    if (publishOnly) {
        auto renderer = new BGFXRenderer(widget, true);
        res.reset(renderer);
        renderer->pimpl->typeName = it->first;
        renderer->pimpl->type = it->second;
        return res;
    }
    if (_BGFXLib.currentType != it->second) {
        for (auto renderer : _BGFXLib.renderers) {
            // A publish-only renderer owns no device state, so the
            // backend switch has nothing of its to tear down -- and
            // deinit() is permanent: it would leave a served document
            // publishing into a dead renderer with no diagnostic.
            if (renderer->publishOnly)
                continue;
            if (renderer->type != it->second)
                renderer->deinit();
        }
        _BGFXLib.shutdown();
    }
    auto renderer = new BGFXRenderer(widget);
    res.reset(renderer);
    renderer->pimpl->typeName = it->first;
    renderer->pimpl->type = it->second;
    return res;
}

/////////////////////////////////////////////////////////
void BGFXRendererLibP::releaseIds(uint16_t &id, uint16_t &span)
{
    if (!span)
        return;
    const int first = id / kIdGranule;
    const int count = span / kIdGranule;
    for (int g = first; g < first + count && g < int(granules.size()); ++g)
        granules[g] = 0;
    id = 0;
    span = 0;
}

bool BGFXRendererLibP::reserveIds(uint16_t &id, uint16_t &span,
                                  uint16_t need)
{
    if (granules.empty()) {
        const uint32_t maxViews = bgfx::getCaps()->limits.maxViews;
        granules.assign(maxViews / kIdGranule, 0);
        // The top granule belongs to Page2D::renderOffscreen, which
        // draws on the fixed id pair just under the ceiling; a viewer
        // block landing there would have its draws redirected into the
        // page's framebuffer.
        if (!granules.empty())
            granules.back() = 1;
    }
    if (span >= need)
        return true;
    const uint16_t oldId = id;
    const uint16_t oldSpan = span;
    releaseIds(id, span);
    const int want = (need + kIdGranule - 1) / kIdGranule;
    for (int g = 0; g + want <= int(granules.size()); ++g) {
        int run = 0;
        while (run < want && !granules[g + run])
            ++run;
        if (run < want) {
            g += run;   // the taken granule cannot start a run either
            continue;
        }
        for (int i = 0; i < want; ++i)
            granules[g + i] = 1;
        id = uint16_t(g * kIdGranule);
        span = uint16_t(want * kIdGranule);
        return true;
    }
    // No room. Take the old block back (it was just freed, so this
    // cannot fail) and let the caller sit this frame out.
    for (int g = oldId / kIdGranule;
         g < (oldId + oldSpan) / kIdGranule; ++g)
        granules[g] = 1;
    id = oldId;
    span = oldSpan;
    return false;
}

void BGFXRendererLibP::releaseBlock(BGFXView *view)
{
    releaseIds(view->viewId, view->viewSpan);
    // Inactive sub-view banks hold id blocks of their own
    // (docs/SplitViews.md sec 9.2).
    for (auto &v : view->subBanks)
        releaseIds(v.second.viewId, v.second.viewSpan);
}

bool BGFXRendererLibP::reserveBlock(BGFXView *view, uint16_t need)
{
    const uint16_t hadSpan = view->viewSpan;
    if (!reserveIds(view->viewId, view->viewSpan, need))
        return false;
    if (view->viewSpan != hadSpan && getenv("FC_BGFX_DEBUG_VIEWS")) {
        int taken = 0;
        for (uint8_t u : granules)
            taken += u;
        fprintf(stderr,
                "bgfx view ids: viewer %p needs %d, block %d..%d;"
                " %d of %d ids held by %d viewer(s)\n",
                (void *)view->widget, int(need), int(view->viewId),
                int(view->viewId + view->viewSpan - 1),
                taken * kIdGranule, int(granules.size()) * kIdGranule,
                int(views.size()));
    }
    return true;
}

/////////////////////////////////////////////////////////
void BGFXRendererLibP::removeView(QOpenGLWidget *widget)
{
    auto it = views.find(widget);
    if (it != views.end()) {
        releaseBlock(it->second.get());
        views.erase(it);
        // Ids came back: a viewer that did not fit may fit now, and
        // should say so again if it still does not.
        warnedViewBudget = false;
        if (views.empty())
            _BGFXLib.shutdown();
    }
}

BGFXView *BGFXRendererLibP::getView(QOpenGLWidget *widget, RendererType::Enum type)
{
    if (!prepare(widget, type))
        return nullptr;

    auto &view = views[widget];
    if (!view) {
        view.reset(new BGFXView);
        view->widget = widget;
        // No ids yet: the block is sized once the frame has declared
        // which passes it draws (BGFXRenderer::render -> reserveBlock).
    }
    return view.get();
}

// Map the active bgfx backend to the shaderc CLI target flags and the
// bin subdirectory the stock shader pack uses (BGFXShaders.cmake keeps
// the flag pairs in sync). The profile string doubles as the label a
// viewer matches against the precompiled variants a snapshot ships
// (UserShader::Compiled::profile).
bool shadercTarget(std::string &platform, std::string &profile,
                          std::string &apiDir)
{
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::OpenGL:
        platform = "linux"; profile = "140"; apiDir = "glsl"; return true;
    case bgfx::RendererType::Vulkan:
        platform = "linux"; profile = "spirv"; apiDir = "spirv"; return true;
    case bgfx::RendererType::OpenGLES:
        platform = "android"; profile = "300_es"; apiDir = "essl"; return true;
    case bgfx::RendererType::Metal:
        platform = "osx"; profile = "metal"; apiDir = "metal"; return true;
    default:
        return false;
    }
}

#ifndef FC_RENDERER_STANDALONE
// Load a compiled shader binary into bgfx (the bgfx_utils loader with
// an explicit full path instead of the name/asset-root convention).
static bgfx::ShaderHandle loadShaderFile(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return BGFX_INVALID_HANDLE;
    QByteArray data = f.readAll();
    if (data.isEmpty())
        return BGFX_INVALID_HANDLE;
    const bgfx::Memory *mem = bgfx::copy(data.constData(),
                                         uint32_t(data.size()) + 1);
    mem->data[mem->size - 1] = '\0';
    return bgfx::createShader(mem);
}
#endif // !FC_RENDERER_STANDALONE

// The per-API subdirectory the stock pack is compiled into, mirroring
// bgfx_utils' own mapping (BGFXShaders.cmake writes the same names).
static const char *shaderBinDir()
{
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Direct3D11: return "dxbc";
    case bgfx::RendererType::Direct3D12: return "dxil";
    case bgfx::RendererType::Agc:
    case bgfx::RendererType::Gnm:        return "pssl";
    case bgfx::RendererType::Metal:      return "metal";
    case bgfx::RendererType::Nvn:        return "nvn";
    case bgfx::RendererType::OpenGLES:   return "essl";
    case bgfx::RendererType::Vulkan:     return "spirv";
    default:                             return "glsl";
    }
}

bgfx::ShaderHandle fcLoadShader(const char *name, const char *path)
{
    std::string file(path ? path : "");
    if (!file.empty() && file.back() != '/')
        file += '/';
    file += "shaders/";
    file += shaderBinDir();
    file += '/';
    file += name;
    file += ".bin";
#ifdef FC_RENDERER_STANDALONE
    bgfx::ShaderHandle h = loadShaderData(file);
#else
    bgfx::ShaderHandle h = loadShaderFile(file);
#endif
    if (!bgfx::isValid(h)) {
        // Missing, unreadable, or not a shader binary this bgfx accepts
        // (createShader rejects a bad signature by itself).
        qWarning() << "bgfx: shader unloadable:" << file.c_str();
        return h;
    }
    bgfx::setName(h, name);
    return h;
}

bgfx::ProgramHandle fcLoadProgram(const char *vsName, const char *fsName,
                                  const char *path)
{
    bgfx::ShaderHandle vsh = fcLoadShader(vsName, path);
    bgfx::ShaderHandle fsh = BGFX_INVALID_HANDLE;
    if (fsName && *fsName)
        fsh = fcLoadShader(fsName, path);
    if (!bgfx::isValid(vsh) || !bgfx::isValid(fsh)) {
        // createProgram would return invalid here too, but its early
        // out ignores destroyShaders and orphans whichever stage did
        // load.
        if (bgfx::isValid(vsh))
            bgfx::destroy(vsh);
        if (bgfx::isValid(fsh))
            bgfx::destroy(fsh);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vsh, fsh, true);
}

// A generated sampler name is known only once a document has been
// generated (desktop) or shipped (viewer), so the handle is made on
// first use, on either tier.
bgfx::UniformHandle
BGFXRendererLibP::userSampler(const std::string &name)
{
    auto it = userSamplers.find(name);
    if (it == userSamplers.end())
        it = userSamplers
                 .emplace(name,
                          bgfx::createUniform(name.c_str(),
                                              bgfx::UniformType::Sampler))
                 .first;
    return it->second;
}

#ifndef FC_RENDERER_STANDALONE

int
BGFXRendererLibP::ensureUserShaderBin(const std::string &source,
                                      bool fragment, QString &binPath,
                                      const char *platformOverride,
                                      const char *profileOverride)
{
    std::string platform, profile, apiDir;
    if (platformOverride && profileOverride) {
        platform = platformOverride;
        profile = profileOverride;
    } else if (!shadercTarget(platform, profile, apiDir)) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Base::Console().Error("user shaders: no shaderc target for "
                                  "the active bgfx backend\n");
        }
        return 2;
    }

    // The compile inputs: the stock varying/include set shipped next to
    // the compiled bins (BGFXShaders.cmake copies shaders/src there), so
    // user source can include the fc_*.sh helpers and bgfx_shader.sh.
    std::string srcDir = shaderPath() + "shaders/src";
    if (!QFile::exists(QString::fromStdString(srcDir + "/varying.def.sc")))
        srcDir = resource() + "shaders/src";

    // The include tree is part of the compile identity: the cache
    // persists across builds, and a shipped-helper edit (e.g.
    // fc_user_lighting.sh) must miss the stale bins. Content-hashed
    // once per source dir per session.
    auto fpIt = userShaderSrcFingerprints.find(srcDir);
    if (fpIt == userShaderSrcFingerprints.end()) {
        QCryptographicHash fp(QCryptographicHash::Sha1);
        const auto entries = QDir(QString::fromStdString(srcDir))
            .entryInfoList({QStringLiteral("*.sh"), QStringLiteral("*.sc")},
                           QDir::Files, QDir::Name);
        for (const auto &fi : entries) {
            fp.addData(fi.fileName().toUtf8());
            fp.addData(QByteArrayView("\1", 1));
            QFile f(fi.absoluteFilePath());
            if (f.open(QIODevice::ReadOnly))
                fp.addData(&f);
            fp.addData(QByteArrayView("\1", 1));
        }
        fpIt = userShaderSrcFingerprints.emplace(srcDir,
                                                 fp.result()).first;
    }

    QByteArray keyed(source.c_str(), int(source.size()));
    keyed.append('\1');
    // The platform is part of the target identity: shaderc emits
    // different essl for asm.js (the WASM viewer pack) than for
    // android even at the same profile.
    keyed.append(platform.c_str());
    keyed.append('\1');
    keyed.append(profile.c_str());
    keyed.append(fragment ? 'f' : 'v');
    keyed.append('\1');
    keyed.append(fpIt->second);
    QString hash = QString::fromLatin1(
        QCryptographicHash::hash(keyed, QCryptographicHash::Sha1).toHex());
    std::string hashKey = hash.toStdString();

    QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/BGFXUserShaders");
    QDir().mkpath(cacheDir);
    binPath = cacheDir + QLatin1Char('/') + hash + QStringLiteral(".bin");
    if (QFile::exists(binPath))
        return 0;
    if (userShaderFailed.count(hashKey))
        return 2;
    if (userShaderInflight.count(hashKey))
        return 1;

    std::string shaderc;
    if (const char *env = std::getenv("FC_BGFX_SHADERC"))
        shaderc = env;
#ifdef FC_BGFX_SHADERC_PATH
    if (shaderc.empty())
        shaderc = FC_BGFX_SHADERC_PATH;
#endif
    std::string err;
    if (!QFile::exists(QString::fromStdString(srcDir + "/varying.def.sc")))
        err = "user shaders: shader include tree not found at " + srcDir
            + " (rebuild Renderer_assets)";
    else if (shaderc.empty()
             || !QFile::exists(QString::fromStdString(shaderc)))
        err = "user shaders: shaderc not found ("
            + (shaderc.empty() ? std::string("set FC_BGFX_SHADERC")
                               : shaderc) + ")";

    QString srcPath =
        cacheDir + QLatin1Char('/') + hash + QStringLiteral(".sc");
    if (err.empty()) {
        QFile srcFile(srcPath);
        if (!srcFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || srcFile.write(source.c_str(), qint64(source.size()))
                   != qint64(source.size()))
            err = "user shaders: cannot write " + srcPath.toStdString();
    }
    if (!err.empty()) {
        userShaderFailed.insert(hashKey);
        Base::Console().Error("%s\n", err.c_str());
        return 2;
    }

    // Never spawn the compiler from here: this runs inside the widget
    // paint traversal, and a blocking QProcess there both hitches the
    // frame and re-enters Qt's repaint machinery. Queue the launch onto
    // the event loop instead; the finished handler records the outcome
    // and bumps the generation so the next frame picks the bin up.
    userShaderInflight.insert(hashKey);
    // shaderc writes its output incrementally: compiling straight into
    // binPath races the exists() fast path above with a partial file (a
    // load failure then negative-caches the program for the session).
    // Compile to a sidecar and rename on success — the .bin only ever
    // appears complete.
    QString tmpPath = binPath + QStringLiteral(".tmp");
    QStringList args = {
        QStringLiteral("-f"), srcPath,
        QStringLiteral("-o"), tmpPath,
        QStringLiteral("--type"),
        fragment ? QStringLiteral("f") : QStringLiteral("v"),
        QStringLiteral("--platform"), QString::fromUtf8(platform.c_str()),
        QStringLiteral("-p"), QString::fromUtf8(profile.c_str()),
        QStringLiteral("-i"), QString::fromUtf8(srcDir.c_str()),
        QStringLiteral("--varyingdef"),
        QString::fromUtf8((srcDir + "/varying.def.sc").c_str()),
    };
    QString cmd = QString::fromUtf8(shaderc.c_str());
    QString bin = binPath;
    QString tmp = tmpPath;
    auto self = this;
    QTimer::singleShot(0, [self, cmd, args, bin, tmp, hashKey]() {
        auto proc = new QProcess;
        QObject::connect(proc,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            [self, proc, bin, tmp, hashKey](int code,
                                            QProcess::ExitStatus status) {
                if (status != QProcess::NormalExit || code != 0) {
                    QFile::remove(tmp);
                    self->userShaderFailed.insert(hashKey);
                    std::string err = (proc->readAllStandardError()
                        + proc->readAllStandardOutput()).toStdString();
                    Base::Console().Error(
                        "user shader compile failed:\n%s\n",
                        err.empty() ? "shaderc failed" : err.c_str());
                }
                else {
                    QFile::remove(bin);
                    if (!QFile::rename(tmp, bin)) {
                        self->userShaderFailed.insert(hashKey);
                        Base::Console().Error(
                            "user shader compile: cannot move %s into "
                            "place\n", tmp.toStdString().c_str());
                    }
                }
                self->userShaderInflight.erase(hashKey);
                ++self->userCompileGeneration;
                proc->deleteLater();
            });
        QObject::connect(proc,
            qOverload<QProcess::ProcessError>(&QProcess::errorOccurred),
            [self, proc, hashKey](QProcess::ProcessError) {
                if (proc->state() != QProcess::NotRunning)
                    return;   // finished() will report
                self->userShaderFailed.insert(hashKey);
                self->userShaderInflight.erase(hashKey);
                ++self->userCompileGeneration;
                Base::Console().Error(
                    "user shader compile failed: cannot run shaderc\n");
                proc->deleteLater();
            });
        // Watchdog: a hung compiler must not pin the pass in the
        // "compiling" state forever.
        QTimer::singleShot(20000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning)
                proc->kill();
        });
        proc->start(cmd, args);
    });
    return 1;
}

const BGFXRendererLibP::MaterialXVariant &
BGFXRendererLibP::materialXVariant(const Render::UserShader &shader)
{
    // A document is identified by the file it came from, or by its text
    // when it has no file, AND by which of its surfaces is worn: one
    // document usually carries a whole asset's material set, and two
    // surfaces of it are two shaders (docs/MaterialStorage.md sec
    // 17.13). Two draws sharing a material share one generation and one
    // compile.
    const std::string key =
        (shader.sourcePath.empty() ? shader.fragmentSource : shader.sourcePath)
        + '\0' + shader.surface;
    auto it = materialXVariants.find(key);
    if (it != materialXVariants.end()) {
        it->second.lastUsed = frameSerial;
        return it->second;
    }

    auto gen = Render::MaterialX::generate(shader.fragmentSource,
                                           shader.sourcePath, shader.surface);
    const std::string document = shader.sourcePath.empty()
        ? std::string("document")
        : shader.sourcePath;
    // Once per distinct message PER DOCUMENT, not per surface. One
    // document usually carries a whole asset's material set, and a note
    // like the OpenPBR translation is about the model the document is
    // authored against -- true of every surface in it and worth saying
    // once, not fifteen times for a chess set. Two surfaces with
    // genuinely different notes (a missing image names the image) still
    // report separately, because the message is part of the key.
    for (const auto &w : gen.warnings) {
        if (materialXWarned.insert(document + '\0' + w).second)
            Base::Console().Warning("MaterialX %s: %s\n", document.c_str(), w.c_str());
    }
    std::string what = document;
    if (!shader.surface.empty())
        what += " surface '" + shader.surface + "'";
    if (!gen.valid) {
        // Reported once, here, and the draw keeps its stock appearance.
        // The path tracer reads the same document on its own terms and
        // may well render it (docs/CyclesIntegration.md sec 6.9).
        Base::Console().Warning("MaterialX %s: not rendered by the raster "
                                "path: %s\n", what.c_str(),
                                gen.error.c_str());
        MaterialXVariant &empty =
            materialXVariants.emplace(key, MaterialXVariant()).first->second;
        empty.lastUsed = frameSerial;
        return empty;
    }

    // The stock TEXTURED mesh fragment stage, spliced. Textured because
    // that is the variant whose vertex stage carries a texture
    // coordinate, which is what a pattern graph asks for most often;
    // TEXTURE itself is deliberately NOT defined, so no texture
    // environment is applied on top of what the document states. The
    // varying list has to match vs_fc_mesh_tex exactly -- bgfx links a
    // program only on an exact varying match.
    const std::string prologue =
        "$input v_normal, v_color0, v_color1, v_color2, v_texcoord0, "
        "v_vpos, v_opos, v_onrm, v_findex\n"
        "#include <bgfx_shader.sh>\n"
        "#define FC_USER_MATERIAL 1\n";
    MaterialXVariant variant;
    variant.source = prologue + "#include \"fc_mesh_fs.sh\"\n" + gen.source;
    // And the glass body stage, for a surface claimed as a glass body
    // (docs/MaterialStorage.md sec 17.22): the same function read by
    // the pass that refracts, so a mapped colour or roughness and a
    // normal map reach the glass per fragment. Same varying list --
    // the glass pairing is vs_fc_mesh_tex too.
    variant.glassSource =
        prologue + "#include \"fc_glass_fs.sh\"\n" + gen.source;
    variant.images = std::move(gen.images);
    variant.imageSampler = std::move(gen.imageSampler);
    variant.imageUnit = gen.imageUnit;
    variant.lastUsed = frameSerial;
    return materialXVariants.emplace(key, std::move(variant)).first->second;
}

void
BGFXRendererLibP::shipUserShader(Render::UserShader &shader)
{
    if (shader.fragmentSource.empty())
        return;
    std::vector<Render::UserShader::Compiled> &out = shader.compiled;
    // A MaterialX document is a material description, not shader text:
    // what the viewer tier loads is the mesh-shader variant generated
    // from it (docs/CyclesIntegration.md sec 6.10). The viewer has no
    // compiler of its own, so this server-side compile is the only one
    // it will ever get.
    std::string generated;
    std::string glassSource;
    if (shader.dialect == Render::UserShader::Dialect::MaterialX) {
        const MaterialXVariant &variant = materialXVariant(shader);
        generated = variant.source;
        if (generated.empty())
            return;
        // The glass body splice travels only for a surface claimed as a
        // glass body: the viewer's glass pass asks for it and nothing
        // else does, and a compile per document per target is not
        // spent on a surface that will never wear it.
        if (shader.glass.claimed)
            glassSource = variant.glassSource;
        // The document's images travel as the shader's own (SceneDump
        // v74), and the tier they reach has no generator to tell it
        // which layer of the program's array each one is -- so the join
        // the desktop makes per draw (BGFXView::pushUserImages) is made
        // here once, on the path, and the answer rides with the image.
        // The sampler name and unit go with it: only the generator
        // knows them (docs/CyclesIntegration.md sec 6.12).
        shader.imageSampler = variant.imageSampler;
        shader.imageUnit = variant.imageUnit;
        for (auto &img : shader.images) {
            img.layer = -1;
            for (const auto &want : variant.images) {
                if (want.path == img.path) {
                    img.layer = want.layer;
                    break;
                }
            }
        }
    }
    else if (shader.dialect != Render::UserShader::Dialect::ShaderText) {
        return;
    }
    const std::string &fsSource =
        generated.empty() ? shader.fragmentSource : generated;
    // A raw volume-stage source is a medium FUNCTION, not a whole
    // program — it can never compile standalone on any tier. Its
    // compiled form ships as the assembled splice variants instead
    // (UserShaderConfig::splices, stage "volume-splice").
    if (shader.stage == "volume")
        return;
    // The viewer targets: the WASM/WebGL viewer (whose stock pack is
    // built asm.js/300_es — BGFXShaders.cmake) and the native GL
    // standalone viewer. Each ships as one Compiled variant labeled
    // with the profile the viewer matches at load time.
    static const struct { const char *platform, *profile; } targets[] = {
        {"asm.js", "300_es"},
        {"linux", "140"},
    };
    auto readAll = [](const QString &path, std::vector<uint8_t> &bytes) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return false;
        QByteArray data = f.readAll();
        bytes.assign(data.begin(), data.end());
        return !bytes.empty();
    };
    for (const auto &t : targets) {
        Render::UserShader::Compiled c;
        c.profile = t.profile;
        QString fsBin, vsBin, simBin, glassBin;
        if (ensureUserShaderBin(fsSource, true, fsBin,
                                t.platform, t.profile) != 0)
            continue;
        // The glass splice is the other half of a glass document's
        // program: without it the viewer draws the body with the flat
        // pass, which is a different picture, so a pending compile
        // holds the variant back like the state step below does.
        if (!glassSource.empty()
                && ensureUserShaderBin(glassSource, true, glassBin,
                                       t.platform, t.profile) != 0)
            continue;
        if (!shader.vertexSource.empty()
                && ensureUserShaderBin(shader.vertexSource, false, vsBin,
                                       t.platform, t.profile) != 0)
            continue;
        // The particle state step (docs/RenderEngine.md §5.8) is a
        // fragment program of its own; without its binary the viewer
        // can draw the emitter but not simulate it, so a pending
        // compile holds the whole variant back rather than shipping a
        // half-usable one.
        if (!shader.simulateSource.empty()
                && ensureUserShaderBin(shader.simulateSource, true, simBin,
                                       t.platform, t.profile) != 0)
            continue;
        if (!readAll(fsBin, c.fsBin))
            continue;
        if (!vsBin.isEmpty() && !readAll(vsBin, c.vsBin))
            continue;
        if (!simBin.isEmpty() && !readAll(simBin, c.simBin))
            continue;
        if (!glassBin.isEmpty() && !readAll(glassBin, c.glassBin))
            continue;
        out.push_back(std::move(c));
    }
}

bgfx::ProgramHandle
BGFXRendererLibP::getUserProgram(const Render::UserShader &shader,
                                 const char *stockVs, bool simulate,
                                 UserSplice splice)
{
    static const std::string kNoVertexStage;
    // A MaterialX document is a material description, not shader text:
    // handing one to shaderc would report a compile error per material
    // and draw nothing new. What is compiled is the mesh-shader variant
    // generated from it (docs/CyclesIntegration.md sec 6.10) -- the
    // stock fragment stage with the document's material-inputs function
    // spliced in, so the engine keeps its own lighting. It pairs with
    // the TEXTURED stock vertex stage, whatever the caller asked for,
    // because that is the one carrying a texture coordinate.
    std::string generated;
    if (shader.dialect == Render::UserShader::Dialect::MaterialX) {
        if (simulate)
            return BGFX_INVALID_HANDLE;
        const MaterialXVariant &variant = materialXVariant(shader);
        generated = splice == GlassSplice ? variant.glassSource
                                          : variant.source;
        if (generated.empty())
            return BGFX_INVALID_HANDLE;
        stockVs = "vs_fc_mesh_tex";
    }
    else if (shader.dialect != Render::UserShader::Dialect::ShaderText) {
        return BGFX_INVALID_HANDLE;
    }
    const std::string &vsSource =
        (simulate || !generated.empty()) ? kNoVertexStage : shader.vertexSource;
    const std::string &fsSource =
        simulate ? shader.simulateSource
                 : (generated.empty() ? shader.fragmentSource : generated);
    if (fsSource.empty())
        return BGFX_INVALID_HANDLE;
    QByteArray keyed(fsSource.c_str(), int(fsSource.size()));
    keyed.append('\1');
    keyed.append(vsSource.c_str(), int(vsSource.size()));
    if (vsSource.empty()) {
        // The stock vertex stage is part of the program identity: the
        // same fragment source pairs differently per stage.
        keyed.append('\1');
        keyed.append(stockVs);
    }
    std::string key =
        QCryptographicHash::hash(keyed, QCryptographicHash::Sha1)
            .toHex().toStdString();
    auto &entry = userPrograms[key];
    entry.lastUsed = frameSerial;
    if (bgfx::isValid(entry.prog) || entry.failed)
        return entry.prog;

    QString fsBin, vsBin;
    int fsState = ensureUserShaderBin(fsSource, true, fsBin);
    int vsState = vsSource.empty()
        ? 0 : ensureUserShaderBin(vsSource, false, vsBin);
    if (fsState == 2 || vsState == 2) {
        // The per-shader compile already reported the error once.
        entry.failed = true;
        return BGFX_INVALID_HANDLE;
    }
    if (fsState == 1 || vsState == 1) {
        // Still compiling: the stock program stands in this frame and
        // the lookup is retried next frame. The stand-in is recorded
        // so the frame tail holds a pending capture -- a capture is of
        // the materials, and this frame does not have them yet.
        userProgramStoodIn = true;
        return BGFX_INVALID_HANDLE;
    }

    bgfx::ShaderHandle fsh = loadShaderFile(fsBin.toStdString());
    if (!bgfx::isValid(fsh)) {
        entry.failed = true;
        // Drop the cache entry (e.g. truncated by a crashed compile) so
        // the next session recompiles instead of trusting it again.
        QFile::remove(fsBin);
        Base::Console().Error("user shader: compiled binary unloadable: "
                              "%s\n", fsBin.toStdString().c_str());
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ShaderHandle vsh = BGFX_INVALID_HANDLE;
    if (vsSource.empty()) {
        std::string platform, profile, apiDir;
        if (shadercTarget(platform, profile, apiDir)) {
            std::string bin = std::string("/") + stockVs + ".bin";
            vsh = loadShaderFile(shaderPath() + "shaders/" + apiDir + bin);
            if (!bgfx::isValid(vsh))
                vsh = loadShaderFile(resource() + "shaders/" + apiDir
                                     + bin);
        }
    }
    else {
        vsh = loadShaderFile(vsBin.toStdString());
    }
    if (!bgfx::isValid(vsh)) {
        entry.failed = true;
        bgfx::destroy(fsh);
        if (!vsSource.empty())
            QFile::remove(vsBin);
        Base::Console().Error("user shader: vertex stage unloadable\n");
        return BGFX_INVALID_HANDLE;
    }
    // The program owns both shader handles (destroyShaders = true).
    entry.prog = bgfx::createProgram(vsh, fsh, true);
    entry.failed = !bgfx::isValid(entry.prog);
    if (entry.failed)
        Base::Console().Error("user shader: program link failed\n");
    return entry.prog;
}

#else // FC_RENDERER_STANDALONE

bgfx::ProgramHandle
BGFXRendererLibP::getUserProgram(const Render::UserShader &shader,
                                 const char *stockVs, bool simulate,
                                 UserSplice splice)
{
    // No compiler in this tier: resolve from the precompiled variants
    // the snapshot ships (server-side compile, docs/RenderDebug.md
    // §6.3). Until the backend's compile finishes and a republished
    // snapshot carries the matching variant, there is nothing to load
    // and the stock program stands in.
    //
    // A MaterialX document ships as its generated splices
    // (shipUserShader): the mesh splice in fsBin and, for a surface
    // claimed as a glass body, the glass splice in glassBin. Both were
    // assembled over the textured mesh varying list, so both pair with
    // vs_fc_mesh_tex whatever the caller asked for -- the desktop makes
    // the same substitution, and bgfx links only on an exact varying
    // match. The glass pass asking for a splice that did not travel
    // gets invalid, and its flat program stands in.
    const bool mtlx = shader.dialect == Render::UserShader::Dialect::MaterialX;
    if (mtlx) {
        if (simulate)
            return BGFX_INVALID_HANDLE;
        stockVs = "vs_fc_mesh_tex";
    }
    else if (splice == GlassSplice) {
        return BGFX_INVALID_HANDLE;
    }
    const bool glass = mtlx && splice == GlassSplice;
    std::string platform, profile, apiDir;
    if (!shadercTarget(platform, profile, apiDir))
        return BGFX_INVALID_HANDLE;
    auto binOf = [&](const Render::UserShader::Compiled &c)
        -> const std::vector<uint8_t> & {
        return simulate ? c.simBin : glass ? c.glassBin : c.fsBin;
    };
    const Render::UserShader::Compiled *variant = nullptr;
    for (const auto &c : shader.compiled) {
        if (c.profile == profile && !binOf(c).empty()) {
            variant = &c;
            break;
        }
    }
    if (!variant)
        return BGFX_INVALID_HANDLE;
    // The state step is always the stock full-screen vertex stage.
    const std::vector<uint8_t> &fsBin = binOf(*variant);
    static const std::vector<uint8_t> kNoVertexBin;
    const std::vector<uint8_t> &vsBin =
        simulate ? kNoVertexBin : variant->vsBin;

    // Cache on the binary payloads themselves: snapshot reloads build
    // fresh UserShader instances, but identical bins keep hitting the
    // same linked program.
    std::string key = profile;
    key += '\1';
    key += stockVs;
    key += '\1';
    key.append(reinterpret_cast<const char *>(fsBin.data()),
               fsBin.size());
    key += '\1';
    if (!vsBin.empty())
        key.append(reinterpret_cast<const char *>(vsBin.data()),
                   vsBin.size());
    auto &entry = userPrograms[key];
    entry.lastUsed = frameSerial;
    if (bgfx::isValid(entry.prog) || entry.failed)
        return entry.prog;

    auto shaderFromBin = [](const std::vector<uint8_t> &bin) {
        const bgfx::Memory *mem = bgfx::alloc(uint32_t(bin.size()) + 1);
        std::memcpy(mem->data, bin.data(), bin.size());
        mem->data[bin.size()] = '\0';
        return bgfx::createShader(mem);
    };
    bgfx::ShaderHandle fsh = shaderFromBin(fsBin);
    if (!bgfx::isValid(fsh)) {
        entry.failed = true;
        fprintf(stderr, "user shader: shipped fragment binary "
                        "unloadable (profile %s)\n", profile.c_str());
        return BGFX_INVALID_HANDLE;
    }
    bgfx::ShaderHandle vsh = vsBin.empty()
        ? fcLoadShader(stockVs, shaderPath().c_str())
        : shaderFromBin(vsBin);
    if (!bgfx::isValid(vsh)) {
        entry.failed = true;
        bgfx::destroy(fsh);
        fprintf(stderr, "user shader: vertex stage unloadable\n");
        return BGFX_INVALID_HANDLE;
    }
    entry.prog = bgfx::createProgram(vsh, fsh, true);
    entry.failed = !bgfx::isValid(entry.prog);
    if (entry.failed)
        fprintf(stderr, "user shader: program link failed\n");
    return entry.prog;
}

#endif // !FC_RENDERER_STANDALONE

BGFXRendererLibP::~BGFXRendererLibP()
{
    // This destructor runs at library unload, when Qt is partially or fully
    // torn down; deleting the QOpenGLContext/QOffscreenSurface (or views
    // holding bgfx resources) here crashes inside Qt. If shutdown() ran the
    // pointers are already null; otherwise leak them, the process is exiting.
    for (auto &v : views)
        v.second.release();
    views.clear();
#ifndef FC_RENDERER_STANDALONE
    context.release();
    offscreen.release();
#endif
}

void BGFXRendererLibP::sweepUserCaches()
{
    // Caps and age. A whole asset's material set is well under the
    // caps and stays put; a gesture's text outlives its last preview
    // frame by the age and goes.
    constexpr size_t kKeepVariants = 32;
    constexpr size_t kKeepPrograms = 64;
    constexpr uint32_t kStaleFrames = 120;
    const uint32_t now = frameSerial++;
    if (materialXVariants.size() > kKeepVariants) {
        for (auto it = materialXVariants.begin();
             it != materialXVariants.end();) {
            if (now - it->second.lastUsed > kStaleFrames)
                it = materialXVariants.erase(it);
            else
                ++it;
        }
    }
    if (userPrograms.size() > kKeepPrograms) {
        for (auto it = userPrograms.begin(); it != userPrograms.end();) {
            if (now - it->second.lastUsed > kStaleFrames) {
                // The program owns its shaders (createProgram with
                // destroyShaders): one destroy frees all three handles.
                if (bgfx::isValid(it->second.prog))
                    bgfx::destroy(it->second.prog);
                it = userPrograms.erase(it);
            }
            else
                ++it;
        }
    }
}

void BGFXRendererLibP::shutdown()
{
    if (currentType == RendererType::Noop)
        return;
    // Must make the context current before shutdown bgfx, or else it seems to
    // mess up with the other context that is currently active
    makeCurrent();
    for (auto &v : userUniforms) {
        if (bgfx::isValid(v.second.handle))
            bgfx::destroy(v.second.handle);
    }
    userUniforms.clear();
    if (bgfx::isValid(timeUniform)) {
        bgfx::destroy(timeUniform);
        timeUniform = BGFX_INVALID_HANDLE;
    }
    for (auto &v : userPrograms) {
        if (bgfx::isValid(v.second.prog))
            bgfx::destroy(v.second.prog);
    }
    userPrograms.clear();
    // The 2D page engine's vg context lives on this device: destroy it
    // while bgfx is still alive, and bump its generation so retained
    // pages forget their now-dead command list handles.
    Vg2D::instance().shutdown();
    bgfx::shutdown();
#ifndef FC_RENDERER_STANDALONE
    if (window) {
        window->deleteLater();
        window = nullptr;
    }
    context.reset();
    offscreen.reset();
#endif
    currentType = RendererType::Noop;
    // The device identity dies with the device. resolveDeviceName()
    // answers once and caches, so a name left standing here is handed
    // to every capture taken after a backend switch -- the sidecar then
    // records the PREVIOUS device under a golden drawn by the new one,
    // which is exactly the confusion that field exists to prevent.
    deviceName.clear();
    typeLockWarned = RendererType::Count;
}
