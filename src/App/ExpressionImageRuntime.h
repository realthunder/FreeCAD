/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 **************************************************************************/

#ifndef APP_EXPRESSION_IMAGE_RUNTIME_H
#define APP_EXPRESSION_IMAGE_RUNTIME_H

/* The transport under ImageHost: what carries a CBOR request into the
 * sandbox guest and its CBOR reply back, and what the guest's mid-eval
 * bridge ops travel through in the other direction.  Everything above
 * this line -- the handle table, the bindings pack, the bridge dispatch,
 * result decoding -- is runtime-agnostic and lives in ImageHost; a
 * runtime is only the pipe and the guest it is attached to.
 *
 * Two runtimes exist (docs/PyodideHost.md, docs/ExpressionImage.md):
 *   - V8 + pyodide, running the image slice as a pyodide extension wheel
 *     (fcx_image-*.whl) beside a pyodide distribution: THE runtime, the
 *     shipping default, and the only one that grows;
 *   - wasmtime, running the wasm32-wasi image (fcx_image.wasm + a stdlib
 *     slice): the REFERENCE implementation since 2026-09-03
 *     (docs/SandboxNetwork.md sec 0), built only with
 *     BUILD_EXPR_WASI_RUNTIME.
 * Which one ImageHost instantiates is the preference
 * BaseApp/Preferences/Expression/Sandbox:Runtime ("pyodide" or "wasi").
 *
 * Internal to the App library: not installed, not part of the API.
 */

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace App
{
namespace ExpressionSandbox
{

/** How one round trip ended, as the runtime saw it.
 *
 * The budget (setBudget) has two stages.  The SOFT stage asks the guest
 * to stop on its own terms -- a signal the interpreter turns into a
 * KeyboardInterrupt at its next bytecode check, so the exception travels
 * the ordinary path and the guest is left exactly as consistent as after
 * any other error.  The HARD stage, a grace period later, stops the
 * engine from outside (V8's TerminateExecution, wasmtime's epoch trap):
 * that unwinds the guest through frames CPython never got to clean up,
 * so a Terminated runtime is not trusted afterwards; ImageHost drops it
 * and the next evaluation starts a fresh one.
 */
enum class Outcome
{
    Ok,           ///< reply valid, nothing fired
    Interrupted,  ///< reply valid, the soft stage fired during the trip
    Terminated,   ///< no reply: the hard stage stopped the guest
    Failed        ///< no reply: a transport failure unrelated to the budget
};

/** A deadline thread shared by the runtimes.  arm() before a call,
 * disarm() after; `fire(stage)` runs on the watchdog thread at the soft
 * deadline (stage 0, only when softMs > 0) and again at the hard
 * deadline (stage 1), and the whole of fire() runs under the same lock
 * disarm() takes, so a stale deadline can never reach the NEXT call: it
 * either fired before disarm() (which then reports it) or finds itself
 * disarmed.  fire() must therefore be quick and must not call back into
 * the watchdog.
 */
class Watchdog
{
public:
    explicit Watchdog(std::function<void(int stage)> fire)
        : fire(std::move(fire))
    {}

    ~Watchdog()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            quit = true;
            armed = false;
        }
        cv.notify_all();
        if (thread.joinable())
            thread.join();
    }

    /// Start the clock.  hardMs <= 0 leaves the call unbounded.
    void arm(int softMs, int hardMs)
    {
        std::lock_guard<std::mutex> lock(mutex);
        fired = -1;
        if (hardMs <= 0) {
            armed = false;
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        soft = softMs > 0 && softMs < hardMs ? now + std::chrono::milliseconds(softMs)
                                              : now + std::chrono::milliseconds(hardMs);
        hard = now + std::chrono::milliseconds(hardMs);
        softStage = softMs > 0 && softMs < hardMs;
        armed = true;
        ++generation;
        if (!thread.joinable())
            thread = std::thread([this] { run(); });
        // A wake-up costs ~2 us of futex traffic per call, so the thread
        // is only nudged when its pending timed wait ends AFTER the new
        // first deadline; a thread already due earlier finds the new
        // generation when it wakes and re-waits (once per budget period,
        // not once per call).
        if (waitingUntil > soft)
            cv.notify_all();
    }

    /// Stop the clock; the highest stage that fired, or -1.
    int disarm()
    {
        std::lock_guard<std::mutex> lock(mutex);
        armed = false;
        return fired;
    }

private:
    using Clock = std::chrono::steady_clock;
    std::function<void(int)> fire;
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool quit = false;
    bool armed = false;
    bool softStage = false;
    int fired = -1;
    uint64_t generation = 0;
    Clock::time_point soft;
    Clock::time_point hard;
    /// When the thread's current wait ends on its own (max = never).
    Clock::time_point waitingUntil = Clock::time_point::max();

    void run()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!quit) {
            if (!armed) {
                waitingUntil = Clock::time_point::max();
                cv.wait(lock, [this] { return quit || armed; });
                continue;
            }
            const uint64_t gen = generation;
            const bool waitingSoft = softStage && fired < 0;
            const Clock::time_point deadline = waitingSoft ? soft : hard;
            waitingUntil = deadline;
            if (cv.wait_until(lock, deadline) == std::cv_status::no_timeout)
                continue;  // re-armed, disarmed or quitting: re-evaluate
            if (!armed || gen != generation)
                continue;
            if (waitingSoft) {
                fired = 0;
                fire(0);
                continue;
            }
            fired = 1;
            armed = false;  // the hard stage ends the call
            fire(1);
        }
    }
};

class ImageRuntime
{
public:
    virtual ~ImageRuntime() = default;

    /// One guest->host bridge op: CBOR request bytes in, CBOR reply out.
    /// ImageHost supplies it; the runtime calls it from inside a round
    /// trip when the guest reaches back.
    using BridgeFn = std::function<std::vector<uint8_t>(const uint8_t*, std::size_t)>;

    /// The paths a runtime resolves for itself (ImageHost::Location).
    struct Paths
    {
        std::string image;     ///< fcx_image.wasm, or the fcx_image wheel
        std::string stdlib;    ///< the stdlib slice dir, or the pyodide dir
        std::string cache;     ///< compiled-form cache, when the runtime has one
        std::string packages;  ///< the user's package set, when the runtime loads one
    };

    /// The runtime's name as the preference spells it.
    virtual const char* name() const = 0;

    /// Where this runtime's guest files are, given the explicit
    /// configuration (both empty when nothing was configured).
    virtual Paths resolve(const std::string& image, const std::string& stdlib) const = 0;

    /// Bring the guest up.  False (with the reason logged) when the
    /// files are missing or the guest fails to start.
    virtual bool initialize(const Paths& paths, BridgeFn bridge) = 0;

    /// The time budget of the NEXT round trips: the soft stage at
    /// `budgetMs`, the hard stage `graceMs` later (see Outcome).  A
    /// runtime without a soft mechanism fires the hard stage at
    /// budgetMs + graceMs.  budgetMs <= 0 leaves calls unbounded.
    virtual void setBudget(int budgetMs, int graceMs) = 0;

    /// One request/reply round trip.  `reply` is valid for Ok and
    /// Interrupted only.  After Failed (and Terminated) ImageHost drops
    /// the runtime.
    virtual Outcome roundTrip(const std::vector<uint8_t>& request,
                              std::vector<uint8_t>& reply) = 0;

    virtual void teardown() = 0;
};

#ifdef FC_EXPR_WASI_RUNTIME
/// The wasm32-wasi reference image under wasmtime
/// (ExpressionWasmtimeRuntime.cpp).
std::unique_ptr<ImageRuntime> makeWasmtimeRuntime();
#endif

#ifdef FC_EXPR_PYODIDE_HOST
/// pyodide on the bare V8 of v8-embed (ExpressionPyodideRuntime.cpp).
std::unique_ptr<ImageRuntime> makePyodideRuntime();
#endif

}  // namespace ExpressionSandbox
}  // namespace App

#endif  // APP_EXPRESSION_IMAGE_RUNTIME_H
