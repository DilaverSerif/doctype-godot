# Keeps the real mouse out of a test window. Every synthetic event a test
# pushes carries device SYNTHETIC; anything else from a pointer is swallowed
# before it reaches the GUI, so the physical cursor hovering over the window
# that just opened cannot change what the test observes.
extends Node

const SYNTHETIC := 42

func _ready() -> void:
	process_priority = -1000

func _input(event: InputEvent) -> void:
	if (event is InputEventMouse or event is InputEventScreenTouch or event is InputEventScreenDrag or event is InputEventGesture) and event.device != SYNTHETIC:
		get_viewport().set_input_as_handled()

static func motion(pos: Vector2, relative: Vector2 = Vector2.ZERO, mask: int = 0) -> void:
	var ev := InputEventMouseMotion.new()
	ev.device = SYNTHETIC
	ev.position = pos
	ev.global_position = pos
	ev.relative = relative
	ev.button_mask = mask
	Input.parse_input_event(ev)

static func button(pos: Vector2, index: MouseButton, pressed: bool) -> void:
	var ev := InputEventMouseButton.new()
	ev.device = SYNTHETIC
	ev.button_index = index
	ev.pressed = pressed
	ev.position = pos
	ev.global_position = pos
	Input.parse_input_event(ev)
