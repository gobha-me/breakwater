extends Node3D
## Breakwater's main scene. The mission and every rule live in the extension;
## this script plays the mission's events back as animation and keeps the
## views in sync with the mission's queries.
##
## User arguments (after `--`):
##   --seed=<n>          start a fresh mission from this seed
##   --log=<path>        start from the position a log reaches
##   --after-turn=<n>    with --log: stop after the log's n-th end turn
##                       (0 = the opening position)
##   --autoplay=<path>   play a log's commands one by one through the scene,
##                       print the outcome and hash, and quit
##   --speed=<x>         playback speed multiplier (autoplay defaults to 8)
##   --pace=<s>          autoplay: show each command's selection and shove
##                       preview for <s> seconds first, as a player would see it
##   --hold=<s>          autoplay: linger on the opening and the ending
##   --watchdog=<s>      autoplay: real-time limit (default 120)
##   --save-dir=<path>   where F5 saves logs (default user://logs)
##   --select=<id> --hover=<x>,<y>
##                       select an engineer and hover a tile, as a player
##                       would, for captures of selection and shove previews

signal playback_finished

const BoardView := preload("res://scripts/board_view.gd")
const UnitsView := preload("res://scripts/units_view.gd")
const Hud := preload("res://scripts/hud.gd")
const PlayInput := preload("res://scripts/play_input.gd")
const Hints := preload("res://scripts/hints.gd")
const Audio := preload("res://scripts/audio.gd")
## Events only an enemy phase produces; a playback containing any is one.
const ENEMY_PHASE_EVENTS := [
	BreakwaterMission.EVENT_ATTACK_RESOLVED,
	BreakwaterMission.EVENT_TIDE_ROSE,
	BreakwaterMission.EVENT_UNIT_SPAWNED,
	BreakwaterMission.EVENT_TURN_STARTED
]
## Events a crawler performs, for the acting-crawler marker.
const ACTOR_EVENTS := [
	BreakwaterMission.EVENT_ATTACK_RESOLVED,
	BreakwaterMission.EVENT_INTENT_DECLARED,
	BreakwaterMission.EVENT_UNIT_SPAWNED,
	BreakwaterMission.EVENT_UNIT_MOVED
]
const STEP_NAMES := {
	BreakwaterMission.EVENT_ATTACK_RESOLVED: "Crawlers strike",
	BreakwaterMission.EVENT_TIDE_ROSE: "The tide rises",
	BreakwaterMission.EVENT_UNIT_SPAWNED: "Crawlers come ashore",
	BreakwaterMission.EVENT_UNIT_MOVED: "Crawlers move and take aim",
	BreakwaterMission.EVENT_INTENT_DECLARED: "Crawlers move and take aim",
}

@export var mission_seed := 20

var mission := BreakwaterMission.new()
var board: BoardView
var units: UnitsView
var hud: Hud
var input: PlayInput
var hints: Hints
var audio: Audio
var playing := false
var save_dir := "user://logs"

## What the player has done this mission, for the hints.
var _acted := false
var _shoved := false
var _ended := false
var _base_speed := 1.0
var _skipping := false
## The command whose events are playing, so a pull sounds like a harpoon.
var _verb := ""


func _ready() -> void:
	board = BoardView.new()
	units = UnitsView.new()
	hud = Hud.new()
	add_child(board)
	add_child(units)
	add_child(hud)
	_build_environment()
	get_tree().auto_accept_quit = false  # See _notification.
	input = PlayInput.new()
	add_child(input)
	input.setup(self)

	var args := _user_args()
	# Hints are for people: scripted runs never show them, unless forced.
	var scripted := args.has("log") or args.has("autoplay") or args.has("capture") or args.has("no-hints")
	hints = Hints.new()
	add_child(hints)
	hints.setup(args.has("hints") or not scripted, not args.has("hints") and not scripted)
	audio = Audio.new()
	add_child(audio)
	audio.setup(not scripted)
	save_dir = args.get("save-dir", save_dir)
	_base_speed = float(args.get("speed", "8" if args.has("autoplay") else "1"))
	Engine.time_scale = _base_speed
	if args.has("log"):
		_load_until(args["log"], int(args.get("after-turn", "-1")))
	else:
		restart(int(args.get("seed", str(mission_seed))))
	if args.has("select"):
		input.click.call_deferred(_engineer_tile(int(args["select"])))
	if args.has("hover"):
		var xy: PackedStringArray = str(args["hover"]).split(",")
		input.hover.call_deferred(Vector2i(int(xy[0]), int(xy[1])))
	if args.has("autoplay"):
		# Real-time watchdog: a script error mid-autoplay must fail the run,
		# not leave a headless process waiting forever.
		get_tree().create_timer(float(args.get("watchdog", "120")), true, false, true).timeout.connect(
			func() -> void:
				printerr("autoplay: timed out")
				Audio.quit(get_tree(), 3)
		)
		_autoplay.call_deferred(args["autoplay"], float(args.get("pace", "0")), float(args.get("hold", "0")))


## Closing the window quits through Audio.quit, like every other exit.
func _notification(what: int) -> void:
	if what == NOTIFICATION_WM_CLOSE_REQUEST:
		Audio.quit(get_tree(), 0)


func _rebuild() -> void:
	input.selected = -1
	board.build(mission)
	units.build(board)
	_sync()
	input.refresh()


func _engineer_tile(id: int) -> Vector2i:
	var u: Dictionary = mission.get_units()[id]
	return Vector2i(u["x"], u["y"])


## A fresh mission from `seed`, discarding the current one. Only offered
## seeds (proven winnable) are played; any other moves on to the next one.
func restart(seed: int) -> void:
	if playing:
		return
	var offered := seed if BreakwaterMission.is_offered(seed) else BreakwaterMission.next_offered(seed)
	mission = BreakwaterMission.new()
	mission.generate(offered)
	_acted = false
	_shoved = false
	_ended = false
	_rebuild()
	hud.say(
		(
			"Mission %d: proven survivable." % offered
			if offered == seed
			else "Seed %d is not proven survivable; mission %d instead." % [seed, offered]
		)
	)


## Takes back the last move, if it can be taken back.
func undo() -> void:
	if playing:
		return
	var err := mission.undo()
	_sync()
	input.selected = -1
	input.refresh()
	hud.say("Move undone." if err.is_empty() else err[0].to_upper() + err.substr(1) + ".")


## The next mission the game offers.
func next_mission() -> void:
	restart(BreakwaterMission.next_offered(mission.get_seed()))


## Writes every accepted command so far as a log bw_replay can replay, and
## returns its absolute path ("" on failure).
func save_log() -> String:
	DirAccess.make_dir_recursive_absolute(save_dir)
	var path := save_dir.path_join(
		"breakwater-seed%d-turn%d-%d.log" % [mission.get_seed(), mission.get_turn(), Time.get_unix_time_from_system()]
	)
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
	update_hints()


## Feeds the hints what the player has done so far.
func update_hints() -> void:
	if hints != null:
		hints.update(
			{
				"turn": mission.get_turn(),
				"selected": input.selected >= 0,
				"acted": _acted,
				"shoved": _shoved,
				"ended": _ended
			}
		)


## Called by the input after the mission accepts a player command.
func note_command(verb: String) -> void:
	_acted = _acted or verb in ["move", "shove", "pull"]
	_shoved = _shoved or verb in ["shove", "pull"]
	_ended = _ended or verb == "end"
	_verb = verb


## Plays back everything the mission reported since the last call, in order,
## then re-syncs. Input should wait for playback_finished.
func play_events() -> void:
	playing = true
	var events := mission.take_events()
	var enemy := false
	for e in events:
		enemy = enemy or int(e["kind"]) in ENEMY_PHASE_EVENTS
	var step := ""
	for e in events:
		if enemy:
			# Name each step of the enemy phase, and who is acting.
			var name: String = STEP_NAMES.get(int(e["kind"]), step)
			if int(e["kind"]) == BreakwaterMission.EVENT_UNIT_MOVED and not _is_crawler(int(e["unit"])):
				name = step
			if name != step and not name.is_empty():
				step = name
				hud.step(step)
				await _wait(0.35)
			units.mark_actor(int(e["unit"]) if int(e["kind"]) in ACTOR_EVENTS else -1)
		audio.play(Audio.effect_for(e, not _is_crawler(int(e["unit"])), _verb))
		await _play(e)
		if enemy and int(e["kind"]) in ACTOR_EVENTS:
			await _wait(0.12)
	units.mark_actor(-1)
	_sync()
	_verb = ""
	if _skipping:
		Engine.time_scale = _base_speed
		_skipping = false
		audio.hushed = false
	playing = false
	playback_finished.emit()


## Fast-forwards the playback in progress. The scene re-syncs from the
## mission when playback ends, so skipping lands on exactly the same state.
func skip() -> void:
	if playing and not _skipping:
		_skipping = true
		audio.hushed = true
		Engine.time_scale = maxf(_base_speed, 30.0)


func _is_crawler(id: int) -> bool:
	var units_now := mission.get_units()
	return id < units_now.size() and not units_now[id]["engineer"]


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


## The position a log reaches after its `turns`-th end turn (all of it when
## negative), without playback. The scene is built from the opening position
## and then synced, as in live play, so buildings razed along the way still
## show as rubble.
func _load_until(path: String, turns: int) -> void:
	var parsed := BreakwaterMission.parse_log(FileAccess.get_file_as_string(path))
	if not parsed["error"].is_empty():
		push_error("log: " + parsed["error"])
		return
	mission.generate(parsed["seed"])
	_rebuild()
	var ended := 0
	for c in parsed["commands"]:
		if turns >= 0 and ended >= turns:
			break
		match c["verb"]:
			"move":
				mission.move(c["engineer"], c["x"], c["y"])
			"shove":
				mission.shove(c["engineer"], c["dir"])
			"pull":
				mission.pull(c["engineer"], c["dir"])
			_:
				mission.end_turn()
				ended += 1
	mission.take_events()
	_sync()


## Runs a log through the scene exactly as a player's commands would go, so
## the presentation is exercised headless. Prints the result and quits.
func _autoplay(path: String, pace: float, hold: float) -> void:
	var parsed := BreakwaterMission.parse_log(FileAccess.get_file_as_string(path))
	if not parsed["error"].is_empty():
		printerr("autoplay: %s: %s" % [path, parsed["error"]])
		Audio.quit(get_tree(), 2)
		return
	mission.generate(parsed["seed"])
	_rebuild()
	await _wait(hold)
	var index := 0
	for c in parsed["commands"]:
		index += 1
		if pace > 0.0 and c["verb"] != "end":
			# Show what a player would see before acting: the selection, and
			# for a shove or a pull the preview of its outcome.
			input.click(_engineer_tile(int(c["engineer"])))
			if c["verb"] == "shove":
				input.hover(_engineer_tile(int(c["engineer"])) + input.DIRS[int(c["dir"])])
			elif c["verb"] == "pull":
				input.hover(_engineer_tile(int(c["engineer"])) + input.DIRS[int(c["dir"])] * 2)
			await _wait(pace)
		var err: String
		match c["verb"]:
			"move":
				err = mission.move(c["engineer"], c["x"], c["y"])
			"shove":
				err = mission.shove(c["engineer"], c["dir"])
			"pull":
				err = mission.pull(c["engineer"], c["dir"])
			_:
				err = mission.end_turn()
		if not err.is_empty():
			printerr("autoplay: command %d refused: %s" % [index, err])
			Audio.quit(get_tree(), 1)
			return
		await play_events()
		input.refresh()
	await _wait(hold)
	var outcome: String = {BreakwaterMission.OUTCOME_WON: "won", BreakwaterMission.OUTCOME_LOST: "lost"}.get(
		mission.get_outcome(), "in-progress"
	)
	print("autoplay: %d commands, outcome %s, hash %s" % [index, outcome, mission.get_state_hash_hex()])
	Audio.quit(get_tree(), 0)


func _wait(seconds: float) -> void:
	if seconds > 0.0:
		await get_tree().create_timer(seconds).timeout


func _build_environment() -> void:
	var center := Vector3(3.5, 0.0, 3.5)
	# Steep enough that a body hides little of the tile behind it, close
	# enough that the board fills the frame.
	var camera := Camera3D.new()
	camera.fov = 50.0
	camera.position = center + Vector3(0.0, 10.5, 6.0)
	add_child(camera)
	camera.look_at(center + Vector3(0.0, 0.6, 0.6))

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
		elif arg.begins_with("--"):
			args[arg.substr(2)] = "true"  # A bare flag, e.g. --hints.
	return args
