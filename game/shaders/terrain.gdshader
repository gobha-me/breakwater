shader_type spatial;
// Terrain tiles: the top face shows the band's ground texture (sand, grass or
// stony ground by elevation) and the sides show the cliff texture, both mapped
// in world space so columns of any height tile without stretching. `tint`
// carries what the game needs to read on top: elevation shading, flooding.

uniform sampler2D top_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D side_texture : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 tint : source_color = vec4(1.0);
uniform float top_scale = 0.5;   // Texture repeats per tile on the top face.
uniform float side_scale = 0.6;
uniform float wet = 0.0;          // 1 for flooded tiles: darker and glossier.

varying vec3 world_pos;
varying vec3 world_normal;

void vertex() {
	world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	world_normal = normalize((MODEL_MATRIX * vec4(NORMAL, 0.0)).xyz);
}

void fragment() {
	vec3 n = abs(world_normal);
	vec3 top = texture(top_texture, world_pos.xz * top_scale).rgb;
	vec3 side_x = texture(side_texture, world_pos.zy * side_scale).rgb;
	vec3 side_z = texture(side_texture, world_pos.xy * side_scale).rgb;
	vec3 side = n.x > n.z ? side_x : side_z;
	vec3 albedo = n.y > 0.5 ? top : side * 0.85;
	ALBEDO = albedo * tint.rgb * mix(1.0, 0.7, wet);
	ROUGHNESS = mix(0.9, 0.35, wet);
}
