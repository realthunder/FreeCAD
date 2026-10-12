// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

#include "Base/Exception.h"
#include "Base/Reader.h"
#include <src/TempDirectory.h>
#include <array>
#include <boost/filesystem.hpp>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <zipios++/zipinputstream.h>
#include <zipios++/zipoutputstream.h>

namespace fs = boost::filesystem;

class ReaderTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        XERCES_CPP_NAMESPACE_QUALIFIER XMLPlatformUtils::Initialize();
        // A directory of this fixture's own, not a fixed name in the shared
        // temp directory. gtest_discover_tests runs every case as a separate
        // process, so under "ctest -j" several of them are live at once and a
        // shared filename means one is writing the file, or removing it in
        // TearDown, while another is still reading it.
        _tempFile = fs::path(_tempDir.string()) / "unit_test_Reader.xml";
    }

    void TearDown() override
    {
        if (inputStream.is_open()) {
            inputStream.close();
        }
        // _tempDir removes itself, and the file with it.
    }

    void givenDataAsXMLStream(const std::string& data)
    {
        auto stringData =
            R"(<?xml version="1.0" encoding="UTF-8"?><document>)" + data + "</document>";
        std::istringstream stream(stringData);
        std::ofstream fileStream(_tempFile.string());
        fileStream.write(stringData.data(), static_cast<std::streamsize>(stringData.length()));
        fileStream.close();
        inputStream.open(_tempFile.string());
        _reader = std::make_unique<Base::XMLReader>(_tempFile.string().c_str(), inputStream);
    }

    Base::XMLReader* Reader()
    {
        return _reader.get();
    }

private:
    std::unique_ptr<Base::XMLReader> _reader;
    tests::TempDirectory _tempDir {"unit_test_Reader"};
    fs::path _tempFile;
    std::ifstream inputStream;
};

TEST_F(ReaderTest, beginCharStreamNormal)
{
    // Arrange
    givenDataAsXMLStream("<data>Test ASCII data</data>");
    Reader()->readElement("data");

    // Act
    auto& result = Reader()->beginCharStream();

    // Assert
    EXPECT_TRUE(result.good());
}

TEST_F(ReaderTest, beginCharStreamOpenClose)
{
    // Arrange
    givenDataAsXMLStream("<data id='12345' />");
    Reader()->readElement("data");

    // Act
    auto& result = Reader()->beginCharStream();  // Not an error, even though there is no data

    // Assert
    EXPECT_TRUE(result.good());
}

TEST_F(ReaderTest, beginCharStreamAlreadyBegun)
{
    // Arrange
    givenDataAsXMLStream("<data>Test ASCII data</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream();

    // Act & Assert
    EXPECT_THROW(Reader()->beginCharStream(), Base::XMLParseException);  // NOLINT
}

TEST_F(ReaderTest, charStreamGood)
{
    // Arrange
    givenDataAsXMLStream("<data>Test ASCII data</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream();

    // Act
    auto& result = Reader()->charStream();

    // Assert
    EXPECT_TRUE(result.good());
}

TEST_F(ReaderTest, charStreamBad)
{
    // Arrange
    givenDataAsXMLStream("<data>Test ASCII data</data>");
    Reader()->readElement("data");

    // Act & Assert
    EXPECT_THROW(Reader()->charStream(), Base::XMLParseException);  // NOLINT
}

TEST_F(ReaderTest, endCharStreamGood)
{
    // Arrange
    givenDataAsXMLStream("<data>Test ASCII data</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream();

    // Act & Assert
    Reader()->endCharStream();  // Does not throw
}

TEST_F(ReaderTest, endCharStreamBad)
{
    // Arrange
    givenDataAsXMLStream("<data>Test ASCII data</data>");
    Reader()->readElement("data");
    // Do not open the stream...

    // Act & Assert
    Reader()->endCharStream();  // Does not throw, even with no open stream
}

TEST_F(ReaderTest, readDataSmallerThanBuffer)
{
    // Arrange
    constexpr size_t bufferSize {20};
    std::string expectedData {"Test ASCII data"};
    givenDataAsXMLStream("<data>" + expectedData + "</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream();
    std::array<char, bufferSize> buffer {};

    // Act
    auto bytesRead = Reader()->read(buffer.data(), bufferSize);

    // Assert
    EXPECT_STREQ(expectedData.c_str(), buffer.data());
    EXPECT_EQ(expectedData.length(), bytesRead);
}

TEST_F(ReaderTest, readDataLargerThanBuffer)
{
    // Arrange
    constexpr size_t bufferSize {5};
    std::string expectedData {"Test ASCII data"};
    givenDataAsXMLStream("<data>" + expectedData + "</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream();
    std::array<char, bufferSize> buffer {};

    // Act
    auto bytesRead = Reader()->read(buffer.data(), bufferSize);

    // Assert
    for (size_t i = 0; i < bufferSize; ++i) {
        EXPECT_EQ(expectedData[i], buffer.at(i));
    }
    EXPECT_EQ(bufferSize, bytesRead);
}

TEST_F(ReaderTest, readDataLargerThanBufferSecondRead)
{
    // Arrange
    constexpr size_t bufferSize {5};
    std::string expectedData {"Test ASCII data"};
    givenDataAsXMLStream("<data>" + expectedData + "</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream();
    std::array<char, bufferSize> buffer {};
    Reader()->read(buffer.data(), bufferSize);  // Read the first five bytes

    // Act
    auto bytesRead = Reader()->read(buffer.data(), bufferSize);  // Second five bytes

    // Assert
    for (size_t i = 0; i < bufferSize; ++i) {
        EXPECT_EQ(expectedData[i + bufferSize], buffer.at(i));
    }
    EXPECT_EQ(bufferSize, bytesRead);
}

TEST_F(ReaderTest, readDataNotStarted)
{
    // Arrange
    constexpr size_t bufferSize {20};
    std::string expectedData {"Test ASCII data"};
    givenDataAsXMLStream("<data>" + expectedData + "</data>");
    Reader()->readElement("data");
    std::array<char, bufferSize> buffer {};

    // Act
    auto bytesRead = Reader()->read(buffer.data(), bufferSize);

    // Assert
    EXPECT_EQ(-1, bytesRead);  // Because we didn't call beginCharStream
}

TEST_F(ReaderTest, readNextStartElement)
{
    auto xmlBody = R"(
<node1 attr='1'>Node1</node1>
<node2 attr='2'>Node2</node2>
)";

    givenDataAsXMLStream(xmlBody);

    // start of document
    EXPECT_TRUE(Reader()->isStartOfDocument());
    Reader()->readElement("document");
    EXPECT_STREQ(Reader()->localName(), "document");

    // next element
    EXPECT_TRUE(Reader()->readNextElement());
    EXPECT_STREQ(Reader()->localName(), "node1");
    EXPECT_STREQ(Reader()->getAttribute("attr"), "1");
    Reader()->readEndElement("node1");
    EXPECT_TRUE(Reader()->isEndOfElement());

    // next element
    EXPECT_TRUE(Reader()->readNextElement());
    EXPECT_STREQ(Reader()->localName(), "node2");
    EXPECT_STREQ(Reader()->getAttribute("attr"), "2");
    Reader()->readEndElement("node2");
    EXPECT_TRUE(Reader()->isEndOfElement());
    Reader()->readEndElement("document");
    EXPECT_TRUE(Reader()->isEndOfDocument());
}

TEST_F(ReaderTest, readNextStartEndElement)
{
    auto xmlBody = R"(
<node1 attr='1'/>
<node2 attr='2'/>
)";

    givenDataAsXMLStream(xmlBody);

    // start of document
    EXPECT_TRUE(Reader()->isStartOfDocument());
    Reader()->readElement("document");
    EXPECT_STREQ(Reader()->localName(), "document");

    // next element
    EXPECT_TRUE(Reader()->readNextElement());
    EXPECT_STREQ(Reader()->localName(), "node1");
    EXPECT_STREQ(Reader()->getAttribute("attr"), "1");

    // next element
    EXPECT_TRUE(Reader()->readNextElement());
    EXPECT_STREQ(Reader()->localName(), "node2");
    EXPECT_STREQ(Reader()->getAttribute("attr"), "2");
    EXPECT_FALSE(Reader()->readNextElement());
    EXPECT_TRUE(Reader()->isEndOfDocument());
}

TEST_F(ReaderTest, charStreamBase64Encoded)
{
    // Arrange
    static constexpr size_t bufferSize {100};
    std::array<char, bufferSize> buffer {};
    givenDataAsXMLStream("<data>RnJlZUNBRCByb2NrcyEg8J+qqPCfqqjwn6qo\n</data>");
    Reader()->readElement("data");
    Reader()->beginCharStream(Base::CharStreamFormat::Base64Encoded);

    // Act
    Reader()->charStream().getline(buffer.data(), bufferSize);
    Reader()->endCharStream();

    // Assert
    // Conversion done using https://www.base64encode.org for testing purposes
    EXPECT_EQ(std::string("FreeCAD rocks! 🪨🪨🪨"), std::string(buffer.data()));
}

// The archive reader hands an entry stream a file handle that an earlier
// stream is done with (docs/HandsOnLog.md, entry 47). What that must not
// change: a stream reads its own entry whatever else is open, in any order,
// and after the reader that opened it is gone.
class ZipFileReaderTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        _archive = (fs::path(_tempDir.string()) / "entries.zip").string();
        zipios::ZipOutputStream zip(_archive);
        for (int i = 0; i < count; ++i) {
            zip.putNextEntry(name(i));
            const std::string data = content(i);
            zip.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        zip.close();
    }

    static std::string name(int i)
    {
        return fmt::format("entry{}.txt", i);
    }

    /// Long enough to be read in several turns, and different for every entry.
    static std::string content(int i)
    {
        std::string data;
        for (int line = 0; line < 400 + 7 * i; ++line) {
            data += fmt::format("entry {} line {}\n", i, line);
        }
        return data;
    }

    static std::string rest(std::istream& in)
    {
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    static constexpr int count {12};
    tests::TempDirectory _tempDir {"unit_test_ZipFileReader"};
    std::string _archive;
};

TEST_F(ZipFileReaderTest, everyEntryOneAfterAnother)
{
    Base::ZipFileReader reader(_archive);

    for (int turn = 0; turn < 3; ++turn) {
        for (int i = count - 1; i >= 0; --i) {
            auto stream = reader.openEntry(name(i));
            ASSERT_TRUE(stream);
            EXPECT_EQ(rest(*stream), content(i)) << name(i) << ", turn " << turn;
        }
    }
    EXPECT_FALSE(reader.openEntry("no such entry"));
}

TEST_F(ZipFileReaderTest, streamsAliveTogetherReadTheirOwnEntry)
{
    Base::ZipFileReader reader(_archive);
    auto a = reader.openEntry(name(3));
    auto b = reader.openEntry(name(7));
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);

    // A line of each in turn: either would lose its place on a shared handle.
    std::string gotA;
    std::string gotB;
    std::string line;
    bool more = true;
    while (more) {
        more = false;
        if (std::getline(*a, line)) {
            gotA += line + "\n";
            more = true;
        }
        if (std::getline(*b, line)) {
            gotB += line + "\n";
            more = true;
        }
    }

    EXPECT_EQ(gotA, content(3));
    EXPECT_EQ(gotB, content(7));
}

TEST_F(ZipFileReaderTest, aStreamLeftHalfReadDoesNotSpoilTheNext)
{
    Base::ZipFileReader reader(_archive);
    {
        auto half = reader.openEntry(name(5));
        ASSERT_TRUE(half);
        std::string line;
        std::getline(*half, line);
        EXPECT_EQ(line, "entry 5 line 0");
    }
    {
        auto past = reader.openEntry(name(count - 1));
        ASSERT_TRUE(past);
        EXPECT_EQ(rest(*past), content(count - 1));
        // read to the end and past it: the handle goes back at the end of the entry
        EXPECT_EQ(past->peek(), std::char_traits<char>::eof());
    }

    auto next = reader.openEntry(name(0));
    ASSERT_TRUE(next);
    EXPECT_EQ(rest(*next), content(0));
}

TEST_F(ZipFileReaderTest, aStreamOutlivesItsReader)
{
    std::unique_ptr<std::istream> stream;
    {
        Base::ZipFileReader reader(_archive);
        stream = reader.openEntry(name(9));
    }

    ASSERT_TRUE(stream);
    EXPECT_EQ(rest(*stream), content(9));
}
