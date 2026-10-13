extends Node3D
## Engineers, crawlers and the crawlers' telegraphed strikes. Placeholder
## shapes until the Meshy assets land (Phase 5). Holds no rules: positions,
## HP and intents all come from the bridge.

const ENGINEER_COLORS := [Color(0.95, 0.55, 0.15), Color(0.2, 0.75, 0.75), Color(0.65, 0.4, 0.9)]
const CRAWLER_COLOR := Color(0.2, 0.45, 0.25)
const CRAWLER_RING := Color(0.9, 0.15, 0.1)
## The model's resting overlay below full HP: a dark wash that reads as hurt.
const HURT := Color(0.1, 0.02, 0.02, 0.45)
const CLEAR := Color(1, 0.15, 0.1, 0.0)
const FLASH := Color(1, 0.15, 0.1, 0.75)
const STRIKE_COLOR := Color(0.95, 0.15, 0.1)
const DIRS := [Vector2i(0, -1), Vector2i(1, 0), Vector2i(0, 1), Vector2i(-1, 0)]
const BoardView := preload("res://scripts/board_view.gd")

var _board: BoardView
var _units := {}  # id -> {"root": Node3D, "ring": HealthRing, "flash": StandardMaterial3D, "engineer": bool}
var _max_hp := 2  # Refreshed from the mission on every sync.
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
	_max_hp = mission.get_unit_max_hp()
	for u in mission.get_units():
		var id: int = u["id"]
		if not _units.has(id):
			_create(id, u["engineer"])
		var view: Dictionary = _units[id]
		var root: Node3D = view["root"]
		root.visible = u["alive"]
		root.position = _board.top_of(Vector2i(u["x"], u["y"]))
		root.scale = Vector3.ONE
		var ring: HealthRing = view["ring"]
		ring.show_hp(u["hp"])
		var flash: StandardMaterial3D = view["flash"]
		flash.albedo_color = HURT if u["alive"] and int(u["hp"]) < _max_hp else CLEAR
		if u["engineer"]:
			# A spent engineer's ring dims.
			var base: Color = ENGINEER_COLORS[id % ENGINEER_COLORS.size()]
			var done: bool = u["action"] == BreakwaterMission.ACTION_DONE
			ring.set_color(base.darkened(0.55) if done else base, 0.0 if done else 0.5)
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
	var flash: StandardMaterial3D = view["flash"]
	var ring: HealthRing = view["ring"]
	ring.show_hp(hp)
	var tween := create_tween()
	tween.tween_property(flash, "albedo_color", HURT if hp > 0 and hp < _max_hp else CLEAR, 0.3).from(FLASH)
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


const ENGINEER_MODEL := "res://assets/models/engineer.glb"
const CRAWLER_MODEL := "res://assets/models/crawler.glb"
## Sizes the bodies are fitted to, in tiles. play_input.gd's pick boxes use
## the same heights.
const ENGINEER_SIZE := Vector2(0.7, 1.0)  # Footprint, height.
const CRAWLER_SIZE := Vector2(0.95, 0.5)


func _create(id: int, engineer: bool) -> void:
	var root := Node3D.new()
	root.name = ("Engineer%d" if engineer else "Crawler%d") % id
	add_child(root)

	var size := ENGINEER_SIZE if engineer else CRAWLER_SIZE
	var body := ModelFit.instance(ENGINEER_MODEL if engineer else CRAWLER_MODEL, size.x, size.y)
	if body == null:
		body = _placeholder(engineer)
	root.add_child(body)

	# Health lives on the ground: a ring of one arc per hit point. Engineers'
	# rings are their team colour, which also tells the three apart;
	# crawlers' are red.
	var ring := HealthRing.new()
	if engineer:
		ring.setup(_max_hp, 0.3, 0.42, ENGINEER_COLORS[id % ENGINEER_COLORS.size()])
	else:
		ring.setup(_max_hp, 0.33, 0.46, CRAWLER_RING)
	root.add_child(ring)

	# Damage flashes, and a hurt unit stays darkened, through one overlay
	# material, which works on textured models too.
	var flash := StandardMaterial3D.new()
	flash.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	flash.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	flash.albedo_color = CLEAR
	_set_overlay(body, flash)

	_units[id] = {"root": root, "ring": ring, "flash": flash, "engineer": engineer, "id": id}


func _placeholder(engineer: bool) -> Node3D:
	var body := MeshInstance3D.new()
	var mat := StandardMaterial3D.new()
	if engineer:
		var capsule := CapsuleMesh.new()
		capsule.radius = 0.22
		capsule.height = 0.8
		body.mesh = capsule
		body.position.y = 0.4
		mat.albedo_color = Color(0.85, 0.85, 0.85)
	else:
		var sphere := SphereMesh.new()
		sphere.radius = 0.32
		sphere.height = 0.36
		body.mesh = sphere
		body.position.y = 0.18
		mat.albedo_color = CRAWLER_COLOR
	body.material_override = mat
	var holder := Node3D.new()
	holder.add_child(body)
	return holder


static func _set_overlay(node: Node, overlay: Material) -> void:
	if node is GeometryInstance3D:
		node.material_overlay = overlay
	for child in node.get_children():
		_set_overlay(child, overlay)


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


## The struck tile: a hollow red frame at the tile's edge, drawn over
## everything like the arrow, so the target inside (its health ring, its hurt
## tint) still shows through.
func _marker(top: Vector3) -> Node3D:
	var frame := Node3D.new()
	frame.position = top + Vector3(0, 0.04, 0)
	var mat := _strike_material(STRIKE_COLOR)
	var edge := 0.44
	var width := 0.07
	for side in [Vector3(0, 0, -edge), Vector3(0, 0, edge), Vector3(-edge, 0, 0), Vector3(edge, 0, 0)]:
		var bar := MeshInstance3D.new()
		var box := BoxMesh.new()
		var along_x: bool = side.x == 0.0
		box.size = Vector3(2.0 * edge + width, 0.02, width) if along_x else Vector3(width, 0.02, 2.0 * edge + width)
		bar.mesh = box
		bar.material_override = mat
		bar.position = side
		frame.add_child(bar)
	return frame
