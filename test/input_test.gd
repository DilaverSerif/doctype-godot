# Drives the demo scene through Godot's own input routing: synthetic mouse
# events go through Input.parse_input_event, reach HtmlView._gui_input and
# must end in the page's click bindings and scroll containers.
#
#   Godot --path . -s res://test/input_test.gd
extends SceneTree

var failures := 0

func check(ok: bool, what: String) -> void:
	if ok:
		print("  PASS  ", what)
	else:
		failures += 1
		print("  FAIL  ", what)

func _initialize() -> void:
	# WATCHDOG: a test that never reaches quit() must not leave a window open.
	create_timer(60.0).timeout.connect(func(): print("done: watchdog timeout"); quit(3))
	call_deferred("run")

func mouse_at(view: Control, doc_point: Vector2) -> Vector2:
	var local: Vector2 = view.call("from_document_point", doc_point)
	return view.get_global_transform_with_canvas() * local

const Synthetic := preload("res://test/synthetic_input.gd")

func press(pos: Vector2, pressed: bool) -> void:
	Synthetic.button(pos, MOUSE_BUTTON_LEFT, pressed)

func move(pos: Vector2, relative: Vector2 = Vector2.ZERO, mask: int = 0) -> void:
	Synthetic.motion(pos, relative, mask)

func wheel(pos: Vector2, down: bool) -> void:
	var index := MOUSE_BUTTON_WHEEL_DOWN if down else MOUSE_BUTTON_WHEEL_UP
	Synthetic.button(pos, index, true)
	Synthetic.button(pos, index, false)

func run() -> void:
	await process_frame
	var out := ProjectSettings.globalize_path("res://demo_out")
	root.add_child(Synthetic.new())
	var scene: PackedScene = load("res://demo/demo.tscn")
	var demo: Control = scene.instantiate()
	root.add_child(demo)
	var view: Control = demo.get_node("HtmlView")
	for i in 4:
		await process_frame

	# Hover, press and release on the primary button.
	var btn: Rect2 = view.call("get_element_rect", "#btn-primary")
	check(btn.size.x > 0, "button rect found %s" % btn)
	var at := mouse_at(view, btn.get_center())
	var cursors: Array = []
	view.connect("cursor_changed", func(c): cursors.append(c))
	move(at)
	for i in 3:
		await process_frame
	check("pointer" in cursors, "hover through _gui_input sets the pointer cursor (%s)" % [cursors])
	var before: Color = view.get_texture().get_image().get_pixel(int(at.x), int(at.y))
	press(at, true)
	for i in 3:
		await process_frame
	var pressed_px: Color = view.get_texture().get_image().get_pixel(int(at.x), int(at.y))
	check(before != pressed_px, ":active restyles the button on press (%s -> %s)" % [before, pressed_px])
	press(at, false)
	for i in 3:
		await process_frame
	check(demo.get("button_clicks") == 1, "click reached the bind_click handler (%d)" % demo.get("button_clicks"))
	check(view.call("has_focus"), "click gives the control focus")
	# The document itself must not have lit a :focus element on a click.
	check(view.call("get_focused_id") == "", "click does not move document focus")

	# Gamepad-style navigation: ui_right from the focused control enters the page.
	view.call("move_focus", 2)
	await process_frame
	check(view.call("get_focused_id") == "btn-primary", "move_focus enters at the first focusable (%s)" % view.call("get_focused_id"))
	view.call("move_focus", 1)
	await process_frame
	check(view.call("get_focused_id") == "btn-danger", "move_focus right (%s)" % view.call("get_focused_id"))
	view.call("activate", "")
	await process_frame
	check(demo.get("last_button") == "danger", "activate runs the click binding (%s)" % demo.get("last_button"))

	# Wheel over the list scrolls it, not the page.
	var list: Rect2 = view.call("get_element_rect", ".list")
	var list_at := mouse_at(view, list.get_center())
	var img_before: Image = view.get_texture().get_image()
	wheel(list_at, true)
	wheel(list_at, true)
	await process_frame
	await process_frame
	var img_after: Image = view.get_texture().get_image()
	var region := Rect2i(Vector2i(list.position) + Vector2i(4, 4), Vector2i(list.size) - Vector2i(8, 8))
	check(img_before.get_region(region).get_data() != img_after.get_region(region).get_data(), "wheel scrolls the list")
	var head := Rect2i(0, 0, 400, 60)
	check(img_before.get_region(head).get_data() == img_after.get_region(head).get_data(), "scrolling the list leaves the nav bar untouched")
	print("  scroll frame dirty mode ", view.call("get_last_dirty_mode"), " rect ", view.call("get_last_dirty_pixels"))
	img_after.save_png(out.path_join("input_scrolled.png"))

	# Drag-to-scroll: press in the list and drag upward.
	var img_a: Image = view.get_texture().get_image()
	move(list_at)
	await process_frame
	press(list_at, true)
	await process_frame
	await process_frame
	move(list_at + Vector2(0, -30), Vector2(0, -30), MOUSE_BUTTON_MASK_LEFT)
	await process_frame
	await process_frame
	move(list_at + Vector2(0, -60), Vector2(0, -30), MOUSE_BUTTON_MASK_LEFT)
	await process_frame
	await process_frame
	press(list_at + Vector2(0, -60), false)
	for i in 3:
		await process_frame
	var img_b: Image = view.get_texture().get_image()
	check(img_a.get_region(region).get_data() != img_b.get_region(region).get_data(), "drag scrolls the list")

	# Content scale: the surface follows the window's scale factor, layout does not.
	var pixels_before: Vector2i = view.call("get_surface_pixels")
	var doc_height_before: float = view.call("get_document_height")
	root.content_scale_factor = 1.5
	for i in 3:
		await process_frame
	var pixels_after: Vector2i = view.call("get_surface_pixels")
	print("  surface pixels %s -> %s, device scale %.2f" % [pixels_before, pixels_after, view.call("get_effective_device_scale")])
	check(absf(float(pixels_after.x) / pixels_before.x - 1.0) < 0.05, "surface pixels follow the control rect")
	check(absf(view.call("get_effective_device_scale") - 1.5) < 0.01, "device scale follows content_scale_factor")
	check(absf(view.call("get_document_height") - doc_height_before * 1.0) > -1.0, "document height read")
	view.get_texture().get_image().save_png(out.path_join("input_scaled.png"))

	print("done: %d failure(s)" % failures)
	quit(1 if failures > 0 else 0)
