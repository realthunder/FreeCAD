// SPDX-License-Identifier: LGPL-2.1-or-later

/** The document version gate, and the one question asked of it so far.
 *
 * Upstream inverted the meaning of a colour's alpha component at 1.1: before
 * that release it held transparency, and from 1.1 it holds opacity. This fork
 * kept the older convention, so it has to convert documents from AFTER the
 * change -- the opposite direction to upstream's own migration, which is why
 * their gate cannot simply be reused (docs/ShapeAppearanceDesign.md 7.9).
 *
 * What matters most here is the answer for a version string nobody
 * recognises. Theirs is "newer than everything", which is safe for a test
 * spelled "older than 1.1" and silently wrong for one spelled "1.1 or
 * later": it would invert every colour in the oldest documents there are.
 */

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include <xercesc/util/PlatformUtils.hpp>

#include <Base/ProgramVersion.h>
#include <Base/Reader.h>

TEST(ProgramVersion, releaseNumberIsTheLeadingMajorMinor)
{
    EXPECT_EQ(Base::getReleaseNumber("1.1R41234").major, 1);
    EXPECT_EQ(Base::getReleaseNumber("1.1R41234").minor, 1);
    EXPECT_EQ(Base::getReleaseNumber("0.22R38472 (Git)").major, 0);
    EXPECT_EQ(Base::getReleaseNumber("0.22R38472 (Git)").minor, 22);
    EXPECT_EQ(Base::getReleaseNumber("10.4").major, 10);
    EXPECT_EQ(Base::getReleaseNumber("10.4").minor, 4);
}

TEST(ProgramVersion, aStringThatStatesNoReleaseIsZero)
{
    // 'pre-0.14' is what a reader fills in for a document carrying no
    // ProgramVersion attribute at all, and it must not read as a release.
    EXPECT_EQ(Base::getReleaseNumber("pre-0.14").major, 0);
    EXPECT_EQ(Base::getReleaseNumber("pre-0.14").minor, 0);
    EXPECT_EQ(Base::getReleaseNumber("").major, 0);
    EXPECT_EQ(Base::getReleaseNumber("1").minor, 0);  // no minor stated
    EXPECT_EQ(Base::getReleaseNumber("1").major, 0);
}

TEST(ProgramVersion, alphaIsOpacityFromEleven)
{
    EXPECT_TRUE(Base::alphaIsOpacity("1.1R41234"));
    EXPECT_TRUE(Base::alphaIsOpacity("1.2"));
    EXPECT_TRUE(Base::alphaIsOpacity("2.0R1"));
    EXPECT_TRUE(Base::alphaIsOpacity("1.11"));
}

TEST(ProgramVersion, alphaIsTransparencyBeforeEleven)
{
    EXPECT_FALSE(Base::alphaIsOpacity("1.0R38472"));
    EXPECT_FALSE(Base::alphaIsOpacity("0.22R38472"));
    EXPECT_FALSE(Base::alphaIsOpacity("0.19"));
}

TEST(ProgramVersion, anUnreadableVersionIsNotTreatedAsNewer)
{
    // The whole point of not reusing upstream's table: each of these would
    // classify as post-1.1 there.
    EXPECT_FALSE(Base::alphaIsOpacity("pre-0.14"));
    EXPECT_FALSE(Base::alphaIsOpacity(""));
    EXPECT_FALSE(Base::alphaIsOpacity("unknown"));
}

TEST(ProgramVersion, thisBuildStillWritesTheLegacyBytes)
{
    // The documents this build writes state its own release, which is below
    // 1.1, so their colours must go out in the legacy convention -- alpha as
    // transparency -- for that statement to stay true. The day the fork
    // calls itself 1.1 this flips on its own and the writing conversion
    // stops with it; this test failing IS that day's checklist.
    EXPECT_FALSE(Base::writerAlphaIsOpacity());
}

TEST(ProgramVersion, upstreamsTableStillAnswersTheirWay)
{
    // Ported unchanged, so their version-gated code compiles here. Kept
    // honest about the answer that made it unusable for the test above.
    EXPECT_EQ(Base::getVersion("1.1R41234"), Base::Version::v1_1);
    EXPECT_EQ(Base::getVersion("0.22R1"), Base::Version::v0_22);
    EXPECT_EQ(Base::getVersion("0.13"), Base::Version::v0_1x);
    EXPECT_EQ(Base::getVersion("pre-0.14"), Base::Version::v1_x);
}

namespace
{

/// A parser over a trivial document, which is all these overloads read from
std::unique_ptr<Base::XMLReader> parserAt(std::istream &stream, const char *version)
{
    auto reader = std::make_unique<Base::XMLReader>("Document.xml", stream);
    reader->ProgramVersion = version;
    return reader;
}

const char *EMPTY_DOC = R"(<?xml version="1.0" encoding="UTF-8"?><document/>)";

}  // namespace

/// Building an XMLReader parses, so these need Xerces up; the plain-string
/// tests above do not.
class ProgramVersionReaderTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        XERCES_CPP_NAMESPACE_QUALIFIER XMLPlatformUtils::Initialize();
    }
};

TEST_F(ProgramVersionReaderTest, aParserAnswersForItsDocument)
{
    std::istringstream xml(EMPTY_DOC);
    auto reader = parserAt(xml, "1.1R41234");
    EXPECT_TRUE(Base::alphaIsOpacity(*reader));

    std::istringstream xml2(EMPTY_DOC);
    auto older = parserAt(xml2, "0.22R1");
    EXPECT_FALSE(Base::alphaIsOpacity(*older));
}

TEST_F(ProgramVersionReaderTest, anArchiveEntryAsksTheParserThatRegisteredIt)
{
    // A property whose values live in their own entry is read after the XML
    // pass, from a reader that holds no version of its own.
    std::istringstream xml(EMPTY_DOC);
    auto parser = parserAt(xml, "1.1R41234");

    std::istringstream bytes("whatever");
    Base::Reader entry(bytes, "Appearance.bin", parser.get());
    EXPECT_TRUE(Base::alphaIsOpacity(entry));
}

TEST(ProgramVersion, anEntryWithNoParserConvertsNothing)
{
    std::istringstream bytes("whatever");
    Base::Reader orphan(bytes, "Appearance.bin");
    EXPECT_FALSE(Base::alphaIsOpacity(orphan));
}
