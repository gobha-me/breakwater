extends Node3D
## The predicted outcome of a hovered shove, drawn over the board. Everything
## shown comes from BreakwaterMission.preview_shove(): the core applies the
## shove to a copy, so this is the outcome, not an estimate.

const BoardView := preload("res://scripts/board_view.gd")
const DIRS := [Vector2i(0, -1), Vector2i(1, 0), Vector2i(0, 1), Vector2i(-1, 0)]
const GHOST := Color(1.0, 1.0, 1.0, 0.85)
const HARM := Color(1.0, 0.25, 0.2)
const REDIRECT := Color(1.0, 0.6, 0.1)

## The shown preview's key, so hovering the same target doesn't redraw.
var shown := ""

var _board: BoardView


func setup(board: BoardView) -> void:
	_board = board


func clear() -> void:
	shown = ""
	for child in get_children():
		child.queue_free()


## Draws `preview` (from preview_shove) against the units as they are now.
func show_preview(key: String, now: Array, preview: Dictionary, unit_max_hp: int) -> void:
	if key == shown:
		return
	clear()
	shown = key
	if not preview["error"].is_empty():
		return
	var after: Array = preview["units"]
	for i in after.size():
		var a: Dictionary = after[i]
		var b: Dictionary = now[i] if i < now.size() else a
		var from := Vector2i(b["x"], b["y"])
		var to := Vector2i(a["x"], a["y"])
		if to != from:
			add_child(_path(from, to))
		var lost := int(b["hp"]) - int(a["hp"])
		if not a["alive"] and b["alive"]:  # Only what this action kills.
			var drowned := false
			for e in preview["events"]:
				drowned = drowned or (int(e["kind"]) == BreakwaterMission.EVENT_UNIT_DROWNED and int(e["unit"]) == i)
			add_child(_label(_board.top_of(to), "drowns" if drowned else "✕", HARM))
		elif a["alive"] and (lost > 0 or to != from):
			var ring := HealthRing.new()
			ring.setup(unit_max_hp, 0.33, 0.46, HARM if lost > 0 else GHOST)
			ring.show_hp(int(a["hp"]))
			ring.position = _board.top_of(to) + Vector3(0, 0.02, 0)
			add_child(ring)
			if lost > 0:
				add_child(_label(_board.top_of(to), "−%d" % lost, HARM))
		# A crawler that ends up elsewhere strikes elsewhere: show where.
		if a["alive"] and not a["engineer"] and int(a["intent"]) >= 0 and to != from:
			add_child(_arrow(to, to + DIRS[int(a["intent"])]))
	for e in preview["events"]:
		if int(e["kind"]) == BreakwaterMission.EVENT_BUILDING_DAMAGED:
			add_child(_label(_board.top_of(e["to"]), "−1" if int(e["value"]) > 0 else "✕", HARM))


func _material(color: Color) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.no_depth_test = true
	mat.render_priority = 11
	if color.a < 1.0:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	return mat


## A thin line from where a unit stands to where the shove leaves it.
func _path(from: Vector2i, to: Vector2i) -> MeshInstance3D:
	var a := _board.top_of(from) + Vector3(0, 0.1, 0)
	var b := _board.top_of(to) + Vector3(0, 0.1, 0)
	var bar := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.08, 0.02, a.distance_to(b))
	bar.mesh = box
	bar.material_override = _material(GHOST)
	bar.position = (a + b) * 0.5
	if not a.is_equal_approx(b):
		bar.look_at_from_position(bar.position, b, Vector3.UP)
	return bar


## The redirected strike: a short orange arrow and a hollow frame.
func _arrow(from: Vector2i, to: Vector2i) -> Node3D:
	var node := Node3D.new()
	var a := _board.top_of(from) + Vector3(0, 0.12, 0)
	var b := Vector3(_board.top_of(to).x, a.y, _board.top_of(to).z)
	var mat := _material(REDIRECT)
	var shaft := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.12, 0.03, 0.55)
	shaft.mesh = box
	shaft.material_override = mat
	shaft.position = a.lerp(b, 0.45)
	shaft.look_at_from_position(shaft.position, b, Vector3.UP)
	node.add_child(shaft)
	var top := _board.top_of(to) + Vector3(0, 0.05, 0)
	for side in [Vector3(0, 0, -0.42), Vector3(0, 0, 0.42), Vector3(-0.42, 0, 0), Vector3(0.42, 0, 0)]:
		var bar := MeshInstance3D.new()
		var edge := BoxMesh.new()
		edge.size = Vector3(0.9, 0.02, 0.05) if side.x == 0.0 else Vector3(0.05, 0.02, 0.9)
		bar.mesh = edge
		bar.material_override = mat
		bar.position = top + side
		node.add_child(bar)
	return node


func _label(top: Vector3, text: String, color: Color) -> Label3D:
	var label := Label3D.new()
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.no_depth_test = true
	label.render_priority = 12
	label.font_size = 72
	label.outline_size = 18
	label.modulate = color
	label.text = text
	label.position = top + Vector3(0.32, 0.55, 0.0)  # To the side, off the model.
	return label
