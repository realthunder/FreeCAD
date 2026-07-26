/*
 * Bundled fountain effect (docs/RenderEngine.md §5.11): the identity
 * scatter medium function of the volume stage — the stock fountain
 * spray density of the body's slot, spliced into the engine's
 * volumetric raymarch. Not a whole program: exactly this function
 * (plus optional helpers and uniform declarations), no
 * $input/main/includes. Defining fcMediumScatter selects the
 * scattering channel: the bound solid becomes a fountain body with
 * its placement's flow frame and the water-surface splash rings
 * engaged. Customize the density for a different plume shape.
 */

float fcMediumScatter(vec3 wp)
{
	return fcStockCloudField(FC_MEDIUM_SLOT, wp);
}
