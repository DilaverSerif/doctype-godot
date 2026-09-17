#include "doctype_surface.h"

#include "doctype_shaders.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

namespace {

// Antialiased edges are evaluated up to a pixel outside the shape, so
// non-glyph quads get a small skirt of geometry to draw that falloff in.
const float ANTI_ALIAS_PAD = 1.5f;

inline void write_float(uint8_t *p_dst, float p_value) {
	std::memcpy(p_dst, &p_value, sizeof(float));
}

Rect2 rect_union(const Rect2 &p_a, const Rect2 &p_b) {
	if (p_a.size.x <= 0.f || p_a.size.y <= 0.f) {
		return p_b;
	}
	if (p_b.size.x <= 0.f || p_b.size.y <= 0.f) {
		return p_a;
	}
	float x0 = std::min(p_a.position.x, p_b.position.x);
	float y0 = std::min(p_a.position.y, p_b.position.y);
	float x1 = std::max(p_a.position.x + p_a.size.x, p_b.position.x + p_b.size.x);
	float y1 = std::max(p_a.position.y + p_a.size.y, p_b.position.y + p_b.size.y);
	return Rect2(x0, y0, x1 - x0, y1 - y0);
}

} // namespace

DoctypeSurface::DoctypeSurface() {
	quad_shader.instantiate();
	quad_shader->set_code(doctype_shaders::QUAD);
	clear_shader.instantiate();
	clear_shader->set_code(doctype_shaders::CLEAR);

	quad_material.instantiate();
	quad_material->set_shader(quad_shader);
	clear_material.instantiate();
	clear_material->set_shader(clear_shader);

	Ref<Image> white = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
	white->fill(Color(1, 1, 1, 1));
	white_tex = ImageTexture::create_from_image(white);

	quad_material->set_shader_parameter("font_tex", white_tex);
	quad_material->set_shader_parameter("grad_tex", white_tex);
	quad_material->set_shader_parameter("image_tex", white_tex);
	quad_material->set_shader_parameter("grad_size", Vector2(1, 1));
	quad_material->set_shader_parameter("data_width", DATA_WIDTH);

	mesh.instantiate();

	RenderingServer *rs = RenderingServer::get_singleton();
	root_item = rs->canvas_item_create();
	clear_item = rs->canvas_item_create();
	mesh_item = rs->canvas_item_create();
	rs->canvas_item_set_parent(clear_item, root_item);
	rs->canvas_item_set_parent(mesh_item, root_item);
	rs->canvas_item_set_z_index(clear_item, 0);
	rs->canvas_item_set_z_index(mesh_item, 1);
	rs->canvas_item_set_material(clear_item, clear_material->get_rid());
	rs->canvas_item_set_material(mesh_item, quad_material->get_rid());
	rs->canvas_item_set_default_texture_filter(mesh_item, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_LINEAR);
	rs->canvas_item_set_default_texture_filter(clear_item, RenderingServer::CANVAS_ITEM_TEXTURE_FILTER_NEAREST);
}

DoctypeSurface::~DoctypeSurface() {
	detach();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs) {
		rs->free_rid(mesh_item);
		rs->free_rid(clear_item);
		rs->free_rid(root_item);
	}
}

void DoctypeSurface::attach(SubViewport *p_viewport) {
	viewport = p_viewport;
	if (!viewport) {
		return;
	}
	Ref<World2D> world = viewport->find_world_2d();
	if (world.is_valid()) {
		RenderingServer::get_singleton()->canvas_item_set_parent(root_item, world->get_canvas());
	}
	target_fresh = true;
	mesh_stale = true;
}

void DoctypeSurface::detach() {
	if (viewport) {
		RenderingServer::get_singleton()->canvas_item_set_parent(root_item, RID());
	}
	viewport = nullptr;
}

bool DoctypeSurface::resize(const Vector2i &p_pixels) {
	Vector2i wanted(std::max(1, p_pixels.x), std::max(1, p_pixels.y));
	if (wanted == size && viewport && viewport->get_size() == wanted) {
		return false;
	}
	size = wanted;
	if (viewport) {
		viewport->set_size(size);
	}
	target_fresh = true;
	return true;
}

RID DoctypeSurface::get_texture_rid() const {
	if (!viewport) {
		return RID();
	}
	return RenderingServer::get_singleton()->viewport_get_texture(viewport->get_viewport_rid());
}

Ref<Texture2D> DoctypeSurface::get_texture() const {
	if (!viewport) {
		return Ref<Texture2D>();
	}
	return viewport->get_texture();
}

void DoctypeSurface::render(const LhuFrame &p_frame, const Color &p_background, const Vector2 &p_document_size,
		const Ref<Texture2D> &p_image_atlas, bool p_force_full) {
	if (!viewport) {
		mesh_stale = true;
		return;
	}

	Vector2 doc = p_document_size;
	if (doc.x <= 0.f || doc.y <= 0.f) {
		doc = Vector2(size);
	}

	// The target retains the previous frame between renders, which is what
	// makes the dirty modes meaningful: None keeps it untouched, Rect repaints
	// a hole in it. Full is the only mode that does not rely on that, and so
	// the only safe answer whenever the retained pixels are gone.
	DirtyMode mode = DirtyMode(p_frame.dirty_mode);
	if (mode < DIRTY_FULL || mode > DIRTY_SCROLL) {
		mode = DIRTY_FULL;
	}
	if (p_force_full || target_fresh) {
		mode = DIRTY_FULL;
	}
	last_dirty_mode = mode;

	if (mode == DIRTY_NONE) {
		return;
	}
	target_fresh = false;

	sync_font_atlas(p_frame);
	sync_gradient_lut(p_frame);

	if (p_image_atlas.is_valid()) {
		quad_material->set_shader_parameter("image_tex", p_image_atlas);
	} else {
		quad_material->set_shader_parameter("image_tex", white_tex);
	}

	// A forced full repaint does not force a full mesh rewrite: the mesh
	// holds content, the target holds pixels, and only the latter was
	// invalidated. mesh_stale covers the frames this surface never consumed.
	int prefix = mesh_stale ? 0 : p_frame.stable_prefix;
	int suffix = mesh_stale ? 0 : p_frame.stable_suffix;
	build_mesh(p_frame.quads, p_frame.quad_count, prefix, suffix);
	mesh_stale = false;

	float sx = float(size.x) / doc.x;
	float sy = float(size.y) / doc.y;

	Rect2 dirty_doc;
	if (mode == DIRTY_FULL) {
		dirty_doc = Rect2(0, 0, doc.x, doc.y);
	} else if (mode == DIRTY_RECT) {
		dirty_doc = Rect2(p_frame.dirty_x, p_frame.dirty_y, p_frame.dirty_w, p_frame.dirty_h);
	} else {
		// Scroll. The canvas path cannot move retained pixels, so the whole
		// scrolled window (the copy destination plus both slivers) is
		// repainted; everything outside it is untouched, which native
		// guarantees.
		dirty_doc = Rect2(p_frame.scroll_x, p_frame.scroll_y, p_frame.scroll_w, p_frame.scroll_h);
		dirty_doc = rect_union(dirty_doc, Rect2(p_frame.dirty_x, p_frame.dirty_y, p_frame.dirty_w, p_frame.dirty_h));
		dirty_doc = rect_union(dirty_doc, Rect2(p_frame.dirty2_x, p_frame.dirty2_y, p_frame.dirty2_w, p_frame.dirty2_h));
	}

	// Document units to target pixels, padded a pixel outward so rounding can
	// never shave the repaint short of the dirt.
	int px0 = std::clamp(int(std::floor(dirty_doc.position.x * sx)) - 1, 0, size.x);
	int py0 = std::clamp(int(std::floor(dirty_doc.position.y * sy)) - 1, 0, size.y);
	int px1 = std::clamp(int(std::ceil((dirty_doc.position.x + dirty_doc.size.x) * sx)) + 1, 0, size.x);
	int py1 = std::clamp(int(std::ceil((dirty_doc.position.y + dirty_doc.size.y) * sy)) + 1, 0, size.y);

	if (px1 <= px0 || py1 <= py0) {
		last_dirty_pixels = Rect2i();
		return;
	}

	Rect2i clip(px0, py0, px1 - px0, py1 - py0);
	last_dirty_pixels = clip;

	RenderingServer *rs = RenderingServer::get_singleton();

	rs->canvas_item_set_custom_rect(root_item, true, Rect2(clip));
	rs->canvas_item_set_clip(root_item, true);

	rs->canvas_item_clear(clear_item);
	rs->canvas_item_add_rect(clear_item, Rect2(clip), p_background);

	rs->canvas_item_clear(mesh_item);
	if (quad_count > 0) {
		// The mesh AABB is computed from the (all-zero) vertices at surface
		// creation and never again, so culling needs an explicit one.
		mesh->set_custom_aabb(AABB(Vector3(-4, -4, -1), Vector3(doc.x + 8, doc.y + 8, 2)));
		rs->canvas_item_add_mesh(mesh_item, mesh->get_rid(), Transform2D(sx, 0, 0, sy, 0, 0), Color(1, 1, 1, 1), RID());
	}

	rs->viewport_set_update_mode(viewport->get_viewport_rid(), RenderingServer::VIEWPORT_UPDATE_ONCE);
}

void DoctypeSurface::ensure_capacity(int p_quads) {
	if (capacity_quads >= p_quads && capacity_quads > 0) {
		return;
	}

	// Grow generously; relayout churn otherwise reallocates every frame.
	int capacity = 256;
	while (capacity < p_quads) {
		capacity <<= 1;
	}

	const int vertex_count = capacity * 4;

	PackedVector2Array verts;
	verts.resize(vertex_count);
	PackedColorArray colors;
	colors.resize(vertex_count);
	PackedVector2Array uvs;
	uvs.resize(vertex_count);
	PackedFloat32Array custom;
	custom.resize(vertex_count);

	// The index pattern never depends on content: quad k is always
	// {4k, 4k+1, 4k+2, 4k, 4k+2, 4k+3}.
	PackedInt32Array indices;
	indices.resize(capacity * 6);
	{
		int32_t *idx = indices.ptrw();
		for (int k = 0; k < capacity; k++) {
			int32_t b = k * 4;
			int j = k * 6;
			idx[j + 0] = b;
			idx[j + 1] = b + 1;
			idx[j + 2] = b + 2;
			idx[j + 3] = b;
			idx[j + 4] = b + 2;
			idx[j + 5] = b + 3;
		}
	}

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_COLOR] = colors;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_CUSTOM0] = custom;
	arrays[Mesh::ARRAY_INDEX] = indices;

	int64_t flags = int64_t(Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE) |
			(int64_t(Mesh::ARRAY_CUSTOM_R_FLOAT) << int64_t(Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT));

	mesh->clear_surfaces();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), flags);

	// Ask the server where it put each attribute rather than assuming: the
	// ranged updates below write raw bytes into its buffers.
	RenderingServer *rs = RenderingServer::get_singleton();
	int64_t format = int64_t(mesh->surface_get_format(0));
	vertex_stride = rs->mesh_surface_get_format_vertex_stride(format, vertex_count);
	attr_stride = rs->mesh_surface_get_format_attribute_stride(format, vertex_count);
	attr_off_color = rs->mesh_surface_get_format_offset(format, vertex_count, Mesh::ARRAY_COLOR);
	attr_off_uv = rs->mesh_surface_get_format_offset(format, vertex_count, Mesh::ARRAY_TEX_UV);
	attr_off_custom = rs->mesh_surface_get_format_offset(format, vertex_count, Mesh::ARRAY_CUSTOM0);

	if (vertex_stride != 8 || attr_stride != 16 || attr_off_color < 0 || attr_off_uv < 0 || attr_off_custom < 0 ||
			attr_off_color + 4 > attr_stride || attr_off_uv + 8 > attr_stride || attr_off_custom + 4 > attr_stride) {
		UtilityFunctions::push_error(vformat("[Doctype] unexpected mesh layout (vertex stride %d, attribute stride %d, "
											 "offsets %d/%d/%d); the page will not render correctly",
				vertex_stride, attr_stride, attr_off_color, attr_off_uv, attr_off_custom));
	}

	positions.assign(size_t(vertex_count) * 2, 0.f);
	attribs.assign(size_t(vertex_count) * size_t(attr_stride), 0);
	quad_data.assign(size_t(capacity) * TEXELS_PER_QUAD * 4, 0.f);
	emitted_before.assign(size_t(capacity) + 1, 0);
	capacity_quads = capacity;

	// The data texture grows with capacity. Rows are whole quads: 8 texels
	// per quad and 512 per row means a quad never straddles a row.
	int rows = std::max(1, capacity * TEXELS_PER_QUAD / DATA_WIDTH);
	PackedByteArray zeros;
	zeros.resize(int64_t(DATA_WIDTH) * rows * 16);
	std::memset(zeros.ptrw(), 0, zeros.size());
	Ref<Image> img = Image::create_from_data(DATA_WIDTH, rows, false, Image::FORMAT_RGBAF, zeros);
	if (data_tex.is_null()) {
		data_tex = ImageTexture::create_from_image(img);
	} else {
		data_tex->set_image(img);
	}
	quad_material->set_shader_parameter("quad_data", data_tex);

	// Whatever the old buffers held is gone; the next build is full.
	mesh_valid = false;
	last_emitted = 0;
	last_native_count = 0;
}

void DoctypeSurface::build_mesh(const LhuQuad *p_quads, int p_count, int p_stable_prefix, int p_stable_suffix) {
	if (p_count <= 0 || !p_quads) {
		// Keep the buffers; an empty frame degenerates every quad it used to
		// draw instead of destroying the mesh.
		if (capacity_quads > 0 && last_emitted > 0) {
			std::fill(positions.begin(), positions.begin() + size_t(last_emitted) * 8, 0.f);
			upload_vertices(0, last_emitted * 4);
		}
		quad_count = 0;
		last_uploaded_vertices = 0;
		last_native_count = 0;
		last_emitted = 0;
		if (capacity_quads > 0) {
			emitted_before[0] = 0;
			mesh_valid = true;
		}
		return;
	}

	ensure_capacity(p_count); // resets mesh_valid when it reallocates

	// The claim is only usable against the exact buffer the last build left
	// behind. Anything that broke that continuity falls back to full.
	int prefix = p_stable_prefix;
	int suffix = p_stable_suffix;

	if (!mesh_valid || prefix < 0 || suffix < 0) {
		prefix = 0;
		suffix = 0;
	} else {
		if (suffix > 0 && p_count != last_native_count) {
			suffix = 0;
		}
		int max_common = std::min(p_count, last_native_count);
		prefix = std::min(prefix, max_common);
		suffix = std::min(suffix, p_count - prefix);
	}

	// Vertex offsets translate through the emitted-quad prefix sum: the
	// prefix quads are byte-identical, so their drop decisions and therefore
	// their emitted count are exactly last frame's.
	int emitted = prefix > 0 ? emitted_before[prefix] : 0;
	const int prefix_emitted = emitted;
	int v = emitted * 4;
	const int upload_start = v;

	int middle_end = p_count - suffix;
	int old_suffix_emit_start = suffix > 0 ? emitted_before[middle_end] : last_emitted;
	int old_middle_emitted = old_suffix_emit_start - emitted;

	for (int q = prefix; q < middle_end; q++) {
		if (emit_quad(p_quads[q], v, emitted)) {
			emitted++;
		}
		emitted_before[q + 1] = emitted;
	}

	int total_emitted;
	if (suffix > 0 && emitted - prefix_emitted == old_middle_emitted) {
		// The middle emitted exactly as many quads as it replaced, so the
		// suffix vertices already sit at their final offsets.
		total_emitted = last_emitted;
	} else {
		for (int q = middle_end; q < p_count; q++) {
			if (emit_quad(p_quads[q], v, emitted)) {
				emitted++;
			}
			emitted_before[q + 1] = emitted;
		}
		total_emitted = emitted;
	}

	int upload_end = v;

	// Vertices past the new end still hold the previous frame's geometry;
	// degenerate them, or they would draw.
	if (total_emitted < last_emitted) {
		std::fill(positions.begin() + size_t(total_emitted) * 8, positions.begin() + size_t(last_emitted) * 8, 0.f);
		upload_end = std::max(upload_end, last_emitted * 4);
	}

	if (upload_end > upload_start) {
		upload_vertices(upload_start, upload_end);
		upload_quad_data();
	}

	quad_count = total_emitted;
	last_uploaded_vertices = upload_end - upload_start;
	uploaded_kb_total += double(last_uploaded_vertices) * BYTES_PER_VERTEX / 1024.0;
	last_native_count = p_count;
	last_emitted = total_emitted;
	mesh_valid = true;
}

bool DoctypeSurface::emit_quad(const LhuQuad &q, int &r_vertex, int p_k) {
	if (q.w <= 0.f || q.h <= 0.f) {
		return false;
	}

	// A fully transparent quad still costs fill rate; drop it here.
	if ((q.color >> 24) == 0 && q.type != LHU_QUAD_IMAGE && q.type < LHU_QUAD_LINEAR_GRAD) {
		return false;
	}

	float pad = q.type == LHU_QUAD_GLYPH ? 0.f : ANTI_ALIAS_PAD;

	float x0 = q.x - pad;
	float y0 = q.y - pad;
	float x1 = q.x + q.w + pad;
	float y1 = q.y + q.h + pad;

	// A border edge quad describes the whole element box, but it can only
	// ever paint a band along its own edge: shrink the geometry to that band.
	if (q.type == LHU_QUAD_BORDER) {
		const float aa = 2.f;
		switch (int(q.params[0])) {
			case 0: // top: corners tl, tr
				y1 = std::min(y1, q.y + std::max(q.border[1], std::max(q.ry[0], q.ry[1])) + aa);
				break;
			case 1: // right: corners tr, br
				x0 = std::max(x0, q.x + q.w - std::max(q.border[2], std::max(q.rx[1], q.rx[2])) - aa);
				break;
			case 2: // bottom: corners br, bl
				y0 = std::max(y0, q.y + q.h - std::max(q.border[3], std::max(q.ry[2], q.ry[3])) - aa);
				break;
			case 3: // left: corners tl, bl
				x1 = std::min(x1, q.x + std::max(q.border[0], std::max(q.rx[0], q.rx[3])) + aa);
				break;
		}

		if (x1 <= x0 || y1 <= y0) {
			return false;
		}
	}

	// UVs are mapped against the unpadded rect so glyphs land on exactly
	// their atlas cell.
	float du = (q.u1 - q.u0) / q.w;
	float dv = (q.v1 - q.v0) / q.h;
	float u0 = q.u0 - pad * du;
	float v0 = q.v0 - pad * dv;
	float u1 = q.u1 + pad * du;
	float v1 = q.v1 + pad * dv;

	const uint8_t r = uint8_t(q.color & 0xFF);
	const uint8_t g = uint8_t((q.color >> 8) & 0xFF);
	const uint8_t b = uint8_t((q.color >> 16) & 0xFF);
	const uint8_t a = uint8_t((q.color >> 24) & 0xFF);

	const float xs[4] = { x0, x1, x1, x0 };
	const float ys[4] = { y0, y0, y1, y1 };
	const float us[4] = { u0, u1, u1, u0 };
	const float vs[4] = { v0, v0, v1, v1 };

	for (int i = 0; i < 4; i++) {
		int vi = r_vertex + i;
		positions[size_t(vi) * 2 + 0] = xs[i];
		positions[size_t(vi) * 2 + 1] = ys[i];

		uint8_t *attr = &attribs[size_t(vi) * size_t(attr_stride)];
		attr[attr_off_color + 0] = r;
		attr[attr_off_color + 1] = g;
		attr[attr_off_color + 2] = b;
		attr[attr_off_color + 3] = a;
		write_float(attr + attr_off_uv, us[i]);
		write_float(attr + attr_off_uv + 4, vs[i]);
		write_float(attr + attr_off_custom, float(p_k));
	}

	float *d = &quad_data[size_t(p_k) * TEXELS_PER_QUAD * 4];
	// 0: rect centre xy, half-size xy
	d[0] = q.x + q.w * 0.5f;
	d[1] = q.y + q.h * 0.5f;
	d[2] = q.w * 0.5f;
	d[3] = q.h * 0.5f;
	// 1, 2: corner radii
	for (int i = 0; i < 4; i++) {
		d[4 + i] = q.rx[i];
		d[8 + i] = q.ry[i];
		d[12 + i] = q.border[i];
	}
	// 4: clip centre xy, half-size xy (x < 0 => unclipped)
	if (q.clip_w >= 0.f) {
		d[16] = q.clip_x + q.clip_w * 0.5f;
		d[17] = q.clip_y + q.clip_h * 0.5f;
		d[18] = q.clip_w * 0.5f;
		d[19] = q.clip_h * 0.5f;
	} else {
		d[16] = 0.f;
		d[17] = 0.f;
		d[18] = -1.f;
		d[19] = -1.f;
	}
	for (int i = 0; i < 4; i++) {
		d[20 + i] = q.clip_r[i];
		d[24 + i] = q.params[i];
	}
	// 7: type, gradient row
	d[28] = float(q.type);
	d[29] = float(q.grad_row);
	d[30] = 0.f;
	d[31] = 0.f;

	r_vertex += 4;
	return true;
}

void DoctypeSurface::upload_vertices(int p_first_vertex, int p_end_vertex) {
	if (p_end_vertex <= p_first_vertex || mesh->get_surface_count() == 0) {
		return;
	}
	const int count = p_end_vertex - p_first_vertex;

	PackedByteArray vb;
	vb.resize(int64_t(count) * vertex_stride);
	std::memcpy(vb.ptrw(), &positions[size_t(p_first_vertex) * 2], size_t(count) * vertex_stride);
	mesh->surface_update_vertex_region(0, p_first_vertex * vertex_stride, vb);

	PackedByteArray ab;
	ab.resize(int64_t(count) * attr_stride);
	std::memcpy(ab.ptrw(), &attribs[size_t(p_first_vertex) * size_t(attr_stride)], size_t(count) * attr_stride);
	mesh->surface_update_attribute_region(0, p_first_vertex * attr_stride, ab);
}

void DoctypeSurface::upload_quad_data() {
	if (data_tex.is_null() || quad_data.empty()) {
		return;
	}
	int rows = std::max(1, capacity_quads * TEXELS_PER_QUAD / DATA_WIDTH);
	PackedByteArray bytes;
	bytes.resize(int64_t(quad_data.size()) * sizeof(float));
	std::memcpy(bytes.ptrw(), quad_data.data(), quad_data.size() * sizeof(float));
	Ref<Image> img = Image::create_from_data(DATA_WIDTH, rows, false, Image::FORMAT_RGBAF, bytes);
	data_tex->update(img);
}

void DoctypeSurface::sync_font_atlas(const LhuFrame &p_frame) {
	if (!p_frame.font_atlas_pixels || p_frame.font_atlas_w <= 0 || p_frame.font_atlas_h <= 0) {
		return;
	}

	Vector2i wanted(p_frame.font_atlas_w, p_frame.font_atlas_h);
	bool resized = font_tex.is_null() || wanted != font_atlas_size;

	if (resized || font_atlas_version != p_frame.font_atlas_version) {
		PackedByteArray bytes;
		bytes.resize(int64_t(wanted.x) * wanted.y);
		std::memcpy(bytes.ptrw(), p_frame.font_atlas_pixels, size_t(wanted.x) * wanted.y);
		Ref<Image> img = Image::create_from_data(wanted.x, wanted.y, false, Image::FORMAT_R8, bytes);

		if (font_tex.is_null()) {
			font_tex = ImageTexture::create_from_image(img);
		} else if (resized) {
			font_tex->set_image(img);
		} else {
			font_tex->update(img);
		}

		font_atlas_size = wanted;
		font_atlas_version = p_frame.font_atlas_version;
		quad_material->set_shader_parameter("font_tex", font_tex);
	}
}

void DoctypeSurface::sync_gradient_lut(const LhuFrame &p_frame) {
	if (!p_frame.grad_lut_pixels || p_frame.grad_lut_rows <= 0 || p_frame.grad_lut_w <= 0) {
		quad_material->set_shader_parameter("grad_tex", white_tex);
		quad_material->set_shader_parameter("grad_size", Vector2(1, 1));
		return;
	}

	Vector2i wanted(p_frame.grad_lut_w, p_frame.grad_lut_rows);
	bool resized = grad_tex.is_null() || wanted != grad_lut_size;

	if (resized || grad_version != p_frame.grad_lut_version) {
		PackedByteArray bytes;
		bytes.resize(int64_t(wanted.x) * wanted.y * 4);
		std::memcpy(bytes.ptrw(), p_frame.grad_lut_pixels, size_t(wanted.x) * wanted.y * 4);
		Ref<Image> img = Image::create_from_data(wanted.x, wanted.y, false, Image::FORMAT_RGBA8, bytes);

		if (grad_tex.is_null()) {
			grad_tex = ImageTexture::create_from_image(img);
		} else if (resized) {
			grad_tex->set_image(img);
		} else {
			grad_tex->update(img);
		}

		grad_lut_size = wanted;
		grad_version = p_frame.grad_lut_version;
	}

	quad_material->set_shader_parameter("grad_tex", grad_tex);
	quad_material->set_shader_parameter("grad_size", Vector2(wanted));
}
