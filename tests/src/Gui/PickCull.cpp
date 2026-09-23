// The whole-scene pick cull: a ray that misses the document must not walk it.
//
// SoSeparator::rayPick skips a subtree only when the separator holds a valid
// bounding-box cache, and only a bounding-box traversal builds one. The
// unified selection root used to answer such a traversal from the renderer's
// own bounds and return without descending -- the right extent, at no cost,
// but it leaves no cache, so in render cache mode 3, the mode the renderer
// and every served view run in, nothing culled and every pick walked every
// object in the document.
//
// One half of that is out of reach here: the shortcut only triggered when
// the render cache manager had built a scene of its own, which takes a GL
// context, so what this file pins is the caching that the shortcut denied.
// The shortcut itself is measured in the app -- over 400 boxes, a miss in
// mode 3 cost 222 us against 13 us in mode 0.
//
// What is pinned here is the property rather than a timing: the objects
// beneath the root count the ray picks that reach them, and each carries its
// own pick culling turned OFF, so the cull at the root is the only thing that
// can stop a ray. A miss must reach nothing; a hit must reach something,
// which is what keeps the first assertion from being a check that cannot
// fail.

#include <gtest/gtest.h>

#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>

#include <App/Application.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/ViewParams.h>

namespace
{

/// A cube that says when a ray reached it.
class CountingCube: public SoCube
{
    using inherited = SoCube;
    SO_NODE_HEADER(CountingCube);

public:
    static void initClass()
    {
        SO_NODE_INIT_CLASS(CountingCube, SoCube, "Cube");
    }
    CountingCube()
    {
        SO_NODE_CONSTRUCTOR(CountingCube);
    }
    void rayPick(SoRayPickAction* action) override
    {
        ++picks;
        inherited::rayPick(action);
    }
    static int picks;

protected:
    ~CountingCube() override = default;
};

SO_NODE_SOURCE(CountingCube);
int CountingCube::picks = 0;

constexpr int ObjectCount = 40;
constexpr float Spacing = 5.0F;

class PickCullTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!SoDB::isInitialized()) {
            SoDB::init();
            SoInteraction::init();
            CountingCube::initClass();
            // Gui's own nodes register their action methods here, not in
            // SoDB::init: without this the root is dispatched as a plain
            // group and never reaches SoSeparator::rayPick, which is
            // where the cull under test lives.
            Gui::SoFCUnifiedSelection::initClass();
        }
        App::Application::Config()["ExeName"] = "PickCull_tests_run";
        int argc = 1;
        char exename[] = "PickCull_tests_run";
        char* argv[] = {exename, nullptr};
        App::Application::init(argc, argv);
    }

    void SetUp() override
    {
        // The mode the renderer and every served view run in, which is
        // where the cull went missing.
        renderCacheWas = Gui::ViewParams::getRenderCache();
        Gui::ViewParams::setRenderCache(3);

        // A row of unit cubes along x, each under its own separator, the
        // shape of a document's view providers beneath the root.
        scene = new Gui::SoFCUnifiedSelection;
        scene->ref();
        for (int i = 0; i < ObjectCount; ++i) {
            auto* obj = new SoSeparator;
            // Only the root may cull, so what the count reads is the one
            // whole-scene test and not each object's own.
            obj->pickCulling = SoSeparator::OFF;
            auto* move = new SoTranslation;
            move->translation = SbVec3f(float(i) * Spacing, 0.0F, 0.0F);
            obj->addChild(move);
            obj->addChild(new CountingCube);
            scene->addChild(obj);
        }

        // The pick root a view picks through: the camera has to be in it,
        // and the scene is its sibling.
        camera = new SoOrthographicCamera;
        root = new SoSeparator;
        root->ref();
        root->addChild(camera);
        root->addChild(scene);
        camera->viewAll(root, viewport);
    }

    void TearDown() override
    {
        root->unref();
        scene->unref();
        Gui::ViewParams::setRenderCache(renderCacheWas);
    }

    /// The bounding-box pass a view runs for auto clipping, which is what
    /// leaves the caches a pick culls with.
    void autoClipPass()
    {
        SoGetBoundingBoxAction action(viewport);
        action.apply(root);
    }

    /// Straight down -z from above \a x, \a y. Stated as a ray rather
    /// than a viewport point so that what the test says about the
    /// geometry is exact: no framing, no aspect ratio, no projection.
    SoPickedPoint* pickDownAt(float x, float y)
    {
        CountingCube::picks = 0;
        picker = std::make_unique<SoRayPickAction>(viewport);
        picker->setRay(SbVec3f(x, y, 100.0F), SbVec3f(0.0F, 0.0F, -1.0F));
        picker->apply(root);
        return picker->getPickedPoint();
    }

    SbViewportRegion viewport {800, 600};
    Gui::SoFCUnifiedSelection* scene = nullptr;
    SoOrthographicCamera* camera = nullptr;
    SoSeparator* root = nullptr;
    std::unique_ptr<SoRayPickAction> picker;
    int renderCacheWas = 0;
};

TEST_F(PickCullTest, aMissDoesNotWalkTheScene)
{
    autoClipPass();

    // A kilometre off the model, along a row that stretches only in x.
    EXPECT_EQ(pickDownAt(0.0F, 1000.0F), nullptr);
    EXPECT_EQ(CountingCube::picks, 0);
}

TEST_F(PickCullTest, aHitStillReachesTheGeometry)
{
    // The other half of the claim above: the count can move, so a zero
    // there means the cull fired and not that the ray was never cast.
    autoClipPass();

    EXPECT_NE(pickDownAt(0.0F, 0.0F), nullptr);
    EXPECT_GT(CountingCube::picks, 0);
}

TEST_F(PickCullTest, aChangedSceneStopsCullingUntilItIsMeasuredAgain)
{
    // Why the serving tier warms this before a pick: the cache is the only
    // thing a pick culls with, any change to the graph drops it, and only a
    // bounding-box traversal builds another. A process with no frame loop
    // runs no such traversal on its own.
    autoClipPass();
    EXPECT_EQ(pickDownAt(0.0F, 1000.0F), nullptr);
    EXPECT_EQ(CountingCube::picks, 0);

    scene->touch();
    EXPECT_EQ(pickDownAt(0.0F, 1000.0F), nullptr);
    EXPECT_EQ(CountingCube::picks, ObjectCount);

    autoClipPass();
    EXPECT_EQ(pickDownAt(0.0F, 1000.0F), nullptr);
    EXPECT_EQ(CountingCube::picks, 0);
}

TEST_F(PickCullTest, theRootStillAnswersTheScenesExtent)
{
    // Dropping the renderer's constant-time answer must not change what
    // the extent is: unit cubes two across, centred on their own origins,
    // spaced along x.
    SoGetBoundingBoxAction action(viewport);
    action.apply(scene);
    const SbBox3f box = action.getBoundingBox();
    ASSERT_FALSE(box.isEmpty());
    EXPECT_FLOAT_EQ(box.getMin()[0], -1.0F);
    EXPECT_FLOAT_EQ(box.getMin()[1], -1.0F);
    EXPECT_FLOAT_EQ(box.getMax()[0], float(ObjectCount - 1) * Spacing + 1.0F);
    EXPECT_FLOAT_EQ(box.getMax()[1], 1.0F);
}


}  // namespace
