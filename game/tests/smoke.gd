extends SceneTree
## Headless smoke test: the extension loads and the bridge plays the core's
## missions exactly: a committed log replays to its committed hash.
##   godot --headless --path game --script res://tests/smoke.gd

var _failures := 0


func _initialize() -> void:
	_check(ClassDB.class_exists("BreakwaterMission"), "BreakwaterMission is registered")
	if _failures == 0:
		_run_mission_checks()
	print("smoke: %s (%d failure(s))" % ["PASS" if _failures == 0 else "FAIL", _failures])
	quit(1 if _failures > 0 else 0)


func _run_mission_checks() -> void:
	var logs := ProjectSettings.globalize_path("res://").path_join("../tests/logs")
	var log_text := FileAccess.get_file_as_string(logs.path_join("win-seed20.log"))
	var hashes := FileAccess.get_file_as_string(logs.path_join("win-seed20.hashes"))
	_check(not log_text.is_empty() and not hashes.is_empty(), "committed win log and hashes are readable")

	# The whole win log, replayed through the bridge, lands on the committed hash.
	var m := BreakwaterMission.new()
	var err := m.load_log(log_text)
	_check(err.is_empty(), "win log replays through the bridge" + ("" if err.is_empty() else ": " + err))
	var final_line := ""
	for line in hashes.split("\n", false):
		if line.begins_with("end "):
			final_line = line
	_check(final_line.ends_with(" " + m.get_state_hash_hex()), "final hash matches win-seed20.hashes")
	_check(m.get_outcome() == BreakwaterMission.OUTCOME_WON, "outcome is won")
	_check(m.get_turn() == m.get_turn_count(), "ended on the last turn")
	_check(m.take_events().is_empty(), "replay leaves no events to animate")
	_check(m.end_turn() == "the mission is over", "commands after the ending are refused")
	_check(m.get_log().begins_with("breakwater-log 1\nseed 20\n"), "recorded log has the header and seed")

	# A fresh mission: setup is visible through queries, commands report reasons.
	var fresh := BreakwaterMission.new()
	fresh.generate(20)
	var units := fresh.get_units()
	_check(units.size() == 5, "three engineers and two crawlers at setup")
	_check(units[0]["engineer"] and not units[3]["engineer"], "engineers first, then crawlers")
	_check(fresh.get_tide() == 1 and fresh.get_turn() == 1, "turn 1 at tide 1")
	var intents_valid := true
	for u in units:
		intents_valid = intents_valid and int(u["intent"]) in [-1, 0, 1, 2, 3]
		intents_valid = intents_valid and (not u["engineer"] or int(u["intent"]) == -1)
	_check(intents_valid, "intents are a direction or -1, and only crawlers hold one")
	_check(not fresh.reachable(0).is_empty(), "engineer 0 has somewhere to go")
	var before := fresh.get_state_hash()
	_check(fresh.move(0, 99, 99) == "destination is out of reach", "far move refused with the core's reason")
	_check(fresh.move(-5, 0, 0) == "not an engineer", "negative id refused")
	_check(fresh.move(3, 0, 0) == "not an engineer", "crawler id refused")
	_check(
		fresh.move(0, 9223372036854775807, -9223372036854775807) == "destination is out of reach",
		"int64 extremes refused, not crashed"
	)
	_check(fresh.shove(0, 7) == "unknown direction", "bad direction refused")
	_check(fresh.get_state_hash() == before, "refusals change nothing")
	var to: Vector2i = fresh.reachable(0)[0]
	_check(fresh.move(0, to.x, to.y) == "", "a reachable move is accepted")
	var events := fresh.take_events()
	_check(
		events.size() == 1 and events[0]["kind"] == BreakwaterMission.EVENT_UNIT_MOVED and events[0]["to"] == to,
		"the move is reported as an event"
	)
	_check(fresh.get_units()[0]["action"] == BreakwaterMission.ACTION_MOVED, "engineer 0 has moved")
	_check(fresh.get_log().ends_with("move 0 %d %d\n" % [to.x, to.y]), "accepted commands are recorded")
	_check(
		(
			BreakwaterMission.is_offered(20)
			and not BreakwaterMission.is_offered(162)
			and not BreakwaterMission.is_offered(1000)
			and not BreakwaterMission.is_offered(-1)
		),
		"offered seeds: proven ones in 0-999 only"
	)
	_check(
		BreakwaterMission.next_offered(161) == 163 and BreakwaterMission.next_offered(999) == 0,
		"next offered seed skips rejected ones and wraps"
	)
	_run_undo_checks()
	_check(
		fresh.load_log("breakwater-log 1\nseed 1\nfly 0\n") == "line 3: unknown command 'fly'",
		"malformed log reported with its line"
	)


func _run_undo_checks() -> void:
	var m := BreakwaterMission.new()
	m.generate(20)
	var start := m.get_state_hash()
	var start_log := m.get_log()
	_check(m.undo() == "nothing to undo" and not m.can_undo(), "undo with nothing to undo is refused")
	var a: Vector2i = m.reachable(0)[0]
	var b: Vector2i = m.reachable(1)[0]
	m.move(0, a.x, a.y)
	var after_one := m.get_state_hash()
	m.move(1, b.x, b.y)
	_check(m.can_undo(), "a move can be undone")
	_check(m.undo() == "" and m.get_state_hash() == after_one, "undo restores the exact state before the last move")
	_check(m.undo() == "" and m.get_state_hash() == start, "moves undo one by one back to the start")
	_check(m.get_log() == start_log, "undone moves leave the log")
	m.take_events()
	# After a shove, nothing before it can be undone.
	var shoved := false
	for e in 3:
		for d in 4:
			if not shoved and m.preview_shove(e, d)["error"].is_empty():
				if not m.reachable(e).is_empty():
					m.move(e, m.reachable(e)[0].x, m.reachable(e)[0].y)
				shoved = m.shove(e, d) == ""
	if shoved:
		_check(m.undo() == "a shove cannot be undone", "a shove is final")
	var ended := BreakwaterMission.new()
	ended.generate(20)
	ended.move(0, a.x, a.y)
	ended.end_turn()
	_check(ended.undo() == "the turn has ended", "ending the turn is final")


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok   ", label)
	else:
		print("  FAIL ", label)
		_failures += 1
