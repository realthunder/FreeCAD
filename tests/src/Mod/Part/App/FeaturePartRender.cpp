// SPDX-License-Identifier: LGPL-2.1-or-later

// The App-side route of a card's render properties: a Part::Feature answers
// GeoFeature::getMaterialRenderProperties() from its ShapeMaterial card, so
// the Gui can state the Render_* view properties without seeing Materials.

#include "gtest/gtest.h"

#include <QString>

#include <src/App/InitApplication.h>
#include <App/Application.h>
#include <App/Document.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/Materials.h>
#include <Mod/Part/App/FeaturePartBox.h>

// clang-format off

class FeaturePartRenderTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _doc = App::GetApplication().newDocument("FeaturePartRender");
        _box = dynamic_cast<Part::Box*>(_doc->addObject("Part::Box"));
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    App::Document* _doc = nullptr;
    Part::Box* _box = nullptr;
};

TEST_F(FeaturePartRenderTest, defaultStatesNothing)
{
    ASSERT_NE(_box, nullptr);
    EXPECT_TRUE(_box->getMaterialRenderProperties().empty());
}

TEST_F(FeaturePartRenderTest, glassCardReachesTheFeature)
{
    ASSERT_NE(_box, nullptr);
    auto& manager = Materials::MaterialManager::getManager();
    auto tinted = manager.getMaterial(QStringLiteral("49fa9744-1cdf-44ba-b3b9-29e621450198"));
    ASSERT_TRUE(tinted);
    _box->ShapeMaterial.setValue(*tinted);

    auto props = _box->getMaterialRenderProperties();
    ASSERT_EQ(props.size(), 3U);
    EXPECT_EQ(props[0].name, "Render_Glass");
    EXPECT_TRUE(props[0].boolean);
    bool density = false;
    for (const auto& prop : props) {
        if (prop.name == "Render_GlassDensity") {
            density = true;
            EXPECT_NEAR(prop.value, 0.045, 1e-6);
        }
    }
    EXPECT_TRUE(density);

    // Back to a card without the glass model: nothing stated again.
    auto steel = manager.getMaterial(QStringLiteral("92589471-a6cb-4bbc-b748-d425a17dea7d"));
    ASSERT_TRUE(steel);
    _box->ShapeMaterial.setValue(*steel);
    EXPECT_TRUE(_box->getMaterialRenderProperties().empty());
}
