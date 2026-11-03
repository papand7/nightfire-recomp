# Graphics styles

Settings window (F1, or Back + Start on a controller) > **Graphics Style**. Styles switch at once, in play, and are
kept in `graphics.toml` beside the exe. They are drawn by the Vulkan renderer (Graphics > Renderer > Vulkan); the
Direct3D renderers draw Original whatever is chosen.

| Style | What it does |
|---|---|
| Original | The console's picture, untouched: no pass, no overrides. The reference. |
| Enhanced | Scene pass: sharpening, screen-space ambient occlusion (from depth), bloom, filmic or Reinhard tone mapping, exposure, contrast, saturation, temperature, vignette, film grain; anisotropic 16x on the 3D scene's textures. |
| Toon | Scene pass: brightness in flat bands (2 to 12), outlines from depth (silhouettes) and optionally from colour (internal edges), outline colour and thickness, halftone dots in the darker bands, grading. |
| Retro | Scene pass: blocks of 1 to 8 console pixels, 2 to 8 bits a channel, ordered (Bayer) dithering, a warm grade; nearest-texel textures and vertices snapped to the block grid. |

**The HUD and menus keep the game's look.** The scene pass runs on a screen-sized target the 3D scene was drawn to
(with a depth buffer), right before the first 2D draw over it (HUD, menus, text), or at the frame's end when there
is none. Texture filtering and vertex snapping change 3D draws only.

## graphics.toml

Written by the settings window; edited by hand it is read again within a second while the game runs (for tuning a
look). Every value is clamped to its range; unknown keys and wrong types are logged and ignored.

| Key | Values |
|---|---|
| `preset` | the preset the values came from (`original`, `enhanced`, `toon`, `retro`, `custom:<file>`) |
| `mode` | `original`, `enhanced`, `toon`, `retro` |
| `exposure` 0.25-4, `contrast` 0.5-2, `saturation` 0-2.5, `temperature` -1-1 | grading |
| `tonemap` | `off`, `filmic`, `reinhard` |
| `sharpen` 0-1, `bloom` 0-2, `bloom_threshold` 0-1, `ambient_occlusion` 0-2, `ao_radius` 0.25-4, `vignette` 0-1, `grain` 0-1 | post-processing |
| `texture_filter` | `game`, `nearest`, `anisotropic` (the 3D scene's textures) |
| `bands` 2-12, `outline` 0-6 (console pixels), `outline_color` [r, g, b] 0-1, `internal_edges`, `edge_threshold` 0.02-1, `halftone` 0-1 | toon |
| `pixel` 1-8, `color_bits` 2-8, `dither`, `retro_tint` 0-1, `vertex_snap` | retro |
| `texture_pack`, `dump_textures` | texture packs, below |

## Custom presets

`mods/presets/<name>.toml` beside the exe: `name` (shown in the list), `base` (`original`, `enhanced`, `toon` or
`retro`, whose values it starts from), then any keys of graphics.toml. Example (`docs/presets/cinematic.toml`):

```toml
name = "Cinematic"
base = "enhanced"
exposure = 1.1
tonemap = "filmic"
bloom = 0.6
vignette = 0.45
grain = 0.15
```

Presets are data only: no shaders or scripts are loaded from them. A file that is not TOML or has an unknown base is
left out, with the reason in the log.

## Shader mod: the scene pass

`mods/shaders/scene_post.hlsl` beside the exe replaces the built-in scene pass (vulkan.cpp, kScenePostHlsl, a good
starting point) and is read again within a second of a change, so a look can be tuned while playing. It is a
trusted local mod: HLSL compiled by DXC at run time, nothing else is run. If it does not compile, the log says why
and the built-in pass draws until the file changes. Interface: entry points `vs` (a full screen triangle, from
`SV_VertexID`) and `ps`; `cbuffer P : register(b1) { float4 p[8]; }` as listed above kScenePostHlsl (mode, the
style's values, target size, host pixels per console pixel); `t0` the scene's colour, `t1` its depth (D24, read as
float; `p[7].x` 0 when there is none), `s0` linear and `s1` point samplers, both clamped.

## Texture packs

- **Dump**: Graphics Style > Textures > *Dump the game's textures* writes each 2D texture the game uploads, once, to
  `mods/textures/dump/<key>.png` (B8G8R8A8). The key is a hash of the texture's level 0 bytes, format and size, so
  it is the same wherever and whenever the game loads it.
- **Replace**: a `mods/textures/<key>.png` of any size (2x, 4x, 8x; at most 8192 across) is drawn in its place, its
  mip levels made from it, through the renderers' replacement path (SetReplacement), so the texture coordinates
  are unchanged. Offline workflow: dump, upscale or repaint the PNGs with any tool, copy them up a folder.
- Not yet: P8 (palette) textures, cube and volume textures, compressed packs, normal/roughness companions.

Dumped textures are the game's: keep them out of any public repository.

## Not available yet

Measured (Vulkan, XBE_PERF): in Paris gameplay all 3D draws, about 450 a frame, are the title's vertex programs; none
is fixed function. Modern lighting therefore needs the normals recovered from the vertex programs first.

Shown disabled in the menu: modern lighting, new shadows, normal maps and PBR materials, height maps and parallax,
tessellation, TAA. They need what the console's vertex programs do not hand the renderer: per-pixel normals and
tangents, view-space positions, the game's lights and a stable material identity per draw. Integration points: the
vertex program translation (shaders.cpp, VertexProgramHlsl) to export a view-space position and normal where a
program's matrix is recognised (MatrixProgram), the fixed-function path (FixedVertexHlsl), which already has them,
and the game's light setup (D3D SetLight for fixed function; vertex shader constants otherwise).
