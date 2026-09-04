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

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>

#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "Expression.h"
#include "ExpressionImageBridge.h"
#include "ExpressionImageHost.h"
#include "ExpressionImageRuntime.h"
#include "ExpressionSecurityRuntime.h"

using json = nlohmann::json;

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

/** Which runtime carries the sandbox: the preference
 * BaseApp/Preferences/Expression/Sandbox:Runtime when set, else the
 * FCX_RUNTIME environment (tests and the corpus gate select a runtime
 * per process this way), else "pyodide", the shipping default -- or
 * "wasi", the reference implementation, in a build that has only that
 * (docs/SandboxNetwork.md sec 0).
 */
std::string runtimeChoice()
{
    auto hGrp = GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Expression/Sandbox");
    std::string name = hGrp->GetASCII("Runtime", "");
    if (name.empty())
        name = envPath("FCX_RUNTIME");
    if (!name.empty())
        return name;
#ifdef FC_EXPR_PYODIDE_HOST
    return "pyodide";
#else
    return "wasi";
#endif
}

/// A runtime by name; nullptr (and a log line) for one this build does
/// not have.
std::unique_ptr<ImageRuntime> makeRuntime(const std::string& name)
{
#ifdef FC_EXPR_PYODIDE_HOST
    if (name == "pyodide")
        return makePyodideRuntime();
#endif
#ifdef FC_EXPR_WASI_RUNTIME
    if (name == "wasi")
        return makeWasmtimeRuntime();
#endif
    FC_ERR("unknown expression sandbox runtime '" << name
           << "' (this build has:"
#ifdef FC_EXPR_PYODIDE_HOST
           << " pyodide"
#endif
#ifdef FC_EXPR_WASI_RUNTIME
           << " wasi"
#endif
           << ")");
    return nullptr;
}

}  // namespace

struct ImageHost::Private: public ParameterGrp::ObserverType
{
    Private()
    {
        prefs = GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Expression/Sandbox");
        prefs->Attach(this);
        readBudget();
    }

    ~Private() override
    {
        prefs->Detach(this);
    }

    /// The budget preferences are cached and re-read on change: a DOM
    /// lookup per round trip measured 2.5 us on a 13 us trip.
    void OnChange(ParameterGrp::SubjectType&, ParameterGrp::MessageType reason) override
    {
        if (!reason || std::strcmp(reason, "BudgetMs") == 0 || std::strcmp(reason, "GraceMs") == 0)
            readBudget();
    }

    void readBudget()
    {
        budgetMs = static_cast<int>(prefs->GetInt("BudgetMs", 5000));
        graceMs = static_cast<int>(prefs->GetInt("GraceMs", 1000));
    }

    ParameterGrp::handle prefs;
    std::recursive_mutex mutex;
    std::string imagePath;
    std::string stdlibPath;
    bool configured = false;
    bool triedInit = false;
    bool live = false;
    /// scheduleReset() was called during the current round trip.
    bool resetPending = false;

    /// The transport and its guest (ExpressionImageRuntime.h); chosen at
    /// initialize() and dropped by teardown(), so a runtime preference
    /// change takes effect at the next evaluation after a reset().
    std::unique_ptr<ImageRuntime> rt;

    // image->host bridge state: the live handles of the transaction
    HandleTable handles;
    std::size_t evals = 0;
    /// guest->host ops by wire name (ImageHost::stats)
    std::map<std::string, std::size_t> ops;

    void teardown()
    {
        if (rt)
            rt->teardown();
        rt.reset();
        live = false;
    }

    /// The guest's paths as the selected runtime resolves them.
    ImageRuntime::Paths paths()
    {
        std::unique_ptr<ImageRuntime> probe;
        ImageRuntime* r = rt.get();
        if (!r) {
            probe = makeRuntime(runtimeChoice());
            r = probe.get();
        }
        if (!r)
            return ImageRuntime::Paths {};
        return r->resolve(configured ? imagePath : std::string(),
                          configured ? stdlibPath : std::string());
    }

    /// One guest->host bridge op, CBOR both ways (ExpressionImageBridge).
    std::vector<uint8_t> bridge(const uint8_t* data, std::size_t len)
    {
        json reply;
        try {
            json req = json::from_cbor(data, data + len);
            ++ops[req.is_object() ? req.value("op", std::string("?")) : std::string("?")];
            reply = dispatchHostOp(handles, req);
        }
        catch (const std::exception& e) {
            reply = {{"ok", false}, {"exc", "ProtocolError"}, {"msg", e.what()}};
        }
        return json::to_cbor(reply);
    }

    bool initialize()
    {
        if (triedInit)
            return live;
        triedInit = true;
        rt = makeRuntime(runtimeChoice());
        if (!rt)
            return false;
        ImageRuntime::Paths where = paths();
        // before initialize(): a runtime may compile its guest with or
        // without the budget's checks depending on whether there is one
        rt->setBudget(budgetMs, graceMs);
        live = rt->initialize(where, [this](const uint8_t* d, std::size_t n) {
            return bridge(d, n);
        });
        if (!live)
            teardown();
        return live;
    }

    /** The time budget of one round trip (Outcome in
     * ExpressionImageRuntime.h), from the preferences (cached, see
     * OnChange) and handed to the runtime per trip so a change applies
     * at once: BudgetMs (default 5000, 0 = unbounded) is the soft
     * deadline, GraceMs (default 1000) the extra time the guest gets to
     * act on it before the engine is stopped.
     */
    int budgetMs = 5000;
    int graceMs = 1000;
    void applyBudget()
    {
        rt->setBudget(budgetMs, graceMs);
    }

    json budgetError(const char* how) const
    {
        return {{"ok", false},
                {"exc", "TimeoutError"},
                {"msg", "expression sandbox: evaluation exceeded its "
                            + std::to_string(budgetMs) + " ms budget (" + how + ")"}};
    }

    /// Terminated guests are not trusted: drop this one, and the next
    /// evaluation starts a fresh one.
    void dropTerminated()
    {
        teardown();
        triedInit = false;
    }

    /// One round trip: request CBOR in, decoded reply out.  A budget
    /// that fires becomes an ordinary error reply (TimeoutError), never a
    /// transport failure.
    bool roundTrip(const std::vector<uint8_t>& request, json& reply)
    {
        if (!live || !rt)
            return false;
        applyBudget();
        std::vector<uint8_t> bytes;
        resetPending = false;
        const Outcome outcome = rt->roundTrip(request, bytes);
        if (outcome == Outcome::Failed)
            return false;
        if (outcome == Outcome::Terminated) {
            dropTerminated();
            reply = budgetError("terminated");
            return true;
        }
        if (resetPending) {
            // a bridge op asked for a fresh instance (a package to pick
            // up at boot); the reply of THIS trip is still good
            resetPending = false;
            dropTerminated();
        }
        try {
            reply = json::from_cbor(bytes.begin(), bytes.end());
        }
        catch (const json::exception& e) {
            FC_ERR("undecodable reply: " << e.what());
            reply = json();
        }
        if (reply.is_null())
            return false;
        if (outcome == Outcome::Interrupted) {
            if (!reply.value("ok", false) && reply.value("exc", "") == "KeyboardInterrupt") {
                reply = budgetError("interrupted");
            }
            else {
                // The signal was raised but the guest finished before
                // acting on it.  An interrupt still pending inside the
                // interpreter would surface in the NEXT evaluation, so
                // absorb it in a throwaway one now.
                std::vector<uint8_t> drain;
                json ping = {{"op", "eval"}, {"src", "0"}};
                if (rt->roundTrip(json::to_cbor(ping), drain) == Outcome::Terminated)
                    dropTerminated();
            }
        }
        return true;
    }
};

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

ImageHost::Location ImageHost::location()
{
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    ImageRuntime::Paths where = d->paths();
    return Location {where.image, where.stdlib, where.cache, where.packages};
}

void ImageHost::scheduleReset()
{
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    d->resetPending = true;
}

std::string ImageHost::runtime()
{
    std::lock_guard<std::recursive_mutex> lock(d->mutex);
    return d->rt ? std::string(d->rt->name()) : runtimeChoice();
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

ImageHost::Stats ImageHost::stats() const
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    Stats s;
    s.evals = d->evals;
    s.handles = d->handles.created();
    s.ops = d->ops;
    return s;
}

void ImageHost::resetStats()
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    d->evals = 0;
    d->ops.clear();
    d->handles.resetCreated();
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

    // one transaction: apply the previous call's queued releases, then
    // hold this call's so the reply stays decodable (see
    // HandleTable::setDeferReleases).  The flush DECREFs, so it needs
    // the GIL -- FreeCAD releases it at init and a bare Py_DECREF here
    // segfaults.
    if (Py_IsInitialized()) {
        // ... and only when there is an interpreter to lock: this entry
        // point is reachable from a test binary that never started one.
        Base::PyGILStateLocker lock;
        d->handles.flushDeferred();
    }
    d->handles.setDeferReleases(true);
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

namespace
{

/** True when this identifier reaches into a document OTHER than the
 * owner's.  It matters because the image has no foreign documents at
 * all -- its Application shim only ever knows the transaction's own
 * document -- so for these the HOST's resolution is authoritative
 * whether it succeeds or fails.  Left to itself the image answers any
 * such reference with "Document 'X' not found", which is the wrong
 * reason whenever X exists here and it was the property or the object
 * inside it that was missing.
 */
bool referencesForeignDocument(const App::ObjectIdentifier& id,
                               const App::DocumentObject* owner)
{
    const std::string& docName = id.getDocumentName().getString();
    if (docName.empty())
        return false;
    const App::Document* doc = owner ? owner->getDocument() : nullptr;
    if (!doc)
        return true;
    if (doc->getName() && docName == doc->getName())
        return false;
    // a document may be named by Label as well as by Name
    return docName != doc->Label.getValue();
}

/// The Python exception type name a Base::Exception raises as, so the
/// image can re-raise the same kind and not just the same text.
/// Requires the GIL.
std::string pyExceptionName(const Base::Exception& e)
{
    PyObject* type = e.getPyExceptionType();
    if (type && PyType_Check(type)) {
        const char* name = reinterpret_cast<PyTypeObject*>(type)->tp_name;
        if (name && *name) {
            const char* dot = std::strrchr(name, '.');
            return dot ? dot + 1 : name;
        }
    }
    return "RuntimeError";
}

}  // namespace

ImageResult ImageHost::evalExpression(const App::DocumentObject* owner,
                                      const std::string& source,
                                      const App::Expression* parsed,
                                      int options)
{
    std::lock_guard<std::recursive_mutex> guard(d->mutex);
    ImageResult res;
    if (!d->initialize()) {
        res.excType = "ImageUnavailable";
        res.message = "expression sandbox image is not available";
        return res;
    }

    // one transaction: apply the previous call's queued releases, then
    // hold this call's so the reply stays decodable (see
    // HandleTable::setDeferReleases).  The flush DECREFs, so it needs
    // the GIL -- FreeCAD releases it at init and a bare Py_DECREF here
    // segfaults.
    if (Py_IsInitialized()) {
        // ... and only when there is an interpreter to lock: this entry
        // point is reachable from a test binary that never started one.
        Base::PyGILStateLocker lock;
        d->handles.flushDeferred();
    }
    d->handles.setDeferReleases(true);
    ++d->evals;
    json req;
    req["op"] = "eval";
    req["lang"] = "expr";
    req["src"] = source;
    // The eval options cross with the request: the image must parse and
    // walk under the same ones, or a python-mode sheet cell and a
    // statement-bearing binding both mean something else in there.
    if (options)
        req["opts"] = options;
    if (owner) {
        json ctx;
        ctx["doc"] = owner->getDocument() ? owner->getDocument()->getName() : "";
        ctx["obj"] = owner->getNameInDocument() ? owner->getNameInDocument() : "";
        req["ctx"] = std::move(ctx);
    }

    // The evaluation's principal, for the pack step AND the round trip:
    // a bridge op the guest makes mid-evaluation (a permission check, a
    // pkg.missing request) is attributed to this owner.  Pushes only
    // when no outer scope is active, so an enclosing evaluation entry
    // or an explicit "session" scope still wins.
    ExpressionSecurity::Runtime::Scope secScope(owner);

    // The bindings pack: enumerate the expression's identifiers without
    // evaluating it, resolve each under the owner's principal, marshal
    // by value or as a handle.  Identifiers that fail to resolve are
    // left out -- the image reports the identical resolution error.
    try {
        Base::PyGILStateLocker lock;

        // An already-parsed expression is the switch-over's hot path:
        // the caller holds the AST, so do not re-parse it here (1.4 us
        // per evaluation, measured).
        App::ExpressionPtr owned;
        const App::Expression* expr = parsed;
        if (!expr) {
            owned = App::Expression::parse(
                    owner, source.c_str(), source.size(), false,
                    (options & App::Expression::OptionPythonMode) != 0);
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
            json bindErrors = json::object();
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
                catch (Base::Exception& e) {
                    // Unresolvable here usually means unresolvable in
                    // the image too, and the image raises the identical
                    // error -- EXCEPT for a foreign document, which it
                    // cannot see at all.  Ship those failures so it can
                    // raise the host's reason verbatim.
                    //
                    // Only those.  An identifier that fails to resolve
                    // here may be a variable BOUND DURING the
                    // evaluation ("a = V + 1; a * 2"), and a negative
                    // entry for `a` would break it.
                    if (referencesForeignDocument(id, owner)) {
                        json err;
                        err["exc"] = pyExceptionName(e);
                        err["msg"] = e.what();
                        bindErrors[id.toString()] = std::move(err);
                    }
                }
                catch (Py::Exception&) {
                    if (PyErr_Occurred())
                        PyErr_Clear();
                }
            }
            if (!bindings.empty())
                req["bindings"] = std::move(bindings);
            if (!bindErrors.empty())
                req["binderrs"] = std::move(bindErrors);
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
