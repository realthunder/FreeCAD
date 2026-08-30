$input v_color0, v_line, v_vpos, v_wpos

/*
 * Clip-plane (section) variant of the line distance-field writer.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_line_sdf.sh"
#include "fc_line_sdf_fs.sh"
