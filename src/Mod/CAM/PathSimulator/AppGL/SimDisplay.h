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

#include "StockObject.h"
#include <Inventor/SbRotation.h>
#include <Inventor/SbVec3f.h>
#include <numbers>

class SoCamera;
class SoPerspectiveCamera;

namespace CAMSimulator
{

/// The simulator's display resources and pass setups, entirely on the
/// draw facade (docs/CAMSimRenderPort.md): the MRT G-buffer the CSG
/// renders into, the programs and uniforms from the compiled shader
/// pack, the AO effect instance, and the deferred lighting resolve.
class SimDisplay
{
public:
    ~SimDisplay();
    void InitGL();
    void CleanGL();
    void CleanFbos();
    void PrepareFrameBuffer();
    void StartDepthPass();
    void StartGeometryPass(const vec3& objColor, bool invertNormals);
    void ScaleViewToStock(StockObject* obj);
    // The deferred lighting resolve: one quad submitted into `pass`,
    // sampling the G-buffer and the last AO result (RunAOFacade).
    void RenderResultFacade(Render::DrawSurface* surface, unsigned pass);
    // The composite: the resolve's image blended into whatever the
    // surface composites into (SimPassComposite).
    void RenderCompositeFacade(Render::DrawSurface* surface, unsigned pass);
    // The AO effect run: hands the G-buffer's prepass attachment to
    // the engine's AO service in the SimPassAOFirst range and keeps
    // the result for the resolve. On a cached frame (recalculate
    // false) the previous result stands -- the effect's targets
    // persist between frames.
    void RunAOFacade(Render::DrawSurface* surface, bool enabled, bool recalculate);
    // Per-frame pass configuration for the facade frame: targets,
    // clears, ordering and transforms of the passes (SimDrawContext.h).
    // Called by the frame driver between beginFrame and the draws.
    void ConfigureFacadeFrame(Render::DrawSurface* surface, const vec3& bgnd);
    void SetupLinePathPass(int curSegment, bool isHidden);
    void UpdateWindowScale(int width, int height);
    void UpdateCamera(const SoCamera& camera);

    void SetPathColor(const vec3& normal, const vec3& rapid);

public:
    bool updateDisplay = false;
    bool displayInitiated = false;

protected:
    void InitShaders();
    void CreateDisplayFbos();

private:
    void UpdateCameraView(const SoCamera& camera);
    void UpdateCameraProjection(const SoCamera& camera);

    void UpdateViewMatrix();
    void UpdateProjectionMatrix();

protected:
    vec3 lightColor = {0.5f, 0.6f, 0.7f};
    vec3 lightPos = {20.0f, 20.0f, 10.0f};
    vec3 ambientCol = {0.2f, 0.2f, 0.25f};
    vec4 pathLineColor = {0.0f, 0.9f, 0.0f, 1.0};
    vec3 pathLineColorPassed = {0.9f, 0.3f, 0.3f};

    mat4x4 mMatLookAt;
    // The projection the facade frame driver reads at frame time
    // (setPassTransform).
    mat4x4 mProjMat;

    int mWidth = -1;
    int mHeight = -1;

    bool mCameraPerspective = true;
    float mCameraHeightAngle = std::numbers::pi / 4;
    float mCameraHeight = 100.0f;
    float mCameraNearDistance = 1.0f;
    float mCameraFarDistance = 100.0f;
    float mMaxStockDimension = 100.0f;

    SbVec3f mCameraPosition;
    SbRotation mCameraOrientation;

    // The facade display set: the G-buffer (colour, view-space
    // position, view-space normal, the AO prepass packing, D24S8),
    // the fullscreen quad, the programs from the compiled pack, the
    // uniform/sampler set and the AO effect instance.
    Render::VertexBufferHandle mRQuadVbo;
    Render::TextureHandle mRColTexture;
    Render::TextureHandle mRPosTexture;
    Render::TextureHandle mRNormTexture;
    // The engine prepass packing (oct normal + linear view depth) the
    // AO effect reads; written by the geometry pass as attachment 3.
    Render::TextureHandle mRNormalZTexture;
    Render::TextureHandle mRDepthTexture;
    Render::TargetHandle mRTarget;
    // Colour + depth only, sharing the G-buffer's attachments: the
    // path-line pass draws here so a program with one output cannot
    // scribble undefined values into the position/normal/prepass
    // attachments (an AO streak along the rapid lines, found the
    // moment the effect first ran).
    Render::TargetHandle mRPathTarget;
    // The resolve's own output, and the target that holds it. The
    // deferred resolve lands here rather than straight in the
    // surface's composite target, so that the only texture the
    // composite pass carries across is this one RGBA8 image -- see
    // SimPassComposite and docs/CAMSimRenderPort.md sec 8.4.
    Render::TextureHandle mRResolveTexture;
    Render::TargetHandle mRResolveTarget;
    Render::EffectHandle mREffectAO;
    // The last AO run's result; invalid = no AO for the resolve.
    Render::TextureHandle mRLastAO;
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
};

}  // namespace CAMSimulator
