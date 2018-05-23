#version 330 core // version 3.3

in		vec2	vs_uv;
in		vec4	vs_col;

out		vec4	frag_col;

uniform sampler2D	tex;

// for wireframe
uniform bool	draw_wireframe = false;

in		vec3	vs_barycentric;

float wireframe_edge_factor () {
	vec3 d = fwidth(vs_barycentric);
	vec3 a3 = smoothstep(vec3(0.0), d*1.5, vs_barycentric);
	return min(min(a3.x, a3.y), a3.z);
}

void main () {
	frag_col = texture(tex, vs_uv) * vs_col;

	//
	if (draw_wireframe) {
		if (wireframe_edge_factor() >= 0.8) discard;
		
		frag_col = mix(vec4(1,1,0,1), vec4(0,0,0,1), wireframe_edge_factor());
	}
}
