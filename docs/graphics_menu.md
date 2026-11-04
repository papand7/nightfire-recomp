# Graphics menu (the game's own menus)

Main menu > PC Settings > **Graphics**, and in a level Pause > PC SETTINGS > **Graphics**. Built by
`src/game/graphics_page.cpp` with the same builder as PC Settings (`nf::BuildPage`, `src/game/pc_settings_page.cpp`):
a copy of the AV Options page's records (title, rows, choice arrows, help line, prompts, sounds) with our ids, so
it draws and moves as Eurocom's pages do. No ImGui.

| Row | Values | Drives |
|---|---|---|
| Graphics Quality | Low / Medium / High / Ultra / Custom | anisotropic filtering, FXAA, AO, bloom together |
| Shader Style | Original, Enhanced, Toon, Retro, mod presets, "(Edited)" | gpu::GraphicsStyle (Vulkan) |
| Style Options | page | the chosen style's rows |
| Texture Filtering | As the Game, 2x-16x | Settings::aniso (both renderers) |
| Ambient Occlusion | Off / Low / High | quality_ao (Vulkan) |
| Bloom | Off / On | quality_bloom (Vulkan) |
| Anti-Aliasing | Off, FXAA (+ MSAA 2x/4x/8x on Direct3D) | Settings::fxaa, msaa |
| Sharpening | Off / Low / Medium / High | Settings::sharpen (both) |
| Render Scale | 50%-200% | Settings::render_percent (both, at once) |
| Renderer | Direct3D 12 / 11 / Vulkan | next start |
| Texture Packs | Off / On | mods/textures (Vulkan) |
| Restore Defaults | button | the rows above and the style |

| Preset | Filtering | FXAA | AO | Bloom |
|---|---|---|---|---|
| Low | as the game | off | off | off |
| Medium | 4x | on | off | on |
| High | 8x | on | low | on |
| Ultra | 16x | on | high | on |

Changing any of those four makes the preset Custom; the style is apart from the preset. Under the Direct3D renderers
the Vulkan-only rows read "Needs Vulkan". A keeps a page's values and goes back, B leaves without, as on AV Options.
Values go to settings.toml and graphics.toml; the game's thread hands the style to the renderer (gpu::RequestStyle),
which takes it at its next frame.

Not offered (no renderer feature yet): shadows, lighting, normal/PBR materials, parallax, tessellation, TAA,
reflections, refresh rate, gamma.

## Reverse-engineered pieces used

- Menu file: a record stream at [0x25FB08]; builder 0x93960; records F0 page (id, var, platform, body size), F9
  widget (id, type, x, y, w, h), F6 attribute (message byte, a, b), F4 focus; sizes in pc_settings_item.cpp
  RecordSize.
- Per-id message dispatcher 0x8E320 (player, widget, message, widget, arg): 0x4C page entered, 0x4B A (arg the
  focused widget), 0x4E focus gained. Widget messages through 0x72820 (player, id, msg, a, b): choices 0x17 clear,
  0x10 add (text, value), 0x1E select by value, 0x35 read; sliders 0x27/0x69/0x2E/0x40; 0x2B hide (1) or show (0);
  0x18 text. Page change 0x92C40 on the player's menu context 0x25F1D0 + player * 0x1D8: 0x44 to a page, 0x5F back.
- **Page limit**: the front end's file takes three pages more than its own (PC Settings + two); a fourth crashed the
  menu code on the first page change (a null write under 0x8F3F0). Enhanced, Toon and Retro options therefore share
  one page: their rows sit on the same lines and the others are hidden with 0x2B.
