/****************************************************************************
 *   Copyright (c) 2021 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
 *                                                                          *
 *   This file is part of the FreeCAD CAx development system.               *
 *                                                                          *
 *   This library is free software; you can redistribute it and/or          *
 *   modify it under the terms of the GNU Library General Public            *
 *   License as published by the Free Software Foundation; either           *
 *   version 2 of the License, or (at your option) any later version.       *
 *                                                                          *
 *   This library  is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *   GNU Library General Public License for more details.                   *
 *                                                                          *
 *   You should have received a copy of the GNU Library General Public      *
 *   License along with this library; see the file COPYING.LIB. If not,     *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,          *
 *   Suite 330, Boston, MA  02111-1307, USA                                 *
 *                                                                          *
 ****************************************************************************/

#include "BGFXRendererP.h"
#include "Environment.h"

namespace {

/// A box-mip chain of the base environment, held on the CPU and sampled
/// by direction.
///
/// This is what filtered importance sampling needs and a point
/// evaluation cannot give. A GGX sample stands for a whole solid angle,
/// and it has to READ an average over that angle; evaluating
/// envRadiance() at the sample direction instead makes the sample
/// either hit a small bright source or miss it entirely. With a preset
/// whose softbox is 170x the surround (Environment.cpp: radiance 26
/// against 0.15) and a fixed sample set shared by every texel, "hit or
/// miss" is not noise that averages out -- it is 64 discrete ghosts of
/// the one source, which is what the prefiltered levels used to show.
struct EnvChain {
    int baseSize = 0;
    int levels = 0;
    /// [level][face], three floats a texel.
    std::vector<std::vector<std::vector<float>>> data;

    void init(int size)
    {
        baseSize = size;
        levels = 1;
        while (size >> levels)
            ++levels;
        data.assign(levels, {});
        for (int m = 0; m < levels; ++m) {
            int s = size >> m;
            data[m].assign(6, {});
            for (int f = 0; f < 6; ++f)
                data[m][f].assign(size_t(s) * s * 3, 0.0f);
        }
    }

    float *base(int face, int x, int y)
    {
        return &data[0][face][(size_t(y) * baseSize + x) * 3];
    }

    /// Box downsample, level by level. A face never needs another
    /// face's texels for this (the same reason buildEnvBackground can
    /// do one face at a time), even though SAMPLING one does.
    void buildMips()
    {
        for (int m = 1; m < levels; ++m) {
            const int s = baseSize >> m, prev = s * 2;
            for (int f = 0; f < 6; ++f) {
                for (int y = 0; y < s; ++y) {
                    for (int x = 0; x < s; ++x) {
                        for (int c = 0; c < 3; ++c) {
                            data[m][f][(size_t(y)*s + x)*3 + c] = 0.25f * (
                                data[m-1][f][(size_t(2*y)*prev + 2*x)*3 + c]
                                + data[m-1][f][(size_t(2*y)*prev + 2*x + 1)*3 + c]
                                + data[m-1][f][(size_t(2*y+1)*prev + 2*x)*3 + c]
                                + data[m-1][f][(size_t(2*y+1)*prev + 2*x + 1)*3 + c]);
                        }
                    }
                }
            }
        }
    }

    /// Bilinear inside the face the direction lands on, clamped at its
    /// edge. The inverse of BGFXView::cubeDir, so a direction comes
    /// back to the texel that direction was built from.
    void sampleLevel(const float d[3], int m, float out[3]) const
    {
        const float ax = std::fabs(d[0]), ay = std::fabs(d[1]), az = std::fabs(d[2]);
        int face;
        float sc, tc, ma;
        if (ax >= ay && ax >= az) {
            ma = ax;
            if (d[0] > 0.0f) { face = 0; sc = -d[2]; tc = -d[1]; }
            else             { face = 1; sc =  d[2]; tc = -d[1]; }
        }
        else if (ay >= az) {
            ma = ay;
            if (d[1] > 0.0f) { face = 2; sc = d[0]; tc =  d[2]; }
            else             { face = 3; sc = d[0]; tc = -d[2]; }
        }
        else {
            ma = az;
            if (d[2] > 0.0f) { face = 4; sc =  d[0]; tc = -d[1]; }
            else             { face = 5; sc = -d[0]; tc = -d[1]; }
        }
        const int s = baseSize >> m;
        const float u = sc / ma, v = tc / ma;
        const float fx = (u * 0.5f + 0.5f) * s - 0.5f;
        const float fy = (v * 0.5f + 0.5f) * s - 0.5f;
        const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
        const float tu = fx - x0, tv = fy - y0;
        auto texel = [&](int x, int y, float c[3]) {
            x = std::clamp(x, 0, s - 1);
            y = std::clamp(y, 0, s - 1);
            const float *t = &data[m][face][(size_t(y) * s + x) * 3];
            c[0] = t[0]; c[1] = t[1]; c[2] = t[2];
        };
        float c00[3], c10[3], c01[3], c11[3];
        texel(x0, y0, c00);
        texel(x0 + 1, y0, c10);
        texel(x0, y0 + 1, c01);
        texel(x0 + 1, y0 + 1, c11);
        for (int i = 0; i < 3; ++i) {
            const float a = c00[i] + (c10[i] - c00[i]) * tu;
            const float b = c01[i] + (c11[i] - c01[i]) * tu;
            out[i] = a + (b - a) * tv;
        }
    }

    void sample(const float d[3], float lod, float out[3]) const
    {
        lod = std::clamp(lod, 0.0f, float(levels - 1));
        const int m0 = int(std::floor(lod)), m1 = std::min(m0 + 1, levels - 1);
        const float f = lod - m0;
        float a[3], b[3];
        sampleLevel(d, m0, a);
        sampleLevel(d, m1, b);
        for (int i = 0; i < 3; ++i)
            out[i] = a[i] + (b[i] - a[i]) * f;
    }
};

}  // namespace

void BGFXView::envRadiance(const float d[3], float out[3]) const
{
    if (m_envImage && m_envImage->width > 0 && m_envImage->height > 0
            && m_envImage->numComponents > 0) {
        sampleEnvImage(*m_envImage, d, out, colorManaged());
        return;
    }
    envRadianceProcedural(d, out);
}

void BGFXView::sampleEnvImage(const Render::TextureImage &img,
                           const float d[3], float out[3],
                           bool managed)
{
    Render::sampleEnvImage(img, d, out, managed);
}

void BGFXView::envRadianceProcedural(const float d[3], float out[3]) const
{
    // Shared with every other backend (Environment.h), so the Cycles
    // world is baked from the same sky this cube map is.
    Render::envRadianceProcedural(m_envPreset, d, out);
}

void BGFXView::cubeDir(int face, float u, float v, float d[3])
{
    switch (face) {
    case 0:  d[0] =  1.0f; d[1] = -v; d[2] = -u; break;
    case 1:  d[0] = -1.0f; d[1] = -v; d[2] =  u; break;
    case 2:  d[0] =  u; d[1] =  1.0f; d[2] =  v; break;
    case 3:  d[0] =  u; d[1] = -1.0f; d[2] = -v; break;
    case 4:  d[0] =  u; d[1] = -v; d[2] =  1.0f; break;
    default: d[0] = -u; d[1] = -v; d[2] = -1.0f; break;
    }
    float len = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    d[0] /= len; d[1] /= len; d[2] /= len;
}

void BGFXView::buildEnvBackground()
{
    m_envBgMips = 1;
    while (kEnvBgSize >> m_envBgMips)
        ++m_envBgMips;
    m_envBgTex = bgfx::createTextureCube(kEnvBgSize, true, 1,
        bgfx::TextureFormat::RGBA16F, 0, nullptr);
    if (!bgfx::isValid(m_envBgTex))
        return;

    const uint16_t halfOne = bx::halfFromFloat(1.0f);
    // One face at a time, base level first and then halved in place:
    // the mips are box downsamples of the level above, so a face never
    // needs another face's texels and the whole chain costs one pass
    // over the base plus a third of it again.
    std::vector<float> level(size_t(kEnvBgSize) * kEnvBgSize * 3);
    std::vector<float> next;
    std::vector<uint16_t> texels;
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < kEnvBgSize; ++y) {
            for (int x = 0; x < kEnvBgSize; ++x) {
                float u = 2.0f * (x + 0.5f) / kEnvBgSize - 1.0f;
                float v = 2.0f * (y + 0.5f) / kEnvBgSize - 1.0f;
                float d[3];
                cubeDir(face, u, v, d);
                envRadiance(d, &level[(size_t(y)*kEnvBgSize + x) * 3]);
            }
        }
        int size = kEnvBgSize;
        for (int mip = 0; mip < m_envBgMips; ++mip) {
            texels.assign(size_t(size) * size * 4, halfOne);
            for (int i = 0; i < size * size; ++i) {
                for (int c = 0; c < 3; ++c)
                    texels[size_t(i)*4 + c] =
                        bx::halfFromFloat(level[size_t(i)*3 + c]);
            }
            bgfx::updateTextureCube(m_envBgTex, 0, uint8_t(face),
                uint8_t(mip), 0, 0, uint16_t(size), uint16_t(size),
                bgfx::copy(texels.data(),
                    uint32_t(texels.size() * sizeof(uint16_t))));
            if (size == 1)
                break;
            const int half = size / 2;
            next.assign(size_t(half) * half * 3, 0.0f);
            for (int y = 0; y < half; ++y) {
                for (int x = 0; x < half; ++x) {
                    for (int c = 0; c < 3; ++c) {
                        next[(size_t(y)*half + x) * 3 + c] = 0.25f * (
                            level[(size_t(2*y)*size + 2*x) * 3 + c]
                            + level[(size_t(2*y)*size + 2*x + 1) * 3 + c]
                            + level[(size_t(2*y + 1)*size + 2*x) * 3 + c]
                            + level[(size_t(2*y + 1)*size + 2*x + 1) * 3 + c]);
                    }
                }
            }
            level.swap(next);
            size = half;
        }
    }
}

void BGFXView::ensureEnvironment()
{
    if (m_envBuilt)
        return;
    m_envBuilt = true;
    if (bgfx::isValid(m_envTex)) {
        bgfx::destroy(m_envTex);
        m_envTex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_envBgTex)) {
        bgfx::destroy(m_envBgTex);
        m_envBgTex = BGFX_INVALID_HANDLE;
    }
    const auto *caps = bgfx::getCaps();
    if (!(caps->formats[bgfx::TextureFormat::RGBA16F]
            & BGFX_CAPS_FORMAT_TEXTURE_CUBE))
        return;
    buildEnvBackground();
    m_envTex = bgfx::createTextureCube(kEnvSize, true, 1,
        bgfx::TextureFormat::RGBA16F, 0, nullptr);
    if (!bgfx::isValid(m_envTex))
        return;

    int numMips = 1;
    while (kEnvSize >> numMips)
        ++numMips;
    const uint16_t halfOne = bx::halfFromFloat(1.0f);
    std::vector<uint16_t> texels;

    // The base level is the environment itself, and every level above it
    // is prefiltered FROM that base rather than from the function again
    // -- so the chain has to exist in full, all six faces, before the
    // first prefiltered texel is written (a GGX sample crosses faces
    // freely).
    EnvChain chain;
    chain.init(int(kEnvSize));
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < int(kEnvSize); ++y) {
            for (int x = 0; x < int(kEnvSize); ++x) {
                float u = 2.0f * (x + 0.5f) / kEnvSize - 1.0f;
                float v = 2.0f * (y + 0.5f) / kEnvSize - 1.0f;
                float d[3];
                cubeDir(face, u, v, d);
                envRadiance(d, chain.base(face, x, y));
            }
        }
    }
    chain.buildMips();
    // Solid angle of one base texel, the yardstick a sample's own solid
    // angle is measured against below.
    const float omegaP = 4.0f * bx::kPi
        / (6.0f * float(kEnvSize) * float(kEnvSize));

    for (int mip = 0; mip < numMips; ++mip) {
        int size = int(kEnvSize) >> mip;
        float rough = std::min(1.0f, float(mip) / 5.0f);
        float a = rough * rough;
        // A wider lobe needs more samples, and the levels that need them
        // are the small ones: mip 5 is 96 texels against mip 1's 24576,
        // so doubling a level's count costs a quarter of what the level
        // below it paid. Fixing the count at 64 for all of them is why
        // the roughest levels were the worst ones.
        const int numSamples = mip > 0
            ? std::min(1024, 64 << (mip - 1)) : 0;
        for (int face = 0; face < 6; ++face) {
            texels.assign(size_t(size) * size * 4, halfOne);
            for (int y = 0; y < size; ++y) {
                for (int x = 0; x < size; ++x) {
                    float u = 2.0f * (x + 0.5f) / size - 1.0f;
                    float v = 2.0f * (y + 0.5f) / size - 1.0f;
                    float d[3];
                    cubeDir(face, u, v, d);
                    float col[3];
                    if (mip == 0) {
                        const float *t = chain.base(face, x, y);
                        col[0] = t[0]; col[1] = t[1]; col[2] = t[2];
                    } else {
                        // GGX importance sampling with the usual
                        // N = V = R approximation; Hammersley set.
                        float up[3] = {0.0f, 0.0f, 1.0f};
                        if (std::fabs(d[2]) > 0.999f) {
                            up[0] = 1.0f; up[2] = 0.0f;
                        }
                        float tx[3] = {
                            up[1]*d[2] - up[2]*d[1],
                            up[2]*d[0] - up[0]*d[2],
                            up[0]*d[1] - up[1]*d[0]};
                        float tl = std::sqrt(tx[0]*tx[0]
                            + tx[1]*tx[1] + tx[2]*tx[2]);
                        tx[0] /= tl; tx[1] /= tl; tx[2] /= tl;
                        float bt[3] = {
                            d[1]*tx[2] - d[2]*tx[1],
                            d[2]*tx[0] - d[0]*tx[2],
                            d[0]*tx[1] - d[1]*tx[0]};
                        float sum[3] = {0.0f, 0.0f, 0.0f};
                        float wsum = 0.0f;
                        for (int i = 0; i < numSamples; ++i) {
                            float u1 = (i + 0.5f) / numSamples;
                            uint32_t bits = uint32_t(i);
                            bits = (bits << 16) | (bits >> 16);
                            bits = ((bits & 0x55555555u) << 1)
                                | ((bits & 0xAAAAAAAAu) >> 1);
                            bits = ((bits & 0x33333333u) << 2)
                                | ((bits & 0xCCCCCCCCu) >> 2);
                            bits = ((bits & 0x0F0F0F0Fu) << 4)
                                | ((bits & 0xF0F0F0F0u) >> 4);
                            bits = ((bits & 0x00FF00FFu) << 8)
                                | ((bits & 0xFF00FF00u) >> 8);
                            float u2 = float(bits)
                                * 2.3283064365386963e-10f;
                            float phi = 2.0f * bx::kPi * u1;
                            float ct = std::sqrt((1.0f - u2)
                                / (1.0f + (a*a - 1.0f) * u2));
                            float st = std::sqrt(
                                std::max(0.0f, 1.0f - ct*ct));
                            float cp = std::cos(phi) * st;
                            float sp = std::sin(phi) * st;
                            float h[3], l[3];
                            for (int k = 0; k < 3; ++k)
                                h[k] = tx[k]*cp + bt[k]*sp + d[k]*ct;
                            for (int k = 0; k < 3; ++k)
                                l[k] = 2.0f*ct*h[k] - d[k];
                            float ndl = d[0]*l[0] + d[1]*l[1]
                                + d[2]*l[2];
                            if (ndl <= 0.0f)
                                continue;
                            // Filtered importance sampling (Colbert and
                            // Krivanek): read the level whose texels are
                            // about as wide as the solid angle this one
                            // sample is standing in for. With n = v = d
                            // both dot(n,h) and dot(v,h) are ct, so the
                            // reflected direction's density is D(h)/4.
                            const float a2 = a * a;
                            const float den = ct*ct*(a2 - 1.0f) + 1.0f;
                            const float D = a2 / (bx::kPi * den * den);
                            const float pdf = D * 0.25f;
                            const float omegaS = 1.0f
                                / (float(numSamples)
                                   * std::max(pdf, 1.0e-8f));
                            const float lod = 0.5f
                                * std::log2(omegaS / omegaP);
                            float r[3];
                            chain.sample(l, std::max(lod, 0.0f), r);
                            for (int k = 0; k < 3; ++k)
                                sum[k] += r[k] * ndl;
                            wsum += ndl;
                        }
                        for (int k = 0; k < 3; ++k)
                            col[k] = sum[k]
                                / std::max(wsum, 1.0e-4f);
                    }
                    uint16_t *t = &texels[(size_t(y)*size + x) * 4];
                    t[0] = bx::halfFromFloat(col[0]);
                    t[1] = bx::halfFromFloat(col[1]);
                    t[2] = bx::halfFromFloat(col[2]);
                }
            }
            bgfx::updateTextureCube(m_envTex, 0, uint8_t(face),
                uint8_t(mip), 0, 0, uint16_t(size), uint16_t(size),
                bgfx::copy(texels.data(),
                    uint32_t(texels.size() * sizeof(uint16_t))));
        }
    }

    // Project the base environment onto the SH basis (per-texel
    // solid-angle weights), then fold the cosine convolution,
    // polynomial constants and the Lambertian 1/pi (Ramamoorthi:
    // the constant L20 terms c3/3 and -c5 cancel).
    double L[kEnvSH][3] = {};
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < kEnvSize; ++y) {
            for (int x = 0; x < kEnvSize; ++x) {
                float u = 2.0f * (x + 0.5f) / kEnvSize - 1.0f;
                float v = 2.0f * (y + 0.5f) / kEnvSize - 1.0f;
                float d[3];
                cubeDir(face, u, v, d);
                double r2 = 1.0 + double(u)*u + double(v)*v;
                double w = 4.0 / (kEnvSize * double(kEnvSize)
                                  * r2 * std::sqrt(r2));
                // The same texels the chain's base level already
                // holds -- projecting them costs nothing extra, where
                // asking the function again costs another 98304
                // evaluations of it.
                const float *col = chain.base(face, x, y);
                double Y[kEnvSH] = {
                    0.282095,
                    0.488603 * d[1],
                    0.488603 * d[2],
                    0.488603 * d[0],
                    1.092548 * d[0] * d[1],
                    1.092548 * d[1] * d[2],
                    0.315392 * (3.0 * d[2] * d[2] - 1.0),
                    1.092548 * d[0] * d[2],
                    0.546274 * (d[0] * d[0] - d[1] * d[1]),
                };
                for (int k = 0; k < kEnvSH; ++k)
                    for (int c = 0; c < 3; ++c)
                        L[k][c] += col[c] * Y[k] * w;
            }
        }
    }
    constexpr double c1 = 0.429043, c2 = 0.511664;
    constexpr double c3 = 0.743125, c4 = 0.886227;
    constexpr double invPi = 0.3183098861837907;
    const double fold[kEnvSH] = {
        c4, 2.0*c2, 2.0*c2, 2.0*c2,
        2.0*c1, 2.0*c1, c3/3.0, 2.0*c1, c1,
    };
    for (int k = 0; k < kEnvSH; ++k) {
        for (int c = 0; c < 3; ++c)
            envSH[k][c] = float(L[k][c] * fold[k] * invPi);
        envSH[k][3] = 0.0f;
    }
}

void BGFXView::submitSunDisc(float sizeDeg)
{
    if (!bgfx::isValid(m_progSun))
        return;
    float r = std::max(sizeDeg, 0.05f) * 3.14159265f / 180.0f;
    float params[4] = {std::cos(r), std::cos(r * 0.7f),
                       0.05f, 64.0f};
    bgfx::setUniform(u_sunParams, params);
    float lightDir[4] = {lightDirView[0], lightDirView[1],
                         lightDirView[2], 1.0f};
    bgfx::setUniform(u_lightDir, lightDir);
    bgfx::setUniform(u_lightColor, lightColorI);
    fullscreen(ViewSunDisc, m_progSun,
               BGFX_STATE_WRITE_RGB
               | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                                       BGFX_STATE_BLEND_ONE));
}

void BGFXView::submitEnvBackground()
{
    // The lighting cube stands in if the background one could not be
    // made (a second allocation is a second thing that can fail): a
    // background at the wrong softness beats no background at all,
    // which would drop the frame back to a gradient that reflects
    // nothing the surfaces show.
    const bool ownMap = bgfx::isValid(m_envBgTex);
    const bgfx::TextureHandle tex = ownMap ? m_envBgTex : m_envTex;
    if (!bgfx::isValid(m_progEnvBg) || !bgfx::isValid(tex))
        return;
    // How soft is the user's call (Render_PBREnvBlur), because both
    // answers are wanted. A backdrop out of focus reads as a place,
    // and softening also lets a small bright source bleed into a wide
    // gentle falloff instead of sitting in the frame as a hard
    // rectangle -- but the same blur is why a Realistic view did not
    // look like the External one standing beside it, and there was no
    // way to ask for the sharp one. Zero is m_envBgTex as baked, which
    // is the resolution the path tracer bakes its world at.
    //
    // What softening MEANS is a lens (Render::envBlurAngle): the
    // aperture's cone of directions, convolved in. Reading one mip
    // level instead was the cheap stand-in for it and looked like one
    // -- see fs_fc_env.sc -- so the cone is spread over taps here, and
    // the mip level's job is now only to size each tap's footprint to
    // the SPACING between them, which is what closes the gaps that
    // made a wide setting pixelated. The count follows the radius,
    // measured in base texels, so a narrow aperture stays a couple of
    // fetches and only a wide one pays for the full 32.
    float params[4] = {1.0f, 2.0f, 1.0f, std::max(pbrEnvIntensity, 0.0f)};
    if (ownMap) {
        // Radians a base texel spans at a face's centre, the unit the
        // aperture radius and the mip chain are both measured in.
        constexpr float kTexelAngle = 1.5707963268f / float(kEnvBgSize);
        const float alpha = Render::envBlurAngle(pbrEnvBlur);
        const float radius = alpha / kTexelAngle;
        const int taps = std::clamp(int(std::ceil(2.0f * radius)), 1, 32);
        if (taps <= 1) {
            // Narrower than a texel: nothing for a second tap to find,
            // and the base level is already the answer.
            params[1] = 0.0f;
        }
        else {
            params[0] = std::cos(alpha);
            // Spacing of N points spread over a disc of this radius,
            // as a mip level: one tap then covers exactly the ground
            // between it and the next.
            params[1] = std::clamp(
                std::log2(std::max(2.0f * radius / std::sqrt(float(taps)),
                                   1.0f)),
                0.0f, float(std::max(m_envBgMips - 1, 0)));
            params[2] = float(taps);
        }
    }
    bgfx::setUniform(u_pbrParams, params);
    bgfx::setTexture(1, s_texEnv, tex);
    fullscreen(ViewBackground, m_progEnvBg,
               BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
               | BGFX_STATE_MSAA);
}

void BGFXView::submitBackground(const Render::Background &bg)
{
    if (bg.type == Render::Background::Flat)
        return;

    uint32_t fcol = vertexColor(bg.fromColor);
    uint32_t tcol = vertexColor(bg.toColor);
    uint32_t mcol = vertexColor(bg.midColor);

    std::vector<TransientVertex> verts;
    auto vert = [](float x, float y, uint32_t rgba) {
        TransientVertex v;
        v.px = x; v.py = y; v.pz = 1.0f;
        v.nx = v.ny = 0.0f; v.nz = 1.0f;
        v.rgba = rgba;
        return v;
    };
    // Triangle-list expansion of a strip/fan given as a vertex list;
    // winding is irrelevant (the background draw does not cull).
    auto strip = [&verts](const std::vector<TransientVertex> &vs) {
        for (size_t i = 2; i < vs.size(); ++i) {
            verts.push_back(vs[i - 2]);
            verts.push_back(vs[i - 1]);
            verts.push_back(vs[i]);
        }
    };
    auto fan = [&verts](const std::vector<TransientVertex> &vs) {
        for (size_t i = 2; i < vs.size(); ++i) {
            verts.push_back(vs[0]);
            verts.push_back(vs[i - 1]);
            verts.push_back(vs[i]);
        }
    };

    if (bg.type == Render::Background::LinearGradient) {
        if (!bg.hasMid)
            strip({vert(-1.0f, 1.0f, fcol), vert(-1.0f, -1.0f, tcol),
                   vert(1.0f, 1.0f, fcol), vert(1.0f, -1.0f, tcol)});
        else {
            strip({vert(-1.0f, 1.0f, fcol), vert(-1.0f, 0.0f, mcol),
                   vert(1.0f, 1.0f, fcol), vert(1.0f, 0.0f, mcol)});
            strip({vert(-1.0f, 0.0f, mcol), vert(-1.0f, -1.0f, tcol),
                   vert(1.0f, 0.0f, mcol), vert(1.0f, -1.0f, tcol)});
        }
    } else {
        // Same 32-segment circle/oval tessellation as the Coin node.
        constexpr int kSegments = 32;
        constexpr float kStep = 2.0f * bx::kPi / kSegments;
        float circle[kSegments][2], oval[kSegments][2];
        for (int i = 0; i < kSegments; ++i) {
            float c = bx::cos(i * kStep), s = bx::sin(i * kStep);
            circle[i][0] = bx::kSqrt2 * c;
            circle[i][1] = bx::kSqrt2 * s;
            oval[i][0] = 0.3f * bx::kSqrt2 * c;
            oval[i][1] = s / bx::kSqrt2;
        }
        if (!bg.hasMid) {
            std::vector<TransientVertex> vs;
            vs.push_back(vert(0.0f, 0.0f, fcol));
            for (auto &p : circle)
                vs.push_back(vert(p[0], p[1], tcol));
            vs.push_back(vs[1]);
            fan(vs);
        } else {
            std::vector<TransientVertex> vs;
            vs.push_back(vert(0.0f, 0.0f, fcol));
            for (auto &p : oval)
                vs.push_back(vert(p[0], p[1], mcol));
            vs.push_back(vs[1]);
            fan(vs);
            vs.clear();
            for (int i = 0; i < kSegments; ++i) {
                vs.push_back(vert(oval[i][0], oval[i][1], mcol));
                vs.push_back(vert(circle[i][0], circle[i][1], tcol));
            }
            vs.push_back(vs[0]);
            vs.push_back(vs[1]);
            strip(vs);
        }
    }

    TransientVertex::init();
    uint32_t num = uint32_t(verts.size());
    if (bgfx::getAvailTransientVertexBuffer(num, TransientVertex::ms_layout)
            < num)
        return;
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, num, TransientVertex::ms_layout);
    memcpy(tvb.data, verts.data(), num * sizeof(TransientVertex));

    float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float params[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // per-vertex color
    bgfx::setUniform(u_matColor, zero);
    bgfx::setUniform(u_matEmissive, zero);
    bgfx::setUniform(u_matSpecular, zero);
    bgfx::setUniform(u_params, params);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_MSAA);
    bgfx::submit(vid(ViewBackground), m_progFlat);
    ++drawcount;
}
