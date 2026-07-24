$input v_texcoord0

/*
 * Render debugging buffer visualization (docs/RenderDebug.md): routes
 * an intermediate render target to the screen instead of the shaded
 * scene. Submitted as a fullscreen blit into the scene color before the
 * on-top/highlight/overlay passes (BGFXView::submitDebug).
 *
 * u_debugParams: x = mode (1 = linearized depth, 2 = view-space
 *                normal, 3 = AO term, 4 = shadow term),
 *                y = 1 / max linear view depth (depth normalization),
 *                z = shadow state valid this frame
 * Stage 0: prepass normal+depth (octahedral view-space normal +
 *          positive linear view depth, .w = 0 marks background).
 * Stage 1: shadow moments (fc_volume_shadow.sh's fixed stage).
 * Stage 2: the finished AO term.
 */

#include <bgfx_shader.sh>
#include "fc_volume_shadow.sh"

SAMPLER2D(s_texNormalZ, 0);
SAMPLER2D(s_texAO, 2);

uniform vec4 u_debugParams;

// Inverse of the prepass octEncode (fc_prepass_fs.sh).
vec3 octDecode(vec2 f)
{
	vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
	if (n.z < 0.0)
	{
		vec2 sn = vec2(n.x >= 0.0 ? 1.0 : -1.0,
		               n.y >= 0.0 ? 1.0 : -1.0);
		n.xy = (vec2_splat(1.0) - abs(n.yx)) * sn;
	}
	return normalize(n);
}

// View-space ray of a screen pixel — fc_volume.sh's volRay, duplicated
// so this pass does not pull in the volumetric uniform block.
void debugRay(vec2 uv, out vec3 origin, out vec3 dir)
{
	vec2 ndc = uv * 2.0 - vec2_splat(1.0);
	if (u_proj[2][3] != 0.0)
	{
		origin = vec3_splat(0.0);
		dir = normalize(vec3((ndc.x + u_proj[2][0]) / u_proj[0][0],
		                     (ndc.y + u_proj[2][1]) / u_proj[1][1],
		                     -1.0));
	}
	else
	{
		origin = vec3((ndc.x - u_proj[3][0]) / u_proj[0][0],
		              (ndc.y - u_proj[3][1]) / u_proj[1][1],
		              0.0);
		dir = vec3(0.0, 0.0, -1.0);
	}
}

void main()
{
	vec4 nz = texture2D(s_texNormalZ, v_texcoord0);
	float mode = u_debugParams.x;
	if (nz.w < 0.5)
	{
		// Background (no prepass fragment): black.
		gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
	vec3 rgb;
	if (mode < 1.5)         // linearized depth, near = white
		rgb = vec3_splat(
			1.0 - clamp(nz.z * u_debugParams.y, 0.0, 1.0));
	else if (mode < 2.5)    // view-space normal
		rgb = octDecode(nz.xy) * 0.5 + vec3_splat(0.5);
	else if (mode < 3.5)    // ambient occlusion term
		rgb = vec3_splat(texture2D(s_texAO, v_texcoord0).x);
	else                    // shadow term (lit = white)
	{
		float vis = 1.0;
		if (u_debugParams.z > 0.5)
		{
			vec3 origin, dir;
			debugRay(v_texcoord0, origin, dir);
			vec3 p = u_proj[2][3] != 0.0
				? dir * (nz.z / max(1.0e-6, -dir.z))
				: origin + dir * nz.z;
			vis = shadowVis(p);
		}
		rgb = vec3_splat(vis);
	}
	gl_FragColor = vec4(rgb, 1.0);
}
