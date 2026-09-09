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
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoIndexedFaceSet.h>
#include <Inventor/nodes/SoIndexedLineSet.h>
#include <Inventor/nodes/SoSeparator.h>

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

}  // namespace
