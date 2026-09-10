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

#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>

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

// Two materials in one document, the way an asset's whole material set
// arrives: one file, a surfacematerial per piece
// (docs/MaterialStorage.md sec 17.13).
std::string twoSurfaceDoc()
{
    return "<?xml version=\"1.0\"?>\n"
           "<materialx version=\"1.39\">\n"
           "  <open_pbr_surface name=\"Bishop\" type=\"surfaceshader\">\n"
           "    <input name=\"base_color\" type=\"color3\" value=\"0.1, 0.2, 0.3\" />\n"
           "  </open_pbr_surface>\n"
           "  <surfacematerial name=\"M_Bishop\" type=\"material\">\n"
           "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
           "nodename=\"Bishop\" />\n"
           "  </surfacematerial>\n"
           "  <open_pbr_surface name=\"King\" type=\"surfaceshader\">\n"
           "    <input name=\"base_color\" type=\"color3\" value=\"0.7, 0.8, 0.9\" />\n"
           "  </open_pbr_surface>\n"
           "  <surfacematerial name=\"M_King\" type=\"material\">\n"
           "    <input name=\"surfaceshader\" type=\"surfaceshader\" "
           "nodename=\"King\" />\n"
           "  </surfacematerial>\n"
           "</materialx>\n";
}

// A directory of files standing in for image maps, and the document
// path they resolve against. The generator only asks whether the file
// is THERE -- decoding it is the capture side's business -- so an empty
// file is a whole image as far as this side is concerned, and a
// document path is what a relative name resolves against.
class ScratchImages
{
public:
    explicit ScratchImages(const std::string &name)
        : dir(std::filesystem::temp_directory_path() / ("fc-mtlxgen-" + name))
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    ~ScratchImages()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    ScratchImages(const ScratchImages &) = delete;
    ScratchImages &operator=(const ScratchImages &) = delete;

    /// Make one, and answer the name to state in the document.
    std::string file(const std::string &name) const
    {
        std::ofstream(dir / name, std::ios::binary).put('\0');
        return name;
    }
    std::string document() const { return (dir / "doc.mtlx").string(); }

private:
    std::filesystem::path dir;
};

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
    // subsurface has no raster lobe, so it must not appear -- and its
    // presence in the document must not stop the rest generating.
    // (Transmission used to be the example; the glass stage reads it
    // now, docs/MaterialStorage.md sec 17.22.)
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"subsurface_weight\" type=\"float\" value=\"1.0\" />\n"
        "    <input name=\"base_color\" type=\"color3\" value=\"1, 0, 0\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_EQ(out.source.find("subsurface"), std::string::npos);
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

TEST_F(MaterialXGenerator, anImageBecomesALayerOfOneArray)
{
    // An image node is a LAYER of the one array texture the engine
    // binds (docs/CyclesIntegration.md sec 6.12), so the document
    // generates and says which layer wants which file.
    ScratchImages scratch("one-layer");
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"img\" />\n",
                   "  <image name=\"img\" type=\"color3\">\n"
                   "    <input name=\"file\" type=\"filename\" "
                   "value=\"" + scratch.file("one.png") + "\" />\n"
                   "  </image>\n"),
        scratch.document());
    ASSERT_TRUE(out.valid) << out.error;
    ASSERT_EQ(out.images.size(), 1u);
    EXPECT_EQ(out.images[0].layer, 0);
    EXPECT_FALSE(out.imageSampler.empty());
    // One sampler, declared, so the generated code compiles against a
    // name that is there -- which is the whole reason an image used to
    // be refused outright.
    EXPECT_NE(out.source.find("SAMPLER2DARRAY(" + out.imageSampler),
              std::string::npos)
        << out.source;
    // ...and the node reads it by layer number rather than through a
    // sampler of its own, which is what makes the units stop counting.
    EXPECT_NE(out.source.find("CONST(int) " + out.images[0].name + " = 0"),
              std::string::npos)
        << out.source;
}

TEST_F(MaterialXGenerator, anImageThatIsNotThereIsSaidSoRatherThanRefused)
{
    // The path is the join key the engine stacks on, and there is no
    // file behind this one. The material still draws -- with that map
    // missing, which is worth a word.
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"img\" />\n",
                   "  <image name=\"img\" type=\"color3\">\n"
                   "    <input name=\"file\" type=\"filename\" "
                   "value=\"nowhere.png\" />\n"
                   "  </image>\n"));
    ASSERT_TRUE(out.valid) << out.error;
    // No file, so no layer: there is nothing for the engine to stack.
    EXPECT_TRUE(out.images.empty());
    bool warned = false;
    for (const auto &w : out.warnings)
        warned = warned || w.find("not where the document says")
                               != std::string::npos;
    EXPECT_TRUE(warned);
    // And the generated code says black for it, rather than sampling a
    // unit whose texture is whatever the previous draw left there.
    EXPECT_NE(out.source.find("= -1"), std::string::npos) << out.source;
}

TEST_F(MaterialXGenerator, everyImageIsALayerOfItsOwn)
{
    // Two images must not land on one layer, or the second draws the
    // first's pixels. FOUR of them, because three samplers is exactly
    // what the mesh shader had units free for -- a material with a
    // base colour, a roughness, a coat and an emission is ordinary,
    // and it is the case this array is for.
    ScratchImages scratch("four-layers");
    const char *const inputs[] = {"base_color", "specular_roughness",
                                  "coat_weight", "emission_color"};
    const char *const types[] = {"color3", "float", "float", "color3"};
    const char *const nodes[] = {"a", "b", "c", "d"};
    std::string surface, patterns;
    for (int i = 0; i < 4; ++i) {
        surface += std::string("    <input name=\"") + inputs[i]
            + "\" type=\"" + types[i] + "\" nodename=\"" + nodes[i] + "\" />\n";
        patterns += std::string("  <image name=\"") + nodes[i] + "\" type=\""
            + types[i] + "\">\n"
              "    <input name=\"file\" type=\"filename\" value=\""
            + scratch.file(std::string(nodes[i]) + ".png") + "\" />\n"
              "  </image>\n";
    }
    auto out = Render::MaterialX::generate(openPbrDoc(surface, patterns),
                                           scratch.document());
    ASSERT_TRUE(out.valid) << out.error;
    ASSERT_EQ(out.images.size(), 4u);
    std::set<int> layers;
    for (const auto &image : out.images) {
        EXPECT_GE(image.layer, 0);
        EXPECT_LT(image.layer, 4);
        layers.insert(image.layer);
    }
    EXPECT_EQ(layers.size(), 4u);
    // One unit for all four, which is the whole point.
    EXPECT_EQ(out.imageUnit, 13);
}

TEST_F(MaterialXGenerator, twoNodesNamingOneFileShareALayer)
{
    // A layer is per FILE, not per node: a material reading one map as
    // both its base colour and its coat colour costs one layer, and
    // the cap counts what the engine actually has to upload.
    ScratchImages scratch("shared-layer");
    const std::string file = scratch.file("shared.png");
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"a\" />\n"
                   "    <input name=\"coat_color\" type=\"color3\" "
                   "nodename=\"b\" />\n",
                   "  <image name=\"a\" type=\"color3\">\n"
                   "    <input name=\"file\" type=\"filename\" value=\""
                   + file + "\" />\n"
                   "  </image>\n"
                   "  <image name=\"b\" type=\"color3\">\n"
                   "    <input name=\"file\" type=\"filename\" value=\""
                   + file + "\" />\n"
                   "  </image>\n"),
        scratch.document());
    ASSERT_TRUE(out.valid) << out.error;
    ASSERT_EQ(out.images.size(), 1u);
    EXPECT_EQ(out.images[0].layer, 0);
}

TEST_F(MaterialXGenerator, moreImagesThanOneArrayHoldsIsRefusedWhole)
{
    // The cap moved from the texture units to the array, but it is
    // still a cap, and it still refuses the document rather than
    // drawing it with some of its maps missing. Seventeen distinct
    // files against a sixteen-layer array.
    ScratchImages scratch("overflow");
    std::string patterns, mixInputs;
    std::string mix = "  <add name=\"sum0\" type=\"color3\">\n"
                      "    <input name=\"in1\" type=\"color3\" "
                      "nodename=\"i0\" />\n"
                      "    <input name=\"in2\" type=\"color3\" "
                      "nodename=\"i1\" />\n"
                      "  </add>\n";
    for (int i = 0; i < 17; ++i)
        patterns += "  <image name=\"i" + std::to_string(i)
            + "\" type=\"color3\">\n"
              "    <input name=\"file\" type=\"filename\" value=\""
            + scratch.file("m" + std::to_string(i) + ".png") + "\" />\n"
              "  </image>\n";
    for (int i = 2; i < 17; ++i)
        mix += "  <add name=\"sum" + std::to_string(i - 1) + "\" type=\"color3\">\n"
               "    <input name=\"in1\" type=\"color3\" nodename=\"sum"
            + std::to_string(i - 2) + "\" />\n"
              "    <input name=\"in2\" type=\"color3\" nodename=\"i"
            + std::to_string(i) + "\" />\n"
              "  </add>\n";
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"sum15\" />\n",
                   patterns + mix),
        scratch.document());
    EXPECT_FALSE(out.valid);
    EXPECT_NE(out.error.find("more images"), std::string::npos) << out.error;
}

TEST_F(MaterialXGenerator, theBuiltinTextureCallIsSpelledPortably)
{
    // MaterialX writes GLSL's texture(); bgfx only has it on the
    // backends that are GLSL. The spelling has to be answered or the
    // document compiles on two profiles of three -- which is exactly
    // how this was first found.
    //
    // With images the answer is the array fetch, on every backend: the
    // sampler parameter is a layer index by then, so the builtin could
    // not be called on it even where it exists.
    ScratchImages scratch("portable");
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "nodename=\"img\" />\n",
                   "  <image name=\"img\" type=\"color3\">\n"
                   "    <input name=\"file\" type=\"filename\" "
                   "value=\"" + scratch.file("one.png") + "\" />\n"
                   "  </image>\n"),
        scratch.document());
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_NE(out.source.find("#define texture(_s, _c) fcMtlxImage(_s, _c)"),
              std::string::npos)
        << out.source;
    // The gradient fetch has no bgfx spelling of its own, so both
    // vocabularies are written out and the backend picks.
    EXPECT_NE(out.source.find("SampleGrad"), std::string::npos) << out.source;
    EXPECT_NE(out.source.find("BGFX_SHADER_LANGUAGE_SPIRV"),
              std::string::npos);
}

TEST_F(MaterialXGenerator, anImagelessDocumentClaimsNoUnitAtAll)
{
    // The array is the image path's cost, and a document that names no
    // image must not pay it: no sampler, no unit, and the plain
    // spelling shim it had before any of this.
    auto out = Render::MaterialX::generate(
        openPbrDoc("    <input name=\"base_color\" type=\"color3\" "
                   "value=\"0.2, 0.4, 0.8\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_TRUE(out.images.empty());
    EXPECT_TRUE(out.imageSampler.empty());
    EXPECT_EQ(out.imageUnit, 0);
    EXPECT_EQ(out.source.find("SAMPLER2DARRAY"), std::string::npos)
        << out.source;
    EXPECT_NE(out.source.find("#define texture(_s, _c) texture2D(_s, _c)"),
              std::string::npos)
        << out.source;
}

TEST_F(MaterialXGenerator, whichSurfaceIsGeneratedIsTheCallersToSay)
{
    // One document, a whole asset's material set: naming one surface
    // generates THAT one, and naming none generates the first, which is
    // what every build before the name existed did (sec 17.13).
    const std::string doc = twoSurfaceDoc();
    auto bishop = Render::MaterialX::generate(doc, {}, "M_Bishop");
    ASSERT_TRUE(bishop.valid) << bishop.error;
    EXPECT_NE(bishop.source.find("m.baseColor = vec3(0.100000, 0.200000, 0.300000)"),
              std::string::npos)
        << bishop.source;

    auto king = Render::MaterialX::generate(doc, {}, "M_King");
    ASSERT_TRUE(king.valid) << king.error;
    EXPECT_NE(king.source.find("m.baseColor = vec3(0.700000, 0.800000, 0.900000)"),
              std::string::npos)
        << king.source;

    auto first = Render::MaterialX::generate(doc);
    ASSERT_TRUE(first.valid) << first.error;
    EXPECT_EQ(first.source, bishop.source);
}

TEST_F(MaterialXGenerator, aSurfaceResolvesByTheShaderNodeToo)
{
    // One string is written down; whether it was copied out of a <look>,
    // out of the picker or typed should not decide whether it resolves.
    auto byMaterial = Render::MaterialX::generate(twoSurfaceDoc(), {}, "M_King");
    auto byShader = Render::MaterialX::generate(twoSurfaceDoc(), {}, "King");
    ASSERT_TRUE(byMaterial.valid) << byMaterial.error;
    ASSERT_TRUE(byShader.valid) << byShader.error;
    EXPECT_EQ(byShader.source, byMaterial.source);
}

TEST_F(MaterialXGenerator, aSurfaceTheDocumentDoesNotStateIsRefusedWithTheOnesItDoes)
{
    auto out = Render::MaterialX::generate(twoSurfaceDoc(), {}, "M_Queen");
    EXPECT_FALSE(out.valid);
    EXPECT_NE(out.error.find("M_Queen"), std::string::npos) << out.error;
    // Named, so the mistake is visible rather than rendered as the first
    EXPECT_NE(out.error.find("M_Bishop"), std::string::npos) << out.error;
    EXPECT_NE(out.error.find("M_King"), std::string::npos) << out.error;
}

TEST_F(MaterialXGenerator, aDocumentSaysWhichSurfacesItStatesAndWhichIsWorn)
{
    auto info = Render::MaterialX::inspect(twoSurfaceDoc(), {}, "M_King");
    ASSERT_TRUE(info.valid) << info.error;
    ASSERT_EQ(info.materials.size(), 2u);
    EXPECT_EQ(info.materials[0], "M_Bishop");
    EXPECT_EQ(info.materials[1], "M_King");
    EXPECT_EQ(info.material, "M_King");
    EXPECT_EQ(info.surface, "open_pbr_surface");
    // The surfaces are named for the MATERIAL nodes: that is what a
    // <look>'s materialassign says, and what the picker offers
    EXPECT_EQ(Render::MaterialX::inspect(twoSurfaceDoc()).material, "M_Bishop");
}

TEST_F(MaterialXGenerator, theSurfaceSurvivesTheOpenPbrTranslation)
{
    // The translation rewrites the shader node in place, so the
    // selection is carried as a position in document order, not as a
    // node.
    const std::string doc =
        "<?xml version=\"1.0\"?>\n"
        "<materialx version=\"1.39\">\n"
        "  <standard_surface name=\"A\" type=\"surfaceshader\">\n"
        "    <input name=\"base_color\" type=\"color3\" value=\"0.1, 0.1, 0.1\" />\n"
        "  </standard_surface>\n"
        "  <surfacematerial name=\"M_A\" type=\"material\">\n"
        "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"A\" />\n"
        "  </surfacematerial>\n"
        "  <standard_surface name=\"B\" type=\"surfaceshader\">\n"
        "    <input name=\"base_color\" type=\"color3\" value=\"0.9, 0.9, 0.9\" />\n"
        "  </standard_surface>\n"
        "  <surfacematerial name=\"M_B\" type=\"material\">\n"
        "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"B\" />\n"
        "  </surfacematerial>\n"
        "</materialx>\n";
    auto a = Render::MaterialX::generate(doc, {}, "M_A");
    auto b = Render::MaterialX::generate(doc, {}, "M_B");
    ASSERT_TRUE(a.valid) << a.error;
    ASSERT_TRUE(b.valid) << b.error;
    EXPECT_NE(a.source, b.source);
    EXPECT_NE(a.source.find("0.100000"), std::string::npos) << a.source;
    EXPECT_NE(b.source.find("0.900000"), std::string::npos) << b.source;
}

namespace
{

std::string standardSurfaceDoc(const std::string &inputs, const std::string &patterns = {})
{
    return "<?xml version=\"1.0\"?>\n"
           "<materialx version=\"1.39\">\n"
           + patterns
           + "  <standard_surface name=\"S\" type=\"surfaceshader\">\n"
           + inputs
           + "  </standard_surface>\n"
             "  <surfacematerial name=\"M\" type=\"material\">\n"
             "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"S\" />\n"
             "  </surfacematerial>\n"
             "</materialx>\n";
}

}  // namespace

TEST_F(MaterialXGenerator, anUnstatedInputKeepsItsOwnModelsDefaultThroughTranslation)
{
    // MaterialX's translator forwards the inputs the shader states and
    // reads the rest off the TRANSLATION nodedef, whose defaults are
    // not the source model's: standard_surface says base 1.0, the
    // translation to OpenPBR says 0.8. An authored document states no
    // base, and rendered at 0.8 of its albedo in both engines for it.
    const std::string colour =
        "    <input name=\"base_color\" type=\"color3\" value=\"0.25, 0.25, 0.25\" />\n";
    auto unstated = Render::MaterialX::generate(standardSurfaceDoc(colour));
    auto one = Render::MaterialX::generate(standardSurfaceDoc(
        colour + "    <input name=\"base\" type=\"float\" value=\"1.0\" />\n"));
    auto arnold = Render::MaterialX::generate(standardSurfaceDoc(
        colour + "    <input name=\"base\" type=\"float\" value=\"0.8\" />\n"));
    ASSERT_TRUE(unstated.valid) << unstated.error;
    ASSERT_TRUE(one.valid) << one.error;
    ASSERT_TRUE(arnold.valid) << arnold.error;
    EXPECT_EQ(unstated.source, one.source);
    EXPECT_NE(unstated.source, arnold.source);
}

TEST_F(MaterialXGenerator, aDocumentMayStateOneModelBesideAnother)
{
    // translateAllMaterials threw on the first material already in
    // OpenPBR, so a document carrying one beside a standard_surface
    // rendered neither. Only the chosen surface is translated now.
    const std::string doc =
        "<?xml version=\"1.0\"?>\n"
        "<materialx version=\"1.39\">\n"
        "  <open_pbr_surface name=\"A\" type=\"surfaceshader\">\n"
        "    <input name=\"base_color\" type=\"color3\" value=\"0.1, 0.1, 0.1\" />\n"
        "  </open_pbr_surface>\n"
        "  <surfacematerial name=\"M_A\" type=\"material\">\n"
        "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"A\" />\n"
        "  </surfacematerial>\n"
        "  <standard_surface name=\"B\" type=\"surfaceshader\">\n"
        "    <input name=\"base_color\" type=\"color3\" value=\"0.9, 0.9, 0.9\" />\n"
        "  </standard_surface>\n"
        "  <surfacematerial name=\"M_B\" type=\"material\">\n"
        "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"B\" />\n"
        "  </surfacematerial>\n"
        "</materialx>\n";
    auto a = Render::MaterialX::generate(doc, {}, "M_A");
    auto b = Render::MaterialX::generate(doc, {}, "M_B");
    ASSERT_TRUE(a.valid) << a.error;
    ASSERT_TRUE(b.valid) << b.error;
    EXPECT_NE(a.source.find("0.100000"), std::string::npos) << a.source;
    EXPECT_NE(b.source.find("0.900000"), std::string::npos) << b.source;
}

TEST_F(MaterialXGenerator, aNormalMapSurvivesTheTranslation)
{
    // The translation nodedef has no socket for normal, tangent or
    // coat_normal, and the translator removes every input of the
    // source -- so every standard_surface normal map was dropped on the
    // floor. Carried across to OpenPBR's geometry_normal, the normalmap
    // node is reachable from the surface again and is generated.
    ScratchImages images("normalmap");
    const std::string patterns =
        "  <image name=\"N\" type=\"vector3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"" + images.file("n.png") + "\" />\n"
        "  </image>\n"
        "  <normalmap name=\"NM\" type=\"vector3\">\n"
        "    <input name=\"in\" type=\"vector3\" nodename=\"N\" />\n"
        "  </normalmap>\n";
    auto out = Render::MaterialX::generate(
        standardSurfaceDoc(
            "    <input name=\"base_color\" type=\"color3\" value=\"0.5, 0.5, 0.5\" />\n"
            "    <input name=\"normal\" type=\"vector3\" nodename=\"NM\" />\n",
            patterns),
        images.document());
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_NE(out.source.find("normalmap"), std::string::npos) << out.source;
    EXPECT_EQ(out.images.size(), 1u);
}

TEST_F(MaterialXGenerator, theChessSetIsFifteenMaterialsInOneDocument)
{
    // The case the whole thing exists for. MaterialX's chess set is one
    // document with fifteen surfacematerials and forty-three images,
    // and before the surface name every object wearing it was the same
    // black bishop. The images were never the problem: the layer cap
    // counts the CHOSEN shader's graph, which is four per piece.
    const std::string path = std::string(FC_TEST_MATERIALX_RESOURCES)
        + "Materials/Examples/StandardSurface/standard_surface_chess_set.mtlx";
    std::ifstream in(path, std::ios::binary);
    if (!in)
        GTEST_SKIP() << "no MaterialX example resources at " << path;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string xml = buf.str();

    auto info = Render::MaterialX::inspect(xml, path);
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_EQ(info.materials.size(), 15u);
    EXPECT_EQ(info.material, info.materials.front());

    // Every one of the fifteen generates, and each is its own surface
    std::set<std::string> distinct;
    for (const auto &name : info.materials) {
        auto out = Render::MaterialX::generate(xml, path, name);
        ASSERT_TRUE(out.valid) << name << ": " << out.error;
        EXPECT_LE(out.images.size(), 5u) << name << " samples "
                                         << out.images.size() << " layers";
        distinct.insert(out.source);
    }
    // Fifteen materials, not one drawn fifteen times. (Two pieces of the
    // set share a shader graph, so distinct sources are fewer than 15;
    // what matters is that it is not 1.)
    EXPECT_GT(distinct.size(), 5u);
}

// -- transmission, read flat for the glass pass (sec 17.21) -------------

TEST_F(MaterialXGenerator, aTransmissiveSurfaceIsReportedAsAGlassBody)
{
    // The raster path has no transmission lobe: a transmissive surface
    // is drawn as a glass BODY by the engine's glass pass, which takes
    // one colour, IOR, density and roughness per draw. The inspection
    // reads them off the surface so the capture can claim the pass.
    auto info = Render::MaterialX::inspect(openPbrDoc(
        "    <input name=\"transmission_weight\" type=\"float\" value=\"1\" />\n"
        "    <input name=\"transmission_color\" type=\"color3\" "
        "value=\"0.2, 0.5, 0.4\" />\n"
        "    <input name=\"transmission_depth\" type=\"float\" value=\"2.5\" />\n"
        "    <input name=\"specular_ior\" type=\"float\" value=\"1.4\" />\n"
        "    <input name=\"specular_roughness\" type=\"float\" value=\"0.1\" />\n"));
    ASSERT_TRUE(info.valid) << info.error;
    const auto &t = info.transmission;
    EXPECT_TRUE(t.glass);
    EXPECT_FLOAT_EQ(t.weight, 1.0f);
    EXPECT_FLOAT_EQ(t.color[0], 0.2f);
    EXPECT_FLOAT_EQ(t.color[1], 0.5f);
    EXPECT_FLOAT_EQ(t.color[2], 0.4f);
    EXPECT_FLOAT_EQ(t.depth, 2.5f);
    EXPECT_FLOAT_EQ(t.ior, 1.4f);
    EXPECT_FLOAT_EQ(t.roughness, 0.1f);
    EXPECT_TRUE(t.roughnessImage.empty());
}

TEST_F(MaterialXGenerator, anOpaqueSurfaceClaimsNoGlassBody)
{
    // The default is what every document that says nothing means.
    auto info = Render::MaterialX::inspect(openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" value=\"1, 0, 0\" />\n"));
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_FALSE(info.transmission.glass);
    EXPECT_FLOAT_EQ(info.transmission.weight, 0.0f);
    // The unstated inputs read the model's own defaults, not zeros.
    EXPECT_FLOAT_EQ(info.transmission.ior, 1.5f);
    EXPECT_FLOAT_EQ(info.transmission.roughness, 0.3f);
    EXPECT_FLOAT_EQ(info.transmission.color[0], 1.0f);
}

TEST_F(MaterialXGenerator, aStandardSurfaceTransmissionArrivesByTranslation)
{
    // standard_surface says `transmission`; the translation says
    // `transmission_weight`. Read after the translation, one vocabulary
    // answers for both -- and the unstated depth is the source model's
    // 0 (a surface tint), the unstated IOR its 1.5, the unstated
    // roughness its 0.2, not the translation nodedef's numbers.
    const std::string doc =
        "<?xml version=\"1.0\"?>\n"
        "<materialx version=\"1.39\">\n"
        "  <standard_surface name=\"S\" type=\"surfaceshader\">\n"
        "    <input name=\"transmission\" type=\"float\" value=\"1\" />\n"
        "    <input name=\"transmission_color\" type=\"color3\" "
        "value=\"1, 1, 0.828\" />\n"
        "  </standard_surface>\n"
        "  <surfacematerial name=\"M\" type=\"material\">\n"
        "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"S\" />\n"
        "  </surfacematerial>\n"
        "</materialx>\n";
    auto info = Render::MaterialX::inspect(doc);
    ASSERT_TRUE(info.valid) << info.error;
    const auto &t = info.transmission;
    EXPECT_TRUE(t.glass);
    EXPECT_FLOAT_EQ(t.color[2], 0.828f);
    EXPECT_FLOAT_EQ(t.depth, 0.0f);
    EXPECT_FLOAT_EQ(t.ior, 1.5f);
    EXPECT_FLOAT_EQ(t.roughness, 0.2f);
}

TEST_F(MaterialXGenerator, aStatedDepthSurvivesTheTranslationFlat)
{
    // A translated surface carries every input as a CONNECTION through
    // the translation graph, so a literal read of the node says 0 for
    // every depth a standard_surface document states -- which is what
    // the path tracer did before flatConstant.
    const std::string doc =
        "<?xml version=\"1.0\"?>\n"
        "<materialx version=\"1.39\">\n"
        "  <standard_surface name=\"S\" type=\"surfaceshader\">\n"
        "    <input name=\"transmission\" type=\"float\" value=\"1\" />\n"
        "    <input name=\"transmission_depth\" type=\"float\" value=\"0.01\" />\n"
        "  </standard_surface>\n"
        "  <surfacematerial name=\"M\" type=\"material\">\n"
        "    <input name=\"surfaceshader\" type=\"surfaceshader\" nodename=\"S\" />\n"
        "  </surfacematerial>\n"
        "</materialx>\n";
    auto info = Render::MaterialX::inspect(doc);
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_FLOAT_EQ(info.transmission.depth, 0.01f);
}

TEST_F(MaterialXGenerator, aMappedTransmissionWeightIsNoFlatGlassBody)
{
    // A pass that draws the body with one weight cannot read a map:
    // the surface stays opaque there, and the document is told why.
    ScratchImages images("mappedweight");
    const std::string mask = images.file("mask.png");
    auto info = Render::MaterialX::inspect(openPbrDoc(
        "    <input name=\"transmission_weight\" type=\"float\" "
        "nodegraph=\"NG\" output=\"w\" />\n",
        "  <nodegraph name=\"NG\">\n"
        "    <image name=\"img\" type=\"float\">\n"
        "      <input name=\"file\" type=\"filename\" value=\"" + mask + "\" />\n"
        "    </image>\n"
        "    <output name=\"w\" type=\"float\" nodename=\"img\" />\n"
        "  </nodegraph>\n"), images.document());
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_FALSE(info.transmission.glass);
    bool said = false;
    for (const auto &w : info.warnings)
        said = said || w.find("transmission_weight") != std::string::npos;
    EXPECT_TRUE(said);
}

TEST_F(MaterialXGenerator, aMappedRoughnessNamesItsImage)
{
    // The one map the flat reading can still make something of: the
    // capture, which holds the decoded pixels, stands the map's mean
    // in for the constant the pass cannot have. So the inspection
    // names the file, resolved, and the model's default meanwhile.
    ScratchImages images("mappedrough");
    const std::string rough = images.file("rough.png");
    auto info = Render::MaterialX::inspect(openPbrDoc(
        "    <input name=\"transmission_weight\" type=\"float\" value=\"1\" />\n"
        "    <input name=\"specular_roughness\" type=\"float\" "
        "nodegraph=\"NG\" output=\"r\" />\n",
        "  <nodegraph name=\"NG\">\n"
        "    <image name=\"img\" type=\"float\">\n"
        "      <input name=\"file\" type=\"filename\" value=\"" + rough + "\" />\n"
        "    </image>\n"
        "    <output name=\"r\" type=\"float\" nodename=\"img\" />\n"
        "  </nodegraph>\n"), images.document());
    ASSERT_TRUE(info.valid) << info.error;
    EXPECT_TRUE(info.transmission.glass);
    EXPECT_FLOAT_EQ(info.transmission.roughness, 0.3f);
    ASSERT_FALSE(info.transmission.roughnessImage.empty());
    EXPECT_NE(info.transmission.roughnessImage.find("rough.png"), std::string::npos)
        << info.transmission.roughnessImage;
}

TEST_F(MaterialXGenerator, theTransmissionReachesTheGeneratedFunction)
{
    // The flat reading above is for the consumers that sample nothing;
    // the glass stage splices this same function and reads the
    // transmission per fragment (docs/MaterialStorage.md sec 17.22),
    // so the generated code has to state it like any other input.
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"transmission_weight\" type=\"float\" value=\"1\" />\n"
        "    <input name=\"transmission_color\" type=\"color3\" "
        "value=\"0.2, 0.5, 0.4\" />\n"
        "    <input name=\"transmission_depth\" type=\"float\" value=\"2.5\" />\n"));
    ASSERT_TRUE(out.valid) << out.error;
    EXPECT_NE(out.source.find("m.transmissionWeight = 1.000000"),
              std::string::npos) << out.source;
    EXPECT_NE(out.source.find("m.transmissionColor = vec3(0.200000, 0.500000, 0.400000)"),
              std::string::npos) << out.source;
    EXPECT_NE(out.source.find("m.transmissionDepth = 2.500000"),
              std::string::npos) << out.source;
}

TEST_F(MaterialXGenerator, aMappedTransmissionColourIsSampledByTheGeneratedFunction)
{
    // What the flat reading turns white, the generated function reads
    // off the map: the colour input is an image fetch, not a literal.
    ScratchImages images("mappedtint");
    const std::string tint = images.file("tint.png");
    auto out = Render::MaterialX::generate(openPbrDoc(
        "    <input name=\"transmission_weight\" type=\"float\" value=\"1\" />\n"
        "    <input name=\"transmission_color\" type=\"color3\" "
        "nodegraph=\"NG\" output=\"c\" />\n",
        "  <nodegraph name=\"NG\">\n"
        "    <image name=\"img\" type=\"color3\">\n"
        "      <input name=\"file\" type=\"filename\" value=\"" + tint + "\" />\n"
        "    </image>\n"
        "    <output name=\"c\" type=\"color3\" nodename=\"img\" />\n"
        "  </nodegraph>\n"), images.document());
    ASSERT_TRUE(out.valid) << out.error;
    ASSERT_EQ(out.images.size(), 1u);
    const size_t at = out.source.find("m.transmissionColor = ");
    ASSERT_NE(at, std::string::npos) << out.source;
    // Not a literal: the value is a variable the image fetch wrote.
    EXPECT_EQ(out.source.find("m.transmissionColor = vec3(", at), std::string::npos)
        << out.source;
    EXPECT_NE(out.source.find("fcMtlxImage"), std::string::npos) << out.source;
}

TEST_F(MaterialXGenerator, aStatedNormalReachesTheGeneratedFunction)
{
    // A normal map survives the translation (above); it is only worth
    // anything once the surface's geometry_normal is stated to the
    // consumer, which reads it in place of the mesh's own.
    ScratchImages images("statednormal");
    const std::string patterns =
        "  <image name=\"N\" type=\"vector3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"" + images.file("n.png") + "\" />\n"
        "  </image>\n"
        "  <normalmap name=\"NM\" type=\"vector3\">\n"
        "    <input name=\"in\" type=\"vector3\" nodename=\"N\" />\n"
        "  </normalmap>\n";
    auto out = Render::MaterialX::generate(
        standardSurfaceDoc(
            "    <input name=\"normal\" type=\"vector3\" nodename=\"NM\" />\n",
            patterns),
        images.document());
    ASSERT_TRUE(out.valid) << out.error;
    const size_t at = out.source.find("m.geometryNormal = ");
    ASSERT_NE(at, std::string::npos) << out.source;
    EXPECT_EQ(out.source.find("m.geometryNormal = vec3(", at), std::string::npos)
        << out.source;
    // The frame the map is expressed in comes from the geometry the
    // stage hands over, bitangent included (fcMtlxGeomFill).
    EXPECT_NE(out.source.find("g.tangentWorld"), std::string::npos) << out.source;
    EXPECT_NE(out.source.find("g.bitangentWorld"), std::string::npos) << out.source;
}

TEST_F(MaterialXGenerator, theChessSetsPawnHeadsAreGlassBodies)
{
    // The case that raised it: the white pawn HEADS state transmission
    // 1 with a tinted colour and no depth, and a mapped roughness; the
    // body of the same pawn states none. Our path tracer drew them as
    // glass and the raster as white balls.
    const std::string path = std::string(FC_TEST_MATERIALX_RESOURCES)
        + "Materials/Examples/StandardSurface/standard_surface_chess_set.mtlx";
    std::ifstream in(path, std::ios::binary);
    if (!in)
        GTEST_SKIP() << "no MaterialX example resources at " << path;
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string xml = buf.str();

    auto head = Render::MaterialX::inspect(xml, path, "M_Pawn_Top_W");
    ASSERT_TRUE(head.valid) << head.error;
    EXPECT_TRUE(head.transmission.glass);
    EXPECT_FLOAT_EQ(head.transmission.color[2], 0.828f);
    EXPECT_FLOAT_EQ(head.transmission.depth, 0.0f);
    EXPECT_FLOAT_EQ(head.transmission.ior, 1.5f);
    EXPECT_NE(head.transmission.roughnessImage.find("pawn_shared_roughness"),
              std::string::npos) << head.transmission.roughnessImage;

    auto body = Render::MaterialX::inspect(xml, path, "M_Pawn_Body_W");
    ASSERT_TRUE(body.valid) << body.error;
    EXPECT_FALSE(body.transmission.glass);
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

// ---------------------------------------------------------------------------
// Image references: the two halves of making a document travel with its
// maps (docs/MaterialStorage.md sec 16). One says what the document
// refers to under a key that is a function of the TEXT and so the same
// everywhere; the other hands back the document naming those files
// where they are on this machine.

TEST_F(MaterialXGenerator, aDocumentSaysWhatFilesItRefersTo)
{
    ScratchImages images("refs");
    const std::string name = images.file("color.png");
    const std::string xml = openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" nodename=\"tex\" />\n",
        "  <image name=\"tex\" type=\"color3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"" + name + "\" />\n"
        "  </image>\n");

    auto refs = Render::MaterialX::imageReferences(xml, images.document());
    ASSERT_EQ(refs.size(), 1u);
    // The key is what the document SAYS, not where it landed.
    EXPECT_EQ(refs[0].name, name);
    EXPECT_FALSE(refs[0].path.empty());
}

TEST_F(MaterialXGenerator, theKeyCarriesTheFileprefixAndNothingElse)
{
    ScratchImages images("prefix");
    images.file("color.png");
    // The shape MaterialX's own examples are written in: an inherited
    // fileprefix on the document, a bare name on the input. Neither
    // half alone is what the document means by the file.
    std::string xml = openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" nodename=\"tex\" />\n",
        "  <image name=\"tex\" type=\"color3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"color.png\" />\n"
        "  </image>\n");
    const std::string prefix = std::string("fileprefix=\"")
        + images.document().substr(0, images.document().rfind("doc.mtlx")) + "\"";
    xml.replace(xml.find("<materialx version=\"1.39\""),
                std::strlen("<materialx version=\"1.39\""),
                "<materialx version=\"1.39\" " + prefix);

    auto refs = Render::MaterialX::imageReferences(xml);
    ASSERT_EQ(refs.size(), 1u);
    // Prefix applied, search path not consulted: the key is a function
    // of the text, which is what makes it the same on every machine.
    EXPECT_NE(refs[0].name.find("color.png"), std::string::npos);
    EXPECT_NE(refs[0].name, std::string("color.png"));
    EXPECT_FALSE(refs[0].path.empty());
}

TEST_F(MaterialXGenerator, aCarriedDocumentNamesItsFilesWhereTheyAre)
{
    ScratchImages images("carried");
    images.file("color.png");
    std::string xml = openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" nodename=\"tex\" />\n",
        "  <image name=\"tex\" type=\"color3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"color.png\" />\n"
        "  </image>\n");
    const std::string dir = images.document().substr(
            0, images.document().rfind("doc.mtlx"));
    xml.replace(xml.find("<materialx version=\"1.39\""),
                std::strlen("<materialx version=\"1.39\""),
                "<materialx version=\"1.39\" fileprefix=\"" + dir + "\"");

    auto refs = Render::MaterialX::imageReferences(xml);
    ASSERT_EQ(refs.size(), 1u);
    // Stand in for the stored blob: somewhere else entirely, which is
    // what a transient directory is to the machine that authored this.
    const std::string stored = images.file("stored.png");
    std::vector<Render::MaterialX::ImageReference> files;
    files.push_back({refs[0].name, dir + stored});

    const std::string carried = Render::MaterialX::substituteImages(xml, files);
    EXPECT_NE(carried.find("stored.png"), std::string::npos) << carried;
    // The prefix went with the name it qualified: left in place it
    // would be prepended to the absolute path by whoever resolves the
    // result next.
    EXPECT_EQ(carried.find("fileprefix"), std::string::npos) << carried;
    // And the result is still a document that reads and resolves.
    auto after = Render::MaterialX::imageReferences(carried);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0].name, dir + stored);
    EXPECT_FALSE(after[0].path.empty());
}

TEST_F(MaterialXGenerator, aReferenceNotCarriedKeepsWhatItsPrefixMeant)
{
    ScratchImages images("partial");
    images.file("color.png");
    images.file("other.png");
    std::string xml = openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" nodename=\"tex\" />\n"
        "    <input name=\"specular_color\" type=\"color3\" nodename=\"tex2\" />\n",
        "  <image name=\"tex\" type=\"color3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"color.png\" />\n"
        "  </image>\n"
        "  <image name=\"tex2\" type=\"color3\">\n"
        "    <input name=\"file\" type=\"filename\" value=\"other.png\" />\n"
        "  </image>\n");
    const std::string dir = images.document().substr(
            0, images.document().rfind("doc.mtlx"));
    xml.replace(xml.find("<materialx version=\"1.39\""),
                std::strlen("<materialx version=\"1.39\""),
                "<materialx version=\"1.39\" fileprefix=\"" + dir + "\"");

    auto refs = Render::MaterialX::imageReferences(xml);
    ASSERT_EQ(refs.size(), 2u);
    // Only the first is carried -- the second's content has not arrived,
    // or this machine never had it. Either way it is not ours to rewrite.
    const std::string stored = images.file("stored.png");
    std::vector<Render::MaterialX::ImageReference> files;
    files.push_back({dir + "color.png", dir + stored});

    const std::string carried = Render::MaterialX::substituteImages(xml, files);
    EXPECT_EQ(carried.find("fileprefix"), std::string::npos) << carried;
    // The prefix is gone for the carried one's sake, so the other has to
    // have taken what the prefix meant: it still names the same file and
    // still resolves. Left as a bare name it would have resolved before
    // this call and not after it.
    auto after = Render::MaterialX::imageReferences(carried);
    ASSERT_EQ(after.size(), 2u);
    EXPECT_EQ(after[0].name, dir + stored);
    EXPECT_EQ(after[1].name, dir + "other.png");
    EXPECT_FALSE(after[1].path.empty()) << after[1].name;
}

TEST_F(MaterialXGenerator, aDocumentWithoutTheNamedFileIsLeftAlone)
{
    const std::string xml = openPbrDoc(
        "    <input name=\"base_color\" type=\"color3\" "
        "value=\"0.25, 0.5, 0.75\" />\n");
    // Nothing to substitute: an imageless document comes back as it
    // went in, rather than round-tripped through the writer.
    std::vector<Render::MaterialX::ImageReference> files;
    files.push_back({"absent.png", "/tmp/somewhere.png"});
    EXPECT_EQ(Render::MaterialX::substituteImages(xml, files), xml);
    EXPECT_TRUE(Render::MaterialX::imageReferences(xml).empty());
}
