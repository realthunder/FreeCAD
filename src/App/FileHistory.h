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

#include <memory>
#include <string>
#include <vector>

#include <FCGlobal.h>

namespace App
{

class Document;
class FileBlobManager;

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
 * 5.b: the blob manager, the directory and the registry. The log's shared
 * half -- the store, its worker, the counters -- moves here in 5.c, when a
 * second document on one file first needs it.
 */
class AppExport FileHistory: public std::enable_shared_from_this<FileHistory>
{
public:
    /// A history in `home`'s transient directory, not yet registered.
    static std::shared_ptr<FileHistory> create(Document& home);
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

    /// The canonical path the history is registered under, empty if none.
    const std::string& path() const { return _path; }
    /** Register under the file at `path` (an empty path unregisters). A
     * path another live history holds is left to it: false, with a
     * warning -- one file, one history (sec 27.5).
     */
    bool setPath(const std::string& path);
    /// The history registered for the file at `path`, null if none.
    static std::shared_ptr<FileHistory> find(const std::string& path);
    /// The canonical form of `path` the registry keys by.
    static std::string canonicalPath(const std::string& path);

private:
    explicit FileHistory(Document& home);

    Document* _home {nullptr};
    std::string _dir;
    std::string _path;
    std::unique_ptr<FileBlobManager> _blobs;
};

} // namespace App

#endif // APP_FILE_HISTORY_H
