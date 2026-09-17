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

#ifndef APP_EXPRESSION_SECURITY_H
#define APP_EXPRESSION_SECURITY_H

// The expression permission service: catalog, grant store, document-hash
// principal identity and audit log, exactly as frozen by
// docs/ExpressionSandboxPhase0.md sec 6 (contracts v1). This is the host
// side of the expression sandbox security model (docs/ExpressionSandbox.md
// sec 3); it never ships into the sandbox image.
//
// This file is the data/policy layer only. Wiring it in front of the
// evaluation chokepoints (and the prompt UX that requires) is a separate
// step -- nothing here changes evaluation behavior yet.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <FCConfig.h>

namespace App {
namespace ExpressionSecurity {

/// Permission catalog v1 (frozen). fs/net are deliberately not offered.
enum class Permission {
    DocReadSelf,    // doc.read.self
    DocWriteSelf,   // doc.write.self
    DocForeign,     // doc.foreign
    GeomCall,       // geom.call
    AppQuery,       // app.query
    PrefsRead,      // prefs.read -- the parameter store, read only, through a
                    // curated reader (Draft's get_param); ALLOW for every
                    // principal class (user ruling 2026-09-04: "allow read
                    // only for params anywhere")
    PrefsWrite,     // prefs.write -- the parameter store, written through the
                    // same curated facade (Draft's set_param, a panel's
                    // ParamGet(...).Set*): DENY for a document, not
                    // promptable; ALLOW for the session and addons (G3a,
                    // docs/Sandbox.md 7.11)
    AppWrite,       // app.write -- newDocument/closeDocument/setActiveDocument:
                    // the application's document set is the workbench's to
                    // change, never a document's (DENY, not promptable);
                    // ALLOW for the session and addons (S1, docs/Sandbox.md
                    // 7.13, ruling 2026-09-07)
    Gui,            // gui
    GuiDoCommand,   // gui.doCommand -- Gui.doCommand / addModule from the
                    // guest: the source runs in the CALLER's guest, never
                    // on the host; the host records the macro line and
                    // an audit line carrying the source's sha256 (S2,
                    // docs/Sandbox.md 7.13).  DENY for a document, not
                    // promptable; ALLOW session; PROMPT addon (the one
                    // permission an addon does not hold by default)
    HostImport,     // host.import:<module>, the module is the target
    UnsafeGetattr,  // unsafe.getattr
    PkgInstall,     // pkg.install:<package> -- an ACTION, not a grant: the
                    // sandbox guest asked for a package the user has not
                    // installed (docs/SandboxNetwork.md sec 9); only ever
                    // a pending request, never resolved by check()
    // The host file and code chokepoints (F1, docs/Sandbox.md 7.14 and
    // 7.29), the path as the target.  Checked INSIDE the core's file
    // primitives rather than at any command's name: a guest that runs a
    // host command by name (Gui.runCommand("Std_RecentMacros")) reaches
    // the same primitive and is stopped there, whatever the command is
    // called.  All three are DENY for a document (not promptable) and
    // PROMPT for the session and an addon -- the one row an addon does
    // not hold by default besides gui.doCommand.  A path the host's own
    // file dialog returned while a guest scope was active is blessed
    // (pathBlessed) and passes without a grant: consent is a capability
    // (S1), so the picker commands still work from a guest.
    FsRead,         // fs.read:<path> -- a host file read by path
    FsWrite,        // fs.write:<path> -- saveAs / saveCopy / exportTo
    HostExec,       // host.exec:<path> -- host Python run from a file
};

enum class Decision {
    Allow,
    Deny,
    Prompt,
};

/// Who is asking: the class of principal, derived from the principal id
/// string ("document:sha256:<hex>", "session", "addon:<name>",
/// "client:..." -- clientPrincipalId).
enum class PrincipalClass {
    Document,
    Session,
    Addon,
    /// A remote user reaching a served document from a sandbox guest in
    /// their own page (catalog v2, docs/Sandbox.md 7.20 C3).
    Client,
};

AppExport const char *permissionName(Permission perm);
AppExport const char *decisionName(Decision decision);

/** Parse a catalog permission name. Accepts the parameterized form
 * "host.import:<module>"; when it carries a module and target is non-null,
 * the module is returned there (the normalized permission is HostImport).
 */
AppExport std::optional<Permission> permissionFromName(
        const std::string &name, std::string *target = nullptr);

/// Classify a principal id string; nullopt if it matches no known form.
AppExport std::optional<PrincipalClass> principalClass(const std::string &principal);

/** The principal id of a remote client (docs/Sandbox.md 7.20, C3), from
 * what the scene server's door knows of its connection:
 *
 *     client:id:<identity>    the front door's verified identity
 *     client:grant:<n>        none: the grant that admitted it
 *     client:conn:<n>         neither (the legacy single-token door)
 *
 * Only the first names a person across runs; a grant id is assigned per
 * run and a connection id per connection, so those two are never
 * persisted (isPersistablePrincipal).  An identity carrying a control
 * character falls back to the next form rather than being rewritten --
 * a rewrite could make two identities one principal.
 */
AppExport std::string clientPrincipalId(const std::string &identity, uint64_t grant,
        uint64_t connection);

/// Whether a grant for this principal may be stored with scope "always":
/// false for a run-local client id and for an unknown form.
AppExport bool isPersistablePrincipal(const std::string &principal);

/// The frozen catalog default for (principal class, permission).
AppExport Decision catalogDefault(PrincipalClass pclass, Permission perm);

/** Whether a default DENY/PROMPT may be lifted interactively. v1 marks
 * four document cells not-promptable; v2's client column is promptable
 * only where it prompts (host.import, pkg.install).
 */
AppExport bool isPromptable(PrincipalClass pclass, Permission perm);

/** Whether any grant -- a panel answer, grants.json, a process --grant --
 * may lift the decision at all.  False only for a client's gui and
 * unsafe.getattr: both reach host code (a command by name runs a recent
 * macro file; an undeclared getattr runs host Python), so nothing
 * narrower than the whole row can be granted to a remote user.  The
 * chokepoints of 7.29 now stop the file and code half of that reach at
 * the primitive, but a remote user is still not offered the row.
 */
AppExport bool isGrantable(PrincipalClass pclass, Permission perm);

// ---- the host file and code chokepoints (F1, docs/Sandbox.md 7.14, 7.29) ----

/** A host path in the one spelling the gate compares by: absolute, with
 * symlinks and "." / ".." resolved as far as the file system allows and
 * '/' as the separator.  An empty path stays empty.  The blessed set and
 * every fs.read / fs.write / host.exec target go through this, so an
 * answer is keyed to the FILE rather than to whichever spelling of it
 * reached the primitive.
 */
AppExport std::string normalizeHostPath(const std::string &path);

/** The picker-blessed paths (S1, docs/Sandbox.md 7.13): a path the
 * host's own file dialog returned while a guest scope was active is a
 * capability -- the user chose that file in the guest's nested modal --
 * and the chokepoints accept it without a grant.  This is what keeps
 * consent a capability rather than a list: Std_Open from a guest opens
 * the file the user just picked, while a command with no picker
 * (Std_RecentFiles) has no blessed path and is refused.  The set lives
 * for the life of one guest and is cleared when a fresh guest boots.
 *
 * It lives in this always-built layer, not with the image bridge: the
 * chokepoints hold in a build with no sandbox image host at all.
 */
AppExport void blessPath(const std::string &path);
AppExport bool pathBlessed(const std::string &path);
AppExport void clearBlessedPaths();

/** The frozen pseudo-property -> permission mapping. Returns nullopt for
 * names needing no permission (_math/_re/_coll/_py are Ring-0 in-image
 * modules) and for unknown names. For _part/_cq the permission is
 * HostImport and hostModule (if non-null) receives the module name.
 */
AppExport std::optional<Permission> pseudoPropertyPermission(
        const std::string &pseudoName, std::string *hostModule = nullptr);

/// SHA-256 (FIPS 180-4) of a byte buffer as lowercase hex. Exposed for tests.
AppExport std::string sha256Hex(const void *data, std::size_t len);

/** Document-hash canonicalization v1 (frozen): the principal identity of a
 * document is a SHA-256 over its code strings ONLY -- sorted, deduplicated,
 * with a version prefix. Paths, cell addresses, Uids and file layout are
 * excluded, so moving code keeps grants while editing any code voids them.
 */
class AppExport DocumentHashBuilder {
public:
    /// A property-bound expression, in its persisted (exported) form.
    void addExpression(const std::string &expr);
    /// A spreadsheet formula cell, in its persisted form (address excluded).
    void addCell(const std::string &expr);
    /// (rung 2 forward-compat) an embedded script payload.
    void addScript(const std::string &moduleClass, const std::string &code);

    bool empty() const { return items.empty(); }

    /// "document:sha256:<64 hex>" over the canonical byte string.
    std::string principalId() const;

private:
    std::set<std::string> items;
};

/// One persisted grant (grants.json schema v1, frozen).
struct AppExport Grant {
    std::string principal;   // "document:sha256:<hex>" | "session" | "addon:<name>"
                             // | "client:id:<identity>" (v2; a v1 reader skips it)
    std::string permission;  // catalog name; HostImport stored as "host.import"
    std::string target;      // "*" or a specific target (doc name, module)
    bool allow = false;      // decision: allow / deny
    std::string grantedUtc;  // ISO 8601
    std::string displayLabel;  // metadata only, never matched
    std::string displayPath;   // metadata only, never matched
};

/** The persisted grant store (<UserAppData>/security/grants.json). All
 * paths are explicit here so the store is testable; the enforcement layer
 * owns the default location. "once"/"session" scoped answers never reach
 * this store -- everything in it is scope "always".
 */
class AppExport GrantStore {
public:
    /** Load a grants.json. A missing file yields an empty store and
     * succeeds. A schema version above 1 fails (migrate-forward only:
     * newer stores are not guessed at). Malformed JSON fails.
     */
    bool load(const std::string &path, std::string *errMsg = nullptr);
    /// Write the store; the write is to a temp file, then renamed over.
    bool save(const std::string &path, std::string *errMsg = nullptr) const;

    /** Grant lookup, frozen precedence: among grants matching (principal,
     * permission) with target equal to the query or "*", any explicit deny
     * outranks any allow. With no matching grant the per-store default for
     * the permission applies, if configured. Returns nullopt when the
     * store has no answer (caller falls back to catalogDefault()).
     */
    std::optional<Decision> lookup(const std::string &principal,
            Permission perm, const std::string &target) const;

    /// lookup() without the defaults fallback: grants only. The enforcement
    /// runtime uses this to try ancestor targets before any default applies.
    std::optional<Decision> lookupGrant(const std::string &principal,
            Permission perm, const std::string &target) const;

    /** Remove grants matching (principal, permission, target); a "*" target
     * removes every target of that permission. Returns the number removed.
     */
    std::size_t remove(const std::string &principal, Permission perm,
            const std::string &target);

    /// Add a grant (stamps grantedUtc if empty).
    void add(Grant grant);
    /// Remove every grant of a principal (e.g. its hash changed). Returns count.
    std::size_t removePrincipal(const std::string &principal);
    void clear();

    const std::vector<Grant> &grants() const { return _grants; }
    std::map<std::string, Decision> &defaults() { return _defaults; }
    const std::map<std::string, Decision> &defaults() const { return _defaults; }

private:
    std::vector<Grant> _grants;
    std::map<std::string, Decision> _defaults;  // keyed by permission name
};

/** Append-only decision log (sibling audit.log). One JSON object per line:
 * {"utc":...,"principal":...,"permission":...,"target":...,"decision":...,
 *  "context":...}. Headless default policy is DENY plus one audit line per
 * decision, never a hang.
 */
class AppExport AuditLog {
public:
    explicit AuditLog(std::string path);
    /// Append one decision line. Returns false if the file cannot be written.
    bool append(const std::string &principal, Permission perm,
            const std::string &target, Decision decision,
            const std::string &context = std::string());

private:
    std::string path;
};

/// Current UTC time as ISO 8601 ("2026-08-30T12:34:56Z"). Exposed for tests.
AppExport std::string utcNow();

}  // namespace ExpressionSecurity
}  // namespace App

#endif  // APP_EXPRESSION_SECURITY_H
