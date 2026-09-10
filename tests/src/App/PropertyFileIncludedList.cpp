// SPDX-License-Identifier: LGPL-2.1-or-later

/** A named set of files stored as shared blobs (App::PropertyFileIncludedList).
 *
 * The type exists so that content referring to its files BY NAME -- a
 * MaterialX document naming its maps -- can travel in a document and
 * still be opened on another machine. So what is tested is exactly
 * that: the names and the bytes both survive a save and a reopen, one
 * file named twice costs one file, and a name that resolves to nothing
 * says so rather than handing back a path that cannot be opened.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyFile.h>
#include <Base/FileInfo.h>

#include <src/App/InitApplication.h>

// In namespace App because the type system reads a registered name as
// <module>::<class> and tries to import the module for anything else.
namespace App
{

/// An object carrying nothing but the property under test.
class TestIncludedFilesFeature: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(TestIncludedFilesFeature);

public:
    TestIncludedFilesFeature()
    {
        ADD_PROPERTY_TYPE(Files, (), "Test", App::Prop_None, "a set of included files");
    }
    App::PropertyFileIncludedList Files;

    const char* getViewProviderName() const override
    {
        return "";
    }
};

}  // namespace App

PROPERTY_SOURCE(App::TestIncludedFilesFeature, App::DocumentObject)

namespace
{

using App::TestIncludedFilesFeature;

class PropertyFileIncludedListTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        TestIncludedFilesFeature::init();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("included");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _obj = static_cast<TestIncludedFilesFeature*>(
            _doc->addObject("App::TestIncludedFilesFeature", "obj"));
    }

    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        for (const auto& path : _scratch) {
            Base::FileInfo fi(path);
            if (fi.exists()) {
                try {
                    fi.deleteFile();
                }
                catch (...) {
                }
            }
        }
        _scratch.clear();
    }

    /// A scratch file holding \a content, cleaned up with the test.
    std::string writeScratch(const std::string& content, const char* suffix)
    {
        std::string path = Base::FileInfo::getTempFileName();
        path += suffix;
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << content;
        }
        _scratch.push_back(path);
        return path;
    }

    static std::string readWhole(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

    /// Save this document, close it, and open it again.
    App::Document* roundTrip()
    {
        if (_file.empty()) {
            _file = Base::FileInfo::getTempFileName();
            _file += ".FCStd";
            _scratch.push_back(_file);
        }
        EXPECT_TRUE(_doc->saveAs(_file.c_str()));
        App::GetApplication().closeDocument(_doc->getName());
        _doc = App::GetApplication().openDocument(_file.c_str(), false);
        _obj = static_cast<TestIncludedFilesFeature*>(_doc->getObject("obj"));
        return _doc;
    }

    std::string _docName;
    std::string _file;
    App::Document* _doc {nullptr};
    TestIncludedFilesFeature* _obj {nullptr};
    std::vector<std::string> _scratch;
};

TEST_F(PropertyFileIncludedListTest, twoFilesTravelUnderTheirOwnNames)
{
    _obj->Files.setFile("brass_color.png", writeScratch("COLOR-BYTES", ".png").c_str());
    _obj->Files.setFile("brass_rough.png", writeScratch("ROUGH-BYTES", ".png").c_str());
    ASSERT_EQ(_obj->Files.getSize(), 2);

    roundTrip();

    ASSERT_EQ(_obj->Files.getSize(), 2);
    // The name is the key a document asks with, and the path it answers
    // has to be openable on the machine that opened the document.
    const std::string color = _obj->Files.filePath("brass_color.png");
    const std::string rough = _obj->Files.filePath("brass_rough.png");
    ASSERT_FALSE(color.empty());
    ASSERT_FALSE(rough.empty());
    EXPECT_EQ(readWhole(color), "COLOR-BYTES");
    EXPECT_EQ(readWhole(rough), "ROUGH-BYTES");
}

TEST_F(PropertyFileIncludedListTest, oneFileNamedTwiceIsStoredOnce)
{
    const std::string path = writeScratch("SHARED-BYTES", ".png");
    _obj->Files.setFile("base.png", path.c_str());
    _obj->Files.setFile("also_base.png", path.c_str());

    // Sharing is what the store is for: same bytes, one file, whatever
    // each entry calls it.
    ASSERT_NE(_obj->Files.find("base.png"), nullptr);
    ASSERT_NE(_obj->Files.find("also_base.png"), nullptr);
    EXPECT_EQ(_obj->Files.find("base.png")->blob, _obj->Files.find("also_base.png")->blob);

    roundTrip();

    ASSERT_NE(_obj->Files.find("base.png"), nullptr);
    ASSERT_NE(_obj->Files.find("also_base.png"), nullptr);
    // And the restore must not undo it: one archive entry serves both
    // entries, which is the case assignRestoredBlob dispatches by hash.
    EXPECT_EQ(_obj->Files.find("base.png")->blob, _obj->Files.find("also_base.png")->blob);
    EXPECT_EQ(readWhole(_obj->Files.filePath("also_base.png")), "SHARED-BYTES");
}

TEST_F(PropertyFileIncludedListTest, aNameThatResolvesToNothingSaysSo)
{
    _obj->Files.setFile("there.png", writeScratch("BYTES", ".png").c_str());

    // Not a path that cannot be opened, and not the first file either:
    // a consumer joins on the name and must be told when there is no
    // answer.
    EXPECT_EQ(_obj->Files.filePath("absent.png"), std::string());
    EXPECT_EQ(_obj->Files.find("absent.png"), nullptr);
    EXPECT_FALSE(_obj->Files.filePath("there.png").empty());
}

TEST_F(PropertyFileIncludedListTest, aSetOfFilesHasNoSpellingWithoutTheStore)
{
    EXPECT_FALSE(_obj->Files.blobContentNeedsStore());
    _obj->Files.setFile("map.png", writeScratch("BYTES", ".png").c_str());
    // There is no pre-store form of a set of files, so the save has to be
    // offered the schema that keeps it rather than dropping it silently.
    EXPECT_TRUE(_obj->Files.blobContentNeedsStore());
}

TEST_F(PropertyFileIncludedListTest, removingAndClearingLeaveTheRest)
{
    _obj->Files.setFile("a.png", writeScratch("A", ".png").c_str());
    _obj->Files.setFile("b.png", writeScratch("B", ".png").c_str());

    _obj->Files.removeFile("a.png");
    EXPECT_EQ(_obj->Files.getSize(), 1);
    EXPECT_EQ(_obj->Files.find("a.png"), nullptr);
    EXPECT_EQ(readWhole(_obj->Files.filePath("b.png")), "B");

    // Removing what is not there is not an error: a consumer syncing a
    // set against a document does it on every pass.
    _obj->Files.removeFile("a.png");
    EXPECT_EQ(_obj->Files.getSize(), 1);

    _obj->Files.clear();
    EXPECT_TRUE(_obj->Files.isEmpty());
}

TEST_F(PropertyFileIncludedListTest, replacingANameKeepsThePosition)
{
    _obj->Files.setFile("a.png", writeScratch("A", ".png").c_str());
    _obj->Files.setFile("b.png", writeScratch("B", ".png").c_str());
    _obj->Files.setFile("a.png", writeScratch("A-EDITED", ".png").c_str());

    // A name is a key, so writing it again replaces rather than appends;
    // the order is the one the entries were first added in.
    ASSERT_EQ(_obj->Files.getSize(), 2);
    EXPECT_EQ(_obj->Files.getValues()[0].name, "a.png");
    EXPECT_EQ(readWhole(_obj->Files.filePath("a.png")), "A-EDITED");
}

TEST_F(PropertyFileIncludedListTest, provenanceIsNotPartOfTheValue)
{
    const std::string path = writeScratch("BYTES", ".png");
    _obj->Files.setFile("map.png", path.c_str(), "/somewhere/else/map.png");

    App::PropertyFileIncludedList other;
    other.setFile("map.png", path.c_str(), "/read/from/here/map.png");

    // Two properties holding the same bytes under the same names are the
    // same value however differently the files were once reached.
    EXPECT_TRUE(_obj->Files.isSame(other));
    EXPECT_EQ(_obj->Files.find("map.png")->original, std::string("/somewhere/else/map.png"));
}

TEST_F(PropertyFileIncludedListTest, aCopySharesTheFilesRatherThanTheBytes)
{
    _obj->Files.setFile("map.png", writeScratch("BYTES", ".png").c_str());

    std::unique_ptr<App::Property> copy(_obj->Files.Copy());
    auto* copied = static_cast<App::PropertyFileIncludedList*>(copy.get());
    // What an undo snapshot costs: a reference each, not the pixels.
    ASSERT_NE(copied->find("map.png"), nullptr);
    EXPECT_EQ(copied->find("map.png")->blob, _obj->Files.find("map.png")->blob);
}

TEST_F(PropertyFileIncludedListTest, aPastedEntryStillWaitingForContentGetsItFromTheStore)
{
    _obj->Files.setFile("map.png", writeScratch("BYTES", ".png").c_str());
    const std::string hash = _obj->Files.find("map.png")->hash;
    // What an undo snapshot taken while a restore was still pending looks
    // like: the name and the hash, and no handle behind them. Pasting it
    // back must not leave the entry that way -- a save would write the
    // hash without ever writing the content.
    std::vector<App::PropertyFileIncludedList::Entry> pending(1);
    pending[0].name = "map.png";
    pending[0].hash = hash;
    _obj->Files.setValues(pending);
    ASSERT_NE(_obj->Files.find("map.png"), nullptr);
    EXPECT_TRUE(_obj->Files.find("map.png")->blob != nullptr);
    EXPECT_EQ(readWhole(_obj->Files.filePath("map.png")), "BYTES");
}

}  // namespace
