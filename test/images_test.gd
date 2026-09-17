# Images and stylesheet imports: a registered texture must land in the atlas
# the right way up, a res:// texture must resolve, and <link rel=stylesheet>
# must reach the res:// file system.
extends SceneTree

var failures := 0

func check(ok: bool, what: String) -> void:
	if ok:
		print("  PASS  ", what)
	else:
		failures += 1
		print("  FAIL  ", what)

const Synthetic := preload("res://test/synthetic_input.gd")

func _initialize() -> void:
	# WATCHDOG: a test that never reaches quit() must not leave a window open.
	create_timer(60.0).timeout.connect(func(): print("done: watchdog timeout"); quit(3))
	call_deferred("run")

func run() -> void:
	await process_frame
	var out := ProjectSettings.globalize_path("res://demo_out")
	DirAccess.make_dir_recursive_absolute(out)

	# Top half red, bottom half blue: orientation is visible in pixels.
	var img := Image.create_empty(32, 32, false, Image.FORMAT_RGBA8)
	img.fill_rect(Rect2i(0, 0, 32, 16), Color.RED)
	img.fill_rect(Rect2i(0, 16, 32, 16), Color.BLUE)
	var tex := ImageTexture.create_from_image(img)
	img.save_png(out.path_join("img_src.png"))
	# The same picture on disk, for the res:// path.
	var disk_path := ProjectSettings.globalize_path("res://test/assets/generated.png")
	img.save_png(disk_path)

	root.add_child(Synthetic.new())
	var view: Control = ClassDB.instantiate("HtmlView")
	view.set("match_control_size", false)
	view.set("match_content_scale", false)
	view.set("surface_size", Vector2i(300, 200))
	view.set("background", Color.BLACK)
	view.call("register_image", "twotone", tex)
	view.set("html", """<html><head><link rel="stylesheet" href="res://test/assets/import.css"></head>
	<body style='margin:0'>
	<img id='a' src='twotone' style='position:absolute;left:10px;top:10px;width:32px;height:32px'>
	<img id='b' src='res://test/assets/generated.png' style='position:absolute;left:60px;top:10px;width:64px;height:64px'>
	<div id='imported' style='position:absolute;left:10px;top:100px'></div>
	</body></html>""")
	root.add_child(view)
	for i in 5:
		await process_frame

	var surface: Image = view.get_texture().get_image()
	surface.save_png(out.path_join("images.png"))
	print("  error: '", view.call("get_last_error"), "'")

	var top: Color = surface.get_pixel(26, 18)
	var bottom: Color = surface.get_pixel(26, 34)
	print("  registered image top ", top, " bottom ", bottom)
	check(top.r > 0.9 and top.b < 0.1, "registered image draws (top half red)")
	check(bottom.b > 0.9 and bottom.r < 0.1, "registered image is the right way up (bottom half blue)")

	var top2: Color = surface.get_pixel(92, 26)
	var bottom2: Color = surface.get_pixel(92, 58)
	print("  res:// image top ", top2, " bottom ", bottom2)
	check(top2.r > 0.9 and bottom2.b > 0.9, "res:// texture resolves and scales")

	var rect_b: Rect2 = view.call("get_element_rect", "#b")
	check(absf(rect_b.size.x - 64.0) < 0.5, "image box laid out at the requested size (%s)" % rect_b)

	var rect_i: Rect2 = view.call("get_element_rect", "#imported")
	print("  imported rule rect ", rect_i)
	check(absf(rect_i.size.x - 123.0) < 0.5 and absf(rect_i.size.y - 45.0) < 0.5, "<link rel=stylesheet> loaded from res://")
	var g: Color = surface.get_pixel(60, 120)
	check(g.g > 0.9 and g.r < 0.1, "imported rule painted")

	# Pass-through: only elements with an id catch the pointer.
	view.set_size(Vector2(300, 200))
	check(view.call("element_at", Vector2(20, 20)) == "a", "element_at finds the image by id")

	# Pass-through: a plain control sits behind the page. Over bare page the
	# pointer falls through to it, exactly like a HUD over a game; over an id
	# element the page keeps the pointer.
	var behind := Control.new()
	behind.name = "Behind"
	behind.set_size(Vector2(300, 200))
	behind.mouse_filter = Control.MOUSE_FILTER_STOP
	root.add_child(behind)
	root.move_child(behind, 0)
	view.set("pass_through_empty_areas", true)
	await process_frame
	var origin: Vector2 = view.get_global_position()

	Synthetic.motion(origin + Vector2(250, 180)) # bare page
	for i in 3:
		await process_frame
	check(root.gui_get_hovered_control() == behind, "pass-through: bare page lets the pointer through to the control behind (%s)" % root.gui_get_hovered_control())

	Synthetic.motion(origin + Vector2(60, 120)) # #imported carries an id
	for i in 3:
		await process_frame
	check(root.gui_get_hovered_control() != behind, "pass-through: an id element keeps the pointer (%s)" % root.gui_get_hovered_control())

	view.set("pass_through_empty_areas", false)
	Synthetic.motion(origin + Vector2(20, 20))
	for i in 2:
		await process_frame
	Synthetic.motion(origin + Vector2(250, 180))
	for i in 3:
		await process_frame
	check(root.gui_get_hovered_control() == view, "without pass-through, bare page is caught by the view (%s)" % root.gui_get_hovered_control())

	DirAccess.remove_absolute(disk_path)
	print("done: %d failure(s)" % failures)
	quit(1 if failures > 0 else 0)
