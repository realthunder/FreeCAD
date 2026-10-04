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
# include <cstdlib>
# include <map>
# include <set>
# include <sstream>
#endif

#ifdef FC_HAVE_ZSTD
# include <zstd.h>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/Uuid.h>

#include "TransactionLog.h"
#include "Actor.h"
#include "Application.h"
#include "Document.h"
#include "DocumentObject.h"
#include "DocumentParams.h"
#include "FileBlobManager.h"
#include "FileHistory.h"
#include "Property.h"
#include "PropertyLinks.h"
#include "PropertyPythonObject.h"
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

/// A dynamic property's metadata: group, doc, then "attr[ ro][ hidden]"
/// and, with `prop`, " status=N" -- its status bits as a save writes them
/// (sec 27.67): an object a cold undo or a switch recreates gets them back,
/// CopyOnChange among them, which a copy-on-change link reads off its
/// target. A static property's status is not carried: its op has no meta.
std::string dynamicMeta(const DynamicProperty::PropData& d, const Property* prop = nullptr)
{
    std::string m = d.group;
    m += '\n';
    m += d.getDoc() ? d.getDoc() : "";
    m += '\n';
    m += std::to_string(d.attr);
    m += d.readonly ? " ro" : "";
    m += d.hidden ? " hidden" : "";
    if (prop) {
        m += " status=";
        m += std::to_string(prop->getStatus());
    }
    return m;
}

/// Whether a copy and the live property hold the same value, as the log
/// sees it. isSame() compares links through getLinks(), which leaves out a
/// target that has left the document -- "links to the removed B" and "links
/// to nothing" compare equal, and the change that cleared the link would be
/// dropped. A link is compared by what the log would write for it, under
/// the removed objects' names (CaptureNames, sec 24.3).
bool sameForLog(const CaptureConfig& config, const Property& copy, const Property& live)
{
    if (dynamic_cast<const PropertyLinkBase*>(&copy)) {
        CapturedValue a = captureValue(config, copy);
        CapturedValue b = captureValue(config, live);
        return a.ok && b.ok && a.fragment == b.fragment;
    }
    return copy.isSame(live);
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
        // A view provider (sec 24.9): named by its object's id, which is
        // how a cold undo and resolvePending() find it again. One with no
        // owner keeps -1 and resolves only through the next copy.
        c.ckind = "view";
        auto owner = tobj->getTransactionOwner();
        c.cid = owner ? owner->getID() : -1;
        c.cname = owner && owner->getNameInDocument() ? owner->getNameInDocument()
                                                      : c.container->getFullName();
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
class TransactionLogCore::FlushingStore : public TransactionStore
{
public:
    explicit FlushingStore(TransactionLogCore& core) : _core(core) {}

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
    std::vector<LogTransaction> chain(int64_t head, int64_t from) override
    { return inner().chain(head, from); }
    std::vector<LogTransaction> history(int64_t head) override { return inner().history(head); }
    bool lastOpOn(const std::string& ckind, long cid, const std::string& prop, int64_t after,
                  int64_t head, LogOp& op) override
    { return inner().lastOpOn(ckind, cid, prop, after, head, op); }
    int64_t lastSeq() override { return inner().lastSeq(); }
    long maxObjectId() override { return inner().maxObjectId(); }
    std::vector<std::pair<long, std::string>> objectNames() override
    { return inner().objectNames(); }
    void addObjectNames(const std::vector<std::pair<long, std::string>>& names) override
    { inner().addObjectNames(names); }
    std::vector<std::pair<long, long>> lastGeoIds() override { return inner().lastGeoIds(); }
    void addLastGeoIds(const std::vector<std::pair<long, long>>& ids) override
    { inner().addLastGeoIds(ids); }
    std::vector<long> objectIdsInOps() override { return inner().objectIdsInOps(); }
    void removeObjectState(const std::vector<long>& ids) override
    { inner().removeObjectState(ids); }
    void addStrings(const std::vector<LogString>& strings) override
    { inner().addStrings(strings); }
    std::vector<LogString> strings(const std::vector<long>& ids) override
    { return inner().strings(ids); }
    std::vector<long> stringIds() override { return inner().stringIds(); }
    void removeStrings(const std::vector<long>& ids) override { inner().removeStrings(ids); }
    void clearStrings() override { inner().clearStrings(); }
    void addStringRefs(const std::string& owner,
                       const std::vector<std::pair<long, long>>& ranges) override
    { inner().addStringRefs(owner, ranges); }
    std::vector<std::pair<long, long>> stringRefs() override { return inner().stringRefs(); }
    void truncate(int64_t before) override
    {
        inner().truncate(before);
        ++_core._rewrites;
        _core.releaseBlobs();
    }
    void removeTransactions(const std::vector<int64_t>& seqs) override
    {
        inner().removeTransactions(seqs);
        ++_core._rewrites;
        _core.releaseBlobs();
    }
    void replaceTransactions(const LogTransaction& txn, std::vector<LogOp>& ops,
                             const std::vector<int64_t>& seqs) override
    {
        inner().replaceTransactions(txn, ops, seqs);
        ++_core._rewrites;
        _core.releaseBlobs();
    }
    int64_t environment(const std::string& json) override { return inner().environment(json); }
    std::string environmentJson(int64_t id) override { return inner().environmentJson(id); }
    int64_t user(const std::string& kind, const std::string& name) override
    { return inner().user(kind, name); }
    std::vector<LogUser> users() override { return inner().users(); }
    int64_t openSession(int64_t env, int64_t user, const std::string& host,
                        const std::string& access, const std::string& uuid,
                        double opened) override
    { return inner().openSession(env, user, host, access, uuid, opened); }
    bool rowId(int64_t seq, LogRowId& id) override { return inner().rowId(seq, id); }
    int64_t findRow(const LogRowId& id) override { return inner().findRow(id); }
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
        _core.releaseBlobs();
    }
    void evictVersions(const std::vector<int64_t>& nums) override
    {
        inner().evictVersions(nums);
        _core.releaseBlobs();
    }
    void copyTo(const std::string& path) override { inner().copyTo(path); }
    void vacuum() override { inner().vacuum(); }
    void dropTier(const std::string& tier, const std::vector<int64_t>& evict) override
    {
        inner().dropTier(tier, evict);
        _core.releaseBlobs();
    }
    bool nameVersion(int64_t num, const std::string& name) override
    { return inner().nameVersion(num, name); }
    std::vector<LogBranch> branches() override { return inner().branches(); }
    bool getBranch(int64_t id, LogBranch& branch) override
    { return inner().getBranch(id, branch); }
    bool findBranch(const std::string& name, LogBranch& branch) override
    { return inner().findBranch(name, branch); }
    int64_t addBranch(LogBranch& branch) override { return inner().addBranch(branch); }
    bool renameBranch(int64_t id, const std::string& name) override
    { return inner().renameBranch(id, name); }
    bool updateBranch(const LogBranch& branch) override { return inner().updateBranch(branch); }
    bool removeBranch(int64_t id) override { return inner().removeBranch(id); }
    bool forwardBranch(int64_t id, int64_t head, const std::vector<int64_t>& seqs) override
    { return inner().forwardBranch(id, head, seqs); }
    void anchorVersions(const std::vector<int64_t>& seqs, int64_t to) override
    { inner().anchorVersions(seqs, to); }
    std::string getMeta(const std::string& key) override { return inner().getMeta(key); }
    void setMeta(const std::string& key, const std::string& value) override
    { inner().setMeta(key, value); }

private:
    TransactionStore& inner()
    {
        _core.flush();
        return *_core._store;
    }
    TransactionLogCore& _core;
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

TransactionLogCore::TransactionLogCore(FileHistory& history)
    : _history(history)
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
    // The desktop user (sec 30.4 P3): `host` until the privacy preference
    // names them.
    _localName = "host";
    if (DocumentParams::getTransactionLogIdentity()) {
        auto u = config.find("UserName");
        if (u != config.end() && !u->second.empty())
            _localName = u->second;
        auto h = config.find("HostName");
        if (h != config.end())
            _host = h->second;
    }
    openProcessSession();
    _worker = std::thread([this]() { run(); });
    liveLogs(this, true);
    FC_LOG("transaction log " << _path << " session " << _session);
}

TransactionLogCore::~TransactionLogCore()
{
    liveLogs(this, false);
    try {
        stopWorker();
        _blobs.clear();
        closeSessions();
    }
    catch (...) {
    }
}

void TransactionLogCore::openProcessSession()
{
    // Main thread, with the store its own: at open, before the worker has
    // anything, or with the queue drained.
    _environment = _store->environment(_envJson);
    _localUser = _store->user(Actor::kindName(Actor::Local), _localName);
    _session = _store->openSession(_environment, _localUser, _host, std::string(),
                                   Base::Uuid::createUuid(), now());
    _actorSessions.clear();
    _ordinals.clear();
}

void TransactionLogCore::closeSessions()
{
    if (!_store)
        return;
    const double closed = now();
    for (const auto& s : _actorSessions)
        _store->closeSession(s.second, closed);
    _actorSessions.clear();
    if (_session)
        _store->closeSession(_session, closed);
    _session = 0;
}

namespace
{
/// What tells one login from the next: the connection when there is one,
/// else the user, who then has one session for the life of the process.
std::string actorKey(const Actor& actor)
{
    if (actor.login)
        return "#" + std::to_string(actor.login);
    return std::string(Actor::kindName(actor.kind)) + ":" + actor.name;
}
}  // namespace

int64_t TransactionLogCore::sessionOf(const Actor* actor)
{
    if (!actor || actor->kind == Actor::Local)
        return _session;
    const std::string key = actorKey(*actor);
    auto it = _actorSessions.find(key);
    if (it != _actorSessions.end())
        return it->second;
    // The first this log sees of the login: its user, made if this is the
    // first of them too, and its session. Once per login, so the wait for
    // the worker is not on any path that repeats.
    flush();
    const int64_t user = _store->user(Actor::kindName(actor->kind), actor->name);
    const int64_t session = _store->openSession(_environment, user, std::string(),
                                                actor->access, Base::Uuid::createUuid(),
                                                now());
    _actorSessions[key] = session;
    return session;
}

int64_t TransactionLogCore::forkBase(TransactionStore& other, int64_t otherHead, int64_t& ours)
{
    ours = 0;
    flush();
    // Newest first along the other's chain, a stretch of it at a time: a
    // fork parts near its head, and the walk ends at the first row both
    // hold.
    const int64_t span = 256;
    for (int64_t cur = otherHead; cur > 0;) {
        const auto rows = other.chain(cur, std::max<int64_t>(1, cur - span + 1));
        if (rows.empty())
            break;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
            LogRowId id;
            if (!other.rowId(it->seq, id))
                continue;
            if (const int64_t seq = _store->findRow(id)) {
                ours = seq;
                return it->seq;
            }
        }
        cur = rows.front().parent;
    }
    return 0;
}

bool TransactionLogCore::closeLogin(const Actor& actor)
{
    auto it = _actorSessions.find(actorKey(actor));
    if (it == _actorSessions.end())
        return false;
    flush();
    _store->closeSession(it->second, now());
    _actorSessions.erase(it);
    return true;
}

void TransactionLogCore::openStore()
{
    std::string dir = _history.directory() + "/history";
    Base::FileInfo(dir).createDirectories();
    _path = dir + "/log.db";
    _store = TransactionStore::openSQLite(_path);
    _nextSeq = _store->lastSeq();
    _nextVersion = _store->lastVersion();
    // An adopted embedded copy may have had every version dropped by its
    // retention; the counter it carries keeps the numbering monotonic.
    const std::string counter = _store->getMeta("version_counter");
    if (!counter.empty())
        _nextVersion = std::max<int64_t>(_nextVersion, std::stoll(counter) - 1);
    // The file's object ids go on past every id its history knows (sec
    // 27.40 item 1): the one an embedded copy carries, any op's, and any
    // branch's -- the ops of a removed object may be trimmed away.
    const std::string lastId = _store->getMeta("last_object_id");
    if (!lastId.empty())
        _history.noteObjectId(std::stol(lastId));
    _history.noteObjectId(_store->maxObjectId());
    for (const auto& b : _store->branches())
        _history.noteObjectId(std::max(b.idBase, b.lastId));
    // And its names (item 3).
    for (const auto& n : _store->objectNames())
        _history.noteObjectName(n.second, n.first);
    // And the last geometry id of each object (item 4).
    for (const auto& g : _store->lastGeoIds())
        _history.noteGeoId(g.first, g.second);
    // What compaction would drop, as estimated so far (sec 27.48).
    const std::string estimate = _store->getMeta("compact_estimate");
    if (!estimate.empty())
        _history.setCompactEstimate(std::stoul(estimate));
    // The strings the store holds (sec 27.50 item 2); memory takes in the
    // ones it lacks before anything is read from the log (loadStrings).
    _storedStrings.clear();
    _storedMax = 0;
    for (long id : _store->stringIds()) {
        _storedStrings.insert(id);
        _storedMax = std::max(_storedMax, id);
    }
    _stringsFor = nullptr;
    loadStrings();
}

void TransactionLogCore::syncStrings(bool all)
{
    const StringHasherRef& hasher = _history.hasher();
    if (!hasher || !_store)
        return;
    if (_stringsFor != hasher.get()) {
        loadStrings();
        all = true;
    }
    const auto rows = hasher->rows(all ? 0 : _storedMax,
                                   [this](long id) { return _storedStrings.count(id) == 0; });
    if (rows.empty())
        return;
    std::vector<LogString> out;
    out.reserve(rows.size());
    for (const auto& row : rows) {
        LogString str;
        str.id = row.id;
        str.flags = row.flags;
        for (const auto& sid : row.sids) {
            if (!str.sids.empty())
                str.sids += ' ';
            str.sids += std::to_string(sid.first);
            if (sid.second)
                str.sids += ':' + std::to_string(sid.second);
        }
        str.data.assign(row.data.constData(), static_cast<std::size_t>(row.data.size()));
        str.postfix.assign(row.postfix.constData(), static_cast<std::size_t>(row.postfix.size()));
        _storedStrings.insert(row.id);
        _storedMax = std::max(_storedMax, row.id);
        out.push_back(std::move(str));
    }
    post([this, out = std::move(out)]() { _store->addStrings(out); });
}

void TransactionLogCore::loadStrings()
{
    const StringHasherRef& hasher = _history.hasher();
    if (!hasher || !_store || _stringsFor == hasher.get())
        return;
    _stringsFor = hasher.get();
    std::vector<long> missing;
    for (long id : _storedStrings) {
        if (!hasher->hasID(id))
            missing.push_back(id);
    }
    if (missing.empty())
        return;
    std::vector<StringHasher::Row> rows;
    for (const auto& str : store().strings(missing)) {
        StringHasher::Row row;
        row.id = str.id;
        row.flags = str.flags;
        std::istringstream in(str.sids);
        std::string token;
        while (in >> token) {
            const auto colon = token.find(':');
            row.sids.emplace_back(std::stol(token.substr(0, colon)),
                                  colon == std::string::npos ? 0
                                                             : std::stoi(token.substr(colon + 1)));
        }
        row.data = QByteArray(str.data.data(), static_cast<int>(str.data.size()));
        row.postfix = QByteArray(str.postfix.data(), static_cast<int>(str.postfix.size()));
        rows.push_back(std::move(row));
    }
    std::size_t conflicts = 0;
    const std::size_t taken = hasher->insertRows(rows, &conflicts);
    FC_LOG("transaction log: " << taken << " string(s) of the store taken into the file's hasher");
    if (conflicts)
        FC_WARN("transaction log: " << conflicts
                << " string(s) of the store disagree with the file's hasher or miss a part;"
                   " left out");
}

std::vector<std::pair<long, long>> TransactionLogCore::retainedStrings()
{
    return store().stringRefs();
}

void TransactionLogCore::dropStrings(const std::vector<long>& ids)
{
    if (ids.empty() || !_store)
        return;
    std::vector<long> stored;
    for (long id : ids) {
        if (_storedStrings.erase(id))
            stored.push_back(id);
    }
    if (!stored.empty())
        post([this, stored]() { _store->removeStrings(stored); });
}

std::vector<std::pair<long, long>> TransactionLogCore::idRanges(const std::vector<long>& ids)
{
    std::vector<std::pair<long, long>> out;
    for (long id : ids) {
        if (!out.empty() && id <= out.back().second + 1)
            out.back().second = std::max(out.back().second, id);
        else
            out.emplace_back(id, id);
    }
    return out;
}

void TransactionLogCore::liveLogs(TransactionLogCore* core, bool add)
{
    // A process may end with documents open -- a script, a test, a crash
    // handler's exit -- and then no ~TransactionLogCore runs, while a
    // worker exporting a shape meets OCCT's statics being destroyed (sec
    // 25.6). atexit handlers run before the destructors of statics
    // constructed earlier, which the libraries loaded before the first
    // log's are.
    static std::mutex mutex;
    static std::set<TransactionLogCore*> logs;
    static bool registered = false;
    std::vector<TransactionLogCore*> all;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (core) {
            if (add)
                logs.insert(core);
            else
                logs.erase(core);
            if (add && !registered) {
                registered = true;
                std::atexit([]() { liveLogs(nullptr, false); });
            }
            return;
        }
        all.assign(logs.begin(), logs.end());
    }
    for (auto l : all)
        l->stopWorker();
}

void TransactionLogCore::stopWorker()
{
    try {
        flush();
    }
    catch (...) {
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
    }
    _wake.notify_all();
    if (_worker.joinable())
        _worker.join();
}

void TransactionLogCore::run()
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

void TransactionLogCore::retire(std::shared_ptr<const Property>&& copy)
{
    std::lock_guard<std::mutex> lock(_retiredMutex);
    _retired.push_back(std::move(copy));
}

void TransactionLogCore::releaseRetired()
{
    if (std::this_thread::get_id() != _mainThread)
        return;
    std::vector<std::shared_ptr<const Property>> gone;
    {
        std::lock_guard<std::mutex> lock(_retiredMutex);
        gone.swap(_retired);
    }
}

void TransactionLogCore::post(std::function<void()> job)
{
    releaseRetired();   // every post is the main thread's
    _deltaHops = DocumentParams::getTransactionLogDeltaHops();
    _deltaRatio = DocumentParams::getTransactionLogDeltaRatio();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_stop)
            return;   // the process is ending (liveLogs): nothing runs it
        _queue.push_back(std::move(job));
    }
    _wake.notify_one();
}

void TransactionLogCore::flush()
{
    {
        // Never wait for the worker holding the GIL: a value's Save may
        // take it (sec 25.6), and ~Document holds it around the log's end.
        std::unique_ptr<Base::PyGILStateRelease> unlocked;
        if (Py_IsInitialized() && PyGILState_Check())
            unlocked = std::make_unique<Base::PyGILStateRelease>();
        std::unique_lock<std::mutex> lock(_mutex);
        _done.wait(lock, [this]() { return (_queue.empty() || _stop) && !_running; });
    }
    releaseRetired();
}

bool TransactionLogCore::adoptEmbedded(const std::string& path)
{
    flush();
    if (_nextSeq != 0 || _nextVersion != 0) {
        FC_WARN("transaction log of " << _history.path() << " has history; not adopting the embedded copy");
        return false;
    }
    closeSessions();
    _store.reset();
    Base::FileInfo(_path).deleteFile();
    Base::FileInfo(_path + "-wal").deleteFile();
    Base::FileInfo(_path + "-shm").deleteFile();
    if (!Base::FileInfo(path).copyTo(_path.c_str())) {
        FC_ERR("cannot adopt the embedded history of " << _history.path());
        openStore();
        return false;
    }
    openStore();
    // The copy's blobs came with the file, restored into the file's blob
    // store for the History property; the log holds them from here.
    _blobs.clear();
    _sourced.clear();
    auto& manager = _history.blobs();
    for (const auto& hash : _store->entitiesStoredAs("file")) {
        if (auto blob = manager.find(hash))
            _blobs[hash] = blob;
        else
            FC_WARN("embedded history of " << _history.path() << ": blob " << hash
                    << " is not in the file's store");
    }
    openProcessSession();
    FC_LOG("transaction log of " << _history.path() << " continues from the embedded copy: seq "
           << _nextSeq << ", next version " << (_nextVersion + 1));
    return true;
}

int64_t TransactionLogCore::closeAdopted()
{
    // Sec 16.6, 26.2 item 7: the copy's history kept, every branch of it
    // closed and its `main` renamed after the save it came from; a new
    // `main` with no rows yet, whose first version -- the file as found,
    // the on-open snapshot -- numbers on and roots a chain of its own: the
    // gap has no ancestry.
    const double closed = now();
    std::string date = _store->getMeta("save_date");
    if (date.empty())
        date = std::to_string(static_cast<int64_t>(closed));
    // Which of them was the file's (sec 30.19 G6): what the file as found
    // was made from, elsewhere, and what an import of it starts with.
    {
        int64_t head = 0;
        _store->setMeta("closed_branch", std::to_string(metaBranch(head)));
    }
    for (auto b : _store->branches()) {
        if (b.name == "main") {
            std::string name = "main@" + date;
            LogBranch taken;
            for (int i = 2; _store->findBranch(name, taken); ++i)
                name = "main@" + date + "#" + std::to_string(i);
            b.name = name;
        }
        if (b.closed == 0)
            b.closed = closed;
        _store->updateBranch(b);
    }
    // The copy's counter is the number the save that wrote the file took in
    // the store it came from; the file edited since is not that version, so
    // it numbers past it.
    const std::string counter = _store->getMeta("version_counter");
    if (!counter.empty())
        _nextVersion = std::max<int64_t>(_nextVersion, std::stoll(counter));
    LogBranch fresh;
    fresh.name = "main";
    fresh.fromVersion = _store->lastVersion();
    fresh.created = closed;
    _store->addBranch(fresh);
    _store->setMeta("branch", std::to_string(fresh.id));
    return fresh.id;
}

TransactionLogCore& TransactionLogCore::of(FileHistory& history)
{
    auto& core = history.logCore();
    if (!core)
        core = std::make_shared<TransactionLogCore>(history);
    return *core;
}

void TransactionLogCore::postVersion(LogVersion v, LogTransaction t,
                                     const TransactionLog::Captures& entries,
                                     const TransactionLog::Blobs& blobs, int schema,
                                     const std::string& path,
                                     std::vector<std::pair<long, long>> strings, bool marked)
{
        std::string escaped;
        for (char c : path) {
            if (c == '"' || c == '\\')
                escaped += '\\';
            escaped += c;
        }
        const size_t nblobs = blobs.size();
        const long keep = DocumentParams::getTransactionLogKeepVersions();
        post([this, v, t, entries, blobs = TransactionLog::Blobs(blobs), schema, escaped, nblobs,
              keep, strings = std::move(strings), marked]() mutable {
            // Each XML entry is a composite (sec 23.3): its skeleton plus
            // the parts, or the bytes themselves when it was read rather
            // than written; each blob an entity the log holds (23.16). The
            // previous version's entries are superseded by this one's (sec
            // 23.2): matched by name, re-encoded toward the newer, parts and
            // skeletons included. A blob's name is its referrer's
            // (`Box.Shape.brp`), so one property's files pair up too.
            std::map<std::string, std::string> previous;
            LogVersion pv;
            if (const int64_t prev = _store->lastVersion()) {
                for (const auto& e : _store->manifest(prev))
                    previous[e.entry] = e.hash;
                _store->getVersion(prev, pv);
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
                manifest.push_back({e.first, hash});
            }
            v.docxml_hash = docHash;
            // A file as read has no marks of this session: it uses what the
            // save that wrote it marked, which its table element says
            // (`used`), and else every string the hasher held -- sec 27.50
            // item 4.
            if (!marked && !entries.empty()) {
                std::vector<std::pair<long, long>> used;
                if (StringHasher::parseUsed(entries.front().second.bytes(), used))
                    strings = std::move(used);
            }
            std::vector<FileBlobHandle> named;
            for (const auto& b : blobs)
                named.push_back(b.second);
            // On disk before the manifest naming them commits (sec 15.8).
            _history.blobs().makeDurable(named);
            for (const auto& b : blobs) {
                if (b.second)
                    manifest.push_back({b.first, putBlob(b.second)});
            }
            blobs.clear();   // the log holds what it keeps; the job lets go
            _store->addVersion(v, manifest);
            // The string ids it uses, beside its entry list and gone with it
            // (sec 27.50 item 4).
            _store->addStringRefs(v.manifest, strings);
            for (const auto& e : manifest) {
                auto it = previous.find(e.entry);
                if (it != previous.end() && it->second != e.hash)
                    supersede(it->second, e.hash);
            }
            // The entry list too (sec 27.54): the previous one a delta on
            // this one, a line or two where a save changed that many.
            if (!pv.manifest.empty() && pv.manifest != v.manifest)
                supersede(pv.manifest, v.manifest);
            evictVersions(keep);

            t.script = "{\"version\":" + std::to_string(v.num) + ",\"docxml\":\"" + docHash
                     + "\",\"blobs\":" + std::to_string(nblobs) + ",\"schema\":"
                     + std::to_string(schema) + ",\"path\":\"" + escaped + "\"}";
            std::vector<LogOp> none;
            _store->append(t, none);
        });
}

std::vector<std::pair<long, long>> TransactionLogCore::allStrings() const
{
    const StringHasherRef& hasher = _history.hasher();
    if (!hasher || hasher->lastID() <= 0)
        return {};
    return {{1, hasher->lastID()}};
}

int64_t TransactionLogCore::metaBranch(int64_t& head)
{
    flush();
    int64_t id = 1;
    const std::string current = _store->getMeta("branch");
    if (!current.empty())
        id = std::stoll(current);
    LogBranch branch;
    if (!_store->getBranch(id, branch)) {
        id = 1;
        _store->getBranch(id, branch);
    }
    head = branch.head;
    return id;
}

int64_t TransactionLogCore::recordFile(const std::string& path, const TransactionLog::Entries& entries,
                                    const TransactionLog::Blobs& blobs, int schema)
{
    // What TransactionLog::onRestore records for a document opened from the
    // file, with no document (sec 27.13): the file as found is the version
    // its embedded copy numbers next, on the branch the copy names.
    if (entries.empty() || entries.front().first != "Document.xml")
        return 0;
    int64_t head = 0;
    const int64_t branch = metaBranch(head);
    LogVersion v;
    v.num = ++_nextVersion;
    v.uuid = Base::Uuid::createUuid();
    v.seq = head;
    v.branch = branch;
    v.env = _environment;
    v.schema = schema;
    v.created = now();
    LogTransaction t;
    t.parent = head;
    t.branch = branch;
    t.seq = ++_nextSeq;
    t.kind = "restore";
    t.name = "restore";
    t.time = v.created;
    t.session = sessionOf(ActorScope::current().get());
    t.ordinal = ++_ordinals[t.session];
    TransactionLog::Captures captures;
    for (const auto& e : entries)
        captures.emplace_back(e.first, Base::EntryCapture(e.second));
    syncStrings(true);
    postVersion(v, t, captures, blobs, schema, path, allStrings(), false);
    return v.num;
}

TransactionLog::TransactionLog(Document& doc, const LogVersion* at)
    : _history(doc.getFileHistory())
    , _c(TransactionLogCore::of(_history))
    , _doc(doc)
{
    _c._cursors.insert(this);
    if (_c._store->getMeta("document").empty())
        _c._store->setMeta("document", _doc.Uid.getValueStr());
    if (at) {
        _branch = 0;
        _head = at->seq;
        _at = at->num;
    }
    else {
        pickBranch();
        auto it = _c._holders.find(_branch);
        if (it == _c._holders.end()) {
            _c._holders[_branch] = this;
        }
        else {
            // Its branch is checked out already (sec 27.7), by a version
            // document that took it: this one stays at its head, and a
            // change makes a branch of its own.
            FC_WARN("the branch " << _branch << " of " << _history.path()
                    << " is open in another document; " << _doc.getName()
                    << " branches at its first change");
            _branch = 0;
        }
    }
    _config = CaptureConfig(doc);
}

void TransactionLog::pickBranch()
{
    // The branch the document is on (sec 26), `main` unless the store says
    // otherwise, and its head: what the next row follows.
    _branch = 1;
    const std::string current = _c._store->getMeta("branch");
    if (!current.empty())
        _branch = std::stoll(current);
    LogBranch branch;
    if (!_c._store->getBranch(_branch, branch)) {
        _branch = 1;
        _c._store->getBranch(_branch, branch);
    }
    _head = branch.head;
}

void TransactionLog::openStore()
{
    _c.openStore();
    pickBranch();
}

void TransactionLog::post(std::function<void()> job)
{
    _c.post(std::move(job));
}

void TransactionLog::flush()
{
    _c.flush();
}

int64_t TransactionLog::session() const
{
    return _c._session;
}

int64_t TransactionLog::environment() const
{
    return _c._environment;
}

const std::string& TransactionLog::path() const
{
    return _c._path;
}

int64_t TransactionLog::lastSeq() const
{
    return _c._nextSeq;
}

TransactionStore& TransactionLogCore::store()
{
    if (!_reader)
        _reader = std::make_unique<FlushingStore>(*this);
    return *_reader;
}

TransactionLog::Embedded TransactionLogCore::embed(const std::string& saveDate, int64_t branch,
                                                  const std::string& saveId)
{
    auto clock = std::chrono::steady_clock::now();
    auto split = [&clock]() {
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - clock).count();
        clock = now;
        return secs;
    };
    flush();
    TransactionLog::Embedded out;
    out.saveId = saveId.empty() ? Base::Uuid::createUuid() : saveId;
    out.version = _nextVersion + 1;
    const std::string dir = _history.directory() + "/history";
    Base::FileInfo(dir).createDirectories();
    out.path = dir + "/embed-" + out.saveId + ".db";
    Base::FileInfo(out.path).deleteFile();
    // The file-scope state (sec 27.40), into the store itself, so that the
    // copy carries it and a recovery after a crash has it as of this save.
    // The strings too (sec 27.50 item 2), which the copy then leaves out:
    // the file carries them as a member of its own.
    syncStrings(true);
    flush();
    // The save, by its id (sec 30.19 G1): the version it becomes and the
    // row its state is at, so a file that says only `Version` names a save
    // this history made. Before the copy is taken, which carries it.
    if (branch) {
        LogBranch saving;
        if (_store->getBranch(branch, saving))
            _store->setMeta("save:" + out.saveId,
                            std::to_string(out.version) + " " + std::to_string(saving.head));
    }
    _store->setMeta("last_object_id", std::to_string(_history.lastObjectId()));
    std::vector<std::pair<long, std::string>> names;
    names.reserve(_history.objectNames().size());
    for (const auto& n : _history.objectNames())
        names.emplace_back(n.second, n.first);
    _store->addObjectNames(names);
    _store->addLastGeoIds({_history.lastGeoIds().begin(), _history.lastGeoIds().end()});
    const double tState = split();
    _store->copyTo(out.path);
    auto copy = TransactionStore::openSQLite(out.path);
    const double tCopy = split();
    // Retention (16.4, 13.3): the named versions travel, the unnamed ones
    // and the cache tier do not; the ops do. Each branch's newest travels
    // too (sec 26.2 item 5), so a switch in the file opened elsewhere
    // checks out its own tip.
    std::map<int64_t, int64_t> newest;
    const auto versions = copy->versions();
    for (const auto& v : versions)
        newest[v.branch] = v.num;
    std::vector<int64_t> evicted;
    for (const auto& v : versions) {
        if (v.kind != "named" && newest[v.branch] != v.num)
            evicted.push_back(v.num);
    }
    copy->dropTier("cache", evicted);
    copy->clearStrings();
    // Every blob the copy still holds as a file (23.16): a kept version's,
    // and an op value's. The ones kept as deltas travel inside the copy.
    for (const auto& hash : copy->entitiesStoredAs("file")) {
        LogEntity e;
        if (!copy->getEntity(hash, e) || e.kind != "blob")
            continue;
        out.blobs.emplace_back(hash, e.data.empty() ? std::string() : "." + e.data);
    }
    // The copy's history ends at this save: a session still open here is
    // closed there, now. A closing time is all that says a user left (sec
    // 30.6 U2), and a file would otherwise say of everyone who was in when
    // it was saved that they never did.
    const double saved = now();
    for (const auto& session : copy->sessions()) {
        if (session.closed == 0)
            copy->closeSession(session.id, saved);
    }
    copy->setMeta("save_id", out.saveId);
    copy->setMeta("save_date", saveDate);
    copy->setMeta("version_counter", std::to_string(out.version));
    // The branch the file reopens on is the saving document's (sec 27.16):
    // the store's names whichever document of the file last switched, and a
    // version document saved as the file never sets it.
    if (branch)
        copy->setMeta("branch", std::to_string(branch));
    // The copy was the whole store; what retention took out is free pages,
    // which would travel in the file and come back as the live store when
    // it is opened elsewhere (sec 27.53).
    const double tRetain = split();
    copy->vacuum();
    copy.reset();
    FC_LOG("embed: state and flush " << tState << "s, copy " << tCopy << "s, retention "
           << tRetain << "s, vacuum " << split() << "s");
    return out;
}

bool TransactionLog::readRecoveryMeta(const std::string& oldDir, RecoverInfo& info)
{
    const std::string path = oldDir + "/history/log.db";
    if (!Base::FileInfo(path).exists())
        return false;
    try {
        auto store = TransactionStore::openSQLite(path);
        info.label = store->getMeta("label");
        info.fileName = store->getMeta("file");
        return true;
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log: cannot read " << path << ": " << e.what());
    }
    return false;
}

void TransactionLog::noteIdentity()
{
    std::string label = _doc.Label.getValue();
    std::string file = _doc.FileName.getValue();
    post([this, label, file]() {
        _c._store->setMeta("label", label);
        _c._store->setMeta("file", file);
    });
}

bool TransactionLog::recover(const std::string& oldDir, RecoverInfo& info)
{
    flush();
    const std::string oldLog = oldDir + "/history/log.db";
    if (!Base::FileInfo(oldLog).exists())
        return false;
    if (_c._nextSeq != 0 || _c._nextVersion != 0) {
        FC_ERR("transaction log of " << _doc.getName() << " has history; not recovering "
               << oldDir);
        return false;
    }
    _c.closeSessions();
    _c._store.reset();
    for (const char* suffix : {"", "-wal", "-shm"}) {
        Base::FileInfo(_c._path + suffix).deleteFile();
    }
    // The WAL moves with the database: the rows the crashed process
    // committed and SQLite had not yet written back are in it. The shared
    // memory index is rebuilt from it on open.
    bool moved = true;
    for (const char* suffix : {"", "-wal"}) {
        Base::FileInfo from(oldLog + suffix);
        if (!from.exists())
            continue;
        if (!from.renameFile((_c._path + suffix).c_str()) && !from.copyTo((_c._path + suffix).c_str()))
            moved = false;
    }
    Base::FileInfo(oldLog + "-shm").deleteFile();
    if (!moved) {
        FC_ERR("cannot take over the log of " << oldDir);
        openStore();
        _c.openProcessSession();
        return false;
    }

    // The blobs: every file of the old store into this one's, then swept.
    auto& manager = _doc.getFileBlobManager();
    const std::string oldBlobs = oldDir + "/blobs";
    const std::string newBlobs = _doc.getFileHistory().directory() + "/blobs";
    if (Base::FileInfo(oldBlobs).isDir()) {
        Base::FileInfo(newBlobs).createDirectories();
        for (const auto& file : Base::FileInfo(oldBlobs).getDirectoryContent()) {
            if (!file.isFile())
                continue;
            const std::string target = newBlobs + "/" + file.fileName();
            if (!Base::FileInfo(file).renameFile(target.c_str()))
                Base::FileInfo(file).copyTo(target.c_str());
        }
    }
    manager.recoverStore();

    openStore();
    _c._blobs.clear();
    _c._sourced.clear();
    size_t missing = 0;
    for (const auto& hash : _c._store->entitiesStoredAs("file")) {
        if (auto blob = manager.recovered(hash))
            _c._blobs[hash] = blob;
        else
            ++missing;
    }
    if (missing)
        FC_WARN("recovery of " << _doc.getName() << ": " << missing
                << " blob(s) the log names are not in the store left behind");
    const double closed = now();
    for (const auto& session : _c._store->sessions()) {
        if (session.closed == 0) {
            _c._store->closeSession(session.id, closed);
            info.crashedSessions.push_back(session.id);
        }
    }
    info.label = _c._store->getMeta("label");
    info.fileName = _c._store->getMeta("file");
    _c.openProcessSession();
    FC_LOG("transaction log of " << _doc.getName() << " recovered from " << oldDir << ": seq "
           << _c._nextSeq << ", versions " << _c._nextVersion);
    return true;
}

int64_t TransactionLog::recordRecovery(const std::string& script)
{
    return record("recover", "Recovered", script);
}

int64_t TransactionLog::record(const char* kind, const std::string& name,
                               const std::string& script, int64_t mergeFrom)
{
    LogTransaction t;
    number(t);
    t.mergeFrom = mergeFrom;
    t.kind = kind;
    t.name = name;
    t.time = now();
    t.script = script;
    post([this, t]() mutable {
        std::vector<LogOp> none;
        _c._store->append(t, none);
    });
    return t.seq;
}

int64_t TransactionLog::login(const Actor& actor)
{
    const int64_t session = _c.sessionOf(&actor);
    // A version document not yet changed is on no branch, and a login is
    // no change (sec 27.5): the session says who came, with no row.
    if (_branch == 0 || !session || session == _c._session)
        return 0;
    auto quoted = [](const std::string& text) {
        std::string j = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\')
                j += '\\';
            j += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
        }
        return j + '"';
    };
    // Who, how the name is known, and what the connection may do (sec 30.6
    // U2). Where it came from is not recorded.
    std::string script = "{\"user\":" + quoted(actor.name) + ",\"kind\":\""
        + Actor::kindName(actor.kind) + "\",\"access\":" + quoted(actor.access)
        + ",\"verified\":" + (actor.kind == Actor::Verified ? "true" : "false") + "}";
    LogTransaction t;
    t.session = session;
    number(t);
    t.kind = "login";
    t.name = "Login " + actor.name;
    t.time = now();
    t.script = script;
    post([this, t]() mutable {
        std::vector<LogOp> none;
        _c._store->append(t, none);
    });
    return t.seq;
}

bool TransactionLog::logout(const Actor& actor)
{
    return _c.closeLogin(actor);
}

int64_t TransactionLog::importSession(const LogSession& session, const LogUser& user,
                                      const std::string& environment, const std::string& file)
{
    flush();
    auto& store = *_c._store;
    if (!session.uuid.empty()) {
        for (const auto& s : store.sessions()) {
            if (s.uuid == session.uuid)
                return s.id;
        }
    }
    // The other copy's desktop user is its own (sec 30.13 F4).
    const bool local = user.kind.empty() || user.kind == Actor::kindName(Actor::Local);
    const std::string name = user.name.empty() ? std::string("host") : user.name;
    const int64_t here = local ? store.user(Actor::kindName(Actor::Fork), name + " (" + file + ")")
                               : store.user(user.kind, name);
    const int64_t env = store.environment(environment);
    const int64_t id = store.openSession(env, here, session.host, session.access, session.uuid,
                                         session.opened);
    store.closeSession(id, session.closed > 0 ? session.closed : session.opened);
    return id;
}

int64_t TransactionLog::reappend(LogTransaction t)
{
    // The record as it was made -- its kind, name, annotation, time and
    // session -- at the head, under a new number.
    number(t);
    t.mergeFrom = 0;
    t.inverts = 0;
    post([this, t]() mutable {
        std::vector<LogOp> none;
        _c._store->append(t, none);
    });
    return t.seq;
}

bool TransactionLog::othersStandOn(const std::vector<int64_t>& seqs) const
{
    for (const TransactionLog* cursor : _c._cursors) {
        if (cursor != this && std::find(seqs.begin(), seqs.end(), cursor->_head) != seqs.end())
            return true;
    }
    return false;
}

int64_t TransactionLog::forkHere(int64_t target, const std::string& name)
{
    flush();
    auto& store = *_c._store;
    LogBranch taken;
    if (name.empty() || store.findBranch(name, taken))
        THROWM(Base::ValueError, "branch name '" + name + "' is empty or taken");
    LogBranch branch;
    branch.name = name;
    branch.fromVersion = _at;
    branch.fromSeq = _head;
    branch.head = _head;
    branch.target = target;
    branch.created = now();
    store.addBranch(branch);
    auto held = _c._holders.find(_branch);
    if (held != _c._holders.end() && held->second == this)
        _c._holders.erase(held);
    _c._holders[branch.id] = this;
    _branch = branch.id;
    _at = 0;
    return branch.id;
}

void TransactionLog::moveHead(int64_t head)
{
    flush();
    _head = head;
}

bool TransactionLog::setBranch(int64_t id)
{
    flush();
    LogBranch branch;
    if (!_c._store->getBranch(id, branch))
        return false;
    // One document per branch (sec 27.7, git's worktree rule).
    auto other = _c._holders.find(id);
    if (other != _c._holders.end() && other->second != this)
        THROWM(Base::RuntimeError, "branch '" + branch.name + "' is open in "
                                       + other->second->_doc.Label.getStrValue());
    auto held = _c._holders.find(_branch);
    if (held != _c._holders.end() && held->second == this)
        _c._holders.erase(held);
    _c._holders[id] = this;
    _branch = id;
    _head = branch.head;
    _at = 0;
    // The branch a file continues on when it is next opened: a version
    // document's is not the file's, until it is saved as the file.
    if (!_doc.testStatus(Document::VersionDoc))
        _c._store->setMeta("branch", std::to_string(id));
    return true;
}

bool TransactionLog::planBranch(LogBranch& from, std::string& name)
{
    auto& store = *_c._store;
    LogVersion version;
    const bool haveVersion = _at && store.getVersion(_at, version);
    // The branch whose tip this is, if nothing moved it since and no
    // document holds it: the version's document continues it.
    bool haveFrom = false;
    for (const auto& b : store.branches()) {
        if (haveVersion ? b.id == version.branch : b.head == _head) {
            from = b;
            haveFrom = true;
            break;
        }
    }
    if (haveFrom && from.closed == 0 && !_c._holders.count(from.id)
            && _c.unchangedSince(from.head, _head)) {
        name = from.name;
        return true;
    }
    // A branch of its own (sec 27.5 ruling 3), named after where it forked.
    std::string base = haveFrom ? from.name : std::string("branch");
    base += haveVersion ? "@v" + std::to_string(version.num) : "@" + std::to_string(_head);
    name = base;
    LogBranch taken;
    for (int i = 2; store.findBranch(name, taken); ++i)
        name = base + "#" + std::to_string(i);
    return false;
}

std::string TransactionLog::branchName()
{
    LogBranch branch;
    if (_branch != 0)
        return store().getBranch(_branch, branch) ? branch.name : std::string();
    std::string name;
    planBranch(branch, name);
    return name;
}

void TransactionLog::ensureBranch()
{
    if (_branch != 0)
        return;
    flush();
    auto& store = *_c._store;
    LogVersion version;
    const bool haveVersion = _at && store.getVersion(_at, version);
    LogBranch from;
    std::string name;
    if (planBranch(from, name)) {
        _c._holders[from.id] = this;
        _branch = from.id;
        _head = from.head;
        _at = 0;
        return;
    }
    LogBranch branch;
    branch.name = name;
    branch.fromVersion = haveVersion ? version.num : 0;
    branch.fromSeq = _head;
    branch.head = _head;
    branch.created = now();
    store.addBranch(branch);
    if (haveVersion && version.kind != "named")
        store.nameVersion(version.num, "branch " + name);
    _c._holders[branch.id] = this;
    _branch = branch.id;
    _at = 0;
    FC_LOG(_doc.getName() << " branches as '" << name << "' at its first change");
    // The branch's own record, before the row that made it.
    std::ostringstream script;
    script << "{\"from_version\":" << branch.fromVersion << ",\"from_seq\":" << branch.fromSeq
           << ",\"implicit\":true}";
    LogTransaction t;
    number(t);
    t.kind = "branch";
    t.name = "Branch " + name;
    t.time = now();
    t.script = script.str();
    post([this, t]() mutable {
        std::vector<LogOp> none;
        _c._store->append(t, none);
    });
    _doc.refreshVersionNames();
    _doc.signalBranchesChanged(_doc);
}

Document* TransactionLogCore::holderOf(int64_t id) const
{
    auto it = _holders.find(id);
    return it != _holders.end() ? &it->second->_doc : nullptr;
}

std::vector<Document*> TransactionLogCore::documents() const
{
    std::vector<Document*> docs;
    for (auto cursor : _cursors)
        docs.push_back(&cursor->_doc);
    return docs;
}

bool TransactionLogCore::unchangedSince(int64_t head, int64_t seq)
{
    if (head == seq)
        return true;
    if (head < seq)
        return false;
    flush();
    bool reached = false;
    for (const auto& t : _store->chain(head, seq)) {
        if (t.seq == seq)
            reached = true;
        else if (t.seq > seq && !_store->ops(t.seq).empty())
            return false;
    }
    return reached;
}

Document* TransactionLogCore::documentAt(const LogVersion& version, bool frozen)
{
    // A document is the version when its branch moved past it only by
    // records -- a save, a snapshot, a switch: rows with no ops.
    for (auto cursor : _cursors) {
        // A pin takes only the frozen instance, which never moves (sec
        // 27.14, 27.22); anything else never takes it.
        if (frozen != cursor->_doc.testStatus(Document::FrozenVersion))
            continue;
        if (cursor->_head == version.seq)
            return &cursor->_doc;
        if (cursor->_branch != version.branch && cursor->_branch != 0)
            continue;
        if (unchangedSince(cursor->_head, version.seq))
            return &cursor->_doc;
    }
    return nullptr;
}

// The file-level half is the core's (sec 27.7); a document's log hands it on.

TransactionStore& TransactionLog::store()
{
    return _c.store();
}

bool TransactionLog::readValue(const std::string& hash, CapturedValue& out)
{
    return _c.readValue(hash, out);
}

bool TransactionLog::readBytes(const std::string& hash, std::string& out, LogEntity* entity,
                               int depth)
{
    return _c.readBytes(hash, out, entity, depth);
}

FileBlobHandle TransactionLog::heldBlob(const std::string& hash)
{
    return _c.heldBlob(hash);
}

size_t TransactionLog::heldBlobCount()
{
    return _c.heldBlobCount();
}

bool TransactionLog::readRevert(int64_t seq, Revert& out)
{
    return _c.readRevert(seq, out);
}

TransactionLog::Embedded TransactionLog::embed(const std::string& saveDate)
{
    return _c.embed(saveDate, _branch);
}

TransactionLog::Embedded TransactionLog::embedForFile(const std::string& saveDate,
                                                      const std::string& saveId)
{
    return _c.embed(saveDate, 0, saveId);
}

std::string TransactionLog::noteSave()
{
    flush();
    const std::string id = Base::Uuid::createUuid();
    const std::string num = std::to_string(_c._store->lastVersion());
    _c._store->setMeta("save:" + id, num + " " + std::to_string(_head));
    return num + " " + id;
}

bool TransactionLog::savedAt(TransactionStore& store, const std::string& saveId,
                             int64_t& version, int64_t& seq)
{
    version = 0;
    seq = 0;
    if (saveId.empty())
        return false;
    const std::string meta = store.getMeta("save:" + saveId);
    if (meta.empty())
        return false;
    std::istringstream in(meta);
    in >> version >> seq;
    return seq > 0;
}

Document* TransactionLog::holderOf(int64_t id) const
{
    return _c.holderOf(id);
}

std::vector<Document*> TransactionLog::documents() const
{
    return _c.documents();
}

Document* TransactionLog::documentAt(const LogVersion& version)
{
    return _c.documentAt(version);
}

void TransactionLog::forgetLiveValues()
{
    // Sec 26: the document was made another branch's state without a
    // transaction, so nothing the log knew of its live values holds -- what
    // an after ref waits on, which properties it holds current, the copies
    // kept for the next edit. The next snapshot serialises afresh.
    // This document's only: the hashes are the file's, by property, and
    // another document of the file still holds its own current (sec 29.6).
    flush();
    _pending.clear();
    for (int64_t key : _recorded)
        _c._hashById.erase(key);
    _recorded.clear();
    _misses.clear();
    TransactionCopyCache::dropOwner(this);
}

void TransactionLog::forgetValue(const Property& prop)
{
    _recorded.erase(prop.getID());
}

bool TransactionLog::adoptStore(const std::string& path)
{
    if (!_c.adoptEmbedded(path))
        return false;
    pickBranch();
    _adopted = true;
    return true;
}

bool TransactionLog::adoptClosed(const std::string& path)
{
    if (!adoptStore(path))
        return false;
    return setBranch(_c.closeAdopted());
}

void TransactionLog::closeStore()
{
    flush();
    _c._store.reset();
}

bool TransactionLog::reopenStore()
{
    if (_c._store)
        return true;
    try {
        openStore();
        FC_LOG("transaction log moved to " << _c._path);
        return true;
    }
    catch (Base::Exception& e) {
        FC_ERR("cannot reopen the transaction log of " << _doc.getName() << ": " << e.what());
    }
    return false;
}

TransactionLog::~TransactionLog()
{
    // The worker is the file's and goes on; this document's jobs, which
    // name this cursor, are written before it goes.
    _c._cursors.erase(this);
    auto held = _c._holders.find(_branch);
    if (held != _c._holders.end() && held->second == this)
        _c._holders.erase(held);
    TransactionCopyCache::dropOwner(this);
    try {
        flush();
    }
    catch (...) {
    }
}

void TransactionLog::onRecompute(const std::vector<RecomputedObject>& objects, double seconds,
                                 Transaction* open)
{
    // A version document recomputed but not changed is still the version:
    // no record, and no branch for it (sec 27.5).
    if (objects.empty() || _branch == 0)
        return;
    try {
        LogTransaction t;
        t.kind = "recompute";
        t.name = "recompute";
        t.time = now();
        // Recorded after an operation, the recompute is its author's (sec
        // 30.3 S.b): the open transaction's, else whoever acts now.
        t.session = _c.sessionOf(open ? open->Author.get() : ActorScope::current().get());
        // The record, as JSON in the script column: the duration, and per
        // object its id, the seconds it took, and its error text if any;
        // then its touched state before ("b" the bits, "p" the touched
        // properties) and after ("a", "q"), after left out when clean
        // (sec 27.58). The environment is not repeated: the row's session
        // names it.
        auto secs = [](double v) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4g", v);
            return std::string(buf);
        };
        auto quoted = [](std::string& j, const std::string& text) {
            j += '"';
            for (char c : text) {
                if (c == '"' || c == '\\')
                    j += '\\';
                else if (c == '\n') {
                    j += "\\n";
                    continue;
                }
                j += c;
            }
            j += '"';
        };
        auto names = [&](std::string& j, const char* key, const std::vector<std::string>& props) {
            if (props.empty())
                return;
            j += ",\"";
            j += key;
            j += "\":[";
            for (size_t i = 0; i < props.size(); ++i) {
                if (i)
                    j += ',';
                quoted(j, props[i]);
            }
            j += ']';
        };
        std::string j = "{\"seconds\":" + secs(seconds) + ",\"objects\":[";
        bool first = true;
        for (auto& o : objects) {
            j += first ? "{" : ",{";
            first = false;
            j += "\"id\":" + std::to_string(o.id);
            if (o.done)
                j += ",\"s\":" + secs(o.seconds);
            if (o.error) {
                j += ",\"error\":";
                quoted(j, o.message);
            }
            if (o.before >= 0) {
                j += ",\"b\":" + std::to_string(o.before);
                names(j, "p", o.beforeProps);
            }
            if (o.after || !o.afterProps.empty()) {
                j += ",\"a\":" + std::to_string(o.after);
                names(j, "q", o.afterProps);
            }
            j += '}';
        }
        j += "]}";
        t.script = j;
        auto write = [t](TransactionLog& log) mutable {
            log.number(t);
            log.remember(t, nullptr);
            log.post([&log, t]() mutable {
                std::vector<LogOp> none;
                log._c._store->append(t, none);
            });
        };
        if (open)
            open->AfterLogRow.emplace_back(std::move(write));
        else
            write(*this);
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

bool TransactionLogCore::readRevert(int64_t seq, TransactionLog::Revert& out)
{
    flush();
    bool found = false;
    for (const auto& t : _store->transactions(seq, 1))
        found = t.seq == seq;
    if (!found)
        return false;
    out.ops = _store->ops(seq);
    for (const auto& o : out.ops) {
        if (o.vbefore.empty() || out.values.count(o.vbefore))
            continue;
        LogEntity e;
        if (!_store->getEntity(o.vbefore, e))
            continue;
        for (const auto& r : e.refs) {
            if (r.role == "blob")
                restoreBlob(r.target, r.name);
        }
        CapturedValue v;
        if (readValue(o.vbefore, v))
            out.values.emplace(o.vbefore, std::move(v));
    }
    return true;
}

bool TransactionLogCore::restoreBlob(const std::string& hash, const std::string& ext, int depth)
{
    if (depth > 1024)
        return false;
    LogEntity e;
    if (!_store->getEntity(hash, e) || e.kind != "blob")
        return false;
    auto& manager = _history.blobs();
    FileBlobHandle blob = manager.find(hash);
    if (!blob) {
        std::string bytes;
        if (!readBytes(hash, bytes)) {
            FC_ERR("transaction log: blob " << hash << " cannot be read back");
            return false;
        }
        std::string extension = e.enc == "file" ? e.data : ext;
        blob = manager.adoptBytes(bytes, extension.empty() ? nullptr : extension.c_str());
        if (!blob || blob->hash() != hash) {
            FC_ERR("transaction log: blob " << hash << " read back as "
                   << (blob ? blob->hash() : std::string("nothing")));
            return false;
        }
    }
    // Held as a file again: current once more, the newest of its chain.
    putBlob(blob);
    for (const auto& r : e.refs) {
        if (r.role == "blob")
            restoreBlob(r.target, r.name, depth + 1);
    }
    return true;
}

int64_t TransactionLog::onSnapshot(const Captures& entries, const Blobs& blobs, int schema,
                                   const char* kind)
{
    return snapshot(kind, _doc.FileName.getValue(), entries, blobs, schema);
}

TransactionLog::Blobs TransactionLog::versionBlobs(const Entries& entries, const Blobs& blobs)
{
    auto hashesIn = [](const std::string& xml, size_t from, size_t to, std::set<std::string>& out) {
        static const std::string attrs[] = {"hash=\"", "db=\""};
        for (const auto& attr : attrs) {
            for (size_t at = xml.find(attr, from); at != std::string::npos && at < to;
                 at = xml.find(attr, at + 1)) {
                const size_t start = at + attr.size();
                const size_t end = xml.find('"', start);
                if (end == std::string::npos)
                    break;
                out.insert(xml.substr(start, end - start));
            }
        }
    };
    std::set<std::string> history, model;
    for (const auto& e : entries) {
        const std::string& xml = e.second;
        size_t from = std::string::npos, to = std::string::npos;
        if (e.first == "Document.xml") {
            const size_t prop = xml.find("<Property name=\"History\"");
            if (prop != std::string::npos) {
                from = xml.find("<History", prop);
                const size_t close = xml.find("</Property>", prop);
                to = close;
                if (from == std::string::npos || from > close)
                    from = to = std::string::npos;
            }
        }
        if (from == std::string::npos) {
            hashesIn(xml, 0, xml.size(), model);
            continue;
        }
        hashesIn(xml, from, to, history);
        hashesIn(xml, 0, from, model);
        hashesIn(xml, to, xml.size(), model);
    }
    Blobs out;
    for (const auto& b : blobs) {
        if (b.second && history.count(b.second->hash()) && !model.count(b.second->hash()))
            continue;
        out.push_back(b);
    }
    return out;
}

int64_t TransactionLog::onRestore(const std::string& path, const Entries& entries,
                                  const Blobs& blobs, int schema)
{
    if (!_adopted && (_c._nextSeq != 0 || _c._nextVersion != 0)) {
        // A store with history at restore is either an adopted embedded
        // copy, whose version the file now becomes, or a mistake.
        FC_WARN("transaction log of " << _doc.getName() << " is not empty at restore");
        return 0;
    }
    _adopted = false;
    // Read, not saved: no marks say which strings it uses (sec 27.50 item 4).
    _versionStrings.clear();
    _haveVersionStrings = false;
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
    // A version writes the shapes' blobs, after which a fresh copy names
    // its blob where a kept one carries the exported bytes: the next edit
    // of each property copies again (sec 25.4).
    TransactionCopyCache::dropOwner(this);
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
        v.num = ++_c._nextVersion;
        v.uuid = Base::Uuid::createUuid();
        v.seq = _head;
        v.branch = _branch;
        v.env = _c._environment;
        v.schema = schema;
        v.created = now();

        LogTransaction t;
        number(t);
        t.kind = kind;
        t.name = kind;
        t.time = v.created;

        // The file's strings go first, and the ids this version uses with it
        // (sec 27.50 items 2, 4).
        _c.syncStrings(true);
        const bool marked = _haveVersionStrings;
        std::vector<std::pair<long, long>> strings =
            marked ? TransactionLogCore::idRanges(_versionStrings) : _c.allStrings();
        _versionStrings.clear();
        _haveVersionStrings = false;
        _c.postVersion(v, t, entries, blobs, schema, path, std::move(strings), marked);
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

void TransactionLogCore::evictVersions(long keep)
{
    // Sec 16.3: unnamed versions over the limit go, oldest first; named
    // ones never, and never the newest, which is what the cadence and a
    // cold undo anchor on. Worker thread, after an addVersion; the limit
    // was read on the main thread when the job was posted.
    if (keep <= 0)
        return;
    auto versions = _store->versions();
    // The newest of each branch stays whatever the limit (sec 26): what a
    // switch to it checks out, so a switch never replays more than the
    // branch's own tail. On a log that never branched, the newest.
    std::map<int64_t, int64_t> newest;
    for (const auto& v : versions)
        newest[v.branch] = v.num;
    std::set<int64_t> kept;
    for (const auto& kv : newest)
        kept.insert(kv.second);
    std::vector<int64_t> unnamed;
    for (const auto& v : versions) {
        if (v.kind == "unnamed" && !kept.count(v.num))
            unnamed.push_back(v.num);
    }
    // What is left is the older unnamed ones; keep the last (keep - 1) of
    // them so that, with the newest, `keep` unnamed versions remain.
    size_t excess = unnamed.size() + 1 > static_cast<size_t>(keep)
                        ? unnamed.size() + 1 - static_cast<size_t>(keep) : 0;
    if (!excess)
        return;
    unnamed.resize(excess);
    FC_LOG("transaction log: evict versions " << unnamed.front() << ".." << unnamed.back());
    _store->evictVersions(unnamed);
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

std::string TransactionLogCore::putValue(const CapturedValue& value, const std::string& tier)
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
        // The edge carries the extension: once the blob is kept as a delta
        // its row no longer does, and a cold undo re-adopts it (sec 24.3).
        blobRefs.push_back(LogRef {putBlob(blob), "blob", blob->extension()});
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
    // The string ids it uses (sec 27.50 item 4), dropped with it.
    _store->addStringRefs(e.hash, idRanges(value.stringIds));
    return e.hash;
}

std::string TransactionLogCore::putBlob(const FileBlobHandle& blob)
{
    const std::string& hash = blob->hash();
    // The row below names the blob, so its bytes go to the disk first
    // (docs/FileBlobsManager.md sec 15.8). A commit made its blobs durable
    // in one go already; this catches whatever path did not.
    _history.blobs().makeDurable({blob});
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

void TransactionLogCore::putSources(const FileBlobHandle& blob, int depth)
{
    if (!blob || depth > 1024 || !_sourced.insert(blob->hash()).second)
        return;
    for (const auto& hash : blob->sources()) {
        std::string ext;
        if (auto source = _history.blobs().find(hash)) {
            putBlob(source);
            putSources(source, depth + 1);
            ext = source->extension();
        }
        else if (!_store->hasEntity(hash)) {
            FC_WARN("transaction log: blob " << blob->hash() << " reads " << hash
                    << ", which is in no store");
            continue;
        }
        _store->addRef(blob->hash(), LogRef {hash, "blob", ext});
    }
}

void TransactionLogCore::releaseBlobs()
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

void TransactionLog::restoreBlobsOf(const std::string& hash)
{
    flush();
    LogEntity e;
    if (!_c._store->getEntity(hash, e))
        return;
    for (const auto& r : e.refs) {
        if (r.role == "blob" && !_c.liveBlob(r.target))
            _c.restoreBlob(r.target, r.name);
    }
}

FileBlobHandle TransactionLogCore::liveBlob(const std::string& hash) const
{
    return _history.blobs().find(hash);
}

FileBlobHandle TransactionLogCore::heldBlob(const std::string& hash)
{
    flush();
    auto it = _blobs.find(hash);
    return it != _blobs.end() ? it->second : FileBlobHandle();
}

size_t TransactionLogCore::heldBlobCount()
{
    flush();
    return _blobs.size();
}

std::string TransactionLogCore::putBytes(const std::string& bytes, const std::string& kind,
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

std::string TransactionLogCore::putComposite(const std::string& entry,
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
    TransactionLog::Composite c;
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
            TransactionLog::CompositePart p;
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
        // No edge per value: the composite's data lists them, and the
        // collector reads it (sec 27.53).
        ce.refs.push_back(LogRef {c.skeleton, "skeleton", ""});
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
        TransactionLog::Composite pc;
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

bool TransactionLogCore::composeEntry(const std::string& data, std::string& out)
{
    TransactionLog::Composite c;
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

int TransactionLogCore::chainBelow(const std::string& hash, int depth)
{
    if (depth > 64)
        return depth;
    int longest = 0;
    for (const auto& below : _store->basedOn(hash))
        longest = std::max(longest, 1 + chainBelow(below, depth + 1));
    return longest;
}

void TransactionLogCore::supersede(const std::string& older, const std::string& newer)
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
            && old.kind != "composite" && old.kind != "manifest" && old.kind != "attach"
            && old.kind != "blob")
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
    if (!TransactionLog::deltaEncode(oldBytes, newBytes, patch))
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
bool TransactionLogCore::readBytes(const std::string& hash, std::string& out, LogEntity* entity,
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
            it != _blobs.end() ? it->second : _history.blobs().find(hash);
        ok = blob && blob->read(out);
    }
    else if (e.enc == "delta" && depth < 1024) {
        std::string base;
        ok = readBytes(e.base, base, nullptr, depth + 1)
            && TransactionLog::deltaDecode(e.data, base, e.size, out);
    }
    if (ok && entity) {
        e.data.clear();
        *entity = std::move(e);
    }
    return ok;
}

bool TransactionLogCore::readValue(const std::string& hash, CapturedValue& out)
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

void TransactionLog::ValueTask::captureNow(const CaptureConfig& config)
{
    if (!copy)
        return;
    if (dynamic_cast<const PropertyLinkBase*>(copy.get())) {
        captured = captureValue(config, *copy);
        isCaptured = true;
    }
    else if (dynamic_cast<const PropertyPythonObject*>(copy.get())) {
        // A Python object (a Proxy) is Python's to touch, on the thread that
        // runs it: its Save pickles through the interpreter, and releasing
        // the copy drops a reference. Both happen here, never on the worker
        // (sec 24.10: a view provider's Proxy, once view changes were
        // recorded, crashed the worker).
        captured = captureValue(config, *copy);
        isCaptured = true;
        copy.reset();
    }
    else if (!copy->canSaveOffThread()) {
        // A value sharing what the main thread may still change in place --
        // an unfrozen shape (sec 27.99) -- is written now, as committed.
        // The copy stays: the worker reads only its blob handle
        // (contentBlob()), and TransactionCopyCache hands it out again.
        captured = captureValue(config, *copy);
        isCaptured = true;
    }
}

void TransactionLog::writeValues(std::vector<ValueTask>& tasks, std::vector<LogOp>& ops)
{
    // Captured first, all of them: the rows written below name the blobs
    // the values hold, and a blob's bytes reach the disk before any row
    // naming it commits (docs/FileBlobsManager.md sec 15.8) -- one flush of
    // the blob store for the whole commit, not one per blob.
    std::vector<CapturedValue> captured(tasks.size());
    std::vector<FileBlobHandle> named;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];
        if (!task.copy && !task.isCaptured)
            continue;
        if (task.hashIn && !task.hashIn->empty())
            continue;   // written already, as the last commit's after value
        // A value that names its blob (decision 6b, `hash=` in the
        // fragment) is complete only while the file exists: the blob is an
        // entity of its own, held as long as the value is (sec 23.16).
        CapturedValue& cv = captured[i];
        cv = task.isCaptured ? std::move(task.captured) : captureValue(_config, *task.copy);
        // A shape names its file without noting it (it notes in
        // beforeSave, which a capture does not run): its contentBlob().
        if (auto referrer = task.copy ? dynamic_cast<const BlobReferrerProperty*>(task.copy.get())
                                      : nullptr) {
            auto blob = referrer->contentBlob();
            if (blob && std::find(cv.blobs.begin(), cv.blobs.end(), blob) == cv.blobs.end())
                cv.blobs.push_back(blob);
        }
        if (cv.ok)
            named.insert(named.end(), cv.blobs.begin(), cv.blobs.end());
    }
    _doc.getFileBlobManager().makeDurable(named);

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        auto& task = tasks[i];
        std::string hash;
        if (task.hashIn && !task.hashIn->empty())
            hash = *task.hashIn;
        else if (task.copy || task.isCaptured) {
            CapturedValue& cv = captured[i];
            if (cv.ok)
                hash = _c.putValue(cv, task.tier);
        }
        if (task.hashOut)
            *task.hashOut = hash;
        if (task.key && !hash.empty())
            _c._hashById[task.key] = hash;
        if (task.opIndex >= 0)
            ops[task.opIndex].vbefore = hash;
        if (task.afterIndex >= 0) {
            LogOp& op = ops[task.afterIndex];
            op.vafter = hash;
            // The op's before is now the older of the pair (sec 23.2); the
            // before tasks of this job come first, so it is filled.
            if (!hash.empty() && !op.vbefore.empty())
                _c.supersede(op.vbefore, hash);
        }
        if (task.resolveTxn > 0) {
            _c._store->resolveAfter(task.resolveTxn, task.resolveIdx, hash);
            // The op's before is now the older of the pair (sec 23.2).
            LogOp resolved;
            if (!hash.empty() && _c._store->getOp(task.resolveTxn, task.resolveIdx, resolved))
                _c.supersede(resolved.vbefore, hash);
        }
        // The share goes as soon as it is written, dropped on the main
        // thread.
        if (task.copy)
            _c.retire(std::move(task.copy));
    }
}

void TransactionLog::number(LogTransaction& t)
{
    // A version document's first row puts it on a branch (sec 27.5).
    if (_branch == 0)
        ensureBranch();
    t.parent = _head;
    t.branch = _branch;
    t.seq = ++_c._nextSeq;
    _head = t.seq;
    // Whoever acts now (sec 30.3 S.b), unless the row names its author: a
    // transaction's is the actor it was opened under, a record appended
    // again keeps the one it was made under.
    if (!t.session)
        t.session = _c.sessionOf(ActorScope::current().get());
    // Its place among its session's rows (sec 30.3 S.e); a record appended
    // again keeps the one it has, since it is the same row.
    if (!t.ordinal)
        t.ordinal = ++_c._ordinals[t.session];
    // Every row, so a walk back from the head finds its parent in memory.
    remember(t, nullptr);
}

void TransactionLog::remember(const LogTransaction& t, const std::vector<LogOp>* ops)
{
    if (_recentAt != _c._rewrites) {
        _recent.clear();
        _recentAt = _c._rewrites;
    }
    auto& row = _recent[t.seq];
    row.seq = t.seq;
    row.parent = t.parent;
    row.kind = t.kind;
    row.script = t.script;
    row.ops.clear();
    if (ops) {
        for (const auto& o : *ops) {
            if (o.op != "set" || o.ckind != "obj")
                continue;
            LogOp slim;
            slim.op = o.op;
            slim.ckind = o.ckind;
            slim.cid = o.cid;
            slim.prop = o.prop;
            slim.derived = o.derived;
            slim.touched = o.touched;
            row.ops.push_back(std::move(slim));
        }
    }
    while (_recent.size() > 4096)
        _recent.erase(_recent.begin());
}

bool TransactionLog::rowsBackTo(int64_t seq, std::vector<TouchedRow>& rows)
{
    rows.clear();
    if (seq <= 0)
        return false;
    // A trim or a squash rewrote rows the copies may name.
    if (_recentAt != _c._rewrites) {
        _recent.clear();
        _recentAt = _c._rewrites;
    }
    for (int64_t cur = _head; cur >= seq;) {
        auto it = _recent.find(cur);
        if (it == _recent.end())
            break;
        rows.push_back(it->second);
        if (cur == seq)
            return true;
        cur = it->second.parent;
    }
    // Not all in memory: the store's, which waits for the worker.
    rows.clear();
    const auto chain = store().chain(_head, seq);
    if (chain.empty() || chain.front().seq != seq)
        return false;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        TouchedRow row;
        row.seq = it->seq;
        row.parent = it->parent;
        row.kind = it->kind;
        row.script = it->script;
        for (auto& o : store().ops(it->seq)) {
            if (o.op == "set" && o.ckind == "obj")
                row.ops.push_back(std::move(o));
        }
        rows.push_back(std::move(row));
    }
    return true;
}

int64_t TransactionLog::onCommit(const Transaction& txn, const char* kind, const char* origin,
                                 int64_t inverts)
{
    if (txn.isEmpty())
        return 0;
    try {
        // The strings minted since, ahead of the values that name them
        // (sec 27.50 item 2): a recovery has every one a value it replays
        // refers to. The hasher the captures list ids of is the document's;
        // changed only with the worker idle, which reads it.
        _c.syncStrings(false);
        if (_config.hasher != _doc.getHasher().get()) {
            flush();
            _config.hasher = _doc.getHasher().get();
        }
        // The author is the actor the transaction was opened under (sec
        // 30.3 S.b), whoever closes it.
        LogTransaction t;
        // -- or the row's own, when it is another copy's (sec 30.13 F3).
        if (_stamp) {
            t.session = _stamp->session;
            t.ordinal = _stamp->ordinal;
        }
        else {
            t.session = _c.sessionOf(txn.Author.get());
        }
        number(t);
        t.id = txn.getID();
        t.kind = kind;
        t.origin = origin;
        t.name = txn.Name;
        t.time = _stamp && _stamp->time > 0 ? _stamp->time : now();
        t.inverts = inverts;
        t.mergeFrom = txn.MergeFrom;
        t.script = txn.LogScript;

        std::vector<LogOp> ops;
        std::vector<ValueTask> tasks;
        // Ops whose after ref is pending, by property id, keyed to
        // (t.seq, idx); moved into _pending once the job is queued.
        std::vector<std::pair<int64_t, Pending>> newPending;
        // The live properties behind them, and whether the copy is kept for
        // the next write (sec 25.4): only for a set, not for the sets a
        // create or an added dynamic property implies.
        std::vector<std::pair<const Property*, bool>> newPendingProps;
        auto pendOp = [&](const Property& prop, const ContainerInfo& c, const char* tier,
                          bool keep) {
            Pending p;
            p.txn = t.seq;
            p.idx = static_cast<int>(ops.size()) - 1;   // the op just emitted
            p.cid = c.cid;
            p.view = c.ckind == "view";
            p.prop = ops.back().prop;
            p.tier = tier;
            newPending.emplace_back(prop.getID(), p);
            newPendingProps.emplace_back(&prop, keep);
        };
        // A copy for the worker: the transaction's own, co-owned from now
        // (decision 4), or one made here for a value the transaction does
        // not hold. `fill` is the op whose before it is, -1 for none.
        auto task = [&](std::shared_ptr<const Property> copy, const char* tier, int fill,
                        int64_t pendingKey, std::shared_ptr<std::string> known = {}) {
            ValueTask v;
            v.copy = std::move(copy);
            v.key = pendingKey;
            v.tier = tier;
            v.opIndex = fill;
            v.hashIn = std::move(known);
            takePending(pendingKey, v);
            if (v.copy)
                _recorded.insert(pendingKey);
            v.captureNow(_config);
            tasks.push_back(std::move(v));
        };
        // Links to what this transaction removed are captured under the
        // names the objects had (ValueTask::captureNow).
        std::unordered_map<const DocumentObject*, std::string> removedNames;
        for (auto& info : txn._Objects.get<0>()) {
            if (info.second->status == TransactionObject::New && !info.second->_NameInDocument.empty())
                if (auto obj = Base::freecad_dynamic_cast<const DocumentObject>(info.first))
                    removedNames.emplace(obj, info.second->_NameInDocument);
        }
        CaptureNames names(std::move(removedNames));
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
                        a->meta = dynamicMeta(dyn, kv.second);
                    }
                    emit("set", kv.first, typeName);
                    pendOp(*kv.second, c, "durable", false);
                }
                continue;
            }

            if (rec.status == TransactionObject::New) {
                // Removed in this transaction. The object is detached and
                // alive (the transaction holds it), but not for as long as
                // the worker may need it: each value is copied here, the
                // way the undo system copies a changed one, and the copy is
                // what is serialised. Each resolves whatever was pending.
                //
                // The value is the one the transaction began with (sec
                // 27.67). A removal is often the end of a cascade that
                // wrote to the object first -- a copy-on-change link loses
                // its copy, its target and the properties it mirrored
                // before it goes -- and the undo system's copy at the
                // first write is what the object was; the live value is
                // what the cascade left.
                std::map<std::string, Property*> props;
                c.container->getPropertyMap(props);
                // The touched state it goes with (sec 27.58): a removal
                // leaves it alone, so it is the state before.
                auto removed = Base::freecad_dynamic_cast<const DocumentObject>(tobj);
                const int objectBits = rec._objectBitsBefore >= 0 ? rec._objectBitsBefore
                    : removed                                     ? removed->getLogTouchedBits()
                                                                  : -1;
                for (auto& kv : props) {
                    short ptype = c.container->getPropertyType(kv.second);
                    if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                        continue;
                    if (!kv.second->getName())
                        continue;
                    auto it = rec._PropChangeMap.find(kv.second->getID());
                    auto* first = it != rec._PropChangeMap.end() && it->second.property
                            && it->second.propertyType == kv.second->getTypeId()
                        ? &it->second
                        : nullptr;
                    auto o = emit("set", kv.first, kv.second->getTypeId().getName());
                    if (first && first->touchedBefore >= 0)
                        o->touched = first->touchedBefore;
                    else if (objectBits >= 0)
                        o->touched = objectBits
                            | (kv.second->hasTouchedBit() ? DocumentObject::LogPropTouched : 0);
                    // A dynamic property's metadata rides on its set, so a
                    // cold undo can add it back to the recreated object.
                    auto dyn = c.container->getDynamicPropertyData(kv.second);
                    if (!dyn.name.empty())
                        o->meta = dynamicMeta(dyn, first ? first->property : kv.second);
                    if (first) {
                        task(share(*first), "durable", last(), it->first, first->logHash);
                        continue;
                    }
                    std::shared_ptr<const Property> copy(kv.second->Copy());
                    task(std::move(copy), "durable", last(), kv.second->getID());
                }
                // Dynamic properties the cascade removed before the object:
                // gone from it, kept by the transaction, and set like the
                // rest so a cold undo adds them back.
                for (auto& kv : rec._PropChangeMap) {
                    auto& data = kv.second;
                    if (!data.property || data.name.empty())
                        continue;
                    auto prop = data.propertyOrig;
                    const char* name = c.container->getPropertyName(prop);
                    if (name && data.name == name && data.propertyType == prop->getTypeId())
                        continue;   // still there: set above
                    if ((data.attr & Prop_Transient) || (data.attr & Prop_NoPersist))
                        continue;
                    auto o = emit("set", data.name, data.propertyType.getName());
                    if (data.touchedBefore >= 0)
                        o->touched = data.touchedBefore;
                    o->meta = dynamicMeta(data, data.property);
                    task(share(data), "durable", last(), kv.first, data.logHash);
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
                    // Nothing that is not saved is logged: a cache a module
                    // hangs on an object as a dynamic property (Part's
                    // shape cache) is not the document's (sec 30.18).
                    if ((data.attr & Prop_Transient) || (data.attr & Prop_NoPersist))
                        continue;
                    // Dynamic property added: metadata, then its value pending.
                    auto a = emit("addprop", data.name, typeName);
                    const char* name = c.container->getPropertyName(prop);
                    const bool alive = name && data.name == name;
                    a->meta = dynamicMeta(data, alive ? prop : nullptr);
                    if (alive) {
                        emit("set", data.name, typeName);
                        pendOp(*prop, c, "durable", false);
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
                    o->meta = dynamicMeta(data, data.property);
                    task(share(data), "durable", last(), kv.first);
                    continue;
                }
                short ptype = c.container->getPropertyType(prop);
                if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                    continue;

                bool derived = data.derived;
                bool waiting = _pending.count(kv.first) != 0;
                bool same = sameForLog(_config, *data.property, *prop);
                if (!waiting && same)
                    continue;   // a write that changed nothing, sec 9.1
                if (waiting && same) {
                    // Only the earlier op needed this copy (decision 6a).
                    task(share(data), tierFor(derived), -1, kv.first);
                    continue;
                }
                auto o = emit("set", name, typeName);
                o->derived = derived;
                if (!derived)
                    o->touched = data.touchedBefore;
                if (recordsValue(derived) || waiting)
                    task(share(data), tierFor(derived), recordsValue(derived) ? last() : -1,
                         kv.first, data.logHash);
                if (recordsValue(derived))
                    pendOp(*prop, c, tierFor(derived), true);
                else
                    _recorded.erase(kv.first);   // changed, and the value not kept
            }
        }

        if (ops.empty()) {
            // Nothing to record; copies that only resolve earlier ops are
            // still written.
            --_c._nextSeq;
            _head = t.parent;
            _recent.erase(t.seq);
            if (!tasks.empty()) {
                post([this, tasks]() mutable {
                    std::vector<LogOp> none;
                    writeValues(tasks, none);
                });
            }
            return 0;
        }
        // Every after value this commit leaves pending is copied now and
        // written with it (sec 25.4): nothing the commit set lives only in
        // memory once the worker has run the job. FC_TXNLOG_NO_STREAM leaves
        // them pending as before, for measuring what this costs.
        static const bool noStream = std::getenv("FC_TXNLOG_NO_STREAM") != nullptr;
        if (noStream) {
            for (auto& p : newPending)
                _pending[p.first] = p.second;
            newPending.clear();
        }
        for (std::size_t i = 0; i < newPending.size(); ++i) {
            const Property* live = newPendingProps[i].first;
            const Pending& p = newPending[i].second;
            std::shared_ptr<Property> copy(live->Copy());
            copy->setStatusValue(live->getStatus());
            ValueTask v;
            v.copy = copy;
            v.key = newPending[i].first;
            v.tier = p.tier;
            v.afterIndex = p.idx;
            _recorded.insert(v.key);
            v.captureNow(_config);
            if (newPendingProps[i].second && v.copy) {
                v.hashOut = std::make_shared<std::string>();
                TransactionCopyCache::put(
                    v.key, {copy, v.hashOut, this, TransactionCopyCache::statusOf(*live)});
            }
            tasks.push_back(std::move(v));
        }
        remember(t, &ops);
        post([this, t, ops, tasks]() mutable {
            writeValues(tasks, ops);
            _c._store->append(t, ops);
        });
        if (_stamp)
            _stamp->seq = t.seq;
        return t.seq;
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    catch (std::exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    return 0;
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
        if (kv.second.view)
            container = Document::viewOf(_doc.getObjectByID(kv.second.cid));
        else if (kv.second.cid == 0)
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
        v.captureNow(_config);
        tasks.push_back(std::move(v));
    }
    if (tasks.empty())
        return;
    post([this, tasks]() mutable {
        std::vector<LogOp> none;
        writeValues(tasks, none);
    });
}
