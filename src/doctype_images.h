// Doctype for Godot: supplies images to a document. The counterpart of
// HtmlResources in the Unity version.
//
// Every image UV the engine emits points into one atlas, because the whole
// page is drawn with a single material. Sources are ordinary Texture2D
// resources: <img src="res://icons/gold.png"> loads through ResourceLoader,
// and HtmlView.register_image() names a texture that is not on disk.

#ifndef DOCTYPE_IMAGES_H
#define DOCTYPE_IMAGES_H

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>

class DoctypeImages {
public:
	// Incremented whenever previously-unavailable content became available
	// or the atlas was repacked. The view turns a change into a relayout and
	// a draw-cache invalidation.
	int get_version() const { return version; }

	godot::Ref<godot::Texture2D> get_atlas() const { return atlas; }

	void register_image(const godot::String &p_name, const godot::Ref<godot::Texture2D> &p_texture);
	void clear();

	int get_max_atlas_size() const { return max_atlas_size; }
	void set_max_atlas_size(int p_size);

	// Host callbacks, in the engine's terms.
	bool get_image_size(const char *p_url, int &r_width, int &r_height);
	void begin_load_image(const char *p_url);
	bool get_image_uv(const char *p_url, float *r_uv4);

private:
	std::unordered_map<std::string, godot::Ref<godot::Image>> sources;
	std::unordered_map<std::string, godot::Rect2> uvs;
	std::unordered_set<std::string> missing;

	godot::Ref<godot::ImageTexture> atlas;
	int version = 0;
	int max_atlas_size = 4096;
	int padding = 2;

	// Source count the last refused pack saw, so a refusal is reported once
	// rather than once per record until the end of time.
	int refused_at_source_count = -1;

	godot::Ref<godot::Image> resolve(const std::string &p_url);
	static godot::Ref<godot::Image> to_rgba8(const godot::Ref<godot::Texture2D> &p_texture);
	void repack();
};

#endif // DOCTYPE_IMAGES_H
