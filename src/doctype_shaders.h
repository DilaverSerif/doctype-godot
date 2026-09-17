// Doctype for Godot: the three canvas_item shaders, embedded so the addon
// works wherever the .gdextension is dropped.
//
// QUAD is the analytic renderer for the litehtml quad stream, a port of
// Assets/Doctype/Runtime/Shaders/HtmlQuad.shader from the Unity version and of
// native/tests/lhu_raster.h (the CPU reference). Per-quad parameters do not fit
// the two custom vertex channels a 2D mesh has in Godot, so they live in an
// RGBA32F data texture indexed by the quad number carried in CUSTOM0.x, and the
// vertex function fetches them once per vertex.
//
// Godot's 2D pipeline works in sRGB space, so unlike the Unity shader nothing
// is linearised here: CSS colours are blended exactly like the CPU rasterizer
// blends them.

#ifndef DOCTYPE_SHADERS_H
#define DOCTYPE_SHADERS_H

namespace doctype_shaders {

static const char *QUAD = R"GDSHADER(
shader_type canvas_item;
render_mode blend_mix, unshaded;

uniform sampler2D quad_data : filter_nearest, repeat_disable;
uniform sampler2D font_tex : filter_linear, repeat_disable;
uniform sampler2D grad_tex : filter_linear, repeat_disable;
uniform sampler2D image_tex : filter_linear, repeat_disable;
uniform vec2 grad_size = vec2(256.0, 1.0);
uniform int data_width = 512;

varying vec2 doc_pos;
varying vec4 v_rect;
varying vec4 v_rx;
varying vec4 v_ry;
varying vec4 v_border;
varying vec4 v_clip;
varying vec4 v_clipr;
varying vec4 v_params;
varying vec2 v_misc;

void vertex() {
	int q = int(CUSTOM0.x + 0.5);
	int base = q * 8;
	int row = base / data_width;
	int col = base - row * data_width;
	v_rect = texelFetch(quad_data, ivec2(col, row), 0);
	v_rx = texelFetch(quad_data, ivec2(col + 1, row), 0);
	v_ry = texelFetch(quad_data, ivec2(col + 2, row), 0);
	v_border = texelFetch(quad_data, ivec2(col + 3, row), 0);
	v_clip = texelFetch(quad_data, ivec2(col + 4, row), 0);
	v_clipr = texelFetch(quad_data, ivec2(col + 5, row), 0);
	v_params = texelFetch(quad_data, ivec2(col + 6, row), 0);
	v_misc = texelFetch(quad_data, ivec2(col + 7, row), 0).xy;
	doc_pos = VERTEX;
}

// Signed distance to a rounded box. Exact for circular corners; for
// elliptical ones the radial term is scaled by the smaller radius, which
// stays continuous and is visually indistinguishable.
float sd_round_box(vec2 p, vec2 b, float rx, float ry) {
	rx = max(rx, 0.0);
	ry = max(ry, 0.0);
	vec2 q = abs(p) - b + vec2(rx, ry);
	vec2 m = max(q, vec2(0.0));
	float outside;
	if (rx > 0.0 && ry > 0.0) {
		outside = length(m / vec2(rx, ry)) * min(rx, ry);
	} else {
		outside = length(m);
	}
	return min(max(q.x, q.y), 0.0) + outside - min(rx, ry);
}

// CSS corner order is top-left, top-right, bottom-right, bottom-left.
vec2 pick_radius(vec4 rx, vec4 ry, vec2 p) {
	vec2 top = p.x > 0.0 ? vec2(rx.y, ry.y) : vec2(rx.x, ry.x);
	vec2 bot = p.x > 0.0 ? vec2(rx.z, ry.z) : vec2(rx.w, ry.w);
	return p.y < 0.0 ? top : bot;
}

float coverage(float d) {
	float fw = max(fwidth(d), 1e-6);
	return clamp(0.5 - d / fw, 0.0, 1.0);
}

// Which of the four border edges owns this point: the smallest normalized
// penetration depth wins, which is the miter split along the diagonals.
int owning_edge(vec2 local, vec2 size, vec4 border) {
	float inf = 1e30;
	float t_l = border.x > 0.0 ? local.x / border.x : inf;
	float t_t = border.y > 0.0 ? local.y / border.y : inf;
	float t_r = border.z > 0.0 ? (size.x - local.x) / border.z : inf;
	float t_b = border.w > 0.0 ? (size.y - local.y) / border.w : inf;
	int best = 0;
	float bt = t_t;
	if (t_r < bt) { best = 1; bt = t_r; }
	if (t_b < bt) { best = 2; bt = t_b; }
	if (t_l < bt) { best = 3; }
	return best;
}

vec4 sample_gradient(float row, float t) {
	float rows = max(grad_size.y, 1.0);
	return texture(grad_tex, vec2(clamp(t, 0.0, 1.0), (row + 0.5) / rows));
}

void fragment() {
	int type = int(v_misc.x + 0.5);
	vec2 p = doc_pos;
	vec2 rel = p - v_rect.xy;
	vec2 hs = v_rect.zw;
	vec4 col = COLOR;
	float alpha = 1.0;

	if (type == 2) {
		// Glyph coverage is already antialiased in the atlas.
		alpha = texture(font_tex, UV).r;
	} else {
		vec2 r = pick_radius(v_rx, v_ry, rel);
		float d = sd_round_box(rel, hs, r.x, r.y);
		alpha = coverage(d);

		if (type == 1) {
			// Border: inner rounded rect = outer shrunk by the per-side widths.
			float bl = v_border.x;
			float bt = v_border.y;
			float br = v_border.z;
			float bb = v_border.w;
			vec2 outer_min = v_rect.xy - hs;
			vec2 size = hs * 2.0;
			vec2 in_half = max(vec2(size.x - bl - br, size.y - bt - bb) * 0.5, vec2(0.0));
			vec2 in_centre = outer_min + vec2(bl, bt) + in_half;
			vec4 in_rx = max(vec4(v_rx.x - bl, v_rx.y - br, v_rx.z - br, v_rx.w - bl), vec4(0.0));
			vec4 in_ry = max(vec4(v_ry.x - bt, v_ry.y - bt, v_ry.z - bb, v_ry.w - bb), vec4(0.0));
			vec2 in_rel = p - in_centre;
			vec2 ir = pick_radius(in_rx, in_ry, in_rel);
			float d_in = sd_round_box(in_rel, in_half, ir.x, ir.y);
			alpha *= coverage(-d_in);
			int want = int(round(v_params.x));
			if (want >= 0 && owning_edge(p - outer_min, size, v_border) != want) {
				alpha = 0.0;
			}
		} else if (type == 3) {
			col = texture(image_tex, UV);
		} else if (type >= 4) {
			float t = 0.0;
			if (type == 4) {
				vec2 ab = v_params.zw - v_params.xy;
				float len2 = dot(ab, ab);
				t = len2 > 0.0 ? dot(p - v_params.xy, ab) / len2 : 0.0;
			} else if (type == 5) {
				vec2 dr = (p - v_params.xy) / max(v_params.zw, vec2(1e-6));
				t = length(dr);
			} else {
				// CSS conic gradients start at 12 o'clock and run clockwise;
				// document y grows downward.
				vec2 dc = p - v_params.xy;
				float deg = degrees(atan(dc.x, -dc.y)) - v_params.z;
				t = fract(deg / 360.0 + 1.0);
			}
			col = sample_gradient(v_misc.y, t);
		}
	}

	alpha *= col.a;
	if (alpha < 0.0005) {
		discard;
	}

	// Rounded-rect clipping. A negative half-width marks "unclipped".
	if (v_clip.z >= 0.0) {
		vec2 c_rel = p - v_clip.xy;
		float cr = c_rel.x > 0.0 ? (c_rel.y < 0.0 ? v_clipr.y : v_clipr.z)
		                         : (c_rel.y < 0.0 ? v_clipr.x : v_clipr.w);
		alpha *= coverage(sd_round_box(c_rel, v_clip.zw, cr, cr));
	}

	COLOR = vec4(col.rgb, alpha);
}
)GDSHADER";

// Replaces a rectangle of the retained surface with the background colour.
// Blending is off, so colour and alpha are written unconditionally: a clear
// by other means that obeys the canvas item's clip rect, which a viewport
// clear would not.
static const char *CLEAR = R"GDSHADER(
shader_type canvas_item;
render_mode blend_disabled, unshaded;

void fragment() {
	COLOR = COLOR;
}
)GDSHADER";

// Composites the surface onto the scene. The quad shader blends with
// SrcAlpha, so the surface holds colour already scaled by its own alpha;
// blending it with plain mix would scale by alpha twice. Premultiplied blend
// is the matching half of that convention, and a modulate has to scale the
// colour by its own alpha too, or fading the control out would leave the
// colour at full strength.
static const char *COMPOSITE = R"GDSHADER(
shader_type canvas_item;
render_mode blend_premul_alpha, unshaded;

varying vec4 v_mod;

void vertex() {
	v_mod = COLOR;
}

void fragment() {
	vec4 s = texture(TEXTURE, UV);
	COLOR = vec4(s.rgb * v_mod.rgb * v_mod.a, s.a * v_mod.a);
}
)GDSHADER";

} // namespace doctype_shaders

#endif // DOCTYPE_SHADERS_H
