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

#pragma once

#include <Gui/Renderer/DrawDevice.h>

#include "Shader.h"
#include "StockObject.h"
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec3f.h>
#include <QOpenGLFunctions>
#include <memory>
#include <numbers>
#include <random>
#include <vector>

class SoCamera;
class SoPerspectiveCamera;

namespace CAMSimulator
{

struct Point3D
{
    float x, y, z;
};

/// The per-host render state (docs/CAMSimRenderPort.md sec 11.9):
/// everything the simulator keeps per surface it draws into. The CSG
/// is view-dependent -- the carve is computed in screen space under
/// the host's camera -- so the camera, the size, and the G-buffer
/// that caches the carve for that camera all live here, while
/// MillSimulation's state (segments, tools, step, clock) and the
/// device-wide resources (programs, uniforms, the quad) stay shared
/// in SimDisplay. Exactly one instance until the multi-host stage
/// adds the per-surface list.
struct SimHostContext
{
    /// The surface this context serves, the key SetCurrentHost looks
    /// up. Null for the default context, which serves the legacy GL
    /// path (single-host by nature: it owns the widget). Borrowed;
    /// the owner drops the context (DropHost) before the surface
    /// dies.
    Render::DrawSurface* surface = nullptr;

    int width = -1;
    int height = -1;

    /// The carve cached in the G-buffer is stale for this host's
    /// camera: re-run the CSG on its next frame.
    bool updateDisplay = false;

    mat4x4 matLookAt;
    /// The projection the facade frame driver reads (the GL path
    /// pushes it into each shader instead of keeping it).
    mat4x4 projMat;

    // The camera state the matrices were built from, kept to elide
    // rebuilds when the camera has not moved.
    bool cameraPerspective = true;
    float cameraHeightAngle = std::numbers::pi / 4;
    float cameraHeight = 100.0f;
    float cameraNearDistance = 1.0f;
    float cameraFarDistance = 100.0f;
    SbVec3f cameraPosition;
    SbRotation cameraOrientation;

    // The facade G-buffer (docs/CAMSimRenderPort.md step 4): the same
    // attachments as the legacy GL FBO, as facade resources.
    Render::TextureHandle colTexture;
    Render::TextureHandle posTexture;
    Render::TextureHandle normTexture;
    /// The engine prepass packing (oct normal + linear view depth) the
    /// AO effect reads; written by the geometry pass as attachment 3.
    Render::TextureHandle normalZTexture;
    Render::TextureHandle depthTexture;
    Render::TargetHandle target;
    /// The resolve's own output, and the target that holds it. The
    /// deferred resolve lands here rather than straight in the
    /// surface's composite target, so that the only texture the
    /// composite pass carries across is this one RGBA8 image -- see
    /// SimPassComposite and docs/CAMSimRenderPort.md sec 8.4.
    Render::TextureHandle resolveTexture;
    Render::TargetHandle resolveTarget;
    /// The engine AO effect run for this host; its internal targets
    /// persist between frames, caching alongside the G-buffer.
    Render::EffectHandle effectAO;
    /// The last AO run's result; invalid = no AO for the resolve.
    Render::TextureHandle lastAO;
};

class SimDisplay
{
public:
    SimDisplay();
    ~SimDisplay();

    /// Select -- creating on first sight -- the per-host context for
    /// \a surface; every camera/size/G-buffer operation until the
    /// next call acts on it. Null selects the default context (the
    /// legacy GL path). Called at the top of each frame, before the
    /// frame's resource updates.
    void SetCurrentHost(Render::DrawSurface* surface);
    /// Drop \a surface's context and release its resources, called
    /// before the surface dies -- a host detaching, the standalone
    /// surface resetting. The default context is never dropped.
    void DropHost(Render::DrawSurface* surface);

    void InitGL();
    void CleanGL();
    void CleanFbos();
    void PrepareFrameBuffer();
    void StartDepthPass();
    void StartGeometryPass(const vec3& objColor, bool invertNormals);
    void StartCloserGeometryPass(const vec3& objColor);
    void RenderLightObject();
    void ScaleViewToStock(StockObject* obj);
    void RenderResult(bool recalculate, bool ssao);
    void RenderResultStandard();
    void RenderResultSSAO(bool recalculate);
    // The facade deferred resolve (docs/CAMSimRenderPort.md step 4):
    // the lighting quad submitted into one pass of the given surface.
    // AO stays off until the effect service (step 7) supplies its
    // texture.
    void RenderResultFacade(Render::DrawSurface* surface, unsigned pass);
    // The composite: the resolve's image blended into whatever the
    // surface composites into (SimPassComposite).
    void RenderCompositeFacade(Render::DrawSurface* surface, unsigned pass);
    // The AO effect run (docs/CAMSimRenderPort.md step 7): hands the
    // G-buffer's prepass attachment to the engine's AO service in the
    // SimPassAOFirst range and keeps the result for the resolve. On a
    // cached frame (recalculate false) the previous result stands --
    // the effect's targets persist between frames.
    void RunAOFacade(Render::DrawSurface* surface, bool enabled, bool recalculate);
    // Per-frame pass configuration for the facade frame (step 6):
    // targets, clears, ordering and transforms of the four passes
    // (SimDrawContext.h). Called by the frame driver between
    // beginFrame and the draws.
    void ConfigureFacadeFrame(Render::DrawSurface* surface, const vec3& bgnd);
    void SetupLinePathPass(int curSegment, bool isHidden);
    void UpdateWindowScale(int width, int height);
    void UpdateCamera(const SoCamera& camera);

    void SetPathColor(const vec3& normal, const vec3& rapid);

public:
    bool displayInitiated = false;

    /// Mark every host's cached carve stale: the SIMULATION changed,
    /// as opposed to one host's camera or size, which mark only that
    /// host stale (SimHostContext::updateDisplay).
    void InvalidateDisplay();
    /// Whether the current host's carve must be re-run this frame.
    bool NeedsRecalculate() const;
    /// The current host's cached carve is valid again.
    void ClearRecalculate();

protected:
    void InitShaders();
    void CreateDisplayFbos();
    void CreateSsaoFbos();
    void CreateFboQuad();
    void SetupVertexAttribs() const;
    void CreateGBufTex(GLenum texUnit, GLint intFormat, GLenum format, GLenum type, GLuint& texid);
    void UniformHemisphere(vec3& randVec);
    void UniformCircle(vec3& randVec);

private:
    void UpdateCameraView(const SoCamera& camera);
    void UpdateCameraProjection(const SoCamera& camera);

    void UpdateViewMatrix();
    void UpdateProjectionMatrix();

    SimHostContext& current()
    {
        return *mCurrent;
    }
    const SimHostContext& current() const
    {
        return *mCurrent;
    }
    /// Release \a host's facade G-buffer and resolve resources. Kept:
    /// its AO effect, which survives a resize (its targets follow the
    /// input size on their own).
    void DestroyHostFbos(SimHostContext& host);

    /// The hosts this display serves: front() is the default context
    /// (null surface); per-surface contexts follow, created by
    /// SetCurrentHost and removed by DropHost. unique_ptr so the
    /// references current() hands out stay stable while the list
    /// grows (docs/CAMSimRenderPort.md sec 11.9).
    std::vector<std::unique_ptr<SimHostContext>> mHosts;
    SimHostContext* mCurrent = nullptr;

protected:
    // shaders
    Shader shader3D, shaderInv3D, shaderFlat, shaderSimFbo;
    Shader shaderGeom, shaderSSAO, shaderSSAOLighting, shaderSSAOBlur;
    Shader shaderGeomCloser;
    Shader shaderLinePath;

    vec3 lightColor = {0.5f, 0.6f, 0.7f};
    vec3 lightPos = {20.0f, 20.0f, 10.0f};
    vec3 ambientCol = {0.2f, 0.2f, 0.25f};
    vec4 pathLineColor = {0.0f, 0.9f, 0.0f, 1.0};
    vec3 pathLineColorPassed = {0.9f, 0.3f, 0.3f};

    StockObject mlightObject;

    std::mt19937 generator;
    std::uniform_real_distribution<float> distr01;

    float mMaxStockDimension = 100.0f;

    // base frame buffer
    unsigned int mFbo = 0;
    unsigned int mFboColTexture = 0;
    unsigned int mFboPosTexture = 0;
    unsigned int mFboNormTexture = 0;
    unsigned int mRboDepthStencil = 0;
    unsigned int mFboQuadVBO = 0;

    // The facade side (docs/CAMSimRenderPort.md step 4): the quad
    // and programs as facade resources, shared by every host; the
    // G-buffer and its companions are per host, in SimHostContext.
    // The GL pair of each leaves with the last step of the port. The
    // SSAO chain has no facade counterpart -- the engine's AO effect
    // replaces it.
    Render::VertexBufferHandle mRQuadVbo;
    Render::ProgramHandle mRProgDiffuse;
    Render::ProgramHandle mRProgInvDiffuse;
    Render::ProgramHandle mRProgFlat;
    Render::ProgramHandle mRProgGeom;
    Render::ProgramHandle mRProgLighting;
    Render::ProgramHandle mRProgLine;
    // The fullscreen copy (fs_camsim_fbo) the composite pass draws
    // with, and its parameters: x is set when the destination holds
    // linear light and the image has to be decoded on the way in, y
    // when the destination's depth is shared with a host scene and
    // this pass has to write into it, z for that host's clip-depth
    // convention.
    Render::ProgramHandle mRProgCopy;
    Render::UniformHandle mRUniComposite;
    // Simulator view space -> host clip space, the matrix that places
    // the simulator's image in the host's depth buffer. Only used, and
    // only meaningful, on an attached surface.
    Render::UniformHandle mRUniDepthXform;
    Render::UniformHandle mRUniNormalRot;
    Render::UniformHandle mRUniLightPos;
    Render::UniformHandle mRUniLightColor;
    Render::UniformHandle mRUniLightAmbient;
    Render::UniformHandle mRUniObjectColor;
    Render::UniformHandle mRUniObjectColorAlpha;
    Render::UniformHandle mRUniParams;
    Render::UniformHandle mRSampColor;
    Render::UniformHandle mRSampPosition;
    Render::UniformHandle mRSampNormal;
    Render::UniformHandle mRSampAo;
    Render::UniformHandle mRSampTex;

    // The legacy GL path's own SSAO chain and its frame buffers. The
    // facade path has no counterpart: AO there is the engine's GTAO
    // effect service (RunAOFacade), which is why these are GL-only.
    bool mSsaoValid = false;
    std::vector<Point3D> mSsaoKernel;
    unsigned int mSsaoFbo = 0;
    unsigned int mSsaoBlurFbo = 0;
    unsigned int mFboSsaoTexture = 0;
    unsigned int mFboSsaoBlurTexture = 0;
    unsigned int mFboRandTexture = 0;
};

}  // namespace CAMSimulator
