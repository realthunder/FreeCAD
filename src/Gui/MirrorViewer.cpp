/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <cmath>

#include <Inventor/SbLine.h>
#include <Inventor/SbPlane.h>
#include <Inventor/SbViewVolume.h>
#include <Inventor/SoPath.h>
#include <Inventor/SoPickedPoint.h>
#include <Inventor/SoRenderManager.h>
#include <Inventor/actions/SoGetBoundingBoxAction.h>
#include <Inventor/actions/SoGetMatrixAction.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <Inventor/nodes/SoGroup.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#endif

#include <Base/Converter.h>
#include <Base/Exception.h>
#include <Base/Matrix.h>
#include <Base/Placement.h>

#include "InventorBase.h"
#include "MirrorViewer.h"
#include "Utilities.h"
#include "ViewProvider.h"

using namespace Gui;

/// A mirror has no screen to ask, and the client that does is a browser,
/// whose logical pixel is defined against exactly this.
constexpr double kCssDotsPerInch = 96.0;

namespace
{
/// Foot of the perpendicular from \a point to \a plane.
///
/// This and lineIntersection below are copies of the two file-static
/// helpers in View3DInventorViewer.cpp, because getPointOnLine has to give
/// the same answer as the desktop's and the construction is what makes it
/// the same answer. They -- and most of the camera-math block further down,
/// which is the desktop's arithmetic over a client's camera -- want hoisting
/// into ViewerContext, where both implementations could share one copy.
/// Stage 4 is when the edit path first exercises them and so when a shared
/// version could be shown to be right (docs/ThinClient.md sec 8.9).
SbVec3f projectOntoPlane(const SbVec3f& point, const SbPlane& plane)
{
    const SbVec3f normal = plane.getNormal();
    return point - normal * (normal.dot(point) + plane.getDistanceFromOrigin());
}

/// Where the line p11..p12 meets the line p21..p22, assuming they do.
SbVec3f lineIntersection(const SbVec3f& p11, const SbVec3f& p12, const SbVec3f& p21,
                         const SbVec3f& p22)
{
    const SbVec3f da = p12 - p11;
    const SbVec3f db = p22 - p21;
    const SbVec3f dc = p21 - p11;
    const double s = double(dc.cross(db).dot(da.cross(db))) / da.cross(db).sqrLength();
    return p11 + da * float(s);
}
}  // namespace

class MirrorViewer::Private
{
public:
    Document* doc = nullptr;
    SoNode* scene = nullptr;
    SoFCRenderCacheManager* cacheManager = nullptr;
    Render::Renderer* renderer = nullptr;

    /// The client's, as of its last 'C' frame.
    MirrorViewer::Camera state;
    bool stated = false;

    /** The camera node, rebuilt when the client switches projection.
     *
     * A node rather than the bare fields because picking needs it traversed
     * as a child, and because everything the edit path asks for is
     * SoCamera's own arithmetic.
     */
    SoCamera* camera = nullptr;

    /** The manager the edit path reaches for the camera and the viewport.
     *
     * Eighteen of the roughly sixty viewer calls an edit mode makes are
     * getSoRenderManager(), and every one of them wants one of those two
     * (docs/ThinClient.md section 8.3). It is deliberately never given a
     * scene graph: setSceneGraph() refs the root and attaches a node sensor
     * that fires on every change, which for one manager per connected client
     * is a cost with nothing behind it -- this manager never renders, and
     * getSceneGraph() answers from the mirror's own member.
     */
    SoRenderManager* renderManager = nullptr;

    SbViewportRegion viewport {short(1), short(1)};

    bool editing = false;
    bool selectionEnabled = true;
    /// Whether the editing root is currently a child of the served graph.
    bool editRootAttached = false;

    /// Put the editing root where the publish traversal will find it.
    void attachEditingRoot(SoNode* editRoot)
    {
        if (editRootAttached || !editRoot || !scene
            || !scene->isOfType(SoGroup::getClassTypeId())) {
            return;
        }
        static_cast<SoGroup*>(scene)->insertChild(editRoot, 0);
        editRootAttached = true;
    }

    void detachEditingRoot(SoNode* editRoot)
    {
        if (!editRootAttached) {
            return;
        }
        editRootAttached = false;
        if (!editRoot || !scene || !scene->isOfType(SoGroup::getClassTypeId())) {
            return;
        }
        auto* group = static_cast<SoGroup*>(scene);
        const int index = group->findChild(editRoot);
        if (index >= 0) {
            group->removeChild(index);
        }
    }

    ~Private()
    {
        if (camera) {
            camera->unref();
        }
        delete renderManager;
    }

    /// The scene as a pickable graph: the camera has to be traversed for
    /// SoRayPickAction to have a view volume at all.
    CoinPtr<SoSeparator> pickRoot() const
    {
        CoinPtr<SoSeparator> root(new SoSeparator, true);
        root->addChild(camera);
        root->addChild(scene);
        return root;
    }

    /** The view volume the pick action will see during traversal.
     *
     * The camera's own, from its stated aspect ratio, which is what the
     * LEAVE_ALONE viewport mapping makes traversal use too -- so a point
     * projected here and a ray the action computes from that point are the
     * same ray. See setCamera() for why the mapping is not the default.
     */
    SbViewVolume viewVolume() const
    {
        return camera->getViewVolume(0.0F);
    }

    /** A viewport pixel as the fraction of the canvas it sits at.
     *
     * The desktop's getNormalizedPosition does this and then corrects for
     * the aspect ratio, and the mirror deliberately does not. The reason is
     * the one difference between the two cameras: nothing sets a desktop
     * viewer's SoCamera::aspectRatio, so it stays 1 and getViewVolume()
     * hands back a SQUARE frustum, which that correction is what maps a
     * pixel into. A mirror's camera states the client's real aspect --
     * that is what a mirror is -- so its frustum already has it and
     * correcting again multiplies it in twice. Measured: every off-centre
     * x came back exactly aspect times too far out.
     *
     * So the mirror has one convention rather than two. Every row here and
     * the pick path share this space and the camera's own view volume, and
     * theCameraMathAgreesWithThePickPathAboutAPixel is what says so.
     */
    SbVec2f normalizedPosition(const SbVec2s& pnt) const
    {
        const SbVec2s& pixels = viewport.getViewportSizePixels();
        return {float(pnt[0]) / float(pixels[0]), float(pnt[1]) / float(pixels[1])};
    }

    /// Where the focal plane is, clamped into the frustum the way every
    /// desktop caller of it does.
    float focalDistance() const
    {
        const float nearDist = camera->nearDistance.getValue();
        const float farDist = camera->farDistance.getValue();
        float focal = camera->focalDistance.getValue();
        if (focal < nearDist || focal > farDist) {
            focal = 0.5F * (nearDist + farDist);
        }
        return focal;
    }
};

MirrorViewer::MirrorViewer(Document* doc, SoNode* scene,
                           SoFCRenderCacheManager* cacheManager, Render::Renderer* renderer)
    : pimpl(std::make_unique<Private>())
{
    pimpl->doc = doc;
    pimpl->scene = scene;
    pimpl->cacheManager = cacheManager;
    pimpl->renderer = renderer;
    pimpl->renderManager = new SoRenderManager;
}

MirrorViewer::~MirrorViewer()
{
    // A connection can drop in the middle of an edit. Give the view provider
    // its children back now, while this is still a MirrorViewer: the base
    // destructor cannot, because resetEditingRoot reaches getDocument() and
    // by then there is no override left to reach.
    resetEditingViewProvider();
}

void MirrorViewer::setCamera(const Camera& camera)
{
    const bool typeChanged = !pimpl->camera
        || (camera.perspective
            != (pimpl->camera->getTypeId() == SoPerspectiveCamera::getClassTypeId()));
    if (typeChanged) {
        if (pimpl->camera) {
            pimpl->camera->unref();
        }
        pimpl->camera = camera.perspective ? static_cast<SoCamera*>(new SoPerspectiveCamera)
                                           : static_cast<SoCamera*>(new SoOrthographicCamera);
        pimpl->camera->ref();
        // Not the ADJUST_CAMERA default, and this is the one place where
        // a mirror must not simply copy the desktop. Under ADJUST_CAMERA
        // Coin applies the height angle to the SMALLER viewport dimension
        // (below unit aspect it scales the volume by 1/aspect), which is
        // the Inventor convention a resizable window wants. A browser
        // canvas applies it vertically at every aspect. On a landscape
        // canvas the two agree, so nothing would have shown -- and on a
        // phone held upright, which is most of what this tier is for, every
        // pick would have been resolved through a frustum wider than the
        // one the client drew. LEAVE_ALONE takes the camera's stated aspect
        // as given, which is the client's convention exactly.
        pimpl->camera->viewportMapping.setValue(SoCamera::LEAVE_ALONE);
    }

    pimpl->camera->position.setValue(camera.position);
    pimpl->camera->orientation.setValue(camera.orientation);
    pimpl->camera->nearDistance.setValue(camera.nearDistance);
    pimpl->camera->farDistance.setValue(camera.farDistance);
    pimpl->camera->aspectRatio.setValue(camera.aspectRatio);
    // No focal distance on the wire: the client orbits about a point the
    // server has no name for, and everything that asks for one wants "the
    // middle of what is in view", which is what this is.
    pimpl->camera->focalDistance.setValue(
        0.5F * (camera.nearDistance + camera.farDistance));
    if (camera.perspective) {
        static_cast<SoPerspectiveCamera*>(pimpl->camera)
            ->heightAngle.setValue(camera.heightOrAngle);
    }
    else {
        static_cast<SoOrthographicCamera*>(pimpl->camera)
            ->height.setValue(camera.heightOrAngle);
    }

    // The WINDOW size first, and it is not optional. A viewport region
    // carries both a pixel size and a normalized one, and setViewportPixels
    // derives the second from the first against the window -- so setting
    // only the pixels on a default-constructed region (a 100x100 window)
    // leaves a canvas of 800x600 describing itself as 8.0 x 6.0 of its
    // window. Picking never noticed, because it reads the pixel size; every
    // row that asks where a pixel is in the world reads the normalized one,
    // and those came back hundreds of times out.
    pimpl->viewport.setWindowSize(camera.sizePixels);
    pimpl->viewport.setViewportPixels(SbVec2s(0, 0), camera.sizePixels);
    pimpl->renderManager->setCamera(pimpl->camera);
    pimpl->renderManager->setViewportRegion(pimpl->viewport);
    pimpl->state = camera;
    pimpl->stated = true;
}

SoCamera* MirrorViewer::getCamera() const
{
    return pimpl->camera;
}

bool MirrorViewer::hasCamera() const
{
    return pimpl->stated && pimpl->camera;
}

bool MirrorViewer::rayToNormPoint(const SbVec3f& origin, const SbVec3f& dir,
                                  SbVec2f& normPoint) const
{
    if (!hasCamera() || dir.sqrLength() <= 0.0F) {
        return false;
    }
    const SbViewVolume vv = pimpl->viewVolume();
    if (vv.getDepth() == 0.0F || vv.getWidth() == 0.0F || vv.getHeight() == 0.0F) {
        return false;
    }
    // Any point on the ray but the eye projects to the same place, so take
    // one comfortably inside the frustum: for a perspective camera the ray
    // origin IS the eye, where the projection divides by zero.
    SbVec3f along = dir;
    along.normalize();
    SbVec3f point = origin + along * (vv.getNearDist() + 0.5F * vv.getDepth());
    SbVec3f screen;
    vv.projectToScreen(point, screen);
    normPoint.setValue(screen[0], screen[1]);
    return true;
}

SoPickedPoint* MirrorViewer::pickRay(const SbVec3f& origin, const SbVec3f& dir) const
{
    SbVec2f normPoint;
    if (!pimpl->scene || !rayToNormPoint(origin, dir, normPoint)) {
        return nullptr;
    }
    SoRayPickAction action(pimpl->viewport);
    action.setNormalizedPoint(normPoint);
    action.setRadius(pimpl->state.pickRadius);
    action.apply(pimpl->pickRoot());
    SoPickedPoint* picked = action.getPickedPoint();
    return picked ? new SoPickedPoint(*picked) : nullptr;
}

SoNode* MirrorViewer::getSceneGraph() const
{
    return pimpl->scene;
}

SoRenderManager* MirrorViewer::getSoRenderManager() const
{
    return pimpl->renderManager;
}

SoEventManager* MirrorViewer::getSoEventManager() const
{
    // Stage 4: the event stream is what an event manager would run, and
    // nothing streams events yet (docs/ThinClient.md section 8.9).
    return nullptr;
}

const SbViewportRegion& MirrorViewer::getViewportRegion() const
{
    return pimpl->viewport;
}

Gui::Document* MirrorViewer::getDocument()
{
    return pimpl->doc;
}

SoFCRenderCacheManager* MirrorViewer::getRenderCacheManager() const
{
    return pimpl->cacheManager;
}

Render::Renderer* MirrorViewer::getExternalRenderer() const
{
    return pimpl->renderer;
}

float MirrorViewer::getPickRadius() const
{
    return pimpl->state.pickRadius;
}

double MirrorViewer::devicePixelRatio() const
{
    return pimpl->state.devicePixelRatio;
}

Qt::MouseButtons MirrorViewer::mouseButtons() const
{
    // Stage 4 again: this answers from the client's button bits once they
    // ride the event stream. Until then no button is down, which is the
    // truth for a mirror driven by picks alone.
    return Qt::NoButton;
}

double MirrorViewer::logicalDotsPerInchX() const
{
    return kCssDotsPerInch;
}

SbVec3f MirrorViewer::getViewDirection() const
{
    if (!hasCamera()) {
        return {0, 0, -1};
    }
    return pimpl->camera->getViewVolume().getProjectionDirection();
}

SbVec3f MirrorViewer::getCenterPointOnFocalPlane() const
{
    if (!hasCamera()) {
        return {0, 0, 0};
    }
    SbVec3f direction;
    pimpl->camera->orientation.getValue().multVec(SbVec3f(0, 0, -1), direction);
    return pimpl->camera->position.getValue()
        + pimpl->camera->focalDistance.getValue() * direction;
}

SbVec3f MirrorViewer::getPointOnFocalPlane(const SbVec2s& pnt) const
{
    if (!hasCamera()) {
        return {};
    }
    const SbViewVolume vol = pimpl->camera->getViewVolume();
    SbLine line;
    vol.projectPointToLine(pimpl->normalizedPosition(pnt), line);
    SbVec3f point;
    vol.getPlane(pimpl->focalDistance()).intersect(line, point);
    return point;
}

SbVec3f MirrorViewer::getPointOnXYPlaneOfPlacement(const SbVec2s& pnt,
                                                   const Base::Placement& plc) const
{
    if (!hasCamera()) {
        THROWM(Base::RuntimeError, "No camera stated by this client")
    }
    const SbViewVolume vol = pimpl->camera->getViewVolume();
    SbLine line;
    vol.projectPointToLine(pimpl->normalizedPosition(pnt), line);

    const Base::Vector3d normalVector = plc.getRotation().multVec(Base::Vector3d(0, 0, 1));
    const SbPlane plane(Base::convertTo<SbVec3f>(normalVector),
                        Base::convertTo<SbVec3f>(plc.getPosition()));
    SbVec3f point;
    if (plane.intersect(line, point)) {
        return point;
    }
    THROWM(Base::RuntimeError, "No intersection found")
}

SbVec3f MirrorViewer::getPointOnLine(const SbVec2s& pnt, const SbVec3f& axisCenter,
                                     const SbVec3f& axis) const
{
    if (!hasCamera()) {
        return {};
    }
    // The desktop's construction, not an equivalent of it: the point on the
    // axis whose projection onto the focal plane is nearest the pointer's.
    // The obvious shortcut -- the point on the axis nearest the pick ray --
    // is the same answer only for an orthographic camera, where the ray runs
    // along the focal normal, and this tier's clients are perspective.
    const SbViewVolume vol = pimpl->camera->getViewVolume();
    const SbPlane focalPlane = vol.getPlane(pimpl->focalDistance());
    SbLine ray;
    SbVec3f onFocalPlane;
    vol.projectPointToLine(pimpl->normalizedPosition(pnt), ray);
    focalPlane.intersect(ray, onFocalPlane);

    // An axis pointing at the eye has no direction on the focal plane to be
    // near, so the pointer's own focal-plane point is the whole answer.
    const SbVec3f focalNormal = focalPlane.getNormal();
    if (std::fabs(axis.dot(focalNormal)) > 1.0F - 1e-6F) {
        return onFocalPlane;
    }
    const SbLine projected(projectOntoPlane(axisCenter, focalPlane),
                           projectOntoPlane(axisCenter + axis, focalPlane));
    const SbVec3f onProjected = projected.getClosestPoint(onFocalPlane);
    return lineIntersection(onProjected, onProjected + focalNormal, axisCenter,
                            axisCenter + axis);
}

SbVec2s MirrorViewer::getPointOnViewport(const SbVec3f& pnt) const
{
    if (!hasCamera()) {
        return {0, 0};
    }
    const SbVec2s& size = pimpl->viewport.getViewportSizePixels();
    SbViewVolume vol =
        pimpl->camera->getViewVolume(pimpl->viewport.getViewportAspectRatio());
    SbVec3f point(pnt);
    vol.projectToScreen(point, point);
    return {short(std::lround(point[0] * size[0])), short(std::lround(point[1] * size[1]))};
}

SbVec2f MirrorViewer::screenCoordsOfPath(SoPath* path) const
{
    if (!hasCamera()) {
        return {0, 0};
    }
    SoGetMatrixAction gma(pimpl->viewport);
    gma.apply(path);

    SbVec3f coords(0, 0, 0);
    gma.getMatrix().transpose().multMatrixVec(coords, coords);
    pimpl->camera->getViewVolume().projectToScreen(coords, coords);

    // The projection is square-normalized, so the shorter side sets the
    // scale and the longer one is centred -- the desktop's own arithmetic,
    // over the client's canvas instead of a GL widget.
    const SbVec2s& size = pimpl->viewport.getViewportSizePixels();
    const float width = float(size[0]);
    const float height = float(size[1]);
    if (width >= height) {
        coords[0] = coords[0] * height + 0.5F * (width - height);
        coords[1] *= height;
    }
    else {
        coords[0] *= width;
        coords[1] = coords[1] * width + 0.5F * (height - width);
    }
    return {coords[0], coords[1]};
}

void MirrorViewer::getNearPlane(SbVec3f& rcPt, SbVec3f& rcNormal) const
{
    if (!hasCamera()) {
        return;
    }
    const SbViewVolume vol = pimpl->camera->getViewVolume();
    const SbPlane nearPlane = vol.getPlane(vol.nearDist);
    const float dist = nearPlane.getDistanceFromOrigin();
    rcNormal = nearPlane.getNormal();
    rcNormal.normalize();
    rcPt.setValue(dist * rcNormal[0], dist * rcNormal[1], dist * rcNormal[2]);
}

float MirrorViewer::getMaxDimension() const
{
    if (!hasCamera()) {
        return 0.0F;
    }
    float height = pimpl->state.heightOrAngle;
    if (pimpl->state.perspective) {
        height = 2.0F * std::tan(0.5F * pimpl->state.heightOrAngle)
            * pimpl->camera->focalDistance.getValue();
    }
    float width = height;
    const float ratio = pimpl->viewport.getViewportAspectRatio();
    if (ratio > 1.0F) {
        width *= ratio;
    }
    else {
        height *= ratio;
    }
    return std::max(height, width);
}

bool MirrorViewer::getSceneBoundBox(SbBox3f& box) const
{
    if (!pimpl->scene) {
        return false;
    }
    SoGetBoundingBoxAction action(pimpl->viewport);
    action.apply(pimpl->scene);
    const SbBox3f bbox = action.getBoundingBox();
    if (bbox.isEmpty()) {
        return false;
    }
    box = bbox;
    return true;
}

void MirrorViewer::setCameraOrientation(const SbRotation& orientation, bool moveToCenter)
{
    // Applied so that anything computed straight afterwards sees it, but the
    // client is the authority on its own camera and the next 'C' frame
    // overwrites this. Turning a browser's view from the server is a
    // downlink message, not a mirror operation.
    if (!hasCamera()) {
        return;
    }
    if (moveToCenter) {
        const SbVec3f focal = getCenterPointOnFocalPlane();
        SbVec3f direction;
        orientation.multVec(SbVec3f(0, 0, -1), direction);
        pimpl->camera->position.setValue(
            focal - pimpl->camera->focalDistance.getValue() * direction);
    }
    pimpl->camera->orientation.setValue(orientation);
    pimpl->state.orientation = orientation;
    pimpl->state.position = pimpl->camera->position.getValue();
}

std::vector<SbVec2f> MirrorViewer::getGLPolygon(const std::vector<SbVec2s>& pnts) const
{
    // In the same space as everything else here, for the same reason:
    // whatever these points are compared against was projected through this
    // mirror's camera (see Private::normalizedPosition).
    std::vector<SbVec2f> poly;
    poly.reserve(pnts.size());
    const SbVec2s& origin = pimpl->viewport.getViewportOriginPixels();
    for (const auto& pnt : pnts) {
        poly.push_back(pimpl->normalizedPosition(pnt - origin));
    }
    return poly;
}

SoPickedPoint* MirrorViewer::getPointOnRay(const SbVec2s& pos, const ViewProvider* vp) const
{
    if (!hasCamera() || !pimpl->scene || !vp) {
        return nullptr;
    }
    // While a mode is editing this view provider its children are not under
    // its own root any more -- setupEditingRoot moved them under the editing
    // root -- so searching for that root would find an empty node and pick
    // nothing. This is the desktop's rule, and it is the one that makes a
    // sketcher drag able to grab what it drew.
    // Both of these outlive the branch on purpose: an SoSearchAction owns
    // the path it hands back, so a path read from one that has gone out of
    // scope is a dangling pointer.
    SoSearchAction search;
    CoinPtr<SoPath> editPath;
    SoPath* path = nullptr;
    if (vp == editViewProvider && pcEditingRoot->getNumChildren() > 1) {
        editPath = CoinPtr<SoPath>(new SoPath, true);
        editPath->append(pcEditingRoot);
        path = editPath;
    }
    else {
        search.setNode(vp->getRoot());
        search.setSearchingAll(true);
        search.apply(pimpl->scene);
        path = search.getPath();
    }
    if (!path) {
        return nullptr;
    }

    // The desktop's pattern: a throwaway graph of the camera, the subgraph's
    // accumulated transform, and the subgraph, so the pick is confined to
    // this view provider and still lands in world space.
    SoGetMatrixAction gma(pimpl->viewport);
    gma.apply(path);
    auto* transform = new SoTransform;
    transform->setMatrix(gma.getMatrix());

    CoinPtr<SoSeparator> root(new SoSeparator, true);
    root->addChild(pimpl->camera);
    root->addChild(transform);
    root->addChild(path->getTail());

    SoRayPickAction action(pimpl->viewport);
    action.setPoint(pos);
    action.setRadius(pimpl->state.pickRadius);
    action.apply(root);
    SoPickedPoint* picked = action.getPickedPoint();
    return picked ? new SoPickedPoint(*picked) : nullptr;
}

SoPickedPoint* MirrorViewer::getPointOnRay(const SbVec3f& pos, const SbVec3f& dir,
                                           const ViewProvider* vp) const
{
    SbVec2f normPoint;
    if (!vp || !rayToNormPoint(pos, dir, normPoint)) {
        return nullptr;
    }
    const SbVec2s& size = pimpl->viewport.getViewportSizePixels();
    return getPointOnRay(SbVec2s(short(std::lround(normPoint[0] * size[0])),
                                 short(std::lround(normPoint[1] * size[1]))),
                         vp);
}

void MirrorViewer::appendDetailPath(SoPath* path, ViewProvider* vp)
{
    // The desktop prefixes the path with its own view-provider root and the
    // group it keeps view furniture in. A mirror shares the served graph,
    // where a view provider's root is a direct child, so there is nothing to
    // prefix -- which is the rule SoFCUnifiedSelection::beginDetailPath
    // already states for a view-less root, and takes by asking whether it
    // has a viewer at all. That field is a View3DInventorViewer, so nothing
    // reaches this yet; it is here because the answer for a mirror is known
    // and is not the desktop's.
    (void)path;
    (void)vp;
}

void MirrorViewer::setEditing(bool edit)
{
    pimpl->editing = edit;
}

bool MirrorViewer::isEditing() const
{
    return pimpl->editing;
}

void MirrorViewer::setEditingViewProvider(Gui::ViewProvider* vp, int ModNum)
{
    // Into the published graph before the base fills it, because filling it
    // is what the change-driven traversal has to notice. First child, which
    // is where the desktop's sits: the aux root is added to the selection
    // root at construction, ahead of every view provider.
    if (vp) {
        pimpl->attachEditingRoot(pcEditingRoot);
    }
    ViewerContext::setEditingViewProvider(vp, ModNum);
}

void MirrorViewer::resetEditingViewProvider()
{
    ViewerContext::resetEditingViewProvider();
    // After, not before: the base gives the view provider its children back
    // out of this root, and it has to still be somewhere the traversal can
    // see for that to be published.
    pimpl->detachEditingRoot(pcEditingRoot);
}

void MirrorViewer::addEventCallback(SoType eventtype, SoEventCallbackCB* cb, void* userdata)
{
    (void)eventtype;
    (void)cb;
    (void)userdata;
}

void MirrorViewer::removeEventCallback(SoType eventtype, SoEventCallbackCB* cb, void* userdata)
{
    (void)eventtype;
    (void)cb;
    (void)userdata;
}

void MirrorViewer::setRedirectToSceneGraph(bool redirect)
{
    (void)redirect;
}

void MirrorViewer::setSelectionEnabled(bool enable)
{
    pimpl->selectionEnabled = enable;
}

bool MirrorViewer::isSelectionEnabled() const
{
    return pimpl->selectionEnabled;
}

bool MirrorViewer::isSelecting() const
{
    // No rubber band without a widget to draw one on.
    return false;
}
