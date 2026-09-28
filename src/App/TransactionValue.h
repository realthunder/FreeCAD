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

#ifndef APP_TRANSACTION_VALUE_H
#define APP_TRANSACTION_VALUE_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <FCGlobal.h>
#include <fastsignals/signal.h>

namespace Base { class Persistence; }

namespace App
{

class Document;
class DocumentObject;
class FileBlob;
class FileBlobManager;
class Property;
class StringHasher;

/** A value as the transaction log sees it (docs/TransactionLog.md sec 9.3,
 * sec 20.2 decision 5): the XML fragment Property::Save writes, plus every
 * file it handed to Writer::addFile(), captured in memory. Nothing is
 * hashed here; the log hashes the attachments and the fragment separately.
 */
struct CapturedValue
{
    struct Attachment
    {
        std::string name;
        std::string bytes;
    };
    std::string fragment;
    std::vector<Attachment> attachments;
    /// The files of the document's blob store the fragment names by hash,
    /// as the Save noted them (BlobRecorder, sec 23.16). Not content: the
    /// log holds them as entities of their own.
    std::vector<std::shared_ptr<FileBlob>> blobs;
    /// The ids of the file's string table the value's element maps use,
    /// sorted (docs/TransactionLog.md sec 27.50 item 4): found by the
    /// capture's own walk, not by what a save marked.
    std::vector<long> stringIds;
    bool ok {false};

    size_t attachmentBytes() const
    {
        size_t n = 0;
        for (auto& a : attachments)
            n += a.bytes.size();
        return n;
    }
};

/// What of the document a capture needs to write the bytes a save would:
/// taken on the main thread so the serialiser can run on another.
struct CaptureConfig
{
    int schema {0};
    bool preferBinary {false};
    /// The document's blob store: where a detached copy's blobs go.
    FileBlobManager* blobs {nullptr};
    /// The file's string hasher, compared by address only: the ids of it
    /// an element map uses are listed, and noted in stringIds.
    const StringHasher* hasher {nullptr};
    /// The document the values are the log's for: the owner a detached
    /// copy of an XLink writes relative to (capturingDocument()). Read on
    /// the main thread only -- links are captured there (sec 24.3).
    const Document* document {nullptr};
    CaptureConfig() = default;
    explicit CaptureConfig(const Document& doc);
};

/// Serialise `what` (a property, or a whole container) the way a save of
/// `doc` would write it. Never throws; `ok` is false on failure.
AppExport CapturedValue captureValue(const Document& doc, const Base::Persistence& what);
AppExport CapturedValue captureValue(const CaptureConfig& config, const Base::Persistence& what);

/// Restore a property from a captured value: the fragment through
/// Property::Restore, each attachment through RestoreDocFile, then
/// Property::afterRestore() -- at once, or when the innermost RestoreBatch
/// on this thread finishes.
AppExport void restoreValue(Property& prop, const CapturedValue& value);

/** Values restored together (docs/TransactionLog.md sec 27.67). A document's
 * restore reads every property before any afterRestore(), and some depend
 * on it: an expression engine installs the expressions it read there, and
 * one naming `Constraints[3]` needs the constraints restored first. While a
 * batch lives on a thread, restoreValue() leaves afterRestore() to the
 * batch's finish() (or its destructor), which runs them in restore order.
 * A property removed meanwhile, or one of an object deleted, is dropped
 * from it: restoring a copy-on-change link's target makes the link remove
 * and add again the properties it mirrors.
 */
class AppExport RestoreBatch
{
public:
    RestoreBatch();
    ~RestoreBatch();
    RestoreBatch(const RestoreBatch&) = delete;
    RestoreBatch& operator=(const RestoreBatch&) = delete;
    void finish();
    /// The innermost batch on this thread, or null.
    static RestoreBatch* current();
    void defer(Property& prop);

private:
    void forget(const Property* prop);
    std::vector<Property*> _props;
    RestoreBatch* _outer;
    bool _finished {false};
    fastsignals::scoped_connection _removed;
    fastsignals::scoped_connection _deleted;
};

/** The names a capture writes for objects that have left the document
 * (docs/TransactionLog.md sec 24.3). A link's value is its target's name,
 * and DocumentObject::getExportName() has none for a detached object -- but
 * a transaction that removed an object still holds the name it had, and a
 * before value linking to it must say so. While one of these lives on a
 * thread, getExportName() of a detached object answers from it.
 */
class AppExport CaptureNames
{
public:
    explicit CaptureNames(std::unordered_map<const DocumentObject*, std::string> names);
    ~CaptureNames();
    CaptureNames(const CaptureNames&) = delete;
    CaptureNames& operator=(const CaptureNames&) = delete;
    /// The name of detached `obj` in the innermost scope, or null.
    static const std::string* find(const DocumentObject* obj);

private:
    std::unordered_map<const DocumentObject*, std::string> _names;
    CaptureNames* _outer;
};

/** The document a capture on this thread is for, or null outside one
 * (docs/TransactionLog.md sec 27.67). A detached copy of a property has no
 * container: the undo system's copy at the first write, a value the log
 * copies at commit. A link names its target by the owner's document, and
 * PropertyXLink::Save of an ownerless copy writes relative to this one --
 * without it, it wrote nothing, and every App::Link target in the log was
 * empty.
 */
AppExport const Document* capturingDocument();

/// SHA-1 hex of `bytes`, spelled as FileBlobManager spells it.
AppExport std::string hashBytes(const std::string& bytes);

} // namespace App

#endif // APP_TRANSACTION_VALUE_H
