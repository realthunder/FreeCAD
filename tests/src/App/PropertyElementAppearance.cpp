// SPDX-License-Identifier: LGPL-2.1-or-later

/** What the elements of a shape look like, held by the object
 * (docs/ShapeAppearanceDesign.md sec 14).
 *
 * The object these run on has no shape, so nothing here has a mapped name
 * and a look given to "Face3" is held by the number: that is the half of
 * the smart indexing this suite can ask, and the names are given outright
 * (setNamed) where a test is about them. What a shape with names makes of
 * the same writes is Part's to test.
 */

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <App/Application.h>
#include <App/Document.h>
#include <App/DocumentObject.h>
#include <App/PropertyElementAppearance.h>
#include <Base/Exception.h>
#include <Base/FileInfo.h>
#include <Base/Reader.h>
#include <Base/Writer.h>

#include <src/App/InitApplication.h>

namespace
{

using Prop = App::PropertyElementAppearance;

App::Color packed(uint32_t rgba)
{
    App::Color color;
    color.setPackedValue(rgba);
    return color;
}

const App::Color Red = packed(0xff0000ff);
const App::Color Green = packed(0x00ff00ff);
const App::Color Blue = packed(0x0000ffff);

App::MaterialAppearance coloured(const App::Color& color, float shininess = 0.25F)
{
    App::MaterialAppearance mat;
    mat.diffuseColor = color;
    mat.transparency = color.transparency();
    mat.shininess = shininess;
    return mat;
}

std::string saveToXML(const Prop& prop, int schema)
{
    Base::StringWriter writer;
    writer.setForceXML(1);
    writer.setSchemaVersion(schema);
    prop.Save(writer);
    return writer.getString();
}

void restoreFromXML(Prop& prop, const std::string& xml)
{
    std::string doc = R"(<?xml version="1.0" encoding="UTF-8"?><document><Property>)" + xml
        + "</Property></document>";
    std::istringstream stream(doc);
    Base::XMLReader reader("appearance.xml", stream);
    reader.readElement("Property");
    prop.Restore(reader);
    reader.readEndElement("Property");
}

}  // namespace

class PropertyElementAppearanceTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("appearance");
        _doc = App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _obj = _doc->addObject("App::FeatureTest", "obj");
        _prop = property(_obj);
    }

    void TearDown() override
    {
        if (_doc) {
            App::GetApplication().closeDocument(_doc->getName());
            _doc = nullptr;
        }
        if (!_file.empty()) {
            Base::FileInfo fi(_file);
            if (fi.exists()) {
                fi.deleteFile();
            }
        }
    }

    static Prop* property(App::DocumentObject* obj)
    {
        auto prop = obj->getPropertyByName("ElementAppearance");
        if (!prop) {
            prop = obj->addDynamicProperty("App::PropertyElementAppearance", "ElementAppearance");
        }
        return dynamic_cast<Prop*>(prop);
    }

    /// Save this document, close it, and open it again
    void roundTrip()
    {
        if (_file.empty()) {
            _file = Base::FileInfo::getTempFileName();
            _file += ".FCStd";
        }
        ASSERT_TRUE(_doc->saveAs(_file.c_str()));
        App::GetApplication().closeDocument(_doc->getName());
        _doc = App::GetApplication().openDocument(_file.c_str(), false);
        ASSERT_NE(_doc, nullptr);
        _obj = _doc->getObject("obj");
        ASSERT_NE(_obj, nullptr);
        _prop = dynamic_cast<Prop*>(_obj->getPropertyByName("ElementAppearance"));
        ASSERT_NE(_prop, nullptr);
    }

    /// Two names with a look each: the first painted, the second given a
    /// material
    void nameTwo()
    {
        App::AppearanceList looks;
        looks.setSize(2, _prop->getBase(Prop::Face));
        looks.set1Value(0, coloured(Red));
        looks.set1Value(1, coloured(Green, 0.75F));
        _prop->setNamed({"Face1", "Face2"}, looks, {Prop::OwnDiffuse, Prop::OwnAll});
    }

    std::string _docName;
    std::string _file;
    App::Document* _doc {nullptr};
    App::DocumentObject* _obj {nullptr};
    Prop* _prop {nullptr};
};

TEST_F(PropertyElementAppearanceTest, anElementIsNamedAsTheShapeCountsIt)
{
    Prop::Kind kind = Prop::KindCount;
    int index = 99;
    ASSERT_TRUE(Prop::parseElement("Face3", kind, index));
    EXPECT_EQ(kind, Prop::Face);
    EXPECT_EQ(index, 2);
    ASSERT_TRUE(Prop::parseElement("Edge", kind, index));
    EXPECT_EQ(kind, Prop::Edge);
    EXPECT_EQ(index, -1);
    ASSERT_TRUE(Prop::parseElement("Vertex12", kind, index));
    EXPECT_EQ(kind, Prop::Vertex);
    EXPECT_EQ(index, 11);
    // A missing element is still that element, and a path is stepped over
    ASSERT_TRUE(Prop::parseElement("?Face7", kind, index));
    EXPECT_EQ(index, 6);
    ASSERT_TRUE(Prop::parseElement("Body.Pad.Face2", kind, index));
    EXPECT_EQ(index, 1);
    EXPECT_FALSE(Prop::parseElement("Face0", kind, index));
    EXPECT_FALSE(Prop::parseElement("Face-1", kind, index));
    EXPECT_FALSE(Prop::parseElement("Face3x", kind, index));
    EXPECT_FALSE(Prop::parseElement("Wire1", kind, index));
    EXPECT_FALSE(Prop::parseElement("", kind, index));
    EXPECT_EQ(Prop::elementName(Prop::Face, 2), "Face3");
    EXPECT_EQ(Prop::elementName(Prop::Vertex, -1), "Vertex");
}

TEST_F(PropertyElementAppearanceTest, anObjectNobodyColouredHoldsNothing)
{
    ASSERT_NE(_prop, nullptr);
    EXPECT_TRUE(_prop->isEmpty());
    EXPECT_FALSE(_prop->hasBase(Prop::Face));
    EXPECT_EQ(_prop->getDrawn(Prop::Face).getSize(), 0);
    EXPECT_TRUE(_prop->getStatedLooks().empty());
    // Its look is the default one, asked any way
    EXPECT_EQ(_prop->getLook(Prop::Face, 4).diffuseColor,
              App::AppearanceList::defaultMaterial().diffuseColor);
    EXPECT_NE(saveToXML(*_prop, 5).find("<ElementAppearance lists=\"0\"/>"), std::string::npos);
}

TEST_F(PropertyElementAppearanceTest, aFaceTheShapeGivesNoNameIsHeldByItsNumber)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    _prop->setLook("Face3", coloured(Red, 0.75F));

    EXPECT_EQ(_prop->getNamedCount(), 0);
    EXPECT_GE(_prop->getNumbered(Prop::Face).getSize(), 3);
    EXPECT_TRUE(_prop->isStated(Prop::Face, 2));
    EXPECT_FALSE(_prop->isStated(Prop::Face, 1));
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook(Prop::Face, 2).shininess, 0.75F);
    EXPECT_EQ(_prop->getLook("Face2").diffuseColor, Blue);
    // The object's own look is what it was
    EXPECT_EQ(_prop->getBase(Prop::Face).diffuseColor, Blue);

    // What is drawn is the numbered list itself, not a copy of it
    EXPECT_TRUE(_prop->getDrawn(Prop::Face).isSameData(_prop->getNumbered(Prop::Face)));

    const auto stated = _prop->getStatedLooks();
    ASSERT_EQ(stated.size(), 1U);
    EXPECT_EQ(stated[0].first, "Face3");
    EXPECT_EQ(stated[0].second.diffuseColor, Red);
}

TEST_F(PropertyElementAppearanceTest, aFaceGivenAColourFollowsTheObjectInTheRest)
{
    _prop->setBase(Prop::Face, coloured(Blue, 0.25F));
    _prop->setColor("Face3", Red);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 2), Prop::OwnDiffuse);
    EXPECT_EQ(_prop->getLook("Face3").shininess, 0.25F);

    // The object given another gloss: the painted face takes it, and keeps
    // its colour
    _prop->setBase(Prop::Face, coloured(Blue, 0.5F));
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Face3").shininess, 0.5F);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 2), Prop::OwnDiffuse);

    // And another colour: the face that was painted stays painted
    _prop->setBase(Prop::Face, coloured(Green, 0.5F));
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Green);
}

TEST_F(PropertyElementAppearanceTest, aFaceGivenAMaterialKeepsItsOwnBesideOneThatWasPainted)
{
    _prop->setBase(Prop::Face, coloured(Blue, 0.25F));
    _prop->setColor("Face1", Red);
    _prop->setLook("Face2", coloured(Green, 0.75F));

    // One element with a gloss of its own makes the list hold a gloss for
    // each: which of them is whose is read off the object's look
    _prop->setBase(Prop::Face, coloured(Blue, 0.5F));
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Face1").shininess, 0.5F);
    EXPECT_EQ(_prop->getLook("Face2").diffuseColor, Green);
    EXPECT_EQ(_prop->getLook("Face2").shininess, 0.75F);
    EXPECT_EQ(_prop->getLook("Face3").shininess, 0.5F);
}

TEST_F(PropertyElementAppearanceTest, everyFacePaintedAlikeLeavesTheObjectItsLook)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    {
        Prop::Edit edit(*_prop);
        for (int i = 0; i < 6; ++i) {
            _prop->setColor(Prop::Face, i, Red);
        }
    }
    // The list has six entries that agree, and keeps the red as its base.
    // The object is still blue.
    ASSERT_EQ(_prop->getNumbered(Prop::Face).getSize(), 6);
    EXPECT_EQ(_prop->getBase(Prop::Face).diffuseColor, Blue);
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(_prop->getLook(Prop::Face, i).diffuseColor, Red) << i;
        EXPECT_TRUE(_prop->isStated(Prop::Face, i)) << i;
    }
    EXPECT_EQ(_prop->getStatedLooks().size(), 6U);

    // One taken away goes back to the object's, which is there to go to
    EXPECT_TRUE(_prop->removeLook("Face4"));
    EXPECT_EQ(_prop->getLook("Face4").diffuseColor, Blue);
    EXPECT_FALSE(_prop->isStated(Prop::Face, 3));
    EXPECT_EQ(_prop->getLook("Face5").diffuseColor, Red);
    EXPECT_EQ(_prop->getStatedLooks().size(), 5U);

    // And the object given another colour while five are red
    _prop->setBase(Prop::Face, coloured(Green));
    EXPECT_EQ(_prop->getLook("Face4").diffuseColor, Green);
    EXPECT_EQ(_prop->getLook("Face5").diffuseColor, Red);
}

TEST_F(PropertyElementAppearanceTest, theLastLookTakenAwayLeavesNoList)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    _prop->setColor("Face2", Red);
    ASSERT_GT(_prop->getNumbered(Prop::Face).getSize(), 0);
    EXPECT_TRUE(_prop->removeLook("Face2"));
    EXPECT_EQ(_prop->getNumbered(Prop::Face).getSize(), 0);
    EXPECT_FALSE(_prop->removeLook("Face2"));
    // What is drawn is the object's look and no more
    EXPECT_EQ(_prop->getDrawn(Prop::Face).getSize(), 1);
    EXPECT_EQ(_prop->getDrawn(Prop::Face).getBase().diffuseColor, Blue);
}

TEST_F(PropertyElementAppearanceTest, aNameStatesWhatItWasGivenAndFollowsInTheRest)
{
    _prop->setBase(Prop::Face, coloured(Blue, 0.25F));
    nameTwo();
    ASSERT_EQ(_prop->getNamedCount(), 2);
    EXPECT_EQ(_prop->getValue(), _obj);
    EXPECT_EQ(_prop->findNamed("Face2"), 1);
    EXPECT_EQ(_prop->getNamedKind(0), Prop::Face);

    // The painted one is the object in its colour; the other is its own
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Face1").shininess, 0.25F);
    EXPECT_EQ(_prop->getLook("Face2").shininess, 0.75F);

    _prop->setBase(Prop::Face, coloured(Blue, 0.5F));
    EXPECT_EQ(_prop->getLook("Face1").shininess, 0.5F);
    EXPECT_EQ(_prop->getLook("Face2").shininess, 0.75F);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 0), Prop::OwnDiffuse);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 1), Prop::OwnAll);

    // A colour given to a name that has a material changes its colour and
    // leaves the rest its own
    _prop->setColor("Face2", Red);
    EXPECT_EQ(_prop->getLook("Face2").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Face2").shininess, 0.75F);
    // A gloss given to the painted one is its own from then on
    App::MaterialAppearance glossy = coloured(Red, 1.0F);
    _prop->setLook("Face1", glossy, Prop::OwnShininess);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 0), Prop::OwnDiffuse | Prop::OwnShininess);
    _prop->setBase(Prop::Face, coloured(Blue, 0.125F));
    EXPECT_EQ(_prop->getLook("Face1").shininess, 1.0F);
}

TEST_F(PropertyElementAppearanceTest, aNameTakenAwayTakesItsLook)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    nameTwo();
    EXPECT_TRUE(_prop->removeLook("Face1"));
    ASSERT_EQ(_prop->getNamedCount(), 1);
    EXPECT_EQ(_prop->getSubValues().front(), "Face2");
    EXPECT_EQ(_prop->getLook("Face2").diffuseColor, Green);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 1), Prop::OwnAll);
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Blue);

    EXPECT_TRUE(_prop->removeLook("Face2"));
    EXPECT_EQ(_prop->getNamedCount(), 0);
    EXPECT_EQ(_prop->getValue(), nullptr);
    EXPECT_EQ(_prop->getNamedLooks().getSize(), 0);
}

TEST_F(PropertyElementAppearanceTest, whatIsDrawnLaysTheNamesOverTheNumbers)
{
    _prop->setBase(Prop::Face, coloured(Blue, 0.25F));
    // Six faces, as far as the numbers say: the object here has no shape to
    // count them by
    {
        Prop::Edit edit(*_prop);
        _prop->setColor("Face4", Green);
        _prop->setColor("Face6", Green);
    }
    nameTwo();

    // The first name is face 3 now, the second face 1; face 5 has the look
    // of the face it was made from
    std::map<int, App::MaterialAppearance> handedOn;
    handedOn[4] = coloured(Green, 1.0F);
    const App::AppearanceList drawn = _prop->compose(Prop::Face, 6, {{2, 0}, {0, 1}}, &handedOn);
    ASSERT_EQ(drawn.getSize(), 6);
    EXPECT_EQ(drawn.getMaterial(0).diffuseColor, Green);
    EXPECT_EQ(drawn.getMaterial(0).shininess, 0.75F);
    EXPECT_EQ(drawn.getMaterial(1).diffuseColor, Blue);
    EXPECT_EQ(drawn.getMaterial(2).diffuseColor, Red);
    EXPECT_EQ(drawn.getMaterial(2).shininess, 0.25F);
    EXPECT_EQ(drawn.getMaterial(3).diffuseColor, Green);
    EXPECT_EQ(drawn.getMaterial(4).shininess, 1.0F);
    EXPECT_EQ(drawn.getMaterial(5).diffuseColor, Green);
    EXPECT_EQ(drawn.getMaterial(5).shininess, 0.25F);
    // The object's look is its base, whatever the faces agree on
    EXPECT_EQ(drawn.getBase().diffuseColor, Blue);

    _prop->setDrawn(Prop::Face, drawn);
    EXPECT_TRUE(_prop->getDrawn(Prop::Face).isSameData(drawn));
    // Kept, and no more than the object's when it says no more
    _prop->setDrawn(Prop::Face, _prop->getNumbered(Prop::Face));
    EXPECT_TRUE(_prop->getDrawn(Prop::Face).isSameData(_prop->getNumbered(Prop::Face)));

    // Numbers counted for another shape say nothing of this one's
    const App::AppearanceList other = _prop->compose(Prop::Face, 2, {});
    ASSERT_EQ(other.getSize(), 2);
    EXPECT_EQ(other.getMaterial(1).diffuseColor, Blue);
}

TEST_F(PropertyElementAppearanceTest, aCopyPutBackIsTheValueItWasTakenOf)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    _prop->setColor("Face4", Green);
    nameTwo();
    std::unique_ptr<App::Property> copy(_prop->Copy());
    EXPECT_TRUE(_prop->isSame(*copy));

    _prop->setBase(Prop::Face, coloured(Red));
    _prop->removeLook("Face1");
    _prop->setColor("Face5", Red);
    EXPECT_FALSE(_prop->isSame(*copy));

    _prop->Paste(*copy);
    EXPECT_TRUE(_prop->isSame(*copy));
    EXPECT_EQ(_prop->getBase(Prop::Face).diffuseColor, Blue);
    ASSERT_EQ(_prop->getNamedCount(), 2);
    EXPECT_EQ(_prop->getValue(), _obj);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 0), Prop::OwnDiffuse);
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Red);
    EXPECT_FALSE(_prop->isStated(Prop::Face, 4));
}

TEST_F(PropertyElementAppearanceTest, namesGivenAsALinkTakesThemStateNothing)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    nameTwo();
    // By something that knows a link and no more: a third name, no look
    static_cast<App::PropertyLinkSub*>(_prop)->setValue(
        _obj, std::vector<std::string> {"Face1", "Face2", "Face3"});
    ASSERT_EQ(_prop->getNamedCount(), 3);
    EXPECT_EQ(_prop->getNamedLooks().getSize(), 3);
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Red);
    EXPECT_EQ(_prop->getNamedOwn(2), Prop::OwnNone);
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Blue);

    static_cast<App::PropertyLinkSub*>(_prop)->setValue(nullptr);
    EXPECT_EQ(_prop->getNamedCount(), 0);
    EXPECT_EQ(_prop->getNamedLooks().getSize(), 0);
}

TEST_F(PropertyElementAppearanceTest, theFileFormIsReadBackAtEitherSchema)
{
    _prop->setBase(Prop::Face, coloured(Blue, 0.25F));
    _prop->setBase(Prop::Edge, coloured(Green));
    {
        Prop::Edit edit(*_prop);
        for (int i = 0; i < 6; ++i) {
            _prop->setColor(Prop::Face, i, Red);
        }
    }
    _prop->setColor("Edge3", Red);
    nameTwo();
    _prop->setDrawn(Prop::Face, _prop->compose(Prop::Face, 6, {{2, 0}, {0, 1}}));

    for (int schema : {4, 5}) {
        const std::string xml = saveToXML(*_prop, schema);
        // The names first, as a link writes them, for a reader that knows
        // no more
        EXPECT_LT(xml.find("<LinkSub"), xml.find("<ElementAppearance")) << schema;

        auto other = _doc->addObject("App::FeatureTest", schema == 4 ? "four" : "five");
        Prop* read = property(other);
        ASSERT_NE(read, nullptr);
        restoreFromXML(*read, xml);
        EXPECT_EQ(read->getSubValues(), _prop->getSubValues()) << schema;
        // The object's own look, which no entry of a list states below 5
        EXPECT_EQ(read->getBase(Prop::Face).diffuseColor, Blue) << schema;
        EXPECT_EQ(read->getBase(Prop::Edge).diffuseColor, Green) << schema;
        for (int i = 0; i < 6; ++i) {
            EXPECT_EQ(read->getNumbered(Prop::Face).getMaterial(i).diffuseColor, Red)
                << schema << " " << i;
        }
        EXPECT_EQ(read->getNumberedOwn(Prop::Edge, 2), Prop::OwnDiffuse) << schema;
        EXPECT_EQ(read->getNamedOwn(0), Prop::OwnDiffuse) << schema;
        EXPECT_EQ(read->getNamedOwn(1), Prop::OwnAll) << schema;
        EXPECT_EQ(read->getNamedLooks().getMaterial(1).shininess, 0.75F) << schema;
        ASSERT_EQ(read->getDrawn(Prop::Face).getSize(), 6) << schema;
        EXPECT_EQ(read->getDrawn(Prop::Face).getMaterial(0).diffuseColor, Green) << schema;
        EXPECT_EQ(read->getDrawn(Prop::Face).getMaterial(1).diffuseColor, Red) << schema;
    }
}

TEST_F(PropertyElementAppearanceTest, aLinkAsItWasWrittenBeforeIsNamesWithoutLooks)
{
    App::PropertyLinkSubHidden plain;
    plain.setContainer(_obj);
    plain.setValue(_obj, std::vector<std::string> {"Face1", "Face2"});
    Base::StringWriter writer;
    writer.setForceXML(1);
    plain.Save(writer);

    _prop->setBase(Prop::Face, coloured(Blue));
    restoreFromXML(*_prop, writer.getString());
    ASSERT_EQ(_prop->getNamedCount(), 2);
    EXPECT_FALSE(_prop->hasBase(Prop::Face));
    EXPECT_EQ(_prop->getNamedOwn(0), Prop::OwnNone);
    plain.setValue(nullptr);
}

TEST_F(PropertyElementAppearanceTest, aDocumentSavedAndOpenedHasIt)
{
    _prop->setBase(Prop::Face, coloured(Blue, 0.25F));
    // Enough faces for the list to go to a file of its own
    {
        Prop::Edit edit(*_prop);
        for (int i = 0; i < 3000; ++i) {
            _prop->setColor(Prop::Face, i, i % 2 ? Red : Green);
        }
    }
    nameTwo();
    roundTrip();

    EXPECT_EQ(_prop->getBase(Prop::Face).diffuseColor, Blue);
    ASSERT_EQ(_prop->getNumbered(Prop::Face).getSize(), 3000);
    EXPECT_EQ(_prop->getLook(Prop::Face, 2998).diffuseColor, Green);
    EXPECT_EQ(_prop->getLook(Prop::Face, 2999).diffuseColor, Red);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 2999), Prop::OwnDiffuse);
    ASSERT_EQ(_prop->getNamedCount(), 2);
    EXPECT_EQ(_prop->getValue(), _obj);
    EXPECT_EQ(_prop->getNamedOwn(0), Prop::OwnDiffuse);
    EXPECT_EQ(_prop->getLook("Face2").shininess, 0.75F);
    // The names are laid over the numbers
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Red);
}

TEST_F(PropertyElementAppearanceTest, aWriteIsOneStepToUndo)
{
    _doc->setUndoMode(1);
    _doc->openTransaction("base");
    _prop->setBase(Prop::Face, coloured(Blue));
    _doc->commitTransaction();

    _doc->openTransaction("paint");
    {
        Prop::Edit edit(*_prop);
        _prop->setColor("Face2", Red);
        _prop->setColor("Face3", Green);
    }
    nameTwo();
    _doc->commitTransaction();
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Green);
    EXPECT_EQ(_prop->getNamedCount(), 2);

    _doc->undo();
    EXPECT_EQ(_prop->getBase(Prop::Face).diffuseColor, Blue);
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Blue);
    EXPECT_EQ(_prop->getNamedCount(), 0);
    EXPECT_EQ(_prop->getNumbered(Prop::Face).getSize(), 0);

    _doc->redo();
    EXPECT_EQ(_prop->getLook("Face3").diffuseColor, Green);
    ASSERT_EQ(_prop->getNamedCount(), 2);
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Red);
}

namespace
{

/// What setStatedColors() does, a name at a time: what it has to come to
void oneAtATime(Prop& prop, const std::map<std::string, App::Color>& colors)
{
    Prop::Edit edit(prop);
    for (const auto& v : prop.getStatedLooks()) {
        if (!colors.count(v.first)) {
            prop.removeLook(v.first.c_str());
        }
    }
    for (const auto& v : colors) {
        if (v.first == "Face" || v.first == "Edge" || v.first == "Vertex") {
            continue;
        }
        try {
            prop.setColor(v.first.c_str(), v.second);
        }
        catch (Base::Exception&) {
        }
    }
}

/// Whether two state the same of the same elements, however each holds it
void expectSameStated(const Prop& a, const Prop& b)
{
    const auto left = a.getStatedLooks();
    const auto right = b.getStatedLooks();
    ASSERT_EQ(left.size(), right.size());
    EXPECT_EQ(a.getNamedCount(), b.getNamedCount());
    for (std::size_t i = 0; i < left.size(); ++i) {
        EXPECT_EQ(left[i].first, right[i].first) << i;
        EXPECT_EQ(Prop::differingFields(left[i].second, right[i].second), Prop::OwnNone)
            << left[i].first;
        Prop::Kind kind = Prop::KindCount;
        int index = -1;
        ASSERT_TRUE(Prop::parseElement(left[i].first.c_str(), kind, index));
        EXPECT_EQ(a.getOwn(kind, index), b.getOwn(kind, index)) << left[i].first;
    }
    for (int k = 0; k < Prop::KindCount; ++k) {
        const auto kind = static_cast<Prop::Kind>(k);
        EXPECT_EQ(Prop::differingFields(a.getBase(kind), b.getBase(kind)), Prop::OwnNone);
    }
}

std::map<std::string, App::Color> palette(int from, int to, uint32_t seed)
{
    std::map<std::string, App::Color> colors;
    for (int i = from; i < to; ++i) {
        colors["Face" + std::to_string(i + 1)] =
            packed(((seed + static_cast<uint32_t>(i) * 2654435761U) & 0xffffff00U) | 0xffU);
    }
    return colors;
}

}  // namespace

TEST_F(PropertyElementAppearanceTest, manyColoursAtOnceAreWhatANameAtATimeMakes)
{
    Prop* other = property(_doc->addObject("App::FeatureTest", "other"));
    ASSERT_NE(other, nullptr);
    for (Prop* prop : {_prop, other}) {
        prop->setBase(Prop::Face, coloured(Blue, 0.25F));
        // One face holds a material of its own before any is painted
        prop->setLook("Face7", coloured(Green, 0.75F));
    }
    // More than are written where they are, their names not in the order of
    // their numbers ("Face10" is before "Face2"); then other colours for a
    // part of them, which takes the rest away; then a few; then none
    const std::vector<std::map<std::string, App::Color>> calls = {
        palette(0, 60, 1U),
        palette(20, 50, 7U),
        palette(45, 90, 7U),
        palette(3, 9, 11U),
        {},
    };
    for (const auto& colors : calls) {
        _prop->setStatedColors(colors);
        oneAtATime(*other, colors);
        expectSameStated(*_prop, *other);
        for (const auto& v : colors) {
            EXPECT_EQ(_prop->getLook(v.first.c_str()).diffuseColor, v.second) << v.first;
        }
    }
    EXPECT_EQ(_prop->getNumbered(Prop::Face).getSize(), 0);

    // The face that had a material keeps it under its new colour, and is
    // back to the object's when it is left out
    _prop->setLook("Face7", coloured(Green, 0.75F));
    _prop->setStatedColors({{"Face7", Red}, {"Face8", Red}});
    EXPECT_EQ(_prop->getLook("Face7").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Face7").shininess, 0.75F);
    EXPECT_EQ(_prop->getLook("Face8").shininess, 0.25F);
    _prop->setStatedColors({{"Face8", Red}});
    EXPECT_FALSE(_prop->isStated(Prop::Face, 6));
    EXPECT_EQ(_prop->getLook("Face7").shininess, 0.25F);
}

TEST_F(PropertyElementAppearanceTest, manyAtOnceTakeAwayTheNamesLeftOut)
{
    Prop* other = property(_doc->addObject("App::FeatureTest", "other"));
    ASSERT_NE(other, nullptr);
    for (Prop* prop : {_prop, other}) {
        prop->setBase(Prop::Face, coloured(Blue, 0.25F));
        App::AppearanceList looks;
        looks.setSize(3, prop->getBase(Prop::Face));
        looks.set1Value(0, coloured(Red));
        looks.set1Value(1, coloured(Green, 0.75F));
        looks.set1Value(2, coloured(Green, 0.5F));
        prop->setNamed({"Face1", "Face2", "Edge4"}, looks,
                       {Prop::OwnDiffuse, Prop::OwnAll, Prop::OwnAll});
        prop->setColor("Face9", Red);
    }
    // The second name is left out and goes; the first changes colour and
    // states no more than it did; a face with no name is held by its number
    const std::map<std::string, App::Color> colors = {
        {"Face1", Green},
        {"Edge4", Red},
        {"Face5", Red},
    };
    _prop->setStatedColors(colors);
    oneAtATime(*other, colors);
    expectSameStated(*_prop, *other);
    ASSERT_EQ(_prop->getNamedCount(), 2);
    EXPECT_EQ(_prop->findNamed("Face2"), -1);
    EXPECT_EQ(_prop->getLook("Face1").diffuseColor, Green);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 0), Prop::OwnDiffuse);
    EXPECT_EQ(_prop->getLook("Edge4").diffuseColor, Red);
    EXPECT_EQ(_prop->getLook("Edge4").shininess, 0.5F);
    EXPECT_FALSE(_prop->isStated(Prop::Face, 8));
    EXPECT_TRUE(_prop->isStated(Prop::Face, 4));

    // Whole looks, the same way: each is all the element's own
    _prop->setStatedLooks({{"Face1", coloured(Blue, 0.75F)}, {"Face6", coloured(Red, 0.5F)}});
    ASSERT_EQ(_prop->getNamedCount(), 1);
    EXPECT_EQ(_prop->getOwn(Prop::Face, 0), Prop::OwnAll);
    EXPECT_EQ(_prop->getLook("Face1").shininess, 0.75F);
    EXPECT_EQ(_prop->getLook("Face6").shininess, 0.5F);
    EXPECT_FALSE(_prop->isStated(Prop::Face, 4));
    EXPECT_EQ(_prop->getStatedLooks().size(), 2U);
}

TEST_F(PropertyElementAppearanceTest, aNameAmongManyThatIsNoElementsIsSaidAndTheRestAreTaken)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    std::vector<std::string> unknown;
    // "Face" is the object's, and no element: stepped over, not refused
    _prop->setStatedColors({{"Face", Green}, {"Face2", Red}, {"Wire1", Red}, {"nothing", Red}},
                           &unknown);
    EXPECT_EQ(unknown, (std::vector<std::string> {"Wire1", "nothing"}));
    EXPECT_EQ(_prop->getBase(Prop::Face).diffuseColor, Blue);
    EXPECT_EQ(_prop->getLook("Face2").diffuseColor, Red);
    EXPECT_EQ(_prop->getStatedLooks().size(), 1U);
}

TEST_F(PropertyElementAppearanceTest, manyAtOnceAreOneStepToUndo)
{
    _prop->setBase(Prop::Face, coloured(Blue));
    _doc->setUndoMode(1);
    _doc->openTransaction("paint");
    _prop->setStatedColors(palette(0, 40, 3U));
    _doc->commitTransaction();
    EXPECT_EQ(_prop->getStatedLooks().size(), 40U);
    _doc->undo();
    EXPECT_TRUE(_prop->getStatedLooks().empty());
    _doc->redo();
    EXPECT_EQ(_prop->getStatedLooks().size(), 40U);
}

TEST_F(PropertyElementAppearanceTest, aLookHeldAndNotGivenIsStatedAndNotDrawn)
{
    EXPECT_FALSE(_prop->hasKept());
    _prop->setKept(coloured(Green, 0.75F));
    ASSERT_TRUE(_prop->hasKept());
    EXPECT_FALSE(_prop->isEmpty());
    // No look is given by it
    EXPECT_FALSE(_prop->hasBase(Prop::Face));
    EXPECT_EQ(_prop->getDrawn(Prop::Face).getSize(), 0);
    EXPECT_TRUE(_prop->getStatedLooks().empty());
    EXPECT_EQ(_prop->getLook(Prop::Face, 0).diffuseColor,
              App::AppearanceList::defaultMaterial().diffuseColor);

    // It is of the value: another that has none is not the same, a copy is
    Prop* other = property(_doc->addObject("App::FeatureTest", "other"));
    ASSERT_NE(other, nullptr);
    EXPECT_FALSE(_prop->isSameStated(*other));
    std::unique_ptr<App::Property> copy(_prop->Copy());
    other->Paste(*copy);
    EXPECT_TRUE(_prop->isSameStated(*other));
    EXPECT_EQ(other->getKept().shininess, 0.75F);

    // In the file at either schema, and in a document saved and opened
    for (int schema : {4, 5}) {
        Prop* back = property(
            _doc->addObject("App::FeatureTest", ("back" + std::to_string(schema)).c_str()));
        ASSERT_NE(back, nullptr);
        restoreFromXML(*back, saveToXML(*_prop, schema));
        ASSERT_TRUE(back->hasKept()) << schema;
        EXPECT_EQ(back->getKept().diffuseColor, Green) << schema;
        EXPECT_EQ(back->getKept().shininess, 0.75F) << schema;
        EXPECT_FALSE(back->hasBase(Prop::Face)) << schema;
    }
    roundTrip();
    ASSERT_TRUE(_prop->hasKept());
    EXPECT_EQ(_prop->getKept().diffuseColor, Green);

    // One step to undo
    _doc->setUndoMode(1);
    _doc->openTransaction("let go");
    _prop->clearKept();
    _doc->commitTransaction();
    EXPECT_FALSE(_prop->hasKept());
    EXPECT_TRUE(_prop->isEmpty());
    _doc->undo();
    ASSERT_TRUE(_prop->hasKept());
    EXPECT_EQ(_prop->getKept().diffuseColor, Green);
}

TEST_F(PropertyElementAppearanceTest, aNameThatIsNoElementsIsRefused)
{
    EXPECT_THROW(_prop->setLook("Wire1", coloured(Red)), Base::ValueError);
    EXPECT_THROW(_prop->getLook("nothing"), Base::ValueError);
    EXPECT_FALSE(_prop->removeLook("nothing"));
    EXPECT_TRUE(_prop->isEmpty());
}
