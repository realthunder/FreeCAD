$input v_normal, v_color0

/*
 * FreeCAD CAD-mesh fragment shader: single headlight Blinn-Phong.
 *
 * u_matColor    : rgba diffuse; used when u_params.x == 0
 * u_matEmissive : rgb emissive add
 * u_matSpecular : rgb specular, w = shininess (0..1 Coin convention)
 * u_params      : x = per-vertex color, y = lighting on, z = two-sided
 */

#include <bgfx_shader.sh>

uniform vec4 u_matColor;
uniform vec4 u_matEmissive;
uniform vec4 u_matSpecular;
uniform vec4 u_params;

void main()
{
	vec4 base = mix(u_matColor, v_color0, u_params.x);
	vec3 color = base.rgb;

	if (u_params.y > 0.5)
	{
		vec3 n = normalize(v_normal);
		// headlight along the view axis
		float ndl = n.z;
		if (u_params.z > 0.5)
			ndl = abs(ndl);
		else
			ndl = max(ndl, 0.0);

		float shininess = max(u_matSpecular.w * 128.0, 1.0);
		float spec = pow(max(abs(n.z), 0.0), shininess);

		color = base.rgb * (0.2 + 0.8 * ndl)
			+ u_matSpecular.rgb * (spec * 0.75);
	}

	color += u_matEmissive.rgb;
	gl_FragColor = vec4(color, base.a);
}
