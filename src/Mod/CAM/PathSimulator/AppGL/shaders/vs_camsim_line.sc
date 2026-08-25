$input a_position, a_normal, a_texcoord0
$output v_index

/*
 * VertShader3DLine's successor: the tool-path line, drawn as
 * camera-facing quads because the backend cannot draw wide lines
 * (the GL path's glLineWidth(2)). Each segment is two triangles;
 * every vertex carries its own endpoint in a_position, the OTHER
 * endpoint in a_normal, and (segment index, side) in a_texcoord0.
 * The offset is half the line width (u_simParams.w, in pixels) along
 * the screen-space perpendicular of the segment; the far endpoint's
 * vertices come with the side sign flipped, so both ends offset to
 * the same screen side. The segment index colors done-vs-remaining
 * against u_simParams.z in the fragment shader (exact below 2^24
 * segments).
 */

#include <bgfx_shader.sh>

uniform vec4 u_simParams;

void main()
{
	vec4 c0 = mul(u_viewProj, vec4(a_position, 1.0));
	vec4 c1 = mul(u_viewProj, vec4(a_normal, 1.0));
	vec2 dirPx = (c1.xy / c1.w - c0.xy / c0.w) * u_viewRect.zw;
	float len = length(dirPx);
	vec2 perp = len > 0.0001 ? vec2(-dirPx.y, dirPx.x) / len
	                         : vec2(0.0, 0.0);
	// Half-width pixels to NDC (2 NDC units across the viewport),
	// scaled by w so the offset survives the perspective division.
	vec2 offset = perp * (u_simParams.w * a_texcoord0.y)
	    * (2.0 / u_viewRect.zw);
	c0.xy += offset * c0.w;
	gl_Position = c0;
	v_index = a_texcoord0.x;
}
