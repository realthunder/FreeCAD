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
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <iostream>
#include <locale>
#include <BRep_Builder.hxx>
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

}  // namespace

// ---------------------------------------------------------------------------
// ShapeOwnerTable
// ---------------------------------------------------------------------------

int ShapeOwnerTable::addFile(const std::string& file)
{
    auto known = _fileIndex.find(file);
    if (known != _fileIndex.end()) {
        return known->second;
    }
    _files.push_back(file);
    const int index = static_cast<int>(_files.size());
    _fileIndex[file] = index;
    return index;
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

void ShapeOwnerTable::clear()
{
    _refs.clear();
    _files.clear();
    _fileIndex.clear();
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
}

ShapeRefSet::ShapeRefSet(const BRep_Builder& builder)
    : BRepTools_ShapeSet(builder, false)
{
    SetFormatNb(TopTools_FormatVersion_VERSION_1);
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
}

void ShapeRefSet::setOwners(const ShapeOwnerTable* owners)
{
    _owners = owners;
}

void ShapeRefSet::setResolver(Resolver resolver)
{
    _resolve = std::move(resolver);
}

void ShapeRefSet::borrow(const TopoDS_Shape& shape, const ShapeRef& ref)
{
    const int slot = _borrowedShapes.Add(shape);
    if (slot > static_cast<int>(_borrowedRefs.size())) {
        // First use of this file in this one: give it a slot in the `Files`
        // block. The block is per file, so the numbers stay small and stable
        // even though the owner table's are neither.
        int local = 0;
        for (std::size_t i = 0; i < _borrowed.size(); ++i) {
            if (_borrowed[i] == ref.file) {
                local = static_cast<int>(i) + 1;
                break;
            }
        }
        if (!local) {
            _borrowed.push_back(ref.file);
            local = static_cast<int>(_borrowed.size());
        }
        _borrowedRefs.resize(slot);
        _borrowedRefs[slot - 1] = ShapeRef {local, ref.index};
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

int ShapeRefSet::add(const TopoDS_Shape& shape)
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
    if (borrowed(base)) {
        return 0;
    }
    if (_owners) {
        if (const ShapeRef* ref = _owners->find(base)) {
            borrow(base, *ref);
            return 0;
        }
    }

    AddGeometry(base);
    for (TopoDS_Iterator it(base, false, false); it.More(); it.Next()) {
        add(it.Value());
    }
    return _shapes.Add(base);
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
            borrowed[_owners->files()[owner - 1]].insert(ref.index);
        }
    }
    for (const auto& token : _borrowedRead) {
        borrowed[_sourceNames[token.first - 1]].insert(token.second);
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
    return plan;
}

void ShapeRefSet::publish(int file, ShapeOwnerTable& owners) const
{
    const int count = _shapes.Extent();
    for (int i = 1; i <= count; ++i) {
        owners.claim(_shapes(i), file, i);
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
            out << _owners->files()[file - 1] << "\n";
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
        source = _sources[slot - 1];
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
            const ShapeIndexMap* source = _resolve ? _resolve(name) : nullptr;
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
