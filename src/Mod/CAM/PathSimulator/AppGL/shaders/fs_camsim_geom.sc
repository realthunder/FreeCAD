$input v_normal, v_position

/*
 * FragShaderGeom: the G-buffer write -- color, view-space position,
 * view-space normal (the GL path's RGB32F attachments become RGBA32F,
 * bgfx has no 3-channel float targets), plus the engine's prepass
 * packing in attachment 3 for the AO effect service: octahedral
 * viewer-facing normal + positive linear view depth, .w = 1 marking
 * written fragments (the pass clear leaves the background at 0).
 * octEncode matches the engine's fc_prepass_fs.sh -- the AO shaders
 * decode with its fc_octDecode.
 */

#include <bgfx_shader.sh>

uniform vec4 u_simObjectColor;

vec2 octEncode(vec3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	if (n.z < 0.0)
	{
		vec2 sn = vec2(n.x >= 0.0 ? 1.0 : -1.0,
		               n.y >= 0.0 ? 1.0 : -1.0);
		return (vec2_splat(1.0) - abs(n.yx)) * sn;
	}
	return n.xy;
}

void main()
{
	vec3 n = normalize(v_normal);
	gl_FragData[0] = vec4(u_simObjectColor.rgb, 1.0);
	gl_FragData[1] = vec4(v_position, 0.0);
	gl_FragData[2] = vec4(n, 0.0);
	// Face the normal toward the viewer for the AO input, as the
	// engine prepass does (view ray = position for perspective, the
	// view axis for orthographic).
	vec3 viewdir = u_proj[2][3] != 0.0 ? v_position : vec3(0.0, 0.0, -1.0);
	vec3 an = dot(n, viewdir) > 0.0 ? -n : n;
	gl_FragData[3] = vec4(octEncode(an), -v_position.z, 1.0);
}
