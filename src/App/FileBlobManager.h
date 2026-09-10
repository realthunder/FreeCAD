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
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <FCGlobal.h>
#include <Base/Persistence.h>

namespace App
{

class Document;
class DocumentObject;
class FileBlobManager;
class Property;
class PropertyFileIncluded;

/** Who refers to a blob, for the purpose of naming the file it is saved to.
 *
 * A blob is content and its identity is its hash, but the file a save
 * writes it to has a name, and for a project saved as a directory that name
 * is what version control follows. Deriving it from the referring property --
 * `Box.Image.png` -- makes an edit a modification of one file rather than the
 * delete plus add of an opaque hash that content-addressed entry names forced.
 */
struct BlobReferrer
{
    /** Id of the object the referrer belongs to, 0 when it has none.
     *
     * The lowest id present wins the name. The id also travels in the content
     * index as a generation token, because an object's internal name is
     * reused after a delete and its id (App::DocumentObject::_Id) never is.
     */
    long id {0};
    /// `Object.Property`, the derived name without its extension. Empty when
    /// nothing can name the content, which leaves it named by its hash.
    std::string name;
    /// Extension the referrer stores its file under, leading dot included, so
    /// a saved blob still opens in whatever handles that type.
    std::string ext;
};

/// One line of the content index: what a file holds and who refers to it.
struct BlobIndexEntry
{
    std::string hash;
    std::vector<std::string> referrers;
};

/** One immutable file living in a document's transient directory.
 *
 * A blob is shared by every referrer that holds a handle to it -- properties,
 * undo transaction snapshots, the clipboard -- and is deleted from disk only
 * when the last handle goes away. That is the whole point: before this,
 * ~PropertyFileIncluded deleted its file unconditionally, so two properties
 * could never name the same file and Copy() had to duplicate the bytes.
 *
 * A blob is pure content: it is identified by the hash of its bytes. Names
 * belong to the referring property, which persists its own file name and
 * original path, so any number of properties can share one blob while each
 * keeps the name the user gave it. The file in the transient directory is
 * named by a uuid rather than by the hash -- at insertFile() time there may
 * be no referrer at all, and the name there is a cache detail nothing may
 * depend on; only the extension is kept, so the path handed to a consumer
 * still says what the content is.
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

/** A property that stores its value as blob content.
 *
 * Both directions of that are here, and both are keyed on this interface
 * rather than on `PropertyFileIncluded` because a shape property is an
 * ordinary referrer too (`docs/SharedShapeStorage.md` sec 12.3), a material
 * card is another (`docs/MaterialStorage.md` sec 8), and they have nothing
 * else in common -- one stores a file the user gave it, the next serializes
 * geometry, the third a material.
 */
class AppExport BlobReferrerProperty
{
public:
    virtual ~BlobReferrerProperty() = default;

    /** Take the content the manager restored on this property's behalf.
     *
     * Called once the archive entry holding the content has been read, or
     * straight away when it had been read already. Not a value change: it
     * completes the restore of a value the document already had, so it must
     * not touch the document.
     */
    virtual void assignRestoredBlob(const FileBlobHandle& blob) = 0;

    /** Note the blobs this property holds, for the save in progress.
     *
     * The collect pass runs before any property is written, and content it
     * does not see gets no archive entry, so this is where a property says
     * what it holds. It is also where the naming is decided: an owner that
     * wants a particular extension or referrer passes it to
     * FileBlobManager::noteReferenced itself rather than returning a handle.
     *
     * \a object supplies the id for a property whose own container has none,
     * i.e. a view provider's.
     *
     * Implement it as nothing only if the property notes its own content at
     * write time instead, and say so there -- silence here is content
     * missing from the archive.
     */
    virtual void collectBlobs(FileBlobManager& manager, const DocumentObject* object) const = 0;

    /** Whether the content this property refers to is LOST when the save
     * writes no store.
     *
     * The store exists at schema 5 and above only, and most referrers have a
     * schema 4 spelling that keeps the data: PropertyFileIncluded writes its
     * own copy of the file, a shape property writes the shape the old way and
     * forfeits sharing rather than content. A referrer with nothing to fall
     * back on answers true, and the save path offers the schema its content
     * needs rather than dropping it silently.
     */
    virtual bool blobContentNeedsStore() const { return false; }

    /** Tell a referrer the content it is waiting for is not coming.
     *
     * Called once the archive is drained and the blob this property asked for
     * was not in it, which is the point at which "not read yet" becomes "not
     * there". A property that can stand in for the missing content -- from a
     * library, a default, anything it can name without inventing it -- does so
     * here and answers true, so the document degrades instead of losing the
     * value outright.
     *
     * Whatever it puts in place is a stand-in, not the content: the property
     * still owes the document the reference it was restored from, so saving
     * must write back what the file said rather than what the stand-in is.
     */
    virtual bool blobUnavailable() { return false; }

    /** Extension the content should be stored under, without the dot.
     *
     * Only names things -- the archive entry, and the file inside an unpacked
     * project -- so that what version control sees says what it holds. Empty
     * leaves the derived name without one.
     */
    virtual std::string blobExtension() const { return {}; }
};

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
 * matching entries to consumers sequentially by name. It therefore owns the
 * names those entries go under, and an index recording them -- see
 * indexName() and noteReferenced().
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
     *
     * The extension is the one the content should be stored under; null takes
     * the source file's own, which is right unless the caller is renaming it.
     */
    FileBlobHandle insertFile(const char* srcPath, const char* extension = nullptr);

    /** Adopt a file that is already inside the transient directory, moving it
     * to its place in the store. Used by the restore path, which streams
     * archive content to a staging path first.
     *
     * The extension is the one the content should be stored under. Null takes
     * the incoming file's own, which is right for a scratch file named after
     * what it holds and wrong for a staging path called "blob.part" -- so a
     * caller streaming into one passes what it knows, and an empty string
     * when it knows nothing.
     */
    FileBlobHandle adoptFile(const char* path, const char* extension = nullptr);

    /// Existing blob for a content hash, or null. Never creates.
    FileBlobHandle find(const std::string& hash) const;

    /// Every live blob, i.e. exactly the set a save must write.
    std::vector<FileBlobHandle> blobs() const;

    /// Transient directory of the owning document.
    std::string transientPath() const;

    /// Content hash of a file, or an empty string if it cannot be read.
    static std::string hashFile(const char* path);
    /// Content hash of bytes held in memory, spelled as hashFile() spells it,
    /// so a value computed here matches the blob the same bytes become.
    static std::string hashBytes(const std::string& bytes);

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

    /** Where a blob's file is now, after the transient directory has moved.
     *
     * The directory is renamed with its contents, so the file keeps the name
     * it was given -- only the directory leading to it is stale.
     */
    std::string relocatedPath(const FileBlobHandle& blob) const;

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
     * instead, under "blobs/", directly behind Document.xml, and claims them
     * again on restore through Base::XMLReader's archive handler.
     */
    //@{
    /// Prefix of the archive entries holding stored content.
    static const char* archivePrefix();

    /** Name of the content index, inside the blob directory.
     *
     * The index binds a saved file's name to its content and to the referrers
     * that justify it. It is not load-bearing for a restore -- identity stays
     * the hash in Document.xml -- so a file written before it existed, or one
     * whose index a user deleted, still opens; it costs a one-time rename on
     * the next save. What the index buys is the *next* save: the name a file
     * already has, the proof that its content is unchanged, and the list of
     * names that may be pruned.
     */
    static const char* indexName();

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
    /** Which save this is, counted across the whole process.
     *
     * A consumer that has to build something once per save and throw it away
     * afterwards has no other way to tell one save from the next: there is no
     * end-of-save signal, and a document can be saved any number of times.
     * Comparing this against what it built for is what says the answer is
     * stale (docs/SharedShapeStorage.md sec 12.4).
     *
     * *** Counted globally, not per document, and that is load-bearing: a
     * closed document's address is handed straight back to the next one, so a
     * per-document count would let a new document's first save be mistaken
     * for a stale answer built for a dead one.
     */
    uint64_t saveGeneration() const;
    /// Format chosen for the save in progress.
    BlobFormat blobFormat() const;
    /// Whether this save writes a `<Blobs>` element, i.e. there is one to read.
    bool hasInlineBlobs() const;
    /** Record that the document being written refers to this blob.
     *
     * The referrer is what names the file the blob is saved to, so a caller
     * that knows which property it is walking should say so. Noting the same
     * blob again with a different referrer adds to the list rather than
     * replacing it: shared content is one file with several referrers.
     */
    void noteReferenced(const FileBlobHandle& blob, const BlobReferrer& referrer = {});

    /** Identify a referrer for naming.
     *
     * \a object supplies the id when the property's own container has none --
     * a view provider's properties are named after, and belong to the
     * generation of, the object it presents.
     */
    static BlobReferrer referrerOf(const Property* prop,
                                   const DocumentObject* object = nullptr);
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
     * from a replayed string while the archive is still open), otherwise at
     * dispatchPending() once the entries have been drained.
     *
     * Arriving after endRestore() is an error, not a wait: nothing dispatches
     * any more and the property would be left empty in silence. It is logged.
     * A tier that restores that late has to be restored inside the load --
     * which is why Gui::Document refuses to park a view provider whose record
     * names a blob.
     */
    void addPendingReferrer(const std::string& hash, BlobReferrerProperty* prop);
    /// Withdraw a referrer that died before it could be served.
    void removePendingReferrer(BlobReferrerProperty* prop);
    /// Hand every waiting referrer its blob.
    void dispatchPending();
    /** Drop the manager's own hold on restored content.
     *
     * Restored blobs are held from the moment they are read until the last
     * point a referrer can appear -- which is after the finish-restore signal,
     * because that is when embedded view documents are replayed. What is still
     * held then is content no property claimed, and it goes.
     *
     * This is also the deadline every restoring tier answers to: anything
     * still to be restored after it cannot be served, so it must not be
     * deferred past it.
     */
    void endRestore();
    //@}

    /// Directory holding the stored files, created on demand.
    std::string blobDir() const;

    /// A free path in the store for content that is about to be stored there.
    std::string newBlobPath(const char* extension) const;

private:
    friend class FileBlob;
    /// One file a save is about to write: the name it goes under, the content
    /// it holds and the referrer tokens that justify the name.
    struct SaveEntry
    {
        std::string name;
        FileBlobHandle blob;
        std::vector<std::string> referrers;
    };

    /// The collected save set in hash order, so a document always writes the
    /// same file for the same content.
    std::vector<FileBlobHandle> collected() const;
    /** Decide what every collected blob is called, in name order.
     *
     * \a previous is the index the target directory already holds, which
     * pins names that are still current -- see the implementation for why a
     * name has to be pinned rather than simply re-derived.
     */
    std::vector<SaveEntry> planSave(const std::map<std::string, BlobIndexEntry>& previous) const;
    /// Parse a content index, or nothing if it is absent or unreadable.
    static std::map<std::string, BlobIndexEntry> readIndex(const std::string& path);
    /// Write the content index as the first entry of the blob directory.
    static void writeIndex(Base::Writer& writer, const std::vector<SaveEntry>& entries);
    /// Remove the files the previous index listed and this save did not write.
    static void prune(const std::string& dir,
                      const std::map<std::string, BlobIndexEntry>& previous,
                      const std::set<std::string>& kept);
    /// Called from ~FileBlob: drop the map entry and unlink the file.
    void release(FileBlob* blob);
    /// Stream one archive entry into the store, keyed by what it contains.
    /// The entry name is trusted for nothing but the extension.
    void readBlobEntry(const std::string& name, Base::Reader& entry);
    /// Take copies of the content of an unpacked project's blob directory.
    void restoreFromDirectory(const std::string& dir);
    /// Keep restored content alive until its referrers have been served.
    void hold(FileBlobHandle blob);
    FileBlobHandle make(const std::string& hash, const std::string& path, uint64_t size);

    Document* _doc {nullptr};
    mutable std::mutex _mutex;
    /// Format the save in progress writes its content in, see beginSave().
    BlobFormat _format {BlobFormat::None};
    /// Which save is in progress, see saveGeneration().
    uint64_t _generation {0};
    /// Blobs this save references, keyed by hash.
    mutable std::unordered_map<std::string, FileBlobHandle> _saveSet;
    /// Who refers to each of them, which is what names the file it goes to.
    std::unordered_map<std::string, std::vector<BlobReferrer>> _saveRefs;
    /// What each of them is, for the ones nothing gets to name: the
    /// extension of the first referrer that offered one. A file named by
    /// its hash still has to say what it holds -- a stored environment
    /// image that came back as a nameless blob was read by whoever
    /// looked at its extension as not being a Radiance picture.
    std::unordered_map<std::string, std::string> _saveExts;
    /// Properties waiting for content that is still to be read.
    std::vector<std::pair<std::string, BlobReferrerProperty*>> _pending;
    /// Whether endRestore() has run, i.e. whether a referrer turning up now
    /// can still be served. See addPendingReferrer().
    bool _restoreClosed {false};
    /// Keeps restored content alive until every referrer has been served.
    std::unordered_map<std::string, FileBlobHandle> _restoreHold;
    std::unordered_map<std::string, std::weak_ptr<FileBlob>> _blobs;
};

}  // namespace App

#endif  // APP_FILEBLOBMANAGER_H
