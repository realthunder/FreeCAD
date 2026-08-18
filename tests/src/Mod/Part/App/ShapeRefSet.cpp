// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <sstream>

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Surface.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <Mod/Part/App/ShapeRefSet.h>
#include <Mod/Part/App/TopoShape.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{

/// Serialize a shape the way a save does, borrowing whatever \a owners holds.
std::string
writeSet(const TopoDS_Shape& shape, const Part::ShapeOwnerTable* owners, Part::ShapeRefSet& set)
{
    set.setOwners(owners);
    set.build(shape);
    std::ostringstream out;
    set.write(shape, out);
    return out.str();
}

/// Record a written file the way a save does, with its root declared.
int publishAs(const Part::ShapeRefSet& set, const char* hash, Part::ShapeOwnerTable& owners)
{
    Part::ShapeOwnerTable::File entry;
    entry.hash = hash;
    entry.plan = set.plan();
    entry.root = set.rootIndex();
    entry.orientation = TopAbs_FORWARD;
    const int index = owners.addFile(entry);
    set.publish(index, owners);
    return index;
}

/// Serialize a shape with the geometry tables allowed to name another file's.
std::string
writeShared(const TopoDS_Shape& shape, const Part::ShapeOwnerTable* owners, Part::ShapeRefSet& set)
{
    set.setOwners(owners);
    set.setGeometrySharing(true);
    set.build(shape);
    std::ostringstream out;
    set.write(shape, out);
    return out.str();
}

/// Serialize a shape with sub-shapes allowed below a face, which needs the
/// shared geometry tables that make their associations nameable.
std::string
writeBelow(const TopoDS_Shape& shape, const Part::ShapeOwnerTable* owners, Part::ShapeRefSet& set)
{
    set.setOwners(owners);
    set.setGeometrySharing(true);
    set.setSubFaceBorrowing(7);
    set.build(shape);
    std::ostringstream out;
    set.write(shape, out);
    return out.str();
}

/// The surface object under a shape's first face, which is what says whether
/// two shapes came back standing on one surface or on two equal ones.
Handle(Geom_Surface) surfaceOf(const TopoDS_Shape& shape)
{
    TopExp_Explorer it(shape, TopAbs_FACE);
    return it.More() ? BRep_Tool::Surface(TopoDS::Face(it.Current())) : nullptr;
}

int countFaces(const TopoDS_Shape& shape)
{
    int faces = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next()) {
        ++faces;
    }
    return faces;
}

}  // namespace

/** A file that borrows nothing must be what the fork writes today.
 *
 * This is the whole claim that most of a project stays plain BRep: the
 * extension is present only where sharing is.
 */
TEST(ShapeRefSet, PlainFileIsUnchanged)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();

    std::ostringstream expected;
    Part::TopoShape(box).exportBrep(expected);

    Part::ShapeRefSet set;
    const std::string written = writeSet(box, nullptr, set);

    EXPECT_FALSE(set.borrows());
    EXPECT_EQ(0, set.references());
    EXPECT_EQ(expected.str(), written);
}

TEST(ShapeRefSet, RoundTripWithoutReferences)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();

    Part::ShapeRefSet set;
    std::istringstream in(writeSet(box, nullptr, set));

    BRep_Builder builder;
    Part::ShapeRefSet reader(builder);
    const TopoDS_Shape read = reader.read(in);

    ASSERT_FALSE(read.IsNull());
    EXPECT_EQ(TopAbs_SOLID, read.ShapeType());
    EXPECT_EQ(countFaces(box), countFaces(read));
}

/** The point of the whole step: a sub-shape stored in an earlier file is
 * referenced rather than serialized again, and comes back as the same TShape.
 */
TEST(ShapeRefSet, BorrowedSubShapeIsShared)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape other = BRepPrimAPI_MakeBox(1.0, 2.0, 3.0).Shape();

    // The first file, and what it puts on the table for later files.
    Part::ShapeRefSet first;
    const std::string firstText = writeSet(box, nullptr, first);
    Part::ShapeOwnerTable owners;
    publishAs(first, "first", owners);
    EXPECT_EQ(static_cast<std::size_t>(first.shapes().Extent()), owners.size());

    // The second holds the very same solid alongside one of its own.
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, box);
    builder.Add(compound, other);

    Part::ShapeRefSet second;
    const std::string secondText = writeSet(compound, &owners, second);

    EXPECT_TRUE(second.borrows());
    // One reference: the walk stops at the solid and does not descend, so
    // none of its faces, edges or vertices are counted or stored.
    EXPECT_EQ(1, second.references());
    EXPECT_NE(std::string::npos, secondText.find("\nFiles 1\nfirst\n"));
    EXPECT_NE(std::string::npos, secondText.find("E1 "));
    // The borrowed solid's geometry is genuinely absent: the compound stores
    // its own box and the reference, nothing more.
    EXPECT_LT(secondText.size(), firstText.size() * 2);

    // Reading the second resolves the first through the table it names.
    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape secondRead = secondReader.read(secondIn);

    ASSERT_FALSE(secondRead.IsNull());
    TopoDS_Iterator children(secondRead);
    ASSERT_TRUE(children.More());
    EXPECT_TRUE(children.Value().IsPartner(firstRead));
    EXPECT_EQ(countFaces(compound), countFaces(secondRead));

    // The plan the file was written with is the plan reading it back states.
    // A save with nothing to do rests on that, and the two sides meet the
    // references in different orders.
    EXPECT_TRUE(first.plan().empty());
    EXPECT_FALSE(second.plan().empty());
    EXPECT_EQ(second.plan(), secondReader.plan());
}

/// A file whose root is itself borrowed holds no shapes at all.
TEST(ShapeRefSet, BorrowedRootLeavesAnEmptyTable)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    TopExp_Explorer explorer(box, TopAbs_FACE);
    ASSERT_TRUE(explorer.More());
    const TopoDS_Shape face = explorer.Current();

    Part::ShapeRefSet first;
    const std::string firstText = writeSet(box, nullptr, first);
    Part::ShapeOwnerTable owners;
    publishAs(first, "first", owners);

    Part::ShapeRefSet second;
    const std::string secondText = writeSet(face, &owners, second);
    EXPECT_EQ(0, second.shapes().Extent());
    EXPECT_NE(std::string::npos, secondText.find("\nTShapes 0\n"));

    BRep_Builder builder;
    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape secondRead = secondReader.read(secondIn);

    ASSERT_FALSE(secondRead.IsNull());
    EXPECT_EQ(TopAbs_FACE, secondRead.ShapeType());
    TopExp_Explorer readFaces(firstRead, TopAbs_FACE);
    ASSERT_TRUE(readFaces.More());
    EXPECT_TRUE(secondRead.IsPartner(readFaces.Current()));
}

/** A borrowed shape keeps the location the borrowing file gives it.
 *
 * The owner table is keyed on TShape identity, so a moved copy borrows the
 * same entry -- which is exactly what location canonicalization (sec 11.4)
 * bought, and what makes two equal parts at different placements share.
 */
TEST(ShapeRefSet, BorrowedShapeKeepsItsOwnLocation)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();

    Part::ShapeRefSet first;
    const std::string firstText = writeSet(box, nullptr, first);
    Part::ShapeOwnerTable owners;
    publishAs(first, "first", owners);

    gp_Trsf move;
    move.SetTranslation(gp_Vec(10.0, 0.0, 0.0));
    const TopoDS_Shape moved = box.Moved(TopLoc_Location(move));

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, moved);

    Part::ShapeRefSet second;
    const std::string secondText = writeSet(compound, &owners, second);
    EXPECT_EQ(1, second.references());

    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape secondRead = secondReader.read(secondIn);

    ASSERT_FALSE(secondRead.IsNull());
    TopoDS_Iterator children(secondRead);
    ASSERT_TRUE(children.More());
    EXPECT_TRUE(children.Value().IsPartner(firstRead));
    const gp_XYZ offset = children.Value().Location().Transformation().TranslationPart();
    EXPECT_DOUBLE_EQ(10.0, offset.X());
}

/// A file naming something the reader cannot supply fails, rather than
/// producing a shape that is quietly missing the geometry it borrowed.
TEST(ShapeRefSet, UnresolvableFileFailsTheRead)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();

    Part::ShapeRefSet first;
    writeSet(box, nullptr, first);
    Part::ShapeOwnerTable owners;
    publishAs(first, "first", owners);

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, box);

    Part::ShapeRefSet second;
    std::istringstream in(writeSet(compound, &owners, second));

    Part::ShapeRefSet reader(builder);
    reader.setResolver([](const std::string&) -> const Part::ShapeRefSet* {
        return nullptr;
    });
    EXPECT_TRUE(reader.read(in).IsNull());
}

/** Sharing on, but nothing to share with: still plain BRep.
 *
 * The same claim the first case makes about sub-shapes, for the tables -- a
 * table nothing was borrowed into is written by OCCT itself.
 */
TEST(ShapeRefSet, SharedGeometryWithNoOwnerIsUnchanged)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();

    std::ostringstream expected;
    Part::TopoShape(box).exportBrep(expected);

    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet set;
    const std::string written = writeShared(box, &owners, set);

    EXPECT_FALSE(set.borrows());
    EXPECT_EQ(0, set.geometryReferences());
    EXPECT_EQ(expected.str(), written);
}

/** Two parts built apart are two TShapes and the same geometry.
 *
 * Nothing can be borrowed as a sub-shape -- the owner table is keyed on TShape
 * identity and these share none -- so what is left is the tables, which is
 * exactly the duplication sec 12.8 measured across a real project's files.
 *
 * The second file holds a shape of its own around that geometry, because two
 * files that would hold the *same* bytes are a different case: they are one
 * file already, and naming rather than writing is what would split them
 * (see EqualContentStaysOneFile).
 */
TEST(ShapeRefSet, EqualGeometryIsNamedInTheEarlierFile)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape twin = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape rod = BRepPrimAPI_MakeCylinder(2.0, 5.0).Shape();
    ASSERT_FALSE(box.IsPartner(twin));

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, twin);
    builder.Add(compound, rod);

    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet first;
    const std::string firstText = writeShared(box, &owners, first);
    publishAs(first, "first", owners);

    Part::ShapeRefSet second;
    const std::string secondText = writeShared(compound, &owners, second);

    // The same file with nothing to name, which is what it is worth against.
    Part::ShapeOwnerTable none;
    Part::ShapeRefSet alone;
    const std::string aloneText = writeShared(compound, &none, alone);

    EXPECT_GT(second.geometryReferences(), 0);
    EXPECT_TRUE(second.borrows());
    EXPECT_NE(std::string::npos, secondText.find("\nFiles 1\nfirst\n"));
    EXPECT_LT(secondText.size(), aloneText.size());

    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape secondRead = secondReader.read(secondIn);

    ASSERT_FALSE(secondRead.IsNull());
    EXPECT_EQ(countFaces(compound), countFaces(secondRead));
    // Its box stands on the first file's surfaces, which is the sharing this
    // buys in memory as well as in bytes.
    TopoDS_Iterator children(secondRead);
    ASSERT_TRUE(children.More());
    EXPECT_EQ(surfaceOf(firstRead), surfaceOf(children.Value()));

    // Both sides state the same plan, which is what a save with nothing to do
    // rests on. The writer settles the tables in order, the reader meets them
    // in the file.
    EXPECT_EQ(second.plan(), secondReader.plan());
    EXPECT_NE(std::string::npos, second.plan().find('|'));
}

/** Two files that would hold the same bytes stay one file.
 *
 * Content addressing merges them, and it is worth more than naming the
 * entries -- which would make the second file a handful of references, and so
 * a second file. `MiSTer` went from 5204 files to 6841 before this rule.
 */
TEST(ShapeRefSet, EqualContentStaysOneFile)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape twin = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    ASSERT_FALSE(box.IsPartner(twin));

    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet first;
    const std::string firstText = writeShared(box, &owners, first);
    publishAs(first, "first", owners);

    Part::ShapeRefSet second;
    const std::string secondText = writeShared(twin, &owners, second);

    EXPECT_EQ(0, second.geometryReferences());
    EXPECT_FALSE(second.borrows());
    // The whole claim: identical bytes, so the store holds one of them.
    EXPECT_EQ(firstText, secondText);
}

/** One entry of another file may be named once only.
 *
 * A compound of two equal boxes holds each plane twice, as two objects. Only
 * one of the two may name the earlier file's: a surface reaching two faces at
 * once would make the pcurve lookups those faces key on it ambiguous, and on
 * the read side it would land on a position the table already has and shift
 * every entry after it.
 */
TEST(ShapeRefSet, AnEntryIsNamedOnceWithinAFile)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape twin = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape third = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();

    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet first;
    const std::string firstText = writeShared(box, &owners, first);
    publishAs(first, "first", owners);

    // Two solids of its own, neither of them the one the first file holds, so
    // nothing is borrowed as a sub-shape and both want the same entries.
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, twin);
    builder.Add(compound, third);

    Part::ShapeRefSet second;
    const std::string secondText = writeShared(compound, &owners, second);
    const int named = second.geometryReferences();
    EXPECT_GT(named, 0);

    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape secondRead = secondReader.read(secondIn);

    ASSERT_FALSE(secondRead.IsNull());
    EXPECT_EQ(2 * countFaces(box), countFaces(secondRead));
    EXPECT_EQ(named, secondReader.geometryReferences());
    EXPECT_EQ(second.plan(), secondReader.plan());

    // The rule itself: the first solid stands on the earlier file's surface,
    // the second on its own copy. Equal geometry, and deliberately not one
    // object -- each face has to key its own edges' 2D curves on it.
    TopoDS_Iterator children(secondRead);
    ASSERT_TRUE(children.More());
    const TopoDS_Shape one = children.Value();
    children.Next();
    ASSERT_TRUE(children.More());
    const TopoDS_Shape two = children.Value();
    EXPECT_EQ(surfaceOf(firstRead), surfaceOf(one));
    EXPECT_NE(surfaceOf(one), surfaceOf(two));
}

/** A geometry entry naming a position the file does not hold fails the read.
 *
 * The same rule as an unresolvable file: a shape quietly missing the surface
 * it stands on is worse than no shape.
 */
TEST(ShapeRefSet, GeometryOutOfRangeFailsTheRead)
{
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape twin = BRepPrimAPI_MakeBox(4.0, 5.0, 6.0).Shape();
    const TopoDS_Shape rod = BRepPrimAPI_MakeCylinder(2.0, 5.0).Shape();

    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet first;
    writeShared(box, &owners, first);
    publishAs(first, "first", owners);

    BRep_Builder shapes;
    TopoDS_Compound compound;
    shapes.MakeCompound(compound);
    shapes.Add(compound, twin);
    shapes.Add(compound, rod);

    Part::ShapeRefSet second;
    std::string text = writeShared(compound, &owners, second);
    const std::size_t at = text.find("\nE1 ");
    ASSERT_NE(std::string::npos, at);
    text.replace(at, 4, "\nE1 999 ");

    // An empty file to resolve against: it names positions nothing holds.
    BRep_Builder builder;
    Part::ShapeRefSet empty(builder);
    std::istringstream in(text);
    Part::ShapeRefSet reader(builder);
    reader.setResolver([&](const std::string&) -> const Part::ShapeRefSet* {
        return &empty;
    });
    EXPECT_TRUE(reader.read(in).IsNull());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

/** A cylinder read back with its seam edge's vertices borrowed still has a
 * 2D curve for that edge on its own face.
 *
 * The case sub-face borrowing exists to make safe, and the one that breaks
 * first: a face keys its edges' 2D curves on the surface object it carries,
 * and a vertex keys its parameter on the curve of the edge it sits on, so
 * both of those objects have to be the ones the file the shapes came from
 * holds. Every failure the first cut of this produced on a real model was a
 * cylinder, which is this.
 */
TEST(ShapeRefSet, SubFaceBorrowingKeepsTheAssociations)
{
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(3.0, 8.0).Shape();

    // The first file holds the circular edges, so the second stores the
    // cylindrical face and the seam edge itself while borrowing what they
    // stand on -- the edges, and through them the seam's own vertices.
    TopoDS_Compound edges;
    BRep_Builder builder;
    builder.MakeCompound(edges);
    int taken = 0;
    for (TopExp_Explorer it(cylinder, TopAbs_EDGE); it.More(); it.Next()) {
        if (BRep_Tool::IsClosed(TopoDS::Edge(it.Current()))) {
            continue;
        }
        builder.Add(edges, it.Current());
        ++taken;
    }
    ASSERT_GT(taken, 0);

    // Through an owner table, as every file in a save is: a file writing on
    // its own publishes no geometry, and then there is nothing to name.
    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet first;
    const std::string firstText = writeBelow(edges, &owners, first);
    publishAs(first, "first", owners);

    Part::ShapeRefSet second;
    const std::string secondText = writeBelow(cylinder, &owners, second);
    EXPECT_TRUE(second.borrows());
    EXPECT_GT(second.references(), 0);

    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape read = secondReader.read(secondIn);
    ASSERT_FALSE(read.IsNull());
    EXPECT_EQ(countFaces(cylinder), countFaces(read));

    // Every edge of every face has to have its 2D curve on that face, which
    // is what anything projecting the shape asks for.
    int missing = 0;
    for (TopExp_Explorer face(read, TopAbs_FACE); face.More(); face.Next()) {
        for (TopExp_Explorer edge(face.Current(), TopAbs_EDGE); edge.More(); edge.Next()) {
            double first2d = 0.0;
            double last2d = 0.0;
            const Handle(Geom2d_Curve) pcurve =
                BRep_Tool::CurveOnSurface(TopoDS::Edge(edge.Current()),
                                          TopoDS::Face(face.Current()),
                                          first2d,
                                          last2d);
            if (pcurve.IsNull()) {
                ++missing;
            }
        }
    }
    EXPECT_EQ(0, missing);
    EXPECT_TRUE(BRepCheck_Analyzer(read).IsValid());
}

/** And the same when the borrowing file uses the shape somewhere else.
 *
 * A 2D curve is keyed on its surface *and* on where the edge sits relative
 * to the face, so a shape borrowed at another location asks the association
 * a question the file it came from never wrote down. Whole parts have always
 * been borrowed across a location -- that is what location canonicalization
 * bought -- but below a face the location is part of the key.
 */
TEST(ShapeRefSet, SubFaceBorrowingAcrossALocation)
{
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(3.0, 8.0).Shape();

    TopoDS_Compound edges;
    BRep_Builder builder;
    builder.MakeCompound(edges);
    for (TopExp_Explorer it(cylinder, TopAbs_EDGE); it.More(); it.Next()) {
        if (!BRep_Tool::IsClosed(TopoDS::Edge(it.Current()))) {
            builder.Add(edges, it.Current());
        }
    }

    Part::ShapeOwnerTable owners;
    Part::ShapeRefSet first;
    const std::string firstText = writeBelow(edges, &owners, first);
    publishAs(first, "first", owners);

    gp_Trsf move;
    move.SetTranslation(gp_Vec(10.0, 20.0, 30.0));
    const TopoDS_Shape moved = cylinder.Moved(TopLoc_Location(move));

    Part::ShapeRefSet second;
    const std::string secondText = writeBelow(moved, &owners, second);
    EXPECT_TRUE(second.borrows());

    std::istringstream firstIn(firstText);
    Part::ShapeRefSet firstReader(builder);
    const TopoDS_Shape firstRead = firstReader.read(firstIn);
    ASSERT_FALSE(firstRead.IsNull());

    std::istringstream secondIn(secondText);
    Part::ShapeRefSet secondReader(builder);
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeRefSet* {
        return name == "first" ? &firstReader : nullptr;
    });
    const TopoDS_Shape read = secondReader.read(secondIn);
    ASSERT_FALSE(read.IsNull());

    int missing = 0;
    for (TopExp_Explorer face(read, TopAbs_FACE); face.More(); face.Next()) {
        for (TopExp_Explorer edge(face.Current(), TopAbs_EDGE); edge.More(); edge.Next()) {
            double first2d = 0.0;
            double last2d = 0.0;
            if (BRep_Tool::CurveOnSurface(TopoDS::Edge(edge.Current()),
                                          TopoDS::Face(face.Current()),
                                          first2d,
                                          last2d)
                    .IsNull()) {
                ++missing;
            }
        }
    }
    EXPECT_EQ(0, missing);
    EXPECT_TRUE(BRepCheck_Analyzer(read).IsValid());
}
