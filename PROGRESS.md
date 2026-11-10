# 007: NightFire recomp - progress log

Newest first. Each entry: date, what changed, what was learned, what's next.

## 2026-11-10: LAN play over slow networks: smoother players, reliable hits and deaths, crouch and aim (protocol NFL2)

- What was already there: each copy moves its own players itself (no input delay, nothing to predict or correct) and
  decides its own shots' hits on what its player saw (the shooter's lag compensation). What was missing:
  - The host stamped each client's state with its own arrival time, so the client's network jitter went into the
    positions every other copy drew. It now keeps the client's own time (the host's clock as the client keeps it).
  - One fixed 100 ms interpolation delay. A client's state goes client -> host -> client, so over the internet it
    often arrived later than that and was extrapolated. Each remote player now has its own delay: its mean lag plus
    three times its jitter plus a send interval (60-350 ms), eased 1.5 ms a state. Corrections after an
    extrapolation are eased in (an error offset that decays 15% a frame); 3 m or more (a respawn) at once.
  - Hits, deaths and shots were sent twice and lost if both packets were: hits missing, scores drifting apart. They
    are now acknowledged (kAck) and sent again every 120 ms for up to 3 s.
  - A hit that reaches the victim's copy more than 400 ms after it was fired is dropped (no dying long after
    reaching cover); events carry their time for it.
- The magic is now "NFL2": 1.5.13 and older copies do not see or join 1.5.14 ones. The master server
  (server/master/api.php) asks both.
- NF_LAN_SIM=delay,jitter,loss (ms, ms, %) holds and drops a copy's outgoing packets (both copies: a round trip of
  twice the delay); NF_LAN_DELAY=ms forces one delay (100 = before); NF_LAN_TRACE=1 logs each remote's delay, lag,
  jitter and the frames drawn past its newest state.
- Measured (two copies on this PC, Skyrail, NF_LAN_SIM=70,30,5, the client strafing, the host firing): lag about
  88 ms, jitter 8, delay settled at about 145 ms, 2% of frames past the newest state. With NF_LAN_DELAY=100, 53-65%.
  10 events sent again (none given up, none dropped as late); the kill counted on both copies.
- Bug found on the way: Story co-op left on made every multiplayer and LAN match a two-player split screen on each
  copy ("reconnect the controller to port 2"). src/game/coop_test.cpp now leaves matches alone (the game's
  multiplayer flag 0x260018, or a LAN session); story co-op still starts with two players (The Exchange).
- Driving the menus now: `.local/lan_drive.py` gets both copies through Netplay to the lobby by the page each log
  shows (title, codename 4000004A, main 40000002, Netplay 4000FC30); the rest (scenario, map, a villain for the host,
  options, start) is still partly by hand. `.local/both.py <name>` takes both copies' screenshots side by side.
- Crouch and aim, the same day: each state now also carries the view's pitch (actor +0x838) and the crouch's eye
  drop (actor +0x89C, 0 to 0.45). A puppet's upper body aims along its player's view through the game's own aim,
  0x12220(obj, target*, 1000.0f, 0.2f, 0, 5) as a player's at 0xACA77 (inst+0x5C the target, flags 0x80|0x200; the
  spine turns to it in the pose, 0x15060), a point 20 m out from its eyes, set each frame after its think. The bots
  have no plain crouch: a scan of the posture ids 1-0x7F (0x341C0, NF_LAN_POSTURE_SCAN, since removed) found AI
  actions only (0x2D hunched over, 0x30 a dive, 0x2E/0x2F rolls, 0x0C/0x0D dodges, 0x40 a death fall; standing ids
  keep the head about 0.45 above the object's place). So the pose hook (foot_ik.cpp, setting or not) lowers a
  crouched puppet's body by the eye drop with the feet kept and the knees bent forward (a straight leg's knee now
  bends the way the character faces). Tested with two copies (Skyrail; the client's X crouches with controller
  scheme 7, the keyboard off): the puppet kneels on the host, looks up and down with its player; the host's shots
  aimed at a standing chest hit zone 4 standing and passed over the crouched puppet, aimed lower they hit it.
- The host's AI bots: each sends its own posture id (its data's +0x466, the one 0x341C0 set: run 1E, walk 08, dives,
  rolls, ducks), and its puppet on the clients plays the same id instead of one guessed from its speed.
- Two older bugs found testing them, both fixed:
  - A bot set up in the menus never came into a LAN match: after the scenario's Continue (0x88EA0 hook) the setup
    kept for the lobby was replaced by the copy from before the call, whose count byte the game only fills in the
    call (0 wanted). The lobby's own copy (made in the call: the host's bots, then the network players added for
    the team check) is kept now, less those added.
  - A match's bot records (0x245282, 0x12 bytes) are the bots' settings, not names (a menu-made bot: 08 00 02 00 64
    00 01 64 64 01 00 00 00 01 ...; byte 4 its health, +0xE in use, +0xF team, +0x10 character). LAN play wrote each
    puppet's name over them, so a puppet's health was its name's fifth letter: names of four letters or fewer
    ("Open", "Bob", the host's bot's settings-as-name) were never spawned and stood frozen at their spawn on the
    other copies. Names are padded to five letters (PutName); the host's own bots keep their whole settings
    record and are named "Bot N".
- Lag compensation (victim-side rewind): the shooter's copy already hits what its player saw (the puppets are drawn
  in the past, so a shot at one is aimed where the victim really was then). The victim's copy now checks it: a hit
  carries the host-clock time at which its shooter saw the victim (the puppet's drawn time, Event.view_time) and the
  contact point (node+0x30); every copy keeps its own players' (and on the host its AI bots') places for 1.5 s
  (g_past); HitPlausible rewinds to the view time and keeps the hit if the contact is within 1.0 m (horizontally) of
  the body at any moment within +-250 ms of it and between feet -1.4 and head +0.7 m (lower by a crouch) of the
  object's place (honest hits measured: within 0.4 m out, -0.9..-0.4 up, the head about +0.5). A hit that could not
  have landed (a stale aim, a cheat) is dropped and logged; history too short or a view time beyond it: taken as it
  is. NF_LAN_LAGCOMP=0 turns it off; NF_LAN_BADHIT=1 sends every hit with its contact 6 m off (diagnostic).
  Tested (120 ms, 50 ms jitter, 5% loss, the target strafing): 8 of 8 honest hits kept (one matched the body 218 ms
  from the view time: with the plain view time alone three had been dropped, 2.4 m off, which is why the +-250 ms
  window), the fake hits all dropped, no damage taken.
- Prediction past the newest state: from the last three states, position = p + v*t + a*t^2/2 (a clamped to 8 m/s2),
  yaw carried at its rate, at most 250 ms ahead (was a straight line). Measured on a 6 m circle, a stop and a direction
  change, 15 cm ahead, with the game's exact positions: 0.007 m mean against 0.04 m for the straight line. With random
  position noise of 1 cm it is worse (0.11 m against 0.05 m), so it only helps where positions arrive exact but late.
  A sudden change of direction (strafing, 1 m error at 150 ms) is not predictable from the past states at all.
- Strafing (not yet seen on screen): a puppet moving mostly sideways (side speed over 0.6 of its forward speed, at
  least 0.4 m/s) gets its legs from the game's locomotion driver 0x18BB0 (cdecl: obj, node=[actor+0x820], |fwd|, fwd,
  0.1*[0x17C100], side, 0.075*[0x17C100]), the call a player's walk makes; its body keeps facing the view. The rates
  read from 0x17C100 are logged ("strafe N: fwd, side, rates") with NF_LAN_TRACE. The automated lobby driving closed
  the game before a match in the last runs, so this is unverified: check by hand in a LAN match.

## 2026-11-10: ragdolls (optional, off by default)

- src/game/ragdoll.cpp, from foot IK's pose hook (0xDD290; the bone math now shared in src/game/bones.h): a human
  enemy whose health ([obj+0xBC]+0x90) is gone keeps its bones' world matrices from that frame, and 17 joints (pelvis,
  chest, head, shoulders, elbows, wrists, hips, knees, ankles, toes) become Verlet points: the torso and head held as
  one piece, the limbs' lengths, knees and elbows not folding flat, knees not backward. Each game frame ([0x1F65BC])
  the level is met through the game's ray 0x2C4E0 (the way a point moved, else the ground under it), the contact planes
  held inside every solver pass (held only after it, the torso and floor fought and the body crawled), no bounce, and
  static friction. Every bone is then its kept matrix moved with its joint (torso by its points' frame, limb bones
  aimed at the next joint, the rest carried by their parents). A body sleeps when its pelvis and head go nowhere in
  half a second (2.5-3 s after death in Chain Reaction). It starts moving as the living pose moved a frame before,
  plus the killing shot's push (hit_feedback.cpp hands the hit over).
- Some models leave bone 33 (a hand) unsolved: its matrix sits near the world origin. Such points are left out
  (a body without its torso points is left to the game); before that the hand fell forever and dragged the body.
- Visual only (nothing of the game's state written), but off in network play all the same: the game may read bone
  matrices (attach queries) and lockstep copies must agree.
- Test: `.local/ragdoll_test.py <tag> [level]` (NF_RAGDOLL_TEST=1): the nearest enemy brought 4 m in front of player 1
  and killed every 4 s, a screenshot a second; NF_RAGDOLL_TRACE=1 logs each body. The made-up hit through 0x3D900
  takes no health (no projectile), so the test sets the health to 0 itself.

## 2026-11-09: HD textures without playing; hd_pack on Linux and macOS

- Level files hold the game's textures in records: "KXT" at +0, +8 size, +12 width, +16 height, +20 format code
  (0 DXT1 0x0C, 4 DXT5 0x0F, 8 swizzled A8R8G8B8 0x06; 1 = palette, skipped), +24 mips, +52 name[36], +88 pixels. The
  renderer's key (a hash of level 0's bytes, format and size) is reproduced from them: 158 of the 163 real pictures
  the game itself dumped in The Exchange; the other 5 are menu and render pictures. `.local/tex_scan.py` is the
  Python prototype (its hash port matches gpu/shaders.cpp's Hash); `XBE_TEXTURE_TRACE=1|2` logs (and with 2 dumps raw)
  every picture the renderer dumps, `.local/tex_trace_run.py` drives it.
- `NF_COLLECT_TEXTURES=1` (src/game/texture_collect.cpp, called from the pads' frame hook): after 600 frames the game
  reads each of the 30 level files through its own archive (0xE2D50, 0xE3280, 0xE2D90 as mp_anims.cpp does), finds
  the records and dumps them (`gpu::TexturePackCollect`, sdk gpu/texture_pack.cpp), then quits. 14 s on this PC:
  3131 pictures (the game dumped 388 playing); 354 of those 388 are in it, the PNGs byte-identical.
- Launcher: HD Textures > "Collect all textures now" starts the game with that variable (main.js textures:collect).
- hd_pack is portable: pictures through app::DecodeImage/EncodePng (WIC on Windows, stb elsewhere), posix_spawn off
  Windows, the upscaler per OS (Real-ESRGAN v0.2.5.0 windows/ubuntu/macos zips, hashes in XbeRecomp.cmake) copied to
  tools/ with its execute bit. Checked: Windows with the real upscaler (3 of 3 with PBR maps); Linux (WSL, software
  Vulkan, 1 min for 3) the same; the collector on Linux gives the same 3131 keys and pixel-identical pictures.
  macOS: configured only, never built or run. Driving.xbe's own files are not collected yet.

## 2026-11-09: Tighter Movement also quickens grenades (and their fall)

- Grenades (weapons 52-55) leave through the round spawner 0x211F0 like shots (fired from 0xBAE50 with the weapon's
  entry). The thrown object's data ([obj+0xBC]) has its speed at +0xD0 (units a frame, the entry's +0x20: 0.167 for
  the stun grenade 53) and its fuse at +0xD4 (frames, 210 = 3.5 s, one off each frame). Both are per frame, so the
  game runs them at the right speed at 60 fps: the slow feel is the original tuning, not a timing fault.
- src/game/throw.cpp: with Tighter Movement (host rule in netplay) the speed x1.5 and the fuse x0.7 at the throw.
  The remote mine 56 is left alone.
- Grenade gravity: the grenade's think (0x23670) integrates velocity with the global gravity vector at 0x1F6648
  (y at 0x1F664C, the same one players use; call 0xD7430 at 0x239FE). The hook on 0x23670 scales that y by 1.6 for the
  thrown grenades only (kept in a list of the last 8), and restores it after. Measured at frame 30 of a stun grenade:
  original 24.64 with only the faster throw, 23.93 with the gravity too.
- Measured (`.local/throw_test.py <tag> 53 [tight]`, LEVEL=07000005 The Exchange, which has weapon 53's model):
  off 0.167 and 210 frames; on 0.250 and 147 frames (2.45 s); no exceptions. Linux build and pause smoke test OK.
- NF_THROW_TRACE=1 logs each throw's data and its path each frame.

## 2026-11-09: on-screen touch controls on Windows (setting)

- Settings: `touch_controls` (settings.toml, Vulkan, D3D11 and D3D12 draw them), F1 window under General:
  "On-screen touch controls (for a touch screen)". Android keeps them always on; `XBE_TOUCH=1` still forces them.
- Windows: WM_POINTER touch points (main.cpp WndProc) go to `app::TouchPoint`, which builds SDL finger events for the
  same touch.cpp logic (left = stick, buttons, right = look). The touch code was `#ifndef _WIN32` before.
- The buttons show after the first touch (the game's own rule), and hide while a controller is connected.
- Checked: builds; a mission starts with the setting on, no touch-related exception. NOT tested on a real touch
  screen; no touch injection in the harness. Test on the handheld before release.
- Found along the way: handing weapon 52 (grenade) in The Exchange and Chain Reaction crashes in the gun routine
  0x118F0 (the gun's instance [gun+0xB8] null: its model is not in those levels; a test-hook artifact). 53 (stun
  grenade) throws in The Exchange. The null read after it went off was the NF_THROW_TRACE diagnostic itself
  (nf::ThrowFrame reading the freed object; llvm-symbolizer on the dump's RVA), now stopped when the object goes.

## 2026-11-09: driving visibility-query freeze (1.5.6)

A live frozen Driving.xbe instance spun at 0x9E540 -> 0xE7CA0 -> 0x165FD0. Two thread dumps sampled
that loop and GetVisibilityTestResult, while the last rendered frame stayed unchanged. This XDK function only
reads the GPU report; unlike the newer implementation it does not submit pending work. Naming
D3DDevice_GetVisibilityTestResult = 0x165FD0 in config/driving.toml enables the SDK's existing push-buffer
walk and render-thread Kick hook. The generated function keeps its original report reading and ret 0xC.

Windows and Linux driving builds pass. A mission-menu-verified Enemies Vanquished test switches to cockpit
view, drives and fires; the fixed build presents hundreds of 3D draws per frame through 153 seconds without
hanging. Automated driving can fail/retry the mission; this does not establish that the exact photographed
3:39 roadside position was driven successfully. The original supplied minidump was a separate settings stack
failure. Diagnostic scripts must close child Driving processes explicitly; PowerShell execution policy can
reject a cleanup .ps1 when invoked with -File.

## 2026-11-09: 1.5.5 release preparation and in-game light shafts

Graphics > Effects now exposes Light Shafts (Off, 0.1 through 3). Blood remains on Gameplay & More.
Fog and shafts are independently controlled: enabling shafts with Fog off sets fog strength zero, and turning
shafts back off disables the atmosphere pass when fog is off too. Live Exchange checks passed Off -> 0.1
without fog -> 3 -> Off, including persistence after Back.

The supplied Nightfire.2026.11.08_22.20.25.dmp has an access violation writing guest D003FFA0 from a menu
widget lookup. Its 96 recorded instruction bytes match current sub_000723C0; the guest ESP is D0040024,
near a stack guard. The native stack repeats the settings page 4000FC30, row 1000FC7E and activation 4B.
Custom settings pages now ignore nested synchronous activations, preventing duplicate apply/back recursion.
Exact user reproduction steps are pending; the dump does not contain guest memory or its matching PDB.

Merged the published 1.5.4 launcher/SDK improvements, keeping Android export and the gameplay fixes.
The source bundles shipped in the launchers must be generated after the final game and SDK commits.

## 2026-11-09: blood at character impacts and visible gameplay toggles

Blood now emits native depth-tested red particles at the actual character hit position after damage, in place
of the old crosshair-centred spray. The native character-puff texture 0x200021D works in every renderer and
split-screen view. LAN puppet hits also emit at their world impact. The screen-edge hurt effect remains.
Blood defaults on for fresh settings; explicit saved off values are respected. Lockstep co-op always creates
the same particles/RNG calls on each copy, hiding rendering locally when Blood is off.

Both Blood and Tighter Movement are grouped under launcher Controls > Gameplay. In-game More Settings now
labels its second page Gameplay & More, which contains both toggles. Polling live menu values preserves the
entire guest CPU context (CallGuest preserves integer registers but not flags/x87), preventing unintended
navigation when a setting changes.

Validation: Windows and Linux executables build. A paused Exchange test finds Blood using the joypad, changes
it live from off to on and confirms graphics.toml after its one-second save delay. The world diagnostic creates
12 sprites per burst, and screenshots visibly show the red mist in the 3D scene. Test game closed and settings
restored. The Windows portable launcher was rebuilt as out/launcher-electron/NightFireLauncher.exe; its packaged
app.asar was checked for both Gameplay toggles. Schema syntax and Git whitespace checks pass.

## 2026-11-09: apply pause-menu settings as they change

The Settings/Graphics pages previously applied their values only on A/Confirm; B silently discarded them.
Their visible choice and slider values are now compared once per menu frame, with a baseline on each page/set
entry. Actual edits apply and save immediately, using the same path for classic and modern menus. Back keeps
those changes. Graphics presets refresh their dependent rows after applying; focus changes alone do not save.
FOV and mouse slider baselines advance after edits so moving back to the entry value really restores it.
Co-op choices apply their safe options live, while network restarts/joins still require explicit activation.

Validation: both Windows and Linux executables build. In a paused Exchange mission, FOV changed 100 -> 105 -> 100,
tighter movement changed on/off/on, and shader style changed Original -> Enhanced -> Original, with values checked
while their menus stayed open. Back retained the edits and movement applied on resuming. The game was closed
and its settings restored. Launcher schema syntax and Git whitespace checks pass.

## 2026-11-08: optional tighter movement, including multiplayer host rules

Added `tight_movement` (off by default) to Settings > More Settings > Next Page, the F1 game options, and the
launcher's Controls page. On foot it increases the native forward/strafe smoothing coefficient from 0.2 to 0.5,
scopes gravity to 1.6x during the player's physics update, and boosts only a newly started jump by sqrt(1.6).
Top speed, collision handling, ladders and swimming retain their native paths. All local players use the same rule.

LAN multiplayer appends the host's movement rule after the existing lobby packet's names. Clients follow it
without overwriting their saved preference; the menu displays a read-only host value. The lockstep network-co-op
handshake carries the host's session rules before gameplay starts, fixed for that session. Legacy hosts without
the optional tail mean original movement. The state hash includes the effective movement rule.

Validation: Windows and Linux builds pass. Three stationary Exchange jumps per mode measured 0.783 s / 0.751 m
original and 0.617 s / 0.743 m tighter (21% shorter airtime, height within 1 cm). UDP integration checks passed
both on/off host advertisements and clients with opposite local preferences, for LAN and lockstep co-op.
Linux title-to-Paris/driving headless smoke passes (369 draws/frame, no traps). The in-game option was navigated
with a joypad, confirmed, checked in settings.toml and verified to apply to the next movement/jump immediately.
Test games closed and settings restored.

## 2026-11-08: pause Controls joystick layouts

Restored the original eight-layout selector inside the modern pause menu's Controls tab. Its one-column native
list shares id 0x10000028 with the tabs and binding table; it now becomes a separate Joystick Layout choice.
Mouse arrows send the native up/down layout input, while left/right still switch tabs. Native bindings update
with the selected layout. Keyboard decoding now restores the joystick layout immediately, allowing menu reads,
profile saves and switches back to NightFire (zero) to retain the actual selection.

Validation: Windows and Linux builds pass. In Exchange, controller down switches Classic Bond to NightFire,
up returns to Classic Bond, and closing/reopening pause retains the selection. A second run with keyboard enabled
cycles all eight layouts and asserts each saved controller_scheme value, including zero. Linux headless smoke
passes title through Paris/driving (392 draws/frame, no traps). Test games closed and settings restored.

## 2026-11-08: light shafts cost and appearance

The screen-space Light shafts slider now integrates scattering at quarter resolution and reconstructs it with
four depth-weighted samples. Local brightness contrast rejects broad pale surfaces as light sources; a local
centroid steadies each source, fixed jitter avoids animated grain, and short-range falloff keeps the effect near
lights. Nearby geometry receives less scattering so the weapon is not washed out. Fog-only frames skip source
extraction and its downsampling chain. Atmosphere images are now freed with the scene target.

Validation: both Windows and Linux executables build; the scattering and composite shaders compile, and matched
1920x1080 Exchange captures at strengths 0, 1 and 3 show reduced haze. In each run the stationary scene was measured
with shafts off and on: atmosphere 0.19 -> 1.30 ms before, 0.21 -> 0.33 ms after; GPU frame 0.82 -> 1.92 ms before,
0.84 -> 0.95 ms after. No shader failures were logged. The test game was closed and its settings restored.
This changes the screen-space atmosphere effect, not the separate ray-traced volumetric lighting effect.

## 2026-11-08: responsive Android launcher

Replaced the launcher's fixed-width, landscape-only screen with native responsive cards: stacked on narrow
windows, two columns when width and text size allow, with bounded tablet content width and system-bar/cutout
insets. Buttons grow with their labels; Settings and dropdown choices wrap and scroll. The first action imports
a package when none is installed and plays when ready. Import work uses the application context and reconnects
to recreated launcher views; compressed ZIP imports show copied bytes rather than a misleading percentage.

Debug and release APK builds pass. The release APK is copied to out/android/NightFire-Android.apk. Android lint
still reports existing errors in generated SDL Bluetooth code; no errors point to LauncherActivity. Attempted
phone/tablet/large-text visual checks are not validated: this CPU-only emulator's System UI repeatedly hangs and
blocks its screen/accessibility tree. A responsive emulator or real-device visual check remains necessary.

## 2026-11-08: photo mode noclip and the single-player body

Photo mode removes its former 40-unit travel sphere. During rendering the camera's room is looked up at its free
position (0x20C30), with the engine's all-rooms path only when outside every room; the original room and draw flag
are restored after the pass. No player position, collision or trigger state is moved. The initial camera frames
Bond from behind, and Player Character defaults to Show with a Show/Hide row.

The local body's camera mask is temporarily enabled. Its model draw (0x16C20) rejects poses older than one render
frame, so the paused body's instance timestamp at +0xA4 is refreshed for the draw only; its timestamp and render
flags are restored afterwards. The first-person gun's child objects are hidden too (model child list inst+0xA0,
count [inst+0x74]+0x29); otherwise paused magazines and suppressors float in the shot.

Validation: both Windows and Linux targets build. Exchange tests flew 160.3 units through geometry, visually
confirmed Bond, the Show/Hide row, and clean weapon-part hiding, then returned to the original first-person
camera and weapon. Test settings were restored and the test game closed. Captures: .local/rt_run/shots/photo_final_*.

## Status
- Android (2026-11-08, branch `android`, SDK `android`): libraries, DXC for Android, the app and the package build;
  in the emulator the game boots to its renderer, which needs VK_KHR_push_descriptor (the emulator has none): a real
  phone test is next.
- Linux (2026-11-08, branch `linux-port`, SDK `linux-port`): both exes build and play in WSL2 (llvmpipe), the Linux
  smoke test passes (SMOKE OK), the Linux launcher (AppImage) builds the game from GitHub; not yet on a real Linux GPU.
- Latest validation (2026-11-07): both Windows executables build; Exchange geometry, two-copy co-op, localhost host
  discovery, particle captures and wheel menu checks completed. Water is on by default: Phoenix Base's pit water
  is known by its texture keys (2026-11-07). SDK work is committed on `vulkan` at 291d356 (NVIDIA DLSS added 2026-11-08, tested on an RTX 5060); main updates still await the required Linux smoke
  tests (unavailable here) or an explicit user exception. No wheel hardware was connected for binding/force tests.
- Released: v1.0.1 (2026-10-30; v1.0.0's launcher built default.xbe alone), public, MIT; NightFireLauncher.exe from
  GitHub Actions (v1.0.1 run 41045371616, sha256 7f18f58a...).
- Milestone: 3 - plays on Windows. Harry (2026-10-28, D3D12, RX 7900 XT): the Paris driving mission to its end and
  back, then 8 minutes of default.xbe's on-foot play with his saves: no traps, 60 frames a second (a slow frame only
  when a shader compiles). Fixed on the way: black screen (gamma), mission-end crash (Driving.xbe's Reset), menu
  panels (SetPalette), the scope's colours (vertex stream read-back), a D3DX trap (recompiler)
- XDK: 4831 named in the SDK (branch nightfire): default.xbe 137 of 252 functions, Driving.xbe 115 (6 by hand);
  layout proven from NightFire's code
- Push buffers: none
- SDK: the fork's main at 37fa3dc (2026-10-31: D3D12 stage memo by contents; c4000f8 the launcher on drives without owners; 66a7066 the launcher takes every xbe; 0b5ff49 launcher updates; ed5e147 edge bars, unsuspended relaunch; 8acd824 vertex buffer locks wait for the renderer; ed1796c replacements for linear textures; 2026-10-28 ca1d0f9 point sprites) (2026-10-28: nightfire fast-forwarded in after EoN, PGR2 and PGR1 smoke and
  PGR1's frame check); the launcher can build this game now
- PC features (pc-features): skip intro, mouse look and menu mouse, key layouts, PC Settings in the main menu, sharp
  2D, ultrawide and field of view; on Windows (2026-10-29) PC Settings, mounted guns, achievements and the icon
  keypads and ultrawide work
- Smoke: scripts/smoke.sh, title -> START -> Driving.xbe's Paris mission, about 4 minutes

## Disc
- PAL, title 0x45410026 (EA), certificate region 0x4, version 2. Ghidra: `~/ghidra-projects/nightfire` (game.exe,
  driving.exe)
- default.xbe SHA-1 796222e048be3595c323b4c6d9795107132dc4ca, built 2002-10-20. Libraries XAPILIB, D3D8, D3DX8,
  XGRAPHC, DSOUND, XBOXKRNL, LIBCMT, XMV, all 1.0.4831. Sections .text 0x11000-0x10046C, D3D, D3DX, XGRPH, DSOUND, XMV,
  XPP 0x157200-0x15D150, .rdata, .data, DOLBY
- Driving.xbe SHA-1 e0c41209d8979fcd03ac3d656be78759006023cb, built 2002-10-18. Libraries XAPILIB, D3DX8, D3D8,
  XGRAPHC, XBOXKRNL, LIBCPMT, LIBCMT, DSOUND, all 1.0.4831 (no XMV)
- Data: `eurocom/filesys.d00`-`d10` and `eurocom/25_fps` (on foot), `driving/*.viv`, `.mus`, `.spe` (driving)

## Log

### 2026-11-08 - Android phase 1: toolchain, the game as Android libraries, DXC for Android, the app, a package

Game branch `android` (from main), SDK branch `android` (from 8f9b1e3, SDK main 9b7f2b5 merged). Sideload only: the
APK has no game code or files; the player's PC builds a game package from their disc, the app imports it.

- Toolchain (WSL Ubuntu 24.04, `/opt/android`, 7.4 GB with the emulator): command-line tools, NDK r27c
  (27.2.12479018), platform 35, build-tools 35.0.0, platform-tools, OpenJDK 17, Gradle 8.11.1 (the project's wrapper
  too), emulator + `system-images;android-34;google_apis;x86_64` (AVD `nf`). KVM works in WSL2.
- SDK: on Android `add_xbe_image` makes `lib<exe>.so` (`libNightfire.so`, `libNightfireDriving.so`) with SDL3 linked
  in (its JNI side exported: SDLActivity's natives, `SDL_main`); a cross build takes `xbe_recomp`/`fsr2_shaders` from
  a host build (`XBE_HOST_TOOLS`). `main_sdl.cpp`: `SDL_main`, ExeDir = the loaded library's folder (logs, hdd,
  settings, caches beside it), landscape, back gesture = Start; `kernel::Relaunch` on Android = `app::AndroidRelaunch`
  -> `GameActivity.relaunch(lib, env)` (JNI): RestartActivity in the launcher's process waits for the game process
  (`:game`) to end and starts it again with the other library and the XBE_/NF_ environment (`--env=` arguments). An
  in-process library swap was ruled out: one process cannot hold two copies of the runtime (guest memory at a fixed
  address, threads, SDL). Arm fixes: SSE state compare, cpuid, x86intrin behind guards; fibers: bionic has no
  ucontext, so an asm register switch (aarch64, x86-64); `memfd_create` via syscall below API 30; log lines to logcat
  (`adb logcat -s xbe`). Vulkan: surface remade after the app returns from the background (SDL lifecycle events),
  identity transform, INHERIT alpha where OPAQUE is missing, a log line naming why each GPU is passed over.
- Touch controls (`runtime/app/touch.cpp`, ImGui foreground drawing in the menu blues): a floating move stick on the
  left, drag to look (right stick by drag speed; the Fire button drags too), buttons from `GameInfo::touch_buttons`
  (NightFire: on foot Fire/Aim/Jump/Crouch/Use/Weapon/Gadget/Vision/Pause, driving Gas/Brake/Fire/Handbrake/Cam/
  Gadget/Weapon/Pause; OK/Back in menus); hidden while a controller is connected (SDL gamepads). `XBE_TOUCH=1` tries
  them on desktop.
- Shaders: **DXC cross-compiled for Android** (`sdk/scripts/android/build_dxc.sh`: v1.8.2407, the desktop builds'
  tag; host tablegens, then the NDK; about 12 minutes, first try, 26 MB stripped, arm64-v8a and x86_64). Chosen over
  precompiling at package time (the HLSL is generated from the game's own shaders and state as it plays: no fixed
  set) and over glslang's HLSL front end (different HLSL coverage and layout rules, no ray queries): the phone gets
  the same compiler and SPIR-V as the PC. It ships in the package beside the libraries (could move into the APK).
  `shadercache.bin` (SPIR-V keyed by HLSL hash) is portable: a later step can carry the PC's cache in the package.
- Package (`sdk/scripts/android/package.sh -r REPO [-a ABI]`): host tools, the Android build, DXC, stripped libraries
  (14 + 18 MB arm64), `assets/` stored, `package.json` (abi, sdl, main library, revisions) ->
  `out/NightFire-android-package.zip` (2.2 GB here). Never committed or uploaded.
- App (`android/`, AGP 8.7.3, minSdk 29, targetSdk 35, debug-signed): LauncherActivity (Play, Import package .zip or
  folder with progress and a space check, Settings: render %, frame cap, FPS, intro), GameActivity (SDLActivity
  loading the package library with `System.load` from `files/game`, process `:game`), PackageStore (checks ABI, SDL
  version, libraries, assets; keeps settings.toml, hdd, logs, caches across imports). SDL's Java from SDL
  release-3.4.16 source pinned by SHA-256 at build time. APK 186 KB.
- Emulator (x86_64 libraries): the APK installs, the system picker imports the 2.2 GB zip (about 20 s), Play loads
  `libNightfire.so`, SDL starts, the game is found (sha1), the kernel boots, then the Vulkan renderer stops: the
  emulator's Vulkan (SwiftShader, and gfxstream over WSL's llvmpipe at 1.3) has **no VK_KHR_push_descriptor**, which
  the renderer requires. No frame drawn in the emulator.
- Windows: BUILD OK after each step. arm64 and x86_64 libraries build and link in WSL.
- Next: a real phone (Vulkan 1.3 with VK_KHR_push_descriptor: recent Adreno and Mali drivers) via sideload: install the
  APK, copy the arm64 package, import, Play; `adb logcat -s xbe SDL NightFire DEBUG`. Then: a push-descriptor fallback
  (descriptor sets) for drivers without it, the relaunch into Driving on a phone, touch tuning, a release signing key
  and a CI job for the APK, the launchers' "Android package" button calling package.sh, the PC's shader cache in the
  package, Arm performance (`-march=armv8.2-a`, render scale).

### 2026-11-08 - Linux port phase 3: the game builds and plays on Linux (WSL2), smoke OK, a Linux launcher (AppImage)

Game branch `linux-port` (from `vulkan-renderer` bf0d7b2), its sdk pointer on the SDK's `linux-port`.

- The game's CMakeLists builds both exes on Linux unchanged: `cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` (x86-64-v2 flags as the presets), then
  `cmake --build build/linux --target Nightfire NightfireDriving`: xbe_recomp is built for Linux there and recompiles both xbes
  into `generated/` of that checkout (never committed). In WSL2 (12 threads, clang 18) the whole build takes about 3 minutes.
  Exes `Nightfire` and `NightfireDriving` (no .exe) with `libdxcompiler.so` / `libdxil.so` beside them.
- src: `nf::SetEnv` (src/common/env.cpp: SetEnvironmentVariableA on Windows, setenv off it) for XBE_NETPLAY (co-op's
  restart) and NF_PRESENCE (Discord's driving mission name, now handed to Driving.xbe's exe on Linux too). Nothing else
  in src/ was Windows only.
- SDK ba34499: the Linux relaunch (exec) closes the sockets first and marks every descriptor close-on-exec (the display
  connection, sound and the GPU stayed open in the new image); a failed exec is logged and shown.
- Run in WSL2 under WSLg (`.local/lx3/run_driving.sh`; llvmpipe, Vulkan 1.4, about 9 to 20 frames a second at 1280x720):
  the movies, the title (DejaVu Sans for the modern menus), START with skip_paris (default.xbe relaunches itself into the
  menus: main menu page 40000002) and with NF_SKIP_PARIS=0 into `NightfireDriving` (exec), which plays Paris: the van's
  sniper tutorial drawn (shots `.local/lx3/s*.png`), RT presses from XBE_PAD_SCRIPT. No traps. Sound: SDL's PipeWire/Pulse
  through WSLg (PipeWire's missing client.conf warns).
- Smoke test (`scripts/smoke.sh` in WSL2, the Linux-recompiled `generated/`): **SMOKE OK: menu 24 draws a frame,
  relaunched into driving, which draws 445 a frame over the last 300 frames, no traps** (about 3 minutes). The harness
  had not linked with this game's PC features: SDK e335d46 `harness/game_headless.cpp` stands in for what they call in
  the files the harness leaves out (gpu/style.cpp, app/*, input.cpp, the wheel). Headless, a relaunch with no image
  beside the harness is logged as info. The harness wants toml++'s headers: in WSL
  `CXXFLAGS=-I<build>/_deps/tomlplusplus-src/include` (or Ubuntu's libtomlplusplus-dev).
- Launcher (SDK b01a85b, 4f5e247): the builder service (`sdk/launcher/service.cpp`, moved from win/) builds and runs on
  Linux (`posix/platform_posix.cpp`): toolchain.json's new `linux` section (LLVM 19.1.7, CMake 3.31.12 tar.gz,
  Ninja 1.13.2, pinned by SHA-256; older launchers read only the top level) with `system` programs that stand in for
  a download (the system's clang 14+, CMake 3.25+, Ninja), tar.gz unpacking, execute bits kept, `CheckSystem` (git, a
  clang++/lld test program, X11 or Wayland and sound headers; the missing ones named with the apt/dnf/pacman/zypper
  command), no Microsoft license on Linux, exes without suffix and `.so` files installed, the game started detached.
  Electron (`launcher/electron`): the service without .exe, the AppImage's folder as the launcher's, running games
  found through /proc (PowerShell on Windows only), `npm run dist:linux` makes `NightFireLauncher-x86_64.AppImage`;
  `build_service.sh`; `launcher/icon.png`. CMakePresets work on any host. BUILDING.md: Linux.
- Checked in WSL2: launcher_cli fetched and unpacked the three Linux tools runnable; the service (game.json on
  linux-port) with the user's assets: the system's clang 18.1.3, CMake 3.28.3 and Ninja 1.11.1 used, the system check
  passed, clone, configure and build in 316 s, both exes and the .so files in play/, Play started the game (menus
  drawn). The AppImage (built in WSL, 117 MB) starts its window and its service under WSLg (Electron needs the
  usual desktop libraries: libnss3, libgtk-3 ...); its home page shows the Linux text and no license row.
- CI: SDK linux workflow run 42461415684 green (harness, runtime on 24.04 and 22.04, the new launcher job: service
  state, Ninja/CMake fetched and run). This repo's release.yml has a linux-launcher job (Ubuntu 24.04: service,
  AppImage, artifact `NightFireLauncher-Linux`; on a tag `gh release upload` after the Windows job made the
  release): run 42461517649 (dispatched on linux-port) green, both jobs. A push to linux-port did not start it (only
  the dispatch did).
- Windows: BUILD OK after each step (game, launcher, service; the service answers with `"os":"windows"`); a run in
  `.local/rt_run` reached the menus.
- Left for a Linux release: a run on a real Linux GPU (Mesa RADV/ANV, NVIDIA) and its frame rate, controllers and
  sound there; the AppImage on a desktop distribution (Ubuntu, Fedora, Arch, Steam Deck) including its system check
  and the LLVM fallback; hd_pack (texture packs) and DLSS are Windows only; refresh rate and HDR through SDL; the SDK's
  linux-port merged into its main (with the other games' smoke tests) and this branch into main.

### 2026-11-08 - Linux port phase 2: the whole runtime compiles and links on Linux

SDK branch `linux-port` (from `vulkan` 6fd2a81; this repo's sdk pointer is not moved until it is merged). The SDK now has
a Linux CI (`.github/workflows/linux.yml`, ubuntu-24.04 and 22.04, clang): it builds the headless harness and the
whole runtime (SDL3 window and sound, the Vulkan renderer, ImGui, FSR 2) around a stub image, without game code or
files (`sdk/tests/linux`: `make_stub.py` writes the stub from a generated folder, every host symbol absent). Also
checked in WSL2 (Ubuntu 24.04, clang 18): both link.

- CMake (`XbeRecomp.cmake`): `xbe_runtime_target()` split out of `add_xbe_image` (used by the CI target too). Off
  Windows: no D3D12/11, XAudio2, DirectInput wheel or Win32 window sources; SDL3 headers for input, `main_sdl.cpp` and
  `audio/sdl.cpp`; imgui's SDL3 backend (1.91.0 kept: built with `SDL_ENABLE_OLD_NAMES` for SDL_TRUE/FALSE);
  `runtime/compat` on the include path; stb_image / stb_image_write (pinned commit) for `app/image.cpp`; DXC's Linux
  release (`libdxcompiler.so`, `libdxil.so`, same v1.8.2407) copied beside the exe; FSR 2 built with `FFX_GCC` and
  `tools/fsr2_shaders/posix_compat.h` (`_countof`, `wcscpy_s`). Windows builds as before.
- Vulkan renderer: DXC moved behind `gpu/dxc.h` (its COM headers only in `gpu/dxc.cpp`; dlopen off Windows). Off
  Windows the loader is SDL's (`app::VulkanGetInstanceProcAddr`, else `libvulkan.so.1`), the instance extensions and
  surface come from `app::VulkanInstanceExtensions` / `CreateVulkanSurface`, the size from `WindowPixels`, ImGui from
  `ImGui_ImplSDL3_NewFrame`; no HDR luminance query or refresh rate (not known there yet). `CreateBackend` off Windows
  is Vulkan with no fallback.
- `runtime/compat` (shared by harness and Linux build): `windows.h`, full `dxgiformat.h`, the D3D12 enums and sampler
  description `d3d_common` names (`d3d12.h`, `d3dcommon.h` with a portable `ID3DBlob`, `d3dcompiler.h`'s
  `D3DCreateBlob`), a small `wrl/client.h` ComPtr. `gpu/ext.h`'s D3D12 parts are Windows only.
- App: `game_files.cpp` (SDL message boxes through the new `app::MessageBox`), `system_info.cpp` (os-release, cpuid,
  sysconf, SDL displays, the environment), `overlay.cpp` (SDL's display modes, `input::KeyDown`), `image.cpp` (stb).
  `core/netplay.cpp`: getifaddrs, gethostname, no wait box (log + Escape).
- Harness: built again on Linux (smaa, upscalers, dxc left out; toml++ include via `CXXFLAGS`; `MixHitSound` stub).
- Windows: BUILD OK after each step; a Vulkan run in `.local/rt_run` reached the menus at 60 frames a second.
- WSLg: SDL video starts (display "rdp-0"), the missing-game message box shows; Vulkan there is llvmpipe only (no
  D3D12-backed driver in Ubuntu's Mesa), so a real render test needs a Linux machine or a GPU passthrough driver.
- Phase 3: the game target on Linux (the recompiled code built with `add_xbe_image` there, the player's own disc,
  `src/` Win32 bits such as `GameInfo::message`), the launcher building on Linux, an AppImage, a refresh rate and HDR
  query through SDL, and the smoke tests on a real Linux GPU.

### 2026-11-08 - Photo mode, screenshots, performance overlay, Discord, achievements page, quick restart

- SDK (`vulkan` 6fd2a81): `gpu::RequestScreenshot` (the Vulkan present's XBE_PAD_CMD shot path, reported written),
  `gpu::LastFrameStats` (the last frame's GPU time from the frame timestamps, scene and output sizes, upscaler),
  `app::HideOverlays`, `app::SetPresence` (app/discord.cpp: Discord's local RPC over `\\.\pipe\discord-ipc-N` or a
  Unix socket, handshake then SET_ACTIVITY on a thread; settings `discord`, `discord_app_id`, empty id = off).
- `src/common/pc_extras.cpp`: GameInfo::ui is nf::GameUi: the modern menus, the screenshot key (F12, Screenshots\
  beside the exe, a "saved" note kept out of the next picture), the performance overlay (More Settings' Performance:
  Off / FPS / Full; `perf_overlay`).
- `src/game/photo_mode.cpp`: from the pause screen's fifth choice PHOTO MODE (GetText answers 0x0200FC07) or F10 (Start
  pressed first). The game stays in its pause; camera 0's world matrix (+0x108, rows right, up, forward, position) and
  field of view (+0x104, +0xF4 = 1) are replaced only around 0xDAC20 and put back after, the first person gun hidden
  (+0xCC | 0x10) for the pass; every 0xD9410 list skipped; MenuUp false (the mouse looks, the modern menus go); the
  pad kept from the game (PhotoPad) and after leaving until let go. +0x180 is neither a copy nor the inverse of +0x108
  (logged) and was left alone: the view follows +0x108. Panel: field of view, exposure (Original drawn as a neutral
  Enhanced for it), depth of field (Focus, strength), look (a preset's look over the current effects); restored on leaving.
- `src/game/quick_keys.cpp`: RESTART's question ("RESTART: ARE YOU SURE?") is a second list widget of the same id
  (0x10000028) made after A's handler returns: found among the page's shown lists each frame, its entries YES, NO
  with NO shown, so Up then A, played on the pad (QueuedPresses, once the player's own buttons are let go). The
  restart key (none by default) presses Start, Down, A first. Discord's presence ("Playing <mission>" + difficulty,
  "Multiplayer: <map>" from 0x17C6A0 / 0x260040, "In the menus"); a driving mission's name reaches Driving.xbe's exe as
  NF_PRESENCE.
- `src/game/graphics_page.cpp`: More Settings is two sets (Next Page / First Page): Performance, Achievements (two sets of
  the 19, the service ones' progress from the score records), aim down sights, Aim Zoom (`ads_zoom`, iron sights
  only), weapon sway, Discord, Reset to Defaults (A twice; keeps keys, wheel, display, language, host, Discord id).
  Key Bindings' second set: Screenshot, Photo Mode, Restart Mission (`screenshot_key`, `photo_key`, `restart_key`).
  In-place set switches focus their first row two frames later: focusing inside A's handler from slot 13 overflowed
  the guest stack (recursion 0x90921 / 0x728AB / 0x93775 ...).
- Launcher: performance overlay, aim zoom, a Discord group (status, application ID), hotkeys.
- Tested in `.local/rt_run3` (`.local/vk/feat_test*_f.sh`, `feat_menu_f.sh`, keys sent with `key.py` after bringing the
  window to the front): F12 PNGs written; overlay; photo mode by the pause screen and by F10 (moved, looked, FOV and
  exposure steps, Y's picture without the panel, back to the pause screen / to play); restart from the pause screen
  and by key (Bond back at the start); both achievement sets, page 2, the reset. Discord against a stand-in client
  (`.local/vk/fake_discord.py`): handshake, "In the menus", then "Playing The Exchange" / "Operative"; without a client
  nothing but one log line. Not tested: a real Discord client and application id, the classic (non-modern) pause list
  with five entries, photo mode in split screen (camera 0 only).

### 2026-11-08 - NVIDIA DLSS (Super Resolution and DLAA); XeSS choice removed

DLSS is an upscaler beside FSR 2 on the Vulkan renderer (SDK `gpu/upscale_dlss.cpp`). Licence: NVIDIA's DLSS repo is
under the NVIDIA RTX SDKs License: object-code redistribution of nvngx_dlss.dll inside an application is allowed
(notice and terms in THIRD_PARTY.txt); no reverse engineering, not stand-alone, not under an open source licence (the
SDK is fetched at build time, never committed; our MIT code only calls it); NVIDIA GPUs only; NVIDIA wants a
notification before a commercial release and NVIDIA marks need its approval (we use none). CMake (`XBE_DLSS`, ON)
downloads v310.9.16's headers, `nvsdk_ngx_d.lib` (msvcrt) and `rel/nvngx_dlss.dll` file by file at the tag's commit
with SHA-256 checks (the archive is ~1 GB of every platform), links the lib and copies the dll beside both exes; the
launcher's install already copies every build dll into play\, release.bat adds it to the zip. At start the renderer
asks NGX for DLSS's Vulkan extensions (instance and device; bufferDeviceAddress on), starts NGX (project id, custom
engine; ~0.5 s, NVIDIA GPUs alone) and offers DLSS only if SuperSampling is available. It takes FSR 2's inputs: the
jittered scene at render size, depth, camera motion (MVLowRes, scale = render size), the reactive mask as DLSS's bias
mask, frame time; output at display size before the HUD. Qualities: DLAA (Native), Quality 1.5x, Balanced 1.7x,
Performance 2x, new Ultra Performance 3x (FSR too). DLSS has no sharpening (the row is FSR's). The DLSS choice on a PC
without it upscales with FSR 2; the in-game row says "DLSS (Not Available)". XeSS is gone (Off / FSR / DLSS; an old
'xess' reads as Off, also in the launcher). Lessons: NGX init returns InvalidParameter when logging has no sink at all
(level off, no callback, other sinks disabled): always pass the callback. DLSS clears its output with
vkCmdClearColorImage: the output needs TRANSFER_DST.

Evidence (RTX 5060, 1920x1080, The Exchange, remaster settings, GPU frame ms from XBE_PERF): off 22.9; FSR native
23.9 / DLAA 24.2; Quality FSR 15.2 / DLSS 17.0; Performance FSR 12.3 / DLSS 13.9; DLSS Ultra Performance 11.1. DLSS
costs ~1.6 ms more than FSR at the same render size and draws finer foliage and edges (zoomed crops
.local/rt_run/shots/dp_crops.png). XBE_VK_DEBUG=1 over DLAA, Quality, Performance and back to FSR: no validation
messages. Next: DLSS on Harry's PC (non-RTX: shown as not available, FSR used).

### 2026-11-08 - Linux port phase 1

Groundwork for a Linux build (SDL3 window, sound and keyboard/mouse). There's no Linux machine here, so the Linux code
is written but not compiled yet. The Windows build stays green after each step.

- Input (SDK 2ae6e45): `input::KeyDown(vk)`, `GameFocused()` and `KeyHeld(vk)` replace the `GetAsyncKeyState` calls in
  `src/common/menu_mouse.cpp`, `src/game/controls.cpp` and `src/game/weapon_keys.cpp`. Windows still reads
  `GetAsyncKeyState`. Linux reads SDL's keyboard and mouse state through a VK to scancode table, because settings and
  key bindings stay stored as VK codes (`core/vk_codes.h`). On Linux the window thread passes controller plug/unplug
  events to input (`input::HandleSdlEvent`), and the steering wheel is stubbed out. Harness: stubs, `XBE_HEADLESS`.
  Windows smoke test (`.local/rt_run2/kb_test.sh`, real keys sent through user32): Enter in the difficulty menu started
  The Exchange, W walked forward, the mouse turned the view and S walked back.
- Sound: `audio/sdl.cpp` implements `audio::StartOutput` with SDL3's `SDL_AudioStream`. It follows the XAudio2 version:
  10 ms chunks, silence while paused, and a silent real-time pull when no device opens.
- Window: `app/main_sdl.cpp` is the Linux entry point and SDL3 event loop, doing what `app/main.cpp` does on Windows:
  - window modes: windowed, borderless (desktop fullscreen) and fullscreen through SDL display modes
  - pause on lost focus
  - mouse look through SDL's relative mouse mode
  - the menu pointer as an SDL colour cursor
  - F1, F3 and F4
  - key binding capture
  - ImGui's SDL3 backend
  - boot and quit

  It also provides SDL's displays and the hooks phase 2's Vulkan changes will call: `app::VulkanInstanceExtensions`,
  `CreateVulkanSurface`, `VulkanGetInstanceProcAddr`, `WindowPixels` and `DisplayModes`. A Linux relaunch passes
  `XBE_RELAUNCHED` and `XBE_INSTANCE` through the environment. Linux has no handover. `settings.cpp` guards its Win32
  parts.
- src portability:
  - `settings.toml`: the key tables and the ImGui modern menus now build on any non-headless build. The guard is
    `XBE_HEADLESS` (defined by `harness/build.sh`) instead of `_WIN32`.
  - Fonts: the menus look for DejaVu, Noto or Liberation Sans in the usual Linux font folders and fall back to ImGui's
    own font.
  - `start.cpp` reads `XBE_RELAUNCHED` on Linux.
  - The PC Settings resolution list comes from `app::DisplayModes()` on Linux.
  - LAN play's UDP sockets are now built everywhere through the Winsock shim, which moved from `sdk/harness/compat` to
    `sdk/runtime/compat`. The player and computer names come from `gethostname` and `$USER`.
- Images: the WIC code for the splash and texture packs now sits behind `app::DecodeImage` / `app::EncodePng` in
  `app/image.cpp`. On Linux these are stubs that return false for now (no splash, packs or dumps), with a TODO for
  stb_image. Windows check: a texture pack's 187 PNGs loaded and a shot PNG was written. That run had no splash file,
  so the splash path wasn't exercised.
- Phase 2 needs to:
  - add a Linux branch to `XbeRecomp.cmake`:
    - `runtime/compat` on the include path
    - `-DXBE_HEADLESS` must not be defined there
    - SDL3 includes for the app and audio files
    - imgui's `imgui_impl_sdl3.cpp`; 1.91.0's backend may need a newer imgui to build with SDL 3.4
    - leave out `wheel.cpp`, `d3d12/11`, `xaudio2.cpp`, `main.cpp`
    - stb_image
    - DXC's `libdxcompiler.so`
  - make the Vulkan renderer's Win32 code portable:
    - surface through `app::CreateVulkanSurface`
    - the loader from `VulkanGetInstanceProcAddr` or a dlopen of `libvulkan.so.1`
    - `GetClientRect` replaced by `WindowPixels`
    - HDR query and `MonitorFromWindow`
    - `ImGui_ImplSDL3_NewFrame`
  - split `d3d_common.h`.
  - port `game_files.cpp`, `system_info.cpp` and `overlay.cpp`: message boxes, environment and display modes.
  - port `netplay.cpp` and `xnet.cpp`: their `HWND` uses still need checking.
  - add a launcher.

### 2026-11-07 - Water: Phoenix Base's pit found, modern water shader, on by default

- Found the water: Phoenix Base (multiplayer, Split Screen -> Arena -> 4th map, one bot) has it at the bottom of the
  central pit (x about 22, z -1, y about -12). Its surface is a 64x64 grey ripple (ARGB) animated through 12 pictures,
  drawn blended with depth writes, nearly invisible in the original. XBE_WATER_TEST=2 numbered every texture and
  overlay 2 drew each pixel's number as a colour, which read the pit as one key; the other 11 frames share its size,
  mean and spread in the survey log. The old colour test never matched it and matched suits, carpet and sky instead.
- Detection: src/common/game_info.cpp registers the 12 keys (gpu::AddWaterTextures); mods/reflections.toml's [water]
  table adds or removes keys; a water picture that changes in place within 8 frames passes it on (animation frames).
  No colour guessing by default.
- Look (SDK water_shade): eight travelling sines for smooth normals, refraction of the scene captured before the
  water, depth absorption to a deep blue-green, screen-space reflections with a room fallback and Fresnel, glints
  from the game's six nearest lights, a soft animated foam band where walls stand in the water. Fixed on the way
  with the user's screenshots: a foam halo round the gun (far-nearer pixels are not edges), stepped foam (8 turned
  directions, 6 radii, smoothstep), blocky glints (value-noise chop replaced by sines; the surface placed from the
  scene depth, not the mask's 16-bit distance). The water passes run only in frames that drew water.
- Particles: our sparks and dust are skipped when the hit made the game's water splash (texture 0x2000129); the
  game's splash is kept. Not yet seen in a log: pit-floor hits read material 6, no splash was caught.
- water is on by default (style, launcher; Graphics > Effects > Water / Floors unchanged).
- Diagnostics (all env-gated, off by default): XBE_WATER_TEST (+ water_pick file), NF_WATER_TEST (player place
  logged, water_warp file moves the player; =bots holds bots still).
- Evidence: before `.local/rt_run/shots/wt_before_pit.png`, after `.local/rt_run/shots/wt_after_pit.png`,
  number view `wt_o2.png`. Not checked: other maps (Atlantis, Sub Pen) and story levels may have other water textures.

### 2026-11-07 - Remaining feature checks: geometry, network co-op, water, particles and wheel

- Geometry: the 20:37 build passed. Exchange comparison `ge10` records about 18.9 ms GPU without refinement and
  19.0 ms with it, 38.5 refined draws and 18,101 triangles per frame. Red/green in that comparison is the intentional
  XBE_DISP_TEST=6 view. Fixed the local perf script's corrupted sed back-reference, which falsely said NOT IN LEVEL.
- Network co-op: two local copies of the rebuilt game reached the Exchange with two players. Client and host movement
  were exercised; both logs report the same player separation at level frames 600, 1200 and 1800 (3.2, 12.4, 7.6).
  No DESYNC, rendering errors or peer timeout in either log. Evidence: `.local/netcoop_{host,client}_checked.log`,
  `.local/rt_run/shots/continue_net_*`. A separate localhost discovery probe received the waiting host's correct
  name, UDP port and player count (`.local/discovery_checked.log`). A remote PC and broadcast discovery remain untested.
- Water prototype (off by default): `water` in graphics.toml, F1 and launcher; Graphics > Effects > Water / Floors.
  Its own reflection-mask tag selects animated normals, screen-space reflection/refraction, depth absorption and
  shallow-edge foam. The RT reflection pass leaves that tag to the later surface pass; ordinary RT reflections
  are not applied twice. Sky is excluded; only square 64-128 textures with opaque, smoothly varying patterns are
  accepted automatically. Survey pictures confirmed that this rejects the observed carpet, clothing and splash
  false positives (adjacent-pixel variation separates the carpet from the smooth water candidate).
  HD replacements retain the original texture's water classification when the option is switched live.
  Both executables build; Double Cross off/on comparison compiled the shader without errors. Real water placement
  still needs visual verification: colour alone also finds suits, carpet, sky and splash pictures. The option
  remains experimental. `mods/reflections.toml` entries set to zero exclude misidentified textures.
- Local water survey fix: use `cygpath -aw` for XBE_WATER_DUMP; a relative script path otherwise gave the game a
  dump folder relative to its run directory and silently failed to write the candidate pictures.
- Particles: Exchange gunfire with NF_FX_TEST=1 and XBE_PARTICLE_TEST=1 exercised both real hit/explosion hooks and
  synthetic events. Captures show sparks, debris and smoke with the gun intact. The runtime reports GPU particles
  drawn, real hit positions/materials and real test explosions; no shader, Vulkan or guest exception errors.
  Evidence: `.local/particles_checked_runtime.log`, `.local/rt_run2/shots/continue_particles_*`.
  Initialized the diagnostic explosion log's fallback coordinates so a rejected place never logs uninitialized data.
- Wheel menu: fixed hidden row gaps that blocked Buttons and Gear Shifter, and the guest overwriting the initial
  focus after a page switch (focus now settles on the first row two frames later). Modern menus have a fallback
  for the row help when the guest omits its help widget. Retested Steering/Pedals -> Buttons -> Gears -> Steering,
  No Shifter, the no-device help, and rotation changing 900 -> 1080 -> 900 with the saved settings updated.
  Evidence: `.local/wheel_checked_runtime.log`, `.local/rt_run/shots/continue_wheel_final_*`. Zero devices attached:
  physical axis/button capture and forces are not verified. All test settings were restored.
- Final validation: Windows BUILD OK (`.local/b_wheel_focus.log`), launcher schema syntax and both diffs' whitespace
  checks pass. Main still requires the Linux smoke tests in CLAUDE.md. `wsl --list --quiet` confirms WSL is not
  installed; no Linux tests were claimed or bypassed.

### 2026-11-07 - Modern menus (blue, for the mouse)

- New option modern_menus (settings.toml; launcher Display > Menus: Classic / Modern; F1 Game). The game's menu pages
  are drawn again with ImGui over its background movie: a navy panel, the page title, rounded rows with the focused
  one lit electric blue, a choice's or slider's value between arrows, the page's own button line as the footer, and a
  Back pill. Font: Bahnschrift from Windows (Segoe UI if missing); nothing is shipped.
- src/game/modern_menu_frame.cpp (game thread, the pad reader's hook) reads the page up each frame from its live
  widgets: labels +0xF4, buttons +0x188, a choice's item +0x304 (+8), a slider's value from message 0x40. A list
  (a widget with no label, plus five stacked lines for the entries around the current one) becomes one row per
  entry. NF_MM_DUMP=1 logs each page's widgets.
- Input: a mouse move hovers (the game's focus moves there via MenuFocus); a click presses A, or Left / Right on a
  value; a click on a list entry presses Up / Down to it first, then A; right-click or Back presses B. Clicks come
  from input::MouseButtons, so test scripts' CLICKL works too. The keyboard and controllers drive the game as
  before. A mouse resting over a row does not take the focus. The classic menu mouse is off meanwhile, and the
  game's own widget sprites are not drawn (0xD9410 skipped). Pages with no rows (Join Game's four panels) stay classic.
- Blue: with modern menus on, the menu movies are always recoloured. The main menu's column behind Bond turns too
  (it stayed orange). The title screen's 512x512 picture and the 007 logos (ARGB, swizzled) turn blue in place;
  Bond's face, hand and collar keep their colour (bright, and green over half the red). Movies in 4:3 now skip the
  menus' background movie.
- Checked (`.local/vk/mm_test.sh`, `title_test.sh`): main menu, Settings (sliders, choices), Edit Codename (list), Edit
  BOND, hover and clicks; the title screen dumped (NF_TITLE_DUMP) and checked.

### 2026-11-07 - Pale sunlit walls and Phoenix Fire's bright walls fixed (the relight's lamps)

- Found with new test views: XBE_RT_TEST=11 passes the relight's input through, and it is clean. rt_sun=0 still gave
  white walls, so the cause was neither the sun nor the fog: it was the game's placed lamps, divided by the baked
  light. (The earlier "ratio about 2" reading was wrong: test 5 still goes through exposure and tone mapping.)
- Outdoors on a bright day (baked brightness 0.5 to 0.9) the lamps now fade to 15%: daylight drowns them. The relight
  ratio is capped at 2.5 instead of 4, so a dark wall beside a lamp no longer reaches four times its baked light.
- Checked (`.local/vk/readme_shots.sh`): Chain Reaction's concrete keeps its texture; Phoenix Fire's walls are dark wood
  brown, not glowing pink; the Exchange looks the same as before.

### 2026-11-07 - Look fixes (grade, exposure), 4:3 movies, water detector; pale sunlit walls still open

- Double Cross's orange (fixed): the grade's curves work per channel, so a strong colour got stronger and moved
  toward yellow, and saturation 1.25 added more. Strongly coloured pixels now take most of the curves' change in
  brightness alone, keeping their hue and saturation. Saturation over 1 now acts as vibrance: it lifts dull colours
  and leaves strong ones nearly as they are. Checked: Double Cross is warm, not burnt orange; the Exchange's snow is
  unchanged.
- Auto exposure: bright scenes are taken down 2.8 times as firmly as dark ones are lifted, with a floor of 0.35.
- Movies 4:3 (movie_43, on): a movie's draw (stage 0 YUY2) on a 4:3 frame stretched wide is squeezed back about the
  middle (DrawConstants::hud[0] = -1 / stretch), with black at the sides. Checked on the attract movie.
- Water: gpu::TextureWater (blue or green, not bright, even), behind XBE_WATER_DUMP=folder for now: logs and dumps
  candidate textures, to find the levels' water before the shader.
- README: Night Shift (07000009, the teal-green garage) added to the indoor comparison.
- NOT FIXED after 3 attempts, stopped as CLAUDE.md asks: Chain Reaction's sunlit pale concrete goes flat white with
  ray tracing on (with it off it keeps its texture). Settings off one at a time (`.local/vk/set_test.sh`): materials,
  the texture pack, reflections, ground detail, bloom and the grade change nothing; ray tracing off fixes it.
  Tried: (1) on a bright day the outdoor sun ratio scaled so full sun equals the baked light (k from the pixel's baked
  brightness); (2) the auto exposure changes above; (3) relighting only the surface's share of a fogged colour
  instead of dividing by the fog factor and clipping. None changed it. XBE_RT_TEST=5 (the ratio halved) still shows
  sunlit walls near white, a ratio of about 2, while the deck in shade is about 1. So the sun reaches the ratio
  another way: likely through `added` (lamps() may include the sun or effect lights) or the open() test failing on
  walls, which takes the indoor branch. Next: show `added / baked` and the open() result in their own test views.

### 2026-11-07 - Movies upscaled (Vulkan)

- New option movie_upscale (graphics.toml, on; F1 > Movies): each new frame of a movie (YUY2 640x480, decoded to BGRA
  when uploaded) is enlarged on the GPU to the screen's height (1.5 to 4 times) before it is drawn. The first pass is
  Lanczos 2, clamped to the nearest 2x2 texels so hard edges don't ring; the second sharpens like AMD's RCAS (0.8).
  The bigger picture is bound in the frame's place: a linear texture's texel coordinates are divided by the guest
  size (FillTexelConstants), so it fits.
- Tested: the attract movie at 1920x1080 is upscaled to 1440x1080, runs at 60 fps with no errors, and looks clean
  (`.local/vk/movie_test.sh`).

### 2026-11-07 - Daylight whiteout; README indoor pictures from Phoenix Fire

- Remastered settings bleached daylight levels white (Chain Reaction 0700000C, Phoenix Fire 07000011). Switching
  settings live one at a time (`.local/vk/white_test.sh <level> <tag>`) showed the ray-traced volumetric light was the
  haze: on a day whose own fog is already pale (fog 0.82) it added as much again. The volumetric strength is now cut by
  up to 75% as the fog's brightness goes from 0.45 to 0.8 (nights unchanged).
- Auto exposure: a bright scene is now taken down 1.8 times as firmly as a dark one is lifted, with a floor of 0.5
  (was 0.7). Dark scenes (The Exchange) are unchanged.
- Checked: Chain Reaction's haze gone; Phoenix Fire lit but not washed out; The Exchange the same as before.
- README: the indoor comparison is now Phoenix Fire instead of the garage.

### 2026-11-07 - README pictures

- New README screenshots (1600x900 jpg, HUD hidden, fps counter covered): The Exchange and the indoor garage level
  (07000027), each Original and Remastered (the Enhanced look with ray tracing, GI, volumetric and ACES with auto
  exposure), and split-screen story co-op in The Exchange. Story co-op added to the README's feature list.
- Taken with `.local/vk/readme_shots.sh <level> <tag>` and `readme_coop.sh` (settings in `.local/vk/remaster.toml`).

### 2026-11-07 - Bug sweep

- Single player (The Exchange, every effect): no error, failed shader or trap in the log. Split screen co-op: no errors,
  the animations put in, two views. Paris (rt_run copy, the release folder's save): START launches Driving.xbe into
  Paris; with rt_run's own save START goes to the menus (that profile is past Paris: not a bug).
- Fixed: co-op's HUD places kept by sprite address across levels (a new level's sprite at the same address would
  have been moved to another's place); cleared at each level.
- .local/vk/paris_rt.sh: Paris in the rt_run copy (stops only its own processes).


### 2026-11-07 - Co-op mission end: not yet seen

- [0x1DF19B] = 1 (0x9C227 sets it with messages 0x2000059/5A; 0x9C31F then sets the mission state [0x17E540] 2)
  ends the mission as failed ("Mission Failed: Replay / Quit", drawn over player 1's view): not the success flag.
  The success state of 0x9C2BC's machine is not yet known; a co-op mission's save and unlocks not yet seen.
- coop.cpp's level frame count counted only with NF_COOP_KILL2 set: now always.


### 2026-11-07 - Co-op friendly fire works (players' hit lists)

- .local/ff_notes.md: 0x3D900 damages NPCs and bots only; a player is damaged from its own hit list ([obj+0xAC],
  read after the collisions 0xAB450, applied by 0xADD00 / 0xAC0A0). nf::CoopHits (after 0xAB450): a node from the
  other co-op player's round (+0x4C the projectile, its +0xBC +0x24 the shooter) keeps its damage (+8) with friendly
  fire on, is zeroed without.
- Checked (.local/vk/ff_test.sh): player 2's three rounds hit player 1 for 3.5 each; with friendly fire off the same
  hits "taken away". (The earlier "no hit" was the log watching 0x3D900.)


### 2026-11-07 - Menu pointer: no walking through a menu by itself

- A row under the pointer the focus could not reach (the menu's order unlike the rows' places) was pursued for up to
  32 d-pad presses: the menu moved by itself. Now a step that brings the focus no nearer gives the row up at once
  (16 steps at most), and the pointer leads only after moving 8 game pixels from where it rested.
- Checked (.local/vk/menukeys_test.sh): the pointer resting on Settings, the d-pad's down moved the focus and it stayed.
- Friendly fire: player 2's shots at player 1's chest never reach 0x3D900 (logged): the story's shots skip players.
  RE under way (.local/ff_notes.md).


### 2026-11-07 - Network play's wait shown; the menu pointer no longer steals the focus

- Netplay (SDK): while the lobby waits (the title not yet started, the window black) a box says so ("waiting for
  player 2 to join", or joining) with OK to stop waiting and play alone (Escape in the window too); the window's title
  says it too; the box closes itself once everyone is in. The user saw Network: Host as a freeze. Checked: the box
  shows, closing it starts the title alone.
- Menu mouse: a pointer resting over a row led the focus on every new page (the user: "menus move by themselves",
  keyboard navigation hard). Now on a new page, and after a d-pad or key move, the pointer leads again only once it
  moves 4 game pixels from where it rested.
- Select Difficulty after the Co-op page checked: back, up to Operative, A opens the mission list.


### 2026-11-07 - Network co-op: story co-op over the SDK's lockstep netplay

- The existing netplay (sdk core/netplay.h: every copy runs the whole game, only the controllers travel, a state
  hash compared each minute) carries story co-op: the host's controller plays player 1, the joiner's player 2,
  each copy shows the same split screen. (lan_play.cpp's puppets suit a match, not a story level: each copy would
  run its own guards.)
- Checked with two copies on this PC (.local/vk/netcoop_test.sh): the menus, then The Exchange in co-op on both
  (150 animation records, two views), the copies agreeing at frames 0 and 3600, no desync. A walk on each pad was
  not seen to move (the scripted stick, also flaky alone); not yet played across two PCs.
- Co-op page: Off / Split Screen / Network: Host / Network: Join. Network sets XBE_NETPLAY (host, or the settings'
  join_ip, now editable in the settings window's General) and relaunches (kernel::Relaunch); back to Off or Split
  Screen relaunches without it. Checked: Network: Host restarts as "hosting on UDP port 27015, waiting for 2 players".
- Both PCs need the same saved data and settings for the menus to match (netplay.h).
- Movement checked (the pointer parked on both copies first: one over a row pulls the menu's focus): each copy's
  stick moved its player, 1.2 to 7.4 units apart, the same on both copies, no desync.


### 2026-11-07 - Story co-op: the multiplayer's player animations in story levels (src/game/mp_anims.cpp)

- .local/anim_load_notes.md: level files are a table of sub-files (kind 4 motions, 5 animations) read whole into the
  level heap; resources registered in the sorted table 0x1F6680 (0x6B420 insert, 0x6B540 ranges, 0x138E0 a motion's
  in-place fixup). The 75 animations of the walk tables and posture moves and their 75 motions are only in the
  multiplayer levels' files.
- Each co-op story level, before player 1 is made (0xAE870), those 150 records (542,688 bytes, read once a session
  from 07000023.bin through the game's archive: 0xE2D50 / 0xE3280 / 0xE2D90) are copied into a block outside the
  game's heap and registered. Then 0xA91E0 / 0xA9350 / 0xA94C0 / 0xA9590 run with the multiplayer flag for the co-op
  players: the match's own walk sets and posture moves. (Without them, the guards' set as before.)
- Checked: "150 multiplayer animation records put in level 07000005 (found)"; player 2's view shows player 1 in the
  match's aiming stance. The third person weapon itself not yet seen in the hand.


### 2026-11-07 - Co-op in single player's Select Difficulty

- .local/sp_menu_notes.md: Select Difficulty is page 0x40000023, list 0x100000BF, handler 0x86410, table 0x17C760
  (3 entries; A stores the difficulty at [0x1F65A8] and opens the mission list 0x4000001C). The hook gives the list a
  copy with a fourth entry, Co-op (Agent's picture; label and help through the GetText hook, handles 0xFC04/0x0100FC05),
  its range 0 to 3, draws its rows from the copy, and A on Co-op opens the Co-op page (back returns to the list; no
  difficulty stored). Checked on screen (.local/vk/spcoop_test.sh).


### 2026-11-07 - A Co-op page; player 2's respawn seen

- Co-op page (the shared page's set 9, nf::OpenCoopPage; More Settings > Co-op > Open for now): Co-op Off / Split
  Screen / Network (not ready: kept off, says so), Screen Split one above the other / side by side (coop_side: the
  game's [0x1F660C] set for the level, put back at the front end), Friendly Fire, Keep Together (coop_leash),
  Player 2 Respawn (coop_respawn), Keep. Checked on screen.
- Player 2's respawn seen: killed (NF_COOP_KILL2) at 161 s, back beside player 1 with 100 health at 166 s.
- Next: the entry in single player's flow (RE under way, .local/sp_menu_notes.md), the multiplayer animations
  loaded into story levels (RE under way, .local/anim_load_notes.md).


### 2026-11-07 - Story co-op: a walk set from the guards' animations; death check fixed

- NF_COOP_ANIMS=1 logs the animations NPC bodies play with their speed. The Exchange: walks 0x60002A2 / 0x600029E
  (~3.5 units/s), runs 0x6000262 / 0x6000288 (~10), stands 0x6000259 / 0x600025B; 0x600025C is a guard pointing an
  arm (tried as an armed idle: wrong). 0xA91E0 / 0xA9350 replaced for the co-op players by a set of those (each the
  first the level loads, 0x6B500); 0x18BB0 given max(|forward|, |side|) so strafing walks. The bodies now walk and
  run, but unarmed: the arms hang (the user: use the multiplayer poses). Those are only in the multiplayer levels'
  files (coop_notes2.md): loading them into a story level needs the level format decoded.
- Player 2's death: its state is the word +0xD0 (+0xD2 the move): Dead() read a dword and never matched, so no
  respawn ran. Fixed (not yet seen respawning). NF_COOP_KILL2=<s> sets player 2's health to 0, a status line every
  600 frames (states, distance apart, zone).
- Friendly fire: player 2's shots at player 1 logged no hit (0x3D900 not reached, or the shots missed: the
  crosshair was over player 1's head). Not settled.
- 0x12220 (the upper body aimed, as a match's spawn) tried: that run dropped to 7-8 fps (a vision gadget was also on).
  Removed until seen alone.


### 2026-11-07 - Story co-op: HUD per view, guns per view, cutscenes, player 2's death and leash (src/game/coop.cpp)

- From .local/coop_notes.md: the radar ring (story HUD element 1, laid out for one full screen) scaled into each
  player's view after 0xB6190 (other full screen elements left alone: scaling them brought hidden overlays into view);
  each first person gun's camera mask ([actor+0x778]+0xD4) 1 << p; cutscenes freeze and release player 2 with player
  1 (0xABD70, 0xABBD0), the cutscene camera 4 given the single screen aspect (0x25190 hook); player 2's death keeps
  the story's failure flags, skips its dead frames (0xAC690) and respawns it beside player 1 after 3 s (0x9F0B0's
  steps, 0xAC7E0); player 2 moved beside player 1 after 3 s over 20 units away or outside the zones.
- The split views' aspect is the game's own (0x25190 doubles it while two players' cameras exist): the extra
  doubling from earlier today removed.
- Body animation: setting the multiplayer flag around 0xA91E0/0xA9350/0xA94C0/0xA9590 broke the picture (a screen
  filled with a sprite); .local/coop_notes2.md: the walk tables' animations exist only in the multiplayer levels'
  files. Left out (NF_COOP_BODY=1 to try). Next: tables from animations a story level does load (NPC walks).
- NF_COOP_OFF bits leave parts out for tests (1 HUD, 2 guns, 4 body, 8 cutscenes/death, 16 leash).
- Checked (.local/vk/coop2_test.sh): The Exchange, the radar in both views, one gun each, the world drawn.


### 2026-11-07 - Story co-op: friendly fire toggle

- nf::coop_friendly_fire (settings: coop_friendly_fire; More Settings > Story Co-op: Off / Split Screen / Split
  Screen + Friendly Fire). Off: in 0x3D900 (every shot's damage) a hit whose shooter is one co-op player's and whose
  victim is the other's does nothing. On: the game's own (whether it hurts an ally player is not yet seen). Not yet
  tested with a shot between the players.


### 2026-11-07 - Story co-op: a setting, the views' shape, one-camera effects off when split

- nf::coop (settings: coop; More Settings > Story Co-op: Off / Split Screen): story co-op as NF_COOP_TEST=3, from the
  next mission. nf::CoopSplit() the split now (0 none, 1 one above the other, 2 side by side): set when the cameras
  are split, cleared at each level's start and back at the front end.
- The views' shape: 0x25190 halves or doubles a split view's aspect only with the multiplayer flag; view.cpp's
  Widened now doubles (one above the other) or halves it for cameras 0 and 1 while the projection and culling planes
  are built. Checked: The Exchange's two views no longer squashed.
- gpu::SetViewCount: with two views the passes that take one full screen camera are left out (ray tracing, snow,
  motion blur, contact shadows, depth of field, GTAO, effect lights, screen-space reflections, smooth turning).
- Next (RE under way, .local/coop_notes.md): the radar in each view (the ammo is drawn in both), the other player's
  gun drawn in the lower view, cutscenes full screen, player 2's death, triggers, the mission's end.


### 2026-11-07 - Merged to main; v1.2.0

- On the user's word (no Linux smoke test on this PC; The Exchange and Paris checked on Windows): the SDK's main
  fast-forwarded to vulkan c777e00, the fork's main to vulkan-renderer. Release notes docs/releases/v1.2.0.md,
  tag v1.2.0 on the fork. The tag push started no run (the fork's first): the Launcher workflow started by hand on
  the tag (run 42348255039). The fork is private: the release and its launcher only its owner's until it is public.
- That run failed: the job's token cannot clone the private SDK (papand7/XboxXBERecomp) as the submodule. The
  launcher built here instead (.local/vk/build_launcher.bat, up to date: no launcher source changed), released with
  gh: https://github.com/papand7/nightfire-recomp/releases/tag/v1.2.0 (NightFireLauncher.exe, sha256 1091584b...).
  CI releases need the SDK public or a token secret with access to it.


### 2026-11-07 - Motion blur stronger

- Motion blur: 40% of the frame's motion (was 25%), capped at 1.4% of the height (was 0.8%), 10 samples. The Amount
  slider (0 to 2) still scales both. Checked (.local/vk/mb_test.sh): clear blur on a turn, sharp still, gun sharp.


### 2026-11-07 - Enhanced's exposure and contrast keep white; steady glows light nothing

- The style pass's grade multiplied by the exposure before its curve and stretched contrast through 0.5: with
  Enhanced's exposure 1.35, contrast 1.18 and filmic (the user's graphics.toml) everything over about 0.75 went
  white, The Exchange's snow flat. Now the curve's output is over its white's (filmic, Reinhard; without a curve
  c e / (1 + (e - 1) c)) and contrast is an S about the middle with black and white kept: the middle brightens, snow
  keeps its detail. Checked with the user's settings (.local/vk/user_test.sh).
- Steady glows (lamp halos) cast no effect light at all: their light came and went as they entered and left the
  screen (the room darkening on a turn); the level's own lighting has those lamps. Flashes and explosions keep theirs.


### 2026-11-07 - Contact shadows, floor reflections, smooth turning with HDR

- Contact shadows (GraphicsStyle::contact_shadows; Effects > Contact Shadows; not under ray traced lighting): from
  each world pixel a 12-step, 0.35 m march toward the world's up in the view (ViewUp, from rt_cams_) through the kept
  depth; something within a thickness above shades it 40%, less the farther. XBE_CONTACT_TEST=1 the shadows alone:
  under branches, the truck, ledges; flat ground clear.
- Floor reflections (GraphicsStyle::floor_reflections 1 ice, 2 also polished floors; Effects > Floor Reflections):
  gpu::TextureIce (opaque, bright, bluish, smooth) and ground detail's concrete reflect 0.5 / 0.2, written to the
  mask negative; the reflection passes (SSR, ray traced) keep them only where the surface faces up (the view's up in
  a free constant). Checked in 0x7000027's garage: the far concrete takes a faint reflection of the shutter.
- Smooth turning with an HDR chain: its own copy (Target::warp_copy, the gun in its alpha) instead of hdr_scene; HDR's
  copy turned alongside (kWarpHlsl warphdr), HdrCompose / HdrOut with both in place. In-between frames seen running;
  their brightness against the real frames not yet checked (another program held the GPU at 99%).
- The shared settings page has 14 rows (Effects needed 14); checked on screen.
- .local/vk scripts now stop only this repo's copies of the game (they stopped any NightFire).


### 2026-11-07 - Volumetric light's steps by the air's brightness

- The volumetric march takes 12 steps by day, fewer as the air dims (round(12 x sqrt(strength)), 6 to 12; The
  Exchange's night about 7): a dim air's noise is as dim. 2.2 to 1.0 ms there; every effect on, the GPU frame 8.1 ms
  (quiet GPU, .local/vk/perf_spot.sh). No noise seen in the shot.


### 2026-11-07 - Smooth turning: an in-between frame turned by the mouse (Vulkan, 100 Hz+ screens)

- GraphicsStyle::smooth_turn (Effects > Smooth Turning). After each present app::Pace asks the backend for a frame
  in between (Backend::MidFrame); the render thread (render_thread.cpp, Op::Mid) runs it after that present and
  waits there until half the presents' eased interval. It takes the turn the mouse has made since the title last read
  it (GameInfo::view_turn: nf::ViewTurn from input::PeekMouse, the same radians a count and zoom as the mouse's
  own turn), turns the frame's scene copy by it (kWarpHlsl: pitch, then yaw; the edge kept past the frame), the HUD
  (where the presented frame is not its scene) and the gun (marked in the copy's alpha from the kept depth) where
  they were, through the same blit and overlay as the title's frames. Screens under 100 Hz and HDR chains: none.
- Checked on this PC (144 Hz): 60 in-between frames a second in The Exchange beside the title's 60;
  XBE_WARP_TEST=5 (every one turned 5 degrees, one written to warp_test.png): the world shifted, HUD and gun still.
  Not yet felt with a real mouse.


### 2026-11-06 - Draw distance; no blob shadows under ray traced lighting (src/game/distance.cpp)

- Static RE (.local/lod_notes.md): on foot no model LOD and no distance cull; the view ends at the level's fog
  (0xDDB00: per-level start and end as immediates, through the setters 0xE3E60 / 0xE3F00) and the camera's room's
  portals cut at 200 units (0xDB5C0's fifth plane, d at 0x29EAAC). The D3D far plane is 3000.
- Setting draw_distance (percent, 100 to 300; NF_DRAW): the fog's start and end scaled (not the sky pass's own fog,
  not a level whose fog ends under 60: its fog is the scene), the far plane moved out by as much, the character
  shadow's ranges (dist² at 0x161DB8 / 0x161DB4) too. Checked (.local/vk/draw_test.sh): The Exchange at 200%, the
  far mountains and castle clearer, nothing missing at the edge in that view. Other outdoor levels not yet looked at.
- The game's blob shadow under each character (0xE04E0, cdecl, 5 arguments) is skipped while ray traced lighting
  is on: the characters are in its scene and cast their own shadows. Not yet seen side by side.


### 2026-11-06 - HDR10 from the float scene; bloom kept near its source

- With HDR output and tone mapping on, ToneResolve also works out each pixel's HDR brightness over its SDR's (the
  exposed float scene: as SDR's up to 0.6, then a soft shoulder toward the screen's peak), drawn into the scene's
  copy's alpha (log2 / 4); HdrOut multiplies the presented pixel by it, so the later passes' grading, depth of field
  and motion blur stay, and the HUD stays at SDR white. Without tone mapping the old knee expansion is used.
  Checked on this PC's HDR screen (peak 418 nits, .local/vk/hdr10_test.sh): The Exchange 22% of the pixels above
  SDR white (the snow a little, lamps and lit snow up to 377 nits); before, 81 nits at most.
- HDR bloom's wider levels count less (0.55 each): the glow of the snow no longer haloes the gun before it.


### 2026-11-06 - Snow sparkle; snow told at night too

- Snow sparkle (GraphicsStyle::snow_sparkle, on; the weather's pass): a glint maybe in each 1.5 cm cell across the
  ground, fixed in the world, each a facet turned its own way that shows only near its mirror direction (the view
  and the light from above), so they twinkle as you move; on snowy ground facing up, within 25 m, outdoors, about
  two pixels, past white (the HDR bloom takes them).
- What counts as snow (footprints too): bright and no warmer or greener than blue. The old "unsaturated" test
  missed The Exchange's blue moonlit snow almost everywhere.
- The snow pass no longer clamps the float scene at white.
- Checked (.local/vk/sparkle_test.sh): scattered glints on the snow near the feet, none off.


### 2026-11-06 - HDR bloom (Vulkan, with tone mapping)

- With HDR tone mapping on, Bloom (the setting or the Enhanced style's) comes from the float scene: its exposed
  light past a soft knee about white, down six levels (13 taps) and up with a tent, mixed in before AgX / ACES. The
  style pass's 8 bit bloom is then left out. Lamps and what the effects light past white glow softly; plain snow
  barely (the knee). About 1 ms under another program's GPU load (.local/vk/bloom_test.sh).


### 2026-11-06 - Motion blur (Vulkan, optional)

- MotionBlurPass (kMotionHlsl), the scene pass's last: the level's camera (rt_cams_, as the snow takes it; now also
  tracked without ray tracing when motion blur is on) and the last frame's; a pixel's world place reprojected,
  0.25 of the way blurred, capped at 0.8% of the height, 8 samples; the gun (nearer than the kept depth) sharp and
  not sampled; no blur on a cut. GraphicsStyle::motion_blur / motion_blur_strength; Effects > Motion Blur Off/On.
- Checked (.local/vk/mb_test.sh): blur along a turn, sharp when still, gun and HUD sharp.


### 2026-11-06 - Effect lights: steady glows no longer flood the scene

- The Exchange's lamp halos and searchlight beams (added glows drawn every frame) lit the castle and snow white as
  effect lights, on top of the ray traced lamps. Each glow (by texture and shader) now has a steadiness: up in a
  second while it is drawn frame after frame, down in about two seconds unseen. A steady one lights 30%, none with
  ray traced lighting (its lamp is lit there already); flashes and explosions keep all of theirs (drawn through a
  constant blend factor, the weight).
- Checked (.local/vk/lamp_test.sh): every effect on now looks as with effect lights off, a night with lit lamps.


### 2026-11-06 - Faster effects, the same picture (Vulkan)

- The world's ray tracing bottom level is built PREFER_FAST_TRACE (was fast build): the main relight about 3.0 to
  1.5 ms, the frame about 1 ms less overall in a same-run A/B.
- Rays skipped where their light could not show: the effect lights' shadow ray where (c + 0.03) x the light is under
  0.001 (a dark pixel), the volumetric pass's lamp and sun rays where a step adds under 4e-5; half the untraced
  light kept, so the error stays under a quarter of an 8 bit step. Volumetric about 3.0 to 2.2 ms.
- The effect lights' list: the glow's 16 x 9 tiles were summed by one thread each (thousands of pixels) and twice;
  now 8 x 8 parts a tile first, then the tiles, and a rank pass before the pick (the same sums and order).
  1.45 to 0.13 ms.
- XBE_PERF's "scene passes" line has the relight's parts (rt-build, rt-gi, rt-lit, rt-vol) and the lights' (lt-).
  Measuring is noisy while another program uses the GPU (SSX Tricky was running): compare within one run.


### 2026-11-06 - Foot IK: the characters' feet on the ground (src/game/foot_ik.cpp)

- Static RE (.local/ik_notes.md): the pose solver 0xDD290 (obj, skel = [obj+0xB8]+0x58, char skin); world bone
  matrices [skel+0x40], 0x3C bytes (rows at +0/+0x10/+0x20, place +0x30); flag 4 at skel+0x50; the 73-bone human
  skeleton's legs 49-52 and 53-56 from the pelvis 0; the ground ray 0x2C4E0 (9 args, mask 0x0C, flags 8; hit +0x38,
  normal +0x10), freed by 0x294D0.
- The hook makes the world pose first (the original through CallGuest, guarded), rays under the middle and each
  ankle, lowers the pelvis for the lower foot, two-bone IK per leg (the knee in its own bend plane), the foot tilted
  60% with the slope, eased; then the original makes the skin from it. Not in the air or dead. Setting foot_ik (on),
  NF_IK=0/1, NF_IK_TRACE=1.
- Checked (.local/vk/ik_test.sh): 5 characters in The Exchange, flat ground bends about 0.01, walking up the slope
  0.04-0.08 with the pelvis lowered; the turned knee and ankle land within 0.0001 of the solve; no crash. Not yet seen
  close on a guard on a slope (the guards were far).


### 2026-11-06 - Full settings at night: the moon, not a day's sun

- Ray traced lighting took The Exchange (night, snow) for a day: snow makes the outdoor baked light as bright as a
  day's, so a full sun washed it out white. A night is now also told by the level's fog (dark: 0.21 0.28 0.40 there):
  the sun 65% weaker and bluer, the volumetric light 70% less. The log's "level's sun" line gives the fog too.
- Tone mapping: AgX's punchy look (power 1.35, colour 1.4); auto exposure brightens at most 1.15x (was 1.3x).
- Checked (.local/vk/full_test.sh, rtparts_test.sh): every effect on reads as a moonlit night with contrast; ACES
  + auto looks best. What is left white is the game's own snow (as without ray tracing).


### 2026-11-06 - The sky box no longer lit as a wall; per-pass GPU times

- The sky's draws (SkyDraw) no longer write depth while the scene pass is wanted (not after the mid-frame clear):
  the passes saw the sky box as walls a few hundred metres off, lit, fogged and metered by their slopes, so its edges
  showed toward the sun and full effects washed The Exchange out white. Checked (.local/vk/sun_test.sh): looking
  at the moon with every effect on, no edges and no wash-out.
- XBE_PERF logs each scene pass's GPU time ("perf: vulkan scene passes"). The Exchange, every effect on: relight
  ~4 ms, effect lights ~2.2, snow ~1.4, the rest under 0.4 each (.local/vk/perf_test.sh). Next: optimize those three.


### 2026-11-06 - Footprints from NPCs, bots and LAN players

- Every NPC and bot runs its think through 0x31530 (lan_play.cpp's hook): it notes the object (nf::NoteWalker, up to
  48 a frame), and SetWalkers (lights.cpp) adds their places (+0x24) after the local players'. A LAN match's other
  players are bots driven from the network, so they leave prints too. The renderer takes 64 walkers, 2400 prints.
- Checked in The Exchange (.local/vk/walk_test.sh): 14 walkers (the player and 13 guards); 37 prints made while the
  player stood still. Not checked in a multiplayer match (no snowy MP map tried).


### 2026-11-06 - HDR tone mapping: a float scene, AgX / ACES, auto exposure (Vulkan)

- The scene passes from the ray traced relighting to the weather draw into an RGBA16F copy of the scene (ToneBegin
  blits it in and swaps it for the target's own; their pipelines take `scene_fmt_`). Their outputs are no longer
  saturated, so the light past white is kept (a UNORM target clamps as before).
- ToneResolve (kToneHlsl): the scene metered on a 64 x 36 grid, the exposure eased in a 1x1 image (this frame's and the
  last in turn), then AgX (minimal fit, a touch more contrast and colour) or ACES (Narkowicz, mid grey kept) back
  into the 8 bit target before the style pass. The style pass's own tone map and exposure are left out meanwhile.
- `GraphicsStyle::hdr_tonemap` / `auto_exposure`; Effects > Tone Mapping: Off, AgX, ACES, each + Auto Exposure.
- Checked in The Exchange (.local/vk/tone_test.sh): with ray traced lighting, Off clips the sunlit snow flat white,
  AgX and ACES keep its detail. Without ray tracing the night stays dark (auto exposure between 0.7x and 1.3x).
- Next: HDR10 output could take the float scene before the curve.

### 2026-11-06 - Vulkan only: the Renderer choice gone

- `GameInfo::vulkan_only` (SDK): the settings hold the renderer at Vulkan and the settings window hides its Renderer
  choice. Direct3D 12 still starts in its place on a PC where Vulkan cannot.
- NightFire sets it; the Graphics page's Renderer row is gone (Texture Packs, HDR and Restore Defaults one row up).
- Checked: a settings file with `renderer = 'd3d12'` starts on Vulkan.


### 2026-11-06 - Sprint slower, weapon sway's steps capped
- Sprint 1.3x the run (was 1.6x: too fast). The sway's steps followed the distance walked, so a sprint (7.8 units a
  second measured at 1.3x; about 9.6 at 1.6x) bobbed 3-4 times a second, the gun's dip twice that; now at most 1.25
  bobs a second, the stride lengthening past it (src/game/sway.cpp kMostSteps). Measured with NF_SWAY_TRACE and
  NF_SPRINT_TEST=2 in The Exchange.

### 2026-11-06 - Snow and footprints, GTAO, Relief High, volumetric light, ground geometry (Vulkan)
- Snow (Effects > Lighting "+ Snow"; `weather`, `snow_amount`, `footprints`): kSnowHlsl over the scene after the fog:
  the view ray walked through three layers of cells with a flake in some, moved by the fall and a gusting wind (world
  anchored), a traced ray up hiding flakes under roofs, a light haze. Where: the game's own snow (4096 point sprites,
  a vertex program) marks it and is left out. Footprints from the players' places ([0x1F6654 + 4p] +0x24; objects at
  0x8xxxxxxx), every 0.7 m, a boot sole with tread pressed in, filled in after a minute or two.
- GTAO (Effects > Ambient Occlusion Low/High, in place of the style pass's 8 taps, which read the end-of-frame depth:
  the gun alone): half size, 2 or 4 slices, the world's kept depth; normals over 24 pixels (the depth comes in
  terraces near the camera: bands otherwise), the start lifted a step.
- Relief High (Materials "... + Relief High", `relief_high`): twice the steps and the surface's own shadows.
- Volumetric light (Ray Tracing "Full + Volumetric", "Full + GI + Volumetric"; `rt_volumetric`): kRtRelightHlsl's vol
  at half size, 12 steps a pixel, the sun traced and the game's lights shadowed; dimmed by the level's outdoor
  brightness (a night washed out like fog otherwise).
- Ground geometry (Materials "Detail + Geometry", "Everything"; `displacement`, a prototype; gpu/displace.cpp): ground
  draws (ground detail's grass, snow, rock, dirt; facing up; spanning 3+ units) cut in 16 and pushed in up to 8 cm,
  edges kept. The gloved hands passed for rock and came apart until the size test.
- Test switch NF_LEVEL=<hex> (coop_test.cpp): the mission the menus start replaced (07000005 The Exchange);
  `.local/vk/mission_test.sh`, `snow_test.sh`, `exchange_test.sh` drive it.
- Evidence (The Exchange, captures private in .local/rt_run/shots): snow with the game's left out, heavy, footprints
  (sn11_prints); GTAO alone without bands (ex11); relief flat/medium/high (ex12); volumetric at night (ex12_vol).
- Measured: snow about 3-4 ms of GPU; ray tracing itself costs about 25 ms a frame in The Exchange on this PC (60 fps
  in Fort Knox): big outdoor levels need ray tracing's cost looked at.
- Not done: ground geometry shows little (the baked light does not shade new bumps); prints from bots and remote
  players; a print's heading is the way walked (backwards shows toes behind).

### 2026-11-06 - Ray traced global illumination (Vulkan, Full + GI)
- What: `rt_gi` / `rt_gi_strength` (graphics.toml, Graphics Style window), the Effects page's Ray Tracing row gains
  "Full + GI". kRtRelightHlsl's new `gi` entry traces, at half size, 4 cosine weighted rays from each pixel's surface;
  a hit gives its texture times the light on it (0.35 of its baked light, the sun with a traced shadow, the game's
  lights unshadowed). The relight adds it (4x4 traced pixels weighed by distance; fixed 4x4 ray patterns, so no frame
  to frame jitter) and keeps 0.25 of the baked light in place of 0.35. The screen-space Bounce is skipped while it draws.
- Evidence: `.local/vk/gi_test.sh` (Quick Game, on/off pairs; RT_TEST=7 the bounced light alone): walls take on the
  lamps' and neighbouring walls' light and colour, smooth, no visible noise; log "ray traced bounced light drawn (4 rays
  at 960x540)". Still 60 fps (vsync) on this PC.
- Not done: GPU cost not measured separately; one bounce only; sky light for rays that miss.

### 2026-11-06 - HDR output (Vulkan)
- Settings::hdr (Graphics page's HDR row, the settings window, settings.toml hdr / hdr_peak_nits /
  hdr_paper_white_nits): an HDR10 swap chain (A2B10G10R10, ST.2084; VK_EXT_swapchain_colorspace) where the screen
  offers one, else SDR as before with a log line. The screen's peak from DXGI (IDXGIOutput6::GetDesc1) and its SDR
  white from Windows (DisplayConfig's SDR white level), read at each new chain; settings override them.
- The game renders 8-bit throughout, so this is SDR expanded: the frame (FXAA, sharpening, gamma ramp) and the ImGui
  overlay are drawn at the screen's size first, then HdrOut takes gamma 2.2 to SDR white and raises the 3D scene's
  highlights from a knee (0.45 linear) toward the peak by luminance. A pixel is the scene's where the presented frame
  still holds what the scene pass left (a copy at its end, Target::hdr_scene): HUD, menus and movies stay at SDR white.
  HDR on runs the scene pass even with no effects on, for that copy.
- Evidence: this PC's screen (418 nits peak, SDR white 80): HDR10 chain made, shots decoded from PQ match the SDR
  look; a lamp reached 379 nits; XBE_HDR_TEST=1 shows the scene mask (the whole 3D view, the HUD and overlay left out).
  SDR (hdr off) unchanged. The Graphics page's new row shows and saves.
- Not done: Direct3D 12/11 (the row says Needs Vulkan); scRGB; HDR metadata.

### 2026-11-06 - Story co-op: two players, split screen (NF_COOP_TEST=3)
- A story level makes its player in 0xAE870 from a start record (0x2774D0 + i*0xCC: place, +0xC facing, +0x1C
  loadout) through 0xACAB0 (cdecl: player, place*, facing*, loadout*; the object in [0x1F6654 + 4p], the actor in
  [0x2774B8 + 4p], it reads the player's camera [0x1F661C + 4p]). Cameras are made in 0x25D10 (one a human only with
  the multiplayer flag) and laid out by 0x25B80 (camera, x, y, w, h as floats of 640 x 480).
- `src/game/coop_test.cpp` =3: the humans held at two from the mission's choice; after 0x25D10 and after 0xAE870 the
  second camera made (0x25BF0) and the screen split (one above the other; side by side with [0x1F660C] 1); after
  0xAE870 the second player made 1.2 units beside the first, with the first start record's loadout less its
  condition (+0x2C, 0x1D: 0xACAB0 returns 0 while flag 0x1DF138[0x1D] is clear).
- Evidence: The Exchange, two views one above the other, each player sees the other, controller 2 walks player 2
  (`.local/vk/coop_test.sh`, 2:LS), no traps over a minute. The multiplayer flag stays clear.
- Next: the HUD (the radar only in the lower view), the views' aspect (each half drawn with the full screen's), the
  Vulkan passes (one camera), then the story's own: objectives and triggers from player 1, deaths, cutscenes, the
  mission's end.

### 2026-11-06 - The sky box with the HD pack and Materials
- The Exchange's moon box showed its cube: faces bumped (Materials' normal and relief maps on the sky's pack pictures),
  dark bands and thin seams at each edge (the pack's pictures upscaled each apart, their edges padded from their
  opposite edges, so neighbouring faces no longer meet). One searchlight beam drew as a black bar the same way.
- Fix (SDK): `SkyDraw` (the ray traced lighting's sky test, now shared and worked out once a draw: fixed function,
  camera at the world's middle, unfogged in a fogged level, or spanning hundreds of units about the eye); a sky draw
  keeps the game's own picture and gets no materials or ground detail.
- Evidence: The Exchange with the pack and PBR + relief, looking up and turning: the moon box smooth, no edges or
  seams (a 2x zoom of an edge clean); the start view's black bar gone.

### 2026-11-06 - README: the launcher; the launcher builds this fork
- README: the Electron launcher (screenshot docs/images/launcher.png, its Mission, Settings, HD Textures, Your music
  and Log pages, how to build it), a work in progress list (story co-op), the original project's features marked as
  theirs, the original README linked rather than copied, an animation of Original / Enhanced + ray tracing / Toon /
  Noir on The Exchange.
- launcher/game.json: this fork's repository (papand7/nightfire-recomp) and toolchain URL; while it is private the
  launcher cannot fetch it. `launcher/electron/build_service.bat` builds the service (npm run service no longer
  points at the ignored .local).

### 2026-11-06 - Story co-op: first test (NF_COOP_TEST)
- `src/game/coop_test.cpp`, diagnostic only. A mission starts through 0xBDDF0 with one human [0x260044], one
  character [0x26002C] and the multiplayer flag [0x260018] clear; the pad reader takes its count from [0x260044].
- =1 (both counts two from the mission's choice): The Exchange loads and plays, still one player, full screen; a
  second controller does nothing. The story level makes its player without those counts.
- =2 (the multiplayer flag too): the camera code would split (0x25E20 makes one camera a human only with the flag),
  but the level then runs the multiplayer rules each frame (0xA2B80, scenario state [0x262920]) and crashes 8 s into
  the load (null read under 0x9CC50: no multiplayer setup).
- Next: find the player spawner the multiplayer start calls per human (the 0xA27C5 loop builds the characters,
  0x1B170 each) and call it for a second human after a story level loaded; then the camera split without the
  multiplayer flag (0x25E20's count).

### 2026-11-06 - Noir: the player's hands no longer turn red
- Skin under a red or warm lamp passed Noir's red test (red at least 2.2 times green and blue), so the hands went
  red by the CCCP banners. Noir now asks the renderer to keep the world's depth at the game's mid-frame clear
  (KeptDepthWanted); when it was kept, the scene's depth holds the gun and hands alone, and those pixels are never red
  (scene pass p[10].x, `noir(c, hands)`).
- Evidence: a temporary mods/shaders/scene_post.hlsl painting the detected pixels green in a Quick Game: exactly the
  shotgun and both hands green, the level's red crates still red.

### 2026-11-06 - Key Bindings page in the game's menus
- Settings > More Settings > Key Bindings (was a row opening the F1 window): two sets of rows on the shared page,
  every on-foot action with its keys; A waits for a key or mouse button and binds it (the old key kept as the
  second), Escape clears, Default Keys restores. SDK: app::BeginKeyCapture / TakeCapturedKey / KeyCaptureBusy (the
  window procedure takes the next new press; the keyboard and mouse are kept from the controllers and the menu
  pointer until it is let go). Sprint is now a bindable control (the left stick click, unused on foot: Shift).
- Evidence: `.local/vk/keys_menu.sh` (rt_run copy): both sets shown, F7 sent to the window bound Jump ("F7 / Space",
  saved), Default Keys restored it; sprint test still 1.6x.

### 2026-11-06 - Sprint on Shift
- New (the original has no sprint): Shift held on the keyboard while moving forward, standing, walks 1.6 times as fast.
  0xA9CF0 (cdecl: the actor, the object, a speed) makes a character's walk from its controls (field 2 forward, 0 and
  1 sideways via the getter 0x6CA20), caps the stick at full and eases it, then multiplies by the speed argument; the
  hook in `src/game/mouse.cpp` raises that argument (nf::SprintFactor, `src/game/controls.cpp`). Raising the forward
  input itself did nothing (capped).
- Evidence: `NF_SPRINT_TEST=1|2 .local/vk/sprint_test.sh` (player 1 walks / sprints, the left stick up): 6.0 units a
  second walking, 9.6 sprinting.

### 2026-11-06 - Hold to crouch on the keyboard
- C / Ctrl crouch while held instead of toggling (`src/game/controls.cpp`). The game toggles on A (style 0's controls
  +0x34); a character's posture is its object's word +0xD2 (0xA8A60, the first person eye: 4 crouched, 5-9 and 13-15
  standing, 10-12 and 16 keep the eye). While the keyboard plays, A reaches the game as a one-frame press whenever the
  key held and the posture differ, again after 12 frames if the posture did not change (no room to stand). Pads keep
  the game's toggle.
- Evidence: `NF_CROUCH_TEST=1 .local/vk/crouch_test.sh` (the pad's A treated as the key): A held 2 s and 2.5 s,
  posture 4 the whole time, 0 the frame after the release.

### 2026-11-06 - A random sun for each level
- Full Lighting's sun is no longer fixed: `src/game/lights.cpp` picks a new one (any side, 26-69 degrees high) when a
  level starts, i.e. after a stop of over half a second in the light updates, if the level's placed lights (every
  lifetime-0 record, on or off, so strobes don't count) differ from before; a level with none rerolls after a stop of
  over 3 s. SDK: `gpu::SetSunDirection` / `SunDirection`; the renderer makes a low sun warmer.
- Evidence: two Quick Games, log `nightfire: the sun ...` once at the level's start (252/65 and 80/38), unchanged for
  the rest of the match; 60 fps.
- Next: an outdoor map's look with low suns.

### 2026-11-06 - Ray traced level lighting, Noir style, blood
- Full Lighting (Effects > Ray Tracing; graphics.toml `rt_lighting`, `rt_sun`): the level's baked light (its vertex
  colours: world draws are texture x diffuse x 2, no light maps) divided out per pixel and new light put in. Indoors
  the game's own lights (src/game/lights.cpp reads the list at [0x21570C] after 0x6D660, 0x40 byte records: place,
  radius, intensity, RGB, kind) with traced shadows; outdoors (a ray up meets nothing) the baked light kept, darker in
  shade and brighter in a traced sun, so night levels stay night. The sky is left out of the capture: fixed-function
  draws, draws spanning 300+ units before and behind the eye, fog-off draws in a fogged level (measured on the snow
  canyon: dome and mountain ring 400-600 units, fog off).
- Noir style (mode `noir`): Toon's bands, outlines and halftone in black and white, red kept (blood, lamps); its
  options are Toon's page. Blood (Effects > Blood, `blood`, any style or renderer, ImGui overlay app/blood.cpp):
  a spray at the shooter's crosshair on a hit (0x3D900), red edges and splashes on the hit player's view.
- Checked (.local/vk/rl_test.sh, noir_test.sh, Quick Game): Fort Knox lamp pools and shadows at 60 fps, castle and
  snow canyon outdoors lit without blowing out, Noir and blood shots. Not yet: the sun's direction is fixed.

### 2026-11-06 - Effect lights and bounced light read the world's depth
- The game clears the scene's depth before the gun, so the end's depth holds the gun alone: the effect lights and
  bounced light saw the world as empty (nothing lit, nothing gathered) and only the gun took part.
- sdk/runtime/gpu/vulkan.cpp: both passes read the nearer of the end's depth and the one kept at that clear
  (RtKeepDepth), as the occlusion, reflections and depth of field do; the depth is now kept when either is on
  (KeptDepthWanted). Traced light shadows from the world leave the gun out (mask 1), the gun's meet everything.
- Fort Knox: XBE_GI_TEST=2 (the gathered light) now covers the walls, not only the gun; XBE_LIGHT_TEST=1 shows a
  glowing window lighting the floor and wall about it. No errors, 60 fps.
- Next: the lights' and bounce's strengths were tuned when they lit almost nothing on foot; a check by eye on a lit
  corridor (Fort Knox's looked strong).

### 2026-11-06 - Ray tracing: cut-outs, the gun and hands, reflections
- Asked: ray tracing left out reflections, the gun and hands, see-through and cut-out surfaces, and the lighting.
- sdk/runtime/gpu/vulkan.cpp: the capture writes a vertex's texture coordinates and diffuse colour after its place (32
  bytes); each triangle gets a material word (RtMaterial: its texture's slot in a per-frame table of 1024 in a
  descriptor set of its own, set 1, the alpha test); the shared HLSL (kRtCommonHlsl) has RT_PROCEED, which alpha
  tests a cut-out's candidate hit. shaderSampledImageArrayNonUniformIndexing is now needed for ray tracing.
- Cut-outs: a second, non-opaque geometry (Fort Knox: 244 triangles a frame). The gun and hands: a bottom level and
  instance of their own (custom index 1, mask 2); the occlusion traces their pixels against everything (the world's
  rays use mask 1). Test view 3 (camera ray against depth) is green on the gun: its captured triangles match its
  depth. In the occlusion view the slide, trigger guard and the fingers around the grip darken.
- Reflections: RtReflectPass (kRtReflectHlsl) replaces the screen-space ones when ray tracing is on; the hit's
  colour is the scene's where it shows on screen, else its texture by its diffuse colour at the scene's brightness
  there. It reads the depth kept at the gun's clear (the end's holds only the gun: every hit read as hidden at
  first). XBE_RT_TEST=4 shows the hits (with XBE_SSR_TEST=1: mostly green, blue off screen, few misses).
- Blended draws stay out (glass and smoke do not block light). The game's own lighting is baked (light maps, vertex
  colours) and is not replaced.
- Next: a check by eye on a level with listed reflective surfaces; the effect lights and bounced light still read
  the end's depth (the gun alone), as noted before.

### 2026-11-05 - Aim down sights: each gun's sights measured again, on the middle
- Reported: most guns' sights were off the middle when aimed (shots go through the middle, so they missed). Measured
  at 1920x1080: pistols 2-5 had their front sight 30-40 px left and 30 px low, the silenced 6-9 50 px left, 13 45 px
  low, 19 95 px left and 70 up; models that shared one entry sat up to 100 px apart.
- src/game/aim.cpp SightOf: one entry per weapon, corrected in three rounds. NF_ADS_CAL=1 (with NF_ADS_WEAPONS and
  NF_ADS_SHOTS) holds each weapon 400 frames and takes picture a as it is, b with the gun 2 cm left, c 2 cm up (the
  view's scale at that gun's sight: about 45 px per 2 cm for a pistol, 25-30 for the rifles' far front sights);
  NF_ADS_ONLY=,2,3, limits the weapons. The weapons test logs the gun's turn and the aim offset (0 here: 0xB7EB0 turns
  the gun only by the actor's +0xE0/+0xE4).
- Seen aimed: 2-23 within about 10 px of the middle (the idle animation moves them that much between runs), the shotgun
  28/29 on its receiver's top. Not the cause: looking up or down, turning, strafing, walking, firing (the same place in one run).
- `.local/ads_run.sh` runs its own copy in `.local/ads_run` (others' games left running).
- Shots go along the view: NF_SHOT_TRACE=1 logs player 1's rounds at the spawner 0x211F0 (src/game/lan_play.cpp);
  aimed, their direction's yaw is the view's (80.52 deg both) and their pitch is the actor's +0x838 times 90 deg
  (-0.864 -> -77.8 deg, sin = 0.977 = the direction's y). The test fires with `RT 30` (`RT 3` is too short to fire).

### 2026-11-05 - Depth of field (Vulkan)
- New Effects row Depth of Field: Off / Distance / Focus (graphics.toml `depth_of_field`, `dof_strength`;
  docs/graphics_styles.md). VulkanBackend::DofPass (kDofHlsl) after SMAA: focus eased in a 1x1 image from the middle
  of the view, a 32-tap gather; the depth kept at the mid-frame clear (RtKeepDepth, now also when depth of field is on)
  keeps the gun and hands sharp. Aiming down the sights (controls.cpp -> gpu::SetAiming) blurs more.
- Checked on screen (.local/vk/dof_test.sh, Quick Game): Focus keeps the rock under the crosshair sharp and blurs the
  mountains; aiming through a doorway (NF_ADS_TEST=1) the room beyond sharp, the frame and walls blurred; gun and HUD
  sharp in every shot; no frame rate change against off (60 and 34 fps both ways, the 34 from other load).
- Effects row checked on screen from the main menu (.local/vk/dof_menu.sh: fits, A keeps it) and from the pause
  menu (.local/vk/dof_pause.sh + rt_step.sh): Focus kept there applies at once in play.
- Found on the way, fixed: from the pause menu a page opened with the focus the page before had (Graphics on Restore
  Defaults: three DOWNs and A there restored defaults). nf::MenuFocus (pc_settings_page.cpp) now gives each of our
  pages its first row on 0x4C, with the menu context's message 0x22 (what 0x92C40's back uses); back still returns to
  the row it kept. Checked from the pause screen (Settings, Graphics, Effects open on their first row; B from Effects
  back on Effects) and dof_menu.sh unchanged.
- Not checked: Driving.xbe (a Z-buffer: Distance's metres do not hold).
- Disk: C: filled up (0 bytes) mid-task; Le Office's snapshots (24 GB in AppData\Local\LeOffice\data\snapshots)
  were cleared at the user's request.

### 2026-11-05 - Ray tracing checked on screen, fixed, and on the Effects page
- The Vulkan ray traced occlusion and light shadows (sdk vulkan.cpp RtCapture/RtBuild/RtPass) drew, but were only
  reachable from graphics.toml and the settings window. Checked with `.local/vk/rt_test.sh` (Quick Game, Fort Knox,
  on/off pairs; RTX 5060): XBE_RT_TEST=3 green over the whole scene (the traced triangles meet the depth).
- Fixed: the gun and hands were darkened (drawn after the game's mid-frame depth clear: now neither traced nor
  shaded); dark lines across flat ground far off (the slope from the depth's neighbours was wrong where the depth steps,
  so rays met the ground they left: now a pixel's place and slope are its own triangle's, the structure's vertices and
  triangles bound to the traced passes).
- Effects > Ray Tracing (Off, Shade, Shade + Shadows; "Not on this GPU" without ray queries), kept across presets.
  Checked in the main menu's Effects page: the row and its help line fit, A keeps it (graphics.toml ray_tracing,
  rt_shadows). docs/graphics_styles.md "Ray tracing".
- Not yet: the cost measured (XBE_PERF) in a busy level; an AMD or Intel GPU; still off by default.

### 2026-11-05 - Paris washed out by the fog (Vulkan, Enhanced)
- Paris at night came out bleached cream with white speckle (on/off pairs from graphics.toml read live: atmosphere
  alone took the frame's mean from 50 to 92). Two faults in the fog (sdk vulkan.cpp, kAtmoHlsl): its colour was the
  brightness-weighted mean of what lights the air, so with a dark sky only the lamps, windows and explosions counted;
  and Driving.xbe's scene is Z-buffered ("fog over a Z-buffered scene", new log line), whose 1 / (1 - z) is no
  distance in metres: every pixel read past 200 m (XBE_ATMO_TEST=2) and got the full fog.
- Now the fog's colour is the sky's own as seen (every sky pixel alike; no sky in view, the picture's mean), and the
  fog is drawn only over a W-buffer (default.xbe's levels); the shafts are unchanged.
- Tested (`.local/paris_run/ab.sh`, a copy of the build so other runs are not disturbed): Paris's Mission Failed view
  with the fog on and off now differs only by the animated menu figure; Enhanced and Original alike in the van. Not
  yet checked: a default.xbe night level with the new fog colour; D3D12 (it has no atmosphere pass).
- Parked (minor): in Enhanced, Paris's road can show a hazy grass-like overlay, with ghost copies of cars on walls,
  like a reflection, fog or smoke. Its effect is not pinned down: frame-to-frame A/B in Paris is unreliable (the
  scripted run moves and zooms the scope; pausing missed). Suspects: screen-space reflections, bounce light, ground
  detail. XBE_SSR_TEST=1-4 makes every opaque surface reflect, so its mask view does not show the real one.

### 2026-11-05 - Weapon sway slowed to the pace of the steps (was too fast)
- src/game/sway.cpp kStep 1.6 -> 2.8 metres a full bob: at a run it was about five dips a second (a bob every 0.8 m),
  now about 1.4 a bob (one dip a step). The eye's bob and the gun's step sway both follow this phase; amounts unchanged.

### 2026-11-05 - Ground detail checked on screen and fixed; on by default
- It never showed: the shader took the vertex programs' clip w as metres (it is the game's scaled W), so its fade
  was 0 everywhere (XBE_DETAIL_SPLIT=2 drew the fade and depth as colours). The view depth is now the mask pass's
  (wlin * wdepth.w * wdepth.z * 4096, 1 m bands confirmed), with nothing nearer than a metre (the gun and hands were
  tagged). Textures are now tagged when first drawn with the setting on (switching it on in play did nothing to
  textures already loaded). XBE_PERF logs the detail draws a frame (90 drawn in a snow map).
- Seen (`.local/vk/detail_pair.sh`, the same place off then on, graphics.toml read again live): grass gets fine blade
  relief over the blurry original, in its own colour; snow gets glints. Strength 1.0 looked like carpet with dark
  pits, 0.6 (the default) visible but quiet. ground_detail is on by default again (Materials: Detail).
- The single-player route scripts broke: the main menu and the mission list keep their last row (Netplay, Equinox);
  `.local/vk/route.sh` clicks NightFire, `.local/vk/exchange.sh` goes up the list, but its timing is not reliable.

### 2026-11-05 - Netplay: a remote player's muzzle flash seen on screen
- NF_FLASH_SHOTS=folder (diagnostic, src/game/lan_play.cpp): the frame a replayed shot's flash is made, a picture
  (flash_N.png, the first 8) when the shooter is within 10 m of player 1; the script's own shots came too late for a
  flash of a frame or two.
- Tested (`.local/vk/flash_net.sh` with it): the puppet firing 4 m away and it and the floor around lit yellow by
  the flash's light (0x6D530). The flash sprite was not made out (its gun behind a balustrade).

### 2026-11-05 - Aim down sights: every weapon checked; grenades keep the game's aim
- NF_ADS_WEAPONS now shoots each weapon at frame 195 of its 200 (at 120 the silenced pistols were still screwing the
  silencer on, which looked like a gun far off the middle) and logs its place at 190.
- Seen aimed (`.local/vk/ads_weapons.sh`): the pistols 2-9 (silenced or not) and the shotgun 28-29 with their sights
  on the middle; 50-51 (flag 0x40) keep the game's scope, as meant. The thrown weapons 52-56 (grenades, remote
  mine) were pulled to the middle and zoomed: src/game/aim.cpp now treats them like the scoped ones (L the game's own
  aim, no sights). Weapon 1 (no gun model) not judged.

### 2026-11-05 - Netplay: an AI bot takes a leaver's place; team games
- A player whose copy leaves mid-match (5 s silent): the host gives its slot back to an AI bot ("Bot N", the
  player's character and team) in the roster; its AI runs again and its state goes out as the host's bot's, and the
  clients move their puppet of it to that bot (RosterFrame). Tested (`.local/vk/leave_test.sh`, three copies, copy
  3 killed 35 s in): host "an AI bot takes slot 5", copy 2 "the host's AI bot in slot 5"; NF_LAN_TRACE=1 showed
  slot 5 moving about (-6.9,-11.5 to -18.7,3.0) after.
- Team games: the scenario options' Continue (0x89006 in 0x88EA0) counts each team from the join records and the bot
  setup and refused a team with only network players ("no players or bots on the MI6 team"). The host's A there now
  has the lobby's network players in free bot records (+0xE in use, +0xF team) for the call, then the setup, the bot
  names and the count put back (the lobby opens inside the call: its saved bot setup is the one from before).
- A client chose no team (it joined before the host picked a team game, and its join page opened again goes straight
  back to the lobby): in a team game a client's A in the lobby now changes its player's team.
- Open places have no name (a team game showed "Open 4" floating over the map).
- Tested (`.local/vk/team_test.sh`, two copies, Team Arena on Skyrail): the check passes with the client on the
  other team, refuses with both on MI6; the client's A moved it MI6 -> Phoenix on both lobbies; in the match the
  host's HUD was blue (MI6), the client's red. Not tested: team kills/scores over the network, Capture The Flag.
  Seen once and not again: the host's team score at -1 with no death logged.

### 2026-11-05 - Ground detail (Vulkan), off until checked on screen
- SDK 7ecb019: the scenery's seamless tiling textures are tagged once by their colours (grass, snow, rock, dirt,
  concrete; mods/details.toml overrides), and up close a procedural detail picture of the kind (normal, brightness)
  shades the game's own colour, fading out by detail_distance. Graphics page: Materials row gains Detail choices.
- Evidence: builds; in a Fort Knox match 30+ textures tagged, every kind's picture made, no shader errors. Not yet
  seen on screen: off/on shots never matched (spawns, bots). So ground_detail defaults to false.
- Next: an off/on pair from a still view (single player), XBE_DETAIL_SPLIT=1 for a same-frame half/half.

### 2026-11-05 - Netplay: other players' shots, muzzle flash and death
- Remote shots were never replayed: ApplyEvents skipped every event without a victim, and a shot has none. Now each
  is fired from its puppet (0x211F0) and, as a bot's fire event does after it (0x18307), gets the muzzle flash: the
  gun's muzzle node (0x13630, 0x13070), a light in the weapon's colour (0x6D530) and the flash sprite 0x2000039
  (0xCD140) unless the weapon has none (entry +0x68 & 0x2000).
- A remote player's death kills its puppet as a bot dies (health at actor +0x90 to 0, message 0x42 via 0x4E320): its
  AI plays the death and respawns it; the think's hook leaves a dead puppet where it fell. Its score slot is held as
  the victim's copy counted it for 4 s (the game would count the bot's death again).
- The roster waits for the host's clock (the first client had been set to 0 of 0 s). The LAN tests' aim skips open
  places.
- Tested (two copies: `.local/vk/fx_fight.sh`, `flash_net.sh`): shots replayed on both copies (logs: projectiles
  made), the host's puppet of the client killed on its death and back after the respawn, bodies on the ground in
  the client's shots, the client's shots seen as red tracers in the host's. The flash itself not made out in a
  screenshot (the puppet too far).

### 2026-11-05 - Netplay: joining a match already running
- src/game/lan_play.cpp: the host keeps the match's roster (its lobby list at 0xA25F0) and sends it (kRoster, with
  the clock: 0x262928 seconds played, 0x26292C the limit) every 500 ms while it plays. A player who joins then gets
  an open place, else one of the host's AI bots: the host makes that slot its puppet, the other clients move their
  puppet of it to the new player (RosterFrame), and the new copy, in the lobby, starts the match from the roster
  with its player as that place's character and team. Every client keeps the host's clock (set when a second off).
- Open places: the bot slots no one has (to six bots, seven players) are bots named "Open" on every copy, puppets
  with no player kept 400 m above the map's middle; so a match with no AI bots can still be joined.
- Tested (`.local/vk/late_join.sh`, two copies on this PC): host Quick Game (3 AI bots, 3 open places), the client
  joined 40 s in: slot 7 (an open place) on the host, both in the match, both clocks 08:42, kills reported alike.
  Not tested: taking an AI bot's place (all six slots used), a team match, two real PCs. The scoreboard lists the
  open places; scores from before the join are not sent.

### 2026-11-05 - Keys 1 to 9 pick a weapon (as in Counter-Strike)
- src/game/weapon_keys.cpp (called from aim.cpp's 0xBD740 hook for player 1, not in the menus): key N asks for the
  Nth weapon carried, in the cycle's order (0xBA280's walk: table entries 1-0x47 whose +4 is 1, carried at actor
  +0x15E + i*12, its variant +0x160, drawable by 0xB9EB0), at the weapon state's +0x53. The keys 1, 2 and 3 were
  second keys of next weapon, next gadget and d-pad left; those now have one key each (Q / G / arrows).
- Tested (NF_SLOT_TEST=1, `.local/vk/mp_match.sh`): carried 1 and 6; key 1 drew 1, key 2 drew 6 again, keys 3-9
  did nothing. Not tested: a real keyboard press, a level with many weapons, Driving.xbe (not added there).

### 2026-11-05 - Menu colours: the background movies blue (PC version), Bond and the text as they are
- Asked for: Bond's face normal, only the backgrounds and menu frames changed, after the PC version, the console's
  animations kept. src/common/menu_colors.cpp (GameInfo::texture_bound): the menus' backgrounds are XMV movies in
  640 x 480 linear YUY2 textures (format 0x24, pitch 1280); each new frame's warm chroma is turned to steel blue in
  guest memory as it is bound (a cool pixel is left; a sparse sum skips a frame already done). On the main menu
  (MenuGeometry page 0x40000002: the context's +0x1B8 keeps 0x40000043 in the front end) Bond's columns (movie x
  382-578, above y 414) keep everything but the background's strong orange (chroma above about 56).
- The SDK's whole-frame pass is gone (it turned the gold text and Bond blue). Setting row: Original / PC Blue.
- Tested (`.local/vk/colors.sh 1`): main menu blue with Bond's skin, the gold text and the sparks' movement as the
  console's; the Settings page's background blue, text and sliders gold. Not checked: other front-end pages' movies,
  Driving.xbe's menus.

### 2026-11-05 - Lighting: fog and bounced light (Graphics' Lighting row)
- Bounced light (sdk Vulkan BouncePass, kBounceHlsl, before the fog and the style pass): at a quarter size each
  pixel averages the scene's colour over a disc about 2 m across at its distance (16 golden-angle taps, weighted by
  how near their distance is to its own), and the scene is lit by it in its colour (most of the gathered brightness
  taken out: a tint and a little light). bounce_light / bounce_strength, default on, 1.0.
- Fog: the light shafts work's fog alone (atmosphere on by default, shaft_strength 0, fog_strength 0.5).
- Graphics page: a Lighting row (Original / Fog / Bounce / Fog + Bounce); 16 rows now, 18 apart from y 76 (at 22
  the last fell off the panel and the help line overlapped Renderer). XBE_GI_TEST=1 left half with, right without;
  2 the gathered light alone (`.local/vk/gi_test.sh N`).
- Tested: split views in Sub Pen (green rooms tinted greener, a corridor taking its walls' red); frames late in a
  two-copy run (both on one GPU): over 25 ms 1-2 of 600 with both off, 2-9 fog, 1-6 fog + bounce: no measurable cost
  there. Seen by eye only in screenshots.

### 2026-11-05 - Subtle weapon sway and head bob (view_motion, on by default)
- src/game/sway.cpp: after 0x24530 places a player's camera (the eye at camera +0x138, from 0xA89C4: the object's
  place plus its height +0x898 less the crouch +0x89C) the eye dips up to 2.2 cm at each step (a bob every 1.6 m
  walked) and sways 1 cm to the side, by the walking speed (whole at 4 m/s), eased, none in the air or after a
  teleport. The gun (src/game/aim.cpp after 0xBD740) lags up to 1.2 cm behind the view's turn and sways with the
  steps; a fifth of it when aimed. Setting view_motion; More Settings' "Weapon Sway" row (the shared page: 12 rows).
- Tested (`.local/vk/sway.sh`, NF_SWAY_TRACE=1: three lines a second): running 6 m/s gave amount 1.0, eye -0.020
  and +0.006; stopped, it faded within a second; a turn (right stick) gave a gun lag of 0.010, easing back to 0.
  Not judged by eye in play yet. The test pad's UP is the D-pad: the stick is LS / RS.

### 2026-11-05 - Aim down sights per weapon: sights on the middle, optics looked through
- src/game/aim.cpp SightOf: each weapon's own move to the middle (by group of its gun's place at the hip; 0xBCAA0
  puts the model's origin from 0.03,0.29,-1.40 to -0.30,-0.13,-0.74), measured from NF_ADS_WEAPONS shots (the
  sight's pixels from the middle times the model's depth over the focal length: 1221 px for a 1410-px view, 1587
  zoomed 1.3x when aiming) and corrected once from aimed shots. Optics (24-25 a scoped rifle at 3x, 42-43 at 2x, the
  launchers 44-49 at 1.6x; not the game's scoped weapons, flag 0x40, which keep its scope) zoom by their own amount,
  their gun moved out of sight at full aim and the game's crosshair left on.
- NF_ADS_WEAPONS=N (diagnostic): player 1 aims, kept alive, and is given each weapon from N in turn (0xBA5E0: actor,
  index, ammo; then the weapon state's +0x53 asked for, the gun's state machine draws it; writing +0x52 alone did not
  change the model), its gun's place logged; with NF_ADS_SHOTS=folder each is shot as w_NN.png
  (`.local/vk/ads_weapons.sh`).
- Seen aimed: 14 and 20 on the middle, 22 and 10 within about 60 px (2% of the view; sway and the draw animation move
  them that much between shots), optics 24/42/44 zoomed with the crosshair. Not seen: weapons 1-9 (no model drawn in
  the test: single player's?), 28, 50-56.

### 2026-11-05 - Everything unlocked in network play
- While a LAN session is on, the game's "all unlocked" byte (0x25D79E: locked entries can be chosen, 0x874DA,
  0x87809, 0x8AC29 ...) and every list entry's own unlock byte (+0x10 of 0x18-byte entries: 0x17C580 12, the maps
  0x17C6A0 8, 0x17C760 3, the scenarios 0x17C7A8 13, the characters 0x17C8E0 29; 66 bytes in all) are set; the
  player's own values are kept and put back when the session ends and around every profile save (0x19A10, which
  writes the byte at 0x19B05), so network play unlocks nothing for good. A profile loaded meanwhile is taken as the
  player's own. The byte alone was not enough: the lists draw entries dimmed and "locked" from their own bytes.
- Tested (`.local/vk/unlock_test.sh`, hosting): Uplink and Demolition, "This scenario is locked." before, now bright
  with their own descriptions ("MI6 attack a site while Phoenix defends").
- Not checked on screen: the character and map lists, the bot setup page (its list at 0x245338 is not touched),
  the unlocks put back after leaving Netplay.

### 2026-11-05 - Focus blur removed; menu colours committed (off)
- Removed ADS focus blur and its settings choice; Aim Down Sights remains Off / On.
- The SDK also holds the menu colours pass (menu_colors, off by default; the main menu's Bond question is still open)
  and the light shafts and fog (atmosphere, off: see the entry below).

### 2026-11-05 - Light shafts and fog: three attempts, stopped (off by default, SDK work uncommitted)
- Asked for: a clearly visible graphics upgrade; the player chose light shafts and fog. sdk/runtime/gpu/vulkan.cpp
  AtmospherePass (kAtmoHlsl) before the style pass: a quarter-size picture of what lights the air (sky by brightness,
  bright surfaces), halved four times; distance fog in the sky's colour; shafts marched toward the light. Settings
  atmosphere / shaft_strength / fog_strength in graphics.toml and the F1 window. XBE_ATMO_TEST=1 left half with,
  right without; 2 the fog and distance; 3 the shafts alone. `.local/vk/atmo_test.sh N`.
- Tried: (1) marching up the slope of the halved picture: smeared copies of the bright sky, washed the scene out;
  (2) each quarter's bright centre as a light, GPU Gems 3 radial gather (4 lights x 32 taps): barely visible in the
  split view, indoors nothing; (3) each quarter's brightest spot, stronger: still barely visible, and the frame rate
  fell from 60 to about 27 (the gather at full size).
- Learned: distances (viewz of the W-buffer) are about 60-100 for a room's far walls; fog 0.006 a unit there gives
  20-40%. Indoor maps have no sky: the lights are small lamps.
- Next, if taken up again: the gather at quarter size and upsampled (cost), real light positions (the game's lights
  or the sun from its sky draws) instead of picking bright pixels, fog as its own cheap option.
- Not committed: the SDK's working tree also holds the unfinished menu colours; both wait.

### 2026-11-05 - LAN play: the host's AI bots, players taking their places
- The host's bot setup (0x245280..0x2452EE as its options left it; Quick Game's 3) is kept when the lobby opens and
  put back if it goes back to the options. The lobby and the match take the network's players first, then as many
  of the host's bots as fit in the six bot slots: a player joining a full game takes a bot's place.
- The bots run only on the host. It sends their places with its players' (by score slot), so on the clients they are
  puppets; their rounds are replayed there, their hits on remote players sent like a player's, their deaths too.
- Hits are now collected from a puppet's own hit list before its think (CollectHits: our rounds' records, damage
  and zone already in them, sent and zeroed): a client's puppets never took 0x3D900 (a bot takes its hits through its
  AI's messages, which a puppet's AI did not handle). A hit from the network on a host's bot goes through 0x3D900 with
  the bot's real handle (0x2FAC0: object, actor, [object+0x20], actor+0xEC: the 8-byte pair crashed), then message
  0x42 at no health (as 0x5DCE8 does). The game counts that death as the bot's own; the host gives the kill to the
  last network hitter within 2 s.
- Tested: Quick Game with one client: host "1 from the network, 3 AI bots", client 4 puppets; bots killed each
  other and both players with the same killers on both copies; NF_LAN_TEST=huntbot (the client stands before a bot,
  aimed) took a bot 74 -> 0 and the host logged "slot 7 died (killer slot 4)", the client "by slot 0" (itself).
  With 4 made-up players and the client, the host kept 1 of its 3 bots (6 bot slots), the client saw 6.
- Not done: joining a match already playing.

### 2026-11-04 - The game's own font enlarged from its own edges (no more blobs)
- With rounded_font off, the font was the glyphs' mask (alpha >= 128) through Scale2x twice, a blur and a cut: small
  letters melted ("s" like "8", a bent "t"; the player: "the weird font"). Now src/common/fonts.cpp's Enlarge takes
  the font's own anti-aliased alpha, enlarges it 8x with Catmull-Rom and firms the edge (slope x2.2 around half), so
  the letters keep the shapes the game drew. Seen on the title screen's copyright text (`.local/vk/font_cmp.sh`).
- Not looked at: Driving.xbe's font (the same path), the in-game HUD at every size.

### 2026-11-04 - LAN play: remote shots replayed from the puppets
- Our players' rounds (0x211F0, args -1, owner, shooter, weapon entry, origin*, direction*, sound: the shooter is the
  second argument, 0x2128F; the puppet check read the first, -1, until now) go to the other copies (kShotEvent:
  origin, direction, sound, weapon), which fire the same round from the shooter's puppet: its tracer and sound. Its
  records in our players' hit lists are zeroed after the collisions (0xAB450): the shooter's copy sends the hits.
- Tested (lan_fight.sh): the client's health still fell 20 a hit (no doubled damage), it died "Defeated by Felix",
  no crash. The replayed tracer was not seen in a screenshot (dark, the view red at death): not confirmed.

### 2026-11-04 - LAN play: hits, deaths and scores over the network; puppets' legs
- Hits: a shot of ours that hits a puppet (its hit record at 0x3D900, from src/game/hit_feedback.cpp) is refused here
  and its record (+8..+0x48: damage, time, directions, type, zone) goes to the victim's copy (kEvent, through the
  host, sent twice, repeats dropped); the shooter sees the hit mark. The victim's copy puts it in its player's hit
  list (0x29560) with a stand-in projectile (class 5, data +0x24 the shooter's puppet, +0x38 the weapon), right after
  the player's collisions (0xAB450): the list is emptied and filled again in the player's update, then walked at
  0xAE39F (0xADD00, 0xADE00, 0x9D700, 0xAC0A0). So health, death and "Defeated by" are the game's own.
- Deaths: a copy whose player's deaths (score slot +0x08) went up tells the others with its killer (+0x28); they count
  it in their score tables as 0xA2211-0xA230E does (kills +0x04, streak +0x10, points +0x18, deaths, killer). The
  puppet does not play the death (calling 0x3D900 from outside crashed; a bot's hit list is walked in its message
  handlers): it goes where its player respawns.
- Legs: the bots' posture requests (0x341C0) are refused for puppets; before their think we ask for standing (0x0A),
  walking (0x08), running (0x1E) or sprinting (0x1F) by their speed over the network, facing the way they move.
- NF_LAN_TEST=aim / meet (diagnostics: turn player 1 to the nearest puppet / stand 5 m before it); `.local/vk/
  lan_fight.sh` runs both copies so, the host firing (RT). Tested: the client's health went 100, 80, 60, 40 and it
  died ("killer slot 4", the host's puppet), twice; the host's HUD showed 2 kills; the host's puppet stood in a pose,
  no longer sliding stiff. Damage per pistol hit was 20 (the record said 5: the game's scaling for a bot shooter,
  0xADE26, or the zone; not checked against split screen).
- Not done: remote shots' flash, sound and tracer; the puppet's death animation; team games over the network.

### 2026-11-04 - LAN lobby: names, up to 8 named players, teams, characters
- Names: a loaded codename (the game's own, [0x17D550] >= 2, 0x17D558), else settings.toml's `player_name`, else the
  Windows user's name; a copy's players 2-4 get " 2".." 4" after it. Two players of the same name are numbered by
  lobby place ("Felix", "Felix 2"), the same on every copy, in the lists and the match.
- The lobby uses both player lists of the summary page (0x10000237/35/38 and 0x10000239/36/3A, four rows each,
  rows used at 0x25E95B / 0x25E95A): by team in team games (join record +4: 1 the first team), else the first list
  then the second (its panel 0x1000023E shown; its title stays "Phoenix": writing text to it crashed). Players past
  eight go to the AI Bots list (no names there).
- Each player's character (join record +8, an index into 0x17CB98) and team are sent; the puppets get them in the
  match through the bot setup (0x245280 = 1, records: name, +0xF team 1/0, +0x10 character), so they look like the
  remote players' choices.
- NF_LAN_FAKE=N (diagnostic, host): N made-up lobby players. Tested with 5 (`FAKE=5 .local/vk/netplay.sh`): both
  copies listed 7 players by name and face in two columns, and the match started with 6 puppets.
- Not tested: a team game over the network (team colours in the match).

### 2026-11-04 - Netplay in the main menu; Multiplayer is Split Screen
- The main menu reads NightFire / Split Screen / Netplay / Codenames / Settings / Quit Game (six rows 22 apart).
  Split Screen is the game's Multiplayer item relabelled (it ends any LAN session); Netplay is a clone of the
  Codenames item, put before it so the menu's order holds.
- The Netplay page is a row set of the shared page (the front end takes no fourth added page): Host Game, Game (the
  games found on this network: PC name, map once chosen, players) and Join Game. Opening it ends any session and
  searches: a broadcast to UDP 27787 (and the loopback) once a second, hosts answer with their PC name, map and
  players; the list follows from the frame. Host Game hosts and Join Game joins the chosen game, each going on to the
  game's own join page (40000019), then the lobby.
- `src/game/lan_play.cpp`: sessions start and end at run time (nf::NetHost, NetJoin, NetStop; NF_LAN still works).
- Tested (`.local/vk/netplay.sh`, two copies, no NF_LAN): the main menu showed the six items; copy 2's Netplay page
  found copy 1 ("DESKTOP-... ") after it chose Host Game; Join Game took it to the lobby; the host's start put both
  in the same match (07000023).
- Not tested: two PCs on a real network (broadcast across machines), a host leaving while clients wait.

### 2026-11-04 - LAN play step 3: the lobby (host's options, player list, host starts)
- The multiplayer summary page (40000049, handler 0x891E0) is the LAN lobby: map, scenario, weapon set, limits and
  friendly fire, and a Playing list with every copy's players by name. The host sets the options through the game's
  own pages (scenario, map, options); clients get them (0x260030-0x26006F, 0x40 bytes, their own humans/bots kept)
  twice a second and the page fills again when they change. A client's A does nothing; its B goes to the main menu.
  The host's A starts the match on every copy (a start count in the lobby packets; each copy runs the page's own A,
  0x8985C), so all load the same map.
- A client's join page goes straight to the lobby (the page change 0x92C40 from 0x87D07 retargeted from the scenario
  list 4000001A). The lobby is filled again from the frame (nf::LanFrame), not inside the menu's update: filling it
  there crashed the menu code.
- Names: each copy sends its joined players' names (slot name 0x25FE38 + i*0x30, character +0x20). A default name
  ("Player N") is numbered by lobby place, so the host is Player 1 and the client Player 2 on both screens; the
  puppets carry the names in the match. Up to 4 show in Playing; more go to the AI Bots list (unnamed there: writing
  a name into a bot row crashed the game).
- Tested (`.local/vk/lan_lobby.sh`): the client reached the lobby from the join page and showed the host's map and
  options (Missile Silo, Arena, 10/10) and both players by name; the host's A started both copies on 07000028.
- Not done: Host Game / Join / server browser menu entries (still NF_LAN), choosing your own name, team modes'
  lists, shots and damage over the network, puppet animation.

### 2026-11-04 - LAN play step 2: client/server network core (NF_LAN)
- `src/game/lan_play.cpp`: UDP client/server, the host the authority. Each copy plays its own players natively (no
  input delay) and sends their state 30 times a second; the host relays everyone's state back 30 times a second.
  Remote players are puppet bots drawn 100 ms in the past, interpolated between snapshots (extrapolated up to 250 ms),
  on the host's clock (clients ping twice a second). A LAN match's bot count is the number of remote players.
- Removed NF_LAN_TEST (the step 1 diagnostic: a puppet standing in front of player 1 and following the camera).
- Puppets no longer shoot: their projectiles are refused at the spawner 0x211F0 (arg 1 the shooter). Blocking the
  target (0x37C10) alone left bots firing through their animation's fire event (0x17C50 -> 0x18302).
- The port is retried for 15 s and a client rejoins after 2 s of silence: the game relaunches itself after the title.
- Tested: two copies on this PC (`.local/vk/lan_match.sh`), each a one-player Quick Game. With NF_LAN_TRACE=1, each
  copy's puppet stood where the other copy's player was; round trip 3-6 ms. Before the spawner fix, both players were
  "Defeated by Kiko" (the puppet); after it, no deaths in 75 s.
- Not done: the map and options are not synced (each copy picks its own), shots/damage/deaths over the network, puppet
  animation (they slide), players joining mid-match, the server browser and Host Game menus, more than 6 remote
  players, several players per copy (sent, but untested).

### 2026-11-04 - Aim down sights (aim_down_sights, on by default)
- More Settings has an "Aim Down Sights" row (Off/On, the page's tenth shared row). Seen on screen: the row toggles, A keeps it (log "aim down sights 0", settings.toml `aim_down_sights = false`), B leaves it as it was.
- L (the right mouse button) no longer enters the game's manual aim (view held, red crosshair moved by the stick):
  `src/game/aim.cpp` clears the controls' slot 19 after 0xDE720, so the weapon state's manual-aim bit never sets and
  shots go through the middle. Instead the zoom target is 1.3 (0xB7190), the first person gun is moved to the middle
  after 0xBD740 (eased in and out) and the crosshair sprite is hidden (0xB6190). Scoped weapons keep the game's scope.
  The mouse slows by the zoom only (not a scope's extra quarter) and the wheel does not zoom while aiming.
- Option `aim_down_sights` in the settings window and settings.toml (`NF_ADS=0/1`); a More Settings row exists but is
  uncommitted with the menu colours work. `NF_ADS_TEST=1` aims player 1 for 240 frames of each 480.
- Evidence: split-screen Quick Game: aiming zooms in, the crosshair goes, the pistol sits centred just below the middle;
  off, the red manual-aim crosshair is back. Not yet seen in a full-screen single player level with a gun, nor with
  the toggle option (controls +3) or a controller.
- Next: tune the gun offset per weapon if some sit badly.

### 2026-11-04 - LAN play, step 1: a bot driven from outside (puppet)
- Plan (the user's): host-authoritative LAN play, a server browser and Host Game in the game's own menus, more than
  4 players, several local split-screen players joining one server. Remote players will be the match's bot
  characters, driven over the network. The engine's cap (RE, `.local/re/game.asm`): 10 characters a match, 4 humans
  (cameras, controls, player arrays sized 4) and 6 bots (0x1D98E0 brains, `cmp eax,6` at 0xA27AA); score and slot
  tables hold 10 (0x262738 / 0x25FE38, 0x30 each, +0x1C the character's object).
- `src/game/lan_play.cpp`: a puppet's think (0x31530) runs the AI's frame (skipped, the bot's body never spawned or
  drew), its target choice (0x37C10) gives none, then its position (object +0x24, metres, +0x28 up) and heading
  (+0x40) are set. NF_LAN_TEST=1: bot slot 4 stands 3 m in front of player 1. Seen (Vulkan, `mp_match.sh 2`): Drake's
  body and name tag there, following player 1, player 1 not shot in 40 s (`.local/vk/shots/lan8.png`, `lan9.png`).
- Not yet: forcing the bot count ([0x260048] at 0xA25F0) did not take, Quick Game still made several bots (the
  per-bot override at 0x245280?); a puppet's facing and walking animation unchecked.
- Next: the network core (UDP, LAN broadcast discovery), then puppets from the network, then the menus.

### 2026-11-04 - The game's own font again by default (rounded_font)
- Both exes' fonts were drawn from Sour Gummy Black; the player wants the original letters. New option rounded_font
  (default off): off, the game's glyphs keep their shapes, enlarged with smooth edges as before (Scale2x, blur, cut);
  on, Sour Gummy Black as before. F1 options, and PC Settings' Sharp Text: Off / On / Rounded (next start).
- Evidence: captures/font_menu3.png (private), Select Codename in the game's letters. Driving.xbe's font follows the
  same switch (DrawEaFont skipped) but was not looked at.

### 2026-11-04 - Effect lights (work in progress, off by default)
- The engine hands over no lights: default.xbe never calls SetLight/LightEnable, all 1,920 draws a frame in a match
  are vertex programs (0 fixed-function lit); the world's light is baked. So lights are taken from the screen.
- SDK vulkan.cpp: added draws (dst ONE, src ONE or SRC_ALPHA, depth tested, not written) drawn again into a glow
  image (RGBA16F, their colour and brightness times distance) over the scene's depth; 16x9 tiles summed (colour,
  brightness centre, mean distance); the 32 brightest listed (pick); a light pass before the style pass lights the
  scene from them (normals from depth, power by the light's distance squared over the surface's). Setting
  effect_lights / light_strength (F1 Particles; menu Particles: Soft + Lights). XBE_LIGHT_TEST 1 the light alone,
  2 the listed lights as dots.
- Seen: 33-55 glowing draws a frame in Fort Knox; the list places lights on the glows (test 2). Cost about 1 ms on
  the scene pass. Not done: the power is untuned (3000 lights the weapon beside a glow, walls metres away stay dark);
  no capture yet with fire, explosions or a muzzle flash; Driving.xbe's perspective depth path looked far too bright.

### 2026-11-04 - Hit feedback: a mark and a tick when your shot hits
- Found: 0x3D900 (cdecl: the hit character's {object, data}, the hit, a scale) is every shot's damage to a character
  on foot; the hit's +8 damage, +0x46 zone (5 head), +0x4C projectile ([+0xBC]+0x24 the shooter); health the data's
  +0x90 float. A debug tweak page (0x82760: Drone Easy..Hard, Head/Arms/Legs/Torso, Armour, MP limbs) led there
  through its multipliers 0x164068..0x16408C, read by 0x3D670. A shooter is player i's when camera i (0x1F661C) has it
  at +0x64 (measured in a 2-player Fort Knox match: 11 hits, one to the head, kills).
- src/game/hit_feedback.cpp: health taken by a player's shot -> app::HitMarker at that player's crosshair (split
  screen: top/bottom for two, quarters above). SDK app/hit_marker.cpp: four strokes drawn over the HUD (white hit,
  amber head, red kill, a short fade) and a synthesized tick mixed into the output. Setting hit_feedback (0 off,
  1 mark, 2 mark and tick, default 2), F1 Display tab. NF_HIT_TEST=1 marks each view every second.
- Evidence: captures/mark_1.png (private): red kill marks centred on both split-screen crosshairs, fading.
- Not verified: the tick heard (no audio capture here); a real single-player hit with this build (the scripted
  runs did not land one; the first diagnostic run's hits were player 1's). No native menu row: More Settings
  holds 9 shared rows, the menu's limit so far.

### 2026-11-04 - Relief (parallax occlusion mapping) for materials

- Materials > PBR + Relief (graphics.toml surface_depth 0-2): the material shader steps the view's ray down the pack's
  height map and moves stage 0's coordinates to the hit, for the game's colour too (game_main is handed the moved tc0).
  pbr.z is now the relief depth (the view angle is taken as 60 degrees in the shader).
- Why not tessellation: the vertex programs output clip positions only; displacing them would crack triangle seams.
- Tested (The Exchange): 60 frames a second with relief on, no shader failures; head-on the stones shift slightly
  (captures/pom_cmp.png, private). A glancing-angle comparison is still missing (the scripted route did not reach one).

### 2026-11-04 - Materials: the HD pack's normal and roughness maps drawn (PBR)

- New option materials (Graphics page > Materials: Original / PBR; settings window; graphics.toml materials,
  material_strength): a pack picture's _n and _r maps, read with it into one image (normal xy, roughness, height, with
  mips), bound to a stage the draw leaves free; the draw's pixel shader variant shades the game's colour by a bumped
  normal (cotangent frame from screen derivatives) and adds a GGX sheen of the roughness. New DrawConstants float4 pbr
  (shared with the D3D backends' HLSL, unused there).
- Tested (The Exchange, Vulkan, the 187-texture 4x pack): about 1,240 material draws a frame, 60.0 frames a second, no
  shader failures; the castle wall shows its stones and mortar in relief with strength 1 against 0 in the same scene
  (captures/pbr_cmp.png, private).
- Limits: no game light directions (the vertex programs keep them), so light comes from above the view and the camera.

### 2026-11-04 - Soft particles (optional)

- New option soft_particles (settings window > Particles; Graphics page > Particles: Original / Soft; graphics.toml):
  blended 3D draws that test but do not write depth are drawn with a variant of their pixel shader that fades them
  where the scene behind is near, against a copy of the depth target made at the frame's first one (the copy is
  bound to a texture stage the draw leaves free). Alpha blended: alpha faded; added: colour and alpha.
- Tested (The Exchange, Vulkan): 76-91 particle draws a frame made soft, 60.0 frames a second, no validation errors.
  Not yet seen side by side on smoke against a wall (the scripted route reaches no smoke).

### 2026-11-04 - HD texture packs made on the player's PC (hd_pack)

- New tool sdk/tools/hd_pack (tools\hd_pack.exe beside the game, with Real-ESRGAN's ncnn Vulkan build and its
  x4plus models, fetched by CMake): the dumped textures upscaled 4x (or 2x from 4x) in one GPU run, wrap-padded
  against seams, alpha apart; --pbr adds normal (_n) and roughness (_r) maps. Incremental.
- Dumping skipped movies: the first dump held 4,902 files, nearly all the menu movie's frames (each frame a new key at
  the same address). A picture whose contents change again within a second is now not dumped: The Exchange's dump is
  187 textures.
- Runtime: a pack picture's _r map gives its mean roughness (PackRoughness); under 0.45 it reflects. A replaced
  texture keeps its game key for reflections (BindGuestTexture's replacement path).
- Tested: 187 Exchange textures 4x + PBR in 45 s (RTX 5060); the game loaded them (texture pack log), the castle wall
  sharper in a before/after shot (captures/hd_cmp.png, private). Normal maps are not drawn (no light/tangent data).
- Next: the tool from the game's menus or the launcher; normal maps once lighting data is recovered.

### 2026-11-04 - More Settings as a page of the game's menus; bullet holes checked

- PC Settings > More Settings opened the ImGui settings window; now a page of the game's own menus: Frame Counter,
  Pause in Background, Hide HUD, Button Pictures, Start in Menus (skip_paris), Keyboard and Mouse (player 1 / alone /
  off), Mouse Look, Language (next start), and Keys and Wheel (A opens the settings window: key binding and the wheel
  are not native yet). Kept with A, saved in settings.toml (seen: mouse look On -> Off written).
- The front end's file takes no fourth page, and a shared page holding every set's rows (28) crashed the menu code
  as the front end loaded (write to guest 0x76). Style Options and More Settings now share one page of nine generic
  rows (0x1000FC71..79): each set's rows map to them by slot, their labels and the page title are strings of ours
  rewritten in place before the page opens, lines a set does not use hidden. Tested: More Settings, then Graphics >
  Style Options (Retro's six rows, its title) in the same session.
- Bullet holes: the game draws them already (The Exchange, 3 pistol shots into snow: a dark mark at the crosshair,
  none before). Improving them (count, lifetime, HD picture) is next if wanted.

### 2026-11-04 - START goes to the menus, not into Paris (skip_paris)

- New option skip_paris (on by default; settings window, settings.toml; NF_SKIP_PARIS): a cold start's START at the
  title opens the main menu instead of launching Driving.xbe into Paris Prelude. src/game/start.cpp overrides 0xDFB50
  (state 9 of the state machine 0x6ACA0 launches Driving.xbe, level 0x9000001 = Paris) and relaunches default.xbe
  with the data Driving.xbe hands back after Paris is quit from its pause screen (offsets 0x4B4 "LOADER READY",
  0x4F4 "paris_mis01", 0x53C 3, bytes 0x5DD 0x615 0x619 0x625 0x979), measured with the new XBE_LAUNCH_DUMP=<folder>
  switch (sdk xapi.cpp: each XLaunchNewImage's 3072 data bytes to a file).
- Cold means not --relaunched: the launch data page is no test, the game has consumed it by the time state 9 runs.
- Tested (Vulkan): START -> main menu; NightFire -> Play without saving -> difficulty -> Paris Prelude -> Driving.xbe
  loads Paris and plays. The harness counts as relaunched, so smoke.sh still goes through Paris; .local route scripts
  set NF_SKIP_PARIS=0.
- Not tested: a saved codename profile, the USA disc.

### 2026-11-04 - Reflections, SMAA, W-buffer depth in the scene pass

- Screen-space reflections (Vulkan; Off/Low/High in the game's Graphics page and the settings window; graphics.toml
  reflections, reflection_strength, reflections_auto). Reflective draws are drawn again into an RGBA16F mask with
  their own shaders (coverage, the distance and its screen gradient from the interpolated W, so the slope is exact);
  the scene pass marches a mirrored ray through the depth. Found by themselves: window glass (blended, a texture whose
  alpha is evenly see-through) and metal (draws the game gives an environment cube map); mods/reflections.toml lists
  more by texture key. Test switch XBE_SSR_TEST: 1 every opaque surface reflects, 2 the mask, 3 the slopes, 4 ray hits.
- Learned: NightFire alpha-blends nearly all world geometry (opaque textures, blending left on), so "opaque" comes
  from the texture's alpha; and its depth is a W-buffer (linear, whole units). The scene pass read every depth as
  perspective Z, which made all distances nearly equal: AO and Toon's depth outlines were doing next to nothing. Now
  read as W (viewz); AO's effect after the fix not yet compared in a still scene.
- SMAA 1x (Ultra, luma edges; iryoku/smaa fetched by CMake, MIT, notice in THIRD_PARTY.txt) on the scene before the HUD;
  Anti-Aliasing > SMAA; quality High/Ultra take SMAA under Vulkan, FXAA under Direct3D. "SMAA ready" in the log, no
  validation run yet.
- Evidence: captures/ (private) mask, slope and hit views in Fort Knox MP; test mode on the Paris road mirrors the lamps.
  Glass auto-detection not yet seen on real glass (none in the rooms reached).
- Next: a level with windows to tune the glass rule; AO before/after in a still scene; validation layers run.

### 2026-11-04 - Split screen checked

- Multiplayer split screen works on Vulkan: Join Game, codenames, Quick Game, 2 and 4 player matches at 60 fps, each
  player's controller its own (shots fired on controllers 2-4 only change that player's ammo), the graphics style on
  every view. Driven with XBE_PAD_PORTS=4 (controllers 2-4 plugged in for the cmd file) and "2:A"-style cmd presses.
- Gap fixed: the keyboard and the first controller were both controller 1, so keyboard + one controller could not
  play split screen. New setting keyboard_alone (settings window, Controls): the keyboard and mouse are controller 1
  alone and controllers start at 2. Not tested with a real controller (none here).
- Cosmetic, not fixed: the Join Game boxes of agents 2-4 show the keyboard's ENTER key when the keyboard is in use
  (the button pictures are one set for every player).

### 2026-11-04 - Bug hunt: menus, mouse, renderers, runtime

- Menus and mouse: the style page's hidden rows (other styles') no longer under the pointer (it cycled their values);
  wheel and stick moves end the hover; a row the d-pad cannot reach is given up, not retried; left/right never used to
  move focus off a value row. PC Settings' Keep leaves a resolution, frame cap, FOV or mouse speed the page cannot show
  as it was. Style Options shows the style just kept (the request is taken a frame later). Menu file copies keyed by
  their bytes and reused per level (were leaked each load); page widget checked live; guest returns masked to a byte.
- Vulkan: occlusion queries returned to the pool, barriers after image-to-buffer copies and between submissions,
  throttling while minimised, no geometry-shader pipelines without the feature, a copy's source looked up again after
  its destination is made; swizzled read-backs not compared as linear (D3D11 and D3D12 too). graphics.toml and texture
  dumps written by temp file and rename; style load and edits locked.
- Runtime: a lost audio device drops to the silent mixer; a settings.toml that does not parse is logged and kept as
  .bak, saves are atomic; renderer fallback tries all three and exits with a message if none starts; language, size
  and render scale checked on load; the mode restored on the monitor it was changed on; swapped mouse buttons; the
  render thread not freed while its worker still runs.
- Checked: Vulkan with validation (no messages), D3D12, PC Settings -> Graphics -> Style Options by mouse clicks.

### 2026-11-04 - Graphics styles: on foot, tuning, shader mod

- On foot (The Exchange, reached by pad commands from Paris's failure): all four styles switched live, 60 fps, HUD
  untouched. Enhanced's filmic curve now keeps mid grey and white (it brightened the night); bloom 0.2 at 0.85;
  Toon keeps 30% of the texture's own shading, 5 bands.
- mods/shaders/scene_post.hlsl replaces the scene pass, read again on change; a broken one falls back to the
  built-in (tested). The settings window driven by scripted presses (Back + Start, d-pad, Reset to defaults saved).
- D3D12 after the shared vertex shader change: 60 fps in Paris, no shader errors.
- Paris gameplay: all ~450 3D draws a frame are vertex programs, none fixed function (new XBE_PERF line). Next for
  modern lighting: recover view-space normals and positions from the vertex programs (shaders.cpp).

### 2026-11-04 - Graphics styles, first milestone (sdk branch vulkan)

- sdk gpu/style.*: Original, Enhanced, Toon, Retro and mods/presets/*.toml presets, graphics.toml (read again when
  edited), the settings window's Graphics Style tab (Reset to defaults; unavailable features shown disabled).
  Format: docs/graphics_styles.md.
- vulkan.cpp: a scene pass before the first 2D draw on the frame's screen target (HUD and menus untouched): AO, bloom,
  tone mapping, grading, sharpen, vignette, grain; toon bands, depth and colour outlines, halftone; retro blocks,
  colour bits, Bayer dither. 3D-only texture filtering overrides; retro vertex snapping (DrawConstants::style, all
  backends zero it). GPU timestamps in the XBE_PERF lines. An in-engine shot for XBE_PAD_CMD runs.
- sdk gpu/texture_pack.*: mods/textures/<content hash>.png replacements (any size) and dumps; tested with a 4x picture
  on the title screen.
- Tested (RTX 5060, validation on, 0 errors): the styles switched live in Paris gameplay, 60 fps in each. GPU per
  frame about 2.1 ms in all; scene pass 0.13 ms enhanced, 0.08 toon, 0.06 retro.
- Not yet: modern lighting, shadows, normal/PBR materials, parallax, tessellation, TAA (need normals, positions and
  lights from the vertex programs: docs/graphics_styles.md); on-foot levels unchecked.

### 2026-11-04 - Vulkan renderer, first milestone (sdk branch vulkan)

- `sdk/runtime/gpu/vulkan.cpp`: Vulkan 1.3 backend (renderer = "vulkan", settings window "Vulkan (experimental)"),
  the D3D11 console path on dynamic rendering and push descriptors; d3d_common's and shaders.cpp's HLSL compiled to
  SPIR-V by DXC at run time (dxcompiler.dll copied beside the exes; Vulkan-Headers and DXC fetched by CMake), SPIR-V
  kept in shadercache.bin, VkPipelineCache in pipelinecache_vk.bin. Falls back to D3D12 when it cannot start.
  Not yet: MSAA, F3 captures, renderer extensions. XBE_VK_DEBUG=1 turns on the validation layer.
- Tested on Windows (RTX 5060, validation on): title, menus, intro movies, Driving.xbe's Paris mission (movie, van
  sniper, Aston Martin chase); no validation errors. Paris gameplay 60.0 fps on both Vulkan and D3D12 (XBE_PERF);
  movies 30 (their own rate).
- 120 fps: the game waits on the console's 60 Hz vertical blank itself; pacing the host faster (tried) still gives 60.
  Past 60 needs frame interpolation or game-speed work, not a renderer change.
- Also: a silent mixer when XAudio2 has no device (a remote session hung in the Paris movie without it), and
  XBE_PAD_SCRIPT / XBE_PAD_CMD (harness pad scripts on Windows, off by default) for unattended runs.
- Next: on-foot levels and HUD/menus compared frame by frame with D3D12; MSAA; launcher packaging of dxcompiler.dll.

### 2026-10-31 - README: graphics settings compared, ultrawide, the menu, Discord

- Harry's 3440x1440 shots (Alpine level) on one spot: the view drifted between them (up to 73 px) and snow and guards
  moved, so each panel's crop is lined up to the default shot by a measured shift (squared differences around the
  crop); nothing else is altered. docs/img: wipe, edges, detail (the castle's windows), sharpness, menu (Harry had the ultrawide shot under Controllers removed).
  MSAA cleans edges, FXAA softens, render scale smoothest, sharpening 100% gritty on stone: confirmed by these shots,
  which also show MSAA, FXAA and sharpening running on Windows with NightFire.
- The wipe is animated as PGR2's: 800x520, 58 frames (50 ms, 600 ms at each end), a 1200x780 crop on the castle.
- README Graphics settings and Community and Support (Discord); v1.0.2 notes: the font, graphics settings.

### 2026-10-31 - SDK at the fork's main 37fa3dc, for v1.0.2

- Brings D3D12's texture stage memo keyed by the header's contents and repeat draws skipping lookups (PGR2's session:
  not exercised headless, not yet run with NightFire), and the launcher window's "about 3 GB". Smoke OK, wincheck OK.
- Next: Harry's Windows build before tagging v1.0.2: prompts keyboard/controller on foot and driving, fonts, driving
  menu panels (one texture, several palettes), snow, sharpening and MSAA/FXAA in F1.

### 2026-10-31 - README: about 3 GB free, plus the game

- Harry's decision, through PGR2's session: "About 3 GB free, plus room for your copy of the game" in both places,
  replacing this README's 15 GB figures (18bd768). When the 7.3 GB copy happens stays on the launcher's line. The
  launcher window says the same on fork main (reaches NightFire with its SDK and a new launcher).

### 2026-10-31 - README: install sizes

- Harry's request to every game (PGR2's fff8d0f). NightFire: disc image 7.3 GB (7,825,162,240 bytes, both discs), copied
  only when it cannot be hard-linked (core/builder.cpp), a folder linked; tools about 1.83 GB and source about 0.33 GB
  are PGR2's measurement of the same toolchain, not NightFire's; 1.4 GB download from a NightFire player's log. So
  about 10 GB or about 2.5 GB installed: estimates, not measured on a NightFire install. The 15 GB first build kept
  (build output on Windows not measured). The source build's "about 15 GB" and Remove the build files (not in this
  README) unchanged.

### 2026-10-31 - MinGit's busybox edition (launcher/toolchain.json)

- From PGR2's session: a PGR2 player's builds stopped at git submodule: MinGit runs it as a script under MSYS2's sh,
  whose forks died on their PC (0xC0000142), CMake's FetchContent too; the busybox edition fixed it there. NightFire
  takes the same entry (size and SHA-256 checked against the download; launcher_cli tool git installs it, cmd/git.exe
  there). Launchers read toolchain.json from main at each build, so no release; each reinstalls Git once. Not run on
  Windows here.

### 2026-10-31 - A player's build stopped on an exFAT-like drive (sdk c4000f8)

- A player's launcher.log (GitHub, v1.0.1, USA disc, E: drive): git "detected dubious ownership" on the launcher's
  own clone, each Build the same. Reproduced with GIT_TEST_ASSUME_DIFFERENT_OWNER=1 in launcher_cli.
- SDK c4000f8: BuildEnv sets safe.directory=* through GIT_CONFIG_COUNT/KEY/VALUE, for the launcher's git, submodules
  and CMake's FetchContent, the player's settings unchanged. With the forced error, a fresh clone and the folder
  that had failed both get through tag and submodule. CMake's FetchContent not run so (same environment).
- The pointer brings 15 commits of the fork's main (XDK 4039, vertex blending, MSAA and FXAA apart, ultrawide). Smoke OK
  for NightFire, PGR2, PGR1 (not EoN, nor PGR1's repro: the launcher is in none of their builds; those commits are
  their sessions' own, gated there); wincheck of src/ OK. Not played on Windows yet.
- docs/releases/v1.0.2.md.

### 2026-10-30 - v1.0.0's launcher offered no update (sdk 7f29c5f)

- Harry: his v1.0.0 launcher offered no v1.0.1; he updated by hand. Both published launchers carry their versions
  (v1.0.0, v1.0.1), and launcher_cli's check acting as v1.0.0 finds v1.0.1 and offers it: the logic holds. The
  check runs once at the launcher's start and dropped any error (offline: no news), so launcher.log could not say
  whether it ran before v1.0.1 was out (a window left open from the failed build) or got no answer.
- SDK 7f29c5f: the start-up check logs what it found (the latest release and whether it is offered, or why there was
  no answer). win/main.cpp checked against mingw's headers (a planted error is caught). Smoke OK for PGR2, PGR1 and
  NightFire. It reaches players with the next release's launcher.

### 2026-10-30 - v1.0.0's launcher built only default.xbe (sdk 66a7066)

- Harry, Windows 11, both discs: the launcher's build stopped preparing: "file SHA1 failed to read file
  .../source/assets/Driving.xbe" (XbeRecomp.cmake:83), then "NightfireDriving is not built by this project". The fork
  main's launcher wrote game.json's one "xbe" alone; the multi-xbe launcher (game.json "xbes" taken from the copy,
  "exes" installed into play: a33f5fa) was only on the fork's eon branches, which EoN's releases build from.
- SDK 66a7066: a33f5fa cherry-picked onto fork main (README conflict only). launcher_cli game + xbes on both images:
  default.xbe and Driving.xbe written with their SHA-1s (USA 7e4b8f8a/8d46a023, Europe 796222e0/e0c41209). Smoke OK
  for PGR2, PGR1 and NightFire (launcher code is in none of their builds; EoN ships this commit already).
- docs/releases/v1.0.1.md; the v1.0.0 launcher builds its own tag, so the fix reaches players as v1.0.1's launcher
  (its update offer).

### 2026-10-30 - Release v1.0.0 (main ef4d3e2)

- MIT License (as EoN's); the repository made public by Harry (GitHub reports visibility public, licence MIT).
- Tag v1.0.0 on ef4d3e2: Actions run 41035237588 built NightFireLauncher.exe (2,322,432 bytes, sha256 9b667c54...)
  and published https://github.com/hk21x/nightfire-recomp/releases/tag/v1.0.0 with docs/releases/v1.0.0.md.
- Harry: the wheel's scope zoom works and resets on leaving the scope.
- Next: Harry's run of the released launcher from an empty folder; reports from players.

### 2026-10-30 - The wheel's scope zoom resets when the scope closes (pc-features)

- Harry: zoomed with the wheel, the view stayed zoomed after leaving the scope, for any weapon. The step changed the
  camera's angle (+0x104), which is lasting game state the scope never touches: 0xAAF21's continuous zoom is a free
  camera's (flag +0x50 bit 0 of [+0xB8]), not the scope's. A weapon's zoom is the player's +0x860, copied into the
  camera's divisor (+0xF4) each frame by the first person camera (0xA8BC6); 0xB7190 eases it to the target +0x864,
  kept to 1 .. +0x868 (the weapon's most, at most 50), the zoom control (slot 6) multiplying the target while held;
  leaving the scope sets it back to 1 (0xB9BD1). A notch now multiplies the target by 1.25 (down by as much), within
  those limits, so the game's easing and reset apply. Smoke OK; Harry to check in a scope.

### 2026-10-30 - The wheel's scope zoom, again (pc-features)

- Harry: the wheel did not zoom the sniper scope. The scope's zoom is continuous: 0xAAF21 adds its zoom control
  (slot 6 through 0x6CA20) times the frame's time to the camera's angle (+0x104, kept to 0.2..3.14) and sets the
  camera (0x25190); a notch's 70 ms d-pad press moved it a few frames' worth at most. Now the input thread counts
  the notches through a scope (nf::ScopeZoom) and the look hook (0xA9CF0, the game's thread) steps the angle 0.8
  times a notch up (nearer), out no further than the scope opened at, setting it through 0x25190. Not seen
  headless (no scope there). Smoke OK.
- Repo check before going public: 65 files tracked, history (58 commits, all by Harry's noreply address) holds no
  game files, generated code, secrets or local paths beyond /home/harry folder names in PROGRESS; biggest blob the
  embedded font (OFL). .gitignore now also ignores *.iso and *.xbe anywhere.

### 2026-10-30 - Keys: 1 the next weapon, 2 the next gadget; Shift and Ctrl pitch in the driving missions (pc-features)

- Harry: on foot 1 switches the weapon and 2 to the gadget: 1 is now the d-pad's right (the next weapon, as the wheel)
  and 2 Black (the next gadget, with G); the d-pad's up and down keep the arrows alone (1 and 2 were theirs), left
  keeps 3. Driving: Left Shift is the left stick's up and Left Ctrl its down, beside the arrows (pitch: Deep
  Descent). Only the defaults change: keys bound anew in settings.toml stay. README tables.

### 2026-10-30 - The USA and Europe (En,Fr,De,Es,It,Nl,Sv) discs (pc-features)

- Harry's two more images (.local/variants, unzipped; extracted with an XDVDFS reader in the scratchpad). Europe
  (En,Fr,De,Es,It,Nl,Sv): the same xbes as the supported PAL release; smoke from the .iso OK on a PAL console.
- USA: its xbes differ in the header alone (signature and certificate, regions 1), so GameInfo::same_code takes them
  and the launcher's game.json lists "NTSC (USA)". Its certificate gives an NTSC console: it plays eurocom/30_fps
  (skip_intro now hides the 30_fps EA and MGM logos too) and driving/misc.viv's NTSC\eng movies. Headless from the
  extracted files and from the .iso: MGM logo, title, attract demo, START into Driving.xbe, the Paris intro, the
  sniper tutorial, the chase, Mission Failed. Smoke's driving window (150 s) ends inside the NTSC movie after the
  tutorial (one draw a frame), so it reads FAIL there; a 240 s run plays on (about 380 draws a frame).

### 2026-10-30 - Scope zoom on the wheel, Quit Game in the main menu (pc-features)

- Mouse wheel through a scope: while player 1's view is zoomed in (its angle under 0.9 of 60 degrees: nf::Zoomed, the
  look code's own test, now src/game/mouse.cpp's ViewZoom) the wheel presses the d-pad's up and down, the NightFire
  style's gadget/zoom (the keyboard plays that style), up a notch away; out of a scope it cycles the weapon as before.
  Not tried in a scope headless (no sniper there): which way zooms in is Harry's to check.
- Quit Game: a fifth main menu item (0x1000FC05, the Codenames item's records cloned as PC Settings'), the five rows
  26 apart from 222 to 326 (two rows up ran into the logo); A calls app::Quit, its help line "Leave the game."
  Headless: down four, A: "quit asked for", Exit (0).
- Driving menus' prompts: already Enter for Select in the pause and on Mission Failed (page 5; headless, keyboard):
  the 2026-10-29 fix covers them; nothing to change.

### 2026-10-29 - The launcher's update prompts (pc-features; sdk 0b5ff49)

- Fork main 0b5ff49 (launcher-update, merged by another session after its gate; launcher only): a release launcher
  knows its tag, offers a newer release (Update the launcher, What's new, Later), builds its own tag and offers a
  rebuild that keeps saves. The check is GitHub's unauthenticated API: a private repository gets no prompt.
  release.yml needs nothing (EoN's). My own gate run: PGR2 and EoN smoke OK; PGR1's collided with the PGR1
  session's run in the same trees (frames stopped), not counted.
- docs/releases/v1.0.0.md: an Updates section, today's features; README: modern buttons, Hide HUD, pause PC Settings.
  Smoke OK.

### 2026-10-29 - Modern button pictures, Hide HUD, scope on ultrawide, the driving exe quarantined (pc-features; sdk ed5e147)

- Modern button pictures (modern_buttons, on): a controller's prompts as today's pad (EoN's shapes, sized by each
  box): coloured A B X Y on dark discs, LB/RB for White/Black, LT/RT, Menu/View, d-pads, sticks; Driving.xbe's P8
  icons take the palette's nearest colours, its LT and RT boxes now the caps' full height (rows 111-130, 152-172).
  Headless: main menu, the bot match's CONTROLS tab, Enemies Vanquished's CONTROLS tab.
- Hide HUD (hide_hud): on foot each view's 2D sprite list (0xD9410 from the 2D pass 0xDAC20, the views 0x1F661C) is
  not drawn while no menu is up; driving, the HUD draw 0xE1970 (ecx [0x23F44C]) is skipped in play. Headless: bot
  match and Enemies Vanquished clean, their pause screens drawn.
- Harry's Deep Descent "crash": Windows Defender quarantined NightfireDriving.exe (Behavior:Win32/DefenseEvasion.A!ml),
  so the handover found no exe and closed. SDK 6eb3183: the new exe is no longer started suspended and resumed (the
  handover event is named by the old instance, --handover=pid); a missing exe gets a message box.
- Harry's scope capture on 21:9: the sight's black bars are textured (32x32, x 0-81 and 559-640, full height), so
  they were squeezed and the widened view showed past them. SDK ed5e147: GameInfo::edge_bars (NightFire on) keeps
  such a bar on the frame's edge, as an untextured fill. Windows renderers only: Harry to check.
- Gate passed (EoN, PGR2, PGR1 smoke, PGR1's frame check); fork main fast-forwarded to ed5e147.
- Harry on Windows: modern button pictures, Hide HUD and the scope on ultrawide all work.
- PC Settings from the pause screen (on foot): a level's menu file (stream 834CC5D0 in the bot match; pages 4B, 42,
  4C, 07, 0C, 44, 46, 2F, 21) has no AV Options page, so the front end's AV records are kept when its file is built
  and the page is made from them into the level's file, with the pause panel (0x10000001) behind it at 16,24
  608x432. The choices list (0x10000028 index 6, type 6: ATTR 0x11 texts 0x16A-0x16C) gets PC SETTINGS (a handle
  GetText answers) and room for four (y 176, h 160); A on it with 3 chosen (0x34) opens the page, B comes back.
  Headless: bot match, START, PC SETTINGS, the page, B, the pause screen again. Driving.xbe's pause (EA's menus)
  has none yet.

### 2026-10-29 - Harry's Windows checks

- Confirmed on Windows: mission loading screens whole (sdk 8acd824), PC Settings (resolution text and applying),
  mounted guns with the mouse, achievements, the icon, the Quit game button (F1), driving at 60 frames a second
  (the vertex buffer wait costs nothing seen).
- Keypads with the left mouse button / Enter and ultrawide confirmed too.
- Release notes written (docs/releases/v1.0.0.md). Next: the first release (launcher run, GitHub release), then polish (Hide HUD, modern button pictures,
  PC Settings in the pause menu, Quit Game in the game's menu, mouse prompts in the driving menus).

### 2026-10-29 - Icon, README, launcher name (pc-features)

- Harry's NightFire PC Port artwork: the round logo as launcher/icon.ico (16 to 256 px; the launcher's and both
  exes' icon), the wide one as the README's banner (docs/img/nightfire-pc-port.webp, 1280x720).
- README rewritten from EoN's: supported release (both SHA-1s), launcher and source builds, PC Settings, on-foot and
  driving keys, features, status, troubleshooting, credits (Sour Gummy, OFL).
- launcher.bat still had the template's short name (Game): now NightFire, so it copies NightFireLauncher.exe.

### 2026-10-29 - Mission loading screens whole on Windows (pc-features; sdk 8acd824)

- Harry's F3 capture of a Driving.xbe loading screen (left half black): the picture is 64x64 P8 tiles on stage 3,
  and the first draws carried later tiles' corners (draw 1 at 384,384; headless, draw 1 is at 0,0). Headless the same
  buffer (00260C40) draws at x 64 and later at x 512 in one frame: EAGL refills its ring of three buffers a tile
  (0xF6E90) within the frame, after a lock that blocks in D3D_BlockOnNonSurfaceResource 0x16CE90, which nothing
  named; its wait (BlockOnTime) returns at once, so the render thread read the refilled buffer.
- SDK 8acd824: D3D_BlockOnNonSurfaceResource hosted as BlockUntilNotBusy (wait for the render thread when the
  buffer is still in a batch), 4831 signature from driving.toml (rescans reproduce both configs; default.xbe has none),
  a placeholder address for other builds. About 155 calls a frame in a race; they wait only on a buffer still in
  flight. Headless (no render thread) it is reached from 0xF6E90; NightFire smoke OK. Gate passed (EoN, PGR2, PGR1
  smoke, PGR1's frame check: a live race); fork main fast-forwarded to it.
- Next: Harry to check a mission loading screen, and the frame rate in a driving mission (a wait per refilled buffer).

### 2026-10-29 - Keypads on the keyboard: the left button and Enter confirm (pc-features)

- Harry: a door's keypad took its digits with Ctrl. Its confirm is the pad's A, which the keyboard has as crouch
  (C, Ctrl, the NightFire style's A). Where Bond is not the player's to move on foot (nf::PlayerFree: 0xA9CF0 has not
  turned player 1 for two frames, as for the guided missile; no menu up), the left mouse button and Enter press A too
  (src/common/menu_mouse.cpp) and A's picture is the left button (src/common/buttons.cpp). Not tried at a keypad
  headless (none reached); the bot match's play and smoke OK.

### 2026-10-29 - PC Settings: the resolution's text (pc-features)

- Harry's screenshot: Resolution showed "WUff". The guest copies of the page's strings were found again by their host
  pointer; the sizes' texts lived in a growing vector, so a new text could take an old one's address and get its copy.
  Now by contents, the sizes' texts in a deque, and every string and the menu file copy in AllocHostContiguous memory
  (AllocSystem's pages are not all readable by recompiled code). Smoke OK.

### 2026-10-29 - PC Settings: display mode, resolution, vertical sync, frame cap (pc-features)

- Harry: add display mode, resolution, VSync and the frame cap to the page. Eleven rows do not fit AV's ten, so the
  page is now made from AV's records (nf::PcPage): its page widget, title, prompt line, help box and pictures kept, its
  rows dropped, ours stacked 24 apart from y 80 from three templates (the music slider with its grey track, the
  subtitles choice, the Restore Defaults button); focus moves by position. Choices carry their values (Display Mode
  the runtime's DisplayMode numbers; Resolution "Desktop" 0 and the monitor's sizes 1.., as the settings window
  lists them; Frame Cap 30, 60, 120, 144, 240, Unlimited). Kept: the settings file, and ApplyDisplay when the mode,
  size or aspect changed. Headless: Frame Cap 60 -> 120 and Display Mode Borderless -> Fullscreen kept; the Edit
  Codename route still opens it. Smoke OK.

### 2026-10-29 - PC Settings as a page of the game's own menus (pc-features)

- Harry: the PC settings in the game's menus, as an option of Edit Codename (Codenames, a profile, its list).
- src/game/pc_settings_page.cpp: the AV Options page (0x40000031) cloned into the menu file copy the builder reads
  (pc_settings_item.cpp), its page and row widgets given our ids (0x4000FC10, 0x1000FC11..), its labels ours: Field of
  View and Mouse Sensitivity (the volume sliders, 21 steps), Invert Mouse, Skip Intro Movies, Sharp Text, Aspect Ratio
  (the choices), More Settings (Restore Defaults' button: the settings window); the rest hidden as AV hides its own.
  Driven through the dispatcher 0x8E320 as AV's handler 0x8D7A0 drives its own (0x4C entered: sliders 0x27/0x69/0x2E,
  choices 0x17, 0x10 text and value, 0x1E by value; 0x4B: read 0x40/0x35, kept to the settings file, back 0x5F;
  0x4E: the row's help line).
- Edit Codename's list (table 0x17CEC8, 7 entries) gets an eighth: its range widened as it is made (0x8C7F0, 0x51:
  0x27 0..7), the rows drawn from a copy of the table (0x7F680 called from 0x8C7F0's hook: its own direct call bypasses
  a hook of 0x7F680) with ours at 7 (label and help through a GetText hook, AV's picture), A on it opens the page. The
  copy lives in AllocHostContiguous memory: AllocSystem's pages are not readable by recompiled code (a fault at
  0xB00F30C8); before the list is made its index is a pointer the game wraps, so the copy is used for real indices
  only. kernel::CallGuest returns the guest's eax (the context is restored): reading ctx.eax after gave stale values.
- The main menu's PC Settings item opens the page too. Headless (Harry's save): Codenames -> Default -> the list's
  eighth entry "PC Settings" with its help -> the page; field of view +3 steps and Invert Mouse on kept ("PC Settings
  kept: field of view 115%, ... invert 1"). Smoke OK.

### 2026-10-29 - Achievements (pc-features)

- The progress map (agent, .local/progress_re.md; checked against Harry's decoded save and a faked Enemies Vanquished
  handover, .local/launch_ev_done.bin): FINDINGS.md "Progress". Deltas on the profile cannot tell a codename load
  from a completion (the profile is restored and the completion scored with no frame between), so each achievement is
  the Results page with mode 6 and the level done, plus its own term.
- src/common/achievement_set.cpp: the 12 missions, gold and platinum on any mission, gold and platinum on every
  mission's best, any mission and Equinox on 00 Agent, every Bond Move in a mission (ACHIEVEMENTS.md). Driving.xbe
  gets the list with a never-met condition (shown, not awarded).
- Headless: the faked Enemies Vanquished handover unlocked Enemies Vanquished, Gold Standard and Shaken, Not Stirred
  (Harry's gold there, its Bond Moves); title -> codename BOND -> mission list unlocked none. Smoke OK.

### 2026-10-29 - Driving font on Windows: replacements for linear textures (pc-features; sdk ed1796c)

- Harry's car screenshot: the objective, timer and ammo still in EA's font, though they draw from the replaced
  82E2E580. D3D12's and D3D11's BindGuestTexture took a title's replacement only for a non-linear texture; LIN_A8's
  coordinates are normalised by the guest's size (texsize), so a larger picture reads the same, and A8 decodes to
  white with alpha as the picture is. SDK ed1796c drops the condition. Gate passed (EoN, PGR2, PGR1 smoke, PGR1's
  frame check; none of them sets a replacement on a linear texture); fork main fast-forwarded to it.
- fonts.cpp: Driving.xbe treats any A8 / LIN_A8 texture with EA's glyph table behind it as a font (the table's size
  from the texture's), remembering those that are not. Smoke OK.

### 2026-10-29 - Driving.xbe's font from Sour Gummy Black too (pc-features)

- EA's font file holds its glyph table before its kerning pairs and the pixels (Driving.xbe font 82E2E580: records at
  82E2D080, 12 bytes each, code order: code u16, width, height, x u16, y u16, advance, x offset, y offset from the
  line's top; 191 codes, the printable Latin-1; then the kerning pairs' count 747 and the pairs, "Buy ERTS", the
  pixels). fonts.cpp's EaTable finds it from the texture's data (back to the 0xFF record whose pairs end just before
  the pixels, then down to the space); DrawEaFont draws each code into its rectangle on the line's baseline (the H's
  bottom), sized to the H, fitted; EA's ligature codes (^ | ~ thorn: ffi ffl ff fl) and much-shrunk glyphs keep the
  game's. Headless Paris: "Driving.xbe's font drawn from Sour Gummy Black". Smoke OK.

### 2026-10-29 - Driving pause prompts: Enter for Select, the Scroll icon (pc-features)

- Harry's car pause screenshot: Select showed the mouse (A's play key) and Scroll a stray mark. The atlas's d-pad
  cross was two pictures (a disc with up and down lit at 220,117-239,136; arrow pieces with up and down lit at
  222,137-241,156), boxed as one; now two. Driving.xbe's nf::MenuKeys: a menu up but the pause's CONTROLS tab (the
  HUD's +0x470 == 2) shows Enter and Backspace. Headless car pause: "ENTER Select, ESC Continue, arrows Next and
  Scroll". Smoke OK.

### 2026-10-29 - default.xbe's font from a real font: Sour Gummy Black (pc-features)

- Harry asked for a real font in place of the upscale and picked Google's Sour Gummy Black (SIL OFL 1.1; the static
  Black from upstream eifetx/Sour-Gummy-Fonts, third_party/fonts with OFL.txt; src/common/font_data.cpp its bytes,
  since the headless harness builds src/ alone). stb_truetype (public domain, third_party/stb) draws it.
- default.xbe's 512x64 font: three rows of slots (columns with a clear one between: 63, 62, 25), row 0 ASCII '!' to
  ']' (the menu's "S" at u 399/512 matches), row 1 '`', a-z and Latin-1, row 2 more Latin-1 (ligatures, quotes and a
  box keep the rebuilt mask; no glyph table was found in memory: none of widths, x as bytes, shorts or floats).
  DrawFont: one size (the game's H), each row's baseline (its H, x, a-grave), centred in the slot, narrowed to fit,
  smaller where it would leave its band (ascenders), the game's glyph where that is under 3/4 (accented capitals).
  NF_FONT_DUMP shows the sheet; a line laid out from it on the game's baselines reads cleanly. Driving.xbe's font
  keeps the smooth rebuild (its own layout). Smoke OK.

### 2026-10-29 - Fonts: stairs on shallow curves ironed out (pc-features)

- Harry (Windows screenshot, the objective text): still a bit stepped. Scale2x fixes 45 degree stairs only; shallower
  ones kept bumps through the Catmull-Rom field. Now Scale2x twice, then a Gaussian half a source texel wide over the
  4x mask (separable), then bilinear 2x cut at 0.5 a pixel wide (src/common/fonts.cpp). NF_FONT_DUMP: "/" one clean
  stroke, "0" a smooth oval, the letters' shapes kept. Smoke OK.
- Harry asked about drawing a real font in place of the upscale: possible (stb_truetype, an OFL font like the game's,
  each glyph fitted to its cell and baseline); not started.

### 2026-10-29 - Keyboard prompts in Driving.xbe; clean fonts in both exes (pc-features)

- Driving.xbe's prompts (the pause CONTROLS tab's icons, REF_DRAWLOG draws 513-518) come from its HUD/menu atlas, a
  256x256 P8 (swizzled indices; FNV-1a 0x96C6096C3CB2BF53) the game binds with three palettes; the icons' palette is
  the one whose entry under the A icon is green. src/common/buttons.cpp now has a layout per atlas: the Xbox
  pictures' boxes (from the atlas drawn with that palette), drawn as keys and coded to that palette's nearest
  colours, at the atlas's size (no replacement: it would not follow the palettes). On a mounted gun RT and LT show
  the mouse's buttons and the reload button R; in the car the right stick (look back) its key. Headless car pause,
  CONTROLS: Fire the left button, Handbrake SPACE, C, Gadget the right button, Steer W A S D, S, Q, W, Z.
- Fonts (src/common/fonts.cpp): default.xbe's 512x64 A8R8G8B8 and Driving.xbe's 256x108 LIN_A8 are pixel art; the
  runtime's sharp 2D (interpolation) left stepped rims. Rebuilt 8x: the mask through Scale2x twice (straight strokes
  kept, stairs made diagonals), then a Catmull-Rom field cut at its middle a pixel wide; the colour the nearest
  opaque texel's; the title's replacement (gpu::SetReplacement) wins over the sharpened one. NF_FONT_DUMP=folder
  writes each as built. Headless: both rebuilt (default 8286A000, Driving 81CDBC80 and 82F50180).
- Smoke OK.

### 2026-10-29 - Keyboard prompts on foot: the button pictures show the keys (pc-features)

- Harry: prompts should show the keys, as in EoN. default.xbe draws every prompt (menus' help lines, the pause
  CONTROLS tab, hints) from one 256x128 A8R8G8B8 swizzled atlas with the PS2 and GameCube pictures too (FNV-1a
  0xF0D6752BB8C56E3B, loaded again per level at another address). src/common/buttons.cpp (EoN's buttons.cpp and
  strokes.h adapted): while the keyboard plays, the Xbox pictures' boxes are drawn as each button's first key (in
  the front end's menus A as ENTER, B as BKSP: nf::MenuKeys; a level's pause shows the keys played, which its
  CONTROLS tab lists), the triggers and White as mouse buttons, the d-pads as arrow keys, the left stick W A S D, the
  right the mouse; written to guest memory and 4x for the renderers (gpu::SetReplacement). The HUD binds the atlas
  once, so it is also redrawn from each poll (RefreshButtons) when the look changes.
- The keyboard's style override (style 0) now holds while the keyboard plays, so the game's controls page and
  prompts name the buttons the keys play; a controller puts the chosen style back, and 0x6ED30 (a player's controls
  packed for the profile and launch data) sees the chosen style.
- Headless: the main menu's "Scroll / Select" as the arrow keys and ENTER; the bot match's CONTROLS tab with every
  picture a key or mouse button (its style text was the controller's: pad script presses are a controller's).
- Driving.xbe: its prompts are glyphs of its LIN_A8 256x108 font tinted by vertex colour (the CONTROLS tab's icons),
  not a picture atlas: not done yet.
- Smoke OK.

### 2026-10-28 - PC controls in Driving.xbe: the mounted guns, the car, the menus (pc-features)

- Harry: the on-foot controls did not reach the vehicle missions (Alpine Escape's snowmobile turret, the Paris van's
  gun, the car). Reverse engineering in .local/driving_input_re.md (angles in turns; the control files are text in
  driving/misc.viv: data\control\povcfg0-7.def for the guns, drivecfg.def for the car).
- Mounted guns (src/driving/mouse.cpp): the gun camera's update 0x857D0 (fastcall) adds the stick's rates to +0x1EC
  yaw and +0x1E8 pitch (up a smaller pitch) and clamps them (0x83190). The mouse's counts go into both before the
  call, the stick's part (+0x290/+0x294, +0x2A0/+0x2A4, ramp +0x1F0/+0x1F4) zeroed; slower zoomed (+0xB4 over 33
  degrees) and a quarter again. Auto-aim [0x2445D0] (default.xbe's option through the launch data, read only by
  0x82E00 from 0x857D0) is off for the call and its lock let go. Headless Alpine Escape with MOUSER: the turret turns.
- Car or gun: the control map's mode [0x1E4560] (1 gun, 0 car), the gun's style [0x1E4568] (nf::GunStyle). On a gun
  the keyboard's car buttons are taken off: the left button fires (RT), the right zooms (LT), R reloads (X in style 0,
  else A); the wheel cycles weapons (its style's controls). In the car: the keys from its controls page (W gas, S
  brake, A/D steer, mouse left fire, Space handbrake, E/mouse right gadget, C camera, Q/Z weapons, V look back), the
  wheel Black/White.
- Menus: the HUD's byte [[0x23F44C]+0x2B0] while an in-game menu takes input (nf::MenuUp): the mouse in Paused and
  Mission Failed. Headless Paris: pause and resume logged.
- mouse_direct in both exes now (NF_DRIVING_EXE dropped). Smoke OK.

### 2026-10-28 - Driving.xbe trap in the car mission (pc-features)

- Harry (Windows, "Enemies Vanquished", 117 s in): "indirect call to 00081D10 has no recompiled function" from
  0x8783F: a state's update (+0x14 of the 0x18-byte entries at [0x1EC368], stored by 0x78770 at 0x79405), reached
  only through that table. Added to config/driving.toml [functions]. A sweep of both xbes' code for function
  addresses stored as immediates (mov [reg+d], imm; push imm) with no recompiled function found no other.
- Headless with Harry's save (.local/hdd_harry, from his hdd.zip; codename BOND): default.xbe's NightFire item ->
  mission list -> Enemies Vanquished wrote .local/launch_enemies_vanquished.bin; 5 minutes of driving with the gas
  held (to a wreck and "Mission Failed"): no trap.
- The car's own controls page (pause, CONTROLS): A fire, B handbrake, Y camera, X gadget, d-pad left/right cycle
  weapons, left stick steer, L brake/reverse, R gas, Black/White weapon forward/back, right stick look back.
- Next: Driving.xbe's controls for the keyboard and mouse (the mounted gun's aim, auto-aim, car vs gun layout, the
  menus): being reverse-engineered (.local/driving_input_re.md).

### 2026-10-28 - Guided missile steered by the mouse; auto-aim off with the mouse (pc-features)

- Harry: a guided missile flew straight (WASD and the mouse did nothing); auto-aim locked onto chests.
- Steering something else, the player's movement update 0xA9CF0 does not turn player 1, so the direct mouse had
  nothing to drive and the right stick got nothing. After 0xDE720, two frames without 0xA9CF0's turn (and no menu
  up), the mouse plays the right stick in the controls (style 0's slots 0, 3 x and 4, 5 y, -1 to 1; 20 counts a
  frame full). Not tried headless (no guided missile in the bot match); the log says "the mouse plays the right stick".
- Auto-aim: the game's option is the byte at +1 of the player's controls (0x1FE6D1; the options page 0x8D0F0 widget
  0x1000011B; +2 in the mode 0x260018 flags); its target search 0xB9950 runs with it. While the keyboard and mouse
  play, 0xB9950 sees both off for the call (the option as set; homing weapons 'P' and 'Q' still lock on).
- Smoke OK; the bot match's direct mouse unchanged.

### 2026-10-28 - On-foot keys from the game's own controls page; the weapon wheel; zoomed mouse a quarter (pc-features)

- Harry: the mouse is excellent; zoomed in (sniper, rocket launcher) it wants a quarter the speed; E for action,
  C or Ctrl crouch, the wheel for weapons, R reload.
- The game's controls page (pause, CONTROLS tab, headless in the bot match) for every style; the style number is
  player +0xE (Classic Bond 7, GoldenEye 6, Thunderball 5, Dr. No 4, Goldfinger 3, Octopussy 2, Moonraker 1,
  NightFire 0). NightFire (the keyboard's): A crouch, X reload/action, Y jump, B next weapon, White alt. fire, Black
  next gadget, L manual aim, R fire, Back vision mode, d-pad left/right cycle weapon, up/down gadget/zoom. The old
  keys were from a guess (Space was crouch, E jump).
- Keys: C/Ctrl crouch, E/R action and reload, Space jump, Q/Backspace next weapon, middle button/F alt. fire, G next
  gadget, V/Tab vision mode, arrows/1-4 the d-pad; the wheel cycles the weapon in play (down the next), Enter selects
  in the menus (menu_mouse.cpp).
- Mouse zoomed in (the camera's angle over its zoom divisor under 0.9 of 60 degrees): a quarter again; the log says
  "zoomed in" and "zoomed out" so Harry's log shows the test works.
- Headless (KEYS, MOUSER, WHEELD in the bot match): the style override (7 -> 0) and the direct mouse logged. Smoke OK.

### 2026-10-28 - The mouse turns the view itself on foot; on-foot play headless; the snow (pc-features)

- Harry: the mouse as the right stick is no FPS mouse (dead spots, a ramp). default.xbe takes the mouse itself now
  (GameInfo::mouse_direct; NF_DRIVING_EXE keeps Driving.xbe on the stick), src/game/mouse.cpp:
  - The movement update 0xA9CF0 (the actor) writes the frame's turn at actor +0x28: radians added to the heading
    next step (right negative), after an acceleration filter (0xB7420) and the stick squared. The look update 0xB7610
    adds to the pitch at +0x838 (camera pitch = pi/2 times it) and sets +0xF0 = 1 (looked; without it the view
    eases back level). After 0xA9CF0 for player 1 the mouse's counts go straight into both, 0.0008 radians a count
    at 100% sensitivity, scaled by the camera's field of view over 60 degrees (the scope).
  - Headless: NF_MOUSE_TEST=100,60,600,619: the camera turned 0.08 a frame and pitched 0.048 a frame to 0.96 and held.
  - The dead zone rewrite (0xE76A0) is gone: the mouse no longer plays the stick on foot.
- Found by probing: getter 0x6CA20 (player, slot): slot k is the float at 0x1FE6E4 + player * 0x158 + 4k (0 turn,
  1 and 2 moving, 3-5 looking); style 0 fills the turn from the right stick. The headless Multiplayer profile turns
  with the left stick (not style 0), so the keyboard's style override stays.
- On-foot play headless: scripts/pad/mp_bots.txt (Multiplayer, one bot, Skyrail) from launch_quit_to_menu.bin.
- The snow (Harry's F3 captures): one fixed-function point-sprite draw of 4096 points with its texture on stage 3,
  stage 0 holding the last draw's (ferns, spheres, stripes). SDK branch nightfire-pointsprite: such a draw uses stage 3
  as its first stage (the Xbox gives point sprite coordinates to stage 3 alone). Gate passed (EoN, PGR2, PGR1 smoke,
  PGR1's frame check; PGR2's point sprites all have a pixel shader, PGR1 and EoN draw none): fork main ca1d0f9.
- Smoke OK.

### 2026-10-28 - The mouse past the game's stick dead zone (pc-features)

- Harry: dead spots in the mouse movement. default.xbe's poll 0xE76A0 makes each stick axis a float, -100 to 100
  (0x1620E0), takes a dead zone off it and stretches the rest (0xE79CF..); slow mouse moves fell inside it. While
  the keyboard and mouse play, controller 1's right stick floats are remade after the poll from the XInputGetState
  shorts, linear, the runtime's 20% mouse start taken off (src/game/controls.cpp). Sensitivity: the overlay's
  mouse sensitivity (10-1000%); the game's top turn rate stays its own.
- Smoke OK. Next: Harry: slow aiming, and the snow captures (F3).

### 2026-10-28 - Keyboard and mouse always as the NightFire controller style (pc-features)

- Harry (Windows): the field of view works; on foot the mouse strafed and moved Bond about, and A/D did not strafe.
- default.xbe's 0xDE720 reads the pads into each player's controls by the player's controller style (u16 at +0xE of
  a 0x158-byte player, port byte at +0x156): style 0 (NightFire) takes moving and strafing from the left stick,
  turning and looking from the right; 2 and 5 (Octopussy, Thunderball by the options' order) swap the x axes. The
  keys and mouse are laid out for style 0, so while controller 1 is on the keyboard and mouse its player is read as
  style 0 for the call (src/game/controls.cpp; the chosen style is logged); a controller keeps its style.
- Smoke OK. Next: Harry on Windows: WASD moves and strafes, the mouse turns and looks, in his save's style.

### 2026-10-28 - PC Settings in the main menu, menu mouse, sharp 2D, ultrawide and field of view (pc-features)

- PC Settings: a fourth main-menu item under NightFire / Multiplayer / Codenames, in the game's own font and focus
  (src/game/pc_settings_item.cpp). Eurocom's menus are a record stream ([0x25FB08]) the page builder 0x93960 reads:
  the builder gets a copy with the Codenames item's records cloned under our id (0x1000FC01) and label, the items moved
  up a row; A on it (dispatcher 0x8E320, message 0x4B) opens the overlay; its help line is set after 0x959E0.
  Headless: shown, focused with its help line, A logs "PC Settings opened from the main menu".
- Mouse in the menus (EoN's MenuMouse): left A, right B, the wheel the d-pad while a menu is up. default.xbe tells
  by Eurocom's menu context (player 1's page widget, 0x25F38C); Driving.xbe's EA menus are not found yet (never up).
- sharpen_2d on (text and HUD sharp at high resolutions; the overlay's sharp_2d turns it off).
- Ultrawide and fov_percent (50-150, both exes; .local/fov_re.md has the reverse engineering):
  - default.xbe (src/game/view.cpp): the D3D projection 0xDD950 and the culling planes 0xDB5C0 get the camera's
    aspect (+0x100) times the frame's width over 16:9 and its angle (+0x104) scaled, for the call alone (the scope's
    zoom and a cutscene camera read those fields back); 0x25190's pixel scales (+0xEC/+0xF0: HUD markers, aim ray)
    follow the field of view, not ultrawide (the HUD is squeezed). Not run headless: no on-foot level is reached
    (the Paris pad ends at "Mission Failed").
  - Driving.xbe (src/driving/view.cpp): 0xE46D0's horizontal angle widened in tangent and its aspect with it (the
    height unchanged); the field of view scales the game's multiplier [[0x1EBFF4]+0x58] around 0x96EB0 and the HUD
    markers 0xCE2B0. Headless Paris: NF_FOV=130 shows more of the scene; NF_ULTRAWIDE=2 (a headless override) gives a
    picture half as wide at the same height, as a 32:9 screen stretches it back. squeeze_screen_programs on: every
    driving draw is a vertex program, the HUD's 640x480 quads too.
- Smoke OK (147 draws a frame).
- Next: Harry on Windows: PC Settings from the main menu, the menus with the mouse, the layout against the game's
  control labels, aspect = fill at 21:9 or 32:9 (edge pop-in, the HUD squeezed, aim at the edges on foot:
  0xB7EB0 keeps 16:9), the field of view; the log should say "out of the menus" in play.

### 2026-10-28 - PC features begun: skip intro, mouse look, keyboard layouts (pc-features)

- skip_intro (settings, NF_SKIP_INTRO=1 headless): default.xbe's EA (25_fps/07370048.xmv) and MGM (07380048) logo
  movies reported missing through GameInfo::hide_file; the game passes over them and "Press START" is up at 15 s
  instead of 26 s; the language select (07350048, its background) stays.
- Mouse look: GameInfo::mouse_look (the mouse plays the right stick, both exes). Keyboard layouts per exe as EoN's
  (kDriving in each hooks.cpp; Driving.xbe's rebinds kept as [driving_keys]): on foot WASD, mouse fire/aim, Space,
  Ctrl, R, E, F, Q, 1-4 the d-pad, Esc/P start, Tab back; driving W accelerate (RT), S brake/reverse (A), A/D steer.
  The game's own control labels (text bank 0): Fire, Jump, Alt. Fire, Crouch, Reload/Action, Look/Strafe, Vision
  Mode, Cycle Weapon, Cycle Gadget/Zoom, Manual Aim, Next Weapon/Gadget, Pause: the layout is to be checked against
  them in game.
- The main menu's text: GetText 0x6D460 (handle = bank << 24 | n, bank bases at [[0x1FEC78] + bank * 4], the
  table at [0x215588]); main menu handles 0x148 NightFire, 0x149 Multiplayer, 0x14A Options (not shown: a hidden
  item), 0x14B Codenames. The menu itself is data-driven; its items are being traced (.local/menu_re.md), as are
  the projection and culling of both engines for ultrawide and FOV (.local/fov_re.md).

### 2026-10-28 - The SDK branch in fork main (main; sdk 41ac590)

EoN's smoke had not failed: its default length is 240 s and 150 s ends inside its intro movie (EoN's session; the
note was wrong). Gate on the nightfire tip: EoN main 2146a11 with its SDK bfbdc0a merged with nightfire SMOKE OK
(race 11736 draws a frame, /home/harry/eon-soak); PGR2 SMOKE OK (2007); PGR1 SMOKE OK (2811) and its frame check a
live race (.local/mm/after.png, draws 6156269 -> 6242839 a second apart). The recompiler change regenerates PGR2's
and PGR1's code byte for byte. Fork main fast-forwarded 0ade542 -> 41ac590 (14 commits); this repository already
pins it.

Next: PC features: skip intro, widescreen, FOV, mouse aim and menus, PC settings in the game's own menu.

### 2026-10-28 - Windows: a long session clean (main)

Harry: "a fair bit of gameplay, all worked OK": 469 s in default.xbe (his saves; the on-foot side), no errors, 60
frames a second paced, slow frames only on shader compiles (1 to 5 a 10 s window, longest 214 ms at boot).

Next: the SDK branch into fork main once EoN's smoke works again (then the launcher builds NightFire); later
missions; PC features (widescreen, FOV, skip intro) as the other ports have.

### 2026-10-28 - default.xbe trapped at 0x11234A: an instruction past the raw data (main; sdk nightfire)

Harry's next run (with his saves, so default.xbe stays in its menus) trapped after 39 s: "undecodable instruction"
at 0x11234A, from the game's 0xE83A8 (D3DX 0x111FC0, a matrix inverse). The D3DX section's raw data ends at
0x11234C, its virtual size at 0x112350: the function's last instruction, ret 0xC (C2 0C 00), has its last byte in
the zero filled tail the loader maps. sdk xbe_recomp: Decode pads with the tail's zeros, and an instruction that
starts in code may run into its own section's tail. Forza's FZFULLUI had the same trap (0x3A9AEA); PGR2, PGR1 and
EoN had none, so their code does not change. Smoke OK.

### 2026-10-28 - Menu panels: Driving.xbe's SetPalette (main; sdk nightfire)

Harry's rebuild: no crash; the scope shows the view inside it now (dim) but not yet as it should, and the menu
panels (Paused, Mission Failed) were black and white noise, also headless. Their textures are P8 (format 0x0B).
0x1669E0 stores a palette in the device's slot [+0xB48 + stage * 4] and pushes 0x41B20 + stage * 0x40 itself (EoN's
QFE 4 SetPalette does the same): D3DDevice_SetPalette, unnamed, so the host never had a palette (and logged
"MakeSpace from 00166A4E"). Named: the Mission Failed panel draws right headless. Smoke OK.

Next: the scope on Windows after this build (the readback of 2026-10-28 made the view show; brightness and the
noise pattern still to compare with the console).

### 2026-10-28 - Leaving Paris crashed; the sniper scope black (main; sdk nightfire)

Harry's Windows run reached the end of the first mission, then crashed: an access violation in the heap's free
(0x110BA6 from RtlFreeHeap) through D3DResource_Release 0x169230 from the game's teardown 0xE8600. Reproduced headless
by quitting a failed mission to the menu (render thread on or off). gdb on the releases: the object is the host's
depth buffer (header 0x00050001: one reference, no internal one), and library code at 0x16D23D had dropped the
device's hold: the frame buffer teardown 0x16D180 inside Driving.xbe's D3DDevice_Reset 0x166230, unnamed, so the
library's Reset ran on the host's buffers. Named (PGR2's 0x1C38D0 at 0.96): the host Reset runs, and the quit now
relaunches into default.xbe, which comes up on the main menu. Smoke OK.

Visual: the sniper scope is black (both renderers). Probes and traces at the scope's centre: draw 158 multiplies
the view by a texture that is near black, the 128x128 target 0x80F07000, a decaying accumulator a black quad dims
each frame and 16384 points (128 x 128) add to. The points' colour stream is the memory of the other 128x128
target 0x80F17000, which the pass before drew from the back buffer: the console reads those texels as vertex
colours; here the GPU's pixels never reached guest memory, the points added zeros and the accumulator stayed black.
sdk: before a draw each vertex stream's data is read back when the GPU drew it, and the D3D12/D3D11 read-backs
write 32 bit swizzled targets in Morton order (they skipped every swizzled one). Headless cannot show it (the null
backend has no GPU targets); wincheck and smoke OK. The icon-atlas frame in Harry's captures runs the same passes.
Ruled out on the way: linear texture coordinates as 0..1 (experiment: no change, the HUD text broke).

Next: Harry rebuilds and checks the scope, the atlas frame and the end of the mission.

### 2026-10-28 - Windows: the first mission was black; the gamma ramp (main; sdk nightfire)

Harry's first Windows build (D3D12, RX 7900 XT) boots, plays the movies and the title, relaunches into Driving.xbe
and then shows black in the first mission. His F3 capture (.local/win) has the frame drawn right (final.bmp: the
van's sniper view), so the picture was lost after the draws. The device's gamma ramps in his memory dump are zeros,
and the game's own base ramp at 0x23FC08 too: Driving.xbe reads the current ramp once with the library's
D3DDevice_GetGammaRamp 0x165E70 (copies [device + 0x259C + slot * 0x300]) and every fade is that ramp scaled
(0xE7740: base * scale + 0.5, then SetGammaRamp). The host never put a ramp in the device: its SetGammaRamp goes to
the GPU only, and the console's linear ramp was never there, so every fade was black. The reference renderer has no
gamma, so headless never showed it. sdk: CreateDevice writes the linear ramp into both slots and SetGammaRamp keeps
them current. Headless the saved base is now linear (0, 32 ... 224); smoke OK.

Next: Harry rebuilds (build.bat updates the sdk) and checks the mission shows.

### 2026-10-28 - Ready for a first Windows build (main; sdk nightfire)

- Gate on the branch: PGR2 smoke OK (2009 draws a frame), PGR1 smoke OK. EoN smoke fails at 1 draw a frame (the
  intro movie in1_palw.xmv never ends) on fork main's SDK, on its own pinned SDK 9e5900b and merged with this branch
  alike, in /home/harry/eon-soak and in EoN's own tree: an EoN problem of its own, so fork main is not moved yet.
- Windows checks: scripts/wincheck.sh clean; scripts/shadercheck.sh compiles all 41 shaders the two xbes made (4 in
  default.xbe's boot, 37 in the Paris level), 0 failed.
- build.bat is EoN's (both exes); the sdk submodule is the pushed nightfire branch's commit, so a clone builds
  without fork main. The launcher builds from fork main and cannot build this game until the merge.

### 2026-10-28 - Pad scripts and a smoke test across the two xbes (main; sdk nightfire)

PAL boot (frames every 120, no input): language select 120..600 (it moves on by itself), the EA and MGM movies to
1200, the title from 1440, "Press START" from about 1560 (24 draws a frame). scripts/pad/smoke.txt: A at 300, START
at 2000; the relaunch follows at about 34 s.

Driving.xbe, Paris (scripts/pad/paris.txt, vblanks from its own start): the sniper tutorial from about 1500 passes
on RT every 40 vblanks with auto-aim; its closing movie leads (about 5100) into the Aston Martin's chase. RT
accelerates; A brakes and reverses (A+RT reversed into the camera). Unsteered, the car meets the barriers and the
mission fails to its menu at about 7200 (A there replays the tutorial). Movie decoding makes frame times vary
between runs, so presses after a movie are timed loosely.

sdk: smoke.sh takes a second xbe (SMOKE_NEXT_GEN/NAME/PAD/SECS): the first run must relaunch, the next xbe runs on
its disk and launch data and gets the drawing checks; SMOKE_SECS. NightFire: SMOKE OK (title 24 draws a frame,
Paris 146); without START it fails ("no relaunch"); PGR1's smoke unchanged on the branch.

Next: steering through the chase (the harness's cmd file takes presses live), PGR2 / EoN smoke and PGR1's frame
check, then the branch into fork main and a Windows build.

### 2026-10-27 - The Paris mission plays headless (main; sdk nightfire)

After the intro movie Driving.xbe stopped three times, each an XDK function only it links:
1. D3D 0x16B160 read the current pixel shader's definition ([[pDevice]+0x370]+8, null with hosted shaders):
   D3DDevice_SetPixelShaderConstant (its opening is PGR2's 0x1C27C0 instruction for instruction, ret 0xC).
2. DSOUND 0x17BE1B -> 0x17BAF4 copies a DSI3DL2LISTENER: IDirectSound_SetI3DL2Listener.
3. 73 s in, a 0x15000 byte contiguous allocation failed with 64156 KB in use. XBE_MEM_LOG=2 (new, sdk) summed the
   live blocks by caller: the game's own 36 MB heap (0x10E818), its D3D resources (454 blocks, 6.9 MB), its render
   targets, and 10 MB the host made: three swap buffers and the depth buffer, as a console makes, plus a copy target
   (1.2 MB) and the DrawVerticesUP ring (4 MB) no console has. sdk: physical memory is mapped to 128 MB as on a
   development kit; the title's allocator and statistics keep the console's 64 MB and the host's own buffers (copy
   target, UP ring, scratch push) come from the part past it. Then 200 s of the mission without a trap. PGR1's
   smoke passes on the branch.

Names signed into sdk/xdk/4831 from both hand configs (.local/resign.sh); both rescans reproduce every hand name.

Next: a pad script through the mission (and the front end: START at the title went straight into mission 1 with the
"Default" profile), a smoke test across the two exes, then PGR2 / EoN smoke and PGR1's frame check before the
branch goes into fork main; a Windows build for Harry.

### 2026-10-27 - XDK 4831 named; title screen; Driving.xbe reached and plays its intro movie (main; sdk nightfire)

Named with three parallel read-only passes (D3D functions and data, the D3D layout, DSOUND), each against PGR2's 5659
bodies (instruction sequences with numbers masked, callees followed) and checked on NightFire's own instructions;
XAPI/XPP by hand. Evidence is in config/nightfire.toml, sdk/xdk/4831/layout.toml and .local/match/result_*.txt.
- D3D: 51 functions by body, render state slot and push methods (byte-identical setters told apart by slot); the
  data (pDevice 0x1117C0, RenderState 0x1119D0, PresentInterval 0x111BCC, ...) found by both D3D passes alike.
- Layout: 5659's way on every 3948/5659 difference (state numbering, Swap, frame buffer pointers, present interval
  global, D3DPRESENT_PARAMETERS to +0x40, D3DVBLANKDATA); device size 0x2C30, miniport at +0x23C0. Spot-checked.
- DSOUND: 41 functions by their chains to the leaf (4831 has no .XTLID). The scan had SetMixBins/SetMixBinVolumes
  swapped. Mix bins as 5659, headroom as 3948.
- Signed into sdk/xdk/4831 from default.xbe (137 of 252); both rescans reproduce the hand names. Driving.xbe's 100
  shared library names compared with default.xbe's by call chain (all 0.97 or more).

Boot, one stop at a time:
1. XInitDevices unnamed: the USB driver touched 0xFED00000. Named (0x15753D) with XInputOpen/Close, XDEVICE_TYPE_GAMEPAD.
2. Direct3D_CreateDevice unnamed: 0xFD001804. With the D3D names: 120 s without a trap, the title screen (REF frames).
3. START launches driving.xbe: EA's front end and driving levels are that xbe, so it is on the main path. Its XPP
   table init 0x183BF4 needed `[functions]` as default.xbe's did; DirectSoundCreateBuffer 0x17C2A0 (default.xbe does
   not link it) and IDirectSoundBuffer_SetFilter 0x17B5E0 (4831's Driving.xbe DSOUND has it, default.xbe's not) named
   by hand after APU faults.
4. Driving.xbe then hung after 60 frames in a stream loop (0x14C800): EA's file layer opened
   `NTSC\eng\paris_intro.mad`, which is not on the disc, and a failed open leaves the stream busy for good. The movies
   are in driving/misc.viv under `pal\<language>\`: the video standard default.xbe passes in the launch data
   (offset 0xE1C) was NTSC. `GameInfo::pal_console` (as EoN): default.xbe then shows the language select and the
   three XMV intro movies, which needed D3DDevice_GetRasterStatus hosted (sdk: the XMV player read the CRTC
   register 0x600808 through the GPU base); its launch data now says PAL and Driving.xbe plays the Paris intro movie.
   Tried first: disc reads paced at 6 MB/s (60 -> 240 frames, not the cause), OBJECT_PATH_NOT_FOUND for a missing
   folder (no change, reverted).

Tools: .local/drv.sh (recompile and run Driving.xbe from .local/launch_title_start_pal.bin), .local/drvstack.sh
(thread stacks of a hang), .local/tools/{seq,align,ident,xmatch,chain}.py (matching).

Next: the crash after the Paris movie in D3D 0x16B160 (Driving.xbe, reading [null + 8]; from game 0xF44A0).

### 2026-10-27 - Repository set up; first scans and first boot attempt (main; sdk nightfire)

Chosen from the two discs dumped today: NightFire is fully extracted and has the same two-xbe structure as EoN, and
its console game differs from the Gearbox PC port. (True Crime: New York City's extract stopped before default.xbe;
its default.xbe from the .iso is XDK 5849 with LTCG D3D8, 116 of 278 functions on a first scan: a later candidate.)

Made from `sdk/templates/game` with EoN's two-xbe wiring: targets `Nightfire` (default.xbe, src/game, config/
nightfire.toml) and `NightfireDriving` (Driving.xbe, src/driving, config/driving.toml), `GameInfo::images`, both exes
in launcher/game.json and release.bat, the workflow title quoted. LESSONS.md collects what PGR2, EoN, PGR1 and Forza
learned.

SDK (branch `nightfire`):
- `xdk/4831`: no signatures existed. Trial scans seeded from each build: 3948 23 functions, 5659 54, 5849 55 (5849
  has more names, the same hits); seeded from 5659 as PGR1 was. layout.toml is 5659's, marked unverified.
- xbe_symbols: default.xbe's scan named 0x15B5D5 XInputGetCapabilities by constants (0.60) though it pops 4 bytes,
  not 8, and wrote an `[abi]` placeholder the recompiler refuses. 0x15B5D5 is an XPP device type's callback (the
  descriptor 0x15731C's +8), not XInputGetCapabilities. Now, in an image without LTCG, a constants match whose ret
  size or entry registers differ is not taken. PGR2, PGR1, EoN (both) and Forza rescans are byte-identical.
- ghidra.sh: `GHIDRA_PROGRAM=driving.exe` reads a second xbe's program.

First headless runs (default.xbe, none.txt):
1. 0.024 s: indirect call to 0x157334 from 0x15757B, which walks XPP's device type table 0x157214..0x157220
   (descriptors 0x1572D8, 0x1572F0, 0x15731C; +4 init, +8 and +0xC callbacks). Only 0x157334 had no function:
   `[functions]` in config/nightfire.toml.
2. Segfault at guest 0xFED00000, the USB host controller's registers: XInitDevices is 0x15753D (allocates the 0xB4
   byte host object, runs the table's inits, starts the controller; stdcall, 2 arguments) and was unnamed, so the
   library's driver ran. The host's XInitDevices needs XPP_GamepadType and the XPP_Current/Changed/Previous layout
   keys, which are still 5659's: naming it belongs to the 4831 pass.

Push buffers (sdk/scripts/ghidra PushBufferScan): all 226 NV2A method stores are in library code, none in the game's
.text, so the game writes no commands by hand. The 0x20001 store in D3D 0x109790 is the lazy state flush's method
value, not a push buffer resource. Whether the game records or runs push buffers stays open until D3D is named.

Recompile: default.xbe 4640 functions (357 direct, 740 by gap fill, 3925 overlapping decodes); the 31 "code
addresses not made functions" at 0x40290-0x403A0 are NV2A method headers in D3D immediates, not code.

Next: Plan 1, the 4831 pass. Name D3D, DSOUND, XPP (XInitDevices 0x15753D first) in default.xbe against PGR1's 3948
and PGR2's 5659 in Ghidra, re-derive the layout, sign, rescan both xbes; then settle push buffers and boot.
