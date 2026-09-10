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


#include "PreCompiled.h"

#include <QDir>

#include <Base/Console.h>
#include <Base/PyObjectBase.h>
#include <Base/Reader.h>
#include <Base/Stream.h>
#include <Base/Writer.h>
#include <Base/Uuid.h>
#include <Base/Tools.h>

#include "PropertyFile.h"
#include "Document.h"
#include "DocumentObject.h"
#include "PropertyContainer.h"


using namespace App;
using namespace Base;
using namespace std;

FC_LOG_LEVEL_INIT("App", true, 2, true)



//**************************************************************************
// PropertyFileIncluded
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFileIncluded , App::Property)


PropertyFileIncluded::PropertyFileIncluded() = default;

// The file is owned by the blob, which outlives this property whenever an undo
// snapshot, the clipboard or another property still references the same
// content. Releasing the handle is all that is needed here -- except when the
// property is still queued for content it will now never take, which happens
// when an object fails to be created partway through a restore.
PropertyFileIncluded::~PropertyFileIncluded()
{
    if (_pendingManager) {
        _pendingManager->removePendingReferrer(this);
    }
}

FileBlobManager &PropertyFileIncluded::blobManager() const
{
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument()) {
            return doc->getFileBlobManager();
        }
    }
    return FileBlobManager::defaultManager();
}

std::string PropertyFileIncluded::getDocTransientPath() const
{
    std::string path;
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument()) {
            path = QDir::fromNativeSeparators(QString::fromUtf8(
                        doc->TransientDir.getValue())).toUtf8().constData();
        }
    }
    return path;
}

std::string PropertyFileIncluded::getUniqueFileName(const std::string& path, const std::string& filename) const
{
    Base::Uuid uuid;
    Base::FileInfo fi(path + "/" + filename);
    while (fi.exists()) {
        fi.setFile(path + "/" + filename + "." + uuid.getValue());
    }

    return fi.filePath();
}

std::string PropertyFileIncluded::getExchangeTempFile() const
{
    return Base::FileInfo::getTempFileName(Base::FileInfo
        (getValue()).fileName().c_str(), blobManager().transientPath().c_str());
}

std::string PropertyFileIncluded::getOriginalFileName() const
{
    return _OriginalName;
}

void PropertyFileIncluded::setValue(const char* sFile, const char* sName)
{
    if (sFile && sFile[0] != '\0') {
        if (_blob && _blob->path() == sFile)
            THROWM(Base::FileSystemError, "Not possible to set the same file!")

        Base::FileInfo file(sFile);
        if (!file.exists()) {
            std::stringstream str;
            str << "File " << file.filePath() << " does not exist.";
            THROWM(Base::FileSystemError, str.str())
        }

        auto &manager = blobManager();
        const std::string pathTrans = manager.transientPath();

        // The name is this property's own, not the blob's: an explicit one
        // wins, otherwise a property that already holds a file keeps the name
        // it is stored under, and only a fresh one takes the source's name.
        std::string name = (sName && sName[0] != '\0') ? sName : std::string();
        if (name.empty())
            name = _BaseFileName.empty() ? file.fileName() : _BaseFileName;

        // A writable file already sitting in the transient directory is a
        // scratch file handed over by getExchangeTempFile(): claim it instead
        // of copying. Read-only ones belong to another blob and must not move.
        // The store keeps the extension of the name this property gives the
        // content, not of the file it happened to arrive in -- a scratch file
        // from getExchangeTempFile() says nothing about what it holds.
        const std::string ext = Base::FileInfo(name).extension();

        FileBlobHandle blob;
        if (file.dirPath() == pathTrans && file.isWritable())
            blob = manager.adoptFile(sFile, ext.c_str());
        else
            blob = manager.insertFile(sFile, ext.c_str());

        aboutToSetValue();
        _blob = std::move(blob);
        // keep the path to the original file
        _OriginalName = sFile;
        _BaseFileName = name;
        hasSetValue();
    }
    else if (_blob) {
        aboutToSetValue();
        // Releasing the handle deletes the file only if nothing else refers to
        // it -- an undo snapshot taken a moment ago typically does.
        _blob.reset();
        hasSetValue();
    }
}

void PropertyFileIncluded::collectBlobs(FileBlobManager& manager,
                                       const DocumentObject* object) const
{
    // The referrer is what names the file the content is saved to, and this
    // pass is where the property is known. Null blobs are ignored by the
    // manager, so a property holding nothing costs a call and no more.
    manager.noteReferenced(_blob, FileBlobManager::referrerOf(this, object));
}

std::string PropertyFileIncluded::blobExtension() const
{
    return Base::FileInfo(_BaseFileName).extension();
}

void PropertyFileIncluded::assignRestoredBlob(const FileBlobHandle& blob)
{
    // No aboutToSetValue()/hasSetValue(): this completes a restore rather than
    // changing anything, and touching the document here would mark it modified
    // just by being opened.
    _blob = blob;
    _pendingManager = nullptr;
}

const char* PropertyFileIncluded::getValue() const
{
    static const std::string empty;
    return _blob ? _blob->path().c_str() : empty.c_str();
}

PyObject *PropertyFileIncluded::getPyObject()
{
    static const std::string empty;
    const std::string &value = _blob ? _blob->path() : empty;
    PyObject *p = PyUnicode_DecodeUTF8(value.c_str(),value.size(),nullptr);
    if (!p) {
        THROWM(Base::UnicodeError, "PropertyFileIncluded: UTF-8 conversion failure")
    }
    return p;
}

namespace App {
const char* getNameFromFile(PyObject* value)
{
    const char* string = nullptr;
    PyObject *oname = PyObject_GetAttrString (value, "name");
    if (oname) {
        if (PyUnicode_Check (oname)) {
            string = PyUnicode_AsUTF8 (oname);
        }
        else if (PyBytes_Check (oname)) {
            string = PyBytes_AsString (oname);
        }
        Py_DECREF (oname);
    }

    if (!string)
        THROWM(Base::TypeError, "Unable to get filename")
    return string;
}



bool isIOFile(PyObject* file)
{
    PyObject* io = PyImport_ImportModule("io");
    PyObject* IOBase_Class = PyObject_GetAttrString(io, "IOBase");
    bool isFile = PyObject_IsInstance(file, IOBase_Class);
    Py_DECREF(IOBase_Class);
    Py_DECREF(io);
    return isFile;
}
}

void PropertyFileIncluded::setPyObject(PyObject *value)
{
    if (PyUnicode_Check(value)) {
        std::string string = PyUnicode_AsUTF8(value);
        setValue(string.c_str());
    }
    else if (PyBytes_Check(value)) {
        std::string string = PyBytes_AsString(value);
        setValue(string.c_str());
    }
    else if (isIOFile(value)){
        std::string string = getNameFromFile(value);
        setValue(string.c_str());
    }
    else if (PyTuple_Check(value)) {
        if (PyTuple_Size(value) != 2)
            THROWM(Base::TypeError, "Tuple needs size of (filePath,newFileName)")
        PyObject* file = PyTuple_GetItem(value,0);
        PyObject* name = PyTuple_GetItem(value,1);

        // decoding file
        std::string fileStr;
        if (PyUnicode_Check(file)) {
            fileStr = PyUnicode_AsUTF8(file);
        }
        else if (PyBytes_Check(file)) {
            fileStr = PyBytes_AsString(file);
        }
        else if (isIOFile(value)) {
            fileStr = getNameFromFile(file);
        }
        else {
            std::string error = std::string("First item in tuple must be a file or string, not ");
            error += file->ob_type->tp_name;
            THROWM(Base::TypeError, error)
        }

        // decoding name
        std::string nameStr;
        if (PyUnicode_Check(name)) {
            nameStr = PyUnicode_AsUTF8(name);
        }
        else if (PyBytes_Check(name)) {
            nameStr = PyBytes_AsString(name);
        }
        else if (isIOFile(value)) {
            nameStr = getNameFromFile(name);
        }
        else {
            std::string error = std::string("Second item in tuple must be a string, not ");
            error += name->ob_type->tp_name;
            THROWM(Base::TypeError, error)
        }

        setValue(fileStr.c_str(),nameStr.c_str());
    }
    else if (PyDict_Check(value)) {
        Py::Dict dict(value);
        if (dict.hasKey("filter")) {
            setFilter(Py::String(dict.getItem("filter")));
        }
        if (dict.hasKey("filename")) {
            std::string string = static_cast<std::string>(Py::String(dict.getItem("filename")));
            setValue(string.c_str());
        }
    }
    else {
        std::string error = std::string("Type must be string or file, not ");
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }
}

std::string PropertyFileIncluded::originalAttribute() const
{
    if (_OriginalName.empty())
        return {};
    return std::string(" original=\"") + encodeAttribute(_OriginalName) + "\"";
}

void PropertyFileIncluded::Save (Base::Writer &writer) const
{
    // when saving a document under a new file name the transient directory
    // name changes and thus the stored file name doesn't work any more.
    if (_blob && !Base::FileInfo(_blob->path()).exists()) {
        auto &manager = blobManager();
        // Saving under a new name gives the document a new transient
        // directory, so the stored absolute path is stale. The directory is
        // renamed with its contents, so the file is still called what it was.
        Base::FileInfo fi(manager.relocatedPath(_blob));
        if (!fi.exists()) {
            // Content written by an older version sat directly in the
            // transient directory under its file name.
            fi.setFile(manager.transientPath() + "/" + _BaseFileName);
        }
        if (fi.exists())
            manager.repath(_blob, fi.filePath());
    }

    // Schema 5 and later: the manager writes one archive entry per blob and
    // this property stores only the hash, so referrers sharing content share
    // the entry. Writers that produce a self-contained stream of their own
    // leave the schema unset and keep the per-property entry below.
    //
    // Where the manager puts that content -- its own archive entries, or a
    // base64 table inside Document.xml when the writer is asked for pure XML
    // -- is the manager's business and does not change what is written here.
    if (writer.getSchemaVersion() >= 5) {
        if (_blob) {
            // The save-time collect pass has noted this blob already. Note it
            // again anyway: this costs nothing, and it keeps a property that
            // is written through some path the collect pass does not walk
            // from losing its content, since the entries are written after
            // Document.xml.
            blobManager().noteReferenced(_blob, FileBlobManager::referrerOf(this));
            writer.Stream() << writer.ind() << "<FileIncluded hash=\""
                            << encodeAttribute(_blob->hash()) << "\" name=\""
                            << encodeAttribute(_BaseFileName) << "\""
                            << originalAttribute() << "/>\n";
        }
        else {
            writer.Stream() << writer.ind() << "<FileIncluded hash=\"\"/>\n";
        }
        return;
    }

    if (writer.isForceXML()>3) {
        if (_blob) {
            writer.Stream() << writer.ind() << "<FileIncluded data=\""
                            << encodeAttribute(_BaseFileName) << "\""
                            << originalAttribute() << ">\n";
            // write the file in the XML stream
            writer.insertBinFile(_blob->path().c_str());
            writer.Stream() << writer.ind() <<"</FileIncluded>\n";
        }
        else {
            writer.Stream() << writer.ind() << "<FileIncluded data=\"\"/>\n";
        }
    }
    else {
        // instead initiate an extra file
        if (_blob) {
            // The entry is named after this property, not after the blob: the
            // file on disk is named by content hash and is shared.
            std::string filename = writer.addFile(_BaseFileName.c_str(), this);
            filename = encodeAttribute(filename);
            writer.Stream() << writer.ind() << "<FileIncluded file=\""
                            << filename << "\""
                            << originalAttribute() << "/>\n";
        }
        else {
            writer.Stream() << writer.ind() << "<FileIncluded file=\"\"/>\n";
        }
    }
}

void PropertyFileIncluded::Restore(Base::XMLReader &reader)
{
    reader.readElement("FileIncluded");
    if (reader.hasAttribute("hash")) {
        const std::string hash = reader.getAttribute("hash");
        _BaseFileName = reader.hasAttribute("name") ? reader.getAttribute("name") : "";
        _OriginalName = reader.hasAttribute("original")
            ? reader.getAttribute("original") : "";
        if (!hash.empty()) {
            // The manager owns the content and hands it over: at once if it
            // has been read already, otherwise when the archive entries have
            // been drained. Either way this property never has to go looking.
            auto &manager = blobManager();
            _pendingManager = &manager;
            manager.addPendingReferrer(hash, this);
        }
    }
    else if (reader.hasAttribute("file")) {
        string file (reader.getAttribute("file") );
        if (!file.empty()) {
            // initiate a file read
            reader.addFile(file.c_str(),this);
            // The content arrives later in RestoreDocFile(), which is what
            // claims the blob. Only note where it is going: two objects
            // restoring the same archive entry must not collide on disk.
            _BaseFileName = file;
            _OriginalName = reader.hasAttribute("original")
                ? reader.getAttribute("original") : "";
            _pendingPath = blobManager().uniquePath(file);
        }
    }
    // section is XML stream
    else if (reader.hasAttribute("data")) {
        string file (reader.getAttribute("data") );
        if (!file.empty()) {
            auto &manager = blobManager();
            const std::string original = reader.hasAttribute("original")
                ? reader.getAttribute("original") : "";
            const std::string path = manager.uniquePath(file);
            reader.readBase64(path.c_str());
            reader.readEndElement("FileIncluded");

            aboutToSetValue();
            // adoptFile() de-duplicates: restoring the same content twice
            // keeps one file and hands back the same blob. The extension
            // comes from the stored name rather than from the staging path,
            // which uniquePath() may have made unique with a uuid.
            _blob = manager.adoptFile(path.c_str(),
                                      Base::FileInfo(file).extension().c_str());
            _BaseFileName = file;
            _OriginalName = original;
            hasSetValue();
        }
    }
}

void PropertyFileIncluded::SaveDocFile (Base::Writer &writer) const
{
    const std::string path = _blob ? _blob->path() : std::string();
    Base::ifstream from(Base::FileInfo(path), std::ios::in | std::ios::binary);
    if (!from) {
        std::stringstream str;
        str << "PropertyFileIncluded::SaveDocFile(): "
            << "File '" << path << "' in transient directory doesn't exist.";
        THROWM(Base::FileSystemError, str.str())
    }

    writer.Stream() << from.rdbuf();
}

void PropertyFileIncluded::RestoreDocFile(Base::Reader &reader)
{
    if (_pendingPath.empty()) {
        return;
    }
    const std::string path = _pendingPath;
    _pendingPath.clear();

    Base::FileInfo fi(path);
    Base::ofstream to(fi, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!to) {
        std::stringstream str;
        str << "PropertyFileIncluded::RestoreDocFile(): "
            << "File '" << path << "' in transient directory cannot be created.";
        THROWM(Base::FileSystemError, str.str())
    }

    // copy plain data
    aboutToSetValue();

    // Written, not extracted: extraction skips leading whitespace, so content
    // that starts with any came back short. See readBlobEntry() for the same
    // fix and what content addressing turns that into.
    to << reader.rdbuf();
    to.close();

    // Hand the file to the store. Two objects referencing identical content --
    // copy&paste inside a document, for instance -- collapse onto one blob
    // here, which is what the old read-only sentinel check used to approximate.
    _blob = blobManager().adoptFile(path.c_str(),
                                    Base::FileInfo(_BaseFileName).extension().c_str());
    hasSetValue();
}

Property *PropertyFileIncluded::copyBeforeChange(void) const
{
    // No support for property compare due to internal file handling.
    return nullptr;
}

bool PropertyFileIncluded::isSame(const Property &other) const
{
    if (&other == this)
        return true;
    if (getTypeId() != other.getTypeId())
        return false;
    auto prop = static_cast<decltype(this)>(&other);
    if (_BaseFileName != prop->_BaseFileName || _OriginalName != prop->_OriginalName)
        return false;
    if (_blob == prop->_blob)
        return true;
    if (!_blob || !prop->_blob)
        return false;
    return _blob->hash() == prop->_blob->hash();
}

Property *PropertyFileIncluded::Copy() const
{
    std::unique_ptr<PropertyFileIncluded> prop(new PropertyFileIncluded());

    // remember the names -- they belong to the property, not the blob
    prop->_BaseFileName = _BaseFileName;
    prop->_OriginalName = _OriginalName;
    // Sharing the blob is the whole point: an undo snapshot of a 50MB texture
    // now costs a reference, not 50MB. The file outlives whichever of the two
    // properties dies first.
    prop->_blob = _blob;

    return prop.release();
}

void PropertyFileIncluded::Paste(const Property &from)
{
    const PropertyFileIncluded &prop = dynamic_cast<const PropertyFileIncluded&>(from);

    FileBlobHandle blob = prop._blob;
    if (blob) {
        auto &manager = blobManager();
        // Blobs never migrate between stores; pasting across documents imports
        // the content into this document's own transient directory.
        if (blob->owner() != &manager)
            blob = manager.insertFile(blob->path().c_str());
    }

    aboutToSetValue();
    _blob = std::move(blob);
    _BaseFileName = prop._BaseFileName;
    _OriginalName = prop._OriginalName;
    hasSetValue();
}

unsigned int PropertyFileIncluded::getMemSize () const
{
    unsigned int mem = Property::getMemSize();
    if (_blob)
        mem += static_cast<unsigned int>(_blob->path().size());
    mem += _BaseFileName.size();
    return mem;
}

void PropertyFileIncluded::setFilter(std::string filter)
{
    m_filter = std::move(filter);
}

std::string PropertyFileIncluded::getFilter() const
{
    return m_filter;
}

//**************************************************************************
// PropertyFile
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFile , App::PropertyString)

PropertyFile::PropertyFile()
{
    m_filter = "";
}

PropertyFile::~PropertyFile() = default;

void PropertyFile::setFilter(const std::string f)
{
    m_filter = f;
}

std::string PropertyFile::getFilter() const
{
    return m_filter;
}

void PropertyFile::setPyObject(PyObject *value)
{
    if (PyDict_Check(value)) {
        Py::Dict dict(value);
        if (dict.hasKey("filter")) {
            setFilter(Py::String(dict.getItem("filter")));
        }
        if (dict.hasKey("filename")) {
            std::string string = static_cast<std::string>(Py::String(dict.getItem("filename")));
            setValue(string.c_str());
        }
    }
    else {
        PropertyString::setPyObject(value);
    }
}


//**************************************************************************
// PropertyStringIncluded
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyStringIncluded , App::PropertyString)

PropertyStringIncluded::PropertyStringIncluded() = default;

// Same reasoning as ~PropertyFileIncluded: the file belongs to the blob, so
// letting go of the handle is all that is owed -- except when this property
// is still queued for content it will now never take.
PropertyStringIncluded::~PropertyStringIncluded()
{
    if (_pendingManager) {
        _pendingManager->removePendingReferrer(this);
    }
}

FileBlobManager &PropertyStringIncluded::blobManager() const
{
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument()) {
            return doc->getFileBlobManager();
        }
    }
    return FileBlobManager::defaultManager();
}

void PropertyStringIncluded::setBlobExtension(const char* ext)
{
    _ext = (ext && ext[0]) ? ext : "txt";
    if (!_ext.empty() && _ext.front() == '.') {
        _ext.erase(0, 1);   // taken either way; stored the way both users want
    }
}

void PropertyStringIncluded::setValue(const char* sString)
{
    // The blob holds the text it was made from. A new value has no stored
    // form until something asks for one, and keeping the old handle here
    // would save the previous text under the new value's name.
    _blob.reset();
    PropertyString::setValue(sString);
}

const FileBlobHandle &PropertyStringIncluded::ensureBlob() const
{
    auto &manager = blobManager();
    if (_blob && !Base::FileInfo(_blob->path()).exists()) {
        // Saving under a new name gives the document a new transient
        // directory, so the stored path is stale. The directory is renamed
        // with its contents, so the file is still where relocatedPath() says.
        Base::FileInfo moved(manager.relocatedPath(_blob));
        if (moved.exists()) {
            manager.repath(_blob, moved.filePath());
        }
        else {
            // Writing the hash of content nothing can read would lose the
            // text. It cannot be lost here: the value is in memory, so drop
            // the handle and store it again below.
            FC_WARN(getFullName() << ": stored text is gone from "
                    << _blob->path() << ", writing it again");
            _blob.reset();
        }
    }
    if (_blob || _cValue.size() < inlineLimit()) {
        return _blob;
    }
    try {
        // Written where the store adopts from, then handed over: adoptFile()
        // hashes it and drops the duplicate when the same text is already
        // stored, which is what makes one document assigned fifty times cost
        // one file.
        const std::string path = manager.uniquePath("string." + _ext);
        {
            Base::ofstream to(Base::FileInfo(path),
                              std::ios::out | std::ios::binary | std::ios::trunc);
            if (!to) {
                FC_ERR("cannot write " << getFullName() << " to " << path
                        << ", storing it inline");
                return _blob;
            }
            to.write(_cValue.data(), std::streamsize(_cValue.size()));
        }
        _blob = manager.adoptFile(path.c_str(), _ext.c_str());
    }
    catch (const Base::Exception &e) {
        // Losing the blob costs sharing, never content: the caller writes
        // the text inline instead.
        FC_ERR("cannot store " << getFullName() << " as a file: " << e.what());
        _blob.reset();
    }
    return _blob;
}

void PropertyStringIncluded::collectBlobs(FileBlobManager& manager,
                                          const DocumentObject* object) const
{
    // The collect pass runs before anything is written, so this is where the
    // file has to exist. Null blobs -- short text -- are ignored by the
    // manager, so a property staying inline costs a call and no more.
    manager.noteReferenced(ensureBlob(), FileBlobManager::referrerOf(this, object));
}

void PropertyStringIncluded::assignRestoredBlob(const FileBlobHandle& blob)
{
    // No aboutToSetValue()/hasSetValue(): this completes the restore of a
    // value the document already had, and touching the document here would
    // mark it modified just by being opened.
    _pendingManager = nullptr;
    _blob = blob;
    _cValue.clear();
    if (!blob) {
        return;
    }
    Base::ifstream from(Base::FileInfo(blob->path()),
                        std::ios::in | std::ios::binary);
    if (!from) {
        FC_ERR("cannot read " << getFullName() << " back from " << blob->path());
        return;
    }
    std::ostringstream str;
    str << from.rdbuf();
    _cValue = str.str();
}

void PropertyStringIncluded::Save (Base::Writer &writer) const
{
    // The store exists at schema 5 and above; below it the text goes inline,
    // which forfeits sharing rather than content (blobContentNeedsStore()
    // stays false for exactly that reason).
    if (writer.getSchemaVersion() >= 5) {
        if (const FileBlobHandle &blob = ensureBlob()) {
            // Noted again here for the same reason PropertyFileIncluded does:
            // it costs nothing, and it keeps a property written through a
            // path the collect pass does not walk from losing its content.
            blobManager().noteReferenced(blob, FileBlobManager::referrerOf(this));
            writer.Stream() << writer.ind() << "<String hash=\""
                            << encodeAttribute(blob->hash()) << "\"/>\n";
            return;
        }
    }
    PropertyString::Save(writer);
}

void PropertyStringIncluded::Restore(Base::XMLReader &reader)
{
    reader.readElement("String");
    if (reader.hasAttribute("hash")) {
        const std::string hash = reader.getAttribute("hash");
        _blob.reset();
        _cValue.clear();
        if (!hash.empty()) {
            // The manager owns the content and hands it over once the archive
            // entries have been drained, so this property never goes looking.
            auto &manager = blobManager();
            _pendingManager = &manager;
            manager.addPendingReferrer(hash, this);
        }
        return;
    }
    // The inline form -- which is also every document written before this
    // property had a type of its own.
    setValue(reader.getAttribute("value"));
}

Property *PropertyStringIncluded::Copy() const
{
    auto p = new PropertyStringIncluded();
    p->_cValue = _cValue;
    p->_ext = _ext;
    // The blob is the file this exact text is stored in, so the copy may
    // share it: an undo snapshot of a MaterialX document costs a reference,
    // and the copy does not have to hash the text again to be saved.
    p->_blob = _blob;
    return p;
}

void PropertyStringIncluded::Paste(const Property &from)
{
    const auto *same = dynamic_cast<const PropertyStringIncluded*>(&from);
    // setValue() drops the blob, which is right for text arriving from
    // anywhere else; a paste from the same type may keep it, the content
    // being identical by definition.
    PropertyString::Paste(from);
    if (same && same->_blob && same->_blob->owner() == &blobManager()) {
        _blob = same->_blob;
    }
}

unsigned int PropertyStringIncluded::getMemSize () const
{
    unsigned int mem = PropertyString::getMemSize();
    if (_blob) {
        mem += static_cast<unsigned int>(_blob->path().size());
    }
    return mem;
}

//**************************************************************************
// PropertyFileIncludedList
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

TYPESYSTEM_SOURCE(App::PropertyFileIncludedList , App::Property)

PropertyFileIncludedList::PropertyFileIncludedList() = default;

PropertyFileIncludedList::~PropertyFileIncludedList()
{
    cancelPending();
}

FileBlobManager &PropertyFileIncludedList::blobManager() const
{
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument()) {
            return doc->getFileBlobManager();
        }
    }
    return FileBlobManager::defaultManager();
}

void PropertyFileIncludedList::cancelPending()
{
    if (_pendingManager) {
        // Withdraws every entry this property queued: the queue is keyed on
        // the referrer, not on the hash, so one call clears them all.
        _pendingManager->removePendingReferrer(this);
        _pendingManager = nullptr;
    }
}

void PropertyFileIncludedList::awaitPending()
{
    // The manager owns the content and hands it over once the archive
    // entries have been drained, so this property never goes looking. A
    // hash the store already holds is served on the spot.
    for (const auto &hash : _files.pendingHashes()) {
        auto &manager = blobManager();
        _pendingManager = &manager;
        manager.addPendingReferrer(hash, this);
    }
}

void PropertyFileIncludedList::setFile(const char *name, const char *path, const char *original)
{
    aboutToSetValue();
    _files.setFile(blobManager(), name, path, original);
    hasSetValue();
}

void PropertyFileIncludedList::setBlob(const char *name, const FileBlobHandle &blob,
                                       const char *original)
{
    aboutToSetValue();
    _files.setBlob(blobManager(), name, blob, original);
    hasSetValue();
}

void PropertyFileIncludedList::removeFile(const char *name)
{
    if (!_files.find(name)) {
        return;
    }
    aboutToSetValue();
    _files.remove(name);
    hasSetValue();
}

void PropertyFileIncludedList::setValues(std::vector<Entry> files)
{
    aboutToSetValue();
    cancelPending();
    _files.assign(blobManager(), std::move(files));
    // An entry that names content the store does not hold yet -- copied
    // off a value whose restore was still pending -- keeps waiting for it
    // here rather than staying a hash with nothing behind it, which a save
    // would write without ever writing the content.
    awaitPending();
    hasSetValue();
}

void PropertyFileIncludedList::setValue(const FileSet &files)
{
    setValues(files.entries());
}

void PropertyFileIncludedList::clear()
{
    if (_files.empty()) {
        return;
    }
    aboutToSetValue();
    cancelPending();
    _files.clear();
    hasSetValue();
}

void PropertyFileIncludedList::assignRestoredBlob(const FileBlobHandle &blob)
{
    // No aboutToSetValue()/hasSetValue(): this completes the restore of a
    // value the document already had, and touching the document here would
    // mark it modified just by being opened.
    if (_files.assignRestoredBlob(blob)) {
        _pendingManager = nullptr;
    }
}

void PropertyFileIncludedList::collectBlobs(FileBlobManager &manager,
                                            const DocumentObject *object) const
{
    _files.collectBlobs(manager, FileBlobManager::referrerOf(this, object));
}

void PropertyFileIncludedList::Save(Base::Writer &writer) const
{
    // Below schema 5 there is no store to write to and no per-property
    // spelling to fall back on. blobContentNeedsStore() is what stops a
    // document holding these from being offered that schema; if one is
    // written anyway the names are kept, so what was lost can be said.
    _files.save(writer, "FileIncludedList", &blobManager(), FileBlobManager::referrerOf(this));
}

void PropertyFileIncludedList::Restore(Base::XMLReader &reader)
{
    cancelPending();
    _files.restore(reader, "FileIncludedList");
    awaitPending();
}

Property *PropertyFileIncludedList::Copy() const
{
    auto p = new PropertyFileIncludedList();
    // Sharing the blobs is the whole point: an undo snapshot of a material's
    // maps costs a reference each, not their bytes. A copy of a value still
    // waiting for its content copies the wait as a hash with no handle, and
    // setValues() takes the wait up again when it is pasted back.
    p->_files = _files;
    return p;
}

void PropertyFileIncludedList::Paste(const Property &from)
{
    const auto &other = dynamic_cast<const PropertyFileIncludedList&>(from);
    setValues(other._files.entries());
}

unsigned int PropertyFileIncludedList::getMemSize() const
{
    return Property::getMemSize() + _files.getMemSize();
}

bool PropertyFileIncludedList::isSame(const Property &other) const
{
    if (&other == this) {
        return true;
    }
    if (getTypeId() != other.getTypeId()) {
        return false;
    }
    return _files == static_cast<const PropertyFileIncludedList&>(other)._files;
}

PyObject *PropertyFileIncludedList::getPyObject()
{
    // A dict of name -> path, which is what a consumer asks this property
    // for. An entry whose content has not arrived maps to an empty string
    // rather than being left out: the document says it is there.
    Py::Dict dict;
    for (const auto &file : _files.entries()) {
        dict.setItem(file.name, Py::String(file.blob ? file.blob->path() : std::string()));
    }
    return Py::new_reference_to(dict);
}

void PropertyFileIncludedList::setPyObject(PyObject *value)
{
    if (!PyDict_Check(value)) {
        std::string error = "type must be a dict of name to file path, not ";
        error += value->ob_type->tp_name;
        THROWM(Base::TypeError, error)
    }

    std::vector<Entry> files;
    PyObject *key = nullptr;
    PyObject *item = nullptr;
    Py_ssize_t pos = 0;
    // PyDict_Next borrows both, and yields insertion order, which is the
    // order the entries are then stored and written in.
    while (PyDict_Next(value, &pos, &key, &item)) {
        if (!PyUnicode_Check(key)) {
            std::string error = "the name of an included file must be a string, not ";
            error += key->ob_type->tp_name;
            THROWM(Base::TypeError, error)
        }
        if (!PyUnicode_Check(item)) {
            std::string error = "the path of an included file must be a string, not ";
            error += item->ob_type->tp_name;
            THROWM(Base::TypeError, error)
        }
        Entry file;
        file.name = PyUnicode_AsUTF8(key);
        if (file.name.empty()) {
            THROWM(Base::ValueError, "An included file needs a name")
        }
        const std::string path = PyUnicode_AsUTF8(item);
        file.original = path;
        if (!path.empty()) {
            file.blob = blobManager().insertFile(
                    path.c_str(), Base::FileInfo(file.name).extension().c_str());
            file.hash = file.blob ? file.blob->hash() : std::string();
        }
        files.push_back(std::move(file));
    }
    setValues(std::move(files));
}
