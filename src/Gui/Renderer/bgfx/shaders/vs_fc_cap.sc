$input a_position, a_texcoord0
$output v_texcoord0

/*
 * FreeCAD section-cap vertex shader, unclipped variant (single clip
 * plane or concave mode); the body lives in fc_cap_vs.sh.
 */

#include <bgfx_shader.sh>
#include "fc_cap_vs.sh"
