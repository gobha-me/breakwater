class_name HealthRing
extends Node3D
## Health on the ground: a flat ring split into one arc per hit point, lying
## around a unit's feet or a building's footprint. Lit arcs are the HP left,
## dark arcs the HP lost. Nothing floats over the models.

const GAP_DEGREES := 16.0
const SPENT := Color(0.12, 0.12, 0.14)

var _arcs: Array[MeshInstance3D] = []
var _lit: StandardMaterial3D
var _spent: StandardMaterial3D


## `segments` arcs between radii `inner` and `outer`, lit in `color`.
func setup(segments: int, inner: float, outer: float, color: Color) -> void:
	_lit = _material(color, 0.5)
	_spent = _material(SPENT, 0.0)
	var span := 360.0 / segments
	for i in segments:
		# Gaps at front and back for two segments, so both arcs show at the
		# sides of a body seen from this camera.
		var start := 90.0 + i * span + GAP_DEGREES * 0.5
		var arc := MeshInstance3D.new()
		arc.mesh = arc_mesh(inner, outer, deg_to_rad(start), deg_to_rad(start + span - GAP_DEGREES), 16)
		arc.position.y = 0.03
		add_child(arc)
		_arcs.append(arc)


func show_hp(hp: int) -> void:
	for i in _arcs.size():
		_arcs[i].material_override = _lit if i < hp else _spent


## Recolours the lit arcs, e.g. to dim a spent engineer.
func set_color(color: Color, glow: float) -> void:
	_lit.albedo_color = color
	_lit.emission = color
	_lit.emission_energy_multiplier = glow


static func arc_mesh(inner: float, outer: float, from: float, to: float, steps: int) -> ArrayMesh:
	var verts := PackedVector3Array()
	for s in steps + 1:
		var a := lerpf(from, to, float(s) / steps)
		var d := Vector3(cos(a), 0.0, sin(a))
		verts.append(d * outer)
		verts.append(d * inner)
	var indices := PackedInt32Array()
	for s in steps:
		var o := s * 2
		indices.append_array([o, o + 2, o + 1, o + 1, o + 2, o + 3])
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


static func _material(color: Color, glow: float) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	if glow > 0.0:
		mat.emission_enabled = true
		mat.emission = color
		mat.emission_energy_multiplier = glow
	return mat
