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
#include <unordered_set>
#include <vector>

#include <FCGlobal.h>
#include <Base/Persistence.h>

namespace App
{

class Document;
class FileBlobManager;
class PropertyFileIncluded;

/** One content-addressed file living in a document's transient directory.
 *
 * A blob is shared by every referrer that holds a handle to it -- properties,
 * undo transaction snapshots, the clipboard -- and is deleted from disk only
 * when the last handle goes away. That is the whole point: before this,
 * ~PropertyFileIncluded deleted its file unconditionally, so two properties
 * could never name the same file and Copy() had to duplicate the bytes.
 *
 * A blob is pure content: it is identified by, and stored under, the hash of
 * its bytes. Names belong to the referring property, which persists its own
 * file name and original path, so any number of properties can share one blob
 * while each keeps the name the user gave it.
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
    uint64_t size() const { return _size; }

private:
    friend class FileBlobManager;
    FileBlob() = default;

    FileBlobManager* _owner {nullptr};
    std::string _hash;
    std::string _path;
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
     * Returns a handle to the existing blob when the content is already
     * stored, whatever the referring properties call it, so importing the
     * same image twice costs one file on disk.
     */
    FileBlobHandle insertFile(const char* srcPath);

    /** Adopt a file that is already inside the transient directory, moving it
     * to its content-addressed location. Used by the restore path, which
     * streams archive content to a staging path first.
     */
    FileBlobHandle adoptFile(const char* path);

    /// Existing blob for a content hash, or null. Never creates.
    FileBlobHandle find(const std::string& hash) const;

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

    /** Re-anchor every stored path after the transient directory has moved.
     *
     * The directory is renamed with its contents, so the content is still
     * there and still under its own hash -- only the recorded paths are stale.
     * Restoring an unpacked project hits this: the blobs are copied in before
     * Document.xml restores the Uid, which is what names the directory.
     */
    void relocate();

    /// Staging path in the transient dir, for content whose hash is not known
    /// yet because it has still to be streamed in.
    std::string uniquePath(const std::string& name) const;

    /** @name Archive blob table (schema 5 and later)
     *
     * Blobs deliberately do not travel through Writer::addFile/writeFiles.
     * That channel matches entries to consumers by walking both lists forward
     * in step, so it can only carry content owned by exactly one property in
     * exactly one place; shared content cannot satisfy that ordering and was
     * silently dropped when it tried. The manager writes its own entries
     * instead, named "blobs/<hash>", directly behind Document.xml, and claims
     * them again on restore through Base::XMLReader's archive handler.
     */
    //@{
    /// Prefix of the archive entries holding stored content.
    static const char* archivePrefix();

    /// Where a save puts the content the document refers to.
    enum class BlobFormat
    {
        /// Below schema 5: nothing, the properties carry their own copies.
        None,
        /// One archive entry per distinct content, named by hash.
        Entries,
        /// A base64 table inside Document.xml, ahead of the object data.
        InlineXml,
    };

    /** Start collecting the blobs a save actually references.
     *
     * The writer decides the format here, once, before anything referring to
     * the content has been written -- so every referrer serializes the same
     * way whatever the answer is, and only this manager has to care.
     */
    void beginSave(Base::Writer& writer);
    /// Format chosen for the save in progress.
    BlobFormat blobFormat() const;
    /// Whether this save writes a `<Blobs>` element, i.e. there is one to read.
    bool hasInlineBlobs() const;
    /// Record that the document being written refers to this blob.
    void noteReferenced(const FileBlobHandle& blob);
    /** Take back a reference noted for the save in progress.
     *
     * The collect pass runs before a single property has been written, so it
     * has to note every blob it can see. A property that then decides to
     * write itself out with no content -- the shape store below schema 5
     * (docs/SharedShapeStorage.md) -- says so here, or the archive carries
     * content nothing in the file refers to.
     */
    void dropReferenced(const FileBlobHandle& blob);
    /** Write one entry per collected blob.
     *
     * Must run while the writer is between entries and before anything
     * registers a file of its own, i.e. straight after Document.xml.
     */
    void writeBlobs(Base::Writer& writer);

    /** Write the collected content as a base64 table inside Document.xml.
     *
     * Must run at the head of the document element, ahead of everything that
     * can refer to it. A no-op unless the format is InlineXml.
     */
    void writeInlineBlobs(Base::Writer& writer);

    /// Read back a `<Blobs>` table, storing each entry under its own hash.
    void restoreInlineBlobs(Base::XMLReader& reader);

    /// Claim this document's blob entries out of the archive being read.
    void beginRestore(Base::XMLReader& reader);
    /** Note that a property refers to this content.
     *
     * The property is handed its blob as soon as the content is available:
     * immediately when it has already been read (a view property restored
     * from a replayed string long after the archive was closed), otherwise at
     * dispatchPending() once the entries have been drained.
     */
    void addPendingReferrer(const std::string& hash, PropertyFileIncluded* prop);
    /// Withdraw a referrer that died before it could be served.
    void removePendingReferrer(PropertyFileIncluded* prop);
    /// Hand every waiting referrer its blob.
    void dispatchPending();
    /** Drop the manager's own hold on restored content.
     *
     * Restored blobs are held from the moment they are read until the last
     * point a referrer can appear -- which is after the finish-restore signal,
     * because that is when embedded view documents are replayed. What is still
     * held then is content no property claimed, and it goes.
     */
    void endRestore();
    //@}

    /// Directory holding the content-addressed files, created on demand.
    std::string blobDir() const;

    /// Where the content with this hash lives: <transient>/blobs/<hash>.
    std::string blobPath(const std::string& hash) const;

private:
    friend class FileBlob;
    /// The collected save set in hash order, so a document always writes the
    /// same file for the same content.
    std::vector<FileBlobHandle> collected() const;
    /// Called from ~FileBlob: drop the map entry and unlink the file.
    void release(FileBlob* blob);
    /// Stream one archive entry into the store, keyed by what it contains.
    void readBlobEntry(Base::Reader& entry);
    /// Take copies of the content of an unpacked project's blob directory.
    void restoreFromDirectory(const std::string& dir);
    /// Keep restored content alive until its referrers have been served.
    void hold(FileBlobHandle blob);
    FileBlobHandle make(const std::string& hash, const std::string& path, uint64_t size);

    Document* _doc {nullptr};
    mutable std::mutex _mutex;
    /// Format the save in progress writes its content in, see beginSave().
    BlobFormat _format {BlobFormat::None};
    /// Blobs this save references, keyed by hash.
    mutable std::unordered_map<std::string, FileBlobHandle> _saveSet;
    /// Properties waiting for content that is still to be read.
    std::vector<std::pair<std::string, PropertyFileIncluded*>> _pending;
    /// Keeps restored content alive until every referrer has been served.
    std::unordered_map<std::string, FileBlobHandle> _restoreHold;
    std::unordered_map<std::string, std::weak_ptr<FileBlob>> _blobs;
};

}  // namespace App

#endif  // APP_FILEBLOBMANAGER_H
