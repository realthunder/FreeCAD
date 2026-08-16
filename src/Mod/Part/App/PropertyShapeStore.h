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
 * The store exists only at schema 6 and above. Below it, nothing here runs:
 * a property keeps its own archive member, and this property writes itself out
 * empty, so a file that has no use for a store does not carry one.
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

    /// Whether a save through this writer stores shapes centrally at all.
    static bool writesStore(Base::Writer& writer);

    /** The document's store for this save, created if this save uses one.
     *
     * Never removes: the property owns the file that every shape still parked
     * at a position reads from, and a save is exactly when those shapes are
     * being restored one by one. What keeps a store out of a schema-5 file is
     * Save() writing nothing, not the property going away.
     */
    static PropertyShapeStore* prepare(App::Document* doc, Base::Writer& writer);

    /// The document's store, or null.
    static PropertyShapeStore* find(const App::Document* doc);

    /** Collect the document's shapes into the store.
     *
     * Runs from the document's own pre-save pass, which is after every object
     * has settled its properties and before a single one has been written --
     * the only window in which a property can still be told where its shape
     * will be.
     */
    void beforeSave(Base::Writer& writer) const override;

    /** Written only into a file that has a use for it.
     *
     * Below schema 6 the property stays on the document -- taking it off
     * would delete the store file, and a shape still parked at a position in
     * it would have nowhere left to come from -- but it writes itself out
     * empty, so the file carries no store.
     */
    void Save(Base::Writer& writer) const override;

    /// Whether the store's file is here and can be read from.
    bool hasContent() const;

    /// The shape stored at \a pos, or a null shape.
    TopoDS_Shape readShape(uint64_t pos) const;

private:
    void collect(Base::Writer& writer) const;

    struct Reader;
    mutable std::unique_ptr<Reader> _reader;
};

}  // namespace Part

#endif  // PART_PROPERTYSHAPESTORE_H
