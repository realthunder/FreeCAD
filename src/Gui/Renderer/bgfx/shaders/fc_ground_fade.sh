/*
 * The shadow ground's dissolved rim (GROUND_FADE).
 *
 * A ground sized to the camera reads as endless only if it does not END
 * anywhere the eye can see, and a hard line across the view is exactly
 * what a finite quad would otherwise draw. So the outer band of the
 * quad fades to nothing.
 *
 * The coordinate is normalized against the quad's OWN axes -- the CPU
 * folds the half extents and the centre into the two uniforms -- so it
 * is 0 at the centre and 1 at the rim whatever the quad's size,
 * orientation or aspect. Nothing about it changes as the camera moves,
 * which is what keeps the band from swimming across the ground.
 *
 * VIEW space, because that is the one position every ground program
 * already has: the mesh variants carry v_vpos for the lighting, the
 * prepass for its depth. The CPU transforms the axes once per frame.
 *
 * Included only by the ground's own program variants. The ground rides
 * the SCENE's mesh program otherwise, and a fade uniform there would be
 * one more global that every scene draw has to clear.
 */

#ifndef FC_GROUND_FADE_SH
#define FC_GROUND_FADE_SH

// xyz = the quad's unit axis over its half extent, in view space;
// w = the same applied to the quad's centre and negated. A fragment's
// coordinate along the axis is dot(xyz, vpos) + w.
uniform vec4 u_groundFadeU;
uniform vec4 u_groundFadeV;

// 1 well inside the quad, 0 at the rim.
float fcGroundFade(vec3 vpos)
{
	float u = dot(u_groundFadeU.xyz, vpos) + u_groundFadeU.w;
	float v = dot(u_groundFadeV.xyz, vpos) + u_groundFadeV.w;
	return 1.0 - smoothstep(0.6, 1.0, max(abs(u), abs(v)));
}

#endif // FC_GROUND_FADE_SH
