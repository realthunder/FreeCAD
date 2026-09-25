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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <FCGlobal.h>
#include <Base/Persistence.h>

namespace App
{

class BlobArchive;
struct BlobRawMember;
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
 *
 * In a document's store the content is normally not a file at all but a
 * member of the pack store (docs/FileBlobsManager.md sec 15.7-15.10): held
 * compressed in memory until the next batch is written, then a member of a
 * zip segment file. A file is written only for a consumer that asks for a
 * path.
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
    /** Absolute path in the owning document's transient directory.
     *
     * Content in the pack store has no file of its own until something asks
     * for one: the first call here writes it. A caller that only needs the bytes should
     * use read() instead, and one that only needs to know what the content is
     * should use hasExtension() -- either of those asked of thousands of blobs
     * through this is thousands of file creates.
     */
    const std::string& path() const;
    uint64_t size() const { return _size; }

    /** Whether the content has no file of its own, only a pack store member.
     *
     * Such a blob has no path whose existence means anything: asking for one
     * creates it. Code that repairs stale paths skips these.
     */
    bool inArchive() const;

    /// Whether the content is a pack store member, in memory or in a segment,
    /// whether or not it also has a file.
    bool inPack() const;

    /// Whether the content is stored under this extension. Never writes a file.
    bool hasExtension(const char* ext) const;
    /// The extension the content is stored under, without the dot, empty
    /// for none. Never writes a file.
    std::string extension() const;

    /// The whole content, from the file or from the pack store. False, and
    /// \a bytes empty, when it cannot be read.
    bool read(std::string& bytes) const;

    /** The other stored files this content reads, by hash.
     *
     * A shape file that borrows geometry names the files it borrows from
     * (docs/SharedShapeStorage.md sec 11.5); it cannot be read without them,
     * so whoever keeps it for later -- the transaction log
     * (docs/TransactionLog.md sec 23.16) -- has to keep them too. The module
     * that owns a format says how to find them, see
     * FileBlobManager::registerSourceReader(); content with no reader for
     * its extension reads nothing else. Reads the content.
     */
    std::vector<std::string> sources() const;

private:
    friend class FileBlobManager;
    FileBlob() = default;

    FileBlobManager* _owner {nullptr};
    std::string _hash;
    /// Written once by the manager when a packed blob gets its file.
    mutable std::string _path;
    uint64_t _size {0};
    /// Extension of a packed blob, without the dot. A file's is its path's.
    std::string _ext;
    /// Whether the content is a pack store member. Set before the blob is
    /// published and never cleared.
    bool _packed {false};
    /** The logical segment holding the member, 0 while it is only in memory.
     *
     * Logical: a merge sends the segments it empties to the one it wrote
     * through the manager's redirection, so no blob is touched (sec 15.8).
     * Read and written under the manager's lock.
     */
    int _segment {0};
    /// False while the content has no file. Set after _path, so a reader
    /// that sees it set may read _path without the lock.
    mutable std::atomic<bool> _materialized {true};
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

    /** The blob holding this property's whole content as it stands, or null.
     *
     * A property that keeps the file its value was last written to (a
     * shape after a save, until it changes) answers it here so that a
     * reader wanting a content hash -- the transaction log
     * (docs/TransactionLog.md sec 20.2, decision 6b) -- can take the hash
     * and hold the blob instead of serialising the value again. Null
     * means "not known": the caller serialises. It must never name a blob
     * whose content differs from the value.
     */
    virtual FileBlobHandle contentBlob() const { return {}; }
};

/** What noteReferenced() is told on this thread while it lives, instead
 * of the save set.
 *
 * Every referrer's Save notes the blobs it writes the hashes of (the
 * "noted again here" convention), so a capture of a value under one learns
 * exactly which files the value names -- and the manager's save set, which
 * belongs to whatever save the document runs next, is left alone. The
 * transaction log captures values on its worker thread this way
 * (docs/TransactionLog.md sec 23.16). Nests; the innermost records.
 */
class AppExport BlobRecorder
{
public:
    BlobRecorder();
    ~BlobRecorder();

    BlobRecorder(const BlobRecorder&) = delete;
    BlobRecorder& operator=(const BlobRecorder&) = delete;

    /// What was noted, each once, in the order first noted.
    const std::vector<FileBlobHandle>& blobs() const { return _blobs; }

    /// The recorder in effect on this thread, or null.
    static BlobRecorder* current();

private:
    friend class FileBlobManager;
    void add(const FileBlobHandle& blob);

    std::vector<FileBlobHandle> _blobs;
    BlobRecorder* _previous {nullptr};
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

    /** Store bytes already held in memory, writing them only if the content
     * is new.
     *
     * The hash is of the bytes, so whether anything has to be written is known
     * before any file exists: content the store already holds costs no file
     * operation at all, and new content costs one write. This is the restore
     * path's route -- it reads an archive entry rather than staging it -- which
     * leaves adoptFile() to the callers that really do have a file to move.
     *
     * The extension is the one the content should be stored under; null or
     * empty stores it without one.
     */
    FileBlobHandle adoptBytes(const std::string& bytes, const char* extension = nullptr);

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

    /** Close the file handles of the pack store's segments.
     *
     * Must precede anything that renames or deletes the transient directory:
     * Windows refuses either while a file inside it is open. A segment opens
     * again on its next read.
     */
    void closeArchives();

    /** Write the content held in memory into segments.
     *
     * New content is batched (sec 15.7): compressed as it arrives, kept in
     * memory, and written one segment rewrite per batch -- here, at the end
     * of a save and of a restore, or when a segment's worth has gathered.
     * Every segment write is flushed to the disk, directory entry included,
     * before this returns. The segment written to drops its dead members on
     * the way; shrinking, deleting and merging the others is the
     * maintenance worker's.
     */
    void flush();

    /** Make the content of these blobs durable: in a segment on disk.
     *
     * The one ordering rule between the transaction log and this store
     * (sec 15.8): a blob's segment reaches the disk before any log row
     * naming the blob commits. One flush for however many blobs, and none
     * when all of them are there already.
     */
    void makeDurable(const std::vector<FileBlobHandle>& blobs);

    /** Hold off every segment write while this lives.
     *
     * For renaming the transient directory: Windows refuses while a file in
     * it is open, and a segment being written is. Take it after anything
     * that waits on a thread which may write here -- the transaction log's
     * worker makes blobs durable.
     */
    std::unique_lock<std::mutex> holdWrites();

    /// Stop the maintenance worker and close every segment: the transient
    /// directory is about to go.
    void shutdown();

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

    /// The collected save set in hash order, so a document always writes the
    /// same file for the same content. Read by the save itself and by the
    /// transaction log's version manifest (docs/TransactionLog.md 16.3).
    std::vector<FileBlobHandle> collected() const;

    /** The collected save set with the names a save gives it, in name order.
     *
     * The names planSave() derives from the referrers -- `Box.Shape.brp` --
     * as an archive would carry them. The transaction log keys a version's
     * blobs by these (docs/TransactionLog.md sec 23.16): the same property's
     * file has the same name in the next version, which is what pairs the
     * old content with the new for a delta.
     */
    std::vector<std::pair<std::string, FileBlobHandle>> collectedEntries() const;

    /** Every live blob with the name the restore read it under, in hash order.
     *
     * The entry name inside `blobs/` for content an archive or a directory
     * carried; content that arrived without one (the inline table) is named
     * by its hash and extension. What a history started from a file keys its
     * first version's blobs by, the same names collectedEntries() gives a
     * save of it.
     */
    std::vector<std::pair<std::string, FileBlobHandle>> restoredEntries() const;

    /// Finds the files a stored file reads, from its bytes; see FileBlob::sources().
    using SourceReader = std::vector<std::string> (*)(const std::string& bytes);
    /// Register the reader for content stored under `ext` (no dot). The
    /// module owning the format does this once, at load.
    static void registerSourceReader(const char* ext, SourceReader reader);

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
    /// Remember the entry name restored content arrived under, see
    /// restoredEntries(). `name` may carry the `blobs/` prefix.
    void nameRestored(const FileBlobHandle& blob, const std::string& name);
    FileBlobHandle make(const std::string& hash, const std::string& path, uint64_t size);
    /** Split a zip document's blob entries into the pack store (sec 15.10).
     *
     * False leaves the entries to the archive handler, one at a time.
     */
    bool restoreFromArchive(const std::string& archive);
    /// Write content to a new read-only file in the store and return its path.
    std::string writeNewFile(const std::string& bytes, const char* extension) const;
    /// Give a packed blob its own file, see FileBlob::path().
    void materialize(const FileBlob& blob);
    /// The segment files something still refers to.
    std::vector<std::shared_ptr<BlobArchive>> liveArchives() const;

    /// @name The pack store (docs/FileBlobsManager.md sec 15.7-15.10)
    //@{
    struct Segment;
    /// Whether this store packs its content: a document's, with the switch on.
    bool packStore() const;
    /// Segment cap, and the member size above which content stays a file.
    uint64_t segmentCap() const;
    uint64_t memberCap() const;
    /// Take a compressed member in memory for the next batch, and the blob.
    /// Called with the lock held.
    FileBlobHandle makePacked(const std::string& hash, uint64_t size, const std::string& ext,
                              std::shared_ptr<const BlobRawMember> member, int segment);
    /// The member holding a packed blob's content, compressed: from memory,
    /// or read out of its segment. Null when it cannot be had.
    std::shared_ptr<const BlobRawMember> readMember(const FileBlob& blob) const;
    /// A packed blob's content, decoded.
    bool readPacked(const FileBlob& blob, std::string& bytes) const;
    /// The live segment a logical one ended up in. Lock held.
    int resolveSegment(int number) const;
    /// Whether a live blob holds this content. Lock held.
    bool isLive(const std::string& hash) const;
    /// A segment number never used in this store. Lock held.
    int newSegmentNumber();
    /** Write segment \a number's next generation: \a copies, each a member
     * of a live segment by (segment, hash), copied raw, then \a added.
     * Installs it; retires its old generation and any other segment copied
     * from, which is then a merge. _writeMutex held, the lock not.
     */
    void writeGeneration(int number, const std::vector<std::pair<int, std::string>>& copies,
                         const std::vector<std::shared_ptr<const BlobRawMember>>& added);
    /// Write what is in memory into segments. _writeMutex held.
    void flushLocked();
    /// adoptBytes() with the hash known, and the member when the caller has
    /// it compressed already -- the split on open copies it raw.
    FileBlobHandle adoptMember(const std::string& hash, const std::string& bytes,
                               std::shared_ptr<BlobRawMember> member, const char* extension);
    /// Repack, delete and merge segments by what is live (sec 15.7-15.8).
    /// _writeMutex held.
    void maintain();
    /// Wake the maintenance worker: a segment member died. Any lock but
    /// _workerMutex may be held.
    void scheduleMaintenance();
    void workerLoop();
    void stopWorker();
    //@}

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
    /// The entry name each piece of content was last restored under, by hash.
    std::unordered_map<std::string, std::string> _restoreNames;
    std::unordered_map<std::string, std::weak_ptr<FileBlob>> _blobs;
    /// Every segment file opened, for closeArchives() and relocate().
    std::vector<std::weak_ptr<BlobArchive>> _archives;

    /// Live segments by number, see Segment.
    std::map<int, std::shared_ptr<Segment>> _segments;
    /// Segments a merge emptied -> the segment it wrote them to (sec 15.8).
    std::unordered_map<int, int> _redirect;
    /// Which segment holds a member for this content, live or dead: a hash
    /// the store has on disk costs no write when it is wanted again.
    std::unordered_map<std::string, int> _where;
    /// Members not yet in a segment, by hash: the batch being gathered.
    std::unordered_map<std::string, std::shared_ptr<const BlobRawMember>> _unflushed;
    uint64_t _unflushedBytes {0};
    /// The next segment number, 0 until the directory has been looked at.
    int _nextSegment {0};
    /// The segment new content goes into while it has room.
    int _appendSegment {0};
    /// Serialises segment writes; taken before _mutex, never inside it.
    std::mutex _writeMutex;
    /// Serialises materialize(), which reads under neither lock.
    mutable std::mutex _materializeMutex;
    /// The maintenance worker (sec 15.2: never the GUI's thread), started
    /// the first time a segment member dies.
    std::thread _worker;
    std::mutex _workerMutex;
    std::condition_variable _wake;
    bool _maintenanceDue {false};
    bool _workerStop {false};
    /// Saves writing blobs now: the worker waits for them (sec 15.8).
    std::atomic<int> _saving {0};
};

}  // namespace App

#endif  // APP_FILEBLOBMANAGER_H
