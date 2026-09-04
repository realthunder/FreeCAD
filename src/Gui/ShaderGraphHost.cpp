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
#endif

#include <App/Document.h>
#include <App/ShaderObject.h>
#include <Base/Console.h>

#include "ShaderGraphHost.h"
#include "Application.h"
#include "Document.h"
#include "View3DInventor.h"
#include "View3DInventorViewer.h"
#include "Inventor/SoFCRenderCache.h"
#include "Inventor/SoFCVertexCache.h"
#include "Inventor/SoFCRenderCacheManager.h"
#include "Inventor/SoFCRendererBridge.h"
#include "Renderer/GraphEditor/GraphEditorWidget.h"
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
}

ShaderGraphHost::~ShaderGraphHost() = default;

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

void ShaderGraphHost::previewInvalidated()
{
    // Coalesced: a drag reports every motion, a load reports once per
    // element; one render answers everything that arrived meanwhile.
    if (!render.isActive())
        render.start();
}

bool ShaderGraphHost::findBackend(Render::Renderer *&renderer,
                                  View3DInventorViewer *&viewer) const
{
    Gui::Document *gdoc = Application::Instance->getDocument(program->getDocument());
    if (!gdoc)
        return false;
    for (auto *view : gdoc->getMDIViewsOfType(View3DInventor::getClassTypeId())) {
        auto *v3d = static_cast<View3DInventor*>(view);
        if (auto *r = v3d->getViewer()->getExternalRenderer()) {
            renderer = r;
            viewer = v3d->getViewer();
            return true;
        }
    }
    return false;
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
    auto sphere = new SoSphere;
    sphere->radius = kSphereRadius;
    root->addChild(sphere);
}

void ShaderGraphHost::renderPreview()
{
    const int w = previewWidth();
    const int h = previewHeight();
    FC_LOG("shader graph preview: render " << w << "x" << h << " camera yaw "
           << camera().yaw << " pitch " << camera().pitch << " dist " << camera().distance);
    if (w <= 0 || h <= 0)
        return;
    const std::string &xml = documentText();
    Render::Renderer *renderer = nullptr;
    View3DInventorViewer *viewer = nullptr;
    if (xml.empty() || !findBackend(renderer, viewer)) {
        clearPreview();
        setCompiling(false);
        poll.stop();
        editor->requestFrame();
        return;
    }
    buildScene();
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

    // Coin scene -> render caches -> backend draws, as the shaded
    // underlay's derived capture does (ShadedUnderlay.cpp,
    // captureSceneViaBackend).
    SoFCRenderCacheManager manager;
    manager.traverse(root, SbViewportRegion(short(w), short(h)));
    SoFCRenderCache *cache = manager.getSceneCache();
    if (!cache) {
        clearPreview();
        editor->requestFrame();
        return;
    }
    Render::DrawCallList draws = RendererBridge::translate(
        cache->getVertexCaches(true), RendererBridge::SectionOnTop());
    FC_LOG("shader graph preview: " << draws.size() << " draws, params " << liveParams.size());
    if (draws.empty() || !renderer->setCaptureScene(std::move(draws))) {
        clearPreview();
        editor->requestFrame();
        return;
    }

    SbMatrix viewMat, projMat;
    previewCamera->getViewVolume(float(w) / float(h)).getMatrices(viewMat, projMat);
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

#include "moc_ShaderGraphHost.cpp"
