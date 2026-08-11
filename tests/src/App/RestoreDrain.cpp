// SPDX-License-Identifier: LGPL-2.1-or-later

/** The restore-drain scope (docs/DocumentLoad.md §13).
 *
 * View work replayed after a load has finished -- the Gui document's
 * deferred view provider drain -- must converge on the state the eager
 * load reached, and the eager load left nothing needing a recompute: the
 * restore purged each object before announcing it. A handler that writes
 * back while it renders therefore costs the eager path nothing and costs
 * the drain a document that asks to be saved after merely being opened.
 *
 * App's half of that contract is one scope and two touch paths, and this
 * is what it promises: inside the scope the write still lands, the touch
 * does not, and what tried is named -- because the handlers that do this
 * live in the workbenches, and are found by opening a file.
 */

#include <gtest/gtest.h>

#include <string>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>

#include <src/App/InitApplication.h>

// In namespace App because the type system reads a registered name as
// <module>::<class> and tries to import the module for anything else.
namespace App
{

/// An object with one ordinary input property to write to.
class TestDrainFeature: public App::DocumentObject
{
    PROPERTY_HEADER_WITH_OVERRIDE(TestDrainFeature);

public:
    TestDrainFeature()
    {
        ADD_PROPERTY_TYPE(Value, (0), "Test", App::Prop_None, "an input property");
    }
    App::PropertyInteger Value;

    const char* getViewProviderName() const override
    {
        return "";
    }
};

}  // namespace App

PROPERTY_SOURCE(App::TestDrainFeature, App::DocumentObject)

namespace
{

using App::TestDrainFeature;

class RestoreDrainTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
        TestDrainFeature::init();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("drain");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _obj = static_cast<TestDrainFeature*>(_doc->addObject("App::TestDrainFeature", "obj"));
        // A freshly added object is touched; the drain's business is with
        // what happens to an object that already settled.
        _obj->purgeTouched();
        _doc->clearRestoreDrainReport();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    std::string _docName;
    App::Document* _doc {};
    TestDrainFeature* _obj {};
};

TEST_F(RestoreDrainTest, anOrdinaryChangeTouches)
{
    _obj->Value.setValue(1);

    EXPECT_TRUE(_obj->isTouched());
    EXPECT_TRUE(_obj->mustRecompute());
    EXPECT_EQ(_doc->getRestoreDrainReport().count, 0U);
}

TEST_F(RestoreDrainTest, aChangeInsideTheScopeLandsButDoesNotTouch)
{
    {
        App::Document::RestoreDrainGuard guard(_doc);
        _obj->Value.setValue(1);
    }

    // The write is the render's own cache refresh: it must still take.
    EXPECT_EQ(_obj->Value.getValue(), 1);
    EXPECT_FALSE(_obj->isTouched());
    EXPECT_FALSE(_obj->mustRecompute());
}

TEST_F(RestoreDrainTest, aTouchInsideTheScopeIsSuppressedToo)
{
    {
        App::Document::RestoreDrainGuard guard(_doc);
        _obj->touch();
    }

    EXPECT_FALSE(_obj->isTouched());
    EXPECT_FALSE(_obj->mustRecompute());
    EXPECT_EQ(_doc->getRestoreDrainReport().count, 1U);
    ASSERT_EQ(_doc->getRestoreDrainReport().names.size(), 1U);
    EXPECT_NE(_doc->getRestoreDrainReport().names[0].find("obj.touch()"), std::string::npos);
}

TEST_F(RestoreDrainTest, whatWasSuppressedNamesItself)
{
    {
        App::Document::RestoreDrainGuard guard(_doc);
        _obj->Value.setValue(1);
        _obj->Value.setValue(2);
    }

    const auto& report = _doc->getRestoreDrainReport();
    // Twice, but one name: the count is the measurement, the name is only
    // there to point at the handler.
    EXPECT_EQ(report.count, 2U);
    ASSERT_EQ(report.names.size(), 1U);
    EXPECT_NE(report.names[0].find("obj.Value"), std::string::npos);
    EXPECT_FALSE(report.truncated);
}

TEST_F(RestoreDrainTest, theScopeEndsWithItself)
{
    {
        App::Document::RestoreDrainGuard guard(_doc);
        _obj->Value.setValue(1);
    }
    _obj->Value.setValue(2);

    EXPECT_TRUE(_obj->isTouched());
    EXPECT_EQ(_doc->getRestoreDrainReport().count, 1U);
}

TEST_F(RestoreDrainTest, anInnerScopeDoesNotEndTheOuterOne)
{
    {
        App::Document::RestoreDrainGuard outer(_doc);
        {
            App::Document::RestoreDrainGuard inner(_doc);
        }
        // A slice may enter the scope again from inside itself; the first
        // one is the one that owns the status bit.
        _obj->Value.setValue(1);
    }

    EXPECT_FALSE(_obj->isTouched());
    EXPECT_EQ(_doc->getRestoreDrainReport().count, 1U);
}

TEST_F(RestoreDrainTest, theReportIsClearedByWhoeverReadsIt)
{
    {
        App::Document::RestoreDrainGuard guard(_doc);
        _obj->Value.setValue(1);
    }
    ASSERT_EQ(_doc->getRestoreDrainReport().count, 1U);

    _doc->clearRestoreDrainReport();

    EXPECT_EQ(_doc->getRestoreDrainReport().count, 0U);
    EXPECT_TRUE(_doc->getRestoreDrainReport().names.empty());
}

}  // namespace
