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

#include "PreCompiled.h"

#ifndef _PreComp_
#include <BinTools_ShapeReader.hxx>
#include <BinTools_ShapeWriter.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Shape.hxx>
#endif

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/FileInfo.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Base/Writer.h>

#include "PropertyShapeStore.h"
#include "PropertyTopoShape.h"

FC_LOG_LEVEL_INIT("ShapeStore", true, true)

using namespace Part;

TYPESYSTEM_SOURCE(Part::PropertyShapeStore, App::PropertyFileIncluded)

/** The store's read side: one stream and one reader, together.
 *
 * They belong together because the reader's position -> shape map is only
 * meaningful against the stream it was filled from, and because that map is
 * the thing that reconstructs sharing. Dropping either drops the identity.
 */
struct PropertyShapeStore::Reader
{
    explicit Reader(const Base::FileInfo& fi)
        : file(fi, std::ios::in | std::ios::binary)
    {}

    Base::ifstream file;
    BinTools_ShapeReader reader;
};

PropertyShapeStore::PropertyShapeStore() = default;

PropertyShapeStore::~PropertyShapeStore() = default;

const char* PropertyShapeStore::propertyName()
{
    return "ShapeStore";
}

bool PropertyShapeStore::writesStore(Base::Writer& writer)
{
    // Schema 6 and nothing below it. The gate is the whole compatibility
    // story: a schema-5 document written by this build comes out exactly as
    // it did before, because none of this runs.
    if (writer.getSchemaVersion() < 6) {
        return false;
    }
    // A writer that keeps a file per property and rewrites only what changed
    // -- the recovery writer -- would rewrite the whole store every autosave
    // cycle instead of the handful of touched shapes.
    if (!writer.supportsSharedStore()) {
        return false;
    }
    // A save asked for pure XML carries every shape inside the document and
    // has no member for the store to be, so there is no position to point at.
    const bool binary = writer.getMode("BinaryBrep");
    return !(writer.getFileVersion() > 1 && writer.isForceXML() >= (binary ? 3 : 2));
}

PropertyShapeStore* PropertyShapeStore::find(const App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }
    return Base::freecad_dynamic_cast<PropertyShapeStore>(
        doc->getDynamicPropertyByName(propertyName()));
}

PropertyShapeStore* PropertyShapeStore::prepare(App::Document* doc, Base::Writer& writer)
{
    if (!doc) {
        return nullptr;
    }
    auto store = find(doc);
    if (!writesStore(writer)) {
        // Deliberately NOT removed. The property holds the handle that keeps
        // the store file alive, and a save is precisely when shapes are still
        // being served out of it one object at a time -- drop it here and
        // every property the pre-save pass has not reached yet loses its
        // geometry, silently. Save() writes the property out empty instead,
        // so the file carries no store either way.
        return nullptr;
    }
    if (!store) {
        store = Base::freecad_dynamic_cast<PropertyShapeStore>(doc->addDynamicProperty(
            PropertyShapeStore::getClassTypeId().getName(),
            propertyName(),
            "Base",
            "Shared storage for this document's shapes",
            App::Prop_Hidden,
            true,
            true));
        if (!store) {
            FC_ERR("Failed to create the shape store of " << doc->getName());
        }
    }
    return store;
}

void PropertyShapeStore::beforeSave(Base::Writer& writer) const
{
    App::PropertyFileIncluded::beforeSave(writer);
    if (writesStore(writer)) {
        collect(writer);
    }
}

namespace
{
/** Every shape property of every object, in document order.
 *
 * Ordering is what step 2 of docs/SharedShapeStorage.md changes (children
 * before parents, grouped by sharing component); nothing here depends on it,
 * because a reference is resolved by position and not by sequence.
 */
std::vector<PropertyPartShape*> gatherShapes(const App::Document* doc)
{
    std::vector<PropertyPartShape*> shapes;
    for (auto obj : doc->getObjects()) {
        std::vector<App::Property*> props;
        obj->getPropertyList(props);
        for (auto prop : props) {
            auto shape = Base::freecad_dynamic_cast<PropertyPartShape>(prop);
            if (shape && shape->getContainer() == obj && shape->hasName()) {
                shapes.push_back(shape);
            }
        }
    }
    return shapes;
}
}  // namespace

void PropertyShapeStore::Save(Base::Writer& writer) const
{
    if (writesStore(writer)) {
        App::PropertyFileIncluded::Save(writer);
        return;
    }
    // Written as an empty file property, which is what a store with no
    // content would write anyway -- through the base class rather than by
    // hand, so the form stays whatever that schema and writer call empty.
    // The handle is held across the call: releasing the last one deletes the
    // store file, and shapes may still be reading from it.
    App::FileBlobHandle held = _blob;
    _blob.reset();
    App::PropertyFileIncluded::Save(writer);
    _blob = std::move(held);
    // The blob collect pass runs before any property is written and notes
    // every blob it can see, this one included. Nothing in the file refers to
    // it now, so take the note back -- otherwise the archive carries the whole
    // store as an entry that the document has no way to reach.
    blobManager().dropReferenced(_blob);
}

void PropertyShapeStore::collect(Base::Writer& writer) const
{
    (void)writer;
    auto container = getContainer();
    auto doc = Base::freecad_dynamic_cast<App::Document>(container);
    if (!doc) {
        FC_ERR("Shape store is not owned by a document");
        return;
    }

    std::vector<PropertyPartShape*> shapes = gatherShapes(doc);
    if (shapes.empty()) {
        return;
    }

    // Spooled into the document's transient directory rather than a buffer:
    // the store is the whole document's geometry, and setValue() below adopts
    // a writable file sitting there instead of copying it.
    const std::string spoolPath =
        Base::FileInfo::getTempFileName("ShapeStore.bin", blobManager().transientPath().c_str());
    Base::FileInfo spool(spoolPath);
    std::size_t written = 0;
    {
        Base::ofstream out(spool, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) {
            FC_ERR("Cannot write the shape store of " << doc->getName() << " to " << spoolPath);
            return;
        }
        // ONE writer for the document. Its shape -> position map is what makes
        // the second use of a TShape cost a reference record instead of its
        // geometry, and it dedups by TShape ignoring location -- which is why
        // sharers sitting at different placements still collapse.
        BinTools_ShapeWriter shapeWriter;
        for (auto prop : shapes) {
            try {
                // Written TWICE, and the position recorded is the SECOND
                // one. The first write puts the geometry down (or, for a
                // shape already stored, a reference to it); the second is
                // therefore always a reference record, and a reference is the
                // only thing the reader resolves through its position -> shape
                // map. Read a shape record directly and OCCT re-parses it into
                // a NEW TShape however many times it has already built that
                // very shape -- which is the sharing this whole store exists
                // to keep. The second record costs a handful of bytes.
                const TopoDS_Shape& shape = prop->getValue();
                shapeWriter.Write(shape, out);
                const auto pos = static_cast<uint64_t>(out.tellp());
                shapeWriter.Write(shape, out);
                prop->_StorePos = pos;
                ++written;
            }
            catch (const Standard_Failure& e) {
                FC_ERR("Failed to store the shape of " << prop->getFullName() << ": "
                                                       << e.GetMessageString());
                // Left without a position, so it writes its own member below,
                // exactly as it would have before any of this existed.
                prop->_StorePos = NoPosition;
            }
        }
    }

    if (!written) {
        spool.deleteFile();
        return;
    }

    try {
        // Assigned rather than set: setValue() would announce a change, and a
        // save that marks the document modified by saving it is a bug the
        // user sees. The blob machinery does the rest -- one archive entry,
        // written after Document.xml, found again by hash on restore.
        _blob = blobManager().adoptFile(spoolPath.c_str());
        _BaseFileName = "ShapeStore.bin";
        // The old store's file is what the old reader is holding open, and it
        // may be deleted with the blob that named it.
        _reader.reset();
    }
    catch (const Base::Exception& e) {
        FC_ERR("Failed to take the shape store of " << doc->getName() << ": " << e.what());
        for (auto prop : shapes) {
            prop->_StorePos = NoPosition;
        }
        return;
    }

    FC_LOG("Shape store of " << doc->getName() << ": " << written << '/' << shapes.size()
                             << " shapes, " << Base::FileInfo(getValue()).size() << " bytes");
}

bool PropertyShapeStore::hasContent() const
{
    if (_reader) {
        return true;
    }
    const char* path = getValue();
    return path && path[0] && Base::FileInfo(path).exists();
}

TopoDS_Shape PropertyShapeStore::readShape(uint64_t pos) const
{
    TopoDS_Shape shape;
    if (pos == NoPosition) {
        return shape;
    }

    if (!_reader) {
        const char* path = getValue();
        if (!path || !path[0]) {
            FC_ERR("Shape store has no content");
            return shape;
        }
        Base::FileInfo fi(path);
        if (!fi.exists()) {
            FC_ERR("Shape store " << path << " is missing");
            return shape;
        }
        auto opened = std::make_unique<Reader>(fi);
        if (!opened->file) {
            FC_ERR("Shape store " << path << " cannot be read");
            return shape;
        }
        _reader = std::move(opened);
    }

    try {
        // clear() first: a previous read that ran to the end of the file left
        // eofbit set, and seekg on a failed stream is a no-op -- which would
        // silently serve the wrong shape rather than fail.
        _reader->file.clear();
        _reader->file.seekg(static_cast<std::streamoff>(pos));
        _reader->reader.Read(_reader->file, shape);
    }
    catch (const Standard_Failure& e) {
        FC_ERR("Failed to read the shape at " << pos << " of " << getFullName() << ": "
                                              << e.GetMessageString());
    }
    catch (const std::exception& e) {
        FC_ERR("Failed to read the shape at " << pos << " of " << getFullName() << ": "
                                              << e.what());
    }
    return shape;
}
