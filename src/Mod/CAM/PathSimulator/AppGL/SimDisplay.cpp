// SPDX-License-Identifier: LGPL-2.1-or-later AND MIT

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
 *                                                                         *
 *   Portions of this code are taken from:                                 *
 *   "OpenGL 4 Shading Language cookbook" Third edition                    *
 *   Written by: David Wolff                                               *
 *   Published by: <packt> www.packt.com                                   *
 *   License: MIT License                                                  *
 *                                                                         *
 *                                                                         *
 ***************************************************************************/

#include "SimDisplay.h"

#include <Gui/Renderer/DrawSurface.h>

#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <algorithm>
#include <numbers>

#include "SimDrawContext.h"

namespace CAMSimulator
{

void SimDisplay::InitShaders()
{
    auto* dev = Render::DrawDevice::instance();
    if (!dev) {
        return;
    }
    // The programs from the compiled shader pack (AppGL/shaders/*.sc).
    mRProgDiffuse = dev->createProgram("vs_camsim_norm", "fs_camsim_diffuse");
    mRProgInvDiffuse = dev->createProgram("vs_camsim_invnorm", "fs_camsim_diffuse");
    mRProgFlat = dev->createProgram("vs_camsim_norm", "fs_camsim_flat");
    mRProgGeom = dev->createProgram("vs_camsim_geom", "fs_camsim_geom");
    mRProgLighting = dev->createProgram("vs_camsim_fbo", "fs_camsim_lighting");
    mRProgLine = dev->createProgram("vs_camsim_line", "fs_camsim_line");
    mRProgCopy = dev->createProgram("vs_camsim_fbo", "fs_camsim_fbo");
    mRUniNormalRot = dev->createUniform("u_simNormalRot", Render::UniformType::Mat4);
    mRUniLightPos = dev->createUniform("u_simLightPos", Render::UniformType::Vec4);
    mRUniLightColor = dev->createUniform("u_simLightColor", Render::UniformType::Vec4);
    mRUniLightAmbient = dev->createUniform("u_simLightAmbient", Render::UniformType::Vec4);
    mRUniObjectColor = dev->createUniform("u_simObjectColor", Render::UniformType::Vec4);
    mRUniObjectColorAlpha = dev->createUniform("u_simObjectColorAlpha", Render::UniformType::Vec4);
    mRUniParams = dev->createUniform("u_simParams", Render::UniformType::Vec4);
    mRSampColor = dev->createUniform("s_simColor", Render::UniformType::Sampler);
    mRSampPosition = dev->createUniform("s_simPosition", Render::UniformType::Sampler);
    mRSampNormal = dev->createUniform("s_simNormal", Render::UniformType::Sampler);
    mRSampAo = dev->createUniform("s_simAo", Render::UniformType::Sampler);
    mRSampTex = dev->createUniform("s_simTex", Render::UniformType::Sampler);

    gSimDraw.uniNormalRot = mRUniNormalRot;
    gSimDraw.uniLightPos = mRUniLightPos;
    gSimDraw.uniLightColor = mRUniLightColor;
    gSimDraw.uniLightAmbient = mRUniLightAmbient;
    gSimDraw.uniObjectColor = mRUniObjectColor;
    gSimDraw.uniObjectColorAlpha = mRUniObjectColorAlpha;
    gSimDraw.uniParams = mRUniParams;
    // The light environment never changes after init.
    gSimDraw.setColor(gSimDraw.lightPos, lightPos, 0.0f);
    gSimDraw.setColor(gSimDraw.lightColor, lightColor, 0.0f);
    gSimDraw.setColor(gSimDraw.lightAmbient, ambientCol, 0.0f);

    mREffectAO = dev->createEffect(Render::EffectType::AO);

    // The fullscreen quad of the deferred resolve: clip-space pos2 +
    // uv2 vertices.
    float quadVertices[] = {
        -1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
        1.0f,  -1.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f,
        1.0f,  -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 1.0f
    };
    Render::VertexLayout layout;
    layout.add(Render::DrawAttrib::Position, 2, Render::DrawAttribType::Float)
        .add(Render::DrawAttrib::TexCoord0, 2, Render::DrawAttribType::Float);
    mRQuadVbo = dev->createVertexBuffer(quadVertices, sizeof(quadVertices), layout);
}

void SimDisplay::CreateDisplayFbos()
{
    auto* dev = Render::DrawDevice::instance();
    if (!dev) {
        return;
    }
    // The G-buffer: colour, view-space position, view-space normal
    // (RGBA32F -- the backend has no 3-channel float targets), the AO
    // prepass packing, and a D24S8 depth/stencil for the CSG. Point
    // filtering; the resolve samples texel centres.
    uint32_t flags = Render::TexturePoint | Render::TextureClamp;
    mRColTexture = dev->createRenderTexture(
        mWidth, mHeight, Render::DrawTextureFormat::RGBA8, flags);
    mRPosTexture = dev->createRenderTexture(
        mWidth, mHeight, Render::DrawTextureFormat::RGBA32F, flags);
    mRNormTexture = dev->createRenderTexture(
        mWidth, mHeight, Render::DrawTextureFormat::RGBA32F, flags);
    mRNormalZTexture = dev->createRenderTexture(
        mWidth, mHeight, Render::DrawTextureFormat::RGBA32F, flags);
    mRDepthTexture = dev->createRenderTexture(
        mWidth, mHeight, Render::DrawTextureFormat::D24S8, 0);
    Render::TextureHandle colors[4] = {mRColTexture, mRPosTexture,
                                       mRNormTexture, mRNormalZTexture};
    mRTarget = dev->createTarget(colors, 4, mRDepthTexture);
    mRPathTarget = dev->createTarget(&mRColTexture, 1, mRDepthTexture);
    // The resolve's output. Colour only -- the composite that reads it
    // needs no depth of its own, and giving it none keeps the sim's
    // depth/stencil out of the pass that crosses into a host frame.
    mRResolveTexture = dev->createRenderTexture(
        mWidth, mHeight, Render::DrawTextureFormat::RGBA8, flags);
    mRResolveTarget = dev->createTarget(&mRResolveTexture, 1, {});
}

SimDisplay::~SimDisplay()
{
    CleanGL();
}

void SimDisplay::InitGL()
{
    if (displayInitiated) {
        return;
    }

    // The facade driver reads these at frame time; before the first
    // camera update they must at least be defined.
    mat4x4_identity(mMatLookAt);
    mat4x4_identity(mProjMat);

    InitShaders();

    displayInitiated = true;

    UpdateWindowScale(800, 600);
}

void SimDisplay::CleanFbos()
{
    if (auto* dev = Render::DrawDevice::instance()) {
        if (mRTarget.valid()) {
            dev->destroy(mRTarget);
        }
        if (mRPathTarget.valid()) {
            dev->destroy(mRPathTarget);
        }
        if (mRColTexture.valid()) {
            dev->destroy(mRColTexture);
        }
        if (mRPosTexture.valid()) {
            dev->destroy(mRPosTexture);
        }
        if (mRNormTexture.valid()) {
            dev->destroy(mRNormTexture);
        }
        if (mRNormalZTexture.valid()) {
            dev->destroy(mRNormalZTexture);
        }
        if (mRDepthTexture.valid()) {
            dev->destroy(mRDepthTexture);
        }
        if (mRResolveTarget.valid()) {
            dev->destroy(mRResolveTarget);
        }
        if (mRResolveTexture.valid()) {
            dev->destroy(mRResolveTexture);
        }
    }
    mRTarget = {};
    mRPathTarget = {};
    mRResolveTarget = {};
    mRResolveTexture = {};
    mRColTexture = {};
    mRPosTexture = {};
    mRNormTexture = {};
    mRNormalZTexture = {};
    mRDepthTexture = {};
    mRLastAO = {};
}

void SimDisplay::CleanGL()
{
    CleanFbos();

    if (auto* dev = Render::DrawDevice::instance()) {
        Render::ProgramHandle progs[] = {mRProgDiffuse, mRProgInvDiffuse,
            mRProgFlat, mRProgGeom, mRProgLighting, mRProgLine,
            mRProgCopy};
        for (auto& p : progs) {
            if (p.valid()) {
                dev->destroy(p);
            }
        }
        Render::UniformHandle unis[] = {mRUniNormalRot, mRUniLightPos,
            mRUniLightColor, mRUniLightAmbient, mRUniObjectColor,
            mRUniObjectColorAlpha, mRUniParams, mRSampColor,
            mRSampPosition, mRSampNormal, mRSampAo, mRSampTex};
        for (auto& u : unis) {
            if (u.valid()) {
                dev->destroy(u);
            }
        }
        if (mRQuadVbo.valid()) {
            dev->destroy(mRQuadVbo);
        }
        if (mREffectAO.valid()) {
            dev->destroy(mREffectAO);
        }
    }
    mREffectAO = {};
    mRLastAO = {};
    gSimDraw.uniNormalRot = gSimDraw.uniLightPos = {};
    gSimDraw.uniLightColor = gSimDraw.uniLightAmbient = {};
    gSimDraw.uniObjectColor = gSimDraw.uniObjectColorAlpha = {};
    gSimDraw.uniParams = {};
    gSimDraw.program = {};

    mRProgDiffuse = mRProgInvDiffuse = mRProgFlat = {};
    mRProgGeom = mRProgLighting = mRProgLine = mRProgCopy = {};
    mRUniNormalRot = mRUniLightPos = mRUniLightColor = {};
    mRUniLightAmbient = mRUniObjectColor = mRUniObjectColorAlpha = {};
    mRUniParams = mRSampColor = mRSampPosition = mRSampNormal = mRSampAo = {};
    mRSampTex = {};
    mRQuadVbo = {};

    displayInitiated = false;
}

void SimDisplay::PrepareFrameBuffer()
{
    // The G-buffer clear is the pass clear the frame driver
    // configures; only the draw state accumulates here.
    gSimDraw.pass = SimPassScene;
    gSimDraw.cullEnabled = true;
    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = true;
}

void SimDisplay::StartDepthPass()
{
    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = true;
    gSimDraw.program = mRProgFlat;
}

void SimDisplay::StartGeometryPass(const vec3& objColor, bool invertNormals)
{
    gSimDraw.program = mRProgGeom;
    gSimDraw.params[0] = invertNormals ? 1.0f : 0.0f;
    gSimDraw.setColor(gSimDraw.objectColor, objColor);
    gSimDraw.cullEnabled = true;
    gSimDraw.state.blend = Render::BlendMode::None;
}

void SimDisplay::ScaleViewToStock(StockObject* obj)
{
    mMaxStockDimension = std::max(std::max(obj->size[0], obj->size[1]), obj->size[2]);
    UpdateProjectionMatrix();
}

void SimDisplay::RunAOFacade(Render::DrawSurface* surface, bool enabled, bool recalculate)
{
    if (!enabled || !surface || !mREffectAO.valid()
        || !mRNormalZTexture.valid()) {
        mRLastAO = {};
        return;
    }
    if (!recalculate && mRLastAO.valid()) {
        return;
    }
    Render::EffectParams params;
    // The engine's automatic radius is 5% of the scene bounding-sphere
    // size; the stock's largest dimension stands in for it here.
    params.radius = 0.05f * 1.7320508f * mMaxStockDimension;
    params.proj = &mProjMat[0][0];
    mRLastAO = surface->runEffect(mREffectAO, SimPassAOFirst,
                                  mRNormalZTexture, params);
}

void SimDisplay::ConfigureFacadeFrame(Render::DrawSurface* surface, const vec3& bgnd)
{
    if (!surface || !mRTarget.valid()) {
        return;
    }
    for (unsigned p = SimPassScene; p <= SimPassPath; p++) {
        surface->setPassTarget(p, p == SimPassPath ? mRPathTarget : mRTarget);
        surface->setPassRect(p, 0, 0, mWidth, mHeight);
        // The CSG depends on draws landing in submission order.
        surface->setPassSequential(p, true);
    }
    // The G-buffer clear; it runs only when the pass draws, so a
    // cached frame keeps the buffer.
    surface->setPassClear(SimPassScene, 0x00000000, 1.0f, 0,
                          Render::ClearColor | Render::ClearDepth
                              | Render::ClearStencil);
    surface->setPassClear(SimPassBaseShape, 0, 1.0f, 0, Render::ClearNone);
    surface->setPassClear(SimPassPath, 0, 1.0f, 0, Render::ClearNone);
    const float* view = &mMatLookAt[0][0];
    surface->setPassTransform(SimPassScene, view, &mProjMat[0][0]);
    // The base shape's glPolygonOffset(0, -2) became this pass's
    // slightly-closer projection -- the trick the GL path's (unused)
    // GeomCloser shader carried.
    mat4x4 biased;
    mat4x4_dup(biased, mProjMat);
    biased[2][2] *= 0.99999f;
    surface->setPassTransform(SimPassBaseShape, view, &biased[0][0]);
    surface->setPassTransform(SimPassPath, view, &mProjMat[0][0]);
    // The resolve lands in the sim's own colour target, cleared fully
    // transparent so that the G-buffer's coverage survives as alpha
    // for the composite to blend with.
    surface->setPassTarget(SimPassResolve, mRResolveTarget);
    surface->setPassRect(SimPassResolve, 0, 0, mWidth, mHeight);
    surface->setPassSequential(SimPassResolve, false);
    surface->setPassClear(SimPassResolve, 0x00000000, 1.0f, 0,
                          Render::ClearColor);

    // The composite draws into whatever this surface composites into:
    // its own backbuffer standalone, the host's scene target attached
    // (docs/CAMSimRenderPort.md sec 8.4). hostTarget() answers both --
    // it is the invalid handle, i.e. "my backbuffer", when there is no
    // host.
    surface->setPassTarget(SimPassComposite, surface->hostTarget());
    surface->setPassRect(SimPassComposite, 0, 0, mWidth, mHeight);
    surface->setPassSequential(SimPassComposite, false);
    if (surface->attached()) {
        // The host drew its own background and its own scene into that
        // target already. Clearing here would erase them.
        surface->setPassClear(SimPassComposite, 0, 1.0f, 0,
                              Render::ClearNone);
        return;
    }
    auto channel = [](float c) {
        if (c < 0.0f) {
            c = 0.0f;
        }
        if (c > 1.0f) {
            c = 1.0f;
        }
        return uint32_t(c * 255.0f + 0.5f);
    };
    uint32_t rgba = (channel(bgnd[0]) << 24) | (channel(bgnd[1]) << 16)
        | (channel(bgnd[2]) << 8) | 0xff;
    surface->setPassClear(SimPassComposite, rgba, 1.0f, 0,
                          Render::ClearColor | Render::ClearDepth);
}

void SimDisplay::RenderCompositeFacade(Render::DrawSurface* surface,
                                       unsigned pass)
{
    if (!surface || !mRProgCopy.valid() || !mRQuadVbo.valid()
            || !mRResolveTexture.valid()) {
        return;
    }
    surface->setTexture(0, mRSampTex, mRResolveTexture);
    Render::DrawState state;
    state.depthWrite = false;
    state.depthFunc = Render::CompareFunc::Always;
    state.blend = Render::BlendMode::Alpha;
    surface->setState(state);
    surface->setVertexBuffer(mRQuadVbo);
    surface->submit(pass, mRProgCopy);
}

void SimDisplay::RenderResultFacade(Render::DrawSurface* surface, unsigned pass)
{
    if (!surface || !mRProgLighting.valid() || !mRQuadVbo.valid()) {
        return;
    }
    // The resolve's uniform values, vec3 padded to vec4. The light
    // position is uploaded as-is even though the G-buffer is
    // view-space -- the original GL path did the same, so the
    // (camera-locked) lighting matches what the simulator always had.
    float v[4];
    auto vec4of = [&v](const vec3& src) {
        v[0] = src[0];
        v[1] = src[1];
        v[2] = src[2];
        v[3] = 0.0f;
        return v;
    };
    surface->setUniform(mRUniLightPos, vec4of(lightPos));
    surface->setUniform(mRUniLightColor, vec4of(lightColor));
    surface->setUniform(mRUniLightAmbient, vec4of(ambientCol));
    // y = ssaoActive: on when the AO effect ran this frame or its
    // cached result stands (RunAOFacade).
    const bool aoOn = mRLastAO.valid();
    float params[4] = {0.0f, aoOn ? 1.0f : 0.0f, 0.0f, 0.0f};
    surface->setUniform(mRUniParams, params);
    surface->setTexture(0, mRSampColor, mRColTexture);
    surface->setTexture(1, mRSampPosition, mRPosTexture);
    surface->setTexture(2, mRSampNormal, mRNormTexture);
    // With AO off the shader branches away from the sample, but the
    // slot must still hold a valid texture on every backend.
    surface->setTexture(3, mRSampAo, aoOn ? mRLastAO : mRColTexture);
    Render::DrawState state;
    state.depthWrite = false;
    state.depthFunc = Render::CompareFunc::Always;
    // Into the sim's own resolve target, which the composite pass then
    // blends: written verbatim so the G-buffer's coverage alpha
    // survives to that blend.
    state.blend = Render::BlendMode::None;
    surface->setState(state);
    surface->setVertexBuffer(mRQuadVbo);
    surface->submit(pass, mRProgLighting);
    gSimDraw.submitted = true;
}

void SimDisplay::SetPathColor(const vec3& normal, const vec3& rapid)
{
    pathLineColor[0] = normal[0];
    pathLineColor[1] = normal[1];
    pathLineColor[2] = normal[2];

    // TODO: Different color for rapid moves is not supported for now.

    (void)rapid;
}

void SimDisplay::SetupLinePathPass(int curSegment, bool isHidden)
{
    pathLineColor[3] = isHidden ? 0.1f : 1.0f;
    gSimDraw.state.depthFunc =
        isHidden ? Render::CompareFunc::Greater : Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = false;
    gSimDraw.state.blend = Render::BlendMode::Alpha;
    // The quads' winding depends on each segment's screen direction.
    gSimDraw.cullEnabled = false;
    gSimDraw.program = mRProgLine;
    for (int i = 0; i < 4; i++) {
        gSimDraw.objectColorAlpha[i] = pathLineColor[i];
    }
    gSimDraw.setColor(gSimDraw.objectColor, pathLineColorPassed);
    gSimDraw.params[2] = (float)curSegment;
    // Half the GL path's glLineWidth(2), in pixels: the vertex shader
    // offsets each quad side by this much from the segment.
    gSimDraw.params[3] = 1.0f;
}

void SimDisplay::UpdateWindowScale(int width, int height)
{
    if (!displayInitiated || (width == mWidth && height == mHeight)) {
        return;
    }

    mWidth = width;
    mHeight = height;

    CleanFbos();
    CreateDisplayFbos();
    UpdateProjectionMatrix();
}

void SimDisplay::UpdateCamera(const SoCamera& camera)
{
    if (!displayInitiated) {
        return;
    }

    UpdateCameraView(camera);
    UpdateCameraProjection(camera);
}

void SimDisplay::UpdateCameraView(const SoCamera& camera)
{

    const SbVec3f position = camera.position.getValue();
    const SbRotation orientation = camera.orientation.getValue();

    if (position == mCameraPosition && orientation == mCameraOrientation) {
        return;
    }

    mCameraPosition = position;
    mCameraOrientation = orientation;

    UpdateViewMatrix();
}

void SimDisplay::UpdateCameraProjection(const SoCamera& camera)
{
    float heightAngle = std::numbers::pi / 4;
    float height = 100.0f;

    const auto perspective = dynamic_cast<const SoPerspectiveCamera*>(&camera);
    const auto orthographic = dynamic_cast<const SoOrthographicCamera*>(&camera);

    // TODO: We can't use the values from the camera here because the dummy viewer never actually
    // renders the scene and therefore the nearDistance and farDistance of the camera are never
    // updated. Figure out a way to update those values without rendering the scene.

    float nearDistance = 0.0;
    float farDistance = 0.0;

    if (perspective) {
        heightAngle = perspective->heightAngle.getValue();

        nearDistance = mMaxStockDimension * 0.001f;
        farDistance = mMaxStockDimension * 10.0f;
    }
    else if (orthographic) {
        height = orthographic->height.getValue();

        nearDistance = -mMaxStockDimension * 10.0f;
        farDistance = mMaxStockDimension * 10.0f;
    }

    if ((bool)perspective == mCameraPerspective && heightAngle == mCameraHeightAngle
        && height == mCameraHeight && nearDistance == mCameraNearDistance
        && farDistance == mCameraFarDistance) {
        return;
    }

    mCameraPerspective = (bool)perspective;
    mCameraHeightAngle = heightAngle;
    mCameraHeight = height;
    mCameraNearDistance = nearDistance;
    mCameraFarDistance = farDistance;

    UpdateProjectionMatrix();
}

void SimDisplay::UpdateViewMatrix()
{
    SbVec3f up(0, 1, 0);
    mCameraOrientation.multVec(up, up);

    SbVec3f dir(0, 0, -1);
    mCameraOrientation.multVec(dir, dir);

    const auto target = mCameraPosition + dir;
    mat4x4_look_at(mMatLookAt, mCameraPosition.getValue(), target.getValue(), up.getValue());

    updateDisplay = true;
}

void SimDisplay::UpdateProjectionMatrix()
{
    // Setup projection

    const float aspect = (float)mWidth / mHeight;

    if (mCameraPerspective) {
        mat4x4_perspective(mProjMat, mCameraHeightAngle, aspect, mCameraNearDistance, mCameraFarDistance);
    }
    else {
        const float h = mCameraHeight;
        const float w = mCameraHeight * aspect;
        mat4x4_ortho(mProjMat, -w / 2, w / 2, -h / 2, h / 2, mCameraNearDistance, mCameraFarDistance);
    }

    updateDisplay = true;
}

}  // namespace CAMSimulator
