# Drives the demo scene through its pages and saves one PNG per page.
#
#   Godot --path . -s res://test/demo_pages_test.gd
extends SceneTree

func _initialize() -> void:
	# WATCHDOG: a test that never reaches quit() must not leave a window open.
	create_timer(60.0).timeout.connect(func(): print("done: watchdog timeout"); quit(3))
	call_deferred("run")

func run() -> void:
	await process_frame
	var out := ProjectSettings.globalize_path("res://demo_out")
	DirAccess.make_dir_recursive_absolute(out)
	var scene: PackedScene = load("res://demo/demo.tscn")
	var demo: Control = scene.instantiate()
	root.add_child(demo)
	var view: Control = demo.get_node("HtmlView")
	for i in 4:
		await process_frame
	var failures := 0
	for page in ["overview", "animation", "typography", "about"]:
		demo.call("_on_anchor_clicked", "page://" + page)
		for i in 6:
			await process_frame
		var stats: Dictionary = view.call("get_stats")
		var err: String = view.call("get_last_error")
		print("page %s: quads %d parse %.2f layout %.2f draw %.2f dirty %d err '%s'" % [page, stats["quads"], stats["parse_ms"], stats["layout_ms"], stats["draw_ms"], stats["last_dirty_mode"], err])
		if stats["quads"] < 20:
			failures += 1
		view.get_texture().get_image().save_png(out.path_join("demo_%s.png" % page))
	# Animation page: two consecutive frames must differ and repaint partially.
	demo.call("_on_anchor_clicked", "page://animation")
	for i in 3:
		await process_frame
	var a: Image = view.get_texture().get_image()
	await process_frame
	await process_frame
	var b: Image = view.get_texture().get_image()
	var stats2: Dictionary = view.call("get_stats")
	print("animation frame: dirty mode %d rect %s draw %.2f ms layout %.2f ms" % [stats2["last_dirty_mode"], stats2["last_dirty_pixels"], stats2["draw_ms"], stats2["layout_ms"]])
	var differs := a.get_data() != b.get_data()
	print("animation frames differ: ", differs)
	if not differs:
		failures += 1
	print("done: %d failure(s)" % failures)
	quit(1 if failures > 0 else 0)
