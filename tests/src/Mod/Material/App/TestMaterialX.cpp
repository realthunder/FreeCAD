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
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <App/Application.h>
#include <App/MaterialAppearance.h>
#include <App/MaterialXDocument.h>
#include <Base/Color.h>
#include <Gui/MetaTypes.h>
#include <src/App/InitApplication.h>
#include <src/TempDirectory.h>

#include <Mod/Material/App/MaterialLibrary.h>
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

TEST_F(TestMaterialX, savingToALibraryCopiesTheFilesUnderMaterialxAndRelinksThem)
{
    // The author picked the files where they were, which is nowhere near a
    // library: two directories apart, one name with a space in it
    QDir(_root).mkpath(QStringLiteral("picked/Images"));
    const QString mtlx = QDir(_root).filePath(QStringLiteral("picked/brass.mtlx"));
    const QString image = QDir(_root).filePath(QStringLiteral("picked/Images/brass color.jpg"));
    for (const auto& [path, content] : {std::pair {mtlx, "<materialx version=\"1.39\"/>"},
                                        std::pair {image, "PIXELS"}}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }
    auto card = libraryCard();
    auto files = std::make_shared<QList<QVariant>>();
    files->append(mtlx);
    files->append(image);
    card->setAppearanceValue(QStringLiteral("MaterialXFiles"), files);
    card->resolveMaterialXFiles(QString());
    const auto hashes = card->getMaterialXHashes();
    ASSERT_EQ(hashes.size(), 2u);
    ASSERT_FALSE(hashes[1].empty());

    const QString libraryRoot = QDir(_root).filePath(QStringLiteral("library"));
    QDir(libraryRoot).mkpath(QStringLiteral("."));
    auto library = std::make_shared<Materials::MaterialLibraryLocal>(
        QStringLiteral("Testing"), libraryRoot, QStringLiteral(":/icons/preferences-general.svg"), false);
    auto saved = library->saveMaterial(card, QStringLiteral("Metals/Brass.FCMat"), true, false, false);
    ASSERT_TRUE(saved);

    // The files sit beside the card's own place in the tree, under the
    // stated names' own file names, and the list now says so relative to
    // materialx/. The names and the hashes did not move.
    const QDir placed(QDir(libraryRoot).filePath(QStringLiteral("materialx/Metals/Brass")));
    EXPECT_TRUE(QFileInfo::exists(placed.filePath(QStringLiteral("brass.mtlx"))));
    EXPECT_TRUE(QFileInfo::exists(placed.filePath(QStringLiteral("brass color.jpg"))));
    EXPECT_EQ(card->getMaterialXFiles(),
              (QStringList {QStringLiteral("Metals/Brass/brass.mtlx"),
                            QStringLiteral("Metals/Brass/brass color.jpg")}));
    EXPECT_EQ(card->getMaterialXNames(),
              (QStringList {QStringLiteral("brass.mtlx"), QStringLiteral("../Images/brass color.jpg")}));
    EXPECT_EQ(card->getMaterialXHashes(), hashes);
    EXPECT_EQ(card->getMaterialXPaths()[1], placed.filePath(QStringLiteral("brass color.jpg")).toStdString());

    // What was written is the relative form, and reading the card back off
    // the library hashes to the same identity
    QFile written(QDir(libraryRoot).filePath(QStringLiteral("Metals/Brass.FCMat")));
    ASSERT_TRUE(written.open(QIODevice::ReadOnly));
    const QString yaml = QString::fromUtf8(written.readAll());
    EXPECT_TRUE(yaml.contains(QStringLiteral("Metals/Brass/brass color.jpg"))) << yaml.toStdString();
    EXPECT_FALSE(yaml.contains(QStringLiteral("picked/"))) << yaml.toStdString();
    auto again = libraryCard();
    again->setAppearanceValue(QStringLiteral("MaterialXFiles"),
                              std::make_shared<QList<QVariant>>(QList<QVariant> {
                                  QStringLiteral("Metals/Brass/brass.mtlx"),
                                  QStringLiteral("Metals/Brass/brass color.jpg")}));
    again->resolveMaterialXFiles(libraryRoot);
    EXPECT_EQ(again->getMaterialXHashes(), hashes);

    // Saving again in place copies nothing and changes nothing
    auto twice = library->saveMaterial(card, QStringLiteral("Metals/Brass.FCMat"), true, false, false);
    ASSERT_TRUE(twice);
    EXPECT_EQ(card->getMaterialXFiles().size(), 2);
    EXPECT_EQ(card->getMaterialXHashes(), hashes);
}

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

TEST_F(TestMaterialX, aCardStatingNoColourWearsTheDefaultLook)
{
    // A look's card says which shader graph shades the surface and nothing
    // else. The model inherits the colour slots, so the card HAS them, with
    // no value in any -- and reading an empty slot as a stated colour gave
    // three floats nobody had written: every dressed object came up green.
    auto card = std::make_shared<Materials::Material>();
    card->addAppearance(Materials::ModelUUIDs::ModelUUID_Rendering_MaterialX);
    card->setAppearanceValue(QStringLiteral("MaterialXShaderGraph"), QStringLiteral("brass.mtlx"));
    ASSERT_TRUE(card->hasAppearanceProperty(QStringLiteral("DiffuseColor")));
    auto diffuse = card->getAppearanceProperty(QStringLiteral("DiffuseColor"));
    ASSERT_TRUE(diffuse->isNull());
    // The slot itself reads as a defined colour, whatever a caller does
    EXPECT_EQ(diffuse->getColor(), Base::Color());

    const App::MaterialAppearance look = card->getMaterialAppearance();
    const App::MaterialAppearance stock(App::MaterialAppearance::DEFAULT);
    EXPECT_EQ(look.ambientColor, stock.ambientColor);
    EXPECT_EQ(look.diffuseColor, stock.diffuseColor);
    EXPECT_EQ(look.specularColor, stock.specularColor);
    EXPECT_EQ(look.emissiveColor, stock.emissiveColor);
    EXPECT_FLOAT_EQ(look.shininess, stock.shininess);
    EXPECT_FLOAT_EQ(look.transparency, stock.transparency);
    // Still the card's own appearance: it is what the object wears
    EXPECT_EQ(look.getType(), App::MaterialAppearance::USER_DEFINED);
    EXPECT_EQ(look.uuid, card->getUUID().toStdString());
    EXPECT_NE(look, App::MaterialAppearance());

    // A stated colour is still the card's word over the default
    card->setAppearanceValue(QStringLiteral("DiffuseColor"), QStringLiteral("(0.1, 0.2, 0.3, 1.0)"));
    EXPECT_EQ(card->getMaterialAppearance().diffuseColor, Base::Color(0.1F, 0.2F, 0.3F, 1.0F));
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
