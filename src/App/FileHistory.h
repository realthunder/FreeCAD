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

#ifndef APP_FILE_HISTORY_H
#define APP_FILE_HISTORY_H

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <FCGlobal.h>
#include <App/StringHasher.h>

namespace App
{

class Document;
class FileBlobManager;
class TransactionLogCore;

/** What the documents of one physical file share (docs/TransactionLog.md
 * sec 27.5, 27.7): one log per file, where it used to be one document per
 * file, and 16.2's one blob manager per history.
 *
 * A history lives in a directory -- `history/` for the log's database,
 * `blobs/` for the blob store -- which is the transient directory of the
 * document that made it, its home, and follows that directory when a
 * restore renames it. Documents hold it by reference count; the last one
 * to let go removes the directory. It is registered under the canonical
 * path of the file once the file has one, so that another document opened
 * from the same file can find it (sec 27.8, 5.c).
 *
 * It holds the blob manager, the directory, the registry, and the shared
 * half of the log (TransactionLogCore): the store, its worker, the
 * counters. Each document's TransactionLog is a cursor on one branch.
 */
class AppExport FileHistory: public std::enable_shared_from_this<FileHistory>
{
public:
    /// A history in `home`'s transient directory, not yet registered.
    static std::shared_ptr<FileHistory> create(Document& home);
    /** The history of the file at `path` with no document of it open
     * (docs/TransactionLog.md sec 27.13): the one registered, or else the
     * one embedded in the file, read straight out of the archive -- its
     * blobs split into a store of its own, the copy adopted under the guard
     * of 16.4 (closed branches when the file was edited elsewhere), and the
     * file as found recorded as the version its copy numbers next. Null,
     * with `reason` set, when the file has no history or cannot be read.
     */
    static std::shared_ptr<FileHistory> openFile(const std::string& path,
                                                 std::string* reason = nullptr);
    ~FileHistory();

    FileHistory(const FileHistory&) = delete;
    FileHistory& operator=(const FileHistory&) = delete;

    /// The directory the log and the blob store live in.
    const std::string& directory() const { return _dir; }
    /// The home document's transient directory was renamed to `dir`.
    void setDirectory(const std::string& dir) { _dir = dir; }
    /// The document whose transient directory this is, null once it has
    /// closed while others still hold the history.
    Document* home() const { return _home; }
    /// The home document is going; the directory stays with the history.
    void releaseHome(const Document& doc);

    /// The blob store of every document of the file; made on first use.
    FileBlobManager& blobs();
    /// The blob store if it was made, else null.
    FileBlobManager* blobsIfMade() const { return _blobs.get(); }

    /// The shared half of the file's log (TransactionLog.cpp), made by the
    /// first document's log and kept for as long as the history is.
    std::shared_ptr<TransactionLogCore>& logCore() { return _logCore; }

    /** The file's object ids (docs/TransactionLog.md sec 27.40 item 1): the
     * last id any document of the file handed out or found. Every document
     * of the file allocates from it, so no two objects of the file, in any
     * version or branch, share an id -- and a reopen does not hand out a
     * deleted object's id again.
     */
    long lastObjectId() const { return _lastObjectId; }
    /// An id in use or once used: the counter goes past it.
    void noteObjectId(long id)
    {
        if (id > _lastObjectId)
            _lastObjectId = id;
    }
    long nextObjectId() { return ++_lastObjectId; }

    /** The file's object names (docs/TransactionLog.md sec 27.40 item 3,
     * 27.41 Q3): one to one, object id <-> name, over every version and
     * branch, and never freed. A new object takes no name the table gives
     * another id; an object coming back -- undo, a version, a branch --
     * has its own.
     */
    /// The id `name` was given to, 0 if none.
    long objectIdOfName(const std::string& name) const
    {
        auto it = _idOfName.find(name);
        return it == _idOfName.end() ? 0 : it->second;
    }
    /// The name the table gives `id`, null if none.
    const std::string* objectNameOfId(long id) const
    {
        auto it = _nameOfId.find(id);
        return it == _nameOfId.end() ? nullptr : &it->second;
    }
    /** `id` has `name`. The first pairing of either stays: a name or an id
     * the table already pairs otherwise is left as it is (history written
     * before the table can hold both; the merge resolves those).
     */
    void noteObjectName(const std::string& name, long id)
    {
        if (name.empty() || id <= 0 || _idOfName.count(name) || _nameOfId.count(id))
            return;
        _idOfName.emplace(name, id);
        _nameOfId.emplace(id, name);
    }
    /** The last geometry id of each object, file wide (docs/TransactionLog.md
     * sec 27.40 item 4, 27.41 Q5): a sketch edited in two branches, or
     * reopened after a deletion, mints no id twice. `floor` is the largest
     * id the object holds itself.
     */
    long nextGeoId(long objectId, long floor)
    {
        long& last = _lastGeoIds[objectId];
        last = std::max(last, floor) + 1;
        return last;
    }
    void noteGeoId(long objectId, long id)
    {
        long& last = _lastGeoIds[objectId];
        if (id > last)
            last = id;
    }
    const std::unordered_map<long, long>& lastGeoIds() const { return _lastGeoIds; }

    /** The bytes of name and geometry-id entries estimated to be referred
     * to by nothing since the last compaction (docs/TransactionLog.md sec
     * 27.48, 27.49): what trimming, deleting a branch and squashing left
     * behind. Kept in the store's meta by the log; compaction resets it.
     */
    std::size_t compactEstimate() const { return _compactEstimate; }
    void setCompactEstimate(std::size_t n) { _compactEstimate = n; }

    /** Compaction (docs/TransactionLog.md sec 27.47): forget the name and
     * the last geometry id of every object not in `used`. The counters stay
     * where they are. Returns the ids forgotten, and in `names` how many
     * names went.
     */
    std::vector<long> forgetObjects(const std::unordered_set<long>& used, std::size_t& names)
    {
        std::unordered_set<long> gone;
        names = 0;
        for (auto it = _nameOfId.begin(); it != _nameOfId.end();) {
            if (used.count(it->first)) {
                ++it;
                continue;
            }
            _idOfName.erase(it->second);
            gone.insert(it->first);
            ++names;
            it = _nameOfId.erase(it);
        }
        for (auto it = _lastGeoIds.begin(); it != _lastGeoIds.end();) {
            if (used.count(it->first)) {
                ++it;
                continue;
            }
            gone.insert(it->first);
            it = _lastGeoIds.erase(it);
        }
        return {gone.begin(), gone.end()};
    }

    /** The file's string hasher (sec 27.40 item 2), the one every document
     * of the file hashes element names with, so one shape has one element
     * map in every version and branch. Null until a document gives it
     * its own (`shareHasher`).
     */
    const StringHasherRef& hasher() const { return _hasher; }
    /** The hasher `doc` should use: the file's, made `own` if the file has
     * none yet; `own` if the file's is another and `own` already holds
     * strings, which stay the document's (a document that hashed before it
     * had its history).
     */
    StringHasherRef shareHasher(const StringHasherRef& own)
    {
        if (!_hasher)
            _hasher = own;
        return own && own != _hasher && own->size() ? own : _hasher;
    }

    /** The string tables, by content hash, this history's hasher has taken
     * in (docs/TransactionLog.md sec 27.50 item 3): a document of the file
     * reads a table at most once.
     */
    void noteTable(const std::string& hash) { _tables.insert(hash); }
    bool tookTable(const std::string& hash) const { return _tables.count(hash) != 0; }
    /** Read the file at `path`'s string table member into the file's
     * hasher, made if there is none (sec 27.50 item 1); the history of a
     * file no document of it has open. False when there is no member.
     */
    bool readTable(const std::string& path);
    /// Every (name, id) pair, in no order.
    const std::unordered_map<std::string, long>& objectNames() const { return _idOfName; }

    /// The canonical path the history is registered under, empty if none.
    const std::string& path() const { return _path; }
    /// What a history opened from a file (openFile) read of it: its label,
    /// and the version number the file as found was recorded as.
    const std::string& fileLabel() const { return _fileLabel; }
    int64_t fileVersion() const { return _fileVersion; }
    /** Register under the file at `path` (an empty path unregisters). A
     * path another live history holds is left to it: false, with a
     * warning -- one file, one history (sec 27.5).
     */
    bool setPath(const std::string& path);
    /// The history registered for the file at `path`, null if none.
    static std::shared_ptr<FileHistory> find(const std::string& path);
    /// The canonical form of `path` the registry keys by.
    static std::string canonicalPath(const std::string& path);
    /// The parts of a name `parseName` reads.
    struct NameParts
    {
        std::string file;   ///< the file's path
        std::string branch; ///< empty for a version (the frozen instance)
        int64_t version = 0; ///< 0 for a branch's tip
    };
    /** The name of a document of a file (docs/TransactionLog.md sec 27.23,
     * 27.24): after the file, `@v<num>` (a version, the frozen instance a
     * pin shows), `@<branch>@v<num>` (an editable instance at a version) or
     * `@<branch>@` (a branch's tip). The file is the longest prefix that is
     * a file or a registered history's path, so a branch name may hold `@`
     * and `@v`. False, with `parts` untouched, for any other name -- one
     * that is a file.
     */
    static bool parseName(const std::string& name, NameParts& parts);
    /** `parseName` reduced to the file: `path` becomes the file's, and the
     * version is returned when the name is the frozen form `<file>@v<num>`,
     * -1 for the other two forms, 0 (with `path` untouched) for a file.
     */
    static int64_t splitVersion(std::string& path);
    /** Whether version `num` of the file at `path` can be opened (sec
     * 27.13): its history opened (and returned in `history`), the version
     * in it, and its uuid `uuid` when one is given. `reason` says why not.
     */
    static bool findVersion(const std::string& path, int64_t num, const std::string& uuid,
                            std::shared_ptr<FileHistory>& history, std::string& reason);

private:
    explicit FileHistory(Document& home);
    explicit FileHistory(const std::string& dir);

    Document* _home {nullptr};
    std::string _fileLabel;
    int64_t _fileVersion {0};
    long _lastObjectId {0};
    std::unordered_map<std::string, long> _idOfName;
    std::unordered_map<long, std::string> _nameOfId;
    StringHasherRef _hasher;
    std::unordered_set<std::string> _tables;
    std::unordered_map<long, long> _lastGeoIds;
    std::size_t _compactEstimate {0};
    std::string _dir;
    std::string _path;
    std::unique_ptr<FileBlobManager> _blobs;
    std::shared_ptr<TransactionLogCore> _logCore;
};

} // namespace App

#endif // APP_FILE_HISTORY_H
