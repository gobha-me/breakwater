extends Node3D
## Tiles, buildings and water, drawn from the mission's queries. Holds no
## rules: every colour and height below is read from the bridge.

const TILE := 1.0
const BASE_HEIGHT := 0.2
const STEP_HEIGHT := 0.15  # Elevation runs 0-8 (docs/RULES.md): keep the beach low.

const SAND := [Color(0.85, 0.78, 0.55), Color(0.82, 0.76, 0.54), Color(0.75, 0.72, 0.5),
		Color(0.6, 0.66, 0.42), Color(0.45, 0.62, 0.35), Color(0.4, 0.58, 0.32),
		Color(0.38, 0.55, 0.3), Color(0.45, 0.5, 0.4), Color(0.5, 0.5, 0.45)]
const FLOODED := Color(0.25, 0.42, 0.55)
const FLOODED_TINT := Color(0.45, 0.62, 0.8)
## Per band (sand, grass, stony ground), multiplied into the texture.
const BAND_TINT := [Color(1.0, 1.0, 1.0), Color(0.72, 0.8, 0.66), Color(0.95, 0.95, 0.95)]
const TERRAIN_SHADER := preload("res://shaders/terrain.gdshader")
const WATER_SHADER := preload("res://shaders/water.gdshader")
const NEXT_FLOOD_SHADER := preload("res://shaders/next_flood.gdshader")
## Decorative props per band (sand, grass, stony ground).
const TOPPERS := ["res://assets/models/driftwood.glb", "res://assets/models/tuft.glb", "res://assets/models/stones.glb"]
## Share of eligible tiles that get a topper.
const TOPPER_DENSITY := 0.4
const CLIFF_TEXTURE := "res://assets/textures/terrain_cliff.jpg"
const HOUSE := Color(0.78, 0.42, 0.32)
const BUILDING_RING := Color(1.0, 0.82, 0.35)
## A cottage below full HP: a dark wash over the model that reads as damaged.
const DAMAGED := Color(0.08, 0.04, 0.02, 0.5)
const COTTAGE_MODEL := "res://assets/models/cottage.glb"
const SEAWALL_MODEL := "res://assets/models/seawall.glb"
const RUBBLE := Color(0.22, 0.16, 0.12)  # Dark, so a razed house reads against the grey high ground.

var _mission: BreakwaterMission
var _size := 0
var _tile_mats: Array[Material] = []
var _next_marks: Array[MeshInstance3D] = []
var _toppers := {}  # Vector2i -> Node3D, decoration only
var _houses := {}  # Vector2i -> {"body", "rubble", "flash", "label", "ring"}
var _water: MeshInstance3D
var _water_mat: ShaderMaterial
var _phase := 0.0
## Captures and recordings advance the ripples by frame, not by wall clock,
## so the same frame always shows the same water.
var _frame_locked := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		_frame_locked = _frame_locked or arg.begins_with("--capture")
	_frame_locked = _frame_locked or OS.get_cmdline_args().has("--write-movie")


func _process(delta: float) -> void:
	_phase += 1.0 / 60.0 if _frame_locked else delta
	if _water_mat != null:
		_water_mat.set_shader_parameter("phase", _phase)


func build(mission: BreakwaterMission) -> void:
	_mission = mission
	_size = mission.get_size()
	for child in get_children():
		child.queue_free()
	_tile_mats.clear()
	_next_marks.clear()
	_houses.clear()
	_toppers.clear()
	for y in _size:
		for x in _size:
			_build_tile(Vector2i(x, y))
	_build_water()
	_build_breakwater()
	_build_toppers()
	sync()


## World position of the top centre of tile `c`.
func top_of(c: Vector2i) -> Vector3:
	var elevation := _mission.get_elevation(c.x, c.y) if _on_board(c) else 0
	return Vector3(c.x * TILE, tile_top(elevation), c.y * TILE)


static func tile_top(elevation: int) -> float:
	return BASE_HEIGHT + elevation * STEP_HEIGHT


func center() -> Vector3:
	var mid := (_size - 1) * TILE * 0.5
	return Vector3(mid, 0.0, mid)


## Matches every tile, building and the water to the mission as it is now.
func sync() -> void:
	for y in _size:
		for x in _size:
			var i := y * _size + x
			var flooded := _mission.is_flooded(x, y)
			_tint_tile(_tile_mats[i], _mission.get_elevation(x, y), flooded)
			_next_marks[i].visible = (not flooded and _mission.floods_next(x, y)
					and _mission.get_outcome() == BreakwaterMission.OUTCOME_IN_PROGRESS)
	for c in _houses:
		_show_house(c, _mission.get_building_hp(c.x, c.y))
	# A prop gives way to anything standing on its tile, so it can never hide
	# a unit or its health ring, and it goes under with the tide.
	var occupied := {}
	for u in _mission.get_units():
		if u["alive"]:
			occupied[Vector2i(u["x"], u["y"])] = true
	for c in _toppers:
		(_toppers[c] as Node3D).visible = not _mission.is_flooded(c.x, c.y) and not occupied.has(c)
	_water.position.y = _water_height(_mission.get_tide())


func tween_tide(tide: int, duration: float) -> Tween:
	var tween := create_tween()
	tween.tween_property(_water, "position:y", _water_height(tide), duration).set_trans(Tween.TRANS_SINE)
	return tween


func flash_building(c: Vector2i, hp: int) -> Tween:
	var house: Dictionary = _houses.get(c, {})
	if house.is_empty():
		return null
	var flash: StandardMaterial3D = house["flash"]
	var tween := create_tween()
	tween.tween_property(flash, "albedo_color", Color(1, 0.2, 0.1, 0.0), 0.3).from(Color(1, 0.2, 0.1, 0.75))
	tween.tween_callback(func() -> void: _show_house(c, hp))
	return tween


func _build_tile(c: Vector2i) -> void:
	var elevation := _mission.get_elevation(c.x, c.y)
	var height := tile_top(elevation)
	var tile := _box(Vector3(TILE * 0.95, height, TILE * 0.95))
	tile.position = Vector3(c.x * TILE, height * 0.5, c.y * TILE)
	tile.material_override = _terrain_material(elevation)
	_tile_mats.append(tile.material_override)
	add_child(tile)

	# Blue hatching over tiles the next tide will take (next_flood.gdshader).
	var mark := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(TILE * 0.9, TILE * 0.9)
	mark.mesh = plane
	var mat := ShaderMaterial.new()
	mat.shader = NEXT_FLOOD_SHADER
	mark.material_override = mat
	mark.position = Vector3(c.x * TILE, height + 0.01, c.y * TILE)
	_next_marks.append(mark)
	add_child(mark)

	if _mission.get_building_hp(c.x, c.y) > 0:
		var top := Vector3(c.x * TILE, height, c.y * TILE)
		var body := ModelFit.instance(COTTAGE_MODEL, 0.76, 0.95)
		if body == null:
			body = _box(Vector3(0.55, 0.6, 0.55))
			body.position.y = 0.3
			(body as MeshInstance3D).material_override.albedo_color = HOUSE
		var holder := Node3D.new()
		holder.position = top
		holder.add_child(body)
		add_child(holder)
		var flash := StandardMaterial3D.new()
		flash.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		flash.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		flash.albedo_color = Color(1, 0.2, 0.1, 0.0)
		_set_overlay(body, flash)
		var rubble := _box(Vector3(0.65, 0.18, 0.65))
		(rubble as MeshInstance3D).material_override.albedo_color = RUBBLE
		rubble.position = top + Vector3(0, 0.09, 0)
		add_child(rubble)
		# Health on the ground, as for units: a segmented ring round the
		# footprint. The ✕ label only marks rubble.
		var ring := HealthRing.new()
		ring.setup(_mission.get_building_max_hp(), 0.4, 0.48, BUILDING_RING)
		ring.position = top
		add_child(ring)
		var label := Label3D.new()
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.font_size = 64
		label.outline_size = 14
		label.position = top + Vector3(0, 0.45, 0)
		add_child(label)
		_houses[c] = {"body": holder, "rubble": rubble, "flash": flash, "label": label, "ring": ring}


func _show_house(c: Vector2i, hp: int) -> void:
	var house: Dictionary = _houses[c]
	var label: Label3D = house["label"]
	(house["body"] as Node3D).visible = hp > 0
	(house["rubble"] as Node3D).visible = hp <= 0
	var ring: HealthRing = house["ring"]
	ring.visible = hp > 0
	ring.show_hp(hp)
	var flash: StandardMaterial3D = house["flash"]
	flash.albedo_color = DAMAGED if hp > 0 and hp < _mission.get_building_max_hp() else Color(1, 0.2, 0.1, 0.0)
	label.text = "" if hp > 0 else "✕"
	label.modulate = Color(1, 0.4, 0.3)


## A tile's material: the band's ground texture on top (sand low, grass in
## the middle, stony ground high), the cliff texture on the sides, falling back
## to the flat palette if the textures are missing.
func _terrain_material(elevation: int) -> Material:
	var band := "sand" if elevation <= 2 else ("grass" if elevation <= 6 else "rock")
	var top_path := "res://assets/textures/terrain_%s.jpg" % band
	if not ResourceLoader.exists(top_path) or not ResourceLoader.exists(CLIFF_TEXTURE):
		return StandardMaterial3D.new()
	var mat := ShaderMaterial.new()
	mat.shader = TERRAIN_SHADER
	mat.set_shader_parameter("top_texture", load(top_path))
	mat.set_shader_parameter("side_texture", load(CLIFF_TEXTURE))
	return mat


## Elevation reads as brightness (higher is lighter); flooded tiles turn to a
## wet blue-grey.
func _tint_tile(mat: Material, elevation: int, flooded: bool) -> void:
	if mat is ShaderMaterial:
		# Grass is the busiest texture and sits under most of the action: calm
		# it so units and highlights stay the brightest things on the board.
		var band: Color = BAND_TINT[0 if elevation <= 2 else (1 if elevation <= 6 else 2)]
		var light := 0.7 + 0.05 * elevation
		mat.set_shader_parameter("tint", FLOODED_TINT if flooded else band * Color(light, light, light))
		mat.set_shader_parameter("wet", 1.0 if flooded else 0.0)
	else:
		(mat as StandardMaterial3D).albedo_color = FLOODED if flooded else SAND[clampi(elevation, 0, SAND.size() - 1)]


func _build_water() -> void:
	_water = MeshInstance3D.new()
	var plane := PlaneMesh.new()
	var extent := _size * TILE + 30.0
	plane.size = Vector2(extent, extent)
	_water.mesh = plane
	var mat := ShaderMaterial.new()
	mat.shader = WATER_SHADER
	_water.material_override = mat
	_water_mat = mat
	var c := center()
	_water.position = Vector3(c.x, _water_height(_mission.get_tide()), c.z)
	add_child(_water)


## Flooded means elevation < tide, so the surface sits half a step below the
## first dry level: flooded tiles are covered, dry ones stand clear.
func _water_height(tide: int) -> float:
	return BASE_HEIGHT + (tide - 0.5) * STEP_HEIGHT


## Decoration only: small props scattered by a presentation-only hash of the
## tile and the seed, never the game's RNG streams. At most one per tile, never
## on a cottage's tile; sync() hides a prop while anything stands on its tile
## and once the tile floods. Neither the rules nor input read them.
func _build_toppers() -> void:
	for y in _size:
		for x in _size:
			var c := Vector2i(x, y)
			if _mission.get_building_hp(x, y) > 0:
				continue
			var h := _hash(c)
			if float(h % 1000) / 1000.0 >= TOPPER_DENSITY:
				continue
			var elevation := _mission.get_elevation(x, y)
			var band := 0 if elevation <= 2 else (1 if elevation <= 6 else 2)
			var prop := ModelFit.instance(TOPPERS[band], 0.42, 0.26)
			if prop == null:
				continue
			var corner := Vector3(0.22 if (h >> 10) & 1 else -0.22, 0.0, 0.22 if (h >> 11) & 1 else -0.22)
			prop.position = top_of(c) + corner
			prop.rotation.y = float((h >> 12) % 360) * PI / 180.0
			add_child(prop)
			_toppers[c] = prop


## A stable per-tile hash from the tile and the seed (FNV-1a over both).
func _hash(c: Vector2i) -> int:
	var h := 2166136261
	for v in [c.x, c.y, _mission.get_seed() & 0xFFFFFFFF]:
		h = ((h ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
	return h


## Decoration only: the old breakwater out at sea, beyond row 0. It stands
## on the sea floor, so the rising tide visibly swallows it. Not game state:
## nothing here is read by the rules or by input.
func _build_breakwater() -> void:
	for i in 4:
		var wall := ModelFit.instance(SEAWALL_MODEL, 1.8, 1.0)
		if wall == null:
			return
		wall.position = Vector3(i * 2.0 + 0.5, 0.0, -1.4)
		add_child(wall)


static func _set_overlay(node: Node, overlay: Material) -> void:
	if node is GeometryInstance3D:
		node.material_overlay = overlay
	for child in node.get_children():
		_set_overlay(child, overlay)


func _on_board(c: Vector2i) -> bool:
	return c.x >= 0 and c.x < _size and c.y >= 0 and c.y < _size


func _box(size: Vector3) -> MeshInstance3D:
	var instance := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	instance.mesh = mesh
	instance.material_override = StandardMaterial3D.new()
	return instance
