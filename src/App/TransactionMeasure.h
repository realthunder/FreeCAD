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

#ifndef APP_TRANSACTION_MEASURE_H
#define APP_TRANSACTION_MEASURE_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <FCGlobal.h>

namespace App
{

class Document;
class Transaction;

/** Phase 0 of the transaction log (docs/TransactionLog.md sec 14, 15):
 * measure what a commit would write, without writing it.
 *
 * When enabled, every Document::_commitTransaction walks the transaction
 * the way the log writer will -- one op per object created, removed or
 * changed, one value per property before and after -- and serialises each
 * value through Property::Save into memory. Per value it records the bytes,
 * the content hash (and whether that hash was seen before in this session,
 * which is what content addressing turns into no write), the compressed
 * size under zlib and zstd, and for a set op the byte-level delta of the
 * after value against the before value. Everything is timed, and the
 * per-transaction total is what answers "does a commit fit in the
 * latency budget" before any store exists.
 *
 * Output is CSV, one row per value, plus a `txn` row per commit, appended
 * to the file given to start(). Nothing here is reached when the measure
 * is off: the hook in the commit path is one static bool test.
 */
class AppExport TransactionMeasure
{
public:
    /// Start appending to \a csvPath (truncating it first). Returns false
    /// if the file cannot be opened.
    static bool start(const char* csvPath);
    static void stop();
    static bool enabled() { return _instance != nullptr; }

    /// Also honoured on the first commit: the FC_TXN_MEASURE environment
    /// variable names the CSV, for runs that cannot call start() first.
    static void checkEnvironment();

    /// The commit hook. Called by Document::_commitTransaction with the
    /// transaction about to move to the undo stack, before it moves.
    static void onCommit(Document& doc, const Transaction& txn);

    /// Add a free-form marker row, so a driver script can label the
    /// scenario the following rows belong to.
    static void mark(const char* label);

    ~TransactionMeasure();

private:
    // Out of line, next to the destructor: an inline default makes MSVC
    // instantiate ~unique_ptr<Impl> with Impl incomplete.
    TransactionMeasure();
    struct Impl;
    std::unique_ptr<Impl> _impl;
    static TransactionMeasure* _instance;
};

} // namespace App

#endif // APP_TRANSACTION_MEASURE_H
