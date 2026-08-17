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

PropertyShapeStore* PropertyShapeStore::find(const App::Document* doc)
{
    if (!doc) {
        return nullptr;
    }
    return Base::freecad_dynamic_cast<PropertyShapeStore>(
        doc->getDynamicPropertyByName(propertyName()));
}

void PropertyShapeStore::Save(Base::Writer& writer) const
{
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
