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
    void StartCloserGeometryPass(const vec3& objColor);
    void RenderLightObject();
    void ScaleViewToStock(StockObject* obj);
    void RenderResult(bool recalculate, bool ssao);
    void RenderResultStandard();
    void RenderResultSSAO(bool recalculate);
    // The facade deferred resolve (docs/CAMSimRenderPort.md step 4):
    // the lighting quad submitted into one pass of the given surface.
    // Dormant until step 6's frame driver hands a surface; AO stays
    // off until the effect service (step 7) supplies its texture.
    void RenderResultFacade(Render::DrawSurface* surface, unsigned pass);
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

    mat4x4 mMatLookAt;
    StockObject mlightObject;

    int mWidth = -1;
    int mHeight = -1;

    std::mt19937 generator;
    std::uniform_real_distribution<float> distr01;

    bool mCameraPerspective = true;
    float mCameraHeightAngle = std::numbers::pi / 4;
    float mCameraHeight = 100.0f;
    float mCameraNearDistance = 1.0f;
    float mCameraFarDistance = 100.0f;
    float mMaxStockDimension = 100.0f;

    SbVec3f mCameraPosition;
    SbRotation mCameraOrientation;

    // base frame buffer
    unsigned int mFbo = 0;
    unsigned int mFboColTexture = 0;
    unsigned int mFboPosTexture = 0;
    unsigned int mFboNormTexture = 0;
    unsigned int mRboDepthStencil = 0;
    unsigned int mFboQuadVBO = 0;

    // The facade side (docs/CAMSimRenderPort.md step 4): the same
    // G-buffer, quad and programs as facade resources. The GL pair of
    // each leaves with the last step of the port. The SSAO chain has
    // no facade counterpart -- the engine's AO effect replaces it.
    Render::VertexBufferHandle mRQuadVbo;
    Render::TextureHandle mRColTexture;
    Render::TextureHandle mRPosTexture;
    Render::TextureHandle mRNormTexture;
    Render::TextureHandle mRDepthTexture;
    Render::TargetHandle mRTarget;
    Render::ProgramHandle mRProgDiffuse;
    Render::ProgramHandle mRProgInvDiffuse;
    Render::ProgramHandle mRProgFlat;
    Render::ProgramHandle mRProgGeom;
    Render::ProgramHandle mRProgLighting;
    Render::ProgramHandle mRProgLine;
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

    // ssao frame buffers
    bool mSsaoValid = false;
    std::vector<Point3D> mSsaoKernel;
    unsigned int mSsaoFbo = 0;
    unsigned int mSsaoBlurFbo = 0;
    unsigned int mFboSsaoTexture = 0;
    unsigned int mFboSsaoBlurTexture = 0;
    unsigned int mFboRandTexture = 0;
};

}  // namespace CAMSimulator
