/*
 * Medium (water/glass/cloud/fire body) interval depth writer fragment
 * shader body: rasterizes a body's front or back faces into the
 * medium's RGBA16F interval target — the body's appearance slot index
 * in .x (u_mediumSlot.x, consumed by the volumetric/caustics passes to
 * look up the per-slot medium parameters), the positive linear view
 * depth in .z, and .w = 1 as the written-fragment mark (the target
 * clears to 0). Layout-compatible with the SSAO prepass writer these
 * passes historically shared, except the octahedral normal in .xy —
 * no medium consumer reads it. Included by fs_fc_meddepth.sc and
 * fs_fc_meddepth_clip.sc (CLIP_PLANES defined).
 */

uniform vec4 u_mediumSlot;

void main()
{
#ifdef CLIP_PLANES
	clipDiscard(v_wpos);
#endif
	gl_FragColor = vec4(u_mediumSlot.x, 0.0, -v_vpos.z, 1.0);
}
