// Phase 0 of the pyodide route (docs/PyodideHost.md): boot pyodide on the
// bare V8 that v8-embed ships, with the host supplying ONLY what the glue
// needs -- a scoped file reader, a scoped module loader, timers, text
// codecs, random bytes -- and check that the result both works (Python,
// a host hop, numpy from a wheel) and stays confined (no process, no
// fetch, no filesystem beyond the pyodide directory, no loader that can
// reach anything else).
//
// Everything a browser or node would hand pyodide for free is authored
// here on purpose.  What is NOT authored is the security mechanism.
//
//   pyodide_probe <pyodide dir> [shim.js]

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "libplatform/libplatform.h"
#include "v8-array-buffer.h"
#include "v8-container.h"
#include "v8-context.h"
#include "v8-exception.h"
#include "v8-function.h"
#include "v8-initialization.h"
#include "v8-isolate.h"
#include "v8-local-handle.h"
#include "v8-message.h"
#include "v8-microtask-queue.h"
#include "v8-object.h"
#include "v8-persistent-handle.h"
#include "v8-primitive.h"
#include "v8-promise.h"
#include "v8-script.h"
#include "v8-template.h"

namespace fs = std::filesystem;

namespace {

// ------------------------------------------------------------------ helpers

std::string ToStd(v8::Isolate* isolate, v8::Local<v8::Value> v)
{
    if (v.IsEmpty())
        return "<empty>";
    v8::String::Utf8Value s(isolate, v);
    return *s ? std::string(*s, s.length()) : "<unprintable>";
}

v8::Local<v8::String> Str(v8::Isolate* isolate, const std::string& s)
{
    return v8::String::NewFromUtf8(isolate, s.c_str(), v8::NewStringType::kNormal,
                                   static_cast<int>(s.size()))
        .ToLocalChecked();
}

double NowMs()
{
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double, std::milli>(steady_clock::now() - t0).count();
}

// ------------------------------------------------------------------- Host

struct Host {
    v8::Platform* platform = nullptr;
    v8::Isolate* isolate = nullptr;
    v8::Global<v8::Context> context;
    fs::path root;         // the pyodide directory; the ONLY readable subtree
    int failures = 0;
    std::mt19937_64 rng{std::random_device{}()};
    // One compiled module per file, so a second import() of the same path
    // hands back the same instance rather than instantiating twice.
    std::map<std::string, v8::Global<v8::Module>> modules;
    std::map<int, std::string> modulePaths;  // module identity hash -> path

    // Resolves a path the guest named and refuses anything outside root.
    // Returns an empty path on refusal.  `file://` is accepted and
    // stripped, relative paths resolve against `base` (a directory).
    fs::path Scope(std::string p, const fs::path& base) const
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
};

Host* g_host = nullptr;

bool ReadFile(const fs::path& p, std::string& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void ThrowError(v8::Isolate* isolate, const std::string& msg)
{
    isolate->ThrowException(v8::Exception::Error(Str(isolate, msg)));
}

void ReportException(v8::Isolate* isolate, v8::TryCatch& tc)
{
    v8::HandleScope scope(isolate);
    v8::Local<v8::Message> m = tc.Message();
    std::string where;
    if (!m.IsEmpty()) {
        v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
        where = ToStd(isolate, m->GetScriptResourceName()) + ":"
            + std::to_string(m->GetLineNumber(ctx).FromMaybe(0));
    }
    std::string text = ToStd(isolate, tc.Exception());
    if (tc.Exception()->IsObject()) {
        v8::Local<v8::Value> stack;
        if (tc.Exception().As<v8::Object>()
                ->Get(isolate->GetCurrentContext(), Str(isolate, "stack"))
                .ToLocal(&stack)
            && stack->IsString())
            text = ToStd(isolate, stack);
    }
    std::fprintf(stderr, "[js exception] %s %s\n", where.c_str(), text.c_str());
}

// --------------------------------------------------------- native functions
// Installed on `__fcx_host`; host_shim.js turns them into the d8/Web shape
// pyodide reads and then deletes the object.

// readText(path) / readBytes(path): the scoped reader.
void HostRead(const v8::FunctionCallbackInfo<v8::Value>& info, bool binary)
{
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsString())
        return ThrowError(isolate, "read: path must be a string");
    const std::string asked = ToStd(isolate, info[0]);
    fs::path p = g_host->Scope(asked, g_host->root);
    if (p.empty())
        return ThrowError(isolate, "read: refused, outside the pyodide directory: " + asked);
    std::string data;
    if (!ReadFile(p, data))
        return ThrowError(isolate, "read: cannot open " + p.string());
    if (!binary) {
        info.GetReturnValue().Set(Str(isolate, data));
        return;
    }
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, data.size());
    if (!data.empty())
        std::memcpy(ab->Data(), data.data(), data.size());
    info.GetReturnValue().Set(ab);
}
void HostReadText(const v8::FunctionCallbackInfo<v8::Value>& info) { HostRead(info, false); }
void HostReadBytes(const v8::FunctionCallbackInfo<v8::Value>& info) { HostRead(info, true); }

void HostRandomBytes(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    const double n = info.Length() > 0 ? info[0]->NumberValue(isolate->GetCurrentContext()).FromMaybe(-1) : -1;
    if (!(n >= 0 && n <= 65536))
        return ThrowError(isolate, "randomBytes: refused size");
    v8::Local<v8::ArrayBuffer> ab = v8::ArrayBuffer::New(isolate, static_cast<size_t>(n));
    auto* out = static_cast<unsigned char*>(ab->Data());
    for (size_t i = 0; i < static_cast<size_t>(n); ++i)
        out[i] = static_cast<unsigned char>(g_host->rng());
    info.GetReturnValue().Set(ab);
}

void HostNow(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    info.GetReturnValue().Set(NowMs());
}

void HostPrint(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    const std::string kind = info.Length() > 0 ? ToStd(isolate, info[0]) : "out";
    const std::string text = info.Length() > 1 ? ToStd(isolate, info[1]) : "";
    std::fprintf(kind == "err" ? stderr : stdout, "  [py %s] %s\n", kind.c_str(), text.c_str());
    std::fflush(stdout);
}

// ------------------------------------------------------------- the loader
// The only module loader this host has.  It resolves a specifier to a file
// under the pyodide directory and nothing else: no bare names, no `node:`
// scheme, no URL that leaves the directory.  Both the static resolver and
// the dynamic import() callback go through it.

v8::MaybeLocal<v8::Module> LoadModule(v8::Local<v8::Context> context,
                                      const std::string& specifier,
                                      const fs::path& referrerDir)
{
    v8::Isolate* isolate = g_host->isolate;
    fs::path p = g_host->Scope(specifier, referrerDir);
    if (p.empty() || !fs::is_regular_file(p)) {
        ThrowError(isolate, "import: refused: " + specifier);
        return {};
    }
    auto it = g_host->modules.find(p.string());
    if (it != g_host->modules.end())
        return it->second.Get(isolate);

    std::string code;
    if (!ReadFile(p, code)) {
        ThrowError(isolate, "import: cannot open " + p.string());
        return {};
    }
    v8::ScriptOrigin origin(Str(isolate, p.string()), 0, 0, false, -1, v8::Local<v8::Value>(),
                            false, false, /*is_module=*/true);
    v8::ScriptCompiler::Source source(Str(isolate, code), origin);
    v8::Local<v8::Module> module;
    if (!v8::ScriptCompiler::CompileModule(isolate, &source).ToLocal(&module))
        return {};
    g_host->modules[p.string()].Reset(isolate, module);
    g_host->modulePaths[module->GetIdentityHash()] = p.string();
    return module;
}

fs::path ModuleDir(v8::Local<v8::Module> referrer)
{
    auto it = g_host->modulePaths.find(referrer->GetIdentityHash());
    return it == g_host->modulePaths.end() ? g_host->root : fs::path(it->second).parent_path();
}

v8::MaybeLocal<v8::Module> ResolveStatic(v8::Local<v8::Context> context,
                                         v8::Local<v8::String> specifier,
                                         v8::Local<v8::FixedArray>,
                                         v8::Local<v8::Module> referrer)
{
    return LoadModule(context, ToStd(g_host->isolate, specifier), ModuleDir(referrer));
}

// Resolves the import() promise with the module namespace once the
// module's own evaluation (which may hold top-level awaits) settles.
void ResolveWithNamespace(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::Array> data = info.Data().As<v8::Array>();
    v8::Local<v8::Promise::Resolver> resolver =
        data->Get(ctx, 0).ToLocalChecked().As<v8::Promise::Resolver>();
    v8::Local<v8::Value> ns = data->Get(ctx, 1).ToLocalChecked();
    resolver->Resolve(ctx, ns).Check();
}
void RejectWith(const v8::FunctionCallbackInfo<v8::Value>& info)
{
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> ctx = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver = info.Data().As<v8::Promise::Resolver>();
    resolver->Reject(ctx, info.Length() > 0 ? info[0] : v8::Undefined(isolate).As<v8::Value>()).Check();
}

v8::MaybeLocal<v8::Promise> ImportDynamically(v8::Local<v8::Context> context,
                                              v8::Local<v8::Data>,
                                              v8::Local<v8::Value> resourceName,
                                              v8::Local<v8::String> specifier,
                                              v8::Local<v8::FixedArray>)
{
    v8::Isolate* isolate = g_host->isolate;
    v8::EscapableHandleScope scope(isolate);
    v8::Local<v8::Promise::Resolver> resolver = v8::Promise::Resolver::New(context).ToLocalChecked();

    // The referrer is a classic script or module named by its file path;
    // relative specifiers resolve beside it, and never outside root.
    fs::path referrerDir = g_host->root;
    if (!resourceName.IsEmpty() && resourceName->IsString()) {
        fs::path rp = g_host->Scope(ToStd(isolate, resourceName), g_host->root);
        if (!rp.empty())
            referrerDir = rp.parent_path();
    }

    v8::TryCatch tc(isolate);
    v8::Local<v8::Module> module;
    if (!LoadModule(context, ToStd(isolate, specifier), referrerDir).ToLocal(&module)
        || module->GetStatus() == v8::Module::kErrored
        || (module->GetStatus() == v8::Module::kUninstantiated
            && module->InstantiateModule(context, ResolveStatic).IsNothing())) {
        resolver->Reject(context, tc.HasCaught() ? tc.Exception()
                                                 : v8::Exception::Error(Str(isolate, "import failed")))
            .Check();
        return scope.Escape(resolver->GetPromise());
    }
    v8::Local<v8::Value> evalResult;
    if (module->GetStatus() == v8::Module::kEvaluated) {
        resolver->Resolve(context, module->GetModuleNamespace()).Check();
        return scope.Escape(resolver->GetPromise());
    }
    if (!module->Evaluate(context).ToLocal(&evalResult)) {
        resolver->Reject(context, tc.HasCaught() ? tc.Exception()
                                                 : v8::Exception::Error(Str(isolate, "evaluate failed")))
            .Check();
        return scope.Escape(resolver->GetPromise());
    }
    // Evaluate() returns a promise; chain the namespace onto it.
    v8::Local<v8::Promise> evalPromise = evalResult.As<v8::Promise>();
    v8::Local<v8::Array> data = v8::Array::New(isolate, 2);
    data->Set(context, 0, resolver).Check();
    data->Set(context, 1, module->GetModuleNamespace()).Check();
    v8::Local<v8::Function> onOk =
        v8::Function::New(context, ResolveWithNamespace, data).ToLocalChecked();
    v8::Local<v8::Function> onErr = v8::Function::New(context, RejectWith, resolver).ToLocalChecked();
    evalPromise->Then(context, onOk, onErr).ToLocalChecked();
    return scope.Escape(resolver->GetPromise());
}

void InitImportMeta(v8::Local<v8::Context> context, v8::Local<v8::Module> module,
                    v8::Local<v8::Object> meta)
{
    v8::Isolate* isolate = g_host->isolate;
    auto it = g_host->modulePaths.find(module->GetIdentityHash());
    const std::string url = "file://" + (it == g_host->modulePaths.end() ? std::string() : it->second);
    meta->CreateDataProperty(context, Str(isolate, "url"), Str(isolate, url)).Check();
}

// A rejection is only "unhandled" if nobody attaches a handler afterwards
// -- import() rejects inside the host callback, before the caller's .then()
// runs -- so rejections are held and reported once the loop goes idle.
std::vector<std::pair<v8::Global<v8::Promise>, std::string>> g_unhandled;

void OnPromiseReject(v8::PromiseRejectMessage msg)
{
    v8::Isolate* isolate = g_host->isolate;
    if (msg.GetEvent() == v8::kPromiseRejectWithNoHandler) {
        g_unhandled.emplace_back(v8::Global<v8::Promise>(isolate, msg.GetPromise()),
                                 ToStd(isolate, msg.GetValue()));
    }
    else if (msg.GetEvent() == v8::kPromiseHandlerAddedAfterReject) {
        for (auto it = g_unhandled.begin(); it != g_unhandled.end(); ++it)
            if (it->first == msg.GetPromise()) {
                g_unhandled.erase(it);
                break;
            }
    }
}

void ReportUnhandled()
{
    for (auto& u : g_unhandled)
        std::fprintf(stderr, "[unhandled rejection] %s\n", u.second.c_str());
    g_unhandled.clear();
}

// ------------------------------------------------------------ event loop
// The whole "runtime": drain microtasks, fire due timers from the shim's
// queue, sleep until the next one, until `done` says so.

bool Pump(Host& host, const std::function<bool()>& done, double timeoutMs = 120000)
{
    v8::Isolate* isolate = host.isolate;
    v8::Local<v8::Context> ctx = host.context.Get(isolate);
    const double deadline = NowMs() + timeoutMs;
    double idleSince = -1;
    for (;;) {
        // Foreground tasks the platform's worker threads posted back -- an
        // async WebAssembly compile completes through here, and nothing
        // else delivers it.
        bool platformWork = false;
        while (v8::platform::PumpMessageLoop(host.platform, isolate,
                                             v8::platform::MessageLoopBehavior::kDoNotWait))
            platformWork = true;
        isolate->PerformMicrotaskCheckpoint();
        if (done())
            return true;
        v8::Local<v8::Value> fnv;
        double next = -1;
        if (ctx->Global()->Get(ctx, Str(isolate, "__fcx_runTimers")).ToLocal(&fnv) && fnv->IsFunction()) {
            v8::TryCatch tc(isolate);
            v8::Local<v8::Value> r;
            if (fnv.As<v8::Function>()->Call(ctx, ctx->Global(), 0, nullptr).ToLocal(&r))
                next = r->NumberValue(ctx).FromMaybe(-1);
            else
                ReportException(isolate, tc);
        }
        isolate->PerformMicrotaskCheckpoint();
        ReportUnhandled();
        if (done())
            return true;
        if (NowMs() > deadline) {
            std::fprintf(stderr, "[pump] timed out after %.0f ms\n", timeoutMs);
            return false;
        }
        if (next < 0 && !platformWork) {
            // Nothing queued here; a worker thread may still be about to
            // post back.  Give that a bounded wait, then call it what it
            // is: something awaits an event this host never delivers.
            if (idleSince < 0)
                idleSince = NowMs();
            else if (NowMs() - idleSince > 10000) {
                std::fprintf(stderr, "[pump] idle with a pending promise for 10 s: deadlock\n");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        idleSince = -1;
        if (next > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(std::min(next, 50.0))));
    }
}

// Runs `code` as a classic script and awaits it if it yields a promise.
// Returns false and reports on any failure.
bool Run(Host& host, const std::string& code, const std::string& name, v8::Local<v8::Value>* out = nullptr)
{
    v8::Isolate* isolate = host.isolate;
    v8::Local<v8::Context> ctx = host.context.Get(isolate);
    v8::EscapableHandleScope scope(isolate);
    v8::TryCatch tc(isolate);
    v8::ScriptOrigin origin(Str(isolate, name));
    v8::ScriptCompiler::Source source(Str(isolate, code), origin);
    v8::Local<v8::Script> script;
    if (!v8::ScriptCompiler::Compile(ctx, &source).ToLocal(&script)) {
        ReportException(isolate, tc);
        return false;
    }
    v8::Local<v8::Value> result;
    if (!script->Run(ctx).ToLocal(&result)) {
        ReportException(isolate, tc);
        return false;
    }
    if (result->IsPromise()) {
        v8::Local<v8::Promise> p = result.As<v8::Promise>();
        if (!Pump(host, [&] { return p->State() != v8::Promise::kPending; }))
            return false;
        if (p->State() == v8::Promise::kRejected) {
            v8::Local<v8::Value> reason = p->Result();
            std::string text = ToStd(isolate, reason);
            v8::Local<v8::Value> stack;
            if (reason->IsObject()
                && reason.As<v8::Object>()->Get(ctx, Str(isolate, "stack")).ToLocal(&stack)
                && stack->IsString())
                text = ToStd(isolate, stack);
            std::fprintf(stderr, "[rejected] %s: %s\n", name.c_str(), text.c_str());
            p->MarkAsHandled();
            return false;
        }
        result = p->Result();
    }
    if (out)
        *out = scope.Escape(result);
    return true;
}

void Check(Host& host, const char* what, const std::string& got, const std::string& want)
{
    const bool ok = got == want;
    std::printf("%-46s %-24s %s\n", what, got.c_str(), ok ? "ok" : "FAILED");
    if (!ok) {
        std::printf("    expected %s\n", want.c_str());
        ++host.failures;
    }
}

// Evaluates `code` (awaiting a promise result) and compares its string form.
void CheckEval(Host& host, const char* what, const std::string& code, const std::string& want)
{
    v8::HandleScope scope(host.isolate);
    v8::Local<v8::Value> r;
    if (!Run(host, code, what, &r)) {
        Check(host, what, "<threw>", want);
        return;
    }
    Check(host, what, ToStd(host.isolate, r), want);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: pyodide_probe <pyodide dir> [host_shim.js]\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Host host;
    host.root = fs::weakly_canonical(argv[1]);
    const std::string shimPath = argc > 2 ? argv[2] : FCX_SHIM_JS;
    if (!fs::is_regular_file(host.root / "pyodide.js") || !fs::is_regular_file(host.root / "pyodide.asm.wasm")) {
        std::fprintf(stderr, "%s does not hold pyodide.js + pyodide.asm.wasm\n", host.root.c_str());
        return 2;
    }
    g_host = &host;

    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    host.platform = platform.get();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();

    v8::Isolate::CreateParams params;
    params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate* isolate = v8::Isolate::New(params);
    host.isolate = isolate;
    isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
    isolate->SetHostImportModuleDynamicallyCallback(ImportDynamically);
    isolate->SetHostInitializeImportMetaObjectCallback(InitImportMeta);
    isolate->SetPromiseRejectCallback(OnPromiseReject);
    {
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope contextScope(context);
        host.context.Reset(isolate, context);

        // 1. The native surface, then the shim that shapes it.
        v8::Local<v8::Object> h = v8::Object::New(isolate);
        auto put = [&](const char* name, v8::FunctionCallback cb) {
            h->Set(context, Str(isolate, name),
                   v8::Function::New(context, cb).ToLocalChecked())
                .Check();
        };
        put("readText", HostReadText);
        put("readBytes", HostReadBytes);
        put("randomBytes", HostRandomBytes);
        put("now", HostNow);
        put("print", HostPrint);
        context->Global()->Set(context, Str(isolate, "__fcx_host"), h).Check();
        std::string shim;
        if (!ReadFile(shimPath, shim)) {
            std::fprintf(stderr, "cannot read %s\n", shimPath.c_str());
            return 2;
        }
        if (!Run(host, shim, shimPath))
            return 1;

        // 2. pyodide.js, a classic script that leaves globalThis.loadPyodide.
        std::string loader;
        ReadFile(host.root / "pyodide.js", loader);
        if (!Run(host, loader, (host.root / "pyodide.js").string()))
            return 1;
        CheckEval(host, "typeof loadPyodide", "typeof loadPyodide", "function");

        // 3. Boot.  indexURL is the directory the reader is scoped to.
        const double t0 = NowMs();
        std::string boot = "globalThis.__root = " + std::string("\"") + host.root.string() + "/\";\n"
            "loadPyodide({ indexURL: __root,"
            "  stdout: (s) => print(s), stderr: (s) => printErr(s) })"
            ".then((py) => { globalThis.py = py; return 'booted'; })";
        CheckEval(host, "loadPyodide()", boot, "booted");
        std::printf("%-46s %.0f ms\n", "cold start", NowMs() - t0);
        if (host.failures)
            return 1;

        // 4. It works.
        CheckEval(host, "py.runPython('1+2*3-4/5')", "String(py.runPython('1+2*3-4/5'))", "6.2");
        CheckEval(host, "sys.version", "py.runPython('import sys; sys.version.split()[0]')", "3.14.2");
        CheckEval(host, "host hop js.hostReadProp()",
                  "globalThis.hostReadProp = () => 42; String(py.runPython('import js; js.hostReadProp() * 2'))",
                  "84");
        CheckEval(host, "quantity-ish math", "String(py.runPython('import math; round(math.sqrt(2), 6)'))",
                  "1.414214");
        CheckEval(host, "loadPackage('numpy')",
                  "py.loadPackage('numpy', { messageCallback: () => {} }).then(() => 'loaded')", "loaded");
        CheckEval(host, "numpy arange(1000).sum()",
                  "String(py.runPython('import numpy as np; int(np.arange(1000).sum())'))", "499500");
        CheckEval(host, "numpy version", "py.runPython('import numpy; numpy.__version__')", "2.4.6");

        // 4b. An extra wheel, when asked: FCX_PROBE_WHEEL=<path under the
        //     pyodide dir> FCX_PROBE_PY=<python expr> FCX_PROBE_EXPECT=<str>.
        //     This is how the toolchain for the guest extension is proven,
        //     and later how a freshly built fcx_image wheel is smoke-tested.
        if (const char* wheel = std::getenv("FCX_PROBE_WHEEL")) {
            const char* code = std::getenv("FCX_PROBE_PY");
            const char* want = std::getenv("FCX_PROBE_EXPECT");
            std::string load = std::string("py.loadPackage('") + wheel
                + "', { messageCallback: () => {} }).then(() => 'loaded')";
            CheckEval(host, "loadPackage(FCX_PROBE_WHEEL)", load, "loaded");
            // FCX_PROBE_JS runs first: a place to install a fake host bridge
            // (globalThis.fcxHost) or anything else the Python side names.
            if (const char* js = std::getenv("FCX_PROBE_JS")) {
                v8::HandleScope s(isolate);
                if (!Run(host, js, "FCX_PROBE_JS"))
                    ++host.failures;
            }
            if (code && want) {
                std::string run = std::string("String(py.runPython(") + "`" + code + "`))";
                CheckEval(host, "FCX_PROBE_PY", run, want);
            }
        }

        // 5. It is confined.  The same claims the v8-embed consumer test makes,
        //    now with the shim installed and pyodide running on top of it.
        for (const char* name : {"process", "require", "module", "fs", "fetch", "XMLHttpRequest",
                                 "Worker", "WebSocket", "MessageChannel", "Deno", "Bun"}) {
            char label[80], code[80];
            std::snprintf(label, sizeof(label), "typeof %s", name);
            std::snprintf(code, sizeof(code), "typeof %s", name);
            CheckEval(host, label, code, "undefined");
        }
        CheckEval(host, "import('node:fs') rejects",
                  "import('node:fs').then(() => 'RESOLVED', (e) => 'rejected')", "rejected");
        CheckEval(host, "import('fs') rejects", "import('fs').then(() => 'RESOLVED', (e) => 'rejected')",
                  "rejected");
        CheckEval(host, "import('/etc/hostname') rejects",
                  "import('/etc/hostname').then(() => 'RESOLVED', (e) => 'rejected')", "rejected");
        CheckEval(host, "Function ctor import() rejects",
                  "Function('return import(\"node:fs\")')().then(() => 'RESOLVED', (e) => 'rejected')",
                  "rejected");
        CheckEval(host, "read('/etc/hostname') throws",
                  "(() => { try { read('/etc/hostname'); return 'READ'; } catch (e) { return 'threw'; } })()",
                  "threw");
        CheckEval(host, "read(root + '/../x') throws",
                  "(() => { try { read(__root + '../pyodide.js'); return 'READ'; } catch (e) { return 'threw'; } })()",
                  "threw");
        CheckEval(host, "os.system('sh -c id') throws",
                  "(() => { try { os.system('sh', ['-c', 'id']); return 'RAN'; } catch (e) { return 'threw'; } })()",
                  "threw");
        CheckEval(host, "python open('/etc/hostname') fails",
                  "py.runPython(\"try:\\n open('/etc/hostname').read(); r='READ'\\nexcept OSError as e: r='OSError'\\nr\")",
                  "OSError");
        CheckEval(host, "python js.process is undefined",
                  "py.runPython('import js; str(getattr(js, \"process\", None))')", "None");
        CheckEval(host, "python socket connect fails",
                  "py.runPython(\"import socket\\ntry:\\n s=socket.socket(); s.settimeout(1); s.connect(('127.0.0.1', 22)); r='CONNECTED'\\nexcept OSError as e: r='OSError'\\nr\")",
                  "OSError");

        // 6. A runaway guest.  wasmtime gave the WASI image fuel and epochs;
        //    here the watchdog is TerminateExecution() from another thread.
        //    Two questions: does it stop `while True: pass`, and is the
        //    interpreter still usable afterwards, or is recovery a reboot?
        {
            v8::HandleScope s(isolate);
            std::thread watchdog([isolate] {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                isolate->TerminateExecution();
            });
            const double t0 = NowMs();
            v8::TryCatch tc(isolate);
            v8::ScriptOrigin origin(Str(isolate, "runaway"));
            v8::ScriptCompiler::Source src(
                Str(isolate, "py.runPython('while True: pass'); 'RETURNED'"), origin);
            v8::Local<v8::Script> script = v8::ScriptCompiler::Compile(context, &src).ToLocalChecked();
            v8::Local<v8::Value> r;
            const bool ran = script->Run(context).ToLocal(&r);
            const bool terminated = !ran && tc.HasTerminated();
            watchdog.join();
            isolate->CancelTerminateExecution();
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s after %.0f ms",
                          terminated ? "terminated" : (ran ? "RETURNED" : "threw"), NowMs() - t0);
            Check(host, "while True: pass + TerminateExecution", terminated ? "terminated" : buf,
                  "terminated");
            std::printf("%-46s %s\n", "  (timing)", buf);
            // Is the interpreter still alive?
            CheckEval(host, "runPython after termination", "String(py.runPython('1+1'))", "2");
            CheckEval(host, "numpy after termination",
                      "String(py.runPython('import numpy as np; int(np.arange(10).sum())'))", "45");
            CheckEval(host, "host hop after termination",
                      "String(py.runPython('import js; js.hostReadProp() + 1'))", "43");
        }

        // 7. What one crossing costs, for the record beside the earlier numbers.
        {
            v8::HandleScope s(isolate);
            v8::Local<v8::Value> r;
            const char* bench =
                "(() => {"
                "  const f = py.runPython('def f():\\n    return 1+2*3-4/5\\nf');"
                "  for (let i = 0; i < 500; i++) f();"
                "  const N = 20000; const t0 = performance.now();"
                "  for (let i = 0; i < N; i++) f();"
                "  const perCall = (performance.now() - t0) * 1000 / N;"
                "  globalThis.hostReadProp = () => 42;"
                "  const g = py.runPython('import js\\ndef g():\\n    return js.hostReadProp() * 2\\ng');"
                "  for (let i = 0; i < 500; i++) g();"
                "  const t1 = performance.now();"
                "  for (let i = 0; i < N; i++) g();"
                "  const perHop = (performance.now() - t1) * 1000 / N;"
                "  f.destroy(); g.destroy();"
                "  return perCall.toFixed(2) + ' us/call, ' + perHop.toFixed(2) + ' us/call+hop';"
                "})()";
            if (Run(host, bench, "bench", &r))
                std::printf("%-46s %s\n", "precompiled call / call + host hop", ToStd(isolate, r).c_str());
        }
    }
    host.modules.clear();
    host.context.Reset();
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    delete params.array_buffer_allocator;

    if (host.failures) {
        std::printf("%d check(s) failed\n", host.failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
