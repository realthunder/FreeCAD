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

#include <cstring>
#include <fstream>
#include <mutex>

#include <nlohmann/json.hpp>
#include <wasm.h>
#include <wasmtime.h>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>

#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "Expression.h"
#include "ExpressionImageBridge.h"
#include "ExpressionImageHost.h"
#include "ExpressionSecurityRuntime.h"

using json = nlohmann::json;

FC_LOG_LEVEL_INIT("ExpressionImage", true, true)

namespace App
{
namespace ExpressionSandbox
{

struct ImageHost::Private
{
    std::recursive_mutex mutex;
    std::string imagePath;
    std::string stdlibPath;
    bool configured = false;
    bool triedInit = false;
    bool live = false;

    wasm_engine_t* engine = nullptr;
    wasmtime_store_t* store = nullptr;
    wasmtime_context_t* context = nullptr;
    wasmtime_module_t* module = nullptr;
    wasmtime_instance_t instance {};
    wasmtime_memory_t memory {};
    wasmtime_func_t funcAlloc {};
    wasmtime_func_t funcFree {};
    wasmtime_func_t funcCall {};

    // image->host bridge state: live handles and the reply pending
    // between the host_call and host_fetch halves of one bridge op
    HandleTable handles;
    std::vector<uint8_t> pendingReply;
    std::size_t evals = 0;

    static wasm_trap_t* hostCallCb(void* env, wasmtime_caller_t* caller,
                                   const wasmtime_val_t* args, size_t nargs,
                                   wasmtime_val_t* results, size_t nresults);
    static wasm_trap_t* hostFetchCb(void* env, wasmtime_caller_t* caller,
                                    const wasmtime_val_t* args, size_t nargs,
                                    wasmtime_val_t* results, size_t nresults);
    bool defineBridge(wasmtime_linker_t* linker);

    void teardown()
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

    bool initialize()
    {
        if (triedInit)
            return live;
        triedInit = true;

        if (!configured) {
            auto hGrp = GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Expression/Sandbox");
            imagePath = hGrp->GetASCII("ImagePath", "");
            stdlibPath = hGrp->GetASCII("StdlibPath", "");
        }
        if (imagePath.empty() || stdlibPath.empty()) {
            FC_LOG("no image/stdlib configured, sandbox image unavailable");
            return false;
        }

        wasm_config_t* cfg = wasm_config_new();
        wasmtime_config_wasm_exceptions_set(cfg, true);
        engine = wasm_engine_new_with_config(cfg);
        store = wasmtime_store_new(engine, nullptr, nullptr);
        context = wasmtime_store_context(store);

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

        // JIT-compiling the ~32 MB image costs ~600 ms; a serialized
        // .cwasm loads in ~20 ms.  The cache sits next to the image and
        // is refreshed whenever it is older than the image or fails to
        // deserialize (wasmtime version change).
        std::string cachePath = imagePath + ".cwasm";
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
            if (!wasmtime_module_serialize(module, &blob)) {
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

    /// One fcx_call round trip: request CBOR in, reply CBOR out.
    bool roundTrip(const std::vector<uint8_t>& request, json& reply)
    {
        // write the request into guest memory
        wasmtime_val_t args[2], result;
        args[0].kind = WASMTIME_I32;
        args[0].of.i32 = (int32_t)request.size();
        if (!callSimple(funcAlloc, args, 1, &result, 1, "fcx_alloc"))
            return false;
        uint32_t guestPtr = (uint32_t)result.of.i32;
        uint8_t* mem = wasmtime_memory_data(context, &memory);
        size_t memSize = wasmtime_memory_data_size(context, &memory);
        if (!guestPtr || guestPtr + request.size() > memSize) {
            FC_ERR("guest allocation out of range");
            return false;
        }
        std::memcpy(mem + guestPtr, request.data(), request.size());

        args[0].kind = WASMTIME_I32;
        args[0].of.i32 = (int32_t)guestPtr;
        args[1].kind = WASMTIME_I32;
        args[1].of.i32 = (int32_t)request.size();
        bool ok = callSimple(funcCall, args, 2, &result, 1, "fcx_call");
        // free the request buffer regardless
        wasmtime_val_t freeArg;
        freeArg.kind = WASMTIME_I32;
        freeArg.of.i32 = (int32_t)guestPtr;
        callSimple(funcFree, &freeArg, 1, nullptr, 0, "fcx_free");
        if (!ok)
            return false;

        uint32_t replyPtr = (uint32_t)result.of.i32;
        mem = wasmtime_memory_data(context, &memory);  // may have moved
        memSize = wasmtime_memory_data_size(context, &memory);
        if (!replyPtr || replyPtr + 4 > memSize) {
            FC_ERR("bad reply pointer");
            return false;
        }
        uint32_t replyLen = 0;
        std::memcpy(&replyLen, mem + replyPtr, 4);
        if (replyPtr + 4 + replyLen > memSize) {
            FC_ERR("bad reply length");
            return false;
        }
        try {
            reply = json::from_cbor(mem + replyPtr + 4,
                                    mem + replyPtr + 4 + replyLen);
        }
        catch (const json::exception& e) {
            FC_ERR("undecodable reply: " << e.what());
            reply = json();
        }
        freeArg.of.i32 = (int32_t)replyPtr;
        callSimple(funcFree, &freeArg, 1, nullptr, 0, "fcx_free");
        return !reply.is_null();
    }
};

/// The host_call half of one bridge op: decode the request out of guest
/// memory, dispatch it, park the reply, return only its length (never
/// re-enter the guest; the image fetches with its own buffer).  -1
/// signals a transport-level failure.
wasm_trap_t* ImageHost::Private::hostCallCb(void* env,
                                            wasmtime_caller_t* caller,
                                            const wasmtime_val_t* args,
                                            size_t nargs,
                                            wasmtime_val_t* results,
                                            size_t nresults)
{
    auto* d = static_cast<Private*>(env);
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
    if (ptr + len > memSize)
        return nullptr;

    json reply;
    try {
        json req = json::from_cbor(data + ptr, data + ptr + len);
        reply = dispatchHostOp(d->handles, req);
    }
    catch (const std::exception& e) {
        reply = {{"ok", false}, {"exc", "ProtocolError"}, {"msg", e.what()}};
    }
    d->pendingReply = json::to_cbor(reply);
    results[0].of.i32 = (int32_t)d->pendingReply.size();
    return nullptr;
}

wasm_trap_t* ImageHost::Private::hostFetchCb(void* env,
                                             wasmtime_caller_t* caller,
                                             const wasmtime_val_t* args,
                                             size_t nargs,
                                             wasmtime_val_t* results,
                                             size_t nresults)
{
    auto* d = static_cast<Private*>(env);
    if (nresults < 1)
        return nullptr;
    results[0].kind = WASMTIME_I32;
    results[0].of.i32 = -1;
    wasmtime_extern_t ext;
    if (nargs < 2 || d->pendingReply.empty()
            || !wasmtime_caller_export_get(caller, "memory", 6, &ext)
            || ext.kind != WASMTIME_EXTERN_MEMORY)
        return nullptr;
    wasmtime_context_t* ctx = wasmtime_caller_context(caller);
    wasmtime_memory_t mem = ext.of.memory;
    uint8_t* data = wasmtime_memory_data(ctx, &mem);
    size_t memSize = wasmtime_memory_data_size(ctx, &mem);
    uint64_t ptr = (uint32_t)args[0].of.i32;
    uint64_t cap = (uint32_t)args[1].of.i32;
    if (ptr + cap > memSize || cap < d->pendingReply.size())
        return nullptr;
    std::memcpy(data + ptr, d->pendingReply.data(), d->pendingReply.size());
    results[0].of.i32 = (int32_t)d->pendingReply.size();
    d->pendingReply.clear();
    return nullptr;
}

bool ImageHost::Private::defineBridge(wasmtime_linker_t* linker)
{
    struct
    {
        const char* name;
        wasmtime_func_callback_t cb;
    } funcs[] = {
        {"host_call", &Private::hostCallCb},
        {"host_fetch", &Private::hostFetchCb},
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

ImageHost::ImageHost()
    : d(new Private)
{}

ImageHost::~ImageHost()
{
    d->teardown();
}

ImageHost& ImageHost::instance()
{
    static ImageHost* inst = new ImageHost();
    return *inst;
}

bool ImageHost::available()
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    return d->initialize();
}

void ImageHost::configure(const std::string& imagePath,
                          const std::string& stdlibPath)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    d->teardown();
    d->triedInit = false;
    d->configured = true;
    d->imagePath = imagePath;
    d->stdlibPath = stdlibPath;
}

uint64_t ImageHost::exportObject(PyObject* obj)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    Base::PyGILStateLocker lock;
    return d->handles.add(obj);
}

void ImageHost::clearHandles()
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    Base::PyGILStateLocker lock;
    d->handles.clear();
}

std::size_t ImageHost::evalCount() const
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    return d->evals;
}

std::size_t ImageHost::handleCount() const
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    return d->handles.size();
}

void ImageHost::reset()
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    d->teardown();
    d->triedInit = false;
}

PyObject* ImageHost::decodeResult(const ImageResult& result)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    if (!result.ok)
        return nullptr;
    try {
        json v = json::from_cbor(result.value.begin(), result.value.end());
        return decodeHostValue(d->handles, v);
    }
    catch (const json::exception& e) {
        FC_ERR("undecodable image result: " << e.what());
        return nullptr;
    }
}

bool ImageHost::rawCall(const std::vector<unsigned char>& requestCbor,
                        std::vector<unsigned char>& replyCbor)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    if (!d->initialize())
        return false;
    json reply;
    std::vector<uint8_t> req(requestCbor.begin(), requestCbor.end());
    if (!d->roundTrip(req, reply)) {
        reset();
        return false;
    }
    auto cbor = json::to_cbor(reply);
    replyCbor.assign(cbor.begin(), cbor.end());
    return true;
}

ImageResult ImageHost::eval(const std::string& source,
                            const std::vector<unsigned char>& bindingsCbor)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    ImageResult res;
    if (!d->initialize()) {
        res.excType = "ImageUnavailable";
        res.message = "expression sandbox image is not available";
        return res;
    }

    ++d->evals;
    json req;
    req["op"] = "eval";
    req["src"] = source;
    if (!bindingsCbor.empty()) {
        try {
            req["bindings"] =
                json::from_cbor(bindingsCbor.begin(), bindingsCbor.end());
        }
        catch (const json::exception& e) {
            res.excType = "ProtocolError";
            res.message = std::string("bad bindings: ") + e.what();
            return res;
        }
    }

    json reply;
    if (!d->roundTrip(json::to_cbor(req), reply)) {
        // a failed round trip may mean a trapped instance; drop it so
        // the next evaluation reinstantiates cleanly
        reset();
        res.excType = "ImageTrapped";
        res.message = "image call failed, instance dropped";
        return res;
    }

    res.ok = reply.value("ok", false);
    if (res.ok) {
        auto val = reply.find("val");
        res.value = json::to_cbor(val != reply.end() ? *val : json());
    }
    else {
        res.excType = reply.value("exc", "Exception");
        res.message = reply.value("msg", "");
    }
    return res;
}

ImageResult ImageHost::evalExpression(const App::DocumentObject* owner,
                                      const std::string& source,
                                      const App::Expression* parsed)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    ImageResult res;
    if (!d->initialize()) {
        res.excType = "ImageUnavailable";
        res.message = "expression sandbox image is not available";
        return res;
    }

    ++d->evals;
    json req;
    req["op"] = "eval";
    req["lang"] = "expr";
    req["src"] = source;
    if (owner) {
        json ctx;
        ctx["doc"] = owner->getDocument() ? owner->getDocument()->getName() : "";
        ctx["obj"] = owner->getNameInDocument() ? owner->getNameInDocument() : "";
        req["ctx"] = std::move(ctx);
    }

    // The bindings pack: enumerate the expression's identifiers without
    // evaluating it, resolve each under the owner's principal, marshal
    // by value or as a handle.  Identifiers that fail to resolve are
    // left out -- the image reports the identical resolution error.
    try {
        ExpressionSecurity::Runtime::Scope secScope(owner);
        Base::PyGILStateLocker lock;

        // An already-parsed expression is the switch-over's hot path:
        // the caller holds the AST, so do not re-parse it here (1.4 us
        // per evaluation, measured).
        App::ExpressionPtr owned;
        const App::Expression* expr = parsed;
        if (!expr) {
            owned = App::Expression::parse(owner, source.c_str(), source.size());
            expr = owned.get();
        }
        if (expr && owner) {
            PyObject* ownerPy =
                const_cast<App::DocumentObject*>(owner)->getPyObject();
            req["owner_h"] = d->handles.add(ownerPy);
            if (const char* fc = facadeKeyFor(Py_TYPE(ownerPy)))
                req["owner_fc"] = fc;
            Py_DECREF(ownerPy);  // the table holds its own reference

            json bindings = json::object();
            std::map<App::ObjectIdentifier, bool> ids;
            expr->getIdentifiers(ids);
            for (auto& v : ids) {
                const auto& id = v.first;
                // Ring 0 pseudo-modules live IN the image (docs/
                // ExpressionSandbox.md sec 7.4): never resolve them on
                // the host -- not even to a handle.  `_py.open` must
                // mean the image's builtins under WASI, not ours.
                const auto& comps = id.getComponents();
                if (!comps.empty() && comps[0].isSimple()) {
                    const std::string& root = comps[0].getName();
                    if (root == "_math" || root == "_re" || root == "_coll"
                            || root == "_py" || root == "_app")
                        continue;
                }
                try {
                    Py::Object value = id.getPyValue(true);
                    bindings[id.toString()] =
                        encodeHostValue(d->handles, value.ptr());
                }
                catch (const ExpressionSecurity::PermissionNeededException&) {
                    throw;
                }
                catch (Base::Exception&) {
                    // unresolvable here -> unresolvable in the image,
                    // with the image's own (native) error message
                }
                catch (Py::Exception&) {
                    if (PyErr_Occurred())
                        PyErr_Clear();
                }
            }
            if (!bindings.empty())
                req["bindings"] = std::move(bindings);
        }
    }
    catch (const ExpressionSecurity::PermissionNeededException& e) {
        {
            Base::PyGILStateLocker lock;
            if (PyErr_Occurred())
                PyErr_Clear();
        }
        res.excType = "PermissionError";
        res.message = e.what();
        return res;
    }
    catch (Base::Exception&) {
        // host-side parse failure: ship as-is, the image parses the
        // same source with the same parser and raises the same error
    }

    json reply;
    if (!d->roundTrip(json::to_cbor(req), reply)) {
        reset();
        res.excType = "ImageTrapped";
        res.message = "image call failed, instance dropped";
        return res;
    }

    res.ok = reply.value("ok", false);
    if (res.ok) {
        auto val = reply.find("val");
        res.value = json::to_cbor(val != reply.end() ? *val : json());
    }
    else {
        res.excType = reply.value("exc", "Exception");
        res.message = reply.value("msg", "");
    }
    return res;
}

}  // namespace ExpressionSandbox
}  // namespace App
