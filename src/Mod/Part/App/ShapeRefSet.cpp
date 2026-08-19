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
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <iostream>
#include <locale>
#include <sstream>
#include <QCryptographicHash>
#include <BRep_Builder.hxx>
#include <BRep_PointRepresentation.hxx>
#include <BRep_TVertex.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <Geom2d_Curve.hxx>
#include <GeomTools_Curve2dSet.hxx>
#include <GeomTools_CurveSet.hxx>
#include <GeomTools_SurfaceSet.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_LocationSet.hxx>
#endif

#include <Base/Console.h>

#include "ShapeRefSet.h"

FC_LOG_LEVEL_INIT("ShapeRefSet", true, true);

using namespace Part;

namespace
{

/** The version banner, by format number.
 *
 * OCCT publishes the same table as TopTools_ShapeSet::THE_ASCII_VERSIONS, but
 * as a static member of a class whose export is per-method; taking a copy
 * costs four strings and owes nothing to that.
 */
const char* const asciiVersions[] = {"",
                                     "CASCADE Topology V1, (c) Matra-Datavision",
                                     "CASCADE Topology V2, (c) Matra-Datavision",
                                     "CASCADE Topology V3, (c) Open Cascade"};
constexpr int lowestVersion = TopTools_FormatVersion_VERSION_1;
constexpr int highestVersion = TopTools_FormatVersion_VERSION_3;

void printShapeEnum(TopAbs_ShapeEnum type, std::ostream& out)
{
    switch (type) {
        case TopAbs_VERTEX:
            out << "Ve";
            break;
        case TopAbs_EDGE:
            out << "Ed";
            break;
        case TopAbs_WIRE:
            out << "Wi";
            break;
        case TopAbs_FACE:
            out << "Fa";
            break;
        case TopAbs_SHELL:
            out << "Sh";
            break;
        case TopAbs_SOLID:
            out << "So";
            break;
        case TopAbs_COMPSOLID:
            out << "CS";
            break;
        case TopAbs_COMPOUND:
            out << "Co";
            break;
        case TopAbs_SHAPE:
            out << "Sp";
            break;
    }
}

TopAbs_ShapeEnum readShapeEnum(std::istream& in)
{
    char buffer[255] = {};
    in >> buffer;
    switch (buffer[0]) {
        case 'V':
            return TopAbs_VERTEX;
        case 'E':
            return TopAbs_EDGE;
        case 'W':
            return TopAbs_WIRE;
        case 'F':
            return TopAbs_FACE;
        case 'S':
            return buffer[1] == 'h' ? TopAbs_SHELL : TopAbs_SOLID;
        case 'C':
            return buffer[1] == 'S' ? TopAbs_COMPSOLID : TopAbs_COMPOUND;
        default:
            return TopAbs_COMPOUND;
    }
}

void printOrientation(TopAbs_Orientation orientation, std::ostream& out)
{
    switch (orientation) {
        case TopAbs_FORWARD:
            out << '+';
            break;
        case TopAbs_REVERSED:
            out << '-';
            break;
        case TopAbs_INTERNAL:
            out << 'i';
            break;
        case TopAbs_EXTERNAL:
            out << 'e';
            break;
    }
}

void applyOrientation(TopoDS_Shape& shape, char code)
{
    switch (code) {
        case '+':
            shape.Orientation(TopAbs_FORWARD);
            break;
        case '-':
            shape.Orientation(TopAbs_REVERSED);
            break;
        case 'i':
            shape.Orientation(TopAbs_INTERNAL);
            break;
        case 'e':
            shape.Orientation(TopAbs_EXTERNAL);
            break;
        default:
            break;
    }
}

/// The header OCCT writes a geometry table under, which is what identifies it.
const char* geomTableName(int table)
{
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            return "Curve2ds";
        case ShapeOwnerTable::CurveTable:
            return "Curves";
        default:
            return "Surfaces";
    }
}

/// How a geometry entry is told from a sub-shape, and from another table, in
/// the plan text -- which is compared, so the three must not collide.
char geomTableTag(int table)
{
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            return 'p';
        case ShapeOwnerTable::CurveTable:
            return 'c';
        default:
            return 's';
    }
}

/** What an entry is keyed on: a digest of the bytes it is written as.
 *
 * The same function the blob manager identifies a whole file by, and it is
 * trusted here for the same reason: two entries that hash alike are held to
 * be the same geometry, and the table holds no text to check that against.
 */
std::string geomDigest(const std::string& text)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(text.data()),
                                static_cast<qsizetype>(text.size())));
    const QByteArray result = hash.result();
    return std::string(result.constData(), static_cast<std::size_t>(result.size()));
}

/// Entry \a index of one of \a set's three geometry tables, or null when the
/// file being read named a position that file does not have.
Handle(Standard_Transient) geomEntry(const BRepTools_ShapeSet& set, int table, int index)
{
    if (index < 1) {
        return {};
    }
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            return index <= set.Curves2d().Extent() ? set.Curves2d().Curve2d(index) : nullptr;
        case ShapeOwnerTable::CurveTable:
            return index <= set.Curves().Extent() ? set.Curves().Curve(index) : nullptr;
        default:
            return index <= set.Surfaces().Extent() ? set.Surfaces().Surface(index) : nullptr;
    }
}

/// One entry read in OCCT's own encoding.
Handle(Standard_Transient) readGeomEntry(int table, std::istream& in)
{
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            return GeomTools_Curve2dSet::ReadCurve2d(in);
        case ShapeOwnerTable::CurveTable:
            return GeomTools_CurveSet::ReadCurve(in);
        default:
            return GeomTools_SurfaceSet::ReadSurface(in);
    }
}

/// Add an entry to a set's table, answering the position it landed at.
int addGeomEntry(BRepTools_ShapeSet& set, int table, const Handle(Standard_Transient)& value)
{
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            return set.ChangeCurves2d().Add(Handle(Geom2d_Curve)::DownCast(value));
        case ShapeOwnerTable::CurveTable:
            return set.ChangeCurves().Add(Handle(Geom_Curve)::DownCast(value));
        default:
            return set.ChangeSurfaces().Add(Handle(Geom_Surface)::DownCast(value));
    }
}

/// A distinct object holding the same geometry, for the one case a borrowed
/// entry would otherwise land on top of an entry this file already has.
Handle(Standard_Transient) copyGeomEntry(int table, const Handle(Standard_Transient)& value)
{
    if (table == ShapeOwnerTable::Curve2dTable) {
        Handle(Geom2d_Curve) curve = Handle(Geom2d_Curve)::DownCast(value);
        return curve.IsNull() ? nullptr : curve->Copy();
    }
    Handle(Geom_Geometry) geometry = Handle(Geom_Geometry)::DownCast(value);
    return geometry.IsNull() ? nullptr : geometry->Copy();
}

}  // namespace

// ---------------------------------------------------------------------------
// ShapeOwnerTable
// ---------------------------------------------------------------------------

int ShapeOwnerTable::addFile(File file)
{
    auto known = _fileIndex.find(file.hash);
    if (known != _fileIndex.end()) {
        return known->second;
    }
    const std::string hash = file.hash;
    _files.push_back(std::move(file));
    const int index = static_cast<int>(_files.size());
    _fileIndex[hash] = index;
    return index;
}

const ShapeOwnerTable::File& ShapeOwnerTable::file(int index) const
{
    static const File none;
    if (index < 1 || index > static_cast<int>(_files.size())) {
        return none;
    }
    return _files[index - 1];
}

int ShapeOwnerTable::rootOwner(const TopoDS_Shape& shape) const
{
    const ShapeRef* ref = find(shape);
    if (!ref) {
        return 0;
    }
    const File& holder = file(ref->file);
    // The orientation has to match too: a file's root token carries it, so a
    // shape that is the same root reversed is not the same bytes.
    if (holder.root != ref->index || holder.orientation != shape.Orientation()) {
        return 0;
    }
    return ref->file;
}

void ShapeOwnerTable::claim(const TopoDS_Shape& shape, int file, int index)
{
    if (shape.IsNull()) {
        return;
    }
    // An existing entry keeps the claim it already has: the earliest file in
    // the walk owns what it holds, which is what makes the assignment a
    // function of the walk order rather than of the order of these calls.
    _refs.emplace(shape.TShape().get(), ShapeRef {file, index});
}

const ShapeRef* ShapeOwnerTable::find(const TopoDS_Shape& shape) const
{
    if (shape.IsNull()) {
        return nullptr;
    }
    auto found = _refs.find(shape.TShape().get());
    return found == _refs.end() ? nullptr : &found->second;
}

void ShapeOwnerTable::claimGeometry(int table, const std::string& digest, int file, int index)
{
    if (table < 0 || table >= GeomTableCount || digest.empty()) {
        return;
    }
    // First claim again, and for the same reason: the earliest file in the
    // walk owns what it holds, so the assignment is a function of the walk.
    _geometry[table].emplace(digest, ShapeRef {file, index});
}

const ShapeRef* ShapeOwnerTable::findGeometry(int table, const std::string& digest) const
{
    if (table < 0 || table >= GeomTableCount) {
        return nullptr;
    }
    auto found = _geometry[table].find(digest);
    return found == _geometry[table].end() ? nullptr : &found->second;
}

void ShapeOwnerTable::claimGeometryHandle(int table,
                                          const void* handle,
                                          int file,
                                          const ShapeRef& resolved)
{
    if (table < 0 || table >= GeomTableCount || !handle || !file) {
        return;
    }
    // First claim, as everywhere else here -- but the key carries the file,
    // so this only ever settles one file writing one object twice, which the
    // tables do not do anyway.
    _geometryHandles[table].emplace(GeomHandleKey {handle, file}, resolved);
    _geometryFirst[table].emplace(handle, resolved);
}

const ShapeRef* ShapeOwnerTable::findGeometryHandle(int table, const void* handle, int file) const
{
    if (table < 0 || table >= GeomTableCount || !handle || !file) {
        return nullptr;
    }
    auto found = _geometryHandles[table].find(GeomHandleKey {handle, file});
    return found == _geometryHandles[table].end() ? nullptr : &found->second;
}

const ShapeRef* ShapeOwnerTable::findGeometryHandle(int table, const void* handle) const
{
    if (table < 0 || table >= GeomTableCount || !handle) {
        return nullptr;
    }
    auto found = _geometryFirst[table].find(handle);
    return found == _geometryFirst[table].end() ? nullptr : &found->second;
}

const ShapeOwnerTable::ContentPlan* ShapeOwnerTable::contentPlan(const std::string& key) const
{
    if (key.empty()) {
        return nullptr;
    }
    auto found = _contents.find(key);
    return found == _contents.end() ? nullptr : &found->second;
}

void ShapeOwnerTable::claimContent(const std::string& key, ContentPlan plan)
{
    if (!key.empty()) {
        // First claim again: the earliest file decides, and every later file
        // with this geometry follows it.
        _contents.emplace(key, std::move(plan));
    }
}

void ShapeOwnerTable::clear()
{
    _refs.clear();
    _files.clear();
    _fileIndex.clear();
    for (auto& table : _geometry) {
        table.clear();
    }
    for (auto& table : _geometryHandles) {
        table.clear();
    }
    for (auto& table : _geometryFirst) {
        table.clear();
    }
    _contents.clear();
}

// ---------------------------------------------------------------------------
// ShapeRefSet
// ---------------------------------------------------------------------------

ShapeRefSet::ShapeRefSet()
    : BRepTools_ShapeSet(false)
{
    // The version TopoShape::exportBrep() writes, so a file that borrows
    // nothing comes out byte for byte as it does today.
    SetFormatNb(TopTools_FormatVersion_VERSION_1);
    // And what it sets: merging equal 2D curves is on in the kernel by
    // default, and every path that wants it goes through
    // TopoShape::applyStorageOptions, which is where the document says so.
    ChangeCurves2d().SetMerging(false);
}

ShapeRefSet::ShapeRefSet(const BRep_Builder& builder)
    : BRepTools_ShapeSet(builder, false)
{
    SetFormatNb(TopTools_FormatVersion_VERSION_1);
    // *** A read never merges. The tables are positional and a file's records
    // name positions in them, so an entry that came back equal to one already
    // read must still be its own entry -- merging it would shift every
    // position after it. OCCT's own reader adds to the map directly and so
    // never had to say this; this one adds through Add(), which does merge.
    ChangeCurves2d().SetMerging(false);
}

ShapeRefSet::~ShapeRefSet() = default;

void ShapeRefSet::Clear()
{
    BRepTools_ShapeSet::Clear();
    _shapes.Clear();
    _borrowedShapes.Clear();
    _borrowedRefs.clear();
    _borrowed.clear();
    _sources.clear();
    _sourceNames.clear();
    _borrowedRead.clear();
    for (auto& table : _geometry) {
        table.clear();
    }
    for (auto& table : _prefer) {
        table.clear();
    }
    _identityBroken = false;
    _geometryRead.clear();
}

void ShapeRefSet::setOwners(const ShapeOwnerTable* owners)
{
    _owners = owners;
}

void ShapeRefSet::setGeometrySharing(bool enable)
{
    _shareGeometry = enable;
}

void ShapeRefSet::setSubFaceBorrowing(int level)
{
    _belowFace = level;
}

namespace
{
/// Which bit of the setting says that what is directly inside this shape may
/// be borrowed. They are separable: what a borrowed face costs is not what a
/// borrowed edge costs, and neither is what it is worth.
int borrowBit(TopAbs_ShapeEnum type)
{
    switch (type) {
        case TopAbs_SHELL:
            return 1;
        case TopAbs_FACE:
        case TopAbs_WIRE:
            return 2;
        case TopAbs_EDGE:
            return 4;
        default:
            return 0;
    }
}
}  // namespace

void ShapeRefSet::setResolver(Resolver resolver)
{
    _resolve = std::move(resolver);
}

int ShapeRefSet::slotFor(int file)
{
    // First use of this file in this one: give it a slot in the `Files`
    // block. The block is per file, so the numbers stay small and stable
    // even though the owner table's are neither.
    for (std::size_t i = 0; i < _borrowed.size(); ++i) {
        if (_borrowed[i] == file) {
            return static_cast<int>(i) + 1;
        }
    }
    _borrowed.push_back(file);
    return static_cast<int>(_borrowed.size());
}

void ShapeRefSet::borrow(const TopoDS_Shape& shape, const ShapeRef& ref)
{
    const int slot = _borrowedShapes.Add(shape);
    if (slot > static_cast<int>(_borrowedRefs.size())) {
        _borrowedRefs.resize(slot);
        _borrowedRefs[slot - 1] = ShapeRef {slotFor(ref.file), ref.index};
    }
}

const ShapeRef* ShapeRefSet::borrowed(const TopoDS_Shape& shape) const
{
    const int slot = _borrowedShapes.FindIndex(shape);
    if (slot < 1 || slot > static_cast<int>(_borrowedRefs.size())) {
        return nullptr;
    }
    return &_borrowedRefs[slot - 1];
}

void ShapeRefSet::build(const TopoDS_Shape& root)
{
    add(root, nullptr, TopLoc_Location());
    // After the walk, because the walk is what fills the tables -- and before
    // anything asks for plan(), because what this settles is half of it.
    planGeometry();
    if (!_belowFace || !_identityBroken) {
        return;
    }
    // *** Something under a face was borrowed and the object its records are
    // keyed on could not be named. Rather than write a file whose faces have
    // edges with no 2D curve on them, this file is written again the way it
    // would have been without borrowing below a face at all -- which is what
    // ships, and is always available because nothing has been written yet.
    //
    // Whole shapes are still borrowed, and the geometry tables still name
    // what they can: what is dropped is exactly what could not be keyed.
    const int level = _belowFace;
    Clear();
    _belowFace = 0;
    add(root, nullptr, TopLoc_Location());
    // Back on for the passes below and for publish(): what the fallback drops
    // is the borrowing, not the sharing of geometry by object.
    _belowFace = level;
    planGeometry();
}

const void* ShapeRefSet::entryHandle(int table, int index) const
{
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            return Curves2d().Curve2d(index).get();
        case ShapeOwnerTable::CurveTable:
            return Curves().Curve(index).get();
        default:
            return Surfaces().Surface(index).get();
    }
}

std::string ShapeRefSet::printGeometry(int table, int index) const
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    // The precision each table's own writer sets, so what is hashed here is
    // the text that would be written -- and what is written instead of a
    // reference is this same text, printed once.
    out.precision(17);
    switch (table) {
        case ShapeOwnerTable::Curve2dTable:
            GeomTools_Curve2dSet::PrintCurve2d(Curves2d().Curve2d(index), out, true);
            break;
        case ShapeOwnerTable::CurveTable:
            GeomTools_CurveSet::PrintCurve(Curves().Curve(index), out, true);
            break;
        default:
            GeomTools_SurfaceSet::PrintSurface(Surfaces().Surface(index), out, true);
            break;
    }
    return out.str();
}

void ShapeRefSet::planGeometry()
{
    if (!_shareGeometry || !_owners) {
        return;
    }
    // First what every entry is, which is what both decisions below are made
    // from: printing an entry is the expensive half and it is done once.
    bool any = false;
    for (int table = 0; table < ShapeOwnerTable::GeomTableCount; ++table) {
        int count = 0;
        switch (table) {
            case ShapeOwnerTable::Curve2dTable:
                count = Curves2d().Extent();
                break;
            case ShapeOwnerTable::CurveTable:
                count = Curves().Extent();
                break;
            default:
                count = Surfaces().Extent();
                break;
        }
        auto& plan = _geometry[table];
        plan.resize(static_cast<std::size_t>(count));
        for (int i = 1; i <= count; ++i) {
            GeomEntry& entry = plan[static_cast<std::size_t>(i) - 1];
            entry.text = printGeometry(table, i);
            if (entry.text.empty()) {
                // A table holding something this build cannot write is left
                // where it is rather than named, since nothing can check it.
                continue;
            }
            entry.digest = geomDigest(entry.text);
            any = true;
        }
    }

    // *** An earlier file holding this same geometry has already decided what
    // to name and what to write out, and making the same decisions is what
    // leaves the two files identical -- so content addressing keeps one of
    // them, which is worth more than any entry the second could have named.
    // *** What a preference says is correctness, and a replayed plan is a
    // saving, so a file with preferences decides for itself. It costs that
    // file nothing: it borrows a sub-shape, so its records name another
    // file's shapes and no other file's bytes were going to match it anyway
    // -- which is also why publish() offers no content plan for it.
    bool pinned = false;
    for (const auto& table : _prefer) {
        pinned = pinned || !table.empty();
    }
    if (any && !pinned) {
        if (const ShapeOwnerTable::ContentPlan* plan = _owners->contentPlan(contentKey())) {
            bool fits = true;
            for (int table = 0; fits && table < ShapeOwnerTable::GeomTableCount; ++table) {
                fits = (*plan)[table].size() == _geometry[table].size();
            }
            if (fits) {
                for (int table = 0; table < ShapeOwnerTable::GeomTableCount; ++table) {
                    for (std::size_t i = 0; i < _geometry[table].size(); ++i) {
                        const ShapeRef& decided = (*plan)[table][i];
                        if (decided.file) {
                            _geometry[table][i].ref =
                                ShapeRef {slotFor(decided.file), decided.index};
                        }
                    }
                }
                return;
            }
            // Sizes that disagree mean two different shapes hashed alike,
            // which costs the sharing and nothing else: decide for ourselves.
            FC_WARN("Two shapes with different geometry tables share a content key");
        }
    }

    for (int table = 0; table < ShapeOwnerTable::GeomTableCount; ++table) {
        auto& plan = _geometry[table];
        // *** One entry of another file may be named once only. Two entries
        // of this file that are written alike are two objects, and they have
        // to stay two: a face keys its edges' 2D curves on the surface object
        // it carries, so a surface reaching two faces at once makes exactly
        // the lookup those keys exist to settle ambiguous. It is also what
        // keeps the table positional -- the reader adds what it is given, and
        // a repeated object would land on the position it already has and
        // shift every entry after it.
        std::set<std::pair<int, int>> taken;
        // The entries something borrowed is keyed on come first and are not
        // a choice: this file's object has to be the object that file holds,
        // and any other file writing the same bytes is a different object.
        for (std::size_t i = 0; i < plan.size() && _belowFace; ++i) {
            GeomEntry& entry = plan[i];
            if (entry.digest.empty()) {
                continue;
            }
            const void* handle = entryHandle(table, static_cast<int>(i) + 1);
            // The file whose shapes this object is keyed on if any of them
            // were borrowed under a shape carrying it, and otherwise
            // wherever the object first went -- both of which are the same
            // object, where the ordinary sharing put every copy on one.
            auto wanted = _prefer[table].find(handle);
            const ShapeRef* ref = wanted == _prefer[table].end()
                ? _owners->findGeometryHandle(table, handle)
                : _owners->findGeometryHandle(table, handle, wanted->second);
            if (ref && taken.emplace(ref->file, ref->index).second) {
                entry.ref = ShapeRef {slotFor(ref->file), ref->index};
                continue;
            }
            // *** Identity could not be restored for this object, and there
            // is no half of it: either it is the same object as the one the
            // borrowed shapes are keyed on or it is not.
            //
            // It happens where this file holds two objects that another file
            // holds as one: two entries cannot name one, because two written
            // alike have to stay two. build() then writes this file the way
            // it would have without borrowing below a face.
            //
            // No claim on the object at all is not that case and is not a
            // failure: an object no other file holds is one no borrowed
            // shape can be keyed on, so there is nothing to restore.
            if (ref || wanted != _prefer[table].end()) {
                _identityBroken = true;
            }
        }
        for (std::size_t i = 0; i < plan.size(); ++i) {
            GeomEntry& entry = plan[i];
            if (entry.digest.empty() || entry.ref.file) {
                continue;
            }
            const ShapeRef* ref = _owners->findGeometry(table, entry.digest);
            if (ref && taken.emplace(ref->file, ref->index).second) {
                entry.ref = ShapeRef {slotFor(ref->file), ref->index};
            }
        }
    }
}

std::string ShapeRefSet::contentKey() const
{
    std::string all;
    for (const auto& plan : _geometry) {
        for (const GeomEntry& entry : plan) {
            all += entry.digest;
            all += '.';
        }
        all += ';';
    }
    if (all.size() == ShapeOwnerTable::GeomTableCount) {
        // Nothing but the separators: a file with no geometry of its own says
        // nothing about its content, and every such file would say the same.
        return {};
    }
    // The shape count with it, so that two shapes standing on one set of
    // surfaces are not taken for one content on the strength of the surfaces.
    all += std::to_string(_shapes.Extent());
    return geomDigest(all);
}

bool ShapeRefSet::sharesGeometry() const
{
    for (const auto& plan : _geometry) {
        for (const GeomEntry& entry : plan) {
            if (entry.ref.file) {
                return true;
            }
        }
    }
    return false;
}

int ShapeRefSet::geometryReferences() const
{
    int count = 0;
    for (const auto& plan : _geometry) {
        for (const GeomEntry& entry : plan) {
            if (entry.ref.file) {
                ++count;
            }
        }
    }
    // A set is either written or read, so the two never both count.
    return count + static_cast<int>(_geometryRead.size());
}

int ShapeRefSet::add(const TopoDS_Shape& shape, Domain* domain, const TopLoc_Location& relative)
{
    if (shape.IsNull()) {
        return 0;
    }

    // The location goes into this file's set whatever happens to the shape:
    // a borrowed sub-shape is stored at the identity in the file that holds
    // it, and the token here is what says where this file puts it.
    ChangeLocations().Add(shape.Location());

    const TopoDS_Shape base = shape.Located(TopLoc_Location());
    const int index = _shapes.FindIndex(base);
    if (index) {
        return index;
    }
    if (const ShapeRef* held = borrowed(base)) {
        if (domain) {
            domain->note(_borrowed[held->file - 1]);
        }
        return 0;
    }
    // *** Inside a face or an edge, only where this shape sits exactly where
    // the file it came from has it. What keys a 2D curve to a face is the
    // surface *and* the location of the edge relative to it, and a location
    // is compared by the TopLoc_Datum3D objects it is built from -- two files
    // parsed separately never share those. The identity is the one location
    // every file does agree on, so it is the one this can cross.
    //
    // Every failure below a face on a real model was this, and not one of
    // them was a missing 2D curve: the curve was there, against a location
    // object this file did not have.
    const bool placed = domain && domain->keyed && !relative.IsIdentity();
    if (_owners && !placed && (!domain || domain->open())) {
        if (const ShapeRef* ref = _owners->find(base)) {
            if (domain) {
                domain->note(ref->file);
            }
            borrow(base, *ref);
            return 0;
        }
    }

    AddGeometry(base);
    // A face, an edge, a wire and a shell each bind what is under them,
    // because the association between a shape and its geometry is keyed on
    // the identity of an object in this file's tables -- a face keys its
    // edges' 2D curves on the surface it carries, an edge keys its vertices'
    // parameters on its curve -- and because a wire and a shell are sewn.
    // Without cross-file geometry there is no way to say "that object", so
    // nothing under them is borrowed at all; with it, one file's objects can
    // be named, so what is under them is borrowed from one file (sec 12.15).
    const TopAbs_ShapeEnum type = base.ShapeType();
    const bool binding = borrowBit(type) != 0;
    // One of its own for each of them, so that what a face learns is what its
    // own edges did and not what a neighbour's did; it is handed up
    // afterwards, because a wire carries no geometry and the face around it
    // is what the edges under it are keyed on.
    Domain own;
    Domain* below = domain;
    if (binding) {
        own.file =
            (!(_belowFace & borrowBit(type)) || (domain && !domain->open())) ? -1 : 0;
        own.keyed = type == TopAbs_FACE || type == TopAbs_EDGE
            || (type == TopAbs_WIRE && domain && domain->keyed);
        below = &own;
    }
    for (TopoDS_Iterator it(base, false, false); it.More(); it.Next()) {
        // Where the child sits relative to the shape its geometry is keyed
        // on: its own location under a face or an edge, and one step further
        // along under a wire, which carries no geometry of its own.
        add(it.Value(),
            below,
            type == TopAbs_WIRE ? relative * it.Value().Location() : it.Value().Location());
    }
    // Something under this shape came out of another file, so what this one
    // is keyed on has to be that file's object and not merely the same bytes.
    if (binding && own.file > 0) {
        preferGeometry(base);
        if (domain) {
            domain->note(own.file);
        }
    }
    return _shapes.Add(base);
}

void ShapeRefSet::preferGeometry(const TopoDS_Shape& shape)
{
    if (!_shareGeometry) {
        return;
    }
    // *** Only the associations that are really there. A face's edges hold
    // their 2D curve against the surface the face carries, and a vertex holds
    // its parameter against the curve of the edge it sits on -- but neither
    // is always written down: a 2D curve on a plane is recomputed on reading
    // rather than stored, and a vertex often carries no parameter at all. So
    // this asks, rather than assuming, and what it finds is a requirement:
    // build() gives up the borrowing rather than write a file where the
    // object cannot be named.
    if (shape.ShapeType() == TopAbs_FACE) {
        const TopoDS_Face& face = TopoDS::Face(shape);
        TopLoc_Location loc;
        // The stored handle, not a located copy: identity is the point.
        const void* surface = BRep_Tool::Surface(face, loc).get();
        if (!surface) {
            return;
        }
        for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
            const ShapeRef* ref = borrowed(it.Current().Located(TopLoc_Location()));
            if (!ref) {
                continue;
            }
            double first = 0.0;
            double last = 0.0;
            bool stored = false;
            BRep_Tool::CurveOnSurface(TopoDS::Edge(it.Current()), face, first, last, &stored);
            if (!stored) {
                continue;
            }
            // First note wins, as every claim here does: two faces sharing
            // one surface cannot each have it from a different file.
            _prefer[ShapeOwnerTable::SurfaceTable].emplace(surface, _borrowed[ref->file - 1]);
            return;
        }
        return;
    }
    if (shape.ShapeType() != TopAbs_EDGE) {
        return;
    }
    TopLoc_Location loc;
    double first = 0.0;
    double last = 0.0;
    const Handle(Geom_Curve)& curve = BRep_Tool::Curve(TopoDS::Edge(shape), loc, first, last);
    if (curve.IsNull()) {
        return;
    }
    for (TopoDS_Iterator it(shape, false, false); it.More(); it.Next()) {
        const ShapeRef* ref = borrowed(it.Value().Located(TopLoc_Location()));
        if (!ref) {
            continue;
        }
        Handle(BRep_TVertex) vertex = Handle(BRep_TVertex)::DownCast(it.Value().TShape());
        if (vertex.IsNull()) {
            continue;
        }
        for (const Handle(BRep_PointRepresentation)& point : vertex->Points()) {
            if (point->IsPointOnCurve() && point->Curve() == curve) {
                _prefer[ShapeOwnerTable::CurveTable].emplace(curve.get(),
                                                             _borrowed[ref->file - 1]);
                return;
            }
        }
    }
}

std::string ShapeRefSet::plan() const
{
    // Grouped by file and sorted, which is what makes the two sides agree:
    // the writer walks depth first, the reader meets the tokens in record
    // order, and neither order is the other.
    std::map<std::string, std::set<int>> borrowed;
    if (_owners) {
        for (const ShapeRef& ref : _borrowedRefs) {
            const int owner = _borrowed[ref.file - 1];
            borrowed[_owners->file(owner).hash].insert(ref.index);
        }
    }
    for (const auto& token : _borrowedRead) {
        borrowed[_sourceNames[token.first - 1]].insert(token.second);
    }

    // The geometry half, kept in its own section so a file that borrows no
    // geometry states its plan exactly as it did before there was any.
    std::map<std::string, std::set<std::string>> geometry;
    for (int table = 0; table < ShapeOwnerTable::GeomTableCount; ++table) {
        if (!_owners) {
            break;
        }
        for (const GeomEntry& entry : _geometry[table]) {
            if (!entry.ref.file) {
                continue;
            }
            const int owner = _borrowed[entry.ref.file - 1];
            geometry[_owners->file(owner).hash].insert(geomTableTag(table)
                                                       + std::to_string(entry.ref.index));
        }
    }
    for (const auto& token : _geometryRead) {
        geometry[_sourceNames[static_cast<std::size_t>(token[1]) - 1]].insert(
            geomTableTag(token[0]) + std::to_string(token[2]));
    }

    std::string plan;
    for (const auto& file : borrowed) {
        plan += file.first;
        char separator = ':';
        for (int index : file.second) {
            plan += separator;
            plan += std::to_string(index);
            separator = ',';
        }
        plan += ';';
    }
    if (!geometry.empty()) {
        plan += '|';
        for (const auto& file : geometry) {
            plan += file.first;
            char separator = ':';
            for (const std::string& token : file.second) {
                plan += separator;
                plan += token;
                separator = ',';
            }
            plan += ';';
        }
    }
    return plan;
}

void ShapeRefSet::publish(int file, ShapeOwnerTable& owners) const
{
    const int count = _shapes.Extent();
    for (int i = 1; i <= count; ++i) {
        owners.claim(_shapes(i), file, i);
    }
    // Only what this file writes out is offered: an entry that names another
    // file's is already in the table under that file, and pointing a third
    // file at this one instead would make a chain out of what is one step.
    for (int table = 0; table < ShapeOwnerTable::GeomTableCount; ++table) {
        const auto& plan = _geometry[table];
        for (std::size_t i = 0; i < plan.size(); ++i) {
            const int index = static_cast<int>(i) + 1;
            if (!plan[i].ref.file) {
                owners.claimGeometry(table, plan[i].digest, file, index);
            }
            // The handle index takes both, because what a later file needs
            // from it is where this entry *ends up*: this file's own entry
            // where it writes one out, and the entry it names where it does
            // not -- naming that is what puts the two on one object. Only
            // kept where something may be borrowed below a face, which is
            // the only thing that asks the object rather than the bytes.
            if (!_belowFace) {
                continue;
            }
            const ShapeRef resolved =
                plan[i].ref.file ? ShapeRef {_borrowed[plan[i].ref.file - 1], plan[i].ref.index}
                                 : ShapeRef {file, index};
            owners.claimGeometryHandle(table, entryHandle(table, index), file, resolved);
        }
    }

    // And what it decided, so that a later file holding the same geometry can
    // decide the same way and come out as the same bytes.
    //
    // *** Not offered by a file that borrowed a sub-shape: its records name
    // another file's shapes, which a later file made of its own TShapes
    // cannot reproduce, so the two would differ whatever their tables say.
    if (_shareGeometry && _borrowedShapes.Extent() == 0) {
        ShapeOwnerTable::ContentPlan decided;
        for (int table = 0; table < ShapeOwnerTable::GeomTableCount; ++table) {
            decided[table].reserve(_geometry[table].size());
            for (const GeomEntry& entry : _geometry[table]) {
                // In the owner table's terms, not this file's slots, because
                // the file reading it back has slots of its own.
                decided[table].push_back(
                    entry.ref.file ? ShapeRef {_borrowed[entry.ref.file - 1], entry.ref.index}
                                   : ShapeRef {});
            }
        }
        owners.claimContent(contentKey(), std::move(decided));
    }
}

void ShapeRefSet::writeToken(const TopoDS_Shape& shape, std::ostream& out) const
{
    if (shape.IsNull()) {
        out << "*";
        return;
    }
    const TopoDS_Shape base = shape.Located(TopLoc_Location());
    const int index = _shapes.FindIndex(base);
    if (index) {
        printOrientation(shape.Orientation(), out);
        // OCCT's reverse index, kept as it is: within one file it is
        // self-contained, because `TShapes N` is read before any token.
        out << _shapes.Extent() - index + 1;
    }
    else {
        const ShapeRef* ref = borrowed(base);
        if (!ref) {
            // add() puts every sub-shape in one of the two maps, so this is
            // a shape that was never added -- a bug here, not bad input.
            FC_ERR("Shape reference set asked to write an unregistered shape");
            out << "*";
            return;
        }
        out << 'E' << ref->file << ' ';
        printOrientation(shape.Orientation(), out);
        // Forward, because this file cannot state the other one's shape count.
        out << ref->index;
    }
    out << " " << Locations().Index(shape.Location()) << " ";
}

void ShapeRefSet::WriteGeometry(Standard_OStream& out, const Message_ProgressRange& progress)
{
    if (!sharesGeometry()) {
        // Nothing was borrowed into any table, so OCCT writes all six of them
        // and the file is what it has always been, byte for byte.
        BRepTools_ShapeSet::WriteGeometry(out, progress);
        return;
    }
    // The order is OCCT's, and it has to be: the reader is OCCT's wherever
    // this build is not the one reading.
    writeGeometry(ShapeOwnerTable::Curve2dTable, out);
    writeGeometry(ShapeOwnerTable::CurveTable, out);
    WritePolygon3D(out, true);
    WritePolygonOnTriangulation(out, true);
    writeGeometry(ShapeOwnerTable::SurfaceTable, out);
    WriteTriangulation(out, true);
}

void ShapeRefSet::writeGeometry(int table, std::ostream& out) const
{
    const auto& plan = _geometry[table];
    const bool borrows = std::any_of(plan.begin(), plan.end(), [](const GeomEntry& entry) {
        return entry.ref.file != 0;
    });
    if (!borrows) {
        // Written by OCCT itself, so a table that shares nothing stays byte
        // for byte what it was even in a file whose other tables do share.
        switch (table) {
            case ShapeOwnerTable::Curve2dTable:
                Curves2d().Write(out);
                break;
            case ShapeOwnerTable::CurveTable:
                Curves().Write(out);
                break;
            default:
                Surfaces().Write(out);
                break;
        }
        return;
    }

    std::streamsize prec = out.precision(17);
    out << geomTableName(table) << " " << plan.size() << "\n";
    for (const GeomEntry& entry : plan) {
        if (entry.ref.file) {
            out << 'E' << entry.ref.file << " " << entry.ref.index << "\n";
        }
        else {
            // Printed when the plan was settled, and printed once: this is
            // the text that was hashed, so what is written and what was
            // offered to later files cannot drift apart.
            out << entry.text;
        }
    }
    out.precision(prec);
}

void ShapeRefSet::ReadGeometry(Standard_IStream& in, const Message_ProgressRange& progress)
{
    if (_sources.empty()) {
        // No `Files` block, so no entry can name another file's and the
        // tables are ordinary ones. Read by OCCT, as they always were.
        BRepTools_ShapeSet::ReadGeometry(in, progress);
        return;
    }
    if (!readGeometry(ShapeOwnerTable::Curve2dTable, in)
        || !readGeometry(ShapeOwnerTable::CurveTable, in)) {
        return;
    }
    ReadPolygon3D(in);
    ReadPolygonOnTriangulation(in);
    if (!readGeometry(ShapeOwnerTable::SurfaceTable, in)) {
        return;
    }
    ReadTriangulation(in);
}

bool ShapeRefSet::readGeometry(int table, std::istream& in)
{
    char keyword[255] = {};
    in >> keyword;
    if (std::strcmp(keyword, geomTableName(table)) != 0) {
        FC_ERR("Not a " << geomTableName(table) << " table");
        return false;
    }

    int count = 0;
    in >> count;
    for (int i = 1; i <= count; ++i) {
        Handle(Standard_Transient) value;
        in >> std::ws;
        if (in.peek() == 'E') {
            in.get();
            int slot = 0;
            int index = 0;
            in >> slot >> index;
            if (slot < 1 || slot > static_cast<int>(_sources.size())) {
                FC_ERR("A geometry entry names file " << slot << ", which this file does not");
                return false;
            }
            value = geomEntry(*_sources[static_cast<std::size_t>(slot) - 1], table, index);
            if (value.IsNull()) {
                FC_ERR("A geometry entry names position " << index << " of '"
                       << _sourceNames[static_cast<std::size_t>(slot) - 1]
                       << "', which that file does not hold");
                return false;
            }
            _geometryRead.insert({table, slot, index});
        }
        else {
            value = readGeomEntry(table, in);
        }

        const int landed = addGeomEntry(*this, table, value);
        if (landed != i) {
            // *** The table is positional and the records name positions in
            // it, so an entry that landed anywhere but where it was written
            // has moved every entry after it. It can only be an object this
            // file already holds -- which the writer's own rule forbids, so
            // this is a file some other writer made. A copy is a distinct
            // object and lands at the end, which keeps the numbering.
            FC_WARN("A geometry entry of a shape file repeats one it already holds");
            const int copied = addGeomEntry(*this, table, copyGeomEntry(table, value));
            if (copied != i) {
                FC_ERR("Shape file geometry table entries collided at " << i);
                return false;
            }
        }
    }
    return true;
}

void ShapeRefSet::write(const TopoDS_Shape& root, std::ostream& out)
{
    std::locale oldLocale = out.imbue(std::locale::classic());
    std::streamsize prec = out.precision(15);

    out << "\n" << asciiVersions[FormatNb()] << "\n";

    // The one addition to the format, and it is absent when there is nothing
    // to say -- which is what keeps most of a project plain BRep.
    if (!_borrowed.empty()) {
        out << "Files " << _borrowed.size() << "\n";
        for (int file : _borrowed) {
            out << _owners->file(file).hash << "\n";
        }
    }

    ChangeLocations().Write(out);
    WriteGeometry(out);

    const int count = _shapes.Extent();
    out << "\nTShapes " << count << "\n";
    for (int i = 1; i <= count; ++i) {
        const TopoDS_Shape& shape = _shapes(i);

        printShapeEnum(shape.ShapeType(), out);
        out << "\n";

        WriteGeometry(shape, out);

        out << "\n";
        out << (shape.Free() ? 1 : 0);
        out << (shape.Modified() ? 1 : 0);
        out << (shape.Checked() ? 1 : 0);
        out << (shape.Orientable() ? 1 : 0);
        out << (shape.Closed() ? 1 : 0);
        out << (shape.Infinite() ? 1 : 0);
        out << (shape.Convex() ? 1 : 0);
        out << "\n";

        int line = 0;
        for (TopoDS_Iterator it(shape, false, false); it.More(); it.Next()) {
            writeToken(it.Value(), out);
            if (++line == 10) {
                out << "\n";
                line = 0;
            }
        }
        writeToken(TopoDS_Shape(), out);  // the null shape ends the list
        out << "\n";
    }

    out << "\n";
    writeToken(root, out);

    out.precision(prec);
    out.imbue(oldLocale);
}

bool ShapeRefSet::readToken(TopoDS_Shape& shape, std::istream& in, int count) const
{
    char buffer[255] = {};
    in >> buffer;
    if (buffer[0] == '*') {
        shape = TopoDS_Shape();
        return true;
    }

    const ShapeIndexMap* source = nullptr;
    int slot = 0;
    if (buffer[0] == 'E') {
        // Uppercase, and it can only be this: an ordinary token starts with
        // an orientation, whose external code is a lowercase 'e'.
        slot = std::atoi(buffer + 1);
        if (slot < 1 || slot > static_cast<int>(_sources.size())) {
            FC_ERR("Shape reference to file " << slot << ", which this file does not name");
            return false;
        }
        source = &_sources[slot - 1]->shapes();
        in >> buffer;
    }

    const int index = std::atoi(buffer + 1);
    if (source) {
        if (index < 1 || index > source->Extent()) {
            FC_ERR("Shape reference to index " << index << " of a file holding "
                                               << source->Extent());
            return false;
        }
        shape = (*source)(index);
        // What this file states it borrows, which is the plan a save has to
        // compare against before it can keep the file as it is.
        _borrowedRead.insert(std::make_pair(slot, index));
    }
    else {
        const int local = count - index + 1;
        if (local < 1 || local > _shapes.Extent()) {
            FC_ERR("Shape reference to index " << index << " of " << count
                                               << ", which is not read yet");
            return false;
        }
        shape = _shapes(local);
    }

    applyOrientation(shape, buffer[0]);

    int location = 0;
    in >> location;
    shape.Location(Locations().Location(location), false);
    return true;
}

TopoDS_Shape ShapeRefSet::read(std::istream& in)
{
    const TopoDS_Shape root = readShape(in);
    // *** Nothing this set keeps points into another parse once the read is
    // over: a borrowed sub-shape was added to this file's own shape table and
    // a borrowed geometry entry to its own geometry table, and both hold what
    // they name. Dropping the pointers here is what makes that a rule rather
    // than an observation -- the sets they addressed are cache entries, and a
    // cache entry goes when nothing holds its file any more.
    _sources.clear();
    return root;
}



TopoDS_Shape ShapeRefSet::readShape(std::istream& in)
{
    std::locale oldLocale = in.imbue(std::locale::classic());
    Clear();

    // The version banner, skipping whatever a writer put in front of it.
    bool known = false;
    char line[101] = {};
    do {
        in.getline(line, 100, '\n');
        std::size_t length = std::strlen(line);
        while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n')) {
            line[--length] = '\0';
        }
        for (int version = lowestVersion; version <= highestVersion; ++version) {
            if (!std::strcmp(line, asciiVersions[version])) {
                SetFormatNb(version);
                known = true;
                break;
            }
        }
    } while (!known && !in.fail());
    if (!known) {
        FC_ERR("Not a shape file this build can read");
        in.imbue(oldLocale);
        return {};
    }

    // The `Files` block, if there is one. Peeked rather than read: absent, the
    // next word is `Locations`, and TopTools_LocationSet::Read wants to read
    // it itself -- so nothing may be consumed here on that path, and seeking
    // back would demand a stream this need not assume.
    in >> std::ws;
    if (in.peek() == 'F') {
        char keyword[255] = {};
        int files = 0;
        in >> keyword >> files;
        if (std::strcmp(keyword, "Files") != 0 || files < 0) {
            FC_ERR("Malformed file table in a shape file");
            in.imbue(oldLocale);
            return {};
        }
        for (int i = 0; i < files; ++i) {
            std::string name;
            in >> name;
            const ShapeRefSet* source = _resolve ? _resolve(name) : nullptr;
            if (!source) {
                FC_ERR("Cannot resolve '" << name << "', which this shape is stored against");
                in.imbue(oldLocale);
                return {};
            }
            _sources.push_back(source);
            _sourceNames.push_back(name);
        }
    }

    ChangeLocations().Read(in);
    ReadGeometry(in);

    char keyword[255] = {};
    in >> keyword;
    if (std::strcmp(keyword, "TShapes") != 0) {
        FC_ERR("Not a shape table");
        in.imbue(oldLocale);
        return {};
    }

    int count = 0;
    in >> count;
    for (int i = 1; i <= count; ++i) {
        TopoDS_Shape shape;
        const TopAbs_ShapeEnum type = readShapeEnum(in);
        ReadGeometry(type, in, shape);

        char flags[255] = {};
        in >> flags;

        TopoDS_Shape sub;
        do {
            if (!readToken(sub, in, count)) {
                in.imbue(oldLocale);
                return {};
            }
            if (!sub.IsNull()) {
                AddShapes(shape, sub);
            }
        } while (!sub.IsNull());

        shape.Free(flags[0] == '1');
        shape.Modified(flags[1] == '1');
        shape.Checked(FormatNb() >= TopTools_FormatVersion_VERSION_2 && flags[2] == '1');
        shape.Orientable(flags[3] == '1');
        shape.Closed(flags[4] == '1');
        shape.Infinite(flags[5] == '1');
        shape.Convex(flags[6] == '1');

        if (FormatNb() == TopTools_FormatVersion_VERSION_1) {
            Check(type, shape);
        }

        if (_shapes.Add(shape) != i) {
            // Every record builds its own TShape, so the map cannot collapse
            // two of them -- but if it ever did, every index after it would
            // silently address the wrong shape.
            FC_ERR("Shape table records collided at index " << i);
            in.imbue(oldLocale);
            return {};
        }
    }

    TopoDS_Shape root;
    if (!readToken(root, in, count)) {
        in.imbue(oldLocale);
        return {};
    }

    in.imbue(oldLocale);
    return root;
}
