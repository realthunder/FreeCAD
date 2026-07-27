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
class AppExport PropertyFileIncluded : public Property
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
    void assignRestoredBlob(const FileBlobHandle &blob);

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


} // namespace App

#endif // APP_PROPERTFILE_H
