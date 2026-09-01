// CENTROID on every surface interpolant, and it is not cosmetic.
//
// A pixel the primitive covers only PARTLY is still shaded once, at the
// pixel CENTRE -- and for a silhouette pixel that centre lies OUTSIDE
// the triangle. Every interpolant is then extrapolated past the edge it
// was defined on: a position lands off the surface, a palette index
// lands on the wrong entry, and the colour that comes back has no
// relation to its neighbours. Measured on the material icons, one such
// pixel took either of two values 100 levels apart depending on which of
// two draws won it. Centroid moves the sampling point to somewhere the
// primitive actually covers, so an interpolant is never extrapolated.
//
// It costs nothing when multisampling is off: with one sample, the
// centroid IS the pixel centre.
//
// v_dist and v_line are deliberately NOT centroid. Analytic line
// coverage (fs_fc_line) computes how much of the pixel a line covers
// from the distance AT THE CENTRE; moving that sampling point into the
// covered area is exactly the measurement it must not have.
centroid vec4 v_color0  : COLOR0;
centroid vec4 v_color1  : COLOR1;
centroid vec4 v_color2  : COLOR2;
centroid vec3 v_normal  : NORMAL;
centroid vec3 v_wpos    : TEXCOORD0;
vec2 v_dist    : TEXCOORD1;
centroid vec2 v_texcoord0 : TEXCOORD2;
centroid vec3 v_vpos    : TEXCOORD3;
centroid vec3 v_opos    : TEXCOORD4;
centroid vec3 v_onrm    : TEXCOORD5;
centroid vec3 v_findex : TEXCOORD6;
vec2 v_line    : TEXCOORD7;

vec3 a_position : POSITION;
vec3 a_normal   : NORMAL;
vec4 a_color0   : COLOR0;
vec4 a_color1   : COLOR1;
vec4 a_color2   : COLOR2;
vec4 a_color3   : COLOR3;
vec2 a_texcoord0 : TEXCOORD0;

vec4 i_data0    : TEXCOORD31;
vec4 i_data1    : TEXCOORD30;
vec4 i_data2    : TEXCOORD29;
vec4 i_data3    : TEXCOORD28;
vec4 i_data4    : TEXCOORD27;
