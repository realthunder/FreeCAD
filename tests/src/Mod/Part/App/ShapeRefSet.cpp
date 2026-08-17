// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <sstream>

#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Trsf.hxx>

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
    set.add(shape);
    std::ostringstream out;
    set.write(shape, out);
    return out.str();
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
    first.publish(owners.addFile("first"), owners);
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
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeIndexMap* {
        return name == "first" ? &firstReader.shapes() : nullptr;
    });
    const TopoDS_Shape secondRead = secondReader.read(secondIn);

    ASSERT_FALSE(secondRead.IsNull());
    TopoDS_Iterator children(secondRead);
    ASSERT_TRUE(children.More());
    EXPECT_TRUE(children.Value().IsPartner(firstRead));
    EXPECT_EQ(countFaces(compound), countFaces(secondRead));
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
    first.publish(owners.addFile("first"), owners);

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
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeIndexMap* {
        return name == "first" ? &firstReader.shapes() : nullptr;
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
    first.publish(owners.addFile("first"), owners);

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
    secondReader.setResolver([&](const std::string& name) -> const Part::ShapeIndexMap* {
        return name == "first" ? &firstReader.shapes() : nullptr;
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
    first.publish(owners.addFile("first"), owners);

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, box);

    Part::ShapeRefSet second;
    std::istringstream in(writeSet(compound, &owners, second));

    Part::ShapeRefSet reader(builder);
    reader.setResolver([](const std::string&) -> const Part::ShapeIndexMap* {
        return nullptr;
    });
    EXPECT_TRUE(reader.read(in).IsNull());
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
