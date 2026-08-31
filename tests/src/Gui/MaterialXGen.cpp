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

// ----------------------------------------------------------------------------
// The document's declared interface (docs/CyclesIntegration.md sec 6.11):
// what a node graph DECLARES is a parameter, and everything else the
// document states stays a constant in the generated code.

namespace
{

// A document whose surface is fed by a graph declaring its own inputs
// -- MaterialX's way of saying "these are the knobs".
std::string declaringDoc(const std::string &declarations,
                         const std::string &nodes,
                         const std::string &surface = "open_pbr_surface")
{
    return "<?xml version=\"1.0\"?>\n"
           "<materialx version=\"1.39\">\n"
           "  <nodegraph name=\"NG\">\n"
           + declarations + nodes
           + "    <output name=\"out\" type=\"color3\" nodename=\"mul\" />\n"
             "  </nodegraph>\n"
             "  <"
           + surface
           + " name=\"S\" type=\"surfaceshader\">\n"
             "    <input name=\"base_color\" type=\"color3\" nodegraph=\"NG\" "
             "output=\"out\" />\n"
             "  </"
           + surface
           + ">\n"
             "  <surfacematerial name=\"M\" type=\"material\">\n"
             "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
             "nodename=\"S\" />\n"
             "  </surfacematerial>\n"
             "</materialx>\n";
}

// tint * gain, both taken from the graph's interface.
const char *TINT_TIMES_GAIN =
    "    <multiply name=\"mul\" type=\"color3\">\n"
    "      <input name=\"in1\" type=\"color3\" interfacename=\"tint\" />\n"
    "      <input name=\"in2\" type=\"float\" interfacename=\"gain\" />\n"
    "    </multiply>\n";

const char *TINT_AND_GAIN =
    "    <input name=\"tint\" type=\"color3\" value=\"0.2, 0.4, 0.6\" "
    "uiname=\"Tint\" uifolder=\"Look\" />\n"
    "    <input name=\"gain\" type=\"float\" value=\"0.5\" />\n";

}  // namespace

TEST_F(MaterialXGenerator, aDeclaredInputIsTheDocumentsInterface)
{
    auto info = Render::MaterialX::inspect(
        declaringDoc(TINT_AND_GAIN, TINT_TIMES_GAIN));
    ASSERT_TRUE(info.valid) << info.error;
    ASSERT_EQ(info.inputs.size(), 2u);
    // In document order, named as the document names them, and
    // carrying what the document says about presenting them.
    EXPECT_EQ(info.inputs[0].name, "tint");
    EXPECT_EQ(info.inputs[0].type, "color3");
    EXPECT_EQ(info.inputs[0].path, "NG/tint");
    EXPECT_EQ(info.inputs[0].label, "Tint");
    EXPECT_EQ(info.inputs[0].folder, "Look");
    ASSERT_EQ(info.inputs[0].value.size(), 3u);
    EXPECT_FLOAT_EQ(info.inputs[0].value[1], 0.4f);
    EXPECT_EQ(info.inputs[1].name, "gain");
    ASSERT_EQ(info.inputs[1].value.size(), 1u);
    EXPECT_FLOAT_EQ(info.inputs[1].value[0], 0.5f);
}

TEST_F(MaterialXGenerator, aDeclaredInputBecomesAUniform)
{
    auto out = Render::MaterialX::generate(
        declaringDoc(TINT_AND_GAIN, TINT_TIMES_GAIN));
    ASSERT_TRUE(out.valid) << out.error;
    // One vec4 lane group per parameter, read as its own type -- the
    // packing every other user-shader parameter already travels in.
    EXPECT_NE(out.source.find("uniform vec4 u_tint"), std::string::npos)
        << out.source;
    EXPECT_NE(out.source.find("uniform vec4 u_gain"), std::string::npos);
    EXPECT_NE(out.source.find("= u_tint.xyz"), std::string::npos) << out.source;
    EXPECT_NE(out.source.find("= u_gain.x"), std::string::npos);
    // And the declared value is not ALSO folded in as a literal.
    EXPECT_EQ(out.source.find("0.200000, 0.400000, 0.600000"), std::string::npos);
}

TEST_F(MaterialXGenerator, aValueStatedOnTheSurfaceIsNoInterface)
{
    // The step-2 contract, restated from the other side: a value the
    // document states on the surface node is its statement, not a knob,
    // and stays the literal it was.
    auto info = Render::MaterialX::inspect(openPbrDoc(
        "    <input name=\"specular_roughness\" type=\"float\" value=\"0.4\" />\n"));
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_TRUE(info.inputs.empty());
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"specular_roughness\" type=\"float\" value=\"0.4\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_EQ(out.source.find("uniform "), std::string::npos) << out.source;
    EXPECT_NE(out.source.find("m.specularRoughness = 0.400000"), std::string::npos);
}

TEST_F(MaterialXGenerator, aDeclaredInputNothingUsesIsNoParameter)
{
    // Declared and connected to nothing: a knob wired to nothing is
    // worse than no knob.
    auto info = Render::MaterialX::inspect(declaringDoc(
        std::string(TINT_AND_GAIN)
            + "    <input name=\"spare\" type=\"float\" value=\"7\" />\n",
        TINT_TIMES_GAIN));
    ASSERT_TRUE(info.valid) << info.error;
    ASSERT_EQ(info.inputs.size(), 2u);
    EXPECT_EQ(info.inputs[0].name, "tint");
    EXPECT_EQ(info.inputs[1].name, "gain");
}

TEST_F(MaterialXGenerator, oneDeclaredInputReadTwiceIsOneUniform)
{
    auto out = Render::MaterialX::generate(declaringDoc(
        "    <input name=\"gain\" type=\"float\" value=\"0.5\" />\n",
        "    <multiply name=\"first\" type=\"color3\">\n"
        "      <input name=\"in1\" type=\"color3\" value=\"1, 0, 0\" />\n"
        "      <input name=\"in2\" type=\"float\" interfacename=\"gain\" />\n"
        "    </multiply>\n"
        "    <multiply name=\"mul\" type=\"color3\">\n"
        "      <input name=\"in1\" type=\"color3\" nodename=\"first\" />\n"
        "      <input name=\"in2\" type=\"float\" interfacename=\"gain\" />\n"
        "    </multiply>\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // MaterialX publishes a value per node input; a parameter is one
    // uniform however many places read it.
    EXPECT_EQ(out.source.find("uniform vec4 u_gain"),
              out.source.rfind("uniform vec4 u_gain"))
        << out.source;
    EXPECT_NE(out.source.find("first_in2 = u_gain.x"), std::string::npos)
        << out.source;
    EXPECT_NE(out.source.find("mul_in2 = u_gain.x"), std::string::npos);
}

TEST_F(MaterialXGenerator, theInterfaceSurvivesTheTranslation)
{
    // The interface is read from the document as AUTHORED, and a graph
    // interface is untouched by the OpenPBR translation -- so the same
    // parameters answer for a document stating another shading model.
    const std::string doc =
        declaringDoc(TINT_AND_GAIN, TINT_TIMES_GAIN, "standard_surface");
    auto info = Render::MaterialX::inspect(doc);
    ASSERT_TRUE(info.valid) << info.error;
    ASSERT_EQ(info.inputs.size(), 2u);
    EXPECT_EQ(info.inputs[0].path, "NG/tint");
    auto out = Render::MaterialX::generate(doc);
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_NE(out.source.find("uniform vec4 u_tint"), std::string::npos)
        << out.source;
}

TEST_F(MaterialXGenerator, aDeclaredInputReadsItsLaneAsItsOwnType)
{
    auto out = Render::MaterialX::generate(declaringDoc(
        "    <input name=\"octaves\" type=\"integer\" value=\"3\" />\n"
        "    <input name=\"tint\" type=\"color3\" value=\"1, 0, 0\" />\n",
        "    <fractal3d name=\"noise\" type=\"float\">\n"
        "      <input name=\"octaves\" type=\"integer\" "
        "interfacename=\"octaves\" />\n"
        "    </fractal3d>\n"
        "    <multiply name=\"mul\" type=\"color3\">\n"
        "      <input name=\"in1\" type=\"color3\" interfacename=\"tint\" />\n"
        "      <input name=\"in2\" type=\"float\" nodename=\"noise\" />\n"
        "    </multiply>\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // A lane is a float; an integer input reads it as one.
    EXPECT_NE(out.source.find("int(u_octaves.x)"), std::string::npos)
        << out.source;
}

TEST_F(MaterialXGenerator, aDeclaredInputNamedAsASurfaceInputStillBinds)
{
    // MaterialX resolves a graph's interface names in the ENCLOSING
    // graph's socket namespace, and the surface's nodedef put every one
    // of its own inputs in there first -- so a graph input named
    // "base_color" is FUSED with the surface's own base_color socket
    // and never appears under its own namepath. That is the obvious
    // document to write, not an exotic one, and before this was handled
    // the generated code read a name nothing declared.
    auto out = Render::MaterialX::generate(declaringDoc(
        "    <input name=\"base_color\" type=\"color3\" value=\"0.2, 0.4, 0.6\" />\n"
        "    <input name=\"gain\" type=\"float\" value=\"0.5\" />\n",
        "    <multiply name=\"mul\" type=\"color3\">\n"
        "      <input name=\"in1\" type=\"color3\" interfacename=\"base_color\" />\n"
        "      <input name=\"in2\" type=\"float\" interfacename=\"gain\" />\n"
        "    </multiply>\n"));
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_NE(out.source.find("uniform vec4 u_base_color"), std::string::npos)
        << out.source;
    EXPECT_NE(out.source.find("base_color = u_base_color.xyz"), std::string::npos)
        << out.source;
    // And it is bound, so nothing reports it as folded away.
    for (const auto &w : out.warnings)
        EXPECT_EQ(w.find("base_color"), std::string::npos) << w;
}
