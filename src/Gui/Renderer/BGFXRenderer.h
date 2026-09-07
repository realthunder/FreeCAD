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

#ifndef RENDERER_BGFX_RENDERER_H
#define RENDERER_BGFX_RENDERER_H

#include "Renderer.h"

namespace Render {

class BGFXRendererLibP;

class BGFXRendererLib : public RendererLib
{
public:
    BGFXRendererLib();
    virtual const std::string &name() const override;
    virtual const std::vector<std::string> &types() const override;
    virtual std::unique_ptr<Renderer> create(
            const std::string &type, QOpenGLWidget *widget,
            bool publishOnly = false) const override;
    virtual bool warmup(QOpenGLWidget *widget, const std::string &type,
                        WarmupTiming *timing = nullptr) override;
    virtual bool deviceSharesQtGL() const override;
    virtual bool deviceMakeCurrent() override;
    virtual void deviceDoneCurrent() override;
    virtual DrawDevice *drawDevice() const override;
};

class BGFXRenderer : public Renderer
{
public:
    BGFXRenderer(QOpenGLWidget *widget, bool publishOnly = false);
    ~BGFXRenderer();
    virtual const std::string &type() const override;
    virtual bool render(const QColor &bg,
                        const void *viewMatrix,
                        const void *projMatrix) override;
    /// Render one capture frame at the requested size into the
    /// framebuffer the caller bound; see Render::Renderer. Standalone
    /// builds return false -- the host owns its backbuffer.
    virtual bool renderOffscreen(const QColor &bg,
                                 const void *viewMatrix,
                                 const void *projMatrix,
                                 int width, int height) override;
    /// Split-view frame (docs/SplitViews.md sec 9.2, sec 13): N
    /// sub-views of the resident scene, per-sub-view camera + rect,
    /// one BGFXView with per-sub-view state banks. Standalone: one
    /// backend frame, rects on the backbuffer in device px. Desktop:
    /// one ordinary frame per submit, each blitted (color + depth)
    /// into the caller's bound framebuffer at its rect, in the host
    /// widget's coordinate units.
    virtual bool renderSubViews(const QColor &bg,
                                const SubViewFrame *subs,
                                int count) override;
    virtual void dropSubView(int id) override;
    virtual void setMainViewStyle(uint8_t styleMask, uint8_t styleNameBit,
                                  bool fromSuperset,
                                  const StyleOverrideTable *overrides,
                                  uint16_t styleMode = 0) override;
    virtual void setCaptureInterest(
            const CaptureInterestTable *table) override;
    virtual void prepareSubViews(const QColor &bg,
                                 const SubViewFrame *subs,
                                 int count) override;
    virtual bool setCaptureFilter(
            const std::vector<std::pair<std::string, std::string>> &objects)
            override;
    virtual void clearCaptureFilter() override;
    virtual bool setCaptureScene(DrawCallList &&draws) override;
    virtual void clearCaptureScene() override;
    virtual bool shaderCompilePending() const override;
    virtual int shaderCompileGeneration() const override;
private:
#ifndef FC_RENDERER_STANDALONE
    bool renderFiltered(const QColor &bg,
                        const void *viewMatrix,
                        const void *projMatrix);
    /// The shared capture-frame body: swap \a scene in for the resident
    /// feeds (selection/overlay/highlight stripped, flat background,
    /// default headlight), render with settle frames, restore.
    bool renderSwappedScene(DrawCallList &&scene,
                            const QColor &bg,
                            const void *viewMatrix,
                            const void *projMatrix);
#endif
public:
    virtual bool publish(const QColor &bg,
                         const void *viewMatrix,
                         const void *projMatrix,
                         int width, int height) override;
    virtual void setPublishGroup(const std::string &doc) override;
    virtual bool boundBox(float &xmin, float &ymin, float &zmin,
                          float &xmax, float &ymax, float &zmax) override;
    virtual bool animating() const override;

    virtual void setScene(DrawCallList &&draws) override;
    virtual void setObjectInfo(ObjectInfoMap &&info) override;
    virtual void updateObjectInfo(ObjectInfoMap &&added) override;
    virtual void setObjectMeta(ObjectMetaMap &&meta) override;
    virtual void updateObjectMeta(
            ObjectMetaMap &&changed,
            const std::vector<std::pair<std::string, std::string>> &removed)
            override;
    virtual void setBackground(const Background &bg) override;
    virtual void addSelection(int id, DrawCallList &&draws) override;
    virtual void removeSelection(int id) override;
    virtual void setHighlight(DrawCallList &&draws, bool wholeOnTop) override;
    virtual void clearHighlight() override;
    virtual void setOverlay(int id, DrawCallList &&draws,
                            const OverlayAnchor &anchor) override;
    virtual void removeOverlay(int id) override;
    virtual void setFrameConsumer(FrameConsumer *consumer, int subView) override;
    virtual DrawSurface *frameConsumerSurface(int subView) override;
    virtual void setHiddenLineConfig(const HiddenLineConfig &config) override;
    virtual void setExternalBaseLayer(bool on, int subView) override;
    virtual void setSectionConfig(const SectionConfig &config) override;
    virtual void setAOConfig(const AOConfig &config) override;
    virtual void setCavityConfig(const CavityConfig &config) override;
    virtual void setMatcapConfig(const MatcapConfig &config) override;
    virtual bool isSceneAnimated() const override;
    virtual bool isSceneDirty() const override;
    virtual void setPBRConfig(const PBRConfig &config) override;
    virtual void setOutputConfig(const OutputConfig &config) override;
    virtual void setBumpConfig(const BumpConfig &config) override;
    virtual void setLightConfig(const LightConfig &config) override;
    virtual void setViewLightConfig(const ViewLightConfig &config) override;
    virtual void setVolumetricConfig(const VolumetricConfig &config) override;
    virtual void setWaterConfig(const WaterConfig &config) override;
    virtual void setBloomConfig(const BloomConfig &config) override;
    virtual void setTemporalConfig(const TemporalConfig &config) override;
    virtual void setRenderDebugConfig(const RenderDebugConfig &config) override;
    virtual void setOcclusionCullConfig(
            const OcclusionCullConfig &config) override;
    virtual void setUserShaderConfig(const UserShaderConfig &config) override;
    virtual void setPreselConfig(const PreselHighlightConfig &config) override;
    virtual void setSelConfig(const PreselHighlightConfig &config) override;
    virtual void setAutoZoomScale(float scale) override;
    virtual void setHatchImage(const void *data, int nc,
                               int width, int height) override;
    virtual bool needsRedraw() const override;
    virtual bool canSkipInternal() const override;
    /// One-shot capture of the bgfx scene color FBO (the true desktop
    /// GL readback, pre-Coin-composite); executes inside the next
    /// frame's blit. Standalone builds return false — the host reads
    /// its own backbuffer (wasm dumpFrame protocol).
    virtual bool requestFrameDump(const FrameDumpRequest &req) override;
    virtual bool frameDumpPending() const override;
    virtual void holdFrameDump() override;
    virtual bool frameDumpHeld() const override;
    virtual bool frameComplete() const override;
    virtual uint64_t renderedFrames() const override;
    virtual uint64_t completeFrames() const override;
    virtual bool getRenderStats(RenderStats &stats) const override;
    virtual std::string deviceName() const override;
    virtual bool reloadShaders() override;
    /// Drop this view's sized targets (BGFXView::destroyTargets, the
    /// resize path's release); the next frame rebuilds them.
    virtual bool releaseTargets() override;
    /// Scene render-target sample count (0/1 = off). Takes effect when the
    /// view next (re)creates its targets (detected at the top of render()).
    virtual void setMSAASamples(int samples) override;
    /// Resolution scale of the expensive screen-space effect passes
    /// (reflection re-render, SSAO resolve); see Render::Renderer.
    virtual void setEffectResolution(float scale) override;
    virtual void setSSAOResolution(float scale) override;
    /// The element gates (docs/SceneStreaming.md #13b). Outside the
    /// desktop guard below: the vertex gate is pure display and is
    /// worth more on a phone than on the desktop, so the standalone
    /// viewer drives it from its URL parameters.
    void setTinyElementCutoff(int prims) override;

    virtual void setElementGates(bool shapeVertices, bool pressureEdges,
                                 bool loadingDrop, int staggerFrames) override;

    /// Outside the desktop guard for the same reason as the gates above,
    /// and it took a browser session to notice it was not: the flag it
    /// sets is cross-tier (Private::levelDebug, "with the environment
    /// variable as the standalone viewer's way in"), the lines it turns
    /// on are cross-tier (FC_RENDER_MSG, which is std::printf here), and
    /// the gate counters they carry are computed on this tier whether or
    /// not anyone can read them. Only the SETTER was walled off, so the
    /// viewer's call resolved to the base class's no-op and every
    /// "render levels:" line stayed dark in the browser -- silently,
    /// which is how a whole tier's instrumentation went unread. The
    /// environment variable the header names as the standalone way in
    /// does not exist in a browser.
    virtual void setLevelDebug(bool on) override;

#ifndef FC_RENDERER_STANDALONE
    virtual void setLevelTolerance(float px) override;
    virtual bool drivesMeshLevels() const override;
    virtual void setGpuMemoryBudget(size_t bytes) override;
    virtual void setLevelPressureRelease(float fraction) override;
    virtual void setDowngradeLedger(bool on) override;
    virtual void setClimbAdmission(bool hardLimit, int batch) override;
    virtual void setDescentOrderBatch(int batch) override;
    virtual void setLevelBudgetDeadband(float fraction) override;
#endif

#ifdef FC_RENDERER_STANDALONE
    /// Standalone (no Qt) build: the host app hands bgfx the native
    /// window handle (Emscripten: the canvas CSS selector, e.g.
    /// "#canvas") before creating a renderer, and reports the current
    /// output size — the next render() picks up a change.
    static void setWindowHandle(void *handle);
    static void setWindowSize(int width, int height);
#endif

    friend class BGFXRendererLib;
    friend class BGFXRendererLibP;

private:
    class Private;
    std::unique_ptr<Private> pimpl;
};

} // namespace Renderer

#endif // RENDERER_BGFX_RENDERER_H
