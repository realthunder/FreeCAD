// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <Mod/Import/App/ReaderLook.h>

// Which object a MaterialX <look> means by a geometry string
// (docs/MaterialStorage.md sec 17.13 item 2). The name on the FreeCAD
// side is an object's Label, which is what an importer writes the mesh's
// name into.

TEST(ReaderLookMatches, aPlainNameIsTheName)
{
    EXPECT_TRUE(Import::ReaderLook::matches("Bishop_B", "Bishop_B"));
    EXPECT_FALSE(Import::ReaderLook::matches("Bishop_B", "Bishop_W"));
    // The whole string, never a part of it: a look assigning Bishop_B
    // does not dress Bishop_B001, which is what an import of the same
    // asset twice makes
    EXPECT_FALSE(Import::ReaderLook::matches("Bishop_B", "Bishop_B001"));
    EXPECT_FALSE(Import::ReaderLook::matches("Bishop", "Bishop_B"));
}

TEST(ReaderLookMatches, aPathAnswersByItsLastElement)
{
    EXPECT_TRUE(Import::ReaderLook::matches("/root/Bishop_B", "Bishop_B"));
    EXPECT_TRUE(Import::ReaderLook::matches("/Bishop_B", "Bishop_B"));
    // ... and by itself, for an importer that kept the path
    EXPECT_TRUE(Import::ReaderLook::matches("/root/Bishop_B", "/root/Bishop_B"));
    EXPECT_FALSE(Import::ReaderLook::matches("/root/Bishop_B", "root"));
}

TEST(ReaderLookMatches, aListIsAnyOfIt)
{
    EXPECT_TRUE(Import::ReaderLook::matches("Pawn_Top_B, Pawn_Body_B", "Pawn_Body_B"));
    EXPECT_TRUE(Import::ReaderLook::matches("Pawn_Top_B Pawn_Body_B", "Pawn_Top_B"));
    EXPECT_FALSE(Import::ReaderLook::matches("Pawn_Top_B, Pawn_Body_B", "Pawn_Top_W"));
}

TEST(ReaderLookMatches, wildcardsAreGlobs)
{
    EXPECT_TRUE(Import::ReaderLook::matches("Pawn_*", "Pawn_Body_W"));
    EXPECT_TRUE(Import::ReaderLook::matches("*_B", "Bishop_B"));
    EXPECT_TRUE(Import::ReaderLook::matches("Pawn_?ody_B", "Pawn_Body_B"));
    EXPECT_TRUE(Import::ReaderLook::matches("*", "anything"));
    EXPECT_TRUE(Import::ReaderLook::matches("/root/*", "Bishop_B"));
    EXPECT_FALSE(Import::ReaderLook::matches("Pawn_*", "Bishop_B"));
    EXPECT_FALSE(Import::ReaderLook::matches("Pawn_?", "Pawn_Body_B"));
}

TEST(ReaderLookMatches, nothingMatchesNothing)
{
    EXPECT_FALSE(Import::ReaderLook::matches("", "Bishop_B"));
    EXPECT_FALSE(Import::ReaderLook::matches("Bishop_B", ""));
    EXPECT_FALSE(Import::ReaderLook::matches("", ""));
}
