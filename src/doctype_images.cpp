#include "doctype_images.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/geometry2d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <vector>

using namespace godot;

void DoctypeImages::set_max_atlas_size(int p_size) {
	int wanted = p_size < 1 ? 1 : p_size;
	if (wanted == max_atlas_size) {
		return;
	}
	max_atlas_size = wanted;
	// A standing refusal was judged under the old limit.
	refused_at_source_count = -1;
}

Ref<Image> DoctypeImages::to_rgba8(const Ref<Texture2D> &p_texture) {
	if (p_texture.is_null()) {
		return Ref<Image>();
	}
	Ref<Image> img = p_texture->get_image();
	if (img.is_null() || img->is_empty()) {
		return Ref<Image>();
	}
	// get_image() may hand back the texture's own image; never mutate it.
	img = img->duplicate();
	if (img->is_compressed()) {
		if (img->decompress() != OK) {
			return Ref<Image>();
		}
	}
	if (img->has_mipmaps()) {
		img->clear_mipmaps();
	}
	if (img->get_format() != Image::FORMAT_RGBA8) {
		img->convert(Image::FORMAT_RGBA8);
	}
	return img;
}

void DoctypeImages::register_image(const String &p_name, const Ref<Texture2D> &p_texture) {
	if (p_name.is_empty() || p_texture.is_null()) {
		return;
	}
	Ref<Image> img = to_rgba8(p_texture);
	if (img.is_null()) {
		UtilityFunctions::push_error(vformat("[Doctype] register_image('%s'): the texture has no readable image", p_name));
		return;
	}

	std::string key = p_name.utf8().get_data();
	sources[key] = img;
	missing.erase(key);
	refused_at_source_count = -1;

	// Already packed under this name: the atlas has to be rebuilt or the old
	// picture would keep being drawn.
	if (uvs.erase(key) > 0) {
		repack();
	}
}

void DoctypeImages::clear() {
	sources.clear();
	uvs.clear();
	missing.clear();
	atlas.unref();
	refused_at_source_count = -1;
	version++;
}

Ref<Image> DoctypeImages::resolve(const std::string &p_url) {
	if (p_url.empty()) {
		return Ref<Image>();
	}

	auto it = sources.find(p_url);
	if (it != sources.end()) {
		return it->second;
	}

	if (missing.count(p_url)) {
		return Ref<Image>();
	}

	String path = String::utf8(p_url.c_str());
	ResourceLoader *loader = ResourceLoader::get_singleton();

	Ref<Texture2D> texture;
	String candidates[2] = { path, path.contains("://") ? String() : "res://" + path };
	for (const String &candidate : candidates) {
		if (candidate.is_empty() || !loader->exists(candidate)) {
			continue;
		}
		Ref<Resource> res = loader->load(candidate, "Texture2D");
		texture = Ref<Texture2D>(Object::cast_to<Texture2D>(res.ptr()));
		if (texture.is_valid()) {
			break;
		}
	}

	Ref<Image> img = to_rgba8(texture);

	// Not an imported resource (a file written at runtime, a user:// path, a
	// project that skipped the import): read the image file directly.
	if (img.is_null()) {
		for (const String &candidate : candidates) {
			if (candidate.is_empty() || !FileAccess::file_exists(candidate)) {
				continue;
			}
			Ref<Image> loaded = Image::load_from_file(candidate);
			if (loaded.is_valid() && !loaded->is_empty()) {
				img = loaded;
				if (img->is_compressed()) {
					img->decompress();
				}
				if (img->has_mipmaps()) {
					img->clear_mipmaps();
				}
				if (img->get_format() != Image::FORMAT_RGBA8) {
					img->convert(Image::FORMAT_RGBA8);
				}
				break;
			}
		}
	}

	if (img.is_null()) {
		// Remembered so a missing icon costs one lookup, not one per frame.
		missing.insert(p_url);
		return Ref<Image>();
	}

	sources[p_url] = img;
	refused_at_source_count = -1;
	return img;
}

bool DoctypeImages::get_image_size(const char *p_url, int &r_width, int &r_height) {
	r_width = 0;
	r_height = 0;
	if (!p_url) {
		return false;
	}
	Ref<Image> img = resolve(p_url);
	if (img.is_null()) {
		return false;
	}
	// Answered from the source, not the atlas: layout asks for this before
	// anything has been packed.
	r_width = img->get_width();
	r_height = img->get_height();
	return true;
}

void DoctypeImages::begin_load_image(const char *p_url) {
	if (!p_url || !*p_url) {
		return;
	}
	std::string key = p_url;
	if (uvs.count(key) || resolve(key).is_null()) {
		return;
	}
	// Packing happens here rather than in get_image_uv because that one is
	// called while a frame is already being recorded: repacking there would
	// move UVs the recorder had handed out moments earlier.
	repack();
}

bool DoctypeImages::get_image_uv(const char *p_url, float *r_uv4) {
	if (!p_url || !r_uv4) {
		return false;
	}
	auto it = uvs.find(p_url);
	if (it == uvs.end()) {
		return false;
	}
	const Rect2 &uv = it->second;
	r_uv4[0] = uv.position.x;
	r_uv4[1] = uv.position.y;
	r_uv4[2] = uv.position.x + uv.size.x;
	r_uv4[3] = uv.position.y + uv.size.y;
	return true;
}

void DoctypeImages::repack() {
	std::vector<std::string> names;
	std::vector<Ref<Image>> images;
	names.reserve(sources.size());
	images.reserve(sources.size());

	for (const auto &pair : sources) {
		if (pair.second.is_null()) {
			continue;
		}
		names.push_back(pair.first);
		images.push_back(pair.second);
	}

	if (images.empty()) {
		return;
	}

	// A refused pack stays refused until the sources change.
	if (int(sources.size()) == refused_at_source_count) {
		return;
	}

	PackedVector2Array sizes;
	sizes.resize(int64_t(images.size()));
	for (size_t i = 0; i < images.size(); i++) {
		sizes[int64_t(i)] = Vector2(float(images[i]->get_width() + padding * 2), float(images[i]->get_height() + padding * 2));
	}

	Dictionary packed = Geometry2D::get_singleton()->make_atlas(sizes);
	PackedVector2Array points = packed["points"];
	Vector2i atlas_size = packed["size"];

	if (atlas_size.x <= 0 || atlas_size.y <= 0 || points.size() != int64_t(images.size())) {
		refused_at_source_count = int(sources.size());
		UtilityFunctions::push_error("[Doctype] image atlas packing failed");
		return;
	}

	if (atlas_size.x > max_atlas_size || atlas_size.y > max_atlas_size) {
		refused_at_source_count = int(sources.size());
		UtilityFunctions::push_error(vformat("[Doctype] could not pack %d image(s) into a %dpx atlas (needs %dx%d); "
											 "raise max_atlas_size or use smaller textures",
				int(images.size()), max_atlas_size, atlas_size.x, atlas_size.y));
		return;
	}

	Ref<Image> atlas_image = Image::create_empty(atlas_size.x, atlas_size.y, false, Image::FORMAT_RGBA8);
	atlas_image->fill(Color(0, 0, 0, 0));

	uvs.clear();
	for (size_t i = 0; i < images.size(); i++) {
		const Ref<Image> &src = images[i];
		Vector2i at = Vector2i(points[int64_t(i)]) + Vector2i(padding, padding);
		atlas_image->blit_rect(src, Rect2i(0, 0, src->get_width(), src->get_height()), at);
		uvs[names[i]] = Rect2(float(at.x) / atlas_size.x, float(at.y) / atlas_size.y,
				float(src->get_width()) / atlas_size.x, float(src->get_height()) / atlas_size.y);
	}

	if (atlas.is_null()) {
		atlas = ImageTexture::create_from_image(atlas_image);
	} else {
		atlas->set_image(atlas_image);
	}

	// Tells the view that previously-unavailable content arrived, which
	// re-lays-out and drops any cached draw commands holding old UVs.
	version++;
}
