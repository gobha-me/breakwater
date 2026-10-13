extends CanvasLayer
## Turn, tide, each engineer's state, the last message, and the ending.

const ACTION_NAMES := {
	BreakwaterMission.ACTION_READY: "ready",
	BreakwaterMission.ACTION_MOVED: "moved, may shove",
	BreakwaterMission.ACTION_DONE: "done",
}

var _status: Label
var _engineers: Label
var _message: Label
var _banner: Label
var _hints: Label
var _step: Label
var _step_tween: Tween
## The last enemy-phase step announced: for the input test.
var last_step := ""


func _ready() -> void:
	var panel := PanelContainer.new()
	panel.position = Vector2(16, 16)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.05, 0.08, 0.12, 0.75)
	style.set_content_margin_all(12)
	style.set_corner_radius_all(6)
	panel.add_theme_stylebox_override("panel", style)
	add_child(panel)
	var column := VBoxContainer.new()
	panel.add_child(column)
	_status = _label(column, 22)
	_engineers = _label(column, 18)
	_message = _label(column, 16)
	_message.modulate = Color(1, 0.8, 0.5)

	_hints = Label.new()
	_hints.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
	_hints.offset_top = -40
	_hints.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_hints.add_theme_font_size_override("font_size", 16)
	_hints.add_theme_constant_override("outline_size", 6)
	_hints.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_hints.text = ("Click an engineer   ·   green: move   ·   orange or WASD: shove   ·   Space: end turn"
			+ "   ·   Z undo move   ·   Tab / Esc   ·   R restart   ·   N next   ·   F5 save")
	add_child(_hints)

	_step = Label.new()
	_step.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP)
	_step.offset_top = 24
	_step.add_theme_font_size_override("font_size", 36)
	_step.add_theme_constant_override("outline_size", 12)
	_step.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_step.add_theme_color_override("font_color", Color(1.0, 0.85, 0.3))
	_step.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_step.grow_horizontal = Control.GROW_DIRECTION_BOTH
	add_child(_step)

	_banner = Label.new()
	_banner.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	_banner.add_theme_font_size_override("font_size", 64)
	_banner.add_theme_constant_override("outline_size", 16)
	_banner.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_banner.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_banner.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_banner.grow_vertical = Control.GROW_DIRECTION_BOTH
	add_child(_banner)


func sync(mission: BreakwaterMission) -> void:
	var tide := mission.get_tide()
	var over := mission.get_outcome() != BreakwaterMission.OUTCOME_IN_PROGRESS
	_status.text = ("Turn %d / %d     Tide %d" % [mission.get_turn(), mission.get_turn_count(), tide]
			+ ("" if over else "  (next %d)" % (tide + 1)))
	var lines: PackedStringArray = []
	for u in mission.get_units():
		if not u["engineer"]:
			continue
		var state: String = "lost"
		if u["alive"]:
			state = "standing" if over else ACTION_NAMES[int(u["action"])]
		lines.append("Engineer %d   %s   %s" % [int(u["id"]) + 1, "●".repeat(u["hp"]), state])
	_engineers.text = "\n".join(lines)
	match mission.get_outcome():
		BreakwaterMission.OUTCOME_WON:
			_banner.text = "The town holds"
			_banner.add_theme_color_override("font_color", Color(0.6, 1, 0.6))
		BreakwaterMission.OUTCOME_LOST:
			_banner.text = "The town is lost"
			_banner.add_theme_color_override("font_color", Color(1, 0.45, 0.4))
		_:
			_banner.text = ""


## Announces an enemy-phase step at the top of the screen, fading after.
func step(text: String) -> void:
	last_step = text
	_step.text = text
	_step.modulate.a = 1.0
	if _step_tween != null:
		_step_tween.kill()
	_step_tween = create_tween()
	_step_tween.tween_interval(1.2)
	_step_tween.tween_property(_step, "modulate:a", 0.0, 0.4)


func say(text: String) -> void:
	_message.text = text


func _label(parent: Control, size: int) -> Label:
	var label := Label.new()
	label.add_theme_font_size_override("font_size", size)
	parent.add_child(label)
	return label
