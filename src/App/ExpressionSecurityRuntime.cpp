/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
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

#include "PreCompiled.h"

#include <Python.h>
#include <algorithm>
#include <set>
#include <sstream>

#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/PyObjectBase.h>

#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentObjectPy.h"
#include "DocumentPy.h"
#include "Expression.h"
#include "ExpressionSecurityRuntime.h"
#include "ObjectIdentifier.h"
#include "PropertyExpressionEngine.h"

namespace App {
namespace ExpressionSecurity {

// key separator inside the in-memory answer maps
static const char SEP = '\x1F';

////////////////////////////////////////////////////////////////////////////////////
//
// PermissionNeededException
//

static std::string permissionMessage(const std::string &principal,
        Permission perm, const std::string &target, bool promptable)
{
    std::ostringstream ss;
    ss << (promptable ? "Permission needed: " : "Permission denied: ")
       << permissionName(perm);
    if (!target.empty() && target != "*")
        ss << ":" << target;
    auto pclass = principalClass(principal);
    const char *who = "this evaluation";
    if (pclass) {
        switch (*pclass) {
        case PrincipalClass::Document: who = "this document"; break;
        case PrincipalClass::Session:  who = "this session";  break;
        case PrincipalClass::Addon:    who = "this addon";    break;
        }
    }
    if (promptable)
        ss << " -- not granted to " << who
           << " (grant it in the Document Permissions panel)";
    else
        ss << " -- not available to " << who;
    return ss.str();
}

PermissionNeededException::PermissionNeededException(std::string principal_,
        Permission perm, std::string target_, bool promptable_)
    : principal(std::move(principal_))
    , permission(perm)
    , target(std::move(target_))
    , promptable(promptable_)
{
    setMessage(permissionMessage(principal, permission, target, promptable));
}

PyObject *PermissionNeededException::getPyExceptionType() const
{
    return PyExc_PermissionError;
}

////////////////////////////////////////////////////////////////////////////////////
//
// The principal scope stack
//

struct ScopeEntry {
    const App::DocumentObject *owner = nullptr;
    const App::Document *doc = nullptr;
    std::string principal;  // empty for a document scope until first use
};

static thread_local std::vector<ScopeEntry> _ScopeStack;

Runtime::Scope::Scope(const App::DocumentObject *owner)
{
    // Evaluation-entry push: only when no scope is active, so an explicit
    // outer scope ("session" from the expression editor) takes precedence.
    if (!_ScopeStack.empty())
        return;
    ScopeEntry entry;
    entry.owner = owner;
    entry.doc = owner ? owner->getDocument() : nullptr;
    if (!entry.doc)
        entry.principal = "session";
    _ScopeStack.push_back(std::move(entry));
    pushed = true;
}

Runtime::Scope::Scope(const char *principalId)
{
    ScopeEntry entry;
    entry.principal = principalId ? principalId : "session";
    _ScopeStack.push_back(std::move(entry));
    pushed = true;
}

Runtime::Scope::~Scope()
{
    if (pushed)
        _ScopeStack.pop_back();
}

bool Runtime::scopeActive()
{
    return !_ScopeStack.empty();
}

////////////////////////////////////////////////////////////////////////////////////
//
// Runtime
//

Runtime &Runtime::instance()
{
    static Runtime *inst;
    if (!inst)
        inst = new Runtime;
    return *inst;
}

static bool parseGrantSpec(const std::string &spec, Permission &perm,
        std::string &target)
{
    std::string t;
    auto p = permissionFromName(spec, &t);
    if (p) {
        perm = *p;
        target = t.empty() ? std::string("*") : t;
        return true;
    }
    auto pos = spec.rfind(':');
    if (pos == std::string::npos)
        return false;
    p = permissionFromName(spec.substr(0, pos), &t);
    if (!p)
        return false;
    perm = *p;
    target = spec.substr(pos + 1);
    if (target.empty())
        target = "*";
    return true;
}

Runtime::Runtime()
{
    // --policy / --grant from the command line (headless flow: default is
    // DENY plus an audit line; these lift specific permissions)
    auto &cfg = Application::Config();
    auto it = cfg.find("ExpressionPolicyFile");
    if (it != cfg.end() && !it->second.empty())
        policyFile = it->second;
    it = cfg.find("ExpressionGrants");
    if (it != cfg.end()) {
        std::istringstream ss(it->second);
        std::string spec;
        while (std::getline(ss, spec)) {
            Permission perm;
            std::string target;
            if (spec.empty())
                continue;
            if (parseGrantSpec(spec, perm, target))
                addProcessGrant(perm, target);
            else
                Base::Console().Warning(
                        "ExpressionSecurity: invalid --grant '%s'\n", spec.c_str());
        }
    }

    auto &app = GetApplication();
    // Any change to an expression container voids the cached principal of
    // its document (the tamper-voids-grants property: the hash is over the
    // code strings, so it must follow every edit).
    app.signalChangedObject.connect(
        [this](const App::DocumentObject &obj, const App::Property &prop) {
            if (!prop.isDerivedFrom(PropertyExpressionContainer::getClassTypeId()))
                return;
            std::lock_guard<std::recursive_mutex> guard(mutex);
            docPrincipals.erase(obj.getDocument());
        });
    app.signalDeleteDocument.connect([this](const App::Document &doc) {
        bool changed = false;
        {
            std::lock_guard<std::recursive_mutex> guard(mutex);
            docPrincipals.erase(&doc);
            auto before = pending.size();
            pending.erase(std::remove_if(pending.begin(), pending.end(),
                    [&doc](const PendingRequest &r) {
                        return r.documentName == doc.getName();
                    }),
                    pending.end());
            changed = pending.size() != before;
        }
        if (changed)
            signalPendingChanged();
    });
}

bool Runtime::enforced() const
{
    static ParameterGrp::handle handle;
    if (!handle)
        handle = GetApplication().GetParameterGroupByPath(
                "User parameter:BaseApp/Preferences/Expression/Security");
    return handle->GetBool("Enforce", true);
}

static std::string securityDir()
{
    return Application::getUserAppDataDir() + "security";
}

void Runtime::ensureLoaded()
{
    if (loaded)
        return;
    loaded = true;
    std::string path = policyFile.empty()
        ? securityDir() + "/grants.json" : policyFile;
    std::string err;
    if (!_store.load(path, &err))
        Base::Console().Warning("ExpressionSecurity: failed to load %s: %s\n",
                path.c_str(), err.c_str());
}

GrantStore &Runtime::store()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    ensureLoaded();
    return _store;
}

bool Runtime::saveStore(std::string *errMsg)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if (!policyFile.empty())
        return true;  // --policy files are input only
    ensureLoaded();
    Base::FileInfo dir(securityDir());
    if (!dir.exists() && !dir.createDirectory()) {
        if (errMsg)
            *errMsg = "cannot create " + dir.filePath();
        return false;
    }
    return _store.save(securityDir() + "/grants.json", errMsg);
}

void Runtime::setPolicyFile(const std::string &path)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    policyFile = path;
    loaded = false;
    _store.clear();
}

void Runtime::audit(const std::string &principal, Permission perm,
        const std::string &target, Decision decision, const std::string &context)
{
    // Dedupe repeat evaluations: one line per unique decision per process,
    // so a 10k-cell recompute cannot flood the log.
    static std::set<std::string> audited;
    std::string key = principal + SEP + permissionName(perm) + SEP + target
        + SEP + decisionName(decision) + SEP + context;
    if (!audited.insert(key).second)
        return;
    Base::FileInfo dir(securityDir());
    if (!dir.exists() && !dir.createDirectory())
        return;
    AuditLog log(securityDir() + "/audit.log");
    log.append(principal, perm, target, decision, context);
}

std::string Runtime::documentPrincipal(const App::Document *doc)
{
    if (!doc)
        return "session";
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        auto it = docPrincipals.find(doc);
        if (it != docPrincipals.end())
            return it->second;
    }
    DocumentHashBuilder builder;
    {
        // toString() of an embedded Python-object constant may touch Python
        Base::PyGILStateLocker lock;
        for (auto obj : doc->getObjects()) {
            std::vector<App::Property *> props;
            obj->getPropertyList(props);
            for (auto prop : props) {
                if (!prop->isDerivedFrom(PropertyExpressionContainer::getClassTypeId()))
                    continue;
                bool isEngine = prop->isDerivedFrom(
                        PropertyExpressionEngine::getClassTypeId());
                auto exprs = static_cast<PropertyExpressionContainer *>(prop)
                        ->getExpressions();
                for (auto &v : exprs) {
                    if (!v.second)
                        continue;
                    std::string s = v.second->toString(true);
                    if (isEngine)
                        builder.addExpression(s);
                    else
                        builder.addCell(s);
                }
            }
        }
    }
    std::string principal = builder.principalId();
    std::lock_guard<std::recursive_mutex> guard(mutex);
    docPrincipals[doc] = principal;
    return principal;
}

std::string Runtime::currentPrincipal()
{
    if (_ScopeStack.empty())
        return {};
    auto &top = _ScopeStack.back();
    if (top.principal.empty())
        top.principal = documentPrincipal(top.doc);
    return top.principal;
}

static std::vector<std::string> targetChain(Permission perm, const std::string &target)
{
    std::vector<std::string> chain;
    chain.push_back(target.empty() ? std::string("*") : target);
    if (perm == Permission::HostImport && target != "*") {
        // "a.b.c" also matches grants for "a.b" and "a"
        std::string t = target;
        for (auto pos = t.rfind('.'); pos != std::string::npos; pos = t.rfind('.')) {
            t.resize(pos);
            chain.push_back(t);
        }
    }
    if (chain.back() != "*")
        chain.push_back("*");
    return chain;
}

Decision Runtime::resolve(const std::string &principal, Permission perm,
        const std::string &target)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    ensureLoaded();
    auto chain = targetChain(perm, target);
    const char *permName = permissionName(perm);
    for (auto &t : chain) {
        auto it = processOverrides.find(permName + (SEP + t));
        if (it != processOverrides.end())
            return it->second;
    }
    for (auto &t : chain) {
        std::string key = principal + SEP + permName + SEP + t;
        auto it = onceAnswers.find(key);
        if (it != onceAnswers.end())
            return it->second;
    }
    for (auto &t : chain) {
        std::string key = principal + SEP + permName + SEP + t;
        auto it = sessionAnswers.find(key);
        if (it != sessionAnswers.end())
            return it->second;
    }
    for (auto &t : chain) {
        // lookupGrant already treats a stored "*" target as matching; the
        // chain adds the dotted ancestors for host.import
        auto granted = _store.lookupGrant(principal, perm, t);
        if (granted)
            return *granted;
    }
    auto &defaults = const_cast<const GrantStore &>(_store).defaults();
    auto it = defaults.find(permName);
    if (it != defaults.end())
        return it->second;
    auto pclass = principalClass(principal);
    if (!pclass)
        return Decision::Deny;
    return catalogDefault(*pclass, perm);
}

void Runtime::check(Permission perm, const std::string &target)
{
    if (_ScopeStack.empty())
        return;  // host code outside any evaluation is already trusted
    if (!enforced())
        return;
    std::string principal = currentPrincipal();
    Decision decision = resolve(principal, perm, target);
    if (decision == Decision::Allow)
        return;

    auto pclass = principalClass(principal);
    bool promptable = decision == Decision::Prompt
        && pclass && isPromptable(*pclass, perm);

    auto &top = _ScopeStack.back();
    std::string docName = top.doc ? top.doc->getName() : std::string();
    std::string objName;
    if (top.owner && top.owner->getNameInDocument())
        objName = top.owner->getNameInDocument();

    bool pendingChanged = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if (promptable)
            pendingChanged = addPending(principal, perm, target, docName, objName);
        audit(principal, perm, target,
                promptable ? Decision::Prompt : Decision::Deny,
                docName.empty() ? objName : docName + ":" + objName);
    }
    if (pendingChanged)
        signalPendingChanged();
    throw PermissionNeededException(principal, perm, target, promptable);
}

void Runtime::auditAllowed(Permission perm, const std::string &target,
        const std::string &context)
{
    if (_ScopeStack.empty())
        return;  // host code outside any evaluation: not a principal's act
    std::string principal = currentPrincipal();
    std::lock_guard<std::recursive_mutex> guard(mutex);
    audit(principal, perm, target, Decision::Allow, context);
}

bool Runtime::addPending(const std::string &principal, Permission perm,
        const std::string &target, const std::string &docName,
        const std::string &objName)
{
    auto it = std::find_if(pending.begin(), pending.end(),
            [&](const PendingRequest &r) {
                return r.principal == principal && r.permission == perm
                    && r.target == target && r.documentName == docName
                    && r.objectName == objName;
            });
    if (it != pending.end()) {
        ++it->count;
        return false;
    }
    PendingRequest req;
    req.principal = principal;
    req.permission = perm;
    req.target = target;
    req.documentName = docName;
    req.objectName = objName;
    req.firstUtc = utcNow();
    pending.push_back(std::move(req));
    return true;
}

void Runtime::requestPending(Permission perm, const std::string &target)
{
    std::string principal = "session";
    std::string docName;
    std::string objName;
    if (!_ScopeStack.empty()) {
        principal = currentPrincipal();
        auto &top = _ScopeStack.back();
        if (top.doc)
            docName = top.doc->getName();
        if (top.owner && top.owner->getNameInDocument())
            objName = top.owner->getNameInDocument();
    }
    bool pendingChanged = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        pendingChanged = addPending(principal, perm, target, docName, objName);
        audit(principal, perm, target, Decision::Prompt,
                docName.empty() ? objName : docName + ":" + objName);
    }
    if (pendingChanged)
        signalPendingChanged();
}

void Runtime::grant(const std::string &principal, Permission perm,
        const std::string &target, bool allow, const std::string &scope,
        const std::string &displayLabel, const std::string &displayPath)
{
    std::string t = target.empty() ? std::string("*") : target;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        ensureLoaded();
        std::string key = principal + SEP + permissionName(perm) + SEP + t;
        Decision decision = allow ? Decision::Allow : Decision::Deny;
        if (scope == "once")
            onceAnswers[key] = decision;
        else if (scope == "session")
            sessionAnswers[key] = decision;
        else if (scope == "always") {
            Grant g;
            g.principal = principal;
            g.permission = permissionName(perm);
            g.target = t;
            g.allow = allow;
            g.displayLabel = displayLabel;
            g.displayPath = displayPath;
            _store.add(std::move(g));
            std::string err;
            if (!saveStore(&err))
                Base::Console().Warning(
                        "ExpressionSecurity: failed to save grants: %s\n", err.c_str());
        } else
            throw Base::ValueError("grant scope must be once/session/always");
        audit(principal, perm, t, decision, "grant:" + scope);
    }
    if (allow)
        clearPending(principal, perm, t);
    signalGrantsChanged();
}

std::size_t Runtime::revoke(const std::string &principal, Permission perm,
        const std::string &target)
{
    std::size_t count = 0;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        ensureLoaded();
        std::string t = target.empty() ? std::string("*") : target;
        count = _store.remove(principal, perm, t);
        // in-memory answers of the same shape go too
        for (auto *answers : {&onceAnswers, &sessionAnswers}) {
            for (auto it = answers->begin(); it != answers->end();) {
                auto pos1 = it->first.find(SEP);
                auto pos2 = it->first.rfind(SEP);
                bool match = it->first.compare(0, pos1, principal) == 0
                    && it->first.compare(pos1 + 1, pos2 - pos1 - 1,
                            permissionName(perm)) == 0
                    && (t == "*" || it->first.compare(pos2 + 1,
                            std::string::npos, t) == 0);
                if (match) {
                    it = answers->erase(it);
                    ++count;
                } else
                    ++it;
            }
        }
        if (count) {
            std::string err;
            if (!saveStore(&err))
                Base::Console().Warning(
                        "ExpressionSecurity: failed to save grants: %s\n", err.c_str());
            audit(principal, perm, t, Decision::Prompt, "revoke");
        }
    }
    if (count)
        signalGrantsChanged();
    return count;
}

void Runtime::clearOnce()
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    onceAnswers.clear();
}

void Runtime::addProcessGrant(Permission perm, const std::string &target)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::string t = target.empty() ? std::string("*") : target;
    processOverrides[permissionName(perm) + (SEP + t)] = Decision::Allow;
}

std::vector<Runtime::ActiveGrant> Runtime::activeGrants(const std::string &principal)
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    ensureLoaded();
    std::vector<ActiveGrant> result;
    for (const auto &g : _store.grants()) {
        if (g.principal != principal)
            continue;
        ActiveGrant a;
        a.principal = g.principal;
        std::string module;
        auto perm = permissionFromName(g.permission, &module);
        a.permission = perm ? *perm : Permission::DocReadSelf;
        a.target = g.target;
        a.allow = g.allow;
        a.scope = "always";
        a.granted = g.grantedUtc;
        result.push_back(std::move(a));
    }
    auto addAnswers = [&](const std::map<std::string, Decision> &answers,
            const char *scope) {
        for (const auto &v : answers) {
            auto pos1 = v.first.find(SEP);
            auto pos2 = v.first.rfind(SEP);
            if (pos1 == std::string::npos || pos2 <= pos1)
                continue;
            if (v.first.compare(0, pos1, principal) != 0)
                continue;
            ActiveGrant a;
            a.principal = principal;
            auto perm = permissionFromName(
                    v.first.substr(pos1 + 1, pos2 - pos1 - 1));
            if (!perm)
                continue;
            a.permission = *perm;
            a.target = v.first.substr(pos2 + 1);
            a.allow = v.second == Decision::Allow;
            a.scope = scope;
            result.push_back(std::move(a));
        }
    };
    addAnswers(sessionAnswers, "session");
    addAnswers(onceAnswers, "once");
    return result;
}

std::vector<PendingRequest> Runtime::pendingRequests(
        const std::string &documentName) const
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::vector<PendingRequest> result;
    for (auto &r : pending) {
        if (documentName.empty() || r.documentName.empty()
                || r.documentName == documentName)
            result.push_back(r);
    }
    return result;
}

void Runtime::clearPending(const std::string &principal, Permission perm,
        const std::string &target)
{
    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        auto before = pending.size();
        pending.erase(std::remove_if(pending.begin(), pending.end(),
                [&](const PendingRequest &r) {
                    return r.principal == principal && r.permission == perm
                        && (target == "*" || r.target == target);
                }),
                pending.end());
        changed = pending.size() != before;
    }
    if (changed)
        signalPendingChanged();
}

std::vector<std::pair<std::string, std::string>> Runtime::pendingObjects(
        const std::string &principal) const
{
    std::lock_guard<std::recursive_mutex> guard(mutex);
    std::set<std::pair<std::string, std::string>> seen;
    for (auto &r : pending) {
        if (r.principal == principal && !r.documentName.empty())
            seen.emplace(r.documentName, r.objectName);
    }
    return {seen.begin(), seen.end()};
}

////////////////////////////////////////////////////////////////////////////////////
//
// The chokepoint surface
//

void checkPermission(Permission perm, const std::string &target)
{
    Runtime::instance().check(perm, target);
}

void auditAllowed(Permission perm, const std::string &target, const std::string &context)
{
    Runtime::instance().auditAllowed(perm, target, context);
}

// Ring-0 module roots: in-image in the final design, no permission attached
// (docs/ExpressionSandboxPhase0.md sec 6.1).
static bool isRing0Module(const std::string &root)
{
    return root == "math" || root == "re" || root == "_sre"
        || root == "collections" || root == "builtins";
}

static std::string rootModule(const std::string &name)
{
    auto pos = name.find('.');
    return pos == std::string::npos ? name : name.substr(0, pos);
}

void checkModuleImport(const std::string &name)
{
    if (!Runtime::scopeActive())
        return;
    std::string root = rootModule(name);
    if (isRing0Module(root))
        return;
    if (root == "FreeCAD" || root == "App")
        checkPermission(Permission::AppQuery);
    else if (root == "FreeCADGui" || root == "Gui")
        checkPermission(Permission::Gui);
    else
        checkPermission(Permission::HostImport, name);
}

void checkGetattr(PyObject *base, const char *attr, PyObject *result)
{
    (void)attr;
    if (!Runtime::scopeActive())
        return;
    // A module reached by attribute walk is an import in disguise -- the
    // check the old code had commented out (ObjectIdentifier.cpp:762).
    if (result && PyModule_Check(result)) {
        const char *name = PyModule_GetName(result);
        checkModuleImport(name ? name : "?");
        return;
    }
    if (!base || PyModule_Check(base))
        return;
    // FreeCAD-bound objects are the typed surface: obj.Placement.Base.x
    // stays a typed read (docs/ExpressionSandbox.md sec 7.1).
    if (PyObject_TypeCheck(base, &Base::PyObjectBase::Type))
        return;
    // Plain value/container types are in-image Ring 0.
    if (base == Py_None || PyBool_Check(base) || PyLong_Check(base)
            || PyFloat_Check(base) || PyComplex_Check(base)
            || PyUnicode_Check(base) || PyBytes_Check(base)
            || PyByteArray_Check(base) || PyList_Check(base)
            || PyTuple_Check(base) || PyDict_Check(base)
            || PyAnySet_Check(base) || PySlice_Check(base)
            || PyRange_Check(base))
        return;
    // Anything else is an arbitrary Python instance: obj.Proxy.foo -- the
    // one deliberate compatibility break.
    checkPermission(Permission::UnsafeGetattr);
}

void checkCallablePermission(const std::string &name, PyObject *callable)
{
    if (!Runtime::scopeActive())
        return;
    // A method bound to a FreeCAD object is a typed call on that object,
    // not an import: shape.cut() is geom.call, obj.getSubObject() is a
    // document read.
    if (callable) {
        PyObject *self = PyObject_GetAttrString(callable, "__self__");
        if (!self)
            PyErr_Clear();
        else {
            bool isBase = PyObject_TypeCheck(self, &Base::PyObjectBase::Type);
            bool isDoc = PyObject_TypeCheck(self, &DocumentObjectPy::Type)
                || PyObject_TypeCheck(self, &DocumentPy::Type);
            Py_DECREF(self);
            if (isDoc) {
                checkPermission(Permission::DocReadSelf);
                return;
            }
            if (isBase) {
                checkPermission(Permission::GeomCall);
                return;
            }
        }
    }
    std::string root = rootModule(name);
    // Base/Units are pure value machinery (vector/placement/quantity math)
    if (isRing0Module(root) || root == "Base" || root == "Units")
        return;
    if (root == "FreeCAD" || root == "App" || root == "__FreeCADConsole__")
        checkPermission(Permission::AppQuery);
    else if (root == "FreeCADGui" || root == "Gui" || root == "Selection")
        checkPermission(Permission::Gui);
    else
        checkPermission(Permission::HostImport, name);
}

}  // namespace ExpressionSecurity
}  // namespace App
