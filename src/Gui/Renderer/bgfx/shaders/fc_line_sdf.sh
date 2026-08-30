/*
 * Shared constants for the line signed-distance field that lets an edge
 * seen through glass warp exactly like the face it lies on.
 *
 * The glass pass resamples the scene through a per-pixel refraction
 * offset. Resampling a rasterized line magnifies it -- there is no
 * fixing that, the line was already pixels before the lens saw it. A
 * DISTANCE FIELD resamples differently: the lens moves the field, and
 * the coverage is reconstructed afterwards by converting the stored
 * distance into post-lens pixels through the Jacobian of the
 * refraction mapping, so the line lands exactly where the lens puts
 * it while its thickness stays what was asked for. That is the same
 * reason SDF text stays crisp at any scale (fs_fc_glass has the
 * history of why the Jacobian and not the field's own fwidth).
 *
 * FC_LINE_SDF_RADIUS is both the support radius and the offset the
 * alpha channel is stored against: alpha = radius - signedDistance, so
 * an untouched texel reads back as "sd = radius", far outside any line,
 * and the target can clear to plain zero. Must match
 * BGFXView::kLineSdfRadius.
 *
 * 32 is the honest cap, and it was measured before it was kept. The
 * reconstruction needs `radius / halfwidth` of compression, so a 2px
 * line survives 32x and a 32px one only 2x, and past that a line at a
 * lens rim has no stored field left to rebuild from.
 * scripts/lens_rim_fade.py differences a comb against the same frame
 * with the comb hidden -- which cancels the sphere's own limb
 * darkening, specular and shading -- and bins the remaining ink by
 * radius. Through an ior-1.6 ball lens the rim's ink relative to the
 * band inside it falls 4.04, 3.52, 3.24, 2.60, 1.68 over widths 2, 4,
 * 8, 16, 32: the predicted signature, and small where it matters
 * (13% from 2px to 4px). The rim never empties -- it stays the
 * DARKEST part of the disc at every width, because a ball lens
 * squeezes the whole scene into it. A gentle ior-1.15 lens, whose
 * compression never approaches the limit, still falls 34% across the
 * same sweep, so much of the thick-line loss is a thick line running
 * its half width off the edge of the glass rather than the support
 * radius. Doubling the radius would buy a 4px line about 15% more rim
 * ink and double the quad every line rasterizes into the field; not
 * worth it for the widths CAD uses.
 *
 * FC_GLASS_LINE_ALPHA is what a line behind glass dims to. Must match
 * BGFXView::kGlassLineAlpha.
 *
 * Constants only: this is included by the line VERTEX shader as well,
 * where derivatives do not exist. The coverage reconstruction that
 * needs fwidth lives with its one consumer, fs_fc_glass.
 */

#define FC_LINE_SDF_RADIUS 32.0
#define FC_GLASS_LINE_ALPHA 0.4
