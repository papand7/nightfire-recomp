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

## The launcher

To build the launcher: `launcher\electron\build_service.bat` (the builder service), then in `launcher\electron`
`npm install` and `npm start` to run it, or `npm run dist -- -c.directories.output=C:\Temp\nf-launcher` for the
portable `NightFireLauncher.exe` (an output folder outside the repository: electron-builder fails on one under `out\`
while its files are in use).

The launcher builds the repository named in `launcher/game.json`. While the repository is private, the launcher
cannot download it: build from source as above.
