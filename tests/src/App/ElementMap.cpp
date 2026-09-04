// SPDX-License-Identifier: LGPL-2.1-or-later

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/ComplexGeoData.h>
#include <App/ElementMap.h>
#include <Base/BoundBox.h>
#include <src/App/InitApplication.h>

// NOLINTBEGIN(readability-magic-numbers)

// This fork keeps class Data::ElementMap inside src/App/ElementMap.cpp, so no
// other translation unit can name the type, let alone construct one. That is
// deliberate: the map is reached through Data::ComplexGeoData, which owns it
// and is the only thing that ever creates one.
//
// So this suite drives the map the way the rest of the codebase does, through
// ComplexGeoData's public API. Every call below is a thin pass-through to the
// ElementMap method of the same job -- setElementName -> ElementMap::addName,
// getElementMappedNames -> findAll, setMappedChildElements -> addChildElements,
// and so on -- so the coverage lands where it is meant to without the class
// leaving its translation unit.
//
// Upstream's equivalent suite constructs Data::ElementMap directly, because
// upstream moved setElementName and the hasher onto the map itself. See
// docs/UpstreamNameMap.md section 7 for that divergence.
//
// eraseElementName was added to ComplexGeoData for this suite: erasing a single
// mapped name had no public route before, only the whole indexed name.

namespace
{

/// The smallest concrete ComplexGeoData that can hold an element map.
class ElementMapHost: public Data::ComplexGeoData
{
public:
    explicit ElementMapHost(long tag = 0)
    {
        Tag = tag;
    }

    const std::vector<const char*>& getElementTypes() const override
    {
        static const std::vector<const char*> types {"Face", "Edge", "Vertex"};
        return types;
    }
    unsigned long countSubElements(const char* Type) const override
    {
        (void)Type;
        return 0;
    }
    Data::Segment* getSubElement(const char* Type, unsigned long number) const override
    {
        (void)Type;
        (void)number;
        return nullptr;
    }
    void setTransform(const Base::Matrix4D& rclTrf) override
    {
        (void)rclTrf;
    }
    Base::Matrix4D getTransform() const override
    {
        return {};
    }
    void transformGeometry(const Base::Matrix4D& rclMat) override
    {
        (void)rclMat;
    }
    Base::BoundBox3d getBoundBox() const override
    {
        // Spelled out rather than `return {}`: BoundBox3's constructor takes
        // its six bounds with defaults and is explicit, and MSVC will not use
        // an explicit constructor for an empty braced return.
        return Base::BoundBox3d();
    }
    bool isSame(const Data::ComplexGeoData& other) const override
    {
        return this == &other;
    }
};

/// A stand-in for a real part: six faces, mapped to themselves, and a hasher.
/// The faces are what the "mimic" cases below name against.
class LessComplexPart: public ElementMapHost
{
public:
    LessComplexPart(long tag, const std::string& nameStr, App::StringHasherRef hasher)
        : ElementMapHost(tag)
        , name(nameStr)
    {
        Hasher = hasher;
        // A part also has vertexes and so on, and the face count varies; that is
        // not important here because this is not a real model.
        for (int i = 1; i <= 6; ++i) {
            Data::IndexedName face("Face", i);
            setElementName(face, Data::MappedName(face));
        }
    }

    Data::MappedName name;
};

}  // namespace

class ElementMapTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        _docName = App::GetApplication().getUniqueDocumentName("test");
        App::GetApplication().newDocument(_docName.c_str(), "testUser");
        _sids = &_sid;
        _hasher = Base::Reference<App::StringHasher>(new App::StringHasher);
        ASSERT_EQ(_hasher.getRefCount(), 1);
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_docName.c_str());
    }

    std::string _docName;
    Data::ElementIDRefs _sid;
    QVector<App::StringIDRef>* _sids;
    App::StringHasherRef _hasher;
};

TEST_F(ElementMapTest, defaultConstruction)
{
    // Act
    ElementMapHost elementMap;

    // Assert
    EXPECT_EQ(elementMap.getElementMapSize(), 0);
}

TEST_F(ElementMapTest, setElementNameDefaults)
{
    // Arrange
    ElementMapHost elementMap;
    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");

    // Act
    auto resultName = elementMap.setElementName(element, mappedName);
    auto mappedToElement = elementMap.getMappedName(element);

    // Assert
    EXPECT_EQ(resultName, mappedName);
    EXPECT_EQ(mappedToElement, mappedName);
}

TEST_F(ElementMapTest, setElementNameNoOverwrite)
{
    // Arrange
    ElementMapHost elementMap;
    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");

    // Act
    auto resultName = elementMap.setElementName(element, mappedName);
    auto resultName2 = elementMap.setElementName(element, anotherMappedName, _sids, false);
    auto mappedToElement = elementMap.getMappedName(element);
    auto findAllResult = elementMap.getElementMappedNames(element);

    // Assert
    EXPECT_EQ(resultName, mappedName);
    EXPECT_EQ(resultName2, anotherMappedName);
    EXPECT_EQ(mappedToElement, mappedName);
    EXPECT_EQ(findAllResult.size(), 2);
    EXPECT_EQ(findAllResult[0].first, mappedName);
    EXPECT_EQ(findAllResult[1].first, anotherMappedName);
}

TEST_F(ElementMapTest, setElementNameWithOverwrite)
{
    // Arrange
    ElementMapHost elementMap;
    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");

    // Act
    auto resultName = elementMap.setElementName(element, mappedName);
    auto resultName2 = elementMap.setElementName(element, anotherMappedName, _sids, true);
    auto mappedToElement = elementMap.getMappedName(element);
    auto findAllResult = elementMap.getElementMappedNames(element);

    // Assert
    EXPECT_EQ(resultName, mappedName);
    EXPECT_EQ(resultName2, anotherMappedName);
    EXPECT_EQ(mappedToElement, anotherMappedName);
    EXPECT_EQ(findAllResult.size(), 1);
    EXPECT_EQ(findAllResult[0].first, anotherMappedName);
}

TEST_F(ElementMapTest, setElementNameWithHashing)
{
    // Arrange
    ElementMapHost elementMap;
    std::ostringstream ss;
    Data::IndexedName element("Edge", 1);
    Data::MappedName elementNameHolder(element);  // Will get modified by the encoder
    const Data::MappedName expectedName(element);

    // Act
    elementMap.encodeElementName(element.getType()[0], elementNameHolder, ss, nullptr, nullptr, 0);
    auto resultName = elementMap.setElementName(element, elementNameHolder, _sids);
    auto mappedToElement = elementMap.getMappedName(element);

    // Assert
    EXPECT_EQ(resultName, expectedName);
    EXPECT_EQ(mappedToElement, expectedName);
}

TEST_F(ElementMapTest, eraseMappedName)
{
    // Arrange
    ElementMapHost elementMap;
    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");
    elementMap.setElementName(element, mappedName);
    elementMap.setElementName(element, anotherMappedName);

    // Act
    auto sizeBefore = elementMap.getElementMapSize();
    auto findAllBefore = elementMap.getElementMappedNames(element);

    auto erased = elementMap.eraseElementName(anotherMappedName);
    auto sizeAfter = elementMap.getElementMapSize();
    auto findAllAfter = elementMap.getElementMappedNames(element);

    auto erasedRepeat = elementMap.eraseElementName(anotherMappedName);
    auto sizeAfterRepeat = elementMap.getElementMapSize();
    auto findAllAfterRepeat = elementMap.getElementMappedNames(element);

    // Assert
    EXPECT_EQ(sizeBefore, 2);
    EXPECT_EQ(findAllBefore.size(), 2);
    EXPECT_EQ(findAllBefore[0].first, mappedName);
    EXPECT_EQ(findAllBefore[1].first, anotherMappedName);

    // Only the named one goes; the element keeps its other name.
    EXPECT_TRUE(erased);
    EXPECT_EQ(sizeAfter, 1);
    EXPECT_EQ(findAllAfter.size(), 1);
    EXPECT_EQ(findAllAfter[0].first, mappedName);

    // Erasing it again is a no-op that reports it found nothing.
    EXPECT_FALSE(erasedRepeat);
    EXPECT_EQ(sizeAfterRepeat, 1);
    EXPECT_EQ(findAllAfterRepeat.size(), 1);
    EXPECT_EQ(findAllAfterRepeat[0].first, mappedName);
}

TEST_F(ElementMapTest, eraseElementNameUnmapped)
{
    // Arrange
    ElementMapHost elementMap;
    elementMap.setElementName(Data::IndexedName("Edge", 1), Data::MappedName("TEST"));

    // Act & Assert: erasing something that was never mapped reports false and
    // leaves the map alone.
    EXPECT_FALSE(elementMap.eraseElementName(Data::MappedName("NOSUCHNAME")));
    EXPECT_FALSE(elementMap.eraseElementName(Data::IndexedName("Edge", 99)));
    EXPECT_EQ(elementMap.getElementMapSize(), 1);
}

TEST_F(ElementMapTest, eraseIndexedName)
{
    // Arrange
    // Create two elements, edge1 and edge2, that have two mapped names each.
    ElementMapHost elementMap;

    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");
    elementMap.setElementName(element, mappedName);
    elementMap.setElementName(element, anotherMappedName);

    Data::IndexedName element2("Edge", 2);
    Data::MappedName mappedName2("TEST2");
    Data::MappedName anotherMappedName2("ANOTHERTEST2");
    elementMap.setElementName(element2, mappedName2);
    elementMap.setElementName(element2, anotherMappedName2);

    // Act
    auto sizeBefore = elementMap.getElementMapSize();
    auto findAllBefore = elementMap.getElementMappedNames(element2);

    elementMap.eraseElementName(element2);
    auto sizeAfter = elementMap.getElementMapSize();
    auto findAllAfter = elementMap.getElementMappedNames(element2);

    elementMap.eraseElementName(element2);
    auto sizeAfterRepeat = elementMap.getElementMapSize();
    auto findAllAfterRepeat = elementMap.getElementMappedNames(element2);

    // Assert
    EXPECT_EQ(sizeBefore, 4);
    EXPECT_EQ(findAllBefore.size(), 2);
    EXPECT_EQ(findAllBefore[0].first, mappedName2);
    EXPECT_EQ(findAllBefore[1].first, anotherMappedName2);

    EXPECT_EQ(sizeAfter, 2);
    EXPECT_EQ(findAllAfter.size(), 0);

    EXPECT_EQ(sizeAfterRepeat, 2);
    EXPECT_EQ(findAllAfterRepeat.size(), 0);
}

TEST_F(ElementMapTest, findMappedName)
{
    // Arrange
    // Create two elements, edge1 and edge2, that have two mapped names each.
    ElementMapHost elementMap;

    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");
    elementMap.setElementName(element, mappedName);
    elementMap.setElementName(element, anotherMappedName);

    Data::IndexedName element2("Edge", 2);
    Data::MappedName mappedName2("TEST2");
    Data::MappedName anotherMappedName2("ANOTHERTEST2");
    elementMap.setElementName(element2, mappedName2);
    elementMap.setElementName(element2, anotherMappedName2);

    // Act
    auto findResult = elementMap.getIndexedName(mappedName);
    auto findResult2 = elementMap.getIndexedName(mappedName2);

    // Assert
    EXPECT_EQ(findResult, element);
    EXPECT_EQ(findResult2, element2);
}

TEST_F(ElementMapTest, findIndexedName)
{
    // Arrange
    // Create two elements, edge1 and edge2, that have two mapped names each.
    ElementMapHost elementMap;

    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");
    elementMap.setElementName(element, mappedName);
    elementMap.setElementName(element, anotherMappedName);

    Data::IndexedName element2("Edge", 2);
    Data::MappedName mappedName2("TEST2");
    Data::MappedName anotherMappedName2("ANOTHERTEST2");
    elementMap.setElementName(element2, mappedName2);
    elementMap.setElementName(element2, anotherMappedName2);

    // Act
    // they return the first mapped name
    auto findResult = elementMap.getMappedName(element);
    auto findResult2 = elementMap.getMappedName(element2);

    // Assert
    EXPECT_EQ(findResult, mappedName);
    EXPECT_EQ(findResult2, mappedName2);
}

TEST_F(ElementMapTest, findAll)
{
    // Arrange
    // Create two elements, edge1 and edge2, that have two mapped names each.
    ElementMapHost elementMap;

    Data::IndexedName element("Edge", 1);
    Data::MappedName mappedName("TEST");
    Data::MappedName anotherMappedName("ANOTHERTEST");
    elementMap.setElementName(element, mappedName);
    elementMap.setElementName(element, anotherMappedName);

    Data::IndexedName element2("Edge", 2);
    Data::MappedName mappedName2("TEST2");
    Data::MappedName anotherMappedName2("ANOTHERTEST2");
    elementMap.setElementName(element2, mappedName2);
    elementMap.setElementName(element2, anotherMappedName2);

    // Act
    auto findResult = elementMap.getElementMappedNames(element);
    auto findResult2 = elementMap.getElementMappedNames(element2);

    // Assert
    EXPECT_EQ(findResult.size(), 2);
    EXPECT_EQ(findResult[0].first, mappedName);
    EXPECT_EQ(findResult[1].first, anotherMappedName);
    EXPECT_EQ(findResult2.size(), 2);
    EXPECT_EQ(findResult2[0].first, mappedName2);
    EXPECT_EQ(findResult2[1].first, anotherMappedName2);
}

TEST_F(ElementMapTest, mimicOnePart)
{
    // Arrange
    //   pattern: new doc, create Cube
    //   for a single part, there is no "naming algo" to speak of
    std::ostringstream ss;
    auto docName = "Unnamed";
    LessComplexPart cube(1L, "Box", _hasher);

    // Act
    auto children = cube.getElementMap();
    ss << docName << "#" << cube.name << "." << cube.getMappedName(Data::IndexedName("Face", 6));

    // Assert
    EXPECT_EQ(children.size(), 6);
    EXPECT_EQ(children[0].index.toString(), "Face1");
    EXPECT_EQ(children[0].name.toString(), "Face1");
    EXPECT_EQ(children[1].index.toString(), "Face2");
    EXPECT_EQ(children[1].name.toString(), "Face2");
    EXPECT_EQ(children[2].index.toString(), "Face3");
    EXPECT_EQ(children[2].name.toString(), "Face3");
    EXPECT_EQ(children[3].index.toString(), "Face4");
    EXPECT_EQ(children[3].name.toString(), "Face4");
    EXPECT_EQ(children[4].index.toString(), "Face5");
    EXPECT_EQ(children[4].name.toString(), "Face5");
    EXPECT_EQ(children[5].index.toString(), "Face6");
    EXPECT_EQ(children[5].name.toString(), "Face6");
    EXPECT_EQ(ss.str(), "Unnamed#Box.Face6");
}

TEST_F(ElementMapTest, mimicSimpleUnion)
{
    // Arrange
    //   pattern: new doc, create Cube, create Cylinder, Union of both (Cube first)
    std::ostringstream ss;
    std::ostringstream finalSs;
    const char* docName = "Unnamed";

    LessComplexPart cube(1L, "Box", _hasher);
    LessComplexPart cylinder(2L, "Cylinder", _hasher);
    // Union (Fusion) operation via the Part Workbench
    LessComplexPart unionPart(3L, "Fusion", _hasher);

    // we are only going to simulate one face for testing purpose
    Data::IndexedName uface3("Face", 3);
    auto PartOp = "FUS";  // Part::OpCodes::Fuse;

    // Act
    //   act: simulate a union/fuse operation
    auto parent = cube.getElementMap()[5];
    Data::MappedName postfixHolder(Data::modPostfix() + "2");
    unionPart.encodeElementName(postfixHolder[0], postfixHolder, ss, nullptr, nullptr,
                                unionPart.Tag);
    auto postfixStr = postfixHolder.toString() + Data::elementMapPrefix() + PartOp;

    //   act: with the fuse op, name against the cube's Face6
    Data::MappedName uface3Holder(parent.index);
    // we will invoke the encoder for face 3
    unionPart.encodeElementName(uface3Holder[0], uface3Holder, ss, nullptr, postfixStr.c_str(),
                                cube.Tag);
    unionPart.setElementName(uface3, uface3Holder, nullptr, true);

    // act: generate a full toponame string for testing  purposes
    finalSs << docName << "#" << unionPart.name;
    finalSs << ".";
    finalSs << Data::elementMapPrefix() + unionPart.getMappedName(uface3).toString();
    finalSs << ".";
    finalSs << uface3;

    // Assert
    EXPECT_EQ(postfixStr, ":M2;FUS");
    EXPECT_EQ(unionPart.getMappedName(uface3).toString(), "Face6;:M2;FUS;:H1:8,F");
    EXPECT_EQ(finalSs.str(), "Unnamed#Fusion.;Face6;:M2;FUS;:H1:8,F.Face3");

    // explanation of "Fusion.;Face6;:M2;FUS;:H2:3,F" toponame
    // Note: every postfix is prefixed by semicolon
    // Note: the start/middle/end are separated by periods
    //
    // "Fusion" means that we are on the "Fusion" object.
    // "." we are done with the first part
    // ";Face6" means default inheritance comes from face 6 of the parent (which is a cube)
    // ";:M2" means that a Workbench op has happened. "M" is the "Mod" directory in the source tree?
    // ";FUS" means that a Fusion operation has happened. Notice the lack of a colon.
    // ";:H2" means the subtending object (cylinder) has a tag of 2
    // ":3" means the writing position is 3; literally how far into the current postfix we are
    // ",F" means are of type "F" which is short for "Face" of Face3 of Fusion.
    // "." we are done with the second part
    // "Face3" is the localized name
}

TEST_F(ElementMapTest, mimicOperationAgainstSelf)
{
    // Arrange
    //   pattern: new doc, create Cube, Mystery Op with self as target
    std::ostringstream ss;
    LessComplexPart finalPart(99L, "MysteryOp", _hasher);
    // we are only going to simulate one face for testing purpose
    Data::IndexedName uface3("Face", 3);
    auto PartOp = "MYS";
    auto ownFace6 = finalPart.getElementMap()[5];
    Data::MappedName uface3Holder(ownFace6.index);
    auto workbenchId = Data::modPostfix() + "9999";

    // Act
    //   act: with the mystery op, name against its own Face6 for some reason
    Data::MappedName postfixHolder(workbenchId);
    finalPart.encodeElementName(postfixHolder[0], postfixHolder, ss, nullptr, nullptr,
                                finalPart.Tag);
    auto postfixStr = postfixHolder.toString() + Data::elementMapPrefix() + PartOp;
    // we will invoke the encoder for face 3
    finalPart.encodeElementName(uface3Holder[0], uface3Holder, ss, nullptr, postfixStr.c_str(),
                                finalPart.Tag);
    // override not forced
    finalPart.setElementName(uface3, uface3Holder, nullptr, false);

    // Assert
    EXPECT_EQ(postfixStr, ":M9999;MYS");
    EXPECT_EQ(finalPart.getMappedName(uface3).toString(), "Face3");  // override not forced
    EXPECT_EQ(uface3Holder.toString(), "Face6;:M9999;MYS;:H63:b,F");
    // explaining ";Face6;:M2;MYS;:H2:3,F" name:
    //
    // ";Face6" means default inheritance comes from face 6 of the ownFace6 (which is itself)
    // ";:M9999" means that a Workbench op happened. "M" is the "Mod" directory in the source tree?
    // ";MYS" means that a "Mystery" operation has happened. Notice the lack of a colon.
    // ";:H63" means the subtending object (cylinder) has a tag of 99 (63 in hex)
    // ":b" means the writing position is b (hex); literally how far into the current postfix we are
    // ",F" means are of type "F" which is short for "Face" of Face3 of Fusion.
}

TEST_F(ElementMapTest, hasChildElementMapTest)
{
    // Arrange
    Data::MappedChildElements child =
        {Data::IndexedName("face", 1), 2, 7, 4L, Data::ElementMapPtr(), QByteArray(""), _sid};
    std::vector<Data::MappedChildElements> children = {child};
    LessComplexPart cubeFull(3L, "FullBox", _hasher);
    cubeFull.setMappedChildElements(children);
    //
    LessComplexPart cubeWithoutChildren(2L, "EmptyBox", _hasher);

    // Act
    bool resultFull = cubeFull.hasChildElementMap();
    bool resultWhenEmpty = cubeWithoutChildren.hasChildElementMap();

    // Assert
    EXPECT_TRUE(resultFull);
    EXPECT_FALSE(resultWhenEmpty);
}

TEST_F(ElementMapTest, hashChildMapsTest)
{
    // Arrange
    LessComplexPart cube(1L, "Box", _hasher);
    auto childOneName = Data::IndexedName("Ping", 1);
    Data::MappedChildElements childOne = {
        childOneName,
        2,
        7,
        3L,
        Data::ElementMapPtr(),
        QByteArray("abcdefghij"),  // postfix must be 10 or more bytes to invoke hasher
        _sid};
    std::vector<Data::MappedChildElements> children = {childOne};
    cube.setMappedChildElements(children);
    auto before = _hasher->getIDMap();

    // Act
    cube.hashChildMaps();

    // Assert
    auto after = _hasher->getIDMap();
    EXPECT_EQ(before.size(), 0);
    EXPECT_EQ(after.size(), 1);
}

TEST_F(ElementMapTest, addAndGetChildElementsTest)
{
    // Arrange
    LessComplexPart cube(1L, "Box", _hasher);
    Data::MappedChildElements childOne = {
        Data::IndexedName("Ping", 1),
        2,
        7,
        3L,
        Data::ElementMapPtr(),
        QByteArray("abcdefghij"),  // postfix must be 10 or more bytes to invoke hasher
        _sid};
    Data::MappedChildElements childTwo =
        {Data::IndexedName("Pong", 2), 2, 7, 4L, Data::ElementMapPtr(), QByteArray("abc"), _sid};
    std::vector<Data::MappedChildElements> children = {childOne, childTwo};

    // Act
    cube.setMappedChildElements(children);
    auto result = cube.getMappedChildElements();

    // Assert
    EXPECT_EQ(result.size(), 2);
    EXPECT_TRUE(std::any_of(result.begin(), result.end(), [](const Data::MappedChildElements& e) {
        return e.indexedName.toString() == "Ping1";
    }));
    EXPECT_TRUE(std::any_of(result.begin(), result.end(), [](const Data::MappedChildElements& e) {
        return e.indexedName.toString() == "Pong2";
    }));
}

// NOLINTEND(readability-magic-numbers)
