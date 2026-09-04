/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
# include <cstring>
# include <QEvent>
# include <QFile>
# include <QImage>
# include <QOpenGLContext>
# include <QOpenGLFramebufferObject>
# include <QOpenGLFunctions>
# include <QOpenGLWidget>
# include <Inventor/SbViewportRegion.h>
# include <Inventor/nodes/SoComplexity.h>
# include <Inventor/nodes/SoFragmentShader.h>
# include <Inventor/nodes/SoMaterial.h>
# include <Inventor/nodes/SoPerspectiveCamera.h>
# include <Inventor/nodes/SoSeparator.h>
# include <Inventor/nodes/SoShaderParameter.h>
# include <Inventor/nodes/SoShaderProgram.h>
# include <Inventor/nodes/SoSphere.h>
# include <Inventor/nodes/SoSubNode.h>
# include <Inventor/fields/SoSFBool.h>
#endif

#include <App/Document.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>

#include "ShaderGraphHost.h"
#include "Application.h"
#include "Command.h"
#include "Document.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "Inventor/SoFCRenderCache.h"
#include "Inventor/SoFCVertexCache.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Inventor/SoFCRendererBridge.h"
#include "Renderer/GraphEditor/GraphEditorWidget.h"
#include "Renderer/CyclesRenderer.h"
#include "Renderer/MaterialXSupport.h"
#include "Renderer/Renderer.h"

FC_LOG_LEVEL_INIT("Gui", true, true)

using namespace Gui;

namespace {
/// The sphere the preview shows, in model units. A MaterialX
/// document's maps are stated per unit, so the ball is the material
/// icon's two-unit one and the camera orbits at the host's distance.
constexpr float kSphereRadius = 1.0f;
/// How often the backend is asked whether a stand-in preview's shader
/// has finished compiling.
constexpr int kCompilePollMs = 250;
/// How long invalidations coalesce before a render: a drag reports
/// every motion, and a render costs a backend frame.
constexpr int kRenderDelayMs = 40;
/// How long the path tracer's staged frames coalesce before one is
/// taken and uploaded: the engine stages many per second early in a
/// render, and the pane needs none of them faster than this.
constexpr int kFrameDelayMs = 30;

/// Whether two scene inputs agree on everything but the draws and the
/// camera (the feed's gate in View3DInventorViewer, spelled once).
bool sameConfig(const Render::Cycles::SceneInput &a, const Render::Cycles::SceneInput &b)
{
    return a.pbr == b.pbr && a.bump == b.bump && a.output == b.output && a.light == b.light
        && a.section == b.section && a.debugView == b.debugView
        && a.background.type == b.background.type
        && a.background.fromColor == b.background.fromColor
        && a.background.toColor == b.background.toColor
        && a.background.midColor == b.background.midColor
        && a.background.hasMid == b.background.hasMid;
}

/// The preview's ball. Coin's sphere generator states a UV per vertex,
/// but the vertex cache keeps them only while a texture unit is enabled
/// on the capture traversal, which nothing here does -- so a plain
/// SoSphere reached the backend without UVs, every fragment of a
/// document's image node sampled texel (0, 0), and a mapped material
/// showed as one flat colour (the chess set's bishop, 2026-09-04). The
/// forceTexCoords field is what SoFCVertexCache reads to keep unit-0
/// UVs regardless, as SoTextImage and SoDatumLabel carry it.
class PreviewSphere : public SoSphere {
    SO_NODE_HEADER(PreviewSphere);
public:
    static void initClass()
    {
        static bool done = false;
        if (!done) {
            SO_NODE_INIT_CLASS(PreviewSphere, SoSphere, "Sphere");
            done = true;
        }
    }
    PreviewSphere()
    {
        SO_NODE_CONSTRUCTOR(PreviewSphere);
        SO_NODE_ADD_FIELD(forceTexCoords, (TRUE));
    }
    SoSFBool forceTexCoords;
};
SO_NODE_SOURCE(PreviewSphere)

/// A document's public inputs as the vec4-padded `u_<name>` parameters
/// the generated shader's uniforms are fed by (the packing of
/// RenderDebugConfig::UserParam).
std::vector<Render::RenderDebugConfig::UserParam> paramsOf(const std::string &xml,
                                                           const char *surface)
{
    std::vector<Render::RenderDebugConfig::UserParam> res;
    for (const auto &input : Render::MaterialX::publicInputs(xml, surface ? surface : "")) {
        Render::RenderDebugConfig::UserParam param;
        param.name = "u_" + input.name;
        param.values = input.value;
        param.values.resize((param.values.size() + 3) & ~size_t(3), 0.0f);
        res.push_back(std::move(param));
    }
    return res;
}
}

ShaderGraphHost::ShaderGraphHost(App::ShaderProgram *prog, Render::GraphEditorWidget *widget)
    : program(prog)
    , editor(widget)
{
    render.setSingleShot(true);
    render.setInterval(kRenderDelayMs);
    connect(&render, &QTimer::timeout, this, [this]() { renderPreview(); });
    frame.setSingleShot(true);
    frame.setInterval(kFrameDelayMs);
    connect(&frame, &QTimer::timeout, this, [this]() { takeTracedFrame(); });
    poll.setInterval(kCompilePollMs);
    connect(&poll, &QTimer::timeout, this, [this]() {
        Render::Renderer *renderer = nullptr;
        View3DInventorViewer *viewer = nullptr;
        if (!findBackend(renderer, viewer)) {
            poll.stop();
            setCompiling(false);
            return;
        }
        // A compile landed (the generation moved) or nothing is in
        // flight any more: the stand-in frame is stale either way. The
        // render decides whether to keep polling.
        if (renderer->shaderCompileGeneration() != compileGeneration
                || !renderer->shaderCompilePending())
            previewInvalidated();
    });

    // Show and Hide reach the widget itself; ShowToParent and
    // HideToParent reach it when an ancestor -- the view in its cell --
    // is the one that moved.
    editor->installEventFilter(this);
    hidden = !editor->isVisible();

    // The preview reads its whole configuration off a 3D view of the
    // document, so it has to know when one comes or goes.
    if (Gui::Document *gdoc = Application::Instance->getDocument(program->getDocument())) {
        attachConnection = gdoc->signalAttachView.connect(
                [this](const Gui::BaseView &, bool) { viewsChanged(); });
        detachConnection = gdoc->signalDetachView.connect(
                [this](const Gui::BaseView &, bool) { viewsChanged(); });
    }
}

ShaderGraphHost::~ShaderGraphHost()
{
    attachConnection.disconnect();
    detachConnection.disconnect();
    // The tracer first: its destructor nulls the callback under its
    // lock and joins the session's thread while this object is still
    // whole, and a call it queued on this object dies with it.
    stopTracer();
}

void ShaderGraphHost::viewsChanged()
{
    // Deferred: signalDetachView is reported BEFORE the view leaves the
    // document's list, so findViewer would still answer the one that is
    // going. From the event loop the list is settled, and a queued call
    // on this object dies with it if the whole document is closing.
    QMetaObject::invokeMethod(this, [this]() {
        if (findViewer()) {
            // A view came back: whatever the tracer refused before is
            // worth one more attempt.
            tracerBlocked = false;
        }
        else if (tracer) {
            // Nothing left to configure a render from: park the
            // session rather than let it hold a device for a pane that
            // cannot be refreshed. The mode stays traced, so the next
            // view to attach starts one again.
            FC_LOG("shader graph preview: no 3D view left, tracer parked");
            stopTracer();
        }
        previewInvalidated();
    }, Qt::QueuedConnection);
}

bool ShaderGraphHost::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == editor) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ShowToParent:
        case QEvent::HideToParent:
            updateHidden();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void ShaderGraphHost::updateHidden()
{
    const bool now = !editor->isVisible();
    if (now == hidden)
        return;
    hidden = now;
    FC_LOG("shader graph preview: pane " << (hidden ? "hidden" : "shown"));
    if (tracer)
        tracer->setPaused(hidden);
    if (hidden) {
        // Nothing is drawn behind a hidden pane; a render in flight is
        // one the editor will never paint.
        render.stop();
        frame.stop();
    }
    else if (staleWhileHidden) {
        staleWhileHidden = false;
        previewInvalidated();
    }
}

void ShaderGraphHost::programChanged()
{
    invalidateThumbnails();
    baseParams = paramsOf(inspectedText.empty() ? documentText() : inspectedText,
                          program->Surface.getValue());
    previewInvalidated();
}

void ShaderGraphHost::setBaseText(const std::string &xml)
{
    baseParams = paramsOf(xml, program->Surface.getValue());
    previewInvalidated();
}

std::string ShaderGraphHost::resolveImage(const std::string &name)
{
    if (name.empty())
        return {};
    if (program->Images.find(name.c_str())) {
        std::string path = program->Images.filePath(name.c_str());
        if (!path.empty())
            return path;
    }
    return EngineGraphHost::resolveImage(name);
}

std::vector<std::string> ShaderGraphHost::imageNames()
{
    std::vector<std::string> names;
    for (const auto &file : program->Images.getValues())
        names.push_back(file.name);
    return names;
}

bool ShaderGraphHost::loadImage(const std::string &path, int &width, int &height,
                                std::vector<uint8_t> &rgba)
{
    if (EngineGraphHost::loadImage(path, width, height, rgba))
        return true;
    // What the engine's decoder does not read (TIFF, BMP, an EXR with
    // a plugin), Qt may.
    QImage image(QString::fromUtf8(path.c_str()));
    if (image.isNull())
        return false;
    if (image.width() > kThumbnailSide || image.height() > kThumbnailSide)
        image = image.scaled(kThumbnailSide, kThumbnailSide, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_RGBA8888);
    width = image.width();
    height = image.height();
    rgba.resize(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(rgba.data() + size_t(y) * width * 4, image.constScanLine(y),
                    size_t(width) * 4);
    return true;
}

const std::vector<std::string> &ShaderGraphHost::surfaceNames()
{
    const std::string &xml = documentText();
    if (xml != namesText) {
        namesText = xml;
        names.clear();
        // An inspection per distinct text, as the viewport pays one per
        // distinct text anyway; the menu reads the cached answer.
        if (!xml.empty())
            names = Render::MaterialX::inspect(xml, {}, program->Surface.getValue()).materials;
    }
    return names;
}

std::string ShaderGraphHost::currentSurface()
{
    const char *surface = program->Surface.getValue();
    return surface ? surface : "";
}

void ShaderGraphHost::selectSurface(const std::string &name)
{
    QMetaObject::invokeMethod(this, [this, name]() {
        if (!program->getNameInDocument() || name == currentSurface())
            return;
        Gui::Document *doc = Application::Instance->getDocument(program->getDocument());
        if (!doc)
            return;
        std::string literal;
        for (char c : name) {
            if (c == '\\' || c == '"')
                literal += '\\';
            literal += c;
        }
        // A Python property assignment in one transaction, as the view
        // writes the text: undoable, on the macro record. The view's
        // watch reloads the editor and the preview follows.
        doc->openCommand(QT_TRANSLATE_NOOP("Command", "Select shader surface"));
        try {
            Gui::Command::doCommand(Gui::Command::Doc,
                    "App.getDocument(\"%s\").getObject(\"%s\").Surface = \"%s\"",
                    program->getDocument()->getName(), program->getNameInDocument(),
                    literal.c_str());
            doc->commitCommand();
        }
        catch (Base::Exception &e) {
            doc->abortCommand();
            FC_ERR("shader graph surface write failed: " << e.what());
        }
    }, Qt::QueuedConnection);
}

void ShaderGraphHost::previewInvalidated()
{
    // A pane nobody can see is not worth a backend frame or a scene
    // restate; the invalidation is remembered and answered when the
    // pane comes back.
    if (hidden) {
        staleWhileHidden = true;
        return;
    }
    // Coalesced: a drag reports every motion, a load reports once per
    // element; one render answers everything that arrived meanwhile.
    if (!render.isActive())
        render.start();
}

View3DInventorViewer *ShaderGraphHost::findViewer() const
{
    Gui::Document *gdoc = Application::Instance->getDocument(program->getDocument());
    if (!gdoc)
        return nullptr;
    View3DInventorViewer *first = nullptr;
    for (auto *view : gdoc->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
        auto *viewer = static_cast<View3DInventor*>(view)->getViewer();
        if (viewer->getExternalRenderer())
            return viewer;
        if (!first)
            first = viewer;
    }
    return first;
}

bool ShaderGraphHost::findBackend(Render::Renderer *&renderer,
                                  View3DInventorViewer *&viewer) const
{
    View3DInventorViewer *v = findViewer();
    if (!v || !v->getExternalRenderer())
        return false;
    renderer = v->getExternalRenderer();
    viewer = v;
    return true;
}

const std::vector<std::string> &ShaderGraphHost::previewModes()
{
    static const std::vector<std::string> both = {"Raster", "Path traced"};
    static const std::vector<std::string> none;
    return Render::Cycles::available() ? both : none;
}

int ShaderGraphHost::previewMode()
{
    return mode;
}

void ShaderGraphHost::setPreviewMode(int m)
{
    if (m == mode || m < 0 || m >= int(previewModes().size()))
        return;
    // From the event loop: the pick is made inside the editor's frame,
    // and a device's session is not something to set up there.
    QMetaObject::invokeMethod(this, [this, m]() { applyPreviewMode(m); },
                              Qt::QueuedConnection);
}

const std::vector<std::string> &ShaderGraphHost::previewDevices()
{
    static const std::vector<std::string> none;
    // Only the traced mode computes on a device of its own; the raster
    // one draws through the view's backend, wherever that is.
    if (mode != 1)
        return none;
    if (deviceTypes.empty()) {
        // The TYPES this machine can compute on: ViewportOptions.device
        // names a type, so two cards of one type are one entry -- the
        // rule the view's Cycles_Device enum is built by.
        for (const auto &info : Render::Cycles::devices()) {
            bool seen = false;
            for (const auto &type : deviceTypes)
                seen = seen || type == info.type;
            if (!seen)
                deviceTypes.push_back(info.type);
        }
    }
    return deviceTypes;
}

int ShaderGraphHost::previewDevice()
{
    // What a session started now would run on: this editor's pick, else
    // the view's effective device.
    std::string current = device;
    if (current.empty()) {
        if (View3DInventorViewer *viewer = findViewer())
            current = viewer->cyclesViewportOptions().device;
    }
    const std::vector<std::string> &types = previewDevices();
    for (size_t i = 0; i < types.size(); ++i) {
        if (types[i] == current)
            return int(i);
    }
    return 0;
}

void ShaderGraphHost::setPreviewDevice(int index)
{
    // From the event loop, as a mode pick is: the session is torn down
    // and set up again, which is not something to do inside the
    // editor's frame.
    QMetaObject::invokeMethod(this, [this, index]() {
        const std::vector<std::string> &types = previewDevices();
        if (index < 0 || index >= int(types.size()) || index == previewDevice())
            return;
        device = types[index];
        FC_LOG("shader graph preview: device " << device);
        tracerBlocked = false;
        if (mode == 1 && startTracer()) {
            previewInvalidated();
            editor->requestFrame();
        }
    }, Qt::QueuedConnection);
}

void ShaderGraphHost::applyPreviewMode(int m)
{
    if (m == mode)
        return;
    if (m == 1) {
        tracerBlocked = false;
        mode = 1;
        // The raster frame stays on the pane until the first traced
        // one lands. A view that is not there yet leaves the mode
        // traced and the session for the next attach to start.
        setCompiling(false);
        poll.stop();
        startTracer();
    }
    else {
        stopTracer();
        mode = 0;
    }
    previewInvalidated();
    editor->requestFrame();
}

bool ShaderGraphHost::startTracer()
{
    View3DInventorViewer *viewer = findViewer();
    if (!viewer) {
        FC_WARN("shader graph preview: no 3D view to path trace for");
        return false;
    }
    Render::Cycles::ViewportOptions options = viewer->cyclesViewportOptions();
    if (!device.empty())
        options.device = device;
    std::string error;
    auto vp = Render::Cycles::Viewport::create(options, &error);
    if (!vp) {
        FC_WARN("shader graph preview: path tracer unavailable: " << error);
        // Every invalidation would otherwise ask the engine again.
        tracerBlocked = true;
        return false;
    }
    // Cycles' threads report a staged frame from wherever they
    // run; the take is queued to this object's thread, where the
    // session is fed too (takeFrame is serialized by its caller
    // against setScene and setCamera).
    vp->setRedrawCallback([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            if (!frame.isActive())
                frame.start();
        }, Qt::QueuedConnection);
    });
    stopTracer();
    tracer = std::move(vp);
    tracer->setPaused(hidden);
    setPreviewStatus("Path tracing");
    FC_LOG("shader graph preview: tracer on " << options.device);
    return true;
}

void ShaderGraphHost::stopTracer()
{
    frame.stop();
    tracedFrames = 0;
    tracer.reset();
    tracedSignature.clear();
    tracedInput.reset();
    setPreviewStatus({});
}

std::string ShaderGraphHost::documentForRender() const
{
    std::string xml = documentText();
    if (!baseParams.empty())
        xml = Render::MaterialX::applyInputsToDocument(xml, baseParams,
                                                       program->Surface.getValue());
    std::vector<Render::MaterialX::ImageReference> files;
    for (const auto &file : program->Images.getValues()) {
        std::string path = program->Images.filePath(file.name.c_str());
        if (!path.empty())
            files.push_back({file.name, std::move(path)});
    }
    if (files.empty())
        return xml;
    return Render::MaterialX::substituteImages(xml, files);
}

void ShaderGraphHost::buildScene()
{
    if (root)
        return;
    root = new SoSeparator;
    previewCamera = new SoPerspectiveCamera;
    previewCamera->viewportMapping = SoCamera::LEAVE_ALONE;
    root->addChild(previewCamera);
    auto complexity = new SoComplexity;
    complexity->value = 1.0f;
    root->addChild(complexity);
    // The program node the render cache turns into the draw's user
    // shader (SoFCRenderCacheManager::postShaderProgram): the same
    // triple a ShaderBinding builds, over the editor's text.
    auto shader = new SoShaderProgram;
    shader->stage = SbName("material");
    fragment = new SoFragmentShader;
    fragment->sourceType = SoShaderObject::MATERIALX;
    shader->shaderObject.setNum(1);
    shader->shaderObject.set1Value(0, fragment);
    root->addChild(shader);
    auto material = new SoMaterial;
    material->diffuseColor.setValue(0.8f, 0.8f, 0.8f);
    root->addChild(material);
    PreviewSphere::initClass();
    auto sphere = new PreviewSphere;
    sphere->radius = kSphereRadius;
    root->addChild(sphere);
    // After the nodes, the sphere's class registration included: the
    // manager's traversal action is built at construction, and one
    // built before PreviewSphere::initClass captured no geometry on its
    // first pass (a persistent manager showed it; the per-render one
    // it replaced was always constructed after the scene).
    manager = std::make_unique<SoFCRenderCacheManager>();
}

void ShaderGraphHost::renderPreview()
{
    const int w = previewWidth();
    const int h = previewHeight();
    FC_LOG("shader graph preview: render " << w << "x" << h << " camera yaw "
           << camera().yaw << " pitch " << camera().pitch << " dist " << camera().distance
           << " mode " << mode);
    if (w <= 0 || h <= 0)
        return;
    const std::string &xml = documentText();
    View3DInventorViewer *viewer = findViewer();
    Render::Renderer *renderer = viewer ? viewer->getExternalRenderer() : nullptr;
    // The mode says traced and there is no session: the last view left
    // and one is back, or the pick was made before there was a view.
    if (mode == 1 && viewer && !tracer && !tracerBlocked)
        startTracer();
    const bool traced = mode == 1 && tracer;
    if (xml.empty() || !viewer || (!traced && !renderer)) {
        if (!viewer)
            stopTracer();
        clearPreview();
        setCompiling(false);
        poll.stop();
        editor->requestFrame();
        return;
    }
    updateScene(w, h);
    if (traced)
        renderTraced(w, h, viewer);
    else
        renderRaster(w, h, renderer, viewer);
}

void ShaderGraphHost::updateScene(int w, int h)
{
    buildScene();
    const std::string &xml = documentText();
    const char *surface = program->Surface.getValue();
    // The live values as parameters; the shader node's text is the live
    // document with its public inputs set back to the baseline's
    // values, so a value drag leaves the generated shader -- and its
    // compile -- alone and only the uniforms move. A topology edit
    // changes the text either way, and regenerates.
    if (xml != inspectedText) {
        inspectedText = xml;
        liveParams = paramsOf(xml, surface);
    }
    const std::string text = documentForRender();
    if (fragment->sourceProgram.getValue() != text.c_str())
        fragment->sourceProgram = text.c_str();
    if (fragment->sourceSurface.getValue() != surface)
        fragment->sourceSurface = surface;
    std::map<std::string, CoinPtr<SoShaderParameterArray1f>> next;
    for (const auto &p : liveParams) {
        auto &node = next[p.name];
        auto it = paramNodes.find(p.name);
        if (it != paramNodes.end())
            node = it->second;
        else {
            node = new SoShaderParameterArray1f;
            node->name = p.name.c_str();
        }
        const int num = int(p.values.size());
        if (node->value.getNum() != num
                || std::memcmp(node->value.getValues(0), p.values.data(),
                               size_t(num) * sizeof(float)) != 0)
            node->value.setValues(0, num, p.values.data());
    }
    paramNodes = std::move(next);
    if (fragment->parameter.getNum() != int(paramNodes.size())) {
        fragment->parameter.setNum(int(paramNodes.size()));
    }
    int index = 0;
    for (auto &entry : paramNodes) {
        if (fragment->parameter[index] != entry.second)
            fragment->parameter.set1Value(index, entry.second);
        ++index;
    }

    // The camera the host's orbit states, in Coin's terms.
    float eye[3], forward[3], up[3];
    cameraFrame(eye, forward, up);
    const SbVec3f fwd(forward[0], forward[1], forward[2]);
    const SbVec3f upv(up[0], up[1], up[2]);
    SbVec3f right = fwd.cross(upv);
    right.normalize();
    const SbVec3f back = -fwd;
    // Rows are the images of the camera axes under Coin's row-vector
    // convention: right, up, back -- the camera looks along -back.
    SbMatrix orient(right[0], right[1], right[2], 0.0f,
                    upv[0], upv[1], upv[2], 0.0f,
                    back[0], back[1], back[2], 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);
    previewCamera->position.setValue(eye[0], eye[1], eye[2]);
    previewCamera->orientation = SbRotation(orient);
    const float distance = previewCamera->position.getValue().length();
    previewCamera->nearDistance = std::max(0.05f, distance - kSphereRadius * 2.0f);
    previewCamera->farDistance = distance + kSphereRadius * 2.0f;
    previewCamera->focalDistance = distance;
    previewCamera->aspectRatio = float(w) / float(h);
}

bool ShaderGraphHost::translateScene(int w, int h, Render::DrawCallList &draws)
{
    // Coin scene -> render caches -> the backend-neutral draw list, as
    // the shaded underlay's derived capture does (ShadedUnderlay.cpp,
    // captureSceneViaBackend).
    manager->traverse(root, SbViewportRegion(short(w), short(h)));
    SoFCRenderCache *cache = manager->getSceneCache();
    if (!cache)
        return false;
    draws = RendererBridge::translate(cache->getVertexCaches(true),
                                      RendererBridge::SectionOnTop());
    FC_LOG("shader graph preview: " << draws.size() << " draws, params " << liveParams.size());
    return !draws.empty();
}

void ShaderGraphHost::cameraMatrices(int w, int h, SbMatrix &view, SbMatrix &proj) const
{
    previewCamera->getViewVolume(float(w) / float(h)).getMatrices(view, proj);
}

void ShaderGraphHost::renderRaster(int w, int h, Render::Renderer *renderer,
                                   View3DInventorViewer *viewer)
{
    Render::DrawCallList draws;
    if (!translateScene(w, h, draws) || !renderer->setCaptureScene(std::move(draws))) {
        clearPreview();
        editor->requestFrame();
        return;
    }

    SbMatrix viewMat, projMat;
    cameraMatrices(w, h, viewMat, projMat);
    auto *glWidget = qobject_cast<QOpenGLWidget*>(viewer->viewport());
    if (!glWidget) {
        renderer->clearCaptureScene();
        clearPreview();
        editor->requestFrame();
        return;
    }
    QOpenGLContext *previous = QOpenGLContext::currentContext();
    QSurface *previousSurface = previous ? previous->surface() : nullptr;
    glWidget->makeCurrent();
    bool ok = false;
    std::vector<uint8_t> pixels;
    {
        QOpenGLFramebufferObject fbo(w, h, QOpenGLFramebufferObject::Depth);
        fbo.bind();
        QOpenGLFunctions *gl = nullptr;
        if (QOpenGLContext *ctx = QOpenGLContext::currentContext())
            gl = ctx->functions();
        if (gl)
            gl->glViewport(0, 0, w, h);
        ok = renderer->renderOffscreen(viewer->backgroundColor(), &viewMat.getValue(),
                                       &projMat.getValue(), w, h);
        if (ok && gl) {
            // Re-bind first: the engine's frame leaves its own readback
            // FBO on the READ binding.
            fbo.bind();
            pixels.resize(size_t(w) * h * 4);
            gl->glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        fbo.release();
    }
    glWidget->doneCurrent();
    if (previous && previousSurface)
        previous->makeCurrent(previousSurface);
    renderer->clearCaptureScene();
    if (!ok || pixels.empty()) {
        FC_WARN("shader graph preview: backend render refused");
        clearPreview();
        editor->requestFrame();
        return;
    }
    // The finished frame's alpha is not a coverage; the pane is opaque.
    for (size_t i = 3; i < pixels.size(); i += 4)
        pixels[i] = 255;
    setPreviewImage(w, h, pixels.data());

    // A shader still compiling drew its stock stand-in: say so and
    // come back when the backend reports the compile done.
    const bool pending = renderer->shaderCompilePending();
    compileGeneration = renderer->shaderCompileGeneration();
    FC_LOG("shader graph preview: rendered " << w << "x" << h << " pending " << pending
           << " generation " << compileGeneration);
    setCompiling(pending);
    if (pending) {
        if (!poll.isActive())
            poll.start();
    }
    else
        poll.stop();
    editor->requestFrame();
}

void ShaderGraphHost::renderTraced(int w, int h, View3DInventorViewer *viewer)
{
    // No shader compile to wait for on this path.
    setCompiling(false);
    poll.stop();

    Render::Cycles::SceneInput input;
    const QColor col = viewer->backgroundColor();
    viewer->cyclesSceneConfig(input, col);
    // The pane's background is the view's flat colour in both modes:
    // the raster path clears its offscreen frame to it (the gradient
    // the config feed states is the on-screen view's, drawn over the
    // backend's frame there) and shows no sky behind the ball, so the
    // traced film is left transparent where the environment would be
    // seen and composites over the same colour -- a mode switch moves
    // the sphere, not the pane.
    input.pbr.envBackground = false;
    input.background = Render::Background();
    input.background.type = Render::Background::Flat;
    input.background.fromColor = (uint32_t(col.red()) << 24) | (uint32_t(col.green()) << 16)
        | (uint32_t(col.blue()) << 8) | 0xff;
    SbMatrix viewMat, projMat;
    cameraMatrices(w, h, viewMat, projMat);
    std::memcpy(input.camera.view, viewMat.getValue(), sizeof(input.camera.view));
    std::memcpy(input.camera.proj, projMat.getValue(), sizeof(input.camera.proj));
    input.camera.width = w;
    input.camera.height = h;

    // What the tracer's shader is made of: a value drag is a new
    // shader key on this path (the raster one only moves uniforms),
    // so the parameter values are part of the signature.
    std::string signature = fragment->sourceProgram.getValue().getString();
    signature += '\n';
    signature += fragment->sourceSurface.getValue().getString();
    for (const auto &p : liveParams) {
        signature += '\n';
        signature += p.name;
        signature.append(reinterpret_cast<const char *>(p.values.data()),
                         p.values.size() * sizeof(float));
    }
    if (tracedInput && signature == tracedSignature && sameConfig(*tracedInput, input)) {
        // The cheap path: a camera move restarts the sampling from the
        // coarse divider, throttled by the session.
        tracer->setCamera(input.camera);
        return;
    }
    if (!translateScene(w, h, input.draws)) {
        clearPreview();
        editor->requestFrame();
        return;
    }
    FC_LOG("shader graph preview: tracer scene " << input.draws.size() << " draws");
    tracer->setScene(input);
    tracedSignature = std::move(signature);
    input.draws.clear();
    tracedInput = std::make_unique<Render::Cycles::SceneInput>(std::move(input));
    updateTracedStatus();
    editor->requestFrame();
}

void ShaderGraphHost::takeTracedFrame()
{
    if (!tracer || !tracedInput)
        return;
    const bool managed = tracedInput->output.transform == Render::OutputConfig::SRGB;
    const Render::Background &background = tracedInput->background;
    std::vector<uint8_t> rgba;
    int fw = 0;
    int fh = 0;
    const bool got = tracer->takeFrame([&](const void *px, int width, int height) {
        // Premultiplied linear half4 over the view's background,
        // encoded exactly when the scene is colour managed -- the
        // bytes the raster readback holds -- bottom-up as
        // setPreviewImage reads them. The frame is at the divider's
        // size early in a render; the pane stretches it.
        Render::Cycles::compositeFrame(px, width, height, background, managed, 4, false,
                                       rgba);
        fw = width;
        fh = height;
    });
    if (got && !rgba.empty()) {
        setPreviewImage(fw, fh, rgba.data());
        // One line per session: that the device this session was
        // started on is producing frames at all is what a device pick
        // has to be judged by -- two devices tracing the same scene to
        // the same sample count settle on the same picture.
        if (++tracedFrames == 1)
            FC_LOG("shader graph preview: first traced frame " << fw << "x" << fh);
    }
    updateTracedStatus();
    editor->requestFrame();
}

void ShaderGraphHost::updateTracedStatus()
{
    if (!tracer)
        return;
    Render::Cycles::ViewportStatus st = tracer->status();
    std::string text;
    if (!st.error.empty())
        text = "Path tracer: " + st.error;
    else if (!st.running)
        text = "Path tracing";
    else if (st.status.empty())
        text = "Path tracing " + std::to_string(int(st.progress * 100.0f)) + "%";
    else
        text = st.status;
    setPreviewStatus(text);
}

#include "moc_ShaderGraphHost.cpp"
