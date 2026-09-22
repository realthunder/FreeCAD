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

#ifndef APP_TRANSACTION_STORE_H
#define APP_TRANSACTION_STORE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <FCGlobal.h>

namespace App
{

/** One committed transaction as the store keeps it (docs/TransactionLog.md
 * sec 13.2, table `txn`). `seq` is assigned by the store on append.
 */
struct LogTransaction
{
    int64_t seq {0};
    int64_t parent {0};      ///< seq of the transaction this one follows
    int id {0};              ///< App::Transaction::getID(), for grouping across documents
    std::string kind;        ///< "user", "implicit", "recompute", "restore", ...
    std::string origin;      ///< "gui", "python", ...
    std::string name;        ///< the transaction's display name
    double time {0};         ///< seconds since the epoch
    std::string script;      ///< the MacroManager lines, an annotation
};

/** One op of a transaction (table `op`).
 *
 * `vbefore` / `vafter` are value refs (LogValue::hash), empty when there is
 * none: a `create` has no before, a `remove` no after, and an after that is
 * not yet resolved (sec 20.2, decision 4) is empty until resolveAfter().
 */
struct LogOp
{
    int64_t txn {0};
    int idx {0};
    std::string op;          ///< create, remove, set, addprop, delprop
    std::string ckind;       ///< "doc", "obj", "view"
    long cid {0};            ///< DocumentObject::getID(), 0 for the document
    std::string cname;       ///< object internal name (create/remove), else empty
    std::string ctype;       ///< object type name (create/remove), else empty
    std::string prop;        ///< property name, empty for create/remove
    std::string ptype;       ///< property type name
    std::string meta;        ///< dynamic-property metadata for addprop/delprop
    std::string vbefore;
    std::string vafter;
    bool derived {false};
};

/** A stored value (table `value`).
 *
 * The XML fragment Property::Save writes, plus the attachments it handed to
 * addFile(), each an entry of `attachments` naming another value by hash.
 * `hash` covers the fragment and the attachment list, so a value with an
 * unchanged attachment and a changed fragment shares the attachment.
 * `data` is the fragment (or, for an attachment value, the file bytes) as
 * stored: `enc` says how ("raw", "zstd").
 */
struct LogValue
{
    std::string hash;
    std::string enc {"raw"};
    std::string tier {"durable"};   ///< "durable" or "cache" (sec 10)
    uint64_t size {0};              ///< uncompressed bytes of `data`
    std::string data;
    std::vector<std::pair<std::string, std::string>> attachments;  ///< (name, hash)
};

/** The interface the document sees: a log is appended, read and truncated
 * through this and nothing else (sec 13.2). openSQLite() is the one
 * implementation.
 */
class AppExport TransactionStore
{
public:
    virtual ~TransactionStore() = default;

    /// Append one transaction with its ops, atomically. Returns its seq;
    /// the ops' `txn` and `idx` are filled in.
    virtual int64_t append(LogTransaction& txn, std::vector<LogOp>& ops) = 0;
    /// Fill the after ref of an op left pending by append().
    virtual void resolveAfter(int64_t txn, int idx, const std::string& hash) = 0;

    virtual bool hasValue(const std::string& hash) = 0;
    /// Store a value; a hash already present is left as it is.
    virtual void putValue(const LogValue& value) = 0;
    /// Read a value back, `data` as stored. False if absent.
    virtual bool getValue(const std::string& hash, LogValue& value) = 0;

    /// Transactions with seq >= from, in order, at most `limit` (0: all).
    virtual std::vector<LogTransaction> transactions(int64_t from = 0, int limit = 0) = 0;
    virtual std::vector<LogOp> ops(int64_t txn) = 0;
    virtual int64_t lastSeq() = 0;

    /// Drop every transaction with seq < before, and the values nothing
    /// refers to any more.
    virtual void truncate(int64_t before) = 0;

    virtual std::string getMeta(const std::string& key) = 0;
    virtual void setMeta(const std::string& key, const std::string& value) = 0;

    /// Open or create the SQLite log at `path` (WAL, synchronous=NORMAL).
    /// Throws Base::RuntimeError on failure.
    static std::unique_ptr<TransactionStore> openSQLite(const std::string& path);
};

} // namespace App

#endif // APP_TRANSACTION_STORE_H
