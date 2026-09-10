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

#include <algorithm>
#include <cmath>

#include "Environment.h"

namespace Render {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

void sampleEnvImage(const TextureImage &img, const float d[3], float out[3], bool managed)
{
    float u, v;
    if (img.width >= img.height * 3 / 2) {
        // Equirectangular: azimuth around Z, elevation from Z.
        //
        // MINUS the azimuth, which is the convention every other tool
        // that reads one of these files uses -- Cycles' kernel spells
        // it direction_to_equirectangular, u = 0.5 - atan2(y, x)/2pi,
        // and bakeEnvironment in CyclesScene.cpp already documents
        // that layout because it writes one. A plus here reads the
        // picture MIRRORED: the background faces the wrong way and so
        // does every reflection in it. It went unseen because this is
        // the single sampler behind BOTH engines -- the raster builds
        // its cubemap through it and the path tracer bakes its equirect
        // through it -- so the two agreed with each other while both
        // disagreed with the world. Measured against Blender on the
        // chess set: mean absolute error 45.9 bytes as it was, 4.4 with
        // the environment mirrored back (fcad-probes/blender_chess.py).
        u = 0.5f - std::atan2(d[1], d[0]) / (2.0f * kPi);
        v = std::acos(std::clamp(d[2], -1.0f, 1.0f)) / kPi;
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
    v = std::clamp(v, 0.0f, 1.0f);
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
        y = std::clamp(y, 0, img.height - 1);
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
                     : (managed ? srgbToLinear(lin) : lin * lin);
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

void envRadianceProcedural(int preset, const float d[3], float out[3])
{
    // Every preset below is scaled so the sphere integrates to the same
    // mean radiance as Gradient, 0.565 in luminance. That is deliberate
    // and load bearing: it means choosing a preset changes contrast and
    // structure WITHOUT changing how bright the scene comes out, so the
    // exposure that suited one suits all five. The constants were
    // measured by integrating each shape over a uniform sphere; edit a
    // shape and its constant is stale.
    float z = std::clamp(d[2], -1.0f, 1.0f);
    float el = std::asin(z);
    float az = std::atan2(d[1], d[0]);
    float t = el / (3.14159265358979323846f * 0.5f);   // -1 nadir .. 1 zenith

    switch (preset) {
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
    case 2: {   // Overcast -- a bright dome, weighted to the zenith.
        if (t >= 0.0f) {
            // The sky is concentrated ABOVE rather than spread evenly,
            // and the reason is where the camera looks. Every preset
            // here carries the same mean radiance, so an even sky has
            // to be bright everywhere to reach it -- including the band
            // just over the horizon, which is exactly what fills the
            // frame behind a model. Plaster and the other near-white
            // appearances then have nothing to stand out against.
            //
            // CIE's standard overcast distribution puts the zenith at
            // three times the horizon; this goes further (about eight),
            // which is a stylised sky but a usable backdrop: the same
            // light arrives, from higher up.
            float k = 0.12f + 0.88f * std::pow(std::sin(el), 1.5f);
            // Enough cloud to break the field up. A flat backdrop reads
            // as a missing background, not as weather.
            k *= 1.0f + 0.10f * std::sin(3.0f * az + 2.0f * el)
                              * std::cos(2.0f * az);
            out[0] = 1.00f * k;
            out[1] = 1.03f * k;
            out[2] = 1.10f * k;
        }
        else {
            float g = -t;
            out[0] = out[1] = 0.11f - 0.05f * g;
            out[2] = 0.1122f - 0.05f * g;
        }
        envHorizon(el, 1.0f, 0.55f, out);
        for (int i = 0; i < 3; ++i)
            out[i] *= 1.96290f;
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
    case 5: {   // Light tent -- a box of white panels, seams and all.
        // Bright in BOTH hemispheres, which is the point and what
        // separates it from every other preset here. A standing
        // cylinder's wall reflects the half of the sphere BELOW the
        // horizon, and in a studio or a room that half is floor and
        // therefore dark, so the wall of a machined billet goes dead
        // however the rest is lit.
        if (t >= 0.0f) {
            out[0] = 0.62f + 0.20f * t;
            out[1] = 0.63f + 0.20f * t;
            out[2] = 0.65f + 0.20f * t;
        }
        else {
            float g = -t;
            out[0] = out[1] = 0.72f - 0.10f * g;
            out[2] = 0.73f - 0.10f * g;
        }
        // Two lit panels and two dark ones, all the way round and at
        // every elevation. Brightness alone is not enough: a groove
        // tilts its normal, and a tilt that only swings the reflection
        // in AZIMUTH shows nothing unless there is something to swing
        // across. Below the horizon most of all -- that is where a
        // vertical groove on a vertical wall looks.
        //
        // TWO cycles, and a plain cosine rather than a narrow seam,
        // because of what the surface does to it. These are rough
        // metals: a roughness of 0.39 integrates a lobe tens of degrees
        // wide, and that lobe is a low-pass filter on the environment.
        // Narrow seams six to a turn measured a 1.43x swing in the
        // environment and 1.01x after the lobe -- gone. Two broad ones
        // survive at 1.90x, which is the whole difference between a
        // brushed wall that reads and one that does not.
        const float panel = 0.5f * (1.0f + std::cos(2.0f * az));
        for (int i = 0; i < 3; ++i)
            out[i] *= 1.0f - 0.75f * panel;
        // The top light, as a smooth power rather than a panel with an
        // edge: a tent diffuses its source, and nothing in here should
        // be sharp enough for a polished surface to clip on.
        const float up = std::max(0.0f, std::sin(el));
        const float lid = 2.0f * up * up * up;
        for (int i = 0; i < 3; ++i)
            out[i] += lid;
        for (int i = 0; i < 3; ++i)
            out[i] *= 0.82700f;
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

void envRadiance(const PBRConfig &pbr, const float d[3], float out[3], bool managed)
{
    if (pbr.envImage && pbr.envImage->width > 0 && pbr.envImage->height > 0
            && pbr.envImage->numComponents > 0) {
        sampleEnvImage(*pbr.envImage, d, out, managed);
        return;
    }
    envRadianceProcedural(pbr.envPreset, d, out);
}

float envBlurAngle(float blur)
{
    blur = std::clamp(blur, 0.0f, 1.0f);
    if (blur <= 0.0f)
        return 0.0f;
    // 45 degrees at one, halved for every eighth of the slider below
    // it. Written from the wide end so the top of the range is the
    // stated number rather than whatever 256 doublings of a texel
    // happen to land on.
    return 0.25f * kPi * std::pow(2.0f, 8.0f * (blur - 1.0f));
}

}  // namespace Render
