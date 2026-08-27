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

#include "GCodeParser.h"
#include "MillPathLine.h"
#include "MillPathSegment.h"
#include "SimDisplay.h"
#include "SolidObject.h"
#include "StockObject.h"
#include "linmath.h"
#include <chrono>
#include <vector>

#define VIEWITEM_SIMULATION 1
#define VIEWITEM_BASE_SHAPE 2
#define VIEWITEM_MAX 4

namespace CAMSimulator
{

struct MillSimulationState
{
    int mCurStep = 0;
    int mNTotalSteps = 0;
    int mPathStep = -1;
    int mSubStep = 0;
    int mNPathSteps = 0;
    int mSimSpeed = 1;
    int mViewItems = VIEWITEM_SIMULATION;
    bool mViewPath = false;
    bool mViewSSAO = false;

    bool mSimPlaying = false;
    bool mSingleStep = false;
};

/// Where the simulation stands, for an outside observer -- the
/// stop-swap driver reading the GL sim's position to replay it
/// through the volumetric one (docs/CAMSimRenderPort.md 11.7.2).
struct SimProgress
{
    bool playing = false;
    /// Index into the parsed motion list (GCodeParser::Operations)
    /// of the motion being cut; -1 before the first cut.
    int motionIndex = -1;
    /// Consumed fraction of that motion, in (0, 1].
    float fraction = 0.0f;
};

class MillSimulation: private MillSimulationState
{
    typedef std::chrono::steady_clock clock;

public:
    MillSimulation();
    ~MillSimulation();
    void ClearMillPathSegments();
    void Clear();
    void InitSimulation(float quality, float maxStockDimension);
    void AddTool(EndMill* tool);
    void AddTool(const std::vector<float>& toolProfile, int toolid, float diameter);
    bool ToolExists(int toolid);
    void RenderSimulation();
    void RenderTool();
    void RenderPath();
    void RenderBaseShape();
    void Render();
    void ProcessSim(const clock::duration& elapsed);
    void SimNext(const clock::duration& elapsed);

    bool LoadGCodeFile(const char* fileName);
    bool AddGcodeLine(const char* line);

    SimProgress GetProgress() const;
    /// Motions parsed per g-code line: entry i is Operations.size()
    /// after line i was added, so a motion index maps back to the
    /// line -- and through the caller's own feed order, to the Path
    /// command -- it came from. Empty for LoadGCodeFile input.
    const std::vector<int>& GetLineTable() const
    {
        return mLineMotionEnd;
    }
    /// The parsed motion at \a index, or null when out of range.
    const MillMotion* GetMotion(int index) const;

    void SetPlaying(bool b);
    void SingleStep();
    void SetSpeed(int s);

    void SetSimulationStage(float stage);
    void SetState(const MillSimulationState& state);
    const MillSimulationState& GetState() const;

    void SetBoxStock(float x, float y, float z, float l, float w, float h);
    void SetArbitraryStock(const std::vector<Vertex>& verts, const std::vector<GLushort>& indices);
    void SetStockVisible(bool b);
    bool IsStockVisible() const;

    void SetBaseObject(const std::vector<Vertex>& verts, const std::vector<GLushort>& indices);
    void SetBaseVisible(bool b);
    bool IsBaseVisible() const;
    /// The base shape is ordinary document geometry, so while the
    /// simulator borrows a host frame the engine draws it -- lit like
    /// the rest of the document, and sorted against the carved stock
    /// through the shared depth buffer. The simulator must then not
    /// draw it a second time. Visibility is unaffected: this says WHO
    /// draws the base shape, IsBaseVisible says WHETHER.
    void SetBaseDrawnByHost(bool b);

    void SetPathVisible(bool b);
    void EnableSsao(bool b);

    void UpdateWindowScale(int width, int height);
    void UpdateCamera(const SoCamera& camera);

    void SetBackgroundColor(const vec3& c);
    void SetPathColor(const vec3& normal, const vec3& rapid);

protected:
    void InitDisplay(float quality);
    void GlsimStart();
    void GlsimToolStep1(void);
    void GlsimToolStep2(void);
    void GlsimClipBack(void);
    void GlsimRenderStock(void);
    void GlsimRenderTools(void);
    void GlsimEnd(void);
    void renderSegmentForward(int iSeg);
    void renderSegmentReversed(int iSeg);
    void CalcSegmentPositions();
    EndMill* GetTool(int tool);
    void RemoveTool(int toolId);

protected:
    bool simulationInitiated = false;
    /// Set while the host draws the base shape (SetBaseDrawnByHost).
    bool mBaseDrawnByHost = false;

    // protected:
public:
    std::vector<EndMill*> mToolTable;
    GCodeParser mCodeParser;
    std::vector<int> mLineMotionEnd;
    SimDisplay simDisplay;
    MillPathLine millPathLine;
    std::vector<MillPathSegment*> MillPathSegments;


    StockObject mStockObject;
    SolidObject mBaseShape;

    vec3 bgndColor = {0.1f, 0.2f, 0.3f};
    vec3 stockColor = {0.5f, 0.55f, 0.9f};
    vec3 cutColor = {0.5f, 0.84f, 0.73f};
    vec3 toolColor = {0.5f, 0.4f, 0.3f};
    vec3 baseShapeColor = {0.7f, 0.6f, 0.5f};

    clock::duration mTotalElapsed;
};

}  // namespace CAMSimulator
