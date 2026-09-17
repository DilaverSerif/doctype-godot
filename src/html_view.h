// Doctype for Godot: an HTML/CSS document laid out by litehtml and drawn on
// the GPU as one mesh. The Godot counterpart of HtmlView + HtmlRawImage +
// HtmlDocument from the Unity version, folded into one Control.
//
// The page renders into a retained SubViewport (see DoctypeSurface) and the
// control composites that texture onto itself with premultiplied alpha, so a
// page can be a see-through HUD. get_texture() hands the same surface to
// anything else that takes a Texture2D: a Sprite3D, a material, a TextureRect.

#ifndef DOCTYPE_HTML_VIEW_H
#define DOCTYPE_HTML_VIEW_H

#include <godot_cpp/classes/control.hpp>
#include <godot_cpp/classes/font_file.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

#include <string>
#include <unordered_map>
#include <vector>

#include "doctype_images.h"
#include "doctype_surface.h"
#include "lhu_api.h"

using namespace godot;

class HtmlView : public Control {
	GDCLASS(HtmlView, Control)

public:
	// Mirrors LhuNavDirection; document space, y down.
	enum NavDirection {
		NAV_UP = 0,
		NAV_RIGHT = 1,
		NAV_DOWN = 2,
		NAV_LEFT = 3,
	};

	// Mirrors LhuDirtyMode.
	enum DirtyMode {
		DIRTY_FULL = 0,
		DIRTY_NONE = 1,
		DIRTY_RECT = 2,
		DIRTY_SCROLL = 3,
	};

	// Mirrors LhuMasterCss.
	enum MasterStylesheet {
		MASTER_CSS_GAME_UI = 0,
		MASTER_CSS_FULL = 1,
	};

	HtmlView();
	~HtmlView();

	// --- content ---------------------------------------------------------
	void set_html(const String &p_html);
	String get_html() const { return html; }
	void set_user_css(const String &p_css);
	String get_user_css() const { return user_css; }
	void set_html_path(const String &p_path);
	String get_html_path() const { return html_path; }

	void load_html(const String &p_html, const String &p_user_css = String());
	bool load_file(const String &p_path);
	bool set_text(const String &p_selector, const String &p_text);
	bool set_style(const String &p_selector, const String &p_css);
	void mark_dirty();
	void mark_layout_dirty();

	// --- surface ---------------------------------------------------------
	void set_background(const Color &p_color);
	Color get_background() const { return background; }
	void set_match_control_size(bool p_enabled);
	bool get_match_control_size() const { return match_control_size; }
	void set_surface_size(const Vector2i &p_size);
	Vector2i get_surface_size() const { return surface_size; }
	void set_auto_height(bool p_enabled);
	bool get_auto_height() const { return auto_height; }
	void set_match_content_scale(bool p_enabled);
	bool get_match_content_scale() const { return match_content_scale; }
	void set_device_scale(float p_scale);
	float get_device_scale() const { return device_scale; }
	void set_render_scale(float p_scale);
	float get_render_scale() const { return render_scale; }
	void set_draw_surface(bool p_enabled);
	bool get_draw_surface() const { return draw_surface; }

	float get_effective_device_scale() const { return active_scale; }
	Vector2i get_surface_pixels() const { return pixel_size; }
	Ref<Texture2D> get_texture() const;

	// --- fonts -----------------------------------------------------------
	void set_fonts(const TypedArray<FontFile> &p_fonts);
	TypedArray<FontFile> get_fonts() const { return fonts; }
	void set_use_system_fonts(bool p_enabled);
	bool get_use_system_fonts() const { return use_system_fonts; }
	void set_default_font_family(const String &p_family);
	String get_default_font_family() const { return default_font_family; }
	void set_default_font_size(float p_size);
	float get_default_font_size() const { return default_font_size; }

	// --- document settings -------------------------------------------------
	void set_master_stylesheet(MasterStylesheet p_sheet);
	MasterStylesheet get_master_stylesheet() const { return master_stylesheet; }
	void set_language(const String &p_language, const String &p_culture = String());
	String get_language() const { return language; }
	String get_culture() const { return culture; }
	void set_language_code(const String &p_language);
	void set_culture_code(const String &p_culture);

	// --- input -------------------------------------------------------------
	void set_pass_through_empty_areas(bool p_enabled);
	bool get_pass_through_empty_areas() const { return pass_through_empty_areas; }
	void set_drag_to_scroll(bool p_enabled);
	bool get_drag_to_scroll() const { return drag_to_scroll; }
	void set_drag_items(bool p_enabled);
	bool get_drag_items() const { return drag_items; }
	void set_scroll_step(float p_step);
	float get_scroll_step() const { return scroll_step; }

	Vector2 to_document_point(const Vector2 &p_local) const;
	Vector2 from_document_point(const Vector2 &p_document) const;
	String element_at(const Vector2 &p_document_point) const;
	Rect2 get_element_rect(const String &p_selector) const;
	bool has_element(const String &p_selector) const;

	void pointer_move(const Vector2 &p_document_point);
	void pointer_down(const Vector2 &p_document_point);
	void pointer_up(const Vector2 &p_document_point);
	void pointer_exit();
	bool scroll_by(const Vector2 &p_delta, const Vector2 &p_document_point);

	// --- focus -------------------------------------------------------------
	void set_document_focus(const String &p_selector);
	bool move_focus(NavDirection p_direction);
	bool activate(const String &p_selector = String());
	String get_focused_id() const;

	// --- clicks ------------------------------------------------------------
	void bind_click(const String &p_element_id, const Callable &p_handler);
	void unbind_click(const String &p_element_id);
	void clear_click_bindings();

	// --- images ------------------------------------------------------------
	void register_image(const String &p_name, const Ref<Texture2D> &p_texture);
	void set_max_atlas_size(int p_size);
	int get_max_atlas_size() const;

	// --- stats -------------------------------------------------------------
	float get_document_width() const;
	float get_document_height() const;
	int get_quad_count() const;
	float get_parse_ms() const { return parse_ms; }
	float get_layout_ms() const { return layout_ms; }
	float get_draw_ms() const { return draw_ms; }
	float get_record_ms() const { return record_ms; }
	DirtyMode get_last_dirty_mode() const;
	Rect2i get_last_dirty_pixels() const;
	Vector2i get_font_atlas_size() const;
	String get_last_error() const;
	Dictionary get_stats() const;

	// --- Godot virtuals -----------------------------------------------------
	void _process(double p_delta) override;
	void _gui_input(const Ref<InputEvent> &p_event) override;
	bool _has_point(const Vector2 &p_point) const override;
	Variant _get_drag_data(const Vector2 &p_at_position) override;
	bool _can_drop_data(const Vector2 &p_at_position, const Variant &p_data) const override;
	void _drop_data(const Vector2 &p_at_position, const Variant &p_data) override;

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	// --- native document --------------------------------------------------
	LhuContext *ctx = nullptr;
	DoctypeSurface surface;
	DoctypeImages images;
	SubViewport *viewport = nullptr;
	RID display_item;
	Ref<Shader> composite_shader;
	Ref<ShaderMaterial> composite_material;

	// --- properties ---------------------------------------------------------
	String html = "<body style='font-family:sans-serif;padding:16px'><h1>Doctype</h1><p>Hello from Godot.</p></body>";
	String user_css;
	String html_path;
	Color background = Color(0, 0, 0, 0);
	bool match_control_size = true;
	Vector2i surface_size = Vector2i(800, 600);
	bool auto_height = false;
	bool match_content_scale = true;
	float device_scale = 1.f;
	float render_scale = 1.f;
	bool draw_surface = true;
	TypedArray<FontFile> fonts;
	bool use_system_fonts = true;
	String default_font_family = "sans-serif";
	float default_font_size = 16.f;
	MasterStylesheet master_stylesheet = MASTER_CSS_GAME_UI;
	String language = "en";
	String culture;
	bool pass_through_empty_areas = false;
	bool drag_to_scroll = true;
	bool drag_items = false;
	float scroll_step = 20.f;

	// --- frame state ---------------------------------------------------------
	bool needs_reload = true;
	bool force_reload = true;
	bool needs_layout = true;
	bool needs_render = true;
	bool fonts_dirty = true;
	bool document_reparsed = false;
	bool document_loaded = false;
	String loaded_html;
	String loaded_css;
	int resource_version = -1;
	Vector2i pixel_size = Vector2i(1, 1);
	float active_scale = 1.f;
	Vector2i laid_out_for;
	float laid_out_scale = 0.f;
	Color last_background = Color(-1, -1, -1, -1);
	Vector2 css_viewport = Vector2(1, 1);

	float parse_ms = 0.f;
	float layout_ms = 0.f;
	float draw_ms = 0.f;
	float record_ms = 0.f;
	int reload_count = 0;
	int layout_count = 0;
	int render_count = 0;
	int skipped_reloads = 0;

	// --- pointer state -------------------------------------------------------
	Vector2 last_pointer;
	bool has_pointer = false;
	bool pointer_is_down = false;
	Vector2 drag_remainder;
	String remembered_focus;
	bool has_remembered_focus = false;

	// --- deferred events ------------------------------------------------------
	struct PendingEvent {
		enum Kind { ANCHOR, ELEMENT, CURSOR } kind;
		String a, b, c, d;
		bool consumed = false;
	};
	std::vector<PendingEvent> pending_events;
	int dispatch_depth = 0;
	std::unordered_map<std::string, Callable> click_bindings;
	std::string css_buffer;

	// --- internals -------------------------------------------------------------
	void create_document();
	void destroy_document();
	void recreate_document();
	void register_fonts();
	bool register_font_bytes(const String &p_family, int p_weight, bool p_italic, const PackedByteArray &p_data);
	int register_system_fonts();

	void update_surface_size();
	void run_reload();
	void run_layout();
	void run_render();
	float measured_layout(float p_width);
	int restore_pointer_state();
	void apply_input_dirty(int p_dirty);
	String current_html() const;
	void update_display();

	void begin_dispatch() { dispatch_depth++; }
	void end_dispatch();
	void queue_event(const PendingEvent &p_event);
	void raise_event(const PendingEvent &p_event);
	void apply_cursor(const String &p_cursor);

	void handle_pointer_motion(const Vector2 &p_local, const Vector2 &p_relative, bool p_left_down);

	// Host callbacks handed to the native context.
	static void cb_get_image_size(void *p_user, const char *p_url, int32_t *r_w, int32_t *r_h);
	static void cb_load_image(void *p_user, const char *p_url);
	static int32_t cb_get_image_uv(void *p_user, const char *p_url, float *r_uv4);
	static const char *cb_import_css(void *p_user, const char *p_url, const char *p_base);
	static void cb_anchor_click(void *p_user, const char *p_url);
	static int32_t cb_element_click(void *p_user, const char *p_id, const char *p_tag, const char *p_classes,
			const char *p_action);
	static void cb_set_cursor(void *p_user, const char *p_cursor);
};

VARIANT_ENUM_CAST(HtmlView::NavDirection);
VARIANT_ENUM_CAST(HtmlView::DirtyMode);
VARIANT_ENUM_CAST(HtmlView::MasterStylesheet);

#endif // DOCTYPE_HTML_VIEW_H
