shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_never, specular_schlick_ggx;
// Stylised sea: colour deepens with the water's thickness above the ground,
// foam rings where the water meets a tile, and gentle ripples in the normal.
// `phase` drives the ripples; the board view advances it per frame in
// captures, so golden images stay deterministic.

uniform vec4 shallow : source_color = vec4(0.36, 0.72, 0.82, 0.55);
uniform vec4 deep : source_color = vec4(0.1, 0.32, 0.55, 0.88);
uniform vec4 foam_color : source_color = vec4(0.95, 0.98, 1.0, 0.9);
uniform float foam_width = 0.09;
uniform float depth_range = 0.9;
uniform float phase = 0.0;
uniform sampler2D depth_texture : hint_depth_texture, filter_linear_mipmap;

varying vec3 world_pos;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	float depth = texture(depth_texture, SCREEN_UV).x;
	vec3 ndc = vec3(SCREEN_UV * 2.0 - 1.0, depth);
	vec4 view = INV_PROJECTION_MATRIX * vec4(ndc, 1.0);
	view.xyz /= view.w;
	float thickness = max(-view.z - (-VERTEX.z), 0.0);

	float d = smoothstep(0.0, depth_range, thickness);
	vec4 water = mix(shallow, deep, d);
	float ripple = sin(world_pos.x * 3.1 + phase * 1.3) * sin(world_pos.z * 2.7 - phase * 1.1);
	float foam = 1.0 - smoothstep(0.0, foam_width * (1.0 + 0.35 * ripple), thickness);

	ALBEDO = mix(water.rgb, foam_color.rgb, foam * foam_color.a);
	ALPHA = mix(water.a, 1.0, foam * 0.8);
	ROUGHNESS = 0.15;
	SPECULAR = 0.6;
	NORMAL_MAP = normalize(vec3(0.5 + 0.08 * ripple, 0.5 + 0.08 * cos(world_pos.z * 3.3 + phase), 1.0));
}
