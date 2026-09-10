// SPDX-License-Identifier: LGPL-2.1-or-later
#include <gtest/gtest.h>

#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/FileSet.h>
#include <App/MaterialXDocument.h>
#include <Base/FileInfo.h>
#include <fstream>
#include <src/App/InitApplication.h>

namespace
{

class MaterialXDocumentTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }
    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("manifest", "testUser");
    }
    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
        }
        for (const auto& path : _scratch) {
            Base::FileInfo fi(path);
            if (fi.exists()) {
                fi.deleteFile();
            }
        }
    }
    std::string writeScratch(const std::string& content, const char* suffix)
    {
        std::string path = Base::FileInfo::getTempFileName();
        path += suffix;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << content;
        _scratch.push_back(path);
        return path;
    }
    App::Document* _doc {nullptr};
    std::vector<std::string> _scratch;
};

}  // namespace

TEST_F(MaterialXDocumentTest, theTextFormRoundTripsAndNamesFilesWithSpaces)
{
    App::MaterialXDocument manifest;
    manifest.document = "brushed aluminium.mtlx";
    manifest.files.push_back({"brushed aluminium.mtlx", "1111111111111111111111111111111111111111"});
    manifest.files.push_back({"../Images/brass color.jpg", "2222222222222222222222222222222222222222"});
    App::MaterialXDocument back;
    ASSERT_TRUE(App::MaterialXDocument::read(manifest.write(), back));
    EXPECT_EQ(back, manifest);
    EXPECT_EQ(back.documentHash(), "1111111111111111111111111111111111111111");
    ASSERT_EQ(back.hashes().size(), 2u);
    // Not a manifest: refused rather than read as an empty one
    EXPECT_FALSE(App::MaterialXDocument::read("<materialx/>", back));
    EXPECT_FALSE(back.isSet());
}

TEST_F(MaterialXDocumentTest, theIdentityIsAFunctionOfNamesAndHashesAlone)
{
    App::MaterialXDocument a;
    a.document = "doc.mtlx";
    a.files.push_back({"doc.mtlx", "aaaa"});
    a.files.push_back({"map.png", "bbbb"});
    App::MaterialXDocument b = a;
    EXPECT_EQ(a.manifestHash(), b.manifestHash());
    // A changed map is a changed identity, transitively (sec 17.6)
    b.files[1].hash = "cccc";
    EXPECT_NE(a.manifestHash(), b.manifestHash());
    // And storing it yields a blob whose hash IS that identity
    auto blob = a.store(_doc->getFileBlobManager());
    ASSERT_TRUE(blob);
    EXPECT_EQ(blob->hash(), a.manifestHash());
    App::MaterialXDocument stored;
    ASSERT_TRUE(App::MaterialXDocument::readFile(blob->path(), stored));
    EXPECT_EQ(stored, a);
}

TEST_F(MaterialXDocumentTest, aSetOfStoredFilesBecomesAManifest)
{
    App::FileSet files;
    auto& manager = _doc->getFileBlobManager();
    files.setFile(manager, "doc.mtlx", writeScratch("<materialx/>", ".mtlx").c_str());
    files.setFile(manager, "map.png", writeScratch("PIXELS", ".png").c_str());
    auto manifest = App::MaterialXDocument::fromFileSet(files, "doc.mtlx");
    ASSERT_EQ(manifest.files.size(), 2u);
    EXPECT_EQ(manifest.documentHash(), files.find("doc.mtlx")->hash);
    EXPECT_EQ(manifest.find("map.png")->hash, files.find("map.png")->hash);
}

TEST_F(MaterialXDocumentTest, aSurfaceNameRidesInTheManifestAndIsPartOfTheIdentity)
{
    // Fifteen materials over one shared set of files is fifteen cards
    // told apart by this and nothing else (sec 17.13)
    App::MaterialXDocument bishop;
    bishop.document = "chess_set.mtlx";
    bishop.surface = "M_Bishop_B";
    bishop.files.push_back({"chess_set.mtlx", "aaaa"});
    bishop.files.push_back({"bishop_black_base_color.jpg", "bbbb"});
    App::MaterialXDocument king = bishop;
    king.surface = "M_King_B";
    EXPECT_NE(bishop.manifestHash(), king.manifestHash());
    EXPECT_NE(bishop, king);

    App::MaterialXDocument back;
    ASSERT_TRUE(App::MaterialXDocument::read(bishop.write(), back));
    EXPECT_EQ(back.surface, "M_Bishop_B");
    EXPECT_EQ(back, bishop);

    // A manifest that wears the document's first surface writes exactly
    // the bytes it wrote before the key existed, so no stored card's
    // identity moved when it was added
    App::MaterialXDocument first = bishop;
    first.surface.clear();
    EXPECT_EQ(first.write().find("surface "), std::string::npos);

    // And a reader that does not know a key steps over it: the same
    // forward compatibility the surface line itself relies on
    std::string text = bishop.write();
    text += "look L_ChessSet\n";
    App::MaterialXDocument newer;
    ASSERT_TRUE(App::MaterialXDocument::read(text, newer));
    EXPECT_EQ(newer, bishop);
}

TEST_F(MaterialXDocumentTest, aFileSetCanNameTheSurfaceItIsWorn_By)
{
    App::FileSet files;
    auto& manager = _doc->getFileBlobManager();
    files.setFile(manager, "doc.mtlx", writeScratch("<materialx/>", ".mtlx").c_str());
    auto manifest = App::MaterialXDocument::fromFileSet(files, "doc.mtlx", "M_Queen_W");
    EXPECT_EQ(manifest.surface, "M_Queen_W");
    EXPECT_EQ(App::MaterialXDocument::fromFileSet(files, "doc.mtlx").surface, "");
}
