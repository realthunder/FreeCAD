// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2026 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include <gtest/gtest.h>

#include <string>

#include <QByteArray>
#include <QDir>
#include <QString>

#include <Mod/Material/App/Library.h>

/// A library takes its own name, and its own root directory, off the front of
/// a path it is given -- and both are whole path components. Getting that
/// wrong was not academic: the library named "User" found "/User" at the
/// front of every absolute path under a macOS home, took it off, and keyed
/// the card at "s/someone/..." -- so a card saved into the User library could
/// not be read back at all. The same unbounded test decided which library a
/// path belonged to, where a library at ".../Material" would answer for a
/// path in ".../Material2".
class TestLibraryPaths: public ::testing::Test
{
protected:
    // The QByteArray icon overload, so constructing one does not go looking
    // for an icon file that is not the subject of these tests
    Materials::Library user {QStringLiteral("User"), QByteArray(), false};

    void SetUp() override
    {
        // An existing directory: getDirectoryPath() canonicalizes, and what
        // it answers for a directory that is not there is not fixed
        user.setDirectory(QDir::tempPath());
    }

    std::string root() const
    {
        return user.getDirectoryPath().toStdString();
    }

    /// gtest prints a QString as a list of two-byte objects, which tells you
    /// nothing about which path came back
    std::string relative(const char* path) const
    {
        return user.getRelativePath(QString::fromUtf8(path)).toStdString();
    }

    /// cleanPath because getLocalPath joins the library root to a remainder
    /// that still carries its own leading '/'; the doubled separator is not
    /// what these tests are about
    std::string local(const char* path) const
    {
        return QDir::cleanPath(user.getLocalPath(QString::fromUtf8(path))).toStdString();
    }

    /// What getRelativePath does to a path it strips nothing else from: it
    /// removes a leading '/' IF THERE IS ONE (Library.cpp, "Remove any
    /// leading '/'"). Spelling that as substr(1) assumes there always is,
    /// which holds wherever QDir::tempPath() is "/var/..." and does not on
    /// Windows, where it is "C:/Users/.../Temp" and the 'C' gets eaten.
    static std::string withoutLeadingSlash(const std::string& path)
    {
        return (!path.empty() && path.front() == '/') ? path.substr(1) : path;
    }
};

TEST_F(TestLibraryPaths, relativePathStripsTheNameAsAWholeComponent)
{
    EXPECT_EQ(relative("/User/Metal/Steel.FCMat"), "Metal/Steel.FCMat");
}

TEST_F(TestLibraryPaths, relativePathKeepsAComponentThatMerelyStartsWithTheName)
{
    // "/Users/..." is the shape of every absolute path on macOS
    EXPECT_EQ(relative("/Users/someone/Steel.FCMat"), "Users/someone/Steel.FCMat");
    EXPECT_EQ(relative("/Userspace/Steel.FCMat"), "Userspace/Steel.FCMat");
}

TEST_F(TestLibraryPaths, relativePathOfTheNameAloneIsEmpty)
{
    EXPECT_EQ(relative("/User"), "");
}

TEST_F(TestLibraryPaths, localPathStripsTheNameAsAWholeComponent)
{
    EXPECT_EQ(local("/User/Metal/Steel.FCMat"), root() + "/Metal/Steel.FCMat");
    EXPECT_EQ(local("/Users/someone/Steel.FCMat"), root() + "/Users/someone/Steel.FCMat");
}

TEST_F(TestLibraryPaths, relativePathStripsTheRootAsAWholeComponent)
{
    const std::string card = root() + "/Metal/Steel.FCMat";
    EXPECT_EQ(relative(card.c_str()), "Metal/Steel.FCMat");
}

TEST_F(TestLibraryPaths, relativePathKeepsADirectoryThatMerelyStartsWithTheRoot)
{
    // A sibling directory whose name begins with the library's own
    const std::string sibling = root() + "2/Steel.FCMat";
    EXPECT_EQ(relative(sibling.c_str()), withoutLeadingSlash(sibling));
}

/// The test the three other callers make through Library::isPathPrefix:
/// two managers deciding which library owns a path, and a folder rename
/// deciding which cards move with it
TEST_F(TestLibraryPaths, isPathPrefixWantsAWholeComponent)
{
    using Materials::Library;

    EXPECT_TRUE(Library::isPathPrefix(QStringLiteral("/a/b"), QStringLiteral("/a/b")));
    EXPECT_TRUE(Library::isPathPrefix(QStringLiteral("/a/b/c"), QStringLiteral("/a/b")));
    EXPECT_FALSE(Library::isPathPrefix(QStringLiteral("/a/bb/c"), QStringLiteral("/a/b")));
    EXPECT_FALSE(Library::isPathPrefix(QStringLiteral("/a/b2"), QStringLiteral("/a/b")));
    EXPECT_FALSE(Library::isPathPrefix(QStringLiteral("/a"), QStringLiteral("/a/b")));

    // Relative keys, which is the shape a folder rename compares
    EXPECT_TRUE(Library::isPathPrefix(QStringLiteral("Metal/Steel.FCMat"),
                                      QStringLiteral("Metal")));
    EXPECT_FALSE(Library::isPathPrefix(QStringLiteral("Metals/Iron.FCMat"),
                                       QStringLiteral("Metal")));
}

TEST_F(TestLibraryPaths, isPathPrefixKeepsTheTwoEdgesStartsWithHad)
{
    using Materials::Library;

    // An empty prefix took nothing off and matched everything; it still does
    EXPECT_TRUE(Library::isPathPrefix(QStringLiteral("/a/b"), QString()));
    // A prefix that already ends at a separator has its own boundary
    EXPECT_TRUE(Library::isPathPrefix(QStringLiteral("/a/b"), QStringLiteral("/")));
}

TEST_F(TestLibraryPaths, isPathPrefixHonoursTheCaseSensitivityItIsGiven)
{
    using Materials::Library;

    EXPECT_FALSE(Library::isPathPrefix(QStringLiteral("/A/b"), QStringLiteral("/a")));
    EXPECT_TRUE(
        Library::isPathPrefix(QStringLiteral("/A/b"), QStringLiteral("/a"), Qt::CaseInsensitive));
}
