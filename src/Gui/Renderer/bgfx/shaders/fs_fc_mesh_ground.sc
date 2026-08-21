$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex

/*
 * Ground variant of the CAD-mesh fragment shader: the same shading,
 * faded out toward the quad's rim (GROUND_FADE in fc_mesh_fs.sh).
 *
 * A variant rather than a flag on the shared program, because the
 * ground rides the SCENE's mesh program: a fade uniform there would be
 * one more global that every scene draw has to clear, which is the trap
 * submitShadowGround already spends a dozen lines avoiding. Nothing but
 * the ground can reach this program, so nothing but the ground can
 * carry its uniforms.
 */

#define GROUND_FADE

#include <bgfx_shader.sh>
#include "fc_mesh_fs.sh"
