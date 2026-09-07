/*
 * Naming one element of a matrix, on every backend.
 *
 * GLSL indexes a matrix by COLUMN -- m[i] is a column, m[i][j] the j-th
 * entry of it -- while HLSL, Metal, SPIR-V and WGSL index it by ROW.
 * bgfx states that difference as BGFX_SHADER_MATRIX_COLUMN_MAJOR, and it
 * makes mul() agree across the two, but not subscripting: written out,
 * m[i][j] names transposed elements on the two halves of the split.
 *
 * That is a silent fault rather than a compile error, and an expensive
 * one. Every shader here tells a perspective camera from an
 * orthographic one by u_proj[2][3] -- the w row's z entry, -1 or 0 --
 * and off GL that subscript reads the z row's w entry instead, which is
 * never 0. So an orthographic camera was taken for a perspective one on
 * Metal, and the ray fan built from the wrong entries turned the
 * environment background into a starburst.
 *
 * FC_MTX names the element the CPU wrote at float[16] index
 * 4 * _i + _j, which is what every call site here means: _i and _j read
 * exactly as the GLSL subscript they replace.
 */

#ifndef FC_MATRIX_SH
#define FC_MATRIX_SH

#if BGFX_SHADER_MATRIX_COLUMN_MAJOR
#	define FC_MTX(_m, _i, _j) _m[_i][_j]
#else
#	define FC_MTX(_m, _i, _j) _m[_j][_i]
#endif // BGFX_SHADER_MATRIX_COLUMN_MAJOR

#endif // FC_MATRIX_SH
