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

#include "MillSimulation.h"

#include "GlUtils.h"
#include "SimDrawContext.h"
#include <algorithm>
#include <iostream>


using namespace std::literals;

#define DRAG_ZOOM_FACTOR 10

namespace CAMSimulator
{

MillSimulation::MillSimulation()
{}

MillSimulation::~MillSimulation()
{
    Clear();
}

void MillSimulation::ClearMillPathSegments()
{
    for (unsigned int i = 0; i < MillPathSegments.size(); i++) {
        delete MillPathSegments[i];
    }
    MillPathSegments.clear();
}

void MillSimulation::Clear()
{
    mCodeParser.Clear();

    ClearMillPathSegments();

    for (unsigned int i = 0; i < mToolTable.size(); i++) {
        delete mToolTable[i];
    }
    mToolTable.clear();

    mStockObject.Clear();
    simDisplay.CleanGL();

    mCurStep = 0;
    mPathStep = -1;
    mNTotalSteps = 0;

    simulationInitiated = false;
}

void MillSimulation::InitSimulation(float quality, float maxStockDimension)
{
    ClearMillPathSegments();
    millPathLine.Clear();
    // mViewSSAO = guiDisplay.IsChecked(eGuiItemAmbientOclusion);

    // gDestPos = curMillOperation->startPos;
    mCurStep = 0;
    mPathStep = -1;
    mNTotalSteps = 0;
    mSimPlaying = false;
    mSimSpeed = 1;
    mTotalElapsed = 0s;

    MillPathSegment::SetQuality(quality, maxStockDimension);

    int nOperations = (int)mCodeParser.Operations.size();
    int segId = 0;

    MillMotion prevMotion = !mCodeParser.Operations.empty() ? mCodeParser.Operations.front()
                                                            : MillMotion();

    MillPathPosition mpPos;
    mpPos.X = prevMotion.x;
    mpPos.Y = prevMotion.y;
    mpPos.Z = prevMotion.z;
    mpPos.SegmentId = segId++;
    millPathLine.MillPathPointsBuffer.push_back(mpPos);

    for (int i = 1; i < nOperations; i++) {
        const MillMotion curMotion = mCodeParser.Operations[i];
        const EndMill* tool = GetTool(curMotion.tool);
        if (tool != nullptr) {
            auto segment = new MillPathSegment(*tool, prevMotion, curMotion);
            segment->indexInArray = i;
            segment->segmentIndex = segId++;
            mNTotalSteps += segment->numSimSteps;
            MillPathSegments.push_back(segment);
            segment->AppendPathPoints(millPathLine.MillPathPointsBuffer);
        }

        prevMotion = curMotion;
    }

    assert(mNTotalSteps >= 0);

    mNPathSteps = (int)MillPathSegments.size();
    millPathLine.GenerateModel();

    InitDisplay(quality);

    simulationInitiated = true;
}

EndMill* MillSimulation::GetTool(int toolId)
{
    for (unsigned int i = 0; i < mToolTable.size(); i++) {
        if (mToolTable[i]->toolId == toolId) {
            return mToolTable[i];
        }
    }
    return nullptr;
}

void MillSimulation::RemoveTool(const int toolId)
{
    EndMill* tool = GetTool(toolId);
    if (tool == nullptr) {
        return;
    }

    if (const auto it = std::ranges::find(mToolTable, tool); it != mToolTable.end()) {
        mToolTable.erase(it);
    }
    delete tool;
}


void MillSimulation::AddTool(EndMill* tool)
{
    // if we have another tool with same id, remove it
    RemoveTool(tool->toolId);
    mToolTable.push_back(tool);
}

void MillSimulation::AddTool(const std::vector<float>& toolProfile, int toolid, float diameter)
{
    // if we have another tool with same id, remove it
    RemoveTool(toolid);
    EndMill* tool = new EndMill(toolProfile, toolid, diameter);
    mToolTable.push_back(tool);
}

bool MillSimulation::ToolExists(int toolid)
{
    return GetTool(toolid) != nullptr;
}

void MillSimulation::GlsimStart()
{
    gSimDraw.state.blend = Render::BlendMode::None;
    gSimDraw.state.colorWrite = false;
    gSimDraw.state.alphaWrite = false;
    gSimDraw.stencil.enabled = true;
}

void MillSimulation::GlsimToolStep1(void)
{
    gSimDraw.cullFace = Render::CullMode::Back;
    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = false;
    gSimDraw.stencil.func = Render::CompareFunc::Always;
    gSimDraw.stencil.ref = 1;
    gSimDraw.stencil.readMask = 0xff;
    gSimDraw.stencil.stencilFail = Render::StencilOp::Zero;
    gSimDraw.stencil.depthFail = Render::StencilOp::Zero;
    gSimDraw.stencil.depthPass = Render::StencilOp::Replace;
}

void MillSimulation::GlsimToolStep2(void)
{
    gSimDraw.cullFace = Render::CullMode::Front;
    gSimDraw.state.depthFunc = Render::CompareFunc::Greater;
    gSimDraw.state.depthWrite = true;
    gSimDraw.stencil.func = Render::CompareFunc::Equal;
    gSimDraw.stencil.ref = 1;
    gSimDraw.stencil.readMask = 0xff;
    gSimDraw.stencil.stencilFail = Render::StencilOp::Keep;
    gSimDraw.stencil.depthFail = Render::StencilOp::Keep;
    gSimDraw.stencil.depthPass = Render::StencilOp::Keep;
}

void MillSimulation::GlsimClipBack(void)
{
    gSimDraw.cullFace = Render::CullMode::Front;
    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = false;
    gSimDraw.stencil.func = Render::CompareFunc::Always;
    gSimDraw.stencil.ref = 1;
    gSimDraw.stencil.readMask = 0xff;
    gSimDraw.stencil.stencilFail = Render::StencilOp::Replace;
    gSimDraw.stencil.depthFail = Render::StencilOp::Replace;
    gSimDraw.stencil.depthPass = Render::StencilOp::Zero;
}

void MillSimulation::GlsimRenderStock(void)
{
    gSimDraw.cullFace = Render::CullMode::Back;
    gSimDraw.state.colorWrite = true;
    gSimDraw.state.alphaWrite = true;
    gSimDraw.state.depthFunc = Render::CompareFunc::Equal;
    gSimDraw.stencil.enabled = true;
    gSimDraw.stencil.func = Render::CompareFunc::Equal;
    gSimDraw.stencil.ref = 1;
    gSimDraw.stencil.readMask = 0xff;
    gSimDraw.stencil.stencilFail = Render::StencilOp::Keep;
    gSimDraw.stencil.depthFail = Render::StencilOp::Keep;
    gSimDraw.stencil.depthPass = Render::StencilOp::Keep;
}

void MillSimulation::GlsimRenderTools(void)
{
    gSimDraw.cullFace = Render::CullMode::Front;
}

void MillSimulation::GlsimEnd(void)
{
    gSimDraw.cullFace = Render::CullMode::Back;
    gSimDraw.state.colorWrite = true;
    gSimDraw.state.alphaWrite = true;
    gSimDraw.state.depthFunc = Render::CompareFunc::Less;
    gSimDraw.state.depthWrite = true;
    gSimDraw.stencil = {};
}

void MillSimulation::renderSegmentForward(int iSeg)
{
    MillPathSegment* p = MillPathSegments.at(iSeg);
    int step = iSeg == mPathStep ? mSubStep : p->numSimSteps;
    int start = p->isMultyPart ? 1 : step;
    for (int i = start; i <= step; i++) {
        GlsimToolStep1();
        p->render(i);
        GlsimToolStep2();
        p->render(i);
    }
}

void MillSimulation::renderSegmentReversed(int iSeg)
{
    MillPathSegment* p = MillPathSegments.at(iSeg);
    int step = iSeg == mPathStep ? mSubStep : p->numSimSteps;
    int end = p->isMultyPart ? 1 : step;
    for (int i = step; i >= end; i--) {
        GlsimToolStep1();
        p->render(i);
        GlsimToolStep2();
        p->render(i);
    }
}

void MillSimulation::CalcSegmentPositions()
{
    mSubStep = mCurStep;
    for (mPathStep = 0; mPathStep < mNPathSteps; mPathStep++) {
        MillPathSegment* p = MillPathSegments[mPathStep];
        if (mSubStep < p->numSimSteps) {
            break;
        }
        mSubStep -= p->numSimSteps;
    }
    if (mPathStep >= mNPathSteps) {
        mPathStep = mNPathSteps - 1;
        mSubStep = MillPathSegments[mPathStep]->numSimSteps;
    }
    else {
        mSubStep++;
    }
}

void MillSimulation::RenderSimulation()
{
    if ((mViewItems & VIEWITEM_SIMULATION) == 0) {
        return;
    }

    simDisplay.StartDepthPass();

    GlsimStart();
    mStockObject.render();

    GlsimToolStep2();

    for (int i = 0; i <= mPathStep; i++) {
        renderSegmentForward(i);
    }

    for (int i = mPathStep; i >= 0; i--) {
        renderSegmentForward(i);
    }

    for (int i = 0; i < mPathStep; i++) {
        renderSegmentReversed(i);
    }

    for (int i = mPathStep; i >= 0; i--) {
        renderSegmentReversed(i);
    }

    GlsimClipBack();
    mStockObject.render();

    // start coloring
    simDisplay.StartGeometryPass(stockColor, false);
    GlsimRenderStock();
    mStockObject.render();

    // render cuts (back faces of tools)
    simDisplay.StartGeometryPass(cutColor, true);
    GlsimRenderTools();
    for (int i = 0; i <= mPathStep; i++) {
        MillPathSegment* p = MillPathSegments.at(i);
        int step = (i == mPathStep) ? mSubStep : p->numSimSteps;
        int start = p->isMultyPart ? 1 : step;
        for (int j = start; j <= step; j++) {
            MillPathSegments.at(i)->render(j);
        }
    }

    GlsimEnd();
}

void MillSimulation::RenderTool()
{
    if (mPathStep < 0) {
        return;
    }

    MillPathSegment* p = MillPathSegments.at(mPathStep);
    vec3 toolPos;
    p->GetHeadPosition(toolPos);
    mat4x4 tmat;
    mat4x4_translate(tmat, toolPos[0], toolPos[1], toolPos[2]);
    // mat4x4_translate(tmat, toolPos.x, toolPos.y, toolPos.z);
    simDisplay.StartGeometryPass(toolColor, false);
    p->endmill->toolShape.Render(tmat, identityMat);
}

void MillSimulation::RenderPath()
{
    if (!mViewPath) {
        return;
    }
    gSimDraw.pass = SimPassPath;
    simDisplay.SetupLinePathPass(mPathStep, false);
    millPathLine.Render();
    simDisplay.SetupLinePathPass(mPathStep, true);
    millPathLine.Render();
    gSimDraw.state.depthWrite = true;
    gSimDraw.pass = SimPassScene;
}

void MillSimulation::RenderBaseShape()
{
    if ((mViewItems & VIEWITEM_BASE_SHAPE) == 0 || mBaseDrawnByHost) {
        return;
    }
    simDisplay.StartDepthPass();
    simDisplay.StartGeometryPass(baseShapeColor, false);
    // The depth-biased projection pass substitutes the GL path's
    // glPolygonOffset (SimPassBaseShape); backend passes run in id
    // order, so the draw still lands after the CSG.
    gSimDraw.pass = SimPassBaseShape;
    mBaseShape.render();
    gSimDraw.pass = SimPassScene;
}

void MillSimulation::Render()
{
    if (!simulationInitiated) {
        return;
    }

    // Without an active facade frame (no backend device) there is
    // nothing to draw into.
    if (!gSimDraw.active()) {
        return;
    }

    // The CSG renders into the G-buffer only when something changed;
    // a cached frame goes straight to the resolve, which reads the
    // preserved targets.
    const bool recalculated = simDisplay.updateDisplay;
    if (recalculated) {
        simDisplay.PrepareFrameBuffer();
        RenderSimulation();
        RenderTool();
        RenderBaseShape();
        RenderPath();
        simDisplay.updateDisplay = false;
    }

    simDisplay.RunAOFacade(gSimDraw.surface, mViewSSAO, recalculated);
    simDisplay.RenderResultFacade(gSimDraw.surface, SimPassResolve);
    simDisplay.RenderCompositeFacade(gSimDraw.surface, SimPassComposite);

    /*   if (mDebug > 0) {
           mat4x4 test;
           mat4x4_identity(test);
           mat4x4_translate_in_place(test, 20, 20, 3);
           mat4x4_rotate_Z(test, test, 30.f * 3.14f / 180.f);
           int dpos = mNPathSteps - mDebug2;
           MillPathSegment* p = MillPathSegments.at(dpos);
           if (mDebug > p->numSimSteps) {
               mDebug = 1;
           }
           p->render(mDebug);
       }*/
}

void MillSimulation::ProcessSim(const clock::duration& elapsed)
{
    SimNext(elapsed);
    Render();
}

void MillSimulation::SimNext(const clock::duration& elapsed)
{
    // calculate number of steps based on elapsed time

    int numSteps = 0;

    if (mSimPlaying) {
        mTotalElapsed += elapsed;

        const float seconds
            = std::chrono::duration_cast<std::chrono::duration<float>>(mTotalElapsed).count();

        const float secondsToSteps = mSimSpeed * 60;
        numSteps = seconds * secondsToSteps;

        const std::chrono::duration<float> processed {(float)numSteps / secondsToSteps};
        mTotalElapsed -= std::chrono::duration_cast<clock::duration>(processed);
    }
    else if (mSingleStep) {
        numSteps = 1;

        mTotalElapsed = 0s;
        mSingleStep = false;
    }
    else {
        return;
    }

    // advance simulation

    const int oldStep = mCurStep;

    mCurStep += numSteps;
    mCurStep = std::clamp(mCurStep, 0, mNTotalSteps);
    if (mCurStep == mNTotalSteps) {
        mSimPlaying = false;
    }

    if (mCurStep != oldStep) {
        CalcSegmentPositions();
        simDisplay.updateDisplay = true;
    }
}

void MillSimulation::InitDisplay(float quality)
{
    // generate tools
    for (unsigned int i = 0; i < mToolTable.size(); i++) {
        mToolTable[i]->GenerateDisplayLists(quality);
    }

    // Make sure the next call to UpdateWindowScale will not return early.
    mWidth = -1;
    mHeight = -1;

    // init 3d display
    simDisplay.InitGL();
}

void MillSimulation::SetBoxStock(float x, float y, float z, float l, float w, float h)
{
    mStockObject.GenerateBoxStock(x, y, z, l, w, h);
    simDisplay.ScaleViewToStock(&mStockObject);
}

void MillSimulation::SetArbitraryStock(
    const std::vector<Vertex>& verts,
    const std::vector<uint16_t>& indices
)
{
    mStockObject.GenerateSolid(verts, indices);
    simDisplay.ScaleViewToStock(&mStockObject);
}

void MillSimulation::SetStockVisible(bool b)
{
    if (b == IsStockVisible()) {
        return;
    }

    mViewItems ^= VIEWITEM_SIMULATION;
    simDisplay.updateDisplay = true;
}

bool MillSimulation::IsStockVisible() const
{
    return mViewItems & VIEWITEM_SIMULATION;
}

void MillSimulation::SetBaseObject(const std::vector<Vertex>& verts, const std::vector<uint16_t>& indices)
{
    mBaseShape.GenerateSolid(verts, indices);
}

void MillSimulation::SetBaseVisible(bool b)
{
    if (b == IsBaseVisible()) {
        return;
    }

    mViewItems ^= VIEWITEM_BASE_SHAPE;
    simDisplay.updateDisplay = true;
}

bool MillSimulation::IsBaseVisible() const
{
    return mViewItems & VIEWITEM_BASE_SHAPE;
}

void MillSimulation::SetBaseDrawnByHost(bool b)
{
    if (b == mBaseDrawnByHost) {
        return;
    }
    mBaseDrawnByHost = b;
    // The base shape is in the G-buffer the CSG leaves behind, so the
    // cached frame has to be rebuilt to add or drop it.
    simDisplay.updateDisplay = true;
}

void MillSimulation::UpdateWindowScale(int width, int height)
{
    if (width == mWidth && height == mHeight) {
        return;
    }

    mWidth = width;
    mHeight = height;

    simDisplay.UpdateWindowScale(width, height);
}

void MillSimulation::SetPathVisible(bool b)
{
    if (b == mViewPath) {
        return;
    }

    mViewPath = b;
    simDisplay.updateDisplay = true;
}

void MillSimulation::EnableSsao(bool b)
{
    if (b == mViewSSAO) {
        return;
    }

    mViewSSAO = b;
    simDisplay.updateDisplay = true;
}

void MillSimulation::UpdateCamera(const SoCamera& camera)
{
    simDisplay.UpdateCamera(camera);
}

bool MillSimulation::LoadGCodeFile(const char* fileName)
{
    if (mCodeParser.Parse(fileName)) {
        std::cout << "GCode file loaded successfully" << std::endl;
        return true;
    }
    return false;
}

bool MillSimulation::AddGcodeLine(const char* line)
{
    return mCodeParser.AddLine(line);
}

void MillSimulation::SetPlaying(bool b)
{
    if (b == mSimPlaying) {
        return;
    }

    mSimPlaying = b;
    simDisplay.updateDisplay = true;
}

void MillSimulation::SingleStep()
{
    if (!mSimPlaying && mSingleStep) {
        return;
    }

    mSimPlaying = false;
    mSingleStep = true;
    simDisplay.updateDisplay = true;
}

void MillSimulation::SetSpeed(int s)
{
    mSimSpeed = s;
}

void MillSimulation::SetSimulationStage(float stage)
{
    const int newStep = (int)((float)mNTotalSteps * stage);
    if (newStep == mCurStep) {
        return;
    }

    mCurStep = newStep;
    mSingleStep = true;
    CalcSegmentPositions();

    simDisplay.updateDisplay = true;
}

void MillSimulation::SetState(const MillSimulationState& state)
{
    SetPlaying(state.mSimPlaying);
    if (state.mSingleStep) {
        SingleStep();
    }

    const float stage = (float)state.mCurStep / state.mNTotalSteps;
    SetSimulationStage(stage);

    SetSpeed(state.mSimSpeed);

    mViewItems = state.mViewItems;
    mViewPath = state.mViewPath;
    mViewSSAO = state.mViewSSAO;
}

const MillSimulationState& MillSimulation::GetState() const
{
    return *this;
}

void MillSimulation::SetBackgroundColor(const vec3& c)
{
    bgndColor[0] = c[0];
    bgndColor[1] = c[1];
    bgndColor[2] = c[2];
}

void MillSimulation::SetPathColor(const vec3& normal, const vec3& rapid)
{
    simDisplay.SetPathColor(normal, rapid);
}


}  // namespace CAMSimulator
