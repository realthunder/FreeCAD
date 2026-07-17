$input a_position, a_normal, a_color0, a_texcoord0
$output v_normal, v_color0, v_texcoord0

/*
 * Textured variant of the CAD-mesh vertex shader: carries the (texture
 * matrix transformed) texture coordinates from the second vertex stream.
 */

#define TEXTURE

#include <bgfx_shader.sh>
#include "fc_mesh_vs.sh"
