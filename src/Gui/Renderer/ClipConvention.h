/***************************************************************************
 *   Copyright (c) 2026 Zheng, Lei <realthunder.dev@gmail.com>             *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU     *
 *   Library General Public License for more details.                      *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#ifndef GUI_RENDERER_CLIPCONVENTION_H
#define GUI_RENDERER_CLIPCONVENTION_H

/** \file
 * The clip-depth conventions a projection matrix can be written for,
 * and the conversion between them (docs/RenderDebug.md sec 5.2b).
 *
 * OpenGL clips depth against [-1,1]; Metal, Direct3D and Vulkan clip
 * against [0,1]. Coin builds its camera projection for OpenGL, so on
 * every other backend it has to be converted before anything -- shader
 * or CPU culler -- reads it. Feeding a [-1,1] matrix to a [0,1] clipper
 * drops the far half of the scene: on the golden test scene the floor
 * ended in a flat cut and the glass rod and bulb vanished entirely,
 * 75236 geometry pixels against the golden's 108191.
 *
 * This lives in a header of its own, as a pure function over a
 * float[16], for two reasons. It was inline in BGFXFrame::render(),
 * where the only way to check it was to run a GPU. And the arithmetic
 * is worth checking on a machine that cannot: a box with no Metal, no
 * Vulkan and no D3D can still prove the conversion right, which is the
 * one useful thing a GL-only CI leg can say about a non-GL backend.
 */

#include <cstring>

namespace Render
{

/** Convert a GL-convention projection to the [0,1] clip depth every
 * other backend uses.
 *
 * \a in and \a out are column-major float[16], the layout bgfx and Coin
 * both use: element (row i, column c) is at index 4*c + i. They may be
 * the same pointer.
 *
 * The map is z -> (z + w) / 2, applied to the **z row alone** (indices
 * 4*c + 2). Two things about that are deliberate:
 *
 * - The w row (4*c + 3) is left exactly as it was. Its z entry is how
 *   every shader tells a perspective camera from an orthographic one --
 *   -1 or 0 -- and rewriting it would make every orthographic camera
 *   read as perspective. No shader reads the z row at all, so the
 *   conversion stays confined to clipping.
 * - Only the z row needs it. The x and y clip ranges are [-1,1] on
 *   every backend bgfx targets; the origin of the *texture* differs
 *   (see fc_screen.sh) but that is a sampling convention, not a
 *   projection one.
 *
 * Worked example, the golden scene's orthographic camera: Coin gives
 * P[10] = -0.023595 and P[14] = -1.038783 (the z row's z and w
 * entries); the correct [0,1] ortho for the same near/far is -0.011797
 * and -0.019392. A factor of two on one and nothing like a scale on the
 * other -- which is why a dump of the fed matrix settles the question
 * in one line.
 */
inline void projToZeroToOneDepth(const float* in, float* out)
{
    if (out != in) {
        std::memcpy(out, in, 16 * sizeof(float));
    }
    for (int c = 0; c < 4; ++c) {
        out[4 * c + 2] = 0.5f * (in[4 * c + 2] + in[4 * c + 3]);
    }
}

}  // namespace Render

#endif  // GUI_RENDERER_CLIPCONVENTION_H
