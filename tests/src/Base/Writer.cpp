// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include "Base/Exception.h"
#include "Base/FileInfo.h"
#include "Base/Stream.h"
#include "Base/Writer.h"
#include <src/TempDirectory.h>
#include <sstream>

// Writer is designed to be a base class, so for testing we actually instantiate a StringWriter,
// which is derived from it

class WriterTest: public ::testing::Test
{
protected:
    // void SetUp() override {}

    // void TearDown() override {}
protected:
    Base::StringWriter _writer;
};

TEST_F(WriterTest, insertTextSimple)
{
    // Arrange
    std::string testTextData {"Simple ASCII data"};
    std::string expectedResult {"<![CDATA[" + testTextData + "]]>"};

    // Act
    _writer.insertText(testTextData);

    // Assert
    EXPECT_EQ(expectedResult, _writer.getString());
}

/// If the data happens to actually include an XML CDATA close marker, that needs to be "escaped" --
/// this is done by breaking it up into two separate CDATA sections, splitting apart the marker.
TEST_F(WriterTest, insertTextNeedsEscape)
{
    // Arrange
    std::string testDataA {"ASCII data with a close marker in it, like so: ]]"};
    std::string testDataB {"> "};
    std::string expectedResult {"<![CDATA[" + testDataA + "]]><![CDATA[" + testDataB + "]]>"};

    // Act
    _writer.insertText(testDataA + testDataB);

    // Assert
    EXPECT_EQ(expectedResult, _writer.getString());
}

TEST_F(WriterTest, insertNonAsciiData)
{
    // Arrange
    std::string testData {"\x01\x02\x03\x04\u0001F450😀"};
    std::string expectedResult {"<![CDATA[" + testData + "]]>"};

    // Act
    _writer.insertText(testData);

    // Assert
    EXPECT_EQ(expectedResult, _writer.getString());
}

TEST_F(WriterTest, beginCharStream)
{
    // Arrange & Act
    auto& checkStream {_writer.beginCharStream()};

    // Assert
    EXPECT_TRUE(checkStream.good());
}

TEST_F(WriterTest, beginCharStreamTwice)
{
    // Arrange
    _writer.beginCharStream();

    // Act & Assert
    EXPECT_THROW(_writer.beginCharStream(), Base::RuntimeError);
}

TEST_F(WriterTest, endCharStream)
{
    // Arrange
    _writer.beginCharStream();

    // Act
    _writer.endCharStream();

    // Assert
    EXPECT_EQ("<![CDATA[]]>", _writer.getString());
}

TEST_F(WriterTest, endCharStreamTwice)
{
    // Arrange
    _writer.beginCharStream();
    _writer.endCharStream();

    // Act
    _writer.endCharStream();  // Doesn't throw, or do anything at all

    // Assert
    EXPECT_EQ("<![CDATA[]]>", _writer.getString());
}

TEST_F(WriterTest, charStream)
{
    // Arrange
    auto& streamA {_writer.beginCharStream()};

    // Act
    auto& streamB {_writer.charStream()};

    // Assert
    EXPECT_EQ(&streamA, &streamB);
}

TEST_F(WriterTest, charStreamBase64Encoded)
{
    // Arrange
    _writer.beginCharStream(Base::CharStreamFormat::Base64Encoded);
    std::string data {"FreeCAD rocks! 🪨🪨🪨"};

    // Act
    _writer.charStream() << data;
    _writer.endCharStream();

    // Assert
    // Conversion done using https://www.base64encode.org for testing purposes
    EXPECT_EQ(std::string("RnJlZUNBRCByb2NrcyEg8J+qqPCfqqjwn6qo\n"), _writer.getString());
}

// ---------------------------------------------------------------------------
// FileWriter writes real files, so these do not share the StringWriter fixture
// above. Both cases are about the path rather than the content: on Windows a
// narrow path is converted with the ANSI code page and stops at MAX_PATH, and
// FileWriter was the last narrow-path stream in Base.
// ---------------------------------------------------------------------------

class FileWriterTest: public ::testing::Test
{
protected:
    /// What is on disk under \a path, read back through Base::FileInfo -- the
    /// wide path on Windows, so a name this finds is the name the entry really
    /// has there rather than the one it was asked for.
    static std::string readEntry(const std::string& path)
    {
        Base::FileInfo info(path);
        Base::ifstream from(info, std::ios::in | std::ios::binary);
        std::ostringstream out;
        out << from.rdbuf();
        return out.str();
    }

    std::string entryPath(const std::string& name) const
    {
        return _dir.string() + "/" + name;
    }

    tests::TempDirectory _dir {"fcFileWriterTest"};
};

/// An entry is named after the object and property that own it, and an object
/// name only has to be a script identifier -- so the name reaches the writer as
/// UTF-8 and has to survive as itself. Converted with the ANSI code page it
/// lands on disk as mojibake, which nothing can then find by name again.
TEST_F(FileWriterTest, entryNameIsNotAscii)
{
    // Arrange: katakana, escaped because the sources here are ASCII.
    const std::string name {"\xe3\x83\x91\xe3\x83\xbc\xe3\x83\x84.txt"};
    Base::FileWriter writer(_dir.string().c_str());

    // Act
    writer.putNextEntry(name.c_str());
    writer.Stream() << "payload";
    writer.close();

    // Assert
    EXPECT_EQ("payload", readEntry(entryPath(name)));
}

/// The same names admit any length, so the path an entry lands on can pass the
/// 260 characters Windows stops at unless the process and the call both ask for
/// more. Each component stays inside the 255 bytes a file system takes.
TEST_F(FileWriterTest, entryPathPastMaxPath)
{
    // Arrange
    const std::string directory(150, 'd');
    const std::string name = directory + "/" + std::string(150, 'e') + ".txt";
    ASSERT_TRUE(Base::FileInfo(entryPath(directory)).createDirectory());
    ASSERT_GT(entryPath(name).size(), 260U);
    Base::FileWriter writer(_dir.string().c_str());

    // Act
    writer.putNextEntry(name.c_str());
    writer.Stream() << "payload";
    writer.close();

    // Assert
    EXPECT_EQ("payload", readEntry(entryPath(name)));
}
