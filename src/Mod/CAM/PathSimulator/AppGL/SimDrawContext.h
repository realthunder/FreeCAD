// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com>*
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
#include <Gui/Renderer/DrawSurface.h>

#include "linmath.h"

namespace CAMSimulator
{

/// The simulator's passes on its draw surface, in backend submission
/// order (docs/CAMSimRenderPort.md step 5). All of the first three
/// target the G-buffer; the split exists because a backend pass has
/// ONE projection, and the base shape substitutes its glPolygonOffset
/// with a depth-biased projection of its own.
enum : unsigned
{
    SimPassScene = 0,      ///< stencil CSG, cut coloring, the tool
    SimPassBaseShape = 1,  ///< base shape, depth-biased projection
    SimPassPath = 2,       ///< tool-path lines
    SimPassResolve = 3,    ///< backbuffer: deferred lighting quad
    SimPassCount = 4,
};

/// The facade-side current state. The GL path keeps state in the
/// driver -- glEnable/glDepthFunc persist until changed, the active
/// Shader holds its uniform values -- while the backend keeps NO
/// current state: every submit carries everything. So the seven
/// Glsim* functions and the SimDisplay pass setups write here as well
/// as into GL, and each draw site folds the accumulated state into
/// its submit. Dormant (never submits) while `surface` is null; step
/// 6's frame driver points it at the live surface.
struct SimDrawContext
{
    Render::DrawSurface* surface = nullptr;
    unsigned pass = SimPassScene;

    Render::DrawState state;
    Render::StencilState stencil;
    /// GL's cull enable and cull face are separate switches and the
    /// pass setups toggle them independently; folded at submit.
    bool cullEnabled = false;
    Render::CullMode cullFace = Render::CullMode::Back;
    Render::ProgramHandle program;

    // Uniform handles, borrowed from SimDisplay (which owns them).
    Render::UniformHandle uniNormalRot;
    Render::UniformHandle uniLightPos;
    Render::UniformHandle uniLightColor;
    Render::UniformHandle uniLightAmbient;
    Render::UniformHandle uniObjectColor;
    Render::UniformHandle uniObjectColorAlpha;
    Render::UniformHandle uniParams;

    // Pending uniform values, re-pushed at every submit.
    mat4x4 model;
    mat4x4 normalRot;
    float lightPos[4];
    float lightColor[4];
    float lightAmbient[4];
    float objectColor[4];
    float objectColorAlpha[4];
    /// x = invertedNormals, y = ssaoActive, z = curSegment.
    float params[4];

    SimDrawContext();

    /// Any submit landed since the frame driver reset it; a frame
    /// with none skips endFrame (nothing to blit over the GL output).
    bool submitted = false;

    bool active() const
    {
        return surface != nullptr;
    }

    void setModel(const mat4x4& modelMat, const mat4x4& normalMat);
    void setColor(float* dst, const vec3& src, float w = 1.0f);

    /// One indexed-triangle draw with everything accumulated.
    void submitIndexed(Render::VertexBufferHandle vb, Render::IndexBufferHandle ib);
    /// One line-strip draw; the line shader has no model transform.
    void submitLines(Render::VertexBufferHandle vb);

private:
    void push(Render::PrimitiveType primitive, bool withTransform);
};

extern SimDrawContext gSimDraw;

}  // namespace CAMSimulator
