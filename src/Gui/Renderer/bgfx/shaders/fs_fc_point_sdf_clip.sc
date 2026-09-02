$input v_color0, v_line, v_dist, v_vpos, v_wpos

/*
 * Point sprite distance-field writer, clip-plane variant.
 * Shares fc_line_sdf_fs.sh; POINT_SDF picks the box distance.
 */

#define POINT_SDF
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_line_sdf.sh"
#include "fc_line_sdf_fs.sh"
