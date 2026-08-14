$input a_position, a_normal, a_color0, a_color1, a_color2, a_texcoord0
$output v_normal, v_color0, v_color1, v_color2, v_texcoord0, v_vpos, v_wpos, v_opos, v_onrm

/*
 * Textured + clip-plane (section) variant of the CAD-mesh vertex shader.
 */

#define TEXTURE
#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
