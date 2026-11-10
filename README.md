# 007: NightFire Remastered (PC)

The original Xbox 007: NightFire, running natively on PC and remastered: ray traced lighting, sharper and richer
graphics, HD textures, story co-op, LAN multiplayer, a modern menu and full mouse, keyboard, controller and steering
wheel support. Everything is set from the game's own menus. For **Windows**, with **Linux** and **macOS** in beta.

![The Exchange: Original, Enhanced with ray tracing, Toon and Noir](docs/images/exchange_looks.webp)

*The Exchange in each look: Original, Enhanced with ray traced lighting, Toon and Noir.*

<table>
<tr><th>Original</th><th>Remastered</th></tr>
<tr><td><img src="docs/images/exchange_original.jpg" alt="The Exchange, Original"></td>
<td><img src="docs/images/exchange_remastered.jpg" alt="The Exchange, Remastered"></td></tr>
<tr><td><img src="docs/images/phoenix_original.jpg" alt="Phoenix Fire, Original"></td>
<td><img src="docs/images/phoenix_remastered.jpg" alt="Phoenix Fire, Remastered"></td></tr>
<tr><td><img src="docs/images/nightshift_original.jpg" alt="Night Shift, Original"></td>
<td><img src="docs/images/nightshift_remastered.jpg" alt="Night Shift, Remastered"></td></tr>
</table>

*The Exchange at night in the snow, Phoenix Fire in Tokyo, and Night Shift's garage.*

![Split-screen story co-op in The Exchange](docs/images/coop_split.jpg)

*Story co-op in split screen.*

> [!NOTE]
> No game files are included. You play it from your own copy of the game (the European or US Xbox disc).

## Features

**Graphics**
- **Ray tracing** (RTX-class graphics cards): real lamps and sunlight with true shadows, light bouncing off walls and
  floors, light shafts through windows, reflections and soft shadows in corners.
- **HD textures:** your own copy's textures upscaled 4x by AI on your PC, with optional bumpy, glossy surfaces and
  stones and mortar that stand out.
- **Graphics styles:** Original, Enhanced, Toon, Retro and Noir, switched live from the menu.
- **Explosions and gunfire:** sparks that bounce, flying debris, dust, rising smoke and fireballs.
- **Water:** waves, reflections of the lights, see-through water that darkens with depth, and foam at the edges.
- **Snow:** falling snow, blizzards and footprints that fill in again; snow that sparkles.
- **More detailed ground and walls:** real lumps and hollows in grass, snow, rock and concrete.
- **HDR lighting:** bright snow and lamps glow instead of turning flat white; HDR on HDR screens.
- **Smooth edges:** TAA, SMAA, FXAA and MSAA, plus **FSR 2** and **NVIDIA DLSS** (RTX graphics cards) for more
  frames per second, and **DLAA** for the smoothest edges at full sharpness.
- **Effects:** ambient occlusion, contact shadows, bloom, ice and wet-floor reflections, light from muzzle flashes
  and fire, fog, depth of field, optional motion blur and blood.
- **Ragdolls** (optional, Gameplay & More): enemies go limp when they die and fall with the shot, onto the ground
  and against walls, instead of the death animations.
- **Movies** upscaled sharp, in 4:3 or stretched.
- **Any resolution, ultrawide** screens, field of view, vsync and a frame rate cap.
- Crisp text and HUD at high resolutions, and today's Xbox button pictures.

![Water in Phoenix Base and an explosion in The Exchange](docs/images/water_particles.jpg)

*Water in Phoenix Base, and an explosion with sparks, debris and smoke in The Exchange.*

**Controls**
- **Mouse aiming** like a PC shooter, with the keyboard's keys shown in the game's prompts.
- **Aim down sights**, sprint, hold-to-crouch, keys 1-9 for weapons, weapon sway and hit markers.
- Every key rebindable in the game's **Key Bindings** page.
- Xbox, PlayStation and Switch **controllers**, with rumble.
- **Touch controls** on a touch screen (Windows): an on-screen stick, buttons and look, turned on under the F1
  settings window's **On-screen touch controls**. Android always has them.
- **Tighter Movement** (Controls, Gameplay; off by default): firmer on-foot jumps and falls, and quicker grenades,
  for a snappier feel. In multiplayer the host sets it for everyone.
- **Steering wheels**, pedals, shifters and button boxes in the driving missions, with force feedback, set up in the
  game's **Steering Wheel** page.

**Play together**
- **Story co-op:** play the missions with a friend, in split screen or over the network, with saves and unlocks as
  normal; friendly fire on or off.
- **Network co-op:** the host's address is shown in the game; friends on your network find the game by themselves.
- **LAN multiplayer:** host or join matches on your network, with AI bots, teams and drop-in play.
- Split screen with the keyboard and mouse as a player of their own.

**Menus and more**

![The modern menus: main menu, pause screen, objectives and map select](docs/images/modern_menus.jpg)

*The modern menus: the main menu, the pause screen and its tabs, and choosing a multiplayer map.*

- **Modern menus:** every menu redrawn in the blues of the PS2 cover, made for the mouse. The classic menus are one
  setting away.
- All settings in the game's own menus, under Settings in the main menu and the pause screen.
- **Achievements:** 19, with pop-ups and their own page in More Settings showing what you have unlocked and how
  close you are to the medal ones (see [ACHIEVEMENTS.md](ACHIEVEMENTS.md)).
- **Photo mode:** pause and pick Photo Mode (or press F10), then fly the camera anywhere nearby with the mouse and
  WASD or a controller, change the field of view, exposure, depth of field and look, and take the shot without the HUD.
- **Screenshots:** F12 saves a PNG in the Screenshots folder next to the game.
- **Performance overlay:** FPS, a frame time graph, GPU time and the render resolution in a corner panel.
- **Quick restart:** Restart on the pause screen restarts the mission at once, and you can bind a key for it.
- **Discord status:** shows the mission you are playing (add your Discord application ID in the launcher).
- Adjustable aim-down-sights zoom, and one button to reset every setting to its default (your keys and saves stay).
- Characters' feet stand on slopes and steps; START at the title can go straight to the menus; Hide HUD for
  screenshots.
- Both the European and US discs, on foot and the driving missions.

## The launcher

![NightFire PC Launcher](docs/images/launcher.png)

The **NightFire PC Launcher** sets everything up:
- **Mission:** choose your disc image and press **Install**: the launcher builds the game on your PC from your own
  disc. Then **Play**, and **Update** when a new version is out. Nothing is installed in Windows.
- **Settings:** every option, also outside the game.
- **HD Textures:** collect every texture with one button (no playing needed), or as you play, then make your HD pack
  with one click.
- **Your music:** a folder of your songs becomes the game's soundtrack.

**Download** it from the [latest release](https://github.com/papand7/nightfire-recomp/releases/latest):
- **Windows:** `NightFireLauncher.exe`. Just run it.
- **Linux (beta):** `NightFireLauncher-x86_64.AppImage`. Make it executable (`chmod +x`) and run it. If your system
  is missing something the game needs, the launcher tells you the one command to install it. It needs a graphics card
  with Vulkan (Mesa or NVIDIA drivers). HD texture packs work; DLSS is Windows-only for now.
- **macOS (beta, Apple Silicon only):** `NightFireLauncher-macOS-arm64.dmg`. Open it and drag the launcher to
  Applications. It is not signed, so the first time right-click it and choose **Open**. This is the first macOS
  build: the launcher builds and packages, but the game itself has not been run on a Mac yet. The game draws with
  Vulkan, which macOS only has through MoltenVK, and that path is untested, so expect problems and please report
  them. The HD texture pack tool is built for the Mac too, but untested; DLSS is Windows-only for now.

Building it yourself instead: see [BUILDING.md](BUILDING.md).

## Credits

- **The original PC port,** [hk21x/nightfire-recomp](https://github.com/hk21x/nightfire-recomp) by Harry, which this
  project started from.
- **The recompiler,** [TGP482/XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482.

## License

MIT: see [LICENSE](LICENSE). 007: NightFire is the property of its owners; this project contains none of the game's
code or files.
