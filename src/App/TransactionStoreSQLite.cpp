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
             " kind TEXT, origin TEXT, name TEXT, time REAL, script TEXT)");
        exec("CREATE TABLE IF NOT EXISTS op(txn INTEGER, idx INTEGER, op TEXT, ckind TEXT,"
             " cid INTEGER, cname TEXT, ctype TEXT, prop TEXT, ptype TEXT, meta TEXT,"
             " vbefore TEXT, vafter TEXT, derived INTEGER, PRIMARY KEY(txn, idx))");
        exec("CREATE INDEX IF NOT EXISTS op_container ON op(cid, prop)");
        exec("CREATE TABLE IF NOT EXISTS value(hash TEXT PRIMARY KEY, enc TEXT, tier TEXT,"
             " size INTEGER, data BLOB, attach TEXT)");
        if (getMeta("schema").empty())
            setMeta("schema", "1");
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
            auto ins = prepare("INSERT INTO txn(parent,id,kind,origin,name,time,script)"
                               " VALUES(?,?,?,?,?,?,?)");
            sqlite3_bind_int64(ins, 1, txn.parent);
            sqlite3_bind_int(ins, 2, txn.id);
            bindText(ins, 3, txn.kind);
            bindText(ins, 4, txn.origin);
            bindText(ins, 5, txn.name);
            sqlite3_bind_double(ins, 6, txn.time);
            bindText(ins, 7, txn.script);
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

    bool hasValue(const std::string& hash) override
    {
        auto s = prepare("SELECT 1 FROM value WHERE hash=?");
        bindText(s, 1, hash);
        bool found = sqlite3_step(s) == SQLITE_ROW;
        sqlite3_reset(s);
        return found;
    }

    void putValue(const LogValue& v) override
    {
        auto s = prepare("INSERT OR IGNORE INTO value(hash,enc,tier,size,data,attach)"
                         " VALUES(?,?,?,?,?,?)");
        bindText(s, 1, v.hash);
        bindText(s, 2, v.enc);
        bindText(s, 3, v.tier);
        sqlite3_bind_int64(s, 4, static_cast<sqlite3_int64>(v.size));
        sqlite3_bind_blob(s, 5, v.data.data(), static_cast<int>(v.data.size()), SQLITE_TRANSIENT);
        std::string attach;
        for (auto& a : v.attachments)
            attach += a.second + ' ' + a.first + '\n';
        bindText(s, 6, attach);
        step(s);
    }

    bool getValue(const std::string& hash, LogValue& v) override
    {
        auto s = prepare("SELECT enc,tier,size,data,attach FROM value WHERE hash=?");
        bindText(s, 1, hash);
        if (sqlite3_step(s) != SQLITE_ROW) {
            sqlite3_reset(s);
            return false;
        }
        v.hash = hash;
        v.enc = text(s, 0);
        v.tier = text(s, 1);
        v.size = static_cast<uint64_t>(sqlite3_column_int64(s, 2));
        const void* blob = sqlite3_column_blob(s, 3);
        int n = sqlite3_column_bytes(s, 3);
        v.data.assign(static_cast<const char*>(blob), blob ? n : 0);
        v.attachments.clear();
        std::istringstream attach(text(s, 4));
        std::string line;
        while (std::getline(attach, line)) {
            auto sp = line.find(' ');
            if (sp != std::string::npos)
                v.attachments.emplace_back(line.substr(sp + 1), line.substr(0, sp));
        }
        sqlite3_reset(s);
        return true;
    }

    std::vector<LogTransaction> transactions(int64_t from, int limit) override
    {
        auto s = prepare("SELECT seq,parent,id,kind,origin,name,time,script FROM txn"
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
            // Values nothing refers to: not an op ref, not an attachment of a
            // value that is.
            exec("DELETE FROM value WHERE hash NOT IN (SELECT vbefore FROM op)"
                 " AND hash NOT IN (SELECT vafter FROM op)"
                 " AND hash NOT IN (SELECT substr(attach, 1, 40) FROM value"
                 "                  WHERE attach<>'')");
            exec("COMMIT");
        }
        catch (...) {
            exec("ROLLBACK");
            throw;
        }
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
