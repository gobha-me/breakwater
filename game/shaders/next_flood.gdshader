shader_type spatial;
render_mode unshaded, blend_mix, cull_disabled, depth_draw_never;
// Tiles the next tide will flood: diagonal blue hatching, so the warning
// can't be mistaken for water already standing on a tile.

uniform vec4 color : source_color = vec4(0.25, 0.55, 1.0, 0.75);
uniform float stripes = 5.0;   // Stripes per tile.

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	float s = fract((world_pos.x + world_pos.z) * stripes);
	float stripe = smoothstep(0.42, 0.5, s) - smoothstep(0.92, 1.0, s);
	ALBEDO = color.rgb;
	ALPHA = stripe * color.a;
}
