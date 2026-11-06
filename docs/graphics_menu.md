# Graphics menu (the game's own menus)

Main menu > Settings > **Graphics**, and in a level Pause > SETTINGS > **Graphics**. Built by
`src/game/graphics_page.cpp` with the same builder as Settings (`nf::BuildPage`, `src/game/pc_settings_page.cpp`):
a copy of the AV Options page's records (title, rows, choice arrows, help line, prompts, sounds) with our ids, so
it draws and moves as Eurocom's pages do. No ImGui.

| Row | Values | Drives |
|---|---|---|
| Graphics Quality | Low / Medium / High / Ultra / Custom | anisotropic filtering, FXAA, AO, bloom together |
| Shader Style | Original, Enhanced, Toon, Retro, Noir, mod presets, "(Edited)" | gpu::GraphicsStyle (Vulkan) |
| Style Options | page | the chosen style's rows |
| Effects | page | Ambient Occlusion, Bloom, Reflections, Particles, Materials, Lighting, Ray Tracing, Depth of Field (Vulkan; the shared page), Blood (any renderer) |
| Texture Filtering | As the Game, 2x-16x | Settings::aniso (both renderers) |
| Anti-Aliasing | Off, FXAA (+ MSAA 2x/4x/8x on Direct3D) | Settings::fxaa, msaa |
| Sharpening | Off / Low / Medium / High | Settings::sharpen (both) |
| Render Scale | 50%-200% | Settings::render_percent (both, at once) |
| Renderer | Direct3D 12 / 11 / Vulkan | next start |
| Texture Packs | Off / On | mods/textures (Vulkan) |
| HDR | Off / On (the screen's peak) / On, 400-2000 nits | Settings::hdr, hdr_peak (Vulkan, an HDR10 swap chain when Windows' HDR is on; the HUD and menus at Windows' SDR white, the 3D scene's highlights raised to the peak; `XBE_HDR_TEST=1` shows the pixels taken as the scene red) |
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

## Key Bindings

Settings > More Settings > **Key Bindings** (shared page, two sets of 12 rows; More Keys / First Keys switch them in
place). Each row an action on foot and its two keys; A (Enter, a click, a controller's A) shows "Press a Key..." and
the next key or mouse button becomes its first key, the old first its second; Escape clears the row; Default Keys
restores all. Saved at once to settings.toml's [keys], the same table as the F1 window's Controls tab. While a key is
awaited, and until it is let go, the keyboard and mouse reach neither the game nor the menu pointer
(app::BeginKeyCapture). Sprint is the left stick click's control (unused on foot), Shift by default.

## Reverse-engineered pieces used

- Menu file: a record stream at [0x25FB08]; builder 0x93960; records F0 page (id, var, platform, body size), F9
  widget (id, type, x, y, w, h), F6 attribute (message byte, a, b), F4 focus; sizes in pc_settings_item.cpp
  RecordSize.
- Per-id message dispatcher 0x8E320 (player, widget, message, widget, arg): 0x4C page entered, 0x4B A (arg the
  focused widget), 0x4E focus gained. Widget messages through 0x72820 (player, id, msg, a, b): choices 0x17 clear,
  0x10 add (text, value), 0x1E select by value, 0x35 read; sliders 0x27/0x69/0x2E/0x40; 0x2B hide (1) or show (0);
  0x18 text. Page change 0x92C40 on the player's menu context 0x25F1D0 + player * 0x1D8: 0x44 to a page, 0x5F back.
- **Page limit**: the front end's file takes three pages more than its own (Settings + two); a fourth crashed the
  menu code on the first page change (a null write under 0x8F3F0). Enhanced, Toon and Retro options, Effects, More
  Settings and Netplay therefore share one page: their rows sit on the same lines and the others are hidden with 0x2B.
- **Rows per page**: the game's font is about 19 units tall, so rows need 22 apart; from y 76 that is 12 rows above
  the help line at about 350. Graphics had 16 at 16 apart and they overlapped; its effect rows went to Effects.

## The mouse in the menus

src/common/menu_mouse.cpp. While a menu is up (GameInfo::menu_up) the mouse is a pointer: the cursor shown (an
orange arrow, GameInfo::menu_cursor, sized to the window) and free; in play it is hidden and held for mouse look.
Each page's rows are its menu file's widget records (nf::MenuGeometry: learned in the builder hook, the page from
the game's 0x4C "entered" message, the focused widget at the page widget + 0xAC), in the console's 640 x 480; the
cursor is mapped there through the shown frame (app::FramePoint). Hovering a row presses the d-pad a step at a time
until the game's own focus is there (its highlight, sound and help line); a page with nothing focused yet gets one
press to wake it. Left click: A on an item or button, left or right on a choice's or slider's value (the side of its
middle clicked); on a label, focus only. Right click: B. Wheel: the d-pad. Moving the d-pad hands the focus back to
it until the mouse moves again.

Tested (scripted pointer, XBE_PAD_CMD's `PTR x y`, `CLICKL`, `CLICKR`): main menu hover and click into Settings,
Settings to Graphics, a value changed by a click, right click back without keeping.

Not yet: a list is one widget (the pause screen's choices, codename and mission lists: hovering focuses the list,
the wheel moves through it, a click is A on its chosen entry); sliders change a step a click (no dragging);
Driving.xbe's EA menus have no geometry (left click A on what is focused, as before); the cursor itself is the
system's and does not show in in-engine shots.
