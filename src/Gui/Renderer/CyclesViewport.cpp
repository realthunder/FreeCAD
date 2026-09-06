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
 *                                                                         *
 ***************************************************************************/

// The Cycles viewport (docs/CyclesIntegration.md sec 5.2, step 11 of
// sec 8): a FrameConsumer around an interactive Cycles session. Only
// built with the engine (HAVE_CYCLES); the no-engine stub of
// Viewport::create lives in CyclesRenderer.cpp.

#include <Base/Console.h>

#include "CyclesRenderer.h"
#include "CyclesSceneP.h"
#include "FrameImageConsumer.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "device/device.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/pass.h"
#include "scene/scene.h"
#include "session/buffers.h"
#include "session/display_driver.h"
#include "session/session.h"
#include "util/half.h"
#include "util/thread.h"
#include "util/types.h"

namespace Render::Cycles {

namespace {

/// The staging buffer Cycles' threads fill (docs/CyclesIntegration.md
/// sec 5.2: "lock-and-copy into staging outside drawFrame"). One
/// buffer, sized to the full render, held under the mutex from
/// update_begin() to update_end() -- the render thread's copy into it
/// is a memcpy, so the host waits at most that long in take(). The
/// effective size (the progressive resolution divider applied) rides
/// with the buffer: Cycles fills the top-left width x height with
/// that width as its pitch, and that region is what the host uploads.
class StagingDriver : public ccl::DisplayDriver
{
public:
    /// Invoked from the render thread after every update; the host
    /// marshals it to its own thread and repaints.
    std::function<void()> redraw;

    bool update_begin(const Params &params, const int width, const int height) override
    {
        const size_t full = size_t(params.size.x) * size_t(params.size.y);
        if (full == 0 || width <= 0 || height <= 0)
            return false;
        update.lock();
        if (pixels.size() != full)
            pixels.assign(full, ccl::half4{});
        effWidth = width;
        effHeight = height;
        return true;
    }

    void update_end() override
    {
        dirty = true;
        update.unlock();
        if (redraw)
            redraw();
    }

    ccl::half4 *map_texture_buffer() override
    {
        return pixels.data();
    }

    void unmap_texture_buffer() override {}

    void next_tile_begin() override {}

    void zero() override
    {
        std::lock_guard<std::mutex> lock(update);
        std::fill(pixels.begin(), pixels.end(), ccl::half4{});
        dirty = true;
    }

    /// Session::draw() lands here on the host thread; the frame is
    /// consumed by take() instead, this only lets the session know a
    /// draw happened (its reset throttle counts them).
    void draw(const Params &) override {}

    /// Hand the staged frame to \a fn if a newer one arrived since the
    /// last take: (pixels, width, height) with width as the pitch.
    template <class F>
    void take(F &&fn)
    {
        std::lock_guard<std::mutex> lock(update);
        if (!dirty || effWidth <= 0 || effHeight <= 0)
            return;
        fn(pixels.data(), effWidth, effHeight);
        dirty = false;
    }

private:
    std::mutex update;
    std::vector<ccl::half4> pixels;
    int effWidth = 0;
    int effHeight = 0;
    bool dirty = false;
};

/// The one link a session's display driver keeps to the viewport that
/// owns it. A retired session (see retireSession) outlives its
/// viewport, and its render thread goes on staging frames until it
/// notices the cancel -- so the driver must not hold a raw `this`.
/// It holds this instead: closing the gate waits for any call already
/// inside it and turns every later one into nothing.
class RedrawGate
{
public:
    void set(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> lock(mutex);
        callback = std::move(fn);
    }

    /// Called from Cycles' threads. Runs under the lock, so close()
    /// returning means no thread is still in the callback.
    void call()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (callback)
            callback();
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mutex);
        callback = nullptr;
    }

private:
    std::mutex mutex;
    std::function<void()> callback;
};

/// A session on its way out, with the translator that states its scene
/// (which points into it, so the two travel together and go in that
/// order). \a token is whatever the viewport asked to outlive the
/// teardown -- a served stream's slot in the cap (sec 7.1) -- and is
/// dropped after the session is destroyed, never before.
struct RetiredSession {
    std::unique_ptr<ccl::Session> session;
    std::unique_ptr<SceneTranslator> translator;
    std::shared_ptr<void> token;
};

/// Destroying a Cycles session can block for MINUTES, and destroying
/// it on the thread that asked would be the GUI thread every time.
/// `~Session` cancels and then joins its own thread unconditionally,
/// and that thread may be inside a first-ever GPU kernel compile --
/// for CUDA a plain blocking `system("nvcc ...")` that
/// `Device::cancel()` does not interrupt (only the Metal backend
/// implements it). So a session is handed here instead: one worker
/// destroys them in the order they were retired, and the caller
/// returns at once. Everything a retired session still touches is its
/// own; the gate above is what stops it reaching its old viewport.
///
/// The mutex and condition variables are never freed. A detached
/// worker parked in wait() for the life of the process would otherwise
/// hang `pthread_cond_destroy` at exit (the trap fixed in
/// Part/Gui/MeshLevelSource.cpp, 7f09efdb7b).
std::mutex &s_retireMutex = *new std::mutex;
std::condition_variable &s_retireCv = *new std::condition_variable;
std::condition_variable &s_retireIdle = *new std::condition_variable;
std::deque<RetiredSession> s_retireQueue;
bool s_retireStarted = false;
bool s_retireBusy = false;

void retireSession(std::unique_ptr<ccl::Session> session,
                   std::unique_ptr<SceneTranslator> translator,
                   std::shared_ptr<void> token)
{
    if (!session && !translator)
        return;
    {
        std::lock_guard<std::mutex> lock(s_retireMutex);
        s_retireQueue.push_back(
            RetiredSession{std::move(session), std::move(translator), std::move(token)});
        if (!s_retireStarted) {
            s_retireStarted = true;
            std::thread([]() {
                for (;;) {
                    RetiredSession item;
                    {
                        std::unique_lock<std::mutex> lock(s_retireMutex);
                        s_retireCv.wait(lock, [] { return !s_retireQueue.empty(); });
                        item = std::move(s_retireQueue.front());
                        s_retireQueue.pop_front();
                        s_retireBusy = true;
                    }
                    // Unlocked, one at a time: this is where the
                    // minutes go, and two devices tearing down at once
                    // is not something to ask of a driver.
                    item.session.reset();
                    item.translator.reset();
                    // Last, and only now: the device is gone, so a
                    // slot this session held in the cap is free.
                    item.token.reset();
                    {
                        std::lock_guard<std::mutex> lock(s_retireMutex);
                        s_retireBusy = false;
                    }
                    s_retireIdle.notify_all();
                }
            }).detach();
        }
    }
    s_retireCv.notify_one();
}

class ViewportImpl : public Viewport
{
public:
    explicit ViewportImpl(const ViewportOptions &options)
        : options(options)
    {}

    ~ViewportImpl() override
    {
        // The callback first, so a render thread that is mid-update
        // finds nothing to call; then the session, to the reaper.
        setRedrawCallback(nullptr);
        retire();
    }

    void setScene(const SceneInput &input) override
    {
        const bool managed = input.output.transform == OutputConfig::SRGB;
        if (session && translator && translator->colorManaged() == managed
            && !session->progress.get_error()) {
            // The session stays: the translator restates its scene in
            // place under the scene's mutex (the session thread reads
            // the scene under it in its update), and the session is
            // reset only if that changed anything -- a restate that
            // touched only what the translation skips (an edge set, a
            // gizmo) leaves the render refining where it was.
            bool changed;
            {
                std::lock_guard<ccl::thread_mutex> lock(session->scene->mutex);
                RenderReport report;
                changed = translator->translate(input, report);
                status_.report = report;
            }
            camera = input.camera;
            pendingCamera = false;
            ++status_.updates;
            if (changed)
                session->reset(sessionParams, bufferParams(camera));
            return;
        }

        // A fresh session: the first scene, a session that failed, or
        // a colour-management flip (the translator decodes colours on
        // its way in, so that is a different translation). Costs the
        // device setup (a CUDA context) and a full translation. The
        // one it replaces is retired, not destroyed here, so a device
        // still loading its kernels does not hold up the new scene --
        // both hold their device memory until the old one is gone.
        retire();
        status_.error.clear();
        status_.report = RenderReport();
        pendingCamera = false;
        ++status_.sessions;

        ccl::SessionParams sp;
        std::string message;
        if (!makeSessionParams(options.device, options.samples, sp, message)) {
            status_.error = message;
            return;
        }
        // Interactive: the session thread outlives the render and waits
        // for the next reset instead of ending; the resolution divider
        // gives the coarse first pass on every restart.
        sp.background = false;
        sp.headless = false;
        sp.use_resolution_divider = true;
        sp.time_limit = options.timeLimit;
        sp.pixel_size = options.pixelSize > 0 ? options.pixelSize : 1;
        sessionParams = sp;

        ccl::SceneParams scp;
        session = std::make_unique<ccl::Session>(sp, scp);
        auto staging = std::make_unique<StagingDriver>();
        driver = staging.get();
        // Through a gate of this session's own: when the session is
        // retired the gate closes, and the render thread that outlives
        // this object finds nothing to call.
        gate = std::make_shared<RedrawGate>();
        gate->set([this] {
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                cb = redrawCallback;
            }
            if (cb)
                cb();
        });
        staging->redraw = [g = gate] { g->call(); };
        session->set_display_driver(std::move(staging));

        translator = std::make_unique<SceneTranslator>(session->scene.get(), managed);
        translator->translate(input, status_.report);

        ccl::Scene *scene = session->scene.get();
        if (options.denoise) {
            // The viewport's denoise is Blender's: fast quality and
            // prefilter, on the render device where OpenImageDenoise
            // supports it (Cycles falls back to its CPU denoiser
            // otherwise). The high quality the offline render would
            // want costs more per scheduled denoise than the samples
            // between them.
            scene->integrator->set_use_denoise(true);
            scene->integrator->set_denoiser_type(ccl::DENOISER_OPENIMAGEDENOISE);
            scene->integrator->set_denoiser_quality(ccl::DENOISER_QUALITY_FAST);
            scene->integrator->set_denoiser_prefilter(ccl::DENOISER_PREFILTER_FAST);
            scene->integrator->set_denoise_use_gpu(true);
        }
        ccl::Pass *pass = scene->create_node<ccl::Pass>();
        pass->set_name(ccl::ustring("combined"));
        pass->set_type(ccl::PASS_COMBINED);

        camera = input.camera;
        session->reset(sessionParams, bufferParams(camera));
        session->start();
    }

    void setCamera(const CameraInput &input) override
    {
        if (!session)
            return;
        if (!pendingCamera && sameCamera(input, camera))
            return;
        camera = input;
        pendingCamera = true;
        applyCamera();
    }

    void setRetireToken(std::shared_ptr<void> token) override
    {
        retireToken = std::move(token);
    }

    void setPaused(bool paused) override
    {
        if (session)
            session->set_pause(paused);
    }

    void setRedrawCallback(std::function<void()> callback) override
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        redrawCallback = std::move(callback);
    }

    ViewportStatus status() const override
    {
        ViewportStatus s = status_;
        s.running = session != nullptr;
        if (session) {
            s.progress = float(session->progress.get_progress());
            std::string st, sub;
            session->progress.get_status(st, sub);
            s.status = sub.empty() ? st : st + ", " + sub;
            if (session->progress.get_error())
                s.error = session->progress.get_error_message();
            s.complete = s.error.empty() && s.progress >= 1.0f;
        }
        return s;
    }

    unsigned framePasses() const override
    {
        return 1;
    }

    void drawFrame(DrawSurface &surface) override
    {
        if (!session || !driver)
            return;
        // The staged frame, if newer, into the blit's image (linear
        // half4, premultiplied); the blit uploads and draws it.
        driver->take([&](const ccl::half4 *px, int w, int h) {
            blit.setImage(px, w, h, FrameImageConsumer::Format::RGBA16F, true);
        });
        blit.drawFrame(surface);
        drawn();
    }

    bool takeFrame(
            const std::function<void(const void *half4, int width, int height)> &fn) override
    {
        if (!session || !driver)
            return false;
        bool taken = false;
        driver->take([&](const ccl::half4 *px, int w, int h) {
            fn(px, w, h);
            taken = true;
        });
        drawn();
        return taken;
    }

private:
    static ccl::BufferParams bufferParams(const CameraInput &cam)
    {
        ccl::BufferParams bp;
        bp.width = cam.width;
        bp.height = cam.height;
        bp.full_width = cam.width;
        bp.full_height = cam.height;
        return bp;
    }

    static bool sameCamera(const CameraInput &a, const CameraInput &b)
    {
        return a.width == b.width && a.height == b.height
            && std::memcmp(a.view, b.view, sizeof(a.view)) == 0
            && std::memcmp(a.proj, b.proj, sizeof(a.proj)) == 0;
    }

    void applyCamera()
    {
        if (!session || !translator || !session->ready_to_reset())
            return;
        bool moved;
        {
            // The session thread reads the scene under this lock in
            // its update; the camera is restated under it too.
            std::lock_guard<ccl::thread_mutex> lock(session->scene->mutex);
            moved = translator->translateCamera(camera);
            // A projection-kind change invalidates the background's
            // ortho camera-ray fan; rebuild it before the reset.
            translator->refreshWorld();
        }
        if (moved)
            session->reset(sessionParams, bufferParams(camera));
        pendingCamera = false;
    }

    /// The session's own draw accounting: it allows the next reset
    /// only once a frame was drawn after the last one, which is what
    /// keeps a camera drag from cancelling every render before a
    /// pixel shows (Blender's throttle). A camera move held back by
    /// that is applied here, now that a frame went out.
    void drawn()
    {
        session->draw();
        if (pendingCamera)
            applyCamera();
    }

    /// Hand the session and its translator to the reaper. The gate
    /// closes first, which waits for a redraw already in flight and
    /// stops every later one, so nothing the retired session does can
    /// reach this object once this returns.
    void retire()
    {
        if (gate) {
            gate->close();
            gate.reset();
        }
        driver = nullptr;
        // A COPY of the token: a viewport that swaps sessions in place
        // retires more than one, and each has to hold the slot.
        retireSession(std::move(session), std::move(translator), retireToken);
    }

    ViewportOptions options;
    std::shared_ptr<void> retireToken;  ///< outlives every session this
                                        ///< viewport retires (sec 7.1)
    ccl::SessionParams sessionParams;
    std::unique_ptr<ccl::Session> session;
    std::unique_ptr<SceneTranslator> translator;
    StagingDriver *driver = nullptr;  ///< owned by the session
    std::shared_ptr<RedrawGate> gate;  ///< also held by the session's driver
    ViewportStatus status_;
    CameraInput camera;
    bool pendingCamera = false;

    std::mutex callbackMutex;
    std::function<void()> redrawCallback;

    FrameImageConsumer blit;
};

}  // namespace

void waitForRetiredSessions()
{
    std::unique_lock<std::mutex> lock(s_retireMutex);
    if (s_retireQueue.empty() && !s_retireBusy)
        return;
    // Said out loud, because this is where a quit can sit for minutes:
    // a GPU kernel compile cannot be interrupted, and letting the
    // process unload the engine from under the worker is the worse
    // answer.
    Base::Console().Message("Cycles: waiting for a render session to shut down"
                            " (an uninterruptible kernel compile may be running)\n");
    const auto start = std::chrono::steady_clock::now();
    s_retireIdle.wait(lock, [] { return s_retireQueue.empty() && !s_retireBusy; });
    Base::Console().Message(
            "Cycles: render session shut down after %.1fs\n",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}

std::unique_ptr<Viewport> Viewport::create(const ViewportOptions &options, std::string *error)
{
    initEngine();
    ccl::SessionParams sp;
    std::string message;
    if (!makeSessionParams(options.device, options.samples > 0 ? options.samples : 1, sp,
                           message)) {
        if (error)
            *error = message;
        return nullptr;
    }
    if (options.samples < 1) {
        if (error)
            *error = "samples must be positive";
        return nullptr;
    }
    return std::make_unique<ViewportImpl>(options);
}

}  // namespace Render::Cycles
