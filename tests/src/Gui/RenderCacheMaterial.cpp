// Stage 2 of the per-face appearance work
// (docs/ShapeAppearanceDesign.md section 6): the render cache captures
// the array form of ambient/emissive/specular/shininess through the
// coin fork's extended lazy element, purely by traversal -- no GL
// context, no window, no document. Verified by reading the values back
// out of the built cache, not by looking at a picture: Coin's own
// output is expected to be unchanged at this stage.
//
// The same binary also verifies the fallback: run with a stock libCoin
// preloaded (one without the coin_lazyex_* symbols) and the second test
// takes over, asserting the capture degrades to exactly the old
// scalar-only behavior.

#include <gtest/gtest.h>

#include <FCGlobal.h>

#include <Inventor/SbViewportRegion.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoMaterialBinding.h>
#include <Inventor/nodes/SoSeparator.h>

#include <App/Application.h>
#include <Gui/SoFCDB.h>
#include <Gui/Inventor/CoinLazyElementEx.h>
#include <Gui/Inventor/SoFCVertexCache.h>
#include <Gui/Inventor/SoFCRenderCache.h>
#include <Gui/Inventor/SoFCRenderCacheManager.h>

namespace {

uint32_t packed(float r, float g, float b)
{
    return SbColor(r, g, b).getPackedValue(0.0F);
}

// Deliberately asymmetric values so no conversion or transposition can
// cancel out, and every field differs from every other field.
const SbColor kDiffuse[3] = {{0.9F, 0.1F, 0.1F}, {0.1F, 0.9F, 0.1F}, {0.1F, 0.1F, 0.9F}};
const SbColor kAmbient[3] = {{0.2F, 0.0F, 0.0F}, {0.0F, 0.3F, 0.0F}, {0.0F, 0.0F, 0.4F}};
const SbColor kEmissive[3] = {{0.05F, 0.0F, 0.1F}, {0.1F, 0.05F, 0.0F}, {0.0F, 0.1F, 0.05F}};
const SbColor kSpecular[3] = {{0.7F, 0.6F, 0.5F}, {0.5F, 0.7F, 0.6F}, {0.6F, 0.5F, 0.7F}};
const float kShininess[3] = {0.15F, 0.45F, 0.85F};

class RenderCacheMaterial : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        // SoFCDB::init registers Base::Type classes, and the cache
        // flatten reads ViewParams, which reaches the application's
        // parameter system -- so the App has to be up, headless
        App::Application::Config()["ExeName"] = "RenderCacheMaterial_tests_run";
        int argc = 1;
        char exename[] = "RenderCacheMaterial_tests_run";
        char* argv[] = {exename, nullptr};
        App::Application::init(argc, argv);
        Gui::SoFCDB::init();
    }

    void SetUp() override
    {
        root = new SoSeparator;
        root->ref();

        auto binding = new SoMaterialBinding;
        binding->value = SoMaterialBinding::PER_FACE;
        root->addChild(binding);

        material = new SoMaterial;
        material->diffuseColor.setValues(0, 3, kDiffuse);
        material->ambientColor.setValues(0, 3, kAmbient);
        material->emissiveColor.setValues(0, 3, kEmissive);
        material->specularColor.setValues(0, 3, kSpecular);
        material->shininess.setValues(0, 3, kShininess);
        root->addChild(material);

        auto coords = new SoCoordinate3;
        const SbVec3f pts[5] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {2, 0, 0}};
        coords->point.setValues(0, 5, pts);
        root->addChild(coords);

        auto faces = new SoIndexedFaceSet;
        const int32_t idx[12] = {0, 1, 2, -1, 0, 2, 3, -1, 1, 4, 2, -1};
        faces->coordIndex.setValues(0, 12, idx);
        root->addChild(faces);
    }

    void TearDown() override
    {
        root->unref();
    }

    // The one triangle-draw material of the built scene.
    const SoFCRenderCache::Material* triangleMaterial()
    {
        manager.traverse(root, SbViewportRegion(256, 256));
        SoFCRenderCache* cache = manager.getSceneCache();
        if (!cache) {
            return nullptr;
        }
        for (const auto& entry : cache->getVertexCaches(true)) {
            if (entry.first.type == SoFCRenderCache::Material::Triangle) {
                return &entry.first;
            }
        }
        return nullptr;
    }

    SoFCRenderCacheManager manager;
    SoSeparator* root = nullptr;
    SoMaterial* material = nullptr;
};

TEST_F(RenderCacheMaterial, CapturesPerFaceArrays)
{
    if (!Gui::CoinLazyElementEx::available()) {
        GTEST_SKIP() << "coin_lazyex_* not present in this libCoin";
    }

    const auto* m = triangleMaterial();
    ASSERT_NE(m, nullptr);

    // the scalars behave exactly as before: entry 0
    float t = 0.0F;
    EXPECT_EQ(m->ambient, kAmbient[0].getPackedValue(t));
    EXPECT_EQ(m->emissive, kEmissive[0].getPackedValue(t));
    EXPECT_EQ(m->specular, kSpecular[0].getPackedValue(t));
    EXPECT_FLOAT_EQ(m->shininess, kShininess[0]);

    // and the array form survives to the cache, entry for entry
    ASSERT_EQ(m->ambients.getNum(), 3);
    ASSERT_EQ(m->emissives.getNum(), 3);
    ASSERT_EQ(m->speculars.getNum(), 3);
    ASSERT_EQ(m->shininesses.getNum(), 3);
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(m->ambients[i], packed(kAmbient[i][0], kAmbient[i][1], kAmbient[i][2]))
            << "ambient " << i;
        EXPECT_EQ(m->emissives[i], packed(kEmissive[i][0], kEmissive[i][1], kEmissive[i][2]))
            << "emissive " << i;
        EXPECT_EQ(m->speculars[i], packed(kSpecular[i][0], kSpecular[i][1], kSpecular[i][2]))
            << "specular " << i;
        EXPECT_FLOAT_EQ(m->shininesses[i], kShininess[i]) << "shininess " << i;
    }
}

TEST_F(RenderCacheMaterial, UniformMaterialStaysScalar)
{
    if (!Gui::CoinLazyElementEx::available()) {
        GTEST_SKIP() << "coin_lazyex_* not present in this libCoin";
    }

    // one entry per field: the arrays must stay empty, so a uniform
    // object's material is bit-identical to what it was before stage 2
    material->diffuseColor.setValue(kDiffuse[0]);
    material->ambientColor.setValue(kAmbient[0]);
    material->emissiveColor.setValue(kEmissive[0]);
    material->specularColor.setValue(kSpecular[0]);
    material->shininess.setValue(kShininess[0]);

    const auto* m = triangleMaterial();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->ambients.getNum(), 0);
    EXPECT_EQ(m->emissives.getNum(), 0);
    EXPECT_EQ(m->speculars.getNum(), 0);
    EXPECT_EQ(m->shininesses.getNum(), 0);
    EXPECT_EQ(m->ambient, kAmbient[0].getPackedValue());
}

TEST_F(RenderCacheMaterial, FallbackAgainstStockCoin)
{
    if (Gui::CoinLazyElementEx::available()) {
        GTEST_SKIP() << "extension present; run with a stock libCoin preloaded";
    }

    // without the extension the scalars must still arrive and the
    // arrays must stay empty -- exactly the pre-stage-2 capture
    const auto* m = triangleMaterial();
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(m->ambient, kAmbient[0].getPackedValue());
    EXPECT_EQ(m->emissive, kEmissive[0].getPackedValue());
    EXPECT_EQ(m->specular, kSpecular[0].getPackedValue());
    EXPECT_FLOAT_EQ(m->shininess, kShininess[0]);
    EXPECT_EQ(m->ambients.getNum(), 0);
    EXPECT_EQ(m->emissives.getNum(), 0);
    EXPECT_EQ(m->speculars.getNum(), 0);
    EXPECT_EQ(m->shininesses.getNum(), 0);
}

}  // namespace
