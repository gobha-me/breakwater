extends Node3D
## Breakwater's main scene. The mission and every rule live in the extension;
## this script plays the mission's events back as animation and keeps the
## views in sync with the mission's queries.
##
## User arguments (after `--`):
##   --seed=<n>          start a fresh mission from this seed
##   --log=<path>        start from the position a log reaches
##   --autoplay=<path>   play a log's commands one by one through the scene,
##                       print the outcome and hash, and quit
##   --speed=<x>         playback speed multiplier (autoplay defaults to 8)
##   --save-dir=<path>   where F5 saves logs (default user://logs)

signal playback_finished

const BoardView := preload("res://scripts/board_view.gd")
const UnitsView := preload("res://scripts/units_view.gd")
const Hud := preload("res://scripts/hud.gd")
const PlayInput := preload("res://scripts/play_input.gd")

@export var mission_seed := 20

var mission := BreakwaterMission.new()
var board: BoardView
var units: UnitsView
var hud: Hud
var input: PlayInput
var playing := false
var save_dir := "user://logs"


func _ready() -> void:
	board = BoardView.new()
	units = UnitsView.new()
	hud = Hud.new()
	add_child(board)
	add_child(units)
	add_child(hud)
	_build_environment()
	input = PlayInput.new()
	add_child(input)
	input.setup(self)

	var args := _user_args()
	save_dir = args.get("save-dir", save_dir)
	Engine.time_scale = float(args.get("speed", "8" if args.has("autoplay") else "1"))
	if args.has("log"):
		var err := mission.load_log(FileAccess.get_file_as_string(args["log"]))
		if not err.is_empty():
			push_error("log: " + err)
	else:
		mission.generate(int(args.get("seed", str(mission_seed))))
	_rebuild()
	if args.has("autoplay"):
		# Real-time watchdog: a script error mid-autoplay must fail the run,
		# not leave a headless process waiting forever.
		get_tree().create_timer(120.0, true, false, true).timeout.connect(func() -> void:
			printerr("autoplay: timed out")
			get_tree().quit(3))
		_autoplay.call_deferred(args["autoplay"])


func _rebuild() -> void:
	board.build(mission)
	units.build(board)
	_sync()
	input.selected = -1
	input.refresh()


## A fresh mission from `seed`, discarding the current one.
func restart(seed: int) -> void:
	if playing:
		return
	mission = BreakwaterMission.new()
	mission.generate(seed)
	_rebuild()
	hud.say("Seed %d." % seed)


## Writes every accepted command so far as a log bw_replay can replay, and
## returns its absolute path ("" on failure).
func save_log() -> String:
	DirAccess.make_dir_recursive_absolute(save_dir)
	var path := save_dir.path_join("breakwater-seed%d-turn%d-%d.log" % [
			mission.get_seed(), mission.get_turn(), Time.get_unix_time_from_system()])
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		hud.say("Could not save %s." % path)
		return ""
	file.store_string(mission.get_log())
	file.close()
	var absolute := ProjectSettings.globalize_path(path)
	hud.say("Saved %s" % absolute)
	print("saved: ", absolute)
	return absolute


## Everything on screen, from queries: the source of truth after any animation.
func _sync() -> void:
	board.sync()
	units.sync(mission)
	hud.sync(mission)


## Plays back everything the mission reported since the last call, in order,
## then re-syncs. Input should wait for playback_finished.
func play_events() -> void:
	playing = true
	for e in mission.take_events():
		await _play(e)
	_sync()
	playing = false
	playback_finished.emit()


func _play(e: Dictionary) -> void:
	var id: int = e["unit"]
	var to := board.top_of(e["to"])
	var tween: Tween = null
	match int(e["kind"]):
		BreakwaterMission.EVENT_UNIT_MOVED:
			tween = units.tween_to(id, to, 0.25)
		BreakwaterMission.EVENT_UNIT_SHOVED:
			tween = units.tween_to(id, to, 0.15) if e["from"] != e["to"] else null
		BreakwaterMission.EVENT_UNIT_DAMAGED:
			tween = units.tween_flash(id, e["value"])
		BreakwaterMission.EVENT_UNIT_DIED:
			tween = units.tween_die(id)
		BreakwaterMission.EVENT_UNIT_DROWNED:
			tween = units.tween_sink(id)
		BreakwaterMission.EVENT_BUILDING_DAMAGED:
			tween = board.flash_building(e["to"], e["value"])
		BreakwaterMission.EVENT_ATTACK_RESOLVED:
			tween = units.tween_bump(id, to)
		BreakwaterMission.EVENT_TIDE_ROSE:
			tween = board.tween_tide(e["value"], 0.5)
		BreakwaterMission.EVENT_UNIT_SPAWNED:
			tween = units.tween_spawn(id, false, to)
		BreakwaterMission.EVENT_TURN_STARTED, BreakwaterMission.EVENT_MISSION_ENDED:
			hud.sync(mission)
		BreakwaterMission.EVENT_INTENT_DECLARED:
			pass  # Strikes are drawn by the sync after playback.
	if tween != null:
		await tween.finished


## Runs a log through the scene exactly as a player's commands would go, so
## the presentation is exercised headless. Prints the result and quits.
func _autoplay(path: String) -> void:
	var parsed := BreakwaterMission.parse_log(FileAccess.get_file_as_string(path))
	if not parsed["error"].is_empty():
		printerr("autoplay: %s: %s" % [path, parsed["error"]])
		get_tree().quit(2)
		return
	mission.generate(parsed["seed"])
	_rebuild()
	var index := 0
	for c in parsed["commands"]:
		index += 1
		var err: String
		match c["verb"]:
			"move":
				err = mission.move(c["engineer"], c["x"], c["y"])
			"shove":
				err = mission.shove(c["engineer"], c["dir"])
			_:
				err = mission.end_turn()
		if not err.is_empty():
			printerr("autoplay: command %d refused: %s" % [index, err])
			get_tree().quit(1)
			return
		await play_events()
	var outcome: String = {BreakwaterMission.OUTCOME_WON: "won", BreakwaterMission.OUTCOME_LOST: "lost"}.get(
			mission.get_outcome(), "in-progress")
	print("autoplay: %d commands, outcome %s, hash %s" % [index, outcome, mission.get_state_hash_hex()])
	get_tree().quit(0)


func _build_environment() -> void:
	var center := Vector3(3.5, 0.0, 3.5)
	var camera := Camera3D.new()
	camera.position = center + Vector3(0.0, 9.0, 8.5)
	add_child(camera)
	camera.look_at(center + Vector3(0.0, 0.0, 0.5))

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


static func _user_args() -> Dictionary:
	var args := {}
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--") and "=" in arg:
			var key := arg.substr(2, arg.find("=") - 2)
			args[key] = arg.substr(arg.find("=") + 1)
	return args
