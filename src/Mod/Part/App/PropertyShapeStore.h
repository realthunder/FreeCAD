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

#ifndef PART_PROPERTYSHAPESTORE_H
#define PART_PROPERTYSHAPESTORE_H

#include <cstdint>
#include <memory>

#include <App/PropertyFile.h>

class TopoDS_Shape;

namespace App
{
class Document;
}
namespace Base
{
class Writer;
}

namespace Part
{

/** The document's shared shape store (docs/SharedShapeStorage.md).
 *
 * One file holding every shape in the document, written through a single
 * `BinTools_ShapeWriter` -- which writes each `TopoDS_TShape` once and emits a
 * reference to its byte position for every later use. A shape property then
 * stores nothing but that position:
 *
 *     <Part store="ShapeStore" pos="4522" ElementMap="..."/>
 *
 * That is what restores the sharing a save destroys today. An array or
 * compound feature's leaves *are* its children's shapes; with one member per
 * property each side is written, parsed and tessellated separately, and after
 * a reload they are unrelated copies. Through one store they are one TShape
 * again, because the reader resolves a reference to the shape it already
 * built.
 *
 * The file is carried by `PropertyFileIncluded`, which puts it in the archive
 * as an ordinary blob and, on restore, leaves it as a real file in the
 * document's transient directory. That is what keeps selective restore: a
 * property seeks to its own position and parses only what its shape's
 * reference graph needs, whenever it is first asked for -- no whole-store
 * inflate and no archive index.
 *
 * ***Read-only as of step 12.3 of docs/SharedShapeStorage.md.*** No save
 * writes a store any more: the geometry is one file per object in the blob
 * store, which gets the same sharing from content addressing -- with the
 * location canonicalized out (sec 11.4), two objects over one TShape
 * serialize to the same bytes, so they are one file and, through the parse
 * cache, one TShape again. What is left here is what opens the documents that
 * were written with a store, and what lets the next save move them out of it.
 *
 * @warning The reader instance is the identity domain. One reader across the
 * document restores `IsPartner` true and the same TShape pointer; a reader per
 * property restores false -- which is today's loss exactly. Hence one reader
 * per store, living as long as the store does.
 */
class PartExport PropertyShapeStore: public App::PropertyFileIncluded
{
    TYPESYSTEM_HEADER_WITH_OVERRIDE();

public:
    PropertyShapeStore();
    ~PropertyShapeStore() override;

    /// What a property carries when its shape is not in a store.
    static constexpr uint64_t NoPosition = ~static_cast<uint64_t>(0);

    /// Name the store is carried under, as a dynamic property of the document.
    static const char* propertyName();

    /// The document's store, or null.
    static PropertyShapeStore* find(const App::Document* doc);

    /** Always written out empty.
     *
     * No save produces a store any more; this property exists to read the
     * documents that have one. It stays on the document rather than being
     * taken off -- it owns the file that every shape still parked at a
     * position reads from, and a save is exactly when those shapes are being
     * restored one by one -- but it writes nothing, so the file it is being
     * saved into carries no store.
     */
    void Save(Base::Writer& writer) const override;

    /// Whether the store's file is here and can be read from.
    bool hasContent() const;

    /// The shape stored at \a pos, or a null shape.
    TopoDS_Shape readShape(uint64_t pos) const;

private:
    struct Reader;
    mutable std::unique_ptr<Reader> _reader;
};

}  // namespace Part

#endif  // PART_PROPERTYSHAPESTORE_H
