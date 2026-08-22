$input v_normal, v_vpos

/*
 * Ground variant of the depth+normal prepass: the same encode, with the
 * quad's faded rim discarded so it occupies no space it does not fill
 * (fc_ground_fade.sh).
 */

#define GROUND_FADE

#include <bgfx_shader.sh>
#include "fc_prepass_fs.sh"
