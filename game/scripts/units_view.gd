extends Node3D
## Engineers, crawlers and the crawlers' telegraphed strikes. Placeholder
## shapes until the Meshy assets land (Phase 5). Holds no rules: positions,
## HP and intents all come from the bridge.

const ENGINEER_COLORS := [Color(0.95, 0.55, 0.15), Color(0.2, 0.75, 0.75), Color(0.65, 0.4, 0.9)]
const CRAWLER_COLOR := Color(0.2, 0.45, 0.25)
const STRIKE_COLOR := Color(0.95, 0.15, 0.1)
const DIRS := [Vector2i(0, -1), Vector2i(1, 0), Vector2i(0, 1), Vector2i(-1, 0)]
const BoardView := preload("res://scripts/board_view.gd")

var _board: BoardView
var _units := {}  # id -> {"root": Node3D, "mat": StandardMaterial3D, "pips": Label3D, "engineer": bool}
var _strikes: Node3D


func build(board: BoardView) -> void:
	_board = board
	for child in get_children():
		child.queue_free()
	_units.clear()
	_strikes = Node3D.new()
	add_child(_strikes)


## Matches every unit and every strike marker to the mission as it is now.
func sync(mission: BreakwaterMission) -> void:
	for u in mission.get_units():
		var id: int = u["id"]
		if not _units.has(id):
			_create(id, u["engineer"])
		var view: Dictionary = _units[id]
		var root: Node3D = view["root"]
		root.visible = u["alive"]
		root.position = _board.top_of(Vector2i(u["x"], u["y"]))
		root.scale = Vector3.ONE
		var pips: Label3D = view["pips"]
		pips.text = "●".repeat(u["hp"])
		# An engineer that has spent its turn fades a little.
		var mat: StandardMaterial3D = view["mat"]
		mat.albedo_color.a = 0.55 if u["engineer"] and u["action"] == BreakwaterMission.ACTION_DONE else 1.0
	_sync_strikes(mission)


func root_of(id: int) -> Node3D:
	return _units[id]["root"] if _units.has(id) else null


func tween_to(id: int, to: Vector3, duration: float) -> Tween:
	var root := root_of(id)
	var tween := create_tween()
	tween.tween_property(root, "position", to, duration).set_trans(Tween.TRANS_QUAD)
	return tween


## A shove that went nowhere: lurch toward the obstacle and back.
func tween_bump(id: int, toward: Vector3) -> Tween:
	var root := root_of(id)
	var home := root.position
	var tween := create_tween()
	tween.tween_property(root, "position", home.lerp(toward, 0.3), 0.08)
	tween.tween_property(root, "position", home, 0.12)
	return tween


func tween_flash(id: int, hp: int) -> Tween:
	var view: Dictionary = _units[id]
	var mat: StandardMaterial3D = view["mat"]
	mat.emission_enabled = true
	mat.emission = Color(1, 0.15, 0.1)
	var pips: Label3D = view["pips"]
	pips.text = "●".repeat(hp)
	var tween := create_tween()
	tween.tween_property(mat, "emission_energy_multiplier", 0.0, 0.3).from(2.5)
	return tween


func tween_die(id: int) -> Tween:
	var tween := create_tween()
	tween.tween_property(root_of(id), "scale", Vector3(0.01, 0.01, 0.01), 0.3)
	return tween


func tween_sink(id: int) -> Tween:
	var root := root_of(id)
	var tween := create_tween().set_parallel()
	tween.tween_property(root, "position:y", root.position.y - 0.6, 0.45)
	tween.tween_property(root, "scale", Vector3(0.6, 0.6, 0.6), 0.45)
	return tween


func tween_spawn(id: int, engineer: bool, at: Vector3) -> Tween:
	if not _units.has(id):
		_create(id, engineer)
	var root := root_of(id)
	root.visible = true
	root.position = at - Vector3(0, 0.5, 0)
	var tween := create_tween()
	tween.tween_property(root, "position", at, 0.3).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	return tween


func _create(id: int, engineer: bool) -> void:
	var root := Node3D.new()
	root.name = ("Engineer%d" if engineer else "Crawler%d") % id
	add_child(root)

	var body := MeshInstance3D.new()
	var mat := StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	if engineer:
		var capsule := CapsuleMesh.new()
		capsule.radius = 0.22
		capsule.height = 0.8
		body.mesh = capsule
		body.position.y = 0.4
		mat.albedo_color = ENGINEER_COLORS[id % ENGINEER_COLORS.size()]
	else:
		var sphere := SphereMesh.new()
		sphere.radius = 0.32
		sphere.height = 0.36
		body.mesh = sphere
		body.position.y = 0.18
		mat.albedo_color = CRAWLER_COLOR
	body.material_override = mat
	root.add_child(body)

	var pips := Label3D.new()
	pips.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	pips.font_size = 64
	pips.outline_size = 14
	pips.modulate = Color(0.4, 1, 0.5) if engineer else Color(1, 0.5, 0.4)
	pips.position.y = 1.05 if engineer else 0.6
	root.add_child(pips)

	_units[id] = {"root": root, "mat": mat, "pips": pips, "engineer": engineer}


## One red arrow and one red tile marker per telegraphed strike.
func _sync_strikes(mission: BreakwaterMission) -> void:
	for child in _strikes.get_children():
		child.queue_free()
	for u in mission.get_units():
		if not u["alive"] or int(u["intent"]) < 0:
			continue
		var from := Vector2i(u["x"], u["y"])
		var to: Vector2i = from + DIRS[int(u["intent"])]
		# Flat on the ground, from the crawler's tile to its target, and drawn
		# over everything (see _strike_material), so no body in front can hide
		# a telegraph and the direction reads from above.
		var lift := Vector3(0, 0.08, 0)
		var a := _board.top_of(from) + lift
		var b := Vector3(_board.top_of(to).x, a.y, _board.top_of(to).z)
		_strikes.add_child(_arrow(a, b))
		_strikes.add_child(_marker(_board.top_of(to)))


## Telegraphs must never be hidden (pillar 1): no depth test, drawn last.
func _strike_material(color: Color) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.no_depth_test = true
	mat.render_priority = 10
	if color.a < 1.0:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	return mat


func _arrow(a: Vector3, b: Vector3) -> Node3D:
	var arrow := Node3D.new()
	var mat := _strike_material(STRIKE_COLOR)
	var length := a.distance_to(b) * 0.8
	var shaft := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.16, 0.03, length * 0.7)
	shaft.mesh = box
	shaft.material_override = mat
	shaft.position = Vector3(0, 0, -length * 0.35)
	arrow.add_child(shaft)
	var tip := MeshInstance3D.new()
	var cone := CylinderMesh.new()
	cone.top_radius = 0.0
	cone.bottom_radius = 0.22
	cone.height = length * 0.3
	tip.mesh = cone
	tip.material_override = mat
	tip.rotation_degrees.x = -90.0
	tip.position = Vector3(0, 0, -length * 0.85)
	arrow.add_child(tip)
	arrow.position = a + (b - a).normalized() * 0.1
	arrow.look_at_from_position(arrow.position, b, Vector3.UP)
	return arrow


func _marker(top: Vector3) -> MeshInstance3D:
	var mark := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(0.8, 0.8)
	mark.mesh = plane
	mark.material_override = _strike_material(Color(STRIKE_COLOR, 0.65))
	mark.position = top + Vector3(0, 0.02, 0)
	return mark
