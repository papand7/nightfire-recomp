# 007: Agent Under Fire recomp - progress log

Newest first. Each entry: date, what changed, what was learned, what's next.

## Status
- Vulkan port: SDK branch vulkan-port (9dbcdf3) contains NightFire's Vulkan backend adapted as the only host renderer; DX renderer sources, interfaces, fallback, and libraries are excluded. This checkout has no game assets, so it has not been built or Windows-smoke-tested.
- Vulkan visual port: SDK branch `visual-port` has FXAA, gamma, scaling, sharpening, anisotropic filtering, texture replacement, SMAA 1x, PNG texture packs/dumps (2e5d161), five screen-space styles (0b65310), SDR bloom (349fd1d), vignette and animated film grain (b569525), YUY2 movie upscaling (b129368), exposure/contrast/saturation/temperature controls (5162dbf), optional Vulkan ray-query setup, geometry capture and per-frame BLAS/TLAS builds (1f6e61a, f9dd919, 9c57952, a250cd2), screen-space AO (c246048), TLAS-backed ray-query AO (43eceeb), and depth of field with focus controls (34d4501), plus depth-aware Toon outlines (3e78285), depth-slope crease outlines (607d5fc), device-addressable allocation for ray-query input buffers (20635d1), address validation for those buffers (201c897), and cached addresses on addressable buffers (9051246).
  - Present in Agent Under Fire: FXAA, SMAA, render scaling/filtering, sharpening, gamma, anisotropic filtering, texture replacement, Enhanced Color, depth-aware and depth-slope screen-space Toon, Retro, Noir, SDR bloom, vignette, film grain, YUY2 movie scaling, screen color adjustments, optional ambient occlusion (screen-space fallback, ray queries when supported), and optional depth of field with focus distance/blur controls (Vulkan only). AO and depth of field are not runtime-verified. Vulkan MSAA is unsupported.
  - Still to adapt in the shared renderer: temporal AA/upscalers (requires depth, jitter and motion vectors), HDR tone mapping and multilevel HDR bloom, and motion blur. These need renderer-owned passes and checks against this game's output.
  - Game-data dependent: the full ray-traced Enhanced look, contact shadows, mesh-normal Toon outlines, global illumination, reflections/water, light shafts, muzzle/fire lighting, fog, snow/footprints, particles and persistent blood. Nightfire's versions depend on its depth, lights, materials, weather and game hooks; identify Agent Under Fire equivalents before implementation.
  - RT capture adaptation: Agent Under Fire has two frame slots, and it waits for a slot in Present after draw recording. Capture buffers therefore need an early wait before the first draw that writes each slot; Nightfire's frame handling cannot be copied as-is.
  Agent Under Fire also exposes the matched scene depth image to final Vulkan passes. Its default W-buffer path can use Nightfire's depth reconstruction; the optional `XBE_WBUFFER=0` path uses standard-depth reconstruction. Ambient occlusion now builds the TLAS when enabled and selects a ray-query pass when supported and the frame capture succeeds; otherwise it uses the depth-based pass. Both modes need build and visual verification.
  The depth-of-field pass uses the same depth mapping but has no saved world-only depth, so it can blur the first-person weapon. Nightfire's sharper weapon relies on a mid-frame depth snapshot that has not been identified in Agent Under Fire.
  Not built or Windows-smoke-tested; this checkout has no game assets.
- Milestone: 1 - plays headless. default.xbe's movies, START, bond_eng.xbe's title, menus, briefing and first mission;
  DRIVING.XBE's five levels and back. No trap in 200 s of the first mission. On Windows the first mission plays to its end; Harry (2026-10-30, D3D12): plays with the driving fix, no obvious visual bugs on a first look.
- XDK: 4039 in the fork's main: default.xbe 98, bond_eng.xbe 113, DRIVING.XBE 112 of
  266 functions; layout proven from bond_eng.xbe's code
- Push buffers: none (no game code calls MakeSpace or touches the device; RunPushBuffer not called)
- Smoke: scripts/smoke.sh, default.xbe -> START -> bond_eng.xbe's first mission, about 6 minutes, then the driving part, SMOKE OK
- GitHub: hk21x/007-agentunderfire-recomp; SDK at the fork's main
- PC features: PC Settings, pointer, achievements, French and German confirmed by Harry; skip intro, field of view, mouse on foot confirmed by Harry; skip intro, field of view, mouse, 60 FPS, radar and culling confirmed by Harry
- Not done: DRIVING.XBE in the smoke test, levels played to their end, PC features

## Disc
- PAL, title 0x4541000D (EA), certificate region 0x4, version 2; extracted with extract-xiso from Harry's image.
- default.xbe 3dbed4eb51701a53676e0ace4cdea1752dc04697, built 2002-03-27: XAPILIB, D3D8, XBOXKRNL, LIBC, XGRAPHC
- bond_eng.xbe 66aace3859a5ca9a992f2d1845c2b5f5265fd9d6, built 2002-03-27: XAPILIB, XBOXKRNL, LIBC, D3D8, D3DX8, XGRAPHC
- bond_fre.xbe 87b2d3c8b7b40916fe29b03490c84c4f690a4c12, built 2002-03-27; bond_ger.xbe 88d06191f3ba940394f258f138f0aa37f1ced1dd, built 2002-04-08
- DRIVING.XBE 6b58e61ee8ce9aa2a1a357affe188af874cf05f4, built 2002-03-26: XAPILIB, D3DX8, D3D8, XGRAPHC, XBOXKRNL, LIBC, LIBCPMT
- Every library 1.0.4039. Sections include a DSOUND section though the library table does not list it.

## Log

### 2026-11-11 - Add depth-based depth of field
- Added an optional Vulkan depth-of-field pass with persisted focus distance (1–100 m) and blur radius (0–12 px). It uses the same-frame depth, supports W-buffer and standard-depth reconstruction, avoids sampling beyond the frame, and runs before final color effects and UI.
- SDK commit: 34d4501. The first-person weapon may blur because Agent Under Fire does not yet have Nightfire's world-only depth snapshot. `git -C sdk diff --check` passes; no build or visual check was run.
- Next: inspect motion-blur inputs and camera history, then continue the shared renderer effects.

### 2026-11-11 - Trace ambient occlusion against the frame TLAS
- The Graphics AO toggle now enables eligible opaque-geometry capture and per-frame BLAS/TLAS builds. On ray-query-capable Vulkan devices, the AO pass traces four short hemisphere rays per half-resolution pixel against that TLAS; other devices or frames without a valid capture use the screen-space depth pass. Shader Model 6.5 is compiled only for the traced path, and the AS descriptor is omitted on devices without RT support.
- SDK commit: 43eceeb. `git -C sdk diff --check` passes. No build or GPU check: DXC's ray-query compilation, descriptor layout, captures and AO output remain unverified. Capture still excludes alpha-tested, blended, HUD and transformed geometry.
- Next: build and inspect on an RT-capable GPU, compare fallback and traced AO, and tune or correct from screenshots.

### 2026-11-11 - Add optional depth-based ambient occlusion
- Added a half-resolution screen-space occlusion pass over the same-frame scene depth and composite it before the final Vulkan blit. Added a saved Graphics toggle, off by default; the pass supports the default W-buffer and `XBE_WBUFFER=0` depth mapping.
- Source review and `git diff --check` only. No build or visual check; depth reconstruction, orientation and quality still need runtime verification. This is screen-space AO, not Nightfire's ray-traced AO.
- Next: use the existing per-frame TLAS for a ray-query AO path, with the screen-space pass as a fallback.

### 2026-11-11 - Check the ambient occlusion depth path
- Source inspection confirms Present retains a same-frame depth view matched to the scene colour target, and the generated draw path supports both the default W-buffer and `XBE_WBUFFER=0` standard depth.
- This gives a renderer-owned GTAO pass a usable depth input; the Nightfire shader cannot be copied unchanged because its world/gun depth split and projection assumptions still need checking against Agent Under Fire.
- No code or build change. Next: implement the depth-aware pass and verify its depth conversion and screen orientation.

### 2026-11-11 - Build frame acceleration structures
- Added opt-in BLAS/TLAS construction from the captured positions and normalized indices. It honors the device's acceleration-structure scratch alignment and keeps each frame slot's source, scratch, storage, and instance buffers alive until that slot is reused.
- SDK commit: a250cd2. git -C sdk diff --check passes. No build or GPU check was run; the TLAS is not yet consumed by a ray-query shader, so there is no visible ray-traced effect.
- Next: bind the TLAS into a ray-query-capable image pass and expose the effect as a user-facing visual option.

### 2026-11-11 - Capture eligible Agent Under Fire scene geometry
- Added an opt-in capture pass (XBE_RT_CAPTURE=1) for opaque, depth-writing, non-HUD triangle draws with matching full-size color/depth targets. It wraps Agent Under Fire's generated vertex shader, writes view-like positions to a per-frame addressable buffer, and records normalized triangle indices; it waits before reusing the two-slot buffer.
- SDK commit: 9c57952. git -C sdk diff --check passes. No build or GPU check was run. This is capture groundwork only; no BLAS/TLAS or visible ray-traced effect exists yet.
- Next: build and bind per-frame acceleration structures from the captured geometry, then add a traced image pass.

### 2026-11-11 - Cache validated Vulkan buffer addresses
- Addressable buffers now cache their device address immediately after binding; later acceleration-structure code can use the validated value without querying Vulkan again.
- SDK commit: 9051246. git -C sdk diff --check passes. No build or GPU check was run; there is still no geometry capture, AS build, or visible RT effect.
- Next: add frame-safe scene geometry capture and build the per-frame acceleration structure.

### 2026-11-11 - Agent Under Fire RT frame-slot constraint
- Confirmed the renderer has two frame slots and waits on a slot in Present, after the scene draws are recorded. A per-slot RT capture buffer must wait before its first write of the next use; copying Nightfire's buffer lifetime unchanged would race the GPU.
- Agent Under Fire's vertex/index inputs are represented by gpu::Assembled (sdk/runtime/gpu/vertex.h). The capture shader can wrap its generated VOut main as in Nightfire, but index/base-vertex mapping must use these fields.
- Next: implement the capture path with the early slot wait, then build a frame acceleration structure.

### 2026-11-11 - Validate device addresses for RT buffers
- Addressable Vulkan buffers now query vkGetBufferDeviceAddress after allocation and binding. If the feature or address is unavailable, creation fails cleanly before the buffer can be used as an acceleration-structure input.
- SDK commit: 201c897. git -C sdk diff --check passes. No build or GPU check was run; geometry capture and visible ray-traced effects remain incomplete.
- Next: implement eligible draw capture and a frame-slot acceleration structure using these validated addresses.

### 2026-11-11 - Depth-slope Toon crease outlines
- Toon now compares depth-derived slopes on either side of each pixel and outlines their discontinuities, which can expose shallow creases beyond raw depth steps. This is a screen-space estimate, not a mesh-normal buffer.
- SDK commit: 607d5fc. git -C sdk diff --check passes. No build or visual check was run.
- Next: continue RT scene capture and acceleration-structure integration; the screen-space estimate does not provide ray tracing.

### 2026-11-11 - Device-addressable Vulkan buffers for ray-query inputs
- Vulkan allocations now opt into VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT only for buffers requesting shader-device-address usage, and keep those memory blocks separate from ordinary allocations.
- SDK commit: 20635d1. git -C sdk diff --check passes. No build or GPU check was run. This is allocator support only; ray-traced geometry capture, acceleration-structure builds, and visible RT effects remain incomplete.
- Next: use the device addresses in Agent Under Fire's eligible scene-geometry capture and per-frame acceleration-structure path.

### 2026-11-11 - Depth-aware Toon outlines
- Toon now combines its color edges with discontinuities from the matching full-size scene depth buffer. The renderer records only same-frame depth associated with the presented color target; missing or mismatched depth keeps the existing color-only Toon edge.
- SDK commit: 3e78285. `git diff --check` passes. No build or game visual check was run.
- Next: add normal-aware edges, then continue the ray-query geometry capture and traced passes.

### 2026-11-11 - Vulkan geometry-capture layout prepared
- Reserved a vertex-stage storage-buffer descriptor for captured scene vertices and a 16-byte vertex push-constant range in the shared Vulkan pipeline layout. Ordinary draws still leave both unused.
- SDK commit: f9dd919. `git diff --check` passes. This prepares the capture shader interface; there is still no geometry capture, acceleration-structure build, or visible RT effect. No build/GPU validation was done.
- Next: add a capture variant of eligible Agent Under Fire vertex shaders and populate the per-frame buffer.
### 2026-11-11 - Ray-query device capability detection
- The Vulkan backend now checks for acceleration-structure, ray-query and deferred-host-operations extensions plus required device features, enables them only when all are supported, loads their optional entry points, and logs availability. Unsupported GPUs keep the existing renderer path.
- SDK commit: 1f6e61a. `git diff --check` passes. This is capability groundwork only: no geometry capture or ray-traced image effect is implemented, and no build/GPU validation has been done.
- Next: capture eligible Agent Under Fire scene draws and build the per-frame acceleration structure for a first traced effect.
### 2026-11-11 - Ray-tracing port dependency check
- Confirmed the Agent Under Fire Vulkan backend has no acceleration-structure extension loading, ray-query path, geometry capture, or RT passes. Nightfire builds a per-frame acceleration structure from vertices captured by scene draws, then uses it for AO, shadows and GI. Porting its final shaders alone cannot enable ray tracing.
- Nightfire captures only opaque or alpha-cutout world triangles with matching full-size color/depth targets, depth test/write enabled, and no HUD/pretransformed geometry. It compiles a storage-writing variant of each generated game vertex shader; its source notes Vulkan ray-query support and DXC Shader Model 6.5 are required (MoltenVK cannot compile those shaders).
- Agent Under Fire's draw path already assembles vertex/index data and generates translated vertex shaders, so there is a plausible capture point. Its scene/frame boundaries and shader variants still need adaptation; this checkout has no game data for visual validation.
- Next: adapt capture eligibility to Agent Under Fire's draw/state conventions, then port the reusable Vulkan acceleration-structure and ray-query pipeline in stages.

### 2026-11-11 - Screen color controls added
- Added persistent Exposure (50-150%), Contrast (50-150%), Saturation (0-200%) and Color temperature (-100 cool to +100 warm) controls to F1 Graphics and the Vulkan final pass. Exposure remains an SDR adjustment, not HDR tone mapping.
- SDK commit: 5162dbf. `git -C sdk diff --check` passes. No build or visual check because this checkout has no game assets.
- Next: continue with renderer-owned temporal/HDR features and investigate scene inputs for the game-dependent effects.

### 2026-11-11 - Lanczos movie upscaling added to the Vulkan renderer
- Ported Nightfire's 4x4 Lanczos 2 enlargement, neighbour clamp and RCAS-style sharpening for large Xbox YUY2 video frames (format 0x24). Agent Under Fire already decodes those frames to BGRA; the pass caches its two outputs per texture and redraws only when the source frame changes. Added a saved F1 Graphics toggle, on by default.
- SDK commit: b129368. `git -C sdk diff --check` passes. Source-path review only; no build or visual check because this checkout has no game assets.
- Next: continue with temporal upscaling inputs, then investigate scene-dependent visual features.

### 2026-11-11 - Enhanced Color and screen-space Toon styles added
- Expanded the F1 style selector to Enhanced Color, Toon (Screen), Retro and Noir. Enhanced Color adjusts contrast/saturation; Toon posterizes brightness and outlines neighboring color changes. These do not port Nightfire's ray-traced lighting or geometry-aware Toon edges.
- SDK commit: 0b65310. `git -C sdk diff --check` passes. No build or visual check: this checkout has no game assets.
- Next: continue with remaining renderer-owned temporal/HDR effects, then inspect Agent Under Fire scene data for the game-dependent features.

### 2026-11-11 - Vignette and animated film grain added
- Added optional vignette and film-grain controls to the Vulkan finished-frame pass and persisted them in graphics settings. The grain phase changes each presented frame. Strength is quantized to 64 steps in the packed pass settings.
- SDK commit: b569525. `git -C sdk diff --check` passes. No build or visual check: this checkout has no game assets.
- Next: port the remaining renderer-owned effects and investigate Agent Under Fire scene data for adaptations of lighting, water and weather features.

### 2026-11-11 - Optional SDR bloom added to the Vulkan final pass
- Added a configurable thresholded screen-space bloom kernel to the finished-frame shader (off by default), with an F1 Graphics slider and saved preference. Enabled the sharp scaling filter in that same final pass when selected. This is SDR bloom; Nightfire's multilevel HDR bloom and tone mapping remain unported.
- SDK commit: 349fd1d. `git -C sdk diff --check` passes. The bloom kernel adds 13 source samples per output pixel when enabled. No build or visual check: this checkout has no game assets.
- Next: continue renderer-owned effects; verify HDR/depth resource support, then adapt game-data-dependent effects.

### 2026-11-11 - Retro and Noir color styles
- Added optional Retro and Noir color grades to the Vulkan finished-frame pass, with F1 Graphics selection and saved settings. These are screen-space looks; they do not reproduce Nightfire's scene-aware toon outlines, lighting or material effects.
- SDK commit: ea96fec. `git -C sdk diff --check` passes. No build or visual check: this checkout has no game assets.
- Next: continue with renderer-owned post-processing and inspect Agent Under Fire scene data before adapting game-dependent effects.

### 2026-11-11 - Nightfire visual feature parity mapped
- Compared Nightfire's documented visual feature set with the Agent Under Fire Vulkan backend and completed `visual-port` changes. Separated renderer features from those dependent on Nightfire game data/hooks; Vulkan MSAA remains unsupported.
- Evidence: source/README review only. No build or visual smoke test is possible because this checkout has no game assets.
- Next: port renderer-owned effects one at a time, and investigate Agent Under Fire depth/light/material data before adapting game-specific effects.
### 2026-11-11 - HD texture packs added to Vulkan visual port
- Added Nightfire-style PNG replacements keyed by the game texture bytes, format and dimensions. F1 settings can enable pack loading or dump original textures to `mods/textures/dump`; user PNGs live in `mods/textures`. Added the WIC image codec and a README note.
- Evidence: reviewed call placement so existing replacements remain fast and pack lookup runs once per texture per frame; `git diff --check` passes. Build and visual checks remain pending because this checkout has no game assets.
- Next: port remaining renderer-wide post-processing and then identify which lighting/effects can be adapted to Agent Under Fire game data.

### 2026-11-11 - SMAA added to the Agent Under Fire Vulkan renderer
- Ported Nightfire's SMAA 1x shader passes: luma edge detection, blending weights and neighbourhood resolve. The settings already exposed SMAA; Vulkan now applies it before the final frame blit and avoids also applying FXAA when old settings enable both.
- Evidence: reviewed shader bindings, transient image lifetimes and command ordering; `git diff --check` passes. Build and visual comparison remain pending because this checkout has no game assets.
- Next: port renderer-wide texture improvements and post-processing in small steps, then adapt Nightfire-specific effects to AUF game data.

### 2026-11-11 - Vulkan-only host renderer port started
- Adapted NightFire's initial Vulkan backend commit onto the AUF SDK history. Vulkan is now the only selectable renderer; D3D11/12 backend sources and ImGui backends, DX renderer link libraries, and the D3D fallback are removed. The Vulkan shader cache now uses an SDK-owned blob helper instead of D3DCreateBlob; DXC remains to compile HLSL into SPIR-V.
- Evidence: source/dependency review and git diff --check; no game assets are present in this checkout, so no build or Windows run has been done.
- Next: build on Windows with Harry's assets, then smoke-test default.xbe, bond_eng.xbe and DRIVING.XBE before considering the SDK pointer for merge.


### 2026-11-11 - SDK at the fork's main 8d9815a: the multi-core renderer on main
- Fork main now holds render-mt2 (measured on Harry's PC on branch mt2: same frame rate, the render thread a third lighter, the picture unchanged), host-read-retry (failed host reads asked again and logged: the map-load deaths off the VM's share), PGR2 1.6.3 and EoN's interp work. AUF's main pins it; branch mt2 is done.
- Then 40a735b: 8d9815a's retried-read log line (disc.cpp, logx::Write) left every launcher unlinked (undefined symbol); 40a735b links runtime/core/log.cpp into launcher_core. launcher_cli links here.

### 2026-11-10 - Release 1.0.2
- Published by Harry: the Hit Flash setting (#7), 16:10 and 3:2 screens (#4), R reloads mounted guns (#8), the launcher's in-place update, SDK 89cafb8. Tag v1.0.2 at 9fef360; packages built on the Windows VM from clean clones with LLVM 22.1.3 (PAL 83,571,087 bytes, USA 78,527,498), their .json checked against the zips.
- Next: the multi-core renderer (branch mt2) once render-mt2 is on fork main; issue #6 waits on the player's logs.

### 2026-11-10 - Issue #7, the driving levels: their hit flash
- Harry's log: Hit Flash off, flash still there, in DRIVING.XBE (the gun of mission 2): the on-foot fix does not reach the driving program. There the flash is a full screen quad through a vertex program, untextured, additive (ONE, ONE). On mis02a's gun its frames were the bright ones (0.3 to 0.74 against about 0.2).
- A draw filter in DRIVING.XBE leaves such draws out when hit_flash is off (gpu::g_draw_filter, set in Init). Headless mis02a: frames over 0.3 41 of 86 with it on, 1 of 80 off. On mis02b, mis07a and mis09 every draw of that kind is the full screen quad (4, 1, 9), so nothing else goes.
- Harry on Windows, 2026-11-10, both fixes (on foot and driving), Hit Flash off: "Working".

### 2026-11-10 - Issue #7 again: the hit flash is the HUD's own white fill
- Harry: the white flash when shot stays with Hit Flash off. Headless in Mediterranean Crisis (Bond shot on the ship), the setflashcolor handler 0xAF9E0 never ran; a full screen untextured blended quad (FVF 0x44, the frame's last draw) came on the hit frames, which the reference pictures show paler. Its source, found by logging every large 2D box with its caller: 0x53840 (from 0x555D4) fills 640x480 white (1,1,1) at alpha [ebx+0x30], skipped at 0.
- Now a hook at 0x539A9 sets that alpha to 0 when hit_flash is off (mapped to French, German, USA). The 0xAF9E0 override is gone.

### 2026-11-09 - Issue #4: 16:10 screens filled (SDK at the fork's main 89cafb8)
- The SDK session's aspect-16x10 reached fork main with Harry's go (with the all-games candidate: EoN's line, per-size colour targets, F1 placement on resize). AUF sets `.fills_narrow_screens = true`: on a 16:10 or 3:2 screen the console reports a 4:3 TV, the views widen by FrameAspect / 4:3 (1.2 on 16:10) and the 2D stays 4:3 in the middle.
- Harry tested that branch on Windows at 1280x800 ("fine"); here smoke, and smoke with OUTPUT=1280x800.

### 2026-11-09 - Issues #4 and #6: waiting
- #4 (16:10, the Steam Deck): the SDK session's aspect-16x10 (89cafb8, GameInfo::fills_narrow_screens) is tested on AUF headless at 1280x800 and on Harry's PC ("fine"). It reaches fork main after Harry's go on the all-games candidate; then AUF sets `.fills_narrow_screens = true` in GameInfo order. A smaller fill_from of mine stays unpushed on local branches fill-1610 and sdk fill-narrow.
- #6 (USA save, the driving mission's result not kept): the player's save (00 Agent) records Trouble in Paradise only (0x38 = 1), and Select Mission opens on Precious Cargo. Each program rewrites auf.log, so the driving session's log is gone. USA and PAL relaunch bond the same way with the same launch data, and US bond takes PAL's driving result as PAL bond does (both go on to mis02b). Not reproduced: no headless way to finish a driving mission. Asked for the steps and the logs right after the driving mission.
- Also from the SDK session: a latent race in bond's first map load (sub_165D40 reads [0x374404], set at 0x15E9E6 in 0x15E5D0, cleared by 0x165E60), seen on render-mt2. To look at.

### 2026-11-09 - Issue #7: a setting for the white flash when hit
- Taking damage, the player's code at 0xBB4A6 posts setflashcolor 1 1 1 0.3 (event 0x334110, handler 0xAF9E0 by the response table at 0x215C60); 0x71110 runs the handler at once, so the damage code's return 0xBB4F4 is at esp+0x18 there.
- `hit_flash` (Game tab's Hit Flash, settings.toml, AUF_HIT_FLASH headless), on by default: off leaves that one call out; scripts' own setflashcolor stays. Mapped to French, German and USA, each address reading as English's.
- Not seen headless: no run got Bond shot (Bad Diplomacy's pale frame was the mission-failed fade; Mission 1's walk never moved). Harry's test decides.

### 2026-11-09 - Issue #8: R did not reload a mounted gun
- The gun's controls (data\Control\povcfg0.def in each mission's .viv, RefPack): GAMEACTION_POVRELOAD is +button0, A; fire is the right trigger, aim the left. src/common/menu_mouse.cpp sent R to X on a gun, and clears A there (the left button fires).
- Headless, PAL mis02a: an empty magazine (0 - 400) reloads by itself when fire is let go; with rounds spent, X left 47, A filled it to 50.
- R now presses A on a gun. A controller's A reloaded all along. Keys cannot be held headless: the evidence is the pad test and the file.

### 2026-11-04 - SDK at the fork's main 97b1654
- The launcher updates an install built the old way in place to the prebuilt one (play\.next staging, old build tools offered for removal).

### 2026-11-03 - 1.0.0's crash on the USA disc: my end mark landed on the start index
- A USA player's dump (Ryzen Z1, 7.7 GB): read of guest 0x2C0 in AgentUnderFireBond.dll, esi = list + 0xFFFF * 12 (D005FD34 + BFFF4 = D011FD28). 0x14B22B's caller keeps 8 entries from esp+0x18 and the start index word at esp+0x80: entry 8's key slot (0x20 + 12 * 8). With 8 parts the mark (2026-11-02) wrote 0xFFFF there and the call began 65535 entries on. So it can happen on either disc.
- Fixed: the mark only for a list on the stack, and never over the start index; a log line when it is left out. Headless, US Bad Diplomacy from Harry's save and the ISO's files (all 318 read identically by the disc reader) ran 3 minutes without a crash before and after; the full-list case did not come up headless, so the evidence is the dump and the caller's layout.
- Needs a 1.0.1: the exes are in the packages.

### 2026-11-02 - v1.0.0 published; README lists its files
- The SDK session reports v1.0.0 published. README says what each asset is and which one a player downloads (sizes and the 111 s Sandbox PAL install as that session measured them).

### 2026-11-02 - SDK at the fork's main 5469868
- launcher.log filtering and one log line reworded (the SDK session's prebuilt-tidy).

### 2026-11-02 - The prebuilt model (branch prebuilt; SDK at the fork's main 2933e64)
- On Windows each program's code is now a DLL built on the player's PC from a package holding the runtime and compiler, no game code: `XBE_GAME_DLL` in CMakeLists.txt, `library` in the hand configs (map_bond.py names them for French and German), `XBE_GAME_CONFIGS` this disc's configs, game.json `packages` by default.xbe's SHA-1, release-package.bat at the repo's root (a zip per disc, each from a checkout holding that disc). The harness still links the code in.
- Evidence here, all eight programs recompiled: every override, `__imp__` call, direct call and hook is in its exports.def, imports.def or hook_exports.h. No Windows link or run from here: next is Harry's build, package and launcher on each disc.
- Harry's PC, both discs through the launcher with a package beside it: PAL "works fine" for hops, saves, frame rate and input, French and German; USA "all working now". PAL: 82 MB package, 49.8 s install (unpack 1.5 s, compile 48.2 s, 141 files). Those builds carried pointer.cpp's fix, committed here: an overridden function's forward declaration needs X86_FUNC in DLL mode, X86_EXTERN_FUNC does not compile.
- Still to come: both packages rebuilt from the committed branch, a clean PC test, the real download through the launcher (never run), the tag.

### 2026-11-02 - USA through the launcher: configure stopped on the icon
- Harry's launcher log: the Windows-only icon step named the French and German targets, which a USA build lacks. Skipped when the target is absent. The Linux configure never runs that step, so smoke could not see it.
- Harry, 2026-11-02, after the fix: "USA works now", built through the launcher on Windows. A full USA playthrough and multiplayer not done. Harry's crash fix also confirmed ("Bugs fixed").

### 2026-11-02 - A crash after Mediterranean Crisis's save: the game's own over-read
- Harry's dump: read of guest 0x2C0 at 0x14AE52. 0x14AD30 batches a model's parts and at a batch's end reads the next entry's key with no check of the list's end; 0x14B22B's list is on the stack, where the entry past the 5 parts held the same stale key and no material (dump: list at D005FD34, esi at its end D005FD70, key 0x1000ED). src/bond/hooks.cpp puts a key no part has past the end during the call. Not reproduced: it needs that stack content.
- Gate for the launcher's release_xbes: PGR2, PGR1 with repro, NightFire, True Crime pass; EoN's pin conflicts as before. PAL smoke 4 OK. USA headless 180 s from title into the first mission, no trap.

### 2026-11-02 - The USA disc (branch usa, sdk branch usa-disc)
- USA: default.xbe, bond.xbe, Driving.xbe, English, 60 Hz. The same code as PAL's with blocks moved: `tools/map_bond.py usa <folder>` writes src/{game,bond,driving}_usa, addr_usa.h and config/*_usa.toml; `Pair(moved=True)` allows reordered blocks (Driving.xbe 413687 instructions aligned). CMake chooses the programs by assets/default.xbe's SHA-1. src/driving now takes its data addresses through A().
- Mapping fault fixed: a code address no run covers fell to "a number nearby", landing inside an instruction. Every config entry, table entry and function was then compared with PAL's by its instructions, numbers masked. Bond and default all agree. Two in Driving.xbe do not, on purpose (the tool leaves them out): no SetGammaRamp function; the pause draws its lit box from the tab's widget so TabPlaces has nothing to move. kSeconds and kRates are PAL only; the tick and the slow motion hooks stay.
- Scans: `build/linux/xbe_symbols assets/<xbe> sdk/xdk --out config/<x>_usa_game.toml`, then `out` set to generated/<x>_usa (the game_usa, bond_usa and driving_usa folders).
- Launcher (SDK): game.json `release_xbes`, SHA-1 to that disc's xbes. launcher_cli took USA's three and PAL's five; PGR2 and NightFire unchanged.
- Evidence headless, assets a USA folder: movies, relaunch to BOND.XBE, menus with PC Settings, Mission 1 in play; mis02a on the gun, mis02b in the car; pause tabs and RECOMP page with its lit box. Not checked: a Windows build, multiplayer, keyboard prompts on USA, a USA playthrough. The disc's extracted copy here is from Harry's zip.

### 2026-11-02 - Mediterranean Crisis's frame rate: runs of alike draws merged (sdk branch merge-draws)
- Harry's log there: normal stretches about 1300 draws a frame, slow ones up to 16061, 27 FPS, 35 ms on the render thread and all of it per
  draw cost, at any render scale. Headless route: Load Mission, Yes, Select Mission opens on the save's mission (.local/med.txt).
- The mission's first view: 8423 draws, 68000 vertices, about 8 a draw; the title draws its world surface by surface.
  7621 of 8426 had no state command since the draw before.
- `GameInfo::merge_draws` (bond images, not DRIVING.XBE, other games off; XBE_MERGE_DRAWS=0/1 overrides): the render thread's recording joins such
  indexed triangle lists and strips into one list. Left alone: 2D, vertex programs, visibility tests, flat shading.
- Evidence: 8426 to 814 draws in that view, whole run 12.66M to 1.46M; vertex upload 16 to 10.5 MB a frame; reference pictures match but for sea and sway;
  smoke 4 OK. Harry on Windows, 2026-11-02: "Significant improvements now."

### 2026-11-02 - Merged: driving's speed, steady windows and cars
- All-games check on the SDK: AUF, PGR2, PGR1, NightFire, True Crime pass; PGR1's repro shows a live race when run alone (under load it ended on car select: its fixed delays). EoN's pin conflicts as before, not run. Fork's main and this repo's main moved. Left for v1.0.0: public repository, a launcher run without a GitHub login, then the tag. Next: USA support.

### 2026-11-02 - Above 60 accepted with a known issue
- His 240 FPS recording of buildings: 51 of 2554 distinct frames have a patch going back and returning, at most 5 small patches. One black shard for one frame (real debris or not, unknown). Signs and posts move unevenly, about 0, 60, 30, 10 pixels between frames: no frames can be shown while the title makes its next step, then the picture catches up. Likely what is left of the flicker; not proven. A proper cure is replays on a thread of their own, for later. Harry accepted it as a known issue in the release notes.

### 2026-11-02 - Car flicker: a build to split it on his PC
- Harry chose the diagnostic round. XBE_INTERP_PIECES=0 turns off the logic for pieces come into view; XBE_INTERP_LOG=1 writes "jump:" lines for draws at a car's distance moving far on screen between the two steps blended.
- The log showed it headless at once: at one step all 51 pieces of the car were new together, each given the camera's move. A program now needs 4 matched draws for each new one to count as scenery.
- Harry: most flicker gone, some left; his log (1400 frames for 600 steps, no warnings) cannot name it, a draw left unmatched never reaching the jump lines. A car's pieces show several vertex counts (a change of detail would lose every match with the object in the same place). A lost draw looked for a draw of the step before with a matrix all but equal: it reached the buildings, wrong partners nearby. Taken out.
- The count that found it: on his PC 35000 to 54000 draws per 600 steps without a match, up to 406 in a step. Almost all had a draw of the step before alike in program, target, count, start and texture: only the vertex buffer's address differed, the title handing meshes other buffers. The key no longer has the buffers. All the handling of pieces above was treating this.
- His log after it: cars whole, flicker better, not gone; 10000 to 13000 without a match, up to 451 in a step. Headless those big steps are a 256x256 viewport every 10 steps or so: the reflection picture, redrawn now and then, so with no self in the step before. Not a hop of the scene, and they account for most of the count. So lost matches are no longer the likely cause of what is left, which is not found.

### 2026-11-02 - Lit windows jumping above 60
- Harry: speed fix fine in play. Window lights: not at a cap of 60, on the blending side, and they shift position very briefly. Counted headless driving: about 3% of world draws have no partner in the step before (come into view) and their own matrix, so they sat at the step's place with the rest behind. Such a draw took the scenery's move: windows fixed by his report, but cars flickered, a car's pieces moving with the camera. The nearest matched draw of its program did no better: his 240 FPS recording shows the car in pieces for about 3 frames at a time, a wheel's spin being in that move. Now only scenery derives: the camera's move, when the neighbour moves that way and nothing of that program turns. Better, not gone: his next recording still shows wheels and exhausts out of place; the test for alike was a fixed 0.02 and a step's moves are all that small. It is now a tenth of the camera's move, and a program counts as scenery only when most of its matched draws go with the camera: counted while moving, the car programs fell from about 140 to 0 and 2. His next recording still has a black bar on the plate, detached exhausts: the generic quad programs B0101931 and B0101991 pass as scenery and draw a car's plate, glow and shadow. Fewer than 24 vertices no longer derive. Some frames show the whole car smaller for a moment, not explained: asked whether at a cap of 60 too. Six tries on this; the windows are what it was for.
- Harry on 37f27b1: at a cap of 60 the car never jumps, so all of this is the blending; at 144 the vehicle still flickers and the lights rarely. Stopped under the three attempts rule. Logged headless how far each draw's origin moves on screen between its two steps: the large moves are scenery near the camera, expected, and no car draw among them. So not reproduced here: the road was driven, no enemy hits, and a car's break-up has never been seen headless. What is thought: car pieces with a wrong partner or a wrong move, for one in-between frame; whether the piece logic is needed for it is unknown, cars being reported only after it. Next, for him to choose: a build with the piece logic switched off to split the two, with the same log on his PC, or driving capped at 60 for v1.0.0.

### 2026-11-02 - Driving at 60 at the right speed
- Harry chose the speed fix, and USA support for v1.1. USA's Driving.xbe has 60 ticks and 1/60 where PAL has 50 and 1/50; plain tuning data in the .viv files is the same (differences: compressed models, text, PAL/NTSC HUD and movies). src/driving/hooks.cpp sets 77 constants of 1/50 and 5 of 50 found by xbe_port with high votes, and two arguments. The 288 and 576 that also differ are screen heights: left alone (they were first listed here as timing, wrongly).
- Evidence: holding the gas in mis02b, the same real time puts the car in about the same place at the original 50 ticks and at 60 fixed, and clearly further at 60 unfixed. Not done: mounted gun's mouse rate was measured at the old speed, to check on Windows.
- Window lights flicker (Harry's video, cap 240, vsync off): not found. Frames show steady windows moving; shader registers between matched draws change smoothly (almost no jumps over 5%). Asked for a test at a cap of 60.

### 2026-11-02 - SDK at the fork's main: the launcher's new look
- The launcher in classic Steam's style reached the fork's main (d509dad), with the settings window's palette shared; the game code DLL stays on the prebuilt branch. Harry to run the new launcher.

### 2026-11-02 - README: support and tips
- Discord link as the other games' READMEs have it; a tips section with the vsync advice, the logs and the launcher's. SDK already at the fork's main.

### 2026-11-02 - Steady frames merged
- Harry on frames-steady: counter holds, wheels better, walls and HUD fine, the gun fine; his log, cap 240, vsync off: 1831 frames for 598 steps. All-games check on the SDK: AUF, PGR2, PGR1 (repro shows a live race), NightFire, True Crime pass; EoN's pin conflicts as before, not run; the change is interp.cpp alone, which EoN does not use. The launcher logging commit that reached the fork's main meanwhile merged in, AUF smoke again. Fork's main and this repo's main moved; README shots and the release notes come with it. Left: public repository, a launcher run without a GitHub login, tag v1.0.0 on Harry's word.

### 2026-11-02 - README: his labelled shots
- Retaken at 1920x1080 from one spot, each file named by its setting: slider (off, on), edges, sharpening, SMAA. Checked by eye against the labels. Smooth alpha edges and 16x filtering show next to nothing at this spot, so no picture of them. RECOMP tab with the new rows; the gun. "On" is render scale 200%, MSAA 4x, sharpening 50%.

### 2026-11-02 - Steady frames above 60; wheels
- Harry: built with the launcher, so the launcher works. Video: wheels pop out from behind the car; the counter sits at 60 with spikes past 100, vsync off. His log: 600 frames for 600 steps with a 722 now and then, so the back-off from one late step had put the limit at 1. Now late steps must keep coming (about 1 in 10, held), recovery after 120 steps, 1800 at most. Headless, no vsync, firing rockets: 1800 frames for 600 steps. Simulated vsync at 100 and 144 Hz still ends at 600 steps, after some 10 to 20 s of trying.
- Wheels: a turning draw whose place moved more than a tenth counted as a different draw and was left at the step's place. The leap in the matrix's own units now decides alike for all. Not seen headless: for his build.

### 2026-11-02 - Release prepared, not published
- release.bat left the French and German exes out of its zip: added. README: Getting started with the launcher. docs/releases/v1.0.0.md drafted. launcher/game.json was right already.
- launcher.bat still had the template's GAME_SHORT=Game: the launcher built, its copy failed and the script said it had failed. Set to AgentUnderFire.
- Blockers, Harry's: the repository is private (404 without a login) and the launcher fetches it from GitHub without one. The launcher has not been run for this game. Tag v1.0.0 only on his word.

### 2026-11-02 - Past 60 merged
- Harry chose to merge without EoN's smoke; the fork's main fast-forwarded to 7d32328 with the feature, this repo's main to it too. EoN is to be checked when its SDK line is reconciled with main. Next: launcher and release, his new screenshots.

### 2026-11-01 - All-games check of the SDK branch frame-interp
- Throwaway clones, each game's pin merged into the candidate 7d32328: AUF 4 SMOKE OK, PGR2, PGR1, NightFire and True Crime SMOKE OK; PGR1's repro.sh shows a live race. EoN not run: its pin e644a6e conflicts (launcher, handover, boot, and overlay, d3d11, d3d12, d3d_common, main from the fork's newer work), none of them files this work changed beyond what the fork's main brought. The feature is off unless a game sets GameInfo::step_interpolation.
- Not merged: waits for Harry on going to the fork's main without EoN's smoke.

### 2026-11-01 - RECOMP rows for the new options; README shots
- Harry: vsync fix confirmed; asked for the new settings in game. Graphics page: render scale, MSAA, edge smoothing (off, FXAA, SMAA), smooth alpha edges, filtering, sharpening, scaling filter. A new Picture page: sharp text, enhanced textures, frame counter. In three languages; "60 FPS on Foot" is "High Frame Rate". Seen on the driving pause headless, pages step.
- README: his shots for on foot, driving, gadget, mansion, keyboard menu and the pause; slider from his railing shots, 7 as all off (assumed, first of the set) and 10 as everything on (his word). His other two railing shots are not labelled, so the edge and sharpness comparisons stay the older ones.

### 2026-11-01 - In-between frames: flicker open, stopped after three attempts
- Harry: textures flicker or shift whenever the frame rate is above 60, vsync on or off; still there on c0c59d8. Below or at 60 the blending is off and it is fine. Branch driving-interp, sdk frame-interp; not on main, and fork main untouched.
- Tried, each cured what it aimed at but not this: (1) motion follows the clock, not squeezed into the time left; (2) a draw with no match follows a matched one of the same matrix (capture 201230 shows one world matrix, so that part is proven); (3) one timeline, no step shown as drawn among blended ones.
- Other things already fixed in this work: scrambled HUD (per-draw headers, kept vertices), flying radar arrow (matching).
- Not reproduced headless: the reference renderer's frames look right and static HUD text changes 0 pixels. So either it needs D3D12, or it is in what blending does to registers beyond the matrix, which a still cannot show.
- Suspects, untested: all 192 registers are blended, not only matrices, so anything per-draw that is not linear (fog, light, texture scroll, flags kept as floats) is moved; D3D12 handling of a surface presented several times, or of a target drawn again within a frame (the reflection target sampled while drawn again; "sampled target reloaded"); the live draw plus replays of one step leaving state the later replay sees; world vertices over 64 KB in dynamic buffers read as the next data.
- Harry then narrowed it: only certain walls, such as the buildings ahead as you drive past. So specific draws, not timing, and the three attempts were aimed wrong for this symptom. Layers split by different partners (z-fighting) checked and ruled out: 0 of about 330 draws in moving frames have a partner differing from others of their matrix. Amplified frame differences headless show nothing on walls, though that run sat at a wall and did not drive past buildings.
- His log (c0c59d8, cap 144, vsync on, D3D12): 1087 frames for 599 steps, 596 blended; no "too long". So the timing and matching are doing what was meant.
- Direct3D 11 flickers the same on his PC, so not the backend. Fourth look, from his "buildings ahead as you pass": the "far" test used each matrix register against its own size, and a translation crossing zero as an object is passed looks like a leap, so that object was left at the step's place among blended ones. Counted headless, old rule against new: 414 world draws against 3 in one 40000 draw window of driving. Far now judges turn and scale against the row's size, and the translation only by a real leap (16 units). Wheels and debris, really far in their turn (7% of world draws moving), now blend their place, not their turn; before they sat out of line with the car. Evidence is the count: whether the walls settle is for his build.
- Harry: walls mostly fixed; other bugs in a video. White wash: seen headless with blending off, so it looks like the game's own flash on firing. Streak: appears while firing rockets, may be a trail, not confirmed. Font block ahead of the car and a scrap of font top left: only with blending. His capture 194153 has the HUD's icons at [0 0 28 28], placed by the matrix alone; the walls' leap of 16 units let them pair with each other again, across the screen being about 2 units in a HUD matrix. The leap is now 4 times the row's own size. Font block not reproduced headless.
- Harry: wall flicker down to about a tenth, acceptable. Letters flash as vehicles take damage. Capture 210919: the reticle's quad with the font atlas, the weapon label's letters with the radar's matrix; the radar gone. Replay's pool of 4096 kept vertex buffer headers ran out, each draw using one for every stream set (draws in capture 210911: 1756), so the last draws, the HUD, read what the game had written after them. Proven: a pool of 1200 headless gives the same picture (radar gone, label a stub, scrap of font top left). So the earlier "scrap gone" credited to the HUD leap was just being under the limit. Pool now 65536, vertices still 8 MB (1.3 MB at most headless; 32 MB did not fit the host contiguous space and stopped the game, in f194133, pushed for a few minutes); a step over them is drawn as it is and logged, and no space means no blending.
- Harry: damage text fixed, walls fine, no "more vertices" in his log. The gun feels laggy: his log has 117 frames a second and steady steps, so delay, not frame rate; blending shows a step late, up to 17 ms. GameInfo::step_ahead: on a gun the step is shown at once and the frames carry on past it (blend above 1), at most a step too far when the aim stops. The car stays behind, which is cleaner. Feel is for him.
- Harry: stutter on mission 2 above 60, not with vsync off. His log, cap 240, vsync on: 215 steps in 10 s, frames up to 194 ms. The render thread waits for the screen, its queue fills, and the title's present waits too. Reproduced by sleeping in the null backend's Present at 100 Hz: 1000 frames for 484 steps. A frame's time now includes its present, and steps coming late lower the frames a step, raised again after a calm that doubles each time (240 steps to 7200). Headless at 100 and 144 Hz and without: 600 steps. Over-careful with vsync: simulated 144 Hz settled at 600 frames in 10 s for a while. Vsync off gets the most.
- Next, if Harry wants it: headless is blind, so switches for his build, one change each: blend matrix registers only; skip the reflection pass in replays; D3D11 in place of D3D12. Each answer narrows it.

### 2026-11-01 - Driving and the gun past 60: in-between frames (sdk branch frame-interp)
- Harry: driving and mission 2 above 60, whichever way is best regardless of time. Time-based physics was ruled out: steps are built into 104 places of 1/50 and all tick counts, and handling would change. In-between frames: sdk runtime/gpu/interp.cpp, opt-in by GameInfo::step_interpolation (DRIVING.XBE only, with the uncapped option, when the cap is past 60).
- Each step's clears, draws and copies are recorded as they pass to the backend. At Present the list is replayed N times with vertex shader constants blended from the same draw in the step before (matched by what it draws, in order), then at 1, so the picture is up to one step late. No blend after a camera cut or when nothing moved.
- What a replay needs that memory no longer holds: stream, texture and palette headers (one vertex buffer repointed per draw: the HUD scrambled) and vertices up to 64 KB (one small buffer rewritten per draw with a wait: the reflection's first quad vanished and the HUD lost its radar). Kept per draw and fed from a two-step region of guest memory.
- Time: 6 ms drawing, 2.5 ms a replay headless. Frames are as many as fit before the next tick, so the game never waits: 1202 frames for 601 steps at 120, about 2360 for 595 at 240. XBE_INTERP_TEST=N draws N a step for snap bursts; three of them at mis02b show the full HUD, radar and even movement (RMSE 0.032 frame to frame).
- Harry's video (mis07a, Unlimited, 3504 frames for 500 steps): the radar's objective arrow flew about the screen. HUD pieces share a texture and buffer, and their number changes, so one was blended with another's matrix. Draws now also match on viewport, and a matrix register moving more than a quarter in a step means a different draw, not blended. Reproduced headless with the old matching (arrow beside the car every third frame), gone with the new.
- Harry: streets, textures, HUD and icons shake. Frames were squeezed into the time left after drawing and ended on the step, so the picture ran ahead for about 8 ms and stopped for about 8 ms, 60 times a second. Now the blend follows the clock over the whole step and the last frame stops short; the next step's first goes on from there. HUD text in a headless burst changes 0 pixels frame to frame, so a shaking HUD is not explained by this and is to be seen on his build.
- Harry: textures shift whenever above 60, vsync on or off; video. Not pacing. His capture 194124 has 26 draws with the world's matrix at translation 89.1452 and 19 at 89.1117 in one frame: pieces of the street at different moments. Pieces that come and go had no match and stayed at the step's own place. A draw without a match now follows any matched draw of the same matrix, register by register; of those alike, the nearest matrix is taken. Headless moving: about 21 draws follow, those left alone are HUD quads (the game gives each its own buffer). Whether the shift is gone is for his build.
- Harry: flicker remains; capture 201230 has one main world matrix, so the pieces agree now. His log: 708 frames for 538 steps at a cap of 144, some steps not blended (F3 pressed often in that run). Blended frames run behind the step, a step shown as drawn is ahead: every switch between them moved the whole picture forward and back. Now every moving step is blended, however few draws match (only a cut or no matrix match shows it as drawn); a machine whose replay takes over 0.6 of a step gets none for 2 s, logged. Headless 600 steps, about 600 of them blended, at 144 and 240. Inference from the log, not seen on screen: for his build.
- Not known until Windows: D3D12's cost at his 6880x2880, presents of one surface in a row. World particles written to dynamic buffers stay at the step's place while the camera blends. Slow motion stays held at half for now.

### 2026-11-01 - On foot unlocked: the frame rate cap goes past 60
- Harry wants unlocked FPS before launch. The cap could not pass 60: a present interval of 1 counts in 60 a second (now app::g_pace_hz, SDK at the fork's main 8de73e5).
- That alone ran the game fast: 0x25CA0 measures the frame's seconds, then takes 0x138760's, 1/60 a frame at any rate; 140 frames a second gave 11.6 s of game time in 5 s. An earlier note here said it ran off the clock: true only at or below 60. 0x138760 now returns the measured time, up to a tenth of a second.
- Evidence, headless: game time equals wall time at 60, 144 and unpaced (dt 0.00695 at 144). Holding forward 4 s in the first mission ends at the same spot at 60 and 144, same picture. No crash in any run.
- Not done: driving stays at 60. Each step draws a frame and speed follows steps, so more frames need in-between frames or the physics made time-based. Combat, doors and scripts above 60 not played; Harry to play.

### 2026-11-01 - Driving radar's road lines back on wide screens
- Harry: the minimap has lost its lines. Reproduced headless at 16:9: draw of the map texture (P8 256x256, the road lines) wrote only from x 477, the disc sits at 438 to 517. The map's quad is larger than the radar's viewport (477,48,135x135), so gpu::DrawSpan takes it for a pass filling its viewport (kSpanViewport, EoN's Thermovision) and leaves it unsqueezed, clipped by the unsqueezed viewport, while the rest is squeezed whole. Depends on the map's turn, which is why the earlier radar fix looked right.
- Fixed in the game, no SDK change: src/driving/view.cpp sets State::squeeze_beyond (shift 0) for the radar 0xB9010. Seen on screen headless at three frames in mis02b. D3D12 takes the same flag; not seen on Windows.
- Worth doing when the SDK next goes through the gate: kSpanViewport should want a quad about the viewport's size.

### 2026-11-01 - Driving slow motion held to half speed
- Harry: 60 FPS feels great, slow motion drops to 15. He chose half speed over no slow motion or in-between frames. Event 0x466D0's scale is held to at least 0.5 (src/driving/hooks.cpp), logged as the scene asks.
- Tested by calling the event with 0.25 at frame 1200 in play, headless. The real scene's value is not read yet: the log line will show it on Windows.

### 2026-11-01 - Driving at 60; slow motion explained
- Harry: only the slow motion parts of cut scenes are low, and asked for driving at 60.
- The engine runs one step per tick and draws a frame a step. Ticks: 0xE0540 asked for 50 (push at 0x5736D), period 1000 / rate in whole ms, callback 0xE0500. Scheduler 0x587F0: steps = ticks x scale (+0x20), so slow motion steps less often: 40 and 60 ms frames, exactly Harry's log. The console's slow motion has the same few frames; drawing more would need interpolating the world.
- Now 60 ticks (src/driving/hooks.cpp, the uncapped option, AUF_30FPS keeps 50), 1 ms timer with the tick let through when due, because 1000 / 60 = 16 ms would be 62.5 a second. Measured headless: 60.00 a second in play.
- It plays a fifth faster: at equal frame numbers, 50 and 60 ticks put the car in about the same place. Tick counts (timers, mission limits, the clock's 1/50 at 0x13ACA0) move with it. Speech and music are real time, so cut scene lines may overlap or be cut. Not seen on Windows.

### 2026-11-01 - Cut scene frame rate: measured, not fixed
- Harry: low frame rate on cut scenes. His log's only sustained slow stretch is a driving mission's end cut scene, F3 capture at 244 s: 77 of 347 frames over 25 ms in 10 s, longest 62.5 ms, 258 draws, render target 6880x2880. On foot the slow lines are short (15-22 frames near 45 ms).
- Driving runs at 50 frames a second (500 in 10 s, exactly 20.0 ms headless), not the 60 the log's "paced" says and earlier notes claimed. Frame times land on 20 ms steps, so an overrun shows as 40 or 60 ms. Where the 20 ms comes from is not found: no caller of BlockUntilVerticalBlank, and present returns at once.
- The mission's start headless: 40 ms frames with 1 draw are the loading picture, not a cut scene; the intro after it holds 20 ms. So the end scene's spikes are not reproduced here, and which part is slow (game or GPU) is unknown.
- Next: Harry to say which cut scenes; reach an end scene headless with frame timing.

### 2026-11-01 - Front end's Back prompt shows Backspace on keys
- Harry confirmed the quit fade no longer flickers. His shot of Select Mission: ENTER drawn, but the red B still there.
- That B is not in the atlas: a 32x64 DXT3 picture of its own, beside the A's, FNV 0xE8116B93FAE4A5F7, the same in all three languages. It now joins the known pictures (src/common/buttons.cpp); pictures of one shape are told apart by hash.
- Evidence: the log says it was drawn as keys, and AUF_BUTTON_DUMP shows BKSP. The reference renderer keeps the disc's look on this page for the A too, so it can't show this; the Windows exe does, as it does ENTER. Needs Harry's eye.

### 2026-11-01 - The flicker: the pause menu on and off in the quit's fade; a wrong diagnosis first
- First reading of Harry's video was wrong: frame 280's desktop is him closing the game (the log's window closed 1.4 s after the relaunch), not a gap in the handover, so SDK branch handover-gap fixes nothing he saw. He reported it still flickers. Read by brightness alone, I had skipped the frames that matter.
- Compared frame to frame: during the fade after Quit (frames 176 on) the picture alternates between the pause menu showing and not. Reproduced headless with a snap burst (SNAP_FRAMES, saved as snap_N.tga in the run folder): no menu, menu, menu, no menu. His F3 capture of the fade has one draw, a full-screen quad: the draw has stopped, and three buffers darken as they turn. The pause flag clears a frame before it stops, so one holds no menu.
- The fade (0xB7B60, 60 steps) draws nothing but a black quad over the back buffer, and the three buffers hold the last three frames: with the menu, without, and the older one. Now each step starts from a copy of the picture on screen (src/driving/view.cpp), with the quad's brightness made relative to the last step so the fade still reaches black in the same time; after it, the other buffers take the dark picture. Snap burst's mean brightness: 51.3, 50.5, 16.4, 10.4, 5.4, 1.1, 0.7 (was 50.5, 17.5, 35.5, 9, 5.1, 2.3, 0.6, 5.1) and the menu does not return. The menu still goes once, for the game's last two frames before the fade starts.
- Runtime detail that cost a try: after Swap the picture on screen is GetBackBuffer(1) on a three-buffer title, -1 is the oldest.
- The SDK handover-gap change was the wrong diagnosis and is dropped: SDK at the fork's main 38f3033 with the redone settings window, without it. Checked with the reference renderer only; the D3D12 and D3D11 copy paths are not run here.

### 2026-11-01 - The strips' tab reads RECOMP
- Harry's shot: on foot "PC Settings" ran off the strip; he asked for Recomp. Now RECOMP on both pauses in every language; the main menu's row keeps its name. The Briefing label stayed coloured under the tab: a label's colour is four floats at +0xA4, and only the first was changed.

### 2026-11-01 - PC Settings' tab opens on the strip
- Harry: right into the tab landed on the Page row, whose left and right change the page, so no way back by keys. It now opens with no row chosen: left the tab before, down to Page, up from Page back to the strip. Foot: the page's current item 0 (pointer.cpp guards it); driving: the cursor -1. Back goes to the tab before too, in the driving pause by the game's own step. The main menu's page opens on Page as before: it has no tabs.

### 2026-11-01 - README: the tab's shots, a better sharpening comparison
- Harry's new shots replace the old page's; the sharpening crop is now a door's hinges and carved lines, not the blurry distant city he pointed out.

### 2026-11-01 - The driving pause's strip laid out by measure
- Harry's shot of Dangerous Pursuit: "PC" over "MAP", the Map box lit. Tabs are now measured with the game's 0xB3740 once in the draw, and closed up if the row would not fit (Map tabs, long languages); the pointer's boxes come from the same. The draw spaces its own labels (0xBBF50) into the stack, so the hook at 0xBBFAB writes ours over them; moving the widgets did nothing. The lit box (0xBC043 on) goes by the current tab's label, so with the page open +0x110 holds ours from 0xBBC5F to 0xBC730 and the real Map label is drawn after.

### 2026-11-01 - Harry's tests of the tab; clicks back off it
- On Windows: PC Settings tab, its rows, pointer, values, the settings applied at once and at the next start, keys, Yes and No, the icon, all good; the strip's look he does not mind. Not reported: a level with a Map tab, the later missions, multiplayer.
- Bug: from PC Settings a click on another tab did nothing, as the open page returned before the strip. Fixed on foot and driving.

### 2026-10-31 - PC Settings is a tab along the pause's top
- Harry: a tab on the strip, not a row on the Mission page. The main menu keeps its row (it has no tabs).
- On foot: a page's plain texts (+0, +8, 0x278 each; x centre +0x90, y +0x94, red +0xA4) hold the tabs at y 113; one is added beside Briefing on each page. Right from Briefing or a click opens the page over Briefing's arrays, with only the strip's and footer's texts kept and the strip's lit bands moved off. Clicks on any tab step there with the game's own left and right.
- Driving: a label drawn after the draw, "PC" when the level has a Map tab, else the full name. +0x420 is 4 from 0xBB1B1 (empty panel), 3 from 0xBBC5F (strip, footer: their widgets exist for it; 4 crashes there), 0 after. Right from the last tab or a click opens. Seen: strip, rows, a value changed, Back. The Map case not seen: mis02a has no Map, so "PC" at 600 is unchecked.
- The tab bar's inner tabs stay row 0, "Page".

### 2026-10-31 - PC Settings in tabs, on every menu
- Harry: more settings in game, tabs, in the pause menus too. Row 0 of the page chooses the tab, Graphics, Display, Controls, Game (a tab bar would need new art and space, and the pause already has its own tabs). Rows from src/common/pc_rows.cpp, so main menu, foot pause and driving pause are the same. Each row changes what the settings window changes; display mode posts ApplyDisplay as it does. Those marked * apply at the next start. Key rebinding stays in the settings window (More Settings on Controls and Game).
- The pause row reading Continue Mission: the game refills its texts as a page comes up; the row's text is set each frame.

### 2026-10-31 - README: status, comparisons from Harry's shots
- Shots at 3440x1440 differ by a measured 24 to 32 across and 68 to 76 down, so crops are offset by that. His file names give the settings; the sharpening percentage in "4x-sharpening-200" is not known, so the captions do not state it.
- His agent-menu.jpg shows the on-foot pause's fourth row reading Continue Mission, not PC Settings: a bug to fix with the tabs. agent-menu-2 (the page) is used meanwhile.

### 2026-10-31 - SDK at the fork's main 37fa3dc: MSAA and FXAA apart, sharpening, its language setting
- Harry asked for the pull, for the new features' screenshots. The page's rows now: field of view, mouse sensitivity, render scale, MSAA, FXAA, sharpening, texture filtering, more settings: eight, so it still fits the pause panels. Sharp text and the frame counter are in the settings window only.
- Language: GameInfo::languages {1, 3, 4}, the settings window's own; the hook of XGetLanguage and my option are gone (an old language key in a settings file is ignored; anyone who set it picks again). auto_fills_screen on.
- Screenshots of those for the README want the real renderer: the reference renderer does not do MSAA, FXAA or sharpening, so they are Harry's to take.

### 2026-10-31 - A and X were NightFire's way round; the icon and README picture
- Harry: R crouches, Ctrl is action. This game's A is action and X crouch, the reverse of NightFire's, whose table the keys were copied from; the README's table was right and the code was not. Swapped. Tried in play here: White (Q) cycles the weapon; X, Y, B and Black show nothing where the harness stands, so those rest on the game's chart and his play.
- launcher/icon.ico and docs/img/agentunderfire-pc-port.webp from his pictures, the icon's corners made clear.

### 2026-10-31 - Harry confirms the mouse's direction, the scope, the wheel, loading colours and languages
- On Windows at 572f01d, Invert mouse off: up and down right, on foot, in the scope and on guns; the wheel stays a zoom in the scope; the three items he was asked (direction, wheel, and the loading colours and language through a driving mission) "all three fixed".
- Nothing else reported open from his lists. Not exercised by anyone: a multiplayer match; the launcher's packaging; the driving page's pointer and Yes/No boxes after widening were asked about, not separately reported.

### 2026-10-31 - The mouse's up and down were backwards everywhere, hidden by Invert mouse
- Harry: Invert mouse is ticked; forward looks up out of the scope, down in it. So with it off (the default) forward looked down, on foot and on guns, since the first mouse work: input::TakeMouse's y is up positive, and pitch grows downwards. Every "fine" from him was with it ticked. Noted as odd twice on paper and left, because he called it right; the setting should have been asked about the first time.
- Fixed in both exes; the scope flip, which was the answer to a wrong picture, is gone. He must untick it. Gun checked by a run; foot by the same sign, not run. His first report, sniper inverted with no flip in the code, is not explained by this: without a flip the scope goes the way normal aiming does.
- Wheel: at zoom 1 the game stops treating it as a scope, Zoomed() false, the wheel changed weapon while scoped. Zoom held at 1.05 at least.

### 2026-10-31 - Scope test from the rates; loading sides in the art's colours
- Harry: the flip changed nothing, the sniper still the wrong way. So Zoomed() (the player state's +0x2E4) was false for it; that also means the scope wheel (never reported on) probably did not work there. Zoomed() now: both rates equal and below 1, as 0xB45C0 sets them through a scope. Still not run: no scope reached headless. If it now flips and is right, the wheel should work there too.
- Weapon keys and page's values confirmed by him. Loading sides: the row's edge pixels, not black, as he asked to match on foot.

### 2026-10-31 - Harry: scope, gun's weapons, page's values, loading stretched
- Guns: 1, 3, Q and the wheel play the d-pad's down and up, as on foot (wheel seen changing the weapon in a capture; keys not, no keyboard here).
- Page's values show "< 100% >"; a mouse press on the left arrow lowers (seen 100, 95, 90), elsewhere raises. Arrow keys Press the d-pad in any menu: on DRIVING.XBE they were on the left stick. Why they failed on foot for him is not found; his report says they did.
- Scope: Harry reports up and down inverted through the sniper at the end of mission 1. Not found in code: the rate's sign is unchanged at 0xB4659; a zoomed gun also goes the same way as unzoomed. y flipped when Zoomed() on his report alone, unverified: the harness has not reached that sniper. The foot's y (up positive, pitch growing down) also looks backwards on paper, yet he calls it right; not resolved, his mouse_invert_y not checked.
- Loading: 0xBDEC0 CPU-copies the picture into the back buffer, so no squeeze. Hook at 0xBDF75 shrinks each row to the 4:3 middle, black sides. Width is the pitch's. Seen with AUF_WIDE; a spinner's place against it to be checked on Windows.

### 2026-10-31 - Harry's tests; PC Settings as a page in the driving pause
- Harry on Windows: achievements on a finished mission on foot and driving work, a finished save awards nothing, PC Settings on foot works, the pointer lines up (ultrawide too), Select Mission clicks, driving prompts, French and German menus and labels all confirmed. Not yet: language through a driving mission and back. The earlier risk about driving's scoring is answered: it works.
- He wanted a page in the driving pause, not the window. src/common/pc_rows.cpp now holds the rows for both exes. The tab goes to 4 from 0xBB1B1 and back at 0xBBC5F so the panel draws nothing (the tabs and the footer index widgets by the tab: 4 at the start crashed), and rows are drawn by a hook at 0xBC730. B is 0x10, START 6. Seen in captures: opening, moving, Render Scale changed, Back to the row. Pointer on the page written; hover and click on it not exercised.
- Yes and No: he saw no hover highlight. Here at 16:9 the choice flipped on every move, at 310 and 366 by log; the highlight's change seen in captures. Boxes were only about 30 wide; now half the row each, on the guess that an offset on ultrawide missed them. Harry to confirm.

### 2026-10-31 - The Windows link: the language folders were in every exe
- Harry's build.log: lld-link duplicate symbols in AgentUnderFire.exe. CMakeLists.txt's XBE_HOST_SOURCES takes src/*/*.cpp less a list of per-exe folders, and bond_fre and bond_ger were not listed, so each exe linked all three builds' hooks. So the throw was not his error (it may or may not have been one too). The harness does not read that list, so smoke cannot see this; checked with build/linux/build.ninja's objects per target.

### 2026-10-31 - The Windows build stopped at the language exes
- Harry's build stopped ("ninja: build stopped"), the error line not yet seen. A() used throw to refuse an unmapped address, an error in every bond_fre and bond_ger source with -fno-exceptions, reproduced here that way. Whether Windows' build has exceptions off is not confirmed, so this may or may not be his error. A() is now consteval, with an undefined non-constexpr function for unmapped: no exceptions needed. All src/ syntax-checked with mingw headers, -fno-exceptions: no errors. The real target (windows-msvc) cannot be checked with these headers.

### 2026-10-31 - French and German
- bond_fre.xbe and bond_ger.xbe are bond_eng.xbe's source with strings changed, so code and data shift a little throughout. tools/map_bond.py aligns the instructions (masked runs unique in both, a limit on distance, gaps filled) and reads data addresses off the aligned operands. It writes config/bond_<l>.toml, src/bond/addr_<l>.h (A()'s table; unmapped does not compile) and src/bond_<l>/*.cpp (src/bond with functions renamed at once: as macros one language's new name was another's old). Run it after changing src/bond; smoke fails if the output is uncommitted.
- Scans need --name, or xbe_symbols writes out = generated/game: it did once, and left stale partitions there.
- The language: option `language` (settings window, AUF_LANGUAGE), read by a hook of default.xbe's XGetLanguage 0x24052. Seen: AUF_LANGUAGE=1 opens SOUND_FRE.VIV, relaunches BOND_FRE.XBE with IDL_FR.LOC. DRIVING.XBE takes the .LOC from the launch data: French pause seen, Paramètres PC row in it, from hand-made launch data, not by a French run's own relaunch.
- Seen in French and German: menus, first mission, PC Settings page with translated labels (the translations are mine, unchecked by a speaker), keys in prompts' log. Smoke runs both to a mission with the mouse hook in force. Not played further: a sweep of their levels, gun rates, widescreen by eye. AUF_WIDE=2 was on for the page's run only.
- Windows exes: launcher/game.json lists the two new ones; the launcher's packaging itself not run here.

### 2026-10-31 - PC Settings, the driving pointer, achievements
- Harry asked for: the driving pause pointer and menu pointer, French and German, achievements, PC Settings from the main menu and pause menus.
- PC Settings (src/bond/pc_settings.cpp): a row under the main menu's Options and under Quit Mission. Page arrays copied into AllocHostContiguous with one more text and item; the page is a second set of arrays laid on the same page, so the game moves, highlights and draws it. 0x40A00's events by pressing each: 0 left, 1 right, 2 up, 3 down, 4 A, 5 B. Rows: field of view, mouse sensitivity, render scale, anti-aliasing, texture filtering, sharp text, frame counter, more settings (F1). Seen in captures on the main menu and the pause, opening, moving, changing one, back. Main menu's labels are baked in main_icons, so the row is text, not a bar. The heading stays MAIN MENU. Renderers' effect of a change not seen here.
- Driving (src/driving/pointer.cpp): the pause object [0x1DFB08] +0x420 tab, +0x424 row, +0x428 prompt's choice, +0x42C its row (research run proved by pokes; earlier diffs missed it, as it is in high memory). Rows hover, tabs click both ways: seen in captures. A fourth row drawn by a hook at 0xBB284 with a widget copy and a string number of ours (0xBC8C0), keyboard and pointer reach it. It opens the settings window, not a page: this engine has no page to borrow. Yes/No by pointer written, not exercised; click on the row not exercised (the harness has no window).
- Achievements (src/common/achievement_set.cpp), 16: each mission, first gold, first platinum, every gold, every platinum. Only tested by poking the medal values (a run's medal and the best): the real scoring was not run, as no mission has been finished headless. That is the risk: whether 0x3B2D08 is set on a driving mission's return is read from code only.
- wincheck of the new files ok, SMOKE OK.

### 2026-10-31 - Harry: movement great on the gun levels and on foot
- Confirms the gun rate and the up and down direction on a real mouse, both places. Pointer, Select Mission clicks, driving prompts and scope wheel not reported on yet.

### 2026-10-31 - Mounted guns about 1400 times too sensitive: my measurement was of the intro
- Harry: on the gun missions any slight movement spins the view thousands of times. Reproduced in the harness: 5 counts a frame turned over 2 radians a frame.
- Why it got through: both earlier figures were wrong. The "0.004 a frame at full stick" was the ramp +0x1BC, not the view, and "2020 counts 1.78 radians" and "2400 counts 2.39 radians, then still" (logged as confirmed on the real path) were taken with the test window inside the mission's intro, another camera path about 1000 times slower. So the earlier spin report was probably this too, and the "fix" only made it stop when the mouse stops.
- Measured in play off the camera's axis: a rate of 0.01 turns 0.054 radians a frame at half angle 33. kRatePerCount now 0.0008/5.6 (was 0.2). 0.00076 a count at 1, 5, 20, 100 counts a frame, mis02a and mis09 the same; the harness's mouse tokens (the real Counts path) 0.00077. Zoomed (half angle 20) 0.00034, slower with the zoom, as the game scales it. Up and down turned half as fast: doubled (kUpScale, not re-measured after).
- Movement built up off a gun is taken each call, and a call is capped at 400 counts: three 1000-count calls moved 400 each. Whether 0x77A20 runs through an intro on Windows was not checked.
- Open: Y on a gun gives 0.7 s at 6.7 times the rate (0.0051 a count), each press. Not found what state that is or whether it is what Harry calls the launcher. mis07a has no gun, so no third level.
- Open: +y (mouse up) looks down, on the gun as on foot, both by test. Harry called the foot fine, so left matching; he should say if up is down.
- Not on a real mouse: Harry's to feel. SMOKE OK.

### 2026-10-31 - Every mission loads; DRIVING.XBE's prompts show keys
- Sweep with the smoke's poke on 0x2A9F98, indices 0 to 15 for 110 s each: no crash. All on-foot levels draw (12 to 15 look like 5, so probably past the list). 1, 2, 6, 8 relaunch to mis02a, mis02b, mis07a, mis09; those ran 60 to 90 s, no crash. That is loading and the first minute, not levels played through.
- bond_eng.xbe 30 minutes on random input: alive at the end, 77 MB flat. (rc=124 is the timeout's.)
- Driving prompts: a 256x256 P8 atlas, FNV 0x0D17DFB8FC1761F7, same in mis02b and mis09, at a different address per level (82104080, 82103880). An earlier note's 820C1580 and 820E2580 were not checked again. Bound with five palettes: the icons' is the one with the A oval's rim green. The line's A is the tall oval (99,100 to 131,164), squeezed to 12x20; change page and scroll are the round cells at (202,150) and (202,176). Keys: a return arrow, back arrow, bare arrows; caps elsewhere. The palette has no dark grey, so bare glyphs read better.
- Took several wrong turns: fonts first, the wrong palette, the wrong cells, and blaming the reference renderer's cache, which a live poke disproved (it clears each frame). The redraw must be in the bind, which returns true: only that marks the texture dirty for the real renderers.
- Seen on screen with AUF_KEYS_TEST=1 (keys from the first bind) and with a controller (the disc's). A switch mid-pause, keys to pad, seen in the log and on screen on the pad side only; D3D12 itself untested. Cells other than the line's not seen in use.
- SMOKE OK, wincheck of the two files ok.

### 2026-10-31 - Multiplayer no longer crashes; long runs; the pointer's other pages; Select Mission by mouse
- Crash found by opening Multiplayer, by d-pad too, so not the pointer: 0x5BE11 ends a typed-out text's 256-byte copy at the count of letters shown, and this page's speed takes that to 1071, a zero past the top of the guest stack at 0xD006003F. Probably mapped memory on the console. Hook ClampTextEnd keeps it in the buffer; the page opens and shows its text. No match started here (one pad): multiplayer play itself untried.
- Gun on the real path: save, Select Mission, left to Precious Cargo, relaunch with mis02a. AUF_MOUSE_TEST 2400 counts turned 2.39 radians, then -0.2605 +/- 0.0003 for 1000 frames. (Wrong: that window was in the intro; see the entry above.)
- Long runs on random input: car mis02b 760 s and gun mis02a 283 s played to the mission's end and relaunched, no crash, 85 and 92 MB flat. Those pads do not fight well: both are survival to a mission end, not coverage of a level.
- Pointer: option rows join both columns (they were one or the other), Multiplayer's choices share one box so are not pointed at. Clicks are read a poll after the press, as a jump and click in one poll used the old answer.
- Select Mission: side pictures step, the middle starts, the difficulty's line steps it, elsewhere nothing. Seen in captures. Rectangles measured at 16:9 only, hand-kept for this page. Option values are still arrows (A does not modify them).
- SMOKE OK, wincheck of src/common/menu_mouse.cpp ok.

### 2026-10-30 - The launch segfault (0x321D0) did not come back
- 360 launches of default.xbe by exit code, 24 at a time, each alive at 3 s: none died. (240 more earlier were judged by their logs, which cannot show a silent death, so they do not count.) With the earlier 42, about 400 without a repeat; it remains one sighting right after a build.
- Nothing changed: a fix without a repro could not be shown to work. Still open; disc_rate first if it returns. 0x321D0 walks the ring from [ebx+0xC] outside the lock, so a stale chunk is plausible, not shown.

### 2026-10-30 - A visible pointer in the on-foot menus
- Harry chose it after "clicking feels odd". While a menu is up in bond_eng.xbe the Windows arrow shows (a window procedure in src/common/menu_mouse.cpp; no SDK change, so no shared gate). src/bond/pointer.cpp hooks 0x56930, the menu's per-frame input, whose argument is the menu file.
- The item under the pointer becomes current by the game's own sequence (0x37AD0, 0x37A30, page +0x60, sound 0x202E0, as 0x39120). A click waits until its item is current; a click off every item does nothing; pages of 0 or 1 items keep click anywhere.
- Rectangles from the page's own widgets; each page's are logged. Wrong at first twice: a save row's thumbnail and caption made rows overlap (now the largest text), the pause rows' only picture is the chevron (narrow pictures give way to text).
- Tried headless with AUF_POINTER=polls:x:y:buttons,... (16:9), read off captures: main menu hover, click off an item ignored, click opens Load Mission; device page click anywhere; slot BONDA, Yes loads the save, back at the main menu; pause menu hover 1 and 2 of 3.
- Not seen: the arrow itself and real window coordinates (no window here, Windows code only compiled); Harry to try. Options pages, Multiplayer, ncpausemenu not tried. Select Mission's carousel and difficulty are not items: still the arrows and wheel. DRIVING.XBE's menu is another engine with no rectangles found: no pointer there.
- wincheck and SMOKE OK.

### 2026-10-30 - The gun's spin, 1 for guns and 2 for gadgets
- Harry: aim better; moving a mounted gun spun the camera 360 degrees; clicking in the main menus feels odd; the wheel zoom not reached yet. His save (.local/win/hdd.zip, all three mission kinds).
- The spin, two causes: rates were set only when the mouse reported movement, so the last one kept turning (the harness's test always "reports", so it never showed); and +0x1BC, +0x1C0 are ramps a held stick builds, read as an angle.
  Now every frame on a gun rates and targets are set, 0 with no movement, ramps cleared. Measured: 2020 counts 1.78 radians (0.00088 a count), the heading the same in the next dump. Up and down stopped at 0.147 (the mission's limit, by the look).
- d-pad tried in play at last: right Q-Laser then Q-Claw (gadgets), down the gun. The earlier mapping read off the code was backwards. 1 next weapon (down), 3 before (up), 2 next gadget (right), the wheel weapons (down, up).
- Menu clicks stepped once a click headless, right button back once, the wheel moves: no double presses found. There is no cursor: a click acts on what is highlighted. Harry to say if that is the oddness.
- Correction: an earlier note here said his save loads by itself and that the relaunch was seen; that run showed neither (no relaunch line, and "00 AGENT" came from my own DOWN on Select Mission).
  Tried properly: Load Mission, Xbox Hard Disk, BONDA (Mission 3), Yes (the prompt starts on No), back at the main menu. Select Mission opens on Dangerous Pursuit, RIGHT does not go past it. Start it: 0x2A6BB relaunches into DRIVING.XBE with level mis02b; that launch data then ran 60 s, 2520 frames, no traps (HOLD_REPORTS). SMOKE OK.
  Not run from the save: the mounted gun by the menu or a real mouse; the spin fix is measured with AUF_MOUSE_TEST only.

### 2026-10-30 - Auto-aim off, NightFire's keys
- Harry: key pictures OK. Auto-aim off (option auto_aim, default off, every input): on foot the target search 0xB8BB0 sees player +0x716 clear for the call (the log says the option was on);
  on guns 0x76310 sees no target [0x1C6C18] for the call (logged on a target at 23 s in mis02a). Whether aim feels free: Harry's. Homing weapons and 0x9DDB0's use of the amounts untouched.
- On foot as NightFire: Q cycle weapon, G use gadget, 1 next weapon, 2 next gadget, 3 weapon before. Which way d-pad down cycles gadgets is from the code, not tried. A layout saved in settings.toml's [keys] keeps what it says.

### 2026-10-30 - Scope wheel, mounted gun aim, key pictures; SDK merged (fork main 36bb42e)
- Harry confirmed the radar and on-foot pop-in. Gate on 36bb42e, each game's commit in a copy (.local/gate2): PGR2, NightFire, PGR1 plus repro's live race,
  EoN (its SDK merged, -X theirs on what already conflicts), this game: SMOKE OK. Fast-forward; .gitmodules follows main.
- Mounted guns (src/driving/mouse.cpp): the camera 0x77A20 turns by rates +0x250/+0x254 eased to targets +0x260/+0x264. Stick full: 1.55 radians in 390 frames.
  Counts now set rates and targets directly (mouse_direct on). AUF_MOUSE_TEST 20,0: the view turned right; 0,20: down. Not felt by hand. Earlier "wrong camera"
  readings were before the stick or in a pause: memory dumps, not gdb's slow runs, showed it. Sensitivity assumes 60 frames a second there.
- MenuUp was wrong in both exes. DRIVING.XBE's 0x1801D8 was up in play (a coincidence of a diff), so the left button played A in the car on Windows; now [0x1BEB1C] == 3
  or 0x1E1818: in and out at the pause and the mission's end. bond_eng.xbe compared the name's pointer's bytes, so never the front end; the menu mouse there did not work
  before now. Fixed, in from 0.18 s to the mission at 37.3 s.
- Scope wheel: notches multiply weapon +0x51C at 0xBD768, up to +0x524. Not run: no scope reachable headless. Whether the zoom resets on leaving is the game's: Harry to see.
- Key pictures (src/common/buttons.cpp): bond_eng.xbe's atlas 0x837ECB80 and the Press A picture 0x8382CB80 replaced by keys while the keyboard plays, by their FNV. Drawings checked by
  dumping them (AUF_BUTTON_DUMP): ENTER and BKSP in menus; the reference renderer does not show replacements, so not seen on screen. Only bond_eng.xbe: DRIVING.XBE's pause icons
  are in P8 atlases 820C1580/820E2580, not done.
- Item 6: no keypad screens exist (the safes use the Q-Decryptor). Enter is A everywhere on foot, the left button too where the player has no control (rate 0).
- Smoke's driving part failed once: harness read poke between creation and write and removed it. Moved into place whole now.

### 2026-10-30 - A and D strafe on foot; driving pop-in and 60 FPS confirmed
- Harry: the culling fix cured the driving pop-in; 60 FPS performs OK. A and D turned the view: they played the left stick's x, which the game's Setup 1
  makes yaw. Now the right stick's x (RSLeft, RSRight), its strafe; the mouse turns. Headless, the right stick held: the command's right 0x7E (cmd +0x11),
  forward 0, yaw unchanged. A layout saved in settings.toml's [keys] keeps what it says.

### 2026-10-30 - Driving pop-in: the world's frustum is fixed at the console's angle
- Harry: 60 FPS fine; the pop-in shows just driving along the road.
- Traced from world draws (gdb stacks: 0x7DF90, 0xA6AF0, the list at +0xC08): the list builder 0x7DC10 tests spheres with 0x7E140 against the
  frustum at 0x190640, built by 0x7E3C0 from the camera's half angle (33 degrees at 0x1906A4) and nothing else: the view's widening never reached
  it, hence the same draw count at every width. 0x7E3C0 now gets the angle widened by the frame and EoN's 2.2 margin, at most 80 degrees.
- Evidence: mis07a frame 1500, 344 draws (277 world) before, 357 (290) after, the picture the same; SMOKE OK. A pop itself was never caught headless:
  whether this cures it is Harry's to see. The earlier memory scan missed it: the planes are in columns, the watchpoint candidate 0x82A6AE20 was mesh data.
- The second test 0xAE550 (planes at 0x1C6CA0) was not looked at; the count of those planes is 0x1C7CA0. First suspect if it still pops.

### 2026-10-30 - Harry's ultrawide run: the radar, 60 FPS, a culling margin (sdk branch agentunderfire)
- Harry, 3440x1440: intro skip, field of view and mouse on foot fine. Three problems.
- Driving map gone: the radar draws in its own viewport (477,48,135x135) and the squeeze moved the picture out of it. A draw squeezed
  whole now takes its viewport and scissor along (gpu::SqueezedX, D3D12, D3D11, reference). Reproduced headless once the reference renderer
  squeezed 576 line targets: cut in half before, whole at FRAME_ASPECT_X100=239 after. Changes SDK behaviour: needs the gate again.
- 30 FPS on foot: 0x139015 and 0x138940 ask for interval 2. Now the default interval, paced by fps_cap (src/bond/framerate.cpp, option uncapped,
  AUF_30FPS). Timing runs off a clock (0x25CA0): 600 frames, 10.18 s of game time, 60 a second headless.
- Pop-in on foot: a margin of 2.2 on the culling frustum's sides (0x15CD00), EoN's remedy. About 16% more draws in the first mission's opening,
  the picture the same; not compared like for like (frame shape differed). Whether it cures Harry's pop-in is not known: not reproduced.
- Pop-in driving: NOT fixed. Draw counts are the same at 4:3 and 4x (284, 344), so what is drawn does not follow the view's width and something
  else chooses it; no EoN-style frustum found by code pattern or by scanning memory for 33 degree planes, one candidate 0x82A6AE20 not tested.
  4x frames show no gaps standing still. Harry's two captures (mis02b) have blank final frames and no log. Next: a clip or capture at a pop.

### 2026-10-30 - SDK merged into the fork's main (0733ba3)
- Gate, each game's own commit in a disposable copy (.local/gate) with the candidate SDK: PGR2 SMOKE OK; NightFire SMOKE OK; PGR1 SMOKE OK and
  repro.sh's frame a live race; EoN SMOKE OK on its pinned SDK merged with the candidate (-X theirs for launcher, handover and boot, which
  conflict between its line and main already); this game SMOKE OK including the driving part. Fast-forward; main had not moved.
- Smoke test now goes on into DRIVING.XBE (Windows build settings, HOLD_REPORTS). Not checked that it fails without the fix by running it;
  the same hold reproduced the freeze by hand. .gitmodules follows main again.

### 2026-10-30 - PC features, headless (pc features in src/; not yet on Windows)
- Skip intro: 0x20BD0 returns at once but for HILITE; bond_eng.xbe reached 0.26 s after start.
- Widescreen: bond_eng.xbe, [0x3467B8] written in 0x1387E0 (draw, culling, tags together, true proportions); DRIVING.XBE,
  0x102990 as NightFire's. 2D kept in 4:3 (hud_aspect). The reference renderer squeezes 480 line targets alone, so the driving
  HUD's 640x576 stays full width in the harness; the GPU rule has no such limit: Windows to confirm (log: "ultrawide:").
- Field of view: the four projection helpers' angle. The first person weapon's own path is skipped (0x14B654), but at AUF_FOV=130 the
  gadget hand still looked smaller: another path draws it. Harry to judge.
- Mouse: bond_eng.xbe 0x24570, degrees by the game's rates (scope, manual aim, no control). AUF_MOUSE_TEST=30,0,500,560 turned the view
  right. DRIVING.XBE's guns take it as the right stick (mouse_direct off): no direct aim there, not tried headless.
- Menus: MenuUp in bond_eng.xbe [0x3B2428], [0x3B2418], state name "frontend"; DRIVING.XBE byte 0x1801D8 (pause: the only byte up across one
  by memory diff) or 0x1E1818 (not in play yet). 0x1801D8 blinks a few times as a pause closes. Guns: the control file's name at 0x17FED1.
- Keys from the game's Controller Setup page; DRIVING.XBE's follow NightFire's, as the same engine's, unconfirmed here.
- Left out: an in-game PC Settings page and keyboard button pictures in the prompts. Auto-aim stays the game's while the mouse aims.

### 2026-10-30 - Windows confirmed
- Harry: the freeze and the models are fixed, the game works, no obvious visual bugs on a first look. Not yet a full
  playthrough. Next: the SDK gate and merge, then PC features.

### 2026-10-30 - Driving levels froze on Windows: visibility reports (sdk)
- Harry: textures fixed (D3D12). Starting the second mission froze: DRIVING.XBE drew about 11 frames, then none, F3's frame
  never came. Thread 14's stack (log, guest_mem_8864656): 0x87851, the loop 0x87830 calling 0xFFC10 until true, that is
  GetVisibilityTestResult 0x114BE0, in the level's load.
- 4039's GetVisibilityTestResult only reads the report. Later XDKs' also set a fence, and the runtime's hosted D3D_SetFence is
  what kicks the renderer; nothing here did, so the report never came. The harness's null backend writes reports at once,
  which hid it.
- SDK: an image naming D3DDevice_GetVisibilityTestResult (DRIVING.XBE only; the others do not link it) walks the pushes and
  kicks the renderer before the library's read. Other games do not name it, so their behaviour is unchanged.
- Proof, in a temporary harness build (RT_THREAD=1 RAW=1, null backend holding reports until Flush): before, frames stop at
  about 24 s with 1054 draws; after, mis02a plays through 60 s. SMOKE OK, wincheck ok. Windows still to confirm.

### 2026-10-30 - Vertex blending on the render thread (sdk)
- Harry's capture 154810 (D3D12): the flaps gone, most of the hand and the flag missing. The render thread's state copy
  (gpu/render_thread.cpp) carried only transforms[256], so WORLD1 to WORLD3 reached the backend as zeros and vertices weighted
  to them collapsed. It now carries all four.
- Proof in the harness built as the Windows exe (RT_THREAD=1 RAW=1): with the old line the frame is Harry's (thumb, fragments,
  no flag); with the new one hand, sleeve and flag are whole. The previous entry had only tested the plain harness.
  SMOKE OK, wincheck ok. Windows still to confirm.

### 2026-10-30 - Vertex blending: the torn hands and characters (sdk)
- Harry: the signed lock (below) did not fix it. The earlier diagnosis was wrong: the tear is in every renderer, also the
  harness's (RAW=1, RT_THREAD=1 and neither alike); it had been read as whole in small montages. Also wrong: the "white door"
  and black ground in the captures were this viewer showing the BMPs' alpha over white. Flatten them (`-alpha off`).
- Cause: bond_eng.xbe's models are FVF 0x11C (XYZB4, stride 48) with D3DRS_VERTEXBLEND 5 (3 weights, 4 matrices): each vertex
  has its weight on one of WORLD to WORLD3 (604 of 1272 logged vertices not on the first). Neither renderer implemented
  vertex blending, the first game to use it. Culling off left the tear; assembling strips as fans broke the sky.
- SDK: FixedKey::blend, FixedVertexHlsl and the reference renderer blend position and normal in view space, then project
  (DrawConstants bwv, bwvn, vpj). Modes 1-6 written, only 5 seen. The shader cache is keyed on the source, so old entries are
  not reused. Evidence: the harness draws hand, sleeve and the flag on the pole whole. 24 shaders compile, wincheck ok, SMOKE OK.
- Open: one segfault in default.xbe at 0.14 s, in the movie stream ring (0x321D0, "STRM"), during a smoke run right after a
  build; 6 runs alone and 36 runs at 6 at a time did not repeat it. A race with the streaming thread is suspected, not shown;
  GameInfo::disc_rate (EoN) is the first thing to try if it returns.
- Next: Harry's rebuild on Windows.

### 2026-10-30 - First Windows run; see-through models: vertex buffer locks did not wait (sdk: 4039 signs the lock's wait)
- Harry, Windows, D3D12, RX 7900 XT: the game loads and the first mission plays to its end with no crash or hitch, but
  surfaces can be seen through. His F3 capture (.local/win/144318, 2560x1440): the hand is torn into pieces, also in the
  depth buffer, so triangles are missing rather than transparent; the ground is black.
- Thought then to be the cause (it was not, see above; the signing stands), as NightFire's FINDINGS: D3D_BlockOnNonSurfaceResource was not in 4039's table (seeded from 3948, which lacks
  it), so a vertex buffer lock (0x196AF0, calling 0x198080) never waited for the renderer. Windows draws on a render thread
  and reads buffers the game has already refilled; the harness has no such thread, which fits its frames being whole.
- Now signed: default.xbe 0x45900, bond_eng.xbe 0x198080, DRIVING.XBE 0x11B030. Compared the table with 4831's and 5659's:
  nothing else missing that this game links, D3DDevice_GetRasterStatus not looked for.
- SMOKE OK, mis02a 40 s without a trap, wincheck ok. None of that tests the fix: only a Windows run can.
- Next: Harry's rebuild; the black ground may be a separate problem.

### 2026-10-30 - Every level loads headless; the driving handover goes both ways; GitHub repository
- hk21x/007-agentunderfire-recomp holds main (merged GitHub's initial commit, the MIT licence). .gitmodules follows the SDK
  branch agentunderfire until it merges into the fork's main.
- Tour, 75 s each with the pad script: mis02a, mis02b, mis07a, mis07b, mis09 from patched launch data, and bond_eng.xbe with
  0x2A9F98 = 0, 3, 4, 5, 7, 9, 10, 11, 12 (6 and 8 relaunch into DRIVING.XBE): no traps, no warnings but wbinvd, a frame of
  each looked at. Each only stood or drove a little: none played to its end. Index 5 and 12 looked like the same place.
- Quit Mission in mis02a relaunches into BOND_ENG.XBE with the level name still in the launch data; it boots and draws.
- Generated HLSL: 24 shaders from bond_eng.xbe's menus and first mission, 22 from mis02a, all compile with DXC.
- The runtime always reports English (language 1), so default.xbe always picks bond_eng.xbe: French and German wait for a
  language option.
- Next: Harry's first Windows build.

### 2026-10-30 - The first mission and a driving level play headless (sdk branch agentunderfire)
- XDK 4039 is new, between 3948 and 4831. Names came from aligning each library section with PGR1's, NightFire's and
  PGR2's in link order (body similarity, ret sizes must agree), then by hand from the instructions. Scan from 16-30 of
  258 functions to 98-113 of 266. layout.toml cites bond_eng.xbe throughout.
- 4039 has its own state numbering (StateNumbering = 4039, runtime xdk/state_numbering.h): 82 simple, 35 deferred
  (SwapFilter added at 116: the frame buffer init 0x198FEB stores it), 29 complex read from the setter table 0x19EE94
  (no SampleAlpha). Swap and frame surface pointers as 4831; present interval a device field and GetBackBuffer's out
  pointer as 3948.
- Push buffers: none. .text of all three xbes has no call to MakeSpace, no reference to D3D_Device or D3D_pDevice, and no
  stores of the method headers searched. Immediate mode goes through the library's SetVertexData calls (D3D section).
- bond_eng.xbe needed SetI3DL2Listener named (unnamed, the library ran on the host's sound object and crashed).
- DRIVING.XBE hung: the heap's lock pointer (0x81CB6010 + 0x38) held pixels. Something writes exactly one row (0xA00)
  past the 640x576 surface at 0x81B4E000 while the loading screen is up, and the host had put the heap right above it.
  Frame surfaces now get a spare row (SDK NewSurface). The writer itself was not found: a hardware watchpoint did not fire.
- Reached mis02a by poking the menu's mission (0x2A9F98 = 1, 0x2A5D38 = 1) on the briefing: the relaunch carries it in
  the launch data; a copy is in .local/hand. The menu's own unlock path has not been played.
- Evidence: SMOKE OK; captures of movies, title, menus, briefing, mission 1, mis02a's gun section and pause; 60 s of
  bond_eng.xbe's mix with no silent second and no clipping; scripts/wincheck.sh ok.
- Next: Windows build (needs a GitHub repository), then the remaining driving levels and languages.

### 2026-10-30 - Project set up
- From the SDK's templates/game with three images. First scans with the 3948 seed: default 16, bond_eng 27, DRIVING 30
  of 258 functions. Next: name the rest, then the push buffer check.
