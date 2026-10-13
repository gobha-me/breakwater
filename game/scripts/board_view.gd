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
const NEXT_FLOOD := Color(0.3, 0.6, 0.95, 0.55)
const HOUSE := Color(0.78, 0.42, 0.32)
const COTTAGE_MODEL := "res://assets/models/cottage.glb"
const SEAWALL_MODEL := "res://assets/models/seawall.glb"
const RUBBLE := Color(0.22, 0.16, 0.12)  # Dark, so a razed house reads against the grey high ground.

var _mission: BreakwaterMission
var _size := 0
var _tile_mats: Array[StandardMaterial3D] = []
var _next_marks: Array[MeshInstance3D] = []
var _houses := {}  # Vector2i -> {"body": MeshInstance3D, "mat": StandardMaterial3D, "label": Label3D}
var _water: MeshInstance3D


func build(mission: BreakwaterMission) -> void:
	_mission = mission
	_size = mission.get_size()
	for child in get_children():
		child.queue_free()
	_tile_mats.clear()
	_next_marks.clear()
	_houses.clear()
	for y in _size:
		for x in _size:
			_build_tile(Vector2i(x, y))
	_build_water()
	_build_breakwater()
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
			_tile_mats[i].albedo_color = FLOODED if flooded else SAND[clampi(_mission.get_elevation(x, y), 0, SAND.size() - 1)]
			_next_marks[i].visible = (not flooded and _mission.floods_next(x, y)
					and _mission.get_outcome() == BreakwaterMission.OUTCOME_IN_PROGRESS)
	for c in _houses:
		_show_house(c, _mission.get_building_hp(c.x, c.y))
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
	tween.tween_property(flash, "albedo_color:a", 0.0, 0.3).from(0.75)
	tween.tween_callback(func() -> void: _show_house(c, hp))
	return tween


func _build_tile(c: Vector2i) -> void:
	var height := tile_top(_mission.get_elevation(c.x, c.y))
	var tile := _box(Vector3(TILE * 0.95, height, TILE * 0.95))
	tile.position = Vector3(c.x * TILE, height * 0.5, c.y * TILE)
	_tile_mats.append(tile.material_override)
	add_child(tile)

	# A translucent blue film over tiles the next tide will take.
	var mark := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(TILE * 0.9, TILE * 0.9)
	mark.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = NEXT_FLOOD
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mark.material_override = mat
	mark.position = Vector3(c.x * TILE, height + 0.01, c.y * TILE)
	_next_marks.append(mark)
	add_child(mark)

	if _mission.get_building_hp(c.x, c.y) > 0:
		var top := Vector3(c.x * TILE, height, c.y * TILE)
		var body := ModelFit.instance(COTTAGE_MODEL, 0.92, 1.0)
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
		var label := Label3D.new()
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		label.font_size = 64
		label.outline_size = 14
		label.position = top + Vector3(0, 1.15, 0)
		add_child(label)
		_houses[c] = {"body": holder, "rubble": rubble, "flash": flash, "label": label}


func _show_house(c: Vector2i, hp: int) -> void:
	var house: Dictionary = _houses[c]
	var label: Label3D = house["label"]
	(house["body"] as Node3D).visible = hp > 0
	(house["rubble"] as Node3D).visible = hp <= 0
	if hp > 0:
		label.text = "■".repeat(hp)
		label.modulate = Color(1, 0.9, 0.6)
		label.position.y = tile_top(_mission.get_elevation(c.x, c.y)) + 1.15
	else:
		label.text = "✕"
		label.modulate = Color(1, 0.4, 0.3)
		label.position.y = tile_top(_mission.get_elevation(c.x, c.y)) + 0.45


func _build_water() -> void:
	_water = MeshInstance3D.new()
	var plane := PlaneMesh.new()
	var extent := _size * TILE + 30.0
	plane.size = Vector2(extent, extent)
	_water.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.15, 0.4, 0.65, 0.75)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_water.material_override = mat
	var c := center()
	_water.position = Vector3(c.x, _water_height(_mission.get_tide()), c.z)
	add_child(_water)


## Flooded means elevation < tide, so the surface sits half a step below the
## first dry level: flooded tiles are covered, dry ones stand clear.
func _water_height(tide: int) -> float:
	return BASE_HEIGHT + (tide - 0.5) * STEP_HEIGHT


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
