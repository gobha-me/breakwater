extends SceneTree
## Average GPU and CPU render time of the main scene over 300 frames (run via
## tools/frame_time.sh, which turns vsync off).
func _initialize() -> void:
	_run.call_deferred()
func _run() -> void:
	root.size = Vector2i(1280, 720)
	var main: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	var vp := root.get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp, true)
	for i in 60: await process_frame
	var gpu := 0.0
	var cpu := 0.0
	for i in 300:
		await process_frame
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(vp)
		cpu += RenderingServer.viewport_get_measured_render_time_cpu(vp)
	print("frame_time: gpu %.3f ms, cpu %.3f ms" % [gpu / 300.0, cpu / 300.0])
	quit(0)
