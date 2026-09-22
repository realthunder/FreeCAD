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
    _environment = _store->environment(env);
    std::string user, host;
    if (DocumentParams::getTransactionLogIdentity()) {
        auto u = config.find("UserName");
        if (u != config.end())
            user = u->second;
        auto h = config.find("HostName");
        if (h != config.end())
            host = h->second;
    }
    _session = _store->openSession(_environment, user, host, now());
    FC_LOG("transaction log " << _path << " session " << _session);
}

void TransactionLog::openStore()
{
    std::string dir = _doc.TransientDir.getStrValue() + "/history";
    Base::FileInfo(dir).createDirectories();
    _path = dir + "/log.db";
    _store = TransactionStore::openSQLite(_path);
    if (_store->getMeta("document").empty())
        _store->setMeta("document", _doc.Uid.getValueStr());
}

void TransactionLog::closeStore()
{
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
        t.parent = _store->lastSeq();
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
        std::vector<LogOp> none;
        _store->append(t, none);
    }
    catch (Base::Exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
    catch (std::exception& e) {
        FC_ERR("transaction log: " << e.what());
    }
}

int64_t TransactionLog::onSave(const std::string& path, const std::string& docXml,
                               const std::vector<std::pair<std::string, std::string>>& blobs,
                               int schema)
{
    return snapshot("save", path, docXml, blobs, schema);
}

int64_t TransactionLog::onRestore(const std::string& path, const std::string& docXml,
                                  const std::vector<std::pair<std::string, std::string>>& blobs,
                                  int schema)
{
    if (_store->lastSeq() != 0 || !_store->versions().empty()) {
        // Sec 16.6's second case, a history the file no longer matches, is
        // the embedded mode's to handle; a session store is always fresh.
        FC_WARN("transaction log of " << _doc.getName() << " is not empty at restore");
        return 0;
    }
    return snapshot("restore", path, docXml, blobs, schema);
}

int64_t TransactionLog::snapshot(const char* kind, const std::string& path,
                                 const std::string& docXml,
                                 const std::vector<std::pair<std::string, std::string>>& blobs,
                                 int schema)
{
    try {
        resolvePending();

        // Document.xml is a value like any other, durable: the version is
        // the one place a whole file is kept (sec 16.1). With no
        // attachments its ref is the SHA-1 of the bytes, which is also the
        // hash a file on disk is matched by.
        CapturedValue xml;
        xml.fragment = docXml;
        xml.ok = true;
        const std::string docHash = putValue(xml, "durable");

        LogVersion v;
        v.uuid = Base::Uuid::createUuid();
        v.seq = _store->lastSeq();
        v.env = _environment;
        v.docxml_hash = docHash;
        v.schema = schema;
        v.created = now();
        std::vector<LogManifestEntry> manifest;
        manifest.push_back({"Document.xml", docHash, "value"});
        for (const auto& b : blobs)
            manifest.push_back({b.first, b.second, "blob"});
        _store->addVersion(v, manifest);

        LogTransaction t;
        t.parent = v.seq;
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
        t.script = "{\"version\":" + std::to_string(v.num) + ",\"docxml\":\"" + docHash
                 + "\",\"blobs\":" + std::to_string(blobs.size()) + ",\"schema\":"
                 + std::to_string(schema) + ",\"path\":\"" + escaped + "\"}";
        std::vector<LogOp> none;
        _store->append(t, none);
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

void TransactionLog::resolve(int64_t key, const std::string& hash)
{
    auto it = _pending.find(key);
    if (it == _pending.end())
        return;
    _store->resolveAfter(it->second.txn, it->second.idx, hash);
    _pending.erase(it);
}

void TransactionLog::resolve(const Property& prop, const std::string& hash)
{
    resolve(prop.getID(), hash);
}

void TransactionLog::onCommit(const Transaction& txn, const char* kind, const char* origin)
{
    if (txn.isEmpty())
        return;
    try {
        LogTransaction t;
        t.parent = _store->lastSeq();
        t.id = txn.getID();
        t.kind = kind;
        t.origin = origin;
        t.name = txn.Name;
        t.time = now();
        t.session = _session;

        std::vector<LogOp> ops;
        // Ops whose after ref is pending, by property id, resolved to
        // (txn, idx) once append() has numbered them.
        std::vector<std::pair<int64_t, Pending>> newPending;
        auto pendOp = [&](const Property& prop, const ContainerInfo& c, const char* tier) {
            Pending p;
            p.txn = 0;
            p.idx = static_cast<int>(ops.size()) - 1;   // the op just emitted
            p.cid = c.cid;
            p.prop = ops.back().prop;
            p.tier = tier;
            newPending.emplace_back(prop.getID(), p);
        };

        for (auto& info : txn._Objects.get<0>()) {
            const TransactionalObject* tobj = info.first;
            const TransactionObject& rec = *info.second;
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
                // alive (the transaction holds it), so its values can be
                // serialised now; each resolves whatever was pending on it.
                std::map<std::string, Property*> props;
                c.container->getPropertyMap(props);
                for (auto& kv : props) {
                    short ptype = c.container->getPropertyType(kv.second);
                    if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                        continue;
                    if (!kv.second->getName())
                        continue;
                    CapturedValue cv = captureValue(_doc, *kv.second);
                    std::string hash = cv.ok ? putValue(cv, "durable") : std::string();
                    resolve(*kv.second, hash);
                    auto o = emit("set", kv.first, kv.second->getTypeId().getName());
                    o->vbefore = hash;
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
                    CapturedValue cv = captureValue(_doc, *data.property);
                    std::string hash = cv.ok ? putValue(cv, "durable") : std::string();
                    resolve(kv.first, hash);
                    auto o = emit("delprop", data.name, typeName);
                    o->meta = dynamicMeta(data);
                    o->vbefore = hash;
                    continue;
                }
                short ptype = c.container->getPropertyType(prop);
                if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                    continue;

                bool derived = data.derived;
                bool waiting = _pending.count(kv.first) != 0;
                if (!waiting && data.property->isSame(*prop))
                    continue;   // a write that changed nothing, sec 9.1
                std::string before;
                if (recordsValue(derived) || waiting) {
                    CapturedValue cv = captureValue(_doc, *data.property);
                    if (cv.ok)
                        before = putValue(cv, tierFor(derived));
                    resolve(kv.first, before);
                }
                if (waiting && data.property->isSame(*prop))
                    continue;   // only the earlier op needed this copy
                auto o = emit("set", name, typeName);
                o->derived = derived;
                o->vbefore = before;
                if (recordsValue(derived))
                    pendOp(*prop, c, tierFor(derived));
            }
        }

        if (ops.empty())
            return;
        _store->append(t, ops);
        for (auto& p : newPending) {
            p.second.txn = t.seq;
            _pending[p.first] = p.second;
        }
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
    // Snapshot the live values behind the pending refs. Looked up by
    // container id and name rather than through the property pointer,
    // which may be gone without a remove op if undo was off for a while.
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
        CapturedValue cv = captureValue(_doc, *prop);
        std::string hash = cv.ok ? putValue(cv, kv.second.tier) : std::string();
        resolve(kv.first, hash);
    }
}
