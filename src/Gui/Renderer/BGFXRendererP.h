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

#ifndef GUI_RENDERER_BGFXRENDERERP_H
#define GUI_RENDERER_BGFXRENDERERP_H

/// Private header of the bgfx renderer: shared by the BGFXRenderer
/// translation units only (BGFXRenderer.cpp and the BGFXView*/
/// per-feature files). Not an API -- nothing outside
/// src/Gui/Renderer may include it.
#include "FCConfig.h"
#include "MaterialXSupport.h"
#include "BGFXRenderer.h"
#include "SceneDump.h"
#include "MeshSource.h"
#include "SceneLadder.h"
#include "ProxyHierarchy.h"
#include "ProxyStore.h"
#include "CullBenefit.h"
#include "MaskedOcclusion.h"
#include "Simd4.h"
#include "OcclusionCull.h"
#include "MeshSimplify.h"
#ifndef FC_RENDERER_STANDALONE
#include "SceneServer.h"
#endif

#ifndef FC_OS_WIN32
# ifndef GL_GLEXT_PROTOTYPES
#   define GL_GLEXT_PROTOTYPES 1
# endif
#endif

#ifndef _PreComp_
# include <float.h>
# ifdef FC_OS_WIN32
#  ifndef NOMINMAX
#   define NOMINMAX
#  endif
#  include <windows.h>
// windows.h still defines the 16-bit memory-model keywords near/far as empty
// macros, which silently eats the type in declarations like "float near = ...".
// It also defines a couple of names this file uses as identifiers.
#  undef near
#  undef far
# endif
# ifdef FC_OS_MACOSX
# include <OpenGL/gl.h>
# else
# include <GL/gl.h>
# endif
#endif

#include <algorithm>
#include <type_traits>
#include <cfloat>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <vector>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <set>

#undef GL_GLEXT_VERSION
#ifdef __EMSCRIPTEN__
#include <cstdlib>
#include <emscripten/html5.h>
#endif
#ifdef FC_RENDERER_STANDALONE
#include "StandalonePlatform.h"
#else
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QWindow>
#include <QDebug>
#include <Base/Console.h>
#endif

// #if !defined(FC_OS_MACOSX)
// # include <GL/gl.h>
// # include <GL/glu.h>
// # include <GL/glext.h>
// #endif
// One narration path for the readouts that belong to BOTH tiers.
//
// Base/Console.h is included above only in the Qt half, so every
// unguarded Base::Console() in this file is a standalone build error --
// and there were six, in the frame-timing instrument, which is why the
// WASM tier had stopped compiling. Desktop routing is deliberately
// UNCHANGED: the performance harnesses read these lines out of
// --log-file, and only the console writes there.
#ifdef FC_RENDERER_STANDALONE
#  define FC_RENDER_MSG(...) std::printf(__VA_ARGS__)
#else
#  define FC_RENDER_MSG(...) Base::Console().Message(__VA_ARGS__)
#endif


#include <bgfx/bgfx.h>
#include <bx/timer.h>
#include <bx/math.h>

#ifndef FC_RENDERER_STANDALONE
#include <bgfx_utils.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#   include <QtGui/qopenglcontext_platform.h>
#elif defined FC_OS_LINUX
#   include <QtPlatformHeaders/QGLXNativeContext>
typedef QGLXNativeContext OpenGLContext;
#elif defined FC_OS_WIN
#  include <QtPlatformHeaders/QWGLNativeContext>
typedef QWGLNativeContext OpenGLContext;
#elif defined FC_OS_MACOSX
#  include <QtPlatformHeaders/QCocoaNativeContext>
typedef QCocoaNativeContext OpenGLContext;
#endif
#endif // !FC_RENDERER_STANDALONE
#undef KeyPress
#undef Status
#undef None

using namespace Render;
using namespace bgfx;

#define _RENDER_LOG(_lvl, _line, _msg) \
    _lvl() << "bgfx (" << _line << "): " << _msg

#define _RENDER_ERR(_line, _msg) _RENDER_LOG(qCritical, _line, _msg)
#define RENDER_ERR(_msg) _RENDER_ERR(__LINE__, _msg)
#define _RENDER_WARN(_line, _msg) _RENDER_LOG(qWarn, _line, _msg)
#define RENDER_WARN(_msg) _RENDER_ERR(__LINE__, _msg)


////////////////////////////////////////////////////////

namespace
{
    /// True when float32 textures cannot be linearly filtered on this GPU.
    /// On WebGL that needs OES_texture_float_linear, which many mobile GPUs
    /// lack -- there a linear-filtered float32 (RG32F) texture is *incomplete*
    /// and every sample reads back (0,0), so a float32 variance shadow map
    /// makes the whole scene read fully shadowed. bgfx misses this on WebGL2
    /// (its per-format linear detection is gated on the WebGL1-era
    /// OES_texture_float extension, which WebGL2 reports as core). Detected
    /// once from the live context; desktop GL (and any WebGL that advertises
    /// the extension, e.g. desktop-GPU browsers) filters float32 natively.
    /// Half-float (RG16F/RGBA16F) linear filtering is separate and far more
    /// widely available, so the shadow moments drop to RG16F here rather than
    /// point-sampling RG32F (which speckles the self-shadowed terminator).
    inline bool shadowFloat32NotFilterable()
    {
        static const bool need = [] {
#ifdef __EMSCRIPTEN__
            char *exts = emscripten_webgl_get_supported_extensions();
            bool has = exts
                && std::strstr(exts, "OES_texture_float_linear") != nullptr;
            if (exts)
                std::free(exts);
            return !has;
#else
            return false;  // desktop GL: float32 linear filtering is core
#endif
        }();
        return need;
    }

#ifndef FC_RENDERER_STANDALONE
    inline void freeFramebufferFunc(QOpenGLFunctions *funcs, GLuint id)
    {
        funcs->glDeleteFramebuffers(1, &id);
    }

    /*
    void freeRenderbufferFunc(QOpenGLFunctions *funcs, GLuint id)
    {
        funcs->glDeleteRenderbuffers(1, &id);
    }

    void freeTextureFunc(QOpenGLFunctions *funcs, GLuint id)
    {
        funcs->glDeleteTextures(1, &id);
    }
    */

    inline bool _checkGLError(int line, const char *msg)
    {
        GLenum error = glGetError();
        if (error == GL_NO_ERROR)
            return false;
        unsigned clamped = qMin(unsigned(error - GL_INVALID_ENUM), 4U);
        const char *errors[] = { "GL_INVALID_ENUM", "GL_INVALID_VALUE", "GL_INVALID_OPERATION", "Unknown" };
        _RENDER_ERR(line, msg << " (" << errors[clamped] << ")");
        return true;
    }
    #define checkGLError(msg) _checkGLError(__LINE__, msg)

    inline bool _checkFramebufferStatus(int line)
    {
        auto *f = QOpenGLContext::currentContext()->extraFunctions();
        GLenum status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        switch(status) {
        case GL_NO_ERROR:
        case GL_FRAMEBUFFER_COMPLETE:
            return true;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            _RENDER_ERR(line, "Unsupported framebuffer format.");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            _RENDER_ERR(line, "Framebuffer incomplete attachment.");
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            _RENDER_ERR(line, "Framebuffer incomplete, missing attachment.");
            break;
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT
        case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT:
            _RENDER_ERR(line, "Framebuffer incomplete, duplicate attachment.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS
        case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
            _RENDER_ERR(line, "Framebuffer incomplete, attached images must have same dimensions.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_FORMATS
        case GL_FRAMEBUFFER_INCOMPLETE_FORMATS:
            _RENDER_ERR(line, "Framebuffer incomplete, attached images must have same format.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
        case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
            _RENDER_ERR(line, "Framebuffer incomplete, missing draw buffer.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
        case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
            _RENDER_ERR(line, "Framebuffer incomplete, missing read buffer.");
            break;
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            _RENDER_ERR(line, "Framebuffer incomplete, attachments must have same number of samples per pixel.");
            break;
#endif
        default:
            _RENDER_ERR(line, "An undefined error has occurred: " << status);
            break;
        }
        return false;
    }
    #define checkFramebufferStatus() _checkFramebufferStatus(__LINE__)
#endif // !FC_RENDERER_STANDALONE
}

////////////////////////////////////////////////////////

class BGFXView;

/// Active backend's shaderc target (defined below): the profile string
/// is the label a viewer matches snapshot-shipped variants against.
bool shadercTarget(std::string &platform, std::string &profile,
                          std::string &apiDir);

/// Stock shader pack loaders (BGFXRenderer.cpp). They replace
/// bgfx_utils' loadShader/loadProgram, which assert on a missing .bin
/// and then hand createShader a null memory block -- a truncated or
/// stale pack would take the process down instead of degrading. These
/// report the missing stage and return an invalid handle (and never
/// leak the sibling shader of a half-loaded pair, which
/// bgfx::createProgram's early-out does); init() turns an invalid core
/// program into a Coin fallback.
bgfx::ShaderHandle fcLoadShader(const char *name, const char *path);
bgfx::ProgramHandle fcLoadProgram(const char *vsName, const char *fsName,
                                  const char *path);

namespace Render {

/// One line a second summarising what the camera can resolve
/// (docs/FarFieldProxies.md §9). Rate-limited here rather than at the
/// call site so the caller stays a plain "measure this frame".
inline void reportCoverage(const CoverageHistogram &hist)
{
    static int64_t lastReport = 0;
    const int64_t now = bx::getHPCounter();
    const int64_t freq = bx::getHPFrequency();
    if (lastReport && now - lastReport < freq)
        return;
    lastReport = now;
    if (!hist.total)
        return;
    std::string line;
    char buf[128];
    const float *edges = CoverageHistogram::kEdges;
    for (int i = 0; i < CoverageHistogram::kBuckets; ++i) {
        if (i == 0)
            snprintf(buf, sizeof(buf), "<=%gpx:%d", double(edges[0]),
                     hist.counts[0]);
        else if (i == CoverageHistogram::kBuckets - 1)
            snprintf(buf, sizeof(buf), " >%gpx:%d",
                     double(edges[CoverageHistogram::kBuckets - 2]),
                     hist.counts[i]);
        else
            snprintf(buf, sizeof(buf), " <=%gpx:%d", double(edges[i]),
                     hist.counts[i]);
        line += buf;
    }
    // The headline is the share a far-field cut could aggregate: objects
    // the camera resolves at a handful of pixels, each still paying for a
    // whole object.
    const int tiny = hist.atOrUnder(4.0f);
    const int onScreen = hist.total - hist.offScreen - hist.noBounds;
    snprintf(buf, sizeof(buf),
             " | on-screen:%d offscreen:%d nobounds:%d | <=4px %.1f%% of on-screen",
             onScreen, hist.offScreen, hist.noBounds,
             onScreen > 0 ? 100.0 * double(tiny) / double(onScreen) : 0.0);
    // Shared code: the standalone/WASM tier has no App layer to log
    // through, but the desktop's line belongs in the report view and
    // the console capture like every other coverage diagnostic.
#ifdef FC_RENDERER_STANDALONE
    std::printf("render coverage: objects:%d %s%s\n", hist.total,
                line.c_str(), buf);
#else
    Base::Console().Message("render coverage: objects:%d %s%s\n", hist.total,
                            line.c_str(), buf);
#endif
}

/// What a frame costs the CPU against what it costs the GPU
/// (docs/FarFieldProxies.md §10.1) — the half of "where does a frame go"
/// that the stage timers of Gui/RenderTiming.h cannot see, since their
/// last stage ends at submission and the drawing happens afterwards.
///
/// §9.1 put the per-object cost of a frame at 6.85 µs by ablating the
/// object count against the frame rate, which cannot tell CPU
/// submission apart from GPU per-draw state. The difference decides
/// what an occlusion scheme has to do: one that still issues the draw
/// and lets the GPU reject it saves nothing if the cost is submission.
/// bgfx already answers it — cpuTimeBegin..cpuTimeEnd brackets the
/// render thread issuing draw commands to the graphics API, and
/// gpuTimeBegin..gpuTimeEnd is what the GPU then spent on them.
struct FrameStatsAccum {
    uint32_t frames = 0;
    double frameMs = 0.0;    ///< between two bgfx::frame calls
    double submitMs = 0.0;   ///< the render thread issuing draw commands
    double waitSubmitMs = 0.0;
    double waitRenderMs = 0.0;
    uint64_t draws = 0;
    uint64_t prims = 0;
    /// The GPU half is counted separately, and only when the frame it
    /// belongs to changes. Its timestamps lag the frame that produced
    /// them, so bgfx reports the same result for several frames
    /// running; averaging it once per frame would weight one GPU sample
    /// as though it were many, and does so unevenly with the frame rate
    /// — exactly the bias that would decide this measurement wrongly.
    uint32_t gpuSamples = 0;
    double gpuMs = 0.0;
    uint32_t gpuFrameSeen = 0;
    bool gpuFrameValid = false;
    /// ⭐ Per-view CPU submit and GPU time, keyed by bgfx view id
    /// (docs/DrawSubmission.md phase 0). The frame line says the whole
    /// frame costs 32 ms of submit and 38 ms of GPU across 30577 draws;
    /// this says *which pass* spends it, which is the difference
    /// between optimizing submission in general and deleting the pass
    /// that turns out to be half of it.
    ///
    /// ⚠️ bgfx only fills `viewStats` while `BGFX_DEBUG_PROFILER` is
    /// set, so this stays empty unless the timing switch turned it on.
    ///
    /// ⚠️⚠️ Keyed by **pass index**, resolved in the frame the sample was
    /// taken — never by raw bgfx view id. The id->pass map is rebuilt
    /// every frame from which passes are live, so a window that mixes
    /// frames with different live sets would name one pass's cost after
    /// another's. -1 collects everything unmarked (they share a sink id
    /// and are not distinguishable).
    std::map<int, std::pair<double, double>> viewMs;
    /// Wall time inside BGFXRenderer::render() and inside bgfx::frame().
    ///
    /// KEY (docs/DrawSubmission.md phase 0 item 2): bgfx's own numbers
    /// account for far less of the frame than they appear to.
    /// `cpuTimeFrame` is the whole application frame; `cpuTimeBegin/End`
    /// -- what this file calls `submitMs` -- is bgfx's *render thread*
    /// issuing GL calls, measured at 10.8ms of a 54.8ms frame. The other
    /// ~44ms is our own per-draw C++, the cull, and Coin compositing on
    /// top, and nothing measured any of it.
    ///
    /// These two seams split it: `renderMs` is everything inside our
    /// render() (so ours, including the bgfx::frame() call), and
    /// `bgfxFrameMs` is the bgfx::frame() call alone. frame - render is
    /// then everything that is not this renderer at all.
    ///
    /// ! renderMs is added by the caller *after* accumulateFrameStats
    /// has run for the same frame, so when a report falls on that frame
    /// its renderMs lands in the next window. Over a 13-19 frame window
    /// that is under a frame's worth and it does not accumulate.
    double renderMs = 0.0;
    double bgfxFrameMs = 0.0;
    /// The size the scene was actually rasterized at.
    ///
    /// ⚠️ Not bgfx::Stats::width/height: those are the *default*
    /// backbuffer, which on the desktop tier is a small dummy nothing
    /// draws into — every content view targets the view's own
    /// framebuffer. Read from stats it sits at its init size forever,
    /// and a resolution sweep looks as though the resolution never
    /// changed. Asking Qt is no better: findChildren() gives the size
    /// of *a* GL widget, not necessarily the one that drew.
    uint16_t width = 0;
    uint16_t height = 0;
};

/// Accumulate one frame of backend statistics. Cheap enough to run
/// unconditionally while the switch is on: getStats() hands back a
/// pointer to state bgfx maintains anyway.
static void accumulateFrameStats(FrameStatsAccum &acc, uint16_t sceneWidth,
                                 uint16_t sceneHeight,
                                 const std::function<int(uint16_t)> &resolvePass)
{
    const bgfx::Stats *s = bgfx::getStats();
    if (!s || s->cpuTimerFreq <= 0)
        return;
    const double toMs = 1000.0 / double(s->cpuTimerFreq);
    ++acc.frames;
    acc.frameMs += double(s->cpuTimeFrame) * toMs;
    acc.submitMs += double(s->cpuTimeEnd - s->cpuTimeBegin) * toMs;
    acc.waitSubmitMs += double(s->waitSubmit) * toMs;
    acc.waitRenderMs += double(s->waitRender) * toMs;
    acc.draws += s->numDraw;
    for (int i = 0; i < bgfx::Topology::Count; ++i)
        acc.prims += s->numPrims[i];
    acc.width = sceneWidth;
    acc.height = sceneHeight;
    // Per-view, while the profiler flag is on. The GPU half of a view
    // is only meaningful when its timer resolved, same as the frame's.
    for (uint16_t i = 0; i < s->numViews; ++i) {
        const bgfx::ViewStats &v = s->viewStats[i];
        auto &slot = acc.viewMs[resolvePass(v.view)];
        slot.first += double(v.cpuTimeEnd - v.cpuTimeBegin) * toMs;
        if (s->gpuTimerFreq > 0 && v.gpuTimeEnd > v.gpuTimeBegin)
            slot.second += double(v.gpuTimeEnd - v.gpuTimeBegin) * 1000.0
                           / double(s->gpuTimerFreq);
    }
    // ! gpuTimerFreq alone is NOT a validity gate: bgfx's GL backend
    // publishes a fixed 1e9 whether or not the driver ever resolved a
    // timestamp pair, so a context whose timer queries never come back
    // reports a *measured zero* rather than "not measured". Requiring
    // the pair to be ordered is what tells the two apart -- and the
    // difference decides whether a frame's GPU cost was small or was
    // never asked. (Mesa d3d12 under WSLg advertises ARB_timer_query
    // and resolves nothing, which is how this was found.)
    if (s->gpuTimerFreq > 0 && s->gpuTimeEnd > s->gpuTimeBegin
            && (!acc.gpuFrameValid || s->gpuFrameNum != acc.gpuFrameSeen)) {
        acc.gpuFrameValid = true;
        acc.gpuFrameSeen = s->gpuFrameNum;
        acc.gpuMs += double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0
                     / double(s->gpuTimerFreq);
        ++acc.gpuSamples;
    }
}

/// Report the means accumulated since the last line and start over.
///
/// The headline is the per-draw pair: submission microseconds against
/// GPU microseconds for the same draw, which is the comparison §9.1
/// could not make. Frame and wait times are reported beside them
/// because a frame can be bound by neither — waiting on the swap or on
/// the other thread is a third answer, and one that would make both
/// per-draw numbers look small for the wrong reason.
static void reportFrameStats(FrameStatsAccum &acc)
{
    if (!acc.frames)
        return;
    const double frames = double(acc.frames);
    const double drawsPerFrame = double(acc.draws) / frames;
    const double submitMs = acc.submitMs / frames;
    const bool haveGpu = acc.gpuSamples > 0;
    const double gpuMs = haveGpu ? acc.gpuMs / double(acc.gpuSamples) : 0.0;
    char perDraw[128];
    if (drawsPerFrame > 0.0) {
        if (haveGpu)
            snprintf(perDraw, sizeof(perDraw),
                     "submit %.2fus gpu %.2fus", 1000.0 * submitMs / drawsPerFrame,
                     1000.0 * gpuMs / drawsPerFrame);
        else
            snprintf(perDraw, sizeof(perDraw), "submit %.2fus gpu n/a",
                     1000.0 * submitMs / drawsPerFrame);
    }
    else
        snprintf(perDraw, sizeof(perDraw), "no draws");
    char gpuText[64];
    if (haveGpu)
        snprintf(gpuText, sizeof(gpuText), "%.2fms(n=%u)", gpuMs, acc.gpuSamples);
    else
        snprintf(gpuText, sizeof(gpuText), "n/a");
    // Primitives per frame and the backbuffer they were rasterized into
    // are what separate the three things GPU time can be: per-draw
    // state, vertex throughput, and fill. One frame cannot tell them
    // apart, but two scenes with different ratios can, and neither
    // ratio is knowable without both numbers on the line.
    const double primsPerFrame = double(acc.prims) / frames;
    // Where the frame's CPU actually goes. `submit` above is only bgfx's
    // render thread; these three split the whole frame into this
    // renderer's own C++, the bgfx call it ends in, and everything that
    // is not this renderer (Coin's composite, Qt, the app). Without the
    // last one the largest term in the frame has no instrument at all.
    const double renderMs = acc.renderMs / frames;
    const double outsideMs = acc.frameMs / frames - renderMs;
    char buf[640];
    snprintf(buf, sizeof(buf),
             "render frame: frames:%u %ux%u frame %.2fms submit %.2fms gpu %s | "
             "draws %.0f prims %.0f (%.0f/draw) | per-draw %s | wait submit %.2fms "
             "render %.2fms | cpu ours %.2fms (bgfx::frame %.2fms) outside %.2fms\n",
             acc.frames, unsigned(acc.width), unsigned(acc.height),
             acc.frameMs / frames, submitMs, gpuText, drawsPerFrame,
             primsPerFrame, drawsPerFrame > 0.0 ? primsPerFrame / drawsPerFrame : 0.0,
             perDraw, acc.waitSubmitMs / frames, acc.waitRenderMs / frames,
             renderMs, acc.bgfxFrameMs / frames, outsideMs);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
    acc = FrameStatsAccum();
}

/// The bgfx reset flags every init/resize path uses.
///
/// MAXANISOTROPY is required for BGFX_SAMPLER_*_ANISOTROPIC to have any
/// effect: bgfx only raises its internal m_maxAnisotropy (and thus
/// honors the per-sampler anisotropic flags) when this reset bit is set,
/// otherwise the flags are silently ignored.
///
/// VSYNC is on unless FC_BGFX_NO_VSYNC says otherwise. That escape
/// hatch exists because vsync makes the renderer unmeasurable: on a
/// 75Hz panel every leg of a timing run returns 13.34ms to three
/// decimals whatever the scene costs, so a wall-clock A/B of any render
/// change reads as exactly zero. Read once -- bgfx::init happens once
/// per process and the resize path has to agree with it.
inline uint32_t bgfxResetFlags()
{
    static const bool noVsync = (getenv("FC_BGFX_NO_VSYNC") != nullptr);
    return (noVsync ? 0u : uint32_t(BGFX_RESET_VSYNC))
        | uint32_t(BGFX_RESET_MAXANISOTROPY);
}

/// Whether the once-a-second frame-cost line is due. Unlike the
/// far-field readouts below, what it reports is accumulated on every
/// frame and only *printed* on a tick, so this gates the printing.
static bool frameStatsDue()
{
    static int64_t lastReport = 0;
    const int64_t now = bx::getHPCounter();
    if (!lastReport) {
        lastReport = now;
        return false;
    }
    if (now - lastReport < bx::getHPFrequency())
        return false;
    lastReport = now;
    return true;
}

/// Whether the once-a-second far-field readout is due. Split from the
/// report because, unlike the coverage histogram, the measurement it
/// gates is not free: it builds a partition over every drawn instance,
/// and a rate limiter inside the report would pay for that on frames
/// nothing is printed.
static bool proxyCutDue()
{
    static int64_t lastReport = 0;
    const int64_t now = bx::getHPCounter();
    const int64_t freq = bx::getHPFrequency();
    if (lastReport && now - lastReport < freq)
        return false;
    lastReport = now;
    return true;
}

/// The same for the generation readout, which builds meshes rather than
/// projecting boxes and can take seconds over a large model.
///
/// Hence the pair: the gap is measured from when the last report
/// *finished*. Stamped on the way in, a report costing longer than the
/// interval would be due again the moment it returned, and the viewer
/// would spend every frame inside the measurement.
static int64_t proxyGenLastReport = 0;

static bool proxyGenDue()
{
    const int64_t now = bx::getHPCounter();
    if (proxyGenLastReport
            && now - proxyGenLastReport < 5 * bx::getHPFrequency())
        return false;
    proxyGenLastReport = now;
    return true;
}

static void proxyGenReported()
{
    proxyGenLastReport = bx::getHPCounter();
}

/// What a far-field cut would cost this camera, generating nothing
/// (docs/FarFieldProxies.md §11.1) — the measurement phase 1 exists to
/// produce, and the gate on whether phase 2 is worth building.
///
/// Reported at several tolerances rather than one, because the draw
/// count against tolerance is a step function whose floor is the tree
/// bottoming out: a single operating point cannot be told apart from
/// having chosen a bad one. \a buildMs is reported for its own sake —
/// phase 3 has to pay this on the plan's schedule, so its size is a
/// design input rather than an aside.
static void reportProxyCut(const Render::ProxyHierarchy &index,
                           const float *V, const float *P,
                           float viewportHeightPx, double buildMs)
{
    const auto stats = index.stats();
    if (!stats.instances)
        return;
    std::string line;
    char buf[256];
    static const float kTolerances[] = {1.0f, 4.0f, 16.0f, 64.0f};
    // Draws AND primitives at each tolerance. Draws alone were what
    // this line reported for phase 1, and draws were then measured to
    // cost nothing on this scene -- the frame is GPU-bound and the GPU
    // is geometry-bound (docs/DrawSubmission.md). `covered` is the
    // primitives a proxy would stand in for: the ceiling on the saving,
    // since the proxy's own primitives come off it and are not knowable
    // without generating (11.1c).
    std::string prims;
    std::string bytesLine;
    for (float tol : kTolerances) {
        Render::ProxyCut cut;
        index.selectCut(V, P, viewportHeightPx, tol, cut);
        snprintf(buf, sizeof(buf), " %gpx:%u(%u+%u)", double(tol),
                 cut.drawCount, cut.proxyDraws, unsigned(cut.exact.size()));
        line += buf;
        const double total = double(cut.exactPrims + cut.coveredPrims);
        snprintf(buf, sizeof(buf), " %gpx:%.2fM exact+%.2fM covered(%.0f%%)",
                 double(tol), double(cut.exactPrims) / 1e6,
                 double(cut.coveredPrims) / 1e6,
                 total > 0.0 ? 100.0 * double(cut.coveredPrims) / total : 0.0);
        prims += buf;
        // The other axis, and the one that decides whether a model
        // opens at all: what the cut lets stop being resident.
        //
        // Counted per distinct MESH, not per instance, and only where
        // NO exact instance still references it. Instances share meshes
        // heavily here (495 instanced submits stand in for 5432 rows),
        // so summing bytes over covered instances would overstate the
        // saving several times over, and a mesh with one exact user
        // left stays resident in full.
        std::unordered_set<const void *> held, freed;
        const auto &insts = index.instances();
        for (uint32_t idx : cut.exact) {
            if (insts[idx].sourceTag)
                held.insert(insts[idx].sourceTag);
        }
        uint64_t freedBytes = 0, heldBytes = 0;
        std::vector<uint32_t> sub;
        for (int node : cut.proxyNodes) {
            sub.clear();
            index.subtreeInstances(node, sub);
            for (uint32_t idx : sub) {
                const auto &inst = insts[idx];
                if (!inst.sourceTag || held.count(inst.sourceTag))
                    continue;
                if (freed.insert(inst.sourceTag).second)
                    freedBytes += inst.meshBytes;
            }
        }
        std::unordered_set<const void *> counted;
        for (uint32_t idx : cut.exact) {
            const auto &inst = insts[idx];
            if (inst.sourceTag && counted.insert(inst.sourceTag).second)
                heldBytes += inst.meshBytes;
        }
        const double totalBytes = double(freedBytes + heldBytes);
        snprintf(buf, sizeof(buf),
                 " %gpx:%.1fMB held+%.1fMB freed(%.0f%%) over %zu meshes",
                 double(tol), double(heldBytes) / 1048576.0,
                 double(freedBytes) / 1048576.0,
                 totalBytes > 0.0 ? 100.0 * double(freedBytes) / totalBytes : 0.0,
                 freed.size());
        bytesLine += buf;
    }
    // The distributions that size the partition (§3.2): how many
    // instances a cell holds decides both the size of a pop and how much
    // draws exactly when the cut is forced down, and how many materials
    // it holds is the fan-out below the cut (§5.1).
    std::string levels;
    for (size_t l = 0; l < stats.byLevel.size(); ++l) {
        const auto &ls = stats.byLevel[l];
        if (!ls.nodes)
            continue;
        snprintf(buf, sizeof(buf), " L%u:%un/%ur/max%u/mb%.1f", unsigned(l),
                 ls.nodes, ls.residents, ls.subtreeMax, ls.bucketsMean);
        levels += buf;
    }
    snprintf(buf, sizeof(buf),
             " | nodes:%u depth:%u buckets:%u | build %.1fms",
             stats.nodes, stats.depth, stats.distinctBuckets, buildMs);
#ifdef FC_RENDERER_STANDALONE
    std::printf("render proxycut: instances:%u draws@tol%s%s\n%s\n%s\n%s\n",
                stats.instances, line.c_str(), buf, prims.c_str(),
                bytesLine.c_str(), levels.c_str());
#else
    Base::Console().Message(
            "render proxycut: instances:%u draws@tol%s%s\n", stats.instances,
            line.c_str(), buf);
    Base::Console().Message("render proxycut prims@tol:%s\n", prims.c_str());
    Base::Console().Message("render proxycut bytes@tol:%s\n",
                            bytesLine.c_str());
    Base::Console().Message("render proxycut levels:%s\n", levels.c_str());
#endif
}

/// What the cut estimate above cannot answer, because nothing it
/// measures has been built (docs/FarFieldProxies.md §11.1c).
///
/// The estimate descends by a node's projected *extent*, which asks
/// that a whole merged blob be smaller than the tolerance — far
/// stricter than what a proxy actually commits, since a cell of twenty
/// screws sixty pixels across decimates into a mesh erring a pixel or
/// two. The conversion between the two is one number per node, the
/// error against the extent, and it can only be had by generating: so
/// this samples the nodes that cut stops on, merges each (cell,
/// material) group for real, decimates it at several grid
/// subdivisions, and reports the ratio.
///
/// Three other things fall out of generating that no estimate could
/// have produced, and all three are reported beside it: what the
/// triangles cost against the geometry instancing shares today (§7.1),
/// how much surface *survives* — clustering deletes what is smaller
/// than a cell rather than shrinking it — and how long a proxy takes to
/// build.
struct ProxyGenTotals {
    uint32_t proxies = 0;      ///< generated
    uint32_t refused = 0;      ///< nothing left to draw at this grid
    uint32_t rated = 0;        ///< contributed an error ratio
    uint64_t members = 0;
    uint64_t sourceTriangles = 0;
    uint64_t uniqueTriangles = 0;
    uint64_t proxyTriangles = 0;
    /// Bounds anything that would stand in for the deleted geometry
    /// per *cell* rather than per member — a representative exists for
    /// every occupied cell whether or not a triangle survived on it.
    uint64_t proxyVertices = 0;
    uint64_t collapsedMembers = 0;
    /// What it costs to carry the deleted members rather than lose
    /// them, both ways round (11.1c): a box per collapsed member, or a
    /// box per grid cell holding deleted content. Both are covers of
    /// the same points, so the triangles are the price and the volume
    /// is how loosely each one holds them.
    Render::StandInStats perMember;
    Render::StandInStats perCell;
    double standInMs = 0.0;
    double sourceArea = 0.0;
    double proxyArea = 0.0;
    double ratioSum = 0.0;
    double rmsRatioSum = 0.0;
    float ratioMax = 0.0f;
    double ms = 0.0;
};

/// Sum one proxy's stand-in numbers into the totals for its grid.
static void accumulate(Render::StandInStats &total,
                       const Render::StandInStats &one)
{
    total.boxes += one.boxes;
    total.triangles += one.triangles;
    total.members += one.members;
    total.named += one.named;
    total.cells += one.cells;
    total.sharedCells += one.sharedCells;
    total.volume += one.volume;
    total.area += one.area;
    total.deletedArea += one.deletedArea;
    total.ms += one.ms;
}

static void reportProxyGen(const Render::ProxyHierarchy &index,
                           const Render::DrawCallList &draws, const float *V,
                           const float *P, float viewportHeightPx)
{
    // The tolerance the cut estimate showed most aggregation at, so
    // that this measures the operating point in question rather than
    // one nothing would use.
    static const float kTolerancePx = 64.0f;
    // Generating for every node on the cut is generating the whole
    // model. Both bounds exist to keep a debug readout from becoming a
    // several-second stall, and both are reported: a measurement that
    // silently drops most of its work reads as coverage it did not have.
    static const uint32_t kMaxNodes = 24;
    static const uint64_t kTriangleBudget = 2000000;
    // Cell edges of the node's own cell divided by these — powers of
    // two, so that every level's decimation grid remains a refinement
    // of the level above it (§3.2, and SimplifyOptions::anchor).
    static const uint32_t kSubdivisions[] = {4, 8, 16};
    static const size_t kGrids = sizeof(kSubdivisions) / sizeof(*kSubdivisions);

    Render::ProxyCut cut;
    index.selectCut(V, P, viewportHeightPx, kTolerancePx, cut);
    if (cut.proxyNodes.empty())
        return;

    ProxyGenTotals totals[kGrids];
    const uint32_t stride = std::max<uint32_t>(
            1, uint32_t(cut.proxyNodes.size()) / kMaxNodes);
    uint32_t sampledNodes = 0;
    uint32_t merges = 0;
    uint32_t overBudget = 0;
    uint32_t belowMinMerge = 0;
    uint32_t nonTriangleBuckets = 0;
    uint64_t mergedTriangles = 0;
    double mergeMs = 0.0;
    std::vector<uint32_t> subtree;
    std::vector<Render::ProxyMember> members;

    for (size_t i = 0;
         i < cut.proxyNodes.size() && sampledNodes < kMaxNodes; i += stride) {
        const int nodeIndex = cut.proxyNodes[i];
        const Render::ProxyNode &node = index.nodes()[size_t(nodeIndex)];
        subtree.clear();
        index.subtreeInstances(nodeIndex, subtree);
        ++sampledNodes;
        // The proxy's unit is (cell, material bucket), so that nothing
        // is ever averaged across materials (§5.1).
        std::map<uint64_t, std::vector<uint32_t>> byBucket;
        for (uint32_t inst : subtree)
            byBucket[index.instances()[size_t(inst)].materialBucket]
                .push_back(inst);
        const float cellEdge = node.cellMax[0] - node.cellMin[0];
        if (!(cellEdge > 0.0f))
            continue;

        for (const auto &bucket : byBucket) {
            members.clear();
            bool nonTriangle = false;
            for (uint32_t inst : bucket.second) {
                const uint32_t row = index.instances()[size_t(inst)].drawIndex;
                if (row >= draws.size())
                    continue;
                const Render::DrawCall &draw = draws[row];
                // Lines and points carry their own materials, so they
                // are buckets of their own and stay exact; a stand-in
                // is not geometry at all.
                if (draw.material.type != Render::Material::Triangle) {
                    nonTriangle = true;
                    continue;
                }
                if (draw.standIn || !draw.mesh)
                    continue;
                Render::ProxyMember member;
                member.mesh = draw.mesh.get();
                member.model = draw.identity ? nullptr : draw.model;
                member.indexStart = draw.indexStart;
                member.indexCount = draw.indexCount;
                member.objectKey = draw.objectKey;
                members.push_back(member);
            }
            if (nonTriangle)
                ++nonTriangleBuckets;
            if (members.size() < index.params().minMerge) {
                ++belowMinMerge;
                continue;
            }
            // What this merge would cost, before paying it. A node high
            // on the cut covers its whole subtree, so one group can be
            // most of the model, and a budget checked only afterwards
            // would already have spent it.
            uint64_t wouldMerge = 0;
            for (const Render::ProxyMember &member : members) {
                wouldMerge += uint64_t(member.indexCount > 0
                                               ? member.indexCount
                                               : member.mesh
                                                     ->numTriangleIndices)
                    / 3;
            }
            if (mergedTriangles + wouldMerge > kTriangleBudget) {
                ++overBudget;
                continue;
            }

            Render::SimplifiedMesh merged;
            Render::ProxyMeshStats mergeStats;
            if (!Render::mergeProxyMembers(members, merged, nullptr,
                                           &mergeStats))
                continue;
            ++merges;
            mergedTriangles += mergeStats.sourceTriangles;
            mergeMs += mergeStats.mergeMs;

            for (size_t g = 0; g < kGrids; ++g) {
                Render::ProxyMeshParams params;
                // Anchoring at the node's own cell corner is anchoring
                // at the level grid: a cell corner is on every grid the
                // level subdivides into.
                params.anchor[0] = node.cellMin[0];
                params.anchor[1] = node.cellMin[1];
                params.anchor[2] = node.cellMin[2];
                params.cellSize = cellEdge / float(kSubdivisions[g]);
                Render::SimplifiedMesh proxy;
                Render::ProxyMeshStats stats = mergeStats;
                const bool made =
                    Render::decimateProxyMesh(merged, params, proxy, &stats);
                ProxyGenTotals &t = totals[g];
                t.members += stats.members;
                t.sourceTriangles += stats.sourceTriangles;
                t.uniqueTriangles += stats.uniqueTriangles;
                t.proxyTriangles += stats.proxyTriangles;
                t.proxyVertices += stats.proxyVertices;
                t.collapsedMembers += stats.collapsedMembers;
                t.sourceArea += stats.sourceArea;
                t.proxyArea += stats.proxyArea;
                t.ms += stats.simplifyMs;
                // What would stand in for the members this grid
                // deleted, measured both ways on the same merge --
                // which is the only way the two can be compared,
                // since a different merge deletes a different set.
                Render::SimplifiedMesh standIn;
                Render::StandInStats perMember, perCell;
                Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerMember, standIn,
                                      &perMember);
                Render::buildStandIns(merged, proxy, params,
                                      Render::StandInMode::PerCell, standIn,
                                      &perCell);
                accumulate(t.perMember, perMember);
                accumulate(t.perCell, perCell);
                t.standInMs += perMember.ms + perCell.ms;
                if (!made) {
                    ++t.refused;
                    continue;
                }
                ++t.proxies;
                if (stats.extent > 0.0f) {
                    const float ratio = stats.maxError / stats.extent;
                    t.ratioSum += ratio;
                    t.rmsRatioSum += stats.rmsError / stats.extent;
                    t.ratioMax = std::max(t.ratioMax, ratio);
                    ++t.rated;
                }
            }
        }
    }

    // How much geometry the scene shares at all, so that the per-proxy
    // instancing gate below can be read. Without it, "the members of a
    // proxy share almost nothing" cannot be told apart from "this
    // measurement could not see sharing if there were any".
    // Counted two ways, because they answer different questions. A
    // cache id is what the merge dedupes by; a source tag is the
    // geometry *node* behind it, which colour variants of one shape
    // share. Equal counts mean the assembly really does hold that many
    // distinct shapes; a sourceTag count well below the cacheId count
    // would mean sharing exists and the merge is blind to it.
    std::set<std::pair<uint64_t, const void *>> sceneMeshes;
    std::set<const void *> sceneSources;
    uint32_t triangleDraws = 0;
    for (const Render::DrawCall &draw : draws) {
        if (draw.material.type != Render::Material::Triangle || draw.standIn
                || !draw.mesh)
            continue;
        ++triangleDraws;
        sceneMeshes.emplace(draw.mesh->cacheId,
                            draw.mesh->cacheId ? nullptr
                                               : (const void *)draw.mesh.get());
        if (draw.mesh->sourceTag)
            sceneSources.insert(draw.mesh->sourceTag);
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
             "render proxygen: tol %gpx nodes:%u/%u merges:%u src %.2fMtri "
             "in %.0fms | skipped budget:%u single:%u | nontri buckets:%u | "
             "scene %u tri draws over %u meshes / %u sources\n",
             double(kTolerancePx), sampledNodes,
             unsigned(cut.proxyNodes.size()), merges,
             double(mergedTriangles) / 1e6, mergeMs, overBudget,
             belowMinMerge, nonTriangleBuckets, triangleDraws,
             unsigned(sceneMeshes.size()), unsigned(sceneSources.size()));
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
    for (size_t g = 0; g < kGrids; ++g) {
        const ProxyGenTotals &t = totals[g];
        const double rated = t.rated ? double(t.rated) : 1.0;
        snprintf(buf, sizeof(buf),
                 "render proxygen 1/%u cell: err/extent mean %.4f worst %.4f "
                 "rms %.4f | tri %.2fM->%.3fM %.1fx (instanced %.2fM, %.1fx) "
                 "| area %.0f%% | lost %llu/%llu members, %llu proxy verts "
                 "| %u proxies %u refused | %.0fms\n",
                 kSubdivisions[g], t.ratioSum / rated, double(t.ratioMax),
                 t.rmsRatioSum / rated,
                 double(t.sourceTriangles) / 1e6,
                 double(t.proxyTriangles) / 1e6,
                 t.proxyTriangles ? double(t.sourceTriangles)
                                        / double(t.proxyTriangles)
                                  : 0.0,
                 double(t.uniqueTriangles) / 1e6,
                 t.proxyTriangles ? double(t.uniqueTriangles)
                                        / double(t.proxyTriangles)
                                  : 0.0,
                 t.sourceArea > 0.0 ? 100.0 * t.proxyArea / t.sourceArea : 0.0,
                 (unsigned long long)t.collapsedMembers,
                 (unsigned long long)t.members,
                 (unsigned long long)t.proxyVertices, t.proxies, t.refused,
                 t.ms);
#ifdef FC_RENDERER_STANDALONE
        std::printf("%s", buf);
#else
        Base::Console().Message("%s", buf);
#endif
        // The deleted mass, carried both ways. Volumes are directly
        // comparable because both are covers of the same points, so
        // the looser one is the one committing more space to hold
        // them; the named counts say what a pick still resolves to.
        snprintf(buf, sizeof(buf),
                 "render standin 1/%u cell: per-member %u boxes %u tri "
                 "vol %.4g area %.4g named %u/%u | per-cell %u boxes %u tri "
                 "vol %.4g area %.4g named %u/%u over %u cells (%u shared) "
                 "| deleted area %.4g vs proxy %.4g | %.1fms\n",
                 kSubdivisions[g], t.perMember.boxes,
                 t.perMember.triangles, t.perMember.volume,
                 t.perMember.area, t.perMember.named, t.perMember.members,
                 t.perCell.boxes, t.perCell.triangles,
                 t.perCell.volume, t.perCell.area, t.perCell.named,
                 t.perCell.members, t.perCell.cells, t.perCell.sharedCells,
                 t.perMember.deletedArea, t.proxyArea, t.standInMs);
#ifdef FC_RENDERER_STANDALONE
        std::printf("%s", buf);
#else
        Base::Console().Message("%s", buf);
#endif
    }
}

/// What building the proxies bottom-up costs against building each node
/// from source, measured on the nodes a cut actually stops on
/// (docs/FarFieldProxies.md section 7.1).
///
/// Both passes generate the same set of nodes and differ only in what
/// each merge is fed, so the difference between them is the geometric
/// series the bottom-up rule is supposed to buy -- and the error and
/// area columns say what it costs in fidelity to merge a decimation of
/// a decimation rather than the geometry itself.
static void reportProxyStore(const Render::ProxyHierarchy &index,
                             const Render::DrawCallList &draws, const float *V,
                             const float *P, float viewportHeightPx)
{
    static const float kTolerancePx = 64.0f;
    // A generation pass over a node is that node's whole subtree, so a
    // handful of them is a large fraction of the model. Both bounds are
    // reported: a measurement that silently drops most of its work
    // reads as coverage it did not have.
    static const uint32_t kMaxNodes = 4;
    static const uint64_t kTriangleBudget = 3000000;

    Render::ProxyCut cut;
    index.selectCut(V, P, viewportHeightPx, kTolerancePx, cut);
    if (cut.proxyNodes.empty())
        return;

    Render::ProxyStoreStats up, src;
    double upperror = 0.0, srcerror = 0.0;
    double upkept = 0.0, srckept = 0.0;
    double uparea = 0.0, srcarea = 0.0;
    uint32_t sampled = 0;
    const uint32_t stride =
        std::max<uint32_t>(1, uint32_t(cut.proxyNodes.size()) / kMaxNodes);
    const auto accumulate = [](Render::ProxyStoreStats &total,
                               const Render::ProxyStoreStats &one) {
        total.nodes += one.nodes;
        total.entries += one.entries;
        total.refused += one.refused;
        total.overBudget += one.overBudget;
        total.mergedTriangles += one.mergedTriangles;
        total.sourceTriangles += one.sourceTriangles;
        total.proxyTriangles += one.proxyTriangles;
        total.standInTriangles += one.standInTriangles;
        total.maxLevelSpan = std::max(total.maxLevelSpan, one.maxLevelSpan);
        total.ms += one.ms;
    };
    // The top entries of a pass are what the cut would draw at that
    // node, so they are where the accumulated error and the area still
    // represented are read.
    const auto top = [](const Render::ProxyStore &store, double &error,
                        double &kept, double &area) {
        uint32_t level = 0xffffffffu;
        for (const Render::ProxyEntry &entry : store.entries())
            level = std::min(level, entry.level);
        for (const Render::ProxyEntry &entry : store.entries()) {
            if (entry.level != level)
                continue;
            error = std::max(error, double(entry.errorRatio));
            kept += entry.keptArea;
            area += entry.sourceArea;
        }
    };

    for (size_t i = 0;
         i < cut.proxyNodes.size() && sampled < kMaxNodes; i += stride) {
        const int node = cut.proxyNodes[i];
        ++sampled;
        Render::ProxyStore bottomUp;
        Render::ProxyGenOptions opts;
        opts.triangleBudget = kTriangleBudget;
        if (bottomUp.generate(index, node, draws, opts)) {
            accumulate(up, bottomUp.stats());
            top(bottomUp, upperror, upkept, uparea);
        }
        Render::ProxyStore fromSource;
        Render::ProxyGenOptions control = opts;
        control.fromSource = true;
        if (fromSource.generate(index, node, draws, control)) {
            accumulate(src, fromSource.stats());
            top(fromSource, srcerror, srckept, srcarea);
        }
    }

    char buf[512];
    snprintf(buf, sizeof(buf),
             "render proxystore: tol %gpx nodes:%u/%u | bottom-up merged "
             "%.2fMtri over %u nodes / %u entries in %.0fms, depth %u "
             "| from source %.2fMtri over %u nodes / %u entries in %.0fms "
             "| skipped budget:%u/%u refused:%u/%u\n",
             double(kTolerancePx), sampled, unsigned(cut.proxyNodes.size()),
             double(up.mergedTriangles) / 1e6, up.nodes, up.entries, up.ms,
             up.maxLevelSpan, double(src.mergedTriangles) / 1e6, src.nodes,
             src.entries, src.ms, up.overBudget, src.overBudget, up.refused,
             src.refused);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
    snprintf(buf, sizeof(buf),
             "render proxystore drawn: bottom-up %llu proxy + %llu standin "
             "tri, worst err/extent %.4f, area kept %.0f%% | from source "
             "%llu + %llu tri, worst err/extent %.4f, area kept %.0f%%\n",
             (unsigned long long)up.proxyTriangles,
             (unsigned long long)up.standInTriangles, upperror,
             uparea > 0.0 ? 100.0 * upkept / uparea : 0.0,
             (unsigned long long)src.proxyTriangles,
             (unsigned long long)src.standInTriangles, srcerror,
             srcarea > 0.0 ? 100.0 * srckept / srcarea : 0.0);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
}

/// What the exactly drawn part of a cut *is* (docs/FarFieldProxies.md
/// section 11.1g).
///
/// The priced readout below measured that a proxy costs 4-9% of what it
/// replaces, and that two thirds of the visible primitives are still
/// exact at a 4px tolerance. The second number is the ceiling and the
/// first says tuning proxies cannot move it, so the next question is
/// what that exact mass is: near field the cut was right to descend
/// into, or geometry nothing ever offered to aggregate.
///
/// Reported beside it, because it is the same question asked from the
/// other side: what a stopped node covers and no generated proxy
/// draws. A cut counts every instance below a node it stops on as
/// covered, but generation works per material bucket and produces
/// nothing for some -- lines and points by design, a bucket the
/// decimation refused by accident. Those primitives are neither drawn
/// exactly nor drawn by a proxy, and a saving that counts them as
/// removed is counting a hole.
static void reportProxyExact(const Render::ProxyHierarchy &index,
                             const Render::ProxyStore &store,
                             const Render::ProxyCut &cut, const float *V,
                             const float *P, float viewportHeightPx,
                             float tolerancePx,
                             const std::vector<Render::ProxyNodeCost> &costs)
{
    Render::ProxyExactBreakdown ex;
    index.explainExact(cut, V, P, viewportHeightPx, tolerancePx, &costs, ex);

    char buf[512];
    const double total = double(ex.total.prims);
    const auto pct = [&](uint64_t v) {
        return total > 0.0 ? 100.0 * double(v) / total : 0.0;
    };
    snprintf(buf, sizeof(buf),
             "render proxyexact %gpx: %.2fM exact over %u inst = resolvable "
             "%.2fM (%.0f%%) | no proxy %.2fM (%.0f%%) | too few members "
             "%.2fM (%.0f%%) | level mean %.1f max %u\n",
             double(tolerancePx), total / 1e6, ex.total.instances,
             double(ex.byReason[Render::ProxyExactBreakdown::Resolvable].prims)
                 / 1e6,
             pct(ex.byReason[Render::ProxyExactBreakdown::Resolvable].prims),
             double(ex.byReason[Render::ProxyExactBreakdown::NoProxy].prims)
                 / 1e6,
             pct(ex.byReason[Render::ProxyExactBreakdown::NoProxy].prims),
             double(ex.byReason[Render::ProxyExactBreakdown::TooFewMembers]
                        .prims) / 1e6,
             pct(ex.byReason[Render::ProxyExactBreakdown::TooFewMembers].prims),
             ex.meanLevel, ex.maxLevel);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif

    // The cross-cut, and the one that says whether any of it is
    // addressable: an instance projecting to less than the tolerance is
    // detail nothing merged, one projecting to ten times it is near
    // field however it got there.
    std::string sizes;
    static const char *kBinNames[] = {"<=1x", "<=4x", "<=16x", ">16x"};
    for (int b = 0; b < Render::ProxyExactBreakdown::SizeBins; ++b) {
        snprintf(buf, sizeof(buf), " %s:%.2fM/%u(res %.2fM,gap %.2fM)",
                 kBinNames[b], double(ex.bySize[b].prims) / 1e6,
                 ex.bySize[b].instances,
                 double(ex.bins[Render::ProxyExactBreakdown::Resolvable][b]
                            .prims) / 1e6,
                 double(ex.bins[Render::ProxyExactBreakdown::NoProxy][b].prims)
                     / 1e6);
        sizes += buf;
    }
    snprintf(buf, sizeof(buf), "render proxyexact %gpx by own size:%s\n",
             double(tolerancePx), sizes.c_str());
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif

    // What the cut counted as covered and nothing draws. Asked per
    // (node, bucket) against the store, which is the pair generation
    // keys on, so a bucket with no entry is exactly a bucket no proxy
    // stands for.
    uint64_t holePrims = 0;
    uint32_t holeInstances = 0, holeBuckets = 0, holeNodes = 0;
    std::vector<uint32_t> sub;
    const auto &insts = index.instances();
    for (int ni : cut.proxyNodes) {
        const auto &node = index.nodes()[size_t(ni)];
        sub.clear();
        index.subtreeInstances(ni, sub);
        std::unordered_set<uint64_t> missing;
        for (uint32_t idx : sub) {
            const auto &inst = insts[idx];
            // What the cut already draws itself is not a hole: an edge
            // below a stopped node is in the exact list by design
            // (11.1g). What is left is the generation gap proper --
            // a triangle bucket that should have had a proxy.
            if (!inst.mergeable
                    || store.find(node.id, inst.materialBucket))
                continue;
            missing.insert(inst.materialBucket);
            holeInstances += 1;
            holePrims += inst.primCount;
        }
        if (!missing.empty()) {
            ++holeNodes;
            holeBuckets += uint32_t(missing.size());
        }
    }
    // And the net figure the priced line should have quoted: what a
    // cut draws is what it draws exactly, plus its proxies, plus
    // everything it counted as covered that no proxy stands for --
    // because that has to be drawn by somebody.
    const uint64_t honest = cut.exactPrims + cut.proxyPrims + holePrims;
    // The denominator is what the frame would draw with no cut at all,
    // so the gated edges belong in it: they are geometry the cut
    // removed, not geometry that was never there.
    const uint64_t visible =
        cut.exactPrims + cut.coveredPrims + cut.gatedPrims;
    snprintf(buf, sizeof(buf),
             "render proxyexact %gpx uncovered: %u of %zu stopped nodes leave "
             "%u triangle buckets with no proxy -- %u inst / %llu prims "
             "(%.1f%% of %llu covered) | below a stopped node: %u inst / "
             "%llu prims drawn exactly, %u / %llu gated by the element "
             "contract | net %llu of %llu, %.2fx\n",
             double(tolerancePx), holeNodes, cut.proxyNodes.size(),
             holeBuckets, holeInstances, (unsigned long long)holePrims,
             cut.coveredPrims ? 100.0 * double(holePrims)
                     / double(cut.coveredPrims)
                 : 0.0,
             (unsigned long long)cut.coveredPrims,
             cut.unmergeableInstances,
             (unsigned long long)cut.unmergeablePrims, cut.gatedInstances,
             (unsigned long long)cut.gatedPrims,
             (unsigned long long)honest, (unsigned long long)visible,
             honest ? double(visible) / double(honest) : 0.0);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
}

/// Whether the draws a stopped node leaves behind are geometry that
/// MUST draw, or geometry nobody ever classified.
///
/// The two are invisible in the one bit that decides it.
/// MeshData::attachedOnly is false BY DEFAULT and the gate reads false
/// as "floating -- never suppress", so a set the producer never judged
/// is indistinguishable from one it judged and found floating (the
/// same trap docs/SceneStreaming.md 13b records for the load storm).
/// At 64px on MiSTer three quarters of what the cut still draws is
/// this population, so which of the two it is decides whether the
/// residue is an irreducible floor or a producer that stopped short.
///
/// It is answerable without a new bit, because the element contract
/// keys on the OBJECT and the object's own faces say what its edges
/// are. A set whose object owns no faces ANYWHERE is floating by
/// construction -- a sketch, a wire, a datum, and nothing else on
/// screen would show it. A set whose faces are still drawn exactly is
/// drawn correctly, because the dependency it waits on is still shown.
/// Only a set whose own faces a proxy has ALREADY taken, and which the
/// cut nevertheless draws, is one the contract would have gated had
/// the producer classified it -- so that population, and only that
/// one, is the suspect the question is about.
static void reportProxyFloating(const Render::ProxyHierarchy &index,
                                const Render::DrawCallList &draws,
                                const Render::ProxyCut &cut,
                                float tolerancePx)
{
    const auto &insts = index.instances();
    // Faces ANYWHERE in the model, not merely under a proxy: "this
    // object has no surface at all" is what makes a line set floating
    // by construction, and that is a claim about the object rather
    // than about the cut.
    std::unordered_set<uint64_t> objectHasFaces, objectAnyDraw;
    for (const auto &inst : insts) {
        if (inst.objectKey)
            objectAnyDraw.insert(inst.objectKey);
        if (inst.mergeable && inst.objectKey)
            objectHasFaces.insert(inst.objectKey);
    }
    // Whose faces a proxy actually took. Recomputed from the same rule
    // rather than borrowed from the cut's counters, so a member the
    // cut silently dropped still appears in the census.
    std::unordered_set<uint64_t> proxiedObjects;
    // And WHICH INSTANCES actually sit below one. The cut gates only
    // inside a stopped subtree, so an object whose faces a proxy took
    // may still own a set the descent reached by another route --
    // and reading such a set as "the producer never classified it"
    // would be reading this census's own approximation back as a
    // finding.
    std::unordered_set<uint32_t> underStopped;
    std::vector<uint32_t> sub;
    for (int ni : cut.proxyNodes) {
        sub.clear();
        index.subtreeInstances(ni, sub);
        for (uint32_t idx : sub) {
            const auto &inst = insts[idx];
            underStopped.insert(idx);
            if (inst.mergeable && inst.objectKey)
                proxiedObjects.insert(inst.objectKey);
        }
    }

    enum Who {
        FaceExact,        ///< a triangle draw no proxy stopped over
        Unkeyed,          ///< no objectKey: the contract cannot key on it
        NoFacesAtAll,     ///< genuinely floating -- nothing else shows it
        FacesStillExact,  ///< its dependency is drawn exactly, so it draws
        WouldGate,        ///< faces already proxied, yet still drawn
        WhoCount
    };
    struct Row {
        uint32_t inst = 0;
        uint64_t prims = 0;
    };
    Row who[WhoCount];
    uint32_t kindLine = 0, kindPoint = 0, kindStandIn = 0, kindOther = 0;
    uint32_t noMesh = 0;
    // The bit itself, COUNTED rather than inferred. Everything above
    // deduces "unclassified" from "the cut did not gate it", and that
    // deduction is sound only for a set the cut could have gated.
    uint32_t attTrue = 0, attFalse = 0, offStopped = 0;
    std::unordered_set<uint64_t> suspectObjects, noFaceObjects;

    for (uint32_t idx : cut.exact) {
        if (idx >= insts.size())
            continue;
        const auto &inst = insts[idx];
        Who w;
        if (inst.mergeable)
            w = FaceExact;
        else if (!inst.objectKey)
            w = Unkeyed;
        else if (!objectHasFaces.count(inst.objectKey))
            w = NoFacesAtAll;
        else if (!proxiedObjects.count(inst.objectKey))
            w = FacesStillExact;
        else
            w = WouldGate;
        who[w].inst += 1;
        who[w].prims += inst.primCount;
        if (w == NoFacesAtAll)
            noFaceObjects.insert(inst.objectKey);
        if (w != WouldGate)
            continue;
        suspectObjects.insert(inst.objectKey);
        (inst.attachedOnly ? attTrue : attFalse) += 1;
        if (!underStopped.count(idx))
            ++offStopped;
        // What the suspect actually is. A stand-in box is not source
        // geometry and was never the producer's to classify; a draw
        // with no mesh could not have carried the bit at all. Both are
        // separated out, or they would be read as producer omissions.
        if (inst.drawIndex < draws.size()) {
            const auto &d = draws[inst.drawIndex];
            if (!d.mesh)
                ++noMesh;
            if (d.standIn)
                ++kindStandIn;
            else if (d.material.type == Render::Material::Line)
                ++kindLine;
            else if (d.material.type == Render::Material::Point)
                ++kindPoint;
            else
                ++kindOther;
        }
    }

    char buf[512];
    const uint64_t unmerged = who[Unkeyed].prims + who[NoFacesAtAll].prims
        + who[FacesStillExact].prims + who[WouldGate].prims;
    const uint32_t unmergedInst = who[Unkeyed].inst + who[NoFacesAtAll].inst
        + who[FacesStillExact].inst + who[WouldGate].inst;
    const auto pct = [&](uint32_t v) {
        return unmergedInst ? 100.0 * double(v) / double(unmergedInst) : 0.0;
    };
    snprintf(buf, sizeof(buf),
             "render proxyfloat %gpx: exact %u inst / %llu prims = faces %u / "
             "%llu + unmergeable %u / %llu | of the unmergeable: unkeyed %u "
             "(%.0f%%) | no faces at all %u (%.0f%%) | faces still exact %u "
             "(%.0f%%) | WOULD GATE %u (%.0f%%)\n",
             double(tolerancePx), unmergedInst + who[FaceExact].inst,
             (unsigned long long)(unmerged + who[FaceExact].prims),
             who[FaceExact].inst, (unsigned long long)who[FaceExact].prims,
             unmergedInst, (unsigned long long)unmerged,
             who[Unkeyed].inst, pct(who[Unkeyed].inst),
             who[NoFacesAtAll].inst, pct(who[NoFacesAtAll].inst),
             who[FacesStillExact].inst, pct(who[FacesStillExact].inst),
             who[WouldGate].inst, pct(who[WouldGate].inst));
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif

    snprintf(buf, sizeof(buf),
             "render proxyfloat %gpx would-gate detail: %u inst / %llu prims "
             "(%.1f prims/draw) over %zu object(s) -- lines %u, points %u, "
             "standin %u, other %u, no mesh %u | attached bit: true %u, "
             "false %u | NOT below a stopped node: %u\n",
             double(tolerancePx), who[WouldGate].inst,
             (unsigned long long)who[WouldGate].prims,
             who[WouldGate].inst
                 ? double(who[WouldGate].prims) / double(who[WouldGate].inst)
                 : 0.0,
             suspectObjects.size(), kindLine, kindPoint, kindStandIn,
             kindOther, noMesh, attTrue, attFalse, offStopped);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif

    // The reading above rests entirely on the object key PAIRING a
    // line draw with its own faces, and there are two ways that could
    // be false rather than informative: the instanced path builds the
    // face group and the edge group as separate SoFCSelectionRoots,
    // and a merged draw-call entry is given a SYNTHETIC key
    // (0x80000000 | cacheId) that belongs to no object at all. Either
    // would make "this object owns no faces" mean "the key did not
    // match", and the whole census would be measuring its own
    // plumbing. So the model-wide pairing is reported beside it: if
    // face-less keys are a handful of real wires the two counts stay
    // far apart, and if the pairing is broken they converge.
    snprintf(buf, sizeof(buf),
             "render proxyfloat %gpx pairing: %zu distinct object key(s) in "
             "the model, %zu own faces, %zu own none -- the cut's 'no faces "
             "at all' is %u inst over %zu key(s)\n",
             double(tolerancePx), objectAnyDraw.size(), objectHasFaces.size(),
             objectAnyDraw.size() - objectHasFaces.size(),
             who[NoFacesAtAll].inst, noFaceObjects.size());
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
}

/// What a cut costs once the proxies it stops on are real: the same
/// frontier chosen by a node's measured error instead of by its extent,
/// and priced by what the proxies draw rather than only by what they
/// replace (docs/FarFieldProxies.md sections 3.3 and 11.1b).
///
/// Phase 1 could report neither. It had no generated proxy to have an
/// error, so it stood the node's extent in for one, and it had no
/// proxy to have a triangle count, so its saving was gross rather than
/// net. Both stand-ins were flagged where they were used; this is the
/// readout that removes them.
static void reportProxyCutPriced(const Render::ProxyHierarchy &index,
                                 const Render::DrawCallList &draws,
                                 const float *V, const float *P,
                                 float viewportHeightPx)
{
    // Generating the whole model, once, because a cut is a property of
    // the whole partition and a sampled subtree cannot be swept over
    // tolerances. Budgeted all the same, and what the budget skipped is
    // reported: a node without a proxy is descended past, so a run that
    // quietly ran out would read as a deeper cut rather than as a
    // missing one.
    static const uint64_t kTriangleBudget = 24000000;
    static const float kTolerances[] = {4.0f, 16.0f, 64.0f};

    Render::ProxyStore store;
    Render::ProxyGenOptions opts;
    opts.triangleBudget = kTriangleBudget;
    if (!store.generate(index, index.root(), draws, opts))
        return;
    std::vector<Render::ProxyNodeCost> costs;
    store.costs(index, costs);

    char buf[512];
    snprintf(buf, sizeof(buf),
             "render proxypriced: store %u nodes / %u entries, merged "
             "%.2fMtri in %.0fms (%.0f merge, %.0f decimate, %.0f standin) "
             "| skipped budget:%u single:%u refused:%u | %llu proxy + %llu "
             "standin tri held\n",
             store.stats().nodes, store.stats().entries,
             double(store.stats().mergedTriangles) / 1e6, store.stats().ms,
             store.stats().mergeMs, store.stats().simplifyMs,
             store.stats().standInMs, store.stats().overBudget,
             store.stats().belowMinMerge, store.stats().refused,
             (unsigned long long)store.stats().proxyTriangles,
             (unsigned long long)store.stats().standInTriangles);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif

    for (float tol : kTolerances) {
        Render::ProxyCut byExtent, byError;
        index.selectCut(V, P, viewportHeightPx, tol, byExtent);
        index.selectCut(V, P, viewportHeightPx, tol, costs, byError);
        // What the frame draws with no cut at all is everything the cut
        // did not cull, which both cuts agree on -- so it is the
        // denominator both rows are read against.
        const uint64_t visible = byError.exactPrims + byError.coveredPrims
            + byError.gatedPrims;
        const uint64_t drawn = byError.exactPrims + byError.proxyPrims;
        snprintf(buf, sizeof(buf),
                 "render proxypriced %gpx: extent draws %u (%u proxy) covers "
                 "%llu prims | error draws %u (%u proxy) covers %llu, proxies "
                 "cost %llu -> %llu of %llu prims drawn, %.2fx\n",
                 double(tol), byExtent.drawCount, byExtent.proxyDraws,
                 (unsigned long long)byExtent.coveredPrims, byError.drawCount,
                 byError.proxyDraws,
                 (unsigned long long)byError.coveredPrims,
                 (unsigned long long)byError.proxyPrims,
                 (unsigned long long)drawn, (unsigned long long)visible,
                 drawn ? double(visible) / double(drawn) : 0.0);
#ifdef FC_RENDERER_STANDALONE
        std::printf("%s", buf);
#else
        Base::Console().Message("%s", buf);
#endif
        // The same cut, asked what it did NOT aggregate -- which is
        // where the ceiling above is, and what phase 3 has to move.
        reportProxyExact(index, store, byError, V, P, viewportHeightPx, tol,
                         costs);
        // And what those exact draws ARE: the residue is three
        // quarters of the cut's draws at 64px, and it matters whether
        // it must draw or merely was never judged.
        reportProxyFloating(index, draws, byError, tol);
    }
}

/// A user shader is animated when its source references the engine
/// clock uniform u_fcTime — no declared flag anywhere, the reference
/// itself is the opt-in (docs/RenderDebug.md §6).
inline bool userShaderAnimated(const UserShader &shader)
{
    return shader.vertexSource.find("u_fcTime") != std::string::npos
        || shader.fragmentSource.find("u_fcTime") != std::string::npos;
}

/// Assemble a user volume-stage medium splice (docs/RenderEngine.md
/// §5.11): a variant of one of the volumetric fragment bodies
/// (fc_volume_fs.sh / fc_volume_ext_fs.sh / fc_refl_media_fs.sh) with
/// the user medium functions dispatched for their fire (emissive) and
/// cloud (scatter) slots. The prelude defines FC_USER_FIRE_<slot> /
/// FC_USER_SCATTER_<slot> (fc_volume.sh prototypes and dispatches on
/// them); each user source is appended after the body include — so it
/// sees every helper — with its contract functions (fcMediumField/
/// fcMediumRamp on the fire channel, fcMediumScatter on the cloud
/// channel) macro-renamed to the slot's dispatch targets and
/// FC_MEDIUM_SLOT set to its slot index. Returns a synthetic
/// UserShader (stock vs_fc_comp vertex stage, merged parameter list)
/// for the shared compile cache, or null when no slot carries a user
/// medium.
/// Collect the volume-stage user medium of every fire / cloud slot
/// from a draw list, replicating exactly the slot assignment of the
/// frame loop's fire and cloud/fountain body scans (dedup by object
/// key in draw order, bodies beyond the slot count share slot 0 and
/// carry no user medium). Shared between the frame loop and the
/// snapshot serializer so the assembled splice sources match
/// byte-for-byte across tiers — the viewer adopts shipped binaries by
/// source equality.
inline void
collectMediumUsers(const Render::DrawCallList &scene,
                   std::shared_ptr<const Render::UserShader> *fireUsers,
                   std::shared_ptr<const Render::UserShader> *cloudUsers,
                   int nslots)
{
    std::unordered_set<uint64_t> fireSeen, cloudSeen;
    int nfire = 0, ncloud = 0;
    for (const auto &draw : scene) {
        const auto &mat = draw.material;
        if (mat.ontop || mat.type != Render::Material::Triangle)
            continue;
        auto user = (mat.usershader && mat.usershader->stage == "volume"
                     && !mat.usershader->fragmentSource.empty())
            ? mat.usershader : nullptr;
        if (mat.fire && fireSeen.insert(draw.objectKey).second
                && nfire < nslots)
            fireUsers[nfire++] = user;
        if ((mat.cloud || mat.fountain)
                && cloudSeen.insert(draw.objectKey).second
                && ncloud < nslots)
            cloudUsers[ncloud++] = user;
    }
}

inline std::shared_ptr<const Render::UserShader>
assembleMediumVariant(const char *body,
                      const std::shared_ptr<const Render::UserShader> *fireUsers,
                      const std::shared_ptr<const Render::UserShader> *scatterUsers,
                      int nslots)
{
    bool any = false;
    for (int i = 0; i < nslots; ++i)
        any = any || fireUsers[i] || scatterUsers[i];
    if (!any)
        return nullptr;
    auto res = std::make_shared<Render::UserShader>();
    res->stage = "volume-splice";
    std::string &s = res->fragmentSource;
    s = "$input v_texcoord0\n\n#include <bgfx_shader.sh>\n";
    for (int i = 0; i < nslots; ++i) {
        if (fireUsers[i])
            s += "#define FC_USER_FIRE_" + std::to_string(i) + "\n";
        if (scatterUsers[i])
            s += "#define FC_USER_SCATTER_" + std::to_string(i) + "\n";
    }
    s += "#include \"";
    s += body;
    s += "\"\n";
    auto mergeParams = [&res](const Render::UserShader &u) {
        for (const auto &p : u.params) {
            if (p.name == "fc_state")
                continue;
            res->params.push_back(p);
        }
    };
    for (int i = 0; i < nslots; ++i) {
        if (!fireUsers[i])
            continue;
        std::string n = std::to_string(i);
        s += "#define fcMediumField fcUserField_" + n + "\n";
        s += "#define fcMediumRamp fcUserRamp_" + n + "\n";
        s += "#define FC_MEDIUM_SLOT " + n + "\n";
        s += fireUsers[i]->fragmentSource;
        s += "\n#undef fcMediumField\n#undef fcMediumRamp\n"
             "#undef FC_MEDIUM_SLOT\n";
        mergeParams(*fireUsers[i]);
    }
    for (int i = 0; i < nslots; ++i) {
        if (!scatterUsers[i])
            continue;
        std::string n = std::to_string(i);
        s += "#define fcMediumScatter fcUserScatter_" + n + "\n";
        s += "#define FC_MEDIUM_SLOT " + n + "\n";
        s += scatterUsers[i]->fragmentSource;
        s += "\n#undef fcMediumScatter\n#undef FC_MEDIUM_SLOT\n";
        mergeParams(*scatterUsers[i]);
    }
    return res;
}

class BGFXRendererLibP {
public:
    BGFXRendererLibP() {
#if defined(FC_OS_MACOSX) && !defined(FC_RENDERER_STANDALONE)
        // Metal is the only backend that can run this renderer on
        // macOS: Apple caps the compatibility profile Coin needs at
        // GL 2.1, the stock shader pack is GLSL 1.40 (GL 3.1), and the
        // two cannot be reconciled in one shared context -- a 4.1 core
        // context has no fixed-function pipeline for Coin, and macOS
        // will not share across profiles.
        //
        // Opt-in until the desktop composite follows: BGFXView::blit
        // stands aside on Metal (its GL framebuffer cannot wrap an
        // id<MTLTexture>), so a Metal session renders and captures but
        // does not yet put its frame on screen -- Coin draws that.
        // Offering it in the backend list would read as a broken
        // renderer rather than an unfinished one. Frame CAPTURE is
        // portable now, which is what the golden render tests gate on.
        if (getenv("FC_BGFX_METAL"))
            typeMap["bgfx - Metal"] = RendererType::Metal;
#endif
        for (auto &v : typeMap)
            types.push_back(v.first);
    }

    ~BGFXRendererLibP();

    BGFXView *getView(QOpenGLWidget *widget, RendererType::Enum type);
    /// The view of \a widget if it already exists, else null. Unlike
    /// getView it creates nothing and prepares no context -- for
    /// callers off the render path (the level plan, asking what is
    /// uploaded) that must not bring a view into being by asking about
    /// it.
    BGFXView *findView(QOpenGLWidget *widget)
    {
        auto it = views.find(widget);
        return it == views.end() ? nullptr : it->second.get();
    }


    void removeView(QOpenGLWidget *widget);

    void shutdown();
    /// Whether bgfx is initialised right now. Every bgfx call after
    /// shutdown() is undefined (bgfx::getStats() locks a mutex that no
    /// longer exists), and the last view's release shuts the library
    /// down while renderer objects -- and their pending timers -- live
    /// on: anything that reaches bgfx from OFF the render path asks
    /// this first.
    bool deviceUp() const
    {
        return currentType != RendererType::Noop;
    }

#ifdef FC_RENDERER_STANDALONE
    /// Standalone (no Qt): bgfx owns the native window/canvas handed in
    /// through setWindowHandle() — under Emscripten the "#canvas" CSS
    /// selector — and creates its own GL context on it.
    bool prepare(QOpenGLWidget *, RendererType::Enum type)
    {
        if (currentType == RendererType::Noop) {
            currentType = type;
            bgfx::Init init;
            init.type = type;
            init.platformData.nwh = windowHandle;
            init.resolution.width = standaloneWidth;
            init.resolution.height = standaloneHeight;
            resetWidth = standaloneWidth;
            resetHeight = standaloneHeight;
            init.resolution.reset = bgfxResetFlags();
            // 0 leaves bgfx at its build ceiling; a smaller number
            // shortens the per-frame walk over the view table and the
            // per-view pools sized from that ceiling.
            init.limits.maxViews = uint32_t(
                    std::max(0, RendererFactory::maxViewIds()));
            if (!bgfx::init(init)) {
                currentType = RendererType::Noop;
                RENDER_ERR("init failed");
                return false;
            }
            resolveDeviceName();
        }
        return true;
    }

    void makeCurrent() {}
    void doneCurrent() {}
    void freeFBO(int) {}
    /// The standalone build owns its window, so there is no Qt context
    /// or surface to time -- but warmup() reports these unconditionally,
    /// and a zero is the true answer here rather than a placeholder.
    double msContext = 0;
    double msDevice = 0;
#else
    /// Milliseconds the last prepare() spent building the GL context
    /// and its surface, and in bgfx::init. Zero when it had nothing to
    /// do. Read by BGFXRendererLib::warmup to report where startup
    /// time goes; a frame never looks at them.
    double msContext = 0;
    double msDevice = 0;

    bool prepare(QOpenGLWidget *widget, RendererType::Enum type)
    {
        // A device this build's shaders cannot run on, already reported.
        if (glUnsupported)
            return false;
        QElapsedTimer _warmClock;
        _warmClock.start();
        msContext = msDevice = 0;
        if (!context) {
            context.reset(new QOpenGLContext);
            auto format = widget->format();
            context->setShareContext(QOpenGLContext::globalShareContext());
            context->setFormat(format);
            if (!context->create()) {
                RENDER_ERR("failed to create a GL context for bgfx");
                context.reset();
                return false;
            }
            offscreen.reset(new QOffscreenSurface);
            offscreen->setFormat(format);
            offscreen->create();
            if (!offscreen->isValid()) {
                RENDER_ERR("failed to create the offscreen surface for bgfx");
                offscreen.reset();
                context.reset();
                return false;
            }
            if (!quitHooked && QCoreApplication::instance() && !getenv("FC_NO_BGFX_QUITHOOK")) {
                quitHooked = true;
                // Tear down bgfx and its Qt GL objects while Qt is still
                // alive. This static _BGFXLib is otherwise destroyed at
                // library unload, after QApplication is gone, where deleting
                // the QOpenGLContext/QOffscreenSurface crashes.
                QObject::connect(QCoreApplication::instance(),
                                 &QCoreApplication::aboutToQuit,
                                 [this]() {
                                     views.clear();
                                     granules.clear();
                                     shutdown();
                                 });
            }
        }

        msContext = _warmClock.nsecsElapsed() / 1.0e6;
        _warmClock.restart();

        if (currentType == RendererType::Noop) {
            currentType = type;

            bgfx::renderFrame();
            bgfx::Init init;
            init.type = currentType;

            if (currentType == RendererType::OpenGL) {
                makeCurrent();
                // The stock shader pack is compiled at GLSL 1.40, which
                // is OpenGL 3.1. Below that bgfx does not refuse the
                // device -- it takes its GL21 path and crashes building
                // the first program -- so this is where the line is
                // drawn, and the caller falls back to the render
                // cache's own GL renderer as it does for any other
                // failure here (docs/CoinRetirement.md 3.7).
                const QSurfaceFormat fmt = context->format();
                if (fmt.majorVersion() < 3
                        || (fmt.majorVersion() == 3 && fmt.minorVersion() < 1)) {
                    RENDER_ERR("OpenGL " << fmt.majorVersion() << "."
                               << fmt.minorVersion()
                               << " is below the 3.1 this renderer's shaders"
                                  " need; drawing through the render cache"
                                  " instead");
                    // Said once. The device is not going to grow a
                    // version, and every frame asks again.
                    glUnsupported = true;
                    currentType = RendererType::Noop;
                    widget->makeCurrent();
                    return false;
                }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#   if defined(FC_OS_LINUX)
                if (auto *glx = context->nativeInterface<QNativeInterface::QGLXContext>())
                    init.platformData.context = glx->nativeContext();
                else if (auto *egl = context->nativeInterface<QNativeInterface::QEGLContext>())
                    init.platformData.context = egl->nativeContext();
#   elif defined(FC_OS_WIN32)
                if (auto *wgl = context->nativeInterface<QNativeInterface::QWGLContext>())
                    init.platformData.context = wgl->nativeContext();
#   elif defined(FC_OS_MACOSX)
                if (auto *cocoa = context->nativeInterface<QNativeInterface::QCocoaGLContext>())
                    init.platformData.context = cocoa->nativeContext();
#   endif
#else
                init.platformData.context = qvariant_cast<OpenGLContext>(
                    context->nativeHandle()).context();
#endif
            } else {
                window = new QWindow();
                window->setObjectName(QStringLiteral("bgfxScreenSurface"));
                // d->offscreenWindow->setSurfaceType(QWindow::OpenGLSurface);
                // d->offscreenWindow->setFormat(d->requestedFormat);
                window->setGeometry(0, 0, widget->width(), widget->height());
                window->create();
                // winId() is an NSView* on macOS, an HWND on Windows and an X11
                // Window elsewhere. bgfx takes all three: its Metal backend does its
                // own isKindOfClass: dispatch over NSView/NSWindow/CAMetalLayer
                // (renderer_mtl.cpp), so no Objective-C++ unwrapping is needed here.
                init.platformData.nwh = reinterpret_cast<void*>(window->winId());
            }
            // bgfx treats an all-null PlatformData as a request for a headless device, and
            // then rejects a non-zero resolution ("resolution of non-existing backbuffer
            // can't be larger than 0x0") - which surfaces only as init() returning false.
            // In the OpenGL path platformData.context is the sole field we set, so if the
            // native handle did not come through, say which step failed rather than letting
            // bgfx report a headless-mode error that has nothing to do with the real cause.
            if (currentType == RendererType::OpenGL && !init.platformData.context) {
                currentType = RendererType::Noop;
                RENDER_ERR("no native GL context handle; bgfx would fall back to headless");
                return false;
            }
            init.resolution.width = framebufferWidth(widget);
            init.resolution.height = framebufferHeight(widget);
            init.resolution.reset = bgfxResetFlags();
            // See the standalone path above: a startup option, because
            // bgfx::init happens once per process.
            init.limits.maxViews = uint32_t(
                    std::max(0, RendererFactory::maxViewIds()));
            if (!bgfx::init(init)) {
                widget->makeCurrent();
                RENDER_ERR("init failed");
                return false;
            }
            resolveDeviceName();
            msDevice = _warmClock.nsecsElapsed() / 1.0e6;
        }
        return true;
    }

    void makeCurrent()
    {
        context->makeCurrent(offscreen.get());
        for (auto &v : pendingRemoves)
            v.second(context->functions(), v.first);
        pendingRemoves.clear();
    }

    void doneCurrent()
    {
        context->doneCurrent();
    }

    void freeFBO(GLint fbo)
    {
        pendingRemoves.emplace_back(fbo, freeFramebufferFunc);
    }
#endif // !FC_RENDERER_STANDALONE

    std::string resource()
    {
        return RendererFactory::resourcePath() + "bgfx/assets/";
    }

    /// Asset root the shader programs load from: FC_BGFX_SHADER_DIR
    /// (a directory containing shaders/{glsl,essl,spirv}/) overrides
    /// the installed resource tree, so a developer can point at the
    /// source tree's compile.sh output and hot-reload from there
    /// (docs/RenderDebug.md §3).
    std::string shaderPath()
    {
        static const std::string overridePath = [] {
            const char *dir = std::getenv("FC_BGFX_SHADER_DIR");
            std::string s = dir ? dir : "";
            if (!s.empty() && s.back() != '/')
                s += '/';
            return s;
        }();
        return overridePath.empty() ? resource() : overridePath;
    }

    /// Shader hot-reload generation (docs/RenderDebug.md §3): bumped by
    /// BGFXRenderer::reloadShaders(); a view whose captured generation
    /// lags re-inits at the top of its next render(), reloading every
    /// program from shaderPath().
    int shaderGeneration = 0;

    /// Dynamically bound named uniforms (docs/RenderDebug.md §2.5):
    /// name -> handle + vec4 count, resolved lazily. bgfx registers
    /// uniforms by name, refcounted, and resizes a same-name uniform
    /// upward on re-create, so growth is just another createUniform
    /// (the old reference is released to keep the count balanced).
    /// The handle MUST default to BGFX_INVALID_HANDLE — a
    /// value-initialized handle is idx 0, a live uniform, and
    /// destroying it underflows someone else's refcount.
    struct UserUniform {
        bgfx::UniformHandle handle = BGFX_INVALID_HANDLE;
        uint16_t num = 0;
    };
    std::map<std::string, UserUniform> userUniforms;

    /// Animation clock for user shaders (u_fcTime): x = seconds on the
    /// shared effect clock (0 while RenderDebug freeze-frame holds), y =
    /// 1 while the clock advances, z/w reserved. Stamped per frame by
    /// the renderer, recorded with every consuming user draw.
    bgfx::UniformHandle timeUniform = BGFX_INVALID_HANDLE;
    float userTime[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    /// The current frame submitted a live time-referencing user draw
    /// (reset each frame, folded into the renderer's animatedFrame so
    /// the viewer keeps scheduling redraws while a user shader
    /// animates). Lives here because the draw-submitting view code has
    /// no path back to the renderer pimpl.
    bool userAnimatedDraw = false;

    void setUserUniform(const std::string &name, const float *data,
                        uint16_t num)
    {
        if (!num)
            return;
        auto &entry = userUniforms[name];
        if (!bgfx::isValid(entry.handle) || entry.num < num) {
            bgfx::UniformHandle h = bgfx::createUniform(
                name.c_str(), bgfx::UniformType::Vec4, num);
            if (bgfx::isValid(entry.handle))
                bgfx::destroy(entry.handle);
            entry.handle = h;
            entry.num = num;
        }
        if (bgfx::isValid(entry.handle))
            bgfx::setUniform(entry.handle, data, num);
    }

    /// Record a user shader's parameters for its consuming draw
    /// (docs/RenderDebug.md §6.4), zeroing every other dynamically
    /// bound uniform: uniform values persist backend-side between
    /// frames, so a parameter dropped from the list would otherwise
    /// keep feeding its stale value to a program that declares it.
    /// Zeroing is safe for the frame's other consumers — every user
    /// draw and the debug lane re-record their own parameters with
    /// their own submits — and engine uniforms have their own handles
    /// outside this map, so they are never touched.
    void pushUserParams(const Render::UserShader &shader)
    {
        if (!bgfx::isValid(timeUniform))
            timeUniform = bgfx::createUniform("u_fcTime",
                                              bgfx::UniformType::Vec4);
        bgfx::setUniform(timeUniform, userTime);
        std::vector<float> zeros;
        for (auto &v : userUniforms) {
            if (!bgfx::isValid(v.second.handle))
                continue;
            bool inParams = false;
            for (const auto &p : shader.params) {
                if (p.name == v.first) {
                    inParams = true;
                    break;
                }
            }
            if (inParams)
                continue;
            if (zeros.size() < v.second.num * 4u)
                zeros.resize(v.second.num * 4u, 0.0f);
            bgfx::setUniform(v.second.handle, zeros.data(), v.second.num);
        }
        for (const auto &p : shader.params)
            setUserUniform(p.name, p.values.data(),
                           uint16_t(p.values.size() / 4));
    }

    /// Runtime user-shader program cache (docs/RenderDebug.md §6.3).
    /// Desktop builds: user .sc source compiles through the host
    /// shaderc into a disk cache keyed on SHA1(source × target), and
    /// the linked programs cache here by source identity. Compilation
    /// is ASYNCHRONOUS: render() only consults the caches (a QProcess
    /// vfork inside the widget paint path both hitches the frame and
    /// corrupts Qt's repaint bookkeeping — learned the hard way),
    /// missing bins schedule a compile on the event loop, and a
    /// finished compile bumps userCompileGeneration so idle-skip
    /// clients re-render. A failed compile is remembered and reported
    /// once — the pass just stays off.
    /// Standalone/WASM builds have no host compiler: programs load
    /// from the profile-matched precompiled variants the snapshot
    /// carries (UserShader::compiled, server-side compile), cached
    /// here keyed on the binary payloads.
    struct UserProgram {
        bgfx::ProgramHandle prog = BGFX_INVALID_HANDLE;
        bool failed = false;
        /// The frameSerial of the last lookup (sweepUserCaches).
        uint32_t lastUsed = 0;
    };
    std::map<std::string, UserProgram> userPrograms;
    /// The mesh-shader variant a MaterialX document compiles to, keyed
    /// on the document's identity (its file, or its text). Generating
    /// one costs tens of milliseconds -- a whole MaterialX document
    /// load and shader generation -- and every draw sharing a material
    /// asks for it every frame, so it is generated once. An entry that
    /// is empty is a document that could not be generated; the reason
    /// was reported when it was first tried, and the draw shades as its
    /// stock appearance from then on without asking again.
    /// What generating one MaterialX document produced.
    struct MaterialXVariant {
        /// The assembled fragment source: the stock mesh fragment stage
        /// with the document's generated material-inputs function
        /// spliced in. Empty when the document cannot be rendered by
        /// the raster path.
        std::string source;
        /// The same function spliced into the glass body stage
        /// (fc_glass_fs.sh), for a surface the capture claimed as a
        /// glass body (Material::glassmtlx): one generation, two
        /// assemblies. Empty exactly when `source` is.
        std::string glassSource;
        /// The layers `source` samples and the file each one wants
        /// (docs/CyclesIntegration.md sec 6.12). Only the generator
        /// knows the layer order, and only the capture has the pixels,
        /// so the draw joins the two lists on the image's path.
        std::vector<Render::MaterialX::GeneratedMaterial::Image> images;
        /// The array sampler `source` declares for those layers and the
        /// unit it claims. Empty and 0 when the document names no image.
        std::string imageSampler;
        int imageUnit = 0;
        /// The frameSerial of the last lookup (sweepUserCaches).
        uint32_t lastUsed = 0;
    };
    std::map<std::string, MaterialXVariant> materialXVariants;
    /// Counts the frames submitted; what the two caches above stamp a
    /// lookup with.
    uint32_t frameSerial = 0;
    /// Bound the two caches (docs/ShaderGraphEditor.md sec 14). The
    /// graph editor makes a distinct text per gesture -- a variant, a
    /// generated source, a linked program with its two shader handles
    /// -- and a session of editing grew both maps without limit, toward
    /// bgfx's 512 shader handles. Once a map is past its cap, entries
    /// not looked up for a while are dropped; the sweep never touches
    /// what this frame used, and below the cap nothing is touched, so
    /// a scene's own materials are not churned by an object out of
    /// view. Every user of a program handle re-resolves it per frame
    /// and keeps it only within the frame, which is what makes a drop
    /// after bgfx::frame() safe. Called once per frame, after it.
    void sweepUserCaches();
    /// The MaterialX generation warnings already printed, so a document
    /// edited per gesture reports each note once.
    std::set<std::string> materialXWarned;
    /// Generate a MaterialX document's mesh-shader variant, once per
    /// document, and remember it.
    const MaterialXVariant &materialXVariant(const Render::UserShader &shader);
    /// Sampler uniform handles by name, created on demand: the names
    /// come from the generated shader, so they are not known until a
    /// document has been generated. On the lib rather than the view
    /// because a uniform handle is global to the backend, while the
    /// TEXTURE it is given is the view's (BGFXView::pushUserImages).
    std::map<std::string, bgfx::UniformHandle> userSamplers;
    /// The handle for one generated sampler name, made on first use.
    bgfx::UniformHandle userSampler(const std::string &name);
    /// Resolve a user program: user fragment stage + either a user
    /// vertex stage or the named stock vertex stage ("vs_fc_comp" for
    /// the post stage's full-screen triangle, "vs_fc_mesh" for the
    /// material stage). Invalid while a compile is pending (desktop)
    /// or while no shipped binary matches the active backend
    /// (standalone); entry.failed on real failure.
    /// simulate = resolve the shader's particle state step
    /// (UserShader::simulateSource) instead of its beauty fragment
    /// stage, always paired with the stock full-screen vertex shader.
    /// Same cache, same async compile, same viewer-tier binary lookup.
    /// splice = which stock fragment stage a MaterialX document's
    /// generated material function is spliced into: the mesh stage
    /// (the beauty passes) or the glass body stage (ViewGlassSurface,
    /// docs/MaterialStorage.md sec 17.22). Ignored for shader text,
    /// which is a whole fragment stage of its own. The standalone tier
    /// answers it from the shipped glass binary (Compiled::glassBin),
    /// and invalid while none travelled, so the viewer draws a
    /// MaterialX glass with the flat pass until then.
    enum UserSplice { MeshSplice, GlassSplice };
    bgfx::ProgramHandle getUserProgram(const Render::UserShader &shader,
                                       const char *stockVs,
                                       bool simulate = false,
                                       UserSplice splice = MeshSplice);
#ifndef FC_RENDERER_STANDALONE
    /// Per-shader compile bookkeeping, keyed by SHA1(source×target×type).
    std::set<std::string> userShaderInflight;
    std::set<std::string> userShaderFailed;
    // include-tree content hash per source dir, part of the compile
    // cache key (shipped-helper edits must miss stale cached bins)
    std::map<std::string, QByteArray> userShaderSrcFingerprints;
    /// Bumped when an async compile finishes (either way); mirrored into
    /// each renderer's dirty state so the next frame retries the lookup
    /// (and the scene server republishes with the fresh bins).
    int userCompileGeneration = 0;
    /// Set by getUserProgram when a draw asked for a program whose
    /// compile is still in flight and was handed the stock program to
    /// stand in. Cleared at the head of every frame (a sub-view
    /// sequence's first submit) and read at its tail: a one-shot frame
    /// dump is not consumed by a frame that drew a stand-in, because
    /// the picture asked for is the scene with its materials, and a
    /// surface drawn without one is not that. A failed compile does
    /// not set it -- its draw stands in for good and the frame is the
    /// picture there is.
    bool userProgramStoodIn = false;

    /// Disk-cache / async-compile step for one user shader. The default
    /// target is the active bgfx backend; \a platform / \a profile
    /// override it for cross-compiles (the viewer tiers).
    /// Returns 0 with \a binPath set when the bin is ready, 1 while a
    /// compile is in flight, 2 when compilation failed (reported once).
    int ensureUserShaderBin(const std::string &source, bool fragment,
                            QString &binPath,
                            const char *platform = nullptr,
                            const char *profile = nullptr);
    /// Make the copy of a user shader that travels whole for the viewer
    /// tiers (SceneSnapshot::shipShader). Server-side compile
    /// (docs/RenderDebug.md sec 6.3): compile \a shader for each viewer
    /// target through the async disk cache and append every READY
    /// variant to its compiled list -- for a MaterialX document, the
    /// mesh splice and, where its surface claims a glass body, the
    /// glass splice beside it. Pending compiles republish on the next
    /// userCompileGeneration bump; failed ones are dropped (reported
    /// once by the compile). And the document's image layout: each
    /// image's array layer, the sampler and the unit, resolved against
    /// the generator those tiers do not have (docs/MaterialStorage.md
    /// sec 17.23).
    void shipUserShader(Render::UserShader &shader);
#endif

#ifdef FC_RENDERER_STANDALONE
    /// Native window handle bgfx initializes on (Emscripten: the canvas
    /// CSS selector) and the current output size, fed by the host app
    /// through BGFXRenderer::setWindowHandle()/setWindowSize().
    void *windowHandle = nullptr;
    uint16_t standaloneWidth = 1024;
    uint16_t standaloneHeight = 768;
    // Scene render-target sample count (BGFXRenderer::setMSAASamples);
    // a change re-creates the view targets on the next render().
    int standaloneSamples = 4;
    /// Sub-view target size override (renderSubViews,
    /// docs/SplitViews.md sec 9.2): while non-zero the view's targets
    /// size to the sub-view rect instead of the canvas -- the
    /// standalone twin of the desktop captureWidth/Height below.
    /// 0 = follow the canvas, which is every plain render().
    uint16_t standaloneSubWidth = 0;
    uint16_t standaloneSubHeight = 0;
    /// What the backbuffer was last sized to (bgfx::init/reset).
    /// Distinct from the view target size since sub-views: a view
    /// whose targets are a sub-view rect must not read the size
    /// mismatch against the canvas as a canvas resize.
    uint16_t resetWidth = 0;
    uint16_t resetHeight = 0;
    /// The size a standalone view's targets build at.
    uint16_t viewTargetWidth() const
    { return standaloneSubWidth ? standaloneSubWidth : standaloneWidth; }
    uint16_t viewTargetHeight() const
    { return standaloneSubHeight ? standaloneSubHeight : standaloneHeight; }
#else
    // Scene render-target sample count override (BGFXRenderer::setMSAASamples,
    // driven by the AntiAliasing preference). -1 = follow the host GL widget's
    // format; >= 0 overrides it. A change re-creates the view targets at the
    // top of the next render() — bgfx renders into its own offscreen FBO and
    // resolves before the blit, so the Qt context need not be multisampled.
    int desktopSamples = -1;
    // Offscreen capture size (BGFXRenderer::renderOffscreen): while
    // non-zero the desktop view renders at this size instead of the
    // host widget's. 0 = follow the widget, which is every on-screen
    // frame -- so the frame after a capture sees the mismatch and
    // sizes the view back on its own.
    uint16_t captureWidth = 0;
    uint16_t captureHeight = 0;
    /// The host widget's framebuffer size. Qt's width()/height() are
    /// logical (device-independent) pixels; the widget's default
    /// framebuffer, which blit() writes into, is that times the device
    /// pixel ratio. Sized from width() alone, a view under
    /// QT_SCALE_FACTOR=2 rendered into the bottom-left quadrant of the
    /// 3D view (docs/ShaderGraphEditor.md sec 14).
    static int framebufferWidth(QOpenGLWidget *widget)
    { return int(widget->width() * widget->devicePixelRatioF() + 0.5); }
    static int framebufferHeight(QOpenGLWidget *widget)
    { return int(widget->height() * widget->devicePixelRatioF() + 0.5); }
    /// The size a desktop view renders at: the host widget's framebuffer,
    /// unless an offscreen capture is asking for its own.
    int viewWidth(QOpenGLWidget *widget) const
    { return captureWidth ? int(captureWidth) : framebufferWidth(widget); }
    int viewHeight(QOpenGLWidget *widget) const
    { return captureHeight ? int(captureHeight) : framebufferHeight(widget); }
    typedef void (*FreeResourceFunc)(QOpenGLFunctions *functions, GLuint id);
    std::vector<std::pair<GLuint, FreeResourceFunc>> pendingRemoves;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> offscreen;
#endif
    // Resolution scale of the expensive screen-space effect passes
    // (Render_EffectResolution; BGFXRenderer::setEffectResolution). A change
    // re-creates the view's scaled targets at the top of the next render().
    float effectResolution = 1.0f;
    // Resolution scale of the SSAO resolve targets (Render_SSAOResolution;
    // BGFXRenderer::setSSAOResolution) -- independent of effectResolution so
    // ambient occlusion stays sharp while the reflection re-render can scale.
    float ssaoResolution = 1.0f;
    std::unordered_map<QOpenGLWidget *, std::unique_ptr<BGFXView>> views;
    // View-id pool. A viewer holds a contiguous block sized to the
    // passes its frames actually use (BGFXView's pass map), which is
    // well under NUM_VIEWS for an ordinary scene, so the ids bgfx offers
    // stretch much further than one block per viewer would allow.
    //
    // Blocks are handed out in granules so that a scene picking up one
    // more pass -- a second overlay, a bulb that starts casting -- does
    // not repack the pool every time. A block only ever grows: it is the
    // high-water mark of what the viewer has needed, which keeps the
    // ids of a viewer that alternates between two configurations still.
    static const uint16_t kIdGranule = 8;
    std::vector<uint8_t> granules;   ///< 1 = taken
    // Whether the "out of bgfx view ids" refusal has already been
    // reported. A viewer asks once per frame, so without this the
    // message would repeat for as long as the extra viewer is open.
    // Cleared whenever a block is returned, so the next viewer that
    // does not fit says so again.
    bool warnedViewBudget = false;

    /// Ids in the pool (0 until the first reservation sizes it).
    uint16_t poolSize() const
    {
        return uint16_t(granules.size() * kIdGranule);
    }
    /// Free the view's block.
    void releaseBlock(BGFXView *view);
    /// Make sure the view's block holds at least `need` ids, moving it
    /// if it has to grow and cannot grow in place. False means the pool
    /// is full -- the caller falls back to Coin for this frame rather
    /// than submitting ids bgfx would abort on.
    bool reserveBlock(BGFXView *view, uint16_t need);
    /// The raw allocator under reserveBlock/releaseBlock: a contiguous
    /// id block held in (id, span), granule-rounded. Draw surfaces
    /// (BGFXDrawDevice.cpp) hold their pass blocks through these, so
    /// their ids never collide with the viewers'.
    bool reserveIds(uint16_t &id, uint16_t &span, uint16_t need);
    void releaseIds(uint16_t &id, uint16_t &span);

    std::map<std::string, RendererType::Enum> typeMap = {
#ifdef FC_RENDERER_STANDALONE
        // The standalone build renders through GLES — under Emscripten
        // that is WebGL2 on the canvas.
        {"bgfx - OpenGL", RendererType::OpenGLES},
#else
        {"bgfx - OpenGL", RendererType::OpenGL},
#endif
        // {"bgfx - Vulkan", RendererType::Vulkan},
#ifdef FC_OS_WIN32
        // {"bgfx - Direct3D9", RendererType::Direct3D9},
        // {"bgfx - Direct3D11", RendererType::Direct3D11},
        // {"bgfx - Direct3D12", RendererType::Direct3D12},
#elif defined(FC_OS_MACOSX)
        // {"bgfx - Metal", RendererType::Metal},
#endif
    };
    std::vector<std::string> types;
    /// The GPU and driver bgfx actually came up on, resolved once after
    /// bgfx::init and handed to a capture's sidecar. Process-wide,
    /// because the device is: bgfx::init happens once.
    std::string deviceName;

    /// Resolve deviceName. The bgfx renderer name says which BACKEND
    /// runs, which is not which DEVICE runs it -- llvmpipe and a real
    /// adapter are both "OpenGL" -- so on GL the driver's own
    /// GL_RENDERER/GL_VERSION strings carry the answer, and the caps'
    /// vendor/device ids carry it everywhere else.
    void resolveDeviceName()
    {
        if (!deviceName.empty())
            return;
        std::string s = bgfx::getRendererName(bgfx::getRendererType());
        // Only where a context is current -- this runs at the tail of
        // prepare(), where the GL path has one and the others never do.
        if (auto *cur = QOpenGLContext::currentContext()) {
            if (auto *f = cur->functions()) {
                for (GLenum e : {GL_RENDERER, GL_VERSION}) {
                    const auto *str = f->glGetString(e);
                    if (str)
                        s += " / " + std::string(
                                reinterpret_cast<const char *>(str));
                }
            }
        }
        if (const bgfx::Caps *caps = bgfx::getCaps()) {
            char ids[64];
            std::snprintf(ids, sizeof(ids),
                          " / vendor 0x%04x device 0x%04x",
                          caps->vendorId, caps->deviceId);
            s += ids;
        }
        deviceName = s;
    }

    /// Set once when the GL device turns out to be older than the stock
    /// shader pack needs: there is nothing to retry, and a frame asks
    /// every time.
    bool glUnsupported = false;
    RendererType::Enum currentType = RendererType::Noop;
    std::string name = "bgfx";
    std::set<BGFXRenderer::Private *> renderers;
#ifndef FC_RENDERER_STANDALONE
    QWindow *window = nullptr;
    bool quitHooked = false;
#endif
};

// Defined in BGFXRenderer.cpp; shared by every renderer TU.
extern BGFXRendererLibP _BGFXLib;
extern BGFXRendererLib BGFXLib;

/// The engine-facing half of an ATTACHED draw surface
/// (docs/CAMSimRenderPort.md sec 8). The surface class itself stays
/// private to BGFXDrawDevice.cpp; the frame path drives it through
/// this, binding the host state for the duration of one
/// FrameConsumer::drawFrame call and unbinding it afterwards, so that
/// a consumer holding the surface past the callback can submit
/// nothing.
class BGFXHostSurface {
public:
    virtual ~BGFXHostSurface() {}

    /// Everything a consumer's one drawFrame call needs to know about
    /// the frame it is drawing into. One struct rather than positional
    /// parameters: the list is long enough that positions stopped
    /// being readable, and stage 3 adds to it.
    struct FrameBind {
        /// The host view ids the consumer's passes 0..numIds-1 map to,
        /// in that order.
        const uint16_t *ids = nullptr;
        unsigned numIds = 0;
        /// The target those passes default to -- the host's scene
        /// framebuffer -- and its attachments, for a consumer that
        /// samples rather than only writes them.
        bgfx::FrameBufferHandle target = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        /// The target's pixel size (the SCENE target's, not the
        /// widget's).
        int width = 0;
        int height = 0;
        /// True when the colour attachment holds linear light.
        bool linearColor = false;
        /// The camera the target was drawn with, column-major 4x4 --
        /// what lets a consumer put its own image into the shared
        /// depth buffer. Null when unavailable.
        const float *viewMtx = nullptr;
        const float *projMtx = nullptr;
    };

    /// The consumer-facing object handed to FrameConsumer::drawFrame.
    virtual Render::DrawSurface &surface() = 0;
    /// Bind this frame for the one drawFrame call \a bind describes.
    virtual void bindFrame(const FrameBind &bind) = 0;
    virtual void unbindFrame() = 0;
    /// Passes the surface was built for -- the consumer's own count,
    /// fixed at creation.
    virtual unsigned passes() const = 0;
};

/// The bgfx implementation of the draw facade (BGFXDrawDevice.cpp,
/// every tier). BGFXRendererLib::drawDevice hands it out once
/// the device is up.
DrawDevice *fcBGFXDrawDevice();

/// A surface for \a scenePasses + \a overlayPasses consumer passes.
/// The split exists to validate each run against its enum block --
/// the surface itself carries only the total, and the id mapping
/// rides bindFrame. Null when the device is down or a run exceeds
/// what a host frame offers. Built in the browser tier too: the
/// streamed frame's blit is a consumer (docs/CyclesIntegration.md
/// sec 7.1); only the widget-owning standalone surface is desktop.
std::unique_ptr<BGFXHostSurface> fcBGFXCreateHostSurface(
        unsigned scenePasses, unsigned overlayPasses);

} // namespace Renderer

// Geometry vertex stream built from the separate MeshData attribute
// arrays: position + normal (zero normal when the cache has none).
// Per-vertex colors ride their own stream (ColorVertex) so caches that
// differ only in baked colors — the PartGui color variants and the
// render cache's recolored copies — share one geometry buffer.
struct SceneVertex
{
    float px, py, pz;
    float nx, ny, nz;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,   3, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Second vertex stream of the mesh/flat programs: per-vertex rgba8
// color. Meshes without baked colors bind a shared all-white buffer
// (grown to the largest vertex count seen) instead of carrying one each.
struct ColorVertex
{
    uint32_t rgba;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Fourth vertex stream of the mesh programs: the per-face material
// bake (MeshData::materials) as three rgba8 attributes — Color1 the
// emissive, Color2 the specular with quantized shininess in alpha, and
// Color3 the surface finish palette index (Material::finishpalette) in
// Color3 the per-face palette indices (finish, projection frame, texture
// layer) one per byte.
//
// All three are NORMALIZED, including the index byte, and the vertex
// shader scales that one back up. Not a style choice: bgfx binds an
// unnormalized integer attribute with glVertexAttribIPointer on
// GLES3/WebGL2 (renderer_gl.cpp, `!isFloat(type) && !normalized`),
// which WebGL2 then refuses against the shader's `vec4 a_color3` --
// "vertex shader input type does not match the type of the bound
// vertex attribute", and EVERY draw carrying the stream is dropped.
// Desktop GL takes the other branch, so it never showed there.
// Bound only for meshes that carry the stream; every other mesh-program
// draw leaves the attributes unbound, which bgfx resolves to the GL
// default attribute — finite values the shader multiplies out, since
// it selects the stream over the material scalars by u_matEmissive.w.
struct MatVertex
{
    uint32_t emissive;
    uint32_t specshine;
    uint32_t finishidx;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Color1, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::Color2, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::Color3, 4, bgfx::AttribType::Uint8, true)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Interleaved position + normal + rgba8 vertex of the TRANSIENT draws
// (background gradient, shadow ground quad): one-off buffers where a
// separate color stream would buy nothing. The mesh programs source
// their attributes from this single stream.
struct TransientVertex
{
    float px, py, pz;
    float nx, ny, nz;
    uint32_t rgba;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,   3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0,   4, bgfx::AttribType::Uint8, true)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Position-only stream of the particle impact splat: one vertex per
// particle whose x is that particle's index (docs/RenderEngine.md
// §5.8). Everything the point draws with is fetched from the state
// grid, so the buffer carries no payload of its own.
struct PointVertex
{
    float px, py, pz;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Texture-coordinate vertex stream of textured meshes: 2D texture
// coordinates, bound only by the textured mesh programs (texcoords live
// outside SceneVertex so untextured scenes don't pay for them).
struct TexCoordVertex
{
    float u, v;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Corner of the unit quad that vs_fc_line expands into a screen-space
// thick line segment: x = end of the segment (0/1), y = side (-1/+1).
struct LineQuadVertex
{
    float x, y, z;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .end();
        // Per-segment instance data consumed as i_data0-3: endpoint A,
        // endpoint B, color at A, color at B. Only the stride matters for
        // instance buffers; the attributes just shape the layout.
        ms_instLayout
            .begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord5, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord4, 4, bgfx::AttribType::Float)
            .end();
        // Per-point instance data consumed as i_data0-1: position, color
        // (the point sprite path).
        ms_pointInstLayout
            .begin()
            .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bgfx::VertexLayout ms_instLayout;
    static bgfx::VertexLayout ms_pointInstLayout;
    static bool ms_initialized;
};


// Section-cap quad vertex: a world-space position on the clip plane plus
// hatch texture coordinates (vs_fc_cap).
struct CapVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;

    static void init()
    {
        if (ms_initialized)
            return;
        ms_initialized = true;
        // The normal is the cap plane's own, and it is here for the
        // depth+normal prepass: the section cap has to appear in
        // aoNormalZ, or every screen-space pass that reads it (cavity,
        // GTAO) keeps shading the geometry the cap hides and paints the
        // hidden creases back over it. The prepass programs take
        // a_position + a_normal, so carrying one lets the cap reuse them.
        ms_layout
            .begin()
            .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
    static bool ms_initialized;
};


// Content identity of a mesh's geometry: every attribute and index array
// EXCEPT the per-vertex colors (they ride their own stream). Two caches
// with the same key — e.g. the color variants of one shared tessellation,
// or the render cache's recolored/reindexed copies — share one set of
// geometry buffers. The hash is FNV-1a over the raw array bytes,
// computed once per cache id.
struct GeomKey
{
    uint64_t hash = 0;
    int numVertices = 0;
    int numTri = 0;
    int numLine = 0;
    int numPoint = 0;
    uint8_t hasNormals = 0;
    uint8_t hasTexCoords = 0;

    bool operator==(const GeomKey &o) const
    {
        return hash == o.hash && numVertices == o.numVertices
            && numTri == o.numTri && numLine == o.numLine
            && numPoint == o.numPoint && hasNormals == o.hasNormals
            && hasTexCoords == o.hasTexCoords;
    }
};

struct GeomKeyHasher
{
    size_t operator()(const GeomKey &k) const { return size_t(k.hash); }
};

inline uint64_t fnv1a64(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

inline GeomKey computeGeomKey(const Render::MeshData &mesh)
{
    GeomKey key;
    key.numVertices = mesh.numVertices;
    key.numTri = mesh.numTriangleIndices;
    key.numLine = mesh.numLineIndices;
    key.numPoint = mesh.numPointIndices;
    key.hasNormals = mesh.normals ? 1 : 0;
    key.hasTexCoords = mesh.texCoords ? 1 : 0;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t nv = size_t(mesh.numVertices);
    h = fnv1a64(h, mesh.positions, nv * 12);
    if (mesh.normals)
        h = fnv1a64(h, mesh.normals, nv * 12);
    if (mesh.texCoords)
        h = fnv1a64(h, mesh.texCoords, nv * 16);
    if (mesh.numTriangleIndices > 0)
        h = fnv1a64(h, mesh.triangleIndices,
                    size_t(mesh.numTriangleIndices) * 4);
    if (mesh.numLineIndices > 0)
        h = fnv1a64(h, mesh.lineIndices, size_t(mesh.numLineIndices) * 4);
    if (mesh.numPointIndices > 0)
        h = fnv1a64(h, mesh.pointIndices,
                    size_t(mesh.numPointIndices) * 4);
    key.hash = h;
    return key;
}

// Colorless GPU buffers of one geometry (GeomKey): shared by every cache
// whose arrays match. Buffers are immutable; entries are dropped when no
// referencing mesh used them recently.
/// Geometry bytes currently uploaded through GpuGeometry/GpuMesh — the
/// GPU-budget estimate where the backend API reports no memory stats
/// (GL does not). Buffer sizes are added at creation and taken back in
/// destroy(); render targets, shadow maps and textures are outside it,
/// deliberately: the budget steers *geometry* residency (§13 step 3),
/// and the fixed passes cost what they cost.
inline std::atomic<size_t> s_gpuGeometryBytes {0};

/// Bumped every time geometry handles are actually given back
/// (GpuGeometry::destroy). A creation the handle pool refused is worth
/// retrying only once something has been freed since -- a pool that is
/// full stays full, and rebuilding the vertex memory every frame for
/// every refused mesh would cost far more than the draws it is trying
/// to rescue. See BGFXView::tryUploadGeometry.
inline std::atomic<uint64_t> s_geomFreedEpoch {0};

struct GpuGeometry
{
    /// Position + normal vertex stream.
    bgfx::VertexBufferHandle vbh = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle tri = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle line = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle point = BGFX_INVALID_HANDLE;
    /// Seam-filtered line index set (hidden-line hideSeam); only created
    /// when the mesh carries a no-seam index set.
    bgfx::IndexBufferHandle lineNoSeam = BGFX_INVALID_HANDLE;
    /// Triangle-edge segment / corner instance data feeding the stencil
    /// outline passes (constant white colors), built lazily on first
    /// outline use. Instance i maps 1:1 onto triangle index position i
    /// (the edge leaving that corner), so partial index ranges translate
    /// directly into instance ranges.
    bgfx::VertexBufferHandle triEdgeInst = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle triCornerInst = BGFX_INVALID_HANDLE;
    /// triEdgeInst with the edges interior to a flat patch left out --
    /// the polygon boundary of the tessellated surface, which is what a
    /// wireframe draw style asks for (submitTessellation). Only built
    /// when such a draw arrives, and only when there is something to
    /// drop: a mesh whose every edge is a crease keeps using
    /// triEdgeInst. creaseEdgeFirst[t] is the instance the triangle t's
    /// kept edges start at, so a partial index range still maps onto an
    /// instance range.
    bgfx::VertexBufferHandle creaseEdgeInst = BGFX_INVALID_HANDLE;
    std::vector<uint32_t> creaseEdgeFirst;
    bool creaseEdgeBuilt = false;
    /// Texture-coordinate stream of textured draws, built lazily on
    /// first textured use.
    bgfx::VertexBufferHandle texcoord = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// Bytes this entry has uploaded (s_gpuGeometryBytes share).
    size_t bytes = 0;
    /// The frame this entry was last refused a vertex buffer on (0 =
    /// never), so a mesh denied by several passes of one frame counts
    /// once, and the destroy-epoch that refusal stood under, so the
    /// retry waits for handles to actually come back.
    uint64_t deniedFrame = 0;
    uint64_t deniedEpoch = ~uint64_t(0);

    void track(size_t add)
    {
        bytes += add;
        s_gpuGeometryBytes += add;
    }

    void destroy()
    {
        bool freed = false;
        for (auto ib : {&tri, &line, &point, &lineNoSeam}) {
            if (bgfx::isValid(*ib)) {
                bgfx::destroy(*ib);
                *ib = BGFX_INVALID_HANDLE;
                freed = true;
            }
        }
        creaseEdgeFirst.clear();
        creaseEdgeFirst.shrink_to_fit();
        creaseEdgeBuilt = false;
        for (auto vb : {&vbh, &triEdgeInst, &triCornerInst, &creaseEdgeInst,
                        &texcoord}) {
            if (bgfx::isValid(*vb)) {
                bgfx::destroy(*vb);
                *vb = BGFX_INVALID_HANDLE;
                freed = true;
            }
        }
        // Handles are back in the pool: whoever was refused one may ask
        // again.
        if (freed)
            ++s_geomFreedEpoch;
        s_gpuGeometryBytes -= bytes;
        bytes = 0;
    }

    void upload(const Render::MeshData &mesh)
    {
        SceneVertex::init();
        const bgfx::Memory *vmem =
            bgfx::alloc(uint32_t(mesh.numVertices) * sizeof(SceneVertex));
        auto *verts = reinterpret_cast<SceneVertex *>(vmem->data);
        for (int i = 0; i < mesh.numVertices; ++i) {
            SceneVertex &v = verts[i];
            v.px = mesh.positions[i*3];
            v.py = mesh.positions[i*3 + 1];
            v.pz = mesh.positions[i*3 + 2];
            if (mesh.normals) {
                v.nx = mesh.normals[i*3];
                v.ny = mesh.normals[i*3 + 1];
                v.nz = mesh.normals[i*3 + 2];
            } else {
                v.nx = v.ny = 0.0f;
                v.nz = 1.0f;
            }
        }
        vbh = bgfx::createVertexBuffer(vmem, SceneVertex::ms_layout);
        track(vmem->size);

        if (mesh.numTriangleIndices > 0) {
            tri = bgfx::createIndexBuffer(
                bgfx::copy(mesh.triangleIndices, mesh.numTriangleIndices * 4),
                BGFX_BUFFER_INDEX32);
            track(size_t(mesh.numTriangleIndices) * 4);
        }
        if (mesh.numLineIndices > 0) {
            line = bgfx::createIndexBuffer(
                bgfx::copy(mesh.lineIndices, mesh.numLineIndices * 4),
                BGFX_BUFFER_INDEX32);
            track(size_t(mesh.numLineIndices) * 4);
        }
        if (mesh.numPointIndices > 0) {
            point = bgfx::createIndexBuffer(
                bgfx::copy(mesh.pointIndices, mesh.numPointIndices * 4),
                BGFX_BUFFER_INDEX32);
            track(size_t(mesh.numPointIndices) * 4);
        }
    }

    void ensureNoSeam(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(lineNoSeam) || mesh.numNoSeamLineIndices <= 1)
            return;
        lineNoSeam = bgfx::createIndexBuffer(
            bgfx::copy(mesh.noSeamLineIndices,
                       mesh.numNoSeamLineIndices * 4),
            BGFX_BUFFER_INDEX32);
        track(size_t(mesh.numNoSeamLineIndices) * 4);
    }

    /// Texture-coordinate stream (Coin xyzw texcoords collapsed to 2D,
    /// projective coordinates divided per vertex), built on first
    /// textured use of the mesh; bound only by the textured programs so
    /// untextured scenes don't pay for it.
    void ensureTexCoord(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(texcoord) || !mesh.texCoords
                || mesh.numVertices == 0)
            return;
        TexCoordVertex::init();
        const bgfx::Memory *tmem = bgfx::alloc(
            uint32_t(mesh.numVertices) * sizeof(TexCoordVertex));
        auto *tc = reinterpret_cast<TexCoordVertex *>(tmem->data);
        for (int i = 0; i < mesh.numVertices; ++i) {
            const float *t = mesh.texCoords + i*4;
            float q = t[3] != 0.0f ? t[3] : 1.0f;
            tc[i].u = t[0] / q;
            tc[i].v = t[1] / q;
        }
        texcoord = bgfx::createVertexBuffer(tmem,
                                            TexCoordVertex::ms_layout);
        track(tmem->size);
    }

    /// Triangle-edge segment + corner instance buffers for the stencil
    /// outline passes, one instance per triangle index position. Built on
    /// first use: whole-scene hidden-line outlines would otherwise
    /// re-upload mesh-sized transient buffers every frame.
    void ensureOutline(const Render::MeshData &mesh)
    {
        if (bgfx::isValid(triEdgeInst)
                || mesh.numTriangleIndices < 3
                || !(bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING))
            return;
        LineQuadVertex::init();
        int n = mesh.numTriangleIndices;

        const bgfx::Memory *emem =
            bgfx::alloc(uint32_t(n) * 16 * sizeof(float));
        float *d = reinterpret_cast<float *>(emem->data);
        for (int i = 0; i < n; ++i, d += 16) {
            int t = i / 3, e = i % 3;
            int32_t ia = mesh.triangleIndices[i];
            int32_t ib = mesh.triangleIndices[t*3 + (e + 1) % 3];
            d[0] = mesh.positions[ia*3];
            d[1] = mesh.positions[ia*3 + 1];
            d[2] = mesh.positions[ia*3 + 2];
            // No stipple run: a triangle edge stands alone, and these
            // are never drawn patterned anyway (a zero length tells
            // the shader to count from A).
            d[3] = 0.0f;
            d[4] = mesh.positions[ib*3];
            d[5] = mesh.positions[ib*3 + 1];
            d[6] = mesh.positions[ib*3 + 2];
            d[7] = 0.0f;
            for (int c = 8; c < 16; ++c)
                d[c] = 1.0f;
        }
        triEdgeInst = bgfx::createVertexBuffer(
            emem, LineQuadVertex::ms_instLayout);
        track(emem->size);

        const bgfx::Memory *cmem =
            bgfx::alloc(uint32_t(n) * 8 * sizeof(float));
        d = reinterpret_cast<float *>(cmem->data);
        for (int i = 0; i < n; ++i, d += 8) {
            int32_t ip = mesh.triangleIndices[i];
            d[0] = mesh.positions[ip*3];
            d[1] = mesh.positions[ip*3 + 1];
            d[2] = mesh.positions[ip*3 + 2];
            d[3] = 0.0f;
            for (int c = 4; c < 8; ++c)
                d[c] = 1.0f;
        }
        triCornerInst = bgfx::createVertexBuffer(
            cmem, LineQuadVertex::ms_pointInstLayout);
        track(cmem->size);
    }

    /// The polygon boundary of the tessellated surface: triEdgeInst
    /// minus the edges a flat patch was split along. An edge shared by
    /// two triangles of the same plane is an artefact of triangulating
    /// a polygon, and GL never draws it -- the shapes this path serves
    /// (SoCube, SoFCBoundingBox: SoFCVertexCache's glrender shapes) are
    /// replayed by Coin as their own polygons under glPolygonMode. Only
    /// coplanar neighbours are dropped, so a tessellated curve keeps
    /// every edge it had.
    ///
    /// Built once per mesh, on the first wireframe draw of it. Leaves
    /// creaseEdgeInst invalid when nothing was dropped, so the common
    /// mesh costs no second buffer and the caller falls back to
    /// triEdgeInst.
    void ensureCreaseEdges(const Render::MeshData &mesh)
    {
        if (creaseEdgeBuilt)
            return;
        creaseEdgeBuilt = true;
        const int n = mesh.numTriangleIndices;
        if (n < 3 || !(bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING))
            return;
        LineQuadVertex::init();
        const int numTri = n / 3;

        auto normalOf = [&mesh](int t, float *out) {
            const int32_t *ix = mesh.triangleIndices + t*3;
            const float *p0 = mesh.positions + ix[0]*3;
            const float *p1 = mesh.positions + ix[1]*3;
            const float *p2 = mesh.positions + ix[2]*3;
            const float ax = p1[0]-p0[0], ay = p1[1]-p0[1], az = p1[2]-p0[2];
            const float bx = p2[0]-p0[0], by = p2[1]-p0[1], bz = p2[2]-p0[2];
            float nx = ay*bz - az*by;
            float ny = az*bx - ax*bz;
            float nz = ax*by - ay*bx;
            const float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            // A degenerate triangle has no plane to compare; a zero
            // normal fails every coplanarity test, so its edges stay.
            if (len > 1.0e-20f) {
                nx /= len; ny /= len; nz /= len;
            }
            else {
                nx = ny = nz = 0.0f;
            }
            out[0] = nx; out[1] = ny; out[2] = nz;
        };

        // Coplanar to within about a degree. Tight on purpose: this is
        // meant to catch a polygon split into triangles, not to
        // simplify a curved surface into feature lines.
        constexpr float kCoplanarDot = 0.99985f;

        std::vector<uint8_t> keep(size_t(n), 1);
        // First index position each edge was seen at; -1 once it has
        // been paired, so a third user of a non-manifold edge is kept
        // rather than silently matched again.
        std::unordered_map<uint64_t, int32_t> seen;
        seen.reserve(size_t(n));
        std::vector<float> normals(size_t(numTri) * 3);
        for (int t = 0; t < numTri; ++t)
            normalOf(t, &normals[size_t(t)*3]);

        int dropped = 0;
        for (int i = 0; i < n; ++i) {
            const int t = i / 3, e = i % 3;
            const int32_t ia = mesh.triangleIndices[i];
            const int32_t ib = mesh.triangleIndices[t*3 + (e + 1) % 3];
            const uint64_t lo = uint64_t(uint32_t(ia < ib ? ia : ib));
            const uint64_t hi = uint64_t(uint32_t(ia < ib ? ib : ia));
            const uint64_t key = (hi << 32) | lo;
            auto it = seen.find(key);
            if (it == seen.end()) {
                seen.emplace(key, int32_t(i));
                continue;
            }
            const int32_t j = it->second;
            it->second = -1;
            if (j < 0)
                continue;
            const float *na = &normals[size_t(t)*3];
            const float *nb = &normals[size_t(j / 3)*3];
            if (na[0]*nb[0] + na[1]*nb[1] + na[2]*nb[2] < kCoplanarDot)
                continue;
            keep[size_t(i)] = 0;
            keep[size_t(j)] = 0;
            dropped += 2;
        }

        // Nothing to drop, or nothing left to draw (a mesh that is one
        // flat patch seen from both sides): triEdgeInst already says it.
        if (dropped == 0 || dropped == n)
            return;

        creaseEdgeFirst.resize(size_t(numTri) + 1);
        const bgfx::Memory *emem = bgfx::alloc(
            uint32_t(n - dropped) * 16 * sizeof(float));
        float *d = reinterpret_cast<float *>(emem->data);
        uint32_t out = 0;
        for (int t = 0; t < numTri; ++t) {
            creaseEdgeFirst[size_t(t)] = out;
            for (int e = 0; e < 3; ++e) {
                const int i = t*3 + e;
                if (!keep[size_t(i)])
                    continue;
                const int32_t ia = mesh.triangleIndices[i];
                const int32_t ib = mesh.triangleIndices[t*3 + (e + 1) % 3];
                d[0] = mesh.positions[ia*3];
                d[1] = mesh.positions[ia*3 + 1];
                d[2] = mesh.positions[ia*3 + 2];
                // No stipple run: each edge starts its own pattern, the
                // way glLineStipple restarts on every polygon edge.
                d[3] = 0.0f;
                d[4] = mesh.positions[ib*3];
                d[5] = mesh.positions[ib*3 + 1];
                d[6] = mesh.positions[ib*3 + 2];
                d[7] = 0.0f;
                for (int c = 8; c < 16; ++c)
                    d[c] = 1.0f;
                d += 16;
                ++out;
            }
        }
        creaseEdgeFirst[size_t(numTri)] = out;
        creaseEdgeInst = bgfx::createVertexBuffer(
            emem, LineQuadVertex::ms_instLayout);
        track(emem->size);
    }
};

// Per-cache GPU buffers of one MeshData, keyed by MeshData::cacheId: the
// baked per-vertex color stream and the color-carrying line/point
// instance data, plus a reference to the shared colorless geometry. A
// cache id always refers to identical content, so buffers are immutable
// and reused until the id disappears from the scene.
struct GpuMesh
{
    /// Shared geometry buffers (owned by the geometry table; guaranteed
    /// to outlive this entry — see collectMeshes).
    GpuGeometry *geom = nullptr;
    /// Per-vertex rgba8 color stream; invalid when the cache carries no
    /// baked colors (the shared white buffer is bound instead).
    bgfx::VertexBufferHandle color = BGFX_INVALID_HANDLE;
    /// Per-vertex material stream (MeshData::materials, 8 bytes per
    /// vertex); invalid for uniform-material meshes, and then simply
    /// not bound — the shader only reads it when the draw's material
    /// sets perfacematerial.
    bgfx::VertexBufferHandle mats = BGFX_INVALID_HANDLE;
    /// Per-segment instance data (endpoints + colors) feeding the
    /// quad-expanded thick line path; invalid without instancing support.
    bgfx::VertexBufferHandle lineInst = BGFX_INVALID_HANDLE;
    /// Per-point instance data (position + color) feeding the point
    /// sprite path; invalid without instancing support.
    bgfx::VertexBufferHandle pointInst = BGFX_INVALID_HANDLE;
    /// Seam-filtered variant of lineInst (hidden-line hideSeam).
    bgfx::VertexBufferHandle lineNoSeamInst = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// MeshData::generation at upload — the arrays this entry was
    /// built from. A ladder refines its mesh in place under one
    /// cacheId, so the id alone no longer proves the upload current.
    uint32_t generation = 0;
    /// Bytes this entry has uploaded (s_gpuGeometryBytes share).
    size_t bytes = 0;

    void track(size_t add)
    {
        bytes += add;
        s_gpuGeometryBytes += add;
    }

    void destroy()
    {
        geom = nullptr;
        for (auto vb : {&color, &mats, &lineInst, &pointInst, &lineNoSeamInst}) {
            if (bgfx::isValid(*vb)) {
                bgfx::destroy(*vb);
                *vb = BGFX_INVALID_HANDLE;
            }
        }
        s_gpuGeometryBytes -= bytes;
        bytes = 0;
    }

    void upload(const Render::MeshData &mesh)
    {
        if (mesh.colors) {
            ColorVertex::init();
            color = bgfx::createVertexBuffer(
                bgfx::copy(mesh.colors, uint32_t(mesh.numVertices) * 4),
                ColorVertex::ms_layout);
            track(size_t(mesh.numVertices) * 4);
        }

        if (mesh.materials) {
            MatVertex::init();
            mats = bgfx::createVertexBuffer(
                bgfx::copy(mesh.materials,
                           uint32_t(mesh.numVertices) * sizeof(MatVertex)),
                MatVertex::ms_layout);
            track(size_t(mesh.numVertices) * sizeof(MatVertex));
        }

        if (mesh.numLineIndices > 1
                && (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)) {
            lineInst = makeSegmentInstances(
                mesh, mesh.lineIndices, mesh.numLineIndices);
            track(size_t(mesh.numLineIndices / 2) * 16 * sizeof(float));
        }

        if (mesh.numPointIndices > 0
                && (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING)) {
            LineQuadVertex::init();
            const bgfx::Memory *imem = bgfx::alloc(
                uint32_t(mesh.numPointIndices) * 8 * sizeof(float));
            float *d = reinterpret_cast<float *>(imem->data);
            for (int i = 0; i < mesh.numPointIndices; ++i, d += 8) {
                int32_t ip = mesh.pointIndices[i];
                d[0] = mesh.positions[ip*3];
                d[1] = mesh.positions[ip*3 + 1];
                d[2] = mesh.positions[ip*3 + 2];
                d[3] = 0.0f;
                if (mesh.colors) {
                    for (int c = 0; c < 4; ++c)
                        d[4 + c] = mesh.colors[ip*4 + c] / 255.0f;
                } else {
                    for (int c = 0; c < 4; ++c)
                        d[4 + c] = 1.0f;
                }
            }
            pointInst = bgfx::createVertexBuffer(
                imem, LineQuadVertex::ms_pointInstLayout);
            track(imem->size);
        }
    }

    /// What re-uploading this mesh as an edge (or point) drawable would
    /// cost, for the element gate's release to weigh against real
    /// headroom before it hands a class back.
    ///
    /// It has to count the GEOMETRY too, not just the instance buffer:
    /// a mesh every draw of which the gate suppressed is collected
    /// outright (collectMeshes), taking its vertices and index stream
    /// with it, so re-admission pays for all of it again. Pricing the
    /// instances alone under-reads by about a fifth, which is enough to
    /// approve a release that then blows the budget -- measured.
    ///
    /// It lives HERE, beside upload(), because it is the same
    /// arithmetic: a copy of these formulas kept anywhere else drifts
    /// the first time a stream is added to either.
    static uint64_t readmitCost(const Render::MeshData &mesh, bool lines)
    {
        uint64_t bytes = uint64_t(mesh.numVertices) * sizeof(SceneVertex);
        if (mesh.colors)
            bytes += uint64_t(mesh.numVertices) * 4;
        if (mesh.materials)
            bytes += uint64_t(mesh.numVertices) * sizeof(MatVertex);
        if (lines) {
            bytes += uint64_t(mesh.numLineIndices) * 4;
            if (mesh.numLineIndices > 1)
                bytes += uint64_t(mesh.numLineIndices / 2) * 16
                    * sizeof(float);
        }
        else {
            bytes += uint64_t(mesh.numPointIndices) * 4;
            bytes += uint64_t(mesh.numPointIndices) * 8 * sizeof(float);
        }
        return bytes;
    }

    /// One quad-expansion instance per line segment (endpoints + endpoint
    /// colors), from any GL_LINES style index set of the mesh.
    static bgfx::VertexBufferHandle makeSegmentInstances(
            const Render::MeshData &mesh, const int32_t *indices, int num)
    {
        LineQuadVertex::init();
        int nseg = num / 2;
        const bgfx::Memory *imem =
            bgfx::alloc(uint32_t(nseg) * 16 * sizeof(float));
        float *d = reinterpret_cast<float *>(imem->data);
        // Stipple phase, in the endpoints' spare w slots: how far along
        // its polyline this segment starts (model units) and how long
        // it is. The shader turns them into the pixel distance the
        // pattern counts. Without it the count restarts at every
        // segment and a dashed curve — which is many segments shorter
        // than one dash — draws solid.
        double run = 0.0;
        int32_t prevB = -1;
        for (int s = 0; s < nseg; ++s, d += 16) {
            int32_t ia = indices[s*2];
            int32_t ib = indices[s*2 + 1];
            // A polyline is what the feed flattened into pairs: as
            // long as a segment starts where the last one ended, the
            // run continues.
            if (ia != prevB)
                run = 0.0;
            prevB = ib;
            const float dx = mesh.positions[ib*3] - mesh.positions[ia*3];
            const float dy = mesh.positions[ib*3 + 1]
                - mesh.positions[ia*3 + 1];
            const float dz = mesh.positions[ib*3 + 2]
                - mesh.positions[ia*3 + 2];
            const float seglen = std::sqrt(dx*dx + dy*dy + dz*dz);
            d[0] = mesh.positions[ia*3];
            d[1] = mesh.positions[ia*3 + 1];
            d[2] = mesh.positions[ia*3 + 2];
            d[3] = float(run);
            d[4] = mesh.positions[ib*3];
            d[5] = mesh.positions[ib*3 + 1];
            d[6] = mesh.positions[ib*3 + 2];
            d[7] = seglen;
            run += seglen;
            if (mesh.colors) {
                for (int i = 0; i < 4; ++i) {
                    d[8 + i] = mesh.colors[ia*4 + i] / 255.0f;
                    d[12 + i] = mesh.colors[ib*4 + i] / 255.0f;
                }
            } else {
                for (int i = 0; i < 8; ++i)
                    d[8 + i] = 1.0f;
            }
        }
        return bgfx::createVertexBuffer(imem, LineQuadVertex::ms_instLayout);
    }

    /// Seam-filtered line buffers (hidden-line hideSeam), built on first
    /// use: a mesh may be uploaded before the hidden-line feed arrives
    /// (GpuMesh is keyed by cacheId), so upload() cannot see this data.
    /// The index set lands on the shared geometry, the color-carrying
    /// segment instances on this entry.
    void ensureNoSeam(const Render::MeshData &mesh)
    {
        geom->ensureNoSeam(mesh);
        if (bgfx::isValid(lineNoSeamInst)
                || mesh.numNoSeamLineIndices <= 1)
            return;
        if (bgfx::getCaps()->supported & BGFX_CAPS_INSTANCING) {
            lineNoSeamInst = makeSegmentInstances(
                mesh, mesh.noSeamLineIndices, mesh.numNoSeamLineIndices);
            track(size_t(mesh.numNoSeamLineIndices / 2) * 16
                  * sizeof(float));
        }
    }
};

// GPU texture of one Render::TextureImage, keyed by
// TextureImage::textureId (a texture id always refers to identical
// content, so textures are immutable and reused until unreferenced).
struct GpuTexture
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// This handle is the stand-in for a texture whose pixels had not
    /// arrived, and must be replaced by the real one when they do.
    bool placeholder = false;

    void destroy()
    {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    void upload(const Render::TextureImage &tex)
    {
        // Expand to RGBA8 (1/2 components are luminance(+alpha), the GL
        // fixed-function texel expansion). Rows are uploaded in the
        // GL bottom-up order the Coin image comes in, matching the GL
        // renderer on the OpenGL backend (other backends may see the
        // image v-flipped, a known deviation until needed).
        const size_t n = size_t(tex.width) * tex.height;
        // A streamed texture arrives as its header first and its pixels
        // later (Renderer.h), so a draw can reach here naming an image
        // that has a size but no bytes — and the expansion below would
        // read every one of them off the end of an empty vector. White
        // is the answer the deferral already promises: a modulating
        // draw renders as if untextured until the payload lands.
        // A float image is an ENVIRONMENT, and an environment is
        // consumed on the CPU (sampleEnvImage builds the prefiltered
        // cube). Nothing should route one here, and the expansion
        // below reads one byte a component -- so refuse rather than
        // walk a float payload as bytes.
        if (tex.sample == Render::TextureImage::F32
                || tex.pixels.size()
                       < n * size_t(tex.numComponents) * tex.sampleSize()) {
            const uint8_t white[4] = {255, 255, 255, 255};
            handle = bgfx::createTexture2D(
                1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                bgfx::copy(white, sizeof(white)));
            placeholder = true;
            return;
        }
        placeholder = false;
        std::vector<std::vector<uint8_t>> levels;
        levels.emplace_back(n * 4);
        {
            const uint8_t *src = tex.pixels.data();
            uint8_t *dst = levels.back().data();
            for (size_t i = 0; i < n; ++i) {
                const uint8_t *p = src + i * tex.numComponents;
                switch (tex.numComponents) {
                case 1: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = 255;
                        break;
                case 2: dst[0] = dst[1] = dst[2] = p[0]; dst[3] = p[1];
                        break;
                case 3: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                        dst[3] = 255; break;
                default: dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                         dst[3] = p[3]; break;
                }
                dst += 4;
            }
        }
        // Full mip chain, CPU 2x2 box filter (bgfx has no runtime mip
        // generation): closes the minification-speckle gap of the
        // texture rows — the GL renderer's fixed-function path stays
        // non-mipped, a known comparison-tolerance difference under
        // strong minification.
        int lw = tex.width, lh = tex.height;
        size_t total = levels.back().size();
        while (lw > 1 || lh > 1) {
            int nw = std::max(1, lw >> 1), nh = std::max(1, lh >> 1);
            const uint8_t *sp = levels.back().data();
            std::vector<uint8_t> lvl(size_t(nw) * nh * 4);
            for (int y = 0; y < nh; ++y) {
                int y0 = std::min(2 * y, lh - 1);
                int y1 = std::min(2 * y + 1, lh - 1);
                for (int x = 0; x < nw; ++x) {
                    int x0 = std::min(2 * x, lw - 1);
                    int x1 = std::min(2 * x + 1, lw - 1);
                    for (int c = 0; c < 4; ++c) {
                        int s = sp[(size_t(y0) * lw + x0) * 4 + c]
                            + sp[(size_t(y0) * lw + x1) * 4 + c]
                            + sp[(size_t(y1) * lw + x0) * 4 + c]
                            + sp[(size_t(y1) * lw + x1) * 4 + c];
                        lvl[(size_t(y) * nw + x) * 4 + c] =
                            uint8_t((s + 2) / 4);
                    }
                }
            }
            total += lvl.size();
            levels.push_back(std::move(lvl));
            lw = nw;
            lh = nh;
        }
        const bgfx::Memory *mem = bgfx::alloc(uint32_t(total));
        {
            uint8_t *dst = mem->data;
            for (const auto &lvl : levels) {
                std::memcpy(dst, lvl.data(), lvl.size());
                dst += lvl.size();
            }
        }
        // Anisotropic min/mag filtering: without it, textures viewed at a
        // grazing angle (the NaviCube face labels are the visible case) get
        // isotropically over-blurred along the foreshortened axis. The desktop
        // path sets setMaximumAnisotropy(4.0) for the same reason; match it
        // here. Harmless where the hardware lacks it (bgfx checks caps; WebGL
        // gates on EXT_texture_filter_anisotropic) and it also sharpens the
        // head-on minified case by taking higher-mip taps.
        uint64_t flags = BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        if (tex.wrapS == Render::TextureImage::Clamp)
            flags |= BGFX_SAMPLER_U_CLAMP;
        if (tex.wrapT == Render::TextureImage::Clamp)
            flags |= BGFX_SAMPLER_V_CLAMP;
        handle = bgfx::createTexture2D(
            uint16_t(tex.width), uint16_t(tex.height), true, 1,
            bgfx::TextureFormat::RGBA8, flags, mem);
    }
};

// GPU array texture of one Render::TexturePalette: the images a draw
// puts on its individual faces, as the layers of a single texture, so
// that one draw with one sampler can paint its faces differently.
//
// The layers of an array texture are all one size, and the palette's
// images are not -- so every layer is resampled onto the largest of
// them (bounded, since a palette of eight 4k images would be 512 MB).
// Keyed by the content of the palette, like GpuTexture: the same images
// in the same order always name the same array.
struct GpuTextureArray
{
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    uint64_t lastUsed = 0;
    /// Some layer's pixels had not arrived, so the array stands in for
    /// a palette that is still assembling and must be rebuilt when it
    /// has (GpuTexture::placeholder, one per array).
    bool placeholder = false;

    /// Longest side any layer is resampled to. A per-face image is a
    /// marking on one face of one part, not an environment; a generated
    /// material's maps are a surface someone authored and are allowed
    /// the larger ceiling below.
    static constexpr int MaxSide = 1024;
    static constexpr int MaterialSide = 2048;
    /// What the whole array may cost, whatever its caller asked for.
    /// The layers of an array are all one size, so "more layers" and
    /// "bigger layers" multiply -- sixteen 2k layers would be a quarter
    /// of a gigabyte before mips. Past this the layers are halved until
    /// they fit, which is a softer answer than refusing the material.
    static constexpr std::size_t MaxBytes = std::size_t(64) << 20;
    /// The layers that were not usable when the array was built: what
    /// `placeholder` is waiting on. The array is rebuilt when one of
    /// them becomes usable and not before -- a file that will never
    /// decode never triggers a rebuild, and a map that lands after any
    /// number of frames still does. (A bound of 120 rebuilds used to
    /// stand in for this and gave up on the browser tier, whose maps
    /// arrive over a network: an array built while they were in flight
    /// stayed white for good, and a metal piece with a white base and
    /// a white metalness map was a chrome reflection of the sky.)
    ///
    /// Layer INDICES, examined against the palette handed in on each
    /// call, never the image objects of the build: a streamed republish
    /// re-parses a shader into fresh objects under the same ids, and
    /// an array that watched the first publish's objects saw their
    /// pixels never arrive while the current ones had them.
    std::vector<uint16_t> waiting;
    bool arrived(const Render::TexturePalette &palette) const
    {
        for (uint16_t i : waiting) {
            if (i < palette.entries.size() && palette.entries[i]
                    && usable(*palette.entries[i]))
                return true;
        }
        return false;
    }

    void destroy()
    {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    /// Expand one image to RGBA8 at (w, h), bilinearly resampled -- the
    /// component expansion is GpuTexture::upload's (1/2 components are
    /// luminance(+alpha)), and the rows stay in the GL bottom-up order
    /// they arrive in.
    static void resample(const Render::TextureImage &tex, int w, int h,
                         uint8_t *dst)
    {
        const int nc = tex.numComponents;
        const uint8_t *src = tex.pixels.data();
        auto texel = [&](int x, int y, uint8_t *out) {
            const uint8_t *p = src + (size_t(y) * tex.width + x) * nc;
            switch (nc) {
            case 1: out[0] = out[1] = out[2] = p[0]; out[3] = 255; break;
            case 2: out[0] = out[1] = out[2] = p[0]; out[3] = p[1]; break;
            case 3: out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
                    out[3] = 255; break;
            default: out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
                     out[3] = p[3]; break;
            }
        };
        for (int y = 0; y < h; ++y) {
            // Pixel centres, so a layer already at the target size
            // resamples to itself exactly rather than to a half-texel
            // shifted copy of itself.
            const float sy = (float(y) + 0.5f) * float(tex.height)
                / float(h) - 0.5f;
            const int y0 = std::max(0, std::min(tex.height - 1,
                                                int(std::floor(sy))));
            const int y1 = std::max(0, std::min(tex.height - 1, y0 + 1));
            const float fy = std::max(0.0f, sy - float(y0));
            for (int x = 0; x < w; ++x) {
                const float sx = (float(x) + 0.5f) * float(tex.width)
                    / float(w) - 0.5f;
                const int x0 = std::max(0, std::min(tex.width - 1,
                                                    int(std::floor(sx))));
                const int x1 = std::max(0, std::min(tex.width - 1, x0 + 1));
                const float fx = std::max(0.0f, sx - float(x0));
                uint8_t p00[4], p10[4], p01[4], p11[4];
                texel(x0, y0, p00);
                texel(x1, y0, p10);
                texel(x0, y1, p01);
                texel(x1, y1, p11);
                uint8_t *out = dst + (size_t(y) * w + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    const float top = float(p00[c])
                        + (float(p10[c]) - float(p00[c])) * fx;
                    const float bot = float(p01[c])
                        + (float(p11[c]) - float(p01[c])) * fx;
                    out[c] = uint8_t(top + (bot - top) * fy + 0.5f);
                }
            }
        }
    }

    void upload(const Render::TexturePalette &palette,
                int maxLayers = Render::MaxFaceTexturePalette,
                int maxSide = MaxSide)
    {
        placeholder = false;
        waiting.clear();
        const uint16_t numLayers =
            uint16_t(std::min(palette.entries.size(),
                              std::size_t(std::max(maxLayers, 0))));
        if (!numLayers)
            return;
        // bgfx makes a plain 2D texture out of a one-layer request --
        // `1 < numLayers` is what picks GL_TEXTURE_2D_ARRAY -- and the
        // mesh shader samples this as an array (SAMPLER2DARRAY
        // s_texFace). So a palette holding a SINGLE image used to bind
        // a non-array texture to an array sampler and draw nothing at
        // all: every one-image case, which is most of them (a marking
        // on one face, and every glTF whose mesh names one base colour
        // image). Pad to two slices, exactly as the white stand-in
        // above already does; the pad is white, and no face names it
        // because u_faceTexParams.w still states the real count.
        const uint16_t numSlices = std::max<uint16_t>(numLayers, 2);
        // The array's own size: the largest layer, bounded. A palette
        // whose images have not all arrived still gets its array now --
        // the ones that have are drawn, and `placeholder` brings the
        // rest in when they land.
        int w = 1, h = 1;
        for (uint16_t i = 0; i < numLayers; ++i) {
            const auto &e = palette.entries[i];
            if (!e || !usable(*e)) {
                placeholder = true;
                waiting.push_back(i);
                continue;
            }
            w = std::max(w, int(e->width));
            h = std::max(h, int(e->height));
        }
        w = std::min(w, maxSide);
        h = std::min(h, maxSide);
        while ((w > 1 || h > 1)
               && std::size_t(w) * std::size_t(h) * 4u * numSlices > MaxBytes) {
            w = std::max(1, w >> 1);
            h = std::max(1, h >> 1);
        }
        const uint64_t flags = BGFX_SAMPLER_MIN_ANISOTROPIC
            | BGFX_SAMPLER_MAG_ANISOTROPIC;
        // WITH a mip chain, and it is not optional here: a face image
        // is laid out in millimetres, so a part zoomed to fit shows
        // several tiles across a few hundred pixels and an unmipped
        // checker boils into speckle the moment the camera moves.
        // bgfx has no runtime mip generation, so the levels are built
        // on the CPU exactly as GpuTexture::upload builds them.
        handle = bgfx::createTexture2D(uint16_t(w), uint16_t(h), true,
                                       numSlices,
                                       bgfx::TextureFormat::RGBA8, flags);
        if (!bgfx::isValid(handle))
            return;
        std::vector<uint8_t> level;
        std::vector<uint8_t> next;
        for (uint16_t i = 0; i < numSlices; ++i) {
            // Back to the full size for every layer: the mip loop below
            // walks this buffer down to 1x1, and the next layer's
            // resample writes a whole level into it. A padding slice
            // keeps the white it is filled with.
            level.assign(size_t(w) * h * 4, uint8_t(255));
            const auto *e = i < numLayers ? &palette.entries[i] : nullptr;
            if (e && *e && usable(**e))
                resample(**e, w, h, level.data());
            bgfx::updateTexture2D(handle, i, 0, 0, 0, uint16_t(w),
                                  uint16_t(h),
                                  bgfx::copy(level.data(),
                                             uint32_t(level.size())));
            int lw = w, lh = h;
            uint8_t mip = 1;
            while (lw > 1 || lh > 1) {
                const int nw = std::max(1, lw >> 1);
                const int nh = std::max(1, lh >> 1);
                next.assign(size_t(nw) * nh * 4, 0);
                for (int y = 0; y < nh; ++y) {
                    const int y0 = std::min(2 * y, lh - 1);
                    const int y1 = std::min(2 * y + 1, lh - 1);
                    for (int x = 0; x < nw; ++x) {
                        const int x0 = std::min(2 * x, lw - 1);
                        const int x1 = std::min(2 * x + 1, lw - 1);
                        for (int c = 0; c < 4; ++c) {
                            const int s =
                                level[(size_t(y0) * lw + x0) * 4 + c]
                                + level[(size_t(y0) * lw + x1) * 4 + c]
                                + level[(size_t(y1) * lw + x0) * 4 + c]
                                + level[(size_t(y1) * lw + x1) * 4 + c];
                            next[(size_t(y) * nw + x) * 4 + c] =
                                uint8_t((s + 2) / 4);
                        }
                    }
                }
                bgfx::updateTexture2D(handle, i, mip, 0, 0, uint16_t(nw),
                                      uint16_t(nh),
                                      bgfx::copy(next.data(),
                                                 uint32_t(next.size())));
                level.swap(next);
                lw = nw;
                lh = nh;
                ++mip;
            }
        }
    }

    /// Whether an image can be walked as bytes at all: a streamed one
    /// arrives as its header first and its pixels later, and a float
    /// image is an environment nothing should have routed here
    /// (GpuTexture::upload refuses both the same way).
    static bool usable(const Render::TextureImage &tex)
    {
        return tex.sample != Render::TextureImage::F32
            && tex.width > 0 && tex.height > 0
            && tex.numComponents > 0
            && tex.pixels.size() >= size_t(tex.width) * tex.height
                   * size_t(tex.numComponents) * tex.sampleSize();
    }
};

// Set the effective model transform of a draw. Plain draws use
// DrawCall::model as-is; autozoom draws replay the material's autozoom
// chain per frame like the GL renderer's setupMatrix: accumulate each
// entry's matrix Coin-style (multLeft — bx::mtxMul(a, m) with the same
// row-vector convention and memory layout), then apply the entry's node
// (SoAutoZoomTranslation::doAction: keep the accumulated rotation and the
// world position of the local origin, substitute the scale with
// scaleFactor times the per-frame world-to-screen scale), and multiply
// the draw's own matrix in last. The scale substitution extracts the
// rotation by normalizing the upper 3x3 rows, which matches Coin's
// getTransform for the rotation + translation + uniform positive scale
// matrices autozoom is used with (no shear support).
inline void setDrawTransform(const Render::DrawCall &draw,
                             float autozoomScale,
                             const float *viewMatrix,
                             const float *projMatrix,
                             float viewportHeight,
                             const Render::OverlayAnchor *overlayAnchor = nullptr,
                             float overlayRectHeight = 0.f)
{
    const auto &autozoom = draw.material.autozoom;
    if (autozoom.empty()) {
        if (!draw.identity)
            bgfx::setTransform(draw.model);
        return;
    }

    float m[16];
    bx::mtxIdentity(m);
    for (const auto &entry : autozoom) {
        // Each entry.matrix is the FULL accumulated model matrix at its
        // SoAutoZoomTranslation node (SoFCRenderCache::addAutoZoom stores
        // SoModelMatrixElement as-is), so it is absolute, not incremental —
        // the deepest autozoom on the path wins, matching Coin's nested
        // doAction (each substitutes scale on the current matrix). Assigning
        // it (rather than multiplying) is byte-identical for a single entry
        // (m starts identity) and fixes the case of two autozooms on one path
        // — e.g. a SoTextImage glyph companion placed under a node that
        // already carries its own autozoom (ViewProviderDatumCS), where the
        // former mtxMul double-counted the shared prefix.
        if (entry.resetmatrix && entry.identity)
            bx::mtxIdentity(m);
        else if (!entry.identity)
            std::memcpy(m, entry.matrix, sizeof(m));
        float sf = entry.scaleFactor == 0.0f
            ? 1.0f : entry.scaleFactor * autozoomScale;
        if (entry.billboard && viewMatrix && projMatrix && viewportHeight > 0.f) {
            // Screen-align: substitute the upper 3x3 with the camera basis so
            // the geometry always faces the viewer (SoText2-style text). The
            // camera's world-space axes are the columns of the view matrix's
            // 3x3 (row-vector layout: p_view = p_world * V). Keep the
            // accumulated translation (m[12..14]) as the anchor.
            const float *V = viewMatrix;
            const float *P = projMatrix;
            // Substituting the SCENE camera basis only screen-aligns the
            // content when the view the draw is submitted into carries the
            // scene's rotation as well, so that the two cancel: the main
            // scene, a sceneCamera overlay, and an orientFromScene overlay
            // (the corner axis cross, the NaviCube's labels).
            //
            // An overlay with its own fixed camera has no scene rotation to
            // cancel, and there the same substitution TILTS the content by
            // the camera instead of screen-aligning it. That is what the
            // foreground feed showed: FEM's colour bar renders on the
            // backend, but its value labels came out rotated with the model
            // (an isometric camera skewed them ~60 degrees), because the
            // foreground anchor is a fixed orthographic camera. Pixel-space
            // feeds are in the same position.
            const bool sceneOriented =
                !overlayAnchor || overlayAnchor->sceneCamera
                || overlayAnchor->orientFromScene;
            float right[3] = {1.0f, 0.0f, 0.0f};  // local X -> screen right
            float up[3]    = {0.0f, 1.0f, 0.0f};  // local Y -> screen up
            float fwd[3]   = {0.0f, 0.0f, 1.0f};  // local Z -> toward viewer
            if (sceneOriented) {
                // The camera's world-space axes are the columns of the view
                // matrix's 3x3 (row-vector layout: p_view = p_world * V).
                right[0] = V[0]; right[1] = V[4]; right[2] = V[8];
                up[0]    = V[1]; up[1]    = V[5]; up[2]    = V[9];
                fwd[0]   = V[2]; fwd[1]   = V[6]; fwd[2]   = V[10];
            }

            // Size the glyph screen-constant from the ANCHOR's own view-space
            // depth (not the global autozoomScale, which uses the orbit-centre
            // distance and so drifts for an off-centre label as the camera
            // orbits). A world length L at view depth d projects to
            // L*P[5]/d * (H/2) pixels (perspective) or L*P[5]*(H/2) (ortho), so
            // to render kBillboard screen pixels per native glyph pixel:
            //   sf = kBillboard * 2 / (P[5]*H)   [* d for perspective]
            float sfb;
            if (overlayAnchor) {
                // Overlay text (corner axis cross, NaviCube axis labels): the
                // draw is submitted into the overlay's own mini view, so P/H
                // above (the main scene projection + full viewport) do not
                // apply. The orientation substitution still uses the main
                // viewMatrix — correct, because orientFromScene builds the
                // overlay view from that same matrix, so the two cancel and the
                // label faces the viewer. Only the SIZE needs the overlay's own
                // projection + the label's depth in it.
                //
                // Size = screen-CONSTANT pixels (on-screen height ~= N*kGlyph
                // for an N-pixel glyph), identical in every overlay regardless
                // of the overlay's own pixel size — so the small axis-cross and
                // the larger NaviCube render their labels at the same absolute
                // on-screen size (kGlyph=1 => the label's own font pixel size).
                // Depth-corrected against the label's ACTUAL depth in the
                // mini-perspective (not the fixed camera distance), so the size
                // stays constant as the label orbits — the fixed-distance form
                // let near-side labels swell.
                const float H = overlayRectHeight > 0.f ? overlayRectHeight
                                                        : viewportHeight;
                float p5o;
                bool ovPersp;
                if (overlayAnchor->fovDeg > 0.f) {
                    p5o = 1.0f / std::tan(overlayAnchor->fovDeg
                                          * float(M_PI) / 360.0f);
                    ovPersp = true;
                }
                else {
                    p5o = 2.0f / std::max(overlayAnchor->orthoHeight, 1e-4f);
                    ovPersp = false;
                }
                const float kOverlayGlyph = 1.0f; // on-screen px per glyph px
                sfb = kOverlayGlyph * 2.0f / (p5o * H);
                if (ovPersp) {
                    // Label depth in the overlay view: its 3x3 is the scene view
                    // rotation (orientFromScene) and its z translation is
                    // -cameraDistance, so zview = a.(V z-basis) - cameraDistance.
                    const float ax = m[12], ay = m[13], az = m[14];
                    const float zov = ax*V[2] + ay*V[6] + az*V[10]
                                    - overlayAnchor->cameraDistance;
                    const float depth = -zov;
                    sfb *= (depth > 1e-4f ? depth : 1e-4f);
                }
            }
            else {
                const bool persp = std::abs(P[15]) < 1e-6f;
                const float ax = m[12], ay = m[13], az = m[14];
                const float zview = ax*V[2] + ay*V[6] + az*V[10] + V[14];
                const float depth = -zview;  // in front of the camera => positive
                const float p5 = std::abs(P[5]) > 1e-8f ? P[5] : 1.0f;
                // On-screen px per emitted unit: the entry's own pixel scale
                // when set (image quads in native pixels, 1:1 with raw GL),
                // else the glyph-legibility text factor.
                const float kBillboard =
                    entry.pixelscale > 0.f ? entry.pixelscale : 1.35f;
                sfb = kBillboard * 2.0f / (p5 * viewportHeight);
                if (persp)
                    sfb *= (depth > 1e-4f ? depth : 1e-4f);
            }

            for (int k = 0; k < 3; ++k) {
                m[0 + k] = right[k] * sfb;
                m[4 + k] = up[k]    * sfb;
                m[8 + k] = fwd[k]   * sfb;
            }
            m[3] = m[7] = m[11] = 0.0f;
        }
        else {
            for (int r = 0; r < 3; ++r) {
                float *row = m + r * 4;
                float len = std::sqrt(
                    row[0]*row[0] + row[1]*row[1] + row[2]*row[2]);
                float s = len > 1e-20f ? sf / len : 0.0f;
                row[0] *= s;
                row[1] *= s;
                row[2] *= s;
                row[3] = 0.0f;
            }
            if (entry.datumFlip && viewMatrix) {
                // Keep the glyph in its dimension plane but mirror its local
                // X/Y so the number reads upright from the current viewpoint —
                // a per-frame port of SoDatumLabel::GLRender's projected-axis +
                // backfacing test. Row 0/1 of m are the glyph's local X/Y axes
                // in world space; project them onto the view axes to see which
                // way they point on screen (row-vector: dir_view = dir_world*V).
                const float *V = viewMatrix;
                const float projX = m[0]*V[0] + m[1]*V[4] + m[2]*V[8];   // screen-x of local +X
                const float projY = m[4]*V[1] + m[5]*V[5] + m[6]*V[9];   // screen-y of local +Y
                // reflection in the model matrix inverts the decision
                const float det =
                    m[0]*(m[5]*m[10] - m[6]*m[9])
                  - m[1]*(m[4]*m[10] - m[6]*m[8])
                  + m[2]*(m[4]*m[9]  - m[5]*m[8]);
                const float margin = det < 0.f ? -1e-3f : 1e-3f;
                float xfactor = (-2.0f*projX < margin) ? 0.5f : -0.5f;
                float yfactor = (-2.0f*projY < margin) ? 0.5f : -0.5f;
                // backfacing: does the plane normal point away from the camera?
                // camera "toward viewer" world axis = view-matrix z column.
                const float *N = entry.normal;
                const float ndotz = N[0]*V[2] + N[1]*V[6] + N[2]*V[10];
                const bool backfacing = ndotz < 0.f;
                bool flip = backfacing ? (xfactor*yfactor > 0.f)
                                       : (xfactor*yfactor < 0.f);
                if (det < 0.f)
                    flip = !flip;
                if (flip)
                    xfactor = -xfactor;
                if (xfactor < 0.f) { m[0] = -m[0]; m[1] = -m[1]; m[2] = -m[2]; }
                if (yfactor < 0.f) { m[4] = -m[4]; m[5] = -m[5]; m[6] = -m[6]; }
            }
        }
        m[15] = 1.0f;  // the translation row m[12..14] stays
    }
    if (!draw.identity) {
        float tmp[16];
        bx::mtxMul(tmp, draw.model, m);
        std::memcpy(m, tmp, sizeof(m));
    }
    bgfx::setTransform(m);
}

inline void unpackColor(uint32_t rgba, float *out)
{
    out[0] = ((rgba >> 24) & 0xff) / 255.0f;
    out[1] = ((rgba >> 16) & 0xff) / 255.0f;
    out[2] = ((rgba >> 8) & 0xff) / 255.0f;
    out[3] = (rgba & 0xff) / 255.0f;
}

/// sRGB -> linear (IEC 61966-2-1); the inverse of what fs_fc_present
/// writes, and the C++ twin of the shaders' fcDecodeSRGB.
inline float decodeSRGB(float c)
{
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/// An AUTHORED colour -- one a person picked, which makes it a display
/// number and so sRGB-encoded. Decoded to linear when the pipeline is
/// colour managed, because everything downstream of here is arithmetic
/// on light (Render::OutputConfig; fc_color.sh does the same for the
/// 8-bit vertex streams this cannot reach).
///
/// ! Alpha is left alone: it is coverage, never light.
inline void unpackAuthoredColor(uint32_t rgba, float *out, bool managed)
{
    unpackColor(rgba, out);
    if (!managed)
        return;
    for (int i = 0; i < 3; ++i)
        out[i] = decodeSRGB(out[i]);
}

// FNV-1a accumulation for the shadow-map caster-set hash.
inline void hashBytes(uint64_t &h, const void *data, size_t len)
{
    auto p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

// 0xRRGGBBAA to the TransientVertex byte order (r,g,b,a in memory, i.e. what
// GpuMesh::upload memcpy's from MeshData::colors).
inline uint32_t vertexColor(uint32_t rgba)
{
    return ((rgba >> 24) & 0xff)
        | (((rgba >> 16) & 0xff) << 8)
        | (((rgba >> 8) & 0xff) << 16)
        | ((rgba & 0xff) << 24);
}

////////////////////////////////////////////////////////

/// The reserved "fc_emitter" parameter of a stateful particle
/// program (docs/RenderEngine.md §5.8): x = particle count,
/// y = fixed steps per second, z = freeze-frame warm-up seconds,
/// w = travel headroom as a fraction of the seed box diagonal, and
/// the next vector's x = the rate of the emitter's clock against the
/// wall clock. Absent or malformed leaves the defaults, which still
/// run — including a sender too old to carry the fifth lane, whose
/// emitters simply play in real time.
inline void emitterParams(const Render::UserShader &shader,
                          int &count, float &rate, float &warmup,
                          float *margin = nullptr,
                          float *timeScale = nullptr)
{
    count = 0;
    rate = 60.0f;
    warmup = 0.0f;
    if (margin)
        *margin = 0.0f;
    if (timeScale)
        *timeScale = 1.0f;
    for (const auto &p : shader.params) {
        if (p.name != "fc_emitter" || p.values.size() < 3)
            continue;
        count = int(p.values[0]);
        if (p.values[1] > 0.0f)
            rate = p.values[1];
        warmup = std::max(0.0f, p.values[2]);
        if (margin && p.values.size() >= 4)
            *margin = std::max(0.0f, p.values[3]);
        if (timeScale && p.values.size() >= 5)
            *timeScale = std::max(0.0f, p.values[4]);
        break;
    }
}

/// How far past its own bounds a draw can put pixels on screen.
///
/// Only a particle emitter has any: its seed geometry is a cloud of
/// zero-area quads at the spawn anchors, and the vertex stage flies
/// the billboards away from them, so the drawn result leaves the
/// box the vertices describe. The headroom is deliberately absent
/// from the geometry (buildEmitterSeedNodes) — it belongs to what
/// is culled, not to what is framed, spawned in, or streamed
/// against — so it is added back here, at the one consumer that
/// would otherwise cull a visible emitter the moment its anchors
/// left the frustum.
inline float drawHeadroom(const Render::DrawCall &d)
{
    const auto &sh = d.material.usershader;
    if (!sh || sh->stage != "particle")
        return 0.0f;
    int count = 0;
    float rate = 0.0f, warmup = 0.0f, margin = 0.0f;
    emitterParams(*sh, count, rate, warmup, &margin);
    if (margin <= 0.0f)
        return 0.0f;
    const float dx = d.bboxMax[0] - d.bboxMin[0];
    const float dy = d.bboxMax[1] - d.bboxMin[1];
    const float dz = d.bboxMax[2] - d.bboxMin[2];
    return margin * std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// The per-object display-mode resolution of ONE view
/// (docs/CoinRetirement.md 5.8, 5.9, 5.11), split out of BGFXView so
/// that the snapshot builder can ask the very same question of the
/// main view's style before it serves a scene (5.16). The rules are
/// subtle enough that a second copy of them would rot; there is one
/// implementation, in BGFXViewSubmit.cpp, and a view is simply the
/// state it reads. Every field is restated at the top of each frame
/// (BGFXFrame.cpp), so none of it is sub-view bank state.
class BGFXStyleState
{
public:
    /// The Class-A display style the submit in progress draws with
    /// (docs/CoinRetirement.md 5.7): a DrawStyleMask that admits a
    /// draw when `(mask >> material.type) & 1`. Restated from
    /// SubViewCtx at the top of every frame, so it is deliberately
    /// NOT a bank field -- a cell whose style changed needs nothing
    /// invalidated, the next frame simply filters differently.
    /// StyleAsIs on every frame outside a unified canvas, where the
    /// Coin traversal that produced the capture applied the style
    /// already.
    uint8_t drawStyleMask = Render::StyleAsIs;
    /// The style name behind drawStyleMask, and whether the capture is
    /// the SUPERSET child. Restated every frame beside the mask.
    uint8_t drawStyleName = 0;
    bool styleFromSuperset = false;
    /// This sub-view's style resolved as an ADDITIVE mode
    /// (docs/CoinRetirement.md 5.11): the style name's interned id and
    /// its DrawCall::interestBits bit, latched together at the top of
    /// the frame and BOTH zero unless the capture's interest list
    /// actually carries the mode. Non-zero means the style is served
    /// by the mode's own tagged draws -- the same three rules an
    /// override naming a mode follows -- instead of by a mask over the
    /// superset child.
    uint16_t drawStyleMode = 0;
    uint16_t drawStyleModeBit = 0;

    /// A per-object display mode override resolved against one draw's
    /// object (docs/CoinRetirement.md 5.9). `has` false = the object
    /// matched no entry (cached so the table is walked once per
    /// objectKey, not once per draw per frame).
    struct OvStyle {
        uint8_t mask = Render::StyleAsIs;
        uint8_t nameBit = 0;
        bool pin = false;
        bool has = false;
        /// Non-standard mode entry (docs/CoinRetirement.md 5.9
        /// "Non-standard modes"): the mode's interned id, and its
        /// DrawCall::interestBits bit under the capture's interest
        /// list (0 when the list does not carry it, in which case the
        /// mode was never captured and the entry resolves to the
        /// object's own mode). mask/nameBit are meaningless when
        /// modeId is set.
        uint16_t modeId = 0;
        uint16_t interestBit = 0;
    };
    /// Lazily filled objectKey -> override cache of ONE sub-view's
    /// table. Lazy rather than a bulk pass because updateObjectInfo()
    /// deliberately does not bump objectInfoVersion(): a key first
    /// seen after the bulk resolve would miss a pass-built table,
    /// while a lazy miss resolves it on first sight. Cleared when the
    /// table's version or the stated info version moves.
    struct OvCache {
        uint32_t tableVersion = 0;
        uint32_t infoVersion = 0;
        /// The capture-interest list's version (0 = none): the list
        /// defines the id->bit mapping the cached interestBit values
        /// were resolved under, so a moved list invalidates them.
        uint32_t interestVersion = 0;
        std::unordered_map<uint64_t, OvStyle> map;
    };
    /// Per sub-view id; erased with the bank in dropSubView.
    std::map<int, OvCache> subOvCaches;
    /// The submit in progress: the current sub-view's cache/table/info,
    /// latched at the top of the frame beside drawStyleMask. All null
    /// outside a frame whose view has overrides.
    OvCache *ovCache = nullptr;
    const Render::StyleOverrideTable *ovTable = nullptr;
    const Render::ObjectInfoMap *ovInfo = nullptr;
    /// The capture's additive-mode interest list (5.9 "Non-standard
    /// modes"), latched beside the table; what maps an entry's modeId
    /// to its DrawCall::interestBits bit.
    const Render::CaptureInterestTable *ovInterest = nullptr;
    /// The override for \a objectKey, or null (BGFXViewSubmit.cpp).
    const OvStyle *lookupStyleOverride(uint64_t objectKey);
    /// Whether this sub-view's per-object style resolution (override,
    /// then view style where registered, then own mode -- 5.8/5.9)
    /// admits \a draw's bucket. Asked by the per-draw submit AND by
    /// the instanced group partition: a group merges by geometry and
    /// material, not objectKey, so members can resolve differently.
    bool styleAdmits(const Render::DrawCall &draw);
};

class BGFXView : public BGFXStyleState
{
public:
    // Pass sequence reproducing (a simplified subset of) SoFCRenderer's
    // draw order. Each is a bgfx view sharing the same framebuffer.
    /// Stateful particle emitters simulated per view, and the fixed
    /// simulation steps each may take in one frame
    /// (docs/RenderEngine.md §5.8). Both are view-id budget: a viewer
    /// occupies a block of contiguous bgfx ids, sized to the passes its
    /// frames declare, out of the Render/MaxViewIds a session hands out
    /// (default 1024, ceiling BGFX_CONFIG_MAX_VIEWS in
    /// src/3rdParty/CMakeLists.txt), so these numbers are part of what
    /// says how many viewers can be open at once -- they widen the
    /// block, not the budget it comes from. Past the budget
    /// BGFXRendererLibP::getView refuses the viewer and it falls back
    /// to Coin rendering; it does not crash, and it does not silently
    /// share ids. A
    /// frame that cannot afford every step lets the simulation fall
    /// behind the clock rather than stretching the step — a stretched
    /// step is a different simulation.
    enum {
        kParticleSlots = 3,
        kParticleSteps = 2,
        kParticleViews = kParticleSlots * kParticleSteps,
    };
    /// How far behind the clock a live emitter may fall before the
    /// missed time is written off instead of simulated (seconds).
    ///
    /// The target is wall-clock, so time that passes while frames are
    /// not being drawn still counts: a stalled view, a slow frame, or a
    /// backgrounded browser tab (where requestAnimationFrame stops
    /// entirely and the gap is unbounded) all leave the simulation owing
    /// time. Repaying it at the full per-frame step budget is visible as
    /// the fountain fast-forwarding when the view comes back.
    ///
    /// A few frames of debt is worth repaying — that is an ordinary
    /// hitch, and catching up keeps the motion continuous. Beyond that
    /// the view was not being watched, so treat it as a pause: the
    /// emitter keeps its state and resumes from the current instant.
    /// The alternative, stretching the step to cover the gap, is a
    /// different simulation (see kParticleSteps).
    static constexpr float kParticleMaxLag = 0.25f;

    enum PassView {
        // The particle state steps come first on purpose: bgfx submits
        // views in id order, and every later pass that draws or shades
        // particles reads the state these wrote this frame.
        ViewParticleSim0 = 0,
        ViewParticleSimLast = ViewParticleSim0 + kParticleViews - 1,
        ViewParticleImpact, // impacts the steps just reported, scattered
                            // into the water impact map (one point per
                            // particle, own framebuffer): every emitter
                            // and every step of this frame splat into
                            // the same map, so one view id carries the
                            // lot (docs/RenderEngine.md §5.8)
        ViewBackground,     // clear + gradient background quad (clip space)
        ViewSunDisc,        // visible sun (disc + limb glow) along the
                            // directional scene light, over the
                            // background before any geometry — the
                            // opaque pass overdraws it (occlusion for
                            // free) and the bloom bright pass picks it
                            // up. Perspective cameras only
        ViewShadow,         // variance shadow map moments of shadow
                            // casting scene triangles, rendered from the
                            // scene light (own framebuffer, light camera)
        ViewShadowBlurH,    // separable gaussian blur of the shadow
                            // moments (ShadowSmoothBorder): horizontal
                            // into the ping texture ...
        ViewShadowBlurV,    // ... and vertical back into the moments
                            // texture, so receivers and the volumetric
                            // raymarch keep sampling the same target
        ViewShadowTint,     // glass casters render their per-channel
                            // light transmittance into the shadow tint
                            // map (cleared to white, multiplicative);
                            // receivers multiply it into the direct
                            // scene-light term — glass shadows are
                            // softer, tinted when colored
        ViewShadowTintBlurH, // ShadowSmoothBorder blur of the tint map
                            // (same separable gaussian as the moments —
                            // an unblurred tint edge would stay hard
                            // inside the smoothed penumbra) ...
        ViewShadowTintBlurV, // ... and vertical back into the tint map
        ViewBulbShadow0,    // shadow-map tiles of shadow-casting
        ViewBulbShadow1,    // light-source bodies (Render_LightShadow):
        ViewBulbShadow2,    // plain VSM moments of a wide downward
        ViewBulbShadow3,    // cone from each bulb (one tile), or of
        ViewBulbShadow4,    // six world-axis cube faces around it when
        ViewBulbShadow5,    // Render_LightShadowExtended is on. Tiles
        ViewBulbShadow6,    // allocate sequentially from a 4x4 atlas
        ViewBulbShadow7,    // grid; each re-renders only when the
        ViewBulbShadow8,    // casters or the bulb change (cached like
        ViewBulbShadow9,    // the scene shadow map).
        ViewBulbShadow10,
        ViewBulbShadow11,
        ViewBulbShadow12,
        ViewBulbShadow13,
        ViewBulbShadow14,
        ViewBulbShadow15,
        ViewAOPrepass,      // SSAO depth+normal prepass of opaque scene
                            // triangles into a non-MSAA RGBA16F target
                            // (own framebuffer, own depth)
        ViewAOPrepassCap,   // section caps of clipped opaque solids into
                            // that same target, so the screen-space
                            // passes reading it (cavity, GTAO) see the
                            // cut as the flat surface it is rather than
                            // the geometry behind it. A view of its own
                            // because the parity marking must precede
                            // the quad, which only a Sequential view
                            // guarantees, and because the stencil the
                            // marking inverts wants clearing first --
                            // neither belongs on the main prepass
        ViewAODepthMip1,    // GTAO prefiltered depth pyramid (XeGTAO's
        ViewAODepthMip2,    // depth MIP chain): each level halves the
        ViewAODepthMip3,    // previous — fullscreen weighted-downsample
        ViewAODepthMip4,    // passes reading the prepass / prior level;
        ViewAODepthMip5,    // far horizon taps of the GTAO pass read the
        ViewAODepthMip6,    // coarse levels (long-range occlusion without
                            // sparse full-res taps, cache-coherent out to
                            // the pixel radius cap)
        ViewWaterFront,     // nearest front-face depths of water body
                            // draws (prepass shader family, own
                            // framebuffer): per-pixel entry of the
                            // volumetric water medium
        ViewWaterBack,      // farthest back-face depths of water body
                            // draws (depth GREATER, cleared to 0): the
                            // medium exit; no front but a back face
                            // means the camera is under water
        ViewGlassFront,     // nearest front-face depths of glass body
                            // draws (prepass shader family, own
                            // framebuffer): entry of the absorption
                            // interval of the glass surface pass
        ViewGlassBack,      // farthest back-face depths of glass body
                            // draws (depth GREATER, cleared to 0): the
                            // absorption interval exit
        ViewCloudFront,     // nearest front-face depths of cloud body
                            // draws (prepass shader family, own
                            // framebuffer): entry of the cloud medium
                            // interval of the volumetric raymarch
        ViewCloudBack,      // farthest back-face depths of cloud body
                            // draws (depth GREATER, cleared to 0): the
                            // cloud medium exit
        ViewFireFront,      // nearest front-face depths of fire body
                            // draws (prepass shader family, own
                            // framebuffer): entry of the emissive flame
                            // interval of the volumetric raymarch
        ViewFireBack,       // farthest back-face depths of fire body
                            // draws (depth GREATER, cleared to 0): the
                            // flame interval exit
        ViewAOGen,          // fullscreen SSAO generation into the R8 AO
                            // target (hemisphere kernel over the prepass)
        ViewAOBlur,         // fullscreen 4x4 AO blur into a second R8
        ViewAOBlur2,        // GTAO only: second edge-aware denoise pass,
                            // ping-ponged back into the first R8 target
                            // (XeGTAO's DenoisePasses > 1)
        ViewVolGen,         // volumetric light shafts: half-res raymarch
                            // of the shadow map through the scattering
                            // medium, bounded by the prepass depth (own
                            // half-res framebuffer, scene transforms for
                            // position reconstruction)
        ViewVolAccum,       // temporal accumulation of the raymarch:
                            // current outputs blended into the history
                            // targets (constant-factor blend, reset to
                            // full replace on camera/scene change); the
                            // apply passes read the history
        ViewGroundRefl,     // ground reflection: the opaque scene
                            // re-rendered with the world mirrored about
                            // the shadow ground plane (own framebuffer,
                            // flipped culling, reflected shadow matrix);
                            // the overlay view blends it onto the ground
        ViewReflMedia,      // fountain/fire bodies composited into the
                            // mirrored-scene texture (analytic cylinder
                            // raymarch from the mirror camera) so the
                            // water/ground reflection shows the plume
                            // and flame, not what stands behind them
        ViewOpaque,         // opaque triangles, lines, points
        ViewSelection,      // opaque draws of non-on-top selections
                            // (SoFCRenderer's opaqueselections bucket):
                            // GL renders them painter-style right after
                            // the opaque scene, so coincident geometry
                            // (a selected sketch edge over its own scene
                            // line) resolves to the highlight color via
                            // LEQUAL. A Sequential view keeps that order
                            // where ViewOpaque's state sorting would not.
        ViewOcclusionProbe, // measurement only (docs/FarFieldProxies.md
                            // §10.1, RenderDebug_Occlusion): bounding
                            // boxes of spatial-index nodes rasterized
                            // against the opaque depth under occlusion
                            // queries, writing no colour and no depth.
                            //
                            // Here, directly after the opaque bucket,
                            // rather than at the end of the frame: the
                            // occluders are exactly the draws that
                            // wrote depth, and transparent geometry
                            // writes none (submit() forces depthwrite
                            // off for it), so nothing later adds an
                            // occluder. Everything after this point —
                            // OIT accumulation and resolve, the water
                            // and glass surface passes, the multisample
                            // resolve the copy passes force — rebinds
                            // or resolves the scene framebuffer, and a
                            // depth test asked after that is not asked
                            // of the depth the scene wrote.
        ViewSectionCap,     // stencil section caps of clipped opaque
                            // solids (GL: _renderSection; before the
                            // outline/transparent passes like the GL
                            // opaque-loop caps, and the cap parity
                            // marking needs the stencil buffer before
                            // the outline passes leave their marks)
        ViewDebugScene,     // debug scene re-render (docs/RenderDebug.md
                            // modes 6/8/11): scene draws re-rasterized
                            // into a dedicated full-res target —
                            // additive fragment counting for the
                            // overdraw heatmap, depth-tested texcoord
                            // output for the UV mode, or every draw's
                            // own identity for the instance-id mode; the
                            // ViewDebug blit samples the result.
                            // (Repurposes the retired AO-apply slot —
                            // the fullscreen AO multiply moved into the
                            // mesh shaders' ambient terms, aoMeshTex at
                            // unit 9.)
        ViewIdReadback,     // blit-only view of the cull audit
                            // (RenderDebug_CullAudit): copies the id
                            // image into a readback texture. Its own
                            // view id because bgfx runs a view's blits
                            // BEFORE its draws -- asked on ViewDebugScene
                            // the copy would carry the previous frame's
                            // image, and comparing a verdict against a
                            // frame-old picture is the very error this
                            // instrument was built to rule out.
        ViewCavity,         // fullscreen cavity (curvature) multiply over
                            // the finished opaque scene: a one-pixel
                            // second derivative of the prepass normals
                            // darkening creases and ridges. After every
                            // opaque pass (section caps included) so it
                            // states the shape of everything solid, and
                            // before the outlines and the transparent
                            // bucket, which must stay crisp -- curvature
                            // read off a silhouette edge is meaningless,
                            // so the shader rejects depth steps rather
                            // than haloing them.
        ViewGroundReflApply, // ground reflection overlay: the mirrored
                            // scene blended onto the shadow ground quad
                            // (depth EQUAL against the ground's own
                            // depth); the reflection itself samples no
                            // screen AO, so it is not AO-darkened twice
        ViewOutline,        // hidden-line stencil outlines of scene draws
                            // (after all opaque geometry so the depth
                            // test sees the whole scene, before the
                            // transparent bucket blends over them)
        ViewConsumerScene0, // an attached FrameConsumer's scene-run
                            // passes (Renderer::setFrameConsumer,
                            // docs/CAMSimRenderPort.md sec 10.2): a
                            // module outside the engine -- the CAM
                            // simulator -- drawing through the
                            // immediate-mode facade on this view's
                            // ids, into this view's scene target.
                            // Placed while the scene is still being
                            // composed: everything from here on --
                            // caustics, the volumetric apply, water
                            // and glass surfaces, WBOIT and its
                            // composite -- sees the consumer's opaque
                            // output and the depth its composite
                            // writes, so transparent document geometry
                            // blends OVER the consumer's image instead
                            // of being occluded by it. Live only while
                            // a consumer is registered, and only as
                            // many as it asked for
        ViewConsumerScene1,
        ViewConsumerScene2,
        ViewConsumerScene3,
        ViewConsumerScene4,
        ViewConsumerScene5,
        ViewConsumerScene6,
        ViewConsumerScene7,
        ViewConsumerScene8,
        ViewConsumerScene9,
        ViewConsumerScene10,
        ViewConsumerScene11,
        ViewConsumerScene12,
        ViewConsumerScene13,
        ViewConsumerScene14,
        ViewConsumerScene15, // sized for the simulator's thirteen (two
                            // geometry passes, the AO effect's nine,
                            // resolve, composite) with headroom for
                            // the next consumer
        ViewCaustics,       // fullscreen additive water caustics splat
                            // over the prepass surfaces inside the water
                            // interval — before the volumetric apply so
                            // the extinction multiply absorbs the
                            // caustic light on its way to the eye
        ViewVolApply,       // fullscreen bilateral upsample of the
                            // half-res inscatter, composited onto the
                            // opaque scene before the transparent bucket
                            // (after the outlines so edges are fogged
                            // like the fills)
        ViewWaterCopy,      // fullscreen copy of the scene color into
                            // the sampleable refraction source (also
                            // forces the multisample resolve), after the
                            // volumetric composite so the refracted view
                            // carries the underwater tint and shafts
        ViewWaterSurface,   // water body draws re-rendered as the water
                            // surface: screen-space refraction from the
                            // copy, Fresnel environment reflection, sun
                            // glint; replaces their transparent-bucket
                            // rendering while the surface is enabled.
                            // The pass composites the raymarch's
                            // in-front-of-the-water inscatter itself —
                            // else a fountain plume / fire over the
                            // pool reads as behind the surface
        ViewGlassLineSdf,   // scene lines and points behind a glass body,
                            // rasterized as a signed-distance field for
                            // ViewGlassSurface to resample. Runs before
                            // it, and only while glassActive.
        ViewGlassSurface,   // glass body draws re-rendered as glass:
                            // screen-space refraction (IOR + normal),
                            // per-channel thickness absorption from the
                            // glass front/back interval, Fresnel
                            // environment reflection; replaces their
                            // ordinary rendering
        ViewGlassLine,      // scene lines and points, moved out of
                            // ViewOpaque while a glass body is on
                            // screen. A screen-space refraction resamples
                            // the scene copy through a per-pixel UV
                            // displacement, and wherever that field
                            // converges -- which is what a curved glass
                            // body IS -- it magnifies whatever it
                            // samples. A CAD edge went in one pixel wide
                            // and came out two or three, smeared further
                            // by the bilinear fetch and, on rough glass,
                            // by the 16-tap disc. There is no fixing that
                            // in the glass shader: the line was already
                            // rasterized before the lens saw it. So the
                            // lines are simply not in the copy -- they
                            // land here instead, after the refraction, at
                            // the exact pixel width they asked for.
                            // Lines the glass hides are not dropped:
                            // they go into ViewGlassLineSdf as a
                            // distance field the glass pass resamples,
                            // so they warp with the face they lie on and
                            // still keep their stated pixel width. Only
                            // while glassActive: with
                            // no glass body the lines stay in ViewOpaque
                            // and nothing about their ordering changes.
                            // The cost of being here is that these lines
                            // miss ViewVolApply, so they are not fogged
                            // by a volumetric the way the fills are.
        ViewParticles,      // blended user particle draws (a "particle"
                            // stage program whose Blend is not Default).
                            // Their own view because the bucket they
                            // used to share, ViewOpaque, runs BEFORE the
                            // volumetric composite: a particle does not
                            // write depth, so the fog resolved the pixel
                            // it covered at the depth of whatever was
                            // behind it and multiplied the full
                            // background extinction over the sprite —
                            // every droplet of a fountain came out
                            // ringed with fog it is nowhere near. Here
                            // the sprites land after the inscatter, and
                            // after the water and glass surfaces, so
                            // spray over a pool reads as being in front
                            // of it (the same reason ViewWaterSurface
                            // composites its own in-front inscatter).
                            // Kept out of ViewTransparent so the sprites
                            // do not enter WBOIT: an emitter is
                            // thousands of tiny quads with no meaningful
                            // per-fragment depth, and the additive ones
                            // are commutative anyway.
        ViewTransparent,    // transparent triangles: WBOIT accumulation
                            // into the OIT targets, or blended
                            // back-to-front into the scene FBO when OIT
                            // is unavailable
        ViewOITComposite,   // fullscreen WBOIT resolve onto the scene FBO
        ViewSectionCapTransp, // section caps of clipped transparent
                            // solids, after the transparent bucket like
                            // GL's grouped section pass; stencil-cleared
                            // because the outline views left marks
        ViewConsumerOverlay0, // an attached FrameConsumer's
                            // overlay-run passes
                            // (FrameConsumer::overlayPasses,
                            // docs/CAMSimRenderPort.md sec 10.2):
                            // translucent consumer output that must
                            // test the frame's FINISHED depth -- the
                            // tool path's hidden-line x-ray. Inside
                            // the composite like the scene run (bloom,
                            // the user post stage, debug and
                            // accumulation all see it), but after the
                            // WBOIT resolve, so it draws over the
                            // blended transparents; the on-top,
                            // highlight and overlay buckets still
                            // draw over it
        ViewConsumerOverlay1,
        ViewConsumerOverlay2,
        ViewConsumerOverlay3, // four: the simulator needs two, and a
                            // consumer wanting more re-opens the
                            // split's sizing (docs sec 10.7)
        ViewBloomBright,    // bloom bright pass: the finished scene
                            // (opaque + water + transparent, before the
                            // on-top/UI buckets) box-downsampled to the
                            // quarter-res halo source, thresholded with
                            // a soft knee
        ViewBloomEmit,      // light-source bodies (Render_Light)
                            // re-rendered additively into the halo
                            // source at their HDR emission color
                            // (diffuse * intensity), depth-rejected
                            // against the prepass — the halo scales
                            // with the intensity even though the LDR
                            // scene clips their own pixels
        ViewBloomBlurH,     // separable gaussian of the halo source:
                            // horizontal into the ping target ...
        ViewBloomBlurV,     // ... and vertical back into the source
        ViewBloomApply,     // the blurred halo added onto the scene
                            // (blend ONE/ONE, scaled by the bloom
                            // intensity)
        ViewUserPostCopy,   // user "post" shader (docs/RenderDebug.md §6)
                            // input: resolve/copy of the composited scene
                            // color into userPostTex, so the user pass can
                            // read and write color without feedback
        ViewUserPost,       // the user post-stage program drawn fullscreen
                            // back into the scene target — after bloom, so
                            // it sees the final composited color, before
                            // ViewDebug/on-top/highlight/overlays so debug
                            // visualization and UI still draw on top
        ViewDebug,          // render debugging buffer visualization
                            // (docs/RenderDebug.md): when the RenderDebug
                            // view mode is active, a fullscreen blit
                            // overwrites the scene color with an
                            // intermediate target (prepass depth/normal,
                            // AO term, shadow term) — before the
                            // on-top/highlight/overlay passes so those
                            // still draw on top and the view stays
                            // navigable
        ViewOnTop,          // scene geometry with on-top materials
        ViewHighlight,      // selection-on-top and preselection highlight
        ViewOverlay0,       // overlay feeds (Renderer::setOverlay — the
                            // foreground superimposition, the corner axis
                            // cross): each active overlay slot renders
                            // late into its own view with an
                            // anchor-derived viewport + camera and a
                            // fresh depth inside its rect
        ViewOverlay1,
        ViewOverlay2,
        ViewOverlay3,
        ViewOverlay4,
        ViewOverlay5,
        ViewOverlay6,
        ViewOverlay7,
        ViewOverlay8,       // headroom: the overlay ids (foreground, axis,
                            // graphics-items, fps, navi-cube, navi-buttons,
                            // editing, dimensions) can all be active at once
        ViewAccum,          // idle temporal accumulation: the finished
                            // frame -- scene, effects and overlays alike
                            // -- averaged into the history target under
                            // a constant-factor blend. Last of the
                            // drawing passes on purpose: everything
                            // ahead of it renders under the jittered
                            // projection, so everything ahead of it is
                            // what converges, and an overlay drawn from
                            // its own unjittered camera is bit-identical
                            // frame to frame and averages to itself
        ViewAccumApply,     // the accumulated history copied back over
                            // the scene color, so the blit and the
                            // present path downstream see the converged
                            // image without knowing this ran
        ViewPresent,        // standalone build only: fullscreen copy of
                            // the scene color onto the default backbuffer
                            // (the desktop build GL-blits into the Qt
                            // framebuffer instead)
        ViewCaptureDepth,   // fullscreen depth encode for a frame
                            // capture: the scene depth attachment
                            // sampled into a colour target, because
                            // colour is what bgfx blits and reads back
                            // on every backend (fs_fc_depthenc).
        ViewCapture,        // blit-only view of a frame capture: copies
                            // the finished scene colour and the encoded
                            // depth into their readback textures.
                            //
                            // ! LAST, and that is the whole point.
                            // bgfx runs views in id order, so a capture
                            // asked anywhere earlier would copy the
                            // scene colour before cavity, bloom, the
                            // temporal accumulation and the output
                            // transform had written to it -- a picture
                            // of a half-finished frame. Its own id
                            // rather than ViewPresent's for the reason
                            // ViewIdReadback has one: a view's blits
                            // run BEFORE its draws.
        NUM_VIEWS
    };
    // ! Counted to the first pass AFTER the overlay block, not to
    // ViewPresent: anything inserted between the two has to leave this
    // reading 9, or the overlay loop claims ids that belong to it.
    enum { NumOverlayViews = ViewAccum - ViewOverlay0 };
    /// Pass ids an attached FrameConsumer may claim, per run
    /// (docs/CAMSimRenderPort.md sec 10.2). Same rule as the overlay
    /// block above: counted between enum entries, so inserting a pass
    /// into a run cannot desync it.
    enum { NumConsumerSceneViews = ViewCaustics - ViewConsumerScene0 };
    enum { NumConsumerOverlayViews = ViewBloomBright - ViewConsumerOverlay0 };

    /// One stateful emitter's particle state (docs/RenderEngine.md
    /// §5.8): two RGBA32F attachment pairs that ping-pong once per
    /// fixed step. `pos` holds xyz position + age, `vel` holds xyz
    /// velocity + lifetime. Sized by particle count, not by the
    /// window, so it survives a resize — which is why it lives outside
    /// BGFXView::destroy().
    struct ParticleState {
        bgfx::FrameBufferHandle fbo[2] = {BGFX_INVALID_HANDLE,
                                          BGFX_INVALID_HANDLE};
        bgfx::TextureHandle pos[2] = {BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
        bgfx::TextureHandle vel[2] = {BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
        /// What each step REPORTED rather than what it remembers:
        /// xyz = where this particle struck (model space), w = how
        /// hard (0 = it did not). Per step, so both buffers of a
        /// two-step frame hold a report the splat pass still owes the
        /// impact map — which is the reason it ping-pongs with the
        /// state instead of being one shared target.
        bgfx::TextureHandle imp[2] = {BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
        /// One quad per particle carrying its index and its corner,
        /// and nothing else: the impact splat's payload comes from
        /// `imp`, so its vertex buffer is a counter.
        bgfx::VertexBufferHandle idxVb = BGFX_INVALID_HANDLE;
        /// Buffers written by this frame's steps, oldest first, and
        /// how many — what the splat pass has to drain.
        int stepBuf[kParticleSteps] = {};
        int stepCount = 0;
        /// The clock this frame's impacts are stamped with. Live, that
        /// is the shared animation clock. Frozen, the animation clock
        /// stands still at zero while the emitter walks to its warm-up
        /// over several frames, so the emitter's own simulated time is
        /// the only reading under which a ring can age — and it makes
        /// the rings of a frozen frame a function of the warm-up, like
        /// everything else about it.
        float stamp = 0.0f;
        /// The emitter's model matrix this frame — how the splat pass
        /// turns a model-space impact into a place in the world.
        float model[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                           0, 0, 1, 0, 0, 0, 0, 1};
        bool modelIdentity = true;
        uint16_t gridW = 0;         ///< state texture size in texels
        uint16_t gridH = 0;
        int count = 0;              ///< particles (<= gridW * gridH)
        /// Identity of what produced this state: the step program
        /// source, the particle count and the emitter seed. A change
        /// is a different simulation, so the state resets.
        uint64_t identity = 0;
        int cur = 0;                ///< buffer holding the live state
        float simTime = 0.0f;       ///< seconds simulated since reset
        bool needInit = true;
        uint32_t lastFrame = 0;     ///< view frame of the last step
        int slot = -1;              ///< sim view slot this frame, or -1

        void destroy()
        {
            for (int i = 0; i < 2; ++i) {
                if (bgfx::isValid(fbo[i])) {
                    bgfx::destroy(fbo[i]);
                    fbo[i] = BGFX_INVALID_HANDLE;
                }
            }
            for (int i = 0; i < 2; ++i) {
                for (auto tex : {&pos[i], &vel[i], &imp[i]}) {
                    if (bgfx::isValid(*tex)) {
                        bgfx::destroy(*tex);
                        *tex = BGFX_INVALID_HANDLE;
                    }
                }
            }
            if (bgfx::isValid(idxVb)) {
                bgfx::destroy(idxVb);
                idxVb = BGFX_INVALID_HANDLE;
            }
            stepCount = 0;
        }
    };
    /// Live particle states keyed by the draw's objectKey — the stable
    /// per-occurrence identity, so two occurrences of one emitter
    /// object simulate independently and a moved emitter keeps its
    /// particles.
    std::map<uint64_t, ParticleState> particles;

    // -----------------------------------------------------------------
    // Handle registry. Every bgfx handle the view owns as a member
    // appears here EXACTLY ONCE, in destruction order (a framebuffer
    // strictly before the textures it references), tagged with its
    // lifetime:
    //
    //   LifeSized -- sized by the window / created by init(): released
    //                by destroy() and recreated by the next init().
    //   LifeView  -- created once per view and kept across the
    //                resize-driven destroy()/init() cycles (validity
    //                guards in init() skip re-creation): released only
    //                when the view ends.
    //
    // destroy() and the destructor both sweep this one list instead of
    // keeping hand-maintained copies -- a handle listed here cannot be
    // released by one of them and leaked by the other, which is the
    // bug class the old per-method hand lists produced. Handles a
    // framebuffer owns (created with destroyTextures=true, e.g. the
    // sink attachments) are NOT listed; their owner's release
    // invalidates them by hand.
    //   LifeProgram -- shader programs, uniforms and the stand-in
    //                textures: size-independent, and linking them is the
    //                single most expensive thing this class does (Intel's
    //                GL JIT ~5 s for this set). A resize keeps them --
    //                init(keepShared) skips destroyPrograms() -- and only
    //                a shader-generation or MSAA change releases them.
    //                ! Because a resize keeps them while re-running the
    //                whole of init(), every one of them must be created
    //                through ensureUniform()/ensureProgram() (or an
    //                equivalent isValid guard): a plain re-creation is
    //                deduped by bgfx, but by refcounting UP, so the
    //                handle is never returned to the pool.
    enum HandleLife : uint8_t { LifeSized, LifeView, LifeProgram };
    template <typename Fn>
    void forEachHandle(Fn &&fn)
    {
        // whiteColorVb is deliberately absent: it is the uploaded
        // scene's white color stream, not a viewport-sized target, and
        // destroySceneCaches() releases it together with the
        // whiteColorCount that gates its recreation. Sweeping it as
        // LifeSized split the two across buckets, and a keepShared init
        // runs only destroyTargets(): the buffer died while the count
        // still read 4096, so whiteColors() skipped the rebuild and
        // handed a destroyed handle to the first mesh without vertex
        // colors -- the water surface -- which is a bgfx fatal.
        // SSAO/debug-scene resources: framebuffers before the textures
        // they reference.
        fn(debugSceneFbo, LifeSized);
        fn(captureDepthFbo, LifeSized);
        fn(aoPrepassFbo, LifeSized);
        fn(aoGenFbo, LifeSized);
        fn(aoBlurFbo, LifeSized);
        for (auto &h : aoMipFbo)
            fn(h, LifeSized);
        fn(debugSceneTex, LifeSized);
        fn(debugSceneDepth, LifeSized);
        fn(idReadTex, LifeSized);
        fn(captureDepthTex, LifeSized);
        fn(captureColorRead, LifeSized);
        fn(captureDepthRead, LifeSized);
        fn(aoNormalZ, LifeSized);
        fn(aoDepth, LifeSized);
        fn(aoTex, LifeSized);
        fn(aoBlurTex, LifeSized);
        fn(aoNoiseTex, LifeSized);
        for (auto &h : aoMipTex)
            fn(h, LifeSized);
        // Volumetric / medium / bloom resources: the framebuffers
        // before their textures.
        fn(volFbo, LifeSized);
        fn(volHistFbo, LifeSized);
        fn(bloomFbo, LifeSized);
        fn(bloomBlurFbo, LifeSized);
        fn(bulbShadowFbo, LifeSized);
        fn(waterFrontFbo, LifeSized);
        fn(waterBackFbo, LifeSized);
        fn(glassFrontFbo, LifeSized);
        fn(glassBackFbo, LifeSized);
        fn(lineSdfFbo, LifeSized);
        fn(cloudFrontFbo, LifeSized);
        fn(cloudBackFbo, LifeSized);
        fn(fireFrontFbo, LifeSized);
        fn(fireBackFbo, LifeSized);
        fn(sceneCopyFbo, LifeSized);
        fn(presentFbo, LifeSized);
        fn(accumFbo, LifeSized);
        fn(reflFbo, LifeSized);
        fn(volTex, LifeSized);
        fn(volFrontTex, LifeSized);
        fn(volHistTex, LifeSized);
        fn(volHistFrontTex, LifeSized);
        fn(bloomTex, LifeSized);
        fn(bloomBlurTex, LifeSized);
        fn(bulbShadowTex, LifeProgram);
        fn(bulbShadowDepth, LifeProgram);
        fn(waterFrontTex, LifeSized);
        fn(waterBackTex, LifeSized);
        fn(waterFrontDepth, LifeSized);
        fn(waterBackDepth, LifeSized);
        fn(glassFrontTex, LifeSized);
        fn(glassBackTex, LifeSized);
        fn(glassFrontDepth, LifeSized);
        fn(glassBackDepth, LifeSized);
        fn(cloudFrontTex, LifeSized);
        fn(cloudBackTex, LifeSized);
        fn(cloudFrontDepth, LifeSized);
        fn(cloudBackDepth, LifeSized);
        fn(fireFrontTex, LifeSized);
        fn(fireBackTex, LifeSized);
        fn(fireFrontDepth, LifeSized);
        fn(fireBackDepth, LifeSized);
        fn(sceneCopyTex, LifeSized);
        fn(presentTex, LifeSized);
        fn(accumTex, LifeSized);
        fn(reflTex, LifeSized);
        fn(reflDepth, LifeSized);
        fn(s_texVol, LifeProgram);
        fn(s_texVolFront, LifeProgram);
        fn(u_volParams, LifeProgram);
        fn(u_volMedium, LifeProgram);
        fn(u_volTexel, LifeProgram);
        fn(s_texWaterFront, LifeProgram);
        fn(s_texWaterBack, LifeProgram);
        fn(u_waterSigma, LifeProgram);
        fn(u_causticParams, LifeProgram);
        fn(s_texScene, LifeProgram);
        fn(s_texRefl, LifeProgram);
        fn(u_waterSurf, LifeProgram);
        fn(u_waterAbsorb, LifeProgram);
        fn(u_waterRipple, LifeProgram);
        fn(u_reflParams, LifeProgram);
        fn(u_groundPlane, LifeProgram);
        fn(u_groundFadeU, LifeProgram);
        fn(u_groundFadeV, LifeProgram);
        fn(s_texGlassFront, LifeProgram);
        fn(s_texGlassBack, LifeProgram);
        fn(s_texLineSdf, LifeProgram);
        fn(s_texLineSdfAux, LifeProgram);
        fn(u_glassParams, LifeProgram);
        fn(u_glassTint, LifeProgram);
        fn(s_texCloudFront, LifeProgram);
        fn(s_texCloudBack, LifeProgram);
        fn(u_cloudParams, LifeProgram);
        fn(s_texFireFront, LifeProgram);
        fn(s_texFireBack, LifeProgram);
        fn(u_fireParams, LifeProgram);
        fn(u_fireParams2, LifeProgram);
        fn(u_fireFrame, LifeProgram);
        fn(u_fountainParams, LifeProgram);
        fn(u_fountainFrame, LifeProgram);
        fn(u_waterSplash, LifeProgram);
        fn(u_mediumSlot, LifeProgram);
        fn(m_progPrepass, LifeProgram);
        fn(m_progPrepassClip, LifeProgram);
        fn(m_progMedDepth, LifeProgram);
        fn(m_progMedDepthClip, LifeProgram);
        fn(m_progPrepassInst, LifeProgram);
        fn(m_progSsao, LifeProgram);
        fn(m_progGtao, LifeProgram);
        fn(m_progGtaoBlur, LifeProgram);
        fn(m_progGtaoDepth, LifeProgram);
        fn(m_progSsaoBlur, LifeProgram);
        fn(m_progCavity, LifeProgram);
        fn(m_progVol, LifeProgram);
        fn(m_progVolAccum, LifeProgram);
        fn(m_progReflMedia, LifeProgram);
        fn(m_progBloomBright, LifeProgram);
        fn(m_progBloomEmit, LifeProgram);
        fn(m_progBloomBlur, LifeProgram);
        fn(m_progBloomApply, LifeProgram);
        fn(m_progSun, LifeProgram);
        fn(m_progEnvBg, LifeProgram);
        fn(m_progVolApply, LifeProgram);
        fn(m_progVolExt, LifeProgram);
        fn(m_progCaustics, LifeProgram);
        fn(m_progWaterCopy, LifeProgram);
        fn(m_progWater, LifeProgram);
        fn(m_progGlass, LifeProgram);
        fn(m_progGroundRefl, LifeProgram);
        fn(m_progGroundShadow, LifeProgram);
        fn(m_progGroundShadowPlane, LifeProgram);
        fn(m_progGroundFade, LifeProgram);
        fn(m_progGroundFadeTex, LifeProgram);
        fn(m_progGroundFadePrepass, LifeProgram);
        // Shadow resources: the framebuffers before their textures.
        fn(shadowFbo, LifeSized);
        fn(shadowBlurFbo, LifeSized);
        fn(shadowBlurBackFbo, LifeSized);
        fn(shadowTintFbo, LifeSized);
        fn(shadowTintBlurFbo, LifeSized);
        fn(shadowTintBlurBackFbo, LifeSized);
        fn(shadowTex, LifeSized);
        fn(shadowDepth, LifeSized);
        fn(shadowBlurTex, LifeSized);
        fn(shadowTintTex, LifeSized);
        fn(shadowTintBlurTex, LifeSized);
        fn(m_progShadow, LifeProgram);
        fn(m_progShadowClip, LifeProgram);
        fn(m_progShadowInst, LifeProgram);
        fn(m_progShadowBlur, LifeProgram);
        fn(m_progShadowTint, LifeProgram);
        fn(s_texShadow, LifeProgram);
        fn(s_texShadowTint, LifeProgram);
        fn(s_texAOScreen, LifeProgram);
        fn(u_debugParams, LifeProgram);
        fn(s_texDebugScene, LifeProgram);
        fn(s_texSceneDepth, LifeProgram);
        fn(u_shadowParams, LifeProgram);
        fn(u_lightDir, LifeProgram);
        fn(u_lightPos, LifeProgram);
        fn(u_lightColor, LifeProgram);
        fn(u_shadowMatrix, LifeProgram);
        fn(u_shadowBlur, LifeProgram);
        fn(u_evsm, LifeProgram);
        fn(u_localLight, LifeProgram);
        fn(u_localLightColor, LifeProgram);
        fn(u_viewLight, LifeProgram);
        fn(u_viewLightColor, LifeProgram);
        fn(u_viewLightAtt, LifeProgram);
        fn(s_texBloom, LifeProgram);
        fn(u_bloomParams, LifeProgram);
        fn(u_bloomTexel, LifeProgram);
        fn(u_bloomBlur, LifeProgram);
        fn(u_sunParams, LifeProgram);
        fn(s_texBulbShadow, LifeProgram);
        fn(u_bulbShadowMtx, LifeProgram);
        fn(u_bulbShadowConf, LifeProgram);
        fn(u_bulbShadowRot, LifeProgram);
        // PBR environment resources.
        fn(m_envTex, LifeProgram);
        fn(m_envBgTex, LifeProgram);
        fn(m_dummyEnvTex, LifeProgram);
        fn(s_texNormalZ, LifeProgram);
        fn(s_texAONoise, LifeProgram);
        fn(s_texAO, LifeProgram);
        for (auto &h : s_texAOMip)
            fn(h, LifeProgram);
        fn(u_aoParams, LifeProgram);
        fn(u_aoParams2, LifeProgram);
        fn(u_cavityParams, LifeProgram);
        fn(u_aoKernel, LifeProgram);
        fn(s_texEnv, LifeProgram);
        fn(u_pbrParams, LifeProgram);
        fn(u_outputParams, LifeProgram);
        fn(u_colorSpace, LifeProgram);
        fn(u_matcapParams, LifeProgram);
        fn(u_envSH, LifeProgram);
        fn(s_texBump, LifeProgram);
        fn(u_bumpParams, LifeProgram);
        fn(u_finishParams, LifeProgram);
        fn(u_frameParams, LifeProgram);
        fn(s_texEmissive, LifeProgram);
        fn(s_texOcclusion, LifeProgram);
        fn(s_texMetallicRoughness, LifeProgram);
        fn(s_texFace, LifeProgram);
        fn(u_faceTexParams, LifeProgram);
        // The OIT framebuffer references bgfxDepth (owned by bgfxFbo),
        // so it goes first; the sink framebuffer owns its attachments
        // (sinkColor/sinkDepth are invalidated by the sweep caller).
        fn(oitFbo, LifeSized);
        fn(oitAccum, LifeSized);
        fn(oitReveal, LifeSized);
        fn(bgfxFbo, LifeSized);
        fn(sinkFbo, LifeSized);
        fn(m_progMesh, LifeProgram);
        fn(m_progMeshInst, LifeProgram);
        fn(m_progMeshInstTex, LifeProgram);
        fn(m_progMeshInstOit, LifeProgram);
        fn(m_progMeshInstOitTex, LifeProgram);
        fn(u_instParams, LifeProgram);
        fn(m_progFlat, LifeProgram);
        fn(m_progMeshClip, LifeProgram);
        fn(m_progFlatClip, LifeProgram);
        fn(m_progLine, LifeProgram);
        fn(m_progLineClip, LifeProgram);
        fn(m_progLinePat, LifeProgram);
        fn(m_progLinePatClip, LifeProgram);
        fn(m_progPoint, LifeProgram);
        fn(m_progPointClip, LifeProgram);
        fn(m_progMeshTex, LifeProgram);
        fn(m_progMeshTexClip, LifeProgram);
        fn(m_progMeshOitTex, LifeProgram);
        fn(m_progMeshOitTexClip, LifeProgram);
        fn(s_texColor, LifeProgram);
        fn(u_texMatrix, LifeProgram);
        fn(u_texParams, LifeProgram);
        fn(u_texBlendColor, LifeProgram);
        fn(m_progMeshOit, LifeProgram);
        fn(m_progMeshOitClip, LifeProgram);
        fn(m_progComp, LifeProgram);
        fn(m_progDebug, LifeProgram);
        fn(m_progDebugScene, LifeProgram);
        fn(m_progDebugSceneClip, LifeProgram);
        fn(m_progDepthEnc, LifeProgram);
        fn(m_progCap, LifeProgram);
        fn(m_progCapClip, LifeProgram);
        fn(s_texHatch, LifeProgram);
        fn(m_whiteTex, LifeProgram);
        fn(m_blackTex, LifeProgram);
        fn(m_whiteTexArray, LifeProgram);
        fn(m_hatchTex, LifeProgram);
        fn(s_texAccum, LifeProgram);
        fn(s_texReveal, LifeProgram);
        fn(m_lineQuadVb, LifeSized);
        fn(m_lineQuadIb, LifeSized);
        fn(u_matColor, LifeProgram);
        fn(u_matEmissive, LifeProgram);
        fn(u_matSpecular, LifeProgram);
        fn(u_ambient, LifeProgram);
        fn(u_envAmbient, LifeProgram);
        fn(u_params, LifeProgram);
        fn(u_polyOffset, LifeProgram);
        fn(u_clipParams, LifeProgram);
        fn(u_clipPlanes, LifeProgram);
        fn(u_linePattern, LifeProgram);
        fn(m_progPresent, LifeProgram);
        // Per-view-lifetime resources. The impact map is sized by the
        // map resolution, not by the window; the stateful-particle
        // programs/uniforms are created once per view and kept across
        // the resize-driven destroy()/init() cycles (see the validity
        // guard in init()).
        fn(impactFbo, LifeView);
        fn(impactTex, LifeView);
        fn(m_progPSimInit, LifeView);
        fn(m_progPImpact, LifeView);
        fn(s_pstate0, LifeView);
        fn(s_pstate1, LifeView);
        fn(u_pgrid, LifeView);
        fn(u_pboxMin, LifeView);
        fn(u_pboxMax, LifeView);
        fn(s_pimpsrc, LifeView);
        fn(u_impactFrame, LifeView);
        fn(u_impactNow, LifeView);
        fn(s_texImpact, LifeView);
        fn(u_waterImpact, LifeView);
        fn(u_waterImpactCfg, LifeView);
    }
    template <typename H>
    static void releaseHandle(H &h)
    {
        if (bgfx::isValid(h)) {
            bgfx::destroy(h);
            h = BGFX_INVALID_HANDLE;
        }
    }
    /// Release every registry handle of the given lifetime, in list
    /// (i.e. dependency) order.
    void sweepHandles(HandleLife life);

    ~BGFXView();

    void destroy();
    /// The uploaded scene (meshes, geometries, textures): kept across a
    /// resize, which is why it is not part of destroyTargets().
    void destroySceneCaches();
    /// Everything sized by the viewport; the one set a plain resize drops.
    void destroyTargets();
    /// Programs, uniforms and stand-in textures: expensive to relink, so
    /// a resize keeps them.
    void destroyPrograms();

    bgfx::TextureHandle createTexture(bgfx::TextureFormat::Enum format, uint64_t flags = 0,
                                      bool sampled = false);

    /// Target sets that are allocated only while something wants them,
    /// rather than wherever the GPU merely permits them. Together they
    /// are ~352MB of a ~554MB per-view footprint at 1080p, and a
    /// default configuration draws none of them.
    ///
    /// ! The order must match the name table in updateEffect().
    enum EffectGroup : uint8_t {
        EffectVolumetric,  ///< raymarch pair + water/cloud/fire intervals
        EffectBulbShadow,  ///< the fixed 2048^2 bulb shadow atlas
        EffectReflection,  ///< the mirrored-camera re-render target
        EffectBloom,       ///< quarter-res halo + blur ping
        EffectSSAO,        ///< depth+normal prepass, AO chain, glass interval
        EffectShadow,      ///< the scene light's shadow maps (moments,
                           ///< blur ping, glass tint pair)
        EffectPresent,     ///< the output colour transform's target
        EffectAccum,       ///< the idle temporal accumulation history
        NumEffectGroups
    };
    /// Does this group's framebuffer set exist right now?
    bool effectAllocated(EffectGroup g) const;
    /// Build / release one group. Prefer updateEffect().
    bool allocEffect(EffectGroup g);
    void freeEffect(EffectGroup g);
    /// Reconcile a group against demand, once per frame.
    ///
    /// ! \a want must be a pure CONFIGURATION predicate. Passing a
    /// frame's *Active flag would fold in scene content ("a water body
    /// is on screen this frame") and free the set across ordinary
    /// editing, only to rebuild it moments later.
    void updateEffect(EffectGroup g, bool want);
    /// A group whose allocation failed: not retried until the pool has
    /// a real chance again (a resize, or the config turning it off and
    /// back on). Retrying every frame is what made a full pool spin.
    bool effectFailed[NumEffectGroups] = {};
    /// A glass body has been seen in this view's scene.
    ///
    /// The one consumer of the SSAO group that has no preference at all
    /// -- glass is a material, so its demand is scene state, and the
    /// rule above says scene state may add to a group's demand but
    /// never take it away. Latching the sighting is how that demand
    /// joins a predicate the configuration also drives: without it,
    /// "config off, glass on screen" would free the group and rebuild
    /// it in the same frame, every frame. Cleared with the targets it
    /// speaks for (destroyTargets), so a resize is where a document
    /// that no longer has glass gives the 98MB back.
    bool glassSeen = false;

    void init(bool keepShared = false);

    /// Radiance of the environment for a world direction (Z up, unit
    /// length). With a user image (PBRConfig::envImage) that image is
    /// sampled — a 2:1 image as equirectangular (lat-long), anything
    /// squarer as a GL sphere map, the convention the Texture mapping
    /// dialog's Environment mode uses for the same file. Otherwise the
    /// fixed procedural studio environment: a vertical
    /// ground/horizon/sky gradient plus three broad light lobes
    /// (key/fill/rim), all values fixed so frames stay deterministic.
    /// Modest HDR range either way.
    void envRadiance(const float d[3], float out[3]) const;

    /// Bilinear lookup of a user environment image along a world
    /// direction. Coin's SoTextureCoordinateEnvironment (and fixed
    /// function GL_SPHERE_MAP) maps a reflection vector to the unit
    /// disc of a sphere/light-probe image, so a roughly square image is
    /// read that way; a 2:1 image is the usual lat-long panorama. The
    /// image is Y-up in world terms: Z is up in FreeCAD.
    /// \a managed selects the photo's decode: the exact inverse of
    /// the output encode when the pipeline is colour managed, and
    /// otherwise the squaring these frames were always drawn with.
    static void sampleEnvImage(const Render::TextureImage &img,
                               const float d[3], float out[3],
                               bool managed);

    /// Reads m_envPreset, so it is a member and not static any more.
    void envRadianceProcedural(const float d[3], float out[3]) const;

    // World direction of a cube face texel; standard GL/D3D face order
    // and orientation (+x, -x, +y, -y, +z, -z), u/v in [-1, 1].
    static void cubeDir(int face, float u, float v, float d[3]);

    // Build the image based lighting data once per view: a
    // GGX-prefiltered RGBA16F cubemap (shader lod = roughness * 5, the
    // 1-2 px tail mips stay at full roughness) and the cosine-convolved
    // irradiance SH of the same environment in Ramamoorthi's polynomial
    // form, basis and 1/pi constants folded so the shader evaluates
    // plain dot products. CPU cost is a one-off ~2M radiance samples.
    void ensureEnvironment();

    // Build the background cubemap (m_envBgTex) the same environment
    // is drawn FROM: the base level at kEnvBgSize, then plain box
    // downsamples all the way to 1x1, which is what the blur factor
    // slides along. Kept apart from the lighting cube because the two
    // want different things -- see kEnvBgSize -- and called from
    // ensureEnvironment, so both are rebuilt by the same invalidation.
    void buildEnvBackground();

    /// Give \a geom its buffers if it has none, and account for the
    /// refusal when the handle pool has none left to give. The draw
    /// sites all skip an invalid vertex buffer rather than fatally
    /// binding it, so a refusal is silent geometry loss -- it has to be
    /// counted, and it has to be retried.
    ///
    /// Retried only once handles have actually come back
    /// (s_geomFreedEpoch): the pool does not refill on its own, and
    /// upload() rebuilds the whole vertex stream before it asks, so
    /// retrying every refused mesh every frame would cost far more than
    /// the draws it is trying to rescue. The epoch is read back AFTER
    /// the attempt because destroy() below may bump it.
    void tryUploadGeometry(GpuGeometry &geom, const Render::MeshData &data)
    {
        if (bgfx::isValid(geom.vbh))
            return;
        if (geom.deniedEpoch != s_geomFreedEpoch.load()) {
            // Give back the partial upload before asking again:
            // upload() creates every buffer unconditionally and tracks
            // its bytes, so a second call over a half-built entry would
            // orphan the index buffers it already holds and count them
            // twice.
            geom.destroy();
            geom.upload(data);
        }
        if (bgfx::isValid(geom.vbh))
            return;
        geom.deniedEpoch = s_geomFreedEpoch.load();
        ++bufferDeniedSubmits;
        if (geom.deniedFrame != frame) {
            geom.deniedFrame = frame;
            ++bufferDeniedMeshes;
        }
    }

    GpuMesh *getMesh(const Render::MeshData &data)
    {
        GpuMesh &mesh = meshes[data.cacheId];
        mesh.lastUsed = frame;
        // "A cache id always refers to identical content" stopped
        // being true when meshes grew rungs: every level of a ladder
        // fills ONE mesh object under one cacheId, in place
        // (docs/SceneStreaming.md §7), and a mesh being drawn is never
        // idle long enough for the two-frame purge to retire its
        // upload. Trusting the id alone kept the coarse buffers under
        // exact-sized index counts — "Insufficient buffer size" from
        // the driver, a scene whose books said exact while the screen
        // showed facets. The generation is bumped by every in-place
        // fill and release; when it moves, the upload is of a mesh
        // that no longer exists. The shared geometry entry stays for
        // whoever still matches it and ages out on its own.
        if (mesh.geom && mesh.generation != data.generation)
            mesh.destroy();
        if (!mesh.geom) {
            mesh.generation = data.generation;
            GeomKey key = computeGeomKey(data);
            auto res = geometries.emplace(key, GpuGeometry());
            GpuGeometry &geom = res.first->second;
            tryUploadGeometry(geom, data);
            mesh.geom = &geom;
            mesh.upload(data);
            static const bool dbgfeed =
                (getenv("FC_BGFX_DEBUG_FEED") != nullptr);
            if (dbgfeed)
                fprintf(stderr,
                        "bgfx mesh cache=%llx nv=%d geom=%llx%s\n",
                        (unsigned long long)data.cacheId,
                        data.numVertices,
                        (unsigned long long)key.hash,
                        res.second ? "" : " shared");
        }
        // A geometry refused a vertex buffer keeps a cached entry that
        // has none, and the branch above never runs for it again: it
        // was never retried, and a mesh drawn every frame keeps its
        // lastUsed current so the two-frame purge never retires it
        // either. That mesh stayed invisible for the life of the
        // process, behind one warning printed once.
        else if (!bgfx::isValid(mesh.geom->vbh))
            tryUploadGeometry(*mesh.geom, data);
        mesh.geom->lastUsed = frame;
        return &mesh;
    }

    /// Color stream fallback of meshes without baked per-vertex colors:
    /// one shared all-white buffer, regrown to the largest vertex count
    /// seen (attribute fetches are bounded by the draw's indices).
    bgfx::VertexBufferHandle whiteColors(int numVertices);

    /// Bind the two mesh vertex streams: the shared colorless geometry
    /// and the per-cache color stream (white fallback). Only for
    /// programs whose vertex stage reads a_color0 (mesh/flat families);
    /// depth-only programs bind gpu->geom->vbh alone.
    void setMeshVertexBuffers(GpuMesh *gpu, const Render::MeshData &mesh);
    /// Stream 2 (the mesh's texture coordinates) under the identity
    /// texture matrix, for a program paired with vs_fc_mesh_tex outside
    /// the texture path: a generated material's mesh or glass splice.
    void bindMeshTexCoord(GpuMesh *gpu, const Render::MeshData &mesh);

    /// The array texture of a per-face palette, uploaded on demand.
    /// Null when this backend cannot do array textures at all, which
    /// leaves the draw untextured per face rather than mis-sampled.
    GpuTextureArray *getTextureArray(
        const Render::TexturePalette &palette,
        int maxLayers = Render::MaxFaceTexturePalette,
        int maxSide = GpuTextureArray::MaxSide)
    {
        if (palette.entries.empty()
                || !(bgfx::getCaps()->supported
                     & BGFX_CAPS_TEXTURE_2D_ARRAY))
            return nullptr;
        // Content key: the ids of the layers in order, and the shape
        // asked for -- a per-face palette and a material stacking the
        // same images want arrays of different sizes, and one key for
        // both would hand the second caller the first one's upload.
        uint64_t key = 1469598103934665603ull;
        auto mix = [&key](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        for (const auto &e : palette.entries)
            mix(e ? e->textureId : 0);
        mix(uint64_t(maxLayers));
        mix(uint64_t(maxSide));
        GpuTextureArray &tex = textureArrays[key];
        tex.lastUsed = frame;
        // A placeholder is rebuilt when a layer it waited on has
        // arrived -- and only then: a file that never decodes never
        // wakes it, and rebuilding every layer of a 2k array once a
        // frame on the chance would be a worse answer than one map
        // staying white.
        if (tex.placeholder && tex.arrived(palette))
            tex.destroy();
        if (!bgfx::isValid(tex.handle))
            tex.upload(palette, maxLayers, maxSide);
        return bgfx::isValid(tex.handle) ? &tex : nullptr;
    }

    /// Stack the images a MaterialX material names into one array
    /// texture and bind it, for the draw about to be submitted
    /// (docs/CyclesIntegration.md sec 6.12). A layer whose image did
    /// not load uploads white: the document is still drawn, with that
    /// one map missing, which is what the generator already warned
    /// about.
    void pushUserImages(const Render::UserShader &shader);

    GpuTexture *getTexture(const Render::TextureImage &data)
    {
        GpuTexture &tex = textures[data.textureId];
        tex.lastUsed = frame;
        // A placeholder is re-examined every frame: the pixels it
        // stands in for are in flight, and the id they will arrive
        // under is this one, so nothing else would ever replace it.
        if (tex.placeholder)
            tex.destroy();
        if (!bgfx::isValid(tex.handle))
            tex.upload(data);
        return &tex;
    }

    /// docs/FarFieldProxies.md §10.1 measurement 1: how much of what
    /// this frame drew could not have reached the screen.
    ///
    /// Re-rasterizes a batch of world-space boxes against the finished
    /// depth buffer under one occlusion query each, writing neither
    /// colour nor depth. A box whose query returns no pixels is behind
    /// the scene everywhere it projects, so nothing inside it can have
    /// contributed one either — which is the conservative direction:
    /// the boxes bound the geometry loosely, so this *under*-reports
    /// how much is hidden and any number it produces is a floor.
    ///
    /// Boxes rather than the geometry itself because that is the unit a
    /// culling scheme would actually test (§10.1's CHC++ shape over the
    /// spatial index): re-submitting the real triangles would measure a
    /// mechanism nobody would build, and would cost a second geometry
    /// pass to do it.
    ///
    /// \a queries must hold one handle per box. Returns the number
    /// submitted, which is fewer than asked for if the transient
    /// buffers are exhausted — reported by the caller rather than
    /// silently shortening the batch.
    uint32_t submitOcclusionBoxes(
            const float *boxMin, const float *boxMax, uint32_t count,
            const std::vector<bgfx::OcclusionQueryHandle> &queries)
    {
        if (!count)
            return 0;
        const uint32_t verts = count * 8;
        const uint32_t indices = count * 36;
        if (bgfx::getAvailTransientVertexBuffer(verts, SceneVertex::ms_layout)
                    < verts
                || bgfx::getAvailTransientIndexBuffer(indices) < indices)
            return 0;
        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, verts, SceneVertex::ms_layout);
        bgfx::allocTransientIndexBuffer(&tib, indices);
        auto *v = reinterpret_cast<SceneVertex *>(tvb.data);
        auto *idx = reinterpret_cast<uint16_t *>(tib.data);
        // Corner order is the bit-encoded one Render::occlusionBoxIndices
        // is written against — and the index list comes from there, not
        // from here, because the version written out at this call site
        // closed only 3.5 of the box's 6 faces and answered occlusion
        // questions with the box's own interior (§12.6). Normals are
        // never read (nothing is written) but the layout carries them,
        // so they are filled rather than left as whatever the buffer
        // held.
        const uint16_t *kBoxIndices = Render::occlusionBoxIndices();
        for (uint32_t b = 0; b < count; ++b) {
            const float *lo = boxMin + b * 3;
            const float *hi = boxMax + b * 3;
            // Already conservative when they arrive: the caller pads
            // them, because the same padded box has to be the one the
            // near-plane exemption is judged on.
            SceneVertex *c = v + b * 8;
            for (int i = 0; i < 8; ++i) {
                c[i].px = (i & 1) ? hi[0] : lo[0];
                c[i].py = (i & 2) ? hi[1] : lo[1];
                c[i].pz = (i & 4) ? hi[2] : lo[2];
                c[i].nx = 0.0f;
                c[i].ny = 0.0f;
                c[i].nz = 1.0f;
            }
            // Relative to the box's own eight vertices, NOT absolute
            // into the shared buffer: each draw below binds the stream
            // at startVertex = b*8, and bgfx offsets the indices by
            // that itself. Written absolutely, every box after the
            // first in a batch reads vertices belonging to a later box
            // — geometry that is somewhere, so it rasterizes and the
            // query answers, which is why the failure showed up as an
            // implausible verdict rather than as nothing drawn.
            uint16_t *bi = idx + b * 36;
            for (int i = 0; i < 36; ++i)
                bi[i] = kBoxIndices[i];
        }

        float identity[16];
        bx::mtxIdentity(identity);
        // ⚠️⚠️ The flat program, and u_params zeroed for every box. Both
        // matter, and either one wrong silently deletes geometry.
        //
        // A bgfx uniform keeps whatever the last draw that set it left
        // in it, and vs_fc_mesh — which these boxes used to be drawn
        // with — reads u_params.w as an NDC depth bias. The last scene
        // draw before this view is routinely a *line* draw, whose
        // u_params.w is not a bias at all but the on-top dim alpha,
        // normally 1.0. Inherited, it pushes every test box a whole NDC
        // unit away from the viewer — past the far plane — so the box
        // rasterizes nothing, the query counts no samples, and the node
        // reports itself hidden however plainly it is in view. That is
        // what deleted visible geometry in §12.5, and it is why the
        // synthetic wall smoke scene never reproduced it: triangles
        // only, no line draw, so the inherited bias there was 0.
        //
        // vs_fc_flat transforms the position and does nothing else, so
        // the box lands where the box is; the explicit zero keeps that
        // true if either shader body ever grows a term of its own. Three
        // other mesh-program pairings in this file zero u_params for the
        // same reason — see the water surface and AO normal passes.
        const float zeroParams[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (uint32_t b = 0; b < count; ++b) {
            bgfx::setTransform(identity);
            bgfx::setUniform(u_params, zeroParams);
            bgfx::setVertexBuffer(0, &tvb, b * 8, 8);
            bgfx::setIndexBuffer(&tib, b * 36, 36);
            // LEQUAL, not LESS: a box face coplanar with the surface it
            // bounds is exactly the common case (a part flush against
            // the cell it sits in), and LESS would call it hidden.
            // Both faces draw — the camera can be inside a node's box,
            // and a back-face-culled box would then vanish and read as
            // fully occluded.
            bgfx::setState(BGFX_STATE_DEPTH_TEST_LEQUAL);
            bgfx::submit(vid(ViewOcclusionProbe), m_progFlat, queries[b]);
            ++drawcount;
        }
        return count;
    }

    /// One sweep's memory of which uploads it has already charged
    /// somebody for. GPU bytes are shared two ways -- several cache ids
    /// (colour variants of one TShape) point at a single GpuGeometry,
    /// and a cache id itself is content-addressed, so two sources whose
    /// meshes match exactly are one upload behind two tags. Charging
    /// every referent its full bytes would promise the same memory
    /// twice and report a deficit covered that is not.
    struct UploadCharge {
        std::set<uint64_t> cacheIds;
        std::set<const void *> geoms;
    };

    /// GPU bytes \a data has uploaded right now: its own per-cache
    /// streams (baked colours, the line/point instance data) plus the
    /// colourless geometry behind them, each charged at most once per
    /// \a charge.
    ///
    /// This is the GPU budget's currency and it is NOT
    /// meshResidentBytes, the CPU arrays of the same mesh. A line
    /// segment is 8 bytes of index there; here it is those 8 bytes AND
    /// a 64-byte quad-expansion instance record (two endpoints and
    /// their colours), so a line-heavy source costs the GPU some nine
    /// times what it costs the heap. Pricing a downgrade in the wrong
    /// currency is how a sweep frees "enough" and stays over budget.
    ///
    /// 0 for a mesh nothing has uploaded, which is the honest answer:
    /// downgrading it gives the GPU nothing back.
    uint64_t uploadedBytesOf(const Render::MeshData &data,
                             UploadCharge *charge)
    {
        auto it = meshes.find(data.cacheId);
        if (it == meshes.end())
            return 0;
        uint64_t bytes = 0;
        if (!charge || charge->cacheIds.insert(data.cacheId).second)
            bytes += it->second.bytes;
        if (const GpuGeometry *geom = it->second.geom) {
            if (!charge || charge->geoms.insert(geom).second)
                bytes += geom->bytes;
        }
        return bytes;
    }

    /// What the GPU accounting is holding, split by whether the scene
    /// still wants it.
    ///
    /// The single total is what a budget was judged against, and it
    /// cannot fall at the moment a descent succeeds: the rungs it
    /// replaced stay uploaded until collectMeshes retires them two
    /// frames later. So a plan that had just given up 2394 sources read
    /// its own memory as having gone UP, and no descent however good
    /// could honour anything. `live` is the same accounting asked the
    /// question the budget means -- what the frames now being drawn
    /// reference -- and it falls the moment the scene stops naming the
    /// old uploads.
    struct GpuBytes {
        uint64_t total = 0;   ///< every buffer currently allocated
        uint64_t live = 0;    ///< referenced recently enough to survive
                              ///< the next collectMeshes
        uint64_t stale = 0;   ///< total - live: awaiting collection
        uint32_t entries = 0;
        uint32_t staleEntries = 0;
    };
    /// The allocator's books split by drawable class and recency --
    /// what stands uploaded for triangles vs lines vs points, and how
    /// much of each no recent frame has drawn. Face, line and point
    /// drawables are separate caches, so an entry classifies by which
    /// streams its geometry carries; a shared geometry is charged to
    /// the first entry that names it. This exists because a 74MB gap
    /// between uploaded and live had three candidate owners and no
    /// meter that could name one.
    struct ClassBytes {
        uint64_t bytes = 0;      ///< everything this class holds
        uint64_t undrawn = 0;    ///< ...of which no recent frame drew
        uint32_t entries = 0;
        uint32_t undrawnEntries = 0;
    };
    struct GpuBytesByClass {
        ClassBytes tri, line, point, other;
    };
    GpuBytesByClass gpuBytesByClass() const
    {
        GpuBytesByClass out;
        std::set<const GpuGeometry *> charged;
        for (const auto &m : meshes) {
            const GpuGeometry *g = m.second.geom;
            uint64_t bytes = m.second.bytes;
            if (g && charged.insert(g).second)
                bytes += g->bytes;
            ClassBytes &cls =
                (g && bgfx::isValid(g->tri))         ? out.tri
                : (g && bgfx::isValid(g->line))
                      || bgfx::isValid(m.second.lineInst)  ? out.line
                : (g && bgfx::isValid(g->point))
                      || bgfx::isValid(m.second.pointInst) ? out.point
                                                           : out.other;
            cls.bytes += bytes;
            ++cls.entries;
            if (m.second.lastUsed + 2 < frame) {
                cls.undrawn += bytes;
                ++cls.undrawnEntries;
            }
        }
        // Geometries no mesh references are in flight to collection;
        // they have no class anymore and land in `other`.
        for (const auto &g : geometries) {
            if (charged.count(&g.second))
                continue;
            out.other.bytes += g.second.bytes;
            ++out.other.entries;
            if (g.second.lastUsed + 2 < frame) {
                out.other.undrawn += g.second.bytes;
                ++out.other.undrawnEntries;
            }
        }
        return out;
    }

    GpuBytes gpuBytes() const
    {
        GpuBytes out;
        // `live` is a recency CENSUS (touched within the two-frame
        // in-flight window) kept for the readout; retention itself is
        // publication-keyed (collectMeshes), so `stale` now reads as
        // "resident but not recently drawn" -- kept rungs of culled or
        // intermittently drawn objects -- not as garbage awaiting
        // collection. The budget is judged on `total`, the
        // allocator's books.
        auto tally = [&out, this](uint64_t bytes, uint64_t lastUsed) {
            out.total += bytes;
            ++out.entries;
            if (lastUsed + 2 >= frame)
                out.live += bytes;
            else {
                out.stale += bytes;
                ++out.staleEntries;
            }
        };
        for (const auto &m : meshes)
            tally(m.second.bytes, m.second.lastUsed);
        for (const auto &g : geometries)
            tally(g.second.bytes, g.second.lastUsed);
        return out;
    }

    // Retire GPU buffers by PUBLICATION, not by recency (sec 13c.5).
    //
    // The old rule -- collect whatever no draw touched for two frames
    // -- was a guess about need, and it guessed wrong twice: it
    // destroyed buffers of draws the scene still rendered on a
    // longer-than-two-frame cadence (198 draws re-uploaded every 4th
    // frame, ~75MB of standing churn, and a live meter flapping
    // 45<->120MB on a quiet scene), and it DELAYED a downgrade's free
    // past the next plan, which is the transient the whole storm
    // chased. \a kept is the truth instead: what the published lists
    // carry, at which generation.
    //
    //  - kept, generation current: never collected. Its bytes are the
    //    scene's bytes; only a publish or a swap may free them.
    //  - kept, generation moved: the rung was swapped -- destroy NOW,
    //    drawn or not. This is what makes a free a synchronous event
    //    on the allocator's books (a culled object's old rung would
    //    otherwise never meet the draw-time generation check and leak).
    //  - not kept: left the lists at a publish; the two-frame grace
    //    only spans in-flight frames.
    //  - kept but gated (\a gatedOnly): published, current, and NOT
    //    SUBMITTABLE -- every draw naming it is suppressed by the
    //    element gates (sec 13b). Falls back to the recency grace,
    //    which a gated mesh always fails, so its buffers retire two
    //    frames after the gate closes and come back through an
    //    ordinary on-demand upload when it lifts. This is 13b's
    //    "suppressing the draw is all it takes to free the memory",
    //    which publication-keyed retention had silently repealed:
    //    74.5MB of gated edge buffers stood resident with the gate
    //    firing on all 5909 of their draws every frame.

    // Drop GPU buffers of caches/textures that no draw call referenced
    // recently.
    void collectMeshes(const std::unordered_map<uint64_t, uint64_t> &kept,
                       const std::unordered_set<uint64_t> &gatedOnly);

    // Fullscreen gradient behind the scene, replicating
    // SoFCBackgroundGradient::GLRender vertex for vertex in clip space
    // (the background view has identity view/proj, so transparent scene
    // geometry blends against the real background colors). Depth is
    // neither tested nor written: the buffer keeps the far-plane clear
    // value, matching the GL path where the gradient sits at the far
    // plane.
    /// Visible sun disc + limb glow along the directional scene light,
    /// drawn additively over the background (its view precedes all
    /// geometry, which then overdraws it). u_proj on the view
    /// reconstructs the pixel direction; the shader outputs nothing for
    /// orthographic cameras.
    void submitSunDisc(float sizeDeg);

    /// PBR environment background (PBRConfig::envBackground): the IBL
    /// cubemap drawn as the visible background instead of the gradient
    /// quad. The background view keeps the scene view/proj for this
    /// (the fullscreen vertex shader ignores them; the fragment shader
    /// reconstructs each pixel's world direction from the predefined
    /// u_proj/u_invView).
    ///
    /// How far out of focus it is drawn is Render_PBREnvBlur, read as
    /// a lens aperture (Render::envBlurAngle) and convolved in by
    /// spreading taps over the cone it subtends: zero is the map as
    /// baked, which is the same backdrop the path tracer shows.
    /// m_envBgTex's mip chain is still what the taps read, but it now
    /// sizes each tap's footprint to the spacing between taps rather
    /// than standing in for the blur itself -- which is what a mip was
    /// bad at, and why a wide setting used to be pixelated.
    void submitEnvBackground();

    void submitBackground(const Render::Background &bg);

    /// The constant `units * r` half of glPolygonOffset(factor, units),
    /// as an NDC depth bias (u_params.w). Positive pushes away from the
    /// viewer. The `factor * m` slope half is per-vertex and lives in
    /// fc_mesh_vs.sh, fed by setPolygonOffsetUniform().
    /// GL's polygon-offset `factor` for this material, in pixels of
    /// depth slope to clear. Raised to the reach of whatever decoration
    /// is drawn over the fill -- a thick line's quad carries the edge's
    /// depth half its width out to each side, so one pixel does not
    /// cover it. See the definition.
    static float polygonOffsetFactor(const Render::Material &mat,
                                     float decorReach);

    /// How far the decoration drawn over this object's fills reaches
    /// from its own geometry, in pixels. 1 when the object has none.
    float decorReachFor(uint64_t objectKey) const;

    /// objectKey -> that reach, rebuilt every frame from the draw list
    /// (BGFXRenderer::Private::render). A fill's own material cannot
    /// answer this: SoDrawStyle's line width lives inside the wireframe
    /// separator, which ViewProviderExt adds after the faces, so the
    /// fill always sees linewidth 1.
    std::unordered_map<uint64_t, float> decorReach;

    static float polygonOffsetBias(const Render::Material &mat);

    /// Ceiling on the depth gradient the vertex stage's slope term
    /// tracks, in NDC depth per NDC screen unit: 1 would be a surface
    /// crossing the entire depth range within one screen width, and the
    /// true gradient runs to infinity as a face turns edge-on.
    ///
    /// It is NOT the case that this only engages on slivers. Measured
    /// (scripts/fill_pullback_slope.py, tilting a plate under a fixed
    /// camera): the offset saturates 17 degrees off edge-on, on a face
    /// still covering 29% of its face-on area and 96 pixels of screen.
    /// How far off depends on the camera's own ry/rz, so there is no
    /// angle that is safe in general -- what the ceiling buys is a
    /// BOUND, `factor * 4 * 2 / height` NDC, not a promise that nothing
    /// visible reaches it. See BGFXView::polygonOffsetFactor for what
    /// that bound costs a fill's neighbours.
    static constexpr float kPolyOffsetMaxSlope = 4.0f;

    /// Support radius of the line distance field, in pixels, and the
    /// offset its alpha channel is stored against (alpha = radius - sd,
    /// so an untouched texel reads as "sd = radius", i.e. no line). Must
    /// match FC_LINE_SDF_RADIUS in the shaders. 32 is comfortably past
    /// what any sane line width needs after a lens has stretched it; the
    /// quads themselves are expanded only as far as their own width
    /// requires, so this costs no fill.
    static constexpr float kLineSdfRadius = 32.0f;

    /// Alpha the lines behind glass keep. The material's own
    /// hiddenlinealpha is about a DIFFERENT question (an on-top line
    /// occluded by the scene) and is 1 for ordinary scene edges, which
    /// would make this pass a no-op; glass wants its own answer, and
    /// this is the value GL's hidden-line style dims to.
    static constexpr float kGlassLineAlpha = 0.4f;

    /// The largest NDC depth bias the slope term can produce for this
    /// material at the current viewport size — what the stencil
    /// outline has to clear to stay behind the fill that owns it.
    float polygonOffsetMaxBias(const Render::Material &mat,
                               uint64_t objectKey = 0) const;

    /// Bind u_polyOffset for one draw: the slope factor and its
    /// ceiling. Call at every site submitting a vs_fc_mesh program;
    /// pass null (or a non-triangle material) to disable the term.
    void setPolygonOffsetUniform(const Render::Material *mat,
                                 uint64_t objectKey = 0);
    /// Set u_ambient for a draw: the ambient term Coin would give it,
    /// which is the material's own ambient colour times the
    /// traversal's global ambient. Falls back to the legacy flat floor
    /// when the feed carries no Coin lighting.
    /// Tell the vertex stages whether the authored colour streams
    /// they carry need decoding (fc_color.sh).
    void setColorSpaceUniform();
    void setAmbientUniform(const Render::Material &mat);

    /// Appearance/placement of one stencil outline.
    struct OutlineSpec {
        uint16_t view = ViewHighlight;  ///< target bgfx view
        uint32_t color = 0x000000ff;    ///< packed RGBA, forced opaque
        float width = 1.0f;             ///< edge/cap width in pixels
        /// Corner cap size in pixels; 0 = same as width. The whole-scene
        /// silhouette pass uses the unscaled outline width for its caps
        /// where the edges are 1.5x (GL: renderSceneOutline ~1544).
        float capWidth = 0.0f;
        /// LEQUAL depth test, like GL's renderOutline for scene
        /// (hidden-line) entries; false = draw on top (highlight).
        bool depthTest = false;
        /// Depth writes of the edge/cap passes (the stencil mark pass
        /// never writes depth — it must not depth-kill farther outlines
        /// or the transparent fills that dim them). Writing the edge
        /// depth stands in for GL's back-to-front entry order: a
        /// *farther* transparent fill drawn later depth-fails on a
        /// nearer outline instead of dimming it, while nearer fills
        /// still blend over hidden outlines.
        bool depthWrite = false;
        bool caps = true;               ///< corner point caps
        int start = 0;                  ///< triangle index range;
        int count = 0;                  ///< count 0 = the whole buffer
    };

    // Stencil outline of (part of) a triangle draw, porting the GL
    // renderer's renderOutline: mark the pixels in the stencil buffer,
    // then redraw the triangle edges as thick screen-space lines (GL
    // uses glPolygonMode(GL_LINE), which modern APIs lack) plus
    // point-sprite corner caps where the stencil does not match — only
    // the boundary survives. Each outline uses its own stencil
    // reference so no per-part stencil clear is needed (refs wrap at
    // 255; collisions between outlines that far apart are accepted).
    // Serves both the selection/preselection face outline and the
    // hidden-line draw style's per-object/per-part outlines; clipped
    // materials clip all three passes like the GL state machine does.
    void submitOutline(const Render::DrawCall &draw, uint32_t refCounter,
                       const OutlineSpec &spec);

    /// Tessellation draw style: the triangle edges as geometry, over a
    /// background-coloured fill that occludes what is behind. Stands in
    /// for glPolygonMode(GL_LINE) plus SoRenderManager::HIDDEN_LINE,
    /// neither of which a modern API has. The fill belongs to the
    /// display MODE (Material::drawstyleoverride); a lone SoDrawStyle
    /// node asking for a wireframe gets the edges alone, as it does
    /// from Coin.
    void submitTessellation(const Render::DrawCall &draw,
                            const float *viewMatrix, uint16_t viewId);
    /// The Points draw style analogue: a filled-triangle draw carrying
    /// SoDrawStyleElement::POINTS renders as its corner points
    /// (BGFXViewOverlay.cpp).
    void submitVertexPoints(const Render::DrawCall &draw,
                            const float *viewMatrix, uint16_t viewId);

    /// Does this draw need a discard-clipping shader variant: its own
    /// section planes, or the mirror pass's water/ground plane.
    bool clipActiveFor(const Render::Material &mat) const;

    /// The mirror pass clips the world to the half-space above its
    /// plane. Without it every surface BELOW the plane — the ground the
    /// basin stands on, the pool floor, the submerged half of the rim —
    /// is mirrored upward and lands in front of the camera, so the
    /// reflection target fills with the underside of the scene and the
    /// water reflects a slab instead of the sky and whatever stands in
    /// it. Concave (union-mode) section clipping is left alone: the
    /// shader has one mode for all planes, and intersecting a plane
    /// with a union needs a second one.
    bool reflClipActive(const Render::Material &mat) const;

    void setClipUniforms(const Render::Material &mat);

    /// Stencil-mark pass of an outline: rasterize (part of) the triangle
    /// draw into the stencil buffer with the given reference, no color
    /// output. Scene outlines keep the depth test (matching GL's
    /// color-masked fill pass; the mark still replaces on depth fail);
    /// highlight outlines draw on top. The whole-scene silhouette marks
    /// every scene draw under one shared reference before its edge
    /// passes. Never writes depth — it must not depth-kill farther
    /// outlines or the transparent fills that dim them.
    bool submitOutlineMark(const Render::DrawCall &draw,
                           uint32_t refCounter, uint16_t view,
                           bool depthTest, int start = 0, int count = 0);

    /// Edge and corner-cap passes of an outline: redraw the triangle
    /// edges as instanced thick lines (plus point-sprite caps) where the
    /// stencil does not match the reference — only the boundary
    /// survives.
    void submitOutlineEdges(const Render::DrawCall &draw,
                            uint32_t refCounter, const OutlineSpec &spec);

    // Upload the section hatch texture when the CPU-side pixels changed
    // (version 0 = no hatch image).
    void updateHatchTexture(uint64_t version, const uint8_t *rgba,
                            int width, int height);

    /// Stencil parity mark of one section-cap pass (GL: _renderSection's
    /// color-masked renderSolids loop): rasterize the solid triangle
    /// ranges clipped by the single active section plane with a
    /// depth-independent stencil INVERT — pixels looking through the
    /// cut opening end up with an odd (non-zero) parity. Assumes the
    /// stencil is zero where the draw rasterizes (the cap view is
    /// stencil-cleared and every cap pass cleans up after itself).
    bool submitCapMark(const Render::DrawCall &draw,
                       const float plane[4], uint16_t view);

    /// Cap fill of one section-cap pass: a world-space quad in the
    /// section plane, drawn where the stencil parity is odd — the cross
    /// section of the marked solids — with depth LESS + write like GL's
    /// cap (LESS so the already-drawn fill keeps the shared rim pixels
    /// that GL's cap-before-fill order gives to the fill). Clipped by
    /// the remaining planes when there are any (never in concave mode,
    /// matching the GL clip state there).
    void submitCapQuad(const CapVertex verts[4], uint32_t color,
                       const float (*otherPlanes)[4], int numOther,
                       bool hatch, bool blend, uint16_t view);

    /// Stencil cleanup of one section-cap pass (the stand-in for GL's
    /// per-pass glClear(GL_STENCIL_BUFFER_BIT)): zero the stencil over
    /// the cap quad, which covers every pixel the parity mark can have
    /// touched (the cut cross section lies inside the plane/circumsphere
    /// intersection), unclipped so marks outside the other planes are
    /// cleaned too. No color or depth output.
    /// The cap quad again, into the depth+normal prepass target, so the
    /// screen-space passes that read it see the cut surface.
    void submitCapPrepass(const CapVertex verts[4],
                          const float (*otherPlanes)[4], int numOther,
                          uint16_t view);

    void submitCapCleanup(const CapVertex verts[4], uint16_t view);

    // Fullscreen WBOIT resolve: average the accumulated premultiplied
    // color and blend it onto the scene by coverage (1 - revealage in
    // the source alpha, blend INV_SRC_ALPHA / SRC_ALPHA).
    void submitComposite();

    /// Render debugging buffer visualization (docs/RenderDebug.md):
    /// overwrite the scene color with an intermediate target — prepass
    /// depth/normal (modes 1/2), the AO term (3), the shadow term
    /// re-evaluated from the prepass position (4), the shadow tile
    /// coverage (5), the overdraw/UV re-render (6/8), or the shadow
    /// filtering-precision probe (7). Runs before the
    /// on-top/highlight/overlay passes so those still draw on top.
    void submitDebug(const Render::RenderDebugConfig &conf, float maxDepth,
                     int aoMethod, bool shadowValid, float impactLife);

    /// SSAO depth+normal prepass of one opaque scene triangle draw:
    /// re-rasterize it into the non-MSAA prepass target with the
    /// encoding fragment shader, replicating the fill's transform, clip
    /// planes and culling so the AO sees exactly the visible geometry.
    // The shadow ground plane: a shadow receiving quad at the bottom of
    // the scene bounds (the Coin-side ground lives outside the captured
    // graph, so the backend draws its own). Lit by the scene light like
    // any receiver, classic shading regardless of the PBR mode.
    void submitShadowGround(const float bmin[3], const float bmax[3],
                            const Render::LightConfig &light,
                            const Render::GroundCamera &cam,
                            bool prepass = false);

    /// The shadow-only ground WITHOUT a quad: a fullscreen pass that
    /// intersects the ground plane per pixel, samples the shadow map
    /// there and multiplies the frame down by it.
    ///
    /// The mode wants an infinite receiver and nothing else -- no
    /// surface, no texture, no depth of its own -- which is exactly
    /// what a quad is bad at: it needs sizing, it ends somewhere, and
    /// the sizing is what tied the ground to the scene bounds. Every
    /// reason to rasterize one is switched off here, so this path
    /// drops it. Returns false when it cannot run (no program), so the
    /// caller can fall back to the quad.
    bool submitShadowGroundPlane(const float bmin[3], const float bmax[3],
                                 const Render::LightConfig &light,
                                 const Render::GroundCamera &cam);

    // Rasterize a shadow casting triangle draw into the variance shadow
    // map under the light camera (the ViewShadow transform). Both faces
    // cast; section-clipped parts do not (clip shader variant).
    void submitShadowCaster(const Render::DrawCall &draw);

    /// Render a caster into one bulb shadow atlas tile (plain VSM
    /// moments — the tiles skip the smooth-border blur).
    void submitBulbShadowCaster(const Render::DrawCall &draw, int tile);

    /// Render a glass caster's light transmittance into the shadow
    /// tint map (multiplicative onto the white-cleared target; front
    /// faces only so one glass body multiplies once, no depth).
    void submitShadowTint(const Render::DrawCall &draw);

    /// Separable gaussian blur of the shadow moments (the Shadow draw
    /// style's SmoothBorder, 0..100): horizontal into the ping texture,
    /// vertical back into shadowTex, so the mesh receivers and the
    /// volumetric raymarch keep sampling the same target. The blur views
    /// sit right after the caster pass. Softens the VSM penumbra and
    /// curbs shimmer on razor-straight CAD edges.
    /// (Re)create the shadow map targets for the requested size
    /// (ShadowPrecision); the stored moments are lost, so the cached
    /// map re-renders. The EffectShadow group's builder -- go through
    /// updateEffect(), which owns when the set exists at all and keeps
    /// a failed allocation from being retried every frame.
    void ensureShadowTargets(uint16_t size);

    void submitShadowBlur(float smoothBorder);

    void submitPrepass(const Render::DrawCall &draw);

    /// Debug scene re-render target (docs/RenderDebug.md modes 6/8),
    /// created on first use at viewport size; every reset/resize path
    /// destroys it with the other offscreen targets.
    bool ensureDebugScene();

    /// Whether this backend can hand the id image back to the CPU at
    /// all. WebGL2 cannot, which is why the audit is a desktop
    /// instrument that informs the browser tier rather than one that
    /// runs there.
    static bool idReadbackSupported()
    {
        const bgfx::Caps *caps = bgfx::getCaps();
        return caps
            && (caps->supported & BGFX_CAPS_TEXTURE_BLIT)
            && (caps->supported & BGFX_CAPS_TEXTURE_READ_BACK);
    }

    /// CPU-readable mirror of the id image, created on first use at the
    /// debug target's size. A render target cannot be read back
    /// directly on every backend; blitting into a plain READ_BACK
    /// texture is the portable arrangement.
    bool ensureIdReadback()
    {
        if (!idReadbackSupported() || !bgfx::isValid(debugSceneTex))
            return false;
        if (bgfx::isValid(idReadTex) && idReadW == debugSceneW
                && idReadH == debugSceneH)
            return true;
        if (bgfx::isValid(idReadTex)) {
            bgfx::destroy(idReadTex);
            idReadTex = BGFX_INVALID_HANDLE;
        }
        idReadTex = bgfx::createTexture2D(debugSceneW, debugSceneH, false, 1,
            bgfx::TextureFormat::RGBA16F,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        idReadW = debugSceneW;
        idReadH = debugSceneH;
        return bgfx::isValid(idReadTex);
    }

    /// Copy this frame's id image and ask for it back. Returns the frame
    /// number at which \a dst is filled -- the caller must keep \a dst
    /// alive until bgfx has reached it, since the render thread writes
    /// into it long after this returns. 0 = the copy could not be made.
    uint32_t readbackId(void *dst)
    {
        if (!bgfx::isValid(idReadTex) || !bgfx::isValid(debugSceneTex))
            return 0;
        bgfx::blit(vid(ViewIdReadback), idReadTex, 0, 0, debugSceneTex);
        return bgfx::readTexture(idReadTex, dst);
    }

    /// Targets for a portable frame capture, created on first use at
    /// viewport size.
    ///
    /// Two staging textures, because a render target cannot be read
    /// back directly on any backend, and one colour target to carry the
    /// depth: bgfx blits and reads back colour textures everywhere and
    /// depth textures nowhere. What used to be two glReadPixels calls
    /// against the scene framebuffer is this, and it is the difference
    /// between a capture that only exists on OpenGL and one the golden
    /// render tests can gate Metal and Vulkan with.
    bool ensureCaptureTargets();

    /// Copy this frame's finished colour and its encoded depth and ask
    /// for both back. Returns the frame number at which \a color and
    /// \a depth are filled -- the caller must keep both alive until
    /// bgfx has reached it, exactly as readbackId requires. 0 = the
    /// copy could not be made.
    ///
    /// Must be called with every pass of the frame already submitted
    /// and before the frame boundary: the blit rides on ViewCapture,
    /// the last view id, so it copies the finished image.
    uint32_t readbackCapture(void *color, void *depth);

    /// Rasterize one scene triangle draw into the debug scene target
    /// (docs/RenderDebug.md): mode 6 accumulates a fragment count with
    /// the depth test off (additive blend — the overdraw heatmap
    /// source); mode 8 writes the depth-tested texcoords (the UV view).
    /// Transform, clip planes and culling replicate the color fill like
    /// the AO prepass does.
    void submitDebugScene(const Render::DrawCall &draw, int mode);

    /// ⭐ Rasterize one scene draw into the debug scene target as its own
    /// identity (docs/RenderDebug.md §2.3b, view mode 11): a pixel's
    /// value IS the id of the draw that won the depth test there, so the
    /// finished image is an exact answer to "which draws reached the
    /// screen" — the invariant occlusion culling claims, and the only
    /// measurement of that claim which compares a verdict against
    /// geometry instead of comparing two pictures.
    ///
    /// \a drawIdx is the DrawCall row, which is the granularity the cull
    /// mask is indexed at (`ProxyInstance::drawIndex`) — per *instance*,
    /// not per mesh, so a disagreement names a thing that can actually
    /// be masked. Encoded as drawIdx+1 in three raw byte lanes, since
    /// zero has to stay available for "no draw owns this pixel".
    ///
    /// ⭐⭐ Exact integers, never a hash or a palette: two draws sharing a
    /// colour is precisely the failure this instrument exists to detect,
    /// and a mode that made ids pretty would hide it. The target is
    /// RGBA16F, which carries 0..255 per channel exactly.
    ///
    /// Every geometry kind, deliberately. The residual damage of the
    /// culling shows up on edges, so an audit that skipped lines and
    /// points would come back clean while missing exactly the draws that
    /// were wrong. Lines and points therefore go through the same
    /// screen-space quad expansion the beauty pass uses — the same
    /// vertex programs, so the coverage IS the coverage — with
    /// fs_fc_flat's constant-colour path carrying the id (u_params.x = 0
    /// selects u_matColor; a zero emissive leaves it untouched).
    /// The one deliberate divergence is the line feather: a negated
    /// width turns off fs_fc_line's coverage ramp, because this image
    /// is decoded as exact integers and alpha < 0.5 means "unowned".
    /// See the u_params assignment below.
    ///
    /// ⚠️ The coverage and depth decisions below are copied from
    /// submit(); they are the ones that decide which pixels a draw
    /// takes, so a copy that drifts reports pixels the frame never drew.
    /// Anything that changes thick-line/point expansion, culling or
    /// depth there has to change here too.
    void submitId(const Render::DrawCall &draw, int drawIdx, bool noseam)
    {
        const Render::Material &mat = draw.material;
        if (!draw.mesh || draw.mesh->numVertices == 0)
            return;
        GpuMesh *mesh = getMesh(*draw.mesh);
        if (!bgfx::isValid(mesh->geom->vbh))
            return;
        if (noseam && mat.type == Render::Material::Line)
            mesh->ensureNoSeam(*draw.mesh);
        noseam = noseam && mat.type == Render::Material::Line
            && bgfx::isValid(mesh->geom->lineNoSeam);
        bgfx::IndexBufferHandle ibh = BGFX_INVALID_HANDLE;
        switch (mat.type) {
        case Render::Material::Triangle: ibh = mesh->geom->tri; break;
        case Render::Material::Line:
            ibh = noseam ? mesh->geom->lineNoSeam : mesh->geom->line;
            break;
        case Render::Material::Point: ibh = mesh->geom->point; break;
        }
        if (!bgfx::isValid(ibh))
            return;

        uint32_t linepattern = mat.linepattern;
        bool patterned = mat.type == Render::Material::Line
            && (linepattern & 0xffff) != 0xffff;
        bool thickline = mat.type == Render::Material::Line
            && m_instancing
            && bgfx::isValid(noseam ? mesh->lineNoSeamInst
                                    : mesh->lineInst);
        patterned = patterned && thickline;
        bool thickpoint = mat.type == Render::Material::Point
            && mat.pointsize > 1.001f
            && m_instancing && bgfx::isValid(mesh->pointInst);

        bool transparent = mat.transparent
            || (mat.pervertexcolor && draw.mesh->hasTransparency);
        bool twoside = mat.twoside || transparent || mat.ontop;
        bool culling = mat.culling && !transparent;

        // Depth exactly as the beauty pass resolves it — who owns the
        // pixel is the entire answer here. On-top draws keep the depth
        // test off and are submitted in a second round by the caller,
        // so they take the same pixels they take on screen.
        bool depthtest = mat.ontop ? false : mat.depthtest;
        bool depthwrite = (!mat.ontop && transparent) ? false
                                                      : mat.depthwrite;
        if (!depthtest)
            depthwrite = false;

        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        if (depthtest)
            state |= depthFuncState(mat.depthfunc);
        if (depthwrite)
            state |= BGFX_STATE_WRITE_Z;
        if (culling && !twoside && mat.type == Render::Material::Triangle)
            state |= mat.ccw ? BGFX_STATE_CULL_CW : BGFX_STATE_CULL_CCW;
        if (mat.type == Render::Material::Line && !thickline)
            state |= BGFX_STATE_PT_LINES;
        else if (mat.type == Render::Material::Point && !thickpoint)
            state |= BGFX_STATE_PT_POINTS
                | BGFX_STATE_POINT_SIZE(
                    uint32_t(qMax(mat.pointsize, 1.0f)));

        const uint32_t id = uint32_t(drawIdx) + 1u;
        const float idc[4] = {float(id & 0xffu), float((id >> 8) & 0xffu),
                              float((id >> 16) & 0xffu), 1.0f};

        bool clipped = clipActiveFor(mat);
        if (clipped)
            setClipUniforms(mat);
        setDrawTransform(draw, autozoomScale, viewMatrix, projMatrix,
                         (float)height);

        bgfx::ProgramHandle prog;
        if (mat.type == Render::Material::Triangle) {
            // The debug-scene programs: a bare transform and a constant
            // output, with none of the mesh shader's lighting, fog, AO
            // or tone mapping between the id and the target.
            float dbg[4] = {11.0f, idc[0], idc[1], idc[2]};
            bgfx::setUniform(u_debugParams, dbg);
            // ...and its polygon offset, so a fill still resolves
            // against its own biased edges the way it does on screen.
            float params[4] = {0.0f, 0.0f, 0.0f, polygonOffsetBias(mat)};
            bgfx::setUniform(u_params, params);
            prog = clipped ? m_progDebugSceneClip : m_progDebugScene;
        }
        else {
            float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            // u_params for the line/point/flat family: x = 0 picks the
            // constant colour over the vertex stream, y = width in
            // pixels, z = the NDC pull of highlighted lines, w = 1 = no
            // alpha ceiling (the dimming of occluded on-top lines must
            // not touch an id).
            //
            // The line width goes in NEGATED, which is how fs_fc_line
            // is told to skip its analytic coverage (fc_line_vs.sh).
            // The beauty pass feathers the quad and ramps alpha across
            // the outer half-pixel; an id image cannot carry that.
            // reportCullAudit reads alpha < 0.5 as "no draw owns this
            // pixel", so a ramp would orphan every edge fragment and
            // report culling damage the frame never had. The
            // expansion below the ramp is identical, so the pixels
            // this pass claims are still the pixels the beauty pass
            // covers -- it loses only the half-pixel of feather, where
            // the beauty pass is under 50% coverage anyway.
            float params[4] = {
                0.0f,
                mat.type == Render::Material::Line
                    ? -qMax(1.0f, mat.linewidth)
                    : qMax(1.0f, std::floor(mat.pointsize + 0.5f)),
                (mat.highlightline && depthtest)
                    ? -2.0f * (2.0f * 16.0f / 16777216.0f) : 0.0f,
                1.0f};
            bgfx::setUniform(u_matColor, idc);
            bgfx::setUniform(u_matEmissive, zero);
            bgfx::setUniform(u_params, params);
            if (patterned) {
                uint32_t factor = linepattern >> 16;
                factor = factor < 1 ? 1 : factor > 256 ? 256 : factor;
                float pat[4] = {float(linepattern & 0xffff), float(factor),
                                0.0f, 0.0f};
                bgfx::setUniform(u_linePattern, pat);
            }
            prog = thickline
                ? (patterned ? (clipped ? m_progLinePatClip : m_progLinePat)
                             : (clipped ? m_progLineClip : m_progLine))
                : thickpoint
                    ? (clipped ? m_progPointClip : m_progPoint)
                    : (clipped ? m_progFlatClip : m_progFlat);
        }
        if (!bgfx::isValid(prog))
            return;

        if (thickline) {
            uint32_t startSeg = 0;
            uint32_t numSeg = uint32_t(noseam
                ? draw.mesh->numNoSeamLineIndices
                : draw.mesh->numLineIndices) / 2;
            if (draw.indexCount > 0) {
                startSeg = uint32_t(draw.indexStart) / 2;
                numSeg = uint32_t(draw.indexCount) / 2;
            }
            bgfx::setVertexBuffer(0, m_lineQuadVb);
            bgfx::setIndexBuffer(m_lineQuadIb);
            bgfx::setInstanceDataBuffer(
                noseam ? mesh->lineNoSeamInst : mesh->lineInst,
                startSeg, numSeg);
        }
        else if (thickpoint) {
            uint32_t startPt = 0;
            uint32_t numPt = uint32_t(draw.mesh->numPointIndices);
            if (draw.indexCount > 0) {
                startPt = uint32_t(draw.indexStart);
                numPt = uint32_t(draw.indexCount);
            }
            bgfx::setVertexBuffer(0, m_lineQuadVb);
            bgfx::setIndexBuffer(m_lineQuadIb);
            bgfx::setInstanceDataBuffer(mesh->pointInst, startPt, numPt);
        }
        else {
            setMeshVertexBuffers(mesh, *draw.mesh);
            if (draw.indexCount > 0)
                bgfx::setIndexBuffer(ibh, uint32_t(draw.indexStart),
                                     uint32_t(draw.indexCount));
            else
                bgfx::setIndexBuffer(ibh);
        }
        bgfx::setState(state);
        bgfx::submit(vid(ViewDebugScene), prog);
        ++drawcount;
    }

    /// Rasterize a water/glass/cloud body draw into one of its interval
    /// depth targets (the meddepth programs write the linear view depth
    /// in .z and the body's appearance slot in .x): front faces with
    /// the nearest depth = interval entry, back faces with the farthest
    /// = interval exit. Culling is forced by face side whatever the
    /// material's two-sidedness. kind: 0 = water, 1 = glass, 2 =
    /// cloud, 3 = fire.
    void submitWaterDepth(const Render::DrawCall &draw, bool back,
                          int kind = 0, int slot = 0);


    /// Advance every stateful particle emitter in this frame's scene by
    /// whole fixed steps (docs/RenderEngine.md §5.8), then leave each
    /// one's live state bound for the beauty draws that follow. The
    /// simulation is a fragment pass over a ping-pong pair of RGBA32F
    /// targets — no compute shaders, so the browser tier runs the same
    /// program the desktop does.
    ///
    /// Returns true while some emitter still owes steps, which keeps
    /// the viewer scheduling redraws: that is how a frozen frame
    /// reaches its warm-up state (the steps are the same length as the
    /// live ones, so a warmed-up frozen frame and a live frame are the
    /// same simulation, and both are reproducible from the reset).
    bool stepParticles(const Render::DrawCallList &scene,
                       float animTime, bool freeze);

    /// Scatter the impacts this frame's steps reported into the water
    /// impact map (docs/RenderEngine.md §5.8), so the water surface can
    /// raise its rings where particles actually struck it.
    ///
    /// `foot` is the world xy footprint the map covers — the bounds of
    /// the water bodies in the scene — as {xmin, ymin, xmax, ymax}, or
    /// null when there is no water to disturb. The map is framed on a
    /// square covering it, so a cell is square and a ring is round.
    ///
    /// Nothing here clears the map: a record is a standing statement
    /// that something struck this place at this time, and it stops
    /// mattering when the surface ages it past the ring lifetime, not
    /// when the frame that wrote it ends.
    void splatImpacts(float animTime, bool freeze, const float *foot);

    /// One clip-space triangle covering the viewport, submitted to a
    /// fullscreen resolve pass (uniforms/textures are set by the caller).
    void fullscreen(uint16_t pass, bgfx::ProgramHandle prog,
                    uint64_t state, uint32_t blendFactor = 0);

    /// Fullscreen AO resolve chain: occlusion from the prepass into the
    /// R8 target — hemisphere-kernel SSAO (method 0) or XeGTAO-style
    /// horizon-integral GTAO (method 1) — and a 4x4 box blur / GTAO
    /// denoise. The result (aoTex for GTAO, aoBlurTex classic) is not
    /// composited here: the mesh programs sample it at unit 9 and fold
    /// it into their ambient/headlight/IBL terms only (aoMeshTex), so
    /// direct scene/bulb light is not AO-darkened.
    /// temporalIndex advances the GTAO noise per idle-accumulation
    /// sample so its noise averages out instead of being averaged
    /// with itself; 0 for an ordinary frame.
    void submitAOResolve(float radius, float intensity, int method,
                         bool fast, int slices, int steps,
                         int temporalIndex);

    /// Cavity (curvature) shading: one fullscreen multiply of the
    /// finished opaque scene by a curvature term read from the prepass
    /// normals. Unlike the AO term above this one *is* composited here
    /// — it darkens the final color rather than an ambient sub-term, so
    /// it states shape independently of how the surface is lit.
    void submitCavity(float valley, float ridge, float radius);

    /// Volumetric light shaft resolve: raymarch the shadow map through
    /// the media at half resolution (ray ends at the prepass depth; a
    /// water body interval carries its own per-channel extinction and
    /// scattering), then composite onto the opaque scene in the
    /// sequential apply view — first the analytic per-channel
    /// transmittance multiply, then the bilateral-upsampled inscatter
    /// add: dst = inscatter + transmittance * scene.
    void submitVolumetric(float density, float intensity, float maxDist,
                          const float medium[4], bool water,
                          bool surfaceSplit, float accum,
                          const float waterSigma[][4],
                          const float cloudParams[][4],
                          const float fireParams[][4],
                          const float fireParams2[][4],
                          const float fireFrames[][16],
                          const float fountainParams[][4],
                          const float fountainFrames[][16]);

    /// Water caustics splat: additive fullscreen pass over the prepass
    /// surfaces inside the water body interval, in its own view before
    /// the volumetric apply (the extinction multiply then absorbs the
    /// caustic light over the eye-ward underwater path). The light /
    /// shadow / water uniforms match the raymarch; u_volParams.w flags
    /// the water span helper active.
    void submitCaustics(const float causticParams[][4],
                        const float waterSigma[][4]);

    /// Copy the scene color into the sampleable refraction source (its
    /// view sits after the volumetric composite; the framebuffer switch
    /// also resolves a multisampled scene attachment).
    void submitWaterCopy();
    /// Average the finished frame into the accumulation history under
    /// \a blend (1 replaces it outright, 0 leaves it alone -- converged),
    /// then copy the history back over the scene colour.
    void submitTemporalAccum(float blend);

    /// User "post" stage (docs/RenderDebug.md §6): resolve the composited
    /// scene color into the water-refraction copy target (safe to share —
    /// this view runs after every reader of the mid-frame water copy),
    /// then draw the user program fullscreen back over the scene reading
    /// the copy. Its uniforms ride the dynamic name binding shared with
    /// the RenderDebug parameters, and like there the updates must be
    /// recorded with the consuming draw (see submitDebug).
    void submitUserPost(const Render::UserShader &shader,
                        bgfx::ProgramHandle prog);

    /// Bloom (glow) chain: bright-pass downsample of the finished scene
    /// into the quarter-res halo source, the light-source bodies added
    /// on top at their HDR emission color, a separable gaussian, and
    /// the additive composite back onto the scene.
    void submitBloom(float threshold, float intensity, float radius,
                     const std::vector<const Render::DrawCall *> &bulbs,
                     bool prepassCurrent);

    /// Re-render a water body draw as the animated water surface:
    /// screen-space refraction from the scene copy, Fresnel-blended
    /// environment reflection and a sun glint (fs_fc_water). Draws
    /// opaquely with depth write — the refraction replaces the
    /// transparent-bucket blending of the body.
    void submitWaterSurface(const Render::DrawCall &draw,
                            float waveStrength, float waveScale,
                            float time, bool depthReject, int reflMode,
                            bool refraction, bool absorb, float absorption,
                            float inscatter, bool shadow,
                            float shadowWobble,
                            int rippleType, float rippleDensity,
                            float impactStrength, float impactLife,
                            const float (*splash)[4], bool volFront);

    /// Re-render a glass body draw as glass: screen-space refraction of
    /// the scene copy (offset from the IOR-refracted view direction and
    /// the front/back thickness), per-channel Beer-Lambert absorption
    /// tinted by the material diffuse, Fresnel-blended environment
    /// reflection (fs_fc_glass). Draws opaquely with depth write like
    /// the water surface.
    void submitGlassSurface(const Render::DrawCall &draw, bool depthReject);
    /// The two colours of a glass body, linear: what it ABSORBS with
    /// over its thickness (the complement is the Beer-Lambert sigma)
    /// and what it TINTS the transmitted light with once at the
    /// surface. A Render_Glass body absorbs with its authored diffuse,
    /// decoded, and tints with nothing; a MaterialX glass (glassmtlx,
    /// docs/MaterialStorage.md sec 17.21) states a linear colour whose
    /// meaning its depth decides -- absorption when there is one, a
    /// surface tint when there is none. Both passes that colour a
    /// glass body (the surface and its shadow tint) read them here.
    void glassBodyColors(const Render::Material &mat, float absorb[4],
                         float tint[4]) const;

    /// Rasterize one line or point draw into the decoration
    /// distance field.
    /// Fragments in front of the glass are discarded there, so only what
    /// is actually seen through the body ends up in the field.
    void submitLineSdf(const Render::DrawCall &draw, const float *viewMatrix,
                       bool noseam);

    /// Composite the fountain/fire media into the mirrored-scene
    /// reflection texture (premultiplied over): the analytic cylinder
    /// raymarch runs with the mirror-view transforms bound.
    void submitReflMedia(const float cloudParams[][4],
                         const float fireParams[][4],
                         const float fireParams2[][4],
                         const float fireFrames[][16],
                         const float fountainParams[][4],
                         const float fountainFrames[][16]);

    /// Blend the mirrored-scene render onto the shadow ground quad:
    /// the same quad geometry and vertex shader as the ground draw, so
    /// the EQUAL depth test hits exactly the ground pixels still
    /// visible; the reflection texture's alpha (0 = nothing mirrored)
    /// scales the blend with the intensity.
    void submitGroundReflOverlay(const float bmin[3], const float bmax[3],
                                 const Render::LightConfig &light,
                                 const Render::GroundCamera &cam);

    // Submission passes mirroring SoFCRenderer's delayed render loop.
    enum SubmitPass {
        PassNormal = 0,
        PassDepthOnly,   // depth-write-only prepass of on-top fills
        PassLineHidden,  // on-top lines/points, no depth test, dimmed
        PassLineSolid,   // on-top lines/points, depth LEQUAL, full color
    };

    /// Per-frame PBR/shadow uniform + sampler state shared by every
    /// triangle draw (the branches are uniform-selected in fc_mesh_fs.sh,
    /// so every mesh program consumes them).
    /// Bind the samplers and uniforms of a textured triangle draw
    /// (shared by the per-draw and the instanced submit paths).
    void bindTextureStage(const Render::Material &mat, bool bumped,
                          bool mapped);

    void setTriangleFrameState(const Render::Material &mat, int pass,
                               bool mapped, bool aoDraw);

    /// True when the cross-object instanced mesh path can run this frame.
    bool instancingActive() const
    {
        return m_instancing && bgfx::isValid(m_progMeshInst);
    }

    /// Stride of one instance: the model matrix columns (i_data0-3,
    /// DrawCall::model layout) followed by the diffuse color (i_data4).
    static constexpr uint32_t InstanceStride = 20 * sizeof(float);

    /// Submit one instanced draw of `count` placements of the prototype's
    /// mesh/material — the eligibility rules live in the producer
    /// (instancableDraw / buildInstanceGroups): an unclipped, non-on-top,
    /// non-water triangle draw of the normal scene pass. Textured draws
    /// batch too (the texture handles are part of the group key); a
    /// transparent group only batches while WBOIT runs — its blending is
    /// order-independent, unlike the sorted fallback. `data` holds
    /// count * InstanceStride bytes. Returns false when the program or
    /// the transient instance space is missing (or the group is
    /// transparent on a sorted-transparency frame); the caller falls
    /// back to per-draw submits.
    bool submitInstanced(const Render::DrawCall &draw, const float *data,
                         uint32_t count);

    /// Instanced counterpart of submitShadowCaster: one caster submit of
    /// `count` placements of the prototype's mesh. `data` uses the same
    /// {model, diffuse} InstanceStride layout as the color pass (the
    /// diffuse rides along unused). Instancable draws are never
    /// section-clipped, so only the unclipped program exists. Returns
    /// false when the program or transient instance space is missing;
    /// the caller falls back to per-draw caster submits.
    bool submitShadowCasterInstanced(const Render::DrawCall &draw,
                                     const float *data, uint32_t count);

    /// Instanced counterpart of submitPrepass (SSAO/volumetric
    /// depth+normal): same instance-data contract and fallback rules as
    /// submitShadowCasterInstanced.
    bool submitPrepassInstanced(const Render::DrawCall &draw,
                                const float *data, uint32_t count);

    void submit(const Render::DrawCall &draw, const float *viewMatrix,
                int pass = PassNormal, bool noseam = false);

    /// Does this user shader's beauty draw blend? Reads the same
    /// reserved "fc_state" parameter applyUserState below acts on
    /// (0 = Default, 1 = Alpha, 2 = Additive), because that is the only
    /// place App::ShaderProgram::Blend reaches the backend — the draw's
    /// Material carries the geometry's transparency, not the program's.
    static bool userDrawBlends(const Render::UserShader &shader);

    /// Reserved "fc_state" parameter = render-state override of a user
    /// shader's beauty draw (App::ShaderProgram Blend / DepthWrite,
    /// docs/RenderDebug.md §6.2): re-record the state — bgfx keeps the
    /// last setState before submit.
    /// The alpha channel means different things in the two targets a
    /// user draw lands in, so the blend cannot be the same in both.
    ///
    /// In the mirror target alpha is COVERAGE: the pass clears it to 0
    /// meaning "nothing reflected", and the water surface keeps only
    /// what covers. A sprite that blends colour without alpha there is
    /// discarded, and the pool reflects the scene but never the spray.
    ///
    /// In the scene target alpha is what the present composite may
    /// consume — the WASM viewer's does, the desktop GL blit does not.
    /// A sprite that blends alpha there punches its own shape out of
    /// the frame and the viewer resolves it toward black, which is why
    /// the browser drew grey spray while the desktop drew white from
    /// the same state.
    ///
    /// So: write coverage into the mirror, leave the scene's alpha
    /// alone.
    static void applyUserState(const Render::UserShader &shader,
                               uint64_t state, uint32_t blendRt,
                               bool coverage);

    static uint64_t depthFuncState(uint8_t func);

    /// The present pass: a fullscreen draw of the scene color through
    /// the output colour transform (fs_fc_present).
    ///
    /// Standalone, it is what puts the frame on the default backbuffer
    /// at all (ViewPresent targets the invalid framebuffer), so it runs
    /// every frame. On the desktop the Qt GL blit does that instead, so
    /// this runs only when a transform is selected, drawing into
    /// presentFbo for the blit to take its source from -- and targeting
    /// a framebuffer at all still forces the MSAA resolve the empty
    /// ViewPresent used to force on its own.
    void present();
#ifndef FC_RENDERER_STANDALONE
    /// Write \a color (tightly packed RGBA8, TOP-DOWN rows) to \a
    /// path: raw PPM for a .ppm extension, else through Qt's image
    /// writers (PNG etc.).
    ///
    /// Top-down because the caller now decides the orientation: the
    /// capture readback flips by bgfx::getCaps()->originBottomLeft,
    /// which is the portable answer. This used to flip unconditionally
    /// on the grounds that "glReadPixels rows are bottom-up" -- true of
    /// OpenGL, and of no other backend.
    static bool writeDumpImage(const std::string &path,
                               const unsigned char *color,
                               int width, int height);

    /// Transfer the finished frame (color + depth) into the caller's
    /// bound framebuffer. dstH names the destination surface's height
    /// for a sub-view blit -- the rect (dstX, dstY) is top-left
    /// widget coords, flipped against it into GL's bottom-left; 0
    /// keeps the full-surface transfer every plain frame does.
    ///
    /// OpenGL only, and it is the one part of a frame that cannot be
    /// anything else: the destination is the QOpenGLWidget's own
    /// framebuffer, and what bgfx hands over is a GL texture name only
    /// while bgfx is running on GL. On any other backend this is a
    /// no-op and Coin composites the view by itself -- which is why
    /// capturing a frame no longer goes through here.
    void blit(Render::RenderStats *stats,
              int dstX = 0, int dstY = 0, int dstH = 0);
#endif // !FC_RENDERER_STANDALONE

    QOpenGLWidget *widget = nullptr;

    /////////////////////////////////////////////////////////
    // Pass -> bgfx view id mapping (the view-id budget)
    //
    // The pass sequence is NUM_VIEWS wide, but a frame draws only a
    // fraction of it: no stateful emitters, no bulb shadow tiles, no
    // media interval passes, two or three overlay slots out of nine.
    // Reserving the full width for every viewer is what limited the
    // renderer to a handful of 3D views at once.
    //
    // So each frame declares which passes it will use (markPass, from
    // the same flags that configure the views) and mapPasses() hands
    // those -- in enum order, which is draw order, which is bgfx's
    // submission order -- the consecutive ids of a block whose size is
    // the high-water mark of what this view has needed, not NUM_VIEWS.
    //
    // A pass that was not marked maps to `sinkView`: a real, configured
    // id that renders into a 1x1 scratch target. Mispredicting liveness
    // therefore costs the pass's pixels and a warning naming it, never a
    // draw into a neighbouring pass and never bgfx's "invalid view id"
    // abort. sinkHits/sinkPasses report it.
    static const uint16_t kNoPass = 0xffff;
    uint16_t viewId = 0;         ///< block base
    uint16_t viewSpan = 0;       ///< ids reserved for the block
    uint16_t viewLive = 0;       ///< ids the current frame mapped (+ sink)
    uint16_t sinkView = 0;       ///< id of the discard view (last of the block)
    uint16_t idMap[NUM_VIEWS] = {};
    bool passMark[NUM_VIEWS] = {};
    mutable uint32_t sinkHits = 0;
    mutable uint64_t sinkPasses[(NUM_VIEWS + 63) / 64] = {};
    bool sinkReported = false;
    bgfx::FrameBufferHandle sinkFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sinkColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle sinkDepth = BGFX_INVALID_HANDLE;

    /// Start a frame's declaration: nothing is live until marked.
    void beginPasses()
    {
        std::memset(passMark, 0, sizeof(passMark));
        sinkHits = 0;
        std::memset(sinkPasses, 0, sizeof(sinkPasses));
    }
    /// Declare pass `p` (or the inclusive range `p`..`last`) live this frame.
    void markPass(int p, bool live = true)
    {
        if (live && p >= 0 && p < NUM_VIEWS)
            passMark[p] = true;
    }
    void markPasses(int p, int last, bool live = true)
    {
        for (; live && p <= last; ++p)
            markPass(p);
    }
    bool passLive(int p) const
    {
        return p >= 0 && p < NUM_VIEWS && passMark[p];
    }
    /// Ids the frame needs: the marked passes plus the sink.
    uint16_t passesNeeded() const
    {
        uint16_t n = 1;
        for (int i = 0; i < NUM_VIEWS; ++i)
            n += passMark[i] ? 1 : 0;
        return n;
    }
    /// Assign the marked passes consecutive ids from the block base.
    /// Must run before anything submits, and after the block is sized.
    void mapPasses()
    {
        uint16_t next = viewId;
        for (int i = 0; i < NUM_VIEWS; ++i)
            idMap[i] = passMark[i] ? next++ : kNoPass;
        sinkView = next++;
        viewLive = uint16_t(next - viewId);
        for (int i = 0; i < NUM_VIEWS; ++i) {
            if (idMap[i] == kNoPass)
                idMap[i] = sinkView;
        }
    }
    /// The bgfx view id of a pass, or the sink if the frame did not
    /// declare it. Never returns an id outside the block.
    /// Which pass a bgfx view id belongs to, for the per-view cost
    /// readout (docs/DrawSubmission.md phase 0). Inverts `idMap`, so it
    /// needs no `setViewName` and cannot drift from the real mapping.
    ///
    /// Named for the passes a CAD frame actually spends its draws in;
    /// anything else reports its pass index, which is enough to find it
    /// in the enum. A name is a convenience — the index is the fact.
    /// ⚠️⚠️ Resolve in the SAME FRAME the stat was taken. `idMap` is
    /// rebuilt every frame from which passes are live, so a raw bgfx
    /// view id means different passes in different frames — accumulating
    /// by id across a reporting window and naming it at the end
    /// attributes one pass's milliseconds to another. Only a pass whose
    /// mark is set is resolved; everything unmarked shares `sinkView`
    /// and cannot be told apart.
    int passIndexOf(uint16_t id) const
    {
        for (int p = 0; p < NUM_VIEWS; ++p) {
            if (passMark[p] && idMap[p] == id)
                return p;
        }
        return -1;
    }

    static std::string passNameOfIndex(int p)
    {
        {
            switch (p) {
            case ViewOpaque: return "opaque";
            case ViewTransparent: return "transparent";
            case ViewSelection: return "selection";
            case ViewOutline: return "outline";
            case ViewShadow: return "shadow";
            case ViewShadowTint: return "shadowtint";
            case ViewAOPrepass: return "aoprepass";
            case ViewAOGen: return "aogen";
            case ViewAOBlur: return "aoblur";
            case ViewBackground: return "background";
            case ViewOITComposite: return "oitcomposite";
            case ViewSectionCap: return "sectioncap";
            case ViewDebugScene: return "debugscene";
            case ViewIdReadback: return "idreadback";
            case ViewCaptureDepth: return "capturedepth";
            case ViewCapture: return "capture";
            case ViewWaterSurface: return "watersurface";
            case ViewGlassLineSdf: return "glasslinesdf";
            case ViewGlassSurface: return "glasssurface";
            case ViewGlassLine: return "glassline";
            case ViewParticles: return "particles";
            case ViewGroundRefl: return "groundrefl";
            case ViewVolGen: return "volgen";
            default: break;
            }
            if (p >= ViewBulbShadow0 && p <= ViewBulbShadow15)
                return "bulbshadow" + std::to_string(p - ViewBulbShadow0);
            if (p < 0)
                return "unmarked";
            return "pass" + std::to_string(p);
        }
    }

    uint16_t vid(int p) const
    {
        if (p < 0 || p >= NUM_VIEWS)
            return sinkView;
        if (!passMark[p]) {
            ++sinkHits;
            sinkPasses[p / 64] |= uint64_t(1) << (p % 64);
        }
        return idMap[p];
    }

    // 0 until the first init() sizes the targets -- and the value a
    // fresh sub-view bank starts from, so it must not be garbage: the
    // resize check compares it.
    uint16_t width = 0;
    uint16_t height = 0;
    // Reduced resolution of the expensive screen-space effect passes
    // (planar/ground reflection re-render, SSAO resolve) -- effectScale of
    // the view resolution, clamped in init(). The main scene, the geometry
    // prepass and the water depth prepass stay full-res.
    float effectScale = 1.0f;
    uint16_t effW = 0;
    uint16_t effH = 0;
    // SSAO resolve resolution (Render_SSAOResolution), independent of
    // effectScale; the AO gen/blur targets and view rects use it.
    float ssaoScale = 1.0f;
    uint16_t ssaoW = 0;
    uint16_t ssaoH = 0;
    bgfx::FrameBufferHandle bgfxFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bgfxColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bgfxDepth = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMesh = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInst = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInstTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInstOit = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshInstOitTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progFlat = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progFlatClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLine = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLineClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLinePat = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLinePatClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPoint = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPointClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshTexClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOitTex = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOitTexClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texBlendColor = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_lineQuadVb = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_lineQuadIb = BGFX_INVALID_HANDLE;
    bool m_instancing = false;
    bgfx::UniformHandle u_matColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matSpecular = BGFX_INVALID_HANDLE;
    // The draw's ambient term: rgb = the colour to add outright
    // (Material::ambient times the traversal's global ambient), w = 1.
    // w = 0 is the legacy floor for a feed that carries no Coin
    // lighting, where the shader falls back to 0.2 * base and the old
    // 0.8 diffuse weight -- see ViewLightConfig::ambient.
    bgfx::UniformHandle u_ambient = BGFX_INVALID_HANDLE;
    // The same ambient without the material factor: rgb = the traversal's
    // global ambient alone, w = 1 when fed. The metallic/roughness branch
    // takes it in this form, as a uniform-radiance environment, so that it
    // reaches a metal (which has no diffuse to fold an ambient into).
    bgfx::UniformHandle u_envAmbient = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_params = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_polyOffset = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_instParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_clipParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_clipPlanes = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_linePattern = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle oitAccum = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle oitReveal = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle oitFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOit = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMeshOitClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progComp = BGFX_INVALID_HANDLE;
    /// Debug buffer-visualization blit (docs/RenderDebug.md).
    bgfx::ProgramHandle m_progDebug = BGFX_INVALID_HANDLE;
    /// x = view mode, y = 1/max linear view depth (depth normalization),
    /// z = shadow-state valid this frame, w = scene shadow map size in
    /// texels (the mode-7 filtering probe).
    bgfx::UniformHandle u_debugParams = BGFX_INVALID_HANDLE;
    /// Debug scene re-render (modes 6/8): the counting/UV rasterization
    /// of the scene fills into their own full-res target, read back by
    /// the visualization blit.
    bgfx::ProgramHandle m_progDebugScene = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progDebugSceneClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texDebugScene = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle debugSceneTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle debugSceneDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle debugSceneFbo = BGFX_INVALID_HANDLE;
    uint16_t debugSceneW = 0;
    uint16_t debugSceneH = 0;
    /// CPU-readable copy of the id image (RenderDebug_CullAudit). A
    /// render target cannot be read back directly, so the audit blits
    /// into this and reads that.
    bgfx::TextureHandle idReadTex = BGFX_INVALID_HANDLE;
    uint16_t idReadW = 0;
    uint16_t idReadH = 0;
    /// Portable frame capture (readbackCapture). captureDepthTex is the
    /// colour target the depth encode writes; the two *Read textures
    /// are the CPU-readable copies the blit lands in.
    bgfx::ProgramHandle m_progDepthEnc = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texSceneDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle captureDepthTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle captureDepthFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle captureColorRead = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle captureDepthRead = BGFX_INVALID_HANDLE;
    /// Format of captureColorRead: the scene colour's own, since a blit
    /// requires source and destination formats to match and the scene
    /// colour is RGBA16F while colour managed (hdrScene).
    bgfx::TextureFormat::Enum captureColorFormat = bgfx::TextureFormat::Count;
    uint16_t captureW = 0;
    uint16_t captureH = 0;
    bgfx::UniformHandle s_texAccum = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texReveal = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCap = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCapClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texHatch = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_whiteTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_blackTex = BGFX_INVALID_HANDLE;
    /// 1x1 white two-layer ARRAY stand-in: what the per-face texture
    /// sampler is bound to when a draw has no palette. A sampler2DArray
    /// cannot stand in with the plain white texture above -- the two are
    /// different sampler types, and a mismatched bind is undefined
    /// rather than merely white.
    bgfx::TextureHandle m_whiteTexArray = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_hatchTex = BGFX_INVALID_HANDLE;
    uint64_t m_hatchVersion = 0;   // Private's hatch pixel generation
    static constexpr int kAOSamples = 16;
    bgfx::TextureHandle aoNormalZ = BGFX_INVALID_HANDLE;
    bool aoNormalZFp16 = true;   // prepass depth precision (see init)
    bgfx::TextureHandle aoDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoBlurTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoNoiseTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoPrepassFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoGenFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle aoBlurFbo = BGFX_INVALID_HANDLE;
    // GTAO prefiltered depth pyramid (XeGTAO depth MIP chain): six
    // successively halved single-channel viewZ levels below full res,
    // each its own texture+framebuffer (rendering into mip N of one
    // texture while sampling mip N-1 is a GL feedback hazard). Six
    // levels keep the per-tap level pick (one level per octave beyond
    // ~10px) cache-coherent out to the pixel radius cap — with fewer,
    // taps past the coarsest level stride sparsely through it and the
    // AO pass turns memory-bound as the camera zooms in.
    static constexpr int kAOMipLevels = 6;
    bgfx::TextureHandle aoMipTex[kAOMipLevels] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle aoMipFbo[kAOMipLevels] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::UniformHandle s_texAOMip[kAOMipLevels] = {
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle m_progGtaoDepth = BGFX_INVALID_HANDLE;
    int aoMipCount = 0;   // 0 = no pyramid (R32F/R16F not renderable)
    bgfx::ProgramHandle m_progPrepass = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPrepassClip = BGFX_INVALID_HANDLE;
    // Medium (water/glass/cloud/fire) interval depth writers: the
    // prepass layout with the body's appearance slot stamped into .x
    // (fs_fc_meddepth); u_mediumSlot.x carries the slot per submit.
    bgfx::ProgramHandle m_progMedDepth = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progMedDepthClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_mediumSlot = BGFX_INVALID_HANDLE;
    // Appearance slots per medium kind: distinct bodies get their own
    // parameter slot (uniform array entry), looked up per pixel via
    // the slot index in the interval targets. Must match MEDIUM_SLOTS
    // in fc_volume.sh.
    static constexpr int kMediumSlots = 4;
    // Local effect lights (LOCAL_LIGHTS in fc_mesh_fs.sh): the first
    // kMediumSlots entries carry the fire body flame lights, the rest
    // the light-source bodies (Render_Light bulbs).
    static constexpr int kLocalLights = 2 * kMediumSlots;
    bgfx::ProgramHandle m_progPrepassInst = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGtao = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGtaoBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSsaoBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texNormalZ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAONoise = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texAO = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoKernel = BGFX_INVALID_HANDLE;
    // Screen-space cavity (curvature) shading: one fullscreen multiply
    // reading the same prepass the AO chain reads. u_cavityParams:
    // x = valley strength, y = ridge strength, zw = prepass texel size.
    bgfx::ProgramHandle m_progCavity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cavityParams = BGFX_INVALID_HANDLE;
    // PBR image based lighting: a fixed procedural studio environment
    // built once on demand — a GGX-prefiltered cubemap mip chain for the
    // specular part and its irradiance SH for the diffuse part.
    static constexpr int kEnvSH = 9;
    // 128 and not 64: the procedural environments have EDGES now
    // (Render_PBREnvPreset), and 64 could not resolve a softbox
    // without the reflection breaking into blocks. Costs about a
    // megabyte of RGBA16F per view and a one-off prefilter, both paid
    // once when the environment changes rather than per frame.
    static constexpr uint16_t kEnvSize = 128;
    /// The background map is a second cube, and a sharper one: what
    /// the lighting map is good at (a prefiltered lobe per roughness)
    /// is not what a backdrop needs, and its base level is only as big
    /// as the reflections asked for. 256 a face is the same angular
    /// resolution the path tracer bakes its world at
    /// (SceneTranslator::translateWorld, 1024x512 equirect), so at
    /// blur 0 the two shading models show the SAME backdrop -- which
    /// is the whole point of the control. Its mips are plain box
    /// downsamples rather than GGX lobes: they are read as a tap
    /// footprint, not as a reflection lobe, and box levels cost
    /// nothing against the 64-sample prefilter the lighting cube pays.
    static constexpr uint16_t kEnvBgSize = 256;
    bgfx::TextureHandle m_envTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_envBgTex = BGFX_INVALID_HANDLE;
    /// Number of mip levels in m_envBgTex, so the blur factor has a
    /// range to land on.
    int m_envBgMips = 1;
    bgfx::TextureHandle m_dummyEnvTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texEnv = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pbrParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_outputParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_colorSpace = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_matcapParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envSH = BGFX_INVALID_HANDLE;
    float envSH[kEnvSH][4];
    /// User environment image the built cubemap came from (null = the
    /// procedural studio environment); a change invalidates the build.
    std::shared_ptr<const Render::TextureImage> m_envImage;
    /// Which procedural environment the built cube holds, so a change
    /// of preset invalidates it the way a change of image does.
    int m_envPreset = 0;
    bool m_envBuilt = false;   // build attempted (m_envTex may still be
                               // invalid when the caps disallow it)
    bool pbrFrame = false;     // PBR active for the frame being submitted
    // Matcap shading for the frame being submitted: a global shading
    // mode, so it rides the view rather than the per-draw material.
    bool matcapFrame = false;
    /// Background colour of the frame being submitted, packed RGBA.
    /// The Tessellation draw style fills its faces with it, the way
    /// Coin's SoRenderManager::HIDDEN_LINE pass does.
    uint32_t bgFillColor = 0x00000000;
    int matcapPreset = 0;
    float matcapTint = 0.0f;
    float pbrMetallic = 0.0f;
    // Read the Phong specular colour as PBR material data where nothing
    // states a metalness (PBRConfig::fromSpecular).
    bool pbrFromSpecular = false;
    /// Render::PBRConfig::shininessMapping for this frame.
    int pbrShininessMapping = 0;
    /// Render::OutputConfig::Transform for this frame.
    int outputTransform = 0;
    /// Render::OutputConfig::exposure for this frame.
    float outputExposure = 1.0f;

    /// What the sized targets were BUILT with: a floating point scene
    /// colour, or the 8-bit one.
    bool hdrScene = false;
    /// What the frame would like them built with -- set before init(),
    /// the way shadowSizeWanted is. Read through hdrSceneWanted(),
    /// never directly: this is the wish, that is the answer.
    bool hdrWanted = false;

    /// Should the scene colour be floating point?
    ///
    /// Only while colour managed, and only where the device can render
    /// to it. Two things want it and neither is optional once the
    /// pipeline works in light: an 8-bit target holds LINEAR light,
    /// which spends most of its codes on highlights nobody can
    /// distinguish and bands the darks, and it clips at one, which
    /// throws away exactly the headroom the exposure stage exists to
    /// bring back.
    ///
    /// ! Also false once the present target has failed to allocate. A
    /// floating point scene colour is only ever seen through that
    /// target's encode -- the desktop blit reads it -- so without one
    /// the frame would go to the screen as raw linear half-floats. This
    /// makes the next init() fall back rather than needing a failure
    /// path of its own.
    bool hdrSceneWanted() const {
        if (!hdrWanted || effectFailed[EffectPresent])
            return false;
        const uint16_t need = BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER
            | (msaaSamples > 1 ? BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA
                               : 0);
        const uint16_t got =
            bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA16F];
        return (got & need) == need;
    }
    /// Is this frame's pipeline colour managed -- authored colours
    /// decoded on the way in, the finished frame encoded on the way
    /// out? One question, so every unpack site asks it the same way.
    bool colorManaged() const {
        return outputTransform != Render::OutputConfig::None;
    }
    float pbrRoughness = 0.0f; // <= 0: derive from the material shininess
    float pbrEnvIntensity = 1.0f;
    /// How far out of focus the environment background is, 0..1
    /// (PBRConfig::envBlur); 1 is a 45-degree aperture, the widest
    /// defocus that still reads as a place (Render::envBlurAngle).
    float pbrEnvBlur = 0.25f;
    bgfx::UniformHandle s_texBump = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bumpParams = BGFX_INVALID_HANDLE;
    /// Machined surface finish of the draw being submitted
    /// (Material::finish): x = the App::SurfaceFinish pattern (0 =
    /// none, which is the whole cost of the feature on a scene that
    /// does not use it: a uniform-selected branch), y = pitch and
    /// z = depth in millimetres of object space, w = the lay angle in
    /// radians. Bound at every site that submits a mesh program.
    /// An ARRAY of Render::MaxFinishPalette entries: a per-face-finished
    /// draw uploads its whole palette, everything else uploads entry 0
    /// alone (which is all an unbound or zero index attribute reads).
    bgfx::UniformHandle u_finishParams = BGFX_INVALID_HANDLE;
    /// The projection frames of the finish above (Material::frame and
    /// Material::framepalette), THREE vec4 per entry: (origin, kind),
    /// (axis, radius), (xdir, spare). Entry 0 is the draw's own frame,
    /// and a draw whose faces differ fills the rest from its palette --
    /// the same arrangement as u_finishParams, and an ARRAY for the
    /// same reason. Kind 0 (unframed) is the triplanar projection, so
    /// an unbound index attribute reads the behaviour that predates
    /// frames.
    bgfx::UniformHandle u_frameParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texOcclusion = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texMetallicRoughness = BGFX_INVALID_HANDLE;
    /// Per-face texture palette of the mesh programs (unit 10): one
    /// ARRAY texture whose layers are the images the draw's faces are
    /// painted with. u_faceTexParams says whether and how it is read --
    /// x = on, y = millimetres of object space per tile (<= 0 = the
    /// mesh's own texture coordinates), z = the one layer the draw uses
    /// (< 0 = read the per-vertex stream), w = how many layers there
    /// are. Bound (with x = 0 and a 1x1 stand-in) on every mesh draw:
    /// a bgfx uniform holds its value for the rest of the frame, so a
    /// draw that left these alone would inherit the last one's palette.
    bgfx::UniformHandle s_texFace = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_faceTexParams = BGFX_INVALID_HANDLE;
    float bumpScale = 1.0f;    // bump/normal map strength (BumpConfig)
    bool bumpParallax = true;  // parallax-occlusion map height maps
    // Variance shadow map of the Shadow draw style's scene light.
    // Coin sizes its shadow map as precision * min(2048, max texture
    // size) (ShadowPrecision, default 1.0 -> 2048); the targets are
    // (re)created for the requested size by ensureShadowTargets.
    static constexpr uint16_t kShadowMaxSize = 2048;
    uint16_t shadowSize = 0;
    /// The size the configuration asks for, resolved from
    /// ShadowPrecision by the frame before it reconciles EffectShadow.
    /// The one group whose extent is a setting rather than the
    /// viewport, so allocEffect() reads it instead of width/height and
    /// a change to it rebuilds the set (updateEffect).
    uint16_t shadowSizeWanted = 0;
    bgfx::TextureFormat::Enum shadowFormat = bgfx::TextureFormat::RG32F;
    bool m_shadow = false;     // shadow resources exist (caps allow it)
    // The reduced RG16F moment path (float32 not linearly filterable) always
    // runs the EVSM warp -- fp16 plain (z, z^2) moments are too imprecise.
    bool m_shadowForceWarp = false;
    float shadowWarp = 42.0f;  // EVSM exponent (by moments format)
    // Per-frame shadow lookup state: warp 0 = plain VSM (Coin
    // parity, SmoothBorder 0), else the EVSM exponent above;
    // epsilon/threshold are Coin's VsmLookup parameters.
    float shadowWarpFrame = 0.0f;
    float shadowEpsilon = 1.0e-5f;
    float shadowThreshold = 0.0f;
    // Coin's N-tap receiver spread kernel (ShadowSpreadSize /
    // SpreadSampleSize), packed into u_evsm.zw: tap spacing in shadow
    // map uv and the kernel mode (0 off, 1 dithered 4-tap, N >= 3 an
    // N x N grid).
    float shadowSpreadUv = 0.0f;
    float shadowSpreadMode = 0.0f;
    bool shadowFrame = false;  // shadows active this frame
    // A scene light is fed this frame (LightConfig::valid): the light
    // uniforms hold it and the shaders shade with it instead of the
    // fixed headlight. Deliberately independent of shadowFrame -- the
    // shadow map needs caps, a fitted scene bound and Render_Shadow,
    // and losing any of those must dim the shadow, not the light.
    bool lightFrame = false;
    bgfx::TextureHandle shadowTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowTintTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowTintFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowTintBlurTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowTintBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowTintBlurBackFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowTint = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texShadowTint = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shadowDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowFbo = BGFX_INVALID_HANDLE;
    // ShadowSmoothBorder blur ping texture and the two color-only
    // framebuffers of the separable passes (the back one re-targets
    // shadowTex).
    bgfx::TextureHandle shadowBlurTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle shadowBlurBackFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadow = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowInst = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progShadowBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowBlur = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texShadow = BGFX_INVALID_HANDLE;
    // Screen-space AO sampler of the mesh programs (unit 9) and the
    // frame's AO chain result to bind there — invalid when AO is off
    // this frame (the white stand-in binds instead). Set by render()
    // before the scene submits; the AO views run before ViewOpaque, so
    // the opaque pass samples this frame's result.
    bgfx::UniformHandle s_texAOScreen = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle aoMeshTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightPos = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowMatrix = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_evsm = BGFX_INVALID_HANDLE;
    float lightDirView[3] = {0.0f, 0.0f, -1.0f}; // view space
    // rgb = color * intensity; w = the spot falloff exponent
    // (dropOffRate * 128; 0 for directional lights, pow(x, 0) = 1).
    float lightColorI[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    // Spot light position in view space; w = cos(cutOffAngle) for a
    // spot light, -1 for a directional one (the shader switch).
    float lightPosView[4] = {0.0f, 0.0f, 0.0f, -1.0f};
    // Fire body effect lights (unshadowed flickering point lights at
    // the flame centroids, one entry per fire appearance slot): xyz =
    // position in view space, w = 1 / range^2 (0 = slot inactive this
    // frame). The colors arrive premultiplied by intensity and the
    // animation-clock flicker.
    bgfx::UniformHandle u_localLight = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_localLightColor = BGFX_INVALID_HANDLE;
    float localLightView[kLocalLights][4] = {};
    float localLightColorI[kLocalLights][4] = {};
    // The ordinary Coin lights of the frame (Render::ViewLightConfig:
    // the viewer's headlight and backlight, document directional/point
    // lights), resolved to camera view space. These replace what used
    // to be a hard-coded white headlight down the view axis, and when
    // the feed carries none -- an old scene dump, a consumer that
    // predates the config -- that same headlight is written into slot 0
    // as a stand-in, so the shader has one code path and no fallback
    // branch of its own.
    //   viewLightView[i]:   xyz = view-space direction the light travels
    //                       (directional) or position (positional/spot),
    //                       w = 0 inactive, 1 directional, 2 positional,
    //                       3 spot, 4 a spot's cone slot
    //   viewLightColorI[i]: rgb = color premultiplied by intensity,
    //                       w = a spot's falloff exponent
    //   viewLightAtt[i]:    xyz = Coin's squared/linear/constant
    //                       distance attenuation (positional only),
    //                       w = a spot's cone cutoff cosine
    // A spot's axis does not fit in one slot, so it takes the next one
    // whole (xyz = axis, w = 4) rather than a fourth uniform array --
    // see the packing in BGFXFrame.cpp and docs/RenderEngine.md 3.2.
    static constexpr int kViewLights = Render::MaxViewLights;
    bgfx::UniformHandle u_viewLight = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_viewLightColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_viewLightAtt = BGFX_INVALID_HANDLE;
    float viewLightView[kViewLights][4] = {};
    float viewLightColorI[kViewLights][4] = {};
    float viewLightAtt[kViewLights][4] = {};
    // The traversal's global ambient (ViewLightConfig::ambient) and
    // whether the feed carried one at all. Per draw, the ambient term
    // is this times the material's own ambient colour; unfed leaves
    // every draw on the legacy flat floor.
    uint32_t viewAmbient = 0x333333ff;
    bool viewAmbientFed = false;
    float shadowMtx[16];       // camera view space -> shadow uv/depth
    // Cached shadow map: hash of the light camera + caster set of the
    // moments currently in shadowTex; the caster pass (and blur) only
    // re-runs when it changes. 0 = nothing rendered yet.
    uint64_t shadowMapHash = 0;
    // AO/prepass cache key: camera + viewport + AO params + prepass draw
    // set (see the aoRender hash in render()); 0 = never cached.
    uint64_t aoMapHash = 0;
    /// Which idle-accumulation sample the mirrored-scene and media
    /// interval targets were last rendered at. -1 so the first frame
    /// of a view cannot match and both are drawn.
    int reflSampleIndex = -1;
    int mediumSampleIndex = -1;
    // The GPU downgrade sweep's unlanded orders (SceneLadder.h): what
    // keeps a plan that samples the apply transient from re-correcting
    // off it. Per view, like the meters it reconciles.
    Render::DowngradeLedger dgLedger;
    // Camera+viewport of the previous frame (medium-interval and
    // planar-reflection frame cache — see staticFrame in render()).
    uint64_t camFrameHash = 0;
    bool m_ssao = false;     // SSAO resources exist (caps allow it)
    // Volumetric light shafts: half-res raymarch of the shadow map
    // bounded by the prepass depth, so both resource sets must exist.
    bool m_vol = false;      // volumetric resources exist
    bgfx::TextureHandle volTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle volFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle volHistTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle volHistFrontTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle volFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle volHistFbo = BGFX_INVALID_HANDLE;
    /// Consecutive static frames feeding the volumetric temporal
    /// accumulation (0 = replace history this frame).
    int volAccumFrames = 0;
    /// User volume-stage medium splice (docs/RenderEngine.md §5.11):
    /// synthetic shaders assembled per frame from the fire and cloud
    /// slots bound to a user medium function — variants of the
    /// volumetric raymarch / extinction / reflection-media programs
    /// with the user field/ramp (fire channel) and scatter density
    /// (cloud channel) dispatched for those slots. Null = all media
    /// stock. volUserKey tracks the slot-source tuple (fire slots then
    /// cloud slots) so the strings only reassemble when a binding
    /// changes.
    std::shared_ptr<const Render::UserShader> volUserVol;
    std::shared_ptr<const Render::UserShader> volUserExt;
    std::shared_ptr<const Render::UserShader> volUserRefl;
    std::array<const void *, 8> volUserKey {};
    /// Compiler-less tier only: fingerprint of the splice table the
    /// current variants were adopted from, and whether that adoption
    /// found binaries for all of them. The server compiles the viewer
    /// binaries asynchronously, so the snapshot that first carries a
    /// binding can carry no binaries with it; a half-adopted variant
    /// is retried when a republished table arrives rather than on
    /// every frame, which would reassemble the sources forever if the
    /// compile never succeeds.
    size_t volSpliceFingerprint = 0;
    bool volUserAdopted = true;
    bgfx::ProgramHandle m_progVol = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolApply = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolAccum = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progReflMedia = BGFX_INVALID_HANDLE;
    // Bloom (glow) chain: quarter-res halo source + blur ping target.
    bgfx::TextureHandle bloomTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bloomBlurTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomBlurFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomBright = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomEmit = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progBloomApply = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texBloom = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomTexel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomBlur = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progSun = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sunParams = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progEnvBg = BGFX_INVALID_HANDLE;
    // Bulb (Render_LightShadow) shadow atlas: 2x2 tiles of plain VSM
    // moments, one per bulb light slot; a tile re-renders only when its
    // hash (bulb + casters) changes.
    /// Atlas tiles (4x4 grid): a plain-shadow bulb uses one downward
    /// tile, an extended (Render_LightShadowExtended) bulb six cube
    /// faces; tiles allocate sequentially per frame.
    static constexpr int kBulbShadowTiles = 16;
    static constexpr int kBulbShadowGrid = 4;
    static constexpr uint16_t kBulbShadowTileSize = 512;
    bgfx::TextureHandle bulbShadowTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle bulbShadowDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bulbShadowFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texBulbShadow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bulbShadowMtx = BGFX_INVALID_HANDLE;
    /// Camera-view-space -> atlas uv/depth per tile, refreshed every
    /// frame (the camera moves); identity-w row zeroed when inactive.
    float bulbShadowMtx[kBulbShadowTiles][16] = {};
    /// Light camera per tile for the caster views (world space).
    float bulbShadowViewMtx[kBulbShadowTiles][16];
    float bulbShadowProjMtx[kBulbShadowTiles][16];
    uint64_t bulbShadowHash[kBulbShadowTiles] = {};
    bool bulbShadowValid[kBulbShadowTiles] = {};
    /// Per bulb slot: x = first atlas tile, y = tile (face) count —
    /// 1 = plain downward cone, 6 = cube faces.
    float bulbShadowConf[kLocalLights - kMediumSlots][4] = {};
    /// Camera view -> world rotation for the shader's cube-face pick
    /// (directions only, translation ignored via w = 0).
    float bulbShadowRotMtx[16] = {};
    bgfx::UniformHandle u_bulbShadowConf = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bulbShadowRot = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texVolFront = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progVolExt = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progCaustics = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texVol = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volMedium = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_causticParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_volTexel = BGFX_INVALID_HANDLE;
    // Water medium: front/back depth targets of water body draws
    // (prepass shader family) bounding the underwater ray stretch.
    bgfx::TextureHandle waterFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle waterBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle waterFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle waterBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle waterFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle waterBackFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texWaterFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texWaterBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterSigma = BGFX_INVALID_HANDLE;
    // Glass body: front/back depth targets bounding the absorption
    // interval of the glass surface pass.
    bgfx::TextureHandle glassFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glassBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glassFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glassBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle glassFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle glassBackFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGlass = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texGlassFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texGlassBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_glassParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_glassTint = BGFX_INVALID_HANDLE;
    // Line signed-distance field, sampled by the glass surface pass so
    // an edge seen through glass warps exactly like the face it lies on
    // (docs/RenderEngine.md, "Lines"). RGB is the line colour; alpha is
    // kLineSdfRadius minus the signed distance to the line's EDGE in
    // pixels, so the target clears to zero = "no line within reach".
    // LINEAR filtering, unlike the point-sampled interval targets: the
    // whole method rests on the field interpolating smoothly.
    bgfx::TextureHandle lineSdfTex = BGFX_INVALID_HANDLE;
    /// Sidecar of whichever decoration won each texel: its
    /// perpendicular axis, view depth and half width, everything the
    /// glass pass needs to turn the field's distance into post-lens
    /// coverage (fc_line_sdf_fs.sh has the layout and the why).
    bgfx::TextureHandle lineSdfAuxTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle lineSdfDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle lineSdfFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLineSdf = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progLineSdfClip = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPointSdf = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progPointSdfClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texLineSdf = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texLineSdfAux = BGFX_INVALID_HANDLE;
    // Cloud body: front/back depth targets bounding the FBM medium
    // interval of the volumetric raymarch.
    bgfx::TextureHandle cloudFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloudBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloudFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloudBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle cloudFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle cloudBackFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texCloudFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texCloudBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cloudParams = BGFX_INVALID_HANDLE;
    // Fire body: front/back depth targets bounding the emissive flame
    // interval of the volumetric raymarch.
    bgfx::TextureHandle fireFrontTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fireBackTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fireFrontDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fireBackDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fireFrontFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle fireBackFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texFireFront = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texFireBack = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fireParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fireParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fireFrame = BGFX_INVALID_HANDLE;
    // Water surface refraction (scene copy) + ground reflection targets.
    bgfx::TextureHandle sceneCopyTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle sceneCopyFbo = BGFX_INVALID_HANDLE;
    /// The output colour transform's destination (EffectPresent): the
    /// encoded frame, which is what the desktop then blits into the Qt
    /// framebuffer. Only allocated while a transform is selected -- with
    /// none, the blit takes the scene colour directly as it always did.
    bgfx::TextureHandle presentTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle presentFbo = BGFX_INVALID_HANDLE;
    /// Idle temporal accumulation (EffectAccum): the running average of
    /// the jittered frames, kept while the camera and the scene hold
    /// still and copied back over the scene colour every frame.
    ///
    /// Floating point whatever the scene target is. This is where the
    /// convergence actually lives, and eight bits cannot hold it: at
    /// sample 32 a frame arrives with weight 1/33, so an 8-bit history
    /// rounds every difference below four codes straight back to what it
    /// already held and the average stops moving after a handful of
    /// samples -- which looks exactly like the feature working and then
    /// giving up.
    bgfx::TextureHandle accumTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle accumFbo = BGFX_INVALID_HANDLE;
    /// Consecutive frames the accumulation was asked for and did not
    /// engage, and whether that has been reported. "On, and nothing
    /// happens" has several causes that all look identical on screen.
    int accumQuietFrames = 0;
    bool accumReported = false;
    /// Jittered samples already averaged into accumTex. 0 = the history
    /// holds nothing this camera may keep, so the next frame replaces it
    /// outright (blend factor 1) and draws unjittered.
    int accumFrames = 0;
    /// The jittered projection of the frame being drawn. A member and
    /// not a local of render(): the view keeps the pointer it is handed
    /// (BGFXView::projMatrix) for the rest of the frame, and a stack
    /// copy would leave it dangling.
    float accumProj[16] = {};
    bgfx::TextureHandle reflTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle reflDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle reflFbo = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progWaterCopy = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progWater = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGroundRefl = BGFX_INVALID_HANDLE;
    /// Shadow-only ground (LightConfig::groundShadowOnly): the same
    /// quad as the solid one, painting the shadow alone.
    bgfx::ProgramHandle m_progGroundShadow = BGFX_INVALID_HANDLE;
    /// The same thing without the quad -- a fullscreen pass that finds
    /// the ground plane per pixel (submitShadowGroundPlane).
    bgfx::ProgramHandle m_progGroundShadowPlane = BGFX_INVALID_HANDLE;
    /// The DRAWN ground, in the mesh program's ground variants: the
    /// same shading with the rim faded out (GROUND_FADE). Variants
    /// rather than a flag on the scene's own mesh program, so the fade
    /// uniforms cannot leak into a scene draw.
    bgfx::ProgramHandle m_progGroundFade = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_progGroundFadeTex = BGFX_INVALID_HANDLE;
    /// ... and in the prepass, where the rim is discarded rather than
    /// faded: what is not drawn must not stop a volumetric shaft
    /// either.
    bgfx::ProgramHandle m_progGroundFadePrepass = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_groundFadeU = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_groundFadeV = BGFX_INVALID_HANDLE;
    /// The ground plane in VIEW space for that pass: xyz the unit
    /// normal, w the offset, so a point is on it where
    /// dot(xyz, p) + w == 0.
    bgfx::UniformHandle u_groundPlane = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texScene = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texRefl = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterSurf = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterAbsorb = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterRipple = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterSplash = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fountainParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fountainFrame = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_reflParams = BGFX_INVALID_HANDLE;
    // Stateful particles (docs/RenderEngine.md §5.8): the state
    // samplers and the step uniforms, plus the stock reset program.
    // Created once with the view, not with the size-dependent targets.
    bgfx::ProgramHandle m_progPSimInit = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pstate0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pstate1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pgrid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pboxMin = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pboxMax = BGFX_INVALID_HANDLE;
    /// RGBA32F is renderable on this backend — WebGL2 only has it with
    /// EXT_color_buffer_float, which bgfx reports through the format
    /// caps. Without it there is nowhere to keep particle state and
    /// stateful emitters fall back to their stateless vertex stage.
    bool particleStateOk = false;
    /// Particle impact map (docs/RenderEngine.md §5.8): one RGBA32F
    /// texel per cell of the water's world footprint holding the most
    /// recent hit there — xy = where, z = when, w = how hard. Written
    /// by the splat pass, read by the water surface, and deliberately
    /// never cleared: a ring outlives by far the step that started it,
    /// and an expired record ages out on its own.
    bgfx::ProgramHandle m_progPImpact = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pimpsrc = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_impactFrame = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_impactNow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texImpact = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterImpact = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterImpactCfg = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impactTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle impactFbo = BGFX_INVALID_HANDLE;
    /// xy = world min corner of the map's footprint, zw = 1 / extent.
    float impactFrame[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    /// The clock the map's records are stamped against — the raw
    /// animation seconds, not the wave clock, so the wave speed can be
    /// tuned without ageing every ring that is already travelling.
    float impactNow = 0.0f;
    /// The map holds this frame's impacts and the water surface may
    /// read it.
    bool impactActive = false;
    /// An emitter reset this frame, so its history is being replayed
    /// from the start and what it struck on the way there is no longer
    /// about anything. Clearing on that is what keeps a frozen frame a
    /// pure function of the warm-up with water in the scene, the same
    /// as it is without.
    bool impactReset = false;
    /// Cells across the map. Rings die two cells out (the water
    /// surface's 5x5 neighbourhood), so this is what sets their size
    /// relative to the pool: coarser cells make bigger, longer rings
    /// and lose more of them to hits sharing a cell.
    static constexpr uint16_t kImpactRes = 24;
    /// A stateful emitter drew this frame and its simulation is running
    /// (not frozen). Unlike the step budget's "still owes steps", this
    /// stays true once an emitter has caught up with the clock, which
    /// is exactly when its sprites keep moving — what the mirrored and
    /// cached passes have to follow.
    bool particlesLive = false;
    // Redirect submit() into the ground reflection view (mirrored
    // camera, flipped culling).
    bool reflPass = false;
    /// Clip the mirror pass to the half-space above its plane, and the
    /// world-space plane equation to clip against (dot(pos, p) >= 0
    /// keeps the fragment, so z >= planeZ is (0,0,1,-planeZ)).
    bool reflClip = false;
    float reflClipPlane[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    bool m_oit = false;      // OIT resources exist (caps allow it)
    bool oitFrame = false;   // OIT active for the frame being submitted
    /// Whether the water surface was last seen with nothing for its
    /// wave normals to act on, so the report below is made when that
    /// state is entered rather than on every frame it persists.
    bool waterNoResponse = false;
    std::unordered_map<uint64_t, GpuMesh> meshes;
    /// Shared colorless geometry buffers keyed by content; GpuMesh::geom
    /// points into this map (values are node-stable).
    std::unordered_map<GeomKey, GpuGeometry, GeomKeyHasher> geometries;
    /// Shared all-white color stream of meshes without baked colors.
    bgfx::VertexBufferHandle whiteColorVb = BGFX_INVALID_HANDLE;
    int whiteColorCount = 0;
    std::unordered_map<uint64_t, GpuTexture> textures;
    /// Per-face texture palettes as array textures, keyed by the
    /// palette's content (the layer image ids in order).
    std::unordered_map<uint64_t, GpuTextureArray> textureArrays;
    uint64_t frame = 0;
    int drawcount = 0;
    /// Geometry the handle pool refused this frame (tryUploadGeometry):
    /// draw submissions that asked for a mesh with no vertex buffer and
    /// were skipped, and how many DISTINCT meshes that was -- one mesh
    /// is asked for by several passes, so the submission count alone
    /// cannot say how much geometry is actually missing from the
    /// screen. Both reset with drawcount at the top of the frame.
    size_t bufferDeniedSubmits = 0, bufferDeniedMeshes = 0;
    // Standalone (WebGL2) one-shot warmup: the very first MSAA scene
    // framebuffer created while bgfx's async WebGL2 init is still settling
    // renders the overlay views wrong (a stale-target artifact — the corner
    // overlay draws the main scene). Re-creating the target once, a few frames
    // after the scene is first present, clears it for good: counts frames with
    // a scene on screen, then sets -1 once the rebuild has fired, so it costs
    // one re-create per view and not one per streamed arrival. (Regression:
    // 6065ad06ed dropped the interaction-triggered rebuild that masked this.)
    int warmup = 0;
    /// init() could not build the scene framebuffer -- the handle pool
    /// is full. Latched so the frame path stops asking: a bailed frame
    /// never reaches bgfx::frame(), and bgfx reclaims a destroyed
    /// handle only at a frame boundary, so retrying every frame both
    /// spins and eats the pool it is waiting on. Cleared by the next
    /// size, scale or program change, which is when there is anything
    /// new to try.
    bool targetsFailed = false;
    bool ontop = false;   // route submits to the highlight pass
    // This frame's shaded image comes from the attached consumer
    // (Renderer::setExternalBaseLayer): scene triangles rasterize
    // depth-only, lines/points and non-on-top selections draw in
    // ViewOnTop over the consumer's blit, transparent scene triangles
    // are skipped (docs/CyclesIntegration.md sec 5.3).
    bool externalBase = false;
    bool selPass = false; // route opaque-view submits into ViewSelection
                          // (non-on-top selection draws follow the opaque
                          // scene in submission order, GL pass parity)
    bool glassLines = false; // a glass body is rendering this frame:
                             // scene lines/points leave ViewOpaque for
                             // ViewGlassLine so the refraction cannot
                             // magnify them (see the enum comment)
    int overlayView = -1; // >= 0: route submits into this overlay view
    // Anchor + rect pixel height of the overlay currently being submitted (set
    // alongside overlayView); billboard text sizes itself against the overlay's
    // own mini projection + rect instead of the main scene view.
    const Render::OverlayAnchor *overlayAnchor = nullptr;
    float overlayRectHeight = 0.f;
    int msaaSamples = 0;  // sample count the current targets were built with
    int shaderGen = 0;    // _BGFXLib.shaderGeneration the programs were
                          // loaded at; a lag re-inits the view (hot-reload)
    // The last init() could not load a program the view cannot draw
    // without (a missing or stale stock shader pack). The view stays
    // torn down and every frame reports failure -- on the desktop that
    // is the Coin fallback -- until the generation moves, so a
    // reloadShaders() over a repaired pack recovers.
    bool shaderFailed = false;
    // Set with it: the abandoned init's queued bgfx commands still
    // need one frame to execute (see the frame path's bail).
    bool shaderFailedDrain = false;
    // Per-frame world-to-screen scale consumed by autozoom draws
    // (Renderer::setAutoZoomScale).
    float autozoomScale = 1.0f;
    // Current frame's view/projection matrices (GL-layout), for billboard
    // autozoom draws (SoTextImage): the screen-aligned basis is read from the

    // view matrix and the screen-constant size from the anchor's own view
    // depth + projection in setDrawTransform. Null outside a frame -> billboard
    // falls back to plain scaling.
    const float *viewMatrix = nullptr;
    const float *projMatrix = nullptr;
    /// The present pass' program (fs_fc_present): the output colour
    /// transform on every tier, and standalone also the copy that puts
    /// the frame on the default backbuffer.
    bgfx::ProgramHandle m_progPresent = BGFX_INVALID_HANDLE;
#ifndef FC_RENDERER_STANDALONE
    GLuint fbo = 0;
    GLuint fboDepth = 0;
    GLuint blitColorId = 0;
    bool hasFBO = false;
    /// Which texture the cached blit framebuffer wraps: the encoded
    /// frame (presentTex) or the linear scene colour. Turning the
    /// output transform on or off changes it, and the cache has to be
    /// rebuilt around the new one.
    bool blitSourceEncoded = false;
#endif

    // ---- Per-sub-view state banks (docs/SplitViews.md sec 9.2) ----
    //
    // renderSubViews drives ONE BGFXView through N (viewport, camera)
    // sub-views per wall-clock frame, all sharing the resident scene.
    // What is per-camera is exactly the state destroyTargets() sweeps
    // or resets -- the LifeSized targets, their sizes and exist/fail
    // bookkeeping, the temporal hashes and accumulation counters --
    // plus the pass-id block; the shared remainder (programs,
    // uniforms, uploaded scene caches, particle state, the
    // caps-derived m_* ability flags) keeps its single copy.
    //
    // The members stay right where they are declared: sub-view i is
    // rendered by swapping bank i into them and stashing it back out
    // afterwards, so no use site changes, and a field added to the
    // list below is per-sub-view by construction. Bank id 0 is the
    // implicit full-canvas sub-view every plain render() uses;
    // desktop frames never leave it.
#ifndef FC_RENDERER_STANDALONE
// Desktop-only bank fields: the cached GL blit framebuffer wraps ONE
// bank's textures, so it swaps with them (docs/SplitViews.md 13.3).
#define FC_SUBVIEW_FIELDS_HOST(X) \
    X(fbo) X(fboDepth) X(blitColorId) X(hasFBO) X(blitSourceEncoded)
#else
#define FC_SUBVIEW_FIELDS_HOST(X)
#endif
#define FC_SUBVIEW_FIELDS(X) \
    FC_SUBVIEW_FIELDS_HOST(X) \
    X(viewId) X(viewSpan) X(viewLive) X(sinkView) X(idMap) X(passMark) \
    X(sinkHits) X(sinkPasses) X(sinkReported) \
    X(sinkFbo) X(sinkColor) X(sinkDepth) \
    X(width) X(height) X(effectScale) X(effW) X(effH) \
    X(ssaoScale) X(ssaoW) X(ssaoH) X(msaaSamples) X(hdrScene) \
    X(bgfxFbo) X(bgfxColor) X(bgfxDepth) \
    X(oitFbo) X(oitAccum) X(oitReveal) \
    X(debugSceneFbo) X(debugSceneTex) X(debugSceneDepth) X(idReadTex) \
    X(captureDepthFbo) X(captureDepthTex) X(captureColorRead) \
    X(captureDepthRead) X(captureColorFormat) \
    X(captureW) X(captureH) \
    X(aoPrepassFbo) X(aoGenFbo) X(aoBlurFbo) X(aoMipFbo) \
    X(aoNormalZ) X(aoDepth) X(aoTex) X(aoBlurTex) X(aoNoiseTex) \
    X(aoMipTex) X(aoMipCount) X(aoMapHash) \
    X(volFbo) X(volHistFbo) X(volTex) X(volFrontTex) \
    X(volHistTex) X(volHistFrontTex) X(volAccumFrames) \
    X(bloomFbo) X(bloomBlurFbo) X(bloomTex) X(bloomBlurTex) \
    X(bulbShadowFbo) X(bulbShadowValid) X(bulbShadowHash) \
    X(waterFrontFbo) X(waterBackFbo) X(glassFrontFbo) X(glassBackFbo) \
    X(cloudFrontFbo) X(cloudBackFbo) X(fireFrontFbo) X(fireBackFbo) \
    X(waterFrontTex) X(waterBackTex) X(waterFrontDepth) X(waterBackDepth) \
    X(glassFrontTex) X(glassBackTex) X(glassFrontDepth) X(glassBackDepth)     X(lineSdfFbo) X(lineSdfTex) X(lineSdfAuxTex) X(lineSdfDepth) \
    X(cloudFrontTex) X(cloudBackTex) X(cloudFrontDepth) X(cloudBackDepth) \
    X(fireFrontTex) X(fireBackTex) X(fireFrontDepth) X(fireBackDepth) \
    X(sceneCopyTex) X(sceneCopyFbo) X(presentTex) X(presentFbo) \
    X(accumTex) X(accumFbo) X(accumFrames) X(accumProj) \
    X(accumQuietFrames) X(accumReported) \
    X(reflTex) X(reflDepth) X(reflFbo) \
    X(reflSampleIndex) X(mediumSampleIndex) \
    X(shadowSize) X(shadowFbo) X(shadowDepth) X(shadowTex) \
    X(shadowBlurTex) X(shadowBlurFbo) X(shadowBlurBackFbo) \
    X(shadowTintTex) X(shadowTintFbo) X(shadowTintBlurTex) \
    X(shadowTintBlurFbo) X(shadowTintBlurBackFbo) X(shadowMapHash) \
    X(m_lineQuadVb) X(m_lineQuadIb) \
    X(camFrameHash) X(warmup) X(targetsFailed) \
    X(effectFailed) X(glassSeen)

    struct SubViewBank {
#define FC_SV_DECL(f) decltype(BGFXView::f) f;
        FC_SUBVIEW_FIELDS(FC_SV_DECL)
#undef FC_SV_DECL
    };
    template <typename T>
    static std::enable_if_t<!std::is_array<T>::value>
    svAssign(T &dst, const T &src) { dst = src; }
    template <typename T, size_t N>
    static void svAssign(T (&dst)[N], const T (&src)[N])
    { std::copy(src, src + N, dst); }
    void stashSubView(SubViewBank &b) const
    {
#define FC_SV_STASH(f) svAssign(b.f, f);
        FC_SUBVIEW_FIELDS(FC_SV_STASH)
#undef FC_SV_STASH
    }
    void loadSubView(const SubViewBank &b)
    {
#define FC_SV_LOAD(f) svAssign(f, b.f);
        FC_SUBVIEW_FIELDS(FC_SV_LOAD)
#undef FC_SV_LOAD
    }
    /// The pristine bank captured at construction: what a sub-view
    /// that has never rendered starts from -- every handle invalid,
    /// size 0, hashes 0 -- so its first frame takes the ordinary
    /// build path. The in-class initializers stay the single source
    /// of truth for what "fresh" means.
    SubViewBank freshBank;
    /// Inactive sub-views by client id. The ACTIVE sub-view lives in
    /// the members themselves, never in the map.
    std::map<int, SubViewBank> subBanks;
    int activeSub = 0;

    BGFXView() { stashSubView(freshBank); }
    /// Swap sub-view \a id into the members (a no-op when it already
    /// is). An id never seen before starts from freshBank.
    void selectSubView(int id)
    {
        if (id == activeSub)
            return;
        const decltype(msaaSamples) liveMsaa = msaaSamples;
        stashSubView(subBanks[activeSub]);
        auto it = subBanks.find(id);
        if (it == subBanks.end()) {
            loadSubView(freshBank);
            // A fresh bank's zeroed sample count must not read as an
            // MSAA change: the programs are shared and already right,
            // and init(false) mid-frame destroys and relinks them per
            // bank -- twice in one un-flushed frame that exhausted the
            // handle pool and blacked the sub-view out. Inheriting the
            // live count leaves only the size mismatch, which is the
            // keepShared init the fresh bank actually needs.
            msaaSamples = liveMsaa;
        }
        else {
            loadSubView(it->second);
            subBanks.erase(it);
        }
        activeSub = id;
    }
};


/// Occlusion query handles, leased one per test, for the two things
/// that ask the depth buffer questions: the measurement probe below and
/// the culler that acts on the same answers.
///
/// ⚠️⚠️ **A bgfx query handle is an object's identity, not a slot to
/// rent** (docs/FarFieldProxies.md §12.11). `createOcclusionQuery()` is
/// the only place bgfx ever writes `NoResult` into a handle's slot —
/// nothing resets it on submit, not the frame swap and not the
/// backend's own begin — so once a handle has answered, `getResult()`
/// returns that same answer for as long as the handle lives. A pool of
/// anonymous handles reassigned per frame therefore serves each test
/// the *previous occupant's* verdict, and the "not answered yet, read
/// it again next frame" guard that would catch a mismatch can never
/// fire. Silently, too: `BGFX_CONFIG_DEBUG_OCCLUSION` asserts only that
/// a handle is not used twice within a single frame, and cross-frame
/// reuse trips nothing.
///
/// So a handle here is created for exactly one test and destroyed when
/// its answer is read. `NoResult` then means what every reader in this
/// file takes it to mean — *this* query has not landed — and nothing
/// else can have written the slot, because creating a handle also
/// invalidates any in-flight query still holding that index.
///
/// The price is a create/destroy pair per test rather than per node,
/// and a transient second handle per lease: bgfx defers the free to the
/// end of the frame, so a released handle cannot be re-allocated until
/// then. src/3rdParty/CMakeLists.txt raises
/// `BGFX_CONFIG_MAX_OCCLUSION_QUERIES` to cover both consumers at that
/// width, and both of them size their appetite from
/// `caps->limits.maxOcclusionQueries` rather than from that number, so
/// a backend built without it culls less instead of answering wrongly.
class OcclusionLeases
{
public:
    /// A handle that reads `NoResult` until the query submitted against
    /// it lands. Invalid when the backend has none left to give, which
    /// costs culling and never correctness: the caller drops that test
    /// and the node keeps whatever it already believed.
    bgfx::OcclusionQueryHandle acquire()
    {
        bgfx::OcclusionQueryHandle q = bgfx::createOcclusionQuery();
        if (bgfx::isValid(q))
            ++leased;
        else
            ++refused;
        return q;
    }

    /// Hand a handle back, once its answer has been read or once the
    /// test it was created for has been given up on.
    void release(bgfx::OcclusionQueryHandle q)
    {
        if (!bgfx::isValid(q))
            return;
        bgfx::destroy(q);
        --leased;
    }

    /// Handles currently out. Not the same as tests in flight: a handle
    /// released this frame is still allocated until the frame ends.
    uint32_t held() const { return leased; }
    /// Tests dropped for want of a handle, since the view opened.
    uint32_t refusals() const { return refused; }

private:
    uint32_t leased = 0;
    uint32_t refused = 0;
};

/// docs/FarFieldProxies.md §10.1 measurement 1: how many of the
/// instances a frame draws could not have reached the screen.
///
/// The mechanism it measures is the one §10.1 proposes — occlusion
/// queries per *spatial-index node* rather than per object, the CHC++
/// shape over the hierarchy phase 1 already builds. So the readout is
/// not an estimate of a mechanism's yield; it is the mechanism, run
/// without acting on its answers.
///
/// Spread over frames because a GPU offers 256 queries at a time and a
/// large model partitions into thousands of nodes: one batch per frame,
/// results collected on a later frame, and a line printed when the
/// whole partition has been walked. A cycle therefore takes a handful
/// of frames, and nothing is reported until it is complete — a partial
/// walk would read as a scene with fewer nodes rather than as an
/// unfinished measurement.
struct OcclusionProbeState {
    /// Rebuilt per cycle, like the far-field cut's, and for the same
    /// reason: a measurement that can be stale measures the wrong
    /// thing. The build cost is reported.
    Render::ProxyHierarchy index;
    enum Verdict : uint8_t { Unknown, Offscreen, Occluded, Visible };
    std::vector<uint8_t> verdict;
    /// Nodes submitted last frame, awaiting their results.
    std::vector<int> pending;
    /// The handle each of those boxes was drawn under, created for that
    /// one test and destroyed when its answer is read. Parallel to
    /// `pending`. ⚠️ Held here rather than in a pool the walk reuses:
    /// reusing them is what made the wait below a no-op, since a handle
    /// that has answered once never reports `NoResult` again (see
    /// OcclusionLeases).
    std::vector<bgfx::OcclusionQueryHandle> handles;
    /// Frames spent waiting for the current batch. A query that never
    /// lands — a box the backend dropped, a device lost between submit
    /// and read — would otherwise stall the walk forever, and the walk
    /// is the only thing that ever prints the line.
    uint32_t waited = 0;
    /// Next node of the walk. Nodes the frustum already rejects never
    /// enter a batch — conflating them with occluded ones would credit
    /// occlusion culling with what frustum culling does today.
    uint32_t next = 0;
    bool active = false;
    bool unsupportedReported = false;
    double buildMs = 0.0;
    uint32_t batches = 0;
    uint32_t skippedBoxes = 0;   ///< transient buffers exhausted
    /// Nodes the near plane made unanswerable, counted as visible.
    uint32_t nearClipped = 0;
    /// Pixels each tested node contributed, bucketed. A node worth 3
    /// pixels is not worth its draws either, so the tail of this is the
    /// far-field question asked of the same partition.
    uint32_t pxHist[5] = {0, 0, 0, 0, 0};
    int64_t lastReport = 0;
    /// The camera the walk started against. A walk spans several
    /// frames, so a camera that moves partway through would have its
    /// early batches answered from one viewpoint and its late ones from
    /// another — and the line would report a scene that never existed.
    /// Restarting is the only honest response; a moving camera simply
    /// does not get a reading until it stops.
    float camera[32] = {};
    bool cameraValid = false;

    /// True when \a V and \a P match what the current walk started
    /// against; records them when there is no walk in progress.
    bool sameCamera(const float *V, const float *P)
    {
        float now[32];
        std::memcpy(now, V, 16 * sizeof(float));
        std::memcpy(now + 16, P, 16 * sizeof(float));
        if (cameraValid && std::memcmp(now, camera, sizeof(now)) == 0)
            return true;
        std::memcpy(camera, now, sizeof(camera));
        cameraValid = true;
        return false;
    }
};

/// Where the box batch is built. One per frame at most, so a plain
/// scratch pair rather than an allocation per batch.
struct OcclusionBoxBatch {
    std::vector<float> mins;
    std::vector<float> maxs;
    std::vector<int> nodes;
};

/// Walk the partition once and report. Returns true while a cycle is in
/// progress, so the caller can leave the pass claimed.
///
/// \a leases hands out the query handles: one per box, destroyed as its
/// answer is read, because a reused handle answers with the previous
/// test's verdict and the wait below would never notice.
static void driveOcclusionProbe(
        BGFXView &view, OcclusionProbeState &st, OcclusionBoxBatch &batch,
        OcclusionLeases &leases,
        const Render::DrawCallList &scene, const float *V, const float *P,
        float viewportHeightPx)
{
    // How long to leave the picture alone between cycles. The walk
    // itself costs a handful of frames of box rasterization; running it
    // back to back would make the readout a load rather than a probe.
    static const int64_t kQuietFrames = 3;
    // How long to wait for a batch before declaring it lost. Answers
    // land two frames after they are submitted; a batch that has not
    // answered in this many has not been drawn at all, and waiting for
    // it forever would stop the walk that prints the line.
    static const uint32_t kWaitFrames = 60;

    const bgfx::Caps *caps = bgfx::getCaps();
    if (!caps || !(caps->supported & BGFX_CAPS_OCCLUSION_QUERY)) {
        if (!st.unsupportedReported) {
            st.unsupportedReported = true;
            RENDER_ERR("render occlusion: this backend reports no occlusion "
                       "query support; nothing measured");
        }
        return;
    }

    // A quarter of the backend's handles, so that the culler — which
    // runs on the same pool and is the mechanism this measures — is not
    // starved by the measurement. Taking fewer per batch costs the walk
    // frames, never accuracy: a cycle is complete when every node has
    // been asked, however many batches that took.
    const uint32_t kBatch =
        std::max<uint32_t>(16, std::min<uint32_t>(
                256, caps->limits.maxOcclusionQueries / 4));

    // Collect the previous batch before anything else: the handles are
    // the resource, and one whose result has not been read is still
    // out. Draining first — ahead of the camera check that may abandon
    // this walk — is what lets an abandoned walk hand its handles back
    // instead of orphaning them.
    if (!st.pending.empty()) {
        for (size_t i = 0; i < st.pending.size(); ++i) {
            if (bgfx::getResult(st.handles[i])
                    != bgfx::OcclusionQueryResult::NoResult)
                continue;
            // Still in flight. ⭐ This wait is only real because the
            // handle was created for this one box: read on a reused
            // handle, `NoResult` never comes back after the first
            // cycle and the walk helps itself to the previous batch's
            // answers.
            if (++st.waited <= kWaitFrames)
                return;
            RENDER_ERR("render occlusion: a batch of " << st.pending.size()
                       << " boxes went unanswered for " << kWaitFrames
                       << " frames and was abandoned; no hidden share is "
                          "reported for this walk");
            for (auto q : st.handles)
                leases.release(q);
            st.handles.clear();
            st.pending.clear();
            st.active = false;
            st.waited = 0;
            st.lastReport = bx::getHPCounter();
            return;
        }
        for (size_t i = 0; i < st.pending.size(); ++i) {
            int32_t px = 0;
            const auto r = bgfx::getResult(st.handles[i], &px);
            leases.release(st.handles[i]);
            if (!st.active)
                continue;   // results of a walk that has been abandoned
            const int node = st.pending[i];
            st.verdict[size_t(node)] =
                (r == bgfx::OcclusionQueryResult::Visible && px > 0)
                    ? OcclusionProbeState::Visible
                    : OcclusionProbeState::Occluded;
            const uint32_t bucket = px <= 0 ? 0
                                  : px <= 4 ? 1
                                  : px <= 64 ? 2
                                  : px <= 1024 ? 3 : 4;
            ++st.pxHist[bucket];
        }
        st.handles.clear();
        st.pending.clear();
        st.waited = 0;
    }

    // A camera that moved abandons the walk rather than finishing it
    // against a different viewpoint: its early batches were answered
    // from one place and its late ones would be answered from another,
    // and the line would describe a scene that never existed. A moving
    // camera simply gets no reading until it stops.
    if (!st.sameCamera(V, P))
        st.active = false;

    if (!st.active) {
        const int64_t now = bx::getHPCounter();
        if (st.lastReport
                && now - st.lastReport < kQuietFrames * bx::getHPFrequency())
            return;
        const int64_t started = now;
        std::vector<Render::ProxyInstance> instances;
        Render::proxyInstances(scene, instances);
        if (instances.empty())
            return;
        st.index.build(instances);
        st.buildMs = 1000.0 * double(bx::getHPCounter() - started)
                     / double(bx::getHPFrequency());
        st.verdict.assign(st.index.nodes().size(), OcclusionProbeState::Unknown);
        st.pending.clear();
        st.next = 0;
        st.batches = 0;
        st.skippedBoxes = 0;
        st.nearClipped = 0;
        std::memset(st.pxHist, 0, sizeof(st.pxHist));
        st.active = true;
    }

    // Fill the next batch, skipping what the frustum already rejects.
    const auto &nodes = st.index.nodes();
    batch.mins.clear();
    batch.maxs.clear();
    batch.nodes.clear();
    while (st.next < nodes.size() && batch.nodes.size() < kBatch) {
        const int node = int(st.next++);
        const Render::ProxyNode &n = nodes[size_t(node)];
        const auto sight = Render::sightBounds(n.contentMin, n.contentMax, V, P,
                                               viewportHeightPx);
        if (sight.what == Render::BoxSight::Offscreen) {
            st.verdict[size_t(node)] = OcclusionProbeState::Offscreen;
            continue;
        }
        if (sight.what == Render::BoxSight::Empty) {
            st.verdict[size_t(node)] = OcclusionProbeState::Visible;
            continue;
        }
        // ⚠️ The box is padded outwards, and the measurement does not
        // work without it.
        //
        // A node's bounds are the union of its contents' bounds, so a
        // face of the box coincides *exactly* with a real surface
        // whenever some part has a flat face at its own extreme — in
        // CAD not an edge case but the common one: panels, plates,
        // brackets, a chassis wall. Rasterized at equal depth the two
        // disagree in the last bit, and where the box loses, LEQUAL
        // rejects every fragment and the node reports itself hidden
        // while its contents are in plain view. A test box has to be a
        // conservative bound, and float equality is not conservative.
        //
        // Relative to the box's own diagonal, so it means the same at
        // any model scale.
        //
        // ⚠️⚠️ And padded by the depth buffer's own resolution as well,
        // which the relative term cannot supply: a small part flush on
        // a large panel has a small diagonal, so a small pad, at a
        // distance where one depth step dwarfs it. Both quantize to the
        // same stored value and LEQUAL loses the tie. The probe shares
        // this with the culler on purpose — a measurement that answers
        // a different question than the mechanism it justifies is worth
        // nothing (§12.6).
        float pmin[3], pmax[3];
        const float dx = n.contentMax[0] - n.contentMin[0];
        const float dy = n.contentMax[1] - n.contentMin[1];
        const float dz = n.contentMax[2] - n.contentMin[2];
        const Render::OcclusionCullConfig defaults;
        const float pad = defaults.padFraction
                * std::sqrt(dx * dx + dy * dy + dz * dz)
            + Render::depthQuantumPad(n.contentMin, n.contentMax, V, P,
                                      caps->homogeneousDepth,
                                      defaults.depthPadLsb);
        for (int k = 0; k < 3; ++k) {
            pmin[k] = n.contentMin[k] - pad;
            pmax[k] = n.contentMax[k] + pad;
        }

        // A box the near plane clips cannot be tested at all: the faces
        // that would prove it visible are gone, and the ones that
        // remain are hidden by the box's own contents, so the query
        // returns nothing and the box reports itself hidden however
        // plainly it is in view. Judged on the *padded* box, since that
        // is what gets rasterized — and padding a box whose front face
        // already sits on the near plane is exactly what pushes it
        // through.
        if (sight.what == Render::BoxSight::Inside
                || Render::boxReachesNearPlane(pmin, pmax, V, P,
                                               caps->homogeneousDepth)) {
            st.verdict[size_t(node)] = OcclusionProbeState::Visible;
            ++st.nearClipped;
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            batch.mins.push_back(pmin[k]);
            batch.maxs.push_back(pmax[k]);
        }
        batch.nodes.push_back(node);
    }

    if (!batch.nodes.empty()) {
        st.handles.clear();
        while (st.handles.size() < batch.nodes.size()) {
            bgfx::OcclusionQueryHandle q = leases.acquire();
            if (!bgfx::isValid(q))
                break;
            st.handles.push_back(q);
        }
        const uint32_t n = uint32_t(st.handles.size());
        const uint32_t sent = n
            ? view.submitOcclusionBoxes(batch.mins.data(), batch.maxs.data(),
                                        n, st.handles)
            : 0;
        // Boxes the batch could not send keep the Unknown verdict; the
        // report names them rather than counting them as either side.
        st.skippedBoxes += uint32_t(batch.nodes.size()) - sent;
        ++st.batches;
        for (uint32_t i = 0; i < sent; ++i)
            st.pending.push_back(batch.nodes[i]);
        // A handle whose box was not drawn will never be answered, and
        // holding it would leak one out of the pool per cycle.
        for (size_t i = sent; i < st.handles.size(); ++i)
            leases.release(st.handles[i]);
        st.handles.resize(sent);
        st.waited = 0;
        return;
    }

    // The walk is done: descend the partition once and attribute every
    // instance to the highest node that rejects it. Descending rather
    // than summing per node is the whole point — culling a node culls
    // its subtree, so counting occluded nodes independently would count
    // the same instances at every level they are hidden at.
    // A root the query calls hidden cannot be true: its box contains
    // every drawn thing, so if the frame put a single pixel on the
    // screen the root is visible. When it happens the box test is not
    // answering — and the descent below would faithfully turn that into
    // "100% hidden", a number that looks like a spectacular result and
    // is nothing of the kind. Say so instead; a measurement that cannot
    // report a failure is not a measurement.
    if (st.index.root() != Render::kNoProxyNode
            && st.verdict[size_t(st.index.root())]
                   == OcclusionProbeState::Occluded) {
        RENDER_ERR("render occlusion: the whole-model box tested as hidden, "
                   "which cannot be true while the frame draws anything -- "
                   "the box test is not answering for this camera and no "
                   "hidden share is reported");
        st.active = false;
        st.lastReport = bx::getHPCounter();
        return;
    }

    uint64_t offscreenInstances = 0;
    uint64_t occludedInstances = 0;
    uint64_t visibleInstances = 0;
    uint32_t occludedNodes = 0;
    uint32_t testedNodes = 0;
    uint32_t offscreenNodes = 0;
    std::vector<int> stack;
    if (st.index.root() != Render::kNoProxyNode)
        stack.push_back(st.index.root());
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        const Render::ProxyNode &n = nodes[size_t(node)];
        const uint8_t vd = st.verdict[size_t(node)];
        if (vd == OcclusionProbeState::Offscreen) {
            offscreenInstances += n.subtreeCount;
            ++offscreenNodes;
            continue;
        }
        if (vd == OcclusionProbeState::Occluded) {
            occludedInstances += n.subtreeCount;
            ++occludedNodes;
            continue;
        }
        // Visible (or untested): its own residents draw, and the
        // question is asked again of each child.
        visibleInstances += n.residentCount;
        for (int c : n.child) {
            if (c != Render::kNoProxyNode)
                stack.push_back(c);
        }
    }
    for (uint8_t vd : st.verdict) {
        if (vd == OcclusionProbeState::Visible || vd == OcclusionProbeState::Occluded)
            ++testedNodes;
    }

    // Per level, because a single hidden share cannot be read without
    // knowing where in the partition it was decided. A model whose root
    // is called hidden and a model whose leaves are each called hidden
    // print the same headline and mean entirely different things — the
    // first is a broken box test, the second is what culling is for.
    struct LevelTally { uint32_t nodes = 0, tested = 0, zeroPx = 0; };
    std::vector<LevelTally> byLevel;
    for (size_t n = 0; n < nodes.size(); ++n) {
        const uint32_t lvl = nodes[n].level;
        if (byLevel.size() <= lvl)
            byLevel.resize(lvl + 1);
        ++byLevel[lvl].nodes;
        const uint8_t vd = st.verdict[n];
        if (vd == OcclusionProbeState::Visible
                || vd == OcclusionProbeState::Occluded) {
            ++byLevel[lvl].tested;
            if (vd == OcclusionProbeState::Occluded)
                ++byLevel[lvl].zeroPx;
        }
    }
    std::string levels;
    for (size_t l = 0; l < byLevel.size(); ++l) {
        if (!byLevel[l].nodes)
            continue;
        char lb[96];
        snprintf(lb, sizeof(lb), " L%u:%u/%u hidden%u", unsigned(l),
                 byLevel[l].tested, byLevel[l].nodes, byLevel[l].zeroPx);
        levels += lb;
    }

    const auto stats = st.index.stats();
    const double total = double(stats.instances);
    char buf[640];
    snprintf(buf, sizeof(buf),
             "render occlusion: instances:%u | hidden %llu (%.1f%%) "
             "offscreen %llu (%.1f%%) drawn %llu (%.1f%%) | nodes %u tested %u "
             "occluded %u offscreen %u | node px 0:%u <=4:%u <=64:%u "
             "<=1k:%u more:%u | batches %u nearclip %u skipped %u | "
             "build %.1fms\n",
             stats.instances, (unsigned long long)occludedInstances,
             total > 0 ? 100.0 * double(occludedInstances) / total : 0.0,
             (unsigned long long)offscreenInstances,
             total > 0 ? 100.0 * double(offscreenInstances) / total : 0.0,
             (unsigned long long)visibleInstances,
             total > 0 ? 100.0 * double(visibleInstances) / total : 0.0,
             stats.nodes, testedNodes, occludedNodes, offscreenNodes,
             st.pxHist[0], st.pxHist[1], st.pxHist[2], st.pxHist[3],
             st.pxHist[4], st.batches, st.nearClipped, st.skippedBoxes,
             st.buildMs);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
    std::printf("render occlusion levels:%s\n", levels.c_str());
#else
    Base::Console().Message("%s", buf);
    Base::Console().Message("render occlusion levels:%s\n", levels.c_str());
#endif
    st.active = false;
    st.lastReport = bx::getHPCounter();
}

/// Project the draw list into the table the culler partitions.
///
/// Not `proxyInstances()` alone, because two kinds of draw are not
/// judgeable by the bounds they carry, and both are exactly the ones the
/// frustum cull already exempts (BGFXRenderer.cpp, "Frustum culling"):
///
/// - **autozoom** draws rebuild their model matrix per frame, so the fed
///   bounds describe where the draw *was*;
/// - **emitters** draw outside their own bounds, which is what
///   drawHeadroom() measures.
///
/// The emitter case could be padded rather than dropped, but an emitter
/// is one draw and the padding would be a guess; dropping it costs one
/// draw and cannot be wrong. Rows that are dropped are simply absent
/// from the index and therefore never masked — `drawIndex` still refers
/// to the original draw list, so the mask stays aligned.
///
/// ⚠️⚠️ **On-top draws are excluded, and leaving them in was measured to
/// delete visible geometry.** An on-top draw is defined by ignoring the
/// depth test: it is drawn over whatever is in front of it, so it is
/// visible *however occluded its geometry is*. A depth-buffer occlusion
/// test answers the question "is this geometry behind something", which
/// for on-top draws is true and irrelevant — and acting on it removes
/// something the eye can plainly see. Culling them cost 1.5% of the
/// pixels of a whole-assembly view of the server model, concentrated in
/// exactly the structure an engineer is looking at.
static void cullInstances(const Render::DrawCallList &scene,
                          std::vector<Render::ProxyInstance> &out,
                          uint32_t *exemptOnTop = nullptr)
{
    Render::proxyInstances(scene, out);
    uint32_t ontop = 0;
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](const Render::ProxyInstance &inst) {
                                 if (inst.drawIndex >= scene.size())
                                     return true;
                                 const auto &d = scene[inst.drawIndex];
                                 if (d.material.ontop) {
                                     ++ontop;
                                     return true;
                                 }
                                 return !d.material.autozoom.empty()
                                     || drawHeadroom(d) > 0.0f;
                             }),
              out.end());
    if (exemptOnTop)
        *exemptOnTop = ontop;
}

/// One occlusion test in flight: the node it asks about, the handle
/// created for it, and the frame it was issued on.
struct OcclusionTestLease {
    bgfx::OcclusionQueryHandle handle = BGFX_INVALID_HANDLE;
    int node = -1;
    uint32_t issued = 0;
};

/// The culler's occlusion queries: the handles it holds and the tests
/// they stand for.
struct OcclusionCullQueries {
    OcclusionLeases leases;
    /// Tests submitted and not yet answered, in issue order.
    std::vector<OcclusionTestLease> inflight;
    /// Scratch for one round's handles, kept across frames so that
    /// issuing a round allocates nothing.
    std::vector<bgfx::OcclusionQueryHandle> scratch;
    /// Tests whose answer never arrived, since the view opened.
    uint32_t expired = 0;
    /// Boxes actually submitted last frame. Distinct from the tests the
    /// walk offered and from the ones it had budget for: a handle the
    /// backend would not give out, or a transient buffer that ran short,
    /// shows up only here.
    uint32_t lastSent = 0;

    void releaseAll()
    {
        for (const auto &test : inflight)
            leases.release(test.handle);
        inflight.clear();
    }
};

/// How long a test may go unanswered before its lease is torn up.
///
/// An answer lands two frames after its box is submitted. One that never
/// lands — a box the backend dropped, a device lost between submit and
/// read — would otherwise hold its handle forever and leave its node
/// marked pending, which is the one state the walk never re-offers. The
/// node would then be left to `maxHiddenFrames`, the fail-safe for
/// answers that stop arriving, and would sit hidden until it fired.
/// Expiring the lease first turns a lost query back into an ordinary
/// un-answered node, re-offered on the next walk. ⚠️ This became
/// necessary with the leases: while handles were pooled and reused, a
/// query that never landed still read as answered — with somebody
/// else's answer — so nothing ever looked stuck.
static const uint32_t kCullLeaseFrames = 16;

/// One frame of occlusion culling: read the answers the last frame's
/// tests have produced, walk the index against this camera, mask what
/// cannot be seen, and issue the next round of tests.
///
/// The masking is *additive* into \a cullMask — the frustum's rejections
/// are already in it — and the mask is consulted only by the eye passes.
/// That is what keeps the culling per pass rather than per frame, which
/// §10.4 requires: shadow casters deliberately re-submit culled draws
/// (off-screen geometry still casts into view) and the mirrored ground
/// reflection never consults the mask at all, so neither inherits a
/// verdict taken from the eye.
static void driveOcclusionCull(
        BGFXView &view, Render::OcclusionCuller &culler,
        Render::OcclusionTestBatch &batch, OcclusionCullQueries &queries,
        const float *V, const float *P,
        float viewportHeightPx, std::vector<uint8_t> &cullMask,
        std::vector<int32_t> *cullOwner)
{
    const bgfx::Caps *caps = bgfx::getCaps();
    if (!caps)
        return;

    // 1. Collect what the last rounds asked. Non-blocking by design: a
    // query whose frame has not landed keeps its lease and is read next
    // time. Stalling on it would trade the frame time this is meant to
    // save for a pipeline bubble, and the node it belongs to simply
    // keeps whatever it already believed.
    //
    // ⭐ `NoResult` carries the whole distinction between "not answered
    // yet" and "answered", and it only tells the truth because the
    // handle was created for this one test — see OcclusionLeases.
    const uint32_t now = culler.frame();
    size_t kept = 0;
    for (size_t i = 0; i < queries.inflight.size(); ++i) {
        const OcclusionTestLease test = queries.inflight[i];
        int32_t px = 0;
        const auto r = bgfx::getResult(test.handle, &px);
        if (r == bgfx::OcclusionQueryResult::NoResult) {
            if (now - test.issued < kCullLeaseFrames) {
                queries.inflight[kept++] = test;
                continue;
            }
            ++queries.expired;
            queries.leases.release(test.handle);
            culler.abandon(test.node);
            continue;
        }
        culler.result(test.node,
                      r == bgfx::OcclusionQueryResult::Visible && px > 0, px);
        queries.leases.release(test.handle);
    }
    queries.inflight.resize(kept);

    // 2. Decide what this camera draws.
    culler.cull(V, P, viewportHeightPx, caps->homogeneousDepth, cullMask,
                batch, cullOwner);
    queries.lastSent = 0;
    if (batch.nodes.empty())
        return;

    // 3. Issue the next round, one handle created for each test.
    //
    // Never taking more than half the backend's pool leaves room for the
    // measurement probe, which runs on the same handles, and for the
    // frame's released ones, which bgfx does not free until the frame
    // ends. The walk already caps the batch at `budget` per frame; this
    // bounds the *accumulation*, for the case where answers stop coming
    // and every eligible node ends up in flight at once.
    const uint32_t ceiling = std::max<uint32_t>(
            1, caps->limits.maxOcclusionQueries / 2);
    auto &handles = queries.scratch;
    handles.clear();
    while (handles.size() < batch.nodes.size()
            && queries.leases.held() < ceiling) {
        bgfx::OcclusionQueryHandle q = queries.leases.acquire();
        if (!bgfx::isValid(q))
            break;
        handles.push_back(q);
    }
    // The boxes are contiguous and the batch is offered in priority
    // order — hidden nodes first, since a test is the only way one can
    // come back — so submitting a prefix drops the least important.
    const uint32_t sent = handles.empty()
        ? 0
        : view.submitOcclusionBoxes(batch.mins.data(), batch.maxs.data(),
                                    uint32_t(handles.size()), handles);
    queries.lastSent = sent;
    const uint32_t issued = culler.frame();
    for (uint32_t i = 0; i < sent; ++i)
        queries.inflight.push_back({handles[i], batch.nodes[i], issued});
    // A handle whose box was not drawn will never be answered; holding
    // it would take one out of the pool per frame.
    for (size_t i = sent; i < handles.size(); ++i)
        queries.leases.release(handles[i]);
    // Everything not sent must be told, or it stays marked pending and
    // is never offered again — for a hidden node that would mean it
    // could only return via the starvation fail-safe.
    for (size_t i = sent; i < batch.nodes.size(); ++i)
        culler.abandon(batch.nodes[i]);
}

/// Whether the once-a-second cull audit is due. Split from the report
/// like the far-field readouts, and for the same reason: what it gates
/// is a full-resolution transfer off the GPU, which must not be paid on
/// the frames that print nothing.
static bool cullAuditDue()
{
    static int64_t lastReport = 0;
    const int64_t now = bx::getHPCounter();
    const int64_t freq = bx::getHPFrequency();
    if (lastReport && now - lastReport < freq)
        return false;
    lastReport = now;
    return true;
}

/// ⭐⭐ What a tighter occludee volume would have culled
/// (docs/FarFieldProxies.md §12.19). A **diagnostic**, not a mechanism:
/// it decides whether the mechanism is worth building.
///
/// Section 12.17 left 90% of what is still drawn reaching no pixel while
/// each of those draws had been tested individually and answered
/// visible — so the boxes are not covered although the geometry is. That
/// has three candidate explanations with wildly different prices, and
/// this measures all three against the same frame:
///
/// - **world AABB** (`aabbMask`, what ships today) — the control;
/// - **OBB corners** (`obbMask`) — the mesh's *local* box through the
///   model matrix. A world AABB is the axis-aligned box of an oriented
///   box for any rotated part, inflated twice; this is the cheap fix,
///   and on an axis-aligned model it is expected to buy nothing;
/// - **per triangle** (`triMask`) — every triangle of the draw asked
///   separately, the draw counted hidden only when none of them reaches
///   a pixel. ⚠️ Not a shippable mechanism (§12.17's closing paragraph
///   costed it at 9.75M triangles of query rasterization) but it is the
///   **ceiling**: no occludee-side refinement against this occluder set
///   can beat asking about the geometry itself.
///
/// KEY: Each arm is monotone in the one above it — a triangle's hull
/// lies inside the OBB, whose hull lies inside the AABB, and `testRect`
/// answers Occluded for a subset rect at a no-nearer depth whenever it
/// does for the enclosing one. So an arm that culls nothing its
/// predecessor did not is a *proven* dead end and not an unlucky sample.
///
/// The masks are handed to the audit rather than acted on, and there
/// intersected with the id image: a row that would flip and owns no
/// pixel is the prize, a row that would flip and owns pixels is
/// over-cull the arm would have introduced. The second number is why
/// this is measurable at all — the arms are hypotheses, and the id image
/// is the only thing in the renderer that can call one wrong.
/// ⚠️ The control arm is not optional. A row reaching this diagnostic
/// survived the cull, but "survived" covers two different things: it was
/// tested with its world AABB and answered visible, or it was **never
/// asked** (an on-top draw, an exempt row, anything the hierarchy does
/// not hold). Without re-running the world AABB here, both would be
/// counted as the tighter arms' winnings, and a coverage gap would be
/// published as a tightness result — the §12.16 mistake in a new place:
/// reading a number as evidence for the mechanism you happen to be
/// building. `aabbCull` is that number, and it belongs to neither arm.
struct TightBoundAudit {
    uint32_t judged = 0;      ///< drawn rows the arms could answer for
    /// ⚠️⚠️ Rows the arms were never asked about, and why. The first run
    /// of this diagnostic judged 2803 of 8388 drawn rows and divided its
    /// result by all 7574 invisible ones — reporting a 3.0% ceiling for
    /// a mechanism that had not been offered two thirds of the problem.
    /// A CAD scene draws each object as fills *and* edges, so the line
    /// draws are not a rounding error here; they are the majority.
    uint32_t skippedPoint = 0;   ///< point sprites: a vertex is not a footprint
    uint32_t skippedNoMesh = 0;  ///< no geometry to ask about at all
    uint32_t capped = 0;      ///< rows too big for the per-primitive arm
    uint32_t aabbCull = 0;    ///< rows the shipping world box would cull
    uint32_t obbCull = 0;     ///< rows the OBB arm would cull
    uint32_t triCull = 0;     ///< rows the per-triangle arm would cull
    uint32_t obbOffscreen = 0;   ///< of obbCull, decided by the rect, not depth
    uint32_t triOffscreen = 0;   ///< of triCull, ditto
    uint64_t primitives = 0;  ///< primitives the per-primitive arm asked about
    float ms = 0.0f;
};

/// The local-space bounds of the vertices a draw actually references.
///
/// KEY: The draw's range, not the whole mesh. A part draw (`indexStart`/
/// `indexCount`) covers a face of a mesh whose world box in `DrawCall`
/// is already that face's, so bounding the *whole* mesh here would make
/// the "tight" arm looser than the control it is being compared against
/// — the one way this measurement could report a negative result that is
/// an artefact of its own arithmetic.
static bool drawLocalBounds(const Render::MeshData &m, const int32_t *indices,
                            size_t first, size_t count, float *lo, float *hi)
{
    lo[0] = lo[1] = lo[2] = FLT_MAX;
    hi[0] = hi[1] = hi[2] = -FLT_MAX;
    bool any = false;
    for (size_t i = 0; i < count; ++i) {
        const int32_t vi = indices[first + i];
        if (vi < 0 || vi >= m.numVertices)
            continue;
        const float *p = m.positions + size_t(vi) * 3;
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], p[k]);
            hi[k] = std::max(hi[k], p[k]);
        }
        any = true;
    }
    return any;
}

/// Take the three arms over every row the cull left drawn.
///
/// Runs on the audit's frame only (once a second, and only while
/// RenderDebug_CullBounds is on): the per-triangle arm is deliberately
/// far too expensive to ship, so it must never be on a path a normal
/// frame takes.
static void auditTightBounds(const Render::DrawCallList &scene,
                             const std::vector<uint8_t> &culled,
                             const Render::MaskedDepth &depth,
                             uint32_t primitiveCap, uint32_t threads,
                             std::vector<uint8_t> &judgedMask,
                             std::vector<uint8_t> &aabbMask,
                             std::vector<uint8_t> &obbMask,
                             std::vector<uint8_t> &triMask,
                             TightBoundAudit &out)
{
    const auto t0 = bx::getHPCounter();
    judgedMask.assign(scene.size(), 0);
    aabbMask.assign(scene.size(), 0);
    obbMask.assign(scene.size(), 0);
    triMask.assign(scene.size(), 0);

    uint32_t workers = Render::occluderWorkers(threads);
    if (scene.size() < 256)
        workers = 1;
    std::vector<TightBoundAudit> tally(workers);

    auto work = [&](uint32_t w) {
        TightBoundAudit &t = tally[w];
        for (size_t i = w; i < scene.size(); i += workers) {
            if (i < culled.size() && culled[i])
                continue;  // already skipped; nothing left to win here
            const Render::DrawCall &d = scene[i];
            if (!d.mesh || !d.mesh->positions || d.mesh->numVertices < 1) {
                ++t.skippedNoMesh;
                continue;
            }
            // ⭐ Whichever primitive this draw actually renders. A CAD
            // frame submits an object's edges as well as its faces, and
            // an edge draw is a draw: it costs the same submission, it
            // is hidden by the same walls, and a segment is a far
            // tighter thing to ask about than the box around a whole
            // wireframe. Asking only about triangles measured a third
            // of the problem and reported the answer as all of it.
            const int32_t *indices = nullptr;
            size_t total = 0;
            size_t per = 3;
            switch (d.material.type) {
            case Render::Material::Triangle:
                indices = d.mesh->triangleIndices;
                total = size_t(std::max(0, d.mesh->numTriangleIndices));
                per = 3;
                break;
            case Render::Material::Line:
                indices = d.mesh->lineIndices;
                total = size_t(std::max(0, d.mesh->numLineIndices));
                per = 2;
                break;
            default:
                // ⚠️ A point draw is a *sprite*: it covers pixels around
                // its vertex, and the vertex alone is not that footprint.
                // Asking about it would be the one arm here that can
                // answer hidden for something on screen.
                ++t.skippedPoint;
                continue;
            }
            if (!indices || total < per) {
                ++t.skippedNoMesh;
                continue;
            }
            // The same resolution the occluder pass uses.
            size_t first = size_t(std::max(0, d.indexStart));
            size_t count = d.indexCount > 0 ? size_t(d.indexCount) : total;
            if (first > total)
                first = total;
            if (first + count > total)
                count = total - first;
            if (count < per) {
                ++t.skippedNoMesh;
                continue;
            }
            ++t.judged;
            judgedMask[i] = 1;

            // The control: the box that ships, asked again here. A row
            // it culls was never asked in the first place, and nothing
            // below may take credit for it.
            if (d.bboxMin[0] <= d.bboxMax[0]
                && depth.testBoxConcurrent(d.bboxMin, d.bboxMax)
                        == Render::OccludeAnswer::Occluded) {
                aabbMask[i] = 1;
                ++t.aabbCull;
            }

            float lo[3], hi[3];
            if (!drawLocalBounds(*d.mesh, indices, first, count, lo, hi)) {
                ++t.skippedNoMesh;
                --t.judged;
                judgedMask[i] = 0;
                continue;
            }
            // The eight corners of the tight *local* box, left in local
            // space and taken to world by the buffer itself: an oriented
            // box, where DrawCall::bboxMin/Max is the axis-aligned box
            // drawn around it.
            const float *model = d.identity ? nullptr : d.model;
            float corners[8 * 3];
            for (int c = 0; c < 8; ++c) {
                corners[c * 3 + 0] = (c & 1) ? hi[0] : lo[0];
                corners[c * 3 + 1] = (c & 2) ? hi[1] : lo[1];
                corners[c * 3 + 2] = (c & 4) ? hi[2] : lo[2];
            }
            const Render::OccludeAnswer obb =
                    depth.testPointsConcurrent(corners, 8, model);
            const bool obbHides = obb == Render::OccludeAnswer::Occluded
                    || obb == Render::OccludeAnswer::Offscreen;
            if (obbHides) {
                obbMask[i] = 1;
                ++t.obbCull;
                if (obb == Render::OccludeAnswer::Offscreen)
                    ++t.obbOffscreen;
                // Monotone: every triangle is inside this box, so the
                // finer arm cannot disagree. Skipping the loop here is
                // not an approximation, and it is most of what makes the
                // diagnostic affordable.
                triMask[i] = 1;
                ++t.triCull;
                if (obb == Render::OccludeAnswer::Offscreen)
                    ++t.triOffscreen;
                continue;
            }

            const size_t prims = count / per;
            if (prims > primitiveCap) {
                ++t.capped;
                continue;
            }
            // Every primitive asked separately. A draw is hidden only if
            // none of them reaches a pixel; the first one that does ends
            // the row, which is why a *visible* draw is cheap here and
            // only the invisible ones — the ones being counted — pay in
            // full.
            bool hidden = true, sawDepth = false;
            for (size_t prim = 0; prim < prims && hidden; ++prim) {
                float p[9];
                bool ok = true;
                for (size_t k = 0; k < per; ++k) {
                    const int32_t vi = indices[first + prim * per + k];
                    if (vi < 0 || vi >= d.mesh->numVertices) {
                        ok = false;
                        break;
                    }
                    const float *src = d.mesh->positions + size_t(vi) * 3;
                    p[k * 3 + 0] = src[0];
                    p[k * 3 + 1] = src[1];
                    p[k * 3 + 2] = src[2];
                }
                ++t.primitives;
                if (!ok)
                    continue;  // a degenerate index reaches no pixel either
                const Render::OccludeAnswer a =
                        depth.testPointsConcurrent(p, per, model);
                if (a == Render::OccludeAnswer::Occluded)
                    sawDepth = true;
                else if (a != Render::OccludeAnswer::Offscreen)
                    hidden = false;
            }
            if (hidden) {
                triMask[i] = 1;
                ++t.triCull;
                if (!sawDepth)
                    ++t.triOffscreen;
            }
        }
    };

    if (workers == 1) {
        work(0);
    }
    else {
        std::vector<std::thread> pool;
        pool.reserve(workers - 1);
        for (uint32_t w = 1; w < workers; ++w)
            pool.emplace_back(work, w);
        work(0);
        for (auto &th : pool)
            th.join();
    }
    for (const TightBoundAudit &t : tally) {
        out.judged += t.judged;
        out.skippedPoint += t.skippedPoint;
        out.skippedNoMesh += t.skippedNoMesh;
        out.capped += t.capped;
        out.obbCull += t.obbCull;
        out.triCull += t.triCull;
        out.obbOffscreen += t.obbOffscreen;
        out.triOffscreen += t.triOffscreen;
        out.primitives += t.primitives;
    }
    out.ms = float(1000.0 * double(bx::getHPCounter() - t0)
                   / double(bx::getHPFrequency()));
}

/// Read the tight-bound arms against the id image.
///
/// \a hist is reportCullAudit's histogram (id = row + 1), so a drawn row
/// owning no pixel is one the frame paid for and could not see. Each arm
/// splits that set two ways, and both halves matter:
///
/// - **prize** — rows the arm would cull that own no pixel: real work
///   removed, and the reason to build it;
/// - **RISK** — rows the arm would cull that own pixels: geometry it
///   would have deleted from the screen. It must be zero. An arm is
///   only conservative on paper until the id image has been asked, and
///   this workstream has twice shipped a box test that answered
///   "hidden" for things that were plainly visible (§12.6, §12.10).
static void reportTightBounds(const std::vector<uint32_t> &hist,
                              const std::vector<uint8_t> &mask,
                              const std::vector<uint8_t> &judgedMask,
                              const std::vector<uint8_t> &aabbMask,
                              const std::vector<uint8_t> &obbMask,
                              const std::vector<uint8_t> &triMask,
                              const TightBoundAudit &t)
{
    if (hist.empty() || obbMask.empty())
        return;
    const size_t rows = hist.size() - 1;
    size_t drawn = 0, invisible = 0;
    // ⚠️⚠️ THE DENOMINATOR THE CEILING IS DIVIDED BY. Not every invisible
    // row was offered to the arms, and dividing by the ones that were
    // not is how a diagnostic reports a mechanism as weak when it was
    // simply never asked. The first run of this made exactly that
    // mistake and quoted 3.0% off a third of the rows.
    size_t judgedInvisible = 0;
    size_t aabbPrize = 0, aabbRisk = 0;
    size_t obbPrize = 0, obbRisk = 0, triPrize = 0, triRisk = 0;
    // What each arm wins *over the control*, which is the only figure
    // that is about the bound rather than about coverage.
    size_t obbOver = 0, triOver = 0;
    for (size_t i = 0; i < rows; ++i) {
        if (i < mask.size() && mask[i])
            continue;
        ++drawn;
        const bool blind = hist[i + 1] == 0;
        if (blind)
            ++invisible;
        if (blind && i < judgedMask.size() && judgedMask[i])
            ++judgedInvisible;
        const bool ctrl = i < aabbMask.size() && aabbMask[i];
        if (ctrl)
            (blind ? aabbPrize : aabbRisk) += 1;
        if (i < obbMask.size() && obbMask[i]) {
            (blind ? obbPrize : obbRisk) += 1;
            if (blind && !ctrl)
                ++obbOver;
        }
        if (i < triMask.size() && triMask[i]) {
            (blind ? triPrize : triRisk) += 1;
            if (blind && !ctrl)
                ++triOver;
        }
    }

    char buf[1200];
    snprintf(buf, sizeof(buf),
             "render tight-bound audit: %zu drawn, %zu invisible (%zu of them "
             "judged) | control world AABB cull %u (%zu prize + %zu RISK) = "
             "rows never asked | OBB corners cull %u (%zu prize + %zu RISK, %u "
             "offscreen), %zu over control | per-primitive cull %u (%zu prize "
             "+ %zu RISK, %u offscreen), %zu over control | ceiling %.1f%% of "
             "judged invisible | judged %u skipped %u (%u points, %u no mesh) "
             "capped %u, %llu prims in %.1f ms\n",
             drawn, invisible, judgedInvisible,
             t.aabbCull, aabbPrize, aabbRisk,
             t.obbCull, obbPrize, obbRisk, t.obbOffscreen, obbOver,
             t.triCull, triPrize, triRisk, t.triOffscreen, triOver,
             judgedInvisible
                     ? 100.0 * double(triOver) / double(judgedInvisible)
                     : 0.0,
             t.judged, t.skippedPoint + t.skippedNoMesh,
             t.skippedPoint, t.skippedNoMesh, t.capped,
             (unsigned long long)t.primitives, t.ms);
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif
    if (aabbRisk || obbRisk || triRisk) {
        RENDER_ERR("render tight-bound audit: an arm would have culled a "
                   "draw that owns pixels. The arm is not conservative, or "
                   "its bound is not the geometry's -- its prize count is "
                   "not usable until this is zero.");
    }
}

/// ⭐⭐ Check the occlusion culling against the geometry it stands for
/// (docs/FarFieldProxies.md §12.9).
///
/// \a pixels is the id image as it was rendered with the mask ignored,
/// \a mask and \a keys the verdict and the draw identities as they were
/// at that moment. A pixel's id is the draw that won the depth test
/// there, so `owns a pixel` ∩ `masked` is a set of *proven* errors —
/// not pixels that differ between two pictures, which is a symptom that
/// names nothing, but named draws that the frame would have shown and
/// the culling removed.
///
/// The converse falls out of the same histogram and is worth as much:
/// rows that were drawn and own no pixel at all are the work the
/// culling has not yet found.
/// \a owner and \a nodes carry the attribution (§12.10): which node's
/// verdict cut each masked row, and what every node's state was at the
/// moment the image was drawn. Both may be empty — occlusion culling
/// off, or the audit turned on mid-flight — in which case the second
/// line is not printed rather than printed with nothing in it.
static void reportCullAudit(const std::vector<uint16_t> &pixels,
                            uint16_t w, uint16_t h,
                            const std::vector<uint8_t> &mask,
                            const std::vector<int32_t> &owner,
                            const std::vector<Render::OcclusionNodeAudit> &nodes,
                            const std::vector<uint64_t> &keys,
                            const Render::ObjectInfoMap &names,
                            std::vector<uint32_t> &hist)
{
    const size_t rows = keys.size();
    const size_t npx = size_t(w) * size_t(h);
    // Emptied before anything can fail: the histogram outlives the call
    // (reportTightBounds reads it next), and a reader cannot tell a
    // stale one, or an all-zero one from a broken id pass, from a frame
    // in which nothing reached the screen. Empty says "no answer here".
    hist.clear();
    if (!rows || pixels.size() < npx * 4)
        return;
    hist.assign(rows + 1, 0u);
    size_t covered = 0, outOfRange = 0;
    for (size_t p = 0; p < npx; ++p) {
        const uint16_t *px = &pixels[p * 4];
        // .w = 1 marks a fragment; the target clears to zero, so a
        // background pixel is not merely id 0 but uncovered.
        if (bx::halfToFloat(px[3]) < 0.5f)
            continue;
        ++covered;
        // Raw byte lanes, rounded rather than truncated: fp16 carries
        // 0..255 exactly, so any rounding here is the *absence* of a
        // bug rather than a tolerance.
        const uint32_t id =
              uint32_t(bx::halfToFloat(px[0]) + 0.5f)
            | (uint32_t(bx::halfToFloat(px[1]) + 0.5f) << 8)
            | (uint32_t(bx::halfToFloat(px[2]) + 0.5f) << 16);
        if (id == 0 || id > rows) {
            ++outOfRange;
            continue;
        }
        ++hist[id];
    }

    // ⛔ The impossible-root guard, in its second incarnation (see
    // OcclusionFrameStats::rootRefused). An id pass that drew nothing
    // makes every draw look as though it reached no pixel, so the audit
    // would report a flawless culling and a colossal amount of wasted
    // work — the most convincing possible output, and entirely a report
    // that the instrument is broken. A frame that put geometry on the
    // screen covers pixels here; say so instead of computing on it.
    if (!covered) {
        RENDER_ERR("render cull audit: the id image is empty -- no draw "
                   "claimed a pixel, which cannot be true of a frame that "
                   "rendered. The audit is not measuring the scene; its "
                   "numbers would be meaningless and are not reported.");
        hist.clear();
        return;
    }

    // The two answers, and their worst offenders.
    struct Offender { uint32_t id; uint32_t px; };
    std::vector<Offender> overcull;
    size_t overcullPx = 0, zeroPixelDrawn = 0, drawn = 0, masked = 0;
    for (size_t i = 0; i < rows; ++i) {
        const bool cut = i < mask.size() && mask[i] != 0;
        if (cut) {
            ++masked;
            if (hist[i + 1])
                overcull.push_back({uint32_t(i), hist[i + 1]});
            overcullPx += hist[i + 1];
        }
        else {
            ++drawn;
            if (!hist[i + 1])
                ++zeroPixelDrawn;
        }
    }
    std::sort(overcull.begin(), overcull.end(),
              [](const Offender &a, const Offender &b) {
                  return a.px > b.px;
              });

    std::string worst;
    const size_t show = std::min<size_t>(overcull.size(), 5);
    for (size_t i = 0; i < show; ++i) {
        const uint64_t key = overcull[i].id < keys.size()
            ? keys[overcull[i].id] : 0;
        auto it = names.find(key);
        worst += " ";
        // The label if the producer resolved one, else the internal
        // name, else the row. Never nothing: a count without a name is
        // the readout this whole exercise exists to replace.
        if (it != names.end() && !it->second.label.empty())
            worst += it->second.label;
        else if (it != names.end() && !it->second.obj.empty())
            worst += it->second.obj;
        else
            worst += "row";
        worst += "#" + std::to_string(overcull[i].id) + ":"
            + std::to_string(overcull[i].px) + "px";
    }

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "render cull audit: %ux%u %zu covered px | over-cull %zu of %zu "
             "masked rows, %zu px (%.3f%% of covered)%s | drawn-but-invisible "
             "%zu of %zu (%.1f%%)%s\n",
             unsigned(w), unsigned(h), covered,
             overcull.size(), masked, overcullPx,
             100.0 * double(overcullPx) / double(covered),
             worst.c_str(), zeroPixelDrawn, drawn,
             drawn ? 100.0 * double(zeroPixelDrawn) / double(drawn) : 0.0,
             outOfRange ? " | WARNING: ids outside the draw list" : "");
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", buf);
#else
    Base::Console().Message("%s", buf);
#endif

    // ⭐⭐ The second line: not *that* the culling deleted visible
    // geometry but *which verdict* did (docs/FarFieldProxies.md §12.10).
    //
    // ⛔ What is deliberately not here: a drawn/hidden split of the
    // tests that produced those verdicts. §12.6's account of the
    // per-test failure is a drawn node losing a depth tie against its
    // own contents, and flagging which set the offer came from looks
    // like the way to confirm it — but only an already-hidden node is
    // offered from the hidden set, so the answer that first sets
    // `hidden` is *always* of the drawn kind. It would report 100%
    // every time, for every scene, including scenes where the account
    // is wrong. See OcclusionNodeState.
    //
    // What is measurable, and what these two numbers are: how *fresh*
    // the deciding answer was, and how often the node has flipped. A
    // query that lied and a world that moved underneath a correct
    // answer look identical in a single verdict; they do not look
    // identical in a node that has entered the hidden state eighteen
    // times against a camera that never moved.
    if (overcull.empty() || owner.empty() || nodes.empty())
        return;

    struct NodeTally { uint32_t px = 0, rows = 0; };
    std::map<int32_t, NodeTally> byNode;
    size_t frustumRows = 0, frustumPx = 0;
    // Fresh: the deciding answer arrived within the last couple of
    // frames, so with a static camera nothing had time to change
    // between the query and the mask it produced.
    size_t freshPx = 0, stalePx = 0, flippingPx = 0;
    static const uint32_t kFreshFrames = 2;
    static const uint16_t kFlipping = 3;
    for (const Offender &o : overcull) {
        const int32_t node = o.id < owner.size() ? owner[o.id] : -1;
        if (node < 0 || size_t(node) >= nodes.size()) {
            // Masked, wrongly, and not by occlusion. A frustum test
            // that rejects a draw owning pixels is a different bug in
            // a different piece of code, and counting the two together
            // is how a fix gets attributed to the wrong mechanism.
            ++frustumRows;
            frustumPx += o.px;
            continue;
        }
        NodeTally &t = byNode[node];
        t.px += o.px;
        ++t.rows;
        const Render::OcclusionNodeAudit &a = nodes[size_t(node)];
        (a.framesSinceAnswer <= kFreshFrames ? freshPx : stalePx) += o.px;
        if (a.hidEvents >= kFlipping)
            flippingPx += o.px;
    }

    std::vector<std::pair<int32_t, NodeTally>> ranked(byNode.begin(),
                                                      byNode.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<int32_t, NodeTally> &a,
                 const std::pair<int32_t, NodeTally> &b) {
                  return a.second.px > b.second.px;
              });

    std::string worstNodes;
    for (size_t i = 0; i < std::min<size_t>(ranked.size(), 5); ++i) {
        const Render::OcclusionNodeAudit &a = nodes[size_t(ranked[i].first)];
        char nb[224];
        snprintf(nb, sizeof(nb),
                 " n%d(L%u res%u sub%u lastpx%d age%uf hid%uf ev%u):%upx/%urows",
                 int(ranked[i].first), unsigned(a.level),
                 unsigned(a.residentCount), unsigned(a.subtreeCount),
                 int(a.lastPx), unsigned(a.framesSinceAnswer),
                 unsigned(a.framesHidden), unsigned(a.hidEvents),
                 unsigned(ranked[i].second.px),
                 unsigned(ranked[i].second.rows));
        worstNodes += nb;
    }

    // Shares of what occlusion cut, not of the whole over-cull: the
    // frustum's rows are in the same mask but are not this mechanism's
    // to explain, and folding them into the denominator would quietly
    // shrink every share here whenever the other bug got worse.
    const size_t occPx = freshPx + stalePx;
    char nbuf[1024];
    snprintf(nbuf, sizeof(nbuf),
             "render cull attribution: %zu node(s) account for %zu px "
             "| verdict fresh (<=%uf) %zu px (%.1f%%), older %zu px (%.1f%%) "
             "| from nodes that have flipped >=%u times: %zu px (%.1f%%) "
             "| frustum, not occlusion: %zu rows %zu px | worst%s\n",
             ranked.size(), occPx, kFreshFrames, freshPx,
             occPx ? 100.0 * double(freshPx) / double(occPx) : 0.0,
             stalePx,
             occPx ? 100.0 * double(stalePx) / double(occPx) : 0.0,
             unsigned(kFlipping), flippingPx,
             occPx ? 100.0 * double(flippingPx) / double(occPx) : 0.0,
             frustumRows, frustumPx, worstNodes.c_str());
#ifdef FC_RENDERER_STANDALONE
    std::printf("%s", nbuf);
#else
    Base::Console().Message("%s", nbuf);
#endif
}

class BGFXRenderer::Private
{
public:
    Private(QOpenGLWidget *widget, bool publishOnly = false)
        :widget(widget), publishOnly(publishOnly)
    {
        _BGFXLib.renderers.insert(this);
    }

    ~Private()
    {
        _BGFXLib.renderers.erase(this);
        deinit();
    }

    void deinit()
    {
        _deinit = true;
        // The occlusion queries are a process-wide pool shared by every
        // view (docs/FarFieldProxies.md §10.1, §12), so a closed view
        // that kept its leases would starve the next one that measured
        // or culled.
        for (auto q : occlusionProbe.handles)
            occlusionLeases.release(q);
        occlusionProbe.handles.clear();
        occlusionProbe.pending.clear();
        cullQueries.releaseAll();
        // A publish-only renderer never asked for a view, so there is
        // none to remove -- and asking would be the one call that
        // brings the graphics device into a process that has none.
        if (!publishOnly)
            _BGFXLib.removeView(widget);
    }

    /// Assemble the CPU-side snapshot of everything the feeds hold:
    /// the draws, the selections and highlight, the overlays and every
    /// per-frame config, plus the camera and viewport the frame was
    /// asked for. Touches no GPU state -- the frame dump and the scene
    /// publish are both built from this, and a publish-only process
    /// (docs/HeadlessServe.md) never gets past it.
    void makeSnapshot(Render::SceneSnapshot &snap,
                      const void *viewMatrix,
                      const void *projMatrix,
                      uint16_t width,
                      uint16_t height,
                      uint32_t clearColor);

    /// A cheap identity of what the feeds currently hold — which draws,
    /// of what, over which geometry. Two consecutive frames with the
    /// same one are a scene that has stopped arriving; that is the
    /// condition a dump meant for comparison waits on
    /// (FC_BGFX_DUMP_SCENE_SETTLE). It deliberately reads identity
    /// (cacheId, objectKey) rather than content: the point is to notice
    /// that something changed, and a counter that moves on every
    /// re-tessellation notices more, not less.
    uint64_t sceneFingerprint() const;

    /// FC_BGFX_DUMP_SCENE=<path>: snapshot the first non-empty scene
    /// feed with all per-frame configs and the camera for the
    /// standalone/wasm viewer (SceneDump.h).
    /// FC_BGFX_DUMP_SCENE_DELAY=<n> skips the first n non-empty
    /// frames, and FC_BGFX_DUMP_SCENE_SEL=1 additionally waits for a
    /// non-empty selection feed, so later state (a selection made by
    /// a script) is in the capture.
    /// FC_BGFX_DUMP_SCENE_SETTLE=<n> waits for the feeds to stop
    /// changing for n consecutive frames first, which is what a dump
    /// meant for comparison wants: a document does not arrive all at
    /// once (objects are added, tessellated, recoloured, and a
    /// coarse-first load refines rungs afterwards), so a dump taken a
    /// fixed number of frames in captures how far the build had got.
    /// Measured on a 40-object scene: three runs of one script, three
    /// different scenes -- 2159580, 2310620 and 2454636 bytes. Nothing
    /// downstream can tell that from a real difference, because a
    /// bundled dump carries no level information at all. Not for an
    /// animated scene, which never settles by construction.
    ///
    /// Written with no chunk/mesh/texture sinks installed, so the file
    /// is monolithic and self-contained — unlike a published manifest,
    /// which is content-keyed and delta-encoded per viewer. That makes
    /// it the comparable form of a scene, and so the way one publisher
    /// is diffed against another (docs/HeadlessServe.md §4, stage 2c).
    /// Hence it hangs off the snapshot, not off the frame: a
    /// publish-only process draws nothing and must still be able to
    /// produce one.
    void maybeDumpScene(const void *viewMatrix,
                        const void *projMatrix,
                        uint16_t width,
                        uint16_t height,
                        uint32_t clearColor,
                        bool dirtyChanged);

#ifndef FC_RENDERER_STANDALONE
    /// Publish the feeds to the scene-stream server when they have
    /// changed (docs/SceneStreaming.md). Starts the server on first
    /// call. Like makeSnapshot() this is CPU work alone: it is
    /// deliberately separable from the frame it currently rides on, so
    /// a source with no 3D view can drive it (docs/HeadlessServe.md
    /// §3.1).
    void publishScene(const void *viewMatrix,
                      const void *projMatrix,
                      uint16_t width,
                      uint16_t height,
                      uint32_t clearColor,
                      bool dirtyChanged);

    /// The whole of what a publish-only renderer does
    /// (docs/HeadlessServe.md §3.1): consume the pending feed change
    /// and put the snapshot on the wire. Everything render() does
    /// besides this needs a graphics device, and this process has
    /// none -- so unlike render(), which reaches the same publish on
    /// its way past a live view, there is nothing here to skip.
    bool publishNoDraw(const QColor &col,
                       const void *viewMatrix,
                       const void *projMatrix,
                       uint16_t width,
                       uint16_t height);
#endif

    bool render(const QColor &col,
                const void * viewMatrix,
                const void * projMatrix);

    /// GL's SectionFillInvert color transform (_renderSection ~1838):
    /// invert each channel, mapping the mid-gray band to 180 and
    /// near-black results to 50; the alpha stays.
    static uint32_t invertCapColor(uint32_t col);

    /// Build the world-space cap quad of one section plane over the
    /// given bounds, replicating _renderSection's geometry: a square of
    /// the bounding sphere's radius centered on the bbox center
    /// projected onto the plane, oriented by Coin's z-to-normal
    /// rotation, with the hatch texture coordinates scaled from the
    /// world-to-pixel ratio at mid view depth.
    void buildCapQuad(const float plane[4], const float bmin[3],
                      const float bmax[3], const float *projMat,
                      int vpWidth, CapVertex verts[4]) const;

    /// Stencil section caps of clipped solids (GL: renderSection /
    /// renderSectionGrouped / _renderSection). For every eligible draw
    /// (whole solid triangle geometry with clip planes) and every one of
    /// its section planes: parity-mark the solid clipped by that plane
    /// alone, fill the cap quad where marked (clipped by the remaining
    /// planes outside concave mode), and clean the stencil up again.
    /// SectionFillGroup caps runs of same-colored draws together like
    /// GL, turning intersecting same-material solids into one boolean
    /// cut.
    void submitSectionCaps(BGFXView *view, const float *projMat);

    void updateBBox();

    QOpenGLWidget *widget;
    /// This renderer will never draw (docs/HeadlessServe.md §3.1): no
    /// bgfx view, no graphics device, no display. render() refuses;
    /// publishNoDraw() is the whole of what it does.
    bool publishOnly = false;
    /// The server document group this renderer publishes into; empty =
    /// the default group (Renderer::setPublishGroup).
    std::string publishGroup;
    bool _deinit = false;
    RendererType::Enum type;
    std::string typeName;

    /// The sub-view currently being submitted (renderSubViews,
    /// docs/SplitViews.md sec 9.2). While active, render() runs as one
    /// submit of a multi-sub-view frame: the view swaps to the bank
    /// named by id, the present pass lands at (x, y, w, h) of the
    /// backbuffer, and only the LAST submit crosses the frame boundary
    /// (bgfx::frame + the post-frame tail); only the FIRST ticks the
    /// per-frame counters. Inactive on every plain render(), which is
    /// bank 0 at full canvas.
    struct SubViewCtx {
        bool active = false;
        bool first = false;
        bool last = false;
        /// A warm-up submit: run the frame path only through target
        /// allocation for a fresh bank, draw nothing, tick nothing.
        /// prepareSubViews warms every unseen id this way, between
        /// frames -- each with its own frame boundary to allocate
        /// against -- so a renderSubViews sequence never meets a
        /// fresh bank mid-frame.
        bool warm = false;
        int id = 0;
        int x = 0, y = 0, w = 0, h = 0;
        /// This sub-view's Class-A display style (SubViewFrame::
        /// drawStyle, docs/CoinRetirement.md 5.7). Not a bank field:
        /// it is restated by every submit rather than carried across
        /// frames, so a cell whose style changed needs no bank reset.
        uint8_t style = Render::StyleAsIs;
        /// That style's NAME bit, and whether the feed captured the
        /// superset child; together they let the filter be resolved per
        /// OBJECT (5.8) instead of applied flat to every draw.
        uint8_t styleName = 0;
        bool fromSuperset = false;
        /// That style's mode id (SubViewFrame::drawStyleMode, 5.11),
        /// when the capture also carries the mode additively.
        uint16_t styleMode = 0;
        /// The cell's per-object override table (5.9), restated per
        /// submit like the style; the producer owns the storage.
        const Render::StyleOverrideTable *styleOverrides = nullptr;
    } subCtx;

    /// The plain (sub-view id 0) frame's style context, stated by the
    /// host through setMainViewStyle() (docs/CoinRetirement.md 5.9).
    /// All at rest -- StyleAsIs, no superset, no overrides -- except on
    /// a view whose override table is non-empty, which captures the
    /// superset and leaves the per-object resolution to the backend
    /// exactly like a canvas cell.
    uint8_t mainStyleMask = Render::StyleAsIs;
    uint8_t mainStyleName = 0;
    bool mainFromSuperset = false;
    uint16_t mainStyleMode = 0;
    const Render::StyleOverrideTable *mainStyleOverrides = nullptr;
    /// The capture's additive-mode interest list, stated through
    /// setCaptureInterest() (docs/CoinRetirement.md 5.9 "Non-standard
    /// modes"). One per renderer -- the interest belongs to the shared
    /// capture, not to a sub-view. The producer owns the storage.
    const Render::CaptureInterestTable *captureInterest = nullptr;

    // CPU-side scene data fed through Render::Renderer's scene API. GPU
    // upload happens lazily during render(), so the feed may arrive before
    // bgfx is initialized.
    Render::DrawCallList scene;
    /// Draw identity resolved by the producer (setObjectInfo); consulted
    /// by the snapshot writer for the published object entries.
    Render::ObjectInfoMap objectInfo;
    /// Renderer::objectInfoVersion() mirrored here when the table is
    /// stated whole -- the Private has no owner backpointer, and the
    /// frame path needs the stamp to invalidate the per-sub-view
    /// override caches (BGFXView::OvCache).
    uint32_t objectInfoStamp = 0;
    /// The labels those identities carry to a viewer (setObjectMeta),
    /// pushed by the serving source when a document changes them rather
    /// than rebuilt per publish. Empty on a view nobody serves.
    Render::ObjectMetaMap objectMeta;
    // Cross-object instance groups of the scene feed: draws sharing one
    // geometry content (by hash — a shared cache OR coincidentally
    // identical flattened caches), index range and material (diffuse
    // aside — links may override colors) collapse into one instanced
    // submit per frame. Rebuilt with the scene; the per-frame loop still
    // skips hidden members and falls back to per-draw submits when a
    // group thins out.
    struct InstGroup { std::vector<int> members; };
    std::vector<InstGroup> instGroups;
    std::vector<int> instGroupOf;   ///< scene index -> group index or -1

    /// Content identity of one mesh cache: the colorless geometry hash
    /// (computeGeomKey) plus a hash of the per-vertex color stream.
    /// Computed once per (cache id, refine generation) — so scene
    /// rebuilds only hash caches they have not seen before, and an
    /// in-place ladder refine re-hashes. Two caches with equal hashes
    /// render identically
    /// through one prototype: the geometry table already shares their
    /// GPU buffers, and equal color hashes mean the baked color streams
    /// match byte for byte.
    struct MeshContent {
        uint64_t geomHash;
        uint64_t colorHash;
        int numVertices;
        int numTri;
        uint32_t generation;
        uint64_t stamp;
    };
    std::unordered_map<uint64_t, MeshContent> meshContents;
    uint64_t meshContentStamp = 0;

    const MeshContent &meshContent(const Render::MeshData &mesh);

    /// A draw the instanced mesh path can express: an unclipped,
    /// non-on-top, non-water triangle draw without autozoom. Textured
    /// draws qualify (the texture identity joins the group key);
    /// transparent draws qualify too but only batch on WBOIT frames
    /// (submitInstanced falls back otherwise — sorted transparency
    /// needs per-draw depth keys). (Hidden-line frames disable
    /// instancing wholesale, so the outline material flag stays out of
    /// the picture.)
    static bool instancableDraw(const Render::DrawCall &d);

    /// How far each object's decoration reaches from its own geometry,
    /// in pixels -- what its fills' polygon offset has to clear so a
    /// thick edge is not half-eaten by the face it straddles (see
    /// BGFXView::polygonOffsetFactor). objectKey -> reach; absent means
    /// the coincident-surface default of 1.
    ///
    /// A pure function of the published scene, so it is resolved once
    /// per setScene rather than per frame -- and it MUST be, because
    /// buildInstanceGroups keys on it: the reach is not a material
    /// field, and an instanced submit binds one polygon offset for the
    /// whole batch.
    std::unordered_map<uint64_t, float> decorReach;

    void buildDecorReach();

    void buildInstanceGroups();

    Render::Background background;
    /// Version stamp of the published SCENE -- bumped by setScene,
    /// consumed by publishedMeshes() below.
    uint64_t drawListVersion = 1;
    /// cacheId -> generation of every mesh the published scene
    /// carries: the collector's keep-set (sec 13c.5). A kept mesh is
    /// never TTL-collected -- its buffers die at the rung swap
    /// (generation moved: freed synchronously at the next collect,
    /// drawn or not, which is what makes a downgrade's free an EVENT
    /// the plan can trust) or when it leaves the scene at a publish.
    /// One carve-out: a kept mesh whose every draw the element gates
    /// suppress (gatedOnlyMeshes) is not submittable, and keeping the
    /// unsubmittable is how 74.5MB of gated edge buffers came to stand
    /// behind a 64MB budget -- it falls back to the recency rule
    /// instead (collectMeshes).
    /// The two-frame TTL guess collected buffers the scene still drew
    /// on a longer-than-two-frame cadence and re-uploaded them
    /// forever: the measured 198-draw, 75MB, period-4 wave.
    ///
    /// The SCENE only, deliberately: overlays, selections and the
    /// highlight feed (NaviCube, show-on-top) draw every frame while
    /// they are active, so the TTL never bites them and they need no
    /// keep entry -- and the highlight churns per HOVER, which would
    /// rebuild this map per mouse move if it were included. They stay
    /// on the recency rule, ungoverned, for now.
    std::unordered_map<uint64_t, uint64_t> publishedIds;
    uint64_t publishedIdsVersion = 0;
    const std::unordered_map<uint64_t, uint64_t> &publishedMeshes()
    {
        if (publishedIdsVersion == drawListVersion)
            return publishedIds;
        publishedIdsVersion = drawListVersion;
        publishedIds.clear();
        for (const auto &d : scene)
            if (d.mesh)
                publishedIds[d.mesh->cacheId] = d.mesh->generation;
        return publishedIds;
    }
    /// The per-capture object filter (Renderer::setCaptureFilter):
    /// while active, renderOffscreen() swaps in a scene reduced to
    /// draws whose objectKey is in this set, with selection /
    /// highlight / overlay feeds stripped and a flat transparent
    /// background, then restores everything.
    std::unordered_set<uint64_t> captureKeys;
    bool captureFilter = false;
    /// The transient supplied scene (Renderer::setCaptureScene): while
    /// active, renderOffscreen() renders these draws instead of the
    /// resident scene, through the same swap-and-restore body as the
    /// object filter. Takes precedence over captureFilter.
    Render::DrawCallList captureScene;
    bool captureSceneActive = false;

    std::map<int, Render::DrawCallList> selections;
    // Overlay feeds keyed by producer id (Renderer::setOverlay); map
    // order assigns the (limited) overlay view slots deterministically.
    struct OverlayFeed {
        Render::DrawCallList draws;
        Render::OverlayAnchor anchor;
    };
    std::map<int, OverlayFeed> overlays;

    /// A module drawing its own passes inside this renderer's frames
    /// (Renderer::setFrameConsumer, docs/CAMSimRenderPort.md sec 8),
    /// and the surface it draws through. The surface is sized to the
    /// consumer's pass count at registration and owned here, so a
    /// consumer that goes away cannot leave one behind, and it is null
    /// whenever the backend device is down -- in which case the
    /// consumer never gets called and keeps its own path.
    struct ConsumerSlot {
        Render::FrameConsumer *consumer = nullptr;
        std::unique_ptr<Render::BGFXHostSurface> surface;
        /// Passes the surface was built for; a consumer that changes
        /// its count has to re-register, and this is what notices.
        unsigned passes = 0;
        /// Of passes, the trailing overlay-run count
        /// (FrameConsumer::overlayPasses; the rest are the scene run).
        unsigned overlayPasses = 0;
        bool externalBase = false;  ///< Renderer::setExternalBaseLayer
    };
    /// One slot per sub-view id (Renderer::setFrameConsumer's subView;
    /// 0 is the plain-render full-canvas sub-view), so that each cell
    /// of a split-view frame can carry a consumer of its own
    /// (docs/CyclesIntegration.md sec 5.11). A slot's surface draws
    /// into that sub-view's bank targets: the consumer's hostTarget()
    /// is the cell's scene target, not the canvas.
    std::map<int, ConsumerSlot> consumerSlots;
    /// The slot of the submit in progress, resolved by
    /// selectConsumer() where the frame swaps in the sub-view's bank;
    /// every read in the frame path goes through these. Cleared
    /// whenever the slots change, so nothing dangles between frames.
    Render::FrameConsumer *frameConsumer = nullptr;
    Render::BGFXHostSurface *consumerSurface = nullptr;
    unsigned consumerPasses = 0;
    unsigned consumerOverlayPasses = 0;
    bool externalBase = false;
    void selectConsumer(int subView)
    {
        auto it = consumerSlots.find(subView);
        if (it == consumerSlots.end()) {
            clearConsumer();
            return;
        }
        const ConsumerSlot &slot = it->second;
        frameConsumer = slot.consumer;
        consumerSurface = slot.surface.get();
        consumerPasses = slot.passes;
        consumerOverlayPasses = slot.overlayPasses;
        externalBase = slot.externalBase;
    }
    void clearConsumer()
    {
        frameConsumer = nullptr;
        consumerSurface = nullptr;
        consumerPasses = 0;
        consumerOverlayPasses = 0;
        externalBase = false;
    }
    Render::DrawCallList highlight;
    std::unordered_set<uint64_t> hiddenKeys;
    std::unordered_set<const Render::DrawCall *> dupDraws;
    Render::HiddenLineConfig hlconfig;
    Render::SectionConfig secconf;
    Render::AOConfig aoconf;
    Render::CavityConfig cavityconf;
    Render::MatcapConfig matcapconf;
    Render::PBRConfig pbrconf;
    Render::BumpConfig bumpconf;
    Render::LightConfig lightconf;
    Render::ViewLightConfig viewlightconf;
    Render::VolumetricConfig volconf;
    Render::WaterConfig waterconf;
    Render::BloomConfig bloomconf;
    Render::TemporalConfig tempconf;
    Render::OutputConfig outconf;
    Render::RenderDebugConfig debugconf;
    /// Backend frame cost accumulated since the last reported line
    /// (docs/FarFieldProxies.md sec 10.1). Per view, because two views
    /// draw different scenes and a shared accumulator would report
    /// their mean as though it were one frame's.
    FrameStatsAccum frameStats;
    /// Where the FIXED per-frame CPU goes inside render()
    /// (docs/DrawSubmission.md phase 0 item 2). The frame line already
    /// splits ours / bgfx / outside; it found 15.6ms of our own C++ that
    /// does NOT scale with draw count, which is four times the per-draw
    /// part and is the largest single CPU item in the renderer. These
    /// name the candidates: the cull walks all rows, and so does the
    /// submit loop, whatever the survivors number.
    ///
    /// Accumulated over the reporting window like frameStats, and reset
    /// with it. Anything not scoped shows up in the derived remainder,
    /// which is the point -- a breakdown that cannot be wrong about what
    /// it left out.
    /// CpuPreSubmit is everything from the top of render() to the main
    /// submit loop (the cull included, and subtracted back out when
    /// reported); CpuPostSubmit is what the remainder must then be.
    /// Measured with a checkpoint rather than a scope because the region
    /// has early returns and is not a block.
    ///
    /// The Post* four subdivide the post-submit region, which measured
    /// 12.95ms flat across a 2.4x change in draw count. ! The first
    /// explanation offered for that -- "eight per-pass full scans of the
    /// draw list" -- was WRONG and is why these exist: six of the eight
    /// are behind ground-reflection, hidden-line or scene-outline
    /// guards that are off on this camera, and the three that do run
    /// short-circuit per row. Counting loops in the source is not
    /// measuring them.
    ///
    /// CpuCtxOut / CpuCtxIn / CpuBlit are the desktop hand-off around
    /// bgfx::frame(): drop the Qt GL context, take bgfx's, and after the
    /// frame take Qt's back and copy bgfx's target into the widget. The
    /// comment on timedBgfxFrame calls these "Qt's cost, not bgfx's" and
    /// excludes them from the bgfx figure -- which is right, and is
    /// exactly why they need a number of their own. They are flat per
    /// frame, which is the shape the fixed cost has.
    enum CpuPhase { CpuCull, CpuSubmitLoop, CpuPreSubmit,
                    CpuPostCaps, CpuPostEffects, CpuPostSel, CpuPostTail,
                    CpuCtxOut, CpuCtxIn, CpuBlit, CpuCtxDone,
                    CpuPhaseCount };
    int64_t renderInnerT0 = 0;
    /// Running checkpoint for the Post* phases: each mark closes the
    /// span since the previous one. Safe because the post region is
    /// straight-line at top level (its only `return` is inside a
    /// lambda), so no span can be left open.
    int64_t cpuMarkT = 0;
    void cpuMark(int phase)
    {
        if (!debugconf.frameTiming)
            return;
        const int64_t now = bx::getHPCounter();
        cpuPhaseMs[phase] += 1000.0 * double(now - cpuMarkT)
            / double(bx::getHPFrequency());
        cpuMarkT = now;
    }
    double cpuPhaseMs[CpuPhaseCount] = {};
    /// RAII so an early return or a throw cannot leave a phase open.
    struct CpuScope {
        Private *self;
        int phase;
        int64_t t0;
        bool on;
        CpuScope(Private *s, int p)
            : self(s), phase(p), t0(0), on(s->debugconf.frameTiming)
        {
            if (on)
                t0 = bx::getHPCounter();
        }
        ~CpuScope()
        {
            if (on)
                self->cpuPhaseMs[phase] += 1000.0
                    * double(bx::getHPCounter() - t0)
                    / double(bx::getHPFrequency());
        }
        CpuScope(const CpuScope &) = delete;
        CpuScope &operator=(const CpuScope &) = delete;
    };
    /// The occluded-fraction walk (docs/FarFieldProxies.md sec 10.1) and
    /// its scratch. Per view for the same reason as frameStats: two
    /// views see different scenes from different cameras.
    OcclusionProbeState occlusionProbe;
    OcclusionBoxBatch occlusionBatch;
    OcclusionLeases occlusionLeases;
    /// Occlusion culling (docs/FarFieldProxies.md sec 12) -- the same
    /// mechanism as the probe above, acting on its answers. Per view
    /// for the same reason: the verdicts are a camera's, not a scene's.
    Render::OcclusionCullConfig cullconf;
    Render::OcclusionCuller culler;
    Render::OcclusionTestBatch cullBatch;
    /// The tests in flight and the handles created for them. ! One
    /// handle per test, not a pool of handles reassigned per frame: a
    /// bgfx query handle is an object's identity and a reassigned one
    /// answers with the previous occupant's verdict (OcclusionLeases).
    OcclusionCullQueries cullQueries;
    /// KEY: The other oracle (docs/FarFieldProxies.md section 12.12): a CPU
    /// depth buffer, rasterized and questioned inside one frame. It
    /// shares the *index* with the culler above and nothing else -- no
    /// verdict, no lease, no pad, no confirmation -- because everything
    /// the culler keeps between frames is there to survive a latency
    /// this path does not have.
    Render::MaskedOccluderPass maskedCull;
    /// The frame-level A/B probe over the whole occlusion block
    /// (12.13, the OcclusionBenefitProbe parameter). Holds its verdict
    /// arms across frames; reset on the probe's off->on edge so a
    /// re-enabled probe is a fresh experiment.
    Render::CullBenefitEstimator cullBenefit;
    bool cullBenefitOn = false;
    /// Occlusion folded per SOURCE, for the level plan (occlusion as a
    /// memory mechanism): how many consecutive rendered frames EVERY
    /// draw of a source has been culled -- frustum or occlusion, with
    /// the software oracle answering. The downgrade sweep treats a
    /// source past OcclusionCullConfig::demoteStreak as free: its
    /// upload comes back without charging the camera visible error.
    /// An entry is only trusted at the CURRENT fold generation, so a
    /// reused tag address cannot inherit a dead source's streak; the
    /// map is pruned of stale entries on a slow cadence rather than
    /// rebuilt, and a streak survives a paused render loop only if the
    /// camera did (the plan fires against the last rendered frame's
    /// camera, which is the one the fold saw).
    struct OcclStreak {
        uint32_t frames = 0;
        uint64_t fold = 0;
    };
    std::unordered_map<const void *, OcclStreak> occlHiddenStreak;
    uint64_t occlStreakFold = 0;
    /// * The cull audit (docs/FarFieldProxies.md sec 12.9): one id image in
    /// flight, plus the verdict it is an answer about.
    ///
    /// The snapshots are the point. A readback lands a frame or two
    /// after the image was drawn, by which time the mask has been walked
    /// again and the draw list may have been republished; checking the
    /// image against *then-current* state would compare a picture with a
    /// verdict that was never applied to it -- which is the same
    /// one-frame skew that made the culling oscillate in the first
    /// place, reappearing inside the instrument built to find it.
    std::vector<uint16_t> idPixels;   ///< RGBA16F, 4 halves per pixel
    std::vector<uint8_t> idMask;      ///< the cull mask, as it was
    /// Which node's verdict cut each masked row, as it was. Empty when
    /// occlusion culling is off, which is how the readout tells "the
    /// frustum cut this row" from "nothing was attributing at all".
    std::vector<int32_t> idOwner;
    /// Every node's visibility state, as it was. Indexed by node.
    std::vector<Render::OcclusionNodeAudit> idNodeAudit;
    std::vector<uint64_t> idKeys;     ///< objectKey per row, as it was
    std::vector<uint32_t> idHist;     ///< pixels owned, per id
    /// What the tight-bound arms would have culled on the frame the
    /// image was taken (docs/FarFieldProxies.md sec 12.19). Snapshots like
    /// everything else here, and for the same reason -- they are answers
    /// about that image. Empty unless RenderDebug_CullBounds is on.
    std::vector<uint8_t> idTightJudged;
    std::vector<uint8_t> idTightAabb;
    std::vector<uint8_t> idTightObb;
    std::vector<uint8_t> idTightTri;
    TightBoundAudit idTight;
    /// Primitives a single draw may be asked about one by one before the
    /// diagnostic gives up on it. A bound on the worst row, not a
    /// sample: rows that hit it are counted and reported, because a cap
    /// nobody is told about reads as a mechanism that found nothing.
    static const uint32_t kTightPrimitiveCap = 200000;
    /// Frame at which idPixels is filled; 0 = no readback in flight.
    uint32_t idReadyFrame = 0;
    uint16_t idPixW = 0;
    uint16_t idPixH = 0;
    bool idAuditWarned = false;
    /// In-flight portable frame capture (BGFXView::readbackCapture).
    ///
    /// The colour arrives in the scene target's own format, which is
    /// RGBA16F while colour managed, so it is held as bytes and decoded
    /// once it lands. bgfx writes into these from the render thread
    /// long after the request, so they must not be resized or freed
    /// while captureReadyFrame is non-zero.
    std::vector<uint8_t> captureColor;
    std::vector<uint8_t> captureDepth;  ///< R32F, one float per pixel
    /// Frame at which both are filled; 0 = no capture in flight.
    uint32_t captureReadyFrame = 0;
    uint16_t capturePixW = 0;
    uint16_t capturePixH = 0;
    /// Whether the in-flight capture's colour is RGBA16F rather than
    /// RGBA8 -- decoded when it lands, not guessed from the config,
    /// which may have changed by then.
    bool captureHdr = false;
    /// The request the in-flight capture is serving. Copied rather
    /// than referenced: a capture spans frames, and pendingDump is
    /// overwritten by whatever asks next.
    Render::FrameDumpRequest captureRequest;
    bool captureIsDump = false;
    /// What cross-object instancing collapsed on the last frame
    /// (docs/DrawSubmission.md phase 0.5). Every submission decision is
    /// scoped against the draw count, and until this existed nothing
    /// said how much of that count the batching already removes --
    /// "the model has repeated parts" and "the renderer is instancing
    /// them" were one belief with no measurement between them.
    struct InstancingStats {
        uint32_t groups = 0;          ///< groups the producer formed
        uint32_t groupsSingleton = 0; ///< of them, holding one member
        uint32_t groupsUsable = 0;    ///< of them, holding two or more
        uint32_t membersUsable = 0;   ///< draws in the usable groups
        uint32_t submits = 0;         ///< instanced submits issued
        uint32_t drawsReplaced = 0;   ///< draws those submits stood in for
        uint32_t refused = 0;         ///< groups the backend refused
        uint32_t refusedMembers = 0;
        uint32_t thinnedMembers = 0;  ///< left alone by culling/visibility
        uint32_t eligible = 0;        ///< scene rows offered
        /// Why nothing was instanced at all, when nothing was. Null when
        /// the path ran -- a zero with no reason beside it is the readout
        /// this exists to replace.
        const char *why = nullptr;
    };
    InstancingStats instStats;
    /// Whether BGFX_DEBUG_PROFILER is currently set. bgfx needs it to
    /// fill per-view stats, and it is a context-wide switch, so it is
    /// tracked rather than set every frame.
    bool profilerOn = false;
    /// The draw list the index was built from. A rebuild costs 12-28 ms
    /// on a large assembly, so it happens when the scene changes and
    /// never per frame.
    uint64_t cullSceneVersion = 0;
    uint64_t cullBuiltVersion = 0;
    double cullBuildMs = 0.0;
    bool cullUnsupported = false;
    /// Draws the index deliberately does not contain, and how many of
    /// them were on-top. Reported, because "the index is smaller than
    /// the scene" is the difference between exempting what cannot be
    /// judged and quietly not culling anything.
    uint32_t cullExemptOnTop = 0;
    uint32_t cullIndexed = 0;
    Render::UserShaderConfig usershaderconf;
    /// Snapshot of _BGFXLib.userCompileGeneration taken by render();
    /// isSceneDirty() reports dirty while they differ (async compile).
    int userShaderGen = 0;
    Render::PreselHighlightConfig preselconf;
    Render::PreselHighlightConfig selconf;
    float autozoomScale = 1.0f;
    // CPU copy of the section hatch texture, expanded to RGBA8; the
    // version stamps GPU re-uploads (null = no image). Held as a
    // TextureImage so the stream can carry it in the texture table,
    // content-keyed and served out of band (SceneDump.h, v27) — the
    // key is then hashed once per hatch change, not per publish.
    std::shared_ptr<Render::TextureImage> hatchTex;
    const void *hatchKey = nullptr;
    /// Streamed mesh chunks by cacheId: content key and chunk size, for
    /// the publish in flight and the one before it (SceneDump.h,
    /// MeshBlobSink). Sound because a cacheId names one content for the
    /// life of the process, so a hit needs no re-hash; bounded because
    /// an entry only survives while the blob it names is still served.
    std::map<uint64_t, std::pair<std::string, uint32_t>> meshKeys;
    std::map<uint64_t, std::pair<std::string, uint32_t>> meshKeysPrev;
    /// The object list of the last publish, ordered by objectKey. The
    /// difference against it is what the server remembers per publish,
    /// and it is the publisher that has to keep it: the server holds
    /// bytes, not scenes.
    std::vector<Render::SceneSnapshot::ObjectEntry> publishedObjects;
    uint64_t hatchVersion = 0;
    bool hlWholeOnTop = false;
    /// One-shot frame capture (requestFrameDump), consumed by the next
    /// frame's blit; lastStats keeps that readback's statistics.
    Render::FrameDumpRequest pendingDump;
    bool dumpPending = false;
    /// The host said the next frame is not the whole scene
    /// (Renderer::holdFrameDump); read and cleared at the frame tail
    /// beside the lib's stand-in record.
    bool hostHold = false;
    /// Whether the last frame held the pending dump (frameDumpHeld).
    bool dumpHeld = false;
    /// This frame still owes something the picture depends on: a
    /// frozen frame's particle warm-up not yet reached, a mesh refine
    /// the level plan just asked for. Per frame, like the lib's
    /// stand-in record; the tail folds both into the verdict.
    bool frameOwes = false;
    /// The last frame's verdict and the counters behind
    /// Renderer::frameComplete / renderedFrames / completeFrames.
    bool lastFrameComplete = false;
    uint64_t renderedFrameCount = 0;
    uint64_t completeFrameCount = 0;
    Render::RenderStats lastStats;
    bool sceneDumped = false;   ///< FC_BGFX_DUMP_SCENE fired
    /// Frames since the feeds last changed, and the fingerprint that is
    /// measured against, for FC_BGFX_DUMP_SCENE_SETTLE.
    int dumpQuietFrames = 0;
    uint64_t dumpFingerprint = 0;
    int dumpFrames = 0;         ///< non-empty frames seen (dump delay)
    bool serveStarted = false;  ///< FC_BGFX_SERVE_SCENE start attempted
    bool scenePublished = false;///< at least one payload published
    /// SceneStreamServer::levelsBuilt() as of the last publish. The
    /// counter moving is what a finished level-generation job looks
    /// like from here, and the response is a republish — that is the
    /// announcement (§7, phase 5c), there is no other channel.
    size_t publishedLevelsBuilt = 0;
    bool sceneDirty = false;
    // Whether the last rendered frame contained time-animated content
    // (fire/cloud/water/caustics) — see the assignment in render().
    // Defaults to true so a client's idle skip never engages before the
    // first frame has classified the scene.
    bool sceneAnimated = true;
    // Distinct from sceneDirty: set ONLY when the scene geometry feed itself
    // changes (setScene), not on cheap per-frame state (AO toggle, selection
    // /preselect highlight, effect resolution). The STANDALONE warmup target
    // rebuild keys off this — re-arming it on every sceneDirty made every
    // highlight/AO toggle trigger a full view->init() (a hi-DPI multisampled
    // target realloc = a WebGL stall), felt as a hitch at drag start and on
    // any preselection.
    bool feedDirty = false;
    bool hasScene = false;
    bool renderOk = false;

    /// Does anything read the pixels this view draws?
    ///
    /// For a serving process (FC_BGFX_SERVE_SCENE): no, and not just
    /// while no viewer is connected. What it puts on the wire is a
    /// scene *snapshot*, re-sent when the scene changes rather than per
    /// frame, and its viewers rasterize and animate it themselves off
    /// their own clock. Its own window is never in that path, connected
    /// viewers or not -- so every pixel it rasterizes is read by
    /// nobody, and under Xvfb (llvmpipe) read by nobody at the cost of
    /// several cores.
    ///
    /// Same view of the local window the level planner already takes
    /// ("a serving process's own window never plans -- its viewers'
    /// cameras decide"). Off the serving path this is always true:
    /// there the window IS the audience.
    ///
    /// FC_BGFX_SERVE_DRAW=1 puts the window back in the picture, for
    /// serving from a desktop session where somebody is in fact
    /// watching it.
    ///
    /// Two things follow: an animated scene stops asking for frames
    /// (BGFXRenderer::animating), and the frames that do happen -- a
    /// scene change, a viewer's hello -- stop at the publish (the
    /// publish-only exit in render()).
    bool localAudience() const
    {
#ifndef FC_RENDERER_STANDALONE
        static const char *servePort = getenv("FC_BGFX_SERVE_SCENE");
        static const bool serveDraw =
            getenv("FC_BGFX_SERVE_DRAW") != nullptr;
        if (servePort && *servePort && !serveDraw)
            return false;
#endif
        return true;
    }
    bool bboxValid = false;
    // The last rendered frame splatted animated water caustics: the
    // viewer keeps redrawing while set so the animation advances.
    bool animatedFrame = false;
    float bboxMin[3], bboxMax[3];
    /// The camera the last frame drew with, kept for the shadow
    /// ground's camera-fitted sizing (LightConfig::groundFollowCamera).
    ///
    /// Kept rather than passed because boundBox() -- the scene bounds
    /// the viewer's auto near/far reads -- is asked OUTSIDE a frame,
    /// and it has to build the same quad the frame did. Invalid until
    /// the first frame, which leaves the ground on its scene-bounds
    /// sizing exactly once.
    Render::GroundCamera groundCam;
    /// The element gates (docs/SceneStreaming.md #13b), pushed in from
    /// the host -- the Gui bridge on the desktop, the URL parameters in
    /// the standalone viewer. Defaults are the pre-feature behaviour:
    /// draw every vertex, drop no edge, suppress nothing while loading.
    ///
    /// OUTSIDE the desktop guard below, and deliberately: the vertex
    /// gate is pure display -- it asks the producer's one-bit
    /// classification and whether the edges are on screen, and needs no
    /// budget, no level plan and no pressure state. That is worth more
    /// on a phone than on the desktop, where a point costs a 32-byte
    /// sprite instance record against 4 bytes in the heap. The edge
    /// gate rides along but stays dormant there, because it reads
    /// gpuOverBudget and the standalone tier's budget is its CPU half
    /// only -- a resident-payload and heap figure that cannot see the
    /// GPU buffers an edge draw would free. It arms itself the day that
    /// tier grows an uploaded-bytes meter (#13a), with no further wiring.
    bool shapeVerticesOn = true;
    bool pressureDropEdges = false;
    bool loadDropElements = false;
    /// What the gates suppressed in the last rendered frame, and how
    /// many drawables were eligible to be suppressed at all (classified
    /// attachedOnly by the producer). Reported with the level plan: a
    /// gate that cannot say whether it fired cannot be told apart from
    /// one that is not wired, and this workstream has already spent a
    /// session on exactly that confusion. `eligible` separates "the
    /// rule refused" from "nobody classified anything".
    size_t gatedPoints = 0, gatedLines = 0, gateEligible = 0;
    /// Of those, the ones the DEPENDENCY rule alone held back: a
    /// companion class that is late (its capture deferred) or itself
    /// gated. Pressure and the parameters account for the rest. It is
    /// the dependency half that a streaming tier has to carry
    /// objectIncomplete on the wire to reproduce.
    size_t gatedByDependency = 0;
    /// Of those, the ones whose object's FACES ARE STILL COARSE -- the
    /// contract's rule that an edge or vertex set describes the shape
    /// its faces approximate, so it waits for the exact rung. Nothing
    /// to do with memory, and the reason a user sees dots and edges
    /// over half-refined geometry when it is not enforced.
    size_t gatedByCoarse = 0;
    /// What Render_TinyElementCutoff suppressed this frame, kept apart
    /// from the contract's own counters so a measurement run cannot be
    /// read back as the contract having gated more (11.1i).
    size_t gatedTiny = 0, gatedTinyPrims = 0;
    /// THE AUDIT (see the gate walk): attached point and line draws
    /// this frame actually SUBMITS, and how many of them break the
    /// contract -- drawn with no face set in the scene at all
    /// (`auditNoFaces`, the display-mode exemption, legitimate only
    /// for a real Wireframe/Points object) or over faces still on a
    /// coarse rung (`auditCoarse`). Counting what the gate suppressed
    /// can never explain a dot that IS on screen; these can.
    size_t auditDrawn = 0, auditNoFaces = 0, auditCoarse = 0;
    /// Point/line draws whose mesh is NOT classified attached, and the
    /// share of those with no face set in the scene at all. The flag
    /// is false by default, so "floating" and "not classified yet"
    /// look identical here -- and the second one is a drawable that
    /// escapes every gate on the way in.
    size_t auditFloating = 0, auditFloatingNoFaces = 0;
    /// ...and how many of those actually carry geometry. A
    /// drawable published before its fill ran is submitted
    /// EMPTY: it paints nothing, and counting it as a visible
    /// dot turns a red herring into a diagnosis.
    size_t auditFloatingDrawn = 0;
    /// Last reported violation totals, so the report is edge-triggered:
    /// the failure being chased lasts a handful of frames.
    size_t auditSeenNoFaces = 0, auditSeenCoarse = 0, auditSeenFloating = 0;
    /// Likewise for the dependency rule, and it is the STREAMING tier
    /// this exists for. The desktop reports the counter in the level
    /// plan; the browser has no planner, so until this there was no
    /// line anywhere saying the rule had fired -- and a gate that
    /// cannot say whether it fired cannot be told apart from one that
    /// is not wired, which this workstream has already spent a session
    /// discovering. Edge-triggered like the audit beside it: a late
    /// companion is late for a handful of frames.
    /// Seeded to a value no tally can hold, so the FIRST frame always
    /// reports. An edge-triggered counter that starts equal to its own
    /// initial value says nothing at all when the answer is a flat zero
    /// -- and "the rule held nothing back" and "the classification
    /// never arrived" are exactly the two readings of a flat zero,
    /// which is the confusion `eligible` exists to end.
    static const size_t kNeverReported = size_t(-1);
    size_t gatedDepSeen = kNeverReported, gatedPointsSeen = kNeverReported,
        gatedLinesSeen = kNeverReported, gateEligibleSeen = kNeverReported;
    /// Last reported handle-pool refusal (BGFXView::tryUploadGeometry),
    /// edge-triggered like the gates above: exhaustion lasts as long as
    /// the scene is too big for the pool, and a line every frame would
    /// bury the moment it started. BOTH edges are reported -- silent
    /// geometry loss that silently stops is exactly as unreadable as
    /// the loss itself, and this counter exists because the condition
    /// used to announce itself once per process and never again.
    size_t bufferDeniedSeen = kNeverReported;
    /// Meshes whose every scene draw the gates suppressed this frame
    /// -- not submittable, so the collector must not keep them for
    /// being published (see collectMeshes). Rebuilt each frame by the
    /// gate walk; empty whenever no gate is active.
    std::unordered_set<uint64_t> gatedOnlyMeshes;
    // Whether the last rendered frame stood over the GPU budget. Out
    // here because the edge gate reads it; only the desktop half ever
    // writes it (the budget crossing, which also wakes the planner), so
    // in the standalone build it stays false and says so honestly.
    bool gpuOverBudget = false;
    // Whether the pressure controller still holds raised error
    // (levelPressure.raisedPx > 0) -- the desktop plan mirrors it here
    // because levelPressure is desktop-only while the edge gate is
    // shared code. The gate LATCHES on this rather than following
    // gpuOverBudget alone: once the collector retires gated edge
    // buffers the total falls under budget, and a gate keyed to the
    // instantaneous bit would re-open, re-upload the whole class, and
    // hand the wave its period back -- eviction and gate coupled
    // through the meter. The staircase's learned floor keeps this
    // true at the settled state, so the latch inherits the ladder's
    // own hysteresis instead of inventing a second one.
    bool pressureStanding = false;
    // The element contract's staged pressure latch (#13b): 0 = nothing
    // dropped, 1 = attached points dropped, 2 = attached lines dropped
    // too. Escalates points -> lines while over budget and releases
    // lines -> points once the pressure has fully cleared, one stage
    // per elemGateStagger frames, so the collector's census can answer
    // whether the cheaper stage was enough before the next is spent.
    int elemPressureStage = 0;
    int elemStageFrames = 0;
    int elemGateStagger = 15;
    /// Render_TinyElementCutoff: suppress line and point draws at or
    /// below this many primitives, contract or no contract. A
    /// MEASUREMENT instrument for the draw axis (11.1i), 0 = off.
    int tinyElementCutoff = 0;
    // The load gate as of the last frame, so the crossing can be
    // reported. The plan readout below cannot carry it: that prints on
    // a camera settle, and a load can begin and end entirely between
    // two settles -- the gate would do its whole job with nothing ever
    // saying it ran.
    bool loadDropSeen = false;
    /// Whether the last release attempt was refused on price, so the
    /// refusal is reported on its crossing rather than every frame.
    bool elemReleaseHeld = false;
    /// The same for the pressure latch, and for a sharper reason: the
    /// plan readout is the only other place the stage appears, and a
    /// settled ladder stops planning altogether -- so the release
    /// walks back over frames that print nothing at all.
    int elemStageSeen = 0;

    /// Whether the level plan narrates its decisions. Pushed in from
    /// the Gui bridge like the budget beside it -- this library knows
    /// nothing of RenderParams -- with the environment variable as the
    /// standalone viewer's way in. Out here with the gates it also
    /// reports, so the standalone build can narrate them too.
    bool levelDebug() const
    {
        static const bool env = std::getenv("FC_LEVEL_DEBUG") != nullptr;
        return levelDebugOn || env;
    }
    bool levelDebugOn = false;


#ifndef FC_RENDERER_STANDALONE
    // The desktop level plan's event half (§13 step 2): fed the camera
    // every rendered frame, fires the plan pass ~300ms after it
    // settles somewhere new. The policy half is planMeshRefines.
    Render::MeshLevelPlanner levelPlanner;
    // The last memory-ceiling epoch this view replanned for (§13
    // step 3) — a new observation marks the planner dirty.
    uint64_t levelCeilingSeen = 0;
    /// How much visible error the ladder is holding to fit its budget,
    /// and what it has learned about giving it back (sec 13c.3). The climb
    /// reads its tolerance from this, so both directions of the ladder
    /// agree on one -- see the plan callback, where the oscillation it
    /// prevents is measured.
    Render::PressureTolerance levelPressure;
    /// The budget the pressure controller's floor was learned under; a
    /// different one is a different question and forgets it.
    size_t levelBudgetSeen = 0;
    /// How much of the held error survives each plan that fits, pushed
    /// in from the bridge beside the budget (this library knows nothing
    /// of RenderParams). The default is the parameter's, so a viewer
    /// that never sets it still releases in steps rather than snapping.
    float levelPressureReleaseFrac = 0.5f;
    /// DowngradeLedger parameter: the downgrade sweep's in-flight credit
    /// (Render::DowngradeLedger on the view); off restores the
    /// storming behaviour for comparison.
    bool downgradeLedgerOn = true;
    /// ClimbHardLimit / ClimbAdmitBatch parameters: the budget is
    /// an absolute ceiling for climbs -- none admitted at or over it
    /// (in-flight ones aborted through the de-want pass), batched
    /// admission under it.
    bool climbHardLimitOn = true;
    int climbAdmitBatch = 64;
    /// DescentOrderBatch parameter: how many descents one plan pass may
    /// order (0 = uncapped); see planMeshDemotes' maxOrders.
    int descentOrderBatch = 64;
    /// LevelBudgetDeadband parameter: the rest band above the GPU
    /// budget, as a fraction of it, inside which the downgrade sweep
    /// does not trigger (it still corrects back to the budget when it
    /// does). See the plan callback for the dither it removes.
    float levelBudgetDeadband = 0.03f;
    // GPU geometry budget (setGpuMemoryBudget); 0 = automatic.
    size_t gpuBudget = 0;

    /// GPU geometry bytes in use: the API's own number where it
    /// reports one, else the upload accounting.
    static size_t gpuUsedBytes()
    {
        const bgfx::Stats *stats = _BGFXLib.deviceUp() ? bgfx::getStats() : nullptr;
        if (stats && stats->gpuMemoryUsed > 0)
            return size_t(stats->gpuMemoryUsed);
        return s_gpuGeometryBytes.load();
    }

    /// The effective GPU budget: the explicit one, else the API's
    /// reported maximum, else none.
    size_t gpuBudgetBytes() const
    {
        if (gpuBudget)
            return gpuBudget;
        const bgfx::Stats *stats = _BGFXLib.deviceUp() ? bgfx::getStats() : nullptr;
        if (stats && stats->gpuMemoryMax > 0)
            return size_t(stats->gpuMemoryMax);
        return 0;
    }
#endif
};

#endif // GUI_RENDERER_BGFXRENDERERP_H
