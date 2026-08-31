$input v_color0, v_dist, v_line

/*
 * Patterned (stipple) line fragment shader: the flat shader body plus a
 * glLineStipple-style pattern discard, on top of the analytic coverage
 * every line variant carries.
 *
 * Selected only for draws with a non-solid line pattern. That used to
 * be what kept early-Z for solid lines; it no longer is, because
 * fs_fc_line discards its zero-coverage feather fragments and so gives
 * up early-Z too. The discard is kept there deliberately: it is what
 * holds the depth these draws write to the line's own silhouette
 * instead of half a pixel wider on every side, and line fragments are
 * cheap enough that late-Z on them is the smaller price.
 */

#define LINE_PATTERN
#define LINE_AA

#include <bgfx_shader.sh>
#include "fc_flat_fs.sh"
