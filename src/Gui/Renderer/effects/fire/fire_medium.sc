/*
 * Bundled fire effect (docs/RenderEngine.md §5.11): the identity
 * medium function of the volume stage — the stock flame field and
 * blackbody ramp of the body's slot, spliced into the engine's
 * volumetric raymarch. Not a whole program: exactly these two
 * functions (plus optional helpers and uniform declarations), no
 * $input/main/includes. Customize the field for a different flame
 * shape, or the ramp for a different flame color.
 */

float fcMediumField(vec3 wp)
{
	return fcStockFireField(FC_MEDIUM_SLOT, wp);
}

vec3 fcMediumRamp(float t)
{
	return fcStockFireRamp(FC_MEDIUM_SLOT, t);
}
