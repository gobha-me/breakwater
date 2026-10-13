extends SceneTree
## Headless input test. Loads the main scene and:
##   1. checks picking against the camera's own projection, for every tile
##      and every unit;
##   2. plays whole missions with synthetic mouse clicks and key presses,
##      through the same _unhandled_input path a player uses, then saves each
##      game with F5 so tools/godot_smoke.sh can replay it with bw_replay.
##
##   3. plays given logs (e.g. a solver witness) by input events only: click the
##      engineer, click the tile or press WASD to shove, Space to end the turn.
##
##   godot --headless --path game --script res://tests/input_test.gd -- \
##       --save-dir=<dir> --seeds=19,20 --play-logs=/abs/win.log

const DIRS := [Vector2i(0, -1), Vector2i(1, 0), Vector2i(0, 1), Vector2i(-1, 0)]

var _failures := 0
var _main: Node3D


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var args := {}
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--") and "=" in arg:
			args[arg.substr(2, arg.find("=") - 2)] = arg.substr(arg.find("=") + 1)
	# Headless windows default to 64x64, which garbles injected mouse
	# positions; use the game's real window size.
	root.size = Vector2i(1280, 720)
	_main = load("res://scenes/main.tscn").instantiate()
	root.add_child(_main)
	_main.save_dir = args.get("save-dir", "user://test-logs")
	await _frames(3)

	_check_picking()
	await _check_preview()
	await _check_undo()
	await _check_strike_hover()
	_main.restart(78)
	_check(_main.mission.get_seed() == 79, "a rejected seed is never played: 78 starts mission 79")
	await _key(KEY_N)
	_check(_main.mission.get_seed() == 80, "N moves to the next offered seed")
	for s in str(args.get("seeds", "20")).split(","):
		await _play_by_clicks(int(s))
	for path in str(args.get("play-logs", "")).split(",", false):
		await _play_log_by_input(path)
	print("input: %s (%d failure(s))" % ["PASS" if _failures == 0 else "FAIL", _failures])
	quit(1 if _failures > 0 else 0)


func _check_picking() -> void:
	var camera := root.get_viewport().get_camera_3d()
	var mission: BreakwaterMission = _main.mission
	var occupied := {}
	for u in mission.get_units():
		if u["alive"]:
			occupied[Vector2i(u["x"], u["y"])] = true
	var exact := 0
	var tiles := 0
	for y in mission.get_size():
		for x in mission.get_size():
			var c := Vector2i(x, y)
			tiles += 1
			var picked: Vector2i = _main.input.pick(camera.unproject_position(_main.board.top_of(c)))
			# A tall unit standing in front may legitimately cover a tile.
			if picked == c:
				exact += 1
			elif not occupied.has(picked):
				_check(false, "tile %s picked as %s" % [c, picked])
	_check(exact >= tiles - occupied.size() * 2, "picking: %d of %d tiles exact, rest covered by units" % [exact, tiles])
	for c in occupied:
		var body: Vector3 = _main.board.top_of(c) + Vector3(0, 0.3, 0)
		var picked: Vector2i = _main.input.pick(camera.unproject_position(body))
		# A unit standing in front, nearer the camera, may cover one behind it;
		# WASD shoves reach the covered one.
		var covered: bool = picked != c and occupied.has(picked) and picked.y > c.y
		_check(picked == c or covered, "unit at %s picked by its body%s" % [c, " (covered by %s)" % picked if covered else ""])


## Hovering a shove target with an engineer selected shows the core's
## predicted outcome, and moving away clears it.
func _check_preview() -> void:
	_main.restart(20)
	await _frames(2)
	var mission: BreakwaterMission = _main.mission
	var units: Array = mission.get_units()
	var e0 := Vector2i(units[0]["x"], units[0]["y"])
	var target := Vector2i(units[2]["x"], units[2]["y"])  # Engineer 2 stands east of engineer 0 at seed 20.
	_check(target - e0 == Vector2i(1, 0), "seed 20 opens with a shove target east of engineer 0")
	await _click(e0)
	var before := mission.get_state_hash()
	await _move_mouse(target)
	var expected: Dictionary = mission.preview_shove(0, BreakwaterMission.DIR_EAST)
	_check(not _main.input.preview.shown.is_empty() and _main.input.preview.get_child_count() > 0,
			"hovering a shove target shows a preview")
	_check(expected["error"].is_empty() and not expected["events"].is_empty(), "the bridge's preview has the shove's events")
	_check(mission.get_state_hash() == before and mission.take_events().is_empty(), "previewing changes nothing")
	await _move_mouse(Vector2i(7, 7))
	_check(_main.input.preview.shown.is_empty(), "moving off the target clears the preview")
	await _key(KEY_ESCAPE)


## Z takes back a move made by clicking, restoring the exact state.
func _check_undo() -> void:
	_main.restart(20)
	await _frames(2)
	var mission: BreakwaterMission = _main.mission
	var before := mission.get_state_hash()
	var u: Dictionary = mission.get_units()[1]
	await _click(Vector2i(u["x"], u["y"]))
	var to: Vector2i = mission.reachable(1)[0]
	await _click(to)
	_check(mission.get_state_hash() != before, "a clicked move changed the state")
	await _key(KEY_Z)
	_check(mission.get_state_hash() == before, "Z restores the exact state before the move")
	await _key(KEY_Z)
	_check(mission.get_state_hash() == before, "Z with nothing to undo changes nothing")


## Hovering a crawler highlights its own strike; moving off clears it.
func _check_strike_hover() -> void:
	_main.restart(20)
	await _frames(2)
	var crawler := -1
	var at := Vector2i(-1, -1)
	for u in _main.mission.get_units():
		if not u["engineer"] and int(u["intent"]) >= 0:
			crawler = int(u["id"])
			at = Vector2i(u["x"], u["y"])
	_check(crawler >= 0, "seed 20 opens with a telegraphing crawler")
	await _move_mouse(at)
	_check(_main.units.highlighted_strike() == crawler, "hovering a crawler highlights its strike")
	await _move_mouse(Vector2i(7, 7))
	_check(_main.units.highlighted_strike() == -1, "moving off clears the highlight")


func _move_mouse(c: Vector2i) -> void:
	var camera := root.get_viewport().get_camera_3d()
	var event := InputEventMouseMotion.new()
	event.position = camera.unproject_position(_main.board.top_of(c) + Vector3(0, 0.3, 0))
	event.global_position = event.position
	Input.parse_input_event(event)
	await _frames(2)


## A simple scripted player that only acts through input events: each turn,
## every engineer that can shove a crawler off toward the sea does so;
## otherwise it walks to its most inland reachable tile. Then Space.
func _play_by_clicks(seed: int) -> void:
	_main.restart(seed)
	await _frames(2)
	var mission: BreakwaterMission = _main.mission
	var clicks := 0
	var shoves := 0
	var guard := 0
	while mission.get_outcome() == BreakwaterMission.OUTCOME_IN_PROGRESS and guard < 200:
		guard += 1
		for u in mission.get_units():
			if not u["engineer"] or not u["alive"]:
				continue
			var id: int = u["id"]
			await _click(Vector2i(u["x"], u["y"]))
			clicks += 1
			if _main.input.selected != id:
				continue
			var crawler := _shovable_crawler(mission, id)
			if crawler.x < 0:
				var tiles: Array = mission.reachable(id)
				if not tiles.is_empty():
					tiles.sort_custom(func(a: Vector2i, b: Vector2i) -> bool: return a.y > b.y or (a.y == b.y and a.x < b.x))
					await _click(tiles[0])
					clicks += 1
					crawler = _shovable_crawler(mission, id)
			if crawler.x >= 0 and mission.get_outcome() == BreakwaterMission.OUTCOME_IN_PROGRESS:
				# Alternate the two ways to shove: click the unit, or press W.
				if shoves % 2 == 0:
					await _click(crawler)
				else:
					await _key(KEY_W)
				clicks += 1
				shoves += 1
		if mission.get_outcome() == BreakwaterMission.OUTCOME_IN_PROGRESS:
			await _key(KEY_SPACE)
	_check(mission.get_outcome() != BreakwaterMission.OUTCOME_IN_PROGRESS, "seed %d played to an ending by input alone" % seed)
	var outcome: String = "won" if mission.get_outcome() == BreakwaterMission.OUTCOME_WON else "lost"
	var saved: String = _main.save_log()
	_check(not saved.is_empty(), "seed %d saved with F5's handler" % seed)
	print("played: seed %d, %s, %d clicks, %d shoves, hash %s, log %s" % [
			seed, outcome, clicks, shoves, mission.get_state_hash_hex(), saved])


## Enters every command of a log as mouse clicks and key presses, then saves
## the game. The log's commands are known legal, so any refusal is an input bug.
func _play_log_by_input(path: String) -> void:
	var parsed := BreakwaterMission.parse_log(FileAccess.get_file_as_string(path))
	_check(parsed["error"].is_empty(), "%s parses" % path)
	_main.restart(parsed["seed"])
	await _frames(2)
	var mission: BreakwaterMission = _main.mission
	var keys := [KEY_W, KEY_D, KEY_S, KEY_A]
	var entered := 0
	for c in parsed["commands"]:
		var before := mission.get_log()
		match c["verb"]:
			"move", "shove":
				var e: Dictionary = mission.get_units()[c["engineer"]]
				var at := Vector2i(e["x"], e["y"])
				if _main.input.selected != int(c["engineer"]):
					await _click(at)
				if c["verb"] == "move":
					await _click(Vector2i(c["x"], c["y"]))
				else:
					await _key(keys[int(c["dir"])])
			_:
				await _key(KEY_SPACE)
		if mission.get_log() == before:
			_check(false, "%s: command %d was not entered by input (%s)" % [path, entered + 1, c])
			return
		entered += 1
	var outcome: String = {BreakwaterMission.OUTCOME_WON: "won", BreakwaterMission.OUTCOME_LOST: "lost"}.get(
			mission.get_outcome(), "in-progress")
	_check(entered == parsed["commands"].size(), "%s: all %d commands entered by input" % [path, entered])
	var saved: String = _main.save_log()
	print("played: seed %d, %s, %d commands by input, hash %s, log %s" % [
			mission.get_seed(), outcome, entered, mission.get_state_hash_hex(), saved])


## A crawler next to engineer `id` that a shove would push north, toward the
## sea, or (-1, -1).
func _shovable_crawler(mission: BreakwaterMission, id: int) -> Vector2i:
	var e: Dictionary = mission.get_units()[id]
	var at := Vector2i(e["x"], e["y"]) + Vector2i(0, -1)
	for u in mission.get_units():
		if u["alive"] and not u["engineer"] and Vector2i(u["x"], u["y"]) == at:
			return at
	return Vector2i(-1, -1)


func _click(c: Vector2i) -> void:
	var camera := root.get_viewport().get_camera_3d()
	var height := 0.3 if _has_unit(c) else 0.0
	var screen := camera.unproject_position(_main.board.top_of(c) + Vector3(0, height, 0))
	for pressed in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = pressed
		event.position = screen
		event.global_position = screen
		Input.parse_input_event(event)
		await _frames(1)
	await _settle()


func _key(keycode: Key) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = keycode
		event.pressed = pressed
		Input.parse_input_event(event)
		await _frames(1)
	await _settle()


func _has_unit(c: Vector2i) -> bool:
	for u in _main.mission.get_units():
		if u["alive"] and Vector2i(u["x"], u["y"]) == c:
			return true
	return false


func _settle() -> void:
	while _main.playing:
		await process_frame


func _frames(n: int) -> void:
	for i in n:
		await process_frame


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok   ", label)
	else:
		print("  FAIL ", label)
		_failures += 1
