/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
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

#ifndef APP_DOCUMENT_H
#define APP_DOCUMENT_H

#include <CXX/Objects.hxx>
#include <Base/Observer.h>
#include <Base/Persistence.h>
#include <Base/Type.h>
#include <Base/Handle.h>

#include "PropertyContainer.h"
#include "PropertyLinks.h"
#include "PropertyStandard.h"
#include "PropertyFile.h"

#include <functional>
#include <memory>
#include <map>
#include <type_traits>
#include <vector>
#include <QString>

class QByteArray;

namespace Base {
    class Writer;
}

namespace App
{
    class TransactionalObject;
    class DocumentObject;
    class DocumentObjectExecReturn;
    class Document;
    class DocumentPy; // the python document class
    class Application;
    class FileBlobManager;
    class FileHistory;
    class PropertyXLink;
    class TransactionLogCore;
    struct LogVersion;
    class TransactionLog;
    struct LogBranch;
    class Transaction;
    class StringHasher;
    using StringHasherRef = Base::Reference<StringHasher>;
}

namespace App
{

/// The document class
class AppExport Document : public App::PropertyContainer
{
    PROPERTY_HEADER_WITH_OVERRIDE(App::Document);

public:
    enum Status {
        SkipRecompute = 0,
        KeepTrailingDigits = 1,
        Closable = 2,
        Restoring = 3,
        Recomputing = 4,
        PartialRestore = 5,
        Importing = 6,
        PartialDoc = 7,
        AllowPartialRecompute = 8, // allow recomputing editing object if SkipRecompute is set
        TempDoc = 9, // Mark as temporary document without prompt for save
        RestoreError = 10,
        LinkStampChanged = 11, // Indicates during restore time if any linked document's time stamp has changed
        IgnoreErrorOnRecompute = 12, // Don't report errors if the recompute failed
        RecomputeOnRestore = 13, // Mark pending recompute on restore for migration purpose
        LiveImport = 14, // A progressive import is filling the document while the GUI
                         // stays interactive; doc-mutating commands are gated meanwhile
        RestoreDrain = 15, // View-side catch-up on a finished restore: the work runs in
                           // full, but nothing it does may modify the document.
                           // See RestoreDrainGuard.
        Initializing = 16, // Being constructed by Application::newDocument: the
                           // writes that set it up open no implicit transaction
        VersionDoc = 17, // A version of a file opened as a document of its own
                         // (docs/TransactionLog.md sec 27.7): read-only as a
                         // partial document is -- save refuses it
        FrozenVersion = 18, // The instance of a version that pins show
                            // (docs/TransactionLog.md sec 27.22): every
                            // change to its data is refused
        OpenedForPin = 19, // Opened because a link pinned it, not by hand:
                           // offered for closing when its pins go (sec 27.30)
    };

    /** @name Properties */
    //@{
    /// holds the long name of the document (utf-8 coded)
    PropertyString Label;
    /// full qualified (with path) file name (utf-8 coded)
    PropertyString FileName;
    /// creators name (utf-8)
    PropertyString CreatedBy;
    PropertyString CreationDate;
    /// user last modified the document
    PropertyString LastModifiedBy;
    PropertyString LastModifiedDate;
    /// company name UTF8(optional)
    PropertyString Company;
    /// Unit System
    PropertyEnumeration UnitSystem;
    /// long comment or description (UTF8 with line breaks)
    PropertyString Comment;
    /// Id e.g. Part number
    PropertyString Id;
    /// unique identifier of the document
    PropertyUUID Uid;
    /// Full name of the licence e.g. "Creative Commons Attribution". See https://spdx.org/licenses/
    App::PropertyString License;
    /// License description/contract URL
    App::PropertyString LicenseURL;
    /// Meta descriptions
    App::PropertyMap Meta;
    /// Material descriptions, used and defined in the Material module.
    App::PropertyMap Material;
    /// read-only name of the temp dir created when the document is opened
    PropertyString TransientDir;
    /// Tip object of the document (if any)
    PropertyLink Tip;
    /// Tip object of the document (if any)
    PropertyString TipName;
    /// Whether to show hidden items in TreeView
    PropertyBool ShowHidden;
    /// Whether to use hasher on topological naming
    PropertyBool UseHasher;
    /// Level of preference to save content inside XML
    PropertyInteger ForceXML;
    /// Whether to split object content into separated XML files
    PropertyBool SplitXML;
    /// Prefer binary format when saving
    PropertyBool PreferBinary;
    /** Document schema version to write.
     *
     * Restoring understands every version this build knows; writing is only
     * possible for the versions the writer can still produce, which is what
     * getWritableSchemaVersions() lists. Lower it to keep a document readable
     * by an older FreeCAD, at the cost of whatever the newer versions added.
     *
     * A new document starts at the current version -- this fork's format.
     * One restored from a file starts at the version that file was written
     * in, so opening an older document does not convert it.
     */
    PropertyIntegerConstraint SaveSchemaVersion;
    /// Specify user defined thumbnail
    PropertyFile ThumbnailFile;
    /// Indicate whether to auto update thumbnail on saving document
    PropertyBool SaveThumbnail;
    //@}

    /** @name Signals of the document */
    //@{
    /// signal before changing an doc property
    fastsignals::signal<void (const App::Document&, const App::Property&)> signalBeforeChange;
    /// signal on changed doc property
    fastsignals::signal<void (const App::Document&, const App::Property&)> signalChanged;
    /// signal on new Object
    fastsignals::signal<void (const App::DocumentObject&)> signalNewObject;
    //fastsignals::signal<void (const App::DocumentObject&)>     m_sig;
    /// signal on deleted Object
    fastsignals::signal<void (const App::DocumentObject&)> signalDeletedObject;
    /// signal before changing an Object
    fastsignals::signal<void (const App::DocumentObject&, const App::Property&)> signalBeforeChangeObject;
    /// signal on changed Object
    fastsignals::signal<void (const App::DocumentObject&, const App::Property&)> signalChangedObject;
    /// signal on manually called DocumentObject::touch()
    fastsignals::signal<void (const App::DocumentObject&)> signalTouchedObject;
    /// signal on DocumentObject::purgeTouched()
    fastsignals::signal<void (const App::DocumentObject&)> signalPurgeTouchedObject;
    /// signal on relabeled Object
    fastsignals::signal<void (const App::DocumentObject&)> signalRelabelObject;
    /// signal on activated Object
    fastsignals::signal<void (const App::DocumentObject&)> signalActivatedObject;
    /// signal on created object
    fastsignals::signal<void (const App::DocumentObject&, Transaction*)> signalTransactionAppend;
    /// signal on removed object
    fastsignals::signal<void (const App::DocumentObject&, Transaction*)> signalTransactionRemove;
    /// signal on undo
    fastsignals::signal<void (const App::Document&)> signalUndo;
    /// signal on redo
    fastsignals::signal<void (const App::Document&)> signalRedo;
    /// signal after the branches of the document's transaction log changed
    /// (docs/TransactionLog.md sec 26): a switch, a new branch, a trim or a
    /// deleted branch
    fastsignals::signal<void (const App::Document&)> signalBranchesChanged;
    /// signal on a frozen version document opened for a pin when the last
    /// link pinned to it lets go (docs/TransactionLog.md sec 27.21 Q5, 27.30):
    /// unpinned, deleted, re-pinned, or its document closed. What becomes of
    /// the document is the Gui's (DocumentParams ClosePinnedVersion).
    fastsignals::signal<void (const App::Document&)> signalPinsReleased;
    /** signal on load/save document
     * this signal is given when the document gets streamed.
     * you can use this hook to write additional information in
     * the file (like the Gui::Document does).
     */
    fastsignals::signal<void (Base::Writer   &)> signalSaveDocument;
    fastsignals::signal<void (Base::XMLReader&)> signalRestoreDocument;
    /** signal collecting the included files a save must carry
     *
     * Emitted before anything is written, because the content is written
     * before the parts of the document that refer to it. Handlers report every
     * App::PropertyFileIncluded they own to the manager; the object list is
     * the subset being written, or empty for the whole document. The Gui
     * document answers for its view providers and its views, which is what
     * lets a view-only file be saved at all.
     */
    fastsignals::signal<void (App::FileBlobManager&,
                                  const std::vector<App::DocumentObject*>&)> signalCollectFiles;
    fastsignals::signal<void (const std::vector<App::DocumentObject*>&,
                                  Base::Writer   &)> signalExportObjects;
    fastsignals::signal<void (const std::vector<App::DocumentObject*>&,
                                  Base::Writer   &)> signalExportViewObjects;
    fastsignals::signal<void (const std::vector<App::DocumentObject*>&,
                                  Base::XMLReader&)> signalImportObjects;
    fastsignals::signal<void (const std::vector<App::DocumentObject*>&, Base::Reader&,
                                  const std::map<std::string, std::string>&)> signalImportViewObjects;
    fastsignals::signal<void (const std::vector<App::DocumentObject*>&)> signalFinishImportObjects;
    //signal starting a save action to a file
    fastsignals::signal<void (const App::Document&, const std::string&)> signalStartSave;
    //signal finishing a save action to a file
    fastsignals::signal<void (const App::Document&, const std::string&)> signalFinishSave;
    fastsignals::signal<void (const App::Document&)> signalBeforeRecompute;
    fastsignals::signal<void (const App::Document&, const std::vector<App::DocumentObject*>&)> signalRecomputed;
    fastsignals::signal<void (const App::DocumentObject&)> signalRecomputedObject;
    //signal a new opened transaction
    fastsignals::signal<void (const App::Document&, std::string)> signalOpenTransaction;
    // signal a committed transaction
    fastsignals::signal<void (const App::Document&)> signalCommitTransaction;
    // signal an aborted transaction
    fastsignals::signal<void (const App::Document&)> signalAbortTransaction;
    fastsignals::signal<void (const App::Document&, const std::vector<App::DocumentObject*>&)> signalSkipRecompute;
    fastsignals::signal<void (const App::DocumentObject&)> signalFinishRestoreObject;
    fastsignals::signal<void (const App::Document&,const App::Property&)> signalChangePropertyEditor;
    //@}
    fastsignals::signal<void (std::string)> signalLinkXsetValue;

    void clearDocument();

    /** @name File handling of the document */
    //@{
    /// Save the Document under a new Name
    //void saveAs (const char* Name);
    /// Save the document to the file in Property Path
    bool save ();
    bool saveAs(const char* file);
    /// Save a copy under another name; `withHistory` false leaves the
    /// embedded history out of the copy (docs/TransactionLog.md sec 13.3).
    bool saveCopy(const char* file, bool withHistory = true) const;
    void save(Base::Writer &writer, bool archive) const;
    /// Restore the document from the file in Property Path
    void restore (const char *filename=nullptr,
            bool delaySignal=false, const std::vector<std::string> &objNames={});
    /// Restore the document from a pre-constructed xml reader
    void restore (Base::XMLReader &xmlReader,
            bool delaySignal=false, const std::vector<std::string> &objNames={});
    /** @name Deferred archive-entry restores (docs/DocumentLoad.md §14)
     *
     * With DeferShapeLoad on, the restore parks the archive entries of
     * properties that opted in (Property::DeferRestore) instead of
     * reading them during the load, and each is served from the still
     * indexed archive on first real use. The consumer's accessors call
     * restoreDeferredFile() before touching their value.
     */
    //@{
    /// Serve \a obj its parked entry; false when nothing was parked.
    bool restoreDeferredFile(Base::Persistence *obj);
    /// Whether \a obj still has a parked entry.
    bool hasDeferredFile(const Base::Persistence *obj) const;
    /// Whether anything at all is still parked, i.e. whether this document
    /// still owes a serve phase (and still holds its archive index open).
    bool hasDeferredFiles() const;
    /// Drop \a obj's parked entry unserved -- its value was overwritten
    /// before anything asked for the archived one.
    void cancelDeferredFile(Base::Persistence *obj);
    /// Serve every parked entry, e.g. before the archive is rewritten.
    void flushDeferredFiles();
    /// Serve parked entries for up to \a budgetSeconds; true while more
    /// remain. The progressive-load drain calls this in slices before it
    /// builds any visuals, so shapes arrive through the same property
    /// change path an edit uses instead of materializing inside a
    /// half-staged visual fill.
    bool serveDeferredFiles(double budgetSeconds);
    //@}
    enum ExportStatus {
        NotExporting,
        Exporting,
    };
    ExportStatus isExporting(const App::DocumentObject *obj) const;
    void exportObjects(const std::vector<App::DocumentObject*>&, std::ostream&);
    void exportGraphviz(std::ostream&) const;
    std::vector<App::DocumentObject*> importObjects(Base::XMLReader& reader);
    /** Import any externally linked objects
     *
     * @param objs: input list of objects. Only objects belonging to this document will
     * be checked for external links. And all found external linked object will be imported
     * to this document. Link type properties of those input objects will be automatically
     * reassigned to the imported objects. Note that the link properties of other objects
     * in the document but not included in the input list, will not be affected even if they
     * point to some object beining imported. To import all objects, simply pass in all objects
     * of this document.
     *
     * @return the list of imported objects
     */
    std::vector<App::DocumentObject*> importLinks(
            const std::vector<App::DocumentObject*> &objs = {});
    /// Opens the document from its file name
    //void open (void);
    /// Is the document already saved to a file?
    bool isSaved() const;
    /// Get the document name
    const char* getName() const;
    /** Returned filename
     *
     * For saved document, this will be the content stored in property
     * 'Filename'. For unsaved temporary file, this will be the content of
     * property 'TransientDir'.
     */
    const char *getFileName() const;
    /// Get program version the project file was created with
    const char* getProgramVersion() const;
    /** Store of the files referenced by this document's PropertyFileIncluded.
     *
     * Owns their lifetime by reference count, and writes and reads their
     * archive entries itself. See App::FileBlobManager.
     */
    FileBlobManager& getFileBlobManager() const;
    /** The history of the file this document is (docs/TransactionLog.md
     * sec 27.7): the blob store and the directory the log lives in, shared
     * by every document of the file. Made on first use, in this document's
     * transient directory.
     */
    FileHistory& getFileHistory() const;
    /** The transaction log, or null when the mode is off
     * (DocumentParams::TransactionLog). Created on the first commit
     * after the mode is set, in the transient directory.
     */
    TransactionLog* getTransactionLog() const;
    /** An unnamed version of the document as it stands (docs/TransactionLog.md
     * sec 16.3): the serialisation of a save -- blobs made, Document.xml
     * and GuiDocument.xml streamed -- into the log and nothing on disk but
     * the store. Taken on the cadence DocumentParams::TransactionLogSnapshot*
     * gives, at commit, or on demand. Returns the version number, 0 when
     * there is no log or the document is not in a state to snapshot.
     */
    int64_t snapshotToLog();
    /** Save to the log only (docs/TransactionLog.md sec 27.22, 27.28): the
     * log records a version of the document, named `name` so that it is
     * kept, and the file is written again with the new history in it -- its
     * other members copied as they are stored, so what the file opens as
     * does not change. For any editable document of a saved file that
     * carries its history. Returns the version; throws when refused.
     */
    int64_t saveToLog(const char* name = "Saved to history");
    /** Restore the document to a version of its log (docs/TransactionLog.md
     * sec 24.5): one forward transaction, kind `restore`, that makes the
     * document what the version was -- objects the version lacks removed,
     * those it has recreated under their ids and names, dynamic properties
     * and every differing value set -- and an undo step like any. The
     * version is materialised and read into a scratch document to compare
     * against; this document is never reloaded. View providers are not
     * restored. Throws if the version is missing or cannot be read; false
     * when there is no log.
     */
    bool restoreVersion(int64_t num);
    /** Open version `num` of this document's file as a document of its own
     * (docs/TransactionLog.md sec 27.5, 27.7): on the file's one log, at
     * that version, named `<file>@v<num>`, with status VersionDoc. A
     * version open already -- a version document of it, or a document
     * whose branch has not changed since -- is returned instead. Its first
     * change puts it on a branch: the one whose tip the version is if no
     * document holds it, else a new one, `<branch>@v<num>`. Throws when
     * there is no log or no such version.
     *
     * This is the editable instance (sec 27.22), named
     * `<file>@<branch>@v<num>` (27.23); the frozen one, which pins show,
     * is opened by openFileVersion() with `frozen`.
     */
    Document* openVersion(int64_t num, bool createView = true);
    /** Pin `link` to version `version` of the file it links to
     * (docs/TransactionLog.md sec 16.5, 27.7); 0 pins the version the linked
     * document is -- the file on disk, or a version document's own. The
     * version is named, the file noted as pinned so it saves with its history
     * whatever the preference (27.5 ruling 1), and its document marked
     * modified. The link's change is an undoable write. Returns the version.
     */
    static int64_t pinLink(PropertyXLink& link, int64_t version = 0);
    /// This file is pinned by a link: its log says so, or a loaded link is.
    bool isPinned() const;
    /** Save a version document over its file (docs/TransactionLog.md sec
     * 27.16), which a plain save() refuses: the document takes its branch
     * if it has none, as its first change would, and the file is written
     * from it and reopens on that branch. The document keeps its name.
     * Returns the version the save became; throws on failure.
     */
    int64_t saveVersionAsFile();
    /** Name every version document of this document's file for where it
     * is now (docs/TransactionLog.md sec 27.23): `<file>@v<num>` and
     * `<label>@v<num>` for the frozen instance, `<file>@<branch>@v<num>`
     * and `<label>@<branch>@v<num>` for an editable one -- its branch, or
     * the one its first change will take. After a branch is made, renamed
     * or switched to, and after a save moves a version tail.
     */
    void refreshVersionNames();
    /// A frozen version (FrozenVersion, sec 27.22), throwing `what` refused.
    void checkNotFrozen(const char* what) const;
    /** The same for a file's history, whether or not a document of the file
     * is open (docs/TransactionLog.md sec 27.13; FileHistory::openFile()).
     * `from`, when given, is the document of the file it is named after.
     * `frozen` opens the instance a pin shows (sec 27.22): status
     * FrozenVersion, named `<file>@v<num>`, every change refused; a version
     * has at most one of each.
     */
    static Document* openFileVersion(const std::shared_ptr<FileHistory>& history, int64_t num,
                                     bool createView = true, const Document* from = nullptr,
                                     bool frozen = false);

    /** What a name of parseName() opens (docs/TransactionLog.md sec 27.23,
     * 27.24): with no `branch`, the frozen instance of version `num`; with
     * a branch and a version, the editable instance at that version; with a
     * branch and no version, the document holding the branch, or else the
     * branch's head opened as an editable instance. Throws when there is no
     * such version or branch.
     */
    static Document* openFileBranch(const std::shared_ptr<FileHistory>& history,
                                    const std::string& branch, int64_t num,
                                    bool createView = true);

    /** Branches (docs/TransactionLog.md sec 17, 26). createBranch() makes
     * branch `name` from version `version`, or else from log row `seq`, or
     * else from the current head, and switches to it; the fork version is
     * named if it was not (a row with no version gets one). Its object ids
     * start a random stride above every other branch's. Returns its id, 0
     * when there is no log; throws on a name taken or a fork not found.
     */
    int64_t createBranch(const std::string& name, int64_t version = 0, int64_t seq = 0);
    /** Make the document the head of branch `name`, in place: the tip left
     * is snapshotted, the head's newest version checked out and its tail
     * replayed, the undo and redo stacks become the branch's own steps
     * since the document was opened. Not an undo step; a `switch` record
     * goes on the branch arrived on. Throws if there is no such branch.
     */
    bool switchBranch(const std::string& name);
    /** Rename branch `name` to `newName` (docs/TransactionLog.md sec 26).
     * Throws if there is no such branch or the new name is empty or taken.
     */
    bool renameBranch(const std::string& name, const std::string& newName);
    /** Trim branch `name` (docs/TransactionLog.md sec 16.7): its rows up to
     * version `version` -- else up to the version at its head -- and its
     * unnamed versions before it go, but never a row another branch's
     * history holds, a named version, or one a branch forked from. The
     * version kept is named if it was not. With `bridge`, where another
     * branch shares the history, the rows from the newest shared one up to
     * the kept version become one row of their net change instead of going,
     * so the two still meet in rows (sec 27.71). Returns the rows removed.
     */
    size_t trimBranch(const std::string& name, int64_t version = 0, bool bridge = true);
    /** Delete branch `name` (sec 16.7): the branch, the rows only it holds,
     * and its versions but those another branch forked from. Not the
     * branch the document is on. Returns the rows removed.
     */
    size_t deleteBranch(const std::string& name);
    /// What compactFileState() dropped.
    struct CompactResult
    {
        size_t names = 0;     ///< object names freed
        size_t geoIds = 0;    ///< objects whose last geometry id was forgotten
        size_t strings = 0;   ///< strings dropped from the file's hasher
    };
    /** Compact the file-scope state (docs/TransactionLog.md sec 27.47): the
     * name and last geometry id of every object nothing refers to any more
     * -- no document of the file holds it, no op names it, no version has
     * it -- are forgotten, in memory and in the store, so the name is free
     * again; and the strings of the file's hasher nothing holds go. The
     * counters stay. Nothing without a log.
     */
    CompactResult compactFileState();
    /** Make a branch at the head this document is on and open it in a
     * second document of the file, this one staying where it is
     * (docs/TransactionLog.md sec 30.3 S.a) -- `<branch>~<n>` when no name
     * is given. The new branch names this one as its target, the one its
     * merges go to and come from unless another is named; neither follows
     * the other. Throws when the name is taken.
     */
    Document* openNewBranch(const std::string& name = std::string(), bool createView = true);
    /// Where the branch this document is on stands against the branch it
    /// was made from (sec 30.3 S.a), by the rows stored so far.
    struct BranchState
    {
        std::string branch;
        std::string target;   ///< the branch it was made from; empty for none
        size_t ahead {0};     ///< operations here its target does not hold
        size_t behind {0};    ///< operations of its target not held here
    };
    BranchState branchState();

    /// What _noteDroppedRows estimated (sec 27.48).
    struct CompactEstimate
    {
        size_t objects = 0;       ///< objects left referred to by nothing, these rows
        size_t bytes = 0;         ///< their name and geometry-id entries, in bytes
        size_t totalBytes = 0;    ///< the file's estimate since the last compaction
        size_t strings = 0;       ///< strings of the file's hasher nothing holds
        size_t stringBytes = 0;   ///< and their bytes
        bool compacted = false;
    };
    /** Squash the rows between versions `from` and `to` (sec 16.7) into one
     * `squash` transaction whose ops are the net change -- undone, replayed
     * and browsed like any. Both versions stay; unnamed ones between go.
     * Refused (throws) when `from` is not behind `to` on one history, or a
     * branch forks, or a named version sits, between them. Returns the rows
     * folded.
     */
    size_t squashVersions(int64_t from, int64_t to);

    /// One change the other branch made, as a merge sees it
    /// (docs/TransactionLog.md sec 28.2 item 3).
    struct MergeChange
    {
        /** `take`: ours has not changed it, theirs goes in. `same`: both
         * left it the same. `conflict`: both changed it, differently -- a
         * side must be picked. `view`: a conflict on a view-provider
         * property, which keeps ours unless picked otherwise (sec 28.6 Q2).
         * `derived`: a value theirs' recompute wrote, not merged; its owner
         * is recomputed. `unit`: a property that is one thing with others
         * of its object (DocumentObject::getMergeUnit, sec 31.5) and goes
         * by the side picked for the conflict whose `key` it has.
         */
        std::string kind;
        /// `set`, `addprop`, `delprop`, `create`, `remove`, `revive`: an
        /// object ours removed and theirs changed, or `unit`: the conflict
        /// of such properties together, which writes nothing itself.
        std::string op;
        /// What a pick names it by: `<object>.<property>`, `.<property>` for
        /// the document's own, `view:<object>.<property>`, `<object>`.
        std::string key;
        std::string ckind;    ///< `doc`, `obj` or `view`
        long cid {0};
        std::string object;   ///< the object's name
        std::string prop;
        std::string ptype;
        /// The value at the base, ours and theirs, as entity refs; empty
        /// where the property is not there.
        std::string base, ours, theirs;
        bool derived {false};   ///< a value theirs' recompute wrote
        std::string note;     ///< why, where the kind alone does not say
    };
    /// What merging a branch would do (sec 28.2 item 9).
    struct MergePreview
    {
        std::string branch;
        int64_t ours {0};     ///< this document's head
        int64_t theirs {0};   ///< the row merged in
        int64_t base {-1};    ///< the newest row both histories hold
        /// Ours has changed nothing since the base: theirs is taken whole,
        /// derived values included, with no recompute (sec 28.6 Q1).
        bool fastForward {false};
        /// This branch has not moved since the base at all -- records
        /// only -- and the base is on both chains (sec 30.4 P1): theirs'
        /// rows, oldest first, which the merge takes as they are, writing
        /// no row of its own. Empty when it writes one.
        std::vector<int64_t> forward;
        std::vector<MergeChange> changes;
        size_t conflicts {0};
    };
    /// What a merge did.
    struct MergeResult
    {
        int64_t seq {0};      ///< the merge row, 0 when none was written
        /// The rows taken as they are (sec 30.4 P1); `seq` is then the
        /// last of them, the head merged in.
        size_t forwarded {0};
        /// The conflicts nobody picked a side for: the merge was refused
        /// and nothing moved (sec 28.6 Q3).
        std::vector<MergeChange> unresolved;
        std::vector<std::string> failed;   ///< objects whose recompute failed
        MergePreview preview;
    };
    /** What merging branch `branch` into the one this document is on would
     * do (docs/TransactionLog.md sec 28): the base, and every change the
     * other branch made since. Read only. With `version`, the branch up to
     * that version of it. Throws when there is no such branch, or the two
     * share no history in the log's rows.
     */
    MergePreview previewMerge(const std::string& branch, int64_t version = 0);
    /** Merge branch `branch` into the one this document is on (sec 28):
     * one transaction of kind `merge`, undone like any, whose second parent
     * is the head merged in. `picks` names a side, `ours` or `theirs`, per
     * conflict key; `fallback` is the side of every conflict not picked.
     * A conflict with no side refuses the merge: nothing moves, and the
     * result lists them. The other branch is left as it is.
     *
     * When this branch has not moved since the base (sec 30.4 P1) the
     * merge is a fast-forward: the other branch's rows are taken as they
     * are, this branch's head moves onto them, the document follows as a
     * switch arrives -- its undo steps the rows of the chain -- and no
     * row is written. Records this branch made since the base, a save or
     * a snapshot, are written again after the rows taken, and their
     * versions stay at the base.
     */
    MergeResult mergeBranch(const std::string& branch,
                            const std::map<std::string, std::string>& picks = {},
                            const std::string& fallback = std::string(), int64_t version = 0);

    /// A branch of another copy of the file, as importFork() can take it.
    struct ForkBranch
    {
        std::string name;
        /// The branch the copy's file reopens on: the one an import takes
        /// when none is named (sec 30.14 F6).
        bool current {false};
        bool closed {false};
        /// It shares no history with this file, and is offered all the same
        /// (sec 30.22): what it is now comes as a branch from nothing.
        bool independent {false};
        /// The row here its history parts from this file's, 0 when the two
        /// share none; and how many operations it has made since.
        int64_t base {0};
        size_t ahead {0};
    };
    /** The branches of the copy of this file at `path` (docs/TransactionLog.md
     * sec 30.13, 30.14 F6): what importFork() offers. A file that carries no
     * history offers one thing, itself, under no name. Throws when the file
     * cannot be read, or is this document's own.
     */
    std::vector<ForkBranch> forkBranches(const std::string& path);
    /// What importFork() did.
    struct ImportResult
    {
        std::string branch;   ///< the branch here the rows went to
        std::string from;     ///< the copy's branch they came from
        int64_t base {0};     ///< the row here both histories hold
        size_t rows {0};      ///< rows brought over
        /// Rows with nothing to bring: records, and rows whose every value
        /// was derived.
        size_t skipped {0};
        /// The copy's named versions that came as versions of the branch (F5).
        size_t versions {0};
        /// The branch is an earlier import's, continued (F7).
        bool extended {false};
        /// The branch hangs off no row of this file (sec 30.22): the file
        /// shares no history with it, or names no save of it.
        bool independent {false};
        /// The copy's row the import stopped at, 0 when it took them all,
        /// and why (F8). The rows before it are in. -1 when what could not
        /// come is the file as found, which is a state and no row.
        int64_t stoppedAt {0};
        std::string reason;
        /// The copy's new objects whose names this file had given to others
        /// (sec 30.4 P4): its name, the one it came under.
        std::map<std::string, std::string> renamed;
        int64_t seq {0};      ///< the import's record, 0 when nothing came
    };
    /** Bring a branch of another copy of this file in as a branch here
     * (docs/TransactionLog.md sec 30.13, 30.14): the copy's history is
     * read without its document, the newest row both hold found, and its
     * rows after that replayed -- in a document of their own at that row,
     * this one staying where it is -- each committed as a row of this log
     * under the author and the identity it had. Objects the copy made come
     * under new ids, and under new names where theirs are taken; derived
     * values are left out, and whoever merges the branch recomputes.
     *
     * `branch` names the copy's branch, the one its file reopens on when
     * empty. `sender` is who sent the file, when it was sent by a client
     * of a served document (sec 30.20 H7): the import's record names them.
     * The rows keep the authors the file gives them. The branch here is named after the file -- and after the
     * copy's branch when that is not its file's -- and a second import of
     * the same copy continues it. A row that cannot be applied ends the
     * import, the rows before it kept. Nothing is merged: that is
     * mergeBranch().
     *
     * A file that carries no history comes as one row (sec 30.19): the
     * difference between the file as it is and the state at the save its
     * `Version` names, on a branch from that save's row; brought again,
     * changed, as one more row on that branch. A file whose history is
     * there but is not its tip -- edited where there is no log -- comes as
     * the branch that was the file's, then that one row, then what it has
     * done since.
     *
     * A file that shares no history with this one -- none in its rows, no
     * save of this file's named by its `Version` -- comes all the same (sec
     * 30.22), as an independent branch: what it is, as one row on a branch
     * from nothing, an object of it taken for one of this file's where its
     * id and its name are that object's. mergeBranch() merges such a branch
     * by what the two documents hold, with no base to compare against.
     *
     * Throws when the file cannot be read, or a branch asked for by name
     * is not there or shares no history.
     */
    ImportResult importFork(const std::string& path, const std::string& branch = std::string(),
                            const std::string& sender = std::string());

    /// An imported branch that waits to be merged (sec 30.20 H1, 30.23).
    struct ImportRequest
    {
        std::string branch;                 ///< the branch here
        std::string file;                   ///< the file it came from
        std::string from;                   ///< the copy's branch, empty for a file with none
        std::vector<std::string> authors;   ///< who made its rows
        std::string sender;                 ///< who sent the file, when someone did (H7)
        double when {0};                    ///< when it was last brought
        size_t rows {0};                    ///< the operations it has to give
        /// The conflicts the merge's preview finds; -1 when it was not
        /// asked for, or could not be made.
        int conflicts {-1};
        bool independent {false};
        int64_t stoppedAt {0};
    };
    /** The requests (docs/TransactionLog.md sec 30.20 H1): the branches an
     * import made that the branch this document is on has not taken. Not a
     * table: worked out from the branches' own marks and from what this
     * branch's history holds, each time. With `preview`, each is put to
     * previewMerge() -- one that has nothing to give is left out, and the
     * rest say how many conflicts it finds; that reads values, and costs
     * what a merge's preview costs.
     */
    std::vector<ImportRequest> importRequests(bool preview = true);

    /// A file someone sent to be merged, kept in the log (sec 30.29).
    struct SentFile
    {
        /// Its `request` row, as numbered now: a fast-forward gives a
        /// record another number, and the file goes with it.
        int64_t seq {0};
        std::string name;         ///< the file's name
        std::string hash;         ///< the blob that holds it
        int64_t size {0};
        std::string sender;       ///< who sent it
        std::string senderKind;   ///< how that name is known: Actor's kinds
        double when {0};          ///< when it came
        /// The branch it was brought in to; empty until it is. Its bytes
        /// are held until that branch is merged or deleted.
        std::string branch;
    };
    /** Keep `bytes`, a file someone sent to be merged, in the log
     * (docs/TransactionLog.md sec 30.28, 30.29): a `request` row named
     * `name` under whoever acts, saying who sent it, and the bytes as one
     * blob. Nothing of the file is read -- not opened, not unpacked (sec
     * 30.20 H6). It is saved with the history and recovered with it.
     * Returns the row. Throws when the document keeps no log.
     */
    int64_t keepSentFile(const std::string& bytes, const std::string& name,
                         const std::string& sender = std::string(),
                         const std::string& senderKind = std::string());
    /// The sent files the log holds, oldest first.
    std::vector<SentFile> sentFiles();
    /// Let the file of row `seq` go unread, with a record saying so. False
    /// when none is held.
    bool dropSentFile(int64_t seq);
    /// Write the bytes of the file of row `seq` to `path`, as they came.
    void writeSentFile(int64_t seq, const std::string& path);
    /** Bring the file of row `seq` in: importFork() of its bytes, the
     * import's record naming the file and who sent it. The file stays held
     * until the branch it came to is merged into this one or deleted; one
     * that has nothing to give is let go at once.
     */
    ImportResult importSentFile(int64_t seq, const std::string& branch = std::string());

    /** Crash recovery (docs/TransactionLog.md sec 25): make this new, empty
     * document what the session that crashed with transient directory
     * `oldDir` had -- its newest version, the log's tail replayed over it --
     * taking over that session's log and blobs, so undo and the log reach
     * across the crash. Nothing it writes is a transaction; one `recover`
     * row records it. False when `oldDir` holds no log.
     */
    bool recoverFromLog(const std::string& oldDir);
    /** Undo log row `seq` though it is not the last step (docs/TransactionLog.md
     * sec 24.4): a new transaction, an undo step itself, applying the row
     * reversed. Refused, with the conflicts reported, when an op since
     * changed a property the row left in a state, a created object is
     * gone, or a removed object's name or id is taken. Derived values are
     * not restored; their owners are touched. Returns whether it ran.
     */
    bool undoLogged(int64_t seq);

    /** How App reaches an object's view provider (docs/TransactionLog.md
     * sec 24.9): the Gui registers a resolver; without one (FreeCADCmd)
     * view state is out of reach and left alone.
     */
    using ViewResolver = std::function<PropertyContainer*(const DocumentObject*)>;
    static void setViewResolver(ViewResolver resolver);
    /// The view provider of `obj`, or null.
    static PropertyContainer* viewOf(const DocumentObject* obj);
    /** Called when an implicit transaction opens outside any invocation
     * (docs/TransactionLog.md sec 24.10): a GUI event that is not a command
     * has nothing that returns to close it. The Gui registers one that
     * commits the implicit transactions once control is back in its event
     * loop; without one (FreeCADCmd) the next invocation, explicit open,
     * save or close commits it, as before.
     */
    static void setImplicitCloser(std::function<void()> closer);
    /// Whether writes are recorded into transactions at all: undo is on,
    /// or the log is (which records without keeping undo steps).
    bool transactionsWanted() const;
    /// Commit the active transaction if it is an implicit one.
    void commitImplicitTransaction();

    /** Tell the manager which included files a save has to carry.
     *
     * Walks the properties of the given objects -- all of them when the list
     * is empty -- and broadcasts signalCollectFiles for the view tier. Runs
     * before anything is written: the content goes into the archive ahead of
     * everything that refers to it.
     */
    void collectFileBlobs(const std::vector<App::DocumentObject*>& objs = {}) const;

    /** Schema versions this build can write, ascending, newest last.
     *
     * Explicit rather than a range: a version is writable only where the
     * writer can still produce that shape, which is not true of every version
     * the reader accepts.
     */
    static const std::vector<long>& getWritableSchemaVersions();
    /// Newest writable schema version.
    static long getCurrentSchemaVersion();
    /** The archive member a schema-5 file keeps its string table in
     * (docs/TransactionLog.md sec 27.50 item 1), read before the objects.
     */
    static const char* stringTableName();
    /// Schema version this document asks to be written with -- the user's
    /// cap, validated against the writable list.
    long getSaveSchemaVersion() const;
    /// Schema version one particular save actually comes out as. The cap
    /// answers "what may this document be?", this answers "what is this
    /// file?" -- kept apart because a save may leave out a half of the
    /// format it cannot carry (a split save shares no default block)
    /// without that changing the version the file is written under.
    long resolveSchemaVersion(const Base::Writer &writer) const;
    //@}

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;
    void SaveDocFile(Base::Writer &writer) const override;
    void RestoreDocFile(Base::Reader &reader) override;

    bool removeDynamicProperty(const char* prop) override;
    App::Property* addDynamicProperty(
            const char* type, const char* name=0,
            const char* group=0, const char* doc=0,
            short attr=0, bool ro=false, bool hidden=false) override;

    /// returns the complete document memory consumption, including all managed DocObjects and Undo Redo.
    unsigned int getMemSize () const override;

    /** @name Object handling  */
    //@{
    /** Add a feature of sType with sName (ASCII) to this document and set it active.
     * Unicode names are set through the Label property.
     * @param sType       the type of created object
     * @param pObjectName if nonNULL use that name otherwise generate a new unique name based on the \a sType
     * @param isNew       if false don't call the \c DocumentObject::setupObject() callback (default is true)
     * @param viewType    override object's view provider name
     * @param isPartial   indicate if this object is meant to be partially loaded
     */
    DocumentObject *addObject(const char* sType, const char* pObjectName=nullptr,
            bool isNew=true, const char *viewType=nullptr, bool isPartial=false);
    /** Add a feature of the given type to the document.
     *
     * The typed form of the call above; the type name is taken from T's own
     * registration. Additive: a call without an explicit T cannot pick this
     * one, so every existing call still resolves to the overload above.
     *
     * @tparam T          the type of created object
     * @param pObjectName if nonNULL use that name otherwise generate a new unique name based on \a T
     * @param isNew       if false don't call the \c DocumentObject::setupObject() callback (default is true)
     * @param viewType    override object's view provider name
     * @param isPartial   indicate if this object is meant to be partially loaded
     */
    template<typename T>
    T* addObject(const char* pObjectName = nullptr, bool isNew = true,
            const char* viewType = nullptr, bool isPartial = false);
    /** Add an array of features of the given types and names.
     * Unicode names are set through the Label property.
     * @param sType       The type of created object
     * @param objectNames A list of object names
     * @param isNew       If false don't call the \c DocumentObject::setupObject() callback (default is true)
     */
    std::vector<DocumentObject *>addObjects(const char* sType, const std::vector<std::string>& objectNames, bool isNew=true);
    /// Remove a feature out of the document
    void removeObject(const char* sName);

    /// Batch remove a number of objects
    void removeObjects(const std::vector<std::string> &objs);
    /** Query if the document is removing so as to delay property change notification
     *
     * The function will also queue the input property to notify change after all objects are moved by removeObjects()
     */
    static bool isRemoving(Property *);
    /** Query if the document is removing so as to delay property change notification
     */
    static void removePendingProperty(Property *);

    /** Query whether a class's recorded defaults are being read into a stand-in.
     *
     * The stand-in belongs to no document and stands for a class rather than
     * for anything in one, so a property landing in it has nobody to notify.
     * Saying so is not an optimisation: an object's reaction to its own
     * property changing is written for an object that is in a document, and
     * App::Link's dereferences one unconditionally.
     */
    static bool isRestoringDefaults();

    /** RAII for isRestoringDefaults(), shared by every block reader.
     *
     * The Gui document restores its view provider blocks into stand-ins of
     * its own, and a detached view provider's reaction to a property is no
     * more written for the occasion than a detached object's -- one guard,
     * both readers.
     */
    class AppExport RestoringDefaultsGuard {
    public:
        RestoringDefaultsGuard();
        ~RestoringDefaultsGuard();
        RestoringDefaultsGuard(const RestoringDefaultsGuard &) = delete;
        RestoringDefaultsGuard &operator=(const RestoringDefaultsGuard &) = delete;
    };

    /** RAII scope that answers isAnyRestoring() with true.
     *
     * For work that replays a load's record after the load itself has let
     * go -- the Gui document's deferred view provider drain. Everything
     * that keys off isAnyRestoring() treated the record's properties as a
     * restore when they were read eagerly; a slice replaying them later is
     * the same work and needs the same answer.
     */
    class AppExport RestoringScopeGuard {
    public:
        RestoringScopeGuard();
        ~RestoringScopeGuard();
        RestoringScopeGuard(const RestoringScopeGuard &) = delete;
        RestoringScopeGuard &operator=(const RestoringScopeGuard &) = delete;
    private:
        bool toggled;
    };

    /** RAII scope for view-side work replayed after a restore has finished.
     *
     * The eager load ran a view provider's updateData() per property while
     * the object was still restoring, and afterRestore() then purged what
     * those handlers touched -- per object, right before it announced the
     * object as finished (see the purgeTouched() call there). A recompute
     * purges its own the same way. So a handler that writes back while it
     * renders -- a page template noting the size of the SVG it just parsed --
     * cost the eager path nothing: the mark never outlived the load.
     *
     * The deferred view provider drain has no such window. It replays those
     * same handlers slices later, past every purge, where the identical
     * write leaves the document needing a recompute merely because it was
     * opened. Marking the objects as restoring again is not the answer:
     * handlers skip their real work while restoring -- rendering an SVG
     * template returns nothing at all -- which is what the catch-up exists
     * to do. This scope says the other half instead, "render, but do not
     * write": a change inside it does not touch its object, and names
     * itself in the document's report, so the handler that should not be
     * writing on a render is found by opening a file rather than by
     * attaching a debugger.
     */
    /** Let a command run, and stop it only if it really changes something.
     *
     * A command declares what it intends to alter in eType, and refusing
     * every command that declares AlterDoc was the first way this document
     * was protected during a live load or import. It is not a good test:
     * eType defaults to AlterDoc, so the great majority of commands claim to
     * alter the document whether or not they touch it, and 138 of them
     * overwrite it with a bare ForEdit and escape the gate entirely. The
     * declaration is simply not evidence, in either direction.
     *
     * So the question is asked where the answer is certain -- at the write.
     * While this guard is alive, any attempt to touch an object, change a
     * property of one, or add or remove an object of a document that is
     * still loading throws, saying why. A command that only looks runs
     * exactly as it always did, and needs no declaration to be trusted.
     *
     * What this cannot cover, and what a caller must still refuse by name:
     * an operation that mutates in bulk and would be left half-done by a
     * throw (undo and redo), and one that damages the document without
     * changing an object at all (revert, recompute, quit, saving a document
     * whose objects are still arriving).
     */
    class AppExport UserEditGuard {
    public:
        UserEditGuard();
        ~UserEditGuard();
        UserEditGuard(const UserEditGuard &) = delete;
        UserEditGuard &operator=(const UserEditGuard &) = delete;
    private:
        bool toggled;
    };

    /// Whether a user command is running inside a UserEditGuard.
    static bool isUserEditing();

    /** Throw if a user command is changing a document that is still filling.
     *
     * Called from the write paths. \a prop is null when the caller touched
     * the object rather than one of its properties, and \a obj may be null
     * for a change that is about the document itself.
     */
    static void checkUserEdit(const Document *doc, const DocumentObject *obj,
                              const Property *prop);

    class AppExport RestoreDrainGuard {
    public:
        explicit RestoreDrainGuard(Document *doc);
        ~RestoreDrainGuard();
        RestoreDrainGuard(const RestoreDrainGuard &) = delete;
        RestoreDrainGuard &operator=(const RestoreDrainGuard &) = delete;
    private:
        Document *doc;
        bool toggled;
    };

    /// What RestoreDrainGuard suppressed: how many changes, and up to ten
    /// distinct property names among them.
    struct RestoreDrainReport {
        std::size_t count = 0;
        std::vector<std::string> names;
        /// Set once a name is dropped, so a report can say that it is a sample
        bool truncated = false;
    };
    const RestoreDrainReport &getRestoreDrainReport() const;
    void clearRestoreDrainReport();
    /// Record a change suppressed by RestoreDrainGuard. Called from the touch
    /// paths; 'prop' is null when the caller touched the object directly.
    void reportRestoreDrainChange(const DocumentObject *obj, const Property *prop);

    /** Add an existing feature with sName (ASCII) to this document and set it active.
     * Unicode names are set through the Label property.
     * This is an overloaded function of the function above and can be used to create
     * a feature outside and add it to the document afterwards.
     * \note The passed feature must not yet be added to a document, otherwise an exception
     * is raised.
     */
    void addObject(DocumentObject*, const char* pObjectName=nullptr, bool activate=true);

    static void clearPendingRemove();


    /** Copy objects from another document to this document
     *
     * @param recursive: if true, then all objects this object depends on are
     * copied as well. By default \a recursive is false.
     *
     * @param returnAll: if true, return all copied objects including those
     * auto included by recursive searching. If false, then only return the
     * copied object corresponding to the input objects.
     *
     * @return Returns the list of objects copied.
     */
    std::vector<DocumentObject*> copyObject(
            const std::vector<DocumentObject*> &objs,
            bool recursive=false, bool returnAll=false);
    /** Move an object from another document to this document
     * If \a recursive is true then all objects this object depends on
     * are moved as well. By default \a recursive is false.
     * Returns the moved object itself or 0 if the object is already part of this
     * document..
     */
    DocumentObject* moveObject(DocumentObject* obj, bool recursive=false);
    /// Returns the active Object of this document
    DocumentObject *getActiveObject() const;
    /// Returns a Object of this document
    DocumentObject *getObject(const char *Name) const;
    /// Returns a Object of this document by its id
    DocumentObject *getObjectByID(long id) const;
    /// Returns true if the DocumentObject is contained in this document
    bool isIn(const DocumentObject *pFeat) const;
    /// Returns a Name of an Object or 0
    const char *getObjectName(DocumentObject *pFeat) const;
    /** A name no object of the document has. For a new object (`id` 0), also
     * none the file's name table gives another object, in any version or
     * branch (docs/TransactionLog.md sec 27.40 item 3); an object coming
     * back under its id keeps its name when the document has it free.
     */
    std::string getUniqueObjectName(const char *Name, long id = 0) const;
    /** A new geometry id for `obj`, above `floor` -- the largest it holds --
     * and, when the document has a file history, above every id `obj` had
     * in any version or branch of the file (docs/TransactionLog.md sec
     * 27.40 item 4). Sketcher's geometry ids.
     */
    long nextGeoId(const DocumentObject& obj, long floor) const;
    /// Returns a name of the form prefix_number. d specifies the number of digits.
    std::string getStandardObjectName(const char *Name, int d) const;
    /// Returns a list of document's objects including the dependencies
    std::vector<DocumentObject*> getDependingObjects() const;
    /// Returns a list of all Objects
    const std::vector<DocumentObject*> &getObjects() const;
    std::vector<DocumentObject*> getObjectsOfType(const Base::Type& typeId) const;
    /// Returns all object with given extensions. If derived=true also all objects with extensions derived from the given one
    std::vector<DocumentObject*> getObjectsWithExtension(const Base::Type& typeId, bool derived = true) const;
    std::vector<DocumentObject*> findObjects(const Base::Type& typeId, const char* objname, const char* label) const;
    /// Returns an array with the correct types already.
    template<typename T> inline std::vector<T*> getObjectsOfType() const;
    int countObjectsOfType(const Base::Type& typeId) const;
    /// Returns the number of objects of the given type.
    template<typename T> inline int countObjectsOfType() const;
    /// get the number of objects in the document
    int countObjects() const;
    //@}

    /** @name methods for modification and state handling
     */
    //@{
    /// Remove all modifications. After this call The document becomes Valid again.
    void purgeTouched();
    /// check if there is any touched object in this document
    bool isTouched() const;
    /// check if there is any object must execute in this document
    bool mustExecute() const;
    /// returns all touched objects
    std::vector<App::DocumentObject *> getTouched() const;
    /// set the document to be closable, this is on by default.
    void setClosable(bool);
    /// check whether the document can be closed
    bool isClosable() const;
    /** Recompute touched features and return the number of recalculated features
     *
     * @param objs: specify a sub set of objects to recompute. If empty, then
     * all object in this document is checked for recompute
     */
    int recompute(const std::vector<App::DocumentObject*> &objs={},
            bool force=false,bool *hasError=nullptr, int options=0);
    /// Recompute only one feature
    bool recomputeFeature(DocumentObject* Feat,bool recursive=false);
    /// get the text of the error of a specified object
    const char* getErrorDescription(const App::DocumentObject*) const;
    /// set the text of the error of a specified object
    void setErrorDescription(App::DocumentObject *, const char *);
    /// set the text of the error of a specified object
    void setErrorDescription(App::Property *, const char *);
    /// return the status bits
    bool testStatus(Status pos) const;
    /// set the status bits
    void setStatus(Status pos, bool on);
    //@}


    /** @name methods for the UNDO REDO and Transaction handling
     *
     * Introduce a new concept of transaction ID. Each transaction must be
     * unique inside the document. Multiple transactions from different
     * documents can be grouped together with the same transaction ID.
     *
     * When undo, Gui component can query getAvailableUndo(id) to see if it is
     * possible to undo with a given ID. If there more than one undo
     * transactions, meaning that there are other transactions before the given
     * ID. The Gui component shall ask user if they want to undo multiple steps.
     * And if the user agrees, call undo(id) to unroll all transaction before
     * and including the one with the given ID. Same applies for redo.
     *
     * The new transaction ID describe here is fully backward compatible.
     * Calling the APIs with a default id=0 gives the original behavior.
     */
    //@{
    /// switch the level of Undo/Redo
    void setUndoMode(int iMode);
    /// switch the level of Undo/Redo
    int getUndoMode() const;
    /// switch the transaction mode
    void setTransactionMode(int iMode);
    /** Open a new command Undo/Redo, an UTF-8 name can be specified
     *
     * @param name: transaction name
     *
     * This function calls App::Application::setActiveTransaction(name) instead
     * to setup a potential transaction which will only be created if there is
     * actual changes.
     */
    void openTransaction(const char* name=nullptr);
    /// Rename the current transaction if the id matches
    void renameTransaction(const char *name, int id);
    /// Commit the Command transaction. Do nothing If there is no Command transaction open.
    void commitTransaction();
    /// Abort the actually running transaction.
    void abortTransaction();
    /// Check if a transaction is open
    bool hasPendingTransaction() const;
    /// Return the undo/redo transaction ID starting from the back
    int getTransactionID(bool undo, unsigned pos=0) const;
    /** Return the ID of the transaction this document is currently taking part in
     * Upstream's spelling for "is a transaction open here"; it answers with the
     * open transaction's ID, else the application-wide active transaction's,
     * else zero. See hasPendingTransaction() for the boolean form.
     */
    int getBookedTransactionID() const;
    /// Check if a transaction is open and its list is empty.
    /// If no transaction is open true is returned.
    bool isTransactionEmpty() const;
    /// Set the Undo limit in Byte!
    void setUndoLimit(unsigned int UndoMemSize=0);
    /// Returns the actual memory consumption of the Undo redo stuff.
    unsigned int getUndoMemSize () const;
    /// Set the Undo limit as stack size
    void setMaxUndoStackSize(unsigned int UndoMaxStackSize=20);
    /// Set the Undo limit as stack size
    unsigned int getMaxUndoStackSize()const;
    /// Remove all stored Undos and Redos
    void clearUndos();
    /// Returns the number of stored Undos. If greater than 0 Undo will be effective.
    int getAvailableUndos(int id=0) const;
    /// Returns a list of the Undo names
    std::vector<std::string> getAvailableUndoNames() const;
    /// Will UNDO one step, returns False if no undo was done (Undos == 0).
    bool undo(int id=0);
    /// Returns the number of stored Redos. If greater than 0 Redo will be effective.
    int getAvailableRedos(int id=0) const;
    /// Returns a list of the Redo names.
    std::vector<std::string> getAvailableRedoNames() const;
    /// Will REDO one step, returns False if no redo was done (Redos == 0).
    bool redo(int id=0) ;
    /** Whether the next undo (or redo) of whoever acts goes through the log
     * (docs/TransactionLog.md sec 30.10): someone has written since the
     * step, so it is taken back as a transaction of its own, with a
     * recompute of what it leaves to one. A caller holding a
     * TransactionGuard lets go of it first: the guard defers the touches a
     * recompute works from.
     */
    bool stepNeedsLog(bool undo) const;
    /// Why the last undo() or redo() was refused, empty when it was not.
    const std::string& undoRefusal() const;
    /// returns true if the document is in an Transaction phase, e.g. currently performing a redo/undo or rollback,
    /// or replaying transaction log rows (a branch switch, a crash recovery)
    bool isPerformingTransaction() const;
    /** Whether the document is being rebuilt from its transaction log: a
     * branch switch or a crash recovery replaying rows, or a restore to a
     * version (docs/TransactionLog.md sec 27.67). What it writes comes from
     * the log, and an object that makes objects of its own on demand -- an
     * Origin its axes and planes -- must wait for the log to supply them.
     */
    bool isReplaying() const;
    /// \internal add or remove property from a transactional object
    void addOrRemovePropertyOfObject(TransactionalObject*, Property *prop, bool add);
    //@}

    /** @name dependency stuff */
    //@{
    /// write GraphViz file
    void writeDependencyGraphViz(std::ostream &out);
    /// checks if the graph is directed and has no cycles
    bool checkOnCycle();
    /// get a list of all objects linking to the given object
    std::vector<App::DocumentObject*> getInList(const DocumentObject* me) const;

    /// Option bit flags used by getDepenencyList()
    enum DependencyOption {
        /// Return topological sorted list
        DepSort = 1,
        /// Do no include object linked by PropertyXLink, as it can handle external link
        DepNoXLinked = 2,
        /// Raise exception on cycles
        DepNoCycle = 4,
    };
    /** Get a complete list of all objects the given objects depend on.
     *
     * This function is defined as static because it accepts objects from
     * different documents, and the returned list will contain dependent
     * objects from all relevant documents
     *
     * @param objs: input objects to query for dependency.
     * @param options: See DependencyOption
     */
    static std::vector<App::DocumentObject*> getDependencyList(
            const std::vector<App::DocumentObject*> &objs, int options=0);

    std::vector<App::Document*> getDependentDocuments(bool sort=true);
    static std::vector<App::Document*> getDependentDocuments(std::vector<App::Document*> docs, bool sort);

    // set Changed
    //void setChanged(DocumentObject* change);
    /// get a list of topological sorted objects (https://en.wikipedia.org/wiki/Topological_sorting)
    std::vector<App::DocumentObject*> topologicalSort() const;
    /// get all root objects (objects no other one reference too)
    std::vector<App::DocumentObject*> getRootObjects() const;
    /** get all tree-root objects, i.e. those referenced only by App::Links
     *
     * getRootObjects() returns the roots of the dependency graph, so an object
     * a Link points at is not one. For the tree-level roots, an object whose
     * every parent is a Link still counts as a root.
     */
    std::vector<App::DocumentObject*> getRootObjectsIgnoreLinks() const;
    /// get all possible paths from one object to another following the OutList
    std::vector<std::list<App::DocumentObject*> > getPathsByOutList
    (const App::DocumentObject* from, const App::DocumentObject* to) const;
    //@}

    /** Called by a property during save to store its StringHasher
     *
     * @param hasher: the input hasher
     * @return Returns a pair<bool,int>. The boolean indicates if the
     * StringHasher has been added before. The integer is the hasher index.
     *
     * The StringHasher object is designed to be shared among multiple objects.
     * We must not save duplicate copies of the same hasher, and must be
     * able to restore with the same sharing relationship. This function returns
     * whether the hasher has been added before by other objects, and the index
     * of the hasher. If the hasher has not been added before, the object must
     * save the hasher by calling StringHasher::Save
     */
    std::pair<bool,int> addStringHasher(const StringHasherRef & hasher) const;

    /** Called by property to restore its StringHasher
     *
     * @param index: the index previously returned by calling addStringHasher()
     * during save. Or if is negative, then return document's own string hasher
     * if UseHasher is True
     *
     * @return Return the resulting string hasher.
     *
     * The caller is responsible for restoring the hasher if the caller is the first
     * owner of the hasher, i.e. if addStringHasher() returns true during save.
     */
    StringHasherRef getStringHasher(int index=-1) const;
    /** The external marker a name of this document's gets where a shape of
     * it crosses into the table `hasher` of another document
     * (docs/TransactionLog.md sec 27.76 item 4): `;:X#<id>`, `<id>` being
     * this document's Uid as a string of `hasher`, so a stored name says
     * which document even when the crossing object is gone.
     */
    std::string externalTagPostfix(const StringHasherRef &hasher) const;

    /// Return the document's own hasher regardless of UseHasher
    StringHasherRef getHasher() const;

    /** Return the links to a given object
     *
     * @param links: holds the links found
     * @param obj: the linked object. If NULL, then all links are returned.
     * @param option: @sa App::GetLinkOption
     * @param maxCount: limit the number of links returned, 0 means no limit
     * @param objs: optional objects to search for, if empty, then all objects
     * of this document are searched.
     */
    void getLinksTo(std::set<DocumentObject*> &links,
            const DocumentObject *obj, int options, int maxCount=0,
            const std::vector<DocumentObject*> &objs = {}) const;

    /// Check if there is any link to the given object
    bool hasLinksTo(const DocumentObject *obj) const;

    /// Called by objects during restore to ask for recompute
    void addRecomputeObject(DocumentObject *obj);

    const std::string &getOldLabel() const {return oldLabel;}

    /// Function called to signal that an object identifier has been renamed
    void renameObjectIdentifiers(const std::map<App::ObjectIdentifier, App::ObjectIdentifier> & paths, const std::function<bool(const App::DocumentObject*)> &selector = [](const App::DocumentObject *) { return true; });

    PyObject *getPyObject() override;

    std::string getFullName(bool python=false) const override;

    App::Document *getOwnerDocument() const override;

    /// Indicate if there is any document restoring/importing
    static bool isAnyRestoring();

    /// Indicate if there is any document recomputing
    static bool isAnyRecomputing();

    long getLastObjectId() const;
    void setLastObjectId(long id); 

    /// Return a revision number that will change on object addition or removal
    long getRevision() const;

    std::pair<long, long> treeRanks() const;
    void reorderObjects(const std::vector<App::DocumentObject *> &objs, App::DocumentObject *before);

    void afterImport(App::DocumentObject *obj);

    friend class Application;
    /// because of transaction handling
    friend class TransactionalObject;
    friend class DocumentObject;
    friend class Transaction;
    friend class TransactionDocumentObject;

    /// Destruction
    ~Document() override;

protected:
    /// Construction
    explicit Document(const char *documentName = "");

    bool afterRestore(bool checkPartial=false);
    bool afterRestore(const std::vector<App::DocumentObject *> &, bool checkPartial=false);

    void readObject(Base::XMLReader &reader);
    void writeObject(Base::Writer &writer, App::DocumentObject *obj) const;

    /** The shared default block, written once per object class.
     *
     * buildDefaults picks the classes worth a block, builds a stand-in for
     * each, and records what its eligible properties serialize to
     * (App::SharedDefaults) -- the stand-in itself does not outlive the
     * recording. saveDefaults writes those records; every object of the
     * class is then pointed at its record and saves only what differs from
     * it, byte for byte. See App::SharedDefaults for the mechanism, and
     * writeObjects for what it buys.
     */
    void buildDefaults(Base::Writer &writer,
            const std::vector<App::DocumentObject*>& obj,
            std::map<std::string, SharedDefaults> &defaults) const;
    void saveDefaults(Base::Writer &writer,
            const std::map<std::string, SharedDefaults> &defaults) const;
    /// Read the block, and work out what of it this build does not already produce.
    void restoreDefaults(Base::XMLReader &reader, int count);
    /// Paste that difference onto an object, before its own properties are read.
    void applyDefaults(DocumentObject *obj);

    void _removeObject(DocumentObject* pcObject);
    void _addObject(DocumentObject* pcObject, const char* pObjectName);
    /// checks if a valid transaction is open
    void _checkTransaction(DocumentObject* pcDelObj, const Property *What, int line);
    void breakDependency(DocumentObject* pcObject, bool clear);
    std::vector<App::DocumentObject*> readObjects(Base::XMLReader& reader);
    void writeObjects(const std::vector<App::DocumentObject*>&, Base::Writer &writer) const;
    bool saveToFile(const char* filename) const;

    void onBeforeChange(const Property* prop) override;
    void onChanged(const Property* prop) override;
    /// callback from the Document objects before property will be changed
    void onBeforeChangeProperty(const TransactionalObject *Who, const Property *What);
    /// callback from the Document objects after property was changed
    void onChangedProperty(const DocumentObject *Who, const Property *What);
    /// helper which Recompute only this feature
    /// @return 0 if succeeded, 1 if failed, -1 if aborted by user.
    int _recomputeFeature(DocumentObject* Feat);
    void _clearRedos();
    /// Sec 30.10: the redo steps of whoever acts, which a new step of
    /// theirs ends; every one when the log is off.
    void _clearMyRedos();
    /// The newest undo (or redo) step of whoever acts, null for none.
    Transaction* _myStep(bool undo) const;
    /// Whether `step` can be applied as it is: the document is in the state
    /// the step left.
    bool _atState(const Transaction* step) const;
    /// Take `step` back through the log as a transaction of its own; false
    /// when refused. It leaves a cold step on the other stack.
    bool _revertStep(bool undo, Transaction* step);
    /// What a cold step reverts (docs/TransactionLog.md sec 24.3), read
    /// from the log and checked against the document before anything moves.
    struct ColdRevert;
    /// Read and check the revert of cold `step`; false, with the reason
    /// reported, when it cannot be applied -- the undo is then refused.
    bool _prepareRevert(int64_t seq, const std::string& name, ColdRevert& revert);
    /// Apply a checked revert, recorded into the open undo record.
    void _applyRevert(ColdRevert& revert);
    /// Log the inverse an undo or redo just recorded (docs/TransactionLog.md
    /// sec 24.2); `applied` is the step it applied, `cold` its revert when
    /// it was a cold one.
    void logInverse(const Transaction& applied, const char* kind,
                    const ColdRevert* cold = nullptr);
    /// Write version `num` out as an unpacked project; returns its directory.
    std::string _materialiseVersion(int64_t num, const std::string& where = std::string());
    /// `blobsInStore`: a schema-5 version's blobs go into the file's blob
    /// store, not onto disk, for a document that restores from that store
    /// (docs/TransactionLog.md sec 27.25 item 2).
    static std::string materialiseVersion(TransactionLogCore& log, int64_t num,
                                          const std::string& dir, bool blobsInStore = false);
    /// Apply the log's rows after `after` forward, folded (sec 25.2 item
    /// 3); returns the rows applied and sets `last` to the last one.
    /// `head` 0 is the log's head; with `whole`, a row that cannot be applied
    /// stops the replay with `*whole` false instead of ending it early.
    size_t _replayLog(int64_t after, int64_t& last, int64_t head = 0, bool* whole = nullptr);
    /// The undo and redo stacks the rows after `after` on the current
    /// branch's chain leave, as cold stubs.
    void _rebuildUndoFromLog(int64_t after = 0);
    /// Make this document what `version` (a scratch document holding a
    /// version) is, recorded into the open transaction (sec 24.5); the view
    /// providers too when `views`, or under ViewObjectTransaction.
    void _applyVersion(Document& version, bool views = false);
    /// Version `num` read into a document of its own as openFileVersion()
    /// reads one -- joined to this file's history, blobs by hash, no view,
    /// no log (sec 27.60) -- handed to `fn`, and closed.
    void _readVersion(int64_t num, const std::function<void(Document&)>& fn);
    /// Restore this new document from `dir`, a materialised version of the
    /// file `history` records, as a version document named `fileName`.
    void _restoreAsVersion(const std::shared_ptr<FileHistory>& history, const std::string& dir,
                           const std::string& fileName);
    /// Sec 27.7: share `history`, another document's of the same file.
    void _joinHistory(const std::shared_ptr<FileHistory>& history);
    int64_t _snapshotToLog(const char* kind);
    /// openFileVersion() once it is known no open document is the version.
    static Document* _openVersionDocument(const std::shared_ptr<FileHistory>& history,
                                          const LogVersion& version, bool createView,
                                          const Document* from, bool frozen);
    /// The log's head was moved (sec 30.4 P1, a fast-forward): the
    /// document follows from the state at row `from`, with nothing recorded.
    void _followHead(int64_t from);
    /// The document an import's rows are replayed in (docs/TransactionLog.md
    /// sec 30.15): the one holding branch `mine`, or that branch opened; with
    /// no branch yet, a version document moved to row `base` and put on a
    /// new branch named `stem`, which `mine` becomes. `scratch` says it was
    /// opened for the import, and is the import's to close.
    Document* _importReplay(int64_t base, const std::string& stem, LogBranch& mine, bool& scratch);
    /// The end of an import: its record on the branch, the maps kept with the
    /// branch (`keep`), the replay's document left as a version and closed.
    void _finishImport(Document* replay, bool scratch, int64_t branch, const std::string& file,
                       const std::string& record, const std::string& keep, ImportResult& result);
    /// Make this document what `from`, a document of another copy of the
    /// file, is (sec 30.19), recorded into the open transaction; the maps
    /// are added to. Throws when something of it cannot come. `same` are
    /// the objects of the other copy that are objects of this file (sec
    /// 30.33), by its id for each: one not here comes under its own id.
    void _applyForeignState(Document& from, std::map<long, long>& ids,
                            std::map<std::string, std::string>& names,
                            std::map<std::string, std::string>& renamed,
                            const Document* kin = nullptr,
                            const std::map<long, long>* same = nullptr);
    /// _applyForeignState() as one row of this document's log, the file its
    /// author. False, the row rolled back and `result` saying why, when it
    /// could not come.
    bool _importStateRow(Document& from, const std::string& file, std::map<long, long>& ids,
                         std::map<std::string, std::string>& names, ImportResult& result,
                         const Document* kin = nullptr,
                         const std::map<long, long>* same = nullptr);
    /// importFork() of a file with no history (sec 30.19): one row against
    /// the save its `Version` names.
    /// Let go every sent file whose branch was merged into this one or is
    /// gone (sec 30.29).
    void _releaseSentFiles();
    ImportResult _importState(const std::string& path, const std::string& file,
                              const std::string& saveId, const std::string& hash,
                              const std::string& sender);
    /// Sec 26: refuse a branch operation in the middle of something else;
    /// an implicit transaction is committed first.
    void _checkBranchable(const char* what);
    /// Sec 26: the tip snapshotted and the last id kept, before leaving.
    void _leaveBranch();
    /// Sec 26: the state at the log's head, checked out in place, unrecorded.
    void _checkoutHead(int64_t fromHead = -1);
    /// Move the document from the state at row `fromHead` to the state at
    /// row `toSeq` through the rows between them (sec 27.34); false when a
    /// row cannot be reverted or applied, or the chains do not meet.
    bool _moveAlongLog(int64_t fromHead, int64_t toSeq, bool views);
    /// Sec 26: the undo stacks of the branch arrived on. The id counter is
    /// the file's and does not move (sec 27.40 item 1).
    void _arriveOnBranch();
    /// The document's ids and names, into the file's history just joined.
    void _noteObjectsInHistory() const;
    /// The object ids the ops of `rows` name: taken before they go.
    std::set<long> _objectIdsOfRows(const std::vector<int64_t>& rows);
    /** After rows went (sec 27.48): how many of `named`, the objects their
     * ops named, nothing refers to now -- no op left names it, no document
     * of the file holds it; versions are not read, hence an estimate --
     * added to the file's estimate in bytes, and compactFileState() run
     * when that and the strings nothing holds reach TransactionLogCompactSize.
     */
    CompactEstimate _noteDroppedRows(const std::set<long>& named);
    /** The string table as the file's member (docs/TransactionLog.md sec
     * 27.50 item 1), compacted first (item 4, 27.51 Q3): the whole table,
     * written after Document.xml and named here with its content hash; or,
     * snapshotting for the log, named and not written (item 2).
     */
    void _saveStringTable(Base::Writer& writer);
    /// Read the member `read` was named to, before the objects -- or not,
    /// when the log or an earlier open has the table in memory (item 3).
    void _restoreStringTable(Base::XMLReader& reader, StringHasher& read);
    /** Drop the strings of the document's hasher that nothing in memory
     * holds and, when it is the file's, no retained version or value of the
     * log uses (item 4): from memory and from the store. Keep-all keeps
     * every one. Returns how many went.
     */
    std::size_t _compactStrings();
    /// Keep at most UndoMaxStackSize steps of `stack` hot (sec 24.3): with
    /// the log, the oldest beyond it become cold stubs; without it, they go.
    /// The undo stack only: its steps are deleted oldest first, the order
    /// that never touches an object an older step already destroyed, and
    /// the redo stack cannot be trimmed from its far end in that order.
    /// Delete a transaction, draining the log's queue first when that
    /// destroys an object a queued copy may link to.
    void _deleteTransaction(Transaction* t);
    void _trimHotWindow(std::list<Transaction*>& stack, std::map<int, Transaction*>& map);

    /// refresh the internal dependency graph
    void _rebuildDependencyList(
        const std::vector<App::DocumentObject*> &objs = std::vector<App::DocumentObject*>());

    std::string getTransientDirectoryName(const std::string& uuid, const std::string& filename) const;

    /** Open a new command Undo/Redo, an UTF-8 name can be specified
     *
     * @param name: transaction name
     * @param id: transaction ID, if 0 then the ID is auto generated.
     *
     * @return: Return the ID of the new transaction.
     *
     * This function creates an actual transaction regardless of Application
     * AutoTransaction setting.
     */
    int _openTransaction(const char* name=nullptr, int id=0, bool implicit=false);
    void _openImplicitTransaction();
    /// Internally called by App::Application to commit the Command transaction.
    void _commitTransaction(bool notify=false);
    /// Internally called by App::Application to abort the running transaction.
    void _abortTransaction();

    void _addOrRemoveProperty(TransactionalObject*, Property *prop, bool add);

    /// Close the Document.xml tap restore(const char*) opened for the log.
    void endRestoreTap(Base::XMLReader& reader);
    /// A version was taken: the snapshot cadence starts over.
    void noteVersionTaken();
    /// Set (or clear) the History and Version properties for a save
    /// (docs/TransactionLog.md sec 16.4, embedded mode).
    void embedHistory(bool archive);
    /// Continue the log from the History property's copy when the guard
    /// agrees that the file is the save that wrote it.
    bool adoptEmbeddedHistory();

private:
    // # Data Member of the document +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    std::list<Transaction*> mUndoTransactions;
    std::map<int,Transaction*> mUndoMap;
    std::list<Transaction*> mRedoTransactions;
    std::map<int,Transaction*> mRedoMap;

    struct DocumentP* d;

    std::string oldLabel;
    std::string myName;
};

template<typename T>
inline std::vector<T*> Document::getObjectsOfType() const
{
    std::vector<T*> type;
    std::vector<App::DocumentObject*> obj = this->getObjectsOfType(T::getClassTypeId());
    type.reserve(obj.size());
    for (std::vector<App::DocumentObject*>::iterator it = obj.begin(); it != obj.end(); ++it)
        type.push_back(static_cast<T*>(*it));
    return type;
}

template<typename T>
inline int Document::countObjectsOfType() const
{
    static_assert(std::is_base_of_v<DocumentObject, T>,
                  "T must be derived from App::DocumentObject");
    return this->countObjectsOfType(T::getClassTypeId());
}

template<typename T>
T* Document::addObject(const char* pObjectName, bool isNew, const char* viewType, bool isPartial)
{
    static_assert(std::is_base_of_v<DocumentObject, T>,
                  "T must be derived from App::DocumentObject");
    // ! Upstream spells this T::getClassName(), a consteval string that its
    // PROPERTY_HEADER_WITH_OVERRIDE macro emits. We have no such member, and
    // adding one is a change to every property container rather than an
    // addition. The registered type name is the same string and is what the
    // non-template overload looks up anyway.
    return static_cast<T*>(addObject(T::getClassTypeId().getName(), pObjectName, isNew,
                                     viewType, isPartial));
}

} //namespace App

#endif // APP_DOCUMENT_H
