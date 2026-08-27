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

#include "SimDrawContext.h"

#include <Inventor/nodes/SoOrthographicCamera.h>
#include <Inventor/nodes/SoPerspectiveCamera.h>
#include <algorithm>
#include <cstring>
#include <numbers>

// include this last as the defines can mess up other includes
#include "OpenGlWrapper.h"

namespace CAMSimulator
{

constexpr auto pi = std::numbers::pi_v<float>;

SimDisplay::SimDisplay()
{
    // The default context; the per-surface ones come and go.
    mHosts.push_back(std::make_unique<SimHostContext>());
    mCurrent = mHosts.front().get();
}

void SimDisplay::SetCurrentHost(Render::DrawSurface* surface)
{
    SimHostContext* found = nullptr;
    for (auto& h : mHosts) {
        if (h->surface == surface) {
            found = h.get();
            break;
        }
    }
    if (!found) {
        auto ctx = std::make_unique<SimHostContext>();
        ctx->surface = surface;
        found = ctx.get();
        mHosts.push_back(std::move(ctx));
    }
    mCurrent = found;
    // Each host has its own AO effect (its targets cache that host's
    // last run), created here rather than in InitShaders because a
    // per-surface context can appear at any time after init.
    if (surface && !found->effectAO.valid()) {
        if (auto* dev = Render::DrawDevice::instance()) {
            found->effectAO = dev->createEffect(Render::EffectType::AO);
        }
    }
}

void SimDisplay::DropHost(Render::DrawSurface* surface)
{
    if (!surface) {
        return;
    }
    for (auto it = mHosts.begin(); it != mHosts.end(); ++it) {
        if ((*it)->surface != surface) {
            continue;
        }
        DestroyHostFbos(**it);
        if ((*it)->effectAO.valid()) {
            if (auto* dev = Render::DrawDevice::instance()) {
                dev->destroy((*it)->effectAO);
            }
        }
        if (mCurrent == it->get()) {
            mCurrent = mHosts.front().get();
        }
        mHosts.erase(it);
        return;
    }
}

void SimDisplay::InitShaders()
{
    if (gSimDraw.legacyGL) {
        // use shaders
        //   standard diffuse shader
        shader3D.CompileShader("StdDiffuse", VertShader3DNorm, FragShaderNorm);
        shader3D.UpdateEnvColor(lightPos, lightColor, ambientCol, 0.0f);

        //   invarted normal diffuse shader for inner mesh
        shaderInv3D.CompileShader("InvertNormal", VertShader3DInvNorm, FragShaderNorm);
        shaderInv3D.UpdateEnvColor(lightPos, lightColor, ambientCol, 0.0f);

        //   null shader to calculate meshes only (simulation stage)
        shaderFlat.CompileShader("Null", VertShader3DNorm, FragShaderFlat);

        //   texture shader to render Simulator FBO
        shaderSimFbo.CompileShader("Texture", VertShader2DFbo, FragShader2dFbo);
        shaderSimFbo.UpdateTextureSlot(0);

        // geometric shader - generate texture with all geometric info for further processing
        shaderGeom.CompileShader("Geometric", VertShaderGeom, FragShaderGeom);
        shaderGeomCloser.CompileShader("GeomCloser", VertShaderGeom, FragShaderGeom);

        // SSAO shader - generate SSAO info and embed in texture buffer
        shaderSSAO.CompileShader("SSAO", VertShader2DFbo, FragShaderSSAO);
        shaderSSAO.UpdateRandomTexSlot(0);
        shaderSSAO.UpdatePositionTexSlot(1);
        shaderSSAO.UpdateNormalTexSlot(2);

        // SSAO blur shader - smooth generated SSAO texture
        shaderSSAOBlur.CompileShader("Blur", VertShader2DFbo, FragShaderSSAOBlur);
        shaderSSAOBlur.UpdateSsaoTexSlot(0);

        // SSAO lighting shader - apply lightig modified by SSAO calculations
        shaderSSAOLighting.CompileShader("SsaoLighting", VertShader2DFbo, FragShaderSSAOLighting);
        shaderSSAOLighting.UpdateColorTexSlot(0);
        shaderSSAOLighting.UpdatePositionTexSlot(1);
        shaderSSAOLighting.UpdateNormalTexSlot(2);
        shaderSSAOLighting.UpdateSsaoTexSlot(3);
        shaderSSAOLighting.UpdateEnvColor(lightPos, lightColor, ambientCol, 0.01f);

        // Mill Path Line Shader
        shaderLinePath.CompileShader("PathLine", VertShader3DLine, FragShader3DLine);
    }

    if (auto* dev = Render::DrawDevice::instance()) {
        // The same programs from the compiled shader pack
        // (AppGL/shaders/*.sc), minus the dead GL ones (SimFbo, the
        // closer geometry pass) and the SSAO chain the engine AO
        // effect replaces.
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
        mRUniComposite = dev->createUniform("u_simComposite", Render::UniformType::Vec4);
        mRUniDepthXform = dev->createUniform("u_simDepthXform", Render::UniformType::Mat4);

        gSimDraw.uniNormalRot = mRUniNormalRot;
        gSimDraw.uniLightPos = mRUniLightPos;
        gSimDraw.uniLightColor = mRUniLightColor;
        gSimDraw.uniLightAmbient = mRUniLightAmbient;
        gSimDraw.uniObjectColor = mRUniObjectColor;
        gSimDraw.uniObjectColorAlpha = mRUniObjectColorAlpha;
        gSimDraw.uniParams = mRUniParams;
        // The light environment never changes after init (the GL path
        // sets it once on each shader here too).
        gSimDraw.setColor(gSimDraw.lightPos, lightPos, 0.0f);
        gSimDraw.setColor(gSimDraw.lightColor, lightColor, 0.0f);
        gSimDraw.setColor(gSimDraw.lightAmbient, ambientCol, 0.0f);

    }
}

void SimDisplay::CreateFboQuad()
{
    float quadVertices[] = {// a quad that fills the entire screen in Normalized Device Coordinates.
                            // positions   // texCoords
                            -1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
                            1.0f,  -1.0f, 1.0f, 0.0f, -1.0f, 1.0f,  0.0f, 1.0f,
                            1.0f,  -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 1.0f
    };

    if (gSimDraw.legacyGL) {
        glGenBuffers(1, &mFboQuadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, mFboQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices[0], GL_STATIC_DRAW);
    }

    if (auto* dev = Render::DrawDevice::instance()) {
        Render::VertexLayout layout;
        layout.add(Render::DrawAttrib::Position, 2, Render::DrawAttribType::Float)
            .add(Render::DrawAttrib::TexCoord0, 2, Render::DrawAttribType::Float);
        mRQuadVbo = dev->createVertexBuffer(quadVertices, sizeof(quadVertices), layout);
    }
}

void SimDisplay::SetupVertexAttribs() const
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, mFboQuadVBO);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
}

void SimDisplay::CreateGBufTex(GLenum texUnit, GLint intFormat, GLenum format, GLenum type, GLuint& texid)
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    glActiveTexture(texUnit);
    glGenTextures(1, &texid);
    glBindTexture(GL_TEXTURE_2D, texid);
    glTexImage2D(
        GL_TEXTURE_2D, 0, intFormat, current().width, current().height, 0, format, type, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
}

void SimDisplay::UniformHemisphere(vec3& randVec)
{
    float x1 = distr01(generator);
    float x2 = distr01(generator);
    float s = sqrt(1.0f - x1 * x1);
    randVec[0] = cosf(pi * 2 * x2) * s;
    randVec[1] = sinf(pi * 2 * x2) * s;
    randVec[2] = x1;
}

void SimDisplay::UniformCircle(vec3& randVec)
{
    float x = distr01(generator);
    randVec[0] = cosf(pi * 2 * x);
    randVec[1] = sinf(pi * 2 * x);
    randVec[2] = 0;
}

void SimDisplay::CreateDisplayFbos()
{
    auto& host = current();
    if (gSimDraw.legacyGL) {
        // setup frame buffer for simulation
        glGenFramebuffers(1, &mFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

        // a color texture for the frame buffer
        CreateGBufTex(GL_TEXTURE0, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, mFboColTexture);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mFboColTexture, 0);

        // a position texture for the frame buffer
        CreateGBufTex(GL_TEXTURE1, GL_RGB32F, GL_RGBA, GL_FLOAT, mFboPosTexture);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mFboPosTexture, 0);

        // a normal texture for the frame buffer
        CreateGBufTex(GL_TEXTURE2, GL_RGB32F, GL_RGBA, GL_FLOAT, mFboNormTexture);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, mFboNormTexture, 0);

        unsigned int attachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, attachments);

        glGenRenderbuffers(1, &mRboDepthStencil);
        glBindRenderbuffer(GL_RENDERBUFFER, mRboDepthStencil);
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH24_STENCIL8,
            host.width,
            host.height
        );  // use a single renderbuffer object for both a depth AND stencil buffer.
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER,
            mRboDepthStencil
        );  // now actually attach it

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    }

    if (auto* dev = Render::DrawDevice::instance()) {
        // Same G-buffer as facade textures: the RGB32F attachments
        // become RGBA32F (no 3-channel float targets in the backend)
        // and the depth/stencil renderbuffer becomes a D24S8 texture.
        // Point filtering, matching the GL parameters above.
        uint32_t flags = Render::TexturePoint | Render::TextureClamp;
        host.colTexture = dev->createRenderTexture(
            host.width, host.height, Render::DrawTextureFormat::RGBA8, flags);
        host.posTexture = dev->createRenderTexture(
            host.width, host.height, Render::DrawTextureFormat::RGBA32F, flags);
        host.normTexture = dev->createRenderTexture(
            host.width, host.height, Render::DrawTextureFormat::RGBA32F, flags);
        host.normalZTexture = dev->createRenderTexture(
            host.width, host.height, Render::DrawTextureFormat::RGBA32F, flags);
        host.depthTexture = dev->createRenderTexture(
            host.width, host.height, Render::DrawTextureFormat::D24S8, 0);
        Render::TextureHandle colors[4] = {host.colTexture, host.posTexture,
                                           host.normTexture, host.normalZTexture};
        host.target = dev->createTarget(colors, 4, host.depthTexture);
        // The resolve's output. Colour only -- the composite that
        // reads it needs no depth of its own, and giving it none keeps
        // the sim's depth/stencil out of the pass that crosses into a
        // host frame.
        host.resolveTexture = dev->createRenderTexture(
            host.width, host.height, Render::DrawTextureFormat::RGBA8, flags);
        host.resolveTarget = dev->createTarget(&host.resolveTexture, 1, {});
    }
}

void SimDisplay::CreateSsaoFbos()
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    mSsaoValid = true;

    // setup framebuffer for SSAO processing
    glGenFramebuffers(1, &mSsaoFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mSsaoFbo);
    // SSAO color buffer
    CreateGBufTex(GL_TEXTURE0, GL_R16F, GL_RED, GL_FLOAT, mFboSsaoTexture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mFboSsaoTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        mSsaoValid = false;
        return;
    }

    // setup framebuffer for SSAO blur processing
    glGenFramebuffers(1, &mSsaoBlurFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mSsaoBlurFbo);
    CreateGBufTex(GL_TEXTURE0, GL_R16F, GL_RED, GL_FLOAT, mFboSsaoBlurTexture);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mFboSsaoBlurTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        mSsaoValid = false;
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // generate sample kernel
    int kernSize = 64;
    for (int i = 0; i < kernSize; i++) {
        vec3 sample;
        UniformHemisphere(sample);
        float scale = ((float)(i * i)) / (kernSize * kernSize);
        float interpScale = 0.1f * (1.0f - scale) + scale;
        vec3_scale(sample, sample, interpScale);
        mSsaoKernel.push_back(*(Point3D*)sample);
    }
    shaderSSAO.Activate();
    shaderSSAO.UpdateKernelVals(mSsaoKernel.size(), &mSsaoKernel[0].x);

    // generate random direction texture
    int randSize = 4 * 4;
    std::vector<Point3D> randDirections;
    for (int i = 0; i < randSize; i++) {
        vec3 randvec;
        UniformCircle(randvec);
        randDirections.push_back(*(Point3D*)randvec);
    }

    glGenTextures(1, &mFboRandTexture);
    glBindTexture(GL_TEXTURE_2D, mFboRandTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGB, GL_FLOAT, &randDirections[0].x);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
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

    // setup light object
    mlightObject.GenerateBoxStock(-0.5f, -0.5f, -0.5f, 1, 1, 1);

    // The facade driver reads these at frame time; before the first
    // camera update they must at least be defined.
    mat4x4_identity(current().matLookAt);
    mat4x4_identity(current().projMat);

    InitShaders();
    CreateFboQuad();

    displayInitiated = true;

    UpdateWindowScale(800, 600);
}

void SimDisplay::DestroyHostFbos(SimHostContext& host)
{
    if (auto* dev = Render::DrawDevice::instance()) {
        if (host.target.valid()) {
            dev->destroy(host.target);
        }
        if (host.colTexture.valid()) {
            dev->destroy(host.colTexture);
        }
        if (host.posTexture.valid()) {
            dev->destroy(host.posTexture);
        }
        if (host.normTexture.valid()) {
            dev->destroy(host.normTexture);
        }
        if (host.normalZTexture.valid()) {
            dev->destroy(host.normalZTexture);
        }
        if (host.depthTexture.valid()) {
            dev->destroy(host.depthTexture);
        }
        if (host.resolveTarget.valid()) {
            dev->destroy(host.resolveTarget);
        }
        if (host.resolveTexture.valid()) {
            dev->destroy(host.resolveTexture);
        }
    }
    host.target = {};
    host.resolveTarget = {};
    host.resolveTexture = {};
    host.colTexture = {};
    host.posTexture = {};
    host.normTexture = {};
    host.normalZTexture = {};
    host.depthTexture = {};
    host.lastAO = {};
}

void SimDisplay::CleanFbos()
{
    // cleanup frame buffers
    GLDELETE_FRAMEBUFFER(mFbo);
    GLDELETE_FRAMEBUFFER(mSsaoFbo);
    GLDELETE_FRAMEBUFFER(mSsaoBlurFbo);

    // cleanup fbo textures
    GLDELETE_TEXTURE(mFboColTexture);
    GLDELETE_TEXTURE(mFboPosTexture);
    GLDELETE_TEXTURE(mFboNormTexture);
    GLDELETE_TEXTURE(mFboSsaoTexture);
    GLDELETE_TEXTURE(mFboSsaoBlurTexture);
    GLDELETE_TEXTURE(mFboRandTexture);
    GLDELETE_RENDERBUFFER(mRboDepthStencil);

    DestroyHostFbos(current());
}

void SimDisplay::CleanGL()
{
    CleanFbos();

    // cleanup geometry
    GLDELETE_BUFFER(mFboQuadVBO);

    // cleanup shaders (no-ops when the legacy path never compiled them)
    shader3D.Destroy();
    shaderInv3D.Destroy();
    shaderFlat.Destroy();
    shaderSimFbo.Destroy();
    shaderGeom.Destroy();
    shaderSSAO.Destroy();
    shaderSSAOLighting.Destroy();
    shaderSSAOBlur.Destroy();

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
            mRSampPosition, mRSampNormal, mRSampAo, mRSampTex,
            mRUniComposite, mRUniDepthXform};
        for (auto& u : unis) {
            if (u.valid()) {
                dev->destroy(u);
            }
        }
        if (mRQuadVbo.valid()) {
            dev->destroy(mRQuadVbo);
        }
    }
    // Every host's resources, and the per-surface contexts
    // themselves: a full teardown forgets the surfaces too.
    for (auto& h : mHosts) {
        DestroyHostFbos(*h);
        if (h->effectAO.valid()) {
            if (auto* dev = Render::DrawDevice::instance()) {
                dev->destroy(h->effectAO);
            }
            h->effectAO = {};
        }
    }
    mHosts.erase(mHosts.begin() + 1, mHosts.end());
    mCurrent = mHosts.front().get();
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
    mRSampTex = mRUniComposite = mRUniDepthXform = {};
    mRQuadVbo = {};

    displayInitiated = false;
}

void SimDisplay::InvalidateDisplay()
{
    // The SIMULATION changed, so every host's cached carve is stale
    // (camera and size changes mark only their own host, in the
    // Update* methods).
    for (auto& h : mHosts) {
        h->updateDisplay = true;
    }
}

bool SimDisplay::NeedsRecalculate() const
{
    return current().updateDisplay;
}

void SimDisplay::ClearRecalculate()
{
    current().updateDisplay = false;
}

void SimDisplay::PrepareFrameBuffer()
{
    if (gSimDraw.legacyGL) {
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
    }

    // The facade's G-buffer clear is the pass clear the frame driver
    // configures; only the draw state accumulates here.
    gSimDraw.pass = SimPassScene;
    gSimDraw.cullEnabled = true;
    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = true;
}

void SimDisplay::StartDepthPass()
{
    if (gSimDraw.legacyGL) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        shaderFlat.Activate();
        shaderFlat.UpdateViewMat(current().matLookAt);
    }

    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = true;
    gSimDraw.program = mRProgFlat;
}

void SimDisplay::StartGeometryPass(const vec3& objColor, bool invertNormals)
{
    if (gSimDraw.legacyGL) {
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
        shaderGeom.Activate();
        shaderGeom.UpdateNormalState(invertNormals);
        shaderGeom.UpdateViewMat(current().matLookAt);
        shaderGeom.UpdateObjColor(objColor);
        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
    }

    gSimDraw.program = mRProgGeom;
    gSimDraw.params[0] = invertNormals ? 1.0f : 0.0f;
    gSimDraw.setColor(gSimDraw.objectColor, objColor);
    gSimDraw.cullEnabled = true;
    gSimDraw.state.blend = Render::BlendMode::None;
}

// A 'closer' geometry pass is similar to std geometry pass, but render the objects
// slightly closer to the camera. This mitigates overlapping faces artifacts.
void SimDisplay::StartCloserGeometryPass(const vec3& objColor)
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
    shaderGeomCloser.Activate();
    shaderGeomCloser.UpdateNormalState(false);
    shaderGeomCloser.UpdateViewMat(current().matLookAt);
    shaderGeomCloser.UpdateObjColor(objColor);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
}

void SimDisplay::RenderLightObject()
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    shaderFlat.Activate();
    shaderFlat.UpdateObjColor(lightColor);
    mlightObject.render();
}

void SimDisplay::ScaleViewToStock(StockObject* obj)
{
    mMaxStockDimension = std::max(std::max(obj->size[0], obj->size[1]), obj->size[2]);
    UpdateProjectionMatrix();
}

void SimDisplay::RenderResult(bool recalculate, bool ssao)
{
    if (!displayInitiated) {
        return;
    }

    if (mSsaoValid && ssao) {
        RenderResultSSAO(recalculate);
    }
    else {
        RenderResultStandard();
    }
}

void SimDisplay::RenderResultStandard()
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    // set default frame buffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // display the sim result within the FBO
    shaderSSAOLighting.Activate();
    shaderSSAOLighting.UpdateColorTexSlot(0);
    shaderSSAOLighting.UpdatePositionTexSlot(1);
    shaderSSAOLighting.UpdateNormalTexSlot(2);
    shaderSSAOLighting.UpdateSsaoActive(false);
    // shaderSimFbo.Activate();
    SetupVertexAttribs();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFboColTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mFboPosTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mFboNormTexture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void SimDisplay::RenderResultSSAO(bool recalculate)
{
    if (!gSimDraw.legacyGL) {
        return;
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (recalculate) {
        // generate SSAO texture
        glBindFramebuffer(GL_FRAMEBUFFER, mSsaoFbo);
        shaderSSAO.Activate();
        shaderSSAO.UpdateRandomTexSlot(0);
        shaderSSAO.UpdatePositionTexSlot(1);
        shaderSSAO.UpdateNormalTexSlot(2);
        shaderSSAO.UpdateScreenDimension(current().width, current().height);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mFboRandTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mFboPosTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mFboNormTexture);
        SetupVertexAttribs();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // blur SSAO texture to remove noise
        glBindFramebuffer(GL_FRAMEBUFFER, mSsaoBlurFbo);
        glClear(GL_COLOR_BUFFER_BIT);
        shaderSSAOBlur.Activate();
        shaderSSAOBlur.UpdateSsaoTexSlot(0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mFboSsaoTexture);
        shaderSSAOBlur.UpdateScreenDimension(current().width, current().height);
        SetupVertexAttribs();
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // lighting pass:
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shaderSSAOLighting.Activate();
    shaderSSAOLighting.UpdateColorTexSlot(0);
    shaderSSAOLighting.UpdatePositionTexSlot(1);
    shaderSSAOLighting.UpdateNormalTexSlot(2);
    shaderSSAOLighting.UpdateSsaoTexSlot(3);
    shaderSSAOLighting.UpdateSsaoActive(true);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFboColTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mFboPosTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, mFboNormTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, mFboSsaoBlurTexture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    SetupVertexAttribs();
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void SimDisplay::RenderCompositeFacade(Render::DrawSurface* surface,
                                       unsigned pass)
{
    auto& host = current();
    if (!surface || !mRProgCopy.valid() || !mRQuadVbo.valid()
            || !host.resolveTexture.valid()) {
        return;
    }
    surface->setTexture(0, mRSampTex, host.resolveTexture);

    // This pass places the simulator's stock in the destination's
    // depth buffer -- shared with the host scene when attached, so the
    // two occlude each other (docs/CAMSimRenderPort.md sec 8.4), and
    // the surface's own when standalone, where it is what the
    // tool-path passes will test against (sec 10.3). The simulator's
    // own G-buffer depth cannot be handed over: its near/far come
    // from the stock size rather than from the camera
    // (UpdateCameraProjection), so it is a different depth space
    // entirely. What IS common is view space -- both cameras are the
    // same camera -- so the depth is rebuilt from the G-buffer's
    // view-space position through this matrix.
    //
    // Standalone there is no host camera and the target camera
    // degenerates to the sim's own view and projection -- the
    // transform collapses to the sim's projection -- which is the
    // whole of the two-flavour difference: one code path, no
    // writeDepth fork.
    mat4x4 depthXform;
    mat4x4_identity(depthXform);
    bool writeDepth = false;
    float hostView[16];
    float hostProj[16];
    if (host.posTexture.valid()) {
        mat4x4 hv;
        mat4x4 hp;
        if (surface->hostCamera(hostView, hostProj)) {
            std::memcpy(&hv[0][0], hostView, sizeof(hv));
            std::memcpy(&hp[0][0], hostProj, sizeof(hp));
        }
        else {
            mat4x4_dup(hv, host.matLookAt);
            mat4x4_dup(hp, host.projMat);
        }
        mat4x4 simToWorld;
        mat4x4_invert(simToWorld, host.matLookAt);
        mat4x4 toHostView;
        mat4x4_mul(toHostView, hv, simToWorld);
        mat4x4_mul(depthXform, hp, toHostView);
        writeDepth = true;
        surface->setTexture(1, mRSampPosition, host.posTexture);
    }

    // The destination decides whether the image is light or pixels:
    // the host's colour-managed scene target holds linear light and
    // its present pass encodes on the way out, so a display-space
    // image must be decoded here to avoid being encoded twice. The
    // standalone backbuffer is display space already.
    Render::DrawDevice* dev = Render::DrawDevice::instance();
    const float composite[4] = {surface->hostLinearColor() ? 1.0f : 0.0f,
                                writeDepth ? 1.0f : 0.0f,
                                dev && dev->homogeneousDepth() ? 1.0f : 0.0f,
                                0.0f};
    surface->setUniform(mRUniComposite, composite);
    surface->setUniform(mRUniDepthXform, &depthXform[0][0]);
    Render::DrawState state;
    // Writing depth means testing against it too: the host's scene
    // geometry is already in there, and the nearer of the two wins.
    state.depthWrite = writeDepth;
    state.depthFunc = writeDepth ? Render::CompareFunc::LEqual
                                 : Render::CompareFunc::Always;
    state.blend = Render::BlendMode::Alpha;
    surface->setState(state);
    surface->setVertexBuffer(mRQuadVbo);
    surface->submit(pass, mRProgCopy);
}

void SimDisplay::RenderResultFacade(Render::DrawSurface* surface, unsigned pass)
{
    auto& host = current();
    if (!surface || !mRProgLighting.valid() || !mRQuadVbo.valid()) {
        return;
    }
    // The GL resolve's uniform values, vec3 padded to vec4. The light
    // position is uploaded as-is even though the G-buffer is
    // view-space -- the GL path does the same, so the (camera-locked)
    // lighting matches it exactly.
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
    const bool aoOn = host.lastAO.valid();
    float params[4] = {0.0f, aoOn ? 1.0f : 0.0f, 0.0f, 0.0f};
    surface->setUniform(mRUniParams, params);
    surface->setTexture(0, mRSampColor, host.colTexture);
    surface->setTexture(1, mRSampPosition, host.posTexture);
    surface->setTexture(2, mRSampNormal, host.normTexture);
    // With AO off the shader branches away from the sample, but the
    // slot must still hold a valid texture on every backend.
    surface->setTexture(3, mRSampAo, aoOn ? host.lastAO : host.colTexture);
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

void SimDisplay::RunAOFacade(Render::DrawSurface* surface, bool enabled, bool recalculate)
{
    auto& host = current();
    if (!enabled || !surface || !host.effectAO.valid()
        || !host.normalZTexture.valid()) {
        host.lastAO = {};
        return;
    }
    if (!recalculate && host.lastAO.valid()) {
        return;
    }
    Render::EffectParams params;
    // The engine's automatic radius is 5% of the scene bounding-sphere
    // size; the stock's largest dimension stands in for it here.
    params.radius = 0.05f * 1.7320508f * mMaxStockDimension;
    params.proj = &host.projMat[0][0];
    host.lastAO = surface->runEffect(host.effectAO, SimPassAOFirst,
                                     host.normalZTexture, params);
}

void SimDisplay::ConfigureFacadeFrame(Render::DrawSurface* surface, const vec3& bgnd)
{
    auto& host = current();
    if (!surface || !host.target.valid()) {
        return;
    }
    for (unsigned p = SimPassScene; p <= SimPassBaseShape; p++) {
        surface->setPassTarget(p, host.target);
        surface->setPassRect(p, 0, 0, host.width, host.height);
        // The CSG depends on draws landing in submission order.
        surface->setPassSequential(p, true);
    }
    // The G-buffer clear; it runs only when the pass draws, so a
    // cached frame keeps the buffer.
    surface->setPassClear(SimPassScene, 0x00000000, 1.0f, 0,
                          Render::ClearColor | Render::ClearDepth
                              | Render::ClearStencil);
    surface->setPassClear(SimPassBaseShape, 0, 1.0f, 0, Render::ClearNone);
    const float* view = &host.matLookAt[0][0];
    surface->setPassTransform(SimPassScene, view, &host.projMat[0][0]);
    // The base shape's glPolygonOffset(0, -2) became this pass's
    // slightly-closer projection -- the trick the GL path's (unused)
    // GeomCloser shader carried.
    mat4x4 biased;
    mat4x4_dup(biased, host.projMat);
    biased[2][2] *= 0.99999f;
    surface->setPassTransform(SimPassBaseShape, view, &biased[0][0]);
    // The tool-path passes: the overlay run, drawn after the
    // composite into its destination against the depth it wrote
    // (docs/CAMSimRenderPort.md sec 10.4). Their camera is the
    // TARGET's -- the host's when attached, the sim's own when not --
    // matching the space the composite's depth is in.
    float pathView[16];
    float pathProj[16];
    if (!surface->hostCamera(pathView, pathProj)) {
        std::memcpy(pathView, &host.matLookAt[0][0], sizeof(pathView));
        std::memcpy(pathProj, &host.projMat[0][0], sizeof(pathProj));
    }
    for (unsigned p = SimPassPathVisible; p <= SimPassPathHidden; p++) {
        surface->setPassTarget(p, surface->hostTarget());
        surface->setPassRect(p, 0, 0, host.width, host.height);
        surface->setPassSequential(p, false);
        surface->setPassClear(p, 0, 1.0f, 0, Render::ClearNone);
        surface->setPassTransform(p, pathView, pathProj);
    }
    // The resolve lands in the sim's own colour target, cleared fully
    // transparent so that the G-buffer's coverage survives as alpha
    // for the composite to blend with.
    surface->setPassTarget(SimPassResolve, host.resolveTarget);
    surface->setPassRect(SimPassResolve, 0, 0, host.width, host.height);
    surface->setPassSequential(SimPassResolve, false);
    surface->setPassClear(SimPassResolve, 0x00000000, 1.0f, 0,
                          Render::ClearColor);

    // The composite draws into whatever this surface composites into:
    // its own backbuffer standalone, the host's scene target attached
    // (docs/CAMSimRenderPort.md sec 8.4). hostTarget() answers both --
    // it is the invalid handle, i.e. "my backbuffer", when there is no
    // host.
    surface->setPassTarget(SimPassComposite, surface->hostTarget());
    surface->setPassRect(SimPassComposite, 0, 0, host.width, host.height);
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
    if (gSimDraw.legacyGL) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDepthFunc(isHidden ? GL_GREATER : GL_LESS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2);
        shaderLinePath.Activate();
        shaderLinePath.UpdateObjColorAlpha(pathLineColor);
        shaderLinePath.UpdateObjColor(pathLineColorPassed);
        shaderLinePath.UpdateCurSegment(curSegment);
        shaderLinePath.UpdateViewMat(current().matLookAt);
    }

    // Facade: an overlay-run pass over the composited image. The
    // destination holds LINEAR light when the host's scene target
    // does, and the line colours are display-space constants -- the
    // shader decodes them on u_simParams.x, like the composite
    // decodes its image (docs/CAMSimRenderPort.md sec 10.4).
    gSimDraw.params[0] =
        (gSimDraw.surface && gSimDraw.surface->hostLinearColor()) ? 1.0f
                                                                  : 0.0f;
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
    auto& host = current();
    if (!displayInitiated || (width == host.width && height == host.height)) {
        return;
    }

    host.width = width;
    host.height = height;

    if (mFbo != 0 && gSimDraw.legacyGL) {
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
    }
    if (mFbo != 0 || host.target.valid()) {
        CleanFbos();
    }

    CreateDisplayFbos();
    CreateSsaoFbos();
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
    auto& host = current();

    const SbVec3f position = camera.position.getValue();
    const SbRotation orientation = camera.orientation.getValue();

    if (position == host.cameraPosition && orientation == host.cameraOrientation) {
        return;
    }

    host.cameraPosition = position;
    host.cameraOrientation = orientation;

    UpdateViewMatrix();
}

void SimDisplay::UpdateCameraProjection(const SoCamera& camera)
{
    auto& host = current();
    float heightAngle = std::numbers::pi / 4;
    float height = 100.0f;

    const auto perspective = dynamic_cast<const SoPerspectiveCamera*>(&camera);
    const auto orthographic = dynamic_cast<const SoOrthographicCamera*>(&camera);

    // TODO: We can't use the values from the camera here because the dummy viewer never actually
    // renders the scene and therefore the nearDistance and farDistance of the camera are never
    // updated. Figure out a way to update those values without rendering the scene.

#if 0

    const float nearDistance = camera.nearDistance.getValue();
    const float farDistance = camera.farDistance.getValue();

#else

    float nearDistance = 0.0;
    float farDistance = 0.0;

#endif

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

    if ((bool)perspective == host.cameraPerspective && heightAngle == host.cameraHeightAngle
        && height == host.cameraHeight && nearDistance == host.cameraNearDistance
        && farDistance == host.cameraFarDistance) {
        return;
    }

    host.cameraPerspective = (bool)perspective;
    host.cameraHeightAngle = heightAngle;
    host.cameraHeight = height;
    host.cameraNearDistance = nearDistance;
    host.cameraFarDistance = farDistance;

    UpdateProjectionMatrix();
}

void SimDisplay::UpdateViewMatrix()
{
    auto& host = current();

    SbVec3f up(0, 1, 0);
    host.cameraOrientation.multVec(up, up);

    SbVec3f dir(0, 0, -1);
    host.cameraOrientation.multVec(dir, dir);

    const auto target = host.cameraPosition + dir;
    mat4x4_look_at(
        host.matLookAt, host.cameraPosition.getValue(), target.getValue(), up.getValue());

    // The camera moved for THIS host: only its cached carve is stale.
    host.updateDisplay = true;
}

void SimDisplay::UpdateProjectionMatrix()
{
    // Setup projection

    auto& host = current();
    const float aspect = (float)host.width / host.height;

    mat4x4 projmat;

    if (host.cameraPerspective) {
        mat4x4_perspective(
            projmat, host.cameraHeightAngle, aspect, host.cameraNearDistance,
            host.cameraFarDistance);
    }
    else {
        const float h = host.cameraHeight;
        const float w = host.cameraHeight * aspect;
        mat4x4_ortho(
            projmat, -w / 2, w / 2, -h / 2, h / 2, host.cameraNearDistance,
            host.cameraFarDistance);
    }

    // Kept for the facade frame driver: passes take their projection
    // at frame time (setPassTransform), not through a shader uniform.
    mat4x4_dup(host.projMat, projmat);

    if (gSimDraw.legacyGL) {
        shader3D.Activate();
        shader3D.UpdateProjectionMat(projmat);
        shaderInv3D.Activate();
        shaderInv3D.UpdateProjectionMat(projmat);
        shaderFlat.Activate();
        shaderFlat.UpdateProjectionMat(projmat);
        shaderGeom.Activate();
        shaderGeom.UpdateProjectionMat(projmat);
        shaderSSAO.Activate();
        shaderSSAO.UpdateProjectionMat(projmat);
        shaderLinePath.Activate();
        shaderLinePath.UpdateProjectionMat(projmat);

        projmat[2][2] *= 0.99999F;
        shaderGeomCloser.Activate();
        shaderGeomCloser.UpdateProjectionMat(projmat);
    }

    host.updateDisplay = true;
}

}  // namespace CAMSimulator
