// SPDX-License-Identifier: LGPL-2.1-or-later

#include "MillPathLine.h"

#include "SimDrawContext.h"

namespace CAMSimulator
{

void MillPathLine::GenerateModel()
{
    auto* dev = Render::DrawDevice::instance();
    if (dev) {
        if (mRVbo.valid()) {
            dev->destroy(mRVbo);
        }
        mRVbo = {};
        // Each strip segment becomes one screen-facing quad (two
        // triangles, non-indexed -- 16-bit indices would overflow on
        // long paths). A vertex is: its endpoint (Position), the
        // OTHER endpoint (Normal -- the shader needs both to find the
        // screen direction), and (segment index, side) in TexCoord0.
        // The far endpoint's vertices flip the side sign so both ends
        // offset to the same screen side; vs_camsim_line.sc keeps the
        // story.
        const size_t n = MillPathPointsBuffer.size();
        if (n >= 2) {
            std::vector<float> verts;
            verts.reserve((n - 1) * 6 * 8);
            auto emit = [&verts](const MillPathPosition& at,
                                 const MillPathPosition& other,
                                 float side) {
                verts.push_back(at.X);
                verts.push_back(at.Y);
                verts.push_back(at.Z);
                verts.push_back(other.X);
                verts.push_back(other.Y);
                verts.push_back(other.Z);
                verts.push_back((float)at.SegmentId);
                verts.push_back(side);
            };
            for (size_t i = 0; i + 1 < n; i++) {
                const auto& p0 = MillPathPointsBuffer[i];
                const auto& p1 = MillPathPointsBuffer[i + 1];
                emit(p0, p1, 1.0f);
                emit(p0, p1, -1.0f);
                emit(p1, p0, -1.0f);
                emit(p0, p1, -1.0f);
                emit(p1, p0, 1.0f);
                emit(p1, p0, -1.0f);
            }
            Render::VertexLayout layout;
            layout.add(Render::DrawAttrib::Position, 3, Render::DrawAttribType::Float)
                .add(Render::DrawAttrib::Normal, 3, Render::DrawAttribType::Float)
                .add(Render::DrawAttrib::TexCoord0, 2, Render::DrawAttribType::Float);
            mRVbo = dev->createVertexBuffer(
                verts.data(),
                (unsigned int)(verts.size() * sizeof(float)),
                layout
            );
        }
    }

    // free
    MillPathPointsBuffer.clear();
}

void MillPathLine::Clear()
{
    MillPathPointsBuffer.clear();
    if (auto* dev = Render::DrawDevice::instance()) {
        if (mRVbo.valid()) {
            dev->destroy(mRVbo);
        }
    }
    mRVbo = {};
}

void MillPathLine::Render()
{
    if (gSimDraw.active()) {
        gSimDraw.submitTriangles(mRVbo);
    }
}

}  // namespace CAMSimulator
