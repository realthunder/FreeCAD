$input v_normal, v_color0, v_vpos

/*
 * FreeCAD CAD-mesh fragment shader, unclipped variant (no discard, keeps
 * early-Z); the shading body lives in fc_mesh_fs.sh.
 */

#include <bgfx_shader.sh>
#include "fc_mesh_fs.sh"
