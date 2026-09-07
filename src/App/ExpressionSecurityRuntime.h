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

#ifndef APP_EXPRESSION_SECURITY_RUNTIME_H
#define APP_EXPRESSION_SECURITY_RUNTIME_H

// The expression permission ENFORCEMENT runtime (phase 1 step 3b), built on
// the step 3a data/policy layer (ExpressionSecurity.h). This is host-side
// code: it owns the evaluation principal, resolves every permission check
// against once/session answers, the persisted grant store and the frozen
// catalog defaults, records pending requests for the GUI panel, and appends
// to the audit log.
//
// The evaluation chokepoints in the core TUs (Expression.cpp,
// ObjectIdentifier.cpp) call only the free functions declared at the bottom
// (checkPermission / checkGetattr / checkModuleImport). When phase 1 step 4
// moves the core into the sandbox image, those calls become ops of the
// bridge dispatcher and the dispatcher calls this runtime; the runtime
// itself never moves.

#ifdef FC_EXPR_IMAGE
// The sandbox image build of the core TUs. Enforcement is host-side at the
// bridge dispatcher (docs/ExpressionSandbox.md sec 3.4): everything the
// image can reach outside itself crosses a permission-checked bridge op,
// and what is in-image is Ring 0 by construction. The chokepoints
// therefore compile to no-ops here; the types they name must still exist
// because the core catches and scopes them.

#include <string>

#include <Base/Exception.h>

#include "ExpressionSecurity.h"

typedef struct _object PyObject;

namespace App {

class DocumentObject;

namespace ExpressionSecurity {

class PermissionNeededException : public Base::Exception {
public:
    PermissionNeededException(std::string principal, Permission perm,
            std::string target, bool promptable)
        : principal(std::move(principal)), permission(perm),
          target(std::move(target)), promptable(promptable)
    {}

    const std::string &getPrincipal() const { return principal; }
    Permission getPermission() const { return permission; }
    const std::string &getTarget() const { return target; }
    bool isPromptable() const { return promptable; }

private:
    std::string principal;
    Permission permission;
    std::string target;
    bool promptable;
};

class Runtime {
public:
    class Scope {
    public:
        explicit Scope(const App::DocumentObject *) {}
        explicit Scope(const char *) {}
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;
    };
    static bool scopeActive() { return false; }
};

inline void checkPermission(Permission, const std::string & = "*") {}
inline void auditAllowed(Permission, const std::string &, const std::string & = std::string()) {}
inline void checkGetattr(PyObject *, const char *, PyObject *) {}
inline void checkModuleImport(const std::string &) {}
inline void checkCallablePermission(const std::string &, PyObject *) {}

}  // namespace ExpressionSecurity
}  // namespace App

#else  // FC_EXPR_IMAGE

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <Base/Exception.h>
#include <fastsignals/signal.h>

#include "ExpressionSecurity.h"

typedef struct _object PyObject;

namespace App {

class Document;
class DocumentObject;

namespace ExpressionSecurity {

/** Thrown by a permission check that does not resolve to ALLOW. Carries the
 * structured PermissionNeeded payload of docs/ExpressionSandbox.md sec 3.3:
 * recompute fails fast with this, the evaluation surfaces it as a cell or
 * object error, and the GUI panel offers the grant if promptable is true.
 */
class AppExport PermissionNeededException : public Base::Exception {
public:
    PermissionNeededException(std::string principal, Permission perm,
            std::string target, bool promptable);

    const std::string &getPrincipal() const { return principal; }
    Permission getPermission() const { return permission; }
    const std::string &getTarget() const { return target; }
    bool isPromptable() const { return promptable; }

    PyObject *getPyExceptionType() const override;

private:
    std::string principal;
    Permission permission;
    std::string target;
    bool promptable;
};

/// One unanswered PROMPT decision, recorded for the panel/indicator.
struct AppExport PendingRequest {
    std::string principal;
    Permission permission = Permission::DocReadSelf;
    std::string target;
    std::string documentName;  // owner document at the time of the check
    std::string objectName;    // owner object at the time of the check
    std::string firstUtc;
    int count = 1;             // repeat checks collapse into one entry
};

class AppExport Runtime {
public:
    static Runtime &instance();

    /** The evaluation principal, held on a per-thread stack. The expression
     * engine pushes a DOCUMENT scope (derived from the evaluation owner) at
     * its entry points only when the stack is empty, so an outer explicit
     * scope -- the expression editor or completer pushing "session" --
     * takes precedence for everything evaluated under it.
     */
    class AppExport Scope {
    public:
        /** Document principal of the owner's document (owner null or
         * document-less -> session: such expressions only come from host
         * code). This form is the evaluation-entry push: it pushes ONLY
         * when the stack is empty, so an outer explicit scope wins.
         */
        explicit Scope(const App::DocumentObject *owner);
        /// Explicit principal id: "session" or "addon:<name>".
        explicit Scope(const char *principalId);
        ~Scope();

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        bool pushed = false;
    };

    /// Whether any principal scope is active on this thread.
    static bool scopeActive();

    /** Resolve (current principal, perm, target). Returns silently on ALLOW.
     * On PROMPT: records a pending request, audits, throws with
     * promptable=true. On DENY: audits, throws with promptable per catalog.
     * No-op when enforcement is disabled or no scope is active (host code
     * calling outside any evaluation is host code -- already trusted).
     */
    void check(Permission perm, const std::string &target = "*");

    /** Record a pending request WITHOUT deciding or throwing: for the
     * things that are actions the user takes rather than permissions the
     * evaluation holds -- a sandbox package install (Permission::
     * PkgInstall, docs/SandboxNetwork.md sec 9.3).  Attributed to the
     * current scope's principal and owner, or to "session" outside any
     * evaluation; audited as a prompt; collapses repeats; fires
     * signalPendingChanged on a new entry.
     */
    void requestPending(Permission perm, const std::string &target);

    /** The decision for an explicit principal, without throwing or
     * recording. Precedence: process overrides (--grant), once answers,
     * session answers, persisted store, store defaults, catalog. For
     * HostImport the target falls back through its dotted ancestors
     * ("a.b.c" -> "a.b" -> "a") before "*".
     */
    Decision resolve(const std::string &principal, Permission perm,
            const std::string &target);

    // --- grant management (Python API, panel, headless flags) ---

    /// scope: "once" | "session" | "always". Fires signalGrantsChanged.
    /// displayLabel/displayPath are grants.json metadata (persisted scope).
    void grant(const std::string &principal, Permission perm,
            const std::string &target, bool allow, const std::string &scope,
            const std::string &displayLabel = std::string(),
            const std::string &displayPath = std::string());
    /// Remove a persisted grant; returns the number removed.
    std::size_t revoke(const std::string &principal, Permission perm,
            const std::string &target);
    /// Drop all once-scoped answers (after the re-run they were granted for).
    void clearOnce();
    /// Process-wide --grant override (headless); applies to every principal.
    void addProcessGrant(Permission perm, const std::string &target);

    /// One effective grant, whatever its scope, for panel display.
    struct ActiveGrant {
        std::string principal;
        Permission permission = Permission::DocReadSelf;
        std::string target;
        bool allow = false;
        std::string scope;    // "once" | "session" | "always"
        std::string granted;  // UTC, persisted grants only
    };
    /// Every effective grant of a principal (persisted + session + once).
    std::vector<ActiveGrant> activeGrants(const std::string &principal);

    // --- pending requests ---

    /// Pending requests, optionally filtered by owner document name
    /// (session/addon-principal requests match any document filter).
    std::vector<PendingRequest> pendingRequests(
            const std::string &documentName = std::string()) const;
    /// Clear pending entries matching (principal, perm, target); "*" matches.
    void clearPending(const std::string &principal, Permission perm,
            const std::string &target);
    /// Objects (documentName, objectName) that had pending requests for the
    /// principal -- the re-run set for the panel's grant action.
    std::vector<std::pair<std::string, std::string>> pendingObjects(
            const std::string &principal) const;

    /// Fired (under no internal lock) when the pending list changes.
    fastsignals::signal<void()> signalPendingChanged;
    /// Fired when grants change in any scope (panel refresh, verdict caches).
    fastsignals::signal<void()> signalGrantsChanged;

    // --- principals ---

    /// Cached "document:sha256:..." of a document; recomputed lazily after
    /// any of its expression containers change.
    std::string documentPrincipal(const App::Document *doc);
    /// The principal of the CURRENT scope (empty when none active).
    std::string currentPrincipal();

    /// One audit line for an ALLOWED action the current principal took --
    /// check() logs denials and prompts only, and a permission whose
    /// design asks for a trail of what was let through (gui.doCommand:
    /// the source's sha256 as the target, its length as the context;
    /// docs/Sandbox.md 7.1 U4) records it here.  Host code outside any
    /// scope is not logged.  Deduped like every other line.
    void auditAllowed(Permission perm, const std::string &target,
            const std::string &context = std::string());

    // --- configuration ---

    /// BaseApp/Preferences/Expression/Security:Enforce (default true).
    bool enforced() const;

    /// The persisted store (loaded lazily from grants.json or --policy).
    GrantStore &store();
    /// Persist the store now (no-op under --policy: policy files are input).
    bool saveStore(std::string *errMsg = nullptr);

    /// Use a --policy file INSTEAD of the user grants.json (read-only).
    void setPolicyFile(const std::string &path);

private:
    Runtime();
    void ensureLoaded();
    void audit(const std::string &principal, Permission perm,
            const std::string &target, Decision decision,
            const std::string &context);
    /// Add or bump a pending entry; true when it was new.  Caller holds
    /// the mutex.
    bool addPending(const std::string &principal, Permission perm,
            const std::string &target, const std::string &docName,
            const std::string &objName);

    mutable std::recursive_mutex mutex;
    GrantStore _store;
    bool loaded = false;
    std::string policyFile;

    // in-memory answers, keyed by principal + '\x1F' + perm + '\x1F' + target
    std::map<std::string, Decision> onceAnswers;
    std::map<std::string, Decision> sessionAnswers;
    // process-wide --grant overrides, keyed by perm + '\x1F' + target
    std::map<std::string, Decision> processOverrides;

    std::vector<PendingRequest> pending;

    // document principal cache, invalidated via application signals
    std::map<const App::Document *, std::string> docPrincipals;
};

// ---- the chokepoint surface (the only names the core TUs use) ----

/// Runtime::instance().check(...): throws PermissionNeededException unless
/// the current principal may perform perm on target.
AppExport void checkPermission(Permission perm, const std::string &target = "*");

/// Runtime::instance().auditAllowed(...): the audit line of an allowed
/// action (no check, no throw).
AppExport void auditAllowed(Permission perm, const std::string &target,
        const std::string &context = std::string());

/** The C7 gate (docs/ExpressionSandbox.md sec 7.1). Classifies a simple
 * attribute read during identifier drill-down: reads on FreeCAD-bound
 * objects (Base::PyObjectBase), modules and plain value/container types are
 * the typed surface and pass; a module RESULT requires the import
 * permission of that module; anything else -- an arbitrary Python instance,
 * an obj.Proxy.foo style walk -- requires unsafe.getattr.
 */
AppExport void checkGetattr(PyObject *base, const char *attr, PyObject *result);

/** The C4 gate. Ring-0 modules (math/re/_sre/collections/builtins) pass
 * without permission; FreeCAD/App map to app.query, FreeCADGui/Gui to gui,
 * anything else to host.import:<name>.
 */
AppExport void checkModuleImport(const std::string &name);

/// Create FreeCAD.ExpressionSecurity (the Python grant-management API) and
/// attach it to the FreeCAD module. Called once at application init.
AppExport void initPyModule(PyObject *appModule);

/** The callable gate behind CallableExpression::securityCheck: same module
 * classification as checkModuleImport, except a method bound to a
 * FreeCAD-object instance (its __self__ is a Base::PyObjectBase) checks
 * geom.call instead of host.import -- shape.cut() is a geometry call, not
 * an import. name may be a dotted attribution ("Part.foo").
 */
AppExport void checkCallablePermission(const std::string &name, PyObject *callable);

}  // namespace ExpressionSecurity
}  // namespace App

#endif  // FC_EXPR_IMAGE

#endif  // APP_EXPRESSION_SECURITY_RUNTIME_H
