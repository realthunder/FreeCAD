// SPDX-License-Identifier: LGPL-2.1-or-later

#include "MillPathLine.h"

#include "SimDrawContext.h"

namespace CAMSimulator
{

void MillPathLine::GenerateModel()
{
    mNumVerts = MillPathPointsBuffer.size();

    if (auto* dev = Render::DrawDevice::instance()) {
        if (mRVbo.valid()) {
            dev->destroy(mRVbo);
        }
        // The struct's int SegmentId converts to float for the facade
        // copy; the GL path's non-integer attribute pointer converted
        // the same way at fetch time.
        std::vector<float> converted;
        converted.reserve(MillPathPointsBuffer.size() * 4);
        for (const auto& p : MillPathPointsBuffer) {
            converted.push_back(p.X);
            converted.push_back(p.Y);
            converted.push_back(p.Z);
            converted.push_back((float)p.SegmentId);
        }
        Render::VertexLayout layout;
        layout.add(Render::DrawAttrib::Position, 3, Render::DrawAttribType::Float)
            .add(Render::DrawAttrib::TexCoord0, 1, Render::DrawAttribType::Float);
        mRVbo = dev->createVertexBuffer(
            converted.data(),
            (unsigned int)(converted.size() * sizeof(float)),
            layout
        );
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
        gSimDraw.submitLines(mRVbo);
    }
}

}  // namespace CAMSimulator
