// SPDX-License-Identifier: LGPL-2.1-or-later

#include "MillPathLine.h"

#include "Shader.h"
#include "SimDrawContext.h"

// include this last as the defines can mess up other includes
#include "OpenGlWrapper.h"

namespace CAMSimulator
{

void MillPathLine::GenerateModel()
{
    mNumVerts = MillPathPointsBuffer.size();
    void* vbuffer = MillPathPointsBuffer.data();

    // vertex buffer
    glGenBuffers(1, &mVbo);
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER, mNumVerts * sizeof(MillPathPosition), vbuffer, GL_STATIC_DRAW);

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

void MillPathLine::SetupVertexAttibs()
{
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(MillPathPosition),
        (void*)offsetof(MillPathPosition, X)
    );
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        1,
        GL_INT,
        GL_FALSE,
        sizeof(MillPathPosition),
        (void*)offsetof(MillPathPosition, SegmentId)
    );
}

void MillPathLine::Clear()
{
    MillPathPointsBuffer.clear();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    GLDELETE_BUFFER(mVbo);
    if (auto* dev = Render::DrawDevice::instance()) {
        if (mRVbo.valid()) {
            dev->destroy(mRVbo);
        }
    }
    mRVbo = {};
}

void MillPathLine::Render()
{
    SetupVertexAttibs();
    glDrawArrays(GL_LINE_STRIP, 0, mNumVerts);

    if (gSimDraw.active()) {
        gSimDraw.submitLines(mRVbo);
    }
}

}  // namespace CAMSimulator
