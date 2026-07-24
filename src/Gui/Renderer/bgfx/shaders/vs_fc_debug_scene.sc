$input a_position, a_texcoord0
$output v_texcoord0

/*
 * Debug scene re-render vertex shader, unclipped variant (docs/
 * RenderDebug.md modes 6/8): rasterizes a scene triangle draw into the
 * debug scene target, carrying the texcoord for the UV mode. Meshes
 * without a texcoord attribute read (0,0). The body lives in
 * fc_debug_scene_vs.sh.
 */

#include <bgfx_shader.sh>
#include "fc_debug_scene_vs.sh"
