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
#include <set>
#include <string>
#include <unordered_map>
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
 * Keyed on the TShape itself, which is what a shape table's identity really
 * is: a shape and the same shape moved are one entry, because the table stores
 * geometry at the identity and spells the location separately.
 *
 * *** The key is the raw TShape address and the table holds no handle, so a
 * table left behind by a finished save pins no geometry. That is safe only
 * because it is cleared at the start of the next save, before any lookup:
 * within one save every shape it names is held by the property it belongs to.
 */
class PartExport ShapeOwnerTable
{
public:
    /// A file the save has already written, as later files need to know it.
    struct File
    {
        /// Content hash: what a reference names, and what finds the blob.
        std::string hash;
        /// What this file itself borrows, i.e. ShapeRefSet::plan().
        std::string plan;
        /// Index of its root shape, 0 when it holds none of its own.
        int root {0};
        /// Orientation its root is stored with, as TopAbs_Orientation.
        int orientation {0};
    };

    /// Intern a written file for claim(). Two properties sharing one blob are
    /// one entry, which is what the hash keys it on.
    int addFile(File file);
    const File& file(int index) const;

    /** The file whose *root* this shape is, or 0.
     *
     * A shape that is another file's whole root is not worth a reference: the
     * two files would hold the same bytes, so the referring property can take
     * that file outright and the store keeps one copy instead of two.
     * Borrowing is for the sub-shapes below a root, where there is real
     * geometry to leave out.
     */
    int rootOwner(const TopoDS_Shape& shape) const;

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
    std::unordered_map<const TopoDS_TShape*, ShapeRef> _refs;
    std::vector<File> _files;
    std::unordered_map<std::string, int> _fileIndex;
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
     * *** Two things a sub-shape may not be borrowed across, both of them the
     * same rule: a shape's geometry can be keyed on the identity of an object
     * in the file's own tables, and identity does not survive being parsed
     * twice.
     *
     *  - **A face's edges** hold their 2D curve against the `Geom_Surface`
     *    the face carries, and an edge's vertices hold their parameter
     *    against the edge's curve. Borrowed into a locally stored face, the
     *    edge's pcurve names another file's surface object, and
     *    `BRep_Tool::CurveOnSurface` then finds nothing -- a null 2D curve,
     *    which is what anything projecting the shape dereferences. So nothing
     *    below a locally stored face or edge is ever borrowed.
     * A face is therefore either borrowed whole or stored whole, and so is an
     * edge, which is what makes every association land inside one parse.
     *
     * What this does not prevent is a borrowed face and a stored face in one
     * shell no longer sharing the edge between them: each keeps its own copy,
     * consistent in itself. That costs the sharing, not the geometry, and
     * ruling it out cost a re-walk per file and made a save unusably slow.
     */
    void build(const TopoDS_Shape& root);

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
    /// This file's root shape index, 0 when it borrowed its root or has none.
    int rootIndex() const
    {
        return _shapes.Extent();
    }
    //@}

    /** What this file borrows, as text, the same either way it was built.
     *
     * A file's bytes are a function of its shape *and* of what the save
     * decided to borrow, so a shape that has not changed is not on its own a
     * reason to keep the file that was written for it. Comparing this against
     * the plan the held file was written with is what says whether a save
     * still has nothing to do -- and reading it back off a parse is what lets
     * a reopened document answer that without re-serializing everything.
     *
     * Order-independent: the write side walks depth first and the read side
     * meets the tokens in record order, so the same file has to come out the
     * same from both.
     */
    std::string plan() const;

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
    /// One step of build(): store \a shape, borrowing where that is allowed.
    /// \a bound says a locally stored face or edge encloses it.
    int add(const TopoDS_Shape& shape, bool bound);
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

    /// Read side: the resolver, the tables the `Files` block named and their
    /// identities, and every (slot, index) a token actually asked for -- which
    /// is this file's plan as the file itself states it.
    Resolver _resolve;
    std::vector<const ShapeIndexMap*> _sources;
    std::vector<std::string> _sourceNames;
    mutable std::set<std::pair<int, int>> _borrowedRead;
};

}  // namespace Part

#endif  // PART_SHAPEREFSET_H
