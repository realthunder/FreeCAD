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
    void Clear();
    void Render();

public:
    std::vector<MillPathPosition> MillPathPointsBuffer;

protected:
    // The path as camera-facing quads (the backend draws only 1px
    // lines, so the GL path's glLineWidth(2) is done in the vertex
    // shader): six vertices per segment, each carrying its endpoint,
    // the other endpoint, and (segment index, screen side). The
    // SegmentId travels as a float -- the backend has no 32-bit
    // integer attribute (exact below 2^24 segments).
    Render::VertexBufferHandle mRVbo;
};

}  // namespace CAMSimulator
