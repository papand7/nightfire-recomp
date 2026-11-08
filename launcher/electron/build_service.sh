#!/usr/bin/env bash
# Builds NightFireLauncherService (sdk/launcher's builder driven over stdin/stdout, launcher/game.json's game) for
# Linux into out/launcher-svc, where `npm start` and `npm run dist:linux` take it. Needs clang, lld, CMake and Ninja.
set -euo pipefail
cd "$(dirname "$0")/../.."
cmake -S sdk/launcher -B out/launcher-svc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DLAUNCHER_GAME="$PWD/launcher"
cmake --build out/launcher-svc --target launcher_service
echo "BUILD OK: out/launcher-svc/NightFireLauncherService"
