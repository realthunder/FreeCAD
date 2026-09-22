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

#include "PreCompiled.h"

#ifndef _PreComp_
# include <sstream>
#endif

#include <Base/Console.h>
#include <Base/Reader.h>
#include <Base/Writer.h>
#include <CXX/Objects.hxx>

#include "PropertyHistory.h"
#include "Document.h"
#include "DocumentObject.h"
#include "PropertyContainer.h"

FC_LOG_LEVEL_INIT("App", true, true)

using namespace App;

TYPESYSTEM_SOURCE(App::PropertyHistory, App::Property)

PropertyHistory::PropertyHistory() = default;

PropertyHistory::~PropertyHistory()
{
    // Queued for content it will now never take: an aborted restore.
    if (!_entries.empty() && !_db && getContainer()) {
        if (auto doc = getContainer()->getOwnerDocument())
            doc->getFileBlobManager().removePendingReferrer(this);
    }
}

FileBlobManager& PropertyHistory::blobManager() const
{
    if (auto container = getContainer()) {
        if (auto doc = container->getOwnerDocument())
            return doc->getFileBlobManager();
    }
    return FileBlobManager::defaultManager();
}

void PropertyHistory::setValue(const FileBlobHandle& db, const std::vector<FileBlobHandle>& blobs,
                               const std::vector<std::string>& exts)
{
    aboutToSetValue();
    _db = db;
    _blobs = blobs;
    _entries.clear();
    if (db)
        _entries.push_back({db->hash(), ".db"});
    for (size_t i = 0; i < blobs.size(); ++i) {
        if (blobs[i])
            _entries.push_back({blobs[i]->hash(), i < exts.size() ? exts[i] : std::string()});
    }
    hasSetValue();
}

void PropertyHistory::assignRestoredBlob(const FileBlobHandle& blob)
{
    // No aboutToSetValue()/hasSetValue(): this completes a restore. The
    // database is the first entry; anything else is a manifest blob.
    if (!blob)
        return;
    if (!_entries.empty() && _entries.front().hash == blob->hash() && _entries.front().ext == ".db")
        _db = blob;
    else
        _blobs.push_back(blob);
}

void PropertyHistory::collectBlobs(FileBlobManager& manager, const DocumentObject* object) const
{
    // The database under this property's name; the manifest blobs keep
    // the extension their manifest gave them, named by hash.
    BlobReferrer referrer = FileBlobManager::referrerOf(this, object);
    referrer.ext = ".db";
    manager.noteReferenced(_db, referrer);
    for (size_t i = 0; i < _blobs.size(); ++i) {
        BlobReferrer r;
        for (const auto& e : _entries) {
            if (e.hash == _blobs[i]->hash()) {
                r.ext = e.ext;
                break;
            }
        }
        manager.noteReferenced(_blobs[i], r);
    }
}

void PropertyHistory::Save(Base::Writer& writer) const
{
    if (writer.getSchemaVersion() < 5 || !_db) {
        // No store to put the files in: the history is not written, and
        // the file is a document without one (13.3, save without history).
        writer.Stream() << writer.ind() << "<History/>\n";
        return;
    }
    blobManager().noteReferenced(_db, [this]() {
        BlobReferrer r = FileBlobManager::referrerOf(this);
        r.ext = ".db";
        return r;
    }());
    writer.Stream() << writer.ind() << "<History db=\"" << _db->hash() << "\" count=\""
                    << _blobs.size() << "\">\n";
    for (const auto& b : _blobs) {
        std::string ext;
        for (const auto& e : _entries) {
            if (e.hash == b->hash()) {
                ext = e.ext;
                break;
            }
        }
        writer.Stream() << writer.ind() << "  <Blob hash=\"" << b->hash() << "\" ext=\""
                        << encodeAttribute(ext) << "\"/>\n";
    }
    writer.Stream() << writer.ind() << "</History>\n";
}

void PropertyHistory::Restore(Base::XMLReader& reader)
{
    reader.readElement("History");
    _db.reset();
    _blobs.clear();
    _entries.clear();
    if (!reader.hasAttribute("db"))
        return;
    const std::string db = reader.getAttribute("db");
    const int count = static_cast<int>(reader.getAttributeAsInteger("count", "0"));
    _entries.push_back({db, ".db"});
    for (int i = 0; i < count; ++i) {
        reader.readElement("Blob");
        _entries.push_back({reader.getAttribute("hash"), reader.getAttribute("ext", "")});
    }
    reader.readEndElement("History");
    // The manager hands each over, at once or when the entries are drained.
    auto& manager = blobManager();
    for (const auto& e : _entries)
        manager.addPendingReferrer(e.hash, this);
}

PyObject* PropertyHistory::getPyObject()
{
    if (!_db)
        return Py::new_reference_to(Py::None());
    Py::Dict dict;
    dict.setItem("db", Py::String(_db->path()));
    Py::List blobs;
    for (const auto& b : _blobs)
        blobs.append(Py::String(b->path()));
    dict.setItem("blobs", blobs);
    return Py::new_reference_to(dict);
}

void PropertyHistory::setPyObject(PyObject*)
{
    throw Base::TypeError("History is set by the document's transaction log, not from Python");
}

Property* PropertyHistory::Copy() const
{
    auto p = new PropertyHistory();
    p->_db = _db;
    p->_blobs = _blobs;
    p->_entries = _entries;
    return p;
}

void PropertyHistory::Paste(const Property& from)
{
    auto p = Base::freecad_dynamic_cast<const PropertyHistory>(&from);
    if (!p)
        throw Base::TypeError("Incompatible property to paste to");
    aboutToSetValue();
    _db = p->_db;
    _blobs = p->_blobs;
    _entries = p->_entries;
    hasSetValue();
}

unsigned int PropertyHistory::getMemSize() const
{
    return static_cast<unsigned int>(sizeof(*this) + _entries.size() * 60);
}

bool PropertyHistory::isSame(const Property& other) const
{
    auto p = Base::freecad_dynamic_cast<const PropertyHistory>(&other);
    if (!p)
        return false;
    if (_entries.size() != p->_entries.size())
        return false;
    for (size_t i = 0; i < _entries.size(); ++i) {
        if (_entries[i].hash != p->_entries[i].hash)
            return false;
    }
    return true;
}
