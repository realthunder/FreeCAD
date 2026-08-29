$input v_normal, v_color0, v_color1, v_color2, v_vpos, v_opos, v_onrm, v_findex

/*
 * Glass body shading (vs_fc_mesh pair): screen-space refraction of the
 * scene-color copy — the sample offset follows the IOR-refracted view
 * direction over the body thickness (the glass front/back interval
 * depth targets), so flat panes viewed head-on sample straight through
 * while curved or oblique glass displaces — with per-channel
 * Beer-Lambert absorption tinted by the material diffuse over the same
 * thickness, Fresnel-blended environment reflection (f0 from the IOR,
 * roughness picks the prefiltered mip) and a sun glint from the scene
 * light. Roughness also frosts the transmission: the refracted sample
 * is spread over a disc that grows with roughness and thickness, and a
 * share of the light comes back diffusely (the whitish body of etched
 * glass), so a frosted pane blurs and pales what is behind it.
 *
 * u_glassParams: x = index of refraction, y = absorption density
 *                (1/world units, resolved by the backend), z = 0..1
 *                roughness, w > 0.5 = the prepass viewZ is bound for
 *                the refraction depth reject
 * u_matColor   : glass diffuse — absorption of the complement
 * u_lightDir   : scene light (w > 0.5 = present), view space
 */

#include <bgfx_shader.sh>

SAMPLER2D(s_texScene, 0);
SAMPLERCUBE(s_texEnv, 1);
SAMPLER2D(s_texNormalZ, 2);
SAMPLER2D(s_texGlassFront, 3);
SAMPLER2D(s_texGlassBack, 4);

uniform vec4 u_matColor;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_glassParams;

void main()
{
	vec3 n = normalize(v_normal);
	// Two-sided like the CAD materials: flip toward the viewer.
	vec3 V = u_proj[2][3] != 0.0 ? normalize(-v_vpos)
	                             : vec3(0.0, 0.0, 1.0);
	if (dot(n, V) < 0.0)
		n = -n;

	float ior = max(u_glassParams.x, 1.0);

	// Body thickness along the view at this pixel from the front/back
	// interval (viewZ in .z, .w = validity; both clear to 0).
	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	vec4 gfront = texture2D(s_texGlassFront, uv);
	vec4 gback = texture2D(s_texGlassBack, uv);
	float entry = gfront.w > 0.5 ? gfront.z : 0.0;
	float thick = gback.w > 0.5 ? max(gback.z - entry, 0.0) : 0.0;

	// Screen-space refraction: the entry-refracted view direction,
	// displaced laterally over the thickness and projected to uv
	// (honest single-interface refraction — real-time engines skip
	// the exit interface too).
	vec3 d = -V;
	vec3 T = refract(d, n, 1.0 / ior);
	if (dot(T, T) < 1.0e-6)
		T = d;  // total internal reflection: sample straight through
	vec2 disp = (T.xy - d.xy) * thick;
	float persp = u_proj[2][3] != 0.0
		? 1.0 / max(-v_vpos.z, 1.0e-3) : 1.0;
	vec2 ruv = uv + disp * vec2(u_proj[0][0], u_proj[1][1]) * 0.5
		* persp;

	// Reject offset samples landing on geometry in front of the glass
	// (the water-surface depth reject, 5-tap cross dilated ~2 px so
	// the CAD edge lines straddling the background cannot smear).
	if (u_glassParams.w > 0.5)
	{
		float fragZ = -v_vpos.z * 0.999;
		vec2 o = u_viewTexel.xy * 2.0;
		vec4 p0 = texture2D(s_texNormalZ, ruv);
		vec4 p1 = texture2D(s_texNormalZ, ruv + vec2(o.x, 0.0));
		vec4 p2 = texture2D(s_texNormalZ, ruv - vec2(o.x, 0.0));
		vec4 p3 = texture2D(s_texNormalZ, ruv + vec2(0.0, o.y));
		vec4 p4 = texture2D(s_texNormalZ, ruv - vec2(0.0, o.y));
		if ((p0.w > 0.5 && p0.z < fragZ)
			|| (p1.w > 0.5 && p1.z < fragZ)
			|| (p2.w > 0.5 && p2.z < fragZ)
			|| (p3.w > 0.5 && p3.z < fragZ)
			|| (p4.w > 0.5 && p4.z < fragZ))
			ruv = uv;
	}
	// Rough transmission. A rough interface scatters the refracted
	// ray into a cone, so the scene shows blurred through it: the
	// cone's footprint at the exit grows with the thickness, like the
	// refraction offset does, and a floor of a few texels keeps a thin
	// pane frosted too, and a cap keeps what is behind it legible as
	// blurred shapes rather than dissolving it. Sixteen taps on a
	// golden-angle spiral, rotated per pixel (interleaved gradient
	// noise) so they dither rather than band; the depth reject above
	// was decided at the centre tap and stands for the disc.
	float rough = u_glassParams.z;
	vec3 refr;
	if (rough > 0.001)
	{
		vec2 rad = rough * thick * 0.35
			* vec2(u_proj[0][0], u_proj[1][1]) * 0.5 * persp;
		rad = max(rad, u_viewTexel.xy * rough * 4.0);
		rad = min(rad, vec2_splat(0.02));
		float ang = 6.2831853 * fract(52.9829189
			* fract(dot(gl_FragCoord.xy,
			            vec2(0.06711056, 0.00583715))));
		refr = vec3_splat(0.0);
		for (int i = 0; i < 16; ++i)
		{
			float fi = float(i) + 0.5;
			float a = ang + fi * 2.39996323;
			vec2 o = rad * sqrt(fi / 16.0) * vec2(cos(a), sin(a));
			refr += texture2D(s_texScene, ruv + o).xyz;
		}
		refr *= 1.0 / 16.0;
	}
	else
		refr = texture2D(s_texScene, ruv).xyz;

	// Per-channel Beer-Lambert absorption of the diffuse complement
	// over the thickness.
	vec3 sigma = u_glassParams.y
		* (vec3_splat(1.0) - u_matColor.rgb);
	refr *= exp(-sigma * thick);

	vec3 Vw = normalize(mul(u_invView, vec4(V, 0.0)).xyz);
	vec3 nw = normalize(mul(u_invView, vec4(n, 0.0)).xyz);

	// The share a frosted surface scatters back diffusely, lit by the
	// environment's irradiance (the coarsest prefiltered mip about the
	// normal) and tinted by the material: what makes etched glass read
	// whitish, and what still says "frosted" once the blur is below a
	// pixel.
	if (rough > 0.001)
	{
		vec3 irr = textureCubeLod(s_texEnv, nw, 5.0).xyz;
		refr = mix(refr, irr * u_matColor.rgb, rough * 0.6);
	}

	// Environment reflection, roughness picks the prefiltered mip.
	vec3 rw = reflect(-Vw, nw);
	vec3 refl = textureCubeLod(s_texEnv, rw, rough * 4.0).xyz;

	// Schlick Fresnel with f0 from the IOR.
	float f0 = (ior - 1.0) / (ior + 1.0);
	f0 = f0 * f0;
	float f = 1.0 - max(dot(n, V), 0.0);
	float f2 = f * f;
	float fres = f0 + (1.0 - f0) * f2 * f2 * f;
	vec3 color = mix(refr, refl, fres);

	// Sun glint from the scene light, widened by the roughness.
	if (u_lightDir.w > 0.5)
	{
		vec3 h = normalize(V - u_lightDir.xyz);
		float e = mix(250.0, 16.0, u_glassParams.z);
		color += u_lightColor.rgb
			* (pow(max(dot(n, h), 0.0), e) * 2.0);
	}

	gl_FragColor = vec4(color, 1.0);
}
