extends Node3D
## Player input: picking, selection, highlights, and turning clicks into
## commands. Every command goes to the mission, which decides; a refusal is
## shown with the core's reason. Holds no rules: what is reachable or
## shovable comes from the mission's queries.
##
##   Click an engineer      select it
##   Click a green tile     move there
##   Click an orange unit   shove it away (Shift-click to shove an engineer)
##   Click a teal unit      harpoon it: pull it one tile closer (Shift-click
##                          for an engineer)
##   WASD / arrows          shove the unit north / west / south / east of the
##                          selected engineer, even one hidden behind another
##   Shift+WASD / arrows    pull the unit two tiles away in that direction
##   Space                  end the turn; during playback, skip to its end
##   Tab / Esc              next engineer / deselect
##   Z                      undo the last move (until a shove or end of turn)
##   H / Shift+H            dismiss the current hint / no more hints
##   R / N                  restart this seed / next seed
##   F5                     save the game as a replayable log

## Emitted with the command's verb after the mission accepts it.
signal command_applied(verb: String)

const BoardView := preload("res://scripts/board_view.gd")
const UnitsView := preload("res://scripts/units_view.gd")
const Hud := preload("res://scripts/hud.gd")
const DIRS := [Vector2i(0, -1), Vector2i(1, 0), Vector2i(0, 1), Vector2i(-1, 0)]
const MOVE_COLOR := Color(0.3, 0.95, 0.4, 0.45)
const SHOVE_COLOR := Color(1.0, 0.6, 0.1, 0.6)
const PULL_COLOR := Color(0.2, 0.85, 1.0, 0.6)
const SELECT_COLOR := Color(1.0, 0.95, 0.3, 0.7)
const PreviewView := preload("res://scripts/preview_view.gd")

var main: Node3D
var selected := -1
var preview: PreviewView

var _marks: Node3D


func setup(owner_scene: Node3D) -> void:
	main = owner_scene
	_marks = Node3D.new()
	add_child(_marks)
	preview = PreviewView.new()
	add_child(preview)
	preview.setup(main.board)


## Previews the shove or pull a click on `c` would make, if it would make one;
## clears the preview otherwise. Hover and the input test both come through
## here.
func hover(c: Vector2i, crawler := -2) -> void:
	# Hovering a crawler highlights its own strike. `crawler` is the first
	# crawler along the mouse ray (see _crawler_under), which finds one even
	# behind an engineer's body; -2 means "use the tile".
	var under := _unit_at(c)
	if crawler == -2:
		crawler = int(under["id"]) if not under.is_empty() and not under["engineer"] else -1
	main.units.highlight_strike(crawler)
	if not _accepting() or selected < 0 or under.is_empty() or not (_is_adjacent(c) or _is_pull_target(c)):
		preview.clear()
		return
	var mission: BreakwaterMission = main.mission
	var pulling := _is_pull_target(c)
	var dir := _pull_dir(c) if pulling else DIRS.find(c - _selected_pos())
	preview.show_preview(
		"%s:%d:%d:%s" % ["pull" if pulling else "shove", selected, dir, mission.get_state_hash_hex()],
		mission.get_units(),
		mission.preview_pull(selected, dir) if pulling else mission.preview_shove(selected, dir),
		mission.get_unit_max_hp()
	)


## The tile under a screen position, or (-1, -1): the nearest box the camera
## ray hits, a unit counting as its tile.
func pick(screen: Vector2) -> Vector2i:
	var hits := _hits(screen)
	return hits[0]["tile"] if not hits.is_empty() else Vector2i(-1, -1)


## The tile a click means. Like pick(), except that with an engineer selected
## the ray passes through bodies a click on would mean nothing: the selected
## engineer itself, and crawlers it cannot shove. From this camera an
## engineer's body covers the tile behind it, which is exactly where a step
## toward the sea goes.
func pick_for_click(screen: Vector2) -> Vector2i:
	for hit in _hits(screen):
		var c: Vector2i = hit["tile"]
		if selected >= 0 and hit["unit"]:
			var u := _unit_at(c)
			var target := _is_adjacent(c) or _is_pull_target(c)
			if c == _selected_pos() or (not u.is_empty() and not u["engineer"] and not target):
				continue
		return c
	return Vector2i(-1, -1)


## The first living crawler along the ray through `screen`, or −1, ignoring
## whatever stands in front of it.
func _crawler_under(screen: Vector2) -> int:
	for hit in _hits(screen):
		if hit["unit"]:
			var u := _unit_at(hit["tile"])
			if not u.is_empty() and not u["engineer"]:
				return int(u["id"])
	return -1


## Every box the camera ray through `screen` hits, nearest first:
## [{"t": float, "tile": Vector2i, "unit": bool}]. Tiles are boxes up to their
## top; living units are boxes standing on their tile.
func _hits(screen: Vector2) -> Array[Dictionary]:
	var camera := get_viewport().get_camera_3d()
	var origin := camera.project_ray_origin(screen)
	var dir := camera.project_ray_normal(screen)
	var mission: BreakwaterMission = main.mission
	var board: BoardView = main.board
	var hits: Array[Dictionary] = []
	for y in mission.get_size():
		for x in mission.get_size():
			var top := board.top_of(Vector2i(x, y))
			var t := _ray_box(origin, dir, AABB(Vector3(x - 0.5, 0.0, y - 0.5), Vector3(1.0, top.y, 1.0)))
			if t < INF:
				hits.append({"t": t, "tile": Vector2i(x, y), "unit": false})
	for u in mission.get_units():
		if not u["alive"]:
			continue
		var c := Vector2i(u["x"], u["y"])
		var top := board.top_of(c)
		var size: Vector2 = UnitsView.ENGINEER_SIZE if u["engineer"] else UnitsView.CRAWLER_SIZE
		var half := size.x * 0.5
		var t := _ray_box(origin, dir, AABB(top + Vector3(-half, 0.0, -half), Vector3(size.x, size.y, size.x)))
		if t < INF:
			hits.append({"t": t, "tile": c, "unit": true})
	hits.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a["t"] < b["t"])
	return hits


## What a click on tile `c` does, given the current selection. `force_shove`
## is Shift: shove or pull even an engineer instead of selecting it.
func click(c: Vector2i, force_shove := false) -> void:
	if not _accepting():
		return
	var mission: BreakwaterMission = main.mission
	var at := _unit_at(c)
	var engineer_here: bool = not at.is_empty() and at["engineer"]
	if engineer_here and not (force_shove and (_is_adjacent(c) or _is_pull_target(c))):
		_select(int(at["id"]))
		return
	if selected < 0:
		return
	if c in mission.reachable(selected):
		_apply("move", mission.move(selected, c.x, c.y))
	elif not at.is_empty() and _is_adjacent(c):
		_apply("shove", mission.shove(selected, DIRS.find(c - _selected_pos())))
	elif not at.is_empty() and _is_pull_target(c):
		_apply("pull", mission.pull(selected, _pull_dir(c)))
	else:
		main.hud.say("Engineer %d can't reach that tile." % (selected + 1))


## Shoves whatever stands next to the selected engineer in direction `dir`
## (a DIRS index, which matches the bridge's DIR_ constants).
func shove_toward(dir: int) -> void:
	if not _accepting() or selected < 0:
		return
	if _unit_at(_selected_pos() + DIRS[dir]).is_empty():
		main.hud.say("Nothing to shove there.")
		return
	_apply("shove", main.mission.shove(selected, dir))


## Pulls whatever stands two tiles from the selected engineer in direction
## `dir`, even one hidden behind another.
func pull_toward(dir: int) -> void:
	if not _accepting() or selected < 0:
		return
	if _unit_at(_selected_pos() + DIRS[dir] * 2).is_empty():
		main.hud.say("Nothing to pull there.")
		return
	_apply("pull", main.mission.pull(selected, dir))


func end_turn() -> void:
	if _accepting():
		_select(-1)
		_apply("end", main.mission.end_turn())


func cycle() -> void:
	if not _accepting():
		return
	var engineers := _actionable_engineers()
	if engineers.is_empty():
		_select(-1)
		return
	var next := engineers.find(selected) + 1
	_select(engineers[next % engineers.size()])


## Called after any change to the mission: redraws the highlights for the
## current selection, dropping it if that engineer can no longer act.
func refresh() -> void:
	if selected >= 0 and not (selected in _actionable_engineers()):
		selected = -1
	preview.clear()
	_draw_marks()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		hover(pick_for_click(event.position), _crawler_under(event.position))
	elif event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var c := pick_for_click(event.position)
		if c.x >= 0:
			click(c, event.shift_pressed)
	elif event is InputEventKey and event.pressed and not event.echo:
		match event.keycode:
			KEY_SPACE:
				if main.playing:
					main.skip()
				else:
					end_turn()
			KEY_W, KEY_UP:
				_act_toward(BreakwaterMission.DIR_NORTH, event.shift_pressed)
			KEY_D, KEY_RIGHT:
				_act_toward(BreakwaterMission.DIR_EAST, event.shift_pressed)
			KEY_S, KEY_DOWN:
				_act_toward(BreakwaterMission.DIR_SOUTH, event.shift_pressed)
			KEY_A, KEY_LEFT:
				_act_toward(BreakwaterMission.DIR_WEST, event.shift_pressed)
			KEY_TAB:
				cycle()
			KEY_ESCAPE:
				_select(-1)
			KEY_H:
				if event.shift_pressed:
					main.hints.turn_off()
				else:
					main.hints.dismiss()
			KEY_Z:
				if _accepting():
					main.undo()
			KEY_R:
				main.restart(main.mission.get_seed())
			KEY_N:
				main.next_mission()
			KEY_F5:
				main.save_log()


func _act_toward(dir: int, pull: bool) -> void:
	if pull:
		pull_toward(dir)
	else:
		shove_toward(dir)


func _accepting() -> bool:
	return not main.playing and main.mission.get_outcome() == BreakwaterMission.OUTCOME_IN_PROGRESS


func _apply(verb: String, err: String) -> void:
	preview.clear()
	if not err.is_empty():
		main.hud.say(err[0].to_upper() + err.substr(1) + ".")
		return
	main.hud.say("")
	main.note_command(verb)
	command_applied.emit(verb)
	await main.play_events()
	refresh()


func _select(id: int) -> void:
	selected = id if id in _actionable_engineers() else -1
	preview.clear()
	_draw_marks()
	main.update_hints()


func _actionable_engineers() -> Array[int]:
	var out: Array[int] = []
	for u in main.mission.get_units():
		if u["engineer"] and u["alive"] and int(u["action"]) != BreakwaterMission.ACTION_DONE:
			out.append(int(u["id"]))
	return out


func _unit_at(c: Vector2i) -> Dictionary:
	for u in main.mission.get_units():
		if u["alive"] and int(u["x"]) == c.x and int(u["y"]) == c.y:
			return u
	return {}


func _selected_pos() -> Vector2i:
	var u: Dictionary = main.mission.get_units()[selected]
	return Vector2i(u["x"], u["y"])


func _is_adjacent(c: Vector2i) -> bool:
	return selected >= 0 and (c - _selected_pos()) in DIRS


## A unit two tiles from the selected engineer in a straight line: what its
## harpoon reaches.
func _is_pull_target(c: Vector2i) -> bool:
	return selected >= 0 and _pull_dir(c) >= 0 and not _unit_at(c).is_empty()


## The direction from the selected engineer to `c` when `c` is two tiles away
## in a line, or −1.
func _pull_dir(c: Vector2i) -> int:
	var delta := c - _selected_pos()
	for i in DIRS.size():
		if delta == DIRS[i] * 2:
			return i
	return -1


func _draw_marks() -> void:
	for child in _marks.get_children():
		child.queue_free()
	if selected < 0:
		return
	var mission: BreakwaterMission = main.mission
	var board: BoardView = main.board
	var here := _selected_pos()
	_marks.add_child(_quad(board.top_of(here), SELECT_COLOR, 0.95))
	for c in mission.reachable(selected):
		_marks.add_child(_quad(board.top_of(c), MOVE_COLOR, 0.7))
	for d in DIRS:
		var target: Vector2i = here + d
		if not _unit_at(target).is_empty():
			_marks.add_child(_quad(board.top_of(target), SHOVE_COLOR, 0.85))
		var far: Vector2i = here + d * 2
		if not _unit_at(far).is_empty():
			_marks.add_child(_quad(board.top_of(far), PULL_COLOR, 0.85))


func _quad(top: Vector3, color: Color, size: float) -> MeshInstance3D:
	var mark := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(size, size)
	mark.mesh = plane
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mark.material_override = mat
	mark.position = top + Vector3(0, 0.03, 0)
	return mark


## Distance along the ray to the box, or INF on a miss (slab method).
static func _ray_box(origin: Vector3, dir: Vector3, box: AABB) -> float:
	var t_min := -INF
	var t_max := INF
	for axis in 3:
		var lo := box.position[axis]
		var hi := box.end[axis]
		if absf(dir[axis]) < 1e-9:
			if origin[axis] < lo or origin[axis] > hi:
				return INF
			continue
		var t1 := (lo - origin[axis]) / dir[axis]
		var t2 := (hi - origin[axis]) / dir[axis]
		t_min = maxf(t_min, minf(t1, t2))
		t_max = minf(t_max, maxf(t1, t2))
	if t_max < maxf(t_min, 0.0):
		return INF
	return maxf(t_min, 0.0)
