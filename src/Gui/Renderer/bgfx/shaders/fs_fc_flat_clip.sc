$input v_color0, v_wpos

/*
 * Clip-plane (section) variant of the flat line/point fragment shader.
 * Selected only for draws with active clip planes, so the discard here
 * does not cost the unclipped scene its early-Z.
 */

#define CLIP_PLANES

#include <bgfx_shader.sh>
#include "fc_clip.sh"
#include "fc_flat_fs.sh"
