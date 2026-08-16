/****************************************************************************
 *   Copyright (c) 2026 Zheng Lei (realthunder) <realthunder.dev@gmail.com> *
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
 ****************************************************************************/

#ifndef RENDERER_SIMD4_H
#define RENDERER_SIMD4_H

/// Four floats at a time -- the smallest vector width every tier of this
/// project actually has.
///
/// KEY: **128 bits, deliberately, and not a byte more.** SSE2 is baseline
/// on x86-64, NEON is baseline on arm64, and WebAssembly's only SIMD
/// proposal that shipped is SIMD128. Widening to AVX2 would double the
/// desktop throughput and split the browser tier off onto a different
/// code path, which is the one thing the software occlusion buffer
/// exists to avoid (Gui/Renderer/MaskedOcclusion.h): a mechanism that
/// behaves differently in Chrome is not the mechanism that was measured
/// here. There is no runtime dispatch for the same reason -- one width,
/// compiled everywhere, with a scalar fallback that produces the same
/// answers where the intrinsics are missing.
///
/// WARNING: This is not a general-purpose math library and should not grow
/// into one. It carries exactly the operations
/// MaskedDepth::filterBatch needs, and every one of them maps to a single
/// instruction on at least two of the three backends. `floor` is the
/// exception and is commented where it is defined.
///
/// A lane mask (`M4`) is the native comparison result, all-ones or
/// all-zeros per lane, and is only ever combined and reduced to four
/// bits. Nothing here selects or blends, because the caller decides its
/// four outcomes with a four-iteration scalar loop after the vector work
/// is done -- which is cheaper than building the answer in registers and
/// far easier to read.

#include <cmath>
#include <cstdint>

#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define RENDER_SIMD4_WASM 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define RENDER_SIMD4_SSE 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
#define RENDER_SIMD4_NEON 1
#else
#define RENDER_SIMD4_SCALAR 1
#endif

namespace Render
{

/// Whether the four lanes below are one instruction or four. Reported so
/// that a measurement can say which it measured.
inline bool simd4Native()
{
#if defined(RENDER_SIMD4_SCALAR)
    return false;
#else
    return true;
#endif
}

/// The name of the backend in use, for the same reason.
inline const char *simd4Name()
{
#if defined(RENDER_SIMD4_WASM)
    return "simd128";
#elif defined(RENDER_SIMD4_SSE)
    return "sse2";
#elif defined(RENDER_SIMD4_NEON)
    return "neon";
#else
    return "scalar";
#endif
}

// ---------------------------------------------------------------------
// The lane mask
// ---------------------------------------------------------------------

struct M4
{
#if defined(RENDER_SIMD4_WASM)
    v128_t v;
#elif defined(RENDER_SIMD4_SSE)
    __m128 v;
#elif defined(RENDER_SIMD4_NEON)
    uint32x4_t v;
#else
    uint32_t v[4];
#endif

    /// The four lanes as bits 0..3, lane 0 lowest.
    int bits() const
    {
#if defined(RENDER_SIMD4_WASM)
        return wasm_i32x4_bitmask(v);
#elif defined(RENDER_SIMD4_SSE)
        return _mm_movemask_ps(v);
#elif defined(RENDER_SIMD4_NEON)
        const uint32_t weights[4] = {1, 2, 4, 8};
        const uint32x4_t b = vandq_u32(v, vld1q_u32(weights));
#if defined(__aarch64__)
        return int(vaddvq_u32(b));
#else
        const uint32x2_t t = vorr_u32(vget_low_u32(b), vget_high_u32(b));
        return int(vget_lane_u32(t, 0) | vget_lane_u32(t, 1));
#endif
#else
        return int((v[0] & 1u) | (v[1] & 1u) << 1 | (v[2] & 1u) << 2
                   | (v[3] & 1u) << 3);
#endif
    }
};

inline M4 operator&(M4 a, M4 b)
{
    M4 r;
#if defined(RENDER_SIMD4_WASM)
    r.v = wasm_v128_and(a.v, b.v);
#elif defined(RENDER_SIMD4_SSE)
    r.v = _mm_and_ps(a.v, b.v);
#elif defined(RENDER_SIMD4_NEON)
    r.v = vandq_u32(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i)
        r.v[i] = a.v[i] & b.v[i];
#endif
    return r;
}

inline M4 operator|(M4 a, M4 b)
{
    M4 r;
#if defined(RENDER_SIMD4_WASM)
    r.v = wasm_v128_or(a.v, b.v);
#elif defined(RENDER_SIMD4_SSE)
    r.v = _mm_or_ps(a.v, b.v);
#elif defined(RENDER_SIMD4_NEON)
    r.v = vorrq_u32(a.v, b.v);
#else
    for (int i = 0; i < 4; ++i)
        r.v[i] = a.v[i] | b.v[i];
#endif
    return r;
}

// ---------------------------------------------------------------------
// Four floats
// ---------------------------------------------------------------------

struct F4
{
#if defined(RENDER_SIMD4_WASM)
    v128_t v;
#elif defined(RENDER_SIMD4_SSE)
    __m128 v;
#elif defined(RENDER_SIMD4_NEON)
    float32x4_t v;
#else
    float v[4];
#endif

    F4() = default;

    /// The same value in all four lanes.
    explicit F4(float s)
    {
#if defined(RENDER_SIMD4_WASM)
        v = wasm_f32x4_splat(s);
#elif defined(RENDER_SIMD4_SSE)
        v = _mm_set1_ps(s);
#elif defined(RENDER_SIMD4_NEON)
        v = vdupq_n_f32(s);
#else
        v[0] = v[1] = v[2] = v[3] = s;
#endif
    }

    /// WARNING: \a p must be 16-byte aligned. Every caller in this project
    /// loads from an `alignas(16)` stack array, so an unaligned form
    /// would be dead code that still has to be right.
    static F4 load(const float *p)
    {
        F4 r;
#if defined(RENDER_SIMD4_WASM)
        r.v = wasm_v128_load(p);
#elif defined(RENDER_SIMD4_SSE)
        r.v = _mm_load_ps(p);
#elif defined(RENDER_SIMD4_NEON)
        r.v = vld1q_f32(p);
#else
        for (int i = 0; i < 4; ++i)
            r.v[i] = p[i];
#endif
        return r;
    }

    void store(float *p) const
    {
#if defined(RENDER_SIMD4_WASM)
        wasm_v128_store(p, v);
#elif defined(RENDER_SIMD4_SSE)
        _mm_store_ps(p, v);
#elif defined(RENDER_SIMD4_NEON)
        vst1q_f32(p, v);
#else
        for (int i = 0; i < 4; ++i)
            p[i] = v[i];
#endif
    }

    float lane(int i) const
    {
        alignas(16) float t[4];
        store(t);
        return t[i];
    }
};

#if defined(RENDER_SIMD4_SCALAR)
#define RENDER_SIMD4_BINOP(name, op)                                          \
    inline F4 name(F4 a, F4 b)                                                \
    {                                                                         \
        F4 r;                                                                 \
        for (int i = 0; i < 4; ++i)                                           \
            r.v[i] = a.v[i] op b.v[i];                                        \
        return r;                                                             \
    }
RENDER_SIMD4_BINOP(operator+, +)
RENDER_SIMD4_BINOP(operator-, -)
RENDER_SIMD4_BINOP(operator*, *)
RENDER_SIMD4_BINOP(operator/, /)
#undef RENDER_SIMD4_BINOP

#define RENDER_SIMD4_CMP(name, op)                                            \
    inline M4 name(F4 a, F4 b)                                                \
    {                                                                         \
        M4 r;                                                                 \
        for (int i = 0; i < 4; ++i)                                           \
            r.v[i] = (a.v[i] op b.v[i]) ? 0xFFFFFFFFu : 0u;                   \
        return r;                                                             \
    }
RENDER_SIMD4_CMP(cmpLt, <)
RENDER_SIMD4_CMP(cmpLe, <=)
RENDER_SIMD4_CMP(cmpGt, >)
RENDER_SIMD4_CMP(cmpGe, >=)
#undef RENDER_SIMD4_CMP

inline F4 min(F4 a, F4 b)
{
    F4 r;
    for (int i = 0; i < 4; ++i)
        r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return r;
}
inline F4 max(F4 a, F4 b)
{
    F4 r;
    for (int i = 0; i < 4; ++i)
        r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return r;
}
inline F4 abs(F4 a)
{
    F4 r;
    for (int i = 0; i < 4; ++i)
        r.v[i] = a.v[i] < 0.0f ? -a.v[i] : a.v[i];
    return r;
}
inline F4 floor(F4 a)
{
    F4 r;
    for (int i = 0; i < 4; ++i)
        r.v[i] = std::floor(a.v[i]);
    return r;
}

#else  // a real instruction set

#if defined(RENDER_SIMD4_WASM)
#define RENDER_SIMD4_WRAP1(name, intrin)                                      \
    inline F4 name(F4 a)                                                      \
    {                                                                         \
        F4 r;                                                                 \
        r.v = intrin(a.v);                                                    \
        return r;                                                             \
    }
#define RENDER_SIMD4_WRAP2(name, intrin)                                      \
    inline F4 name(F4 a, F4 b)                                                \
    {                                                                         \
        F4 r;                                                                 \
        r.v = intrin(a.v, b.v);                                               \
        return r;                                                             \
    }
#define RENDER_SIMD4_WRAPC(name, intrin)                                      \
    inline M4 name(F4 a, F4 b)                                                \
    {                                                                         \
        M4 r;                                                                 \
        r.v = intrin(a.v, b.v);                                               \
        return r;                                                             \
    }
RENDER_SIMD4_WRAP2(operator+, wasm_f32x4_add)
RENDER_SIMD4_WRAP2(operator-, wasm_f32x4_sub)
RENDER_SIMD4_WRAP2(operator*, wasm_f32x4_mul)
RENDER_SIMD4_WRAP2(operator/, wasm_f32x4_div)
RENDER_SIMD4_WRAP2(min, wasm_f32x4_min)
RENDER_SIMD4_WRAP2(max, wasm_f32x4_max)
RENDER_SIMD4_WRAP1(abs, wasm_f32x4_abs)
RENDER_SIMD4_WRAP1(floor, wasm_f32x4_floor)
RENDER_SIMD4_WRAPC(cmpLt, wasm_f32x4_lt)
RENDER_SIMD4_WRAPC(cmpLe, wasm_f32x4_le)
RENDER_SIMD4_WRAPC(cmpGt, wasm_f32x4_gt)
RENDER_SIMD4_WRAPC(cmpGe, wasm_f32x4_ge)

#elif defined(RENDER_SIMD4_SSE)
#define RENDER_SIMD4_WRAP2(name, intrin)                                      \
    inline F4 name(F4 a, F4 b)                                                \
    {                                                                         \
        F4 r;                                                                 \
        r.v = intrin(a.v, b.v);                                               \
        return r;                                                             \
    }
#define RENDER_SIMD4_WRAPC(name, intrin)                                      \
    inline M4 name(F4 a, F4 b)                                                \
    {                                                                         \
        M4 r;                                                                 \
        r.v = intrin(a.v, b.v);                                               \
        return r;                                                             \
    }
RENDER_SIMD4_WRAP2(operator+, _mm_add_ps)
RENDER_SIMD4_WRAP2(operator-, _mm_sub_ps)
RENDER_SIMD4_WRAP2(operator*, _mm_mul_ps)
RENDER_SIMD4_WRAP2(operator/, _mm_div_ps)
RENDER_SIMD4_WRAP2(min, _mm_min_ps)
RENDER_SIMD4_WRAP2(max, _mm_max_ps)
RENDER_SIMD4_WRAPC(cmpLt, _mm_cmplt_ps)
RENDER_SIMD4_WRAPC(cmpLe, _mm_cmple_ps)
RENDER_SIMD4_WRAPC(cmpGt, _mm_cmpgt_ps)
RENDER_SIMD4_WRAPC(cmpGe, _mm_cmpge_ps)

inline F4 abs(F4 a)
{
    F4 r;
    r.v = _mm_andnot_ps(_mm_set1_ps(-0.0f), a.v);
    return r;
}

/// WARNING: SSE2 has no rounding instruction -- `roundps` arrived with
/// SSE4.1 and using it would mean either a build-time floor for the
/// whole application or a runtime dispatch, and this header refuses
/// both (see the file comment). The truncate-and-correct sequence below
/// is four instructions and is exact for |a| < 2^31, which the caller
/// guarantees with its guard band; outside that range it returns \a a
/// unchanged rather than the garbage `cvttps` would produce, because a
/// wrong answer here has to stay a *conservative* wrong answer.
inline F4 floor(F4 a)
{
    const __m128 big = _mm_set1_ps(8388608.0f);  // 2^23, no fraction above
    const __m128 mag = _mm_andnot_ps(_mm_set1_ps(-0.0f), a.v);
    const __m128 huge = _mm_cmpge_ps(mag, big);
    const __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(a.v));
    // cvttps rounds towards zero, so it is one too high for negatives
    // with a fractional part.
    const __m128 adj = _mm_and_ps(_mm_cmpgt_ps(t, a.v), _mm_set1_ps(1.0f));
    const __m128 f = _mm_sub_ps(t, adj);
    F4 r;
    r.v = _mm_or_ps(_mm_and_ps(huge, a.v), _mm_andnot_ps(huge, f));
    return r;
}

#else  // NEON
#define RENDER_SIMD4_WRAP2(name, intrin)                                      \
    inline F4 name(F4 a, F4 b)                                                \
    {                                                                         \
        F4 r;                                                                 \
        r.v = intrin(a.v, b.v);                                               \
        return r;                                                             \
    }
#define RENDER_SIMD4_WRAPC(name, intrin)                                      \
    inline M4 name(F4 a, F4 b)                                                \
    {                                                                         \
        M4 r;                                                                 \
        r.v = intrin(a.v, b.v);                                               \
        return r;                                                             \
    }
RENDER_SIMD4_WRAP2(operator+, vaddq_f32)
RENDER_SIMD4_WRAP2(operator-, vsubq_f32)
RENDER_SIMD4_WRAP2(operator*, vmulq_f32)
RENDER_SIMD4_WRAP2(min, vminq_f32)
RENDER_SIMD4_WRAP2(max, vmaxq_f32)
RENDER_SIMD4_WRAPC(cmpLt, vcltq_f32)
RENDER_SIMD4_WRAPC(cmpLe, vcleq_f32)
RENDER_SIMD4_WRAPC(cmpGt, vcgtq_f32)
RENDER_SIMD4_WRAPC(cmpGe, vcgeq_f32)

inline F4 operator/(F4 a, F4 b)
{
    F4 r;
#if defined(__aarch64__)
    r.v = vdivq_f32(a.v, b.v);
#else
    // ARMv7 has only a reciprocal estimate; two Newton steps bring it to
    // within an ulp, which is as close as this file needs -- the caller
    // treats every projected coordinate as approximate and guards on it.
    float32x4_t e = vrecpeq_f32(b.v);
    e = vmulq_f32(vrecpsq_f32(b.v, e), e);
    e = vmulq_f32(vrecpsq_f32(b.v, e), e);
    r.v = vmulq_f32(a.v, e);
#endif
    return r;
}

inline F4 abs(F4 a)
{
    F4 r;
    r.v = vabsq_f32(a.v);
    return r;
}

inline F4 floor(F4 a)
{
    F4 r;
#if defined(__aarch64__)
    r.v = vrndmq_f32(a.v);
#else
    // The same truncate-and-correct as SSE2 -- see the note there.
    const float32x4_t big = vdupq_n_f32(8388608.0f);
    const uint32x4_t huge = vcgeq_f32(vabsq_f32(a.v), big);
    const float32x4_t t = vcvtq_f32_s32(vcvtq_s32_f32(a.v));
    const float32x4_t adj =
            vreinterpretq_f32_u32(vandq_u32(vcgtq_f32(t, a.v),
                                            vreinterpretq_u32_f32(vdupq_n_f32(1.0f))));
    r.v = vbslq_f32(huge, a.v, vsubq_f32(t, adj));
#endif
    return r;
}
#endif

#undef RENDER_SIMD4_WRAP1
#undef RENDER_SIMD4_WRAP2
#undef RENDER_SIMD4_WRAPC

#endif  // RENDER_SIMD4_SCALAR

/// -floor(-a). A separate instruction exists on two of the three
/// backends and is not worth a fourth spelling of the same guard.
inline F4 ceil(F4 a)
{
    return F4(0.0f) - floor(F4(0.0f) - a);
}

}  // namespace Render

#endif  // RENDERER_SIMD4_H
