#version 330 core // version 3.3

in		vec2	attr_pos_screen;
in		vec2	attr_uv;
in		vec4	attr_col;

out		vec2	vs_uv;
out		vec4	vs_col;

uniform	vec2	screen_dim;

void main () {
	vec2 pos = attr_pos_screen / screen_dim;
	pos.y = 1 -pos.y; // positings are specified top-down
	
	gl_Position =		vec4(pos * 2 -1, 0,1);
	vs_uv =				attr_uv;
	vs_col =			attr_col;
}
