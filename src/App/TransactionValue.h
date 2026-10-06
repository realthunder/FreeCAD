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

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <FCGlobal.h>
#include <fastsignals/signal.h>

#include "StringHasher.h"

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

/** The names the values restored on this thread are read through
 * (docs/TransactionLog.md sec 30.13, 30.4 P4). A fork's new object whose
 * name this file has given to another since arrives under a new one, and
 * what names it follows: a link's target, an object in a sub-object path,
 * an expression. While one of these lives, restoreValue() reads through
 * it the way a paste reads through its reader's map. One at a time: an
 * expression is told through ExpressionParser::ExpressionImporter, which
 * does not nest.
 */
class AppExport RestoreNames
{
public:
    explicit RestoreNames(std::map<std::string, std::string> names);
    ~RestoreNames();
    RestoreNames(const RestoreNames&) = delete;
    RestoreNames& operator=(const RestoreNames&) = delete;
    /// The innermost scope on this thread, or null.
    static const RestoreNames* current();
    /// What `name` is here: itself, unless the scope renames it.
    const char* map(const char* name) const;
    bool empty() const { return _names.empty(); }

private:
    struct Importing;
    std::map<std::string, std::string> _names;
    std::unique_ptr<Importing> _importing;
    RestoreNames* _outer;
};

/** The string table the values restored on this thread name their strings
 * in (docs/TransactionLog.md sec 30.13 F1, 30.16). A value of another copy
 * of the file names string ids of that copy's table; in this file's the
 * same numbers are other strings, or none. While one of these lives, a
 * restore reads the ids of an element map, of a reference's shadow name
 * and of an expression's element paths as `from`'s, and each string is
 * taken into `to` by content (StringHasher::importID): what the property
 * ends up holding names this file's table only.
 */
class AppExport RestoreStrings
{
public:
    RestoreStrings(StringHasherRef from, StringHasherRef to);
    ~RestoreStrings();
    RestoreStrings(const RestoreStrings&) = delete;
    RestoreStrings& operator=(const RestoreStrings&) = delete;
    /// The innermost scope on this thread, or null.
    static RestoreStrings* current();
    const StringHasherRef& from() const { return _from; }
    const StringHasherRef& to() const { return _to; }
    /** The element part of a name, `;<mapped>.<indexed>`, with the ids of
     * its mapped name taken into `to`. One that names a string `from` has
     * not got falls to its indexed name alone: stale ids resolve nothing,
     * or the wrong thing. An element with no mapped name is as it was.
     */
    std::string element(const char* element);
    /// A sub-object path, its element part through element().
    std::string sub(const std::string& sub);
    /** The object the sub-object paths read on this thread start from,
     * while one of these lives: a link's target, set by the link as it
     * reads its paths. An element's name says which thing of its object
     * it is by a number the object gave (RestoreMinted), and a path that
     * names no object of its own is an element of this one.
     */
    class AppExport Target
    {
    public:
        explicit Target(const std::string& object);
        ~Target();
        Target(const Target&) = delete;
        Target& operator=(const Target&) = delete;

    private:
        const std::string* _outer;
    };
    /// The ids of `to` the text element() or sub() last returned as `text`
    /// names, which whoever keeps the text holds.
    QVector<StringIDRef> held(const std::string& text) const;
    /// The id in `to` of `from`'s string `id`, 0 when it cannot be had.
    long id(long id);

private:
    StringHasherRef _from;
    StringHasherRef _to;
    StringHasher::ImportMemo _memo;
    std::map<std::string, QVector<StringIDRef>> _held;
    std::vector<StringIDRef> _ids;
    RestoreStrings* _outer;
};

/** The numbers the objects of a document gave the things they hold, as
 * another copy of the file gave them (docs/TransactionLog.md sec 31.14).
 * Two copies of a file number on from one counter: the fifth line of a
 * sketch is `g5` in each, and they are two lines. An import gives what the
 * copy made numbers of this file (DocumentObject::importMintedIds); while
 * one of these lives on the thread, the names that carry the numbers --
 * a reference's `;g5;SKT`, the string an element name is built on -- are
 * read the same way (DocumentObject::importMintedName).
 */
class AppExport RestoreMinted
{
public:
    /// An object's id here, to the copy's numbers and this file's.
    using Maps = std::map<long, std::map<long, long>>;
    RestoreMinted(Document& doc, const Maps& maps);
    ~RestoreMinted();
    RestoreMinted(const RestoreMinted&) = delete;
    RestoreMinted& operator=(const RestoreMinted&) = delete;
    /// The innermost scope on this thread, or null.
    static const RestoreMinted* current();
    /// `name`, an element's name of the object with id `id` here, as this
    /// file numbers it. False when it is as it was.
    bool byId(long id, std::string& name) const;
    /// The same, of the object called `object` here.
    bool byName(const std::string& object, std::string& name) const;

private:
    Document& _doc;
    const Maps& _maps;
    const RestoreMinted* _outer;
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
