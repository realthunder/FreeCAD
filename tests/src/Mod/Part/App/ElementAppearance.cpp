// SPDX-License-Identifier: LGPL-2.1-or-later

/** The look of a shape's elements, on a shape that has names
 * (docs/ShapeAppearanceDesign.md sec 14.3): a look given to an element goes
 * to its name where the shape has a mapped name for it, and to its number
 * where it has not. And what the object makes of the looks, with no view
 * provider (sec 14.6.2): each name at the face it is now, and a face no
 * name paints in the look of the face it was made from.
 */

#include "gtest/gtest.h"

#include <cmath>

#include <App/Application.h>
#include <App/Document.h>
#include <App/PropertyElementAppearance.h>
#include <Mod/Part/App/PartFeature.h>
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
        // What is stated is what these cases are of; what the faces take
        // from the boxes is PartAppearanceMadeTest's
        _cut->MapFaceColor.setValue(false);
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

/** What the object makes of the looks (docs/ShapeAppearanceDesign.md sec
 * 14.6.2). The first box is 1 x 2 x 3 at the origin and the second cuts the
 * far half of it away: the face of the first at y = 0 is a face of the cut
 * as it was, and the face at y = 1 is made from a face of the second.
 */
class PartAppearanceMadeTest: public ::testing::Test, public PartTestHelpers::PartTestHelperClass
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
        // Whatever the preferences of where this runs say
        for (Part::Feature* obj : {static_cast<Part::Feature*>(_cut),
                                   static_cast<Part::Feature*>(_boxes[0]),
                                   static_cast<Part::Feature*>(_boxes[1])}) {
            obj->MapFaceColor.setValue(true);
            obj->MapLineColor.setValue(false);
            obj->MapPointColor.setValue(false);
            obj->MapTransparency.setValue(false);
        }
        _doc->recompute();
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(_doc->getName());
    }

    /// The face of \a obj that lies in the plane y = \a y, from 0; -1 if none
    static int faceAtY(const Part::Feature* obj, double y)
    {
        const Part::TopoShape shape = obj->Shape.getShape();
        const int count = static_cast<int>(shape.countSubShapes(TopAbs_FACE));
        for (int i = 0; i < count; ++i) {
            const Base::BoundBox3d box = shape.getSubTopoShape(TopAbs_FACE, i + 1).getBoundBox();
            if (std::abs(box.MinY - y) < 1e-6 && std::abs(box.MaxY - y) < 1e-6) {
                return i;
            }
        }
        return -1;
    }

    /// The look face \a index of \a obj is drawn in
    static App::MaterialAppearance drawn(const Part::Feature* obj, int index)
    {
        App::AppearanceList list;
        EXPECT_TRUE(obj->getDrawnAppearance(Prop::Face, list));
        return list.getSize() == 1 ? list.getBase() : list.getMaterial(index);
    }

    Part::Cut* _cut = nullptr;  // NOLINT Can't be private in a test framework
};

TEST_F(PartAppearanceMadeTest, aNewObjectIsGivenALookAndKeepsNoMore)
{
    for (const Part::Feature* obj : {static_cast<const Part::Feature*>(_cut),
                                     static_cast<const Part::Feature*>(_boxes[0])}) {
        const Prop& store = obj->ElementAppearance;
        EXPECT_TRUE(store.getStatedLooks().empty());
        for (Prop::Kind kind : {Prop::Face, Prop::Edge, Prop::Vertex}) {
            // Its own look, which nobody chose, and nothing made of it
            EXPECT_TRUE(store.hasBase(kind));
            EXPECT_TRUE(store.isFollowingMaterial(kind));
            EXPECT_EQ(store.getDrawn(kind).getSize(), 1);
            EXPECT_EQ(store.getNumbered(kind).getSize(), 0);
        }
        EXPECT_STREQ(store.getGroup(), "Appearances");
        EXPECT_STREQ(obj->MapFaceColor.getGroup(), "Appearances");
        // The faces are the material card's, which is the preference's
        EXPECT_EQ(store.getBase(Prop::Face).diffuseColor,
                  obj->getMaterialAppearance().diffuseColor);
    }
    // The edges of every object given the same look are one storage
    EXPECT_TRUE(_cut->ElementAppearance.getDrawn(Prop::Edge).isSameData(
        _boxes[0]->ElementAppearance.getDrawn(Prop::Edge)));
    App::AppearanceList list;
    ASSERT_TRUE(_cut->getDrawnAppearance(Prop::Face, list));
    ASSERT_EQ(list.getSize(), 1);
    // A look somebody gives is chosen, and the card's no more
    _cut->ElementAppearance.setBase(Prop::Face, coloured(0xff0000ff));
    EXPECT_FALSE(_cut->ElementAppearance.isFollowingMaterial(Prop::Face));
}

TEST_F(PartAppearanceMadeTest, aFaceTakesTheColourOfTheFaceItWasMadeFrom)
{
    const App::Color red(1.0F, 0.0F, 0.0F);
    const App::Color blue(0.0F, 0.0F, 1.0F);
    const App::Color green(0.0F, 1.0F, 0.0F);
    const int kept = faceAtY(_cut, 0.0);
    const int made = faceAtY(_cut, 1.0);
    ASSERT_GE(kept, 0);
    ASSERT_GE(made, 0);

    // No recompute: an object drawn differently tells what was made from it
    _boxes[0]->ElementAppearance.setBase(Prop::Face, coloured(red.getPackedValue()));
    _boxes[1]->ElementAppearance.setBase(Prop::Face, coloured(blue.getPackedValue()));
    EXPECT_FALSE(_cut->isTouched());
    EXPECT_EQ(drawn(_cut, kept).diffuseColor, red);
    EXPECT_EQ(drawn(_cut, made).diffuseColor, blue);
    const int count = static_cast<int>(_cut->Shape.getShape().countSubShapes(TopAbs_FACE));
    EXPECT_EQ(_cut->ElementAppearance.getDrawn(Prop::Face).getSize(), count);
    // Made, and not stated
    EXPECT_TRUE(_cut->ElementAppearance.getStatedLooks().empty());
    EXPECT_TRUE(_cut->ElementAppearance.isFollowingMaterial(Prop::Face));

    // The cut's own look is what its faces are in everything but the colour
    _cut->ElementAppearance.setBase(Prop::Face, coloured(green.getPackedValue(), 0.5F));
    EXPECT_EQ(drawn(_cut, kept).diffuseColor, red);
    EXPECT_EQ(drawn(_cut, kept).shininess, 0.5F);

    // A name outranks what is handed on, and is that face after the shape
    // is made again
    _cut->ElementAppearance.setColor(Prop::Face, made, App::Color(1.0F, 1.0F, 0.0F));
    ASSERT_EQ(_cut->ElementAppearance.getNamedCount(), 1);
    EXPECT_EQ(drawn(_cut, made).diffuseColor, App::Color(1.0F, 1.0F, 0.0F));
    EXPECT_EQ(drawn(_cut, kept).diffuseColor, red);
    _boxes[0]->Height.setValue(_boxes[0]->Height.getValue() + 1.0);
    _doc->recompute();
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 1.0)).diffuseColor, App::Color(1.0F, 1.0F, 0.0F));
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 0.0)).diffuseColor, red);

    // Taking nothing, every face is the cut's own but the one a name paints
    _cut->MapFaceColor.setValue(false);
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 0.0)).diffuseColor, green);
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 1.0)).diffuseColor, App::Color(1.0F, 1.0F, 0.0F));
    _cut->ElementAppearance.removeLook(Prop::Face, faceAtY(_cut, 1.0));
    EXPECT_EQ(_cut->ElementAppearance.getDrawn(Prop::Face).getSize(), 1);
    _cut->MapFaceColor.setValue(true);
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 0.0)).diffuseColor, red);
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 1.0)).diffuseColor, blue);
}

TEST_F(PartAppearanceMadeTest, aMaterialIsHandedOnWholeAndAColourAlone)
{
    const App::Color red(1.0F, 0.0F, 0.0F);
    const int front = faceAtY(_boxes[0], 0.0);
    const int back = faceAtY(_boxes[1], 1.0);
    ASSERT_GE(front, 0);
    ASSERT_GE(back, 0);
    _cut->ElementAppearance.setBase(Prop::Face, coloured(0x00ff00ff, 0.5F));
    // A face given a material, and a face only painted
    _boxes[0]->ElementAppearance.setLook(Prop::Face, front, coloured(red.getPackedValue(), 0.75F));
    _boxes[1]->ElementAppearance.setColor(Prop::Face, back, App::Color(0.0F, 0.0F, 1.0F));

    const App::MaterialAppearance whole = drawn(_cut, faceAtY(_cut, 0.0));
    EXPECT_EQ(whole.diffuseColor, red);
    EXPECT_EQ(whole.shininess, 0.75F);
    const App::MaterialAppearance painted = drawn(_cut, faceAtY(_cut, 1.0));
    EXPECT_EQ(painted.diffuseColor, App::Color(0.0F, 0.0F, 1.0F));
    EXPECT_EQ(painted.shininess, 0.5F);
}

TEST_F(PartAppearanceMadeTest, transparencyIsTakenOnlyWhereItIsAskedFor)
{
    App::MaterialAppearance glass = coloured(0xff0000ff);
    glass.transparency = 0.5F;
    glass.diffuseColor.setTransparency(0.5F);
    _boxes[0]->ElementAppearance.setBase(Prop::Face, glass);
    const int kept = faceAtY(_cut, 0.0);
    EXPECT_EQ(drawn(_cut, kept).diffuseColor.r, 1.0F);
    EXPECT_EQ(drawn(_cut, kept).transparency, 0.0F);
    _cut->MapTransparency.setValue(true);
    EXPECT_EQ(drawn(_cut, kept).transparency, 0.5F);
}

TEST_F(PartAppearanceMadeTest, aCopyMadeOnceStatesWhatItTakes)
{
    const App::Color red(1.0F, 0.0F, 0.0F);
    _boxes[0]->ElementAppearance.setBase(Prop::Face, coloured(red.getPackedValue()));
    auto copy = dynamic_cast<Part::Feature*>(_doc->addObject("Part::Feature"));
    ASSERT_NE(copy, nullptr);
    copy->Shape.setValue(_cut->Shape.getShape());
    // It links to nothing, so nothing is taken from where its faces came
    EXPECT_TRUE(copy->ElementAppearance.getStatedLooks().empty());
    EXPECT_EQ(copy->ElementAppearance.getDrawn(Prop::Face).getSize(), 1);

    copy->updateAppearance(_doc, true);
    const int count = static_cast<int>(copy->Shape.getShape().countSubShapes(TopAbs_FACE));
    EXPECT_EQ(copy->ElementAppearance.getNumbered(Prop::Face).getSize(), count);
    EXPECT_EQ(drawn(copy, faceAtY(copy, 0.0)).diffuseColor, red);
    // Stated: the box given another colour changes the cut and not the copy
    _boxes[0]->ElementAppearance.setBase(Prop::Face, coloured(0x00ff00ff));
    EXPECT_EQ(drawn(_cut, faceAtY(_cut, 0.0)).diffuseColor, App::Color(0.0F, 1.0F, 0.0F));
    EXPECT_EQ(drawn(copy, faceAtY(copy, 0.0)).diffuseColor, red);
}
