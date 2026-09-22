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
    bool hasValue(const std::string& hash) override { return inner().hasValue(hash); }
    void putValue(const LogValue& value) override { inner().putValue(value); }
    bool getValue(const std::string& hash, LogValue& value) override
    { return inner().getValue(hash, value); }
    std::vector<LogTransaction> transactions(int64_t from, int limit) override
    { return inner().transactions(from, limit); }
    std::vector<LogOp> ops(int64_t txn) override { return inner().ops(txn); }
    int64_t lastSeq() override { return inner().lastSeq(); }
    void truncate(int64_t before) override { inner().truncate(before); }
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
    void evictVersion(int64_t num) override { inner().evictVersion(num); }
    void copyTo(const std::string& path) override { inner().copyTo(path); }
    void dropTier(const std::string& tier) override { inner().dropTier(tier); }
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
    for (const auto& v : copy->versions()) {
        for (const auto& e : copy->manifest(v.num)) {
            if (e.source != "blob")
                continue;
            std::string ext;
            auto dot = e.entry.rfind('.');
            if (dot != std::string::npos)
                ext = e.entry.substr(dot);
            out.blobs.emplace_back(e.hash, ext);
        }
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

int64_t TransactionLog::onSave(const std::string& path, const Entries& entries,
                               const std::vector<std::pair<std::string, std::string>>& blobs,
                               int schema)
{
    return snapshot("save", path, entries, blobs, schema);
}

void TransactionLog::onCheckout(int64_t num)
{
    // The head state is the version's; pending after refs of the state
    // that was left describe values that are gone, so they are dropped
    // rather than resolved against the restored document.
    _pending.clear();
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

int64_t TransactionLog::onSnapshot(const Entries& entries,
                                   const std::vector<std::pair<std::string, std::string>>& blobs,
                                   int schema)
{
    return snapshot("snapshot", _doc.FileName.getValue(), entries, blobs, schema);
}

int64_t TransactionLog::onRestore(const std::string& path, const Entries& entries,
                                  const std::vector<std::pair<std::string, std::string>>& blobs,
                                  int schema)
{
    if (!_adopted && (_nextSeq != 0 || _nextVersion != 0)) {
        // A store with history at restore is either an adopted embedded
        // copy, whose version the file now becomes, or a mistake.
        FC_WARN("transaction log of " << _doc.getName() << " is not empty at restore");
        return 0;
    }
    _adopted = false;
    return snapshot("restore", path, entries, blobs, schema);
}

int64_t TransactionLog::snapshot(const char* kind, const std::string& path,
                                 const Entries& entries,
                                 const std::vector<std::pair<std::string, std::string>>& blobs,
                                 int schema)
{
    if (entries.empty() || entries.front().first != "Document.xml") {
        FC_ERR("transaction log: a snapshot needs Document.xml first");
        return 0;
    }
    try {
        resolvePending();

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
        post([this, v, t, entries, blobs, schema, escaped, nblobs, keep]() mutable {
            // Each XML entry is a value like any other, durable: the
            // version is the one place a whole file is kept (sec 16.1).
            // With no attachments a ref is the SHA-1 of the bytes, which
            // is also the hash a file on disk is matched by.
            std::vector<LogManifestEntry> manifest;
            std::string docHash;
            for (const auto& e : entries) {
                CapturedValue xml;
                xml.fragment = e.second;
                xml.ok = true;
                const std::string hash = putValue(xml, "durable");
                if (docHash.empty())
                    docHash = hash;   // Document.xml, first by contract
                manifest.push_back({e.first, hash, "value"});
            }
            v.docxml_hash = docHash;
            for (const auto& b : blobs)
                manifest.push_back({b.first, b.second, "blob"});
            _store->addVersion(v, manifest);
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
}

std::string TransactionLog::putValue(const CapturedValue& value, const std::string& tier)
{
    LogValue v;
    v.tier = tier;
    // Attachments first, each its own value: a fragment that changes while
    // its geometry does not (a move at schema 5) shares the geometry.
    for (auto& a : value.attachments) {
        LogValue av;
        av.tier = tier;
        av.hash = hashBytes(a.bytes);
        v.attachments.emplace_back(a.name, av.hash);
        if (_store->hasValue(av.hash))
            continue;
        av.size = a.bytes.size();
        av.data = a.bytes;
#ifdef FC_HAVE_ZSTD
        if (a.bytes.size() > 128) {
            std::string out(ZSTD_compressBound(a.bytes.size()), '\0');
            size_t n = ZSTD_compress(out.data(), out.size(), a.bytes.data(), a.bytes.size(), 3);
            if (!ZSTD_isError(n)) {
                out.resize(n);
                av.data = std::move(out);
                av.enc = "zstd";
            }
        }
#endif
        _store->putValue(av);
    }
    std::string keyed = value.fragment;
    for (auto& a : v.attachments) {
        keyed += '\0';
        keyed += a.first;
        keyed += '\0';
        keyed += a.second;
    }
    v.hash = hashBytes(keyed);
    if (_store->hasValue(v.hash))
        return v.hash;
    v.size = value.fragment.size();
    v.data = value.fragment;
#ifdef FC_HAVE_ZSTD
    if (value.fragment.size() > 128) {
        std::string out(ZSTD_compressBound(value.fragment.size()), '\0');
        size_t n = ZSTD_compress(out.data(), out.size(), value.fragment.data(),
                                 value.fragment.size(), 3);
        if (!ZSTD_isError(n)) {
            out.resize(n);
            v.data = std::move(out);
            v.enc = "zstd";
        }
    }
#endif
    _store->putValue(v);
    return v.hash;
}

namespace {
bool inflate(LogValue& v, std::string& out)
{
    if (v.enc == "raw") {
        out = std::move(v.data);
        return true;
    }
#ifdef FC_HAVE_ZSTD
    if (v.enc == "zstd") {
        out.assign(v.size, '\0');
        size_t n = ZSTD_decompress(out.data(), out.size(), v.data.data(), v.data.size());
        if (ZSTD_isError(n))
            return false;
        out.resize(n);
        return true;
    }
#endif
    return false;
}
} // namespace

bool TransactionLog::readValue(const std::string& hash, CapturedValue& out)
{
    flush();
    LogValue v;
    if (!_store->getValue(hash, v))
        return false;
    out = CapturedValue();
    if (!inflate(v, out.fragment))
        return false;
    for (auto& a : v.attachments) {
        LogValue av;
        if (!_store->getValue(a.second, av))
            return false;
        CapturedValue::Attachment att;
        att.name = a.first;
        if (!inflate(av, att.bytes))
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
            CapturedValue cv = captureValue(_config, *task.copy);
            if (cv.ok)
                hash = putValue(cv, task.tier);
            // A value that named its blob (decision 6b, `hash=` in the
            // fragment) is complete only while the file exists: hold it,
            // in the document's one store (sec 16.2).
            if (auto referrer = dynamic_cast<const BlobReferrerProperty*>(task.copy.get())) {
                if (auto blob = referrer->contentBlob())
                    _blobs.emplace(blob->hash(), blob);
            }
        }
        if (task.opIndex >= 0)
            ops[task.opIndex].vbefore = hash;
        if (task.resolveTxn > 0)
            _store->resolveAfter(task.resolveTxn, task.resolveIdx, hash);
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
            v.tier = tier;
            v.opIndex = fill;
            takePending(pendingKey, v);
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
        v.tier = kv.second.tier;
        takePending(kv.first, v);
        tasks.push_back(std::move(v));
    }
    if (tasks.empty())
        return;
    post([this, tasks]() mutable {
        std::vector<LogOp> none;
        writeValues(tasks, none);
    });
}
