// SPDX-License-Identifier: LGPL-2.1-or-later

// Gui::applyMaterialRenderProperties: the one creator of a card's Render_*
// dynamic view properties (docs/CyclesIntegration.md phase 6 item 14). It
// reaches the container through App::PropertyContainer alone, so the
// subject here is a document object: a view provider cannot exist without
// the Gui application and its main window. The contract under test: a named property is created or updated, a property
// of the glass family the card does NOT name is removed, an unchanged
// state reports no change, and the properties land in the "Render" group
// where the Render Settings panel keeps its own.

#include <gtest/gtest.h>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/MaterialAppearance.h>
#include <App/PropertyStandard.h>
#include <Gui/ViewProviderGeometryObject.h>

// clang-format off

namespace
{

App::MaterialRenderProperty boolProp(const char* name, bool value)
{
    return {name, true, value ? 1.0 : 0.0};
}

App::MaterialRenderProperty floatProp(const char* name, double value)
{
    return {name, false, value};
}

double floatValue(App::PropertyContainer& vp, const char* name)
{
    auto prop = dynamic_cast<App::PropertyFloat*>(vp.getPropertyByName(name));
    return prop ? prop->getValue() : -1.0;
}

}  // namespace

class RenderPropertiesTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        App::Application::Config()["ExeName"] = "RenderProperties_tests_run";
        int argc = 1;
        char exename[] = "RenderProperties_tests_run";
        char* argv[] = {exename, nullptr};
        App::Application::init(argc, argv);
    }

    void SetUp() override
    {
        doc = App::GetApplication().newDocument("RenderProperties");
        vp = doc->addObject("App::FeatureTest", "Subject");
        ASSERT_NE(vp, nullptr);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(doc->getName());
    }

    App::Document* doc = nullptr;
    App::DocumentObject* vp = nullptr;
};

TEST_F(RenderPropertiesTest, nullProviderIsNoChange)
{
    EXPECT_FALSE(Gui::applyMaterialRenderProperties(nullptr, {boolProp("Render_Glass", true)}));
}

TEST_F(RenderPropertiesTest, emptyOnBareProviderIsNoChange)
{
    EXPECT_FALSE(Gui::applyMaterialRenderProperties(vp, {}));
    EXPECT_EQ(vp->getPropertyByName("Render_Glass"), nullptr);
}

TEST_F(RenderPropertiesTest, statedPropertiesAreCreated)
{
    App::MaterialRenderProperties tinted = {
        boolProp("Render_Glass", true),
        floatProp("Render_GlassIOR", 1.52),
        floatProp("Render_GlassDensity", 0.08),
    };
    EXPECT_TRUE(Gui::applyMaterialRenderProperties(vp, tinted));

    auto glass = dynamic_cast<App::PropertyBool*>(vp->getPropertyByName("Render_Glass"));
    ASSERT_NE(glass, nullptr);
    EXPECT_TRUE(glass->getValue());
    EXPECT_STREQ(vp->getPropertyGroup(glass), "Render");
    EXPECT_DOUBLE_EQ(floatValue(*vp, "Render_GlassIOR"), 1.52);
    EXPECT_DOUBLE_EQ(floatValue(*vp, "Render_GlassDensity"), 0.08);
    // Not stated by the card: not created.
    EXPECT_EQ(vp->getPropertyByName("Render_GlassRoughness"), nullptr);

    // The same card again changes nothing.
    EXPECT_FALSE(Gui::applyMaterialRenderProperties(vp, tinted));
    EXPECT_DOUBLE_EQ(floatValue(*vp, "Render_GlassDensity"), 0.08);
}

TEST_F(RenderPropertiesTest, anotherCardReplacesTheFamily)
{
    ASSERT_TRUE(Gui::applyMaterialRenderProperties(vp, {
        boolProp("Render_Glass", true),
        floatProp("Render_GlassIOR", 1.52),
        floatProp("Render_GlassDensity", 0.08),
    }));
    // Frosted: roughness in, density out (back to automatic), IOR kept.
    EXPECT_TRUE(Gui::applyMaterialRenderProperties(vp, {
        boolProp("Render_Glass", true),
        floatProp("Render_GlassIOR", 1.52),
        floatProp("Render_GlassRoughness", 0.6),
    }));
    EXPECT_NE(vp->getPropertyByName("Render_Glass"), nullptr);
    EXPECT_DOUBLE_EQ(floatValue(*vp, "Render_GlassIOR"), 1.52);
    EXPECT_DOUBLE_EQ(floatValue(*vp, "Render_GlassRoughness"), 0.6);
    EXPECT_EQ(vp->getPropertyByName("Render_GlassDensity"), nullptr);

    // A value change on an existing property is a change.
    EXPECT_TRUE(Gui::applyMaterialRenderProperties(vp, {
        boolProp("Render_Glass", true),
        floatProp("Render_GlassIOR", 1.49),
        floatProp("Render_GlassRoughness", 0.6),
    }));
    EXPECT_DOUBLE_EQ(floatValue(*vp, "Render_GlassIOR"), 1.49);
}

TEST_F(RenderPropertiesTest, cardWithoutGlassClearsTheFamily)
{
    ASSERT_TRUE(Gui::applyMaterialRenderProperties(vp, {
        boolProp("Render_Glass", true),
        floatProp("Render_GlassIOR", 1.52),
        floatProp("Render_GlassDensity", 0.08),
        floatProp("Render_GlassRoughness", 0.6),
    }));
    EXPECT_TRUE(Gui::applyMaterialRenderProperties(vp, {}));
    for (const char* name : {"Render_Glass", "Render_GlassIOR", "Render_GlassDensity",
                             "Render_GlassRoughness"}) {
        EXPECT_EQ(vp->getPropertyByName(name), nullptr) << name;
    }
    EXPECT_FALSE(Gui::applyMaterialRenderProperties(vp, {}));
}

TEST_F(RenderPropertiesTest, glassOffIsStatedNotRemoved)
{
    // A card may state Render_Glass false explicitly; the property then
    // exists and is false, which is not the same as absent.
    EXPECT_TRUE(Gui::applyMaterialRenderProperties(vp, {boolProp("Render_Glass", false)}));
    auto glass = dynamic_cast<App::PropertyBool*>(vp->getPropertyByName("Render_Glass"));
    ASSERT_NE(glass, nullptr);
    EXPECT_FALSE(glass->getValue());
}
