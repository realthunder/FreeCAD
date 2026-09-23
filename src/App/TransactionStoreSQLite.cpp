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
# include <sstream>
#endif

#include <sqlite3.h>

#include <Base/Console.h>
#include <Base/Exception.h>

#include "TransactionStore.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

namespace {

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
        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA synchronous=NORMAL");
        exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)");
        exec("CREATE TABLE IF NOT EXISTS txn(seq INTEGER PRIMARY KEY, parent INTEGER, id INTEGER,"
             " kind TEXT, origin TEXT, name TEXT, time REAL, script TEXT, session INTEGER)");
        exec("CREATE TABLE IF NOT EXISTS environment(id INTEGER PRIMARY KEY, json TEXT UNIQUE)");
        exec("CREATE TABLE IF NOT EXISTS session(id INTEGER PRIMARY KEY, env INTEGER, user TEXT,"
             " host TEXT, opened REAL, closed REAL)");
        exec("CREATE TABLE IF NOT EXISTS op(txn INTEGER, idx INTEGER, op TEXT, ckind TEXT,"
             " cid INTEGER, cname TEXT, ctype TEXT, prop TEXT, ptype TEXT, meta TEXT,"
             " vbefore TEXT, vafter TEXT, derived INTEGER, PRIMARY KEY(txn, idx))");
        exec("CREATE INDEX IF NOT EXISTS op_container ON op(cid, prop)");
        exec("CREATE TABLE IF NOT EXISTS version(num INTEGER PRIMARY KEY, uuid TEXT, branch TEXT,"
             " kind TEXT, name TEXT, seq INTEGER, env INTEGER, docxml_hash TEXT, schema INTEGER,"
             " created REAL)");
        exec("CREATE INDEX IF NOT EXISTS version_hash ON version(docxml_hash)");
        exec("CREATE TABLE IF NOT EXISTS manifest(version INTEGER, entry TEXT, hash TEXT,"
             " source TEXT, PRIMARY KEY(version, entry))");
        if (getMeta("schema") == "1")
            migrateValues();
        exec("CREATE TABLE IF NOT EXISTS entity(hash TEXT PRIMARY KEY, kind TEXT, enc TEXT,"
             " base TEXT, tier TEXT, size INTEGER, data BLOB)");
        exec("CREATE TABLE IF NOT EXISTS ref(entity TEXT, target TEXT, role TEXT, name TEXT,"
             " seq INTEGER, PRIMARY KEY(entity, role, name, target))");
        exec("CREATE INDEX IF NOT EXISTS ref_target ON ref(target, role)");
        if (getMeta("schema").empty() || getMeta("schema") == "1")
            setMeta("schema", "2");
    }

    /// Schema 1 kept values in `value(hash, enc, tier, size, data, attach)`
    /// with the attachment list as lines of `attach`, and the manifest named
    /// them `source='value'`. Schema 2 is the entity table and the ref edges
    /// of sec 23.1. A store from the days between is rare (the log has
    /// shipped in no release) but an embedded copy from one opens again.
    void migrateValues()
    {
        exec("BEGIN");
        try {
            exec("CREATE TABLE IF NOT EXISTS entity(hash TEXT PRIMARY KEY, kind TEXT, enc TEXT,"
                 " base TEXT, tier TEXT, size INTEGER, data BLOB)");
            exec("CREATE TABLE IF NOT EXISTS ref(entity TEXT, target TEXT, role TEXT, name TEXT,"
                 " seq INTEGER, PRIMARY KEY(entity, role, name, target))");
            exec("INSERT OR IGNORE INTO entity(hash,kind,enc,base,tier,size,data)"
                 " SELECT hash,'prop',enc,'',tier,size,data FROM value");
            // An attachment is a value with no attachments of its own that
            // some other value's list names; mark those `attach`.
            exec("WITH RECURSIVE lines(hash, rest, line, seq) AS ("
                 "  SELECT hash, attach || char(10), '', -1 FROM value WHERE attach<>''"
                 "  UNION ALL"
                 "  SELECT hash, substr(rest, instr(rest, char(10)) + 1),"
                 "         substr(rest, 1, instr(rest, char(10)) - 1), seq + 1"
                 "  FROM lines WHERE rest<>'')"
                 " INSERT OR IGNORE INTO ref(entity,target,role,name,seq)"
                 " SELECT hash, substr(line, 1, 40), 'attach', substr(line, 42), seq"
                 " FROM lines WHERE line<>''");
            exec("UPDATE entity SET kind='attach' WHERE hash IN"
                 " (SELECT target FROM ref WHERE role='attach')");
            exec("UPDATE entity SET kind='xml' WHERE hash IN"
                 " (SELECT hash FROM manifest WHERE source='value')");
            exec("UPDATE manifest SET source='entity' WHERE source='value'");
            exec("DROP TABLE value");
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
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
            auto ins = prepare("INSERT INTO txn(seq,parent,id,kind,origin,name,time,script,session)"
                               " VALUES(?,?,?,?,?,?,?,?,?)");
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
            step(ins);
            txn.seq = sqlite3_last_insert_rowid(db);

            auto op = prepare("INSERT INTO op(txn,idx,op,ckind,cid,cname,ctype,prop,ptype,meta,"
                              "vbefore,vafter,derived) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
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
        bindText(s, 1, hash);
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
            bindText(s, 1, e.hash);
            bindText(s, 2, e.kind);
            bindText(s, 3, e.enc);
            bindText(s, 4, e.base);
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
        bindText(s, 1, hash);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_reset(s);
            return false;
        }
        e.hash = hash;
        e.kind = text(s, 0);
        e.enc = text(s, 1);
        e.base = text(s, 2);
        e.tier = text(s, 3);
        e.size = static_cast<uint64_t>(sqlite3_column_int64(s, 4));
        const void* blob = sqlite3_column_blob(s, 5);
        int n = sqlite3_column_bytes(s, 5);
        e.data.assign(static_cast<const char*>(blob), blob ? n : 0);
        sqlite3_reset(s);
        e.refs.clear();
        auto r = prepare("SELECT target,role,name FROM ref WHERE entity=? AND role<>'base'"
                         " ORDER BY seq");
        bindText(r, 1, hash);
        while (sqlite3_step(r) == SQLITE_ROW)
            e.refs.push_back(LogRef {text(r, 0), text(r, 1), text(r, 2)});
        sqlite3_reset(r);
        return true;
    }

    void reencodeEntity(const std::string& hash, const std::string& enc,
                        const std::string& base, const std::string& data) override
    {
        exec("BEGIN");
        try {
            auto s = prepare("UPDATE entity SET enc=?, base=?, data=? WHERE hash=?");
            bindText(s, 1, enc);
            bindText(s, 2, base);
            sqlite3_bind_blob(s, 3, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
            bindText(s, 4, hash);
            step(s);
            s = prepare("DELETE FROM ref WHERE entity=? AND role='base'");
            bindText(s, 1, hash);
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

    std::vector<std::string> basedOn(const std::string& hash) override
    {
        auto s = prepare("SELECT entity FROM ref WHERE target=? AND role='base'");
        bindText(s, 1, hash);
        std::vector<std::string> out;
        while (sqlite3_step(s) == SQLITE_ROW)
            out.push_back(text(s, 0));
        sqlite3_reset(s);
        return out;
    }

    std::vector<LogTransaction> transactions(int64_t from, int limit) override
    {
        auto s = prepare("SELECT seq,parent,id,kind,origin,name,time,script,session FROM txn"
                         " WHERE seq>=? ORDER BY seq LIMIT ?");
        sqlite3_bind_int64(s, 1, from);
        sqlite3_bind_int(s, 2, limit > 0 ? limit : -1);
        std::vector<LogTransaction> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
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
            out.push_back(std::move(t));
        }
        sqlite3_reset(s);
        return out;
    }

    std::vector<LogOp> ops(int64_t txn) override
    {
        auto s = prepare("SELECT idx,op,ckind,cid,cname,ctype,prop,ptype,meta,vbefore,vafter,"
                         "derived FROM op WHERE txn=? ORDER BY idx");
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
            out.push_back(std::move(o));
        }
        sqlite3_reset(s);
        return out;
    }

    bool getOp(int64_t txn, int idx, LogOp& o) override
    {
        auto s = prepare("SELECT op,ckind,cid,cname,ctype,prop,ptype,meta,vbefore,vafter,derived"
                         " FROM op WHERE txn=? AND idx=?");
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

    /// The one collector (sec 23.5): delete every entity not reachable from
    /// a root -- an op's ref or a manifest entry -- over the ref edges, of
    /// every role. A delta's base and a composite's parts are held the
    /// same way an attachment is. Inside the caller's transaction.
    void collectEntities()
    {
        exec("WITH RECURSIVE live(hash) AS ("
             "  SELECT vbefore FROM op WHERE vbefore<>''"
             "  UNION SELECT vafter FROM op WHERE vafter<>''"
             "  UNION SELECT hash FROM manifest WHERE source='entity'"
             "  UNION SELECT r.target FROM ref r JOIN live ON r.entity=live.hash)"
             " DELETE FROM entity WHERE hash NOT IN (SELECT hash FROM live)");
        exec("DELETE FROM ref WHERE entity NOT IN (SELECT hash FROM entity)");
    }

    void insertRef(const std::string& entity, const LogRef& r, int seq)
    {
        auto s = prepare("INSERT OR IGNORE INTO ref(entity,target,role,name,seq)"
                         " VALUES(?,?,?,?,?)");
        bindText(s, 1, entity);
        bindText(s, 2, r.target);
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

    void dropTier(const std::string& tier) override
    {
        exec("BEGIN");
        try {
            // Not one a manifest reaches, and not one a surviving delta is
            // based on: the base of a cache-tier chain is what the durable
            // row above it decodes through.
            auto s = prepare("DELETE FROM entity WHERE tier=?"
                             " AND hash NOT IN (SELECT hash FROM manifest WHERE source='entity')"
                             " AND hash NOT IN (SELECT target FROM ref WHERE role='base')");
            bindText(s, 1, tier);
            step(s);
            exec("DELETE FROM ref WHERE entity NOT IN (SELECT hash FROM entity)");
            collectEntities();
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
        exec("BEGIN");
        try {
            auto s = prepare("DELETE FROM manifest WHERE version=?");
            sqlite3_bind_int64(s, 1, num);
            step(s);
            s = prepare("DELETE FROM version WHERE num=?");
            sqlite3_bind_int64(s, 1, num);
            step(s);
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

    int64_t openSession(int64_t env, const std::string& user, const std::string& host,
                        double opened) override
    {
        auto s = prepare("INSERT INTO session(env,user,host,opened,closed) VALUES(?,?,?,?,0)");
        sqlite3_bind_int64(s, 1, env);
        bindText(s, 2, user);
        bindText(s, 3, host);
        sqlite3_bind_double(s, 4, opened);
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
        auto s = prepare("SELECT id,env,user,host,opened,closed FROM session ORDER BY id");
        std::vector<LogSession> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogSession r;
            r.id = sqlite3_column_int64(s, 0);
            r.env = sqlite3_column_int64(s, 1);
            r.user = text(s, 2);
            r.host = text(s, 3);
            r.opened = sqlite3_column_double(s, 4);
            r.closed = sqlite3_column_double(s, 5);
            out.push_back(std::move(r));
        }
        sqlite3_reset(s);
        return out;
    }

    int64_t addVersion(LogVersion& v, const std::vector<LogManifestEntry>& manifest) override
    {
        exec("BEGIN");
        try {
            auto s = prepare("INSERT INTO version(num,uuid,branch,kind,name,seq,env,docxml_hash,"
                             "schema,created) VALUES(?,?,?,?,?,?,?,?,?,?)");
            if (v.num > 0)
                sqlite3_bind_int64(s, 1, v.num);
            else
                sqlite3_bind_null(s, 1);
            bindText(s, 2, v.uuid);
            bindText(s, 3, v.branch);
            bindText(s, 4, v.kind);
            bindText(s, 5, v.name);
            sqlite3_bind_int64(s, 6, v.seq);
            sqlite3_bind_int64(s, 7, v.env);
            bindText(s, 8, v.docxml_hash);
            sqlite3_bind_int(s, 9, v.schema);
            sqlite3_bind_double(s, 10, v.created);
            step(s);
            v.num = sqlite3_last_insert_rowid(db);
            auto m = prepare("INSERT OR REPLACE INTO manifest(version,entry,hash,source)"
                             " VALUES(?,?,?,?)");
            for (const auto& e : manifest) {
                sqlite3_reset(m);
                sqlite3_bind_int64(m, 1, v.num);
                bindText(m, 2, e.entry);
                bindText(m, 3, e.hash);
                bindText(m, 4, e.source);
                step(m);
            }
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
        v.branch = text(s, 2);
        v.kind = text(s, 3);
        v.name = text(s, 4);
        v.seq = sqlite3_column_int64(s, 5);
        v.env = sqlite3_column_int64(s, 6);
        v.docxml_hash = text(s, 7);
        v.schema = sqlite3_column_int(s, 8);
        v.created = sqlite3_column_double(s, 9);
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
        auto s = prepare("SELECT num,uuid,branch,kind,name,seq,env,docxml_hash,schema,created"
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
        auto s = prepare("SELECT num,uuid,branch,kind,name,seq,env,docxml_hash,schema,created"
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
        auto s = prepare("SELECT num,uuid,branch,kind,name,seq,env,docxml_hash,schema,created"
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
        auto s = prepare("SELECT entry,hash,source FROM manifest WHERE version=? ORDER BY entry");
        sqlite3_bind_int64(s, 1, num);
        std::vector<LogManifestEntry> out;
        while (sqlite3_step(s) == SQLITE_ROW) {
            LogManifestEntry e;
            e.entry = text(s, 0);
            e.hash = text(s, 1);
            e.source = text(s, 2);
            out.push_back(std::move(e));
        }
        sqlite3_reset(s);
        return out;
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

    sqlite3* db {nullptr};
    std::map<std::string, sqlite3_stmt*> stmts;
};

} // namespace

std::unique_ptr<TransactionStore> TransactionStore::openSQLite(const std::string& path)
{
    return std::make_unique<SQLiteStore>(path);
}
