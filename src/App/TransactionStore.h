/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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

#ifndef APP_TRANSACTION_STORE_H
#define APP_TRANSACTION_STORE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <FCGlobal.h>

namespace App
{

/** One committed transaction as the store keeps it (docs/TransactionLog.md
 * sec 13.2, table `txn`). `seq` is assigned by the store on append.
 */
struct LogTransaction
{
    int64_t seq {0};
    int64_t parent {0};      ///< seq of the transaction this one follows
    int id {0};              ///< App::Transaction::getID(), for grouping across documents
    std::string kind;        ///< "user", "implicit", "recompute", "restore", ...
    std::string origin;      ///< "gui", "python", ...
    std::string name;        ///< the transaction's display name
    double time {0};         ///< seconds since the epoch
    std::string script;      ///< the MacroManager lines, an annotation
    int64_t session {0};     ///< the session row this was written in
    /// The seq of the transaction this one is the inverse of (sec 24.2):
    /// an `undo` names what it undid, a `redo` the undo it redid. 0 else.
    int64_t inverts {0};
    /// The branch it was made on (sec 26), `main` being 1.
    int64_t branch {1};
    /// A `merge` row's second parent (sec 28.2 item 1): the seq of the head
    /// merged in. 0 on any other row.
    int64_t mergeFrom {0};
};

/** A branch row (sec 17.1, 26): a named tip into the `parent` tree.
 * `head` is the seq of its newest transaction, moved by every append on
 * it; the branch's history is the parent chain from there. `idBase` is
 * where its object ids started under the stride of sec 17.2, which the
 * file's own counter replaced (sec 27.40 item 1): 0 for any branch made
 * since. `lastId` the last id its document had, kept when it leaves it. `closed` is the
 * time it was closed, 0 while open.
 */
struct LogBranch
{
    int64_t id {0};
    std::string name;
    int64_t fromVersion {0};
    int64_t fromSeq {0};
    int64_t head {0};
    long idBase {0};
    long lastId {0};
    double created {0};
    double closed {0};
    /// The branch this one was made from when it was opened in a document
    /// of its own (sec 30.3 S.a): the default side of its merges. 0 for
    /// none.
    int64_t target {0};
};

/** A user row (sec 30.6 U1): who the author of a row is, known across
 * logins. One row per kind and name; the kinds are App::Actor's -- `local`,
 * the desktop user, named `host` unless the privacy preference names them
 * (sec 30.4 P3); `verified`, `invited`, `declared`.
 */
struct LogUser
{
    int64_t id {0};
    std::string kind;
    std::string name;
};

/** A session row (sec 11, 30.6): one login of one user -- the process's
 * own for the desktop user, opened with the log, and one more for each
 * admitted connection. `user` names its LogUser; `access` is what a
 * connection was admitted with (`view`, `edit`, `host`), empty for the
 * desktop's. `host` is the machine, recorded only if the preference says
 * so.
 */
struct LogSession
{
    int64_t id {0};
    int64_t env {0};
    int64_t user {0};
    std::string host;
    std::string access;
    double opened {0};
    double closed {0};
};

/** One op of a transaction (table `op`).
 *
 * `vbefore` / `vafter` are entity refs (LogEntity::hash), empty when there is
 * none: a `create` has no before, a `remove` no after, and an after that is
 * not yet resolved (sec 20.2, decision 4) is empty until resolveAfter().
 */
struct LogOp
{
    int64_t txn {0};
    int idx {0};
    std::string op;          ///< create, remove, set, addprop, delprop
    std::string ckind;       ///< "doc", "obj", "view"
    long cid {0};            ///< DocumentObject::getID(), 0 for the document
    std::string cname;       ///< object internal name (create/remove), else empty
    std::string ctype;       ///< object type name (create/remove), else empty
    std::string prop;        ///< property name, empty for create/remove
    std::string ptype;       ///< property type name
    std::string meta;        ///< dynamic-property metadata for addprop/delprop
    std::string vbefore;
    std::string vafter;
    bool derived {false};
    /// A set's touched state before it (sec 27.58), DocumentObject::
    /// LogTouchedBit packed: the property's bit and its object's. -1 when
    /// not recorded -- another op, a container that is no object, a write
    /// made during a recompute (the recompute record has the recompute's),
    /// a created object.
    /// The state after is the write's: the property touched, and the object
    /// too unless the property is an output.
    int touched {-1};
};

/// One edge out of an entity (table `ref`, sec 23.1): an attachment it
/// carries (`attach`, `name` the file name), the entity its delta applies
/// to (`base`), a part of a composite (`part`, `name` the property), or a
/// blob a value names or a blob reads (`blob`, sec 23.16).
struct LogRef
{
    std::string target;
    std::string role {"attach"};
    std::string name;
};

/** A stored entity (table `entity`, sec 23.1): anything the log holds
 * bytes for -- a property's fragment (`prop`), a file it handed to
 * addFile() (`attach`), a file of the document's blob store (`blob`), an
 * XML entry, a skeleton or a composite. `hash` is the identity: the SHA-1
 * of the full content (for a fragment, the fragment plus its ordered
 * attachment list), never of the stored form. `enc` says how `data` is
 * stored -- "raw", "zstd", "delta", a zstd patch against the entity `base`
 * names, or "file" (sec 23.16): the bytes are the blob store's file of
 * that hash, which the log holds, and `data` is its extension -- and a
 * re-encode changes `enc`/`base`/`data` under the same hash.
 */
struct LogEntity
{
    std::string hash;
    std::string kind {"prop"};
    std::string enc {"raw"};
    std::string base;               ///< the delta's base, when enc is "delta"
    std::string tier {"durable"};   ///< "durable" or "cache" (sec 10)
    uint64_t size {0};              ///< full bytes of the content
    std::string data;
    std::vector<LogRef> refs;

    /// The attachments, in order, as (name, hash).
    std::vector<std::pair<std::string, std::string>> attachments() const
    {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& r : refs) {
            if (r.role == "attach")
                out.emplace_back(r.name, r.target);
        }
        return out;
    }
};

/** A version row (sec 16.3): a snapshot of the document as a file, held
 * apart. `num` is the per-document number people and links use; `uuid`
 * tells two documents' "version 7" apart. `docxml_hash` is the SHA-1 of
 * the `Document.xml` the snapshot holds, which is also what a file on
 * disk is matched to a version by (sec 11, `save`).
 */
struct LogVersion
{
    int64_t num {0};
    std::string uuid;
    int64_t branch {1};             ///< the branch row it was taken on
    std::string kind {"unnamed"};   ///< "unnamed" or "named"
    std::string name;
    int64_t seq {0};                ///< the log sequence the snapshot is at
    int64_t env {0};
    std::string docxml_hash;
    int schema {0};                 ///< the document schema it was written under
    double created {0};
    std::string manifest;           ///< the entity holding its entries (sec 27.54)
};

/// One entry of a version's manifest: archive entry name -> entity hash.
/// The XML entries by their archive names, the blobs by the names a save
/// gives them under `blobs/` (sec 23.16). A version's entries are kept as
/// one entity (sec 27.54).
struct LogManifestEntry
{
    std::string entry;
    std::string hash;
};

/** One string of the file's string table (docs/TransactionLog.md sec 27.50
 * item 2, table `strtable`): the table is file-scope state, kept once, and
 * no version carries one. `sids` names the strings it is built from, as
 * `id` or `id:index`, space separated; `data` and `postfix` are its bytes as
 * the hasher holds them.
 */
struct LogString
{
    long id {0};
    int flags {0};
    std::string sids;
    std::string data;
    std::string postfix;
};

/** The interface the document sees: a log is appended, read and truncated
 * through this and nothing else (sec 13.2). openSQLite() is the one
 * implementation.
 */
class AppExport TransactionStore
{
public:
    virtual ~TransactionStore() = default;

    /// Append one transaction with its ops, atomically. Returns its seq;
    /// the ops' `txn` and `idx` are filled in. A `seq` set ahead (> 0) is
    /// used as given, so a caller that numbers on one thread and writes
    /// on another can name the row before it exists; 0 takes the next.
    /// The head of `txn.branch` moves to it.
    virtual int64_t append(LogTransaction& txn, std::vector<LogOp>& ops) = 0;
    /// Fill the after ref of an op left pending by append().
    virtual void resolveAfter(int64_t txn, int idx, const std::string& hash) = 0;

    virtual bool hasEntity(const std::string& hash) = 0;
    /// Store an entity with its refs; a hash already present is left as it is.
    virtual void putEntity(const LogEntity& entity) = 0;
    /// Read an entity back, `data` as stored, refs in insertion order.
    /// False if absent.
    virtual bool getEntity(const std::string& hash, LogEntity& entity) = 0;
    /// Change how an entity is stored, under the same hash (sec 23.2): the
    /// new `enc`, `base` and `data`; the `base` ref is replaced. Refs of
    /// other roles stay.
    virtual void reencodeEntity(const std::string& hash, const std::string& enc,
                                const std::string& base, const std::string& data) = 0;
    /// The entities whose delta is based on `hash`.
    virtual std::vector<std::string> basedOn(const std::string& hash) = 0;
    /// Add an edge to an entity already stored, after its others; one
    /// already there is left as it is.
    virtual void addRef(const std::string& entity, const LogRef& ref) = 0;
    /// Every entity stored as `enc`, by hash.
    virtual std::vector<std::string> entitiesStoredAs(const std::string& enc) = 0;

    /// Transactions with seq >= from, in order, at most `limit` (0: all).
    virtual std::vector<LogTransaction> transactions(int64_t from = 0, int limit = 0) = 0;
    virtual std::vector<LogOp> ops(int64_t txn) = 0;
    virtual bool getOp(int64_t txn, int idx, LogOp& op) = 0;
    /// The transactions on the parent chain ending at `head`, oldest
    /// first, those with seq >= from (sec 26): one branch's history. A
    /// chain ends where a row's parent is 0 or no longer stored.
    virtual std::vector<LogTransaction> chain(int64_t head, int64_t from = 0) = 0;
    /// Every stored row the `parent` and `mergeFrom` edges reach from
    /// `head`, itself included, oldest first (sec 28.2 item 1): a row's
    /// history, where chain() is one branch's.
    virtual std::vector<LogTransaction> history(int64_t head) = 0;
    /// The newest op on property `prop` of container (`ckind`, `cid`) in a
    /// transaction after `after` on the chain ending at `head` (0: in any
    /// transaction); false when there is none (sec 24.4).
    virtual bool lastOpOn(const std::string& ckind, long cid, const std::string& prop,
                          int64_t after, int64_t head, LogOp& op) = 0;
    virtual int64_t lastSeq() = 0;
    /// The largest object id any op names (sec 27.40 item 1), 0 if none.
    virtual long maxObjectId() = 0;
    /** The file's object names (sec 27.40 item 3): the `objname` table's
     * pairs, then every `create` op's (id, name), newest first -- the
     * table is written only into an embedded copy, and the ops cover what
     * came after it.
     */
    virtual std::vector<std::pair<long, std::string>> objectNames() = 0;
    /// Add `names` to the `objname` table; a pair whose id or name it has
    /// is left out.
    virtual void addObjectNames(const std::vector<std::pair<long, std::string>>& names) = 0;
    /// The last geometry id of each object (sec 27.40 item 4), table
    /// `lastgeoid`: (object id, id).
    virtual std::vector<std::pair<long, long>> lastGeoIds() = 0;
    /// Raise the rows of `ids`, adding the missing ones.
    virtual void addLastGeoIds(const std::vector<std::pair<long, long>>& ids) = 0;
    /// Every object id an op names (sec 27.47).
    virtual std::vector<long> objectIdsInOps() = 0;
    /// Drop the `objname` and `lastgeoid` rows of `ids` (sec 27.47).
    virtual void removeObjectState(const std::vector<long>& ids) = 0;

    /// Add `strings` to the file's string table (sec 27.50 item 2); an id
    /// the table has is left as it is.
    virtual void addStrings(const std::vector<LogString>& strings) = 0;
    /// The strings of `ids`, all of them when `ids` is empty, in id order.
    virtual std::vector<LogString> strings(const std::vector<long>& ids = {}) = 0;
    /// Every id the string table holds, in order.
    virtual std::vector<long> stringIds() = 0;
    /// Drop the strings of `ids` (a compaction, sec 27.50 item 4).
    virtual void removeStrings(const std::vector<long>& ids) = 0;
    /// Drop the whole string table: an embedded copy, whose file carries
    /// the table as a member of its own.
    virtual void clearStrings() = 0;
    /** The string ids entity `owner` uses (sec 27.50 item 4, 27.51 Q4), as
     * inclusive ranges: a version's manifest (the ids its save marked), a
     * value (the ids its capture listed). Dropped with the entity.
     */
    virtual void addStringRefs(const std::string& owner,
                               const std::vector<std::pair<long, long>>& ranges) = 0;
    /// Every range any entity holds, merged, in order.
    virtual std::vector<std::pair<long, long>> stringRefs() = 0;

    /// Drop every transaction with seq < before, and the entities nothing
    /// reaches any more (sec 23.5).
    virtual void truncate(int64_t before) = 0;
    /// Drop these transactions with their ops, and the entities nothing
    /// reaches any more, atomically (sec 16.7, trimming).
    virtual void removeTransactions(const std::vector<int64_t>& seqs) = 0;
    /// Rewrite row `txn.seq` -- its fields and its ops -- and drop the rows
    /// `seqs`, then the entities nothing reaches, atomically (sec 16.7,
    /// squash). The ops' `txn` and `idx` are filled in.
    virtual void replaceTransactions(const LogTransaction& txn, std::vector<LogOp>& ops,
                                     const std::vector<int64_t>& seqs) = 0;

    /// Id of the environment row holding `json`, made if absent (sec 11).
    virtual int64_t environment(const std::string& json) = 0;
    virtual std::string environmentJson(int64_t id) = 0;
    /// Id of the user row of `kind` and `name`, made if absent (sec 30.6).
    virtual int64_t user(const std::string& kind, const std::string& name) = 0;
    virtual std::vector<LogUser> users() = 0;
    virtual int64_t openSession(int64_t env, int64_t user, const std::string& host,
                                const std::string& access, double opened) = 0;
    virtual void closeSession(int64_t id, double closed) = 0;
    virtual std::vector<LogSession> sessions() = 0;

    /// Append a version with its manifest, atomically; `num` is assigned
    /// (the next number, or as preset when > 0, as for append) and returned.
    virtual int64_t addVersion(LogVersion& version,
                               const std::vector<LogManifestEntry>& manifest) = 0;
    virtual std::vector<LogVersion> versions() = 0;
    /// The highest version number, 0 when there is none.
    virtual int64_t lastVersion() = 0;
    virtual bool getVersion(int64_t num, LogVersion& version) = 0;
    /// The latest version whose Document.xml hashes to `hash`, or false.
    virtual bool findVersion(const std::string& docxmlHash, LogVersion& version) = 0;
    virtual std::vector<LogManifestEntry> manifest(int64_t num) = 0;
    /// Remove a version and its manifest, and the entities nothing reaches
    /// any more (sec 16.3, eviction). Ops are never removed by this.
    virtual void evictVersion(int64_t num) = 0;
    /// evictVersion() of each of `nums`, collected once.
    virtual void evictVersions(const std::vector<int64_t>& nums) = 0;
    /// Make a version named (kind `named`, never evicted) with `name`; an
    /// empty name makes it unnamed again. False if there is no such version.
    virtual bool nameVersion(int64_t num, const std::string& name) = 0;

    /// The branches, by id (sec 26). Every store has `main`, id 1.
    virtual std::vector<LogBranch> branches() = 0;
    virtual bool getBranch(int64_t id, LogBranch& branch) = 0;
    virtual bool findBranch(const std::string& name, LogBranch& branch) = 0;
    /// Add a branch; `id` is assigned and returned. A name already taken
    /// throws.
    virtual int64_t addBranch(LogBranch& branch) = 0;
    /// False if there is no such branch; a name already taken throws.
    virtual bool renameBranch(int64_t id, const std::string& name) = 0;
    /// Write a branch row's fields back, all but `head`, which only an
    /// append moves. False if there is no such branch.
    virtual bool updateBranch(const LogBranch& branch) = 0;
    /// Drop a branch row; its transactions and versions are the caller's
    /// (sec 16.7). False if there is no such branch.
    virtual bool removeBranch(int64_t id) = 0;
    /** Fast-forward branch `id` to `head` (sec 30.4 P1): its head moves
     * there, and the rows `seqs` -- another branch's, which `id` now holds
     * as its own -- and the versions taken at them become `id`'s. False if
     * there is no such branch.
     */
    virtual bool forwardBranch(int64_t id, int64_t head, const std::vector<int64_t>& seqs) = 0;
    /** The versions taken at the rows `seqs` are taken at row `to` instead
     * (sec 30.4 P1): for records after `to` with nothing between that
     * changed the document, which a fast-forward takes off the chain --
     * the versions are the state at `to`, and stay on it.
     */
    virtual void anchorVersions(const std::vector<int64_t>& seqs, int64_t to) = 0;

    virtual std::string getMeta(const std::string& key) = 0;
    virtual void setMeta(const std::string& key, const std::string& value) = 0;

    /// A consistent, compacted copy of the whole store at `path` (SQLite's
    /// VACUUM INTO); the file must not exist. What the embedded mode ships.
    virtual void copyTo(const std::string& path) = 0;
    /// Rewrite the store without its free pages (SQLite's VACUUM): what
    /// the embedded copy does once retention has emptied them (sec 27.53).
    virtual void vacuum() = 0;
    /// Drop every entity of `tier` that no manifest reaches and no delta
    /// is based on (the embedded copy carries no cache tier, sec 13.3).
    /// The ops' refs stay; the entity goes. The versions `evict` go first,
    /// as evictVersions() would take them, in the same collection -- the
    /// embedded copy's retention, which paid for three (sec 27.69).
    virtual void dropTier(const std::string& tier, const std::vector<int64_t>& evict) = 0;

    /// Open or create the SQLite log at `path` (WAL, synchronous=NORMAL).
    /// Throws Base::RuntimeError on failure.
    static std::unique_ptr<TransactionStore> openSQLite(const std::string& path);
};

} // namespace App

#endif // APP_TRANSACTION_STORE_H
