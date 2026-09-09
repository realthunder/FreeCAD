// SPDX-License-Identifier: LGPL-2.1-or-later

/// The per-client mirror viewer (docs/ThinClient.md sec 8.3, stage 3 of
/// sec 8.9). A serving process has one synthetic camera and every viewer
/// navigates with its own, so a click sent up as a world ray had nothing
/// to be resolved against. A mirror is what that client stated over the
/// wire -- its camera and its canvas -- and picking through it is what
/// makes a served click behave like a desktop one.
///
/// The two things worth an oracle of its own, because both are silent
/// when wrong:
///
///  * the round trip. The client builds a ray from a pixel; the mirror
///    projects it back to that pixel. Get the view volume slightly wrong
///    and the centre of the canvas still resolves perfectly while
///    everything else lands somewhere else -- which is exactly how the
///    first attempt behaved.
///  * the pick radius. Coin gives a ray set with SoRayPickAction::setRay
///    a radius of essentially zero and ignores setRadius() altogether,
///    so an edge or a vertex was unpickable from a browser. Resolved
///    through a mirror the radius applies, in pixels of the client's own
///    canvas.
///
/// Portrait is a case of its own rather than a variation. Coin's default
/// ADJUST_CAMERA mapping applies the height angle to the smaller viewport
/// dimension; a browser canvas applies it vertically at any aspect. The
/// two agree on a landscape canvas and disagree on a phone held upright.
///
/// No document, no main window, no QApplication, no GL: a mirror is a
/// camera, a viewport and a scene graph.

#include <gtest/gtest.h>

#include <cmath>

// Python.h before anything that pulls in Gui headers: ViewerContext
// declares a PyObject* row.
#include <Python.h>

#include <Inventor/SoDB.h>
#include <Inventor/SoInteraction.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/events/SoKeyboardEvent.h>
#include <Inventor/events/SoLocation2Event.h>
#include <Inventor/events/SoMouseButtonEvent.h>
#include <Inventor/nodes/SoEventCallback.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>

#include <Base/Matrix.h>

#include <Gui/MirrorViewer.h>

namespace
{

/// The quad lies in z = 0 over [-5, 5]^2; the line runs along its x = 5
/// side. Looking straight down, a ray a few pixels outside the quad has
/// only the line within reach, and only with a radius.
SoSeparator* makeScene()
{
    static const SbVec3f points[6] = {
        {-5, -5, 0}, {5, -5, 0}, {5, 5, 0}, {-5, 5, 0},   // the quad
        {5, -5, 0},  {5, 5, 0},                           // the line
    };
    static const int32_t face[5] = {0, 1, 2, 3, -1};
    static const int32_t line[3] = {4, 5, -1};

    auto* coords = new SoCoordinate3;
    coords->point.setValues(0, 6, points);
    auto* faces = new SoIndexedFaceSet;
    faces->coordIndex.setValues(0, 5, face);
    auto* lines = new SoIndexedLineSet;
    lines->coordIndex.setValues(0, 3, line);

    auto* root = new SoSeparator;
    root->ref();
    root->addChild(coords);
    root->addChild(faces);
    root->addChild(lines);
    return root;
}

constexpr float kEye = 60.0F;
constexpr float kHeightAngle = 0.7853981634F;   // 45 degrees

Gui::MirrorViewer::Camera perspectiveCamera(short width, short height)
{
    Gui::MirrorViewer::Camera camera;
    camera.perspective = true;
    camera.position.setValue(0, 0, kEye);
    camera.orientation = SbRotation::identity();   // looks down -z, +y up
    camera.heightOrAngle = kHeightAngle;
    camera.nearDistance = 10.0F;
    camera.farDistance = 200.0F;
    camera.aspectRatio = float(width) / float(height);
    camera.sizePixels.setValue(width, height);
    camera.pickRadius = 5.0F;
    return camera;
}

/// The ray a client casts through canvas pixel (px, py), built the way the
/// browser viewer builds it -- from the camera frame, not from a matrix,
/// and with the height angle applied vertically at any aspect.
void clientRay(const Gui::MirrorViewer::Camera& camera, float px, float py,
               SbVec3f& origin, SbVec3f& direction)
{
    const float width = float(camera.sizePixels[0]);
    const float height = float(camera.sizePixels[1]);
    const float nx = 2.0F * px / width - 1.0F;
    const float ny = 1.0F - 2.0F * py / height;
    const float th = std::tan(0.5F * camera.heightOrAngle);
    origin = camera.position;
    direction = SbVec3f(nx * th * camera.aspectRatio, ny * th, -1.0F);
    direction.normalize();
}

/// Where the ray through pixel (px, py) crosses the z = 0 plane. The
/// client's own arithmetic, so a pick can be aimed at a world feature by
/// name rather than by pixel.
float planeXAt(const Gui::MirrorViewer::Camera& camera, float px)
{
    const float width = float(camera.sizePixels[0]);
    const float nx = 2.0F * px / width - 1.0F;
    return nx * std::tan(0.5F * camera.heightOrAngle) * camera.aspectRatio
        * (camera.position[2]);
}

/// The pixel that shows world x on the z = 0 plane -- the inverse.
float pixelAtPlaneX(const Gui::MirrorViewer::Camera& camera, float x)
{
    const float width = float(camera.sizePixels[0]);
    const float nx = x
        / (std::tan(0.5F * camera.heightOrAngle) * camera.aspectRatio
           * camera.position[2]);
    return 0.5F * (nx + 1.0F) * width;
}

class MirrorViewerTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        if (!SoDB::isInitialized()) {
            SoDB::init();
            SoInteraction::init();
        }
    }

    void SetUp() override
    {
        scene = makeScene();
        mirror = std::make_unique<Gui::MirrorViewer>(nullptr, scene, nullptr, nullptr);
    }

    void TearDown() override
    {
        mirror.reset();
        scene->unref();
    }

    SoSeparator* scene = nullptr;
    std::unique_ptr<Gui::MirrorViewer> mirror;
};

TEST_F(MirrorViewerTest, statesNothingUntilTheClientDoes)
{
    // Until a client says where it is looking from there is no framing to
    // resolve anything against, and the caller has to fall back rather
    // than be handed an answer computed in a default camera.
    EXPECT_FALSE(mirror->hasCamera());
    EXPECT_EQ(mirror->getCamera(), nullptr);

    SbVec2f normPoint;
    EXPECT_FALSE(mirror->rayToNormPoint(SbVec3f(0, 0, 60), SbVec3f(0, 0, -1), normPoint));
    EXPECT_EQ(mirror->pickRay(SbVec3f(0, 0, 60), SbVec3f(0, 0, -1)), nullptr);
}

TEST_F(MirrorViewerTest, aRayComesBackToThePixelItWasBuiltFrom)
{
    const auto camera = perspectiveCamera(800, 600);
    mirror->setCamera(camera);
    ASSERT_TRUE(mirror->hasCamera());

    // The centre is the case that passes whatever the view volume is, so
    // it proves nothing on its own -- the corners are the test.
    const float pixels[][2] = {
        {400, 300}, {100, 120}, {700, 480}, {40, 560}, {760, 30},
    };
    for (const auto& pixel : pixels) {
        SbVec3f origin;
        SbVec3f direction;
        clientRay(camera, pixel[0], pixel[1], origin, direction);
        SbVec2f normPoint;
        ASSERT_TRUE(mirror->rayToNormPoint(origin, direction, normPoint));
        // Coin's normalized viewport point runs bottom-up.
        EXPECT_NEAR(normPoint[0] * 800.0F, pixel[0], 0.05F);
        EXPECT_NEAR((1.0F - normPoint[1]) * 600.0F, pixel[1], 0.05F);
    }
}

TEST_F(MirrorViewerTest, aPortraitCanvasResolvesLikeTheClientDrewIt)
{
    // The whole reason the mirror's camera does not use Coin's default
    // viewport mapping. Under ADJUST_CAMERA this case comes back scaled by
    // 1 / aspect and every off-centre pick lands wide.
    const auto camera = perspectiveCamera(600, 900);
    mirror->setCamera(camera);

    const float pixels[][2] = {
        {300, 450}, {80, 150}, {520, 800}, {30, 870},
    };
    for (const auto& pixel : pixels) {
        SbVec3f origin;
        SbVec3f direction;
        clientRay(camera, pixel[0], pixel[1], origin, direction);
        SbVec2f normPoint;
        ASSERT_TRUE(mirror->rayToNormPoint(origin, direction, normPoint));
        EXPECT_NEAR(normPoint[0] * 600.0F, pixel[0], 0.05F);
        EXPECT_NEAR((1.0F - normPoint[1]) * 900.0F, pixel[1], 0.05F);
    }
}

TEST_F(MirrorViewerTest, aRotatedCameraRoundTripsToo)
{
    // Every other case here looks straight down the world -z, where a
    // transposed or conjugated orientation is the identity's own mistake
    // and shows up as nothing at all. This is the case that says the
    // mirror reads the quaternion the way the client wrote it: the camera
    // looks down its own -z with +y up, so the client's frame is that
    // rotation applied to the local axes.
    auto camera = perspectiveCamera(800, 600);
    camera.orientation = SbRotation(SbVec3f(0.3F, -0.6F, 0.5F), 0.9F)
        * SbRotation(SbVec3f(1, 0, 0), 0.7F);
    camera.position.setValue(12.0F, -30.0F, 44.0F);
    mirror->setCamera(camera);

    SbVec3f forward;
    SbVec3f up;
    SbVec3f right;
    camera.orientation.multVec(SbVec3f(0, 0, -1), forward);
    camera.orientation.multVec(SbVec3f(0, 1, 0), up);
    camera.orientation.multVec(SbVec3f(1, 0, 0), right);
    const float th = std::tan(0.5F * camera.heightOrAngle);

    const float pixels[][2] = {
        {400, 300}, {120, 90}, {720, 510}, {50, 550},
    };
    for (const auto& pixel : pixels) {
        const float nx = 2.0F * pixel[0] / 800.0F - 1.0F;
        const float ny = 1.0F - 2.0F * pixel[1] / 600.0F;
        SbVec3f direction = forward + right * (nx * th * camera.aspectRatio)
            + up * (ny * th);
        direction.normalize();

        SbVec2f normPoint;
        ASSERT_TRUE(mirror->rayToNormPoint(camera.position, direction, normPoint));
        EXPECT_NEAR(normPoint[0] * 800.0F, pixel[0], 0.05F)
            << "pixel " << pixel[0] << "," << pixel[1];
        EXPECT_NEAR((1.0F - normPoint[1]) * 600.0F, pixel[1], 0.05F)
            << "pixel " << pixel[0] << "," << pixel[1];
    }
}

TEST_F(MirrorViewerTest, anOrthographicClientRoundTripsToo)
{
    Gui::MirrorViewer::Camera camera;
    camera.perspective = false;
    camera.position.setValue(0, 0, kEye);
    camera.orientation = SbRotation::identity();
    camera.heightOrAngle = 40.0F;   // the ortho height, in world units
    camera.nearDistance = 10.0F;
    camera.farDistance = 200.0F;
    camera.aspectRatio = 800.0F / 600.0F;
    camera.sizePixels.setValue(800, 600);
    mirror->setCamera(camera);

    // An orthographic ray starts on the near plane and every point on it
    // projects to the same place, so the client's origin is a point in the
    // plane rather than the eye.
    const SbVec3f origin(10.0F, -6.0F, kEye);
    const SbVec3f direction(0, 0, -1);
    SbVec2f normPoint;
    ASSERT_TRUE(mirror->rayToNormPoint(origin, direction, normPoint));
    // Half-width is 40 * (4/3) / 2 = 26.667, half-height 20.
    EXPECT_NEAR(normPoint[0], 0.5F + 10.0F / (2.0F * 26.6667F), 1e-4F);
    EXPECT_NEAR(normPoint[1], 0.5F - 6.0F / (2.0F * 20.0F), 1e-4F);
}

TEST_F(MirrorViewerTest, picksWhatTheRayCrosses)
{
    const auto camera = perspectiveCamera(800, 600);
    mirror->setCamera(camera);

    SbVec3f origin;
    SbVec3f direction;
    clientRay(camera, 400, 300, origin, direction);
    std::unique_ptr<SoPickedPoint> hit(mirror->pickRay(origin, direction));
    ASSERT_NE(hit, nullptr);
    EXPECT_NEAR(hit->getPoint()[0], 0.0F, 1e-3F);
    EXPECT_NEAR(hit->getPoint()[1], 0.0F, 1e-3F);
    EXPECT_NEAR(hit->getPoint()[2], 0.0F, 1e-3F);

    // Off the quad but still on it in x: the point follows the pixel.
    const float px = pixelAtPlaneX(camera, 3.0F);
    clientRay(camera, px, 300, origin, direction);
    hit.reset(mirror->pickRay(origin, direction));
    ASSERT_NE(hit, nullptr);
    EXPECT_NEAR(hit->getPoint()[0], 3.0F, 1e-2F);
}

TEST_F(MirrorViewerTest, thePickRadiusReachesAnEdgeTheRayMisses)
{
    const auto camera = perspectiveCamera(800, 600);
    mirror->setCamera(camera);

    // The line sits at x = 5, the quad ends there. A ray a few pixels
    // outside crosses neither: only the radius can reach the line.
    const float edgePx = pixelAtPlaneX(camera, 5.0F);
    SbVec3f origin;
    SbVec3f direction;

    clientRay(camera, edgePx + 3.0F, 300, origin, direction);
    std::unique_ptr<SoPickedPoint> near3(mirror->pickRay(origin, direction));
    EXPECT_NE(near3, nullptr) << "three pixels out is inside a five pixel radius";
    if (near3) {
        EXPECT_NEAR(near3->getPoint()[0], 5.0F, 0.5F);
    }

    clientRay(camera, edgePx + 20.0F, 300, origin, direction);
    std::unique_ptr<SoPickedPoint> near20(mirror->pickRay(origin, direction));
    EXPECT_EQ(near20, nullptr) << "twenty pixels out is well outside it";

    // And the radius is the client's, not a constant: a client that says
    // it is being driven by a fingertip reaches further.
    auto coarse = camera;
    coarse.pickRadius = 30.0F;
    mirror->setCamera(coarse);
    clientRay(coarse, edgePx + 20.0F, 300, origin, direction);
    std::unique_ptr<SoPickedPoint> coarseHit(mirror->pickRay(origin, direction));
    EXPECT_NE(coarseHit, nullptr);
}

TEST_F(MirrorViewerTest, switchingProjectionRebuildsTheCamera)
{
    mirror->setCamera(perspectiveCamera(800, 600));
    SoCamera* first = mirror->getCamera();
    ASSERT_NE(first, nullptr);

    // Same projection: the node stays, so nothing downstream that held it
    // for the duration of an event sees it swapped underneath.
    auto moved = perspectiveCamera(800, 600);
    moved.position.setValue(1, 2, kEye);
    mirror->setCamera(moved);
    EXPECT_EQ(mirror->getCamera(), first);
    EXPECT_EQ(mirror->getCamera()->position.getValue(), SbVec3f(1, 2, kEye));

    Gui::MirrorViewer::Camera ortho = moved;
    ortho.perspective = false;
    ortho.heightOrAngle = 40.0F;
    mirror->setCamera(ortho);
    EXPECT_NE(mirror->getCamera(), first);
    EXPECT_TRUE(mirror->hasCamera());
}

TEST_F(MirrorViewerTest, theViewportIsTheClientCanvas)
{
    const auto camera = perspectiveCamera(1024, 768);
    mirror->setCamera(camera);
    EXPECT_EQ(mirror->getViewportRegion().getViewportSizePixels(), SbVec2s(1024, 768));
    EXPECT_EQ(mirror->getPickRadius(), 5.0F);
    EXPECT_EQ(mirror->getSceneGraph(), scene);
    // The render manager is what an edit mode reaches through for the
    // camera and the viewport, and it must answer this client's.
    ASSERT_NE(mirror->getSoRenderManager(), nullptr);
    EXPECT_EQ(mirror->getSoRenderManager()->getCamera(), mirror->getCamera());
    EXPECT_EQ(mirror->getSoRenderManager()->getViewportRegion().getViewportSizePixels(),
              SbVec2s(1024, 768));
}

TEST_F(MirrorViewerTest, theCameraMathAgreesWithThePickPathAboutAPixel)
{
    // The edit path asks a view where a pixel is (getPointOnFocalPlane and
    // its neighbours); a pick asks it where a ray goes. Both are this
    // client's camera, so they have to be the same pixel -- and they are
    // written differently enough that they could quietly stop being, with
    // the centre of the canvas agreeing either way. Stage 4 is what would
    // find that out the hard way.
    const auto camera = perspectiveCamera(800, 600);
    mirror->setCamera(camera);

    // Where the mirror puts its focal plane, which is what
    // getPointOnFocalPlane answers against.
    const float focal = 0.5F * (camera.nearDistance + camera.farDistance);
    const float th = std::tan(0.5F * camera.heightOrAngle);

    const float pixels[][2] = {
        {400, 300}, {150, 200}, {650, 420}, {60, 540},
    };
    for (const auto& pixel : pixels) {
        const float nx = 2.0F * pixel[0] / 800.0F - 1.0F;
        const float ny = 1.0F - 2.0F * pixel[1] / 600.0F;
        // The client's own answer: along its ray, at the focal plane.
        const SbVec3f expected(nx * th * camera.aspectRatio * focal, ny * th * focal,
                               camera.position[2] - focal);

        // Coin's viewport pixels are bottom-up.
        const SbVec2s viewportPixel(short(pixel[0]), short(600.0F - pixel[1]));
        const SbVec3f got = mirror->getPointOnFocalPlane(viewportPixel);
        EXPECT_NEAR(got[0], expected[0], 0.02F) << "pixel " << pixel[0] << "," << pixel[1];
        EXPECT_NEAR(got[1], expected[1], 0.02F) << "pixel " << pixel[0] << "," << pixel[1];
        EXPECT_NEAR(got[2], expected[2], 0.02F) << "pixel " << pixel[0] << "," << pixel[1];

        // And back again, which is the row the draggers use.
        const SbVec2s back = mirror->getPointOnViewport(got);
        EXPECT_NEAR(float(back[0]), float(viewportPixel[0]), 1.0F);
        EXPECT_NEAR(float(back[1]), float(viewportPixel[1]), 1.0F);
    }
}

TEST_F(MirrorViewerTest, aDegenerateRayIsRefusedRatherThanResolved)
{
    mirror->setCamera(perspectiveCamera(800, 600));
    SbVec2f normPoint;
    EXPECT_FALSE(
        mirror->rayToNormPoint(SbVec3f(0, 0, kEye), SbVec3f(0, 0, 0), normPoint));
    EXPECT_EQ(mirror->pickRay(SbVec3f(0, 0, kEye), SbVec3f(0, 0, 0)), nullptr);
}

/// The editing root and what is done to it are ViewerContext's, shared by
/// the desktop viewer and the mirror (docs/ThinClient.md sec 8.9 stage 4).
/// A mirror is the only one of the two that can be built without a
/// QApplication, a GL context and a document, so it is where the shared
/// rows get their oracle.

TEST_F(MirrorViewerTest, theEditingRootIsBuiltEmptyAndOwned)
{
    // "Empty" is one child, always: the editing transform. Every test in
    // the edit path for "is anything being edited" reads getNumChildren()
    // > 1, so the count of an idle root is part of the contract.
    auto* editRoot = mirror->getEditRootNode();
    ASSERT_NE(editRoot, nullptr);
    ASSERT_TRUE(editRoot->isOfType(SoSeparator::getClassTypeId()));
    EXPECT_EQ(static_cast<SoSeparator*>(editRoot)->getNumChildren(), 1);
    EXPECT_FALSE(mirror->isEditingViewProvider());
    EXPECT_EQ(mirror->getEditingViewProvider(), nullptr);

    // Owned by the context, not by whatever graph it is hung in: it must
    // survive being taken out of one, which is what a mirror does at the
    // end of every edit.
    EXPECT_GE(editRoot->getRefCount(), 1);
}

TEST_F(MirrorViewerTest, anIdleEditingRootIsNotInTheServedGraph)
{
    // A connected client that is not editing must not put a separator in
    // everybody else's scene -- the graph is shared, and the publish
    // traverses it for every client.
    EXPECT_EQ(scene->findChild(mirror->getEditRootNode()), -1);
}

TEST_F(MirrorViewerTest, theEditingTransformIsTheMatrixItWasGiven)
{
    Base::Matrix4D mat;
    mat.move(Base::Vector3d(3, -4, 5));
    mirror->setEditingTransform(mat);

    auto* editRoot = static_cast<SoSeparator*>(mirror->getEditRootNode());
    ASSERT_GE(editRoot->getNumChildren(), 1);
    auto* node = editRoot->getChild(0);
    ASSERT_TRUE(node->isOfType(SoTransform::getClassTypeId()));

    const SbVec3f translation = static_cast<SoTransform*>(node)->translation.getValue();
    EXPECT_NEAR(translation[0], 3.0F, 1e-5F);
    EXPECT_NEAR(translation[1], -4.0F, 1e-5F);
    EXPECT_NEAR(translation[2], 5.0F, 1e-5F);
}

TEST_F(MirrorViewerTest, editingTheRootDoesNothingWithNobodyEditing)
{
    // setupEditingRoot with no editing view provider has nothing to hang
    // geometry for, and resetEditingRoot on an untouched root has nothing
    // to give back. Both are reached on teardown paths where the view
    // provider is already gone, so neither may act and neither may crash.
    auto* editRoot = static_cast<SoSeparator*>(mirror->getEditRootNode());
    auto* extra = new SoSeparator;
    mirror->setupEditingRoot(extra);
    EXPECT_EQ(editRoot->getNumChildren(), 1);

    mirror->resetEditingRoot();
    EXPECT_EQ(editRoot->getNumChildren(), 1);
}

/// The event path (docs/ThinClient.md sec 8.5, the `'E'` frame). A
/// mirror's events are handled over a graph of its own -- this client's
/// camera, this client's event callback, then the shared scene -- so one
/// client's callbacks never see another's events, and a pointer position
/// is resolved through the camera that client stated.

namespace
{
/// What a replayed event looked like by the time it reached a callback
/// installed through ViewerContext::addEventCallback.
struct Seen
{
    int count = 0;
    SbVec2s position {0, 0};
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    SbTime time;
    bool hit = false;      ///< the callback's own pick found geometry
    SbVec3f point {0, 0, 0};
    SoType type = SoType::badType();
    int key = 0;
};

void record(void* userdata, SoEventCallback* node)
{
    auto* seen = static_cast<Seen*>(userdata);
    const SoEvent* event = node->getEvent();
    ++seen->count;
    seen->position = event->getPosition();
    seen->shift = event->wasShiftDown();
    seen->ctrl = event->wasCtrlDown();
    seen->alt = event->wasAltDown();
    seen->time = event->getTime();
    seen->type = event->getTypeId();
    if (event->isOfType(SoKeyboardEvent::getClassTypeId())) {
        seen->key = int(static_cast<const SoKeyboardEvent*>(event)->getKey());
    }
    // The picked point the handler would use. This is the whole path in
    // one read: the camera has to be in the event root for there to be a
    // view volume at all, and the position has to have been flipped into
    // Coin's bottom-up viewport for it to land on the geometry.
    if (const SoPickedPoint* picked = node->getPickedPoint()) {
        seen->hit = true;
        seen->point = picked->getPoint();
    }
}

Gui::MirrorViewer::Input moveTo(int x, int y)
{
    Gui::MirrorViewer::Input input;
    input.kind = Gui::MirrorViewer::Input::Move;
    input.x = x;
    input.y = y;
    input.time = 1.5;
    return input;
}
}  // namespace

TEST_F(MirrorViewerTest, aReplayedMoveReachesTheCallbackAtThatPixel)
{
    mirror->setCamera(perspectiveCamera(800, 600));
    Seen seen;
    mirror->addEventCallback(SoEvent::getClassTypeId(), record, &seen);

    // Two thirds across, one quarter down the canvas.
    Gui::MirrorViewer::Input input = moveTo(533, 150);
    input.shift = true;
    input.alt = true;
    mirror->handleInput(input);

    EXPECT_EQ(seen.count, 1);
    EXPECT_EQ(seen.type, SoLocation2Event::getClassTypeId());
    // Coin's viewport origin is bottom left and the client's is top left.
    EXPECT_EQ(seen.position[0], 533);
    EXPECT_EQ(seen.position[1], 600 - 1 - 150);
    EXPECT_TRUE(seen.shift);
    EXPECT_FALSE(seen.ctrl);
    EXPECT_TRUE(seen.alt);
    EXPECT_NEAR(seen.time.getValue(), 1.5, 1e-6);

    mirror->removeEventCallback(SoEvent::getClassTypeId(), record, &seen);
}

TEST_F(MirrorViewerTest, aReplayedMoveCanPickWhatIsUnderIt)
{
    // The centre of the canvas looks straight down at the quad, and a
    // corner of the canvas looks past it. Both readings are needed: a
    // camera missing from the event root fails the first, and a position
    // that was never flipped passes the first and fails the second --
    // which is the same "correct at the centre" trap the round-trip
    // cases are built around.
    const Gui::MirrorViewer::Camera camera = perspectiveCamera(800, 600);
    mirror->setCamera(camera);
    Seen seen;
    mirror->addEventCallback(SoEvent::getClassTypeId(), record, &seen);

    mirror->handleInput(moveTo(400, 300));
    EXPECT_TRUE(seen.hit);
    EXPECT_NEAR(seen.point[0], 0.0F, 0.2F);
    EXPECT_NEAR(seen.point[1], 0.0F, 0.2F);

    // A pixel showing world y near +4, well inside the quad's +5 edge,
    // is ABOVE the centre on the client's canvas -- a smaller y, because
    // the client counts down. Read the world point back to say so.
    const float py = 300.0F - (pixelAtPlaneX(camera, 4.0F) - 400.0F);
    seen = Seen();
    mirror->handleInput(moveTo(400, int(py)));
    EXPECT_TRUE(seen.hit);
    EXPECT_NEAR(seen.point[1], 4.0F, 0.3F) << "pixel y " << py;

    // Off the quad entirely.
    seen = Seen();
    mirror->handleInput(moveTo(20, 20));
    EXPECT_FALSE(seen.hit);

    mirror->removeEventCallback(SoEvent::getClassTypeId(), record, &seen);
}

TEST_F(MirrorViewerTest, theButtonsAreThisClientsOwn)
{
    mirror->setCamera(perspectiveCamera(800, 600));
    EXPECT_EQ(mirror->mouseButtons(), Qt::NoButton);
    EXPECT_FALSE(mirror->isMouseButtonDown());

    Gui::MirrorViewer::Input press = moveTo(400, 300);
    press.kind = Gui::MirrorViewer::Input::Press;
    press.code = 1;
    mirror->handleInput(press);
    EXPECT_EQ(mirror->mouseButtons(), Qt::MouseButtons(Qt::LeftButton));
    EXPECT_TRUE(mirror->isMouseButtonDown());

    Gui::MirrorViewer::Input second = press;
    second.code = 3;
    mirror->handleInput(second);
    EXPECT_TRUE(mirror->mouseButtons().testFlag(Qt::LeftButton));
    EXPECT_TRUE(mirror->mouseButtons().testFlag(Qt::RightButton));

    Gui::MirrorViewer::Input release = press;
    release.kind = Gui::MirrorViewer::Input::Release;
    mirror->handleInput(release);
    EXPECT_FALSE(mirror->mouseButtons().testFlag(Qt::LeftButton));
    EXPECT_TRUE(mirror->mouseButtons().testFlag(Qt::RightButton));
}

TEST_F(MirrorViewerTest, aKeyKeepsThePointerWhereItWas)
{
    // A key event carries no position, and Coin's handlers read one off
    // every event -- so it has to be the last one the pointer was at,
    // not the zero the frame left in the field.
    mirror->setCamera(perspectiveCamera(800, 600));
    Seen seen;
    mirror->addEventCallback(SoEvent::getClassTypeId(), record, &seen);

    mirror->handleInput(moveTo(533, 150));
    Gui::MirrorViewer::Input key;
    key.kind = Gui::MirrorViewer::Input::KeyDown;
    key.code = int(SoKeyboardEvent::ESCAPE);
    key.time = 2.0;
    mirror->handleInput(key);

    EXPECT_EQ(seen.count, 2);
    EXPECT_EQ(seen.type, SoKeyboardEvent::getClassTypeId());
    EXPECT_EQ(seen.key, int(SoKeyboardEvent::ESCAPE));
    EXPECT_EQ(seen.position[0], 533);
    EXPECT_EQ(seen.position[1], 600 - 1 - 150);

    mirror->removeEventCallback(SoEvent::getClassTypeId(), record, &seen);
}

TEST_F(MirrorViewerTest, nothingIsReplayedBeforeTheClientStatesACamera)
{
    // Without a camera a pointer position is not a place in the world.
    // Resolving it through a default frustum would put every pick
    // somewhere plausible and wrong, so the event is refused instead.
    Seen seen;
    mirror->addEventCallback(SoEvent::getClassTypeId(), record, &seen);
    EXPECT_FALSE(mirror->handleInput(moveTo(400, 300)));
    EXPECT_EQ(seen.count, 0);
    mirror->removeEventCallback(SoEvent::getClassTypeId(), record, &seen);
}

TEST_F(MirrorViewerTest, oneClientsCallbackDoesNotSeeAnothersEvents)
{
    // The reason the event root is per client rather than in the shared
    // graph. Two mirrors on one served scene, one callback each.
    mirror->setCamera(perspectiveCamera(800, 600));
    Gui::MirrorViewer other(nullptr, scene, nullptr, nullptr);
    other.setCamera(perspectiveCamera(800, 600));

    Seen mine;
    Seen theirs;
    mirror->addEventCallback(SoEvent::getClassTypeId(), record, &mine);
    other.addEventCallback(SoEvent::getClassTypeId(), record, &theirs);

    mirror->handleInput(moveTo(400, 300));
    EXPECT_EQ(mine.count, 1);
    EXPECT_EQ(theirs.count, 0);

    other.handleInput(moveTo(100, 100));
    EXPECT_EQ(mine.count, 1);
    EXPECT_EQ(theirs.count, 1);

    mirror->removeEventCallback(SoEvent::getClassTypeId(), record, &mine);
    other.removeEventCallback(SoEvent::getClassTypeId(), record, &theirs);
}

TEST_F(MirrorViewerTest, theCurrentViewIsTheInnermostScope)
{
    // Gui::Document::setEdit asks which view an event is being handled in
    // before it goes looking for an active window. With nothing said, the
    // answer must be "nobody said", so that the desktop falls back to what
    // it always did rather than to some view left over from last time.
    EXPECT_EQ(Gui::ViewerContext::current(), nullptr);
    {
        Gui::ViewerScope outer(mirror.get());
        EXPECT_EQ(Gui::ViewerContext::current(), mirror.get());
        {
            Gui::ViewerScope inner(nullptr);
            EXPECT_EQ(Gui::ViewerContext::current(), nullptr);
        }
        EXPECT_EQ(Gui::ViewerContext::current(), mirror.get());
    }
    EXPECT_EQ(Gui::ViewerContext::current(), nullptr);
}

}  // namespace
