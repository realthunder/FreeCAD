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

#ifndef APP_PROPERTY_HISTORY_H
#define APP_PROPERTY_HISTORY_H

#include <string>
#include <vector>

#include "FileBlobManager.h"
#include "Property.h"

namespace App
{

/** The embedded transaction log (docs/TransactionLog.md sec 16.4).
 *
 * A document-level dynamic property, present in `embedded` mode, holding
 * the log database as one blob and a handle on every blob the retained
 * versions' manifests name. All of them are noted to the document's blob
 * manager at collect time, so the archive carries exactly the historical
 * files the embedded history needs; on restore the manager hands the
 * same handles back, told apart by hash. Save writes hashes only, at
 * schema 5 and above; below, where there is no store, the history is not
 * written (blobContentNeedsStore), which is the "save a copy without
 * history" of 13.3 by another route.
 *
 * A FreeCAD that does not know the property round-trips it as plain
 * data, which is what lets a pinned link (16.5) find the history; the
 * save id (`Version` property, meta `save_id` in the database) is what
 * tells an edit made there apart.
 */
class AppExport PropertyHistory : public Property, public BlobReferrerProperty
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyHistory();
    ~PropertyHistory() override;

    /// One blob the history holds: its hash and the extension it is
    /// stored under (leading dot), which names the archive entry.
    struct Entry
    {
        std::string hash;
        std::string ext;
    };

    /// Set the database blob and the blobs the retained manifests name.
    void setValue(const FileBlobHandle& db, const std::vector<FileBlobHandle>& blobs,
                  const std::vector<std::string>& exts);
    const FileBlobHandle& getDatabase() const { return _db; }
    const std::vector<FileBlobHandle>& getBlobs() const { return _blobs; }
    /// The hashes this property was restored with, database first;
    /// what it holds may be fewer if the archive lacked some.
    const std::vector<Entry>& getEntries() const { return _entries; }
    bool isEmpty() const { return !_db; }

    /** @name Blob referrer */
    //@{
    void assignRestoredBlob(const FileBlobHandle& blob) override;
    void collectBlobs(FileBlobManager& manager, const DocumentObject* object) const override;
    bool blobContentNeedsStore() const override { return true; }
    std::string blobExtension() const override { return ".db"; }
    //@}

    PyObject* getPyObject() override;
    void setPyObject(PyObject* value) override;

    void Save(Base::Writer& writer) const override;
    void Restore(Base::XMLReader& reader) override;

    Property* Copy() const override;
    void Paste(const Property& from) override;
    unsigned int getMemSize() const override;
    bool isSame(const Property& other) const override;
    const char* getEditorName() const override { return ""; }

private:
    FileBlobManager& blobManager() const;

    FileBlobHandle _db;
    std::vector<FileBlobHandle> _blobs;
    /// Database first, then the blobs, in manifest order.
    std::vector<Entry> _entries;
};

} // namespace App

#endif // APP_PROPERTY_HISTORY_H
