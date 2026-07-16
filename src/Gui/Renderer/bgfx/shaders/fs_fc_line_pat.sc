$input v_color0, v_dist

/*
 * Patterned (stipple) line fragment shader: the flat shader body plus a
 * glLineStipple-style pattern discard. Selected only for draws with a
 * non-solid line pattern, so solid lines keep early-Z.
 */

#define LINE_PATTERN

#include <bgfx_shader.sh>
#include "fc_flat_fs.sh"
