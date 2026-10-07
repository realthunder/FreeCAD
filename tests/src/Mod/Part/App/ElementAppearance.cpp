// SPDX-License-Identifier: LGPL-2.1-or-later

/** The look of a shape's elements, on a shape that has names
 * (docs/ShapeAppearanceDesign.md sec 14.3): a look given to an element goes
 * to its name where the shape has a mapped name for it, and to its number
 * where it has not.
 */

#include "gtest/gtest.h"

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyElementAppearance.h>
#include "Mod/Part/App/FeaturePartBox.h"
#include "Mod/Part/App/FeaturePartCut.h"
#include <src/App/InitApplication.h>

#include "PartTestHelpers.h"

namespace
{
using Prop = App::PropertyElementAppearance;

App::MaterialAppearance coloured(uint32_t rgba, float shininess = 0.25F)
{
    App::MaterialAppearance mat;
    mat.diffuseColor.setPackedValue(rgba);
    mat.transparency = mat.diffuseColor.transparency();
    mat.shininess = shininess;
    return mat;
}

Prop* appearanceOf(App::DocumentObject* obj)
{
    auto prop = obj->getPropertyByName("ElementAppearance");
    if (!prop) {
        prop = obj->addDynamicProperty("App::PropertyElementAppearance", "ElementAppearance");
    }
    return dynamic_cast<Prop*>(prop);
}
}  // namespace

class PartElementAppearanceTest: public ::testing::Test,
                                 public PartTestHelpers::PartTestHelperClass
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        createTestDoc();
        _cut = dynamic_cast<Part::Cut*>(_doc->addObject("Part::Cut"));
        _cut->Base.setValue(_boxes[0]);
        _cut->Tool.setValue(_boxes[1]);
        _doc->recompute();
    }

    void TearDown() override
    {
        // Closed, so that nothing of it is still being written when the
        // process ends
        App::GetApplication().closeDocument(_doc->getName());
    }

    Part::Cut* _cut = nullptr;  // NOLINT Can't be private in a test framework
};

TEST_F(PartElementAppearanceTest, aFaceWithAMappedNameIsHeldByItsName)
{
    Prop* prop = appearanceOf(_cut);
    ASSERT_NE(prop, nullptr);
    const Part::TopoShape shape = _cut->Shape.getShape();
    ASSERT_GT(shape.getElementMapSize(), 0U);
    ASSERT_TRUE(prop->hasMappedName(Prop::Face, 0));
    EXPECT_EQ(prop->countElements(Prop::Face),
              static_cast<int>(shape.countSubShapes(TopAbs_FACE)));

    prop->setBase(Prop::Face, coloured(0x0000ffff));
    prop->setColor("Face1", App::Color(1.0F, 0.0F, 0.0F));
    ASSERT_EQ(prop->getNamedCount(), 1);
    EXPECT_EQ(prop->getNumbered(Prop::Face).getSize(), 0);
    EXPECT_EQ(prop->getValue(), _cut);
    EXPECT_EQ(prop->getOwn(Prop::Face, 0), Prop::OwnDiffuse);
    EXPECT_EQ(prop->getLook("Face1").diffuseColor, App::Color(1.0F, 0.0F, 0.0F));

    // The link holds it by its mapped name, and it is found by that too
    const std::string mapped = prop->getShadowSubs().front().first;
    ASSERT_FALSE(mapped.empty());
    EXPECT_EQ(prop->findNamed(mapped.c_str()), 0);
    EXPECT_EQ(prop->getLook(mapped.c_str()).diffuseColor, App::Color(1.0F, 0.0F, 0.0F));

    // By its number it is the same face, and the same name
    prop->setLook(Prop::Face, 0, coloured(0x00ff00ff, 0.75F));
    EXPECT_EQ(prop->getNamedCount(), 1);
    EXPECT_EQ(prop->getOwn(Prop::Face, 0), Prop::OwnAll);
    EXPECT_EQ(prop->getLook("Face1").shininess, 0.75F);

    EXPECT_TRUE(prop->removeLook(Prop::Face, 0));
    EXPECT_EQ(prop->getNamedCount(), 0);
    EXPECT_EQ(prop->getLook("Face1").diffuseColor, coloured(0x0000ffff).diffuseColor);
}

TEST_F(PartElementAppearanceTest, aNameIsStillItsFaceAfterTheShapeIsMadeAgain)
{
    Prop* prop = appearanceOf(_cut);
    ASSERT_NE(prop, nullptr);
    {
        Prop::Edit edit(*prop);
        prop->setColor("Face1", App::Color(1.0F, 0.0F, 0.0F));
        prop->setColor("Face2", App::Color(0.0F, 1.0F, 0.0F));
    }
    ASSERT_EQ(prop->getNamedCount(), 2);

    _boxes[0]->Height.setValue(_boxes[0]->Height.getValue() + 1.0);
    _doc->recompute();

    ASSERT_EQ(prop->getNamedCount(), 2);
    // Each by the name the shape counts it by now
    const auto& shadows = prop->getShadowSubs();
    EXPECT_EQ(prop->getNamedKind(0), Prop::Face);
    EXPECT_EQ(prop->getLook(shadows[0].second.c_str()).diffuseColor,
              App::Color(1.0F, 0.0F, 0.0F));
    EXPECT_EQ(prop->getLook(shadows[1].second.c_str()).diffuseColor,
              App::Color(0.0F, 1.0F, 0.0F));
}

TEST_F(PartElementAppearanceTest, aFaceTheShapeGivesNoNameIsHeldByItsNumber)
{
    // Whatever a primitive's faces are to the shape: held by name where it
    // names them, by number where it does not, and found either way
    Prop* prop = appearanceOf(_boxes[2]);
    ASSERT_NE(prop, nullptr);
    const bool named = prop->hasMappedName(Prop::Face, 0);
    prop->setColor("Face1", App::Color(1.0F, 0.0F, 0.0F));
    EXPECT_EQ(prop->getNamedCount(), named ? 1 : 0);
    EXPECT_EQ(prop->getNumbered(Prop::Face).getSize(), named ? 0 : 6);
    EXPECT_TRUE(prop->isStated(Prop::Face, 0));
    EXPECT_FALSE(prop->isStated(Prop::Face, 1));
    EXPECT_EQ(prop->getLook(Prop::Face, 0).diffuseColor, App::Color(1.0F, 0.0F, 0.0F));
}
