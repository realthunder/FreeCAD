$input v_texcoord0

/*
 * Volumetric raymarch fragment body; lives in an include so the
 * user volume-stage splice can assemble program variants around it
 * (docs/RenderEngine.md §5.11).
 */

#include <bgfx_shader.sh>
#include "fc_volume_fs.sh"
