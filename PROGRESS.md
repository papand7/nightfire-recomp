# 007: NightFire recomp - progress log

Newest first. Each entry: date, what changed, what was learned, what's next.

## Status
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
