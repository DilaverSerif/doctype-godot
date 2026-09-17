# Doctype for Godot

HTML and CSS as a game UI for Godot 4, laid out by
[litehtml](https://github.com/litehtml/litehtml) and drawn entirely on the GPU.
No embedded browser, no CPU rasterizer, no per-platform web view. One mesh, one
material, and it compiles everywhere Godot does. A GDExtension port of
[doctype-unity](https://github.com/DilaverSerif/doctype-unity).

<p align="center">
  <img src="native/demo.png" width="560" alt="The demo page rendered by the reference rasterizer">
</p>

## Why this exists

litehtml is plain C++, has no renderer at all, and does not draw a single
pixel. It parses HTML and CSS, runs layout, and then calls *you* through a
`document_container` interface: fill this rectangle, put this glyph here,
stroke this border, paint this gradient.

This project is the other half of that interface. The native layer (shared
with the Unity version, unchanged) turns those calls into a flat stream of
quads. The Godot layer hands the whole page to the GPU as one mesh and
reconstructs rounded corners, borders, gradients and glyph coverage
analytically in a `canvas_item` shader. What comes out is an `HtmlView`
Control you drop into any scene, and a `Texture2D` you can put anywhere Godot
accepts one: a `Sprite3D`, a material slot, a `TextureRect`.

## What works

- **CSS you would actually use.** Flexbox, floats, tables, `position:
  absolute/fixed`, `overflow:auto` with real scrolling, `vw`/`vh`/`%` units,
  border radius, linear/radial/conic gradients, images, embedded fonts.
- **Interaction.** `:hover` and `:active`, anchor clicks, element clicks by
  id, wheel and trackpad scrolling, drag-to-scroll on touch, and Godot's own
  drag-and-drop between surfaces.
- **Gamepad and keyboard focus.** Elements opt in with `tabindex`, style
  themselves with `:focus`, and the `ui_up/down/left/right/accept` actions walk
  the D-pad between them. When the page has nowhere left to go, focus leaves
  through Godot's normal focus neighbours.
- **Mutation without re-parsing.** `set_text` and `set_style` change a text
  node or an inline style in place, and only the touched subtree is re-laid
  when that is provably enough.
- **Partial redraw.** The native side byte-diffs every recorded frame against
  the previous one and reports a dirty rectangle. The surface is a retained
  `SubViewport` that never clears itself, so changing a score repaints the
  score, not the page, and an unchanged page costs nothing at all.
- **Transparency that composites correctly.** A page can be a HUD over a
  running game, with premultiplied-alpha compositing and touch pass-through
  so the parts it does not paint are not a sheet of glass over the game.
- **Every renderer.** Forward+, Mobile and Compatibility (GLES3) all run the
  same shader; the retained-surface trick needs nothing beyond a 2D canvas.

## How it fits together

```
Godot (GDExtension, C++)                 Native layer (C++)

HtmlView (Control)   ── HTML/CSS ──▶  litehtml: parse, cascade, layout
  │                                        │
  │                                        ▼
  │                                      lhu_container: records draw calls
  │                                      as an analytic quad stream
  │                                        │
  │                    ◀── quads ──────────┘   (retained; only dirty subtrees
  │                                             are re-recorded)
  ▼
DoctypeSurface       one quad → 4 vertices + 8 RGBA32F texels of shape data
  ▼
SubViewport          canvas_item shader, clipped to the dirty rect,
  │                  target never cleared between frames
  ▼
HtmlView._draw       composites the surface with premultiplied alpha,
                     forwards mouse / touch / gamepad input back
```

Nothing is rasterized on the CPU except glyph coverage, which is cached in an
atlas and uploaded once.

## Getting started

Godot 4.7 or newer. Either download the macOS frameworks from the
[releases](https://github.com/DilaverSerif/doctype-godot/releases) and unzip
them into `addons/doctype/bin/`, or build the extension once (below). Then
copy `addons/doctype/` into your project.

Add an `HtmlView` node to a scene, or from code:

```gdscript
var view := HtmlView.new()
add_child(view)
view.load_html("<body style='font-family:sans-serif'><h1>Merhaba</h1></body>")
# view.get_texture() is a Texture2D

view.set_text("#score", "1280")                      # no re-parse
view.set_style("#slot3", "border-color:#3b82f6")     # no re-parse
var id := view.element_at(point_in_css_pixels)        # pure query, no hover change
```

The contract between the three entry points, because using the wrong one is
the easiest way to lose the performance the rest of this README measures:

| call | use it for | cost |
|---|---|---|
| `load_html` | structural document changes: a new screen, different markup | full parse + layout; never call it per frame |
| `set_text` | runtime text mutation: a score, a timer, a name | incremental; a fraction of a millisecond and a partial redraw |
| `set_style` | runtime visual or layout mutation: a colour, a position, a bar width | incremental; a fraction of a millisecond and a partial redraw |

Signals: `anchor_clicked(url)`, `element_clicked(id, tag, class_names,
action)`, `cursor_changed(cursor)`, `document_loaded`, `item_drag_started`,
`item_dropped`. `bind_click("save", callable)` runs a handler for one id and
stops the click bubbling further.

The addon README under [`addons/doctype/`](addons/doctype/README.md) covers
fonts, images, HUD layout, focus and the input model in more depth (in
Turkish).

## Building from source

The extension is litehtml + the Doctype recorder + the Godot glue, built with
SCons against [godot-cpp](https://github.com/godotengine/godot-cpp), a
submodule pinned to a commit that carries the 4.7 API.

```bash
git clone --recursive https://github.com/DilaverSerif/doctype-godot
cd doctype-godot

# macOS
scons platform=macos arch=universal target=template_debug
scons platform=macos arch=universal target=template_release

# Android (needs ANDROID_HOME with an NDK)
scons platform=android arch=arm64 target=template_release

# iOS (static library, linked at export)
scons platform=ios arch=arm64 target=template_release

# Linux / Windows
scons platform=linux arch=x86_64 target=template_release
scons platform=windows arch=x86_64 target=template_release
```

Every build drops its library into `addons/doctype/bin/`, where
`doctype.gdextension` already lists it. litehtml needs C++ exceptions, so the
`SConstruct` builds godot-cpp with them enabled.

## Tests

The native layer has its own harness, no Godot and no GPU required. It drives
the same C ABI the extension does, asserts on the quad stream and rasterizes
`demo.html` through a reference CPU rasterizer (the executable specification
of the shader):

```bash
native/build_harness.sh          # 188 checks
```

The Godot side is tested through the real renderer. Each script opens a
window, renders, reads the surface back and saves PNGs into `demo_out/`:

```bash
Godot --path . -s res://test/render_test.gd       # rendering, partial redraw, hit testing, premultiplied alpha
Godot --path . -s res://test/input_test.gd        # Godot input routing: hover, click, focus, wheel, drag, content scale
Godot --path . -s res://test/demo_pages_test.gd   # every demo page, animation via set_style
Godot --path . -s res://test/turkish_test.gd      # locale-aware text-transform
```

Prefix any of them with `--rendering-driver opengl3` or `--rendering-method
mobile` to run the same checks on the other renderers.

## Measurements

Measured on an Apple M2 Max with the demo page (229 quads) at 800x600:

| scenario | CPU per frame |
|---|---|
| page unchanged | 0 (no record, no upload, no draw) |
| one text node changed (`set_text`) | layout 0.02 ms + record and upload 0.10 ms, repaints 192x37 px |
| one inline style changed (`set_style`) | layout 0.1 ms + 0.1 ms |
| page re-parsed (`load_html`, the negative control) | 3.3 ms |

The first frame of a new document additionally rasterizes its glyphs into
the atlas, which is a one-time cost.

## What is different from the Unity version

- `HtmlView`, `HtmlRawImage` and `HtmlDocument` are one node. The surface is
  an internal `SubViewport`; `get_texture()` hands it out.
- Per-quad shape parameters live in a data texture instead of eight UV
  channels, because a 2D mesh in Godot carries two custom vertex channels. The
  vertex is 24 bytes instead of 108.
- Scrolling repaints the scrolled window instead of copying pixels; a 2D
  canvas has no texel copy. Everything outside the window stays retained.
- Fonts come from `FontFile` resources (their family name and weight are read
  from the file), images from `Texture2D` resources or `res://` paths.
- Drag and drop between surfaces goes through Godot's own `_get_drag_data` /
  `_drop_data` machinery.

## Licenses

| component | license |
|---|---|
| litehtml | BSD-3-Clause |
| gumbo-parser (inside litehtml) | Apache-2.0 |
| stb_truetype | Public domain / MIT |
| godot-cpp | MIT |
| Doctype | MIT |
