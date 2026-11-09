# Building from source

For developers. Players: use the launcher, which does all of this for you.

## The game

**You need:** Windows 10 or 11 (64 bit), [Git](https://git-scm.com/download/win), Visual Studio 2022 (Community is
fine) with **Desktop development with C++** (`build.bat` offers to add the Clang and CMake tools), about 3 GB free,
and your own copy of the game (PAL or USA: the disc's files or its `.iso`). For the Vulkan renderer, a GPU with
Vulkan 1.3; for ray tracing, one with ray queries (RTX-class).

1. Clone this repository with its runtime:
   ```
   git clone --recurse-submodules https://github.com/papand7/nightfire-recomp.git
   cd nightfire-recomp
   ```
2. Put your game files in `assets`: `assets\default.xbe` with `Driving.xbe` and the game's folders beside it, or your
   disc's `.iso`.
3. Run `build.bat`. The first build recompiles both programs and takes a while; later builds are quicker.
4. Run `out\build\release\Nightfire.exe`.

## The game on Linux

**You need:** x86-64 Linux with git, clang (14 or newer) and lld, CMake 3.25+, Ninja, pkg-config and the development
packages SDL builds its window and sound with; on Ubuntu or Debian:
```
sudo apt install git curl clang lld cmake ninja-build pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev \
  libdecor-0-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libvulkan1
```
and a GPU with a Vulkan 1.3 driver (Mesa's RADV or ANV, or NVIDIA's). Then, with your game files in `assets/` as above:
```
cmake --preset release
cmake --build out/build/release
out/build/release/Nightfire
```
The build compiles the recompiler for Linux and recompiles both xbes with it; the exes are `Nightfire` and
`NightfireDriving` (the game starts the second one itself), with `libdxcompiler.so` and `libdxil.so` beside them.

## Android (in progress)

Sideloaded only. The app (`android/`, an APK with no game code or files) is a launcher and player; the game comes as a
**game package** built on your PC from your own disc and copied to the phone, where the app imports it (a `.zip` or a
folder) into its private storage. Never share the package: it is your game.

**The package**: the Windows and Linux launchers have an **Android** page. Install the desktop game first, then choose **Build Android package** and a ZIP destination; optionally include your HD textures. Windows runs the builder in WSL. Install the following requirements inside Linux/WSL (the launcher also checks the default `~/Android/Sdk`):

For manual packaging (Linux or WSL): the Android NDK r27 or newer,
clang, CMake 3.25+, Ninja, git, python3 and zip; with your game in `assets/` as above:
```
export ANDROID_HOME=/opt/android        # with ndk/<version> inside, or ANDROID_NDK_HOME=<the NDK>
bash sdk/scripts/android/package.sh -r .                  # arm64-v8a (phones); -a x86_64 for the emulator
```
It builds `xbe_recomp` and `fsr2_shaders` for this machine (`out/build/android-host`), the game for Android
(`out/build/android-arm64-v8a`: `libNightfire.so`, `libNightfireDriving.so`), DXC for Android once (about 20 minutes,
kept in `~/.cache/xbe-android/dxc`, `sdk/scripts/android/build_dxc.sh`), and zips them with `assets/` and a
`package.json` into `out/NightFire-android-package.zip`.

**The app**: JDK 17 and the Android SDK (platform 35, build-tools 35.0.0); in `android/`: `./gradlew assembleRelease`
(`app/build/outputs/apk/release/app-release.apk`, signed with the debug key for now). SDL's Java side is downloaded from
SDL's source at the release the runtime uses (`gradle.properties`); the app refuses a package built with another SDL.

## The launcher

To build the launcher: `launcher\electron\build_service.bat` (the builder service), then in `launcher\electron`
`npm install` and `npm start` to run it, or `npm run dist -- -c.directories.output=C:\Temp\nf-launcher` for the
portable `NightFireLauncher.exe` (an output folder outside the repository: electron-builder fails on one under `out\`
while its files are in use).

On Linux: `bash launcher/electron/build_service.sh`, then in `launcher/electron` `npm install` and `npm start`, or
`npm run dist:linux` for `out/launcher-electron/NightFireLauncher-x86_64.AppImage`. The Linux launcher builds with the
system's clang, CMake and Ninja when they are new enough (else it downloads LLVM's, CMake's and Ninja's Linux
releases, pinned in `launcher/toolchain.json`'s `linux` section) and names the packages to install when the system
lacks git, the C++ library or the window's and sound's development files. Put the AppImage and your `.iso` in one
folder; it works under `NightFire/` beside them.

The launcher builds the repository named in `launcher/game.json`. While the repository is private, the launcher
cannot download it: build from source as above.
