$input v_color0, v_wpos, v_dist, v_line

/*
 * Patterned (stipple) + clip-plane variant of the line fragment shader.
 */

#define LINE_PATTERN
#define LINE_AA
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_flat_fs.sh"
