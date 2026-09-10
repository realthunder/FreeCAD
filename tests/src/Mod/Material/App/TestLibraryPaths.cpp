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

/// A library strips its own name off the front of a path it is given, and the
/// name is a whole path component. Getting that wrong was not academic: the
/// library named "User" found "/User" at the front of every absolute path
/// under a macOS home, took it off, and keyed the card at "s/someone/..." --
/// so a card saved into the User library could not be read back at all.
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
