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
class Property;
class PropertyContainer;
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
class AppExport TransactionLog
{
public:
    explicit TransactionLog(Document& doc);
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

    /// One recomputed object, for the recompute record.
    struct RecomputedObject
    {
        long id;
        std::string name;
        bool error;
        std::string message;
    };
    /// The recompute pseudo transaction (sec 11): no ops, a record of what
    /// was recomputed under which environment, how long it took, and how
    /// each object came out. Written at Document::signalRecomputed.
    void onRecompute(const std::vector<RecomputedObject>& objects, double seconds);

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
    /// `script` its JSON. Returns its seq.
    int64_t record(const char* kind, const std::string& name, const std::string& script);

    /// Put the log on branch `id` (sec 26): the next row follows its head.
    /// Kept in `meta` so a recovery continues on it. False if no such branch.
    bool setBranch(int64_t id);
    /// Forget what the log knew of the live values (sec 26): after the
    /// document was made another state without a transaction.
    void forgetLiveValues();


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

    /// The unnamed version between saves (sec 16.3), from
    /// Document::snapshotToLog: like onSave, with a `snapshot` record.
    int64_t onSnapshot(const Captures& entries, const Blobs& blobs, int schema);

    int64_t session() const { return _session; }
    int64_t environment() const { return _environment; }

    /// Serialise the live value behind every pending after ref. What a
    /// version snapshot does first; also what makes the log complete for
    /// a reader that wants the head state from the log alone.
    void resolvePending();
    size_t pendingCount() const { return _pending.size(); }

    /// The store, for reading: what it returns waits for every queued
    /// write before each call, so the reference can be kept.
    TransactionStore& store();
    const std::string& path() const { return _path; }
    /// Wait until every queued job has been written.
    void flush();
    /// Transactions numbered so far (the last seq, queued writes included).
    int64_t lastSeq() const { return _nextSeq; }
    /// The branch the document is on (sec 26) and its head: the seq of its
    /// newest row, queued writes included, which the next row follows.
    int64_t branch() const { return _branch; }
    int64_t head() const { return _head; }

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

    /// Open the store under the document's current transient directory.
    void openStore();
    /// A version from the file's entries plus the record (`save` or
    /// `restore`) that names it; what onSave and onRestore share.
    int64_t snapshot(const char* kind, const std::string& path, const Captures& entries,
                     const Blobs& blobs, int schema);
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

    /// The delta policy, read on the main thread as each job is posted so
    /// the worker never touches the preferences.
    std::atomic<long> _deltaHops {0};
    std::atomic<long> _deltaRatio {0};
    void run();

    Document& _doc;
    std::string _path;
    std::string _envJson;
    std::string _user;
    std::string _host;
    int64_t _environment {0};
    int64_t _session {0};
    std::unique_ptr<TransactionStore> _store;
    class FlushingStore;
    std::unique_ptr<FlushingStore> _reader;
    /// The last seq and version number handed out; main thread only.
    int64_t _nextSeq {0};
    int64_t _nextVersion {0};
    /// The current branch and its head (sec 26); main thread only.
    int64_t _branch {1};
    int64_t _head {0};
    /// An embedded copy was just adopted: the next onRestore is its version.
    bool _adopted {false};
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
    /// Property id -> the hash of its newest value. Worker thread only.
    std::unordered_map<int64_t, std::string> _hashById;
    /// What a capture on the worker needs of the document.
    CaptureConfig _config;
    /// The blobs stored as `file` (sec 23.16), held so the document's
    /// blob store keeps their files: every one a version or a value
    /// names, until the collector drops it or a delta replaces it
    /// (releaseBlobs). Worker thread, or a flushed caller.
    std::unordered_map<std::string, FileBlobHandle> _blobs;
    /// The blobs putSources() has read. Worker thread.
    std::unordered_set<std::string> _sourced;

    std::thread _worker;
    std::mutex _mutex;
    std::condition_variable _wake;
    std::condition_variable _done;
    std::deque<std::function<void()>> _queue;
    bool _running {false};
    /// Copies the worker has written, released on the main thread: a copy
    /// may unregister itself from a list the main thread walks (an
    /// expression engine's copy leaves PropertyExpressionContainer's), so
    /// its last reference is never dropped on the worker (sec 25.4).
    std::vector<std::shared_ptr<const Property>> _retired;
    std::mutex _retiredMutex;
    std::thread::id _mainThread {std::this_thread::get_id()};
    void retire(std::shared_ptr<const Property>&& copy);
    void releaseRetired();
    /// Write what is queued, then stop and join the worker.
    void stopWorker();
    /// The logs alive in the process; `log` null stops every one's worker,
    /// which an atexit handler does.
    static void liveLogs(TransactionLog* log, bool add);
    bool _stop {false};
};

} // namespace App

#endif // APP_TRANSACTION_LOG_H
