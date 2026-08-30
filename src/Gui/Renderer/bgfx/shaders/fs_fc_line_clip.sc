$input v_color0, v_wpos, v_line

/*
 * Clip-plane (section) variant of the line fragment shader. Selected
 * only for draws with active clip planes, so the unclipped scene does
 * not pay for the clip discard.
 */

#define LINE_AA
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_flat_fs.sh"
