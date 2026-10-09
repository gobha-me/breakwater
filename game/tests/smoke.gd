extends SceneTree
## Headless smoke test: the extension loads and the bridge matches the core.
##   godot --headless --path game --script res://tests/smoke.gd

var _failures := 0


func _initialize() -> void:
	_check(ClassDB.class_exists("BreakwaterBoard"), "BreakwaterBoard is registered")
	if _failures == 0:
		_run_board_checks()
	print("smoke: %s (%d failure(s))" % ["PASS" if _failures == 0 else "FAIL", _failures])
	quit(1 if _failures > 0 else 0)


func _run_board_checks() -> void:
	var a := BreakwaterBoard.new()
	var b := BreakwaterBoard.new()
	a.generate(1234)
	b.generate(1234)
	_check(a.get_size() == 8, "board is 8x8")
	_check(a.get_state_hash() == b.get_state_hash(), "same seed, same state")
	b.generate(1235)
	_check(a.get_state_hash() != b.get_state_hash(), "different seed, different state")

	var before := a.get_state_hash()
	a.advance_tide()
	_check(a.get_tide() == 1, "tide advances")
	_check(a.get_state_hash() != before, "tide changes state hash")

	var buildings := 0
	for y in a.get_size():
		for x in a.get_size():
			buildings += 1 if a.get_building_hp(x, y) > 0 else 0
	_check(buildings == 3, "three buildings")
	_check(not BreakwaterBoard.core_version().is_empty(), "core version reported")


func _check(condition: bool, label: String) -> void:
	if condition:
		print("  ok   ", label)
	else:
		print("  FAIL ", label)
		_failures += 1
