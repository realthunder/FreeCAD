// SPDX-License-Identifier: LGPL-2.1-or-later
/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei <realthunder.dev@gmail.com>              *
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

#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <App/Application.h>
#include <App/MaterialXDocument.h>
#include <Gui/MetaTypes.h>
#include <src/App/InitApplication.h>
#include <src/TempDirectory.h>

#include <Mod/Material/App/MaterialLoader.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/Materials.h>
#include <Mod/Material/App/ModelUuids.h>

// The MaterialX document set a card carries (docs/MaterialStorage.md sec
// 17): stated by name in the library, identified by content everywhere.

namespace
{
// A bundled appearance card to grow the model on.
const char* Steel = "92589471-a6cb-4bbc-b748-d425a17dea7d";
}  // namespace

class TestMaterialX : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (App::Application::GetARGC() == 0) {
            tests::initApplication();
        }
    }
    void SetUp() override
    {
        _root = QString::fromStdString(_tempDir.string());
        QDir(_root).mkpath(QStringLiteral("materialx/brass"));
    }
    /// A file under the library's materialx/ directory, as a card author
    /// would put it there
    void writeLibraryFile(const char* relative, const char* content)
    {
        QFile file(QDir(_root).filePath(QStringLiteral("materialx/") + QString::fromLatin1(relative)));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }
    /// A copy of Steel wearing the MaterialX model in its library form
    std::shared_ptr<Materials::Material> libraryCard()
    {
        auto steel = Materials::MaterialManager::getManager().getMaterial(QString::fromLatin1(Steel));
        auto card = std::make_shared<Materials::Material>(*steel);
        card->addAppearance(Materials::ModelUUIDs::ModelUUID_Rendering_MaterialX);
        card->setAppearanceValue(QStringLiteral("MaterialXShaderGraph"), QStringLiteral("brass.mtlx"));
        auto names = std::make_shared<QList<QVariant>>();
        names->append(QStringLiteral("brass.mtlx"));
        names->append(QStringLiteral("../Images/brass color.jpg"));
        card->setAppearanceValue(QStringLiteral("MaterialXNames"), names);
        auto files = std::make_shared<QList<QVariant>>();
        files->append(QStringLiteral("brass/brass.mtlx"));
        files->append(QStringLiteral("brass/brass_color.jpg"));
        card->setAppearanceValue(QStringLiteral("MaterialXFiles"), files);
        return card;
    }
    tests::TempDirectory _tempDir {"TestMaterialX"};
    QString _root;
};

TEST_F(TestMaterialX, aLibraryCardIsHashedOffItsFilesAndIdentifiedByContent)
{
    writeLibraryFile("brass/brass.mtlx", "<materialx version=\"1.39\"/>");
    writeLibraryFile("brass/brass_color.jpg", "PIXELS");
    auto card = libraryCard();
    ASSERT_TRUE(card->hasMaterialX());
    card->resolveMaterialXFiles(_root);
    ASSERT_EQ(card->getMaterialXHashes().size(), 2u);
    EXPECT_FALSE(card->getMaterialXHashes()[0].empty());
    EXPECT_FALSE(card->getMaterialXHashes()[1].empty());
    // The manifest names the document and its map by content, and its hash
    // is what the card's appearance carries
    auto manifest = card->getMaterialXManifest();
    ASSERT_TRUE(manifest.isSet());
    EXPECT_EQ(manifest.document, "brass.mtlx");
    EXPECT_EQ(manifest.documentHash(), card->getMaterialXHashes()[0]);
    EXPECT_EQ(card->getMaterialAppearance().materialx, manifest.manifestHash());
    // The canonical form says the hashes and NOT where the library keeps
    // the files (17.6): a card moved to another library is the same card
    const QString canonical = card->getCanonicalForm();
    EXPECT_NE(canonical.indexOf(QStringLiteral("MaterialX:")), -1) << canonical.toStdString();
    EXPECT_NE(canonical.indexOf(QString::fromStdString(manifest.documentHash())), -1);
    EXPECT_EQ(canonical.indexOf(QStringLiteral("brass/brass_color.jpg")), -1) << canonical.toStdString();
    EXPECT_EQ(canonical.indexOf(QStringLiteral("MaterialXFiles")), -1) << canonical.toStdString();
    // Edit a map in place and the identity moves with it
    const std::string before = card->getContentHash();
    writeLibraryFile("brass/brass_color.jpg", "OTHER PIXELS");
    card->resolveMaterialXFiles(_root);
    EXPECT_NE(card->getContentHash(), before);
    EXPECT_NE(card->getMaterialAppearance().materialx, manifest.manifestHash());
}

TEST_F(TestMaterialX, aMissingFileLeavesTheLookToTheColourSlots)
{
    writeLibraryFile("brass/brass.mtlx", "<materialx version=\"1.39\"/>");
    auto card = libraryCard();
    card->resolveMaterialXFiles(_root);
    // One of two files is not there: no manifest, no hash on the look --
    // half a manifest would be a different identity altogether
    EXPECT_FALSE(card->getMaterialXManifest().isSet());
    EXPECT_TRUE(card->getMaterialAppearance().materialx.empty());
}

TEST_F(TestMaterialX, aCardStoredInADocumentReadsItsHashesBack)
{
    writeLibraryFile("brass/brass.mtlx", "<materialx version=\"1.39\"/>");
    writeLibraryFile("brass/brass_color.jpg", "PIXELS");
    auto card = libraryCard();
    card->resolveMaterialXFiles(_root);
    // What a document's blob holds: the canonical form, and nothing else
    const QString path = QDir(_root).filePath(QStringLiteral("stored.FCMat"));
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(card->getCanonicalForm().toUtf8());
    }
    auto stored = Materials::MaterialLoader::getMaterialFromFile(path);
    ASSERT_TRUE(stored);
    ASSERT_TRUE(stored->hasMaterialX());
    EXPECT_EQ(stored->getMaterialXShaderGraph(), QStringLiteral("brass.mtlx"));
    EXPECT_EQ(stored->getMaterialXNames(), card->getMaterialXNames());
    EXPECT_EQ(stored->getMaterialXHashes(), card->getMaterialXHashes());
    // No paths: the bytes are the document's, not this machine's
    EXPECT_TRUE(stored->getMaterialXFiles().isEmpty());
    for (const auto& p : stored->getMaterialXPaths()) {
        EXPECT_TRUE(p.empty());
    }
    // And it is the same card
    EXPECT_EQ(stored->getContentHash(), card->getContentHash());
    EXPECT_EQ(stored->getMaterialAppearance().materialx, card->getMaterialAppearance().materialx);
}
