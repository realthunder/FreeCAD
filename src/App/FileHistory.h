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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <FCGlobal.h>

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
    std::string _dir;
    std::string _path;
    std::unique_ptr<FileBlobManager> _blobs;
    std::shared_ptr<TransactionLogCore> _logCore;
};

} // namespace App

#endif // APP_FILE_HISTORY_H
