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

#ifndef PART_SHAPEREFSET_H
#define PART_SHAPEREFSET_H

#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

#include <BRepTools_ShapeSet.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>

#include <Mod/Part/PartGlobal.h>

namespace Part
{

/** An ordered set of shapes addressed by a 1-based index.
 *
 * Spelt out rather than through TopTools_IndexedMapOfShape, which OCCT 8.0
 * deprecates in favour of exactly this. Identity is TopoDS_Shape::IsSame --
 * TShape and location, orientation ignored -- so with the location stripped
 * off, as everything here stores it, one entry is one TShape.
 */
using ShapeIndexMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

/** Where a sub-shape lives when it is not in the file being written.
 *
 * \a file indexes ShapeOwnerTable::files() rather than naming the file
 * outright: an owner table holds one entry per distinct TShape in the
 * document, and on a real model that is tens of thousands of them
 * (`scanner.FCStd`, 71185), so the identity is interned once instead of
 * copied into every entry.
 */
struct ShapeRef
{
    int file {0};
    /// Forward index in that file's own shape table, 1-based.
    int index {0};
};

/** The sub-shapes a save has already stored, and which file each is in.
 *
 * Filled as the save walks the document: every file publishes what it holds,
 * and every later file borrows from it instead of serializing the same
 * geometry again (`docs/SharedShapeStorage.md` sec 11.5). Because the walk
 * order is deterministic and the table is built from scratch each save, the
 * assignment is a pure function of the model -- there is no ownership to carry
 * forward, and so nothing to drift, transfer or dangle.
 *
 * Keyed on the location-stripped shape, which is TShape identity: a shape and
 * the same shape moved are one entry, because a shape table stores geometry at
 * the identity and spells the location separately.
 */
class PartExport ShapeOwnerTable
{
public:
    /// Intern a file identity -- its blob's content hash -- for claim().
    int addFile(const std::string& file);
    const std::vector<std::string>& files() const
    {
        return _files;
    }

    /// Record that \a shape is held by \a file at \a index. Keeps the first
    /// claim: the earliest file in the walk owns what it holds.
    void claim(const TopoDS_Shape& shape, int file, int index);

    /// Where this shape is stored, or null if no file holds it yet.
    const ShapeRef* find(const TopoDS_Shape& shape) const;

    bool empty() const
    {
        return _refs.empty();
    }
    std::size_t size() const
    {
        return _refs.size();
    }
    void clear();

private:
    ShapeIndexMap _shapes;
    /// Parallel to _shapes, 0-based against its 1-based index.
    std::vector<ShapeRef> _refs;
    std::vector<std::string> _files;
};

/** An ASCII BRep shape table that may borrow sub-shapes from other files.
 *
 * The format is OCCT's, with one addition: a file may name other files at its
 * head and then, wherever a sub-shape is listed, say that the sub-shape is in
 * one of them instead of in this file.
 *
 *     CASCADE Topology V1, (c) Matra-Datavision
 *     Files 2                  <- new, absent when nothing is borrowed
 *     8f1c...                  <- content hash of the file borrowed from
 *     a30b...
 *     Locations 3
 *     ...
 *     TShapes 17
 *     ...
 *     +12 3                    <- as today: shape 12 of this file, location 3
 *     E1 +7 3                  <- new: shape 7 of file 1, location 3
 *
 * A file that borrows nothing has no `Files` block and no `E` token, so it is
 * byte-identical to what TopoShape::exportBrep() writes -- most of a project
 * stays plain BRep, and the extension is present only where sharing is.
 *
 * *** The index in an `E` token is a FORWARD index, while the ordinary token
 * keeps OCCT's reverse one. A reverse index is relative to a shape count, and
 * this file cannot state another file's; within one file it is self-contained,
 * because the reader has read `TShapes N` before it reads any token.
 *
 * This has to reimplement TopTools_ShapeSet's Add/Write/Read rather than
 * extend them -- they are not virtual and the shape map is private -- but
 * every geometry hook it needs (AddGeometry, WriteGeometry, ReadGeometry,
 * AddShapes, Check) is public and virtual, so no OCCT change is involved and
 * the geometry encoding stays exactly OCCT's.
 */
class PartExport ShapeRefSet: public BRepTools_ShapeSet
{
public:
    /// For writing. Triangulation is left out, as exportBrep() leaves it out.
    ShapeRefSet();
    /// For reading, which needs a builder to assemble what it reads.
    explicit ShapeRefSet(const BRep_Builder& builder);
    ~ShapeRefSet() override;

    void Clear() override;

    /** @name Writing */
    //@{
    /** Sub-shapes to borrow rather than serialize, or null to borrow nothing.
     *
     * Not owned, and read for the whole of add(): it is the save's one table,
     * which every file adds to as it goes.
     */
    void setOwners(const ShapeOwnerTable* owners);

    /** Store a shape and its sub-shapes, stopping wherever one is borrowed.
     *
     * Returns the shape's index in this file, or 0 when it is borrowed or
     * null -- so a root that is itself borrowed gives an empty table, which
     * is a file holding nothing but a reference.
     */
    int add(const TopoDS_Shape& shape);

    /// Write the tables and then the token naming \a root. add(root) first.
    void write(const TopoDS_Shape& root, std::ostream& out);

    /// Files borrowed from, as indices into the owner table's files(), in the
    /// order this file's `Files` block lists them.
    const std::vector<int>& borrowedFiles() const
    {
        return _borrowed;
    }
    /// Whether anything is borrowed, i.e. whether this is still plain BRep.
    bool borrows() const
    {
        return !_borrowed.empty();
    }
    /** Distinct sub-shapes this file borrows.
     *
     * One per sub-shape the walk stopped at, not per token written -- a
     * borrowed shape listed under two parents is one reference and two
     * tokens. This is the count sec 11.8 of docs/SharedShapeStorage.md
     * measured, so it is the one to compare against.
     */
    int references() const
    {
        return _borrowedShapes.Extent();
    }

    /// Record everything this file holds as owned by it, for later files.
    void publish(int file, ShapeOwnerTable& owners) const;
    //@}

    /** @name Reading */
    //@{
    /** How a borrowed file is obtained: its shape table, in its own forward
     * numbering. Returning null fails the read -- the geometry is genuinely
     * not there, and a silently short shape is worse than none.
     */
    using Resolver = std::function<const ShapeIndexMap*(const std::string&)>;
    void setResolver(Resolver resolver);

    /// Read the tables and the root token. Null shape when the file is not
    /// readable, which includes a borrowed file the resolver cannot supply.
    TopoDS_Shape read(std::istream& in);
    //@}

    /// This file's shapes in its own forward numbering, 1-based.
    const ShapeIndexMap& shapes() const
    {
        return _shapes;
    }

private:
    /// Note a borrowed sub-shape, assigning its file a slot on first use.
    void borrow(const TopoDS_Shape& shape, const ShapeRef& ref);
    /// The slot and forward index of a borrowed sub-shape, or null.
    const ShapeRef* borrowed(const TopoDS_Shape& shape) const;
    /// Write one sub-shape token, ordinary or borrowed.
    void writeToken(const TopoDS_Shape& shape, std::ostream& out) const;
    /// Read one sub-shape token. \a count is this file's declared shape count.
    bool readToken(TopoDS_Shape& shape, std::istream& in, int count) const;

    ShapeIndexMap _shapes;

    /// Write side: the owner table, and what this file took out of it.
    const ShapeOwnerTable* _owners {nullptr};
    ShapeIndexMap _borrowedShapes;
    /// Parallel to _borrowedShapes: the slot in _borrowed, and the forward
    /// index in that file.
    std::vector<ShapeRef> _borrowedRefs;
    /// Owner-table file indices, in `Files` block order. Slot n is [n-1].
    std::vector<int> _borrowed;

    /// Read side: the resolver, and the tables the `Files` block named.
    Resolver _resolve;
    std::vector<const ShapeIndexMap*> _sources;
};

}  // namespace Part

#endif  // PART_SHAPEREFSET_H
