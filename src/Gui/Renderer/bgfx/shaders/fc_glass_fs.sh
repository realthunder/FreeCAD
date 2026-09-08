/*
 * Glass body shading, the shared body of fs_fc_glass.sc (see its header
 * for the pass and the uniforms). Included once plain, paired with
 * vs_fc_mesh, and once more per MaterialX glass with FC_USER_MATERIAL
 * defined and the document's generated material function appended,
 * paired with vs_fc_mesh_tex: there the IOR, roughness, transmission
 * colour, depth and weight, and the shading normal, come from
 * fcUserMaterialInputs per fragment -- a mapped colour or roughness
 * and a normal map draw as the document states them -- and the
 * uniforms the flat pass reads are not consulted, except for the
 * depth-reject flag in u_glassParams.w, which is the frame's.
 */

#include "fc_line_sdf.sh"
#include "fc_matrix.sh"
#include "fc_openpbr.sh"

#ifdef FC_USER_MATERIAL
void fcUserMaterialInputs(inout FcOpenPbr m, FcMtlxGeom g);
#endif

SAMPLER2D(s_texScene, 0);
SAMPLERCUBE(s_texEnv, 1);
SAMPLER2D(s_texNormalZ, 2);
SAMPLER2D(s_texGlassFront, 3);
SAMPLER2D(s_texGlassBack, 4);
SAMPLER2D(s_texLineSdf, 5);
SAMPLER2D(s_texLineSdfAux, 6);

uniform vec4 u_matColor;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_glassParams;
uniform vec4 u_glassTint;

/*
 * Rebuild a decoration's coverage from the resampled distance field.
 *
 * `stored` is RT0's alpha at this pixel; the distance from the line or
 * point CENTRE comes back as radius - stored, in the pixels the field
 * was rasterized at. To hold the drawn thickness while the position
 * follows the warp, that distance has to be converted into POST-lens
 * pixels and thresholded against the decoration's own half width
 * (aux.w).
 *
 * An earlier version stored the distance to the EDGE and skipped the
 * half width, on the reasoning that SDF text stays crisp this way. It
 * does -- but only its ANTIALIASING does. The core still stretches with
 * the field, so lines behind the lens came out magnified exactly as
 * before, just with clean edges. The width has to be thresholded in
 * post-lens pixels, which means it has to be known here.
 *
 * The next version did the conversion with fwidth(stored): the field's
 * own screen gradient says how many screen pixels one field pixel is
 * worth after the lens. TRUE EVERYWHERE BUT THE CORE: the field is an
 * absolute value, folded at the centreline, and finite differences
 * across the fold cancel -- two pixels straddling the centre store the
 * same value and derive a gradient of zero. So the reconstruction died
 * on an alignment lottery exactly at the pixels that matter, and lines
 * came through as broken ~1px shells. Hence jx/jy: the Jacobian of the
 * refraction MAPPING (smooth where the field folds), projected onto
 * the line's own perpendicular axis carried in aux.xy -- how many
 * field pixels one screen step spans in the direction the field falls.
 * A POINT takes the same branch. Its box distance falls along whichever
 * axis dominates -- max() ignores the other -- so the writer sends that
 * axis and a sprite is scaled per-axis like a line. It used to send a
 * zero axis and land in the mean branch below, which made the drawn
 * half extents `s * halfw / |grad u|` and `s * halfw / |grad v|`: a
 * sprite that should be square on screen came out with an aspect equal
 * to the entire local warp anisotropy. That is not the "corner, not a
 * size" the old note claimed -- through an ior-1.6 ball lens a 9px
 * vertex measured 5.6 x 14.5 at r/R 0.81 (scripts/lens_point_aniso.py).
 * The mean branch now only catches a degenerate axis.
 *
 * aux.z answers "is this decoration behind the glass", in two forms the
 * writer picks between and the SIGN tells apart:
 *
 *   > 0  the decoration's own texel had glass and the writer already
 *        proved it is behind that entry. The value is the CLEARANCE.
 *        Nothing to decide here.
 *   < 0  no glass over its texel, so the writer could not decide --
 *        this sample was taken wherever REFRACTION landed. The value
 *        is -viewZ and the call is made here, against this pixel's own
 *        entry depth, with a relative epsilon sized to 16F's step.
 *
 * The epsilon governs only the second form now, which is what it was
 * always meant for. It used to govern both, and that erased things: it
 * and the 16F step both scale with the CAMERA's view depth, which
 * fitAll takes from the whole document, so a small glass part in a
 * large assembly lost every decoration near its entry surface -- 3.7mm
 * of it in a 3m document, the entire body in a 30m one, and zooming in
 * did not help because an orthographic zoom does not move the camera
 * (scripts/glass_entry_epsilon.py).
 *
 * An untouched texel decodes to the full radius and returns through
 * the support guard, which also caps what compression the method can
 * follow: past the support there is no data, however the Jacobian
 * scales.
 */
float fcLineSdfCoverage(float stored, vec4 aux, float entry,
                        vec2 jx, vec2 jy)
{
	float dc = FC_LINE_SDF_RADIUS - stored;
	if (dc > FC_LINE_SDF_RADIUS - 1.5)
		return 0.0;
	// A positive aux.z is a clearance the writer already verified; only
	// a negative one carries a raw depth this pass has to judge.
	if (aux.z < 0.0
	    && -aux.z <= entry + 1.0e-3 * max(abs(entry), 1.0))
		return 0.0;
	float alen = length(aux.xy);
	float s;
	if (alen > 0.5)
	{
		vec2 p = aux.xy / alen;
		s = length(vec2(dot(p, jx), dot(p, jy)));
	}
	else
		s = 0.5 * (length(vec2(jx.x, jy.x))
		           + length(vec2(jx.y, jy.y)));
	float dpost = dc / max(s, 1.0e-3);
	return clamp(aux.w + 0.5 - dpost, 0.0, 1.0);
}

void main()
{
	vec3 n = normalize(v_normal);
	// Two-sided like the CAD materials: flip toward the viewer.
	vec3 V = FC_MTX(u_proj, 2, 3) != 0.0 ? normalize(-v_vpos)
	                             : vec3(0.0, 0.0, 1.0);
	if (dot(n, V) < 0.0)
		n = -n;

	float ior = max(u_glassParams.x, 1.0);
	float density = u_glassParams.y;
	float rough = u_glassParams.z;
	// The body colour absorbs its complement over the thickness; the
	// tint multiplies the transmitted light once at the surface.
	vec3 absorb = u_matColor.rgb;
	vec3 tint = u_glassTint.rgb;
#ifdef FC_USER_MATERIAL
	// The document states the surface. Its transmission_depth decides
	// what its colour means, exactly as the flat route reads it
	// (docs/MaterialStorage.md sec 17.21): over a positive depth the
	// colour is what survives that path length (density 1 / depth),
	// at zero it is a tint applied once. Its weight, below one, leaves
	// a diffuse share of the base colour (mixed in below); its normal
	// replaces the mesh's for the refraction and the reflection both.
	FcMtlxGeom g = fcMtlxGeomFill(n, v_vpos, v_onrm, v_opos,
	                              v_texcoord0, v_color0.rgb);
	FcOpenPbr m;
	fcOpenPbrDefaults(m);
	fcUserMaterialInputs(m, g);
	// Read before the clamp: its roughness floor is for a microfacet
	// lobe under a delta light, and a glass with roughness 0 is a
	// polished pane, not a frosted one.
	rough = clamp(m.specularRoughness, 0.0, 1.0);
	fcOpenPbrClamp(m);
	n = fcOpenPbrShadingNormal(m, n);
	ior = max(m.specularIor, 1.0);
	bool absorbing = m.transmissionDepth > 0.0;
	density = absorbing ? 1.0 / m.transmissionDepth : 0.0;
	absorb = absorbing ? m.transmissionColor : vec3_splat(1.0);
	tint = absorbing ? vec3_splat(1.0) : m.transmissionColor;
	float weight = m.transmissionWeight;
	vec3 diffuseBase = m.baseColor * m.baseWeight * (1.0 - m.baseMetalness);
#endif

	// Body thickness along the view at this pixel from the front/back
	// interval (viewZ in .z, .w = validity; both clear to 0).
	vec2 uv = gl_FragCoord.xy * u_viewTexel.xy;
	vec4 gfront = texture2D(s_texGlassFront, uv);
	vec4 gback = texture2D(s_texGlassBack, uv);
	float entry = gfront.w > 0.5 ? gfront.z : 0.0;
	float thick = gback.w > 0.5 ? max(gback.z - entry, 0.0) : 0.0;

	// Screen-space refraction: the entry-refracted view direction,
	// displaced laterally over the thickness and projected to uv
	// (honest single-interface refraction -- real-time engines skip
	// the exit interface too).
	vec3 d = -V;
	vec3 T = refract(d, n, 1.0 / ior);
	if (dot(T, T) < 1.0e-6)
		T = d;  // total internal reflection: sample straight through
	vec2 disp = (T.xy - d.xy) * thick;
	float persp = FC_MTX(u_proj, 2, 3) != 0.0
		? 1.0 / max(-v_vpos.z, 1.0e-3) : 1.0;
	vec2 ruv = uv + disp * vec2(FC_MTX(u_proj, 0, 0), FC_MTX(u_proj, 1, 1)) * 0.5
		* persp;

	// The Jacobian of the refraction mapping in field pixels per
	// screen pixel, for the line-field coverage rebuild below. Taken
	// HERE, on the smooth pre-reject mapping and in uniform control
	// flow (derivatives are undefined after divergence); the reject
	// branch resets it to the identity along with the uv, since an
	// unwarped sample is one field pixel per screen pixel by
	// definition -- finite differences across that per-pixel switch
	// would say anything at all.
	vec2 sdfRes = vec2(1.0, 1.0) / u_viewTexel.xy;
	vec2 sdfJx = dFdx(ruv) * sdfRes;
	vec2 sdfJy = dFdy(ruv) * sdfRes;

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
		{
			ruv = uv;
			sdfJx = vec2(1.0, 0.0);
			sdfJy = vec2(0.0, 1.0);
		}
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
	vec3 refr;
	if (rough > 0.001)
	{
		vec2 rad = rough * thick * 0.35
			* vec2(FC_MTX(u_proj, 0, 0), FC_MTX(u_proj, 1, 1))
			* 0.5 * persp;
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
	vec3 sigma = density * (vec3_splat(1.0) - absorb);
	refr *= exp(-sigma * thick) * tint;

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
		refr = mix(refr, irr * absorb * tint, rough * 0.6);
	}

#ifdef FC_USER_MATERIAL
	// A partial transmission weight: OpenPBR mixes the dielectric base
	// (a diffuse slab) with the transmission by it, and the base here
	// is the environment's irradiance under the base colour -- the
	// same coarse lookup the frosted scatter uses. The specular layer
	// above covers both, which is why it is added after.
	if (weight < 1.0)
	{
		vec3 irr = textureCubeLod(s_texEnv, nw, 5.0).xyz;
		refr = mix(irr * diffuseBase, refr, weight);
	}
#endif

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
		float e = mix(250.0, 16.0, rough);
		color += u_lightColor.rgb
			* (pow(max(dot(n, h), 0.0), e) * 2.0);
	}

	// The scene's edges and vertices, seen through the body. They are not in the
	// colour copy above -- resampling a rasterized line magnifies it,
	// and no amount of care in this shader can undo that, because the
	// line was already pixels before the lens saw it. They arrive as a
	// distance field instead, sampled at the SAME refracted uv as the
	// face, so an edge lands exactly where the lens puts the surface it
	// lies on. The coverage is then rebuilt against the refraction
	// mapping's own Jacobian, which is what holds the drawn thickness
	// at the width that was asked for however far the sample has been
	// stretched.
	vec4 ln = texture2D(s_texLineSdf, ruv);
	vec4 laux = texture2D(s_texLineSdfAux, ruv);
	float lcov = fcLineSdfCoverage(ln.a, laux, entry, sdfJx, sdfJy);
	color = mix(color, ln.rgb, lcov * FC_GLASS_LINE_ALPHA);

	gl_FragColor = vec4(color, 1.0);
}
