$input v_color0, v_line, v_dist, v_vpos

/*
 * Point sprite distance-field writer.
 * Shares fc_line_sdf_fs.sh; POINT_SDF picks the box distance.
 */

#define POINT_SDF


#include <bgfx_shader.sh>

#include "fc_line_sdf.sh"
#include "fc_line_sdf_fs.sh"
