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
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <wasm.h>
#include <wasmtime.h>

#include <Base/Console.h>
#include <Base/FileInfo.h>

#include "Application.h"
#include "ExpressionImageRuntime.h"

/* The wasmtime runtime: the wasm32-wasi image (src/App/ExpressionImage,
 * built by wasi-sdk) instantiated in-process with one preopened
 * directory, its stdlib slice.  Moved here unchanged from ImageHost when
 * the pyodide runtime arrived; docs/ExpressionImage.md describes it.
 */

FC_LOG_LEVEL_INIT("ExpressionImage", true, true)

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

/** Where the compiled form of `imagePath` is cached.
 *
 * NOT beside the image: an installed data directory is read-only for the
 * user who runs FreeCAD, and the compiled form is specific to the wasmtime
 * build and the host CPU, so it can never be shipped either -- it has to be
 * made once per user.  The file name carries a hash of the image's full
 * path because a box can have several (a build tree and an install) whose
 * base names are identical, and the staleness check is a timestamp: a
 * collision there would deserialize the wrong module.
 */
std::string cachePathFor(const std::string& imagePath)
{
    uint64_t hash = 1469598103934665603ULL;  // FNV-1a, 64 bit
    for (unsigned char c : imagePath) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream name;
    name << Base::FileInfo(imagePath).fileNamePure() << '-' << std::hex << hash
         << ".cwasm";
    return App::Application::getUserCachePath() + "ExpressionSandbox/"
            + name.str();
}

class WasmtimeRuntime: public ImageRuntime
{
public:
    ~WasmtimeRuntime() override
    {
        teardown();
    }

    const char* name() const override
    {
        return "wasi";
    }

    /** Precedence: an explicit configure() (tests and headless flags) wins,
     * then the preference, then the FCX_IMAGE / FCX_STDLIB environment (a
     * developer pointing at a build tree), then the bundle a packaged
     * FreeCAD installs as <datadir>/Fcx.  A path that resolves to nothing
     * is not an error: the sandbox is then unavailable and evaluation
     * stays in process.
     */
    Paths resolve(const std::string& image, const std::string& stdlib) const override
    {
        Paths p;
        p.image = image;
        p.stdlib = stdlib;
        if (p.image.empty() || p.stdlib.empty()) {
            auto hGrp = GetApplication().GetParameterGroupByPath(
                    "User parameter:BaseApp/Preferences/Expression/Sandbox");
            if (p.image.empty())
                p.image = hGrp->GetASCII("ImagePath", "");
            if (p.stdlib.empty())
                p.stdlib = hGrp->GetASCII("StdlibPath", "");
            if (p.image.empty())
                p.image = envPath("FCX_IMAGE");
            if (p.stdlib.empty())
                p.stdlib = envPath("FCX_STDLIB");
            std::string bundle = App::Application::getResourceDir() + "Fcx/";
            if (p.image.empty())
                p.image = bundle + "fcx_image.wasm";
            if (p.stdlib.empty())
                p.stdlib = bundle + "Lib";
        }
        p.cache = cachePathFor(p.image);
        return p;
    }

    bool initialize(const Paths& paths, BridgeFn bridgeFn) override
    {
        bridge = std::move(bridgeFn);
        const std::string& imagePath = paths.image;
        const std::string& stdlibPath = paths.stdlib;
        if (!Base::FileInfo(imagePath).isFile()
                || !Base::FileInfo(stdlibPath).isDir()) {
            FC_LOG("no image at " << imagePath << " with a stdlib at "
                   << stdlibPath << ", sandbox image unavailable");
            return false;
        }

        wasm_config_t* cfg = wasm_config_new();
        wasmtime_config_wasm_exceptions_set(cfg, true);
        // The hard stage of the time budget (ExpressionImageRuntime.h,
        // Outcome): the guest checks the engine's epoch at loop and call
        // boundaries and traps once it passes the store's deadline; the
        // watchdog thread advances the epoch.  The checks are compiled
        // into the guest and cost ~5 us on a 13 us expression, so an
        // instance started with no budget does without them (a budget
        // set later takes effect at the next reset).  With the option on
        // every store traps at once until it is given a deadline, hence
        // the unbounded one below.
        epochChecks = budgetMs > 0;
        wasmtime_config_epoch_interruption_set(cfg, epochChecks);
        engine = wasm_engine_new_with_config(cfg);
        store = wasmtime_store_new(engine, nullptr, nullptr);
        context = wasmtime_store_context(store);
        if (epochChecks)
            wasmtime_context_set_epoch_deadline(context, kUnboundedTicks);

        wasi_config_t* wasi = wasi_config_new();
        wasi_config_inherit_stderr(wasi);
        if (!wasi_config_preopen_dir(wasi, stdlibPath.c_str(), "/Lib", false)) {
            FC_ERR("cannot preopen stdlib " << stdlibPath);
            teardown();
            return false;
        }
        wasmtime_error_t* err = wasmtime_context_set_wasi(context, wasi);
        if (err) {
            FC_ERR("wasi setup failed: " << errorText(err, nullptr));
            teardown();
            return false;
        }

        // JIT-compiling the image costs ~600 ms; a serialized .cwasm
        // loads in ~20 ms.  The cache lives in the user cache directory
        // (see cachePathFor) and is refreshed whenever it is older than
        // the image or fails to deserialize (wasmtime version change).
        std::string cachePath = paths.cache.empty() ? cachePathFor(imagePath) : paths.cache;
        // the two compilations differ, and one must not evict the other
        if (epochChecks)
            cachePath.insert(cachePath.size() - std::strlen(".cwasm"), "-budget");
        Base::FileInfo imageInfo(imagePath);
        Base::FileInfo cacheInfo(cachePath);
        if (cacheInfo.exists()
                && cacheInfo.lastModified() >= imageInfo.lastModified()) {
            err = wasmtime_module_deserialize_file(engine, cachePath.c_str(),
                                                   &module);
            if (err) {
                FC_LOG("stale image cache " << cachePath << ": "
                       << errorText(err, nullptr));
                module = nullptr;
            }
        }
        if (!module) {
            std::ifstream f(imagePath, std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());
            if (bytes.empty()) {
                FC_ERR("cannot read image " << imagePath);
                teardown();
                return false;
            }
            err = wasmtime_module_new(engine, bytes.data(), bytes.size(),
                                      &module);
            if (err) {
                FC_ERR("image does not compile: " << errorText(err, nullptr));
                teardown();
                return false;
            }
            wasm_byte_vec_t blob;
            Base::FileInfo cacheDir(App::Application::getUserCachePath()
                                    + "ExpressionSandbox");
            if ((cacheDir.isDir() || cacheDir.createDirectory())
                    && !wasmtime_module_serialize(module, &blob)) {
                std::ofstream out(cachePath, std::ios::binary);
                if (out)
                    out.write(blob.data, (std::streamsize)blob.size);
                wasm_byte_vec_delete(&blob);
            }
        }

        wasmtime_linker_t* linker = wasmtime_linker_new(engine);
        err = wasmtime_linker_define_wasi(linker);
        wasm_trap_t* trap = nullptr;
        if (!err && !defineBridge(linker)) {
            wasmtime_linker_delete(linker);
            teardown();
            return false;
        }
        if (!err)
            err = wasmtime_linker_instantiate(linker, context, module,
                                              &instance, &trap);
        wasmtime_linker_delete(linker);
        if (err || trap) {
            FC_ERR("image instantiation failed: " << errorText(err, trap));
            teardown();
            return false;
        }

        wasmtime_extern_t ext;
        if (!getExport("memory", WASMTIME_EXTERN_MEMORY, ext)) {
            teardown();
            return false;
        }
        memory = ext.of.memory;

        wasmtime_func_t funcInitialize {};
        wasmtime_func_t funcInit {};
        struct
        {
            const char* name;
            wasmtime_func_t* slot;
        } funcs[] = {
            {"_initialize", &funcInitialize},
            {"fcx_init", &funcInit},
            {"fcx_alloc", &funcAlloc},
            {"fcx_free", &funcFree},
            {"fcx_call", &funcCall},
        };
        for (auto& fn : funcs) {
            if (!getExport(fn.name, WASMTIME_EXTERN_FUNC, ext)) {
                teardown();
                return false;
            }
            *fn.slot = ext.of.func;
        }

        if (!callSimple(funcInitialize, nullptr, 0, nullptr, 0, "_initialize")) {
            teardown();
            return false;
        }
        wasmtime_val_t result;
        if (!callSimple(funcInit, nullptr, 0, &result, 1, "fcx_init")
                || result.of.i32 != 0) {
            FC_ERR("fcx_init failed (rc "
                   << (result.kind == WASMTIME_I32 ? result.of.i32 : -1) << ")");
            teardown();
            return false;
        }

        live = true;
        FC_LOG("sandbox image live: " << imagePath);
        return true;
    }

    void setBudget(int budget, int grace) override
    {
        budgetMs = budget;
        graceMs = grace;
    }

    /// One fcx_call round trip: request CBOR in, reply CBOR out.  This
    /// runtime has no soft stage (the wasi CPython has no interrupt
    /// buffer to poll), so the budget is one hard deadline at
    /// budget + grace, and a trip that hits it ends the instance.
    Outcome roundTrip(const std::vector<uint8_t>& request,
                      std::vector<uint8_t>& reply) override
    {
        if (!live)
            return Outcome::Failed;
        // write the request into guest memory
        wasmtime_val_t args[2], result;
        args[0].kind = WASMTIME_I32;
        args[0].of.i32 = (int32_t)request.size();
        if (!callSimple(funcAlloc, args, 1, &result, 1, "fcx_alloc"))
            return Outcome::Failed;
        uint32_t guestPtr = (uint32_t)result.of.i32;
        uint8_t* mem = wasmtime_memory_data(context, &memory);
        size_t memSize = wasmtime_memory_data_size(context, &memory);
        if (!guestPtr || guestPtr + request.size() > memSize) {
            FC_ERR("guest allocation out of range");
            return Outcome::Failed;
        }
        std::memcpy(mem + guestPtr, request.data(), request.size());

        args[0].kind = WASMTIME_I32;
        args[0].of.i32 = (int32_t)guestPtr;
        args[1].kind = WASMTIME_I32;
        args[1].of.i32 = (int32_t)request.size();
        const int hardMs = epochChecks && budgetMs > 0 ? budgetMs + std::max(graceMs, 0) : 0;
        if (hardMs > 0) {
            // trap on the first epoch tick; only the watchdog ticks
            wasmtime_context_set_epoch_deadline(context, 1);
            watchdog.arm(0, hardMs);
        }
        bool ok = callSimple(funcCall, args, 2, &result, 1, "fcx_call");
        const int fired = hardMs > 0 ? watchdog.disarm() : -1;
        // A tick that landed as the call was returning must not trap the
        // bookkeeping calls below.
        if (hardMs > 0)
            wasmtime_context_set_epoch_deadline(context, kUnboundedTicks);
        if (!ok && fired >= 0) {
            FC_WARN("sandbox image terminated after " << hardMs
                    << " ms; the instance is dropped");
            return Outcome::Terminated;
        }
        // free the request buffer regardless
        wasmtime_val_t freeArg;
        freeArg.kind = WASMTIME_I32;
        freeArg.of.i32 = (int32_t)guestPtr;
        callSimple(funcFree, &freeArg, 1, nullptr, 0, "fcx_free");
        if (!ok)
            return Outcome::Failed;

        uint32_t replyPtr = (uint32_t)result.of.i32;
        mem = wasmtime_memory_data(context, &memory);  // may have moved
        memSize = wasmtime_memory_data_size(context, &memory);
        if (!replyPtr || replyPtr + 4 > memSize) {
            FC_ERR("bad reply pointer");
            return Outcome::Failed;
        }
        uint32_t replyLen = 0;
        std::memcpy(&replyLen, mem + replyPtr, 4);
        if (replyPtr + 4 + replyLen > memSize) {
            FC_ERR("bad reply length");
            return Outcome::Failed;
        }
        reply.assign(mem + replyPtr + 4, mem + replyPtr + 4 + replyLen);
        freeArg.of.i32 = (int32_t)replyPtr;
        callSimple(funcFree, &freeArg, 1, nullptr, 0, "fcx_free");
        return Outcome::Ok;
    }

    void teardown() override
    {
        if (module) {
            wasmtime_module_delete(module);
            module = nullptr;
        }
        if (store) {
            wasmtime_store_delete(store);
            store = nullptr;
            context = nullptr;
        }
        if (engine) {
            wasm_engine_delete(engine);
            engine = nullptr;
        }
        live = false;
    }

private:
    /// An epoch deadline no watchdog reaches (the epoch only ever moves
    /// by one per fired call).
    static constexpr uint64_t kUnboundedTicks = uint64_t(1) << 40;

    bool live = false;
    BridgeFn bridge;
    int budgetMs = 0;
    int graceMs = 0;
    bool epochChecks = false;  ///< compiled in at initialize() when budgetMs > 0
    Watchdog watchdog {[this](int) {
        if (engine)
            wasmtime_engine_increment_epoch(engine);
    }};

    wasm_engine_t* engine = nullptr;
    wasmtime_store_t* store = nullptr;
    wasmtime_context_t* context = nullptr;
    wasmtime_module_t* module = nullptr;
    wasmtime_instance_t instance {};
    wasmtime_memory_t memory {};
    wasmtime_func_t funcAlloc {};
    wasmtime_func_t funcFree {};
    wasmtime_func_t funcCall {};

    // the reply pending between the host_call and host_fetch halves of
    // one bridge op
    std::vector<uint8_t> pendingReply;

    static std::string errorText(wasmtime_error_t* err, wasm_trap_t* trap)
    {
        wasm_byte_vec_t text;
        text.data = nullptr;
        text.size = 0;
        if (err) {
            wasmtime_error_message(err, &text);
            wasmtime_error_delete(err);
        }
        else if (trap) {
            wasm_trap_message(trap, &text);
            wasm_trap_delete(trap);
        }
        std::string s(text.data ? text.data : "", text.size);
        if (text.data)
            wasm_byte_vec_delete(&text);
        return s;
    }

    bool getExport(const char* name, wasmtime_extern_kind_t kind,
                   wasmtime_extern_t& ext)
    {
        if (!wasmtime_instance_export_get(context, &instance, name,
                                          std::strlen(name), &ext)
                || ext.kind != kind) {
            FC_ERR("image is missing export '" << name << "'");
            return false;
        }
        return true;
    }

    bool callSimple(wasmtime_func_t& func, wasmtime_val_t* args, size_t nargs,
                    wasmtime_val_t* results, size_t nresults, const char* what)
    {
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* err = wasmtime_func_call(context, &func, args, nargs,
                                                   results, nresults, &trap);
        if (err || trap) {
            FC_ERR(what << " failed: " << errorText(err, trap));
            return false;
        }
        return true;
    }

    /// The host_call half of one bridge op: decode the request out of guest
    /// memory, dispatch it, park the reply, return only its length (never
    /// re-enter the guest; the image fetches with its own buffer).  -1
    /// signals a transport-level failure.
    static wasm_trap_t* hostCallCb(void* env, wasmtime_caller_t* caller,
                                   const wasmtime_val_t* args, size_t nargs,
                                   wasmtime_val_t* results, size_t nresults)
    {
        auto* self = static_cast<WasmtimeRuntime*>(env);
        if (nresults < 1)
            return nullptr;
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = -1;
        wasmtime_extern_t ext;
        if (nargs < 2
                || !wasmtime_caller_export_get(caller, "memory", 6, &ext)
                || ext.kind != WASMTIME_EXTERN_MEMORY)
            return nullptr;
        wasmtime_context_t* ctx = wasmtime_caller_context(caller);
        wasmtime_memory_t mem = ext.of.memory;
        const uint8_t* data = wasmtime_memory_data(ctx, &mem);
        size_t memSize = wasmtime_memory_data_size(ctx, &mem);
        uint64_t ptr = (uint32_t)args[0].of.i32;
        uint64_t len = (uint32_t)args[1].of.i32;
        if (ptr + len > memSize || !self->bridge)
            return nullptr;
        self->pendingReply = self->bridge(data + ptr, (size_t)len);
        results[0].of.i32 = (int32_t)self->pendingReply.size();
        return nullptr;
    }

    static wasm_trap_t* hostFetchCb(void* env, wasmtime_caller_t* caller,
                                    const wasmtime_val_t* args, size_t nargs,
                                    wasmtime_val_t* results, size_t nresults)
    {
        auto* self = static_cast<WasmtimeRuntime*>(env);
        if (nresults < 1)
            return nullptr;
        results[0].kind = WASMTIME_I32;
        results[0].of.i32 = -1;
        wasmtime_extern_t ext;
        if (nargs < 2 || self->pendingReply.empty()
                || !wasmtime_caller_export_get(caller, "memory", 6, &ext)
                || ext.kind != WASMTIME_EXTERN_MEMORY)
            return nullptr;
        wasmtime_context_t* ctx = wasmtime_caller_context(caller);
        wasmtime_memory_t mem = ext.of.memory;
        uint8_t* data = wasmtime_memory_data(ctx, &mem);
        size_t memSize = wasmtime_memory_data_size(ctx, &mem);
        uint64_t ptr = (uint32_t)args[0].of.i32;
        uint64_t cap = (uint32_t)args[1].of.i32;
        if (ptr + cap > memSize || cap < self->pendingReply.size())
            return nullptr;
        std::memcpy(data + ptr, self->pendingReply.data(), self->pendingReply.size());
        results[0].of.i32 = (int32_t)self->pendingReply.size();
        self->pendingReply.clear();
        return nullptr;
    }

    bool defineBridge(wasmtime_linker_t* linker)
    {
        struct
        {
            const char* name;
            wasmtime_func_callback_t cb;
        } funcs[] = {
            {"host_call", &WasmtimeRuntime::hostCallCb},
            {"host_fetch", &WasmtimeRuntime::hostFetchCb},
        };
        for (auto& fn : funcs) {
            wasm_functype_t* ft = wasm_functype_new_2_1(wasm_valtype_new_i32(),
                                                        wasm_valtype_new_i32(),
                                                        wasm_valtype_new_i32());
            wasmtime_error_t* err = wasmtime_linker_define_func(
                linker, "fcx", 3, fn.name, std::strlen(fn.name), ft, fn.cb, this,
                nullptr);
            wasm_functype_delete(ft);
            if (err) {
                FC_ERR("cannot define fcx." << fn.name << ": "
                       << errorText(err, nullptr));
                return false;
            }
        }
        return true;
    }
};

}  // namespace

std::unique_ptr<ImageRuntime> makeWasmtimeRuntime()
{
    return std::make_unique<WasmtimeRuntime>();
}

}  // namespace ExpressionSandbox
}  // namespace App
