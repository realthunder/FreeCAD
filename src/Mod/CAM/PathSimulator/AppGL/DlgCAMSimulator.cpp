// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2024 Shai Seger <shaise at gmail>                       *
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


#include "DlgCAMSimulator.h"

#include <App/Application.h>
#include <Base/Parameter.h>
#include <Gui/Renderer/DrawSurface.h>

#include "Dummy3DViewer.h"
#include "GuiDisplay.h"
#include "MillSimulation.h"
#include "SimDrawContext.h"
#include "ViewCAMSimulator.h"
#include <Gui/View3DInventorViewer.h>
#include <Inventor/nodes/SoCamera.h>
#include <QSurfaceFormat>
#include <deque>
#include <limits>
#include <numeric>

// include this last as the defines can mess up other includes
#include "OpenGlWrapper.h"

using namespace std::literals;

namespace CAMSimulator
{

float SimShape::maxDimension() const
{
    float xmin = NAN, ymin = NAN, zmin = NAN;
    float xmax = NAN, ymax = NAN, zmax = NAN;

    for (const auto& v : verts) {
        xmin = std::fmin(xmin, v.x);
        ymin = std::fmin(ymin, v.x);
        zmin = std::fmin(zmin, v.x);

        xmax = std::fmax(xmax, v.x);
        ymax = std::fmax(ymax, v.x);
        zmax = std::fmax(zmax, v.x);
    }

    const float xsize = xmax - xmin;
    const float ysize = ymax - ymin;
    const float zsize = zmax - zmin;

    return std::max(std::max(xsize, ysize), zsize);
}


DlgCAMSimulator::DlgCAMSimulator(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    int samples = Gui::View3DInventorViewer::getNumSamples();
    if (samples > 1) {
        format.setSamples(samples);
    }
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    setFormat(format);

    setMouseTracking(true);

    mMillSimulator.reset(new MillSimulation);
}

DlgCAMSimulator::~DlgCAMSimulator()
{
    makeCurrent();
    mMillSimulator = nullptr;
}

void DlgCAMSimulator::connectTo(GuiDisplay& gui, Dummy3DViewer& dv)
{
    // connect to gui

    mGui = &gui;
    updateGui();

    connect(&gui, &GuiDisplay::play, this, [this](bool b) { mMillSimulator->SetPlaying(b); });

    connect(&gui, &GuiDisplay::singleStep, this, [this] { mMillSimulator->SingleStep(); });

    connect(&gui, &GuiDisplay::speedChanged, this, [this](int speed) {
        mMillSimulator->SetSpeed(speed);
    });

    connect(&gui, &GuiDisplay::stageChanged, this, [this](float f) {
        mMillSimulator->SetSimulationStage(f);
    });

    connect(&gui, &GuiDisplay::rotateEnableChanged, this, &DlgCAMSimulator::setRotateEnabled);

    connect(&gui, &GuiDisplay::pathVisibleChanged, this, [this](bool b) {
        mMillSimulator->SetPathVisible(b);
    });

    connect(&gui, &GuiDisplay::ssaoEnableChanged, this, [this](bool b) {
        mMillSimulator->EnableSsao(b);
    });

    connect(&gui, &GuiDisplay::stockVisibleChanged, this, &DlgCAMSimulator::setStockVisible);
    connect(&gui, &GuiDisplay::baseVisibleChanged, this, &DlgCAMSimulator::setBaseVisible);

    // connect to dummy viewer

    mDummyViewer = &dv;

    // The viewer's own providers draw in the same colours the
    // simulator draws its copies in, so the picture does not change
    // when a shape crosses from one to the other.
    const vec3& sc = mMillSimulator->stockColor;
    const vec3& bc = mMillSimulator->baseShapeColor;
    mDummyViewer->setStockColor(sc[0], sc[1], sc[2]);
    mDummyViewer->setBaseColor(bc[0], bc[1], bc[2]);

    syncViewerMirrors();

    // connect gui and dummy viewer

    connect(
        &gui,
        &GuiDisplay::viewAll,
        &dv,
        static_cast<void (Dummy3DViewer::*)()>(&Dummy3DViewer::viewAll)
    );
}

void DlgCAMSimulator::updateGui()
{
    if (!mGui) {
        return;
    }

    const auto state = mMillSimulator->GetState();

    mGui->setPlaying(state.mSimPlaying);
    mGui->setSpeed(state.mSimSpeed);

    const float stage = (float)state.mCurStep / state.mNTotalSteps;
    mGui->setStage(stage, state.mNTotalSteps);

    mGui->setStockVisible(state.mViewItems & VIEWITEM_SIMULATION);
    mGui->setBaseVisible(state.mViewItems & VIEWITEM_BASE_SHAPE);

    mGui->setPathVisible(state.mViewPath);
    mGui->setSsaoEnabled(state.mViewSSAO);

    if (mDummyViewer && !mDummyViewer->isAnimating()) {
        mGui->setRotateEnabled(false);
    }
}

void DlgCAMSimulator::cloneFrom(const DlgCAMSimulator& from)
{
    mNeedsInitialize = true;
    mNeedsClear = true;
    setAnimating(from.mAnimating);

    mQuality = from.mQuality;

    mGCode = from.mGCode;
    mTools = from.mTools;

    mStock = from.mStock;
    mStock.needsUpdate = true;

    mBase = from.mBase;
    mBase.needsUpdate = true;

    const auto state = from.mMillSimulator->GetState();
    mState = std::make_unique<MillSimulationState>(state);
}

DlgCAMSimulator* DlgCAMSimulator::instance(Gui::Document* doc)
{
    return &ViewCAMSimulator::instance(doc).dlg();
}

void DlgCAMSimulator::setAnimating(bool animating)
{
    if (animating == mAnimating) {
        return;
    }

    mAnimating = animating;

    if (animating && mAnimatingTimer == 0) {
        mAnimatingTimer = startTimer(0);
    }
    else if (!animating && mAnimatingTimer != 0) {
        killTimer(mAnimatingTimer);
        mAnimatingTimer = 0;
    }
}

void DlgCAMSimulator::startSimulation(const Part::TopoShape& stock, float quality)
{
    // Which renderer draws decides which resources get built, and
    // simulationStarted below reaches a paint before this returns. In
    // practice the buffers are built inside that frame, where the
    // driver has already set the flag -- but stating it here as well
    // means nothing depends on that ordering holding.
    gSimDraw.legacyGL = useLegacyGL();

    mQuality = quality;
    mNeedsInitialize = true;

    setStockShape(stock, 1);
    setAnimating(true);

    Q_EMIT simulationStarted();
}

void DlgCAMSimulator::resetSimulation()
{
    mNeedsClear = true;

    mGCode.clear();
    mTools.clear();
    mStock = {};
    mBase = {};
}

void DlgCAMSimulator::addGcodeCommand(const char* cmd)
{
    mGCode.push_back(cmd);
}

void DlgCAMSimulator::addTool(
    const std::vector<float>& toolProfilePoints,
    int toolNumber,
    float diameter,
    float resolution
)
{
    Q_UNUSED(resolution)

    std::string toolCmd = "T" + std::to_string(toolNumber);
    addGcodeCommand(toolCmd.c_str());
    mTools.emplace_back(toolProfilePoints, toolNumber, diameter);
}

static SimShape getMeshData(const Part::TopoShape& shape, float resolution)
{
    SimShape ret;

    std::vector<int> normalCount;
    int nVerts = 0;
    for (auto& shape : shape.getSubTopoShapes(TopAbs_FACE)) {
        std::vector<Base::Vector3d> points;
        std::vector<Data::ComplexGeoData::Facet> facets;
        shape.getFaces(points, facets, resolution);

        std::vector<Base::Vector3d> normals(points.size());
        std::vector<int> normalCount(points.size());

        // copy triangle indices and calculate normals
        for (auto face : facets) {
            ret.indices.push_back(face.I1 + nVerts);
            ret.indices.push_back(face.I2 + nVerts);
            ret.indices.push_back(face.I3 + nVerts);

            // calculate normal
            Base::Vector3d vAB = points[face.I2] - points[face.I1];
            Base::Vector3d vAC = points[face.I3] - points[face.I1];
            Base::Vector3d vNorm = vAB.Cross(vAC).Normalize();

            normals[face.I1] += vNorm;
            normals[face.I2] += vNorm;
            normals[face.I3] += vNorm;

            normalCount[face.I1]++;
            normalCount[face.I2]++;
            normalCount[face.I3]++;
        }

        // copy points and set normals
        for (unsigned int i = 0; i < points.size(); i++) {
            Base::Vector3d& point = points[i];
            Base::Vector3d& normal = normals[i];
            int count = normalCount[i];
            normal /= count;
            ret.verts.push_back(Vertex(point.x, point.y, point.z, normal.x, normal.y, normal.z));
        }

        nVerts = ret.verts.size();
    }

    ret.needsUpdate = true;
    return ret;
}

void DlgCAMSimulator::setStockShape(const Part::TopoShape& shape, float resolution)
{
    mStock = getMeshData(shape, resolution);
    mStockBox = shape.getBoundBox();

    if (mDummyViewer && mirrorsStockToViewer()) {
        mDummyViewer->setStockShape(shape);
    }

    requestRedraw();
}

void DlgCAMSimulator::setStockVisible(bool b)
{
    if (b == mMillSimulator->IsStockVisible()) {
        return;
    }

    mMillSimulator->SetStockVisible(b);

    if (mDummyViewer && mirrorsStockToViewer()) {
        mDummyViewer->setStockVisible(b);
    }

    requestRedraw();
}

void DlgCAMSimulator::setBaseShape(const Part::TopoShape& shape, float resolution)
{
    mBase = getMeshData(shape, resolution);
    mBaseBox = shape.getBoundBox();

    if (mDummyViewer && mirrorsBaseToViewer()) {
        mDummyViewer->setBaseShape(shape);
    }

    requestRedraw();
}

void DlgCAMSimulator::setBaseVisible(bool b)
{
    if (b == mMillSimulator->IsBaseVisible()) {
        return;
    }

    mMillSimulator->SetBaseVisible(b);

    if (mDummyViewer && mirrorsBaseToViewer()) {
        mDummyViewer->setBaseVisible(b);
    }

    requestRedraw();
}

// this is very similar to DemoMode::getDirection in Gui/DemoMode.cpp

static SbVec3f getRotationDirection(Gui::View3DInventorViewer* viewer)
{
    const SbVec3f viewAxis = {0, 0, -1};

    SoCamera* cam = viewer->getSoRenderManager()->getCamera();
    if (!cam) {
        return viewAxis;
    }
    SbRotation rot = cam->orientation.getValue();
    SbRotation inv = rot.inverse();
    SbVec3f vec(viewAxis);
    inv.multVec(vec, vec);
    if (vec.length() < std::numeric_limits<float>::epsilon()) {
        vec = viewAxis;
    }
    vec.normalize();
    return vec;
}

void DlgCAMSimulator::setRotateEnabled(bool b)
{
    if (b) {
        mDummyViewer->startSpinningAnimation(getRotationDirection(mDummyViewer), 0.5f);
    }
    else {
        mDummyViewer->stopAnimating();
    }
}

void DlgCAMSimulator::setBackgroundColor(const QColor& c)
{
    mMillSimulator->SetBackgroundColor({c.redF(), c.greenF(), c.blueF()});
    requestRedraw();
}

void DlgCAMSimulator::setPathColor(const QColor& normal, const QColor& rapid)
{
    const vec3 vnormal = {normal.redF(), normal.greenF(), normal.blueF()};
    const vec3 vrapid = {rapid.redF(), rapid.greenF(), rapid.blueF()};
    mMillSimulator->SetPathColor(vnormal, vrapid);
}

void DlgCAMSimulator::timerEvent(QTimerEvent* event)
{
    (void)event;

    requestRedraw();

    // TODO: keep things simple for now, should probably only update gui if something changed

    updateGui();
}

void DlgCAMSimulator::updateResources()
{
    // clear simulator

    if (mNeedsClear) {
        mMillSimulator->Clear();
        mLastGCode = 0;
        mNeedsClear = false;
    }

    // update gcode

    for (int i = mLastGCode; i < (int)mGCode.size(); i++) {
        const std::string& cmd = mGCode[i];
        mMillSimulator->AddGcodeLine(cmd.c_str());
    }

    mLastGCode = mGCode.size();

    // update tools

    for (const auto& tool : mTools) {
        if (!mMillSimulator->ToolExists(tool.id)) {
            mMillSimulator->AddTool(tool.profile, tool.id, tool.diameter);
        }
    }

    // initialize simulator

    if (mNeedsInitialize) {
        // TODO: mStock is set when we arrive here, still this could be handled nicer
        const float maxStockDimension = mStock.maxDimension();

        mMillSimulator->InitSimulation(mQuality, maxStockDimension);
        mNeedsInitialize = false;

        mLastProcessSim = clock::time_point::min();
    }

    // update stock and base

    if (mStock.needsUpdate) {
        mMillSimulator->SetArbitraryStock(mStock.verts, mStock.indices);
        mStock.needsUpdate = false;
    }

    if (mBase.needsUpdate) {
        mMillSimulator->SetBaseObject(mBase.verts, mBase.indices);
        mBase.needsUpdate = false;
    }

    // update state

    if (mState) {
        mMillSimulator->SetState(*mState);
        mState = nullptr;
    }
}

void DlgCAMSimulator::updateWindowScale(int w, int h)
{
    mMillSimulator->UpdateWindowScale(w, h);
}

void DlgCAMSimulator::updateCamera()
{
    if (!mDummyViewer) {
        return;
    }

    const SoCamera& camera = *mDummyViewer->getCamera();
    mMillSimulator->UpdateCamera(camera);
}

void DlgCAMSimulator::initializeGL()
{
    gOpenGLFunctions.initializeOpenGLFunctions();
}

bool DlgCAMSimulator::forceLegacyGLPref()
{
    static ParameterGrp::handle hGrp = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/CAM"
    );
    return hGrp->GetBool("ForceLegacyGLRender", false);
}

bool DlgCAMSimulator::useLegacyGL()
{
    // The legacy raw-GL renderer draws whenever there is no backend to
    // draw through: BUILD_BGFX off (both default OFF, so this is the
    // ORDINARY build), a backend that would not start, or a session
    // where nothing has brought one up yet. Without it the simulator
    // is simply blank in those builds.
    //
    // The preference forces it on where a backend does exist, which is
    // what makes the two comparable on one machine and gives a user
    // whose driver the backend dislikes somewhere to stand.
    if (forceLegacyGLPref()) {
        return true;
    }
    return Render::DrawDevice::instance() == nullptr;
}

bool DlgCAMSimulator::beginFacadeFrame()
{
    if (useLegacyGL() || !Render::DrawDevice::instance()) {
        // Device gone (or never up): surface handles died with it.
        mDrawSurface.reset();
        gSimDraw.surface = nullptr;
        return false;
    }
    if (!mDrawSurface) {
        mDrawSurface = Render::DrawSurface::create(this, SimPassCount);
    }
    if (!mDrawSurface) {
        return false;
    }
    const qreal ratio = devicePixelRatioF();
    const int w = int(width() * ratio);
    const int h = int(height() * ratio);
    return mDrawSurface->beginFrame(w, h);
}

void DlgCAMSimulator::endFacadeFrame()
{
    if (!mDrawSurface) {
        return;
    }
    if (gSimDraw.submitted) {
        mDrawSurface->endFrame();
    }
}

void DlgCAMSimulator::syncViewerMirrors()
{
    if (!mDummyViewer) {
        return;
    }
    mDummyViewer->setStockVisible(mirrorsStockToViewer()
                                  && mMillSimulator->IsStockVisible());
    mDummyViewer->setBaseVisible(mirrorsBaseToViewer()
                                 && mMillSimulator->IsBaseVisible());
    mMillSimulator->SetBaseDrawnByHost(isAttached());
}

Base::BoundBox3d DlgCAMSimulator::simulationBoundBox() const
{
    // Both shapes unconditionally: when the mirrors ARE on, the
    // viewer's own scene bounds already cover them and the union is a
    // no-op, so the caller needs no test of which path is running.
    Base::BoundBox3d box = mStockBox;
    if (mBaseBox.IsValid()) {
        box.Add(mBaseBox);
    }
    return box;
}

bool DlgCAMSimulator::mirrorsStockToViewer() const
{
    // The viewer carries its own stock view provider, fed in step with
    // the simulator's copy. It was dead weight while the viewer never
    // painted; now that it does, it would draw the stock UNCUT over
    // the carved one -- the same object rendered twice, and the wrong
    // one on top. Nothing but the simulator can draw the carved stock:
    // the material removal IS its rendering, and there is no mesh of
    // the result to hand over. So while attached this mirror stays
    // off.
    return !isAttached();
}

bool DlgCAMSimulator::mirrorsBaseToViewer() const
{
    // The base shape is the opposite case. It is ordinary document
    // geometry -- the simulator only ever drew it flat, biased a
    // fraction closer to stand in for a polygon offset -- so while
    // attached the engine draws it instead, with the document's
    // lighting, and the carved stock sorts against it through the
    // depth the composite now writes (docs/CAMSimRenderPort.md
    // sec 8.4). MillSimulation::SetBaseDrawnByHost is the other half:
    // without it both would draw it.
    //
    // Fed even while standalone, where the viewer never paints: that
    // is what lets a later attach just work. syncViewerMirrors
    // restates visibility, not geometry, so a provider that was never
    // given the shape would stay empty.
    return true;
}

void DlgCAMSimulator::requestRedraw()
{
    if (isAttached() && mDummyViewer) {
        // The render manager's own request, not QWidget::update(): a
        // Quarter viewer redraws when its manager is asked to, and
        // that is the call every other producer in the Gui uses.
        if (auto* mgr = mDummyViewer->getSoRenderManager()) {
            mgr->scheduleRedraw();
        }
        return;
    }
    update();
}

unsigned DlgCAMSimulator::framePasses() const
{
    return SimPassCount;
}

void DlgCAMSimulator::attachToHost(Gui::View3DInventorViewer* viewer)
{
    // Legacy GL owns the widget's own context and cannot draw inside
    // somebody else's frame, so a forced-legacy session never attaches.
    // The device half of useLegacyGL() needs no test here: with no
    // device there is no renderer to borrow either.
    Render::Renderer* host =
        (viewer && !forceLegacyGLPref()) ? viewer->getExternalRenderer() : nullptr;
    if (host == mHostRenderer) {
        return;
    }
    if (mHostRenderer) {
        mHostRenderer->setFrameConsumer(nullptr);
        mHostRenderer = nullptr;
    }
    if (!host) {
        // No frame to borrow: back to drawing this widget, which is
        // what the standalone surface below does.
        syncViewerMirrors();
        return;
    }
    host->setFrameConsumer(this);
    mHostRenderer = host;
    // Attachment changes who draws the stock and the base, so the
    // viewer's mirror providers have to be told again.
    syncViewerMirrors();
    // The standalone surface's handles are the device's, not the
    // host's, and nothing will drive them again.
    mDrawSurface.reset();
    gSimDraw.surface = nullptr;
}

void DlgCAMSimulator::drawFrame(Render::DrawSurface& surface)
{
    // The facade owns this frame; no GL call may reach the context.
    gSimDraw.legacyGL = false;
    updateResources();

    // We need to call updateWindowScale on every render since the devicePixelRatio we get in
    // resizeGL might be wrong on the first resize.

    int w = 0;
    int h = 0;
    surface.hostSize(w, h);
    updateWindowScale(w, h);
    updateCamera();

    const auto now = clock::now();
    const auto elapsed = mLastProcessSim != clock::time_point::min() ? now - mLastProcessSim : 0s;

    mMillSimulator->simDisplay.ConfigureFacadeFrame(&surface,
                                                    mMillSimulator->bgndColor);
    gSimDraw.submitted = false;
    gSimDraw.surface = &surface;
    mMillSimulator->ProcessSim(elapsed);
    gSimDraw.surface = nullptr;

    mLastProcessSim = now;
}

void DlgCAMSimulator::paintGL()
{
    // Attached, the host's frame drives drawFrame() and this widget is
    // hidden; a stray paint must not open a second frame.
    if (isAttached()) {
        return;
    }
    if (useLegacyGL()) {
        drawFrameLegacyGL();
        return;
    }
    if (!beginFacadeFrame()) {
        return;
    }
    drawFrame(*mDrawSurface);
    endFacadeFrame();
}

void DlgCAMSimulator::drawFrameLegacyGL()
{
    // The pre-port renderer, drawing straight into this widget's own
    // GL context. Same simulation, same frame shape as drawFrame():
    // resources, size, camera, then one ProcessSim. What differs is
    // where the draws go -- gSimDraw.legacyGL sends them to GL and
    // leaves the facade dormant, and there is no frame boundary to
    // open because the widget's context is already current.
    //
    // Set before updateResources(), not just around the draws: the
    // buffers and shaders are built lazily from there, and each path
    // builds only its own.
    gSimDraw.legacyGL = true;
    updateResources();

    const qreal ratio = devicePixelRatioF();
    updateWindowScale(int(width() * ratio), int(height() * ratio));
    updateCamera();

    const auto now = clock::now();
    const auto elapsed = mLastProcessSim != clock::time_point::min()
        ? now - mLastProcessSim
        : 0s;

    gSimDraw.surface = nullptr;
    gSimDraw.submitted = false;
    mMillSimulator->ProcessSim(elapsed);

    mLastProcessSim = now;
}

void DlgCAMSimulator::resizeGL(int w, int h)
{
    (void)w, (void)h;
}

}  // namespace CAMSimulator
