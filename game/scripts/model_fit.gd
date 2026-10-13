class_name ModelFit
## Loads a generated model and fits it to the board: scaled so its largest
## horizontal extent (or its height) matches a target, centred on its footprint,
## and standing on y = 0. Meshy's GLBs come at arbitrary scale and origin, so
## this is the one place that normalises them (assets/PROVENANCE.md, "Edits").


## A fitted instance of the model at `path`, or null when it is missing, so
## callers can fall back to placeholder shapes.
static func instance(path: String, footprint: float, max_height: float) -> Node3D:
	if not ResourceLoader.exists(path):
		return null
	var scene: PackedScene = load(path)
	var model := scene.instantiate() as Node3D
	var box := _bounds(model, Transform3D.IDENTITY)
	if box.size == Vector3.ZERO:
		return model
	var scale := minf(footprint / maxf(box.size.x, box.size.z), max_height / box.size.y)
	var holder := Node3D.new()
	holder.add_child(model)
	model.scale = Vector3.ONE * scale
	var center := box.get_center()
	model.position = Vector3(-center.x, -box.position.y, -center.z) * scale
	return holder


## The merged AABB of every mesh under `node`, in the space of `xform`.
static func _bounds(node: Node, xform: Transform3D) -> AABB:
	var box := AABB()
	var found := false
	var here := xform * (node as Node3D).transform if node is Node3D else xform
	if node is MeshInstance3D and node.mesh != null:
		box = here * node.mesh.get_aabb()
		found = true
	for child in node.get_children():
		var sub := _bounds(child, here)
		if sub.size != Vector3.ZERO:
			box = sub if not found else box.merge(sub)
			found = true
	return box
