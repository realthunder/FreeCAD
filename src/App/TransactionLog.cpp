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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <algorithm>
# include <chrono>
# include <map>
#endif

#ifdef FC_HAVE_ZSTD
# include <zstd.h>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Uuid.h>

#include "TransactionLog.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentParams.h"
#include "FileBlobManager.h"
#include "Property.h"
#include "Transactions.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace {

const char* tierFor(bool derived)
{
    // sec 10: none records no value, cache the evictable tier, full durable
    return (derived && DocumentParams::getTransactionLogDerived() == 1) ? "cache" : "durable";
}

bool recordsValue(bool derived)
{
    return !derived || DocumentParams::getTransactionLogDerived() != 0;
}

std::string dynamicMeta(const DynamicProperty::PropData& d)
{
    std::string m = d.group;
    m += '\n';
    m += d.getDoc() ? d.getDoc() : "";
    m += '\n';
    m += std::to_string(d.attr);
    m += d.readonly ? " ro" : "";
    m += d.hidden ? " hidden" : "";
    return m;
}

double now()
{
    return std::chrono::duration<double>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

/// What an op says about its container.
struct ContainerInfo
{
    const PropertyContainer* container;
    std::string ckind;
    long cid {0};
    std::string cname;
    std::string ctype;
};

ContainerInfo describe(Document& doc, const TransactionalObject* tobj, const std::string& nameInTxn)
{
    ContainerInfo c;
    c.container = tobj ? static_cast<const PropertyContainer*>(tobj)
                       : static_cast<const PropertyContainer*>(&doc);
    c.ctype = c.container->getTypeId().getName();
    if (auto obj = Base::freecad_dynamic_cast<const DocumentObject>(tobj)) {
        c.ckind = "obj";
        c.cid = obj->getID();
        c.cname = obj->getNameInDocument() ? obj->getNameInDocument() : nameInTxn;
    }
    else if (tobj) {
        // No id to find it by again: its pending refs resolve only through
        // the next copy, never through resolvePending().
        c.ckind = "view";
        c.cid = -1;
        c.cname = c.container->getFullName();
    }
    else {
        c.ckind = "doc";
    }
    return c;
}

} // namespace

/** What store() hands out: every call waits for the worker's queue to
 * drain, then forwards. A reference kept across commits stays safe, which
 * a bare reference to the store would not be.
 */
class TransactionLog::FlushingStore : public TransactionStore
{
public:
    explicit FlushingStore(TransactionLog& log) : _log(log) {}

    int64_t append(LogTransaction& txn, std::vector<LogOp>& ops) override
    { return inner().append(txn, ops); }
    void resolveAfter(int64_t txn, int idx, const std::string& hash) override
    { inner().resolveAfter(txn, idx, hash); }
    bool hasEntity(const std::string& hash) override { return inner().hasEntity(hash); }
    void putEntity(const LogEntity& entity) override { inner().putEntity(entity); }
    bool getEntity(const std::string& hash, LogEntity& entity) override
    { return inner().getEntity(hash, entity); }
    void reencodeEntity(const std::string& hash, const std::string& enc,
                        const std::string& base, const std::string& data) override
    { inner().reencodeEntity(hash, enc, base, data); }
    std::vector<std::string> basedOn(const std::string& hash) override
    { return inner().basedOn(hash); }
    void addRef(const std::string& entity, const LogRef& ref) override
    { inner().addRef(entity, ref); }
    std::vector<std::string> entitiesStoredAs(const std::string& enc) override
    { return inner().entitiesStoredAs(enc); }
    std::vector<LogTransaction> transactions(int64_t from, int limit) override
    { return inner().transactions(from, limit); }
    std::vector<LogOp> ops(int64_t txn) override { return inner().ops(txn); }
    bool getOp(int64_t txn, int idx, LogOp& op) override { return inner().getOp(txn, idx, op); }
    int64_t lastSeq() override { return inner().lastSeq(); }
    void truncate(int64_t before) override
    {
        inner().truncate(before);
        _log.releaseBlobs();
    }
    int64_t environment(const std::string& json) override { return inner().environment(json); }
    std::string environmentJson(int64_t id) override { return inner().environmentJson(id); }
    int64_t openSession(int64_t env, const std::string& user, const std::string& host,
                        double opened) override
    { return inner().openSession(env, user, host, opened); }
    void closeSession(int64_t id, double closed) override { inner().closeSession(id, closed); }
    std::vector<LogSession> sessions() override { return inner().sessions(); }
    int64_t addVersion(LogVersion& version, const std::vector<LogManifestEntry>& manifest) override
    { return inner().addVersion(version, manifest); }
    std::vector<LogVersion> versions() override { return inner().versions(); }
    int64_t lastVersion() override { return inner().lastVersion(); }
    bool getVersion(int64_t num, LogVersion& version) override
    { return inner().getVersion(num, version); }
    bool findVersion(const std::string& docxmlHash, LogVersion& version) override
    { return inner().findVersion(docxmlHash, version); }
    std::vector<LogManifestEntry> manifest(int64_t num) override { return inner().manifest(num); }
    void evictVersion(int64_t num) override
    {
        inner().evictVersion(num);
        _log.releaseBlobs();
    }
    void copyTo(const std::string& path) override { inner().copyTo(path); }
    void dropTier(const std::string& tier) override
    {
        inner().dropTier(tier);
        _log.releaseBlobs();
    }
    bool nameVersion(int64_t num, const std::string& name) override
    { return inner().nameVersion(num, name); }
    std::string getMeta(const std::string& key) override { return inner().getMeta(key); }
    void setMeta(const std::string& key, const std::string& value) override
    { inner().setMeta(key, value); }

private:
    TransactionStore& inner()
    {
        _log.flush();
        return *_log._store;
    }
    TransactionLog& _log;
};

/** The property sink (sec 23.3). Record mode claims nothing; compose
 * mode claims a property whose newest value the worker holds and no
 * pending after ref is waiting on. Whatever is not claimed is noted, so
 * the part the snapshot stores for it counts as held from then on.
 */
class TransactionLog::Sink : public Base::PropertySink
{
public:
    Sink(TransactionLog& log, bool compose) : _log(log), _compose(compose) {}

    bool claim(const Base::Persistence&, const char*, const Base::Persistence& prop,
               int64_t key) override
    {
        // Never a blob referrer (sec 23.16): its Save names the file this
        // save's own pass makes (a shape's beforeSave), and writes what
        // only the live property in its document knows (the hasher index),
        // so the value a detached copy gave the log is not its part.
        if (_compose && _log._recorded.count(key) && !_log._pending.count(key)
                && !dynamic_cast<const BlobReferrerProperty*>(&prop))
            return true;
        _log._misses.insert(key);
        return false;
    }
    bool verify() const override { return _compose && _log._verify; }
    bool composes() const override { return _compose; }

private:
    TransactionLog& _log;
    bool _compose;
};

Base::PropertySink* TransactionLog::beginSnapshot(bool compose)
{
    resolvePending();
    _misses.clear();
    _verify = DocumentParams::getTransactionLogVerify();
#ifdef FC_DEBUG
    _verify = true;
#endif
    _sink = std::make_unique<Sink>(*this, compose);
    return _sink.get();
}

TransactionLog::TransactionLog(Document& doc)
    : _doc(doc)
{
    openStore();

    // The environment row (sec 11): what App::Application::Config() knows
    // of this build, stored once and referred to by the session. Nothing
    // per-document records the kernel version today; this does.
    std::string env = "{";
    auto& config = Application::Config();
    const char* keys[] = {"ExeName", "ExeVersion", "BuildVersionMajor", "BuildVersionMinor",
                          "BuildVersionPoint", "BuildRevision", "BuildRevisionHash",
                          "BuildRevisionBranch", "BuildRevisionDate", "OCC_VERSION",
                          "PythonVersion", "QtVersion", "SystemName", nullptr};
    bool first = true;
    for (const char** k = keys; *k; ++k) {
        auto it = config.find(*k);
        if (it == config.end())
            continue;
        env += first ? "\"" : ",\"";
        first = false;
        env += *k;
        env += "\":\"";
        for (char c : it->second) {
            if (c == '"' || c == '\\')
                env += '\\';
            env += c;
        }
        env += '"';
    }
    env += '}';
    _envJson = env;
    if (DocumentParams::getTransactionLogIdentity()) {
        auto u = config.find("UserName");
        if (u != config.end())
            _user = u->second;
        auto h = config.find("HostName");
        if (h != config.end())
            _host = h->second;
    }
    _environment = _store->environment(_envJson);
    _session = _store->openSession(_environment, _user, _host, now());
    _config = CaptureConfig(doc);
    _worker = std::thread([this]() { run(); });
    FC_LOG("transaction log " << _path << " session " << _session);
}

void TransactionLog::run()
{
    std::unique_lock<std::mutex> lock(_mutex);
    for (;;) {
        _wake.wait(lock, [this]() { return _stop || !_queue.empty(); });
        if (_queue.empty()) {
            if (_stop)
                return;
            continue;
        }
        auto job = std::move(_queue.front());
        _queue.pop_front();
        _running = true;
        lock.unlock();
        try {
            job();
        }
        catch (Base::Exception& e) {
            FC_ERR("transaction log: " << e.what());
        }
        catch (std::exception& e) {
            FC_ERR("transaction log: " << e.what());
        }
        catch (...) {
            FC_ERR("transaction log: write failed");
        }
        lock.lock();
        _running = false;
        _done.notify_all();
    }
}

void TransactionLog::post(std::function<void()> job)
{
    _deltaHops = DocumentParams::getTransactionLogDeltaHops();
    _deltaRatio = DocumentParams::getTransactionLogDeltaRatio();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push_back(std::move(job));
    }
    _wake.notify_one();
}

void TransactionLog::flush()
{
    std::unique_lock<std::mutex> lock(_mutex);
    _done.wait(lock, [this]() { return _queue.empty() && !_running; });
}

TransactionStore& TransactionLog::store()
{
    if (!_reader)
        _reader = std::make_unique<FlushingStore>(*this);
    return *_reader;
}

void TransactionLog::openStore()
{
    std::string dir = _doc.TransientDir.getStrValue() + "/history";
    Base::FileInfo(dir).createDirectories();
    _path = dir + "/log.db";
    _store = TransactionStore::openSQLite(_path);
    if (_store->getMeta("document").empty())
        _store->setMeta("document", _doc.Uid.getValueStr());
    _nextSeq = _store->lastSeq();
    _nextVersion = _store->lastVersion();
    // An adopted embedded copy may have had every version dropped by its
    // retention; the counter it carries keeps the numbering monotonic.
    const std::string counter = _store->getMeta("version_counter");
    if (!counter.empty())
        _nextVersion = std::max<int64_t>(_nextVersion, std::stoll(counter) - 1);
}

TransactionLog::Embedded TransactionLog::embed(const std::string& saveDate)
{
    flush();
    Embedded out;
    out.saveId = Base::Uuid::createUuid();
    out.version = _nextVersion + 1;
    const std::string dir = _doc.TransientDir.getStrValue() + "/history";
    Base::FileInfo(dir).createDirectories();
    out.path = dir + "/embed-" + out.saveId + ".db";
    Base::FileInfo(out.path).deleteFile();
    _store->copyTo(out.path);
    auto copy = TransactionStore::openSQLite(out.path);
    // Retention (16.4, 13.3): the named versions travel, the unnamed ones
    // and the cache tier do not; the ops do.
    for (const auto& v : copy->versions()) {
        if (v.kind != "named")
            copy->evictVersion(v.num);
    }
    copy->dropTier("cache");
    // Every blob the copy still holds as a file (23.16): a kept version's,
    // and an op value's. The ones kept as deltas travel inside the copy.
    for (const auto& hash : copy->entitiesStoredAs("file")) {
        LogEntity e;
        if (!copy->getEntity(hash, e) || e.kind != "blob")
            continue;
        out.blobs.emplace_back(hash, e.data.empty() ? std::string() : "." + e.data);
    }
    copy->setMeta("save_id", out.saveId);
    copy->setMeta("save_date", saveDate);
    copy->setMeta("version_counter", std::to_string(out.version));
    copy.reset();
    return out;
}

bool TransactionLog::adoptStore(const std::string& path)
{
    flush();
    if (_nextSeq != 0 || _nextVersion != 0) {
        FC_WARN("transaction log of " << _doc.getName() << " has history; not adopting the embedded copy");
        return false;
    }
    if (_store && _session)
        _store->closeSession(_session, now());
    _store.reset();
    Base::FileInfo(_path).deleteFile();
    Base::FileInfo(_path + "-wal").deleteFile();
    Base::FileInfo(_path + "-shm").deleteFile();
    if (!Base::FileInfo(path).copyTo(_path.c_str())) {
        FC_ERR("cannot adopt the embedded history of " << _doc.getName());
        openStore();
        return false;
    }
    openStore();
    // The copy's blobs came with the file, restored into the document's
    // store for the History property; the log holds them from here.
    _blobs.clear();
    _sourced.clear();
    auto& manager = _doc.getFileBlobManager();
    for (const auto& hash : _store->entitiesStoredAs("file")) {
        if (auto blob = manager.find(hash))
            _blobs[hash] = blob;
        else
            FC_WARN("embedded history of " << _doc.getName() << ": blob " << hash
                    << " is not in the document's store");
    }
    _environment = _store->environment(_envJson);
    _session = _store->openSession(_environment, _user, _host, now());
    _adopted = true;
    FC_LOG("transaction log of " << _doc.getName() << " continues from the embedded copy: seq "
           << _nextSeq << ", next version " << (_nextVersion + 1));
    return true;
}

void TransactionLog::closeStore()
{
    flush();
    _store.reset();
}

bool TransactionLog::reopenStore()
{
    if (_store)
        return true;
    try {
        openStore();
        FC_LOG("transaction log moved to " << _path);
        return true;
    }
    catch (Base::Exception& e) {
        FC_ERR("cannot reopen the transaction log of " << _doc.getName() << ": " << e.what());
    }
    return false;
}

TransactionLog::~TransactionLog()
{
    try {
        flush();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _stop = true;
        }
        _wake.notify_all();
        if (_worker.joinable())
            _worker.join();
        _blobs.clear();
        if (_store && _session)
            _store->closeSession(_session, now());
    }
    catch (...) {
    }
}

void TransactionLog::onRecompute(const std::vector<RecomputedObject>& objects, double seconds)
{
    if (objects.empty())
        return;
    try {
        LogTransaction t;
        t.parent = _nextSeq;
        t.seq = ++_nextSeq;
        t.kind = "recompute";
        t.name = "recompute";
        t.time = now();
        t.session = _session;
        // The record, as JSON in the script column: environment, duration,
        // and per object its id, name, and error text if any.
        std::string j = "{\"env\":" + std::to_string(_environment)
                      + ",\"seconds\":" + std::to_string(seconds) + ",\"objects\":[";
        bool first = true;
        for (auto& o : objects) {
            j += first ? "{" : ",{";
            first = false;
            j += "\"id\":" + std::to_string(o.id) + ",\"name\":\"" + o.name + "\"";
            if (o.error) {
                j += ",\"error\":\"";
                for (char c : o.message) {
                    if (c == '"' || c == '\\')
                        j += '\\';
                    else if (c == '\n') {
                        j += "\\n";
                        continue;
                    }
                    j += c;
                }
                j += '"';
            }
            j += '}';
        }
        j += "]}";
        t.script = j;
        post([this, t]() mutable {
            std::vector<LogOp> none;
            _store->append(t, none);
        });
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    catch (std::exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
}

int64_t TransactionLog::onSave(const std::string& path, const Captures& entries,
                               const Blobs& blobs, int schema)
{
    return snapshot("save", path, entries, blobs, schema);
}

void TransactionLog::onUndoRedo(const Transaction& txn)
{
    for (auto& info : txn._Objects.get<0>()) {
        TransactionObject& rec = *info.second;
        for (auto& kv : rec._PropChangeMap)
            _recorded.erase(kv.first);
        if (rec.status == TransactionObject::Chn || !info.first)
            continue;
        std::map<std::string, Property*> props;
        static_cast<const PropertyContainer*>(info.first)->getPropertyMap(props);
        for (auto& kv : props)
            _recorded.erase(kv.second->getID());
    }
}

void TransactionLog::onCheckout(int64_t num)
{
    // The head state is the version's; pending after refs of the state
    // that was left describe values that are gone, so they are dropped
    // rather than resolved against the restored document.
    _pending.clear();
    _recorded.clear();
    LogTransaction t;
    t.parent = _nextSeq;
    t.seq = ++_nextSeq;
    t.kind = "checkout";
    t.name = "checkout";
    t.time = now();
    t.session = _session;
    t.script = "{\"version\":" + std::to_string(num) + "}";
    post([this, t]() mutable {
        std::vector<LogOp> none;
        _store->append(t, none);
    });
}

int64_t TransactionLog::onSnapshot(const Captures& entries, const Blobs& blobs, int schema)
{
    return snapshot("snapshot", _doc.FileName.getValue(), entries, blobs, schema);
}

int64_t TransactionLog::onRestore(const std::string& path, const Entries& entries,
                                  const Blobs& blobs, int schema)
{
    if (!_adopted && (_nextSeq != 0 || _nextVersion != 0)) {
        // A store with history at restore is either an adopted embedded
        // copy, whose version the file now becomes, or a mistake.
        FC_WARN("transaction log of " << _doc.getName() << " is not empty at restore");
        return 0;
    }
    _adopted = false;
    // As read, the entries are bytes: no parts to hold, the file itself.
    Captures captures;
    for (const auto& e : entries)
        captures.emplace_back(e.first, Base::EntryCapture(e.second));
    return snapshot("restore", path, captures, blobs, schema);
}

int64_t TransactionLog::snapshot(const char* kind, const std::string& path,
                                 const Captures& entries, const Blobs& blobs, int schema)
{
    if (entries.empty() || entries.front().first != "Document.xml") {
        FC_ERR("transaction log: a snapshot needs Document.xml first");
        return 0;
    }
    try {
        // Whatever the sink did not claim is stored by this snapshot as a
        // part, and held from here on; a snapshot taken without a sink
        // (restore) resolves what is pending itself.
        if (_sink) {
            _recorded.insert(_misses.begin(), _misses.end());
            _misses.clear();
            _sink.reset();
        }
        else {
            resolvePending();
        }

        // Numbered here, written by the worker after everything queued
        // before it -- the resolves above included, so the version's ops
        // are complete when it lands.
        LogVersion v;
        v.num = ++_nextVersion;
        v.uuid = Base::Uuid::createUuid();
        v.seq = _nextSeq;
        v.env = _environment;
        v.schema = schema;
        v.created = now();

        LogTransaction t;
        t.parent = _nextSeq;
        t.seq = ++_nextSeq;
        t.kind = kind;
        t.name = kind;
        t.time = v.created;
        t.session = _session;

        std::string escaped;
        for (char c : path) {
            if (c == '"' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        const size_t nblobs = blobs.size();
        const long keep = DocumentParams::getTransactionLogKeepVersions();
        post([this, v, t, entries, blobs = Blobs(blobs), schema, escaped, nblobs, keep]() mutable {
            // Each XML entry is a composite (sec 23.3): its skeleton plus
            // the parts, or the bytes themselves when it was read rather
            // than written; each blob an entity the log holds (23.16). The
            // previous version's entries are superseded by this one's (sec
            // 23.2): matched by name, re-encoded toward the newer, parts and
            // skeletons included. A blob's name is its referrer's
            // (`Box.Shape.brp`), so one property's files pair up too.
            std::map<std::string, std::string> previous;
            if (const int64_t prev = _store->lastVersion()) {
                for (const auto& e : _store->manifest(prev))
                    previous[e.entry] = e.hash;
            }
            std::vector<LogManifestEntry> manifest;
            std::string docHash;
            for (const auto& e : entries) {
                auto it = previous.find(e.first);
                std::string full;
                const std::string hash = putComposite(
                    e.first, e.second, it != previous.end() ? it->second : std::string(), full);
                // Document.xml, first by contract: matched to a file on
                // disk by the SHA-1 of the bytes (sec 11) when they were
                // all in hand, else named by its composite.
                if (docHash.empty())
                    docHash = full;
                manifest.push_back({e.first, hash, "entity"});
            }
            v.docxml_hash = docHash;
            for (const auto& b : blobs) {
                if (b.second)
                    manifest.push_back({b.first, putBlob(b.second), "entity"});
            }
            blobs.clear();   // the log holds what it keeps; the job lets go
            _store->addVersion(v, manifest);
            for (const auto& e : manifest) {
                auto it = previous.find(e.entry);
                if (it != previous.end() && it->second != e.hash)
                    supersede(it->second, e.hash);
            }
            evictVersions(keep);

            t.script = "{\"version\":" + std::to_string(v.num) + ",\"docxml\":\"" + docHash
                     + "\",\"blobs\":" + std::to_string(nblobs) + ",\"schema\":"
                     + std::to_string(schema) + ",\"path\":\"" + escaped + "\"}";
            std::vector<LogOp> none;
            _store->append(t, none);
        });
        return v.num;
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    catch (std::exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    return 0;
}

void TransactionLog::evictVersions(long keep)
{
    // Sec 16.3: unnamed versions over the limit go, oldest first; named
    // ones never, and never the newest, which is what the cadence and a
    // cold undo anchor on. Worker thread, after an addVersion; the limit
    // was read on the main thread when the job was posted.
    if (keep <= 0)
        return;
    auto versions = _store->versions();
    std::vector<int64_t> unnamed;
    for (const auto& v : versions) {
        if (v.kind == "unnamed")
            unnamed.push_back(v.num);
    }
    if (!unnamed.empty() && unnamed.back() == versions.back().num)
        unnamed.pop_back();   // the newest stays whatever the limit
    // What is left is the older unnamed ones; keep the last (keep - 1) of
    // them so that, with the newest, `keep` unnamed versions remain.
    size_t excess = unnamed.size() + 1 > static_cast<size_t>(keep)
                        ? unnamed.size() + 1 - static_cast<size_t>(keep) : 0;
    for (size_t i = 0; i < excess; ++i) {
        FC_LOG("transaction log: evict version " << unnamed[i]);
        _store->evictVersion(unnamed[i]);
    }
    if (excess)
        releaseBlobs();
}

namespace {

/// zstd at level 3 for anything over 128 bytes; smaller stays raw.
void compressInto(LogEntity& e, const std::string& bytes)
{
    e.size = bytes.size();
    e.data = bytes;
    e.enc = "raw";
#ifdef FC_HAVE_ZSTD
    if (bytes.size() > 128) {
        std::string out(ZSTD_compressBound(bytes.size()), '\0');
        size_t n = ZSTD_compress(out.data(), out.size(), bytes.data(), bytes.size(), 3);
        if (!ZSTD_isError(n)) {
            out.resize(n);
            e.data = std::move(out);
            e.enc = "zstd";
        }
    }
#endif
}

#ifdef FC_HAVE_ZSTD
/// The window has to cover the whole base for a match anywhere in it to
/// be found (zstd's --patch-from sets it the same way).
int windowLogFor(size_t baseSize)
{
    const ZSTD_bounds bounds = ZSTD_cParam_getBounds(ZSTD_c_windowLog);
    int log = bounds.lowerBound;
    while ((size_t(1) << log) < baseSize && log < bounds.upperBound)
        ++log;
    return log;
}
#endif

} // namespace

bool TransactionLog::deltaEncode(const std::string& bytes, const std::string& base,
                                 std::string& patch)
{
#ifdef FC_HAVE_ZSTD
    std::unique_ptr<ZSTD_CCtx, size_t (*)(ZSTD_CCtx*)> cctx(ZSTD_createCCtx(), ZSTD_freeCCtx);
    if (!cctx)
        return false;
    ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_compressionLevel, 3);
    ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_windowLog, windowLogFor(base.size() + bytes.size()));
    ZSTD_CCtx_setParameter(cctx.get(), ZSTD_c_enableLongDistanceMatching, 1);
    if (ZSTD_isError(ZSTD_CCtx_refPrefix(cctx.get(), base.data(), base.size())))
        return false;
    patch.assign(ZSTD_compressBound(bytes.size()), '\0');
    size_t n = ZSTD_compress2(cctx.get(), patch.data(), patch.size(), bytes.data(), bytes.size());
    if (ZSTD_isError(n))
        return false;
    patch.resize(n);
    return true;
#else
    (void)bytes; (void)base; (void)patch;
    return false;
#endif
}

bool TransactionLog::deltaDecode(const std::string& patch, const std::string& base,
                                 size_t size, std::string& bytes)
{
#ifdef FC_HAVE_ZSTD
    std::unique_ptr<ZSTD_DCtx, size_t (*)(ZSTD_DCtx*)> dctx(ZSTD_createDCtx(), ZSTD_freeDCtx);
    if (!dctx)
        return false;
    ZSTD_DCtx_setParameter(dctx.get(), ZSTD_d_windowLogMax,
                           ZSTD_dParam_getBounds(ZSTD_d_windowLogMax).upperBound);
    if (ZSTD_isError(ZSTD_DCtx_refPrefix(dctx.get(), base.data(), base.size())))
        return false;
    bytes.assign(size, '\0');
    size_t n = ZSTD_decompressDCtx(dctx.get(), bytes.data(), bytes.size(),
                                   patch.data(), patch.size());
    if (ZSTD_isError(n))
        return false;
    bytes.resize(n);
    return true;
#else
    (void)patch; (void)base; (void)size; (void)bytes;
    return false;
#endif
}

std::string TransactionLog::putValue(const CapturedValue& value, const std::string& tier)
{
    LogEntity e;
    e.kind = "prop";
    e.tier = tier;
    // Attachments first, each its own entity: a fragment that changes while
    // its geometry does not (a move at schema 5) shares the geometry.
    for (auto& a : value.attachments) {
        LogEntity ae;
        ae.kind = "attach";
        ae.tier = tier;
        ae.hash = hashBytes(a.bytes);
        e.refs.push_back(LogRef {ae.hash, "attach", a.name});
        if (_store->hasEntity(ae.hash))
            continue;
        compressInto(ae, a.bytes);
        _store->putEntity(ae);
    }
    std::string keyed = value.fragment;
    for (auto& r : e.refs) {
        keyed += '\0';
        keyed += r.name;
        keyed += '\0';
        keyed += r.target;
    }
    e.hash = hashBytes(keyed);
    // The blobs the fragment names (decision 6b) are edges, not part of
    // the identity: the fragment already spells their hashes (sec 23.16).
    std::vector<LogRef> blobRefs;
    for (const auto& blob : value.blobs) {
        if (!blob)
            continue;
        blobRefs.push_back(LogRef {putBlob(blob), "blob", ""});
        putSources(blob);
    }
    if (_store->hasEntity(e.hash)) {
        // Stored before, possibly as a part that named the files without
        // an edge to them.
        for (const auto& r : blobRefs)
            _store->addRef(e.hash, r);
        return e.hash;
    }
    e.refs.insert(e.refs.end(), blobRefs.begin(), blobRefs.end());
    compressInto(e, value.fragment);
    _store->putEntity(e);
    return e.hash;
}

std::string TransactionLog::putBlob(const FileBlobHandle& blob)
{
    const std::string& hash = blob->hash();
    LogEntity e;
    if (!_store->getEntity(hash, e)) {
        e = LogEntity();
        e.hash = hash;
        e.kind = "blob";
        e.enc = "file";
        e.tier = "durable";
        e.size = blob->size();
        e.data = blob->extension();
        _store->putEntity(e);
    }
    else if (e.enc == "delta") {
        // Superseded once and current again (a shape changed and changed
        // back): the newest is full (23.2), and here is its file.
        _store->reencodeEntity(hash, "file", "", blob->extension());
    }
    else if (e.enc != "file") {
        return hash;   // the same bytes stored in the log already
    }
    _blobs[hash] = blob;
    return hash;
}

void TransactionLog::putSources(const FileBlobHandle& blob, int depth)
{
    if (!blob || depth > 1024 || !_sourced.insert(blob->hash()).second)
        return;
    for (const auto& hash : blob->sources()) {
        if (auto source = _doc.getFileBlobManager().find(hash)) {
            putBlob(source);
            putSources(source, depth + 1);
        }
        else if (!_store->hasEntity(hash)) {
            FC_WARN("transaction log: blob " << blob->hash() << " reads " << hash
                    << ", which is in no store");
            continue;
        }
        _store->addRef(blob->hash(), LogRef {hash, "blob", ""});
    }
}

void TransactionLog::releaseBlobs()
{
    if (_blobs.empty())
        return;
    const auto files = _store->entitiesStoredAs("file");
    const std::unordered_set<std::string> kept(files.begin(), files.end());
    for (auto it = _blobs.begin(); it != _blobs.end();) {
        if (kept.count(it->first))
            ++it;
        else
            it = _blobs.erase(it);
    }
}

FileBlobHandle TransactionLog::heldBlob(const std::string& hash)
{
    flush();
    auto it = _blobs.find(hash);
    return it != _blobs.end() ? it->second : FileBlobHandle();
}

size_t TransactionLog::heldBlobCount()
{
    flush();
    return _blobs.size();
}

std::string TransactionLog::putBytes(const std::string& bytes, const std::string& kind,
                                     const std::string& tier)
{
    LogEntity e;
    e.kind = kind;
    e.tier = tier;
    e.hash = hashBytes(bytes);
    if (_store->hasEntity(e.hash))
        return e.hash;
    compressInto(e, bytes);
    _store->putEntity(e);
    return e.hash;
}

std::string TransactionLog::Composite::encode() const
{
    // One line per item: the skeleton first, then `c <container>` where
    // the container changes and `p <offset> <hash> <name>` per part.
    std::string out = "skeleton " + skeleton + '\n';
    std::string container;
    for (const auto& p : parts) {
        if (p.container != container) {
            container = p.container;
            out += "c " + container + '\n';
        }
        out += "p " + std::to_string(p.offset) + ' ' + p.hash + ' ' + p.name + '\n';
    }
    return out;
}

bool TransactionLog::Composite::decode(const std::string& data)
{
    parts.clear();
    skeleton.clear();
    std::string container;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos);
        if (end == std::string::npos)
            end = data.size();
        const std::string line = data.substr(pos, end - pos);
        pos = end + 1;
        if (line.compare(0, 9, "skeleton ") == 0) {
            skeleton = line.substr(9);
        }
        else if (line.compare(0, 2, "c ") == 0) {
            container = line.substr(2);
        }
        else if (line.compare(0, 2, "p ") == 0) {
            size_t a = line.find(' ', 2);
            size_t b = a == std::string::npos ? a : line.find(' ', a + 1);
            if (b == std::string::npos)
                return false;
            CompositePart p;
            p.offset = std::stoull(line.substr(2, a - 2));
            p.hash = line.substr(a + 1, b - a - 1);
            p.name = line.substr(b + 1);
            p.container = container;
            parts.push_back(std::move(p));
        }
        else if (!line.empty()) {
            return false;
        }
    }
    return !skeleton.empty();
}

std::string TransactionLog::putComposite(const std::string& entry,
                                         const Base::EntryCapture& capture,
                                         const std::string& previous, std::string& full)
{
    using Segment = Base::EntryCapture::Segment;
    const auto& segs = capture.segments;
    bool plain = true;
    for (const auto& s : segs) {
        if (s.kind != Segment::Text) {
            plain = false;
            break;
        }
    }
    if (plain) {
        // As read, or written by a container that made no parts: the
        // bytes are the entity, an `xml` like any other.
        full = putBytes(capture.bytes(), "xml", "durable");
        return full;
    }

    // Each part to its hash: what the worker holds for a claimed one,
    // stored from the bytes otherwise. A verified part carries its bytes
    // although claimed; a mismatch is the defect of 23.6, reported by
    // name, and the bytes win so the version is right.
    std::vector<std::string> hashes(segs.size());
    std::vector<bool> keep(segs.size(), true);
    bool allBytes = true;
    std::string container;
    for (size_t i = 0; i < segs.size(); ++i) {
        const Segment& s = segs[i];
        if (s.kind == Segment::Count) {
            container = s.name;
            continue;
        }
        if (s.kind != Segment::Part)
            continue;
        std::string hash;
        if (s.claimed) {
            auto it = _hashById.find(s.key);
            if (it == _hashById.end())
                throw Base::RuntimeError("transaction log: " + entry + ": " + container + "."
                                         + s.name + " claimed but not held");
            hash = it->second;
            if (!s.text.empty()) {
                const std::string fresh = hashBytes(s.text);
                if (fresh != hash) {
                    FC_ERR("transaction log: " << entry << ": " << container << "." << s.name
                           << " changed without aboutToSetValue (held " << hash << ", is "
                           << fresh << ")");
                    hash = putBytes(s.text, "prop", "durable");
                    _hashById[s.key] = hash;
                }
            }
            else {
                allBytes = false;
            }
        }
        else {
            hash = putBytes(s.text, "prop", "durable");
            _hashById[s.key] = hash;
        }
        hashes[i] = hash;
        // Equal to the shared default the file carries: left out, as the
        // save would have left it out (23.3).
        if (!s.elide.empty() && s.elide == hash)
            keep[i] = false;
    }

    // The counts: each Count's base plus the parts kept after it.
    std::vector<size_t> counts(segs.size(), 0);
    size_t current = segs.size();
    for (size_t i = 0; i < segs.size(); ++i) {
        if (segs[i].kind == Segment::Count)
            current = i;
        else if (segs[i].kind == Segment::Part && keep[i] && current < segs.size())
            ++counts[current];
    }

    // The skeleton, the composite, and the entry's bytes when they are
    // all here (record mode: what a file on disk is matched by, sec 11).
    Composite c;
    std::string skeleton;
    std::string composed;
    container.clear();
    for (size_t i = 0; i < segs.size(); ++i) {
        const Segment& s = segs[i];
        switch (s.kind) {
        case Segment::Text:
            skeleton += s.text;
            if (allBytes)
                composed += s.text;
            break;
        case Segment::Count: {
            container = s.name;
            const std::string n = std::to_string(
                std::stoull(s.text.empty() ? "0" : s.text) + counts[i]);
            skeleton += n;
            if (allBytes)
                composed += n;
            break;
        }
        case Segment::Part:
            if (!keep[i])
                break;
            skeleton += s.open;
            CompositePart p;
            p.offset = skeleton.size();
            p.hash = hashes[i];
            p.container = container;
            p.name = s.name;
            c.parts.push_back(std::move(p));
            skeleton += s.close;
            if (allBytes) {
                composed += s.open;
                composed += s.text;
                composed += s.close;
            }
            break;
        }
    }
    c.skeleton = putBytes(skeleton, "skeleton", "durable");

    LogEntity ce;
    ce.kind = "composite";
    ce.tier = "durable";
    const std::string data = c.encode();
    ce.hash = hashBytes(data);
    if (!_store->hasEntity(ce.hash)) {
        ce.refs.push_back(LogRef {c.skeleton, "skeleton", ""});
        std::unordered_set<std::string> seen;
        for (const auto& p : c.parts) {
            if (seen.insert(p.hash).second)
                ce.refs.push_back(LogRef {p.hash, "part", ""});
        }
        compressInto(ce, data);
        _store->putEntity(ce);
    }
    full = allBytes ? hashBytes(composed) : ce.hash;

    // Supersession (23.2) below the entry: the previous composite's
    // skeleton by this one's, its parts by the parts of the same
    // container and property. The op path has already done this for
    // every value it recorded; what is left is the rest.
    if (!previous.empty() && previous != ce.hash) {
        LogEntity pe;
        std::string pdata;
        Composite pc;
        if (_store->getEntity(previous, pe) && pe.kind == "composite"
                && readBytes(previous, pdata) && pc.decode(pdata)) {
            if (pc.skeleton != c.skeleton)
                supersede(pc.skeleton, c.skeleton);
            std::map<std::pair<std::string, std::string>, std::string> byName;
            for (const auto& p : c.parts)
                byName[{p.container, p.name}] = p.hash;
            for (const auto& p : pc.parts) {
                auto it = byName.find({p.container, p.name});
                if (it != byName.end() && it->second != p.hash)
                    supersede(p.hash, it->second);
            }
        }
    }
    return ce.hash;
}

bool TransactionLog::composeEntry(const std::string& data, std::string& out)
{
    Composite c;
    if (!c.decode(data))
        return false;
    std::string skeleton;
    if (!readBytes(c.skeleton, skeleton))
        return false;
    out.clear();
    size_t pos = 0;
    std::string part;
    for (const auto& p : c.parts) {
        if (p.offset < pos || p.offset > skeleton.size())
            return false;
        out.append(skeleton, pos, p.offset - pos);
        pos = p.offset;
        if (!readBytes(p.hash, part))
            return false;
        out += part;
    }
    out.append(skeleton, pos, std::string::npos);
    return true;
}

int TransactionLog::chainBelow(const std::string& hash, int depth)
{
    if (depth > 64)
        return depth;
    int longest = 0;
    for (const auto& below : _store->basedOn(hash))
        longest = std::max(longest, 1 + chainBelow(below, depth + 1));
    return longest;
}

void TransactionLog::supersede(const std::string& older, const std::string& newer)
{
    const long hops = _deltaHops;
    const long ratio = _deltaRatio;
    if (hops <= 0 || older.empty() || newer.empty() || older == newer)
        return;
    LogEntity old;
    if (!_store->getEntity(older, old))
        return;
    if (old.kind == "prop") {
        // A value's files are superseded with it (23.16): its attachments
        // by name, the blob it names when each names one. A shape's
        // fragment is a line; its geometry is what the pair is for.
        LogEntity cur;
        if (_store->getEntity(newer, cur) && cur.kind == "prop") {
            std::map<std::string, std::string> attach;
            std::vector<std::string> oldBlobs, newBlobs;
            for (const auto& r : cur.refs) {
                if (r.role == "attach")
                    attach[r.name] = r.target;
                else if (r.role == "blob")
                    newBlobs.push_back(r.target);
            }
            for (const auto& r : old.refs) {
                if (r.role == "attach") {
                    auto it = attach.find(r.name);
                    if (it != attach.end())
                        supersede(r.target, it->second);
                }
                else if (r.role == "blob") {
                    oldBlobs.push_back(r.target);
                }
            }
            if (oldBlobs.size() == 1 && newBlobs.size() == 1)
                supersede(oldBlobs.front(), newBlobs.front());
        }
    }
    if (old.enc == "delta" || old.size <= 128)
        return;
    if (old.kind != "prop" && old.kind != "xml" && old.kind != "skeleton"
            && old.kind != "composite" && old.kind != "attach" && old.kind != "blob")
        return;
    // The newer one must not decode through the older, or the chain is a
    // loop; and the chain already hanging off the older one, plus this
    // hop, must fit the bound.
    LogEntity walk;
    for (std::string h = newer; !h.empty();) {
        if (h == older || !_store->getEntity(h, walk))
            return;
        h = walk.enc == "delta" ? walk.base : std::string();
    }
    if (chainBelow(older) + 1 > hops)
        return;
    std::string oldBytes, newBytes, patch;
    if (!readBytes(older, oldBytes) || !readBytes(newer, newBytes))
        return;
    if (!deltaEncode(oldBytes, newBytes, patch))
        return;
    // Against what it costs full in the log: its compressed size. A file
    // (23.16) is not compressed where it lies, so that is measured here.
    size_t full = old.data.size();
    if (old.enc == "file") {
        LogEntity compressed;
        compressInto(compressed, oldBytes);
        full = compressed.data.size();
    }
    if (patch.size() * 100 > full * static_cast<size_t>(ratio))
        return;
    _store->reencodeEntity(older, "delta", newer, patch);
    // The patch is the content now; the file can go, unless something
    // else holds it (the live property, an undo copy).
    if (old.enc == "file")
        _blobs.erase(older);
}

/// The full bytes of an entity, decoding a delta chain base first (sec
/// 23.2). `depth` guards a cycle a corrupt store could hold.
bool TransactionLog::readBytes(const std::string& hash, std::string& out, LogEntity* entity,
                               int depth)
{
    LogEntity e;
    if (!_store->getEntity(hash, e))
        return false;
    bool ok = false;
    if (e.enc == "raw") {
        out = std::move(e.data);
        ok = true;
    }
#ifdef FC_HAVE_ZSTD
    else if (e.enc == "zstd") {
        out.assign(e.size, '\0');
        size_t n = ZSTD_decompress(out.data(), out.size(), e.data.data(), e.data.size());
        ok = !ZSTD_isError(n);
        if (ok)
            out.resize(n);
    }
#endif
    else if (e.enc == "file") {
        // Sec 23.16: the document's blob store holds the bytes.
        auto it = _blobs.find(hash);
        FileBlobHandle blob =
            it != _blobs.end() ? it->second : _doc.getFileBlobManager().find(hash);
        ok = blob && blob->read(out);
    }
    else if (e.enc == "delta" && depth < 1024) {
        std::string base;
        ok = readBytes(e.base, base, nullptr, depth + 1)
            && deltaDecode(e.data, base, e.size, out);
    }
    if (ok && entity) {
        e.data.clear();
        *entity = std::move(e);
    }
    return ok;
}

bool TransactionLog::readValue(const std::string& hash, CapturedValue& out)
{
    flush();
    LogEntity e;
    out = CapturedValue();
    if (!readBytes(hash, out.fragment, &e))
        return false;
    if (e.kind == "composite") {
        std::string composed;
        if (!composeEntry(out.fragment, composed))
            return false;
        out.fragment = std::move(composed);
        out.ok = true;
        return true;
    }
    for (auto& r : e.refs) {
        if (r.role != "attach")
            continue;
        CapturedValue::Attachment att;
        att.name = r.name;
        if (!readBytes(r.target, att.bytes))
            return false;
        out.attachments.push_back(std::move(att));
    }
    out.ok = true;
    return true;
}

void TransactionLog::takePending(int64_t key, ValueTask& task)
{
    auto it = _pending.find(key);
    if (it == _pending.end())
        return;
    task.resolveTxn = it->second.txn;
    task.resolveIdx = it->second.idx;
    _pending.erase(it);
}

void TransactionLog::writeValues(std::vector<ValueTask>& tasks, std::vector<LogOp>& ops)
{
    for (auto& task : tasks) {
        std::string hash;
        if (task.copy) {
            // A value that names its blob (decision 6b, `hash=` in the
            // fragment) is complete only while the file exists: the blob
            // is an entity of its own, held as long as the value is
            // (sec 23.16).
            CapturedValue cv = captureValue(_config, *task.copy);
            // A shape names its file without noting it (it notes in
            // beforeSave, which a capture does not run): its contentBlob().
            if (auto referrer = dynamic_cast<const BlobReferrerProperty*>(task.copy.get())) {
                auto blob = referrer->contentBlob();
                if (blob && std::find(cv.blobs.begin(), cv.blobs.end(), blob) == cv.blobs.end())
                    cv.blobs.push_back(blob);
            }
            if (cv.ok)
                hash = putValue(cv, task.tier);
        }
        if (task.key && !hash.empty())
            _hashById[task.key] = hash;
        if (task.opIndex >= 0)
            ops[task.opIndex].vbefore = hash;
        if (task.resolveTxn > 0) {
            _store->resolveAfter(task.resolveTxn, task.resolveIdx, hash);
            // The op's before is now the older of the pair (sec 23.2).
            LogOp resolved;
            if (!hash.empty() && _store->getOp(task.resolveTxn, task.resolveIdx, resolved))
                supersede(resolved.vbefore, hash);
        }
        task.copy.reset();   // the share is released as soon as it is written
    }
}

void TransactionLog::onCommit(const Transaction& txn, const char* kind, const char* origin)
{
    if (txn.isEmpty())
        return;
    try {
        LogTransaction t;
        t.parent = _nextSeq;
        t.seq = ++_nextSeq;
        t.id = txn.getID();
        t.kind = kind;
        t.origin = origin;
        t.name = txn.Name;
        t.time = now();
        t.session = _session;

        std::vector<LogOp> ops;
        std::vector<ValueTask> tasks;
        // Ops whose after ref is pending, by property id, keyed to
        // (t.seq, idx); moved into _pending once the job is queued.
        std::vector<std::pair<int64_t, Pending>> newPending;
        auto pendOp = [&](const Property& prop, const ContainerInfo& c, const char* tier) {
            Pending p;
            p.txn = t.seq;
            p.idx = static_cast<int>(ops.size()) - 1;   // the op just emitted
            p.cid = c.cid;
            p.prop = ops.back().prop;
            p.tier = tier;
            newPending.emplace_back(prop.getID(), p);
        };
        // A copy for the worker: the transaction's own, co-owned from now
        // (decision 4), or one made here for a value the transaction does
        // not hold. `fill` is the op whose before it is, -1 for none.
        auto task = [&](std::shared_ptr<const Property> copy, const char* tier, int fill,
                        int64_t pendingKey) {
            ValueTask v;
            v.copy = std::move(copy);
            v.key = pendingKey;
            v.tier = tier;
            v.opIndex = fill;
            takePending(pendingKey, v);
            if (v.copy)
                _recorded.insert(pendingKey);
            tasks.push_back(std::move(v));
        };
        auto share = [](TransactionObject::PropData& data) {
            if (!data.shared && data.property)
                data.shared.reset(data.property);
            return std::shared_ptr<const Property>(data.shared);
        };

        for (auto& info : txn._Objects.get<0>()) {
            const TransactionalObject* tobj = info.first;
            TransactionObject& rec = *info.second;
            ContainerInfo c = describe(_doc, tobj, rec._NameInDocument);
            auto emit = [&](const char* op, const std::string& prop, const std::string& ptype) {
                LogOp o;
                o.op = op;
                o.ckind = c.ckind;
                o.cid = c.cid;
                o.prop = prop;
                o.ptype = ptype;
                ops.push_back(std::move(o));
                return &ops.back();
            };
            auto last = [&]() { return static_cast<int>(ops.size()) - 1; };

            if (rec.status == TransactionObject::Del) {
                // Created in this transaction: the op, the dynamic property
                // metadata, and a pending set per persisted property.
                auto o = emit("create", "", "");
                o->cname = c.cname;
                o->ctype = c.ctype;
                std::map<std::string, Property*> props;
                c.container->getPropertyMap(props);
                for (auto& kv : props) {
                    short ptype = c.container->getPropertyType(kv.second);
                    if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                        continue;
                    if (!kv.second->getName())
                        continue;
                    std::string typeName = kv.second->getTypeId().getName();
                    auto dyn = c.container->getDynamicPropertyData(kv.second);
                    if (!dyn.name.empty()) {
                        auto a = emit("addprop", kv.first, typeName);
                        a->meta = dynamicMeta(dyn);
                    }
                    emit("set", kv.first, typeName);
                    pendOp(*kv.second, c, "durable");
                }
                continue;
            }

            if (rec.status == TransactionObject::New) {
                // Removed in this transaction. The object is detached and
                // alive (the transaction holds it), but not for as long as
                // the worker may need it: each value is copied here, the
                // way the undo system copies a changed one, and the copy is
                // what is serialised. Each resolves whatever was pending.
                std::map<std::string, Property*> props;
                c.container->getPropertyMap(props);
                for (auto& kv : props) {
                    short ptype = c.container->getPropertyType(kv.second);
                    if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                        continue;
                    if (!kv.second->getName())
                        continue;
                    emit("set", kv.first, kv.second->getTypeId().getName());
                    std::shared_ptr<const Property> copy(kv.second->Copy());
                    task(std::move(copy), "durable", last(), kv.second->getID());
                }
                auto o = emit("remove", "", "");
                o->cname = c.cname;
                o->ctype = c.ctype;
                continue;
            }

            for (auto& kv : rec._PropChangeMap) {
                auto& data = kv.second;
                auto prop = const_cast<Property*>(data.propertyOrig);
                std::string typeName = data.propertyType.getName();
                if (!data.property) {
                    // Dynamic property added: metadata, then its value pending.
                    auto a = emit("addprop", data.name, typeName);
                    a->meta = dynamicMeta(data);
                    const char* name = c.container->getPropertyName(prop);
                    if (name && data.name == name) {
                        emit("set", data.name, typeName);
                        pendOp(*prop, c, "durable");
                    }
                    continue;
                }
                const char* name = c.container->getPropertyName(prop);
                if (!name || (!data.name.empty() && data.name != name)
                        || data.propertyType != prop->getTypeId()) {
                    // The original is gone: a dynamic property removed.
                    if (data.name.empty())
                        continue;
                    auto o = emit("delprop", data.name, typeName);
                    o->meta = dynamicMeta(data);
                    task(share(data), "durable", last(), kv.first);
                    continue;
                }
                short ptype = c.container->getPropertyType(prop);
                if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                    continue;

                bool derived = data.derived;
                bool waiting = _pending.count(kv.first) != 0;
                bool same = data.property->isSame(*prop);
                if (!waiting && same)
                    continue;   // a write that changed nothing, sec 9.1
                if (waiting && same) {
                    // Only the earlier op needed this copy (decision 6a).
                    task(share(data), tierFor(derived), -1, kv.first);
                    continue;
                }
                auto o = emit("set", name, typeName);
                o->derived = derived;
                if (recordsValue(derived) || waiting)
                    task(share(data), tierFor(derived), recordsValue(derived) ? last() : -1,
                         kv.first);
                if (recordsValue(derived))
                    pendOp(*prop, c, tierFor(derived));
                else
                    _recorded.erase(kv.first);   // changed, and the value not kept
            }
        }

        if (ops.empty()) {
            // Nothing to record; copies that only resolve earlier ops are
            // still written.
            --_nextSeq;
            if (!tasks.empty()) {
                post([this, tasks]() mutable {
                    std::vector<LogOp> none;
                    writeValues(tasks, none);
                });
            }
            return;
        }
        for (auto& p : newPending)
            _pending[p.first] = p.second;
        post([this, t, ops, tasks]() mutable {
            writeValues(tasks, ops);
            _store->append(t, ops);
        });
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    catch (std::exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
}

void TransactionLog::resolvePending()
{
    // Copy the live values behind the pending refs -- the copy the undo
    // system would take, here for the log -- and let the worker write
    // them. Looked up by container id and name rather than through the
    // property pointer, which may be gone without a remove op if undo
    // was off for a while.
    std::vector<ValueTask> tasks;
    std::vector<std::pair<int64_t, Pending>> todo(_pending.begin(), _pending.end());
    for (auto& kv : todo) {
        const PropertyContainer* container = nullptr;
        if (kv.second.cid < 0) {
            _pending.erase(kv.first);
            continue;
        }
        if (kv.second.cid == 0)
            container = &_doc;
        else
            container = _doc.getObjectByID(kv.second.cid);
        if (!container) {
            _pending.erase(kv.first);
            continue;
        }
        Property* prop = container->getPropertyByName(kv.second.prop.c_str());
        if (!prop || prop->getID() != kv.first) {
            _pending.erase(kv.first);
            continue;
        }
        ValueTask v;
        v.copy.reset(prop->Copy());
        v.key = kv.first;
        v.tier = kv.second.tier;
        takePending(kv.first, v);
        _recorded.insert(kv.first);
        tasks.push_back(std::move(v));
    }
    if (tasks.empty())
        return;
    post([this, tasks]() mutable {
        std::vector<LogOp> none;
        writeValues(tasks, none);
    });
}
