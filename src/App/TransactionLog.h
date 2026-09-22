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

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <FCGlobal.h>

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
 */
class AppExport TransactionLog
{
public:
    explicit TransactionLog(Document& doc);
    ~TransactionLog();

    TransactionLog(const TransactionLog&) = delete;
    TransactionLog& operator=(const TransactionLog&) = delete;

    /// The commit hook, called with the transaction about to move to the
    /// undo stack. Never throws; a store failure is reported and skipped.
    void onCommit(const Transaction& txn, const char* kind = "user",
                  const char* origin = "");

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

    /** The save record and the unnamed version it makes (sec 11, 16.3).
     *
     * Called from Document::save once the file's entries are written:
     * `docXml` is the Document.xml as it streamed out (hashed with the
     * writer's tap), `blobs` the (name, hash) of every blob the file
     * carries, `schema` the schema it was written under. Pending after
     * refs are resolved first so the log is complete at the snapshot.
     * Returns the version number, 0 on failure (reported, not thrown).
     */
    int64_t onSave(const std::string& path, const std::string& docXml,
                   const std::vector<std::pair<std::string, std::string>>& blobs, int schema);

    int64_t session() const { return _session; }
    int64_t environment() const { return _environment; }

    /// Serialise the live value behind every pending after ref. What a
    /// version snapshot does first; also what makes the log complete for
    /// a reader that wants the head state from the log alone.
    void resolvePending();
    size_t pendingCount() const { return _pending.size(); }

    TransactionStore& store() { return *_store; }
    const std::string& path() const { return _path; }

    /// Read a value back, decompressed, with its attachments inlined.
    bool readValue(const std::string& hash, CapturedValue& out);

private:
    struct Pending
    {
        int64_t txn;
        int idx;
        long cid;            ///< 0 for the document's own property
        std::string prop;
        std::string tier;
    };

    /// Store a captured value (fragment and attachments) and return its ref.
    std::string putValue(const CapturedValue& value, const std::string& tier);
    void resolve(const Property& prop, const std::string& hash);
    void resolve(int64_t key, const std::string& hash);

    Document& _doc;
    std::string _path;
    int64_t _environment {0};
    int64_t _session {0};
    std::unique_ptr<TransactionStore> _store;
    /// Property id -> the op whose after ref that property's next copy
    /// resolves. Filled after append() assigns the seq.
    std::unordered_map<int64_t, Pending> _pending;
};

} // namespace App

#endif // APP_TRANSACTION_LOG_H
