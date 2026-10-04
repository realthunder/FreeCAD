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

#ifndef APP_TRANSACTION_LOG_H
#define APP_TRANSACTION_LOG_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <FCGlobal.h>

#include <Base/Writer.h>

#include "FileBlobManager.h"
#include "TransactionStore.h"
#include "TransactionValue.h"

namespace App
{

class Document;
class FileHistory;
class Property;
class PropertyContainer;
class StringHasher;
class Transaction;

/** The document's transaction log (docs/TransactionLog.md).
 *
 * Written at commit from the in-memory Transaction: one op per object
 * created, removed or changed, one per property touched, with values
 * content-addressed in the store. The rules of sec 20.2 apply --
 *
 * - only detached copies are serialised at commit. A set's *before* is the
 *   copy the undo system took at the first write; its *after* is left
 *   pending and resolved when the property is next copied (the next
 *   transaction to touch it) or when resolvePending() is called for a
 *   snapshot. The live property is never serialised on the commit path.
 * - a value whose copy isSame() as the live property is not serialised,
 *   unless an earlier op is waiting on it.
 * - a value is the fragment plus its attachments, each hashed on its own.
 * - a create is the create op plus one pending set per persisted property;
 *   a remove is one resolved set (before only) per property plus the op.
 *
 * Mode is DocumentParams::TransactionLog: 0 off, 1 session (the store lives
 * in the transient directory and dies with it). Derived values follow
 * DocumentParams::TransactionLogDerived (sec 10).
 *
 * The writer thread (sec 20.2, decision 4). The commit path on the main
 * thread decides what is written -- the ops, which copies are worth
 * serialising, what they resolve -- and numbers the transaction; the
 * serialising, hashing, compressing and the store writes run on one
 * worker, in commit order. A copy handed over is co-owned
 * (TransactionObject::PropData::shared) so it outlives its transaction's
 * eviction until written. The main thread owns the sequence counters and
 * the pending map; the worker owns the store while jobs are queued, and
 * every main-thread read goes through store(), which waits for the queue
 * to drain first.
 */
class TransactionLogCore;
struct Actor;

class AppExport TransactionLog
{
public:
    /** The log of `doc`: a cursor over its file's shared log (sec 27.7).
     * With `at`, the cursor of a version document (sec 27.5 ruling 3): at
     * that version and on no branch until the first change, which takes
     * the branch whose tip the version is when no document holds it, and
     * otherwise makes one, `<branch>@v<num>`. Without it, on the branch the
     * store names, held for this document.
     */
    explicit TransactionLog(Document& doc, const LogVersion* at = nullptr);
    ~TransactionLog();

    TransactionLog(const TransactionLog&) = delete;
    TransactionLog& operator=(const TransactionLog&) = delete;

    /// The commit hook, called with the transaction about to move to the
    /// undo stack, or with the inverse an undo or redo just recorded (kind
    /// `undo`/`redo`, `inverts` the seq of the row it inverts, sec 24.2).
    /// Returns the seq of the row written, 0 when nothing was. Never
    /// throws; a store failure is reported and skipped.
    int64_t onCommit(const Transaction& txn, const char* kind = "user",
                     const char* origin = "", int64_t inverts = 0);

    /// One object a recompute made up to date or failed on, for the
    /// recompute record, or one whose touched state it changed otherwise.
    struct RecomputedObject
    {
        long id;
        double seconds;      ///< in its execute(); 0 when only its touches were purged
        bool error;
        std::string message;
        /// False for an object the recompute neither made up to date nor
        /// failed on, whose touched state it changed all the same -- one it
        /// marked for a recompute it did not run (a partial recompute).
        bool done {true};
        /// The touched state just before the recompute (sec 27.58): the
        /// object's DocumentObject::LogTouchedBit bits and the names of its
        /// touched properties; `before` is -1 for an object the recompute
        /// made. `after` and `afterProps` the same once it is done.
        int before {-1};
        std::vector<std::string> beforeProps;
        int after {0};
        std::vector<std::string> afterProps;
    };
    /// The recompute pseudo transaction (sec 11, 27.53): no ops, a record
    /// of the objects the recompute made up to date or failed on -- not
    /// every one it looked at -- by id, with the time each took, the error
    /// text of a failure, and the whole duration, and each one's touched
    /// state before and after (sec 27.58). The environment is the row's
    /// session's. Written at Document::signalRecomputed; with a transaction
    /// `open`, after that transaction's row instead (sec 27.59), since the
    /// writes made before the recompute are in it.
    void onRecompute(const std::vector<RecomputedObject>& objects, double seconds,
                     Transaction* open = nullptr);

    /// The archive entries a version holds, as read: (name, bytes), the
    /// first being Document.xml, then GuiDocument.xml when there is one.
    using Entries = std::vector<std::pair<std::string, std::string>>;
    /// The same as written (sec 23.3): each entry captured by the writer,
    /// cut at every property body, Document.xml first.
    using Captures = std::vector<std::pair<std::string, Base::EntryCapture>>;
    /// The blobs a version holds (sec 23.16): each under the name a save
    /// gives it inside `blobs/` (FileBlobManager::collectedEntries(), or
    /// restoredEntries() as read), with the handle the log takes over.
    using Blobs = std::vector<std::pair<std::string, FileBlobHandle>>;
    /** A version of a file as read (sec 27.29): its blobs, less the ones only
     * the document's History property names. Read off the XML, the only
     * record of who refers to what at that point: a hash in Document.xml's
     * History element and in no other entry goes.
     */
    static Blobs versionBlobs(const Entries& entries, const Blobs& blobs);

    /** The property sink a save or snapshot serialises under (sec 23.3).
     *
     * Called before Document::Save: the pending after refs are resolved
     * so the log is complete, and the sink returned is set on the writer.
     * In record mode (`compose` false, a real save) it claims nothing and
     * every property's bytes are captured; in compose mode (a snapshot)
     * it claims every property whose newest value the log already holds,
     * and only the rest run Save. TransactionLogVerify makes compose mode
     * serialise the claimed ones too and compare on the worker.
     */
    Base::PropertySink* beginSnapshot(bool compose);

    /** The save record and the unnamed version it makes (sec 11, 16.3).
     *
     * Called from Document::save once the file's entries are written:
     * `entries` the XML entries as they streamed out (captured, never
     * serialised twice), `blobs` every blob the file carries, `schema` the
     * schema it was written under. Each entry becomes a composite (23.3):
     * its skeleton plus one part per property; each blob an entity the log
     * holds (23.16). Returns the version number, 0 on failure (reported,
     * not thrown).
     */
    int64_t onSave(const std::string& path, const Captures& entries, const Blobs& blobs,
                   int schema);

    /** The history initialised from a file (sec 16.6).
     *
     * Called from Document::restore with the file as found: `entries` the
     * XML entries as the readers saw them (tapped, sec 16.6), `blobs`
     * every blob the file carried, `schema` what it was written under.
     * Version 1 is that snapshot and a `restore` record follows it; the
     * ops start at the next transaction. Only an empty store is
     * initialised: a store with history is the embedded mode's concern
     * (the hash guard of 16.4) and is left alone with a warning.
     * Returns the version number, 0 when nothing was written.
     */
    int64_t onRestore(const std::string& path, const Entries& entries, const Blobs& blobs,
                      int schema);

    /** The embedded copy (sec 16.4, 13.3), for a save in `embedded` mode.
     *
     * Waits for the queue, copies the store compacted to a file in the
     * history directory, applies the retention policy to the copy -- the
     * unnamed versions and the cache tier go, the ops and the named
     * versions stay -- and stamps it with the save id, date and version
     * counter the guard on open compares. Returns the copy's path and
     * every blob the copy still holds as a file (23.16) -- a surviving
     * version's or an op value's -- with its extension; `version` is the
     * number this save becomes.
     */
    struct Embedded
    {
        std::string path;
        std::vector<std::pair<std::string, std::string>> blobs;   ///< (hash, ext)
        std::string saveId;
        int64_t version {0};
    };
    Embedded embed(const std::string& saveDate);
    /** The copy a save to the log only puts into the file (sec 27.28): the
     * file's document is not written, so the copy carries the file's own
     * save id and date -- the guard on open then still matches -- and
     * leaves the branch the file reopens on as the store has it.
     */
    Embedded embedForFile(const std::string& saveDate, const std::string& saveId);

    /** Continue from an embedded copy (sec 16.4): the live store is
     * replaced by `path`'s content, the counters follow the copy's, and
     * the on-open snapshot then becomes the version the copy expects.
     * Only for a store with no history of its own yet. The copy's blobs
     * are in the document's blob store by then (PropertyHistory restored
     * them); the log takes a handle on each.
     */
    bool adoptStore(const std::string& path);
    /** The embedded copy of a file edited elsewhere (sec 16.6, 26.2 item
     * 7): adopted as adoptStore() does, every branch of it closed and its
     * `main` renamed `main@<save date>`, and a new, empty `main` opened, so
     * the file as found becomes its first version, numbered on.
     */
    bool adoptClosed(const std::string& path);

    /// What recover() found.
    struct RecoverInfo
    {
        /// Sessions the crashed process never closed, closed now.
        std::vector<int64_t> crashedSessions;
        /// The label and file name the crashed session last recorded.
        std::string label;
        std::string fileName;
    };
    /** Crash recovery (docs/TransactionLog.md sec 25.2 item 4): take over
     * the log and the blob store a crashed session left in `oldDir`, its
     * transient directory. `history/log.db` moves in with its WAL -- the
     * rows committed and not yet checkpointed -- the blob segments and
     * loose files move into this document's store and are swept
     * (FileBlobManager::recoverStore()), and the log holds its blobs again.
     * The sessions the crashed process left open are closed and a new one
     * opened. Only for a log with no history of its own; the caller ends
     * the blob store's recovery (FileBlobManager::endRecovery()) once the
     * document is rebuilt.
     */
    bool recover(const std::string& oldDir, RecoverInfo& info);
    /// The label and file name a recovery shows before anything is read
    /// (meta `label`, `file`), from a leftover store; false if none.
    static bool readRecoveryMeta(const std::string& oldDir, RecoverInfo& info);
    /// Keep the document's label and file name in `meta` for a recovery.
    void noteIdentity();
    /// Append the `recover` record (sec 25.2), `script` its JSON.
    int64_t recordRecovery(const std::string& script);
    /// Append a record with no ops on the current branch: `kind`, `name`,
    /// `script` its JSON; `mergeFrom` a merge's second parent (sec 28.2).
    /// Returns its seq.
    int64_t record(const char* kind, const std::string& name, const std::string& script,
                   int64_t mergeFrom = 0);

    /// Put the log on branch `id` (sec 26): the next row follows its head.
    /// Kept in `meta` so a recovery continues on it. False if no such branch.
    bool setBranch(int64_t id);
    /** Put this cursor, a version document's not yet on a branch, on a new
     * branch `name` made from branch `target` (sec 30.3 S.a): made where the
     * cursor is, with no record of its own, so its head is the target's.
     * Returns its id; throws on a name taken.
     */
    int64_t forkHere(int64_t target, const std::string& name);
    /// The cursor follows its branch to `head`, where the document is
    /// moved without a row (sec 30.4 P1, a fast-forward).
    void moveHead(int64_t head);
    /// Append record `t` again at the head, as it was made but for its
    /// number (sec 30.4 P1): a record a fast-forward took off the chain.
    int64_t reappend(LogTransaction t);
    /// Whether another document of the file stands on one of these rows.
    bool othersStandOn(const std::vector<int64_t>& seqs) const;
    /// On no branch yet: a version document before its first change.
    bool detached() const { return _branch == 0; }
    /// The version a detached cursor is at, 0 for none.
    int64_t detachedAt() const { return _at; }
    /** Sec 27.5 ruling 3, one version open once: the document of the file
     * that is version `version` -- a version document not yet changed, or
     * one whose branch has not changed since that version -- null if none.
     */
    Document* documentAt(const LogVersion& version);
    /// The document of the file holding branch `id`, null if none.
    Document* holderOf(int64_t id) const;
    /// Every document of the file with a log (sec 27.7).
    std::vector<Document*> documents() const;
    /// Forget what the log knew of the live values (sec 26): after the
    /// document was made another state without a transaction.
    void forgetLiveValues();
    /// The same for one property of the document, written as bookkeeping
    /// rather than in a transaction (sec 27.5). Main thread.
    void forgetValue(const Property& prop);


    /** What a cold undo needs of row `seq` (sec 24.3): its ops in log
     * order, and each value they restore read back by hash -- a value the
     * log no longer has (a derived one evicted from the cache tier) is
     * simply absent. Every blob such a value names, and every blob those
     * read, is in the document's blob store again when this returns: a
     * blob the log kept as a delta is decoded and adopted, and held as a
     * file once more. False when the row does not exist.
     */
    struct Revert
    {
        std::vector<LogOp> ops;
        std::map<std::string, CapturedValue> values;
    };
    bool readRevert(int64_t seq, Revert& out);
    /// Make every blob value `hash` names live in the file's store, as
    /// readRevert() does for the values it reads (sec 27.34): a value applied
    /// forward names its blobs by hash, and one the log keeps as a delta is
    /// not live until decoded.
    void restoreBlobsOf(const std::string& hash);

    /// The unnamed version between saves (sec 16.3), from
    /// Document::snapshotToLog: like onSave, with a `snapshot` record.
    int64_t onSnapshot(const Captures& entries, const Blobs& blobs, int schema,
                       const char* kind = "snapshot");
    /** The string ids the version the next save or snapshot records uses
     * (docs/TransactionLog.md sec 27.50 item 4): what its save marked. A
     * version recorded without them -- a file as read -- keeps every id the
     * file's hasher holds.
     */
    void noteVersionStrings(std::vector<long> ids)
    {
        _versionStrings = std::move(ids);
        _haveVersionStrings = true;
    }

    /// The process's own session, the desktop user's (sec 30.6).
    int64_t session() const;
    int64_t environment() const;
    /** A connection was admitted (sec 30.6 U2): its session is opened --
     * under its user, made if this is the first login of them -- and a
     * `login` record written at the head, a row with no ops whose
     * annotation says who, how the name is known and with what access.
     * Returns the row's seq; 0 for a version document not yet changed,
     * where the session alone says it, and for the desktop user, who logs
     * in by opening the log.
     */
    int64_t login(const Actor& actor);
    /// The connection left: its session is closed, which is all that says
    /// so (sec 30.6 U2). False when the log had no session of it.
    bool logout(const Actor& actor);

    /** What the rows written while it is set are known by (sec 30.13 F3):
     * an imported row is the fork's own, so it goes under the session it
     * was made in there -- brought over by importSession() -- with the
     * ordinal and the time it had. A second import of the same fork then
     * finds what is already here, and a merge back finds its base.
     */
    struct Stamp
    {
        int64_t session {0};
        int64_t ordinal {0};
        double time {0};
        /// Set to the row a commit wrote under it; 0 while none has.
        int64_t seq {0};
    };
    /// Null ends it. The stamp is the caller's, and outlives the rows.
    void setStamp(Stamp* stamp) { _stamp = stamp; }
    /** The session here of a session of another copy of the file (sec
     * 30.13 F3, F4): the one with its uuid, made if this store has none --
     * closed, with the times it had, its environment and its user brought
     * over. `file` names the copy: its desktop user is not this file's, and
     * comes as a user of kind `fork` named `<name> (<file>)`; any other is
     * who they were. Main thread.
     */
    int64_t importSession(const LogSession& session, const LogUser& user,
                          const std::string& environment, const std::string& file);

    /// Serialise the live value behind every pending after ref. What a
    /// version snapshot does first; also what makes the log complete for
    /// a reader that wants the head state from the log alone.
    void resolvePending();
    size_t pendingCount() const { return _pending.size(); }

    /// The store, for reading: what it returns waits for every queued
    /// write before each call, so the reference can be kept.
    TransactionStore& store();
    const std::string& path() const;
    /// Wait until every queued job has been written.
    void flush();
    /// Transactions numbered so far (the last seq, queued writes included).
    int64_t lastSeq() const;
    /// The branch the document is on (sec 26) and its head: the seq of its
    /// newest row, queued writes included, which the next row follows.
    int64_t branch() const { return _branch; }
    int64_t head() const { return _head; }

    /// One row as the touched state reads it (sec 27.63): its script and
    /// its sets' ops, the values left out.
    struct TouchedRow
    {
        int64_t seq {0};
        int64_t parent {0};
        std::string kind;
        std::string script;
        std::vector<LogOp> ops;
    };
    /** The rows from the head back to `seq`, both included, newest first
     * (sec 27.63). From memory for the rows this document numbered lately,
     * so an undo of a step still hot never waits for the worker; else from
     * the store. False when `seq` is not on the head's chain, or a row on
     * the way is gone.
     */
    bool rowsBackTo(int64_t seq, std::vector<TouchedRow>& rows);
    /// Sec 27.16: a version document about to be saved as its file takes
    /// its branch now, as its first change would.
    void takeBranch() { ensureBranch(); }
    /// The name of the branch the document is on, or, before its first
    /// change, of the one it will take (sec 27.23: its name shows it).
    std::string branchName();

    /** The store follows the transient directory.
     *
     * A restore replaces the document's Uid with the file's, and the
     * transient directory is renamed after it; the store is already open
     * there by then (the on-open snapshot needs it before the parse). The
     * database handle would survive a rename, its WAL sidecar, which SQLite
     * finds by path, would not: closeStore() before the rename and
     * reopenStore() after it, which returns false when the store could
     * not be opened at the new path -- the log is then unusable and the
     * document drops it.
     */
    void closeStore();
    bool reopenStore();

    /// Read a value back, decompressed, with its attachments inlined. A
    /// composite (23.3) comes back composed: the fragment is the entry's
    /// bytes, parts in place.
    bool readValue(const std::string& hash, CapturedValue& out);
    /// A composite's data (23.3): the skeleton hash and, in order, each
    /// part's offset in the skeleton, hash, container and property name.
    struct CompositePart
    {
        size_t offset {0};
        std::string hash;
        std::string container;
        std::string name;
    };
    // Exported on its own: MSVC does not export a nested class of a
    // dllexport class, and the Gui's log view decodes composites.
    struct AppExport Composite
    {
        std::string skeleton;
        std::vector<CompositePart> parts;
        std::string encode() const;
        bool decode(const std::string& data);
    };
    /// The full bytes of one entity, whatever its encoding (a delta chain
    /// is decoded through its bases, sec 23.2); the row itself, without
    /// `data`, into `entity` when asked. Worker or flushed caller.
    bool readBytes(const std::string& hash, std::string& out, LogEntity* entity = nullptr,
                   int depth = 0);
    /// The file of a blob the log holds full (sec 23.16), null when the
    /// log has none -- no such entity, or one kept as a delta, which
    /// readValue() decodes. Flushes first.
    FileBlobHandle heldBlob(const std::string& hash);
    /// How many blob files the log holds (sec 23.16). Flushes first.
    size_t heldBlobCount();

    /// zstd patch-from: `bytes` against `base` into `patch`, and back.
    /// False without zstd or on a codec error.
    static bool deltaEncode(const std::string& bytes, const std::string& base,
                            std::string& patch);
    static bool deltaDecode(const std::string& patch, const std::string& base, size_t size,
                            std::string& bytes);

private:
    struct Pending
    {
        int64_t txn;
        int idx;
        long cid;            ///< 0 for the document's own property
        bool view {false};   ///< the view provider of object `cid` (sec 24.9)
        std::string prop;
        std::string tier;
    };

    /// One value the worker serialises: the copy it owns a share of, the
    /// live property's id (what a composed snapshot claims it by), the
    /// tier, the op of the job's transaction whose before ref it fills
    /// (-1 for none) and the earlier op whose after ref it resolves
    /// (resolveTxn 0 for none).
    struct ValueTask
    {
        std::shared_ptr<const Property> copy;
        int64_t key {0};
        std::string tier;
        int opIndex {-1};
        int64_t resolveTxn {0};
        int resolveIdx {0};
        /// A link's value is the name of its target, which the worker
        /// cannot read once the target has left the document -- a link to
        /// an object removed in the same transaction would be logged empty
        /// -- nor safely while the main thread edits it. Links are captured
        /// on the main thread as the task is made (sec 24.3), here.
        CapturedValue captured;
        bool isCaptured {false};
        /// The op of the job's transaction whose after ref this value is,
        /// -1 for none: every commit's after values are copied and written
        /// with it (sec 25.4).
        int afterIndex {-1};
        /// Filled with the value's hash once written, for the copy kept in
        /// TransactionCopyCache.
        std::shared_ptr<std::string> hashOut;
        /// The hash the worker already wrote this copy under, when the
        /// copy is one it wrote as an after value: not serialised again.
        std::shared_ptr<std::string> hashIn;
        void captureNow(const CaptureConfig& config);
    };

    class Sink;
    friend class Sink;

    /// Open the file's store in the history's directory (the core's), and
    /// take the branch the store names.
    void openStore();
    /// This cursor on the branch the store's meta names, at its head.
    void pickBranch();
    /// Sec 27.5 ruling 3: a detached cursor's first row puts it on a branch.
    void ensureBranch();
    /// The branch ensureBranch() would take: true when it continues
    /// `from`, false when it makes a new one named `name`.
    bool planBranch(LogBranch& from, std::string& name);
    /// A version from the file's entries plus the record (`save` or
    /// `restore`) that names it; what onSave and onRestore share.
    int64_t snapshot(const char* kind, const std::string& path, const Captures& entries,
                     const Blobs& blobs, int schema);
    /// Serialise each task's copy and write what it fills and resolves.
    /// Worker thread.
    void writeValues(std::vector<ValueTask>& tasks, std::vector<LogOp>& ops);
    /// Take the pending entry of `key` into `task`, if there is one.
    void takePending(int64_t key, ValueTask& task);
    /// Queue a job for the worker, in order.
    void post(std::function<void()> job);
    /// Number a row: the next seq, on the current branch, following its
    /// head, which moves to it. Main thread.
    void number(LogTransaction& t);
    /// Keep row `t` in `_recent`, with the sets of `ops` (sec 27.63).
    void remember(const LogTransaction& t, const std::vector<LogOp>* ops);

    /// The file's history (docs/TransactionLog.md sec 27.7) and the shared
    /// half of its log in it: the store, the worker, the counters, the
    /// entities held (TransactionLogCore, in TransactionLog.cpp). What is
    /// below is this document's cursor on one branch.
    FileHistory& _history;
    TransactionLogCore& _c;

    Document& _doc;
    /// The current branch and its head (sec 26); main thread only.
    int64_t _branch {1};
    int64_t _head {0};
    /// The rows numbered lately, by seq, as rowsBackTo reads them; the
    /// oldest go past a few thousand. Main thread only.
    std::map<int64_t, TouchedRow> _recent;
    int64_t _recentAt {0};   ///< TransactionLogCore::_rewrites when it was kept
    /// An embedded copy was just adopted: the next onRestore is its version.
    bool _adopted {false};
    /// The identity the next rows take (setStamp), null for their own.
    Stamp* _stamp {nullptr};
    /// A detached cursor's version (sec 27.5), and the id base its branch
    /// will start at.
    int64_t _at {0};
    /// Property id -> the op whose after ref that property's next copy
    /// resolves; main thread only.
    std::unordered_map<int64_t, Pending> _pending;
    /// Property ids whose newest value the worker holds by hash (23.3):
    /// every copy posted with a key, every part a snapshot stored; taken
    /// out by a change whose value is not recorded. Main thread only.
    std::unordered_set<int64_t> _recorded;
    /// What the sink of the snapshot in progress did not claim, to join
    /// _recorded once the snapshot is posted. Main thread only.
    std::unordered_set<int64_t> _misses;
    std::unique_ptr<Sink> _sink;
    bool _verify {false};
    std::vector<long> _versionStrings;
    bool _haveVersionStrings {false};
    /// What a capture on the worker needs of the document.
    CaptureConfig _config;
    friend class TransactionLogCore;
};

/** The shared half of a file's log (docs/TransactionLog.md sec 27.7): what
 * every document of the file writes through -- the store, the worker that
 * writes it, the sequence and version counters, the entities held -- owned
 * by the file's history, so it outlives any one document. Each document's
 * TransactionLog is a cursor on one branch over it.
 */
class AppExport TransactionLogCore
{
public:
    explicit TransactionLogCore(FileHistory& history);
    ~TransactionLogCore();

    /// Open the store in the history's directory; the counters follow it.
    void openStore();
    /// The core of `history`'s log, made on first use (sec 27.7).
    static TransactionLogCore& of(FileHistory& history);
    /** Continue from an embedded copy (sec 16.4): the store replaced by
     * `path`'s content, the counters following it, a session opened. Only
     * for a store with no history of its own yet. The copy's blobs are in
     * the file's blob store by then; the log takes a handle on each.
     */
    bool adoptEmbedded(const std::string& path);
    /// Sec 16.6, 26.2 item 7: every branch of the copy just adopted closed,
    /// its `main` renamed, a new `main` made current; returns its id.
    int64_t closeAdopted();
    /// The branch the store names as the file's, and its head.
    int64_t metaBranch(int64_t& head);
    /** Record the file as found as a version, with no document (sec
     * 27.13): what TransactionLog::onRestore does for a document opened
     * from it -- `entries` the XML entries as read, Document.xml first,
     * `blobs` its blobs under their archive names. Returns the number.
     */
    int64_t recordFile(const std::string& path, const TransactionLog::Entries& entries,
                       const TransactionLog::Blobs& blobs, int schema);
    /// Write version `v` and its record `t`, numbered already, from the
    /// captured entries and blobs: the worker's half of a snapshot.
    void postVersion(LogVersion v, LogTransaction t, const TransactionLog::Captures& entries,
                     const TransactionLog::Blobs& blobs, int schema, const std::string& path,
                     std::vector<std::pair<long, long>> strings = {}, bool marked = true);
    /// The ids a version recorded with no marks keeps: every one the file's
    /// hasher holds (sec 27.50 item 4).
    std::vector<std::pair<long, long>> allStrings() const;

    /// The store, for reading: every call waits for the queue first, so the
    /// reference can be kept.
    TransactionStore& store();
    /// Read a value back (TransactionLog::readValue), and one entity's bytes.
    bool readValue(const std::string& hash, CapturedValue& out);
    bool readBytes(const std::string& hash, std::string& out, LogEntity* entity = nullptr,
                   int depth = 0);
    FileBlobHandle heldBlob(const std::string& hash);
    size_t heldBlobCount();
    bool readRevert(int64_t seq, TransactionLog::Revert& out);
    /// `branch`: the saving document's, the one the file reopens on
    /// (sec 27.16); 0 leaves the store's.
    TransactionLog::Embedded embed(const std::string& saveDate, int64_t branch = 0,
                                   const std::string& saveId = std::string());
    /// The documents of the file and their branches (sec 27.7).
    Document* holderOf(int64_t id) const;
    std::vector<Document*> documents() const;
    Document* documentAt(const LogVersion& version, bool frozen = false);
    /// Row `seq` is on the chain ending at `head`, and nothing after it
    /// there changed the document: only records (save, snapshot, switch).
    bool unchangedSince(int64_t head, int64_t seq);

    // The entity functions (sec 23).
    /// Store one captured entry as a composite (23.3): the parts resolved
    /// or stored, the skeleton, the composite row. `previous` is the last
    /// version's composite of the same entry, for supersession. Returns
    /// the composite's hash; `full` gets the SHA-1 of the entry's bytes
    /// when they are all in hand (record mode), else the composite hash.
    /// Worker thread.
    std::string putComposite(const std::string& entry, const Base::EntryCapture& capture,
                             const std::string& previous, std::string& full);
    /// Compose an entry's bytes from its composite's `data`. Worker or
    /// flushed caller.
    bool composeEntry(const std::string& data, std::string& out);
    /// Unnamed versions over `keep` (DocumentParams::TransactionLogKeepVersions,
    /// read when the job was posted) go, oldest first (sec 16.3). Worker
    /// thread, after a version is added.
    void evictVersions(long keep);
    /// Store a captured value (fragment and attachments) and return its ref.
    /// Each blob the value names (its `blobs`, decision 6b) is held and
    /// becomes a `blob` ref of the value. Worker thread.
    std::string putValue(const CapturedValue& value, const std::string& tier);
    /// Sec 23.16: a blob of the document's store as an entity, stored as
    /// `file` and held -- made, or made full again if the content had been
    /// kept as a delta -- and its hash returned. Worker thread, or the main
    /// thread with the queue drained.
    std::string putBlob(const FileBlobHandle& blob);
    /// The `blob` refs from a blob to the files it reads (FileBlob::sources),
    /// each of those stored and held in turn: what lets a blob a value
    /// names outlive the version that wrote it. Once per blob per log.
    /// Worker thread.
    void putSources(const FileBlobHandle& blob, int depth = 0);
    /// Make the blob `hash` a file of the document's store again, and the
    /// blobs it reads (sec 24.3); `ext` is its extension, which a blob kept
    /// as a delta no longer carries in `data`. Main thread, queue drained.
    bool restoreBlob(const std::string& hash, const std::string& ext, int depth = 0);
    /// The blob `hash` if the file's store has it live, null if not.
    FileBlobHandle liveBlob(const std::string& hash) const;
    /// Let go of the file of every blob no longer stored as `file`: gone
    /// to the collector, or kept as a delta. After anything that removes
    /// or re-encodes entities.
    void releaseBlobs();
    /// Store plain bytes as an entity of `kind` and return its hash.
    /// Worker thread.
    std::string putBytes(const std::string& bytes, const std::string& kind,
                         const std::string& tier);
    /// Sec 23.2: `older` has been superseded by `newer` -- the before of an
    /// op by its resolved after, a version's entry by the next version's.
    /// Re-encode `older` as a patch against `newer` when the policy allows:
    /// a chain no longer than TransactionLogDeltaHops, a patch no larger
    /// than TransactionLogDeltaRatio of the full. Worker thread.
    void supersede(const std::string& older, const std::string& newer);
    /// The longest delta chain hanging off `hash`, in hops.
    int chainBelow(const std::string& hash, int depth = 0);

    /** The file's strings into the store (docs/TransactionLog.md sec 27.50
     * item 2): those minted since the last call, or with `all` every one the
     * store lacks. Main thread; the worker writes them ahead of the job
     * posted next. The first call for a hasher takes the store's strings it
     * lacks into it first (loadStrings).
     */
    void syncStrings(bool all);
    /** The store's strings the file's hasher lacks, into it: a version or a
     * value read from the log carries no table (sec 27.50 item 3). Once per
     * hasher and store; memory holds every string the store does after.
     * Main thread.
     */
    void loadStrings();
    /** Every string id a retained version or value uses (sec 27.50 item 4,
     * 27.51 Q4), merged into ranges: what a compaction keeps besides what
     * memory holds. Main thread; waits for the worker.
     */
    std::vector<std::pair<long, long>> retainedStrings();
    /// A compaction dropped `ids` from the file's hasher: from the store too.
    void dropStrings(const std::vector<long>& ids);
    /// Sorted ids as the inclusive ranges the store keeps them in.
    static std::vector<std::pair<long, long>> idRanges(const std::vector<long>& ids);

    /// Open the process's own session, the desktop user's (sec 30.6): at
    /// open, and again whenever the store is replaced by another.
    void openProcessSession();
    /// Close every session this process opened, the logins' and its own.
    void closeSessions();
    /** The session of `actor` in this log (sec 30.3 S.b): the process's own
     * for none or for the desktop user, else the one of that login, opened
     * at its first use here -- its user row with it. Main thread.
     */
    int64_t sessionOf(const Actor* actor);
    /// Close the session of `actor`'s login; false when there is none.
    bool closeLogin(const Actor& actor);
    /** Where this history and another part (docs/TransactionLog.md sec
     * 30.3 S.e). Two files are one history when they hold a row in common,
     * each row known by its session's uuid and its ordinal there; the base
     * of a fork is the newest such row. `other` is the other file's store
     * and `otherHead` the head of the chain to look along; returns the
     * base's seq there and sets `ours` to its seq here, 0 and 0 when the
     * two hold nothing in common.
     */
    int64_t forkBase(TransactionStore& other, int64_t otherHead, int64_t& ours);

    void run();
    void post(std::function<void()> job);
    void flush();
    void stopWorker();
    void retire(std::shared_ptr<const Property>&& copy);
    void releaseRetired();
    /// The logs alive in the process; `core` null stops every one's worker,
    /// which an atexit handler does.
    static void liveLogs(TransactionLogCore* core, bool add);

    FileHistory& _history;
    std::string _path;
    std::string _envJson;
    /// The desktop user's name as recorded: `host` unless the privacy
    /// preference names them (sec 30.4 P3), and the machine's with it.
    std::string _localName;
    std::string _host;
    int64_t _environment {0};
    int64_t _localUser {0};
    int64_t _session {0};
    /// The sessions of the logins this process has seen, by login (sec
    /// 30.6); main thread.
    std::map<std::string, int64_t> _actorSessions;
    /// The last ordinal given in each session of this process (sec 30.3
    /// S.e); main thread.
    std::map<int64_t, int64_t> _ordinals;
    std::unique_ptr<TransactionStore> _store;
    class FlushingStore;
    std::unique_ptr<FlushingStore> _reader;
    /// Counts the rewrites of stored rows -- a trim, a squash, a removal --
    /// after which a copy of a row may be stale (sec 27.63); main thread.
    int64_t _rewrites {0};
    /// The last seq and version number handed out; main thread only.
    int64_t _nextSeq {0};
    int64_t _nextVersion {0};
    /// The delta policy, read on the main thread as each job is posted so
    /// the worker never touches the preferences.
    std::atomic<long> _deltaHops {0};
    std::atomic<long> _deltaRatio {0};
    /// The ids the store's string table holds, and the largest, as posted
    /// (sec 27.50 item 2); the hasher whose strings the store's were last
    /// taken into (loadStrings). Main thread.
    std::unordered_set<long> _storedStrings;
    long _storedMax {0};
    const StringHasher* _stringsFor {nullptr};
    /// Property id -> the hash of its newest value. Worker thread only.
    std::unordered_map<int64_t, std::string> _hashById;
    /// The blobs stored as `file` (sec 23.16), held so the blob store keeps
    /// their files: every one a version or a value names, until the
    /// collector drops it or a delta replaces it (releaseBlobs). Worker
    /// thread, or a flushed caller.
    std::unordered_map<std::string, FileBlobHandle> _blobs;
    /// The blobs putSources() has read. Worker thread.
    std::unordered_set<std::string> _sourced;

    std::thread _worker;
    std::mutex _mutex;
    std::condition_variable _wake;
    std::condition_variable _done;
    std::deque<std::function<void()>> _queue;
    bool _running {false};
    bool _stop {false};
    /// Copies the worker has written, released on the main thread: a copy
    /// may unregister itself from a list the main thread walks (an
    /// expression engine's copy leaves PropertyExpressionContainer's), so
    /// its last reference is never dropped on the worker (sec 25.4).
    std::vector<std::shared_ptr<const Property>> _retired;
    std::mutex _retiredMutex;
    std::thread::id _mainThread {std::this_thread::get_id()};

    /// The documents' cursors, and which holds each branch: a branch is
    /// checked out by one document at most (sec 27.7). Main thread.
    std::set<TransactionLog*> _cursors;
    std::map<int64_t, TransactionLog*> _holders;
};


} // namespace App

#endif // APP_TRANSACTION_LOG_H
