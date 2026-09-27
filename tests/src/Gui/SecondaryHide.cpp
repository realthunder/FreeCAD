// A secondary hide of one occurrence keeps the caches above it honest.
//
// ViewProvider::partialRender with the hidden marker -- and a LinkGroup
// element's hide list -- hides a node through ONE chain of selection roots:
// a secondary SoSelectionElementAction Hide stores a hideAll context in the
// node's contextMap2, keyed by that chain, and every traversal matches its
// own chain's tails against it (SoFCSelectionRoot::getNodeContext2). The
// same node reached through another chain -- a Link to the Part that holds
// it -- stays visible.
//
// The node is shared by both occurrences, and so is every separator between
// it and the roots, together with their bounding box caches. Two defects,
// found measuring the per-view hide (served edit root plan, step 1):
//
//   - a hidden root returned before it recorded anything, so the caches
//     above it were built without it and then answered for the occurrence
//     that shows it: a pick there was culled at the separator and missed;
//   - unhiding (Show) dropped the context without giving back the count
//     that makes the node invalidate the caches above it, so from then on
//     none of them was ever valid again and every pass walked the scene.
//
// The graph is the Part-and-Link shape reduced to its nodes:
//
//   root [camera, scene [A [G], B [offset, G]]],  G [X [cube], Y [move, cube]]
//
// G is the shared separator, A the Part's root and B the Link's; a probe
// in G counts the passes its cache does not answer.

#include <gtest/gtest.h>

#include <Inventor/SbViewportRegion.h>
#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPath.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/nodes/SoCube.h>
#include <Inventor/nodes/SoInfo.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTranslation.h>

#include <App/Application.h>
#include <Gui/SoFCUnifiedSelection.h>
#include <Gui/ViewParams.h>

namespace
{

/// Counts the bounding box passes that reach it. Put directly under a
/// separator, it is reached exactly when that separator's cache is not
/// valid -- a node below a selection root would not do, since the root's
/// own cache answers for it even while the root spoils the ones above.
class BoundsProbe: public SoInfo
{
    using inherited = SoInfo;
    SO_NODE_HEADER(BoundsProbe);

public:
    static void initClass()
    {
        SO_NODE_INIT_CLASS(BoundsProbe, SoInfo, "Info");
    }
    BoundsProbe()
    {
        SO_NODE_CONSTRUCTOR(BoundsProbe);
    }
    void getBoundingBox(SoGetBoundingBoxAction* action) override
    {
        ++passes;
        inherited::getBoundingBox(action);
    }
    int passes = 0;

protected:
    ~BoundsProbe() override = default;
};

SO_NODE_SOURCE(BoundsProbe);

constexpr float LinkOffset = 10.0F;
constexpr float YOffset = 5.0F;

class SecondaryHideTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!SoDB::isInitialized()) {
            SoDB::init();
            SoInteraction::init();
            BoundsProbe::initClass();
            // In SoFCDB's order: Gui's nodes and actions register their
            // methods in their own initClass, not in SoDB::init.
            Gui::SoFCUnifiedSelection::initClass();
            Gui::SoHighlightElementAction::initClass();
            Gui::SoSelectionElementAction::initClass();
            Gui::SoFCSeparator::initClass();
            Gui::SoFCSelectionRoot::initClass();
        }
        App::Application::Config()["ExeName"] = "SecondaryHide_tests_run";
        int argc = 1;
        char exename[] = "SecondaryHide_tests_run";
        char* argv[] = {exename, nullptr};
        App::Application::init(argc, argv);
    }

    void SetUp() override
    {
        renderCacheWas = Gui::ViewParams::getRenderCache();
        Gui::ViewParams::setRenderCache(3);

        x = new Gui::SoFCSelectionRoot(true);
        x->addChild(new SoCube);
        auto* y = new Gui::SoFCSelectionRoot(true);
        auto* move = new SoTranslation;
        move->translation = SbVec3f(YOffset, 0.0F, 0.0F);
        y->addChild(move);
        y->addChild(new SoCube);

        shared = new SoSeparator;
        sharedProbe = new BoundsProbe;
        shared->addChild(sharedProbe);
        shared->addChild(x);
        shared->addChild(y);

        a = new Gui::SoFCSelectionRoot(true);
        aProbe = new BoundsProbe;
        a->addChild(aProbe);
        a->addChild(shared);
        auto* b = new Gui::SoFCSelectionRoot(true);
        auto* offset = new SoTranslation;
        offset->translation = SbVec3f(0.0F, LinkOffset, 0.0F);
        b->addChild(offset);
        b->addChild(shared);

        scene = new Gui::SoFCUnifiedSelection;
        scene->ref();
        sceneProbe = new BoundsProbe;
        scene->addChild(sceneProbe);
        scene->addChild(a);
        scene->addChild(b);

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

    /// Hide (or show again) X in A's occurrence only, as partialRender does
    /// from the Part: the path starts at the Part's own root.
    void hideXInA(bool hide)
    {
        Gui::SoSelectionElementAction action(
            hide ? Gui::SoSelectionElementAction::Hide : Gui::SoSelectionElementAction::Show,
            true);
        auto* path = new SoPath(a);
        path->ref();
        path->append(shared);
        path->append(x);
        action.apply(path);
        path->unref();
    }

    /// One bounding box pass over the view, returning how many times it
    /// walked into the shared separator: zero once its cache is valid.
    /// The other probes count the scene root and A the same way.
    int boundingBoxPass()
    {
        for (auto* probe : {sharedProbe, aProbe, sceneProbe}) {
            probe->passes = 0;
        }
        SoGetBoundingBoxAction action(viewport);
        action.apply(root);
        return sharedProbe->passes;
    }

    bool picksAt(float px, float py)
    {
        SoRayPickAction picker(viewport);
        picker.setRay(SbVec3f(px, py, 100.0F), SbVec3f(0.0F, 0.0F, -1.0F));
        picker.apply(root);
        return picker.getPickedPoint() != nullptr;
    }

    SbViewportRegion viewport {800, 600};
    Gui::SoFCUnifiedSelection* scene = nullptr;
    Gui::SoFCSelectionRoot* a = nullptr;
    Gui::SoFCSelectionRoot* x = nullptr;
    SoSeparator* shared = nullptr;
    BoundsProbe* sharedProbe = nullptr;
    BoundsProbe* aProbe = nullptr;
    BoundsProbe* sceneProbe = nullptr;
    SoOrthographicCamera* camera = nullptr;
    SoSeparator* root = nullptr;
    int renderCacheWas = 0;
};

TEST_F(SecondaryHideTest, theGraphCachesWithNoHide)
{
    // What the other claims are measured against: with no hide, the pass
    // after a change walks in, and the next one is answered from the cache.
    shared->touch();
    EXPECT_GT(boundingBoxPass(), 0);
    EXPECT_EQ(boundingBoxPass(), 0);
    EXPECT_TRUE(picksAt(0.0F, 0.0F));
    EXPECT_TRUE(picksAt(0.0F, LinkOffset));
}

TEST_F(SecondaryHideTest, aHideInOneOccurrenceLeavesTheOtherPickable)
{
    hideXInA(true);
    boundingBoxPass();
    boundingBoxPass();

    EXPECT_FALSE(picksAt(0.0F, 0.0F)) << "X is hidden in A's occurrence";
    EXPECT_TRUE(picksAt(YOffset, 0.0F)) << "Y is not";
    EXPECT_TRUE(picksAt(0.0F, LinkOffset)) << "X is still shown through B";
}

TEST_F(SecondaryHideTest, showingAgainGivesTheCachesBack)
{
    hideXInA(true);
    boundingBoxPass();
    hideXInA(false);
    boundingBoxPass();

    EXPECT_EQ(boundingBoxPass(), 0);
    EXPECT_TRUE(picksAt(0.0F, 0.0F));
    EXPECT_TRUE(picksAt(0.0F, LinkOffset));
}

TEST_F(SecondaryHideTest, aHideSpoilsOnlyTheCachesInsideItsKey)
{
    // What a hide costs every frame: the view's auto clipping runs a
    // bounding box pass per render. The key [A, X] matches within A's
    // subtree, so only the caches opened inside A (and inside B, where
    // the same node answers the other way) may be spoiled; the scene
    // root's cache gets the same answer on every pass and must survive,
    // or each pass walks every object under it.
    hideXInA(true);
    boundingBoxPass();
    boundingBoxPass();

    EXPECT_EQ(sceneProbe->passes, 0) << "the scene root answered from its cache";
    EXPECT_FALSE(picksAt(0.0F, 0.0F));
    EXPECT_TRUE(picksAt(0.0F, LinkOffset));
}

TEST_F(SecondaryHideTest, theKeysFirstRootKeepsItsOwnCache)
{
    // A holds the whole key in its subtree, so its own cache answers the
    // same on every pass, and a pick can cull A with it. Only what A holds
    // -- G, shared with B -- is spoiled.
    hideXInA(true);
    boundingBoxPass();
    scene->touch();
    boundingBoxPass();

    EXPECT_GT(sceneProbe->passes, 0) << "the touched scene root was walked";
    EXPECT_EQ(aProbe->passes, 0) << "A answered from its own cache";
}

}  // namespace
