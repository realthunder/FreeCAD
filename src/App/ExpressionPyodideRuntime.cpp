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

#include "PreCompiled.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <thread>

#include <libplatform/libplatform.h>
#include <v8-array-buffer.h>
#include <v8-container.h>
#include <v8-context.h>
#include <v8-exception.h>
#include <v8-external.h>
#include <v8-function.h>
#include <v8-initialization.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-message.h>
#include <v8-microtask-queue.h>
#include <v8-object.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-promise.h>
#include <v8-script.h>
#include <v8-typed-array.h>

#include <Base/Console.h>
#include <Base/FileInfo.h>

#include "Application.h"
#include "ExpressionImageRuntime.h"
#include "ExpressionPyodide.h"

// The two JavaScript files compiled in (embed_js.py at build time):
// the Web-environment shim and the boot/call glue.  See
// docs/PyodideHost.md; they are the security policy, so they live in
// the binary, not beside it.
#include "PyodideShim.inc"
#include "PyodideGlue.inc"

/* The pyodide runtime: pyodide (CPython on wasm32-emscripten) inside a
 * bare V8 from the v8-embed package, with this file supplying ONLY the
 * environment pyodide's glue reads -- a file reader and a module loader
 * both scoped to the pyodide directory, timers, text codecs, random
 * bytes, console -- and the guest image loaded from a wheel beside the
 * pyodide files.  What is not supplied is the mechanism: the guest has
 * no filesystem, network, process or loader to name.
 * docs/PyodideHost.md is the design record; src/App/PyodideHost/ holds
 * the shim, the glue, the standalone probe and the guest build.
 */

FC_LOG_LEVEL_INIT("ExpressionImage", true, true)

namespace fs = std::filesystem;

namespace App
{
namespace ExpressionSandbox
{

namespace
{

std::string envPath(const char* name)
{
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : std::string();
}

std::string toStd(v8::Isolate* isolate, v8::Local<v8::Value> v)
{
    if (v.IsEmpty())
        return "<empty>";
    v8::String::Utf8Value s(isolate, v);
    return *s ? std::string(*s, s.length()) : "<unprintable>";
}

v8::Local<v8::String> str(v8::Isolate* isolate, const std::string& s)
{
    return v8::String::NewFromUtf8(isolate, s.c_str(), v8::NewStringType::kNormal,
                                   static_cast<int>(s.size()))
        .ToLocalChecked();
}

double nowMs()
{
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double, std::milli>(steady_clock::now() - t0).count();
}

bool readFile(const fs::path& p, std::string& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

/// V8 is initialised once per process and never torn down: a second
/// InitializePlatform is undefined behaviour, and the engine outliving
/// its last isolate costs nothing.
v8::Platform* platform()
{
    static v8::Platform* p = [] {
        std::unique_ptr<v8::Platform> made = v8::platform::NewDefaultPlatform();
        v8::V8::InitializePlatform(made.get());
        v8::V8::Initialize();
        return made.release();
    }();
    return p;
}

class PyodideRuntime: public ImageRuntime
{
public:
    ~PyodideRuntime() override
    {
        teardown();
    }

    const char* name() const override
    {
        return "pyodide";
    }

    /** image = the fcx_image wheel, stdlib = the pyodide directory,
     * packages = the user's package set (docs/PyodideHost.md sec 12).
     *
     * The pyodide directory, in order: explicit configure(), the
     * preference PyodideDir, the FCX_PYODIDE environment, the runtime
     * the bootstrap installed under the user's data directory (the one
     * its `current` marker names), then <datadir>/Pyodide -- a dev tree
     * mirrors the npm directory there, and a package may choose to
     * bundle one.
     *
     * The wheel: explicit, PyodideWheel, FCX_PYODIDE_WHEEL, an
     * fcx_image-*.whl beside the runtime (the dev layout), else the
     * shipped one under <datadir>/Pyodide/wheels whose ABI tag matches
     * the runtime's.  The packages directory comes from the layout.
     */
    Paths resolve(const std::string& image, const std::string& stdlib) const override
    {
        Paths p;
        p.image = image;
        p.stdlib = stdlib;
        Pyodide::Layout layout = Pyodide::layout();
        p.packages = layout.packages;
        if (p.stdlib.empty() || p.image.empty()) {
            auto hGrp = GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Expression/Sandbox");
            if (p.stdlib.empty())
                p.stdlib = hGrp->GetASCII("PyodideDir", "");
            if (p.image.empty())
                p.image = hGrp->GetASCII("PyodideWheel", "");
            if (p.stdlib.empty())
                p.stdlib = envPath("FCX_PYODIDE");
            if (p.image.empty())
                p.image = envPath("FCX_PYODIDE_WHEEL");
            if (p.stdlib.empty() && !layout.current.empty())
                p.stdlib = (fs::path(layout.userDir) / layout.current).string();
            if (p.stdlib.empty())
                p.stdlib = App::Application::getResourceDir() + "Pyodide";
        }
        if (p.image.empty()) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(p.stdlib, ec)) {
                const std::string fn = entry.path().filename().string();
                if (fn.rfind("fcx_image-", 0) == 0 && fn.size() > 4
                        && fn.compare(fn.size() - 4, 4, ".whl") == 0) {
                    p.image = entry.path().string();
                    break;
                }
            }
            if (p.image.empty())
                p.image = Pyodide::wheelForAbi(Pyodide::directoryAbi(p.stdlib));
            if (p.image.empty())
                p.image = (fs::path(p.stdlib) / "fcx_image.whl").string();
        }
        return p;
    }

    bool initialize(const Paths& paths, BridgeFn bridgeFn) override
    {
        bridge = std::move(bridgeFn);
        std::error_code ec;
        root = fs::weakly_canonical(paths.stdlib, ec);
        if (ec || !fs::is_regular_file(root / "pyodide.js") || !fs::is_regular_file(root / "pyodide.asm.wasm")) {
            FC_LOG("no pyodide at " << paths.stdlib << ", sandbox runtime unavailable");
            return false;
        }
        if (root.filename().empty() && root.has_parent_path())
            root = root.parent_path();

        // The allowlist rule (docs/PyodideHost.md sec 5): only a pinned
        // release boots, and only with the files it was pinned with.  A
        // developer bringing up a NEW release sets FCX_PYODIDE_UNPINNED
        // (or the PyodideUnpinned preference) and is told so on every
        // boot, until the table is widened.
        {
            auto hGrp = GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Expression/Sandbox");
            const bool unpinned = hGrp->GetBool("PyodideUnpinned", false)
                || !envPath("FCX_PYODIDE_UNPINNED").empty();
            const std::string reason = Pyodide::verifyDirectory(root.string());
            if (!reason.empty()) {
                if (!unpinned) {
                    FC_ERR("pyodide runtime refused: " << reason
                           << " (FCX_PYODIDE_UNPINNED=1 overrides, for development only)");
                    return false;
                }
                FC_WARN("pyodide runtime UNPINNED: " << reason);
            }
        }

        // What the guest's reader may open, canonical and nothing else:
        // the runtime, the directory the wheel is in, and the package set.
        roots.clear();
        roots.push_back(root.string());
        fs::path wheel = fs::weakly_canonical(paths.image, ec);
        if (ec || !fs::is_regular_file(wheel)) {
            FC_LOG("no fcx_image wheel at " << paths.image << " for the pyodide at " << root.string());
            return false;
        }
        roots.push_back(wheel.parent_path().string());
        fs::path packagesDir;
        packagesRoot.clear();
        if (!paths.packages.empty()) {
            packagesDir = fs::weakly_canonical(paths.packages, ec);
            if (!ec && fs::is_directory(packagesDir)) {
                if (packagesDir.filename().empty() && packagesDir.has_parent_path())
                    packagesDir = packagesDir.parent_path();
                roots.push_back(packagesDir.string());
                packagesRoot = packagesDir.string();
            }
            else
                packagesDir.clear();
        }
        const std::string abi = Pyodide::directoryAbi(root.string());
        {
            const std::string tag = "pyodide_" + abi + "_wasm32";
            if (!abi.empty() && wheel.filename().string().find(tag) == std::string::npos) {
                FC_ERR("fcx_image wheel " << wheel.filename().string()
                       << " does not match the pyodide ABI " << abi << " of " << root.string());
                return false;
            }
        }
        // Bundled wheels first (FreeCAD's own workbench code packed for
        // the guest, docs/Sandbox.md sec 5.6), by absolute path; their
        // directories join the reader's roots.  Then the user's package
        // set by lock-file name.
        std::vector<std::string> packageNames;
        size_t bundledCount = 0;
        std::vector<std::string> bundledAll = Pyodide::layout().bundled;
        for (const auto& b : Pyodide::layout().bundledCompiled)
            if (b.first == abi)
                bundledAll.push_back(b.second);
        for (const auto& b : bundledAll) {
            fs::path w = fs::weakly_canonical(b, ec);
            if (ec || !fs::is_regular_file(w))
                continue;
            const std::string dir = w.parent_path().string();
            if (std::find(roots.begin(), roots.end(), dir) == roots.end())
                roots.push_back(dir);
            packageNames.push_back(w.generic_string());
            ++bundledCount;
        }
        if (!packagesDir.empty()) {
            for (const auto& n : Pyodide::manifestPackages(packagesDir.string(), abi))
                packageNames.push_back(n);
        }

        platform();
        v8::Isolate::CreateParams params;
        allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        params.array_buffer_allocator = allocator;
        isolate = v8::Isolate::New(params);
        isolate->SetData(0, this);
        isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        isolate->SetHostImportModuleDynamicallyCallback(importDynamically);
        isolate->SetHostInitializeImportMetaObjectCallback(initImportMeta);
        isolate->SetPromiseRejectCallback(onPromiseReject);

        // Everything that enters the isolate is in this lambda, so that a
        // failed boot tears down AFTER the scopes have exited: disposing
        // an isolate a thread is still entered in is a V8 fatal.
        const bool booted = [&]() -> bool {
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> ctx = v8::Context::New(isolate);
        v8::Context::Scope contextScope(ctx);
        context.Reset(isolate, ctx);

        // 1. the native surface, then the shim that shapes it
        v8::Local<v8::Object> h = v8::Object::New(isolate);
        auto put = [&](v8::Local<v8::Object> target, const char* fname, v8::FunctionCallback cb) {
            target->Set(ctx, str(isolate, fname),
                        v8::Function::New(ctx, cb, v8::External::New(isolate, this)).ToLocalChecked())
                .Check();
        };
        put(h, "readText", hostReadText);
        put(h, "readBytes", hostReadBytes);
        put(h, "randomBytes", hostRandomBytes);
        put(h, "now", hostNow);
        put(h, "print", hostPrint);
        ctx->Global()->Set(ctx, str(isolate, "__fcx_host"), h).Check();
        if (!run(std::string(reinterpret_cast<const char*>(kPyodideShim), kPyodideShim_len),
                 "host_shim.js")) {
            return false;
        }

        // 2. pyodide.js, then the glue that boots and calls it
        std::string loader;
        if (!readFile(root / "pyodide.js", loader)
                || !run(loader, (root / "pyodide.js").string())) {
            return false;
        }
        if (!run(std::string(reinterpret_cast<const char*>(kPyodideGlue), kPyodideGlue_len),
                 "pyodide_glue.js")) {
            return false;
        }
        // The guest's two "env" imports (ImageBridge.cpp), which the
        // glue merges into the main module's symbol table before the
        // wheel loads; a bridge op is one wasm import call landing here.
        put(ctx->Global(), "__fcx_host_call", hostCall);
        put(ctx->Global(), "__fcx_host_fetch", hostFetch);

        // 3. boot: ~1.5 s, once per session.  Paths cross as generic
        // (forward-slash) strings: pyodide's loader does URL arithmetic
        // on them, and the reader on the way back accepts either form.
        const double t0 = nowMs();
        std::string names = "[";
        for (size_t i = 0; i < packageNames.size(); ++i)
            names += (i ? ", " : "") + jsString(packageNames[i]);
        names += "]";
        std::string boot = "__fcx_boot(" + jsString(root.generic_string() + "/") + ", "
            + jsString(wheel.generic_string()) + ", "
            + jsString(packagesDir.empty() ? std::string() : packagesDir.generic_string() + "/")
            + ", " + names + ")";
        v8::Local<v8::Value> result;
        if (!run(boot, "boot", &result) || toStd(isolate, result) != "booted") {
            FC_ERR("pyodide boot failed");
            return false;
        }
        // The guest's exports and its emscripten Module, from the glue:
        // host->guest calls go straight to the wasm functions from here.
        {
            v8::Local<v8::Value> link;
            if (!run("__fcx_link()", "link", &link) || !link->IsObject()) {
                FC_ERR("pyodide glue left no guest link");
                return false;
            }
            v8::Local<v8::Object> l = link.As<v8::Object>();
            auto take = [&](const char* key, v8::Global<v8::Function>& into) {
                v8::Local<v8::Value> v;
                if (!l->Get(ctx, str(isolate, key)).ToLocal(&v) || !v->IsFunction())
                    return false;
                into.Reset(isolate, v.As<v8::Function>());
                return true;
            };
            v8::Local<v8::Value> mod;
            if (!take("alloc", allocFn) || !take("free", freeFn) || !take("call", callFn)
                    || !l->Get(ctx, str(isolate, "module")).ToLocal(&mod) || !mod->IsObject()) {
                FC_ERR("pyodide guest link is incomplete");
                return false;
            }
            emModule.Reset(isolate, mod.As<v8::Object>());
            heapKey.Reset(isolate, str(isolate, "HEAPU8"));
        }

        // 4. the soft stage of the budget: pyodide's interrupt buffer,
        // an Int32Array the interpreter polls from its eval loop
        // (CPython's Py_EMSCRIPTEN_SIGNAL_HANDLING; a non-zero value is
        // taken as a signal number and raised at the next bytecode
        // check).  Its storage is THIS object's `signal` word, wrapped as
        // a SharedArrayBuffer so the watchdog thread writes it with no
        // engine involvement -- exactly how a browser worker is
        // interrupted from the main thread.  Installed by roundTrip only
        // while there is a budget: the polling costs ~5 us per
        // expression.
        {
            std::unique_ptr<v8::BackingStore> store = v8::SharedArrayBuffer::NewBackingStore(
                    &signal, sizeof(signal), [](void*, size_t, void*) {}, nullptr);
            v8::Local<v8::SharedArrayBuffer> sab =
                v8::SharedArrayBuffer::New(isolate, std::shared_ptr<v8::BackingStore>(std::move(store)));
            interruptArray.Reset(isolate, v8::Int32Array::New(sab, 0, 1));
            v8::Local<v8::Value> setter;
            if (!ctx->Global()->Get(ctx, str(isolate, "__fcx_setInterrupt")).ToLocal(&setter)
                    || !setter->IsFunction()) {
                FC_ERR("pyodide glue left no __fcx_setInterrupt");
                return false;
            }
            interruptSetter.Reset(isolate, setter.As<v8::Function>());
        }
        live = true;
        FC_LOG("sandbox runtime live: pyodide at " << root.string() << " with "
               << wheel.filename().string() << ", " << bundledCount << " bundled wheel(s) and "
               << (packageNames.size() - bundledCount) << " package(s) ("
               << (int)(nowMs() - t0) << " ms)");
        return true;
        }();
        if (!booted) {
            teardown();
            return false;
        }
        return true;
    }

    void setBudget(int budget, int grace) override
    {
        budgetMs = budget;
        graceMs = grace;
    }

    Outcome roundTrip(const std::vector<uint8_t>& request,
                      std::vector<uint8_t>& reply) override
    {
        if (!live)
            return Outcome::Failed;
        // Nested inside a bridge op (rung 2: a guest hook's write runs a
        // host hook whose Proxy is in the guest again): the Locker is
        // re-entrant and a wasm export may be called from a native
        // callback; the budget, the interrupt signal and the microtask
        // checkpoint belong to the outermost call only.
        struct Depth
        {
            int& d;
            explicit Depth(int& v)
                : d(v)
            {
                ++d;
            }
            ~Depth()
            {
                --d;
            }
        } depthGuard(depth);
        const bool outer = depth == 1;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> ctx = context.Get(isolate);
        v8::Context::Scope contextScope(ctx);
        v8::TryCatch tc(isolate);

        // The request into wasm memory at a guest-allocated address:
        // fcx_alloc, a copy, then fcx_call(ptr, len) -- the reactor's
        // shape (ImageMain.cpp), called as wasm exports from here.
        v8::Local<v8::Value> undef = v8::Undefined(isolate);
        v8::Local<v8::Value> lenArg =
            v8::Integer::NewFromUnsigned(isolate, static_cast<uint32_t>(request.size()));
        v8::Local<v8::Value> ptrArg;
        if (!allocFn.Get(isolate)->Call(ctx, undef, 1, &lenArg).ToLocal(&ptrArg)) {
            report(tc, "fcx_alloc");
            return Outcome::Failed;
        }
        const uint32_t ptr = ptrArg->Uint32Value(ctx).FromMaybe(0);
        {
            size_t size = 0;
            uint8_t* mem = guestMemory(size);
            if (!mem || (!ptr && !request.empty())
                    || static_cast<uint64_t>(ptr) + request.size() > size) {
                FC_ERR("pyodide guest: request buffer out of range");
                return Outcome::Failed;
            }
            if (!request.empty())
                std::memcpy(mem + ptr, request.data(), request.size());
        }
        v8::Local<v8::Value> callArgs[2] = {ptrArg, lenArg};
        v8::Local<v8::Value> result;
        if ((budgetMs > 0) != interruptOn) {
            // toggle the interpreter's polling with the budget
            v8::Local<v8::Value> arg = budgetMs > 0 ? interruptArray.Get(isolate).As<v8::Value>()
                                                    : v8::Null(isolate).As<v8::Value>();
            if (interruptSetter.Get(isolate)->Call(ctx, ctx->Global(), 1, &arg).IsEmpty()) {
                report(tc, "interrupt buffer");
                return Outcome::Failed;
            }
            interruptOn = budgetMs > 0;
        }
        if (outer) {
            signal.store(0);
            if (budgetMs > 0)
                watchdog.arm(budgetMs, budgetMs + std::max(graceMs, 0));
        }
        const bool called = callFn.Get(isolate)->Call(ctx, undef, 2, callArgs).ToLocal(&result);
        const int fired = outer ? watchdog.disarm() : -1;
        // A soft signal the guest did not get to consume must not greet
        // the next call (the guest zeroes it when it does consume it).
        if (outer)
            signal.store(0);
        if (!called) {
            if (tc.HasTerminated()) {
                // nested: the outer frame is terminating too and cancels
                if (outer) {
                    isolate->CancelTerminateExecution();
                    FC_WARN("pyodide guest terminated after " << (budgetMs + std::max(graceMs, 0))
                            << " ms; the runtime is dropped");
                }
                return Outcome::Terminated;
            }
            report(tc, "fcx call");
            return Outcome::Failed;
        }
        guestFree(ctx, ptrArg);
        // The reply: u32 length + CBOR bytes at the returned address,
        // read after re-taking the memory (the call may have grown it).
        const uint32_t rp = result->Uint32Value(ctx).FromMaybe(0);
        size_t size = 0;
        uint8_t* mem = guestMemory(size);
        if (!rp || !mem || static_cast<uint64_t>(rp) + 4 > size) {
            FC_ERR("pyodide guest returned no reply buffer");
            return Outcome::Failed;
        }
        uint32_t n = 0;
        std::memcpy(&n, mem + rp, 4);
        if (static_cast<uint64_t>(rp) + 4 + n > size) {
            FC_ERR("pyodide guest reply out of range");
            guestFree(ctx, result);
            return Outcome::Failed;
        }
        reply.assign(mem + rp + 4, mem + rp + 4 + n);
        guestFree(ctx, result);
        // Let the guest's own housekeeping (proxy finalizers, deferred
        // work) run now rather than pile up -- outermost only, never
        // from inside a bridge op's native frame.
        if (outer)
            isolate->PerformMicrotaskCheckpoint();
        return fired >= 0 ? Outcome::Interrupted : Outcome::Ok;
    }

    void teardown() override
    {
        if (isolate) {
            {
                v8::Locker locker(isolate);
                v8::Isolate::Scope isolateScope(isolate);
                v8::HandleScope handleScope(isolate);
                if (!context.IsEmpty() && live) {
                    v8::Local<v8::Context> ctx = context.Get(isolate);
                    v8::Context::Scope contextScope(ctx);
                    v8::Local<v8::Value> fn;
                    if (ctx->Global()->Get(ctx, str(isolate, "__fcx_teardown")).ToLocal(&fn)
                            && fn->IsFunction()) {
                        v8::TryCatch tc(isolate);
                        fn.As<v8::Function>()->Call(ctx, ctx->Global(), 0, nullptr).IsEmpty();
                    }
                }
                allocFn.Reset();
                freeFn.Reset();
                callFn.Reset();
                emModule.Reset();
                heapKey.Reset();
                pendingReply.clear();
                interruptSetter.Reset();
                interruptArray.Reset();
                interruptOn = false;
                context.Reset();
                modules.clear();
                modulePaths.clear();
                unhandled.clear();
                roots.clear();
                packagesRoot.clear();
            }
            isolate->Dispose();
            isolate = nullptr;
        }
        delete allocator;
        allocator = nullptr;
        live = false;
    }

private:
    bool live = false;
    BridgeFn bridge;
    fs::path root;
    /// What the reader and the loader may open (Pyodide::scopePath):
    /// the runtime directory, the wheel's, the package set's.
    std::vector<std::string> roots;
    /// The package set, where a bare wheel name the loader asks for is
    /// looked up after the runtime directory (empty when there is none).
    std::string packagesRoot;
    int budgetMs = 0;
    int graceMs = 0;
    /// The interrupt buffer's one word (ExpressionImageRuntime.h,
    /// Outcome): 2 = SIGINT, which the guest raises as KeyboardInterrupt.
    std::atomic<int32_t> signal {0};
    Watchdog watchdog {[this](int stage) {
        if (stage == 0)
            signal.store(2);
        else if (isolate)
            isolate->TerminateExecution();
    }};
    v8::Isolate* isolate = nullptr;
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Global<v8::Context> context;
    /// The guest side module's exports (ImageModule.cpp), called as
    /// wasm functions; its emscripten Module, whose HEAPU8 is the
    /// current view of wasm memory (replaced on growth, so re-read per
    /// use and never held across guest code).
    v8::Global<v8::Function> allocFn;
    v8::Global<v8::Function> freeFn;
    v8::Global<v8::Function> callFn;
    v8::Global<v8::Object> emModule;
    v8::Global<v8::String> heapKey;
    /// The reply pending between the host_call and host_fetch halves of
    /// one bridge op (ExpressionWasmtimeRuntime.cpp has the same).
    std::vector<uint8_t> pendingReply;
    /// round trips in flight (2 or more: nested inside a bridge op)
    int depth = 0;
    v8::Global<v8::Function> interruptSetter;
    v8::Global<v8::Value> interruptArray;
    bool interruptOn = false;
    std::mt19937_64 rng {std::random_device {}()};
    std::map<std::string, v8::Global<v8::Module>> modules;
    std::map<int, std::string> modulePaths;
    std::vector<std::pair<v8::Global<v8::Promise>, std::string>> unhandled;

    static PyodideRuntime* self(v8::Isolate* isolate)
    {
        return static_cast<PyodideRuntime*>(isolate->GetData(0));
    }
    static PyodideRuntime* self(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        return static_cast<PyodideRuntime*>(info.Data().As<v8::External>()->Value());
    }

    static std::string jsString(const std::string& s)
    {
        std::string out = "\"";
        for (char c : s) {
            if (c == '\\' || c == '"')
                out += '\\';
            out += c;
        }
        return out + "\"";
    }

    /// A path the guest named, canonicalised and confined to one of the
    /// roots (or empty); relative paths resolve against `base`.  The
    /// rules -- `file://`, drive letters, `..`, symlinks, sibling
    /// prefixes -- are Pyodide::scopePath's, tested on their own.
    fs::path scope(const std::string& p, const fs::path& base) const
    {
        return fs::path(Pyodide::scopePath(roots, p, base.string(), {packagesRoot}));
    }

    static void throwError(v8::Isolate* isolate, const std::string& msg)
    {
        isolate->ThrowException(v8::Exception::Error(str(isolate, msg)));
    }

    void report(v8::TryCatch& tc, const char* what)
    {
        v8::HandleScope scope(isolate);
        if (tc.HasTerminated()) {
            FC_ERR("pyodide " << what << ": execution terminated");
            return;
        }
        std::string text = toStd(isolate, tc.Exception());
        v8::Local<v8::Value> stack;
        if (!tc.Exception().IsEmpty() && tc.Exception()->IsObject()
                && tc.Exception().As<v8::Object>()->Get(context.Get(isolate), str(isolate, "stack")).ToLocal(&stack)
                && stack->IsString())
            text = toStd(isolate, stack);
        FC_ERR("pyodide " << what << ": " << text);
    }

    // ------------------------------------------------------------ natives

    static void hostRead(const v8::FunctionCallbackInfo<v8::Value>& info, bool binary)
    {
        v8::Isolate* isolate = info.GetIsolate();
        PyodideRuntime* rt = self(info);
        if (info.Length() < 1 || !info[0]->IsString())
            return throwError(isolate, "read: path must be a string");
        const std::string asked = toStd(isolate, info[0]);
        fs::path p = rt->scope(asked, rt->root);
        if (p.empty())
            return throwError(isolate, "read: refused, outside the pyodide directories: " + asked);
        std::string data;
        if (!readFile(p, data))
            return throwError(isolate, "read: cannot open " + p.string());
        if (!binary) {
            info.GetReturnValue().Set(str(isolate, data));
            return;
        }
        v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, data.size());
        if (!data.empty())
            std::memcpy(ab->Data(), data.data(), data.size());
        info.GetReturnValue().Set(ab);
    }
    static void hostReadText(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        hostRead(info, false);
    }
    static void hostReadBytes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        hostRead(info, true);
    }

    static void hostRandomBytes(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        PyodideRuntime* rt = self(info);
        const double n = info.Length() > 0
            ? info[0]->NumberValue(isolate->GetCurrentContext()).FromMaybe(-1)
            : -1;
        if (!(n >= 0 && n <= 65536))
            return throwError(isolate, "randomBytes: refused size");
        v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, static_cast<size_t>(n));
        auto* out = static_cast<unsigned char*>(ab->Data());
        for (size_t i = 0; i < static_cast<size_t>(n); ++i)
            out[i] = static_cast<unsigned char>(rt->rng());
        info.GetReturnValue().Set(ab);
    }

    static void hostNow(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        info.GetReturnValue().Set(nowMs());
    }

    static void hostPrint(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        const std::string kind = info.Length() > 0 ? toStd(isolate, info[0]) : "out";
        const std::string text = info.Length() > 1 ? toStd(isolate, info[1]) : "";
        if (kind == "err")
            FC_WARN("pyodide: " << text);
        else
            FC_LOG("pyodide: " << text);
    }

    /// Wasm memory as the guest sees it now: Module.HEAPU8's buffer.
    /// Valid until the guest runs again (growth detaches it), so taken
    /// per use; nullptr when the link is gone.
    uint8_t* guestMemory(size_t& size)
    {
        size = 0;
        if (emModule.IsEmpty())
            return nullptr;
        v8::Local<v8::Context> ctx = context.Get(isolate);
        v8::Local<v8::Value> heap;
        if (!emModule.Get(isolate)->Get(ctx, heapKey.Get(isolate)).ToLocal(&heap)
                || !heap->IsUint8Array())
            return nullptr;
        v8::Local<v8::ArrayBuffer> ab = heap.As<v8::Uint8Array>()->Buffer();
        size = ab->ByteLength();
        return static_cast<uint8_t*>(ab->Data());
    }

    /// fcx_free on a guest pointer; a failure is logged, not fatal.
    void guestFree(v8::Local<v8::Context> ctx, v8::Local<v8::Value> ptr)
    {
        v8::TryCatch tc(isolate);
        if (freeFn.Get(isolate)->Call(ctx, v8::Undefined(isolate), 1, &ptr).IsEmpty())
            report(tc, "fcx_free");
    }

    /// The guest's only way out, first half: the "env" import
    /// fcx_host_call(ptr, len) -> reply length.  The request is read out
    /// of wasm memory in place and dispatched; the reply is parked for
    /// the fetch.  Negative on any failure, and never a throw: the
    /// guest turns the sign into a Python exception.
    static void hostCall(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        PyodideRuntime* rt = self(info);
        info.GetReturnValue().Set(-1);
        if (info.Length() < 2 || !rt->bridge)
            return;
        v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
        const uint32_t ptr = info[0]->Uint32Value(ctx).FromMaybe(0);
        const uint32_t len = info[1]->Uint32Value(ctx).FromMaybe(0);
        size_t size = 0;
        const uint8_t* mem = rt->guestMemory(size);
        if (!mem || static_cast<uint64_t>(ptr) + len > size)
            return;
        rt->pendingReply = rt->bridge(mem + ptr, len);
        info.GetReturnValue().Set(static_cast<int32_t>(rt->pendingReply.size()));
    }

    /// Second half: fcx_host_fetch(dst, cap) copies the parked reply
    /// into the guest's own buffer and returns its length; negative when
    /// there is none or it does not fit.
    static void hostFetch(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        PyodideRuntime* rt = self(info);
        info.GetReturnValue().Set(-1);
        if (info.Length() < 2 || rt->pendingReply.empty())
            return;
        v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
        const uint32_t dst = info[0]->Uint32Value(ctx).FromMaybe(0);
        const uint32_t cap = info[1]->Uint32Value(ctx).FromMaybe(0);
        size_t size = 0;
        uint8_t* mem = rt->guestMemory(size);
        if (!mem || static_cast<uint64_t>(dst) + cap > size || cap < rt->pendingReply.size())
            return;
        std::memcpy(mem + dst, rt->pendingReply.data(), rt->pendingReply.size());
        info.GetReturnValue().Set(static_cast<int32_t>(rt->pendingReply.size()));
        rt->pendingReply.clear();
    }

    // ------------------------------------------------------------- loader
    // The only module loader this runtime has: files under the pyodide
    // directory, nothing else.  Shell-mode pyodide loads its emscripten
    // glue with import(); every other specifier is refused.

    v8::MaybeLocal<v8::Module> loadModule(v8::Local<v8::Context> ctx,
                                          const std::string& specifier,
                                          const fs::path& referrerDir)
    {
        fs::path p = scope(specifier, referrerDir);
        if (p.empty() || !fs::is_regular_file(p)) {
            throwError(isolate, "import: refused: " + specifier);
            return {};
        }
        auto it = modules.find(p.string());
        if (it != modules.end())
            return it->second.Get(isolate);
        std::string code;
        if (!readFile(p, code)) {
            throwError(isolate, "import: cannot open " + p.string());
            return {};
        }
        v8::ScriptOrigin origin(str(isolate, p.string()), 0, 0, false, -1, v8::Local<v8::Value>(),
                                false, false, /*is_module=*/true);
        v8::ScriptCompiler::Source source(str(isolate, code), origin);
        v8::Local<v8::Module> module;
        if (!v8::ScriptCompiler::CompileModule(isolate, &source).ToLocal(&module))
            return {};
        (void)ctx;
        modules[p.string()].Reset(isolate, module);
        modulePaths[module->GetIdentityHash()] = p.string();
        return module;
    }

    fs::path moduleDir(v8::Local<v8::Module> referrer)
    {
        auto it = modulePaths.find(referrer->GetIdentityHash());
        return it == modulePaths.end() ? root : fs::path(it->second).parent_path();
    }

    static v8::MaybeLocal<v8::Module> resolveStatic(v8::Local<v8::Context> ctx,
                                                    v8::Local<v8::String> specifier,
                                                    v8::Local<v8::FixedArray>,
                                                    v8::Local<v8::Module> referrer)
    {
        PyodideRuntime* rt = self(v8::Isolate::GetCurrent());
        return rt->loadModule(ctx, toStd(rt->isolate, specifier), rt->moduleDir(referrer));
    }

    static void resolveWithNamespace(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
        v8::Local<v8::Array> data = info.Data().As<v8::Array>();
        auto resolver = data->Get(ctx, 0).ToLocalChecked().As<v8::Promise::Resolver>();
        v8::Local<v8::Value> ns = data->Get(ctx, 1).ToLocalChecked();
        resolver->Resolve(ctx, ns).Check();
    }
    static void rejectWith(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
        auto resolver = info.Data().As<v8::Promise::Resolver>();
        resolver->Reject(ctx, info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>()).Check();
    }

    static v8::MaybeLocal<v8::Promise> importDynamically(v8::Local<v8::Context> ctx,
                                                         v8::Local<v8::Data>,
                                                         v8::Local<v8::Value> resourceName,
                                                         v8::Local<v8::String> specifier,
                                                         v8::Local<v8::FixedArray>)
    {
        v8::Isolate* isolate = v8::Isolate::GetCurrent();
        PyodideRuntime* rt = self(isolate);
        v8::EscapableHandleScope scope(isolate);
        auto resolver = v8::Promise::Resolver::New(ctx).ToLocalChecked();

        fs::path referrerDir = rt->root;
        if (!resourceName.IsEmpty() && resourceName->IsString()) {
            fs::path rp = rt->scope(toStd(isolate, resourceName), rt->root);
            if (!rp.empty())
                referrerDir = rp.parent_path();
        }

        v8::TryCatch tc(isolate);
        v8::Local<v8::Module> module;
        auto fail = [&](const char* what) {
            resolver->Reject(ctx, tc.HasCaught() ? tc.Exception()
                                                 : v8::Exception::Error(str(isolate, what)))
                .Check();
            return scope.Escape(resolver->GetPromise());
        };
        if (!rt->loadModule(ctx, toStd(isolate, specifier), referrerDir).ToLocal(&module)
                || module->GetStatus() == v8::Module::kErrored
                || (module->GetStatus() == v8::Module::kUninstantiated
                    && module->InstantiateModule(ctx, resolveStatic).IsNothing()))
            return fail("import failed");
        if (module->GetStatus() == v8::Module::kEvaluated) {
            resolver->Resolve(ctx, module->GetModuleNamespace()).Check();
            return scope.Escape(resolver->GetPromise());
        }
        v8::Local<v8::Value> evalResult;
        if (!module->Evaluate(ctx).ToLocal(&evalResult))
            return fail("evaluate failed");
        v8::Local<v8::Promise> evalPromise = evalResult.As<v8::Promise>();
        v8::Local<v8::Array> data = v8::Array::New(isolate, 2);
        data->Set(ctx, 0, resolver).Check();
        data->Set(ctx, 1, module->GetModuleNamespace()).Check();
        v8::Local<v8::Function> onOk = v8::Function::New(ctx, resolveWithNamespace, data).ToLocalChecked();
        v8::Local<v8::Function> onErr = v8::Function::New(ctx, rejectWith, resolver).ToLocalChecked();
        evalPromise->Then(ctx, onOk, onErr).ToLocalChecked();
        return scope.Escape(resolver->GetPromise());
    }

    static void initImportMeta(v8::Local<v8::Context> ctx, v8::Local<v8::Module> module,
                               v8::Local<v8::Object> meta)
    {
        v8::Isolate* isolate = v8::Isolate::GetCurrent();
        PyodideRuntime* rt = self(isolate);
        auto it = rt->modulePaths.find(module->GetIdentityHash());
        const std::string url = "file://" + (it == rt->modulePaths.end() ? std::string() : it->second);
        meta->CreateDataProperty(ctx, str(isolate, "url"), str(isolate, url)).Check();
    }

    // A rejection is unhandled only if nobody attaches a handler afterwards
    // (import() rejects inside the callback, before the caller's .then()).
    static void onPromiseReject(v8::PromiseRejectMessage msg)
    {
        v8::Isolate* isolate = v8::Isolate::GetCurrent();
        PyodideRuntime* rt = self(isolate);
        if (msg.GetEvent() == v8::kPromiseRejectWithNoHandler) {
            rt->unhandled.emplace_back(v8::Global<v8::Promise>(isolate, msg.GetPromise()),
                                       toStd(isolate, msg.GetValue()));
        }
        else if (msg.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
            for (auto it = rt->unhandled.begin(); it != rt->unhandled.end(); ++it)
                if (it->first == msg.GetPromise()) {
                    rt->unhandled.erase(it);
                    break;
                }
        }
    }

    void reportUnhandled()
    {
        for (auto& u : unhandled)
            FC_WARN("pyodide: unhandled rejection: " << u.second);
        unhandled.clear();
    }

    // --------------------------------------------------------- event loop
    // Pump platform tasks (an async WebAssembly compile completes through
    // the foreground queue), run microtasks, fire the shim's due timers,
    // until `done` or a bounded idle.

    bool pump(const std::function<bool()>& done, double timeoutMs = 120000)
    {
        v8::Local<v8::Context> ctx = context.Get(isolate);
        const double deadline = nowMs() + timeoutMs;
        double idleSince = -1;
        for (;;) {
            bool platformWork = false;
            while (v8::platform::PumpMessageLoop(platform(), isolate,
                                                 v8::platform::MessageLoopBehavior::kDoNotWait))
                platformWork = true;
            isolate->PerformMicrotaskCheckpoint();
            if (done())
                return true;
            double next = -1;
            v8::Local<v8::Value> fnv;
            if (ctx->Global()->Get(ctx, str(isolate, "__fcx_runTimers")).ToLocal(&fnv) && fnv->IsFunction()) {
                v8::TryCatch tc(isolate);
                v8::Local<v8::Value> r;
                if (fnv.As<v8::Function>()->Call(ctx, ctx->Global(), 0, nullptr).ToLocal(&r))
                    next = r->NumberValue(ctx).FromMaybe(-1);
                else
                    report(tc, "timer");
            }
            isolate->PerformMicrotaskCheckpoint();
            reportUnhandled();
            if (done())
                return true;
            if (nowMs() > deadline) {
                FC_ERR("pyodide: timed out after " << (int)timeoutMs << " ms");
                return false;
            }
            if (next < 0 && !platformWork) {
                if (idleSince < 0)
                    idleSince = nowMs();
                else if (nowMs() - idleSince > 10000) {
                    FC_ERR("pyodide: idle with a pending promise for 10 s: deadlock");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            idleSince = -1;
            if (next > 0)
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(std::min(next, 50.0))));
        }
    }

    /// Runs `code` as a classic script, awaiting a promise result.
    bool run(const std::string& code, const std::string& name, v8::Local<v8::Value>* out = nullptr)
    {
        v8::Local<v8::Context> ctx = context.Get(isolate);
        v8::EscapableHandleScope scope(isolate);
        v8::TryCatch tc(isolate);
        v8::ScriptOrigin origin(str(isolate, name));
        v8::ScriptCompiler::Source source(str(isolate, code), origin);
        v8::Local<v8::Script> script;
        if (!v8::ScriptCompiler::Compile(ctx, &source).ToLocal(&script)) {
            report(tc, name.c_str());
            return false;
        }
        v8::Local<v8::Value> result;
        if (!script->Run(ctx).ToLocal(&result)) {
            report(tc, name.c_str());
            return false;
        }
        if (result->IsPromise()) {
            v8::Local<v8::Promise> p = result.As<v8::Promise>();
            if (!pump([&] { return p->State() != v8::Promise::kPending; }))
                return false;
            if (p->State() == v8::Promise::kRejected) {
                v8::Local<v8::Value> reason = p->Result();
                std::string text = toStd(isolate, reason);
                v8::Local<v8::Value> stack;
                if (reason->IsObject()
                        && reason.As<v8::Object>()->Get(ctx, str(isolate, "stack")).ToLocal(&stack)
                        && stack->IsString())
                    text = toStd(isolate, stack);
                FC_ERR("pyodide " << name << ": " << text);
                p->MarkAsHandled();
                return false;
            }
            result = p->Result();
        }
        if (out)
            *out = scope.Escape(result);
        return true;
    }
};

}  // namespace

std::unique_ptr<ImageRuntime> makePyodideRuntime()
{
    return std::make_unique<PyodideRuntime>();
}

}  // namespace ExpressionSandbox
}  // namespace App
