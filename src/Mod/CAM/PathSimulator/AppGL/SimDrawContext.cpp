// SPDX-License-Identifier: LGPL-2.1-or-later

#include "SimDrawContext.h"

namespace CAMSimulator
{

SimDrawContext gSimDraw;

SimDrawContext::SimDrawContext()
{
    mat4x4_identity(model);
    mat4x4_identity(normalRot);
    for (int i = 0; i < 4; i++) {
        lightPos[i] = lightColor[i] = lightAmbient[i] = 0.0f;
        objectColor[i] = objectColorAlpha[i] = 1.0f;
        params[i] = 0.0f;
    }
}

void SimDrawContext::setModel(const mat4x4& modelMat, const mat4x4& normalMat)
{
    mat4x4_dup(model, const_cast<mat4x4&>(modelMat));
    mat4x4_dup(normalRot, const_cast<mat4x4&>(normalMat));
}

void SimDrawContext::setColor(float* dst, const vec3& src, float w)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = w;
}

void SimDrawContext::push(Render::PrimitiveType primitive, bool withTransform)
{
    if (withTransform) {
        surface->setTransform(&model[0][0]);
    }
    // Uniform values are global per name in the backend; pushing ones
    // the active program does not read is harmless, and pushing the
    // whole set keeps this independent of which program is active.
    surface->setUniform(uniNormalRot, normalRot);
    surface->setUniform(uniLightPos, lightPos);
    surface->setUniform(uniLightColor, lightColor);
    surface->setUniform(uniLightAmbient, lightAmbient);
    surface->setUniform(uniObjectColor, objectColor);
    surface->setUniform(uniObjectColorAlpha, objectColorAlpha);
    surface->setUniform(uniParams, params);
    Render::DrawState s = state;
    s.cull = cullEnabled ? cullFace : Render::CullMode::None;
    s.primitive = primitive;
    surface->setState(s);
    surface->setStencil(stencil);
}

void SimDrawContext::submitIndexed(Render::VertexBufferHandle vb,
                                   Render::IndexBufferHandle ib)
{
    if (!surface || !program.valid() || !vb.valid() || !ib.valid()) {
        return;
    }
    push(Render::PrimitiveType::Triangles, true);
    surface->setVertexBuffer(vb);
    surface->setIndexBuffer(ib);
    surface->submit(pass, program);
    submitted = true;
}

void SimDrawContext::submitTriangles(Render::VertexBufferHandle vb)
{
    if (!surface || !program.valid() || !vb.valid()) {
        return;
    }
    push(Render::PrimitiveType::Triangles, false);
    surface->setVertexBuffer(vb);
    surface->submit(pass, program);
    submitted = true;
}

}  // namespace CAMSimulator
