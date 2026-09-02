$input v_color0, v_line

/*
 * FreeCAD line fragment shader, unclipped variant: the flat body with
 * analytic line coverage (LINE_AA). Points keep fs_fc_flat, which has
 * neither the extra varying nor the coverage cost -- a point sprite is
 * a square, not a strip whose apparent weight turns with the model.
 */

#define LINE_AA

#include <bgfx_shader.sh>
#include "fc_flat_fs.sh"
