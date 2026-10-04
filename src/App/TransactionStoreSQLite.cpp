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
# include <array>
# include <cstring>
# include <map>
# include <set>
# include <sstream>
# include <unordered_map>
# include <unordered_set>
#endif

#include <sqlite3.h>

#ifdef FC_HAVE_ZSTD
# include <zstd.h>
#endif

#include <Base/Console.h>
#include <Base/Exception.h>

#include "TransactionLog.h"
#include "TransactionStore.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace {

// docs/TransactionLog.md sec 27.53: an entity's hash, its base and both
// ends of an edge are the SHA-1's 20 bytes, and `ref` is its key.
#define FC_ENTITY_COLUMNS                                                              \
    "(hash BLOB PRIMARY KEY, kind TEXT, enc TEXT, base BLOB, tier TEXT,"         \
    " size INTEGER, data BLOB)"
#define FC_REF_COLUMNS                                                                 \
    "(entity BLOB, target BLOB, role TEXT, name TEXT, seq INTEGER,"                 \
    " PRIMARY KEY(entity, role, name, target)) WITHOUT ROWID"

int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

/// A hash as the store keeps it: 40 lowercase hex digits become their 20
/// bytes; anything else -- empty, or not a SHA-1 -- stays as it is, text,
/// and reads back unchanged.
bool packHash(const char* hex, size_t n, unsigned char* out)
{
    if (n != 40)
        return false;
    for (size_t i = 0; i < 20; ++i) {
        int hi = hexDigit(hex[2 * i]);
        int lo = hexDigit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = static_cast<unsigned char>(hi << 4 | lo);
    }
    return true;
}

/** The SQLite log (docs/TransactionLog.md sec 13). One database per
 * document history; every append is one SQL transaction, so atomicity
 * and crash recovery are the database's, not ours.
 */
class SQLiteStore : public TransactionStore
{
public:
    explicit SQLiteStore(const std::string& path)
    {
        if (sqlite3_open_v2(path.c_str(), &db,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
            std::string msg = db ? sqlite3_errmsg(db) : "cannot allocate";
            if (db)
                sqlite3_close(db);
            db = nullptr;
            throw Base::RuntimeError("Cannot open transaction log " + path + ": " + msg);
        }
        // Read-only -- the embedded copy as the guard reads it (sec 16.4),
        // a blob file -- is read as it is: no journal mode set, no table
        // made. The store the log adopts is a writable copy.
        if (sqlite3_db_readonly(db, "main") == 1)
            return;
        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA synchronous=NORMAL");
        // One layout, no schema number and no migration: the log has shipped
        // in no release (user, 2026-09-28, sec 27.55).
        exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)");
        exec("CREATE TABLE IF NOT EXISTS txn(seq INTEGER PRIMARY KEY, parent INTEGER, id INTEGER,"
             " kind TEXT, origin TEXT, name TEXT, time REAL, script TEXT, session INTEGER,"
             " inverts INTEGER DEFAULT 0, branch INTEGER DEFAULT 1,"
             " merge_from INTEGER DEFAULT 0)");
        exec("CREATE TABLE IF NOT EXISTS environment(id INTEGER PRIMARY KEY, json TEXT UNIQUE)");
        // Sec 30.6: a session is one login of one user.
        exec("CREATE TABLE IF NOT EXISTS user(id INTEGER PRIMARY KEY, kind TEXT, name TEXT,"
             " UNIQUE(kind, name))");
        exec("CREATE TABLE IF NOT EXISTS session(id INTEGER PRIMARY KEY, env INTEGER,"
             " user INTEGER, host TEXT, access TEXT, opened REAL, closed REAL)");
        exec("CREATE TABLE IF NOT EXISTS op(txn INTEGER, idx INTEGER, op TEXT, ckind TEXT,"
             " cid INTEGER, cname TEXT, ctype TEXT, prop TEXT, ptype TEXT, meta TEXT,"
             " vbefore TEXT, vafter TEXT, derived INTEGER, touched INTEGER, PRIMARY KEY(txn, idx))");
        exec("CREATE INDEX IF NOT EXISTS op_container ON op(cid, prop)");
        // `manifest` names the version's entry list, an entity of kind
        // `manifest` (sec 27.54).
        exec("CREATE TABLE IF NOT EXISTS version(num INTEGER PRIMARY KEY, uuid TEXT, branch INTEGER,"
             " kind TEXT, name TEXT, seq INTEGER, env INTEGER, docxml_hash TEXT, schema INTEGER,"
             " created REAL, manifest BLOB)");
        exec("CREATE INDEX IF NOT EXISTS version_hash ON version(docxml_hash)");
        // Sec 27.40 item 3: the file's object names, one to one.
        exec("CREATE TABLE IF NOT EXISTS objname(cid INTEGER PRIMARY KEY, name TEXT UNIQUE)");
        // Item 4: the last geometry id of each object.
        exec("CREATE TABLE IF NOT EXISTS lastgeoid(cid INTEGER PRIMARY KEY, id INTEGER)");
        // Sec 27.50 item 2: the file's string table, kept once for every
        // version; item 4: the ids each version and value uses, as ranges.
        exec("CREATE TABLE IF NOT EXISTS strtable(id INTEGER PRIMARY KEY, flags INTEGER,"
             " sids TEXT, data BLOB, postfix BLOB)");
        // One row per owner, its ranges packed (packRanges): a recomputed
        // shape's ids come in hundreds of short runs.
        exec("CREATE TABLE IF NOT EXISTS strref(owner BLOB PRIMARY KEY, ranges BLOB)"
             " WITHOUT ROWID");
        exec("CREATE TABLE IF NOT EXISTS entity" FC_ENTITY_COLUMNS);
        exec("CREATE TABLE IF NOT EXISTS ref" FC_REF_COLUMNS);
        exec("CREATE INDEX IF NOT EXISTS ref_target ON ref(target, role)");
        // Sec 26: branches; `main` is id 1.
        exec("CREATE TABLE IF NOT EXISTS branch(id INTEGER PRIMARY KEY, name TEXT UNIQUE,"
             " from_version INTEGER, from_seq INTEGER, head_seq INTEGER, id_base INTEGER,"
             " last_id INTEGER DEFAULT 0, created REAL, closed REAL,"
             " target INTEGER DEFAULT 0)");
        if (!hasRow("SELECT 1 FROM branch WHERE id=1"))
            exec("INSERT INTO branch(id,name,from_version,from_seq,head_seq,id_base,created,"
                 "closed) VALUES(1,'main',0,0,(SELECT COALESCE(MAX(seq),0) FROM txn),0,"
                 "(SELECT COALESCE(MIN(time),0) FROM txn),0)");
    }

    bool hasRow(const char* sql)
    {
        auto s = prepare(sql);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        sqlite3_reset(s);
        return found;
    }

    /** A version's entry list as an entity (sec 27.54): one line per entry,
     * `<hash> <entry>`, by entry name, raw or zstd as any entity; the log
     * supersedes the previous version's with it, so that one is stored as
     * a delta (23.2). What it names is held by the collector through it.
     * Inside the caller's transaction; the hash.
     */
    std::string putManifest(std::vector<LogManifestEntry> entries)
    {
        std::stable_sort(entries.begin(), entries.end(),
                         [](const LogManifestEntry& a, const LogManifestEntry& b) {
                             return a.entry < b.entry;
                         });
        std::string bytes;
        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            // One line per name, the last given.
            if (i + 1 < entries.size() && entries[i + 1].entry == e.entry)
                continue;
            bytes += e.hash;
            bytes += ' ';
            bytes += e.entry;
            bytes += '\n';
        }
        const std::string hash = hashBytes(bytes);
        std::string enc = "raw";
        std::string data = bytes;
#ifdef FC_HAVE_ZSTD
        if (bytes.size() > 128) {
            data.resize(ZSTD_compressBound(bytes.size()));
            size_t n = ZSTD_compress(data.data(), data.size(), bytes.data(), bytes.size(), 3);
            if (!ZSTD_isError(n) && n < bytes.size()) {
                data.resize(n);
                enc = "zstd";
            }
            else {
                data = bytes;
            }
        }
#endif
        auto s = prepare("INSERT OR IGNORE INTO entity(hash,kind,enc,base,tier,size,data)"
                         " VALUES(?,'manifest',?,'','durable',?,?)");
        bindHash(s, 1, hash);
        bindText(s, 2, enc);
        sqlite3_bind_int64(s, 3, static_cast<sqlite3_int64>(bytes.size()));
        sqlite3_bind_blob(s, 4, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
        step(s);
        return hash;
    }

    /// The entries of a manifest entity, as putManifest() wrote them.
    bool readManifest(const std::string& hash, std::vector<LogManifestEntry>& out)
    {
        out.clear();
        std::string bytes;
        Decoded decoded;
        if (!readStored(hash, bytes, decoded))
            return false;
        size_t pos = 0;
        while (pos < bytes.size()) {
            size_t end = bytes.find('\n', pos);
            if (end == std::string::npos)
                end = bytes.size();
            const size_t space = bytes.find(' ', pos);
            if (space == std::string::npos || space > end)
                return false;
            LogManifestEntry e;
            e.hash = bytes.substr(pos, space - pos);
            e.entry = bytes.substr(space + 1, end - space - 1);
            out.push_back(std::move(e));
            pos = end + 1;
        }
        return true;
    }

    ~SQLiteStore() override
    {
        for (auto& s : stmts)
            sqlite3_finalize(s.second);
        if (db)
            sqlite3_close(db);
    }

    int64_t append(LogTransaction& txn, std::vector<LogOp>& ops) override
    {
        exec("BEGIN");
        try {
            // A preset seq is honoured (the writer thread's caller numbers
            // ahead, TransactionLog); NULL takes the next rowid.
            auto ins = prepare("INSERT INTO txn(seq,parent,id,kind,origin,name,time,script,session,"
                               "inverts,branch,merge_from) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
            if (txn.seq > 0)
                sqlite3_bind_int64(ins, 1, txn.seq);
            else
                sqlite3_bind_null(ins, 1);
            sqlite3_bind_int64(ins, 2, txn.parent);
            sqlite3_bind_int(ins, 3, txn.id);
            bindText(ins, 4, txn.kind);
            bindText(ins, 5, txn.origin);
            bindText(ins, 6, txn.name);
            sqlite3_bind_double(ins, 7, txn.time);
            bindText(ins, 8, txn.script);
            sqlite3_bind_int64(ins, 9, txn.session);
            sqlite3_bind_int64(ins, 10, txn.inverts);
            sqlite3_bind_int64(ins, 11, txn.branch);
            sqlite3_bind_int64(ins, 12, txn.mergeFrom);
            step(ins);
            txn.seq = sqlite3_last_insert_rowid(db);
            auto head = prepare("UPDATE branch SET head_seq=? WHERE id=?");
            sqlite3_bind_int64(head, 1, txn.seq);
            sqlite3_bind_int64(head, 2, txn.branch);
            step(head);

            auto op = prepare("INSERT INTO op(txn,idx,op,ckind,cid,cname,ctype,prop,ptype,meta,"
                              "vbefore,vafter,derived,touched) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            int idx = 0;
            for (auto& o : ops) {
                o.txn = txn.seq;
                o.idx = idx++;
                sqlite3_reset(op);
                sqlite3_bind_int64(op, 1, o.txn);
                sqlite3_bind_int(op, 2, o.idx);
                bindText(op, 3, o.op);
                bindText(op, 4, o.ckind);
                sqlite3_bind_int64(op, 5, o.cid);
                bindText(op, 6, o.cname);
                bindText(op, 7, o.ctype);
                bindText(op, 8, o.prop);
                bindText(op, 9, o.ptype);
                bindText(op, 10, o.meta);
                bindText(op, 11, o.vbefore);
                bindText(op, 12, o.vafter);
                sqlite3_bind_int(op, 13, o.derived ? 1 : 0);
                if (o.touched < 0)
                    sqlite3_bind_null(op, 14);
                else
                    sqlite3_bind_int(op, 14, o.touched);
                step(op);
            }
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
        return txn.seq;
    }

    void resolveAfter(int64_t txn, int idx, const std::string& hash) override
    {
        auto s = prepare("UPDATE op SET vafter=? WHERE txn=? AND idx=?");
        bindText(s, 1, hash);
        sqlite3_bind_int64(s, 2, txn);
        sqlite3_bind_int(s, 3, idx);
        step(s);
    }

    bool hasEntity(const std::string& hash) override
    {
        auto s = prepare("SELECT 1 FROM entity WHERE hash=?");
        bindHash(s, 1, hash);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        sqlite3_reset(s);
        return found;
    }

    void putEntity(const LogEntity& e) override
    {
        exec("BEGIN");
        try {
            auto s = prepare("INSERT OR IGNORE INTO entity(hash,kind,enc,base,tier,size,data)"
                             " VALUES(?,?,?,?,?,?,?)");
            bindHash(s, 1, e.hash);
            bindText(s, 2, e.kind);
            bindText(s, 3, e.enc);
            bindHash(s, 4, e.base);
            bindText(s, 5, e.tier);
            sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(e.size));
            sqlite3_bind_blob(s, 7, e.data.data(), static_cast<int>(e.data.size()),
                              SQLITE_TRANSIENT);
            step(s);
            // A hash already there keeps its refs too: same content, same
            // edges. Only a new row writes them.
            if (sqlite3_changes(db) > 0) {
                int seq = 0;
                for (const auto& r : e.refs)
                    insertRef(e.hash, r, seq++);
                if (e.enc == "delta" && !e.base.empty())
                    insertRef(e.hash, LogRef {e.base, "base", ""}, seq++);
            }
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    bool getEntity(const std::string& hash, LogEntity& e) override
    {
        auto s = prepare("SELECT kind,enc,base,tier,size,data FROM entity WHERE hash=?");
        bindHash(s, 1, hash);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_reset(s);
            return false;
        }
        e.hash = hash;
        e.kind = text(s, 0);
        e.enc = text(s, 1);
        e.base = columnHash(s, 2);
        e.tier = text(s, 3);
        e.size = static_cast<uint64_t>(sqlite3_column_int64(s, 4));
        const void* blob = sqlite3_column_blob(s, 5);
        int n = sqlite3_column_bytes(s, 5);
        e.data.assign(static_cast<const char*>(blob), blob ? n : 0);
        sqlite3_reset(s);
        e.refs.clear();
        auto r = prepare("SELECT target,role,name FROM ref WHERE entity=? AND role<>'base'"
                         " ORDER BY seq");
        bindHash(r, 1, hash);
        while (sqlite3_step(r) == SQLITE_ROW)
            e.refs.push_back(LogRef {columnHash(r, 0), text(r, 1), text(r, 2)});
        sqlite3_reset(r);
        return true;
    }

    void reencodeEntity(const std::string& hash, const std::string& enc,
                        const std::string& base, const std::string& data) override
    {
        exec("BEGIN");
        try {
            // Delete and insert, not update (sec 27.54, F6): a row shrunk
            // in place leaves its leaf page mostly empty, and SQLite merges
            // a leaf only under a third full; the new row goes with the
            // other new ones at the end.
            auto s = prepare("SELECT kind,tier,size FROM entity WHERE hash=?");
            bindHash(s, 1, hash);
            if (sqlite3_step(s) != SQLITE_ROW) {
                sqlite3_reset(s);
                exec("COMMIT");
                return;
            }
            const std::string kind = text(s, 0);
            const std::string tier = text(s, 1);
            const sqlite3_int64 size = sqlite3_column_int64(s, 2);
            sqlite3_reset(s);
            s = prepare("DELETE FROM entity WHERE hash=?");
            bindHash(s, 1, hash);
            step(s);
            s = prepare("INSERT INTO entity(hash,kind,enc,base,tier,size,data)"
                        " VALUES(?,?,?,?,?,?,?)");
            bindHash(s, 1, hash);
            bindText(s, 2, kind);
            bindText(s, 3, enc);
            bindHash(s, 4, base);
            bindText(s, 5, tier);
            sqlite3_bind_int64(s, 6, size);
            sqlite3_bind_blob(s, 7, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
            step(s);
            s = prepare("DELETE FROM ref WHERE entity=? AND role='base'");
            bindHash(s, 1, hash);
            step(s);
            if (enc == "delta" && !base.empty())
                insertRef(hash, LogRef {base, "base", ""}, 1 << 30);
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    void addRef(const std::string& entity, const LogRef& r) override
    {
        auto s = prepare("SELECT COALESCE(MAX(seq),-1)+1 FROM ref WHERE entity=? AND role<>'base'");
        bindHash(s, 1, entity);
        int seq = 0;
        if (sqlite3_step(s) == SQLITE_ROW)
            seq = sqlite3_column_int(s, 0);
        sqlite3_reset(s);
        insertRef(entity, r, seq);
    }

    std::vector<std::string> entitiesStoredAs(const std::string& enc) override
    {
        auto s = prepare("SELECT hash FROM entity WHERE enc=? ORDER BY hash");
        bindText(s, 1, enc);
        std::vector<std::string> out;
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(columnHash(s, 0));
        sqlite3_reset(s);
        return out;
    }

    std::vector<std::string> basedOn(const std::string& hash) override
    {
        auto s = prepare("SELECT entity FROM ref WHERE target=? AND role='base'");
        bindHash(s, 1, hash);
        std::vector<std::string> out;
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(columnHash(s, 0));
        sqlite3_reset(s);
        return out;
    }

    static LogTransaction readTransaction(sqlite3_stmt* s)
    {
        LogTransaction t;
        t.seq = sqlite3_column_int64(s, 0);
        t.parent = sqlite3_column_int64(s, 1);
        t.id = sqlite3_column_int(s, 2);
        t.kind = text(s, 3);
        t.origin = text(s, 4);
        t.name = text(s, 5);
        t.time = sqlite3_column_double(s, 6);
        t.script = text(s, 7);
        t.session = sqlite3_column_int64(s, 8);
        t.inverts = sqlite3_column_int64(s, 9);
        t.branch = sqlite3_column_int64(s, 10);
        t.mergeFrom = sqlite3_column_int64(s, 11);
        return t;
    }

    std::vector<LogTransaction> transactions(int64_t from, int limit) override
    {
        auto s = prepare("SELECT seq,parent,id,kind,origin,name,time,script,session,inverts,branch,"
                         "merge_from FROM txn WHERE seq>=? ORDER BY seq LIMIT ?");
        sqlite3_bind_int64(s, 1, from);
        sqlite3_bind_int(s, 2, limit > 0 ? limit : -1);
        std::vector<LogTransaction> out;
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(readTransaction(s));
        sqlite3_reset(s);
        return out;
    }

    // The parent chain from a head, as a CTE the queries below share: the
    // head, then each row's parent, down to `from`.
#define FC_TXN_CHAIN                                                                   \
    "WITH RECURSIVE chain(seq) AS (SELECT ?1 UNION ALL SELECT t.parent FROM txn t"     \
    " JOIN chain c ON t.seq=c.seq WHERE t.parent>0 AND t.parent>=?2) "

    std::vector<LogTransaction> chain(int64_t head, int64_t from) override
    {
        auto s = prepare(FC_TXN_CHAIN
                         "SELECT seq,parent,id,kind,origin,name,time,script,session,inverts,"
                         "branch,merge_from FROM txn WHERE seq IN (SELECT seq FROM chain)"
                         " AND seq>=?2 ORDER BY seq");
        sqlite3_bind_int64(s, 1, head);
        sqlite3_bind_int64(s, 2, from);
        std::vector<LogTransaction> out;
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(readTransaction(s));
        sqlite3_reset(s);
        return out;
    }

    std::vector<LogTransaction> history(int64_t head) override
    {
        // Both edges of each row reached, one recursive term: `e.k` picks
        // the edge. UNION, not UNION ALL -- a merge and the branch it took
        // reach the fork twice.
        auto s = prepare("WITH RECURSIVE hist(seq) AS (SELECT ?1 UNION"
                         " SELECT CASE e.k WHEN 0 THEN t.parent ELSE t.merge_from END"
                         " FROM txn t JOIN hist h ON t.seq=h.seq,"
                         " (SELECT 0 AS k UNION ALL SELECT 1) e"
                         " WHERE CASE e.k WHEN 0 THEN t.parent ELSE t.merge_from END>0) "
                         "SELECT seq,parent,id,kind,origin,name,time,script,session,inverts,"
                         "branch,merge_from FROM txn WHERE seq IN (SELECT seq FROM hist)"
                         " ORDER BY seq");
        sqlite3_bind_int64(s, 1, head);
        std::vector<LogTransaction> out;
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(readTransaction(s));
        sqlite3_reset(s);
        return out;
    }

    std::vector<LogOp> ops(int64_t txn) override
    {
        auto s = prepare("SELECT idx,op,ckind,cid,cname,ctype,prop,ptype,meta,vbefore,vafter,"
                         "derived,touched FROM op WHERE txn=? ORDER BY idx");
        sqlite3_bind_int64(s, 1, txn);
        std::vector<LogOp> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogOp o;
            o.txn = txn;
            o.idx = sqlite3_column_int(s, 0);
            o.op = text(s, 1);
            o.ckind = text(s, 2);
            o.cid = static_cast<long>(sqlite3_column_int64(s, 3));
            o.cname = text(s, 4);
            o.ctype = text(s, 5);
            o.prop = text(s, 6);
            o.ptype = text(s, 7);
            o.meta = text(s, 8);
            o.vbefore = text(s, 9);
            o.vafter = text(s, 10);
            o.derived = sqlite3_column_int(s, 11) != 0;
            o.touched = sqlite3_column_type(s, 12) == SQLITE_NULL ? -1 : sqlite3_column_int(s, 12);
            out.push_back(std::move(o));
        }
        sqlite3_reset(s);
        return out;
    }

    bool lastOpOn(const std::string& ckind, long cid, const std::string& prop, int64_t after,
                  int64_t head, LogOp& o) override
    {
        sqlite3_stmt* s = nullptr;
        if (head > 0) {
            s = prepare(FC_TXN_CHAIN
                        "SELECT txn,idx FROM op WHERE cid=?3 AND prop=?4 AND ckind=?5 AND txn>=?2"
                        " AND txn IN (SELECT seq FROM chain) ORDER BY txn DESC, idx DESC LIMIT 1");
            sqlite3_bind_int64(s, 1, head);
            sqlite3_bind_int64(s, 2, after + 1);
        }
        else {
            s = prepare("SELECT txn,idx FROM op WHERE cid=?3 AND prop=?4 AND ckind=?5 AND txn>=?2"
                        " ORDER BY txn DESC, idx DESC LIMIT 1");
            sqlite3_bind_int64(s, 2, after + 1);
        }
        sqlite3_bind_int64(s, 3, cid);
        bindText(s, 4, prop);
        bindText(s, 5, ckind);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_reset(s);
            return false;
        }
        int64_t txn = sqlite3_column_int64(s, 0);
        int idx = sqlite3_column_int(s, 1);
        sqlite3_reset(s);
        return getOp(txn, idx, o);
    }

    bool getOp(int64_t txn, int idx, LogOp& o) override
    {
        auto s = prepare("SELECT op,ckind,cid,cname,ctype,prop,ptype,meta,vbefore,vafter,derived,"
                         "touched FROM op WHERE txn=? AND idx=?");
        sqlite3_bind_int64(s, 1, txn);
        sqlite3_bind_int(s, 2, idx);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_reset(s);
            return false;
        }
        o.txn = txn;
        o.idx = idx;
        o.op = text(s, 0);
        o.ckind = text(s, 1);
        o.cid = static_cast<long>(sqlite3_column_int64(s, 2));
        o.cname = text(s, 3);
        o.ctype = text(s, 4);
        o.prop = text(s, 5);
        o.ptype = text(s, 6);
        o.meta = text(s, 7);
        o.vbefore = text(s, 8);
        o.vafter = text(s, 9);
        o.derived = sqlite3_column_int(s, 10) != 0;
        o.touched = sqlite3_column_type(s, 11) == SQLITE_NULL ? -1 : sqlite3_column_int(s, 11);
        sqlite3_reset(s);
        return true;
    }

    int64_t lastSeq() override
    {
        auto s = prepare("SELECT MAX(seq) FROM txn");
        int64_t seq = 0;
        if (sqlite3_step(s) == SQLITE_ROW)
            seq = sqlite3_column_int64(s, 0);
        sqlite3_reset(s);
        return seq;
    }

    long maxObjectId() override
    {
        auto s = prepare("SELECT MAX(cid) FROM op");
        long id = 0;
        if (sqlite3_step(s) == SQLITE_ROW)
            id = static_cast<long>(sqlite3_column_int64(s, 0));
        sqlite3_reset(s);
        return id;
    }

    std::vector<std::pair<long, std::string>> objectNames() override
    {
        std::vector<std::pair<long, std::string>> out;
        auto read = [&](sqlite3_stmt* s) {
            while (sqlite3_step(s) == SQLITE_ROW)
                out.emplace_back(static_cast<long>(sqlite3_column_int64(s, 0)), text(s, 1));
            sqlite3_reset(s);
        };
        read(prepare("SELECT cid, name FROM objname"));
        read(prepare("SELECT cid, cname FROM op WHERE op='create' AND ckind='obj'"
                     " AND cname<>'' ORDER BY txn DESC, idx DESC"));
        return out;
    }

    void addObjectNames(const std::vector<std::pair<long, std::string>>& names) override
    {
        exec("BEGIN");
        auto s = prepare("INSERT OR IGNORE INTO objname(cid, name) VALUES(?, ?)");
        for (const auto& n : names) {
            sqlite3_bind_int64(s, 1, n.first);
            bindText(s, 2, n.second);
            sqlite3_step(s);
            sqlite3_reset(s);
        }
        exec("COMMIT");
    }

    std::vector<std::pair<long, long>> lastGeoIds() override
    {
        std::vector<std::pair<long, long>> out;
        auto s = prepare("SELECT cid, id FROM lastgeoid");
        while (sqlite3_step(s) == SQLITE_ROW)
            out.emplace_back(static_cast<long>(sqlite3_column_int64(s, 0)),
                             static_cast<long>(sqlite3_column_int64(s, 1)));
        sqlite3_reset(s);
        return out;
    }

    void addLastGeoIds(const std::vector<std::pair<long, long>>& ids) override
    {
        exec("BEGIN");
        auto s = prepare("INSERT INTO lastgeoid(cid, id) VALUES(?, ?)"
                         " ON CONFLICT(cid) DO UPDATE SET id=MAX(id, excluded.id)");
        for (const auto& n : ids) {
            sqlite3_bind_int64(s, 1, n.first);
            sqlite3_bind_int64(s, 2, n.second);
            sqlite3_step(s);
            sqlite3_reset(s);
        }
        exec("COMMIT");
    }

    std::vector<long> objectIdsInOps() override
    {
        std::vector<long> out;
        auto s = prepare("SELECT DISTINCT cid FROM op WHERE ckind='obj'");
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(static_cast<long>(sqlite3_column_int64(s, 0)));
        sqlite3_reset(s);
        return out;
    }

    void removeObjectState(const std::vector<long>& ids) override
    {
        exec("BEGIN");
        auto names = prepare("DELETE FROM objname WHERE cid=?");
        auto geo = prepare("DELETE FROM lastgeoid WHERE cid=?");
        for (long id : ids) {
            for (auto s : {names, geo}) {
                sqlite3_bind_int64(s, 1, id);
                sqlite3_step(s);
                sqlite3_reset(s);
            }
        }
        exec("COMMIT");
    }

    void addStrings(const std::vector<LogString>& strings) override
    {
        if (strings.empty())
            return;
        exec("BEGIN");
        auto s = prepare("INSERT OR IGNORE INTO strtable(id, flags, sids, data, postfix)"
                         " VALUES(?, ?, ?, ?, ?)");
        for (const auto& str : strings) {
            sqlite3_bind_int64(s, 1, str.id);
            sqlite3_bind_int(s, 2, str.flags);
            bindText(s, 3, str.sids);
            sqlite3_bind_blob(s, 4, str.data.data(), static_cast<int>(str.data.size()),
                              SQLITE_TRANSIENT);
            sqlite3_bind_blob(s, 5, str.postfix.data(), static_cast<int>(str.postfix.size()),
                              SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_reset(s);
        }
        exec("COMMIT");
    }

    std::vector<LogString> strings(const std::vector<long>& ids) override
    {
        std::vector<LogString> out;
        auto blob = [](sqlite3_stmt* s, int col) {
            const void* p = sqlite3_column_blob(s, col);
            const int n = sqlite3_column_bytes(s, col);
            return p ? std::string(static_cast<const char*>(p), static_cast<std::size_t>(n))
                     : std::string();
        };
        auto read = [&](sqlite3_stmt* s) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                LogString str;
                str.id = static_cast<long>(sqlite3_column_int64(s, 0));
                str.flags = sqlite3_column_int(s, 1);
                str.sids = text(s, 2);
                str.data = blob(s, 3);
                str.postfix = blob(s, 4);
                out.push_back(std::move(str));
            }
            sqlite3_reset(s);
        };
        if (ids.empty()) {
            read(prepare("SELECT id, flags, sids, data, postfix FROM strtable ORDER BY id"));
            return out;
        }
        std::vector<long> sorted(ids);
        std::sort(sorted.begin(), sorted.end());
        auto s = prepare("SELECT id, flags, sids, data, postfix FROM strtable WHERE id=?");
        for (long id : sorted) {
            sqlite3_bind_int64(s, 1, id);
            read(s);
        }
        return out;
    }

    std::vector<long> stringIds() override
    {
        std::vector<long> out;
        auto s = prepare("SELECT id FROM strtable ORDER BY id");
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(static_cast<long>(sqlite3_column_int64(s, 0)));
        sqlite3_reset(s);
        return out;
    }

    void removeStrings(const std::vector<long>& ids) override
    {
        if (ids.empty())
            return;
        exec("BEGIN");
        auto s = prepare("DELETE FROM strtable WHERE id=?");
        for (long id : ids) {
            sqlite3_bind_int64(s, 1, id);
            sqlite3_step(s);
            sqlite3_reset(s);
        }
        exec("COMMIT");
    }

    void clearStrings() override
    {
        exec("DELETE FROM strtable");
    }

    void addStringRefs(const std::string& owner,
                       const std::vector<std::pair<long, long>>& ranges) override
    {
        if (owner.empty() || ranges.empty())
            return;
        // An owner's ids are its content's: the same hash, the same ranges.
        const std::string packed = packRanges(ranges);
        auto s = prepare("INSERT OR IGNORE INTO strref(owner, ranges) VALUES(?, ?)");
        bindHash(s, 1, owner);
        sqlite3_bind_blob(s, 2, packed.data(), static_cast<int>(packed.size()), SQLITE_TRANSIENT);
        sqlite3_step(s);
        sqlite3_reset(s);
    }

    /// Ranges as unsigned LEB128 pairs: the gap from the previous range's
    /// end, then the length less one.
    static std::string packRanges(const std::vector<std::pair<long, long>>& ranges)
    {
        std::string out;
        auto put = [&out](uint64_t v) {
            do {
                unsigned char b = v & 0x7f;
                v >>= 7;
                out += static_cast<char>(v ? b | 0x80 : b);
            } while (v);
        };
        long last = 0;
        for (const auto& r : ranges) {
            put(static_cast<uint64_t>(r.first - last));
            put(static_cast<uint64_t>(r.second - r.first));
            last = r.second;
        }
        return out;
    }

    static void unpackRanges(const unsigned char* p, int n,
                             std::vector<std::pair<long, long>>& out)
    {
        const unsigned char* end = p + n;
        auto get = [&p, end](uint64_t& v) {
            v = 0;
            for (int shift = 0; p < end && shift < 64; shift += 7) {
                const unsigned char b = *p++;
                v |= static_cast<uint64_t>(b & 0x7f) << shift;
                if (!(b & 0x80))
                    return true;
            }
            return false;
        };
        long last = 0;
        uint64_t gap = 0, length = 0;
        while (p < end && get(gap) && get(length)) {
            const long lo = last + static_cast<long>(gap);
            last = lo + static_cast<long>(length);
            out.emplace_back(lo, last);
        }
    }

    std::vector<std::pair<long, long>> stringRefs() override
    {
        std::vector<std::pair<long, long>> all;
        auto s = prepare("SELECT ranges FROM strref");
        while (sqlite3_step(s) == SQLITE_ROW) {
            unpackRanges(static_cast<const unsigned char*>(sqlite3_column_blob(s, 0)),
                         sqlite3_column_bytes(s, 0), all);
        }
        sqlite3_reset(s);
        std::sort(all.begin(), all.end());
        std::vector<std::pair<long, long>> out;
        for (const auto& r : all) {
            if (!out.empty() && r.first <= out.back().second + 1)
                out.back().second = std::max(out.back().second, r.second);
            else
                out.push_back(r);
        }
        return out;
    }

    void truncate(int64_t before) override
    {
        exec("BEGIN");
        try {
            auto s = prepare("DELETE FROM op WHERE txn<?");
            sqlite3_bind_int64(s, 1, before);
            step(s);
            s = prepare("DELETE FROM txn WHERE seq<?");
            sqlite3_bind_int64(s, 1, before);
            step(s);
            collectEntities();
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    void removeTransactions(const std::vector<int64_t>& seqs) override
    {
        exec("BEGIN");
        try {
            for (int64_t seq : seqs) {
                auto s = prepare("DELETE FROM op WHERE txn=?");
                sqlite3_bind_int64(s, 1, seq);
                step(s);
                s = prepare("DELETE FROM txn WHERE seq=?");
                sqlite3_bind_int64(s, 1, seq);
                step(s);
            }
            collectEntities();
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    void replaceTransactions(const LogTransaction& txn, std::vector<LogOp>& ops,
                             const std::vector<int64_t>& seqs) override
    {
        exec("BEGIN");
        try {
            for (int64_t seq : seqs) {
                auto s = prepare("DELETE FROM op WHERE txn=?");
                sqlite3_bind_int64(s, 1, seq);
                step(s);
                s = prepare("DELETE FROM txn WHERE seq=?");
                sqlite3_bind_int64(s, 1, seq);
                step(s);
            }
            auto s = prepare("DELETE FROM op WHERE txn=?");
            sqlite3_bind_int64(s, 1, txn.seq);
            step(s);
            s = prepare("UPDATE txn SET parent=?, id=?, kind=?, origin=?, name=?, time=?,"
                        " script=?, session=?, inverts=?, branch=?, merge_from=? WHERE seq=?");
            sqlite3_bind_int64(s, 1, txn.parent);
            sqlite3_bind_int(s, 2, txn.id);
            bindText(s, 3, txn.kind);
            bindText(s, 4, txn.origin);
            bindText(s, 5, txn.name);
            sqlite3_bind_double(s, 6, txn.time);
            bindText(s, 7, txn.script);
            sqlite3_bind_int64(s, 8, txn.session);
            sqlite3_bind_int64(s, 9, txn.inverts);
            sqlite3_bind_int64(s, 10, txn.branch);
            sqlite3_bind_int64(s, 11, txn.mergeFrom);
            sqlite3_bind_int64(s, 12, txn.seq);
            step(s);
            auto op = prepare("INSERT INTO op(txn,idx,op,ckind,cid,cname,ctype,prop,ptype,meta,"
                              "vbefore,vafter,derived,touched) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
            int idx = 0;
            for (auto& o : ops) {
                o.txn = txn.seq;
                o.idx = idx++;
                sqlite3_reset(op);
                sqlite3_bind_int64(op, 1, o.txn);
                sqlite3_bind_int(op, 2, o.idx);
                bindText(op, 3, o.op);
                bindText(op, 4, o.ckind);
                sqlite3_bind_int64(op, 5, o.cid);
                bindText(op, 6, o.cname);
                bindText(op, 7, o.ctype);
                bindText(op, 8, o.prop);
                bindText(op, 9, o.ptype);
                bindText(op, 10, o.meta);
                bindText(op, 11, o.vbefore);
                bindText(op, 12, o.vafter);
                sqlite3_bind_int(op, 13, o.derived ? 1 : 0);
                if (o.touched < 0)
                    sqlite3_bind_null(op, 14);
                else
                    sqlite3_bind_int(op, 14, o.touched);
                step(op);
            }
            collectEntities();
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    /// A hash as the collector keys it, the way bindHash() stores one: a
    /// SHA-1, packed or in hex, as its 20 bytes; anything else in `other`,
    /// 'b' and a blob's bytes or 't' and the text. The ops' values and the
    /// manifests' entries are hex, `entity` and `ref` packed.
    struct HeldKey
    {
        std::array<unsigned char, 20> packed {};
        std::string other;
        bool operator==(const HeldKey& k) const
        {
            return packed == k.packed && other == k.other;
        }
    };
    struct HeldKeyHash
    {
        size_t operator()(const HeldKey& k) const
        {
            if (!k.other.empty())
                return std::hash<std::string>()(k.other);
            size_t v;
            std::memcpy(&v, k.packed.data(), sizeof(v));
            return v;
        }
    };

    static void makeKey(const char* p, size_t n, bool blob, HeldKey& key)
    {
        if (blob && n == 20)
            std::memcpy(key.packed.data(), p, 20);
        else if (blob || !packHash(p, n, key.packed.data())) {
            key.other.reserve(n + 1);
            key.other += blob ? 'b' : 't';
            key.other.append(p, n);
        }
    }

    /// Column `col` as makeKey(); false for a NULL.
    static bool columnKey(sqlite3_stmt* s, int col, HeldKey& key)
    {
        switch (sqlite3_column_type(s, col)) {
            case SQLITE_NULL:
                return false;
            case SQLITE_BLOB:
                makeKey(static_cast<const char*>(sqlite3_column_blob(s, col)),
                        static_cast<size_t>(sqlite3_column_bytes(s, col)), true, key);
                return true;
            default: {
                const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
                makeKey(t ? t : "", static_cast<size_t>(sqlite3_column_bytes(s, col)), false,
                        key);
                return true;
            }
        }
    }

    /// The hash a key names, as the rest of the store spells it (columnHash).
    static std::string keyHash(const HeldKey& key)
    {
        if (!key.other.empty())
            return key.other.substr(1);
        static const char digits[] = "0123456789abcdef";
        std::string out(40, '0');
        for (int i = 0; i < 20; ++i) {
            out[2 * i] = digits[key.packed[i] >> 4];
            out[2 * i + 1] = digits[key.packed[i] & 15];
        }
        return out;
    }

    /// Each hash a composite's data lists -- its skeleton, its parts -- as
    /// Composite::decode() reads them, without making the parts; false
    /// where decode() fails.
    template<class F>
    static bool compositeHashes(const std::string& data, F&& f)
    {
        bool skeleton = false;
        size_t pos = 0;
        while (pos < data.size()) {
            size_t end = data.find('\n', pos);
            if (end == std::string::npos)
                end = data.size();
            const char* line = data.data() + pos;
            const size_t n = end - pos;
            pos = end + 1;
            if (n >= 9 && std::memcmp(line, "skeleton ", 9) == 0) {
                if (n > 9) {
                    f(line + 9, n - 9);
                    skeleton = true;
                }
            }
            else if (n >= 2 && std::memcmp(line, "p ", 2) == 0) {
                const char* a = static_cast<const char*>(std::memchr(line + 2, ' ', n - 2));
                const char* b = a ? static_cast<const char*>(
                                        std::memchr(a + 1, ' ', line + n - a - 1))
                                  : nullptr;
                if (!b)
                    return false;
                f(a + 1, static_cast<size_t>(b - a - 1));
            }
            else if (n != 0 && !(n >= 2 && std::memcmp(line, "c ", 2) == 0)) {
                return false;
            }
        }
        return skeleton;
    }

    /// Each hash a manifest's data lists, as readManifest() reads them.
    template<class F>
    static bool manifestHashes(const std::string& data, F&& f)
    {
        size_t pos = 0;
        while (pos < data.size()) {
            size_t end = data.find('\n', pos);
            if (end == std::string::npos)
                end = data.size();
            const size_t space = data.find(' ', pos);
            if (space == std::string::npos || space > end)
                return false;
            f(data.data() + pos, space - pos);
            pos = end + 1;
        }
        return true;
    }

    /// Bytes decoded during one collection, for the delta chains composites
    /// share (each version's is based on the next, sec 23.2); dropped
    /// whole past a bound.
    struct Decoded
    {
        std::map<std::string, std::string> bytes;
        size_t total {0};
    };

    /// The one collector (sec 23.5): delete every entity not reachable from
    /// a root -- an op's ref or a manifest entry -- over the ref edges, of
    /// every role, and into each composite's values (HeldGraph). A delta's
    /// base and a composite's values are held the same way an attachment
    /// is. Inside the caller's transaction.
    void collectEntities()
    {
        HeldGraph g;
        loadGraph(g);
        std::vector<uint32_t> roots;
        addRoots(g, opRoots, roots);
        addRoots(g, manifestRoots, roots);
        std::vector<char> held;
        if (walk(g, roots, nullptr, held))
            sweep(g, held);
    }

    // UNION ALL: the walk takes each hash once, where UNION would sort every
    // op's values first.
    static constexpr const char* opRoots = "SELECT vbefore FROM op WHERE vbefore<>''"
                                           " UNION ALL SELECT vafter FROM op WHERE vafter<>''";
    static constexpr const char* manifestRoots =
        "SELECT manifest FROM version WHERE manifest IS NOT NULL";

    /** What the collector walks: every hash the ref edges, the roots and the
     * composites' and manifests' own data name, numbered, with the edges of
     * every role -- and, once a walk reaches one, a composite's skeleton and
     * values or a manifest's entries, which no edge repeats (sec 27.53).
     *
     * Read once for an operation, however many walks it takes, and walked
     * in memory. The versions' composites list mostly the same values, and
     * marking each part of each of them in SQL, one walk per step of a
     * retention, made a save of a long history spend seconds collecting
     * (sec 27.69).
     */
    struct HeldGraph
    {
        static constexpr uint32_t none = UINT32_MAX;
        enum List : char
        {
            NotAList,
            Composite,
            Manifest,
            Expanded,
        };
        std::vector<HeldKey> keys;
        std::unordered_map<HeldKey, uint32_t, HeldKeyHash> ids;
        std::vector<std::vector<uint32_t>> edges;
        /// A delta's base, or none.
        std::vector<uint32_t> base;
        std::vector<List> list;
        Decoded decoded;

        uint32_t id(HeldKey&& key)
        {
            auto it = ids.find(key);
            if (it != ids.end())
                return it->second;
            const auto n = static_cast<uint32_t>(keys.size());
            ids.emplace(key, n);
            keys.push_back(std::move(key));
            edges.emplace_back();
            base.push_back(none);
            list.push_back(NotAList);
            return n;
        }
    };

    void loadGraph(HeldGraph& g)
    {
        auto s = prepare("SELECT entity, target, role FROM ref");
        while (sqlite3_step(s) == SQLITE_ROW) {
            HeldKey from, to;
            if (!columnKey(s, 0, from) || !columnKey(s, 1, to))
                continue;
            const uint32_t f = g.id(std::move(from));
            const uint32_t t = g.id(std::move(to));
            g.edges[f].push_back(t);
            if (text(s, 2) == "base")
                g.base[f] = t;
        }
        sqlite3_reset(s);
        s = prepare("SELECT hash, kind FROM entity WHERE kind IN ('composite','manifest')");
        while (sqlite3_step(s) == SQLITE_ROW) {
            HeldKey key;
            if (columnKey(s, 0, key))
                g.list[g.id(std::move(key))] =
                    text(s, 1) == "manifest" ? HeldGraph::Manifest : HeldGraph::Composite;
        }
        sqlite3_reset(s);
    }

    /// The hashes the query `roots` returns, numbered in `g`.
    void addRoots(HeldGraph& g, const char* roots, std::vector<uint32_t>& out)
    {
        auto s = prepare(roots);
        while (sqlite3_step(s) == SQLITE_ROW) {
            HeldKey key;
            if (columnKey(s, 0, key))
                out.push_back(g.id(std::move(key)));
        }
        sqlite3_reset(s);
    }

    /// A composite's or a manifest's entries as edges of its own. False when
    /// its data cannot be read: what it holds is then unknown.
    bool expand(HeldGraph& g, uint32_t n)
    {
        const bool manifest = g.list[n] == HeldGraph::Manifest;
        const std::string hash = keyHash(g.keys[n]);
        std::string data;
        std::vector<uint32_t> listed;
        auto add = [&g, &listed](const char* p, size_t len) {
            HeldKey key;
            makeKey(p, len, false, key);
            listed.push_back(g.id(std::move(key)));
        };
        if (!readStored(hash, data, g.decoded)
            || !(manifest ? manifestHashes(data, add) : compositeHashes(data, add))) {
            FC_WARN("transaction log: " << (manifest ? "manifest " : "composite ") << hash
                    << " cannot be read; nothing collected");
            return false;
        }
        g.list[n] = HeldGraph::Expanded;
        g.edges[n].insert(g.edges[n].end(), listed.begin(), listed.end());
        return true;
    }

    /** Mark in `held` every node reachable from `roots`, not passing
     * through one `gone` marks. False when a composite or manifest on the
     * way cannot be read, and the caller must then drop nothing.
     */
    bool walk(HeldGraph& g, const std::vector<uint32_t>& roots, const std::vector<char>* gone,
              std::vector<char>& held)
    {
        held.assign(g.keys.size(), 0);
        std::vector<uint32_t> work;
        auto reach = [&](uint32_t n) {
            if (n >= held.size())
                held.resize(g.keys.size(), 0);
            if (held[n] || (gone && n < gone->size() && (*gone)[n]))
                return;
            held[n] = 1;
            work.push_back(n);
        };
        for (uint32_t r : roots)
            reach(r);
        while (!work.empty()) {
            const uint32_t n = work.back();
            work.pop_back();
            if (g.list[n] == HeldGraph::Composite || g.list[n] == HeldGraph::Manifest) {
                if (!expand(g, n))
                    return false;
            }
            // By index: an expansion numbers new nodes, and edges grows.
            for (size_t i = 0; i < g.edges[n].size(); ++i)
                reach(g.edges[n][i]);
        }
        held.resize(g.keys.size(), 0);
        return true;
    }

    /// Delete every entity `held` does not mark, and the refs and string
    /// ranges it owned.
    void sweep(const HeldGraph& g, const std::vector<char>& held)
    {
        exec("CREATE TEMP TABLE IF NOT EXISTS held(hash PRIMARY KEY) WITHOUT ROWID");
        exec("DELETE FROM held");
        auto ins = prepare("INSERT OR IGNORE INTO held(hash) VALUES(?)");
        for (size_t n = 0; n < held.size(); ++n) {
            if (!held[n])
                continue;
            const HeldKey& key = g.keys[n];
            sqlite3_reset(ins);
            if (key.other.empty())
                sqlite3_bind_blob(ins, 1, key.packed.data(), 20, SQLITE_TRANSIENT);
            else if (key.other[0] == 'b')
                sqlite3_bind_blob(ins, 1, key.other.data() + 1,
                                  static_cast<int>(key.other.size() - 1), SQLITE_TRANSIENT);
            else
                sqlite3_bind_text(ins, 1, key.other.data() + 1,
                                  static_cast<int>(key.other.size() - 1), SQLITE_TRANSIENT);
            step(ins);
        }
        exec("DELETE FROM entity WHERE hash NOT IN (SELECT hash FROM held)");
        exec("DELETE FROM ref WHERE entity NOT IN (SELECT hash FROM entity)");
        exec("DELETE FROM strref WHERE owner NOT IN (SELECT hash FROM entity)");
    }

    /// An entity's full bytes from the store alone: raw, zstd, or a delta
    /// on another such. A file (23.16) is the log's to read, and no
    /// composite is one.
    bool readStored(const std::string& hash, std::string& out, Decoded& decoded, int depth = 0)
    {
        auto it = decoded.bytes.find(hash);
        if (it != decoded.bytes.end()) {
            out = it->second;
            return true;
        }
        auto s = prepare("SELECT enc,base,size,data FROM entity WHERE hash=?");
        bindHash(s, 1, hash);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_reset(s);
            return false;
        }
        const std::string enc = text(s, 0);
        const std::string base = columnHash(s, 1);
        const auto size = static_cast<size_t>(sqlite3_column_int64(s, 2));
        const void* blob = sqlite3_column_blob(s, 3);
        std::string data(static_cast<const char*>(blob), blob ? sqlite3_column_bytes(s, 3) : 0);
        sqlite3_reset(s);
        bool ok = false;
        if (enc == "raw") {
            out = std::move(data);
            ok = true;
        }
#ifdef FC_HAVE_ZSTD
        else if (enc == "zstd") {
            out.assign(size, '\0');
            size_t n = ZSTD_decompress(out.data(), out.size(), data.data(), data.size());
            ok = !ZSTD_isError(n);
            if (ok)
                out.resize(n);
        }
#endif
        else if (enc == "delta" && depth < 1024) {
            std::string b;
            ok = readStored(base, b, decoded, depth + 1)
                && TransactionLog::deltaDecode(data, b, size, out);
        }
        if (ok) {
            if (decoded.total > (64u << 20)) {
                decoded.bytes.clear();
                decoded.total = 0;
            }
            decoded.total += out.size();
            decoded.bytes[hash] = out;
        }
        return ok;
    }

    void insertRef(const std::string& entity, const LogRef& r, int seq)
    {
        auto s = prepare("INSERT OR IGNORE INTO ref(entity,target,role,name,seq)"
                         " VALUES(?,?,?,?,?)");
        bindHash(s, 1, entity);
        bindHash(s, 2, r.target);
        bindText(s, 3, r.role);
        bindText(s, 4, r.name);
        sqlite3_bind_int(s, 5, seq);
        step(s);
    }

    void copyTo(const std::string& path) override
    {
        auto s = prepare("VACUUM INTO ?");
        bindText(s, 1, path);
        step(s);
    }

    void vacuum() override
    {
        exec("VACUUM");
    }

    void dropTier(const std::string& tier, const std::vector<int64_t>& evict) override
    {
        exec("BEGIN");
        try {
            for (int64_t num : evict) {
                auto s = prepare("DELETE FROM version WHERE num=?");
                sqlite3_bind_int64(s, 1, num);
                step(s);
            }
            // What evicting the versions keeps is what every root reaches
            // (`all`). Of that, the tier's entities go that no manifest
            // reaches -- through a composite's values and an attachment's
            // refs as much as directly (23.3) -- and that no kept delta is
            // based on: the base of a cache-tier chain is what the durable
            // row above it decodes through. Then what only they reached.
            HeldGraph g;
            loadGraph(g);
            std::vector<uint32_t> manifests, roots;
            addRoots(g, manifestRoots, manifests);
            addRoots(g, opRoots, roots);
            roots.insert(roots.end(), manifests.begin(), manifests.end());
            std::vector<char> listed, all;
            if (walk(g, manifests, nullptr, listed) && walk(g, roots, nullptr, all)) {
                std::vector<char> based(g.keys.size(), 0);
                for (size_t n = 0; n < all.size(); ++n) {
                    if (all[n] && g.base[n] != HeldGraph::none)
                        based[g.base[n]] = 1;
                }
                std::vector<char> gone(g.keys.size(), 0);
                auto s = prepare("SELECT hash FROM entity WHERE tier=?");
                bindText(s, 1, tier);
                while (sqlite3_step(s) == SQLITE_ROW) {
                    HeldKey key;
                    if (!columnKey(s, 0, key))
                        continue;
                    auto it = g.ids.find(key);
                    if (it == g.ids.end())
                        continue;   // reached by nothing: the sweep takes it
                    const uint32_t n = it->second;
                    if (all[n] && !listed[n] && !based[n])
                        gone[n] = 1;
                }
                sqlite3_reset(s);
                std::vector<char> kept;
                if (walk(g, roots, &gone, kept))
                    sweep(g, kept);
            }
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    bool nameVersion(int64_t num, const std::string& name) override
    {
        auto s = prepare("UPDATE version SET kind=?, name=? WHERE num=?");
        bindText(s, 1, name.empty() ? std::string("unnamed") : std::string("named"));
        bindText(s, 2, name);
        sqlite3_bind_int64(s, 3, num);
        step(s);
        return sqlite3_changes(db) > 0;
    }

    void evictVersion(int64_t num) override
    {
        evictVersions({num});
    }

    void evictVersions(const std::vector<int64_t>& nums) override
    {
        exec("BEGIN");
        try {
            for (int64_t num : nums) {
                auto s = prepare("DELETE FROM version WHERE num=?");
                sqlite3_bind_int64(s, 1, num);
                step(s);
            }
            collectEntities();
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    int64_t environment(const std::string& json) override
    {
        auto s = prepare("SELECT id FROM environment WHERE json=?");
        bindText(s, 1, json);
        if (sqlite3_step(s) == SQLITE_ROW) {
            int64_t id = sqlite3_column_int64(s, 0);
            sqlite3_reset(s);
            return id;
        }
        sqlite3_reset(s);
        s = prepare("INSERT INTO environment(json) VALUES(?)");
        bindText(s, 1, json);
        step(s);
        return sqlite3_last_insert_rowid(db);
    }

    std::string environmentJson(int64_t id) override
    {
        auto s = prepare("SELECT json FROM environment WHERE id=?");
        sqlite3_bind_int64(s, 1, id);
        std::string v;
        if (sqlite3_step(s) == SQLITE_ROW)
            v = text(s, 0);
        sqlite3_reset(s);
        return v;
    }

    int64_t user(const std::string& kind, const std::string& name) override
    {
        auto find = prepare("SELECT id FROM user WHERE kind=? AND name=?");
        bindText(find, 1, kind);
        bindText(find, 2, name);
        int64_t id = 0;
        if (sqlite3_step(find) == SQLITE_ROW)
            id = sqlite3_column_int64(find, 0);
        sqlite3_reset(find);
        if (id)
            return id;
        auto s = prepare("INSERT INTO user(kind,name) VALUES(?,?)");
        bindText(s, 1, kind);
        bindText(s, 2, name);
        step(s);
        return sqlite3_last_insert_rowid(db);
    }

    std::vector<LogUser> users() override
    {
        auto s = prepare("SELECT id,kind,name FROM user ORDER BY id");
        std::vector<LogUser> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogUser r;
            r.id = sqlite3_column_int64(s, 0);
            r.kind = text(s, 1);
            r.name = text(s, 2);
            out.push_back(std::move(r));
        }
        sqlite3_reset(s);
        return out;
    }

    int64_t openSession(int64_t env, int64_t user, const std::string& host,
                        const std::string& access, double opened) override
    {
        auto s = prepare("INSERT INTO session(env,user,host,access,opened,closed)"
                         " VALUES(?,?,?,?,?,0)");
        sqlite3_bind_int64(s, 1, env);
        sqlite3_bind_int64(s, 2, user);
        bindText(s, 3, host);
        bindText(s, 4, access);
        sqlite3_bind_double(s, 5, opened);
        step(s);
        return sqlite3_last_insert_rowid(db);
    }

    void closeSession(int64_t id, double closed) override
    {
        auto s = prepare("UPDATE session SET closed=? WHERE id=?");
        sqlite3_bind_double(s, 1, closed);
        sqlite3_bind_int64(s, 2, id);
        step(s);
    }

    std::vector<LogSession> sessions() override
    {
        auto s = prepare("SELECT id,env,user,host,access,opened,closed FROM session ORDER BY id");
        std::vector<LogSession> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogSession r;
            r.id = sqlite3_column_int64(s, 0);
            r.env = sqlite3_column_int64(s, 1);
            r.user = sqlite3_column_int64(s, 2);
            r.host = text(s, 3);
            r.access = text(s, 4);
            r.opened = sqlite3_column_double(s, 5);
            r.closed = sqlite3_column_double(s, 6);
            out.push_back(std::move(r));
        }
        sqlite3_reset(s);
        return out;
    }

    int64_t addVersion(LogVersion& v, const std::vector<LogManifestEntry>& manifest) override
    {
        exec("BEGIN");
        try {
            v.manifest = putManifest(manifest);
            auto s = prepare("INSERT INTO version(num,uuid,branch,kind,name,seq,env,docxml_hash,"
                             "schema,created,manifest) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
            if (v.num > 0)
                sqlite3_bind_int64(s, 1, v.num);
            else
                sqlite3_bind_null(s, 1);
            bindText(s, 2, v.uuid);
            sqlite3_bind_int64(s, 3, v.branch);
            bindText(s, 4, v.kind);
            bindText(s, 5, v.name);
            sqlite3_bind_int64(s, 6, v.seq);
            sqlite3_bind_int64(s, 7, v.env);
            bindText(s, 8, v.docxml_hash);
            sqlite3_bind_int(s, 9, v.schema);
            sqlite3_bind_double(s, 10, v.created);
            bindHash(s, 11, v.manifest);
            step(s);
            v.num = sqlite3_last_insert_rowid(db);
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
        return v.num;
    }

    static void readVersion(sqlite3_stmt* s, LogVersion& v)
    {
        v.num = sqlite3_column_int64(s, 0);
        v.uuid = text(s, 1);
        v.branch = sqlite3_column_int64(s, 2);
        v.kind = text(s, 3);
        v.name = text(s, 4);
        v.seq = sqlite3_column_int64(s, 5);
        v.env = sqlite3_column_int64(s, 6);
        v.docxml_hash = text(s, 7);
        v.schema = sqlite3_column_int(s, 8);
        v.created = sqlite3_column_double(s, 9);
        v.manifest = columnHash(s, 10);
    }

    int64_t lastVersion() override
    {
        auto s = prepare("SELECT MAX(num) FROM version");
        int64_t n = 0;
        if (sqlite3_step(s) == SQLITE_ROW)
            n = sqlite3_column_int64(s, 0);
        sqlite3_reset(s);
        return n;
    }

    std::vector<LogVersion> versions() override
    {
        auto s = prepare("SELECT num,uuid,branch,kind,name,seq,env,docxml_hash,schema,created,manifest"
                         " FROM version ORDER BY num");
        std::vector<LogVersion> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogVersion v;
            readVersion(s, v);
            out.push_back(std::move(v));
        }
        sqlite3_reset(s);
        return out;
    }

    bool getVersion(int64_t num, LogVersion& v) override
    {
        auto s = prepare("SELECT num,uuid,branch,kind,name,seq,env,docxml_hash,schema,created,manifest"
                         " FROM version WHERE num=?");
        sqlite3_bind_int64(s, 1, num);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        if (found)
            readVersion(s, v);
        sqlite3_reset(s);
        return found;
    }

    bool findVersion(const std::string& hash, LogVersion& v) override
    {
        auto s = prepare("SELECT num,uuid,branch,kind,name,seq,env,docxml_hash,schema,created,manifest"
                         " FROM version WHERE docxml_hash=? ORDER BY num DESC LIMIT 1");
        bindText(s, 1, hash);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        if (found)
            readVersion(s, v);
        sqlite3_reset(s);
        return found;
    }

    std::vector<LogManifestEntry> manifest(int64_t num) override
    {
        auto s = prepare("SELECT manifest FROM version WHERE num=?");
        sqlite3_bind_int64(s, 1, num);
        std::string hash;
        if (sqlite3_step(s) == SQLITE_ROW)
            hash = columnHash(s, 0);
        sqlite3_reset(s);
        std::vector<LogManifestEntry> out;
        if (!hash.empty() && !readManifest(hash, out))
            FC_ERR("transaction log: the manifest of version " << num << " cannot be read");
        return out;
    }

    static void readBranch(sqlite3_stmt* s, LogBranch& b)
    {
        b.id = sqlite3_column_int64(s, 0);
        b.name = text(s, 1);
        b.fromVersion = sqlite3_column_int64(s, 2);
        b.fromSeq = sqlite3_column_int64(s, 3);
        b.head = sqlite3_column_int64(s, 4);
        b.idBase = static_cast<long>(sqlite3_column_int64(s, 5));
        b.lastId = static_cast<long>(sqlite3_column_int64(s, 6));
        b.created = sqlite3_column_double(s, 7);
        b.closed = sqlite3_column_double(s, 8);
        b.target = sqlite3_column_int64(s, 9);
    }

    std::vector<LogBranch> branches() override
    {
        auto s = prepare("SELECT id,name,from_version,from_seq,head_seq,id_base,last_id,created,closed,"
                         "target FROM branch ORDER BY id");
        std::vector<LogBranch> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogBranch b;
            readBranch(s, b);
            out.push_back(std::move(b));
        }
        sqlite3_reset(s);
        return out;
    }

    bool getBranch(int64_t id, LogBranch& b) override
    {
        auto s = prepare("SELECT id,name,from_version,from_seq,head_seq,id_base,last_id,created,closed,"
                         "target FROM branch WHERE id=?");
        sqlite3_bind_int64(s, 1, id);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        if (found)
            readBranch(s, b);
        sqlite3_reset(s);
        return found;
    }

    bool findBranch(const std::string& name, LogBranch& b) override
    {
        auto s = prepare("SELECT id,name,from_version,from_seq,head_seq,id_base,last_id,created,closed,"
                         "target FROM branch WHERE name=?");
        bindText(s, 1, name);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        if (found)
            readBranch(s, b);
        sqlite3_reset(s);
        return found;
    }

    int64_t addBranch(LogBranch& b) override
    {
        if (b.id <= 0) {
            // Past every id any row has named, not only the live branches':
            // a deleted branch's id stays on the versions kept from it.
            auto m = prepare("SELECT MAX(x) FROM (SELECT MAX(id) AS x FROM branch"
                             " UNION ALL SELECT MAX(branch) FROM txn"
                             " UNION ALL SELECT MAX(CAST(branch AS INTEGER)) FROM version)");
            b.id = 1;
            if (sqlite3_step(m) == SQLITE_ROW)
                b.id = sqlite3_column_int64(m, 0) + 1;
            sqlite3_reset(m);
        }
        auto s = prepare("INSERT INTO branch(id,name,from_version,from_seq,head_seq,id_base,"
                         "last_id,created,closed,target) VALUES(?,?,?,?,?,?,?,?,?,?)");
        sqlite3_bind_int64(s, 1, b.id);
        bindText(s, 2, b.name);
        sqlite3_bind_int64(s, 3, b.fromVersion);
        sqlite3_bind_int64(s, 4, b.fromSeq);
        sqlite3_bind_int64(s, 5, b.head);
        sqlite3_bind_int64(s, 6, b.idBase);
        sqlite3_bind_int64(s, 7, b.lastId);
        sqlite3_bind_double(s, 8, b.created);
        sqlite3_bind_double(s, 9, b.closed);
        sqlite3_bind_int64(s, 10, b.target);
        step(s);
        b.id = sqlite3_last_insert_rowid(db);
        return b.id;
    }

    bool updateBranch(const LogBranch& b) override
    {
        auto s = prepare("UPDATE branch SET name=?, from_version=?, from_seq=?, id_base=?,"
                         " last_id=?, created=?, closed=?, target=? WHERE id=?");
        bindText(s, 1, b.name);
        sqlite3_bind_int64(s, 2, b.fromVersion);
        sqlite3_bind_int64(s, 3, b.fromSeq);
        sqlite3_bind_int64(s, 4, b.idBase);
        sqlite3_bind_int64(s, 5, b.lastId);
        sqlite3_bind_double(s, 6, b.created);
        sqlite3_bind_double(s, 7, b.closed);
        sqlite3_bind_int64(s, 8, b.target);
        sqlite3_bind_int64(s, 9, b.id);
        step(s);
        return sqlite3_changes(db) > 0;
    }

    bool forwardBranch(int64_t id, int64_t head, const std::vector<int64_t>& seqs) override
    {
        exec("BEGIN");
        try {
            auto s = prepare("UPDATE branch SET head_seq=? WHERE id=?");
            sqlite3_bind_int64(s, 1, head);
            sqlite3_bind_int64(s, 2, id);
            step(s);
            const bool found = sqlite3_changes(db) > 0;
            for (int64_t seq : seqs) {
                s = prepare("UPDATE txn SET branch=? WHERE seq=?");
                sqlite3_bind_int64(s, 1, id);
                sqlite3_bind_int64(s, 2, seq);
                step(s);
                s = prepare("UPDATE version SET branch=? WHERE seq=?");
                sqlite3_bind_int64(s, 1, id);
                sqlite3_bind_int64(s, 2, seq);
                step(s);
            }
            exec("COMMIT");
            return found;
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }

    void anchorVersions(const std::vector<int64_t>& seqs, int64_t to) override
    {
        for (int64_t seq : seqs) {
            auto s = prepare("UPDATE version SET seq=? WHERE seq=?");
            sqlite3_bind_int64(s, 1, to);
            sqlite3_bind_int64(s, 2, seq);
            step(s);
        }
    }

    bool removeBranch(int64_t id) override
    {
        auto s = prepare("DELETE FROM branch WHERE id=?");
        sqlite3_bind_int64(s, 1, id);
        step(s);
        return sqlite3_changes(db) > 0;
    }

    bool renameBranch(int64_t id, const std::string& name) override
    {
        auto s = prepare("UPDATE branch SET name=? WHERE id=?");
        bindText(s, 1, name);
        sqlite3_bind_int64(s, 2, id);
        step(s);
        return sqlite3_changes(db) > 0;
    }

    std::string getMeta(const std::string& key) override
    {
        auto s = prepare("SELECT value FROM meta WHERE key=?");
        bindText(s, 1, key);
        std::string v;
        if (sqlite3_step(s) == SQLITE_ROW)
            v = text(s, 0);
        sqlite3_reset(s);
        return v;
    }

    void setMeta(const std::string& key, const std::string& value) override
    {
        auto s = prepare("INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)");
        bindText(s, 1, key);
        bindText(s, 2, value);
        step(s);
    }

private:
    void exec(const char* sql)
    {
        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = err ? err : "unknown";
            sqlite3_free(err);
            throw Base::RuntimeError(std::string("Transaction log: ") + msg + " in " + sql);
        }
    }

    sqlite3_stmt* prepare(const char* sql)
    {
        auto it = stmts.find(sql);
        if (it != stmts.end()) {
            sqlite3_reset(it->second);
            sqlite3_clear_bindings(it->second);
            return it->second;
        }
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            throw Base::RuntimeError(std::string("Transaction log: ") + sqlite3_errmsg(db)
                                     + " preparing " + sql);
        stmts[sql] = s;
        return s;
    }

    void step(sqlite3_stmt* s)
    {
        int rc = sqlite3_step(s);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            std::string msg = sqlite3_errmsg(db);
            sqlite3_reset(s);
            throw Base::RuntimeError("Transaction log: " + msg);
        }
        sqlite3_reset(s);
    }

    static void bindText(sqlite3_stmt* s, int i, const std::string& v)
    {
        sqlite3_bind_text(s, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    }

    static std::string text(sqlite3_stmt* s, int col)
    {
        const unsigned char* t = sqlite3_column_text(s, col);
        return t ? reinterpret_cast<const char*>(t) : "";
    }

    /// A hash into an `entity` or `ref` column, or a version's manifest
    /// (packHash); every other hash column is text.
    static void bindHash(sqlite3_stmt* s, int i, const std::string& v)
    {
        unsigned char packed[20];
        if (packHash(v.data(), v.size(), packed))
            sqlite3_bind_blob(s, i, packed, 20, SQLITE_TRANSIENT);
        else
            bindText(s, i, v);
    }

    static std::string columnHash(sqlite3_stmt* s, int col)
    {
        if (sqlite3_column_type(s, col) != SQLITE_BLOB || sqlite3_column_bytes(s, col) != 20)
            return text(s, col);
        static const char digits[] = "0123456789abcdef";
        const auto* b = static_cast<const unsigned char*>(sqlite3_column_blob(s, col));
        std::string out(40, '0');
        for (int i = 0; i < 20; ++i) {
            out[2 * i] = digits[b[i] >> 4];
            out[2 * i + 1] = digits[b[i] & 15];
        }
        return out;
    }

    sqlite3* db {nullptr};
    std::map<std::string, sqlite3_stmt*> stmts;
};

} // namespace

std::unique_ptr<TransactionStore> TransactionStore::openSQLite(const std::string& path)
{
    return std::make_unique<SQLiteStore>(path);
}
