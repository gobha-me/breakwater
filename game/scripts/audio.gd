extends Node
## Sound: one effect per animated event, and a music loop. Driven by the same
## events the animations play, so audio holds no rules. M mutes; - and =
## step the volume. Settings persist in user://settings.cfg for interactive
## play only; scripted runs (tests, captures) leave them alone.
##
## Headless runs and captures use Godot's dummy audio driver. There nothing is
## played, only recorded in `played`: a playback the driver never mixes is
## still held at exit, which the Godot gate counts as a leak. Movie Maker also
## runs on the dummy driver but mixes into the recording, so it plays. The
## effects and the music are generated with the venice CLI
## (assets/PROVENANCE.md).

const SETTINGS := "user://settings.cfg"
const SFX := [
	"step",
	"scuttle",
	"shove",
	"pull",
	"hit",
	"strike",
	"die",
	"drown",
	"building_hit",
	"razed",
	"tide",
	"spawn",
	"win",
	"lose",
	"turn",
]
## The music sits under the effects.
const MUSIC_DB := -12.0
const VOLUME_STEP := 0.1
## Effects started this many times at once share one voice each.
const VOICES := 6

var muted := false
var volume := 0.8
## Effects played, oldest first: for the input test.
var played: PackedStringArray = []
## True while the enemy phase fast-forwards: effects would only clatter.
var hushed := false

var _persist := false
var _audible := false
var _streams := {}
var _voices: Array[AudioStreamPlayer] = []
var _next_voice := 0
var _music: AudioStreamPlayer


func setup(persist: bool) -> void:
	_persist = persist
	if _persist:
		var cfg := ConfigFile.new()
		if cfg.load(SETTINGS) == OK:
			muted = cfg.get_value("audio", "muted", muted)
			volume = clampf(float(cfg.get_value("audio", "volume", volume)), 0.0, 1.0)
	_apply()


func _ready() -> void:
	add_to_group("audio")
	_audible = AudioServer.get_driver_name() != "Dummy" or not Engine.get_write_movie_path().is_empty()
	if not _audible:
		return
	for name in SFX:
		_streams[name] = load("res://assets/audio/%s.ogg" % name)
	for i in VOICES:
		var voice := AudioStreamPlayer.new()
		add_child(voice)
		_voices.append(voice)
	_music = AudioStreamPlayer.new()
	var music: AudioStreamOggVorbis = load("res://assets/audio/music.ogg")
	music.loop = true
	_music.stream = music
	_music.volume_db = MUSIC_DB
	add_child(_music)
	_music.play()
	_apply()


## Plays effect `name`, if it exists and sound is on.
func play(name: String) -> void:
	if name.is_empty():
		return
	played.append(name)
	if not _audible or muted or hushed or not _streams.has(name):
		return
	var voice := _voices[_next_voice]
	_next_voice = (_next_voice + 1) % _voices.size()
	voice.stream = _streams[name]
	voice.play()


## The effect for a mission event, or "" for none. `verb` is the command that
## caused the playback, so a pull sounds like a harpoon, not a shove.
static func effect_for(e: Dictionary, engineer: bool, verb: String) -> String:
	match int(e["kind"]):
		BreakwaterMission.EVENT_UNIT_MOVED:
			return "step" if engineer else "scuttle"
		BreakwaterMission.EVENT_UNIT_SHOVED:
			return "pull" if verb == "pull" else "shove"
		BreakwaterMission.EVENT_UNIT_DAMAGED:
			return "hit"
		BreakwaterMission.EVENT_UNIT_DIED:
			return "die"
		BreakwaterMission.EVENT_UNIT_DROWNED:
			return "drown"
		BreakwaterMission.EVENT_BUILDING_DAMAGED:
			return "building_hit" if int(e["value"]) > 0 else "razed"
		BreakwaterMission.EVENT_ATTACK_RESOLVED:
			return "strike"
		BreakwaterMission.EVENT_TIDE_ROSE:
			return "tide"
		BreakwaterMission.EVENT_UNIT_SPAWNED:
			return "spawn"
		BreakwaterMission.EVENT_TURN_STARTED:
			return "turn"
		BreakwaterMission.EVENT_MISSION_ENDED:
			return "win" if int(e["value"]) == BreakwaterMission.OUTCOME_WON else "lose"
	# IntentDeclared has no sound: the telegraphs are drawn after playback,
	# and several crawlers declare at once, so a sound each would be noise.
	return ""


## Quits the game, first stopping every sound and giving the mixer a moment
## to let go of them: a playback still held when the engine goes down is
## reported as a leak. Every quit goes through here.
static func quit(tree: SceneTree, code: int) -> void:
	tree.call_group("audio", "silence")
	await tree.create_timer(0.1, true, false, true).timeout
	tree.quit(code)


func silence() -> void:
	for voice in _voices:
		voice.stop()
		voice.stream = null
	if _music != null:
		_music.stop()
		_music.stream = null
	_streams.clear()


func toggle_mute() -> String:
	muted = not muted
	_save()
	_apply()
	return "Sound off" if muted else "Sound on"


func step_volume(direction: int) -> String:
	volume = clampf(snappedf(volume + direction * VOLUME_STEP, VOLUME_STEP), 0.0, 1.0)
	_save()
	_apply()
	return "Volume %d%%" % roundi(volume * 100.0)


func _apply() -> void:
	var bus := AudioServer.get_bus_index("Master")
	AudioServer.set_bus_mute(bus, muted)
	AudioServer.set_bus_volume_db(bus, linear_to_db(maxf(volume, 0.0001)))


func _save() -> void:
	if not _persist:
		return
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value("audio", "muted", muted)
	cfg.set_value("audio", "volume", volume)
	cfg.save(SETTINGS)
