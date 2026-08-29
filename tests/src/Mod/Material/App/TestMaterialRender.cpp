// SPDX-License-Identifier: LGPL-2.1-or-later

// The render properties a material card states (App::MaterialRenderProperty,
// docs/CyclesIntegration.md phase 6 item 14): a card carrying the fork's
// GlassRendering model IS glass -- presence is the switch -- and each of its
// values reaches a Render_* view property only when it is above the
// "engine decides" sentinel of zero.

#include <gtest/gtest.h>

#include <QString>

#include <App/Application.h>
#include <src/App/InitApplication.h>
#include <Mod/Material/App/MaterialManager.h>
#include <Mod/Material/App/Materials.h>

// clang-format off

namespace
{

const char* TintedGlass = "49fa9744-1cdf-44ba-b3b9-29e621450198";
const char* FrostedGlass = "b4e115d3-8294-4b39-ab3e-9d1864a5d380";
const char* Acrylic = "28ae5807-9bb9-40b7-9aed-1655faf52afe";
const char* ClearGlass = "89327390-cd6c-46d1-ab16-a946248e65e3";
// A bundled appearance card with no glass model.
const char* Steel = "92589471-a6cb-4bbc-b748-d425a17dea7d";
// The Test Material: no appearance at all.
const char* TestMaterial = "c6c64159-19c1-40b5-859c-10561f20f979";

const App::MaterialRenderProperty* find(const App::MaterialRenderProperties& props,
                                        const char* name)
{
    for (const auto& prop : props) {
        if (prop.name == name) {
            return &prop;
        }
    }
    return nullptr;
}

}  // namespace

class TestMaterialRender : public ::testing::Test
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
        _materialManager = &(Materials::MaterialManager::getManager());
    }

    App::MaterialRenderProperties renderProperties(const char* uuid)
    {
        auto material = _materialManager->getMaterial(QString::fromLatin1(uuid));
        EXPECT_TRUE(material) << uuid;
        return material ? material->getRenderProperties() : App::MaterialRenderProperties();
    }

    Materials::MaterialManager* _materialManager = nullptr;
};

TEST_F(TestMaterialRender, TintedGlassStatesDensity)
{
    auto props = renderProperties(TintedGlass);
    ASSERT_EQ(props.size(), 3U);

    auto glass = find(props, "Render_Glass");
    ASSERT_NE(glass, nullptr);
    EXPECT_TRUE(glass->boolean);
    EXPECT_NE(glass->value, 0.0);

    auto ior = find(props, "Render_GlassIOR");
    ASSERT_NE(ior, nullptr);
    EXPECT_FALSE(ior->boolean);
    EXPECT_NEAR(ior->value, 1.52, 1e-6);

    auto density = find(props, "Render_GlassDensity");
    ASSERT_NE(density, nullptr);
    EXPECT_FALSE(density->boolean);
    EXPECT_NEAR(density->value, 0.08, 1e-6);

    // Roughness 0.0 is "engine decides": not stated.
    EXPECT_EQ(find(props, "Render_GlassRoughness"), nullptr);
}

TEST_F(TestMaterialRender, FrostedGlassStatesRoughnessNotDensity)
{
    auto props = renderProperties(FrostedGlass);
    ASSERT_EQ(props.size(), 3U);
    EXPECT_NE(find(props, "Render_Glass"), nullptr);
    auto ior = find(props, "Render_GlassIOR");
    ASSERT_NE(ior, nullptr);
    EXPECT_NEAR(ior->value, 1.52, 1e-6);
    auto rough = find(props, "Render_GlassRoughness");
    ASSERT_NE(rough, nullptr);
    EXPECT_NEAR(rough->value, 0.6, 1e-6);
    // Density 0.0 = automatic, from the body's bounds: not stated.
    EXPECT_EQ(find(props, "Render_GlassDensity"), nullptr);
}

TEST_F(TestMaterialRender, ClearCardsStateOnlyTheIOR)
{
    for (const char* uuid : {Acrylic, ClearGlass}) {
        auto props = renderProperties(uuid);
        ASSERT_EQ(props.size(), 2U) << uuid;
        EXPECT_NE(find(props, "Render_Glass"), nullptr) << uuid;
        EXPECT_NE(find(props, "Render_GlassIOR"), nullptr) << uuid;
    }
    EXPECT_NEAR(find(renderProperties(Acrylic), "Render_GlassIOR")->value, 1.49, 1e-6);
}

TEST_F(TestMaterialRender, GlassIsAlwaysFirst)
{
    // applyMaterialRenderProperties reads the list by name, but the
    // switch leading is what a reader of the list expects.
    auto props = renderProperties(TintedGlass);
    ASSERT_FALSE(props.empty());
    EXPECT_EQ(props.front().name, "Render_Glass");
}

TEST_F(TestMaterialRender, NonGlassCardsStateNothing)
{
    EXPECT_TRUE(renderProperties(Steel).empty());
    EXPECT_TRUE(renderProperties(TestMaterial).empty());
}
