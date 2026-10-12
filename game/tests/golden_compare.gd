extends SceneTree
## Compares a capture with a golden image. Runs headless: images are loaded,
## not rendered.
##   godot --headless --path game --script res://tests/golden_compare.gd -- \
##       --golden=<png> --actual=<png> --diff=<png> [--tolerance=24] [--max-share=0.005]
##
## A pixel differs when any channel differs by more than `tolerance` (0-255).
## The comparison fails when the share of differing pixels exceeds `max-share`,
## or the sizes differ. Differing pixels are painted red in the diff image.


func _initialize() -> void:
	var args := {}
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--") and "=" in arg:
			args[arg.substr(2, arg.find("=") - 2)] = arg.substr(arg.find("=") + 1)
	var golden := Image.load_from_file(args.get("golden", ""))
	var actual := Image.load_from_file(args.get("actual", ""))
	if golden == null or actual == null:
		printerr("golden: cannot load images")
		quit(2)
		return
	if golden.get_size() != actual.get_size():
		print("golden: FAIL size %s vs %s" % [golden.get_size(), actual.get_size()])
		quit(1)
		return
	var tolerance := float(args.get("tolerance", "24")) / 255.0
	var max_share := float(args.get("max-share", "0.005"))
	var diff := Image.create(golden.get_width(), golden.get_height(), false, Image.FORMAT_RGB8)
	var differing := 0
	for y in golden.get_height():
		for x in golden.get_width():
			var a := golden.get_pixel(x, y)
			var b := actual.get_pixel(x, y)
			var delta := maxf(maxf(absf(a.r - b.r), absf(a.g - b.g)), absf(a.b - b.b))
			if delta > tolerance:
				differing += 1
				diff.set_pixel(x, y, Color(1, 0, 0))
			else:
				diff.set_pixel(x, y, a.darkened(0.6))
	var share := float(differing) / float(golden.get_width() * golden.get_height())
	if args.has("diff"):
		diff.save_png(args["diff"])
	var verdict := "PASS" if share <= max_share else "FAIL"
	print("golden: %s %d pixels differ (%.3f%%, limit %.3f%%)" % [verdict, differing, share * 100.0, max_share * 100.0])
	quit(0 if verdict == "PASS" else 1)
