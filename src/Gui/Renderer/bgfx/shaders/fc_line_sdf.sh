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
 * FC_GLASS_LINE_ALPHA is what a line behind glass dims to. Must match
 * BGFXView::kGlassLineAlpha.
 *
 * Constants only: this is included by the line VERTEX shader as well,
 * where derivatives do not exist. The coverage reconstruction that
 * needs fwidth lives with its one consumer, fs_fc_glass.
 */

#define FC_LINE_SDF_RADIUS 32.0
#define FC_GLASS_LINE_ALPHA 0.4
