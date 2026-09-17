// Doctype for Godot: draws a recorded quad stream into a retained
// SubViewport. The Godot counterpart of HtmlMeshBuilder + HtmlRenderer.
//
// One ArrayMesh holds every quad (four vertices, 24 bytes each: position, uv,
// colour and the quad index); the per-quad shape parameters live in an
// RGBA32F data texture the vertex shader reads. The SubViewport never clears
// itself, so a frame whose native dirty rect is small repaints only that
// rect: the root canvas item is clipped to it, a blend-off quad clears it, and
// the whole mesh is drawn under the clip.

#ifndef DOCTYPE_SURFACE_H
#define DOCTYPE_SURFACE_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/rect2i.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include <cstdint>
#include <vector>

#include "lhu_types.h"

class DoctypeSurface {
public:
	// Mirrors LhuDirtyMode; the values are part of the native ABI.
	enum DirtyMode {
		DIRTY_FULL = 0,
		DIRTY_NONE = 1,
		DIRTY_RECT = 2,
		DIRTY_SCROLL = 3,
	};

	static const int BYTES_PER_VERTEX = 24;
	static const int DATA_WIDTH = 512; // texels per data-texture row
	static const int TEXELS_PER_QUAD = 8;

	DoctypeSurface();
	~DoctypeSurface();

	void attach(godot::SubViewport *p_viewport);
	void detach();

	// Returns true when the render target was (re)created and therefore holds
	// garbage where partial repaint expects the previous frame.
	bool resize(const godot::Vector2i &p_pixels);

	void render(const LhuFrame &p_frame, const godot::Color &p_background, const godot::Vector2 &p_document_size,
			const godot::Ref<godot::Texture2D> &p_image_atlas, bool p_force_full);

	// The next build rewrites every vertex: the mesh no longer mirrors the
	// frame native diffed against.
	void mark_stale() { mesh_stale = true; }

	godot::RID get_texture_rid() const;
	godot::Ref<godot::Texture2D> get_texture() const;
	godot::Vector2i get_size() const { return size; }

	int get_quad_count() const { return quad_count; }
	int get_last_uploaded_vertices() const { return last_uploaded_vertices; }
	double get_uploaded_kb_total() const { return uploaded_kb_total; }
	DirtyMode get_last_dirty_mode() const { return last_dirty_mode; }
	godot::Rect2i get_last_dirty_pixels() const { return last_dirty_pixels; }
	godot::Vector2i get_font_atlas_size() const { return font_atlas_size; }
	godot::Ref<godot::ShaderMaterial> get_material() const { return quad_material; }

private:
	godot::SubViewport *viewport = nullptr;
	godot::Vector2i size;
	bool target_fresh = true;

	godot::RID root_item;
	godot::RID clear_item;
	godot::RID mesh_item;

	godot::Ref<godot::ArrayMesh> mesh;
	godot::Ref<godot::Shader> quad_shader;
	godot::Ref<godot::Shader> clear_shader;
	godot::Ref<godot::ShaderMaterial> quad_material;
	godot::Ref<godot::ShaderMaterial> clear_material;
	godot::Ref<godot::ImageTexture> font_tex;
	godot::Ref<godot::ImageTexture> grad_tex;
	godot::Ref<godot::ImageTexture> data_tex;
	godot::Ref<godot::ImageTexture> white_tex;

	// CPU mirrors of the GPU buffers, sized to capacity.
	std::vector<float> positions; // 2 floats per vertex
	std::vector<uint8_t> attribs; // attr_stride bytes per vertex
	std::vector<float> quad_data; // TEXELS_PER_QUAD * 4 floats per emitted quad
	std::vector<int> emitted_before; // emitted quads before native index q

	int capacity_quads = 0;
	int last_native_count = 0;
	int last_emitted = 0;
	bool mesh_valid = false;
	bool mesh_stale = true;

	int vertex_stride = 8;
	int attr_stride = 16;
	int attr_off_color = 0;
	int attr_off_uv = 4;
	int attr_off_custom = 12;

	int quad_count = 0;
	int last_uploaded_vertices = 0;
	double uploaded_kb_total = 0.0;

	int font_atlas_version = -1;
	godot::Vector2i font_atlas_size;
	int grad_version = -1;
	godot::Vector2i grad_lut_size;

	DirtyMode last_dirty_mode = DIRTY_FULL;
	godot::Rect2i last_dirty_pixels;

	void ensure_capacity(int p_quads);
	void build_mesh(const LhuQuad *p_quads, int p_count, int p_stable_prefix, int p_stable_suffix);
	bool emit_quad(const LhuQuad &p_quad, int &r_vertex, int p_emitted_index);
	void upload_vertices(int p_first_vertex, int p_end_vertex);
	void upload_quad_data();
	void sync_font_atlas(const LhuFrame &p_frame);
	void sync_gradient_lut(const LhuFrame &p_frame);
};

#endif // DOCTYPE_SURFACE_H
