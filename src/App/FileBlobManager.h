/***************************************************************************
 *   Copyright (c) 2026 FreeCAD contributors                               *
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

#ifndef APP_FILEBLOBMANAGER_H
#define APP_FILEBLOBMANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <FCGlobal.h>

namespace App
{

class Document;
class FileBlobManager;

/** One content-addressed file living in a document's transient directory.
 *
 * A blob is shared by every referrer that holds a handle to it -- properties,
 * undo transaction snapshots, the clipboard -- and is deleted from disk only
 * when the last handle goes away. That is the whole point: before this,
 * ~PropertyFileIncluded deleted its file unconditionally, so two properties
 * could never name the same file and Copy() had to duplicate the bytes.
 *
 * Blobs are immutable. Changing a property's file always produces a new blob;
 * the file on disk is kept read-only to enforce that.
 */
class AppExport FileBlob
{
public:
    ~FileBlob();

    FileBlob(const FileBlob&) = delete;
    FileBlob& operator=(const FileBlob&) = delete;

    /// Store this blob belongs to. Blobs do not migrate between documents;
    /// crossing documents means importing the content into the target store.
    FileBlobManager* owner() const { return _owner; }
    /// Content hash, the blob's identity and its key in the manager.
    const std::string& hash() const { return _hash; }
    /// Absolute path in the owning document's transient directory.
    const std::string& path() const { return _path; }
    /// Name the blob is saved under, and the base for its transient file name.
    const std::string& baseName() const { return _baseName; }
    uint64_t size() const { return _size; }

private:
    friend class FileBlobManager;
    FileBlob() = default;

    FileBlobManager* _owner {nullptr};
    std::string _hash;
    std::string _path;
    std::string _baseName;
    uint64_t _size {0};
};

using FileBlobHandle = std::shared_ptr<FileBlob>;

/** Per-document store of the files referenced by PropertyFileIncluded.
 *
 * Ownership is deliberately per document rather than per application. A
 * document knows all of its own referrers, so its counts need no locking
 * against other processes -- which matters because documents are meant to move
 * into separate processes eventually. Cross-document de-duplication is a
 * separate, best-effort concern: losing it costs disk, never data.
 *
 * The manager is also the only file-channel consumer at save time. It writes
 * one archive entry per blob and properties serialize just the hash, which
 * sidesteps Base::Writer::addFile renaming colliding names and ZipReader
 * matching entries to consumers sequentially by name.
 */
class AppExport FileBlobManager
{
public:
    explicit FileBlobManager(Document* doc);
    ~FileBlobManager();

    FileBlobManager(const FileBlobManager&) = delete;
    FileBlobManager& operator=(const FileBlobManager&) = delete;

    /** Take a copy of an external file into the store.
     *
     * Returns a handle to an existing blob when the same content is already
     * stored under the same name, so importing the same image twice costs one
     * file on disk. Same content under a *different* name is a distinct blob
     * with its own copy: the name is visible to callers and is what the file
     * is saved under, so it cannot be collapsed onto another name.
     * @param srcPath   file to import; must exist
     * @param name      preferred save name; defaults to srcPath's file name
     */
    FileBlobHandle insertFile(const char* srcPath, const char* name = nullptr);

    /** Adopt a file that is already inside the transient directory, without
     * copying it. Used by the restore path, which streams archive content
     * straight to its final location.
     */
    FileBlobHandle adoptFile(const char* path, const char* name = nullptr);

    /// Existing blob for a content hash and name, or null. Never creates.
    FileBlobHandle find(const std::string& hash, const std::string& name) const;

    /// Every live blob, i.e. exactly the set a save must write.
    std::vector<FileBlobHandle> blobs() const;

    /// Transient directory of the owning document.
    std::string transientPath() const;

    /// Content hash of a file, or an empty string if it cannot be read.
    static std::string hashFile(const char* path);

    /** Store for properties that have no owning document.
     *
     * Backed by the system temporary directory. Reference counting works the
     * same; only cross-document de-duplication is meaningless here.
     */
    static FileBlobManager& defaultManager();

    /** Point a blob at a new location after its file has moved.
     *
     * Saving a document under a new name changes TransientDir, which leaves
     * every stored path stale. The content, and therefore the hash, is
     * unchanged, so only the path is updated.
     */
    void repath(const FileBlobHandle& blob, const std::string& path);

    /// Staging path in the transient dir, for content whose hash is not known
    /// yet because it has still to be streamed in.
    std::string uniquePath(const std::string& name) const;

    /** Directory a blob's file lives in, one per content hash.
     *
     * Blobs are stored as <transient>/blobs/<hash>/<name> rather than flat in
     * the transient directory, so a file always carries its real name even
     * when another blob is already using that name -- which happens routinely
     * now that an undo snapshot keeps the previous content alive.
     */
    std::string blobDir(const std::string& hash) const;

private:
    friend class FileBlob;
    /// Called from ~FileBlob: drop the map entry and unlink the file.
    void release(FileBlob* blob);
    FileBlobHandle make(const std::string& hash, const std::string& path,
                        const std::string& name, uint64_t size);
    /// Map key: content plus name, since both are visible to callers.
    static std::string key(const std::string& hash, const std::string& name);

    Document* _doc {nullptr};
    mutable std::mutex _mutex;
    std::unordered_map<std::string, std::weak_ptr<FileBlob>> _blobs;
};

}  // namespace App

#endif  // APP_FILEBLOBMANAGER_H
