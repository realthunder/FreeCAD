// SPDX-License-Identifier: LGPL-2.1-or-later

/** Material lists stored one field at a time (docs/ShapeAppearanceDesign.md).
 *
 * The property keeps a logical entry count and, per field, an array that is
 * 0, 1 or that many long. What these tests hold to account is that the three
 * cardinalities are interchangeable from the outside -- every read answers
 * the same whichever one the storage is in -- and that a value only ever
 * costs the smallest of them.
 *
 * The collapsing is not an optimisation to be tested loosely. The
 * shared-default scheme elides a property whose bytes match its class
 * default's, so two lists that mean the same thing have to serialise the
 * same way; a test that only checked values would pass while every
 * appearance in a document quietly stopped being elided.
 *
 * No document, no view provider and no GL context: this is App's own
 * storage, so the suite links FreeCADApp alone.
 */

#include <gtest/gtest.h>

#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <App/FileBlobManager.h>
#include <App/MaterialAppearance.h>
#include <App/PropertyStandard.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/Exception.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include <src/App/InitApplication.h>

namespace
{

/// A colour built from its packed form, so nothing is lost to 8-bit rounding
App::Color packed(uint32_t rgba)
{
    App::Color color;
    color.setPackedValue(rgba);
    return color;
}

App::MaterialAppearance redMaterial()
{
    App::MaterialAppearance mat;
    mat.diffuseColor = packed(0xff0000ff);
    return mat;
}

App::MaterialAppearance texturedMaterial()
{
    App::MaterialAppearance mat;
    mat.diffuseColor = packed(0x0000ffff);
    mat.image = std::string("PNG\x01\x02", 5);  // bytes, not text
    mat.imagePath = "/home/someone/textures/oak <old>.png";
    mat.uuid = "f0e1d2c3-b4a5-6978-8a9b-0c1d2e3f4050";
    return mat;
}

App::SurfaceFinish knurlFinish()
{
    App::SurfaceFinish finish;
    finish.pattern = App::SurfaceFinish::Knurl;
    // mm. Short decimals, so they survive the doc file's 6-digit text form
    // as well as the XML form's max_digits10
    finish.pitch = 0.8F;
    finish.depth = 0.3F;
    finish.angle = 45.0F;
    return finish;
}

App::SurfaceFinish brushedFinish()
{
    App::SurfaceFinish finish;
    finish.pattern = App::SurfaceFinish::Brushed;
    finish.pitch = 0.05F;
    finish.depth = 0.002F;
    finish.angle = 30.0F;
    return finish;
}

/// Content hashes, not paths: the slots name blobs the manager owns
App::SurfaceTexture oakTexture()
{
    App::SurfaceTexture texture;
    texture.maps[App::SurfaceTexture::BaseColor] = "0123456789abcdef";
    texture.maps[App::SurfaceTexture::Normal] = "fedcba9876543210";
    // Short decimals, so they survive the doc file's 6-digit text form
    texture.scale[0] = 2.0F;
    texture.scale[1] = 0.5F;
    texture.offset[0] = 0.25F;
    texture.rotation = 90.0F;
    return texture;
}

App::MaterialAppearance fullyPaintedMaterial()
{
    App::MaterialAppearance mat;
    mat.ambientColor = packed(0x11223344);
    mat.diffuseColor = packed(0x55667788);
    // Consistent, as the property enforces: the diffuse alpha is the
    // transparency's complement, and both here are exact binary fractions so
    // in-memory round trips compare bit for bit.
    mat.diffuseColor.a = 0.75F;
    mat.transparency = 0.25F;
    mat.specularColor = packed(0x99aabbcc);
    mat.emissiveColor = packed(0xddeeff00);
    mat.shininess = 0.75F;
    return mat;
}

std::string saveToXML(const App::PropertyAppearanceList& prop, int schema)
{
    Base::StringWriter writer;
    writer.setForceXML(1);
    writer.setSchemaVersion(schema);
    prop.Save(writer);
    return writer.getString();
}

void restoreFromXML(App::PropertyAppearanceList& prop, const std::string& xml,
                    const char* programVersion = "")
{
    std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?><document>)" + xml + "</document>";
    std::istringstream stream(doc);
    Base::XMLReader reader("material.xml", stream);
    // What App::Document reads off the root element. Which release wrote a
    // document decides what a colour's alpha component means in it.
    reader.ProgramVersion = programVersion;
    prop.Restore(reader);
}

std::string saveDocFile(const App::PropertyAppearanceList& prop, int schema)
{
    Base::StringWriter writer;
    writer.setPreferBinary(false);
    writer.setSchemaVersion(schema);
    prop.SaveDocFile(writer);
    return writer.getString();
}

void restoreDocFile(App::PropertyAppearanceList& prop, const std::string& data)
{
    std::istringstream stream(data);
    Base::Reader reader(stream, "material.txt");
    prop.RestoreDocFile(reader);
}

/** The same for a binary entry, and with a parser behind it
 *
 * An entry read as part of a document is served by the parser that registered
 * it, which is the only thing that knows the document's version -- the entry
 * itself carries none. Passing none here is a reader with no document at all.
 */
void restoreBinaryDocFile(App::PropertyAppearanceList& prop, const std::string& data,
                          const char* programVersion = nullptr)
{
    std::istringstream stream(data);
    if (!programVersion) {
        Base::Reader reader(stream, "material.bin");
        prop.RestoreDocFile(reader);
        return;
    }
    std::istringstream doc(R"(<?xml version="1.0" encoding="UTF-8"?><document/>)");
    Base::XMLReader parser("Document.xml", doc);
    parser.ProgramVersion = programVersion;
    Base::Reader reader(stream, "material.bin", &parser);
    prop.RestoreDocFile(reader);
}

/** The archive entry a release that means opacity by alpha writes
 *
 * Byte by byte rather than through our own writer: the claim is about their
 * layout and their convention. Every colour is opaque -- alpha 0xff, the
 * vestigial value their migration parks in it -- and each entry's
 * transparency is in the field that carries it, which is their truth.
 */
std::string opacityEraDocFile(const std::vector<std::pair<uint32_t, float>>& entries)
{
    std::string bytes;
    auto putU32 = [&bytes](uint32_t value) {
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    auto putFloat = [&bytes](float value) {
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };

    putU32(static_cast<uint32_t>(entries.size()));
    for (const auto& entry : entries) {
        putU32(0x333333ff);  // ambient, opaque their way
        putU32(entry.first);  // diffuse, likewise
        putU32(0x000000ff);  // specular
        putU32(0x000000ff);  // emissive
        putFloat(0.9F);
        putFloat(entry.second);
    }
    // version 3's second pass: three empty strings per entry
    for (std::size_t i = 0; i < entries.size() * 3; ++i) {
        putU32(0);
    }
    return bytes;
}

/// The same list as an inline element, which is read during the XML pass
std::string opacityEraElement(const std::vector<std::pair<uint32_t, float>>& entries)
{
    std::ostringstream body;
    body << "<MaterialList count=\"" << entries.size() << "\" >\n" << std::hex;
    for (const auto& entry : entries) {
        body << 0x333333ff << ' ' << entry.first << ' ' << 0x000000ff << ' ' << 0x000000ff
             << std::dec << " 0.9 " << entry.second << std::hex << '\n';
    }
    body << std::dec << "</MaterialList>\n";
    return body.str();
}

/// Every entry read back whole, which is what a caller of the old API saw
void expectEntries(const App::PropertyAppearanceList& prop,
                   const std::vector<App::MaterialAppearance>& expected)
{
    ASSERT_EQ(prop.getSize(), static_cast<int>(expected.size()));
    for (int i = 0; i < prop.getSize(); ++i) {
        EXPECT_TRUE(prop.getMaterial(i) == expected[i]) << "entry " << i;
    }
}

/// One 8-bit step of alpha, as a float
constexpr float ALPHA_STEP = 1.0F / 255.0F;

/** The same through a document, where an alpha is stored as a byte
 *
 * A colour's alpha crosses the on-disk boundary as a byte in the legacy
 * convention -- packed, inverted, and inverted back on the way in -- so a
 * float that is not an exact multiple of 1/255 returns one step off, and the
 * transparency, its complement, moves with it. Everything else round trips
 * exactly: rgb bytes are bytes both ways and the floats are printed at full
 * precision.
 */
void expectEntriesQ8(const App::PropertyAppearanceList& prop,
                     const std::vector<App::MaterialAppearance>& expected)
{
    ASSERT_EQ(prop.getSize(), static_cast<int>(expected.size()));
    for (int i = 0; i < prop.getSize(); ++i) {
        const App::MaterialAppearance got = prop.getMaterial(i);
        const App::MaterialAppearance& want = expected[i];
        auto sameColor = [](const App::Color& g, const App::Color& w) {
            return (g.getPackedValue() >> 8) == (w.getPackedValue() >> 8)
                && std::abs(g.a - w.a) <= ALPHA_STEP;
        };
        EXPECT_TRUE(sameColor(got.ambientColor, want.ambientColor)) << "ambient " << i;
        EXPECT_TRUE(sameColor(got.diffuseColor, want.diffuseColor)) << "diffuse " << i;
        EXPECT_TRUE(sameColor(got.specularColor, want.specularColor)) << "specular " << i;
        EXPECT_TRUE(sameColor(got.emissiveColor, want.emissiveColor)) << "emissive " << i;
        EXPECT_NEAR(got.shininess, want.shininess, 1e-6) << "shininess " << i;
        EXPECT_NEAR(got.transparency, want.transparency, ALPHA_STEP) << "transparency " << i;
        EXPECT_EQ(got.image, want.image) << "image " << i;
        EXPECT_EQ(got.imagePath, want.imagePath) << "imagePath " << i;
        EXPECT_EQ(got.uuid, want.uuid) << "uuid " << i;
    }
}

class PropertyAppearanceListTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tests::initApplication();
    }
};

}  // namespace

TEST_F(PropertyAppearanceListTest, defaultEntriesCostNothing)
{
    App::PropertyAppearanceList prop;
    EXPECT_EQ(prop.getSize(), 0);
    EXPECT_EQ(prop.getMemSize(), 0U);

    // ten thousand default materials is still nothing stored
    prop.setSize(10000);
    EXPECT_EQ(prop.getSize(), 10000);
    EXPECT_EQ(prop.getMemSize(), 0U);
    EXPECT_TRUE(prop.getMaterial(5000) == App::MaterialAppearance());
    EXPECT_TRUE(prop.getBase() == App::MaterialAppearance());
    EXPECT_FALSE(prop.hasOverrides());
}

TEST_F(PropertyAppearanceListTest, outOfRangeReadsAsDefault)
{
    App::PropertyAppearanceList prop;
    prop.setValue(redMaterial());
    EXPECT_TRUE(prop.getMaterial(-1) == App::MaterialAppearance());
    EXPECT_TRUE(prop.getMaterial(1) == App::MaterialAppearance());
}

TEST_F(PropertyAppearanceListTest, uniformListIsOneMaterial)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(1000, redMaterial()));

    EXPECT_EQ(prop.getSize(), 1000);
    // nothing overrides, so the list IS its base -- and only the field of it
    // that differs from the default costs anything
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getBase() == redMaterial());
    EXPECT_EQ(prop.getMemSize(), sizeof(App::Color));

    // and every entry still reads as that material
    for (int i : {0, 1, 500, 999}) {
        EXPECT_TRUE(prop.getMaterial(i) == redMaterial()) << "entry " << i;
    }
}

TEST_F(PropertyAppearanceListTest, oneOddEntryStatesOnlyItsOwnField)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(10, redMaterial()));

    App::MaterialAppearance odd = redMaterial();
    odd.diffuseColor = packed(0x00ff00ff);
    prop.set1Value(3, odd);

    // one overriding face, stating the one field it differs in
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({3}));
    EXPECT_EQ(prop.getDiffuseOverrides().size(), 1U);
    EXPECT_TRUE(prop.getAmbientOverrides().empty());
    EXPECT_TRUE(prop.getMaterial(3) == odd);
    EXPECT_TRUE(prop.getMaterial(4) == redMaterial());

    // put it back and the override goes with it
    prop.set1Value(3, redMaterial());
    EXPECT_EQ(prop.getMemSize(), sizeof(App::Color));
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getMaterial(3) == redMaterial());
}

TEST_F(PropertyAppearanceListTest, everyFieldCanVaryOnItsOwn)
{
    App::PropertyAppearanceList prop;
    prop.setSize(4);
    prop.setAmbientColor(1, packed(0x11111111));
    prop.setSpecularColor(2, packed(0x22222222));
    prop.setEmissiveColor(3, packed(0x33333333));
    prop.setShininess(0, 0.5F);
    prop.setTransparency(2, 0.5F);

    EXPECT_EQ(prop.getSize(), 4);
    // four faces painted, each stating the one field it differs in
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({0, 1, 2, 3}));
    EXPECT_EQ(prop.getAmbientOverrides().size(), 4U);
    // per-entry transparency IS per-entry diffuse alpha, so that field is
    // stated with it
    EXPECT_EQ(prop.getDiffuseOverrides().size(), 4U);
    EXPECT_TRUE(prop.getAmbientColor(1) == packed(0x11111111));
    EXPECT_TRUE(prop.getAmbientColor(0) == App::MaterialAppearance().ambientColor);
    EXPECT_TRUE(prop.getSpecularColor(2) == packed(0x22222222));
    EXPECT_TRUE(prop.getEmissiveColor(3) == packed(0x33333333));
    EXPECT_FLOAT_EQ(prop.getShininess(0), 0.5F);
    EXPECT_FLOAT_EQ(prop.getShininess(1), App::MaterialAppearance().shininess);
    EXPECT_FLOAT_EQ(prop.getTransparency(2), 0.5F);
}

TEST_F(PropertyAppearanceListTest, theWholeObjectSetterWritesTheBase)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(100, App::MaterialAppearance()));
    prop.setDiffuseColor(packed(0x0000ffff));

    EXPECT_EQ(prop.getSize(), 100);
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getBase().diffuseColor == packed(0x0000ffff));
    EXPECT_TRUE(prop.getDiffuseColor(99) == packed(0x0000ffff));

    // setting it back to the default gives the storage up entirely
    prop.setDiffuseColor(App::MaterialAppearance().diffuseColor);
    EXPECT_EQ(prop.getMemSize(), 0U);
}

TEST_F(PropertyAppearanceListTest, aWholeObjectWriteLeavesThePaintedFacesAlone)
{
    // docs/ShapeAppearanceDesign.md 12.2: the whole point of the base is
    // that assigning the object a colour does not collapse the faces that
    // hold one of their own
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(10, redMaterial()));
    prop.setDiffuseColor(3, packed(0x00ff00ff));
    prop.setDiffuseColor(7, packed(0x0000ffff));

    prop.setDiffuseColor(packed(0xffff00ff));
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({3, 7}));
    EXPECT_TRUE(prop.getDiffuseColor(0) == packed(0xffff00ff));
    EXPECT_TRUE(prop.getDiffuseColor(3) == packed(0x00ff00ff));
    EXPECT_TRUE(prop.getDiffuseColor(7) == packed(0x0000ffff));

    // and clearing the overrides is what a whole-object write used to do
    prop.clearOverrides();
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getDiffuseColor(3) == packed(0xffff00ff));
}

TEST_F(PropertyAppearanceListTest, wholeFieldSetterSizesTheList)
{
    App::PropertyAppearanceList prop;
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff), packed(0x0000ffff)});

    EXPECT_EQ(prop.getSize(), 3);
    EXPECT_EQ(prop.getDiffuseColors().size(), 3U);
    EXPECT_TRUE(prop.getMaterial(1).diffuseColor == packed(0x00ff00ff));

    // a uniform vector arrives and is the whole-object write it says it is
    prop.setDiffuseColors(std::vector<App::Color>(3, packed(0xff0000ff)));
    EXPECT_EQ(prop.getSize(), 3);
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getBase().diffuseColor == packed(0xff0000ff));
}

TEST_F(PropertyAppearanceListTest, resizingKeepsWhatItCanAndDefaultsTheRest)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {redMaterial(), fullyPaintedMaterial()};
    prop.setValues(values);

    prop.setSize(4);
    values.resize(4);
    expectEntries(prop, values);

    prop.setSize(1);
    values.resize(1);
    expectEntries(prop, values);

    prop.setSize(0);
    EXPECT_EQ(prop.getSize(), 0);
    EXPECT_EQ(prop.getMemSize(), 0U);
}

TEST_F(PropertyAppearanceListTest, growingWithAFillStaysUniform)
{
    // the shape of an import that appends one identically coloured face at a
    // time: it must not materialise the field on every step
    App::PropertyAppearanceList prop;
    for (int i = 0; i < 500; ++i) {
        prop.set1Value(-1, redMaterial());
    }
    EXPECT_EQ(prop.getSize(), 500);
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getMaterial(499) == redMaterial());
}

TEST_F(PropertyAppearanceListTest, legacyXMLRoundTrip)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {redMaterial(), fullyPaintedMaterial(), App::MaterialAppearance()};
    prop.setValues(values);

    const std::string xml = saveToXML(prop, 4);
    EXPECT_EQ(xml.find("fields="), std::string::npos) << xml;

    App::PropertyAppearanceList restored;
    restoreFromXML(restored, xml);
    expectEntriesQ8(restored, values);
}

TEST_F(PropertyAppearanceListTest, fieldXMLRoundTrip)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {redMaterial(), fullyPaintedMaterial(), App::MaterialAppearance()};
    prop.setValues(values);

    const std::string xml = saveToXML(prop, 5);
    EXPECT_NE(xml.find("fields=\"1\""), std::string::npos) << xml;

    App::PropertyAppearanceList restored;
    restoreFromXML(restored, xml);
    expectEntriesQ8(restored, values);
    // and it came back in the compact form, not spelled out
    EXPECT_EQ(restored.getMemSize(), prop.getMemSize());
}

TEST_F(PropertyAppearanceListTest, fieldXMLIsSmallForAUniformList)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(2000, fullyPaintedMaterial()));

    const std::string compact = saveToXML(prop, 5);
    const std::string legacy = saveToXML(prop, 4);
    EXPECT_LT(compact.size(), legacy.size() / 100);

    App::PropertyAppearanceList restored;
    restoreFromXML(restored, compact);
    EXPECT_EQ(restored.getSize(), 2000);
    expectEntriesQ8(restored, std::vector<App::MaterialAppearance>(2000, fullyPaintedMaterial()));
}

TEST_F(PropertyAppearanceListTest, legacyDocFileRoundTrip)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {redMaterial(), fullyPaintedMaterial()};
    prop.setValues(values);

    App::PropertyAppearanceList restored;
    restoreDocFile(restored, saveDocFile(prop, 4));
    expectEntriesQ8(restored, values);
}

TEST_F(PropertyAppearanceListTest, fieldDocFileRoundTrip)
{
    // the shape a STEP import produces: diffuse varies per face and nothing
    // else does, so five of the seven fields are not written at all
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(300, redMaterial());
    values[7].diffuseColor = packed(0x00ff00ff);
    prop.setValues(values);

    // a third of the size, and that is the text encoding's ratio: in binary
    // it is the full six-to-one, one packed colour against a whole material
    const std::string compact = saveDocFile(prop, 5);
    EXPECT_LT(compact.size(), saveDocFile(prop, 4).size() / 3);

    App::PropertyAppearanceList restored;
    restoreDocFile(restored, compact);
    expectEntries(restored, values);
}

TEST_F(PropertyAppearanceListTest, fieldDocFileCostsLittleWhenNothingCollapses)
{
    // the other end of it, stated rather than hidden: when every field
    // genuinely varies there is nothing to save, and the per field encoding
    // must not cost meaningfully more than the one it replaces
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(300, redMaterial());
    values[7] = fullyPaintedMaterial();
    prop.setValues(values);

    const std::string compact = saveDocFile(prop, 5);
    const std::string legacy = saveDocFile(prop, 4);
    EXPECT_LT(compact.size(), legacy.size() + legacy.size() / 100);

    App::PropertyAppearanceList restored;
    restoreDocFile(restored, compact);
    expectEntriesQ8(restored, values);
}

TEST_F(PropertyAppearanceListTest, aFileWrittenTheOldWayStillReadsBack)
{
    // What a legacy document holds: one whole material per entry, in the
    // order the packed colours have always been written, alpha meaning
    // transparency in both slots. The merge takes the larger of the two per
    // entry (an unset slot never wins over a set one) and the colours
    // convert to opacity on the way in.
    App::PropertyAppearanceList prop;
    restoreFromXML(prop,
                   "<MaterialList count=\"2\" >\n"
                   "11223344 55667788 99aabbcc ddeeff00 0.75 0.25\n"
                   "0 ff0000ff 0 0 0.2 0\n"
                   "</MaterialList>\n");

    ASSERT_EQ(prop.getSize(), 2);
    // entry 0: diffuse alpha byte 0x88 says 0.533 transparent, the field
    // says 0.25 -- the alpha slot wins the merge
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue() >> 8, 0x556677U);
    EXPECT_NEAR(prop.getTransparency(0), 0x88 / 255.0F, ALPHA_STEP);
    // its ambient converts from the legacy byte
    EXPECT_NEAR(prop.getAmbientColor(0).a, 1.0F - 0x44 / 255.0F, ALPHA_STEP);
    // entry 1: alpha 0xff was the legacy spelling of invisible
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue() >> 8, 0xff0000U);
    EXPECT_NEAR(prop.getTransparency(1), 1.0F, ALPHA_STEP);
    // an alpha of 0 was the legacy spelling of opaque
    EXPECT_NEAR(prop.getAmbientColor(1).a, 1.0F, ALPHA_STEP);
}

TEST_F(PropertyAppearanceListTest, equalListsSerialiseIdentically)
{
    // the requirement the shared-default scheme rests on: two lists that
    // mean the same thing must produce the same bytes, however they got
    // there, or elision silently stops happening
    App::PropertyAppearanceList byWholeValues;
    byWholeValues.setValues(std::vector<App::MaterialAppearance>(50, redMaterial()));

    App::PropertyAppearanceList byField;
    byField.setSize(50);
    byField.setDiffuseColor(packed(0xff0000ff));

    App::PropertyAppearanceList byEntry;
    byEntry.setSize(50);
    for (int i = 0; i < 50; ++i) {
        byEntry.setDiffuseColor(i, packed(0xff0000ff));
    }

    // The follow flag is part of the value too, and only byField's write is
    // the whole-object one that ends it -- so say it once for all three
    // rather than let three routes disagree about it
    byWholeValues.setFollowMaterial(false);
    byEntry.setFollowMaterial(false);

    EXPECT_TRUE(byWholeValues.isSame(byField));
    EXPECT_TRUE(byWholeValues.isSame(byEntry));
    EXPECT_EQ(saveToXML(byWholeValues, 5), saveToXML(byField, 5));
    EXPECT_EQ(saveToXML(byWholeValues, 5), saveToXML(byEntry, 5));
    EXPECT_EQ(saveDocFile(byWholeValues, 5), saveDocFile(byEntry, 5));
}

TEST_F(PropertyAppearanceListTest, isSameSeesThroughCardinality)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));

    App::PropertyAppearanceList other;
    other.setValues(std::vector<App::MaterialAppearance>(5, redMaterial()));
    EXPECT_FALSE(prop.isSame(other));

    other.setSize(4);
    EXPECT_TRUE(prop.isSame(other));

    other.setShininess(2, 0.9F);
    EXPECT_FALSE(prop.isSame(other));
}

TEST_F(PropertyAppearanceListTest, copyAndPasteCarryEveryField)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {fullyPaintedMaterial(), redMaterial()};
    prop.setValues(values);

    std::unique_ptr<App::Property> copy(prop.Copy());
    App::PropertyAppearanceList pasted;
    pasted.Paste(*copy);

    expectEntries(pasted, values);
    EXPECT_TRUE(pasted.isSame(prop));
}

TEST_F(PropertyAppearanceListTest, saveSizeAnswersForTheEncodingBeingWritten)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(1000, redMaterial()));

    Base::StringWriter writer;
    writer.setSchemaVersion(5);
    EXPECT_EQ(prop.getSaveSize(writer), prop.getMemSize());

    // the compatible encoding spells out every entry, and the rule that
    // decides between an inline list and an archive entry has to hear that
    writer.setSchemaVersion(4);
    EXPECT_EQ(prop.getSaveSize(writer), 1000U * 24U);
}

TEST_F(PropertyAppearanceListTest, texturesAndCardsRideTheBaseLikeEveryOtherField)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(200, texturedMaterial()));

    EXPECT_EQ(prop.getSize(), 200);
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_EQ(prop.getBase().uuid, texturedMaterial().uuid);
    EXPECT_EQ(prop.getUuid(199), texturedMaterial().uuid);

    prop.setUuid(7, "another-card");
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({7}));
    EXPECT_EQ(prop.getUuidOverrides().size(), 1U);
    EXPECT_EQ(prop.getUuid(7), "another-card");
    EXPECT_EQ(prop.getUuid(8), texturedMaterial().uuid);
    // and no other field had to state anything
    EXPECT_TRUE(prop.getImageOverrides().empty());
}

TEST_F(PropertyAppearanceListTest, aTexturePathSurvivesTheXMLForm)
{
    // spaces and angle brackets in a path, and bytes that are not text in an
    // embedded image: the inline form has to carry them intact
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {texturedMaterial(), App::MaterialAppearance()};
    prop.setValues(values);

    const std::string xml = saveToXML(prop, 4);
    EXPECT_NE(xml.find("fields=\"1\""), std::string::npos) << xml;

    App::PropertyAppearanceList restored;
    restoreFromXML(restored, xml);
    expectEntries(restored, values);
    EXPECT_EQ(restored.getImagePath(0), texturedMaterial().imagePath) << xml;
    EXPECT_EQ(restored.getUuid(0), texturedMaterial().uuid) << xml;
}

TEST_F(PropertyAppearanceListTest, aManifestHashRoundTripsBothFieldEncodings)
{
    // The MaterialX field rides the ESCAPE bit in the stream form and its
    // own self-describing key in the XML form, and both carry the base's
    // value ahead of the column because RunBase cannot grow a member
    // (docs/MaterialStorage.md 17.9). Uniform first: one value, no base.
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(4, redMaterial());
    for (auto& value : values)
        value.materialx = "0123456789abcdef0123456789abcdef01234567";
    prop.setValues(values);
    EXPECT_TRUE(prop.blobContentNeedsStore());

    App::PropertyAppearanceList fromXml;
    const std::string xml = saveToXML(prop, 5);
    EXPECT_NE(xml.find("\nm 2 "), std::string::npos) << xml;
    restoreFromXML(fromXml, xml);
    expectEntries(fromXml, values);
    EXPECT_EQ(fromXml.getMaterialX(3), values[3].materialx);
    EXPECT_FALSE(fromXml.variesInMaterialX());

    App::PropertyAppearanceList fromStream;
    restoreDocFile(fromStream, saveDocFile(prop, 5));
    expectEntries(fromStream, values);
    EXPECT_EQ(fromStream.getMaterialX(0), values[0].materialx);
}

TEST_F(PropertyAppearanceListTest, aPerFaceManifestHashRidesTheBaseAndTheColumn)
{
    // Sparse: the base wears one document set and one face another, so the
    // field's run states the base value and then the override's
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(6, redMaterial());
    for (auto& value : values)
        value.materialx = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    values[2].materialx = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    prop.setValues(values);
    EXPECT_TRUE(prop.variesInMaterialX());

    App::PropertyAppearanceList fromXml;
    restoreFromXML(fromXml, saveToXML(prop, 5));
    expectEntries(fromXml, values);
    EXPECT_EQ(fromXml.getMaterialX(2), values[2].materialx);
    EXPECT_EQ(fromXml.getMaterialX(5), values[5].materialx);

    App::PropertyAppearanceList fromStream;
    restoreDocFile(fromStream, saveDocFile(prop, 5));
    expectEntries(fromStream, values);
    EXPECT_EQ(fromStream.getMaterialX(2), values[2].materialx);
    EXPECT_EQ(fromStream.getMaterialX(5), values[5].materialx);

    // A face that stops naming one folds back into the base, and a list
    // that never named one has no such column and no store to ask for
    fromStream.setMaterialX(2, values[5].materialx);
    EXPECT_FALSE(fromStream.variesInMaterialX());
    App::PropertyAppearanceList plain;
    plain.setValues(std::vector<App::MaterialAppearance>(3, redMaterial()));
    EXPECT_FALSE(plain.blobContentNeedsStore());
}

TEST_F(PropertyAppearanceListTest, stringsRideTheCompactDocFile)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(50, texturedMaterial());
    values[3].uuid = "odd-one-out";
    prop.setValues(values);

    App::PropertyAppearanceList restored;
    restoreDocFile(restored, saveDocFile(prop, 5));
    expectEntries(restored, values);
}

TEST_F(PropertyAppearanceListTest, aFileWithStringsIsWrittenAsUpstreamsVersionThree)
{
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {texturedMaterial(), redMaterial()};
    prop.setValues(values);

    // the element has to say version="3", because that is the only way
    // upstream's reader knows a second pass follows
    Base::StringWriter element;
    element.setSchemaVersion(4);
    element.setPreferBinary(true);
    element.setForceXML(0);  // a StringWriter forces XML unless told otherwise
    prop.Save(element);
    EXPECT_NE(element.getString().find("version=\"3\""), std::string::npos)
        << element.getString();
    EXPECT_NE(element.getString().find("file="), std::string::npos);

    // and round trips through it
    Base::StringWriter file;
    file.setSchemaVersion(4);
    file.setPreferBinary(true);
    file.setForceXML(0);
    prop.SaveDocFile(file);

    App::PropertyAppearanceList restored;
    restoreFromXML(restored, element.getString());
    std::istringstream stream(file.getString());
    Base::Reader reader(stream, "material.bin");
    restored.RestoreDocFile(reader);
    expectEntries(restored, values);
}

TEST_F(PropertyAppearanceListTest, readsAFileLaidOutTheWayUpstreamWritesIt)
{
    // Built byte by byte rather than with our own writer, because the claim
    // is about their layout: a count, then four packed colours, shininess
    // and transparency per entry, then a second pass of three
    // length-prefixed strings per entry.
    std::string bytes;
    auto putU32 = [&bytes](uint32_t value) {
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    auto putFloat = [&bytes](float value) {
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    auto putString = [&bytes, &putU32](const std::string& value) {
        putU32(static_cast<uint32_t>(value.size()));
        bytes.append(value);
    };

    putU32(2);  // count
    for (int i = 0; i < 2; ++i) {
        putU32(0x11223344);            // ambient
        putU32(i == 0 ? 0xff0000ffU : 0x00ff00ffU);  // diffuse
        putU32(0x99aabbcc);            // specular
        putU32(0xddeeff00);            // emissive
        putFloat(0.5F);                // shininess
        putFloat(0.25F);               // transparency
    }
    putString("");
    putString("/tex/one.png");
    putString("card-1");
    putString("");
    putString("");
    putString("");

    App::PropertyAppearanceList prop;
    // Their layout comes with their era: the version says what the bytes
    // mean, so the reader is wired the way a real document wires it.
    restoreFromXML(prop, "<MaterialList file=\"m.bin\" version=\"3\"/>\n", "1.1R41234");
    restoreBinaryDocFile(prop, bytes, "1.1R41234");

    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue() >> 8, 0xff0000U);
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue() >> 8, 0x00ff00U);
    // the transparency field is their truth; the alpha stores its complement
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(0).a, 0.75F);
    EXPECT_FLOAT_EQ(prop.getTransparency(1), 0.25F);
    EXPECT_FLOAT_EQ(prop.getShininess(1), 0.5F);
    EXPECT_EQ(prop.getImagePath(0), "/tex/one.png");
    EXPECT_EQ(prop.getUuid(0), "card-1");
    EXPECT_TRUE(prop.getImagePath(1).empty());
    // the ambient colour was uniform in the file and belongs to the base
    EXPECT_FALSE(prop.variesInAmbient());
}

/** Reading a document from after the alpha component changed meaning
 *
 * The fork means opacity by the alpha now, as 1.1 does, so a 1.1 file's
 * colours arrive without conversion -- but its per-entry transparency lives
 * in the field their renderer reads, with a vestigial 1.0 in the alpha, so
 * the field is taken as the truth and the stored alpha becomes its
 * complement. docs/ShapeAppearanceDesign.md 7.9.
 */
TEST_F(PropertyAppearanceListTest, anOpacityEraFileHasItsTransparencyMoved)
{
    const std::vector<std::pair<uint32_t, float>> entries {{0xff0000ffU, 0.25F},
                                                           {0x00ff00ffU, 0.75F}};
    App::PropertyAppearanceList prop;
    restoreFromXML(prop, "<MaterialList file=\"m.bin\" version=\"3\"/>\n", "1.1R41234");
    restoreBinaryDocFile(prop, opacityEraDocFile(entries), "1.1R41234");

    ASSERT_EQ(prop.getSize(), 2);
    // the colour itself is untouched
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue() >> 8, 0xff0000U);
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue() >> 8, 0x00ff00U);
    // and its alpha is the transparency field's complement, not the file's
    // vestigial 0xff
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(0).a, 0.75F);
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(1).a, 0.25F);
    EXPECT_FLOAT_EQ(prop.getTransparency(0), 0.25F);
    EXPECT_FLOAT_EQ(prop.getTransparency(1), 0.75F);
    // their opaque is 0xff and so is ours: no conversion for their era
    EXPECT_FLOAT_EQ(prop.getAmbientColor(0).a, 1.0F);
    EXPECT_FLOAT_EQ(prop.getSpecularColor(1).a, 1.0F);
}

TEST_F(PropertyAppearanceListTest, theSameBytesUnderAnOldVersionReadAsLegacy)
{
    // The control, and the point of the whole gate: the encoding says nothing
    // about the convention, only the release that wrote the document does.
    // Under a legacy version these bytes mean something else entirely: an
    // alpha of 0xff was the legacy spelling of INVISIBLE, and the merge
    // takes the larger of the two transparency-meaning slots.
    const std::vector<std::pair<uint32_t, float>> entries {{0xff0000ffU, 0.25F},
                                                           {0x00ff00ffU, 0.75F}};
    App::PropertyAppearanceList prop;
    restoreFromXML(prop, "<MaterialList file=\"m.bin\" version=\"3\"/>\n", "0.22R38472");
    restoreBinaryDocFile(prop, opacityEraDocFile(entries), "0.22R38472");

    ASSERT_EQ(prop.getSize(), 2);
    // alpha slot says 1.0 transparent, the field says less: the alpha wins
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(0).a, 0.0F);
    EXPECT_FLOAT_EQ(prop.getTransparency(0), 1.0F);
    // and the legacy ambient byte 0xff converts to invisible
    EXPECT_FLOAT_EQ(prop.getAmbientColor(0).a, 0.0F);
}

TEST_F(PropertyAppearanceListTest, aFileStatingNoVersionReadsAsLegacy)
{
    // 'pre-0.14' is the stand-in a reader fills in for a document with no
    // ProgramVersion attribute, and upstream's own table classifies it as
    // newer than every release it knows. Fail closed: unreadable means old,
    // because the oldest files there are all predate the change.
    const std::vector<std::pair<uint32_t, float>> entries {{0xff0000ffU, 0.25F}};
    App::PropertyAppearanceList prop;
    restoreFromXML(prop, "<MaterialList file=\"m.bin\" version=\"3\"/>\n", "pre-0.14");
    restoreBinaryDocFile(prop, opacityEraDocFile(entries), "pre-0.14");

    ASSERT_EQ(prop.getSize(), 1);
    // the legacy reading of these bytes, as the test above spells out
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(0).a, 0.0F);
}

TEST_F(PropertyAppearanceListTest, theInlineEncodingIsConvertedToo)
{
    // An inline list is read during the XML pass and an archive entry after
    // it, and the two have disagreed about a restore before now. Same file
    // era as anOpacityEraFileHasItsTransparencyMoved, same answers.
    const std::vector<std::pair<uint32_t, float>> entries {{0xff0000ffU, 0.25F},
                                                           {0x00ff00ffU, 0.75F}};
    App::PropertyAppearanceList prop;
    restoreFromXML(prop, opacityEraElement(entries), "1.1R41234");

    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue() >> 8, 0x00ff00U);
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(0).a, 0.75F);
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(1).a, 0.25F);
    EXPECT_FLOAT_EQ(prop.getAmbientColor(0).a, 1.0F);
}

TEST_F(PropertyAppearanceListTest, aConvertedUniformListStillCollapses)
{
    // The conversion runs over the fields and then renormalises. If it left
    // them expanded, two lists that mean the same thing would stop
    // serialising the same way and the shared-default scheme would quietly
    // stop eliding appearances.
    const std::vector<std::pair<uint32_t, float>> entries {{0xff0000ffU, 0.5F},
                                                           {0xff0000ffU, 0.5F},
                                                           {0xff0000ffU, 0.5F}};
    App::PropertyAppearanceList prop;
    restoreFromXML(prop, "<MaterialList file=\"m.bin\" version=\"3\"/>\n", "1.1R41234");
    restoreBinaryDocFile(prop, opacityEraDocFile(entries), "1.1R41234");

    ASSERT_EQ(prop.getSize(), 3);
    EXPECT_FALSE(prop.hasOverrides());
    // 0.5 either way round: the entry's transparency is 0.5 and so is the
    // stored alpha, its complement.
    EXPECT_FLOAT_EQ(prop.getTransparency(2), 0.5F);
    EXPECT_FLOAT_EQ(prop.getDiffuseColor(2).a, 0.5F);
}

//**************************************************************************
// PBR mode: the same arrays reinterpreted, the mode riding the encodings
// that can carry it and converted out of the ones that cannot.

TEST_F(PropertyAppearanceListTest, pbrReadsTheSameSlotsItsOwnWay)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(3, redMaterial()));
    // Phong mode: no metals, roughness derived from the shininess
    EXPECT_FLOAT_EQ(prop.getMetallic(1), 0.0F);
    EXPECT_FLOAT_EQ(prop.getRoughness(1),
                    App::MaterialAppearance::shininessToRoughness(prop.getShininess(1)));

    prop.setPBR(true);
    ASSERT_TRUE(prop.isPBR());
    // The unset fields read as the PBR defaults -- dielectric under a white
    // tint at mid roughness -- NOT as the Phong default specular, whose
    // alpha of one would spell full metal
    EXPECT_FLOAT_EQ(prop.getMetallic(1), 0.0F);
    EXPECT_FLOAT_EQ(prop.getRoughness(1), 0.5F);
    EXPECT_EQ(prop.getSpecularColor(1).getPackedValue() >> 8, 0xffffffU);

    prop.setMetallic(1, 1.0F);
    prop.setRoughness(1, 0.25F);
    EXPECT_FLOAT_EQ(prop.getMetallic(0), 0.0F);
    EXPECT_FLOAT_EQ(prop.getMetallic(1), 1.0F);
    EXPECT_FLOAT_EQ(prop.getRoughness(1), 0.25F);
    // the same arrays, reinterpreted: the raw slots show the PBR values
    EXPECT_FLOAT_EQ(prop.getSpecularColor(1).a, 1.0F);
    EXPECT_FLOAT_EQ(prop.getShininess(1), 0.25F);
}

TEST_F(PropertyAppearanceListTest, pbrSettersDemandTheMode)
{
    // In Phong mode the slots these writers land in mean something else; a
    // caller holding a metallic value has decided the mode and must say so
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    EXPECT_THROW(prop.setMetallic(0.5F), Base::RuntimeError);
    EXPECT_THROW(prop.setMetallic(0, 0.5F), Base::RuntimeError);
    EXPECT_THROW(prop.setMetallicValues({0.1F, 0.2F}), Base::RuntimeError);
    EXPECT_THROW(prop.setRoughness(0.5F), Base::RuntimeError);
    EXPECT_THROW(prop.setRoughness(0, 0.5F), Base::RuntimeError);
    EXPECT_THROW(prop.setRoughnessValues({0.1F, 0.2F}), Base::RuntimeError);

    prop.setPBR(true);
    prop.setMetallic(1.0F);
    EXPECT_FLOAT_EQ(prop.getMetallic(1), 1.0F);
}

TEST_F(PropertyAppearanceListTest, aPBRListGrowsDielectric)
{
    App::PropertyAppearanceList prop;
    prop.setPBR(true);
    prop.setRoughnessValues({0.25F, 0.75F});
    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_FLOAT_EQ(prop.getMetallic(0), 0.0F);

    prop.setSize(4);
    EXPECT_FLOAT_EQ(prop.getMetallic(3), 0.0F);
    EXPECT_FLOAT_EQ(prop.getRoughness(3), 0.5F);
    // and none of that made the untouched specular field an override
    EXPECT_FALSE(prop.variesInSpecular());
}

TEST_F(PropertyAppearanceListTest, pbrRoundTripsTheFieldEncodings)
{
    App::PropertyAppearanceList prop;
    prop.setSize(3);
    prop.setPBR(true);
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff), packed(0x0000ffff)});
    prop.setMetallicValues({0.0F, 1.0F, 0.2F});
    prop.setRoughnessValues({0.25F, 0.75F, 0.5F});

    const std::string xml = saveToXML(prop, 5);
    EXPECT_NE(xml.find("pbr=\"1\""), std::string::npos) << xml;

    for (const bool binary : {false, true}) {
        App::PropertyAppearanceList restored;
        if (binary) {
            restoreDocFile(restored, saveDocFile(prop, 5));
        }
        else {
            restoreFromXML(restored, xml);
        }
        ASSERT_TRUE(restored.isPBR());
        // roughness is the float slot: full precision both ways
        EXPECT_FLOAT_EQ(restored.getRoughness(0), 0.25F);
        EXPECT_FLOAT_EQ(restored.getRoughness(1), 0.75F);
        // metallic rides an 8-bit alpha: 0 and 1 are exact, anything else
        // is within one step (the legacy alpha flip is float arithmetic)
        EXPECT_FLOAT_EQ(restored.getMetallic(0), 0.0F);
        EXPECT_FLOAT_EQ(restored.getMetallic(1), 1.0F);
        EXPECT_NEAR(restored.getMetallic(2), 0.2F, ALPHA_STEP);
        EXPECT_EQ(restored.getDiffuseColor(2).getPackedValue() >> 8, 0x0000ffU);
    }
}

TEST_F(PropertyAppearanceListTest, anOldSchemaSaveWritesThePhongDerivation)
{
    // The compatible encodings cannot state the mode, so they state the
    // Phong look the values most nearly mean
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    prop.setPBR(true);
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff)});
    prop.setMetallicValues({1.0F, 0.0F});
    prop.setRoughnessValues({0.5F, 0.5F});

    for (const bool binary : {false, true}) {
        App::PropertyAppearanceList restored;
        if (binary) {
            restoreDocFile(restored, saveDocFile(prop, 4));
        }
        else {
            restoreFromXML(restored, saveToXML(prop, 4));
        }
        ASSERT_FALSE(restored.isPBR());
        // the metal: its colour lands in the specular; the diffuse stays
        // the base colour (see getPhongMaterial for why)
        EXPECT_EQ(restored.getDiffuseColor(0).getPackedValue() >> 8, 0xff0000U);
        EXPECT_EQ(restored.getSpecularColor(0).getPackedValue() >> 8, 0xff0000U);
        // the dielectric keeps its diffuse and gets the 0.04-scaled tint
        EXPECT_EQ(restored.getDiffuseColor(1).getPackedValue() >> 8, 0x00ff00U);
        EXPECT_EQ(restored.getSpecularColor(1).getPackedValue() >> 8,
                  App::Color(0.04F, 0.04F, 0.04F).getPackedValue() >> 8);
        // and the shininess slots hold the converted roughness
        EXPECT_NEAR(restored.getShininess(0),
                    App::MaterialAppearance::roughnessToShininess(0.5F), 1e-6);
    }
}

TEST_F(PropertyAppearanceListTest, theModeIsPartOfTheSerialisedIdentity)
{
    // Two lists whose fields match byte for byte must not elide into one
    // another across modes under the shared-default scheme
    App::PropertyAppearanceList phong;
    phong.setValues(std::vector<App::MaterialAppearance>(10, redMaterial()));
    App::PropertyAppearanceList pbr;
    pbr.setValues(std::vector<App::MaterialAppearance>(10, redMaterial()));
    pbr.setPBR(true);

    EXPECT_FALSE(phong.isSame(pbr));
    EXPECT_NE(saveToXML(phong, 5), saveToXML(pbr, 5));
    EXPECT_NE(saveDocFile(phong, 5), saveDocFile(pbr, 5));
}

TEST_F(PropertyAppearanceListTest, copyAndPasteCarryTheMode)
{
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    prop.setPBR(true);
    prop.setMetallic(1.0F);

    std::unique_ptr<App::Property> copy(prop.Copy());
    App::PropertyAppearanceList pasted;
    pasted.Paste(*copy);
    EXPECT_TRUE(pasted.isPBR());
    EXPECT_FLOAT_EQ(pasted.getMetallic(0), 1.0F);
    EXPECT_TRUE(pasted.isSame(prop));
}

TEST_F(PropertyAppearanceListTest, aPhongEraRestoreResetsTheMode)
{
    // Restoring what an older document holds over a property currently in
    // PBR mode must land in Phong mode, whichever encoding it arrives in
    App::PropertyAppearanceList prop;
    prop.setSize(1);
    prop.setPBR(true);
    restoreFromXML(prop, opacityEraElement({{0xff0000ffU, 0.0F}}));
    EXPECT_FALSE(prop.isPBR());

    prop.setPBR(true);
    App::PropertyAppearanceList phong;
    phong.setValues(std::vector<App::MaterialAppearance>(2, redMaterial()));
    restoreDocFile(prop, saveDocFile(phong, 5));
    EXPECT_FALSE(prop.isPBR());
}

TEST_F(PropertyAppearanceListTest, materialValuesCarryTheMode)
{
    // Every material handed out is stamped with the list's mode, so whoever
    // holds the value still knows which reading its slots are in -- and the
    // tag is part of material equality, so a Phong value never quietly
    // stands in for a PBR one
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(2, redMaterial()));
    EXPECT_FALSE(prop.getMaterial(0).pbr);

    prop.setPBR(true);
    prop.setMetallic(1.0F);
    prop.setRoughness(0.25F);
    App::MaterialAppearance raw = prop.getMaterial(0);
    EXPECT_TRUE(raw.pbr);
    // the value-level readings agree with the list's
    EXPECT_FLOAT_EQ(raw.getMetallic(), 1.0F);
    EXPECT_FLOAT_EQ(raw.getRoughness(), 0.25F);
    // the Phong derivation is a Phong value
    EXPECT_FALSE(prop.getPhongMaterial(0).pbr);

    // the tag alone tells the values apart
    App::MaterialAppearance retagged = raw;
    retagged.pbr = false;
    EXPECT_NE(raw, retagged);

    App::MaterialAppearance phong = redMaterial();
    EXPECT_NE(raw, phong);
    // a Phong value has no metals, and its roughness is the derivation
    EXPECT_FLOAT_EQ(phong.getMetallic(), 0.0F);
    EXPECT_FLOAT_EQ(phong.getRoughness(),
                    App::MaterialAppearance::shininessToRoughness(phong.shininess));
}

TEST_F(PropertyAppearanceListTest, aValueConvertsWhenItsModeIsSet)
{
    // The mode is an attribute of the value like any other, and setting it
    // converts: the surface keeps looking like itself in the other model
    App::MaterialAppearance mat = redMaterial();
    mat.shininess = 0.9F;
    const App::Color base = mat.diffuseColor;

    mat.setPBR(true);
    EXPECT_TRUE(mat.pbr);
    EXPECT_EQ(mat.diffuseColor.getPackedValue(), base.getPackedValue());
    EXPECT_FLOAT_EQ(mat.getMetallic(), 0.0F);
    EXPECT_FLOAT_EQ(mat.getRoughness(), App::MaterialAppearance::shininessToRoughness(0.9F));
    EXPECT_EQ(mat.specularColor.getPackedValue() >> 8, 0xffffffU);

    mat.setPBR(false);
    EXPECT_FALSE(mat.pbr);
    EXPECT_NEAR(mat.shininess, 0.9F, 1e-6);
    EXPECT_EQ(mat.diffuseColor.getPackedValue(), base.getPackedValue());

    // and stating a PBR quantity decides the mode rather than landing a
    // number in a slot that means something else
    App::MaterialAppearance metal = redMaterial();
    metal.setMetallic(1.0F);
    EXPECT_TRUE(metal.pbr);
    EXPECT_FLOAT_EQ(metal.getMetallic(), 1.0F);
    App::MaterialAppearance rough = redMaterial();
    rough.setRoughness(0.25F);
    EXPECT_TRUE(rough.pbr);
    EXPECT_FLOAT_EQ(rough.getRoughness(), 0.25F);
}

TEST_F(PropertyAppearanceListTest, theListTakesItsModeFromTheFirstMaterialAssigned)
{
    // A list holds ONE mode. A whole-list assignment states it through the
    // material it starts with, and every further entry is converted to
    // that reading rather than stored under the wrong one.
    App::MaterialAppearance pbrMat = redMaterial();
    pbrMat.setPBR(true);
    pbrMat.setMetallic(1.0F);
    pbrMat.setRoughness(0.25F);
    App::MaterialAppearance phongMat = redMaterial();
    phongMat.shininess = 0.9F;

    App::PropertyAppearanceList prop;
    prop.setValues({pbrMat, phongMat});
    ASSERT_TRUE(prop.isPBR());
    EXPECT_FLOAT_EQ(prop.getMetallic(0), 1.0F);
    // the Phong straggler converted: dielectric at the fitted roughness
    EXPECT_FLOAT_EQ(prop.getMetallic(1), 0.0F);
    EXPECT_FLOAT_EQ(prop.getRoughness(1), App::MaterialAppearance::shininessToRoughness(0.9F));
    EXPECT_TRUE(prop.getMaterial(1).pbr);

    // the other way round, and a single value states the mode too
    prop.setValue(phongMat);
    ASSERT_FALSE(prop.isPBR());
    EXPECT_NEAR(prop.getShininess(0), 0.9F, 1e-6);

    // one entry cannot restate the mode, so it is converted into the list
    prop.setValues({phongMat, phongMat});
    prop.set1Value(1, pbrMat);
    EXPECT_FALSE(prop.isPBR());
    EXPECT_NEAR(prop.getShininess(1),
                App::MaterialAppearance::roughnessToShininess(0.25F), 1e-6);
    // the metal's colour lands in the specular, as the Phong derivation says
    EXPECT_EQ(prop.getSpecularColor(1).getPackedValue() >> 8,
              prop.getDiffuseColor(1).getPackedValue() >> 8);

    // growth fills entries of THIS list, so the filler converts as well
    App::PropertyAppearanceList grown;
    grown.setValue(pbrMat);
    ASSERT_TRUE(grown.isPBR());
    grown.setSize(3, phongMat);
    EXPECT_TRUE(grown.isPBR());
    EXPECT_FLOAT_EQ(grown.getRoughness(2), App::MaterialAppearance::shininessToRoughness(0.9F));

    // an empty assignment states nothing: the mode it finds stands
    grown.setValues({});
    EXPECT_TRUE(grown.isPBR());
}

TEST_F(PropertyAppearanceListTest, convertPBRKeepsTheLook)
{
    // The editor's toggle: unlike setPBR it converts the stored values, so
    // the surface keeps looking like itself in the other model
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(3, redMaterial()));
    prop.setShininess(0.9F);
    prop.setDiffuseColor(1, packed(0x00ff00ff));
    const App::Color diffuse0 = prop.getDiffuseColor(0);
    const App::Color diffuse1 = prop.getDiffuseColor(1);

    prop.convertPBR(true);
    ASSERT_TRUE(prop.isPBR());
    // base colour = the Phong diffuse, per entry
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), diffuse0.getPackedValue());
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue(), diffuse1.getPackedValue());
    // a Phong surface converts dielectric, at the fitted roughness
    EXPECT_FLOAT_EQ(prop.getMetallic(0), 0.0F);
    EXPECT_FLOAT_EQ(prop.getRoughness(0), App::MaterialAppearance::shininessToRoughness(0.9F));
    EXPECT_EQ(prop.getSpecularColor(0).getPackedValue() >> 8, 0xffffffU);

    prop.convertPBR(false);
    ASSERT_FALSE(prop.isPBR());
    // the round trip keeps the look: diffuse and shininess return exactly
    // (the fit is invertible over the Phong range) -- only the specular
    // colour is forgotten, which Phong alone can state
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue(), diffuse1.getPackedValue());
    EXPECT_NEAR(prop.getShininess(0), 0.9F, 1e-6);
    EXPECT_EQ(prop.getSpecularColor(0).getPackedValue() >> 8,
              App::Color(0.04F, 0.04F, 0.04F).getPackedValue() >> 8);

    // a no-op when the mode already matches
    const std::string before = saveToXML(prop, 5);
    prop.convertPBR(false);
    EXPECT_EQ(saveToXML(prop, 5), before);
}

TEST_F(PropertyAppearanceListTest, rgbWritesLeaveTheAlphasAlone)
{
    // The dialog's colour edits: the diffuse alpha is the opacity and the
    // PBR specular alpha the metallic, so a colour edit writes rgb only.
    //
    // It is a whole-object write, so it moves the BASE and the faces that
    // hold a colour of their own keep it (docs/ShapeAppearanceDesign.md
    // 12.2). A per-entry transparency IS a per-entry diffuse alpha, so
    // stating one makes that face hold its whole colour -- which is what
    // makes entries 1 and 2 below keep theirs.
    App::PropertyAppearanceList prop;
    prop.setPBR(true);
    prop.setSize(3);
    prop.setTransparencies({0.0F, 0.5F, 0.25F});
    prop.setMetallicValues({0.0F, 1.0F, 0.5F});
    const App::Color wasDiffuse = prop.getDiffuseColor(1);
    const App::Color wasSpecular = prop.getSpecularColor(1);

    prop.setDiffuseRGB(packed(0x00ff00ff));
    prop.setSpecularRGB(packed(0xff8000ff));
    // entry 0 states neither alpha, so it follows the base
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue() >> 8, 0x00ff00U);
    EXPECT_EQ(prop.getSpecularColor(0).getPackedValue() >> 8, 0xff8000U);
    EXPECT_EQ(prop.getBase().diffuseColor.getPackedValue() >> 8, 0x00ff00U);
    EXPECT_EQ(prop.getBase().specularColor.getPackedValue() >> 8, 0xff8000U);
    for (int i : {1, 2}) {
        EXPECT_EQ(prop.getDiffuseColor(i).getPackedValue() >> 8,
                  wasDiffuse.getPackedValue() >> 8) << i;
        EXPECT_EQ(prop.getSpecularColor(i).getPackedValue() >> 8,
                  wasSpecular.getPackedValue() >> 8) << i;
    }
    // and the alphas, which is what this is really about, are untouched
    EXPECT_FLOAT_EQ(prop.getTransparency(1), 0.5F);
    EXPECT_FLOAT_EQ(prop.getTransparency(2), 0.25F);
    EXPECT_FLOAT_EQ(prop.getMetallic(0), 0.0F);
    EXPECT_FLOAT_EQ(prop.getMetallic(1), 1.0F);
    EXPECT_FLOAT_EQ(prop.getMetallic(2), 0.5F);

    // on a collapsed field it stays collapsed
    App::PropertyAppearanceList uniform;
    uniform.setSize(4);
    uniform.setTransparency(0.5F);
    uniform.setDiffuseRGB(packed(0x123456ff));
    EXPECT_FALSE(uniform.hasOverrides());
    EXPECT_FLOAT_EQ(uniform.getTransparency(3), 0.5F);
    EXPECT_EQ(uniform.getDiffuseColor(3).getPackedValue() >> 8, 0x123456U);
}

//**************************************************************************
// Surface finish (docs/ShapeAppearanceDesign.md section 9)

TEST_F(PropertyAppearanceListTest, aFinishRidesTheBaseLikeEveryOtherField)
{
    App::PropertyAppearanceList prop;
    prop.setSize(6);
    EXPECT_FALSE(prop.hasFinish());
    EXPECT_FALSE(prop.variesInFinish());   // unset costs nothing
    EXPECT_EQ(prop.getFinish(3).pattern, App::SurfaceFinish::None);

    prop.setFinish(brushedFinish());
    EXPECT_TRUE(prop.hasFinish());
    EXPECT_FALSE(prop.variesInFinish());   // the object's, stated once
    EXPECT_EQ(prop.getFinish(5).pattern, App::SurfaceFinish::Brushed);
    EXPECT_TRUE(prop.variesOnlyInDiffuse());

    prop.setFinish(2, knurlFinish());
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({2}));
    EXPECT_EQ(prop.getFinishOverrides().size(), 1U);
    EXPECT_EQ(prop.getFinish(2).pattern, App::SurfaceFinish::Knurl);
    EXPECT_EQ(prop.getFinish(1).pattern, App::SurfaceFinish::Brushed);
    // a finish that varies is something a colour list cannot say
    EXPECT_FALSE(prop.variesOnlyInDiffuse());

    // and back: giving it the base's finish drops the override
    prop.setFinish(2, brushedFinish());
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.variesOnlyInDiffuse());

    // clearing it empties the field rather than storing six Nones
    prop.setFinish(App::SurfaceFinish());
    EXPECT_FALSE(prop.hasFinish());
    EXPECT_FALSE(prop.variesInFinish());
}

TEST_F(PropertyAppearanceListTest, aFinishIsClampedOnTheWayIn)
{
    App::PropertyAppearanceList prop;
    prop.setSize(1);

    App::SurfaceFinish odd;
    odd.pattern = App::SurfaceFinish::Turned;
    odd.pitch = 0.0F;     // a zero pitch is a divisor, not a finish
    odd.depth = -1.0F;
    odd.angle = 190.0F;   // a lay has an axis, not a direction
    prop.setFinish(0, odd);

    const App::SurfaceFinish stored = prop.getFinish(0);
    EXPECT_EQ(stored.pattern, App::SurfaceFinish::Turned);
    EXPECT_FLOAT_EQ(stored.pitch, App::SurfaceFinish::MinPitch);
    EXPECT_FLOAT_EQ(stored.depth, 0.0F);
    EXPECT_FLOAT_EQ(stored.angle, 10.0F);

    // no pattern means no numbers, so the record can elide
    App::SurfaceFinish none;
    none.pitch = 3.0F;
    none.depth = 2.0F;
    prop.setFinish(0, none);
    EXPECT_FLOAT_EQ(prop.getFinish(0).pitch, 0.0F);
    EXPECT_FALSE(prop.hasFinish());
}

TEST_F(PropertyAppearanceListTest, aFinishRidesAWholeMaterialBothWays)
{
    App::MaterialAppearance mat = redMaterial();
    mat.finish = knurlFinish();

    App::PropertyAppearanceList prop;
    prop.setValues({mat, redMaterial()});
    EXPECT_EQ(prop.getFinish(0).pattern, App::SurfaceFinish::Knurl);
    EXPECT_EQ(prop.getFinish(1).pattern, App::SurfaceFinish::None);
    EXPECT_EQ(prop.getMaterial(0).finish, knurlFinish());
    EXPECT_EQ(prop.getMaterial(0), mat);   // equality includes the finish

    // a preset states the whole material, and none of them states a finish
    App::MaterialAppearance preset = mat;
    preset.setType(App::MaterialAppearance::STEEL);
    EXPECT_EQ(preset.finish.pattern, App::SurfaceFinish::None);
    // ... but USER_DEFINED states nothing, here as for the colours
    App::MaterialAppearance kept = mat;
    kept.setType(App::MaterialAppearance::USER_DEFINED);
    EXPECT_EQ(kept.finish, knurlFinish());
}

TEST_F(PropertyAppearanceListTest, aTextureIsUnsetUntilASlotIsFilled)
{
    App::SurfaceTexture texture;
    EXPECT_FALSE(texture.isSet());

    // A transform with nothing to transform states nothing, so it is not
    // a set record and normalize takes the numbers back out
    texture.scale[0] = 4.0F;
    texture.offset[1] = 0.75F;
    texture.rotation = 45.0F;
    EXPECT_FALSE(texture.isSet());
    texture.normalize();
    EXPECT_EQ(texture, App::SurfaceTexture());

    // Any one slot is enough, and then the transform means something
    texture.maps[App::SurfaceTexture::Occlusion] = "abc";
    texture.rotation = 45.0F;
    EXPECT_TRUE(texture.isSet());
    texture.normalize();
    EXPECT_FLOAT_EQ(texture.rotation, 45.0F);
}

TEST_F(PropertyAppearanceListTest, aTextureTransformIsClampedOnTheWayIn)
{
    App::SurfaceTexture texture;
    texture.maps[App::SurfaceTexture::BaseColor] = "abc";
    texture.scale[0] = std::numeric_limits<float>::quiet_NaN();
    texture.scale[1] = std::numeric_limits<float>::infinity();
    texture.offset[0] = std::numeric_limits<float>::quiet_NaN();
    texture.rotation = 400.0F;   // a texture rotation is a direction
    texture.normalize();

    EXPECT_FLOAT_EQ(texture.scale[0], 1.0F);
    EXPECT_FLOAT_EQ(texture.scale[1], 1.0F);
    EXPECT_FLOAT_EQ(texture.offset[0], 0.0F);
    EXPECT_FLOAT_EQ(texture.rotation, 40.0F);

    // ... so the full turn, unlike the finish lay's half
    texture.rotation = -30.0F;
    texture.normalize();
    EXPECT_FLOAT_EQ(texture.rotation, 330.0F);
}

TEST_F(PropertyAppearanceListTest, everyTextureSlotRoundTripsThroughItsName)
{
    for (uint8_t slot = 0; slot < App::SurfaceTexture::SlotCount; ++slot) {
        const char* name = App::SurfaceTexture::slotName(slot);
        EXPECT_STRNE(name, "") << int(slot);
        EXPECT_EQ(App::SurfaceTexture::slotFromName(name), slot) << name;
    }
    // A slot this build does not know is skipped, not named wrongly
    EXPECT_STREQ(App::SurfaceTexture::slotName(App::SurfaceTexture::SlotCount), "");
    EXPECT_EQ(App::SurfaceTexture::slotFromName("clearcoat"),
              App::SurfaceTexture::SlotCount);
    EXPECT_EQ(App::SurfaceTexture::slotFromName(""), App::SurfaceTexture::SlotCount);
    EXPECT_EQ(App::SurfaceTexture::slotFromName(nullptr), App::SurfaceTexture::SlotCount);
}

TEST_F(PropertyAppearanceListTest, aTextureRidesAWholeMaterialBothWays)
{
    App::MaterialAppearance mat = redMaterial();
    mat.texture = oakTexture();

    App::MaterialAppearance plain = redMaterial();
    EXPECT_NE(mat, plain);   // equality includes the texture

    // Two slots naming the same content are the same statement
    App::MaterialAppearance same = redMaterial();
    same.texture = oakTexture();
    EXPECT_EQ(mat, same);

    // a preset states the whole material, and none of them states a texture
    App::MaterialAppearance preset = mat;
    preset.setType(App::MaterialAppearance::STEEL);
    EXPECT_FALSE(preset.texture.isSet());
    // ... but USER_DEFINED states nothing, here as for the finish
    App::MaterialAppearance kept = mat;
    kept.setType(App::MaterialAppearance::USER_DEFINED);
    EXPECT_EQ(kept.texture, oakTexture());
}

TEST_F(PropertyAppearanceListTest, aTextureFieldCostsNothingUntilSomethingStatesOne)
{
    App::PropertyAppearanceList prop;
    prop.setSize(5);
    EXPECT_FALSE(prop.hasTexture());
    EXPECT_EQ(prop.getTexturePalette().size(), 0U);
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
    EXPECT_FALSE(prop.getTexture(3).isSet());

    // The object's: the base holds it, and no face states anything
    prop.setTexture(oakTexture());
    EXPECT_TRUE(prop.hasTexture());
    EXPECT_EQ(prop.getBase().texture, oakTexture());
    EXPECT_EQ(prop.getTexturePalette().size(), 0U);
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(prop.getTexture(i), oakTexture()) << i;

    // Back to the default everywhere, and the storage goes with it
    prop.setTexture(App::SurfaceTexture());
    EXPECT_FALSE(prop.hasTexture());
    EXPECT_EQ(prop.getTexturePalette().size(), 0U);
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
}

TEST_F(PropertyAppearanceListTest, oneOddEntryCostsTwoBytesNotARecordPerEntry)
{
    // The whole point of the palette: an odd face among many must not
    // materialise a record for every other face
    App::PropertyAppearanceList prop;
    prop.setSize(1000);
    prop.setTexture(oakTexture());

    App::SurfaceTexture odd;
    odd.maps[App::SurfaceTexture::Emissive] = "odd-one-out";
    prop.setTexture(700, odd);

    // One overriding face, one palette slot, one index entry -- where the
    // dense form would have written a slot for every one of the thousand
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({700}));
    EXPECT_EQ(prop.getTexturePalette().size(), 1U);
    EXPECT_EQ(prop.getTextureIndex().size(), 1U);
    EXPECT_EQ(prop.getTexture(699), oakTexture());
    EXPECT_EQ(prop.getTexture(700), odd);
    EXPECT_EQ(prop.getTexture(701), oakTexture());

    // ... and putting it back collapses the whole thing again
    prop.setTexture(700, oakTexture());
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_EQ(prop.getTexturePalette().size(), 0U);
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
    EXPECT_EQ(prop.getTexture(700), oakTexture());
}

TEST_F(PropertyAppearanceListTest, aTexturePaletteHoldsOnlyWhatIsDistinct)
{
    App::SurfaceTexture a;
    a.maps[App::SurfaceTexture::BaseColor] = "aaa";
    App::SurfaceTexture b;
    b.maps[App::SurfaceTexture::BaseColor] = "bbb";

    App::PropertyAppearanceList prop;
    // Six entries, three distinct values, one of them the default
    prop.setTextures({a, b, a, App::SurfaceTexture(), b, a});
    EXPECT_EQ(prop.getSize(), 6);
    // Nothing has said which of the three the object is, so every entry
    // states its own over an unset base
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({0, 1, 2, 4, 5}));
    EXPECT_EQ(prop.getTexturePalette().size(), 2U);
    EXPECT_EQ(prop.getTextureIndex().size(), 5U);
    // First-use order, which is what makes the stored form canonical
    EXPECT_EQ(prop.getTexturePalette()[0], a);
    EXPECT_EQ(prop.getTexturePalette()[1], b);
    const std::vector<uint16_t> expected {0, 1, 0, 1, 0};
    EXPECT_EQ(prop.getTextureIndex(), expected);
    EXPECT_EQ(prop.getTexture(2), a);
    EXPECT_FALSE(prop.getTexture(3).isSet());

    // A run that is all one value is the base saying it, index and all
    prop.setTextures({b, b, b, b, b, b});
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_EQ(prop.getTexturePalette().size(), 0U);
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
    EXPECT_EQ(prop.getTexture(4), b);
}

TEST_F(PropertyAppearanceListTest, aTextureRidesTheWholeMaterialThroughTheList)
{
    App::MaterialAppearance textured = redMaterial();
    textured.texture = oakTexture();

    App::PropertyAppearanceList prop;
    prop.setValues({textured, redMaterial(), textured});
    EXPECT_EQ(prop.getTexture(0), oakTexture());
    EXPECT_FALSE(prop.getTexture(1).isSet());
    EXPECT_EQ(prop.getMaterial(2).texture, oakTexture());
    // Two entries share one palette slot, the third states nothing
    EXPECT_EQ(prop.getTexturePalette().size(), 1U);
    EXPECT_EQ(prop.getTextureIndex().size(), 2U);

    // set1Value reads back through the same storage, and once every entry
    // agrees the texture belongs to the object
    prop.set1Value(1, textured);
    EXPECT_EQ(prop.getTexture(1), oakTexture());
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_EQ(prop.getBase().texture, oakTexture());
    EXPECT_EQ(prop.getTexturePalette().size(), 0U);
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
}

TEST_F(PropertyAppearanceListTest, aTexturePaletteFollowsTheEntryCount)
{
    App::SurfaceTexture a;
    a.maps[App::SurfaceTexture::Normal] = "aaa";

    App::PropertyAppearanceList prop;
    prop.setSize(3);
    prop.setTexture(1, a);
    EXPECT_EQ(prop.getTextureIndex().size(), 1U);

    // Growth adds faces that read the base, so the index does not move
    prop.setSize(6);
    EXPECT_EQ(prop.getTextureIndex().size(), 1U);
    EXPECT_EQ(prop.getTexture(1), a);
    EXPECT_FALSE(prop.getTexture(5).isSet());

    // Shrinking past the odd entry leaves nothing varying, so the whole
    // field collapses away rather than keeping a dead palette slot
    prop.setSize(1);
    EXPECT_FALSE(prop.hasTexture());
    EXPECT_EQ(prop.getTextureIndex().size(), 0U);
}

TEST_F(PropertyAppearanceListTest, aTextureFieldIsPartOfTheListsIdentity)
{
    App::SurfaceTexture a;
    a.maps[App::SurfaceTexture::Occlusion] = "aaa";

    App::PropertyAppearanceList one;
    one.setSize(4);
    one.setTexture(2, a);

    App::PropertyAppearanceList two;
    two.setSize(4);
    EXPECT_FALSE(one.isSame(two));

    two.setTexture(2, a);
    EXPECT_TRUE(one.isSame(two));

    // ... and Copy/Paste carry it
    std::unique_ptr<App::Property> copy(one.Copy());
    App::PropertyAppearanceList three;
    three.Paste(*copy);
    EXPECT_TRUE(three.isSame(one));
    EXPECT_EQ(three.getTexture(2), a);

    // A field that varies is something a plain colour list cannot say
    EXPECT_FALSE(one.variesOnlyInDiffuse());
    App::PropertyAppearanceList uniform;
    uniform.setSize(4);
    uniform.setTexture(a);
    EXPECT_TRUE(uniform.variesOnlyInDiffuse());
}

TEST_F(PropertyAppearanceListTest, aTextureSurvivesTheModeConversions)
{
    // Which image is pasted on a surface is not a reading of the shading
    // slots, so nothing about the mode may touch it
    App::MaterialAppearance mat = redMaterial();
    mat.texture = oakTexture();

    EXPECT_EQ(App::MaterialAppearance::phongToPbr(mat).texture, oakTexture());
    EXPECT_EQ(App::MaterialAppearance::pbrToPhong(App::MaterialAppearance::phongToPbr(mat)).texture,
              oakTexture());

    App::MaterialAppearance converted = mat;
    converted.setPBR(true);
    EXPECT_EQ(converted.texture, oakTexture());
    converted.setPBR(false);
    EXPECT_EQ(converted.texture, oakTexture());
}

TEST_F(PropertyAppearanceListTest, aFinishSurvivesTheModeConversions)
{
    // A finish is a statement about the surface, not a reading of the
    // shading slots, so nothing about the mode may touch it
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    prop.setFinish(0, knurlFinish());
    prop.setFinish(1, brushedFinish());

    prop.convertPBR(true);
    EXPECT_EQ(prop.getFinish(0), knurlFinish());
    EXPECT_EQ(prop.getFinish(1), brushedFinish());
    prop.convertPBR(false);
    EXPECT_EQ(prop.getFinish(0), knurlFinish());

    App::MaterialAppearance mat = redMaterial();
    mat.finish = brushedFinish();
    mat.setPBR(true);
    EXPECT_EQ(mat.finish, brushedFinish());
    EXPECT_EQ(App::MaterialAppearance::pbrToPhong(mat).finish, brushedFinish());
    EXPECT_EQ(App::MaterialAppearance::phongToPbr(redMaterial()).finish.pattern,
              App::SurfaceFinish::None);
}

TEST_F(PropertyAppearanceListTest, aFinishRoundTripsBothCompactEncodings)
{
    App::PropertyAppearanceList prop;
    prop.setSize(3);
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff), packed(0x0000ffff)});
    prop.setFinish(0, knurlFinish());
    prop.setFinish(2, brushedFinish());

    for (bool asXML : {true, false}) {
        App::PropertyAppearanceList back;
        if (asXML) {
            restoreFromXML(back, saveToXML(prop, 5));
        }
        else {
            restoreDocFile(back, saveDocFile(prop, 5));
        }
        ASSERT_EQ(back.getSize(), 3) << asXML;
        EXPECT_EQ(back.getFinish(0), knurlFinish()) << asXML;
        EXPECT_EQ(back.getFinish(1).pattern, App::SurfaceFinish::None) << asXML;
        EXPECT_EQ(back.getFinish(2), brushedFinish()) << asXML;
        EXPECT_EQ(back.getDiffuseColor(1).getPackedValue(), 0x00ff00ffU) << asXML;
        EXPECT_TRUE(back.isSame(prop)) << asXML;
    }
}

TEST_F(PropertyAppearanceListTest, aFinishRidesItsOwnElementBelowSchemaSix)
{
    // The material encodings below schema 5 are upstream's and cannot state a
    // finish. Rather than give up their compatibility for it, the finish goes
    // out as an element of its own beside them -- which upstream's reader
    // walks past, since readElement skips elements it did not ask for.
    App::PropertyAppearanceList prop;
    prop.setSize(3);
    prop.setDiffuseColor(packed(0x804020ff));
    const std::string plain = saveToXML(prop, 4);

    prop.setFinish(0, knurlFinish());
    prop.setFinish(2, brushedFinish());
    const std::string xml = saveToXML(prop, 4);
    EXPECT_NE(xml.find("<SurfaceFinishList count=\"3\""), std::string::npos) << xml;
    // and the material element itself is untouched: the bytes upstream reads
    // are the bytes it always read
    const std::size_t closed = xml.find("</SurfaceFinishList>");
    ASSERT_NE(closed, std::string::npos);
    const std::string materialPart = xml.substr(xml.find('<', closed + 1));
    EXPECT_EQ(materialPart, plain.substr(plain.find('<'))) << materialPart;

    App::PropertyAppearanceList back;
    restoreFromXML(back, xml);
    ASSERT_EQ(back.getSize(), 3);
    EXPECT_EQ(back.getFinish(0), knurlFinish());
    EXPECT_EQ(back.getFinish(1).pattern, App::SurfaceFinish::None);
    EXPECT_EQ(back.getFinish(2), brushedFinish());
    EXPECT_EQ(back.getDiffuseColor(0).getPackedValue(), 0x804020ffU);
}

TEST_F(PropertyAppearanceListTest, nothingExtraIsWrittenAtSchemaSix)
{
    // At schema 5 the per field encoding states the finish itself, so there
    // is no second element at all -- not an empty one.
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    prop.setFinish(knurlFinish());

    const std::string xml = saveToXML(prop, 5);
    EXPECT_EQ(xml.find("SurfaceFinishList"), std::string::npos) << xml;

    App::PropertyAppearanceList back;
    restoreFromXML(back, xml);
    EXPECT_EQ(back.getFinish(1), knurlFinish());
}

TEST_F(PropertyAppearanceListTest, anArchivedFinishWaitsForItsMaterials)
{
    // ⚠️ The archive entry is read long after the XML pass, and reading it
    // CLEARS the finish field -- the compatible stream cannot state one. So
    // the finish restored from the companion element has to wait for the
    // materials rather than land when it was read.
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values {texturedMaterial(), redMaterial()};
    values[1].finish = knurlFinish();   // the finish is part of the material
    prop.setValues(values);

    Base::StringWriter element;
    element.setSchemaVersion(4);
    element.setPreferBinary(true);
    element.setForceXML(0);
    prop.Save(element);
    EXPECT_NE(element.getString().find("SurfaceFinishList"), std::string::npos);
    EXPECT_NE(element.getString().find("file="), std::string::npos);

    Base::StringWriter file;
    file.setSchemaVersion(4);
    file.setPreferBinary(true);
    file.setForceXML(0);
    prop.SaveDocFile(file);

    App::PropertyAppearanceList restored;
    restoreFromXML(restored, element.getString());
    std::istringstream stream(file.getString());
    Base::Reader reader(stream, "material.bin");
    restored.RestoreDocFile(reader);
    expectEntries(restored, values);
    EXPECT_EQ(restored.getFinish(1), knurlFinish());
    EXPECT_EQ(restored.getFinish(0).pattern, App::SurfaceFinish::None);
}

TEST_F(PropertyAppearanceListTest, internalPropertyTypesCannotBeAddedByAUser)
{
    // A type that only works as one container's member is marked by a leading
    // underscore on its registered name, because Base::Type has nowhere to
    // record such a thing
    EXPECT_TRUE(App::Property::isInternalType("Gui::_PropertyShapeColor"));
    EXPECT_TRUE(App::Property::isInternalType("App::_PropertySurfaceFinishList"));
    EXPECT_TRUE(App::Property::isInternalType("_PropertyNoNamespace"));
    EXPECT_FALSE(App::Property::isInternalType("App::PropertyAppearanceList"));
    EXPECT_FALSE(App::Property::isInternalType("App::PropertyFloat"));
    EXPECT_FALSE(App::Property::isInternalType(""));
    EXPECT_FALSE(App::Property::isInternalType(nullptr));
}

TEST_F(PropertyAppearanceListTest, aPlainMaterialPropertyCarriesTheFinishToo)
{
    // The single-value property is the other place a whole material is
    // stored, and a field it silently dropped would be a trap waiting for
    // whoever first stores a finish there.
    App::MaterialAppearance mat = redMaterial();
    mat.finish = knurlFinish();
    App::PropertyAppearance prop;
    prop.setValue(mat);

    Base::StringWriter writer;
    writer.setForceXML(1);
    prop.Save(writer);
    const std::string xml = writer.getString();
    EXPECT_NE(xml.find("finish="), std::string::npos);

    App::PropertyAppearance back;
    std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?><document>)" + xml + "</document>";
    std::istringstream stream(doc);
    Base::XMLReader reader("material.xml", stream);
    back.Restore(reader);
    EXPECT_EQ(back.getValue().finish, knurlFinish());

    // and an unfinished material writes exactly the bytes it always did
    App::PropertyAppearance plain;
    plain.setValue(redMaterial());
    Base::StringWriter plainWriter;
    plainWriter.setForceXML(1);
    plain.Save(plainWriter);
    EXPECT_EQ(plainWriter.getString().find("finish="), std::string::npos);
}

TEST_F(PropertyAppearanceListTest, aRunFromALaterBuildIsReadAndDropped)
{
    // Forward compatibility (docs/ShapeAppearanceDesign.md 9.4.1): a field
    // added after this build must not make the document unreadable. Built
    // by hand, because the claim is about bytes this writer cannot produce.
    std::ostringstream file;
    auto put = [&file](unsigned long value) { file << value << '\n'; };
    // A run head is its shape, its BYTE LENGTH and its entry count -- the
    // length being what a reader steps over when it understands neither
    auto run = [&file, &put](unsigned long type, unsigned long count,
                             const std::string& payload) {
        put(type);
        put(payload.size());
        put(count);
        file << payload;
    };
    auto num = [](float value) {
        std::ostringstream s;
        s << value << '\n';
        return s.str();
    };
    put(0xffffffffUL);   // FieldStreamMarker: what follows is per field
    put(2);              // entry count
    // FieldDiffuse | FieldFinish | a field this build has never heard of.
    // One rather than two: bits 13 and 14 are the base and the follow flag
    // now, and 15 is the last free one -- an unknown RUN SHAPE, which the
    // same byte length gets past, is what the test below this one covers.
    put((1U << 1) | (1U << 11) | (1U << 15));
    // A doc file read with no document version behind it reads as legacy, so
    // the alpha byte means TRANSPARENCY here: 0 is opaque
    run(0, 2, "4278190080\n16711680\n");   // RunColors
    run(4, 2, num(App::SurfaceFinish::Brushed) + num(0.05F) + num(0.002F) + num(30.0F)
                  + num(App::SurfaceFinish::Blasted) + num(0.02F) + num(0.004F)
                  + num(0.0F));            // RunFinish
    run(1, 2, num(1.5F) + num(2.5F));      // the unknown field, as floats

    App::PropertyAppearanceList prop;
    ASSERT_NO_THROW(restoreDocFile(prop, file.str()));
    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0xff0000ffU);
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue(), 0x00ff00ffU);
    EXPECT_EQ(prop.getFinish(0).pattern, App::SurfaceFinish::Brushed);
    EXPECT_FLOAT_EQ(prop.getFinish(0).angle, 30.0F);
    EXPECT_EQ(prop.getFinish(1).pattern, App::SurfaceFinish::Blasted);
}

TEST_F(PropertyAppearanceListTest, aRunShapeFromALaterBuildIsSteppedOver)
{
    // The half a run's shape byte alone cannot buy: a record KIND added
    // after this build. Only the byte length in the head gets past it --
    // without one the rest of the stream was consumed as garbage
    // (docs/ShapeAppearanceDesign.md 9.4.2).
    std::ostringstream file;
    auto put = [&file](unsigned long value) { file << value << '\n'; };
    auto run = [&file, &put](unsigned long type, unsigned long count,
                             const std::string& payload) {
        put(type);
        put(payload.size());
        put(count);
        file << payload;
    };
    auto num = [](float value) {
        std::ostringstream s;
        s << value << '\n';
        return s.str();
    };
    put(0xffffffffUL);
    put(2);
    // FieldDiffuse, then FieldType carrying a shape this build has never
    // heard of, then FieldFinish behind it -- which is the one that used to
    // be lost along with everything after it
    put((1U << 1) | (1U << 6) | (1U << 11));
    run(0, 2, "4278190080\n16711680\n");
    run(97, 2, "whatever this is\nand however long it runs\n");
    run(4, 2, num(App::SurfaceFinish::Brushed) + num(0.05F) + num(0.002F) + num(30.0F)
                  + num(App::SurfaceFinish::Blasted) + num(0.02F) + num(0.004F)
                  + num(0.0F));

    App::PropertyAppearanceList prop;
    ASSERT_NO_THROW(restoreDocFile(prop, file.str()));
    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0xff0000ffU);
    // The unknown shape was dropped, and the field behind it still landed
    EXPECT_EQ(prop.getType(0), App::MaterialAppearance::USER_DEFINED);
    EXPECT_EQ(prop.getFinish(0).pattern, App::SurfaceFinish::Brushed);
    EXPECT_EQ(prop.getFinish(1).pattern, App::SurfaceFinish::Blasted);
}

TEST_F(PropertyAppearanceListTest, aTextureRoundTripsBothForkEncodings)
{
    App::SurfaceTexture other;
    other.maps[App::SurfaceTexture::Emissive] = "e-hash";
    other.maps[App::SurfaceTexture::MetallicRoughness] = "mr-hash";
    other.scale[0] = 0.5F;
    other.rotation = 270.0F;

    App::PropertyAppearanceList prop;
    prop.setSize(4);
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff),
                           packed(0x0000ffff), packed(0xffffffff)});
    // Every entry differs in its colour, so every one is an override --
    // including the one whose texture is the base's, which is what the
    // third (unset) palette slot is
    prop.setTextures({oakTexture(), other, oakTexture(), App::SurfaceTexture()});
    ASSERT_EQ(prop.getTexturePalette().size(), 3U);

    for (bool asXML : {true, false}) {
        App::PropertyAppearanceList back;
        if (asXML) {
            restoreFromXML(back, saveToXML(prop, 5));
        }
        else {
            restoreDocFile(back, saveDocFile(prop, 5));
        }
        ASSERT_EQ(back.getSize(), 4) << asXML;
        EXPECT_EQ(back.getTexture(0), oakTexture()) << asXML;
        EXPECT_EQ(back.getTexture(1), other) << asXML;
        EXPECT_EQ(back.getTexture(2), oakTexture()) << asXML;
        EXPECT_FALSE(back.getTexture(3).isSet()) << asXML;
        // Shared content is one palette slot, coming back as it went out
        EXPECT_EQ(back.getTexturePalette().size(), 3U) << asXML;
        EXPECT_EQ(back.getTextureIndex().size(), 4U) << asXML;
        EXPECT_TRUE(back.isSame(prop)) << asXML;
    }
}

TEST_F(PropertyAppearanceListTest, aUniformTextureCostsNoIndexOnTheWireEither)
{
    App::PropertyAppearanceList prop;
    prop.setSize(500);
    prop.setTexture(oakTexture());

    for (bool asXML : {true, false}) {
        App::PropertyAppearanceList back;
        if (asXML) {
            restoreFromXML(back, saveToXML(prop, 5));
        }
        else {
            // One record and no index, whatever the entry count: the whole
            // field is well under what 500 two-byte slots would cost
            EXPECT_LT(saveDocFile(prop, 5).size(), 500U);
            restoreDocFile(back, saveDocFile(prop, 5));
        }
        ASSERT_EQ(back.getSize(), 500) << asXML;
        EXPECT_FALSE(back.hasOverrides()) << asXML;
        EXPECT_EQ(back.getBase().texture, oakTexture()) << asXML;
        EXPECT_EQ(back.getTexture(499), oakTexture()) << asXML;
    }
}

TEST_F(PropertyAppearanceListTest, aTextureRidesItsOwnElementBelowSchemaFive)
{
    // At schema 4 the material encodings are upstream's and have nowhere to
    // put a texture. Rather than give up their compatibility for it, it goes
    // beside them (docs/ShapeAppearanceDesign.md 9.4.1).
    App::SurfaceTexture other;
    other.maps[App::SurfaceTexture::Emissive] = "e-hash";

    App::PropertyAppearanceList prop;
    prop.setSize(3);
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff), packed(0x0000ffff)});
    prop.setTextures({oakTexture(), other, oakTexture()});

    const std::string xml = saveToXML(prop, 4);
    EXPECT_NE(xml.find("<SurfaceTextureList count=\"2\""), std::string::npos) << xml;
    // Its own element, ahead of the material one and closed before it -- so
    // a reader that asks for the material element by name walks past it
    const std::size_t closed = xml.find("</SurfaceTextureList>");
    ASSERT_NE(closed, std::string::npos) << xml;
    EXPECT_LT(closed, xml.find("<MaterialList")) << xml;

    App::PropertyAppearanceList back;
    restoreFromXML(back, xml);
    ASSERT_EQ(back.getSize(), 3);
    EXPECT_EQ(back.getTexture(0), oakTexture());
    EXPECT_EQ(back.getTexture(1), other);
    EXPECT_EQ(back.getTexture(2), oakTexture());
    EXPECT_EQ(back.getTexturePalette().size(), 2U);
}

TEST_F(PropertyAppearanceListTest, nothingExtraIsWrittenForATextureAtSchemaFive)
{
    // At 5 the field form carries it, so no document holds it twice
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    prop.setTexture(oakTexture());
    EXPECT_EQ(saveToXML(prop, 5).find("SurfaceTextureList"), std::string::npos);
    // ... and an appearance with no texture writes no companion at either
    App::PropertyAppearanceList plain;
    plain.setSize(2);
    EXPECT_EQ(saveToXML(plain, 4).find("SurfaceTextureList"), std::string::npos);
}

TEST_F(PropertyAppearanceListTest, anArchivedTextureWaitsForItsMaterials)
{
    // Below schema 5 the values go to an archive entry read long after the
    // XML pass, and that read clears the texture field -- so the companion
    // has to wait for it rather than land on the spot
    App::PropertyAppearanceList prop;
    prop.setSize(2);
    prop.setImagePath(0, "/tmp/oak.png");   // what sends it down the file route
    prop.setTexture(0, oakTexture());

    Base::StringWriter element;
    element.setSchemaVersion(4);
    element.setPreferBinary(true);
    element.setForceXML(0);
    prop.Save(element);
    ASSERT_NE(element.getString().find("SurfaceTextureList"), std::string::npos)
            << element.getString();
    ASSERT_NE(element.getString().find("file="), std::string::npos)
            << element.getString();

    Base::StringWriter file;
    file.setSchemaVersion(4);
    file.setPreferBinary(true);
    file.setForceXML(0);
    prop.SaveDocFile(file);

    App::PropertyAppearanceList back;
    restoreFromXML(back, element.getString());
    std::istringstream stream(file.getString());
    Base::Reader reader(stream, "material.bin");
    back.RestoreDocFile(reader);

    ASSERT_EQ(back.getSize(), 2);
    EXPECT_EQ(back.getTexture(0), oakTexture());
    EXPECT_FALSE(back.getTexture(1).isSet());
    EXPECT_EQ(back.getImagePath(0), "/tmp/oak.png");
}

namespace
{

/// A file in the system temp directory holding exactly \a content
std::string writeTempFile(const std::string& content)
{
    const std::string path = Base::FileInfo::getTempFileName("texture") + ".bin";
    std::ofstream out(path, std::ios::binary);
    out << content;
    out.close();
    return path;
}

}  // namespace

TEST_F(PropertyAppearanceListTest, textureContentIsHeldByTheHashItIsNamedBy)
{
    // No container, so this is the process-wide store rather than a
    // document's -- the reference counting is the same either way
    const std::string oakPath = writeTempFile("this is an oak plank");
    const std::string steelPath = writeTempFile("this is brushed steel");

    App::PropertyAppearanceList prop;
    prop.setSize(2);
    const std::string oakHash = prop.insertTextureFile(oakPath.c_str());
    const std::string steelHash = prop.insertTextureFile(steelPath.c_str());
    ASSERT_FALSE(oakHash.empty());
    ASSERT_FALSE(steelHash.empty());
    EXPECT_NE(oakHash, steelHash);

    // The property holds the content, so the file is on disk and findable
    // by the hash a slot spells it with
    EXPECT_FALSE(prop.getTextureFile(oakHash).empty());
    EXPECT_TRUE(Base::FileInfo(prop.getTextureFile(oakHash)).exists());
    EXPECT_TRUE(prop.getTextureFile("no such hash").empty());

    // Same content twice is one blob, whatever the file it arrived in
    const std::string copyPath = writeTempFile("this is an oak plank");
    EXPECT_EQ(prop.insertTextureFile(copyPath.c_str()), oakHash);

    App::SurfaceTexture texture;
    texture.maps[App::SurfaceTexture::BaseColor] = oakHash;
    texture.maps[App::SurfaceTexture::Normal] = steelHash;
    prop.setTexture(texture);
    EXPECT_EQ(prop.getTexture(1).maps[App::SurfaceTexture::BaseColor], oakHash);

    Base::FileInfo(oakPath).deleteFile();
    Base::FileInfo(steelPath).deleteFile();
    Base::FileInfo(copyPath).deleteFile();
}

TEST_F(PropertyAppearanceListTest, aRestoredBlobFindsItsSlotByHashNotByOrder)
{
    // The whole cost of being the first multi-blob referrer
    // (docs/ShapeAppearanceDesign.md 10.2): addPendingReferrer serves an
    // already-read hash IMMEDIATELY and queues the rest, so the handles
    // come back in an order that has nothing to do with the order asked in.
    const std::string firstPath = writeTempFile("content of the first map");
    const std::string secondPath = writeTempFile("content of the second map");

    App::PropertyAppearanceList source;
    source.setSize(1);
    const std::string firstHash = source.insertTextureFile(firstPath.c_str());
    const std::string secondHash = source.insertTextureFile(secondPath.c_str());
    ASSERT_FALSE(firstHash.empty());
    ASSERT_FALSE(secondHash.empty());

    auto& manager = App::FileBlobManager::defaultManager();
    const App::FileBlobHandle first = manager.find(firstHash);
    const App::FileBlobHandle second = manager.find(secondHash);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    App::SurfaceTexture texture;
    texture.maps[App::SurfaceTexture::BaseColor] = firstHash;
    texture.maps[App::SurfaceTexture::Normal] = secondHash;
    // Both slots over one content, which has to be assigned to both
    texture.maps[App::SurfaceTexture::Emissive] = firstHash;

    App::PropertyAppearanceList restored;
    restored.setSize(1);
    restored.setTexture(texture);
    EXPECT_TRUE(restored.getTextureFile(firstHash).empty());

    // Backwards, which is exactly what the manager may do
    restored.assignRestoredBlob(second);
    restored.assignRestoredBlob(first);

    EXPECT_EQ(restored.getTextureFile(firstHash), first->path());
    EXPECT_EQ(restored.getTextureFile(secondHash), second->path());
    // Idempotent, which is what lets a duplicated queue entry be harmless
    restored.assignRestoredBlob(first);
    EXPECT_EQ(restored.getTextureFile(firstHash), first->path());

    Base::FileInfo(firstPath).deleteFile();
    Base::FileInfo(secondPath).deleteFile();
}

TEST_F(PropertyAppearanceListTest, aTextureClaimFollowsWhatThePaletteNames)
{
    const std::string path = writeTempFile("content nothing will name for long");

    App::PropertyAppearanceList prop;
    prop.setSize(2);
    const std::string hash = prop.insertTextureFile(path.c_str());
    ASSERT_FALSE(hash.empty());

    App::SurfaceTexture texture;
    texture.maps[App::SurfaceTexture::Occlusion] = hash;
    prop.setTexture(texture);
    EXPECT_FALSE(prop.getTextureFile(hash).empty());

    // A whole-list assignment states the palette in full, and the claim
    // goes with it
    prop.setTextures({App::SurfaceTexture(), App::SurfaceTexture()});
    EXPECT_FALSE(prop.hasTexture());
    EXPECT_TRUE(prop.getTextureFile(hash).empty());

    Base::FileInfo(path).deleteFile();
}

TEST_F(PropertyAppearanceListTest, aTextureSlotFromALaterBuildIsReadAndDropped)
{
    // A record states how many slots it carries, so a build with one map
    // more than this one still writes a palette this one can read
    std::ostringstream file;
    auto put = [&file](unsigned long value) { file << value << '\n'; };
    auto run = [&file, &put](unsigned long type, unsigned long count,
                             const std::string& payload) {
        put(type);
        put(payload.size());
        put(count);
        file << payload;
    };
    // A string over a TEXT-mode stream is its embedded newline count, a
    // colon, the text, and a closing newline (Base::OutputStream)
    auto str = [](const std::string& value) { return "0:" + value + "\n"; };
    std::ostringstream texture;
    texture << App::SurfaceTexture::SlotCount + 1 << '\n';   // one slot more
    texture << 1 << '\n';                                    // one palette entry
    for (unsigned slot = 0; slot < App::SurfaceTexture::SlotCount; ++slot) {
        texture << str("hash-" + std::to_string(slot));
    }
    texture << str("clearcoat");                             // the slot we lack
    texture << 1.0F << '\n' << 1.0F << '\n' << 0.0F << '\n' << 0.0F << '\n'
            << 0.0F << '\n';
    texture << 0 << '\n';                                    // no index: uniform

    put(0xffffffffUL);
    put(2);
    put(1U << 12);   // FieldTexture alone
    run(5, 1, texture.str());

    App::PropertyAppearanceList prop;
    ASSERT_NO_THROW(restoreDocFile(prop, file.str()));
    ASSERT_EQ(prop.getSize(), 2);
    ASSERT_TRUE(prop.hasTexture());
    for (unsigned slot = 0; slot < App::SurfaceTexture::SlotCount; ++slot) {
        EXPECT_EQ(prop.getTexture(0).maps[slot], "hash-" + std::to_string(slot)) << slot;
    }
}

TEST_F(PropertyAppearanceListTest, aKeyFromALaterBuildIsSteppedOver)
{
    // The XML half of the same rule, and the sharper case: the unknown key
    // sits BETWEEN two known ones, so it is skipped by its token count
    // rather than by being last.
    const std::string xml =
        R"(<MaterialList count="3" fields="1">)"
        "\n"
        // alpha means transparency in a file this old, so 00 is opaque
        "d 3 ff000000 00ff0000 0000ff00\n"
        "z 6 1 2 3 4 5 6\n"          // a field added later, six tokens
        "f 12 1 0.8 0.3 45 0 0 0 0 3 0.05 0.002 30\n"
        "h 1 0.75\n"
        "</MaterialList>";

    App::PropertyAppearanceList prop;
    ASSERT_NO_THROW(restoreFromXML(prop, xml));
    ASSERT_EQ(prop.getSize(), 3);
    EXPECT_EQ(prop.getDiffuseColor(2).getPackedValue(), 0x0000ffffU);
    EXPECT_FLOAT_EQ(prop.getShininess(0), 0.75F);
    EXPECT_EQ(prop.getFinish(0).pattern, App::SurfaceFinish::Knurl);
    EXPECT_FLOAT_EQ(prop.getFinish(0).pitch, 0.8F);
    EXPECT_EQ(prop.getFinish(1).pattern, App::SurfaceFinish::None);
    EXPECT_EQ(prop.getFinish(2).pattern, App::SurfaceFinish::Brushed);
    EXPECT_FLOAT_EQ(prop.getFinish(2).angle, 30.0F);
}

//--------------------------------------------------------------------------
// The value is shared and copied on write (docs/PythonValueBindings.md)
//
// The storage lives in App::AppearanceList now, and copying one costs a
// pointer. What these hold to account is the two halves of that bargain:
// nobody sees another holder's write, and a write that changes nothing
// leaves the storage exactly as it found it -- which is how the property
// decides whether there is a change to record at all.
//--------------------------------------------------------------------------

TEST_F(PropertyAppearanceListTest, aCopyOfTheValueSharesUntilOneOfThemWrites)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(1000, redMaterial()));

    App::AppearanceList copy = prop.getList();
    EXPECT_TRUE(prop.getList().isShared());
    EXPECT_TRUE(copy.isSameData(prop.getList()));
    EXPECT_EQ(copy.getSize(), 1000);

    // Reading never detaches, however much of it there is
    for (int i = 0; i < 1000; ++i) {
        EXPECT_TRUE(prop.getMaterial(i) == redMaterial()) << "entry " << i;
    }
    EXPECT_TRUE(prop.getList().isShared());

    App::MaterialAppearance blue;
    blue.diffuseColor = packed(0x0000ffff);
    prop.setDiffuseColor(500, blue.diffuseColor);

    EXPECT_FALSE(prop.getList().isShared());
    EXPECT_FALSE(copy.isSameData(prop.getList()));
    // the copy still says what it said
    EXPECT_EQ(copy.getDiffuseColor(500).getPackedValue(), 0xff0000ffU);
    EXPECT_EQ(prop.getDiffuseColor(500).getPackedValue(), 0x0000ffffU);
}

TEST_F(PropertyAppearanceListTest, anUndoSnapshotIsAPointerRatherThanTheWholeList)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(1000, redMaterial()));
    EXPECT_FALSE(prop.getList().isShared());

    // Copy() is what the transaction machinery calls before every change
    std::unique_ptr<App::Property> snapshot(prop.Copy());
    EXPECT_TRUE(prop.getList().isShared());

    auto *copied = dynamic_cast<App::PropertyAppearanceList *>(snapshot.get());
    ASSERT_NE(copied, nullptr);
    EXPECT_TRUE(copied->getList().isSameData(prop.getList()));

    prop.setDiffuseColor(0, packed(0x00ff00ff));
    EXPECT_FALSE(prop.getList().isShared());
    EXPECT_EQ(copied->getDiffuseColor(0).getPackedValue(), 0xff0000ffU);

    // and pasting it back is the same pointer trade in reverse
    prop.Paste(*copied);
    EXPECT_TRUE(prop.getList().isSameData(copied->getList()));
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0xff0000ffU);
}

TEST_F(PropertyAppearanceListTest, aWriteThatChangesNothingLeavesTheStorageAlone)
{
    // The property reads "did anything change" off the storage identity, so
    // a setter that decides nothing changed must not detach -- otherwise
    // every no-op write would record an undo step and touch the document.
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));

    const App::AppearanceList before = prop.getList();
    prop.setDiffuseColor(2, redMaterial().diffuseColor);
    EXPECT_TRUE(prop.getList().isSameData(before));
    prop.setDiffuseColor(redMaterial().diffuseColor);
    EXPECT_TRUE(prop.getList().isSameData(before));
    prop.setShininess(2, prop.getShininess(2));
    EXPECT_TRUE(prop.getList().isSameData(before));
    prop.setFinish(2, prop.getFinish(2));
    EXPECT_TRUE(prop.getList().isSameData(before));
    prop.setTexture(2, prop.getTexture(2));
    EXPECT_TRUE(prop.getList().isSameData(before));
    prop.setPBR(prop.isPBR());
    EXPECT_TRUE(prop.getList().isSameData(before));
    prop.setSize(4);
    EXPECT_TRUE(prop.getList().isSameData(before));

    // A WHOLE-LIST assignment is the exception, and always was: it rebuilds
    // every field rather than comparing first, so it records a change even
    // when the values match. Nothing here changes that -- it is the same
    // signal the property sent before the value moved out of it.
    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));
    EXPECT_FALSE(prop.getList().isSameData(before));

    // and a real entry write detaches too
    const App::AppearanceList reassigned = prop.getList();
    prop.setDiffuseColor(2, packed(0x00ff00ff));
    EXPECT_FALSE(prop.getList().isSameData(reassigned));
}

TEST_F(PropertyAppearanceListTest, anIndexedWriteRecordsThatIndexAndAWholeOneClearsThem)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));
    EXPECT_TRUE(prop.getTouchList().empty());

    prop.setDiffuseColor(2, packed(0x00ff00ff));
    EXPECT_EQ(prop.getTouchList(), std::set<int>({2}));
    prop.setShininess(1, 0.5F);
    EXPECT_EQ(prop.getTouchList(), std::set<int>({1, 2}));

    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));
    EXPECT_TRUE(prop.getTouchList().empty());
}

TEST_F(PropertyAppearanceListTest, contentTheListHoldsTravelsWithACopyOfIt)
{
    const std::string oakPath = writeTempFile("this is an oak plank");

    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(2, redMaterial()));
    const std::string hash = prop.insertTextureFile(oakPath.c_str());
    ASSERT_FALSE(hash.empty());

    App::MaterialAppearance textured = redMaterial();
    textured.texture.maps[App::SurfaceTexture::BaseColor] = hash;
    prop.set1Value(1, textured);

    // A copy names the same content, which is the point of a shared store:
    // the file lives while any handle to it does
    App::AppearanceList copy = prop.getList();
    EXPECT_FALSE(copy.getTextureFile(hash).empty());
    EXPECT_EQ(copy.getTexture(1).maps[App::SurfaceTexture::BaseColor], hash);

    // and taking content in is a claim rather than a value change, so it
    // does not detach the copy or record a change
    const std::string steelPath = writeTempFile("this is brushed steel");
    const std::string second = prop.insertTextureFile(steelPath.c_str());
    ASSERT_FALSE(second.empty());
    EXPECT_TRUE(copy.isSameData(prop.getList()));
    EXPECT_FALSE(copy.getTextureFile(second).empty());
}

//--------------------------------------------------------------------------
// What Python sees (docs/PythonValueBindings.md)
//
// The property hands Python a LIVE VIEW of its value rather than a tuple of
// copies, so vp.ShapeAppearance[0].DiffuseColor = c reaches the object --
// which it never did before. These run the chain end to end through the
// interpreter, because every link in it is a Python one: the item stamp on
// the value a sequence slot returns, the notification on the write, the
// item assignment it turns into, and the property's own change signalling.
//--------------------------------------------------------------------------

namespace
{

/// Run a snippet with \a prop bound to the name "mlist"
void runOn(App::PropertyAppearanceList& prop, const char* code)
{
    Base::PyGILStateLocker lock;
    PyObject* view = prop.getPyObject();
    ASSERT_NE(view, nullptr);
    Py::Module main(PyImport_AddModule("__main__"), false);
    main.setAttr("mlist", Py::Object(view, true));
    Base::Interpreter().runString(code);
}

/// The same, with nothing bound: for a snippet that keeps its own reference
void run(const char* code)
{
    Base::PyGILStateLocker lock;
    Base::Interpreter().runString(code);
}

}  // namespace

TEST_F(PropertyAppearanceListTest, aPythonWriteToOneEntryReachesTheProperty)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));

    // The whole point: a field written on the entry Python was handed goes
    // back into the property, at that index and nowhere else
    runOn(prop, "mlist[2].DiffuseColor = (0.0, 1.0, 0.0, 1.0)");
    EXPECT_EQ(prop.getDiffuseColor(2).getPackedValue(), 0x00ff00ffU);
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue(), 0xff0000ffU);
    EXPECT_EQ(prop.getSize(), 4);

    // and so does a whole-entry assignment
    runOn(prop, "import FreeCAD\n"
                "mat = FreeCAD.Material()\n"
                "mat.DiffuseColor = (0.0, 0.0, 1.0, 1.0)\n"
                "mlist[0] = mat\n");
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0x0000ffffU);
}

TEST_F(PropertyAppearanceListTest, aPythonListReadsWithoutCopyingTheStorage)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(1000, redMaterial()));

    // A view, not a tuple of a thousand materials: reading it neither
    // copies the storage nor detaches it
    const App::AppearanceList before = prop.getList();
    runOn(prop,
          "assert len(mlist) == 1000\n"
          "assert mlist.Count == 1000\n"
          "assert mlist.IsAttached\n"
          "assert len([m for m in mlist]) == 1000\n");
    EXPECT_TRUE(prop.getList().isSameData(before));
}

TEST_F(PropertyAppearanceListTest, aDetachedListIsAValueAndWritesNowhere)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(2, redMaterial()));

    runOn(prop, "import FreeCAD\n"
                "copy = mlist.copy()\n"
                "assert not copy.IsAttached\n"
                "mat = FreeCAD.Material()\n"
                "mat.DiffuseColor = (0.0, 1.0, 0.0, 1.0)\n"
                "copy[0] = mat\n"
                "assert copy[0].DiffuseColor[1] == 1.0\n");
    // the property is untouched by anything done to the copy
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0xff0000ffU);

    // and assigning it back is what writes
    Base::PyGILStateLocker lock;
    Py::Module main(PyImport_AddModule("__main__"), false);
    Py::Object copy = main.getAttr("copy");
    prop.setPyObject(copy.ptr());
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0x00ff00ffU);
    // the value assigned in is shared, not copied
    EXPECT_TRUE(prop.getList().isShared());
}

TEST_F(PropertyAppearanceListTest, aViewOfADeadPropertyIsStillReadable)
{
    {
        App::PropertyAppearanceList prop;
        prop.setValues(std::vector<App::MaterialAppearance>(3, redMaterial()));
        runOn(prop, "kept = mlist");
    }
    // The property took its views down with it, each keeping the value it
    // could still see. Reading one must answer, and writing one must reach
    // nothing at all rather than a freed property.
    run("assert kept.Count == 3\n"
        "assert not kept.IsAttached\n"
        "assert kept[0].DiffuseColor[0] == 1.0\n"
        "import FreeCAD\n"
        "kept[0] = FreeCAD.Material()\n");
}

TEST_F(PropertyAppearanceListTest, aReadOnlyPropertyHandsOutSomethingNothingWritesThrough)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(2, redMaterial()));
    prop.setStatus(App::Property::ReadOnly, true);

    runOn(prop, "import FreeCAD\n"
                "try:\n"
                "    mlist[0] = FreeCAD.Material()\n"
                "    raise AssertionError('a read-only list took a write')\n"
                "except ReferenceError:\n"
                "    pass\n");
    EXPECT_EQ(prop.getDiffuseColor(0).getPackedValue(), 0xff0000ffU);
}

TEST_F(PropertyAppearanceListTest, aTextureIsStatedFromPythonByFile)
{
    const std::string oakPath = writeTempFile("this is an oak plank");

    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(3, redMaterial()));

    // Two steps or one: content into the store, then a slot naming it --
    // and setTextureFile() is both, which is what a script wants
    runOn(prop,
          ("h = mlist.insertTextureFile(r'" + oakPath + "')\n"
           "assert len(h) > 8\n"
           "assert mlist.getTextureFile(h)\n"
           "mlist.setTexture(0, 'basecolor', h)\n"
           "assert mlist.getTexture(0)['basecolor'] == h\n"
           "assert 'basecolor' not in mlist.getTexture(1)\n"
           "n = mlist.setTextureFile(1, 'normal', r'" + oakPath + "')\n"
           "assert n == h\n"  // same content, same hash, one file
           "mlist.setTextureTransform(0, Scale=(2.0, 3.0), Rotation=45.0)\n")
              .c_str());

    const App::SurfaceTexture first = prop.getTexture(0);
    EXPECT_FALSE(first.maps[App::SurfaceTexture::BaseColor].empty());
    EXPECT_FLOAT_EQ(first.scale[1], 3.0F);
    EXPECT_FLOAT_EQ(first.rotation, 45.0F);
    EXPECT_EQ(prop.getTexture(1).maps[App::SurfaceTexture::Normal],
              first.maps[App::SurfaceTexture::BaseColor]);
    EXPECT_TRUE(prop.getTexture(2).maps[App::SurfaceTexture::BaseColor].empty());
    // one file for content named twice
    EXPECT_FALSE(prop.getTextureFile(first.maps[App::SurfaceTexture::BaseColor]).empty());

    runOn(prop, "mlist.clearTexture(0, 'basecolor')\n"
                "assert 'basecolor' not in mlist.getTexture(0)\n");
    EXPECT_TRUE(prop.getTexture(0).maps[App::SurfaceTexture::BaseColor].empty());
}

TEST_F(PropertyAppearanceListTest, aPythonFieldWriteNamesOneEntryOrEveryOne)
{
    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(4, redMaterial()));

    // With an index it is one entry; without one it is every entry, which
    // is why "every entry" is not spelled as an index at all
    runOn(prop, "mlist.setDiffuseColor(2, (0.0, 1.0, 0.0, 1.0))\n"
                "mlist.setShininess(0.5)\n");
    EXPECT_EQ(prop.getDiffuseColor(2).getPackedValue(), 0x00ff00ffU);
    EXPECT_EQ(prop.getDiffuseColor(1).getPackedValue(), 0xff0000ffU);
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(prop.getShininess(i), 0.5F) << "entry " << i;
    }

    runOn(prop, "assert abs(mlist.getDiffuseColor(2)[1] - 1.0) < 1e-6\n"
                "assert abs(mlist.getShininess(3) - 0.5) < 1e-6\n"
                "assert abs(mlist.getDiffuseColor(-3)[0] - 1.0) < 1e-6\n");

    // the PBR pair demands the mode, exactly as the C++ setters do
    runOn(prop, "try:\n"
                "    mlist.setMetallic(0.5)\n"
                "    raise AssertionError('a Phong list took a metallic factor')\n"
                "except Exception:\n"
                "    pass\n"
                "mlist.PBR = True\n"
                "mlist.setMetallic(1, 0.9)\n");
    EXPECT_TRUE(prop.isPBR());
    EXPECT_NEAR(prop.getMetallic(1), 0.9F, 0.01F);
}

//**************************************************************************
// The base entry and the overriding faces
// (docs/ShapeAppearanceDesign.md section 12)

TEST_F(PropertyAppearanceListTest, aDenseListHoldsItsBaseOpenUntilOneIsDerived)
{
    // Every encoding that states one entry at a time says nothing about
    // which of them the object is, so the list stands with every entry an
    // override over a default base -- which resolves correctly and costs
    // exactly what the dense form cost.
    App::MaterialAppearance green = redMaterial();
    green.diffuseColor = packed(0x00ff00ff);

    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(10, redMaterial());
    values[4] = green;
    prop.setValues(values);

    EXPECT_FALSE(prop.hasDerivedBase());
    EXPECT_EQ(prop.getOverrides().size(), 10U);
    expectEntries(prop, values);

    // With nothing to go on the most common entry wins, and every face
    // wearing it stops being an override
    prop.deriveBase();
    EXPECT_TRUE(prop.hasDerivedBase());
    EXPECT_TRUE(prop.getBase() == redMaterial());
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({4}));
    expectEntries(prop, values);

    // and it runs once: a second call is not a second answer
    prop.deriveBase();
    EXPECT_TRUE(prop.getBase() == redMaterial());
}

TEST_F(PropertyAppearanceListTest, theBaseIsTheMaterialCoveringTheLargestArea)
{
    // The board-and-pads case, which entry COUNT decides the wrong way: one
    // big green face and five small gold ones
    App::MaterialAppearance gold = redMaterial();
    gold.diffuseColor = packed(0xffd700ff);
    App::MaterialAppearance board = redMaterial();
    board.diffuseColor = packed(0x008000ff);

    std::vector<App::MaterialAppearance> values(6, gold);
    values[0] = board;
    const std::vector<double> areas {1000.0, 1.0, 1.0, 1.0, 1.0, 1.0};

    App::PropertyAppearanceList byArea;
    byArea.setValues(values);
    byArea.deriveBase(nullptr, &areas);
    EXPECT_TRUE(byArea.getBase() == board);
    EXPECT_EQ(byArea.getOverrides(), std::vector<uint32_t>({1, 2, 3, 4, 5}));
    expectEntries(byArea, values);

    // ... and by count it is the pads, which is why the area is asked for
    App::PropertyAppearanceList byCount;
    byCount.setValues(values);
    byCount.deriveBase();
    EXPECT_TRUE(byCount.getBase() == gold);
    expectEntries(byCount, values);
}

TEST_F(PropertyAppearanceListTest, theMirrorWinsWhenTheListNamesIt)
{
    // What a document this fork wrote before the base holds: the mirror is
    // the last uniform value, which is what the object looked like before
    // its faces were painted
    App::MaterialAppearance body = redMaterial();
    App::MaterialAppearance pad = redMaterial();
    pad.diffuseColor = packed(0x00ff00ff);

    std::vector<App::MaterialAppearance> values(6, pad);
    values[0] = body;
    values[1] = body;

    App::PropertyAppearanceList prop;
    prop.setValues(values);
    EXPECT_TRUE(prop.namesDiffuse(body.diffuseColor));
    const App::Color hint = body.diffuseColor;
    prop.deriveBase(&hint);
    // the mirror outranks the count, which would have chosen the pads
    EXPECT_TRUE(prop.getBase() == body);
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({2, 3, 4, 5}));
    expectEntries(prop, values);

    // A mirror the list does not hold declines, and the count decides
    App::PropertyAppearanceList other;
    other.setValues(values);
    const App::Color grey = App::MaterialAppearance().diffuseColor;
    EXPECT_FALSE(other.namesDiffuse(grey));
    other.deriveBase(&grey);
    EXPECT_TRUE(other.getBase() == pad);
}

TEST_F(PropertyAppearanceListTest, theSparseFormRoundTripsBothForkEncodings)
{
    App::MaterialAppearance painted = fullyPaintedMaterial();
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(200, redMaterial());
    values[3] = painted;
    values[100].diffuseColor = packed(0x00ff00ff);
    prop.setValues(values);
    prop.deriveBase();
    ASSERT_EQ(prop.getOverrides(), std::vector<uint32_t>({3, 100}));

    for (bool asXML : {true, false}) {
        App::PropertyAppearanceList back;
        if (asXML) {
            const std::string xml = saveToXML(prop, 5);
            EXPECT_NE(xml.find("\nb "), std::string::npos) << xml;
            EXPECT_NE(xml.find("\no 2 3 100\n"), std::string::npos) << xml;
            restoreFromXML(back, xml);
        }
        else {
            restoreDocFile(back, saveDocFile(prop, 5));
        }
        ASSERT_EQ(back.getSize(), 200) << asXML;
        // the base came back stated, so no heuristic runs on it again
        EXPECT_TRUE(back.hasDerivedBase()) << asXML;
        EXPECT_EQ(back.getOverrides(), std::vector<uint32_t>({3, 100})) << asXML;
        EXPECT_TRUE(back.isSame(prop)) << asXML;
        expectEntriesQ8(back, values);
    }
}

TEST_F(PropertyAppearanceListTest, aSparseListCostsTheFacesItPaints)
{
    // The claim of 12.3: a 10,000 face import with three painted faces is a
    // base and three colours, not 10,000 colours
    App::PropertyAppearanceList prop;
    prop.setSize(10000);
    prop.setDiffuseColor(packed(0x008000ff));
    for (int face : {17, 4096, 9999}) {
        prop.setDiffuseColor(face, packed(0xffd700ff));
    }
    EXPECT_EQ(prop.getOverrides().size(), 3U);
    EXPECT_LT(saveDocFile(prop, 5).size(), 400U);
    EXPECT_LT(saveToXML(prop, 5).size(), 600U);
}

TEST_F(PropertyAppearanceListTest, schemaFourStillWritesOneEntryPerFace)
{
    // The compatible encoding cannot state a base, so it resolves as it
    // goes -- which is what makes an old FreeCAD able to open the file
    App::PropertyAppearanceList prop;
    std::vector<App::MaterialAppearance> values(4, redMaterial());
    values[2].diffuseColor = packed(0x00ff00ff);
    prop.setValues(values);
    prop.deriveBase();
    ASSERT_EQ(prop.getOverrides().size(), 1U);

    const std::string xml = saveToXML(prop, 4);
    EXPECT_EQ(xml.find("fields="), std::string::npos) << xml;
    EXPECT_EQ(xml.find("\nb "), std::string::npos) << xml;

    App::PropertyAppearanceList back;
    restoreFromXML(back, xml);
    expectEntriesQ8(back, values);
    // and it comes back with no base in it, ready for the heuristic
    EXPECT_FALSE(back.hasDerivedBase());
}

TEST_F(PropertyAppearanceListTest, anOverrideListAFileStatesIsChecked)
{
    // Every number here came out of a file, so none of it is evidence: the
    // storage invariants of 12.6 are the format's checks
    const std::string head = "<MaterialList count=\"4\" fields=\"1\">\n";
    for (const char *body : {"o 2 3 1\n",          // not sorted
                             "o 2 1 9\n",          // names no entry
                             "o 1 1\nd 2 ff0000ff 00ff00ff\n"}) {  // too long a field
        App::PropertyAppearanceList prop;
        EXPECT_THROW(restoreFromXML(prop, head + body + "</MaterialList>\n"),
                     Base::Exception)
                << body;
    }
}

TEST_F(PropertyAppearanceListTest, anAppearanceCardKeepsThePaintedFaces)
{
    // 12.2, and the reason the base exists at all: assigning the object a
    // whole material must not collapse the faces the user painted
    App::MaterialAppearance painted = redMaterial();
    painted.diffuseColor = packed(0x00ff00ff);
    painted.shininess = 0.9F;

    App::PropertyAppearanceList prop;
    prop.setValues(std::vector<App::MaterialAppearance>(8, redMaterial()));
    prop.set1Value(5, painted);
    ASSERT_EQ(prop.getOverrides(), std::vector<uint32_t>({5}));

    App::MaterialAppearance card;
    card.diffuseColor = packed(0x0000ffff);
    card.shininess = 0.2F;
    prop.setBase(card);

    EXPECT_EQ(prop.getSize(), 8);
    EXPECT_EQ(prop.getOverrides(), std::vector<uint32_t>({5}));
    EXPECT_TRUE(prop.getMaterial(0).diffuseColor == packed(0x0000ffff));
    EXPECT_FLOAT_EQ(prop.getShininess(0), 0.2F);
    EXPECT_TRUE(prop.getMaterial(5).diffuseColor == packed(0x00ff00ff));
    EXPECT_FLOAT_EQ(prop.getShininess(5), 0.9F);

    // one face back to the base, and then all of them
    prop.clearOverride(5);
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getMaterial(5).diffuseColor == packed(0x0000ffff));
}

TEST_F(PropertyAppearanceListTest, aListOfOneEntryHasNoOverrides)
{
    // 12.6: that entry IS what the object looks like
    App::PropertyAppearanceList prop;
    prop.setSize(1);
    prop.setDiffuseColor(0, packed(0x00ff00ff));
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_TRUE(prop.getBase().diffuseColor == packed(0x00ff00ff));
}

TEST_F(PropertyAppearanceListTest, aFieldEveryFaceAgreesOnBelongsToTheObject)
{
    // What keeps a whole field landed one entry at a time from costing one
    // value per face: if nothing disagrees, it is the base's
    App::PropertyAppearanceList prop;
    prop.setSize(4);
    for (int i = 0; i < 4; ++i) {
        prop.setShininess(i, 0.75F);
    }
    EXPECT_FALSE(prop.hasOverrides());
    EXPECT_FLOAT_EQ(prop.getBase().shininess, 0.75F);
    EXPECT_FLOAT_EQ(prop.getShininess(2), 0.75F);
}

//**************************************************************************
// Following the object's material card (docs/MaterialStorage.md section 15)

TEST_F(PropertyAppearanceListTest, aWholeObjectWriteEndsTheFollow)
{
    App::MaterialAppearance card = redMaterial();
    card.shininess = 0.7F;

    App::PropertyAppearanceList prop;
    prop.setSize(6);
    prop.followMaterial(card);
    EXPECT_TRUE(prop.isFollowingMaterial());
    EXPECT_TRUE(prop.getBase() == card);

    // A per-face write does not touch the base, so it does not end it
    prop.setDiffuseColor(2, packed(0x00ff00ff));
    EXPECT_TRUE(prop.isFollowingMaterial());

    // ... and the card moving on carries the base with it, over the face
    // that holds its own
    App::MaterialAppearance moved = card;
    moved.diffuseColor = packed(0x0000ffff);
    prop.followMaterial(moved);
    EXPECT_TRUE(prop.isFollowingMaterial());
    EXPECT_TRUE(prop.getDiffuseColor(0) == packed(0x0000ffff));
    EXPECT_TRUE(prop.getDiffuseColor(2) == packed(0x00ff00ff));

    // A whole-object write is a look the user chose, and it outranks the
    // card from then on
    prop.setDiffuseColor(packed(0xffff00ff));
    EXPECT_FALSE(prop.isFollowingMaterial());

    // A whole-LIST assignment is not one of them: an import states one look
    // per face and says nothing about which card the object wears, and it
    // is exactly the imported part that has to be able to take a card and
    // keep its painted faces
    App::PropertyAppearanceList imported;
    imported.setValues(std::vector<App::MaterialAppearance>(6, redMaterial()));
    EXPECT_TRUE(imported.isFollowingMaterial());
}

TEST_F(PropertyAppearanceListTest, aFreshAppearanceIsWaitingForACard)
{
    // The flag starts true -- "a fresh object that carries a card follows
    // it" -- and that is the CLASS DEFAULT, so nothing about the elision of
    // an untouched appearance changes
    App::PropertyAppearanceList fresh;
    EXPECT_TRUE(fresh.isFollowingMaterial());
    fresh.setSize(1);
    EXPECT_TRUE(fresh.isFollowingMaterial());
    fresh.setValue(redMaterial());
    EXPECT_TRUE(fresh.isFollowingMaterial());
    fresh.setDiffuseColor(packed(0x00ff00ff));
    EXPECT_FALSE(fresh.isFollowingMaterial());
}

TEST_F(PropertyAppearanceListTest, theFollowFlagIsPartOfTheValue)
{
    App::PropertyAppearanceList following;
    following.setSize(3);
    following.followMaterial(redMaterial());

    App::PropertyAppearanceList custom;
    custom.setSize(3);
    custom.setBase(redMaterial());
    // the same entries, and not the same value: one of them will take the
    // next card and the other will not
    EXPECT_TRUE(following.getBase() == custom.getBase());
    EXPECT_FALSE(following.isSame(custom));

    std::unique_ptr<App::Property> copy(following.Copy());
    App::PropertyAppearanceList pasted;
    pasted.Paste(*copy);
    EXPECT_TRUE(pasted.isFollowingMaterial());
}

TEST_F(PropertyAppearanceListTest, theFollowFlagRidesBothForkEncodings)
{
    App::PropertyAppearanceList prop;
    prop.setSize(4);
    prop.followMaterial(redMaterial());
    prop.setDiffuseColor(1, packed(0x00ff00ff));
    ASSERT_TRUE(prop.isFollowingMaterial());

    for (bool asXML : {true, false}) {
        App::PropertyAppearanceList back;
        if (asXML) {
            const std::string xml = saveToXML(prop, 5);
            EXPECT_NE(xml.find("follow=\"1\""), std::string::npos) << xml;
            restoreFromXML(back, xml);
        }
        else {
            restoreDocFile(back, saveDocFile(prop, 5));
        }
        EXPECT_TRUE(back.isFollowingMaterial()) << asXML;
        EXPECT_TRUE(back.isSame(prop)) << asXML;
    }

    // A following list with nothing overriding it still states its base:
    // the field lines cannot say that a value is the card's
    App::PropertyAppearanceList uniform;
    uniform.setSize(4);
    uniform.followMaterial(redMaterial());
    App::PropertyAppearanceList back;
    restoreDocFile(back, saveDocFile(uniform, 5));
    EXPECT_TRUE(back.isFollowingMaterial());
    EXPECT_TRUE(back.isSame(uniform));

    // Schema 4 cannot state it, and restores not following -- which is what
    // the view provider derives again (docs/MaterialStorage.md 15.4)
    App::PropertyAppearanceList old;
    restoreFromXML(old, saveToXML(prop, 4));
    EXPECT_FALSE(old.isFollowingMaterial());
}
