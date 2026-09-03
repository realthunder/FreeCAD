/*
 * OpenPBR surface evaluation for the CAD mesh shader.
 *
 * The shading model of the PBR branch is OpenPBR (ASWF, v1.1): a
 * layered slab model whose parameters are the ones Cycles' Principled
 * BSDF v2 carries, so the rasterizer and the path tracer describe ONE
 * surface (docs/CyclesIntegration.md sec 6.10). It replaces the single
 * metallic/roughness GGX lobe that stood here before, which had no
 * expression for a coat or a fuzz at all.
 *
 * Reference: OpenPBR-viewer's rasterizer (portsmouth/OpenPBR-viewer,
 * glsl/rasterization/openpbr.frag.glsl, MIT, by an OpenPBR co-author).
 * Four deliberate departures, each forced by the difference between a
 * viewport and a demo that draws one material:
 *
 *  - The lobe DIRECTIONAL ALBEDOS -- which the slab layering needs at
 *    every fragment and the environment terms reuse -- are the analytic
 *    split-sum fit (Lazarov) the IBL path already used, not the
 *    reference's 16-sample Monte Carlo integration. Sixteen GGX samples
 *    per lobe per fragment is not a viewport budget.
 *  - No transmission and no subsurface lobe. A rasterizer cannot refract
 *    through geometry: transmission is the glass pass's business
 *    (docs/ShaderDesign.md 3.8), and a surface stating a constant
 *    transmission_weight of one half or more IS routed there -- the
 *    capture resolves the document's transmission flat and the draw
 *    becomes a glass body (docs/MaterialStorage.md sec 17.21), so this
 *    function never sees it. The struct still CARRIES the transmission
 *    inputs: the glass pass splices the same generated material
 *    function (fc_glass_fs.sh, sec 17.22) and reads them per fragment.
 *    A mapped weight stays here and degrades to diffuse, as does
 *    subsurface, which has no raster route at all. Cycles renders both
 *    properly, so those are a known raster/path-tracer divergence, not
 *    a parity failure.
 *  - No anisotropy and no thin film. Anisotropy needs a tangent frame
 *    the untextured mesh path does not carry and an anisotropic
 *    environment probe we do not build; thin film is rasterizable but
 *    costs ~180 lines of complex arithmetic on every mesh draw.
 *  - None of the OpenPBR 1.2 extras (specular haze, retroreflectivity,
 *    dispersion). They have no socket in Cycles' Principled either, so
 *    implementing them here would move raster AWAY from parity.
 *
 * The frame. Everything runs in the surface's own local frame: z is the
 * shading normal and x is the tangential part of the view direction,
 * which in view space is +z. That puts the view vector at (sin, 0, cos)
 * -- the configuration the sheen LTC fit already assumes -- so its
 * basis transform is the identity and costs nothing.
 *
 * Punctual lights. The reference integrates area lights, so it needs no
 * bound on the specular peak. Ours are delta lights: a smooth face under
 * a light along the view axis sits exactly on the GGX peak, which
 * flashes whole faces white at low roughness. The microfacet term is
 * therefore clamped (FC_PBR_SPEC_MAX), which is the same bound the
 * single-lobe branch carried before.
 */

#ifndef FC_OPENPBR_SH
#define FC_OPENPBR_SH

#define FC_PBR_PI      3.14159265358979
#define FC_PBR_RCP_PI  0.31830988618379
#define FC_PBR_EPS     1.0e-10
#define FC_PBR_ALPHA_MIN 1.0e-4
// Bound on D * G2 * J of any microfacet lobe under a delta light; see
// the punctual-light note above. Carried over unchanged from the
// single-lobe branch so a stock scene keeps its highlight sizes.
#define FC_PBR_SPEC_MAX 4.0

float fcPbrSqr(float x)
{
	return x * x;
}

// ---------------------------------------------------------------------
// Fresnel

vec3 fcPbrSchlick(vec3 f0, float mu)
{
	float m = 1.0 - mu;
	float m2 = m * m;
	return f0 + (m2 * m2 * m) * (vec3_splat(1.0) - f0);
}

/* Conductor Fresnel in OpenPBR's F82-tint parametrization: f0 is the
 * reflectance at normal incidence (the metal's base colour) and f82 the
 * reflectance at 82 degrees (its specular colour), the angle where a
 * real conductor's dip is deepest. Clamped per OpenPBR PR #256 -- an
 * f82 above one otherwise drives the result negative.
 */
vec3 fcPbrFresnelF82(float mu, vec3 f0, vec3 f82)
{
	float muBar = 1.0 / 7.0;
	float c = 1.0 - muBar;
	float denom = muBar * (c * c * c * c * c * c);
	vec3 fBar = fcPbrSchlick(f0, muBar);
	float w = 1.0 - mu;
	float w6 = w * w * w * w * w * w;
	return clamp(fcPbrSchlick(f0, mu)
	             - (mu * w6 / denom) * (vec3_splat(1.0) - f82) * fBar,
	             vec3_splat(0.0), vec3_splat(1.0));
}

/* Unpolarized dielectric Fresnel reflectance. mu = |cos| of the
 * incident angle to the micronormal, eta = transmitted/incident IOR.
 */
float fcPbrFresnelDielectric(float mu, float eta)
{
	float mut2 = fcPbrSqr(eta) - (1.0 - fcPbrSqr(mu));
	if (mut2 <= 0.0)
		return 1.0;   // total internal reflection
	float mut = sqrt(mut2) / eta;
	float rs = (mu - eta * mut) / max(mu + eta * mut, FC_PBR_EPS);
	float rp = (mut - eta * mu) / max(mut + eta * mu, FC_PBR_EPS);
	return 0.5 * (rs * rs + rp * rp);
}

float fcPbrFresnelDielectricNormal(float eta)
{
	return fcPbrSqr((eta - 1.0) / max(eta + 1.0, FC_PBR_EPS));
}

/* OpenPBR's specular_weight scales F0 without disturbing refraction
 * (PR #247): it is expressed as a modified IOR whose normal-incidence
 * reflectance is weight * F0. Below one the modified ratio can fall
 * under unity, where Stokes reciprocity gives the energy-conserving
 * answer at the refracted angle.
 */
float fcPbrFresnelDielectricMod(float mu, float eta, float weight)
{
	if (abs(weight - 1.0) < 1.0e-6)
		return fcPbrFresnelDielectric(mu, eta);
	float etam1 = eta - 1.0;
	if (fcPbrSqr(etam1) < 1.1920929e-7)
		return 0.0;
	float f0 = fcPbrFresnelDielectricNormal(eta);
	float eps = sign(etam1) * sqrt(clamp(weight * f0, 0.0, 1.0));
	float etaMod = (1.0 + eps) / max(1.0 - eps, FC_PBR_EPS);
	if (etaMod >= 1.0)
		return fcPbrFresnelDielectric(mu, etaMod);
	float mut2 = 1.0 - (1.0 - fcPbrSqr(mu)) / fcPbrSqr(eta);
	if (mut2 <= 0.0)
		return 1.0;
	return fcPbrFresnelDielectric(sqrt(mut2), 1.0 / etaMod);
}

/* Hemispherical albedo of the dielectric Fresnel curve -- the share of
 * light a coat sends back down at its inner face, which is what makes a
 * coated base darker (d'Eon's Hitchhiker's Guide closed form, with the
 * low-eta limit where it loses precision).
 */
float fcPbrFresnelDielectricAvg(float eta)
{
	if (eta < 1.03)
		return (eta - 1.0) / 3.0;
	float etapo = eta + 1.0;
	float etamo = eta - 1.0;
	float fPerp = 8.0 / 3.0 * (etapo - 0.5) / fcPbrSqr(etapo);
	float eta2 = fcPbrSqr(eta);
	float eta2po = eta2 + 1.0;
	float eta2po2 = fcPbrSqr(eta2po);
	float eta2mo = eta2 - 1.0;
	float eta2mo2 = fcPbrSqr(eta2mo);
	float fPara = 2.0 * eta2
		* (2.0 * eta * (eta2 + 2.0 * eta - 1.0) / (eta2po2 * eta2mo)
		   - eta2po * log(eta) / eta2mo2
		   + eta2mo2 * log(eta * etapo / etamo) / (eta2po2 * eta2po));
	return 1.0 - 0.5 * (fPerp + fPara);
}

// ---------------------------------------------------------------------
// GGX

float fcPbrGgxD(float ndh, float a)
{
	float d = ndh * ndh * (a * a - 1.0) + 1.0;
	return a * a / max(FC_PBR_PI * d * d, FC_PBR_EPS);
}

float fcPbrGgxLambda(float mu, float a)
{
	float m2 = fcPbrSqr(max(mu, FC_PBR_EPS));
	return 0.5 * (-1.0 + sqrt(1.0 + a * a * (1.0 - m2) / m2));
}

// Height-correlated Smith masking-shadowing.
float fcPbrGgxG2(float ndv, float ndl, float a)
{
	if (ndv < FC_PBR_EPS || ndl < FC_PBR_EPS)
		return 0.0;
	return 1.0 / (1.0 + fcPbrGgxLambda(ndv, a) + fcPbrGgxLambda(ndl, a));
}

/* The split-sum environment BRDF (Karis/Lazarov's analytic fit): the
 * pair (A, B) for which the directional albedo of a GGX lobe is
 * f0 * A + fEdge * B. It is the same fit the IBL path has always used,
 * lifted out so the slab layering can ask for a lobe's albedo too --
 * that is what a layer above needs in order to know how much light
 * reaches the layer below.
 */
vec2 fcPbrGgxSplitSum(float ndv, float rough)
{
	vec4 r4 = rough * vec4(-1.0, -0.0275, -0.572, 0.022)
		+ vec4(1.0, 0.0425, 1.04, -0.04);
	float a004 = min(r4.x * r4.x, exp2(-9.28 * ndv)) * r4.x + r4.y;
	return vec2(-1.04, 1.04) * a004 + r4.zw;
}

/* Directional albedo of a GGX lobe. fEdge is the reflectance the
 * grazing half of the split carries: white for a plain dielectric, and
 * the F82 tint for a conductor, where it is what keeps a tinted metal's
 * environment reflection tinted. A default OpenPBR metal states
 * specular_color = 1, which reduces this to exactly the f0 * A + B the
 * branch computed before.
 */
vec3 fcPbrGgxAlbedo(float ndv, float rough, vec3 f0, vec3 fEdge)
{
	vec2 ab = fcPbrGgxSplitSum(ndv, rough);
	return f0 * ab.x + fEdge * ab.y;
}

// ---------------------------------------------------------------------
// Fuzz (Zeltner's sheen, the LTC fit OpenPBR names)

float fcPbrSheenAlbedo(float mu, float r)
{
	float s = r * (0.0206607 + 1.58491 * r) / (0.0379424 + r * (1.32227 + r));
	float m = r * (-0.193854 + r * (-1.14885 + r * (1.7932 - 0.95943 * r * r)))
		/ (0.046391 + r);
	float o = r * (0.000654023 + (-0.0207818 + 0.119681 * r) * r)
		/ (1.26264 + r * (-1.92021 + r));
	return exp(-0.5 * fcPbrSqr((mu - m) / s)) / (s * sqrt(2.0 * FC_PBR_PI)) + o;
}

float fcPbrSheenAInv(float x, float y)
{
	return (2.58126 * x + 0.813703 * y) * y
		/ (1.0 + 0.310327 * x * x + 2.60994 * x * y);
}

float fcPbrSheenBInv(float x, float y)
{
	return sqrt(1.0 - x) * (y - 1.0) * y * y * y
		/ (0.0000254053 + 1.71228 * x - 1.71506 * x * y + 1.34174 * y * y);
}

/* The fuzz lobe. The LTC basis the reference builds from the view
 * direction is the identity in our frame (x already carries the view
 * vector's tangential part), so the linearly transformed cosine acts on
 * the light vector directly.
 */
float fcPbrFuzzEval(vec3 l, float ndv, float rough)
{
	float r = clamp(rough, 0.01, 1.0);
	float aInv = fcPbrSheenAInv(ndv, r);
	float bInv = fcPbrSheenBInv(ndv, r);
	vec3 wo = vec3(aInv * l.x + bInv * l.z, aInv * l.y, l.z);
	float lenSqr = max(dot(wo, wo), FC_PBR_EPS);
	float jacobian = fcPbrSqr(aInv / lenSqr);
	float pdf = max(wo.z, 0.0) * FC_PBR_RCP_PI * jacobian;
	return fcPbrSheenAlbedo(ndv, r) * pdf
		/ max(abs(l.z), 1.1920929e-7);
}

// ---------------------------------------------------------------------
// Diffuse (EON: the energy-preserving Oren-Nayar OpenPBR specifies)

#define FC_PBR_FON_C1 (0.5 - 2.0 / (3.0 * FC_PBR_PI))
#define FC_PBR_FON_C2 (2.0 / 3.0 - 28.0 / (15.0 * FC_PBR_PI))

float fcPbrFonAlbedo(float mu, float r)
{
	float c = 1.0 - mu;
	float c2 = c * c;
	// Fujii's fit of the FON directional albedo at rho = 1.
	float gOverPi = (0.0571085289 * c + 0.491881867 * c2)
		+ (-0.332181442 * c + 0.0714429953 * c2) * c2;
	return (1.0 + r * gOverPi) / (1.0 + FC_PBR_FON_C1 * r);
}

vec3 fcPbrDiffuseEval(vec3 rho, float r, float ndv, float ndl, float vdl)
{
	float s = vdl - ndv * ndl;
	float stinv = s > 0.0 ? s / max(max(ndv, ndl), FC_PBR_EPS) : s;
	float af = 1.0 / (1.0 + FC_PBR_FON_C1 * r);
	vec3 fss = rho * FC_PBR_RCP_PI * af * (1.0 + r * stinv);
	float efo = fcPbrFonAlbedo(ndl, r);
	float efi = fcPbrFonAlbedo(ndv, r);
	float avg = af * (1.0 + FC_PBR_FON_C2 * r);
	vec3 rhoMs = rho * rho * avg
		/ max(vec3_splat(1.0) - rho * max(0.0, 1.0 - avg), vec3_splat(FC_PBR_EPS));
	vec3 fms = rhoMs * FC_PBR_RCP_PI
		* (max(1.1920929e-7, 1.0 - efo) * max(1.1920929e-7, 1.0 - efi)
		   / max(1.1920929e-7, 1.0 - avg));
	return fss + fms;
}

vec3 fcPbrDiffuseAlbedo(vec3 rho, float r, float ndv)
{
	float af = 1.0 / (1.0 + FC_PBR_FON_C1 * r);
	float ef = fcPbrFonAlbedo(ndv, r);
	float avg = af * (1.0 + FC_PBR_FON_C2 * r);
	vec3 rhoMs = rho * rho * avg
		/ max(vec3_splat(1.0) - rho * max(0.0, 1.0 - avg), vec3_splat(FC_PBR_EPS));
	return rho * ef + rhoMs * (1.0 - ef);
}

// ---------------------------------------------------------------------
// The surface

/* The OpenPBR parameters this rasterizer expresses. The excluded groups
 * (subsurface, anisotropy, thin film) are stated in the file header; the
 * Cycles interpreter carries them, so leaving them out of the struct is
 * what keeps the divergence visible rather than silently rendering them
 * as zero. Transmission IS carried, for the glass pass's splice of the
 * generated material function (fc_glass_fs.sh): the mesh lighting
 * ignores it, as the header says.
 *
 * geometryNormal is the world-space shading normal the document states
 * (a normal map, typically), or ZERO when it states none: a consumer
 * keeps the mesh's own normal for a zero-length one. Zero rather than
 * the mesh normal because the struct is filled before the geometry is
 * in hand, and because "unstated" has to stay distinguishable from
 * "stated as the mesh normal" for the two-sided flip that follows.
 */
struct FcOpenPbr
{
	vec3  baseColor;
	float baseWeight;
	float baseMetalness;
	float baseDiffuseRoughness;
	vec3  specularColor;
	float specularWeight;
	float specularRoughness;
	float specularIor;
	vec3  coatColor;
	float coatWeight;
	float coatRoughness;
	float coatIor;
	float coatDarkening;
	vec3  fuzzColor;
	float fuzzWeight;
	float fuzzRoughness;
	vec3  emissionColor;
	float emissionLuminance;
	float geometryOpacity;
	float transmissionWeight;
	vec3  transmissionColor;
	float transmissionDepth;
	vec3  geometryNormal;
};

/* What a generated material (a MaterialX document, docs/CyclesIntegration.md
 * sec 6.10) may ask of the geometry. It is passed BY VALUE rather than
 * read from the varyings where it is used: the SPIR-V path resolves a
 * varying only inside main(), the same restriction gl_FragCoord carries,
 * so a generated function at file scope cannot touch one.
 */
struct FcMtlxGeom
{
	vec3 normalWorld;
	vec3 tangentWorld;
	vec3 bitangentWorld;
	vec3 positionWorld;
	vec3 normalObject;
	vec3 positionObject;
	vec2 texcoord0;
	vec3 color0;
};

/* Fill one from what a fragment stage has in hand: the view-space
 * normal, position, the object-space pair, the texture coordinate and
 * the vertex colour. Shared by the mesh stage and the glass stage, so a
 * generated function sees the same geometry from either.
 *
 * The tangent frame is the cotangent-frame trick the bump path uses
 * (screen-space derivatives of position against those of the uv): no
 * vertex tangents, any uv source. It is what a normalmap node needs to
 * mean anything -- the frame this stood in for before was the view's
 * own x axis, which put every normal map in camera space. With no uv
 * (a constant one derives to zero) the frame falls back to that axis,
 * orthogonalized, so an untextured draw still hands the graph a basis.
 * The bitangent is the derived one, not cross(n, t): a mirrored uv
 * flips it, and a normal map painted for that uv expects the flip.
 */
FcMtlxGeom fcMtlxGeomFill(vec3 n, vec3 vpos, vec3 onrm, vec3 opos,
                          vec2 uv, vec3 color0)
{
	FcMtlxGeom g;
	vec3 dp1 = dFdx(vpos);
	vec3 dp2 = dFdy(vpos);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);
	vec3 dp2perp = cross(dp2, n);
	vec3 dp1perp = cross(n, dp1);
	vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
	if (dot(t, t) < 1.0e-20 || dot(b, b) < 1.0e-20)
	{
		t = vec3(1.0, 0.0, 0.0);
		t = t - n * dot(t, n);
		if (dot(t, t) < 1.0e-6)
			t = vec3(0.0, 1.0, 0.0) - n * dot(vec3(0.0, 1.0, 0.0), n);
		b = cross(n, t);
	}
	t = normalize(t - n * dot(t, n));
	b = normalize(b - n * dot(b, n));
	g.normalWorld = normalize(mul(u_invView, vec4(n, 0.0)).xyz);
	g.tangentWorld = normalize(mul(u_invView, vec4(t, 0.0)).xyz);
	g.bitangentWorld = normalize(mul(u_invView, vec4(b, 0.0)).xyz);
	g.positionWorld = mul(u_invView, vec4(vpos, 1.0)).xyz;
	g.normalObject = normalize(onrm);
	g.positionObject = opos;
	g.texcoord0 = uv;
	g.color0 = color0;
	return g;
}

// The spec's own defaults, so a caller states only what it knows.
void fcOpenPbrDefaults(out FcOpenPbr m)
{
	m.baseColor = vec3_splat(0.8);
	m.baseWeight = 1.0;
	m.baseMetalness = 0.0;
	m.baseDiffuseRoughness = 0.0;
	m.specularColor = vec3_splat(1.0);
	m.specularWeight = 1.0;
	m.specularRoughness = 0.3;
	m.specularIor = 1.5;
	m.coatColor = vec3_splat(1.0);
	m.coatWeight = 0.0;
	m.coatRoughness = 0.1;
	m.coatIor = 1.6;
	m.coatDarkening = 1.0;
	m.fuzzColor = vec3_splat(1.0);
	m.fuzzWeight = 0.0;
	m.fuzzRoughness = 0.5;
	m.emissionColor = vec3_splat(1.0);
	m.emissionLuminance = 0.0;
	m.geometryOpacity = 1.0;
	m.transmissionWeight = 0.0;
	m.transmissionColor = vec3_splat(1.0);
	m.transmissionDepth = 0.0;
	m.geometryNormal = vec3_splat(0.0);
}

/* OpenPBR clamps its inputs (v1.2 PR #277), and the specular roughness
 * carries a floor besides: a mirror-smooth GGX lobe under a delta light
 * is a numerical spike, not a highlight.
 */
void fcOpenPbrClamp(inout FcOpenPbr m)
{
	m.baseWeight = clamp(m.baseWeight, 0.0, 1.0);
	m.baseColor = clamp(m.baseColor, vec3_splat(0.0), vec3_splat(1.0));
	m.baseDiffuseRoughness = clamp(m.baseDiffuseRoughness, 0.0, 1.0);
	m.baseMetalness = clamp(m.baseMetalness, 0.0, 1.0);
	m.specularWeight = max(m.specularWeight, 0.0);
	m.specularColor = clamp(m.specularColor, vec3_splat(0.0), vec3_splat(1.0));
	m.specularRoughness = clamp(m.specularRoughness, 0.05, 1.0);
	m.specularIor = max(m.specularIor, 1.0);
	m.coatWeight = clamp(m.coatWeight, 0.0, 1.0);
	m.coatRoughness = clamp(m.coatRoughness, 0.05, 1.0);
	m.coatIor = max(m.coatIor, 1.0);
	m.coatDarkening = clamp(m.coatDarkening, 0.0, 1.0);
	m.fuzzWeight = clamp(m.fuzzWeight, 0.0, 1.0);
	m.fuzzRoughness = clamp(m.fuzzRoughness, 0.0, 1.0);
	m.geometryOpacity = clamp(m.geometryOpacity, 0.0, 1.0);
	m.transmissionWeight = clamp(m.transmissionWeight, 0.0, 1.0);
	m.transmissionColor = clamp(m.transmissionColor, vec3_splat(0.0),
	                            vec3_splat(1.0));
	m.transmissionDepth = max(m.transmissionDepth, 0.0);
}

/* The shading normal a generated material stated, in VIEW space, or the
 * mesh's own when it stated none (fcOpenPbrDefaults leaves it zero).
 * Oriented like the mesh normal it replaces: a two-sided draw has
 * already flipped `n` toward the viewer, and a normal map perturbs the
 * face it is painted on, so a stated normal pointing away from `n`'s
 * hemisphere is reflected into it rather than trusted -- the map was
 * authored on the front, and this is the back of the same face.
 */
vec3 fcOpenPbrShadingNormal(FcOpenPbr m, vec3 n)
{
	if (dot(m.geometryNormal, m.geometryNormal) < 0.25)
		return n;
	vec3 sn = normalize(mul(u_view, vec4(m.geometryNormal, 0.0)).xyz);
	if (dot(sn, n) < 0.0)
		sn = normalize(sn - 2.0 * dot(sn, n) * n);
	return sn;
}

/* The IOR ratio the specular lobe sees. A coat refracts what reaches
 * the base, so the ratio is taken against the coat rather than against
 * air wherever one is present.
 */
float fcOpenPbrSpecEta(FcOpenPbr m)
{
	float etaSc = m.specularIor / max(m.coatIor, FC_PBR_EPS);
	if (etaSc < 1.0)
		etaSc = 1.0 / max(etaSc, FC_PBR_EPS);
	return mix(m.specularIor, etaSc, m.coatWeight);
}

/* The dielectric specular lobe's directional albedo against the
 * environment. The split-sum fit is f0 * A + fEdge * B, with fEdge the
 * reflectance the grazing half carries -- one for a full-strength
 * dielectric, Schlick's F90. OpenPBR's specular_weight lowers the IOR
 * toward air, and at air the lobe is gone at EVERY angle, the edge
 * included; left at one, a surface stating specular_weight 0 still took
 * the fit's whole edge term from the environment and drew 1.07x the
 * closed form (docs/MaterialStorage.md sec 17.24). The edge is scaled
 * by the same weight as f0, the convention KHR_materials_specular
 * states for the same knob; the punctual path had it exactly all along
 * (fcPbrFresnelDielectricMod).
 */
vec3 fcOpenPbrSpecEnvAlbedo(FcOpenPbr m, float ndv)
{
	float specF0 = clamp(m.specularWeight
	                     * fcPbrFresnelDielectricNormal(fcOpenPbrSpecEta(m)),
	                     0.0, 1.0);
	float edge = min(m.specularWeight, 1.0);
	return fcPbrGgxAlbedo(ndv, m.specularRoughness,
	                      vec3_splat(specF0), vec3_splat(edge));
}

/* The slab weights: how much of the light arriving at the surface each
 * lobe is allowed to claim, after the layers above it have taken their
 * share. This is the whole of OpenPBR's layering -- the lobes
 * themselves are ordinary BRDFs -- so it is where a coat darkens its
 * base and a fuzz dulls what is under it.
 */
struct FcPbrWeights
{
	vec3 fuzz;
	vec3 coat;
	vec3 metal;
	vec3 spec;
	vec3 diff;
};

void fcOpenPbrWeights(FcOpenPbr m, float ndv, out FcPbrWeights w)
{
	float metalness = m.baseMetalness;
	vec3 diffAlbedo = m.baseWeight * m.baseColor;
	vec3 metalAlbedo = fcPbrGgxAlbedo(ndv, m.specularRoughness,
	                                  diffAlbedo, m.specularColor);
	vec3 specAlbedo = fcOpenPbrSpecEnvAlbedo(m, ndv);
	float fuzzAlbedo = m.fuzzWeight > 0.0
		? fcPbrSheenAlbedo(ndv, clamp(m.fuzzRoughness, 0.01, 1.0))
		: 0.0;

	// Under the fuzz.
	vec3 coatedBase = mix(vec3_splat(1.0),
	                      vec3_splat(1.0 - fuzzAlbedo), m.fuzzWeight);

	// Under the coat. Its inner face reflects part of what the base
	// sends back up, and the light bounces between the two until it
	// leaves -- a geometric series whose sum is the darkening factor.
	vec3 substrate = coatedBase;
	if (m.coatWeight > 0.0)
	{
		float kr = 1.0 - (1.0 - fcPbrFresnelDielectricAvg(m.coatIor))
			/ fcPbrSqr(m.coatIor);
		float ks = fcPbrFresnelDielectric(abs(ndv), m.coatIor);
		float fs = fcPbrFresnelDielectricNormal(fcOpenPbrSpecEta(m));
		// Estimated roughness of the whole base, which decides how
		// diffuse that internal reflection is.
		float rd = mix(1.0, m.specularRoughness,
		               min(1.0, m.specularWeight * fs));
		float rb = mix(rd, m.specularRoughness, metalness);
		float k = mix(ks, kr, rb);
		vec3 baseAlbedo = mix(diffAlbedo, metalAlbedo, metalness);
		vec3 delta = max(vec3_splat(1.0 - k), vec3_splat(0.0))
			/ max(vec3_splat(1.0) - baseAlbedo * k * m.coatColor,
			      vec3_splat(FC_PBR_EPS));
		vec3 darkening = mix(vec3_splat(1.0), delta, m.coatDarkening);
		vec3 coatAlbedo = fcPbrGgxAlbedo(ndv, m.coatRoughness,
		                                 vec3_splat(fcPbrFresnelDielectricNormal(m.coatIor)),
		                                 vec3_splat(1.0));
		substrate = coatedBase * mix(vec3_splat(1.0),
		                             darkening * m.coatColor
		                             * (vec3_splat(1.0) - coatAlbedo),
		                             m.coatWeight);
	}

	vec3 dielectric = substrate * max(0.0, 1.0 - metalness);

	w.fuzz = vec3_splat(m.fuzzWeight);
	w.coat = coatedBase * m.coatWeight;
	w.metal = substrate * metalness;
	w.spec = dielectric * m.specularColor;
	w.diff = dielectric * (vec3_splat(1.0) - specAlbedo);
}

/* The surface's response to one light. v and l are unit vectors in the
 * local frame (z = the shading normal), both pointing away from the
 * surface. The caller multiplies by the light's colour and by the
 * cosine, exactly as the reference does.
 */
vec3 fcOpenPbrBsdf(FcOpenPbr m, FcPbrWeights w, vec3 v, vec3 l)
{
	float ndv = v.z;
	float ndl = l.z;
	if (ndl < FC_PBR_EPS || ndv < FC_PBR_EPS)
		return vec3_splat(0.0);

	vec3 f = vec3_splat(0.0);
	vec3 h = normalize(v + l);
	float ndh = max(h.z, 0.0);
	float vdh = max(dot(v, h), 0.0);
	float j = 1.0 / max(4.0 * ndv * ndl, FC_PBR_EPS);

	// Fuzz.
	if (m.fuzzWeight > 0.0)
		f += w.fuzz * m.fuzzColor
			* fcPbrFuzzEval(l, ndv, m.fuzzRoughness);

	// Coat: a plain dielectric microfacet layer, unaffected by
	// specular_weight (that knob belongs to the base).
	if (m.coatWeight > 0.0)
	{
		float a = max(fcPbrSqr(m.coatRoughness), FC_PBR_ALPHA_MIN);
		float dg = min(fcPbrGgxD(ndh, a) * fcPbrGgxG2(ndv, ndl, a) * j,
		               FC_PBR_SPEC_MAX);
		f += w.coat * (fcPbrFresnelDielectric(vdh, m.coatIor) * dg);
	}

	float a = max(fcPbrSqr(m.specularRoughness), FC_PBR_ALPHA_MIN);
	float dg = min(fcPbrGgxD(ndh, a) * fcPbrGgxG2(ndv, ndl, a) * j,
	               FC_PBR_SPEC_MAX);

	// Metal: the conductor lobe, tinted at normal incidence by the base
	// colour and at 82 degrees by the specular colour.
	if (m.baseMetalness > 0.0)
		f += w.metal * fcPbrFresnelF82(vdh, m.baseWeight * m.baseColor,
		                               m.specularColor) * dg;

	// Dielectric specular, and the diffuse it sits over.
	if (m.baseMetalness < 1.0)
	{
		f += w.spec * (fcPbrFresnelDielectricMod(vdh, fcOpenPbrSpecEta(m),
		                                         m.specularWeight) * dg);
		f += w.diff * fcPbrDiffuseEval(m.baseWeight * m.baseColor,
		                               m.baseDiffuseRoughness,
		                               ndv, ndl, dot(v, l));
	}
	return f;
}

/* The same surface against the environment, which reaches it as an
 * irradiance (the diffuse-like lobes) and as a prefiltered radiance in
 * the mirror direction (the microfacet ones). Each lobe's directional
 * albedo is what it takes from that -- the split-sum's whole purpose --
 * and the caller supplies the two radiances because only it knows how
 * this engine's environment is stored.
 *
 * Two mip levels are needed, because a coat is smoother than what it
 * covers: prefSpec at the specular roughness, prefCoat at the coat's.
 */
vec3 fcOpenPbrEnv(FcOpenPbr m, FcPbrWeights w, float ndv,
                  vec3 irradiance, vec3 prefSpec, vec3 prefCoat)
{
	vec3 c = vec3_splat(0.0);

	// Diffuse and fuzz take the irradiance. The reference leaves the
	// fuzz's environment reflection untinted; it is tinted here, which
	// is both what fuzz_color means and what Cycles' Sheen Tint does.
	if (m.baseMetalness < 1.0)
		c += irradiance * w.diff
			* fcPbrDiffuseAlbedo(m.baseWeight * m.baseColor,
			                     m.baseDiffuseRoughness, ndv);
	if (m.fuzzWeight > 0.0)
		c += irradiance * w.fuzz * m.fuzzColor
			* fcPbrSheenAlbedo(ndv, clamp(m.fuzzRoughness, 0.01, 1.0));

	// The microfacet lobes take the prefiltered radiance.
	if (m.baseMetalness > 0.0)
		c += prefSpec * w.metal
			* fcPbrGgxAlbedo(ndv, m.specularRoughness,
			                 m.baseWeight * m.baseColor, m.specularColor);
	if (m.baseMetalness < 1.0)
		c += prefSpec * w.spec * fcOpenPbrSpecEnvAlbedo(m, ndv);
	if (m.coatWeight > 0.0)
		c += prefCoat * w.coat
			* fcPbrGgxAlbedo(ndv, m.coatRoughness,
			                 vec3_splat(fcPbrFresnelDielectricNormal(m.coatIor)),
			                 vec3_splat(1.0));
	return c;
}

/* The local shading frame. z is the normal; x carries the tangential
 * part of the view direction, which is +z in view space. The view
 * vector is then (sqrt(1 - ndv^2), 0, ndv) by construction, which is
 * the configuration the sheen LTC fit assumes -- so nothing has to
 * rotate it.
 */
void fcOpenPbrFrame(vec3 n, out vec3 tx, out vec3 ty)
{
	vec3 t = vec3(-n.x * n.z, -n.y * n.z, 1.0 - n.z * n.z);
	float len = length(t);
	// Looking straight down the normal leaves no tangential part, and
	// any tangent will do -- every lobe here is isotropic.
	tx = len > 1.0e-5 ? t / len
	                  : normalize(vec3(1.0 - n.x * n.x, -n.x * n.y, -n.x * n.z));
	ty = cross(n, tx);
}

#endif // FC_OPENPBR_SH
