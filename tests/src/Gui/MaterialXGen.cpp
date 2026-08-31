// Tests for the raster MaterialX generator (docs/CyclesIntegration.md
// sec 6.10): a document becomes a material-inputs function, or it is
// reported and the draw keeps its stock appearance. What these pin down
// is the CONTRACT -- that a constant folds to a literal, that a pattern
// graph is carried through, that another shading model arrives by
// translation, and that the two things the raster path cannot do fail
// whole instead of generating code that will not compile.
//
// They need no GL context and no document; they do need the shipped
// data library, whose path the build hands in.

#include <gtest/gtest.h>

#include <string>

#include "Gui/Renderer/MaterialXSupport.h"
#include "Gui/Renderer/Renderer.h"

namespace
{

// A document states its surface, and a material node names it. Both
// halves matter: the generator resolves the surface through the
// material the way the library's own examples are written.
std::string openPbrDoc(const std::string &surfaceInputs,
                       const std::string &patterns = {})
{
    return "<?xml version=\"1.0\"?>\n"
           "<materialx version=\"1.39\">\n"
           + patterns
           + "  <open_pbr_surface name=\"S\" type=\"surfaceshader\">\n"
           + surfaceInputs
           + "  </open_pbr_surface>\n"
             "  <surfacematerial name=\"M\" type=\"material\">\n"
             "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
             "nodename=\"S\" />\n"
             "  </surfacematerial>\n"
             "</materialx>\n";
}

class MaterialXGenerator : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!Render::MaterialX::available())
            GTEST_SKIP() << "built without MaterialX (BUILD_MATERIALX)";
        // The data library ships beside the binary; a bare test binary
        // has no one to seed the resource path, so the build states it.
        Render::RendererFactory::setResourcePath(FC_TEST_RENDERER_RESOURCES);
        if (Render::MaterialX::dataLibraryPath().empty())
            GTEST_SKIP() << "no MaterialX data library on disk";
    }
};

}  // namespace

TEST_F(MaterialXGenerator, statesTheSurfaceAsAFunctionOfItsOwn)
{
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" "
        "value=\"0.25, 0.5, 0.75\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // The engine keeps its lighting: what comes back is one function
    // over the OpenPBR parameters, not a program.
    EXPECT_NE(out.source.find("void fcUserMaterialInputs(inout FcOpenPbr m, "
                              "FcMtlxGeom g)"),
              std::string::npos);
    EXPECT_EQ(out.source.find("void main("), std::string::npos);
    EXPECT_EQ(out.source.find("uniform "), std::string::npos);
}

TEST_F(MaterialXGenerator, aStatedConstantBecomesALiteral)
{
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" "
        "value=\"0.25, 0.5, 0.75\" />\n"
        "    <input name=\"specular_roughness\" type=\"float\" value=\"0.4\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // A value nothing will ever write is worth no uniform: it is the
    // document's own statement, so it is emitted as one.
    EXPECT_NE(out.source.find("m.baseColor = vec3(0.250000, 0.500000, 0.750000)"),
              std::string::npos)
        << out.source;
    EXPECT_NE(out.source.find("m.specularRoughness = 0.400000"),
              std::string::npos);
}

TEST_F(MaterialXGenerator, anInputTheRasterSurfaceLacksIsLeftAtItsDefault)
{
    // transmission has no raster lobe, so it must not appear -- and its
    // presence in the document must not stop the rest generating.
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"transmission_weight\" type=\"float\" value=\"1.0\" />\n"
        "    <input name=\"base_color\" type=\"color3\" value=\"1, 0, 0\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_EQ(out.source.find("transmission"), std::string::npos);
    EXPECT_NE(out.source.find("m.baseColor"), std::string::npos);
}

TEST_F(MaterialXGenerator, carriesAPatternGraphThrough)
{
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"mul\" />\n",
                   "  <multiply name=\"mul\" type=\"color3\">\n"
                   "    <input name=\"in1\" type=\"color3\" value=\"1, 0, 0\" />\n"
                   "    <input name=\"in2\" type=\"float\" value=\"0.5\" />\n"
                   "  </multiply>\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // The pattern is computed in the function and assigned, rather than
    // folded away or dropped.
    EXPECT_NE(out.source.find("mul_out"), std::string::npos) << out.source;
    EXPECT_NE(out.source.find("m.baseColor = mul_out"), std::string::npos)
        << out.source;
}

TEST_F(MaterialXGenerator, geometryComesFromTheMeshShadersVaryings)
{
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"tex\" />\n",
                   "  <texcoord name=\"uv\" type=\"vector2\" />\n"
                   "  <convert name=\"tex\" type=\"color3\">\n"
                   "    <input name=\"in\" type=\"vector2\" nodename=\"uv\" />\n"
                   "  </convert>\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // Whatever the graph asks of the geometry is answered from the
    // struct the mesh shader fills, never from a varying read at file
    // scope -- which the SPIR-V path cannot do.
    EXPECT_NE(out.source.find("= g.texcoord0"), std::string::npos) << out.source;
}

TEST_F(MaterialXGenerator, anotherShadingModelArrivesByTranslation)
{
    std::string doc = "<?xml version=\"1.0\"?>\n"
                      "<materialx version=\"1.39\">\n"
                      "  <standard_surface name=\"S\" type=\"surfaceshader\">\n"
                      "    <input name=\"base_color\" type=\"color3\" "
                      "value=\"0.8, 0.2, 0.2\" />\n"
                      "  </standard_surface>\n"
                      "  <surfacematerial name=\"M\" type=\"material\">\n"
                      "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
                      "nodename=\"S\" />\n"
                      "  </surfacematerial>\n"
                      "</materialx>\n";
    auto out = Render::MaterialX::generate(doc);
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_NE(out.source.find("m.baseColor"), std::string::npos);
    // A translation is an approximation and says so, because the two
    // models do not agree on every lobe.
    bool said = false;
    for (const auto &w : out.warnings)
        said = said || w.find("translated") != std::string::npos;
    EXPECT_TRUE(said);
}

TEST_F(MaterialXGenerator, aModelThatDoesNotTranslateIsReportedWhole)
{
    // UsdPreviewSurface translates FROM standard_surface but not TO
    // OpenPBR, so there is nothing to render it with. The draw keeps
    // its stock appearance rather than compiling half a material.
    std::string doc = "<?xml version=\"1.0\"?>\n"
                      "<materialx version=\"1.39\">\n"
                      "  <UsdPreviewSurface name=\"S\" type=\"surfaceshader\">\n"
                      "    <input name=\"diffuseColor\" type=\"color3\" "
                      "value=\"0.5, 0.5, 0.5\" />\n"
                      "  </UsdPreviewSurface>\n"
                      "  <surfacematerial name=\"M\" type=\"material\">\n"
                      "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
                      "nodename=\"S\" />\n"
                      "  </surfacematerial>\n"
                      "</materialx>\n";
    auto out = Render::MaterialX::generate(doc);
    EXPECT_FALSE(out.valid);
    EXPECT_TRUE(out.source.empty());
    EXPECT_NE(out.error.find("UsdPreviewSurface"), std::string::npos) << out.error;
}

TEST_F(MaterialXGenerator, anImageIsReportedRatherThanLeftUndeclared)
{
    // Nothing binds a texture to a user shader yet, so an image node
    // would generate a sampler name no one declares. Reported whole --
    // the path tracer renders such a document properly.
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"img\" />\n",
                   "  <image name=\"img\" type=\"color3\">\n"
                   "    <input name=\"file\" type=\"filename\" "
                   "value=\"nowhere.png\" />\n"
                   "  </image>\n"));
    EXPECT_FALSE(out.valid);
    EXPECT_TRUE(out.source.empty());
    EXPECT_NE(out.error.find("image"), std::string::npos) << out.error;
}

TEST_F(MaterialXGenerator, aDocumentWithNoSurfaceIsReported)
{
    std::string doc = "<?xml version=\"1.0\"?>\n"
                      "<materialx version=\"1.39\">\n"
                      "  <multiply name=\"mul\" type=\"color3\">\n"
                      "    <input name=\"in1\" type=\"color3\" value=\"1, 0, 0\" />\n"
                      "    <input name=\"in2\" type=\"float\" value=\"0.5\" />\n"
                      "  </multiply>\n"
                      "</materialx>\n";
    auto out = Render::MaterialX::generate(doc);
    EXPECT_FALSE(out.valid);
    EXPECT_NE(out.error.find("no surface"), std::string::npos) << out.error;
}

TEST_F(MaterialXGenerator, unparsableTextIsReportedNotThrown)
{
    auto out = Render::MaterialX::generate("this is not a document");
    EXPECT_FALSE(out.valid);
    EXPECT_FALSE(out.error.empty());
}
