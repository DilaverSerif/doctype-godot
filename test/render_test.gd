# Renders demo.html through the real GPU path, saves PNGs into demo_out/ and
# prints what the renderer reports. Run from the project folder:
#
#   Godot --path . -s res://test/render_test.gd
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

func run() -> void:
	await process_frame
	var out := ProjectSettings.globalize_path("res://demo_out")
	DirAccess.make_dir_recursive_absolute(out)

	var view: Control = ClassDB.instantiate("HtmlView")
	check(view != null, "HtmlView instantiates")
	view.set("match_control_size", false)
	view.set("match_content_scale", false)
	view.set("surface_size", Vector2i(800, 600))
	view.set("background", Color(0.07, 0.078, 0.11, 1.0))
	view.set("html_path", "res://demo/demo.html")
	var cursors: Array = []
	view.connect("cursor_changed", func(c): cursors.append(c))
	var anchors: Array = []
	view.connect("anchor_clicked", func(u): anchors.append(u))
	root.add_child(view)

	for i in 3:
		await process_frame
	var stats: Dictionary = view.call("get_stats")
	print("stats after load: ", stats)
	check(stats["quads"] > 100, "demo page emits quads (%d)" % stats["quads"])
	check(stats["font_atlas"].x > 0, "font atlas uploaded %s" % [stats["font_atlas"]])
	check(view.call("get_last_error") == "", "no native error: '%s'" % view.call("get_last_error"))
	var tex: Texture2D = view.call("get_texture")
	check(tex != null, "surface texture exists")
	var img: Image = tex.get_image()
	check(img != null and img.get_width() == 800 and img.get_height() == 600, "surface is 800x600")
	img.save_png(out.path_join("godot_demo.png"))
	var bg: Color = img.get_pixel(2, 2)
	print("  corner pixel: ", bg)
	check(bg.a > 0.99 and bg.r < 0.2, "background painted")
	var card: Color = img.get_pixel(400, 100)
	print("  card pixel: ", card)
	check(card != bg, "card gradient painted")

	# Partial redraw: one text node changes, only its rect repaints.
	var changed: bool = view.call("set_text", "h1", "litehtml -> Godot GPU")
	check(changed, "set_text on h1")
	await process_frame
	await process_frame
	var mode: int = view.call("get_last_dirty_mode")
	var dirty: Rect2i = view.call("get_last_dirty_pixels")
	print("  after set_text: dirty mode ", mode, " pixels ", dirty, " uploaded ", view.call("get_stats")["uploaded_vertices"])
	check(mode == 2, "set_text repaints a rect, not the page")
	check(dirty.size.y < 200, "dirty rect is a strip")
	view.get_texture().get_image().save_png(out.path_join("godot_demo_settext.png"))

	print("  set_text frame: parse %.2f layout %.2f draw %.2f ms" % [view.call("get_parse_ms"), view.call("get_layout_ms"), view.call("get_draw_ms")])

	# Nothing changed: no work at all, not even a record.
	var renders_before: int = view.call("get_stats")["renders"]
	await process_frame
	await process_frame
	check(view.call("get_stats")["renders"] == renders_before, "idle frames do not render")

	# Hit testing and hover.
	var card_rect: Rect2 = view.call("get_element_rect", ".card")
	print("  card rect: ", card_rect)
	check(card_rect.size.x > 700 and card_rect.size.y > 300, "block element rect found")
	# Inline elements carry no box of their own in litehtml; their position is
	# still reported, so probe just inside it.
	var link: Rect2 = view.call("get_element_rect", "a")
	print("  anchor rect: ", link)
	check(link.position.x > 0, "anchor position found")
	var center: Vector2 = link.position + Vector2(20, 8)
	check(view.call("element_at", center) == "", "element_at ignores elements without id")
	view.call("pointer_move", center)
	await process_frame
	await process_frame
	print("  cursors: ", cursors)
	check("pointer" in cursors, "hover over the link sets the pointer cursor")
	view.call("pointer_down", center)
	view.call("pointer_up", center)
	await process_frame
	print("  anchors clicked: ", anchors)
	check(anchors.size() == 1 and anchors[0].begins_with("https://github.com/litehtml"), "click reaches anchor_clicked")

	# Transparency: a half-transparent page over nothing must land premultiplied.
	view.set("background", Color(0, 0, 0, 0))
	view.call("load_html", "<body style='margin:0'><div style='width:800px;height:600px;background:rgba(255,0,0,0.5)'></div></body>")
	for i in 3:
		await process_frame
	var p: Color = view.get_texture().get_image().get_pixel(100, 100)
	print("  premultiplied pixel: ", p)
	check(absf(p.a - 0.5) < 0.03 and absf(p.r - 0.5) < 0.03 and p.g < 0.02, "surface holds premultiplied colour")

	# set_style animation path.
	view.call("load_html", "<body style='margin:0;background:#000'><div id='bar' style='width:100px;height:40px;background:#3b82f6'></div><p id='n'>0</p></body>")
	for i in 2:
		await process_frame
	var ok_style: bool = view.call("set_style", "#bar", "width:300px;height:40px;background:#3b82f6")
	check(ok_style, "set_style applies")
	await process_frame
	await process_frame
	var r: Rect2 = view.call("get_element_rect", "#bar")
	check(absf(r.size.x - 300.0) < 0.5, "set_style moved layout (%s)" % r)
	view.get_texture().get_image().save_png(out.path_join("godot_setstyle.png"))

	print("done: %d failure(s)" % failures)
	quit(1 if failures > 0 else 0)
