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
    float u, v;
    if (img.width >= img.height * 3 / 2) {
        // Equirectangular: azimuth around Z, elevation from Z.
        u = 0.5f + std::atan2(d[1], d[0]) / (2.0f * bx::kPi);
        v = std::acos(bx::clamp(d[2], -1.0f, 1.0f)) / bx::kPi;
    }
    else {
        // Sphere map: the classic m = 2*sqrt(dx^2+dy^2+(dz+1)^2)
        // parametrization with the view axis along -Y (the front
        // view), so the image center faces the default camera.
        float rx = d[0], ry = d[2], rz = -d[1];
        float m = 2.0f * std::sqrt(rx*rx + ry*ry + (rz + 1.0f)
                                   * (rz + 1.0f));
        if (m < 1e-6f)
            m = 1e-6f;
        u = rx / m + 0.5f;
        v = 1.0f - (ry / m + 0.5f);
    }
    // Wrap horizontally (a panorama seam is continuous), clamp
    // vertically (poles).
    u -= std::floor(u);
    v = bx::clamp(v, 0.0f, 1.0f);
    // Rows are bottom-up like GL, v runs top-down.
    float fx = u * img.width - 0.5f;
    float fy = (1.0f - v) * img.height - 0.5f;
    int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    float tx = fx - x0, ty = fy - y0;
    const int nc = img.numComponents;
    // An HDR picture is already linear radiance, and may be far
    // brighter than one -- a sun is thousands. It is neither decoded
    // below nor clamped here; that headroom is the entire reason to
    // load one.
    const bool hdr = img.sample == Render::TextureImage::F32;
    auto texel = [&](int x, int y, float c[3]) {
        x = ((x % img.width) + img.width) % img.width;
        y = bx::clamp(y, 0, img.height - 1);
        const size_t base = (size_t(y) * img.width + x) * nc;
        if (nc >= 3) {
            c[0] = img.component(base);
            c[1] = img.component(base + 1);
            c[2] = img.component(base + 2);
        }
        else {
            c[0] = c[1] = c[2] = img.component(base);
        }
    };
    float c00[3], c10[3], c01[3], c11[3];
    texel(x0, y0, c00);
    texel(x0 + 1, y0, c10);
    texel(x0, y0 + 1, c01);
    texel(x0 + 1, y0 + 1, c11);
    for (int i = 0; i < 3; ++i) {
        float a = c00[i] + (c10[i] - c00[i]) * tx;
        float b = c01[i] + (c11[i] - c01[i]) * tx;
        // The shading math is linear and an 8-bit photo is gamma
        // encoded, so the photo is decoded here either way. WHICH
        // decode follows the pipeline: a colour-managed frame owes the
        // exact inverse of what it encodes on the way out, so that an
        // environment drawn as the visible background comes back out of
        // the round trip as the photo it was. Unmanaged, the squaring
        // this always used stays -- it is what those frames were drawn
        // with, and nothing re-encodes them.
        float lin = a + (b - a) * ty;
        out[i] = hdr ? lin
                     : (managed ? decodeSRGB(lin) : lin * lin);
    }
}

namespace {

/// A rectangular source in (azimuth, elevation), with a soft edge.
///
/// Rectangular and not a cosine lobe on purpose: the lobes this file
/// used to be built from have no edges anywhere, and an edge is the
/// whole difference between a reflection that reads as a light and one
/// that reads as a gradient. `soft` is the fraction of the half-width
/// spent on the falloff, so the rest is a flat core the way a real
/// diffuser is.
float envBox(float az, float el, float azDeg, float elDeg,
             float halfAzDeg, float halfElDeg, float soft)
{
    const float kDeg = 3.14159265358979323846f / 180.0f;
    float da = az - azDeg * kDeg;
    while (da > 3.14159265358979323846f) da -= 2.0f * 3.14159265358979323846f;
    while (da < -3.14159265358979323846f) da += 2.0f * 3.14159265358979323846f;
    float u = std::fabs(da) / (halfAzDeg * kDeg);
    float v = std::fabs(el - elDeg * kDeg) / (halfElDeg * kDeg);
    float m = std::max(u, v);
    if (m >= 1.0f)
        return 0.0f;
    return std::min(1.0f, (1.0f - m) / soft);
}

struct EnvBox {
    float az, el, halfAz, halfEl, radiance, soft, color[3];
};

void envAddBoxes(float az, float el, const EnvBox *boxes, int count,
                 float out[3])
{
    for (int b = 0; b < count; ++b) {
        const EnvBox &s = boxes[b];
        float w = envBox(az, el, s.az, s.el, s.halfAz, s.halfEl, s.soft)
            * s.radiance;
        if (w <= 0.0f)
            continue;
        for (int i = 0; i < 3; ++i)
            out[i] += s.color[i] * w;
    }
}

/// Darken a thin band at the horizon. A horizon LINE is the cheapest
/// cue there is that a reflection is of a place rather than of a ramp.
void envHorizon(float el, float halfDeg, float amount, float out[3])
{
    const float kDeg = 3.14159265358979323846f / 180.0f;
    if (std::fabs(el) >= halfDeg * kDeg)
        return;
    for (int i = 0; i < 3; ++i)
        out[i] *= amount;
}

} // namespace

void BGFXView::envRadianceProcedural(const float d[3], float out[3]) const
{
    // Every preset below is scaled so the sphere integrates to the same
    // mean radiance as Gradient, 0.565 in luminance. That is deliberate
    // and load bearing: it means choosing a preset changes contrast and
    // structure WITHOUT changing how bright the scene comes out, so the
    // exposure that suited one suits all five. The constants were
    // measured by integrating each shape over a uniform sphere; edit a
    // shape and its constant is stale.
    float z = bx::clamp(d[2], -1.0f, 1.0f);
    float el = std::asin(z);
    float az = std::atan2(d[1], d[0]);
    float t = el / (3.14159265358979323846f * 0.5f);   // -1 nadir .. 1 zenith

    switch (m_envPreset) {
    case 1: {   // Gradient -- what this engine had before the others.
        // The ground stays fairly bright: metals reflect the lower
        // hemisphere over most of a model's side faces, and a dark
        // floor reads as black plastic in a CAD view.
        static const float ground[3] = {0.30f, 0.30f, 0.32f};
        static const float horizon[3] = {0.45f, 0.46f, 0.48f};
        static const float sky[3] = {0.60f, 0.66f, 0.76f};
        float g = std::sqrt(std::fabs(z));
        for (int i = 0; i < 3; ++i)
            out[i] = z < 0.0f ? horizon[i] + (ground[i] - horizon[i]) * g
                              : horizon[i] + (sky[i] - horizon[i]) * g;

        struct Lobe {
            float dir[3];       // not normalized
            float power;
            float intensity;
            float color[3];
        };
        static const Lobe lobes[3] = {
            {{0.45f, -0.35f, 0.82f}, 40.0f, 3.0f, {1.0f, 0.98f, 0.92f}},
            {{-0.75f, -0.25f, 0.35f}, 12.0f, 1.0f, {0.75f, 0.8f, 0.9f}},
            {{0.15f, 0.85f, 0.25f}, 25.0f, 1.5f, {0.9f, 0.93f, 1.0f}},
        };
        for (const auto &lobe : lobes) {
            float len = std::sqrt(lobe.dir[0]*lobe.dir[0]
                                  + lobe.dir[1]*lobe.dir[1]
                                  + lobe.dir[2]*lobe.dir[2]);
            float dot = (d[0]*lobe.dir[0] + d[1]*lobe.dir[1]
                         + d[2]*lobe.dir[2]) / len;
            if (dot <= 0.0f)
                continue;
            float s = std::pow(dot, lobe.power) * lobe.intensity;
            for (int i = 0; i < 3; ++i)
                out[i] += lobe.color[i] * s;
        }
        return;
    }
    case 2: {   // Overcast -- bright even sky over dark ground.
        if (t >= 0.0f) {
            out[0] = 0.62f + 0.38f * t;
            out[1] = 0.65f + 0.38f * t;
            out[2] = 0.72f + 0.36f * t;
        }
        else {
            float g = -t;
            out[0] = out[1] = 0.20f - 0.09f * g;
            out[2] = 0.21f - 0.09f * g;
        }
        envHorizon(el, 1.0f, 0.6f, out);
        for (int i = 0; i < 3; ++i)
            out[i] *= 1.19158f;
        return;
    }
    case 3: {   // Sunset -- a low warm sun, deep sky, the widest hue span.
        if (t >= 0.0f) {
            out[0] = 0.55f * (1.0f - t) + 0.05f * t;
            out[1] = 0.30f * (1.0f - t) + 0.07f * t;
            out[2] = 0.18f * (1.0f - t) + 0.20f * t;
        }
        else {
            float g = -t;
            out[0] = 0.14f - 0.09f * g;
            out[1] = 0.10f - 0.06f * g;
            out[2] = 0.09f - 0.05f * g;
        }
        static const EnvBox boxes[2] = {
            // The sun itself: small, hard edged and very bright, which
            // is what puts a specular on a curved surface.
            {25.0f, 4.0f, 5.0f, 4.0f, 90.0f, 0.5f, {1.00f, 0.62f, 0.30f}},
            // Its glow, wide and soft, doing the colour work.
            {25.0f, 10.0f, 40.0f, 22.0f, 1.2f, 1.0f, {1.00f, 0.55f, 0.28f}},
        };
        envAddBoxes(az, el, boxes, 2, out);
        for (int i = 0; i < 3; ++i)
            out[i] *= 2.21629f;
        return;
    }
    case 4: {   // Interior -- one window, a ceiling panel, close walls.
        if (t >= 0.0f) {
            out[0] = 0.20f + 0.06f * t;
            out[1] = 0.19f + 0.055f * t;
            out[2] = 0.175f + 0.05f * t;
        }
        else {
            float g = -t;
            out[0] = 0.14f - 0.05f * g;
            out[1] = 0.125f - 0.045f * g;
            out[2] = 0.105f - 0.04f * g;
        }
        envHorizon(el, 1.2f, 0.5f, out);
        // Piers: something with vertical structure for a mirror to show.
        float pier = 0.5f + 0.5f * std::cos(4.0f * az);
        for (int i = 0; i < 3; ++i)
            out[i] *= 0.78f + 0.22f * pier;
        static const EnvBox boxes[2] = {
            {-50.0f, 18.0f, 17.0f, 24.0f, 20.0f, 0.22f, {0.86f, 0.92f, 1.00f}},
            {120.0f, 62.0f, 22.0f, 8.0f, 6.0f, 0.40f, {1.00f, 0.96f, 0.88f}},
        };
        envAddBoxes(az, el, boxes, 2, out);
        for (int i = 0; i < 3; ++i)
            out[i] *= 0.80033f;
        return;
    }
    default: {  // 0 Studio -- softboxes on a dark surround.
        if (t >= 0.0f) {
            out[0] = 0.15f + 0.07f * t;
            out[1] = 0.155f + 0.075f * t;
            out[2] = 0.17f + 0.09f * t;
        }
        else {
            float g = 1.0f + t;
            out[0] = out[1] = 0.11f + 0.05f * g;
            out[2] = 0.115f + 0.05f * g;
        }
        envHorizon(el, 1.2f, 0.45f, out);
        static const EnvBox boxes[4] = {
            // Key, fill, rim, and a wide strip overhead -- the rig a
            // product photograph is actually lit with.
            {40.0f, 42.0f, 15.0f, 11.0f, 26.0f, 0.30f, {1.00f, 0.98f, 0.94f}},
            {-68.0f, 12.0f, 13.0f, 15.0f, 7.0f, 0.30f, {0.80f, 0.86f, 1.00f}},
            {150.0f, 28.0f, 8.0f, 7.0f, 15.0f, 0.30f, {1.00f, 0.96f, 0.90f}},
            {0.0f, 78.0f, 30.0f, 9.0f, 5.0f, 0.30f, {0.95f, 0.97f, 1.00f}},
        };
        envAddBoxes(az, el, boxes, 4, out);
        for (int i = 0; i < 3; ++i)
            out[i] *= 1.06484f;
        return;
    }
    }
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

void BGFXView::ensureEnvironment()
{
    if (m_envBuilt)
        return;
    m_envBuilt = true;
    if (bgfx::isValid(m_envTex)) {
        bgfx::destroy(m_envTex);
        m_envTex = BGFX_INVALID_HANDLE;
    }
    const auto *caps = bgfx::getCaps();
    if (!(caps->formats[bgfx::TextureFormat::RGBA16F]
            & BGFX_CAPS_FORMAT_TEXTURE_CUBE))
        return;
    m_envTex = bgfx::createTextureCube(kEnvSize, true, 1,
        bgfx::TextureFormat::RGBA16F, 0, nullptr);
    if (!bgfx::isValid(m_envTex))
        return;

    int numMips = 1;
    while (kEnvSize >> numMips)
        ++numMips;
    const uint16_t halfOne = bx::halfFromFloat(1.0f);
    std::vector<uint16_t> texels;
    for (int mip = 0; mip < numMips; ++mip) {
        int size = int(kEnvSize) >> mip;
        float rough = std::min(1.0f, float(mip) / 5.0f);
        float a = rough * rough;
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
                        envRadiance(d, col);
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
                        constexpr int kSamples = 64;
                        float sum[3] = {0.0f, 0.0f, 0.0f};
                        float wsum = 0.0f;
                        for (int i = 0; i < kSamples; ++i) {
                            float u1 = (i + 0.5f) / kSamples;
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
                            float r[3];
                            envRadiance(l, r);
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
                float col[3];
                envRadiance(d, col);
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
    if (!bgfx::isValid(m_progEnvBg) || !bgfx::isValid(m_envTex))
        return;
    // A backdrop wants to be soft: a real one is out of focus, and the
    // blur is also what lets a small bright source bleed into a wide
    // gentle falloff instead of sitting there as a hard rectangle. LOD
    // 2 is 32x32 per face on a 128 cube -- the same softness this
    // always had, at four times the resolution it had it at, which is
    // what stops it blocking up now the presets have edges in them.
    float params[4] = {0.0f, 2.0f, 0.0f,
                       std::max(pbrEnvIntensity, 0.0f)};
    bgfx::setUniform(u_pbrParams, params);
    bgfx::setTexture(1, s_texEnv, m_envTex);
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
