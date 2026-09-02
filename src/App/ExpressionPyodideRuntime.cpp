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

    /** image = the fcx_image wheel, stdlib = the pyodide directory.
     * Precedence: explicit configure(), the preferences (PyodideDir,
     * PyodideWheel), the FCX_PYODIDE / FCX_PYODIDE_WHEEL environment,
     * then <datadir>/Pyodide.  With no wheel named, the first
     * fcx_image-*.whl in the directory.
     */
    Paths resolve(const std::string& image, const std::string& stdlib) const override
    {
        Paths p;
        p.image = image;
        p.stdlib = stdlib;
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
        fs::path wheel = fs::weakly_canonical(paths.image, ec);
        if (ec || !fs::is_regular_file(wheel) || scope(wheel.string(), root).empty()) {
            FC_LOG("no fcx_image wheel at " << paths.image << " under " << root.string());
            return false;
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
            teardown();
            return false;
        }

        // 2. pyodide.js, then the glue that boots and calls it
        std::string loader;
        if (!readFile(root / "pyodide.js", loader)
                || !run(loader, (root / "pyodide.js").string())) {
            teardown();
            return false;
        }
        if (!run(std::string(reinterpret_cast<const char*>(kPyodideGlue), kPyodideGlue_len),
                 "pyodide_glue.js")) {
            teardown();
            return false;
        }
        put(ctx->Global(), "__fcx_bridge", hostBridge);

        // 3. boot: ~1.5 s, once per session
        const double t0 = nowMs();
        std::string boot = "__fcx_boot(" + jsString(root.string() + "/") + ", "
            + jsString(wheel.string()) + ")";
        v8::Local<v8::Value> result;
        if (!run(boot, "boot", &result) || toStd(isolate, result) != "booted") {
            FC_ERR("pyodide boot failed");
            teardown();
            return false;
        }
        v8::Local<v8::Value> fn;
        if (!ctx->Global()->Get(ctx, str(isolate, "__fcx_call")).ToLocal(&fn) || !fn->IsFunction()) {
            FC_ERR("pyodide glue left no __fcx_call");
            teardown();
            return false;
        }
        callFn.Reset(isolate, fn.As<v8::Function>());
        live = true;
        FC_LOG("sandbox runtime live: pyodide at " << root.string() << " with "
               << wheel.filename().string() << " (" << (int)(nowMs() - t0) << " ms)");
        return true;
    }

    bool roundTrip(const std::vector<uint8_t>& request,
                   std::vector<uint8_t>& reply) override
    {
        if (!live)
            return false;
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> ctx = context.Get(isolate);
        v8::Context::Scope contextScope(ctx);
        v8::TryCatch tc(isolate);

        v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, request.size());
        if (!request.empty())
            std::memcpy(ab->Data(), request.data(), request.size());
        v8::Local<v8::Value> arg = v8::Uint8Array::New(ab, 0, request.size());
        v8::Local<v8::Value> result;
        if (!callFn.Get(isolate)->Call(ctx, ctx->Global(), 1, &arg).ToLocal(&result)) {
            report(tc, "fcx call");
            return false;
        }
        if (!result->IsUint8Array()) {
            FC_ERR("pyodide guest returned " << toStd(isolate, result) << " instead of bytes");
            return false;
        }
        v8::Local<v8::Uint8Array> out = result.As<v8::Uint8Array>();
        reply.resize(out->ByteLength());
        if (!reply.empty())
            out->CopyContents(reply.data(), reply.size());
        // Let the guest's own housekeeping (proxy finalizers, deferred
        // work) run now rather than pile up.
        isolate->PerformMicrotaskCheckpoint();
        return true;
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
                callFn.Reset();
                context.Reset();
                modules.clear();
                modulePaths.clear();
                unhandled.clear();
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
    v8::Isolate* isolate = nullptr;
    v8::ArrayBuffer::Allocator* allocator = nullptr;
    v8::Global<v8::Context> context;
    v8::Global<v8::Function> callFn;
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

    /// A path the guest named, canonicalised and confined to root (or
    /// empty).  `file://` is stripped; relative paths resolve against
    /// `base`.  Symlinks and `..` cannot leave root: the test is on the
    /// canonical form.
    fs::path scope(std::string p, const fs::path& base) const
    {
        if (p.rfind("file://", 0) == 0)
            p = p.substr(7);
        fs::path candidate = fs::path(p).is_absolute() ? fs::path(p) : base / p;
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(candidate, ec);
        if (ec)
            return {};
        const std::string r = root.string() + "/";
        const std::string c = canon.string();
        if (c.compare(0, r.size(), r) != 0)
            return {};
        return canon;
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
            return throwError(isolate, "read: refused, outside the pyodide directory: " + asked);
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

    /// The guest's only way out: one bridge op, Uint8Array in, Uint8Array
    /// out, straight into ImageHost's dispatch.
    static void hostBridge(const v8::FunctionCallbackInfo<v8::Value>& info)
    {
        v8::Isolate* isolate = info.GetIsolate();
        PyodideRuntime* rt = self(info);
        if (info.Length() < 1 || !info[0]->IsUint8Array())
            return throwError(isolate, "bridge: request must be a Uint8Array");
        v8::Local<v8::Uint8Array> in = info[0].As<v8::Uint8Array>();
        std::vector<uint8_t> req(in->ByteLength());
        if (!req.empty())
            in->CopyContents(req.data(), req.size());
        std::vector<uint8_t> reply = rt->bridge ? rt->bridge(req.data(), req.size())
                                                : std::vector<uint8_t>();
        if (reply.empty())
            return throwError(isolate, "bridge: no reply");
        v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, reply.size());
        std::memcpy(ab->Data(), reply.data(), reply.size());
        info.GetReturnValue().Set(v8::Uint8Array::New(ab, 0, reply.size()));
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
