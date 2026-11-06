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
| `reflections` (`off`, `low`, `high`), `reflection_strength` 0-1, `reflections_auto` | reflections, below |

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
`SV_VertexID`) and `ps`; `cbuffer P : register(b1) { float4 p[10]; }` as listed above kScenePostHlsl (mode, the
style's values, target size, host pixels per console pixel); `t0` the scene's colour, `t1` its depth (D24, read as
float; `p[7].x` 0 when there is none), `t2` and `t3` the reflection mask and its depth (`p[8].x` 0 when there is
none), `s0` linear and `s1` point samplers, both clamped.

## Reflections

Graphics Style > **Reflections**, or the game's Graphics page > Reflections (Off, Low, High). Screen-space
reflections, drawn in every style but Retro (Original takes them without the rest of the scene pass):

- **Which surfaces**: each reflective draw is drawn a second time into a mask (its own vertex and pixel shader, so
  its coverage and alpha test hold) with its depth, the nearest reflective surface kept. Found by itself
  (`reflections_auto`): alpha-blended (source alpha, one less source alpha), depth-tested 3D draws whose texture's
  alpha is see-through all over and even, as window glass is (a smoke puff's or a leaf's alpha spans the range, a
  cut-out's is 0 or 1). Listed: `mods/reflections.toml` beside the exe, read again within a second of a change:

  ```toml
  [textures]
  "0123456789abcdef" = 0.7   # a texture's key (as mods/textures/dump names it) = its strength 0-1
  "fedcba9876543210" = 0     # 0: never reflects, even when found as glass
  ```

  A listed texture drawn opaque (a polished floor) reflects all over; a blended one where its alpha covers.
- **How**: in the scene pass, from each pixel of the mask a ray mirrored about the surface's slope (from the mask's
  depth) is marched through the scene's depth, 20 steps (Low) or 48 (High), and what it meets is mixed in, more at a
  grazing view. The view angle is taken as 60 degrees vertical; the game's own is in its matrices.
- **Limits**: only what is on screen can be mirrored (a reflection fades towards the picture's edges, and what is
  behind the camera or hidden behind something is not there); no sky or environment fallback; a mod's
  `scene_post.hlsl` draws none unless it reads the mask (`t2`, `t3`, `p[8]`, `p[9]` as listed above kScenePostHlsl).

## Particles

Graphics Style > **Particles**, or the game's Graphics page > Particles (Original, Soft); graphics.toml
`soft_particles`, `particle_fade` (0.005-0.2, the fade's depth in parts of the particle's distance). Vulkan, every style
but Retro. A particle is a 3D draw blended over the scene (alpha blended, or added) that tests depth but does not
write it, and is no glass or reflective surface; it is drawn with its own pixel shader, faded where the scene behind
is near (the depth target copied once a frame at the first one; a W-buffer read as the distance), so smoke, flashes
and sparks melt into walls and floors instead of cutting through them in hard lines. Their pictures are upgraded as
any texture is, by the texture pack (hd_pack). More or longer-lived particles need the game's own code (not yet).

## Ray tracing

The game's Graphics page > Effects > **Ray Tracing** (Off, Shade, Shade + Shadows), Graphics Style > Ray tracing, or
graphics.toml `ray_tracing`, `rt_ao` (0-2), `rt_radius` (0.25-6 m), `rt_rays` (1-16 a pixel), `rt_shadows`. Vulkan on a
GPU with ray queries (VK_KHR_ray_query: NVIDIA RTX, AMD RX 6000 and later, Intel Arc); elsewhere the row reads "Not on
this GPU". Every style but Retro. Off by default.

- Each frame the 3D scene's opaque draws write their vertices where the game's own vertex programs place them (a
  variant of the draw's vertex shader), and the scene pass builds them into an acceleration structure: what is off
  screen or hidden is in it, unlike the depth the screen-space passes read. Left out: the 2D, blended and cut-out
  draws, and what is drawn after the game clears depth mid-frame (the gun and hands).
- **Shade**: ambient occlusion traced against it at half the scene's size, in place of the screen-space one. A pixel's
  place and slope are its own triangle's, where a ray from the camera meets it (the depth's neighbours give a wrong
  slope on flat ground far off). **Shadows**: effect lights (Particles > Soft + Lights) cast traced shadows.
- Test views (read at start): `XBE_RT_TEST=1` the left half with it, `2` the occlusion alone, `3` a ray from the
  camera against the depth (green where they agree). `.local/vk/rt_test.sh` shoots on/off pairs in a Quick Game.

## Depth of field

The game's Graphics page > Effects > **Depth of Field** (Off, Distance, Focus), Graphics Style > Depth of field, or
graphics.toml `depth_of_field` (`"off"`, `"distance"`, `"focus"`) and `dof_strength` (0-2). Vulkan, every style but
Retro. Off by default. Drawn last over the scene (after SMAA), before the HUD.

- **Distance**: the scenery blurs from 20 m, fully from 120 m. **Focus**: the nearest of five points about the middle
  of the view is in focus (eased over a few frames, by 1/distance), nearer and farther blurred by how far from it;
  aiming down the sights (nf::AimingDownSights, handed over by gpu::SetAiming) narrows the sharp band and doubles the
  blur.
- The world's distance is the depth kept at the game's mid-frame clear (as ray tracing keeps it): the gun and hands,
  drawn after it, stay sharp and are not gathered into the blur, and the focus is not taken from the gun.
- A gather of 32 taps at full size; a nearer tap counts only as far as its own blur reaches, so what is in focus casts
  no halo over the blurred distance behind it.
- Test views (read at start): `XBE_DOF_TEST=1` the left half with it, `2` the blur (white the most, blue the gun, red
  the focus point). `.local/vk/dof_test.sh` shoots off/distance/focus at three views (`NF_ADS_TEST=1` for aiming).

## SMAA

Settings window > Graphics > **SMAA**, or the game's Graphics page > Anti-Aliasing > SMAA (Vulkan). SMAA 1x (Ultra
preset, luma edges; Jimenez et al., MIT, fetched by the build) over the 3D scene before the HUD, so text and HUD stay
sharp, unlike FXAA, which is drawn over the finished picture. Not drawn in Retro. Quality High and Ultra take SMAA
under Vulkan, FXAA under Direct3D.

## Texture packs

- **Dump**: Graphics Style > Textures > *Dump the game's textures* writes each 2D texture the game uploads, once, to
  `mods/textures/dump/<key>.png` (B8G8R8A8). The key is a hash of the texture's level 0 bytes, format and size, so
  it is the same wherever and whenever the game loads it.
- **Replace**: a `mods/textures/<key>.png` of any size (2x, 4x, 8x; at most 8192 across) is drawn in its place, its
  mip levels made from it, through the renderers' replacement path (SetReplacement), so the texture coordinates
  are unchanged. Offline workflow: dump, upscale or repaint the PNGs with any tool, copy them up a folder.
- Not yet: P8 (palette) textures, cube and volume textures, compressed packs, normal/roughness companions.

Dumped textures are the game's: keep them out of any public repository.

### HD texture pack (hd_pack)

`tools\hd_pack.exe` beside the game makes a pack on the player's own PC: turn on *Dump the game's textures*, play the
levels to upgrade (a movie's frames, pictures that change within a second, are not dumped), then run

```
tools\hd_pack.exe [--scale 2|4] [--pbr] [--model x4plus|anime] [--force]
```

Each dumped picture is upscaled 4x by Real-ESRGAN (`tools
ealesrgan`, its ncnn Vulkan build: any Vulkan GPU, no
install) in one run, padded with its own opposite edges first so repeating textures meet without seams, its alpha
upscaled apart; `--scale 2` halves the result. `--pbr` adds `<key>_n.png` (normal map, height in alpha) and
`<key>_r.png` (roughness) generated from the picture. Pictures already done are skipped, so it can be run again after
playing more. The game loads the pack at its next start. Measured (RTX 5060): 187 textures of The Exchange, 4x with
PBR maps, 45 seconds.

The renderer uses: the upscaled pictures (the texture pack path); roughness for reflections (a pack picture whose mean
roughness is under 0.45 reflects, smoother more); and, with **Materials** on (Graphics > Materials > PBR, or graphics.toml
`materials`, `material_strength` 0-1.5; read with the pack, so at the next start), normal and roughness maps drawn: a
pack-replaced surface drawn opaque gets a variant of its pixel shader that builds the surface's frame from screen
derivatives (its position, the view taken as 60 degrees, and its texture coordinates: a cotangent frame), lights the
bumped normal from above the view and from the camera, scales the game's own colour by the bumped light over the flat
light, and adds a GGX highlight of the roughness kept in proportion to the colour. The game's baked lighting stays; the
game hands over no light directions, so the maps add relief and sheen rather than relight the scene. Vulkan, every
style but Retro. Measured (The Exchange, RTX 5060): about 1,240 such draws a frame, 60 frames a second.

**Relief** (Materials > PBR + Relief, graphics.toml `surface_depth` 0-2): parallax occlusion mapping. The view's ray is
stepped down the height map (8 to 24 steps, more at a glancing view) and the texture coordinates moved to where it
meets it, for the game's own textures as well as the maps, so mortar sinks, stones stand out and hide one another as
the view moves. Not tessellation: the game's vertex programs hand over only finished screen positions, so geometry
moved along a normal would open cracks between triangles and differ from what the player collides with. Silhouettes
stay flat.

The pack is made from the player's own copy of the game: nothing of the game's ships with the tool, and the pack is
not to be shared.

## Not available yet

Measured (Vulkan, XBE_PERF): in Paris gameplay all 3D draws, about 450 a frame, are the title's vertex programs; none
is fixed function. Modern lighting therefore needs the normals recovered from the vertex programs first.

Shown disabled in the menu: modern lighting, new shadows, normal maps and PBR materials, height maps and parallax,
tessellation, TAA. They need what the console's vertex programs do not hand the renderer: per-pixel normals and
tangents, view-space positions, the game's lights and a stable material identity per draw. Integration points: the
vertex program translation (shaders.cpp, VertexProgramHlsl) to export a view-space position and normal where a
program's matrix is recognised (MatrixProgram), the fixed-function path (FixedVertexHlsl), which already has them,
and the game's light setup (D3D SetLight for fixed function; vertex shader constants otherwise).
