extends Node
## Self-capture for review and golden images. Needs no desktop screenshot
## permission because the game reads back its own viewport.
##
##   godot --path game -- --capture=/abs/path/out.png [--capture-frames=30]
##
## Waits the given number of frames, saves the root viewport as PNG, then quits
## with a non-zero exit code if saving failed. Inert without --capture.

var _path := ""
var _frames := 30


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture="):
			_path = arg.trim_prefix("--capture=")
		elif arg.begins_with("--capture-frames="):
			_frames = maxi(1, arg.trim_prefix("--capture-frames=").to_int())
	set_process(not _path.is_empty())


func _process(_delta: float) -> void:
	_frames -= 1
	if _frames > 0:
		return
	set_process(false)
	await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	var err := image.save_png(_path)
	if err != OK:
		printerr("capture: failed to write %s (error %d)" % [_path, err])
		get_tree().quit(1)
		return
	print("capture: wrote %s (%dx%d)" % [_path, image.get_width(), image.get_height()])
	get_tree().quit(0)
