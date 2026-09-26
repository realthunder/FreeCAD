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

// Where each palette entry above LIES, when the face's projection frame
// cannot say (Render::FinishPalette::Entry::extent): xy = the axis the
// pattern is laid about, octahedrally encoded, zw = the band of the
// object-space coordinate dot(p, axis) the pattern covers. Stated only
// when z < w; all zero (everything an appearance authors) is the face's
// own frame, face-wide. A screw thread is what states one: it is cut
// about its BORE's axis, which the frame palette may have had to drop,
// and a tapped hole's thread stops at the thread depth, part way down
// one face.
uniform vec4 u_finishExtent[FC_FINISH_PALETTE];

vec4 fcFinishExtentEntry(float index)
{
	int i = int(clamp(index, 0.0, float(FC_FINISH_PALETTE - 1)));
	return u_finishExtent[i];
}

/// The inverse of ViewProviderGeometryObject::ImpliedFinish::setExtent:
/// the octahedral square unfolded back onto the unit sphere.
vec3 fcFinishDecodeAxis(vec2 e)
{
	vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
	float t = max(-v.z, 0.0);
	v.x += v.x >= 0.0 ? -t : t;
	v.y += v.y >= 0.0 ? -t : t;
	return normalize(v);
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
#define FC_FINISH_THREAD          6.0
#define FC_FINISH_THREAD_LEFT     7.0
#define FC_FINISH_LAST            7.0

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
void fcFinishPerturbD(float dhdx, float dhdy, vec3 vpos, inout vec3 n)
{
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

void fcFinishPerturb(vec3 g, vec3 opos, vec3 vpos, vec3 dox, vec3 doy,
                     inout vec3 n)
{
	fcFinishPerturbD(dot(g, dox), dot(g, doy), vpos, n);
}

/// One turn of a screw thread's profile, as a height in [-1, 0] -- 0 at
/// the crest the bore was drilled to, -1 at the root the tap cut --
/// against the phase along the helix. The truncated V every standard
/// thread is: a crest flat, a straight flank down, a root flat, a flank
/// back up, laid symmetrically about the root's centre at phase 0.5.
/// prof.x is half the root flat, prof.y one flank's width, both in turns.
float fcThreadProfile(float ph, vec2 prof)
{
	float y = abs(fract(ph) - 0.5);
	return -clamp((prof.x + prof.y - y) / prof.y, 0.0, 1.0);
}

/// A screw thread (App::SurfaceFinish::Thread / ThreadLeft): the helical
/// relief a tap or a die leaves, shaded on the smooth bore the model
/// carries.
///
/// What makes a thread read as a thread rather than as stripes is that
/// it is DEEP -- over half the pitch, flanks at sixty degrees -- so four
/// things are done that the shallow finishes above can do without:
///
/// - The helix is laid about the bore's own axis: the phase is the
///   distance along it in pitches, less the turn about it (taken off the
///   normal, which on a surface of revolution points straight at the
///   axis), so one crest runs round and down the bore the way the tap
///   did, left-handed with the sign flipped.
/// - The flank slope is BOX-FILTERED over the pixel's footprint in phase:
///   the difference of the profile across the footprint over its width,
///   which is exact for a piecewise linear profile. The sharp corners a
///   thread really has stay sharp up close and average away at a
///   distance, without the sine-shaped stand-in the finishes above use.
/// - A PARALLAX march along the profile: the view ray enters at the
///   crest and is followed down the groove until it meets a flank, so a
///   flank turned away from the eye is hidden behind the one in front of
///   it. Looking into a tapped hole, that is most of what it looks like.
/// - The roots are OCCLUDED: they see little of the sky between two
///   sixty-degree flanks, which is what darkens a real thread's grooves.
///
/// The band (extent zw) ends the thread where the tap stopped, with the
/// last pitch and a half running out rather than cut off square.
void fcFinishThread(vec3 opos, vec3 onrm, vec3 vpos, vec4 params,
                    vec4 extent, float frameIndex, vec3 dox, vec3 doy,
                    inout vec3 n, inout float rough, inout float occ)
{
	float pitch = max(params.y, 1.0e-6);

	// The axis the thread is cut about: stated beside the finish when
	// the feature that implied it knew, else the face's own frame -- and
	// a face that is not a surface of revolution has no axis to cut one
	// about.
	vec3 axis;
	bool banded = extent.z < extent.w;
	if (banded)
		axis = fcFinishDecodeAxis(extent.xy);
	else
	{
		int fi = int(clamp(frameIndex, 0.0,
		                   float(FC_FRAME_PALETTE - 1))) * 3;
		if (u_frameParams[fi].w < FC_FRAME_RADIAL - 0.5)
			return;
		axis = normalize(u_frameParams[fi + 1].xyz);
	}

	float z = dot(opos, axis);
	// Past the band there is no thread; toward either end it runs out.
	float amount = 1.0;
	if (banded)
	{
		float run = 1.5 * pitch;
		amount = clamp((z - extent.z) / run, 0.0, 1.0)
		       * clamp((extent.w - z) / run, 0.0, 1.0);
		if (amount <= 0.0)
			return;
	}

	// The turn about the axis, off the normal's radial part. The frame
	// round the axis is any one fixed by the axis alone, so every face
	// of one bore -- a bore a later cut split in two -- agrees on it.
	vec3 nr = onrm - axis * dot(onrm, axis);
	float nrl = length(nr);
	if (nrl < 1.0e-4)
		return;   // an end face: no turn to measure
	nr /= nrl;
	vec3 ref = abs(axis.z) < 0.9 ? vec3(0.0, 0.0, 1.0)
	                             : vec3(1.0, 0.0, 0.0);
	vec3 xd = normalize(cross(axis, ref));
	vec3 yd = cross(axis, xd);
	float theta = atan2(dot(nr, yd), dot(nr, xd));
	float hand = params.x > FC_FINISH_THREAD + 0.5 ? -1.0 : 1.0;
	// Wrapped before the turn is added, as fcFinishGroove wraps: a bore
	// far from the object origin is thousands of pitches out.
	float ph = fract(z / pitch) - hand * theta / FC_FINISH_TWOPI;

	// The profile, from the height and the included angle. Toward the
	// runout the thread is shallower and its groove narrower, as the
	// chamfered lead of a tap leaves it.
	float halfAngle = params.w > 1.0e-3 ? 0.5 * params.w : 0.52359878;
	float d = params.z * amount;
	float f = clamp(d * tan(halfAngle) / pitch, 1.0e-3, 0.5);
	// What the flanks leave is crest and root, two to one: ISO's P/4
	// crest flat and P/8 root flat for its 5/8 H working height.
	float rest = 1.0 - 2.0 * f;
	vec2 prof = vec2(rest / 6.0, f);

	// The pixel's footprint in turns: along the axis, and round it (the
	// turn's own screen derivative, unwrapped across atan2's cut).
	float dthx = dFdx(theta);
	float dthy = dFdy(theta);
	dthx -= FC_FINISH_TWOPI * floor(dthx / FC_FINISH_TWOPI + 0.5);
	dthy -= FC_FINISH_TWOPI * floor(dthy / FC_FINISH_TWOPI + 0.5);
	float dphx = dot(axis, dox) / pitch - hand * dthx / FC_FINISH_TWOPI;
	float dphy = dot(axis, doy) / pitch - hand * dthy / FC_FINISH_TWOPI;
	float w = max(abs(dphx), abs(dphy));
	float vis = smoothstep(1.5, 4.0, 1.0 / max(w, 1.0e-6));

	// The flank's slope, and the mean square of it over a turn: what a
	// thread too fine to draw still does to a highlight.
	float slope = d / (f * pitch);
	float lost = (1.0 - vis) * 2.0 * f * slope * slope;
	rough = min(sqrt(rough * rough + lost), 1.0);

	// Occlusion a turn's worth averaged, for where the thread is not
	// drawn: the mean of 1 - 0.55 u^1.5 over the profile, u the depth
	// fraction (zero on the crest, a linear ramp on the flanks, one on
	// the root).
	float aoFar = 1.0 - 0.55 * (2.0 * f * 0.4 + 2.0 * prof.x);
	if (vis <= 0.0)
	{
		occ *= mix(1.0, aoFar, amount);
		return;
	}

	// Parallax: follow the view ray from the crest down the groove.
	// The axis in view space comes off the surface's own Jacobian
	// (the object and view derivatives of the same pixel), which holds
	// for an instanced draw too, whose model matrix this stage never
	// sees. The phase the ray gains per millimetre of depth is the
	// view direction's run along the axis over its rise off the face.
	vec3 dpx = dFdx(vpos);
	vec3 dpy = dFdy(vpos);
	float xx = dot(dox, dox);
	float xy = dot(dox, doy);
	float yy = dot(doy, doy);
	float det = xx * yy - xy * xy;
	float phHit = ph;
	if (det > 1.0e-30)
	{
		float ax = dot(axis, dox);
		float ay = dot(axis, doy);
		vec3 av = ((ax * yy - ay * xy) * dpx + (ay * xx - ax * xy) * dpy)
		        / det;
		float avl = length(av);
		vec3 vdir = FC_MTX(u_proj, 2, 3) != 0.0
			? normalize(-vpos) : vec3(0.0, 0.0, 1.0);
		float vn = dot(vdir, n);
		if (avl > 1.0e-12 && vn > 0.02)
		{
			float k = -dot(vdir, av / avl) / (vn * pitch);
			// At grazing incidence the ray crosses turn after turn
			// before it reaches the root. One turn over the whole
			// depth is as far as the march may carry a pixel: past
			// that neighbouring pixels land on unrelated flanks and
			// the picture turns to noise, which reads less like a
			// thread than no parallax at all. And it fades in from
			// grazing, where the same holds for every ray.
			float kmax = 1.0 / max(d, 1.0e-6);
			k = clamp(k, -kmax, kmax) * smoothstep(0.05, 0.35, vn);
			float stepT = d / 16.0;
			float t = 0.0;
			float g = -fcThreadProfile(ph, prof) * d;
			float tp = 0.0;
			float gp = g;
			for (int i = 0; i < 16; ++i)
			{
				if (t >= g)
					break;
				tp = t;
				gp = g;
				t += stepT;
				g = -fcThreadProfile(ph + k * t, prof) * d;
			}
			// Between the last step above the flank and the first
			// below it, where the ray's depth meets the groove's.
			float e0 = tp - gp;
			float e1 = t - g;
			float s = e1 - e0 > 1.0e-9 ? clamp(-e0 / (e1 - e0), 0.0, 1.0)
			                          : 0.0;
			phHit = ph + k * mix(tp, t, s) * vis;
		}
	}

	// The flank's slope at the point the ray met, box-filtered across
	// the footprint, and through the chain rule into screen space. The
	// march stretches the phase across the pixel -- a flank seen edge
	// on spans a few turns' worth of rays -- so the footprint is the
	// larger of the surface's and the marched phase's own (unwrapped:
	// the phase carries fract()'s cut).
	float dhx = dFdx(phHit);
	float dhy = dFdy(phHit);
	dhx -= floor(dhx + 0.5);
	dhy -= floor(dhy + 0.5);
	float hw = 0.5 * max(max(w, max(abs(dhx), abs(dhy))), 1.0e-4);
	float dH = (fcThreadProfile(phHit + hw, prof)
	          - fcThreadProfile(phHit - hw, prof)) / (2.0 * hw);
	float dh = d * dH * vis;
	fcFinishPerturbD(dh * dphx, dh * dphy, vpos, n);

	float u = -fcThreadProfile(phHit, prof);
	float aoNear = 1.0 - 0.55 * u * sqrt(u);
	occ *= mix(1.0, mix(aoFar, aoNear, vis), amount);
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
/// (fcFinishEntry), extent where that entry lies (fcFinishExtentEntry)
/// and frameIndex the projection frame it names beside it. occ is the
/// indirect-light occlusion, which only a thread's grooves are deep
/// enough to change. Does nothing at all when no finish is stated.
void fcApplyFinish(vec3 opos, vec3 onrm, vec3 vpos, vec4 params,
                   vec4 extent, float frameIndex, inout vec3 n,
                   inout float rough, inout float occ)
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

	// A thread is laid about an axis and filters itself along it; it
	// shares nothing below but the footprint.
	if (pattern > FC_FINISH_THREAD - 0.5)
	{
		fcFinishThread(opos, onrm, vpos, params, extent, frameIndex,
		               dox, doy, n, rough, occ);
		return;
	}
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

/// Texture coordinates for laying an IMAGE on a face, in the same
/// projection frames the finish above is laid out in.
///
/// A per-face image has no parametrization of its own -- a CAD face
/// carries no UVs -- so it is projected exactly the way the finish is:
/// in the plane's own axes for a planar face, unwrapped about the axis
/// for a turned one, and triplanarly off the object-space normal for a
/// face whose surface could not be classified. \a mmPerTile is the size
/// the image is printed at, in the millimetres of object space
/// everything here is measured in.
///
/// Unlike the finish this returns coordinates rather than a gradient:
/// the sampler does the filtering, and an image has no analytic slope
/// to hand to a surface gradient.
vec2 fcFrameTexUV(vec3 opos, vec3 onrm, float frameIndex, float mmPerTile)
{
	float inv = 1.0 / max(mmPerTile, 1.0e-6);
	int fi = int(clamp(frameIndex, 0.0, float(FC_FRAME_PALETTE - 1))) * 3;
	vec4 f0 = u_frameParams[fi];
	if (f0.w > 0.5)
	{
		vec4 f1 = u_frameParams[fi + 1];
		vec4 f2 = u_frameParams[fi + 2];
		vec3 origin = f0.xyz;
		vec3 axis = normalize(f1.xyz);
		vec3 xdir = normalize(f2.xyz - axis * dot(axis, f2.xyz));
		vec3 ydir = cross(axis, xdir);
		vec3 d = opos - origin;
		if (f0.w < FC_FRAME_RADIAL - 0.5)
			return vec2(dot(d, xdir), dot(d, ydir)) * inv;

		// Radial: arc length about the axis and distance along it, so
		// an image wraps a turned face the way a printed label does.
		// The arc is snapped to a whole number of tiles round the
		// reference radius for the same reason the pattern is: the
		// atan2 seam would otherwise cut the image mid-tile.
		float z = dot(d, axis);
		vec3 rvec = d - z * axis;
		float r = length(rvec);
		float theta = atan2(dot(rvec, ydir), dot(rvec, xdir));
		float rref = f1.w > 1.0e-6 ? f1.w : max(r, 1.0e-6);
		float tiles = max(1.0, floor(FC_FINISH_TWOPI * rref * inv
		                             + 0.5));
		return vec2(theta * tiles / FC_FINISH_TWOPI, z * inv);
	}

	// Triplanar: the same normal-weighted blend the finish falls back
	// to, but a coordinate cannot be blended -- two projections
	// averaged read as a ghost of the image over itself -- so the
	// dominant axis wins outright.
	vec3 w = abs(normalize(onrm));
	if (w.x >= w.y && w.x >= w.z)
		return opos.yz * inv;
	if (w.y >= w.z)
		return opos.zx * inv;
	return opos.xy * inv;
}

#endif // FC_FINISH_SH
