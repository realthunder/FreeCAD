#ifndef FC_FINISH_SH
#define FC_FINISH_SH

/*
 * Machined surface finish (App::SurfaceFinish) as a procedural pattern.
 *
 * A finish states what was done to the surface -- knurled, brushed,
 * blasted, turned -- with the pitch and depth of the resulting relief in
 * millimetres. This shades it as a perturbed normal, and as roughness
 * once the relief is finer than the pixel it lands in.
 *
 * Three things make it work without a parametrization:
 *
 * - The pattern lives in OBJECT space, so it is attached to the geometry
 *   the way a machining operation is: a moved, scaled or instanced copy
 *   carries the same finish rather than one that swims as it is placed.
 *   v_opos/v_onrm carry the object-space position and normal.
 * - The height field is projected in the face's OWN FRAME when the
 *   geometry could state one -- the plane the tool swept, or the axis
 *   the part was turned about, read off the OCCT surface at tessellation
 *   time and carried per face in u_frameParams. That is what makes a
 *   straight knurl run along a cylinder's axis and turning marks centre
 *   on it. A face with no analytic surface falls back to a TRIPLANAR
 *   projection (the object-space normal weights three axis-aligned
 *   guesses), which is plausible from any view but does not know the
 *   geometry -- and is what every face got before frames existed.
 * - The normal is perturbed by Mikkelsen's surface gradient (2010,
 *   "Bump Mapping Unparametrized Surfaces on the GPU"): the height
 *   field's screen-space derivatives, taken from its ANALYTIC
 *   object-space gradient rather than by differencing samples of the
 *   pattern, rotate the shading normal with no tangent frame, no UV and
 *   no matrix of its own.
 *
 * Every pattern is filtered against the pixel footprint at the point it
 * is evaluated -- a 0.3 mm knurl on a part zoomed to fit is far below
 * one pixel, and an unfiltered pattern would boil into moire the moment
 * the camera moved. What the filter takes away is not simply dropped:
 * relief too fine to resolve still scatters like relief, so its lost
 * slope variance is handed to the roughness (Toksvig's argument).
 *
 * The costs are a uniform-selected branch away on a scene with no
 * finish, which is what lets this sit in the middle of the CAD mesh
 * shader.
 */

// The draw's finish PALETTE, entry 0 being the finish of the draw as a
// whole. x = pattern (App::SurfaceFinish::Pattern; 0 = none, and a value
// this shader does not know shades as none, which is how a scene written
// by a later build degrades), y = pitch and z = depth in millimetres of
// object space, w = the lay angle in radians.
//
// A per-face-finished draw states up to FC_FINISH_PALETTE entries and
// every vertex carries the index of its own (the material stream's third
// slot, v_findex); a uniformly finished one states entry 0 alone and
// every vertex indexes it. Must match Render::MaxFinishPalette, which is
// what the backend creates the uniform with.
#define FC_FINISH_PALETTE 8
uniform vec4 u_finishParams[FC_FINISH_PALETTE];

/// The palette entry a vertex's index names, clamped into the array a
/// mismatched or unbound index cannot then read past.
vec4 fcFinishEntry(float index)
{
	int i = int(clamp(index, 0.0, float(FC_FINISH_PALETTE - 1)));
	return u_finishParams[i];
}

// The draw's PROJECTION FRAME palette: what the pattern above is laid
// out ON. Three vec4 an entry -- (origin, kind), (axis, radius),
// (xdir, spare) -- and the material stream's fourth byte (v_findex.y)
// names a face's own. Must match Render::MaxFramePalette.
//
// kind 0 = unframed, and the frame the shader cannot use is the frame
// it had before this existed: the triplanar projection below. That is
// what an unbound index attribute, an overflowed palette and a face
// whose surface is neither planar nor a surface of revolution all read.
#define FC_FRAME_PALETTE 16
uniform vec4 u_frameParams[FC_FRAME_PALETTE * 3];

#define FC_FRAME_UNFRAMED 0.0
#define FC_FRAME_PLANAR   1.0
#define FC_FRAME_RADIAL   2.0

#define FC_FINISH_KNURL           1.0
#define FC_FINISH_KNURL_STRAIGHT  2.0
#define FC_FINISH_BRUSHED         3.0
#define FC_FINISH_BLASTED         4.0
#define FC_FINISH_TURNED          5.0
#define FC_FINISH_LAST            5.0

#define FC_FINISH_TWOPI 6.2831853
#define FC_FINISH_PI    3.1415927

float fcFinishHash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

/// 1D value noise on a lane of its own: x = value in -0.5..0.5,
/// y = d(value)/dx. Smoothstep interpolation, so the derivative is
/// continuous and can be handed straight to the surface gradient.
vec2 fcFinishNoise1(float x, float lane)
{
	float i = floor(x);
	float f = x - i;
	float a = fcFinishHash(vec2(i, lane));
	float b = fcFinishHash(vec2(i + 1.0, lane));
	float u = f * f * (3.0 - 2.0 * f);
	float du = 6.0 * f * (1.0 - f);
	return vec2(a + (b - a) * u - 0.5, (b - a) * du);
}

/// 2D value noise: x = value in -0.5..0.5, yz = its gradient.
vec3 fcFinishNoise2(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = p - i;
	float a = fcFinishHash(i);
	float b = fcFinishHash(i + vec2(1.0, 0.0));
	float c = fcFinishHash(i + vec2(0.0, 1.0));
	float d = fcFinishHash(i + vec2(1.0, 1.0));
	vec2 u = f * f * (3.0 - 2.0 * f);
	vec2 du = 6.0 * f * (1.0 - f);
	float k1 = b - a;
	float k2 = c - a;
	float k3 = a - b - c + d;
	return vec3(a + k1 * u.x + k2 * u.y + k3 * u.x * u.y - 0.5,
	            du.x * (k1 + k3 * u.y),
	            du.y * (k2 + k3 * u.x));
}

/// The flank slope of one groove train: grooves `pitch` apart and
/// `depth` deep, at `x` millimetres across the lay.
///
/// Phase before amplitude, and the phase is wrapped before it reaches
/// the sine: a 0.15 mm brushed lay a metre from the object origin is
/// some millions of cycles out, and taking the sine of that directly
/// would spend the whole float32 mantissa on the cycle count.
///
/// The profile is a sine sharpened toward a constant flank, which is
/// what a form tool leaves: a real triangle wave would be truer still,
/// but its slope discontinuity at every crest is exactly the kind of
/// edge no amount of filtering here can band-limit.
float fcFinishGroove(float x, float pitch, float depth)
{
	float s = sin(fract(x / pitch) * FC_FINISH_TWOPI);
	s = sign(s) * pow(abs(s), 0.45);
	return s * (FC_FINISH_PI * depth / pitch);
}

/// The gradient of one pattern over one projection plane: the slope of
/// the height field at p (millimetres), in the same units, before the
/// triplanar weighting. ca/sa are the cosine and sine of the lay angle.
vec2 fcFinishPattern(vec2 p, float pattern, float pitch, float depth,
                     float ca, float sa)
{
	// Into the lay frame. The gradient comes back out through the
	// transpose of the same rotation (h(p) = f(R' p) makes grad h =
	// R grad f), which is the last line.
	vec2 q = vec2(p.x * ca + p.y * sa, -p.x * sa + p.y * ca);
	vec2 g;
	if (pattern < 1.5)
	{
		// Diamond knurl: two groove trains crossing at a right angle,
		// each half as deep so the pyramids between them stand `depth`
		// tall. The chain rule through the 45 degree rotation is the
		// 0.7071 on the way in and on the way out.
		float k = 0.70710678;
		float ga = fcFinishGroove((q.x + q.y) * k, pitch, depth * 0.5);
		float gb = fcFinishGroove((q.x - q.y) * k, pitch, depth * 0.5);
		g = vec2((ga + gb) * k, (ga - gb) * k);
	}
	else if (pattern < 2.5)
	{
		// Straight knurl: one train, grooves running along the lay.
		g = vec2(fcFinishGroove(q.x, pitch, depth), 0.0);
	}
	else if (pattern < 3.5)
	{
		// Brushed: scratches running along the lay, random in depth
		// and in where they fade out. Two octaves across the lay for
		// the scratch widths, one very slow one along it so a scratch
		// starts and stops instead of running the whole face.
		float s2 = pitch * 0.37;
		float sl = pitch * 60.0;
		vec2 n1 = fcFinishNoise1(q.x / pitch, 0.0);
		vec2 n2 = fcFinishNoise1(q.x / s2, 11.0);
		vec2 nl = fcFinishNoise1(q.y / sl, 3.0);
		float amp = 0.6 + nl.x;   // 0.1 .. 1.1 along the lay
		float h = n1.x * 0.7 + n2.x * 0.3;
		float dh = n1.y * 0.7 / pitch + n2.y * 0.3 / s2;
		g = vec2(depth * amp * dh, depth * h * nl.y / sl);
	}
	else if (pattern < 4.5)
	{
		// Blasted: isotropic craters, two octaves. No lay -- the
		// rotation above and below cancels, which is correct: a
		// blasted surface has no direction to state.
		float s2 = pitch * 0.41;
		vec3 n1 = fcFinishNoise2(q / pitch);
		vec3 n2 = fcFinishNoise2(q / s2 + vec2(17.0, 5.0));
		g = depth * (n1.yz * (0.75 / pitch) + n2.yz * (0.25 / s2));
	}
	else
	{
		// Turned: concentric feed marks about the lay frame's origin,
		// i.e. one groove train in the radius. The gradient points
		// along the radius, which is undefined at the centre.
		float r = length(q);
		g = r > 1.0e-6
			? fcFinishGroove(r, pitch, depth) * q / r
			: vec2(0.0, 0.0);
	}
	return vec2(g.x * ca - g.y * sa, g.x * sa + g.y * ca);
}

/// Rotate the shading normal by an object-space height gradient
/// (Mikkelsen's surface gradient).
///
/// dh/dscreen comes from the ANALYTIC gradient through the chain rule,
/// so the pattern is never sampled twice and never differenced -- the
/// 2x2 quad granularity that would otherwise blur every crest is simply
/// not in this path. dox/doy are dFdx/dFdy of opos, measured once by the
/// caller because the filter needs them too.
void fcFinishPerturb(vec3 g, vec3 opos, vec3 vpos, vec3 dox, vec3 doy,
                     inout vec3 n)
{
	float dhdx = dot(g, dox);
	float dhdy = dot(g, doy);
	vec3 dpx = dFdx(vpos);
	vec3 dpy = dFdy(vpos);
	vec3 r1 = cross(dpy, n);
	vec3 r2 = cross(n, dpx);
	float det = dot(dpx, r1);
	if (abs(det) < 1.0e-20)
		return;
	vec3 sg = sign(det) * (dhdx * r1 + dhdy * r2);
	n = normalize(abs(det) * n - sg);
}

/// The object-space gradient of the height field over the face's own
/// projection frame: the rung above the triplanar projection below.
///
/// A frame states what the machine knew -- the plane the tool swept, or
/// the axis the part turned about -- so the pattern is laid ONCE, in one
/// honest 2D coordinate, instead of being blended out of three
/// axis-aligned guesses. The gradient comes back to object space through
/// the transpose of that coordinate's own Jacobian, which is what keeps
/// Mikkelsen's chain rule exact.
///
/// The frame's first coordinate is the surface's X direction and the
/// second follows the right-hand rule about the axis, so a lay angle of
/// zero means the same thing on both kinds: the pattern runs ALONG the
/// axis of a turned face and along the plane's own X on a flat one.
vec3 fcFinishFramed(vec3 opos, vec4 f0, vec4 f1, vec4 f2, float pattern,
                    float pitch, float depth, float ca, float sa)
{
	vec3 origin = f0.xyz;
	vec3 axis = normalize(f1.xyz);
	vec3 xdir = normalize(f2.xyz - axis * dot(axis, f2.xyz));
	vec3 ydir = cross(axis, xdir);
	vec3 d = opos - origin;

	if (f0.w < FC_FRAME_RADIAL - 0.5)
	{
		// Planar: the plane's own two axes, and the frame origin sets
		// where a concentric pattern centres.
		vec2 p = vec2(dot(d, xdir), dot(d, ydir));
		vec2 g = fcFinishPattern(p, pattern, pitch, depth, ca, sa);
		return g.x * xdir + g.y * ydir;
	}

	// Radial: the coordinate is (arc length about the axis, distance
	// along it), which is what makes a straight knurl run along the axis
	// and a turned lay run round it. Both are millimetres, so a pitch
	// still means a pitch.
	float z = dot(d, axis);
	vec3 rvec = d - z * axis;
	float r = length(rvec);
	if (r < 1.0e-6)
		return vec3(0.0, 0.0, 0.0);   // the axis itself has no direction
	vec3 that = cross(axis, rvec / r);
	float theta = atan2(dot(rvec, ydir), dot(rvec, xdir));

	// KEY: the seam closes by construction. atan2 cuts at +-pi, where the
	// arc length would jump by the whole circumference -- so the period
	// is snapped to fit a WHOLE number of pattern cycles round the
	// reference radius, which makes that jump an exact multiple of the
	// pattern's own period and the pattern continuous across it. Real
	// knurling tooling is chosen for the same reason and by the same
	// arithmetic.
	//
	// The period is not always the pitch: a diamond knurl's two trains
	// run at 45 degrees, so its period ALONG the arc is the pitch times
	// root two, and snapping to the pitch would leave it mismatched by
	// most of a diamond. This closes exactly at lay angle zero (and for
	// the straight knurl at ninety); a lay laid over at some other angle
	// meets itself the way a rolled one does.
	float rref = f1.w > 1.0e-6 ? f1.w : r;
	float period = pattern < 1.5 ? pitch * 1.41421356 : pitch;
	float cycles = max(1.0, floor(FC_FINISH_TWOPI * rref / period + 0.5));
	float k = cycles * period / FC_FINISH_TWOPI;   // arc per radian

	// Turning is the one pattern whose meaning is fixed to the axis
	// rather than to the lay: a lathe leaves feed marks running ROUND
	// the work, whatever else is stated. It is a groove train across the
	// axial coordinate, which is the straight knurl with the lay turned
	// a quarter turn -- so turn it, rather than teaching the pattern
	// library a second spelling of the same relief.
	float p2pattern = pattern;
	float pca = ca;
	float psa = sa;
	if (pattern > FC_FINISH_TURNED - 0.5)
	{
		p2pattern = FC_FINISH_KNURL_STRAIGHT;
		pca = -sa;
		psa = ca;
	}

	vec2 p = vec2(theta * k, z);
	vec2 g = fcFinishPattern(p, p2pattern, pitch, depth, pca, psa);
	// d(arc)/d(opos) = k * d(theta)/d(opos) = k * that / r, and
	// d(z)/d(opos) = axis.
	return g.x * (k / r) * that + g.y * axis;
}

/// Perturb the shading normal (view space) by the finish, and hand the
/// part of it this pixel cannot resolve to the roughness.
///
/// opos/onrm are the object-space position and normal, vpos the view-
/// space position, params the palette entry this fragment's face names
/// (fcFinishEntry) and frameIndex the projection frame it names beside
/// it. Does nothing at all when no finish is stated.
void fcApplyFinish(vec3 opos, vec3 onrm, vec3 vpos, vec4 params,
                   float frameIndex, inout vec3 n, inout float rough)
{
	float pattern = params.x;
	if (pattern < 0.5 || pattern > FC_FINISH_LAST + 0.5)
		return;
	float pitch = max(params.y, 1.0e-6);
	float depth = params.z;

	// The pixel footprint, in object space and so in the units the
	// pitch is stated in. It bounds the footprint in each of the three
	// projection planes below (projecting a step can only shorten it),
	// which is why one measurement filters all of them.
	vec3 dox = dFdx(opos);
	vec3 doy = dFdy(opos);
	float fw = max(length(dox), length(doy));
	// Pixels per feature, faded out below the two the Nyquist limit
	// asks for. The window has to be wide enough that the fade itself
	// does not read as a band on a surface receding from the camera.
	float vis = smoothstep(1.5, 4.0, pitch / max(fw, 1.0e-9));

	// Relief too fine to draw is not relief that stopped existing: its
	// slopes still spread the highlight, so what the fade takes away
	// is added to the roughness as the variance of the profile's own
	// slope (RMS slope of a sine of this amplitude, squared).
	float slope = FC_FINISH_PI * depth / pitch;
	float lost = (1.0 - vis) * 0.5 * slope * slope;
	rough = min(sqrt(rough * rough + lost), 1.0);
	if (vis <= 0.0)
		return;

	float ca = cos(params.w);
	float sa = sin(params.w);
	float d = depth * vis;

	// The face's own projection frame, if the geometry could state one.
	// Everything below this branch is the fallback it replaces.
	int fi = int(clamp(frameIndex, 0.0, float(FC_FRAME_PALETTE - 1))) * 3;
	vec4 f0 = u_frameParams[fi];
	if (f0.w > 0.5)
	{
		vec3 gf = fcFinishFramed(opos, f0, u_frameParams[fi + 1],
		                         u_frameParams[fi + 2], pattern, pitch,
		                         d, ca, sa);
		fcFinishPerturb(gf, opos, vpos, dox, doy, n);
		return;
	}

	// Triplanar projection weights off the object-space normal. The
	// interpolated (shading) normal rather than the facet one the
	// derivatives would give: a tessellated cylinder would otherwise
	// step from facet to facet wherever two projections blend.
	vec3 w = abs(normalize(onrm));
	w = max(w - 0.25, vec3_splat(0.0));
	w = w * w;
	w = w * w;
	float wsum = w.x + w.y + w.z;
	w = wsum > 1.0e-8 ? w / wsum : vec3(0.0, 0.0, 1.0);

	// The object-space gradient of the height field. Each plane
	// contributes its 2D slope along that plane's two axes; the
	// component along its own axis is zero, which is the standard
	// triplanar approximation and the reason a face at 45 degrees to
	// everything gets a slightly shallower pattern than one facing an
	// axis. The weight test skips the two planes a flat face does not
	// use at all.
	vec3 g = vec3(0.0, 0.0, 0.0);
	if (w.x > 0.002)
	{
		vec2 gx = fcFinishPattern(opos.yz, pattern, pitch, d, ca, sa);
		g += w.x * vec3(0.0, gx.x, gx.y);
	}
	if (w.y > 0.002)
	{
		vec2 gy = fcFinishPattern(opos.zx, pattern, pitch, d, ca, sa);
		g += w.y * vec3(gy.y, 0.0, gy.x);
	}
	if (w.z > 0.002)
	{
		vec2 gz = fcFinishPattern(opos.xy, pattern, pitch, d, ca, sa);
		g += w.z * vec3(gz.x, gz.y, 0.0);
	}

	fcFinishPerturb(g, opos, vpos, dox, doy, n);
}

#endif // FC_FINISH_SH
