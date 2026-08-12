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

#include <sstream>
#include <string>
#include <vector>

#include <App/Material.h>
#include <App/PropertyStandard.h>
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

App::Material redMaterial()
{
    App::Material mat;
    mat.diffuseColor = packed(0xff0000ff);
    return mat;
}

App::Material texturedMaterial()
{
    App::Material mat;
    mat.diffuseColor = packed(0x0000ffff);
    mat.image = std::string("PNG\x01\x02", 5);  // bytes, not text
    mat.imagePath = "/home/someone/textures/oak <old>.png";
    mat.uuid = "f0e1d2c3-b4a5-6978-8a9b-0c1d2e3f4050";
    return mat;
}

App::Material fullyPaintedMaterial()
{
    App::Material mat;
    mat.ambientColor = packed(0x11223344);
    mat.diffuseColor = packed(0x55667788);
    mat.specularColor = packed(0x99aabbcc);
    mat.emissiveColor = packed(0xddeeff00);
    mat.shininess = 0.75F;
    mat.transparency = 0.25F;
    return mat;
}

std::string saveToXML(const App::PropertyMaterialList& prop, int schema)
{
    Base::StringWriter writer;
    writer.setForceXML(1);
    writer.setSchemaVersion(schema);
    prop.Save(writer);
    return writer.getString();
}

void restoreFromXML(App::PropertyMaterialList& prop, const std::string& xml)
{
    std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?><document>)" + xml + "</document>";
    std::istringstream stream(doc);
    Base::XMLReader reader("material.xml", stream);
    prop.Restore(reader);
}

std::string saveDocFile(const App::PropertyMaterialList& prop, int schema)
{
    Base::StringWriter writer;
    writer.setPreferBinary(false);
    writer.setSchemaVersion(schema);
    prop.SaveDocFile(writer);
    return writer.getString();
}

void restoreDocFile(App::PropertyMaterialList& prop, const std::string& data)
{
    std::istringstream stream(data);
    Base::Reader reader(stream, "material.txt");
    prop.RestoreDocFile(reader);
}

/// Every entry read back whole, which is what a caller of the old API saw
void expectEntries(const App::PropertyMaterialList& prop,
                   const std::vector<App::Material>& expected)
{
    ASSERT_EQ(prop.getSize(), static_cast<int>(expected.size()));
    for (int i = 0; i < prop.getSize(); ++i) {
        EXPECT_TRUE(prop.getMaterial(i) == expected[i]) << "entry " << i;
    }
}

class PropertyMaterialListTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tests::initApplication();
    }
};

}  // namespace

TEST_F(PropertyMaterialListTest, defaultEntriesCostNothing)
{
    App::PropertyMaterialList prop;
    EXPECT_EQ(prop.getSize(), 0);
    EXPECT_EQ(prop.getMemSize(), 0U);

    // ten thousand default materials is still nothing stored
    prop.setSize(10000);
    EXPECT_EQ(prop.getSize(), 10000);
    EXPECT_EQ(prop.getMemSize(), 0U);
    EXPECT_TRUE(prop.getMaterial(5000) == App::Material());
    EXPECT_TRUE(prop.getAmbientColors().empty());
    EXPECT_TRUE(prop.getDiffuseColors().empty());
}

TEST_F(PropertyMaterialListTest, outOfRangeReadsAsDefault)
{
    App::PropertyMaterialList prop;
    prop.setValue(redMaterial());
    EXPECT_TRUE(prop.getMaterial(-1) == App::Material());
    EXPECT_TRUE(prop.getMaterial(1) == App::Material());
}

TEST_F(PropertyMaterialListTest, uniformListCollapsesToOneOfEachField)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(1000, redMaterial()));

    EXPECT_EQ(prop.getSize(), 1000);
    // only the field that differs from the default is stored, and only once
    EXPECT_EQ(prop.getDiffuseColors().size(), 1U);
    EXPECT_TRUE(prop.getAmbientColors().empty());
    EXPECT_TRUE(prop.getSpecularColors().empty());
    EXPECT_TRUE(prop.getEmissiveColors().empty());
    EXPECT_TRUE(prop.getShininessValues().empty());
    EXPECT_TRUE(prop.getTransparencies().empty());
    EXPECT_EQ(prop.getMemSize(), sizeof(App::Color));

    // and every entry still reads as that material
    for (int i : {0, 1, 500, 999}) {
        EXPECT_TRUE(prop.getMaterial(i) == redMaterial()) << "entry " << i;
    }
}

TEST_F(PropertyMaterialListTest, oneOddEntryExpandsOnlyItsOwnField)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(10, redMaterial()));

    App::Material odd = redMaterial();
    odd.diffuseColor = packed(0x00ff00ff);
    prop.set1Value(3, odd);

    EXPECT_EQ(prop.getDiffuseColors().size(), 10U);
    EXPECT_TRUE(prop.getAmbientColors().empty());
    EXPECT_TRUE(prop.getMaterial(3) == odd);
    EXPECT_TRUE(prop.getMaterial(4) == redMaterial());

    // put it back and the field collapses again
    prop.set1Value(3, redMaterial());
    EXPECT_EQ(prop.getMemSize(), sizeof(App::Color));
    EXPECT_EQ(prop.getDiffuseColors().size(), 1U);
    EXPECT_TRUE(prop.getMaterial(3) == redMaterial());
}

TEST_F(PropertyMaterialListTest, everyFieldCanVaryOnItsOwn)
{
    App::PropertyMaterialList prop;
    prop.setSize(4);
    prop.setAmbientColor(1, packed(0x11111111));
    prop.setSpecularColor(2, packed(0x22222222));
    prop.setEmissiveColor(3, packed(0x33333333));
    prop.setShininess(0, 0.5F);
    prop.setTransparency(2, 0.5F);

    EXPECT_EQ(prop.getSize(), 4);
    EXPECT_EQ(prop.getAmbientColors().size(), 4U);
    EXPECT_TRUE(prop.getDiffuseColors().empty());
    EXPECT_TRUE(prop.getAmbientColor(1) == packed(0x11111111));
    EXPECT_TRUE(prop.getAmbientColor(0) == App::Material().ambientColor);
    EXPECT_TRUE(prop.getSpecularColor(2) == packed(0x22222222));
    EXPECT_TRUE(prop.getEmissiveColor(3) == packed(0x33333333));
    EXPECT_FLOAT_EQ(prop.getShininess(0), 0.5F);
    EXPECT_FLOAT_EQ(prop.getShininess(1), App::Material().shininess);
    EXPECT_FLOAT_EQ(prop.getTransparency(2), 0.5F);
}

TEST_F(PropertyMaterialListTest, uniformSetterStoresOneValue)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(100, App::Material()));
    prop.setDiffuseColor(packed(0x0000ffff));

    EXPECT_EQ(prop.getSize(), 100);
    EXPECT_EQ(prop.getDiffuseColors().size(), 1U);
    EXPECT_TRUE(prop.getDiffuseColor(99) == packed(0x0000ffff));

    // setting it back to the default gives the storage up entirely
    prop.setDiffuseColor(App::Material().diffuseColor);
    EXPECT_TRUE(prop.getDiffuseColors().empty());
    EXPECT_EQ(prop.getMemSize(), 0U);
}

TEST_F(PropertyMaterialListTest, wholeFieldSetterSizesTheList)
{
    App::PropertyMaterialList prop;
    prop.setDiffuseColors({packed(0xff0000ff), packed(0x00ff00ff), packed(0x0000ffff)});

    EXPECT_EQ(prop.getSize(), 3);
    EXPECT_EQ(prop.getDiffuseColors().size(), 3U);
    EXPECT_TRUE(prop.getMaterial(1).diffuseColor == packed(0x00ff00ff));

    // a uniform vector arrives and normalises on the way in
    prop.setDiffuseColors(std::vector<App::Color>(3, packed(0xff0000ff)));
    EXPECT_EQ(prop.getSize(), 3);
    EXPECT_EQ(prop.getDiffuseColors().size(), 1U);
}

TEST_F(PropertyMaterialListTest, resizingKeepsWhatItCanAndDefaultsTheRest)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {redMaterial(), fullyPaintedMaterial()};
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

TEST_F(PropertyMaterialListTest, growingWithAFillStaysUniform)
{
    // the shape of an import that appends one identically coloured face at a
    // time: it must not materialise the field on every step
    App::PropertyMaterialList prop;
    for (int i = 0; i < 500; ++i) {
        prop.set1Value(-1, redMaterial());
    }
    EXPECT_EQ(prop.getSize(), 500);
    EXPECT_EQ(prop.getDiffuseColors().size(), 1U);
    EXPECT_TRUE(prop.getMaterial(499) == redMaterial());
}

TEST_F(PropertyMaterialListTest, legacyXMLRoundTrip)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {redMaterial(), fullyPaintedMaterial(), App::Material()};
    prop.setValues(values);

    const std::string xml = saveToXML(prop, 5);
    EXPECT_EQ(xml.find("fields="), std::string::npos) << xml;

    App::PropertyMaterialList restored;
    restoreFromXML(restored, xml);
    expectEntries(restored, values);
}

TEST_F(PropertyMaterialListTest, fieldXMLRoundTrip)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {redMaterial(), fullyPaintedMaterial(), App::Material()};
    prop.setValues(values);

    const std::string xml = saveToXML(prop, 6);
    EXPECT_NE(xml.find("fields=\"1\""), std::string::npos) << xml;

    App::PropertyMaterialList restored;
    restoreFromXML(restored, xml);
    expectEntries(restored, values);
    // and it came back in the compact form, not spelled out
    EXPECT_EQ(restored.getMemSize(), prop.getMemSize());
}

TEST_F(PropertyMaterialListTest, fieldXMLIsSmallForAUniformList)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(2000, fullyPaintedMaterial()));

    const std::string compact = saveToXML(prop, 6);
    const std::string legacy = saveToXML(prop, 5);
    EXPECT_LT(compact.size(), legacy.size() / 100);

    App::PropertyMaterialList restored;
    restoreFromXML(restored, compact);
    EXPECT_EQ(restored.getSize(), 2000);
    EXPECT_TRUE(restored.getMaterial(1999) == fullyPaintedMaterial());
}

TEST_F(PropertyMaterialListTest, legacyDocFileRoundTrip)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {redMaterial(), fullyPaintedMaterial()};
    prop.setValues(values);

    App::PropertyMaterialList restored;
    restoreDocFile(restored, saveDocFile(prop, 5));
    expectEntries(restored, values);
}

TEST_F(PropertyMaterialListTest, fieldDocFileRoundTrip)
{
    // the shape a STEP import produces: diffuse varies per face and nothing
    // else does, so five of the seven fields are not written at all
    App::PropertyMaterialList prop;
    std::vector<App::Material> values(300, redMaterial());
    values[7].diffuseColor = packed(0x00ff00ff);
    prop.setValues(values);

    // a third of the size, and that is the text encoding's ratio: in binary
    // it is the full six-to-one, one packed colour against a whole material
    const std::string compact = saveDocFile(prop, 6);
    EXPECT_LT(compact.size(), saveDocFile(prop, 5).size() / 3);

    App::PropertyMaterialList restored;
    restoreDocFile(restored, compact);
    expectEntries(restored, values);
}

TEST_F(PropertyMaterialListTest, fieldDocFileCostsLittleWhenNothingCollapses)
{
    // the other end of it, stated rather than hidden: when every field
    // genuinely varies there is nothing to save, and the per field encoding
    // must not cost meaningfully more than the one it replaces
    App::PropertyMaterialList prop;
    std::vector<App::Material> values(300, redMaterial());
    values[7] = fullyPaintedMaterial();
    prop.setValues(values);

    const std::string compact = saveDocFile(prop, 6);
    const std::string legacy = saveDocFile(prop, 5);
    EXPECT_LT(compact.size(), legacy.size() + legacy.size() / 100);

    App::PropertyMaterialList restored;
    restoreDocFile(restored, compact);
    expectEntries(restored, values);
}

TEST_F(PropertyMaterialListTest, aFileWrittenTheOldWayStillReadsBack)
{
    // what a document written by any other FreeCAD holds: one whole material
    // per entry, in the order the packed colours have always been written
    App::PropertyMaterialList prop;
    restoreFromXML(prop,
                   "<MaterialList count=\"2\" >\n"
                   "11223344 55667788 99aabbcc ddeeff00 0.75 0.25\n"
                   "0 ff0000ff 0 0 0.2 0\n"
                   "</MaterialList>\n");

    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_TRUE(prop.getMaterial(0) == fullyPaintedMaterial());
    EXPECT_TRUE(prop.getDiffuseColor(1) == packed(0xff0000ff));
    EXPECT_TRUE(prop.getAmbientColor(1) == packed(0));
}

TEST_F(PropertyMaterialListTest, equalListsSerialiseIdentically)
{
    // the requirement the shared-default scheme rests on: two lists that
    // mean the same thing must produce the same bytes, however they got
    // there, or elision silently stops happening
    App::PropertyMaterialList byWholeValues;
    byWholeValues.setValues(std::vector<App::Material>(50, redMaterial()));

    App::PropertyMaterialList byField;
    byField.setSize(50);
    byField.setDiffuseColor(packed(0xff0000ff));

    App::PropertyMaterialList byEntry;
    byEntry.setSize(50);
    for (int i = 0; i < 50; ++i) {
        byEntry.setDiffuseColor(i, packed(0xff0000ff));
    }

    EXPECT_TRUE(byWholeValues.isSame(byField));
    EXPECT_TRUE(byWholeValues.isSame(byEntry));
    EXPECT_EQ(saveToXML(byWholeValues, 6), saveToXML(byField, 6));
    EXPECT_EQ(saveToXML(byWholeValues, 6), saveToXML(byEntry, 6));
    EXPECT_EQ(saveDocFile(byWholeValues, 6), saveDocFile(byEntry, 6));
}

TEST_F(PropertyMaterialListTest, isSameSeesThroughCardinality)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(4, redMaterial()));

    App::PropertyMaterialList other;
    other.setValues(std::vector<App::Material>(5, redMaterial()));
    EXPECT_FALSE(prop.isSame(other));

    other.setSize(4);
    EXPECT_TRUE(prop.isSame(other));

    other.setShininess(2, 0.9F);
    EXPECT_FALSE(prop.isSame(other));
}

TEST_F(PropertyMaterialListTest, copyAndPasteCarryEveryField)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {fullyPaintedMaterial(), redMaterial()};
    prop.setValues(values);

    std::unique_ptr<App::Property> copy(prop.Copy());
    App::PropertyMaterialList pasted;
    pasted.Paste(*copy);

    expectEntries(pasted, values);
    EXPECT_TRUE(pasted.isSame(prop));
}

TEST_F(PropertyMaterialListTest, saveSizeAnswersForTheEncodingBeingWritten)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(1000, redMaterial()));

    Base::StringWriter writer;
    writer.setSchemaVersion(6);
    EXPECT_EQ(prop.getSaveSize(writer), prop.getMemSize());

    // the compatible encoding spells out every entry, and the rule that
    // decides between an inline list and an archive entry has to hear that
    writer.setSchemaVersion(5);
    EXPECT_EQ(prop.getSaveSize(writer), 1000U * 24U);
}

TEST_F(PropertyMaterialListTest, texturesAndCardsCollapseLikeEveryOtherField)
{
    App::PropertyMaterialList prop;
    prop.setValues(std::vector<App::Material>(200, texturedMaterial()));

    EXPECT_EQ(prop.getSize(), 200);
    EXPECT_EQ(prop.getImages().size(), 1U);
    EXPECT_EQ(prop.getImagePaths().size(), 1U);
    EXPECT_EQ(prop.getUuids().size(), 1U);
    EXPECT_EQ(prop.getUuid(199), texturedMaterial().uuid);

    prop.setUuid(7, "another-card");
    EXPECT_EQ(prop.getUuids().size(), 200U);
    EXPECT_EQ(prop.getUuid(7), "another-card");
    EXPECT_EQ(prop.getUuid(8), texturedMaterial().uuid);
    // the other two did not have to grow
    EXPECT_EQ(prop.getImages().size(), 1U);
}

TEST_F(PropertyMaterialListTest, aTexturePathSurvivesTheXMLForm)
{
    // spaces and angle brackets in a path, and bytes that are not text in an
    // embedded image: the inline form has to carry them intact
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {texturedMaterial(), App::Material()};
    prop.setValues(values);

    const std::string xml = saveToXML(prop, 5);
    EXPECT_NE(xml.find("fields=\"1\""), std::string::npos) << xml;

    App::PropertyMaterialList restored;
    restoreFromXML(restored, xml);
    expectEntries(restored, values);
    EXPECT_EQ(restored.getImagePath(0), texturedMaterial().imagePath);
}

TEST_F(PropertyMaterialListTest, stringsRideTheCompactDocFile)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values(50, texturedMaterial());
    values[3].uuid = "odd-one-out";
    prop.setValues(values);

    App::PropertyMaterialList restored;
    restoreDocFile(restored, saveDocFile(prop, 6));
    expectEntries(restored, values);
}

TEST_F(PropertyMaterialListTest, aFileWithStringsIsWrittenAsUpstreamsVersionThree)
{
    App::PropertyMaterialList prop;
    std::vector<App::Material> values {texturedMaterial(), redMaterial()};
    prop.setValues(values);

    // the element has to say version="3", because that is the only way
    // upstream's reader knows a second pass follows
    Base::StringWriter element;
    element.setSchemaVersion(5);
    element.setPreferBinary(true);
    element.setForceXML(0);  // a StringWriter forces XML unless told otherwise
    prop.Save(element);
    EXPECT_NE(element.getString().find("version=\"3\""), std::string::npos)
        << element.getString();
    EXPECT_NE(element.getString().find("file="), std::string::npos);

    // and round trips through it
    Base::StringWriter file;
    file.setSchemaVersion(5);
    file.setPreferBinary(true);
    file.setForceXML(0);
    prop.SaveDocFile(file);

    App::PropertyMaterialList restored;
    restoreFromXML(restored, element.getString());
    std::istringstream stream(file.getString());
    Base::Reader reader(stream, "material.bin");
    restored.RestoreDocFile(reader);
    expectEntries(restored, values);
}

TEST_F(PropertyMaterialListTest, readsAFileLaidOutTheWayUpstreamWritesIt)
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

    App::PropertyMaterialList prop;
    restoreFromXML(prop, "<MaterialList file=\"m.bin\" version=\"3\"/>\n");
    std::istringstream stream(bytes);
    Base::Reader reader(stream, "m.bin");
    prop.RestoreDocFile(reader);

    ASSERT_EQ(prop.getSize(), 2);
    EXPECT_TRUE(prop.getDiffuseColor(0) == packed(0xff0000ff));
    EXPECT_TRUE(prop.getDiffuseColor(1) == packed(0x00ff00ff));
    EXPECT_FLOAT_EQ(prop.getShininess(1), 0.5F);
    EXPECT_EQ(prop.getImagePath(0), "/tex/one.png");
    EXPECT_EQ(prop.getUuid(0), "card-1");
    EXPECT_TRUE(prop.getImagePath(1).empty());
    // the ambient colour was uniform in the file and is stored once
    EXPECT_EQ(prop.getAmbientColors().size(), 1U);
}
