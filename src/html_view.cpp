#include "html_view.h"

#include "doctype_shaders.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/classes/input_event_mouse_button.hpp>
#include <godot_cpp/classes/input_event_mouse_motion.hpp>
#include <godot_cpp/classes/input_event_pan_gesture.hpp>
#include <godot_cpp/classes/input_event_screen_drag.hpp>
#include <godot_cpp/classes/input_event_screen_touch.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/text_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

namespace {

String utf8(const char *p_text) {
	return p_text ? String::utf8(p_text) : String();
}

double now_ms() {
	return double(Time::get_singleton()->get_ticks_usec()) / 1000.0;
}

bool approx(float p_a, float p_b) {
	return std::fabs(p_a - p_b) < 1e-4f;
}

} // namespace

// ---------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------

HtmlView::HtmlView() {
	set_focus_mode(FOCUS_ALL);
	set_mouse_filter(MOUSE_FILTER_STOP);
	set_process(true);

	// The offscreen surface. Internal, so it is neither saved with the scene
	// nor visible in the scene tree.
	viewport = memnew(SubViewport);
	viewport->set_name("DoctypeSurface");
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(true);
	viewport->set_clear_mode(SubViewport::CLEAR_MODE_NEVER);
	viewport->set_update_mode(SubViewport::UPDATE_DISABLED);
	viewport->set_msaa_2d(Viewport::MSAA_DISABLED);
	viewport->set_snap_2d_transforms_to_pixel(false);
	viewport->set_default_canvas_item_texture_filter(Viewport::DEFAULT_CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
	viewport->set_handle_input_locally(false);
	viewport->set_disable_input(true);
	viewport->set_size(Vector2i(1, 1));
	add_child(viewport, false, Node::INTERNAL_MODE_FRONT);
	surface.attach(viewport);

	// The composite draws on a child canvas item with its own material, so
	// the control's material slot stays free for the user.
	composite_shader.instantiate();
	composite_shader->set_code(doctype_shaders::COMPOSITE);
	composite_material.instantiate();
	composite_material->set_shader(composite_shader);

	RenderingServer *rs = RenderingServer::get_singleton();
	display_item = rs->canvas_item_create();
	rs->canvas_item_set_parent(display_item, get_canvas_item());
	rs->canvas_item_set_material(display_item, composite_material->get_rid());
	rs->canvas_item_set_default_texture_filter(display_item, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);

	create_document();
}

HtmlView::~HtmlView() {
	destroy_document();
	surface.detach();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs && display_item.is_valid()) {
		rs->free_rid(display_item);
	}
}

void HtmlView::create_document() {
	LhuHostCallbacks host;
	std::memset(&host, 0, sizeof(host));
	host.user_data = this;
	host.get_image_size = &HtmlView::cb_get_image_size;
	host.load_image = &HtmlView::cb_load_image;
	host.get_image_uv = &HtmlView::cb_get_image_uv;
	host.import_css = &HtmlView::cb_import_css;
	host.on_anchor_click = &HtmlView::cb_anchor_click;
	host.on_element_click = &HtmlView::cb_element_click;
	host.on_set_cursor = &HtmlView::cb_set_cursor;

	ctx = lhu_create(&host);
	if (!ctx) {
		UtilityFunctions::push_error("[Doctype] lhu_create returned null");
		return;
	}

	lhu_set_master_css(ctx, int32_t(master_stylesheet));
	fonts_dirty = true;
	loaded_html = String();
	loaded_css = String();
	document_loaded = false;
	needs_reload = true;
	force_reload = true;
	surface.mark_stale();
}

void HtmlView::destroy_document() {
	if (ctx) {
		lhu_destroy(ctx);
		ctx = nullptr;
	}
}

void HtmlView::recreate_document() {
	destroy_document();
	create_document();
}

// ---------------------------------------------------------------------------
// fonts
// ---------------------------------------------------------------------------

bool HtmlView::register_font_bytes(const String &p_family, int p_weight, bool p_italic, const PackedByteArray &p_data) {
	if (!ctx || p_data.is_empty() || p_family.is_empty()) {
		return false;
	}
	CharString family = p_family.to_lower().utf8();
	return lhu_register_font(ctx, family.get_data(), p_weight, p_italic ? 1 : 0, p_data.ptr(), int32_t(p_data.size())) != 0;
}

int HtmlView::register_system_fonts() {
	static const char *sans_candidates[] = {
		"Arial", "Helvetica", "Helvetica Neue", "Roboto", "Noto Sans", "Segoe UI", "DejaVu Sans",
		"Liberation Sans", "Open Sans", "Ubuntu", "Cantarell", nullptr
	};
	static const char *mono_candidates[] = {
		"Menlo", "Courier New", "Consolas", "Roboto Mono", "Droid Sans Mono", "DejaVu Sans Mono",
		"Liberation Mono", "Noto Sans Mono", "Ubuntu Mono", nullptr
	};
	static const char *generic_sans[] = { "sans-serif", "serif", "system-ui", "arial", "helvetica", nullptr };
	static const char *generic_mono[] = { "monospace", "courier", "menlo", nullptr };

	OS *os = OS::get_singleton();
	int registered = 0;

	for (int i = 0; sans_candidates[i]; i++) {
		String regular = os->get_system_font_path(sans_candidates[i], 400, 100, false);
		if (regular.is_empty() || !FileAccess::file_exists(regular)) {
			continue;
		}
		PackedByteArray data = FileAccess::get_file_as_bytes(regular);
		if (data.is_empty()) {
			continue;
		}

		bool ok = false;
		for (int g = 0; generic_sans[g]; g++) {
			ok |= register_font_bytes(generic_sans[g], 400, false, data);
		}
		if (!ok) {
			continue;
		}
		register_font_bytes(sans_candidates[i], 400, false, data);
		registered++;

		String bold = os->get_system_font_path(sans_candidates[i], 700, 100, false);
		if (!bold.is_empty() && bold != regular && FileAccess::file_exists(bold)) {
			PackedByteArray bold_data = FileAccess::get_file_as_bytes(bold);
			for (int g = 0; generic_sans[g]; g++) {
				register_font_bytes(generic_sans[g], 700, false, bold_data);
			}
			register_font_bytes(sans_candidates[i], 700, false, bold_data);
		}

		String italic = os->get_system_font_path(sans_candidates[i], 400, 100, true);
		if (!italic.is_empty() && italic != regular && FileAccess::file_exists(italic)) {
			PackedByteArray italic_data = FileAccess::get_file_as_bytes(italic);
			register_font_bytes("sans-serif", 400, true, italic_data);
			register_font_bytes(sans_candidates[i], 400, true, italic_data);
		}
		break;
	}

	for (int i = 0; mono_candidates[i]; i++) {
		String path = os->get_system_font_path(mono_candidates[i], 400, 100, false);
		if (path.is_empty() || !FileAccess::file_exists(path)) {
			continue;
		}
		PackedByteArray data = FileAccess::get_file_as_bytes(path);
		bool ok = false;
		for (int g = 0; generic_mono[g]; g++) {
			ok |= register_font_bytes(generic_mono[g], 400, false, data);
		}
		if (ok) {
			register_font_bytes(mono_candidates[i], 400, false, data);
			registered++;
			break;
		}
	}

	return registered;
}

void HtmlView::register_fonts() {
	fonts_dirty = false;
	if (!ctx) {
		return;
	}

	int registered = 0;
	String primary_family;

	for (int i = 0; i < fonts.size(); i++) {
		Ref<FontFile> font = fonts[i];
		if (font.is_null()) {
			continue;
		}

		PackedByteArray data = font->get_data();
		if (data.is_empty()) {
			UtilityFunctions::push_warning(vformat("[Doctype] font '%s' carries no data; import it as a dynamic font", font->get_path()));
			continue;
		}

		String family = font->get_font_name().to_lower().strip_edges();
		if (family.is_empty()) {
			family = default_font_family.to_lower();
		}
		int weight = font->get_font_weight();
		if (weight <= 0) {
			weight = 400;
		}
		bool italic = (int64_t(font->get_font_style()) & int64_t(TextServer::FONT_ITALIC)) != 0;

		if (!register_font_bytes(family, weight, italic, data)) {
			UtilityFunctions::push_warning(vformat("[Doctype] could not parse font '%s'", font->get_path()));
			continue;
		}
		registered++;

		// Every face of the first family is also the default family, so a
		// page that says font-family:sans-serif gets the project's own font,
		// bold and italic included.
		if (primary_family.is_empty()) {
			primary_family = family;
		}
		if (family == primary_family && family != default_font_family.to_lower()) {
			register_font_bytes(default_font_family, weight, italic, data);
		}
	}

	if (registered == 0 && use_system_fonts) {
		registered = register_system_fonts();
	}

	if (registered == 0) {
		UtilityFunctions::push_error("[Doctype] no fonts registered. Add FontFile resources to 'fonts' or enable use_system_fonts on a platform that has them.");
		return;
	}

	CharString family = default_font_family.to_lower().utf8();
	lhu_set_default_font(ctx, family.get_data(), default_font_size);
}

// ---------------------------------------------------------------------------
// properties
// ---------------------------------------------------------------------------

void HtmlView::set_html(const String &p_html) {
	html = p_html;
	html_path = String();
	needs_reload = true;
}

void HtmlView::set_user_css(const String &p_css) {
	user_css = p_css;
	needs_reload = true;
}

void HtmlView::set_html_path(const String &p_path) {
	html_path = p_path;
	needs_reload = true;
	force_reload = true;
}

void HtmlView::load_html(const String &p_html, const String &p_user_css) {
	html = p_html;
	html_path = String();
	if (!p_user_css.is_empty()) {
		user_css = p_user_css;
	}
	needs_reload = true;
}

bool HtmlView::load_file(const String &p_path) {
	if (!FileAccess::file_exists(p_path)) {
		UtilityFunctions::push_error(vformat("[Doctype] file not found: %s", p_path));
		return false;
	}
	html = FileAccess::get_file_as_string(p_path);
	html_path = String();
	needs_reload = true;
	return true;
}

bool HtmlView::set_text(const String &p_selector, const String &p_text) {
	if (!ctx || p_selector.is_empty()) {
		return false;
	}
	CharString selector = p_selector.utf8();
	CharString text = p_text.utf8();
	if (lhu_set_text(ctx, selector.get_data(), text.get_data()) == 0) {
		return false;
	}
	parse_ms = 0.f;
	needs_layout = true;
	return true;
}

bool HtmlView::set_style(const String &p_selector, const String &p_css) {
	if (!ctx || p_selector.is_empty()) {
		return false;
	}
	CharString selector = p_selector.utf8();
	CharString css = p_css.utf8();
	if (lhu_set_style(ctx, selector.get_data(), css.get_data()) == 0) {
		return false;
	}
	parse_ms = 0.f;
	needs_layout = true;
	return true;
}

void HtmlView::mark_dirty() {
	needs_reload = true;
	force_reload = true;
}

void HtmlView::mark_layout_dirty() {
	needs_layout = true;
}

void HtmlView::set_background(const Color &p_color) {
	background = p_color;
	needs_render = true;
}

void HtmlView::set_match_control_size(bool p_enabled) {
	match_control_size = p_enabled;
	needs_layout = true;
}

void HtmlView::set_surface_size(const Vector2i &p_size) {
	surface_size = Vector2i(std::max(1, p_size.x), std::max(1, p_size.y));
	needs_layout = true;
}

void HtmlView::set_auto_height(bool p_enabled) {
	auto_height = p_enabled;
	needs_layout = true;
}

void HtmlView::set_match_content_scale(bool p_enabled) {
	match_content_scale = p_enabled;
	needs_layout = true;
}

void HtmlView::set_device_scale(float p_scale) {
	device_scale = std::clamp(p_scale, 0.5f, 8.f);
	needs_layout = true;
}

void HtmlView::set_render_scale(float p_scale) {
	render_scale = std::clamp(p_scale, 0.25f, 2.f);
	needs_layout = true;
}

void HtmlView::set_draw_surface(bool p_enabled) {
	draw_surface = p_enabled;
	queue_redraw();
}

Ref<Texture2D> HtmlView::get_texture() const {
	return surface.get_texture();
}

void HtmlView::set_fonts(const TypedArray<FontFile> &p_fonts) {
	fonts = p_fonts;
	fonts_dirty = true;
}

void HtmlView::set_use_system_fonts(bool p_enabled) {
	use_system_fonts = p_enabled;
	fonts_dirty = true;
}

void HtmlView::set_default_font_family(const String &p_family) {
	default_font_family = p_family.is_empty() ? String("sans-serif") : p_family;
	fonts_dirty = true;
}

void HtmlView::set_default_font_size(float p_size) {
	default_font_size = p_size > 0.f ? p_size : 16.f;
	fonts_dirty = true;
}

void HtmlView::set_master_stylesheet(MasterStylesheet p_sheet) {
	master_stylesheet = p_sheet;
	if (ctx) {
		lhu_set_master_css(ctx, int32_t(master_stylesheet));
	}
	needs_reload = true;
	force_reload = true;
}

void HtmlView::set_language(const String &p_language, const String &p_culture) {
	// Copied before assignment: a caller may pass this view's own members
	// back in, and godot-cpp's String does not survive self-assignment.
	String lang = p_language;
	String cult = p_culture;
	language = lang;
	culture = cult;
	needs_reload = true;
	force_reload = true;
}

void HtmlView::set_language_code(const String &p_language) {
	String lang = p_language;
	language = lang;
	needs_reload = true;
	force_reload = true;
}

void HtmlView::set_culture_code(const String &p_culture) {
	String cult = p_culture;
	culture = cult;
	needs_reload = true;
	force_reload = true;
}

void HtmlView::set_pass_through_empty_areas(bool p_enabled) {
	pass_through_empty_areas = p_enabled;
}

void HtmlView::set_drag_to_scroll(bool p_enabled) {
	drag_to_scroll = p_enabled;
}

void HtmlView::set_drag_items(bool p_enabled) {
	drag_items = p_enabled;
}

void HtmlView::set_scroll_step(float p_step) {
	scroll_step = p_step;
}

void HtmlView::register_image(const String &p_name, const Ref<Texture2D> &p_texture) {
	images.register_image(p_name, p_texture);
}

void HtmlView::set_max_atlas_size(int p_size) {
	images.set_max_atlas_size(p_size);
}

int HtmlView::get_max_atlas_size() const {
	return images.get_max_atlas_size();
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

Vector2 HtmlView::to_document_point(const Vector2 &p_local) const {
	Vector2 size = get_size();
	if (size.x <= 0.f || size.y <= 0.f) {
		return p_local;
	}
	return Vector2(p_local.x / size.x * css_viewport.x, p_local.y / size.y * css_viewport.y);
}

Vector2 HtmlView::from_document_point(const Vector2 &p_document) const {
	if (css_viewport.x <= 0.f || css_viewport.y <= 0.f) {
		return p_document;
	}
	Vector2 size = get_size();
	return Vector2(p_document.x / css_viewport.x * size.x, p_document.y / css_viewport.y * size.y);
}

String HtmlView::element_at(const Vector2 &p_document_point) const {
	if (!ctx) {
		return String();
	}
	char buffer[256];
	if (lhu_element_at(ctx, p_document_point.x, p_document_point.y, buffer, int32_t(sizeof(buffer))) == 0) {
		return String();
	}
	buffer[sizeof(buffer) - 1] = 0;
	return String::utf8(buffer);
}

Rect2 HtmlView::get_element_rect(const String &p_selector) const {
	if (!ctx || p_selector.is_empty()) {
		return Rect2();
	}
	CharString selector = p_selector.utf8();
	float x = 0, y = 0, w = 0, h = 0;
	if (lhu_element_rect(ctx, selector.get_data(), &x, &y, &w, &h) == 0) {
		return Rect2();
	}
	return Rect2(x, y, w, h);
}

bool HtmlView::has_element(const String &p_selector) const {
	if (!ctx || p_selector.is_empty()) {
		return false;
	}
	CharString selector = p_selector.utf8();
	return lhu_element_rect(ctx, selector.get_data(), nullptr, nullptr, nullptr, nullptr) != 0;
}

float HtmlView::get_document_width() const {
	return ctx ? lhu_doc_width(ctx) : 0.f;
}

float HtmlView::get_document_height() const {
	return ctx ? lhu_doc_height(ctx) : 0.f;
}

int HtmlView::get_quad_count() const {
	return surface.get_quad_count();
}

HtmlView::DirtyMode HtmlView::get_last_dirty_mode() const {
	return DirtyMode(surface.get_last_dirty_mode());
}

Rect2i HtmlView::get_last_dirty_pixels() const {
	return surface.get_last_dirty_pixels();
}

Vector2i HtmlView::get_font_atlas_size() const {
	return surface.get_font_atlas_size();
}

String HtmlView::get_last_error() const {
	return ctx ? utf8(lhu_last_error(ctx)) : String("no document");
}

Dictionary HtmlView::get_stats() const {
	Dictionary d;
	d["quads"] = surface.get_quad_count();
	d["parse_ms"] = parse_ms;
	d["layout_ms"] = layout_ms;
	d["draw_ms"] = draw_ms;
	d["record_ms"] = record_ms;
	d["total_ms"] = parse_ms + layout_ms + draw_ms;
	d["reloads"] = reload_count;
	d["skipped_reloads"] = skipped_reloads;
	d["layouts"] = layout_count;
	d["renders"] = render_count;
	d["last_dirty_mode"] = int(surface.get_last_dirty_mode());
	d["last_dirty_pixels"] = surface.get_last_dirty_pixels();
	d["uploaded_vertices"] = surface.get_last_uploaded_vertices();
	d["uploaded_kb_total"] = surface.get_uploaded_kb_total();
	d["font_atlas"] = surface.get_font_atlas_size();
	d["surface_pixels"] = pixel_size;
	d["device_scale"] = active_scale;
	d["document_height"] = get_document_height();
	if (ctx) {
		d["quads_replayed"] = int64_t(lhu_quadcache_stat(ctx, LHU_QC_QUADS_REPLAYED));
		d["quads_emitted"] = int64_t(lhu_quadcache_stat(ctx, LHU_QC_QUADS_EMITTED));
		d["frames_fast"] = int64_t(lhu_quadcache_stat(ctx, LHU_QC_FRAMES_FAST));
	}
	return d;
}

// ---------------------------------------------------------------------------
// frame loop
// ---------------------------------------------------------------------------

String HtmlView::current_html() const {
	if (!html_path.is_empty()) {
		if (FileAccess::file_exists(html_path)) {
			return FileAccess::get_file_as_string(html_path);
		}
		UtilityFunctions::push_error(vformat("[Doctype] html_path not found: %s", html_path));
	}
	return html;
}

void HtmlView::update_surface_size() {
	float scale = 1.f;
	if (match_content_scale) {
		if (is_inside_tree() && get_viewport()) {
			Transform2D xform = get_viewport()->get_final_transform() * get_global_transform_with_canvas();
			Vector2 s = xform.get_scale();
			scale = std::max(s.x, s.y);
		}
		if (!(scale > 0.f) || !std::isfinite(scale)) {
			scale = 1.f;
		}
	} else {
		scale = device_scale;
	}
	scale *= render_scale;

	Vector2i wanted;
	if (match_control_size) {
		Vector2 px = get_size() * scale;
		wanted = Vector2i(int(std::lround(px.x)), int(std::lround(px.y)));
	} else {
		wanted = surface_size;
	}
	wanted.x = std::max(1, wanted.x);
	wanted.y = std::max(1, wanted.y);

	if (wanted != pixel_size || !approx(scale, active_scale)) {
		pixel_size = wanted;
		active_scale = scale;
		needs_layout = true;
	}
}

void HtmlView::_process(double p_delta) {
	if (!ctx) {
		return;
	}

	if (fonts_dirty) {
		// Font registration is baked into the document litehtml built, so a
		// changed font list means a fresh context and a re-parse.
		if (document_loaded) {
			recreate_document();
		}
		register_fonts();
	}

	// Newly-arrived images change layout, so watch the provider too; and a
	// repack moves UVs baked into native's retained display list, which a
	// relayout alone would not notice.
	int version = images.get_version();
	if (version != resource_version) {
		resource_version = version;
		needs_layout = true;
		lhu_quadcache_stat(ctx, LHU_QC_INVALIDATE);
	}

	update_surface_size();

	if (pixel_size != laid_out_for || !approx(active_scale, laid_out_scale)) {
		needs_layout = true;
	}

	if (needs_reload) {
		run_reload();
	}
	if (needs_layout) {
		run_layout();
	}
	if (needs_render) {
		run_render();
	}
}

void HtmlView::run_reload() {
	needs_reload = false;

	String markup = current_html();
	String css = user_css;

	// Identical markup produces an identical document, so skipping the parse
	// is not just faster: it also keeps hover, active and scroll state that a
	// fresh parse would throw away.
	if (!force_reload && document_loaded && markup == loaded_html && css == loaded_css) {
		skipped_reloads++;
		parse_ms = 0.f;
		return;
	}
	force_reload = false;

	CharString lang = language.utf8();
	CharString cult = culture.utf8();
	lhu_set_language(ctx, language.is_empty() ? nullptr : lang.get_data(), culture.is_empty() ? nullptr : cult.get_data());

	CharString markup_utf8 = markup.utf8();
	CharString css_utf8 = css.utf8();

	double t0 = now_ms();
	int32_t ok = lhu_load_html(ctx, markup_utf8.get_data(), css.is_empty() ? nullptr : css_utf8.get_data());
	parse_ms = float(now_ms() - t0);
	reload_count++;

	if (!ok) {
		UtilityFunctions::push_error(vformat("[Doctype] %s", utf8(lhu_last_error(ctx))));
		return;
	}

	loaded_html = markup;
	loaded_css = css;
	document_loaded = true;
	document_reparsed = true;
	needs_layout = true;

	emit_signal("document_loaded");
}

float HtmlView::measured_layout(float p_width) {
	double t0 = now_ms();
	float height = lhu_layout(ctx, p_width);
	layout_ms = float(now_ms() - t0);
	layout_count++;
	return height;
}

int HtmlView::restore_pointer_state() {
	if (!has_pointer) {
		return 0;
	}
	begin_dispatch();
	int dirty = lhu_mouse_move(ctx, last_pointer.x, last_pointer.y);
	if (pointer_is_down) {
		dirty |= lhu_mouse_down(ctx, last_pointer.x, last_pointer.y);
	}
	end_dispatch();
	return dirty;
}

void HtmlView::run_layout() {
	needs_layout = false;

	float width = float(pixel_size.x) / active_scale;
	float height = float(pixel_size.y) / active_scale;
	css_viewport = Vector2(width, height);

	lhu_set_device_scale(ctx, active_scale);
	lhu_set_viewport(ctx, width, height);

	float document_height = measured_layout(width);

	// Restoring the pointer can itself dirty layout: the re-applied :hover
	// may resize what it lands on. Hit testing needs the laid-out tree, hence
	// after the first pass.
	if (document_reparsed) {
		document_reparsed = false;
		if (restore_pointer_state() & LHU_DIRTY_LAYOUT) {
			document_height = measured_layout(width);
		}
	}

	if (auto_height) {
		if (match_control_size) {
			// Grow the control to its content; the surface follows the rect.
			float wanted = std::ceil(document_height);
			Vector2 minimum = get_custom_minimum_size();
			if (!approx(minimum.y, wanted)) {
				set_custom_minimum_size(Vector2(minimum.x, wanted));
			}
		} else {
			int wanted = std::max(1, int(std::ceil(document_height * active_scale)));
			if (wanted != pixel_size.y) {
				pixel_size.y = wanted;
				surface_size.y = wanted;
			}
		}
	}

	laid_out_for = pixel_size;
	laid_out_scale = active_scale;
	needs_render = true;
}

void HtmlView::run_render() {
	needs_render = false;

	// A recreated target holds garbage where partial repaint expects the
	// previous frame, and a changed background falsifies every pixel outside
	// the dirty rect; each forces one full repaint.
	bool force_full = surface.resize(pixel_size);
	if (background != last_background) {
		last_background = background;
		force_full = true;
	}

	double t0 = now_ms();

	LhuFrame frame;
	std::memset(&frame, 0, sizeof(frame));
	lhu_record(ctx, &frame);

	double t1 = now_ms();
	record_ms = float(t1 - t0);

	surface.render(frame, background, css_viewport, images.get_atlas(), force_full);

	draw_ms = float(now_ms() - t0);
	render_count++;

	if (force_full) {
		queue_redraw();
	}
}

void HtmlView::update_display() {
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->canvas_item_clear(display_item);
	if (!draw_surface) {
		return;
	}
	RID texture = surface.get_texture_rid();
	if (!texture.is_valid()) {
		return;
	}
	rs->canvas_item_add_texture_rect(display_item, Rect2(Vector2(), get_size()), texture, false, Color(1, 1, 1, 1), false);
}

// ---------------------------------------------------------------------------
// notifications
// ---------------------------------------------------------------------------

void HtmlView::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_DRAW: {
			update_display();
		} break;

		case NOTIFICATION_RESIZED: {
			needs_layout = true;
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible_in_tree()) {
				needs_render = true;
			}
		} break;

		case NOTIFICATION_MOUSE_EXIT: {
			pointer_exit();
		} break;

		case NOTIFICATION_FOCUS_ENTER: {
			// Focus gained by a click keeps the pointer's :hover as the only
			// highlight; focus gained by keyboard or gamepad lands on the
			// remembered element, or enters at the top-left focusable.
			Input *input = Input::get_singleton();
			bool by_click = input && input->is_mouse_button_pressed(MOUSE_BUTTON_LEFT);
			if (!by_click && ctx) {
				if (has_remembered_focus && !remembered_focus.is_empty() && has_element("#" + remembered_focus)) {
					set_document_focus("#" + remembered_focus);
				} else if (get_focused_id().is_empty()) {
					move_focus(NAV_DOWN);
				}
			}
		} break;

		case NOTIFICATION_FOCUS_EXIT: {
			if (ctx) {
				String id = get_focused_id();
				has_remembered_focus = !id.is_empty();
				remembered_focus = id;
				set_document_focus(String());
			}
		} break;
	}
}

// ---------------------------------------------------------------------------
// input
// ---------------------------------------------------------------------------

void HtmlView::apply_input_dirty(int p_dirty) {
	if (p_dirty <= 0) {
		return;
	}
	needs_render = true;
	if (p_dirty & LHU_DIRTY_LAYOUT) {
		needs_layout = true;
	}
}

void HtmlView::pointer_move(const Vector2 &p_document_point) {
	last_pointer = p_document_point;
	has_pointer = true;
	if (!ctx) {
		return;
	}
	begin_dispatch();
	int dirty = lhu_mouse_move(ctx, p_document_point.x, p_document_point.y);
	end_dispatch();
	apply_input_dirty(dirty);
}

void HtmlView::pointer_down(const Vector2 &p_document_point) {
	last_pointer = p_document_point;
	has_pointer = true;
	pointer_is_down = true;
	drag_remainder = Vector2();
	if (!ctx) {
		return;
	}
	begin_dispatch();
	int dirty = lhu_mouse_down(ctx, p_document_point.x, p_document_point.y);
	end_dispatch();
	apply_input_dirty(dirty);
}

void HtmlView::pointer_up(const Vector2 &p_document_point) {
	last_pointer = p_document_point;
	has_pointer = true;
	pointer_is_down = false;
	if (!ctx) {
		return;
	}
	begin_dispatch();
	int dirty = lhu_mouse_up(ctx, p_document_point.x, p_document_point.y);
	end_dispatch();
	apply_input_dirty(dirty);
}

void HtmlView::pointer_exit() {
	has_pointer = false;
	pointer_is_down = false;
	if (!ctx) {
		return;
	}
	begin_dispatch();
	int dirty = lhu_mouse_leave(ctx);
	end_dispatch();
	apply_input_dirty(dirty);
}

bool HtmlView::scroll_by(const Vector2 &p_delta, const Vector2 &p_document_point) {
	if (!ctx) {
		return false;
	}
	begin_dispatch();
	int consumed = lhu_scroll(ctx, p_delta.x, p_delta.y, p_document_point.x, p_document_point.y);
	end_dispatch();
	if (consumed > 0) {
		needs_render = true;
		return true;
	}
	return false;
}

void HtmlView::handle_pointer_motion(const Vector2 &p_local, const Vector2 &p_relative, bool p_left_down) {
	Vector2 now = to_document_point(p_local);

	if (p_left_down && pointer_is_down && drag_to_scroll) {
		Vector2 before = to_document_point(p_local - p_relative);
		Vector2 moved = now - before;

		// Quantize the drag to whole document pixels and carry the fraction
		// to the next event, so the engine's scroll fast path sees whole
		// pixel translations.
		drag_remainder += moved;
		Vector2 step(std::round(drag_remainder.x), std::round(drag_remainder.y));
		drag_remainder -= step;

		if (step != Vector2()) {
			// Dragging the content down reveals what is above it, which is a
			// negative scroll.
			if (scroll_by(Vector2(-step.x, -step.y), now)) {
				accept_event();
			}
		}
	}

	pointer_move(now);
}

void HtmlView::_gui_input(const Ref<InputEvent> &p_event) {
	if (!ctx || p_event.is_null()) {
		return;
	}

	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid()) {
		bool left = motion->get_button_mask().has_flag(MOUSE_BUTTON_MASK_LEFT);
		handle_pointer_motion(motion->get_position(), motion->get_relative(), left);
		return;
	}

	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid()) {
		Vector2 doc = to_document_point(button->get_position());
		switch (button->get_button_index()) {
			case MOUSE_BUTTON_LEFT: {
				if (button->is_pressed()) {
					pointer_down(doc);
				} else {
					pointer_up(doc);
				}
				accept_event();
			} break;

			case MOUSE_BUTTON_WHEEL_UP:
			case MOUSE_BUTTON_WHEEL_DOWN:
			case MOUSE_BUTTON_WHEEL_LEFT:
			case MOUSE_BUTTON_WHEEL_RIGHT: {
				if (!button->is_pressed()) {
					return;
				}
				float factor = button->get_factor();
				if (factor <= 0.f) {
					factor = 1.f;
				}
				Vector2 delta;
				MouseButton index = button->get_button_index();
				if (index == MOUSE_BUTTON_WHEEL_UP) {
					delta.y = -scroll_step * factor;
				} else if (index == MOUSE_BUTTON_WHEEL_DOWN) {
					delta.y = scroll_step * factor;
				} else if (index == MOUSE_BUTTON_WHEEL_LEFT) {
					delta.x = -scroll_step * factor;
				} else {
					delta.x = scroll_step * factor;
				}
				if (scroll_by(delta, doc)) {
					accept_event();
				}
			} break;

			default:
				break;
		}
		return;
	}

	Ref<InputEventPanGesture> pan = p_event;
	if (pan.is_valid()) {
		Vector2 doc = to_document_point(pan->get_position());
		Vector2 delta = pan->get_delta() * scroll_step;
		if (scroll_by(delta, doc)) {
			accept_event();
		}
		return;
	}

	// Raw touches only when they are not already arriving as emulated mouse
	// events, or every tap would be handled twice.
	Input *input = Input::get_singleton();
	bool emulated = input && input->is_emulating_mouse_from_touch();

	Ref<InputEventScreenTouch> touch = p_event;
	if (touch.is_valid()) {
		if (emulated || touch->get_index() != 0) {
			return;
		}
		Vector2 doc = to_document_point(touch->get_position());
		if (touch->is_pressed()) {
			pointer_down(doc);
		} else {
			pointer_up(doc);
		}
		accept_event();
		return;
	}

	Ref<InputEventScreenDrag> drag = p_event;
	if (drag.is_valid()) {
		if (emulated || drag->get_index() != 0) {
			return;
		}
		handle_pointer_motion(drag->get_position(), drag->get_relative(), true);
		return;
	}

	// Keyboard / gamepad: only reaches here while this control has focus.
	if (!has_focus()) {
		return;
	}

	struct { const char *action; NavDirection direction; } navs[] = {
		{ "ui_up", NAV_UP }, { "ui_right", NAV_RIGHT }, { "ui_down", NAV_DOWN }, { "ui_left", NAV_LEFT },
	};
	for (const auto &nav : navs) {
		if (p_event->is_action_pressed(nav.action, false, false)) {
			// Nowhere to go inside the page: leave the event alone so Godot
			// hands focus to the neighbouring control.
			if (move_focus(nav.direction)) {
				accept_event();
			}
			return;
		}
	}

	if (p_event->is_action_pressed("ui_accept", false, false)) {
		if (activate(String())) {
			accept_event();
		}
	}
}

bool HtmlView::_has_point(const Vector2 &p_point) const {
	if (!Rect2(Vector2(), get_size()).has_point(p_point)) {
		return false;
	}
	if (!pass_through_empty_areas || !ctx) {
		return true;
	}
	// Only elements carrying an id catch input, so a HUD stretched over the
	// whole screen does not swallow the game everywhere it is transparent.
	return !element_at(to_document_point(p_point)).is_empty();
}

Variant HtmlView::_get_drag_data(const Vector2 &p_at_position) {
	if (!drag_items || !ctx) {
		return Variant();
	}
	Vector2 doc = to_document_point(p_at_position);
	String id = element_at(doc);
	if (id.is_empty()) {
		return Variant();
	}
	Dictionary data;
	data["type"] = "doctype";
	data["view"] = this;
	data["element_id"] = id;
	data["document_point"] = doc;
	emit_signal("item_drag_started", id, doc);
	return data;
}

bool HtmlView::_can_drop_data(const Vector2 &p_at_position, const Variant &p_data) const {
	if (!ctx || p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	Dictionary data = p_data;
	if (!data.has("type") || String(data["type"]) != "doctype") {
		return false;
	}
	return !element_at(to_document_point(p_at_position)).is_empty();
}

void HtmlView::_drop_data(const Vector2 &p_at_position, const Variant &p_data) {
	if (!ctx) {
		return;
	}
	Vector2 doc = to_document_point(p_at_position);
	String target = element_at(doc);
	emit_signal("item_dropped", p_data, target, doc);
}

// ---------------------------------------------------------------------------
// focus
// ---------------------------------------------------------------------------

void HtmlView::set_document_focus(const String &p_selector) {
	if (!ctx) {
		return;
	}
	CharString selector = p_selector.utf8();
	begin_dispatch();
	int dirty = lhu_set_focus(ctx, p_selector.is_empty() ? nullptr : selector.get_data());
	end_dispatch();
	apply_input_dirty(dirty);
}

bool HtmlView::move_focus(NavDirection p_direction) {
	if (!ctx) {
		return false;
	}
	begin_dispatch();
	int result = lhu_focus_move(ctx, int32_t(p_direction));
	end_dispatch();
	if (result < 0) {
		return false;
	}
	apply_input_dirty(result);
	return true;
}

bool HtmlView::activate(const String &p_selector) {
	if (!ctx) {
		return false;
	}
	CharString selector = p_selector.utf8();
	begin_dispatch();
	int result = lhu_activate(ctx, p_selector.is_empty() ? nullptr : selector.get_data());
	end_dispatch();
	if (result == 1) {
		needs_render = true;
		return true;
	}
	return false;
}

String HtmlView::get_focused_id() const {
	if (!ctx) {
		return String();
	}
	char buffer[256];
	if (lhu_focused_id(ctx, buffer, int32_t(sizeof(buffer))) == 0) {
		return String();
	}
	buffer[sizeof(buffer) - 1] = 0;
	return String::utf8(buffer);
}

// ---------------------------------------------------------------------------
// clicks
// ---------------------------------------------------------------------------

void HtmlView::bind_click(const String &p_element_id, const Callable &p_handler) {
	if (p_element_id.is_empty()) {
		return;
	}
	std::string key = p_element_id.utf8().get_data();
	if (!p_handler.is_valid()) {
		click_bindings.erase(key);
	} else {
		click_bindings[key] = p_handler;
	}
}

void HtmlView::unbind_click(const String &p_element_id) {
	click_bindings.erase(p_element_id.utf8().get_data());
}

void HtmlView::clear_click_bindings() {
	click_bindings.clear();
}

// ---------------------------------------------------------------------------
// deferred events
// ---------------------------------------------------------------------------
//
// User-facing signals are queued by the native callbacks and raised on the
// way out of the native call, never while native frames are on the stack. A
// handler that loads the next page therefore runs with the previous dispatch
// fully unwound.

void HtmlView::end_dispatch() {
	if (--dispatch_depth != 0 || pending_events.empty()) {
		return;
	}
	std::vector<PendingEvent> run;
	run.swap(pending_events);
	for (const PendingEvent &event : run) {
		raise_event(event);
	}
}

void HtmlView::queue_event(const PendingEvent &p_event) {
	if (dispatch_depth > 0) {
		pending_events.push_back(p_event);
	} else {
		raise_event(p_event);
	}
}

void HtmlView::raise_event(const PendingEvent &p_event) {
	switch (p_event.kind) {
		case PendingEvent::ANCHOR: {
			emit_signal("anchor_clicked", p_event.a);
		} break;

		case PendingEvent::ELEMENT: {
			emit_signal("element_clicked", p_event.a, p_event.b, p_event.c, p_event.d);
			if (p_event.consumed) {
				auto it = click_bindings.find(p_event.a.utf8().get_data());
				if (it != click_bindings.end() && it->second.is_valid()) {
					Dictionary click;
					click["id"] = p_event.a;
					click["tag"] = p_event.b;
					click["class_names"] = p_event.c;
					click["action"] = p_event.d;
					it->second.call(click);
				}
			}
		} break;

		case PendingEvent::CURSOR: {
			apply_cursor(p_event.a);
			emit_signal("cursor_changed", p_event.a);
		} break;
	}
}

void HtmlView::apply_cursor(const String &p_cursor) {
	CursorShape shape = CURSOR_ARROW;
	if (p_cursor == "pointer") {
		shape = CURSOR_POINTING_HAND;
	} else if (p_cursor == "text") {
		shape = CURSOR_IBEAM;
	} else if (p_cursor == "move" || p_cursor == "all-scroll") {
		shape = CURSOR_MOVE;
	} else if (p_cursor == "crosshair") {
		shape = CURSOR_CROSS;
	} else if (p_cursor == "wait") {
		shape = CURSOR_WAIT;
	} else if (p_cursor == "progress") {
		shape = CURSOR_BUSY;
	} else if (p_cursor == "grab" || p_cursor == "grabbing") {
		shape = CURSOR_DRAG;
	} else if (p_cursor == "not-allowed" || p_cursor == "no-drop") {
		shape = CURSOR_FORBIDDEN;
	} else if (p_cursor == "help") {
		shape = CURSOR_HELP;
	} else if (p_cursor == "ew-resize" || p_cursor == "col-resize" || p_cursor == "e-resize" || p_cursor == "w-resize") {
		shape = CURSOR_HSIZE;
	} else if (p_cursor == "ns-resize" || p_cursor == "row-resize" || p_cursor == "n-resize" || p_cursor == "s-resize") {
		shape = CURSOR_VSIZE;
	} else if (p_cursor == "nwse-resize" || p_cursor == "nw-resize" || p_cursor == "se-resize") {
		shape = CURSOR_FDIAGSIZE;
	} else if (p_cursor == "nesw-resize" || p_cursor == "ne-resize" || p_cursor == "sw-resize") {
		shape = CURSOR_BDIAGSIZE;
	}
	set_default_cursor_shape(shape);
}

// ---------------------------------------------------------------------------
// native callbacks
// ---------------------------------------------------------------------------

void HtmlView::cb_get_image_size(void *p_user, const char *p_url, int32_t *r_w, int32_t *r_h) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	int w = 0, h = 0;
	if (view) {
		view->images.get_image_size(p_url, w, h);
	}
	if (r_w) {
		*r_w = w;
	}
	if (r_h) {
		*r_h = h;
	}
}

void HtmlView::cb_load_image(void *p_user, const char *p_url) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	if (view) {
		view->images.begin_load_image(p_url);
	}
}

int32_t HtmlView::cb_get_image_uv(void *p_user, const char *p_url, float *r_uv4) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	return (view && view->images.get_image_uv(p_url, r_uv4)) ? 1 : 0;
}

const char *HtmlView::cb_import_css(void *p_user, const char *p_url, const char *p_base) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	if (!view || !p_url || !*p_url) {
		return nullptr;
	}

	String url = utf8(p_url);
	String base = utf8(p_base);

	String candidates[3] = { url, String(), String() };
	if (!url.contains("://")) {
		candidates[1] = "res://" + url;
		if (!base.is_empty()) {
			candidates[2] = base.get_base_dir().path_join(url);
		}
	}

	for (const String &path : candidates) {
		if (path.is_empty() || !FileAccess::file_exists(path)) {
			continue;
		}
		// Native copies the string immediately; one buffer per view is enough.
		view->css_buffer = FileAccess::get_file_as_string(path).utf8().get_data();
		return view->css_buffer.c_str();
	}
	return nullptr;
}

void HtmlView::cb_anchor_click(void *p_user, const char *p_url) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	if (!view) {
		return;
	}
	PendingEvent event;
	event.kind = PendingEvent::ANCHOR;
	event.a = utf8(p_url);
	view->queue_event(event);
}

int32_t HtmlView::cb_element_click(void *p_user, const char *p_id, const char *p_tag, const char *p_classes,
		const char *p_action) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	if (!view) {
		return 0;
	}
	PendingEvent event;
	event.kind = PendingEvent::ELEMENT;
	event.a = utf8(p_id);
	event.b = utf8(p_tag);
	event.c = utf8(p_classes);
	event.d = utf8(p_action);

	// The consumption answer must be synchronous (litehtml keeps bubbling on
	// 0), but it is derived from the bindings alone, no user code. Only an id
	// binding claims the click; everything else keeps bubbling, which is what
	// lets a click on a button's label text reach the button itself.
	event.consumed = p_id && *p_id && view->click_bindings.count(p_id) > 0;
	view->queue_event(event);
	return event.consumed ? 1 : 0;
}

void HtmlView::cb_set_cursor(void *p_user, const char *p_cursor) {
	HtmlView *view = static_cast<HtmlView *>(p_user);
	if (!view) {
		return;
	}
	PendingEvent event;
	event.kind = PendingEvent::CURSOR;
	event.a = utf8(p_cursor);
	view->queue_event(event);
}

// ---------------------------------------------------------------------------
// bindings
// ---------------------------------------------------------------------------

void HtmlView::_bind_methods() {
	// content
	ClassDB::bind_method(D_METHOD("set_html", "html"), &HtmlView::set_html);
	ClassDB::bind_method(D_METHOD("get_html"), &HtmlView::get_html);
	ClassDB::bind_method(D_METHOD("set_user_css", "css"), &HtmlView::set_user_css);
	ClassDB::bind_method(D_METHOD("get_user_css"), &HtmlView::get_user_css);
	ClassDB::bind_method(D_METHOD("set_html_path", "path"), &HtmlView::set_html_path);
	ClassDB::bind_method(D_METHOD("get_html_path"), &HtmlView::get_html_path);
	ClassDB::bind_method(D_METHOD("load_html", "html", "user_css"), &HtmlView::load_html, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("load_file", "path"), &HtmlView::load_file);
	ClassDB::bind_method(D_METHOD("set_text", "selector", "text"), &HtmlView::set_text);
	ClassDB::bind_method(D_METHOD("set_style", "selector", "css"), &HtmlView::set_style);
	ClassDB::bind_method(D_METHOD("mark_dirty"), &HtmlView::mark_dirty);
	ClassDB::bind_method(D_METHOD("mark_layout_dirty"), &HtmlView::mark_layout_dirty);

	// surface
	ClassDB::bind_method(D_METHOD("set_background", "color"), &HtmlView::set_background);
	ClassDB::bind_method(D_METHOD("get_background"), &HtmlView::get_background);
	ClassDB::bind_method(D_METHOD("set_match_control_size", "enabled"), &HtmlView::set_match_control_size);
	ClassDB::bind_method(D_METHOD("get_match_control_size"), &HtmlView::get_match_control_size);
	ClassDB::bind_method(D_METHOD("set_surface_size", "size"), &HtmlView::set_surface_size);
	ClassDB::bind_method(D_METHOD("get_surface_size"), &HtmlView::get_surface_size);
	ClassDB::bind_method(D_METHOD("set_auto_height", "enabled"), &HtmlView::set_auto_height);
	ClassDB::bind_method(D_METHOD("get_auto_height"), &HtmlView::get_auto_height);
	ClassDB::bind_method(D_METHOD("set_match_content_scale", "enabled"), &HtmlView::set_match_content_scale);
	ClassDB::bind_method(D_METHOD("get_match_content_scale"), &HtmlView::get_match_content_scale);
	ClassDB::bind_method(D_METHOD("set_device_scale", "scale"), &HtmlView::set_device_scale);
	ClassDB::bind_method(D_METHOD("get_device_scale"), &HtmlView::get_device_scale);
	ClassDB::bind_method(D_METHOD("set_render_scale", "scale"), &HtmlView::set_render_scale);
	ClassDB::bind_method(D_METHOD("get_render_scale"), &HtmlView::get_render_scale);
	ClassDB::bind_method(D_METHOD("set_draw_surface", "enabled"), &HtmlView::set_draw_surface);
	ClassDB::bind_method(D_METHOD("get_draw_surface"), &HtmlView::get_draw_surface);
	ClassDB::bind_method(D_METHOD("get_effective_device_scale"), &HtmlView::get_effective_device_scale);
	ClassDB::bind_method(D_METHOD("get_surface_pixels"), &HtmlView::get_surface_pixels);
	ClassDB::bind_method(D_METHOD("get_texture"), &HtmlView::get_texture);

	// fonts
	ClassDB::bind_method(D_METHOD("set_fonts", "fonts"), &HtmlView::set_fonts);
	ClassDB::bind_method(D_METHOD("get_fonts"), &HtmlView::get_fonts);
	ClassDB::bind_method(D_METHOD("set_use_system_fonts", "enabled"), &HtmlView::set_use_system_fonts);
	ClassDB::bind_method(D_METHOD("get_use_system_fonts"), &HtmlView::get_use_system_fonts);
	ClassDB::bind_method(D_METHOD("set_default_font_family", "family"), &HtmlView::set_default_font_family);
	ClassDB::bind_method(D_METHOD("get_default_font_family"), &HtmlView::get_default_font_family);
	ClassDB::bind_method(D_METHOD("set_default_font_size", "size"), &HtmlView::set_default_font_size);
	ClassDB::bind_method(D_METHOD("get_default_font_size"), &HtmlView::get_default_font_size);

	// document
	ClassDB::bind_method(D_METHOD("set_master_stylesheet", "sheet"), &HtmlView::set_master_stylesheet);
	ClassDB::bind_method(D_METHOD("get_master_stylesheet"), &HtmlView::get_master_stylesheet);
	ClassDB::bind_method(D_METHOD("set_language", "language", "culture"), &HtmlView::set_language, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_language"), &HtmlView::get_language);
	ClassDB::bind_method(D_METHOD("get_culture"), &HtmlView::get_culture);
	ClassDB::bind_method(D_METHOD("set_language_code", "language"), &HtmlView::set_language_code);
	ClassDB::bind_method(D_METHOD("set_culture_code", "culture"), &HtmlView::set_culture_code);

	// input
	ClassDB::bind_method(D_METHOD("set_pass_through_empty_areas", "enabled"), &HtmlView::set_pass_through_empty_areas);
	ClassDB::bind_method(D_METHOD("get_pass_through_empty_areas"), &HtmlView::get_pass_through_empty_areas);
	ClassDB::bind_method(D_METHOD("set_drag_to_scroll", "enabled"), &HtmlView::set_drag_to_scroll);
	ClassDB::bind_method(D_METHOD("get_drag_to_scroll"), &HtmlView::get_drag_to_scroll);
	ClassDB::bind_method(D_METHOD("set_drag_items", "enabled"), &HtmlView::set_drag_items);
	ClassDB::bind_method(D_METHOD("get_drag_items"), &HtmlView::get_drag_items);
	ClassDB::bind_method(D_METHOD("set_scroll_step", "pixels"), &HtmlView::set_scroll_step);
	ClassDB::bind_method(D_METHOD("get_scroll_step"), &HtmlView::get_scroll_step);
	ClassDB::bind_method(D_METHOD("to_document_point", "local_point"), &HtmlView::to_document_point);
	ClassDB::bind_method(D_METHOD("from_document_point", "document_point"), &HtmlView::from_document_point);
	ClassDB::bind_method(D_METHOD("element_at", "document_point"), &HtmlView::element_at);
	ClassDB::bind_method(D_METHOD("get_element_rect", "selector"), &HtmlView::get_element_rect);
	ClassDB::bind_method(D_METHOD("has_element", "selector"), &HtmlView::has_element);
	ClassDB::bind_method(D_METHOD("pointer_move", "document_point"), &HtmlView::pointer_move);
	ClassDB::bind_method(D_METHOD("pointer_down", "document_point"), &HtmlView::pointer_down);
	ClassDB::bind_method(D_METHOD("pointer_up", "document_point"), &HtmlView::pointer_up);
	ClassDB::bind_method(D_METHOD("pointer_exit"), &HtmlView::pointer_exit);
	ClassDB::bind_method(D_METHOD("scroll_by", "delta", "document_point"), &HtmlView::scroll_by);

	// focus
	ClassDB::bind_method(D_METHOD("set_document_focus", "selector"), &HtmlView::set_document_focus);
	ClassDB::bind_method(D_METHOD("move_focus", "direction"), &HtmlView::move_focus);
	ClassDB::bind_method(D_METHOD("activate", "selector"), &HtmlView::activate, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_focused_id"), &HtmlView::get_focused_id);

	// clicks
	ClassDB::bind_method(D_METHOD("bind_click", "element_id", "handler"), &HtmlView::bind_click);
	ClassDB::bind_method(D_METHOD("unbind_click", "element_id"), &HtmlView::unbind_click);
	ClassDB::bind_method(D_METHOD("clear_click_bindings"), &HtmlView::clear_click_bindings);

	// images
	ClassDB::bind_method(D_METHOD("register_image", "name", "texture"), &HtmlView::register_image);
	ClassDB::bind_method(D_METHOD("set_max_atlas_size", "size"), &HtmlView::set_max_atlas_size);
	ClassDB::bind_method(D_METHOD("get_max_atlas_size"), &HtmlView::get_max_atlas_size);

	// stats
	ClassDB::bind_method(D_METHOD("get_document_width"), &HtmlView::get_document_width);
	ClassDB::bind_method(D_METHOD("get_document_height"), &HtmlView::get_document_height);
	ClassDB::bind_method(D_METHOD("get_quad_count"), &HtmlView::get_quad_count);
	ClassDB::bind_method(D_METHOD("get_parse_ms"), &HtmlView::get_parse_ms);
	ClassDB::bind_method(D_METHOD("get_layout_ms"), &HtmlView::get_layout_ms);
	ClassDB::bind_method(D_METHOD("get_draw_ms"), &HtmlView::get_draw_ms);
	ClassDB::bind_method(D_METHOD("get_record_ms"), &HtmlView::get_record_ms);
	ClassDB::bind_method(D_METHOD("get_last_dirty_mode"), &HtmlView::get_last_dirty_mode);
	ClassDB::bind_method(D_METHOD("get_last_dirty_pixels"), &HtmlView::get_last_dirty_pixels);
	ClassDB::bind_method(D_METHOD("get_font_atlas_size"), &HtmlView::get_font_atlas_size);
	ClassDB::bind_method(D_METHOD("get_last_error"), &HtmlView::get_last_error);
	ClassDB::bind_method(D_METHOD("get_stats"), &HtmlView::get_stats);

	// properties
	ADD_GROUP("Content", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "html", PROPERTY_HINT_MULTILINE_TEXT), "set_html", "get_html");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "user_css", PROPERTY_HINT_MULTILINE_TEXT), "set_user_css", "get_user_css");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "html_path", PROPERTY_HINT_FILE, "*.html,*.htm"), "set_html_path", "get_html_path");

	ADD_GROUP("Surface", "");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "background"), "set_background", "get_background");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "match_control_size"), "set_match_control_size", "get_match_control_size");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "surface_size", PROPERTY_HINT_NONE, "suffix:px"), "set_surface_size", "get_surface_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_height"), "set_auto_height", "get_auto_height");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "match_content_scale"), "set_match_content_scale", "get_match_content_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "device_scale", PROPERTY_HINT_RANGE, "0.5,8,0.01"), "set_device_scale", "get_device_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "render_scale", PROPERTY_HINT_RANGE, "0.25,2,0.01"), "set_render_scale", "get_render_scale");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "draw_surface"), "set_draw_surface", "get_draw_surface");

	ADD_GROUP("Fonts", "");
	String font_hint = vformat("%d/%d:FontFile", int(Variant::OBJECT), int(PROPERTY_HINT_RESOURCE_TYPE));
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "fonts", PROPERTY_HINT_ARRAY_TYPE, font_hint), "set_fonts", "get_fonts");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_system_fonts"), "set_use_system_fonts", "get_use_system_fonts");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "default_font_family"), "set_default_font_family", "get_default_font_family");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "default_font_size", PROPERTY_HINT_RANGE, "4,128,0.5,suffix:px"), "set_default_font_size", "get_default_font_size");

	ADD_GROUP("Document", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "master_stylesheet", PROPERTY_HINT_ENUM, "Game UI,Full"), "set_master_stylesheet", "get_master_stylesheet");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "language"), "set_language_code", "get_language");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "culture"), "set_culture_code", "get_culture");

	ADD_GROUP("Input", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pass_through_empty_areas"), "set_pass_through_empty_areas", "get_pass_through_empty_areas");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "drag_to_scroll"), "set_drag_to_scroll", "get_drag_to_scroll");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "drag_items"), "set_drag_items", "get_drag_items");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scroll_step", PROPERTY_HINT_RANGE, "1,200,1,suffix:px"), "set_scroll_step", "get_scroll_step");

	ADD_GROUP("Images", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_atlas_size", PROPERTY_HINT_RANGE, "256,16384,1,suffix:px"), "set_max_atlas_size", "get_max_atlas_size");

	// signals
	ADD_SIGNAL(MethodInfo("document_loaded"));
	ADD_SIGNAL(MethodInfo("anchor_clicked", PropertyInfo(Variant::STRING, "url")));
	ADD_SIGNAL(MethodInfo("element_clicked", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::STRING, "tag"),
			PropertyInfo(Variant::STRING, "class_names"), PropertyInfo(Variant::STRING, "action")));
	ADD_SIGNAL(MethodInfo("cursor_changed", PropertyInfo(Variant::STRING, "cursor")));
	ADD_SIGNAL(MethodInfo("item_drag_started", PropertyInfo(Variant::STRING, "element_id"),
			PropertyInfo(Variant::VECTOR2, "document_point")));
	ADD_SIGNAL(MethodInfo("item_dropped", PropertyInfo(Variant::DICTIONARY, "data"),
			PropertyInfo(Variant::STRING, "target_id"), PropertyInfo(Variant::VECTOR2, "document_point")));

	// enums
	BIND_ENUM_CONSTANT(NAV_UP);
	BIND_ENUM_CONSTANT(NAV_RIGHT);
	BIND_ENUM_CONSTANT(NAV_DOWN);
	BIND_ENUM_CONSTANT(NAV_LEFT);
	BIND_ENUM_CONSTANT(DIRTY_FULL);
	BIND_ENUM_CONSTANT(DIRTY_NONE);
	BIND_ENUM_CONSTANT(DIRTY_RECT);
	BIND_ENUM_CONSTANT(DIRTY_SCROLL);
	BIND_ENUM_CONSTANT(MASTER_CSS_GAME_UI);
	BIND_ENUM_CONSTANT(MASTER_CSS_FULL);
}
