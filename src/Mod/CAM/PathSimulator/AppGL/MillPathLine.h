// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <vector>

#include <Gui/Renderer/DrawDevice.h>

namespace CAMSimulator
{

struct MillPathPosition
{
    float X, Y, Z;
    int SegmentId;
};

class MillPathLine
{
public:
    void GenerateModel();
    void SetupVertexAttibs();
    void Clear();
    void Render();

public:
    std::vector<MillPathPosition> MillPathPointsBuffer;

protected:
    unsigned int mVbo = 0;
    int mNumVerts;
    // The path as a facade buffer (docs/CAMSimRenderPort.md step 3):
    // pos3 + the SegmentId as one float in TexCoord0 -- the backend
    // has no 32-bit integer attribute (exact below 2^24 segments).
    Render::VertexBufferHandle mRVbo;
};

}  // namespace CAMSimulator
