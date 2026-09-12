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

/** \file
 * The GL -> [0,1] clip-depth conversion (docs/RenderDebug.md sec 5.2b).
 *
 * This is arithmetic that only runs on Metal, Direct3D and Vulkan --
 * `render()` applies it under `!caps->homogeneousDepth`, so an OpenGL
 * build never executes a line of it. That is exactly why it is worth a
 * unit test: the defect it fixes (the far half of every scene falling
 * outside the clipper) was found by capturing frames on a Mac, and
 * before this file nothing anywhere checked the conversion itself.
 * These cases need no GPU, no window and no bgfx -- the header is
 * inline over a float[16] and includes only <cstring> -- so a box with
 * none of those backends can still prove the maths right.
 */

#include <gtest/gtest.h>

#include <Gui/Renderer/ClipConvention.h>

namespace
{

// Column-major float[16]: element (row i, column c) at 4*c + i.
// Identity, which is also a valid (degenerate) starting point.
void identity(float m[16])
{
    for (int i = 0; i < 16; ++i) {
        m[i] = 0.0f;
    }
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// The golden test scene's orthographic camera, as Coin builds it for
// OpenGL. Only the two z-row entries the conversion touches are given
// real values (docs/RenderDebug.md sec 5.2b); the rest is an ortho
// skeleton, whose w row is (0, 0, 0, 1).
void goldenSceneOrtho(float m[16])
{
    identity(m);
    m[4 * 2 + 2] = -0.023595f;  // z row, column 2
    m[4 * 3 + 2] = -1.038783f;  // z row, column 3
}

}  // namespace

// The measured pair from the diagnosis. NOTE the tolerance: the two
// values printed in the doc are not rounded the same way -- -0.0117975
// was truncated to -0.011797 while -0.0193915 was rounded to -0.019392
// -- so asserting against those six digits is off by one in the last
// place whichever convention a test picks. Assert the closed form.
TEST(ClipConvention, GoldenSceneOrthoMatchesTheMeasuredPair)
{
    float in[16];
    goldenSceneOrtho(in);
    float out[16];
    Render::projToZeroToOneDepth(in, out);

    EXPECT_NEAR(out[4 * 2 + 2], 0.5f * (-0.023595f + 0.0f), 1e-7f);
    EXPECT_NEAR(out[4 * 3 + 2], 0.5f * (-1.038783f + 1.0f), 1e-7f);
    // and, to the doc's printed precision, the pair it records
    EXPECT_NEAR(out[4 * 2 + 2], -0.011797f, 1e-6f);
    EXPECT_NEAR(out[4 * 3 + 2], -0.019392f, 1e-6f);
}

// The near and far planes must land on 0 and 1 exactly, which is the
// whole point of the conversion: under GL they sit at -1 and +1.
TEST(ClipConvention, OrthoMapsNearAndFarOntoZeroAndOne)
{
    // A GL ortho maps near -> z/w = -1 and far -> +1. Build one
    // directly for near 1, far 3 and check the converted matrix puts
    // them at 0 and 1.
    const float n = 1.0f;
    const float f = 3.0f;
    float in[16];
    identity(in);
    in[4 * 2 + 2] = -2.0f / (f - n);          // z row, column 2
    in[4 * 3 + 2] = -(f + n) / (f - n);       // z row, column 3

    float out[16];
    Render::projToZeroToOneDepth(in, out);

    // eye-space z = -n (the near plane), w = 1 for an ortho
    const float zNear = out[4 * 2 + 2] * (-n) + out[4 * 3 + 2];
    const float zFar = out[4 * 2 + 2] * (-f) + out[4 * 3 + 2];
    EXPECT_NEAR(zNear, 0.0f, 1e-6f);
    EXPECT_NEAR(zFar, 1.0f, 1e-6f);
}

// The same for a perspective camera, where w is -z rather than 1.
TEST(ClipConvention, PerspectiveMapsNearAndFarOntoZeroAndOne)
{
    const float n = 2.0f;
    const float f = 50.0f;
    float in[16];
    identity(in);
    in[4 * 2 + 2] = -(f + n) / (f - n);
    in[4 * 3 + 2] = -2.0f * f * n / (f - n);
    in[4 * 2 + 3] = -1.0f;  // w row, column 2: the perspective term
    in[4 * 3 + 3] = 0.0f;

    float out[16];
    Render::projToZeroToOneDepth(in, out);

    for (const float ez : {-n, -f}) {
        const float clipZ = out[4 * 2 + 2] * ez + out[4 * 3 + 2];
        const float clipW = out[4 * 2 + 3] * ez + out[4 * 3 + 3];
        ASSERT_GT(clipW, 0.0f);
        const float ndc = clipZ / clipW;
        EXPECT_NEAR(ndc, ez == -n ? 0.0f : 1.0f, 1e-5f);
    }
}

// THE INVARIANT THAT MATTERS MOST. The w row carries the perspective
// term, and a dozen shaders tell a perspective camera from an
// orthographic one by reading it -- FC_MTX(u_proj, 2, 3), which is -1
// or exactly 0. Fold it and every orthographic camera reads as
// perspective, which is defect 3 of sec 5.2b arriving by another road.
// This case exists to stop a future "simplify the loop to cover all
// four rows".
TEST(ClipConvention, LeavesTheWRowExactlyAlone)
{
    float in[16];
    goldenSceneOrtho(in);
    in[4 * 2 + 3] = -1.0f;  // pretend perspective, to have something to lose
    in[4 * 3 + 3] = 0.0f;

    float out[16];
    Render::projToZeroToOneDepth(in, out);

    for (int c = 0; c < 4; ++c) {
        EXPECT_FLOAT_EQ(out[4 * c + 3], in[4 * c + 3])
            << "w row column " << c << " must survive untouched";
    }
}

// x and y clip against [-1,1] on every backend bgfx targets. The
// texture ORIGIN differs off GL, but that is fc_screen.sh's business
// and not the projection's, so these rows must not move either.
TEST(ClipConvention, LeavesTheXAndYRowsAlone)
{
    float in[16];
    identity(in);
    for (int c = 0; c < 4; ++c) {
        in[4 * c + 0] = 1.0f + float(c);
        in[4 * c + 1] = 5.0f + float(c);
    }

    float out[16];
    Render::projToZeroToOneDepth(in, out);

    for (int c = 0; c < 4; ++c) {
        EXPECT_FLOAT_EQ(out[4 * c + 0], in[4 * c + 0]);
        EXPECT_FLOAT_EQ(out[4 * c + 1], in[4 * c + 1]);
    }
}

// The call site passes distinct buffers, so nothing there would catch a
// regression in the aliased case -- but the signature permits it.
TEST(ClipConvention, WorksInPlace)
{
    float expected[16];
    float m[16];
    goldenSceneOrtho(m);
    Render::projToZeroToOneDepth(m, expected);

    goldenSceneOrtho(m);
    Render::projToZeroToOneDepth(m, m);

    for (int i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(m[i], expected[i]) << "index " << i;
    }
}

// Applying it twice is not the same as applying it once. This is not a
// property anyone wants, it is a guard: the conversion runs exactly
// once per frame, at the top of render(), and a second call site added
// later would be silent without this.
TEST(ClipConvention, IsNotIdempotent)
{
    float once[16];
    float twice[16];
    float in[16];
    goldenSceneOrtho(in);

    Render::projToZeroToOneDepth(in, once);
    Render::projToZeroToOneDepth(once, twice);

    EXPECT_NE(once[4 * 3 + 2], twice[4 * 3 + 2]);
}
