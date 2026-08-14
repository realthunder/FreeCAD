$input a_position, a_normal, a_color0, a_color1, a_color2
$output v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm

/*
 * FreeCAD CAD-mesh vertex shader, unclipped variant; the body lives in
 * fc_mesh_vs.sh.
 */

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
