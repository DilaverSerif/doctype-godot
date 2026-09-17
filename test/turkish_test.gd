# text-transform under the Turkish locale: "i" must uppercase to İ (with a
# dot), which only shows in pixels, so two surfaces are compared.
extends SceneTree

func _initialize() -> void:
	# WATCHDOG: a test that never reaches quit() must not leave a window open.
	create_timer(60.0).timeout.connect(func(): print("done: watchdog timeout"); quit(3))
	call_deferred("run")

func make_view(lang: String) -> Control:
	var view: Control = ClassDB.instantiate("HtmlView")
	view.set("match_control_size", false)
	view.set("match_content_scale", false)
	view.set("surface_size", Vector2i(300, 80))
	view.set("background", Color.BLACK)
	view.set("language", lang)
	view.set("culture", "")
	view.set("html", "<body style='margin:0;color:#fff;font-size:40px'><p id='t' style='margin:0;text-transform:uppercase'>iiii ıııı</p></body>")
	root.add_child(view)
	return view

func run() -> void:
	await process_frame
	var en := make_view("en")
	var tr := make_view("tr")
	for i in 4:
		await process_frame
	var a: Image = en.get_texture().get_image()
	var b: Image = tr.get_texture().get_image()
	var out := ProjectSettings.globalize_path("res://demo_out")
	a.save_png(out.path_join("upper_en.png"))
	b.save_png(out.path_join("upper_tr.png"))
	var differs := a.get_data() != b.get_data()
	print("en/tr uppercase differ: ", differs)
	print("done: %d failure(s)" % (0 if differs else 1))
	quit(0 if differs else 1)
