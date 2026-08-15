/*
 * Procedural matcaps: the shading of a fragment from its view-space
 * normal alone, with no scene lights involved.
 *
 * A matcap ("material capture") is normally an image of a lit sphere,
 * indexed by the screen projection of the normal. These presets compute
 * the same thing analytically instead, which is what lets the feature
 * ship with no image assets to commit, install, or fetch -- the browser
 * tier gets it for free, at any resolution, with no bundle growth.
 *
 * Every direction below is in VIEW space, i.e. the studio is welded to
 * the camera. That is the whole point of matcap shading: the light does
 * not swim when the model turns, so two faces that face the same way
 * shade the same way, anywhere in the scene, and form is comparable
 * across parts.
 *
 * Needs nothing but the normal, so it can be included anywhere.
 */

#ifndef FC_MATCAP_SH
#define FC_MATCAP_SH

// n: shading normal, normalized, view space, already flipped toward the
// viewer by the caller for two-sided draws.
// preset: 0 studio, 1 clay, 2 metal, 3 pearl.
vec3 fc_matcap(float preset, vec3 n)
{
	// Key from the upper left, fill from the lower right — the studio
	// convention, and the one that reads as "lit from above" to the eye.
	vec3 key = normalize(vec3(-0.45, 0.60, 0.66));
	vec3 fill = normalize(vec3(0.55, -0.35, 0.75));
	float ndk = dot(n, key);
	float ndf = dot(n, fill);
	// Facing ratio: 1 head-on, 0 at the silhouette.
	float ndv = clamp(n.z, 0.0, 1.0);
	float fres = pow(1.0 - ndv, 4.0);

	if (preset < 0.5)
	{
		// Studio: broad key, weak fill, tight highlight, faint rim.
		float d = 0.42 + 0.50 * max(ndk, 0.0) + 0.16 * max(ndf, 0.0);
		vec3 h = normalize(key + vec3(0.0, 0.0, 1.0));
		float s = pow(max(dot(n, h), 0.0), 48.0);
		return vec3_splat(d + s * 0.50 + fres * 0.16);
	}
	if (preset < 1.5)
	{
		// Clay: matte hemisphere, no highlight at all. The most neutral
		// read of form — nothing competes with the shape itself.
		float d = 0.30 + 0.68 * (0.5 + 0.5 * ndk);
		return vec3_splat(d * (1.0 - 0.22 * fres));
	}
	if (preset < 2.5)
	{
		// Metal: a banded sweep across the normal, so a small change of
		// slope moves a band a long way — curvature is exaggerated on
		// purpose, which is what makes shallow defects visible.
		float band = 0.5 + 0.5 * sin(ndk * 6.0);
		float d = 0.16 + 0.50 * smoothstep(0.15, 0.95, band)
			+ 0.34 * max(ndk, 0.0);
		return vec3_splat(d + fres * 0.50);
	}
	// Pearl: warm/cool dual tone across the key axis plus a soft sheen.
	// The hue shift carries gentle undulation that a grey matcap flattens.
	vec3 warm = vec3(1.00, 0.94, 0.86);
	vec3 cool = vec3(0.62, 0.70, 0.85);
	vec3 c = mix(cool, warm, clamp(0.5 + 0.5 * ndk, 0.0, 1.0));
	float s = pow(max(ndk, 0.0), 16.0);
	return c * (0.55 + 0.45 * ndv) + vec3_splat(s * 0.28) + cool * fres * 0.38;
}

#endif // FC_MATCAP_SH
