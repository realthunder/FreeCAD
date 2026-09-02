/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
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


#ifndef APP_PROPERTFILE_H
#define APP_PROPERTFILE_H

#include <string>

#include "FileBlobManager.h"
#include "FileSet.h"
#include "PropertyStandard.h"


namespace Base {
class Writer;
}

namespace App
{

/** File properties
  * This property holds a file name
  */
class AppExport PropertyFile : public PropertyString
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyFile();
    ~PropertyFile() override;

    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyFileItem"; }

    void setPyObject(PyObject *) override;
    virtual void setFilter(const std::string filter);
    virtual std::string getFilter() const;

private:
    std::string m_filter;
};

/** File include properties
  * This property doesn't only save the file name like PropertyFile
  * it also includes the file itself into the document. The file
  * doesn't get loaded into memory, it gets copied from the document
  * archive into the document transient directory. There it is accessible for
  * the algorithms. You get the transient path through getDocTransientPath()
  * It's allowed to read the file, it's not allowed to write the file
  * directly in the transient path! That would undermine the Undo/Redo
  * framework. It's only allowed to use setValue() to change the file.
  * If you give a file name outside the transient dir to setValue() it
  * will copy the file. If you give a file name in the transient path it
  * will just rename and use the same file. You can use getExchangeTempFile() to 
  * get a file name in the transient dir to write a new file version.
 */
class AppExport PropertyFileIncluded : public Property, public BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyFileIncluded();
    ~PropertyFileIncluded() override;

    void setValue(const char* sFile, const char* sName=nullptr);
    void setValue(const std::string &sFile) {
        setValue(sFile.c_str());
    }
    const char* getValue() const;

    const char* getEditorName() const override
    { return "Gui::PropertyEditor::PropertyTransientFileItem"; }
    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    void SaveDocFile (Base::Writer &writer) const override;
    void RestoreDocFile(Base::Reader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

    Property *copyBeforeChange(void) const override;

    bool isSame(const Property &other) const override;

    /** get a temp file name in the transient path of the document.
      * Using this file for new Version of the file and set 
      * this file with setValue() is the fastest way to change
      * the File.
      */
    std::string getExchangeTempFile() const;
    /** Path the content was originally read from.
     *
     * Persisted with the document, so it survives a restore and stays this
     * property's own -- blobs are shared by content, names are not.
     */
    std::string getOriginalFileName() const;
    /// Name this property stores its file under, i.e. the archive entry name.
    const std::string &getBaseFileName() const {return _BaseFileName;}

    bool isEmpty() const {return !_blob;}

    /// The blob this property references, or null. Holding the handle keeps
    /// the file alive independently of this property.
    const FileBlobHandle &getBlob() const {return _blob;}

    /** Take the content the manager restored on this property's behalf.
     *
     * Called by FileBlobManager once the archive entry holding the content
     * has been read, or straight away when it had been read already. Not a
     * value change: it completes the restore of a value the document already
     * had, so it must not touch the document.
     */
    void assignRestoredBlob(const FileBlobHandle &blob) override;

    /// Note this property's file for the save in progress, named after it.
    void collectBlobs(FileBlobManager &manager, const DocumentObject *object) const override;

    /// The extension of the name this property stores its file under.
    std::string blobExtension() const override;

    void setFilter(std::string filter);
    std::string getFilter() const;

protected:
    // get the transient path if the property is in a DocumentObject
    std::string getDocTransientPath() const;
    std::string getUniqueFileName(const std::string&, const std::string&) const;
    /** Store owning the referenced file's lifetime.
     *
     * The owner document's manager when there is one, so that a view's
     * property shares the same store as the document's objects. Falls back to
     * a process-wide temporary store for a property with no document.
     */
    FileBlobManager &blobManager() const;
    /// Serialized form of the original path, omitted when unknown.
    std::string originalAttribute() const;

protected:
    /// Reference to the file. Its destruction is what deletes the file, once
    /// no other property, undo snapshot or clipboard entry still holds it.
    mutable FileBlobHandle _blob;
    /// Path written by Restore() and claimed by RestoreDocFile() once the
    /// archive content has actually been streamed to it.
    mutable std::string _pendingPath;
    /// Manager this property is queued with, waiting for its content. Held so
    /// the queue entry can be withdrawn without asking the container, which
    /// may already be halfway through its own destruction.
    FileBlobManager *_pendingManager {nullptr};
    mutable std::string _BaseFileName;
    mutable std::string _OriginalName;

private:
    std::string m_filter;
};

/** A string whose stored form is a shared, content-addressed file.
 *
 * The value behaves exactly like PropertyString -- same accessors, same
 * Python type, same property editor -- and only the way it is SAVED
 * differs: text at or above inlineLimit() goes to App::FileBlobManager as
 * one archive entry named by its content hash, and Document.xml carries
 * the hash instead of the text. Two properties holding the same text
 * therefore cost one entry, wherever in the document they are.
 *
 * Shader sources are what this is for. A MaterialX document is kilobytes
 * of XML; the seventeen of the MaterialX demo went into Document.xml
 * inline, and the same document assigned to two objects went in twice,
 * because PropertyString has no route into the blob store that the
 * material cards and included files already share.
 *
 * Short text stays inline, in the same <String value="..."/> element it
 * always used: an archive entry per one-line source costs more than it
 * saves. Sharing the element spelling is also what lets this type restore
 * a document written while these properties were plain PropertyStrings --
 * the container reports the type change, and the value reads back
 * unchanged (App::ShaderProgram::handleChangedPropertyType).
 *
 * The content arrives asynchronously on restore, once the archive entries
 * have been drained, which is AFTER the view document has been read. A
 * consumer that builds something from the text at restore time has to
 * rebuild it when the document finishes restoring rather than when its
 * view provider attaches.
 */
class AppExport PropertyStringIncluded : public PropertyString,
                                         public BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyStringIncluded();
    ~PropertyStringIncluded() override;

    /// Any value change drops the stored form: the blob is the file the
    /// PREVIOUS text was written to, and the next save makes a new one.
    void setValue(const char* sString) override;
    using PropertyString::setValue;

    /** Size at which the text stops being written inline.
     *
     * A blob costs an archive entry, an index line and a hash attribute --
     * a few hundred bytes between them -- so text shorter than this is
     * cheaper where it already was.
     */
    static std::string::size_type inlineLimit() { return 512; }

    /** Extension the content is stored under, with or without the dot.
     *
     * Only names the archive entry (`Object.Property.mtlx`), which is what
     * an unpacked project shows and what version control follows. The
     * owner sets it when it knows what the text is.
     */
    void setBlobExtension(const char* ext);
    /// What the owner set, without the dot.
    std::string blobExtension() const override { return _ext; }

    /// The blob holding this property's text, or null while it is inline.
    const FileBlobHandle &getBlob() const { return _blob; }

    void Save (Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize () const override;

    /// Take the text the manager restored on this property's behalf.
    void assignRestoredBlob(const FileBlobHandle &blob) override;
    /// Note this property's text for the save in progress, named after it.
    void collectBlobs(FileBlobManager &manager, const DocumentObject *object) const override;

protected:
    /// Store owning the content: the document's when there is one.
    FileBlobManager &blobManager() const;
    /** The blob for the current text, made if there is not one yet.
     *
     * Null for text below inlineLimit(), which is never given a file, and
     * for a write that failed -- the caller falls back to inline, so a
     * failure here costs sharing and never content.
     */
    const FileBlobHandle &ensureBlob() const;

protected:
    /// The file the current text is stored in, once something has asked for
    /// one. Null means the text has no stored form yet, not that it is empty.
    mutable FileBlobHandle _blob;
    /// Manager this property is queued with, waiting for its content.
    FileBlobManager *_pendingManager {nullptr};
    /// Stored without the dot, the form the store and the referrer both want.
    std::string _ext {"txt"};
};


/** A property holding an App::FileSet: a named set of shared files.
 *
 * PropertyFileIncluded holds one file; this holds any number of them,
 * each under the NAME the content referring to them uses to ask for one
 * -- see App::FileSet for why that name is the point of the type, and
 * for everything about the entries themselves. What this adds is what
 * only a property can: the store (the owner document's), the queue for
 * content that arrives after the document has been read, the change
 * signalling, and the Python form (a dict of name to path).
 *
 * There is no pre-store spelling of a set of files, so
 * blobContentNeedsStore() answers true whenever there is one to write
 * and the save offers the schema the content needs.
 */
class AppExport PropertyFileIncludedList : public Property,
                                           public BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    using Entry = FileSet::Entry;

    PropertyFileIncludedList();
    ~PropertyFileIncludedList() override;

    /// The set itself, for whoever wants more than one entry at a time.
    const FileSet &getValue() const { return _files; }
    /// The files, in the order they were added.
    const std::vector<Entry> &getValues() const { return _files.entries(); }
    int getSize() const { return _files.size(); }
    bool isEmpty() const { return _files.empty(); }

    /// Add the file at \a path under \a name, replacing any file of that
    /// name. See FileSet::setFile.
    void setFile(const char *name, const char *path, const char *original = nullptr);
    /// Add \a blob under \a name, replacing any file of that name. See
    /// FileSet::setBlob.
    void setBlob(const char *name, const FileBlobHandle &blob, const char *original = nullptr);
    /// Drop the file called \a name. Nothing happens if there is none.
    void removeFile(const char *name);
    /// Replace the whole set.
    void setValues(std::vector<Entry> files);
    /// Replace the whole set with another set's entries.
    void setValue(const FileSet &files);
    void clear();
    /// The default value, which is the empty set. Only here because the
    /// ADD_PROPERTY macros initialize a property by calling setValue,
    /// and a set of files has no other value to be given one of.
    void setValue() { clear(); }

    /// The entry called \a name, or null.
    const Entry *find(const char *name) const { return _files.find(name); }
    /// Absolute path of the file called \a name, empty when it cannot be
    /// opened. See FileSet::filePath.
    std::string filePath(const char *name) const { return _files.filePath(name); }

    PyObject *getPyObject() override;
    void setPyObject(PyObject *) override;

    void Save(Base::Writer &writer) const override;
    void Restore(Base::XMLReader &reader) override;

    Property *Copy() const override;
    void Paste(const Property &from) override;
    unsigned int getMemSize() const override;
    bool isSame(const Property &other) const override;
    /// No compare-before-change: the value is a set of files, and the
    /// blob handles a snapshot would hold are the point of the type.
    Property *copyBeforeChange() const override { return nullptr; }

    /// Take the content the manager restored for one of the entries.
    void assignRestoredBlob(const FileBlobHandle &blob) override;
    /// Note every file of the set, each named after the entry that holds
    /// it so an unpacked project shows what it is.
    void collectBlobs(FileBlobManager &manager, const DocumentObject *object) const override;
    /// True while there is anything to write: a set of files has no
    /// spelling below schema 5, so the save has to offer one.
    bool blobContentNeedsStore() const override { return !_files.empty(); }

protected:
    /// Store owning the content: the document's when there is one.
    FileBlobManager &blobManager() const;
    /// Queue every entry whose content has not arrived with the manager
    /// that will serve it.
    void awaitPending();
    /// Withdraw every queued request, e.g. because the value is being
    /// replaced or the property is going away.
    void cancelPending();

protected:
    FileSet _files;
    /// Manager this property is queued with, waiting for content. Held so
    /// the queue entries can be withdrawn without asking the container,
    /// which may already be halfway through its own destruction.
    FileBlobManager *_pendingManager {nullptr};
};


} // namespace App

#endif // APP_PROPERTFILE_H
