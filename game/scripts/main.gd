extends Node3D
## Placeholder diorama: draws the core board as stepped tiles so the
## C++ -> Godot path is visible end to end. Meshy assets replace the boxes later.

const TILE := 1.0
const BASE_HEIGHT := 0.2
const STEP_HEIGHT := 0.15  # Elevation runs 0-8 (docs/RULES.md): keep the beach low.

@export var board_seed := 42
@export var tide_steps := 1

var board: BreakwaterBoard


func _ready() -> void:
	board = BreakwaterBoard.new()
	board.generate(board_seed)
	for _i in tide_steps:
		board.advance_tide()
	_build_tiles()
	_build_water()
	_build_environment()


func _tile_top(elevation: int) -> float:
	return BASE_HEIGHT + elevation * STEP_HEIGHT


func _build_tiles() -> void:
	var size := board.get_size()
	for y in size:
		for x in size:
			var elevation := board.get_elevation(x, y)
			var height := _tile_top(elevation)
			var tile := _box(Vector3(TILE * 0.95, height, TILE * 0.95), _tile_color(elevation))
			tile.position = Vector3(x * TILE, height * 0.5, y * TILE)
			add_child(tile)
			if board.get_building_hp(x, y) > 0:
				var house := _box(Vector3(0.55, 0.6, 0.55), Color(0.78, 0.42, 0.32))
				house.position = Vector3(x * TILE, height + 0.3, y * TILE)
				add_child(house)


func _build_water() -> void:
	# Flooded means elevation < tide, so the surface sits half a step below the
	# first dry level: flooded tiles are covered, dry ones stand clear.
	var water := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	var extent := board.get_size() * TILE + 6.0
	plane.size = Vector2(extent, extent)
	water.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.15, 0.4, 0.65, 0.75)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	water.material_override = mat
	var center := (board.get_size() - 1) * TILE * 0.5
	water.position = Vector3(center, BASE_HEIGHT + (board.get_tide() - 0.5) * STEP_HEIGHT, center)
	add_child(water)


func _build_environment() -> void:
	var center := (board.get_size() - 1) * TILE * 0.5
	var camera := Camera3D.new()
	camera.position = Vector3(center, 9.0, center + 9.0)
	add_child(camera)
	camera.look_at(Vector3(center, 0.0, center + 0.5))

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50.0, -30.0, 0.0)
	sun.shadow_enabled = true
	add_child(sun)

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.55, 0.7, 0.85)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.6, 0.65, 0.7)
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)


func _tile_color(elevation: int) -> Color:
	var palette := [Color(0.85, 0.78, 0.55), Color(0.82, 0.76, 0.54), Color(0.75, 0.72, 0.5),
			Color(0.6, 0.66, 0.42), Color(0.45, 0.62, 0.35), Color(0.4, 0.58, 0.32),
			Color(0.38, 0.55, 0.3), Color(0.45, 0.5, 0.4), Color(0.5, 0.5, 0.45)]
	return palette[clampi(elevation, 0, palette.size() - 1)]


func _box(size: Vector3, color: Color) -> MeshInstance3D:
	var instance := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	instance.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	instance.material_override = mat
	return instance
