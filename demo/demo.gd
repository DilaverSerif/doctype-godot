# Doctype demo: a navigable, animated page driven entirely from GDScript.
#
# litehtml has no JavaScript and no CSS animations, so this does what the
# engine expects: numbers change through set_text, animated boxes through
# set_style, and only a page change goes through load_html.
extends Control

@onready var view: Control = $HtmlView

var page := "overview"
var clock := 0.0
var fps_smoothed := 60.0
var last_link := "-"
var button_clicks := 0
var last_button := "-"
var next_stats := 0.0
var reported := false

const PAGES := ["overview", "animation", "typography", "about"]
const PAGE_TITLES := {"overview": "Genel", "animation": "Animasyon", "typography": "Tipografi", "about": "Hakkında"}

func _ready() -> void:
	view.connect("anchor_clicked", _on_anchor_clicked)
	view.connect("element_clicked", _on_element_clicked)
	view.call("bind_click", "btn-primary", func(click): _on_button(click))
	view.call("bind_click", "btn-danger", func(click): _on_button(click))
	view.set("language", "tr")
	view.set("culture", "tr-TR")
	view.set("html", build_document())

func _process(delta: float) -> void:
	clock += delta
	if delta > 0.0:
		fps_smoothed = lerpf(fps_smoothed, 1.0 / delta, 0.05)

	if page == "animation":
		animate()

	if clock >= next_stats:
		next_stats = clock + 0.25
		refresh_stats()
		if not reported and clock > 1.0:
			reported = true
			var stats: Dictionary = view.call("get_stats")
			print("Doctype demo: %s / %s, %d quads, parse %.2f ms, layout %.2f ms, draw %.2f ms, surface %s @ %.2fx, error '%s'" % [
				RenderingServer.get_current_rendering_method(), RenderingServer.get_current_rendering_driver_name(),
				stats["quads"], stats["parse_ms"], stats["layout_ms"], stats["draw_ms"], stats["surface_pixels"], stats["device_scale"], view.call("get_last_error")])

func _on_anchor_clicked(url: String) -> void:
	last_link = url
	if url.begins_with("page://"):
		var wanted := url.trim_prefix("page://")
		if wanted in PAGES and wanted != page:
			page = wanted
			view.call("load_html", build_document())
	elif url.begins_with("http"):
		OS.shell_open(url)

func _on_element_clicked(id: String, tag: String, class_names: String, action: String) -> void:
	if id.is_empty():
		return
	print("element clicked: <%s id='%s' class='%s' data-action='%s'>" % [tag, id, class_names, action])

func _on_button(click: Dictionary) -> void:
	button_clicks += 1
	last_button = click["action"]
	view.call("set_text", "#stat-clicks", str(button_clicks))
	view.call("set_text", "#stat-last-button", last_button)

# --- live values: set_text, no re-parse ---------------------------------------

func refresh_stats() -> void:
	if page != "overview":
		return
	var stats: Dictionary = view.call("get_stats")
	view.call("set_text", "#stat-fps", "%d" % roundi(fps_smoothed))
	view.call("set_text", "#stat-quads", str(stats["quads"]))
	view.call("set_text", "#stat-cpu", "%.2f ms" % stats["total_ms"])
	view.call("set_text", "#stat-dirty", ["tam", "yok", "dikdörtgen", "kaydırma"][stats["last_dirty_mode"]])
	view.call("set_text", "#row-height", "%d px" % roundi(stats["document_height"]))
	view.call("set_text", "#row-surface", "%d x %d @ %.2fx" % [stats["surface_pixels"].x, stats["surface_pixels"].y, stats["device_scale"]])
	view.call("set_text", "#row-link", last_link)
	view.call("set_text", "#row-renderer", RenderingServer.get_current_rendering_method())

# --- animation: set_style, no re-parse ------------------------------------------

func animate() -> void:
	var t := clock
	var angle := fmod(t * 60.0, 360.0)
	view.call("set_style", "#a-conic", "background:conic-gradient(from %ddeg,#3b82f6,#22d3ee,#a855f7,#3b82f6)" % int(angle))
	var breath := 0.5 + 0.5 * sin(t * 2.0)
	var radius := 30 + 40 * breath
	view.call("set_style", "#a-radial", "background:radial-gradient(circle at 50%% 50%%,#f59e0b 0%%,#ef4444 %d%%,#1e2333 %d%%)" % [int(radius), int(radius + 20)])
	var pulse := 0.5 + 0.5 * sin(t * 3.0)
	var size := 48 + 24 * pulse
	var rad := 6 + 18 * pulse
	view.call("set_style", "#a-pulse", "width:%dpx;height:%dpx;border-radius:%dpx;background:rgb(%d,%d,%d);margin:%dpx" % [size, size, rad, 59 + 150 * pulse, 130, 246 - 150 * pulse, (72 - size) / 2])
	var slide := 0.5 + 0.5 * sin(t * 1.5)
	view.call("set_style", "#a-slide", "left:%dpx" % int(slide * 220))
	for i in 28:
		var h := 6 + 34 * (0.5 + 0.5 * sin(t * 4.0 + i * 0.35))
		view.call("set_style", "#a-bar%d" % i, "height:%dpx" % int(h))
	var stats: Dictionary = view.call("get_stats")
	view.call("set_text", "#a-stats", "layout %.2f ms · draw %.2f ms · %d quad · yeniden çizim: %s" % [stats["layout_ms"], stats["draw_ms"], stats["quads"], ["tam", "yok", "dikdörtgen", "kaydırma"][stats["last_dirty_mode"]]])

# --- markup -------------------------------------------------------------------------

func css() -> String:
	return """
	body { margin:0; background:#12141c; font-family:sans-serif; color:#e6e9f0; font-size:15px; }
	.nav { display:flex; padding:12px 20px; background:#1a1e2c; border-bottom:1px solid #2c3450; }
	.nav a { color:#8e97b3; text-decoration:none; padding:8px 14px; border-radius:8px; margin-right:6px; }
	.nav a:hover { background:#2a3350; color:#fff; }
	.nav a.on { background:#3b82f6; color:#fff; }
	.page { padding:20px 24px; }
	.card { padding:20px 24px; border-radius:14px; margin-bottom:16px;
	        background:linear-gradient(135deg,#1e2333,#2a3350); border:1px solid #3d4870; }
	h1 { font-size:28px; margin:0 0 6px 0; color:#fff; }
	h2 { font-size:18px; margin:0 0 10px 0; color:#fff; }
	.sub { font-size:14px; color:#8e97b3; margin:0 0 18px 0; }
	.pill { display:inline-block; padding:5px 14px; border-radius:999px; background:#3b82f6; color:#fff; font-size:13px; margin-right:6px; }
	.pill.warn { background:#f59e0b; }
	.pill.bad { background:#ef4444; }
	table { width:100%; border-collapse:collapse; font-size:13px; }
	th, td { text-align:left; padding:7px 8px; border-bottom:1px solid #2c3450; }
	th { color:#8e97b3; font-weight:normal; }
	.stats { display:flex; }
	.stat { flex:1; padding:14px; margin-right:12px; border-radius:10px; background:#232a3d; text-align:center; }
	.stat b { display:block; font-size:26px; color:#7dd3fc; }
	.stat span { font-size:12px; color:#8e97b3; }
	.btn { display:inline-block; padding:10px 18px; border-radius:8px; background:#3b82f6; color:#fff; cursor:pointer; margin-right:8px; }
	.btn:hover { background:#2563eb; }
	.btn:active { background:#1d4ed8; }
	.btn.danger { background:#ef4444; }
	.btn.danger:hover { background:#dc2626; }
	.btn:focus { border:2px solid #fff; }
	.list { height:150px; overflow:auto; border:1px solid #2c3450; border-radius:8px; padding:6px 10px; background:#151926; }
	.list p { margin:4px 0; }
	a { color:#7dd3fc; }
	.anim-row { display:flex; margin-bottom:16px; }
	.anim-box { width:120px; height:120px; border-radius:12px; margin-right:16px; background:#1e2333; }
	.pulse-wrap { width:72px; height:72px; margin-right:16px; }
	.slide-track { position:relative; height:24px; background:#232a3d; border-radius:12px; width:300px; margin-top:16px; }
	.slide-track i { position:absolute; top:0; width:80px; height:24px; border-radius:12px; background:linear-gradient(to right,#22d3ee,#3b82f6); }
	.bars { display:flex; height:40px; }
	.bars i { display:block; width:8px; margin-right:4px; background:#3b82f6; border-radius:2px; }
	.bars-wrap { display:flex; height:44px; }
	code { font-family:monospace; background:#232a3d; padding:2px 6px; border-radius:4px; }
	"""

func nav() -> String:
	var s := "<div class='nav'>"
	for p in PAGES:
		s += "<a href='page://%s' class='%s'>%s</a>" % [p, "on" if p == page else "", PAGE_TITLES[p]]
	return s + "</div>"

func build_document() -> String:
	var body := ""
	match page:
		"overview":
			body = overview()
		"animation":
			body = animation()
		"typography":
			body = typography()
		"about":
			body = about()
	return "<html><head><style>%s</style></head><body>%s<div class='page'>%s</div></body></html>" % [css(), nav(), body]

func overview() -> String:
	var rows := ""
	for i in 40:
		rows += "<p>Satır %d — kaydırılabilir liste (overflow:auto), tekerlek veya sürükleme ile</p>" % (i + 1)
	return """
	<div class='card'>
		<h1>Doctype for Godot</h1>
		<p class='sub'>HTML/CSS oyun arayüzü. Layout CPU'da litehtml, çizim GPU'da tek mesh. Sayılar set_text ile yerinde güncelleniyor, parse yok.</p>
		<div class='stats'>
			<div class='stat'><b id='stat-fps'>-</b><span>fps</span></div>
			<div class='stat'><b id='stat-quads'>-</b><span>quad</span></div>
			<div class='stat'><b id='stat-cpu'>-</b><span>CPU / kare</span></div>
			<div class='stat'><b id='stat-dirty'>-</b><span>son yeniden çizim</span></div>
		</div>
	</div>
	<div class='card'>
		<h2>Etkileşim</h2>
		<p><span class='pill'>:hover</span><span class='pill warn'>:active</span><span class='pill bad'>:focus</span></p>
		<p>
			<span id='btn-primary' class='btn' tabindex='0' data-action='primary'>Birincil</span>
			<span id='btn-danger' class='btn danger' tabindex='0' data-action='danger'>Tehlikeli</span>
			<span>Tıklama: <b id='stat-clicks'>0</b> · son: <b id='stat-last-button'>-</b></span>
		</p>
		<div class='list'>%s</div>
	</div>
	<div class='card'>
		<table>
			<tr><th>Belge yüksekliği</th><td id='row-height'>-</td></tr>
			<tr><th>Yüzey</th><td id='row-surface'>-</td></tr>
			<tr><th>Renderer</th><td id='row-renderer'>-</td></tr>
			<tr><th>Son link</th><td id='row-link'>-</td></tr>
		</table>
	</div>
	""" % rows

func animation() -> String:
	var bars := ""
	for i in 28:
		bars += "<i id='a-bar%d' style='height:20px'></i>" % i
	return """
	<div class='card'>
		<h1>Animasyon</h1>
		<p class='sub'>litehtml'de CSS animation/transition yok. Değerler GDScript'te hesaplanıp set_style ile inline style olarak yazılıyor: parse yok, artımlı layout, yalnız değişen bölge yeniden çiziliyor.</p>
		<div class='anim-row'>
			<div id='a-conic' class='anim-box'></div>
			<div id='a-radial' class='anim-box'></div>
			<div class='pulse-wrap'><div id='a-pulse' style='width:48px;height:48px;border-radius:6px;background:#3b82f6;margin:12px'></div></div>
			<div class='bars-wrap'><div class='bars'>%s</div></div>
		</div>
		<div class='slide-track'><i id='a-slide' style='left:0px'></i></div>
		<p class='sub' style='margin-top:16px'><span id='a-stats'>-</span></p>
	</div>
	""" % bars

func typography() -> String:
	return """
	<div class='card'>
		<h1>Tipografi</h1>
		<p class='sub'>stb_truetype ile rasterize edilen glyph'ler bir R8 atlasta tutulur; metin GPU'da atlastan örneklenir.</p>
		<p style='font-size:32px'>Büyük başlık — 32px</p>
		<p style='font-size:20px'>Orta metin — 20px, <b>kalın</b>, <i>italik</i>, <u>altı çizili</u></p>
		<p>Türkçe: İstanbul'da ışıklı sokaklar, çöp kutuları ve güvercinler. ÇĞIİÖŞÜ çğıiöşü</p>
		<p style='text-transform:uppercase'>text-transform: uppercase — istanbul, iğdır, ılık</p>
		<p style='letter-spacing:3px'>letter-spacing: 3px</p>
		<p style='line-height:2'>line-height: 2 — satırlar arası boşluk iki kat. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.</p>
		<p><code>monospace kod bloğu</code> ve normal metin bir arada.</p>
		<p style='text-align:center'>ortalanmış</p>
		<p style='text-align:right'>sağa yaslı</p>
	</div>
	"""

func about() -> String:
	return """
	<div class='card'>
		<h1>Hakkında</h1>
		<p class='sub'>Doctype for Godot, <a href='https://github.com/DilaverSerif/doctype-unity'>doctype-unity</a> projesinin GDExtension portudur.</p>
		<table>
			<tr><th>Bileşen</th><th>Lisans</th></tr>
			<tr><td>litehtml</td><td>BSD-3-Clause</td></tr>
			<tr><td>gumbo-parser</td><td>Apache-2.0</td></tr>
			<tr><td>stb_truetype</td><td>Public domain / MIT</td></tr>
			<tr><td>godot-cpp</td><td>MIT</td></tr>
		</table>
		<p style='margin-top:16px'>Yuvarlak köşe, kenarlık, gradient ve kırpma CPU'da rasterize edilmez; hepsi fragment shader'da analitik olarak (SDF) kurulur.</p>
		<p><span class='pill'>Forward+</span><span class='pill'>Mobile</span><span class='pill'>Compatibility</span> — üç renderer'da da aynı canvas_item shader.</p>
	</div>
	"""
