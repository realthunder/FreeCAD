$input v_normal, v_color0, v_texcoord0, v_vpos

/*
 * Textured variant of the CAD-mesh fragment shader: applies the GL
 * fixed-function texture environment to the lit color.
 */

#define TEXTURE

#include <bgfx_shader.sh>
#include "fc_mesh_fs.sh"
