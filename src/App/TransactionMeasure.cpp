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
# include <cstdlib>
# include <fstream>
# include <sstream>
# include <vector>
#endif

#include <zlib.h>
#ifdef FC_HAVE_ZSTD
# include <zstd.h>
#endif

#include <Base/Console.h>

#include "TransactionMeasure.h"
#include "TransactionValue.h"
#include "Document.h"
#include "DocumentObject.h"
#include "Property.h"
#include "Transactions.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace {

using Clock = std::chrono::steady_clock;

double microsSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

size_t zlibSize(const std::string& in)
{
    uLongf outLen = compressBound(static_cast<uLong>(in.size()));
    std::vector<Bytef> out(outLen);
    if (compress2(out.data(), &outLen,
                  reinterpret_cast<const Bytef*>(in.data()),
                  static_cast<uLong>(in.size()), 6) != Z_OK)
        return 0;
    return outLen;
}

#ifdef FC_HAVE_ZSTD
size_t zstdSize(const std::string& in, int level)
{
    std::vector<char> out(ZSTD_compressBound(in.size()));
    size_t n = ZSTD_compress(out.data(), out.size(), in.data(), in.size(), level);
    return ZSTD_isError(n) ? 0 : n;
}

/// Size of `after` compressed with `before` as the reference prefix: the
/// generic byte-level delta the design says to try before any per-type
/// codec (sec 9.3). What `zstd --patch-from` does.
size_t zstdPatchSize(const std::string& before, const std::string& after, int level)
{
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    if (!cctx)
        return 0;
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
    // Large windows so the whole reference is addressable.
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 27);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_enableLongDistanceMatching, 1);
    ZSTD_CCtx_refPrefix(cctx, before.data(), before.size());
    std::vector<char> out(ZSTD_compressBound(after.size()));
    size_t n = ZSTD_compress2(cctx, out.data(), out.size(), after.data(), after.size());
    ZSTD_freeCCtx(cctx);
    return ZSTD_isError(n) ? 0 : n;
}
#endif

/** Content-defined chunking, gear hash, target 4 KB chunks.
 *
 * Returns the chunk boundaries as (offset, length) pairs. The per-chunk
 * hash is FNV-1a; good enough to count "chunks of after that were not in
 * before", which is the only question asked of it.
 */
struct Chunk
{
    size_t offset;
    size_t length;
    uint64_t hash;
};

const uint32_t* gearTable()
{
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        uint32_t x = 0x9E3779B9u;
        for (auto& v : table) {
            // xorshift, deterministic
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            v = x;
        }
        init = true;
    }
    return table;
}

std::vector<Chunk> chunk(const std::string& data)
{
    const size_t minLen = 1024, avgMask = 4095, maxLen = 16384;
    const uint32_t* table = gearTable();
    std::vector<Chunk> chunks;
    size_t start = 0;
    uint32_t h = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        h = (h << 1) + table[static_cast<unsigned char>(data[i])];
        size_t len = i + 1 - start;
        if ((len >= minLen && (h & avgMask) == 0) || len >= maxLen || i + 1 == data.size()) {
            uint64_t fnv = 1469598103934665603ull;
            for (size_t k = start; k <= i; ++k) {
                fnv ^= static_cast<unsigned char>(data[k]);
                fnv *= 1099511628211ull;
            }
            chunks.push_back({start, len, fnv});
            start = i + 1;
            h = 0;
        }
    }
    return chunks;
}

std::string csvQuote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

struct TransactionMeasure::Impl
{
    std::ofstream csv;
    std::unordered_set<std::string> seen;
    long seq {0};
    const char* dumpDir {std::getenv("FC_TXN_MEASURE_DUMP")};

    struct Value
    {
        std::string hash;
        std::string bytes;      // canonical
        size_t xmlBytes {0};
        size_t attachBytes {0};
        size_t attachCount {0};
        bool seen {false};
        double tSerialise {0};
        double tHash {0};
        bool ok {false};
    };

    Value capture(const Document& doc, const Base::Persistence& what)
    {
        Value v;
        auto t0 = Clock::now();
        CapturedValue cv = captureValue(doc, what);
        v.ok = cv.ok;
        v.xmlBytes = cv.fragment.size();
        v.attachBytes = cv.attachmentBytes();
        v.attachCount = cv.attachments.size();
        // Every byte in one string, XML first, in a fixed frame so the
        // same value always hashes the same.
        v.bytes = cv.fragment;
        for (auto& a : cv.attachments) {
            v.bytes += '\0';
            v.bytes += a.name;
            v.bytes += '\0';
            v.bytes += a.bytes;
        }
        v.tSerialise = microsSince(t0);

        t0 = Clock::now();
        v.hash = hashBytes(v.bytes);
        v.tHash = microsSince(t0);
        v.seen = !seen.insert(v.hash).second;
        if (dumpDir && !v.seen) {
            // FC_TXN_MEASURE_DUMP=<dir>: keep every distinct value's bytes,
            // named by hash, for looking at what was hashed.
            std::ofstream out(std::string(dumpDir) + "/" + v.hash, std::ios::binary);
            out << v.bytes;
        }
        return v;
    }

    void header()
    {
        csv << "row,seq,txn,name,op,container,object,type,prop,proptype,derived,output,"
               "before_hash,before_bytes,before_xml,before_attach,before_files,before_seen,"
               "after_hash,after_bytes,after_xml,after_attach,after_files,after_seen,"
               "t_ser_us,t_hash_us,"
               "zlib_bytes,t_zlib_us,zstd3_bytes,t_zstd3_us,zstd1_bytes,t_zstd1_us,"
               "patch_bytes,t_patch_us,cdc_chunks,cdc_new_chunks,cdc_new_bytes,t_cdc_us\n";
    }

    void row(const std::string& op, const std::string& container, const std::string& object,
             const std::string& type, const std::string& prop, const std::string& propType,
             bool derived, bool output, const Value* before, const Value* after)
    {
        csv << "op," << seq << ",," << "," << op << ',' << container << ',' << csvQuote(object)
            << ',' << type << ',' << prop << ',' << propType << ',' << (derived ? 1 : 0) << ','
            << (output ? 1 : 0) << ',';
        auto emit = [this](const Value* v) {
            if (!v) {
                csv << ",,,,,,";
                return;
            }
            csv << v->hash << ',' << v->bytes.size() << ',' << v->xmlBytes << ','
                << v->attachBytes << ',' << v->attachCount << ',' << (v->seen ? 1 : 0) << ',';
        };
        emit(before);
        emit(after);
        double tSer = (before ? before->tSerialise : 0) + (after ? after->tSerialise : 0);
        double tHash = (before ? before->tHash : 0) + (after ? after->tHash : 0);
        csv << tSer << ',' << tHash << ',';

        // Compression is measured on the value the op would store new: the
        // after value, or the before value of a remove/delprop.
        const Value* stored = after ? after : before;
        if (stored && !stored->seen && !stored->bytes.empty()) {
            auto t0 = Clock::now();
            size_t z = zlibSize(stored->bytes);
            csv << z << ',' << microsSince(t0) << ',';
#ifdef FC_HAVE_ZSTD
            t0 = Clock::now();
            size_t z3 = zstdSize(stored->bytes, 3);
            csv << z3 << ',' << microsSince(t0) << ',';
            t0 = Clock::now();
            size_t z1 = zstdSize(stored->bytes, 1);
            csv << z1 << ',' << microsSince(t0) << ',';
#else
            csv << ",,,,";
#endif
        }
        else {
            csv << ",,,,,,";
        }

        // The delta, for a set whose two values differ.
        if (before && after && !before->bytes.empty() && before->hash != after->hash) {
#ifdef FC_HAVE_ZSTD
            auto t0 = Clock::now();
            size_t p = zstdPatchSize(before->bytes, after->bytes, 3);
            csv << p << ',' << microsSince(t0) << ',';
#else
            csv << ",,";
#endif
            auto t1 = Clock::now();
            auto cb = chunk(before->bytes);
            auto ca = chunk(after->bytes);
            std::unordered_set<uint64_t> have;
            for (auto& c : cb)
                have.insert(c.hash);
            size_t newChunks = 0, newBytes = 0;
            for (auto& c : ca) {
                if (!have.count(c.hash)) {
                    ++newChunks;
                    newBytes += c.length;
                }
            }
            csv << ca.size() << ',' << newChunks << ',' << newBytes << ',' << microsSince(t1);
        }
        else {
            csv << ",,,,,";
        }
        csv << '\n';
    }
};

TransactionMeasure* TransactionMeasure::_instance = nullptr;

TransactionMeasure::~TransactionMeasure() = default;

bool TransactionMeasure::start(const char* csvPath)
{
    stop();
    auto m = new TransactionMeasure;
    m->_impl = std::make_unique<Impl>();
    m->_impl->csv.open(csvPath, std::ios::out | std::ios::trunc);
    if (!m->_impl->csv) {
        FC_ERR("TransactionMeasure: cannot open " << csvPath);
        delete m;
        return false;
    }
    m->_impl->header();
    _instance = m;
    FC_MSG("TransactionMeasure: writing " << csvPath);
    return true;
}

void TransactionMeasure::stop()
{
    delete _instance;
    _instance = nullptr;
}

void TransactionMeasure::checkEnvironment()
{
    static bool checked = false;
    if (checked)
        return;
    checked = true;
    if (const char* path = std::getenv("FC_TXN_MEASURE")) {
        if (*path)
            start(path);
    }
}

void TransactionMeasure::mark(const char* label)
{
    if (!_instance)
        return;
    _instance->_impl->csv << "mark,," << "," << csvQuote(label ? label : "") << '\n';
    _instance->_impl->csv.flush();
}

void TransactionMeasure::onCommit(Document& doc, const Transaction& txn)
{
    if (!_instance)
        return;
    auto& impl = *_instance->_impl;
    auto tTxn = Clock::now();
    ++impl.seq;

    size_t ops = 0, newBytes = 0, newValues = 0;
    auto account = [&](const Impl::Value& v) {
        if (v.ok && !v.seen) {
            newBytes += v.bytes.size();
            ++newValues;
        }
    };

    for (auto& info : txn._Objects.get<0>()) {
        const TransactionalObject* tobj = info.first;
        const TransactionObject& rec = *info.second;
        const PropertyContainer* container = tobj ? static_cast<const PropertyContainer*>(tobj)
                                                  : static_cast<const PropertyContainer*>(&doc);
        std::string ckind = "doc";
        std::string oname, otype = container->getTypeId().getName();
        if (auto obj = Base::freecad_dynamic_cast<const DocumentObject>(tobj)) {
            ckind = "obj:" + std::to_string(obj->getID());
            oname = obj->getNameInDocument() ? obj->getNameInDocument() : rec._NameInDocument;
        }
        else if (tobj) {
            ckind = "view";
            oname = container->getFullName();
        }

        // Status New means the object was removed in this transaction (undo
        // re-adds it); Del means it was created (undo removes it). The log
        // sees them the other way round.
        if (rec.status == TransactionObject::New) {
            auto v = impl.capture(doc, *container);
            account(v);
            ++ops;
            impl.row("remove", ckind, oname, otype, "", "", false, false, &v, nullptr);
            continue;
        }
        if (rec.status == TransactionObject::Del) {
            auto v = impl.capture(doc, *container);
            account(v);
            ++ops;
            impl.row("create", ckind, oname, otype, "", "", false, false, nullptr, &v);
            continue;
        }

        for (auto& kv : rec._PropChangeMap) {
            auto& data = kv.second;
            auto prop = const_cast<Property*>(data.propertyOrig);
            const char* name = container->getPropertyName(prop);
            if (!data.property) {
                // Dynamic property added: metadata only, no value.
                ++ops;
                impl.row("addprop", ckind, oname, otype, data.name,
                         data.propertyType.getName(), false, false, nullptr, nullptr);
                continue;
            }
            std::string propType = data.propertyType.getName();
            if (!name || (!data.name.empty() && data.name != name)
                    || data.propertyType != prop->getTypeId()) {
                // Original gone: a removed dynamic property.
                auto before = impl.capture(doc, *data.property);
                account(before);
                ++ops;
                impl.row("delprop", ckind, oname, otype, data.name, propType,
                         data.derived, false, &before, nullptr);
                continue;
            }
            short ptype = container->getPropertyType(prop);
            if ((ptype & Prop_Transient) || (ptype & Prop_NoPersist))
                continue;
            bool output = (ptype & Prop_Output) || prop->testStatus(Property::Output);

            auto before = impl.capture(doc, *data.property);
            auto after = impl.capture(doc, *prop);
            // Equal refs: the writer drops the op (sec 9.1). Still rowed so
            // the count of no-op writes is visible; marked by equal hashes.
            account(before);
            account(after);
            ++ops;
            impl.row("set", ckind, oname, otype, name, propType, data.derived, output,
                     &before, &after);
        }
    }

    // Summary row, its own layout: row, seq, txn id, name, ops, new values,
    // new bytes, total microseconds for the walk above.
    impl.csv << "txn," << impl.seq << ',' << txn.getID() << ',' << csvQuote(txn.Name) << ','
             << ops << ',' << newValues << ',' << newBytes << ',' << microsSince(tTxn) << '\n';
    impl.csv.flush();
}
