// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"
#include <gmock/gmock.h>

#include "App/Application.h"
#include "App/Document.h"
#include "App/DocumentObject.h"
#include "App/StringHasher.h"
#include "Base/Exception.h"
#include "Base/Writer.h"
#include <src/App/InitApplication.h>

using ::testing::Eq;
using ::testing::Ne;

// NOLINTBEGIN(readability-magic-numbers)

class FakeWriter: public Base::Writer
{
    void writeFiles() override
    {}
    std::ostream& Stream() override
    {
        return std::cout;
    }
};

class DocumentTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    App::Document* doc()
    {
        return _doc;
    }

private:
    std::string _docName;
    App::Document* _doc {};
};


TEST_F(DocumentTest, addStringHasherIndicatesUnwrittenWhenNew)
{
    // Arrange
    App::StringHasherRef hasher(new App::StringHasher);

    // Act
    auto addResult = doc()->addStringHasher(hasher);

    // Assert
    EXPECT_TRUE(addResult.first);
    EXPECT_THAT(addResult.second, Ne(-1));
}

TEST_F(DocumentTest, addStringHasherIndicatesAlreadyWritten)
{
    // Arrange
    App::StringHasherRef hasher(new App::StringHasher);
    doc()->addStringHasher(hasher);

    // Act
    auto addResult = doc()->addStringHasher(hasher);

    // Assert
    EXPECT_FALSE(addResult.first);
}

TEST_F(DocumentTest, getStringHasherGivesExpectedHasher)
{
    // Arrange
    App::StringHasherRef hasher(new App::StringHasher);
    auto pair = doc()->addStringHasher(hasher);
    int index = pair.second;

    // Act
    auto foundHasher = doc()->getStringHasher(index);

    // Assert
    EXPECT_EQ(hasher, foundHasher);
}

TEST_F(DocumentTest, liveImportUserEditExemptsTreeRankByIdentity)
{
    // Arrange: an object that exists before the document goes live, so
    // its creation is not what the guard refuses; then a user command's
    // scope over a document a progressive import is still filling.
    auto obj = doc()->addObject("App::FeatureTest", "obj");
    ASSERT_NE(obj, nullptr);
    doc()->setStatus(App::Document::LiveImport, true);
    {
        App::Document::UserEditGuard guard;

        // Act / Assert: the tree view's rank bookkeeping is not an edit
        // (it is what the tree writes from its own timer, which a
        // command's nested event loop lets run inside the guard) ...
        EXPECT_NO_THROW(obj->TreeRank.setValue(7));
        EXPECT_EQ(obj->TreeRank.getValue(), 7);
        // ... showing and hiding is looking ...
        EXPECT_NO_THROW(obj->Visibility.setValue(false));
        // ... and anything else is refused, as an abort rather than an
        // error so the command adds no dialog.
        EXPECT_THROW(obj->Label.setValue("renamed"), Base::AbortException);
    }
    // Outside a command scope the live document is writable by the
    // import itself.
    EXPECT_NO_THROW(obj->Label.setValue("filled"));
    doc()->setStatus(App::Document::LiveImport, false);
}

// NOLINTEND(readability-magic-numbers)
