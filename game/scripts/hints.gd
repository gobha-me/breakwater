extends CanvasLayer
## First-mission hints: one at a time, each shown when it becomes relevant and
## retired when the player does what it teaches. H dismisses the current hint;
## Shift+H turns hints off for good. Dismissals persist in user://settings.cfg.
## Scripted runs (logs, autoplay, captures, tests) never show hints, so the
## gates and the golden are unaffected; --hints forces them on for review.

const SETTINGS := "user://settings.cfg"

## In the order they are taught. Each entry: id, text.
const HINTS := [
	["select", "Click one of your engineers to select it."],
	["act", "Green tiles: where it can move.\nOrange: units it can shove away. Hover one to see exactly what the shove will do."],
	["strikes", "Red arrows are next turn's crawler strikes. A strike razes a cottage or fells an engineer.\nShove a crawler and its strike turns with it."],
	["tide", "Blue hatching marks tiles the next tide will flood. Engineers drown in water.\nPress Space to end your turn."],
	["goal", "Hold the town for 5 turns: keep at least one cottage and one engineer standing."],
]

var enabled := true
var _persist := true
var _seen := {}
var _panel: PanelContainer
var _label: Label
## The hint on screen, or "": for the input test.
var showing := ""
var _last_state := {}


func setup(on: bool, persist: bool) -> void:
	enabled = on
	_persist = persist
	_seen.clear()
	if _persist:
		var cfg := ConfigFile.new()
		if cfg.load(SETTINGS) == OK:
			enabled = enabled and not cfg.get_value("hints", "off", false)
			for h in HINTS:
				_seen[h[0]] = cfg.get_value("hints", h[0], false)


func _ready() -> void:
	_panel = PanelContainer.new()
	_panel.position = Vector2(16, 210)
	_panel.custom_minimum_size = Vector2(270, 0)
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.1, 0.16, 0.24, 0.9)
	style.border_color = Color(1.0, 0.85, 0.3)
	style.set_border_width_all(2)
	style.set_content_margin_all(12)
	style.set_corner_radius_all(6)
	_panel.add_theme_stylebox_override("panel", style)
	add_child(_panel)
	_label = Label.new()
	_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_label.custom_minimum_size = Vector2(246, 0)
	_label.add_theme_font_size_override("font_size", 17)
	_panel.add_child(_label)
	_panel.visible = false


## Picks the hint for the moment. `state` holds: turn, selected (bool),
## acted (bool, a move or shove this mission), shoved (bool), ended (bool).
func update(state: Dictionary) -> void:
	_last_state = state
	if not enabled:
		_show("")
		return
	# Retire what the player has already done.
	if state["selected"]:
		_mark("select")
	if state["acted"]:
		_mark("act")
	if state["shoved"]:
		_mark("strikes")
	if state["ended"]:
		_mark("tide")
	if int(state["turn"]) >= 3:
		_mark("goal")
	for h in HINTS:
		if not _seen.get(h[0], false):
			# "goal" waits for the second turn; the rest come in order.
			if h[0] == "goal" and int(state["turn"]) < 2:
				break
			_show(h[0])
			return
	_show("")


func dismiss() -> void:
	if not showing.is_empty():
		_mark(showing)
		if not _last_state.is_empty():
			update(_last_state)
		else:
			_show("")


func turn_off() -> void:
	enabled = false
	_show("")
	_save("off", true)


func _mark(id: String) -> void:
	if not _seen.get(id, false):
		_seen[id] = true
		_save(id, true)


func _save(key: String, value: bool) -> void:
	if not _persist:
		return
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value("hints", key, value)
	cfg.save(SETTINGS)


func _show(id: String) -> void:
	showing = id
	_panel.visible = not id.is_empty()
	for h in HINTS:
		if h[0] == id:
			_label.text = h[1] + "\n\n[H] got it   ·   [Shift+H] no more hints"
